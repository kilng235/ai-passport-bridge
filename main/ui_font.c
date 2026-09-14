#include "ui_font.h"
#include <string.h>

#if LV_FONT_HANSANS_14_CJK

const lv_font_t *ui_font_cjk_14(void)
{
    // CJK 字体本身已含当前 UI 用到的全部汉字/标点,直接作为主字体使用。
    return &lv_font_hansans_14_cjk;
}

// 检测文本是否含 UTF-8 高位字节(= 非 ASCII,当前代表中文)。
static bool has_non_ascii(const char *s)
{
    if (!s) return false;
    while (*s) {
        if (((unsigned char)*s) & 0x80) return true;
        s++;
    }
    return false;
}

const lv_font_t *ui_font_pick_14(const char *text)
{
    // 含中文 → CJK;纯英文 → Montserrat。不依赖 fallback,确定性渲染。
    if (has_non_ascii(text)) return ui_font_cjk_14();
    return &lv_font_montserrat_14;
}

const lv_font_t *ui_font_pick_20(const char *text)
{
    if (has_non_ascii(text)) return ui_font_cjk_14(); /* 仅 14px 中文 */
    return &lv_font_montserrat_20;
}

#endif
