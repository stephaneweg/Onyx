#
# user/Libs/av/Makefile -- the media library's codecs built for the Pi: libvpx.a, libdav1d.a, libopus.a
# (their sources and flags: codecs.mk; README.onyx in each library's folder of third_party). The
# archives are committed: run this after a change to a codec's sources or flags.
#
#   make -C user/Libs/av            (aarch64-none-elf + newlib; -O3 for libvpx as upstream builds it)
#
# Linked by the Media Player (user/Makefile). Jet's media engine has its own build of them for the
# POSIX toolchain (tools/webkit/av.mk).
#
PREFIX   ?= aarch64-none-elf-
CC        = $(PREFIX)gcc
AR        = $(PREFIX)ar
TP       := $(abspath $(CURDIR)/../../../third_party)
AV_ARCH  := aarch64
CF = -mcpu=cortex-a72 -O2 -std=c99 -fno-pic -fno-pie -fno-stack-protector \
     -ffunction-sections -fdata-sections -fcommon -DNDEBUG \
     -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L

include $(CURDIR)/codecs.mk

.PHONY: all codecs clean
all: codecs

# -MMD -MP: each object's headers in its .o.d (a changed header rebuilds its users)
%.o: %.c
	$(CC) $(CF) $(CINC) -MMD -MP -MF $@.d -c $< -o $@
%.o: %.S
	$(CC) -mcpu=cortex-a72 -fno-pic -fno-pie $(CINC) -MMD -MP -MF $@.d -c $< -o $@

VPX_OBJ := $(patsubst %.c,%.o,$(VPX_SRC))
$(VPX_OBJ): CINC := -O3 $(VPX_CF)
$(VPX)/libvpx.a: $(VPX_OBJ); $(AR) rcs $@ $^
DAV1D_OBJ := $(patsubst %.c,%.o,$(DAV1D_SRC)) $(patsubst %.S,%.o,$(DAV1D_ASM))
DAV1D_TMPL_OBJ := $(patsubst %.c,%.o,$(DAV1D_TMPL_SRC))
$(DAV1D_OBJ): CINC := $(DAV1D_CF)
$(DAV1D_TMPL_OBJ): CINC := -DBITDEPTH=8 $(DAV1D_CF)
$(DAV1D)/libdav1d.a: $(DAV1D_OBJ) $(DAV1D_TMPL_OBJ); $(AR) rcs $@ $^
OPUS_OBJ := $(patsubst %.c,%.o,$(OPUS_SRC))
$(OPUS_OBJ): CINC := $(OPUS_CF)
$(OPUS)/libopus.a: $(OPUS_OBJ); $(AR) rcs $@ $^
CODEC_OBJ := $(VPX_OBJ) $(DAV1D_OBJ) $(DAV1D_TMPL_OBJ) $(OPUS_OBJ)

codecs: $(VPX)/libvpx.a $(DAV1D)/libdav1d.a $(OPUS)/libopus.a

clean:
	rm -f $(CODEC_OBJ) $(patsubst %.o,%.o.d,$(CODEC_OBJ)) $(VPX)/libvpx.a $(DAV1D)/libdav1d.a $(OPUS)/libopus.a

-include $(wildcard $(patsubst %.o,%.o.d,$(CODEC_OBJ)))
