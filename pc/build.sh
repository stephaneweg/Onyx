#!/bin/sh
# build.sh -- the Onyx tools for Windows, built on Linux: obcore.dll (mingw-w64: the compiler, VM
# and screen of user/basic), OnyxBasic.exe (the .NET Framework 4.8 editor + runtime) and OnyxRemote.exe
# (the client of rdpd; the .NET SDK with EnableWindowsTargeting), NintendoEMU.exe + nemucore.dll (the
# emulators of Onyx), Koton (pc/Koton/build.sh: pc/dist/Koton/) and Jet Browser (pc/Jet/build.sh: pc/dist/Jet/). Result: pc/dist/ -- copy it to the PC.
#   sh pc/build.sh
# On Windows (Git Bash / MSYS2): a MinGW-w64 g++ (e.g. WinLibs) and the .NET SDK on the PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
DIST="$HERE/dist"
mkdir -p "$DIST"
STRIP=$(command -v x86_64-w64-mingw32-strip || echo strip)
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ \
	-I "$ROOT/user" -o "$DIST/obcore.dll" "$HERE/obcore/obcore.cpp" \
	"$ROOT/user/basic/bascomp.cpp" "$ROOT/user/basic/basvm.cpp" "$ROOT/user/basic/basnum.cpp" "$ROOT/user/basic/basbax.cpp" -lwinmm
"$STRIP" "$DIST/obcore.dll"
DOTNET=${DOTNET:-$(command -v dotnet || echo "$HOME/.dotnet/dotnet")}
DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 "$DOTNET" build "$HERE/OnyxBasic/OnyxBasic.csproj" -c Release -o "$HERE/OnyxBasic/bin/out" -v quiet -nologo
cp "$HERE/OnyxBasic/bin/out/OnyxBasic.exe" "$HERE/OnyxBasic/bin/out/OnyxBasic.exe.config" "$DIST/"
cp "$ROOT/sdcard/apps/qbasic.app/help.txt" "$DIST/help.txt"
# Onyx Remote: the client of rdpd (the Onyx windows on the PC)
DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 "$DOTNET" build "$HERE/OnyxRemote/OnyxRemote.csproj" -c Release -o "$HERE/OnyxRemote/bin/out" -v quiet -nologo
cp "$HERE/OnyxRemote/bin/out/OnyxRemote.exe" "$HERE/OnyxRemote/bin/out/OnyxRemote.exe.config" "$DIST/"
# NintendoEMU: the emulators of Onyx (user/gb, gba, nes, snes, n64, gc) + a library of the games
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ \
	-I "$ROOT/user" -o "$DIST/nemucore.dll" "$HERE/NintendoEMU/core/nemucore.cpp" "$HERE/NintendoEMU/core/gxgl.cpp" \
	"$ROOT/user/gb/gb.cpp" "$ROOT"/user/gba/*.cpp "$ROOT"/user/nes/*.cpp "$ROOT"/user/snes/*.cpp \
	"$ROOT"/user/n64/*.cpp "$ROOT"/user/gc/*.cpp -lwinmm -lopengl32 -lgdi32
"$STRIP" "$DIST/nemucore.dll"
DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 "$DOTNET" build "$HERE/NintendoEMU/NintendoEMU.csproj" -c Release -o "$HERE/NintendoEMU/bin/out" -v quiet -nologo
cp "$HERE/NintendoEMU/bin/out/NintendoEMU.exe" "$HERE/NintendoEMU/bin/out/NintendoEMU.exe.config" "$DIST/"
# Koton, the studio of Onyx, for Windows: pc/dist/Koton (its own script: the Onyx sources over a Win32 kapi)
sh "$HERE/Koton/build.sh"
# Jet Browser for Windows: pc/dist/Jet + pc/dist/Jet.zip (its own script: NetSurf and its libraries over a Win32 kapi)
sh "$HERE/Jet/build.sh"
echo "built: $DIST"
