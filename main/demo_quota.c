// main/demo_quota.c —— 8-bit 经典像素风格 Token 额度展示。
#include "demo.h"
#include "app_cfg.h"
#include "app_net.h"
#include "ui_theme.h"
#include "ui_pixel.h"
#include "ui_font.h"
#include "bsp_battery.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "time.h"
#include "cJSON.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "demo_quota";

#define QUOTA_BUF_SIZE 4096
#define BLOCKS_COUNT 10 // 10 格 8-bit 经典像素能量槽

static app_cfg_t s_cfg;

#define DBG      UI_THEME_BG         /* 纯黑 CRT */
#define DSURF    UI_THEME_PANEL      /* 卡片表面 */
#define DBORD    UI_THEME_GRID       /* 像素描边 */
#define DBORD_HI UI_THEME_GRID_HI    /* 像素高光 */
#define DTXT     UI_THEME_TEXT       /* 亮白 */
#define DMUT     UI_THEME_MUTED      /* 灰 */
#define DAMBER   UI_THEME_AMBER      /* 金币黄 */
#define DCYAN    UI_THEME_CYAN       /* 霓虹青 */
#define DGREEN   UI_THEME_GREEN      /* 鲜绿 */
#define DRED     UI_THEME_RED        /* 鲜红 */

typedef enum {
    Q_OFF,
    Q_CONNECTING,
    Q_FETCHING_DEEPSEEK,
    Q_FETCHING_MINIMAX,
    Q_DONE,
    Q_FAILED,
} quota_state_t;

typedef struct {
    char balance[32];
    char currency[8];
    bool available;
    int err_code;
    int http_status;
} deepseek_data_t;

typedef struct {
    int percent_5h;
    int percent_week;
    int remains_ms_5h;
    int remains_ms_week;
    bool valid;
    int err_code;
    int http_status;
    char err_msg[32];
} minimax_data_t;

static volatile quota_state_t s_state = Q_OFF;
static esp_err_t s_error;
static deepseek_data_t s_ds;
static minimax_data_t s_mm;
static TaskHandle_t s_worker_task;

static lv_obj_t *s_scr;
static lv_obj_t *s_status_label;
static lv_obj_t *s_ds_label;
static lv_obj_t *s_clock_label;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_pct_5h;
static lv_obj_t *s_pct_week;
static lv_obj_t *s_time_5h;
static lv_obj_t *s_time_week;

// 10 段分体像素能量块
static lv_obj_t *s_blocks_5h[BLOCKS_COUNT];
static lv_obj_t *s_blocks_week[BLOCKS_COUNT];
static lv_timer_t *s_timer;

// ---- 8-bit 复古像素 HUD 卡片创建 ----
static lv_obj_t *pixel_hud_card_create(lv_obj_t *parent, int x, int y, int w, int h, const char *tag)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(DSURF), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(DBORD), 0);
    lv_obj_set_style_border_width(c, 2, 0);

    // 四角像素十字/高光块
    ui_theme_box(c, 0, 0, 4, 4, DBORD_HI, 0);
    ui_theme_box(c, w - 4, 0, 4, 4, DBORD_HI, 0);
    ui_theme_box(c, 0, h - 4, 4, 4, DBORD_HI, 0);
    ui_theme_box(c, w - 4, h - 4, 4, 4, DBORD_HI, 0);

    if (tag) {
        lv_obj_t *t = ui_pixel_label(c, tag, ui_font_pick_14(tag), DAMBER);
        lv_obj_set_pos(t, 8, 6);
    }
    return c;
}

// 8-bit 分体能量块渲染（10 个独立方块小格子）
static void create_blocky_bar(lv_obj_t *parent, int x, int y, lv_obj_t *blocks_out[BLOCKS_COUNT])
{
    const int bw = 9, bh = 8, bgap = 3;
    for (int i = 0; i < BLOCKS_COUNT; i++) {
        lv_obj_t *b = ui_theme_box(parent, x + i * (bw + bgap), y, bw, bh, UI_THEME_GRID, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(UI_THEME_BG), 0);
        blocks_out[i] = b;
    }
}

static void update_blocky_bar(lv_obj_t *blocks[BLOCKS_COUNT], int percent, bool reverse_color)
{
    int filled = (percent * BLOCKS_COUNT + 50) / 100;
    if (filled > BLOCKS_COUNT) filled = BLOCKS_COUNT;
    if (filled < 0) filled = 0;

    uint32_t active_c = DGREEN;
    if (reverse_color) {
        active_c = percent >= 80 ? DRED : (percent >= 50 ? DAMBER : DGREEN);
    } else {
        active_c = percent <= 20 ? DRED : (percent <= 50 ? DAMBER : DGREEN);
    }

    for (int i = 0; i < BLOCKS_COUNT; i++) {
        if (i < filled) {
            lv_obj_set_style_bg_color(blocks[i], lv_color_hex(active_c), 0);
        } else {
            lv_obj_set_style_bg_color(blocks[i], lv_color_hex(UI_THEME_GRID), 0);
        }
    }
}

// ---- 网络请求与解析 ----
static esp_err_t fetch_json(const char *url, const char *bearer_token, char *buf, size_t buf_size, int *out_http_status)
{
    // 必须容纳 "Bearer " + 最长 Key(APP_CFG_KEY_MAX-1) + NUL;
    // MiniMax 的订阅 Key(sk-cp-…) 常超过 120 字符,缓冲过小会截断 Key 导致 login fail。
    char auth_header[APP_CFG_KEY_MAX + 16];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", bearer_token);

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed for %s: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    (void)content_length;
    int status_code = esp_http_client_get_status_code(client);
    if (out_http_status) *out_http_status = status_code;

    int total_read = 0;
    while (total_read < (int)buf_size - 1) {
        int read_bytes = esp_http_client_read(client, buf + total_read, buf_size - 1 - total_read);
        if (read_bytes <= 0) break;
        total_read += read_bytes;
    }
    buf[total_read] = '\0';

    ESP_LOGI(TAG, "Fetch %s -> status=%d, len=%d, body: %s", url, status_code, total_read, buf);

    esp_http_client_cleanup(client);
    if (status_code != 200) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static void parse_deepseek(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        s_ds.available = false;
        s_ds.err_code = ESP_ERR_INVALID_RESPONSE;
        return;
    }
    cJSON *avail = cJSON_GetObjectItem(root, "is_available");
    s_ds.available = avail ? cJSON_IsTrue(avail) : false;

    cJSON *infos = cJSON_GetObjectItem(root, "balance_infos");
    if (cJSON_IsArray(infos) && cJSON_GetArraySize(infos) > 0) {
        cJSON *info = cJSON_GetArrayItem(infos, 0);
        cJSON *cur = cJSON_GetObjectItem(info, "currency");
        cJSON *bal = cJSON_GetObjectItem(info, "total_balance");
        if (cur && cJSON_IsString(cur)) strncpy(s_ds.currency, cur->valuestring, sizeof(s_ds.currency) - 1);
        if (bal && cJSON_IsString(bal)) strncpy(s_ds.balance, bal->valuestring, sizeof(s_ds.balance) - 1);
    }
    cJSON_Delete(root);
}

static void parse_minimax(const char *json)
{
    s_mm.err_msg[0] = '\0';
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        s_mm.valid = false;
        s_mm.err_code = ESP_ERR_INVALID_RESPONSE;
        snprintf(s_mm.err_msg, sizeof(s_mm.err_msg), "JSON ERR");
        return;
    }

    // 检查 base_resp 错误
    cJSON *base_resp = cJSON_GetObjectItem(root, "base_resp");
    if (base_resp) {
        cJSON *status_code = cJSON_GetObjectItem(base_resp, "status_code");
        cJSON *status_msg = cJSON_GetObjectItem(base_resp, "status_msg");
        if (status_code && status_code->valueint != 0) {
            s_mm.valid = false;
            s_mm.err_code = status_code->valueint;
            if (status_msg && cJSON_IsString(status_msg)) {
                strncpy(s_mm.err_msg, status_msg->valuestring, sizeof(s_mm.err_msg) - 1);
                s_mm.err_msg[sizeof(s_mm.err_msg) - 1] = '\0';
            }
            cJSON_Delete(root);
            return;
        }
    }

    // /v1/token_plan/remains 返回 model_remains[](可能包在 data 里),
    // 每个模型含 5 小时窗口与周窗口:
    //   current_interval_remaining_percent / current_weekly_remaining_percent(剩余百分比)
    //   remains_time / weekly_remains_time(窗口剩余毫秒)
    cJSON *data_obj = cJSON_GetObjectItem(root, "data");
    cJSON *target_root = data_obj ? data_obj : root;
    cJSON *remains = cJSON_GetObjectItem(target_root, "model_remains");

    if (!cJSON_IsArray(remains) || cJSON_GetArraySize(remains) == 0) {
        s_mm.valid = false;
        s_mm.err_code = ESP_ERR_INVALID_RESPONSE;
        snprintf(s_mm.err_msg, sizeof(s_mm.err_msg), "NO DATA");
        cJSON_Delete(root);
        return;
    }

    // 优先取 general 模型,否则用第一条。
    cJSON *entry = NULL;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, remains) {
        cJSON *name = cJSON_GetObjectItem(it, "model_name");
        if (cJSON_IsString(name) && strcmp(name->valuestring, "general") == 0) {
            entry = it;
            break;
        }
        if (!entry) entry = it;
    }

    s_mm.percent_5h = 0;
    s_mm.percent_week = 0;
    s_mm.remains_ms_5h = 0;
    s_mm.remains_ms_week = 0;

    cJSON *p5h = cJSON_GetObjectItem(entry, "current_interval_remaining_percent");
    cJSON *pwk = cJSON_GetObjectItem(entry, "current_weekly_remaining_percent");
    cJSON *t5h = cJSON_GetObjectItem(entry, "remains_time");
    cJSON *twk = cJSON_GetObjectItem(entry, "weekly_remains_time");
    if (cJSON_IsNumber(p5h)) s_mm.percent_5h = p5h->valueint;
    if (cJSON_IsNumber(pwk)) s_mm.percent_week = pwk->valueint;
    if (cJSON_IsNumber(t5h)) s_mm.remains_ms_5h = t5h->valueint;
    if (cJSON_IsNumber(twk)) s_mm.remains_ms_week = twk->valueint;

    s_mm.valid = true;
    cJSON_Delete(root);
}

static void quota_worker(void *arg)
{
    (void)arg;
    char *buf = malloc(QUOTA_BUF_SIZE);
    if (!buf) {
        s_state = Q_FAILED;
        s_error = ESP_ERR_NO_MEM;
        s_worker_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    for (int i = 0; i < 30 && !app_net_is_up(); i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!app_net_is_up()) {
        s_state = Q_FAILED;
        s_error = ESP_FAIL;
        free(buf);
        s_worker_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_state = Q_FETCHING_DEEPSEEK;
    int ds_status = 0;
    esp_err_t err = fetch_json("https://api.deepseek.com/user/balance",
                               s_cfg.deepseek_key, buf, QUOTA_BUF_SIZE, &ds_status);
    s_ds.http_status = ds_status;
    if (err == ESP_OK) {
        parse_deepseek(buf);
    } else {
        s_ds.available = false;
        s_ds.err_code = err;
        snprintf(s_ds.balance, sizeof(s_ds.balance), "ERR");
    }

    s_state = Q_FETCHING_MINIMAX;
    int mm_status = 0;
    err = fetch_json("https://www.minimaxi.com/v1/token_plan/remains",
                     s_cfg.minimax_key, buf, QUOTA_BUF_SIZE, &mm_status);
    s_mm.http_status = mm_status;
    if (err == ESP_OK) {
        parse_minimax(buf);
    } else {
        s_mm.valid = false;
        s_mm.err_code = err;
    }

    s_state = Q_DONE;
    free(buf);
    s_worker_task = NULL;
    vTaskDelete(NULL);
}

static void start_fetch(void)
{
    if (s_worker_task) return;
    s_ds.available = false;
    s_mm.valid = false;
    s_state = Q_CONNECTING;
    app_net_start();
    xTaskCreate(quota_worker, "quota_worker", 6144, NULL, 5, &s_worker_task);
}

static void stop_fetch(void)
{
    if (s_worker_task) {
        vTaskDelete(s_worker_task);
        s_worker_task = NULL;
    }
    s_state = Q_OFF;
}

static void format_time(int ms, char *buf, size_t size)
{
    int total_sec = ms / 1000;
    int hours = total_sec / 3600;
    int mins = (total_sec % 3600) / 60;
    snprintf(buf, size, "%dh%dm", hours, mins);
}

static void ui_set_status(const char *txt, uint32_t color)
{
    lv_label_set_text(s_status_label, txt);
    lv_obj_set_style_text_font(s_status_label, ui_font_pick_14(txt), 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(color), 0);
}

static void refresh_battery(void)
{
    static bool init_done;
    static int countdown;
    if (!s_battery_label) return;
    if (!init_done) {
        if (bsp_battery_init() != ESP_OK) {
            init_done = true;
            lv_label_set_text(s_battery_label, "");
            return;
        }
        init_done = true;
    }
    if (++countdown < 30) return;
    countdown = 0;
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_label_set_text(s_battery_label, "");
        return;
    }
    if (soc > 100) soc = 100;
    lv_label_set_text_fmt(s_battery_label, "[%d%%]", soc);
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(soc <= 15 ? DRED : DCYAN), 0);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    refresh_battery();

    // 5 分钟定时自动刷新
    static int s_auto_cnt = 0;
    if (s_state == Q_DONE) {
        if (++s_auto_cnt >= (300 * 5)) {
            s_auto_cnt = 0;
            start_fetch();
        }
    } else {
        s_auto_cnt = 0;
    }

    if (s_clock_label) {
        if (app_net_time_synced()) {
            time_t now = time(NULL);
            struct tm tmv;
            localtime_r(&now, &tmv);
            // 恢复月/日 时:分（例如 09/14 15:30）
            lv_label_set_text_fmt(s_clock_label, "%02d/%02d %02d:%02d",
                                  tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        } else {
            lv_label_set_text(s_clock_label, "--/-- --:--");
        }
    }

    switch (s_state) {
    case Q_CONNECTING:
        ui_set_status("<< 正在连接网络… >>", DAMBER);
        break;
    case Q_FETCHING_DEEPSEEK:
        ui_set_status("<< 正在获取 DeepSeek… >>", DCYAN);
        break;
    case Q_FETCHING_MINIMAX:
        ui_set_status("<< 正在获取 MiniMax… >>", DCYAN);
        break;
    case Q_DONE: {
        ui_set_status("[上]刷新  [下]设置  [长按]返回", DGREEN);

        if (s_ds.available) {
            lv_label_set_text_fmt(s_ds_label, "%s %s", s_ds.currency, s_ds.balance);
            lv_obj_set_style_text_color(s_ds_label, lv_color_hex(DAMBER), 0);
        } else {
            if (s_ds.http_status > 0 && s_ds.http_status != 200) {
                lv_label_set_text_fmt(s_ds_label, "HTTP %d", s_ds.http_status);
            } else {
                lv_label_set_text_fmt(s_ds_label, "ERR 0x%x", s_ds.err_code);
            }
            lv_obj_set_style_text_color(s_ds_label, lv_color_hex(DRED), 0);
        }

        if (s_mm.valid) {
            char t5h[16], twk[16];
            format_time(s_mm.remains_ms_5h, t5h, sizeof(t5h));
            format_time(s_mm.remains_ms_week, twk, sizeof(twk));
            int used5h = 100 - s_mm.percent_5h;
            int usedwk = 100 - s_mm.percent_week;
            lv_label_set_text_fmt(s_pct_5h, "%d%%", used5h);
            lv_label_set_text_fmt(s_pct_week, "%d%%", usedwk);
            lv_label_set_text_fmt(s_time_5h, "%s", t5h);
            lv_label_set_text_fmt(s_time_week, "%s", twk);
            update_blocky_bar(s_blocks_5h, used5h, true);
            update_blocky_bar(s_blocks_week, usedwk, true);
        } else {
            if (s_mm.err_msg[0] != '\0') {
                lv_label_set_text(s_pct_5h, s_mm.err_msg);
            } else if (s_mm.http_status > 0 && s_mm.http_status != 200) {
                lv_label_set_text_fmt(s_pct_5h, "HTTP %d", s_mm.http_status);
            } else {
                lv_label_set_text_fmt(s_pct_5h, "ERR 0x%x", s_mm.err_code);
            }
            lv_label_set_text(s_pct_week, "--");
            lv_label_set_text(s_time_5h, "");
            lv_label_set_text(s_time_week, "");
            update_blocky_bar(s_blocks_5h, 0, true);
            update_blocky_bar(s_blocks_week, 0, true);
        }
        break;
    }
    case Q_FAILED:
        ui_set_status("<< 获取失败 (上键重试) >>", DRED);
        s_state = Q_OFF;
        break;
    default:
        break;
    }
}

void demo_quota_enter(void)
{
    s_scr = ui_theme_screen();

    /* 8-bit 顶部 HUD（品牌左 / 时钟居中 / 电量居右） */
    ui_theme_label(s_scr, ":: 额度看板 ::", 10, 10, 84, DAMBER, ui_font_pick_14(":: 额度看板 ::"));
    s_clock_label = ui_theme_label(s_scr, "--/-- --:--", 94, 10, 92, DTXT, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_CENTER, 0);
    s_battery_label = ui_theme_label(s_scr, "[--%]", 186, 10, 44, DCYAN, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(s_battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    ui_theme_divider(s_scr, 12, 34, 216);

    /* DeepSeek 8-bit HUD 框 */
    lv_obj_t *ds = pixel_hud_card_create(s_scr, 12, 44, 216, 68, "■ DeepSeek 余额");
    s_ds_label = ui_pixel_label(ds, "--", &lv_font_montserrat_20, DAMBER);
    lv_obj_set_pos(s_ds_label, 12, 30);

    /* MiniMax 8-bit HUD 框 */
    lv_obj_t *mm = pixel_hud_card_create(s_scr, 12, 120, 216, 156, "■ MiniMax 套餐用量");

    lv_obj_t *n5h = ui_pixel_label(mm, "5H 限额", ui_font_pick_14("5H 限额"), DTXT);
    lv_obj_set_pos(n5h, 12, 32);
    s_pct_5h = ui_pixel_label(mm, "--", &lv_font_montserrat_14, DCYAN);
    lv_obj_set_pos(s_pct_5h, 72, 32);
    create_blocky_bar(mm, 12, 54, s_blocks_5h);
    s_time_5h = ui_pixel_label(mm, "", &lv_font_montserrat_14, DMUT);
    lv_obj_set_pos(s_time_5h, 140, 50);
    lv_obj_set_width(s_time_5h, 64);
    lv_obj_set_style_text_align(s_time_5h, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *nwk = ui_pixel_label(mm, "周度套餐", ui_font_pick_14("周度套餐"), DTXT);
    lv_obj_set_pos(nwk, 12, 88);
    s_pct_week = ui_pixel_label(mm, "--", &lv_font_montserrat_14, DCYAN);
    lv_obj_set_pos(s_pct_week, 72, 88);
    create_blocky_bar(mm, 12, 110, s_blocks_week);
    s_time_week = ui_pixel_label(mm, "", &lv_font_montserrat_14, DMUT);
    lv_obj_set_pos(s_time_week, 140, 106);
    lv_obj_set_width(s_time_week, 64);
    lv_obj_set_style_text_align(s_time_week, LV_TEXT_ALIGN_RIGHT, 0);

    ui_theme_divider(s_scr, 12, 292, 216);
    s_status_label = ui_theme_label(s_scr, "--", 12, 298, 216, DMUT, &lv_font_montserrat_14);
    ui_theme_center(s_status_label);

    s_timer = lv_timer_create(tick, 200, NULL);
    lv_screen_load(s_scr);

    if (!app_cfg_load(&s_cfg) || !app_cfg_wifi_ready(&s_cfg) || !app_cfg_keys_ready(&s_cfg)) {
        ui_set_status("<< 需先配置网络 (下键设置) >>", DAMBER);
        return;
    }
    start_fetch();
}

void demo_quota_exit(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    stop_fetch();
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status_label = s_ds_label = s_clock_label = s_battery_label = NULL;
        s_pct_5h = s_pct_week = s_time_5h = s_time_week = NULL;
        for (int i = 0; i < BLOCKS_COUNT; i++) {
            s_blocks_5h[i] = s_blocks_week[i] = NULL;
        }
    }
}

void demo_quota_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_DOWN) {
        app_nav_request_portal();
        return;
    }
    if (btn != BSP_BTN_UP || (s_state != Q_DONE && s_state != Q_OFF)) return;
    start_fetch();
}
