#!/bin/sh
# run_pinball_sim_test.sh -- the scripted checks of Pinball's window (user/Apps/pinball, AutoDev round 4) in the desktop
# simulator: the app built for the PC against the stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp, UIKit,
# FreeType, AudioKit -- as shots.sh builds it; its own sources with -Wall -Wextra, no warning allowed), each case a run
# through a script of events (the held keys: "hold" / "release") with a fresh SIM_WRITES of its own, then asserted on
# what the app printed (SIM_LOG: "pinball: playing ...", "refused ...", "launch N", "overlay ...", "game over N"), on
# the scores.ini it wrote, and on its window's dumps (made PNGs in $OUT, looked at by hand). 02 §12 AC 30, 31 (+ the
# pause, the tap and the hold, the name entry in French).
#
#   sh tools/tests/run_pinball_sim_test.sh          -> "pinball-sim: all N checks passed" (exit 0)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_pinball_sim
CXX="g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
FT=third_party/freetype-2.14.3
rm -rf "$OUT/w" "$OUT/log"; mkdir -p "$OUT/obj" "$OUT/ft" "$OUT/ak" "$OUT/w" "$OUT/log"

# ---- the building (UIKit, FreeType and AudioKit kept between runs: UIKit rebuilt when a source is newer) ----------
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
if [ ! -f "$OUT/libaudiokit.a" ]; then				# (the game's sounds: AudioKit's FM voices, as shots.sh)
	I="-Iuser -Iuser/Kits -Iuser/Runtime -Iuser/Include -Iuser/Libs -Iuser/Emulators -Iuser/Ports -Ithird_party"
	gcc -O2 -w $I -c user/Apps/media/codecs.c -o "$OUT/ak/codecs.o"
	gcc -O2 -w $I -c user/Apps/media/vorbis.c -o "$OUT/ak/vorbis.o"
	for f in user/Apps/koton/synth/*.cpp user/Kits/audiokit/*.cpp; do
		$CXX -Iuser/Apps/koton -Iuser/Apps/media -Ithird_party -c "$f" -o "$OUT/ak/$(basename "$f" .cpp).o" &
	done; wait
	ar rcs "$OUT/libaudiokit.a" "$OUT"/ak/*.o
fi
$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
# (the app's own sources without -w: no warning allowed; no fused multiply-add, as user/Makefile)
WFLAGS="-std=gnu++17 -O1 -Wall -Wextra -Wno-format-truncation -ffp-contract=off -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I kernel/include -fno-exceptions -fno-rtti -Iuser/Kits/fontkit -I$FT/include"
: > "$OUT/pinball.warn"
for f in user/Apps/pinball/*.cpp; do
	g++ $WFLAGS -c "$f" -o "$OUT/p_$(basename "$f" .cpp).o" 2>> "$OUT/pinball.warn" || { cat "$OUT/pinball.warn"; echo "pinball-sim: FAIL the build"; exit 1; }
done
if grep -q "Apps/pinball/.*warning" "$OUT/pinball.warn"; then cat "$OUT/pinball.warn"; echo "pinball-sim: FAIL warnings in Pinball"; exit 1; fi
g++ -o "$OUT/pinball" "$OUT/fakekapi.o" "$OUT"/p_*.o "$OUT/libuikit.a" "$OUT/libft.a" "$OUT/libaudiokit.a" -lpthread -lm
[ "$1" = build ] && { echo "pinball-sim: built $OUT/pinball"; exit 0; }

# ---- the running -------------------------------------------------------------------------------------------------
PASS=0; FAILS=0
ok ()   { PASS=$((PASS + 1)); echo "ok   $*"; }
bad ()  { FAILS=$((FAILS + 1)); echo "FAIL $*"; }
check () { name=$1; shift; if "$@"; then ok "$name"; else bad "$name"; fi; }
SC=apps/pinball.app/scores.ini
T=SD:/apps/pinball.app/tables
# seed CASE [fr]: a fresh writes folder (French: the system's language)
seed () {
	rm -rf "$OUT/w/$1"; mkdir -p "$OUT/w/$1/apps/pinball.app"
	if [ "$2" = fr ]; then mkdir -p "$OUT/w/$1/etc"; { grep -v '^language' sdcard/etc/system.ini; echo "language=fr"; } > "$OUT/w/$1/etc/system.ini"; fi
	return 0
}
# run CASE "SCRIPT" [VAR=value ...]: Pinball through the script (in the case's writes), its log in $OUT/log/CASE.log
run () {
	c=$1; s=$2; shift 2
	: > "$OUT/log/$c.log"
	env SIM_WRITES="$OUT/w/$c" SIM_OVERLAY=$D/sd SIM_POS=60,30 SIM_SCREEN=1280x900 SIM_LOG="$OUT/log/$c.log" "$@" SIM="$s" "$OUT/pinball" >> "$OUT/log/$c.log" 2>&1 \
		|| bad "$c: Pinball ended with an error ($OUT/log/$c.log)"
	return 0
}
png () { [ -f "$OUT/$1.elsm" ] && python3 $D/shot.py "$OUT/$1.elsm" "$OUT/$1.png" > /dev/null || true; }
waits () { i=0; o=""; while [ $i -lt $1 ]; do o="$o;wait"; i=$((i + 1)); done; printf '%s' "${o#;}"; }
logs () { grep -qF "$2" "$OUT/log/$1.log" || { echo "  (no '$2' in $OUT/log/$1.log)"; return 1; }; }
nolog () { ! grep -qF "$2" "$OUT/log/$1.log"; }
launch () { sed -n 's/^pinball: launch //p' "$OUT/log/$1.log" | head -1; }
kv () { sed -n "/^\[$2\]/,/^\[/s/^$3 = //p" "$OUT/w/$1/$SC" | head -1; }
W3="wait;wait;wait"

# ---- AC 30: a broken table as the argument -> the picker, that table chosen, its error ------------------------------
seed broken; run broken "$W3;dump $OUT/broken.elsm;exit" SIM_ARGS=SD:/docs/pinball/broken.table && png broken
check "AC30 pinball broken.table: refused with its line and reason" logs broken "pinball: refused broken.table: line 12: unknown block [bumber]"
check "AC30 ... no game started (the picker)" nolog broken "pinball: playing"
check "AC30 ... its window dumped ($OUT/broken.png)" test -s "$OUT/broken.elsm"
seed brokenfr fr; run brokenfr "$W3;dump $OUT/brokenfr.elsm;exit" SIM_ARGS=SD:/docs/pinball/broken.table && png brokenfr

# ---- AC 30: a good table as the argument -> played at once ---------------------------------------------------------
seed first; run first "$W3;dump $OUT/first.elsm;exit" SIM_ARGS=SD:/docs/pinball/my-first-table.table && png first
check "AC30 pinball my-first-table.table: played at once" logs first "pinball: playing My First Table"
check "     ... the last table played remembered ([settings] table = user.my-first-table)" test "$(kv first settings table)" = user.my-first-table
seed noarg; run noarg "$W3;exit"
check "     no argument: the picker with the 3 shipped tables (and the player's 2 of the overlay -- not listed: not in SIM_WRITES)" logs noarg "pinball: picker (3 tables)"

# ---- the plunger: a tap launches at 'auto', a hold of ~0.7 s at about 0.7 of 'max' (binding note 12) ------------------
seed tap; run tap "$W3;key 32;$(waits 10);exit" "SIM_ARGS=--seed 3 SD:/docs/pinball/quick.table"
check "     a tap of Space: launched at auto (2300)" test "$(launch tap)" = 2300
seed hold; run hold "$W3;hold 32;$(waits 35);release 32;$(waits 10);exit" "SIM_ARGS=--seed 3 SD:/docs/pinball/quick.table"
L=$(launch hold)
check "     Space held 35 steps then released: a pulled launch, not a tap ($L: 1400..2300)" sh -c "[ -n '$L' ] && [ '$L' -gt 1400 ] && [ '$L' -lt 2300 ]"

# ---- the pause: P shows the card (the world frozen), P again resumes; Esc shows it too -------------------------------
seed pause; run pause "$W3;key 32;$(waits 20);key p;$W3;dump $OUT/pause.elsm;key p;$W3;key 27;$W3;dump $OUT/pause2.elsm;exit" "SIM_ARGS=--seed 3 $T/2-haunted-manor.table" && png pause && png pause2
check "     P: the pause card" logs pause "pinball: overlay pause"
check "     ... P again: resumed" logs pause "pinball: overlay none"
check "     ... Esc: the pause card again" test "$(grep -c 'overlay pause' "$OUT/log/pause.log")" = 2
seed back; run back "$W3;key 27;$W3;key 0x101;key 13;$W3;exit" "SIM_ARGS=$T/1-space-station.table"
check "     Esc, then Back to the tables (down, Enter): the picker" logs back "pinball: picker"

# ---- AC 31: a whole game on quick.table (1 ball, no ball save): the score enters the top 5, the name kept ------------
seed quick
run quick "$W3;key 32;waitlog 3000 pinball: game over;$(waits 10);dump $OUT/name.elsm;key 13;$W3;dump $OUT/scores.elsm;key 13;$W3;exit" "SIM_ARGS=--seed 3 SD:/docs/pinball/quick.table"
png name; png scores
S=$(sed -n 's/^pinball: game over //p' "$OUT/log/quick.log" | head -1)
check "AC31 quick.table: the game over (score $S > 0)" sh -c "[ -n '$S' ] && [ '$S' -gt 0 ]"
check "AC31 ... the name entry shown" logs quick "pinball: overlay name"
check "AC31 ... Enter: scores.ini written" test -f "$OUT/w/quick/$SC"
check "AC31 ... [user.quick] 1 = $S Player" test "$(kv quick user.quick 1)" = "$S Player"
check "AC31 ... [settings] name = Player, table = user.quick" sh -c "[ '$(kv quick settings name)' = Player ] && [ '$(kv quick settings table)' = user.quick ]"
check "AC31 ... the top 5 shown, then Enter: the picker" sh -c "grep -q 'overlay scores' '$OUT/log/quick.log' && grep -q 'pinball: picker' '$OUT/log/quick.log'"

# a second game, a name typed (the field emptied first), in French: the default name is "Joueur"
seed quickfr fr
run quickfr "$W3;key 32;waitlog 3000 pinball: game over;$(waits 10);dump $OUT/namefr.elsm;key 0x105;$(waits 1);key 0x08;key 0x08;key 0x08;key 0x08;key 0x08;key 0x08;key A;key n;key a;key 13;$W3;dump $OUT/scoresfr.elsm;exit" "SIM_ARGS=--seed 3 SD:/docs/pinball/quick.table"
png namefr; png scoresfr
S=$(sed -n 's/^pinball: game over //p' "$OUT/log/quickfr.log" | head -1)
check "AC31 (fr) the name field held 'Joueur', emptied, 'Ana' typed: 1 = $S Ana" test "$(kv quickfr user.quick 1)" = "$S Ana"

echo
if [ $FAILS -ne 0 ]; then echo "pinball-sim: $FAILS of $((PASS + FAILS)) checks FAILED"; exit 1; fi
echo "pinball-sim: all $PASS checks passed"
