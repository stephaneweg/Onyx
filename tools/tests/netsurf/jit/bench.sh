#!/bin/sh
# tools/tests/netsurf/jit/bench.sh -- QuickJS's speed without the browser: Octane's CPU
# benchmarks (Richards, DeltaBlue, RayTrace, NavierStokes, Crypto, Splay, EarleyBoyer) and
# React 18's server rendering of 1000 rows, run by qjsrun (build.sh). The sources are fetched
# once into $CACHE (Octane from GitHub, React from npm; not in the repo).
#
#   sh tools/tests/netsurf/jit/bench.sh [qjsrun binary] [benchmark...]      Octane's scores
#   COUNT=1 sh tools/tests/netsurf/jit/bench.sh ...     instructions (callgrind), fixed work:
#       each Octane benchmark run 3 times (octane-fixed.js) -- the measure to compare builds
#       with on this shared machine, where the scores move by 30 %
#   BIN="qemu-aarch64 /tmp/qjsrun/qjsrun-a64" sh tools/tests/netsurf/jit/bench.sh
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${1:-${BIN:-/tmp/qjsrun/qjsrun}}
[ $# -gt 0 ] && shift
CACHE=${CACHE:-/tmp/qjsbench}
mkdir -p "$CACHE"
if [ ! -f "$CACHE/octane/base.js" ]; then
	git clone -q --depth 1 https://github.com/chromium/octane "$CACHE/octane" || exit 1
fi
if [ ! -f "$CACHE/react/react.production.min.js" ]; then
	mkdir -p "$CACHE/react" && (cd "$CACHE/react" && npm pack -q react@18.3.1 react-dom@18.3.1 >/dev/null &&
		mkdir -p r d && tar xzf react-18.3.1.tgz -C r && tar xzf react-dom-18.3.1.tgz -C d &&
		cp r/package/umd/react.production.min.js d/package/umd/react-dom-server-legacy.browser.production.min.js .) || exit 1
fi
LIST=${*:-richards deltablue raytrace navier-stokes crypto splay earley-boyer react}
run() {	# run <name> <files...>
	name=$1
	shift
	if [ -n "$COUNT" ]; then
		valgrind --tool=callgrind --callgrind-out-file=/dev/null $BIN "$@" 2>&1 >/dev/null |
			sed -n "s/.*Collected : \([0-9]*\).*/$name: \1 instructions/p"
	else
		$BIN "$@" | grep -v '^Score'
	fi
}
for b in $LIST; do
	if [ "$b" = react ]; then
		run ReactSSR "$CACHE/react/react.production.min.js" \
			"$CACHE/react/react-dom-server-legacy.browser.production.min.js" "$HERE/react-ssr.js"
	elif [ -n "$COUNT" ]; then
		run "$b" "$CACHE/octane/base.js" "$CACHE/octane/$b.js" "$HERE/octane-fixed.js"
	else
		run "$b" "$CACHE/octane/base.js" "$CACHE/octane/$b.js" "$HERE/octane-run.js"
	fi
done
