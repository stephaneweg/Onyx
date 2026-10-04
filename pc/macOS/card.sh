#!/bin/sh
# pc/macOS/card.sh DEST -- the card's files Ledger and Letters read, copied to DEST (Ledger.app's
# Contents/Resources/sd: the read-only card under the user's own folders): the fonts, the theme, Ledger's
# templates, words (lang/: French) and manuals, wtk's words (res/lang), the demo company and its bank
# statement, Letters' fonts (res/fonts).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SD="$HERE/../../sdcard"
D=$1
[ -n "$D" ] || { echo "usage: card.sh DEST"; exit 1; }
mkdir -p "$D/fonts" "$D/res/fonts" "$D/res/lang" "$D/etc" "$D/docs" "$D/apps/ledger.app" "$D/manuals/ledger"
cp "$SD"/fonts/*.fnt "$D/fonts/"
cp "$SD"/res/fonts/* "$D/res/fonts/"
cp "$SD"/res/lang/* "$D/res/lang/"
cp "$SD/etc/theme.txt" "$D/etc/"
cp "$SD/apps/ledger.app/app.txt" "$SD/apps/ledger.app/icon.bmp" "$D/apps/ledger.app/"
cp -R "$SD/apps/ledger.app/templates" "$SD/apps/ledger.app/lang" "$D/apps/ledger.app/"
cp "$SD/docs/demo-company.ledger" "$SD/docs/demo-bank-statement.cod" "$D/docs/"
cp "$SD"/manuals/ledger/*.pdf "$D/manuals/ledger/"
