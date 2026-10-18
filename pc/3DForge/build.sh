#!/bin/sh
# pc/3DForge/build.sh -- 3DForge for Windows: forgecore.dll (MinGW-w64 g++: Onyx's 3DForge -- user/Apps/3dforge --,
# Manifold and Clipper2, over core/shim) and 3DForge.exe (the WPF program, .NET Framework 4.8: the .NET SDK).
# Result: pc/dist/3DForge/ (3DForge.exe, 3DForge.exe.config, forgecore.dll, printers.ini, samples\).
#   sh pc/3DForge/build.sh            (FORGE_CORE_ONLY=1: the DLL alone)
# On Windows (Git Bash): a MinGW-w64 g++ (e.g. WinLibs) and the .NET SDK on the PATH; on Linux: x86_64-w64-mingw32-g++.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OBJ="$HERE/build"; DIST="$ROOT/pc/dist/3DForge"
mkdir -p "$OBJ/mf" "$DIST"
CXX=$(command -v x86_64-w64-mingw32-g++ || echo g++)
AR=$(command -v x86_64-w64-mingw32-ar || echo ar)
STRIP=$(command -v x86_64-w64-mingw32-strip || echo strip)
MF="$ROOT/third_party/manifold-3.5.4"; CL="$ROOT/third_party/clipper2-46f6391/CPP/Clipper2Lib"
DEFS="-DMANIFOLD_PAR=-1 -DMANIFOLD_CROSS_SECTION -DMANIFOLD_NO_IOSTREAM -DMANIFOLD_NO_FILESYSTEM -DCLIPPER2_NO_IOSTREAM -I$MF/include -I$CL/include"
INC="-I$HERE/core/shim -I$ROOT/kernel/include -I$ROOT/user -I$ROOT/user/Kits -I$ROOT/user/Libs -I$ROOT/user/Apps/3dforge"

# Manifold and Clipper2, once
if [ ! -f "$OBJ/libmanifold.a" ]; then
	for f in "$MF"/src/*.cpp "$MF"/src/cross_section/*.cpp "$CL"/src/*.cpp; do
		$CXX -std=c++17 -O2 -w $DEFS -c "$f" -o "$OBJ/mf/$(basename "$f" .cpp).o" &
	done; wait
	$AR rcs "$OBJ/libmanifold.a" "$OBJ"/mf/*.o
fi
# the core: 3DForge's own code (the view's painter: UIKit's vpaint.cpp, the rest the shim's)
$CXX -std=gnu++17 -O2 -c $INC "$ROOT/user/Kits/uikit/vpaint.cpp" -o "$OBJ/vpaint.o" &
$CXX -std=gnu++17 -O2 -c $INC "$HERE/core/shim/shim.cpp" -o "$OBJ/shim.o" &
$CXX -std=gnu++17 -O2 -Wall -Wno-misleading-indentation -Wno-unused-function -Wno-unused-variable $DEFS $INC -c "$HERE/core/forgecore.cpp" -o "$OBJ/forgecore.o" &
wait
$CXX -shared -static -static-libgcc -static-libstdc++ -o "$DIST/forgecore.dll" "$OBJ/forgecore.o" "$OBJ/shim.o" "$OBJ/vpaint.o" "$OBJ/libmanifold.a" -lgdiplus -lgdi32 -lole32
"$STRIP" "$DIST/forgecore.dll"
[ -n "$FORGE_CORE_ONLY" ] && { echo "built: $DIST/forgecore.dll"; exit 0; }

# the program
DOTNET=${DOTNET:-$(command -v dotnet || echo "$HOME/.dotnet/dotnet")}
DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 "$DOTNET" build "$HERE/3DForge.csproj" -c Release -o "$HERE/bin/out" -v quiet -nologo
cp "$HERE/bin/out/3DForge.exe" "$HERE/bin/out/3DForge.exe.config" "$DIST/"
cp "$HERE/printers.ini" "$HERE/README.txt" "$DIST/"
mkdir -p "$DIST/samples" "$DIST/licences"; cp "$ROOT/sdcard/docs/3d/"*.3df "$DIST/samples/"
cp "$MF/LICENSE" "$DIST/licences/Manifold-LICENSE.txt"; cp "$CL/../../LICENSE" "$DIST/licences/Clipper2-LICENSE.txt"
echo "built: $DIST"
