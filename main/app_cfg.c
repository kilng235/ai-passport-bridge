#include "app_cfg.h"

#include <string.h>

// 逐字符拷贝,超长时按 UTF-8 边界截断(避免在多字节序列中间切断)。
static void copy_str(char *dst, size_t len, const char *src)
{
    if (!dst || len == 0) return;
    src = src ? src : "";
    size_t out = 0;
    while (*src && out + 1 < len) {
        unsigned char first = (unsigned char)*src;
        size_t bytes = first < 0x80 ? 1 :
                       (first & 0xE0) == 0xC0 ? 2 :
                       (first & 0xF0) == 0xE0 ? 3 :
                       (first & 0xF8) == 0xF0 ? 4 : 1;
        if (out + bytes >= len) break;

        bool valid = true;
        for (size_t i = 1; i < bytes; i++) {
            if (!src[i] || ((unsigned char)src[i] & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) { src++; continue; }

        memcpy(dst + out, src, bytes);
        out += bytes;
        src += bytes;
    }
    dst[out] = '\0';
}

void app_cfg_init(app_cfg_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = APP_CFG_MAGIC;
    cfg->version = APP_CFG_VERSION;
}

bool app_cfg_validate(const app_cfg_t *cfg)
{
    if (!cfg) return false;
    if (cfg->magic != APP_CFG_MAGIC || cfg->version != APP_CFG_VERSION) return false;

    // 每个字段都必须在其数组范围内以 NUL 结尾,防止损坏 blob 造成越界读取。
    if (!memchr(cfg->wifi_ssid, '\0', sizeof(cfg->wifi_ssid))) return false;
    if (!memchr(cfg->wifi_pass, '\0', sizeof(cfg->wifi_pass))) return false;
    if (!memchr(cfg->deepseek_key, '\0', sizeof(cfg->deepseek_key))) return false;
    if (!memchr(cfg->minimax_key, '\0', sizeof(cfg->minimax_key))) return false;
    return true;
}

bool app_cfg_set_wifi(app_cfg_t *cfg, const char *ssid, const char *pass)
{
    if (!cfg || !ssid || ssid[0] == '\0') return false;
    copy_str(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), ssid);
    copy_str(cfg->wifi_pass, sizeof(cfg->wifi_pass), pass ? pass : "");
    return true;
}

bool app_cfg_set_key(app_cfg_t *cfg, const char *provider, const char *key)
{
    if (!cfg || !provider || !key) return false;
    if (strcmp(provider, "deepseek") == 0) {
        copy_str(cfg->deepseek_key, sizeof(cfg->deepseek_key), key);
        return true;
    }
    if (strcmp(provider, "minimax") == 0) {
        copy_str(cfg->minimax_key, sizeof(cfg->minimax_key), key);
        return true;
    }
    return false;
}

bool app_cfg_wifi_ready(const app_cfg_t *cfg)
{
    return cfg && cfg->wifi_ssid[0] != '\0';
}

bool app_cfg_keys_ready(const app_cfg_t *cfg)
{
    return cfg && cfg->deepseek_key[0] != '\0' && cfg->minimax_key[0] != '\0';
}
