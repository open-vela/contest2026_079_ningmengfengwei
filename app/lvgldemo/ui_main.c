/*
 * ui_main.c - 桌宠界面控件创建
 *
 * 布局：上=表情，中=用户语音(stt)，下=回复文本(tts)
 * 显示风格：黑底白字，对齐 PDF 9.4 的 lvgldemo
 *
 * 阶段4新增：
 *   - 顶部右上角 WiFi 状态图标（点击触发扫描）
 *   - 弹出式配网面板：扫描列表 + 密码输入 + 确认按钮
 *   - 状态图标颜色：红=未连/黄=扫描或连接中/绿=已连
 */

#include "ui_main.h"
#include <cJSON.h>
#include <string.h>
#include <stdio.h>

void ui_create_views(lv_obj_t **stt, lv_obj_t **tts, lv_obj_t **emo)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    /* 表情（顶部居中）*/
    lv_obj_t *e = lv_label_create(scr);
    lv_label_set_text(e, "(o_o)");
    lv_obj_set_style_text_font(e, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(e, lv_color_make(255, 200, 0), 0);
    lv_obj_align(e, LV_ALIGN_TOP_MID, 0, 20);

    /* 用户语音文字（中部，左对齐换行）*/
    lv_obj_t *s = lv_label_create(scr);
    lv_label_set_text(s, "...");
    lv_obj_set_style_text_color(s, lv_color_white(), 0);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s, 300);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, -10);

    /* 回复文本（下部）*/
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "...");
    lv_obj_set_style_text_color(t, lv_color_make(180, 255, 180), 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, 300);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 40);

    if (stt) *stt = s;
    if (tts) *tts = t;
    if (emo) *emo = e;
}

/* ===== 阶段4：WiFi 配网界面 ===== */

/* 状态图标当前选定的 SSID（列表点击后写入，确认按钮读出）*/
static char g_selected_ssid[64] = {0};

/* 配网面板里的回调（lv_100ask_xz_ai_main 注册）*/
static void (*s_cb_scan)(void) = NULL;
static void (*s_cb_connect)(const char *ssid, const char *pwd) = NULL;

/* 状态图标点击：切换面板显隐 + 触发扫描 */
static void wifi_bar_event_cb(lv_event_t *e)
{
    lv_obj_t *panel = (lv_obj_t *)lv_event_get_user_data(e);
    if (!panel) {
        return;
    }
    if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
        if (s_cb_scan) {
            s_cb_scan();
        }
    } else {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 列表项点击：把 SSID 记到 g_selected_ssid，并在密码框旁提示已选 */
static void wifi_list_item_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_obj_get_user_data(btn);
    if (ssid) {
        strncpy(g_selected_ssid, ssid, sizeof(g_selected_ssid) - 1);
        g_selected_ssid[sizeof(g_selected_ssid) - 1] = 0;
        printf("ui: selected ssid=%s\n", g_selected_ssid);
    }
}

/* 确认按钮：读取密码框内容，调用 cb_connect */
static void wifi_connect_btn_cb(lv_event_t *e)
{
    lv_obj_t *pwd = (lv_obj_t *)lv_event_get_user_data(e);
    if (!pwd || g_selected_ssid[0] == 0 || !s_cb_connect) {
        printf("ui: connect skipped (no ssid or no cb)\n");
        return;
    }
    const char *pwd_text = lv_textarea_get_text(pwd);
    s_cb_connect(g_selected_ssid, pwd_text ? pwd_text : "");
}

lv_obj_t *ui_create_wifi_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_label_create(parent);
    lv_label_set_text(bar, "WiFi:#");
    lv_obj_set_style_text_color(bar, lv_color_make(255, 60, 60), 0);  /* 红=未连 */
    lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

void ui_create_wifi_panel(lv_obj_t *parent, lv_obj_t *bar,
                          lv_obj_t **out_panel, lv_obj_t **out_list,
                          lv_obj_t **out_pwd,
                          void (*cb_scan)(void),
                          void (*cb_connect)(const char *ssid, const char *pwd))
{
    s_cb_scan = cb_scan;
    s_cb_connect = cb_connect;

    /* 整体面板：覆盖屏幕中下部 */
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 300, 200);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(panel, lv_color_make(30, 30, 30), 0);
    lv_obj_set_style_border_color(panel, lv_color_make(80, 80, 80), 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);  /* 默认隐藏 */

    /* 扫描结果列表 */
    lv_obj_t *list = lv_list_create(panel);
    lv_obj_set_size(list, 280, 110);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 5);

    /* 密码输入框 */
    lv_obj_t *pwd = lv_textarea_create(panel);
    lv_obj_set_size(pwd, 200, 30);
    lv_textarea_set_placeholder_text(pwd, "password");
    lv_textarea_set_password_mode(pwd, true);
    lv_obj_align(pwd, LV_ALIGN_BOTTOM_LEFT, 0, -5);

    /* 确认按钮 */
    lv_obj_t *btn = lv_btn_create(panel);
    lv_obj_set_size(btn, 70, 30);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, -5);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Connect");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, pwd);

    /* 状态图标点击 → 切面板显隐 + 触发扫描 */
    if (bar) {
        lv_obj_add_event_cb(bar, wifi_bar_event_cb, LV_EVENT_CLICKED, panel);
    }

    if (out_panel) *out_panel = panel;
    if (out_list)  *out_list = list;
    if (out_pwd)   *out_pwd = pwd;
}

void ui_wifi_set_status(lv_obj_t *bar, ui_wifi_state_t st)
{
    if (!bar) {
        return;
    }
    lv_color_t c;
    const char *txt;
    switch (st) {
    case UI_WIFI_DISCONNECTED: c = lv_color_make(255, 60, 60);  txt = "WiFi:#"; break;
    case UI_WIFI_SCANNING:     c = lv_color_make(255, 200, 0);  txt = "WiFi:?"; break;
    case UI_WIFI_CONNECTING:   c = lv_color_make(255, 200, 0);  txt = "WiFi:..."; break;
    case UI_WIFI_CONNECTED:    c = lv_color_make(60, 255, 60);  txt = "WiFi:*"; break;
    default:                   c = lv_color_make(255, 60, 60);  txt = "WiFi:#"; break;
    }
    lv_label_set_text(bar, txt);
    lv_obj_set_style_text_color(bar, c, 0);
}

void ui_wifi_fill_list(lv_obj_t *list, const char *aps_json)
{
    if (!list || !aps_json) {
        return;
    }
    lv_obj_clean(list);  /* 清空旧项 */

    cJSON *root = cJSON_Parse(aps_json);
    if (!cJSON_IsArray(root)) {
        printf("ui: wifi list parse fail: %s\n", aps_json);
        if (root) cJSON_Delete(root);
        return;
    }

    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n; i++) {
        cJSON *ap = cJSON_GetArrayItem(root, i);
        cJSON *ssid = cJSON_GetObjectItem(ap, "ssid");
        cJSON *rssi = cJSON_GetObjectItem(ap, "rssi");
        if (!cJSON_IsString(ssid)) {
            continue;
        }
        char line[96];
        if (cJSON_IsNumber(rssi)) {
            snprintf(line, sizeof(line), "%s (%d)", ssid->valuestring, rssi->valueint);
        } else {
            snprintf(line, sizeof(line), "%s", ssid->valuestring);
        }
        lv_obj_t *btn = lv_list_add_btn(list, NULL, line);
        if (btn) {
            /* 把 ssid 字符串挂在按钮 user_data 上，点击时取出。
             * 注意：list 被清空时这些字符串会泄漏，阶段4 简化处理。 */
            char *saved = (char *)malloc(strlen(ssid->valuestring) + 1);
            if (saved) {
                strcpy(saved, ssid->valuestring);
                lv_obj_set_user_data(btn, saved);
                lv_obj_add_event_cb(btn, wifi_list_item_cb, LV_EVENT_CLICKED, NULL);
            }
        }
    }
    cJSON_Delete(root);
}
