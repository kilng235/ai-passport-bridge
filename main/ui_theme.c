#include "ui_theme.h"
#include "ui_pixel.h"
#include "bsp_pins.h"   // BSP_LCD_W/H:底屏铺满面板分辨率

lv_obj_t *ui_theme_box(lv_obj_t *parent, int x, int y, int w, int h,
                       uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}

lv_obj_t *ui_theme_label(lv_obj_t *parent, const char *text, int x, int y,
                         int w, uint32_t color, const lv_font_t *font)
{
    // ui_pixel_label 负责按文本内容选 CJK/Montserrat 字体。
    lv_obj_t *label = ui_pixel_label(parent, text, font, color);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

void ui_theme_center(lv_obj_t *label)
{
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

lv_obj_t *ui_theme_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_THEME_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return scr;
}

lv_obj_t *ui_theme_divider(lv_obj_t *parent, int x, int y, int w)
{
    return ui_theme_box(parent, x, y, w, 1, UI_THEME_GRID, 0);
}

lv_obj_t *ui_theme_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *panel = ui_theme_box(parent, x, y, w, h, UI_THEME_PANEL, 0); // 8-bit 直角像素框
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_THEME_GRID), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    return panel;
}

void ui_theme_select_row(lv_obj_t *row, bool selected)
{
    lv_obj_set_style_bg_color(row, lv_color_hex(selected ? UI_THEME_AMBER_SOFT
                                                         : UI_THEME_PANEL), 0);
}
