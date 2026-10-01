#!/bin/sh
# tools/tests/netsurf/iframetest.sh -- iframes and the messaging between windows on the PC bench
# (docs/06 section 26):
#  - pages/frames-api.html (file: URLs, same origin): the parser's and a script's frames as windows
#    (contentWindow, contentDocument, window.length, window[i], parent, top, frameElement, name),
#    srcdoc, about:blank, a src changed, a frame removed, the parent's boxes made again (the frames
#    kept), a frame in a frame, sibling frames, sandbox, postMessage with the structured clone and
#    the targetOrigin checks, MessageChannel ports transferred (in the transfer list, in the value),
#    closed, BroadcastChannel across frames, window.open to a frame name;
#  - pages/frames-input.html: a click on a checkbox, focus and typing, Enter and the wheel inside a
#    frame, a link targeting the frame by name;
#  - pages/recaptcha-outer.html over two local servers (two origins, wssrv.py on PORT and PORT+1):
#    a mimic of reCAPTCHA v2's anchor and challenge frames -- cross-origin WindowProxy limits, ports
#    transferred to two other realms, the checkbox clicked in the anchor, the challenge shown, its
#    tile and "Verify" clicked, the token posted back, the hidden textarea filled, the callback run.
# Builds as jstest.sh (OUT, default /tmp/nsbench). Exit status 0: every check passed.
#
#   sh tools/tests/netsurf/iframetest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8134}
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
fail=0
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
# a click at a simulator point (window client coordinates: the page's y + 40)
click() { printf 'move %s %s;wait;down %s %s;wait;wait;up %s %s;' "$1" "$2" "$1" "$2" "$1" "$2"; waits 30; }
run() {	# run <url> <sim script> <log> [realnet]
	SIM_REALNET=${4:-} SIM_SCREEN=900x900 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
	SIM_ARGS="$1" SIM="$2exit" timeout 300 "$OUT/build/netsurf" >"$3" 2>&1
	[ $? = 0 ] || { echo "  FAIL  NetSurf ended badly (see $3)"; fail=1; }
}
expect() {	# expect <log> <text>
	if grep -a -q -F -- "console: $2" "$1"; then echo "  ok    $2"; else echo "  FAIL  $2"; fail=1; fi
}
refuse() {	# refuse <log> <text>
	if grep -a -q -F -- "console: $2" "$1"; then echo "  FAIL  (not expected) $2"; fail=1; else echo "  ok    no \"$2\""; fi
}

echo "frames-api.html (windows, documents, postMessage, MessageChannel, BroadcastChannel)"
L=$OUT/frames-api.log
run "file://$(realpath $T/pages/frames-api.html)" "$(waits 400)" "$L"
grep -a "^console: FAIL frames \|^JS " "$L" | sed 's/^console: /  /'
n_ok=$(grep -a -c "^console: OK frames " "$L")
if grep -a -q "^console: FAIL frames \|^JS " "$L" || ! grep -a -q "^console: frames done" "$L" || [ "$n_ok" -lt 54 ]; then
	echo "  FAIL  ($n_ok checks passed: $L)"; fail=1
else
	echo "  ok: $n_ok checks"
fi

echo "frames-input.html (the mouse, the keyboard, the wheel in a frame; a link to a frame's name)"
L=$OUT/frames-input.log
run "file://$(realpath $T/pages/frames-input.html)" \
	"$(waits 60)$(click 156 118)$(click 60 120)key a;wait;key b;$(waits 20)key 13;$(waits 20)move 150 200;wheel 150 200 -3;$(waits 30)$(click 25 250)$(waits 30)" "$L"
for s in "framein load 1 va" "framein a checkbox true" "framein a focus" "framein a input a" "framein a input ab" \
	 "framein a enter" "framein a scrolled" "framein load 2 vnav"; do
	expect "$L" "$s"
done
refuse "$L" "framein outer click f"

echo "recaptcha-outer.html (a reCAPTCHA v2 mimic: two origins, the anchor and challenge frames)"
python3 $T/wssrv.py $T/pages $PORT > "$OUT/ifsrv.log" 2>&1 &
SRV=$!
python3 $T/wssrv.py $T/pages $((PORT + 1)) > "$OUT/ifsrv2.log" 2>&1 &
SRV2=$!
trap 'kill $SRV $SRV2 2>/dev/null' EXIT
sleep 1
L=$OUT/recaptcha.log
# the anchor's checkbox at (44, 117) in the page; the challenge (shown at 20,170): a tile at (70, 250),
# "Verify" at (355, 435)
run "http://127.0.0.1:$PORT/recaptcha-outer.html" \
	"$(waits 120)$(click 44 157)$(waits 30)$(click 70 290)$(click 355 475)$(waits 60)" "$L" 1
for s in "recap ready anchor anchor" "recap ready bframe bframe" \
	 "recap cross-origin nodoc SecurityError href-SecurityError parent function open" \
	 "recap anchor sees SecurityError href-SecurityError top len2" \
	 "recap anchor init ports 2 source parent true" "recap bframe init ports 2" \
	 "recap bframe anchor says hello anchor" "recap anchor clicked" "recap challenge shown" \
	 "recap bframe challenge given" "recap bframe tile t0" "recap bframe verified" \
	 "recap anchor solved tok-t0-ok" "recap anchor checked" "recap challenge hidden" \
	 "recap callback tok-t0-ok at /recaptcha-outer.html" "recap textarea tok-t0-ok"; do
	expect "$L" "$s"
done
refuse "$L" "recap anchor got a message for another origin"
refuse "$L" "recap bframe verify without a challenge"
if grep -a -q "^JS " "$L"; then echo "  FAIL  script errors:"; grep -a "^JS " "$L" | head -5; fail=1; fi

[ "$fail" = 0 ] && echo "all passed" || echo "FAILED (logs: $OUT/frames-*.log, $OUT/recaptcha.log)"
exit "$fail"
