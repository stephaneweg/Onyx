#!/bin/sh
# tools/tests/telegram/run_tgclient_test.sh -- the Telegram app's core: offline (tgunit_test.cpp: the TL codec and the
# crypto; tgmodel_test.cpp: the client's model fed the server's objects, SRP), then against Telegram's servers
# (tgclient_test.cpp says what is checked). Needs the network and an api_id / api_hash of the developer's
# (my.telegram.org) in TG_API_ID / TG_API_HASH; without them it is skipped. g++, the vendored mbedTLS and zlib.
set -e
cd "$(dirname "$0")/../../.."
O=${TG_TMP:-/tmp/tg_test}
mkdir -p "$O/mb" "$O/z"
if [ ! -f "$O/libmbedcrypto.a" ]; then
	ls third_party/mbedtls-3.6.3/library/*.c | xargs -P 8 -I{} sh -c 'gcc -O2 -w -Ithird_party/mbedtls-3.6.3/include -Ithird_party/mbedtls-3.6.3/library -c {} -o '"$O"'/mb/$(basename {} .c).o'
	ar rcs "$O/libmbedcrypto.a" "$O"/mb/*.o
fi
if [ ! -f "$O/libz.a" ]; then
	for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c third_party/zlib-1.3.1/$f.c -o "$O/z/$f.o"; done
	ar rcs "$O/libz.a" "$O"/z/*.o
fi
CXX="g++ -std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers -Iuser/Apps/telegram -Itools/tests/telegram -Ithird_party/mbedtls-3.6.3/include -Ithird_party/zlib-1.3.1"
$CXX tools/tests/telegram/tgunit_test.cpp "$O/libmbedcrypto.a" "$O/libz.a" -o "$O/tgunit"
"$O/tgunit"
$CXX tools/tests/telegram/tgmodel_test.cpp "$O/libmbedcrypto.a" "$O/libz.a" -o "$O/tgmodel"
"$O/tgmodel"
if [ -z "$TG_API_ID" ]; then echo "TG_API_ID / TG_API_HASH unset: the network part skipped"; exit 0; fi
$CXX tools/tests/telegram/tgclient_test.cpp "$O/libmbedcrypto.a" "$O/libz.a" -o "$O/tgclient"
"$O/tgclient"
