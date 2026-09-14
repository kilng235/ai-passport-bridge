#include <assert.h>
#include "app_beep_logic.h"

int main(void)
{
    // valid:必须启用、小时在界内、起止不相等。
    const app_beep_quiet_t off = {false, 22, 8};
    assert(!app_beep_quiet_valid(&off));                 // 未启用
    const app_beep_quiet_t w = {true, 22, 8};
    assert(app_beep_quiet_valid(&w));
    const app_beep_quiet_t bad_hour = {true, 24, 8};
    assert(!app_beep_quiet_valid(&bad_hour));
    const app_beep_quiet_t neg_hour = {true, -1, 8};
    assert(!app_beep_quiet_valid(&neg_hour));
    const app_beep_quiet_t zero_width = {true, 8, 8};
    assert(!app_beep_quiet_valid(&zero_width));          // 相等 = 未配置
    const app_beep_quiet_t same_day = {true, 10, 18};
    assert(app_beep_quiet_valid(&same_day));
    const app_beep_quiet_t bad_end = {true, 10, 24};
    assert(!app_beep_quiet_valid(&bad_end));

    // 跨零点窗口 22-8:22..23 与 0..7 静音,8..21 不静音。
    assert(app_beep_quiet_active(&w, 22));
    assert(app_beep_quiet_active(&w, 23));
    assert(app_beep_quiet_active(&w, 0));
    assert(app_beep_quiet_active(&w, 7));
    assert(!app_beep_quiet_active(&w, 8));
    assert(!app_beep_quiet_active(&w, 12));
    assert(!app_beep_quiet_active(&w, 21));
    // 语义:22:00 起静音、08:00 整点解除(左闭右开)。

    // 同日窗口 10-18。
    assert(app_beep_quiet_active(&same_day, 10));
    assert(app_beep_quiet_active(&same_day, 17));
    assert(!app_beep_quiet_active(&same_day, 18));
    assert(!app_beep_quiet_active(&same_day, 9));

    // 无效窗口/越界小时:一律不静音。
    assert(!app_beep_quiet_active(&off, 23));
    assert(!app_beep_quiet_active(&zero_width, 12));
    assert(!app_beep_quiet_active(&w, -1));
    assert(!app_beep_quiet_active(&w, 24));
    return 0;
}
