#
# tools/tests/av/bench.mk -- avbench (tools/tests/av/avbench.c) with the media library and its
# codecs (user/Libs/av/codecs.mk), for the PC (AV_ARCH=generic, CC=gcc) or for AArch64 Linux
# (AV_ARCH=aarch64, CC=aarch64-linux-gnu-gcc: the Pi's NEON / assembly paths, run with qemu).
#
#   make -f tools/tests/av/bench.mk OUT=/tmp/avbench [AV_ARCH=aarch64 CC=aarch64-linux-gnu-gcc]
#
ROOT  := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../..)
TP    := $(ROOT)/third_party
OUT   ?= /tmp/avbench
CC    ?= gcc
AV_ARCH ?= generic
include $(ROOT)/user/Libs/av/codecs.mk
# (the PC's: the C code; AArch64 Linux: dav1d's assembly with glibc's threads -- the Pi's
# pthread stand-in is for newlib: its flags are those of codecs.mk's aarch64 minus that)
ifeq ($(AV_ARCH),aarch64)
DAV1D_CF := -std=gnu11 -I$(DAV1D)/onyx -I$(DAV1D) -I$(DAV1D)/include
DAV1D_SRC := $(filter-out %/onyx_sysconf.c,$(DAV1D_SRC))
endif

.DEFAULT_GOAL := $(OUT)/avbench
AV_SRC := $(wildcard $(ROOT)/user/Libs/av/*.c)
CODEC_SRC := $(VPX_SRC) $(DAV1D_SRC) $(DAV1D_TMPL_SRC) $(OPUS_SRC)
ARCHF := $(if $(filter aarch64,$(AV_ARCH)),-mcpu=cortex-a72,)
obj = $(OUT)/o/$(subst /,_,$(patsubst $(ROOT)/%,%,$(basename $(1)))).o
define RULE
$(call obj,$(1)): $(1)
	@mkdir -p $(OUT)/o
	$$(CC) -O2 $(ARCHF) $(2) -c $$< -o $$@
endef
$(foreach s,$(AV_SRC) $(ROOT)/tools/tests/av/avbench.c,$(eval $(call RULE,$(s),-std=gnu11 -DAV_POSIX -I$(ROOT)/user/Libs/av $(AV_CODECS_CF))))
$(foreach s,$(VPX_SRC),$(eval $(call RULE,$(s),-O3 $(VPX_CF))))
$(foreach s,$(DAV1D_SRC) $(DAV1D_ASM),$(eval $(call RULE,$(s),-D_GNU_SOURCE $(DAV1D_CF))))
$(foreach s,$(DAV1D_TMPL_SRC),$(eval $(call RULE,$(s),-DBITDEPTH=8 $(DAV1D_CF))))
$(foreach s,$(OPUS_SRC),$(eval $(call RULE,$(s),$(OPUS_CF))))
OBJ := $(foreach s,$(AV_SRC) $(ROOT)/tools/tests/av/avbench.c $(CODEC_SRC) $(DAV1D_ASM),$(call obj,$(s)))

$(OUT)/avbench: $(OBJ)
	$(CC) -static -o $@ $^ -lpthread -lm
