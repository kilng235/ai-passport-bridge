// main/app_notify.h —— 通知服务:保持家庭 Wi-Fi(STA)+ HTTP /notify,接收电脑推送。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "app_notify_logic.h"

// 开机初始化:建存储互斥量 + 后台任务(幂等)。不启动 Wi-Fi。
esp_err_t app_notify_init(void);

// 期望状态:true = 网络在线时保持 HTTP /notify 常驻以接收通知(默认开机常驻)。
// 异步:服务启停在后台任务完成,不阻塞调用方。
void app_notify_set_enabled(bool enabled);
bool app_notify_is_enabled(void);
bool app_notify_is_online(void);

// 同步停止 HTTP 服务(供配网页抢占 80 端口前调用)。
void app_notify_stop_server(void);

// 最新一条通知(供 HTTP handler 写入、通知页读取)。
// 同 kind 的重复推送是"静默刷新":更新内容但不置未读(不响铃/不亮屏)。
void app_notify_post(const app_notify_msg_t *msg);
bool app_notify_get(app_notify_msg_t *out);
bool app_notify_has_new(void);
void app_notify_mark_read(void);

// 距最新一条推送到达过去了多少毫秒(从未收到过推送 = INT64_MAX)。
// 配合 app_notify_kind_effective 做 running 保鲜判定。
int64_t app_notify_age_ms(void);

// 存储版本号:每次落库自增(含静默刷新);页面以此检测内容变化并重绘。
uint32_t app_notify_version(void);

// 最近一次收到 /notify POST 的源地址(即电脑端 Bridge 的局域网 IPv4)。
// 设备据此把语音等反向请求发回桥所在主机;从未收到过推送时返回 false。
bool app_notify_bridge_ip(char *out, size_t out_len);
