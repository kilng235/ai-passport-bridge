// main/app_portal.h —— 极简 SoftAP 配置门户(参考社区 TOKEN JOURNEY 模式)。
// AP 模式,手机连热点后手动访问 http://192.168.4.1;只有 "/" 与 "/save" 两条路由,
// 不做 DNS 劫持/强制门户;保存成功后设备重启。
#pragma once

#include <stdbool.h>
#include "app_cfg.h"
#include "esp_err.h"

// 启动/停止门户热点。重复 start 幂等。
esp_err_t app_portal_start(void);
void app_portal_stop(void);
bool app_portal_running(void);
const char *app_portal_ssid(void);
const char *app_portal_password(void);
