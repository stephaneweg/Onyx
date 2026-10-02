#
# tools/tests/desktop_sim/av_host.mk -- Onyx's media library (user/av) and its codecs (libvpx, dav1d, libopus:
# their C code, user/av/codecs.mk) built for the PC into one archive, $(OUT)/libavhost.a, for the apps run in the
# desktop simulator (the Media Player's videos: shots.sh media). The kapi is the simulator's (fakekapi.cpp).
# With FFmpeg (as the Pi's media.elf): its libraries built for the PC by third_party/ffmpeg-7.1.2/onyx/build.sh host
# into FFMPEG_LIB (link them after libavhost.a: -L$(FFMPEG_LIB) -lavformat -lavcodec -lswscale -lswresample -lavutil).
#
#   sh third_party/ffmpeg-7.1.2/onyx/build.sh host /tmp/onyx_ffmpeg_host
#   make -f tools/tests/desktop_sim/av_host.mk OUT=/tmp/onyx_shots/av -j8
#
OUT ?= /tmp/onyx_shots/av
TP := third_party
AV_ARCH := generic
include user/av/codecs.mk
AV_SRC := $(wildcard user/av/*.c)
obj = $(OUT)/o/$(subst /,_,$(patsubst %.c,%.o,$(1)))
ALL_SRC := $(AV_SRC) $(VPX_SRC) $(DAV1D_SRC) $(DAV1D_TMPL_SRC) $(OPUS_SRC)
OBJS := $(foreach s,$(ALL_SRC),$(call obj,$(s)))

$(OUT)/libavhost.a: $(OBJS)
	@rm -f $@
	@ar rcs $@ $^

define RULE
$(call obj,$(1)): $(1) $(wildcard user/av/*.h)
	@mkdir -p $(OUT)/o
	@gcc -w -O2 $(2) -c $(1) -o $$@
endef
$(foreach s,$(AV_SRC),$(eval $(call RULE,$(s),-std=gnu11 -DONYX_HOST_SIM -Iuser/av -Iuser -Ikernel/include $(AV_CODECS_CF) -DAV_WITH_FFMPEG -Ithird_party/ffmpeg-7.1.2/onyx/include)))
$(foreach s,$(VPX_SRC),$(eval $(call RULE,$(s),$(VPX_CF))))
$(foreach s,$(DAV1D_SRC),$(eval $(call RULE,$(s),$(DAV1D_CF))))
$(foreach s,$(DAV1D_TMPL_SRC),$(eval $(call RULE,$(s),-DBITDEPTH=8 $(DAV1D_CF))))
$(foreach s,$(OPUS_SRC),$(eval $(call RULE,$(s),$(OPUS_CF))))
