// main/demo_status.c —— 8-bit 经典像素街机风格:通知 ↔ 桌宠 ↔ 语音对讲（中文 + 像素修饰符）。
// 像素 HUD 对话框 + 方块指示器 + 像素跑马灯。
#include "demo.h"
#include "app_net.h"
#include "app_notify.h"
#include "app_beep.h"
#include "app_voice.h"
#include "power_idle.h"
#include "ui_theme.h"
#include "ui_pixel.h"
#include "ui_font.h"
#include "pet_frames.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define PANEL_W 240
#define PANEL_H 320
#define PAGE_COUNT 3
#define LIST_ROWS 4
#define KIND_HOLD_US (5LL * 1000 * 1000)

static const char *TAG = "demo_status";

static lv_obj_t *s_scr;
static lv_obj_t *s_strip;
static int s_page;                 // 0 = 通知, 1 = 桌宠, 2 = 语音对讲
static lv_timer_t *s_timer;

// ---- 通知页控件 ----
static lv_obj_t *s_status;
static lv_obj_t *s_title;
static lv_obj_t *s_text;
static lv_obj_t *s_agg;
static lv_obj_t *s_row[LIST_ROWS];
static lv_obj_t *s_tag[LIST_ROWS];
static lv_obj_t *s_name[LIST_ROWS];
static lv_obj_t *s_clock;
static int64_t s_kind_until_us;
static app_notify_kind_t s_base_kind = APP_NOTIFY_IDLE;
static uint32_t s_last_version;
static app_notify_kind_t s_eff_kind;
static bool s_rendered_stale;

// ---- 桌宠页控件 ----
static lv_obj_t *s_pet_anim;
static lv_obj_t *s_pet_state;
static lv_obj_t *s_pet_clock;
static app_notify_kind_t s_pet_kind = APP_NOTIFY_IDLE;
static bool s_pet_have_state;

// ---- 语音对讲页 (PTT) 控件 ----
static lv_obj_t *s_voice_clock;
static lv_obj_t *s_voice_tag;
static lv_obj_t *s_voice_state_lbl;
static lv_obj_t *s_voice_level_box;
static lv_obj_t *s_voice_level_bar;
static lv_obj_t *s_voice_dur_lbl;
static lv_obj_t *s_voice_text_box;
static lv_obj_t *s_voice_text_lbl;
static lv_obj_t *s_voice_hint_lbl;

// ---------------------------------------------------------------- 8-bit 配色与像素标签
static uint32_t kind_color(app_notify_kind_t k)
{
    switch (k) {
    case APP_NOTIFY_RUNNING: return UI_THEME_CYAN;
    case APP_NOTIFY_DONE:    return UI_THEME_GREEN;
    case APP_NOTIFY_ALERT:   return UI_THEME_RED;
    default:                 return UI_THEME_MUTED;
    }
}

static const char *kind_tag(app_notify_kind_t k)
{
    switch (k) {
    case APP_NOTIFY_RUNNING: return "▶ 执行";
    case APP_NOTIFY_DONE:    return "★ 完成";
    case APP_NOTIFY_ALERT:   return "! 告警";
    default:                 return "· 空闲";
    }
}

static const char *kind_text(app_notify_kind_t k)
{
    switch (k) {
    case APP_NOTIFY_RUNNING: return "▶ 任务进行中…";
    case APP_NOTIFY_DONE:    return "★ 任务执行完毕 ★";
    case APP_NOTIFY_ALERT:   return "!! 异常告警 !!";
    default:                 return "[ 暂无活跃任务 ]";
    }
}

static void set_hidden(lv_obj_t *o, bool hidden)
{
    if (!o) return;
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *panel_create(int x)
{
    lv_obj_t *p = lv_obj_create(s_strip);
    lv_obj_remove_style_all(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(p, x, 0);
    lv_obj_set_size(p, PANEL_W, PANEL_H);
    lv_obj_set_style_bg_color(p, lv_color_hex(UI_THEME_BG), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    return p;
}

// ---------------------------------------------------------------- 通知页
static void pet_set_state(app_notify_kind_t k);

static void notify_apply_kind(app_notify_kind_t k)
{
    if (!s_title) return;
    const char *txt = kind_text(k);
    lv_label_set_text(s_title, txt);
    lv_obj_set_style_text_font(s_title, ui_font_pick_14(txt), 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(kind_color(k)), 0);
}

static void notify_render_list(const app_notify_msg_t *m)
{
    set_hidden(s_title, true);
    set_hidden(s_text, true);
    set_hidden(s_agg, false);
    s_kind_until_us = 0;

    int total = m->count > 0 ? m->count : m->sess_count;
    char head[48];
    snprintf(head, sizeof(head), ":: %s (%d) ::", kind_tag(m->kind), total);
    lv_label_set_text(s_agg, head);
    lv_obj_set_style_text_font(s_agg, ui_font_pick_14(head), 0);
    lv_obj_set_style_text_color(s_agg, lv_color_hex(kind_color(m->kind)), 0);

    for (int i = 0; i < LIST_ROWS; i++) {
        if (i < m->sess_count) {
            const app_notify_session_t *s = &m->sessions[i];
            set_hidden(s_row[i], false);
            const char *tag = kind_tag(s->kind);
            lv_label_set_text(s_tag[i], tag);
            lv_obj_set_style_text_font(s_tag[i], ui_font_pick_14(tag), 0);
            lv_obj_set_style_text_color(s_tag[i], lv_color_hex(kind_color(s->kind)), 0);
            lv_label_set_text(s_name[i], s->name);
            lv_obj_set_style_text_font(s_name[i], ui_font_pick_14(s->name), 0);
            lv_obj_set_style_text_color(s_name[i],
                lv_color_hex(s->kind == APP_NOTIFY_IDLE ? UI_THEME_MUTED : UI_THEME_TEXT), 0);
        } else {
            set_hidden(s_row[i], true);
        }
    }
}

static void notify_render_single(const app_notify_msg_t *m)
{
    set_hidden(s_agg, true);
    for (int i = 0; i < LIST_ROWS; i++) set_hidden(s_row[i], true);
    set_hidden(s_title, false);
    set_hidden(s_text, false);

    if (m->kind == APP_NOTIFY_RUNNING) {
        s_base_kind = APP_NOTIFY_RUNNING;
    } else if (m->kind == APP_NOTIFY_DONE || m->kind == APP_NOTIFY_IDLE) {
        s_base_kind = APP_NOTIFY_IDLE;
    }
    notify_apply_kind(m->kind);
    s_kind_until_us = (m->kind == APP_NOTIFY_DONE || m->kind == APP_NOTIFY_ALERT)
                          ? esp_timer_get_time() + KIND_HOLD_US : 0;
    lv_label_set_text(s_text, m->text);
    lv_obj_set_style_text_font(s_text, ui_font_pick_14(m->text), 0);
}

static void notify_panel_create(lv_obj_t *p)
{
    ui_theme_label(p, ":: 任务通知 ::", 12, 10, 110, UI_THEME_AMBER, ui_font_pick_14(":: 任务通知 ::"));
    s_clock = ui_theme_label(p, "--:--", 124, 10, 104, UI_THEME_TEXT, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_RIGHT, 0);
    ui_theme_divider(p, 12, 34, 216);

    s_status = ui_theme_label(p, "<< 服务在线: 等待中 >>", 12, 44, 216,
                              UI_THEME_CYAN, ui_font_pick_14("<< 服务在线: 等待中 >>"));
    ui_theme_center(s_status);

    s_agg = ui_theme_label(p, "", 12, 68, 216, UI_THEME_MUTED, &lv_font_montserrat_14);
    ui_theme_center(s_agg);

    for (int i = 0; i < LIST_ROWS; i++) {
        // 8-bit 双层像素框
        lv_obj_t *box = ui_theme_box(p, 12, 92 + i * 46, 216, 40, UI_THEME_PANEL, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(UI_THEME_GRID), 0);

        // 像素角标
        ui_theme_box(box, 0, 0, 3, 3, UI_THEME_GRID_HI, 0);
        ui_theme_box(box, 213, 0, 3, 3, UI_THEME_GRID_HI, 0);
        ui_theme_box(box, 0, 37, 3, 3, UI_THEME_GRID_HI, 0);
        ui_theme_box(box, 213, 37, 3, 3, UI_THEME_GRID_HI, 0);

        s_row[i] = box;
        s_tag[i] = ui_theme_label(box, "", 8, 10, 56, UI_THEME_MUTED, &lv_font_montserrat_14);
        s_name[i] = ui_theme_label(box, "", 66, 10, 142, UI_THEME_TEXT, &lv_font_montserrat_14);
        lv_label_set_long_mode(s_name[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_height(s_name[i], 20);
        set_hidden(box, true);
    }

    s_title = ui_theme_label(p, "[ 暂无活跃任务 ]", 12, 96, 216, UI_THEME_MUTED, ui_font_pick_14("[ 暂无活跃任务 ]"));
    ui_theme_center(s_title);
    s_text = ui_theme_label(p, "", 12, 138, 216, UI_THEME_TEXT, &lv_font_montserrat_14);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(s_text, 116);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);

    ui_theme_divider(p, 12, 292, 216);
    lv_obj_t *hint = ui_theme_label(p, "[上下]切页  [长按]菜单", 12, 298, 216,
                                    UI_THEME_MUTED, ui_font_pick_14("[上下]切页  [长按]菜单"));
    ui_theme_center(hint);
}

static void render_stale_idle(void)
{
    app_notify_msg_t idle = {0};
    idle.kind = APP_NOTIFY_IDLE;
    idle.valid = true;
    notify_render_single(&idle);
}

static void notify_panel_tick(void)
{
    if (app_notify_is_online()) {
        lv_label_set_text(s_status, "<< 服务在线: 等待中 >>");
        lv_obj_set_style_text_font(s_status, ui_font_pick_14("<< 服务在线: 等待中 >>"), 0);
    } else if (app_notify_is_enabled()) {
        lv_label_set_text(s_status, "<< 正在连接中… >>");
        lv_obj_set_style_text_font(s_status, ui_font_pick_14("<< 正在连接中… >>"), 0);
    } else {
        lv_label_set_text(s_status, "<< 离线 >>");
        lv_obj_set_style_text_font(s_status, ui_font_pick_14("<< 离线 >>"), 0);
    }

    if (s_kind_until_us && (int64_t)esp_timer_get_time() > s_kind_until_us) {
        s_kind_until_us = 0;
        notify_apply_kind(s_base_kind);
    }

    app_notify_msg_t m;
    bool have = app_notify_get(&m);
    app_notify_kind_t eff = have
        ? app_notify_kind_effective(m.kind, app_notify_age_ms())
        : APP_NOTIFY_IDLE;
    bool stale = have && eff != m.kind;

    uint32_t ver = app_notify_version();
    if (have && ver != s_last_version && !stale) {
        if (m.sess_count > 0) notify_render_list(&m);
        else                   notify_render_single(&m);
        s_last_version = ver;
        s_rendered_stale = false;
    } else if (stale && !s_rendered_stale) {
        render_stale_idle();
        s_last_version = ver;
        s_rendered_stale = true;
    }

    if (s_eff_kind != eff) {
        s_eff_kind = eff;
        if (have && m.sess_count > 0 && s_agg) {
            char head[48];
            int total = m.count > 0 ? m.count : m.sess_count;
            snprintf(head, sizeof(head), ":: %s (%d) ::", kind_tag(eff), total);
            lv_label_set_text(s_agg, head);
            lv_obj_set_style_text_font(s_agg, ui_font_pick_14(head), 0);
            lv_obj_set_style_text_color(s_agg, lv_color_hex(kind_color(eff)), 0);
        }
    }

    if (s_pet_kind != eff) pet_set_state(eff);

    if (app_notify_has_new()) {
        app_notify_mark_read();
        power_idle_note_activity();
        app_beep_play(eff);
    }
}

// ---------------------------------------------------------------- 桌宠页
static void pet_set_state(app_notify_kind_t k)
{
    if (!s_pet_anim) return;

    const void **frames;
    int count, dur;
    switch (k) {
    case APP_NOTIFY_RUNNING: frames = pet_running_frames; count = pet_running_count; dur = 360; break;
    case APP_NOTIFY_DONE:    frames = pet_done_frames;    count = pet_done_count;    dur = 480; break;
    case APP_NOTIFY_ALERT:   frames = pet_alert_frames;   count = pet_alert_count;   dur = 480; break;
    default:                 frames = pet_idle_frames;    count = pet_idle_count;    dur = 640; break;
    }

    lv_animimg_set_src(s_pet_anim, frames, count);
    lv_animimg_set_duration(s_pet_anim, dur);
    lv_animimg_set_repeat_count(s_pet_anim, LV_ANIM_REPEAT_INFINITE);
    lv_animimg_start(s_pet_anim);

    const char *txt = kind_text(k);
    lv_label_set_text(s_pet_state, txt);
    lv_obj_set_style_text_font(s_pet_state, ui_font_pick_14(txt), 0);
    lv_obj_set_style_text_color(s_pet_state, lv_color_hex(kind_color(k)), 0);

    s_pet_kind = k;
    s_pet_have_state = true;
}

static void pet_panel_create(lv_obj_t *p)
{
    ui_theme_label(p, ":: 桌面萌宠 ::", 12, 10, 110, UI_THEME_AMBER, ui_font_pick_14(":: 桌面萌宠 ::"));
    s_pet_clock = ui_theme_label(p, "--:--", 124, 10, 104, UI_THEME_TEXT, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(s_pet_clock, LV_TEXT_ALIGN_RIGHT, 0);
    ui_theme_divider(p, 12, 34, 216);

    // 8-bit 像素舞台框
    lv_obj_t *stage = ui_theme_box(p, 20, 56, 200, 160, UI_THEME_PANEL, 0);
    lv_obj_set_style_border_width(stage, 2, 0);
    lv_obj_set_style_border_color(stage, lv_color_hex(UI_THEME_GRID), 0);
    ui_theme_box(stage, 0, 0, 4, 4, UI_THEME_GRID_HI, 0);
    ui_theme_box(stage, 196, 0, 4, 4, UI_THEME_GRID_HI, 0);
    ui_theme_box(stage, 0, 156, 4, 4, UI_THEME_GRID_HI, 0);
    ui_theme_box(stage, 196, 156, 4, 4, UI_THEME_GRID_HI, 0);

    s_pet_anim = lv_animimg_create(stage);
    lv_obj_set_pos(s_pet_anim, (200 - 96) / 2, (160 - 96) / 2);

    s_pet_state = ui_theme_label(p, "[ 暂无活跃任务 ]", 12, 230, 216, UI_THEME_MUTED, ui_font_pick_14("[ 暂无活跃任务 ]"));
    ui_theme_center(s_pet_state);

    ui_theme_divider(p, 12, 292, 216);
    lv_obj_t *hint = ui_theme_label(p, "[上下]切页  [长按]菜单", 12, 298, 216,
                                    UI_THEME_MUTED, ui_font_pick_14("[上下]切页  [长按]菜单"));
    ui_theme_center(hint);

    pet_set_state(APP_NOTIFY_IDLE);
}

static void pet_panel_tick(void)
{
    app_notify_msg_t m;
    if (app_notify_get(&m) && m.kind != s_pet_kind) {
        pet_set_state(m.kind);
    }
}

// ---------------------------------------------------------------- 语音对讲页
static void voice_panel_create(lv_obj_t *p)
{
    s_voice_tag = ui_theme_label(p, ":: 语音对讲机 ::", 12, 10, 120, UI_THEME_CYAN, ui_font_pick_14(":: 语音对讲机 ::"));
    s_voice_clock = ui_theme_label(p, "--:--", 134, 10, 94, UI_THEME_TEXT, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(s_voice_clock, LV_TEXT_ALIGN_RIGHT, 0);
    ui_theme_divider(p, 12, 34, 216);

    // 状态条 (空闲/录音中/识别中/待确认/已发送)
    s_voice_state_lbl = ui_theme_label(p, "[ 短按 OK 开始录音 ]", 12, 46, 216, UI_THEME_MUTED, ui_font_pick_14("[ 短按 OK 开始录音 ]"));
    ui_theme_center(s_voice_state_lbl);

    // 像素声波电平条外框
    s_voice_level_box = ui_theme_box(p, 16, 76, 208, 40, UI_THEME_PANEL, 0);
    lv_obj_set_style_border_width(s_voice_level_box, 2, 0);
    lv_obj_set_style_border_color(s_voice_level_box, lv_color_hex(UI_THEME_GRID), 0);

    s_voice_level_bar = lv_label_create(s_voice_level_box);
    lv_obj_set_pos(s_voice_level_bar, 0, 10);
    lv_obj_set_width(s_voice_level_bar, 204);
    lv_label_set_long_mode(s_voice_level_bar, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_voice_level_bar, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_voice_level_bar, "░░░░░░░░░░");
    lv_obj_set_style_text_font(s_voice_level_bar, ui_font_pick_14("░"), 0);
    lv_obj_set_style_text_color(s_voice_level_bar, lv_color_hex(UI_THEME_AMBER), 0);

    s_voice_dur_lbl = ui_theme_label(p, "时长: 0.0s", 16, 122, 208, UI_THEME_MUTED, ui_font_pick_14("时长: 0.0s"));

    // 识别文本大视窗
    s_voice_text_box = ui_theme_box(p, 16, 144, 208, 116, UI_THEME_PANEL, 0);
    lv_obj_set_style_border_width(s_voice_text_box, 2, 0);
    lv_obj_set_style_border_color(s_voice_text_box, lv_color_hex(UI_THEME_GRID_HI), 0);

    lv_obj_t *box_title = lv_label_create(s_voice_text_box);
    lv_obj_set_pos(box_title, 8, 6);
    lv_label_set_text(box_title, "▶ 识别结果 (Prompt)");
    lv_obj_set_style_text_font(box_title, ui_font_pick_14("▶ 识别结果"), 0);
    lv_obj_set_style_text_color(box_title, lv_color_hex(UI_THEME_MUTED), 0);

    s_voice_text_lbl = lv_label_create(s_voice_text_box);
    lv_obj_set_pos(s_voice_text_lbl, 8, 28);
    lv_obj_set_width(s_voice_text_lbl, 192);
    lv_label_set_long_mode(s_voice_text_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_voice_text_lbl, "(暂无内容，按 OK 开始说话)");
    lv_obj_set_style_text_font(s_voice_text_lbl, ui_font_pick_14("暂无内容"), 0);
    lv_obj_set_style_text_color(s_voice_text_lbl, lv_color_hex(UI_THEME_TEXT), 0);

    // 底部操作说明
    ui_theme_divider(p, 12, 268, 216);
    s_voice_hint_lbl = ui_theme_label(p, "[OK]录音/结束  [上下]切页", 12, 276, 216,
                                      UI_THEME_MUTED, ui_font_pick_14("[OK]录音/结束  [上下]切页"));
    ui_theme_center(s_voice_hint_lbl);

    lv_obj_t *menu_hint = ui_theme_label(p, "[长按OK]返回主菜单", 12, 296, 216,
                                        UI_THEME_MUTED, ui_font_pick_14("[长按OK]返回主菜单"));
    ui_theme_center(menu_hint);
}

static void voice_panel_tick(void)
{
    if (!s_voice_state_lbl) return;

    voice_state_t vs = app_voice_get_state();

    switch (vs) {
    case VOICE_RECORDING: {
        lv_label_set_text(s_voice_state_lbl, "● 正在录音 (再按 OK 结束)");
        lv_obj_set_style_text_color(s_voice_state_lbl, lv_color_hex(UI_THEME_RED), 0);
        lv_obj_set_style_border_color(s_voice_level_box, lv_color_hex(UI_THEME_RED), 0);

        int lvl = app_voice_get_level() / 10;   // 0..10
        if (lvl < 0) lvl = 0;
        if (lvl > 10) lvl = 10;
        char bar[64] = {0};
        int pos = 0;
        for (int i = 0; i < 10; i++)
            pos += snprintf(bar + pos, sizeof(bar) - pos, i < lvl ? "■" : "░");
        lv_label_set_text(s_voice_level_bar, bar);

        uint32_t ms = app_voice_get_duration_ms();
        lv_label_set_text_fmt(s_voice_dur_lbl, "时长: %u.%us / 30s", (unsigned)(ms / 1000),
                              (unsigned)((ms % 1000) / 100));

        lv_label_set_text(s_voice_hint_lbl, "短按 [OK] 结束录音并识别");
        break;
    }
    case VOICE_UPLOADING:
        lv_label_set_text(s_voice_state_lbl, "▲ 语音上传 & 识别中…");
        lv_obj_set_style_text_color(s_voice_state_lbl, lv_color_hex(UI_THEME_CYAN), 0);
        lv_obj_set_style_border_color(s_voice_level_box, lv_color_hex(UI_THEME_CYAN), 0);
        lv_label_set_text(s_voice_level_bar, "■■■■■■■■■■");
        lv_label_set_text(s_voice_hint_lbl, "正在识别，请稍候…");
        break;
    case VOICE_REVIEW: {
        const char *res = app_voice_get_result_text();
        lv_label_set_text(s_voice_state_lbl, "★ 识别完成 (等待用户确认)");
        lv_obj_set_style_text_color(s_voice_state_lbl, lv_color_hex(UI_THEME_AMBER), 0);
        lv_obj_set_style_border_color(s_voice_level_box, lv_color_hex(UI_THEME_AMBER), 0);
        lv_label_set_text(s_voice_level_bar, "==========");

        lv_label_set_text(s_voice_text_lbl, res[0] ? res : "(空内容)");
        lv_label_set_text(s_voice_hint_lbl, "OK发送 上续说 下撤销 双击重录");
        break;
    }
    case VOICE_DONE: {
        const char *res = app_voice_get_result_text();
        lv_label_set_text(s_voice_state_lbl, "√ Prompt 发送成功");
        lv_obj_set_style_text_color(s_voice_state_lbl, lv_color_hex(UI_THEME_GREEN), 0);
        lv_obj_set_style_border_color(s_voice_level_box, lv_color_hex(UI_THEME_GREEN), 0);
        lv_label_set_text(s_voice_level_bar, "░░░░░░░░░░");
        if (res[0]) lv_label_set_text(s_voice_text_lbl, res);
        lv_label_set_text(s_voice_hint_lbl, "已注入会话，按 [OK] 新录音");
        break;
    }
    case VOICE_FAILED: {
        const char *res = app_voice_get_result_text();
        lv_label_set_text(s_voice_state_lbl, "× 识别或发送失败");
        lv_obj_set_style_text_color(s_voice_state_lbl, lv_color_hex(UI_THEME_RED), 0);
        lv_obj_set_style_border_color(s_voice_level_box, lv_color_hex(UI_THEME_RED), 0);
        lv_label_set_text(s_voice_level_bar, "░░░░░░░░░░");
        if (res[0]) lv_label_set_text(s_voice_text_lbl, res);
        lv_label_set_text(s_voice_hint_lbl, "按 [OK] 重新录音");
        break;
    }
    case VOICE_IDLE:
    default:
        lv_label_set_text(s_voice_state_lbl, "[ 短按 OK 开始录音 ]");
        lv_obj_set_style_text_color(s_voice_state_lbl, lv_color_hex(UI_THEME_MUTED), 0);
        lv_obj_set_style_border_color(s_voice_level_box, lv_color_hex(UI_THEME_GRID), 0);
        lv_label_set_text(s_voice_level_bar, "░░░░░░░░░░");
        lv_label_set_text(s_voice_dur_lbl, "时长: 0.0s / 30s");
        lv_label_set_text(s_voice_hint_lbl, "[OK]录音/结束  [上下]切页");
        break;
    }
}

// ---------------------------------------------------------------- 轮播
static void strip_x_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void go_to(int page)
{
    if (!s_strip) return;
    if (page < 0) page = PAGE_COUNT - 1;
    if (page >= PAGE_COUNT) page = 0;
    s_page = page;

    lv_anim_delete(s_strip, strip_x_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_strip);
    lv_anim_set_exec_cb(&a, strip_x_cb);
    lv_anim_set_values(&a, lv_obj_get_x(s_strip), -page * PANEL_W);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void update_clock(lv_obj_t *lbl)
{
    if (!lbl) return;
    if (app_net_time_synced()) {
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        lv_label_set_text_fmt(lbl, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    } else {
        lv_label_set_text(lbl, "--:--");
    }
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    update_clock(s_clock);
    update_clock(s_pet_clock);
    update_clock(s_voice_clock);
    notify_panel_tick();
    pet_panel_tick();
    voice_panel_tick();
}

static void carousel_enter(int start)
{
    setenv("TZ", "UTC-8", 1);
    tzset();

    s_kind_until_us = 0;
    s_base_kind = APP_NOTIFY_IDLE;
    s_eff_kind = APP_NOTIFY_IDLE;
    s_last_version = 0;
    s_rendered_stale = false;
    s_pet_have_state = false;
    s_page = start;

    s_scr = ui_theme_screen();
    s_strip = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_strip);
    lv_obj_remove_flag(s_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_strip, 0, 0);
    lv_obj_set_size(s_strip, PANEL_W * PAGE_COUNT, PANEL_H);

    notify_panel_create(panel_create(0));
    pet_panel_create(panel_create(PANEL_W));
    voice_panel_create(panel_create(PANEL_W * 2));
    lv_obj_set_x(s_strip, -start * PANEL_W);

    s_timer = lv_timer_create(tick, 100, NULL);
    lv_screen_load(s_scr);

    app_notify_set_enabled(true);
    ESP_LOGI(TAG, "8-bit 状态页进入 (start=%d, total_pages=%d)", start, PAGE_COUNT);
}

static void carousel_exit(void)
{
    // 返回菜单时收尾:录音中→发送收尾;待确认→撤销,避免状态悬挂。
    voice_state_t vs = app_voice_get_state();
    if (vs == VOICE_RECORDING) {
        app_voice_stop_record();
    } else if (vs == VOICE_REVIEW) {
        app_voice_cancel();
    }
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_strip = NULL;
        s_status = s_title = s_text = s_agg = s_clock = NULL;
        s_pet_anim = s_pet_state = s_pet_clock = NULL;
        s_voice_clock = s_voice_tag = s_voice_state_lbl = NULL;
        s_voice_level_box = s_voice_level_bar = s_voice_dur_lbl = NULL;
        s_voice_text_box = s_voice_text_lbl = s_voice_hint_lbl = NULL;
        for (int i = 0; i < LIST_ROWS; i++) s_row[i] = s_tag[i] = s_name[i] = NULL;
    }
}

static void carousel_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    voice_state_t vs = app_voice_get_state();

    if (btn == BSP_BTN_OK) {
        // 只有在语音对讲页 (s_page == 2) 才响应语音录音/发送/重录操作
        if (s_page == 2) {
            if (ev == BSP_BTN_CLICK) {
                if (vs == VOICE_RECORDING) {
                    app_voice_stop_record();                 // 结束录音 → 识别
                } else if (vs == VOICE_REVIEW) {
                    app_voice_commit();                      // 确认发送
                } else if (vs == VOICE_IDLE || vs == VOICE_DONE || vs == VOICE_FAILED) {
                    app_voice_start_record();                // 开始新录音
                }
            } else if (ev == BSP_BTN_DOUBLE && vs == VOICE_REVIEW) {
                app_voice_start_record();                    // 重录(丢弃当前)
            }
        }
        return;
    }

    if (ev != BSP_BTN_CLICK) return;
    if (s_page == 2 && vs == VOICE_REVIEW) { // 语音页待确认时上下键改作语音操作
        if (btn == BSP_BTN_UP)        app_voice_start_append();   // 继续说
        else if (btn == BSP_BTN_DOWN) app_voice_cancel();         // 撤销
        return;
    }
    if (btn == BSP_BTN_DOWN) go_to(s_page + 1);
    else if (btn == BSP_BTN_UP) go_to(s_page - 1);
}

void demo_notify_enter(void) { carousel_enter(0); }
void demo_pet_enter(void)    { carousel_enter(1); }
void demo_voice_enter(void)  { carousel_enter(2); }
void demo_notify_exit(void)  { carousel_exit(); }
void demo_notify_key(bsp_btn_t btn, bsp_btn_ev_t ev) { carousel_key(btn, ev); }
