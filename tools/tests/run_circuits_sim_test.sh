#!/bin/sh
# run_circuits_sim_test.sh -- the scripted checks of Circuits' window (user/Apps/circuits, AutoDev round 2) in the
# desktop simulator: the app built for the PC against the stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp,
# UIKit, FreeType, as shots.sh builds it; its own sources with -Wall -Wextra, no warning allowed), each case a run
# through a script of events with a fresh SIM_WRITES of its own (seeded with a progress.ini when the case wants one),
# then asserted on what the run wrote (progress.ini: the stars, the boards as circuit text, the levels open), on the
# clipboard (SIM_CLIPFILE) and on the pixels of its window's dumps (numpy, at points derived from the layout). One
# line a case: "ok ...", "FAIL ...". The dumps are made PNGs in $OUT (looked at by hand).
#
#   sh tools/tests/run_circuits_sim_test.sh          -> "circuits-sim: all N checks passed" (exit 0)
#
# The geometry (client coordinates at the default 1000 x 620, SIM_POS=8,34 so the window is not shrunk): the board
# is at (234, by) 508 wide, its cell 12 px, the grid's origin (ox, oy) inside it: a cell (gx, gy) is at
# x = 234 + ox + 12 gx, y = by + oy + 12 gy (cell () below); by depends on the card's height (the level's text):
# the test reads it from the app's log (CIRCUITS_TRACE=1 prints the layout).
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_circuits_sim
CXX="g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
FT=third_party/freetype-2.14.3
rm -rf "$OUT/w" "$OUT/log"; mkdir -p "$OUT/obj" "$OUT/ft" "$OUT/w" "$OUT/log"

# ---- the building (UIKit and FreeType kept between runs: rebuilt when a source is newer) --------------------
if [ ! -f "$OUT/libuikit.a" ] || [ -n "$(find user/Kits/uikit -newer "$OUT/libuikit.a" -name '*.[ch]*' | head -1)" ]; then
	for f in user/Kits/uikit/*.cpp; do $CXX -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
	rm -f "$OUT/libuikit.a"; ar rcs "$OUT/libuikit.a" "$OUT"/obj/*.o
fi
if [ ! -f "$OUT/libft.a" ]; then
	FT_SRC="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c"
	for f in $FT_SRC; do gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
		-Iuser/Kits/fontkit -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
	ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
fi
$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
# (the app's own sources without -w: no warning allowed; every .cpp of user/Apps/circuits but main.cpp beside it)
WFLAGS="-std=gnu++17 -O1 -Wall -Wextra -Wno-format-truncation -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I kernel/include -fno-exceptions -fno-rtti -Iuser/Kits/fontkit -I$FT/include"
: > "$OUT/circuits.warn"
for f in user/Apps/circuits/*.cpp; do
	g++ $WFLAGS -c "$f" -o "$OUT/c_$(basename "$f" .cpp).o" 2>> "$OUT/circuits.warn" || { cat "$OUT/circuits.warn"; echo "circuits-sim: FAIL the build"; exit 1; }
done
if grep -q "Apps/circuits/.*warning" "$OUT/circuits.warn"; then cat "$OUT/circuits.warn"; echo "circuits-sim: FAIL warnings in Circuits"; exit 1; fi
g++ -o "$OUT/circuits" "$OUT/fakekapi.o" "$OUT"/c_*.o "$OUT/libuikit.a" "$OUT/libft.a"
[ "$1" = build ] && { echo "circuits-sim: built $OUT/circuits"; exit 0; }

# ---- the geometry and the pixels (numpy on the dumps) -----------------------------------------------------------
cat > "$OUT/geo.py" <<'PY'
import struct, sys
import numpy as np
B, T = 4, 28						# the window's frame in a dump: its border, its title bar (fakekapi.cpp)
def load (f):
	d = open (f, 'rb').read (); m, w, h, x, y = struct.unpack ('<5i', d[:20])
	return np.frombuffer (d[20:20 + w * h * 4], dtype = '<u4').reshape (h, w) & 0xFFFFFF
a = sys.argv
if a[1] == 'by':					# by DUMP: the board's top (client y) -- the inputs' strip starts a row under it
	px = load (a[2])
	for y in range (T + 60, px.shape[0]):
		if px[y, B + 234 + 20] == 0xE2EAE4: print (y - T - 1); break
elif a[1] == 'cell':					# cell BY GX GY [W H]: the client point of the grid's (gx, gy) (cells, decimals)
	by = int (a[2]); W = int (a[5]) if len (a) > 5 else 1000; H = int (a[6]) if len (a) > 6 else 620
	mw = W - 10 - 238 - 10 - 234; bh = H - 10 - 50 - 8 - by
	c = max (10, min ((mw - 20) // 40, (bh - 20) // 30)); ox = (mw - 40 * c) // 2; oy = (bh - 30 * c) // 2
	print (234 + ox + int (round (float (a[3]) * c)), by + oy + int (round (float (a[4]) * c)))
elif a[1] == 'near':					# near DUMP X Y RRGGBB [TOL]: the client pixel (x, y) is that colour (each channel within TOL)
	px = load (a[2]); v = int (px[int (a[4]) + T, int (a[3]) + B]); w = int (a[5], 16); t = int (a[6]) if len (a) > 6 else 24
	ok = all (abs (((v >> s) & 255) - ((w >> s) & 255)) <= t for s in (0, 8, 16))
	if not ok: sys.stderr.write ('  (pixel %s,%s is %06X, not %06X)\n' % (a[3], a[4], v, w))
	sys.exit (0 if ok else 1)
elif a[1] == 'green':					# green DUMP X Y: a green pixel (a lit switch's key, a wire at 1)
	px = load (a[2]); v = int (px[int (a[4]) + T, int (a[3]) + B]); r, g, b = v >> 16, (v >> 8) & 255, v & 255
	sys.exit (0 if g > r + 40 and g > b + 20 else 1)
elif a[1] == 'purple':					# purple DUMP X Y: the lesson card's header
	px = load (a[2]); v = int (px[int (a[4]) + T, int (a[3]) + B]); r, g, b = v >> 16, (v >> 8) & 255, v & 255
	sys.exit (0 if b > 0xA0 and r > g + 30 and b > g + 60 else 1)
elif a[1] == 'stars':					# stars DUMP Y: the gold stars filled on the levels list's row at client y
	px = load (a[2]); n = 0
	for k in range (3):
		v = int (px[int (a[3]) + T, 166 + 14 * k + B]); r, g, b = v >> 16, (v >> 8) & 255, v & 255
		if r > 0xD0 and 0x90 < g < 0xD0 and b < 0x60: n += 1
	print (n)
elif a[1] == 'marks':					# marks DUMP Y: something drawn in the stars' column of the list's row at y (open)
	px = load (a[2]); band = px[int (a[3]) + T - 6: int (a[3]) + T + 7, 160 + B: 204 + B]
	print (len (np.unique (band)))
PY
geo () { python3 "$OUT/geo.py" "$@"; }

# ---- the running -------------------------------------------------------------------------------------------
PASS=0; FAILS=0
ok ()   { PASS=$((PASS + 1)); echo "ok   $*"; }
bad ()  { FAILS=$((FAILS + 1)); echo "FAIL $*"; }
check () { name=$1; shift; if "$@"; then ok "$name"; else bad "$name"; fi; }
PR=apps/circuits.app/progress.ini
# seed CASE [FIXTURE]: a fresh writes folder (with tools/tests/desktop_sim/circuits/FIXTURE.ini as progress.ini)
seed () {
	W="$OUT/w/$1"; rm -rf "$W"; mkdir -p "$W/apps/circuits.app"
	[ -n "$2" ] && cp $D/circuits/$2.ini "$W/$PR"
	return 0
}
# board CASE TEXT: the board of the level the case's progress.ini is on (its "level =") set to the circuit text
board () {
	lv=$(sed -n 's/^level = //p' "$OUT/w/$1/$PR")
	printf '%s.circuit = %s\n' "$lv" "$2" >> "$OUT/w/$1/$PR"
}
# french CASE: the system's language French for that case
french () { mkdir -p "$OUT/w/$1/etc"; { grep -v '^language' sdcard/etc/system.ini; echo "language=fr"; } > "$OUT/w/$1/etc/system.ini"; }
# run CASE "SCRIPT" [VAR=value ...]: Circuits through the script (in the case's writes), its log in $OUT/log/CASE.log
run () {
	c=$1; s=$2; shift 2
	env SIM_WRITES="$OUT/w/$c" SIM_OVERLAY=$D/sd SIM_POS=8,34 "$@" SIM="$s" "$OUT/circuits" > "$OUT/log/$c.log" 2>&1 \
		|| bad "$c: Circuits ended with an error ($OUT/log/$c.log)"
	return 0
}
png () { [ -f "$OUT/$1.elsm" ] && python3 $D/shot.py "$OUT/$1.elsm" "$OUT/$1.png" > /dev/null || true; }
waits () { i=0; o=""; while [ $i -lt $1 ]; do o="$o;wait"; i=$((i + 1)); done; printf '%s' "${o#;}"; }
W3="wait;wait;wait"; PAUSE=$(waits 60)			# (60 turns: 1.2 s of the simulator's clock: the autosave's pause)
# a press, moves to, a release (a drag), from client (x0, y0) to (x1, y1)
drag () { printf 'down %s %s;move %s %s;move %s %s;move %s %s;up %s %s' $1 $2 $(( ($1 + $3) / 2 )) $(( ($2 + $4) / 2 )) $3 $4 $3 $4 $3 $4; }
click () { printf 'down %s %s;up %s %s' $1 $2 $1 $2; }
# kv CASE KEY: progress.ini's value of KEY (the first line that has it)
kv () { sed -n "s/^$2 = //p" "$OUT/w/$1/$PR" | head -1; }
has () { [ "$(kv "$1" "$2")" = "$3" ] || { printf "  (%s = '%s', not '%s')\n" "$2" "$(kv "$1" "$2")" "$3"; return 1; }; }
openhas () { kv "$1" open | tr ' ' '\n' | grep -qx "$2"; }
# probe CASE: the board's top on the level the case's writes start on (the card's height depends on the level's
# text), from a copy of them
probe () { rm -rf "$OUT/w/probe"; cp -r "$OUT/w/$1" "$OUT/w/probe"; run probe "$W3;dump $OUT/probe.elsm;exit"; geo by "$OUT/probe.elsm"; }
# the points of a case's board: P X Y (cells, decimals) -> "x y" client; MSG: inside the message bar (its face)
P () { geo cell $BY "$1" "$2"; }
MSG="722 600"
# a few progress files written here (the fixtures of tools/tests/desktop_sim/circuits/ for the bigger ones)
wire_seen () { printf 'open = wire\n\n[Player]\nseen.wire = 1\n' > "$OUT/w/$1/$PR"; }
not_level () { printf 'open = wire not\npack = 1-gates.circuits\nlevel = not\n\n[Player]\nwire = 3\nseen.wire = 1\nseen.not = 1\n' > "$OUT/w/$1/$PR"; }
lamp_on () { geo near "$OUT/$1.elsm" $2 $3 FFD23F 30; }
lamp_off () { geo near "$OUT/$1.elsm" $2 $3 5B5648 30; }
AND1='part g1 AND 16 13'					# (an AND at 16 13: its pins (16, 14), (16, 16), its output (21, 15))
ANDW="$AND1\\nwire A g1.1\\nwire B g1.2\\nwire g1 Out"

# ---- AC 18: the first start, then the stars of a fixture -------------------------------------------------------
seed fresh; BY=$(probe fresh)
run fresh "$W3;dump $OUT/fresh1.elsm;key 13;$W3;dump $OUT/fresh2.elsm;exit" && png fresh1 && png fresh2
BH=$((552 - BY)); LH=$((BH - 16 < 330 ? BH - 16 : 330)); LT=$((BY + (BH - LH) / 2))
check "AC18 no progress.ini: the 'wire' lesson card over the board (its purple header)" geo purple "$OUT/fresh1.elsm" 488 $((LT + 20))
check "AC18 ... Enter closes it" sh -c "! python3 '$OUT/geo.py' purple '$OUT/fresh2.elsm' 488 $((LT + 20))"
check "AC18 ... the lesson remembered (seen.wire = 1)" has fresh seen.wire 1
check "AC18 ... only 1.1 open (open = wire)" has fresh open wire
check "AC18 ... 1.1's row has its stars, 1.2's none (locked)" sh -c "[ \$(python3 '$OUT/geo.py' marks '$OUT/fresh2.elsm' 56) -gt 1 ] && [ \$(python3 '$OUT/geo.py' marks '$OUT/fresh2.elsm' 84) = 1 ]"
check "AC18 ... and 2.1's none (world 2 locked)" test "$(geo marks "$OUT/fresh2.elsm" $((56 + 30 + 8 * 28)))" = 1
seed stars stars; run stars "$W3;dump $OUT/stars.elsm;exit" && png stars
check "AC18 fixture stars.ini: 1.1 three gold stars" test "$(geo stars "$OUT/stars.elsm" 56)" = 3
check "AC18 ... 1.2 two" test "$(geo stars "$OUT/stars.elsm" 84)" = 2
check "AC18 ... 1.3 one" test "$(geo stars "$OUT/stars.elsm" 112)" = 1
check "AC18 ... 1.4 none won, open (its outlined stars)" sh -c "[ \$(python3 '$OUT/geo.py' stars '$OUT/stars.elsm' 140) = 0 ] && [ \$(python3 '$OUT/geo.py' marks '$OUT/stars.elsm' 140) -gt 1 ]"
check "AC18 ... 1.5 locked (no stars)" test "$(geo marks "$OUT/stars.elsm" 168)" = 1

# ---- AC 19: live -- the switches clicked, the lamp and the wires follow; a click on a row of the table ------------
seed live and; board live "$ANDW"; BY=$(probe live)
LAMP="$(P 35.95 15.25)"; WIRE="$(P 28 15)"; KA="$(P 3.5 7)"; KB="$(P 3.5 22)"
run live "$W3;dump $OUT/live0.elsm;$(click $KA);wait;$(click $KB);$W3;dump $OUT/live1.elsm;exit" && png live0 && png live1
check "AC19 A = 0, B = 0: the lamp dark" lamp_off live0 $LAMP
check "AC19 ... the AND's output wire dark" geo near "$OUT/live0.elsm" $WIRE 334A5E 30
check "AC19 A and B clicked: the lamp lit" lamp_on live1 $LAMP
check "AC19 ... the wire lit (green)" geo green "$OUT/live1.elsm" $WIRE
check "AC19 ... switch A's key green" geo green "$OUT/live1.elsm" $(P 3 6.6)
seed row and; board row "$ANDW"
run row "$W3;$(click 871 $((34 + 46 + 3 * 21 + 10)));$W3;dump $OUT/row.elsm;exit" && png row
check "G3   a click on the table's row 1 1: the lamp lit" lamp_on row $LAMP
check "G3   ... the switches set on it (A and B green)" sh -c "python3 '$OUT/geo.py' green '$OUT/row.elsm' $(P 3 6.6) && python3 '$OUT/geo.py' green '$OUT/row.elsm' $(P 3 21.6)"

# ---- AC 20: a pack given as the argument; a malformed one; one dropped ------------------------------------------
seed arg; run arg "$W3;dump $OUT/arg.elsm;exit" SIM_ARGS=SD:/docs/circuits/extra.circuits && png arg
check "AC20 circuits <pack>: its first level shown (level = x-both)" has arg level x-both
check "AC20 ... the pack remembered (pack = extra.circuits)" has arg pack extra.circuits
seed arg2; run arg2 "$W3;key 0x0E;$W3;exit" SIM_ARGS=SD:/docs/circuits/extra.circuits
check "AC20 ... its levels open: Ctrl+N goes to its second (x-nor3)" has arg2 level x-nor3
seed bad; run bad "$W3;dump $OUT/bad.elsm;key 13;$W3;dump $OUT/bad2.elsm;exit" SIM_ARGS=SD:/docs/circuits/bad.circuits && png bad && png bad2
check "AC20 a malformed pack: nothing added, the first level shown (level = wire)" has bad level wire
seed drop and; run drop "$W3;drop 500 300 SD:/docs/circuits/extra.circuits;$W3;exit"
check "G4   a .circuits dropped on the window: opened (level = x-both)" has drop level x-both

# ---- AC 21: a level won -- the stars, the board, the next level open; Enter on the result card --------------------
seed win; wire_seen win; BY=$(probe win)
WIN="$W3;$(drag $(P 5 15) $(P 34 15));wait;key 0x114;$W3"
run win "$WIN;dump $OUT/win.elsm;exit" && png win
RH=$((BH - 16 < 250 ? BH - 16 : 250)); BH=$((552 - BY)); RT=$((BY + (BH - RH) / 2))
check "AC21 wire won: wire = 3" has win wire 3
check "AC21 ... wire.circuit = wire A Out" has win wire.circuit "wire A Out"
check "AC21 ... the next level open (open holds not)" openhas win not
check "AC21 ... the result card (its green header)" geo near "$OUT/win.elsm" 488 $((RT + 20)) 4C9F55 30
seed win2; wire_seen win2; run win2 "$WIN;key 13;$W3;exit"
check "AC21 ... Enter on it: the next level (level = not)" has win2 level not

# ---- AC 16 in the app: Copy Truth Table, Copy Circuit (half adder, checked) ---------------------------------------
HALF='part g1 AND 16 4\nwire A g1.1\nwire B g1.2\nwire g1 C\npart g2 XOR 16 19\nwire A g2.1\nwire B g2.2\nwire g2 S'
seed half solving; sed -i 's/^level = full/level = half/' "$OUT/w/half/$PR"; board half "$HALF"
run half "$W3;key 0x114;$W3;key 27;menu 8;$W3;exit" SIM_CLIPFILE="$OUT/w/half.clip"
printf "A\tB\tC\tS\tC'\tS'\n0\t0\t0\t0\t0\t0\n0\t1\t0\t1\t0\t1\n1\t0\t0\t1\t0\t1\n1\t1\t1\t0\t1\t0\n" > "$OUT/w/half.want"
check "AC16 Copy Truth Table after a Check: the table with the obtained columns, tab-separated" cmp -s "$OUT/w/half.clip" "$OUT/w/half.want"
seed half2 solving; sed -i 's/^level = full/level = half/' "$OUT/w/half2/$PR"; board half2 "$HALF"
run half2 "$W3;menu 9;$W3;exit" SIM_CLIPFILE="$OUT/w/half2.clip"
printf 'part g1 AND 16 4\npart g2 XOR 16 19\nwire A g1.1\nwire B g1.2\nwire A g2.1\nwire B g2.2\nwire g1 C\nwire g2 S' > "$OUT/w/half2.want"
check "AC16 Copy Circuit: the board as circuit text" cmp -s "$OUT/w/half2.clip" "$OUT/w/half2.want"

# ---- G2: placing (a drag from the palette, the keys 1-6), wiring, undo; a loop refused; moves; a wire picked up -----
seed place and; BY=$(probe place)
ANDBT="$((234 + 58 + 41 + 20)) $((BY - 29))"			# (the palette: Select, then NOT, AND -- the level's two gates)
BUILD="$W3;$(drag $ANDBT $(P 18.5 15));wait;$(drag $(P 5 7) $(P 16 14));wait;$(drag $(P 5 22) $(P 16 16));wait;$(drag $(P 21 15) $(P 34 15))"
run place "$BUILD;$PAUSE;dump $OUT/place.elsm;exit" && png place
check "G2   AND dragged from the palette, three wires drawn: and.circuit after the pause" has place and.circuit "$ANDW"
seed undo and; run undo "$BUILD;wait;key 0x1A;$PAUSE;exit"
check "G2   ... Ctrl+Z: the last wire undone" has undo and.circuit "$AND1\\nwire A g1.1\\nwire B g1.2"
seed redo and; run redo "$BUILD;wait;key 0x1A;wait;key 0x19;$PAUSE;exit"
check "G2   ... Ctrl+Y: done again" has redo and.circuit "$ANDW"
seed keys and; run keys "$W3;key 2;move $(P 18.5 15);$(click $(P 18.5 15));wait;key 1;move $(P 18.5 24);$(click $(P 18.5 24));$PAUSE;dump $OUT/keys.elsm;exit" && png keys
check "G2   keys 2 then 1: an AND, then a NOT placed (click then click)" has keys and.circuit "$AND1\\npart g2 NOT 16 22"
seed quit and; run quit "$W3;key 2;move $(P 18.5 15);$(click $(P 18.5 15));quit;wait"
check "G2   the window closed right after a change: the board saved" has quit and.circuit "$AND1"
seed arrows and; board arrows "$AND1"
run arrows "$W3;$(click $(P 18.5 15));wait;key 0x103;key 0x103;key 0x101;$PAUSE;exit"
check "G2   a gate clicked, then the arrows (right, right, down): moved to 18 14" has arrows and.circuit "part g1 AND 18 14"
seed mv and; board mv "$AND1"
run mv "$W3;$(drag $(P 18.5 15) $(P 22.5 19));$PAUSE;exit"
check "G2   a gate dragged by its body: moved (20 17)" has mv and.circuit "part g1 AND 20 17"
seed pick and; board pick "$AND1\\nwire A g1.1"
run pick "$W3;$(drag $(P 16 14) $(P 16 16));$PAUSE;dump $OUT/pick.elsm;exit" && png pick
check "G2   a fed input's wire picked up and plugged into the other input" has pick and.circuit "$AND1\\nwire A g1.2"
seed pick2 and; board pick2 "$AND1\\nwire A g1.1"
run pick2 "$W3;$(drag $(P 16 14) $(P 26 26));$PAUSE;exit"
check "G2   ... dropped on nothing: the wire removed" has pick2 and.circuit "$AND1"
seed esc and; board esc "$AND1"
run esc "$W3;down $(P 5 7);move $(P 10 10);move $(P 16 14);key 27;up $(P 16 14);$PAUSE;exit"
check "G2   Esc during a wire's drag: nothing made" has esc and.circuit "$AND1"
seed rdel and; board rdel "$AND1\\nwire A g1.1"
run rdel "$W3;rdown $(P 18.5 15);rup $(P 18.5 15);$PAUSE;exit"
check "G2   a right click on a gate: removed with its wire" has rdel and.circuit ""
seed rdel2 and; board rdel2 "$AND1\\nwire A g1.1"
run rdel2 "$W3;rdown $(P 18.5 15);rup $(P 18.5 15);wait;key 0x1A;$PAUSE;exit"
check "G2   ... Ctrl+Z brings it back" has rdel2 and.circuit "$AND1\\nwire A g1.1"
seed del and; board del "$AND1\\nwire A g1.1\\nwire B g1.2"
run del "$W3;$(click $(P 8 7));wait;key 0x108;$PAUSE;exit"
check "G2   a wire clicked, then Delete: removed" has del and.circuit "$AND1\\nwire B g1.2"
seed clr and; board clr "$AND1\\nwire A g1.1"
run clr "$W3;menu 7;$PAUSE;exit"
check "G2   Edit > Clear Board: the board empty" has clr and.circuit ""
seed loop; not_level loop; board loop 'part g1 NOT 16 13'; BY=$(probe loop)
run loop "$W3;$(drag $(P 21 15) $(P 16 15));$W3;dump $OUT/loop.elsm;$PAUSE;exit" && png loop
check "G2   a loop (the NOT's output into its own input): refused, the message in red" geo near "$OUT/loop.elsm" $MSG FBE0DC 10
check "G2   ... the board unchanged" has loop not.circuit "part g1 NOT 16 13"

# ---- G3: a failed Check, the step mode --------------------------------------------------------------------------
seed chk check; BY=$(probe chk)
run chk "$W3;key 0x114;$W3;dump $OUT/chk.elsm;exit" && png chk
check "G3   check.ini, F5: row 1 1 tinted red in the table" geo near "$OUT/chk.elsm" 777 $((34 + 46 + 3 * 21 + 5)) FBE0DC 8
check "G3   ... the switches set on it (A and B green)" sh -c "python3 '$OUT/geo.py' green '$OUT/chk.elsm' $(P 3 6.6) && python3 '$OUT/geo.py' green '$OUT/chk.elsm' $(P 3 21.6)"
check "G3   ... the message in red" geo near "$OUT/chk.elsm" $MSG FBE0DC 10
check "G3   ... nothing recorded (no xor stars)" test -z "$(kv chk xor)"
seed stp solving; BY=$(probe stp)
run stp "$W3;key 0x117;key 0x117;$W3;dump $OUT/stp.elsm;key 0x116;$W3;dump $OUT/stp2.elsm;exit" && png stp && png stp2
check "G3   solving.ini, F8 twice: the OR (depth 3) not computed -- its face grey" geo near "$OUT/stp.elsm" $(P 26.5 6.2) F0F2F4 6
check "G3   ... the AND of depth 1 computed (white: 0)" geo near "$OUT/stp.elsm" $(P 10.5 3.2) FFFFFF 6
check "G3   ... F7: live again (the OR white)" geo near "$OUT/stp2.elsm" $(P 26.5 6.2) FFFFFF 6

# ---- G4: a locked level clicked, the lesson again, the hint -----------------------------------------------------
seed lk stars; run lk "$W3;$(click 100 168);$W3;dump $OUT/lk.elsm;exit" && png lk
check "G4   a locked row clicked: the level stays (or), the message only" sh -c "grep -q '^level = or\$' '$OUT/w/lk/$PR' && python3 '$OUT/geo.py' near '$OUT/lk.elsm' $MSG E4ECF7 10"
seed lk2 stars; run lk2 "$W3;$(click 100 84);$W3;exit"
check "G4   an open row clicked: shown (level = not)" has lk2 level not
seed les and; BY=$(probe les); BH=$((552 - BY)); LH=$((BH - 16 < 330 ? BH - 16 : 330)); LT=$((BY + (BH - LH) / 2))
run les "$W3;key 0x110;$W3;dump $OUT/les1.elsm;key 27;$W3;dump $OUT/les2.elsm;exit" && png les1 && png les2
check "G4   F1: the lesson card (AND)" geo purple "$OUT/les1.elsm" 488 $((LT + 20))
check "G4   ... Esc closes it" sh -c "! python3 '$OUT/geo.py' purple '$OUT/les2.elsm' 488 $((LT + 20))"
seed hint and; run hint "$W3;key 0x111;$W3;dump $OUT/hint.elsm;exit" && png hint
check "G4   F2: the hint under the level's text (the card taller: the board lower)" test "$(geo by "$OUT/hint.elsm")" -gt "$BY"
seed nx and; run nx "$W3;key 0x0E;$W3;dump $OUT/nx.elsm;key 0x10;$W3;exit" && png nx
check "G4   Ctrl+N on a level not won: stays (the next one locked); Ctrl+P: the previous" has nx level not

# ---- the French window, the minimum size (dumps looked at) ---------------------------------------------------------
seed fr solving; french fr; run fr "$W3;dump $OUT/fr.elsm;exit" && png fr
check "FR   the French catalogue loaded (the window in French: looked at in $OUT/fr.png)" test -s "$OUT/fr.elsm"
seed min solving; run min "$W3;dump $OUT/min.elsm;exit" SIM_SCREEN=928x746 SIM_POS=0,30 && png min
check "MIN  at 920 x 600 (SIM_SCREEN): the window shrunk to its minimum" test "$(od -An -t d4 -j 4 -N 8 "$OUT/min.elsm" | tr -s ' ')" = " 928 632"
seed minfr check; french minfr; run minfr "$W3;key 0x114;$W3;dump $OUT/minfr.elsm;exit" SIM_SCREEN=928x746 SIM_POS=0,30 && png minfr

echo
if [ $FAILS -ne 0 ]; then echo "circuits-sim: $FAILS of $((PASS + FAILS)) checks FAILED"; exit 1; fi
echo "circuits-sim: all $PASS checks passed"
