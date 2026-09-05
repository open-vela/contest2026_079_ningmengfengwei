# openvela 桌宠 - 阶段1 Ubuntu 现有文件修改说明

本文件说明在 Ubuntu `~/vela-opensource` 源码树上对**现有文件**做的修改。
按用户规则：**先备份原件（cp xx xx.bak），再修改，确认编译/运行可行后才定稿。**

工作区 `openvela/` 下的**新建文件**（control\_center/、xiaozhi.sh、get\_wifi.sh）直接拷到对应路径即可，无需备份。

***

## 修改 1：apps/examples/Kconfig（注册 control\_center）

**路径**：`apps/examples/Kconfig`
**操作**：在文件末尾追加一行

```
source "$APPS/examples/control_center/Kconfig"
```

**原因**：让 menuconfig 能看到 CONFIG\_EXAMPLES\_CONTROL\_CENTER 选项。

***

## 修改 2：defconfig（启用阶段1配置项）

**路径**：`vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/defconfig`
**操作**：追加以下配置项（已有则跳过）

```
# HTTP 激活(libcurl)
CONFIG_EXAMPLES_HTTP=y
CONFIG_LIB_CURL=y
CONFIG_DEV_URANDOM=y
CONFIG_DEV_URANDOM_ARCH=y

# WebSocket(libwebsockets)
CONFIG_EXAMPLES_WEBSOCKET_TEST=y
CONFIG_NETUTILS_CJSON=y
CONFIG_OPENSSL_MBEDTLS_WRAPPER=y
CONFIG_NETUTILS_LIBWEBSOCKETS=y
CONFIG_CRYPTO_MBEDTLS=y
CONFIG_MBEDTLS_NET_C=y
CONFIG_NET_TCPPROTO_OPTIONS=y

# 控制中心
CONFIG_EXAMPLES_CONTROL_CENTER=y
CONFIG_EXAMPLES_CONTROL_CENTER_STACKSIZE=204800
CONFIG_NET_UDP_MAX_CONNS=0
```

**原因**：激活/libcurl/websocket/control\_center 依赖。URANDOM 不加会卡死。

***

## 修改 3：rcS（启动脚本入口）

**路径**：`vendor/allwinnertech/boards/r528/r528s3-velaevb1/src/etc/init.d/rcS`
**操作**：在文件末尾追加

```sh
sh /data/xiaozhi.sh &
```

**原因**：开机自动启动桌宠各任务。

***

## 修改 4：libwebsockets Makefile（补头文件与 CSRCS）

**路径**：`apps/netutils/libwebsockets/Makefile`
**操作**：参考 PDF 9.3.2，补 CFLAGS 头文件路径与 CSRCS：

```makefile
# 补 mbedtls wrapper 头文件路径（按实际目录调整）
CFLAGS += -I$(NETUTILS_DIR)/libwebsockets/core/plat/event-libs
CFLAGS += -I$(NETUTILS_DIR)/libwebsockets/core/plat/roles/tls/mbedtls/wrapper

# 补 TLS 相关源文件
CSRCS += tls.c tls-network.c mbedtls-openssl-wrapper.c mbedtls-x509.c
```

**原因**：libwebsockets 用 mbedtls 做 TLS，需补 wrapper 源文件与头文件，否则链接报错。

***

## 修改 5：libwebsockets 补丁（加缺失函数）

按 PDF 9.3.2，以下两个文件需加缺失函数：

**5a. ssl\_lib.c**：加 `SSL_want_write` 函数（libwebsockets 调用但 mbedtls wrapper 未实现）
**5b. err.c**：加 `ERR_error_string` 函数

**操作**：在对应文件中按 mbedtls wrapper 的命名风格补这两个函数的桩实现。具体签名参考 libwebsockets 对它们的调用点（grep `SSL_want_write` / `ERR_error_string` 定位）。

***

## 编译验证流程

```bash
cd ~/vela-opensource/vendor/allwinnertech/lichee/
source vela_env.sh
source envsetup.sh
lunch_nuttx        # 选 2 r528s3-velaevb1
m c                 # 清除
m                   # 编译（首次需先 m menuconfig 确认配置生效）
pack                # 打包
```

阶段1验证标准：烧录后串口（MobaXterm 1500000 流控 none）见到：

```
control_center: starting
control_center: device_id=xx:xx:xx:xx:xx:xx client_id=openvela-xx...
control_center: use cached token  (或 activate 后)
control_center: ws connected, sending hello
control_center: server hello received
```

服务器端（xiaozhi.me 控制台）见设备上线。

***

# 阶段2 修改：语音收发（arecord/aplay + opus）

## 修改 6：替换 arecord.c / aplay.c（先备份原件）

**路径**：`vendor/allwinnertech/chips/r528/drivers/rtos-hal/hal/test/sound/`
**操作**：

```bash
cd vendor/allwinnertech/chips/r528/drivers/rtos-hal/hal/test/sound/
cp arecord.c arecord.c.bak      # 备份原件
cp aplay.c aplay.c.bak          # 备份原件
# 用工作区 openvela/vendor/.../sound/ 下的改造版替换
cp <工作区路径>/openvela/vendor/allwinnertech/chips/r528/drivers/rtos-hal/hal/test/sound/arecord.c .
cp <工作区路径>/openvela/vendor/allwinnertech/chips/r528/drivers/rtos-hal/hal/test/sound/aplay.c .
```

**改动说明**：

- arecord.c 新增 `-o` opus 模式：ALSA 采集 16000Hz 单声道 → `opus_encode` → IPC 发 `AUDIO_PORT_UP` 给 control\_center

- aplay.c 新增 `-o` opus 模式：IPC 收 `AUDIO_PORT_DOWN` → `opus_decode` → ALSA 播放

- 两者 include 了 `ipc_udp.h` / `control_center.h`（来自 `apps/examples/control_center/`）

## 修改 7：sound 目录 Makefile（加 include 路径与 opus 依赖）

**路径**：`vendor/allwinnertech/chips/r528/drivers/rtos-hal/hal/test/sound/Makefile`
**操作**：追加

```makefile
# 让 arecord.c/aplay.c 能 include ipc_udp.h / control_center.h
CFLAGS += -I$(APPDIR)/examples/control_center

# opus 头文件路径（按实际 external/opus 目录调整）
CFLAGS += -I$(TOPDIR)/../external/opus/opus/include
```

**原因**：arecord.c/aplay.c 现依赖 control\_center 目录的 IPC 头和 opus 头。

## 修改 8：defconfig 追加 opus 配置

**路径**：`vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/defconfig`
**操作**：在阶段1配置项后追加

```
# opus 编解码（阶段2）
CONFIG_LIB_OPUS=y
```

## 阶段2验证标准

烧录后串口见到：

```
arecord: recording rate=16000 ch=1 opus period=960
aplay: playing rate=16000 ch=1 opus period=960
control_center: server hello received
```

按键触发后（阶段4加按键，阶段2可临时持续上传）：对设备说话 → 服务器处理 → 收到 TTS opus → 扬声器播放回复语音。

***

# 阶段3 修改：GUI 显示（lvgldemo + LVGL）

## 修改 9：apps/examples/Kconfig（注册 lvgldemo）

**路径**：`apps/examples/Kconfig`
**操作**：在 control\_center 的 source 之后追加

```
source "$APPS/examples/lvgldemo/Kconfig"
```

## 修改 10：defconfig 追加 LVGL 配置

**路径**：`vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/defconfig`
**操作**：追加

```
# GUI(阶段3)
CONFIG_EXAMPLES_LVGLDEMO=y
CONFIG_EXAMPLES_LVGLDEMO_STACKSIZE=204800
CONFIG_LV_USE_NUTTX=y
CONFIG_LV_USE_NUTTX_TOUCHSCREEN=y
CONFIG_LV_FONT_MONTSERRAT_28=y
```

**触摸中断**：**不要**配置 `CONFIG_FT5X06_POLLMODE`（默认即中断模式，避免轮询开销）。若中断模式下数据丢失，触摸驱动再加队列。

## 阶段3验证标准

烧录后 LCD 显示：

- 顶部表情 `(o_o)`

- 中部 `...`（用户语音占位）

- 下部 `...`（回复占位）
  对设备说话后 LCD 中部显示用户语音文字(stt)、表情变化(llm)、下部显示回复文本(tts)。
  串口见 `lvgldemo: starting` + `lvgldemo: running`。

***

# 阶段4 修改：触摸配网 + LED + 按键触发

阶段4 把桌宠从"被动响应"升级为"按键触发录音 + 触摸配网"，对齐 xiaozhi-esp32 的唤醒词 + 配网流程。所有改动**先备份 .bak 再改**，与阶段1-3规则一致。

## 新增文件（工作区 openvela/，直接拷到 Ubuntu 对应路径）

```
openvela/apps/examples/button_led/
  ├── Kconfig        # CONFIG_EXAMPLES_BUTTON_LED，depends on CONTROL_CENTER
  ├── Make.defs      # include control_center 目录（ipc_udp.h）
  ├── Makefile       # APPNAME=button_led, STACKSIZE=8192
  ├── button_led.h   # GPIO 路径 + ACTIVE_LOW 配置
  └── button_led.c   # K1 轮询 + 防抖 + IPC 上报 record_start/stop + LED 控制
```

## 修改 11：apps/examples/Kconfig（注册 button\_led）

**路径**：`apps/examples/Kconfig`
**操作**：在 lvgldemo 的 source 之后追加

```
source "$APPS/examples/button_led/Kconfig"
```

## 修改 12：defconfig 追加阶段4配置

**路径**：`vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nosh/defconfig`
**操作**：追加

```
# 阶段4：按键 + LED 任务
CONFIG_EXAMPLES_BUTTON_LED=y

# LVGL 控件（配网面板用 list/textara/btn）
CONFIG_LV_USE_LIST=y
CONFIG_LV_USE_TEXTAREA=y
CONFIG_LV_USE_BTN=y
```

**原因**：ui\_main.c 的 WiFi 配网面板用到了 `lv_list_create` / `lv_textarea_create` / `lv_btn_create`，需在 menuconfig 中启用对应控件。

## 修改 13：GPIO export（rcS 启动脚本）

**路径**：`vendor/allwinnertech/boards/r528/r528s3-velaevb1/src/etc/init.d/rcS`
**操作**：在 `sh /data/xiaozhi.sh &` 之前追加 GPIO export（按板子原理图调整 gpio 编号）

```sh
# K1 按键 + LED GPIO export 到 sysfs（阶段4）
echo <K1_GPIO_NUM> > /sys/class/gpio/export   2>/dev/null
echo <LED_GPIO_NUM> > /sys/class/gpio/export  2>/dev/null
echo in  > /sys/class/gpio/gpio<K1_GPIO_NUM>/direction
echo out > /sys/class/gpio/gpio<LED_GPIO_NUM>/direction
```

**说明**：button\_led.c 通过 `/sys/class/gpio/gpioXX/value` 读写，需先 export。若改用 NuttX GPIO driver ioctl，此步可省。

## 修改 14：control\_center.c 扩展（按键门控 + 配网请求处理）

**路径**：`apps/examples/control_center/control_center.c`
**改动**（与 .bak 对比）：

1. 顶部注释：职责第5点补"阶段4：仅当 button\_led 通知 g\_recording=true 时才上行转发"
2. 新增 IPC 端点：

   - `g_ep_button`：绑定 `BUTTON_PORT_UP`(8005) 收 button\_led 按键消息

   - `g_ep_ui_up`：绑定 `UI_PORT_UP`(8004) 收 lvgldemo 触摸/配网请求
3. 新增回调：

   - `on_button_msg`：解析 `{"type":"record_start"/"record_stop"}`，更新 `g_recording`，停止时清 `g_has_pending` 残留帧

   - `on_ui_msg`：解析 `wifi_scan` / `wifi_connect`，**先打桩**（printf + 回复空列表 / disconnected），真机调试时再补 wlantool 调用
4. `on_audio_from_arecord`：开头加 `if (!g_recording) return;`，按键松开后丢弃 arecord 上报
5. main：创建 `g_ep_button` / `g_ep_ui_up`，清理时销毁

## 修改 15：ipc\_udp.h 新增 BUTTON\_PORT\_UP

**路径**：`apps/examples/control_center/ipc_udp.h`
**操作**：在 `UI_PORT_UP` 后追加

```
#define BUTTON_PORT_UP   8005  /* button_led -> control_center (按键录音状态上报) */
```

**原因**：button\_led 若复用 `UI_PORT_UP`(8004) 会和 lvgldemo 触摸上报抢同一本地端口（UDP 一个端口只能 bind 一个 socket）。新增 8005 专用端口隔离按键链与触摸链。

## 修改 16：button\_led.c 端口修正

**路径**：`apps/examples/button_led/button_led.c`
**改动**：

- 头部注释 `UI_PORT_UP` → `BUTTON_PORT_UP`

- `ipc_endpoint_create_udp(0, UI_PORT_UP, ...)` → `ipc_endpoint_create_udp(0, BUTTON_PORT_UP, ...)`

## 修改 17：ui\_main.c/.h 扩展 WiFi 配网界面

**路径**：`apps/examples/lvgldemo/ui_main.h` + `ui_main.c`
**新增 API**：

```c
lv_obj_t *ui_create_wifi_bar(lv_obj_t *parent);                           /* 右上角状态图标 */
void ui_create_wifi_panel(lv_obj_t *parent, lv_obj_t *bar, ...);         /* 配网面板（默认隐藏）*/
void ui_wifi_set_status(lv_obj_t *bar, ui_wifi_state_t st);              /* 状态颜色更新 */
void ui_wifi_fill_list(lv_obj_t *list, const char *aps_json);            /* 扫描结果填充 */
```

**交互流程**：

1. 点击右上角 WiFi 图标 → 弹出面板 + 触发 `cb_scan` → IPC 发 `{"type":"wifi_scan"}` 给 control\_center
2. control\_center（打桩）回 `{"type":"wifi_list","aps":[...]}` → UI 填列表
3. 点列表项记下 SSID → 密码框输入 → 点 Connect → 触发 `cb_connect` → IPC 发 `{"type":"wifi_connect","ssid":"...","pwd":"..."}` → 隐藏面板
4. control\_center（打桩）回 `{"type":"wifi_status","state":"connected"/"disconnected"}` → UI 更新图标颜色

## 修改 18：lv\_100ask\_xz\_ai\_main.c 集成 WiFi 流程

**路径**：`apps/examples/lvgldemo/lv_100ask_xz_ai_main.c`
**改动**：

- 顶部注释职责补"WiFi 配网"

- 新增全局控件：`g_wifi_bar` / `g_wifi_panel` / `g_wifi_list` / `g_wifi_pwd`

- 新增回调 `cb_wifi_scan` / `cb_wifi_connect`：组 JSON 通过 IPC 上报 control\_center

- `update_ui_from_json` 新增 `wifi_list` / `wifi_status` 两条分支

- main 中调用 `ui_create_wifi_bar` + `ui_create_wifi_panel`，启动时设默认 `DISCONNECTED`

## 阶段4验证标准

**烧录后串口见**：

```
button_led: running
control_center: recording ON     ← 按下 K1
control_center: recording OFF    ← 松开 K1
lvgldemo: -> control_center wifi_scan    ← 点击 WiFi 图标
control_center: [stub] wifi_scan requested
control_center: [stub] wifi_connect ssid=XXX pwd_len=8   ← 选网输密码
```

**LCD 显示**：

- 右上角 WiFi 图标：红 `WiFi:#`（未连）→ 黄 `WiFi:?`（扫描中）→ 绿 `WiFi:*`（已连）

- 点击图标弹出配网面板，含扫描列表 + 密码框 + Connect 按钮

- 按下 K1 时 LED 亮，松开灭；按住期间对着麦克风说话 → 服务器处理后扬声器播放回复

## 已知 TODO（真机调试阶段补）

- `on_ui_msg` 的 `wifi_scan` / `wifi_connect` 现为打桩，需补：

  - `wlantool scan` 解析 → 下发真实 wifi\_list JSON

  - `wlantool join <ssid> <pwd>` → 下发真实 wifi\_status

- `ui_wifi_fill_list` 清空列表时 user\_data 上 malloc 的 SSID 字符串会泄漏，阶段4 简化处理，量产前需 free

- GPIO 路径 `gpio_PJ0/PJ1` 为占位，需按 T113S3 原理图改为真实 gpio 编号

***

# 阶段4b：修正 button\_led GPIO 映射（根因 + 验证）

> 对 finalgoal 对齐：本修正对标 xiaozhi-esp32 的 **WakeWord 唤醒触发 + 录音状态灯**（K1 物理按键替代唤醒词，LED 作录音指示）。已实机验证通过 ✅。

## 现象

原代码 K1 全无反应：按下/松开都无 `record start/stop`，LED 不亮。

## 根因（两处错）

1. **节点号错**：原值 `BUTTON_GPIO_PATH="/dev/gpio3"`，实际板上按键在 `/dev/gpio1`。
2. **极性反**：原 `BUTTON_ACTIVE_LOW=1`（低有效按下读0），且 `gpio_init_direction()` 把按键设为 `PULLUP`。

> 判据来自 openvela 官方 LED 任务示例（`3_程序源码/source/3-3-3_创建LED任务/led/led.c`）：`/dev/gpio0`=LED（OUTPUT）、`/dev/gpio1`=Button（`GPIO_INPUT_PIN_PULLDOWN`，`invalue==1` 判按下 = 高有效）。

## 修复（已备份 .bak\_gpiofix\_pre 后改）

- `button_led.h`：`BUTTON_GPIO_PATH` `gpio3`→`gpio1`；`BUTTON_ACTIVE_LOW` `1`→`0`（高有效，按下读1）。

- `button_led.c`：

  - `gpio_init_direction()` 按键组态 `PULLUP`→`PULLDOWN`（对齐官方）。

  - `read_button()` 打开模式 `O_RDONLY`→`O_RDWR`（对齐官方）。

- LED 保持 `/dev/gpio0`（与官方示例一致，未改动）。

## 实机验证（2026-09-05）

```
button_led: running
button_led: record start   ← 按下 K1
button_led: record stop    ← 松开 K1
（反复触发正常，LED 随按键亮灭）
```

## 经验

- 板端 `ls /dev/gpio*` 报 `stat failed` 是 NSH 不支持 glob，不代表节点不存在；应 `ls /dev` 直接看。

- 内部通道是否可达，用官方针对本板的示例源码核对节点&极性最可靠，优先于原理图推断。

