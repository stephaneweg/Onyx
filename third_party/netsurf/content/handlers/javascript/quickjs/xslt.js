/*
 * Onyx: XPath 1.0 and XSLT 1.0 for Jet Browser (docs/06 section 43).
 *
 * Loaded on demand (qjs_xml.c's N.loadXslt: the first document.evaluate, XSLTProcessor or
 * <?xml-stylesheet type="text/xsl"?>), a function of the natives and the global object that
 * returns the library html5.js wraps.
 *
 * The data model: a tree of XNodes made in one call from the DOM (N.xmlFlat: the subtree in
 * document order) -- a node's type, names, value, parent, children, attributes and its
 * document order (a number: every tree takes a range of its own, so the trees of a
 * transform -- the source, the style sheet, the result tree fragments -- sort together); the
 * namespace declarations (xmlns attributes) are kept apart, not attributes. document.evaluate
 * maps its results back to the DOM's nodes (an attribute: its Attr).
 *
 * XPath: a tokenizer (the 1.0 disambiguation rules), a recursive-descent parser to a small
 * tree, an evaluator over the XNodes -- the 13 axes but namespace (empty), positional and
 * boolean predicates, node-sets kept unique in document order ("//x" a descendant step), the
 * core functions, numbers printed as XPath prints them. XSLT: the templates and their match
 * patterns (priorities, modes, import precedence), the built-in rules, apply-templates /
 * call-template / apply-imports with parameters, for-each, if, choose, sort (text, number,
 * order, case-order), variables and parameters (result tree fragments, usable as node-sets
 * as in EXSLT's node-set()), value-of, copy, copy-of, element, attribute, attribute-set,
 * text (disable-output-escaping), comment, processing-instruction, number, message, key(),
 * format-number() / decimal-format, generate-id(), current(), document('') (the style sheet;
 * another URI: empty), strip-space / preserve-space, output (methods xml, html and text,
 * doctype, indent ignored), include / import (by a resolver: the top-level transform fetches
 * them first), literal result elements with attribute value templates and their
 * namespaces (exclude-result-prefixes), a literal result element as the style sheet.
 */
(function (N, G) {
'use strict';

const NS_XSL = 'http://www.w3.org/1999/XSL/Transform';
const NS_XMLNS = 'http://www.w3.org/2000/xmlns/';
const NS_XML = 'http://www.w3.org/XML/1998/namespace';
const NS_HTML = 'http://www.w3.org/1999/xhtml';
const NS_EXSL = 'http://exslt.org/common';
const NS_MSXSL = 'urn:schemas-microsoft-com:xslt';

const ELEMENT = 1, ATTRIBUTE = 2, TEXT = 3, PI = 7, COMMENT = 8, ROOT = 9;

function xerr(msg, name) {
	return G.DOMException ? new G.DOMException(msg, name || 'SyntaxError') : new Error(msg);
}

/* ---- the data model ------------------------------------------------------------------------ */

let ORD = 1;		/* the next document order (every tree a range of its own) */
const TREE_GAP = 1 << 20;

class XNode {
	constructor(t, p) {
		this.t = t;	/* type */
		this.p = p;	/* parent (an attribute's: its element) */
		this.c = null;	/* children */
		this.a = null;	/* attributes */
		this.nsd = null;	/* namespace declarations: [prefix ('' default), uri]... */
		this.ln = null;	/* local name (a PI's target) */
		this.ns = null;
		this.px = null;
		this.v = null;	/* value: text, comment, PI data, attribute value */
		this.d = null;	/* the DOM node (null: made by a transform) */
		this.o = ORD++;
		this.doe = false;	/* text: disable-output-escaping */
	}
	get qn() { return this.px ? this.px + ':' + this.ln : this.ln; }
}

function newTree() { ORD += TREE_GAP; }

/** the tree of a DOM node (its whole tree: the document, or a detached subtree's top) */
/* (the DOM nodes are not wrapped here -- a transform of a big document needs none: only the
 * node looked for, find, and later a result's: domOf) */
function snapshot(dom, find) {
	let top = dom;
	for (let p; (p = N.parent(top));) top = p;
	newTree();
	const f = N.xmlFlat(top, false, find || null);
	const odoc = N.type(top) === 9 ? top : N.ownerDoc(top);
	const htmlDoc = !!odoc && N.docKind(odoc) === 0;
	const recs = new Array(f.length / 7);
	let root = null, found = null;
	for (let i = 0, r = 0; i < f.length; i += 7, r++) {
		let t = f[i];
		const pi = f[i + 2];
		const parent = pi >= 0 ? recs[pi] : null;
		if (t === 10 || (pi >= 0 && parent === null)) { recs[r] = null; continue; }	/* (a doctype) */
		if (t === 4) t = TEXT;
		if (t === 11) t = ROOT;
		const n = new XNode(t, parent);
		n.d = f[i + 1]; n.ri = r; n.ln = f[i + 3]; n.ns = f[i + 4]; n.px = f[i + 5]; n.v = f[i + 6];
		if (htmlDoc && t === ELEMENT && (n.ns === null || n.ns === NS_HTML)) {
			/* (an HTML document's element: libdom names it in upper case) */
			n.ln = n.ln.toLowerCase();
			n.ns = NS_HTML;
		}
		recs[r] = n;
		if (r === 0) root = n;
		if (t === ATTRIBUTE) {
			if (n.ns === NS_XMLNS) {
				(parent.nsd || (parent.nsd = [])).push(n.px ? n.ln : '', n.v);
				recs[r] = null;
				continue;
			}
			(parent.a || (parent.a = [])).push(n);
		} else if (parent) {
			(parent.c || (parent.c = [])).push(n);
		}
		if (n.d) found = n;
	}
	if (root && root.t !== ROOT) {
		/* (a detached subtree: under a root of its own, as XPath sees it) */
		const r = new XNode(ROOT, null);
		r.o = root.o - 1;
		r.c = [root];
		root.p = r;
		root = r;
	}
	if (root) {
		root.html = htmlDoc;
		root.dom = top;
	}
	return { root, found };
}

function rootOf(n) { while (n.p) n = n.p; return n; }

function stringValue(n) {
	switch (n.t) {
	case ELEMENT: case ROOT: {
		let s = '';
		const walk = x => {
			if (!x.c) return;
			for (const k of x.c) {
				if (k.t === TEXT) s += k.v;
				else if (k.t === ELEMENT) walk(k);
			}
		};
		walk(n);
		return s;
	}
	default: return n.v == null ? '' : n.v;
	}
}

/* ---- XPath: tokens ------------------------------------------------------------------------- */

const AXES = new Set(['ancestor', 'ancestor-or-self', 'attribute', 'child', 'descendant',
	'descendant-or-self', 'following', 'following-sibling', 'namespace', 'parent',
	'preceding', 'preceding-sibling', 'self']);
const NODE_TYPES = new Set(['comment', 'text', 'processing-instruction', 'node']);
const NAME_START = /[A-Za-z_À-ÖØ-öø-˿Ͱ-ͽͿ-῿‌-‍⁰-↏Ⰰ-⿯、-퟿豈-﷏ﷰ-�]/;
const NAME_CHAR = /[-.0-9·̀-ͯ‿-⁀A-Za-z_À-ÖØ-öø-˿Ͱ-ͽͿ-῿‌-‍⁰-↏Ⰰ-⿯、-퟿豈-﷏ﷰ-�]/;

function tokenize(s) {
	const toks = [];
	let i = 0;
	const n = s.length;
	const ncname = () => {
		const b = i;
		if (i < n && NAME_START.test(s[i])) {
			i++;
			while (i < n && NAME_CHAR.test(s[i])) i++;
		}
		return s.slice(b, i);
	};
	while (i < n) {
		const c = s[i];
		if (c === ' ' || c === '\t' || c === '\n' || c === '\r') { i++; continue; }
		const prev = toks.length ? toks[toks.length - 1] : null;
		/* (3.7: an operator when a token comes before that is not @ :: ( [ , or an operator) */
		const opCtx = prev && !(prev.k === '@' || prev.k === '::' || prev.k === '(' || prev.k === '[' ||
			prev.k === ',' || prev.op);
		if (c === '(' || c === ')' || c === '[' || c === ']' || c === '@' || c === ',') {
			toks.push({ k: c }); i++; continue;
		}
		if (c === '.' ) {
			if (s[i + 1] === '.') { toks.push({ k: '..' }); i += 2; continue; }
			if (i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '9') {
				const m = /^\.[0-9]+/.exec(s.slice(i));
				toks.push({ k: 'num', v: parseFloat(m[0]) }); i += m[0].length; continue;
			}
			toks.push({ k: '.' }); i++; continue;
		}
		if (c === ':' && s[i + 1] === ':') { toks.push({ k: '::' }); i += 2; continue; }
		if (c === '"' || c === "'") {
			const e = s.indexOf(c, i + 1);
			if (e < 0) throw xerr('XPath: unterminated string literal');
			toks.push({ k: 'lit', v: s.slice(i + 1, e) }); i = e + 1; continue;
		}
		if (c >= '0' && c <= '9') {
			const m = /^[0-9]+(\.[0-9]*)?/.exec(s.slice(i));
			toks.push({ k: 'num', v: parseFloat(m[0]) }); i += m[0].length; continue;
		}
		if (c === '$') {
			i++;
			let name = ncname();
			if (s[i] === ':' && s[i + 1] !== ':') { i++; name += ':' + ncname(); }
			if (!name) throw xerr('XPath: bad variable reference');
			toks.push({ k: '$', v: name }); continue;
		}
		if (c === '*') {
			if (opCtx) toks.push({ k: '*', op: true });
			else toks.push({ k: 'name', v: '*' });
			i++; continue;
		}
		if (c === '/') {
			if (s[i + 1] === '/') { toks.push({ k: '//', op: true }); i += 2; }
			else { toks.push({ k: '/', op: true }); i++; }
			continue;
		}
		if (c === '|' || c === '+' || c === '-' || c === '=') { toks.push({ k: c, op: true }); i++; continue; }
		if (c === '!' && s[i + 1] === '=') { toks.push({ k: '!=', op: true }); i += 2; continue; }
		if (c === '<' || c === '>') {
			if (s[i + 1] === '=') { toks.push({ k: c + '=', op: true }); i += 2; }
			else { toks.push({ k: c, op: true }); i++; }
			continue;
		}
		if (NAME_START.test(c)) {
			let name = ncname();
			if (opCtx && (name === 'and' || name === 'or' || name === 'mod' || name === 'div')) {
				toks.push({ k: name, op: true }); continue;
			}
			/* a QName, prefix:* */
			if (s[i] === ':' && s[i + 1] !== ':') {
				if (s[i + 1] === '*') { name += ':*'; i += 2; }
				else {
					const save = i;
					i++;
					const l = ncname();
					if (l) name += ':' + l; else i = save;
				}
			}
			/* what follows: ( a function or node type, :: an axis */
			let j = i;
			while (j < n && /\s/.test(s[j])) j++;
			if (s[j] === '(' && !name.endsWith('*')) {
				toks.push({ k: NODE_TYPES.has(name) ? 'ntype' : 'fn', v: name });
			} else if (s[j] === ':' && s[j + 1] === ':') {
				if (!AXES.has(name)) throw xerr('XPath: unknown axis ' + name);
				toks.push({ k: 'axis', v: name });
			} else {
				toks.push({ k: 'name', v: name });
			}
			continue;
		}
		throw xerr('XPath: unexpected character "' + c + '"');
	}
	return toks;
}

/* ---- XPath: the parser --------------------------------------------------------------------- */

class Parser {
	constructor(src, resolve) {
		this.t = tokenize(src);
		this.i = 0;
		this.src = src;
		this.resolve = resolve;	/* prefix -> namespace URI (or null) */
	}
	peek(k) { const t = this.t[this.i]; return t && (k === undefined || t.k === k) ? t : null; }
	next() { return this.t[this.i++]; }
	expect(k) {
		const t = this.next();
		if (!t || t.k !== k) throw xerr('XPath: expected "' + k + '" in ' + this.src);
		return t;
	}
	ns(prefix) {
		if (prefix === 'xml') return NS_XML;
		const u = this.resolve ? this.resolve(prefix) : null;
		if (u == null || u === '') {
			const e = xerr('XPath: unresolved namespace prefix ' + prefix, 'NamespaceError');
			throw e;
		}
		return u;
	}
	qname(q) {
		const i = q.indexOf(':');
		return i < 0 ? { ns: null, local: q } : { ns: this.ns(q.slice(0, i)), local: q.slice(i + 1) };
	}
	parse() {
		const e = this.expr();
		if (this.i < this.t.length) throw xerr('XPath: unexpected "' + (this.t[this.i].v || this.t[this.i].k) + '" in ' + this.src);
		return e;
	}
	binary(sub, ops) {
		let a = sub();
		for (;;) {
			const t = this.peek();
			if (!t || !ops.includes(t.k)) return a;
			this.next();
			a = { k: t.k, a, b: sub() };
		}
	}
	expr() { return this.binary(() => this.andExpr(), ['or']); }
	andExpr() { return this.binary(() => this.eqExpr(), ['and']); }
	eqExpr() { return this.binary(() => this.relExpr(), ['=', '!=']); }
	relExpr() { return this.binary(() => this.addExpr(), ['<', '>', '<=', '>=']); }
	addExpr() { return this.binary(() => this.mulExpr(), ['+', '-']); }
	mulExpr() { return this.binary(() => this.unary(), ['*', 'div', 'mod']); }
	unary() {
		if (this.peek('-')) { this.next(); return { k: 'neg', a: this.unary() }; }
		return this.union();
	}
	union() {
		const a = this.pathExpr();
		if (!this.peek('|')) return a;
		const list = [a];
		while (this.peek('|')) { this.next(); list.push(this.pathExpr()); }
		return { k: 'union', list };
	}
	pathExpr() {
		const t = this.peek();
		if (!t) throw xerr('XPath: expression expected in ' + this.src);
		if (t.k === '$' || t.k === '(' || t.k === 'lit' || t.k === 'num' || t.k === 'fn') {
			let e = this.primary();
			const preds = this.predicates();
			if (preds.length) e = { k: 'filter', e, preds };
			if (this.peek('/') || this.peek('//')) {
				const steps = [];
				this.relPath(steps, true);
				return { k: 'path', e, abs: false, steps };
			}
			return e;
		}
		return this.locationPath();
	}
	primary() {
		const t = this.next();
		switch (t.k) {
		case '$': return { k: 'var', name: t.v, q: this.qname(t.v) };
		case '(': { const e = this.expr(); this.expect(')'); return e; }
		case 'lit': return { k: 'lit', v: t.v };
		case 'num': return { k: 'num', v: t.v };
		case 'fn': {
			this.expect('(');
			const args = [];
			if (!this.peek(')')) {
				args.push(this.expr());
				while (this.peek(',')) { this.next(); args.push(this.expr()); }
			}
			this.expect(')');
			const q = t.v.indexOf(':') > 0 ? this.qname(t.v) : { ns: null, local: t.v };
			return { k: 'fn', name: t.v, q, args };
		}
		}
		throw xerr('XPath: unexpected token in ' + this.src);
	}
	predicates() {
		const preds = [];
		while (this.peek('[')) {
			this.next();
			preds.push(this.expr());
			this.expect(']');
		}
		return preds;
	}
	locationPath() {
		const steps = [];
		if (this.peek('/')) {
			this.next();
			const t = this.peek();
			if (t && (t.k === 'name' || t.k === 'axis' || t.k === '@' || t.k === '.' || t.k === '..' || t.k === 'ntype'))
				this.relPath(steps, false);
			return { k: 'path', e: null, abs: true, steps };
		}
		if (this.peek('//')) {
			this.relPath(steps, true);
			return { k: 'path', e: null, abs: true, steps };
		}
		this.relPath(steps, false);
		return { k: 'path', e: null, abs: false, steps };
	}
	/* steps (separators first when sep: the path after a filter or after "//") */
	relPath(steps, sep) {
		let first = true;
		for (;;) {
			let desc = false;
			if (!first || sep) {
				if (this.peek('//')) { this.next(); desc = true; }
				else if (this.peek('/')) { this.next(); }
				else if (!first) return;
			}
			first = false;
			const st = this.step();
			if (desc) {
				/* "//x" (no positional predicate): descendant::x; else
				 * descendant-or-self::node()/x */
				if (st.axis === 'child' && st.preds.every(notPositional)) st.axis = 'descendant';
				else if (st.axis === 'attribute' && !st.preds.length) {
					steps.push({ axis: 'descendant-or-self', test: { type: 'node' }, preds: [] });
				} else steps.push({ axis: 'descendant-or-self', test: { type: 'node' }, preds: [] });
			}
			steps.push(st);
			if (!this.peek('/') && !this.peek('//')) return;
		}
	}
	step() {
		const t = this.peek();
		if (t && t.k === '.') { this.next(); return { axis: 'self', test: { type: 'node' }, preds: [] }; }
		if (t && t.k === '..') { this.next(); return { axis: 'parent', test: { type: 'node' }, preds: [] }; }
		let axis = 'child';
		if (t && t.k === '@') { this.next(); axis = 'attribute'; }
		else if (t && t.k === 'axis') { this.next(); this.expect('::'); axis = t.v; }
		const nt = this.next();
		let test;
		if (!nt) throw xerr('XPath: a step expected in ' + this.src);
		if (nt.k === 'name') {
			const v = nt.v;
			if (v === '*') test = { type: 'name', ns: undefined, local: '*' };
			else if (v.endsWith(':*')) test = { type: 'name', ns: this.ns(v.slice(0, -2)), local: '*' };
			else {
				const q = this.qname(v);
				test = { type: 'name', ns: q.ns, local: q.local, prefixed: v.indexOf(':') > 0 };
			}
		} else if (nt.k === 'ntype') {
			this.expect('(');
			let target = null;
			if (nt.v === 'processing-instruction' && this.peek('lit')) target = this.next().v;
			this.expect(')');
			test = { type: nt.v === 'processing-instruction' ? 'pi' : nt.v, target };
		} else {
			throw xerr('XPath: a node test expected in ' + this.src);
		}
		return { axis, test, preds: this.predicates() };
	}
}

/** a predicate whose value is no number and that asks no position: the same for a node
 *  whichever list it is in ("//x[@a='1']" is "descendant::x[@a='1']") */
const BOOL_FNS = new Set(['not', 'boolean', 'true', 'false', 'contains', 'starts-with', 'lang']);
function usesPosition(e) {
	if (!e || typeof e !== 'object') return false;
	if (e.k === 'fn' && !e.q.ns && (e.name === 'position' || e.name === 'last')) return true;
	if (e.k === 'path' || e.k === 'filter') return usesPosition(e.e);	/* (a path's own steps: their context) */
	for (const k of ['a', 'b']) if (e[k] && usesPosition(e[k])) return true;
	if (e.args) for (const a of e.args) if (usesPosition(a)) return true;
	if (e.list) for (const a of e.list) if (usesPosition(a)) return true;
	return false;
}
function notPositional(e) {
	const boolish = ['=', '!=', '<', '>', '<=', '>=', 'and', 'or', 'path', 'union', 'lit'].includes(e.k) ||
		(e.k === 'fn' && !e.q.ns && BOOL_FNS.has(e.name));
	return boolish && !usesPosition(e);
}

function compile(src, resolve) { return new Parser(String(src), resolve).parse(); }

/* ---- XPath: values ------------------------------------------------------------------------- */

/* (XSLT prints numbers as libxslt does -- Chrome's XSLT: 15 significant digits, 0.1 + 0.2 is
 * "0.3"; document.evaluate as Blink's XPath: the shortest that reads back) */
let num15 = false;
function numToStr(x) {
	if (Number.isNaN(x)) return 'NaN';
	if (x === Infinity) return 'Infinity';
	if (x === -Infinity) return '-Infinity';
	if (x === 0) return '0';
	if (Number.isInteger(x) && Math.abs(x) < 1e21) return String(x);
	if (num15) {
		const r = parseFloat(x.toPrecision(15));
		if (Number.isInteger(r) && Math.abs(r) < 1e21) return String(r);
		x = r;
	}
	let s = String(x);
	if (s.indexOf('e') < 0) return s;
	/* (no exponent in XPath) */
	const neg = x < 0;
	const m = /^(\d)(?:\.(\d+))?e([+-]\d+)$/.exec(String(Math.abs(x)));
	if (!m) return s;
	const digits = m[1] + (m[2] || ''), e = parseInt(m[3], 10);
	if (e < 0) s = '0.' + '0'.repeat(-e - 1) + digits;
	else s = digits.length > e + 1 ? digits.slice(0, e + 1) + '.' + digits.slice(e + 1) : digits + '0'.repeat(e + 1 - digits.length);
	return (neg ? '-' : '') + s;
}
function strToNum(s) {
	s = String(s).replace(/^[\s]+|[\s]+$/g, '');
	return /^-?(\d+(\.\d*)?|\.\d+)$/.test(s) ? parseFloat(s) : NaN;
}
function isSet(v) { return Array.isArray(v); }
function toStr(v) {
	if (typeof v === 'string') return v;
	if (typeof v === 'number') return numToStr(v);
	if (typeof v === 'boolean') return v ? 'true' : 'false';
	if (isSet(v)) return v.length ? stringValue(v[0]) : '';
	return String(v);
}
function toNum(v) {
	if (typeof v === 'number') return v;
	if (typeof v === 'boolean') return v ? 1 : 0;
	return strToNum(toStr(v));
}
function toBool(v) {
	if (typeof v === 'boolean') return v;
	if (typeof v === 'number') return v !== 0 && !Number.isNaN(v);
	if (typeof v === 'string') return v.length > 0;
	if (isSet(v)) return v.length > 0;
	return !!v;
}

/** unique, in document order */
function docOrder(list) {
	if (list.length < 2) return list;
	const seen = new Set();
	const out = [];
	for (const n of list) if (!seen.has(n)) { seen.add(n); out.push(n); }
	out.sort((a, b) => a.o - b.o);
	return out;
}

/* ---- XPath: axes and tests ----------------------------------------------------------------- */

function descendants(n, out, self) {
	if (self) out.push(n);
	const stack = [];
	if (n.c) for (let i = n.c.length - 1; i >= 0; i--) stack.push(n.c[i]);
	while (stack.length) {
		const x = stack.pop();
		out.push(x);
		if (x.c) for (let i = x.c.length - 1; i >= 0; i--) stack.push(x.c[i]);
	}
	return out;
}

/** the axis' nodes in its order (reverse axes: nearest first) */
function axisNodes(axis, n) {
	switch (axis) {
	case 'child': return n.c || [];
	case 'attribute': return n.a || [];
	case 'self': return [n];
	case 'parent': return n.p ? [n.p] : [];
	case 'descendant': return n.t === ATTRIBUTE ? [] : descendants(n, [], false);
	case 'descendant-or-self': return n.t === ATTRIBUTE ? [n] : descendants(n, [], true);
	case 'ancestor': { const r = []; for (let p = n.p; p; p = p.p) r.push(p); return r; }
	case 'ancestor-or-self': { const r = [n]; for (let p = n.p; p; p = p.p) r.push(p); return r; }
	case 'following-sibling': {
		if (n.t === ATTRIBUTE || !n.p || !n.p.c) return [];
		const s = n.p.c, i = s.indexOf(n);
		return s.slice(i + 1);
	}
	case 'preceding-sibling': {
		if (n.t === ATTRIBUTE || !n.p || !n.p.c) return [];
		const s = n.p.c, i = s.indexOf(n);
		return s.slice(0, i).reverse();
	}
	case 'following': {
		const r = [];
		let x = n.t === ATTRIBUTE ? n.p : n;
		if (n.t === ATTRIBUTE) descendants(x, r, false);
		for (; x && x.p; x = x.p) {
			const s = x.p.c, i = s ? s.indexOf(x) : -1;
			if (s) for (let k = i + 1; k < s.length; k++) descendants(s[k], r, true);
		}
		return r;
	}
	case 'preceding': {
		const r = [];
		let x = n.t === ATTRIBUTE ? n.p : n;
		for (; x && x.p; x = x.p) {
			const s = x.p.c, i = s ? s.indexOf(x) : -1;
			for (let k = i - 1; k >= 0; k--) {
				const d = descendants(s[k], [], true);
				for (let j = d.length - 1; j >= 0; j--) r.push(d[j]);
			}
		}
		return r;
	}
	case 'namespace': return [];
	}
	return [];
}
const REVERSE = new Set(['ancestor', 'ancestor-or-self', 'preceding', 'preceding-sibling']);

function testNode(test, n, axis) {
	switch (test.type) {
	case 'node': return true;
	case 'text': return n.t === TEXT;
	case 'comment': return n.t === COMMENT;
	case 'pi': return n.t === PI && (test.target === null || n.ln === test.target);
	case 'name': {
		const principal = axis === 'attribute' ? ATTRIBUTE : ELEMENT;
		if (n.t !== principal) return false;
		if (test.local === '*') return test.ns === undefined || n.ns === test.ns;
		if (test.ns === null && !test.prefixed) {
			/* (an HTML document's HTML elements: their names in any case) */
			if (n.t === ELEMENT && n.ns === NS_HTML && rootOf(n).html)
				return n.ln.toLowerCase() === test.local.toLowerCase();
			return n.ns == null && n.ln === test.local;
		}
		return n.ns === test.ns && n.ln === test.local;
	}
	}
	return false;
}

/* ---- XPath: evaluation --------------------------------------------------------------------- */

/*
 * ctx: { node, pos, size, vars (lookup(q) -> value), fns (extra functions by "{ns}local"),
 *        x (the XSLT state: current(), key(), the style sheet) }
 */
function evalExpr(e, ctx) {
	switch (e.k) {
	case 'or': return toBool(evalExpr(e.a, ctx)) || toBool(evalExpr(e.b, ctx));
	case 'and': return toBool(evalExpr(e.a, ctx)) && toBool(evalExpr(e.b, ctx));
	case '=': case '!=': case '<': case '>': case '<=': case '>=':
		return compare(e.k, evalExpr(e.a, ctx), evalExpr(e.b, ctx));
	case '+': return toNum(evalExpr(e.a, ctx)) + toNum(evalExpr(e.b, ctx));
	case '-': return toNum(evalExpr(e.a, ctx)) - toNum(evalExpr(e.b, ctx));
	case '*': return toNum(evalExpr(e.a, ctx)) * toNum(evalExpr(e.b, ctx));
	case 'div': return toNum(evalExpr(e.a, ctx)) / toNum(evalExpr(e.b, ctx));
	case 'mod': {
		const a = toNum(evalExpr(e.a, ctx)), b = toNum(evalExpr(e.b, ctx));
		return a % b;
	}
	case 'neg': return -toNum(evalExpr(e.a, ctx));
	case 'lit': return e.v;
	case 'num': return e.v;
	case 'var': {
		const v = ctx.vars ? ctx.vars(e.q, e.name) : undefined;
		if (v === undefined) throw xerr('XPath: undefined variable $' + e.name);
		return v;
	}
	case 'union': {
		let all = [];
		for (const x of e.list) {
			const v = evalExpr(x, ctx);
			if (!isSet(v)) throw new TypeError('XPath: | of a value that is not a node-set');
			all = all.concat(v);
		}
		return docOrder(all);
	}
	case 'filter': {
		let v = evalExpr(e.e, ctx);
		if (!isSet(v)) throw new TypeError('XPath: a predicate on a value that is not a node-set');
		for (const p of e.preds) v = applyPred(p, v, ctx);
		return v;
	}
	case 'path': {
		let set;
		if (e.e) {
			set = evalExpr(e.e, ctx);
			if (!isSet(set)) throw new TypeError('XPath: a path from a value that is not a node-set');
		} else if (e.abs) {
			set = [rootOf(ctx.node)];
		} else {
			set = [ctx.node];
		}
		for (const st of e.steps) set = applyStep(st, set, ctx);
		return set;
	}
	case 'fn': return callFn(e, ctx);
	}
	throw xerr('XPath: bad expression');
}

function applyStep(st, set, ctx) {
	let out = [];
	const many = set.length > 1;
	for (const n of set) {
		let nodes = axisNodes(st.axis, n);
		if (st.test.type !== 'node' || st.axis === 'attribute') {
			const f = [];
			for (const x of nodes) if (testNode(st.test, x, st.axis)) f.push(x);
			nodes = f;
		}
		for (const p of st.preds) nodes = applyPred(p, nodes, ctx);
		if (!many) {
			out = REVERSE.has(st.axis) ? nodes.slice().reverse() : nodes;
			return out;
		}
		for (const x of nodes) out.push(x);
	}
	return docOrder(out);
}

function applyPred(p, nodes, ctx) {
	const out = [];
	/* (a number literal: the position, no evaluation per node) */
	if (p.k === 'num') {
		const i = p.v;
		return Number.isInteger(i) && i >= 1 && i <= nodes.length ? [nodes[i - 1]] : [];
	}
	const size = nodes.length;
	const c = Object.assign({}, ctx);
	c.size = size;
	for (let i = 0; i < size; i++) {
		c.node = nodes[i];
		c.pos = i + 1;
		const v = evalExpr(p, c);
		if (typeof v === 'number' ? v === i + 1 : toBool(v)) out.push(nodes[i]);
	}
	return out;
}

function compare(op, a, b) {
	const cmp = (x, y) => {
		switch (op) {
		case '=': return typeof x === 'boolean' || typeof y === 'boolean' ? toBool(x) === toBool(y) :
			typeof x === 'number' || typeof y === 'number' ? toNum(x) === toNum(y) : toStr(x) === toStr(y);
		case '!=': return typeof x === 'boolean' || typeof y === 'boolean' ? toBool(x) !== toBool(y) :
			typeof x === 'number' || typeof y === 'number' ? toNum(x) !== toNum(y) : toStr(x) !== toStr(y);
		case '<': return toNum(x) < toNum(y);
		case '>': return toNum(x) > toNum(y);
		case '<=': return toNum(x) <= toNum(y);
		case '>=': return toNum(x) >= toNum(y);
		}
		return false;
	};
	if (isSet(a) && isSet(b)) {
		const bs = b.map(stringValue);
		for (const x of a) {
			const sx = stringValue(x);
			for (const sy of bs) if (cmp(sx, sy)) return true;
		}
		return false;
	}
	if (isSet(a) || isSet(b)) {
		const set = isSet(a) ? a : b, other = isSet(a) ? b : a, left = isSet(a);
		if (typeof other === 'boolean') return left ? cmp(toBool(set), other) : cmp(other, toBool(set));
		for (const x of set) {
			const sv = typeof other === 'number' ? strToNum(stringValue(x)) : stringValue(x);
			if (left ? cmp(sv, other) : cmp(other, sv)) return true;
		}
		return false;
	}
	return cmp(a, b);
}

function argSet(e, ctx, name) {
	const v = evalExpr(e, ctx);
	if (!isSet(v)) throw new TypeError('XPath: ' + name + '() needs a node-set');
	return v;
}

function localNameOf(n) { return n && (n.t === ELEMENT || n.t === ATTRIBUTE || n.t === PI) ? n.ln : ''; }

function langOf(n) {
	for (let x = n.t === ATTRIBUTE ? n.p : n; x; x = x.p)
		if (x.a) for (const a of x.a) if (a.ln === 'lang' && (a.ns === NS_XML || (a.px === 'xml'))) return a.v;
	return null;
}

const FNS = {
	last: (a, c) => c.size,
	position: (a, c) => c.pos,
	count: (a, c) => argSet(a[0], c, 'count').length,
	id: (a, c) => {
		const v = evalExpr(a[0], c);
		const ids = isSet(v) ? v.map(stringValue).join(' ') : toStr(v);
		const want = new Set(ids.split(/\s+/).filter(Boolean));
		const out = [];
		if (!want.size) return out;
		const root = rootOf(c.node);
		for (const n of descendants(root, [], false))
			if (n.t === ELEMENT && n.a)
				for (const at of n.a)
					if ((at.ln === 'id' && !at.ns) || (at.ln === 'id' && at.ns === NS_XML))
						if (want.has(at.v)) { out.push(n); break; }
		return out;
	},
	'local-name': (a, c) => { const s = a.length ? argSet(a[0], c, 'local-name') : [c.node]; return localNameOf(s[0]); },
	'namespace-uri': (a, c) => {
		const s = a.length ? argSet(a[0], c, 'namespace-uri') : [c.node];
		return s[0] && (s[0].t === ELEMENT || s[0].t === ATTRIBUTE) ? s[0].ns || '' : '';
	},
	name: (a, c) => {
		const s = a.length ? argSet(a[0], c, 'name') : [c.node];
		const n = s[0];
		if (!n) return '';
		return n.t === ELEMENT || n.t === ATTRIBUTE ? n.qn : n.t === PI ? n.ln : '';
	},
	string: (a, c) => a.length ? toStr(evalExpr(a[0], c)) : stringValue(c.node),
	concat: (a, c) => a.map(x => toStr(evalExpr(x, c))).join(''),
	'starts-with': (a, c) => toStr(evalExpr(a[0], c)).startsWith(toStr(evalExpr(a[1], c))),
	contains: (a, c) => toStr(evalExpr(a[0], c)).includes(toStr(evalExpr(a[1], c))),
	'substring-before': (a, c) => {
		const s = toStr(evalExpr(a[0], c)), t = toStr(evalExpr(a[1], c)), i = s.indexOf(t);
		return i < 0 ? '' : s.slice(0, i);
	},
	'substring-after': (a, c) => {
		const s = toStr(evalExpr(a[0], c)), t = toStr(evalExpr(a[1], c)), i = s.indexOf(t);
		return i < 0 ? '' : s.slice(i + t.length);
	},
	substring: (a, c) => {
		const s = Array.from(toStr(evalExpr(a[0], c)));
		const start = Math.round(toNum(evalExpr(a[1], c)));
		const len = a.length > 2 ? Math.round(toNum(evalExpr(a[2], c))) : Infinity;
		if (Number.isNaN(start) || Number.isNaN(len)) return '';
		const from = Math.max(start, 1), to = start + len;	/* [from, to) */
		if (!(to > from)) return '';
		return s.slice(from - 1, Number.isFinite(to) ? to - 1 : s.length).join('');
	},
	'string-length': (a, c) => Array.from(a.length ? toStr(evalExpr(a[0], c)) : stringValue(c.node)).length,
	'normalize-space': (a, c) => (a.length ? toStr(evalExpr(a[0], c)) : stringValue(c.node))
		.replace(/[ \t\n\r]+/g, ' ').replace(/^ | $/g, ''),
	translate: (a, c) => {
		const s = toStr(evalExpr(a[0], c)), from = Array.from(toStr(evalExpr(a[1], c))),
			to = Array.from(toStr(evalExpr(a[2], c)));
		const m = new Map();
		from.forEach((ch, i) => { if (!m.has(ch)) m.set(ch, i < to.length ? to[i] : ''); });
		let r = '';
		for (const ch of s) r += m.has(ch) ? m.get(ch) : ch;
		return r;
	},
	boolean: (a, c) => toBool(evalExpr(a[0], c)),
	not: (a, c) => !toBool(evalExpr(a[0], c)),
	true: () => true,
	false: () => false,
	lang: (a, c) => {
		const want = toStr(evalExpr(a[0], c)).toLowerCase(), l = langOf(c.node);
		if (l === null) return false;
		const h = l.toLowerCase();
		return h === want || h.startsWith(want + '-');
	},
	number: (a, c) => a.length ? toNum(evalExpr(a[0], c)) : strToNum(stringValue(c.node)),
	sum: (a, c) => argSet(a[0], c, 'sum').reduce((s, n) => s + strToNum(stringValue(n)), 0),
	floor: (a, c) => Math.floor(toNum(evalExpr(a[0], c))),
	ceiling: (a, c) => Math.ceil(toNum(evalExpr(a[0], c))),
	round: (a, c) => {
		const x = toNum(evalExpr(a[0], c));
		if (!Number.isFinite(x)) return x;
		if (x < 0 && x >= -0.5) return -0;
		return Math.floor(x + 0.5);
	},
};

function callFn(e, ctx) {
	if (e.q.ns) {
		const k = '{' + e.q.ns + '}' + e.q.local;
		const f = ctx.fns && ctx.fns[k];
		if (!f) throw xerr('XPath: unknown function ' + e.name);
		return f(e.args, ctx);
	}
	const f = FNS[e.name] || (ctx.fns && ctx.fns[e.name]);
	if (!f) throw xerr('XPath: unknown function ' + e.name + '()');
	return f(e.args, ctx);
}

/* ---- document.evaluate ----------------------------------------------------------------------- */

const ANY_TYPE = 0, NUMBER_TYPE = 1, STRING_TYPE = 2, BOOLEAN_TYPE = 3,
	UNORDERED_NODE_ITERATOR_TYPE = 4, ORDERED_NODE_ITERATOR_TYPE = 5,
	UNORDERED_NODE_SNAPSHOT_TYPE = 6, ORDERED_NODE_SNAPSHOT_TYPE = 7,
	ANY_UNORDERED_NODE_TYPE = 8, FIRST_ORDERED_NODE_TYPE = 9;

function resolverOf(r) {
	if (r == null) return null;
	if (typeof r === 'function') return p => r(p);
	if (typeof r.lookupNamespaceURI === 'function') return p => r.lookupNamespaceURI(p);
	return null;
}

/** the DOM nodes of these XNodes (one walk: N.xmlWrap) */
function domOf(list) {
	const byRoot = new Map();
	for (const n of list) {
		const x = n.t === ATTRIBUTE ? n.p : n;
		if (!x || x.d || x.ri === undefined) continue;
		const r = rootOf(x);
		if (!r.dom) continue;
		let l = byRoot.get(r);
		if (!l) byRoot.set(r, l = []);
		l.push(x);
	}
	for (const [r, l] of byRoot) {
		l.sort((a, b) => a.ri - b.ri);
		const idx = [];
		for (const x of l) if (!idx.length || idx[idx.length - 1] !== x.ri) idx.push(x.ri);
		const w = N.xmlWrap(r.dom, idx);
		const m = new Map();
		idx.forEach((k, i) => m.set(k, w[i]));
		for (const x of l) x.d = m.get(x.ri) || null;
	}
	return list.map(toDom);
}

/** an XNode back to the DOM (an attribute: its Attr) */
function toDom(n) {
	if (n.d) return n.d;
	if (n.t === ATTRIBUTE && n.p && n.p.d) {
		const el = n.p.d;
		if (n.ns && typeof el.getAttributeNodeNS === 'function') return el.getAttributeNodeNS(n.ns, n.ln);
		return el.getAttributeNode(n.qn);
	}
	return null;
}

/**
 * evaluate(src | compiled, contextNode, resolver, type) -> { type, value }: the result for
 * html5.js' XPathResult (value: a number, string, boolean or an array of DOM nodes)
 */
function evaluate(expr, context, resolver, type) {
	const ast = typeof expr === 'string' ? compile(expr, resolverOf(resolver)) : expr;
	if (context == null) throw new TypeError('evaluate: the context is not a node');
	let dom = context, attrName = null;
	if (!(G.Attr && context instanceof G.Attr) && !N.type(context))
		throw new TypeError('evaluate: the context is not a node');
	if (G.Attr && context instanceof G.Attr) {
		dom = context.ownerElement;
		attrName = context.name;
		if (!dom) throw new TypeError('evaluate: a detached attribute as the context');
	}
	const snap = snapshot(dom, dom);
	let node = snap.found;
	if (!node) throw new TypeError('evaluate: the context is not in a tree');
	if (attrName !== null && node.a) node = node.a.find(a => a.qn === attrName) || node;
	const v = evalExpr(ast, { node, pos: 1, size: 1, vars: null, fns: null });
	let t = type | 0;
	if (t === ANY_TYPE)
		t = typeof v === 'number' ? NUMBER_TYPE : typeof v === 'string' ? STRING_TYPE :
			typeof v === 'boolean' ? BOOLEAN_TYPE : UNORDERED_NODE_ITERATOR_TYPE;
	switch (t) {
	case NUMBER_TYPE: return { type: t, value: toNum(v) };
	case STRING_TYPE: return { type: t, value: toStr(v) };
	case BOOLEAN_TYPE: return { type: t, value: toBool(v) };
	default:
		if (t < 0 || t > 9) throw new TypeError('evaluate: unknown result type ' + type);
		if (!isSet(v)) throw new TypeError('evaluate: the result is not a node-set');
		return { type: t, value: domOf(v).filter(x => x) };
	}
}

/* ---- XSLT: the style sheet ------------------------------------------------------------------- */

function isXsl(n, local) { return n.t === ELEMENT && n.ns === NS_XSL && (local === undefined || n.ln === local); }
function attr(n, name) {
	if (n.a) for (const a of n.a) if (a.ln === name && !a.ns) return a.v;
	return null;
}
function attrNS(n, ns, name) {
	if (n.a) for (const a of n.a) if (a.ln === name && a.ns === ns) return a.v;
	return null;
}
/** the namespaces in scope at a node: prefix -> uri */
function inScope(n) {
	const m = new Map([['xml', NS_XML]]);
	const chain = [];
	for (let x = n.t === ATTRIBUTE ? n.p : n; x; x = x.p) chain.push(x);
	for (let i = chain.length - 1; i >= 0; i--) {
		const d = chain[i].nsd;
		if (d) for (let k = 0; k < d.length; k += 2) {
			if (d[k + 1] === '' && d[k] !== '') m.delete(d[k]);
			else m.set(d[k], d[k + 1]);
		}
	}
	/* (an element's own prefix, if no declaration is seen: a DOM made by script) */
	for (let i = chain.length - 1; i >= 0; i--) {
		const x = chain[i];
		if (x.t === ELEMENT && x.ns && !m.has(x.px || '')) m.set(x.px || '', x.ns);
	}
	return m;
}
function resolverAt(n) {
	let m = null;
	return p => { if (!m) m = inScope(n); return m.has(p) ? m.get(p) : null; };
}

/** an expression of the style sheet, compiled once (cached on its node) */
function xpathAt(n, src) {
	const cache = n._xp || (n._xp = new Map());
	let e = cache.get(src);
	if (!e) { e = compile(src, resolverAt(n)); cache.set(src, e); }
	return e;
}
/** an attribute value template's parts, compiled once */
function avtAt(n, src) {
	const cache = n._avt || (n._avt = new Map());
	let parts = cache.get(src);
	if (parts) return parts;
	parts = [];
	let i = 0, lit = '';
	while (i < src.length) {
		const ch = src[i];
		if (ch === '{') {
			if (src[i + 1] === '{') { lit += '{'; i += 2; continue; }
			let j = i + 1, q = null;
			for (; j < src.length; j++) {
				const cj = src[j];
				if (q) { if (cj === q) q = null; }
				else if (cj === '"' || cj === "'") q = cj;
				else if (cj === '}') break;
			}
			if (j >= src.length) throw xerr('XSLT: unterminated { in ' + src);
			if (lit) { parts.push(lit); lit = ''; }
			parts.push(compile(src.slice(i + 1, j), resolverAt(n)));
			i = j + 1;
		} else if (ch === '}') {
			lit += '}';
			i += src[i + 1] === '}' ? 2 : 1;
		} else { lit += ch; i++; }
	}
	if (lit || !parts.length) parts.push(lit);
	cache.set(src, parts);
	return parts;
}

/** a QName of the style sheet -> {ns, local} */
function qnameAt(n, q, dflt) {
	const i = q.indexOf(':');
	if (i < 0) return { ns: dflt ? (inScope(n).get('') || null) : null, local: q };
	const u = inScope(n).get(q.slice(0, i));
	if (u === undefined) throw xerr('XSLT: undeclared prefix in ' + q, 'NamespaceError');
	return { ns: u, local: q.slice(i + 1) };
}
const qkey = q => (q.ns ? '{' + q.ns + '}' : '') + q.local;

/** pattern alternatives: [{steps, abs, keyfn, priority}] */
function compilePattern(n, src) {
	const e = compile(src, resolverAt(n));
	const alts = e.k === 'union' ? e.list : [e];
	return alts.map(a => {
		if (a.k === 'fn' && (a.name === 'id' || a.name === 'key')) return { fn: a, steps: [], abs: false, priority: 0.5 };
		if (a.k !== 'path') throw xerr('XSLT: bad pattern ' + src);
		const p = { steps: a.steps, abs: a.abs, fn: a.e && a.e.k === 'fn' ? a.e : null };
		if (a.e && a.e.k !== 'fn') throw xerr('XSLT: bad pattern ' + src);
		/* the default priority */
		let pr = 0.5;
		if (!p.fn && p.steps.length === 1 && !p.abs) {
			const st = p.steps[0], t = st.test;
			if (!st.preds.length && (st.axis === 'child' || st.axis === 'attribute')) {
				if (t.type === 'name') pr = t.local === '*' ? (t.ns === undefined ? -0.5 : -0.25) : 0;
				else if (t.type === 'pi' && t.target !== null) pr = 0;
				else pr = -0.5;
			}
		} else if (!p.fn && p.abs && p.steps.length === 0) pr = -0.5;
		p.priority = pr;
		return p;
	});
}

function matchStepNode(st, n, ctx) {
	if (n.t === ROOT) return false;	/* (only "/" matches the root) */
	const axis = st.axis === 'descendant' || st.axis === 'descendant-or-self' ? 'child' : st.axis;
	if (axis === 'attribute') { if (n.t !== ATTRIBUTE) return false; }
	else if (n.t === ATTRIBUTE) return false;
	if (!testNode(st.test, n, axis === 'attribute' ? 'attribute' : 'child')) return false;
	if (!st.preds.length) return true;
	/* (the predicates against the node's siblings the step selects) */
	if (!n.p) return false;
	let cands = axis === 'attribute' ? (n.p.a || []) : (n.p.c || []);
	cands = cands.filter(x => testNode(st.test, x, axis === 'attribute' ? 'attribute' : 'child'));
	for (const p of st.preds) cands = applyPred(p, cands, ctx);
	return cands.includes(n);
}

function matchPattern(p, n, ctx) {
	const steps = p.steps;
	let i = steps.length - 1;
	let cur = n;
	if (i < 0) {
		if (p.fn) return evalExpr(p.fn, Object.assign({}, ctx, { node: n })).includes(n);
		return p.abs && n.t === ROOT;
	}
	/* (the last step against the node; then each step before against a parent or ancestor) */
	const rec = (k, node) => {
		const st = steps[k];
		if (!matchStepNode(st, node, ctx)) return false;
		const parentOf = node.p;
		if (k === 0) {
			if (p.fn) {
				const set = evalExpr(p.fn, Object.assign({}, ctx, { node }));
				if (st.axis === 'descendant' || st.axis === 'descendant-or-self') {
					for (let a = parentOf; a; a = a.p) if (set.includes(a)) return true;
					return false;
				}
				return parentOf ? set.includes(parentOf) : false;
			}
			if (p.abs) {
				if (st.axis === 'descendant' || st.axis === 'descendant-or-self') return true;
				return parentOf ? parentOf.t === ROOT : false;
			}
			return true;
		}
		/* the separator before this step: "//" when it is a descendant step */
		if (st.axis === 'descendant') {
			for (let a = parentOf; a; a = a.p) if (rec(k - 1, a)) return true;
			return false;
		}
		const prev = steps[k - 1];
		if (prev.axis === 'descendant-or-self' && prev.test.type === 'node') {
			/* ("a//b[1]": the descendant-or-self step) */
			if (k - 2 < 0) return p.abs || !!p.fn || true;
			for (let a = parentOf; a; a = a.p) if (rec(k - 2, a)) return true;
			return false;
		}
		return parentOf ? rec(k - 1, parentOf) : false;
	};
	return rec(i, cur);
}

class Stylesheet {
	constructor() {
		this.templates = [];	/* {match: pattern alt, mode, priority, prec, order, node, name} */
		this.named = new Map();
		this.globals = [];	/* {q, node, param} */
		this.keys = new Map();	/* key name -> [{match, use, node}] */
		this.attrSets = new Map();
		this.output = { method: null, doctypePublic: null, doctypeSystem: null, omitDecl: false,
			mediaType: null, encoding: 'UTF-8', indent: false };
		this.strip = [];	/* {test, prec, priority, strip} */
		this.decimal = new Map();
		this.prec = 0;
		this.order = 0;
		this.root = null;
		this.roots = [];
	}
}

function decimalFormatOf(n) {
	const f = { decimal: '.', grouping: ',', infinity: 'Infinity', minus: '-', nan: 'NaN',
		percent: '%', permille: '‰', zero: '0', digit: '#', pattern: ';' };
	const map = { 'decimal-separator': 'decimal', 'grouping-separator': 'grouping', infinity: 'infinity',
		'minus-sign': 'minus', NaN: 'nan', percent: 'percent', 'per-mille': 'permille', 'zero-digit': 'zero',
		digit: 'digit', 'pattern-separator': 'pattern' };
	for (const k of Object.keys(map)) { const v = attr(n, k); if (v !== null) f[map[k]] = v; }
	return f;
}

/**
 * compile(styleDom, resolve) -> a Stylesheet. resolve(href, base) -> a DOM document or null
 * (xsl:include / xsl:import)
 */
function compileStylesheet(dom, resolve, base) {
	const ss = new Stylesheet();
	const snap = snapshot(dom, null);
	ss.root = snap.root;
	loadSheet(ss, snap.root, resolve, base, 0, []);
	/* precedence: import order (the imports below the importing sheet) */
	return ss;
}

function stripStyleWhitespace(n, preserve) {
	if (!n.c) return;
	const sp = attrNS(n, NS_XML, 'space');
	if (sp === 'preserve') preserve = true;
	else if (sp === 'default') preserve = false;
	const keep = [];
	for (const k of n.c) {
		if (k.t === TEXT && !/[^ \t\n\r]/.test(k.v) && !preserve && !(n.t === ELEMENT && isXsl(n, 'text'))) continue;
		if (k.t === COMMENT || k.t === PI) continue;	/* (not instructions) */
		keep.push(k);
		if (k.t === ELEMENT) stripStyleWhitespace(k, preserve);
	}
	n.c = keep;
}

function loadSheet(ss, root, resolve, base, depth, seen) {
	if (depth > 16) throw xerr('XSLT: too many nested imports');
	stripStyleWhitespace(root, false);
	ss.roots.push(root);
	const top = (root.c || []).find(c => c.t === ELEMENT);
	if (!top) throw xerr('XSLT: an empty style sheet');
	if (!isXsl(top, 'stylesheet') && !isXsl(top, 'transform')) {
		/* a literal result element as the style sheet: the template of "/" */
		if (attrNS(top, NS_XSL, 'version') === null) throw xerr('XSLT: not a style sheet');
		const tpl = { match: { steps: [], abs: true, priority: -0.5 }, mode: null, priority: 0.5,
			prec: ss.prec, order: ss.order++, node: { c: [top], t: ELEMENT, ns: NS_XSL, ln: 'template', p: top.p }, name: null };
		ss.templates.push(tpl);
		return;
	}
	/* imports first: lower precedence */
	for (const k of top.c || []) {
		if (isXsl(k, 'import')) {
			const href = attr(k, 'href');
			const d = resolve ? resolve(href, base) : null;
			if (d) {
				const s2 = snapshot(d, null);
				loadSheet(ss, s2.root, resolve, href, depth + 1, seen);
				ss.prec++;
			}
		}
	}
	const prec = ss.prec;
	for (const k of top.c || []) {
		if (k.t !== ELEMENT) continue;
		if (k.ns !== NS_XSL) continue;	/* (top-level elements of other namespaces: ignored) */
		switch (k.ln) {
		case 'import': break;
		case 'include': {
			const href = attr(k, 'href');
			const d = resolve ? resolve(href, base) : null;
			if (d) {
				const s2 = snapshot(d, null);
				stripStyleWhitespace(s2.root, false);
				const t2 = (s2.root.c || []).find(c => c.t === ELEMENT);
				if (t2 && t2.c) {
					/* (its top-level elements, here) */
					const sub = { c: t2.c };
					for (const x of t2.c) x.p = t2;
					loadTop(ss, sub.c, prec, resolve, href);
				}
			}
			break;
		}
		default: loadTop(ss, [k], prec, resolve, base);
		}
	}
}

function loadTop(ss, list, prec, resolve, base) {
	for (const k of list) {
		if (k.t !== ELEMENT || k.ns !== NS_XSL) continue;
		switch (k.ln) {
		case 'template': {
			const name = attr(k, 'name');
			const mode = attr(k, 'mode');
			const modeKey = mode !== null ? qkey(qnameAt(k, mode)) : null;
			const pri = attr(k, 'priority');
			const order = ss.order++;
			const tpl = { node: k, mode: modeKey, prec, order, name: null, params: null };
			if (name !== null) {
				const key = qkey(qnameAt(k, name));
				const prev = ss.named.get(key);
				if (!prev || prev.prec <= prec) ss.named.set(key, tpl);
			}
			const m = attr(k, 'match');
			if (m !== null)
				for (const alt of compilePattern(k, m))
					ss.templates.push(Object.assign({}, tpl, { match: alt,
						priority: pri !== null ? parseFloat(pri) : alt.priority }));
			break;
		}
		case 'variable': case 'param': {
			const q = qnameAt(k, attr(k, 'name') || '');
			ss.globals = ss.globals.filter(g => !(qkey(g.q) === qkey(q) && g.prec <= prec));
			ss.globals.push({ q, node: k, param: k.ln === 'param', prec });
			break;
		}
		case 'output': {
			const o = ss.output;
			const m = attr(k, 'method');
			if (m !== null) o.method = m.indexOf(':') > 0 ? 'xml' : m;
			const dp = attr(k, 'doctype-public'), ds = attr(k, 'doctype-system');
			if (dp !== null) o.doctypePublic = dp;
			if (ds !== null) o.doctypeSystem = ds;
			if (attr(k, 'omit-xml-declaration') === 'yes') o.omitDecl = true;
			if (attr(k, 'media-type') !== null) o.mediaType = attr(k, 'media-type');
			if (attr(k, 'encoding') !== null) o.encoding = attr(k, 'encoding');
			if (attr(k, 'indent') === 'yes') o.indent = true;
			break;
		}
		case 'key': {
			const name = qkey(qnameAt(k, attr(k, 'name') || ''));
			const list = ss.keys.get(name) || [];
			list.push({ match: compilePattern(k, attr(k, 'match') || ''), use: xpathAt(k, attr(k, 'use') || ''), node: k });
			ss.keys.set(name, list);
			break;
		}
		case 'attribute-set': {
			const name = qkey(qnameAt(k, attr(k, 'name') || ''));
			const list = ss.attrSets.get(name) || [];
			list.push(k);
			ss.attrSets.set(name, list);
			break;
		}
		case 'strip-space': case 'preserve-space': {
			for (const t of (attr(k, 'elements') || '').split(/\s+/).filter(Boolean)) {
				let test;
				if (t === '*') test = { ns: undefined, local: '*' };
				else if (t.endsWith(':*')) test = { ns: inScope(k).get(t.slice(0, -2)), local: '*' };
				else test = qnameAt(k, t);
				ss.strip.push({ test, prec, strip: k.ln === 'strip-space',
					priority: test.local === '*' ? (test.ns === undefined ? -0.5 : -0.25) : 0 });
			}
			break;
		}
		case 'decimal-format': {
			const name = attr(k, 'name');
			ss.decimal.set(name === null ? '' : qkey(qnameAt(k, name)), decimalFormatOf(k));
			break;
		}
		default: break;	/* (namespace-alias, ...: not supported) */
		}
	}
}

/* ---- XSLT: the transform --------------------------------------------------------------------- */

class Output {
	constructor() {
		newTree();
		this.root = new XNode(ROOT, null);
		this.cur = this.root;
	}
	append(n) {
		n.p = this.cur;
		const c = this.cur.c || (this.cur.c = []);
		c.push(n);
	}
	text(s, doe) {
		if (!s) return;
		const c = this.cur.c;
		const last = c && c[c.length - 1];
		if (last && last.t === TEXT && last.doe === !!doe) { last.v += s; return; }
		const t = new XNode(TEXT, null);
		t.v = s;
		t.doe = !!doe;
		this.append(t);
	}
	attr(q, ns, px, value) {
		const el = this.cur;
		if (el.t !== ELEMENT) return;	/* (an attribute after a child, or at the top: ignored) */
		if (el.c && el.c.length) return;
		const a = el.a || (el.a = []);
		for (let i = 0; i < a.length; i++)
			if (a[i].ln === q && a[i].ns === ns) { a[i].v = value; a[i].px = px; return; }
		const n = new XNode(ATTRIBUTE, el);
		n.ln = q; n.ns = ns; n.px = px; n.v = value;
		a.push(n);
	}
}

const MAX_DEPTH = 3000;

class Transform {
	constructor(ss, params) {
		this.ss = ss;
		this.params = params || new Map();	/* "{ns}local" -> value */
		this.globals = new Map();
		this.globalBusy = new Set();
		this.keyIndex = new Map();
		this.depth = 0;
		this.messages = [];
		this.ids = new Map();
	}

	/* variables: a chain of frames */
	lookup(frame, q, name) {
		const k = qkey(q);
		for (let f = frame; f; f = f.up) if (f.vars.has(k)) return f.vars.get(k);
		return this.global(k);
	}
	global(k) {
		if (this.globals.has(k)) return this.globals.get(k);
		const g = this.ss.globals.find(x => qkey(x.q) === k);
		if (!g) return undefined;
		if (g.param && this.params.has(k)) {
			const v = this.params.get(k);
			this.globals.set(k, v);
			return v;
		}
		if (this.globalBusy.has(k)) throw xerr('XSLT: a circular reference to $' + k);
		this.globalBusy.add(k);
		const v = this.varValue(g.node, this.srcRoot, 1, 1, null, this.srcRoot);
		this.globalBusy.delete(k);
		this.globals.set(k, v);
		return v;
	}

	ctx(node, pos, size, frame, current) {
		return {
			node, pos, size,
			vars: (q, name) => this.lookup(frame, q, name),
			fns: this.fns,
			current: current || node,
		};
	}

	xp(n, src, node, pos, size, frame, current) {
		return evalExpr(xpathAt(n, src), this.ctx(node, pos, size, frame, current));
	}
	avt(n, src, node, pos, size, frame) {
		const parts = avtAt(n, src);
		if (parts.length === 1 && typeof parts[0] === 'string') return parts[0];
		const c = this.ctx(node, pos, size, frame);
		let s = '';
		for (const p of parts) s += typeof p === 'string' ? p : toStr(evalExpr(p, c));
		return s;
	}

	/** a variable's / parameter's value: its select, else its content as a result tree fragment */
	varValue(n, node, pos, size, frame, current) {
		const sel = attr(n, 'select');
		if (sel !== null) return this.xp(n, sel, node, pos, size, frame, current);
		if (!n.c || !n.c.length) return '';
		const out = new Output();
		this.exec(n.c, node, pos, size, frame, out);
		return [out.root];	/* (a result tree fragment: a node-set of its root) */
	}

	run(source) {
		this.srcRoot = source;
		this.fns = this.makeFns();
		const out = new Output();
		const save = num15;
		num15 = true;
		try {
			this.applyTemplates([source], null, null, out, new Map());
		} finally {
			num15 = save;
		}
		return out;
	}

	makeFns() {
		const self = this;
		const f = {
			current: (a, c) => [c.current],
			key: (a, c) => self.key(toStr(evalExpr(a[0], c)), evalExpr(a[1], c), c),
			'generate-id': (a, c) => {
				const s = a.length ? argSet(a[0], c, 'generate-id') : [c.node];
				return s.length ? 'idx' + s[0].o.toString(36) : '';
			},
			'format-number': (a, c) => formatNumber(toNum(evalExpr(a[0], c)), toStr(evalExpr(a[1], c)),
				self.ss.decimal.get(a.length > 2 ? self.decimalKey(toStr(evalExpr(a[2], c)), c) : '')),
			'system-property': (a, c) => {
				const p = toStr(evalExpr(a[0], c));
				if (/(^|:)version$/.test(p)) return 1;
				if (/(^|:)vendor$/.test(p)) return 'Jet Browser (Onyx)';
				if (/(^|:)vendor-url$/.test(p)) return 'https://github.com/stephaneweg/Onyx';
				return '';
			},
			'element-available': (a, c) => {
				const p = toStr(evalExpr(a[0], c));
				return /^(xsl:)?(apply-imports|apply-templates|attribute|call-template|choose|comment|copy|copy-of|element|fallback|for-each|if|message|number|processing-instruction|text|value-of|variable)$/.test(p);
			},
			'function-available': (a, c) => {
				const p = toStr(evalExpr(a[0], c));
				return !!FNS[p] || ['current', 'key', 'generate-id', 'format-number', 'system-property',
					'element-available', 'function-available', 'document', 'unparsed-entity-uri'].includes(p) ||
					/(^|:)node-set$/.test(p);
			},
			'unparsed-entity-uri': () => '',
			document: (a, c) => {
				const v = evalExpr(a[0], c);
				const uris = isSet(v) ? v.map(stringValue) : [toStr(v)];
				const out = [];
				for (const u of uris) {
					if (u === '' && self.ss.root) out.push(self.ss.root);
					else if (self.docResolve) { const d = self.docResolve(u); if (d) out.push(d); }
				}
				return docOrder(out);
			},
		};
		const nodeSet = (a, c) => { const v = evalExpr(a[0], c); return isSet(v) ? v : [textNode(toStr(v))]; };
		f['{' + NS_EXSL + '}node-set'] = nodeSet;
		f['{' + NS_MSXSL + '}node-set'] = nodeSet;
		f['{' + NS_EXSL + '}object-type'] = (a, c) => {
			const v = evalExpr(a[0], c);
			return isSet(v) ? (v.length === 1 && v[0].t === ROOT && !v[0].d ? 'RTF' : 'node-set') : typeof v;
		};
		return f;
	}
	decimalKey(name, c) {
		const i = name.indexOf(':');
		return i < 0 ? name : name;	/* (prefixed names: as written) */
	}

	key(name, value, c) {
		const k = name.indexOf(':') > 0 ? name : name;
		let defs = this.ss.keys.get(k);
		if (!defs) for (const [kk, d] of this.ss.keys) if (kk.endsWith('}' + name) || kk === name) defs = d;
		if (!defs) throw xerr('XSLT: unknown key ' + name);
		const root = rootOf(c.node);
		let idx = this.keyIndex.get(defs);
		if (!idx) { idx = new Map(); this.keyIndex.set(defs, idx); }
		let map = idx.get(root);
		if (!map) {
			map = new Map();
			const all = descendants(root, [], true);
			for (const n of all.slice()) if (n.a) for (const a of n.a) all.push(a);
			for (const d of defs) {
				for (const n of all) {
					if (!d.match.some(p => matchPattern(p, n, this.ctx(n, 1, 1, null)))) continue;
					const u = evalExpr(d.use, this.ctx(n, 1, 1, null));
					const vals = isSet(u) ? u.map(stringValue) : [toStr(u)];
					for (const v of vals) {
						let l = map.get(v);
						if (!l) map.set(v, l = []);
						l.push(n);
					}
				}
			}
			idx.set(root, map);
		}
		const vals = isSet(value) ? value.map(stringValue) : [toStr(value)];
		let out = [];
		for (const v of vals) { const l = map.get(v); if (l) out = out.concat(l); }
		return docOrder(out);
	}

	/** the template rule for a node in a mode (null: the built-in) */
	findTemplate(n, mode, below) {
		let best = null;
		let ctx = null;
		const idx = this.index();
		const named = (n.t === ELEMENT || n.t === ATTRIBUTE) ? idx.byName.get(n.ln) : null;
		for (const list of named ? [named, idx.generic] : [idx.generic]) {
			for (const t of list) {
				if (t.mode !== mode) continue;
				if (below !== undefined && t.prec >= below) continue;
				if (best && !(t.prec > best.prec || (t.prec === best.prec && (t.priority > best.priority ||
						(t.priority === best.priority && t.order > best.order)))))
					continue;
				if (!ctx) ctx = this.ctx(n, 1, 1, null);
				if (!matchPattern(t.match, n, ctx)) continue;
				best = t;
			}
		}
		return best;
	}
	/** the templates by the name their pattern's last step tests (the others generic) */
	index() {
		if (this.ss._index) return this.ss._index;
		const byName = new Map(), generic = [];
		for (const t of this.ss.templates) {
			const st = t.match.steps && t.match.steps[t.match.steps.length - 1];
			if (st && st.test.type === 'name' && st.test.local !== '*' &&
			    !(st.test.ns === null && !st.test.prefixed && false)) {
				const l = byName.get(st.test.local) || [];
				l.push(t);
				byName.set(st.test.local, l);
			} else generic.push(t);
		}
		this.ss._index = { byName, generic };
		return this.ss._index;
	}

	applyTemplates(nodes, mode, frame, out, params, below) {
		const size = nodes.length;
		for (let i = 0; i < size; i++) {
			const n = nodes[i];
			const t = this.findTemplate(n, mode, below);
			if (!t) { this.builtin(n, mode, out, params); continue; }
			this.callTemplate(t, n, i + 1, size, out, params, frame);
		}
	}

	builtin(n, mode, out, params) {
		switch (n.t) {
		case ROOT: case ELEMENT:
			this.applyTemplates(n.c || [], mode, null, out, params);
			break;
		case TEXT: case ATTRIBUTE:
			out.text(n.v);
			break;
		default: break;
		}
	}

	callTemplate(t, node, pos, size, out, params, frame) {
		if (++this.depth > MAX_DEPTH) throw xerr('XSLT: too deep a recursion');
		const f = { vars: new Map(), up: null, tpl: t };
		const body = t.node.c || [];
		let i = 0;
		for (; i < body.length; i++) {
			const k = body[i];
			if (!isXsl(k, 'param')) break;
			const q = qkey(qnameAt(k, attr(k, 'name') || ''));
			f.vars.set(q, params && params.has(q) ? params.get(q) :
				this.varValue(k, node, pos, size, f, node));
		}
		this.exec(i ? body.slice(i) : body, node, pos, size, f, out);
		this.depth--;
	}

	withParams(n, node, pos, size, frame) {
		const m = new Map();
		for (const k of n.c || [])
			if (isXsl(k, 'with-param'))
				m.set(qkey(qnameAt(k, attr(k, 'name') || '')), this.varValue(k, node, pos, size, frame, node));
		return m;
	}

	sortNodes(n, nodes, frame) {
		const keys = (n.c || []).filter(k => isXsl(k, 'sort'));
		if (!keys.length) return nodes;
		const size = nodes.length;
		const specs = keys.map(k => ({
			sel: attr(k, 'select') || '.',
			k,
			type: attr(k, 'data-type') !== null ? this.avt(k, attr(k, 'data-type'), nodes[0], 1, size, frame) : 'text',
			desc: attr(k, 'order') !== null && this.avt(k, attr(k, 'order'), nodes[0], 1, size, frame) === 'descending',
			upper: attr(k, 'case-order') !== null && this.avt(k, attr(k, 'case-order'), nodes[0], 1, size, frame) === 'upper-first',
		}));
		const rows = nodes.map((node, i) => ({
			node, i,
			vals: specs.map(s => {
				const v = toStr(this.xp(s.k, s.sel, node, i + 1, size, frame));
				return s.type === 'number' ? strToNum(v) : v;
			}),
		}));
		rows.sort((a, b) => {
			for (let j = 0; j < specs.length; j++) {
				const s = specs[j];
				let x = a.vals[j], y = b.vals[j], r = 0;
				if (s.type === 'number') {
					const nx = Number.isNaN(x), ny = Number.isNaN(y);
					r = nx && ny ? 0 : nx ? -1 : ny ? 1 : x - y;
				} else {
					r = x.toLowerCase() < y.toLowerCase() ? -1 : x.toLowerCase() > y.toLowerCase() ? 1 : 0;
					if (!r && x !== y) {
						/* (the same letters: the case order) */
						r = x < y ? (s.upper ? -1 : 1) : (s.upper ? 1 : -1);
					}
				}
				if (r) return s.desc ? -r : r;
			}
			return a.i - b.i;
		});
		return rows.map(r => r.node);
	}

	useAttrSets(n, names, node, pos, size, frame, out, seen) {
		for (const nm of names.split(/\s+/).filter(Boolean)) {
			const key = qkey(qnameAt(n, nm));
			if (seen.has(key)) throw xerr('XSLT: attribute set ' + nm + ' uses itself');
			const sets = this.ss.attrSets.get(key);
			if (!sets) continue;
			seen.add(key);
			for (const s of sets) {
				const u = attr(s, 'use-attribute-sets');
				if (u) this.useAttrSets(s, u, node, pos, size, null, out, seen);
				this.exec((s.c || []).filter(k => isXsl(k, 'attribute')), node, pos, size, null, out);
			}
			seen.delete(key);
		}
	}

	/** an element of the result: its namespace prefix chosen */
	startElement(out, local, ns, px) {
		const el = new XNode(ELEMENT, null);
		el.ln = local; el.ns = ns; el.px = px || null;
		out.append(el);
		return el;
	}

	exec(list, node, pos, size, frame, out) {
		let f = frame;
		for (let i = 0; i < list.length; i++) {
			const k = list[i];
			if (k.t === TEXT) { out.text(k.v); continue; }
			if (k.t !== ELEMENT) continue;
			if (k.ns !== NS_XSL) { this.literal(k, node, pos, size, f, out); continue; }
			switch (k.ln) {
			case 'variable': case 'param': {
				const q = qkey(qnameAt(k, attr(k, 'name') || ''));
				const v = this.varValue(k, node, pos, size, f, node);
				f = { vars: new Map([[q, v]]), up: f, tpl: f ? f.tpl : null };
				break;
			}
			case 'value-of': {
				const v = this.xp(k, attr(k, 'select') || '.', node, pos, size, f);
				out.text(toStr(v), attr(k, 'disable-output-escaping') === 'yes');
				break;
			}
			case 'text':
				out.text(stringValue(k), attr(k, 'disable-output-escaping') === 'yes');
				break;
			case 'apply-templates': {
				const sel = attr(k, 'select');
				let nodes = sel !== null ? this.xp(k, sel, node, pos, size, f) : (node.c || []);
				if (!isSet(nodes)) throw new TypeError('XSLT: apply-templates select is not a node-set');
				nodes = this.sortNodes(k, nodes, f);
				const mode = attr(k, 'mode');
				this.applyTemplates(nodes, mode !== null ? qkey(qnameAt(k, mode)) : null, f, out,
					this.withParams(k, node, pos, size, f));
				break;
			}
			case 'call-template': {
				const name = qkey(qnameAt(k, attr(k, 'name') || ''));
				const t = this.ss.named.get(name);
				if (!t) throw xerr('XSLT: no template named ' + attr(k, 'name'));
				this.callTemplate(t, node, pos, size, out, this.withParams(k, node, pos, size, f), f);
				break;
			}
			case 'apply-imports': {
				const t = f && f.tpl;
				if (t) {
					const found = this.findTemplate(node, t.mode, t.prec);
					if (found) this.callTemplate(found, node, pos, size, out, new Map(), f);
					else this.builtin(node, t.mode, out, new Map());
				}
				break;
			}
			case 'for-each': {
				let nodes = this.xp(k, attr(k, 'select') || '.', node, pos, size, f);
				if (!isSet(nodes)) throw new TypeError('XSLT: for-each select is not a node-set');
				nodes = this.sortNodes(k, nodes, f);
				const body = (k.c || []).filter(x => !isXsl(x, 'sort'));
				for (let j = 0; j < nodes.length; j++)
					this.exec(body, nodes[j], j + 1, nodes.length, f, out);
				break;
			}
			case 'if':
				if (toBool(this.xp(k, attr(k, 'test') || 'false()', node, pos, size, f)))
					this.exec(k.c || [], node, pos, size, f, out);
				break;
			case 'choose':
				for (const w of k.c || []) {
					if (isXsl(w, 'when')) {
						if (toBool(this.xp(w, attr(w, 'test') || 'false()', node, pos, size, f))) {
							this.exec(w.c || [], node, pos, size, f, out);
							break;
						}
					} else if (isXsl(w, 'otherwise')) {
						this.exec(w.c || [], node, pos, size, f, out);
						break;
					}
				}
				break;
			case 'copy-of': {
				const v = this.xp(k, attr(k, 'select') || '.', node, pos, size, f);
				if (isSet(v)) for (const n of v) this.copyNode(n, out, true);
				else out.text(toStr(v));
				break;
			}
			case 'copy': {
				if (node.t === ELEMENT) {
					const el = this.startElement(out, node.ln, node.ns, node.px);
					if (node.nsd) el.nsd = node.nsd.slice();
					const save = out.cur;
					out.cur = el;
					const u = attr(k, 'use-attribute-sets');
					if (u) this.useAttrSets(k, u, node, pos, size, f, out, new Set());
					this.exec(k.c || [], node, pos, size, f, out);
					out.cur = save;
				} else if (node.t === ROOT) {
					this.exec(k.c || [], node, pos, size, f, out);
				} else {
					this.copyNode(node, out, false);
				}
				break;
			}
			case 'element': {
				const name = this.avt(k, attr(k, 'name') || '', node, pos, size, f);
				const nsAttr = attr(k, 'namespace');
				let ns, px = null, local = name;
				const ci = name.indexOf(':');
				if (ci > 0) { px = name.slice(0, ci); local = name.slice(ci + 1); }
				if (nsAttr !== null) ns = this.avt(k, nsAttr, node, pos, size, f) || null;
				else ns = inScope(k).get(px || '') || null;
				if (nsAttr === null && px && ns === null) throw xerr('XSLT: undeclared prefix in element name ' + name);
				const el = this.startElement(out, local, ns, px);
				const save = out.cur;
				out.cur = el;
				const u = attr(k, 'use-attribute-sets');
				if (u) this.useAttrSets(k, u, node, pos, size, f, out, new Set());
				this.exec(k.c || [], node, pos, size, f, out);
				out.cur = save;
				break;
			}
			case 'attribute': {
				const name = this.avt(k, attr(k, 'name') || '', node, pos, size, f);
				const nsAttr = attr(k, 'namespace');
				let ns = null, px = null, local = name;
				const ci = name.indexOf(':');
				if (ci > 0) { px = name.slice(0, ci); local = name.slice(ci + 1); }
				if (nsAttr !== null) ns = this.avt(k, nsAttr, node, pos, size, f) || null;
				else if (px) ns = inScope(k).get(px) || null;
				if (local === 'xmlns' && !px) break;
				if (ns && !px) px = 'ns' + (k.o % 1000);
				const sub = new Output();
				this.exec(k.c || [], node, pos, size, f, sub);
				out.attr(local, ns, px, textOf(sub.root));
				break;
			}
			case 'comment': {
				const sub = new Output();
				this.exec(k.c || [], node, pos, size, f, sub);
				const c = new XNode(COMMENT, null);
				c.v = textOf(sub.root).replace(/--/g, '- -').replace(/-$/, '- ');
				out.append(c);
				break;
			}
			case 'processing-instruction': {
				const name = this.avt(k, attr(k, 'name') || '', node, pos, size, f);
				const sub = new Output();
				this.exec(k.c || [], node, pos, size, f, sub);
				const p = new XNode(PI, null);
				p.ln = name;
				p.v = textOf(sub.root).replace(/\?>/g, '? >');
				out.append(p);
				break;
			}
			case 'number':
				out.text(this.number(k, node, pos, size, f));
				break;
			case 'message': {
				const sub = new Output();
				this.exec(k.c || [], node, pos, size, f, sub);
				const msg = textOf(sub.root);
				if (N.log) N.log('XSLT message: ' + msg);
				if (attr(k, 'terminate') === 'yes') throw xerr('XSLT: terminated by xsl:message: ' + msg, 'OperationError');
				break;
			}
			case 'fallback': break;
			case 'sort': case 'with-param': break;
			default: {
				/* (an unknown instruction: its xsl:fallback) */
				const fb = (k.c || []).filter(x => isXsl(x, 'fallback'));
				for (const x of fb) this.exec(x.c || [], node, pos, size, f, out);
			}
			}
		}
	}

	literal(k, node, pos, size, f, out) {
		/* (an extension element: its fallback) */
		const el = this.startElement(out, k.ln, k.ns, k.px);
		/* the namespaces of the style sheet element, less XSLT's and the excluded ones */
		const excluded = new Set([NS_XSL]);
		for (let x = k; x; x = x.p) {
			if (x.t !== ELEMENT) continue;
			const ex = x.ns === NS_XSL ? attr(x, 'exclude-result-prefixes') : attrNS(x, NS_XSL, 'exclude-result-prefixes');
			const ext = x.ns === NS_XSL ? attr(x, 'extension-element-prefixes') : attrNS(x, NS_XSL, 'extension-element-prefixes');
			for (const list of [ex, ext]) if (list)
				for (const p of list.split(/\s+/).filter(Boolean)) {
					const u = p === '#default' ? inScope(x).get('') : inScope(x).get(p);
					if (u) excluded.add(u);
				}
		}
		const scope = inScope(k);
		const decl = [];
		for (const [p, u] of scope) {
			if (p === 'xml' || excluded.has(u)) continue;
			decl.push(p, u);
		}
		if (decl.length) el.nsd = decl;
		const save = out.cur;
		out.cur = el;
		const u = attrNS(k, NS_XSL, 'use-attribute-sets');
		if (u) this.useAttrSets(k, u, node, pos, size, f, out, new Set());
		if (k.a) for (const a of k.a) {
			if (a.ns === NS_XSL) continue;
			out.attr(a.ln, a.ns, a.px, this.avt(k, a.v, node, pos, size, f));
		}
		this.exec(k.c || [], node, pos, size, f, out);
		out.cur = save;
	}

	copyNode(n, out, deep) {
		switch (n.t) {
		case ROOT:
			if (deep) for (const c of n.c || []) this.copyNode(c, out, true);
			break;
		case ELEMENT: {
			const el = this.startElement(out, n.ln, n.ns, n.px);
			if (n.nsd) el.nsd = n.nsd.slice();
			if (deep) {
				if (n.a) for (const a of n.a) {
					const c = new XNode(ATTRIBUTE, el);
					c.ln = a.ln; c.ns = a.ns; c.px = a.px; c.v = a.v;
					(el.a || (el.a = [])).push(c);
				}
				const save = out.cur;
				out.cur = el;
				for (const c of n.c || []) this.copyNode(c, out, true);
				out.cur = save;
			}
			break;
		}
		case ATTRIBUTE: out.attr(n.ln, n.ns, n.px, n.v); break;
		case TEXT: out.text(n.v, n.doe); break;
		case COMMENT: { const c = new XNode(COMMENT, null); c.v = n.v; out.append(c); break; }
		case PI: { const c = new XNode(PI, null); c.ln = n.ln; c.v = n.v; out.append(c); break; }
		}
	}

	number(k, node, pos, size, f) {
		const value = attr(k, 'value');
		let nums;
		if (value !== null) {
			nums = [Math.round(toNum(this.xp(k, value, node, pos, size, f)))];
		} else {
			const level = attr(k, 'level') || 'single';
			const count = attr(k, 'count');
			const from = attr(k, 'from');
			const ctx = this.ctx(node, pos, size, f);
			const countP = count !== null ? compilePattern(k, count) : null;
			const fromP = from !== null ? compilePattern(k, from) : null;
			const matchesCount = n => countP ? countP.some(p => matchPattern(p, n, ctx)) :
				n.t === node.t && n.ln === node.ln && n.ns === node.ns;
			const matchesFrom = n => fromP ? fromP.some(p => matchPattern(p, n, ctx)) : false;
			const siblingsBefore = n => {
				let c = 1;
				if (n.p && n.p.c) for (const s of n.p.c) { if (s === n) break; if (matchesCount(s)) c++; }
				return c;
			};
			if (level === 'any') {
				let c = 0;
				const all = descendants(rootOf(node), [], true);
				for (const n of all) {
					if (n.o > node.o) break;
					if (fromP && matchesFrom(n)) c = 0;
					if (matchesCount(n)) c++;
				}
				nums = c ? [c] : [];
			} else {
				const anc = [];
				for (let a = node; a; a = a.p) {
					if (fromP && matchesFrom(a)) break;
					if (matchesCount(a)) { anc.push(a); if (level === 'single') break; }
				}
				nums = anc.reverse().map(siblingsBefore);
			}
		}
		const fmt = attr(k, 'format') !== null ? this.avt(k, attr(k, 'format'), node, pos, size, f) : '1';
		const gs = attr(k, 'grouping-separator') !== null ? this.avt(k, attr(k, 'grouping-separator'), node, pos, size, f) : null;
		const gz = attr(k, 'grouping-size') !== null ? parseInt(this.avt(k, attr(k, 'grouping-size'), node, pos, size, f), 10) : 0;
		return formatNumbers(nums, fmt, gs, gz);
	}
}

function textOf(n) {
	let s = '';
	const walk = x => { for (const c of x.c || []) { if (c.t === TEXT) s += c.v; else if (c.t === ELEMENT) walk(c); } };
	walk(n);
	return s;
}
function textNode(s) {
	newTree();
	const r = new XNode(ROOT, null);
	const t = new XNode(TEXT, r);
	t.v = s;
	r.c = [t];
	return t;
}

/* xsl:number's format: 1 01 a A i I, the separators between */
function formatNumbers(nums, fmt, gs, gz) {
	const toks = [];
	const re = /[0-9A-Za-zÀ-￿]+/g;
	let last = 0, m;
	const seps = [];
	let prefix = '', suffix = '';
	const fmts = [];
	while ((m = re.exec(fmt))) {
		const sep = fmt.slice(last, m.index);
		if (!fmts.length) prefix = sep; else seps.push(sep);
		fmts.push(m[0]);
		last = m.index + m[0].length;
	}
	suffix = fmt.slice(last);
	if (!fmts.length) { fmts.push('1'); }
	let s = prefix;
	nums.forEach((n, i) => {
		const f = fmts[Math.min(i, fmts.length - 1)];
		if (i > 0) s += seps.length ? seps[Math.min(i - 1, seps.length - 1)] : '.';
		s += formatOne(n, f, gs, gz);
	});
	return s + suffix;
}
function formatOne(n, f, gs, gz) {
	if (!Number.isFinite(n)) return String(n);
	if (f === 'a' || f === 'A') {
		if (n < 1) return String(n);
		let s = '';
		for (let x = n; x > 0; x = Math.floor((x - 1) / 26)) s = String.fromCharCode((f === 'a' ? 97 : 65) + (x - 1) % 26) + s;
		return s;
	}
	if (f === 'i' || f === 'I') {
		if (n < 1 || n > 3999) return String(n);
		const R = [[1000, 'm'], [900, 'cm'], [500, 'd'], [400, 'cd'], [100, 'c'], [90, 'xc'], [50, 'l'],
			[40, 'xl'], [10, 'x'], [9, 'ix'], [5, 'v'], [4, 'iv'], [1, 'i']];
		let s = '';
		for (const [v, r] of R) while (n >= v) { s += r; n -= v; }
		return f === 'I' ? s.toUpperCase() : s;
	}
	let s = String(n);
	const width = /^0*1$/.test(f) ? f.length : 1;
	while (s.length < width) s = '0' + s;
	if (gs && gz > 0) {
		let r = '';
		for (let i = 0; i < s.length; i++) {
			if (i && (s.length - i) % gz === 0) r += gs;
			r += s[i];
		}
		s = r;
	}
	return s;
}

/* format-number() (the JDK 1.1 DecimalFormat patterns XSLT 1.0 names) */
function formatNumber(x, pattern, df) {
	const d = df || { decimal: '.', grouping: ',', infinity: 'Infinity', minus: '-', nan: 'NaN',
		percent: '%', permille: '‰', zero: '0', digit: '#', pattern: ';' };
	if (Number.isNaN(x)) return d.nan;
	const parts = pattern.split(d.pattern);
	let pat = parts[0];
	const neg = x < 0 || Object.is(x, -0);
	if (neg && parts.length > 1) pat = parts[1];
	const isSpecial = ch => ch === d.digit || ch === d.zero || ch === d.grouping || ch === d.decimal ||
		(ch >= '0' && ch <= '9');
	let i = 0;
	while (i < pat.length && !isSpecial(pat[i])) i++;
	let j = pat.length;
	while (j > i && !isSpecial(pat[j - 1])) j--;
	const prefix = pat.slice(0, i), suffix = pat.slice(j), core = pat.slice(i, j);
	let v = Math.abs(x);
	if (prefix.includes(d.percent) || suffix.includes(d.percent)) v *= 100;
	else if (prefix.includes(d.permille) || suffix.includes(d.permille)) v *= 1000;
	if (!Number.isFinite(v)) return (neg && parts.length < 2 ? d.minus : '') + prefix + d.infinity + suffix;
	const di = core.indexOf(d.decimal);
	const intPat = di < 0 ? core : core.slice(0, di), fracPat = di < 0 ? '' : core.slice(di + 1);
	const minInt = (intPat.match(new RegExp('[0-9' + d.zero.replace(/[\]\\^-]/g, '\\$&') + ']', 'g')) || []).length;
	const minFrac = (fracPat.match(/0/g) || []).length;
	const maxFrac = (fracPat.replace(new RegExp('\\' + d.grouping, 'g'), '').length);
	const gi = intPat.lastIndexOf(d.grouping);
	const group = gi >= 0 ? intPat.length - gi - 1 : 0;
	let s = v.toFixed(maxFrac);
	let [ip, fp = ''] = s.split('.');
	while (fp.length > minFrac && fp.endsWith('0')) fp = fp.slice(0, -1);
	ip = ip.replace(/^0+/, '');
	while (ip.length < minInt) ip = '0' + ip;
	if (group > 0) {
		let r = '';
		for (let k = 0; k < ip.length; k++) {
			if (k && (ip.length - k) % group === 0) r += d.grouping;
			r += ip[k];
		}
		ip = r;
	}
	if (d.zero !== '0') {
		const z = d.zero.charCodeAt(0);
		const map = t => t.replace(/[0-9]/g, c => String.fromCharCode(z + c.charCodeAt(0) - 48));
		ip = map(ip); fp = map(fp);
	}
	s = ip + (fp ? d.decimal + fp : '');
	if (!s) s = d.zero;
	return (neg && parts.length < 2 && v !== 0 ? d.minus : '') + prefix + s + suffix;
}

/* ---- the source's whitespace (xsl:strip-space) ----------------------------------------------- */

function stripSource(ss, root) {
	if (!ss.strip.length) return;
	const decide = el => {
		let best = null;
		for (const r of ss.strip) {
			const t = r.test;
			const ok = t.local === '*' ? (t.ns === undefined || el.ns === t.ns) : (el.ln === t.local && el.ns === t.ns);
			if (!ok) continue;
			if (!best || r.prec > best.prec || (r.prec === best.prec && r.priority >= best.priority)) best = r;
		}
		return best ? best.strip : false;
	};
	const walk = (n, preserve) => {
		if (!n.c) return;
		let strip = false;
		if (n.t === ELEMENT) {
			const sp = attrNS(n, NS_XML, 'space');
			if (sp === 'preserve') preserve = true;
			else if (sp === 'default') preserve = false;
			strip = !preserve && decide(n);
		}
		if (strip) n.c = n.c.filter(c => !(c.t === TEXT && !/[^ \t\n\r]/.test(c.v)));
		for (const c of n.c) if (c.t === ELEMENT) walk(c, preserve);
	};
	walk(root, false);
}

/* ---- serialization ------------------------------------------------------------------------- */

const VOID = new Set(['area', 'base', 'basefont', 'br', 'col', 'embed', 'frame', 'hr', 'img',
	'input', 'isindex', 'link', 'meta', 'param', 'source', 'track', 'wbr']);
const RAW = new Set(['script', 'style']);

function escText(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
function escAttr(s) {
	return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;')
		.replace(/\n/g, '&#10;').replace(/\r/g, '&#13;').replace(/\t/g, '&#9;');
}
function escHtmlAttr(s) { return s.replace(/&(?!\{)/g, '&amp;').replace(/"/g, '&quot;'); }

/** the output method: the style sheet's, else html for an <html> first element */
function methodOf(ss, out) {
	if (ss.output.method) return ss.output.method;
	for (const c of out.root.c || []) {
		if (c.t === TEXT && /[^ \t\n\r]/.test(c.v)) return 'xml';
		if (c.t === ELEMENT) return c.ln.toLowerCase() === 'html' && !c.ns ? 'html' : 'xml';
	}
	return 'xml';
}

function serialize(root, method, o) {
	const out = [];
	if (method === 'text') return textOf(root);
	const first = (root.c || []).find(c => c.t === ELEMENT);
	if (method === 'xml' && !o.omitDecl)
		out.push('<?xml version="1.0" encoding="UTF-8"?>');
	if (first && (o.doctypeSystem || o.doctypePublic)) {
		const nm = method === 'html' ? 'html' : first.qn;
		out.push('<!DOCTYPE ' + nm + (o.doctypePublic ? ' PUBLIC "' + o.doctypePublic + '"' + (o.doctypeSystem ? ' "' + o.doctypeSystem + '"' : '') :
			' SYSTEM "' + o.doctypeSystem + '"') + '>');
		if (method === 'xml') out.push('\n');
	} else if (method === 'xml' && !o.omitDecl) {
		out.push('\n');
	}
	const html = method === 'html';
	const walk = (n, scope, rawParent) => {
		for (const c of n.c || []) {
			switch (c.t) {
			case TEXT:
				out.push(c.doe || rawParent ? c.v : escText(c.v));
				break;
			case COMMENT: out.push('<!--', c.v, '-->'); break;
			case PI: out.push('<?', c.ln, c.v ? ' ' + c.v : '', html ? '>' : '?>'); break;
			case ELEMENT: {
				const isHtmlEl = html && (!c.ns || c.ns === NS_HTML);
				const name = isHtmlEl ? c.ln : c.qn;
				out.push('<', name);
				let sc = scope;
				const declare = (p, u) => {
					if (sc.get(p) === u) return;
					if (sc === scope) sc = new Map(scope);
					sc.set(p, u);
					out.push(p ? ' xmlns:' + p + '="' : ' xmlns="', escAttr(u), '"');
				};
				if (!isHtmlEl) {
					if (c.nsd) for (let k = 0; k < c.nsd.length; k += 2)
						if (c.nsd[k] !== 'xml') declare(c.nsd[k], c.nsd[k + 1]);
					declare(c.px || '', c.ns || '');
				}
				if (c.a) for (const a of c.a) {
					let an = a.ln;
					if (a.ns) {
						if (a.ns === NS_XML) an = 'xml:' + a.ln;
						else {
							let p = a.px;
							if (!p || (sc.has(p) && sc.get(p) !== a.ns)) p = 'ns' + (a.o % 1000);
							if (!html || !isHtmlEl) declare(p, a.ns);
							an = p + ':' + a.ln;
						}
					}
					out.push(' ', an, '="', isHtmlEl ? escHtmlAttr(a.v) : escAttr(a.v), '"');
				}
				if (isHtmlEl) {
					out.push('>');
					const lname = c.ln.toLowerCase();
					if (VOID.has(lname) && !(c.c && c.c.length)) break;
					walk(c, sc, RAW.has(lname));
					out.push('</', name, '>');
				} else if (!c.c || !c.c.length) {
					out.push(html ? '></' + name + '>' : '/>');
				} else {
					out.push('>');
					walk(c, sc, false);
					out.push('</', name, '>');
				}
				break;
			}
			}
		}
	};
	walk(root, new Map([['', ''], ['xml', NS_XML]]), false);
	return out.join('');
}

/* ---- the result into DOM nodes (transformToFragment) ----------------------------------------- */

function buildInto(doc, parent, n, htmlDoc) {
	for (const c of n.c || []) {
		let d = null;
		switch (c.t) {
		case TEXT: d = doc.createTextNode(c.v); break;
		case COMMENT: d = doc.createComment(c.v); break;
		case PI: try { d = doc.createProcessingInstruction(c.ln, c.v); } catch (e) { d = null; } break;
		case ELEMENT: {
			if (!c.ns && htmlDoc) d = doc.createElement(c.ln);
			else d = doc.createElementNS(c.ns, c.qn);
			if (c.a) for (const a of c.a) {
				if (a.ns) d.setAttributeNS(a.ns, a.px ? a.px + ':' + a.ln : a.ln, a.v);
				else d.setAttribute(a.ln, a.v);
			}
			buildInto(doc, d, c, htmlDoc);
			break;
		}
		}
		if (d) parent.appendChild(d);
	}
}

/* ---- the library ----------------------------------------------------------------------------- */

function paramKey(ns, local) { return (ns ? '{' + ns + '}' : '') + local; }

/** a JS value given to setParameter -> an XPath value */
function paramValue(v) {
	if (typeof v === 'number' || typeof v === 'boolean' || typeof v === 'string') return v;
	if (v && N.type(v)) {
		const n = snapshot(v, v).found;
		return n ? [n] : [];
	}
	if (v && typeof v.length === 'number') {
		const out = [];
		for (const x of Array.from(v)) {
			if (x && N.type(x)) { const n = snapshot(x, x).found; if (n) out.push(n); }
		}
		return docOrder(out);
	}
	return String(v);
}

/**
 * transform(stylesheet, sourceDomNode, params) -> {method, out (the result tree), ss}
 */
function transform(ss, source, params) {
	const snap = snapshot(source, source);
	const node = snap.found || snap.root;
	stripSource(ss, snap.root);
	const p = new Map();
	if (params) for (const [k, v] of params) p.set(k, paramValue(v));
	const t = new Transform(ss, p);
	const out = t.run(node);
	return { method: methodOf(ss, out), out, ss };
}

return {
	compile,
	evaluate,
	compileStylesheet,
	transform,
	serialize: r => serialize(r.out.root, r.method, r.ss.output),
	paramKey,
	/* transformToFragment: the result's nodes in doc's fragment */
	toFragment(r, doc) {
		const frag = doc.createDocumentFragment();
		const htmlDoc = N.type(doc) === 9 && N.docKind(doc) === 0;
		if (r.method === 'text') { frag.appendChild(doc.createTextNode(textOf(r.out.root))); return frag; }
		buildInto(doc, frag, r.out.root, htmlDoc || r.method === 'html');
		return frag;
	},
	/* transformToDocument: the result serialized and parsed (html: as HTML) */
	toDocument(r) {
		if (r.method === 'html')
			return N.parseDocument(serialize(r.out.root, 'html', r.ss.output));
		if (r.method === 'text') {
			const t = textOf(r.out.root).replace(/&/g, '&amp;').replace(/</g, '&lt;');
			return N.parseDocument('<html><head></head><body><pre>' + t + '</pre></body></html>');
		}
		const d = N.parseXML(serialize(r.out.root, 'xml', Object.assign({}, r.ss.output, { omitDecl: true })), 1);
		const root = d && d.documentElement;
		if (root && root.namespaceURI === NS_HTML) N.setDocKind(d, 2);
		return d;
	},
	/* the top-level transform (html.c via js_xslt_transform): [method, text] */
	transformText(ss, source, params) {
		const r = transform(ss, source, params);
		return [r.method, serialize(r.out.root, r.method, r.ss.output)];
	},
};
})
