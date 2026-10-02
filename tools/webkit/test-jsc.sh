#!/bin/sh
# test-jsc.sh -- jsc (tools/webkit/build-jsc.sh) on the posixsim bench: relinked against the
# bench's libonyxposix + fakekapi (tools/tests/posixsim/run.sh), then run under qemu-user on
#   smoke    tools/webkit/smoke.js (the language, ICU, the collector, microtasks)
#   es6      WebKit's JSTests/es6 (es6.yaml: 595 tests expected to pass, 10 to fail)
#   stress   a part of WebKit's JSTests/stress: the tests the harness runs with its default
#            options (no //@ directive, or //@ runDefault alone), one in STRESS_STEP; the few
#            that cannot pass in this port are expected to fail (STRESS_XFAIL below)
#   wasm     WebKit's JSTests/wasm (the LLInt build: WebAssembly in its interpreter): the
#            directories its harness runs as "the WebAssembly suite" (WASM_DIRS below), each test
#            as a module (-m) from its directory, with its //@ requireOptions; a test with a
#            //@ skip or another directive of its own is left out
#   bench    tools/webkit/bench.js (timings under qemu: to compare the interpreters, not the Pi)
#
#   sh tools/webkit/test-jsc.sh [smoke] [es6] [stress] [wasm] [bench]   # default: smoke es6
#   INTERP=cloop sh tools/webkit/test-jsc.sh ...                    # the C_LOOP build's jsc
#   STRESS_STEP=1 sh tools/webkit/test-jsc.sh stress                # every such test (default 10)
#
# Variables: WEBKIT_DIR, BUILD (as build-jsc.sh), JOBS (default nproc), TIMEOUT (seconds a test,
# default 600: qemu
# is slow), OUT (the logs, default $POSIXSIM_ROOT/jsc-<interp>). A test passes when jsc exits
# with 0 (an uncaught exception: 3) in time. The failures' names are in $OUT/failed.txt, each
# test's output in $OUT/log/. The exit status is the number of unexpected results.
# The bench is not the Pi (its kernel, its memory, its timing): the Pi is the reference.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../.." && pwd)
if [ -z "${WEBKIT_DIR:-}" ]; then
	if [ -d /home/user ]; then WEBKIT_DIR=/home/user/webkit; else WEBKIT_DIR=$HOME/webkit; fi
fi
: "${INTERP:=llint}"
: "${BUILD:=$WEBKIT_DIR-build/jsc-$INTERP}"
: "${ONYX_SYSROOT:=$ONYX/out/sysroot-onyx}"
: "${JOBS:=$(nproc)}"
: "${TIMEOUT:=600}"
: "${STRESS_STEP:=10}"
: "${POSIXSIM_ROOT:=/tmp/posixsim}"
: "${POSIXSIM_QEMU:=$(command -v qemu-aarch64-static || command -v qemu-aarch64)}"
: "${OUT:=$POSIXSIM_ROOT/jsc-$INTERP}"
export POSIXSIM_ROOT POSIXSIM_QEMU
PATH=/opt/toolchains/aarch64-onyx-elf-14.2/bin:$PATH
export PATH
suites=${*:-smoke es6}
R=$POSIXSIM_ROOT
T=$WEBKIT_DIR/JSTests
NAME=jsc-$INTERP
# the options WebKit's harness (Tools/Scripts/run-jsc-stress-tests: BASE_OPTIONS) gives every test
# (and its time zone: TZ=US/Pacific, which the Intl tests expect)
OPTS="--validateOptions=true --useFTLJIT=false --useFunctionDotArguments=true --validateExceptionChecks=true --useDollarVM=true --maxPerThreadStackUsage=1572864"
# stress tests that cannot pass here: a locale left out of the filtered ICU data
# (tools/ports/icu/data-filter.json: Swahili, Ewe); with C_LOOP, the two that use WebAssembly
STRESS_XFAIL="intl-relativetimeformat.js string-localeCompare.js"
[ "$INTERP" = cloop ] && STRESS_XFAIL="$STRESS_XFAIL structured-clone.js wasm-gc-structureid-cast-optimization.js"
# JSTests/wasm: what wasm.yaml runs with runWebAssemblySuite (fuzz and v8 left out: other harnesses)
WASM_DIRS="stress js-api noJIT function-tests references function-references gc regress self-test branch-hints extended-const"
# WebAssembly tests that cannot pass in this port:
# - SIMD: the parser only has it with the B3 JIT (ENABLE(B3_JIT))
WASM_XFAIL_SIMD="gc/bulk-array-element-types.js gc/struct-new-default-v128.js stress/inline-wasm-simd-into-non-simd.js stress/simd-multimemory.js extended-const/extended-const.js"
# - shared memories and "signaling" (fast) memories: they need the fault handler, which Onyx
#   cannot have (no signal for a fault): memories are bounds-checked explicitly
WASM_XFAIL_SHARED="references/memory_copy_shared.js references/memory_fill_shared.js stress/atomic-multimemory.js stress/multimemory-shared-grow-refreshes-only-its-own-slots.js stress/shared-memory-errors.js stress/shared-wasm-memory-buffer.js stress/wasm-shared-memory-growable.js stress/tail-call-unused-pins.js js-api/memory-toFixedLengthBuffer.js js-api/memory-toResizableBuffer.js js-api/memory64-js-api.js js-api/test_memory.js"
: "${WASM_XFAIL:=$WASM_XFAIL_SIMD $WASM_XFAIL_SHARED}"

[ -f "$BUILD/lib/libJavaScriptCore.a" ] || { echo "test-jsc.sh: no build in $BUILD: sh tools/webkit/build-jsc.sh" >&2; exit 2; }
[ -x "$POSIXSIM_QEMU" ] || { echo "test-jsc.sh: no qemu-aarch64" >&2; exit 2; }

# ---- relink for the bench: the shell's objects and the libraries, as the build's link line ----
objs=$(cd "$BUILD" && ninja -t commands jsc | tail -n 1 | tr ' ' '\n' | grep 'jsc\.dir.*\.o$' | sed "s|^|$BUILD/|" | tr '\n' ' ')
[ -n "$objs" ] || { echo "test-jsc.sh: jsc's objects not found in $BUILD" >&2; exit 2; }
S=$ONYX_SYSROOT
BUILD_ONLY=1 PROG=none NAME=$NAME SIM_CXX=1 PREFIX=aarch64-onyx-elf- \
	OBJS="$objs $BUILD/lib/libJavaScriptCore.a $BUILD/lib/libWTF.a $S/lib/libicui18n.a $S/lib/libicuuc.a $S/lib/libicudata.a $BUILD/lib/libbmalloc.a" \
	sh "$ONYX/tools/tests/posixsim/run.sh" || exit 2
echo "test-jsc.sh: $NAME ($(wc -c < "$R/SD/bin/$NAME") bytes) on the bench, $JOBS jobs"

rm -rf "$OUT"
mkdir -p "$OUT/log" "$R/SD/jstests"
: > "$OUT/results.txt"
jsc () { POSIXSIM_ARGV0="SD:/bin/$NAME" "$POSIXSIM_QEMU" "$R/SD/bin/$NAME" "$@"; }

# one test: <suite> <file> <pass|fail> [jsc options] -> a line
# "<PASS|FAIL|XFAIL|XPASS|TIMEOUT> <suite>/<file>" (the suite is its directory in SD:/jstests)
cat > "$OUT/one.sh" <<EOF
#!/bin/sh
suite=\$1; f=\$2; want=\$3; shift 3
log="$OUT/log/\$(echo "\$suite-\$f" | tr / _).log"
TZ=US/Pacific POSIXSIM_CWD="SD:/jstests/\$suite" POSIXSIM_ARGV0="SD:/bin/$NAME" \\
	timeout $TIMEOUT "$POSIXSIM_QEMU" "$R/SD/bin/$NAME" $OPTS "\$@" "\$f" > "\$log" 2>&1 < /dev/null
st=\$?
if [ \$st -eq 124 ]; then r=TIMEOUT
elif [ \$st -eq 0 ]; then [ \$want = pass ] && r=PASS || r=XPASS
else [ \$want = pass ] && r=FAIL || r=XFAIL
fi
[ \$r = PASS ] || [ \$r = XFAIL ] && rm -f "\$log" || echo "[exit \$st]" >> "\$log"
echo "\$r \$suite/\$f"
EOF

bad=0
summary () {	# <suite>
	p=$(grep -c "^PASS $1/" "$OUT/results.txt"); xf=$(grep -c "^XFAIL $1/" "$OUT/results.txt")
	f=$(grep -c "^FAIL $1/" "$OUT/results.txt"); xp=$(grep -c "^XPASS $1/" "$OUT/results.txt")
	t=$(grep -c "^TIMEOUT $1/" "$OUT/results.txt")
	echo "$1: $p passed, $xf failed as expected, $f FAILED, $xp passed unexpectedly, $t timed out"
	bad=$((bad + f + t))
}

for s in $suites; do
	case $s in
	smoke)
		mkdir -p "$R/SD/docs/jsc"
		cp "$HERE/smoke.js" "$HERE/bench.js" "$R/SD/docs/jsc/"
		out=$(jsc SD:/docs/jsc/smoke.js 2>&1); st=$?
		echo "$out" | tail -n 3
		case "$out" in *"smoke: ok"*) [ $st -eq 0 ] && echo "PASS  smoke" || { echo "FAIL  smoke (exit $st)"; bad=$((bad + 1)); };;
		*) echo "FAIL  smoke (exit $st)"; bad=$((bad + 1));; esac
		out=$(jsc -e 'print(6 * 7)' 2>&1)
		[ "$out" = 42 ] && echo "PASS  jsc -e" || { echo "FAIL  jsc -e ($out)"; bad=$((bad + 1)); }
		out=$(jsc -e 'throw new Error("x")' 2>&1); st=$?
		[ $st -eq 3 ] && echo "PASS  an uncaught exception: exit 3" || { echo "FAIL  an uncaught exception (exit $st)"; bad=$((bad + 1)); }
		;;
	es6)
		[ -d "$T/es6" ] || { echo "test-jsc.sh: no $T/es6 (TESTS=1 sh tools/webkit/fetch.sh)" >&2; exit 2; }
		rm -rf "$R/SD/jstests/es6"; cp -r "$T/es6" "$R/SD/jstests/es6"
		awk '/^- path: es6\// { f = substr($3, 5) } /cmd: runES6/ { print f, ($3 == ":normal" ? "pass" : "fail") }' "$T/es6.yaml" > "$OUT/es6.list"
		start=$(date +%s)
		sed 's/^/es6 /' "$OUT/es6.list" | xargs -P "$JOBS" -L 1 sh "$OUT/one.sh" >> "$OUT/results.txt"
		echo "es6: $(wc -l < "$OUT/es6.list") tests in $(( $(date +%s) - start )) s"
		summary es6
		;;
	stress)
		[ -d "$T/stress" ] || { echo "test-jsc.sh: no $T/stress (TESTS=1 sh tools/webkit/fetch.sh)" >&2; exit 2; }
		rm -rf "$R/SD/jstests/stress"; cp -r "$T/stress" "$R/SD/jstests/stress"
		# the tests run with the default options only; one in STRESS_STEP (sorted: the same ones each time)
		( cd "$T/stress" && for f in *.js; do
			d=$(grep '^//@' "$f" | tr -d ' \r')
			case $d in ""|"//@runDefault") echo "$f";; esac
		  done ) | sort | awk -v n="$STRESS_STEP" -v xf=" $STRESS_XFAIL " 'NR % n == 0 || n == 1 { print "stress", $1, (index(xf, " " $1 " ") ? "fail" : "pass") }' > "$OUT/stress.list"
		start=$(date +%s)
		xargs -P "$JOBS" -L 1 sh "$OUT/one.sh" < "$OUT/stress.list" >> "$OUT/results.txt"
		echo "stress: $(wc -l < "$OUT/stress.list") tests (one in $STRESS_STEP) in $(( $(date +%s) - start )) s"
		summary stress
		;;
	wasm)
		[ -d "$T/wasm/stress" ] || { echo "test-jsc.sh: no $T/wasm (TESTS=1 sh tools/webkit/fetch.sh)" >&2; exit 2; }
		[ "$INTERP" = llint ] || { echo "test-jsc.sh: wasm: the LLInt build only (C_LOOP has no WebAssembly)" >&2; exit 2; }
		# the harness's modules (assert.js, Builder.js, wasm.json...) beside the directories run
		rm -rf "$R/SD/jstests/wasm"; mkdir -p "$R/SD/jstests/wasm"
		for f in "$T"/wasm/*; do [ -f "$f" ] && cp "$f" "$R/SD/jstests/wasm/"; done
		for d in $WASM_DIRS; do
			[ -d "$T/wasm/$d" ] || continue
			cp -r "$T/wasm/$d" "$R/SD/jstests/wasm/$d"
		done
		# files some tests load: the .wasm modules, v8's module builder
		[ -d "$T/wasm/modules" ] && cp -r "$T/wasm/modules" "$R/SD/jstests/wasm/modules"
		[ -d "$T/wasm/v8/resources" ] && mkdir -p "$R/SD/jstests/wasm/v8" && cp -r "$T/wasm/v8/resources" "$R/SD/jstests/wasm/v8/resources"
		for d in $WASM_DIRS; do
			[ -d "$T/wasm/$d" ] || continue
			( cd "$T/wasm/$d" && for f in *.js; do
				[ -f "$f" ] || continue
				grep -q '^//@ *skip' "$f" && continue
				[ -n "$(grep '^//@' "$f" | grep -v '^//@ *requireOptions')" ] && continue
				ro=$(grep '^//@ *requireOptions' "$f" | sed 's/^[^(]*(//; s/)[^)]*$//' | tr -d '"\r' | tr ',' ' ' | tr '\n' ' ')
				case " $WASM_XFAIL " in *" $d/$f "*) w=fail;; *) w=pass;; esac
				echo "wasm/$d $f $w -m $ro"
			  done )
		done | sed 's/ *$//' > "$OUT/wasm.list"	# (xargs -L joins a line ending with a blank to the next)
		start=$(date +%s)
		xargs -P "$JOBS" -L 1 sh "$OUT/one.sh" < "$OUT/wasm.list" >> "$OUT/results.txt"
		echo "wasm: $(wc -l < "$OUT/wasm.list") tests in $(( $(date +%s) - start )) s"
		summary wasm
		;;
	bench)
		mkdir -p "$R/SD/docs/jsc"
		cp "$HERE/bench.js" "$R/SD/docs/jsc/"
		echo "bench ($INTERP, under qemu):"
		jsc SD:/docs/jsc/bench.js || bad=$((bad + 1))
		;;
	*) echo "test-jsc.sh: unknown suite $s (smoke es6 stress wasm bench)" >&2; exit 2;;
	esac
done
grep -E '^(FAIL|TIMEOUT|XPASS) ' "$OUT/results.txt" | sort > "$OUT/failed.txt"
[ -s "$OUT/failed.txt" ] && echo "test-jsc.sh: the unexpected results: $OUT/failed.txt (logs in $OUT/log)"
echo "test-jsc.sh: $bad failed"
exit $bad
