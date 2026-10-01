#!/bin/sh
# tools/tests/netsurf/otptest.sh -- one-time-code fields (netflix.com's sign-in code: docs/06
# section 45) reduced to local pages, on the PC bench (no network):
#
# - pages/otp-netflix.html: Netflix's <InputPinCodeV2> (React 18, pages/react/): one text
#   field, its text transparent, over its "chrome" (absolute, z-index -1 behind it in the
#   wrapper's stacking context) that draws the six boxes. A click on the boxes focuses the
#   field (the chrome is under it: negative z-index), the digits typed reach React's onChange
#   (a letter dropped), the caret is drawn in the field while typing (a re-render at each
#   key: the boxes built again, the caret placed again), a pasted "98-76 54" gives 987654.
# - pages/otp-boxes.html: six <input maxlength=1>, moving on at input / at keyup (the input
#   event before keyup), Backspace back, keydown filtering (e.key, e.code, e.keyCode), a
#   pasted code shared out by the paste event (clipboardData); maxlength kept; InputEvent's
#   inputType / data; setSelectionRange / selectionStart; beforeinput prevented.
# - pages/otp-react.html: six React-controlled boxes (focus() of the next box through a ref,
#   select(), onPaste), and one invisible field (opacity: 0) over drawn boxes in a <label>.
# - pages/stack-negz.html: the painting order and the hit test of negative z-indexes.
#
#   sh tools/tests/netsurf/otptest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
mkdir -p "$OUT/otp"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
D=$OUT/otp
fail=0
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
# a click at a simulator point (window client coordinates: the page's y + 40)
click() { printf 'move %s %s;wait;down %s %s;wait;wait;up %s %s;' "$1" "$2" "$1" "$2" "$1" "$2"; waits 20; }
# keys: each typed, then some waits (a re-render between)
keys() { for k in "$@"; do printf 'key %s;' "$k"; waits 4; done; }
run() {	# run <page> <sim script> <log> [env...]
	p=$1; s=$2; l=$3; shift 3
	env "$@" SIM_SCREEN=900x700 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
		SIM_ARGS="file://$(realpath "$T/pages/$p")" SIM="${s}exit" \
		timeout 300 "$OUT/build/netsurf" >"$l" 2>&1
}
expect() { if grep -q -F -- "console: $2" "$1"; then echo "  ok    $2"; else echo "  FAIL  $2"; fail=1; fi; }
refuse() { if grep -q -F -- "console: $2" "$1"; then echo "  FAIL  (not expected) $2"; fail=1; else echo "  ok    no \"$2\""; fi; }
noerr() { if grep -q "^JS " "$1"; then echo "  FAIL  script errors:"; grep "^JS " "$1" | head -3; fail=1; else echo "  ok    no script error"; fi; }
# pixel <elsm> <page x> <page y>: its colour "r,g,b" (the page's origin at 4, 68 in the dump)
pixel() {
	python3 - "$1" "$2" "$3" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read(); w, h = struct.unpack('<2i', d[4:12])
x, y = int(sys.argv[2]) + 4, int(sys.argv[3]) + 68
b, g, r = d[20 + (y * w + x) * 4: 20 + (y * w + x) * 4 + 3]
print('%d,%d,%d' % (r, g, b))
PY
}
colour() {	# colour <elsm> <x> <y> <r,g,b> <what>
	c=$(pixel "$1" "$2" "$3")
	if [ "$c" = "$4" ]; then echo "  ok    $5 ($c)"; else echo "  FAIL  $5: $c, not $4"; fail=1; fi
}
# caret <elsm> <x0> <y0> <x1> <y1>: the caret's red pixels in a page rectangle
caret() {
	python3 - "$@" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read(); w, h = struct.unpack('<2i', d[4:12])
x0, y0, x1, y1 = [int(v) for v in sys.argv[2:6]]
n = 0
for y in range(y0 + 68, y1 + 68):
	for x in range(x0 + 4, x1 + 4):
		b, g, r = d[20 + (y * w + x) * 4: 20 + (y * w + x) * 4 + 3]
		n += r > 200 and g < 60 and b < 60
print(n)
PY
}

echo "otp-netflix.html (Netflix's code field: one field over its boxes, React)"
L=$D/netflix-1.log
run otp-netflix.html "$(waits 60)$(click 150 72)$(keys 1 2 a 3)dump $D/netflix-1.elsm;$(keys 4 5 6)$(waits 10)" "$L"
noerr "$L"
expect "$L" "mousedown input#pin"
expect "$L" "focus true"
expect "$L" 'change "12a" -> 12'
expect "$L" "complete 123456"
expect "$L" "shown 123456 value 123456 sel 6,6"
n=$(caret "$D/netflix-1.elsm" 20 20 340 70)
if [ "$n" -gt 10 ]; then echo "  ok    the caret drawn in the field while typing ($n px)"
else echo "  FAIL  no caret in the field ($n px)"; fail=1; fi
L=$D/netflix-2.log
run otp-netflix.html "$(waits 60)$(click 150 72)key 22;$(waits 20)" "$L" SIM_CLIP="98-76 54"
expect "$L" "complete 987654"
expect "$L" "shown 987654 value 987654 sel 6,6"

echo "otp-boxes.html (six boxes: moving on at input / keyup, Backspace, paste, maxlength)"
L=$D/boxes-1.log
# row 1 at page y 8..48 (its boxes 38 px apart from x 20), row 2 at 56..96
run otp-boxes.html "$(waits 40)$(click 36 68)$(keys 1 x 2 3)key 0x08;$(waits 4)key 0x08;$(waits 4)$(keys 7 3 4 5 6)$(click 36 116)$(keys 9 8 7 6 5 4)" "$L"
noerr "$L"
expect "$L" "ready 1 function function"
expect "$L" "r1 input 0 1_____ insertText 1 active=r1-1"
expect "$L" "r1 refused x code=KeyX keyCode=88"
expect "$L" "r1 back to 2 12____"
expect "$L" "r1 back to 1 1_____"
expect "$L" "r1 complete 173456"
expect "$L" "r2 keyup 0 9_____ active=r2-1"
expect "$L" "r2 complete 987654"
L=$D/boxes-2.log
run otp-boxes.html "$(waits 40)$(click 112 68)key 22;$(waits 20)$(click 36 116)key 22;$(waits 20)" "$L" SIM_CLIP="123456"
expect "$L" 'r1 paste "123456" types=text/plain'
expect "$L" "r1 pasted 123456 active=r1-5"
expect "$L" "r2 pasted 123456 active=r2-5"
L=$D/boxes-3.log
run otp-boxes.html "$(waits 40)$(click 100 164)$(keys 1 2 3 4 5 6)key 0x08;$(waits 4)key 22;$(waits 10)$(click 100 212)key X;$(waits 4)key #;$(waits 4)" "$L" SIM_CLIP="abcdef"
expect "$L" 'long 1 insertText "1" true'
expect "$L" 'long 1234 insertText "4" true'
refuse "$L" "long 12345"
expect "$L" 'long 123 deleteContentBackward null true'
expect "$L" 'long 123a insertFromPaste "a" true'
expect "$L" "sel range 2,4"
expect "$L" "sel abXef caret 3,3"
expect "$L" "sel beforeinput refused #"
refuse "$L" "sel abX#ef"

echo "otp-react.html (React-controlled boxes, an invisible field over drawn boxes)"
L=$D/react-1.log
run otp-react.html "$(waits 60)$(click 36 70)$(keys 4 2 7)key 0x08;$(waits 6)$(keys 8 1 9 3)$(click 60 124)$(keys 5 b 5 5)dump $D/react-1.elsm;$(keys 1 2 3)" "$L"
noerr "$L"
expect "$L" 'boxes change 0 "4" insertText -> 4 next'
expect "$L" 'boxes change 1 "2" insertText -> 42 next'
expect "$L" "boxes back 2"
expect "$L" "boxes complete 428193"
refuse "$L" "boxes complete 4271"
expect "$L" "mousedown input#hid"
expect "$L" "hidden focus"
expect "$L" "hidden refused b"
expect "$L" "hidden complete 555123"
colour "$D/react-1.elsm" 147 85 0,0,255 "the drawn box after the digits typed highlighted (blue)"
L=$D/react-2.log
run otp-react.html "$(waits 60)$(click 36 70)key 22;$(waits 20)" "$L" SIM_CLIP="246 810"
expect "$L" "boxes pasted 246810"

echo "stack-negz.html (negative z-index: under the in-flow content)"
for gpu in 0 1; do
	L=$D/negz-$gpu.log
	run stack-negz.html "$(waits 30)dump $D/negz-$gpu.elsm;$(click 50 80)$(click 150 80)" "$L" NS_GPU=$gpu
	colour "$D/negz-$gpu.elsm" 50 40 0,160,0 "NS_GPU=$gpu: the in-flow field over its z-index -1 chrome"
	colour "$D/negz-$gpu.elsm" 150 40 0,0,255 "NS_GPU=$gpu: the chrome beside it"
	colour "$D/negz-$gpu.elsm" 100 110 255,255,255 "NS_GPU=$gpu: a z-index -1 box under its z-index auto parent's background"
	colour "$D/negz-$gpu.elsm" 50 180 255,255,0 "NS_GPU=$gpu: a static block's background over the page's z-index -1 box"
	colour "$D/negz-$gpu.elsm" 150 180 255,160,0 "NS_GPU=$gpu: that box painted (over the page's background)"
	colour "$D/negz-$gpu.elsm" 320 50 128,0,128 "NS_GPU=$gpu: z-index -2"
	colour "$D/negz-$gpu.elsm" 375 50 0,128,128 "NS_GPU=$gpu: z-index -1 over -2"
	colour "$D/negz-$gpu.elsm" 380 190 255,0,255 "NS_GPU=$gpu: a z-index -1 box in an overflow: hidden box"
	colour "$D/negz-$gpu.elsm" 420 190 255,255,255 "NS_GPU=$gpu: clipped by it"
	expect "$L" "clicked field"
	n=$(grep -c "^console: clicked chrome" "$L")
	if [ "$n" = 1 ]; then echo "  ok    the click on the field: the field's; beside it: the chrome's"
	else echo "  FAIL  clicked chrome $n times, not 1"; fail=1; fi
done

[ $fail = 0 ] && echo "all passed"
exit $fail
