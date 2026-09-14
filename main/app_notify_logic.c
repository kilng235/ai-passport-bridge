// main/app_notify_logic.c —— 见 app_notify_logic.h。纯 C,不用 cJSON,便于主机测试。
#include "app_notify_logic.h"

#include <string.h>

app_notify_kind_t app_notify_kind_from_str(const char *s)
{
    if (!s) return APP_NOTIFY_IDLE;
    if (strcmp(s, "running") == 0) return APP_NOTIFY_RUNNING;
    if (strcmp(s, "done")    == 0) return APP_NOTIFY_DONE;
    if (strcmp(s, "alert")   == 0) return APP_NOTIFY_ALERT;
    if (strcmp(s, "error")   == 0) return APP_NOTIFY_ALERT;   // 旧 error 并入"需要确认"
    return APP_NOTIFY_IDLE;                                    // 未知/缺省 -> 空闲
}

const char *app_notify_kind_str(app_notify_kind_t k)
{
    switch (k) {
    case APP_NOTIFY_RUNNING: return "running";
    case APP_NOTIFY_DONE:    return "done";
    case APP_NOTIFY_ALERT:   return "alert";
    default:                 return "idle";
    }
}

int app_notify_kind_rank(app_notify_kind_t k)
{
    switch (k) {
    case APP_NOTIFY_ALERT:   return 3;
    case APP_NOTIFY_RUNNING: return 2;
    case APP_NOTIFY_DONE:    return 1;
    default:                 return 0;
    }
}

// 在顶层对象(深度 1:进入最外层 '{' 之后)找 "key" : value,返回 value 首个非空白字符。
// 逐字符扫描并跳过字符串内容,且只在深度 1 匹配,避免命中 sessions 数组里的同名键。
static const char *find_value(const char *json, const char *key)
{
    const size_t klen = strlen(key);
    bool in_str = false;
    int depth = 0;
    const char *p = json;

    while (*p) {
        if (in_str) {
            if (*p == '\\' && p[1]) { p += 2; continue; }
            if (*p == '"') in_str = false;
            p++;
            continue;
        }
        if (*p == '"') {
            if (depth == 1) {
                const char *ks = p + 1;
                if (strncmp(ks, key, klen) == 0 && ks[klen] == '"') {
                    const char *q = ks + klen + 1;
                    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
                    if (*q == ':') {
                        q++;
                        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
                        return q;
                    }
                }
            }
            in_str = true;
        } else if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            if (depth > 0) depth--;
        }
        p++;
    }
    return NULL;
}

// 在 [p,end) 范围内找 "key" : value(用于解析 sessions 里的对象成员)。
static const char *find_value_range(const char *p, const char *end, const char *key)
{
    const size_t klen = strlen(key);
    bool in_str = false;

    while (p < end && *p) {
        if (in_str) {
            if (*p == '\\' && p + 1 < end) { p += 2; continue; }
            if (*p == '"') in_str = false;
            p++;
            continue;
        }
        if (*p == '"') {
            const char *ks = p + 1;
            if (ks + klen + 1 <= end && strncmp(ks, key, klen) == 0 && ks[klen] == '"') {
                const char *q = ks + klen + 1;
                while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
                if (q < end && *q == ':') {
                    q++;
                    while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
                    return q;
                }
            }
            in_str = true;
        }
        p++;
    }
    return NULL;
}

// 解析 JSON 字符串字面量(含转义)到 out;非引号开头则原样拷贝(裸文本兜底)。
static void copy_value(const char *p, char *out, size_t cap)
{
    size_t o = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && o + 1 < cap) {
            if (*p == '\\' && p[1]) {
                char c = p[1];
                p += 2;
                switch (c) {
                case 'n':  out[o++] = '\n'; break;
                case 't':  out[o++] = '\t'; break;
                case 'r':  out[o++] = '\r'; break;
                case '"':  out[o++] = '"';  break;
                case '\\': out[o++] = '\\'; break;
                case '/':  out[o++] = '/';  break;
                default:   out[o++] = c;    break;   // \uXXXX 不解码(插件发 UTF-8)
                }
            } else {
                out[o++] = *p++;
            }
        }
    } else {
        while (*p && *p != ',' && *p != '}' && o + 1 < cap) out[o++] = *p++;
        while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\n' || out[o - 1] == '\r')) o--;
    }
    out[o] = '\0';
}

static int64_t parse_int(const char *p)
{
    int64_t v = 0;
    bool neg = false;
    while (*p == ' ') p++;
    if (*p == '-') { neg = true; p++; }
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return neg ? -v : v;
}

// 从 '{' 起找配对的 '}',跳过字符串内容(名字里含 '}' 也不会截断)。
static const char *find_obj_end(const char *p)
{
    bool in_str = false;
    for (; *p; p++) {
        if (in_str) {
            if (*p == '\\' && p[1]) { p++; continue; }
            if (*p == '"') in_str = false;
        } else if (*p == '"') {
            in_str = true;
        } else if (*p == '}') {
            return p;
        }
    }
    return NULL;
}

// 解析 "sessions":[{...},...]。arr 指向 '['。
static void parse_sessions(const char *arr, app_notify_msg_t *out)
{
    const char *p = arr;
    if (*p != '[') return;
    p++;

    while (*p && out->sess_count < APP_NOTIFY_SESS_MAX) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p != '{') break;

        const char *end = find_obj_end(p);
        if (!end) break;

        app_notify_session_t *s = &out->sessions[out->sess_count];
        const char *nm = find_value_range(p, end, "name");
        if (nm) copy_value(nm, s->name, sizeof(s->name));

        const char *kd = find_value_range(p, end, "kind");
        if (kd) {
            char tmp[16];
            copy_value(kd, tmp, sizeof(tmp));
            s->kind = app_notify_kind_from_str(tmp);
        } else {
            s->kind = APP_NOTIFY_IDLE;
        }

        if (s->name[0] != '\0') out->sess_count++;   // 无名条目不占位
        p = end + 1;
    }
}

bool app_notify_parse(const char *json, app_notify_msg_t *out)
{
    if (!json || !out) return false;
    memset(out, 0, sizeof(*out));

    const char *v;
    if ((v = find_value(json, "kind")) != NULL) {
        char tmp[16];
        copy_value(v, tmp, sizeof(tmp));
        out->kind = app_notify_kind_from_str(tmp);
    }
    if ((v = find_value(json, "title")) != NULL) copy_value(v, out->title, sizeof(out->title));
    if ((v = find_value(json, "text"))  != NULL) copy_value(v, out->text,  sizeof(out->text));
    if ((v = find_value(json, "time"))  != NULL) out->time_ms = parse_int(v);
    if ((v = find_value(json, "count")) != NULL) out->count = (int)parse_int(v);
    if ((v = find_value(json, "sessions")) != NULL) parse_sessions(v, out);

    if (out->sess_count == 0 && out->title[0] == '\0' && out->text[0] == '\0') {
        // 没有 title/text/sessions:若为裸文本则整段当正文;否则视为无效。
        if (json[0] != '{' && json[0] != '\0') {
            copy_value(json, out->text, sizeof(out->text));
        } else {
            return false;
        }
    }
    out->valid = true;
    return true;
}

app_notify_kind_t app_notify_kind_effective(app_notify_kind_t kind, int64_t age_ms)
{
    if (kind == APP_NOTIFY_RUNNING && age_ms > APP_NOTIFY_RUNNING_FRESH_MS) {
        return APP_NOTIFY_IDLE;
    }
    return kind;
}
