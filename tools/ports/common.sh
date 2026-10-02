# common.sh -- shared by the ports' build scripts (tools/ports/<name>/build.sh): where things are,
# the toolchain, the POSIX sysroot (built and installed first). Sourced, not run.
#
#   ONYX_SYSROOT   the sysroot (default <onyx>/out/sysroot)
#   PORTS_OUT      the build trees and the tools made (default <onyx>/out/ports)
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
: "${ONYX_SYSROOT:=$ONYX/out/sysroot}"
: "${PORTS_OUT:=$ONYX/out/ports}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
TOOLCHAIN_FILE=$ONYX/tools/onyx-toolchain.cmake
ONYX_ENV_QUIET=1 . "$ONYX/tools/onyx-env.sh" "$ONYX_SYSROOT"
mkdir -p "$PORTS_OUT/build" "$PORTS_OUT/bin"
export PYTHONDONTWRITEBYTECODE=1

# CMake with the toolchain file (CMake would also take CFLAGS / LDFLAGS from the environment: the
# toolchain file's flags are the ones)
onyx_cmake ()
{
	env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CC -u CXX cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
		-DONYX_SYSROOT="$ONYX_SYSROOT" -DCMAKE_BUILD_TYPE=Release "$@"
}

# the sysroot: libonyxposix's headers, library, crt0, specs (always brought up to date)
make --no-print-directory -s -C "$ONYX/user/libc/posix" install SYSROOT="$ONYX_SYSROOT" >/dev/null

# the libraries the ports use that are already in third_party (prebuilt for Onyx): headers and
# archives copied into the sysroot, with their pkg-config files
onyx_install_deps ()
{
	inc=$ONYX_SYSROOT/include
	lib=$ONYX_SYSROOT/lib
	pc=$lib/pkgconfig
	mkdir -p "$inc" "$lib" "$pc"
	cp "$TP/zlib-1.3.1/zlib.h" "$TP/zlib-1.3.1/zconf.h" "$inc/"
	cp "$TP/zlib-1.3.1/libz.a" "$lib/"
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: zlib\nDescription: zlib compression library\nVersion: 1.3.1\nLibs: -L${libdir} -lz\nCflags: -I${includedir}\n' "$ONYX_SYSROOT" > "$pc/zlib.pc"
	mkdir -p "$inc/nghttp2"
	cp "$TP/nghttp2-1.70.0/lib/includes/nghttp2/nghttp2.h" "$TP/nghttp2-1.70.0/lib/includes/nghttp2/nghttp2ver.h" "$inc/nghttp2/"
	cp "$TP/nghttp2-1.70.0/libnghttp2.a" "$lib/"
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: libnghttp2\nDescription: HTTP/2 C library\nVersion: 1.70.0\nLibs: -L${libdir} -lnghttp2\nCflags: -I${includedir} -DNGHTTP2_STATICLIB\n' "$ONYX_SYSROOT" > "$pc/libnghttp2.pc"
	mkdir -p "$inc/brotli"
	cp "$TP/brotli-1.1.0/c/include/brotli/"*.h "$inc/brotli/"
	cp "$TP/brotli-1.1.0/libbrotlidec.a" "$lib/"
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: libbrotlidec\nDescription: Brotli decoder library (with brotlicommon)\nVersion: 1.1.0\nLibs: -L${libdir} -lbrotlidec\nCflags: -I${includedir}\n' "$ONYX_SYSROOT" > "$pc/libbrotlidec.pc"
}

# a tool built: kept as <name>.elf (the in-tree convention: make stage copies user/bin/*.elf),
# checked by el0scan
onyx_tool_done ()
{
	src=$1
	name=$2
	cp "$src" "$PORTS_OUT/bin/$name.elf"
	sh "$ONYX/tools/el0scan.sh" "$PORTS_OUT/bin/$name.elf"
	"${ONYX_TOOLCHAIN_PREFIX}size" "$PORTS_OUT/bin/$name.elf"
}
