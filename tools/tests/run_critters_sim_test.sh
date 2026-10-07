#!/bin/sh
# run_critters_sim_test.sh -- the scripted checks of Critters' window (user/Apps/critters, AutoDev round 5) in the desktop
# simulator: the app built for the PC against the stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp, UIKit,
# FreeType, AudioKit -- as shots.sh builds it; its own sources with -Wall -Wextra, no warning allowed), each case a run
# through a script of events with a fresh SIM_WRITES of its own, then asserted on what the app printed (SIM_LOG:
# "critters: playing ...", "refused ...", "role ... ok", "overlay ...", "end won ..."), on the progress.ini it wrote,
# and on its window's dumps (made PNGs in $OUT, looked at by hand). 02 §12 AC 26, 27, 28 (+ the keyboard's Tab / Enter,
# the pause and the cards, a restart, a locked level, a lost run writing nothing, the error in French).
# The fixtures: tools/tests/desktop_sim/sd/docs/critters (broken.level, my-first-level.level, quick.level + quick.sol),
# read through SIM_OVERLAY; the shipped levels from the card.
#
#   sh tools/tests/run_critters_sim_test.sh          -> "critters-sim: all N checks passed" (exit 0)
#   sh tools/tests/run_critters_sim_test.sh build    -> the app built only ($OUT/critters)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_critters_sim
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
# (the app's own sources without -w: no warning allowed)
WFLAGS="-std=gnu++17 -O1 -Wall -Wextra -Wno-format-truncation -ffp-contract=off -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I kernel/include -fno-exceptions -fno-rtti -Iuser/Kits/fontkit -I$FT/include"
: > "$OUT/critters.warn"
for f in user/Apps/critters/*.cpp; do
	g++ $WFLAGS -c "$f" -o "$OUT/c_$(basename "$f" .cpp).o" 2>> "$OUT/critters.warn" || { cat "$OUT/critters.warn"; echo "critters-sim: FAIL the build"; exit 1; }
done
if grep -q "Apps/critters/.*warning" "$OUT/critters.warn"; then cat "$OUT/critters.warn"; echo "critters-sim: FAIL warnings in Critters"; exit 1; fi
g++ -o "$OUT/critters" "$OUT/fakekapi.o" "$OUT"/c_*.o "$OUT/libuikit.a" "$OUT/libft.a" "$OUT/libaudiokit.a" -lpthread -lm
[ "$1" = build ] && { echo "critters-sim: built $OUT/critters"; exit 0; }

# ---- the running -------------------------------------------------------------------------------------------------
PASS=0; FAILS=0
ok ()   { PASS=$((PASS + 1)); echo "ok   $*"; }
bad ()  { FAILS=$((FAILS + 1)); echo "FAIL $*"; }
check () { name=$1; shift; if "$@"; then ok "$name"; else bad "$name"; fi; }
PR=apps/critters.app/progress.ini
L=SD:/apps/critters.app/levels
U=SD:/docs/critters
# seed CASE [fr]: a fresh writes folder (French: the system's language)
seed () {
	rm -rf "$OUT/w/$1"; mkdir -p "$OUT/w/$1/apps/critters.app"
	if [ "$2" = fr ]; then mkdir -p "$OUT/w/$1/etc"; { grep -v '^language' sdcard/etc/system.ini; echo "language=fr"; } > "$OUT/w/$1/etc/system.ini"; fi
	return 0
}
# run CASE "SCRIPT" [VAR=value ...]: Critters through the script (in the case's writes), its log in $OUT/log/CASE.log
run () {
	c=$1; s=$2; shift 2
	: > "$OUT/log/$c.log"
	env SIM_WRITES="$OUT/w/$c" SIM_OVERLAY=$D/sd SIM_POS=60,30 SIM_LOG="$OUT/log/$c.log" "$@" SIM="$s" "$OUT/critters" >> "$OUT/log/$c.log" 2>&1 \
		|| bad "$c: Critters ended with an error ($OUT/log/$c.log)"
	return 0
}
png () { [ -f "$OUT/$1.elsm" ] && python3 $D/shot.py "$OUT/$1.elsm" "$OUT/$1.png" > /dev/null || true; }
logs () { grep -qF "$2" "$OUT/log/$1.log" || { echo "  (no '$2' in $OUT/log/$1.log)"; return 1; }; }
nolog () { ! grep -qF "$2" "$OUT/log/$1.log"; }
count () { grep -cF "$2" "$OUT/log/$1.log"; }
kv () { [ -f "$OUT/w/$1/$PR" ] && sed -n "/^\[$2\]/,/^\[/s/^$3 = //p" "$OUT/w/$1/$PR" | head -1; }
W3="wait;wait;wait"

# ---- AC-26: a broken level as the argument -> refused with its line and reason, the picker (that row, its error) ----
seed broken; run broken "$W3;dump $OUT/broken.elsm;exit" SIM_ARGS=$U/broken.level && png broken
check "AC26 critters broken.level: refused with its line and reason" logs broken "critters: refused broken.level: line 24: unknown block [shap]"
check "AC26 ... no level started" nolog broken "critters: playing"
check "AC26 ... the picker shown (its window dumped: $OUT/broken.png)" sh -c "grep -q 'critters: picker' '$OUT/log/broken.log' && test -s '$OUT/broken.elsm'"
seed brokenfr fr; run brokenfr "$W3;dump $OUT/brokenfr.elsm;exit" SIM_ARGS=$U/broken.level && png brokenfr
check "AC26 (fr) the same, its reason in French in the window ($OUT/brokenfr.png, by eye)" logs brokenfr "critters: refused broken.level: line 24"

# ---- AC-26: a good level as the argument -> played at once, its start card ------------------------------------------
seed first; run first "$W3;dump $OUT/first.elsm;exit" SIM_ARGS=$U/my-first-level.level && png first
check "AC26 critters my-first-level.level: played at once" logs first "critters: playing My First Level"
check "     ... its start card" logs first "critters: overlay card"
check "     ... the last level played remembered ([settings] last = user.my-first-level)" test "$(kv first settings last)" = user.my-first-level
seed noarg; run noarg "$W3;exit"
check "     no argument: the picker with the 12 shipped levels" logs noarg "critters: picker (12 levels)"

# ---- AC-27: a whole level replayed to its end (--until end) -> won, progress.ini written ----------------------------
seed quick; run quick "$W3;dump $OUT/quickend.elsm;exit" "SIM_ARGS=$U/quick.level --replay $U/quick.sol --until end" && png quickend
check "AC27 quick.level --replay quick.sol --until end: won 5 of 1 needed" logs quick "critters: end won 5/1 9s"
check "AC27 ... the end card" logs quick "critters: overlay end"
check "AC27 ... progress.ini written" logs quick "critters: progress written"
check "AC27 ... [user.quick] solved = 1, saved = 5, time = 9" sh -c "[ '$(kv quick user.quick solved)' = 1 ] && [ '$(kv quick user.quick saved)' = 5 ] && [ '$(kv quick user.quick time)' = 9 ]"
check "AC27 ... [settings] last = user.quick" test "$(kv quick settings last)" = user.quick
# the same level played live on the clock: the card dismissed, the end awaited
seed live; run live "$W3;key 13;waitlog 3000 critters: end;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;dump $OUT/live.elsm;exit" SIM_ARGS=$U/quick.level && png live
check "     quick.level played live (Enter on the card, the clock running): won" logs live "critters: end won 5/1 "
check "     ... the end card after half a second" logs live "critters: overlay end"
# a lost run writes no result (AC-23): Mind the Gap with nothing done
seed lost; run lost "$W3;exit" "SIM_ARGS=$L/training-02-mind-the-gap.level --replay $U/quick.sol --until end"
check "     Mind the Gap with nothing done: lost" logs lost "critters: end lost 0/8"
check "     ... and no result written for it" sh -c "! grep -q 'training-02-mind-the-gap\]' '$OUT/w/lost/$PR'"

# ---- AC-28: the Digger slot clicked, then creature 0 on Straight Down's floor (step 61: x 92, y 79 -> 185, 150) ------
seed click; run click "$W3;key p;down 206 396;up 206 396;down 185 150;up 185 150;wait;dump $OUT/click.elsm;exit" "SIM_ARGS=$L/training-01-straight-down.level --until 61" && png click
check "AC28 Digger chosen, creature 0 clicked: it digs" logs click "critters: role digger c0 ok (2 left)"
# the keyboard: 5 (Digger), Tab (the leftmost creature in view: c0), Enter
seed keys; run keys "$W3;key p;key 5;key 0x09;key 13;wait;exit" "SIM_ARGS=$L/training-01-straight-down.level --until 61"
check "     the keyboard: 5, Tab, Enter -> creature 0 digs" logs keys "critters: role digger c0 ok (2 left)"
# a refusal: a climber on Straight Down (none given) -> a blip, nothing logged as given
seed refuse; run refuse "$W3;key p;key 1;key 0x09;key 13;wait;exit" "SIM_ARGS=$L/training-01-straight-down.level --until 61"
check "     1 (Climber: none given): not chosen, nothing given" nolog refuse "critters: role climber"

# ---- the pause, the cards, a restart, a locked level --------------------------------------------------------------
seed cards; run cards "$W3;key p;wait;key p;wait;key 27;wait;key 27;wait;key n;wait;key 27;wait;key n;wait;key 13;wait;wait;dump $OUT/nuking.elsm;key 18;wait;exit" "SIM_ARGS=$L/training-01-straight-down.level --until 100" && png nuking
check "     P: the pause banner, P again: resumed" sh -c "[ \$(grep -c 'overlay pause' '$OUT/log/cards.log') -ge 2 ] && grep -q 'overlay none' '$OUT/log/cards.log'"
check "     Esc: the Paused card; Esc again: resumed" logs cards "critters: overlay menu"
check "     N: the All explode card; Esc: cancelled; N, Enter: confirmed" test "$(count cards 'overlay nuke')" = 2
check "     Ctrl+R: the level restarted (its start card again)" sh -c "[ \$(grep -c 'critters: playing Straight Down' '$OUT/log/cards.log') = 2 ] && grep -q 'overlay card' '$OUT/log/cards.log'"
seed locked; run locked "$W3;key 0x101;key 0x101;key 0x101;key 0x101;key 13;wait;dump $OUT/locked.elsm;exit" && png locked
check "     a locked level chosen (Soft Landing), Enter: not played" nolog locked "critters: playing"

echo
if [ $FAILS -ne 0 ]; then echo "critters-sim: $FAILS of $((PASS + FAILS)) checks FAILED"; exit 1; fi
echo "critters-sim: all $PASS checks passed"
