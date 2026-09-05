/*
 * control_center - openvela 桌宠控制中心（无 WebSocket 精简版）
 *
 * 职责：
 *   1. libcurl HTTP 激活拿 token（带缓存）
 *   2. IPC 端点管理（arecord/button_led/lvgldemo/aplay）
 *   3. 按键门控：button_led 通知 g_recording → 控制 arecord 上行转发
 *   4. UI 消息处理：lvgldemo 配网请求（打桩）
 *
 * 注意：WebSocket 功能已剥离，后续补 libwebsockets + TLS 配置后再加回。
 * 对齐 xiaozhi-esp32 websocket_protocol.cc 的协议留待 WS 启用时对接。
 */

#include "control_center.h"
#include "ipc_udp.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>

#include <curl/curl.h>
#include <cJSON.h>
#include <opus.h>   /* opus 解码；头在 .../apps/external/opus/opus/include/ 平面目录（无 opus/ 子层），故用 <opus.h> */

#define CC_TAG "control_center"

/* ===== 全局状态 ===== */
static volatile bool g_stop = false;

static char g_token[256] = {0};      /* Authorization token */
static char g_device_id[64] = {0};   /* MAC */
static char g_client_id[96] = {0};   /* 客户端标识 */

/* IPC 端点 */
static p_ipc_endpoint_t g_ep_audio_up = NULL;    /* 收 arecord opus */
static p_ipc_endpoint_t g_ep_audio_down = NULL; /* 发 aplay */
static p_ipc_endpoint_t g_ep_ui = NULL;          /* 发 lvgldemo */
static p_ipc_endpoint_t g_ep_button = NULL;      /* 收 button_led 按键消息(BUTTON_PORT_UP) */
static p_ipc_endpoint_t g_ep_ui_up = NULL;       /* 收 lvgldemo 触摸/配网请求(UI_PORT_UP) */

/* 录音状态：由 button_led 通过 IPC 通知 */
static volatile bool g_recording = false;

/* ===== libcurl 写回调 ===== */
struct memory {
    char *response;
    size_t size;
};

static size_t write_cb(void *data, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct memory *mem = (struct memory *)userp;
    char *ptr = realloc(mem->response, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }
    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), data, realsize);
    mem->size += realsize;
    mem->response[mem->size] = 0;
    return realsize;
}

/* ===== 设备标识 ===== */
static int get_device_id(char *out, size_t n)
{
    FILE *f = fopen("/sys/class/net/wlan0/address", "r");
    if (f) {
        if (fgets(out, n, f)) {
            out[strcspn(out, "\r\n")] = 0;
        }
        fclose(f);
        if (strlen(out) > 0) {
            return 0;
        }
    }
    strncpy(out, "AA:BB:CC:DD:EE:FF", n - 1);
    out[n - 1] = 0;
    return -1;
}

static void make_client_id(char *out, size_t n)
{
    char tmp[64];
    strncpy(tmp, g_device_id, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    for (char *p = tmp; *p; p++) {
        if (*p == ':') {
            *p = '-';
        }
    }
    snprintf(out, n, "openvela-%s", tmp);
}

/* ===== HTTP 激活 ===== */
static int device_activate(void)
{
    /* 先用缓存 token */
    FILE *f = fopen("/data/token", "r");
    if (f) {
        if (fgets(g_token, sizeof(g_token), f)) {
            g_token[strcspn(g_token, "\r\n")] = 0;
        }
        fclose(f);
        if (strlen(g_token) > 0) {
            printf(CC_TAG ": use cached token\n");
            return 0;
        }
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    struct memory chunk = {0};
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

    char body[256];
    snprintf(body, sizeof(body),
             "{\"device_id\":\"%s\",\"client_id\":\"%s\",\"model\":\"openvela-desktop-pet\"}",
             g_device_id, g_client_id);

    curl_easy_setopt(curl, CURLOPT_URL, XIAOZHI_ACTIVATE_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        printf(CC_TAG ": activate failed: %s\n", curl_easy_strerror(res));
        free(chunk.response);
        return -1;
    }

    /* 解析 token：chunk.response 可能为空（网络返回空/curl 未写数据），必须先判空，否则 cJSON_Parse(NULL) 会解引用崩溃 */
    if (!chunk.response || chunk.size == 0) {
        printf(CC_TAG ": activate: empty response, skip parse\n");
        return -1;
    }
    cJSON *root = cJSON_Parse(chunk.response);
    if (root) {
        cJSON *t = cJSON_GetObjectItem(root, "token");
        if (cJSON_IsString(t)) {
            strncpy(g_token, t->valuestring, sizeof(g_token) - 1);
            g_token[sizeof(g_token) - 1] = 0;
        }
        cJSON *io = cJSON_GetObjectItem(root, "websocket");
        if (cJSON_IsString(io)) {
            printf(CC_TAG ": server io=%s\n", io->valuestring);
        }
        cJSON_Delete(root);
    }
    free(chunk.response);

    if (strlen(g_token) > 0) {
        f = fopen("/data/token", "w");
        if (f) {
            fputs(g_token, f);
            fclose(f);
        }
        return 0;
    }
    printf(CC_TAG ": activate no token in response\n");
    return -1;
}

/* ===== IPC 回调：arecord 上报 opus → 仅在 recording 态转发 aplay（本地回环测试）=====
 * WebSocket 未启用时，把 arecord 的 opus 解码后转发 aplay，验证音频通路通。 */
static OpusDecoder *g_dec = NULL;
static unsigned char g_pcm_buf[16000 * 2];  /* 1s @ 16kHz 16bit */

static void on_audio_from_arecord(const char *data, int len, void *user)
{
    (void)user;
    if (!g_recording) {
        return;
    }
    if (!g_dec || !g_ep_audio_down) {
        return;
    }

    /* 跳过 BinaryProtocol3 头（4字节），payload 是 opus */
    const unsigned char *opus = (const unsigned char *)data;
    int opus_len = len;
    if (len > 4) {
        /* 可能带 bp3 头，尝试跳过 */
        if (opus[0] == XZ_BIN_TYPE_OPUS) {
            uint16_t plen = (opus[2] << 8) | opus[3];
            if ((int)(4 + plen) <= len) {
                opus += 4;
                opus_len = plen;
            }
        }
    }

    int frame_size = opus_decoder_get_size(g_dec);
    int samples = opus_decode(g_dec, opus, opus_len, (opus_int16 *)g_pcm_buf,
                              sizeof(g_pcm_buf) / 2, 0);
    if (samples > 0) {
        g_ep_audio_down->send(g_ep_audio_down, (const char *)g_pcm_buf, samples * 2);
    }
}

/* ===== IPC 回调：button_led 按键消息 ===== */
static void on_button_msg(const char *data, int len, void *user)
{
    (void)user;
    if (!data || len <= 0) {
        return;
    }
    char buf[128];
    int n = len;
    if (n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    memcpy(buf, data, n);
    buf[n] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        printf(CC_TAG ": button msg parse fail: %s\n", buf);
        return;
    }
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type)) {
        if (strcmp(type->valuestring, "record_start") == 0) {
            g_recording = true;
            printf(CC_TAG ": recording ON (K1 pressed)\n");
        } else if (strcmp(type->valuestring, "record_stop") == 0) {
            g_recording = false;
            printf(CC_TAG ": recording OFF (K1 released)\n");
        }
    }
    cJSON_Delete(root);
}

/* ===== IPC 回调：lvgldemo 触摸/配网请求 ===== */
static void on_ui_msg(const char *data, int len, void *user)
{
    (void)user;
    if (!data || len <= 0) {
        return;
    }
    char buf[512];
    int n = len;
    if (n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    memcpy(buf, data, n);
    buf[n] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        printf(CC_TAG ": ui msg parse fail: %s\n", buf);
        return;
    }
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type)) {
        const char *t = type->valuestring;
        if (strcmp(t, "wifi_scan") == 0) {
            printf(CC_TAG ": [stub] wifi_scan requested\n");
            const char *reply = "{\"type\":\"wifi_list\",\"aps\":[]}";
            if (g_ep_ui) {
                g_ep_ui->send(g_ep_ui, reply, (int)strlen(reply));
            }
        } else if (strcmp(t, "wifi_connect") == 0) {
            cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
            cJSON *pwd = cJSON_GetObjectItem(root, "pwd");
            const char *s = cJSON_IsString(ssid) ? ssid->valuestring : "?";
            const char *p = cJSON_IsString(pwd) ? pwd->valuestring : "";
            printf(CC_TAG ": [stub] wifi_connect ssid=%s pwd_len=%d\n", s, (int)strlen(p));
            const char *reply = "{\"type\":\"wifi_status\",\"state\":\"disconnected\"}";
            if (g_ep_ui) {
                g_ep_ui->send(g_ep_ui, reply, (int)strlen(reply));
            }
        } else {
            printf(CC_TAG ": ui msg type=%s\n", t);
        }
    }
    cJSON_Delete(root);
}

/* ===== main ===== */
int main(int argc, char **argv)
{
    printf(CC_TAG ": starting (no-ws mode)\n");
    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* opus 解码器（本地方便时用）*/
    int err;
    g_dec = opus_decoder_create(OPUS_SAMPLE_RATE, OPUS_CHANNELS, &err);
    if (!g_dec) {
        printf(CC_TAG ": opus decoder create failed: %d\n", err);
    }

    get_device_id(g_device_id, sizeof(g_device_id));
    make_client_id(g_client_id, sizeof(g_client_id));
    printf(CC_TAG ": device_id=%s client_id=%s\n", g_device_id, g_client_id);

    /* IPC 端点 */
    g_ep_audio_up = ipc_endpoint_create_udp(AUDIO_PORT_UP, 0, on_audio_from_arecord, NULL);
    g_ep_audio_down = ipc_endpoint_create_udp(0, AUDIO_PORT_DOWN, NULL, NULL);
    g_ep_ui = ipc_endpoint_create_udp(0, UI_PORT_DOWN, NULL, NULL);
    g_ep_button = ipc_endpoint_create_udp(BUTTON_PORT_UP, 0, on_button_msg, NULL);
    if (!g_ep_button) {
        printf(CC_TAG ": WARN: button endpoint create failed\n");
    }
    g_ep_ui_up = ipc_endpoint_create_udp(UI_PORT_UP, 0, on_ui_msg, NULL);
    if (!g_ep_ui_up) {
        printf(CC_TAG ": WARN: ui_up endpoint create failed\n");
    }

    /* HTTP 激活：本板无真实外网，curl 在 RTOS 上无网解析时可能触发 Data abort 崩溃。
     * 阶段目标为打通本地 IPC 链路，故遇无外网环境直接跳过 activate，不执行 curl 网络请求。
     * device_activate() 函数保留，待接入 Wi-Fi/外网或提供 /data/token 缓存后再启用。
     */
    printf(CC_TAG ": activate skipped (no-network build, IPC path only)\n");

    printf(CC_TAG ": running. IPC ready. Press K1 to toggle recording.\n");

    /* 主循环：IPC 端点在后台线程运行，这里只 sleep 等退出 */
    while (!g_stop) {
        sleep(1);
    }

    /* 清理 */
    ipc_endpoint_destroy_udp(g_ep_audio_up);
    ipc_endpoint_destroy_udp(g_ep_audio_down);
    ipc_endpoint_destroy_udp(g_ep_ui);
    ipc_endpoint_destroy_udp(g_ep_button);
    ipc_endpoint_destroy_udp(g_ep_ui_up);
    if (g_dec) {
        opus_decoder_destroy(g_dec);
    }
    curl_global_cleanup();
    printf(CC_TAG ": exit\n");
    return 0;
}
