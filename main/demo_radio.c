#include "demo_radio.h"
#include "app_cfg.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "demo_radio";

static bool s_nvs_ready;
static bool s_netif_ready;
static bool s_event_loop_ready;

esp_err_t demo_radio_nvs_prepare(void)
{
    if (s_nvs_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        // 示例不能为了启动无线功能而擦除未来应用可能已经保存的数据。
        ESP_LOGE(TAG, "NVS 初始化失败: %s;未自动擦除分区", esp_err_to_name(err));
        return err;
    }
    s_nvs_ready = true;
    return ESP_OK;
}

esp_err_t demo_radio_network_prepare(void)
{
    if (!s_netif_ready) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK) return err;
        s_netif_ready = true;
    }
    if (!s_event_loop_ready) {
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK) return err;
        s_event_loop_ready = true;
    }
    return ESP_OK;
}

// ---- 共享 Wi-Fi STA(收音机/城市页;额度页因 SNTP 绑定自持一套) ----
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_ip_handler;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_handler_registered;
static volatile bool s_got_ip;

static void sta_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == IP_EVENT_STA_GOT_IP) s_got_ip = true;
}

bool demo_radio_wifi_sta_up(void)
{
    return s_got_ip;
}

esp_err_t demo_radio_wifi_sta_connect(void)
{
    if (s_got_ip) return ESP_OK;

    app_cfg_t cfg;
    if (!app_cfg_load(&cfg) || !app_cfg_wifi_ready(&cfg)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) goto fail;
    s_wifi_initialized = true;

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              sta_ip_event, NULL, &s_ip_handler);
    if (err != ESP_OK) goto fail;
    s_handler_registered = true;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail;

    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    // 目标字段是定长裸缓冲;按字段长度截断拷贝(其余字节已由初始化清零)。
    size_t ssid_len = strnlen(cfg.wifi_ssid, sizeof(wifi_cfg.sta.ssid));
    memcpy(wifi_cfg.sta.ssid, cfg.wifi_ssid, ssid_len);
    size_t pass_len = strnlen(cfg.wifi_pass, sizeof(wifi_cfg.sta.password));
    memcpy(wifi_cfg.sta.password, cfg.wifi_pass, pass_len);

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;

    err = esp_wifi_connect();
    if (err != ESP_OK) goto fail;

    // 阻塞等 IP;调用方是 worker 任务,页面此时保持"正在连接"状态。
    for (int i = 0; i < 30 && !s_got_ip; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!s_got_ip) {
        ESP_LOGW(TAG, "15s 内未拿到 IP");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "Wi-Fi 初始化失败: %s", esp_err_to_name(err));
    return err;
}

void demo_radio_wifi_sta_disconnect(void)
{
    if (s_wifi_started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_handler_registered) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              s_ip_handler);
        s_handler_registered = false;
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
}
