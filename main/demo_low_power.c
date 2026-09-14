// main/demo_low_power.c —— 待机与熄屏设置:选择无操作后的熄屏超时并持久化到 NVS。
#include "demo.h"
#include "app_cfg.h"
#include "power_idle.h"
#include "power_idle_logic.h"
#include "ui_pixel.h"
#include "ui_theme.h"
#include "ui_font.h"

#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdint.h>

static const char *TAG = "demo_power";

#define OPTION_COUNT 4

typedef struct {
    uint16_t sec;
    const char *title;
    const char *tag;
    const char *detail;
} idle_opt_t;

static const idle_opt_t OPTIONS[OPTION_COUNT] = {
    { POWER_IDLE_TIMEOUT_30S,   "30 秒",   "极速省电", "15s 降至微光，30s 完全熄屏" },
    { POWER_IDLE_TIMEOUT_60S,   "1 分钟",  "标准平衡", "30s 降至微光，60s 完全熄屏" },
    { POWER_IDLE_TIMEOUT_180S,  "3 分钟",  "长时显示", "90s 降至微光，3m 完全熄屏" },
    { POWER_IDLE_TIMEOUT_NEVER, "从不熄屏", "桌面常亮", "屏幕始终保持设定亮度" },
};

static lv_obj_t *s_scr;
static lv_obj_t *s_status;
static lv_obj_t *s_detail;
static lv_obj_t *s_cards[OPTION_COUNT];
static lv_obj_t *s_labels[OPTION_COUNT];
static lv_obj_t *s_tags[OPTION_COUNT];
static int s_selected = 1;      // 默认 1 分钟
static uint16_t s_saved_sec = POWER_IDLE_TIMEOUT_60S;

static void update_cards(void)
{
    for (int i = 0; i < OPTION_COUNT; i++) {
        bool is_sel = (i == s_selected);
        bool is_cur = (OPTIONS[i].sec == s_saved_sec);

        ui_pixel_set_selected(s_cards[i], is_sel, true);

        if (s_labels[i]) {
            lv_obj_set_style_text_color(s_labels[i],
                lv_color_hex(is_sel ? UI_THEME_BG : (is_cur ? UI_THEME_AMBER : UI_THEME_TEXT)), 0);
        }
        if (s_tags[i]) {
            lv_obj_set_style_text_color(s_tags[i],
                lv_color_hex(is_sel ? UI_THEME_BG : UI_THEME_MUTED), 0);
        }
    }

    if (s_detail) {
        lv_label_set_text(s_detail, OPTIONS[s_selected].detail);
    }
}

void demo_low_power_enter(void)
{
    s_saved_sec = power_idle_get_timeout_sec();
    s_selected = 1; // 默认 60s
    for (int i = 0; i < OPTION_COUNT; i++) {
        if (OPTIONS[i].sec == s_saved_sec) {
            s_selected = i;
            break;
        }
    }

    s_scr = ui_pixel_screen_create("待机与熄屏");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 14, 50, 212, 256, UI_PAPER);

    s_status = lv_label_create(panel);
    lv_obj_set_width(s_status, 196);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_font(s_status, ui_font_pick_14("上下选择"), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 6);
    lv_label_set_text(s_status, "[上下]选择  [OK]保存生效");

    const int card_h = 40;
    const int gap = 8;
    const int y0 = 30;

    for (int i = 0; i < OPTION_COUNT; i++) {
        s_cards[i] = ui_pixel_panel_create(panel, 8, y0 + i * (card_h + gap), 180, card_h, UI_PAPER);

        s_labels[i] = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(s_labels[i], ui_font_pick_14(OPTIONS[i].title), 0);
        lv_obj_set_pos(s_labels[i], 10, 12);
        lv_label_set_text(s_labels[i], OPTIONS[i].title);

        s_tags[i] = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(s_tags[i], ui_font_pick_14(OPTIONS[i].tag), 0);
        lv_obj_set_pos(s_tags[i], 112, 12);
        lv_label_set_text(s_tags[i], OPTIONS[i].tag);
    }

    // 底部专属说明文本(位于 panel 内部正下方)
    s_detail = lv_label_create(panel);
    lv_obj_set_pos(s_detail, 6, 226);
    lv_obj_set_width(s_detail, 184);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_detail, ui_font_pick_14("30s 完全熄屏"), 0);
    lv_obj_set_style_text_color(s_detail, lv_color_hex(UI_THEME_CYAN), 0);

    update_cards();
    lv_screen_load(s_scr);
}

void demo_low_power_exit(void)
{
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status = s_detail = NULL;
        for (int i = 0; i < OPTION_COUNT; i++) {
            s_cards[i] = s_labels[i] = s_tags[i] = NULL;
        }
    }
}

void demo_low_power_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_DOWN) {
        s_selected = (s_selected + 1) % OPTION_COUNT;
        update_cards();
    } else if (btn == BSP_BTN_UP) {
        s_selected = (s_selected + OPTION_COUNT - 1) % OPTION_COUNT;
        update_cards();
    } else if (btn == BSP_BTN_OK) {
        uint16_t new_sec = OPTIONS[s_selected].sec;
        s_saved_sec = new_sec;
        app_cfg_save_idle_timeout(new_sec);
        power_idle_set_timeout_sec(new_sec);

        update_cards();

        if (s_status) {
            char tip[64];
            snprintf(tip, sizeof(tip), "√ 保存成功: %s", OPTIONS[s_selected].title);
            lv_label_set_text(s_status, tip);
            lv_obj_set_style_text_color(s_status, lv_color_hex(UI_THEME_GREEN), 0);
        }
        ESP_LOGI(TAG, "已保存并应用待机超时: %u 秒", new_sec);
    }
}
