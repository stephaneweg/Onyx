#!/bin/sh
# tools/tests/netsurf/layoutdiff.sh -- where NetSurf's layout differs from Chromium's, element
# by element: the same script (layoutdiff.js) measures every displayed element's box in both
# (NetSurf: NS_INJECT then F5; Chromium: playwright, NetSurf's User-Agent, the same page width)
# and the boxes are compared by their element's path, in document order.
#
#   sh tools/tests/netsurf/layoutdiff.sh <url | file> [page width] [waits] [tolerance px]
#
# Prints the first differing elements (NetSurf's box, then Chromium's), and a count. The page
# width is the viewport's (default 720: site.sh's 800x1000 window). OUT as the others. REL=1
# compares each box's position relative to its parent element's (and its size) instead of the
# page's: a box placed too low does not make all that follow it differ.
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
url=$1; pw=${2:-720}; waits=${3:-300}; tol=${4:-2}
case "$url" in *://*) ;; *) url="file://$(realpath "$url")";; esac
mkdir -p "$OUT"
# LD_COMPARE=1: compare the last run's measures again (e.g. with REL=1), without running
if [ -z "$LD_COMPARE" ]; then
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
W=$(i=0; while [ $i -lt "$waits" ]; do printf 'wait;'; i=$((i + 1)); done)
# the window: the page width + the frame and the scroll bar (82 px), 1000 high
SIM_REALNET=1 NS_JSDEBUG=1 NS_INJECT=$T/layoutdiff.js SIM_SCREEN=$((pw + 82))x1000 SIM_SLEEP=1 SIM_POS=0,0 \
	SIM_ARGS="$url" SIM="${W}key 276;wait;wait;exit" timeout "${TMO:-300}" "$OUT/build/netsurf" 2>&1 |
	sed 's/^console: //' | grep '^LB ' > "$OUT/ld-ns.txt"
UA=$(sed -n '/return "Mozilla/,/;/p' third_party/netsurf/utils/useragent.c | grep -o '"[^"]*"' | tr -d '"' | tr -d '\n')
NODE_PATH=$(npm root -g) UA="$UA" URL="$url" PW="$pw" JS="$T/layoutdiff.js" node -e '
const { chromium } = require("playwright");
(async () => {
	const b = await chromium.launch({ executablePath: process.env.CHROME || undefined });
	const p = await b.newPage({ userAgent: process.env.UA, viewport: { width: +process.env.PW, height: 790 },
		locale: "fr-FR", ignoreHTTPSErrors: false });
	const lines = [];
	p.on("console", m => { for (const l of m.text().split("\n")) if (l.startsWith("LB ")) lines.push(l); });
	await p.goto(process.env.URL, { waitUntil: "load", timeout: 60000 });
	await p.waitForTimeout(2000);
	await p.evaluate(require("fs").readFileSync(process.env.JS, "utf8"));
	await p.waitForTimeout(200);
	console.log(lines.join("\n"));
	await b.close();
})();' > "$OUT/ld-ch.txt" 2>"$OUT/ld-ch.err"
fi
python3 - "$OUT/ld-ns.txt" "$OUT/ld-ch.txt" "$tol" "${REL:-0}" <<'PY'
import sys
rel = sys.argv[4] == '1'
def load(f):
    d = {}; order = []
    for l in open(f):
        p = l.split()
        if len(p) < 6: continue
        k = p[1]; v = tuple(map(int, p[2:6]))
        if rel and len(p) >= 8: v = (int(p[6]), int(p[7])) + v[2:]
        n = 1
        while (k if n == 1 else k + '~' + str(n)) in d: n += 1
        k = k if n == 1 else k + '~' + str(n)
        d[k] = v; order.append(k)
    return d, order
ns, _ = load(sys.argv[1]); ch, order = load(sys.argv[2]); tol = int(sys.argv[3])
bad = 0; shown = 0; missing = 0
for k in order:
    if k not in ns:
        missing += 1
        if shown < 60: print('MISSING in NetSurf  %-70s chrome %s' % (k[-70:], ch[k])); shown += 1
        continue
    a, b = ns[k], ch[k]
    if any(abs(x - y) > tol for x, y in zip(a, b)):
        bad += 1
        if shown < 60: print('%-80s ns %-22s ch %s' % (k[-80:], a, b)); shown += 1
print('elements: chromium %d, netsurf %d, differing %d, missing %d' % (len(ch), len(ns), bad, missing))
PY
