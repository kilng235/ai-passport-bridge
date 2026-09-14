// main/app_beep.h —— 通知提示音:按通知类型播放短促音调,放完挂起音频省电。
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "app_notify_logic.h"   // app_notify_kind_t

// 开机初始化:建提示音后台任务(幂等)。不占用音频通路。
esp_err_t app_beep_init(void);

// 非阻塞:把类型投递给音频任务,由它生成波形并写 I2S。静音时段内不发声。
void app_beep_play(app_notify_kind_t kind);

// 试听:无视静音时段(提示音页用户主动按键时,静音窗内也要有反馈)。
void app_beep_preview(app_notify_kind_t kind);

// 当前时刻是否处于静音时段内(时钟未对上时恒为 false)。
bool app_beep_in_quiet(void);

// 静音时段预设:0 关 / 1 = 22:00-08:00 / 2 = 23:00-07:00。设置后立即生效。
int  app_beep_quiet_preset(void);
void app_beep_set_quiet_preset(int index);
const char *app_beep_quiet_preset_label(int index);   // 越界按 0(关)返回

// 运行期开关(默认开)。
void app_beep_set_enabled(bool enabled);
bool app_beep_enabled(void);

// 音量 0..100(默认 70)。
void app_beep_set_volume(uint8_t percent);
uint8_t app_beep_volume(void);

// 把当前开关/音量/静音时段写入 NVS(namespace pcfg);成功返回 true。
bool app_beep_save(void);
