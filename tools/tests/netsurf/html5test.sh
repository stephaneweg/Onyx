#!/bin/sh
# tools/tests/netsurf/html5test.sh -- the HTML5test score (html5test.co, Niels Leenheer's test,
# MIT: github.com/niutech/html5test) of the PC NetSurf. The test is cloned once into
# $OUT/html5test, served by a local HTTP server (its scripts ask for /assets/...), and a small
# page runs its engine and logs the score, each section's points and the failed features
# (console.log, NS_JSDEBUG=1) instead of drawing its results page.
#
#   sh tools/tests/netsurf/html5test.sh [waits]        (default 1500 loop turns)
#
# Output: "HTML5TEST score S / M", "HTML5TEST points ...", "HTML5TEST no <key>" lines
# ($OUT/html5test.log has everything, the scripts' errors too).
set -e
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8124}
waits=${1:-1500}
mkdir -p "$OUT"
H=$OUT/html5test
[ -f "$H/scripts/9/engine.js" ] || git clone -q --depth 1 https://github.com/niutech/html5test "$H"
cat > "$H/onyx-run.html" <<'EOF'
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>html5test run</title>
<script src="/scripts/base.js"></script>
<script src="/scripts/9/engine.js"></script>
<script src="/scripts/9/data.js"></script>
</head><body><p id="out">running</p>
<script>
/* the page normally asks WhichBrowser (a remote service) who the browser is, for its
 * blacklists: nothing is blacklisted here */
var Browsers = { isDevice: function () { return false; }, isOs: function () { return false; },
	isBrowser: function () { return false; }, isType: function () { return false; } };
/* a few tests use "x instanceof C" unguarded: a missing C (a feature missing) would stop the
 * whole run with a ReferenceError; an empty function makes that one test fail instead */
['FileList', 'Blob', 'HTMLDataElement', 'HTMLDataListElement', 'HTMLDialogElement',
 'HTMLFieldSetElement', 'HTMLMeterElement', 'HTMLOutputElement', 'HTMLProgressElement',
 'HTMLSelectElement', 'HTMLTextAreaElement', 'HTMLTimeElement', 'HTMLUnknownElement',
 'ArrayBuffer', 'Promise'].forEach(function (n) {
	if (typeof window[n] == 'undefined') {
		window[n] = function () {};
		console.log('HTML5TEST missing ' + n);
	}
});
function report(r) {
	var m = new Metadata(tests), c = new Calculate(r, m.data);
	console.log('HTML5TEST score ' + c.score + ' / ' + c.maximum);
	console.log('HTML5TEST points ' + c.points);
	var list = String(r.results).split(',');
	for (var i = 0; i < list.length; i++)
		if (/=0$/.test(list[i]))
			console.log('HTML5TEST no ' + list[i].replace(/=0$/, ''));
	document.getElementById('out').textContent = c.score + ' / ' + c.maximum;
}
var done = false, runner = null;
/* html5test.co starts its run once WhichBrowser (a script it loads) is there: after the
 * parse, the style sheets loaded -- here, at the load */
window.addEventListener('load', function () {
try {
	runner = new Test(function (r) { done = true; report(r); },
		function (e) { console.log('HTML5TEST error ' + (e && e.message)); });
	/* a test waiting for ever (an event that never comes): after 20 s its result stays
	 * "no" and the run ends */
	setTimeout(function () {
		if (done || !runner || !runner.backgroundTasks)
			return;
		for (var id in runner.backgroundIds)
			if (runner.backgroundTasks[runner.backgroundIds[id]])
				console.log('HTML5TEST timeout ' + id);
		for (var k = 0; k < runner.backgroundTasks.length; k++)
			runner.backgroundTasks[k] = 0;
	}, 20000);
} catch (e) {
	console.log('HTML5TEST error ' + e.message + ' ' + e.stack);
}
});
</script></body></html>
EOF
make -f $T/host.mk OUT="$OUT/build" -j4 >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
(cd "$H" && exec python3 -m http.server $PORT --bind 127.0.0.1) >"$OUT/html5test-srv.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 1
W=$(i=0; while [ $i -lt "$waits" ]; do printf 'wait;'; i=$((i + 1)); done)
SIM_REALNET=1 SIM_SCREEN=1024x768 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 \
	SIM_ARGS="http://127.0.0.1:$PORT/onyx-run.html" SIM="${W}exit" \
	timeout 600 "$OUT/build/netsurf" >"$OUT/html5test.log" 2>&1 || true
grep "HTML5TEST" "$OUT/html5test.log" | sed 's/^console: //' || echo "no result: $OUT/html5test.log"
