// main/app_net.h —— 全局网络服务:应用内唯一持有 Wi-Fi STA 的组件。
//
// 设计目标(P1):让 Wi-Fi 从"每个页面各自 init/deinit"收敛为一处持有,
// 从而通知服务可以在任意页面常驻接收,不再与额度/扫描/配网页抢占 Wi-Fi。
//
// 生命周期:
//   app_main 调用 app_net_init() 完成 NVS/netif/事件循环准备;
//   配置就绪后调用 app_net_start() 启动 STA 并后台自动重连;
//   进入配网(AP)前调用 app_net_stop() 释放 Wi-Fi,退出后再 app_net_start() 恢复。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// 初始化(幂等):准备 NVS、netif、默认事件循环。不启动 Wi-Fi。
esp_err_t app_net_init(void);

// 启动 STA(幂等):注册事件处理、后台自动重连。
// 有 Wi-Fi 配置则连接;无配置仅启动(供扫描页使用),不连接。
// 非阻塞:返回时连接可能仍在进行,用 app_net_is_up() 查询。
esp_err_t app_net_start(void);

// 停止并释放 STA(Wi-Fi deinit + netif 销毁)。供配网页接管 AP 前调用。
// 同步返回:返回后 Wi-Fi 已完全释放,可安全再次 esp_wifi_init。
void app_net_stop(void);

// 是否已拿到 IP。
bool app_net_is_up(void);

// 复制当前 IP 到 buf(如 "192.168.0.109");未联网返回 false。
bool app_net_get_ip(char *buf, size_t len);

// SNTP 是否已同步(用于额度页时钟)。
bool app_net_time_synced(void);

// 在共享 STA 上发起一次异步扫描;调用方自行监听 WIFI_EVENT_SCAN_DONE。
esp_err_t app_net_scan_start(void);
