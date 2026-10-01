#!/bin/sh
# tools/tests/netsurf/layouttest.sh -- the layout regression test: layoutdiff.sh over every
# reduced page of pages/layout/ (one feature each: flex, grid, heights, abs-pos, inline, forms,
# tables / floats, misc), a summary line per page (Chromium's element count, NetSurf's, the
# differing boxes, the missing ones; "local": the boxes whose size or position relative to their
# parent element differ -- one misplaced box does not count all that follow it) and the total.
# The details of a page (its differing elements, NetSurf's box then Chromium's) are in
# $OUT/layouttest/<page>.txt (the page's coordinates) and <page>.rel.txt (relative ones).
#
#   sh tools/tests/netsurf/layouttest.sh [page ...]      (names without .html; default: all)
#
# OUT as the others (the build is layoutdiff.sh's); WAITS (default 60) the loop turns before
# the measure; TOL (default 2) the tolerance in px.
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
export OUT
mkdir -p "$OUT/layouttest"
pages=$*
[ -n "$pages" ] || pages=$(ls $T/pages/layout/*.html | sed 's#.*/##; s#\.html$##')
tot_d=0; tot_m=0; tot_c=0; tot_r=0
printf '%-22s %8s %8s %9s %8s %6s\n' page chromium netsurf differing missing local
for p in $pages; do
	sh $T/layoutdiff.sh "$T/pages/layout/$p.html" 720 "${WAITS:-60}" "${TOL:-2}" > "$OUT/layouttest/$p.txt" 2>&1
	l=$(grep '^elements:' "$OUT/layouttest/$p.txt")
	if [ -z "$l" ]; then printf '%-22s failed (see %s)\n' "$p" "$OUT/layouttest/$p.txt"; continue; fi
	LD_COMPARE=1 REL=1 sh $T/layoutdiff.sh "$T/pages/layout/$p.html" 720 0 "${TOL:-2}" > "$OUT/layouttest/$p.rel.txt" 2>&1
	r=$(grep '^elements:' "$OUT/layouttest/$p.rel.txt" | tr -d ',' | awk '{print $7}')
	set -- $(echo "$l" | tr -d ',' | awk '{print $3, $5, $7, $9}')
	printf '%-22s %8s %8s %9s %8s %6s\n' "$p" "$1" "$2" "$3" "$4" "$r"
	tot_c=$((tot_c + $1)); tot_d=$((tot_d + $3)); tot_m=$((tot_m + $4)); tot_r=$((tot_r + r))
done
printf '%-22s %8s %8s %9s %8s %6s\n' TOTAL "$tot_c" "" "$tot_d" "$tot_m" "$tot_r"
