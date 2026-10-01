#!/bin/sh
# tools/tests/netsurf/wasm/build.sh -- bench.c compiled to WebAssembly for the bench
# (pages/js-wasm-bench.wasm: clang's wasm32 target and wasm-ld, LLVM 18 here), then the
# same functions natively and in Node (V8) for the comparison with NetSurf's wasm3 timings
# (pages/js-wasm.html logs "wasm timing ..." : jstest.sh prints it).
#
#   sh tools/tests/netsurf/wasm/build.sh
cd "$(dirname "$0")"
clang --target=wasm32 -O2 -fno-builtin -nostdlib -Wl,--no-entry -o ../pages/js-wasm-bench.wasm bench.c || exit 1
ls -l ../pages/js-wasm-bench.wasm
T=${TMPDIR:-/tmp}
cc -O2 -DNATIVE -o "$T/wasm-bench-native" bench.c && "$T/wasm-bench-native"
command -v node >/dev/null && node -e '
const fs = require("fs");
const b = fs.readFileSync("../pages/js-wasm-bench.wasm");
WebAssembly.instantiate(b, { env: { report() {} } }).then(({ instance: { exports: e } }) => {
	const p = e.buffer_in();
	new Uint8Array(e.memory.buffer).set([97, 98, 99], p);
	let t = performance.now(); e.sha256(p, 3, 20000); const a = performance.now() - t;
	t = performance.now(); const pr = e.sieve(2000000); const b2 = performance.now() - t;
	t = performance.now(); e.fib(27); const c = performance.now() - t;
	t = performance.now(); e.mandel(320, 240, 256); const d = performance.now() - t;
	console.log("node (V8) sha256x20000 " + a.toFixed(1) + " ms, sieve(2e6) " + b2.toFixed(1) +
		" ms (" + pr + "), fib(27) " + c.toFixed(1) + " ms, mandel 320x240x256 " + d.toFixed(1) + " ms");
});'
