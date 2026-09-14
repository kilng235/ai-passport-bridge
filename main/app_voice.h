// main/app_voice.h —— 8-bit 对讲机(PTT)音频采集与上传。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_IDLE = 0,
    VOICE_RECORDING,
    VOICE_UPLOADING,   // 录音上传/识别中,或正在发送/撤销
    VOICE_REVIEW,      // 已识别,等待用户决定(发送/继续说/撤销/重录)
    VOICE_DONE,
    VOICE_FAILED,
} voice_state_t;

// 初始化语音对讲模块
esp_err_t app_voice_init(void);

// 短按开始一段新录音(丢弃上一段)
void app_voice_start_record(void);

// "继续说":在上一段基础上追加录音(PC 侧拼接后再识别)
void app_voice_start_append(void);

// 再短按:结束录音并上传识别(识别后进入 VOICE_REVIEW,不自动注入)
void app_voice_stop_record(void);

// 确认发送:把当前识别结果注入会话
void app_voice_commit(void);

// 撤销:丢弃当前识别结果
void app_voice_cancel(void);

// 获取当前状态
voice_state_t app_voice_get_state(void);

// 获取实时音量电平 (0 ~ 100)，用于 8-bit 跳动声波
int app_voice_get_level(void);

// 获取录音时长 (ms)
uint32_t app_voice_get_duration_ms(void);

// 获取识别返回的文本（用于在屏幕展示）
const char *app_voice_get_result_text(void);

#ifdef __cplusplus
}
#endif
