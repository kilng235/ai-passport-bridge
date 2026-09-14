// main/demo.h —— 每个演示页实现的统一接口。
// 新增一个演示页 = 实现这三个函数 + 在 main.c 的 DEMOS[] 里加一行。
#pragma once

#include "bsp_button.h"

typedef struct {
    const char *name;
    void (*enter)(void);                          // 建自己的屏并载入
    void (*exit)(void);                           // 删屏、停定时器、释放资源
    void (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);  // 收按键(长按确定已被 main 拦截)
} demo_entry_t;

// 各演示页(定义在各自的 .c 里)
void demo_display_enter(void); void demo_display_exit(void);
void demo_display_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_button_enter(void);  void demo_button_exit(void);
void demo_button_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_audio_enter(void);   void demo_audio_exit(void);
void demo_audio_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_battery_enter(void); void demo_battery_exit(void);
void demo_battery_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_wifi_enter(void);    void demo_wifi_exit(void);
void demo_wifi_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_low_power_enter(void); void demo_low_power_exit(void);
void demo_low_power_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_brightness_enter(void); void demo_brightness_exit(void);
void demo_brightness_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_notify_enter(void);  void demo_notify_exit(void);
void demo_notify_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void demo_pet_enter(void);
void demo_voice_enter(void);

void demo_sound_enter(void);   void demo_sound_exit(void);
void demo_sound_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_quota_enter(void); void demo_quota_exit(void);
void demo_quota_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_portal_enter(void); void demo_portal_exit(void);
void demo_portal_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 演示页请求切换到配置门户页(由 main 在按键分发后处理),用于随时重开配置网页。
void app_nav_request_portal(void);

// 演示页请求返回主页菜单(由 main 在按键分发后处理),用于额度页 OK 短按退回。
void app_nav_request_menu(void);
