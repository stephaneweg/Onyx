#!/bin/sh
# export-patches.sh -- regenerate tools/webkit/patches/*.patch from the WebKit checkout's branch
# `onyx` (one patch per commit on top of the pinned revision, git format-patch: the commit message
# is the patch's header: what and why).
#
#   (in the checkout: commit the change on the branch onyx)
#   sh tools/webkit/export-patches.sh
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/revision.sh"
if [ -z "${WEBKIT_DIR:-}" ]; then
	if [ -d /home/user ]; then WEBKIT_DIR=/home/user/webkit; else WEBKIT_DIR=$HOME/webkit; fi
fi
rm -f "$HERE"/patches/*.patch
mkdir -p "$HERE/patches"
git -C "$WEBKIT_DIR" format-patch -q --no-signature --zero-commit -o "$HERE/patches" "$WEBKIT_REVISION..onyx"
ls "$HERE/patches"
