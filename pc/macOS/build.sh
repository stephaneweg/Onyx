#!/bin/sh
# pc/macOS/build.sh -- Ledger for macOS (Apple silicon), built ON A MAC from the Onyx sources, unchanged:
# user/Apps/ledger (the accounting), user/Apps/letters (it prints Ledger's quotes, orders and invoices from
# their templates), user/wtk and FreeType, over pc/macOS/hostkapi.cpp + cocoa.mm (the Onyx kernel's table
# on macOS). Result: pc/dist/macOS/Ledger.app (Letters.app in its Contents/Helpers, the card's files it
# reads in its Contents/Resources/sd) and pc/dist/macOS/Ledger-macOS-arm64.zip.
#   sh pc/macOS/build.sh
# Needs the Xcode command-line tools (xcode-select --install): clang, codesign, ditto. Nothing else.
# The environment:
#   ARCHS="arm64"          the processors (default: Apple silicon only; "arm64 x86_64": a universal app)
#   MINOS=11.0             the oldest macOS it runs on (11 Big Sur ... 26 Tahoe)
#   SIGN_ID="-"            the signature: "-" ad hoc (this Mac, or one told to open it anyway: README.txt);
#                          a "Developer ID Application: ..." identity for an app to give away (then notarise:
#                          xcrun notarytool submit pc/dist/macOS/Ledger-macOS-arm64.zip ... --wait;
#                          xcrun stapler staple pc/dist/macOS/Ledger.app)
#   VERSION=1.0
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${LEDGER_MAC_TMP:-/tmp/onyx_ledger_mac}
DIST="$ROOT/pc/dist/macOS"
APP="$DIST/Ledger.app"
ARCHS=${ARCHS:-arm64}
MINOS=${MINOS:-11.0}
SIGN_ID=${SIGN_ID:--}
VERSION=${VERSION:-1.0}
BUILD=$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)
[ "$(uname)" = Darwin ] || { echo "pc/macOS/build.sh: run it on a Mac (on Linux: sh pc/macOS/check.sh checks the port)"; exit 1; }
CC="xcrun clang"
CXX="xcrun clang++"
AF=""; for a in $ARCHS; do AF="$AF -arch $a"; done
U="$ROOT/user"
FT="$ROOT/third_party/freetype-2.14.3"
FLAGS="-O2 -w $AF -mmacosx-version-min=$MINOS"
CXXF="-std=gnu++17 $FLAGS -I$U -I$ROOT/kernel/include -fno-exceptions -fno-rtti -include $HERE/onyxmac.h -DIMG_HOST_TEST"
mkdir -p "$OUT/wtk" "$OUT/ft" "$OUT/kapi"
bg () { ( "$@" || touch "$OUT/FAILED" ) & }		# a compile in the background (wtk's ~50 files at once)
done_bg () { wait; if [ -e "$OUT/FAILED" ]; then echo "pc/macOS/build.sh: a compile failed (above)"; exit 1; fi; }
rm -f "$OUT/FAILED"

# ---- the Onyx kernel's table on macOS ------------------------------------------------------------------------
$CXX $CXXF -c "$HERE/hostkapi.cpp" -o "$OUT/kapi/hostkapi.o"
$CXX $CXXF -x objective-c++ -fobjc-arc -c "$HERE/cocoa.mm" -o "$OUT/kapi/cocoa.o"
# ---- wtk ---------------------------------------------------------------------------------------------------
for f in "$U"/wtk/*.cpp; do bg $CXX $CXXF -c "$f" -o "$OUT/wtk/$(basename "$f" .cpp).o"; done; done_bg
rm -f "$OUT/libwtk.a"; ar rcs "$OUT/libwtk.a" "$OUT"/wtk/*.o
# ---- FreeType (Onyx's configuration: Letters' fonts) ----------------------------------------------------------
for f in base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c \
	 truetype/truetype.c sfnt/sfnt.c smooth/smooth.c; do
	bg $CC $FLAGS -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
		-I"$U/ft" -I"$FT/include" -c "$FT/src/$f" -o "$OUT/ft/$(basename "$f" .c).o"
done; done_bg
rm -f "$OUT/libft.a"; ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
# ---- the programs (hostkapi.o first: the table placed before the apps' constructors run) ------------------------
bg $CXX $CXXF -o "$OUT/Ledger" "$OUT/kapi/hostkapi.o" "$OUT/kapi/cocoa.o" "$U/Apps/ledger/main.cpp" "$OUT/libwtk.a" -framework Cocoa
bg $CXX $CXXF -I"$U/ft" -I"$FT/include" -o "$OUT/Letters" "$OUT/kapi/hostkapi.o" "$OUT/kapi/cocoa.o" "$U/Apps/letters/main.cpp" \
	"$OUT/libwtk.a" "$OUT/libft.a" -framework Cocoa
done_bg

# ---- Ledger.app ------------------------------------------------------------------------------------------------------
plist () { sed -e "s/@VERSION@/$VERSION/" -e "s/@BUILD@/$BUILD/" -e "s/@MINOS@/$MINOS/" "$1" > "$2"; }
rm -rf "$APP"
W="$APP/Contents/Helpers/Letters.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$W/Contents/MacOS" "$W/Contents/Resources"
cp "$OUT/Ledger" "$APP/Contents/MacOS/Ledger"
cp "$OUT/Letters" "$W/Contents/MacOS/Letters"
plist "$HERE/res/Ledger.plist" "$APP/Contents/Info.plist"
plist "$HERE/res/Letters.plist" "$W/Contents/Info.plist"
cp "$HERE/res/PkgInfo" "$APP/Contents/PkgInfo"; cp "$HERE/res/PkgInfo" "$W/Contents/PkgInfo"
cp "$HERE/res/Ledger.icns" "$APP/Contents/Resources/"
cp "$HERE/res/Letters.icns" "$W/Contents/Resources/"
sh "$HERE/card.sh" "$APP/Contents/Resources/sd"
cp "$HERE/README.txt" "$DIST/README.txt"
xattr -cr "$APP" 2>/dev/null || true				# (Finder's details, quarantine: codesign refuses them)
OPTS=""; [ "$SIGN_ID" = "-" ] || OPTS="--options runtime --timestamp"
codesign --force --sign "$SIGN_ID" $OPTS "$W"
codesign --force --sign "$SIGN_ID" $OPTS "$APP"
codesign --verify --deep --strict "$APP"
ZIP="$DIST/Ledger-macOS-$(echo $ARCHS | tr ' ' '-').zip"
rm -f "$ZIP"; ditto -c -k --keepParent "$APP" "$ZIP"
echo "built: $APP"
echo "       $ZIP"
