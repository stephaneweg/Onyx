# common.sh -- shared by the ports' build scripts (tools/ports/<name>/build.sh): where things are,
# the toolchain, the POSIX sysroot (built and installed first). Sourced, not run.
#
#   ONYX_TOOLCHAIN_PREFIX  the toolchain (default: aarch64-onyx-elf- when installed, else
#                  aarch64-none-elf-: tools/onyx-env.sh)
#   ONYX_SYSROOT   the sysroot (default <onyx>/out/sysroot-onyx, or out/sysroot for aarch64-none-elf)
#   PORTS_OUT      the build trees and the tools made (default <onyx>/out/ports-onyx, or out/ports)
#   JOBS           parallel jobs (default: the CPUs)
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

set -e
ONYX=$(cd "$(dirname "$0")/../../.." && pwd)
TP=$ONYX/third_party
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
TOOLCHAIN_FILE=$ONYX/tools/onyx-toolchain.cmake
# the toolchain and its sysroot (onyx-env.sh: ONYX_TOOLCHAIN_PREFIX, else aarch64-onyx-elf when
# installed, else aarch64-none-elf; out/sysroot-onyx or out/sysroot)
ONYX_ROOT=$ONYX ONYX_ENV_QUIET=1 . "$ONYX/tools/onyx-env.sh"
case $ONYX_TOOLCHAIN_PREFIX in
aarch64-onyx-elf-) : "${PORTS_OUT:=$ONYX/out/ports-onyx}";;
*) : "${PORTS_OUT:=$ONYX/out/ports}";;
esac
mkdir -p "$PORTS_OUT/build" "$PORTS_OUT/bin"
export PYTHONDONTWRITEBYTECODE=1

# CMake with the toolchain file (CMake would also take CFLAGS / LDFLAGS from the environment: the
# toolchain file's flags are the ones)
onyx_cmake ()
{
	env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CC -u CXX cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
		-DONYX_SYSROOT="$ONYX_SYSROOT" -DONYX_TOOLCHAIN_PREFIX="$ONYX_TOOLCHAIN_PREFIX" -DCMAKE_BUILD_TYPE=Release "$@"
}

# the sysroot: libonyxposix's headers, library, crt0, specs (always brought up to date)
make --no-print-directory -s -C "$ONYX/user/Runtime/libc/posix" install PREFIX="$ONYX_TOOLCHAIN_PREFIX" SYSROOT="$ONYX_SYSROOT" >/dev/null

# the libraries the ports use that are already in third_party (prebuilt for Onyx): headers and
# archives copied into the sysroot, with their pkg-config files
#
# onyx_dep_lib <lib.a> <dir> <cflags> <sources...>: under aarch64-none-elf the prebuilt <dir>/<lib.a>
# (the newlib apps' own copy) is copied; under aarch64-onyx-elf the sources are compiled (once, in
# $PORTS_OUT/build/deps): objects of the interim toolchain do not link with its newlib (errno and
# stdio are thread-local there: no __errno, no _impure_ptr).
onyx_dep_lib ()
{
	_a=$1; _dir=$2; _cf=$3; shift 3
	if [ "$ONYX_TOOLCHAIN_PREFIX" != aarch64-onyx-elf- ]; then
		cp "$_dir/$_a" "$ONYX_SYSROOT/lib/"
		return
	fi
	_b=$PORTS_OUT/build/deps/${_a%.a}
	if [ ! -f "$_b/$_a" ]; then
		mkdir -p "$_b"
		for _s in "$@"; do
			"${ONYX_TOOLCHAIN_PREFIX}gcc" $CFLAGS -O2 -w $_cf -c "$_s" -o "$_b/$(basename "$_s" .c).o"
		done
		"${ONYX_TOOLCHAIN_PREFIX}ar" rcs "$_b/$_a" "$_b"/*.o
	fi
	cp "$_b/$_a" "$ONYX_SYSROOT/lib/"
}

onyx_install_deps ()
{
	inc=$ONYX_SYSROOT/include
	lib=$ONYX_SYSROOT/lib
	pc=$lib/pkgconfig
	mkdir -p "$inc" "$lib" "$pc"
	cp "$TP/zlib-1.3.1/zlib.h" "$TP/zlib-1.3.1/zconf.h" "$inc/"
	onyx_dep_lib libz.a "$TP/zlib-1.3.1" "-DHAVE_UNISTD_H -DZ_HAVE_UNISTD_H -DHAVE_STDARG_H" "$TP"/zlib-1.3.1/*.c
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: zlib\nDescription: zlib compression library\nVersion: 1.3.1\nLibs: -L${libdir} -lz\nCflags: -I${includedir}\n' "$ONYX_SYSROOT" > "$pc/zlib.pc"
	mkdir -p "$inc/nghttp2"
	cp "$TP/nghttp2-1.70.0/lib/includes/nghttp2/nghttp2.h" "$TP/nghttp2-1.70.0/lib/includes/nghttp2/nghttp2ver.h" "$inc/nghttp2/"
	onyx_dep_lib libnghttp2.a "$TP/nghttp2-1.70.0" "-std=gnu99 -DHAVE_CONFIG_H -DNGHTTP2_STATICLIB -I$TP/nghttp2-1.70.0/lib -I$TP/nghttp2-1.70.0/lib/includes" "$TP"/nghttp2-1.70.0/lib/*.c
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: libnghttp2\nDescription: HTTP/2 C library\nVersion: 1.70.0\nLibs: -L${libdir} -lnghttp2\nCflags: -I${includedir} -DNGHTTP2_STATICLIB\n' "$ONYX_SYSROOT" > "$pc/libnghttp2.pc"
	mkdir -p "$inc/brotli"
	cp "$TP/brotli-1.1.0/c/include/brotli/"*.h "$inc/brotli/"
	onyx_dep_lib libbrotlidec.a "$TP/brotli-1.1.0" "-I$TP/brotli-1.1.0/c/include" "$TP"/brotli-1.1.0/c/common/*.c "$TP"/brotli-1.1.0/c/dec/*.c
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: libbrotlidec\nDescription: Brotli decoder library (with brotlicommon)\nVersion: 1.1.0\nLibs: -L${libdir} -lbrotlidec\nCflags: -I${includedir}\n' "$ONYX_SYSROOT" > "$pc/libbrotlidec.pc"
}

# a tool built: kept as <name>.elf (the in-tree convention: make stage copies user/BinUtils/*.elf),
# checked by el0scan
onyx_tool_done ()
{
	src=$1
	name=$2
	cp "$src" "$PORTS_OUT/bin/$name.elf"
	OBJDUMP=${ONYX_TOOLCHAIN_PREFIX}objdump sh "$ONYX/tools/el0scan.sh" "$PORTS_OUT/bin/$name.elf"
	"${ONYX_TOOLCHAIN_PREFIX}size" "$PORTS_OUT/bin/$name.elf"
}
