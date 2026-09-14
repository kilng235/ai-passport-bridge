// main/demo_brightness.c —— 背光亮度设置页:上/下短按步进调整并即时预览,OK 短按保存到 NVS。
// 保存走一次性 worker 任务(仓库约定:按键回调不做存储等慢操作);未保存就离开则丢弃预览。
// 布局对齐 leo-radio 的音量页:居中标题 + 大号数值 + 宽进度条 + 底部操作提示。
#include "demo.h"
#include "app_cfg.h"
#include "brightness_logic.h"
#include "bsp_display.h"
#include "power_idle.h"
#include "ui_pixel.h"
#include "ui_theme.h"
#include "ui_font.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "demo_brightness";

static lv_obj_t *s_scr;
static lv_obj_t *s_pct_label;
static lv_obj_t *s_bar;
static lv_obj_t *s_result_label;
static volatile uint8_t s_saved;    // 已确认的亮度;预览未保存时,离开页面回到该值
static volatile uint8_t s_current;  // 正在预览的亮度
static volatile bool s_saving;

// 把当前预览值套用到硬件与 UI。按键回调由 main 持有 LVGL 锁后调用,这里不再加锁。
static void apply_current(void)
{
    bsp_display_backlight(s_current);
    lv_bar_set_value(s_bar, s_current, LV_ANIM_ON);
    lv_label_set_text_fmt(s_pct_label, "%u%%", (unsigned)s_current);
}

static void save_worker(void *arg)
{
    (void)arg;
    uint8_t value = s_current;
    bool ok = app_cfg_save_brightness(value);
    if (ok) {
        s_saved = value;
        // 保存期间页面可能已退出:无论在哪个界面,把硬件与空闲恢复档对齐到新值。
        bsp_display_backlight(value);
        power_idle_set_on_level(value);
    }
    ESP_LOGI(TAG, "亮度 %u%% 保存%s", (unsigned)value, ok ? "成功" : "失败");

    if (bsp_lvgl_lock(500)) {
        if (s_result_label) {
            const char *res = ok ? "已保存" : "保存失败";
            lv_label_set_text(s_result_label, res);
            // 标签建时文本为空会选成 Montserrat,填中文必须重选字库,否则是占位框。
            lv_obj_set_style_text_font(s_result_label, ui_font_pick_14(res), 0);
            lv_obj_set_style_text_color(s_result_label,
                                        lv_color_hex(ok ? UI_THEME_GREEN : UI_THEME_RED), 0);
        }
        bsp_lvgl_unlock();
    }
    s_saving = false;
    vTaskDelete(NULL);
}

static void save_request(void)
{
    if (s_saving) return;
    s_saving = true;
    if (xTaskCreate(save_worker, "brightness_save", 3072, NULL, 4, NULL) != pdPASS) {
        s_saving = false;
        ESP_LOGE(TAG, "创建保存任务失败");
        if (s_result_label) {   // 此处仍处于按键回调(持有 LVGL 锁),可直接更新
            lv_label_set_text(s_result_label, "保存失败");
            lv_obj_set_style_text_font(s_result_label, ui_font_pick_14("保存失败"), 0);
            lv_obj_set_style_text_color(s_result_label, lv_color_hex(UI_THEME_RED), 0);
        }
    }
}

void demo_brightness_enter(void)
{
    power_idle_set_enabled(false);   // 本页直接驱动背光,关闭全局空闲分级

    s_saved = BRIGHTNESS_DEFAULT;
    uint8_t stored = s_saved;
    if (app_cfg_load_brightness(&stored)) s_saved = stored;
    s_current = s_saved;

    s_scr = ui_theme_screen();

    /* leo 音量页布局:居中标题 + 大号百分比 + 宽进度条 */
    lv_obj_t *title = ui_theme_label(s_scr, "亮度", 12, 16, 216, UI_THEME_TEXT,
                                     &lv_font_montserrat_20);
    ui_theme_center(title);

    s_pct_label = ui_theme_label(s_scr, "", 12, 84, 216, UI_THEME_AMBER,
                                 &lv_font_montserrat_20);
    ui_theme_center(s_pct_label);

    s_bar = lv_bar_create(s_scr);
    lv_obj_set_pos(s_bar, 30, 136);
    lv_obj_set_size(s_bar, 180, 14);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_THEME_GRID), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_THEME_AMBER), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 7, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 7, LV_PART_INDICATOR);

    s_result_label = ui_theme_label(s_scr, "", 12, 170, 216, UI_THEME_MUTED,
                                    &lv_font_montserrat_14);
    ui_theme_center(s_result_label);

    /* 底部操作提示 */
    lv_obj_t *hint = ui_theme_label(s_scr, "上下调节  确定保存", 12, 276, 216,
                                    UI_THEME_MUTED, &lv_font_montserrat_14);
    ui_theme_center(hint);

    apply_current();
    lv_screen_load(s_scr);
}

void demo_brightness_exit(void)
{
    // 丢弃未保存的预览,回到已确认亮度;保存任务若还在跑,成功后会再次套用新值。
    bsp_display_backlight(s_saved);
    power_idle_set_on_level(s_saved);
    power_idle_set_enabled(true);

    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_pct_label = NULL;
        s_bar = NULL;
        s_result_label = NULL;
    }
}

void demo_brightness_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;   // 长按 OK 由 main 统一拦截返回菜单
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        s_current = brightness_step(s_current, btn == BSP_BTN_UP ? +1 : -1);
        apply_current();
    } else if (btn == BSP_BTN_OK) {
        save_request();
    }
}
