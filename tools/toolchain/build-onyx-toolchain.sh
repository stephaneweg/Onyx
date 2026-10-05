#!/bin/sh
# build-onyx-toolchain.sh -- build the aarch64-onyx-elf toolchain (docs/POSIX-PLAN.md §2, WP-TC;
# docs/03 §1): GCC 14.2 + binutils 2.43 + newlib 4.4, thread model posix, native TLS, newlib's
# _reent / errno per thread, libgcc and libstdc++ built against libonyxposix's <pthread.h>.
#
#   sh tools/toolchain/build-onyx-toolchain.sh            # -> /opt/toolchains/aarch64-onyx-elf-14.2
#   PREFIX=$HOME/x-tools/aarch64-onyx-elf-14.2 sh tools/toolchain/build-onyx-toolchain.sh
#
# Variables: PREFIX (the install), WORK (sources, build trees, stamps; default
# $HOME/.cache/onyx-toolchain -- on WSL keep it on the Linux side, not under /mnt/c), JOBS
# (default nproc), KEEP_BUILD=1 (keep the build trees), STAGES (default "all"; or a list among
# fetch binutils gcc1 newlib overlay gcc final check).
#
# Resumable: every stage leaves a stamp in $WORK/stamps; a new run skips what is done (rm a stamp,
# or the whole $WORK, to redo it). Needs a host C/C++ compiler, make, m4, bison, flex, xz, curl
# (or wget), patch; ~10 GB in $WORK during the build. Linux or WSL (Ubuntu:
#   sudo apt install build-essential m4 xz-utils curl patch).
#
# The stages: fetch (pinned tarballs, sha256 checked) -> binutils -> GCC stage 1 (C only, no
# headers) -> newlib -> the header overlay (libonyxposix's pthread.h, sys/_pthreadtypes.h,
# semaphore.h, sys/dirent.h and a sys/features.h stating the POSIX options, into
# $PREFIX/aarch64-onyx-elf/include) -> GCC final (C, C++; libgcc and libstdc++ against the
# overlay) -> final (strip, the build info) -> check (the acceptance checks, printed).
# The one GCC source change: patches/gcc-14.2.0-libstdcxx-onyx.patch (libstdc++'s configure).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../.." && pwd)
TARGET=aarch64-onyx-elf
: "${PREFIX:=/opt/toolchains/aarch64-onyx-elf-14.2}"
: "${WORK:=${XDG_CACHE_HOME:-$HOME/.cache}/onyx-toolchain}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
: "${STAGES:=all}"
: "${KEEP_BUILD:=0}"
# Bump with any change of versions, options, patch or overlay (shown by gcc --version).
ONYX_TC_REVISION=onyx1

# --- the pinned sources ----------------------------------------------------------------------
BINUTILS=binutils-2.43
GCC=gcc-14.2.0
NEWLIB=newlib-4.4.0.20231231
GMP=gmp-6.3.0
MPFR=mpfr-4.2.1
MPC=mpc-1.3.1
SOURCES="
https://ftp.gnu.org/gnu/binutils/$BINUTILS.tar.xz b53606f443ac8f01d1d5fc9c39497f2af322d99e14cea5c0b4b124d630379365
https://ftp.gnu.org/gnu/gcc/$GCC/$GCC.tar.xz a7b39bc69cbf9e25826c5a60ab26477001f7c08d85cec04bc0e29cabed6f3cc9
https://sourceware.org/pub/newlib/$NEWLIB.tar.gz 0c166a39e1bf0951dfafcd68949fe0e4b6d3658081d6282f39aeefc6310f2f13
https://ftp.gnu.org/gnu/gmp/$GMP.tar.xz a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
https://ftp.gnu.org/gnu/mpfr/$MPFR.tar.xz 277807353a6726978996945af13e52829e3abd7a9a5b7fb2793894e18f1fcbb2
https://ftp.gnu.org/gnu/mpc/$MPC.tar.gz ab642492f5cf882b74aa0cb730cd410a81edcdbec895183ce930e706c1c759b8
"

# --- the configurations (docs/POSIX-PLAN.md "WP-TC resolutions") -------------------------------
COMMON_CONF="--target=$TARGET --prefix=$PREFIX --disable-nls --disable-multilib"
BINUTILS_CONF="--disable-werror --disable-gdb --disable-gdbserver --disable-sim --disable-gprofng --disable-readline"
# newlib: per-thread _reent members (errno, stdio's state...) as __thread variables, the locks
# retargetable (libonyxposix provides them), long long and C99 formats, no syscall stubs (libonyxposix
# or onyx_syscalls.c provides them), sections for --gc-sections.
NEWLIB_CONF="--enable-newlib-reent-thread-local --enable-newlib-retargetable-locking --enable-newlib-io-long-long --enable-newlib-io-c99-formats --disable-newlib-supplied-syscalls --enable-newlib-mb --enable-newlib-register-fini"
TARGET_CFLAGS="-O2 -g -ffunction-sections -fdata-sections"
# GCC: the sysroot is $PREFIX/aarch64-onyx-elf (newlib's headers in its include/), as Arm's toolchain:
# without it GCC looks for the target's headers in .../sys-include when it builds its <limits.h>,
# finds none and installs a <limits.h> that does not chain to newlib's (no PATH_MAX...). Under the
# prefix, so the toolchain stays relocatable.
GCC_COMMON="--with-sysroot=$PREFIX/$TARGET --with-native-system-header-dir=/include --with-cpu=cortex-a72 --with-newlib --enable-tls --disable-shared --without-isl --disable-libssp --disable-libgomp --disable-libquadmath --disable-libsanitizer --disable-libvtv --disable-libatomic --disable-libitm --disable-libcc1 --with-pkgversion=Onyx-toolchain-$ONYX_TC_REVISION --with-bugurl=https://github.com/stephaneweg/Onyx"
GCC1_CONF="$GCC_COMMON --enable-languages=c --without-headers --disable-threads"
# libstdc++'s time features: --enable-libstdcxx-time=yes runs link tests, impossible before the
# target's libc can link (gcc_no_link): the default "auto" + the patch states them for *-onyx-*.
GCC2_CONF="$GCC_COMMON --enable-languages=c,c++ --enable-threads=posix --enable-libstdcxx-filesystem-ts --disable-libstdcxx-pch --enable-libstdcxx-backtrace=no"

DL=$WORK/dl
SRC=$WORK/src
BLD=$WORK/build
STAMPS=$WORK/stamps
LOGS=$WORK/logs
mkdir -p "$DL" "$SRC" "$BLD" "$STAMPS" "$LOGS"
export PATH="$PREFIX/bin:$PATH"

say () { printf '\n=== %s ===\n' "$*"; }
want () { case " $STAGES " in *" all "*|*" $1 "*) return 0;; esac; return 1; }
done_ () { [ -f "$STAMPS/$1" ]; }
mark () { date > "$STAMPS/$1"; }
# run <log> <command...>: the output into $LOGS/<log>.log, its tail on failure.
run () {
	_log=$LOGS/$1.log; shift
	if ! "$@" >>"$_log" 2>&1; then
		echo "FAILED: $* (log: $_log)" >&2
		tail -n 40 "$_log" >&2
		exit 1
	fi
}
sha256 () { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
fetch_url () {
	if command -v curl >/dev/null 2>&1; then curl -fL --retry 3 -o "$2.part" "$1"; else wget -O "$2.part" "$1"; fi
	mv "$2.part" "$2"
}

# The prefix must be writable (sudo once: mkdir + chown, rather than building as root).
if ! mkdir -p "$PREFIX" 2>/dev/null || [ ! -w "$PREFIX" ]; then
	echo "build-onyx-toolchain: $PREFIX is not writable: sudo mkdir -p $PREFIX && sudo chown \$(id -u):\$(id -g) $PREFIX" >&2
	exit 1
fi
for t in make gcc g++ m4 xz patch tar; do
	command -v $t >/dev/null 2>&1 || { echo "build-onyx-toolchain: '$t' is missing (sudo apt install build-essential m4 xz-utils curl patch)" >&2; exit 1; }
done
T0=$(date +%s)

# --- fetch --------------------------------------------------------------------------------------
if want fetch && ! done_ fetch; then
	say "fetch: the source tarballs"
	echo "$SOURCES" | while read -r url sum; do
		[ -n "$url" ] || continue
		f=$DL/$(basename "$url")
		[ -f "$f" ] && [ "$(sha256 "$f")" = "$sum" ] || fetch_url "$url" "$f"
		got=$(sha256 "$f")
		[ "$got" = "$sum" ] || { echo "sha256 mismatch: $f ($got, want $sum)" >&2; exit 1; }
		echo "ok  $(basename "$f")"
	done
	say "unpack"
	for p in $BINUTILS $GCC $NEWLIB $GMP $MPFR $MPC; do
		rm -rf "${SRC:?}/$p"
		tar -C "$SRC" -xf "$DL"/$p.tar.*
	done
	# GMP, MPFR and MPC built in GCC's tree (no host libraries needed)
	ln -s ../$GMP "$SRC/$GCC/gmp"
	ln -s ../$MPFR "$SRC/$GCC/mpfr"
	ln -s ../$MPC "$SRC/$GCC/mpc"
	(cd "$SRC/$GCC" && patch -p1 < "$HERE/patches/gcc-14.2.0-libstdcxx-onyx.patch")
	mark fetch
fi

# --- binutils -----------------------------------------------------------------------------------
if want binutils && ! done_ binutils; then
	say "binutils ($BINUTILS)"
	rm -rf "$BLD/binutils" && mkdir -p "$BLD/binutils" && cd "$BLD/binutils"
	run binutils "$SRC/$BINUTILS/configure" $COMMON_CONF $BINUTILS_CONF
	run binutils make -j"$JOBS" MAKEINFO=true
	run binutils make install-strip MAKEINFO=true
	mark binutils
fi

# --- GCC stage 1: C only, no target headers ------------------------------------------------------
if want gcc1 && ! done_ gcc1; then
	say "GCC stage 1 (C, no headers)"
	rm -rf "$BLD/gcc1" && mkdir -p "$BLD/gcc1" && cd "$BLD/gcc1"
	run gcc1 "$SRC/$GCC/configure" $COMMON_CONF $GCC1_CONF
	run gcc1 make -j"$JOBS" all-gcc all-target-libgcc MAKEINFO=true
	run gcc1 make install-gcc install-target-libgcc MAKEINFO=true
	mark gcc1
fi

# --- newlib --------------------------------------------------------------------------------------
if want newlib && ! done_ newlib; then
	say "newlib ($NEWLIB)"
	rm -rf "$BLD/newlib" && mkdir -p "$BLD/newlib" && cd "$BLD/newlib"
	run newlib env CFLAGS_FOR_TARGET="$TARGET_CFLAGS" "$SRC/$NEWLIB/configure" $COMMON_CONF $NEWLIB_CONF
	# newlib only: libgloss (semihosting crt0, rdimon, nosys) is of no use on Onyx (libonyxposix
	# provides the system calls), and its aarch64 rdimon does not build with a thread-local _reent
	run newlib make -j"$JOBS" all-target-newlib MAKEINFO=true
	run newlib make install-target-newlib MAKEINFO=true
	rm -f "$STAMPS/overlay"			# a fresh newlib install: the overlay again
	mark newlib
fi

# --- the header overlay ----------------------------------------------------------------------------
# libonyxposix's thread headers replace newlib's (which declare nothing on this target); newlib's
# <sys/features.h> becomes <sys/_newlib_features.h>, included by a <sys/features.h> that adds the
# POSIX options Onyx implements (libonyxposix's overlay, its #include_next turned into an include);
# <sys/dirent.h> (newlib's is an #error stub on this target) for libstdc++'s directory iterators.
# The pthread types are ABI-frozen from here on (docs/POSIX-PLAN.md "WP-LIBC resolutions" 7).
OVL_SRC=$ONYX/user/Runtime/libc/posix/include
INC=$PREFIX/$TARGET/include
if want overlay && ! done_ overlay; then
	say "the header overlay (libonyxposix) -> $INC"
	for h in pthread.h sys/_pthreadtypes.h semaphore.h sys/dirent.h sys/features.h; do
		[ -f "$OVL_SRC/$h" ] || { echo "overlay: $OVL_SRC/$h missing (run from an Onyx checkout)" >&2; exit 1; }
	done
	[ -f "$INC/sys/_newlib_features.h" ] || mv "$INC/sys/features.h" "$INC/sys/_newlib_features.h"
	for h in pthread.h sys/_pthreadtypes.h semaphore.h sys/dirent.h; do
		cp "$OVL_SRC/$h" "$INC/$h"
	done
	sed -e 's|#include_next <sys/features.h>|#include <sys/_newlib_features.h>	/* newlib'"'"'s, renamed by the toolchain build */|' \
	    -e 's|_ONYX_SYS_FEATURES_H|_ONYX_TC_SYS_FEATURES_H|g' \
	    "$OVL_SRC/sys/features.h" > "$INC/sys/features.h"
	grep -q '_newlib_features.h' "$INC/sys/features.h" || { echo "overlay: sys/features.h not rewritten" >&2; exit 1; }
	mark overlay
fi

# --- GCC final: C and C++, posix threads, TLS ------------------------------------------------------
if want gcc && ! done_ gcc; then
	say "GCC final (C, C++, threads=posix, TLS)"
	rm -rf "$BLD/gcc" && mkdir -p "$BLD/gcc" && cd "$BLD/gcc"
	run gcc env CFLAGS_FOR_TARGET="$TARGET_CFLAGS" CXXFLAGS_FOR_TARGET="$TARGET_CFLAGS" \
		"$SRC/$GCC/configure" $COMMON_CONF $GCC2_CONF
	run gcc make -j"$JOBS" MAKEINFO=true
	run gcc make install-strip MAKEINFO=true
	mark gcc
fi

# --- final: the build info, the build trees removed -----------------------------------------------
if want final && ! done_ final; then
	say "final"
	{
		echo "aarch64-onyx-elf toolchain, revision $ONYX_TC_REVISION (Onyx, tools/toolchain/build-onyx-toolchain.sh)"
		echo "built $(date -u +%Y-%m-%dT%H:%MZ) on $(uname -sm)"
		echo "sources:"
		echo "$SOURCES" | while read -r url sum; do if [ -n "$url" ]; then echo "  $url  sha256 $sum"; fi; done
		echo "patch: gcc-14.2.0-libstdcxx-onyx.patch"
		echo "binutils: $COMMON_CONF $BINUTILS_CONF"
		echo "newlib:   $COMMON_CONF $NEWLIB_CONF (CFLAGS_FOR_TARGET=$TARGET_CFLAGS)"
		echo "gcc:      $COMMON_CONF $GCC2_CONF"
		echo "overlay:  libonyxposix pthread.h sys/_pthreadtypes.h semaphore.h sys/dirent.h sys/features.h"
	} > "$PREFIX/ONYX-TOOLCHAIN.txt"
	mkdir -p "$PREFIX/share/onyx-toolchain"
	cp "$HERE/patches/gcc-14.2.0-libstdcxx-onyx.patch" "$HERE/build-onyx-toolchain.sh" "$PREFIX/share/onyx-toolchain/"
	[ "$KEEP_BUILD" = 1 ] || { rm -rf "${BLD:?}"/* "${SRC:?}"/*; rm -f "$STAMPS/fetch"; }
	mark final
fi

# --- check: the acceptance (docs/POSIX-PLAN.md §2.3) ----------------------------------------------
if want check; then
	say "acceptance checks"
	fail=0
	GCCV=$("$PREFIX/bin/$TARGET-gcc" -v 2>&1)
	echo "$GCCV" | grep -q '^Thread model: posix' && echo "ok    Thread model: posix" || { echo "FAIL  Thread model: $(echo "$GCCV" | sed -n 's/^Thread model: //p')"; fail=1; }
	CXXCONFIG=$(ls "$PREFIX/$TARGET"/include/c++/*/"$TARGET"/bits/c++config.h 2>/dev/null | head -n 1)
	for m in _GLIBCXX_HAS_GTHREADS _GLIBCXX_USE_CLOCK_MONOTONIC _GLIBCXX_USE_NANOSLEEP _GLIBCXX_USE_SCHED_YIELD \
		 _GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT _GLIBCXX_HAVE_TLS _GLIBCXX_USE_CLOCK_REALTIME _GLIBCXX_HAVE_DIRENT_H; do
		if [ -n "$CXXCONFIG" ] && grep -q "^#define $m 1" "$CXXCONFIG"; then echo "ok    $m"; else echo "FAIL  $m"; fail=1; fi
	done
	grep -q '_REENT_THREAD_LOCAL\|_WANT_REENT_THREAD_LOCAL' "$PREFIX/$TARGET/include/newlib.h" && echo "ok    newlib: _REENT_THREAD_LOCAL (errno per thread)" || { echo "FAIL  newlib without _REENT_THREAD_LOCAL"; fail=1; }
	# native TLS: a __thread access compiles to TPIDR_EL0, not __emutls_get_address
	tmp=$(mktemp -d)
	printf '__thread int v; int f (void) { return v; }\n' > "$tmp/t.c"
	"$PREFIX/bin/$TARGET-gcc" -O2 -S -o "$tmp/t.s" "$tmp/t.c"
	grep -q 'tpidr_el0' "$tmp/t.s" && ! grep -q emutls "$tmp/t.s" && echo "ok    native TLS (mrs tpidr_el0)" || { echo "FAIL  TLS is emulated"; fail=1; }
	# <limits.h> chains to newlib's (PATH_MAX), <pthread.h> is libonyxposix's
	printf '#include <limits.h>\n#include <pthread.h>\nint a[PATH_MAX > 0 && _POSIX_THREADS > 0 ? 1 : -1]; int f (pthread_mutex_t *m) { return pthread_mutex_clocklock (m, CLOCK_MONOTONIC, 0); }\n' > "$tmp/h.c"
	"$PREFIX/bin/$TARGET-gcc" -c -o "$tmp/h.o" "$tmp/h.c" && echo "ok    <limits.h> (PATH_MAX), the pthread overlay" || { echo "FAIL  <limits.h> / <pthread.h>"; fail=1; }
	rm -rf "$tmp"
	echo
	echo "installed: $PREFIX ($(du -sh "$PREFIX" 2>/dev/null | cut -f1)); time: $(( ($(date +%s) - T0) / 60 )) min this run"
	if [ $fail = 0 ]; then echo "ACCEPTANCE: PASS"; else echo "ACCEPTANCE: FAIL"; exit 1; fi
fi
