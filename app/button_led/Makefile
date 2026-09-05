# apps/examples/button_led/Makefile

# 修复：APPNAME → PROGNAME（Application.mk 用 PROGNAME 做 main 重命名）
# 修复：include Make.defs 移至顶部
# 修复：入口主文件走 MAINSRC（而非 CSRCS），才能触发 -Dmain=button_led_main

include $(APPDIR)/Make.defs

PROGNAME = button_led
PRIORITY = SCHED_PRIORITY_DEFAULT
STACKSIZE = 8192

MODULE = $(CONFIG_EXAMPLES_BUTTON_LED)

CFLAGS += -I$(APPDIR)/examples/control_center

MAINSRC = button_led.c

include $(APPDIR)/Application.mk
