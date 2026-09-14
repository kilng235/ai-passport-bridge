#include <assert.h>
#include <string.h>
#include "app_notify_logic.h"

int main(void)
{
    // 枚举映射:已知值精确匹配,未知/空按 idle;error 兼容为 alert。
    assert(app_notify_kind_from_str("running") == APP_NOTIFY_RUNNING);
    assert(app_notify_kind_from_str("done")  == APP_NOTIFY_DONE);
    assert(app_notify_kind_from_str("alert") == APP_NOTIFY_ALERT);
    assert(app_notify_kind_from_str("idle")  == APP_NOTIFY_IDLE);
    assert(app_notify_kind_from_str("error") == APP_NOTIFY_ALERT);   // 旧 error 并入 alert
    assert(app_notify_kind_from_str("DONE")  == APP_NOTIFY_IDLE);    // 大小写敏感 -> 未知
    assert(app_notify_kind_from_str(NULL)    == APP_NOTIFY_IDLE);
    assert(strcmp(app_notify_kind_str(APP_NOTIFY_ALERT), "alert") == 0);
    assert(strcmp(app_notify_kind_str(APP_NOTIFY_RUNNING), "running") == 0);

    // 完整 JSON。
    app_notify_msg_t m;
    assert(app_notify_parse(
        "{\"kind\":\"alert\",\"title\":\"需要确认\",\"text\":\"Bash: rm -rf\",\"time\":1731234567890}",
        &m));
    assert(m.valid);
    assert(m.kind == APP_NOTIFY_ALERT);
    assert(strcmp(m.title, "需要确认") == 0);
    assert(strcmp(m.text, "Bash: rm -rf") == 0);
    assert(m.time_ms == 1731234567890LL);

    // 缺字段:kind 默认 idle;time 默认 0。
    assert(app_notify_parse("{\"title\":\"任务完成\"}", &m));
    assert(m.kind == APP_NOTIFY_IDLE);
    assert(strcmp(m.title, "任务完成") == 0);
    assert(m.text[0] == '\0');
    assert(m.time_ms == 0);

    // 字段顺序无关 + 字符串转义。
    assert(app_notify_parse("{\"text\":\"a\\\"b\\\\c\",\"title\":\"t\"}", &m));
    assert(strcmp(m.text, "a\"b\\c") == 0);
    assert(strcmp(m.title, "t") == 0);

    // 名字里含 "title" 的水印不应被误当字段(扫描需跳过字符串内容)。
    assert(app_notify_parse("{\"title\":\"x\",\"text\":\"no title here\"}", &m));
    assert(strcmp(m.text, "no title here") == 0);

    // 非 JSON 非空 → 整段当正文。
    assert(app_notify_parse("plain text", &m));
    assert(strcmp(m.text, "plain text") == 0);

    // 空 / 无有效字段 → false。
    assert(!app_notify_parse("", &m));
    assert(!app_notify_parse("{}", &m));

    // 超长截断:不超过缓冲上界。
    char big[512];
    big[0] = '{';
    strcpy(big + 1, "\"text\":\"");
    size_t off = strlen(big);
    for (int i = 0; i < 400; i++) big[off + i] = 'a';
    strcpy(big + off + 400, "\"}");
    assert(app_notify_parse(big, &m));
    assert(strlen(m.text) == APP_NOTIFY_TEXT_MAX);

    // 优先级:confirm(alert) > running > done > idle。
    assert(app_notify_kind_rank(APP_NOTIFY_ALERT)   == 3);
    assert(app_notify_kind_rank(APP_NOTIFY_RUNNING) == 2);
    assert(app_notify_kind_rank(APP_NOTIFY_DONE)    == 1);
    assert(app_notify_kind_rank(APP_NOTIFY_IDLE)    == 0);

    // 多会话:sessions 数组 + count;聚合 kind 在顶层。
    assert(app_notify_parse(
        "{\"kind\":\"running\",\"count\":2,\"sessions\":["
        "{\"name\":\"build\",\"kind\":\"running\"},"
        "{\"name\":\"Hello Kitty风格角色\",\"kind\":\"done\"}]}",
        &m));
    assert(m.sess_count == 2);
    assert(m.count == 2);
    assert(strcmp(m.sessions[0].name, "build") == 0);
    assert(m.sessions[0].kind == APP_NOTIFY_RUNNING);
    assert(strcmp(m.sessions[1].name, "Hello Kitty风格角色") == 0);
    assert(m.sessions[1].kind == APP_NOTIFY_DONE);

    // 数组里的 kind 不得污染顶层 kind(深度 0 才匹配)。
    assert(app_notify_parse(
        "{\"sessions\":[{\"name\":\"A\",\"kind\":\"alert\"}],\"kind\":\"done\"}", &m));
    assert(m.kind == APP_NOTIFY_DONE);
    assert(m.sess_count == 1);
    assert(m.sessions[0].kind == APP_NOTIFY_ALERT);

    // 无名条目跳过;缺 kind 默认 idle;超过 SESS_MAX 丢弃。
    assert(app_notify_parse(
        "{\"kind\":\"idle\",\"sessions\":["
        "{\"kind\":\"running\"},"
        "{\"name\":\"B\"},"
        "{\"name\":\"C\",\"kind\":\"bogus\"},"
        "{\"name\":\"D\",\"kind\":\"running\"},"
        "{\"name\":\"E\",\"kind\":\"done\"},"
        "{\"name\":\"F\",\"kind\":\"alert\"}]}",
        &m));
    assert(m.sess_count == APP_NOTIFY_SESS_MAX);
    assert(strcmp(m.sessions[0].name, "B") == 0);
    assert(m.sessions[0].kind == APP_NOTIFY_IDLE);
    assert(strcmp(m.sessions[1].name, "C") == 0);
    assert(m.sessions[1].kind == APP_NOTIFY_IDLE);   // 未知 -> idle

    // 名字含 '}' 也不应截断对象解析(字符串感知)。
    assert(app_notify_parse(
        "{\"sessions\":[{\"name\":\"a}b\",\"kind\":\"done\"}]}", &m));
    assert(m.sess_count == 1);
    assert(strcmp(m.sessions[0].name, "a}b") == 0);

    // 只有 sessions(无 title/text)也算有效。
    assert(app_notify_parse("{\"sessions\":[{\"name\":\"X\",\"kind\":\"running\"}]}", &m));
    assert(m.sess_count == 1);

    // 有效状态:running 超保鲜期未刷新 → idle(打断/进程退出无终止事件);
    // 其余状态不受时限;边界值(恰好等于保鲜期)不算超期。
    assert(app_notify_kind_effective(APP_NOTIFY_RUNNING, 0) == APP_NOTIFY_RUNNING);
    assert(app_notify_kind_effective(APP_NOTIFY_RUNNING, 1000) == APP_NOTIFY_RUNNING);
    assert(app_notify_kind_effective(APP_NOTIFY_RUNNING, APP_NOTIFY_RUNNING_FRESH_MS)
           == APP_NOTIFY_RUNNING);
    assert(app_notify_kind_effective(APP_NOTIFY_RUNNING, APP_NOTIFY_RUNNING_FRESH_MS + 1)
           == APP_NOTIFY_IDLE);
    assert(app_notify_kind_effective(APP_NOTIFY_DONE, APP_NOTIFY_RUNNING_FRESH_MS + 1)
           == APP_NOTIFY_DONE);
    assert(app_notify_kind_effective(APP_NOTIFY_ALERT, APP_NOTIFY_RUNNING_FRESH_MS + 1)
           == APP_NOTIFY_ALERT);
    assert(app_notify_kind_effective(APP_NOTIFY_IDLE, APP_NOTIFY_RUNNING_FRESH_MS + 1)
           == APP_NOTIFY_IDLE);

    return 0;
}
