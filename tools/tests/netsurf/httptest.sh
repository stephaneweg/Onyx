#!/bin/sh
# tools/tests/netsurf/httptest.sh -- the Onyx fetcher (user/netsurf/onyx_fetch.c) over real HTTP
# on the PC: a local HTTP/1.1 server (httpsrv.py: keep-alive, chunked, gzip / br / zstd, a
# redirect, cookies) serves the copy of kotonviolins.com (getsites.sh), NetSurf loads it through
# /redir, then again (F5). Checks: the connections kept (fewer connections than requests), the
# redirect's cookie and the page's sent back, the Referer, the codings, the page drawn as the
# file:// copy is, and launched again, the disc cache (docs/06 section 22: the images fresh, no
# request; the style sheet revalidated, a 304) -- in RAM: (docs/06 section 33: the kernel's RAM
# volume, here the folder SIM_RAM, the same for the launches of one "boot"; an object stored the
# first time it is seen), nothing written to the card, all of it gone after a restart (a new
# SIM_RAM); and with Choices' cache_on_card:1, on the card (an object stored the second launch
# it is seen, docs/06 section 32: read from the card at the third). Then HTTP/2 (docs/06 section 22; needs Python's h2: pip install h2): an HTTPS
# server with ALPN h2 (h2srv.py, a certificate made here for 127.0.0.1 and added to the bench's
# trusted roots) serves the same copy -- one connection, the requests multiplexed, drawn the
# same, with the bench's OpenSSL and with the Pi's mbedTLS code (NS_MBEDTLS=1); the scripts'
# requests over it (pages/net-h2.html: a POST, a body past the flow-control windows, a streamed
# response, 24 requests at once, the cookies, XHR progress); and a server that chooses
# http/1.1 by ALPN (the fallback).
#
#   sh tools/tests/netsurf/getsites.sh $HOME/nssites          (once)
#   SITES=$HOME/nssites sh tools/tests/netsurf/httptest.sh
set -e
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
SITES=${SITES:-/tmp/nssites}
PORT=${PORT:-8123}
H2PORT=${H2PORT:-8443}
H1TLSPORT=${H1TLSPORT:-8444}
[ -f "$SITES/kotonviolins.com/index.html" ] || { echo "no $SITES: run getsites.sh"; exit 1; }
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
python3 $T/httpsrv.py "$SITES" $PORT > "$OUT/httpsrv.log" 2>&1 &
SRV=$!
SRV2=
SRV3=
trap 'kill $SRV $SRV2 $SRV3 2>/dev/null' EXIT
sleep 1
W=$(i=0; while [ $i -lt 150 ]; do printf 'wait;'; i=$((i + 1)); done)
# (the disc cache and the rest of the app's files: a folder of this test's, emptied first)
export SIM_WRITES="$OUT/httptest-writes"
rm -rf "$SIM_WRITES"
Q="quit;$W"	# (the app ends as when its window is closed: the disc cache written)
# (RAM:, the kernel's RAM volume: this folder for the launches below -- one boot)
export SIM_RAM="$OUT/httptest-ram"
rm -rf "$SIM_RAM" "$OUT/httptest-ram2"
CARD="$SIM_WRITES$(realpath "$OUT")/build/data"	# (the card's copy of the app's data folder)
run() {
	SIM_REALNET=1 SIM_SCREEN=1024x768 SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$1" SIM="$2" \
		timeout 300 "$OUT/build/netsurf" >"$OUT/httptest-$3.log" 2>&1
}
run "http://127.0.0.1:$PORT/redir" "${W}dump $OUT/http1.elsm;key 276;${W}dump $OUT/http2.elsm;${Q}exit" http
n1=$(wc -l < "$OUT/httpsrv.log")
# launched again: the page from the disc cache (the images fresh, the style sheet revalidated)
run "http://127.0.0.1:$PORT/kotonviolins.com/index.html" "${W}dump $OUT/http3.elsm;${Q}exit" http3
run "file://$(realpath "$SITES")/kotonviolins.com/index.html" "${W}dump $OUT/file.elsm;exit" file
fail=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; fail=1; fi; }
# drawn <dumps...>: each as the file:// copy (below the toolbar: its address differs)
drawn() {
	python3 - $OUT "$@" <<'PY'
import struct, sys
def px(f):
    d = open(f, 'rb').read(); w, h = struct.unpack('<2i', d[4:12])
    return w, h, d[20:20 + w * h * 4]
w, h, a = px(sys.argv[1] + '/file.elsm')
for f in sys.argv[2:]:
    w2, h2, b = px(sys.argv[1] + '/' + f)
    top = 70 * w * 4
    if (w, h) != (w2, h2) or a[top:] != b[top:]:
        sys.exit(1)
PY
}
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
check "drawn as the file:// copy (gzip, chunked)" "drawn http1.elsm http2.elsm"
tail -n +$((n1 + 1)) "$L" > "$OUT/httpsrv-3.log"
check "launched again: the style sheet revalidated (a 304)" "grep -q '^NOTMOD .*style.css' $OUT/httpsrv-3.log"
check "launched again: the images from the card (no request)" "! grep -q '^REQ .*\.png' $OUT/httpsrv-3.log"
check "launched again: drawn the same" "drawn http3.elsm"
check "the disc cache in RAM: (RAM:/jet/cache), the code cache too, nothing on the card" \
	"[ -f $SIM_RAM/jet/cache/index ] && [ -d $SIM_RAM/jet/jscache ] && [ ! -e $CARD/cache ] && [ ! -e $CARD/jscache ]"
# a restart: RAM: empty again (a new SIM_RAM) -- the images asked for again
n3=$(wc -l < "$L")
export SIM_RAM="$OUT/httptest-ram2"
run "http://127.0.0.1:$PORT/kotonviolins.com/index.html" "${W}dump $OUT/http4.elsm;${Q}exit" http4
export SIM_RAM="$OUT/httptest-ram"
tail -n +$((n3 + 1)) "$L" > "$OUT/httpsrv-4.log"
check "after a restart (RAM: empty): the images asked for again" "grep -q '^REQ .*\.png' $OUT/httpsrv-4.log"
# Choices' cache_on_card:1 -- the card: seen at the 1st launch, stored at the 2nd, read at the 3rd
echo "cache_on_card:1" >> "$OUT/build/res/Choices"	# (make rewrites it)
rm -rf "$SIM_RAM"
for k in 5 6 7; do
	n0=$(wc -l < "$L")
	run "http://127.0.0.1:$PORT/kotonviolins.com/index.html" "${W}dump $OUT/http$k.elsm;${Q}exit" http$k
	tail -n +$((n0 + 1)) "$L" > "$OUT/httpsrv-$k.log"
done
sed -i '/^cache_on_card:/d' "$OUT/build/res/Choices"
check "cache_on_card:1: the disc cache and the code cache on the card, nothing in RAM:" \
	"[ -f $CARD/cache/index ] && [ -d $CARD/jscache ] && [ ! -e $SIM_RAM/jet ]"
check "cache_on_card:1: the 2nd launch still asks for the images (stored then)" "grep -q '^REQ .*\.png' $OUT/httpsrv-6.log"
check "cache_on_card:1: the 3rd launch: the images from the card (no request)" "! grep -q '^REQ .*images/koton' $OUT/httpsrv-7.log"
check "cache_on_card:1: the 1.9 MB image not kept (the card's 512 KB cap; RAM:'s is 2 MB)" "grep -q '^REQ .*wood-contrast-real\.png' $OUT/httpsrv-7.log"
check "cache_on_card:1: the 3rd launch: the style sheet revalidated (a 304)" "grep -q '^NOTMOD .*style.css' $OUT/httpsrv-7.log"
check "cache_on_card:1: drawn the same" "drawn http7.elsm"

# ---- HTTP/2 ----------------------------------------------------------------------------------
if python3 -c 'import h2' 2>/dev/null; then
	mkdir -p "$OUT/h2"
	openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 30 \
		-subj /CN=localhost -addext subjectAltName=DNS:localhost,IP:127.0.0.1 \
		-keyout "$OUT/h2/key.pem" -out "$OUT/h2/cert.pem" >/dev/null 2>&1
	cat "$OUT/build/res/ca-bundle" "$OUT/h2/cert.pem" > "$OUT/h2/ca.pem"
	echo "ca_bundle:$OUT/h2/ca.pem" >> "$OUT/build/res/Choices"	# (make rewrites it)
	H2SRV_PAGES=$T/pages python3 $T/h2srv.py "$SITES" $H2PORT "$OUT/h2/cert.pem" \
		"$OUT/h2/key.pem" > "$OUT/h2srv.log" 2>&1 &
	SRV2=$!
	python3 $T/h2srv.py "$SITES" $H1TLSPORT "$OUT/h2/cert.pem" "$OUT/h2/key.pem" h1 \
		> "$OUT/h1tlssrv.log" 2>&1 &
	SRV3=$!
	sleep 1
	echo "HTTP/2 (h2srv.py)"
	run "https://127.0.0.1:$H2PORT/redir" "${W}dump $OUT/h2a.elsm;key 276;${W}dump $OUT/h2b.elsm;exit" h2
	L="$OUT/h2srv.log"
	conns=$(grep -c '^CONN .* h2' "$L" || true); reqs=$(grep -c '^REQ' "$L" || true)
	echo "  (OpenSSL) $reqs requests over $conns connections"
	check "one HTTP/2 connection for the two loads" "[ $conns = 1 ] && [ $reqs -ge 10 ]"
	check "the redirect's cookie and the page's sent back" \
		"grep -q 'REQ .*/kotonviolins.com/index.html cookie=redir=1 ' $L && grep -q 'cookie=redir=1; sid=abc123' $L"
	check "the Referer sent" "grep -q 'referer=https://127.0.0.1:$H2PORT/kotonviolins.com/index.html' $L"
	check "drawn as the file:// copy (br, zstd, gzip over HTTP/2)" "drawn h2a.elsm h2b.elsm"
	NS_MBEDTLS=1 run "https://127.0.0.1:$H2PORT/redir" "${W}dump $OUT/h2c.elsm;exit" h2mb
	check "the same with the Pi's mbedTLS code (ALPN h2)" \
		"[ \$(grep -c '^CONN .* h2' $L) = 2 ] && drawn h2c.elsm"
	# the scripts' requests over HTTP/2
	SIM_REALNET=1 SIM_SCREEN=900x900 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
		SIM_ARGS="https://127.0.0.1:$H2PORT/pages/net-h2.html" SIM="${W}${W}exit" \
		timeout 300 "$OUT/build/netsurf" >"$OUT/httptest-h2js.log" 2>&1 || true
	for e in "h2 echo 200 POST hello h2" "h2 big 3000000 true" "h2 stream head 200 early=true" \
		"h2 stream parts>2=true first<600=true 5 lines" "h2 parallel 24" "h2 cookie true" \
		"h2 xhr 200 progress>1=true 4" "h2 done"; do
		check "$e" "grep -a -q -F -- 'console: $e' $OUT/httptest-h2js.log"
	done
	check "the page's requests on one connection" "[ \$(grep -c '^CONN .* h2' $L) = 3 ]"
	# a server that chooses http/1.1 by ALPN
	run "https://127.0.0.1:$H1TLSPORT/kotonviolins.com/index.html" "${W}dump $OUT/h1tls.elsm;exit" h1tls
	check "ALPN http/1.1: the fallback (drawn the same)" \
		"grep -q '^REQ h1 .*style.css' $OUT/h1tlssrv.log && drawn h1tls.elsm"
else
	echo "  skip  HTTP/2 (pip install h2)"
fi
[ $fail = 0 ] && echo "all passed" || exit 1
