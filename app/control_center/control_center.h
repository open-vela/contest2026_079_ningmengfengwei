#ifndef CONTROL_CENTER_H
#define CONTROL_CENTER_H

#include <stdint.h>
#include <stdbool.h>

/* 小智服务器与协议 */
#define XIAOZHI_WS_HOST       "api.xiaozhi.me"
#define XIAOZHI_WS_PATH       "/v1/chat/completions"
#define XIAOZHI_WS_PORT       443
#define XIAOZHI_ACTIVATE_URL  "https://api.xiaozhi.me/v1/device/activate"
#define XIAOZHI_PROTO_VER     3

/* opus 参数（对齐小智 hello audio_params）*/
#define OPUS_SAMPLE_RATE      16000
#define OPUS_CHANNELS         1
#define OPUS_FRAME_MS         60
#define OPUS_FRAME_SIZE       (OPUS_SAMPLE_RATE / 1000 * OPUS_FRAME_MS)  /* 960 samples */

/* 二进制音频帧 type（小智 BinaryProtocol3）*/
#define XZ_BIN_TYPE_OPUS      0
#define XZ_BIN_TYPE_JSON      1

/* 小智 version3 二进制帧头（4 字节，紧 packed）*/
#pragma pack(push, 1)
typedef struct {
    uint8_t  type;           /* 0=OPUS, 1=JSON */
    uint8_t  reserved;
    uint16_t payload_size;   /* 网络字节序 */
    uint8_t  payload[];
} binary_proto3_t;
#pragma pack(pop)

#endif /* CONTROL_CENTER_H */
