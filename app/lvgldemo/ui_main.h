#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "lvgl.h"

/* 创建桌宠界面控件，返回 stt/tts/表情 三个 label 指针 */
void ui_create_views(lv_obj_t **stt, lv_obj_t **tts, lv_obj_t **emo);

/* ===== 阶段4：WiFi 配网界面 ===== */

/* WiFi 状态枚举（图标颜色/文字） */
typedef enum {
    UI_WIFI_DISCONNECTED = 0,  /* 未连接：红色 */
    UI_WIFI_SCANNING,          /* 扫描中：黄色 */
    UI_WIFI_CONNECTING,        /* 连接中：黄色 */
    UI_WIFI_CONNECTED,         /* 已连接：绿色 */
} ui_wifi_state_t;

/* 创建顶部 WiFi 状态图标（右上角），返回 label 指针。
 * 点击图标触发扫描请求（通过 cb_scan 回调上报 control_center）。 */
lv_obj_t *ui_create_wifi_bar(lv_obj_t *parent);

/* 创建 WiFi 配网弹出面板（扫描结果列表 + 密码输入 + 确认按钮）。
 * 默认隐藏，点击状态图标后显示。
 * bar        : 由 ui_create_wifi_bar 创建的状态图标（用于绑定点击事件 → 弹面板）
 * out_panel  : 返回整个面板容器（用于显示/隐藏）
 * out_list   : 扫描结果列表（用于填充 SSID）
 * out_pwd    : 密码输入框
 * cb_scan    : 点击状态图标时回调（请求扫描）
 * cb_connect : 选定 SSID 并输入密码后回调（请求连接，ssid/pwd 通过参数传） */
void ui_create_wifi_panel(lv_obj_t *parent, lv_obj_t *bar,
                          lv_obj_t **out_panel, lv_obj_t **out_list,
                          lv_obj_t **out_pwd,
                          void (*cb_scan)(void),
                          void (*cb_connect)(const char *ssid, const char *pwd));

/* 更新状态图标（颜色 + 文字） */
void ui_wifi_set_status(lv_obj_t *bar, ui_wifi_state_t st);

/* 填充扫描结果列表（清空后追加），aps_json 形如 [{"ssid":"X","rssi":-60}, ...] */
void ui_wifi_fill_list(lv_obj_t *list, const char *aps_json);

#endif /* UI_MAIN_H */
