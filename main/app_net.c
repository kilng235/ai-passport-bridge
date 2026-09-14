// main/app_net.c —— 全局网络服务实现(见 app_net.h)。
#include "app_net.h"

#include "app_cfg.h"
#include "demo_radio.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "app_net";

static SemaphoreHandle_t s_lock;   // 串行化 start/stop,避免与事件任务抢 Wi-Fi

static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_handlers_registered;

static volatile bool s_desired;       // 期望保持连接(断开时据此自动重连)
static volatile bool s_got_ip;
static volatile bool s_time_synced;
static volatile bool s_sntp_started;
static volatile bool s_mdns_started;
static volatile bool s_mdns_pending;
static char s_ip[16];

// ---------------------------------------------------------------- SNTP
static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
}

static void start_sntp_once(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;

    setenv("TZ", "UTC-8", 1);   // 中国标准时区 CST+8(POSIX 符号取反)
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = true;
    cfg.sync_cb = on_sntp_sync;
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP 启动失败");
        s_sntp_started = false;
    }
}

// ---------------------------------------------------------------- mDNS
// 独立小任务:避免在事件循环里做 mdns_init 的阻塞/分配。
static void mdns_task(void *arg)
{
    (void)arg;
    if (s_desired && !s_mdns_started) {
        if (mdns_init() == ESP_OK) {
            mdns_hostname_set("folopassport");
            mdns_instance_name_set("FoloToy AI Passport");
            mdns_service_add(NULL, "_folopassport", "_tcp", 80, NULL, 0);
            s_mdns_started = true;
            ESP_LOGI(TAG, "mDNS 就绪: folopassport.local (_folopassport._tcp:80)");
        } else {
            ESP_LOGW(TAG, "mdns_init 失败");
        }
    }
    s_mdns_pending = false;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------- 事件
static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id != IP_EVENT_STA_GOT_IP) return;

    s_got_ip = true;
    esp_netif_ip_info_t ip;
    if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip) == ESP_OK) {
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ip.ip));
        ESP_LOGI(TAG, "Wi-Fi 已连接: http://%s/notify", s_ip);
    }

    start_sntp_once();

    if (!s_mdns_started && !s_mdns_pending) {
        s_mdns_pending = true;
        xTaskCreate(mdns_task, "app_net_mdns", 4096, NULL, 3, NULL);
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_got_ip = false;
        if (s_desired) {
            ESP_LOGI(TAG, "Wi-Fi 断开,自动重连…");
            esp_wifi_connect();   // 事件任务里不能阻塞;立即重试
        }
    }
}

// ---------------------------------------------------------------- 生命周期
esp_err_t app_net_init(void)
{
    if (s_lock) return ESP_OK;

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    return demo_radio_network_prepare();
}

esp_err_t app_net_start(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;

    if (s_wifi_started) {
        s_desired = true;
        goto out;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) { err = ESP_ERR_NO_MEM; goto out; }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) goto out;
    s_wifi_initialized = true;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              on_wifi_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) goto out;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              on_ip_event, NULL, &s_ip_handler);
    if (err != ESP_OK) goto out;
    s_handlers_registered = true;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto out;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto out;
    err = esp_wifi_start();
    if (err != ESP_OK) goto out;
    s_wifi_started = true;
    s_desired = true;

    app_cfg_t cfg;
    if (app_cfg_load(&cfg) && app_cfg_wifi_ready(&cfg)) {
        wifi_config_t wifi_cfg = {
            .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK },
        };
        size_t ssid_len = strnlen(cfg.wifi_ssid, sizeof(wifi_cfg.sta.ssid));
        memcpy(wifi_cfg.sta.ssid, cfg.wifi_ssid, ssid_len);
        size_t pass_len = strnlen(cfg.wifi_pass, sizeof(wifi_cfg.sta.password));
        memcpy(wifi_cfg.sta.password, cfg.wifi_pass, pass_len);

        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        if (err != ESP_OK) goto out;
        err = esp_wifi_connect();
        if (err != ESP_OK) goto out;
    } else {
        ESP_LOGW(TAG, "无 Wi-Fi 配置:STA 已启动但不连接(供扫描)");
    }

out:
    if (err != ESP_OK) ESP_LOGE(TAG, "启动 Wi-Fi 失败: %s", esp_err_to_name(err));
    xSemaphoreGive(s_lock);
    return err;
}

void app_net_stop(void)
{
    if (!s_lock) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_desired = false;

    if (s_mdns_started) {
        mdns_free();
        s_mdns_started = false;
    }
    if (s_wifi_started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_handlers_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        s_handlers_registered = false;
    }
    if (s_wifi_initialized) {
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    s_got_ip = false;
    s_sntp_started = false;
    s_ip[0] = '\0';
    xSemaphoreGive(s_lock);
}

// ---------------------------------------------------------------- 查询
bool app_net_is_up(void)           { return s_got_ip; }
bool app_net_time_synced(void)     { return s_time_synced; }

bool app_net_get_ip(char *buf, size_t len)
{
    if (!buf || len == 0 || !s_got_ip) return false;
    snprintf(buf, len, "%s", s_ip);
    return true;
}

esp_err_t app_net_scan_start(void)
{
    if (!s_wifi_started) return ESP_ERR_INVALID_STATE;
    return esp_wifi_scan_start(NULL, false);
}
