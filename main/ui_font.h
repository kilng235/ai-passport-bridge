// main/ui_font.h —— 中英混排字体:以 Montserrat 为主,缺失的中文字形回退到按需生成的 CJK 字体。
#pragma once

#include "lvgl.h"

#if LV_FONT_HANSANS_14_CJK
// 按需生成的中文字形子集(定义在 lv_font_hansans_14_cjk.c)。
LV_FONT_DECLARE(lv_font_hansans_14_cjk);
// 直接使用的中文字体:内含当前 UI 用到的全部汉字/标点。
const lv_font_t *ui_font_cjk_14(void);
// 14px 混排选择:文本含非 ASCII(中文)→ CJK 14,否则 → Montserrat 14。
const lv_font_t *ui_font_pick_14(const char *text);
// 20px 混排选择:文本含非 ASCII(中文)→ CJK 14(仅 14px 中文),否则 → Montserrat 20。
const lv_font_t *ui_font_pick_20(const char *text);
#endif
