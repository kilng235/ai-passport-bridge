// main/app_notify_logic.h —— 通知数据模型与解析(纯逻辑,零 ESP-IDF 依赖,可主机测试)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_NOTIFY_IDLE = 0,   // 空闲/待命(默认;未知 kind 也归此)
    APP_NOTIFY_RUNNING,    // 任务进行中(session.status = busy,含 retry)
    APP_NOTIFY_DONE,       // 任务完成(session.idle)
    APP_NOTIFY_ALERT,      // 需要确认(permission.asked / session.error)
} app_notify_kind_t;

#define APP_NOTIFY_TITLE_MAX 63
#define APP_NOTIFY_TEXT_MAX  191
#define APP_NOTIFY_NAME_MAX  95    // 会话名(UTF-8 字节;约 31 个汉字,支持长标题跑马灯)
#define APP_NOTIFY_SESS_MAX  4     // 通知页最多显示 4 行会话(单行循环滚动跑马灯)

typedef struct {
    app_notify_kind_t kind;
    char name[APP_NOTIFY_NAME_MAX + 1];
} app_notify_session_t;

typedef struct {
    app_notify_kind_t kind;
    char    title[APP_NOTIFY_TITLE_MAX + 1];
    char    text[APP_NOTIFY_TEXT_MAX + 1];
    int64_t time_ms;    // epoch 毫秒;0 = 未知
    bool    valid;
    int     count;      // 会话总数(插件给出;0 = 未知)
    int     sess_count; // 实际解析到的会话数(<= APP_NOTIFY_SESS_MAX)
    app_notify_session_t sessions[APP_NOTIFY_SESS_MAX];
} app_notify_msg_t;

// "idle"/"running"/"done"/"alert" → 枚举;未知按 idle("error" 兼容为 alert)。
app_notify_kind_t app_notify_kind_from_str(const char *s);
const char       *app_notify_kind_str(app_notify_kind_t k);

// running 的保鲜期:agent 被打断/进程退出时没有终止事件(钩子只在回合边界
// 和工具调用处触发),超期未刷新的 running 按 idle 展示,避免设备永远"进行中"。
#define APP_NOTIFY_RUNNING_FRESH_MS (5LL * 60 * 1000)

// 有效状态:running 超过保鲜期未刷新 → idle;其余状态原样透传(不受时限)。
app_notify_kind_t app_notify_kind_effective(app_notify_kind_t kind, int64_t age_ms);

// 状态优先级(用于排序/聚合):alert=3 > running=2 > done=1 > idle=0。
int app_notify_kind_rank(app_notify_kind_t k);

// 解析 JSON:{"kind","title","text","time","count","sessions":[{"name","kind"},...]}
// 容错:缺字段用默认;字符串处理 \" \\ \n \t \r \/;超长截断;会话数组最多 SESS_MAX 条。
// 非 JSON 且非空 → 整段作为 text。title/text/sessions 皆空 → 返回 false。
bool app_notify_parse(const char *json, app_notify_msg_t *out);
