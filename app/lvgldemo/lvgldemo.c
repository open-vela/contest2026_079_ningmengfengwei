/*
 * lvgldemo.c - openvela 桌宠 GUI 入口
 *
 * 仅调用 lv_100ask_xz_ai_main()，保持与 PDF 9.4 一致的入口结构。
 * display/touch 初始化由 NuttX LVGL 集成(CONFIG_LV_USE_NUTTX)自动处理。
 */

#include <stdio.h>

extern int lv_100ask_xz_ai_main(int argc, char **argv);

int main(int argc, char **argv)
{
    return lv_100ask_xz_ai_main(argc, argv);
}
