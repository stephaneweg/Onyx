// bench.js -- a few timings of jsc's interpreter (tools/webkit/test-jsc.sh bench; on the Pi:
// jsc SD:/docs/jsc/bench.js): C_LOOP against the LLInt, the PC bench against the Pi. Not a
// benchmark suite: orders of magnitude.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
function time(name, f) {
    let t = Date.now();
    let r = f();
    print(name.padEnd(28) + String(Date.now() - t).padStart(7) + " ms   " + r);
}
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
time("fib(30): calls", () => fib(30));
time("loop: 20M additions", () => { let s = 0; for (let i = 0; i < 20000000; i++) s = (s + i) | 0; return s; });
time("objects: 1M allocations", () => { let o; for (let i = 0; i < 1000000; i++) o = {a: i, b: [i]}; return o.b[0]; });
time("strings: 300k concat/split", () => { let p = []; for (let i = 0; i < 300000; i++) p.push("k" + i); return p.join(",").split(",").length; });
time("array sort: 300k numbers", () => { let a = []; let x = 1; for (let i = 0; i < 300000; i++) { x = (x * 1103515245 + 12345) & 0x7fffffff; a.push(x); } a.sort((p, q) => p - q); return a[0]; });
time("regexp: 100k matches", () => { let n = 0; let re = /(\d+)-([a-z]+)/; for (let i = 0; i < 100000; i++) if (re.test(i + "-abc")) n++; return n; });
time("Map: 500k set/get", () => { let m = new Map(); for (let i = 0; i < 500000; i++) m.set(i, i); let s = 0; for (let i = 0; i < 500000; i++) s += m.get(i); return s; });
time("typed array: 5M doubles", () => { let f = new Float64Array(1000000); let s = 0; for (let k = 0; k < 5; k++) for (let i = 0; i < f.length; i++) { f[i] = i * 0.5; s += f[i]; } return s; });
time("JSON: 20 x 200 KB", () => { let o = []; for (let i = 0; i < 5000; i++) o.push({id: i, name: "n" + i, v: [i, i + 1]}); let n = 0; for (let i = 0; i < 20; i++) n += JSON.parse(JSON.stringify(o)).length; return n; });
