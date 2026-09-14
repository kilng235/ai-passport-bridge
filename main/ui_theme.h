// main/ui_theme.h —— 深色琥珀主题:借鉴 leo-radio(https://github.com/leo0183/leo-radio)
// 的设计语言 —— 近黑蓝底、琥珀强调、网格线分隔、底部操作提示。
// 新页面直接用这里的构件;旧浅色页面经由 ui_pixel.h 的同名常量重映射自动换肤。
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#define UI_THEME_BG         0x0A0A0C   // 纯正 8-bit CRT 黑
#define UI_THEME_PANEL      0x14181F   // 像素卡片底色
#define UI_THEME_PANEL_SOFT 0x1B222D   // 像素次级面板
#define UI_THEME_GRID       0x334455   // 8-bit 像素框外描边
#define UI_THEME_GRID_HI    0x557799   // 8-bit 高光描边
#define UI_THEME_TEXT       0xF8F9FA   // 像素纯亮白
#define UI_THEME_MUTED      0x6C7A89   // 8-bit 暗灰
#define UI_THEME_AMBER      0xF5A623   // 8-bit 经典金币黄
#define UI_THEME_AMBER_SOFT 0x5C3E0E   // 8-bit 选中底色
#define UI_THEME_CYAN       0x00E5FF   // 8-bit 霓虹青
#define UI_THEME_GREEN      0x00E676   // 8-bit 像素鲜绿
#define UI_THEME_RED        0xFF1744   // 8-bit 危险红

// 纯色块:remove_style_all 后只设背景与圆角,继承 leo 的 make_box。
lv_obj_t *ui_theme_box(lv_obj_t *parent, int x, int y, int w, int h,
                       uint32_t color, int radius);

// 标签:在 ui_pixel_label 的中英混排选字之上,补坐标/宽度/DOT 截断。
lv_obj_t *ui_theme_label(lv_obj_t *parent, const char *text, int x, int y,
                         int w, uint32_t color, const lv_font_t *font);
void ui_theme_center(lv_obj_t *label);

// 空的深色底屏。
lv_obj_t *ui_theme_screen(void);

// 1px 网格分隔线(leo 用于顶栏下沿与底部提示上沿)。
lv_obj_t *ui_theme_divider(lv_obj_t *parent, int x, int y, int w);

// 面板:主题表面色 + 1px 网格边框 + 8px 圆角。
lv_obj_t *ui_theme_panel(lv_obj_t *parent, int x, int y, int w, int h);

// 行选中态(leo 的 Wi-Fi 列表式):选中 = 暗琥珀底,未选 = 面板底。
// 文字颜色由调用方自理,配合 UI_THEME_TEXT 两种底色都保持可读。
void ui_theme_select_row(lv_obj_t *row, bool selected);
