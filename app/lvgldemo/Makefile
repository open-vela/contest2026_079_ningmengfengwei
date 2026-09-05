# apps/examples/lvgldemo/Makefile
# 修复：APPNAME → PROGNAME（NuttX Application.mk 用 PROGNAME 做 main 重命名）
# 修复：include Make.defs 移至 PROGNAME 之前（确保 Application.mk 能读到 PROGNAME）

include $(APPDIR)/Make.defs

PROGNAME = lvgldemo
PRIORITY = SCHED_PRIORITY_DEFAULT
STACKSIZE = $(CONFIG_EXAMPLES_LVGLDEMO_STACKSIZE)

MODULE = $(CONFIG_EXAMPLES_LVGLDEMO)

CFLAGS += -I$(APPDIR)/examples/control_center

# 入口主文件必须走 MAINSRC（Application.mk: MAINCSRCS=$(filter %.c,$(MAINSRC))，
# 只有 MAINSRC 里的文件才会被编译时打上 -Dmain=lvgldemo_main 重命名）。
# 辅助源文件留 CSRCS。
MAINSRC = lvgldemo.c
CSRCS += lv_100ask_xz_ai_main.c ui_main.c

include $(APPDIR)/Application.mk
