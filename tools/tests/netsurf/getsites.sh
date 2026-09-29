#!/bin/sh
# tools/tests/netsurf/getsites.sh -- local copies of the two sites NetSurf is compared on (the
# user's: kotonviolins.com, kotonstudio.com), for the PC bench, whose NetSurf has no https.
# wget mirrors each home page with what it needs -- style sheets, scripts, images, the Google
# Fonts it uses (TrueType) -- its links made local. Then:
#
#   sh tools/tests/netsurf/getsites.sh [dir]                      (default /tmp/nssites)
#   sh tools/tests/netsurf/shot.sh /tmp/nssites/kotonviolins.com/index.html kv.png 700x1200
#   sh tools/tests/netsurf/chrome.sh /tmp/nssites/kotonviolins.com/index.html kv-chrome.png 700 1200
DIR=${1:-/tmp/nssites}
mkdir -p "$DIR" && cd "$DIR" || exit 1
for url in https://kotonviolins.com/ https://kotonstudio.com/fr/; do
	host=$(echo "$url" | sed -E 's|https?://([^/]+)/.*|\1|')
	# (a missing requisite makes wget's status non-zero: the rest is still there)
	wget -q -p -k -E -H -D "$host,fonts.googleapis.com,fonts.gstatic.com" \
		--restrict-file-names=windows -e robots=off \
		-U "Mozilla/5.0 (Windows NT 10.0; Win64; x64)" "$url"
done
ls -d "$DIR/kotonviolins.com/index.html" "$DIR/kotonstudio.com/fr/index.html"
