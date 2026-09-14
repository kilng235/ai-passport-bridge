#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Wi-Fi 与 NimBLE 都依赖 NVS；只初始化，不在失败时擦除用户数据。
esp_err_t demo_radio_nvs_prepare(void);

// Wi-Fi 默认 STA netif 依赖这两个全局服务。它们按应用生命周期保留。
esp_err_t demo_radio_network_prepare(void);

// ---- 共享 Wi-Fi STA 连接(收音机/城市页使用;额度页含 SNTP 逻辑自持) ----

// 当前 STA 是否已拿到 IP(连接由 demo_radio_wifi_sta_connect 建立)。
bool demo_radio_wifi_sta_up(void);

// 阻塞连接 NVS 里保存的 Wi-Fi:最多等待 15s,拿到 IP 返回 ESP_OK。
// 未配置 Wi-Fi 返回 ESP_ERR_INVALID_STATE。仅可在 worker 任务里调用。
esp_err_t demo_radio_wifi_sta_connect(void);

// 断开并释放 STA(事件句柄/netif 一并清理)。
void demo_radio_wifi_sta_disconnect(void);
