#!/bin/sh
# build-all.sh -- the C smoke ports of the POSIX layer (docs/POSIX-PLAN.md §3.4; docs/03,
# "Building a third-party library for Onyx"): the sysroot (libonyxposix), then SQLite, mbedTLS,
# libxml2 and curl as static libraries in it, and their tools in out/ports/bin:
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
for p in $ports; do
	echo "=== $p"
	sh "$here/$p/build.sh"
done
