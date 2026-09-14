// main/main.c —— FoloToy AI Passport BSP 驱动参考示例:初始化 + 两级菜单 + 按键分发。
//
// 导航:
//   开机直达 Token Quota(未配置 Wi-Fi/Key 时先进入配网)。
//   长按确定:演示页→所属菜单;设置菜单→主页菜单;主页菜单→Token Quota。
//   短按上/下:菜单中移动选中项;演示页中由该页自定义。
//   短按确定:菜单中进入选中项;演示页中由该页自定义。
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_pins.h"      // 错误日志里要打印 BSP_LCD_* 引脚号
#include "demo.h"
#include "app_cfg.h"
#include "app_net.h"
#include "app_notify.h"
#include "app_beep.h"
#include "app_voice.h"
#include "brightness_logic.h"   // BRIGHTNESS_DEFAULT:开机套用已存亮度
#include "power_idle.h"
#include "power_idle_logic.h"
#include "ui_pixel.h"
#include "ui_theme.h"
#include "ui_font.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif
#include <stddef.h>

static const char *TAG = "main";

// ---- 叶子页面(按需初始化,失败在页面内显示) ----
enum {
    PAGE_QUOTA = 0,
    PAGE_NOTIFY,
    PAGE_WIFI,
    PAGE_LOW_POWER,
    PAGE_BRIGHTNESS,
    PAGE_SOUND,
    PAGE_PORTAL,
    PAGE_COUNT,
};

static const demo_entry_t PAGES[PAGE_COUNT] = {
    [PAGE_QUOTA]      = { "Token Quota", demo_quota_enter,      demo_quota_exit,      demo_quota_key      },
    [PAGE_NOTIFY]     = { "通知/桌宠",   demo_notify_enter,     demo_notify_exit,     demo_notify_key     },
    [PAGE_WIFI]       = { "Wi-Fi",       demo_wifi_enter,       demo_wifi_exit,       demo_wifi_key       },
    [PAGE_LOW_POWER]  = { "待机熄屏",    demo_low_power_enter,  demo_low_power_exit,  demo_low_power_key  },
    [PAGE_BRIGHTNESS] = { "亮度",        demo_brightness_enter, demo_brightness_exit, demo_brightness_key },
    [PAGE_SOUND]      = { "提示音",      demo_sound_enter,      demo_sound_exit,      demo_sound_key      },
    [PAGE_PORTAL]     = { "配网",        demo_portal_enter,     demo_portal_exit,     demo_portal_key     },
};

// ---- 菜单(主页 + 设置两个层级) ----
enum { MENU_MAIN = 0, MENU_SETTINGS, MENU_COUNT };

typedef struct {
    const char *name;
    int page;      // 叶子页索引,或 -1
    int submenu;   // 子菜单索引,或 -1
} menu_item_t;

typedef struct {
    const char *title;
    const menu_item_t *items;
    int count;
    int parent;    // 父菜单索引;主页为 -1
} menu_t;

#define MENU_MAX_ITEMS 5

static const menu_item_t MAIN_ITEMS[] = {
    { "Token Quota", PAGE_QUOTA,  -1 },
    { "通知/桌宠",   PAGE_NOTIFY, -1 },
    { "设置",        -1,          MENU_SETTINGS },
};
static const menu_item_t SETTINGS_ITEMS[] = {
    { "Wi-Fi",   PAGE_WIFI,       -1 },
    { "待机熄屏", PAGE_LOW_POWER,  -1 },
    { "亮度",    PAGE_BRIGHTNESS, -1 },
    { "提示音",  PAGE_SOUND,      -1 },
    { "配网",    PAGE_PORTAL,     -1 },
};

static const menu_t MENUS[MENU_COUNT] = {
    [MENU_MAIN]     = { "FoloToy", MAIN_ITEMS,     3, -1 },
    [MENU_SETTINGS] = { "设置",    SETTINGS_ITEMS, 5, MENU_MAIN },
};

static lv_obj_t *s_menu_scr;
static lv_obj_t *s_cards[MENU_MAX_ITEMS];
static lv_obj_t *s_rows[MENU_MAX_ITEMS];
static lv_obj_t *s_mascot;
static int  s_sel;                       // 当前选中项
static int  s_active = -1;               // 当前所在演示页;-1 = 在菜单
static int  s_menu = MENU_MAIN;          // 当前菜单
static int  s_page_return_menu = MENU_MAIN;   // 演示页长按返回的目标菜单
static volatile int s_pending_page = -1; // 页面请求切换的目标页(-1 = 无)

/* 菜单配色:深色琥珀主题(见 ui_theme.h,借鉴 leo-radio) */

static void menu_refresh(void)
{
    const menu_t *m = &MENUS[s_menu];
    for (int i = 0; i < m->count; i++) {
        bool hi = (i == s_sel);
        // leo 设置菜单选中态:琥珀底 + 深色文字;未选:面板底 + 暖白文字。
        lv_obj_set_style_bg_color(s_cards[i],
            lv_color_hex(hi ? UI_THEME_AMBER : UI_THEME_PANEL), 0);
        lv_obj_set_style_text_color(s_rows[i],
            lv_color_hex(hi ? UI_THEME_BG : UI_THEME_TEXT), 0);
    }
}

static void menu_destroy(void)
{
    if (s_menu_scr) {
        lv_obj_delete(s_menu_scr);
        s_menu_scr = NULL;
    }
    s_mascot = NULL;
}

static void menu_build(int menu_idx)
{
    s_menu = menu_idx;
    s_sel = 0;
    s_active = -1;

    const menu_t *m = &MENUS[menu_idx];
    s_menu_scr = ui_theme_screen();

    /* 页头:8-bit 街机标题 */
    const char *title = (m->title && strcmp(m->title, "FoloToy") == 0) ? ":: 系统菜单 ::" : m->title;
    ui_theme_label(s_menu_scr, title, 12, 10, 200, UI_THEME_AMBER,
                   ui_font_pick_14(title));
    ui_theme_divider(s_menu_scr, 12, 34, 216);

    /* 行卡片:8-bit 像素框 */
    const bool compact = m->count > 3;
    const int H = compact ? 38 : 46, GAP = compact ? 6 : 12;
    const int X = 20, W = 200, Y0 = 48;
    for (int i = 0; i < m->count; i++) {
        lv_obj_t *card = ui_theme_box(s_menu_scr, X, Y0 + i * (H + GAP), W, H,
                                      UI_THEME_PANEL, 0); // 8-bit 直角
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(UI_THEME_GRID), 0);
        s_cards[i] = card;

        // 8-bit 卡片像素四角
        ui_theme_box(card, 0, 0, 3, 3, UI_THEME_GRID_HI, 0);
        ui_theme_box(card, W - 3, 0, 3, 3, UI_THEME_GRID_HI, 0);
        ui_theme_box(card, 0, H - 3, 3, 3, UI_THEME_GRID_HI, 0);
        ui_theme_box(card, W - 3, H - 3, 3, 3, UI_THEME_GRID_HI, 0);

        lv_obj_t *row = ui_theme_label(card, m->items[i].name, 0, 0, W,
                                       UI_THEME_TEXT, ui_font_pick_14(m->items[i].name));
        ui_theme_center(row);
        lv_obj_center(row);
        s_rows[i] = row;
    }
    for (int i = m->count; i < MENU_MAX_ITEMS; i++) {
        s_cards[i] = NULL;
        s_rows[i] = NULL;
    }

    s_mascot = NULL;
    ui_theme_divider(s_menu_scr, 12, 292, 216);
    const char *hint_txt = compact ? "[确定]进入  [长按]返回" : "[上下]移动  [确定]进入";
    lv_obj_t *hint = ui_theme_label(s_menu_scr, hint_txt,
                                    12, 298, 216, UI_THEME_MUTED,
                                    ui_font_pick_14(hint_txt));
    ui_theme_center(hint);
    menu_refresh();
    lv_screen_load(s_menu_scr);
}

static void enter_page(int page)
{
    s_page_return_menu = s_menu;
    s_active = page;
    PAGES[page].enter();
}

static void leave_page_to_menu(void)
{
    PAGES[s_active].exit();
    menu_build(s_page_return_menu);
}

// 供演示页请求切换到配置门户页;在 on_key 分发后统一处理,保证先 exit 再 enter。
void app_nav_request_portal(void)
{
    s_pending_page = PAGE_PORTAL;
}

// 按键回调运行在 button 组件的任务里,操作 LVGL 必须加锁。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    power_idle_note_activity();   // 任意按键唤醒并重置空闲计时

    // 按键组件在长按保持期可能重复触发 LONG_PRESS_START:去抖,避免同一次长按
    // 被处理两次(先"退到菜单",又立刻"进入选中页",表现为页面刷新)。
    static int64_t s_last_long_us;
    if (ev == BSP_BTN_LONG) {
        int64_t now = esp_timer_get_time();
        if (now - s_last_long_us < 600 * 1000) return;
        s_last_long_us = now;
    }

    if (!bsp_lvgl_lock(500)) return;

    if (s_active >= 0) {
        // 长按 OK 统一返回所属菜单;通知/桌宠页的录音已改为“短按开始/再短按结束”,
        // 不再占用长按,故这里对所有页面一致处理。
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {     // 统一返回所属菜单
            leave_page_to_menu();
        } else {
            PAGES[s_active].key(btn, ev);
            if (s_pending_page >= 0) {
                int target = s_pending_page;
                s_pending_page = -1;
                if (target >= 0 && target < PAGE_COUNT) {
                    PAGES[s_active].exit();
                    s_active = target;
                    PAGES[s_active].enter();
                }
            }
        }
    } else if (ev == BSP_BTN_CLICK) {
        const menu_t *m = &MENUS[s_menu];
        if (btn == BSP_BTN_UP)   { s_sel = (s_sel + m->count - 1) % m->count; menu_refresh(); }
        if (btn == BSP_BTN_DOWN) { s_sel = (s_sel + 1) % m->count;            menu_refresh(); }
        if (btn == BSP_BTN_OK) {
            const menu_item_t *it = &m->items[s_sel];
            int target = -1;
            if (it->page >= 0) {
                target = it->page;
            } else if (it->submenu >= 0) {
                menu_destroy();
                menu_build(it->submenu);
                bsp_lvgl_unlock();
                return;
            }
            if (target >= 0) {
                ui_pixel_mascot_jump(s_mascot);
                menu_destroy();
                enter_page(target);
            }
        }
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            ui_pixel_mascot_jump(s_mascot);
        }
    } else if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        // 菜单中长按:设置菜单→主页菜单;主页菜单→回 Token Quota。
        int parent = MENUS[s_menu].parent;
        menu_destroy();
        if (parent >= 0) {
            menu_build(parent);
        } else {
            enter_page(PAGE_QUOTA);
        }
    }
    bsp_lvgl_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "FoloToy AI Passport BSP demo 启动");

#if CONFIG_PM_ENABLE
    // DFS:空闲降到 80MHz,负载上来再升到 160MHz;无人持锁时允许自动浅睡。
    // 屏幕亮时 bsp_display 会持 NO_LIGHT_SLEEP 锁,保证 LEDC 背光不停摆。
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 80,
        .light_sleep_enable = true,
    };
    esp_err_t pm_err = esp_pm_configure(&pm_cfg);
    if (pm_err != ESP_OK) ESP_LOGW(TAG, "esp_pm_configure 失败: %s", esp_err_to_name(pm_err));
#endif

    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "休眠唤醒原因: %d", wakeup);
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是本 demo 的 UI 载体,失败就没有菜单可言 —— 打清楚日志后退出,
    // 不做"串口菜单"降级(那会让本文件复杂一倍,违背参考示例的初衷)。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,demo 无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    // 应用已保存的亮度(无存档则全亮),并作为空闲恢复档;NVS 由此提前就绪,后续读取复用。
    uint8_t brightness = BRIGHTNESS_DEFAULT;
    app_cfg_load_brightness(&brightness);
    bsp_display_backlight(brightness);
    power_idle_init();   // 全局空闲变暗/熄屏
    power_idle_set_on_level(brightness);

    // 加载用户设定的待机/熄屏超时秒数(默认 60s)
    uint16_t idle_sec = POWER_IDLE_TIMEOUT_60S;
    if (app_cfg_load_idle_timeout(&idle_sec)) {
        power_idle_set_timeout_sec(idle_sec);
    }

    // 网络与通知(P1):app_net 单点持有 Wi-Fi;通知服务开机常驻,任意页面可收。
    app_net_init();
    app_notify_init();
    app_beep_init();   // 通知提示音后台任务(按类型播放,放完挂起音频)
    app_voice_init();  // 对讲机语音采集模块初始化

    // 按键是导航的唯一输入;失败仍继续显示,但无法操作。
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败,无法导航");
    }

    // 未配置 Wi-Fi 时优先进入配网;配置存于 NVS。
    app_cfg_t cfg;
    bool configured = app_cfg_load(&cfg) && app_cfg_wifi_ready(&cfg) &&
                      app_cfg_keys_ready(&cfg);
    if (configured) {
        // 先起网络与常驻通知,再进页面;连接在后台完成,不阻塞开机。
        app_net_start();
        app_notify_set_enabled(true);
    }
    if (bsp_lvgl_lock(1000)) {
        s_page_return_menu = MENU_MAIN;
        if (!configured) {
            s_active = PAGE_PORTAL;      // 开机直达配网页,不显示主页菜单
            PAGES[PAGE_PORTAL].enter();
        } else {
            s_active = PAGE_QUOTA;       // 开机直达额度页,不显示主页菜单
            PAGES[PAGE_QUOTA].enter();
        }
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "就绪:开机进入 %s", configured ? "Token Quota" : "配网");
}
