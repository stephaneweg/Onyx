#!/bin/sh
# tools/tests/netsurf/html5lib.sh -- NetSurf's HTML parser (libhubbub + libdom's binding)
# against the html5lib-tests: the tokenizer tests (tokenizer/*.test) and the tree-construction
# tests (tree-construction/*.dat: documents, fragments with their context element, scripting
# on / off). The tests are fetched once into $OUT/html5lib-tests (git), at the last commit that
# still had the tree-construction tests in that repository (they moved to WPT since).
#
#   sh tools/tests/netsurf/html5lib.sh [tree|tok|time <file.html>] [-v] [files...]
#
# OUT (default /tmp/nshtml5) holds the tests, the objects and the drivers. -v prints every
# failure. `time page.html` times the parse of a page (32 KB chunks, as the fetcher gives it).
set -e
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nshtml5}
REV=9329e64694e7835d0dcff9811e22856ef6ad16f9		# html5lib-tests: the last commit with tree-construction/
mkdir -p "$OUT"
if [ ! -d "$OUT/html5lib-tests/tree-construction" ]; then
	rm -rf "$OUT/html5lib-tests"
	git clone -q --filter=blob:none https://github.com/html5lib/html5lib-tests "$OUT/html5lib-tests"
	git -C "$OUT/html5lib-tests" checkout -q "$REV"
fi
make -f $T/html5lib.mk OUT="$OUT" -j4 >"$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; echo "build failed: $OUT/build.log"; exit 1; }
what=${1:-all}
[ $# -gt 0 ] && shift
H=$OUT/html5lib-tests
case "$what" in
tree)	if [ $# -gt 0 ] && [ "${1#-}" = "$1" ] || [ $# -gt 1 ]; then "$OUT/html5lib_tree" "$@"
	else "$OUT/html5lib_tree" "$@" "$H"/tree-construction/*.dat; fi ;;
tok)	if [ $# -gt 0 ] && [ "${1#-}" = "$1" ] || [ $# -gt 1 ]; then python3 $T/html5lib_tok.py "$OUT/html5lib_tok" "$@"
	else python3 $T/html5lib_tok.py "$OUT/html5lib_tok" "$@" "$H"/tokenizer/*.test; fi ;;
time)	"$OUT/html5lib_time" "$@" ;;
all)	python3 $T/html5lib_tok.py "$OUT/html5lib_tok" "$H"/tokenizer/*.test
	"$OUT/html5lib_tree" "$H"/tree-construction/*.dat ;;
*)	echo "usage: html5lib.sh [tree|tok|time <file>] [-v] [files...]"; exit 1 ;;
esac
