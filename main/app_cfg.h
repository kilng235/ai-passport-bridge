// main/app_cfg.h —— 配置模型(纯逻辑,零 ESP-IDF/LVGL 依赖,可主机测试)。
// 整份配置作为 NVS blob 存储;头部 magic+version 用于校验。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_CFG_MAGIC    0x50434647u  // 'PCFG'
#define APP_CFG_VERSION  1u
#define APP_CFG_SSID_MAX 33           // 802.11 SSID 上限 32 字节 + NUL
#define APP_CFG_PASS_MAX 65           // WPA2 密码上限 63 字节 + NUL
#define APP_CFG_KEY_MAX  192          // API Key 缓冲(MiniMax 的 sk-cp 长 key 可达 125+)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    char wifi_ssid[APP_CFG_SSID_MAX];
    char wifi_pass[APP_CFG_PASS_MAX];
    char deepseek_key[APP_CFG_KEY_MAX];
    char minimax_key[APP_CFG_KEY_MAX];
} app_cfg_t;

// 清空并写入 magic/version。
void app_cfg_init(app_cfg_t *cfg);

// 校验结构:magic、version、各字段在界内以 NUL 结尾。
bool app_cfg_validate(const app_cfg_t *cfg);

// 写入一组 Wi-Fi(ssid 必填);超长按 UTF-8 边界安全截断。
bool app_cfg_set_wifi(app_cfg_t *cfg, const char *ssid, const char *pass);

// 写入平台 Key:provider 取 "deepseek" / "minimax";key 允许为空(清空)。
bool app_cfg_set_key(app_cfg_t *cfg, const char *provider, const char *key);

bool app_cfg_wifi_ready(const app_cfg_t *cfg);
bool app_cfg_keys_ready(const app_cfg_t *cfg);

// NVS blob 存取(实现见 app_portal.c)。无有效配置时 load 返回 false。
bool app_cfg_save(const app_cfg_t *cfg);
bool app_cfg_load(app_cfg_t *cfg);

// 背光亮度存独立 NVS 整型键:不属于 portal 表单写的那份 blob,免去为单项设置迁移整个配置结构。
// 保存前收敛到合法区间;load 在无值/值非法时返回 false,*out 保持不变。
bool app_cfg_save_brightness(uint8_t percent);
bool app_cfg_load_brightness(uint8_t *out_percent);

// 语音服务共享 Token(独立 NVS 字符串键,同样不进 blob)。空串 = 清除。
#define APP_CFG_TOKEN_MAX 65
bool app_cfg_save_voice_token(const char *token);
bool app_cfg_load_voice_token(char *out, size_t out_len);
