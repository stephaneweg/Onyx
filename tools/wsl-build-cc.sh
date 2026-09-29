#!/bin/sh
# wsl-build-cc.sh -- a stand-in for BUILD_CC (the host compiler NetSurf's codegen tools need,
# convert_font / convert_image) on a WSL without a native gcc: compiles the tool with the
# Windows MinGW gcc (WinLibs), and puts at the -o path a shell wrapper that runs the .exe with
# its path arguments turned into Windows paths (wslpath -w). `-lpng` brings libpng + zlib
# from third_party. Use:
#   make -f user/netsurf/netsurf-app.mk BUILD_CC=$PWD/tools/wsl-build-cc.sh link
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TP="$HERE/../third_party"
GCC=${WIN_GCC:-$(ls -d /mnt/c/Users/*/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.*/mingw64/bin/gcc.exe 2>/dev/null | head -1)}
[ -x "$GCC" ] || { echo "wsl-build-cc: no Windows gcc (set WIN_GCC)" >&2; exit 1; }

win () { case "$1" in /*) wslpath -w "$1" ;; *) echo "$1" ;; esac; }

out= args= png=
while [ $# -gt 0 ]; do
	case "$1" in
	-o) out="$2"; shift 2 ;;
	-lpng) png=1; shift ;;
	-I*) args="$args -I$(win "${1#-I}")"; shift ;;
	-*) args="$args $1"; shift ;;
	*) args="$args $(win "$1")"; shift ;;
	esac
done
[ -n "$out" ] || { echo "wsl-build-cc: no -o" >&2; exit 1; }
if [ -n "$png" ]; then
	P="$TP/libpng-1.6.44"; Z="$TP/zlib-1.3.1"
	for f in png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset \
		 pngtrans pngwio pngwrite pngwtran pngwutil; do args="$args $(win "$P/$f.c")"; done
	for f in adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil; do
		args="$args $(win "$Z/$f.c")"; done
	args="$args -I$(win "$P") -I$(win "$Z") -DPNG_ARM_NEON_OPT=0"
fi
# (the .exe on the Windows side -- Windows cannot run one kept in WSL's own file system --,
# in the Windows user's temp folder; gcc.exe gets Windows paths)
EXEDIR="${GCC%%/AppData/*}/AppData/Local/Temp/onyx-buildcc"
mkdir -p "$EXEDIR"
exe="$EXEDIR/$(basename "$out").exe"
"$GCC" $args -o "$(win "$exe")"
cat > "$out" <<EOF
#!/bin/sh
# (made by tools/wsl-build-cc.sh) run the Windows tool with Windows paths
set --\$(for a in "\$@"; do case "\$a" in /*) printf ' %s' "\$(wslpath -w "\$a")" ;; *) printf ' %s' "\$a" ;; esac; done)
exec "$exe" "\$@"
EOF
chmod +x "$out"
