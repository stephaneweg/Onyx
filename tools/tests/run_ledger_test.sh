#!/bin/sh
# run_ledger_test.sh -- Ledger's books (user/Apps/ledger/) on the PC: money, dates, the Belgian checks, invoices
# and statements posted, the VAT grids, matching, the file's round trip, the reports, the year's end; then the
# Intervat XML files it wrote checked against the official schemas (tools/tests/ledger/xsd/, SPF Finances
# v0.9) by xmllint, when installed; so is its SEPA credit transfer file (ISO 20022 pain.001.001.09).
#
#   sh tools/tests/run_ledger_test.sh
#
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/onyx_ledger_test
mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-format-truncation -fsanitize=address,undefined \
    -I"$ROOT/user" -I"$ROOT/kernel/include" "$HERE/ledger/engine_test.cpp" -o "$OUT/engine_test"
D=$OUT/files; rm -rf "$D"; mkdir -p "$D"
"$OUT/engine_test" "$D"
if command -v xmllint >/dev/null 2>&1; then
	X=$HERE/ledger/xsd
	xmllint --noout --schema "$X/NewTVA-in_v0_9.xsd" "$D/vat_return.xml" "$D/vat_return_month.xml"
	xmllint --noout --schema "$X/NewLK-in_v0_9.xsd" "$D/client_listing.xml"
	xmllint --noout --schema "$X/NewICO-in_v0_9.xsd" "$D/intra_listing.xml"
	xmllint --noout --schema "$X/pain.001.001.09.xsd" "$D/payments.xml"
else
	echo "(xmllint not installed: the XML files not checked against the schemas)"
fi
