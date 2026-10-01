#!/bin/sh
# tools/tests/netsurf/uatest.sh -- the toolbar's pill (a site's version: Standard, Mobile, Desktop;
# docs/06 section 35) and its padlock, on the PC bench with a local HTTP server (httpsrv.py's /ua: the
# User-Agent the request carried, shown in the page, logged "UA n ..."; fresh for an hour: the
# disk cache answers the next launch). Checks: the default User-Agent (NetSurf's); the pill's
# menu (a click: Mobile, then Desktop) -- the page loaded again with Chrome on Android's, then
# Chrome on Windows', the choice written to site-modes; launched again, each version from the
# disk cache under its own key ("M|", "D|", none for Standard: onyx_cache.c's oc_key_as) -- the
# page shown is the version asked for, never the other one's; the older desktop-sites file
# still read; jet.ini's [sites] line wins ("Custom", the menu's versions greyed); the padlock
# hidden-or-grey on http (no green). Screenshots in $OUT/ua/ (the menu open: ua-menu.png).
#
#   sh tools/tests/netsurf/uatest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8125}
SITES=${SITES:-/tmp/nssites}
mkdir -p "$OUT/ua" "$SITES"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
DATA="$OUT/build/data"
clean() { rm -f "$DATA/site-modes" "$DATA/desktop-sites" "$DATA/jet.ini"; }
clean
python3 $T/httpsrv.py "$SITES" $PORT > "$OUT/ua/srv.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null; clean' EXIT
sleep 1
export SIM_WRITES="$OUT/ua/writes" SIM_RAM="$OUT/ua/ram"
rm -rf "$SIM_WRITES" "$SIM_RAM"
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
W=$(waits 100)
Q="quit;$W"		# (the app ends as when its window is closed: the disk cache written)
# the pill (client coordinates: its right end at the window's right less 6, the band 40 high)
# and its menu's rows under it (Standard, Mobile, Desktop)
PILL="move 940 19;wait;down 940 19;$(waits 20)"
pick() { printf 'up 940 19;wait;wait;move 850 %s;wait;down 850 %s;up 850 %s;' "$1" "$1" "$1"; waits 100; }
MOBILE=114; DESKTOP=140
UA_NS="Mozilla/5.0 (X11; Linux aarch64) NetSurf/3.12"
UA_AND="Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Mobile Safari/537.36"
UA_WIN="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Safari/537.36"
fail=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; fail=1; fi; }
run() {	# run <name> <sim script>: the log's new server lines in $OUT/ua/<name>.srv
	n0=$(wc -l < "$OUT/ua/srv.log")
	SIM_REALNET=1 SIM_SCREEN=1024x700 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
		SIM_ARGS="http://127.0.0.1:$PORT/ua" SIM="$2exit" \
		timeout 300 "$OUT/build/netsurf" >"$OUT/ua/$1.log" 2>&1
	tail -n +$((n0 + 1)) "$OUT/ua/srv.log" > "$OUT/ua/$1.srv"
}
shown() { grep -q -F -- "console: page-ua $2 | nav $2" "$OUT/ua/$1.log"; }	# the page shows it, the scripts see it
asked() { grep -q -F -- "UA " "$OUT/ua/$1.srv"; }
sent() { grep -q -F -- "$2" "$OUT/ua/$1.srv"; }
png() { python3 tools/tests/desktop_sim/shot.py "$OUT/ua/$1.elsm" "$OUT/ua/$1.png" >/dev/null; }
# colours in the band: green / red pixels (the padlock) among its first 64 rows
band() {
	python3 - "$OUT/ua/$1.elsm" "$2" <<'PY'
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

echo "the site's version (the pill)"
run a "${W}dump $OUT/ua/ua-standard.elsm;${PILL}dump $OUT/ua/ua-menu.elsm;$(pick $MOBILE)dump $OUT/ua/ua-mobile.elsm;${PILL}$(pick $DESKTOP)dump $OUT/ua/ua-desktop.elsm;$Q"
for f in ua-standard ua-menu ua-mobile ua-desktop; do png $f; done
check "Standard: NetSurf's User-Agent sent and seen" "sent a 'UA 1 $UA_NS' && shown a '$UA_NS'"
check "the pill's menu: Mobile -- the page again with Chrome on Android's" "sent a '$UA_AND' && shown a '$UA_AND'"
check "then Desktop -- with Chrome on Windows'" "sent a '$UA_WIN' && shown a '$UA_WIN'"
check "the choice kept: site-modes '127.0.0.1 desktop'" "grep -q '^127.0.0.1 desktop$' '$DATA/site-modes'"
check "http: no green padlock" "[ \$(band ua-standard green) = 0 ]"

echo "the disk cache: one copy per version (launched again, RAM: kept)"
run b "${W}$Q"
check "Desktop: from the cache (no request), Chrome on Windows' page" "! asked b && shown b '$UA_WIN'"
echo "127.0.0.1 mobile" > "$DATA/site-modes"
run c "${W}$Q"
check "Mobile: Chrome on Android's page (its own copy or asked again, never Desktop's)" \
	"shown c '$UA_AND' && ! sent c '$UA_WIN' && ! sent c '$UA_NS'"
rm -f "$DATA/site-modes"
run d "${W}$Q"
check "Standard: NetSurf's page (its own copy: no M| / D| prefix)" "shown d '$UA_NS' && ! sent d 'Chrome'"
run e "${W}$Q"
check "Standard again: from the cache" "! asked e && shown e '$UA_NS'"

echo "compatibility, jet.ini"
rm -rf "$SIM_RAM"
echo "127.0.0.1" > "$DATA/desktop-sites"
run f "${W}$Q"
check "the older desktop-sites file read: Desktop" "sent f '$UA_WIN' && shown f '$UA_WIN'"
rm -f "$DATA/desktop-sites"
printf '[sites]\n127.0.0.1 = UATest/1.0 (jet.ini)\n' > "$DATA/jet.ini"
run g "${W}dump $OUT/ua/ua-custom.elsm;${PILL}dump $OUT/ua/ua-custom-menu.elsm;$(pick $MOBILE)$Q"
png ua-custom; png ua-custom-menu
check "jet.ini's [sites] line wins (Custom), the menu changes nothing" \
	"sent g 'UATest/1.0 (jet.ini)' && ! sent g 'Chrome' && [ ! -e '$DATA/site-modes' ]"
echo "the address bar: words to the search engine (jet.ini's [search] engine), an address to its site"
typed() {	# a click in the address field (all of it selected), the text's keys, Enter
	printf 'move 400 19;wait;down 400 19;up 400 19;wait;wait;'
	python3 -c 'import sys; print("".join("key 0x20;" if c == " " else "key %s;" % c for c in sys.argv[1]), end="")' "$1"
	printf 'wait;key 13;'; waits 100; }
printf '[search]\nengine = http://127.0.0.1:%s/ua?s=%%s&from=jet\n' $PORT > "$DATA/jet.ini"
run h "${W}$(typed 'hi there')$Q"
check "words: the engine's URL, the words in place of %s (spaces as +)" "sent h 'REQ' && grep -q -F '/ua?s=hi+there&from=jet ' '$OUT/ua/h.srv'"
printf '[search]\nengine = http://127.0.0.1:%s/ua?q=\n' $PORT > "$DATA/jet.ini"
run i "${W}$(typed 'c++ & co')$(typed "127.0.0.1:$PORT/ua?addr")$Q"
check "words: appended to the engine, escaped" "grep -q -F '/ua?q=c%2B%2B+%26+co ' '$OUT/ua/i.srv'"
check "an address (host:port/path, no scheme): its site in https (the server here speaks http), not a search" \
	"grep -q -F 'https://127.0.0.1:$PORT/ua?addr' '$OUT/ua/i.log' && ! grep -q -F 'addr' '$OUT/ua/i.srv'"
printf '[search]\nengine = http://127.0.0.1:%s/ua?q=\n' $PORT > "$DATA/jet.ini"
run j "${W}$(typed 'caf' | sed 's/wait;key 13;.*//')key 0xE9;key 0x20;key 0x80;wait;key 13;$(waits 100)$Q"
check "accented words (Latin-1 keys): UTF-8, escaped" "grep -q -F '/ua?q=caf%C3%A9+%E2%82%AC ' '$OUT/ua/j.srv'"
echo "  (screenshots: $OUT/ua/ua-standard.png ua-menu.png ua-mobile.png ua-desktop.png ua-custom-menu.png)"
[ $fail = 0 ] && echo "all passed" || exit 1
