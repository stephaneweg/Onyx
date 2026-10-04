#
# tools/webkit/av.mk -- Onyx's media library (user/av) and its codecs for the POSIX toolchain
# (aarch64-onyx-elf): libonyxav.a, what Web's media engine (WebKit's MediaPlayerPrivateOnyx) links.
# build-web.sh runs it:   make -f tools/webkit/av.mk ONYX=<the sources> S=<the sysroot> O=<objects' directory>
#
# The same sources and flags as the Pi's newlib build (user/av/Makefile, through user/av/codecs.mk):
# libvpx (VP8, VP9: NEON), dav1d (AV1: its AArch64 assembly, one thread -- its own pthread stand-in),
# libopus; user/av with -DAV_POSIX (its threads are pthreads here) and -DAV_KAPI_SOUND (the sound
# output is still the kernel's, through kapi.h). No FFmpeg: Web stays LGPL (docs/LICENSING.md).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
#
TP := $(ONYX)/third_party
AV_ARCH := aarch64
include $(ONYX)/user/av/codecs.mk

CC := aarch64-onyx-elf-gcc -specs=$(S)/lib/onyx.specs -mcpu=cortex-a72 -w -ffunction-sections -fdata-sections
AR := aarch64-onyx-elf-ar

AV_SRC := $(addprefix $(ONYX)/user/av/,av_demux.c av_mkv.c av_mp4.c av_riff.c av_flac.c av_mp3.c av_stub.c \
	av_codec.c av_yuv.c av_resample.c av_store.c av_player.c av_vpx.c av_dav1d.c av_opus.c)
AV_CF := -std=gnu99 -O2 -DAV_POSIX -DAV_KAPI_SOUND $(AV_CODECS_CF) -I$(ONYX)/user/av -I$(ONYX)/user -I$(ONYX)/kernel/include

obj = $(patsubst $(ONYX)/%,$(O)/%.o,$(1))
AV_OBJ := $(call obj,$(AV_SRC))
VPX_OBJ := $(call obj,$(VPX_SRC))
DAV1D_OBJ := $(call obj,$(DAV1D_SRC) $(DAV1D_ASM))
DAV1D_TMPL_OBJ := $(call obj,$(DAV1D_TMPL_SRC))
OPUS_OBJ := $(call obj,$(OPUS_SRC))

$(AV_OBJ): CF := $(AV_CF)
$(VPX_OBJ): CF := -O3 $(VPX_CF)
$(DAV1D_OBJ): CF := -O2 $(DAV1D_CF)
$(DAV1D_TMPL_OBJ): CF := -O2 -DBITDEPTH=8 $(DAV1D_CF)
$(OPUS_OBJ): CF := -O2 $(OPUS_CF)

$(O)/libonyxav.a: $(AV_OBJ) $(VPX_OBJ) $(DAV1D_OBJ) $(DAV1D_TMPL_OBJ) $(OPUS_OBJ)
	rm -f $@
	$(AR) rcs $@ $^

$(O)/%.c.o: $(ONYX)/%.c
	@mkdir -p $(@D)
	$(CC) $(CF) -MMD -MP -MF $@.d -c $< -o $@

$(O)/%.S.o: $(ONYX)/%.S
	@mkdir -p $(@D)
	$(CC) -fno-pic -fno-pie $(CF) -MMD -MP -MF $@.d -c $< -o $@

-include $(AV_OBJ:.o=.o.d) $(VPX_OBJ:.o=.o.d) $(DAV1D_OBJ:.o=.o.d) $(DAV1D_TMPL_OBJ:.o=.o.d) $(OPUS_OBJ:.o=.o.d)
