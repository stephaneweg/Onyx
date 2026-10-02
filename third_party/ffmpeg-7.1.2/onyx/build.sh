#!/bin/sh
# third_party/ffmpeg-7.1.2/onyx/build.sh -- FFmpeg built for Onyx's media library (user/av: av_ffmpeg.c,
# av_lavf.c; AV_WITH_FFMPEG). GPL-2.0-or-later (--enable-gpl): every program linking it is distributed
# under the GPL-2.0 (the Media Player, Jet Browser). libavcodec (every decoder, no encoder), libavformat
# (every demuxer, no muxer, no protocol: the media library feeds it), libavutil, libswscale (frames
# brought to 4:2:0), libswresample; no programs, no filters, no devices, no threads (the media library
# opens decoders one at a time: av__ff_lock), no external libraries.
#
#   sh third_party/ffmpeg-7.1.2/onyx/build.sh pi     -> onyx/aarch64/lib*.a (the Pi: aarch64-none-elf, NEON
#                                                        assembly; committed -- user/Makefile links them)
#   sh third_party/ffmpeg-7.1.2/onyx/build.sh host [OUT]  -> OUT/lib*.a (the PC: the desktop simulator, the
#                                                        tests; default /tmp/onyx_ffmpeg_host)
#   the headers: onyx/include (make install-headers; the same for both: little-endian, 64 bits)
# Onyx: -D__ONYX__ (pi): libavutil/aarch64/timer.h reads cntvct_el0, not the PMU's pmccntr_el0 -- the
# apps run at EL0, where the PMU is closed (tools/el0scan.sh checks the libraries).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(dirname "$HERE")
COMMON="--enable-gpl --disable-programs --disable-doc --disable-avdevice --disable-avfilter --disable-postproc
	--disable-network --disable-encoders --disable-muxers --disable-hwaccels --disable-protocols --disable-devices
	--disable-indevs --disable-outdevs --disable-pthreads --disable-debug --disable-autodetect --disable-iconv
	--enable-static --disable-shared --pkg-config=false"
case "$1" in
pi)
	B=${2:-/tmp/onyx_ffmpeg_pi}; mkdir -p "$B"; cd "$B"
	"$SRC/configure" $COMMON --enable-cross-compile --cross-prefix=aarch64-none-elf- --arch=aarch64 --cpu=cortex-a72 \
		--target-os=none --disable-pic --disable-runtime-cpudetect \
		--extra-cflags="-mcpu=cortex-a72 -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-stack-protector -D__ONYX__" \
		--extra-ldflags="--specs=nosys.specs" >configure.log
	make -j"$(nproc)" >make.log
	mkdir -p "$HERE/aarch64"
	for l in avcodec avformat avutil swscale swresample; do cp lib$l/lib$l.a "$HERE/aarch64/"; done
	rm -rf "$HERE/include"; make install-headers prefix="$B/inst" >/dev/null; cp -r "$B/inst/include" "$HERE/include"
	;;
host)
	B=${2:-/tmp/onyx_ffmpeg_host}; mkdir -p "$B"; cd "$B"
	[ -f "$B/libavcodec/libavcodec.a" ] && [ -f "$B/.done" ] && exit 0
	"$SRC/configure" $COMMON --disable-x86asm --extra-cflags="-w" >configure.log
	make -j"$(nproc)" >make.log
	for l in avcodec avformat avutil swscale swresample; do cp lib$l/lib$l.a "$B/"; done
	touch "$B/.done"
	;;
*) echo "usage: build.sh pi | host [OUT]"; exit 1 ;;
esac
