/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: Intl for QuickJS (ECMA-402), which is built without it -- Intl.DateTimeFormat,
 * NumberFormat, PluralRules, RelativeTimeFormat, ListFormat, Collator, Segmenter, DisplayNames,
 * Locale, getCanonicalLocales, supportedValuesOf -- and the built-ins that
 * depend on it (Number / BigInt / Date / Array / TypedArray toLocaleString, String's
 * localeCompare and toLocale{Lower,Upper}Case).
 *
 * Two parts, split by qjs_intl.h at the marker line (QJS_INTL_MARK):
 *  - the boot (evaluated in every document's context, before dom.js): the Intl object, whose
 *    members are accessors that load the implementation at their first use, and the built-ins;
 *  - the implementation (evaluated at the first use): a function of the natives (N.data: a
 *    record of the locale data, third_party/cldr-48/intl-data.txt; N.zone: the host's time
 *    zone name) returning the members.
 * The locale data is read (JSON.parse) a section at a time, when a formatter needs it.
 */
(function (load) {
'use strict';
const G = globalThis;
let impl = null;
const I = () => impl || (impl = load());
const NAMES = ['Collator', 'DateTimeFormat', 'DisplayNames', 'ListFormat', 'Locale', 'NumberFormat',
	'PluralRules', 'RelativeTimeFormat', 'Segmenter', 'getCanonicalLocales', 'supportedValuesOf'];
const Intl = {};
function real() {
	const x = I();
	for (const n of NAMES)
		Object.defineProperty(Intl, n, { value: x[n], writable: true, enumerable: false, configurable: true });
}
for (const n of NAMES) {
	Object.defineProperty(Intl, n, {
		get() { real(); return Intl[n]; },
		set(v) { real(); Intl[n] = v; },
		enumerable: false, configurable: true,
	});
}
Object.defineProperty(Intl, Symbol.toStringTag, { value: 'Intl', configurable: true });
Object.defineProperty(G, 'Intl', { value: Intl, writable: true, enumerable: false, configurable: true });

/* the built-ins: their ECMA-402 versions (same names, lengths, attributes) */
function method(obj, name, f) {
	Object.defineProperty(obj, name, { value: f, writable: true, enumerable: false, configurable: true });
}
const TA = Object.getPrototypeOf(Int8Array.prototype);
const B = {
	toLocaleString() { return I().numberLocale(this, arguments[0], arguments[1]); },
};
method(Number.prototype, 'toLocaleString', B.toLocaleString);
method(BigInt.prototype, 'toLocaleString', { toLocaleString() { return I().bigintLocale(this, arguments[0], arguments[1]); } }.toLocaleString);
method(Date.prototype, 'toLocaleString', { toLocaleString() { return I().dateLocale(this, arguments[0], arguments[1], 'any', 'all'); } }.toLocaleString);
method(Date.prototype, 'toLocaleDateString', { toLocaleDateString() { return I().dateLocale(this, arguments[0], arguments[1], 'date', 'date'); } }.toLocaleDateString);
method(Date.prototype, 'toLocaleTimeString', { toLocaleTimeString() { return I().dateLocale(this, arguments[0], arguments[1], 'time', 'time'); } }.toLocaleTimeString);
method(String.prototype, 'localeCompare', { localeCompare(that) { return I().localeCompare(this, that, arguments[1], arguments[2]); } }.localeCompare);
method(String.prototype, 'toLocaleLowerCase', { toLocaleLowerCase() { return I().localeCase(this, arguments[0], false); } }.toLocaleLowerCase);
method(String.prototype, 'toLocaleUpperCase', { toLocaleUpperCase() { return I().localeCase(this, arguments[0], true); } }.toLocaleUpperCase);
method(Array.prototype, 'toLocaleString', { toLocaleString() { return I().arrayLocale(this, arguments[0], arguments[1], false); } }.toLocaleString);
method(TA, 'toLocaleString', { toLocaleString() { return I().arrayLocale(this, arguments[0], arguments[1], true); } }.toLocaleString);
})
//@@INTL-IMPL@@
(function (N) {
'use strict';

/* ==== helpers ==================================================================================== */

const G = globalThis;
const OP = Object.prototype;
const hasOwn = (o, k) => OP.hasOwnProperty.call(o, k);
const isObj = (v) => (typeof v === 'object' && v !== null) || typeof v === 'function';
/* a[a.length] = v, as CreateDataProperty (a page's Array.prototype setters or push not called) */
function append(a, v) { Object.defineProperty(a, a.length, { value: v, writable: true, enumerable: true, configurable: true }); }
function def(obj, props) {
	for (const k of Reflect.ownKeys(props)) {
		const d = Object.getOwnPropertyDescriptor(props, k);
		d.enumerable = false;
		Object.defineProperty(obj, k, d);
	}
	return obj;
}
function tag(obj, name) { Object.defineProperty(obj, Symbol.toStringTag, { value: name, configurable: true }); }
function ToObject(v) {
	if (v === undefined || v === null) throw new TypeError('Cannot convert undefined or null to object');
	return Object(v);
}
function ToString(v) {
	if (typeof v === 'symbol') throw new TypeError('Cannot convert a Symbol value to a string');
	return String(v);
}
function ToNumber(v) {
	if (typeof v === 'bigint') throw new TypeError('Cannot convert a BigInt value to a number');
	return +v;
}
function ToIntegerOrInfinity(v) {
	const n = ToNumber(v);
	if (Number.isNaN(n) || n === 0) return 0;
	if (!Number.isFinite(n)) return n;
	return Math.trunc(n);
}
function ToLength(v) {
	const n = ToIntegerOrInfinity(v);
	return n <= 0 ? 0 : Math.min(n, 2 ** 53 - 1);
}
/* GetOption (options, name, type, values, default) */
function GetOption(opts, name, type, values, dflt) {
	let v = opts[name];
	if (v === undefined) return dflt;
	if (type === 'boolean') v = Boolean(v);
	else if (type === 'number') { v = ToNumber(v); if (Number.isNaN(v)) throw new RangeError(name + ' is NaN'); }
	else v = ToString(v);
	if (values !== undefined && !values.includes(v)) throw new RangeError('Value ' + String(v) + ' out of range for ' + name);
	return v;
}
/* GetBooleanOrStringNumberFormatOption */
function GetBoolOrStr(opts, name, strs, truthy, falsy, dflt) {
	let v = opts[name];
	if (v === undefined) return dflt;
	if (v === true) return truthy;
	if (!v) return falsy;
	v = ToString(v);
	if (v === 'true' || v === 'false') return dflt;
	if (!strs.includes(v)) throw new RangeError('Value ' + v + ' out of range for ' + name);
	return v;
}
function DefaultNumberOption(v, min, max, fallback, name) {
	if (v === undefined) return fallback;
	v = ToNumber(v);
	if (Number.isNaN(v) || v < min || v > max) throw new RangeError((name || 'value') + ' out of range');
	return Math.floor(v);
}
function GetNumberOption(opts, name, min, max, fallback) {
	return DefaultNumberOption(opts[name], min, max, fallback, name);
}
function GetOptionsObject(o) {
	if (o === undefined) return Object.create(null);
	if (isObj(o)) return o;
	throw new TypeError('Options must be an object');
}
function CoerceOptionsToObject(o) {
	return o === undefined ? Object.create(null) : ToObject(o);
}
/* the internal slots of Intl's objects: a WeakMap per kind */
function slots() {
	const m = new WeakMap();
	return {
		set(o, s) { m.set(o, s); },
		get(o, what) {
			const s = isObj(o) ? m.get(o) : undefined;
			if (!s) throw new TypeError((what || 'method') + ' called on an incompatible receiver');
			return s;
		},
		has(o) { return isObj(o) && m.has(o); },
	};
}
/* a constructor made from a plain function: its length, its prototype (non-writable) */
function ctor(f, len, protoProps, name) {
	Object.defineProperty(f, 'length', { value: len, configurable: true });
	const proto = f.prototype;
	def(proto, protoProps);
	tag(proto, 'Intl.' + name);
	Object.defineProperty(f, 'prototype', { writable: false, enumerable: false, configurable: false });
	return f;
}
function OrdinaryCreateFromConstructor(newTarget, dflt) {
	let proto = newTarget.prototype;
	if (!isObj(proto)) {
		/* another realm's default: ours */
		proto = dflt;
	}
	return Object.create(proto);
}

/* ==== the locale data ============================================================================== */

function rec(key) {
	const t = N.data(key);
	return t == null ? null : JSON.parse(t);
}
const META = rec('meta');
const DATA_LOCS = new Set(META.locales.split(' '));
const LANGS_WITH_DATA = new Set(META.locales.split(' ').map((l) => l.split('-')[0]));
const LEAN = new Set(META.lean.split(' '));
const SEC = new Map();
function patchLeaves(o, d) {
	for (const path of Object.keys(d)) {
		const ks = path.split('.');
		let t = o;
		for (let i = 0; i < ks.length - 1; i++) {
			if (!isObj(t[ks[i]])) t[ks[i]] = {};
			t = t[ks[i]];
		}
		if (d[path] === null) delete t[ks[ks.length - 1]];
		else t[ks[ks.length - 1]] = d[path];
	}
	return o;
}
function decodeZ(z) {
	const out = { g: z.g, z: {} };
	for (const k of Object.keys(z.z)) out.z[k] = String(z.z[k]).split(',').map((i) => (i === '0' || i === '' ? 0 : z.s[i - 1]));
	return out;
}
/* a section (n d u r l dn z) of a data locale (en, en-GB...): merged with its language's */
function sec(loc, k) {
	const key = loc + '/' + k;
	let v = SEC.get(key);
	if (v) return v;
	const b = loc.split('-')[0];
	if (b !== loc) {
		const d = rec(key);
		if (!d) v = sec(b, k);
		else if (k === 'z') {
			v = sec(b, k);
			v = { g: d.g || v.g, z: Object.assign({}, v.z, d.zz || {}) };
		} else {
			v = rec(b + '/' + k);
			if (!v) v = rec('en/' + k);
			patchLeaves(v, d);
		}
	} else {
		v = rec(key);
		if (!v) v = sec('en', k);	/* (the lean languages: English's units, names) */
		else if (k === 'z') v = decodeZ(v);
	}
	SEC.set(key, v);
	return v;
}

/* ==== language tags ================================================================================ */

const LANG_ALIAS = {
	iw: 'he', in: 'id', ji: 'yi', jw: 'jv', mo: 'ro', tl: 'fil', aju: 'jrb', als: 'sq', arb: 'ar', ayr: 'ay', azj: 'az',
	bcc: 'bal', bcl: 'bik', bxk: 'luy', bxr: 'bua', cld: 'syr', cmn: 'zh', cwd: 'cr', dgo: 'doi', dhd: 'mwr', dik: 'din',
	diq: 'zza', lbk: 'bnc', drh: 'mn', ekk: 'et', emk: 'man', esk: 'ik', fat: 'ak', fuc: 'ff', gaz: 'om', gbo: 'grb',
	gno: 'gon', gug: 'gn', gya: 'gba', hdn: 'hai', hea: 'hmn', ike: 'iu', kmr: 'ku', knc: 'kr', kng: 'kg', knn: 'kok',
	kpv: 'kv', lvs: 'lv', mhr: 'chm', mup: 'raj', khk: 'mn', npi: 'ne', ojg: 'oj', ory: 'or', pbu: 'ps', pes: 'fa',
	plt: 'mg', pnb: 'lah', quz: 'qu', rmy: 'rom', spy: 'kln', src: 'sc', swh: 'sw', ttq: 'tmh', tw: 'ak', umu: 'del',
	uzn: 'uz', xpe: 'kpe', xsl: 'den', ydd: 'yi', zai: 'zap', zsm: 'ms', zyb: 'za', him: 'srx', mnk: 'man', bh: 'bho',
	aam: 'aas', adp: 'dz', aue: 'ktz', ayx: 'nun', bjd: 'drl', ccq: 'rki', cjr: 'mom', cka: 'cmr', cmk: 'xch',
	coy: 'pij', cqu: 'quh', drw: 'fa-AF', gav: 'dev', gfx: 'vaj', ggn: 'gvr', gti: 'nyc', guv: 'duz', ibi: 'opa',
	ilw: 'gal', jeg: 'oyb', kgc: 'tdf', kgh: 'kml', koj: 'kwv', krm: 'bmf', ktr: 'dtp', kvs: 'gdj', kwq: 'yam',
	kxe: 'tvd', kzj: 'dtp', kzt: 'dtp', lii: 'raq', lmm: 'rmx', meg: 'cir', mst: 'mry', mwj: 'vaj', myt: 'mry',
	nad: 'xny', ncp: 'kdz', nnx: 'ngv', nts: 'pij', oun: 'vaj', pcr: 'adx', pmc: 'huw', pmu: 'phr', ppa: 'bfy',
	ppr: 'lcq', pry: 'prt', puz: 'pub', sca: 'hle', skk: 'oyb', tdu: 'dtp', thc: 'tpo', thx: 'oyb', tie: 'ras',
	tkk: 'twm', tlw: 'weo', tmp: 'tyj', tne: 'kak', tnf: 'fa-AF', tsf: 'taj', uok: 'ema', xba: 'cax', xia: 'acn',
	xkh: 'waw', xrq: 'dmw', ybd: 'rki', yma: 'lrr', ymt: 'mtm', yos: 'zom', yuu: 'yug',
	aar: 'aa', abk: 'ab', afr: 'af', aka: 'ak', amh: 'am', ara: 'ar', arg: 'an', asm: 'as', ava: 'av', ave: 'ae',
	aym: 'ay', aze: 'az', bak: 'ba', bam: 'bm', bel: 'be', ben: 'bn', bis: 'bi', bod: 'bo', bos: 'bs', bre: 'br',
	bul: 'bg', cat: 'ca', ces: 'cs', cha: 'ch', che: 'ce', chu: 'cu', chv: 'cv', cor: 'kw', cos: 'co', cre: 'cr',
	cym: 'cy', dan: 'da', deu: 'de', div: 'dv', dzo: 'dz', ell: 'el', eng: 'en', epo: 'eo', est: 'et', eus: 'eu',
	ewe: 'ee', fao: 'fo', fas: 'fa', fij: 'fj', fin: 'fi', fra: 'fr', fry: 'fy', ful: 'ff', gla: 'gd', gle: 'ga',
	glg: 'gl', glv: 'gv', grn: 'gn', guj: 'gu', hat: 'ht', hau: 'ha', heb: 'he', her: 'hz', hin: 'hi', hmo: 'ho',
	hrv: 'hr', hun: 'hu', hye: 'hy', ibo: 'ig', ido: 'io', iii: 'ii', iku: 'iu', ile: 'ie', ina: 'ia', ind: 'id',
	ipk: 'ik', isl: 'is', ita: 'it', jav: 'jv', jpn: 'ja', kal: 'kl', kan: 'kn', kas: 'ks', kat: 'ka', kau: 'kr',
	kaz: 'kk', khm: 'km', kik: 'ki', kin: 'rw', kir: 'ky', kom: 'kv', kon: 'kg', kor: 'ko', kua: 'kj', kur: 'ku',
	lao: 'lo', lat: 'la', lav: 'lv', lim: 'li', lin: 'ln', lit: 'lt', ltz: 'lb', lub: 'lu', lug: 'lg', mah: 'mh',
	mal: 'ml', mar: 'mr', mkd: 'mk', mlg: 'mg', mlt: 'mt', mon: 'mn', mri: 'mi', msa: 'ms', mya: 'my', nau: 'na',
	nav: 'nv', nbl: 'nr', nde: 'nd', ndo: 'ng', nep: 'ne', nld: 'nl', nno: 'nn', nob: 'nb', nor: 'no', nya: 'ny',
	oci: 'oc', oji: 'oj', ori: 'or', orm: 'om', oss: 'os', pan: 'pa', pli: 'pi', pol: 'pl', por: 'pt', pus: 'ps',
	que: 'qu', roh: 'rm', ron: 'ro', run: 'rn', rus: 'ru', sag: 'sg', san: 'sa', sin: 'si', slk: 'sk', slv: 'sl',
	sme: 'se', smo: 'sm', sna: 'sn', snd: 'sd', som: 'so', sot: 'st', spa: 'es', sqi: 'sq', srd: 'sc', srp: 'sr',
	ssw: 'ss', sun: 'su', swa: 'sw', swe: 'sv', tah: 'ty', tam: 'ta', tat: 'tt', tel: 'te', tgk: 'tg', tgl: 'fil',
	tha: 'th', tir: 'ti', ton: 'to', tsn: 'tn', tso: 'ts', tuk: 'tk', tur: 'tr', twi: 'ak', uig: 'ug', ukr: 'uk',
	urd: 'ur', uzb: 'uz', ven: 've', vie: 'vi', vol: 'vo', wln: 'wa', wol: 'wo', xho: 'xh', yid: 'yi', yor: 'yo',
	zha: 'za', zho: 'zh', zul: 'zu', alb: 'sq', arm: 'hy', baq: 'eu', bur: 'my', chi: 'zh', cze: 'cs', dut: 'nl',
	fre: 'fr', geo: 'ka', ger: 'de', gre: 'el', ice: 'is', mac: 'mk', mao: 'mi', may: 'ms', per: 'fa', rum: 'ro',
	slo: 'sk', tib: 'bo', wel: 'cy', sh: 'sr-Latn', cnr: 'sr-ME', hbs: 'sr-Latn', no: 'no', aze_: 0,
};
delete LANG_ALIAS.aze_;
const REGION_ALIAS = {
	BU: 'MM', CT: 'KI', DD: 'DE', DY: 'BJ', FQ: 'AQ', FX: 'FR', HV: 'BF', JT: 'UM', MI: 'UM', NH: 'VU', NQ: 'AQ',
	PU: 'UM', PZ: 'PA', QU: 'EU', RH: 'ZW', TP: 'TL', UK: 'GB', VD: 'VN', WK: 'UM', YD: 'YE', YU: 'RS', ZR: 'CD',
	CS: 'RS', AN: 'CW', SU: 'RU', NT: 'SA',
	'004': 'AF', '008': 'AL', '010': 'AQ', '012': 'DZ', '016': 'AS', '020': 'AD', '024': 'AO', '028': 'AG', '031': 'AZ',
	'032': 'AR', '036': 'AU', '040': 'AT', '044': 'BS', '048': 'BH', '050': 'BD', '051': 'AM', '052': 'BB', '056': 'BE',
	'060': 'BM', '064': 'BT', '068': 'BO', '070': 'BA', '072': 'BW', '076': 'BR', '084': 'BZ', '090': 'SB', '092': 'VG',
	'096': 'BN', '100': 'BG', '104': 'MM', '108': 'BI', '112': 'BY', '116': 'KH', '120': 'CM', '124': 'CA', '132': 'CV',
	'136': 'KY', '140': 'CF', '144': 'LK', '148': 'TD', '152': 'CL', '156': 'CN', '158': 'TW', '170': 'CO', '178': 'CG',
	'180': 'CD', '188': 'CR', '191': 'HR', '192': 'CU', '196': 'CY', '203': 'CZ', '208': 'DK', '214': 'DO', '218': 'EC',
	'222': 'SV', '231': 'ET', '232': 'ER', '233': 'EE', '242': 'FJ', '246': 'FI', '250': 'FR', '268': 'GE', '276': 'DE',
	'288': 'GH', '300': 'GR', '320': 'GT', '324': 'GN', '332': 'HT', '340': 'HN', '344': 'HK', '348': 'HU', '352': 'IS',
	'356': 'IN', '360': 'ID', '364': 'IR', '368': 'IQ', '372': 'IE', '376': 'IL', '380': 'IT', '388': 'JM', '392': 'JP',
	'398': 'KZ', '400': 'JO', '404': 'KE', '408': 'KP', '410': 'KR', '414': 'KW', '417': 'KG', '418': 'LA', '422': 'LB',
	'428': 'LV', '430': 'LR', '434': 'LY', '440': 'LT', '442': 'LU', '446': 'MO', '450': 'MG', '458': 'MY', '470': 'MT',
	'484': 'MX', '492': 'MC', '496': 'MN', '498': 'MD', '499': 'ME', '504': 'MA', '508': 'MZ', '512': 'OM', '516': 'NA',
	'524': 'NP', '528': 'NL', '554': 'NZ', '558': 'NI', '562': 'NE', '566': 'NG', '578': 'NO', '586': 'PK', '591': 'PA',
	'598': 'PG', '600': 'PY', '604': 'PE', '608': 'PH', '616': 'PL', '620': 'PT', '630': 'PR', '634': 'QA', '642': 'RO',
	'643': 'RU', '646': 'RW', '682': 'SA', '686': 'SN', '688': 'RS', '702': 'SG', '703': 'SK', '704': 'VN', '705': 'SI',
	'706': 'SO', '710': 'ZA', '716': 'ZW', '724': 'ES', '752': 'SE', '756': 'CH', '760': 'SY', '762': 'TJ', '764': 'TH',
	'780': 'TT', '784': 'AE', '788': 'TN', '792': 'TR', '795': 'TM', '800': 'UG', '804': 'UA', '807': 'MK', '818': 'EG',
	'826': 'GB', '834': 'TZ', '840': 'US', '854': 'BF', '858': 'UY', '860': 'UZ', '862': 'VE', '887': 'YE', '894': 'ZM',
};
/* complex region aliases: the replacement by the language's likely region */
const REGION_MULTI = { SU: 'RU AM AZ BY EE GE KZ KG LV LT MD TJ TM UA UZ', NT: 'SA IQ', YU: 'RS ME', CS: 'RS ME',
	AN: 'CW SX BQ', '172': 'RU AM AZ BY GE KG KZ MD TJ TM UA UZ', '062': '034 143', '200': 'CZ SK', '810': 'RU AM AZ BY EE GE KZ KG LV LT MD TJ TM UA UZ',
	'890': 'RS ME SI HR MK BA', '891': 'RS ME', '230': 'ET', '280': 'DE', '532': 'CW SX BQ', '582': 'FM MH MP PW', '736': 'SD', '886': 'YE' };
const VARIANT_ALIAS = { heploc: 'alalc97', polytoni: 'polyton' };
const GRANDFATHERED = { 'art-lojban': 'jbo', 'cel-gaulish': 'xtg', 'zh-guoyu': 'zh', 'zh-hakka': 'hak', 'zh-xiang': 'hsn' };
/* the unicode extension values' aliases */
const U_ALIAS = {
	ca: { 'ethiopic-amete-alem': 'ethioaa', islamicc: 'islamic-civil' },
	ks: { primary: 'level1', tertiary: 'level3' },
	ms: { imperial: 'uksystem' },
	kb: { yes: 'true' }, kc: { yes: 'true' }, kh: { yes: 'true' }, kk: { yes: 'true' }, kn: { yes: 'true' },
	tz: { cnckg: 'cnsha', eire: 'iedub', est: 'utcw05', gmt0: 'gmt', uct: 'utc', zulu: 'utc', aqams: 'nzakl', cnhrb: 'cnsha', cnkhg: 'cnurc', usnavajo: 'usden' },
};
const T_ALIAS = { m0: { names: 'prprname' }, d0: { name: 'charname' } };

const ALPHA = /^[a-z]+$/i, DIGIT = /^[0-9]+$/, ALNUM = /^[a-z0-9]+$/i;
function isLang(s) { return ALPHA.test(s) && (s.length >= 2 && s.length <= 3 || s.length >= 5 && s.length <= 8); }
function isScript(s) { return s.length === 4 && ALPHA.test(s); }
function isRegion(s) { return (s.length === 2 && ALPHA.test(s)) || (s.length === 3 && DIGIT.test(s)); }
function isVariant(s) { return ALNUM.test(s) && ((s.length >= 5 && s.length <= 8) || (s.length === 4 && /^[0-9]/.test(s))); }

/* parse a (lower-cased) unicode_locale_id -> { lang, script, region, variants, u: { attrs, kw }, t, other, x } or null */
function parseTag(str) {
	if (str === '' || /[^a-z0-9-]/i.test(str) || str[0] === '-' || str[str.length - 1] === '-' || str.includes('--')) return null;
	const s = str.toLowerCase().split('-');
	let i = 0;
	const r = { lang: '', script: '', region: '', variants: [], u: null, t: null, other: [], x: '' };
	if (!isLang(s[0])) return null;
	r.lang = s[i++];
	if (i < s.length && isScript(s[i])) r.script = s[i++];
	if (i < s.length && isRegion(s[i])) r.region = s[i++];
	while (i < s.length && isVariant(s[i])) {
		if (r.variants.includes(s[i])) return null;
		r.variants.push(s[i++]);
	}
	const seen = new Set();
	while (i < s.length) {
		const sg = s[i];
		if (sg.length !== 1) return null;
		if (sg === 'x') {
			const rest = s.slice(i + 1);
			if (!rest.length || rest.some((p) => !ALNUM.test(p) || p.length > 8)) return null;
			r.x = rest.join('-');
			i = s.length;
			break;
		}
		if (seen.has(sg)) return null;
		seen.add(sg);
		i++;
		if (sg === 'u') {
			const u = { attrs: [], kw: [] };
			while (i < s.length && s[i].length >= 3 && s[i].length <= 8 && ALNUM.test(s[i])) u.attrs.push(s[i++]);
			while (i < s.length && s[i].length === 2) {
				if (!/^[a-z0-9][a-z]$/.test(s[i])) return null;
				const key = s[i++];
				const vals = [];
				while (i < s.length && s[i].length >= 3 && s[i].length <= 8 && ALNUM.test(s[i])) vals.push(s[i++]);
				u.kw.push([key, vals.join('-')]);
			}
			if (!u.attrs.length && !u.kw.length) return null;
			r.u = u;
		} else if (sg === 't') {
			const t = { lang: null, fields: [] };
			if (i < s.length && isLang(s[i])) {
				const tl = { lang: s[i++], script: '', region: '', variants: [] };
				if (i < s.length && isScript(s[i])) tl.script = s[i++];
				if (i < s.length && isRegion(s[i])) tl.region = s[i++];
				while (i < s.length && isVariant(s[i])) {
					if (tl.variants.includes(s[i])) return null;
					tl.variants.push(s[i++]);
				}
				t.lang = tl;
			}
			while (i < s.length && /^[a-z][0-9]$/.test(s[i])) {
				const key = s[i++];
				const vals = [];
				while (i < s.length && s[i].length >= 3 && s[i].length <= 8 && ALNUM.test(s[i])) vals.push(s[i++]);
				if (!vals.length) return null;
				t.fields.push([key, vals.join('-')]);
			}
			if (!t.lang && !t.fields.length) return null;
			r.t = t;
		} else {
			const vals = [];
			while (i < s.length && s[i].length >= 2 && s[i].length <= 8 && ALNUM.test(s[i])) vals.push(s[i++]);
			if (!vals.length) return null;
			r.other.push([sg, vals.join('-')]);
		}
	}
	return r;
}
function langIdString(r) {
	let s = r.lang;
	if (r.script) s += '-' + r.script[0].toUpperCase() + r.script.slice(1);
	if (r.region) s += '-' + r.region.toUpperCase();
	for (const v of r.variants) s += '-' + v;
	return s;
}
/* the language id's aliases (language, region, variants) -- in place */
function canonLangId(r, likelyLang) {
	/* language + variant aliases */
	if (r.lang === 'sgn' && r.region) {
		/* sign languages by region: a few */
		const SGN = { gb: 'bfi', us: 'ase', fr: 'fsl', de: 'gsg', br: 'bzs', jp: 'jsl' };
		const k = r.region.toLowerCase();
		if (SGN[k]) { r.lang = SGN[k]; r.region = ''; }
	}
	if (r.lang === 'hy' && r.variants.includes('arevela')) { r.variants = r.variants.filter((v) => v !== 'arevela'); }
	if (r.lang === 'hy' && r.variants.includes('arevmda')) { r.lang = 'hyw'; r.variants = r.variants.filter((v) => v !== 'arevmda'); }
	const a = LANG_ALIAS[r.lang];
	if (a && a !== r.lang) {
		const p = a.split('-');
		r.lang = p[0];
		for (const x of p.slice(1)) {
			if (x.length === 4 && !r.script) r.script = x.toLowerCase();
			else if (x.length === 2 && !r.region) r.region = x.toLowerCase();
		}
	}
	if (r.region) {
		const R = r.region.toUpperCase();
		if (REGION_MULTI[R]) {
			const choices = REGION_MULTI[R].split(' ');
			const lk = likelyLang ? likelyLang(r) : null;
			r.region = (lk && choices.includes(lk) ? lk : choices[0]).toLowerCase();
		} else if (REGION_ALIAS[R]) r.region = REGION_ALIAS[R].toLowerCase();
	}
	r.variants = r.variants.map((v) => VARIANT_ALIAS[v] || v);
	if (r.variants.includes('aaland') && (!r.region || r.region === 'fi')) { r.region = 'ax'; r.variants = r.variants.filter((v) => v !== 'aaland'); }
	r.variants.sort();
	return r;
}
function likelyRegion(r) {
	const L = META.likely[r.lang + (r.script ? '' : '')];
	if (L) return L.split('-')[1];
	return null;
}
/* CanonicalizeUnicodeLocaleId -> the canonical string */
function canonicalize(r) {
	canonLangId(r, likelyRegion);
	let s = langIdString(r);
	const ext = [];
	for (const [sg, v] of r.other) ext.push([sg, sg + '-' + v]);
	if (r.t) {
		let t = 't';
		if (r.t.lang) {
			const tl = Object.assign({}, r.t.lang, { variants: r.t.lang.variants.slice() });
			canonLangId(tl);
			t += '-' + langIdString(tl).toLowerCase();
		}
		const f = r.t.fields.slice().sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
		for (const [k, v] of f) t += '-' + k + '-' + ((T_ALIAS[k] && T_ALIAS[k][v]) || v);
		ext.push(['t', t]);
	}
	if (r.u) {
		let u = 'u';
		const attrs = [...new Set(r.u.attrs)].sort();
		for (const a of attrs) u += '-' + a;
		const seen = new Set();
		const kw = [];
		for (const [k, v] of r.u.kw) if (!seen.has(k)) { seen.add(k); kw.push([k, v]); }
		kw.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
		for (let [k, v] of kw) {
			if (U_ALIAS[k] && U_ALIAS[k][v]) v = U_ALIAS[k][v];
			if ((k === 'rg' || k === 'sd') && v) {
				const sd = { no23: 'no50', cn11: 'cnbj', cz10a: 'cz110', fra: 'frges', frg: 'frges', lud: 'lucl', frbl: 'bl' };
				if (sd[v]) v = sd[v];
			}
			u += '-' + k + (v && v !== 'true' ? '-' + v : '');
		}
		ext.push(['u', u]);
	}
	ext.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
	for (const e of ext) s += '-' + e[1];
	if (r.x) s += '-x-' + r.x;
	return s;
}
function IsStructurallyValidLanguageTag(s) { return parseTag(s) !== null; }
function CanonicalizeTag(s) {
	const g = GRANDFATHERED[s.toLowerCase()];
	if (g) s = g;
	const r = parseTag(s);
	if (!r) throw new RangeError('Incorrect locale information provided');
	return canonicalize(r);
}
const CANON_CACHE = new Map();
function canonTag(s) {
	let c = CANON_CACHE.get(s);
	if (c === undefined) {
		c = CanonicalizeTag(s);
		if (CANON_CACHE.size < 200) CANON_CACHE.set(s, c);
	}
	return c;
}
function CanonicalizeLocaleList(locales) {
	if (locales === undefined) return [];
	const seen = [];
	let O;
	if (typeof locales === 'string' || LOCALE.has(locales)) O = [locales];
	else O = ToObject(locales);
	const len = ToLength(O.length);
	for (let k = 0; k < len; k++) {
		if (!(k in O)) continue;
		const v = O[k];
		if (typeof v !== 'string' && !isObj(v)) throw new TypeError('Locale must be a string or an object');
		const tagS = LOCALE.has(v) ? LOCALE.get(v).locale : ToString(v);
		const c = canonTag(tagS);
		if (!seen.includes(c)) append(seen, c);
	}
	return seen;
}

/* ---- availability: a language with data, a known region; its data locale ---- */

const REGIONS = new Set(META.regions.split(' '));
const SCRIPT_OF = { ja: 'jpan', zh: 'hans', ko: 'kore', ru: 'cyrl' };
function removeUnicodeExt(tag) {
	/* the tag without its -u- extension (and the others: the lookup is on the language id) */
	const i = tag.search(/-[a-wyz0-9]-/);
	return i < 0 ? tag : tag.slice(0, i);
}
function available(tag) {
	const r = parseTag(tag);
	if (!r || r.variants.length || r.u || r.t || r.other.length || r.x) return false;
	if (!LANGS_WITH_DATA.has(r.lang)) return false;
	if (r.script) {
		const sc = r.script;
		if (r.lang === 'zh') { if (sc !== 'hans' && sc !== 'hant') return false; }
		else if (sc !== (SCRIPT_OF[r.lang] || 'latn')) return false;
	}
	if (r.region && !REGIONS.has(r.region.toUpperCase())) return false;
	return true;
}
const EN_US_LIKE = new Set(['US', 'PR', 'PH', 'UM', 'VI', 'AS', 'GU', 'MP', 'MH', 'FM', 'PW']);
const DKEY_CACHE = new Map();
/* the data locale of an available tag: en-SG -> en-GB, es-CO -> es-419, zh-HK -> zh-TW, fr-LU -> fr */
function dataKey(tag) {
	let k = DKEY_CACHE.get(tag);
	if (k) return k;
	const r = parseTag(removeUnicodeExt(tag)) || { lang: 'en', script: '', region: '' };
	const R = r.region.toUpperCase();
	const lr = r.lang + (R ? '-' + R : '');
	if (DATA_LOCS.has(lr)) k = lr;
	else if (r.lang === 'en' && R && !EN_US_LIKE.has(R)) k = 'en-GB';
	else if (r.lang === 'es' && R && R !== 'ES' && R !== 'EA' && R !== 'IC' && R !== 'GQ' && R !== 'PH') k = 'es-419';
	else if (r.lang === 'pt' && R && R !== 'BR') k = 'pt-PT';
	else if (r.lang === 'zh' && (r.script === 'hant' || R === 'HK' || R === 'MO')) k = 'zh-TW';
	else if (r.lang === 'nl' && R === 'SR') k = 'nl';
	else if (DATA_LOCS.has(r.lang)) k = r.lang;
	else k = 'en';
	DKEY_CACHE.set(tag, k);
	return k;
}

let DEFAULT_LOCALE = null;
function DefaultLocale() {
	if (DEFAULT_LOCALE) return DEFAULT_LOCALE;
	let l = 'en-US';
	try {
		const nav = G.navigator;
		const v = nav && nav.language;
		if (typeof v === 'string' && v) {
			const c = CanonicalizeTag(v);
			const b = removeUnicodeExt(c);
			if (available(b)) l = b;
			else if (available(b.split('-')[0])) l = b.split('-')[0];
		}
	} catch (e) { /* the default */ }
	DEFAULT_LOCALE = l;
	return l;
}

function BestAvailableLocale(locale) {
	let c = locale;
	for (;;) {
		if (available(c)) return c;
		let pos = c.lastIndexOf('-');
		if (pos < 0) return undefined;
		if (pos >= 2 && c[pos - 2] === '-') pos -= 2;
		c = c.slice(0, pos);
	}
}
function LookupMatcher(requested) {
	for (const locale of requested) {
		const noExt = removeUnicodeExt(locale);
		const a = BestAvailableLocale(noExt);
		if (a !== undefined) {
			const r = { __proto__: null, locale: a };
			if (locale !== noExt) {
				const p = parseTag(locale);
				if (p && p.u) r.ext = p.u;
			}
			return r;
		}
	}
	return { __proto__: null, locale: DefaultLocale() };
}
/* ResolveLocale: the locale, the relevant extension keys' values */
function ResolveLocale(requested, opt, keys, keyData) {
	const r = LookupMatcher(requested);
	const found = r.locale;
	const result = { __proto__: null, dataLocale: found };
	let add = '';
	for (const key of keys) {
		const list = keyData(key, found);
		let value = list[0];
		let addition = '';
		if (r.ext) {
			const kw = r.ext.kw.find((k) => k[0] === key);
			if (kw) {
				let rv = kw[1];
				if (U_ALIAS[key] && U_ALIAS[key][rv]) rv = U_ALIAS[key][rv];
				if (rv !== '') {
					if (list.includes(rv)) { value = rv; addition = '-' + key + '-' + rv; }
				} else if (list.includes('true')) { value = 'true'; addition = '-' + key; }
			}
		}
		if (opt && key in opt) {
			let ov = opt[key];
			if (typeof ov === 'boolean') ov = String(ov);
			if (ov !== undefined && list.includes(ov) && ov !== value) { value = ov; addition = ''; }
		}
		result[key] = value;
		add += addition;
	}
	result.locale = add ? insertUnicodeExt(found, add) : found;
	return result;
}
function insertUnicodeExt(locale, add) {
	const i = locale.indexOf('-x-');
	if (i >= 0) return locale.slice(0, i) + '-u' + add + locale.slice(i);
	return locale + '-u' + add;
}
function SupportedLocales(requested, options) {
	options = CoerceOptionsToObject(options);
	GetOption(options, 'localeMatcher', 'string', ['lookup', 'best fit'], 'best fit');
	const out = [];
	for (const l of requested) if (BestAvailableLocale(removeUnicodeExt(l)) !== undefined) append(out, l);
	return out;
}
function supportedLocalesOf(locales, options) {
	return SupportedLocales(CanonicalizeLocaleList(locales), options);
}

/* numbering systems: the simple digit ones */
const NU = META.nu;
const NU_LIST = Object.keys(NU).sort();
function nuKeyData() { return ['latn'].concat(NU_LIST.filter((n) => n !== 'latn')); }
function digitsOf(nu) {
	if (!nu || nu === 'latn') return null;
	const z = NU[nu];
	if (z === undefined) return null;
	const a = typeof z === 'string' ? [...z] : Array.from({ length: 10 }, (_, i) => String.fromCodePoint(z + i));
	return a;
}
function transliterate(s, digits) {
	if (!digits) return s;
	return s.replace(/[0-9]/g, (d) => digits[d.charCodeAt(0) - 48]);
}
const TYPE_RE = /^[a-z0-9]{3,8}(-[a-z0-9]{3,8})*$/i;
function checkType(v, name) {
	if (v !== undefined && !TYPE_RE.test(v)) throw new RangeError('Invalid ' + name + ': ' + v);
}

/* ==== plural rules ================================================================================== */

/* the operands of a formatted number (its digit strings): n i v f t (e: the compact exponent) */
function operands(intStr, fracStr, e) {
	const i = Number(intStr);
	const v = fracStr.length;
	const tf = fracStr.replace(/0+$/, '');
	const n = Number(intStr + (v ? '.' + fracStr : '')) * (e ? 10 ** e : 1);
	return { n, i: e ? Math.trunc(n) : i, v, f: v ? Number(fracStr) : 0, t: tf ? Number(tf) : 0, e: e || 0 };
}
const inR = (x, a, b) => x >= a && x <= b && Math.floor(x) === x;
const PLURAL = {
	en: [(o) => (o.i === 1 && o.v === 0 ? 'one' : 'other'),
		(o) => { const n10 = o.n % 10, n100 = o.n % 100; return n10 === 1 && n100 !== 11 ? 'one' : n10 === 2 && n100 !== 12 ? 'two' : n10 === 3 && n100 !== 13 ? 'few' : 'other'; },
		['one', 'other'], ['one', 'two', 'few', 'other']],
	fr: [(o) => (o.i === 0 || o.i === 1 ? 'one' : manyFr(o) ? 'many' : 'other'), (o) => (o.n === 1 ? 'one' : 'other'),
		['one', 'many', 'other'], ['one', 'other']],
	de: [(o) => (o.i === 1 && o.v === 0 ? 'one' : 'other'), () => 'other', ['one', 'other'], ['other']],
	es: [(o) => (o.n === 1 && o.e === 0 ? 'one' : manyFr(o) ? 'many' : 'other'), () => 'other', ['one', 'many', 'other'], ['other']],
	it: [(o) => (o.i === 1 && o.v === 0 ? 'one' : manyFr(o) ? 'many' : 'other'),
		(o) => ([11, 8, 80, 800].includes(o.n) ? 'many' : 'other'), ['one', 'many', 'other'], ['many', 'other']],
	pt: [(o) => (inR(o.i, 0, 1) ? 'one' : manyFr(o) ? 'many' : 'other'), () => 'other', ['one', 'many', 'other'], ['other']],
	'pt-PT': [(o) => (o.i === 1 && o.v === 0 ? 'one' : manyFr(o) ? 'many' : 'other'), () => 'other', ['one', 'many', 'other'], ['other']],
	ja: [() => 'other', () => 'other', ['other'], ['other']],
	ru: [(o) => {
		if (o.v !== 0) return 'other';
		const i10 = o.i % 10, i100 = o.i % 100;
		if (i10 === 1 && i100 !== 11) return 'one';
		if (i10 >= 2 && i10 <= 4 && !(i100 >= 12 && i100 <= 14)) return 'few';
		return 'many';
	}, () => 'other', ['one', 'few', 'many', 'other'], ['other']],
	pl: [(o) => {
		if (o.i === 1 && o.v === 0) return 'one';
		if (o.v !== 0) return 'other';
		const i10 = o.i % 10, i100 = o.i % 100;
		if (i10 >= 2 && i10 <= 4 && !(i100 >= 12 && i100 <= 14)) return 'few';
		return 'many';
	}, () => 'other', ['one', 'few', 'many', 'other'], ['other']],
	cs: [(o) => (o.i === 1 && o.v === 0 ? 'one' : inR(o.i, 2, 4) && o.v === 0 ? 'few' : o.v !== 0 ? 'many' : 'other'), () => 'other',
		['one', 'few', 'many', 'other'], ['other']],
	da: [(o) => (o.n === 1 || (o.t !== 0 && (o.i === 0 || o.i === 1)) ? 'one' : 'other'), () => 'other', ['one', 'other'], ['other']],
	nb: [(o) => (o.n === 1 ? 'one' : 'other'), () => 'other', ['one', 'other'], ['other']],
	sv: [(o) => (o.i === 1 && o.v === 0 ? 'one' : 'other'),
		(o) => { const n10 = o.n % 10, n100 = o.n % 100; return (n10 === 1 || n10 === 2) && n100 !== 11 && n100 !== 12 ? 'one' : 'other'; },
		['one', 'other'], ['one', 'other']],
};
PLURAL.nl = PLURAL.de; PLURAL.fi = PLURAL.de;
PLURAL.tr = PLURAL.nb;
PLURAL.zh = PLURAL.ja; PLURAL.ko = PLURAL.ja;
function manyFr(o) { return (o.e === 0 && o.i !== 0 && o.i % 1000000 === 0 && o.v === 0) || !inR(o.e, 0, 5); }
function pluralOf(loc) {
	const dk = dataKey(loc);
	return PLURAL[dk] || PLURAL[dk.split('-')[0]] || PLURAL.en;
}
const PLURAL_ORDER = ['zero', 'one', 'two', 'few', 'many', 'other'];

/* ==== decimals: exact digit strings ================================================================ */

/* A decimal: { neg, m: digits (no leading zeros; '0' for zero), x: exponent } = (-1)^neg m 10^x;
   also NaN / Infinity: { neg, nan } { neg, inf }. From a Number: its shortest round-trip digits. */
function decOfNumber(v) {
	const neg = v < 0 || Object.is(v, -0);
	if (Number.isNaN(v)) return { neg: false, nan: true };
	if (!Number.isFinite(v)) return { neg, inf: true };
	if (v === 0) return { neg, m: '0', x: 0 };
	const s = Math.abs(v).toExponential();
	const ei = s.indexOf('e');
	const mant = s.slice(0, ei).replace('.', '');
	const e = Number(s.slice(ei + 1));
	return norm({ neg, m: mant, x: e - (mant.length - 1) });
}
function decOfBigInt(b) {
	const neg = b < 0n;
	const s = (neg ? -b : b).toString();
	return norm({ neg, m: s, x: 0 });
}
/* a string: a StringNumericLiteral, exactly */
function decOfString(str) {
	const s = str.replace(/^[\s\ufeff\u00a0\u1680\u2000-\u200a\u2028\u2029\u202f\u205f\u3000]+|[\s\ufeff\u00a0\u1680\u2000-\u200a\u2028\u2029\u202f\u205f\u3000]+$/g, '');
	if (s === '') return { neg: false, m: '0', x: 0 };
	let m = /^([+-]?)(Infinity)$/.exec(s);
	if (m) return { neg: m[1] === '-', inf: true };
	if (/^0[xob]/i.test(s)) {
		try { return decOfBigInt(BigInt(s)); } catch (e) { return { neg: false, nan: true }; }
	}
	m = /^([+-]?)(\d*)(?:\.(\d*))?(?:[eE]([+-]?\d+))?$/.exec(s);
	if (!m || (m[2] === '' && (m[3] === undefined || m[3] === ''))) return { neg: false, nan: true };
	const ip = m[2], fp = m[3] || '';
	let e = m[4] ? Number(m[4]) : 0;
	if (!Number.isFinite(e)) return { neg: m[1] === '-', inf: true };
	if (Math.abs(e) > 1e8) {
		const zero = !/[1-9]/.test(ip + fp);
		if (zero) return { neg: m[1] === '-', m: '0', x: 0 };
		return e > 0 ? { neg: m[1] === '-', inf: true } : { neg: m[1] === '-', m: '0', x: 0 };
	}
	return norm({ neg: m[1] === '-', m: ip + fp, x: e - fp.length });
}
function norm(d) {
	if (d.nan || d.inf) return d;
	let m = d.m.replace(/^0+/, '');
	let x = d.x;
	if (m === '') return { neg: d.neg, m: '0', x: 0 };
	const t = m.match(/0+$/);
	if (t) { m = m.slice(0, m.length - t[0].length); x += t[0].length; }
	return { neg: d.neg, m, x };
}
const isZero = (d) => d.m === '0';
/* the exponent of the leading digit */
function magnitude(d) { return isZero(d) ? 0 : d.m.length - 1 + d.x; }
function decShift(d, k) { return isZero(d) || d.nan || d.inf ? d : { neg: d.neg, m: d.m, x: d.x + k }; }
function addOne(s) {
	/* digit string + 1 */
	const a = s.split('');
	let i = a.length - 1;
	while (i >= 0) {
		if (a[i] === '9') { a[i] = '0'; i--; } else { a[i] = String.fromCharCode(a[i].charCodeAt(0) + 1); break; }
	}
	return (i < 0 ? '1' : '') + a.join('');
}
/* round at 10^e by a rounding mode; the rounding increment inc (1, 2, 5, 10, 20, 25, 50, ...) in units of 10^e */
function roundAt(d, e, mode, inc) {
	if (isZero(d)) return { neg: d.neg, m: '0', x: 0 };
	inc = inc || 1;
	if (inc !== 1) return roundIncrement(d, e, mode, inc);
	if (d.x >= e) return d;
	const cut = e - d.x;			/* digits dropped */
	const keep = d.m.length - cut;
	let kept = keep > 0 ? d.m.slice(0, keep) : '';
	const dropped = keep >= 0 ? d.m.slice(keep) : '0'.repeat(-keep) + d.m;
	/* the dropped part vs a half: -1 under, 0 exact half, 1 over (dropped is non-zero: normalised) */
	const first = dropped.charCodeAt(0) - 48;
	const restNZ = /[1-9]/.test(dropped.slice(1));
	const half = first > 5 || (first === 5 && restNZ) ? 1 : first === 5 ? 0 : -1;
	const lastKept = kept ? kept.charCodeAt(kept.length - 1) - 48 : 0;
	let up;
	switch (mode) {
	case 'ceil': up = !d.neg; break;
	case 'floor': up = d.neg; break;
	case 'expand': up = true; break;
	case 'trunc': up = false; break;
	case 'halfCeil': up = half > 0 || (half === 0 && !d.neg); break;
	case 'halfFloor': up = half > 0 || (half === 0 && d.neg); break;
	case 'halfTrunc': up = half > 0; break;
	case 'halfEven': up = half > 0 || (half === 0 && lastKept % 2 === 1); break;
	default: up = half >= 0;	/* halfExpand */
	}
	if (up) kept = addOne(kept || '0');
	return norm({ neg: d.neg, m: kept || '0', x: e });
}
function roundIncrement(d, e, mode, inc) {
	/* round d / (inc 10^e) to an integer by the mode, times inc */
	const q = decShift(d, -e);		/* d in units of 10^e */
	/* q / inc: long division on the digit string, to an integer + a remainder */
	const intDigits = q.x >= 0 ? q.m + '0'.repeat(q.x) : q.m.slice(0, Math.max(0, q.m.length + q.x)) || '0';
	const fracDigits = q.x >= 0 ? '' : (q.m.length + q.x < 0 ? '0'.repeat(-(q.m.length + q.x)) + q.m : q.m.slice(q.m.length + q.x));
	let rem = 0, quo = '';
	for (const c of intDigits) { rem = rem * 10 + (c.charCodeAt(0) - 48); quo += Math.floor(rem / inc); rem %= inc; }
	/* the fraction of an increment: rem + 0.fracDigits, compared to inc / 2 */
	const fracNZ = /[1-9]/.test(fracDigits);
	const twice = rem * 2;
	const half = twice > inc || (twice === inc && fracNZ) ? 1 : twice === inc ? 0 : (rem === 0 && !fracNZ ? -2 : -1);
	let qd = norm({ neg: false, m: quo, x: 0 });
	const exact = half === -2;
	const last = qd.m.charCodeAt(qd.m.length - 1) - 48;
	let up = false;
	if (!exact) {
		switch (mode) {
		case 'ceil': up = !d.neg; break;
		case 'floor': up = d.neg; break;
		case 'expand': up = true; break;
		case 'trunc': up = false; break;
		case 'halfCeil': up = half > 0 || (half === 0 && !d.neg); break;
		case 'halfFloor': up = half > 0 || (half === 0 && d.neg); break;
		case 'halfTrunc': up = half > 0; break;
		case 'halfEven': up = half > 0 || (half === 0 && last % 2 === 1); break;
		default: up = half >= 0;
		}
	}
	if (up) qd = { neg: false, m: addOne(isZero(qd) ? '0' : qd.m + '0'.repeat(qd.x)), x: 0 };
	/* times inc, at 10^e */
	const prod = mulSmall(isZero(qd) ? '0' : qd.m + '0'.repeat(qd.x), inc);
	return norm({ neg: d.neg, m: prod, x: e });
}
function mulSmall(s, k) {
	let carry = 0, out = '';
	for (let i = s.length - 1; i >= 0; i--) {
		const p = (s.charCodeAt(i) - 48) * k + carry;
		out = (p % 10) + out;
		carry = Math.floor(p / 10);
	}
	while (carry) { out = (carry % 10) + out; carry = Math.floor(carry / 10); }
	return out;
}
/* a decimal's integer and fraction digit strings, with minFrac trailing zeros at least */
function digitStrings(d, minFrac) {
	let ip, fp;
	if (isZero(d)) { ip = '0'; fp = ''; }
	else if (d.x >= 0) { ip = d.m + '0'.repeat(d.x); fp = ''; }
	else {
		const k = d.m.length + d.x;
		if (k > 0) { ip = d.m.slice(0, k); fp = d.m.slice(k); }
		else { ip = '0'; fp = '0'.repeat(-k) + d.m; }
	}
	if (fp.length < minFrac) fp += '0'.repeat(minFrac - fp.length);
	return [ip, fp];
}
function decCompare(a, b) {
	/* -1, 0, 1 for finite decimals (signs included) */
	const sa = isZero(a) ? 0 : a.neg ? -1 : 1, sb = isZero(b) ? 0 : b.neg ? -1 : 1;
	if (sa !== sb) return sa < sb ? -1 : 1;
	if (sa === 0) return 0;
	const ma = magnitude(a), mb = magnitude(b);
	let c;
	if (ma !== mb) c = ma < mb ? -1 : 1;
	else {
		const la = a.m, lb = b.m;
		const n = Math.max(la.length, lb.length);
		const pa = la.padEnd(n, '0'), pb = lb.padEnd(n, '0');
		c = pa < pb ? -1 : pa > pb ? 1 : 0;
	}
	return sa < 0 ? -c : c;
}

/* ==== number formatting ================================================================================ */

const FALLBACK = Symbol('IntlLegacyConstructedSymbol');
const ROUNDING_INCREMENTS = [1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500, 5000];
const ROUNDING_MODES = ['ceil', 'floor', 'expand', 'trunc', 'halfCeil', 'halfFloor', 'halfExpand', 'halfTrunc', 'halfEven'];

/* SetNumberFormatDigitOptions: the digit and rounding slots of s */
function SetNumberFormatDigitOptions(s, options, mnfdDefault, mxfdDefault, notation) {
	const mnid = GetNumberOption(options, 'minimumIntegerDigits', 1, 21, 1);
	let mnfd = options.minimumFractionDigits;
	let mxfd = options.maximumFractionDigits;
	let mnsd = options.minimumSignificantDigits;
	let mxsd = options.maximumSignificantDigits;
	s.minimumIntegerDigits = mnid;
	const inc = GetNumberOption(options, 'roundingIncrement', 1, 5000, 1);
	if (!ROUNDING_INCREMENTS.includes(inc)) throw new RangeError('roundingIncrement out of range');
	const mode = GetOption(options, 'roundingMode', 'string', ROUNDING_MODES, 'halfExpand');
	const priority = GetOption(options, 'roundingPriority', 'string', ['auto', 'morePrecision', 'lessPrecision'], 'auto');
	const tzd = GetOption(options, 'trailingZeroDisplay', 'string', ['auto', 'stripIfInteger'], 'auto');
	if (inc !== 1) mxfdDefault = mnfdDefault;
	s.roundingIncrement = inc;
	s.roundingMode = mode;
	s.trailingZeroDisplay = tzd;
	const hasSd = mnsd !== undefined || mxsd !== undefined;
	const hasFd = mnfd !== undefined || mxfd !== undefined;
	let needSd = true, needFd = true;
	if (priority === 'auto') {
		needSd = hasSd;
		if (needSd || (!hasFd && notation === 'compact')) needFd = false;
	}
	if (needSd) {
		if (hasSd) {
			mnsd = DefaultNumberOption(mnsd, 1, 21, 1, 'minimumSignificantDigits');
			mxsd = DefaultNumberOption(mxsd, mnsd, 21, 21, 'maximumSignificantDigits');
			s.minimumSignificantDigits = mnsd;
			s.maximumSignificantDigits = mxsd;
		} else {
			s.minimumSignificantDigits = 1;
			s.maximumSignificantDigits = 21;
		}
	}
	if (needFd) {
		if (hasFd) {
			mnfd = DefaultNumberOption(mnfd, 0, 100, undefined, 'minimumFractionDigits');
			mxfd = DefaultNumberOption(mxfd, 0, 100, undefined, 'maximumFractionDigits');
			if (mnfd === undefined) mnfd = Math.min(mnfdDefault, mxfd);
			else if (mxfd === undefined) mxfd = Math.max(mxfdDefault, mnfd);
			else if (mnfd > mxfd) throw new RangeError('minimumFractionDigits is greater than maximumFractionDigits');
			s.minimumFractionDigits = mnfd;
			s.maximumFractionDigits = mxfd;
		} else {
			s.minimumFractionDigits = mnfdDefault;
			s.maximumFractionDigits = mxfdDefault;
		}
	}
	if (!needSd && !needFd) {
		s.minimumFractionDigits = 0;
		s.maximumFractionDigits = 0;
		s.minimumSignificantDigits = 1;
		s.maximumSignificantDigits = 2;
		s.roundingType = 'morePrecision';
		s.roundingPriority = 'morePrecision';
	} else if (priority === 'auto') {
		s.roundingType = hasSd ? 'significantDigits' : 'fractionDigits';
		s.roundingPriority = 'auto';
	} else {
		s.roundingType = priority;
		s.roundingPriority = priority;
	}
	if (inc !== 1) {
		if (s.roundingType !== 'fractionDigits') throw new TypeError('roundingIncrement needs fraction digits');
		if (s.maximumFractionDigits !== s.minimumFractionDigits) throw new RangeError('roundingIncrement needs equal fraction digits');
	}
}

/* ToRawPrecision / ToRawFixed: { r: the rounded decimal, ip, fp: digit strings, rm: the rounding magnitude } */
function ToRawPrecision(d, minP, maxP, mode) {
	let e = magnitude(d);
	const r = roundAt(d, e - maxP + 1, mode);
	if (!isZero(r) && magnitude(r) > e) e = magnitude(r);
	let [ip, fp] = digitStrings(r, 0);
	/* at least minP significant digits */
	const sig = ip !== '0' ? ip.length + fp.length : (isZero(r) ? 1 + fp.length : fp.length - (fp.match(/^0*/)[0].length));
	if (sig < minP) fp += '0'.repeat(minP - sig);
	return { r, ip, fp, rm: e - maxP + 1 };
}
function ToRawFixed(d, minF, maxF, inc, mode) {
	const r = roundAt(d, -maxF, mode, inc);
	const [ip, fp] = digitStrings(r, minF);
	return { r, ip, fp, rm: -maxF };
}
function FormatNumericToString(s, d) {
	let res;
	if (s.roundingType === 'significantDigits') res = ToRawPrecision(d, s.minimumSignificantDigits, s.maximumSignificantDigits, s.roundingMode);
	else if (s.roundingType === 'fractionDigits') res = ToRawFixed(d, s.minimumFractionDigits, s.maximumFractionDigits, s.roundingIncrement, s.roundingMode);
	else {
		const sr = ToRawPrecision(d, s.minimumSignificantDigits, s.maximumSignificantDigits, s.roundingMode);
		const fr = ToRawFixed(d, s.minimumFractionDigits, s.maximumFractionDigits, s.roundingIncrement, s.roundingMode);
		let fixed;
		if (s.roundingType === 'morePrecision') fixed = !(sr.rm <= fr.rm);
		else fixed = sr.rm <= fr.rm;
		res = fixed ? fr : sr;
	}
	let { ip, fp } = res;
	if (s.trailingZeroDisplay === 'stripIfInteger' && !/[1-9]/.test(fp)) fp = '';
	if (ip.length < s.minimumIntegerDigits) ip = '0'.repeat(s.minimumIntegerDigits - ip.length) + ip;
	return { r: res.r, ip, fp };
}

/* ToIntlMathematicalValue */
function ToIntlMV(v) {
	if (isObj(v)) {
		v = typeof v[Symbol.toPrimitive] === 'function' || true ? toPrimNumber(v) : v;
	}
	if (typeof v === 'bigint') return decOfBigInt(v);
	if (typeof v === 'string') {
		const d = decOfString(v);
		return d;
	}
	return decOfNumber(ToNumber(v));
}
function toPrimNumber(o) {
	const ex = o[Symbol.toPrimitive];
	if (ex !== undefined && ex !== null) {
		if (typeof ex !== 'function') throw new TypeError('@@toPrimitive is not a function');
		const r = ex.call(o, 'number');
		if (isObj(r)) throw new TypeError('Cannot convert object to primitive value');
		return r;
	}
	for (const m of ['valueOf', 'toString']) {
		const f = o[m];
		if (typeof f === 'function') {
			const r = f.call(o);
			if (!isObj(r)) return r;
		}
	}
	throw new TypeError('Cannot convert object to primitive value');
}

/* the templates of the data: '#' the number, '-' '+' the sign, '%' '\u00a4', '{text}' a compact or unit part */
function tplParse(t) {
	/* -> [ { k: 'num' | 'sign' | 'pct' | 'cur' | 'lit' | 'x' (typed text), v } ] */
	const out = [];
	let lit = '';
	const flush = () => { if (lit) { out.push({ k: 'lit', v: lit }); lit = ''; } };
	for (let i = 0; i < t.length; i++) {
		const c = t[i];
		if (c === '\\') { lit += t[++i]; continue; }
		if (c === '#') { flush(); out.push({ k: 'num' }); }
		else if (c === '-' || c === '+') { flush(); out.push({ k: 'sign' }); }
		else if (c === '%') { flush(); out.push({ k: 'pct' }); }
		else if (c === '\u00a4') { flush(); out.push({ k: 'cur' }); }
		else if (c === '{') {
			flush();
			let v = '';
			for (i++; i < t.length && t[i] !== '}'; i++) { if (t[i] === '\\') i++; v += t[i]; }
			out.push({ k: 'x', v });
		} else lit += c;
	}
	flush();
	return out;
}
const TPL_CACHE = new Map();
function tpl(t) {
	let v = TPL_CACHE.get(t);
	if (!v) { v = tplParse(t); TPL_CACHE.set(t, v); }
	return v;
}
/* "other|one:...|few:..." -> the text for a plural category */
function byPlural(s, cat) {
	if (s === undefined || s === null) return s;
	const p = String(s).split('|');
	for (let i = 1; i < p.length; i++) {
		const j = p[i].indexOf(':');
		if (p[i].slice(0, j) === cat) return p[i].slice(j + 1);
	}
	return p[0];
}

const SIMPLE_UNITS = new Set(META.units.split(' '));
function IsWellFormedUnitIdentifier(u) {
	if (SIMPLE_UNITS.has(u)) return true;
	const i = u.indexOf('-per-');
	if (i < 0) return false;
	return SIMPLE_UNITS.has(u.slice(0, i)) && SIMPLE_UNITS.has(u.slice(i + 5));
}
const CURRENCY_DIGITS = META.cd;
function CurrencyDigits(c) { return hasOwn(CURRENCY_DIGITS, c) ? CURRENCY_DIGITS[c] : 2; }

const NF = slots();
function NumberFormat(locales, options) {
	const nt = new.target || NumberFormat;
	const nf = OrdinaryCreateFromConstructor(nt, NumberFormat.prototype);
	InitializeNumberFormat(nf, locales, options);
	if (new.target === undefined && isObj(this) && this instanceof NumberFormat) {
		Object.defineProperty(this, FALLBACK, { value: nf, writable: false, enumerable: false, configurable: false });
		return this;
	}
	return nf;
}
function InitializeNumberFormat(nf, locales, options) {
	const requested = CanonicalizeLocaleList(locales);
	options = CoerceOptionsToObject(options);
	const opt = { __proto__: null };
	GetOption(options, 'localeMatcher', 'string', ['lookup', 'best fit'], 'best fit');
	const nuOpt = GetOption(options, 'numberingSystem', 'string', undefined, undefined);
	checkType(nuOpt, 'numberingSystem');
	opt.nu = nuOpt;
	const r = ResolveLocale(requested, opt, ['nu'], nuKeyData);
	const s = { __proto__: null, locale: r.locale, dataLocale: dataKey(r.dataLocale), numberingSystem: r.nu };
	/* SetNumberFormatUnitOptions */
	const style = GetOption(options, 'style', 'string', ['decimal', 'percent', 'currency', 'unit'], 'decimal');
	s.style = style;
	const currency = GetOption(options, 'currency', 'string', undefined, undefined);
	if (currency === undefined) { if (style === 'currency') throw new TypeError('Currency code is required with currency style.'); }
	else if (!/^[a-zA-Z]{3}$/.test(currency)) throw new RangeError('Invalid currency code : ' + currency);
	const currencyDisplay = GetOption(options, 'currencyDisplay', 'string', ['code', 'symbol', 'narrowSymbol', 'name'], 'symbol');
	const currencySign = GetOption(options, 'currencySign', 'string', ['standard', 'accounting'], 'standard');
	const unit = GetOption(options, 'unit', 'string', undefined, undefined);
	if (unit === undefined) { if (style === 'unit') throw new TypeError('Unit is required with unit style.'); }
	else if (!IsWellFormedUnitIdentifier(unit)) throw new RangeError('Invalid unit argument for Intl.NumberFormat() \'' + unit + '\'');
	const unitDisplay = GetOption(options, 'unitDisplay', 'string', ['short', 'narrow', 'long'], 'short');
	if (style === 'currency') {
		s.currency = currency.toUpperCase();
		s.currencyDisplay = currencyDisplay;
		s.currencySign = currencySign;
	}
	if (style === 'unit') {
		s.unit = unit;
		s.unitDisplay = unitDisplay;
	}
	const notation = GetOption(options, 'notation', 'string', ['standard', 'scientific', 'engineering', 'compact'], 'standard');
	s.notation = notation;
	let mnfdDefault, mxfdDefault;
	if (style === 'currency' && notation === 'standard') {
		const cd = CurrencyDigits(s.currency);
		mnfdDefault = cd; mxfdDefault = cd;
	} else {
		mnfdDefault = 0;
		mxfdDefault = style === 'percent' ? 0 : 3;
	}
	SetNumberFormatDigitOptions(s, options, mnfdDefault, mxfdDefault, notation);
	const compactDisplay = GetOption(options, 'compactDisplay', 'string', ['short', 'long'], 'short');
	const defaultUseGrouping = notation === 'compact' ? 'min2' : 'auto';
	s.useGrouping = GetBoolOrStr(options, 'useGrouping', ['min2', 'auto', 'always', 'true', 'false'], 'always', false, defaultUseGrouping);
	if (notation === 'compact') s.compactDisplay = compactDisplay;
	s.signDisplay = GetOption(options, 'signDisplay', 'string', ['auto', 'never', 'always', 'exceptZero', 'negative'], 'auto');
	s.boundFormat = undefined;
	NF.set(nf, s);
	return nf;
}
function unwrapNF(nf, what) {
	if (!NF.has(nf) && isObj(nf) && nf instanceof NumberFormat) nf = nf[FALLBACK];
	return NF.get(nf, what);
}

/* ---- the parts of a formatted number ---- */

function numData(s) { return sec(s.dataLocale, 'n'); }
/* the grouped integer: parts integer / group */
function groupParts(out, ip, s, D, digits) {
	const ug = s.useGrouping;
	const g1 = D.g1, g2 = D.g2;
	let grouped = false;
	if (ug !== false && s.notation !== 'scientific' && s.notation !== 'engineering') {
		if (ug === 'always') grouped = ip.length > g1;
		else if (ug === 'min2') grouped = ip.length >= g1 + 2;
		else grouped = ip.length >= g1 + D.mg;
	}
	if (!grouped) { out.push({ type: 'integer', value: transliterate(ip, digits) }); return; }
	const groups = [];
	let end = ip.length;
	groups.unshift(ip.slice(end - g1, end));
	end -= g1;
	while (end > 0) {
		groups.unshift(ip.slice(Math.max(0, end - g2), end));
		end -= g2;
	}
	groups.forEach((g, i) => {
		if (i) out.push({ type: 'group', value: D.grp });
		out.push({ type: 'integer', value: transliterate(g, digits) });
	});
}
/* the number itself: integer, groups, decimal, fraction (and the exponent) */
function numberCore(s, D, f, exponent, digits) {
	const out = [];
	groupParts(out, f.ip, s, D, digits);
	if (f.fp) {
		out.push({ type: 'decimal', value: D.dec });
		out.push({ type: 'fraction', value: transliterate(f.fp, digits) });
	}
	if (s.notation === 'scientific' || s.notation === 'engineering') {
		out.push({ type: 'exponentSeparator', value: D.exp });
		if (exponent < 0) out.push({ type: 'exponentMinusSign', value: D.min });
		out.push({ type: 'exponentInteger', value: transliterate(String(Math.abs(exponent)), digits) });
	}
	return out;
}
function compactPattern(s, D, mag) {
	if (mag < 3) return null;
	const list = D.cp[s.compactDisplay === 'long' ? 'l' : 's'];
	const p = list[Math.min(mag, 14) - 3];
	return p || null;
}
function ComputeExponentForMagnitude(s, D, mag) {
	switch (s.notation) {
	case 'scientific': return mag;
	case 'engineering': return Math.floor(mag / 3) * 3;
	case 'compact': {
		const p = compactPattern(s, D, mag);
		if (!p) return 0;
		const I = Number(p.slice(0, p.indexOf(':')));
		return Math.min(mag, 14) - (I - 1);
	}
	default: return 0;
	}
}
function ComputeExponent(s, D, d) {
	if (isZero(d)) return [0, 0];
	const mag = magnitude(d);
	let exponent = ComputeExponentForMagnitude(s, D, mag);
	const f = FormatNumericToString(s, decShift(d, -exponent));
	if (isZero(f.r)) return [exponent, mag];
	if (magnitude(f.r) === mag - exponent) return [exponent, mag];
	exponent = ComputeExponentForMagnitude(s, D, mag + 1);
	return [exponent, mag + 1];
}
/* the sign to show: 'minus', 'plus', '' (x: the rounded decimal) */
function signOf(s, x) {
	const zero = x.nan ? false : !x.inf && isZero(x);
	switch (s.signDisplay) {
	case 'never': return '';
	case 'always': return x.nan ? '+' : x.neg ? '-' : '+';
	case 'exceptZero': return x.nan || zero ? '' : x.neg ? '-' : '+';
	case 'negative': return x.neg && !zero ? '-' : '';
	default: return x.neg ? '-' : '';
	}
}
function currencyText(s, D, cat) {
	const c = s.currency;
	switch (s.currencyDisplay) {
	case 'code': return c;
	case 'name': {
		const n = D.cn[c];
		return n === undefined ? c : Array.isArray(n) ? byPlural(n.join('|'), cat) : n;
	}
	case 'narrowSymbol': return (D.cm && D.cm[c]) || NARROW[c] || D.cs[c] || c;
	default: return D.cs[c] || c;
	}
}
const NARROW = { USD: '$', EUR: '\u20ac', GBP: '\u00a3', JPY: '\u00a5', CNY: '\u00a5', INR: '\u20b9', KRW: '\u20a9', ILS: '\u20aa', RUB: '\u20bd', TRY: '\u20ba', UAH: '\u20b4', VND: '\u20ab', PHP: '\u20b1', NGN: '\u20a6', THB: '\u0e3f', PLN: 'z\u0142', BRL: 'R$', CAD: '$', AUD: '$', MXN: '$', ARS: '$', CLP: '$', COP: '$', NZD: '$', HKD: '$', SGD: '$', TWD: '$', ZAR: 'R', SEK: 'kr', NOK: 'kr', DKK: 'kr', CZK: 'K\u010d', HUF: 'Ft', ISK: 'kr', CHF: 'CHF' };
/* the unit's template for a plural category */
function unitTemplate(s, D, cat) {
	const U = sec(s.dataLocale, 'u');
	const w = s.unitDisplay === 'long' ? 0 : s.unitDisplay === 'narrow' ? 2 : 1;
	const get = (u) => {
		const e = U[u];
		if (!e) return null;
		const v = e[w] === 0 ? e[1] : e[w];
		return byPlural(v, cat);
	};
	let t = get(s.unit);
	if (t) return t;
	const i = s.unit.indexOf('-per-');
	const x = get(s.unit.slice(0, i));
	const perE = U.per[s.unit.slice(i + 5)];
	const per = perE ? (perE[w] === 0 ? perE[1] : perE[w]) : '{0}/' + s.unit.slice(i + 5);
	/* the numerator's template in the per form, the texts merged into one unit part */
	const j = per.indexOf('{0}');
	const before = per.slice(0, j), after = per.slice(j + 3);
	t = (before ? '{' + before + '}' : '') + x + (after ? '{' + after + '}' : '');
	return t.replace(/\}\{/g, '');
}

/* PartitionNumberPattern -> parts (and the rounded decimal: .x, the plural category: .cat) */
function partitionNumber(s, d) {
	const D = numData(s);
	const digits = digitsOf(s.numberingSystem);
	let core, x, exponent = 0, mag = 0, f = null;
	if (d.nan) { core = [{ type: 'nan', value: D.nan }]; x = d; }
	else if (d.inf) { core = [{ type: 'infinity', value: D.inf }]; x = d; }
	else {
		if (s.style === 'percent') d = decShift(d, 2);
		[exponent, mag] = ComputeExponent(s, D, d);
		f = FormatNumericToString(s, decShift(d, -exponent));
		x = f.r;
		core = numberCore(s, D, f, exponent, digits);
	}
	/* the plural category of the shown number (for names, units, compact) */
	const cat = f ? pluralOf(s.dataLocale)[0](operands(f.ip, f.fp, 0)) : 'other';
	/* the compact affix around the number */
	let numT = [{ k: 'num' }];
	if (s.notation === 'compact' && f && exponent !== 0) {
		const p = compactPattern(s, D, mag);
		if (p) {
			const body = p.slice(p.indexOf(':') + 1);
			numT = tpl(byPlural(body, cat));
		}
	}
	/* the style's template: positive / negative / accounting / unit / name */
	const sign = signOf(s, x);
	let t;
	if (s.style === 'unit') t = '-' + unitTemplate(s, D, cat);
	else if (s.style === 'currency' && s.currencyDisplay === 'name') t = '-' + D.tn;
	else {
		const src = s.style === 'percent' ? D.tp : s.style === 'currency' ? (s.currencySign === 'accounting' && sign === '-' ? D.ta : D.tc) : D.t;
		t = src.split(';')[1];		/* the negative one: its sign replaced / removed */
	}
	/* a unit / name template's sign goes before its number */
	if (s.style === 'unit' || (s.style === 'currency' && s.currencyDisplay === 'name')) {
		t = t.slice(1);
		const k = t.indexOf('#');
		t = t.slice(0, k) + '-' + t.slice(k);
	}
	const T = tpl(t);
	const out = [];
	for (const e of T) {
		switch (e.k) {
		case 'num':
			for (const n of numT) {
				if (n.k === 'num') out.push(...core);
				else if (n.k === 'x') out.push({ type: 'compact', value: n.v });
				else out.push({ type: 'literal', value: n.v });
			}
			break;
		case 'sign':
			if (sign === '-') out.push({ type: 'minusSign', value: D.min });
			else if (sign === '+') out.push({ type: 'plusSign', value: D.plus });
			break;
		case 'pct': out.push({ type: 'percentSign', value: D.pct }); break;
		case 'cur': out.push({ type: 'currency', value: currencyText(s, D, cat) }); break;
		case 'x': out.push({ type: 'unit', value: e.v }); break;
		default: out.push({ type: 'literal', value: e.v });
		}
	}
	/* accounting: the parentheses are the sign */
	if (s.style === 'currency') currencySpacing(out);
	const res = mergeLiterals(out);
	res.x = x;
	res.cat = cat;
	return res;
}
const NUM_TYPES = new Set(['integer', 'nan', 'infinity']);
function currencySpacing(out) {
	/* a letter currency next to the number: a no-break space between (CLDR's currency spacing) */
	for (let i = 0; i < out.length; i++) {
		if (out[i].type !== 'currency') continue;
		const v = out[i].value;
		const next = out[i + 1], prev = out[i - 1];
		if (next && (NUM_TYPES.has(next.type) || next.type === 'minusSign' || next.type === 'plusSign') && next.type !== 'infinity') {
			const ch = String.fromCodePoint(v.codePointAt(v.length - 1) >= 0xdc00 && v.length > 1 ? v.codePointAt(v.length - 2) : v.codePointAt(v.length - 1));
			if (!/\p{S}/u.test(ch) && NUM_TYPES.has(next.type)) out.splice(i + 1, 0, { type: 'literal', value: '\u00a0' });
		} else if (prev && (NUM_TYPES.has(prev.type) || prev.type === 'fraction')) {
			const ch = String.fromCodePoint(v.codePointAt(0));
			if (!/\p{S}/u.test(ch)) { out.splice(i, 0, { type: 'literal', value: '\u00a0' }); i++; }
		}
	}
}
function mergeLiterals(parts) {
	const out = [];
	for (const p of parts) {
		if (p.type === 'literal' && out.length && out[out.length - 1].type === 'literal') out[out.length - 1] = { type: 'literal', value: out[out.length - 1].value + p.value };
		else if (p.type !== 'literal' || p.value !== '') out.push(p);
	}
	return out;
}
const joinParts = (p) => { let s = ''; for (const x of p) s += x.value; return s; };
function FormatNumeric(s, d) { return joinParts(partitionNumber(s, d)); }

/* ---- ranges ---- */

function partitionNumberRange(s, x, y) {
	if (x.nan || y.nan) throw new RangeError('NaN is not allowed in a range');
	const px = partitionNumber(s, x), py = partitionNumber(s, y);
	const D = numData(s);
	if (joinParts(px) === joinParts(py)) {
		/* approximately: its sign where the sign goes */
		const out = [];
		let done = false;
		const hasSign = px.some((p) => p.type === 'minusSign' || p.type === 'plusSign');
		for (const p of px) {
			if (!done && (p.type === 'minusSign' || p.type === 'plusSign' || (!hasSign && isNumPart(p)))) {
				out.push({ type: 'approximatelySign', value: D.ap });
				done = true;
			}
			out.push(p);
		}
		return out.map((p) => Object.assign({}, p, { source: 'shared' }));
	}
	/* affixes: what surrounds the number */
	const split = (p) => {
		let a = 0, b = p.length;
		while (a < p.length && !isNumPart(p[a])) a++;
		while (b > a && !isNumPart(p[b - 1])) b--;
		return [p.slice(0, a), p.slice(a, b), p.slice(b)];
	};
	const [xa, xn, xz] = split(px), [ya, yn, yz] = split(py);
	const txt = joinParts;
	const noSign = (a) => !a.some((p) => p.type === 'minusSign' || p.type === 'plusSign');
	const cpLen = (a) => [...txt(a)].length;
	/* ICU: the unit (currency, percent, compact, unit: the affixes without the sign) is collapsed
	   when both sides have the same and it is longer than one code point */
	const unsigned = (a) => a.filter((p) => p.type !== 'minusSign' && p.type !== 'plusSign');
	const same = txt(unsigned(xa)) === txt(unsigned(ya)) && txt(xz) === txt(yz);
	const signed = !noSign(xa) || !noSign(ya);
	let collapse = same && cpLen(unsigned(xa)) + cpLen(xz) > 1;
	if (collapse && signed && unsigned(xa).length) collapse = false;
	const collapsePre = collapse && xa.length > 0 && !signed;
	const collapseSuf = collapse && xz.length > 0;
	const left = collapseSuf ? xa.concat(xn) : px;
	const right = collapsePre ? yn.concat(yz) : collapseSuf ? ya.concat(yn) : py;
	const repeated = !collapse ? (xa.length || xz.length) : signed;
	let sep = D.rg;
	if (repeated && !/^\s/.test(sep)) sep = ' ' + sep + ' ';
	const out = [];
	if (collapsePre) for (const p of xa) out.push(Object.assign({}, p, { source: 'shared' }));
	for (const p of (collapsePre ? left.slice(xa.length) : left)) out.push(Object.assign({}, p, { source: 'startRange' }));
	out.push({ type: 'literal', value: sep, source: 'shared' });
	for (const p of right) out.push(Object.assign({}, p, { source: 'endRange' }));
	if (collapseSuf) for (const p of xz) out.push(Object.assign({}, p, { source: 'shared' }));
	return out;
}
function isNumPart(p) {
	return p.type === 'integer' || p.type === 'group' || p.type === 'decimal' || p.type === 'fraction' || p.type === 'nan' ||
		p.type === 'infinity' || p.type === 'exponentSeparator' || p.type === 'exponentMinusSign' || p.type === 'exponentInteger';
}

ctor(NumberFormat, 0, {
	get format() {
		const s = unwrapNF(this, 'Intl.NumberFormat.prototype.format');
		if (!s.boundFormat) {
			const f = (value) => FormatNumeric(s, ToIntlMV(value));
			s.boundFormat = f;
		}
		return s.boundFormat;
	},
	formatToParts(value) {
		const s = NF.get(this, 'Intl.NumberFormat.prototype.formatToParts');
		return partitionNumber(s, ToIntlMV(value)).map((p) => ({ type: p.type, value: p.value }));
	},
	formatRange(start, end) {
		const s = NF.get(this, 'Intl.NumberFormat.prototype.formatRange');
		if (start === undefined || end === undefined) throw new TypeError('start or end is undefined');
		const x = ToIntlMV(start), y = ToIntlMV(end);
		return joinParts(partitionNumberRange(s, x, y));
	},
	formatRangeToParts(start, end) {
		const s = NF.get(this, 'Intl.NumberFormat.prototype.formatRangeToParts');
		if (start === undefined || end === undefined) throw new TypeError('start or end is undefined');
		const x = ToIntlMV(start), y = ToIntlMV(end);
		return partitionNumberRange(s, x, y).map((p) => ({ type: p.type, value: p.value, source: p.source }));
	},
	resolvedOptions() {
		const s = unwrapNF(this, 'Intl.NumberFormat.prototype.resolvedOptions');
		const o = {};
		for (const k of ['locale', 'numberingSystem', 'style', 'currency', 'currencyDisplay', 'currencySign', 'unit', 'unitDisplay',
			'minimumIntegerDigits', 'minimumFractionDigits', 'maximumFractionDigits', 'minimumSignificantDigits',
			'maximumSignificantDigits', 'useGrouping', 'notation', 'compactDisplay', 'signDisplay', 'roundingIncrement',
			'roundingMode', 'roundingPriority', 'trailingZeroDisplay']) {
			if (s[k] !== undefined) o[k] = s[k];
		}
		return o;
	},
}, 'NumberFormat');
def(NumberFormat, { supportedLocalesOf(locales) { return supportedLocalesOf(locales, arguments[1]); } });

/* ==== Intl.PluralRules ================================================================================ */

const PR = slots();
function PluralRules(locales, options) {
	if (new.target === undefined) throw new TypeError('Constructor Intl.PluralRules requires \'new\'');
	const pr = OrdinaryCreateFromConstructor(new.target, PluralRules.prototype);
	const requested = CanonicalizeLocaleList(locales);
	options = CoerceOptionsToObject(options);
	GetOption(options, 'localeMatcher', 'string', ['lookup', 'best fit'], 'best fit');
	const r = ResolveLocale(requested, {}, [], null);
	const s = { __proto__: null, locale: r.locale, dataLocale: dataKey(r.dataLocale) };
	s.type = GetOption(options, 'type', 'string', ['cardinal', 'ordinal'], 'cardinal');
	const notation = GetOption(options, 'notation', 'string', ['standard', 'scientific', 'engineering', 'compact'], 'standard');
	s.notation = notation;
	SetNumberFormatDigitOptions(s, options, 0, 3, notation);
	s.style = 'decimal';
	PR.set(pr, s);
	return pr;
}
function ResolvePlural(s, d) {
	if (d.nan || d.inf) return { cat: 'other', text: '' };
	const f = FormatNumericToString(s, { neg: false, m: d.m, x: d.x });
	const rules = pluralOf(s.dataLocale);
	return { cat: rules[s.type === 'ordinal' ? 1 : 0](operands(f.ip, f.fp, 0)), f };
}
ctor(PluralRules, 0, {
	select(value) {
		const s = PR.get(this, 'Intl.PluralRules.prototype.select');
		return ResolvePlural(s, decOfNumber(ToNumber(value))).cat;
	},
	selectRange(start, end) {
		const s = PR.get(this, 'Intl.PluralRules.prototype.selectRange');
		if (start === undefined || end === undefined) throw new TypeError('start or end is undefined');
		const x = ToNumber(start), y = ToNumber(end);
		if (Number.isNaN(x) || Number.isNaN(y)) throw new RangeError('NaN in selectRange');
		/* CLDR's plural ranges: mostly the end's category */
		const cx = ResolvePlural(s, decOfNumber(x)).cat, cy = ResolvePlural(s, decOfNumber(y)).cat;
		const lang = s.dataLocale.split('-')[0];
		if (lang === 'fr' && cx === 'one' && cy === 'one') return 'one';
		void cx;
		return cy;
	},
	resolvedOptions() {
		const s = PR.get(this, 'Intl.PluralRules.prototype.resolvedOptions');
		const rules = pluralOf(s.dataLocale);
		const cats = rules[s.type === 'ordinal' ? 3 : 2];
		const o = { locale: s.locale, type: s.type, notation: s.notation, minimumIntegerDigits: s.minimumIntegerDigits };
		for (const k of ['minimumFractionDigits', 'maximumFractionDigits', 'minimumSignificantDigits', 'maximumSignificantDigits'])
			if (s[k] !== undefined) o[k] = s[k];
		o.pluralCategories = PLURAL_ORDER.filter((c) => cats.includes(c));
		o.roundingIncrement = s.roundingIncrement;
		o.roundingMode = s.roundingMode;
		o.roundingPriority = s.roundingPriority;
		o.trailingZeroDisplay = s.trailingZeroDisplay;
		return o;
	},
}, 'PluralRules');
def(PluralRules, { supportedLocalesOf(locales) { return supportedLocalesOf(locales, arguments[1]); } });

/* ==== time zones ==================================================================================== */

let TZ = null;
function tzData() {
	if (!TZ) {
		TZ = rec('tz');
		TZ.lower = new Map();
		for (const z of Object.keys(TZ.zones)) TZ.lower.set(z.toLowerCase(), z);
		for (const z of Object.keys(TZ.links)) TZ.lower.set(z.toLowerCase(), z);
	}
	return TZ;
}
const UTC_NAMES = new Set(['utc', 'etc/utc', 'etc/gmt', 'gmt', 'etc/uct', 'uct', 'etc/universal', 'universal', 'etc/zulu', 'zulu',
	'etc/greenwich', 'greenwich', 'etc/gmt0', 'gmt0', 'etc/gmt+0', 'etc/gmt-0', 'gmt+0', 'gmt-0']);
const UTC_CASE = { utc: 'UTC', 'etc/utc': 'Etc/UTC', 'etc/gmt': 'Etc/GMT', gmt: 'GMT', 'etc/uct': 'Etc/UCT', uct: 'UCT',
	'etc/universal': 'Etc/Universal', universal: 'Universal', 'etc/zulu': 'Etc/Zulu', zulu: 'Zulu', 'etc/greenwich': 'Etc/Greenwich',
	greenwich: 'Greenwich', 'etc/gmt0': 'Etc/GMT0', gmt0: 'GMT0', 'etc/gmt+0': 'Etc/GMT+0', 'etc/gmt-0': 'Etc/GMT-0', 'gmt+0': 'GMT+0', 'gmt-0': 'GMT-0' };
const isUTC = (z) => typeof z === 'string' && UTC_NAMES.has(z.toLowerCase());
/* the zone's name, case-normalised as in the IANA data (a link keeps its own name), or undefined */
function namedZone(name) {
	const l = name.toLowerCase();
	if (UTC_CASE[l]) return UTC_CASE[l];
	const g = /^etc\/gmt([+-])(\d{1,2})$/.exec(l);
	if (g && !(g[2].length === 2 && g[2][0] === '0') && Number(g[2]) <= (g[1] === '+' ? 12 : 14)) return 'Etc/GMT' + g[1] + Number(g[2]);
	const T = tzData();
	return T.lower.get(l);
}
function zoneRule(name) {
	const T = tzData();
	let z = T.zones[name];
	if (!z && T.links[name]) z = T.zones[T.links[name]];
	if (!z) {
		const g = /^Etc\/GMT([+-])(\d+)$/.exec(name);
		if (g) z = [(g[1] === '+' ? -60 : 60) * Number(g[2])];
	}
	return z || [0];
}
/* "+01:00", "-0530", "+05": an offset time zone -> minutes, or null */
function parseOffsetZone(s) {
	const m = /^([+\-])([01][0-9]|2[0-3])(?::?([0-5][0-9]))?$/.exec(s);
	if (!m) return null;
	if (s.length === 5 && s[3] !== ':' && false) return null;
	/* \u00b1HH:MM or \u00b1HHMM or \u00b1HH, not \u00b1HH:M */
	if (!/^[+\-]\d\d(:\d\d|\d\d)?$/.test(s)) return null;
	const v = Number(m[2]) * 60 + (m[3] ? Number(m[3]) : 0);
	return m[1] === '+' ? v : -v;
}
function offsetZoneName(min) {
	const a = Math.abs(min);
	return (min < 0 ? '-' : '+') + String(Math.floor(a / 60)).padStart(2, '0') + ':' + String(a % 60).padStart(2, '0');
}
/* a rule's instant in a year (ms, UTC): [month, week (1-4, 5: last), weekday, minutes, basis u/w/s] */
function ruleInstant(r, year, std, save, isStart) {
	const [mon, wk, wd, time, basis] = r;
	let day;
	if (wk === 5) {
		const last = new Date(Date.UTC(year, mon, 0)).getUTCDate();
		const dw = new Date(Date.UTC(year, mon - 1, last)).getUTCDay();
		day = last - ((dw - wd + 7) % 7);
	} else {
		const dw = new Date(Date.UTC(year, mon - 1, 1)).getUTCDay();
		day = 1 + ((wd - dw + 7) % 7) + (wk - 1) * 7;
	}
	let t = Date.UTC(year, mon - 1, day) + time * 60000;
	if (basis === 'w') t -= (isStart ? std : std + save) * 60000;
	else if (basis === 's') t -= std * 60000;
	return t;
}
/* the offset (minutes east of UTC) of a zone at an instant */
function zoneOffset(zone, t) {
	if (typeof zone === 'number') return zone;
	if (isUTC(zone)) return 0;
	const z = zoneRule(zone);
	if (z.length === 1) return z[0];
	const std = z[0];
	let start, end, save;
	if (z.length === 2) {
		const R = tzData().rules[z[1]];
		if (!R) return std;
		[start, end, save] = R;
	} else {
		[, start, end, save] = z;
	}
	const year = new Date(t + std * 60000).getUTCFullYear();
	const a = ruleInstant(start, year, std, save, true), b = ruleInstant(end, year, std, save, false);
	const dst = a < b ? t >= a && t < b : t < b || t >= a;
	return std + (dst ? save : 0);
}
function zoneIsDst(zone, t) {
	if (typeof zone === 'number' || isUTC(zone)) return false;
	const z = zoneRule(zone);
	if (z.length === 1) return false;
	return zoneOffset(zone, t) !== z[0];
}
/* the zones proposed for a region, in order (the default time zone's guess) */
const REGION_ZONES = {
	FR: 'Europe/Paris', BE: 'Europe/Brussels', DE: 'Europe/Berlin', NL: 'Europe/Amsterdam', LU: 'Europe/Luxembourg',
	CH: 'Europe/Zurich', AT: 'Europe/Vienna', IT: 'Europe/Rome', ES: 'Europe/Madrid Atlantic/Canary', PT: 'Europe/Lisbon Atlantic/Azores',
	GB: 'Europe/London', IE: 'Europe/Dublin', SE: 'Europe/Stockholm', NO: 'Europe/Oslo', DK: 'Europe/Copenhagen',
	FI: 'Europe/Helsinki', PL: 'Europe/Warsaw', CZ: 'Europe/Prague', GR: 'Europe/Athens', TR: 'Europe/Istanbul', RU: 'Europe/Moscow',
	US: 'America/New_York America/Chicago America/Denver America/Phoenix America/Los_Angeles America/Anchorage Pacific/Honolulu',
	CA: 'America/Toronto America/Winnipeg America/Edmonton America/Vancouver America/Halifax America/St_Johns',
	MX: 'America/Mexico_City', BR: 'America/Sao_Paulo', AR: 'America/Buenos_Aires', JP: 'Asia/Tokyo', CN: 'Asia/Shanghai',
	TW: 'Asia/Taipei', KR: 'Asia/Seoul', IN: 'Asia/Calcutta', AU: 'Australia/Sydney Australia/Brisbane Australia/Adelaide Australia/Perth',
	NZ: 'Pacific/Auckland', ZA: 'Africa/Johannesburg',
};
const POPULAR_ZONES = 'Europe/London Europe/Paris Europe/Helsinki Europe/Moscow Asia/Dubai Asia/Karachi Asia/Calcutta Asia/Dhaka ' +
	'Asia/Bangkok Asia/Shanghai Asia/Tokyo Australia/Sydney Pacific/Auckland America/Sao_Paulo America/New_York America/Chicago ' +
	'America/Denver America/Phoenix America/Los_Angeles America/Anchorage Pacific/Honolulu Atlantic/Azores America/Halifax';
let DEFAULT_TZ = null;
function DefaultTimeZone() {
	if (DEFAULT_TZ !== null) return DEFAULT_TZ;
	/* the host's zone name (the PC: TZ, /etc/localtime), when the Date agrees with it */
	const now = Date.now();
	const dOff = (t) => -new Date(t).getTimezoneOffset();
	const y = new Date(now).getUTCFullYear();
	const probes = [now, Date.UTC(y, 0, 15), Date.UTC(y, 6, 15)];
	const agrees = (z) => probes.every((t) => zoneOffset(z, t) === dOff(t));
	const agreesNow = (z) => zoneOffset(z, now) === dOff(now);
	let host = null;
	try { host = N.zone ? N.zone() : null; } catch (e) { host = null; }
	if (host) {
		const z = namedZone(host);
		if (z && agreesNow(z)) { DEFAULT_TZ = z; return z; }
	}
	if (probes.every((t) => dOff(t) === 0)) { DEFAULT_TZ = 'UTC'; return 'UTC'; }
	const region = (parseTag(DefaultLocale()) || {}).region;
	const cands = ((region && REGION_ZONES[region.toUpperCase()]) || '').split(' ').filter(Boolean).concat(POPULAR_ZONES.split(' '));
	for (const pass of [agrees, agreesNow]) {
		for (const z of cands) if (pass(z)) { DEFAULT_TZ = z; return z; }
	}
	const off = dOff(now);
	if (off % 60 === 0 && Math.abs(off) <= 14 * 60) DEFAULT_TZ = 'Etc/GMT' + (off > 0 ? '-' : '+') + Math.abs(off / 60);
	else DEFAULT_TZ = offsetZoneName(off);
	if (DEFAULT_TZ === 'Etc/GMT+0' || DEFAULT_TZ === 'Etc/GMT-0') DEFAULT_TZ = 'UTC';
	return DEFAULT_TZ;
}

/* ==== Intl.DateTimeFormat ============================================================================= */

const DTF = slots();
const DT_COMPONENTS = [
	['weekday', ['narrow', 'short', 'long']],
	['era', ['narrow', 'short', 'long']],
	['year', ['2-digit', 'numeric']],
	['month', ['2-digit', 'numeric', 'narrow', 'short', 'long']],
	['day', ['2-digit', 'numeric']],
	['dayPeriod', ['narrow', 'short', 'long']],
	['hour', ['2-digit', 'numeric']],
	['minute', ['2-digit', 'numeric']],
	['second', ['2-digit', 'numeric']],
	['fractionalSecondDigits', null],
	['timeZoneName', ['short', 'long', 'shortOffset', 'longOffset', 'shortGeneric', 'longGeneric']],
];
const HC_KEYDATA = [null, 'h11', 'h12', 'h23', 'h24'];
function dtfKeyData(key) {
	if (key === 'ca') return ['gregory'];
	if (key === 'hc') return HC_KEYDATA;
	return nuKeyData();
}

function DateTimeFormat(locales, options) {
	const nt = new.target || DateTimeFormat;
	const dtf = CreateDateTimeFormat(nt, locales, options, 'any', 'date');
	if (new.target === undefined && isObj(this) && this instanceof DateTimeFormat) {
		Object.defineProperty(this, FALLBACK, { value: dtf, writable: false, enumerable: false, configurable: false });
		return this;
	}
	return dtf;
}
function CreateDateTimeFormat(newTarget, locales, options, required, defaults) {
	const dtf = OrdinaryCreateFromConstructor(newTarget, DateTimeFormat.prototype);
	const requested = CanonicalizeLocaleList(locales);
	options = CoerceOptionsToObject(options);
	const opt = { __proto__: null };
	GetOption(options, 'localeMatcher', 'string', ['lookup', 'best fit'], 'best fit');
	const calendar = GetOption(options, 'calendar', 'string', undefined, undefined);
	checkType(calendar, 'calendar');
	opt.ca = calendar;
	const nuOpt = GetOption(options, 'numberingSystem', 'string', undefined, undefined);
	checkType(nuOpt, 'numberingSystem');
	opt.nu = nuOpt;
	const hour12 = GetOption(options, 'hour12', 'boolean', undefined, undefined);
	let hourCycle = GetOption(options, 'hourCycle', 'string', ['h11', 'h12', 'h23', 'h24'], undefined);
	if (hour12 !== undefined) hourCycle = null;
	opt.hc = hourCycle;
	const r = ResolveLocale(requested, opt, ['ca', 'hc', 'nu'], dtfKeyData);
	const dk = dataKey(r.dataLocale);
	const D = sec(dk, 'd');
	const s = { __proto__: null, locale: r.locale, calendar: r.ca, numberingSystem: r.nu, dataLocale: dk };
	let hc;
	if (hour12 === true) hc = D.hc12;
	else if (hour12 === false) hc = D.hc === 'h24' ? 'h24' : 'h23';
	else { hc = r.hc; if (hc === null || hc === undefined) hc = D.hc; }
	let tz = options.timeZone;
	if (tz === undefined) tz = DefaultTimeZone();
	else {
		tz = ToString(tz);
		const off = parseOffsetZone(tz);
		if (off !== null) tz = offsetZoneName(off);
		else {
			const z = namedZone(tz);
			if (z === undefined) throw new RangeError('Invalid time zone specified: ' + tz);
			tz = z;
		}
	}
	s.timeZone = tz;
	s.zone = parseOffsetZone(tz) !== null ? parseOffsetZone(tz) : tz;
	const fo = { __proto__: null };
	let explicit = false;
	for (const [prop, values] of DT_COMPONENTS) {
		let v;
		if (prop === 'fractionalSecondDigits') v = GetNumberOption(options, 'fractionalSecondDigits', 1, 3, undefined);
		else v = GetOption(options, prop, 'string', values, undefined);
		fo[prop] = v;
		if (v !== undefined) explicit = true;
	}
	GetOption(options, 'formatMatcher', 'string', ['basic', 'best fit'], 'best fit');
	const dateStyle = GetOption(options, 'dateStyle', 'string', ['full', 'long', 'medium', 'short'], undefined);
	const timeStyle = GetOption(options, 'timeStyle', 'string', ['full', 'long', 'medium', 'short'], undefined);
	s.dateStyle = dateStyle;
	s.timeStyle = timeStyle;
	let pattern;
	if (dateStyle !== undefined || timeStyle !== undefined) {
		if (explicit) throw new TypeError('Can\'t set option ' + (Object.keys(fo).find((k) => fo[k] !== undefined)) + ' when ' + (dateStyle !== undefined ? 'dateStyle' : 'timeStyle') + ' is used');
		if (required === 'date' && timeStyle !== undefined) throw new TypeError('Invalid option : timeStyle');
		if (required === 'time' && dateStyle !== undefined) throw new TypeError('Invalid option : dateStyle');
		pattern = stylePattern(D, dateStyle, timeStyle, hc);
	} else {
		let need = true;
		if (required === 'date' || required === 'any') for (const p of ['weekday', 'year', 'month', 'day']) if (fo[p] !== undefined) need = false;
		if (required === 'time' || required === 'any') for (const p of ['dayPeriod', 'hour', 'minute', 'second', 'fractionalSecondDigits']) if (fo[p] !== undefined) need = false;
		if (need && (defaults === 'date' || defaults === 'all')) { fo.year = 'numeric'; fo.month = 'numeric'; fo.day = 'numeric'; }
		if (need && (defaults === 'time' || defaults === 'all')) { fo.hour = 'numeric'; fo.minute = 'numeric'; fo.second = 'numeric'; }
		pattern = componentPattern(D, fo, hc);
	}
	s.pattern = pattern;
	s.compiled = compilePattern(pattern);
	s.hasHour = s.compiled.some((p) => 'hHkK'.includes(p.f));
	/* the hour cycle the pattern shows */
	if (s.hasHour) {
		const h = s.compiled.find((p) => 'hHkK'.includes(p.f)).f;
		s.hourCycle = h === 'h' ? 'h12' : h === 'K' ? 'h11' : h === 'H' ? 'h23' : 'h24';
	}
	s.fo = fo;
	s.boundFormat = undefined;
	DTF.set(dtf, s);
	return dtf;
}
function unwrapDTF(o, what) {
	if (!DTF.has(o) && isObj(o) && o instanceof DateTimeFormat) o = o[FALLBACK];
	return DTF.get(o, what);
}

/* ---- patterns ---- */

function setCycle(p, hc) {
	/* the pattern's hour letters to the cycle's (h12: h, h11: K, h23: H, h24: k) */
	const L = hc === 'h11' ? 'K' : hc === 'h12' ? 'h' : hc === 'h24' ? 'k' : 'H';
	return mapLetters(p, (c, n) => ('hHkK'.includes(c) ? L.repeat(n) : c.repeat(n)));
}
/* rewrite a pattern's fields (outside quotes): f (letter, count) -> text */
function mapLetters(p, f) {
	let out = '';
	for (let i = 0; i < p.length;) {
		const c = p[i];
		if (c === "'") {
			let j = i + 1;
			for (; j < p.length; j++) {
				if (p[j] === "'") { if (p[j + 1] === "'") { j++; continue; } break; }
			}
			out += p.slice(i, j + 1);
			i = j + 1;
		} else if (/[A-Za-z]/.test(c)) {
			let j = i;
			while (p[j] === c) j++;
			out += f(c, j - i);
			i = j;
		} else { out += c; i++; }
	}
	return out;
}
function stylePattern(D, ds, ts, hc) {
	const IDX = { full: 0, long: 1, medium: 2, short: 3 };
	let tp;
	if (ts !== undefined) {
		const cyc12 = hc === 'h11' || hc === 'h12';
		tp = D.ts[IDX[ts]][cyc12 ? 0 : 1];
		if (hc === 'h11' || hc === 'h24') tp = setCycle(tp, hc);
	}
	if (ds === undefined) return tp;
	const dp = D.ds[IDX[ds]];
	if (ts === undefined) return dp;
	return D.sg[IDX[ds]].replace('{1}', dp).replace('{0}', tp);
}
function componentPattern(D, fo, hc) {
	const W = { long: 'l', short: 's', narrow: 's' };
	const hasDate = fo.weekday || fo.era || fo.year || fo.month || fo.day;
	const hasTime = fo.hour || fo.minute || fo.second || fo.fractionalSecondDigits || fo.dayPeriod;
	let dp = null, tp = null;
	if (hasDate) {
		let w = fo.weekday ? W[fo.weekday] : '-';
		let g = fo.era && fo.year && w === '-' ? 's' : '-';
		const y = fo.year ? 'n' : '-';
		const m = !fo.month ? '-' : fo.month === 'numeric' || fo.month === '2-digit' ? 'n' : fo.month === 'short' ? 's' : fo.month === 'long' ? 'l' : 'x';
		const d = fo.day ? 'n' : '-';
		let key = w + g + y + m + d;
		if (key === '-----') key = '--n--';
		dp = D.dp[key] || D.dp['--nnn'];
		/* an era the table has not got (with a weekday, or no year): after the date */
		if (fo.era && g === '-') dp += ' G';
		dp = mapLetters(dp, (c, n) => {
			if (c === 'E' && fo.weekday) return fo.weekday === 'narrow' ? 'EEEEE' : fo.weekday === 'long' ? 'EEEE' : 'EEE';
			if (c === 'c' && fo.weekday) return fo.weekday === 'narrow' ? 'ccccc' : fo.weekday === 'long' ? 'cccc' : 'ccc';
			if (c === 'G' && fo.era) return fo.era === 'narrow' ? 'GGGGG' : fo.era === 'long' ? 'GGGG' : 'G';
			if (c === 'y' && fo.year === '2-digit') return 'yy';
			if ((c === 'M' || c === 'L') && n <= 2 && fo.month === '2-digit') return c + c;
			if (c === 'd' && fo.day === '2-digit') return 'dd';
			return c.repeat(n);
		});
	}
	if (hasTime) {
		const cyc12 = hc === 'h11' || hc === 'h12';
		const hk = fo.hour ? 'n' : '-', mk = fo.minute ? 'n' : '-', sk = fo.second ? 'n' : '-';
		const key = hk + mk + sk;
		if (key !== '---') {
			tp = D.tp[key + (cyc12 ? 1 : 2) + (fo.timeZoneName ? 'z' : '')];
			if (hc === 'h11' || hc === 'h24') tp = setCycle(tp, hc);
			tp = mapLetters(tp, (c, n) => {
				if ('hHkK'.includes(c) && fo.hour === '2-digit') return c + c;
				if (c === 'm' && fo.minute === '2-digit') return 'mm';
				if (c === 's' && fo.second === '2-digit') return 'ss';
				if (c === 'a' && fo.dayPeriod && cyc12) return fo.dayPeriod === 'narrow' ? 'BBBBB' : fo.dayPeriod === 'long' ? 'BBBB' : 'B';
				return c.repeat(n);
			});
		}
		if (fo.fractionalSecondDigits) {
			const S = 'S'.repeat(fo.fractionalSecondDigits);
			if (tp && /s/.test(tp.replace(/'[^']*'/g, ''))) tp = tp.replace(/(s+)(?![^']*'(?:[^']*'[^']*')*[^']*$)/, '$1' + escLit(D.fs) + S);
			else tp = tp ? tp + ' ' + S : S;
		}
		if (!tp && fo.dayPeriod) tp = fo.dayPeriod === 'narrow' ? 'BBBBB' : fo.dayPeriod === 'long' ? 'BBBB' : 'B';
	}
	let p;
	if (dp && tp) {
		const gi = fo.weekday && fo.month === 'long' ? 0 : fo.month === 'long' ? 1 : fo.month === 'short' || fo.month === 'narrow' ? 2 : 3;
		p = D.gl[gi].replace('{1}', dp).replace('{0}', tp);
	} else p = dp || tp || '';
	if (fo.timeZoneName && !/z/.test(tp || '')) {
		/* a date alone with a zone: the locale's "date, zone" */
		const tmpl = D.dz.replace(D.dp['--nnn'], '{0}');
		p = tmpl.indexOf('{0}') >= 0 ? tmpl.replace('{0}', p) : p + ' z';
	}
	if (fo.timeZoneName) {
		const Z = { short: 'z', long: 'zzzz', shortOffset: 'O', longOffset: 'OOOO', shortGeneric: 'v', longGeneric: 'vvvv' }[fo.timeZoneName];
		p = mapLetters(p, (c, n) => (c === 'z' ? Z : c.repeat(n)));
	}
	return p;
}
function escLit(s) { return /[A-Za-z']/.test(s) ? "'" + s.replace(/'/g, "''") + "'" : s; }
function compilePattern(p) {
	const out = [];
	let lit = '';
	for (let i = 0; i < p.length;) {
		const c = p[i];
		if (c === "'") {
			if (p[i + 1] === "'") { lit += "'"; i += 2; continue; }
			let j = i + 1;
			for (; j < p.length; j++) {
				if (p[j] === "'") { if (p[j + 1] === "'") { lit += "'"; j++; continue; } break; }
				lit += p[j];
			}
			i = j + 1;
		} else if (/[A-Za-z]/.test(c)) {
			if (lit) { out.push({ lit }); lit = ''; }
			let j = i;
			while (p[j] === c) j++;
			out.push({ f: c, n: j - i });
			i = j;
		} else { lit += c; i++; }
	}
	if (lit) out.push({ lit });
	return out;
}

/* ---- formatting ---- */

function TimeClip(t) {
	if (!Number.isFinite(t) || Math.abs(t) > 8.64e15) return NaN;
	return Math.trunc(t) + 0;
}
/* the fields of an instant in a zone */
function localFields(t, zone) {
	const off = zoneOffset(zone, t);
	const d = new Date(t + off * 60000);
	return {
		y: d.getUTCFullYear(), M: d.getUTCMonth(), d: d.getUTCDate(), wd: d.getUTCDay(),
		h: d.getUTCHours(), m: d.getUTCMinutes(), s: d.getUTCSeconds(), ms: d.getUTCMilliseconds(), off, t,
	};
}
function pad(n, w) { const s = String(n); return s.length >= w ? s : '0'.repeat(w - s.length) + s; }
function flexiblePeriod(D, w, h, m) {
	const e = D.fp[w];
	if (h === 12 && m === 0 && e.noon) return e.noon;
	if (h === 0 && m === 0 && e.mid) return e.mid;
	let v = '';
	for (const run of e.r.split('|')) {
		const i = run.indexOf(':');
		if (Number(run.slice(0, i)) <= h) v = run.slice(i + 1);
	}
	return v;
}
/* the zone's name in the locale: short z, long zzzz, O, OOOO, v, vvvv */
function zoneName(s, f, width, isLong) {
	const zone = s.zone;
	const T = sec(s.dataLocale, 'z');
	const off = f.off;
	const gmt = (long) => {
		const g = T.g;
		const pre = g[0].replace(/[+\-\u2212].*$/, '');
		if (off === 0) return pre + (long ? '+00:00' : '+0');
		const minus = /\u2212/.test(g[3]) ? '\u2212' : '-';
		const a = Math.abs(off), hh = Math.floor(a / 60), mm = a % 60;
		const sign = off < 0 ? minus : '+';
		if (long) return pre + sign + pad(hh, 2) + ':' + pad(mm, 2);
		return pre + sign + hh + (mm ? ':' + pad(mm, 2) : '');
	};
	if (width === 'O') return gmt(isLong);
	if (typeof zone === 'number') return gmt(isLong);
	let names = T.z[zone];
	if (!names) {
		const TT = tzData();
		const link = TT.links[zone];
		if (link) names = T.z[link];
		if (!names) for (const k of Object.keys(TT.links)) if (TT.links[k] === zone && T.z[k]) { names = T.z[k]; break; }
	}
	if (isUTC(zone)) names = names || T.z.UTC;
	const dst = zoneIsDst(zone, f.t);
	let v = 0;
	if (names) {
		if (width === 'z') v = isLong ? names[dst ? 3 : 2] : names[dst ? 1 : 0];
		else v = isLong ? names[5] : names[4];
	}
	return v || gmt(isLong);
}
function partitionDate(s, x) {
	const D = sec(s.dataLocale, 'd');
	const digits = digitsOf(s.numberingSystem);
	const f = localFields(x, s.zone);
	const out = [];
	const num = (v) => transliterate(v, digits);
	for (const p of s.compiled) {
		if (p.lit !== undefined) { out.push({ type: 'literal', value: p.lit }); continue; }
		const n = p.n;
		switch (p.f) {
		case 'G': out.push({ type: 'era', value: D.era[n === 4 ? 0 : n === 5 ? 2 : 1][f.y > 0 ? 1 : 0] }); break;
		case 'y': case 'u': {
			const ey = f.y > 0 ? f.y : 1 - f.y;
			out.push({ type: 'year', value: num(n === 2 ? pad(ey % 100, 2) : String(ey)) });
			break;
		}
		case 'M': case 'L': {
			if (n <= 2) { out.push({ type: 'month', value: num(pad(f.M + 1, n)) }); break; }
			const tbl = p.f === 'L' && D.monS ? D.monS : D.mon;
			out.push({ type: 'month', value: tbl[n === 3 ? 1 : n === 4 ? 0 : 2][f.M] });
			break;
		}
		case 'd': out.push({ type: 'day', value: num(pad(f.d, n)) }); break;
		case 'E': case 'c': {
			const tbl = p.f === 'c' && D.wdS ? D.wdS : D.wd;
			out.push({ type: 'weekday', value: tbl[n === 4 ? 0 : n === 5 ? 2 : 1][f.wd] });
			break;
		}
		case 'a': out.push({ type: 'dayPeriod', value: D.ap[f.h < 12 ? 0 : 1] }); break;
		case 'B': out.push({ type: 'dayPeriod', value: flexiblePeriod(D, n === 5 ? 0 : n === 4 ? 2 : 1, f.h, f.m) }); break;
		case 'h': out.push({ type: 'hour', value: num(pad(f.h % 12 === 0 ? 12 : f.h % 12, n)) }); break;
		case 'K': out.push({ type: 'hour', value: num(pad(f.h % 12, n)) }); break;
		case 'H': out.push({ type: 'hour', value: num(pad(f.h, n)) }); break;
		case 'k': out.push({ type: 'hour', value: num(pad(f.h === 0 ? 24 : f.h, n)) }); break;
		case 'm': out.push({ type: 'minute', value: num(pad(f.m, n)) }); break;
		case 's': out.push({ type: 'second', value: num(pad(f.s, n)) }); break;
		case 'S': out.push({ type: 'fractionalSecond', value: num(pad(f.ms, 3).slice(0, n)) }); break;
		case 'z': out.push({ type: 'timeZoneName', value: zoneName(s, f, 'z', n >= 4) }); break;
		case 'O': out.push({ type: 'timeZoneName', value: zoneName(s, f, 'O', n >= 4) }); break;
		case 'v': out.push({ type: 'timeZoneName', value: zoneName(s, f, 'v', n >= 4) }); break;
		default: out.push({ type: 'literal', value: p.f.repeat(n) });
		}
	}
	return mergeLiterals(out);
}
function dateValue(date) {
	const x = date === undefined ? Date.now() : ToNumber(date);
	const t = TimeClip(x);
	if (Number.isNaN(t)) throw new RangeError('Invalid time value');
	return t;
}

/* ---- ranges ---- */

const FIELD_RANK = { era: 7, year: 6, month: 5, day: 4, weekday: 4, dayPeriod: 3, hour: 2, minute: 1, second: 0, fractionalSecond: -1 };
function partitionDateRange(s, x, y) {
	const a = partitionDate(s, x), b = partitionDate(s, y);
	const D = sec(s.dataLocale, 'd');
	const same = a.length === b.length && a.every((p, i) => p.value === b[i].value);
	if (same) return a.map((p) => Object.assign({}, p, { source: 'shared' }));
	/* the greatest field that differs */
	let top = -2;
	a.forEach((p, i) => { if (p.type !== 'literal' && p.value !== b[i].value) top = Math.max(top, FIELD_RANK[p.type] !== undefined ? FIELD_RANK[p.type] : 8); });
	const hasTime = a.some((p) => p.type === 'hour' || p.type === 'minute');
	const hasDate = a.some((p) => p.type === 'year' || p.type === 'month' || p.type === 'day');
	const numericMonth = s.compiled.some((p) => (p.f === 'M' || p.f === 'L') && p.n <= 2);
	const full = () => {
		const sep = D.rs[3];
		return a.map((p) => Object.assign({}, p, { source: 'startRange' }))
			.concat([{ type: 'literal', value: sep, source: 'shared' }], b.map((p) => Object.assign({}, p, { source: 'endRange' })));
	};
	if (a.length !== b.length || top >= 6 || (top >= 4 && (hasTime || numericMonth)) || top === 8) return full();
	/* the varying span: every field at or under the top rank (the time's fields together) */
	let i0 = -1, i1 = -1;
	a.forEach((p, i) => {
		if (p.type === 'literal') return;
		const rk = FIELD_RANK[p.type];
		/* the time's numbers vary together (10:00 \u2013 11:00 AM); a date's fields up to the top one */
		const varies = p.value !== b[i].value || (rk !== undefined && (top <= 3 ? rk <= 2 : rk >= 4 && rk <= top));
		if (varies) { if (i0 < 0) i0 = i; i1 = i; }
	});
	if (i0 < 0) return full();
	const sep = top <= 3 ? D.rs[2] : top === 4 ? D.rs[0] : D.rs[1];
	const out = [];
	a.forEach((p, i) => { if (i < i0) out.push(Object.assign({}, p, { source: 'shared' })); });
	for (let i = i0; i <= i1; i++) out.push(Object.assign({}, a[i], { source: 'startRange' }));
	out.push({ type: 'literal', value: sep, source: 'shared' });
	for (let i = i0; i <= i1; i++) out.push(Object.assign({}, b[i], { source: 'endRange' }));
	a.forEach((p, i) => { if (i > i1) out.push(Object.assign({}, p, { source: 'shared' })); });
	void hasDate;
	return out;
}

ctor(DateTimeFormat, 0, {
	get format() {
		const s = unwrapDTF(this, 'Intl.DateTimeFormat.prototype.format');
		if (!s.boundFormat) s.boundFormat = (date) => joinParts(partitionDate(s, dateValue(date)));
		return s.boundFormat;
	},
	formatToParts(date) {
		const s = DTF.get(this, 'Intl.DateTimeFormat.prototype.formatToParts');
		return partitionDate(s, dateValue(date)).map((p) => ({ type: p.type, value: p.value }));
	},
	formatRange(startDate, endDate) {
		const s = DTF.get(this, 'Intl.DateTimeFormat.prototype.formatRange');
		if (startDate === undefined || endDate === undefined) throw new TypeError('startDate or endDate is undefined');
		const x = dateValue(startDate), y = dateValue(endDate);
		return joinParts(partitionDateRange(s, x, y));
	},
	formatRangeToParts(startDate, endDate) {
		const s = DTF.get(this, 'Intl.DateTimeFormat.prototype.formatRangeToParts');
		if (startDate === undefined || endDate === undefined) throw new TypeError('startDate or endDate is undefined');
		const x = dateValue(startDate), y = dateValue(endDate);
		return partitionDateRange(s, x, y).map((p) => ({ type: p.type, value: p.value, source: p.source }));
	},
	resolvedOptions() {
		const s = unwrapDTF(this, 'Intl.DateTimeFormat.prototype.resolvedOptions');
		const o = { locale: s.locale, calendar: s.calendar, numberingSystem: s.numberingSystem, timeZone: s.timeZone };
		if (s.hasHour) {
			o.hourCycle = s.hourCycle;
			o.hour12 = s.hourCycle === 'h11' || s.hourCycle === 'h12';
		}
		if (s.dateStyle === undefined && s.timeStyle === undefined) {
			const W = (n) => (n === 4 ? 'long' : n === 5 ? 'narrow' : 'short');
			for (const p of s.compiled) {
				if (p.lit !== undefined) continue;
				const n = p.n;
				switch (p.f) {
				case 'E': case 'c': o.weekday = W(n); break;
				case 'G': o.era = W(n); break;
				case 'y': o.year = n === 2 ? '2-digit' : 'numeric'; break;
				case 'M': case 'L': o.month = n === 1 ? 'numeric' : n === 2 ? '2-digit' : W(n === 3 ? 1 : n); break;
				case 'd': o.day = n === 2 ? '2-digit' : 'numeric'; break;
				case 'B': o.dayPeriod = W(n); break;
				case 'h': case 'H': case 'k': case 'K': o.hour = n === 2 ? '2-digit' : 'numeric'; break;
				case 'm': o.minute = n === 2 ? '2-digit' : 'numeric'; break;
				case 's': o.second = n === 2 ? '2-digit' : 'numeric'; break;
				case 'S': o.fractionalSecondDigits = n; break;
				case 'z': case 'O': case 'v': o.timeZoneName = s.fo.timeZoneName; break;
				default:
				}
			}
			const ordered = {};
			for (const k of ['locale', 'calendar', 'numberingSystem', 'timeZone', 'hourCycle', 'hour12', 'weekday', 'era', 'year',
				'month', 'day', 'dayPeriod', 'hour', 'minute', 'second', 'fractionalSecondDigits', 'timeZoneName'])
				if (o[k] !== undefined) ordered[k] = o[k];
			return ordered;
		}
		if (s.dateStyle !== undefined) o.dateStyle = s.dateStyle;
		if (s.timeStyle !== undefined) o.timeStyle = s.timeStyle;
		return o;
	},
}, 'DateTimeFormat');
def(DateTimeFormat, { supportedLocalesOf(locales) { return supportedLocalesOf(locales, arguments[1]); } });

/* ==== Intl.RelativeTimeFormat ========================================================================= */

const RTF = slots();
const RT_UNITS = { second: 'second', seconds: 'second', minute: 'minute', minutes: 'minute', hour: 'hour', hours: 'hour',
	day: 'day', days: 'day', week: 'week', weeks: 'week', month: 'month', months: 'month', quarter: 'quarter',
	quarters: 'quarter', year: 'year', years: 'year' };
function RelativeTimeFormat(locales, options) {
	if (new.target === undefined) throw new TypeError('Constructor Intl.RelativeTimeFormat requires \'new\'');
	const o = OrdinaryCreateFromConstructor(new.target, RelativeTimeFormat.prototype);
	const requested = CanonicalizeLocaleList(locales);
	options = CoerceOptionsToObject(options);
	const opt = { __proto__: null };
	GetOption(options, 'localeMatcher', 'string', ['lookup', 'best fit'], 'best fit');
	const nu = GetOption(options, 'numberingSystem', 'string', undefined, undefined);
	checkType(nu, 'numberingSystem');
	opt.nu = nu;
	const r = ResolveLocale(requested, opt, ['nu'], nuKeyData);
	const s = { __proto__: null, locale: r.locale, dataLocale: dataKey(r.dataLocale), numberingSystem: r.nu };
	s.style = GetOption(options, 'style', 'string', ['long', 'short', 'narrow'], 'long');
	s.numeric = GetOption(options, 'numeric', 'string', ['always', 'auto'], 'always');
	s.nf = NF.get(new NumberFormat(r.locale.replace(/-u-.*$/, '') + (r.nu !== 'latn' ? '-u-nu-' + r.nu : '')));
	RTF.set(o, s);
	return o;
}
function partitionRelative(s, value, unit) {
	value = ToNumber(value);
	unit = ToString(unit);
	if (!Number.isFinite(value)) throw new RangeError('Invalid value');
	const u = RT_UNITS[unit];
	if (!u) throw new RangeError('Invalid unit argument \'' + unit + '\'');
	const R = sec(s.dataLocale, 'r')[s.style[0]][u];
	if (s.numeric === 'auto' && R.a) {
		const k = Object.is(value, -0) ? '-0' : String(value);
		const w = R.a[k === '-0' ? '0' : k];
		if (w !== undefined && (k !== '-0' || R.a['0'] !== undefined)) return [{ type: 'literal', value: w }];
	}
	const neg = value < 0 || Object.is(value, -0);
	const d = decOfNumber(Math.abs(value));
	const parts = partitionNumber(s.nf, d);
	const t = tpl(byPlural(neg ? R.p : R.f, parts.cat));
	const out = [];
	for (const e of t) {
		if (e.k === 'num') for (const p of parts) out.push({ type: p.type, value: p.value, unit: u });
		else if (e.k === 'lit' || e.k === 'x') out.push({ type: 'literal', value: e.v });
	}
	return mergeLiterals(out);
}
ctor(RelativeTimeFormat, 0, {
	format(value, unit) { return joinParts(partitionRelative(RTF.get(this, 'Intl.RelativeTimeFormat.prototype.format'), value, unit)); },
	formatToParts(value, unit) {
		return partitionRelative(RTF.get(this, 'Intl.RelativeTimeFormat.prototype.formatToParts'), value, unit)
			.map((p) => (p.unit ? { type: p.type, value: p.value, unit: p.unit } : { type: p.type, value: p.value }));
	},
	resolvedOptions() {
		const s = RTF.get(this, 'Intl.RelativeTimeFormat.prototype.resolvedOptions');
		return { locale: s.locale, style: s.style, numeric: s.numeric, numberingSystem: s.numberingSystem };
	},
}, 'RelativeTimeFormat');
def(RelativeTimeFormat, { supportedLocalesOf(locales) { return supportedLocalesOf(locales, arguments[1]); } });

/* ==== Intl.ListFormat ================================================================================ */

const LF = slots();
function ListFormat(locales, options) {
	if (new.target === undefined) throw new TypeError('Constructor Intl.ListFormat requires \'new\'');
	const o = OrdinaryCreateFromConstructor(new.target, ListFormat.prototype);
	const requested = CanonicalizeLocaleList(locales);
	options = GetOptionsObject(options);
	GetOption(options, 'localeMatcher', 'string', ['lookup', 'best fit'], 'best fit');
	const r = ResolveLocale(requested, {}, [], null);
	const s = { __proto__: null, locale: r.locale, dataLocale: dataKey(r.dataLocale) };
	s.type = GetOption(options, 'type', 'string', ['conjunction', 'disjunction', 'unit'], 'conjunction');
	s.style = GetOption(options, 'style', 'string', ['long', 'short', 'narrow'], 'long');
	LF.set(o, s);
	return o;
}
function StringListFromIterable(it) {
	if (it === undefined) return [];
	const out = [];
	for (const v of it) {
		if (typeof v !== 'string') throw new TypeError('Iterable yielded ' + String(v) + ' which is not a string');
		out.push(v);
	}
	return out;
}
function listStringsSafe(it) {
	/* the iterator closed on an error (IteratorClose), as for-of does */
	return StringListFromIterable(it);
}
function partitionList(s, list) {
	const n = list.length;
	if (n === 0) return [];
	const P = sec(s.dataLocale, 'l')[s.type[0] + s.style[0]];
	const lang = s.dataLocale.split('-')[0];
	const el = (v) => ({ type: 'element', value: v });
	const joint = (sep, next) => {
		/* Spanish: y -> e before i- / hi-, o -> u before o- / ho- */
		if (lang === 'es') {
			if (sep === ' y ' && /^(i|hi)(?!e)/i.test(next)) return ' e ';
			if (sep === ' o ' && /^(o|ho|8|11(?!\d))/i.test(next)) return ' u ';
		}
		return sep;
	};
	if (n === 1) return [el(list[0])];
	if (n === 2) return [el(list[0]), { type: 'literal', value: joint(P[3], list[1]) }, el(list[1])];
	const out = [el(list[0])];
	for (let i = 1; i < n; i++) {
		const sep = i === 1 ? P[0] : i === n - 1 ? P[2] : P[1];
		out.push({ type: 'literal', value: joint(sep, list[i]) }, el(list[i]));
	}
	return out;
}
ctor(ListFormat, 0, {
	format(list) {
		const s = LF.get(this, 'Intl.ListFormat.prototype.format');
		return joinParts(partitionList(s, listStringsSafe(list)));
	},
	formatToParts(list) {
		const s = LF.get(this, 'Intl.ListFormat.prototype.formatToParts');
		return partitionList(s, listStringsSafe(list));
	},
	resolvedOptions() {
		const s = LF.get(this, 'Intl.ListFormat.prototype.resolvedOptions');
		return { locale: s.locale, type: s.type, style: s.style };
	},
}, 'ListFormat');
def(ListFormat, { supportedLocalesOf(locales) { return supportedLocalesOf(locales, arguments[1]); } });

/* ==== Intl.Collator ================================================================================== */

/* A simplified Unicode collation: primary = the base letters (accents folded, NFD), letters after
   digits after symbols after punctuation after spaces; secondary = accents; tertiary = case
   (lower first); the numeric option compares digit runs as numbers. */
const CO = slots();
function Collator(locales, options) {
	const nt = new.target || Collator;
	const o = OrdinaryCreateFromConstructor(nt, Collator.prototype);
	const requested = CanonicalizeLocaleList(locales);
	options = CoerceOptionsToObject(options);
	const usage = GetOption(options, 'usage', 'string', ['sort', 'search'], 'sort');
	const opt = { __proto__: null };
	GetOption(options, 'localeMatcher', 'string', ['lookup', 'best fit'], 'best fit');
	const collation = GetOption(options, 'collation', 'string', undefined, undefined);
	checkType(collation, 'collation');
	opt.co = collation;
	const numeric = GetOption(options, 'numeric', 'boolean', undefined, undefined);
	opt.kn = numeric === undefined ? undefined : String(numeric);
	const caseFirst = GetOption(options, 'caseFirst', 'string', ['upper', 'lower', 'false'], undefined);
	opt.kf = caseFirst;
	const r = ResolveLocale(requested, opt, ['co', 'kf', 'kn'], (k) => (k === 'co' ? [null] : k === 'kf' ? ['false', 'lower', 'upper'] : ['false', 'true']));
	const s = { __proto__: null, locale: r.locale, usage, collation: 'default', numeric: r.kn === 'true', caseFirst: r.kf || 'false' };
	s.lang = dataKey(r.dataLocale).split('-')[0];
	let sens = GetOption(options, 'sensitivity', 'string', ['base', 'accent', 'case', 'variant'], undefined);
	if (sens === undefined) sens = 'variant';
	s.sensitivity = sens;
	s.ignorePunctuation = GetOption(options, 'ignorePunctuation', 'boolean', undefined, false);
	s.boundCompare = undefined;
	CO.set(o, s);
	return o;
}
const SPECIAL_FOLD = { '\u00df': 'ss', '\u00e6': 'ae', '\u00c6': 'AE', '\u0153': 'oe', '\u0152': 'OE', '\u00f8': 'o', '\u00d8': 'O', '\u0111': 'd', '\u0110': 'D', '\u0142': 'l', '\u0141': 'L', '\u0131': 'i', '\u00fe': 'th', '\u00de': 'TH', '\u00f0': 'd', '\u00d0': 'D', '\u0127': 'h', '\u0126': 'H' };
/* the class of a (base) character: 0 space, 1 punctuation, 2 symbol, 3 currency, 4 digit, 5 letter */
function cclass(c) {
	if (/\s/.test(c)) return 0;
	if (/\p{P}/u.test(c)) return 1;
	if (/\p{Sc}/u.test(c)) return 3;
	if (/\p{S}/u.test(c)) return 2;
	if (/\p{N}/u.test(c)) return 4;
	return 5;
}
/* the collation elements of a string: [primary, secondary, tertiary] per unit */
const KEY_CACHE = new Map();
function collKeys(str, s) {
	const ck = s.lang + (s.ignorePunctuation ? '1' : '0') + str;
	let k = KEY_CACHE.get(ck);
	if (k) return k;
	k = [];
	const nfd = str.normalize('NFD');
	let i = 0;
	const chars = [...nfd];
	while (i < chars.length) {
		let c = chars[i++];
		let acc = '';
		while (i < chars.length && /\p{M}/u.test(chars[i])) acc += chars[i++];
		if (/\p{M}/u.test(c)) { if (k.length) k[k.length - 1].s += c; continue; }
		const low = c.toLowerCase();
		const upper = c !== low;
		let base = SPECIAL_FOLD[c] ? SPECIAL_FOLD[c].toLowerCase() : low;
		const variant = SPECIAL_FOLD[c] ? c : '';
		const cl = cclass(c);
		if (s.ignorePunctuation && (cl === 0 || cl === 1)) continue;
		/* tailorings: Spanish \u00f1, Nordic, German none */
		if (s.lang === 'es' && low === 'n' && acc === '\u0303') { base = 'n\uffff'; acc = ''; }
		if ((s.lang === 'sv' || s.lang === 'fi') && (low === 'a' || low === 'o') && (acc === '\u030a' || acc === '\u0308')) { base = low === 'a' ? (acc === '\u030a' ? 'z\u0001' : 'z\u0002') : 'z\u0003'; acc = ''; }
		if ((s.lang === 'da' || s.lang === 'nb') && ((low === 'a' && acc === '\u030a') || c === '\u00f8' || c === '\u00d8' || low === '\u00e6')) { base = low === '\u00e6' || c === '\u00e6' || c === '\u00c6' ? 'z\u0001' : c === '\u00f8' || c === '\u00d8' ? 'z\u0002' : 'z\u0003'; acc = ''; }
		k.push({ c: cl, p: base, s: acc + variant, t: upper ? 1 : 0, d: cl === 4 ? c : null });
	}
	if (KEY_CACHE.size > 2000) KEY_CACHE.clear();
	KEY_CACHE.set(ck, k);
	return k;
}
function cmpStr(a, b) { return a < b ? -1 : a > b ? 1 : 0; }
function digitVal(c) {
	/* any Unicode decimal digit -> 0-9 */
	const n = c.codePointAt(0);
	if (n >= 48 && n <= 57) return n - 48;
	for (let z = n; z > n - 10; z--) {
		if (!/\p{Nd}/u.test(String.fromCodePoint(z - 1))) return n - z;
	}
	return 0;
}
function CompareStrings(s, x, y) {
	if (x === y) return 0;
	const A = collKeys(x, s), B = collKeys(y, s);
	/* primary */
	let i = 0, j = 0;
	while (i < A.length && j < B.length) {
		const a = A[i], b = B[j];
		if (s.numeric && a.c === 4 && b.c === 4) {
			let na = '', nb = '';
			while (i < A.length && A[i].c === 4) na += digitVal(A[i++].d);
			while (j < B.length && B[j].c === 4) nb += digitVal(B[j++].d);
			na = na.replace(/^0+(?=.)/, ''); nb = nb.replace(/^0+(?=.)/, '');
			if (na.length !== nb.length) return na.length < nb.length ? -1 : 1;
			if (na !== nb) return na < nb ? -1 : 1;
			continue;
		}
		if (a.c !== b.c) return a.c < b.c ? -1 : 1;
		const pa = a.c === 4 ? String(digitVal(a.d)) : a.p, pb = b.c === 4 ? String(digitVal(b.d)) : b.p;
		if (pa !== pb) return cmpStr(pa, pb);
		i++; j++;
	}
	if (i < A.length || j < B.length) return i < A.length ? 1 : -1;
	if (s.sensitivity === 'base') return 0;
	/* secondary: the accents */
	if (s.sensitivity !== 'case') {
		for (let k = 0; k < Math.min(A.length, B.length); k++) if (A[k].s !== B[k].s) return A[k].s === '' ? -1 : B[k].s === '' ? 1 : cmpStr(A[k].s, B[k].s);
	}
	if (s.sensitivity === 'accent') return 0;
	/* tertiary: the case */
	for (let k = 0; k < Math.min(A.length, B.length); k++) {
		if (A[k].t !== B[k].t) {
			const lowerFirst = s.caseFirst !== 'upper';
			return (A[k].t < B[k].t) === lowerFirst ? -1 : 1;
		}
	}
	if (s.sensitivity === 'case') return 0;
	/* variant: the code points */
	const nx = x.normalize('NFD'), ny = y.normalize('NFD');
	return nx === ny ? 0 : cmpStr(nx, ny);
}
ctor(Collator, 0, {
	get compare() {
		const s = CO.get(this, 'Intl.Collator.prototype.compare');
		if (!s.boundCompare) s.boundCompare = (x, y) => CompareStrings(s, ToString(x), ToString(y));
		return s.boundCompare;
	},
	resolvedOptions() {
		const s = CO.get(this, 'Intl.Collator.prototype.resolvedOptions');
		return { locale: s.locale, usage: s.usage, sensitivity: s.sensitivity, ignorePunctuation: s.ignorePunctuation,
			collation: s.collation, numeric: s.numeric, caseFirst: s.caseFirst };
	},
}, 'Collator');
def(Collator, { supportedLocalesOf(locales) { return supportedLocalesOf(locales, arguments[1]); } });

/* ==== Intl.Segmenter ================================================================================= */

const SG = slots();
function Segmenter(locales, options) {
	if (new.target === undefined) throw new TypeError('Constructor Intl.Segmenter requires \'new\'');
	const o = OrdinaryCreateFromConstructor(new.target, Segmenter.prototype);
	const requested = CanonicalizeLocaleList(locales);
	options = GetOptionsObject(options);
	GetOption(options, 'localeMatcher', 'string', ['lookup', 'best fit'], 'best fit');
	const r = ResolveLocale(requested, {}, [], null);
	const granularity = GetOption(options, 'granularity', 'string', ['grapheme', 'word', 'sentence'], 'grapheme');
	SG.set(o, { __proto__: null, locale: r.locale, granularity });
	return o;
}
/* the boundaries of a string: [0, ..., length], and for words whether each segment is word-like */
const GRAPHEME = /\r\n|(?:\p{RI}\p{RI})|(?:[\u1100-\u115f\ua960-\ua97c]+[\u1160-\u11a7\ud7b0-\ud7c6]*[\u11a8-\u11ff\ud7cb-\ud7fb]*)|(?:(?:\p{Extended_Pictographic}(?:[\p{M}\u200c\ufe0f\p{EMod}]*\u200d\p{Extended_Pictographic})*|[^\r\n])[\p{M}\u200c\u200d\ufe0f\p{EMod}\u{E0020}-\u{E007F}]*)|[\s\S]/gu;
function graphemes(str) {
	const b = [0];
	GRAPHEME.lastIndex = 0;
	let m;
	while ((m = GRAPHEME.exec(str)) !== null) {
		if (m[0] === '') { GRAPHEME.lastIndex++; continue; }
		b.push(GRAPHEME.lastIndex);
	}
	return { b };
}
const WORD = /(?:[\p{L}\p{M}\p{N}_]+(?:['\u2019.:\u00b7][\p{L}\p{M}\p{N}_]+)*)|\r\n|\s|[\s\S]/gu;
const CJK = /[\p{sc=Han}\p{sc=Hiragana}\p{sc=Katakana}\p{sc=Thai}]/u;
function words(str) {
	const b = [0], wl = [];
	WORD.lastIndex = 0;
	let m;
	while ((m = WORD.exec(str)) !== null) {
		const t = m[0];
		/* CJK / Thai without spaces: a character at a time (no dictionary) */
		if (t.length > 1 && CJK.test(t)) {
			let pos = m.index;
			for (const ch of t) { pos += ch.length; b.push(pos); wl.push(true); }
			continue;
		}
		b.push(WORD.lastIndex);
		wl.push(/[\p{L}\p{N}]/u.test(t));
	}
	return { b, wl };
}
function sentences(str) {
	const b = [0];
	const re = /[^.!?\u3002\uff01\uff1f]*(?:[.!?\u3002\uff01\uff1f]+["'\u2019\u201d)\]]*(?:\s+|$)|$)/gu;
	re.lastIndex = 0;
	let m;
	while ((m = re.exec(str)) !== null) {
		if (m[0] === '') { if (re.lastIndex >= str.length) break; re.lastIndex++; continue; }
		b.push(re.lastIndex);
		if (re.lastIndex >= str.length) break;
	}
	if (b[b.length - 1] !== str.length) b.push(str.length);
	return { b };
}
function breaks(granularity, str) {
	return granularity === 'word' ? words(str) : granularity === 'sentence' ? sentences(str) : graphemes(str);
}
function segmentData(seg, idx) {
	const i = seg.br.b.findIndex((x, k) => k > 0 && x > idx) - 1;
	if (i < 0) return undefined;
	const start = seg.br.b[i], end = seg.br.b[i + 1];
	const o = { segment: seg.str.slice(start, end), index: start, input: seg.str };
	if (seg.granularity === 'word') o.isWordLike = seg.br.wl[i];
	return o;
}
const SEGMENTS_PROTO = def({}, {
	containing(index) {
		const seg = SEGS.get(this, '%Segments.prototype%.containing');
		const n = ToIntegerOrInfinity(index);
		if (n < 0 || n >= seg.str.length) return undefined;
		return segmentData(seg, n);
	},
	[Symbol.iterator]() {
		const seg = SEGS.get(this, '%Segments.prototype%[@@iterator]');
		const it = Object.create(SEG_ITER_PROTO);
		ITER.set(it, { seg, pos: 0 });
		return it;
	},
});
const SEGS = slots(), ITER = slots();
const SEG_ITER_PROTO = Object.create(Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())));
def(SEG_ITER_PROTO, {
	next() {
		const it = ITER.get(this, '%SegmentIterator.prototype%.next');
		if (it.pos >= it.seg.str.length) return { value: undefined, done: true };
		const d = segmentData(it.seg, it.pos);
		it.pos = d.index + d.segment.length;
		return { value: d, done: false };
	},
});
tag(SEG_ITER_PROTO, 'Segmenter String Iterator');
ctor(Segmenter, 0, {
	segment(string) {
		const s = SG.get(this, 'Intl.Segmenter.prototype.segment');
		const str = ToString(string);
		const o = Object.create(SEGMENTS_PROTO);
		SEGS.set(o, { str, granularity: s.granularity, br: breaks(s.granularity, str) });
		return o;
	},
	resolvedOptions() {
		const s = SG.get(this, 'Intl.Segmenter.prototype.resolvedOptions');
		return { locale: s.locale, granularity: s.granularity };
	},
}, 'Segmenter');
def(Segmenter, { supportedLocalesOf(locales) { return supportedLocalesOf(locales, arguments[1]); } });

/* ==== Intl.DisplayNames ============================================================================== */

const DN = slots();
const DN_LISTS = { langs: META.langs.split(' '), regions: META.regions.split(' '), scripts: META.scripts.split(' '),
	clist: META.clist.split(' '), cals: META.cals.split(' '), fields: META.fields.split(' ') };
function DisplayNames(locales, options) {
	if (new.target === undefined) throw new TypeError('Constructor Intl.DisplayNames requires \'new\'');
	const o = OrdinaryCreateFromConstructor(new.target, DisplayNames.prototype);
	const requested = CanonicalizeLocaleList(locales);
	options = GetOptionsObject(options);
	GetOption(options, 'localeMatcher', 'string', ['lookup', 'best fit'], 'best fit');
	const r = ResolveLocale(requested, {}, [], null);
	const style = GetOption(options, 'style', 'string', ['narrow', 'short', 'long'], 'long');
	const type = GetOption(options, 'type', 'string', ['language', 'region', 'script', 'currency', 'calendar', 'dateTimeField'], undefined);
	if (type === undefined) throw new TypeError('Required option \'type\' is missing');
	const fallback = GetOption(options, 'fallback', 'string', ['code', 'none'], 'code');
	const languageDisplay = GetOption(options, 'languageDisplay', 'string', ['dialect', 'standard'], 'dialect');
	const s = { __proto__: null, locale: r.locale, dataLocale: dataKey(r.dataLocale), style, type, fallback };
	if (type === 'language') s.languageDisplay = languageDisplay;
	DN.set(o, s);
	return o;
}
function dnLookup(s, list, key, code, sparse) {
	const D = sec(s.dataLocale, 'dn');
	const i = DN_LISTS[list].indexOf(code);
	if (i < 0) return '';
	if (sparse && D[sparse] && D[sparse][i] !== undefined) return D[sparse][i];
	return (D[key] && D[key][i]) || '';
}
function langName(s, code) {
	/* a language tag: its name, or the language's name with the script / region in parentheses */
	const sparse = s.languageDisplay === 'standard' ? 'ls' : s.style !== 'long' ? 'lsh' : null;
	let v = dnLookup(s, 'langs', 'lg', code, sparse);
	if (v) return v;
	const r = parseTag(code);
	if (!r) return '';
	const base = dnLookup(s, 'langs', 'lg', r.lang, null);
	if (!base) return '';
	const extra = [];
	if (r.script) extra.push(dnLookup(s, 'scripts', 'sc', r.script[0].toUpperCase() + r.script.slice(1), null) || r.script);
	if (r.region) extra.push(dnLookup(s, 'regions', 'rg', r.region.toUpperCase(), s.style !== 'long' ? 'rgs' : null) || r.region.toUpperCase());
	if (!extra.length) return base;
	const D = sec(s.dataLocale, 'dn');
	/* the locale's pattern ("xx (YY)") */
	const pat = D.pat || 'xx (YY)';
	const m = /^xx(.*)YY(.*)$/.exec(pat);
	const sepm = D.pat2 && /Latin(.*)YY/.exec(D.pat2);
	const join = sepm ? sepm[1] : ', ';
	return m ? base + m[1] + extra.join(join) + m[2] : base + ' (' + extra.join(', ') + ')';
}
ctor(DisplayNames, 2, {
	of(code) {
		const s = DN.get(this, 'Intl.DisplayNames.prototype.of');
		code = ToString(code);
		let canon, v = '';
		switch (s.type) {
		case 'language': {
			const r = parseTag(code);
			if (!r || r.u || r.t || r.other.length || r.x || code.includes('_')) throw new RangeError('invalid language tag ' + code);
			canon = CanonicalizeTag(code);
			v = langName(s, canon);
			break;
		}
		case 'region':
			if (!/^([a-z]{2}|[0-9]{3})$/i.test(code)) throw new RangeError('invalid region ' + code);
			canon = code.toUpperCase();
			v = dnLookup(s, 'regions', 'rg', canon, s.style !== 'long' ? 'rgs' : null);
			break;
		case 'script':
			if (!/^[a-z]{4}$/i.test(code)) throw new RangeError('invalid script ' + code);
			canon = code[0].toUpperCase() + code.slice(1).toLowerCase();
			v = dnLookup(s, 'scripts', 'sc', canon, null);
			break;
		case 'currency':
			if (!/^[a-z]{3}$/i.test(code)) throw new RangeError('invalid currency ' + code);
			canon = code.toUpperCase();
			v = dnLookup(s, 'clist', 'cu', canon, null);
			break;
		case 'calendar':
			if (!TYPE_RE.test(code)) throw new RangeError('invalid calendar ' + code);
			canon = code.toLowerCase();
			if (canon === 'gregorian') canon = 'gregory';
			v = dnLookup(s, 'cals', 'ca', canon, null);
			break;
		default:
			if (!DN_LISTS.fields.includes(code)) throw new RangeError('invalid dateTimeField ' + code);
			canon = code;
			v = dnLookup(s, 'fields', 'fl', code, s.style === 'short' ? 'fs' : s.style === 'narrow' ? 'fn' : null);
		}
		if (v) return v;
		return s.fallback === 'code' ? canon : undefined;
	},
	resolvedOptions() {
		const s = DN.get(this, 'Intl.DisplayNames.prototype.resolvedOptions');
		const o = { locale: s.locale, style: s.style, type: s.type, fallback: s.fallback };
		if (s.languageDisplay !== undefined) o.languageDisplay = s.languageDisplay;
		return o;
	},
}, 'DisplayNames');
def(DisplayNames, { supportedLocalesOf(locales) { return supportedLocalesOf(locales, arguments[1]); } });

/* ==== Intl.Locale ==================================================================================== */

const LOCALE = slots();
function Locale(tagArg, options) {
	if (new.target === undefined) throw new TypeError('Constructor Intl.Locale requires \'new\'');
	const o = OrdinaryCreateFromConstructor(new.target, Locale.prototype);
	if (typeof tagArg !== 'string' && !isObj(tagArg)) throw new TypeError('First argument to Intl.Locale constructor can\'t be empty or missing');
	let t = LOCALE.has(tagArg) ? LOCALE.get(tagArg).locale : ToString(tagArg);
	options = CoerceOptionsToObject(options);
	/* ApplyOptionsToTag */
	const r0 = parseTag(t);
	if (!r0 || GRAPHEMELESS(t)) throw new RangeError('Incorrect locale information provided');
	const language = GetOption(options, 'language', 'string', undefined, undefined);
	if (language !== undefined && !(isLang(language) && language.length !== 4)) throw new RangeError('Invalid language');
	const script = GetOption(options, 'script', 'string', undefined, undefined);
	if (script !== undefined && !isScript(script)) throw new RangeError('Invalid script');
	const region = GetOption(options, 'region', 'string', undefined, undefined);
	if (region !== undefined && !isRegion(region)) throw new RangeError('Invalid region');
	const variants = GetOption(options, 'variants', 'string', undefined, undefined);
	let vlist;
	if (variants !== undefined) {
		vlist = variants.toLowerCase().split('-');
		if (!variants || vlist.some((v) => !isVariant(v)) || new Set(vlist).size !== vlist.length) throw new RangeError('Invalid variants');
	}
	t = CanonicalizeTag(t);
	const r = parseTag(t);
	if (language !== undefined) r.lang = language.toLowerCase();
	if (script !== undefined) r.script = script.toLowerCase();
	if (region !== undefined) r.region = region.toLowerCase();
	if (vlist !== undefined) r.variants = vlist;
	t = canonicalize(r);
	/* the relevant keywords */
	const kw = {};
	const ca = GetOption(options, 'calendar', 'string', undefined, undefined);
	if (ca !== undefined && !TYPE_RE.test(ca)) throw new RangeError('Invalid calendar');
	kw.ca = ca;
	const co = GetOption(options, 'collation', 'string', undefined, undefined);
	if (co !== undefined && !TYPE_RE.test(co)) throw new RangeError('Invalid collation');
	kw.co = co;
	const fw = GetOption(options, 'firstDayOfWeek', 'string', undefined, undefined);
	let fwv;
	if (fw !== undefined) {
		const W = { 0: 'sun', 1: 'mon', 2: 'tue', 3: 'wed', 4: 'thu', 5: 'fri', 6: 'sat', 7: 'sun' };
		fwv = W[fw] || fw.toLowerCase();
		if (!TYPE_RE.test(fwv)) throw new RangeError('Invalid firstDayOfWeek');
	}
	kw.fw = fwv;
	kw.hc = GetOption(options, 'hourCycle', 'string', ['h11', 'h12', 'h23', 'h24'], undefined);
	kw.kf = GetOption(options, 'caseFirst', 'string', ['upper', 'lower', 'false'], undefined);
	const kn = GetOption(options, 'numeric', 'boolean', undefined, undefined);
	kw.kn = kn === undefined ? undefined : String(kn);
	const nu = GetOption(options, 'numberingSystem', 'string', undefined, undefined);
	if (nu !== undefined && !TYPE_RE.test(nu)) throw new RangeError('Invalid numberingSystem');
	kw.nu = nu;
	/* insert them into the -u- extension */
	const p = parseTag(t);
	const u = p.u || { attrs: [], kw: [] };
	for (const k of ['ca', 'co', 'fw', 'hc', 'kf', 'kn', 'nu']) {
		if (kw[k] === undefined) continue;
		const e = u.kw.find((x) => x[0] === k);
		if (e) e[1] = kw[k]; else u.kw.push([k, kw[k]]);
	}
	if (u.attrs.length || u.kw.length) p.u = u;
	t = canonicalize(p);
	LOCALE.set(o, { __proto__: null, locale: t, r: parseTag(t) });
	return o;
}
function GRAPHEMELESS() { return false; }
function localeKw(o, k) {
	const s = LOCALE.get(o, 'Intl.Locale.prototype getter');
	if (!s.r.u) return undefined;
	const e = s.r.u.kw.find((x) => x[0] === k);
	if (!e) return undefined;
	return e[1] === '' ? 'true' : e[1];
}
function maximizeR(r) {
	const L = META.likely;
	const lang = r.lang === 'und' ? '' : r.lang;
	let script = r.script, region = r.region;
	const fill = (v) => {
		if (!v) return false;
		const p = v.split('-');
		if (p.length === 2 && p[0].length === 4) { if (!script) script = p[0].toLowerCase(); if (!region) region = p[1].toLowerCase(); }
		else if (p.length === 2) { if (!region) region = p[0].length === 2 || /^\d/.test(p[0]) ? p[0].toLowerCase() : region; }
		return true;
	};
	let outLang = lang;
	if (lang) {
		if (lang === 'zh' && (region === 'tw' || region === 'hk' || region === 'mo') && !script) script = 'hant';
		if (lang === 'zh' && script === 'hant' && !region) region = 'tw';
		fill(L[lang]);
	} else {
		const byRegion = region && L['und-' + region.toUpperCase()];
		const byScript = script && L['und-' + script[0].toUpperCase() + script.slice(1)];
		if (byRegion) { const p = byRegion.split('-'); outLang = p[0]; if (!script) script = p[1].toLowerCase(); }
		else if (byScript) { const p = byScript.split('-'); outLang = p[0]; if (!region) region = p[1].toLowerCase(); }
		else { outLang = 'en'; if (!script) script = 'latn'; if (!region) region = 'us'; }
		if (outLang && (!script || !region)) fill(L[outLang]);
	}
	return Object.assign({}, r, { lang: outLang || r.lang, script, region });
}
const LOCALE_PROTO = {
	maximize() {
		const s = LOCALE.get(this, 'Intl.Locale.prototype.maximize');
		const m = maximizeR(s.r);
		return new Locale(canonicalize(m));
	},
	minimize() {
		const s = LOCALE.get(this, 'Intl.Locale.prototype.minimize');
		const max = maximizeR(s.r);
		for (const cand of [{ script: '', region: '' }, { script: '', region: max.region }, { script: max.script, region: '' }]) {
			const t = Object.assign({}, max, cand);
			const mt = maximizeR(t);
			if (mt.lang === max.lang && mt.script === max.script && mt.region === max.region) return new Locale(canonicalize(t));
		}
		return new Locale(canonicalize(max));
	},
	toString() { return LOCALE.get(this, 'Intl.Locale.prototype.toString').locale; },
	toJSON() { return LOCALE.get(this, 'Intl.Locale.prototype.toJSON').locale; },
	get baseName() { const s = LOCALE.get(this, 'Intl.Locale.prototype.baseName'); return langIdString(s.r); },
	get calendar() { return localeKw(this, 'ca'); },
	get caseFirst() { return localeKw(this, 'kf'); },
	get collation() { return localeKw(this, 'co'); },
	get firstDayOfWeek() { return localeKw(this, 'fw'); },
	get hourCycle() { return localeKw(this, 'hc'); },
	get numeric() { return localeKw(this, 'kn') === 'true'; },
	get numberingSystem() { return localeKw(this, 'nu'); },
	get language() { return LOCALE.get(this, 'Intl.Locale.prototype.language').r.lang; },
	get script() { const r = LOCALE.get(this, 'Intl.Locale.prototype.script').r; return r.script ? r.script[0].toUpperCase() + r.script.slice(1) : undefined; },
	get region() { const r = LOCALE.get(this, 'Intl.Locale.prototype.region').r; return r.region ? r.region.toUpperCase() : undefined; },
	get variants() { const r = LOCALE.get(this, 'Intl.Locale.prototype.variants').r; return r.variants.length ? r.variants.join('-') : undefined; },
	getCalendars() { const c = localeKw(this, 'ca'); return [c || 'gregory']; },
	getCollations() { const c = localeKw(this, 'co'); return [c || 'emoji', 'eor'].filter((x, i, a) => a.indexOf(x) === i).sort(); },
	getHourCycles() {
		const c = localeKw(this, 'hc');
		if (c) return [c];
		const s = LOCALE.get(this);
		return [sec(dataKey(available(langIdString(s.r)) ? langIdString(s.r) : s.r.lang), 'd').hc];
	},
	getNumberingSystems() { const c = localeKw(this, 'nu'); return [c || 'latn']; },
	getTimeZones() {
		const r = LOCALE.get(this, 'Intl.Locale.prototype.getTimeZones').r;
		if (!r.region) return undefined;
		const z = REGION_ZONES[r.region.toUpperCase()];
		return z ? z.split(' ').sort() : [];
	},
	getTextInfo() {
		const r = LOCALE.get(this, 'Intl.Locale.prototype.getTextInfo').r;
		return { direction: ['ar', 'he', 'fa', 'ur', 'yi', 'ps', 'sd', 'ug', 'dv', 'ckb'].includes(r.lang) || r.script === 'arab' || r.script === 'hebr' ? 'rtl' : 'ltr' };
	},
	getWeekInfo() {
		const s = LOCALE.get(this, 'Intl.Locale.prototype.getWeekInfo');
		const dk = dataKey(langIdString(Object.assign({}, s.r, { variants: [] })));
		const wk = sec(dk, 'd').wk || [1, 4, [6, 7]];
		const fw = localeKw(this, 'fw');
		const FW = { mon: 1, tue: 2, wed: 3, thu: 4, fri: 5, sat: 6, sun: 7 };
		return { firstDay: fw && FW[fw] ? FW[fw] : wk[0], weekend: wk[2], minimalDays: wk[1] };
	},
};
ctor(Locale, 1, LOCALE_PROTO, 'Locale');

/* ==== the namespace's functions ====================================================================== */

const IntlFns = {
	getCanonicalLocales(locales) { return CanonicalizeLocaleList(locales); },
	supportedValuesOf(key) {
		key = ToString(key);
		let v;
		switch (key) {
		case 'calendar': v = ['gregory']; break;
		case 'collation': v = ['default'].filter(() => false).concat(['emoji', 'eor']); break;
		case 'currency': v = META.cur.split(' '); break;
		case 'numberingSystem': v = NU_LIST.slice(); break;
		case 'timeZone': {
			const T = tzData();
			v = Object.keys(T.zones).filter((z) => !z.startsWith('Etc/') || /^Etc\/GMT[+-]\d+$/.test(z) || z === 'Etc/UTC');
			if (!v.includes('UTC')) v.push('UTC');
			for (let h = 1; h <= 14; h++) { if (h <= 12) v.push('Etc/GMT+' + h); v.push('Etc/GMT-' + h); }
			v = [...new Set(v)];
			v = v.filter((z) => z !== 'Etc/UTC');
			break;
		}
		case 'unit': v = META.units.split(' '); break;
		default: throw new RangeError('Invalid key : ' + key);
		}
		return v.sort();
	},
};

/* ==== the built-ins ================================================================================== */

let NF_DEFAULT = null;
const DT_DEFAULT = {};
function thisNumber(v) {
	if (typeof v === 'number') return v;
	if (isObj(v)) { try { return Number.prototype.valueOf.call(v); } catch (e) { /* below */ } }
	throw new TypeError('Number.prototype.toLocaleString requires that \'this\' be a Number');
}
function numberLocale(v, locales, options) {
	const x = thisNumber(v);
	if (locales === undefined && options === undefined) {
		if (!NF_DEFAULT) NF_DEFAULT = NF.get(new NumberFormat());
		return FormatNumeric(NF_DEFAULT, decOfNumber(x));
	}
	return FormatNumeric(NF.get(new NumberFormat(locales, options)), decOfNumber(x));
}
function bigintLocale(v, locales, options) {
	let x = v;
	if (typeof x !== 'bigint') x = BigInt.prototype.valueOf.call(v);
	return FormatNumeric(NF.get(new NumberFormat(locales, options)), decOfBigInt(x));
}
function dateLocale(v, locales, options, required, defaults) {
	const t = Date.prototype.valueOf.call(v);
	if (Number.isNaN(t)) return 'Invalid Date';
	let s;
	if (locales === undefined && options === undefined) {
		s = DT_DEFAULT[required];
		if (!s) s = DT_DEFAULT[required] = DTF.get(CreateDateTimeFormat(DateTimeFormat, undefined, undefined, required, defaults));
	} else s = DTF.get(CreateDateTimeFormat(DateTimeFormat, locales, options, required, defaults));
	return joinParts(partitionDate(s, t));
}
let CO_DEFAULT = null;
function localeCompare(v, that, locales, options) {
	if (v === undefined || v === null) throw new TypeError('String.prototype.localeCompare called on null or undefined');
	const S = ToString(v), T = ToString(that);
	if (locales === undefined && options === undefined) {
		if (!CO_DEFAULT) CO_DEFAULT = CO.get(new Collator());
		return CompareStrings(CO_DEFAULT, S, T);
	}
	return CompareStrings(CO.get(new Collator(locales, options)), S, T);
}
function localeCase(v, locales, upper) {
	if (v === undefined || v === null) throw new TypeError('String.prototype.toLocale' + (upper ? 'Upper' : 'Lower') + 'Case called on null or undefined');
	const S = ToString(v);
	const req = CanonicalizeLocaleList(locales);
	const loc = req.length ? req[0] : DefaultLocale();
	const lang = (parseTag(loc) || { lang: 'en' }).lang;
	if (lang === 'tr' || lang === 'az') {
		if (upper) return S.replace(/i/g, '\u0130').toUpperCase();
		return S.replace(/\u0130/g, 'i').replace(/I\u0307/g, 'i').replace(/I/g, '\u0131').toLowerCase();
	}
	if (lang === 'lt' && !upper) return S.replace(/([IJ\u012e])(?=[\u0300-\u036f])/g, (c) => c.toLowerCase() + '\u0307').toLowerCase();
	return upper ? S.toUpperCase() : S.toLowerCase();
}
function arrayLocale(v, locales, options, typed) {
	let O, len;
	if (typed) {
		O = v;
		/* ValidateTypedArray */
		Object.getOwnPropertyDescriptor(Object.getPrototypeOf(Int8Array.prototype), 'length').get.call(O);
		len = O.length;
	} else {
		O = ToObject(v);
		len = ToLength(O.length);
	}
	let r = '';
	for (let k = 0; k < len; k++) {
		if (k > 0) r += ',';
		const e = O[k];
		if (e !== undefined && e !== null) {
			const f = e.toLocaleString;
			if (typeof f !== 'function') throw new TypeError('toLocaleString is not a function');
			r += ToString(f.call(e, locales, options));
		}
	}
	return r;
}

return {
	Collator, DateTimeFormat, DisplayNames, ListFormat, Locale, NumberFormat, PluralRules, RelativeTimeFormat, Segmenter,
	getCanonicalLocales: IntlFns.getCanonicalLocales, supportedValuesOf: IntlFns.supportedValuesOf,
	numberLocale, bigintLocale, dateLocale, localeCompare, localeCase, arrayLocale,
};
})
