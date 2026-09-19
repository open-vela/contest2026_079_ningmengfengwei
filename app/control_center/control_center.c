// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2008-2023 100askTeam : Dongshan WEI <weidongshan@100ask.net> 
 * Discourse:  https://forums.100ask.net
 */
 
/*  Copyright (C) 2008-2023 深圳百问网科技有限公司
 *  All rights reserved
 *
 * 免责声明: 百问网编写的文档, 仅供学员学习使用, 可以转发或引用(请保留作者信息),禁止用于商业用途！
 * 免责声明: 百问网编写的程序, 用于商业用途请遵循GPL许可, 百问网不承担任何后果！
 * 
 * 本程序遵循GPL V3协议, 请遵循协议
 * 百问网学习平台   : https://www.100ask.net
 * 百问网交流社区   : https://forums.100ask.net
 * 百问网官方B站    : https://space.bilibili.com/275908810
 * 本程序所用开发板 : Linux开发板
 * 百问网官方淘宝   : https://100ask.taobao.com
 * 联系我们(E-mail) : weidongshan@100ask.net
 *
 *          版权所有，盗版必究。
 *  
 * 修改历史     版本号           作者        修改内容
 *-----------------------------------------------------
 * 2025.03.20      v01         百问科技      创建文件
 *-----------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdbool.h>
#include <libwebsockets.h>
#include <cJSON.h>
#include "websocket_client.h"
#include "http.h"
#include "ipc_udp.h"
#include "uuid.h"
#include "cfg.h"
#include "leds.h"

#include <syslog.h>
#define printf(fmt, ...) syslog(LOG_INFO, fmt, ##__VA_ARGS__)

static int g_ui_upload_enable = 1;
static int g_audio_upload_enable = 1;
static int g_audio_download_enable = 1;
static int g_audio_disabled_while_speaking = 0;
static char g_session_id[64] = "";

typedef enum ListeningMode {
    kListeningModeAutoStop,
    kListeningModeManualStop,
    kListeningModeAlwaysOn // 需要 AEC 支持
} ListeningMode;

// 定义设备状态枚举类型
typedef enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateFatalError
} DeviceState;

static p_ipc_endpoint_t g_ipc_ep_audio;
static p_ipc_endpoint_t g_ipc_ep_ui;
static p_ipc_endpoint_t g_ipc_ep_button;
static DeviceState g_device_state = kDeviceStateUnknown;
static void set_device_state(DeviceState state)
{
    g_device_state = state;
}

static void send_device_state(void)
{
    char stateString[64];
    snprintf(stateString, sizeof(stateString), "{\"state\":%d}", g_device_state);

    if (g_ui_upload_enable)
        g_ipc_ep_ui->send(g_ipc_ep_ui, stateString, strlen(stateString));
}

static void send_stt(const char* text)
{
    if (!g_ipc_ep_ui) {
        fprintf(stderr, "Error: g_ipc_ep_ui is nullptr\n");
        return;
    }

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "text", text);
    char *textString = cJSON_PrintUnformatted(j);
    g_ipc_ep_ui->send(g_ipc_ep_ui, textString, strlen(textString));
    cJSON_Delete(j);
    free(textString);
}

static void process_opus_data_downloaded(const char *buffer, size_t size)
{
#if 0    
    printf("Received opus data: %zu bytes\n", size);
    static int file_number = 1;
    // 构造文件名
    char filename[20];
    snprintf(filename, sizeof(filename), "test%03d.opus", file_number);

    // 打开文件
    FILE *file = fopen(filename, "wb");
    if (file) {
        // 写入Opus数据
        fwrite(buffer, 1, size, file);
        fclose(file);
        file_number++; // 增加文件编号
    } else {
        fprintf(stderr, "Failed to open file %s for writing\n", filename);
    }     
#endif    
    if (g_audio_download_enable)
    {
        g_ipc_ep_audio->send(g_ipc_ep_audio, buffer, size);
    }
}

static void send_start_listening_req(ListeningMode mode)
{
    char startString[256];
    snprintf(startString, sizeof(startString), "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"start\"", g_session_id);

    if (mode == kListeningModeAutoStop) {
        strcat(startString, ",\"mode\":\"auto\"}");
    } else if (mode == kListeningModeManualStop) {
        strcat(startString, ",\"mode\":\"manual\"}");
    } else if (mode == kListeningModeAlwaysOn) {
        strcat(startString, ",\"mode\":\"realtime\"}");
    }

    websocket_send_text(startString, strlen(startString));
    printf("Send: %s\n", startString);
}

static void send_stop_listening_req(void)
{
    char stopString[256];
    snprintf(stopString, sizeof(stopString), "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"stop\"", g_session_id);



    websocket_send_text(stopString, strlen(stopString));
    printf("Send: %s\n", stopString);
}
static void process_hello_json(const char *buffer, size_t size)
{    
    cJSON *j = cJSON_Parse(buffer);
    if (!j) {
        fprintf(stderr, "Failed to parse JSON\n");
        return;
    }

    cJSON *audio_params = cJSON_GetObjectItem(j, "audio_params");
    if (audio_params) {
        int sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate")->valueint;
        int channels = cJSON_GetObjectItem(audio_params, "channels")->valueint;
        printf("Received valid 'hello' message with sample_rate: %d and channels: %d\n", sample_rate, channels);
    }

    cJSON *session_id = cJSON_GetObjectItem(j, "session_id");
    if (session_id && cJSON_IsString(session_id)) {
        strncpy(g_session_id, session_id->valuestring, sizeof(g_session_id)-1);
    }

    const char *desc = "{\"session_id\":\"\",\"type\":\"iot\",\"update\":true,\"descriptors\":[{\"name\":\"LED1\",\"description\":\"厨房灯\",\"properties\":{\"status\":{\"description\":\"打开或者关闭\",\"type\":\"number\"}},\"methods\":{\"SetStatus\":{\"description\":\"设置状态\",\"parameters\":{\"status\":{\"description\":\"0或1\",\"type\":\"boolean\"}}}}}]}";
    websocket_send_text(desc, strlen(desc));
    printf("Send: %s\n", desc);

    desc = "{\"session_id\":\"\",\"type\":\"iot\",\"update\":true,\"descriptors\":[{\"name\":\"LED2\",\"description\":\"客厅灯\",\"properties\":{\"status\":{\"description\":\"打开或者关闭\",\"type\":\"number\"}},\"methods\":{\"SetStatus\":{\"description\":\"设置状态\",\"parameters\":{\"status\":{\"description\":\"0或1\",\"type\":\"boolean\"}}}}}]}";
    websocket_send_text(desc, strlen(desc));
    printf("Send: %s\n", desc);

    const char *startString = "{\"session_id\":\"\",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"auto\"}";
    websocket_send_text(startString, strlen(startString));
    printf("Send: %s\n", startString);

    g_audio_disabled_while_speaking = 0;

    // const char *state = "{\"session_id\":\"\",\"type\":\"iot\",\"update\":true,\"states\":[{\"name\":\"Speaker\",\"state\":{\"volume\":80}},{\"name\":\"Backlight\",\"state\":{\"brightness\":75}},{\"name\":\"Battery\",\"state\":{\"level\":0,\"charging\":false}}]}";
    // websocket_send_text(state, strlen(state));
    // printf("Send: %s\n", state);

    cJSON_Delete(j);
}

static void process_other_json(const char *buffer, size_t size)
{
    cJSON *j = cJSON_Parse(buffer);
    if (!j) {
        fprintf(stderr, "Failed to parse JSON\n");
        return;
    }

    cJSON *type = cJSON_GetObjectItem(j, "type");
    if (!type || !cJSON_IsString(type)) {
        cJSON_Delete(j);
        return;
    }

    if (strcmp(type->valuestring, "tts") == 0) {
        cJSON *state = cJSON_GetObjectItem(j, "state");
        if (state && cJSON_IsString(state)) {
            if (strcmp(state->valuestring, "start") == 0) {
                // 下发语音, 可以关闭录音
                g_audio_disabled_while_speaking = 1;
                set_device_state(kDeviceStateListening);
                send_device_state();
            } else if (strcmp(state->valuestring, "stop") == 0) {
                // 本次交互结束, 可以继续上传声音
                // 等待一会以免她听到自己的话误以为再次对话
                sleep(1);
                send_start_listening_req(kListeningModeAutoStop);
                set_device_state(kDeviceStateListening);
                send_device_state();

                g_audio_disabled_while_speaking = 0;
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                // 取出"text", 通知GUI
                cJSON *text = cJSON_GetObjectItem(j, "text");
                if (text && cJSON_IsString(text)) {
                    send_stt(text->valuestring);
                }
                send_start_listening_req(kListeningModeAutoStop);
                set_device_state(kDeviceStateSpeaking);
                send_device_state();
            }
        }
    } else if (strcmp(type->valuestring, "stt") == 0) {
        // 表示服务器端识别到了用户语音, 取出"text", 通知GUI
        cJSON *text = cJSON_GetObjectItem(j, "text");
        if (text && cJSON_IsString(text)) {
            send_stt(text->valuestring);
        }
    } else if (strcmp(type->valuestring, "llm") == 0) {
        // 有"happy"等取值
        cJSON *emotion = cJSON_GetObjectItem(j, "emotion");
        // 处理情绪...
    } else if (strcmp(type->valuestring, "iot") == 0) {
        // 处理 IoT 消息...
        // {"type":"iot","commands":[{"name":"LED1","method":"SetStatus","parameters":{"status":1}}],"session_id":"7116146e"}
        // 获取commands数组
        cJSON *commands = cJSON_GetObjectItem(j, "commands");
        if (commands && cJSON_IsArray(commands)) {
            // 遍历commands数组
            int commands_count = cJSON_GetArraySize(commands);
            for (int i = 0; i < commands_count; i++) {
                cJSON *command = cJSON_GetArrayItem(commands, i);
                
                cJSON *method = cJSON_GetObjectItem(command, "method");
                cJSON *name = cJSON_GetObjectItem(command, "name");
                cJSON *parameters = cJSON_GetObjectItem(command, "parameters"); 
                
                if (name && cJSON_IsString(name) && strncmp(name->valuestring, "LED", 3) == 0) {
                    int which = name->valuestring[3] - '1';
                    if (method && cJSON_IsString(method)) {
                        if (strcmp(method->valuestring, "SetStatus") == 0 && parameters) {
                            cJSON *status = cJSON_GetObjectItem(parameters, "status");
                            if (status) {
                                int led_status = status->valueint;
                                printf("LED%d SetStatus request received, status: %d\n", which+1, led_status);
                                leds_ctl(which, led_status);
                            }
                        }
                    }
                }
            }
        }
       
    }

    cJSON_Delete(j);
}

static void process_txt_data_downloaded(const char *buffer, size_t size)
{
    cJSON *j = cJSON_Parse(buffer);
    if (!j) {
        fprintf(stderr, "Failed to parse JSON message\n");
        return;
    }

    cJSON *type = cJSON_GetObjectItem(j, "type");
    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "hello") == 0) {
        process_hello_json(buffer, size);
    } else {
        process_other_json(buffer, size);
    }

    cJSON_Delete(j);
}

int process_opus_data_uploaded(char *buffer, size_t size, void *user_data)
{
#if 0    
    static int file_number = 1;
    // 构造文件名
    char filename[20];
    snprintf(filename, sizeof(filename), "test%03d.opus", file_number);

    // 打开文件
    FILE *file = fopen(filename, "wb");
    if (file) {
        // 写入Opus数据
        fwrite(buffer, 1, size, file);
        fclose(file);
        file_number++; // 增加文件编号
    } else {
        fprintf(stderr, "Failed to open file %s for writing\n", filename);
    }   
#endif
    if (g_audio_upload_enable && !g_audio_disabled_while_speaking) {
        static int cnt = 0;
        if ((cnt++ % 100) == 0)
            printf("Send opus data to server: %zu count: %d\n", size, cnt);
        websocket_send_binary(buffer, size);
    }
    return 0;
}

int process_ui_data(char *buffer, size_t size, void *user_data)
{
    if (!strcmp(buffer, "standby"))
    {
        //send_stop_listening_req();
        //set_device_state(kDeviceStateIdle);
        g_audio_upload_enable = 0;
        g_audio_download_enable = 0;
        g_ui_upload_enable = 0;
    }
    else
    {
        //send_start_listening_req(kListeningModeAutoStop);
        //set_device_state(kDeviceStateListening);
        g_audio_upload_enable = 1;
        g_audio_download_enable = 1;
        g_ui_upload_enable = 1;
        g_audio_disabled_while_speaking = 0;
    }
    return 0;
}

int on_button_msg(char *buffer, size_t size, void *user_data)
{
    printf("Received button message: %s\n", buffer);

    cJSON *j = cJSON_Parse(buffer);
    if (!j) {
        fprintf(stderr, "Failed to parse button JSON\n");
        return -1;
    }

    cJSON *type = cJSON_GetObjectItem(j, "type");
    if (!type || !cJSON_IsString(type)) {
        cJSON_Delete(j);
        return -1;
    }

    if (strcmp(type->valuestring, "record_start") == 0) {
        printf("recording ON (K1 pressed)\n");
        g_audio_upload_enable = 1;
        g_audio_disabled_while_speaking = 0;
    } else if (strcmp(type->valuestring, "record_stop") == 0) {
        printf("recording OFF (K1 released)\n");
        g_audio_upload_enable = 0;
    }

    cJSON_Delete(j);
    return 0;
}

/**
 * 从配置文件中读取 UUID
 *
 * 该函数尝试从 /etc/xiaozhi.cfg 文件中读取 UUID。
 * 如果文件存在且包含有效的 UUID，则返回该 UUID。
 * 否则，返回空字符串。
 *
 * @return 从配置文件中读取的 UUID，如果未找到则返回空字符串
 */
char* read_uuid_from_config() {
    FILE *config_file = fopen(CFG_FILE, "r");
    if (!config_file) {
        fprintf(stderr, "Failed to open " CFG_FILE " for reading\n");
        return NULL;
    }

    fseek(config_file, 0, SEEK_END);
    long length = ftell(config_file);
    fseek(config_file, 0, SEEK_SET);
    
    char *data = malloc(length + 1);
    fread(data, 1, length, config_file);
    data[length] = '\0';
    fclose(config_file);

    cJSON *config_json = cJSON_Parse(data);
    free(data);
    
    if (!config_json) {
        fprintf(stderr, "Failed to parse " CFG_FILE "\n");
        return NULL;
    }

    cJSON *uuid = cJSON_GetObjectItem(config_json, "uuid");
    if (uuid && cJSON_IsString(uuid)) {
        char *result = strdup(uuid->valuestring);
        cJSON_Delete(config_json);
        return result;
    }

    cJSON_Delete(config_json);
    return NULL;
}

/**
 * 将 UUID 写入配置文件
 *
 * 该函数将给定的 UUID 写入 /etc/xiaozhi.cfg 文件。
 * 如果文件不存在，则创建新文件。
 *
 * @param uuid 要写入配置文件的 UUID
 * @return 成功写入文件返回 true，否则返回 false
 */
bool write_uuid_to_config(const char* uuid) {
    FILE *config_file = fopen(CFG_FILE, "w");
    if (!config_file) {
        fprintf(stderr, "Failed to open " CFG_FILE " for writing\n");
        return false;
    }

    cJSON *config_json = cJSON_CreateObject();
    cJSON_AddStringToObject(config_json, "uuid", uuid);
    
    char *json_str = cJSON_Print(config_json);
    fwrite(json_str, 1, strlen(json_str), config_file);
    
    fclose(config_file);
    cJSON_Delete(config_json);
    free(json_str);
    
    return true;
}

int main(int argc, char **argv)
{
    char active_code[20] = "";

    g_ui_upload_enable = 1;
    g_audio_upload_enable = 1;
    g_audio_download_enable = 1;
    g_audio_disabled_while_speaking = 0;

    leds_init();

    // 获取无线网卡的 MAC 地址
    char *mac;
    
    while (1)
    {
        printf("to get MAC ...\n");
        mac = get_wireless_mac_address();
        if (!mac || !strcmp(mac, "00:00:00:00:00:00"))
            sleep(1);
        else
            break;
    }
    printf("MAC: %s\n", mac);

    // 读取配置文件中的 UUID
    char *uuid = read_uuid_from_config();
    if (!uuid) {
        fprintf(stderr, "UUID not found in " CFG_FILE "\n");
        // 生成新的 UUID
        uuid = generate_uuid();
        printf("Generated new UUID: %s\n", uuid);

        // 将新的 UUID 写入配置文件
        if (!write_uuid_to_config(uuid)) {
            fprintf(stderr, "Failed to write UUID to " CFG_FILE "\n");
        } else {
            printf("UUID written to " CFG_FILE "\n");
        }
    } else {
        printf("UUID from " CFG_FILE ": %s\n", uuid);
    }    

    g_ipc_ep_audio = ipc_endpoint_create_udp(AUDIO_PORT_UP, AUDIO_PORT_DOWN, process_opus_data_uploaded, NULL);
    g_ipc_ep_ui = ipc_endpoint_create_udp(UI_PORT_UP, UI_PORT_DOWN, process_ui_data, NULL);
    g_ipc_ep_button = ipc_endpoint_create_udp(BUTTON_PORT_UP, 0, on_button_msg, NULL);
    if (!g_ipc_ep_audio || !g_ipc_ep_ui || !g_ipc_ep_button)
    {
        printf("Failed to create IPC endpoints, %p,%p,%p\n", g_ipc_ep_audio, g_ipc_ep_ui, g_ipc_ep_button);
        return -1;
    }

    http_data_t http_data;
    http_data.url = "https://api.tenclass.net/xiaozhi/ota/";

    // 构造 http_data.post
    char post_buffer[512];
    snprintf(post_buffer, sizeof(post_buffer), 
        "{\"uuid\":\"%s\",\"application\":{\"name\":\"xiaozhi_linux_100ask\",\"version\":\"1.0.0\"},\"ota\":{},\"board\":{\"type\":\"100ask_openvela_board\",\"name\":\"100ask_t113s3_board\"}}", 
        uuid);
    http_data.post = post_buffer;

    // 构造 http_data.headers
    char headers_buffer[512];
    snprintf(headers_buffer, sizeof(headers_buffer),
        "{\"Content-Type\":\"application/json\",\"Device-Id\":\"%s\",\"User-Agent\":\"weidongshan1\",\"Accept-Language\":\"zh-CN\"}",
        mac);
    http_data.headers = headers_buffer;

    while (0 != active_device(&http_data, active_code)) {
        if (active_code[0]) {
            char auth_code[64];
            snprintf(auth_code, sizeof(auth_code), "Active-Code: %s", active_code);
            set_device_state(kDeviceStateActivating);
            send_device_state();
            send_stt(auth_code);
        }
        sleep(5);
    }

    set_device_state(kDeviceStateIdle);
    send_device_state();
    send_stt("设备已经激活");

    websocket_data_t ws_data;

    // 构造 ws_data.headers
    char ws_headers_buffer[512];
    snprintf(ws_headers_buffer, sizeof(ws_headers_buffer),
        "{\"Authorization\":\"Bearer test-token\",\"Protocol-Version\":\"1\",\"Device-Id\":\"%s\",\"Client-Id\":\"%s\"}",
        mac, uuid);
    ws_data.headers = ws_headers_buffer;

    ws_data.hello = "{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\",\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}";
    ws_data.hostname = "api.tenclass.net";
    ws_data.port = "443";
    ws_data.path = "/xiaozhi/v1/";    

    websocket_set_callbacks(process_opus_data_downloaded, process_txt_data_downloaded, &ws_data);
    websocket_start();

    while (1)
    {
        sleep(1);
    }

    // 清理资源
    free(mac);
    free(uuid);
    
    return 0;
}
