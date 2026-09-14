idf_component_register(
    SRCS "main.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "ui_theme.c"
         "demo_display.c"
         "demo_button.c"
         "demo_audio.c"
         "demo_battery.c"
         "demo_radio.c"
         "demo_wifi.c"
         "demo_low_power.c"
         "demo_quota.c"
         "demo_brightness.c"
         "demo_sound.c"
         "demo_status.c"
         "app_voice.c"
         "pet_frames.c"
         "app_notify_logic.c"
         "app_notify.c"
         "app_net.c"
         "app_beep.c"
         "app_beep_logic.c"
         "app_cfg.c"
         "app_portal.c"
         "demo_portal.c"
         "brightness_logic.c"
         "power_idle_logic.c"
         "power_idle.c"
         "ui_font.c"
         "lv_font_hansans_14_cjk.c"
     INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash esp_http_client esp_http_server json lwip mbedtls esp_pm
)

# 让所有翻译单元都能看到该宏,从而启用 ui_font/ui_pixel 里的中文回退。
# LV_LVGL_H_INCLUDE_SIMPLE:lv_font_conv 生成的字体靠它选 `#include "lvgl.h"`,
# 否则会走 `#include "lvgl/lvgl.h"`(本工程无此路径)导致编译失败。
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    LV_FONT_HANSANS_14_CJK=1
    LV_LVGL_H_INCLUDE_SIMPLE=1)
