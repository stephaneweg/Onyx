// smoke.js -- jsc's smoke test for the Onyx port (tools/webkit/test-jsc.sh smoke; on the Pi:
// jsc SD:/docs/jsc/smoke.js). Each check throws on a mismatch; the last line is "smoke: ok".
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
let checks = 0;
function eq(what, got, want) {
    checks++;
    if (got !== want)
        throw new Error(what + ": got " + JSON.stringify(got) + ", want " + JSON.stringify(want));
}

// the language
eq("arithmetic", 6 * 7, 42);
eq("closures, arrays", [1, 2, 3].map(x => x * x).join(","), "1,4,9");
eq("JSON", JSON.stringify({a: 1, b: [true, null]}), '{"a":1,"b":[true,null]}');
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
eq("recursion", fib(22), 17711);
class A { #x = 41; get x() { return this.#x + 1; } }
eq("classes, private fields", new A().x, 42);
eq("BigInt", (2n ** 64n).toString(), "18446744073709551616");
eq("generators", [...(function* () { yield 1; yield* [2, 3]; })()].length, 3);
eq("destructuring, spread", (({a, ...r}) => a + Object.keys(r).length)({a: 1, b: 2, c: 3}), 3);
eq("regexp", /(\d+)-(\d+)/.exec("10-20").slice(1).join("+"), "10+20");
eq("regexp, named groups, unicode", "été 2026".replace(/(?<y>\p{Nd}+)/u, "[$<y>]"), "été [2026]");
eq("typed arrays", new Uint8Array(new Float64Array([1.5]).buffer)[7], 0x3f);
eq("toSorted, at", [3, 1, 2].toSorted().at(-1), 3);
eq("exceptions", (() => { try { null.x; } catch (e) { return e instanceof TypeError; } })(), true);
eq("Math", Math.round(Math.hypot(3, 4) * Math.cos(0)), 5);
eq("parseFloat, toFixed", parseFloat("3.14159").toFixed(2), "3.14");
eq("Date (UTC)", new Date(Date.UTC(2026, 9, 2, 12, 0, 0)).toISOString(), "2026-10-02T12:00:00.000Z");
eq("WeakRef, Symbol", typeof new WeakRef({}).deref() + typeof Symbol.iterator, "objectsymbol");
eq("Proxy, Reflect", new Proxy({}, {get: (t, k) => "p:" + String(k)}).z + Reflect.ownKeys({a: 1}).length, "p:z1");

// ICU (the data linked into the program)
eq("normalize", "été".normalize("NFD").length, 5);
eq("localeCompare", ["z", "é", "e", "a"].sort((a, b) => a.localeCompare(b, "fr")).join(""), "aeéz");
eq("Intl.NumberFormat", new Intl.NumberFormat("en-US", {style: "currency", currency: "USD"}).format(1234567.891), "$1,234,567.89");
eq("Intl.NumberFormat fr", new Intl.NumberFormat("fr-FR").format(1234567.5).replace(/\s/g, " "), "1 234 567,5");
eq("Intl.DateTimeFormat", new Intl.DateTimeFormat("en-GB", {dateStyle: "full", timeZone: "UTC"}).format(new Date(Date.UTC(2026, 9, 2))), "Friday, 2 October 2026");
eq("Intl.PluralRules", new Intl.PluralRules("en", {type: "ordinal"}).select(2), "two");
eq("Intl.Segmenter", [...new Intl.Segmenter("ja", {granularity: "word"}).segment("今日は晴れです")].length > 2, true);
eq("toUpperCase (tr)", "i".toLocaleUpperCase("tr"), "İ");

// memory: the collector on a few hundred thousand objects
let m = new Map();
for (let i = 0; i < 200000; i++)
    m.set("k" + i, {i});
eq("Map", m.size, 200000);
m = null;
gc();
let a = [];
for (let i = 0; i < 50; i++)
    a.push(new Array(20000).fill(i));
eq("arrays", a[49][19999], 49);
a = null;
gc();

// promises, async functions: the microtask queue runs before the shell exits
let order = [];
Promise.resolve(7).then(v => order.push("then " + v));
(async () => { await null; order.push("async"); })();
drainMicrotasks();
eq("microtasks", order.join(", "), "then 7, async");

print("smoke: ok (" + checks + " checks)");
