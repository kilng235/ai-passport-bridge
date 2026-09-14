#include "ui_pixel.h"
#include "ui_theme.h"
#include "ui_font.h"

static void start_blink(lv_obj_t *eye);

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color)
{
#if LV_FONT_HANSANS_14_CJK
    // 按文本内容确定性选字:含中文 → CJK 子集,纯英文 → Montserrat。
    // 不依赖内置字体的 fallback(该路径在本环境下实际未生效导致中文空白)。
    if (font == &lv_font_montserrat_14) font = ui_font_pick_14(text);
    else if (font == &lv_font_montserrat_20) font = ui_font_pick_20(text);
#endif
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

lv_obj_t *ui_pixel_screen_create(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_SKY), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // 深色主题页头(leo-radio 设置页样式):居中标题 + 1px 网格分隔线。
    // 面板等子元素从 y=54 起,页头占据 0..42。
    lv_obj_t *heading = ui_pixel_label(scr, title, &lv_font_montserrat_20, UI_INK);
    lv_obj_set_width(heading, 216);
    lv_obj_set_pos(heading, 12, 12);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
    ui_theme_divider(scr, 12, 42, 216);
    return scr;
}

lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color)
{
    // leo-radio 面板:纯色表面 + 1px 网格边框,替代原"墨色阴影+4px 描边"。
    lv_obj_t *panel = block(parent, x, y, w, h, color);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_GRASS), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 7, 0);
    return panel;
}

lv_obj_t *ui_pixel_mascot_create(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *m = lv_obj_create(parent);
    lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(m, x, y);
    lv_obj_set_size(m, 38, 48);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);

    /* 原创“小电视机器人”：天线、发光屏幕脸、橙色围巾与履带脚。 */
    block(m, 18, 0, 3, 6, UI_INK);
    block(m, 16, 0, 7, 3, UI_ORANGE);
    block(m, 3, 6, 32, 24, UI_INK);
    block(m, 0, 12, 5, 10, 0x7557D9);
    block(m, 33, 12, 5, 10, 0x7557D9);
    block(m, 7, 10, 24, 16, 0xB9F3FF);
    lv_obj_t *left_eye = block(m, 11, 14, 4, 6, 0x294B7A);
    lv_obj_t *right_eye = block(m, 23, 14, 4, 6, 0x294B7A);
    block(m, 16, 22, 7, 2, 0x7557D9);
    block(m, 10, 29, 18, 4, UI_ORANGE);
    block(m, 8, 33, 22, 11, 0x7557D9);
    block(m, 3, 35, 5, 7, 0xB9F3FF);
    block(m, 30, 35, 5, 7, 0xB9F3FF);
    block(m, 8, 44, 9, 4, UI_INK);
    block(m, 21, 44, 9, 4, UI_INK);
    start_blink(left_eye);
    start_blink(right_eye);
    return m;
}

static void jump_y(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

static void blink_eye(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void start_blink(lv_obj_t *eye)
{
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, eye);
    lv_anim_set_exec_cb(&anim, blink_eye);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_duration(&anim, 70);
    lv_anim_set_playback_duration(&anim, 70);
    lv_anim_set_repeat_delay(&anim, 1700);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_step);
    lv_anim_start(&anim);
}

void ui_pixel_mascot_jump(lv_obj_t *mascot)
{
    if (!mascot) return;
    int y = lv_obj_get_y(mascot);
    lv_anim_delete(mascot, jump_y);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, mascot);
    lv_anim_set_exec_cb(&anim, jump_y);
    lv_anim_set_values(&anim, y, y - 5);
    lv_anim_set_duration(&anim, 110);
    lv_anim_set_playback_duration(&anim, 140);
    lv_anim_set_path_cb(&anim, lv_anim_path_step);
    lv_anim_start(&anim);
}

void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled)
{
    // leo-radio 的 Wi-Fi 列表选中样式:暗琥珀底/常规面板底;不可用则次级面板。
    uint32_t color = !enabled ? UI_GRASS_DARK
                     : selected ? UI_YELLOW : UI_PAPER;
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
}
