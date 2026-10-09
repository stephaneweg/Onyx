#!/bin/sh
# get_unicorn.sh -- Unicorn Engine (GPL-2.0; a development tool's library, not shipped with Onyx: docs/LICENSING.md)
# built for the runner: its source from PyPI's release, only the AArch64 target, static, for Linux (gcc) and for
# Windows (mingw-w64, x86_64-w64-mingw32-gcc-posix) -> tools/onyxrun/build/unicorn/{include,lin,win}.
# Needs cmake, gcc, python3's pip (to download), mingw-w64 for the Windows one (skipped when missing).
# MIT licence (Onyx). Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
set -e
cd "$(dirname "$0")"
V=${UNICORN_VERSION:-2.1.4}
B=$(pwd)/build/unicorn
mkdir -p "$B"
cd "$B"
if [ ! -d src/unicorn-$V ]; then
	python3 -m pip download "unicorn==$V" --no-deps --no-binary=:all: -d src -q
	tar xzf src/unicorn-$V.tar.gz -C src
fi
S=$(pwd)/src/unicorn-$V/src
# (the PyPI tarball keeps its cmake modules beside CMakeLists.txt, which looks for them in cmake/)
mkdir -p "$S/cmake"; cp "$S"/*.cmake "$S/cmake/" 2>/dev/null || true
rm -rf include; cp -r "$S/include" include
J=$(nproc 2>/dev/null || echo 4)
if [ ! -f lin/libunicorn.a ]; then
	mkdir -p build-lin && (cd build-lin && cmake "$S" -DUNICORN_ARCH=aarch64 -DCMAKE_BUILD_TYPE=Release \
		-DUNICORN_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF >/dev/null && make -j"$J" >/dev/null)
	mkdir -p lin && cp build-lin/libunicorn.a lin/
	echo "get_unicorn: Linux library built"
fi
if [ ! -f win/libunicorn.a ] && command -v x86_64-w64-mingw32-gcc-posix >/dev/null; then
	mkdir -p build-win && cat > build-win/tc.cmake <<TC
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
TC
	(cd build-win && cmake "$S" -DCMAKE_TOOLCHAIN_FILE=tc.cmake -DUNICORN_ARCH=aarch64 -DCMAKE_BUILD_TYPE=Release \
		-DUNICORN_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF >/dev/null && make -j"$J" >/dev/null)
	mkdir -p win && cp build-win/libunicorn.a win/
	echo "get_unicorn: Windows library built"
fi
echo "get_unicorn: $B"
