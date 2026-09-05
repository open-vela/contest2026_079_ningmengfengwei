#ifndef IPC_UDP_H
#define IPC_UDP_H

#include <stdint.h>
#include <stdbool.h>

/* IPC 端口约定（openvela 桌宠各任务共用，本地回环 127.0.0.1）*/
#define AUDIO_PORT_UP    8001  /* arecord -> control_center (用户语音上传) */
#define AUDIO_PORT_DOWN  8002  /* control_center -> aplay (TTS 语音下发) */
#define UI_PORT_DOWN     8003  /* control_center -> lvgldemo (文字/表情下发) */
#define UI_PORT_UP       8004  /* lvgldemo -> control_center (触摸事件上报) */
#define BUTTON_PORT_UP   8005  /* button_led -> control_center (按键录音状态上报) */

#define IPC_MAX_PACKET   1600  /* 单包最大字节（opus 帧 + 二进制头够用）*/

/* 收到远端数据后的回调（由后台 recv 线程调用）*/
typedef void (*transfer_callback_t)(const char *data, int len, void *user_data);

typedef struct ipc_endpoint_t {
    void *priv;            /* 内部实现（socket fd 等）*/
    void *user_data;        /* 调用者上下文 */
    transfer_callback_t cb; /* 收到数据回调，非 NULL 时启动后台 recv 线程 */
    int (*send)(struct ipc_endpoint_t *self, const char *data, int len);
    int (*recv)(struct ipc_endpoint_t *self, unsigned char *data, int maxlen, int *retlen);
} ipc_endpoint_t, *p_ipc_endpoint_t;

/* 创建 UDP 本地回环 endpoint。
 * port_local : 本地绑定端口（仅发送方可填 0 = 任意端口）
 * port_remote: 对端端口（仅接收方可填 0）
 * cb         : 收到数据回调（非 NULL 时启动后台线程循环 recv）
 * user_data  : 透传给回调
 * 返回 NULL 失败。
 */
p_ipc_endpoint_t ipc_endpoint_create_udp(int port_local, int port_remote,
                                          transfer_callback_t cb, void *user_data);

/* 销毁 endpoint：停止后台线程、关闭 socket、释放内存 */
void ipc_endpoint_destroy_udp(p_ipc_endpoint_t pendpoint);

#endif /* IPC_UDP_H */
