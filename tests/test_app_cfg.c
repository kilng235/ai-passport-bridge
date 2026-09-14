#include <assert.h>
#include <string.h>
#include "app_cfg.h"

int main(void)
{
    app_cfg_t cfg;
    app_cfg_init(&cfg);
    assert(cfg.magic == APP_CFG_MAGIC);
    assert(cfg.version == APP_CFG_VERSION);
    assert(app_cfg_validate(&cfg));
    assert(!app_cfg_wifi_ready(&cfg));
    assert(!app_cfg_keys_ready(&cfg));

    // Wi-Fi set/clear and validation
    assert(app_cfg_set_wifi(&cfg, "MyNet", "pw123456"));
    assert(strcmp(cfg.wifi_ssid, "MyNet") == 0);
    assert(strcmp(cfg.wifi_pass, "pw123456") == 0);
    assert(app_cfg_wifi_ready(&cfg));
    assert(!app_cfg_set_wifi(&cfg, "", "x"));
    assert(!app_cfg_set_wifi(NULL, "a", "b"));

    // Provider keys
    assert(app_cfg_set_key(&cfg, "deepseek", "sk-ds"));
    assert(app_cfg_set_key(&cfg, "minimax", "sk-mm"));
    assert(strcmp(cfg.deepseek_key, "sk-ds") == 0);
    assert(strcmp(cfg.minimax_key, "sk-mm") == 0);
    assert(app_cfg_keys_ready(&cfg));
    assert(!app_cfg_set_key(&cfg, "unknown", "x"));
    assert(app_cfg_set_key(&cfg, "deepseek", ""));
    assert(!app_cfg_keys_ready(&cfg));

    // Corrupt header or missing terminator is rejected
    app_cfg_t bad;
    app_cfg_init(&bad);
    bad.magic = 0;
    assert(!app_cfg_validate(&bad));
    app_cfg_init(&bad);
    bad.version = 99;
    assert(!app_cfg_validate(&bad));
    app_cfg_init(&bad);
    memset(bad.wifi_ssid, 'a', sizeof(bad.wifi_ssid));
    assert(!app_cfg_validate(&bad));
    assert(!app_cfg_validate(NULL));

    // Overlong SSID truncates to the field limit and stays valid
    char long_ssid[200];
    memset(long_ssid, 'a', sizeof(long_ssid) - 1);
    long_ssid[sizeof(long_ssid) - 1] = '\0';
    assert(app_cfg_set_wifi(&cfg, long_ssid, ""));
    assert(strlen(cfg.wifi_ssid) == APP_CFG_SSID_MAX - 1);
    assert(app_cfg_validate(&cfg));

    // Multi-byte truncation must drop whole UTF-8 chars (3-byte "中")
    app_cfg_t u;
    app_cfg_init(&u);
    char mb[64];
    size_t pos = 0;
    for (int i = 0; i < 11; i++) {
        mb[pos++] = (char)0xE4;
        mb[pos++] = (char)0xB8;
        mb[pos++] = (char)0xAD;
    }
    mb[pos] = '\0';
    assert(app_cfg_set_wifi(&u, mb, ""));
    assert(strlen(u.wifi_ssid) == 30);   // 10 whole chars fit in 32 bytes
    assert(app_cfg_validate(&u));

    return 0;
}
