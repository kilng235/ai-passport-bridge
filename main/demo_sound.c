// main/demo_sound.c —— 提示音设置页:OK 开/关,上/下调音量,长按上/下循环静音时段预设;
// 改动即时试听并存入 NVS(试听走 app_beep_preview,静音窗内也有反馈)。
// 存储走一次性 worker(仓库约定:按键回调不做存储等慢操作),多次改动合并成一次写入。
#include "demo.h"
#include "app_beep.h"
#include "ui_pixel.h"
#include "ui_theme.h"
#include "ui_font.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>

static const char *TAG = "demo_sound";

#define SOUND_VOL_MIN  10
#define SOUND_VOL_MAX  100
#define SOUND_VOL_STEP 10

static lv_obj_t *s_scr;
static lv_obj_t *s_status;
static lv_obj_t *s_pct;
static lv_obj_t *s_bar;
static lv_obj_t *s_quiet;
static bool s_on;
static uint8_t s_vol;

static volatile bool s_saving;
static volatile bool s_dirty;

static void apply_current(void)
{
    // 这些标签建时文本为空(选成了 Montserrat),填中文时必须重选字库,否则是占位框。
    const char *st = s_on ? "开" : "关";
    lv_label_set_text(s_status, st);
    lv_obj_set_style_text_font(s_status, ui_font_pick_20(st), 0);
    lv_obj_set_style_text_color(s_status,
        lv_color_hex(s_on ? UI_THEME_GREEN : UI_THEME_MUTED), 0);

    char pct[24];
    snprintf(pct, sizeof(pct), "音量 %u%%", (unsigned)s_vol);
    lv_label_set_text(s_pct, pct);
    lv_obj_set_style_text_font(s_pct, ui_font_pick_20(pct), 0);
    lv_obj_set_style_text_color(s_pct,
        lv_color_hex(s_on ? UI_THEME_AMBER : UI_THEME_MUTED), 0);

    lv_bar_set_value(s_bar, s_vol, LV_ANIM_ON);

    char quiet[32];
    snprintf(quiet, sizeof(quiet), "静音 %s",
             app_beep_quiet_preset_label(app_beep_quiet_preset()));
    lv_label_set_text(s_quiet, quiet);
    lv_obj_set_style_text_font(s_quiet, ui_font_pick_14(quiet), 0);
    lv_obj_set_style_text_color(s_quiet,
        lv_color_hex(app_beep_quiet_preset() ? UI_THEME_AMBER : UI_THEME_MUTED), 0);
}

static void save_worker(void *arg)
{
    (void)arg;
    do {
        app_beep_save();
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (s_dirty);
    s_saving = false;
    ESP_LOGI(TAG, "提示音设置已保存: %s / %u%%", s_on ? "开" : "关", (unsigned)s_vol);
    vTaskDelete(NULL);
}

static void save_request(void)
{
    s_dirty = true;
    if (s_saving) return;
    s_saving = true;
    if (xTaskCreate(save_worker, "sound_save", 3072, NULL, 4, NULL) != pdPASS) {
        s_saving = false;
        ESP_LOGE(TAG, "创建保存任务失败");
    }
}

void demo_sound_enter(void)
{
    s_on = app_beep_enabled();
    s_vol = app_beep_volume();
    if (s_vol < SOUND_VOL_MIN) s_vol = SOUND_VOL_MIN;
    if (s_vol > SOUND_VOL_MAX) s_vol = SOUND_VOL_MAX;

    s_scr = ui_theme_screen();

    lv_obj_t *title = ui_theme_label(s_scr, "提示音", 12, 16, 216, UI_THEME_TEXT,
                                     &lv_font_montserrat_20);
    ui_theme_center(title);

    s_status = ui_theme_label(s_scr, "", 12, 70, 216, UI_THEME_TEXT,
                              &lv_font_montserrat_20);
    ui_theme_center(s_status);

    s_pct = ui_theme_label(s_scr, "", 12, 116, 216, UI_THEME_AMBER,
                           &lv_font_montserrat_20);
    ui_theme_center(s_pct);

    s_bar = lv_bar_create(s_scr);
    lv_obj_set_pos(s_bar, 30, 160);
    lv_obj_set_size(s_bar, 180, 14);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_THEME_GRID), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_THEME_AMBER), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 7, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 7, LV_PART_INDICATOR);

    s_quiet = ui_theme_label(s_scr, "", 12, 210, 216, UI_THEME_MUTED,
                             &lv_font_montserrat_14);
    ui_theme_center(s_quiet);

    lv_obj_t *hint = ui_theme_label(s_scr, "OK 开/关  上下音量  长按静音", 12, 276,
                                    216, UI_THEME_MUTED, &lv_font_montserrat_14);
    ui_theme_center(hint);

    apply_current();
    lv_screen_load(s_scr);
}

void demo_sound_exit(void)
{
    if (s_dirty) save_request();   // 离页确保落盘(worker 不碰 LVGL)
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status = s_pct = s_bar = s_quiet = NULL;
    }
}

void demo_sound_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    // 长按 OK 由 main 统一拦截返回菜单;长按上/下在本页循环静音时段预设。
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        if (ev == BSP_BTN_CLICK) {
            int v = (int)s_vol + (btn == BSP_BTN_UP ? SOUND_VOL_STEP : -SOUND_VOL_STEP);
            if (v < SOUND_VOL_MIN) v = SOUND_VOL_MIN;
            if (v > SOUND_VOL_MAX) v = SOUND_VOL_MAX;
            s_vol = (uint8_t)v;
            app_beep_set_volume(s_vol);
            apply_current();
            if (s_on) app_beep_preview(APP_NOTIFY_DONE);   // 即时试听
            save_request();
        } else if (ev == BSP_BTN_LONG) {
            int count = 3;   // 关 / 22:00-08:00 / 23:00-07:00
            int next = app_beep_quiet_preset() + (btn == BSP_BTN_UP ? 1 : count - 1);
            app_beep_set_quiet_preset(next % count);
            apply_current();
            save_request();
        }
        return;
    }
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        s_on = !s_on;
        app_beep_set_enabled(s_on);
        apply_current();
        if (s_on) app_beep_preview(APP_NOTIFY_DONE);   // 试听:无视静音窗
        save_request();
    }
}
