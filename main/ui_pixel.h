#pragma once

#include "lvgl.h"
#include "ui_theme.h"

// 配色已统一到深色琥珀主题(借鉴 leo-radio)。旧名字保留为语义别名,
// 让 Wi-Fi/蓝牙/低功耗/配网等旧页面不改代码即完成换肤:
// 原"纸白面板+墨色描边"翻转为"深色面板+亮色文字",对比关系保持。
#define UI_SKY        UI_THEME_BG         // 屏幕背景(原天蓝)
#define UI_SKY_DARK   UI_THEME_AMBER      // 强调文字(原深蓝)
#define UI_INK        UI_THEME_TEXT       // 面板上的文字(原墨色)
#define UI_PAPER      UI_THEME_PANEL      // 面板底色(原纸白)
#define UI_GRASS      UI_THEME_GRID       // 结构件/边框(原草绿)
#define UI_GRASS_DARK UI_THEME_PANEL_SOFT // 结构件暗部
#define UI_YELLOW     UI_THEME_AMBER      // 选中态
#define UI_ORANGE     0xFFB23E            // 吉祥物围巾(保留)
#define UI_RED        UI_THEME_RED        // 错误
#define UI_MUTED      UI_THEME_MUTED      // 弱化文字

lv_obj_t *ui_pixel_screen_create(const char *title);
lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color);
lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color);
lv_obj_t *ui_pixel_mascot_create(lv_obj_t *parent, int x, int y);
void ui_pixel_mascot_jump(lv_obj_t *mascot);
void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled);
