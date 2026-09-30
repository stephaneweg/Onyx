#!/bin/sh
# tools/tests/netsurf/intl/compare.sh -- cases.js in Node (full ICU: Chrome's data) and in qjsintl
# (NetSurf's Intl); prints the cases that differ and the count. OUT as build.sh.
cd "$(dirname "$0")"
OUT=${OUT:-/tmp/nsbench}
Q=$(OUT=$OUT sh ./build.sh) || exit 1
TZ=UTC node cases.js > "$OUT/intl/node.txt"
TZ=UTC "$Q" cases.js > "$OUT/intl/qjs.txt"
diff "$OUT/intl/node.txt" "$OUT/intl/qjs.txt" | grep '^[<>]'
n=$(wc -l < "$OUT/intl/node.txt")
d=$(diff "$OUT/intl/node.txt" "$OUT/intl/qjs.txt" | grep -c '^>')
echo "$((n - d)) / $n cases as Node"
