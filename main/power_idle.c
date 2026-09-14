#include "power_idle.h"
#include "power_idle_logic.h"

#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "power_idle";

static esp_timer_handle_t s_timer;
static volatile int64_t s_last_activity_us;
static volatile uint8_t s_applied = POWER_IDLE_ON_PERCENT;
static volatile uint8_t s_on_level = POWER_IDLE_ON_PERCENT;   // "恢复档":用户设定的日常亮度
static volatile uint16_t s_timeout_sec = POWER_IDLE_TIMEOUT_60S; // 默认 60 秒
static volatile bool s_enabled = true;

// 定时器回调运行在 esp_timer 任务里;只操作 LEDC 背光,不触碰 LVGL。
static void idle_tick(void *arg)
{
    (void)arg;
    if (!s_enabled) return;

    int64_t idle = esp_timer_get_time() - s_last_activity_us;
    uint8_t target = power_idle_target_custom(idle, s_timeout_sec);
    if (target == POWER_IDLE_ON_PERCENT) target = s_on_level;   // 亮档映射到用户设定值
    if (target == s_applied) return;

    bsp_display_backlight(target);
    s_applied = target;
    ESP_LOGD(TAG, "空闲 %llds -> 背光 %u%%", (long long)(idle / 1000000), target);
}

void power_idle_note_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
    if (s_enabled && s_applied != s_on_level) {
        bsp_display_backlight(s_on_level);
        s_applied = s_on_level;
    }
}

void power_idle_set_on_level(uint8_t percent)
{
    if (percent == 0 || percent > 100) percent = POWER_IDLE_ON_PERCENT;
    s_on_level = percent;
}

void power_idle_set_timeout_sec(uint16_t sec)
{
    s_timeout_sec = sec;
    power_idle_note_activity(); // 重置计时并立即亮起
}

uint16_t power_idle_get_timeout_sec(void)
{
    return s_timeout_sec;
}

void power_idle_set_enabled(bool enabled)
{
    s_enabled = enabled;
    if (enabled) {
        // 页面退出时统一恢复到设定亮度,避免停在熄屏状态。
        s_last_activity_us = esp_timer_get_time();
        bsp_display_backlight(s_on_level);
        s_applied = s_on_level;
    }
}

esp_err_t power_idle_init(void)
{
    if (s_timer) return ESP_OK;

    s_last_activity_us = esp_timer_get_time();
    s_applied = s_on_level;

    const esp_timer_create_args_t args = {
        .callback = idle_tick,
        .name = "power_idle",
    };
    esp_err_t err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create 失败: %s", esp_err_to_name(err));
        return err;
    }
    return esp_timer_start_periodic(s_timer, 1000 * 1000);
}
