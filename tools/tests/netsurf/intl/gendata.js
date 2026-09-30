#!/usr/bin/env node
/*
 * tools/tests/netsurf/intl/gendata.js -- the locale data of NetSurf's Intl (Onyx), made from
 * CLDR through the ICU of Node.js (full-icu): every string and pattern is read back from Node's
 * own Intl (formatToParts on probe values), so the data is CLDR's, as Chrome shows it.
 *
 *   node tools/tests/netsurf/intl/gendata.js > third_party/cldr-48/intl-data.js
 *
 * The output is one JavaScript expression (an object): { v: versions, loc: { tag: { section:
 * 'json', ... } }, tz: 'json', ... }. Each locale's sections are JSON strings parsed on first
 * use by intl.js; a regional locale (en-GB) keeps only the keys that differ from its language.
 */
'use strict';

/* the languages with only the formatting data (no units, display names, time zone names: English's) */
const LEAN = ['ja', 'zh', 'zh-TW', 'ko', 'ru', 'pl', 'sv', 'da', 'nb', 'fi', 'tr', 'cs'];
const LOCALES = [
	'en', 'en-GB', 'en-AU', 'en-CA', 'en-IN', 'en-IE', 'en-NZ', 'en-ZA',
	'fr', 'fr-CA', 'fr-BE', 'fr-CH',
	'de', 'de-AT', 'de-CH',
	'es', 'es-MX', 'es-US', 'es-419', 'es-AR',
	'it', 'it-CH',
	'nl', 'nl-BE',
	'pt', 'pt-PT',
	'ja', 'zh', 'zh-TW', 'ko', 'ru', 'pl', 'sv', 'da', 'nb', 'fi', 'tr', 'cs',
];
/* a locale's language (its base: the data it differs from) */
const base = (l) => l.split('-')[0];

const UTC = 'UTC';
/* the probe instants: Sunday 5 March 2023, 01:02:03.045 and 13:02:03.045 UTC */
const PA = new Date(Date.UTC(2023, 2, 5, 1, 2, 3, 45));
const PB = new Date(Date.UTC(2023, 2, 5, 13, 2, 3, 45));
const BC = new Date(Date.UTC(-100, 2, 5, 1, 2, 3, 45));

function dtf(l, o) { if (process.env.DBG) process.stderr.write(l + JSON.stringify(o) + "\n"); return new Intl.DateTimeFormat(l, Object.assign({ timeZone: UTC }, o)); }
function names(l, field, width, ctx) {
	/* month / weekday / era names, format or standalone forms */
	const out = [];
	if (field === 'month') {
		for (let m = 0; m < 12; m++) {
			const d = new Date(Date.UTC(2023, m, 5, 12));
			const o = ctx ? { month: width, day: 'numeric' } : { month: width };
			out.push(dtf(l, o).formatToParts(d).find((p) => p.type === 'month').value);
		}
	} else if (field === 'weekday') {
		for (let w = 0; w < 7; w++) {
			const d = new Date(Date.UTC(2023, 2, 5 + w, 12));	/* 5 March 2023: a Sunday */
			const o = ctx ? { weekday: width, day: 'numeric' } : { weekday: width };
			out.push(dtf(l, o).formatToParts(d).find((p) => p.type === 'weekday').value);
		}
	} else if (field === 'era') {
		for (const d of [BC, PA])
			out.push(dtf(l, { era: width, year: 'numeric' }).formatToParts(d).find((p) => p.type === 'era').value);
	}
	return out;
}

/* ---- date patterns, read back from formatToParts ------------------------------------------ */

function quote(s) {
	if (!/[A-Za-z']/.test(s)) return s;
	return "'" + s.replace(/'/g, "''") + "'";
}
function toPattern(l, opts, N) {
	const pa = dtf(l, opts).formatToParts(PA), pb = dtf(l, opts).formatToParts(PB);
	if (pa.length !== pb.length) throw new Error('parts differ ' + l + JSON.stringify(opts));
	let s = '';
	for (let i = 0; i < pa.length; i++) {
		const a = pa[i], b = pb[i], v = a.value;
		switch (a.type) {
		case 'literal': s += quote(v); break;
		case 'year': s += v.length === 2 ? 'yy' : 'y'; break;
		case 'month':
			if (/^\d+$/.test(v)) s += v.length === 2 ? 'MM' : 'M';
			else if (v === N.mon.l[2]) s += 'MMMM';
			else if (v === N.mon.s[2]) s += 'MMM';
			else if (v === N.mon.n[2]) s += 'MMMMM';
			else if (N.monS && v === N.monS.l[2]) s += 'LLLL';
			else if (N.monS && v === N.monS.s[2]) s += 'LLL';
			else if (N.monS && v === N.monS.n[2]) s += 'LLLLL';
			else throw new Error('month ' + v + ' ' + l);
			break;
		case 'day': s += v.length === 2 ? 'dd' : 'd'; break;
		case 'weekday':
			if (v === N.wd.l[0]) s += 'EEEE';
			else if (v === N.wd.s[0]) s += 'EEE';
			else if (v === N.wd.n[0]) s += 'EEEEE';
			else if (N.wdS && v === N.wdS.l[0]) s += 'cccc';
			else if (N.wdS && v === N.wdS.s[0]) s += 'ccc';
			else if (N.wdS && v === N.wdS.n[0]) s += 'ccccc';
			else throw new Error('weekday ' + v + ' ' + l);
			break;
		case 'era':
			if (v === N.era.l[1]) s += 'GGGG';
			else if (v === N.era.n[1] && v !== N.era.s[1]) s += 'GGGGG';
			else s += 'G';
			break;
		case 'hour': {
			const twentyfour = b.value === '13';
			const pad = a.value.length === 2;
			if (twentyfour) s += pad ? 'HH' : 'H';
			else if (a.value === '0' || a.value === '00') s += pad ? 'KK' : 'K';
			else s += pad ? 'hh' : 'h';
			break;
		}
		case 'minute': s += v.length === 2 ? 'mm' : 'm'; break;
		case 'second': s += v.length === 2 ? 'ss' : 's'; break;
		case 'fractionalSecond': s += 'S'.repeat(v.length); break;
		case 'dayPeriod':
			if (opts.dayPeriod) s += opts.dayPeriod === 'narrow' ? 'BBBBB' : opts.dayPeriod === 'long' ? 'BBBB' : 'B';
			else s += 'a';
			break;
		case 'timeZoneName': s += 'z'; break;
		default: throw new Error('part ' + a.type);
		}
	}
	return s;
}

/* date field combinations: weekday (-lsn: none long short narrow), era (-s), year (-n), month
   (-nslx: none numeric short long narrow), day (-n) */
function dateKeys() {
	const keys = [];
	for (const w of '-ls') for (const g of '-s') for (const y of '-n') for (const m of '-nslx') for (const d of '-n') {
		if (w + g + y + m + d === '-----') continue;
		if (g === 's' && (y === '-' || w !== '-')) continue;	/* (era with a weekday: V8 aborts on de-CH) */
		keys.push(w + g + y + m + d);
	}
	return keys;
}
const W = { l: 'long', s: 'short', n: 'narrow', x: 'narrow' };
function dateOpts(k) {
	const o = {};
	if (k[0] !== '-') o.weekday = W[k[0]];
	if (k[1] !== '-') o.era = 'short';
	if (k[2] !== '-') o.year = 'numeric';
	if (k[3] !== '-') o.month = k[3] === 'n' ? 'numeric' : W[k[3]];
	if (k[4] !== '-') o.day = 'numeric';
	return o;
}
/* time: hour minute second (-n each), the hour cycle (1 = 12 h, 2 = 24 h) */
function timeKeys() {
	const keys = [];
	for (const h of '-n') for (const m of '-n') for (const s of '-n') {
		if (h + m + s === '---') continue;
		keys.push(h + m + s);
	}
	return keys;
}
function timeOpts(k, c) {
	const o = {};
	if (k[0] !== '-') o.hour = 'numeric';
	if (k[1] !== '-') o.minute = 'numeric';
	if (k[2] !== '-') o.second = 'numeric';
	if (k[0] !== '-') o.hourCycle = c === 1 ? 'h12' : 'h23';
	return o;
}

/* the glue of a date and a time: "{1}, {0}" -- found by formatting both and the whole */
function glue(l, dOpts, tOpts, N) {
	const pd = toPattern(l, dOpts, N), pt = toPattern(l, tOpts, N), pw = toPattern(l, Object.assign({}, dOpts, tOpts), N);
	const i = pw.indexOf(pd), j = pw.indexOf(pt);
	if (i >= 0 && j >= 0) {
		if (i < j) return pw.slice(0, i) + '{1}' + pw.slice(i + pd.length, j) + '{0}' + pw.slice(j + pt.length);
		return pw.slice(0, j) + '{0}' + pw.slice(j + pt.length, i) + '{1}' + pw.slice(i + pd.length);
	}
	return null;
}

function dateData(l) {
	const N = {
		mon: { l: names(l, 'month', 'long', 1), s: names(l, 'month', 'short', 1), n: names(l, 'month', 'narrow', 1) },
		wd: { l: names(l, 'weekday', 'long', 1), s: names(l, 'weekday', 'short', 1), n: names(l, 'weekday', 'narrow', 1) },
		era: { l: names(l, 'era', 'long'), s: names(l, 'era', 'short'), n: names(l, 'era', 'narrow') },
	};
	const monS = { l: names(l, 'month', 'long', 0), s: names(l, 'month', 'short', 0), n: names(l, 'month', 'narrow', 0) };
	const wdS = { l: names(l, 'weekday', 'long', 0), s: names(l, 'weekday', 'short', 0), n: names(l, 'weekday', 'narrow', 0) };
	const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
	if (!same(monS, N.mon)) N.monS = monS;
	if (!same(wdS, N.wd)) N.wdS = wdS;
	const D = {};
	D.mon = [N.mon.l, N.mon.s, N.mon.n];
	if (N.monS) D.monS = [N.monS.l, N.monS.s, N.monS.n];
	D.wd = [N.wd.l, N.wd.s, N.wd.n];
	if (N.wdS) D.wdS = [N.wdS.l, N.wdS.s, N.wdS.n];
	D.era = [N.era.l, N.era.s, N.era.n];
	/* AM / PM */
	D.ap = [0, 13].map((h) => dtf(l, { hour: 'numeric', hourCycle: 'h12' }).formatToParts(new Date(Date.UTC(2023, 2, 5, h))).find((p) => p.type === 'dayPeriod').value);
	/* the flexible day periods (dayPeriod: narrow short long), by hour: runs "h:text" */
	D.fp = ['narrow', 'short', 'long'].map((w) => {
		const out = [];
		let last = null;
		for (let h = 0; h < 24; h++) {
			const v = dtf(l, { dayPeriod: w }).format(new Date(Date.UTC(2023, 2, 5, h)));
			const noon = h === 12 ? dtf(l, { dayPeriod: w }).format(new Date(Date.UTC(2023, 2, 5, 12, 0))) : null;
			if (v !== last) { out.push(h + ':' + v); last = v; }
			void noon;
		}
		/* noon exactly (12:00) may have its own name */
		const n12 = dtf(l, { dayPeriod: w }).format(new Date(Date.UTC(2023, 2, 5, 12, 0)));
		const n1201 = dtf(l, { dayPeriod: w }).format(new Date(Date.UTC(2023, 2, 5, 12, 1)));
		const m0 = dtf(l, { dayPeriod: w }).format(new Date(Date.UTC(2023, 2, 5, 0, 0)));
		const m1 = dtf(l, { dayPeriod: w }).format(new Date(Date.UTC(2023, 2, 5, 0, 1)));
		return { r: out.join('|'), noon: n12 !== n1201 ? n12 : undefined, mid: m0 !== m1 ? m0 : undefined };
	});
	/* the combinations */
	D.dp = {};
	for (const k of dateKeys()) D.dp[k] = toPattern(l, dateOpts(k), N);
	D.tp = {};
	for (const k of timeKeys()) for (const c of [1, 2]) {
		D.tp[k + c] = toPattern(l, timeOpts(k, c), N);
		D.tp[k + c + 'z'] = toPattern(l, Object.assign(timeOpts(k, c), { timeZoneName: 'short' }), N);
	}
	/* a date alone with a time zone name */
	D.dz = toPattern(l, { year: 'numeric', month: 'numeric', day: 'numeric', timeZoneName: 'short' }, N);
	D.dzl = toPattern(l, { year: 'numeric', month: 'long', day: 'numeric', timeZoneName: 'short' }, N);
	/* fractional seconds: the separator */
	D.fs = dtf(l, { second: 'numeric', fractionalSecondDigits: 1 }).formatToParts(PA).filter((p) => p.type === 'literal').map((p) => p.value).join('');
	/* the glues by the date's month width: full (weekday + long), long, medium (short), short (numeric) */
	const T = { hour: 'numeric', minute: 'numeric' };
	D.gl = [
		glue(l, { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' }, T, N),
		glue(l, { year: 'numeric', month: 'long', day: 'numeric' }, T, N),
		glue(l, { year: 'numeric', month: 'short', day: 'numeric' }, T, N),
		glue(l, { year: 'numeric', month: 'numeric', day: 'numeric' }, T, N),
	];
	/* styles */
	const ST = ['full', 'long', 'medium', 'short'];
	D.ds = ST.map((s) => toPattern(l, { dateStyle: s }, N));
	D.ts = ST.map((s) => [toPattern(l, { timeStyle: s, hourCycle: 'h12' }, N), toPattern(l, { timeStyle: s, hourCycle: 'h23' }, N)]);
	D.tsd = ST.map((s) => toPattern(l, { timeStyle: s }, N));
	D.sg = ST.map((s) => {
		const pd = D.ds[ST.indexOf(s)], pt = toPattern(l, { timeStyle: 'short' }, N);
		const pw = toPattern(l, { dateStyle: s, timeStyle: 'short' }, N);
		const i = pw.indexOf(pd), j = pw.indexOf(pt);
		if (i < 0 || j < 0) throw new Error('style glue ' + l);
		return i < j ? pw.slice(0, i) + '{1}' + pw.slice(i + pd.length, j) + '{0}' + pw.slice(j + pt.length)
			: pw.slice(0, j) + '{0}' + pw.slice(j + pt.length, i) + '{1}' + pw.slice(i + pd.length);
	});
	/* the hour cycles: the locale's default, and its 12-hour one */
	const hcOf = (o) => {
		const p = toPattern(l, Object.assign({ hour: 'numeric' }, o), N);
		return /H/.test(p) ? 'h23' : /K/.test(p) ? 'h11' : 'h12';
	};
	D.hc = hcOf({});
	D.hc12 = hcOf({ hour12: true });
	/* date ranges: the separator when only the day differs, the month, the time (hour), everything */
	const rsep = (o, a, b, sa, sb) => {
		const r = dtf(l, o).formatRange(a, b);
		void sa; void sb;
		const m = /[\s  ]*[–～〜][\s  ]*/.exec(r) || /\s+-\s+/.exec(r) || /-/.exec(r);
		return m ? m[0] : ' – ';
	};
	const jan5 = new Date(Date.UTC(2023, 0, 5, 10)), jan7 = new Date(Date.UTC(2023, 0, 7, 10)), apr5 = new Date(Date.UTC(2023, 3, 5, 10));
	D.rs = [
		rsep({ year: 'numeric', month: 'long', day: 'numeric' }, jan5, jan7, '5', '7'),
		rsep({ year: 'numeric', month: 'long' }, jan5, apr5, N.mon.l[0], N.mon.l[3]),
		rsep({ hour: 'numeric', minute: 'numeric', hourCycle: 'h23' }, jan5, new Date(Date.UTC(2023, 0, 5, 14)), '10:00'.replace(':', dtf(l, { hour: 'numeric', minute: 'numeric', hourCycle: 'h23' }).formatToParts(jan5).find((p) => p.type === 'literal').value), '14'),
		rsep({ year: 'numeric', month: 'numeric', day: 'numeric' }, jan5, new Date(Date.UTC(2024, 1, 7, 10)), '2023', dtf(l, { year: 'numeric', month: 'numeric', day: 'numeric' }).format(new Date(Date.UTC(2024, 1, 7, 10))).slice(0, 1)),
	];
	/* first day of the week, minimal days (Intl.Locale's weekInfo): from Intl.Locale when Node has it */
	try {
		const wi = new Intl.Locale(l).getWeekInfo ? new Intl.Locale(l).getWeekInfo() : new Intl.Locale(l).weekInfo;
		if (wi) D.wk = [wi.firstDay, wi.minimalDays, wi.weekend];
	} catch (e) { /* none */ }
	/* Chrome shows a plain space where CLDR has a narrow no-break one (V8, web compatibility) */
	return JSON.parse(JSON.stringify(D).replace(/\u202f/g, " "));
}

/* ---- numbers ---------------------------------------------------------------------------------- */

/* A template from parts: the number's parts become '#', the sign '-' / '+', the percent '%', the
   currency '¤', the compact / unit text stays literal (the literal chars # % ¤ - + are escaped \). */
function templ(parts, keep) {
	let s = '', inNum = false;
	for (const p of parts) {
		switch (p.type) {
		case 'integer': case 'group': case 'decimal': case 'fraction': case 'nan': case 'infinity':
		case 'exponentSeparator': case 'exponentInteger': case 'exponentMinusSign':
			if (!inNum) { s += '#'; inNum = true; }
			continue;
		case 'minusSign': s += '-'; break;
		case 'plusSign': s += '+'; break;
		case 'percentSign': s += '%'; break;
		case 'currency': s += '¤'; break;
		case 'compact': s += '{' + esc(p.value) + '}'; break;
		case 'unit': s += '{' + esc(p.value) + '}'; break;
		case 'literal': s += esc(p.value); break;
		default: throw new Error('number part ' + p.type);
		}
		inNum = false;
	}
	return s;
}
function esc(s) { return s.replace(/[\\#%¤+\-~{}]/g, (c) => '\\' + c); }

const CURRENCIES = ['USD', 'EUR', 'GBP', 'JPY', 'CNY', 'CHF', 'CAD', 'AUD', 'NZD', 'INR', 'BRL', 'MXN', 'ARS', 'CLP', 'COP',
	'SEK', 'NOK', 'DKK', 'PLN', 'CZK', 'HUF', 'RON', 'RUB', 'UAH', 'TRY', 'KRW', 'HKD', 'SGD', 'TWD', 'THB', 'ZAR',
	'ILS', 'AED', 'SAR', 'EGP', 'MAD', 'NGN', 'IDR', 'MYR', 'PHP', 'VND', 'PKR', 'BTC', 'XAF', 'XOF', 'ISK', 'BGN'];

function numberData(l) {
	const N = {};
	const nf = (o) => new Intl.NumberFormat(l, o);
	const parts = nf({ useGrouping: 'always' }).formatToParts(-1234567.5);
	N.dec = parts.find((p) => p.type === 'decimal').value;
	N.grp = parts.find((p) => p.type === 'group').value;
	N.min = parts.find((p) => p.type === 'minusSign').value;
	N.plus = nf({ signDisplay: 'always' }).formatToParts(1).find((p) => p.type === 'plusSign').value;
	N.pct = nf({ style: 'percent' }).formatToParts(1).find((p) => p.type === 'percentSign').value;
	N.exp = nf({ notation: 'scientific' }).formatToParts(1234).find((p) => p.type === 'exponentSeparator').value;
	N.inf = nf().format(Infinity);
	N.nan = nf().format(NaN);
	/* grouping: primary and secondary sizes, minimum grouping digits */
	const ints = nf({ useGrouping: 'always' }).formatToParts(1234567890).filter((p) => p.type === 'integer').map((p) => p.value.length);
	N.g1 = ints[ints.length - 1];
	N.g2 = ints.length > 2 ? ints[ints.length - 2] : N.g1;
	N.mg = nf().format(1234).indexOf(N.grp) < 0 ? 2 : 1;
	/* the templates: decimal, percent (positive, negative) */
	N.t = templ(nf().formatToParts(1)) + ';' + templ(nf().formatToParts(-1));
	N.tp = templ(nf({ style: 'percent' }).formatToParts(0.5)) + ';' + templ(nf({ style: 'percent' }).formatToParts(-0.5));
	N.ts = templ(nf({ notation: 'scientific' }).formatToParts(1234));
	/* currencies: symbol, code, name (plural), accounting -- with EUR (a symbol) */
	const ct = (o, v) => templ(nf(Object.assign({ style: 'currency', currency: 'EUR' }, o)).formatToParts(v));
	N.tc = ct({}, 1) + ';' + ct({}, -1);
	N.ta = ct({ currencySign: 'accounting' }, 1) + ';' + ct({ currencySign: 'accounting' }, -1);
	/* currency spacing: inserted between a letter currency and the number? (USD: en "USD 1.00") */
	N.csp = templ(nf({ style: 'currency', currency: 'USD', currencyDisplay: 'code' }).formatToParts(1));
	N.tn = templ(nf({ style: 'currency', currency: 'EUR', currencyDisplay: 'name' }).formatToParts(2));
	/* symbols (differing from the code), narrow symbols, names (singular, plural by category) */
	N.cs = {}; N.cn = {}; N.cm = {};
	const cats = new Intl.PluralRules(l).resolvedOptions().pluralCategories;
	const catVal = {};
	for (const v of [1, 0, 2, 3, 5, 11, 21, 101, 1.5, 0.5, 1000000]) {
		const c = new Intl.PluralRules(l).select(v);
		if (!(c in catVal)) catVal[c] = v;
	}
	for (const c of CURRENCIES) {
		const sym = nf({ style: 'currency', currency: c }).formatToParts(1).find((p) => p.type === 'currency').value;
		const nar = nf({ style: 'currency', currency: c, currencyDisplay: 'narrowSymbol' }).formatToParts(1).find((p) => p.type === 'currency').value;
		if (sym !== c) N.cs[c] = sym;
		if (nar !== sym) N.cm[c] = nar;
		const byCat = {};
		for (const cat of cats) {
			if (!(cat in catVal)) continue;
			byCat[cat] = nf({ style: 'currency', currency: c, currencyDisplay: 'name' }).formatToParts(catVal[cat]).find((p) => p.type === 'currency').value;
		}
		const o = byCat.other;
		const arr = [o];
		for (const cat of ['one', 'two', 'few', 'many', 'zero']) if (byCat[cat] && byCat[cat] !== o) arr.push(cat + ':' + byCat[cat]);
		N.cn[c] = arr.length === 1 ? o : arr;
	}
	/* compact: short and long, magnitudes 3 .. 14, by plural category */
	/* ranges: the separator ("3–5"), the approximately sign ("~3") */
	const rg = nf().formatRange(3, 5);
	N.rg = rg.slice(1, rg.length - 1);
	N.ap = nf().formatRangeToParts(3, 3).find((p) => p.type === 'approximatelySign').value;
	N.cp = {};
	for (const disp of ['short', 'long']) {
		const f = nf({ notation: 'compact', compactDisplay: disp });
		const out = [];
		for (let e = 3; e <= 14; e++) {
			/* the number of integer digits shown at 10^e (2, 20, 200: exact), the divisor */
			const p2 = f.formatToParts(2 * 10 ** e);
			if (!p2.find((q) => q.type === 'compact')) { out.push(''); continue; }
			const I = p2.filter((q) => q.type === 'integer').map((q) => q.value).join('').length;
			const div = e - (I - 1);
			/* a shown number of each plural category (the standard rules on the shown number, as ICU) */
			const cand = I === 1 ? [1, 2, 3, 5, 1.5, 2.5] : I === 2 ? [10, 11, 12, 20, 21, 22, 25] : [100, 101, 102, 105, 111, 121, 200];
			const byCat = {};
			for (const D of cand) {
				const cat = new Intl.PluralRules(l).select(D);
				if (cat in byCat) continue;
				const p = f.formatToParts(Number(D + 'e' + div));
				byCat[cat] = templ(p, true);
			}
			const other = byCat.other || byCat.many || byCat.few || byCat.one;
			const arr = [I + ':' + other];
			for (const cat of ['one', 'two', 'few', 'many', 'zero']) if (byCat[cat] && byCat[cat] !== other) arr.push(cat + ':' + byCat[cat]);
			out.push(arr.join('|'));
		}
		N.cp[disp[0]] = out;
	}
	return N;
}

/* ---- units --------------------------------------------------------------------------------- */

const UNITS = Intl.supportedValuesOf('unit');
const COMPOUNDS = ['kilometer-per-hour', 'mile-per-hour', 'meter-per-second', 'liter-per-kilometer', 'mile-per-gallon'];
function unitData(l) {
	const U = {};
	const cats = new Intl.PluralRules(l).resolvedOptions().pluralCategories;
	const vals = [1, 0, 2, 3, 5, 11, 21, 101, 1.5, 0.5, 1000000, 22, 25];
	for (const u of UNITS.concat(COMPOUNDS)) {
		U[u] = ['long', 'short', 'narrow'].map((w) => {
			const byCat = {};
			for (const v of vals) {
				const c = new Intl.PluralRules(l).select(v);
				if (c in byCat || !cats.includes(c)) continue;
				byCat[c] = templ(new Intl.NumberFormat(l, { style: 'unit', unit: u, unitDisplay: w }).formatToParts(v));
			}
			const arr = [byCat.other];
			for (const cat of ['one', 'two', 'few', 'many', 'zero']) if (byCat[cat] && byCat[cat] !== byCat.other) arr.push(cat + ':' + byCat[cat]);
			return arr.join('|');
		});
		if (U[u][2] === U[u][1]) U[u][2] = 0;
	}
	/* "per" forms: X-per-Y is X's text with Y's per form around it; read with byte-per-Y */
	U.per = {};
	for (const y of UNITS) {
		if (y === 'percent') continue;
		U.per[y] = ['long', 'short', 'narrow'].map((w) => {
			const whole = new Intl.NumberFormat(l, { style: 'unit', unit: 'byte-per-' + y, unitDisplay: w }).format(5);
			const x = new Intl.NumberFormat(l, { style: 'unit', unit: 'byte', unitDisplay: w }).format(5);
			const i = whole.indexOf(x);
			if (i < 0) return '?' + whole;
			return esc(whole.slice(0, i)) + '{0}' + esc(whole.slice(i + x.length));
		});
		if (U.per[y][2] === U.per[y][1]) U.per[y][2] = 0;
	}
	return U;
}

/* ---- relative time, lists ---------------------------------------------------------------------- */

const RUNITS = ['year', 'quarter', 'month', 'week', 'day', 'hour', 'minute', 'second'];
function relData(l) {
	const R = {};
	const cats = new Intl.PluralRules(l).resolvedOptions().pluralCategories;
	const vals = [1, 0, 2, 3, 5, 11, 21, 101, 1.5, 0.5, 1000000, 22, 25];
	for (const st of ['long', 'short', 'narrow']) {
		const always = new Intl.RelativeTimeFormat(l, { style: st });
		const auto = new Intl.RelativeTimeFormat(l, { style: st, numeric: 'auto' });
		R[st[0]] = {};
		for (const u of RUNITS) {
			const e = {};
			for (const dir of [-1, 1]) {
				const byCat = {};
				for (const v of vals) {
					const c = new Intl.PluralRules(l).select(v);
					if (c in byCat || !cats.includes(c)) continue;
					byCat[c] = templ(always.formatToParts(dir * v, u));
				}
				const arr = [byCat.other];
				for (const cat of ['one', 'two', 'few', 'many', 'zero']) if (byCat[cat] && byCat[cat] !== byCat.other) arr.push(cat + ':' + byCat[cat]);
				e[dir < 0 ? 'p' : 'f'] = arr.join('|');
			}
			/* numeric: auto -- the words for -2 .. 2 */
			const w = {};
			for (const v of [-2, -1, 0, 1, 2]) {
				const p = auto.formatToParts(v, u);
				if (p.length === 1 && p[0].type === 'literal') w[v] = p[0].value;
			}
			if (Object.keys(w).length) e.a = w;
			R[st[0]][u] = e;
		}
	}
	return R;
}
function listData(l) {
	const L = {};
	for (const type of ['conjunction', 'disjunction', 'unit']) for (const st of ['long', 'short', 'narrow']) {
		const f = new Intl.ListFormat(l, { type, style: st });
		const two = f.format(['{0}', '{1}']);
		const three = f.format(['{0}', '{1}', '{2}']);
		const four = f.format(['{0}', '{1}', '{2}', '{3}']);
		/* start: {0}<s>{1}, middle: {0}<m>{1}, end: {0}<e>{1}, pair: {0}<p>{1} */
		const s = three.slice(3, three.indexOf('{1}'));
		const e = three.slice(three.indexOf('{1}') + 3, three.indexOf('{2}'));
		const m = four.slice(four.indexOf('{1}') + 3, four.indexOf('{2}'));
		const p = two.slice(3, two.indexOf('{1}'));
		L[type[0] + st[0]] = [s, m, e, p];
	}
	return L;
}

/* ---- display names ------------------------------------------------------------------------ */

const LANGS = ['af', 'am', 'ar', 'az', 'be', 'bg', 'bn', 'bs', 'ca', 'cs', 'cy', 'da', 'de', 'de-AT', 'de-CH', 'el', 'en',
	'en-AU', 'en-CA', 'en-GB', 'en-US', 'eo', 'es', 'es-419', 'es-ES', 'es-MX', 'et', 'eu', 'fa', 'fi', 'fil', 'fr', 'fr-CA',
	'fr-CH', 'ga', 'gl', 'gu', 'he', 'hi', 'hr', 'hu', 'hy', 'id', 'is', 'it', 'ja', 'ka', 'kk', 'km', 'kn', 'ko', 'ky',
	'la', 'lb', 'lo', 'lt', 'lv', 'mk', 'ml', 'mn', 'mr', 'ms', 'mt', 'my', 'nb', 'ne', 'nl', 'nl-BE', 'nn', 'no', 'pa',
	'pl', 'ps', 'pt', 'pt-BR', 'pt-PT', 'ro', 'ru', 'si', 'sk', 'sl', 'so', 'sq', 'sr', 'sv', 'sw', 'ta', 'te', 'th',
	'tl', 'tr', 'uk', 'ur', 'uz', 'vi', 'xh', 'yi', 'yo', 'zh', 'zh-Hans', 'zh-Hant', 'zu', 'br', 'co', 'oc', 'gd', 'fy'];
const SCRIPTS = ['Latn', 'Cyrl', 'Grek', 'Arab', 'Hebr', 'Hans', 'Hant', 'Jpan', 'Kore', 'Hira', 'Kana', 'Deva', 'Thai',
	'Armn', 'Geor', 'Beng', 'Taml', 'Ethi', 'Hang', 'Zyyy', 'Zzzz'];
function regionCodes() {
	const out = [];
	const A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';
	const dn = new Intl.DisplayNames('en', { type: 'region', fallback: 'none' });
	for (const a of A) for (const b of A) {
		const c = a + b;
		const n = dn.of(c);
		if (n && n !== c) out.push(c);
	}
	for (const c of ['001', '002', '003', '005', '009', '011', '013', '014', '015', '017', '018', '019', '021', '029', '030',
		'034', '035', '039', '053', '054', '057', '061', '142', '143', '145', '150', '151', '154', '155', '202', '419'])
		out.push(c);
	return out;
}
const REGIONS = regionCodes();
const FIELDS = ['era', 'year', 'quarter', 'month', 'weekOfYear', 'weekday', 'day', 'dayPeriod', 'hour', 'minute', 'second', 'timeZoneName'];
const CALS = ['gregory', 'buddhist', 'chinese', 'coptic', 'dangi', 'ethioaa', 'ethiopic', 'hebrew', 'indian', 'islamic',
	'islamic-civil', 'islamic-umalqura', 'iso8601', 'japanese', 'persian', 'roc'];
function namesData(l) {
	const D = {};
	const get = (type, codes, o) => {
		const out = [];
		for (const c of codes) {
			let v = '';
			try { v = new Intl.DisplayNames(l, Object.assign({ type, fallback: 'none' }, o)).of(c) || ''; } catch (e) { v = ''; }
			out.push(v === c ? '' : v);
		}
		return out;
	};
	const sparse = (a, b) => { const o = {}; a.forEach((v, i) => { if (v !== b[i]) o[i] = v; }); return o; };
	D.lg = get('language', LANGS);
	D.ls = get('language', LANGS, { languageDisplay: 'standard' });
	D.lsh = get('language', LANGS, { style: 'short' });
	D.rg = get('region', REGIONS);
	D.rgs = get('region', REGIONS, { style: 'short' });
	D.sc = get('script', SCRIPTS);
	D.cu = get('currency', CURRENCIES);
	D.fl = get('dateTimeField', FIELDS);
	D.fs = get('dateTimeField', FIELDS, { style: 'short' });
	D.fn = get('dateTimeField', FIELDS, { style: 'narrow' });
	D.ca = get('calendar', CALS);
	/* a locale's pattern: "English (United Kingdom)", its separator */
	D.ls = sparse(D.ls, D.lg);
	D.lsh = sparse(D.lsh, D.lg);
	D.rgs = sparse(D.rgs, D.rg);
	D.fs = sparse(D.fs, D.fl);
	D.fn = sparse(D.fn, D.fl);
	D.pat = new Intl.DisplayNames(l, { type: 'language' }).of('xx-YY');
	D.pat2 = new Intl.DisplayNames(l, { type: 'language' }).of('xx-Latn-YY');
	return D;
}

/* ---- time zones -------------------------------------------------------------------------- */

/* The zones and their current rules: a standard offset (minutes) and a DST rule, read from Node's
   tz data by finding the 2026 transitions. */
const RULES = {
	/* id: [start: month, week (1..4, 5 last, or -n: the n-th day >= date), weekday, time min, time base (u w s)], end: same, save */
	EU: [[3, 5, 0, 60, 'u'], [10, 5, 0, 60, 'u'], 60],
	US: [[3, 2, 0, 120, 'w'], [11, 1, 0, 120, 'w'], 60],
	AUS: [[10, 1, 0, 120, 's'], [4, 1, 0, 180, 'w'], 60],	/* southern: start in October */
	NZ: [[9, 5, 0, 120, 's'], [4, 1, 0, 180, 'w'], 60],
	CL: [[9, 1, 0, 240, 'u'], [4, 1, 0, 180, 'u'], 60],	/* Chile: first Sunday of September (24:00 Saturday local), April */
	PY: null,
	MX: null,
};
const DTFZ = {};
function offsetAt(zone, t) {
	const f = DTFZ[zone] || (DTFZ[zone] = new Intl.DateTimeFormat('en-US', { timeZone: zone, hourCycle: 'h23', year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric', second: 'numeric' }));
	const p = f.formatToParts(new Date(t));
	const g = (k) => Number(p.find((x) => x.type === k).value);
	return Math.round((Date.UTC(g('year'), g('month') - 1, g('day'), g('hour'), g('minute'), g('second')) - t) / 60000);
}
const TRC = {};
function transitions(zone, year) {
	const key = zone + year;
	if (TRC[key]) return TRC[key];
	const out = TRC[key] = [];
	let t = Date.UTC(year, 0, 1);
	const end = Date.UTC(year + 1, 0, 1);
	let o = offsetAt(zone, t);
	const step = 3600e3 * 6;
	while (t < end) {
		const o2 = offsetAt(zone, t + step);
		if (o2 !== o) {
			let a = t, b = t + step;
			while (b - a > 60e3) { const m = Math.floor((a + b) / 2 / 60e3) * 60e3; if (offsetAt(zone, m) === o) a = m; else b = m; }
			out.push([b, o, o2]);
			o = o2;
		}
		t += step;
	}
	return out;
}
/* the rule's transition instant in a year */
function ruleInstant(r, year, std, save, isStart) {
	const [mon, wk, wd, time, basis] = r;
	let day;
	if (wk === 5) {
		const last = new Date(Date.UTC(year, mon, 0)).getUTCDate();
		const dw = new Date(Date.UTC(year, mon - 1, last)).getUTCDay();
		day = last - ((dw - wd + 7) % 7);
	} else if (wk > 0) {
		const dw = new Date(Date.UTC(year, mon - 1, 1)).getUTCDay();
		day = 1 + ((wd - dw + 7) % 7) + (wk - 1) * 7;
	} else {
		const from = -wk;
		const dw = new Date(Date.UTC(year, mon - 1, from)).getUTCDay();
		day = from + ((wd - dw + 7) % 7);
	}
	let t = Date.UTC(year, mon - 1, day) + time * 60e3;
	/* the local time of the change: wall (the offset before it), standard, or universal */
	if (basis === 'w') t -= (isStart ? std : std + save) * 60e3;
	else if (basis === 's') t -= std * 60e3;
	return t;
}
function tzData() {
	const zones = Intl.supportedValuesOf('timeZone');
	const Z = {};
	const unknown = [];
	for (const z of zones) {
		const tr = transitions(z, 2026);
		const jan = offsetAt(z, Date.UTC(2026, 0, 15)), jul = offsetAt(z, Date.UTC(2026, 6, 15));
		if (!tr.length) { Z[z] = [jan]; continue; }
		if (tr.length !== 2) { unknown.push(z + ' ' + tr.length); Z[z] = [offsetAt(z, Date.now())]; continue; }
		const std = Math.min(jan, jul), save = Math.max(jan, jul) - std;
		const start = tr.find((x) => x[2] > x[1]), end = tr.find((x) => x[2] < x[1]);
		let found = null;
		for (const id of Object.keys(RULES)) {
			const R = RULES[id];
			if (!R) continue;
			if (R[2] !== save) continue;
			if (ruleInstant(R[0], 2026, std, save, true) === start[0] && ruleInstant(R[1], 2026, std, save, false) === end[0]
				&& ruleInstant(R[0], 2027, std, save, true) === transitions(z, 2027).find((x) => x[2] > x[1])[0]) { found = id; break; }
		}
		if (!found) {
			/* describe the zone's own rule from 2026 and 2027 and check it */
			const r = describe(z, start, end, std, save);
			if (r) { Z[z] = [std, r[0], r[1], save]; continue; }
			unknown.push(z);
			Z[z] = [offsetAt(z, Date.now())];
			continue;
		}
		Z[z] = [std, found];
	}
	return { Z, unknown };
}
/* a zone's rule from its transitions: [month, week, weekday, time, basis] each, checked on 2027 and 2028 */
function describe(z, start, end, std, save) {
	const rs = [];
	for (const [tr, isStart] of [[start, true], [end, false]]) {
		const d = new Date(tr[0]);
		let found = null;
		for (const basis of ['u', 'w', 's']) {
			for (const wk of [1, 2, 3, 4, 5]) {
				for (let time = 0; time <= 25 * 60; time += 30) {
					for (const wd of [0, 1, 2, 3, 4, 5, 6]) {
						const r = [d.getUTCMonth() + 1, wk, wd, time, basis];
						const r2 = r.slice();
						let ok = true;
						for (const y of [2026, 2027, 2028]) {
							const t = transitions(z, y).find((x) => (x[2] > x[1]) === isStart);
							if (!t) { ok = false; break; }
							/* the month may differ when the day rolls over: try the next month too */
							if (ruleInstant(r, y, std, save, isStart) !== t[0]) { ok = false; break; }
						}
						if (ok) { found = r2; break; }
					}
					if (found) break;
				}
				if (found) break;
			}
			if (found) break;
		}
		if (!found) {
			/* a month-before start ("the last Saturday of March at 24:00" = Sunday 00:00): try the previous month */
			return null;
		}
		rs.push(found);
	}
	return rs;
}

/* time zone names: metazones (a group of zones with the same names), per locale */
const TZN_ZONES = ['UTC', 'Europe/London', 'Europe/Dublin', 'Europe/Lisbon', 'Europe/Paris', 'Europe/Berlin', 'Europe/Brussels',
	'Europe/Amsterdam', 'Europe/Madrid', 'Europe/Rome', 'Europe/Zurich', 'Europe/Vienna', 'Europe/Stockholm', 'Europe/Oslo',
	'Europe/Copenhagen', 'Europe/Warsaw', 'Europe/Prague', 'Europe/Budapest', 'Europe/Athens', 'Europe/Helsinki',
	'Europe/Bucharest', 'Europe/Kiev', 'Europe/Kyiv', 'Europe/Istanbul', 'Europe/Moscow', 'Atlantic/Reykjavik', 'Africa/Casablanca',
	'Africa/Lagos', 'Africa/Cairo', 'Africa/Johannesburg', 'Africa/Nairobi', 'Asia/Dubai', 'Asia/Karachi', 'Asia/Kolkata',
	'Asia/Calcutta', 'Asia/Dhaka', 'Asia/Bangkok', 'Asia/Jakarta', 'Asia/Singapore', 'Asia/Shanghai', 'Asia/Hong_Kong',
	'Asia/Taipei', 'Asia/Seoul', 'Asia/Tokyo', 'Asia/Manila', 'Asia/Jerusalem', 'Asia/Tehran', 'Asia/Riyadh',
	'Australia/Perth', 'Australia/Adelaide', 'Australia/Darwin', 'Australia/Brisbane', 'Australia/Sydney', 'Australia/Melbourne',
	'Pacific/Auckland', 'Pacific/Honolulu', 'America/Anchorage', 'America/Los_Angeles', 'America/Vancouver', 'America/Denver',
	'America/Phoenix', 'America/Chicago', 'America/Mexico_City', 'America/New_York', 'America/Toronto', 'America/Montreal',
	'America/Halifax', 'America/St_Johns', 'America/Sao_Paulo', 'America/Argentina/Buenos_Aires', 'America/Buenos_Aires',
	'America/Bogota', 'America/Lima', 'America/Santiago', 'America/Caracas', 'Atlantic/Azores', 'Atlantic/Canary',
	'Europe/Luxembourg', 'Europe/Monaco', 'Europe/Andorra'];
function tzNames(l) {
	const T = {};
	const w = Date.UTC(2023, 0, 15, 12), s = Date.UTC(2023, 6, 15, 12);
	const get = (z, n, t) => new Intl.DateTimeFormat(l, { timeZone: z, timeZoneName: n }).formatToParts(new Date(t)).find((p) => p.type === 'timeZoneName').value;
	/* the GMT format: "GMT+1" (short), "GMT+01:00" (long); the zero: "GMT" */
	const gs = get('Etc/GMT-1', 'shortOffset', w), gl = get('Etc/GMT-1', 'longOffset', w), g0 = get('UTC', 'longOffset', w);
	T.g = [gs, gl, g0, get('Etc/GMT+5', 'shortOffset', w), get('Asia/Kolkata', 'shortOffset', w)];
	T.z = {};
	for (const z of TZN_ZONES) {
		let v;
		try { v = ['short', 'long', 'shortGeneric', 'longGeneric'].map((n) => [get(z, n, w), get(z, n, s)]); } catch (e) { continue; }
		T.z[z] = [v[0][0], v[0][1], v[1][0], v[1][1], v[2][0], v[3][0]];
	}
	/* the offsets ("GMT+1", "UTC−05:00") are made by intl.js: 0 */
	for (const z of Object.keys(T.z))
		T.z[z] = T.z[z].map((v) => (/^(GMT|UTC)[+\-\u2212]\d/.test(v) ? 0 : v));
	return T;
}

/* ---- the output ------------------------------------------------------------------------------ */

/* a regional locale's section: the leaves (paths) that differ from its language's, { path: value } */
function flat(o, pre, out) {
	for (const k of Object.keys(o)) {
		const v = o[k], p = pre ? pre + '.' + k : k;
		if (v && typeof v === 'object') flat(v, p, out);
		else out[p] = v;
	}
	return out;
}
function diffLeaves(a, b) {
	const fa = flat(a, '', {}), fb = flat(b, '', {});
	const out = {};
	for (const k of Object.keys(fa)) if (fa[k] !== fb[k]) out[k] = fa[k];
	for (const k of Object.keys(fb)) if (!(k in fa)) out[k] = null;
	return out;
}
function diff(a, b) {
	/* the keys of a that differ from b (shallow; objects one level deeper) */
	if (!b) return a;
	const out = {};
	for (const k of Object.keys(a)) {
		const va = a[k], vb = b[k];
		if (JSON.stringify(va) === JSON.stringify(vb)) continue;
		if (va && vb && typeof va === 'object' && !Array.isArray(va) && typeof vb === 'object' && !Array.isArray(vb)) {
			const o = {};
			for (const k2 of Object.keys(va)) if (JSON.stringify(va[k2]) !== JSON.stringify(vb[k2])) o[k2] = va[k2];
			out[k] = Object.assign({ '+': 1 }, o);	/* '+': merge into the parent's */
		} else {
			out[k] = va;
		}
	}
	return out;
}

/* a base locale's time zone names: a table of its strings, each zone "i,j,..." (0: the offset) */
function tzIndex(T) {
	const tab = [], idx = new Map();
	const z = {};
	for (const k of Object.keys(T.z)) {
		z[k] = T.z[k].map((v) => {
			if (!v) return 0;
			if (!idx.has(v)) { idx.set(v, tab.length + 1); tab.push(v); }
			return idx.get(v);
		}).join(',').replace(/(,0)+$/, '');
	}
	return { g: T.g, s: tab, z };
}

function main() {
	const rec = [];	/* "key\tjson" */
	const full = {};
	const sections = { n: numberData, d: dateData, u: unitData, r: relData, l: listData, dn: namesData, z: tzNames };
	for (const l of LOCALES) {
		full[l] = {};
		for (const [k, f] of Object.entries(sections)) {
			if ((k === 'dn' || k === 'z' || k === 'u') && LEAN.includes(l)) continue;
			full[l][k] = f(l);
			const b = l.includes('-') && full[base(l)][k] ? full[base(l)][k] : null;
			let d;
			if (!b) d = k === 'z' ? tzIndex(full[l][k]) : full[l][k];
			else if (k === 'z') {
				/* a regional locale: its zones whose names differ, spelled out */
				d = {};
				if (JSON.stringify(full[l][k].g) !== JSON.stringify(b.g)) d.g = full[l][k].g;
				const zz = {};
				for (const z of Object.keys(full[l][k].z)) if (JSON.stringify(full[l][k].z[z]) !== JSON.stringify(b.z[z])) zz[z] = full[l][k].z[z];
				if (Object.keys(zz).length) d.zz = zz;
			} else d = diffLeaves(full[l][k], b);
			if (Object.keys(d).length) rec.push(l + '/' + k + '\t' + JSON.stringify(d));
		}
	}
	const meta = {};
	/* the currencies' fraction digits (other than 2) */
	const cd = {};
	for (const c of Intl.supportedValuesOf('currency')) {
		const d = new Intl.NumberFormat('en', { style: 'currency', currency: c }).resolvedOptions().maximumFractionDigits;
		if (d !== 2) cd[c] = d;
	}
	meta.v = { cldr: process.versions.cldr, icu: process.versions.icu, tz: process.versions.tz };
	meta.locales = LOCALES.join(' ');
	meta.lean = LEAN.join(' ');
	meta.cd = cd;
	meta.cur = Intl.supportedValuesOf('currency').join(' ');
	meta.units = UNITS.join(' ');
	meta.langs = LANGS.join(' ');
	meta.regions = REGIONS.join(' ');
	meta.scripts = SCRIPTS.join(' ');
	meta.cals = CALS.join(' ');
	meta.fields = FIELDS.join(' ');
	meta.clist = CURRENCIES.join(' ');
	/* the numbering systems with simple digits: their zero's code point (hanidec: its ten digits) */
	meta.nu = {};
	for (const nu of Intl.supportedValuesOf('numberingSystem')) {
		const d = [...new Intl.NumberFormat('en', { numberingSystem: nu, useGrouping: false }).format(1234567890)];
		const digits = d.slice(9).concat(d.slice(0, 9));
		if (new Intl.NumberFormat('en', { numberingSystem: nu }).resolvedOptions().numberingSystem !== nu) continue;
		const z = digits[0].codePointAt(0);
		meta.nu[nu] = digits.every((c, i) => c.codePointAt(0) === z + i) ? z : digits.join('');
	}
	/* likely subtags: language -> script-region, und-region -> language-script, und-script -> language-region */
	meta.likely = {};
	for (const t of LANGS) {
		if (t.includes('-')) continue;
		const m = new Intl.Locale(t).maximize();
		meta.likely[t] = m.script + '-' + m.region;
	}
	for (const r of REGIONS) {
		if (!/^[A-Z]{2}$|^419$/.test(r)) continue;
		try { const m = new Intl.Locale('und-' + r).maximize(); meta.likely['und-' + r] = m.language + '-' + m.script; } catch (e) { /* none */ }
	}
	for (const sc of SCRIPTS) {
		try { const m = new Intl.Locale('und-' + sc).maximize(); if (m.language !== 'und') meta.likely['und-' + sc] = m.language + '-' + m.region; } catch (e) { /* none */ }
	}
	meta.likely['und'] = 'en-Latn-US';
	rec.unshift('meta\t' + JSON.stringify(meta));
	const tz = tzData();
	if (tz.unknown.length) process.stderr.write('zones without a rule: ' + tz.unknown.join(', ') + '\n');
	rec.push('tz\t' + JSON.stringify({ rules: RULES, zones: tz.Z, links: tzLinks() }));
	process.stdout.write('# Generated by tools/tests/netsurf/intl/gendata.js from CLDR ' + process.versions.cldr + ' (ICU ' +
		process.versions.icu + ', tz ' + process.versions.tz + ') -- do not edit. Unicode License v3: LICENSE.\n# key<TAB>JSON\n' +
		rec.join('\n') + '\n');
}

/* aliases: zone names Node accepts that are not in its canonical list, mapped to the canonical */
function tzLinks() {
	const names = ['Asia/Kolkata', 'Asia/Calcutta', 'Europe/Kyiv', 'Europe/Kiev', 'America/Buenos_Aires', 'America/Argentina/Buenos_Aires',
		'Asia/Saigon', 'Asia/Ho_Chi_Minh', 'Asia/Katmandu', 'Asia/Kathmandu', 'Asia/Rangoon', 'Asia/Yangon', 'US/Eastern', 'US/Central',
		'US/Mountain', 'US/Pacific', 'US/Alaska', 'US/Hawaii', 'US/Arizona', 'Canada/Eastern', 'Canada/Pacific', 'Canada/Central',
		'Canada/Mountain', 'Canada/Atlantic', 'GB', 'Europe/Belfast', 'Etc/UTC', 'Etc/GMT', 'GMT', 'Etc/Universal', 'Universal', 'Zulu',
		'Etc/Zulu', 'Etc/UCT', 'UCT', 'Etc/Greenwich', 'Greenwich', 'Etc/GMT0', 'GMT0', 'Etc/GMT+0', 'Etc/GMT-0', 'Japan', 'PRC', 'ROC',
		'ROK', 'Singapore', 'Hongkong', 'Israel', 'Iran', 'Egypt', 'Turkey', 'Poland', 'Portugal', 'Eire', 'Iceland', 'NZ',
		'Australia/ACT', 'Australia/NSW', 'Australia/Canberra', 'Australia/Victoria', 'Australia/Queensland', 'Australia/West',
		'Australia/South', 'Australia/North', 'Australia/Tasmania', 'Brazil/East', 'Mexico/General', 'Chile/Continental', 'Cuba',
		'Jamaica', 'Navajo', 'America/Indianapolis', 'America/Fort_Wayne', 'America/Louisville', 'Asia/Istanbul', 'Europe/Nicosia',
		'Asia/Chongqing', 'Asia/Harbin', 'Asia/Macao', 'Asia/Thimbu', 'Asia/Ulan_Bator', 'Asia/Dacca', 'Asia/Tel_Aviv', 'Atlantic/Faeroe',
		'Pacific/Samoa', 'America/Godthab', 'America/Nuuk', 'Europe/Uzhgorod', 'Europe/Zaporozhye', 'America/Montreal', 'EST5EDT',
		'CST6CDT', 'MST7MDT', 'PST8PDT', 'EST', 'MST', 'HST', 'CET', 'EET', 'MET', 'WET'];
	const canon = new Set(Intl.supportedValuesOf('timeZone'));
	const out = {};
	for (const n of names) {
		let r;
		try { r = new Intl.DateTimeFormat('en', { timeZone: n }).resolvedOptions().timeZone; } catch (e) { continue; }
		if (!canon.has(n)) out[n] = r;
	}
	return out;
}

main();
