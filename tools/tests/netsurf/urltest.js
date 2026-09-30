/* urltest.js -- see urltest.sh */
const fs = require('fs');
const src = fs.readFileSync(process.env.DOMJS, 'utf8');
const a = src.indexOf('/* Onyx: URL and URLSearchParams');
const b = src.indexOf('static revokeObjectURL() {}', a);
const code = src.slice(a, src.indexOf('\n}', b) + 2);
const { URL } = new Function(code + '\nreturn { URL, URLSearchParams };')();
const D = process.env.D, verbose = process.argv[2] === '-v';
let pass = 0, fail = 0;
for (const t of JSON.parse(fs.readFileSync(D + '/urltestdata.json'))) {
	if (typeof t === 'string') continue;
	let u = null, err = null;
	try { u = t.base == null ? new URL(t.input) : new URL(t.input, t.base); } catch (e) { err = e; }
	const ok = t.failure ? err !== null : u !== null && ['href', 'protocol', 'username', 'password',
		'host', 'hostname', 'port', 'pathname', 'search', 'hash'].every(k => u[k] === t[k]) &&
		(t.origin === undefined || u.origin === t.origin);
	if (ok) pass++; else { fail++; if (verbose) console.log('FAIL', JSON.stringify(t.input), t.base, u ? u.href : String(err)); }
}
console.log('urltestdata.json  ' + pass + ' / ' + (pass + fail));
let sp = 0, sf = 0;
const st = JSON.parse(fs.readFileSync(D + '/setters_tests.json'));
for (const prop of Object.keys(st)) {
	if (prop === 'comment') continue;
	for (const t of st[prop]) {
		let u;
		try { u = new URL(t.href); u[prop] = t.new_value; } catch (e) { sf++; continue; }
		if (Object.keys(t.expected).every(k => u[k] === t.expected[k])) sp++;
		else { sf++; if (verbose) console.log('FAIL set', prop, t.href, JSON.stringify(t.new_value), u.href); }
	}
}
console.log('setters_tests.json ' + sp + ' / ' + (sp + sf));
let ap = 0, af = 0;
for (const t of JSON.parse(fs.readFileSync(D + '/toascii.json'))) {
	if (typeof t === 'string') continue;
	let u = null;
	try { u = new URL('https://' + t.input + '/x'); } catch (e) {}
	if (t.output === null ? u === null : u !== null && u.host === t.output) ap++;
	else { af++; if (verbose) console.log('FAIL ascii', JSON.stringify(t.input), t.output, u && u.host); }
}
console.log('toascii.json      ' + ap + ' / ' + (ap + af));
process.exit(fail > 0 || sf > 0 ? 1 : 0);
