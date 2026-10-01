#!/bin/sh
# tools/tests/netsurf/xmltest.sh -- XML documents on the PC bench (docs/06 section 43), the pages of
# pages/xml/ served by xmlsrv.py with the Content-Type of their extension:
#  - frameload.html: each frame's load event comes, whatever its resource (an SVG, an unknown type,
#    a 404, a malformed XML, XHTML served as text/xml); a frame on a type Jet does not show has an
#    empty document (about:blank), not an error page.
# Builds as jstest.sh (OUT, default /tmp/nsbench). Exit status 0: every check passed.
#
#   OUT=/tmp/nsbench PORT=8170 sh tools/tests/netsurf/xmltest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8170}
X=$OUT/xml
mkdir -p "$X"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$X/build.log" 2>&1 ||
	{ echo "build failed: $X/build.log"; exit 1; }
fail=0
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
python3 $T/xmlsrv.py $T/pages/xml $PORT >"$X/srv.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 1
U=http://127.0.0.1:$PORT
run() {	# run <page> <log> [waits] [screen]
	SIM_REALNET=1 SIM_SCREEN=${4:-900x700} SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
	SIM_ARGS="$U/$1" SIM="$(waits ${3:-80})dump $2.elsm;exit" timeout 300 "$OUT/build/netsurf" >"$2" 2>&1
	[ $? = 0 ] || { echo "  FAIL  NetSurf ended badly (see $2)"; fail=1; }
	python3 tools/tests/desktop_sim/shot.py "$2.elsm" "$2.png" >/dev/null 2>&1
	rm -f "$2.elsm"
}
expect() {	# expect <log> <text>
	if grep -a -q -F -- "console: XMLT $2" "$1"; then echo "  ok    $2"; else echo "  FAIL  $2"; fail=1; fi
}
refuse() {	# refuse <log> <text>
	if grep -a -q -F -- "$2" "$1"; then echo "  FAIL  (not expected) $2"; fail=1; else echo "  ok    no \"$2\""; fi
}

echo "frameload.html (the frames' load events, an empty document for a type not shown)"
L=$X/frameload.log
run frameload.html "$L"
expect "$L" "load object acid-svg.svg"
expect "$L" "load iframe blob.unk doc=html"
expect "$L" "load iframe nothere.html doc=html"
expect "$L" "load iframe empty.xml"
expect "$L" "load iframe xhtml1.txml"
expect "$L" "load iframe acid-svg.svg"
expect "$L" "loads done"
refuse "$L" "reason=UnacceptableType"

[ $fail = 0 ] && echo "xmltest: all passed" || echo "xmltest: FAILED"
exit $fail
