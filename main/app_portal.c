// main/app_portal.c —— 极简 SoftAP 配置门户实现。
// 手机连 FoloPassport-XXXX 热点后,浏览器打开 http://192.168.4.1,填写
// Wi-Fi 与 DeepSeek/MiniMax Key;保存到 NVS 后设备自动重启。
// 无 DNS 劫持、无通配路由,只注册 "/"(GET 表单)与 "/save"(POST)。
#include "app_portal.h"

#include "app_cfg.h"
#include "app_net.h"
#include "app_notify.h"
#include "brightness_logic.h"
#include "demo_radio.h"

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_portal";

#define PORTAL_SSID_PREFIX "FoloPassport-"
#define PORTAL_PASSWORD    "folotoy123"
#define PORTAL_FORM_MAX    2048
#define NVS_NAMESPACE      "pcfg"
#define NVS_KEY            "main"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_IDLE_SEC   "idle_sec"
#define NVS_KEY_VTOKEN     "vtoken"

static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static bool s_wifi_initialized;
static bool s_wifi_started;
// "FoloPassport-" + 4 位十六进制 = 17 字符;缓冲留少量余量避免 SSID 拷贝告警。
static char s_ssid[20] = "FoloPassport-0000";

// ---------------------------------------------------------------- 工具
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// application/x-www-form-urlencoded 解码:'+' -> 空格,"%XX" -> 字节。
static void url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    size_t out = 0;
    for (size_t i = 0; i < src_len && out + 1 < dst_len; i++) {
        if (src[i] == '+') {
            dst[out++] = ' ';
        } else if (src[i] == '%' && i + 2 < src_len) {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                dst[out++] = src[i];
            }
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

// 从 urlencoded 请求体中取出 key 对应值并解码。找到返回 true(值可为空)。
static bool form_get(const char *body, const char *key, char *out, size_t out_len)
{
    if (!body || !key || !out || out_len == 0) return false;
    size_t key_len = strlen(key);
    const char *cursor = body;
    while (*cursor) {
        const char *end = strchr(cursor, '&');
        if (!end) end = cursor + strlen(cursor);
        const char *eq = memchr(cursor, '=', (size_t)(end - cursor));
        if (eq && (size_t)(eq - cursor) == key_len &&
            memcmp(cursor, key, key_len) == 0) {
            url_decode(out, out_len, eq + 1, (size_t)(end - eq - 1));
            return true;
        }
        cursor = *end ? end + 1 : end;
    }
    out[0] = '\0';
    return false;
}

static void send_html_escaped(httpd_req_t *req, const char *text)
{
    const char *cursor = text ? text : "";
    while (*cursor) {
        const char *escaped = NULL;
        switch (*cursor) {
        case '&': escaped = "&amp;"; break;
        case '<': escaped = "&lt;"; break;
        case '>': escaped = "&gt;"; break;
        case '"': escaped = "&quot;"; break;
        default: break;
        }
        if (escaped) {
            httpd_resp_sendstr_chunk(req, escaped);
        } else {
            char one[2] = { *cursor, '\0' };
            httpd_resp_sendstr_chunk(req, one);
        }
        cursor++;
    }
}

// ---------------------------------------------------------------- NVS 存取
bool app_cfg_save(const app_cfg_t *cfg)
{
    if (!cfg || !app_cfg_validate(cfg)) return false;
    if (demo_radio_nvs_prepare() != ESP_OK) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, NVS_KEY, cfg, sizeof(*cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool app_cfg_load(app_cfg_t *cfg)
{
    if (!cfg) return false;
    app_cfg_init(cfg);
    if (demo_radio_nvs_prepare() != ESP_OK) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(*cfg);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, cfg, &len);
    nvs_close(h);
    return err == ESP_OK && len == sizeof(*cfg) && app_cfg_validate(cfg);
}

bool app_cfg_save_brightness(uint8_t percent)
{
    percent = brightness_clamp(percent);
    if (demo_radio_nvs_prepare() != ESP_OK) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_BRIGHTNESS, percent);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool app_cfg_load_brightness(uint8_t *out_percent)
{
    if (!out_percent) return false;
    if (demo_radio_nvs_prepare() != ESP_OK) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t val = 0;
    esp_err_t err = nvs_get_u8(h, NVS_KEY_BRIGHTNESS, &val);
    nvs_close(h);
    if (err != ESP_OK || !brightness_is_valid((int)val)) return false;
    *out_percent = val;
    return true;
}

bool app_cfg_save_idle_timeout(uint16_t sec)
{
    if (demo_radio_nvs_prepare() != ESP_OK) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u16(h, NVS_KEY_IDLE_SEC, sec);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool app_cfg_load_idle_timeout(uint16_t *out_sec)
{
    if (!out_sec) return false;
    if (demo_radio_nvs_prepare() != ESP_OK) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    uint16_t val = 0;
    esp_err_t err = nvs_get_u16(h, NVS_KEY_IDLE_SEC, &val);
    nvs_close(h);
    if (err != ESP_OK) return false;
    *out_sec = val;
    return true;
}

bool app_cfg_save_voice_token(const char *token)
{
    if (demo_radio_nvs_prepare() != ESP_OK) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err;
    if (token && token[0]) {
        err = nvs_set_str(h, NVS_KEY_VTOKEN, token);
    } else {
        err = nvs_erase_key(h, NVS_KEY_VTOKEN);   // 空串 = 清除
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool app_cfg_load_voice_token(char *out, size_t out_len)
{
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    if (demo_radio_nvs_prepare() != ESP_OK) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = out_len;
    esp_err_t err = nvs_get_str(h, NVS_KEY_VTOKEN, out, &len);
    nvs_close(h);
    return err == ESP_OK && out[0] != '\0';
}

// ---------------------------------------------------------------- HTTP 处理
static esp_err_t root_get(httpd_req_t *req)
{
    app_cfg_t cfg;
    if (!app_cfg_load(&cfg)) app_cfg_init(&cfg);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>FoloPassport 配置</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#0b1020;color:#e6ecff;"
        "font:15px/1.55 -apple-system,BlinkMacSystemFont,'PingFang SC','Microsoft YaHei',sans-serif}"
        "header{border-top:4px solid #22d3ee;border-bottom:1px solid #24345c;padding:18px 16px}"
        "main{max-width:680px;margin:auto;padding:0 16px 40px}"
        "h1{font-size:21px;margin:0}h2{font-size:16px;color:#22d3ee;margin:22px 0 6px}"
        ".sub{color:#8fa1af}.row{display:grid;gap:6px;padding:8px 0;border-bottom:1px solid #1d2b4d}"
        "label{color:#aeb9d8;font-size:13px}input{width:100%;background:#131c38;color:#e6ecff;"
        "border:1px solid #2e3f6b;border-radius:6px;padding:10px;font-size:15px}"
        ".hint{color:#7c8ab5;font-size:12px}.btn{display:block;width:100%;background:#22d3ee;"
        "color:#0b1020;border:0;border-radius:8px;padding:13px;font-size:16px;font-weight:700;"
        "margin-top:22px}</style></head><body>"
        "<header><h1>FoloPassport 配置</h1>"
        "<div class=\"sub\">本地配置 · 密钥只保存在设备上</div></header><main>"
        "<form method=\"post\" action=\"/save\">"
        "<h2>Wi-Fi</h2>"
        "<div class=\"row\"><label>附近 Wi-Fi（点击选择，仅 2.4G）</label>"
        "<div style=\"display:flex;gap:8px\">"
        "<select id=\"ssid_sel\" style=\"flex:1;background:#131c38;color:#e6ecff;"
        "border:1px solid #2e3f6b;border-radius:6px;padding:10px;font-size:15px\">"
        "<option value=\"\">正在扫描…</option></select>"
        "<button type=\"button\" style=\"background:#22d3ee;color:#0b1020;border:0;"
        "border-radius:6px;padding:0 14px;font-size:15px;font-weight:700\" "
        "onclick=\"scanWifi()\">刷新</button></div>"
        "<div class=\"hint\" id=\"scan_hint\"></div></div>"
        "<div class=\"row\"><label>Wi-Fi 名称（SSID）</label><input name=\"ssid\" maxlength=\"32\" value=\"");
    send_html_escaped(req, cfg.wifi_ssid);
    httpd_resp_sendstr_chunk(req,
        "\"></div><div class=\"row\"><label>Wi-Fi 密码（留空表示不修改）</label>"
        "<input type=\"password\" name=\"pass\" maxlength=\"64\"></div>"
        "<h2>API Key</h2>"
        "<div class=\"row\"><label>DeepSeek Key</label>"
        "<input name=\"key_deepseek\" maxlength=\"191\" placeholder=\"sk-...\"></div>"
        "<div class=\"row\"><label>MiniMax Key</label>"
        "<input name=\"key_minimax\" maxlength=\"191\" placeholder=\"sk-...\"></div>"
        "<div class=\"row\"><label>语音 Token（可选，与电脑端 PASSPORT_VOICE_TOKEN 一致）</label>"
        "<input name=\"voice_token\" maxlength=\"64\" placeholder=\"留空表示不修改\"></div>"
        "<button class=\"btn\" type=\"submit\">保存</button>"
        "</form>"
        "<form method=\"post\" action=\"/reboot\" style=\"margin-top:14px\">"
        "<button class=\"btn\" type=\"submit\" "
        "style=\"background:#334155;color:#e6ecff\">重启设备</button>"
        "</form>"
        "<p class=\"hint\">密钥只保存在设备 NVS 中；保存不会自动重启，"
        "离开本页或点“重启设备”后生效。</p>"
        "<script>"
        "var sel=document.getElementById('ssid_sel');"
        "var ssidInput=document.querySelector('input[name=ssid]');"
        "function scanWifi(){"
        "sel.innerHTML='<option value=\"\">正在扫描…</option>';"
        "document.getElementById('scan_hint').textContent='';"
        "fetch('/scan').then(function(r){return r.json();}).then(function(a){"
        "sel.innerHTML='<option value=\"\">请选择附近 Wi-Fi…</option>';"
        "a.forEach(function(x){var o=document.createElement('option');"
        "o.value=x.ssid;o.textContent=x.ssid+' ('+x.rssi+'dBm)';sel.appendChild(o);});"
        "document.getElementById('scan_hint').textContent="
        "(a.length?a.length+' 个网络（仅 2.4G）':'未发现网络');"
        "}).catch(function(e){sel.innerHTML='<option value=\"\">扫描失败</option>';"
        "document.getElementById('scan_hint').textContent='扫描失败，请重试';});}"
        "sel.addEventListener('change',function(){if(this.value)ssidInput.value=this.value;});"
        "scanWifi();"
        "</script>"
        "</main></body></html>");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(400));   // 让保存成功的响应先发完
    esp_restart();
}

static esp_err_t save_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= PORTAL_FORM_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请求过大");
        return ESP_OK;
    }

    // 堆分配并按声明的长度读满,避免 httpd 任务栈溢出。
    char *body = malloc((size_t)req->content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
        return ESP_OK;
    }
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "接收失败");
            return ESP_OK;
        }
        received += n;
    }
    body[received] = '\0';

    // 基于已有配置增量覆盖:表单里空字段表示保持原值(Key 不回填,避免误清空)。
    app_cfg_t cfg;
    if (!app_cfg_load(&cfg)) app_cfg_init(&cfg);

    char v[APP_CFG_KEY_MAX];
    if (form_get(body, "ssid", v, sizeof(v)) && v[0]) {
        char pass[APP_CFG_PASS_MAX];
        if (!form_get(body, "pass", pass, sizeof(pass)) || !pass[0]) {
            snprintf(pass, sizeof(pass), "%s", cfg.wifi_pass);   // 保留旧密码
        }
        app_cfg_set_wifi(&cfg, v, pass);
    }
    if (form_get(body, "key_deepseek", v, sizeof(v)) && v[0])
        app_cfg_set_key(&cfg, "deepseek", v);
    if (form_get(body, "key_minimax", v, sizeof(v)) && v[0])
        app_cfg_set_key(&cfg, "minimax", v);
    if (form_get(body, "voice_token", v, sizeof(v)) && v[0])
        app_cfg_save_voice_token(v);   // 独立 NVS 键,与主配置 blob 无关
    free(body);

    if (!app_cfg_wifi_ready(&cfg)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请填写 Wi-Fi 名称");
        return ESP_OK;
    }
    if (!app_cfg_save(&cfg)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "保存失败");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "配置已保存: ssid=%s", cfg.wifi_ssid);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width\"></head>"
        "<body style=\"background:#0b1020;color:#e6ecff;font:15px sans-serif;"
        "text-align:center;padding-top:70px\">"
        "<p style=\"color:#34d399;font-size:20px;font-weight:700\">保存成功</p>"
        "<p>配置已写入，未重启。</p>"
        "<p><a style=\"color:#22d3ee\" href=\"/\">返回继续修改</a></p>"
        "<form method=\"post\" action=\"/reboot\" style=\"margin-top:18px\">"
        "<button style=\"background:#22d3ee;color:#0b1020;border:0;border-radius:8px;"
        "padding:12px 22px;font-size:16px;font-weight:700\">立即重启设备</button>"
        "</form></body></html>");
    return ESP_OK;
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width\"></head>"
        "<body style=\"background:#0b1020;color:#e6ecff;font:15px sans-serif;"
        "text-align:center;padding-top:80px\">"
        "<p style=\"color:#22d3ee;font-size:20px;font-weight:700\">正在重启…</p>"
        "<p>设备将断开热点并在几秒后重新启动。</p></body></html>");
    xTaskCreate(restart_task, "portal_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

#define PORTAL_SCAN_MAX 20

static int cmp_rssi_desc(const void *a, const void *b)
{
    const wifi_ap_record_t *ra = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *rb = (const wifi_ap_record_t *)b;
    return (int)rb->rssi - (int)ra->rssi;   // 信号强的排前面
}

// GET /scan —— 阻塞扫描附近 2.4G AP,去重(同 SSID 取最强)后按 RSSI 降序返回 JSON。
static esp_err_t scan_get(httpd_req_t *req)
{
    wifi_ap_record_t *records =
        malloc(sizeof(wifi_ap_record_t) * PORTAL_SCAN_MAX);
    if (!records) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
        return ESP_OK;
    }

    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_wifi_started) {
        wifi_scan_config_t scan_cfg = { 0 };
        scan_cfg.show_hidden = false;
        scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        scan_cfg.scan_time.active.min = 80;
        scan_cfg.scan_time.active.max = 240;
        err = esp_wifi_scan_start(&scan_cfg, true);   // 阻塞至扫描结束
    }
    if (err != ESP_OK) {
        free(records);
        ESP_LOGE(TAG, "扫描失败: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "扫描失败");
        return ESP_OK;
    }

    uint16_t count = PORTAL_SCAN_MAX;
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        free(records);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "读取失败");
        return ESP_OK;
    }

    qsort(records, count, sizeof(records[0]), cmp_rssi_desc);

    cJSON *arr = cJSON_CreateArray();
    for (uint16_t i = 0; i < count; i++) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0') continue;   // 跳过隐藏 SSID

        bool dup = false;
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, arr) {
            cJSON *name = cJSON_GetObjectItem(it, "ssid");
            if (cJSON_IsString(name) && strcmp(name->valuestring, ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", ssid);
        cJSON_AddNumberToObject(o, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(o, "ch", records[i].primary);
        cJSON_AddItemToArray(arr, o);
    }
    free(records);

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "序列化失败");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return ESP_OK;
}

static esp_err_t register_handlers(void)
{
    const httpd_uri_t handlers[] = {
        { .uri = "/", .method = HTTP_GET, .handler = root_get },
        { .uri = "/scan", .method = HTTP_GET, .handler = scan_get },
        { .uri = "/save", .method = HTTP_POST, .handler = save_post },
        { .uri = "/reboot", .method = HTTP_POST, .handler = reboot_post },
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(s_server, &handlers[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------- 启停
esp_err_t app_portal_start(void)
{
    if (s_wifi_started) return ESP_OK;

    // 暂停常驻 STA 与通知 HTTP,交出 80 端口和 Wi-Fi,供 AP 门户接管。
    app_notify_set_enabled(false);
    app_notify_stop_server();
    app_net_stop();

    ESP_LOGI(TAG, "启动前堆: free=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    esp_err_t err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK)
        snprintf(s_ssid, sizeof(s_ssid), "%s%02X%02X",
                 PORTAL_SSID_PREFIX, mac[4], mac[5]);
    else
        snprintf(s_ssid, sizeof(s_ssid), "%s0000", PORTAL_SSID_PREFIX);

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) return ESP_ERR_NO_MEM;

    // APSTA:保留手机可连的热点,同时用 STA 扫描附近 2.4G Wi-Fi。
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) goto fail;
    s_wifi_initialized = true;

    wifi_config_t config = { 0 };
    snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid), "%s", s_ssid);
    snprintf((char *)config.ap.password, sizeof(config.ap.password), "%s",
             PORTAL_PASSWORD);
    config.ap.ssid_len = strlen(s_ssid);
    config.ap.channel = 6;
    config.ap.max_connection = 2;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.required = false;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.stack_size = 6144;
    server_config.max_uri_handlers = 6;
    err = httpd_start(&s_server, &server_config);
    if (err != ESP_OK) goto fail;
    err = register_handlers();
    if (err != ESP_OK) goto fail;

    ESP_LOGI(TAG, "配置门户已启动: %s / %s / http://192.168.4.1 (heap free=%u)",
             s_ssid, PORTAL_PASSWORD, (unsigned)esp_get_free_heap_size());
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "配置门户启动失败: %s", esp_err_to_name(err));
    app_portal_stop();
    return err;
}

void app_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_wifi_initialized) {
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }

    // 恢复常驻网络与通知服务(配网保存后即使不重启也能生效)。
    app_net_start();
    app_notify_set_enabled(true);
}

bool app_portal_running(void)
{
    return s_wifi_started && s_server != NULL;
}

const char *app_portal_ssid(void)
{
    return s_ssid;
}

const char *app_portal_password(void)
{
    return PORTAL_PASSWORD;
}
