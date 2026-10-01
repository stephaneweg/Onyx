#!/bin/sh
# tools/tests/netsurf/quadtest.sh -- the quadratic audit's micro-benchmarks (docs/06-JET-BROWSER.md
# §36) on the PC bench: pages/perf-quadratic.html runs each case at n = 1000, 2000, 4000, 8000 and
# logs its time; printed here as a table with the growth from 1000 to 8000 (linear: ~8x,
# quadratic: ~64x). The cases fixed by the audit must stay under 20x (exit status 1 otherwise; the
# 1000 case's time taken as 5 ms at least, nothing under 50 ms at 8000 fails: the noise).
# Builds as shot.sh (OUT, default /tmp/nsbench).
#
#   sh tools/tests/netsurf/quadtest.sh [case,case,...]
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
L=$OUT/perf-quadratic.log
# (the cases asked: the page copied beside the bench with QUAD_ONLY set -- a file: URL keeps no
# query or fragment)
P=$OUT/perf-quadratic.html
{ printf '<script>var QUAD_ONLY = "%s";</script>\n' "$1"; cat "$T/pages/perf-quadratic.html"; } >"$P"
waits=$(awk 'BEGIN { for (i = 0; i < 24000; i++) printf "wait;" }')
# (the page logs "quad done" at its end; the run is cut there)
( SIM_SCREEN=900x900 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
	SIM_ARGS="file://$P" SIM="${waits}exit" \
	timeout 900 "$OUT/build/netsurf" >"$L" 2>&1 ) &
pid=$!
while kill -0 "$pid" 2>/dev/null && ! grep -q -a "^console: quad done" "$L"; do sleep 1; done
pkill -P "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
# the cases the audit fixed: they must stay (about) linear
FIXED=" appendManyObservers formElements styleElements bigSheet queryNthChild setTimeouts childNodes children childElementCount byTagNameLoop selectOptions selectLength optionIndex getById querySelectorId compareSort addListener removeListener "
awk -v fixed="$FIXED" '
/^console: quad [A-Za-z]+ [0-9]+ / {
	k = $3; n = $4; t[k, n] = $5
	if (!(k in seen)) { seen[k] = 1; order[++m] = k }
}
END {
	printf "  %-20s %9s %9s %9s %9s %7s\n", "case (ms)", "n=1000", "2000", "4000", "8000", "x8000"
	bad = 0
	for (i = 1; i <= m; i++) {
		k = order[i]
		a = t[k, 1000]; d = t[k, 8000]
		g = (a + 0 > 0.5 && d != "" && d != "skipped") ? sprintf("%.0f", d / a) : "-"
		flag = ""
		# (a fixed case fails past 20x its time at 1000 -- taken as 5 ms at least: the
		# small times are noise -- and past 50 ms at 8000)
		if (index(fixed, " " k " ") && d != "" && d != "skipped" && d + 0 > 50 &&
		    d / (a + 0 > 5 ? a : 5) > 20) { flag = "  FAIL (quadratic)"; bad = 1 }
		printf "  %-20s %9s %9s %9s %9s %7s%s\n", k, a, t[k, 2000], t[k, 4000], d, g, flag
	}
	exit bad
}' "$L"
st=$?
grep -q -a "^console: quad done" "$L" || { echo "  FAIL  the page did not finish ($L)"; st=1; }
grep -a "^JS " "$L" | head -5 | sed 's/^/  /'
[ "$st" = 0 ] && echo "all passed" || echo "FAILED ($L)"
exit "$st"
