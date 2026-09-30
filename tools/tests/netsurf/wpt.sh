#!/bin/sh
# tools/tests/netsurf/wpt.sh -- web-platform-tests (testharness.js tests) in the PC NetSurf: the
# WPT directories asked for are fetched (a sparse git clone, once, into $OUT/wpt), served over
# http on 127.0.0.1 (python's http.server), and each test page is run in NetSurf with a
# testharnessreport.js that logs every subtest's result; the pass rate per file and in all.
#
#   sh tools/tests/netsurf/wpt.sh [dir|file ...]     (default: shadow-dom)
#
# WAITS (default 250) ~20 ms turns per page; PORT (default 8431). Logs in $OUT/wpt-logs/.
# Tests needing testdriver (real input), reftests (-ref, -crash) and manual ones are skipped.
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8431}
W=$OUT/wpt
mkdir -p "$OUT/wpt-logs"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
[ $# -gt 0 ] || set -- shadow-dom
if [ ! -d "$W/.git" ]; then
	git clone -q --depth 1 --filter=blob:none --sparse https://github.com/web-platform-tests/wpt.git "$W" || exit 1
fi
git -C "$W" sparse-checkout set resources common dom/events/resources html/resources "$@" >/dev/null 2>&1
# the report: each subtest's result on the console
cat > "$W/resources/testharnessreport.js" <<'EOF'
add_completion_callback(function (tests, status) {
	var names = ['PASS', 'FAIL', 'TIMEOUT', 'NOTRUN', 'PRECONDITION_FAILED'];
	for (var i = 0; i < tests.length; i++)
		console.log('WPT ' + names[tests[i].status] + ' ' + tests[i].name);
	console.log('WPT DONE ' + status.status + ' ' + tests.length);
});
EOF
(cd "$W" && exec python3 -m http.server "$PORT" --bind 127.0.0.1 >/dev/null 2>&1) &
SRV=$!
sleep 1
WS=$(i=0; while [ $i -lt "${WAITS:-250}" ]; do printf 'wait;'; i=$((i + 1)); done)
rm -f "$OUT/wpt-logs/.sum"
for a in "$@"; do
	if [ -d "$W/$a" ]; then find "$W/$a" -name '*.html' | sort; else echo "$W/$a"; fi
done | grep -v -e '-ref\.html$' -e 'crash' -e '-manual\.html$' -e '/support/' -e '/resources/' |
while read -r f; do
	grep -q 'testharness.js' "$f" || continue
	grep -q 'testdriver' "$f" && continue
	rel=${f#$W/}
	n=$(echo "$rel" | sed 's#[^a-zA-Z0-9]#_#g')
	L=$OUT/wpt-logs/$n.log
	SIM_REALNET=1 NS_JSDEBUG=1 SIM_SCREEN=900x900 SIM_SLEEP=1 SIM_POS=0,0 \
		SIM_ARGS="http://127.0.0.1:$PORT/$rel" SIM="${WS}exit" timeout 200 "$OUT/build/netsurf" >"$L" 2>&1
	st=$?
	p=$(grep -c '^console: WPT PASS ' "$L")
	t=$(grep -c '^console: WPT \(PASS\|FAIL\|TIMEOUT\|NOTRUN\|PRECONDITION_FAILED\) ' "$L")
	d=$(grep -c '^console: WPT DONE' "$L")
	[ "$st" -ge 128 ] && [ "$st" -ne 143 ] && echo "CRASH $rel (exit $st)"
	printf '%4d / %-4d %s%s\n' "$p" "$t" "$rel" "$([ "$d" = 0 ] && echo '  (no completion)')"
	echo "$p $t" >> "$OUT/wpt-logs/.sum"
done
kill $SRV 2>/dev/null
awk '{ p += $1; t += $2 } END { if (t) printf "TOTAL: %d / %d subtests passed (%.1f%%)\n", p, t, 100 * p / t }' "$OUT/wpt-logs/.sum"
rm -f "$OUT/wpt-logs/.sum"
