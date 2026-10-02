#!/bin/sh
# build-all.sh -- the C smoke ports of the POSIX layer (docs/POSIX-PLAN.md §3.4; docs/03,
# "Building a third-party library for Onyx"): the sysroot (libonyxposix), then SQLite, mbedTLS,
# libxml2 and curl as static libraries in it, and their tools in out/ports/bin (out/ports-onyx/bin
# with WP-TC's aarch64-onyx-elf, the default when it is installed: tools/onyx-env.sh):
#   sqlite3.elf  xmllint.elf  curl.elf
# `make -C user/bin ports` runs it and copies the tools to user/bin (make stage puts them on the
# card as /bin/sqlite3, /bin/xmllint, /bin/curl).
#
#   sh tools/ports/build-all.sh [sqlite] [mbedtls] [libxml2] [curl]     (default: all four)
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
here=$(cd "$(dirname "$0")" && pwd)
ports=${*:-sqlite mbedtls libxml2 curl}
set --			# (onyx-env.sh reads $1 as a sysroot)
# the toolchain, the sysroot and the output, decided once for every port (common.sh, onyx-env.sh)
ONYX_ROOT=$(cd "$here/../.." && pwd)
ONYX_ENV_QUIET=1 . "$ONYX_ROOT/tools/onyx-env.sh"
case $ONYX_TOOLCHAIN_PREFIX in
aarch64-onyx-elf-) : "${PORTS_OUT:=$ONYX_ROOT/out/ports-onyx}";;
*) : "${PORTS_OUT:=$ONYX_ROOT/out/ports}";;
esac
export ONYX_TOOLCHAIN_PREFIX ONYX_SYSROOT PORTS_OUT
echo "ports: $ONYX_TOOLCHAIN_PREFIX, sysroot $ONYX_SYSROOT, out $PORTS_OUT"
for p in $ports; do
	echo "=== $p"
	sh "$here/$p/build.sh"
done
# PORTS_COPY_TO=<dir>: the tools made, copied there (user/bin's `make ports`)
if [ -n "$PORTS_COPY_TO" ]; then
	for t in sqlite3 xmllint curl; do
		if [ -f "$PORTS_OUT/bin/$t.elf" ]; then cp "$PORTS_OUT/bin/$t.elf" "$PORTS_COPY_TO/$t.elf"; fi
	done
fi
