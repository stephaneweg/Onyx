#!/usr/bin/env node
/*
 * tools/tests/netsurf/intl/mkpage.js -- makes tools/tests/netsurf/pages/js-intl.html: the cases of
 * cases.js with Node's answers (full ICU, as Chrome) as the expected values; the page logs
 * "OK <case>" or "FAIL <case>: <got> != <expected>" (jstest.sh checks them).
 *
 *   TZ=UTC node tools/tests/netsurf/intl/mkpage.js
 */
'use strict';
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, 'cases.js'), 'utf8');
/* the cases and Node's answers */
const lines = [];
const print = (s) => lines.push(s);
new Function('print', src)(print);
/* cases where ICU's answer is not Chrome's own or not the spec's */
const SKIP = [/pluralCategories/, /hour: 'numeric', minute: 'numeric'}\)\)\.formatRange/];
const exp = [];
for (const l of lines) {
	const i = l.indexOf(' => ');
	const c = l.slice(0, i), r = l.slice(i + 4);
	if (SKIP.some((re) => re.test(c))) continue;
	exp.push([c, r]);
}
const head = src.slice(src.indexOf('var d = '), src.indexOf('var CASES'));
const page = `<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Intl</title></head>
<body><p>Intl (made by tools/tests/netsurf/intl/mkpage.js from cases.js: Node's answers)</p>
<script>
${head}var EXP = ${JSON.stringify(exp, null, 0).replace(/\],\[/g, '],\n[')};
var ok = 0, bad = 0;
for (var i = 0; i < EXP.length; i++) {
	var r;
	try { r = String(eval(EXP[i][0])); } catch (e) { r = 'THROWS ' + e.name; }
	if (r === EXP[i][1]) ok++;
	else { bad++; console.log('FAIL ' + EXP[i][0] + ': ' + r + ' != ' + EXP[i][1]); }
}
console.log('intl ' + ok + ' / ' + EXP.length + ' as Chrome');
/* the default locale and time zone: the browser's, and the Date agrees */
var ro = new Intl.DateTimeFormat().resolvedOptions();
console.log('intl default ' + ro.locale + ' ' + (typeof ro.timeZone));
var off = new Date().getTimezoneOffset();
var parts = new Intl.DateTimeFormat('en', {hour: 'numeric', minute: 'numeric', hourCycle: 'h23'}).formatToParts(new Date());
var now = new Date();
console.log('intl zone agrees ' + (Number(parts[0].value) % 24 === now.getHours() && Number(parts[2].value) === now.getMinutes()));
console.log('intl done');
</script></body></html>
`;
fs.writeFileSync(path.join(__dirname, '..', 'pages', 'js-intl.html'), page);
console.log(exp.length + ' cases');
