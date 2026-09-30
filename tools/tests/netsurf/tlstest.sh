#!/bin/sh
# tools/tests/netsurf/tlstest.sh -- the certificate check on live servers (badssl.com), with the
# bench's TLS (OpenSSL, host_stubs.c) and with the Pi's own (NS_MBEDTLS=1: onyx_nstls.cpp /
# user/tls/onyx_tls.hpp on mbedTLS, over the PC's sockets): each bad certificate refused with
# its reason (FETCH_CERT_ERR: NetSurf's "Privacy error" page), the good sites loaded; then
# "Proceed" on the error page (the page loads, the host accepted) and about:certificate (the
# chain drawn by the mbedTLS viewer). The trusted roots are the card's bundle
# (third_party/netsurf/resources/ca-bundle) plus the proxy's CA when this machine has one
# (host.mk's res rule). Needs the network (HTTPS_PROXY is followed). Builds as jstest.sh.
#
#   sh tools/tests/netsurf/tlstest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
mkdir -p "$OUT/tls"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
fail=0
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
run() {	# run <url> <log> [steps]
	SIM_REALNET=1 SIM_SCREEN=900x700 SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$1" \
	SIM="$(waits 250)${3:-}dump $2.elsm;exit" timeout 200 "$OUT/build/netsurf" >"$2.log" 2>&1
}
for tls in openssl mbedtls; do
	[ $tls = mbedtls ] && export NS_MBEDTLS=1 || unset NS_MBEDTLS
	echo "== $tls"
	for c in "expired:has expired" "wrong.host:different host" "self-signed:self signed" \
		"untrusted-root:self signed"; do
		h=${c%%:*}; why=${c#*:}
		L="$OUT/tls/$tls-$h"
		run "https://$h.badssl.com/" "$L"
		if grep -a -q "ONYX-FETCH FAIL https://$h.badssl.com/ (type 10)" "$L.log" &&
		   grep -a "ONYX-TLS refused https://$h.badssl.com/" "$L.log" | grep -q -i "$why"; then
			echo "  ok    $h.badssl.com refused ($why)"
		else
			echo "  FAIL  $h.badssl.com: $(grep -a 'ONYX-TLS\|ONYX-FETCH' "$L.log" | head -2 | tr '\n' ' ')"
			fail=1
		fi
	done
	for u in https://badssl.com/ https://en.wikipedia.org/wiki/Raspberry_Pi https://github.com/; do
		n=$(echo "$u" | sed 's#https://##; s#[^a-zA-Z0-9]#_#g' | cut -c1-30)
		L="$OUT/tls/$tls-$n"
		run "$u" "$L"
		if grep -a -q "ONYX-TLS refused\|ONYX-FETCH FAIL $u " "$L.log"; then
			echo "  FAIL  $u: $(grep -a 'ONYX-TLS\|ONYX-FETCH' "$L.log" | head -2 | tr '\n' ' ')"
			fail=1
		else
			echo "  ok    $u trusted"
		fi
	done
	# "Proceed" on the error page (its button, client coordinates of the 900x700 window)
	L="$OUT/tls/$tls-proceed"
	run "https://self-signed.badssl.com/" "$L" "down 679 347;up 679 347;$(waits 250)"
	n=$(grep -a -c "sim: tcp_connect self-signed.badssl.com" "$L.log")
	if [ "$n" -ge 2 ] && [ "$(grep -a -c 'ONYX-FETCH FAIL https://self-signed' "$L.log")" = 1 ]; then
		echo "  ok    proceed: the page fetched again, accepted"
	else
		echo "  FAIL  proceed ($n connections)"; fail=1
	fi
done
unset NS_MBEDTLS
# the certificate viewer (mbedTLS): "View certificate details" on the error page
L="$OUT/tls/viewer"
run "https://expired.badssl.com/" "$L" "down 180 306;up 180 306;$(waits 150)"
python3 tools/tests/desktop_sim/shot.py "$L.elsm" "$L.png" >/dev/null 2>&1
echo "  (the certificate viewer: $L.png)"
[ $fail = 0 ] && echo "all passed" || exit 1
