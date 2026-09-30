#!/bin/sh
# tools/tests/netsurf/nsfonts-conf.sh <fonts dir> -- a fontconfig file (on stdout) that gives
# Chromium NetSurf's fonts: only the card's font files (the bench's res/fonts), the families
# NetSurf substitutes (frontends/framebuffer/font_freetype.c, fb_font_aliases: Arial ->
# Liberation Sans, Verdana -> DejaVu Sans, Segoe UI -> Selawik, Georgia -> Gelasio...) and its
# generic families, DejaVu Sans as the fallback. layoutdiff.sh runs Chromium with it
# (FONTCONFIG_FILE), so that the boxes compared differ by their layout, not by the fonts a
# Linux Chromium has (it has no Verdana, Segoe UI or Georgia: the user's Windows Chrome has).
dir=$1
alias() { # family -> font
	printf '<match target="pattern"><test qual="any" name="family"><string>%s</string></test>' "$1"
	printf '<edit name="family" mode="assign" binding="strong"><string>%s</string></edit></match>\n' "$2"
}
cat <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
<dir>$dir</dir>
<cachedir>$dir/.fccache</cachedir>
<config><rescan><int>0</int></rescan></config>
EOF
for f in arial helvetica "helvetica neue" arimo tahoma "trebuchet ms" sans-serif; do alias "$f" "Liberation Sans"; done
for f in "times new roman" times tinos serif; do alias "$f" "Liberation Serif"; done
for f in "segoe ui" system-ui; do alias "$f" "Selawik"; done
alias georgia Gelasio
for f in verdana "lucida sans unicode" "lucida grande" "bitstream vera sans"; do alias "$f" "DejaVu Sans"; done
alias "bitstream vera serif" "DejaVu Serif"
for f in "courier new" courier consolas monaco menlo "lucida console" "bitstream vera sans mono" monospace; do
	alias "$f" "DejaVu Sans Mono"
done
cat <<EOF
<alias><family>Liberation Sans</family><default><family>DejaVu Sans</family></default></alias>
<match target="pattern"><edit name="family" mode="append_last"><string>DejaVu Sans</string></edit></match>
</fontconfig>
EOF
