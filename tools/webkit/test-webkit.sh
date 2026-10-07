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
#   WK2TEST_GPU=1 sh tools/webkit/test-webkit.sh    # the same pages through the compositor (the bench has
#                                                   # no GPU: gpucomp's CPU path, the kernel surfaces as files)
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
rm -f "$R"/.shm/surface-*				# (the compositor's surfaces of earlier runs)
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

# 4. the compositor (WK2TEST_GPU=1: the page's layers composited; here by gpucomp's CPU path)
gpu0=${WK2TEST_GPU:-}
# 4a. animations paused at a known time: the software path's picture, the compositor's, compared.
# (Edges drawn through a transform are filtered by one and painted by the other: a few pixels differ.)
WK2TEST_GPU=0; export WK2TEST_GPU
run animsw /wktest/anim-paused.html
[ $st -eq 0 ] && ok "paused animations, software: wk2test ran" || ko "paused animations, software: wk2test" "exit $st"
WK2TEST_GPU=1; export WK2TEST_GPU
run animgpu /wktest/anim-paused.html
[ $st -eq 0 ] && ok "paused animations, compositor: wk2test ran" || ko "paused animations, compositor: wk2test" "exit $st"
case "$out" in *"web: gpu: compositing on"*) ok "paused animations: the compositor drew the page";; *) ko "paused animations: the compositor" "no 'compositing on' line";; esac
if [ -f "$R/RAM/animsw.png" ] && [ -f "$R/RAM/animgpu.png" ]; then
	# (the software picture is not blank where the layers are; then the two pictures)
	python3 "$HERE/tests/checkpng.py" "$R/RAM/animsw.png" 60,80=98,146,216 270,470=249,228,134 ink=20,520,500,40
	fails=$((fails + $?))
	python3 "$HERE/tests/checkpng.py" "$R/RAM/animgpu.png" "same=$R/RAM/animsw.png,24,1.5" ink=20,520,500,40
	fails=$((fails + $?))
else
	ko "paused animations: the pictures" "missing"
fi
# 4b. animations that run: frames are composited, nothing is painted for them (the 2 s lines of the
# log), and most frames need no update of the page (the layers keep the animations)
WK2TEST_HOLD=7000; export WK2TEST_HOLD
run animrun /wktest/anim-running.html
unset WK2TEST_HOLD
[ $st -eq 0 ] && ok "running animations: wk2test ran and ended cleanly" || ko "running animations: wk2test" "exit $st"
lines=$(echo "$out" | grep -E 'web: gpu: [0-9.]+ s: [0-9]+ frames')
# (ten frames at least in a line: the bench's UI process answers a frame every 100 ms)
if echo "$lines" | grep -Eq ' [1-9][0-9]+ frames \([0-9]+ without a page update\), 0 tiles painted, 0 rects repainted, 0 px'; then
	ok "running animations: frames composited with nothing painted"
else
	ko "running animations: frames with nothing painted" "no such line"
fi
if echo "$lines" | grep -Eq ' frames \([1-9][0-9]+ without a page update\)'; then
	ok "running animations: frames without an update of the page"
else
	ko "running animations: frames without an update of the page" "no such line"
fi
# 4c. scrolling under a fixed header, a sticky side bar and a fixed button: the three are layers that
# are moved; the scroll paints the tiles that come into view and nothing again in those already
# painted. (The page waits 2.5 s before the scroll: the log's first 2 s line is the load's.)
WK2TEST_SCROLL=20; WK2TEST_SCROLL_DELAY=2500; export WK2TEST_SCROLL WK2TEST_SCROLL_DELAY
WK2TEST_GPU=0; export WK2TEST_GPU
run scrollsw /wktest/scroll-fixed.html
[ $st -eq 0 ] && ok "scroll, software: wk2test ran" || ko "scroll, software: wk2test" "exit $st"
WK2TEST_GPU=1; export WK2TEST_GPU
run scrollgpu /wktest/scroll-fixed.html
unset WK2TEST_SCROLL WK2TEST_SCROLL_DELAY
[ $st -eq 0 ] && ok "scroll, compositor: wk2test ran" || ko "scroll, compositor: wk2test" "exit $st"
lines=$(echo "$out" | grep -E 'web: gpu: [0-9.]+ s: [0-9]+ frames' | tail -n +2)
if [ -n "$lines" ] && ! echo "$lines" | grep -qv ', 0 rects repainted,'; then
	ok "scroll: nothing painted again in the tiles already painted"
else
	ko "scroll: nothing painted again" "$(echo "$lines" | grep -v ', 0 rects repainted,' | head -n 1 | cut -c1-120)"
fi
if echo "$lines" | grep -Eq ' [1-9][0-9]* tiles painted,'; then
	ok "scroll: the tiles that came into view were painted"
else
	ko "scroll: new tiles" "none painted"
fi
if [ -f "$R/RAM/scrollsw.png" ] && [ -f "$R/RAM/scrollgpu.png" ]; then
	python3 "$HERE/tests/checkpng.py" "$R/RAM/scrollgpu.png" "same=$R/RAM/scrollsw.png,24,0.5"
	fails=$((fails + $?))
else
	ko "scroll: the pictures" "missing"
fi
# 4d. a page that is costly to rasterise, scrolled quickly: with the compositor its tiles are
# rasterised from pictures on the app cores (the bench's: host threads, POSIXSIM_CORES of them,
# 2 by default) and the main thread; the picture must be the software path's.
# (The wheel, then the page put at a known place, and time for the last tiles: the bench is slow.)
WK2TEST_SCROLL=40; WK2TEST_SCROLL_TO=5000; WK2TEST_SETTLE=2500; WK2TEST_TIMEOUT=150; export WK2TEST_SCROLL WK2TEST_SCROLL_TO WK2TEST_SETTLE WK2TEST_TIMEOUT
WK2TEST_GPU=0; export WK2TEST_GPU
run rastersw /wktest/raster-stress.html
[ $st -eq 0 ] && ok "raster stress, software: wk2test ran" || ko "raster stress, software: wk2test" "exit $st"
WK2TEST_GPU=1; export WK2TEST_GPU
run rastergpu /wktest/raster-stress.html
rasterout=$out
[ $st -eq 0 ] && ok "raster stress, compositor: wk2test ran and ended cleanly" || ko "raster stress, compositor: wk2test" "exit $st"
# (the bench's cores are as the Pi's: a kernel call made on one ends the program, with the slot)
case "$out" in *"made a kernel call"*) ko "raster stress: no kernel call on an app core" "one was made: see the posixsim: appcore line";; *) ok "raster stress: no kernel call on an app core";; esac
if [ "${POSIXSIM_CORES:-2}" != 0 ]; then
	# 4e. a worker that fails in the middle of a batch (ONYX_CORES_TEST, onyxcores.c; the bench's kernel
	# stops the job as the Pi's does: POSIXSIM_CORE_FAULT=1). A kernel call on the core, a worker stuck in
	# a loop: the job is done again by the main thread, the picture is right, the cores are given back.
	# The same with malloc's lock taken: nobody can go on, the web process says so and ends.
	POSIXSIM_CORE_FAULT=1; export POSIXSIM_CORE_FAULT
	for how in call stall; do
		ONYX_CORES_TEST=$how; export ONYX_CORES_TEST
		run raster$how /wktest/raster-stress.html
		[ $st -eq 0 ] && ok "a worker fails ($how): wk2test ran and ended cleanly" || ko "a worker fails ($how): wk2test" "exit $st"
		case "$out" in *"an app core faulted while it rasterised (core "*"painting on 1 core from now on"*) ok "a worker fails ($how): said, the main thread goes on alone";; *) ko "a worker fails ($how): the log" "no 'an app core faulted' line";; esac
		if [ -f "$R/RAM/rastersw.png" ] && [ -f "$R/RAM/raster$how.png" ]; then
			python3 "$HERE/tests/checkpng.py" "$R/RAM/raster$how.png" "same=$R/RAM/rastersw.png,24,0.5"
			fails=$((fails + $?))
		else
			ko "a worker fails ($how): the pictures" "missing"
		fi
	done
	ONYX_CORES_TEST=lock; WK2TEST_TIMEOUT=20; export ONYX_CORES_TEST WK2TEST_TIMEOUT
	run rasterlock /wktest/raster-stress.html
	case "$out" in *"an app core faulted while it rasterised ("*"and it held malloc's lock"*"this process ends"*) ok "a worker dies with malloc's lock: said, the web process ends (no frozen page)";; *) ko "a worker dies with malloc's lock" "no 'this process ends' line";; esac
	unset ONYX_CORES_TEST POSIXSIM_CORE_FAULT
fi
if [ "${POSIXSIM_CORES:-2}" != 0 ]; then
	# 4f. the app cores' self test (ONYX_WEB_GPU_CORETEST; SD:/etc/web-gpu-coretest on the Pi): its
	# stages one by one on a core, then the page's raster jobs one at a time -- and a stage that
	# faults (ONYX_CORES_TEST=call: the worker's 5th job makes a kernel call) said with its number.
	ONYX_WEB_GPU_CORETEST=1; export ONYX_WEB_GPU_CORETEST
	run rastertest /wktest/raster-stress.html
	[ $st -eq 0 ] && ok "core test: wk2test ran and ended cleanly" || ko "core test: wk2test" "exit $st"
	n=$(echo "$out" | grep -c 'gpu: core test [0-9]* .*: ok in ')
	[ "$n" -ge 12 ] && ok "core test: $n stages and jobs ok" || ko "core test: the stages" "$n ok lines"
	case "$out" in *"core test"*"WRONG"*|*"core test"*"FAULT"*) ko "core test: a stage failed" "see the lines";; *) ok "core test: no stage failed";; esac
	if [ -f "$R/RAM/rastersw.png" ] && [ -f "$R/RAM/rastertest.png" ]; then
		python3 "$HERE/tests/checkpng.py" "$R/RAM/rastertest.png" "same=$R/RAM/rastersw.png,24,0.5"
		fails=$((fails + $?))
	else
		ko "core test: the pictures" "missing"
	fi
	ONYX_CORES_TEST=call; POSIXSIM_CORE_FAULT=1; export ONYX_CORES_TEST POSIXSIM_CORE_FAULT
	run rastertestf /wktest/raster-stress.html
	case "$out" in *": FAULT at breadcrumb "*) ok "core test: a stage that faults is said, with its breadcrumb";; *) ko "core test: a stage that faults" "no FAULT line";; esac
	[ $st -eq 0 ] && ok "core test, a fault: wk2test ran and ended cleanly" || ko "core test, a fault: wk2test" "exit $st"
	unset ONYX_WEB_GPU_CORETEST ONYX_CORES_TEST POSIXSIM_CORE_FAULT
fi
unset WK2TEST_SCROLL WK2TEST_SCROLL_TO WK2TEST_SETTLE WK2TEST_TIMEOUT
if [ "${POSIXSIM_CORES:-2}" != 0 ]; then
	out=$rasterout
	case "$out" in *"web: gpu: painting on "[23]" cores"*) ok "raster stress: the app cores were taken";; *) ko "raster stress: the app cores" "no 'painting on N cores' line";; esac
	if echo "$out" | grep -Eq 'compositing off .* [1-9][0-9]+ raster jobs on the cores'; then
		ok "raster stress: the tiles were rasterised on the cores"
	else
		ko "raster stress: raster jobs" "none"
	fi
fi
if [ -f "$R/RAM/rastersw.png" ] && [ -f "$R/RAM/rastergpu.png" ]; then
	python3 "$HERE/tests/checkpng.py" "$R/RAM/rastergpu.png" "same=$R/RAM/rastersw.png,24,0.5" ink=30,30,700,500
	fails=$((fails + $?))
else
	ko "raster stress: the pictures" "missing"
fi
if [ -n "$gpu0" ]; then WK2TEST_GPU=$gpu0; export WK2TEST_GPU; else unset WK2TEST_GPU; fi

echo "test-webkit.sh: $fails failed"
exit $fails
