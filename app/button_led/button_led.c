/*
 * button_led.c - openvela 桌宠按键与 LED 任务（阶段4）
 *
 * 职责：
 *   1. 轮询 K1 按键（/dev/gpioN ioctl，含简单防抖）
 *   2. 按下 → IPC(BUTTON_PORT_UP) 发 record_start JSON → control_center 开始转发 arecord 到 ws
 *      松开 → 发 record_stop → control_center 停止转发
 *   3. LED 指示：按下(录音中)=亮，松开(空闲)=灭
 *
 * 对齐：xiaozhi-esp32 唤醒词触发（T113 用 K1 物理按键替代）+ LED 状态
 *
 * R528/T113 用 NuttX 字符设备 ioctl（不是 Linux sysfs）：
 *   - GPIOC_SETPINTYPE 配方向（输入上拉 / 输出）
 *   - GPIOC_READ       读电平（arg = bool*）
 *   - GPIOC_WRITE      写电平（arg = 0/1）
 * GPIO 设备节点在 button_led.h，按 ls /dev/gpio* + 原理图调整。
 */

#include "button_led.h"
#include "ipc_udp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <nuttx/ioexpander/gpio.h>

#define BL_TAG "button_led"

/* 初始化 GPIO 方向（读不到/不支持时忽略错误，让板级默认生效）*/
static void gpio_init_direction(void)
{
    int bfd = open(BUTTON_GPIO_PATH, O_RDWR);
    if (bfd >= 0) {
        /* 按键组态对齐官方示例：PULLDOWN，按下读到1（高有效）*/
        ioctl(bfd, GPIOC_SETPINTYPE, (unsigned long)GPIO_INPUT_PIN_PULLDOWN);
        close(bfd);
    }
    int lfd = open(LED_GPIO_PATH, O_RDWR);
    if (lfd >= 0) {
        ioctl(lfd, GPIOC_SETPINTYPE, (unsigned long)GPIO_OUTPUT_PIN);
        close(lfd);
    }
}

static int read_button(void)
{
    int fd = open(BUTTON_GPIO_PATH, O_RDWR);  /* 官方示例用 O_RDWR 后 ioctl READ */
    if (fd < 0) {
        return -1;  /* 读不到按"未按下"处理 */
    }
    bool v = false;
    int rc = ioctl(fd, GPIOC_READ, (unsigned long)&v);
    close(fd);
    if (rc < 0) {
        return -1;
    }
    return v ? 1 : 0;
}

static void led_set(int on)
{
    int fd = open(LED_GPIO_PATH, O_RDWR);
    if (fd < 0) {
        return;
    }
    ioctl(fd, GPIOC_WRITE, (unsigned long)(on ? 1 : 0));
    close(fd);
}

/* 判定按键是否按下（处理高低电平有效）*/
static bool is_pressed(int raw)
{
    if (raw < 0) {
        return false;
    }
    return BUTTON_ACTIVE_LOW ? (raw == 0) : (raw != 0);
}

int main(int argc, char **argv)
{
    p_ipc_endpoint_t ep = ipc_endpoint_create_udp(0, BUTTON_PORT_UP, NULL, NULL);
    if (!ep) {
        printf(BL_TAG ": ipc create failed\n");
        return 1;
    }

    const char *start_msg = "{\"type\":\"record_start\"}";
    const char *stop_msg  = "{\"type\":\"record_stop\"}";

    bool was_pressed = false;
    bool confirmed_pressed = false;
    int debounce = 0;

    printf(BL_TAG ": running\n");
    gpio_init_direction();
    led_set(0);

    while (1) {
        bool pressed = is_pressed(read_button());

        /* 简单防抖：连续 3 次相同状态才确认 */
        if (pressed == was_pressed) {
            debounce = 0;
        } else {
            debounce++;
            if (debounce >= 3) {
                was_pressed = pressed;
                debounce = 0;
            }
        }

        if (was_pressed != confirmed_pressed) {
            confirmed_pressed = was_pressed;
            if (confirmed_pressed) {
                ep->send(ep, start_msg, (int)strlen(start_msg));
                led_set(1);
                printf(BL_TAG ": record start\n");
            } else {
                ep->send(ep, stop_msg, (int)strlen(stop_msg));
                led_set(0);
                printf(BL_TAG ": record stop\n");
            }
        }
        usleep(20000);  /* 20ms 轮询 */
    }

    ipc_endpoint_destroy_udp(ep);
    return 0;
}
