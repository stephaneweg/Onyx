#!/bin/sh
# test-webkit.sh -- WebKit2 on the posixsim bench (qemu on the PC): wk2test (tools/webkit/wk2test.cpp,
# built by BENCH=1 tools/webkit/build-wk2test.sh) is the UI process and, started again by WebKit with a
# role argument, the web and the network process: three processes of one program over Onyx's IPC
# (the bench's: Linux socket pairs). It loads tools/webkit/tests/page1.html three ways --
#   a data: URL (no network process load), file: (the network process reads it with curl),
#   http: from a local Python server (curl over a socket) --
# and each time checks what it prints (the load, the title, the text through a script run in the
# page) and, for page1, the picture's pixels (checkpng.py: the same points as test-webcore.sh); then
# that the program ended cleanly and left no process behind. PASS / FAIL lines; the exit status is
# the number of failures. The bench is not the Pi: the Pi is the reference.
#
#   BENCH=1 sh tools/webkit/build-wk2test.sh && sh tools/webkit/test-webkit.sh
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../.." && pwd)
: "${POSIXSIM_ROOT:=$HOME/posixsim}"
: "${POSIXSIM_QEMU:=$(command -v qemu-aarch64-static || command -v qemu-aarch64)}"
: "${PORT:=18431}"
export POSIXSIM_ROOT POSIXSIM_QEMU
R=$POSIXSIM_ROOT
[ -x "$R/SD/bin/wk2test" ] || { echo "test-webkit.sh: no $R/SD/bin/wk2test: BENCH=1 sh tools/webkit/build-wk2test.sh" >&2; exit 2; }

# The pages in SD:/wktest ("/tmp" is RAM:/tmp for an Onyx program); the card's fonts.
mkdir -p "$R/SD/wktest" "$R/RAM" "$R/SD/res/fonts"
cp "$HERE"/tests/*.html "$R/SD/wktest/"
cp "$ONYX"/sdcard/res/fonts/*.ttf "$R/SD/res/fonts/"

fails=0
ok () { echo "PASS  $1"; }
ko () { echo "FAIL  $1 ($2)"; fails=$((fails + 1)); }

# run <name> <url>: wk2test on the URL, its output in $out, its status in $st
run () {
	rm -f "$R/RAM/$1.png"
	start=$(date +%s)
	out=$(POSIXSIM_ARGV0="SD:/bin/wk2test" timeout "${TIMEOUT:-300}" "$POSIXSIM_QEMU" "$R/SD/bin/wk2test" "$2" "RAM:/$1.png" 800 600 2>&1)
	st=$?
	echo "$out" | sed 's/^/  | /'
	echo "wk2test $1: exit $st in $(( $(date +%s) - start )) s"
	sleep 1
	left=$(pgrep -f "$R/SD/bin/wk2test" | wc -l)
	[ "$left" -eq 0 ] || pkill -f "$R/SD/bin/wk2test"
}

# page1's checks: what wk2test printed, then the picture
check_page1 () {
	[ $st -eq 0 ] && ok "$1: wk2test ran and ended cleanly" || ko "$1: wk2test" "exit $st"
	[ "$left" -eq 0 ] && ok "$1: no process left behind" || ko "$1: processes left behind" "$left"
	case "$out" in *"loaded: "*) ok "$1: the page loaded (UI, web and network processes)";; *) ko "$1: the load" "no loaded line";; esac
	case "$out" in *"title: Onyx WebCore test"*) ok "$1: the title";; *) ko "$1: the title" "not found";; esac
	case "$out" in *"Hello from WebCore on Onyx"*) ok "$1: the text (a script in the page, its answer)";; *) ko "$1: the text" "not found";; esac
	case "$out" in *"script: ran, 6 * 7 = 42"*) ok "$1: the page's own script changed the DOM";; *) ko "$1: the page's script" "not run";; esac
	case "$out" in *"été, Ελληνικά, Кириллица"*) ok "$1: UTF-8 text";; *) ko "$1: UTF-8 text" "not found";; esac
	if [ -f "$R/RAM/$1.png" ]; then
		python3 "$HERE/tests/checkpng.py" "$R/RAM/$1.png" \
			50,50=255,0,0 150,50=0,128,0 250,50=0,0,255 \
			50,125=10,20,30 150,125=40,50,60 250,125=70,80,90 \
			50,175=200,100,0 200,175=0,100,200 \
			405,5=0,255,255 450,50=128,0,128 \
			405,115=255,128,0 450,160=0,0,0 \
			675,50=255,128,128 \
			ink=10,220,400,40 ink=10,265,500,100 ink=10,400,500,40
		fails=$((fails + $?))
	else
		ko "$1: the picture" "no $R/RAM/$1.png"
	fi
}

# 1. data: -- WebCore's own scheme handling, no file or socket read by the network process
run data 'data:text/html,<title>data%20test</title><body style="margin:0;background:rgb(255,0,0)">hello from a data URL'
[ $st -eq 0 ] && ok "data: wk2test ran and ended cleanly" || ko "data: wk2test" "exit $st"
case "$out" in *"text: hello from a data URL"*) ok "data: the text";; *) ko "data: the text" "not found";; esac
if [ -f "$R/RAM/data.png" ]; then
	python3 "$HERE/tests/checkpng.py" "$R/RAM/data.png" 400,300=255,0,0
	fails=$((fails + $?))
else
	ko "data: the picture" "no $R/RAM/data.png"
fi

# 2. file: -- the network process reads the card's file through curl
run file /wktest/page1.html
check_page1 file

# 3. http: -- a local server; curl in the network process over a socket
( cd "$HERE/tests" && exec python3 -m http.server "$PORT" --bind 127.0.0.1 ) >/dev/null 2>&1 &
srv=$!
sleep 1
run http "http://127.0.0.1:$PORT/page1.html"
kill $srv 2>/dev/null
check_page1 http

echo "test-webkit.sh: $fails failed"
exit $fails
