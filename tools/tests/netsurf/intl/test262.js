#!/usr/bin/env node
/*
 * tools/tests/netsurf/intl/test262.js -- runs test262's intl402 tests in qjsintl (QuickJS + NetSurf's
 * Intl): the pass rate by directory.
 *
 *   git clone --depth 1 --filter=blob:none --sparse https://github.com/tc39/test262 T
 *   (cd T && git sparse-checkout set harness test/intl402)
 *   node tools/tests/netsurf/intl/test262.js <qjsintl> <T> [dir-filter] [-v] [-eager]
 *
 * Skipped: the features QuickJS or intl.js has not got (Temporal, DurationFormat, ...).
 * -eager: Intl's members are read first (a page's first use does it; before that they are
 * accessors -- the property-descriptor tests fail on them).
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');
const os = require('os');

const [qjs, root] = process.argv.slice(2);
const filter = process.argv[4] && !process.argv[4].startsWith('-') ? process.argv[4] : '';
const verbose = process.argv.includes('-v');
const eager = process.argv.includes('-eager');
const SKIP_FEATURES = ['Temporal', 'Intl.DurationFormat', 'Intl.Era-monthcode', 'ShadowRealm', 'cross-realm',
	'Intl.NumberFormat-v3-precision', 'IsHTMLDDA', 'Atomics', 'SharedArrayBuffer', 'Intl.Locale-info-ext',
	'import-defer', 'source-phase-imports', 'explicit-resource-management', 'Array.fromAsync'];

function files(dir) {
	let out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) out = out.concat(files(p));
		else if (e.name.endsWith('.js') && !e.name.includes('_FIXTURE')) out.push(p);
	}
	return out;
}
function meta(src) {
	const m = /\/\*---([\s\S]*?)---\*\//.exec(src);
	const y = m ? m[1] : '';
	const list = (k) => {
		const r = new RegExp('^' + k + ':\\s*\\[([^\\]]*)\\]', 'm').exec(y);
		if (r) return r[1].split(',').map((s) => s.trim()).filter(Boolean);
		const r2 = new RegExp('^' + k + ':\\s*\\n((?:\\s+-\\s*.*\\n?)+)', 'm').exec(y);
		return r2 ? r2[1].split('\n').map((s) => s.replace(/^\s*-\s*/, '').trim()).filter(Boolean) : [];
	};
	const neg = /negative:\s*\n\s*phase:\s*(\w+)\s*\n\s*type:\s*(\w+)/.exec(y) || /negative:\s*\n\s*type:\s*(\w+)\s*\n\s*phase:\s*(\w+)/.exec(y);
	return { includes: list('includes'), flags: list('flags'), features: list('features'), negative: neg };
}
const harness = (n) => fs.readFileSync(path.join(root, 'harness', n), 'utf8');
const PRE = 'var $262 = { global: globalThis, evalScript: function (s) { return (0, eval)(s); }, gc: function () {}, ' +
	'detachArrayBuffer: function () { throw new TypeError(\'no detach\'); }, agent: {} };\n' +
	(eager ? 'void Intl.Collator;\n' : '');

const tests = files(path.join(root, 'test', 'intl402')).filter((f) => f.includes(filter)).sort();
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 't262-'));
const res = {};
let idx = 0, running = 0, done = 0;
const fails = [];
function record(f, st) {
	const rel = path.relative(path.join(root, 'test', 'intl402'), f);
	const dir = rel.split(path.sep)[0].replace(/\.js$/, '') || '.';
	const d = res[dir] || (res[dir] = { pass: 0, fail: 0, skip: 0 });
	d[st]++;
	if (st === 'fail') fails.push(rel);
}
function next() {
	while (running < 3 && idx < tests.length) {
		const f = tests[idx++];
		const src = fs.readFileSync(f, 'utf8');
		const m = meta(src);
		if (m.features.some((x) => SKIP_FEATURES.includes(x)) || m.flags.includes('module') || /Temporal/.test(f)) { record(f, 'skip'); done++; continue; }
		let s = PRE;
		if (!m.flags.includes('raw')) {
			s += harness('assert.js') + '\n' + harness('sta.js') + '\n';
			if (m.flags.includes('async')) s += harness('doneprintHandle.js') + '\n';
			for (const inc of m.includes) s += harness(inc) + '\n';
		}
		if (m.flags.includes('onlyStrict')) s = '"use strict";\n' + s;
		s += src;
		const tf = path.join(tmp, 't' + idx + '.js');
		fs.writeFileSync(tf, s);
		running++;
		execFile(qjs, [tf], { timeout: 20000 }, (err, stdout) => {
			running--;
			let ok;
			if (m.negative) {
				const type = m.negative[1] === 'parse' || m.negative[1] === 'resolution' || m.negative[1] === 'runtime' ? m.negative[2] : m.negative[1];
				ok = new RegExp('EXCEPTION: ' + type).test(stdout);
			} else if (m.flags.includes('async')) ok = /Test262:AsyncTestComplete/.test(stdout) && !err;
			else ok = !err;
			if (!ok && verbose) console.log('FAIL ' + path.relative(root, f) + '\n  ' + String(stdout).split('\n').slice(0, 2).join('\n  '));
			record(f, ok ? 'pass' : 'fail');
			fs.unlinkSync(tf);
			done++;
			if (done === tests.length) report();
			else next();
		});
	}
	if (done === tests.length && running === 0) report();
}
let reported = false;
function report() {
	if (reported) return;
	reported = true;
	let P = 0, F = 0, S = 0;
	for (const k of Object.keys(res).sort()) {
		const d = res[k];
		P += d.pass; F += d.fail; S += d.skip;
		const n = d.pass + d.fail;
		console.log(k.padEnd(28) + String(d.pass).padStart(5) + ' / ' + String(n).padEnd(5) + (n ? (100 * d.pass / n).toFixed(1) + '%' : '') + (d.skip ? '  (' + d.skip + ' skipped)' : ''));
	}
	console.log('total'.padEnd(28) + String(P).padStart(5) + ' / ' + String(P + F).padEnd(5) + (100 * P / (P + F)).toFixed(1) + '%  (' + S + ' skipped)');
	fs.writeFileSync(path.join(tmp, '..', 't262-fails.txt'), fails.join('\n') + '\n');
	fs.rmSync(tmp, { recursive: true, force: true });
}
next();
