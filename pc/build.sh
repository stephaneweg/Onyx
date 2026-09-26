#!/bin/sh
# build.sh -- Onyx BASIC for Windows, built on Linux: obcore.dll (mingw-w64: the compiler, VM
# and screen of user/basic) and OnyxBasic.exe (the .NET Framework 4.8 editor + runtime; the
# .NET SDK with EnableWindowsTargeting). Result: pc/dist/ -- copy that folder to the PC.
#   sh pc/build.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
DIST="$HERE/dist"
mkdir -p "$DIST"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ \
	-I "$ROOT/user" -o "$DIST/obcore.dll" "$HERE/obcore/obcore.cpp" \
	"$ROOT/user/basic/bascomp.cpp" "$ROOT/user/basic/basvm.cpp" "$ROOT/user/basic/basnum.cpp" -lwinmm
x86_64-w64-mingw32-strip "$DIST/obcore.dll"
DOTNET=${DOTNET:-$(command -v dotnet || echo "$HOME/.dotnet/dotnet")}
DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 "$DOTNET" build "$HERE/OnyxBasic/OnyxBasic.csproj" -c Release -o "$HERE/OnyxBasic/bin/out" -v quiet -nologo
cp "$HERE/OnyxBasic/bin/out/OnyxBasic.exe" "$HERE/OnyxBasic/bin/out/OnyxBasic.exe.config" "$DIST/"
cp "$ROOT/sdcard/apps/qbasic.app/help.txt" "$DIST/help.txt"
echo "built: $DIST"
