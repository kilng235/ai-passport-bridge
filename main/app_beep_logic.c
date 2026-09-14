#include "app_beep_logic.h"

bool app_beep_quiet_valid(const app_beep_quiet_t *w)
{
    if (!w || !w->enabled) return false;
    if (w->start_hour < 0 || w->start_hour > 23) return false;
    if (w->end_hour < 0 || w->end_hour > 23) return false;
    return w->start_hour != w->end_hour;   // 起止相等:窗口宽度为 0,视为未配置
}

bool app_beep_quiet_active(const app_beep_quiet_t *w, int hour)
{
    if (!app_beep_quiet_valid(w)) return false;
    if (hour < 0 || hour > 23) return false;

    if (w->start_hour < w->end_hour) {
        return hour >= w->start_hour && hour < w->end_hour;
    }
    return hour >= w->start_hour || hour < w->end_hour;   // 跨零点窗口
}
