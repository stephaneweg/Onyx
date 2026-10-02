#!/bin/sh
# tools/tests/run_mail_test.sh -- Mail's protocol layer (user/mail/: IMAP, POP3, SMTP, MIME, OAuth) built for the PC
# (the stand-in kernel, mbedTLS) and run against tools/tests/mail/fakemail.py on local ports (IMAP also over a
# self-signed TLS: refused when checked, accepted when the account says not to check).
set -e
cd "$(dirname "$0")/../.."
OUT=${MAIL_TEST_TMP:-/tmp/onyx_mail_test}
M=third_party/mbedtls-3.6.3
mkdir -p "$OUT/mb"
CXX="g++ -std=gnu++17 -O1 -g -w -I user -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
if [ ! -f "$OUT/libmb.a" ]; then
	for f in $M/library/*.c; do gcc -O1 -w -I$M/include -I$M/library -c $f -o "$OUT/mb/$(basename $f .c).o" & done; wait
	ar rcs "$OUT/libmb.a" "$OUT"/mb/*.o
fi
$CXX -c tools/tests/desktop_sim/fakekapi.cpp -o "$OUT/fakekapi.o"
$CXX -I$M/include tools/tests/mail/mailtest.cpp "$OUT/fakekapi.o" "$OUT/libmb.a" -lpthread -o "$OUT/mailtest"
# a self-signed certificate for localhost
[ -f "$OUT/cert.pem" ] || openssl req -x509 -newkey rsa:2048 -nodes -subj /CN=localhost -days 30 \
	-keyout "$OUT/cert.pem" -out "$OUT/crt.pem" 2>/dev/null && cat "$OUT/crt.pem" >> "$OUT/cert.pem"
B=$(( 20000 + $$ % 20000 ))
python3 tools/tests/mail/fakemail.py --imap $B --pop $((B+1)) --smtp $((B+2)) --http $((B+3)) --imaps $((B+4)) --cert "$OUT/cert.pem" >"$OUT/server.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
for i in 1 2 3 4 5 6 7 8 9 10; do grep -q ready "$OUT/server.log" 2>/dev/null && break; sleep 0.3; done
SIM_REALNET=1 SIM_REALCLOCK=1 IMAP_PORT=$B POP_PORT=$((B+1)) SMTP_PORT=$((B+2)) HTTP_PORT=$((B+3)) IMAPS_PORT=$((B+4)) "$OUT/mailtest" 2>"$OUT/sim.log"
rm -f out.elsm
echo "mail: all good"
