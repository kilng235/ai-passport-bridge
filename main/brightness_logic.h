// main/brightness_logic.h —— 背光亮度调节的纯逻辑(零依赖,可主机测试)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 可设定亮度的范围与步进。下限取空闲调暗档(20%):再低的话,"调暗"反而比用户设定更亮。
#define BRIGHTNESS_MIN_PERCENT 20
#define BRIGHTNESS_MAX_PERCENT 100
#define BRIGHTNESS_STEP        10
#define BRIGHTNESS_DEFAULT     BRIGHTNESS_MAX_PERCENT

// 是否为可直接使用的亮度值(判断 NVS 读出的原始值)。
bool brightness_is_valid(int percent);

// 收敛到 [MIN, MAX];步进与 NVS 存储前的兜底都用它。
uint8_t brightness_clamp(int percent);

// 按 dir(+1 上调 / -1 下调 / 0 不变)步进一档,越界停在边界。
uint8_t brightness_step(uint8_t current, int dir);
