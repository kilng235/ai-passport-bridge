// main/app_notify.c —— 通知服务实现。
//
// 依赖 app_net 持有 Wi-Fi:本服务不再自己连接/断开,只在网络在线时挂起
// HTTP /notify。默认开机即常驻(P1),可在配网页临时停用。
#include "app_notify.h"

#include "app_net.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "app_notify";

#define NOTIFY_BODY_MAX 1280   // 会话列表(最多 3 条中文名)会超过 768

// ---- 存储:最新一条 + 未读标志 ----
// 同 kind 的重复推送走"静默刷新"(更新内容与到达时间,不置未读)——桥端把
// 每次工具调用都当作 running 的保鲜刷新,不加这条规则设备会被刷得不停响铃。
static SemaphoreHandle_t s_mutex;
static app_notify_msg_t  s_latest;
static volatile bool     s_has_new;
static int64_t           s_recv_ms;    // 最新一条的到达时刻(esp_timer 毫秒)
static uint32_t          s_version;    // 每次落库自增(含静默刷新),供页面检测内容变化

// ---- 服务状态 ----
static SemaphoreHandle_t s_srv_lock;   // 保护 s_server/s_online(任务 + 配网页并发)
static volatile bool     s_enabled;    // 期望状态
static volatile bool     s_online;     // HTTP 服务在跑
static httpd_handle_t    s_server;

// 最近一次 /notify 的源 IPv4(网络字节序,0 = 未知)。即电脑端桥所在主机,
// 供对讲机语音回传等反向请求定位目标。
static volatile uint32_t s_bridge_ip;

// ---------------------------------------------------------------- HTTP
static esp_err_t notify_post_handler(httpd_req_t *req)
{
    // 记住对端地址:桥推通知的源 IP 就是它自己。
    int fd = httpd_req_to_sockfd(req);
    if (fd >= 0) {
        struct sockaddr_in peer;
        socklen_t len = sizeof(peer);
        if (getpeername(fd, (struct sockaddr *)&peer, &len) == 0) {
            s_bridge_ip = peer.sin_addr.s_addr;
        }
    }

    int total = req->content_len;
    if (total <= 0 || total >= NOTIFY_BODY_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_OK;
    }

    char buf[NOTIFY_BODY_MAX];
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
            return ESP_OK;
        }
        received += r;
    }
    buf[received] = '\0';

    app_notify_msg_t msg;
    if (app_notify_parse(buf, &msg)) {
        app_notify_post(&msg);
        ESP_LOGI(TAG, "notify kind=%s sess=%d count=%d title=\"%s\"",
                 app_notify_kind_str(msg.kind), msg.sess_count, msg.count, msg.title);
    } else {
        ESP_LOGW(TAG, "忽略无效通知 (%d 字节)", received);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static httpd_handle_t server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 5120;
    cfg.max_uri_handlers = 2;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start 失败");
        return NULL;
    }

    const httpd_uri_t notify = {
        .uri = "/notify", .method = HTTP_POST,
        .handler = notify_post_handler, .user_ctx = NULL,
    };
    if (httpd_register_uri_handler(server, &notify) != ESP_OK) {
        ESP_LOGE(TAG, "注册 /notify 失败");
        httpd_stop(server);
        return NULL;
    }
    return server;
}

static void server_stop_locked(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    s_online = false;
}

// ---------------------------------------------------------------- 后台任务
static void notify_task(void *arg)
{
    (void)arg;
    for (;;) {
        bool want = s_enabled && app_net_is_up();

        if (want && !s_online) {
            ESP_LOGI(TAG, "网络在线,启动通知服务…");
            xSemaphoreTake(s_srv_lock, portMAX_DELAY);
            s_server = server_start();
            s_online = (s_server != NULL);
            xSemaphoreGive(s_srv_lock);
            if (s_server) {
                char ip[16];
                if (app_net_get_ip(ip, sizeof(ip)))
                    ESP_LOGI(TAG, "通知服务在线:插件 POST http://%s/notify", ip);
            }
        } else if (!want && s_online) {
            xSemaphoreTake(s_srv_lock, portMAX_DELAY);
            server_stop_locked();
            xSemaphoreGive(s_srv_lock);
            ESP_LOGI(TAG, "通知服务已停");
        }

        vTaskDelay(pdMS_TO_TICKS(want ? 1000 : 500));
    }
}

// ---------------------------------------------------------------- 公共 API
esp_err_t app_notify_init(void)
{
    if (s_mutex) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    s_srv_lock = xSemaphoreCreateMutex();
    if (!s_mutex || !s_srv_lock) return ESP_ERR_NO_MEM;

    if (xTaskCreate(notify_task, "app_notify", 6144, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "创建通知任务失败");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_notify_set_enabled(bool enabled) { s_enabled = enabled; }
bool app_notify_is_enabled(void)          { return s_enabled; }
bool app_notify_is_online(void)           { return s_online; }

void app_notify_stop_server(void)
{
    if (!s_srv_lock) return;
    xSemaphoreTake(s_srv_lock, portMAX_DELAY);
    server_stop_locked();
    xSemaphoreGive(s_srv_lock);
}

void app_notify_post(const app_notify_msg_t *msg)
{
    if (!s_mutex || !msg) return;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        bool same_kind = s_latest.valid && msg->valid && s_latest.kind == msg->kind;
        s_latest = *msg;
        s_latest.valid = true;
        s_recv_ms = esp_timer_get_time() / 1000;
        s_version++;
        if (!same_kind) s_has_new = true;   // kind 变化才置未读(响铃/亮屏/重绘)
        xSemaphoreGive(s_mutex);
    }
}

int64_t app_notify_age_ms(void)
{
    if (!s_mutex) return INT64_MAX;
    int64_t age;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        age = s_latest.valid ? esp_timer_get_time() / 1000 - s_recv_ms : INT64_MAX;
        xSemaphoreGive(s_mutex);
    } else {
        age = INT64_MAX;
    }
    return age;
}

uint32_t app_notify_version(void)
{
    return s_version;
}

bool app_notify_get(app_notify_msg_t *out)
{
    if (!s_mutex || !out) return false;
    bool ok = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_latest.valid) { *out = s_latest; ok = true; }
        xSemaphoreGive(s_mutex);
    }
    return ok;
}

bool app_notify_has_new(void) { return s_has_new; }
void app_notify_mark_read(void) { s_has_new = false; }

bool app_notify_bridge_ip(char *out, size_t out_len)
{
    if (!out || out_len < 8) return false;
    uint32_t ip = s_bridge_ip;
    if (ip == 0) return false;
    esp_ip4_addr_t addr = { .addr = ip };
    esp_ip4addr_ntoa(&addr, out, out_len);
    return out[0] != '\0';
}
