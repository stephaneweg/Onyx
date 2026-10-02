#
# user/av/codecs.mk -- the media library's vendored codecs (docs/06 §44, docs/media/README.md):
# their sources and flags, for the three builds that include this file with TP set to
# third_party/:
#   - the Pi (user/netsurf/Makefile: libvpx.a, libdav1d.a, libopus.a; AV_ARCH = aarch64:
#     libvpx's NEON intrinsics, dav1d's AArch64 assembly);
#   - the PC bench (tools/tests/netsurf/host.mk) and Windows (pc/Jet/jet.mk): the C code alone.
#
#   libvpx 1.15.2 (BSD)  VP8 + VP9 decoders, 8 bits, no threads, no post-processing; the
#                        configure's headers in onyx/generic and onyx/arm64 (README.onyx)
#   dav1d 1.5.1 (BSD)    AV1, 8 bits; config.h by hand (onyx/), one thread
#   libopus 1.5.2 (BSD)  Opus, floating point, C only; config.h by hand (onyx/)
#
# Each user sets AV_ARCH (aarch64 or generic) before including.  It gets:
#   VPX_SRC, VPX_CF       libvpx's sources and flags
#   DAV1D_SRC, DAV1D_CF   dav1d's C files (DAV1D_TMPL_SRC: + -DBITDEPTH=8), DAV1D_ASM (the Pi)
#   OPUS_SRC, OPUS_CF     libopus
#   AV_CODECS_CF          the flags user/av's glue (av_vpx.c, av_dav1d.c, av_opus.c) needs
#
AV_ARCH ?= generic
VPX   := $(TP)/libvpx-1.15.2
DAV1D := $(TP)/dav1d-1.5.1
OPUS  := $(TP)/opus-1.5.2

# ---- libvpx ------------------------------------------------------------------------------
VPX_ALL := $(shell find $(VPX) -name '*.c' -not -path '*/onyx/*' | sort)
VPX_ARM := $(filter %_neon.c %/arm/loopfilter_arm.c %/aarch64_cpudetect.c,$(VPX_ALL))
ifeq ($(AV_ARCH),aarch64)
VPX_SRC := $(VPX_ALL) $(VPX)/onyx/arm64/vpx_config.c
VPX_CF  := -std=gnu99 -I$(VPX)/onyx/arm64 -I$(VPX)/onyx -I$(VPX)
else
VPX_SRC := $(filter-out $(VPX_ARM),$(VPX_ALL)) $(VPX)/onyx/generic/vpx_config.c
VPX_CF  := -std=gnu99 -I$(VPX)/onyx/generic -I$(VPX)/onyx -I$(VPX)
endif

# ---- dav1d -------------------------------------------------------------------------------
DAV1D_TMPL_SRC := $(wildcard $(DAV1D)/src/*_tmpl.c)
DAV1D_SRC := $(filter-out $(DAV1D_TMPL_SRC),$(wildcard $(DAV1D)/src/*.c))
DAV1D_CF  := -std=gnu11 -D_GNU_SOURCE -I$(DAV1D)/onyx -I$(DAV1D) -I$(DAV1D)/include
ifeq ($(AV_ARCH),aarch64)
DAV1D_SRC += $(DAV1D)/src/arm/cpu.c $(DAV1D)/onyx/onyx_sysconf.c
DAV1D_ASM := $(addprefix $(DAV1D)/src/arm/64/,itx.S looprestoration_common.S msac.S refmvs.S \
             cdef.S filmgrain.S ipred.S loopfilter.S looprestoration.S mc.S mc_dotprod.S)
DAV1D_CF  := -std=gnu11 -D__ONYX_DAV1D_PI -I$(DAV1D)/onyx/pthread -I$(DAV1D)/onyx -I$(DAV1D) -I$(DAV1D)/include
endif
DAV1D_WIN_SRC := $(DAV1D)/src/win32/thread.c

# ---- libopus -----------------------------------------------------------------------------
OPUS_SRC := $(filter-out $(OPUS)/celt/opus_custom_demo.c,$(wildcard $(OPUS)/src/*.c $(OPUS)/celt/*.c \
            $(OPUS)/silk/*.c $(OPUS)/silk/float/*.c))
OPUS_CF  := -std=gnu99 -DHAVE_CONFIG_H -I$(OPUS)/onyx -I$(OPUS)/include -I$(OPUS)/celt -I$(OPUS)/silk \
            -I$(OPUS)/silk/float -I$(OPUS)/src

# ---- user/av's glue ------------------------------------------------------------------------
AV_CODECS_CF := -DAV_WITH_VPX -DAV_WITH_DAV1D -DAV_WITH_OPUS -I$(VPX) -I$(DAV1D)/include -I$(OPUS)/include
