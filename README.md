# VelaAI 桌面语音助手

## 一、作品简介

VelaAI 桌面宠物是一款基于 openvela（NuttX RTOS）+ 全志 T113S3 的智能语音桌面机器人。本项目将小智 AI 桌面机器人从 ESP32 平台移植到 openvela 系统，实现了语音交互、WiFi 联网、WebSocket 实时通信、LVGL GUI 显示等核心功能。

**核心亮点：**

1. 在 openvela 上完整集成 libwebsockets + TLS/SSL，实现与小智服务器的安全通信
2. 通过 POSIX 消息队列实现跨进程 IPC 通信，规避 NuttX 网络栈对齐 bug
3. 48kHz/2ch 高清音频采集与 Opus 编解码，对齐 DMIC 硬件原生参数
4. LVGL 图形界面，支持表情显示、语音文字展示、WiFi 配网

**团队分工（柠檬风未队）：**

| 成员 | 职责 |
| --- | --- |
| 王奥迪 | libwebsockets/TLS 通信模块开发 |
| 王晓婵 | NuttX 网络栈适配与 bug 修复 |
| 舒政博 | 项目架构设计与系统集成 |
| 邢梦凡 | 音频驱动适配与 Opus 编解码集成 |
| 刘怡蕊 | LVGL GUI 界面开发与交互设计 |

---

## 二、选题方向

**AI 硬件产品创新 + 新硬件平台适配**

将成熟的 ESP32 AI 桌面机器人方案移植到更高算力的 openvela/NuttX 平台，验证 openvela 在嵌入式 AI 场景下的可行性与优势，探索 RTOS 在智能语音交互产品中的应用。

---

## 三、目录结构

```
contest2026_079_ningmengfengwei/
├── app/
│   ├── control_center/      # 主控应用：HTTP激活 + WebSocket通信 + IPC分发
│   │   ├── control_center.c/h   # 主程序入口与业务逻辑
│   │   ├── websocket_client.c/h # WebSocket + TLS 通信封装
│   │   ├── http.c/h             # HTTP 设备激活（libcurl）
│   │   ├── uuid.c/h             # 设备 UUID/MAC 生成
│   │   ├── leds.c/h             # LED GPIO 控制
│   │   ├── ipc_udp.c/h          # POSIX 消息队列 IPC
│   │   ├── cfg.h                # 端口与配置常量
│   │   ├── Makefile / Make.defs # 构建文件
│   │   └── Kconfig             # 配置选项
│   ├── button_led/          # 按键 + LED 控制应用
│   └── lvgldemo/            # LVGL GUI 应用（表情/文字/配网面板）
├── board/                  # 板级适配
├── docs/                   # 文档
├── logs/                   # AI Coding 日志
├── quickapp/               # 快应用
├── contest2026_079_ningmengfengwei.xml  # repo manifest（含 linkfile 映射）
└── README.md               # 本文件
```

---

## 四、运行方式

### 1. 拉取工程

```bash
repo init -u https://github.com/open-vela/contest2026_079_ningmengfengwei \
  -b dev-ai-contest-2026 -m contest2026_079_ningmengfengwei.xml
repo sync -c -j8
```

### 2. 编译

```bash
# 进入 openvela 工作区根目录（仓的上一级）
cd ..

# 配置（以 R528S3 为例）
./build.sh vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh menuconfig

# 启用应用选项（menuconfig 中勾选）：
#   CONFIG_EXAMPLES_CONTROL_CENTER=y
#   CONFIG_EXAMPLES_LVGLDEMO=y
#   CONFIG_EXAMPLES_BUTTON_LED=y
#   CONFIG_NETUTILS_LIBWEBSOCKETS=y
#   CONFIG_GRAPHICS_LVGL=y

# 编译
./build.sh vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh -j8
```

### 3. 烧录

使用 PhoenixSuit 烧录生成的 `.img` 文件到 T113S3 开发板。

### 4. 运行

烧录后通过串口连接（波特率 115200），在 NSH 终端执行：

```bash
# WiFi 连接
wapi mode wlan0 2
wapi psk wlan0 <password> 3
wapi essid wlan0 <ssid> 1
ifconfig wlan0 <ip>

# 启动应用（rcS 已配置开机自启）
control_center &
arecord -D hw:snddmic -r 48000 -f 16 -c 2 -o &
aplay -D hw:audiocodec -r 48000 -f 16 -c 2 -o &
lvgldemo &
```

### 5. 关键配置（defconfig）

```
CONFIG_NETUTILS_LIBWEBSOCKETS=y
CONFIG_EXAMPLES_CONTROL_CENTER=y
CONFIG_EXAMPLES_CONTROL_CENTER_PROGNAME="control_center"
CONFIG_EXAMPLES_CONTROL_CENTER_PRIORITY=100
CONFIG_EXAMPLES_CONTROL_CENTER_STACKSIZE=40960
CONFIG_EXAMPLES_LVGLDEMO=y
CONFIG_EXAMPLES_BUTTON_LED=y
CONFIG_GRAPHICS_LVGL=y
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_48=y
CONFIG_NET_ROUTE=y
CONFIG_NETINIT_DHCPC=y
CONFIG_MQ_MAXMSGSIZE=1600
```

---

## 五、AI Coding 使用说明

本项目借助 AI 辅助开发，覆盖以下环节：

| 环节 | AI 协作内容 |
| --- | --- |
| 代码分析 | 快速定位 NuttX 网络栈内存对齐 bug 根因 |
| 编译调试 | 分析 libwebsockets Kconfig 依赖关系，解决 SSL 符号冲突 |
| 代码生成 | 生成 IPC 消息队列、WebSocket 回调等模块代码 |
| 文档编写 | 自动生成技术报告、README、源码注释 |

**AI 工具：** Claude Code（代码分析、调试建议、文档生成）

**AI Coding 代码占比：** 约 30%

**关键问题与 AI 辅助解决：**

1. libwebsockets 编译失败 → AI 分析 Kconfig 依赖，定位 `OPENSSL_MBEDTLS_WRAPPER` 问题
2. ssl.h 头文件冲突 → AI 建议复制 bundled wrapper 的 include 内容
3. NuttX 网络栈崩溃 → AI 分析 DFSR 寄存器值，定位到内存对齐错误

完整对话日志见 `logs/` 目录。

---

## 六、已验证功能

| 功能 | 状态 | 说明 |
| --- | --- | --- |
| WiFi 连接 | ✅ | WPA2 握手成功，信号 -54dBm |
| HTTP 激活 | ✅ | 激活码 880122 获取成功 |
| WebSocket 连接 | ✅ | wss://api.tenclass.net:443 TLS 握手成功 |
| Hello 握手 | ✅ | sample_rate 24000 协商成功 |
| IoT 设备注册 | ✅ | LED1/LED2 描述符发送成功 |
| LVGL GUI | ✅ | 320x480 显示正常，触摸响应 |
| 录音/播放 | ⚠️ | 参数正确（48kHz/2ch），arecord 运行时崩溃待修复 |

---

## 七、已知问题

1. **arecord 运行时崩溃**：NuttX 音频驱动层 bug，运行时内存损坏，待修复
2. 配网流程未通过 LVGL GUI 实现（当前通过串口命令配置）
3. 缺少端侧 AI 模型（如 VAD 语音活动检测）
