#!/bin/sh
# tools/tests/netsurf/nettest.sh -- the scripts' real-time APIs over real sockets on the PC: a local
# server (wssrv.py, Python's standard library: WebSocket with permessage-deflate, an event stream,
# a chunked body) serves the test pages, NetSurf loads them (SIM_REALNET=1) and their console is
# checked -- WebSocket (pages/net-ws.html: text, binary, Blob, a 100 KB compressed message,
# fragments and a ping, the closing handshakes, a protocol error, a refused connection, the
# handshake's cookie), EventSource and the streamed fetch / XHR (pages/net-stream.html), and a
# page left with its sockets, streams and workers open (pages/net-teardown.html), CORS against a
# second server on the next port (pages/net-cors.html), preconnect (pages/net-preconnect.html). Builds as
# jstest.sh (OUT, default /tmp/nsbench). Exit status 0: every check passed.
#
#   sh tools/tests/netsurf/nettest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8124}
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
python3 $T/wssrv.py $T/pages $PORT > "$OUT/wssrv.log" 2>&1 &
SRV=$!
python3 $T/wssrv.py $T/pages $((PORT + 1)) > "$OUT/wssrv2.log" 2>&1 &	# (another origin: CORS)
SRV2=$!
trap 'kill $SRV $SRV2 2>/dev/null' EXIT
sleep 1
fail=0
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
run() {	# run <page> <waits> <log> [host]
	SIM_REALNET=1 SIM_SCREEN=900x900 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
	SIM_ARGS="http://${4:-127.0.0.1}:$PORT/$1" SIM="$(waits "$2")exit" \
		timeout 300 "$OUT/build/netsurf" >"$3" 2>&1
	[ $? = 0 ] || { echo "  FAIL  $1: NetSurf ended badly (see $3)"; fail=1; }
}
expect() {	# expect <log> <text>
	if grep -a -q -F -- "console: $2" "$1"; then printf "  ok    %s\n" "$2"; else printf "  FAIL  %s\n" "$2"; fail=1; fi
}
refuse() {	# refuse <log> <text>
	if grep -a -q -F -- "console: $2" "$1"; then echo "  FAIL  (not expected) $2"; fail=1; else echo "  ok    no \"$2\""; fi
}
server2() {	# server2 <text>: the second server (another origin) saw it
	if grep -a -q -F -- "$1" "$OUT/wssrv2.log"; then echo "  ok    server: $1"; else echo "  FAIL  server: $1"; fail=1; fi
}
server() {	# server <text>: the server saw it
	if grep -a -q -F -- "$1" "$OUT/wssrv.log"; then echo "  ok    server: $1"; else echo "  FAIL  server: $1"; fail=1; fi
}

echo "net-ws.html (WebSocket)"
L=$OUT/net-ws.log
run net-ws.html 1500 "$L"
for s in "ws types function 1 function" "ws bad scheme SyntaxError" "ws fragment SyntaxError" \
	 "ws dup protocol SyntaxError" "ws state 0" "ws send early InvalidStateError" \
	 "ws open 1 protocol=superchat ext=permessage-deflate" "ws text héllo €" \
	 "ws binary true 1,2,3,250" "ws binary true 98,108,111,98,98,121" "ws text long 100000" \
	 "ws text again" "ws close 1000 done true 3" "ws cookie wscookie=yes" "frag text fragmentéd" \
	 "frag blob true 200000" "frag close 4001 bye true" "bad close true 1006 false" \
	 "refused error" "refused close 1006" "early state 2" "early close 1006 false" \
	 "server close 4002 as asked true"; do
	expect "$L" "$s"
done
refuse "$L" "bad message (not expected)"
server "WS got text 100000"
server "WS close 1000 done"
server "WS bad close 8 1002"
server "cookie=wscookie=yes origin=http://127.0.0.1:$PORT"

echo "net-stream.html (EventSource, the streamed fetch and XHR)"
L=$OUT/net-stream.log
run net-stream.html 2500 "$L"
for s in "sse state 0 2" "sse open 1 1" 'sse message "first" id=' "sse tick 1 id=7" \
	 'sse message "line one\nline two" id=7' 'sse message "third" id=8' "sse error 0" \
	 "sse open 2 1" "sse resumed after 8" "sse done bye" "sse closed 2" "sse404 error 2" \
	 "fetch head 200 text/plain early=true" "fetch stream parts>1=true bytes=6000" \
	 "fetch text 6000 part0;" "fetch clone 6000 6000" "fetch stream body 200 from a stream" \
	 "fetch abort AbortError" "xhr headers 200 text/plain" "xhr states 1,2,3,4" \
	 "xhr progress>1=true partial=true length=6000" "stream done"; do
	expect "$L" "$s"
done
refuse "$L" "sse message \"no end\""
server "last-event-id=8"

echo "net-teardown.html (a page left with its sockets, streams and workers open)"
L=$OUT/net-teardown.log
run net-teardown.html 1500 "$L"
expect "$L" "teardown leaving true"
expect "$L" "teardown second page"

echo "net-cors.html (cross-origin fetch and XHR: CORS)"
L=$OUT/net-cors.log
run net-cors.html 800 "$L"
for s in "allowed 200 cors cors GET cookie=None secret=null type=text/plain" "exposed secret=hidden" \
	 "refused threw TypeError" "star 200" "star cred threw TypeError" "cred set 200" \
	 "cred send cors GET cookie=corscookie=yes" "cred omit cors GET cookie=None" \
	 "cred no acac threw TypeError" "preflight 200 cors PUT cookie=None" \
	 "preflight refused threw TypeError" "simple post cors POST cookie=None" \
	 "mode same-origin threw TypeError" 'mode no-cors opaque 0 ""' "same 200 basic secret=hidden" \
	 "xhr allowed load 200 cors GET cookie=None secret=null" "xhr refused error 0" \
	 "xhr cred load 200 cors GET cookie=corscookie=yes secret=null" "cors done"; do
	expect "$L" "$s"
done
if grep -a -q "console: .* NOT$" "$L"; then echo "  FAIL  a refused request allowed: $(grep -a 'console: .* NOT$' "$L" | tr '\n' ' ')"; fail=1; else echo "  ok    no refused request allowed"; fi
server2 "CORS OPTIONS /cors?acao=origin&methods=PUT&headers=X-Custom&t=10 cookie=None origin=http://127.0.0.1:$PORT acrm=PUT acrh=x-custom"
server2 "CORS OPTIONS /cors?acao=origin&t=11"
if grep -a -q "CORS PUT /cors?acao=origin&t=11\|CORS POST /cors?acao=origin&t=12 .*acrm=[^N]" "$OUT/wssrv.log"; then
	echo "  FAIL  server: a refused preflight's request sent, or a simple one preflighted"; fail=1
else
	echo "  ok    server: no request after a refused preflight"
fi

echo "net-preconnect.html (<link rel=preconnect>, <link rel=dns-prefetch>)"
L=$OUT/net-preconnect.log
NS_PERF=1 run net-preconnect.html 300 "$L"
expect "$L" "preconnect fetch 200"
n=$(grep -a -c "ONYX-PERF net:conn 127.0.0.1:$((PORT + 1)) " "$L")
if grep -a -q "ONYX-PERF net:preconnect 127.0.0.1:$((PORT + 1)) http/1.1" "$L" && [ "$n" = 1 ] &&
   grep -a -q "ONYX-PERF net:preconnect localhost:$PORT resolved" "$L"; then
	echo "  ok    preconnected (the fetch on that connection), the name resolved"
else
	echo "  FAIL  preconnect: $n connections, $(grep -a 'net:preconnect' "$L" | tr '\n' ' ')"; fail=1
fi

[ $fail = 0 ] && echo "all passed" || exit 1
