#!/bin/sh
# tools/tests/netsurf/tlstest.sh -- the certificate check on live servers (badssl.com), with the
# bench's TLS (OpenSSL, host_stubs.c) and with the Pi's own (NS_MBEDTLS=1: onyx_nstls.cpp /
# user/tls/onyx_tls.hpp on mbedTLS, over the PC's sockets): each bad certificate refused with
# its reason (FETCH_CERT_ERR: NetSurf's "Privacy error" page), the good sites loaded; then
# "Proceed" on the error page (the page loads, the host accepted) and about:certificate (the
# chain drawn by the mbedTLS viewer); the toolbar's padlock: green on a trusted site, red past
# "Proceed", a click on it the viewer. The trusted roots are the card's bundle
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
# the toolbar's padlock (docs/06 section 35): green / red pixels among the window's first 64 rows (its
# title and the band)
band() {
	python3 - "$1" "$2" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read(); w, h = struct.unpack('<2i', d[4:12]); px = d[20:20 + w * h * 4]
n = 0
for y in range(0, min(64, h)):
    for x in range(0, w):
        b, g, r = px[(y * w + x) * 4], px[(y * w + x) * 4 + 1], px[(y * w + x) * 4 + 2]
        if sys.argv[2] == 'green' and g > r + 50 and g > b + 30: n += 1
        if sys.argv[2] == 'red' and r > 180 and g < 100 and b < 100: n += 1
print(n)
PY
}
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
		elif [ "$(band "$L.elsm" green)" -gt 10 ] && [ "$(band "$L.elsm" red)" = 0 ]; then
			echo "  ok    $u trusted (the padlock green)"
		else
			echo "  FAIL  $u trusted, but the padlock not green"; fail=1
		fi
	done
	# "Proceed" on the error page (its button, client coordinates of the default window)
	L="$OUT/tls/$tls-proceed"
	run "https://self-signed.badssl.com/" "$L" "down 684 316;up 684 316;$(waits 250)"
	n=$(grep -a -c "sim: tcp_connect self-signed.badssl.com" "$L.log")
	if [ "$n" -ge 2 ] && [ "$(grep -a -c 'ONYX-FETCH FAIL https://self-signed' "$L.log")" = 1 ]; then
		echo "  ok    proceed: the page fetched again, accepted"
	else
		echo "  FAIL  proceed ($n connections)"; fail=1
	fi
	if [ "$(band "$L.elsm" red)" -gt 10 ] && [ "$(band "$L.elsm" green)" = 0 ]; then
		echo "  ok    proceed: the padlock red"
	else
		echo "  FAIL  proceed: the padlock not red"; fail=1
	fi
done
unset NS_MBEDTLS
# the certificate viewer (mbedTLS): "View certificate details" on the error page
L="$OUT/tls/viewer"
run "https://expired.badssl.com/" "$L" "down 180 282;up 180 282;$(waits 150)"
python3 tools/tests/desktop_sim/shot.py "$L.elsm" "$L.png" >/dev/null 2>&1
echo "  (the certificate viewer: $L.png)"
# the padlock clicked: the page's chain in the viewer (a trusted site's; past "Proceed", the faulty one)
for c in "badssl.com:green" "self-signed.badssl.com:red"; do
	h=${c%%:*}; col=${c#*:}
	L="$OUT/tls/lock-$col"
	steps="dump $L-page.elsm;"
	[ $col = red ] && steps="down 684 316;up 684 316;$(waits 250)$steps"
	run "https://$h/" "$L" "${steps}down 214 19;up 214 19;$(waits 150)"
	python3 tools/tests/desktop_sim/shot.py "$L-page.elsm" "$L-page.png" >/dev/null 2>&1
	python3 tools/tests/desktop_sim/shot.py "$L.elsm" "$L.png" >/dev/null 2>&1
	if [ "$(band "$L-page.elsm" $col)" -gt 10 ] && [ "$(band "$L.elsm" $col)" = 0 ] &&
	   [ "$(band "$L.elsm" green)" = 0 ]; then
		echo "  ok    the $col padlock opens the certificate viewer ($L.png)"
	else
		echo "  FAIL  the $col padlock: $L-page.png, $L.png"; fail=1
	fi
done
[ $fail = 0 ] && echo "all passed" || exit 1
