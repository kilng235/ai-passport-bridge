// main/power_idle.h —— 全局空闲熄屏:周期检查无操作时长,分级调暗/熄屏。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// 启动 1s 周期检查(幂等)。
esp_err_t power_idle_init(void);

// 有按键活动:重置计时并立即恢复到设定亮度(默认 100%)。
void power_idle_note_activity(void);

// 设定"恢复档"亮度(0-100,非法值按 100 处理);空闲调暗/熄屏分级不受影响。
// 亮度设置页保存后调用,让唤醒回到用户选择而非固定全亮。
void power_idle_set_on_level(uint8_t percent);

// 设定熄屏超时秒数(0 = 从不熄屏,或 30/60/180 秒)。
void power_idle_set_timeout_sec(uint16_t sec);

// 获取当前设定的熄屏超时秒数。
uint16_t power_idle_get_timeout_sec(void);

// 需要自行控制背光的页面可临时关闭(关闭期间不改变亮度)。
void power_idle_set_enabled(bool enabled);
