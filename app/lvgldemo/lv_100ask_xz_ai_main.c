/*
 * lv_100ask_xz_ai_main.c - openvela 桌宠 GUI 主逻辑
 *
 * 职责：
 *   1. lv_init + 创建 UI 控件（表情/stt/tts/WiFi 状态图标/配网面板）
 *   2. IPC 收 control_center 下发的 stt/llm/tts/wifi_list/wifi_status JSON（UI_PORT_DOWN）
 *   3. 用户在配网面板选网 + 输密码 → IPC 上报 wifi_scan/wifi_connect 给 control_center（UI_PORT_UP）
 *   4. 主循环：解析待处理 JSON 更新控件 + lv_timer_handler
 *
 * 线程安全：LVGL 非线程安全，IPC 回调只把 JSON 存入待处理缓冲，
 * 实际控件更新在主循环(主线程)做，避免多线程访问 LVGL。
 *
 * 对齐：xiaozhi-esp32 display/lcd_display + LVGL（表情/文字显示）+ 触摸配网
 */

#include "ipc_udp.h"
#include "ui_main.h"

#include "lvgl.h"
#include "src/drivers/nuttx/lv_nuttx_entry.h"
#include <cJSON.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define UI_TAG "lvgldemo"

static lv_obj_t *g_label_stt = NULL;
static lv_obj_t *g_label_tts = NULL;
static lv_obj_t *g_label_emo = NULL;

/* 阶段4：WiFi 配网控件 */
static lv_obj_t *g_wifi_bar = NULL;
static lv_obj_t *g_wifi_panel = NULL;
static lv_obj_t *g_wifi_list = NULL;
static lv_obj_t *g_wifi_pwd = NULL;

static p_ipc_endpoint_t g_ep_ui = NULL;

/* 待处理 UI 数据（IPC 后台线程写，主线程读后清）*/
static char g_pending_ui[1024];
static volatile bool g_has_ui = false;

/* ===== 阶段4：配网面板回调（由 ui_main 在 LVGL 事件中调用，主线程上下文）=====
 * 通过 IPC(UI_PORT_UP) 把请求 JSON 发给 control_center，由后者调 wlantool 实际执行。
 */
static void cb_wifi_scan(void)
{
    if (g_ep_ui) {
        const char *msg = "{\"type\":\"wifi_scan\"}";
        g_ep_ui->send(g_ep_ui, msg, (int)strlen(msg));
        printf(UI_TAG ": -> control_center wifi_scan\n");
    }
    if (g_wifi_bar) {
        ui_wifi_set_status(g_wifi_bar, UI_WIFI_SCANNING);
    }
}

static void cb_wifi_connect(const char *ssid, const char *pwd)
{
    if (!g_ep_ui || !ssid) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "wifi_connect");
    cJSON_AddStringToObject(root, "ssid", ssid);
    cJSON_AddStringToObject(root, "pwd", pwd ? pwd : "");
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (s) {
        g_ep_ui->send(g_ep_ui, s, (int)strlen(s));
        free(s);
        printf(UI_TAG ": -> control_center wifi_connect ssid=%s\n", ssid);
    }
    if (g_wifi_bar) {
        ui_wifi_set_status(g_wifi_bar, UI_WIFI_CONNECTING);
    }
    /* 隐藏面板，等待连接结果 */
    if (g_wifi_panel) {
        lv_obj_add_flag(g_wifi_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 解析 JSON 更新控件（主线程调用，LVGL 安全）*/
static void update_ui_from_json(const char *data, int len)
{
    char buf[1024];
    int n = (len < (int)sizeof(buf) - 1) ? len : (int)sizeof(buf) - 1;
    memcpy(buf, data, n);
    buf[n] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return;
    }
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type)) {
        const char *t = type->valuestring;
        if (strcmp(t, "stt") == 0) {
            cJSON *text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text) && g_label_stt) {
                lv_label_set_text(g_label_stt, text->valuestring);
            }
        } else if (strcmp(t, "llm") == 0) {
            /* llm 可能含表情或回复文本 */
            cJSON *emoji = cJSON_GetObjectItem(root, "emoji");
            if (cJSON_IsString(emoji) && g_label_emo) {
                lv_label_set_text(g_label_emo, emoji->valuestring);
            }
            cJSON *text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text) && g_label_tts) {
                lv_label_set_text(g_label_tts, text->valuestring);
            }
        } else if (strcmp(t, "tts") == 0) {
            cJSON *text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text) && g_label_tts) {
                lv_label_set_text(g_label_tts, text->valuestring);
            }
        } else if (strcmp(t, "wifi_list") == 0) {
            /* 阶段4：扫描结果下发，aps 字段是数组 */
            cJSON *aps = cJSON_GetObjectItem(root, "aps");
            if (aps) {
                char *aps_str = cJSON_PrintUnformatted(aps);
                if (aps_str) {
                    ui_wifi_fill_list(g_wifi_list, aps_str);
                    free(aps_str);
                }
            }
            if (g_wifi_bar) {
                ui_wifi_set_status(g_wifi_bar, UI_WIFI_DISCONNECTED);
            }
        } else if (strcmp(t, "wifi_status") == 0) {
            /* 阶段4：连接状态变化 {"state":"connected"/"disconnected"} */
            cJSON *state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state) && g_wifi_bar) {
                if (strcmp(state->valuestring, "connected") == 0) {
                    ui_wifi_set_status(g_wifi_bar, UI_WIFI_CONNECTED);
                } else {
                    ui_wifi_set_status(g_wifi_bar, UI_WIFI_DISCONNECTED);
                }
            }
        } else {
            printf(UI_TAG ": unknown type %s\n", t);
        }
    }
    cJSON_Delete(root);
}

/* IPC 回调：后台线程收到 control_center JSON，存待处理缓冲 */
static void process_ui_data(const char *data, int len, void *user)
{
    int n = (len < (int)sizeof(g_pending_ui) - 1) ? len : (int)sizeof(g_pending_ui) - 1;
    memcpy(g_pending_ui, data, n);
    g_pending_ui[n] = 0;
    g_has_ui = true;
}

int lv_100ask_xz_ai_main(int argc, char **argv)
{
    printf(UI_TAG ": [DBG] step0 enter main\n");

    /* 1. LVGL 初始化 + display/touch */
    printf(UI_TAG ": [DBG] step1 before lv_init\n");
    lv_init();
    printf(UI_TAG ": [DBG] step2 after lv_init\n");

    /* NuttX LVGL 集成：必须显式调 lv_nuttx_init() 注册 display/touch，
       否则 lv_scr_act() 返回 NULL → ui_create_views 解引用 NULL 崩溃 */
    extern void lv_nuttx_dsc_init(lv_nuttx_dsc_t * dsc);
    extern void lv_nuttx_init(const lv_nuttx_dsc_t * dsc, lv_nuttx_result_t * result);

    lv_nuttx_dsc_t nuttx_dsc;
    lv_nuttx_result_t nuttx_result;
    lv_nuttx_dsc_init(&nuttx_dsc);
    printf(UI_TAG ": [DBG] step2.4 nuttx_dsc fb=%s input=%s\n",
           nuttx_dsc.fb_path, nuttx_dsc.input_path);
    lv_nuttx_init(&nuttx_dsc, &nuttx_result);
    printf(UI_TAG ": [DBG] step2.5 lv_nuttx_init disp=%p indev=%p\n",
           (void *)nuttx_result.disp, (void *)nuttx_result.indev);

    /* 2. 创建 UI 控件：基础三 label + WiFi 状态图标 + 配网面板 */
    printf(UI_TAG ": [DBG] step3 before lv_scr_act\n");
    lv_obj_t *scr = lv_scr_act();
    printf(UI_TAG ": [DBG] step4 after lv_scr_act scr=%p\n", (void *)scr);
    if (!scr) {
        printf(UI_TAG ": [DBG] ERROR: lv_scr_act() returned NULL!\n");
        return -1;
    }

    printf(UI_TAG ": [DBG] step5 before ui_create_views\n");
    ui_create_views(&g_label_stt, &g_label_tts, &g_label_emo);
    printf(UI_TAG ": [DBG] step6 after ui_create_views\n");

    g_wifi_bar = ui_create_wifi_bar(scr);
    printf(UI_TAG ": [DBG] step7 after ui_create_wifi_bar\n");

    ui_create_wifi_panel(scr, g_wifi_bar,
                         &g_wifi_panel, &g_wifi_list, &g_wifi_pwd,
                         cb_wifi_scan, cb_wifi_connect);
    printf(UI_TAG ": [DBG] step8 after ui_create_wifi_panel\n");

    /* 3. IPC：收 control_center 下发(UI_PORT_DOWN)，可发触摸/配网事件(UI_PORT_UP) */
    g_ep_ui = ipc_endpoint_create_udp(UI_PORT_DOWN, UI_PORT_UP, process_ui_data, NULL);
    printf(UI_TAG ": [DBG] step9 after ipc create ep=%p\n", (void *)g_ep_ui);
    if (!g_ep_ui) {
        printf(UI_TAG ": ipc create failed\n");
    }

    /* 启动时默认显示未连接状态 */
    ui_wifi_set_status(g_wifi_bar, UI_WIFI_DISCONNECTED);
    printf(UI_TAG ": [DBG] step10 before main loop\n");

    /* 4. 主循环：CONFIG_LV_USE_NUTTX_LIBUV=y 时 LVGL 用 libuv 事件循环，
       不需要手动调 lv_timer_handler（会与 libuv 冲突导致死锁）。
       只需处理 IPC 待更新数据，然后让线程睡眠。 */
    printf(UI_TAG ": [DBG] step11 entering libuv idle loop\n");
    while (1) {
        if (g_has_ui) {
            update_ui_from_json(g_pending_ui, (int)strlen(g_pending_ui));
            g_has_ui = false;
        }
        usleep(10000);  /* 10ms 轮询 IPC，LVGL 定时器由 libuv 自动处理 */
    }

    /* 不会到达 */
    ipc_endpoint_destroy_udp(g_ep_ui);
    return 0;
}
