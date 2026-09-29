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

[ "$fail" = 0 ] && echo "all passed" || echo "FAILED (logs: $OUT/js-*.log)"
exit "$fail"
