#!/bin/sh
# fetch.sh -- install the prebuilt aarch64-onyx-elf toolchain (WP-TC; docs/03 §1.1) from the
# repository stephaneweg/onyx-toolchain, its sha256 checked. Linux x86_64 hosts, WSL included (on
# Windows: run it in WSL).
#
#   sh tools/toolchain/fetch.sh                        # -> /opt/toolchains/aarch64-onyx-elf-14.2
#   PREFIX=$HOME/x-tools sh tools/toolchain/fetch.sh   # -> $HOME/x-tools/aarch64-onyx-elf-14.2
#   VERSION=aarch64-onyx-elf-14.2 FORCE=1 ...          # another version / reinstall
#
# The repository holds, per version, <version>/<version>-linux-x86_64.tar.xz split into parts
# below GitHub's file limit (<tarball>.part-00, -01, ...: `split -b 90M -d -a 2`), SHA256SUMS
# (the whole tarball's and each part's), and sources/: the exact source tarballs, the GCC patch
# and build-onyx-toolchain.sh (GPL: the toolchain's sources go with its binaries). It is fetched
# with git (a shallow clone: the user's credentials, or a git proxy, apply as for any clone);
# REPO=<url or local clone> fetches from elsewhere. To build the toolchain instead:
# sh tools/toolchain/build-onyx-toolchain.sh (about 30 minutes on 4 cores).
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

: "${REPO:=https://github.com/stephaneweg/onyx-toolchain}"
: "${VERSION:=aarch64-onyx-elf-14.2}"
: "${PREFIX:=/opt/toolchains}"
HOST=linux-x86_64
TARBALL=$VERSION-$HOST.tar.xz

case "$(uname -s)-$(uname -m)" in
Linux-x86_64) ;;
*) echo "fetch.sh: the prebuilt toolchain is for Linux x86_64 (WSL on Windows); here: $(uname -sm) -- build it: sh tools/toolchain/build-onyx-toolchain.sh" >&2; exit 1;;
esac
if [ -x "$PREFIX/$VERSION/bin/aarch64-onyx-elf-gcc" ] && [ "${FORCE:-0}" != 1 ]; then
	echo "fetch.sh: $PREFIX/$VERSION is already installed (FORCE=1 to reinstall)"
	"$PREFIX/$VERSION/bin/aarch64-onyx-elf-gcc" --version | head -n 1
	exit 0
fi
if ! mkdir -p "$PREFIX" 2>/dev/null || [ ! -w "$PREFIX" ]; then
	echo "fetch.sh: $PREFIX is not writable: sudo mkdir -p $PREFIX && sudo chown \$(id -u):\$(id -g) $PREFIX (or PREFIX=...)" >&2
	exit 1
fi
command -v git >/dev/null 2>&1 || { echo "fetch.sh: git is missing" >&2; exit 1; }
sha256 () { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
echo "fetch.sh: $REPO ($VERSION)"
if [ -d "$REPO/$VERSION" ]; then
	src=$REPO				# a local clone
else
	# only the requested version's tarball (the sources stay on the server: sparse checkout)
	if git clone --depth 1 --filter=blob:none --sparse "$REPO" "$tmp/repo" 2>/dev/null &&
	   git -C "$tmp/repo" sparse-checkout set "$VERSION"; then
		:
	else				# an older git, or a server without partial clone: all of it
		rm -rf "$tmp/repo"
		git clone --depth 1 "$REPO" "$tmp/repo"
	fi
	src=$tmp/repo
fi
d=$src/$VERSION
[ -f "$d/SHA256SUMS" ] || { echo "fetch.sh: no $VERSION/SHA256SUMS in $REPO" >&2; exit 1; }
want=$(awk -v f="$TARBALL" '$2 == f || $2 == "*" f { print $1 }' "$d/SHA256SUMS")
[ -n "$want" ] || { echo "fetch.sh: $TARBALL is not listed in SHA256SUMS" >&2; exit 1; }
set -- "$d/$TARBALL".part-*
[ -f "$1" ] || { echo "fetch.sh: no $TARBALL.part-* in $d" >&2; exit 1; }
for p in "$@"; do
	pw=$(awk -v f="$(basename "$p")" '$2 == f || $2 == "*" f { print $1 }' "$d/SHA256SUMS")
	[ -z "$pw" ] || [ "$(sha256 "$p")" = "$pw" ] || { echo "fetch.sh: $(basename "$p"): sha256 mismatch" >&2; exit 1; }
done
cat "$@" > "$tmp/$TARBALL"
got=$(sha256 "$tmp/$TARBALL")
if [ "$got" != "$want" ]; then
	echo "fetch.sh: $TARBALL: sha256 mismatch: $got (want $want)" >&2
	exit 1
fi
echo "fetch.sh: sha256 ok ($want); unpacking into $PREFIX"
rm -rf "${PREFIX:?}/$VERSION"
tar -C "$PREFIX" -xJf "$tmp/$TARBALL"
"$PREFIX/$VERSION/bin/aarch64-onyx-elf-gcc" -v 2>&1 | grep -E '^(gcc version|Thread model)'
echo "fetch.sh: installed $PREFIX/$VERSION -- the Onyx build finds it in /opt/toolchains (elsewhere: put $PREFIX/$VERSION/bin on the PATH)"
