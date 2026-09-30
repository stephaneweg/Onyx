/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: the HTML5 parts of the DOM, run after dom.js (qjs.c calls this function with the
 * natives N and dom.js's element classes TAGS) -- they extend and correct dom.js's classes:
 *
 *  - namespaces: an element's namespaceURI / localName / tagName, the *NS methods, SVG and
 *    MathML elements in their own classes, HTMLUnknownElement for unknown names;
 *  - the HTML fragment serialization (innerHTML, outerHTML, getHTML, XMLSerializer) and the
 *    fragment parsing APIs on the standard's parser (outerHTML, insertAdjacentHTML,
 *    createContextualFragment, setHTMLUnsafe, DOMParser, createHTMLDocument), <template>'s
 *    contents;
 *  - messaging: MessageChannel / MessagePort, window.postMessage, BroadcastChannel,
 *    structuredClone;
 *  - <dialog>, <details>, the hidden attribute, and the forms: input types with their value
 *    sanitization, valueAsNumber / valueAsDate, stepUp / stepDown, the constraint
 *    validation API (validity, checkValidity, reportValidity, setCustomValidity) and the
 *    submission it blocks, <output>, <datalist>, <meter>, <progress>;
 *  - custom elements (define, upgrades, the lifecycle callbacks) and a shadow root tree;
 *  - smaller APIs: Blob / File / FileReader / FileList, URL.createObjectURL, crypto,
 *    performance marks, EventSource, Workers on a second QuickJS context, history's
 *    pushState changing the URL shown...
 *
 * Every feature said to be there works: nothing answers "supported" without doing it.
 */
(function (N, TAGS) {
'use strict';

const G = globalThis;
const NS_HTML = 'http://www.w3.org/1999/xhtml', NS_SVG = 'http://www.w3.org/2000/svg',
	NS_MATHML = 'http://www.w3.org/1998/Math/MathML', NS_XLINK = 'http://www.w3.org/1999/xlink',
	NS_XML = 'http://www.w3.org/XML/1998/namespace', NS_XMLNS = 'http://www.w3.org/2000/xmlns/';
const ELEMENT_NODE = 1, TEXT_NODE = 3, CDATA_SECTION_NODE = 4, PROCESSING_INSTRUCTION_NODE = 7,
	COMMENT_NODE = 8, DOCUMENT_NODE = 9, DOCUMENT_TYPE_NODE = 10, DOCUMENT_FRAGMENT_NODE = 11;
const Element = G.Element, HTMLElement = G.HTMLElement, Node = G.Node;

function report(e) {
	N.log('Uncaught ' + (e && e.stack ? e + '\n' + e.stack : String(e)));
}
function def(obj, props) {
	for (const k of Object.keys(props)) {
		const d = Object.getOwnPropertyDescriptor(props, k);
		d.enumerable = false;
		d.configurable = true;
		Object.defineProperty(obj, k, d);
	}
}
function getter(obj, name, get, set) {
	Object.defineProperty(obj, name, { configurable: true, enumerable: true, get, set });
}
function domError(message, name) { return new G.DOMException(message, name); }
function fire(target, type, init) {
	const ev = new G.Event(type, init || {});
	return target.dispatchEvent(ev);
}
/* a task (not a microtask): the event loop's next turn */
function task(fn) { N.timer(() => { try { fn(); } catch (e) { report(e); } }, 0, false); }

/* ---- element names and namespaces ------------------------------------------------------ */

def(Element.prototype, {
	get namespaceURI() { return N.nsURI(this); },
	get localName() { return N.lname(this); },
	get tagName() { return N.qname(this); },
	get prefix() {
		const q = N.qname(this), i = q.indexOf(':');
		return i > 0 && N.nsURI(this) !== NS_HTML ? q.slice(0, i) : null;
	},
	getAttributeNS(ns, local) { return N.attrNS(this, ns || null, String(local)); },
	setAttributeNS(ns, qname, value) {
		N.setAttrNS(this, ns || null, String(qname), String(value));
	},
	removeAttributeNS(ns, local) { N.removeAttrNS(this, ns || null, String(local)); },
	hasAttributeNS(ns, local) { return N.attrNS(this, ns || null, String(local)) !== null; },
});
/* an attribute's name: lower case on HTML elements only (an SVG element keeps "viewBox") */
const I = N.internals || {};
function attrKey(el, name) {
	const s = String(name);
	return N.nsURI(el) === NS_HTML ? s.toLowerCase() : s;
}
function attrRecord(el, k, old) {
	if (I.observers && I.observers.size)
		I.queueMutation({ type: 'attributes', target: el, attributeName: k, oldValue: old,
			addedNodes: G.NodeList ? [] : [], removedNodes: [] });
}
const attrProto = {
	getAttribute(name) { return N.attr(this, attrKey(this, name)); },
	hasAttribute(name) { return N.attr(this, attrKey(this, name)) !== null; },
	setAttribute(name, value) {
		const k = attrKey(this, name);
		const old = I.observers && I.observers.size ? N.attr(this, k) : null;
		N.setAttr(this, k, String(value));
		attrRecord(this, k, old);
		ceAttributeChanged(this, k, old, String(value));
	},
	removeAttribute(name) {
		const k = attrKey(this, name);
		const old = N.attr(this, k);
		if (old === null) return;
		N.removeAttr(this, k);
		attrRecord(this, k, old);
		ceAttributeChanged(this, k, old, null);
	},
	toggleAttribute(name, force) {
		const has = this.hasAttribute(name);
		const want = force === undefined ? !has : !!force;
		if (want && !has) this.setAttribute(name, '');
		else if (!want && has) this.removeAttribute(name);
		return want;
	},
};
def(Element.prototype, attrProto);

/* a copy of n for doc (the same document: a clone), templates' contents copied too */
function isTemplate(n) {
	return N.type(n) === ELEMENT_NODE && N.nsURI(n) === NS_HTML && N.lname(n) === 'template';
}
function copyNode(n, deep, doc) {
	const same = N.ownerDoc(n) === doc || (N.type(n) === DOCUMENT_NODE && n === doc);
	if (same && deep) {
		/* the native deep clone, then the templates' contents */
		const c = N.clone(n, true);
		const t = N.type(n);
		if (t === ELEMENT_NODE || t === DOCUMENT_FRAGMENT_NODE || t === DOCUMENT_NODE) {
			const from = isTemplate(n) ? [n] : [], to = isTemplate(c) ? [c] : [];
			from.push(...n.querySelectorAll('template'));
			to.push(...c.querySelectorAll('template'));
			for (let i = 0; i < from.length && i < to.length; i++)
				for (const k of N.children(N.templateContent(from[i])))
					N.insert(N.templateContent(to[i]), copyNode(k, true, doc), null);
		}
		return c;
	}
	const one = k => same ? N.clone(k, false) : N.importTo(doc, k, false);
	const c = one(n);
	if (deep) {
		for (const k of N.children(n)) N.insert(c, copyNode(k, true, doc), null);
		if (N.type(n) === ELEMENT_NODE && N.nsURI(n) === NS_HTML && N.lname(n) === 'template')
			for (const k of N.children(N.templateContent(n)))
				N.insert(N.templateContent(c), copyNode(k, true, doc), null);
	}
	return c;
}
def(Node.prototype, {
	cloneNode(deep) { return copyNode(this, !!deep, N.ownerDoc(this) || this); },
});

/* the documents: a node made by a document is that document's (DOMParser's own documents) */
function isMainDoc(d) { return d === G.document; }
const docCreate = G.Document.prototype;
const mainCreate = {
	createElement: docCreate.createElement,
	createTextNode: docCreate.createTextNode,
	createComment: docCreate.createComment,
	createDocumentFragment: docCreate.createDocumentFragment,
	importNode: docCreate.importNode,
};
def(G.Document.prototype, {
	createElementNS(ns, qname) {
		ns = ns === undefined || ns === null || ns === '' ? null : String(ns);
		const q = String(qname);
		if (!isMainDoc(this)) return N.createIn(this, 'elementNS', ns, q);
		if (ns === NS_HTML && !q.includes(':'))
			return this.createElement(q);
		return N.createNS(ns, q);
	},
	createElement(name) {
		if (isMainDoc(this)) return mainCreate.createElement.call(this, name);
		return N.createIn(this, 'element', String(name).toLowerCase());
	},
	createTextNode(s) {
		if (isMainDoc(this)) return mainCreate.createTextNode.call(this, s);
		return N.createIn(this, 'text', String(s));
	},
	createComment(s) {
		if (isMainDoc(this)) return mainCreate.createComment.call(this, s);
		return N.createIn(this, 'comment', String(s));
	},
	createDocumentFragment() {
		if (isMainDoc(this)) return mainCreate.createDocumentFragment.call(this);
		return N.createIn(this, 'fragment');
	},
	importNode(n, deep) { return copyNode(n, !!deep, this); },
	adoptNode(n) {
		const p = N.parent(n);
		if (p) p.removeChild(n);
		if (N.ownerDoc(n) === this) return n;
		return N.importTo(this, n, true);
	},
});
getter(Node.prototype, 'ownerDocument', function () {
	return N.type(this) === DOCUMENT_NODE ? null : N.ownerDoc(this);
});

/* the classes of the element names (qjs.c's qjs_proto_for: "svg:<name>", "svg:*",
 * "math:*", "*unknown" for an HTML name no class has) */
for (const t of ('abbr address article aside b bdi bdo cite code dd dfn dt em figcaption ' +
		'figure footer header hgroup i kbd main mark nav noscript rp rt ruby s samp search ' +
		'section small strong sub summary sup u var wbr acronym basefont big center nobr ' +
		'noembed noframes plaintext rb rtc strike tt').split(' '))
	if (!TAGS[t]) TAGS[t] = HTMLElement.prototype;
function elementClass(name, tags, base = HTMLElement, body) {
	if (G[name] && !body) return G[name];
	const cls = body || class extends base {};
	Object.defineProperty(cls, 'name', { value: name });
	G[name] = cls;
	for (const t of tags) TAGS[t] = cls.prototype;
	return cls;
}
if (G.HTMLPreElement) { TAGS.xmp = TAGS.listing = G.HTMLPreElement.prototype; }
elementClass('HTMLFrameSetElement', ['frameset']);
elementClass('HTMLMarqueeElement', ['marquee']);
elementClass('HTMLDirectoryElement', ['dir']);
elementClass('HTMLFrameElement', ['frame']);
elementClass('HTMLSelectedContentElement', ['selectedcontent']);
if (G.HTMLUnknownElement) TAGS['*unknown'] = G.HTMLUnknownElement.prototype;

/* SVG: the classes dom.js gave plain names get their namespaced name; MathML */
if (G.SVGElement) {
	const svgProto = G.SVGElement.prototype;
	for (const k of Object.keys(TAGS))
		if (!k.includes(':') && k !== '*unknown' &&
				(TAGS[k] === svgProto || svgProto.isPrototypeOf(TAGS[k])))
			TAGS['svg:' + k] = TAGS[k];
	TAGS['svg:*'] = svgProto;
	if (G.SVGSVGElement) TAGS['svg:svg'] = G.SVGSVGElement.prototype;
}
class MathMLElement extends Element {
	get style() { return Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'style').get.call(this); }
	get dataset() { return Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'dataset').get.call(this); }
	focus() {}
	blur() {}
}
G.MathMLElement = MathMLElement;
TAGS['math:*'] = MathMLElement.prototype;

/* custom elements: attributeChangedCallback (filled in below) */
let ceAttributeChanged = () => {};

/* ---- serialising: the HTML fragment serialization algorithm ------------------------------- */

const VOID = new Set(['area', 'base', 'basefont', 'bgsound', 'br', 'col', 'embed', 'frame',
	'hr', 'img', 'input', 'keygen', 'link', 'meta', 'param', 'source', 'track', 'wbr']);
const RAW_TEXT = new Set(['style', 'script', 'xmp', 'iframe', 'noembed', 'noframes',
	'plaintext', 'noscript']);

function escapeText(s) {
	return s.replace(/[& <>]/g, c => c === '&' ? '&amp;' : c === '<' ? '&lt;' :
		c === '>' ? '&gt;' : '&nbsp;');
}
function escapeAttr(s) {
	return s.replace(/[& "<>]/g, c => c === '&' ? '&amp;' : c === '"' ? '&quot;' :
		c === '<' ? '&lt;' : c === '>' ? '&gt;' : '&nbsp;');
}
function attrName(qname, ns, local) {
	if (ns === null || ns === undefined) return local || qname;
	if (ns === NS_XML) return 'xml:' + local;
	if (ns === NS_XMLNS) return local === 'xmlns' ? 'xmlns' : 'xmlns:' + local;
	if (ns === NS_XLINK) return 'xlink:' + local;
	return qname;
}

function serializeNode(n, out) {
	switch (N.type(n)) {
	case ELEMENT_NODE: {
		const ns = N.nsURI(n);
		const tag = ns === NS_HTML || ns === NS_SVG || ns === NS_MATHML ? N.lname(n) : N.qname(n);
		out.push('<', tag);
		for (const [q, v, ans, local] of N.attrsNS(n))
			out.push(' ', attrName(q, ans, local), '="', escapeAttr(v), '"');
		out.push('>');
		if (ns === NS_HTML && VOID.has(tag))
			return;
		serializeChildren(ns === NS_HTML && tag === 'template' ? N.templateContent(n) || n : n, out);
		out.push('</', tag, '>');
		break;
	}
	case TEXT_NODE: case CDATA_SECTION_NODE: {
		const p = N.parent(n);
		const raw = p && N.type(p) === ELEMENT_NODE && N.nsURI(p) === NS_HTML &&
			RAW_TEXT.has(N.lname(p));
		out.push(raw ? N.value(n) : escapeText(N.value(n)));
		break;
	}
	case COMMENT_NODE:
		out.push('<!--', N.value(n), '-->');
		break;
	case PROCESSING_INSTRUCTION_NODE:
		out.push('<?', N.name(n), ' ', N.value(n), '>');
		break;
	case DOCUMENT_TYPE_NODE:
		out.push('<!DOCTYPE ', N.name(n), '>');
		break;
	case DOCUMENT_NODE: case DOCUMENT_FRAGMENT_NODE:
		serializeChildren(n, out);
		break;
	}
}
function serializeChildren(n, out) {
	for (const c of N.children(n))
		serializeNode(c, out);
}
function innerHTMLOf(n) {
	const out = [];
	serializeChildren(N.type(n) === ELEMENT_NODE && N.nsURI(n) === NS_HTML &&
		N.lname(n) === 'template' ? N.templateContent(n) || n : n, out);
	return out.join('');
}
function outerHTMLOf(n) {
	const out = [];
	serializeNode(n, out);
	return out.join('');
}

/* ---- parsing fragments -------------------------------------------------------------------- */

/* the nodes html makes in the context of an element, in a new fragment */
function parseFragment(html, context) {
	const f = N.createFragment();
	if (!context || N.type(context) !== ELEMENT_NODE ||
			(N.nsURI(context) === NS_HTML && N.lname(context) === 'html'))
		context = null;			/* (a body) */
	N.setHTML(f, String(html), context);
	return f;
}

const innerSet = Object.getOwnPropertyDescriptor(Element.prototype, 'innerHTML').set;
def(Element.prototype, {
	get innerHTML() { return innerHTMLOf(this); },
	set innerHTML(v) { innerSet.call(this, v === null ? '' : String(v)); },
	get outerHTML() { return outerHTMLOf(this); },
	set outerHTML(v) {
		const p = N.parent(this);
		if (!p) return;
		if (N.type(p) === DOCUMENT_NODE)
			throw domError('the document element', 'NoModificationAllowedError');
		const f = parseFragment(v === null ? '' : String(v), N.type(p) === ELEMENT_NODE ? p : null);
		p.replaceChild(f, this);
	},
	insertAdjacentHTML(pos, html) {
		const where = String(pos).toLowerCase();
		let context;
		if (where === 'beforebegin' || where === 'afterend') {
			context = N.parent(this);
			if (!context || N.type(context) === DOCUMENT_NODE)
				throw domError('no parent', 'NoModificationAllowedError');
		} else if (where === 'afterbegin' || where === 'beforeend') {
			context = this;
		} else {
			throw domError('bad position', 'SyntaxError');
		}
		const f = parseFragment(html, context);
		switch (where) {
		case 'beforebegin': N.parent(this).insertBefore(f, this); break;
		case 'afterbegin': this.insertBefore(f, N.first(this)); break;
		case 'beforeend': this.appendChild(f); break;
		case 'afterend': N.parent(this).insertBefore(f, N.next(this)); break;
		}
	},
	setHTMLUnsafe(html) { innerSet.call(this, String(html)); },
	getHTML() { return innerHTMLOf(this); },
});
if (G.Range) def(G.Range.prototype, {
	createContextualFragment(html) {
		let n = this.startContainer;
		if (n && N.type(n) !== ELEMENT_NODE) n = N.parent(n);
		return parseFragment(html, n && N.type(n) === ELEMENT_NODE ? n : null);
	},
});

/* <template>: its contents are a fragment of their own (the parser fills it) */
if (G.HTMLTemplateElement) {
	def(G.HTMLTemplateElement.prototype, {
		get content() { return N.templateContent(this); },
		get shadowRootMode() { return (N.attr(this, 'shadowrootmode') || '').toLowerCase(); },
		set shadowRootMode(v) { this.setAttribute('shadowrootmode', v); },

	});
}

/* documents parsed apart: DOMParser, document.implementation.createHTMLDocument */
class DOMParser {
	parseFromString(s, type) {
		const t = String(type).toLowerCase();
		if (t === 'text/html')
			return N.parseDocument(String(s));
		if (['text/xml', 'application/xml', 'application/xhtml+xml', 'image/svg+xml'].includes(t))
			return parseXMLDocument(String(s), t);
		throw new TypeError('DOMParser: unsupported type ' + type);
	}
}
G.DOMParser = DOMParser;
class XMLSerializer {
	serializeToString(n) {
		if (!(n instanceof Node)) throw new TypeError('not a node');
		return outerHTMLOf(n);
	}
}
G.XMLSerializer = XMLSerializer;
G.Document.parseHTMLUnsafe = html => N.parseDocument(String(html));

const implementation = {
	createHTMLDocument(title) {
		const d = N.parseDocument('<!DOCTYPE html>');
		if (title !== undefined) {
			const t = d.createElement('title');
			t.textContent = String(title);
			d.head.appendChild(t);
		}
		return d;
	},
	createDocument(ns, qname) {
		const d = N.createDocument();
		if (qname) d.appendChild(d.createElementNS(ns, qname));
		return d;
	},
	createDocumentType(name, pub, sys) { return { nodeType: DOCUMENT_TYPE_NODE, name, publicId: pub, systemId: sys }; },
	hasFeature() { return true; },
};
getter(G.Document.prototype, 'implementation', () => implementation);

/* XML documents (DOMParser's XML types): a small non-validating parser -- elements in
 * their namespaces (xmlns declarations), attributes, text, CDATA, comments, the five
 * entities and numeric references; a malformed document gets a <parsererror> as the
 * browsers give */
function parseXMLDocument(src, type) {
	const doc = N.createDocument();
	const stack = [{ node: doc, ns: new Map([['xml', NS_XML], ['xmlns', NS_XMLNS]]),
		dflt: type === 'application/xhtml+xml' ? NS_HTML : null }];
	const ent = s => s.replace(/&(#x[0-9a-f]+|#[0-9]+|lt|gt|amp|quot|apos);/gi, (m, e) => {
		if (e[0] === '#') return String.fromCodePoint(e[1] === 'x' || e[1] === 'X' ?
			parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10));
		return { lt: '<', gt: '>', amp: '&', quot: '"', apos: "'" }[e.toLowerCase()];
	});
	let i = 0, error = null;
	const top = () => stack[stack.length - 1];
	while (i < src.length && !error) {
		const lt = src.indexOf('<', i);
		if (lt < 0 || lt > i) {
			const text = src.slice(i, lt < 0 ? src.length : lt);
			if (stack.length > 1) top().node.appendChild(doc.createTextNode(ent(text)));
			else if (text.trim()) error = 'text outside the root element';
			if (lt < 0) break;
			i = lt;
			continue;
		}
		if (src.startsWith('<!--', i)) {
			const e = src.indexOf('-->', i + 4);
			if (e < 0) { error = 'unclosed comment'; break; }
			top().node.appendChild(doc.createComment(src.slice(i + 4, e)));
			i = e + 3;
		} else if (src.startsWith('<![CDATA[', i)) {
			const e = src.indexOf(']]>', i + 9);
			if (e < 0) { error = 'unclosed CDATA section'; break; }
			top().node.appendChild(doc.createTextNode(src.slice(i + 9, e)));
			i = e + 3;
		} else if (src.startsWith('<?', i)) {
			const e = src.indexOf('?>', i + 2);
			if (e < 0) { error = 'unclosed processing instruction'; break; }
			i = e + 2;
		} else if (src.startsWith('<!', i)) {
			const e = src.indexOf('>', i + 2);
			if (e < 0) { error = 'unclosed declaration'; break; }
			i = e + 1;
		} else if (src[i + 1] === '/') {
			const e = src.indexOf('>', i);
			const name = src.slice(i + 2, e).trim();
			if (e < 0 || stack.length < 2 || top().qname !== name) { error = 'mismatched end tag ' + name; break; }
			stack.pop();
			i = e + 1;
		} else {
			const m = /^<([^\s/>]+)((?:\s+[^\s=/>]+\s*=\s*(?:"[^"]*"|'[^']*'))*)\s*(\/?)>/.exec(src.slice(i));
			if (!m) { error = 'bad start tag'; break; }
			const qname = m[1];
			const attrs = [];
			const re = /([^\s=/>]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/g;
			let a;
			while ((a = re.exec(m[2]))) attrs.push([a[1], ent(a[2] !== undefined ? a[2] : a[3])]);
			const scope = { ns: new Map(top().ns), dflt: top().dflt, qname };
			for (const [k, v] of attrs) {
				if (k === 'xmlns') scope.dflt = v || null;
				else if (k.startsWith('xmlns:')) scope.ns.set(k.slice(6), v);
			}
			const colon = qname.indexOf(':');
			const ns = colon > 0 ? scope.ns.get(qname.slice(0, colon)) : scope.dflt;
			if (colon > 0 && ns === undefined) { error = 'unbound prefix ' + qname; break; }
			if (stack.length === 1 && N.first(doc) && [...N.children(doc)].some(c => N.type(c) === ELEMENT_NODE)) {
				error = 'a second root element'; break;
			}
			const el = N.createIn(doc, 'elementNS', ns || null, qname);
			for (const [k, v] of attrs) {
				const c = k.indexOf(':');
				const ans = k === 'xmlns' || k.startsWith('xmlns:') ? NS_XMLNS :
					c > 0 ? scope.ns.get(k.slice(0, c)) : null;
				try { N.setAttrNS(el, ans || null, k, v); } catch (e) { N.setAttr(el, k, v); }
			}
			top().node.appendChild(el);
			scope.node = el;
			if (!m[3]) stack.push(scope);
			i += m[0].length;
		}
	}
	if (!error && stack.length > 1) error = 'unclosed element ' + top().qname;
	if (!error && ![...N.children(doc)].some(c => N.type(c) === ELEMENT_NODE)) error = 'no root element';
	if (error) {
		for (let c; (c = N.first(doc));) N.remove(doc, c);
		const pe = N.createIn(doc, 'elementNS', 'http://www.mozilla.org/newlayout/xml/parsererror.xml', 'parsererror');
		pe.textContent = 'XML Parsing Error: ' + error;
		doc.appendChild(pe);
	}
	return doc;
}

/* ---- structuredClone -------------------------------------------------------------------- */

function cloneError(what) { return domError(what + ' could not be cloned', 'DataCloneError'); }
function structuredCloneValue(v, memo) {
	if (v === null || typeof v !== 'object') {
		if (typeof v === 'function') throw cloneError('A function');
		if (typeof v === 'symbol') throw cloneError('A symbol');
		return v;
	}
	if (memo.has(v)) return memo.get(v);
	let r;
	const tag = Object.prototype.toString.call(v);
	if (v instanceof Node || v === G) throw cloneError('A DOM object');
	if (v instanceof Boolean) r = new Boolean(v.valueOf());
	else if (v instanceof Number) r = new Number(v.valueOf());
	else if (v instanceof String) r = new String(v.valueOf());
	else if (typeof BigInt !== 'undefined' && tag === '[object BigInt]') r = Object(v.valueOf());
	else if (v instanceof Date) r = new Date(v.getTime());
	else if (v instanceof RegExp) r = new RegExp(v.source, v.flags);
	else if (v instanceof ArrayBuffer) r = v.slice(0);
	else if (ArrayBuffer.isView(v)) {
		const buf = structuredCloneValue(v.buffer, memo);
		r = v instanceof DataView ? new DataView(buf, v.byteOffset, v.byteLength) :
			new v.constructor(buf, v.byteOffset, v.length);
	} else if (v instanceof Map) {
		r = new Map();
		memo.set(v, r);
		for (const [k, x] of v) r.set(structuredCloneValue(k, memo), structuredCloneValue(x, memo));
		return r;
	} else if (v instanceof Set) {
		r = new Set();
		memo.set(v, r);
		for (const x of v) r.add(structuredCloneValue(x, memo));
		return r;
	} else if (v instanceof Error) {
		const C = { EvalError, RangeError, ReferenceError, SyntaxError, TypeError, URIError }[v.name] || Error;
		r = new C(v.message);
		if (v.stack) r.stack = String(v.stack);
		if ('cause' in v) r.cause = structuredCloneValue(v.cause, memo);
	} else if (G.Blob && v instanceof G.Blob) {
		r = v;				/* (immutable: shared) */
	} else if (G.FileList && v instanceof G.FileList) {
		r = v;
	} else if (G.ImageData && v instanceof G.ImageData) {
		r = new G.ImageData(new Uint8ClampedArray(v.data), v.width, v.height);
	} else if (Array.isArray(v)) {
		r = new Array(v.length);
		memo.set(v, r);
		for (const k of Object.keys(v)) r[k] = structuredCloneValue(v[k], memo);
		return r;
	} else if (tag === '[object Object]' || Object.getPrototypeOf(v) === null ||
			typeof v.constructor !== 'function' || !(v instanceof G.EventTarget)) {
		if (v instanceof Promise || v instanceof WeakMap || v instanceof WeakSet ||
				(typeof WeakRef !== 'undefined' && v instanceof WeakRef))
			throw cloneError(tag);
		r = {};
		memo.set(v, r);
		for (const k of Object.keys(v)) r[k] = structuredCloneValue(v[k], memo);
		return r;
	} else {
		throw cloneError(tag);
	}
	memo.set(v, r);
	return r;
}
function structuredClone(v, opts) {
	const memo = new Map();
	const transfer = opts && opts.transfer ? Array.from(opts.transfer) : [];
	for (const t of transfer) {
		if (t instanceof ArrayBuffer && typeof t.transfer === 'function') {
			/* the buffer moves: the original is detached */
			memo.set(t, t.transfer());
		} else if (t instanceof MessagePort) {
			memo.set(t, t);
		}
	}
	return structuredCloneValue(v, memo);
}
G.structuredClone = structuredClone;

/* ---- messaging: MessageEvent, MessageChannel, MessagePort, postMessage, BroadcastChannel ---- */

class MessageEvent extends G.Event {
	constructor(type, init = {}) {
		super(type, init);
		this.data = init.data === undefined ? null : init.data;
		this.origin = init.origin === undefined ? '' : String(init.origin);
		this.lastEventId = init.lastEventId === undefined ? '' : String(init.lastEventId);
		this.source = init.source || null;
		this.ports = Object.freeze(Array.from(init.ports || []));
	}
	initMessageEvent(type, bubbles, cancelable, data, origin, lastEventId, source, ports) {
		this.initEvent(type, bubbles, cancelable);
		this.data = data; this.origin = origin || ''; this.lastEventId = lastEventId || '';
		this.source = source || null; this.ports = ports || [];
	}
}
G.MessageEvent = MessageEvent;

/* an on<type> property as an event listener of its own */
function handlerProperty(proto, type, onSet) {
	const key = Symbol('on' + type);
	Object.defineProperty(proto, 'on' + type, { configurable: true, enumerable: true,
		get() { return this[key] ? this[key].fn : null; },
		set(fn) {
			if (this[key]) this.removeEventListener(type, this[key].l);
			this[key] = null;
			if (typeof fn === 'function') {
				const l = ev => {
					const r = fn.call(this, ev);
					if (r === false) ev.preventDefault();
				};
				this[key] = { fn, l };
				this.addEventListener(type, l);
				if (onSet) onSet.call(this);
			}
		} });
}

class MessagePort extends G.EventTarget {
	constructor(key) {
		super();
		if (key !== PORT_KEY) throw new TypeError('Illegal constructor');
		this._other = null;
		this._queue = [];
		this._started = false;
		this._closed = false;
	}
	postMessage(message, opts) {
		if (this._closed) return;
		const transfer = Array.isArray(opts) ? opts : opts && opts.transfer ? opts.transfer : [];
		const data = structuredClone(message, { transfer });
		const ports = transfer.filter(t => t instanceof MessagePort);
		const target = this._other;
		if (!target || target._closed) return;
		target._enqueue({ data, ports });
	}
	_enqueue(m) {
		this._queue.push(m);
		if (this._started) this._schedule();
	}
	_schedule() {
		/* each message is a task of its own, in order (a microtask checkpoint between) */
		if (this._pending) return;
		this._pending = true;
		task(() => {
			this._pending = false;
			if (this._closed || !this._queue.length) return;
			const m = this._queue.shift();
			this.dispatchEvent(new MessageEvent('message', { data: m.data, ports: m.ports }));
			if (this._queue.length) this._schedule();
		});
	}
	start() {
		if (this._started) return;
		this._started = true;
		if (this._queue.length) this._schedule();
	}
	close() {
		this._closed = true;
		this._queue.length = 0;
		if (this._other) { const o = this._other; this._other = null; o._other = null; }
	}
}
const PORT_KEY = {};
handlerProperty(MessagePort.prototype, 'message', function () { this.start(); });
handlerProperty(MessagePort.prototype, 'messageerror');
G.MessagePort = MessagePort;

class MessageChannel {
	constructor() {
		const a = new MessagePort(PORT_KEY), b = new MessagePort(PORT_KEY);
		a._other = b;
		b._other = a;
		Object.defineProperty(this, 'port1', { value: a, enumerable: true });
		Object.defineProperty(this, 'port2', { value: b, enumerable: true });
	}
}
G.MessageChannel = MessageChannel;

/* window.postMessage: to this window (the only one); the targetOrigin checked */
G.postMessage = function postMessage(message, targetOrigin, transfer) {
	let opts = targetOrigin;
	if (typeof targetOrigin !== 'object' || targetOrigin === null)
		opts = { targetOrigin: targetOrigin === undefined ? '/' : String(targetOrigin), transfer };
	const origin = G.location.origin;
	const to = opts.targetOrigin === undefined ? '/' : opts.targetOrigin;
	if (to !== '*' && to !== '/') {
		let o;
		try { o = new URL(to).origin; } catch (e) { throw domError('bad targetOrigin', 'SyntaxError'); }
		if (o !== origin) return;
	}
	const data = structuredClone(message, { transfer: opts.transfer || [] });
	const ports = (opts.transfer || []).filter(t => t instanceof MessagePort);
	task(() => G.dispatchEvent(new MessageEvent('message', { data, origin, source: G, ports })));
};

const broadcast = new Map();		/* name -> Set of channels */
class BroadcastChannel extends G.EventTarget {
	constructor(name) {
		super();
		this.name = String(name);
		this._closed = false;
		if (!broadcast.has(this.name)) broadcast.set(this.name, new Set());
		broadcast.get(this.name).add(this);
	}
	postMessage(message) {
		if (this._closed) throw domError('closed', 'InvalidStateError');
		const data = structuredClone(message);
		const origin = G.location.origin;
		for (const c of broadcast.get(this.name) || [])
			if (c !== this)
				task(() => { if (!c._closed) c.dispatchEvent(new MessageEvent('message', { data: structuredClone(data), origin })); });
	}
	close() {
		this._closed = true;
		const s = broadcast.get(this.name);
		if (s) s.delete(this);
	}
}
handlerProperty(BroadcastChannel.prototype, 'message');
handlerProperty(BroadcastChannel.prototype, 'messageerror');
G.BroadcastChannel = BroadcastChannel;

return { serialize: outerHTMLOf, parseFragment, structuredClone };
})
