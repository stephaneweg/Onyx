#!/bin/sh
# tools/manuals/koton_shots.sh -- the Koton manual's pictures (sdcard/manuals/koton/images/*.png), taken from
# the REAL app run on the PC against the stand-in kernel (as tools/tests/desktop_sim/shots.sh does): Koton
# built for the host, maximised on a 1920 x 1080 screen, driven by a script of events over its demo song
# (SD:/koton/songs/demo.kson), its window dumped, then cut to the part a section talks about and, for some,
# marked with numbered callouts (annotate.py). The PC has no sound output: the status bar says so.
#
#   sh tools/manuals/koton_shots.sh [name ...]		(default: all of them)
#
# Needs g++, python3 with Pillow + numpy. What the app writes goes to a scratch folder, never to sdcard/.
set -e
cd "$(dirname "$0")/../.."
D=tools/tests/desktop_sim
M=tools/manuals
OUT=${SHOTS_TMP:-/tmp/onyx_koton_manual}
IMG=sdcard/manuals/koton/images
WANT=" $* "
rm -rf "$OUT/writes"; mkdir -p "$OUT/obj" "$OUT/writes" "$OUT/ft" "$OUT/k" "$IMG"
: > "$OUT/log.txt"
export SIM_WRITES="$OUT/writes"
CXX="g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
want () { [ "$WANT" = "  " ] || case "$WANT" in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

# ---- the building: uikit, the stand-in kernel, FreeType, Koton (its engine, MeltySynth, its plugin host) ----
for f in user/Kits/uikit/*.cpp; do $CXX -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
rm -f "$OUT/libuikit.a"; ar rcs "$OUT/libuikit.a" "$OUT"/obj/*.o
$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
FT=third_party/freetype-2.14.3
FT_SRC="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c"
for f in $FT_SRC; do gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
	-Iuser/Kits/fontkit -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
rm -f "$OUT/libft.a"; ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
K=user/Apps/koton
for f in $K/engine/*.cpp $K/synth/*.cpp $K/plug/*.cpp; do $CXX -I$K -c "$f" -o "$OUT/k/$(basename "$f" .cpp).o" & done; wait
$CXX -I$K -Iuser/Kits/fontkit -I$FT/include -o "$OUT/koton" "$OUT/fakekapi.o" $K/main.cpp "$OUT"/k/*.o "$OUT/libuikit.a" "$OUT/libft.a"

# ---- the running --------------------------------------------------------------------------------------------
# raw NAME "SCRIPT": Koton through the script, its window -> OUT/NAME.png (the whole window)
raw () {
	name=$1; script=$2
	if ! env SIM_SCREEN=1920x1080 SIM_ARGS=SD:/koton/songs/demo.kson SIM="wait;wait;$script;wait;wait;wait;dump $OUT/$name.elsm;exit" \
		"$OUT/koton" >>"$OUT/log.txt" 2>&1
	then echo "koton_shots: $name failed (see $OUT/log.txt)"; exit 1; fi
	python3 $D/shot.py "$OUT/$name.elsm" "$OUT/$name.png" >/dev/null
}
# cut NAME x0,y0,x1,y1 [FROM]: OUT/FROM.png's part (FROM: NAME by default) -> IMG/NAME.png
cut () { python3 -c "from PIL import Image; Image.open ('$OUT/${3:-$1}.png').crop (tuple (int (v) for v in '$2'.split (','))).save ('$IMG/$1.png')"; echo "  $IMG/$1.png"; }
# mark NAME [--crop ...] N:x,y ...: OUT/NAME.png with callouts -> IMG/NAME.png
mark () { n=$1; shift; python3 $M/annotate.py "$OUT/$n.png" "$IMG/$n.png" "$@" >/dev/null; echo "  $IMG/$n.png"; }
c () { printf "down %d %d;up %d %d" $1 $2 $1 $2; }			# a click (the window's client coordinates)
rc () { printf "rdown %d %d;rup %d %d" $1 $2 $1 $2; }			# a right click
ARR="$(c 338 512)"					# the first chord (F#m9) selected

# the window, its parts numbered (the manual's section 3)
if want window; then
	raw window "$ARR"
	mark window 1:100,57:t 2:440,57:t 3:760,57:t 4:1190,57:t 5:1400,45:t 6:1600,57:t 7:1790,50:t 8:700,95:l 9:280,122:l \
		10:40,190:l 11:330,212:l 12:340,560:l 13:500,700:l 14:1480,700:b 15:1780,300:r 16:400,948:b
	cut window-chain 1355,610,1655,940 window
fi
if want header; then raw header ""; mark header --crop 0,150,258,230 1:40,167:t 2:204,167:t 3:230,167:t 4:100,192:l 5:80,216:b 6:226,202:r; fi
if want transport; then raw transport ""; cut transport 0,28,1920,86; fi
if want browser; then raw browser ""; cut browser 1655,86,1920,940; fi
if want lanemenu; then raw lanemenu "$(rc 1400 312)"; cut lanemenu 1150,280,1700,500; fi
if want headmenu; then raw headmenu "$(rc 100 150)"; cut headmenu 0,140,560,470; fi
if want sound; then raw sound "$(c 100 164)"; cut sound 640,280,1280,705; fi
if want song; then raw song "$(c 600 29)"; cut song 720,285,1200,700; fi
if want cadence; then raw cadence "$ARR;wait;$(c 1040 787)"; cut cadence 720,365,1200,615; fi
if want chord; then
	raw chord "$ARR"
	mark chord --crop 0,600,1360,940 1:300,691:l 2:300,751:l 3:590,667:t 4:680,720:t 5:960,880:b 6:950,815:t 7:1095,815:t 8:1330,627:t
fi
if want accomp; then raw accomp "$(c 400 237)"; cut accomp 0,600,1360,940; cut accomp-chain 1355,610,1655,940 accomp; fi
if want accgrid; then raw accgrid "$(c 400 237);wait;$(c 750 730)"; cut accgrid 0,600,1360,940; fi
if want melcell; then raw melcell "$(c 400 237);wait;$(c 865 641)"; cut melcell 620,640,1360,940; fi
if want riff; then raw riff "$(c 400 162)"; cut riff 0,600,1360,940; fi
if want drums; then raw drums "$(c 400 382)"; cut drums 0,600,1360,940; fi
if want drumgrid; then raw drumgrid "$(c 400 382);wait;$(c 165 726)"; cut drumgrid 0,600,1360,940; fi
if want line; then raw line "$(c 700 312)"; cut line 0,600,1360,940; fi
if want poly; then raw poly "$(c 800 462)"; cut poly 0,600,1360,940; fi
if want polychord; then raw polychord "wheel 800 400 -3;wait;wait;$(c 1280 404)"; cut polychord 0,600,1360,940; fi
if want rings; then raw rings "wheel 800 400 -3;wait;wait;$(c 760 464)"; cut rings 0,600,1360,940; fi
if want ai; then raw ai "$(c 1600 30)"; cut ai 560,150,1360,810; fi
# the plugins' own editors (tools/tests/koton/plug_host_run.sh draws them: the PC has no plugin processes)
if want plugins; then cp screenshots/koton-plugin-fm2.png $IMG/plugin-fm2.png; cp screenshots/koton-plugin-arp.png $IMG/plugin-arp.png; echo "  $IMG/plugin-*.png"; fi
echo "koton_shots: done"
