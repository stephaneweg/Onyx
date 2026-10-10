#!/bin/sh
# build.sh -- PocketTerm Simulator alone (the .NET SDK on the PATH; Windows or Linux), into pc/dist:
# PocketTermSim.exe, its .config and the default PocketTermSim.buttons.ini. It leaves the other programs
# of pc/dist as they are (pc/build.sh builds everything, this one too).
#   sh pc/PocketTermSim/build.sh
# MIT licence (Onyx).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DIST="$HERE/../dist"
mkdir -p "$DIST"
DOTNET=${DOTNET:-$(command -v dotnet || echo "$HOME/.dotnet/dotnet")}
DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 "$DOTNET" build "$HERE/PocketTermSim.csproj" -c Release -o "$HERE/bin/out" -v quiet -nologo
cp "$HERE/bin/out/PocketTermSim.exe" "$HERE/bin/out/PocketTermSim.exe.config" "$DIST/"
cp "$HERE/PocketTermSim.buttons.ini" "$DIST/"
echo "built: $DIST/PocketTermSim.exe"
