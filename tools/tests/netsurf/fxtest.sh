#!/bin/sh
# tools/tests/netsurf/fxtest.sh -- the compositing layers (opacity, transform, filter,
# backdrop-filter, mix-blend-mode: docs/06 §21) against Chromium, pixel by pixel: each page
# (pages/css-opacity.html, css-transform.html, css-filter.html) shot in the PC NetSurf
# (shot.sh) and in Chromium (chrome.sh), aligned on the page's magenta mark at (0, 0), then
# each 140 px cell compared: its mean difference per channel (0..255) and the share of its
# pixels off by more than 48. A cell passes under a mean of 6 and 3 % of pixels off (blur and
# anti-aliasing differ a little between the engines). The pictures and a diff go to
# $OUT/fx/. Exit status 0: every cell passed.
#
#   OUT=/tmp/nsbench sh tools/tests/netsurf/fxtest.sh [page...]
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
mkdir -p "$OUT/fx"
pages=${*:-"css-opacity css-transform css-filter"}
fail=0
for p in $pages; do
	OUT=$OUT sh $T/shot.sh "$T/pages/$p.html" "$OUT/fx/$p-ns.png" 1100x700 40 >/dev/null || { echo "$p: NetSurf failed"; fail=1; continue; }
	sh $T/chrome.sh "$T/pages/$p.html" "$OUT/fx/$p-chrome.png" 1000 600 >/dev/null || { echo "$p: Chromium failed"; fail=1; continue; }
	python3 - "$OUT/fx/$p-ns.png" "$OUT/fx/$p-chrome.png" "$OUT/fx/$p-diff.png" "$p" <<'EOF' || fail=1
import sys
import numpy as np
from PIL import Image
ns = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.int32)
ch = np.asarray(Image.open(sys.argv[2]).convert("RGB")).astype(np.int32)
name = sys.argv[4]
# the page's origin in NetSurf's window: the magenta mark
m = (ns[:, :, 0] == 255) & (ns[:, :, 1] == 0) & (ns[:, :, 2] == 255)
ys, xs = np.nonzero(m)
if len(ys) == 0:
    print(name + ": no mark"); sys.exit(1)
oy, ox = ys.min(), xs.min()
h = min(ch.shape[0], ns.shape[0] - oy - 20)
w = min(ch.shape[1], ns.shape[1] - ox - 20)
a = ns[oy:oy + h, ox:ox + w]
b = ch[:h, :w]
d = np.abs(a - b)
Image.fromarray(np.clip(d * 4, 0, 255).astype(np.uint8)).save(sys.argv[3])
bad = 0
for row in range(3):
    for col in range(6):
        x0, y0 = 10 + col * 150, 10 + row * 150
        if y0 + 140 > h or x0 + 140 > w:
            continue
        cb = b[y0:y0 + 140, x0:x0 + 140]
        if (cb == 255).all():
            continue	# (no cell there)
        cd = d[y0:y0 + 140, x0:x0 + 140]
        mean = cd.mean()
        off = (cd.max(axis=2) > 48).mean() * 100
        ok = mean < 6 and off < 3
        bad += not ok
        print("  %s  %s cell %d: mean %.2f, %.1f%% off" % ("ok  " if ok else "FAIL", name, row * 6 + col + 1, mean, off))
sys.exit(1 if bad else 0)
EOF
done
# the hover: effects changed by :hover redrawn in part (the restyle's rectangles through the
# transforms) must give the full redraw's pixels (NS_HOVER_FULL=1: the boxes made again)
W=$(i=0; while [ $i -lt 40 ]; do printf 'wait;'; i=$((i + 1)); done)
for mode in part full; do
	for step in 1 2; do
		# step 1: over the card; step 2: over the card, then over the other box
		moves="move 140 160;${W}"
		[ "$step" = 2 ] && moves="${moves}move 350 170;${W}"
		if [ "$mode" = full ]; then export NS_HOVER_FULL=1; else unset NS_HOVER_FULL; fi
		NS_PERF=1 SIM_SCREEN=900x700 SIM_SLEEP=1 SIM_POS=0,0 \
			SIM_ARGS="file://$(realpath "$T/pages/css-fxhover.html")" \
			SIM="${W}${moves}dump $OUT/fx/hover-$mode-$step.elsm;exit" \
			timeout 300 "$OUT/build/netsurf" >"$OUT/fx/hover-$mode-$step.log" 2>&1
		python3 tools/tests/desktop_sim/shot.py "$OUT/fx/hover-$mode-$step.elsm" \
			"$OUT/fx/hover-$mode-$step.png" >/dev/null
	done
done
unset NS_HOVER_FULL
for step in 1 2; do
	python3 - "$OUT/fx/hover-part-$step.png" "$OUT/fx/hover-full-$step.png" "$step" <<'EOF' || fail=1
import sys
import numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.int32)
b = np.asarray(Image.open(sys.argv[2]).convert("RGB")).astype(np.int32)
n = int((np.abs(a - b).max(axis=2) > 8).sum())
print("  %s  css-fxhover step %s: %d pixels differ from the full redraw" % ("ok  " if n < 20 else "FAIL", sys.argv[3], n))
sys.exit(0 if n < 20 else 1)
EOF
done

[ "$fail" = 0 ] && echo "all passed" || echo "FAILED (pictures: $OUT/fx/)"
exit "$fail"
