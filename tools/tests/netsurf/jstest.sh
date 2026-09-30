#!/bin/sh
# tools/tests/netsurf/jstest.sh -- NetSurf's JavaScript (QuickJS) on the PC bench: the DOM API
# (pages/js-dom.html), the browser's events to the scripts (pages/js-events.html: clicks, a
# menu over the page, a prevented link, a checkbox, typing, Enter, Escape, the wheel) and a
# runaway recursion (pages/js-recursion.html). The pages log with console.log; NS_JSDEBUG=1
# puts it on stderr ("console: ..."), checked here. Builds as shot.sh (OUT, default
# /tmp/nsbench). Exit status 0: every check passed.
#
#   sh tools/tests/netsurf/jstest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
fail=0
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
# a click at a simulator point (window client coordinates: the page's y + 40)
click() { printf 'move %s %s;wait;down %s %s;wait;wait;up %s %s;' "$1" "$2" "$1" "$2" "$1" "$2"; waits 30; }
run() {	# run <page> <sim script> <log>
	SIM_SCREEN=900x900 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
	SIM_ARGS="file://$(realpath "$T/pages/$1")" SIM="$2exit" \
		timeout 300 "$OUT/build/netsurf" >"$3" 2>&1
}
expect() {	# expect <log> <text>
	if grep -q -F -- "console: $2" "$1"; then echo "  ok    $2"; else echo "  FAIL  $2"; fail=1; fi
}
refuse() {	# refuse <log> <text>
	if grep -q -F -- "console: $2" "$1"; then echo "  FAIL  (not expected) $2"; fail=1; else echo "  ok    no \"$2\""; fi
}

echo "js-dom.html (the DOM API)"
L=$OUT/js-dom.log
run js-dom.html "$(waits 150)" "$L"
n_ok=$(grep -c "^console: OK " "$L")
grep "^console: FAIL \|^JS " "$L" | sed 's/^/  FAIL  /'
if grep -q "^console: FAIL " "$L" || [ "$n_ok" -lt 26 ]; then fail=1; fi
echo "  $n_ok checks passed"

echo "js-events.html (the browser's events)"
L=$OUT/js-events.log
run js-events.html "$(waits 100)$(click 80 75)$(click 80 120)$(click 60 230)$(click 28 268)$(click 60 312)key a;wait;key b;$(waits 30)key 13;$(waits 30)key 27;$(waits 30)move 300 300;wheel 300 300 -3;$(waits 60)" "$L"
for s in "DOMContentLoaded interactive" "load complete 20,20" "menu open" "nav link clicked" \
	 "link prevented" "checkbox true" "input a" "input ab" "submit ab" "key Escape" "scroll"; do
	expect "$L" "$s"
done
refuse "$L" "under clicked"

echo "js-recursion.html (a runaway recursion)"
L=$OUT/js-recursion.log
run js-recursion.html "$(waits 60)" "$L"
expect "$L" "recursion: RangeError"

echo "js-fetch.html (fetch, XMLHttpRequest, Headers, Response)"
L=$OUT/js-fetch.log
run js-fetch.html "$(waits 200)" "$L"
expect "$L" "fetch: done"
if grep -q '^console: FAIL \|^JS ' "$L"; then
	echo "  FAIL: $(grep -c '^console: FAIL ' "$L") check(s) failed"; grep '^console: FAIL \|^JS ' "$L" | head -10; fail=1
else
	echo "  ok: $(grep -c '^console: OK ' "$L") checks"
fi

echo "js-hover.html (mouseover / mouseenter / mouseleave / mousemove)"
L=$OUT/js-hover.log
run js-hover.html "$(waits 40)move 300 110;$(waits 20)move 100 130;$(waits 20)move 100 390;$(waits 20)" "$L"
expect "$L" "hover mouseenter outer"
expect "$L" "hover mouseenter inner"
expect "$L" "hover over inner from outer"
expect "$L" "hover mouseleave inner"
expect "$L" "hover mouseleave outer"
expect "$L" "hover mouseenter other"
expect "$L" "hover mousemove"
refuse "$L" "hover mouseleave outer.*inner"

echo "js-hovercss.html (CSS :hover, the styles made again)"
L=$OUT/js-hovercss.log
run js-hovercss.html "$(waits 40)move 100 130;$(waits 30)move 100 390;$(waits 30)" "$L"
expect "$L" "hovercss before none"
expect "$L" "hovercss over block"
expect "$L" "hovercss after none"

echo "js-storage.html (localStorage kept across two runs)"
L=$OUT/js-storage.log
run js-storage.html "$(waits 40)" "$L"
a=$(sed -n 's/^console: storage count //p' "$L")
run js-storage.html "$(waits 40)" "$L"
b=$(sed -n 's/^console: storage count //p' "$L")
expect "$L" "storage proxy proxy"
expect "$L" "storage keys ok"
if [ -n "$a" ] && [ "$b" = "$((a + 1))" ]; then echo "  ok    kept: $a -> $b"; else echo "  FAIL  kept: '$a' -> '$b'"; fail=1; fi

echo "js-module.html (ES modules; CSS.supports and element.style from libcss)"
L=$OUT/js-module.log
run js-module.html "$(waits 60)" "$L"
expect "$L" "module a 4 b meta true"
expect "$L" "module dynamic c-loaded"
expect "$L" "module inline 42"
expect "$L" "module order before DOMContentLoaded"
expect "$L" "css supports color true bad false unknown false"
expect "$L" "css supports cond true selector true badsel false"
expect "$L" "style in true false"
expect "$L" "style invalid 0 valid 1 red"

echo "js-reactreveal.html (React 18's streaming reveal: comments, insertBefore null)"
L=$OUT/js-reactreveal.log
run js-reactreveal.html "$(waits 60)" "$L"
expect "$L" "reveal content true"
expect "$L" "reveal fallback gone true"
expect "$L" "reveal after kept true"

# Onyx: the HTML5 checks -- each page logs "OK <area> name" / "FAIL <area> name" and ends
# with "<area> done N" (N checks)
html5page() {	# html5page <page> <area> <what>
	echo "$1 ($3)"
	L=$OUT/${1%.html}.log
	run "$1" "$(waits 150)" "$L"
	grep "^console: FAIL $2 " "$L" | sed 's/^console: /  /'
	if grep -q "^console: FAIL $2 " "$L" || ! grep -q "^console: $2 done" "$L"; then
		echo "  FAIL  ($2: not all run: $L)"; fail=1
	else
		echo "  ok: $(grep -c "^console: OK $2 " "$L") checks"
	fi
}
html5page js-html5.html html5 "the parser's DOM: fragments, namespaces, templates, DOMParser; messaging"

[ "$fail" = 0 ] && echo "all passed" || echo "FAILED (logs: $OUT/js-*.log)"
exit "$fail"
