#!/bin/sh
# tools/tests/netsurf/urltest.sh -- dom.js's URL / URLSearchParams against the web-platform-tests'
# URL data (urltestdata.json, setters_tests.json, toascii.json, fetched into $OUT/url), run in
# Node on the code taken out of dom.js (between "Onyx: URL and URLSearchParams" and URL's end).
#   sh tools/tests/netsurf/urltest.sh [-v]
cd "$(dirname "$0")/../../.."
OUT=${OUT:-/tmp/nsbench}
D=$OUT/url
mkdir -p "$D"
W=https://raw.githubusercontent.com/web-platform-tests/wpt/master/url/resources
for f in urltestdata.json setters_tests.json toascii.json; do
	[ -s "$D/$f" ] || curl -sS -o "$D/$f" "$W/$f" || { echo "cannot fetch $f"; exit 1; }
done
DOMJS=third_party/netsurf/content/handlers/javascript/quickjs/dom.js D=$D node tools/tests/netsurf/urltest.js "$@"
