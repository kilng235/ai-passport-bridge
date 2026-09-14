// main/power_idle_logic.h —— 空闲背光分级的纯逻辑(零依赖,可主机测试)。
#pragma once

#include <stdint.h>

// 无操作 30s 降到 20%,60s 熄屏;任意按键恢复用户设定的亮度(见 power_idle.h)。
// 本文件只产出 0/20/100 三个分级标记,"100"是亮档标记,由 power_idle.c 映射到用户设定值。
#define POWER_IDLE_DIM_AFTER_US (30LL * 1000 * 1000)
#define POWER_IDLE_OFF_AFTER_US (60LL * 1000 * 1000)
#define POWER_IDLE_DIM_PERCENT  20
#define POWER_IDLE_ON_PERCENT   100

// 依据空闲时长返回目标背光百分比(0 = 熄屏)。
uint8_t power_idle_target(int64_t idle_us);
