#!/bin/sh
# tools/manuals/ledger_shots.sh -- the Ledger manual's pictures (sdcard/manuals/ledger/images/*.png), taken
# from the REAL app run on the PC against the stand-in kernel (as tools/tests/desktop_sim/shots.sh does):
# Ledger (and Writer, for the printed documents and a report) built for the host, driven by a script of
# events over the demo company (SD:/docs/demo-company.ledger), their windows dumped and made PNGs; two of
# them then marked with numbered callouts (annotate.py). The stand-in kernel's day is 28/09/2026: the
# pictures come out the same each time.
#
#   sh tools/manuals/ledger_shots.sh [name ...]		(default: all of them)
#
# Needs g++, python3 with Pillow + numpy. What the apps write (the books, the printed documents) goes to
# a scratch folder, never to sdcard/.
set -e
cd "$(dirname "$0")/../.."
D=tools/tests/desktop_sim
OUT=${SHOTS_TMP:-/tmp/onyx_manual_shots}
IMG=sdcard/manuals/ledger/images
WANT=" $* "
rm -rf "$OUT/writes"; mkdir -p "$OUT/obj" "$OUT/writes" "$OUT/ft" "$IMG"
: > "$OUT/log.txt"
export SIM_WRITES="$OUT/writes"
CXX="g++ -std=gnu++17 -O1 -w -I user -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
want () { [ "$WANT" = "  " ] || case "$WANT" in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

# ---- the building: wtk, the stand-in kernel, FreeType (Writer's), Ledger and Writer ----------------------
for f in user/wtk/*.cpp; do $CXX -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
rm -f "$OUT/libwtk.a"; ar rcs "$OUT/libwtk.a" "$OUT"/obj/*.o
$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
FT=third_party/freetype-2.14.3
FT_SRC="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c"
for f in $FT_SRC; do gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
	-Iuser/ft -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
rm -f "$OUT/libft.a"; ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
$CXX -Iuser/ft -I$FT/include -o "$OUT/ledger" "$OUT/fakekapi.o" user/Apps/ledger/main.cpp "$OUT/libwtk.a" "$OUT/libft.a" &
$CXX -Iuser/ft -I$FT/include -o "$OUT/writer" "$OUT/fakekapi.o" user/Apps/writer/main.cpp "$OUT/libwtk.a" "$OUT/libft.a" &
wait

# ---- the running --------------------------------------------------------------------------------------------
# sim APP NAME "SCRIPT" [VAR=value ...]: APP through the script, its window -> IMG/NAME.png
sim () {
	app=$1; name=$2; script=$3; shift 3
	if ! env SIM_POS=100,100 "$@" SIM="wait;wait;$script;wait;wait;wait;dump $OUT/$name.elsm;exit" "$OUT/$app" >>"$OUT/log.txt" 2>&1
	then echo "ledger_shots: $name failed (see $OUT/log.txt)"; exit 1; fi
	python3 $D/shot.py "$OUT/$name.elsm" "$IMG/$name.png" >/dev/null && echo "  $IMG/$name.png"
}
L=SIM_ARGS=SD:/docs/demo-company.ledger
# typ "text": a key a character (a space: key 32)
typ () {
	s=$1; o=""
	while [ -n "$s" ]; do
		c=${s%"${s#?}"}; s=${s#?}
		if [ "$c" = " " ]; then o="$o;key 32"; else o="$o;key $c"; fi
	done
	printf '%s' "${o#;}"
}
TAB="key 0x09"; ENTER="key 13"
c () { printf "down %d %d;up %d %d" $1 $2 $1 $2; }			# a click (the window's client coordinates)
dc () { printf "down %d %d;up %d %d;wait;down %d %d;up %d %d" $1 $2 $1 $2 $1 $2 $1 $2; }	# a double click
rc () { printf "rdown %d %d;rup %d %d" $1 $2 $1 $2; }			# a right click
# the side bar's pages
OVERVIEW=$(c 60 119); SALES=$(c 60 173); PURCH=$(c 60 203); BANK=$(c 60 233); MISC=$(c 60 263); DOCS=$(c 60 317)
CUST=$(c 60 371); SUPP=$(c 60 401); CHART=$(c 60 455); REPORTS=$(c 60 485); VAT=$(c 60 515); SETTINGS=$(c 60 569)
# a page's buttons (at the top right), a list's rows (the first one at 146, a row 24 high)
row () { echo $((146 + 24 * $1)); }
OFF=$(c 496 148)			# (a click on a document's Description: its lines' grid left, not in its editing)
# Settings' parts; a report chosen in the list of reports
S_YEARS=$(c 365 84); S_JOURNALS=$(c 472 84); S_ACCOUNTS=$(c 562 84); S_PRINTING=$(c 652 84)
report () { printf "%s;wait;%s" "$(c 328 82)" "$(c 300 $((113 + 24 * $1)))"; }	# 0 Journals, 1 General ledger, 2 Trial balance,
									# 3 Balance sheet, 4 Income statement, 7 Receivables by age
# Writer's window made the screen's, the page's width shown (its menu's item 16; 17: the whole page)
WRITER="wait;wait;winctl 2;wait;wait;menu 17;wait;wait"

# ---- getting started (no books open: the welcome, a new company) -------------------------------------------
if want welcome; then rm -f "$OUT/writes/apps/ledger.app/last.txt"; sim ledger welcome "wait"; fi
if want new-company; then
	rm -f "$OUT/writes/apps/ledger.app/last.txt"
	sim ledger new-company "wait;$(c 604 265);wait;$(typ 'Studio Nova SRL');$TAB;$(typ BE0456789034);$TAB;$(typ 'Rue Haute 120');$TAB;$(typ 1000);$TAB;$(typ Bruxelles);$TAB;$(typ hello@studionova.be);$TAB;$(typ '+32 2 555 12 34');$TAB;$(typ BE22068999887747);wait"
fi
# ---- the demo company ------------------------------------------------------------------------------------------
if want overview; then sim ledger overview "wait" $L; fi
# settings
if want settings-company; then sim ledger settings-company "$SETTINGS" $L; fi
if want settings-years; then sim ledger settings-years "$SETTINGS;wait;$S_YEARS" $L; fi
if want settings-journals; then sim ledger settings-journals "$SETTINGS;wait;$S_JOURNALS" $L; fi
if want journal-dialog; then sim ledger journal-dialog "$SETTINGS;wait;$S_JOURNALS;wait;$(dc 300 209)" $L; fi
if want settings-accounts; then sim ledger settings-accounts "$SETTINGS;wait;$S_ACCOUNTS" $L; fi
if want settings-printing; then sim ledger settings-printing "$SETTINGS;wait;$S_PRINTING" $L; fi
if want close-year; then sim ledger close-year "$SETTINGS;wait;$S_YEARS;wait;$(c 892 175)" $L; fi
# the parties, the chart
if want customers; then sim ledger customers "$CUST;wait;$(c 396 194)" $L; fi
if want customer-card; then sim ledger customer-card "$CUST;wait;$(c 396 194);wait;$(c 796 28)" $L; fi
if want suppliers; then sim ledger suppliers "$SUPP" $L; fi
if want chart; then sim ledger chart "$CHART;wait;$(c 876 84);wait;$(typ 7001);wait;$(c 400 194)" $L; fi
if want account-dialog; then sim ledger account-dialog "$CHART;wait;$(c 876 84);wait;$(typ 7001);wait;$(c 400 194);wait;$(c 800 28)" $L; fi
# sales
if want sales; then sim ledger sales "$SALES" $L; fi
if want sales-menu; then sim ledger sales-menu "$SALES;wait;$(rc 460 $(row 7))" $L; fi
if want party-picker; then sim ledger party-picker "$SALES;wait;$(c 916 28);wait;$(typ Brou)" $L; fi
if want invoice-new; then
	sim ledger invoice-new "$SALES;wait;$(c 916 28);wait;$(typ Brou);wait;$ENTER;wait;$(c 476 279);wait;$(typ 'Label design, 3 beers');$TAB;$(typ 1850);$TAB;$TAB;$TAB;$TAB;wait;$(typ 'Printing proofs');$TAB;$(typ 240);$TAB;wait;$OFF;wait;$(typ 'Beer labels: design and proofs')" $L
fi
if want invoice; then sim ledger invoice "$SALES;wait;$(c 400 $(row 7));wait;$ENTER;wait;$OFF" $L; fi
if want invoice-printed; then
	sim ledger invoice-print0 "$SALES;wait;$(c 400 $(row 7));wait;$ENTER;wait;$(c 604 28)" $L
	sim writer invoice-printed "$WRITER" SIM_ARGS="--merge SD:/apps/ledger.app/merge.job" SIM_OVERLAY="$OUT/writes"
	rm -f "$IMG/invoice-print0.png"
fi
# purchases, paying them
if want purchases; then sim ledger purchases "$PURCH" $L; fi
if want purchase; then sim ledger purchase "$PURCH;wait;$(c 400 $(row 3));wait;$ENTER;wait;$OFF" $L; fi
if want pay-suppliers; then sim ledger pay-suppliers "$PURCH;wait;$(c 632 28);wait;$(c 96 192);wait;$(c 96 216)" $L; fi
# the bank
if want bank; then sim ledger bank "$BANK" $L; fi
if want statement; then sim ledger statement "$BANK;wait;$(c 400 $(row 0));wait;$ENTER;wait;$(c 500 244)" $L; fi
if want coda; then
	sim ledger coda "$BANK;wait;$(c 756 28);wait;$(c 499 441);wait;$(typ demo-bank-statement.cod);wait;$(c 540 478)" $L
fi
# miscellaneous operations
if want misc; then sim ledger misc "$MISC" $L; fi
if want misc-new; then
	sim ledger misc-new "$MISC;wait;$(c 908 28);wait;$(typ 630200);$TAB;$(typ 'Depreciation 2026');$TAB;$(typ 1425);$TAB;$TAB;$TAB;$(typ 230009);$TAB;$(typ 'Depreciation 2026');$TAB;$TAB;$(typ 1425);$TAB;wait;$(c 496 116);wait;$(typ 'Depreciation of the equipment 2026')" $L
fi
# quotes and orders
if want quotes; then sim ledger quotes "$DOCS" $L; fi
if want quote; then sim ledger quote "$DOCS;wait;$(c 400 $(row 3));wait;$ENTER;wait;$OFF" $L; fi
if want quote-next; then sim ledger quote-next "$DOCS;wait;$(c 400 $(row 3));wait;$ENTER;wait;$OFF;wait;$(c 702 28)" $L; fi
if want quote-printed; then
	sim ledger quote-print0 "$DOCS;wait;$(c 400 $(row 3));wait;$ENTER;wait;$(c 600 28)" $L
	sim writer quote-printed "$WRITER" SIM_ARGS="--merge SD:/apps/ledger.app/merge.job" SIM_OVERLAY="$OUT/writes"
	rm -f "$IMG/quote-print0.png"
fi
# reports
if want reports; then sim ledger reports "$REPORTS" $L; fi
if want reports-list; then sim ledger reports-list "$REPORTS;wait;$(c 328 82)" $L; fi
if want balance-sheet; then sim ledger balance-sheet "$REPORTS;wait;$(report 3)" $L; fi
if want income-statement; then sim ledger income-statement "$REPORTS;wait;$(report 4)" $L; fi
if want receivables; then sim ledger receivables "$REPORTS;wait;$(report 7)" $L; fi
if want report-writer; then
	sim ledger report-writer0 "$REPORTS;wait;$(report 3);wait;$(c 656 28)" $L
	sim writer report-writer "$WRITER" SIM_ARGS="SD:/docs/Reports/Balance sheet abbreviated scheme 2026.rtf"
	rm -f "$IMG/report-writer0.png"
fi
# VAT
if want vat; then sim ledger vat "$VAT" $L; fi
if want vat-listings; then sim ledger vat-listings "$VAT;wait;$(c 928 28)" $L; fi

# ---- the callouts: the window's parts, an invoice's --------------------------------------------------------------
A=tools/manuals/annotate.py
if want overview && [ -f "$IMG/overview.png" ]; then		# (the company, the year shown, the pages, the file; the page's title, its
								#  main action, its content)
	python3 $A "$IMG/overview.png" "$IMG/window-parts.png" 1:160,50:l 2:17,102:l 3:14,260:l 4:22,708:l 5:244,48:t 6:690,44:t 7:986,140:r 8:986,380:r 9:986,640:r \
		&& echo "  $IMG/window-parts.png"
fi
if want invoice-new && [ -f "$IMG/invoice-new.png" ]; then	# (the page only: the party, the dates, the description, the kind, the
								#  communication, the reference, the lines, the entry, the totals, Save)
	python3 $A "$IMG/invoice-new.png" "$IMG/invoice-parts.png" --crop 212,28,1004,728 1:224,112:l 2:930,112:r 3:224,176:l 4:978,177:r 5:224,208:l \
		6:982,208:r 7:224,279:l 8:224,600:l 9:986,650:r 10:984,56:t && echo "  $IMG/invoice-parts.png"
fi
