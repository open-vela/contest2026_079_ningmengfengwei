# apps/examples/control_center/Makefile

# 修复：APPNAME → PROGNAME（Application.mk 用 PROGNAME 做 main 重命名）
# 修复：include Make.defs 移至顶部
# 修复：入口主文件走 MAINSRC（而非 CSRCS），才能触发 -Dmain=control_center_main

include $(APPDIR)/Make.defs

PROGNAME = control_center
PRIORITY = SCHED_PRIORITY_DEFAULT
STACKSIZE = $(CONFIG_EXAMPLES_CONTROL_CENTER_STACKSIZE)

MODULE = $(CONFIG_EXAMPLES_CONTROL_CENTER)

# cJSON include 路径：对齐参考 9-5_系统集成/control_center/Makefile，
# control_center.c 用 #include <cJSON.h> 与 cJSON_Parse 等 API。
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/netutils/cjson/cJSON
# opus include 路径：对齐参考 9-5_系统集成/sound/Makefile，
# control_center.c 用 #include <opus/opus.h> 做音频解码；libopus 由 CONFIG_LIB_OPUS=y 链接。
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../external/opus/opus/include

MAINSRC = control_center.c
CSRCS += ipc_udp.c

include $(APPDIR)/Application.mk
