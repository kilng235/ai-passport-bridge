// main/app_beep.c —— 提示音实现:后台任务用整数正弦表生成音调写 I2S,放完挂起音频省电。
// 全部整数运算:C3 无 FPU,sinf 会拖入大量软件浮点代码(约 +60KB flash)。
// 静音时段:app_beep_play 内查窗口(app_net 的 SNTP 对时后本地时钟可信),只压声音不压亮屏。
#include "app_beep.h"
#include "app_beep_logic.h"
#include "app_voice.h"

#include "bsp_audio.h"
#include "demo_radio.h"   // demo_radio_nvs_prepare()

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include <string.h>
#include <time.h>

static const char *TAG = "app_beep";

#define BEEP_SR            16000
#define BEEP_CHUNK         256
#define BEEP_FADE_SAMPLES  (BEEP_SR / 200)   // 5ms 淡入淡出,避免爆音
#define SINE_STEPS         256

// 与亮度存同一 NVS namespace,键独立,免去迁移整份配置 blob。
#define BEEP_NVS_NS   "pcfg"
#define BEEP_NVS_ON    "beep_on"
#define BEEP_NVS_VOL   "beep_vol"
#define BEEP_NVS_QUIET "beep_quiet"   // u8 预设索引

// 7000 * sin(2*pi*i/256),i = 0..255(满量程 7000,配合 1/1024 包络)。
static const int16_t SINE[SINE_STEPS] = {
         0,    172,    343,    515,    686,    857,   1027,   1197,
      1366,   1534,   1701,   1867,   2032,   2196,   2358,   2519,
      2679,   2837,   2993,   3147,   3300,   3450,   3599,   3745,
      3889,   4031,   4170,   4307,   4441,   4572,   4701,   4827,
      4950,   5070,   5187,   5300,   5411,   5518,   5622,   5723,
      5820,   5914,   6004,   6091,   6173,   6253,   6328,   6399,
      6467,   6531,   6591,   6647,   6699,   6746,   6790,   6830,
      6865,   6897,   6924,   6947,   6966,   6981,   6992,   6998,
      7000,   6998,   6992,   6981,   6966,   6947,   6924,   6897,
      6865,   6830,   6790,   6746,   6699,   6647,   6591,   6531,
      6467,   6399,   6328,   6253,   6173,   6091,   6004,   5914,
      5820,   5723,   5622,   5518,   5411,   5300,   5187,   5070,
      4950,   4827,   4701,   4572,   4441,   4307,   4170,   4031,
      3889,   3745,   3599,   3450,   3300,   3147,   2993,   2837,
      2679,   2519,   2358,   2196,   2032,   1867,   1701,   1534,
      1366,   1197,   1027,    857,    686,    515,    343,    172,
         0,   -172,   -343,   -515,   -686,   -857,  -1027,  -1197,
     -1366,  -1534,  -1701,  -1867,  -2032,  -2196,  -2358,  -2519,
     -2679,  -2837,  -2993,  -3147,  -3300,  -3450,  -3599,  -3745,
     -3889,  -4031,  -4170,  -4307,  -4441,  -4572,  -4701,  -4827,
     -4950,  -5070,  -5187,  -5300,  -5411,  -5518,  -5622,  -5723,
     -5820,  -5914,  -6004,  -6091,  -6173,  -6253,  -6328,  -6399,
     -6467,  -6531,  -6591,  -6647,  -6699,  -6746,  -6790,  -6830,
     -6865,  -6897,  -6924,  -6947,  -6966,  -6981,  -6992,  -6998,
     -7000,  -6998,  -6992,  -6981,  -6966,  -6947,  -6924,  -6897,
     -6865,  -6830,  -6790,  -6746,  -6699,  -6647,  -6591,  -6531,
     -6467,  -6399,  -6328,  -6253,  -6173,  -6091,  -6004,  -5914,
     -5820,  -5723,  -5622,  -5518,  -5411,  -5300,  -5187,  -5070,
     -4950,  -4827,  -4701,  -4572,  -4441,  -4307,  -4170,  -4031,
     -3889,  -3745,  -3599,  -3450,  -3300,  -3147,  -2993,  -2837,
     -2679,  -2519,  -2358,  -2196,  -2032,  -1867,  -1701,  -1534,
     -1366,  -1197,  -1027,   -857,   -686,   -515,   -343,   -172,
};

static volatile bool s_enabled = true;
static uint8_t s_volume = 70;
static uint8_t s_quiet_preset;   // 0 关 / 1 = 22-08 / 2 = 23-07
static app_beep_quiet_t s_quiet; // 当前生效窗口(由预设映射)
static bool s_audio_ready;
static TaskHandle_t s_task;

// 静音时段预设(UI 循环切换;新增预设 = 加一行)
static const app_beep_quiet_t kQuietPresets[] = {
    {false, 22, 8},   // 0: 关
    {true,  22, 8},   // 1: 22:00-08:00
    {true,  23, 7},   // 2: 23:00-07:00
};
static const char *const kQuietLabels[] = {
    "关", "22:00-08:00", "23:00-07:00",
};

#define QUIET_PRESET_COUNT (int)(sizeof(kQuietPresets) / sizeof(kQuietPresets[0]))

static void apply_quiet_preset(int index)
{
    if (index < 0 || index >= QUIET_PRESET_COUNT) index = 0;
    s_quiet_preset = (uint8_t)index;
    s_quiet = kQuietPresets[index];
}

static int current_hour(void)
{
    time_t now = time(NULL);
    if (now < 1700000000) return -1;   // SNTP 未对上:时钟不可信
    struct tm tmv;
    localtime_r(&now, &tmv);
    return tmv.tm_hour;
}
static bool s_audio_ready;
static TaskHandle_t s_task;

static void tone(int freq, int ms)
{
    if (freq <= 0 || ms <= 0) return;
    const int total = BEEP_SR * ms / 1000;
    const uint32_t step = (uint32_t)((int64_t)freq * SINE_STEPS / BEEP_SR);
    int16_t chunk[BEEP_CHUNK];
    uint32_t phase = 0;

    for (int done = 0; done < total; ) {
        int n = total - done;
        if (n > BEEP_CHUNK) n = BEEP_CHUNK;
        for (int i = 0; i < n; i++) {
            int idx = done + i;
            int env = 1024;   // 1.0(1/1024 定点)
            if (idx < BEEP_FADE_SAMPLES) {
                env = idx * 1024 / BEEP_FADE_SAMPLES;
            } else if (idx >= total - BEEP_FADE_SAMPLES) {
                env = (total - idx) * 1024 / BEEP_FADE_SAMPLES;
            }
            if (env < 0) env = 0;
            int16_t s = SINE[phase & (SINE_STEPS - 1)];
            chunk[i] = (int16_t)((int32_t)s * env / 1024);
            phase += step;
        }
        bsp_audio_write(chunk, (size_t)n * sizeof(int16_t));
        done += n;
    }
}

static void silence(int ms)
{
    int16_t zero[BEEP_CHUNK];
    memset(zero, 0, sizeof(zero));
    int total = BEEP_SR * ms / 1000;
    while (total > 0) {
        int n = total > BEEP_CHUNK ? BEEP_CHUNK : total;
        bsp_audio_write(zero, (size_t)n * sizeof(int16_t));
        total -= n;
    }
}

static bool audio_begin(void)
{
    if (!s_audio_ready) {
        if (bsp_audio_init() != ESP_OK) {
            ESP_LOGW(TAG, "音频初始化失败,跳过提示音");
            return false;
        }
        s_audio_ready = true;
    }
    bsp_audio_resume();   // 上次放完挂起过,先使能 I2S
    if (bsp_audio_set_format(BEEP_SR, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "设置音频格式失败");
        return false;
    }
    bsp_audio_set_volume(s_volume);
    return true;
}

static void beep_task(void *arg)
{
    (void)arg;
    uint32_t kind;
    for (;;) {
        if (xTaskNotifyWait(0, 0, &kind, portMAX_DELAY) != pdTRUE) continue;
        if (!s_enabled) continue;
        // 录音/上传期间完全避让:录音独占 codec,beep 若此刻重配采样率
        // 会拆掉录音上下文(16kHz↔16kHz 同格式才复用,否则 close 重开),
        // 造成 RX 通道停摆 → 录音读数恒为 0。
        voice_state_t vs = app_voice_get_state();
        if (vs == VOICE_RECORDING || vs == VOICE_UPLOADING) continue;
        if (!audio_begin()) continue;

        switch ((app_notify_kind_t)kind) {
        case APP_NOTIFY_RUNNING:                  // 任务进行中: 8-bit 下行电子音
            tone(1175, 45); // D6
            tone(880,  55); // A5
            tone(587,  70); // D5
            break;
        case APP_NOTIFY_DONE:                     // 任务完成: 8-bit 经典马里奥吃金币音效 (B5 -> E6)
            tone(988,  60); // B5
            tone(1319, 140); // E6
            break;
        case APP_NOTIFY_ALERT:                    // 需要确认: 8-bit 经典三连警报音
            tone(1760, 40); // A6
            silence(30);
            tone(1760, 40);
            silence(30);
            tone(1760, 60);
            break;
        default:                                  // idle: 8-bit 清脆 Blip
            tone(1046, 50); // C6
            break;
        }
        bsp_audio_suspend();                      // 放完挂起,省 codec/I2S 电流
    }
}

static void load_settings(void)
{
    if (demo_radio_nvs_prepare() != ESP_OK) return;
    nvs_handle_t h;
    if (nvs_open(BEEP_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t on = 1;
    uint8_t vol = s_volume;
    uint8_t quiet = 0;
    if (nvs_get_u8(h, BEEP_NVS_ON, &on) == ESP_OK) s_enabled = (on != 0);
    if (nvs_get_u8(h, BEEP_NVS_VOL, &vol) == ESP_OK && vol <= 100) s_volume = vol;
    if (nvs_get_u8(h, BEEP_NVS_QUIET, &quiet) == ESP_OK) apply_quiet_preset((int)quiet);
    nvs_close(h);
}

bool app_beep_save(void)
{
    if (demo_radio_nvs_prepare() != ESP_OK) return false;
    nvs_handle_t h;
    if (nvs_open(BEEP_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_u8(h, BEEP_NVS_ON, s_enabled ? 1 : 0);
    if (e == ESP_OK) e = nvs_set_u8(h, BEEP_NVS_VOL, s_volume);
    if (e == ESP_OK) e = nvs_set_u8(h, BEEP_NVS_QUIET, s_quiet_preset);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

esp_err_t app_beep_init(void)
{
    if (s_task) return ESP_OK;
    load_settings();
    if (xTaskCreate(beep_task, "app_beep", 4096, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "创建提示音任务失败");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_beep_play(app_notify_kind_t kind)
{
    if (!s_task || !s_enabled) return;
    if (app_beep_in_quiet()) return;   // 静音时段:只压声音,亮屏/动画不受影响
    app_beep_preview(kind);
}

void app_beep_preview(app_notify_kind_t kind)
{
    if (!s_task) return;
    xTaskNotify(s_task, (uint32_t)kind, eSetValueWithOverwrite);
}

bool app_beep_in_quiet(void)
{
    int hour = current_hour();
    return hour >= 0 && app_beep_quiet_active(&s_quiet, hour);
}

int app_beep_quiet_preset(void) { return s_quiet_preset; }

void app_beep_set_quiet_preset(int index)
{
    apply_quiet_preset(index);
}

const char *app_beep_quiet_preset_label(int index)
{
    if (index < 0 || index >= QUIET_PRESET_COUNT) index = 0;
    return kQuietLabels[index];
}

void app_beep_set_enabled(bool enabled) { s_enabled = enabled; }
bool app_beep_enabled(void)             { return s_enabled; }
void app_beep_set_volume(uint8_t percent)
{
    s_volume = percent > 100 ? 100 : percent;
}
uint8_t app_beep_volume(void) { return s_volume; }
