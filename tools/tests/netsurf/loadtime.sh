#!/bin/sh
# tools/tests/netsurf/loadtime.sh -- page load times on live sites in the PC NetSurf: each URL
# loaded RUNS times (a fresh process each), the first "cold" (no disk cache, no TLS session, no
# HTTP1Hosts: the files the app writes go to $OUT/load/writes, emptied for each URL), the others
# "warm" (a second launch: the disk cache, the TLS sessions kept); its "page:load" (NS_PERF: the
# throbber's start to its stop -- the page, what it fetched, its scripts) and the fetcher's
# connections ("net:conn" lines: HTTP/1.1 or HTTP/2, TLS full or resumed) and responses
# ("net:done": 304s; "net:cache": disk / memory hits) summed up. NS_H2=0 turns HTTP/2 off (the
# fetcher offers only http/1.1): the before / after of docs/06 section 22.
#
#   sh tools/tests/netsurf/loadtime.sh [url ...]
#   NS_H2=0 RUNS=3 sh tools/tests/netsurf/loadtime.sh https://www.bbc.com/
#   NS_MBEDTLS=1 ... (the Pi's TLS code: its sessions are the ones kept across launches)
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
RUNS=${RUNS:-3}
mkdir -p "$OUT/load"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
[ $# -gt 0 ] || set -- https://www.bbc.com/ https://github.com/ https://en.wikipedia.org/wiki/Raspberry_Pi
W=$(i=0; while [ $i -lt "${WAITS:-1500}" ]; do printf 'wait;'; i=$((i + 1)); done)
Q=$(i=0; while [ $i -lt 300 ]; do printf 'wait;'; i=$((i + 1)); done)	# (the quit: the cache written)
for u in "$@"; do
	n=$(echo "$u" | sed 's#https\?://##; s#[^a-zA-Z0-9]#_#g' | cut -c1-40)
	r=1
	export SIM_WRITES="$OUT/load/writes"
	rm -rf "$SIM_WRITES" "$OUT/build/data/TLSSessions" "$OUT/build/data/HTTP1Hosts"	# (fopen's: not SIM_WRITES')
	while [ $r -le "$RUNS" ]; do
		L="$OUT/load/$n-h2${NS_H2:-1}-$r.log"
		SIM_REALNET=1 NS_PERF=1 SIM_SCREEN=800x1000 SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$u" \
			SIM="${W}quit;${Q}exit" timeout 300 "$OUT/build/netsurf" >"$L" 2>&1
		load=$(grep -a "ONYX-PERF page:load" "$L" | head -1 | awk '{print int($3 / 1000)}')
		h1=$(grep -a -c "ONYX-PERF net:conn .* http/1.1 us" "$L")
		h2=$(grep -a -c "ONYX-PERF net:conn .* h2 us" "$L")
		res=$(grep -a -c "ONYX-PERF net:conn .* resumed " "$L")
		reqs=$(grep -a -c "ONYX-PERF net:done" "$L")
		nm=$(grep -a -c "ONYX-PERF net:done .*revalidated" "$L")
		hit=$(grep -a -c "ONYX-PERF net:cache .*card" "$L")
		[ $r = 1 ] && k=cold || k=warm
		echo "$u run $r ($k): load ${load:-?} ms, $reqs responses ($nm 304), $hit from the card," \
			"connections: $h1 HTTP/1.1, $h2 HTTP/2 ($res TLS resumed)"
		r=$((r + 1))
	done
done
