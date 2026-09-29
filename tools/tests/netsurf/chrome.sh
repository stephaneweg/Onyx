#!/bin/sh
# tools/tests/netsurf/chrome.sh -- the same page in Chromium (headless), as a reference for
# shot.sh's NetSurf picture:
#
#   sh tools/tests/netsurf/chrome.sh <url | file> <out.png> [width] [height]
#
# (default 1280x810: the page area of shot.sh's default window). Uses Playwright's Chromium
# (/opt/pw-browsers) or CHROME.
set -e
url=$1; png=$2; w=${3:-1280}; h=${4:-810}
case "$url" in *://*) ;; *) url="file://$(realpath "$url")";; esac
CHROME=${CHROME:-$(ls /opt/pw-browsers/chromium_headless_shell-*/chrome-linux/headless_shell 2>/dev/null | head -1)}
[ -x "$CHROME" ] || { echo "no Chromium (set CHROME)"; exit 1; }
"$CHROME" --headless --no-sandbox --hide-scrollbars --window-size="$w,$h" \
	--screenshot="$(realpath -m "$png")" "$url" >/dev/null 2>&1
echo "$png"
