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
        "<title>FoloPassport · 控制台</title><style>"
        // === 8-bit Cyber Arcade 极客控制台视觉 ===
        ":root{--bg:#0a0d14;--panel:#131826;--panel2:#0f1320;--grid:#1c2236;"
        "--amber:#f59e0b;--amber2:#b27a08;--cyan:#06b6d4;--cyan2:#0891b2;"
        "--green:#10b981;--red:#ef4444;--muted:#6b7693;--text:#dbe5ff;--ink:#020409}"
        "*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
        "html,body{margin:0;padding:0;background:var(--bg);color:var(--text);"
        "font-family:'SF Mono','Cascadia Code','JetBrains Mono','Consolas',"
        "'Menlo','PingFang SC','Microsoft YaHei',monospace;min-height:100%}"
        // 8-bit 像素扫描线网格背景
        "body{background-image:"
        "linear-gradient(rgba(6,182,212,0.04) 1px,transparent 1px),"
        "linear-gradient(90deg,rgba(6,182,212,0.04) 1px,transparent 1px);"
        "background-size:24px 24px;background-attachment:fixed;"
        "background-position:-1px -1px}"
        // 顶部霓虹发光 Header
        ".top{border-top:4px solid var(--amber);border-bottom:1px solid #1f2937;"
        "background:linear-gradient(180deg,#0f1320 0%,#0a0d14 100%);"
        "padding:18px 16px 14px;position:relative;overflow:hidden}"
        ".top::before{content:'';position:absolute;inset:0;"
        "background:radial-gradient(circle at 50% 0%,rgba(245,158,11,0.18),transparent 60%);"
        "pointer-events:none}"
        ".top h1{margin:0;font-size:18px;letter-spacing:2px;color:var(--amber);"
        "font-weight:700;text-shadow:0 0 8px rgba(245,158,11,0.5)}"
        ".top h1 .c{color:var(--cyan);text-shadow:0 0 8px rgba(6,182,212,0.5)}"
        ".sub{color:var(--muted);font-size:11px;margin-top:4px;letter-spacing:1px}"
        // 设备 HUD 铭牌
        ".hud{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px;font-size:11px}"
        ".hud span{background:#0f1320;border:1px solid #1f2937;"
        "padding:4px 8px;color:var(--cyan);letter-spacing:1px}"
        ".hud span b{color:var(--amber);font-weight:400}"
        // 主体
        "main{max-width:680px;margin:0 auto;padding:14px 16px 60px}"
        // 8-bit 像素卡片(直角硬边 + 双层描边 + 微发光)
        ".card{background:var(--panel);border:1px solid #2a3149;"
        "box-shadow:0 0 0 1px var(--ink),0 0 0 2px #1a2038 inset,0 0 16px rgba(6,182,212,0.08);"
        "padding:16px;margin-bottom:18px;position:relative}"
        ".card::before{content:'';position:absolute;left:0;right:0;top:0;height:1px;"
        "background:linear-gradient(90deg,transparent,var(--cyan),transparent);"
        "opacity:0.5}"
        ".card h2{font-size:13px;margin:0 0 12px;color:var(--amber);"
        "letter-spacing:3px;display:flex;align-items:center;gap:8px}"
        ".card h2::before{content:'▶';color:var(--cyan);font-size:11px}"
        ".row{margin-bottom:14px}.row:last-child{margin-bottom:0}"
        ".row label{display:block;color:var(--muted);font-size:11px;"
        "margin-bottom:5px;letter-spacing:1px;text-transform:uppercase}"
        // 输入框极客等宽字体 + Focus 发光
        "input[type=text],input[type=password],select{width:100%;box-sizing:border-box;"
        "background:var(--ink);color:var(--text);border:1px solid #2a3149;"
        "padding:11px 12px;font-size:14px;font-family:inherit;"
        "outline:none;border-radius:0;transition:border-color .15s,box-shadow .15s}"
        "input:focus,select:focus{border-color:var(--cyan);"
        "box-shadow:0 0 0 1px var(--cyan),0 0 12px rgba(6,182,212,0.3)}"
        "input::placeholder{color:#4a5577}"
        "select{appearance:none;-webkit-appearance:none;"
        "background-image:linear-gradient(45deg,transparent 50%,var(--amber) 50%),"
        "linear-gradient(135deg,var(--amber) 50%,transparent 50%);"
        "background-position:calc(100% - 16px) 50%,calc(100% - 11px) 50%;"
        "background-size:5px 5px,5px 5px;background-repeat:no-repeat;"
        "padding-right:30px}"
        // 行内布局(Wi-Fi 列表 + 刷新按钮)
        ".row-flex{display:flex;gap:8px}"
        ".row-flex > *{flex:1;min-width:0}"
        // 信号格
        ".rssi{font-family:inherit;display:inline-block;width:14px;"
        "text-align:center;margin-right:4px;color:var(--cyan);"
        "text-shadow:0 0 6px rgba(6,182,212,0.4)}"
        ".rssi.weak{color:#7a4a16;text-shadow:none}"
        // Key 输入框右侧密码可见切换按钮
        ".keywrap{position:relative}.keywrap input{padding-right:44px}"
        ".key-toggle{position:absolute;right:8px;top:50%;transform:translateY(-50%);"
        "width:30px;height:30px;background:transparent;border:1px solid #2a3149;"
        "color:var(--cyan);font-size:14px;cursor:pointer;display:flex;"
        "align-items:center;justify-content:center}"
        ".key-toggle:hover{border-color:var(--amber);color:var(--amber)}"
        // 街机风实体按键
        ".actions{display:flex;gap:10px;margin-top:18px;flex-wrap:wrap}"
        "button.btn{font-family:inherit;cursor:pointer;letter-spacing:1px;"
        "position:relative;border:0;border-radius:0;padding:13px 22px;"
        "font-size:14px;font-weight:700;transition:transform .08s,filter .15s;"
        "box-shadow:0 4px 0 rgba(0,0,0,0.4),0 0 0 1px rgba(255,255,255,0.06) inset}"
        "button.btn:hover{filter:brightness(1.1)}"
        "button.btn:active{transform:translateY(2px);box-shadow:0 2px 0 rgba(0,0,0,0.4)}"
        "button.btn.primary{background:linear-gradient(180deg,#fbbf24,var(--amber));color:#1a0f00;"
        "text-shadow:0 1px 0 rgba(255,255,255,0.2)}"
        "button.btn.secondary{background:linear-gradient(180deg,#475569,#334155);color:var(--text)}"
        "button.btn.danger{background:linear-gradient(180deg,#dc2626,#991b1b);color:#fff}"
        "button.btn[disabled]{opacity:.55;cursor:wait;transform:none}"
        // 提示文本
        ".hint{color:var(--muted);font-size:11px;margin-top:6px;letter-spacing:.5px}"
        ".hint b{color:var(--cyan);font-weight:400}"
        // Footer 版权区
        "footer{text-align:center;color:var(--muted);font-size:11px;"
        "padding:20px 0 30px;letter-spacing:2px}"
        "footer .heart{color:var(--amber);text-shadow:0 0 6px rgba(245,158,11,0.5)}"
        "</style></head><body>"
        "<div class=\"top\">"
        "<h1><span class=\"c\">::</span> FOLOPASSPORT <span class=\"c\">::</span> CONTROL</h1>"
        "<div class=\"sub\">// 桌面 AI 物理外设 · 配置控制台 · v1.0</div>"
        "<div class=\"hud\">"
        "<span>DEVICE <b>ESP32-C3 · 8MB FLASH</b></span>"
        "<span>AP <b>FoloPassport-XXXX</b></span>"
        "<span>STATUS <b id=\"hud_st\">INIT</b></span>"
        "</div></div>"
        "<main>"
        "<form id=\"main_form\" method=\"post\" action=\"/save\">"
        // === Wi-Fi 模块 ===
        "<div class=\"card\">"
        "<h2>WI-FI NETWORK</h2>"
        "<div class=\"row\"><label>SCAN · 附近网络(2.4GHz)</label>"
        "<div class=\"row-flex\">"
        "<select id=\"ssid_sel\"><option value=\"\">正在扫描…</option></select>"
        "<button type=\"button\" class=\"btn secondary\" "
        "onclick=\"scanWifi()\" id=\"scan_btn\" style=\"flex:0 0 84px\">SCAN</button>"
        "</div><div class=\"hint\" id=\"scan_hint\"></div></div>"
        "<div class=\"row\"><label>SSID · Wi-Fi 名称</label>"
        "<input name=\"ssid\" id=\"ssid_in\" maxlength=\"32\" autocomplete=\"off\" value=\"");
    send_html_escaped(req, cfg.wifi_ssid);
    httpd_resp_sendstr_chunk(req, "\"></div>"
        "<div class=\"row keywrap\"><label>PSK · 密码(留空 = 不修改)</label>"
        "<input type=\"password\" name=\"pass\" id=\"pass_in\" maxlength=\"64\" autocomplete=\"off\">"
        "<button type=\"button\" class=\"key-toggle\" id=\"pass_toggle\" "
        "onclick=\"toggleKey('pass_in', this)\" title=\"显示/隐藏\">▣</button>"
        "</div></div>"
        // === API Keys 模块 ===
        "<div class=\"card\">"
        "<h2>API KEYS · 大模型凭证</h2>"
        "<div class=\"row keywrap\"><label>DEEPSEEK · DeepSeek Key</label>"
        "<input type=\"password\" name=\"key_deepseek\" id=\"kd_in\" "
        "maxlength=\"191\" placeholder=\"sk-...\" autocomplete=\"off\">"
        "<button type=\"button\" class=\"key-toggle\" "
        "onclick=\"toggleKey('kd_in', this)\" title=\"显示/隐藏\">▣</button>"
        "</div>"
        "<div class=\"row keywrap\"><label>MINIMAX · MiniMax Key</label>"
        "<input type=\"password\" name=\"key_minimax\" id=\"km_in\" "
        "maxlength=\"191\" placeholder=\"sk-...\" autocomplete=\"off\">"
        "<button type=\"button\" class=\"key-toggle\" "
        "onclick=\"toggleKey('km_in', this)\" title=\"显示/隐藏\">▣</button>"
        "</div>"
        "<div class=\"hint\">▣ Key 不回显,只在设备 NVS 中保存,从未外发。</div>"
        "</div>"
        // === 语音 Token 模块 ===
        "<div class=\"card\">"
        "<h2>PTT TOKEN · 语音链路共享密钥</h2>"
        "<div class=\"row keywrap\"><label>TOKEN · 与电脑端 <b>PASSPORT_VOICE_TOKEN</b> 一致</label>"
        "<input type=\"password\" name=\"voice_token\" id=\"vt_in\" "
        "maxlength=\"64\" placeholder=\"留空 = 不修改\" autocomplete=\"off\">"
        "<button type=\"button\" class=\"key-toggle\" "
        "onclick=\"toggleKey('vt_in', this)\" title=\"显示/隐藏\">▣</button>"
        "</div>"
        "<div class=\"hint\">留空保持原值;输入新值后保存会覆盖旧 Token。</div>"
        "</div>"
        // === 操作区 ===
        "<div class=\"actions\">"
        "<button type=\"submit\" class=\"btn primary\" id=\"save_btn\">"
        "▸ SAVE TO NVS</button>"
        "<button type=\"button\" class=\"btn danger\" onclick=\"doReboot()\" "
        "id=\"reboot_btn\">▸ REBOOT DEVICE</button>"
        "</div>"
        "</form>"
        "<footer>made with <span class=\"heart\">◆</span> for the desk-pet coder</footer>"
        "<script>"
        // === 视觉与交互增强 ===
        "var ssidInput=document.getElementById('ssid_in');"
        "var scanHint=document.getElementById('scan_hint');"
        "var scanBtn=document.getElementById('scan_btn');"
        "var hudSt=document.getElementById('hud_st');"
        "var saveBtn=document.getElementById('save_btn');"
        "var rebootBtn=document.getElementById('reboot_btn');"
        // Wi-Fi 信号格按强度映射
        "function rssiBars(r){"
        "if(r>=-50)return['█','█','█','█'];if(r>=-65)return['▂','█','█','█'];"
        "if(r>=-75)return['▂','▄','█','█'];if(r>=-85)return['▂','▄','▆','░'];"
        "return['▁','░','░','░'];}"
        "function rssiClass(r){return r>=-75?'rssi':'rssi weak';}"
        // Wi-Fi 扫描
        "function scanWifi(){"
        "var sel=document.getElementById('ssid_sel');"
        "sel.innerHTML='<option value=\"\">正在扫描…</option>';"
        "scanHint.textContent='';scanBtn.disabled=true;scanBtn.textContent='...';"
        "fetch('/scan').then(function(r){return r.json();}).then(function(a){"
        "sel.innerHTML='<option value=\"\">-- 请选择附近 Wi-Fi --</option>';"
        "if(!a.length){scanHint.innerHTML='<b>未发现网络</b>';}else{"
        "a.forEach(function(x){var o=document.createElement('option');"
        "o.value=x.ssid;var b=rssiBars(x.rssi);"
        "o.innerHTML=b.map(function(c){return'<span class=\"'+rssiClass(x.rssi)+'\">'+c+'</span>';}).join('')+x.ssid+' · '+x.rssi+'dBm';"
        "sel.appendChild(o);});"
        "scanHint.innerHTML='发现 <b>'+a.length+'</b> 个网络(2.4GHz)';}"
        "}).catch(function(e){sel.innerHTML='<option value=\"\">扫描失败</option>';"
        "scanHint.innerHTML='<b>扫描失败</b>,请重试';}).then(function(){"
        "scanBtn.disabled=false;scanBtn.textContent='SCAN';});}"
        "document.getElementById('ssid_sel').addEventListener('change',"
        "function(e){if(e.target.value)ssidInput.value=e.target.value;});"
        // 密码可见切换
        "function toggleKey(id,btn){var el=document.getElementById(id);"
        "if(el.type==='password'){el.type='text';btn.textContent='◫';}"
        "else{el.type='password';btn.textContent='▣';}}"
        // 保存
        "document.getElementById('main_form').addEventListener('submit',"
        "function(){saveBtn.disabled=true;saveBtn.textContent='▸ WRITING NVS...';});"
        // 重启
        "function doReboot(){if(!confirm('确定重启设备?已保存的配置会立即生效。'))return;"
        "rebootBtn.disabled=true;rebootBtn.textContent='▸ REBOOTING...';"
        "fetch('/reboot',{method:'POST'}).then(function(){"
        "document.body.innerHTML='<body style=\"background:#0a0d14;color:#06b6d4;"
        "font-family:monospace;text-align:center;padding-top:40vh\">"
        "<div style=\"font-size:20px;letter-spacing:4px\">REBOOTING…</div>"
        "<div style=\"color:#6b7693;margin-top:12px;font-size:12px\">"
        "设备将在几秒后断开热点并重新启动</div></body>';}).catch(function(e){"
        "alert('重启请求失败:'+e);rebootBtn.disabled=false;"
        "rebootBtn.textContent='▸ REBOOT DEVICE';});}"
        // 启动
        "scanWifi();"
        // 状态指示滚动
        "var _stRun=false;function updateHud(){"
        "fetch('/scan',{method:'HEAD'}).then(function(r){"
        "hudSt.textContent='READY';hudSt.style.color='#10b981';}).catch(function(){"
        "hudSt.textContent='OFFLINE';hudSt.style.color='#ef4444';});}"
        "updateHud();setInterval(updateHud,15000);"
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
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>SAVE OK</title><style>"
        "body{margin:0;background:#0a0d14;color:#dbe5ff;height:100vh;"
        "display:flex;align-items:center;justify-content:center;flex-direction:column;"
        "font-family:'SF Mono','Cascadia Code','JetBrains Mono','Consolas',monospace}"
        "body{background-image:linear-gradient(rgba(6,182,212,0.05) 1px,transparent 1px),"
        "linear-gradient(90deg,rgba(6,182,212,0.05) 1px,transparent 1px);"
        "background-size:24px 24px}"
        ".box{border:1px solid #1f2937;background:#0f1320;padding:32px 40px;"
        "box-shadow:0 0 0 1px #020409,0 0 32px rgba(16,185,129,0.18);text-align:center}"
        ".ok{color:#10b981;font-size:22px;letter-spacing:4px;margin:0 0 8px;"
        "text-shadow:0 0 12px rgba(16,185,129,0.6)}"
        ".sub{color:#6b7693;font-size:13px;margin:0 0 24px}"
        ".acts{display:flex;gap:12px;justify-content:center;flex-wrap:wrap}"
        "a,button{font-family:inherit;cursor:pointer;letter-spacing:1px;border:0;"
        "border-radius:0;padding:12px 22px;font-size:13px;font-weight:700;"
        "text-decoration:none;transition:filter .15s,transform .08s}"
        "a:hover,button:hover{filter:brightness(1.1)}"
        "a:active,button:active{transform:translateY(1px)}"
        ".primary{background:linear-gradient(180deg,#fbbf24,#f59e0b);color:#1a0f00}"
        ".danger{background:linear-gradient(180deg,#dc2626,#991b1b);color:#fff}"
        "</style></head><body>"
        "<div class=\"box\">"
        "<p class=\"ok\">▣ CONFIG SAVED</p>"
        "<p class=\"sub\">// 已写入 NVS · 重启后生效</p>"
        "<div class=\"acts\">"
        "<a class=\"primary\" href=\"/\">▸ BACK</a>"
        "<form method=\"post\" action=\"/reboot\" style=\"display:inline\">"
        "<button class=\"danger\" type=\"submit\">▸ REBOOT NOW</button>"
        "</form></div></div></body></html>");
    return ESP_OK;
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>REBOOTING</title><style>"
        "body{margin:0;background:#0a0d14;color:#dbe5ff;height:100vh;"
        "display:flex;align-items:center;justify-content:center;flex-direction:column;"
        "font-family:'SF Mono','Cascadia Code','JetBrains Mono','Consolas',monospace}"
        "body{background-image:linear-gradient(rgba(6,182,212,0.05) 1px,transparent 1px),"
        "linear-gradient(90deg,rgba(6,182,212,0.05) 1px,transparent 1px);"
        "background-size:24px 24px}"
        ".box{border:1px solid #1f2937;background:#0f1320;padding:32px 40px;"
        "box-shadow:0 0 0 1px #020409,0 0 32px rgba(6,182,212,0.22);text-align:center}"
        ".spin{color:#06b6d4;font-size:22px;letter-spacing:6px;margin:0 0 8px;"
        "text-shadow:0 0 12px rgba(6,182,212,0.7)}"
        ".sub{color:#6b7693;font-size:13px;margin:0}"
        "</style></head><body>"
        "<div class=\"box\">"
        "<p class=\"spin\">▣ REBOOTING…</p>"
        "<p class=\"sub\">// 设备将在几秒后断开热点并重新启动</p>"
        "</div></body></html>");
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
