#!/bin/sh
# test-webcrypto.sh -- WebCrypto (crypto.subtle on mbedTLS: Source/WebCore/crypto/mbedtls) on the
# posixsim bench (qemu on the PC): wctest (tools/webkit/wctest.cpp, built by BENCH=1
# tools/webkit/build-wctest.sh) loads tools/webkit/tests/crypto1.html, whose script runs
# crypto.subtle against published test vectors (the page says which) and writes "PASS name" or
# "FAIL name: what" per check into its body, then sets its title to "done: N passed, M failed".
# crypto.subtle is asynchronous: wctest turns its run loop until that title (WCTEST_WAIT_TITLE).
# The PASS / FAIL lines are printed (PASS lines only with V=1: there are hundreds); the exit status
# is the number of failures -- not zero either if the page never came to "done:".
# The bench is not the Pi: the Pi is the reference.
#
#   BENCH=1 sh tools/webkit/build-wctest.sh && sh tools/webkit/test-webcrypto.sh
#
# Variables: POSIXSIM_ROOT, POSIXSIM_QEMU (as test-webcore.sh), TIMEOUT (seconds for the whole
# run, default 900: an RSA key is generated, under emulation), V=1 (print the PASS lines too).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../.." && pwd)
: "${POSIXSIM_ROOT:=$HOME/posixsim}"
: "${POSIXSIM_QEMU:=$(command -v qemu-aarch64-static || command -v qemu-aarch64)}"
: "${TIMEOUT:=900}"
export POSIXSIM_ROOT POSIXSIM_QEMU
R=$POSIXSIM_ROOT
[ -x "$R/SD/bin/wctest" ] || { echo "test-webcrypto.sh: no $R/SD/bin/wctest: BENCH=1 sh tools/webkit/build-wctest.sh" >&2; exit 2; }

mkdir -p "$R/SD/tmp" "$R/RAM" "$R/SD/res/fonts"
cp "$HERE/tests/crypto1.html" "$R/SD/tmp/crypto1.html"
cp "$ONYX"/sdcard/res/fonts/*.ttf "$R/SD/res/fonts/"
rm -f "$R/RAM/crypto1.png"

fails=0
start=$(date +%s)
out=$(POSIXSIM_ARGV0="SD:/bin/wctest" WCTEST_WAIT_TITLE="done:" WCTEST_TIMEOUT=$((TIMEOUT - 30)) \
	timeout "$TIMEOUT" "$POSIXSIM_QEMU" "$R/SD/bin/wctest" SD:/tmp/crypto1.html RAM:/crypto1.png 800 600 2>&1)
st=$?
echo "wctest: exit $st in $(( $(date +%s) - start )) s"

# what wctest prints: "title: ...", "elements: N", "text: <the body's text on one line>", "painted: ..."
title=$(printf '%s\n' "$out" | sed -n 's/^title: //p' | head -n 1)
text=$(printf '%s\n' "$out" | sed -n 's/^text: //p' | head -n 1)
# one check per line again: the names are single words, and the messages hold neither word
lines=$(printf '%s\n' "$text" | sed 's/ \(PASS\|FAIL\) /\n\1 /g')
passed=$(printf '%s\n' "$lines" | grep -c '^PASS ')
failed=$(printf '%s\n' "$lines" | grep -c '^FAIL ')
if [ "${V:-0}" = 1 ]; then
	printf '%s\n' "$lines" | grep '^\(PASS\|FAIL\) '
else
	printf '%s\n' "$lines" | grep '^FAIL '
fi
fails=$failed

if [ $st -ne 0 ]; then
	echo "FAIL  wctest ran (exit $st)"
	printf '%s\n' "$out" | grep -v '^text: ' | tail -n 5 | sed 's/^/  | /'
	fails=$((fails + 1))
fi
case "$title" in
"done: $passed passed, $failed failed")
	;;
"done: "*)
	echo "FAIL  the title and the lines agree (title \"$title\", lines: $passed passed, $failed failed)"
	fails=$((fails + 1));;
*)
	echo "FAIL  the page came to an end (title \"$title\": not \"done: ...\")"
	fails=$((fails + 1));;
esac
[ "$passed" -gt 0 ] || { echo "FAIL  no check passed"; fails=$((fails + 1)); }

echo "crypto1.html: $title"
echo "test-webcrypto.sh: $passed passed, $fails failed"
[ $fails -gt 255 ] && fails=255
exit $fails
