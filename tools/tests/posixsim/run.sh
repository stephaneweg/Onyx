#!/bin/sh
# run.sh -- run /bin/posixtest (or another libonyxposix program) on the PC under qemu-user, on the
# posixsim stand-in kernel (fakekapi.c: the kapi table over raw Linux system calls).
#
#   sh tools/tests/posixsim/run.sh [posixtest arguments]       # the v75 calls (the WPs' spec)
#   POSIXSIM_LEVEL=74 sh tools/tests/posixsim/run.sh [...]     # no v75: libonyxposix's fallbacks
#   PROG=<file.c> sh tools/tests/posixsim/run.sh [args]         # another program
#   PROG=none NAME=<name> OBJS="<objects, libraries>" sh ...    # a program from built objects
#   (CFLAGS_EXTRA: more compiler flags for PROG; BUILD_ONLY=1: link it, do not run it)
#
# Needs qemu-aarch64-static (apt install qemu-user-static) and the toolchain. The files live in
# POSIXSIM_ROOT (default /tmp/posixsim: SD/ and RAM/), the program is installed as SD:/bin/<name>.
# Not a model of the real kernel's timing, FAT, the RAM volume or its DNS: a check of the library's
# logic. The Pi is the reference.
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
HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../../.." && pwd)
: "${PREFIX:=aarch64-none-elf-}"
command -v "${PREFIX}gcc" >/dev/null 2>&1 || PATH=/opt/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin:$PATH
: "${POSIXSIM_QEMU:=$(command -v qemu-aarch64-static || command -v qemu-aarch64)}"
[ -x "$POSIXSIM_QEMU" ] || { echo "run.sh: no qemu-aarch64(-static) (apt install qemu-user-static)" >&2; exit 2; }
: "${POSIXSIM_ROOT:=/tmp/posixsim}"
: "${PROG:=$ONYX/user/bin/posixtest.c}"
name=${NAME:-$(basename "$PROG" .c)}
B=$POSIXSIM_ROOT/build
mkdir -p "$B" "$POSIXSIM_ROOT/SD/bin" "$POSIXSIM_ROOT/RAM"

P=$ONYX/user/libc/posix
# the library built for the bench (-DONYX_POSIXSIM: the virtual counter, which qemu-user lets EL0 read)
make -s --no-print-directory -C "$P" B=build-sim EXTRA_CFLAGS=-DONYX_POSIXSIM >/dev/null
CF="-mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -ffunction-sections -fdata-sections -isystem $P/include -DFD_SETSIZE=1024 -I$ONYX/user -I$ONYX/kernel/include"
"${PREFIX}gcc" $CF -c "$HERE/fakekapi.c" -o "$B/fakekapi.o"
"${PREFIX}gcc" -c "$HERE/start.S" -o "$B/start.o"
src=$PROG
[ "$PROG" = none ] && src=
"${PREFIX}gcc" $CF $CFLAGS_EXTRA -specs="$P/build-sim/onyx.specs" -Wl,-e,posixsim_start $src "$B/fakekapi.o" "$B/start.o" \
	$OBJS -o "$B/$name.elf"
cp "$B/$name.elf" "$POSIXSIM_ROOT/SD/bin/$name"
[ -n "$BUILD_ONLY" ] && exit 0

export POSIXSIM_ROOT POSIXSIM_QEMU
export POSIXSIM_ARGV0="SD:/bin/$name"
exec "$POSIXSIM_QEMU" "$POSIXSIM_ROOT/SD/bin/$name" "$@"
