# onyx-env.sh -- the environment for building third-party code for Onyx with autotools or a plain
# Makefile, against the POSIX sysroot (libonyxposix: docs/03, "Building a third-party library
# for Onyx"). Source it:
#
#   . tools/onyx-env.sh [sysroot]          # default: $ONYX_SYSROOT, else <onyx>/out/sysroot-onyx
#                                          # (aarch64-onyx-elf) or <onyx>/out/sysroot (aarch64-none-elf)
#   ./configure --host=$ONYX_HOST --prefix=$ONYX_SYSROOT --disable-shared --enable-static
#
# Exports CC CXX AR RANLIB STRIP CFLAGS CXXFLAGS LDFLAGS PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
# ONYX_SYSROOT ONYX_HOST. A configure test that must RUN a program cannot (cross build): give
# its answer as a cache variable (ac_cv_...=yes).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

#
# The toolchain (docs/03 §1): $ONYX_TOOLCHAIN_PREFIX if set; else WP-TC's aarch64-onyx-elf- when it
# is installed (on the PATH or in /opt/toolchains/aarch64-onyx-elf-14.2: native TLS, real C++
# threads); else the interim aarch64-none-elf-. Each has its own sysroot (the objects differ):
# <onyx>/out/sysroot-onyx for aarch64-onyx-elf, <onyx>/out/sysroot for aarch64-none-elf.

# ONYX_ROOT: the checkout (needed when sourced from a POSIX sh script: no BASH_SOURCE there)
_onyx_here=${ONYX_ROOT:-$(cd "$(dirname "${BASH_SOURCE:-$0}")/.." 2>/dev/null && pwd)}
if [ -z "$ONYX_TOOLCHAIN_PREFIX" ]; then
	if command -v aarch64-onyx-elf-gcc >/dev/null 2>&1 || [ -x /opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-gcc ]; then
		ONYX_TOOLCHAIN_PREFIX=aarch64-onyx-elf-
	else
		ONYX_TOOLCHAIN_PREFIX=aarch64-none-elf-
	fi
fi
if ! command -v "${ONYX_TOOLCHAIN_PREFIX}gcc" >/dev/null 2>&1; then
	for d in /opt/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin /opt/toolchains/aarch64-onyx-elf-14.2/bin; do
		[ -x "$d/${ONYX_TOOLCHAIN_PREFIX}gcc" ] && PATH=$d:$PATH && break
	done
fi
[ -n "$1" ] && ONYX_SYSROOT=$1
if [ -z "$ONYX_SYSROOT" ]; then
	case $ONYX_TOOLCHAIN_PREFIX in
	aarch64-onyx-elf-) ONYX_SYSROOT=$_onyx_here/out/sysroot-onyx;;
	*) ONYX_SYSROOT=$_onyx_here/out/sysroot;;
	esac
fi
[ -f "$ONYX_SYSROOT/lib/onyx.specs" ] || [ -n "$ONYX_ENV_QUIET" ] || echo "onyx-env: no sysroot at $ONYX_SYSROOT -- make -C user/Runtime/libc/posix install PREFIX=$ONYX_TOOLCHAIN_PREFIX SYSROOT=$ONYX_SYSROOT" >&2

ONYX_HOST=${ONYX_TOOLCHAIN_PREFIX%-}
CC=${ONYX_TOOLCHAIN_PREFIX}gcc
CXX=${ONYX_TOOLCHAIN_PREFIX}g++
AR=${ONYX_TOOLCHAIN_PREFIX}ar
RANLIB=${ONYX_TOOLCHAIN_PREFIX}ranlib
STRIP=${ONYX_TOOLCHAIN_PREFIX}strip
# onyx-cc.specs: __unix__ defined, -pthread accepted (libonyxposix's sysroot; older ones lack it)
_onyx_ccspecs=
[ -f "$ONYX_SYSROOT/lib/onyx-cc.specs" ] && _onyx_ccspecs="-specs=$ONYX_SYSROOT/lib/onyx-cc.specs"
CFLAGS="-mcpu=cortex-a72 $_onyx_ccspecs -O2 -ffunction-sections -fdata-sections -fno-pic -fno-pie -isystem $ONYX_SYSROOT/include -DFD_SETSIZE=1024"
CXXFLAGS="$CFLAGS"
LDFLAGS="-specs=$ONYX_SYSROOT/lib/onyx.specs -L$ONYX_SYSROOT/lib"
PKG_CONFIG_LIBDIR=$ONYX_SYSROOT/lib/pkgconfig
PKG_CONFIG_SYSROOT_DIR=
export PATH ONYX_SYSROOT ONYX_HOST ONYX_TOOLCHAIN_PREFIX CC CXX AR RANLIB STRIP CFLAGS CXXFLAGS LDFLAGS \
       PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
unset _onyx_here _onyx_ccspecs
