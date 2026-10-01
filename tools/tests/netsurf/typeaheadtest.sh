#!/bin/sh
# tools/tests/netsurf/typeaheadtest.sh -- Wikipedia's search box (Codex's TypeaheadSearch, the
# Minerva and Vector skins: docs/06 section 42) reduced to local pages, on the PC bench:
#
# - pages/stack-typeahead.html: the suggestions menu (absolute, z-index 50) inside a relative
#   wrapper with z-index auto, inside the page's stacking context (relative, z-index 0), the
#   page's relative content after it: the menu painted over the content (its background
#   opaque), the clicks its own; its footer (absolute in the menu, under a static scrolling
#   list) painted, clickable, the list not scrollable for it; a z-index 999 box inside its own
#   stacking context (z-index 1) stays under a later z-index 2 one; an overlay with
#   pointer-events: none lets the clicks through, its part with auto takes them;
#   getComputedStyle answers pointer-events. Both with the GPU compositor (NS_GPU=1) and
#   without (NS_GPU=0).
# - pages/form-scripted.html: forms a script made (as Vue renders them): Enter in the field
#   submits the form (the submit event, then the GET with the hidden field); with a default
#   button, Enter clicks it (its click handler, its name=value sent); Object.prototype.toString
#   of the DOM's objects ("[object HTMLInputElement]": Vue's reactive() leaves them alone).
#
#   sh tools/tests/netsurf/typeaheadtest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
mkdir -p "$OUT/typeahead"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
D=$OUT/typeahead
fail=0
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
# a click at a simulator point (window client coordinates: the page's y + 40)
click() { printf 'move %s %s;wait;down %s %s;wait;wait;up %s %s;' "$1" "$2" "$1" "$2" "$1" "$2"; waits 20; }
run() {	# run <page> <sim script> <log> [env...]
	p=$1; s=$2; l=$3; shift 3
	env "$@" SIM_SCREEN=900x900 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
		SIM_ARGS="file://$(realpath "$T/pages/$p")" SIM="${s}exit" \
		timeout 300 "$OUT/build/netsurf" >"$l" 2>&1
}
expect() { if grep -q -F -- "console: $2" "$1"; then echo "  ok    $2"; else echo "  FAIL  $2"; fail=1; fi; }
refuse() { if grep -q -F -- "console: $2" "$1"; then echo "  FAIL  (not expected) $2"; fail=1; else echo "  ok    no \"$2\""; fi; }
# pixel <elsm> <page x> <page y>: its colour "r,g,b" (the dump is the window: the page's
# origin at 4, 68 in it)
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

for gpu in 0 1; do
	echo "stack-typeahead.html (NS_GPU=$gpu: the menu over the page, its footer, pointer-events)"
	L=$D/stack-$gpu.log
	# the menu over the content, its footer, the content beside it, the overlay's tab, the
	# overlay over the content (pointer-events: none)
	run stack-typeahead.html "$(waits 40)dump $D/stack-$gpu.elsm;$(click 100 240)$(click 100 320)$(click 600 200)$(click 430 55)$(click 700 300)" "$L" NS_GPU=$gpu
	python3 tools/tests/desktop_sim/shot.py "$D/stack-$gpu.elsm" "$D/stack-$gpu.png" >/dev/null
	colour "$D/stack-$gpu.elsm" 100 200 0,0,255 "the menu (z-index 50) over the later content"
	colour "$D/stack-$gpu.elsm" 100 280 255,255,0 "its footer, absolute under the scrolling list"
	colour "$D/stack-$gpu.elsm" 302 100 0,0,255 "the list not scrollable for the footer (no scroll bar)"
	colour "$D/stack-$gpu.elsm" 500 200 255,0,0 "the content beside the menu"
	colour "$D/stack-$gpu.elsm" 450 340 255,160,0 "a z-index 999 box in its own context under a later z-index 2"
	expect "$L" "pointer-events none auto"
	expect "$L" "menu clicked"
	expect "$L" "footer clicked"
	expect "$L" "tab clicked"
	refuse "$L" "veil clicked"
	n=$(grep -c "^console: content clicked" "$L")
	if [ "$n" = 2 ]; then echo "  ok    the content clicked through the overlay, not under the menu"
	else echo "  FAIL  content clicked $n times, not 2"; fail=1; fi
done

echo "form-scripted.html (forms a script made: Enter submits)"
L=$D/form-1.log
run form-scripted.html "$(waits 40)$(click 100 60)key a;wait;key b;$(waits 10)key 13;$(waits 60)" "$L"
expect "$L" "Object.prototype.toString [object HTMLInputElement] [object Document] [object Event]"
expect "$L" "submit event one ab"
expect "$L" "submitted search=ab title=Special:Search go=null"
L=$D/form-2.log
run form-scripted.html "$(waits 40)$(click 100 100)key x;wait;key 13;$(waits 60)" "$L"
expect "$L" "default button clicked x"
expect "$L" "submitted q2=x btn=go"

[ $fail = 0 ] && echo "all passed"
exit $fail
