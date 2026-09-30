#!/bin/sh
# tools/tests/netsurf/css3test.sh -- css3test.com (Lea Verou's CSS feature test) on the PC bench:
# the score NetSurf gets, per specification, and (optionally) Chromium's on the same copy.
#
#   sh tools/tests/netsurf/css3test.sh [netsurf|chrome|both] [waits]
#
# The site is not in the repository (its licence): it is fetched into $OUT/css3test (default
# OUT /tmp/nsbench) -- the site's own sources (git branch v1 of github.com/LeaVerou/css3test,
# what css3test.com serves: index.html, csstest.js, supports.js, tests.js and its ~154 test
# modules), bliss.js from cdnjs beside it. The copy run is $OUT/css3test/onyx-run.html: the
# page without its trackers / ads, plus a reporter script that prints, once the tests have run,
# "CSS3TEST SCORE <score> <passed>/<total>", a line per spec ("CSS3TEST SPEC <score> <id>")
# and a line per test not passed ("CSS3TEST FAIL <spec> <group> <feature> :: <test>"). The
# reports: $OUT/css3test-netsurf.txt, $OUT/css3test-chrome.txt; with "both" a per-spec table
# (NetSurf vs Chromium) follows.
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
what=${1:-netsurf}; waits=${2:-600}
C=$OUT/css3test
mkdir -p "$OUT"

if [ ! -f "$C/tests.js" ]; then
	rm -rf "$C"
	git clone -q --depth 1 -b v1 https://github.com/LeaVerou/css3test.git "$C" ||
		{ echo "css3test: fetch failed"; exit 1; }
fi
[ -f "$C/bliss.js" ] || curl -sS -o "$C/bliss.js" \
	https://cdnjs.cloudflare.com/ajax/libs/blissfuljs/1.0.3/bliss.js || { echo "bliss.js: fetch failed"; exit 1; }

# the page to run: no trackers, ads or RUM; the local bliss.js; the reporter
python3 - "$C" <<'EOF'
import re, sys
c = sys.argv[1]
h = open(c + "/index.html", encoding="utf-8").read()
h = re.sub(r'<script[^>]*(google-analytics|carbonads|netlify)[^>]*>\s*</script>', '', h)
h = h.replace('https://cdnjs.cloudflare.com/ajax/libs/blissfuljs/1.0.3/bliss.js', 'bliss.js')
rep = r'''<script>
(function () {
	function report() {
		var s = document.querySelector('#score');
		if (!s || !s.textContent) { setTimeout(report, 200); return; }
		var out = [];
		out.push('CSS3TEST SCORE ' + s.textContent + ' ' + document.querySelector('#passedTests').textContent +
			'/' + document.querySelector('#totalTests').textContent);
		var specs = document.querySelectorAll('#all > section');
		for (var i = 0; i < specs.length; i++) {
			var sp = specs[i], sc = sp.querySelector('h1 > .score');
			var n = 0, p = 0, fails = [];
			var groups = sp.querySelectorAll('section.tests');
			for (var g = 0; g < groups.length; g++) {
				var gname = groups[g].querySelector('h1').textContent;
				var dets = groups[g].querySelectorAll('details');
				for (var d = 0; d < dets.length; d++) {
					var feat = dets[d].querySelector('summary').firstChild.textContent;
					var lis = dets[d].querySelectorAll('ul > li');
					for (var l = 0; l < lis.length; l++) {
						n++;
						if (lis[l].className === 'pass') p++;
						else fails.push('CSS3TEST FAIL ' + sp.id + ' ' + gname + ' ' + feat + ' :: ' +
							lis[l].firstChild.textContent + ' [' + lis[l].className + ']');
					}
				}
			}
			out.push('CSS3TEST SPEC ' + (sc ? sc.textContent : '?') + ' ' + p + '/' + n + ' ' + sp.id);
			out = out.concat(fails);
		}
		out.push('CSS3TEST END');
		for (var k = 0; k < out.length; k++) console.log(out[k]);
		var pre = document.createElement('pre');
		pre.id = 'onyxreport';
		pre.textContent = out.join('\n');
		document.body.appendChild(pre);
	}
	window.addEventListener('load', function () { setTimeout(report, 100); });
})();
</script>
'''
h = h.replace('</body>', rep + '</body>')
open(c + "/onyx-run.html", "w", encoding="utf-8").write(h)
EOF

run_netsurf() {
	make -f $T/host.mk OUT="$OUT/build" -j4 >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
	W=$(i=0; while [ $i -lt "$waits" ]; do printf 'wait;'; i=$((i + 1)); done)
	t0=$(date +%s)
	SIM_SCREEN=1600x1000 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 SIM_ARGS="file://$(realpath "$C/onyx-run.html")" \
		SIM="${W}exit" timeout 600 "$OUT/build/netsurf" >"$OUT/css3test-netsurf.log" 2>&1
	sed -n 's/^console: \(CSS3TEST .*\)/\1/p' "$OUT/css3test-netsurf.log" >"$OUT/css3test-netsurf.txt"
	grep -q "CSS3TEST END" "$OUT/css3test-netsurf.txt" ||
		echo "netsurf: no complete report (more waits?) -- $OUT/css3test-netsurf.log"
	echo "netsurf ($(($(date +%s) - t0)) s): $(grep '^CSS3TEST SCORE' "$OUT/css3test-netsurf.txt")"
}
run_chrome() {
	CHROME=${CHROME:-$(ls /opt/pw-browsers/chromium_headless_shell-*/chrome-linux/headless_shell 2>/dev/null | head -1)}
	[ -x "$CHROME" ] || { echo "no Chromium (set CHROME)"; exit 1; }
	# file:// modules need the same origin: serve the copy over a local HTTP server
	port=$((20000 + $$ % 10000))
	(cd "$C" && exec python3 -m http.server "$port" --bind 127.0.0.1) >/dev/null 2>&1 &
	srv=$!
	sleep 1
	"$CHROME" --headless --no-sandbox --virtual-time-budget=30000 --dump-dom \
		"http://127.0.0.1:$port/onyx-run.html" 2>/dev/null |
		python3 -c 'import sys,re,html; m=re.search(r"<pre id=\"onyxreport\">(.*?)</pre>", sys.stdin.read(), re.S); print(html.unescape(m.group(1)) if m else "")' \
		>"$OUT/css3test-chrome.txt"
	kill $srv 2>/dev/null
	echo "chrome: $(grep '^CSS3TEST SCORE' "$OUT/css3test-chrome.txt")"
}

case "$what" in
netsurf) run_netsurf ;;
chrome) run_chrome ;;
both) run_netsurf; run_chrome
	python3 - "$OUT/css3test-netsurf.txt" "$OUT/css3test-chrome.txt" <<'EOF'
import sys
def load(f):
    d = {}
    for l in open(f):
        if l.startswith('CSS3TEST SPEC '):
            _, _, sc, pn, sid = l.split()
            d[sid] = (sc, pn)
    return d
a, b = load(sys.argv[1]), load(sys.argv[2])
print('%-28s %14s %14s' % ('spec', 'netsurf', 'chrome'))
for k in sorted(set(a) | set(b)):
    x = a.get(k, ('-', '-')); y = b.get(k, ('-', '-'))
    print('%-28s %5s %8s %5s %8s' % (k, x[0], x[1], y[0], y[1]))
EOF
	;;
*) echo "usage: css3test.sh [netsurf|chrome|both] [waits]"; exit 1 ;;
esac
