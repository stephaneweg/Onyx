#!/bin/sh
# tools/tests/netsurf/findtest.sh -- Jet Browser's find in page, copy and paste, the page's context
# menu (docs/06 §40) on the PC bench, with the local HTTP server and pages/jet-find.html (fixed places:
# the line to select at the page's y 0..40, a link 40..80, the image jet-find.png 80..120, a text field
# 140..170, the words to find from 200, "zebra" at 3000).
#
#   find          Ctrl+F opens the bar (the page 30 px shorter), typing finds as it goes ("1 of 5": case
#                 and accents ignored -- Café, cafe, CAFE, café, cafeteria), Enter / Shift+Enter / F3 /
#                 Shift+F3 step around (wrapping), Match case (2 of them), '*' and '#' literal, a
#                 typographic apostrophe found by a typed one, a match far down scrolled into the middle
#                 of the view, Esc closes the bar and clears the highlights (yellow / orange pixels
#                 counted), Ctrl+F again: the words kept and found again; the highlights composited
#                 (GPU compositing) = painted by the CPU (NS_GPU=0); a big page: the search's time.
#   copy          a line selected with the mouse, Ctrl+C: its exact text (UTF-8) on the clipboard; the
#                 context menu's Copy the same; Copy Link Address; Copy Image: a PNG in RAM:/jet/clip/
#                 with the image's pixels, the clipboard a file (CLIP_FILES) -- what Paint pastes.
#   paste         the context menu's Paste into the field (not focused before), Ctrl+V, Ctrl+A + Ctrl+C,
#                 Ctrl+X in the field (the clipboard's text in and out).
#
#   sh tools/tests/netsurf/findtest.sh            (OUT=/tmp/nsbench PORT=8231 by default)
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8231}
D="$OUT/find"
mkdir -p "$D"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
python3 $T/httpsrv.py $T/pages $PORT > "$D/srv.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 1
export SIM_WRITES="$D/writes" SIM_RAM="$D/ram" SIM_CLIPFILE="$D/clip"
rm -rf "$SIM_WRITES" "$SIM_RAM" "$D"/*.log "$D/clip"
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
W=$(waits 60)
fail=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; fail=1; fi; }
run() {	# run <name> <sim script> [page]: the log in $D/<name>.log
	SIM_REALNET=1 SIM_SCREEN=1024x700 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
		SIM_ARGS="http://127.0.0.1:$PORT/${3:-jet-find.html}" SIM="$2exit" \
		timeout 300 "$OUT/build/netsurf" >"$D/$1.log" 2>&1
}
has() { grep -a -q -F -- "$2" "$D/$1.log"; }
png() { python3 tools/tests/desktop_sim/shot.py "$D/$1.elsm" "$D/$1.png" >/dev/null; }
ctrl() { printf 'mods 1;key %s;mods 0;' "$1"; waits "${2:-15}"; }
typ() { echo "$1" | sed 's/./key &;/g'; waits "${2:-15}"; }
click() { printf 'move %s %s;wait;down %s %s;up %s %s;' "$1" "$2" "$1" "$2" "$1" "$2"; waits "${3:-15}"; }
rclick() { printf 'move %s %s;wait;rdown %s %s;wait;rup %s %s;' "$1" "$2" "$1" "$2" "$1" "$2"; waits "${3:-10}"; }
downs() { i=0; while [ "$i" -lt "$1" ]; do printf 'key 0x101;'; i=$((i + 1)); done; printf 'key 13;'; waits "${2:-20}"; }
clip() { cat "$D/clip" 2>/dev/null; }
# hl <picture> <yellow> <orange>: at least that many yellow / orange pixels (the highlights); "0 0": none
hl() { python3 -c "
import sys
from PIL import Image
im = Image.open('$D/$1.png').convert('RGB')
y = o = 0
for x in range(im.width):
    for yy in range(im.height):
        p = im.getpixel((x, yy))
        if p == (255, 255, 0): y += 1
        elif p == (255, 150, 50): o += 1
print('        (%s: %d yellow, %d orange pixels)' % ('$1', y, o))
ok = (y == 0 and o == 0) if ($2 == 0 and $3 == 0) else (y >= $2 and o >= $3)
sys.exit(0 if ok else 1)"; }
# the window: 960 x 550 (the screen 1024 x 700), the page from y 40; the find bar (shown) at 498..528:
# its field 8..308, previous 314, next 344, Match case 384.., close 924..952; its middle y 513
FY=513

echo "find"
run f1 "${W}$(ctrl 0x06)$(typ cafe 30)dump $D/find-cafe.elsm;key 13;$(waits 10)key 13;$(waits 10)mods 2;key 13;key 13;key 13;mods 0;$(waits 10)key 0x112;$(waits 10)mods 2;key 0x112;mods 0;$(waits 10)$(click 395 $FY 30)$(click 395 $FY 20)$(ctrl 0x06 5)$(typ 'a*b')$(ctrl 0x06 5)$(typ '#')$(ctrl 0x06 5)$(typ "l'ete")$(ctrl 0x06 5)$(typ zebra 40)dump $D/find-zebra.elsm;key 27;$(waits 30)dump $D/find-closed.elsm;$(ctrl 0x06 40)dump $D/find-again.elsm;"
png find-cafe; png find-zebra; png find-closed; png find-again
check "Ctrl+F, 'cafe' typed: found as it goes ('c' 9, 'ca' 5...), 1 of 5 (Café, cafe, CAFE, café, cafeteria)" \
	"has f1 'ONYX-FIND \"c\" dir=0 case=0: 1 of 9' && has f1 'ONYX-FIND \"cafe\" dir=0 case=0: 1 of 5'"
check "the bar says '1 of 5'" "has f1 'ONYX-FINDBAR 1 of 5'"
check "the highlights: 4 yellow and 1 orange (the current) in the page" \
	"hl find-cafe 200 50"
check "Enter, Enter: 2, 3 of 5; Shift+Enter x3: 2, 1, around to 5; F3: around to 1; Shift+F3: 5;" \
	"grep -a 'ONYX-FIND \"cafe\" dir=' '$D/f1.log' | sed 's/.*: //; s/ (.*//' | tr '\n' ' ' | grep -q '^1 of 5 2 of 5 3 of 5 2 of 5 1 of 5 5 of 5 1 of 5 5 of 5 1 of 2 1 of 5 '"
check "  then Match case: 'cafe' 2 of them (cafe, cafeteria); off again: 5" \
	"has f1 'ONYX-FIND \"cafe\" dir=0 case=1: 1 of 2'"
check "'a*b' and '#' are literal: 1 of 1 each" \
	"has f1 'ONYX-FIND \"a*b\" dir=0 case=0: 1 of 1' && has f1 'ONYX-FIND \"#\" dir=0 case=0: 1 of 1'"
check "\"l'ete\" typed: l’été and l'été (the typographic apostrophe, the accents)" \
	"has f1 'ete\" dir=0 case=0: 1 of 2'"
check "'zebra' at y 3000: the view moved to show it (in its middle, or the page's end: scrollY 2500..3000)" \
	"grep -a 'console: scrolly' '$D/f1.log' | tail -1 | awk '{ exit !(\$3 >= 2500 && \$3 <= 3000) }'"
check "Esc: the bar closed, the highlights cleared" \
	"has f1 'ONYX-FIND closed' && hl find-closed 0 0"
check "Ctrl+F again: the words kept, found again (1 of 1, highlighted)" \
	"[ \$(grep -a -c 'ONYX-FIND \"zebra\" dir=0' '$D/f1.log') -ge 2 ] && hl find-again 0 50"
# the composited frames (GPU compositing) against the CPU painting, the highlights in them
run f2 "${W}$(ctrl 0x06)$(typ cafe 30)key 13;$(waits 20)dump $D/find-gpu.elsm;"
NS_GPU=0 run f3 "${W}$(ctrl 0x06)$(typ cafe 30)key 13;$(waits 20)dump $D/find-cpu.elsm;"
png find-gpu; png find-cpu
check "the highlights composited = painted by the CPU (the page's pixels)" \
	"python3 -c \"
from PIL import Image, ImageChops
a = Image.open('$D/find-gpu.png').convert('RGB'); b = Image.open('$D/find-cpu.png').convert('RGB')
import sys; sys.exit(0 if ImageChops.difference(a, b).getbbox() is None else 1)\""
# a big page: 4000 paragraphs, 'e' everywhere -- the search's time (the Pi: ~15 x the PC)
python3 - "$T/pages/find-big.html" <<'PY'
import sys
w = 'Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. '
open(sys.argv[1], 'w').write('<!doctype html><title>big</title>' + ''.join('<p>%d %s</p>\n' % (i, w * 2) for i in range(4000)))
PY
run f4 "${W}${W}$(ctrl 0x06)$(typ e 60)$(typ t 60)key 13;$(waits 10)" find-big.html
rm -f "$T/pages/find-big.html"
check "a big page (4000 paragraphs): 'e' and 'et' found" \
	"has f4 'ONYX-FIND \"e\" dir=0 case=0: 1 of ' && has f4 'ONYX-FIND \"et\"'"
grep -a 'ONYX-FIND "e' "$D/f4.log" | sed 's/^/        /'

echo "copy and paste"
# the line selected by a drag, Ctrl+C; then the context menu's Copy over it
run c1 "${W}move 12 60;wait;down 12 60;move 100 60;wait;move 400 60;wait;up 400 60;$(waits 10)$(ctrl 0x03 10)dump $D/copy-sel.elsm;"
png copy-sel
check "a line selected with the mouse, Ctrl+C: 'Select me: déjà vu 42' (UTF-8) on the clipboard" \
	"has c1 'SIM-CLIPBOARD type=1' && [ \"\$(clip)\" = 'Select me: déjà vu 42' ]"
rm -f "$D/clip"
run c2 "${W}move 12 60;wait;down 12 60;move 100 60;wait;move 400 60;wait;up 400 60;$(waits 10)$(rclick 100 60)dump $D/menu-sel.elsm;$(downs 1)"
png menu-sel
check "the context menu over the selection: Copy -- the same text" \
	"has c2 'ONYX-CONTEXT command 5' && [ \"\$(clip)\" = 'Select me: déjà vu 42' ]"
run c3 "${W}$(rclick 40 100)dump $D/menu-link.elsm;$(downs 3)"
png menu-link
check "a link's menu: Copy Link Address" \
	"has c3 'flags=0x1' && [ \"\$(clip)\" = 'http://127.0.0.1:$PORT/jet-plain.txt?from=find' ]"
run c4 "${W}$(rclick 40 140)dump $D/menu-image.elsm;$(downs 3 30)"
png menu-image
check "an image's menu: Copy Image -- RAM:/jet/clip/image-1.png, the clipboard a file (type 2)" \
	"has c4 'ONYX-CLIPBOARD image 64x40 RAM:/jet/clip/image-1.png' && has c4 'SIM-CLIPBOARD type=2' && [ \"\$(clip)\" = 'RAM:/jet/clip/image-1.png' ]"
check "the PNG has the image's pixels (its alpha too)" \
	"python3 -c \"
from PIL import Image, ImageChops
a = Image.open('$SIM_RAM/jet/clip/image-1.png').convert('RGBA'); b = Image.open('$T/pages/jet-find.png').convert('RGBA')
import sys; sys.exit(0 if a.size == b.size and ImageChops.difference(a, b).getbbox() is None else 1)\""
run c5 "${W}$(rclick 40 140)$(downs 3 30)$(rclick 40 140)$(downs 3 30)$(rclick 40 140)$(downs 3 30)"
check "copied again and again: image-2.png, then image-1.png (two files at most)" \
	"has c5 'RAM:/jet/clip/image-2.png' && [ \"\$(clip)\" = 'RAM:/jet/clip/image-1.png' ] && [ \$(ls '$SIM_RAM/jet/clip' | wc -l) = 2 ]"
# Paint (the desktop simulator's build: wtk, the stand-in kernel) pastes the copied image: Ctrl+V
PS="$OUT/paintsim"
mkdir -p "$PS/obj"
PCXX="g++ -std=gnu++17 -O1 -w -I user -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
if [ ! -x "$PS/paint" ] || [ -n "$(find user/wtk user/Apps/paint user/img tools/tests/desktop_sim/fakekapi.cpp -newer "$PS/paint" 2>/dev/null | head -1)" ]; then
	# (wait for these only: a bare wait also waits for the HTTP server, forever)
	pids=
	for f in user/wtk/*.cpp; do $PCXX -c "$f" -o "$PS/obj/$(basename "$f" .cpp).o" & pids="$pids $!"; done
	wait $pids
	rm -f "$PS/libwtk.a"; ar rcs "$PS/libwtk.a" "$PS"/obj/*.o
	$PCXX -o "$PS/paint" tools/tests/desktop_sim/fakekapi.cpp user/Apps/paint/main.cpp "$PS/libwtk.a" 2>"$PS/build.log"
fi
paint() {	# paint <name> <clipboard>: Paint opened, Ctrl+V, its window dumped
	SIM_CLIPFILE= SIM_CLIP="$2" SIM_POS=0,0 SIM_SCREEN=1024x700 SIM="wait;wait;mods 1;key 0x16;mods 0;wait;wait;dump $D/$1.elsm;exit" \
		timeout 60 "$PS/paint" > "$D/$1.log" 2>&1
	png "$1"
}
paint paint-empty ""
paint paint-paste "files:RAM:/jet/clip/image-1.png"
check "Paint, Ctrl+V: the copied image pasted (its opaque pixels in Paint's window)" \
	"python3 -W ignore -c \"
from PIL import Image
src = Image.open('$T/pages/jet-find.png').convert('RGBA')
want = set(p[:3] for p in src.getdata() if p[3] == 255)
def hits(f):
    return sum(1 for p in Image.open(f).convert('RGB').getdata() if p in want)
a, b = hits('$D/paint-paste.png'), hits('$D/paint-empty.png')
print('        (Paint: %d of the image\'s opaque pixels after the paste, %d before)' % (a, b))
import sys; sys.exit(0 if a >= 1500 and b < 100 else 1)\""
# the field: the menu's Paste (the field not focused), Ctrl+V, Ctrl+A + Ctrl+C, Ctrl+X
SIM_CLIP="pasted é text" run p1 "${W}$(rclick 100 195)dump $D/menu-field.elsm;$(downs 1 30)$(ctrl 0x16 30)$(ctrl 0x01 5)$(ctrl 0x03 20)"
png menu-field
check "the field's menu: Paste (Cut and Copy greyed) -- 'pasted é text' in the field" \
	"has p1 'flags=0x10' && has p1 'console: field [pasted é text]'"
check "Ctrl+V: pasted again at the caret" "has p1 'console: field [pasted é textpasted é text]'"
check "Ctrl+A, Ctrl+C in the field: its text on the clipboard" "[ \"\$(clip)\" = 'pasted é textpasted é text' ]"
SIM_CLIP="cut me" run p2 "${W}$(click 100 195)$(ctrl 0x16 30)$(ctrl 0x01 5)$(ctrl 0x18 30)"
check "Ctrl+X in the field: emptied, its text on the clipboard" \
	"has p2 'console: field [cut me]' && has p2 'console: field []' && [ \"\$(clip)\" = 'cut me' ]"
echo "  (screenshots: $D/*.png)"
[ $fail = 0 ] && echo "all passed" || exit 1
