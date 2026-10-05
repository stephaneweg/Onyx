#!/bin/sh
# pc/macOS/check.sh -- the macOS port's POSIX half (hostkapi.cpp: the card's two folders, the programs
# started, their arguments) checked on Linux: Ledger and Letters built over it with a screen-less window
# (headless.cpp), Ledger opens the demo company, prints a quote (Letters started as on the Mac, its
# document written on the user's card), switches to French and back, the window's pictures in $OUT. Needs g++ (and Pillow for the PNGs).
#   sh pc/macOS/check.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${MAC_CHECK_TMP:-/tmp/onyx_mac_check}
U="$ROOT/user"
FT="$ROOT/third_party/freetype-2.14.3"
CXX="g++ -std=gnu++17 -O1 -w -I$U -I$U/Kits -I$U/Runtime -I$U/Include -I$U/Libs -I$U/Emulators -I$U/Ports -I$ROOT/kernel/include -fno-exceptions -fno-rtti -include $HERE/onyxmac.h -DIMG_HOST_TEST"
rm -rf "$OUT"; mkdir -p "$OUT/uikit" "$OUT/ft" "$OUT/helpers/Letters.app/Contents/MacOS" "$OUT/user" "$OUT/docs"
for f in "$U"/Kits/uikit/*.cpp; do $CXX -c "$f" -o "$OUT/uikit/$(basename "$f" .cpp).o" & done; wait
ar rcs "$OUT/libuikit.a" "$OUT"/uikit/*.o
for f in base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c \
	 truetype/truetype.c sfnt/sfnt.c smooth/smooth.c; do
	gcc -O2 -w -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
		-I"$U/ft" -I"$FT/include" -c "$FT/src/$f" -o "$OUT/ft/$(basename "$f" .c).o" &
done; wait
ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
$CXX -c "$HERE/hostkapi.cpp" -o "$OUT/hostkapi.o"
$CXX -c "$HERE/headless.cpp" -o "$OUT/headless.o"
# (hostkapi.o first: its table is placed before the apps' constructors run)
$CXX -o "$OUT/ledger" "$OUT/hostkapi.o" "$OUT/headless.o" "$U/Apps/ledger/main.cpp" "$OUT/libuikit.a" -lpthread
$CXX -I"$U/ft" -I"$FT/include" -o "$OUT/helpers/Letters.app/Contents/MacOS/Letters" "$OUT/hostkapi.o" "$OUT/headless.o" "$U/Apps/letters/main.cpp" \
	"$OUT/libuikit.a" "$OUT/libft.a" -lpthread
sh "$HERE/card.sh" "$OUT/base"
export ONYX_SD="$OUT/user" ONYX_DOCS="$OUT/docs" ONYX_SD_BASE="$OUT/base" ONYX_HELPERS="$OUT/helpers"
fail () { echo "check.sh: FAILED: $*"; exit 1; }

# 1. the demo company opened (in the bundle's card), its overview drawn
HEADLESS_DUMP="$OUT/overview.ppm" "$OUT/ledger" SD:/docs/demo-company.ledger
[ -s "$OUT/overview.ppm" ] || fail "no picture of the overview"
grep -q demo-company "$OUT/user/apps/ledger.app/last.txt" || fail "last.txt not written on the user's card"
# 2. the books opened last (last.txt, read back), the quotes, a quote, printed: Letters started (ONYX_ARGS)
#    (Helpers/Letters.app, as in Ledger.app) with the merge job, its document written in SD:/docs/Quotes
#    (the user's ~/Documents/Onyx Ledger/Quotes)
HEADLESS_DUMP="$OUT/quote.ppm" HEADLESS_SCRIPT="wait;down 60 316;up 60 316;wait;down 400 218;up 400 218;wait;key 13;wait;wait;down 592 28;up 592 28;wait;wait" "$OUT/ledger"
[ -s "$OUT/user/apps/ledger.app/merge.job" ] || fail "no merge job"
for i in 1 2 3 4 5 6 7 8 9 10; do ls "$OUT/docs/Quotes/"* >/dev/null 2>&1 && break; sleep 1; done
ls "$OUT/docs/Quotes/"* >/dev/null 2>&1 || fail "Letters made no quote in SD:/docs/Quotes"
# 3. the first start copied Ledger's templates to the user's card
[ -s "$OUT/user/apps/ledger.app/templates/quote.rtf" ] || fail "the templates not copied to the user's card"
# 4. a quote saved (Ctrl+S) in the demo company, the bundle's (read-only): the books written in the user's
#    SD:/docs, the bundle's copy unchanged
[ ! -e "$OUT/docs/demo-company.ledger" ] || fail "the demo company written before any change"
HEADLESS_SCRIPT="wait;down 60 316;up 60 316;wait;down 400 218;up 400 218;wait;key 13;wait;key 19;wait;wait" "$OUT/ledger"
[ -s "$OUT/docs/demo-company.ledger" ] || fail "the saved books not written in the user's SD:/docs"
cmp -s "$OUT/base/docs/demo-company.ledger" "$ROOT/sdcard/docs/demo-company.ledger" || fail "the bundle's demo company changed"
# 5. a file given as a host path (Finder's "Open With"): seen as MAC:/...
cp "$OUT/base/docs/demo-company.ledger" "$OUT/copy.ledger"
HEADLESS_DUMP="$OUT/hostpath.ppm" "$OUT/ledger" "$OUT/copy.ledger"
grep -q "MAC:$OUT/copy.ledger" "$OUT/user/apps/ledger.app/last.txt" || fail "a host path not opened as MAC:/..."
# 6. the language: FR clicked at the side bar's foot -> lang.txt "fr", Ledger started again by itself (on the same
#    books, ONYX_ARGS), in French; EN back
rm -f "$OUT/user/apps/ledger.app/lang.txt"
HEADLESS_SCRIPT="wait;down 178 680;up 178 680;wait" "$OUT/ledger" SD:/docs/demo-company.ledger
[ "$(cat "$OUT/user/apps/ledger.app/lang.txt" 2>/dev/null)" = fr ] || fail "FR clicked: lang.txt not written"
HEADLESS_DUMP="$OUT/french.ppm" "$OUT/ledger" SD:/docs/demo-company.ledger
HEADLESS_SCRIPT="wait;down 144 680;up 144 680;wait" "$OUT/ledger" SD:/docs/demo-company.ledger
[ "$(cat "$OUT/user/apps/ledger.app/lang.txt" 2>/dev/null)" = en ] || fail "EN clicked: lang.txt not written"
python3 - "$OUT" <<'PY' 2>/dev/null || true
import sys, glob
from PIL import Image
for p in glob.glob (sys.argv[1] + "/*.ppm"): Image.open (p).save (p[:-4] + ".png")
PY
echo "check.sh: OK ($OUT: overview.png, quote.png, french.png, docs/Quotes)"
ls "$OUT/docs/Quotes"; ls "$OUT/user/apps/ledger.app/templates" | head -3
