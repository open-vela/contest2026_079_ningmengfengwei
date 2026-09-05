#ifndef BUTTON_LED_H
#define BUTTON_LED_H

/*
 * 按键(K1) 与 LED 的 GPIO 设备节点。
 * R528/T113 的 GPIO 是字符设备 + ioctl（不是 Linux sysfs），
 * 节点为 /dev/gpio0~N（见 include/nuttx/ioexpander/gpio.h）。
 *
 * 节点选用对齐 openvela 官方 LED 任务示例（本板 T113S3 源码
 *   3_程序源码/source/3-3-3_创建LED任务/led/led.c）：
 *   /dev/gpio0 = LED     （设 GPIO_OUTPUT_PIN）
 *   /dev/gpio1 = Button  （设 GPIO_INPUT_PIN_PULLDOWN，读到1=按下，高有效）
 * 板端 ls /dev 已确认 gpio0~gpio5 节点都存在。
 *
 * 说明：drv_gpio.c maps[] 中 gpio1 标 KEY1(PD7)、gpio3 标 USER_KEY1(PD8)，
 * 但官方示例用 /dev/gpio1 作为按键，且 GPIO_INPUT_PIN_PULLDOWN 组态 +
 * invalue==1 判定按下（即高电平有效）。故按键用 gpio1、极性=高有效。
 * 按键组态必须在 button_led.c gpio_init_direction() 中同步设为 PULLDOWN。
 */
#define BUTTON_GPIO_PATH  "/dev/gpio1"   /* K1 按键 = 官方示例 Button (PD7) */
#define LED_GPIO_PATH     "/dev/gpio0"   /* LED = 官方示例 / USER_LED2 = PD21 */

#define BUTTON_ACTIVE_LOW  0   /* 0=高电平有效(按下读到1)，对齐官方示例 pulldown+invalue==1 */

#endif /* BUTTON_LED_H */
