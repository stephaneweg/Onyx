#!/bin/sh
# tools/tests/netsurf/intl/build.sh -- builds qjsintl (QuickJS + NetSurf's Intl, the PC) in
# $OUT/intl (OUT default /tmp/nsbench): intl.js and the locale data as qjs_intl_js.h, as the
# makefiles make it. Prints the binary's path.
set -e
cd "$(dirname "$0")/../../../.."
OUT=${OUT:-/tmp/nsbench}
B=$OUT/intl
Q=third_party/quickjs-ng-0.17.0
J=third_party/netsurf/content/handlers/javascript/quickjs
mkdir -p "$B"
sh tools/tests/netsurf/intl/genh.sh "$J/intl.js" third_party/cldr-48/intl-data.txt "$B/qjs_intl_js.h"
for f in quickjs libregexp libunicode dtoa; do
	[ "$B/$f.o" -nt "$Q/$f.c" ] || cc -O2 -std=gnu11 -w -I$Q -c "$Q/$f.c" -o "$B/$f.o"
done
cc -O1 -g -std=gnu11 -Wall -Wno-unused-function -I$Q -I$J -I"$B" tools/tests/netsurf/intl/qjsintl.c \
	"$B/quickjs.o" "$B/libregexp.o" "$B/libunicode.o" "$B/dtoa.o" -lm -o "$B/qjsintl"
echo "$B/qjsintl"
