#!/bin/sh
# tools/tests/netsurf/acidtest.sh -- the Acid tests on the PC bench (docs/06 section 37):
#
#   1. Acid2 (pages/acid2/: http://acid2.acidtests.org/ as published, its reference.png and
#      reference.html): the test opened at #top as a browser shows it, the face (168 x 168 px
#      below "Hello World!") compared with reference.png pixel by pixel -- identical, no
#      tolerance --, and the whole view with reference.html's (the reference image in a page);
#      composited (the default) and painted by the CPU (NS_GPU=0); then scrolled down and back
#      (the fixed boxes and backgrounds painted again where the scroll moved them).
#   2. Acid3 (pages/acid3/: http://acid3.acidtests.org/ and its support files), served over
#      HTTP by acidsrv.py with the original server's statuses and content types: the score
#      the page reaches must be at least ACID3_MIN; the failing subtests are listed
#      (NS_JSDEBUG: the test's own report of each failure).
#
#   OUT=/tmp/nsbench PORT=8160 sh tools/tests/netsurf/acidtest.sh [acid2|acid3]
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8160}
ACID3_MIN=${ACID3_MIN:-94}
A=$OUT/acid
mkdir -p "$A"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$A/build.log" 2>&1 || { echo "build failed: $A/build.log"; exit 1; }
fail=0
what=${1:-all}

W=$(i=0; while [ $i -lt 30 ]; do printf 'wait;'; i=$((i + 1)); done)
# shoot <url> <steps> <out.png> -- env passes through
shoot() {
	steps=$(echo "$2" | sed "s/@W/$W/g")
	SIM_SCREEN=1000x700 SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$1" SIM="${steps}dump $3.elsm;exit" \
		timeout 300 "$OUT/build/netsurf" >"$3.log" 2>&1
	python3 tools/tests/desktop_sim/shot.py "$3.elsm" "$3" >/dev/null
}

if [ "$what" = all ] || [ "$what" = acid2 ]; then
	echo "---- Acid2"
	P="file://$PWD/$T/pages/acid2"
	shoot "$P/reference.html" "@W@W" "$A/acid2-ref.png"
	for mode in gpu cpu scroll; do
		steps="@W@W@W"
		[ "$mode" = scroll ] && steps="@W@W@Wwheel 400 400 -1;@Wwheel 400 400 -1;@Wwheel 400 400 1;@Wwheel 400 400 1;@W"
		gpu=1
		[ "$mode" = cpu ] && gpu=0
		NS_GPU=$gpu shoot "$P/index.html#top" "$steps" "$A/acid2-$mode.png"
		python3 - "$A/acid2-$mode.png" "$A/acid2-ref.png" "$T/pages/acid2/reference.png" "$mode" <<'EOF' || fail=1
import sys
import numpy as np
from PIL import Image
t = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.int32)
page = np.asarray(Image.open(sys.argv[2]).convert("RGB")).astype(np.int32)
ref = np.asarray(Image.open(sys.argv[3]).convert("RGB")).astype(np.int32)
mode = sys.argv[4]
# where reference.html shows reference.png: the face's place in the view
H, W = page.shape[:2]
at = None
for y in range(H - 168):
    row = page[y]
    for x in np.nonzero((row[:W - 168] == ref[0, 0]).all(axis=1))[0]:
        if (page[y:y + 168, x:x + 168] == ref).all():
            at = (x, y)
            break
    if at:
        break
if at is None:
    print("  FAIL  acid2 (%s): reference.html does not show reference.png" % mode)
    sys.exit(1)
x, y = at
face = t[y:y + 168, x:x + 168]
d = np.abs(face - ref).max(axis=2)
n = int((d > 0).sum())
# the whole view above the window's scroll bars: the page as reference.html's
vw, vh = W - x - 60, H - y - 60
v = np.abs(t[y - 108:y + vh, x - 72:x + vw] - page[y - 108:y + vh, x - 72:x + vw]).max(axis=2)
m = int((v > 0).sum())
ok = n == 0 and m == 0
print("  %s  acid2 (%s): face %d pixels differ from reference.png, view %d from reference.html" %
      ("ok  " if ok else "FAIL", mode, n, m))
if not ok:
    big = np.where(d[..., None] > 0, [255, 0, 255], ref // 2 + 64)
    Image.fromarray(np.concatenate([face, ref, big], axis=1).astype(np.uint8)).resize(
        (168 * 9, 168 * 3), Image.NEAREST).save(sys.argv[1].replace(".png", "-diff.png"))
sys.exit(0 if ok else 1)
EOF
	done
fi

if [ "$what" = all ] || [ "$what" = acid3 ]; then
	echo "---- Acid3"
	python3 $T/acidsrv.py $T/pages/acid3 $PORT >"$A/acidsrv.log" 2>&1 &
	SRV=$!
	trap 'kill $SRV 2>/dev/null' EXIT
	sleep 1
	# the test's own score and log, read from the page once it is over (F5 runs NS_INJECT)
	cat >"$A/acid3-report.js" <<'EOF'
console.log("ACID3-SCORE " + score + " (" + index + " run)");
log.split("\n").forEach(function (l) {
	var m = /^Test (\d+) failed: (.*)/.exec(l);
	if (m) console.log("ACID3-FAIL " + m[1] + ": " + m[2]);
});
EOF
	# (the test runs its 100 subtests on timers, the ones waiting for a support file
	# retried up to 5 s: its report read every ~12 s, the last one counts)
	R="@W@W@W@W@W@W@W@W@W@W@W@W@W@W@W@W@W@W@W@Wkey 0x114;"
	SIM_REALNET=1 NS_JSDEBUG=1 NS_INJECT="$A/acid3-report.js" shoot "http://127.0.0.1:$PORT/" \
		"$R$R$R$R$R@W" "$A/acid3.png"
	kill $SRV 2>/dev/null
	python3 - "$A/acid3.png.log" "$ACID3_MIN" <<'EOF' || fail=1
import re, sys
log = open(sys.argv[1], errors="replace").read()
need = int(sys.argv[2])
# (the last report: the test over, or as far as it got)
last = log.rfind('ACID3-SCORE')
scores = re.findall(r'ACID3-SCORE (\d+)', log[last:] if last >= 0 else '')
fails = re.findall(r'ACID3-FAIL (\d+): (.*)', log[last:] if last >= 0 else '')
for n, why in fails:
    print("        test %s: %s" % (n, why[:150]))
if not scores:
    print("  FAIL  acid3: no score (see %s)" % sys.argv[1]); sys.exit(1)
score = int(scores[-1])
run = re.findall(r'ACID3-SCORE \d+ \((\d+) run\)', log[last:])
run = int(run[-1]) if run else 0
ok = score >= need and run == 100
print("  %s  acid3: %d/100 (at least %d)%s" % ("ok  " if ok else "FAIL", score, need,
      "" if run == 100 else ", only %d subtests run" % run))
sys.exit(0 if ok else 1)
EOF
fi

[ $fail = 0 ] && echo "acidtest: all passed" || echo "acidtest: FAILED"
exit $fail
