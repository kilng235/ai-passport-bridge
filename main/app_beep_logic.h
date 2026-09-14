// main/app_beep_logic.h —— 提示音静音时段的纯逻辑(零依赖,可主机测试)。
#pragma once

#include <stdbool.h>

// 静音时段窗口:start_hour(含)到 end_hour(不含),支持跨零点(如 22 -> 8)。
typedef struct {
    bool enabled;    // false = 未启用静音时段
    int  start_hour; // 0..23
    int  end_hour;   // 0..23
} app_beep_quiet_t;

// 窗口参数是否可执行:启用、小时在界内、起止不相等(相等视为未配置)。
bool app_beep_quiet_valid(const app_beep_quiet_t *w);

// hour(0..23)是否落在静音窗内。窗口无效或小时越界一律返回 false(不静音)。
bool app_beep_quiet_active(const app_beep_quiet_t *w, int hour);
