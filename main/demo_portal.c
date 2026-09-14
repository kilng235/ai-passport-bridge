// main/demo_portal.c —— 配置门户页:显示热点信息并启停 SoftAP 门户。
// 保存成功会触发设备重启,因此本页只需展示信息,不需要复杂状态机。
#include "demo.h"
#include "app_portal.h"
#include "ui_pixel.h"

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "demo_portal";

typedef enum {
    PORTAL_STARTING = 0,
    PORTAL_READY,
    PORTAL_FAILED,
    PORTAL_OFF,
} portal_state_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_status;
static lv_obj_t *s_detail;
static lv_timer_t *s_timer;
static volatile portal_state_t s_state;
static esp_err_t s_error;

static void tick(lv_timer_t *timer)
{
    (void)timer;
    switch (s_state) {
    case PORTAL_STARTING:
        lv_label_set_text(s_status, "STARTING...");
        break;
    case PORTAL_READY:
        lv_label_set_text(s_status, "WAITING FOR PHONE");
        break;
    case PORTAL_FAILED:
        lv_label_set_text_fmt(s_status, "FAILED: %s", esp_err_to_name(s_error));
        s_state = PORTAL_OFF;
        break;
    default:
        break;
    }
}

void demo_portal_enter(void)
{
    s_error = ESP_OK;
    s_state = PORTAL_STARTING;

    s_scr = ui_pixel_screen_create("配网");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 12, 54, 216, 206, UI_PAPER);

    s_status = ui_pixel_label(panel, "STARTING...", &lv_font_montserrat_14, UI_RED);
    lv_obj_set_width(s_status, 190);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 4);

    s_detail = ui_pixel_label(panel, "", &lv_font_montserrat_14, UI_INK);
    lv_obj_set_width(s_detail, 190);
    lv_obj_align(s_detail, LV_ALIGN_TOP_LEFT, 2, 40);

    ui_pixel_mascot_create(s_scr, 101, 266);

    s_timer = lv_timer_create(tick, 200, NULL);
    lv_screen_load(s_scr);

    esp_err_t err = app_portal_start();
    if (err == ESP_OK) {
        s_state = PORTAL_READY;
        lv_label_set_text_fmt(s_detail,
            "AP: %s\nPASS: %s\n\nOpen in browser:\nhttp://192.168.4.1\n\n"
            "Page auto-scans nearby Wi-Fi;\npick from the dropdown.\n\n"
            "Long-press OK to return.",
            app_portal_ssid(), app_portal_password());
    } else {
        s_error = err;
        s_state = PORTAL_FAILED;
        ESP_LOGE(TAG, "门户启动失败: %s", esp_err_to_name(err));
    }
}

void demo_portal_exit(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    app_portal_stop();
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status = s_detail = NULL;
    }
}

void demo_portal_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    // 本页不定义按键动作;长按 OK 返回菜单由 main 统一拦截。
    (void)btn;
    (void)ev;
}
