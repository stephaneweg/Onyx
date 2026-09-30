#!/bin/sh
# tools/tests/netsurf/httptest.sh -- the Onyx fetcher (user/netsurf/onyx_fetch.c) over real HTTP
# on the PC: a local HTTP/1.1 server (httpsrv.py: keep-alive, chunked, gzip, a redirect, cookies)
# serves the copy of kotonviolins.com (getsites.sh), NetSurf loads it through /redir, then again
# (F5). Checks: the connections kept (fewer connections than requests), the redirect's cookie
# and the page's sent back, the Referer, and the page drawn as the file:// copy is.
#
#   sh tools/tests/netsurf/getsites.sh $HOME/nssites          (once)
#   SITES=$HOME/nssites sh tools/tests/netsurf/httptest.sh
set -e
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
SITES=${SITES:-/tmp/nssites}
PORT=${PORT:-8123}
[ -f "$SITES/kotonviolins.com/index.html" ] || { echo "no $SITES: run getsites.sh"; exit 1; }
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
python3 $T/httpsrv.py "$SITES" $PORT > "$OUT/httpsrv.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 1
W=$(i=0; while [ $i -lt 150 ]; do printf 'wait;'; i=$((i + 1)); done)
run() {
	SIM_REALNET=1 SIM_SCREEN=1024x768 SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$1" SIM="$2" \
		timeout 300 "$OUT/build/netsurf" >"$OUT/httptest-$3.log" 2>&1
}
run "http://127.0.0.1:$PORT/redir" "${W}dump $OUT/http1.elsm;key 276;${W}dump $OUT/http2.elsm;exit" http
run "file://$(realpath "$SITES")/kotonviolins.com/index.html" "${W}dump $OUT/file.elsm;exit" file
fail=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; fail=1; fi; }
L="$OUT/httpsrv.log"
conns=$(grep -c '^CONN' "$L"); reqs=$(grep -c '^REQ' "$L")
echo "httptest: $reqs requests over $conns connections"
check "keep-alive (connections < requests / 2)" "[ $((conns * 2)) -lt $reqs ]"
check "the redirect's cookie sent back" "grep -q 'REQ .*/kotonviolins.com/index.html cookie=redir=1' $L"
check "the page's cookie sent back" "grep -q 'cookie=redir=1; sid=abc123' $L"
check "the Referer sent" "grep -q 'referer=http://127.0.0.1:$PORT/kotonviolins.com/index.html' $L"
# (the codings the server could use: Python's brotli / zstandard modules -- pip install them)
if python3 -c 'import brotli' 2>/dev/null; then
	check "a style sheet sent br (decoded: the drawing below)" "grep -q '^ENC .*\.css br' $L"
else echo "  skip  br (pip install brotli)"; fi
if python3 -c 'import zstandard' 2>/dev/null; then
	check "the images sent zstd (decoded: the drawing below)" "grep -q '^ENC .*\.png zstd' $L"
else echo "  skip  zstd (pip install zstandard)"; fi
check "drawn as the file:// copy (gzip, chunked)" "python3 - $OUT <<'PY'
import struct, sys
def px(f):
    d = open(f, 'rb').read(); w, h = struct.unpack('<2i', d[4:12])
    return w, h, d[20:20 + w * h * 4]
w, h, a = px(sys.argv[1] + '/file.elsm')
for f in ('http1.elsm', 'http2.elsm'):
    w2, h2, b = px(sys.argv[1] + '/' + f)
    top = 70 * w * 4	# (the toolbar: its address differs)
    if (w, h) != (w2, h2) or a[top:] != b[top:]:
        sys.exit(1)
PY"
[ $fail = 0 ] && echo "all passed" || exit 1
