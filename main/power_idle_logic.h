// main/power_idle_logic.h —— 空闲背光分级的纯逻辑(零依赖,可主机测试)。
#pragma once

#include <stdint.h>

// 默认分级:无操作 30s 降到 20%,60s 熄屏;任意按键恢复用户设定的亮度。
#define POWER_IDLE_DIM_PERCENT  20
#define POWER_IDLE_ON_PERCENT   100

// 常见预设超时秒数:0 表示从不熄屏
#define POWER_IDLE_TIMEOUT_NEVER  0
#define POWER_IDLE_TIMEOUT_30S    30
#define POWER_IDLE_TIMEOUT_60S    60
#define POWER_IDLE_TIMEOUT_180S   180

// 依据空闲时长与设定的熄屏超时秒数(off_sec)返回目标背光百分比(0 = 熄屏)。
// off_sec == 0 表示从不熄屏(始终返回 POWER_IDLE_ON_PERCENT)。
// 半程时长降为 POWER_IDLE_DIM_PERCENT,满程降为 0。
uint8_t power_idle_target_custom(int64_t idle_us, uint16_t off_sec);

// 兼容默认 60s 超时的旧接口
uint8_t power_idle_target(int64_t idle_us);
