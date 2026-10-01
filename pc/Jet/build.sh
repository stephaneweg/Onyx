#!/bin/sh
# pc/Jet/build.sh -- Jet Browser for Windows x64, built on Linux with MinGW-w64 from the Onyx sources (pc/Jet/jet.mk:
# the libraries, NetSurf's core, the framebuffer frontend, the Onyx glue, wtk -- over pc/Jet/winkapi.cpp, the
# Onyx kernel's table on Win32). Result: pc/dist/Jet/ -- Jet.exe and what it reads (res\: Choices, Messages,
# the style sheets, ca-bundle, the fonts; data\jet.ini; wtk's font and theme) -- and pc/dist/Jet.zip.
#   sh pc/Jet/build.sh            (JET_WIN_OUT: the objects' folder, default /home/user/jetwin-build; JOBS)
# Needs x86_64-w64-mingw32-gcc / g++ (the posix threads variant: apt-get install g++-mingw-w64-x86-64-posix
# gcc-mingw-w64-x86-64-posix), gcc + libpng (the host tools), perl, python3 + Pillow (the icon), zip.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${JET_WIN_OUT:-/home/user/jetwin-build}
DIST="$ROOT/pc/dist/Jet"
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
STRIP=x86_64-w64-mingw32-strip
SD="$ROOT/sdcard"

make -C "$ROOT" -f pc/Jet/jet.mk OUT="$OUT" -j"$JOBS"

rm -rf "$DIST"
mkdir -p "$DIST/res" "$DIST/data" "$DIST/fonts" "$DIST/etc"
cp "$OUT/Jet.exe" "$DIST/Jet.exe"		# (the one with its symbols stays in $OUT: addr2line -e)
"$STRIP" "$DIST/Jet.exe"
# res\: the card's (sdcard/res, as netsurf-app.mk stages it: Choices, Messages, the UA style sheets, the
# trusted roots -- NetSurf's ca-bundle, as on the Pi --, the fonts and their licences)
(cd "$SD/res" && git -C "$ROOT" ls-files -- sdcard/res | sed 's|^sdcard/res/||' | while read -r f; do
	mkdir -p "$DIST/res/$(dirname "$f")"; cp "$f" "$DIST/res/$f"; done)
# data\: the browser's own folder (on the Pi SD:/apps/jet.app/): the User-Agent file; the rest is made there
cp "$SD/apps/jet.app/jet.ini" "$DIST/data/"
# wtk's: its bitmap font, the theme
cp "$SD"/fonts/*.fnt "$DIST/fonts/"
cp "$SD/etc/theme.txt" "$DIST/etc/"
cp "$HERE/README.txt" "$DIST/"
# pc/dist/Jet.zip: the folder, for a download
rm -f "$ROOT/pc/dist/Jet.zip"
(cd "$ROOT/pc/dist" && zip -qr9 Jet.zip Jet)
echo "built: $DIST ($(du -sh "$DIST" | cut -f1)), $ROOT/pc/dist/Jet.zip ($(du -h "$ROOT/pc/dist/Jet.zip" | cut -f1))"
