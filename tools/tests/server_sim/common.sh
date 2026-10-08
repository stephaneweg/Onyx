# tools/tests/server_sim/common.sh -- what run.sh and adaptive.sh share: UIKit built with the two wire ports, the
# stand-in kernel, the servers' objects, FreeType; the helpers app / ftapp (an app linked with a server and a UIKit),
# run (an app under its server through a script, its checks counted), png. Sourced: OUT is $1 or /tmp/server_sim.
OUT=${1:-/tmp/server_sim}
mkdir -p "$OUT/obj"
D=tools/tests/desktop_sim
S=tools/tests/server_sim
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include"
CXX="g++ -std=gnu++17 -O1 -w -fno-exceptions -fno-rtti"
SRV="-DWIN_PIXELS_HOOK -I $D/kstub"
FAIL=0
: > "$OUT/log.txt"

# ---- UIKit (two wire ports), the stand-in kernel, the apps' objects -------------------------------------------
build_uikit () {	# build_uikit <name> <flags>: UIKit's objects with that port -> $OUT/lib<name>.a
	mkdir -p "$OUT/obj/$1"
	for f in user/Kits/uikit/*.cpp; do $CXX $INC -DIMG_HOST_TEST $2 -c "$f" -o "$OUT/obj/$1/$(basename "$f" .cpp).o" & done; wait
	rm -f "$OUT/lib$1.a"; ar rcs "$OUT/lib$1.a" "$OUT/obj/$1"/*.o
}
build_uikit uikit_pocket "-DUK_PORT_WIRE -DUK_PORT_POCKET"
build_uikit uikit_wire "-DUK_PORT_WIRE"
$CXX $INC -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"

# ---- the servers' objects -------------------------------------------------------------------------------------
# server <name> <sources> <flags>: the server's code for the host -> $OUT/obj/<name>/*.o
server () {
	mkdir -p "$OUT/obj/$1"; rm -f "$OUT/obj/$1"/*.o
	for f in $2; do $CXX $SRV $3 $INC -c "$f" -o "$OUT/obj/$1/$(basename "$f" .cpp).o" || return 1; done
	$CXX $SRV $3 $INC -DSIM_$(echo $1 | tr a-z A-Z) -c $S/server_sim.cpp -o "$OUT/obj/$1/server_sim.o"
	$CXX -c $S/sim_mem.cpp -o "$OUT/obj/$1/sim_mem.o"
}
C=user/Servers/common
server pocket "$C/core.cpp $C/ops.cpp $C/route.cpp $C/wm/window.cpp kernel/gui/gimage.cpp user/Servers/pocketui/wm.cpp user/Servers/pocketui/band.cpp" \
	"-I $C/wm -I $C -I user/Servers/pocketui"
server elegant "$C/core.cpp $C/ops.cpp $C/route.cpp $C/wm/window.cpp kernel/gui/gimage.cpp" "-I $C/wm -I $C"

# app <server> <app> <uikit>: the app linked with that server and UIKit -> $OUT/<server>_<app>
app () {
	$CXX $INC -o "$OUT/$1_$2" "$OUT/obj/$1"/*.o "$OUT/fakekapi.o" user/Apps/$2/main.cpp "$OUT/lib$3.a" -lpthread
}
# the FreeType apps (pocketshell, the menu bar): FreeType built once, TrueType only (as user/Makefile builds it)
FT=third_party/freetype-2.14.3
if [ ! -f "$OUT/libft.a" ]; then
	mkdir -p "$OUT/ft"
	for f in base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c; do
		gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
			-Iuser/Kits/fontkit -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
	ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
fi
# ftapp <server> <app> <uikit> [extra sources]: an app whose text is FreeType's
ftapp () {
	s=$1; a=$2; u=$3; shift 3
	$CXX $INC -Iuser/Kits/fontkit -I$FT/include -o "$OUT/${s}_$a" "$OUT/obj/$s"/*.o "$OUT/fakekapi.o" user/Apps/$a/main.cpp "$@" "$OUT/lib$u.a" "$OUT/libft.a" -lpthread
}
# run <binary> <name> "<script>" [VAR=value ...]: the app under its server through the script (its checks counted)
run () {
	b=$1; n=$2; sc=$3; shift 3
	if env SIM_OVERLAY=$D/sd "$@" SIM="$sc;exit" "$OUT/$b" >>"$OUT/log.txt" 2>&1; then echo "  $n: ok"
	else echo "  $n: FAILED (see $OUT/log.txt)"; FAIL=1; fi
}
png () { python3 $D/shot.py "$OUT/$1.elsm" "$OUT/$1.png" >/dev/null && echo "  $OUT/$1.png"; }
W="wait;wait;wait"
WW="$W;$W;$W;$W;$W;$W"			# (the Terminal: its WINRESIZE ignored, then asked to maximise -- wm.cpp FILL_WAIT)
PIPE='SD:/ $ ls /bin | grep e\necho\nsleep\nyes\nSD:/ $ ps\n  1 k R  idle\n  2 k S  usb\n 14 a R  pocketui\n 21 a R  terminal\n 22 a S  cmd\n 25 a R  ps\nSD:/ $ echo onyx | wc -c\n5\nSD:/ $ '

