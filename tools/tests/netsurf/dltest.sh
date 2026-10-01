#!/bin/sh
# tools/tests/netsurf/dltest.sh -- Jet Browser's page zoom, status bar and downloads (docs/06 section 38)
# on the PC bench, with the local HTTP server (httpsrv.py: /dl/<name>?n=&cd= sends n bytes with a
# Content-Disposition, /status/<code> a page with that status) and two pages (pages/jet-zoom.html,
# pages/jet-dl.html: links 40 px high, one under the other).
#
#   the zoom      Ctrl++ / Ctrl+= / Ctrl+- / Ctrl+0, the toolbar's "-  100%  +", Ctrl+wheel: Chrome's
#                 steps; what the page's scripts see (devicePixelRatio, innerWidth: CSS px); a click
#                 through the zoom lands where the page shows it; kept per site in data/view (launched
#                 again: the same zoom); Ctrl+0 forgets it.
#   the status    "Ready" once loaded, the link under the pointer, "404 Not Found" / "500 Internal Server
#   bar           Error" in red, a connection that fails; View > Hide Status Bar: the page taller, kept.
#   downloads     the Save dialog's name (Content-Disposition filename / filename* / the address / <a
#                 download="name"> / a data: URL / a script's blob: / an HTML page sent as an attachment /
#                 a name made safe), SD:/Downloads made when missing, the bytes written exactly (a 3 MB
#                 chunked body too), Esc in the dialog: nothing written; a slow download cancelled from
#                 the toolbar's downloads menu: its partial file removed.
#
#   sh tools/tests/netsurf/dltest.sh            (OUT=/tmp/nsbench PORT=8172 by default)
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8172}
D="$OUT/dl"
mkdir -p "$D"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
DATA="$OUT/build/data"
rm -f "$DATA/view"
python3 $T/httpsrv.py $T/pages $PORT > "$D/srv.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null; rm -f "$DATA/view"' EXIT
sleep 1
export SIM_WRITES="$D/writes" SIM_RAM="$D/ram"
rm -rf "$SIM_WRITES" "$SIM_RAM"
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
W=$(waits 60)
fail=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; fail=1; fi; }
run() {	# run <name> <page> <sim script>: the log in $D/<name>.log
	SIM_REALNET=1 SIM_SCREEN=1024x700 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
		SIM_ARGS="http://127.0.0.1:$PORT/$2" SIM="$3exit" \
		timeout 300 "$OUT/build/netsurf" >"$D/$1.log" 2>&1
}
has() { grep -a -q -F -- "$2" "$D/$1.log"; }
first() { grep -a -m1 -F -- "$2" "$D/$1.log"; }	# the first line with that
png() { python3 tools/tests/desktop_sim/shot.py "$D/$1.elsm" "$D/$1.png" >/dev/null; }
ctrl() { printf 'mods 1;key %s;mods 0;' "$1"; waits 25; }
click() { printf 'move %s %s;wait;down %s %s;up %s %s;' "$1" "$2" "$1" "$2" "$1" "$2"; waits "${3:-40}"; }
link() { click 60 $((40 + 40 * $1 + 20)) "${2:-40}"; }	# the page's link n (40 px each, under the 40 px band)
# the window: 960 x 550 (the screen 1024 x 700); the zoom control at 858..954 (no downloads' button yet):
# - at 871, the % at 906, + at 941, the band's y 19
ZM="871 19"; ZP="906 19"; ZPLUS="941 19"

echo "the zoom"
run z1 jet-zoom.html "${W}$(ctrl +)$(ctrl =)dump $D/zoom-125.elsm;$(ctrl -)$(ctrl 0)$(click $ZPLUS 15)$(click $ZPLUS 15)$(click $ZPLUS 15)$(click $ZPLUS 15)$(click $ZPLUS 15)$(click $ZM 15)$(click $ZPLUS 15)dump $D/zoom-200.elsm;$(click 300 290 20)mods 1;wheel 300 300 1;mods 0;$(waits 25)quit;$(waits 40)"
png zoom-125; png zoom-200
check "Ctrl++ then Ctrl+=: 110 % then 125 % (the page's scripts: devicePixelRatio 1.25, innerWidth 753)" \
	"has z1 'ONYX-ZOOM site=127.0.0.1 zoom=110' && has z1 'zoominfo dpr 1.25 w 753'"
zooms() { grep -a -o "ONYX-ZOOM site=127.0.0.1 zoom=[0-9]*" "$D/$1.log" | sed 's/.*zoom=//' | tr '\n' ' '; }
check "Ctrl+-: 110 %, Ctrl+0: 100 % (innerWidth 942 again); then + x5, -, +, Ctrl+wheel: Chrome's steps" \
	"[ \"\$(zooms z1)\" = '110 125 110 100 110 125 150 175 200 175 200 250 ' ] && has z1 'dpr 1 w 942'"
check "the toolbar's + (five times), - then +: 200 % (dpr 2, innerWidth 471)" "has z1 'zoominfo dpr 2 w 471'"
check "a click at 300,290 at 200 %: the page's 150,125 -- on the red box (100..200 x 100..150 CSS px)" \
	"has z1 'hit-target 150,125'"
check "Ctrl+wheel forward: 250 % (not a scroll)" "has z1 'zoom=250' && ! has z1 'sy 50'"
check "kept for the site: data/view 'zoom 127.0.0.1 250'" "grep -q '^zoom 127.0.0.1 250$' '$DATA/view'"
run z2 jet-zoom.html "${W}$(click $ZP 15)quit;$(waits 40)"
check "launched again: the site's 250 % at once (its first layout)" \
	"first z2 zoominfo | grep -q 'dpr 2.5 '"
check "a click on the %: 100 % -- forgotten (no zoom line left)" \
	"has z2 'zoom=100' && ! grep -q '^zoom ' '$DATA/view'"

echo "the status bar"
run s1 jet-dl.html "${W}move 60 70;$(waits 5)move 800 300;$(waits 5)$(link 8 60)dump $D/status-404.elsm;"
png status-404
check "'Ready' once the page is loaded" "has s1 'ONYX-STATUSBAR state=Ready'"
check "the link under the pointer: its address" \
	"has s1 'ONYX-STATUSBAR link=http://127.0.0.1:$PORT/dl/x.bin?n=200000&cd=attach'"
check "off the link: the state again" "grep -a -A1 'link=.*cd=attach\$' '$D/s1.log' | grep -q 'state=Ready'"
check "a 404 page: '404 Not Found', in red" "has s1 'ONYX-STATUSBAR state=404 Not Found (error)'"
run s2 jet-dl.html "${W}$(link 9 60)$(link 10 80)"
check "a 500 page: '500 Internal Server Error'" "has s2 'state=500 Internal Server Error (error)'"
run s3 jet-dl.html "${W}$(link 10 80)"
check "a port with no server: 'Error: Connection failed'" "has s3 'state=Error: Connection failed (error)'"
run s4 jet-zoom.html "${W}menu 12;$(waits 30)dump $D/status-hidden.elsm;quit;$(waits 40)"
run s5 jet-zoom.html "${W}menu 12;$(waits 30)quit;$(waits 40)"
png status-hidden
check "View > Hide Status Bar: the page 22 px taller (innerHeight 470 -> 492)" \
	"first s4 zoominfo | grep -q ' h 470 ' && has s4 ' h 492 '"
check "launched again: still hidden (kept: the same window, the page 492 high), then shown again (470)" \
	"first s5 zoominfo | grep -q ' h 492 ' && has s5 ' h 470 ' && grep -q '^status_bar 1' '$DATA/view'"

echo "downloads"
# each: the link, then Enter in the Save dialog
save() { printf '%skey 13;' "$(link "$1" 40)"; waits "${2:-30}"; }
run d1 jet-dl.html "${W}$(link 0 40)dump $D/dl-dialog.elsm;key 13;$(waits 30)$(save 1)$(save 2 80)$(save 3)$(save 4)$(save 5)$(save 6)$(save 7)$(link 0 40)key 27;$(waits 20)dump $D/dl-done.elsm;$(click 937 19 10)dump $D/dl-menu.elsm;key 27;$(waits 5)"
png dl-dialog; png dl-done; png dl-menu
F="$SIM_WRITES/Downloads"
same() {	# same <file> <n>: the server's n bytes ((i * 7 + 3) mod 251)
	python3 - "$F/$1" "$2" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
n = int(sys.argv[2])
sys.exit(0 if d == bytes((i * 7 + 3) % 251 for i in range(n)) else 1)
PY
}
check "SD:/Downloads made (it was not there)" "has d1 'ONYX-DOWNLOAD mkdir SD:/Downloads made'"
check "the dialog: SD:/Downloads, the Content-Disposition's name filled in" \
	"has d1 'ask dir=SD:/Downloads name=report 2026.pdf'"
check "saved: report 2026.pdf, its 200000 bytes exactly" "same 'report 2026.pdf' 200000"
check "filename*=UTF-8''r%C3%A9sum%C3%A9%20%E2%82%AC.txt: 'resume EUR.txt' (ASCII for the card)" \
	"same 'resume EUR.txt' 5000"
check "no disposition: the address's last segment decoded ('data set.bin'), 3 MB chunked exactly" \
	"same 'data set.bin' 3000000"
check "<a download=\"custom.txt\"> on a text file (shown otherwise): saved as custom.txt" \
	"cmp -s '$F/custom.txt' $T/pages/jet-plain.txt"
check "<a download=\"hello.txt\" href=\"data:...\">: hello.txt" "[ \"\$(cat '$F/hello.txt')\" = 'hello data' ]"
check "a script's blob: and a.click(): blob.txt (its UTF-8 bytes)" \
	"[ \"\$(od -An -tx1 '$F/blob.txt' | tr -d ' \\n')\" = '626c6f6220627974657320c3a90a' ]"
check "an HTML page sent as an attachment: saved, not shown (page.html)" "same page.html 500"
check "filename=\"../x/a:b*c?.txt\": its last segment, made safe: a_b_c_.txt" "same a_b_c_.txt 10"
check "Esc in the dialog: cancelled, nothing written" \
	"has d1 'ONYX-DOWNLOAD cancelled id=9' && [ \$(ls '$F' | wc -l) = 8 ]"
check "the status bar: the last download's end" "has d1 'Download cancelled: report 2026.pdf'"
rm -rf "$SIM_WRITES"
# the slow one: saved, then cancelled from the downloads' menu (its first row, then Yes)
run d2 jet-dl.html "${W}$(save 11 30)$(click 937 19 10)dump $D/dl-running.elsm;$(click 800 60 10)key 13;$(waits 40)"
png dl-running
check "a download running: its progress in the status bar" "has d2 'Downloading slow.bin: '"
check "cancelled from the downloads' menu: the fetch stopped, the partial file removed" \
	"has d2 'ONYX-DOWNLOAD cancelled id=1' && [ ! -e '$F/slow.bin' ] && grep -q 'DLABORT' '$D/srv.log'"
echo "  (screenshots: $D/*.png)"
[ $fail = 0 ] && echo "all passed" || exit 1
