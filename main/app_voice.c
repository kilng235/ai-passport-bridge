// main/app_voice.c —— 8-bit 对讲机(PTT)音频采集与两段式识别。
//
// 交互(通知/桌宠页):
//   短按 OK 开始录音 → 再短按 OK 结束 → 上传识别(先不注入)→ VOICE_REVIEW
//   REVIEW 中:OK=发送注入 / 上=继续说(追加录音)/ 下=撤销 / 双击 OK=重录
//
// 上传采用**流式 chunked POST**:录音边读边写 HTTP,不缓存整段 PCM(ESP32-C3 无
// PSRAM,整段 8s/16k 就要 256KB,连续分配必然失败);流式只占约 1KB 栈缓冲。
// "继续说"由 PC 侧按 session 拼接音频,设备端同样不占内存。
// 目标地址取自 app_notify 记录的最后一次 /notify 源 IP(电脑端 Bridge)。
#include "app_voice.h"
#include "app_cfg.h"
#include "app_net.h"
#include "app_notify.h"
#include "bsp_audio.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "app_voice";

#define SAMPLE_RATE         16000
#define CHUNK_SAMPLES       512          // 每块 1KB;仅此占栈
#define MAX_RECORD_SEC      10
#define MIN_RECORD_MS       300
#define VOICE_PORT          8090
#define VOICE_PATH          "/api/voice-prompt"
#define VOICE_COMMIT_PATH   "/api/voice-commit"
#define VOICE_CANCEL_PATH   "/api/voice-cancel"
#define VOICE_FALLBACK_HOST "192.168.0.108"   // 未收到过通知时的兜底电脑地址

static volatile voice_state_t s_voice_state = VOICE_IDLE;
static size_t s_audio_len = 0;
static int64_t s_start_time_us = 0;
static volatile int s_current_level = 0;
static char s_result_text[256] = {0};

static uint32_t s_session = 0;      // 当前会话 id(device 生成,PC 据此拼接音频)
static bool s_append = false;       // 本次录音是否追加到同 session
static uint32_t s_session_seq = 0;
static char s_token[APP_CFG_TOKEN_MAX];   // 可选共享 Token(与 PC 端一致)

static TaskHandle_t s_worker = NULL;

// 计算音量电平 (0 ~ 100)
static int calculate_level(const int16_t *samples, int count)
{
    if (!samples || count <= 0) return 0;
    int32_t sum = 0;
    for (int i = 0; i < count; i++) {
        int val = samples[i];
        if (val < 0) val = -val;
        sum += val;
    }
    int avg = sum / count;
    int level = (avg * 100) / 4000;
    if (level > 100) level = 100;
    return level;
}

// chunked 编码单块:"<十六进制长度>\r\n<数据>\r\n"
static bool chunk_write(esp_http_client_handle_t client, const void *data, size_t len)
{
    char head[16];
    int n = snprintf(head, sizeof(head), "%x\r\n", (unsigned)len);
    if (esp_http_client_write(client, head, n) != n) return false;
    if (esp_http_client_write(client, (const char *)data, (int)len) != (int)len) return false;
    if (esp_http_client_write(client, "\r\n", 2) != 2) return false;
    return true;
}

static const char *bridge_host(char *buf, size_t len)
{
    if (app_notify_bridge_ip(buf, len)) return buf;
    snprintf(buf, len, "%s", VOICE_FALLBACK_HOST);
    return buf;
}

// 录音 + 上传识别工作线程(一次一任务,结束自删)。
static void voice_worker(void *arg)
{
    (void)arg;

    char host[16];
    char url[160];
    const char *target_ip = bridge_host(host, sizeof(host));
    snprintf(url, sizeof(url), "http://%s:%d%s?session=%u&append=%d",
             target_ip, VOICE_PORT, VOICE_PATH,
             (unsigned)s_session, s_append ? 1 : 0);
    ESP_LOGI(TAG, "语音上传目标: %s", url);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        s_voice_state = VOICE_FAILED;
        snprintf(s_result_text, sizeof(s_result_text), "客户端初始化失败");
        goto done;
    }

    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    if (s_token[0]) esp_http_client_set_header(client, "X-Passport-Token", s_token);
    // write_len = -1 → Transfer-Encoding: chunked,长度未知也能边录边传。
    if (esp_http_client_open(client, -1) != ESP_OK) {
        s_voice_state = VOICE_FAILED;
        ESP_LOGE(TAG, "连接语音服务失败: %s", url);
        snprintf(s_result_text, sizeof(s_result_text), "连不上 %s", target_ip);
        esp_http_client_cleanup(client);
        goto done;
    }

    bsp_audio_resume();
    bsp_audio_set_format(SAMPLE_RATE, 16, 1);

    int16_t chunk[CHUNK_SAMPLES];
    s_audio_len = 0;
    s_start_time_us = esp_timer_get_time();

    while (s_voice_state == VOICE_RECORDING) {
        if (s_audio_len >= (size_t)SAMPLE_RATE * 2 * MAX_RECORD_SEC) {
            ESP_LOGW(TAG, "达到最长录音 %d 秒,自动结束", MAX_RECORD_SEC);
            break;
        }
        if (bsp_audio_read(chunk, sizeof(chunk)) == ESP_OK) {
            if (!chunk_write(client, chunk, sizeof(chunk))) break;
            s_audio_len += sizeof(chunk);
            s_current_level = calculate_level(chunk, CHUNK_SAMPLES);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    bsp_audio_suspend();
    s_current_level = 0;

    uint32_t ms = (uint32_t)((esp_timer_get_time() - s_start_time_us) / 1000);
    if (s_audio_len == 0 || ms < MIN_RECORD_MS) {
        s_voice_state = VOICE_FAILED;
        snprintf(s_result_text, sizeof(s_result_text), "录音时间过短");
        esp_http_client_write(client, "0\r\n\r\n", 5);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        goto done;
    }

    // 结束 chunked 请求体并读取识别结果 {text}
    s_voice_state = VOICE_UPLOADING;
    esp_http_client_write(client, "0\r\n\r\n", 5);
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status == 200) {
        char resp[512] = {0};
        int r = esp_http_client_read(client, resp, sizeof(resp) - 1);
        if (r > 0) resp[r] = '\0';
        s_result_text[0] = '\0';
        cJSON *root = cJSON_Parse(resp);
        if (root) {
            cJSON *txt = cJSON_GetObjectItem(root, "text");
            if (txt && cJSON_IsString(txt)) {
                snprintf(s_result_text, sizeof(s_result_text), "%s", txt->valuestring);
            }
            cJSON_Delete(root);
        }
        // 识别成功 → 等待用户确认(不自动注入)
        s_voice_state = VOICE_REVIEW;
        ESP_LOGI(TAG, "识别完成,等待确认: \"%s\"", s_result_text);
    } else {
        s_voice_state = VOICE_FAILED;
        snprintf(s_result_text, sizeof(s_result_text), "识别失败 HTTP %d", status);
        ESP_LOGW(TAG, "识别失败: HTTP %d", status);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

done:
    s_worker = NULL;
    vTaskDelete(NULL);
}

// 向 PC 发一个 JSON POST(commit/cancel 共用)。
static bool post_json(const char *path, const char *json)
{
    char host[16];
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", bridge_host(host, sizeof(host)), VOICE_PORT, path);

    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 10000 };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_http_client_set_header(c, "Content-Type", "application/json");
    if (s_token[0]) esp_http_client_set_header(c, "X-Passport-Token", s_token);
    int len = (int)strlen(json);
    bool ok = false;
    if (esp_http_client_open(c, len) == ESP_OK) {
        esp_http_client_write(c, json, len);
        esp_http_client_fetch_headers(c);
        ok = esp_http_client_get_status_code(c) == 200;
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return ok;
}

static void commit_worker(void *arg)
{
    (void)arg;
    char body[48];
    snprintf(body, sizeof(body), "{\"session\":%u}", (unsigned)s_session);
    bool ok = post_json(VOICE_COMMIT_PATH, body);
    if (ok) {
        s_voice_state = VOICE_DONE;
        ESP_LOGI(TAG, "已发送: \"%s\"", s_result_text);
    } else {
        s_voice_state = VOICE_FAILED;
        snprintf(s_result_text, sizeof(s_result_text), "发送失败");
        ESP_LOGW(TAG, "发送失败");
    }
    s_worker = NULL;
    vTaskDelete(NULL);
}

static void cancel_worker(void *arg)
{
    (void)arg;
    char body[48];
    snprintf(body, sizeof(body), "{\"session\":%u}", (unsigned)s_session);
    post_json(VOICE_CANCEL_PATH, body);   // 尽力而为,失败也不影响本地状态
    s_worker = NULL;
    vTaskDelete(NULL);
}

esp_err_t app_voice_init(void)
{
    s_voice_state = VOICE_IDLE;
    s_current_level = 0;
    s_result_text[0] = '\0';
    app_cfg_load_voice_token(s_token, sizeof(s_token));   // 失败 → 空,不发 Token 头
    if (s_token[0]) ESP_LOGI(TAG, "已启用语音 Token 鉴权");
    return ESP_OK;
}

void app_voice_start_record(void)
{
    if (s_voice_state == VOICE_RECORDING || s_voice_state == VOICE_UPLOADING) return;
    s_session = ++s_session_seq;   // 新会话 → PC 侧丢弃上一段
    s_append = false;
    s_current_level = 0;
    s_result_text[0] = '\0';
    s_voice_state = VOICE_RECORDING;   // 先占位,防止连点重复起任务
    if (xTaskCreate(voice_worker, "voice", 5120, NULL, 5, &s_worker) != pdPASS) {
        s_voice_state = VOICE_FAILED;
        snprintf(s_result_text, sizeof(s_result_text), "任务创建失败");
    }
}

void app_voice_start_append(void)
{
    if (s_voice_state == VOICE_RECORDING || s_voice_state == VOICE_UPLOADING) return;
    s_append = true;               // 同 session,PC 侧拼接
    s_current_level = 0;
    s_voice_state = VOICE_RECORDING;
    if (xTaskCreate(voice_worker, "voice", 5120, NULL, 5, &s_worker) != pdPASS) {
        s_voice_state = VOICE_FAILED;
        snprintf(s_result_text, sizeof(s_result_text), "任务创建失败");
    }
}

void app_voice_stop_record(void)
{
    if (s_voice_state == VOICE_RECORDING) {
        s_voice_state = VOICE_UPLOADING;   // 通知 worker 退出录音循环
    }
}

void app_voice_commit(void)
{
    if (s_voice_state != VOICE_REVIEW) return;
    s_voice_state = VOICE_UPLOADING;   // 忙碌态
    if (xTaskCreate(commit_worker, "voice_c", 4096, NULL, 5, &s_worker) != pdPASS) {
        s_voice_state = VOICE_FAILED;
        snprintf(s_result_text, sizeof(s_result_text), "任务创建失败");
    }
}

void app_voice_cancel(void)
{
    if (s_voice_state != VOICE_REVIEW) return;
    s_result_text[0] = '\0';
    s_voice_state = VOICE_IDLE;
    xTaskCreate(cancel_worker, "voice_x", 3072, NULL, 4, NULL);   // 通知 PC 丢弃缓存
}

voice_state_t app_voice_get_state(void) { return s_voice_state; }
int app_voice_get_level(void) { return s_current_level; }
uint32_t app_voice_get_duration_ms(void)
{
    if (s_voice_state == VOICE_RECORDING) {
        return (uint32_t)((esp_timer_get_time() - s_start_time_us) / 1000);
    }
    return 0;
}
const char *app_voice_get_result_text(void) { return s_result_text; }
