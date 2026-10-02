#!/bin/sh
# fetch.sh -- fetch the pinned WebKit revision for the Onyx port (docs/08-WEBKIT-PORT.md) as a
# sparse, shallow git checkout OUTSIDE the Onyx repository, then apply Onyx's patch series
# (tools/webkit/patches/*.patch, `git am`: one commit per patch on the branch `onyx`).
#
#   sh tools/webkit/fetch.sh                    # -> $WEBKIT_DIR (default /home/user/webkit, else
#                                               #    $HOME/webkit when /home/user is not there)
#   WEBKIT_DIR=<dir> sh tools/webkit/fetch.sh   # elsewhere
#   TESTS=0 sh tools/webkit/fetch.sh            # without JSTests/stress, es6 and wasm/*
#   REAPPLY=1 sh tools/webkit/fetch.sh          # reset the checkout to the pinned revision and
#                                               # apply the patches again (local changes lost)
#
# Only what step 1 (WTF + JavaScriptCore + the jsc shell) needs is checked out: the top-level
# CMake files, Source/cmake, Source/WTF, Source/JavaScriptCore, Source/bmalloc (its headers are
# still included with USE_SYSTEM_MALLOC), Source/ThirdParty/skia is NOT (the sysroot's libskia.a is
# the same sources), and the JSTests subsets the bench runs. The whole repository is many GB;
# this is about 300 MB. Later steps widen the cone (SPARSE_EXTRA="Source/WebCore ...").
#
# To work on the port: commit in the checkout's branch `onyx`, then regenerate the series with
#   sh tools/webkit/export-patches.sh
#
# WebKit is LGPL-2.1+ / BSD-2 (docs/LICENSING.md): the patches keep every WebKit licence notice;
# the files Onyx adds inside WebKit's tree carry WebKit's usual BSD-2 header ("Onyx contributors").
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
. "$HERE/revision.sh"
if [ -z "${WEBKIT_DIR:-}" ]; then
	if [ -d /home/user ]; then WEBKIT_DIR=/home/user/webkit; else WEBKIT_DIR=$HOME/webkit; fi
fi
: "${TESTS:=1}"
: "${SPARSE_EXTRA:=}"

PATHS="Source/cmake Source/WTF Source/JavaScriptCore Source/bmalloc Tools/Scripts/webkitperl"
# (JSTests/wasm: the directories test-jsc.sh runs and the files they load; the whole of it is 190 MB)
WASM_TESTS="stress js-api noJIT function-tests references function-references gc regress self-test branch-hints extended-const modules v8/resources"
[ "$TESTS" = 1 ] && PATHS="$PATHS JSTests/stress JSTests/es6 JSTests/resources $(for d in $WASM_TESTS; do printf 'JSTests/wasm/%s ' "$d"; done)"
PATHS="$PATHS $SPARSE_EXTRA"

if [ ! -d "$WEBKIT_DIR/.git" ]; then
	echo "fetch.sh: WebKit $WEBKIT_REVISION -> $WEBKIT_DIR (sparse, shallow)"
	mkdir -p "$WEBKIT_DIR"
	git -C "$WEBKIT_DIR" init -q
	git -C "$WEBKIT_DIR" remote add origin "$WEBKIT_REPO"
	git -C "$WEBKIT_DIR" config core.sparseCheckout true
fi
# the cone: the root's files and those of each listed directory's parents come with it
git -C "$WEBKIT_DIR" sparse-checkout set --cone $PATHS
if ! git -C "$WEBKIT_DIR" cat-file -e "$WEBKIT_REVISION^{commit}" 2>/dev/null; then
	git -C "$WEBKIT_DIR" fetch -q --depth 1 --filter=blob:none origin "$WEBKIT_REVISION"
fi

if git -C "$WEBKIT_DIR" rev-parse -q --verify refs/heads/onyx >/dev/null && [ "${REAPPLY:-0}" != 1 ]; then
	echo "fetch.sh: the branch onyx exists already (REAPPLY=1 to start again from the pinned revision)"
	git -C "$WEBKIT_DIR" checkout -q onyx
	exit 0
fi
git -C "$WEBKIT_DIR" checkout -q -B onyx "$WEBKIT_REVISION"
git -C "$WEBKIT_DIR" reset -q --hard "$WEBKIT_REVISION"
n=0
for p in "$HERE"/patches/*.patch; do
	[ -f "$p" ] || continue
	GIT_COMMITTER_NAME="Onyx contributors" GIT_COMMITTER_EMAIL="onyx@localhost" \
	GIT_COMMITTER_DATE="2026-10-02T00:00:00Z" \
		git -C "$WEBKIT_DIR" am -q --keep-cr "$p" || {
			echo "fetch.sh: $p does not apply (git -C $WEBKIT_DIR am --abort)" >&2; exit 1; }
	n=$((n + 1))
done
echo "fetch.sh: $WEBKIT_DIR at $WEBKIT_REVISION + $n Onyx patches (branch onyx)"
