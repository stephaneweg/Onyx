#!/bin/sh
# tools/tests/netsurf/xmltest.sh -- XML documents on the PC bench (docs/06 section 43), the pages of
# pages/xml/ served by xmlsrv.py with the Content-Type of their extension (.xhtml
# application/xhtml+xml, .xml application/xml, .txml text/xml, .svg image/svg+xml, .xsl text/xsl):
#  - frameload.html: each frame's load event comes, whatever its resource (an SVG in an <object>
#    and an <iframe>, an unknown type, a 404, a malformed XML, XHTML served as text/xml -- its
#    script run); a frame on a type Jet does not show has an empty document (about:blank);
#  - xmlframes.html: XML documents in frames through their contentDocument -- the tree view of an
#    XML file with no style (and of an +xml type), the error box with its line and column, XML
#    with CSS (its own sheet only; elements of no HTML class), an SVG document (getSVGDocument,
#    SVGRectElement, getNumberOfChars), an XSLT's result, XHTML, an <object>'s SVG document;
#  - jsxml.html: DOMParser / XMLSerializer, createDocument, document.evaluate (XPath 1.0),
#    XSLTProcessor (XSLT 1.0: sort, keys, number, format-number, parameters, call-template...);
#  - the pages themselves, shown at the top (their colours in the screenshot): page.xhtml (CSS by
#    <link> and <style>, its script, inline SVG, &nbsp; from the XHTML DTD), bad.xhtml (the error
#    box, the script after the error not run), data.xml (the tree), styled.xml (XML + CSS), pic.svg
#    (an SVG document and its script), catalog.xml (<?xml-stylesheet type="text/xsl"?>: html
#    output), list.xml (an XHTML result: output method xml, its script run).
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
checks() {	# checks <log> <name> <min>: the page's "XMLT ok" / "XMLT FAIL" lines, its "<name> done"
	grep -a "^console: XMLT FAIL\|^JS " "$1" | sed 's/^console: /  /'
	n_ok=$(grep -a -c "^console: XMLT ok " "$1")
	if grep -a -q "^console: XMLT FAIL\|^JS " "$1" || ! grep -a -q "^console: XMLT $2 done fails=0" "$1" ||
	   [ "$n_ok" -lt "$3" ]; then
		echo "  FAIL  $2 ($n_ok checks passed: $1)"; fail=1
	else
		echo "  ok    $2: $n_ok checks"
	fi
}
colours() {	# colours <png> <label> <#rrggbb>... : each colour drawn (10 pixels or more, within 24)
	png=$1; label=$2; shift 2
	python3 - "$png" "$label" "$@" <<'EOF' || fail=1
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
seen = im.getcolors(1 << 24)
bad = []
for h in sys.argv[3:]:
    rgb = tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))
    n = sum(k for k, c in seen if max(abs(c[0] - rgb[0]), abs(c[1] - rgb[1]), abs(c[2] - rgb[2])) <= 24)
    if n < 10:
        bad.append(h)
print("  %s  %s: %s" % ("ok  " if not bad else "FAIL", sys.argv[2],
      "its colours drawn" if not bad else "not drawn: " + " ".join(bad)))
sys.exit(1 if bad else 0)
EOF
}

echo "frameload.html (the frames' load events, an empty document for a type not shown)"
L=$X/frameload.log
run frameload.html "$L"
expect "$L" "load object acid-svg.svg"
expect "$L" "load iframe blob.unk doc=html"
expect "$L" "load iframe nothere.html doc=html"
expect "$L" "load iframe empty.xml doc=html"
expect "$L" "load iframe xhtml1.txml doc=html"
expect "$L" "load iframe acid-svg.svg doc=svg"
expect "$L" "notify xhtml.1"
expect "$L" "loads done"
refuse "$L" "reason=UnacceptableType"

echo "xmlframes.html (XML documents in frames: tree view, errors, XML + CSS, SVG, XSLT, XHTML)"
L=$X/xmlframes.log
run xmlframes.html "$L" 120
checks "$L" xmlframes 23

echo "jsxml.html (DOMParser, XMLSerializer, createDocument, document.evaluate, XSLTProcessor)"
L=$X/jsxml.log
run jsxml.html "$L" 40
checks "$L" jsxml 39

echo "the documents at the top"
L=$X/page-xhtml.log
run page.xhtml "$L" 40
for s in "xhtml script ran p1" "xhtml contentType application/xhtml+xml" \
	 "xhtml tagName p html http://www.w3.org/1999/xhtml" "xhtml entity true true" \
	 "xhtml createElement http://www.w3.org/1999/xhtml div" "xhtml box 104" \
	 "xhtml color rgb(0, 0, 255)" "xhtml write throws InvalidStateError"; do
	expect "$L" "$s"
done
colours "$L.png" "page.xhtml" "#aa0000" "#0000ff" "#00aa00" "#ff0000"
L=$X/bad-xhtml.log
run bad.xhtml "$L" 30
refuse "$L" "bad script after error ran"
colours "$L.png" "bad.xhtml (the error box)" "#ffdddd"
L=$X/data-xml.log
run data.xml "$L" 30
colours "$L.png" "data.xml (the tree view)" "#881280" "#994500" "#1a1aa6" "#236e25"
L=$X/styled-xml.log
run styled.xml "$L" 30
colours "$L.png" "styled.xml (XML + CSS)" "#008000" "#ddeeff" "#cc0000"
L=$X/pic-svg.log
run pic.svg "$L" 30
expect "$L" "svg script ran 180 svg image/svg+xml"
colours "$L.png" "pic.svg" "#3366cc" "#ffcc00"
L=$X/catalog-xml.log
run catalog.xml "$L" 40
expect "$L" "xslt page script 5 text/html"
colours "$L.png" "catalog.xml (XSLT, html output)" "#9acd32" "#c00000"
L=$X/list-xml.log
run list.xml "$L" 40
expect "$L" "xslt xml result 3 a. Apple"

[ $fail = 0 ] && echo "xmltest: all passed" || echo "xmltest: FAILED"
exit $fail
