#!/bin/sh
# ports.sh -- the ports' smoke tools (tools/ports) on the posixsim bench: relinked from their build
# trees / the sysroot against the bench's libonyxposix + fakekapi, then run under qemu-user on a few
# real jobs:
#   sqlite3  a database on RAM: and on SD: (10 000 rows in a transaction, an index, a query, the
#            database opened again), the rollback journal
#   xmllint  a document parsed, an XPath query, a malformed one refused
#   curl     HTTP and HTTPS (a self-signed certificate, -k) from a local Python server; a page
#            written with -o, compared
#   icutest  ICU: collation, break iterators (Thai, Japanese), converters, Intl formatting... (its
#            own checks: tools/ports/icu/icutest.cpp)
#   hbtest   HarfBuzz with the card's fonts (sdcard/res/fonts copied to SD:/res/fonts) + GNU
#            FreeSerif from the PC when present, for Devanagari (no font on the card has it)
#   skiatest Skia: the scene rendered into RAM:/skiatest.png (kept in $POSIXSIM_ROOT/RAM), checked
#   skiademo linked only (it opens a window: the Pi)
# PASS / FAIL per job; the exit status is the number of failures. Needs the ports built first
# (sh tools/ports/build-all.sh), qemu-aarch64-static, python3 and openssl.
#   sh tools/tests/posixsim/ports.sh [c] [webkit]          # default both (each only if it is built)
#   POSIXSIM_LEVEL=74 sh tools/tests/posixsim/ports.sh      # on libonyxposix's fallbacks
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../../.." && pwd)
: "${POSIXSIM_ROOT:=/tmp/posixsim}"
: "${POSIXSIM_QEMU:=$(command -v qemu-aarch64-static || command -v qemu-aarch64)}"
export POSIXSIM_ROOT POSIXSIM_QEMU
GROUPS_ARG=$*
# the toolchain, sysroot and build trees build-all.sh used (tools/onyx-env.sh: ONYX_TOOLCHAIN_PREFIX,
# else aarch64-onyx-elf when installed, else aarch64-none-elf)
set --
ONYX_ROOT=$ONYX ONYX_ENV_QUIET=1 . "$ONYX/tools/onyx-env.sh"
case $ONYX_TOOLCHAIN_PREFIX in
aarch64-onyx-elf-) : "${PORTS_OUT:=$ONYX/out/ports-onyx}";;
*) : "${PORTS_OUT:=$ONYX/out/ports}";;
esac
PREFIX=$ONYX_TOOLCHAIN_PREFIX
export PREFIX
echo "ports.sh: $ONYX_TOOLCHAIN_PREFIX, sysroot $ONYX_SYSROOT, $PORTS_OUT"
S=$ONYX_SYSROOT
PB=$PORTS_OUT/build
ok () { echo "PASS  $1"; }
ko () { echo "FAIL  $1 ($2)"; fails=$((fails + 1)); }
run () { name=$1; shift; POSIXSIM_ARGV0="SD:/bin/$name" "$POSIXSIM_QEMU" "$POSIXSIM_ROOT/SD/bin/$name" "$@"; }

groups=${GROUPS_ARG:-c webkit}
do_c=; do_wk=
case " $groups " in *" c "*) [ -f "$S/lib/libsqlite3.a" ] && [ -f "$PB/curl/lib/libcurl.a" ] && [ -f "$PB/libxml2/libxml2.a" ] && do_c=1;; esac
case " $groups " in *" webkit "*) [ -f "$S/lib/libskia.a" ] && [ -f "$S/lib/libicuuc.a" ] && [ -f "$S/lib/libharfbuzz.a" ] && do_wk=1;; esac
[ -n "$do_c$do_wk" ] || { echo "ports.sh: build the ports first: sh tools/ports/build-all.sh" >&2; exit 2; }
R=$POSIXSIM_ROOT
mkdir -p "$R/RAM" "$R/SD/tmp"
fails=0

if [ -n "$do_c" ]; then

# ---- relink the tools for the bench ----
SQDEFS="-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_MAX_MMAP_SIZE=0 -DHAVE_USLEEP=1 -DHAVE_LOCALTIME_R=1 -DHAVE_GMTIME_R=1 -DHAVE_READLINE=0 -DHAVE_EDITLINE=0 -DSQLITE_OMIT_POPEN"
BUILD_ONLY=1 PROG=$ONYX/third_party/sqlite-3.50.4/shell.c NAME=sqlite3 CFLAGS_EXTRA="$SQDEFS" \
	OBJS="-L$S/lib -lsqlite3" sh "$HERE/run.sh" || exit 2
BUILD_ONLY=1 PROG=none NAME=xmllint OBJS="$PB/libxml2/CMakeFiles/xmllint.dir/xmllint.c.o $PB/libxml2/libxml2.a $S/lib/libz.a -lm" \
	sh "$HERE/run.sh" || exit 2
BUILD_ONLY=1 PROG=none NAME=curl OBJS="$(ls "$PB"/curl/src/CMakeFiles/curl.dir/*.o) $PB/curl/lib/libcurl.a $S/lib/libmbedtls.a $S/lib/libmbedx509.a $S/lib/libmbedcrypto.a $S/lib/libz.a $S/lib/libbrotlidec.a $S/lib/libnghttp2.a" \
	sh "$HERE/run.sh" || exit 2

rm -f "$R/RAM/t.db" "$R/SD/tmp/t.db"

# ---- sqlite3 (the SQL on stdin: the old kernel's argument line splits at every space) ----
for db in RAM:/t.db SD:/tmp/t.db; do
	q="CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); BEGIN; WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<10000) INSERT INTO t(b) SELECT 'row ' || x FROM c; COMMIT; CREATE INDEX ib ON t(b); SELECT count(*), max(a) FROM t;"
	out=$(echo "$q" | run sqlite3 -batch "$db" 2>&1)
	[ "$out" = "10000|10000" ] && ok "sqlite3 $db: 10 000 rows, an index" || ko "sqlite3 $db: 10 000 rows" "$out"
	out=$(echo "SELECT b FROM t WHERE b = 'row 4242'; PRAGMA integrity_check;" | run sqlite3 -batch "$db" 2>&1)
	[ "$out" = "row 4242
ok" ] && ok "sqlite3 $db: opened again, a query by the index, integrity_check" || ko "sqlite3 $db: opened again" "$out"
	out=$(echo "BEGIN; DELETE FROM t; ROLLBACK; SELECT count(*) FROM t;" | run sqlite3 -batch "$db" 2>&1)
	[ "$out" = "10000" ] && ok "sqlite3 $db: a rollback (the journal)" || ko "sqlite3 $db: rollback" "$out"
done

# ---- xmllint ----
printf '<?xml version="1.0"?>\n<library><book id="1"><title>Onyx</title></book><book id="2"><title>Circle</title></book></library>\n' > "$R/SD/tmp/a.xml"
printf '<a><b></a>\n' > "$R/SD/tmp/bad.xml"
out=$(run xmllint --noout SD:/tmp/a.xml 2>&1) && [ -z "$out" ] && ok "xmllint --noout: a valid document" || ko "xmllint --noout" "$out"
out=$(run xmllint --xpath "string(//book[@id='2']/title)" SD:/tmp/a.xml 2>&1)
[ "$out" = "Circle" ] && ok "xmllint --xpath" || ko "xmllint --xpath" "$out"
run xmllint --noout SD:/tmp/bad.xml >/dev/null 2>&1 && ko "xmllint: a malformed document refused" "exit 0" || ok "xmllint: a malformed document refused"

# ---- curl (a local server: HTTP and HTTPS) ----
W=$R/www
mkdir -p "$W"
head -c 300000 /dev/urandom | base64 > "$W/page.txt"
PORT=$((20000 + $$ % 20000))
( cd "$W" && exec python3 -m http.server "$PORT" --bind 127.0.0.1 ) >/dev/null 2>&1 &
HTTP=$!
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$R/key.pem" -out "$R/cert.pem" -days 1 -subj "/CN=localhost" >/dev/null 2>&1
cat > "$R/tls.py" <<EOF
import http.server, ssl, os
os.chdir("$W")
s = http.server.HTTPServer(("127.0.0.1", $((PORT + 1))), http.server.SimpleHTTPRequestHandler)
c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); c.load_cert_chain("$R/cert.pem", "$R/key.pem")
s.socket = c.wrap_socket(s.socket, server_side=True)
s.serve_forever()
EOF
python3 "$R/tls.py" >/dev/null 2>&1 &
HTTPS=$!
sleep 1
rm -f "$R/RAM/page.txt" "$R/RAM/page2.txt"
out=$(run curl -s -S -o RAM:/page.txt "http://127.0.0.1:$PORT/page.txt" 2>&1)
cmp -s "$W/page.txt" "$R/RAM/page.txt" && ok "curl http:// -o RAM:/page.txt (400 KB, compared)" || ko "curl http://" "$out"
out=$(run curl -s -S -k -o RAM:/page2.txt "https://localhost:$((PORT + 1))/page.txt" 2>&1)
cmp -s "$W/page.txt" "$R/RAM/page2.txt" && ok "curl -k https://localhost (mbedTLS, the threaded resolver)" || ko "curl https://" "$out"
out=$(run curl -s -S -I "http://127.0.0.1:$PORT/page.txt" 2>&1 | head -1)
case "$out" in HTTP/1.?\ 200*) ok "curl -I";; *) ko "curl -I" "$out";; esac
out=$(run curl -s -S "http://127.0.0.1:1/" 2>&1)
case "$out" in *"Could not connect"*|*"Failed to connect"*|*"onnection refused"*) ok "curl to a closed port: a clean error";; *) ko "curl to a closed port" "$out";; esac
kill $HTTP $HTTPS 2>/dev/null
fi

# ---- WebKit's libraries: icutest, hbtest, skiatest (their own PASS / FAIL lines; each counts its
#      failures in its exit status), skiademo linked ----
if [ -n "$do_wk" ]; then
	PT=$ONYX/tools/ports
	WKCF="-Wno-attributes -idirafter $S/include -I$S/include/harfbuzz -I$S/include/freetype2 -I$S/include/skia"
	SKDEFS=$(PKG_CONFIG_LIBDIR=$S/lib/pkgconfig pkg-config --cflags-only-other skia)
	IMGLIBS="-lfreetype -lpng16 -ljpeg -lwebpmux -lwebpdemux -lwebp -lsharpyuv -lbrotlidec -lz"
	BUILD_ONLY=1 PROG=$PT/icu/icutest.cpp CFLAGS_EXTRA="$WKCF" OBJS="-L$S/lib -licui18n -licuuc -licudata" \
		sh "$HERE/run.sh" || exit 2
	BUILD_ONLY=1 PROG=$PT/harfbuzz/hbtest.cpp CFLAGS_EXTRA="$WKCF" OBJS="-L$S/lib -lharfbuzz $IMGLIBS" \
		sh "$HERE/run.sh" || exit 2
	BUILD_ONLY=1 PROG=$PT/skia/skiatest.cpp CFLAGS_EXTRA="$WKCF $SKDEFS" \
		OBJS="-L$S/lib -lharfbuzz-icu -licui18n -licuuc -licudata -lskia -lharfbuzz $IMGLIBS" sh "$HERE/run.sh" || exit 2
	BUILD_ONLY=1 PROG=$PT/skia/skiademo.cpp CFLAGS_EXTRA="$WKCF $SKDEFS -DSCENE_NO_ICU -I$ONYX/user" -I$ONYX/user/Kits" \
		OBJS="-L$S/lib -lskia -lharfbuzz $IMGLIBS" sh "$HERE/run.sh" || exit 2
	ok "skiademo linked (it opens a window: run it on the Pi)"
	mkdir -p "$R/SD/res/fonts"
	rm -f "$R/SD/res/fonts/"*
	cp "$ONYX"/sdcard/res/fonts/*.ttf "$R/SD/res/fonts/"
	for t in icutest hbtest skiatest; do
		if [ $t = hbtest ] && [ -f /usr/share/fonts/truetype/freefont/FreeSerif.ttf ]; then
			mkdir -p "$R/SD/tmp/fonts-deva"
			cp "$ONYX"/sdcard/res/fonts/*.ttf /usr/share/fonts/truetype/freefont/FreeSerif.ttf "$R/SD/tmp/fonts-deva/"
			set -- SD:/tmp/fonts-deva
		else
			set --
		fi
		out=$(run $t "$@" 2>&1)
		st=$?
		echo "$out" | grep -E "^(PASS|FAIL|SKIP)  " | sed "s/^\(PASS\|FAIL\|SKIP\)  /\1  $t: /"
		echo "$out" | grep -E "^$t: " | tail -1
		[ $st -eq 0 ] || ko "$t" "exit status $st"
	done
	[ -f "$R/RAM/skiatest.png" ] && echo "ports.sh: the scene: $R/RAM/skiatest.png"
fi

echo "ports.sh: $fails failed"
exit $fails
