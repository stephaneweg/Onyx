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
 *  - custom elements (define, upgrades, the lifecycle callbacks, ElementInternals);
 *  - shadow DOM (attachShadow, ShadowRoot, slots, declarative shadow roots, the events'
 *    path and retargeting across the shadow trees);
 *  - smaller APIs: the session history (pushState changing the URL shown), streams,
 *    Blob / File / FileReader / FileList, URL.createObjectURL, microdata, performance
 *    marks and PerformanceObserver, XMLHttpRequest's documents...
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
	get localName() {
		/* (Onyx: a namespaced element's name less its prefix) */
		const q = N.lname(this), i = q.indexOf(':');
		return i > 0 && N.nsURI(this) !== NS_HTML ? q.slice(i + 1) : q;
	},
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
		const old = (I.observers && I.observers.size) || ceDefs.size ? N.attr(this, k) : null;
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

/* Onyx: the names an element may have (XML's Name, QName: createElement('<div>') throws an
 * InvalidCharacterError, createElementNS('x', 'xml:a') a NamespaceError -- as browsers) */
const NAME_START = 'A-Za-z_:\u00C0-\u00D6\u00D8-\u00F6\u00F8-\u02FF\u0370-\u037D' +
	'\u037F-\u1FFF\u200C-\u200D\u2070-\u218F\u2C00-\u2FEF\u3001-\uD7FF\uF900-\uFDCF' +
	'\uFDF0-\uFFFD';
const NAME_RE = new RegExp('^[' + NAME_START + '][' + NAME_START +
	'\\-.0-9\u00B7\u0300-\u036F\u203F-\u2040]*$');
const NS_XMLNS_URI = 'http://www.w3.org/2000/xmlns/', NS_XML_URI = 'http://www.w3.org/XML/1998/namespace';
function checkName(name) {
	if (!NAME_RE.test(name))
		throw domError('not a valid name: ' + name, 'InvalidCharacterError');
}
/* validate and extract (DOM): [namespace, prefix, local] or throws */
function checkQName(ns, q) {
	checkName(q);
	const i = q.indexOf(':');
	if (i === 0 || (i > 0 && (q.indexOf(':', i + 1) >= 0 || i === q.length - 1)))
		throw domError('not a valid qualified name: ' + q, 'NamespaceError');
	const prefix = i > 0 ? q.slice(0, i) : null;
	if (prefix !== null && !/^[^0-9.\-]/.test(q.slice(i + 1)))
		throw domError('not a valid qualified name: ' + q, 'NamespaceError');
	if (prefix !== null && ns === null)
		throw domError('a prefix without a namespace', 'NamespaceError');
	if (prefix === 'xml' && ns !== NS_XML_URI)
		throw domError('the xml prefix in another namespace', 'NamespaceError');
	if ((q === 'xmlns' || prefix === 'xmlns') !== (ns === NS_XMLNS_URI))
		throw domError('xmlns and its namespace', 'NamespaceError');
}

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
		checkQName(ns, q);	/* (Onyx) */
		if (!isMainDoc(this)) return N.createIn(this, 'elementNS', ns, q);
		if (ns === NS_HTML && !q.includes(':'))
			return this.createElement(q);
		return N.createNS(ns, q);
	},
	createElement(name) {
		checkName(String(name));	/* (Onyx) */
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
		if (N.type(n) === DOCUMENT_NODE) throw domError('adoptNode: a document', 'NotSupportedError');
		return N.adopt(this, n);	/* (Onyx: the same node, now this document's) */
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
TAGS['*custom'] = HTMLElement.prototype;	/* (a valid custom element name: HTMLElement) */

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
	createDocument(ns, qname, doctype) {
		/* (Onyx: with its doctype, which becomes the document's) */
		const d = N.createDocument(doctype || null);
		if (qname) d.appendChild(d.createElementNS(ns, qname));
		return d;
	},
	createDocumentType(name, pub, sys) {
		checkQName('', String(name));	/* (Onyx: "a:" is no qualified name) */
		/* (a DocumentType node, as browsers: it joins a document) */
		return N.createDoctype(String(name), String(pub), String(sys));
	},
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

/* ---- forms: input types, values, the constraint validation API ------------------------------ */

const INPUT_TYPES = ['hidden', 'text', 'search', 'tel', 'url', 'email', 'password', 'date',
	'month', 'week', 'time', 'datetime-local', 'number', 'range', 'color', 'checkbox', 'radio',
	'file', 'submit', 'image', 'reset', 'button'];
const TEXTLIKE = new Set(['text', 'search', 'tel', 'url', 'email', 'password']);
const DATELIKE = new Set(['date', 'month', 'week', 'time', 'datetime-local']);
const NUMERIC = new Set(['number', 'range', ...DATELIKE]);
const VALUE_MODE = new Set([...TEXTLIKE, ...NUMERIC, 'color']);
function inputType(el) {
	const t = (N.attr(el, 'type') || 'text').toLowerCase();
	return INPUT_TYPES.includes(t) ? t : 'text';
}

/* dates and times (the "valid ... string" microsyntaxes), all in UTC */
const pad = (n, w = 2) => String(n).padStart(w, '0');
function daysIn(y, m) { return new Date(Date.UTC(y, m, 0)).getUTCDate() || 31; }
function mkDate(y, m, d) { const t = new Date(0); t.setUTCFullYear(y, m - 1, d); return t.getTime(); }
function parseDateStr(s) {
	const m = /^(\d{4,})-(\d\d)-(\d\d)$/.exec(s);
	if (!m) return null;
	const y = +m[1], mo = +m[2], d = +m[3];
	if (y < 1 || mo < 1 || mo > 12 || d < 1 || d > daysIn(y, mo)) return null;
	return mkDate(y, mo, d);
}
function parseMonthStr(s) {
	const m = /^(\d{4,})-(\d\d)$/.exec(s);
	if (!m || +m[1] < 1 || +m[2] < 1 || +m[2] > 12) return null;
	return (+m[1] - 1970) * 12 + (+m[2] - 1);
}
function weeksIn(y) {
	const jan1 = new Date(mkDate(y, 1, 1)).getUTCDay();
	const leap = daysIn(y, 2) === 29;
	return jan1 === 4 || (leap && jan1 === 3) ? 53 : 52;
}
function weekStart(y, w) {
	/* the Monday of ISO week w of year y */
	const jan4 = mkDate(y, 1, 4), dow = (new Date(jan4).getUTCDay() + 6) % 7;
	return jan4 - dow * 86400000 + (w - 1) * 604800000;
}
function parseWeekStr(s) {
	const m = /^(\d{4,})-W(\d\d)$/.exec(s);
	if (!m || +m[1] < 1 || +m[2] < 1 || +m[2] > weeksIn(+m[1])) return null;
	return weekStart(+m[1], +m[2]);
}
function parseTimeStr(s) {
	const m = /^(\d\d):(\d\d)(?::(\d\d)(?:\.(\d{1,3}))?)?$/.exec(s);
	if (!m || +m[1] > 23 || +m[2] > 59 || (m[3] && +m[3] > 59)) return null;
	return ((+m[1] * 60 + +m[2]) * 60 + +(m[3] || 0)) * 1000 + +((m[4] || '0') + '00').slice(0, 3);
}
function parseLocalStr(s) {
	const m = /^(\d{4,}-\d\d-\d\d)[T ](.+)$/.exec(s);
	if (!m) return null;
	const d = parseDateStr(m[1]), t = parseTimeStr(m[2]);
	return d === null || t === null ? null : d + t;
}
function timeStr(ms) {
	ms = ((ms % 86400000) + 86400000) % 86400000;
	const h = Math.floor(ms / 3600000), mi = Math.floor(ms / 60000) % 60, sec = Math.floor(ms / 1000) % 60;
	const f = ms % 1000;
	let s = pad(h) + ':' + pad(mi);
	if (sec || f) s += ':' + pad(sec);
	if (f) s += '.' + pad(f, 3).replace(/0+$/, '');
	return s;
}
function yearStr(y) { return pad(y, 4); }
function dateStr(ms) {
	const d = new Date(ms);
	return yearStr(d.getUTCFullYear()) + '-' + pad(d.getUTCMonth() + 1) + '-' + pad(d.getUTCDate());
}
const TYPE_NUM = {
	date: { parse: parseDateStr, str: dateStr, scale: 86400000, step: 1, base: 0 },
	month: { parse: parseMonthStr, str: n => { const y = Math.floor(n / 12) + 1970, m = ((n % 12) + 12) % 12; return yearStr(y) + '-' + pad(m + 1); }, scale: 1, step: 1, base: 0 },
	week: { parse: parseWeekStr, str: ms => {
		/* the ISO week of the day */
		const d = new Date(ms), dow = (d.getUTCDay() + 6) % 7;
		const thu = new Date(ms - dow * 86400000 + 3 * 86400000);
		const y = thu.getUTCFullYear();
		const w = Math.floor((thu.getTime() - weekStart(y, 1)) / 604800000) + 1;
		return yearStr(y) + '-W' + pad(w);
	}, scale: 604800000, step: 1, base: -259200000 },
	time: { parse: parseTimeStr, str: timeStr, scale: 1000, step: 60, base: 0 },
	'datetime-local': { parse: parseLocalStr, str: ms => dateStr(Math.floor(ms / 86400000) * 86400000) + 'T' + timeStr(ms), scale: 1000, step: 60, base: 0 },
	number: { parse: s => /^-?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][-+]?\d+)?$/.test(s) ? +s : null, str: n => String(n), scale: 1, step: 1, base: 0 },
	range: { parse: s => /^-?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][-+]?\d+)?$/.test(s) ? +s : null, str: n => String(n), scale: 1, step: 1, base: 0 },
};
function numAttr(el, name, t) {
	const v = N.attr(el, name);
	if (v === null) return null;
	const n = TYPE_NUM[t].parse(v);
	return n === null ? null : n;
}
function rangeBounds(el) {
	let min = numAttr(el, 'min', 'range'), max = numAttr(el, 'max', 'range');
	if (min === null) min = 0;
	if (max === null) max = 100;
	if (max < min) max = min;
	return [min, max];
}
/* the allowed value step (in the type's unit), or null for "any" */
function allowedStep(el, t) {
	const d = TYPE_NUM[t];
	const s = N.attr(el, 'step');
	if (s !== null && s.trim().toLowerCase() === 'any') return null;
	let st = s === null ? NaN : parseFloat(s);
	if (!(st > 0)) st = d.step;
	if (t === 'month' || t === 'week' || t === 'date') st = Math.max(1, Math.round(st));
	return st * d.scale;
}
function stepBase(el, t) {
	const min = numAttr(el, 'min', t);
	if (min !== null) return min;
	const v = numAttr(el, 'value', t);
	if (v !== null) return v;
	return TYPE_NUM[t].base;
}
function aligned(v, base, step) {
	const q = (v - base) / step;
	return Math.abs(q - Math.round(q)) < 1e-9;
}

/* the value sanitization algorithm of each type */
function sanitize(el, t, v) {
	switch (t) {
	case 'text': case 'search': case 'tel': case 'password':
		return v.replace(/[\r\n]/g, '');
	case 'url':
		return v.replace(/[\r\n]/g, '').trim();
	case 'email':
		v = v.replace(/[\r\n]/g, '');
		return N.attr(el, 'multiple') !== null ?
			v.split(',').map(x => x.trim()).join(',') : v.trim();
	case 'number':
		return TYPE_NUM.number.parse(v) === null ? '' : v;
	case 'range': {
		let n = TYPE_NUM.range.parse(v);
		const [min, max] = rangeBounds(el);
		if (n === null) n = min + (max - min) / 2;
		if (n < min) n = min;
		if (n > max) n = max;
		const st = allowedStep(el, 'range');
		if (st !== null) {
			const base = stepBase(el, 'range');
			if (!aligned(n, base, st)) {
				let a = base + Math.round((n - base) / st) * st;
				if (a > max) a -= st;
				if (a < min) a += st;
				n = +a.toFixed(10);
			}
		}
		return n === TYPE_NUM.range.parse(v) ? v : String(n);
	}
	case 'color':
		return /^#[0-9a-fA-F]{6}$/.test(v) ? v.toLowerCase() : '#000000';
	case 'date': return parseDateStr(v) === null ? '' : v;
	case 'month': return parseMonthStr(v) === null ? '' : v;
	case 'week': return parseWeekStr(v) === null ? '' : v;
	case 'time': return parseTimeStr(v) === null ? '' : v;
	case 'datetime-local': {
		const n = parseLocalStr(v);
		return n === null ? '' : TYPE_NUM['datetime-local'].str(n);
	}
	}
	return v;
}

const inputProto = G.HTMLInputElement.prototype;
const rawValue = Object.getOwnPropertyDescriptor(inputProto, 'value');
const customValidity = new WeakMap();
const indeterminate = new WeakMap();
def(inputProto, {
	get type() { return inputType(this); },
	set type(v) { this.setAttribute('type', v); },
	get value() {
		const t = inputType(this);
		const v = rawValue.get.call(this);
		if (t === 'file') return '';
		return VALUE_MODE.has(t) ? sanitize(this, t, v) : v;
	},
	set value(v) {
		const t = inputType(this);
		if (t === 'file') {
			if (v === '' || v === null) return;
			throw domError('a file input\'s value can only be emptied', 'InvalidStateError');
		}
		v = v === null ? '' : String(v);
		rawValue.set.call(this, VALUE_MODE.has(t) ? sanitize(this, t, v) : v);
	},
	get valueAsNumber() {
		const t = inputType(this);
		if (!TYPE_NUM[t]) return NaN;
		const n = TYPE_NUM[t].parse(this.value);
		return n === null ? NaN : n;
	},
	set valueAsNumber(n) {
		const t = inputType(this);
		if (!TYPE_NUM[t]) throw domError('no numbers for type ' + t, 'InvalidStateError');
		n = +n;
		if (!isFinite(n)) {
			if (isNaN(n)) { this.value = ''; return; }
			throw new TypeError('not a finite number');
		}
		this.value = TYPE_NUM[t].str(n);
	},
	get valueAsDate() {
		const t = inputType(this);
		if (!['date', 'month', 'week', 'time'].includes(t)) return null;
		const n = TYPE_NUM[t].parse(this.value);
		if (n === null) return null;
		if (t === 'month') {
			const y = Math.floor(n / 12) + 1970, m = ((n % 12) + 12) % 12;
			return new Date(mkDate(y, m + 1, 1));
		}
		return new Date(n);
	},
	set valueAsDate(d) {
		const t = inputType(this);
		if (!['date', 'month', 'week', 'time'].includes(t))
			throw domError('no dates for type ' + t, 'InvalidStateError');
		if (d === null) { this.value = ''; return; }
		if (!(d instanceof Date)) throw new TypeError('not a Date');
		const ms = d.getTime();
		if (isNaN(ms)) { this.value = ''; return; }
		if (t === 'month') {
			const dd = new Date(ms);
			this.value = TYPE_NUM.month.str((dd.getUTCFullYear() - 1970) * 12 + dd.getUTCMonth());
		} else {
			this.value = TYPE_NUM[t].str(ms);
		}
	},
	stepUp(n = 1) { stepBy(this, n); },
	stepDown(n = 1) { stepBy(this, -n); },
	get indeterminate() { return !!indeterminate.get(this); },
	set indeterminate(v) { indeterminate.set(this, !!v); },
	get files() {
		if (inputType(this) !== 'file') return null;
		if (!this._files) Object.defineProperty(this, '_files', { value: new FileList([]) });
		return this._files;
	},
	set files(v) { if (v instanceof FileList) Object.defineProperty(this, '_files', { value: v, configurable: true }); },
	get list() {
		const id = N.attr(this, 'list');
		const e = id ? G.document.getElementById(id) : null;
		return e && e.localName === 'datalist' ? e : null;
	},
	get width() {
		if (inputType(this) !== 'image') return 0;
		const r = N.rect(this);
		return r ? r[2] : parseInt(N.attr(this, 'width'), 10) || 0;
	},
	set width(v) { this.setAttribute('width', String(v >>> 0)); },
	get height() {
		if (inputType(this) !== 'image') return 0;
		const r = N.rect(this);
		return r ? r[3] : parseInt(N.attr(this, 'height'), 10) || 0;
	},
	set height(v) { this.setAttribute('height', String(v >>> 0)); },
	get selectionDirection() { return TEXTLIKE.has(inputType(this)) ? 'none' : null; },
	set selectionDirection(v) {},
	setRangeText(text) { this.value = String(text); },
});
for (const p of ['formEnctype', 'formTarget', 'alt', 'src'])
	if (!(p in inputProto)) Object.defineProperty(inputProto, p, { configurable: true,
		get() { return N.attr(this, p.toLowerCase()) || ''; },
		set(v) { this.setAttribute(p.toLowerCase(), v); } });

function stepBy(el, n) {
	const t = inputType(el);
	if (!TYPE_NUM[t]) throw domError('stepUp / stepDown: not for type ' + t, 'InvalidStateError');
	const st = allowedStep(el, t);
	if (st === null) throw domError('step is "any"', 'InvalidStateError');
	const min = t === 'range' ? rangeBounds(el)[0] : numAttr(el, 'min', t);
	const max = t === 'range' ? rangeBounds(el)[1] : numAttr(el, 'max', t);
	if (min !== null && max !== null && min > max) return;
	const base = stepBase(el, t);
	let v = TYPE_NUM[t].parse(el.value);
	if (v === null) v = 0;
	n = Math.trunc(+n) || 0;
	if (!aligned(v, base, st)) {
		v = n > 0 ? base + Math.floor((v - base) / st + 1) * st : base + Math.ceil((v - base) / st - 1) * st;
	} else {
		v += st * n;
	}
	if (min !== null && v < min) v = base + Math.ceil((min - base) / st) * st;
	if (max !== null && v > max) v = base + Math.floor((max - base) / st) * st;
	el.value = TYPE_NUM[t].str(+v.toFixed(10));
}

/* FileList (the input's files: none chosen -- no file picker on Onyx yet) */
class FileList {
	constructor(files) {
		if (!Array.isArray(files)) throw new TypeError('Illegal constructor');
		files.forEach((f, i) => { this[i] = f; });
		Object.defineProperty(this, 'length', { value: files.length });
	}
	item(i) { return this[i] || null; }
	*[Symbol.iterator]() { for (let i = 0; i < this.length; i++) yield this[i]; }
}
G.FileList = FileList;

/* ValidityState */
class ValidityState {
	constructor(flags) { Object.assign(this, flags); }
}
for (const k of ['valueMissing', 'typeMismatch', 'patternMismatch', 'tooLong', 'tooShort',
		'rangeUnderflow', 'rangeOverflow', 'stepMismatch', 'badInput', 'customError'])
	ValidityState.prototype[k] = false;
Object.defineProperty(ValidityState.prototype, 'valid', { configurable: true,
	get() {
		return !(this.valueMissing || this.typeMismatch || this.patternMismatch || this.tooLong ||
			this.tooShort || this.rangeUnderflow || this.rangeOverflow || this.stepMismatch ||
			this.badInput || this.customError);
	} });
G.ValidityState = ValidityState;

const EMAIL = /^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/;
function patternMatches(pattern, v) {
	let re;
	try { re = new RegExp('^(?:' + pattern + ')$', 'v'); } catch (e) {
		try { re = new RegExp('^(?:' + pattern + ')$', 'u'); } catch (e2) { return true; }
	}
	return re.test(v);
}
function disabledByFieldset(el) {
	for (let p = N.parent(el); p && N.type(p) === ELEMENT_NODE; p = N.parent(p)) {
		if (N.lname(p) === 'fieldset' && N.attr(p, 'disabled') !== null) {
			/* not inside that fieldset's first legend */
			const legend = [...N.children(p)].find(c => N.type(c) === ELEMENT_NODE && N.lname(c) === 'legend');
			if (!legend || !legend.contains(el)) return true;
		}
	}
	return false;
}
function isDisabled(el) {
	return N.attr(el, 'disabled') !== null || disabledByFieldset(el);
}
/* a candidate for constraint validation? */
function willValidate(el) {
	const tag = N.lname(el);
	if (isDisabled(el) || el.closest('datalist')) return false;
	if (tag === 'input') {
		const t = inputType(el);
		if (t === 'hidden' || t === 'reset' || t === 'button') return false;
		if (N.attr(el, 'readonly') !== null && !['checkbox', 'radio', 'file', 'submit', 'image', 'color', 'range'].includes(t)) return false;
		return true;
	}
	if (tag === 'textarea') return N.attr(el, 'readonly') === null;
	if (tag === 'select') return true;
	if (tag === 'button') return (N.attr(el, 'type') || 'submit').toLowerCase() === 'submit';
	return false;
}
function validityOf(el) {
	const f = {};
	const tag = N.lname(el);
	const custom = customValidity.get(el) || '';
	if (custom) f.customError = true;
	if (!willValidate(el)) return new ValidityState(f);
	const required = N.attr(el, 'required') !== null;
	if (tag === 'input') {
		const t = inputType(el);
		const v = el.value;
		if (required) {
			if (t === 'checkbox') f.valueMissing = !el.checked;
			else if (t === 'radio') {
				const name = N.attr(el, 'name');
				const group = name ? [...(el.form || G.document).querySelectorAll('input[type=radio]')]
					.filter(r => N.attr(r, 'name') === name && r.form === el.form) : [el];
				f.valueMissing = !group.some(r => r.checked);
			} else if (t === 'file') f.valueMissing = el.files.length === 0;
			else if (VALUE_MODE.has(t) && t !== 'range' && t !== 'color') f.valueMissing = v === '';
		}
		if (v !== '') {
			if (t === 'email')
				f.typeMismatch = !(N.attr(el, 'multiple') !== null ? v.split(',') : [v]).every(x => EMAIL.test(x));
			if (t === 'url') {
				try { new URL(v); } catch (e) { f.typeMismatch = true; }
			}
			const pattern = N.attr(el, 'pattern');
			if (pattern !== null && TEXTLIKE.has(t))
				f.patternMismatch = !(N.attr(el, 'multiple') !== null && t === 'email' ? v.split(',') : [v])
					.every(x => patternMatches(pattern, x));
			if (TYPE_NUM[t]) {
				const n = TYPE_NUM[t].parse(v);
				if (n !== null) {
					const min = numAttr(el, 'min', t), max = numAttr(el, 'max', t);
					if (min !== null && n < min) f.rangeUnderflow = true;
					if (max !== null && n > max) f.rangeOverflow = true;
					const st = allowedStep(el, t);
					if (st !== null && !aligned(n, stepBase(el, t), st)) f.stepMismatch = true;
				}
			}
			/* tooLong / tooShort: only a value the user typed */
			if (TEXTLIKE.has(t) && N.formValue(el) !== null) {
				const max = parseInt(N.attr(el, 'maxlength'), 10), min = parseInt(N.attr(el, 'minlength'), 10);
				if (max >= 0 && v.length > max) f.tooLong = true;
				if (min >= 0 && v.length < min) f.tooShort = true;
			}
		}
	} else if (tag === 'textarea') {
		const v = el.value;
		if (required && v === '') f.valueMissing = true;
		if (v !== '' && N.formValue(el) !== null) {
			const max = parseInt(N.attr(el, 'maxlength'), 10), min = parseInt(N.attr(el, 'minlength'), 10);
			if (max >= 0 && v.length > max) f.tooLong = true;
			if (min >= 0 && v.length < min) f.tooShort = true;
		}
	} else if (tag === 'select') {
		if (required) {
			const opts = [...el.options];
			const sel = opts.filter(o => o.selected);
			/* a placeholder label option (the first, value "", no multiple, size 1) does not count */
			const ph = N.attr(el, 'multiple') === null && (parseInt(N.attr(el, 'size'), 10) || 1) === 1 &&
				opts[0] && opts[0].value === '' && N.parent(opts[0]) === el ? opts[0] : null;
			f.valueMissing = sel.length === 0 || (sel.length === 1 && sel[0] === ph);
		}
	}
	return new ValidityState(f);
}
function validationMessage(el) {
	if (!willValidate(el)) return '';
	const v = validityOf(el);
	if (v.customError) return customValidity.get(el);
	if (v.valueMissing) return 'Please fill in this field.';
	if (v.typeMismatch) return inputType(el) === 'email' ? 'Please enter an email address.' : 'Please enter a URL.';
	if (v.patternMismatch) return 'Please match the requested format.';
	if (v.tooLong) return 'Please shorten this text.';
	if (v.tooShort) return 'Please lengthen this text.';
	if (v.rangeUnderflow) return 'Value must be greater than or equal to ' + N.attr(el, 'min') + '.';
	if (v.rangeOverflow) return 'Value must be less than or equal to ' + N.attr(el, 'max') + '.';
	if (v.stepMismatch) return 'Please enter a valid value.';
	return '';
}
/* checkValidity: false and an "invalid" event when the control is not valid */
function checkOne(el) {
	if (!willValidate(el) || validityOf(el).valid) return true;
	fire(el, 'invalid', { cancelable: true });
	return false;
}
function reportOne(el) {
	if (!willValidate(el) || validityOf(el).valid) return true;
	if (fire(el, 'invalid', { cancelable: true })) {
		N.log('form: ' + validationMessage(el));
		try { el.focus(); } catch (e) {}
	}
	return false;
}
const validationMethods = {
	get validity() { return validityOf(this); },
	get willValidate() { return willValidate(this); },
	get validationMessage() { return validationMessage(this); },
	checkValidity() { return checkOne(this); },
	reportValidity() { return reportOne(this); },
	setCustomValidity(m) { customValidity.set(this, m === undefined || m === null ? '' : String(m)); },
};
const LISTED = ['HTMLInputElement', 'HTMLTextAreaElement', 'HTMLSelectElement', 'HTMLButtonElement',
	'HTMLFieldSetElement', 'HTMLOutputElement', 'HTMLObjectElement'];
function formOwner(el) {
	const id = N.attr(el, 'form');
	if (id !== null) {
		const f = G.document.getElementById(id);
		return f && N.lname(f) === 'form' ? f : null;
	}
	return el.closest('form');
}
for (const name of LISTED) {
	const C = G[name];
	if (!C) continue;
	def(C.prototype, validationMethods);
	def(C.prototype, { get form() { return formOwner(this); } });
	if (!('labels' in C.prototype) && name !== 'HTMLFieldSetElement' && name !== 'HTMLObjectElement')
		def(C.prototype, { get labels() { return G.document.querySelectorAll('label').length ?
			[...G.document.querySelectorAll('label')].filter(l => l.control === this) : []; } });
}
/* the submitter's form* attributes */
for (const name of ['HTMLInputElement', 'HTMLButtonElement']) {
	const C = G[name];
	if (!C) continue;
	def(C.prototype, {
		get formAction() {
			const v = N.attr(this, 'formaction');
			if (v === null || v === '') { const f = this.form; return f ? f.action : N.url(); }
			try { return new URL(v, N.url()).href; } catch (e) { return v; }
		},
		set formAction(v) { this.setAttribute('formaction', v); },
		get formMethod() { const m = (N.attr(this, 'formmethod') || '').toLowerCase(); return ['get', 'post', 'dialog'].includes(m) ? m : m ? 'get' : ''; },
		set formMethod(v) { this.setAttribute('formmethod', v); },
		get formEnctype() { const e = (N.attr(this, 'formenctype') || '').toLowerCase(); return ['application/x-www-form-urlencoded', 'multipart/form-data', 'text/plain'].includes(e) ? e : e ? 'application/x-www-form-urlencoded' : ''; },
		set formEnctype(v) { this.setAttribute('formenctype', v); },
		get formTarget() { return N.attr(this, 'formtarget') || ''; },
		set formTarget(v) { this.setAttribute('formtarget', v); },
		get formNoValidate() { return N.attr(this, 'formnovalidate') !== null; },
		set formNoValidate(v) { this.toggleAttribute('formnovalidate', !!v); },
	});
}
if (G.HTMLTextAreaElement) def(G.HTMLTextAreaElement.prototype, {
	get minLength() { const v = parseInt(N.attr(this, 'minlength'), 10); return isNaN(v) ? -1 : v; },
	set minLength(v) { this.setAttribute('minlength', String(v)); },
	get selectionDirection() { return 'none'; },
	set selectionDirection(v) {},
	get dirName() { return N.attr(this, 'dirname') || ''; },
	set dirName(v) { this.setAttribute('dirname', v); },
});
if (G.HTMLSelectElement) def(G.HTMLSelectElement.prototype, {
	get required() { return N.attr(this, 'required') !== null; },
	set required(v) { this.toggleAttribute('required', !!v); },
});
if (G.HTMLFieldSetElement) def(G.HTMLFieldSetElement.prototype, {
	get disabled() { return N.attr(this, 'disabled') !== null; },
	set disabled(v) { this.toggleAttribute('disabled', !!v); },
	get type() { return 'fieldset'; },
	get name() { return N.attr(this, 'name') || ''; },
	set name(v) { this.setAttribute('name', v); },
});
if (G.HTMLOutputElement) def(G.HTMLOutputElement.prototype, {
	get value() { return this.textContent; },
	set value(v) {
		if (!Object.prototype.hasOwnProperty.call(this, '_dflt'))
			Object.defineProperty(this, '_dflt', { value: this.textContent, writable: true });
		this.textContent = v;
	},
	get defaultValue() { return Object.prototype.hasOwnProperty.call(this, '_dflt') ? this._dflt : this.textContent; },
	set defaultValue(v) {
		if (Object.prototype.hasOwnProperty.call(this, '_dflt')) this._dflt = String(v);
		else this.textContent = v;
	},
	get type() { return 'output'; },
	get htmlFor() { return new G.DOMTokenList(this, 'for'); },
	get name() { return N.attr(this, 'name') || ''; },
	set name(v) { this.setAttribute('name', v); },
});

/* the form: its listed elements (the form attribute's too), validation, submission blocked
 * while a control is invalid */
if (G.HTMLFormElement) {
	const formProto = G.HTMLFormElement.prototype;
	const elementsGet = Object.getOwnPropertyDescriptor(formProto, 'elements').get;
	/* Onyx: kept while no tree and no attribute changed (a loop over form.elements[i] made
	 * it again at each step: a query of the form, one of the document) */
	function formElements() {
		const own = elementsGet.call(this);
		const id = N.attr(this, 'id');
		if (!id) return own;
		const extra = [...G.document.querySelectorAll('[form]')].filter(e =>
			N.attr(e, 'form') === id && LISTED.includes(e.constructor.name));
		if (!extra.length) return own;
		const all = [...own, ...extra.filter(e => ![...own].includes(e))];
		all.sort((a, b) => a.compareDocumentPosition(b) & 4 ? -1 : 1);
		return G.HTMLCollection && own instanceof G.HTMLCollection ? Object.setPrototypeOf(
			Object.assign(all, { item: i => all[i] || null, namedItem: n => all.find(e => N.attr(e, 'name') === n || N.attr(e, 'id') === n) || null }),
			Object.getPrototypeOf(own)) : all;
	}
	const FORM_ELS = Symbol('elements');
	def(formProto, {
		get elements() {
			const g = N.treeGen(), a = N.attrGen(), k = this[FORM_ELS];
			if (k !== undefined && k.g === g && k.a === a) return k.v;
			const v = formElements.call(this);
			try { this[FORM_ELS] = { g, a, v }; } catch (e) { /* (not kept) */ }
			return v;
		},
		checkValidity() {
			let ok = true;
			for (const e of this.elements) if (!checkOne(e)) ok = false;
			return ok;
		},
		reportValidity() {
			let ok = true, first = null;
			for (const e of this.elements)
				if (willValidate(e) && !validityOf(e).valid) {
					if (fire(e, 'invalid', { cancelable: true }) && !first) first = e;
					ok = false;
				}
			if (first) { N.log('form: ' + validationMessage(first)); try { first.focus(); } catch (e) {} }
			return ok;
		},
	});
	/* interactive validation: before a submit event (the browser's or requestSubmit's) */
	G.addEventListener('submit', ev => {
		const form = ev.target;
		if (!(form instanceof G.HTMLFormElement) || N.attr(form, 'novalidate') !== null) return;
		const s = ev.submitter;
		if (s && N.attr(s, 'formnovalidate') !== null) return;
		if (!form.reportValidity()) {
			ev.preventDefault();
			ev.stopImmediatePropagation();
		}
	}, true);
}

/* on* handlers the elements lacked */
for (const t of ['invalid', 'close', 'cancel', 'beforetoggle', 'formdata', 'beforeinput',
		'search', 'selectionchange', 'selectstart', 'slotchange', 'drag', 'dragstart', 'dragend',
		'dragenter', 'dragleave', 'dragover', 'drop'])
	if (!('on' + t in HTMLElement.prototype)) handlerProperty(HTMLElement.prototype, t);

/* the pseudo-classes of forms, dialogs, details (dom.js's selector engine asks) */
const pseudo = (e, name) => {
	const tag = N.lname(e);
	switch (name) {
	case 'valid': return (willValidate(e) && validityOf(e).valid) ||
		(tag === 'form' && [...e.elements].every(c => !willValidate(c) || validityOf(c).valid)) ||
		(tag === 'fieldset' && [...e.elements].every(c => !willValidate(c) || validityOf(c).valid));
	case 'invalid': return (willValidate(e) && !validityOf(e).valid) ||
		((tag === 'form' || tag === 'fieldset') && [...e.elements].some(c => willValidate(c) && !validityOf(c).valid));
	case 'user-valid': case 'user-invalid':
		return N.formValue(e) !== null && willValidate(e) && validityOf(e).valid === (name === 'user-valid');
	case 'in-range': case 'out-of-range': {
		if (tag !== 'input' || !TYPE_NUM[inputType(e)]) return false;
		const t = inputType(e);
		if (numAttr(e, 'min', t) === null && numAttr(e, 'max', t) === null && t !== 'range') return false;
		const v = validityOf(e);
		const out = v.rangeUnderflow || v.rangeOverflow;
		return name === 'out-of-range' ? out : !out;
	}
	case 'indeterminate': return (tag === 'input' && inputType(e) === 'checkbox' && !!indeterminate.get(e)) ||
		(tag === 'progress' && N.attr(e, 'value') === null);
	case 'default': return (tag === 'input' && ['checkbox', 'radio'].includes(inputType(e)) && N.attr(e, 'checked') !== null) ||
		(tag === 'option' && N.attr(e, 'selected') !== null);
	case 'open': return (tag === 'details' || tag === 'dialog') && N.attr(e, 'open') !== null;
	case 'modal': return tag === 'dialog' && !!modalDialogs && modalDialogs.includes(e);
	case 'popover-open': return openPopovers.includes(e);
	case 'autofill': return false;
	}
	return false;
};
let modalDialogs = [];
/* Onyx: the showing popovers, in the order they were shown (the Popover API, below) */
const openPopovers = [];
/* the states the style sheets' :popover-open and :modal see (libcss asks NetSurf: N.setState) */
const STATE_POPOVER_OPEN = 1, STATE_MODAL = 2;
let hideAutoPopovers = () => {};	/* (showModal hides them: set by the Popover API below) */
if (N.internals) N.internals.pseudo = pseudo;

/* ---- <dialog>, <details>, hidden ---------------------------------------------------------- */

if (G.HTMLDialogElement) {
	const returnValues = new WeakMap();
	def(G.HTMLDialogElement.prototype, {
		get open() { return N.attr(this, 'open') !== null; },
		set open(v) { this.toggleAttribute('open', !!v); },
		get returnValue() { return returnValues.get(this) || ''; },
		set returnValue(v) { returnValues.set(this, String(v)); },
		get closedBy() { return (N.attr(this, 'closedby') || 'auto').toLowerCase(); },
		set closedBy(v) { this.setAttribute('closedby', v); },
		show() {
			if (this.open) {
				if (modalDialogs.includes(this)) throw domError('a modal dialog', 'InvalidStateError');
				return;
			}
			this.setAttribute('open', '');
			focusDialog(this);
		},
		showModal() {
			if (this.open) {
				if (modalDialogs.includes(this)) return;
				throw domError('already open', 'InvalidStateError');
			}
			if (!this.isConnected) throw domError('not connected', 'InvalidStateError');
			hideAutoPopovers();
			this.setAttribute('open', '');
			modalDialogs.push(this);
			N.setState(this, STATE_MODAL, true);
			focusDialog(this);
		},
		close(result) {
			if (!this.open) return;
			this.removeAttribute('open');
			if (result !== undefined) returnValues.set(this, String(result));
			modalDialogs = modalDialogs.filter(d => d !== this);
			N.setState(this, STATE_MODAL, false);
			task(() => fire(this, 'close'));
		},
		requestClose(result) {
			if (!this.open) return;
			if (fire(this, 'cancel', { cancelable: true })) this.close(result);
		},
	});
	const focusDialog = d => {
		const f = d.querySelector('[autofocus]') || d.querySelector('button,input,select,textarea,a[href],[tabindex]');
		try { if (f) f.focus(); } catch (e) {}
	};
	/* Escape: the topmost modal dialog asks to close */
	G.document.addEventListener('keydown', ev => {
		if (ev.key !== 'Escape' || !modalDialogs.length || ev.defaultPrevented) return;
		const d = modalDialogs[modalDialogs.length - 1];
		if (d.closedBy !== 'none') d.requestClose();
	});
	/* a form method=dialog closes its dialog with the submitter's value */
	G.addEventListener('submit', ev => {
		const form = ev.target;
		if (ev.defaultPrevented || !(form instanceof G.HTMLFormElement)) return;
		const s = ev.submitter;
		const method = (s && N.attr(s, 'formmethod')) || N.attr(form, 'method') || '';
		if (method.toLowerCase() !== 'dialog') return;
		ev.preventDefault();
		const d = form.closest('dialog');
		if (d) d.close(s && N.attr(s, 'value') !== null ? N.attr(s, 'value') : undefined);
	});
}

/* ---- the Popover API (Onyx) ----------------------------------------------------------------
 * showPopover / hidePopover / togglePopover, the popovertarget buttons, beforetoggle (the
 * showing can be cancelled) and toggle (a task, coalesced), the "auto" popovers' light
 * dismiss (a click outside, Escape) and their stack (showing one hides the others but its
 * ancestors). The UA sheet hides [popover]:not(:popover-open); the state reaches libcss
 * through N.setState (css/select.c: :popover-open). */
class ToggleEvent extends G.Event {
	constructor(type, init = {}) {
		super(type, init);
		Object.defineProperty(this, '_t', { value: {
			oldState: String(init.oldState ?? ''), newState: String(init.newState ?? ''),
			source: init.source ?? null } });
	}
	get oldState() { return this._t.oldState; }
	get newState() { return this._t.newState; }
	get source() { return this._t.source; }
}
G.ToggleEvent = ToggleEvent;
{
	const popoverKind = el => {
		const v = N.attr(el, 'popover');
		if (v === null) return null;
		const k = v.toLowerCase();
		return k === 'manual' ? 'manual' : k === 'hint' ? 'hint' : 'auto';
	};
	const pendingToggle = new WeakMap();
	const queueToggle = (el, oldState, newState, source) => {
		const rec = pendingToggle.get(el);
		if (rec) { rec.newState = newState; return; }
		const r = { oldState, newState };
		pendingToggle.set(el, r);
		task(() => {
			pendingToggle.delete(el);
			if (r.oldState !== r.newState)
				el.dispatchEvent(new ToggleEvent('toggle', { oldState: r.oldState, newState: r.newState, source }));
		});
	};
	const check = (el, showing) => {
		if (popoverKind(el) === null) throw domError('Not a popover', 'NotSupportedError');
		if (openPopovers.includes(el) !== showing) return false;
		if (!el.isConnected) throw domError('The popover is not connected', 'InvalidStateError');
		if (N.lname(el) === 'dialog' && modalDialogs.includes(el))
			throw domError('The popover is a modal dialog', 'InvalidStateError');
		return true;
	};
	/* the showing auto popovers an element (or its invoker) is inside: they stay */
	const ancestors = (el, invoker) => {
		const keep = new Set();
		for (const start of [el, invoker]) {
			for (let n = start && N.parent(start); n; n = N.parent(n))
				if (openPopovers.includes(n)) keep.add(n);
		}
		return keep;
	};
	const hide = (el, events) => {
		const i = openPopovers.indexOf(el);
		if (i < 0) return;
		/* the auto popovers shown after it (its descendants in the stack) go first */
		if (popoverKind(el) === 'auto')
			for (let j = openPopovers.length - 1; j > i; j--)
				if (popoverKind(openPopovers[j]) === 'auto') hide(openPopovers[j], events);
		if (events)
			fire(el, 'beforetoggle', { oldState: 'open', newState: 'closed' });
		const k = openPopovers.indexOf(el);
		if (k < 0) return;
		openPopovers.splice(k, 1);
		N.setState(el, STATE_POPOVER_OPEN, false);
		if (events) queueToggle(el, 'open', 'closed', null);
	};
	const hideAutoExcept = (keep) => {
		for (let j = openPopovers.length - 1; j >= 0; j--) {
			const p = openPopovers[j];
			if (p && popoverKind(p) === 'auto' && !keep.has(p)) hide(p, true);
		}
	};
	hideAutoPopovers = () => hideAutoExcept(new Set());
	const show = (el, invoker) => {
		if (!check(el, false)) return;
		const ev = new ToggleEvent('beforetoggle', { cancelable: true, oldState: 'closed',
			newState: 'open', source: invoker || null });
		if (!el.dispatchEvent(ev)) return;
		if (!check(el, false)) return;
		if (popoverKind(el) === 'auto') hideAutoExcept(ancestors(el, invoker));
		openPopovers.push(el);
		N.setState(el, STATE_POPOVER_OPEN, true);
		const f = el.querySelector('[autofocus]');
		try { if (f) f.focus(); } catch (e) {}
		queueToggle(el, 'closed', 'open', invoker || null);
	};
	def(HTMLElement.prototype, {
		showPopover(opts) { show(this, opts && opts.source); },
		hidePopover() { if (check(this, true)) hide(this, true); },
		togglePopover(opts) {
			const force = typeof opts === 'boolean' ? opts : opts && opts.force;
			const open = openPopovers.includes(this);
			if (open && force !== true) this.hidePopover();
			else if (!open && force !== false) show(this, opts && typeof opts === 'object' ? opts.source : null);
			else check(this, open);		/* (the exceptions) */
			return openPopovers.includes(this);
		},
	});
	/* the invokers: <button> / <input type=button...> popovertarget, popovertargetaction */
	const invokerOf = n => {
		const tag = N.lname(n);
		if (tag !== 'button' && tag !== 'input') return false;
		if (tag === 'input' && !['button', 'submit', 'reset', 'image'].includes((N.attr(n, 'type') || '').toLowerCase()))
			return false;
		return true;
	};
	const targets = new WeakMap();
	const targetOf = n => {
		const t = targets.get(n);
		if (t) return t;
		const id = N.attr(n, 'popovertarget');
		return id === null ? null : G.document.getElementById(id);
	};
	for (const C of [G.HTMLButtonElement, G.HTMLInputElement]) {
		if (!C) continue;
		def(C.prototype, {
			get popoverTargetElement() { return targetOf(this); },
			set popoverTargetElement(el) {
				if (el) { targets.set(this, el); this.setAttribute('popovertarget', ''); }
				else { targets.delete(this); this.removeAttribute('popovertarget'); }
			},
			get popoverTargetAction() {
				const v = (N.attr(this, 'popovertargetaction') || '').toLowerCase();
				return v === 'show' || v === 'hide' ? v : 'toggle';
			},
			set popoverTargetAction(v) { this.setAttribute('popovertargetaction', v); },
		});
	}
	/* a click: an invoker's action, else (the user's click, not a script's) the light dismiss
	 * of the auto popovers it is outside */
	G.document.addEventListener('click', ev => {
		if (ev.defaultPrevented) return;
		let t = ev.target;
		for (let n = t; n && N.type(n) === ELEMENT_NODE; n = N.parent(n)) {
			if (!invokerOf(n) || (N.attr(n, 'popovertarget') === null && !targets.has(n))) continue;
			if (n.disabled) return;
			const p = targetOf(n);
			if (!p || popoverKind(p) === null) return;
			const action = n.popoverTargetAction;
			const open = openPopovers.includes(p);
			if (open && action !== 'show') hide(p, true);
			else if (!open && action !== 'hide') show(p, n);
			return;
		}
		if (!ev.isTrusted) return;
		for (let j = openPopovers.length - 1; j >= 0; j--) {
			const p = openPopovers[j];
			if (popoverKind(p) !== 'auto' && popoverKind(p) !== 'hint') continue;
			if (t && (p === t || p.contains(t))) break;
			hide(p, true);
		}
	});
	/* Escape (the user's): the topmost auto popover hides */
	G.document.addEventListener('keydown', ev => {
		if (ev.key !== 'Escape' || ev.defaultPrevented || !ev.isTrusted) return;
		for (let j = openPopovers.length - 1; j >= 0; j--) {
			if (popoverKind(openPopovers[j]) !== 'manual') { hide(openPopovers[j], true); break; }
		}
	});
}

/* <details>: "toggle" after the open attribute changes (a task; coalesced); a click on its
 * summary toggles it (dom.js's activation) */
const toggles = new WeakMap();
function openChanged(el, old, now) {
	const tag = N.lname(el);
	if (tag !== 'details' && tag !== 'dialog') return;
	if ((old !== null) === (now !== null)) return;
	const pending = toggles.get(el);
	const oldState = pending ? pending.oldState : old !== null ? 'open' : 'closed';
	const newState = now !== null ? 'open' : 'closed';
	if (pending) { pending.newState = newState; return; }
	const rec = { oldState, newState };
	toggles.set(el, rec);
	task(() => {
		toggles.delete(el);
		if (rec.oldState === rec.newState) return;
		el.dispatchEvent(new G.ToggleEvent('toggle', { oldState: rec.oldState, newState: rec.newState }));
	});
}
if (G.HTMLDetailsElement) def(G.HTMLDetailsElement.prototype, {
	get open() { return N.attr(this, 'open') !== null; },
	set open(v) { this.toggleAttribute('open', !!v); },
	get name() { return N.attr(this, 'name') || ''; },
	set name(v) { this.setAttribute('name', v); },
});

/* the hidden attribute: true, false or "until-found" */
def(HTMLElement.prototype, {
	get hidden() {
		const v = N.attr(this, 'hidden');
		if (v === null) return false;
		return v.toLowerCase() === 'until-found' ? 'until-found' : true;
	},
	set hidden(v) {
		if (v === 'until-found') this.setAttribute('hidden', 'until-found');
		else this.toggleAttribute('hidden', !!v);
	},
	get translate() {
		for (let n = this; n && N.type(n) === ELEMENT_NODE; n = N.parent(n)) {
			const v = N.attr(n, 'translate');
			if (v !== null) return v.toLowerCase() !== 'no';
		}
		return true;
	},
	set translate(v) { this.setAttribute('translate', v ? 'yes' : 'no'); },
	get accessKey() { return N.attr(this, 'accesskey') || ''; },
	set accessKey(v) { this.setAttribute('accesskey', v); },
	get accessKeyLabel() { const k = N.attr(this, 'accesskey'); return k ? 'Alt+' + k.toUpperCase() : ''; },
	get inert() { return N.attr(this, 'inert') !== null; },
	set inert(v) { this.toggleAttribute('inert', !!v); },
	get contentEditable() {
		const v = N.attr(this, 'contenteditable');
		if (v === null) return 'inherit';
		const l = v.toLowerCase();
		return l === '' || l === 'true' ? 'true' : l === 'false' ? 'false' : l === 'plaintext-only' ? 'plaintext-only' : 'inherit';
	},
	set contentEditable(v) {
		const l = String(v).toLowerCase();
		if (l === 'inherit') this.removeAttribute('contenteditable');
		else if (['true', 'false', 'plaintext-only'].includes(l)) this.setAttribute('contenteditable', l);
		else throw domError('bad value', 'SyntaxError');
	},
	get isContentEditable() {
		for (let n = this; n && N.type(n) === ELEMENT_NODE; n = N.parent(n)) {
			const v = N.attr(n, 'contenteditable');
			if (v !== null) return v.toLowerCase() !== 'false';
		}
		return G.document.designMode === 'on';
	},
	get draggable() {
		const v = N.attr(this, 'draggable');
		if (v !== null) return v.toLowerCase() === 'true';
		const t = N.lname(this);
		return (t === 'img') || (t === 'a' && N.attr(this, 'href') !== null);
	},
	set draggable(v) { this.setAttribute('draggable', v ? 'true' : 'false'); },
	get spellcheck() {
		for (let n = this; n && N.type(n) === ELEMENT_NODE; n = N.parent(n)) {
			const v = N.attr(n, 'spellcheck');
			if (v !== null) return v.toLowerCase() !== 'false';
		}
		return true;
	},
	set spellcheck(v) { this.setAttribute('spellcheck', v ? 'true' : 'false'); },
	get enterKeyHint() { return N.attr(this, 'enterkeyhint') || ''; },
	set enterKeyHint(v) { this.setAttribute('enterkeyhint', v); },
	get autocapitalize() { return N.attr(this, 'autocapitalize') || ''; },
	set autocapitalize(v) { this.setAttribute('autocapitalize', v); },
	get popover() { const v = N.attr(this, 'popover'); return v === null ? null : v === 'manual' ? 'manual' : v === 'hint' ? 'hint' : 'auto'; },
	set popover(v) { if (v === null) this.removeAttribute('popover'); else this.setAttribute('popover', v); },
});


/* ---- the session history: pushState / replaceState change the document's URL -------------
 * (qjs.c's setURL: location and the address bar); back / forward / go between the entries
 * this document made fire popstate (and hashchange); past them, the browser's own history.
 * (NetSurf's toolbar back button still leaves the page.) */
{
	const entries = [{ state: null, url: N.url() }];
	let index = 0;
	const sameOrigin = u => { try { return new URL(u).origin === new URL(N.url()).origin; } catch (e) { return false; } };
	const resolve = url => new URL(String(url), N.url()).href;
	const noFrag = u => u.replace(/#.*$/, '');
	const history = G.history;
	def(history, {
		get length() { return entries.length; },
		get state() { return entries[index].state; },
		pushState(state, title, url) {
			const s = structuredClone(state);
			let u = N.url();
			if (url !== undefined && url !== null) {
				u = resolve(url);
				if (!sameOrigin(u)) throw domError('pushState: another origin', 'SecurityError');
			}
			entries.splice(index + 1);
			entries.push({ state: s, url: u });
			index++;
			if (u !== N.url()) N.setURL(u);
		},
		replaceState(state, title, url) {
			const s = structuredClone(state);
			let u = N.url();
			if (url !== undefined && url !== null) {
				u = resolve(url);
				if (!sameOrigin(u)) throw domError('replaceState: another origin', 'SecurityError');
			}
			entries[index] = { state: s, url: u };
			if (u !== N.url()) N.setURL(u);
		},
		go(delta) {
			delta = Math.trunc(+delta) || 0;
			if (delta === 0) { G.location.reload(); return; }
			const to = index + delta;
			if (to < 0 || to >= entries.length) {
				N.history(delta);
				return;
			}
			task(() => {
				const from = entries[index];
				index = to;
				const e = entries[index];
				const oldURL = N.url();
				if (e.url !== oldURL) N.setURL(e.url);
				G.dispatchEvent(new G.PopStateEvent('popstate', { state: e.state }));
				if (noFrag(e.url) === noFrag(from.url) && e.url !== from.url)
					G.dispatchEvent(new G.HashChangeEvent('hashchange', { oldURL, newURL: e.url }));
			});
		},
		back() { this.go(-1); },
		forward() { this.go(1); },
	});
	/* the fragment: a new entry, the element scrolled to, hashchange */
	const LocationProto = Object.getPrototypeOf(G.location);
	def(LocationProto, {
		get hash() { try { const h = new URL(N.url()).hash; return h === '#' ? '' : h; } catch (e) { return ''; } },
		set hash(v) {
			let h = String(v);
			if (h && h[0] !== '#') h = '#' + h;
			const oldURL = N.url();
			const newURL = noFrag(oldURL) + (h === '#' ? '' : h);
			if (newURL === oldURL) return;
			entries.splice(index + 1);
			entries.push({ state: null, url: newURL });
			index++;
			N.setURL(newURL);
			const id = decodeURIComponent(h.slice(1));
			const target = id ? (G.document.getElementById(id) || G.document.querySelector('a[name="' + id.replace(/"/g, '\\"') + '"]')) : null;
			if (target) target.scrollIntoView();
			else if (!id) G.scrollTo(0, 0);
			task(() => G.dispatchEvent(new G.HashChangeEvent('hashchange', { oldURL, newURL })));
		},
	});
}

let streamOfBytes = null;	/* (a stream of the bytes a promise gives: Blob.stream) */
/* ---- streams: ReadableStream, WritableStream, TransformStream (the WHATWG Streams
 * standard's default streams; a byte stream reads as a default one, a BYOB reader copies) --- */
{
	const P = Promise;
	class CountQueuingStrategy {
		constructor({ highWaterMark = 1 } = {}) { this.highWaterMark = highWaterMark; }
		size() { return 1; }
	}
	class ByteLengthQueuingStrategy {
		constructor({ highWaterMark = 16384 } = {}) { this.highWaterMark = highWaterMark; }
		size(chunk) { return chunk.byteLength; }
	}
	function deferred() {
		let resolve, reject;
		const promise = new P((a, b) => { resolve = a; reject = b; });
		promise.catch(() => {});
		return { promise, resolve, reject };
	}

	class ReadableStreamDefaultController {
		constructor(stream, source, hwm, size) {
			this._s = stream; this._src = source; this._hwm = hwm; this._size = size || (() => 1);
			this._q = []; this._qsize = 0; this._closeRequested = false; this._pulling = false;
			this._pullAgain = false; this._started = false;
		}
		get desiredSize() {
			const st = this._s._state;
			return st === 'errored' ? null : st === 'closed' ? 0 : this._hwm - this._qsize;
		}
		enqueue(chunk) {
			if (this._closeRequested || this._s._state !== 'readable') throw new TypeError('the stream is closed');
			const s = this._s;
			if (s._reader && s._reader._reqs && s._reader._reqs.length) {
				s._reader._reqs.shift().resolve({ value: chunk, done: false });
			} else {
				let n = 1;
				try { n = +this._size(chunk); } catch (e) { this.error(e); throw e; }
				this._q.push([chunk, n]);
				this._qsize += n;
			}
			this._callPull();
		}
		close() {
			if (this._closeRequested || this._s._state !== 'readable') throw new TypeError('the stream is closed');
			this._closeRequested = true;
			if (!this._q.length) this._s._close();
		}
		error(e) {
			if (this._s._state !== 'readable') return;
			this._q = []; this._qsize = 0;
			this._s._error(e);
		}
		_callPull() {
			if (!this._started || this._closeRequested || this._s._state !== 'readable') return;
			const r = this._s._reader;
			const want = (r && r._reqs && r._reqs.length) || this.desiredSize > 0;
			if (!want || !this._src.pull) return;
			if (this._pulling) { this._pullAgain = true; return; }
			this._pulling = true;
			P.resolve().then(() => this._src.pull(this)).then(() => {
				this._pulling = false;
				if (this._pullAgain) { this._pullAgain = false; this._callPull(); }
			}, e => this.error(e));
		}
		_pull(req) {
			if (this._q.length) {
				const [chunk, n] = this._q.shift();
				this._qsize -= n;
				if (this._closeRequested && !this._q.length) this._s._close();
				else this._callPull();
				req.resolve({ value: chunk, done: false });
			} else {
				this._s._reader._reqs.push(req);
				this._callPull();
			}
		}
	}

	class ReadableStream {
		constructor(source = {}, strategy = {}) {
			this._state = 'readable';
			this._reader = null;
			this._storedError = undefined;
			this._disturbed = false;
			const bytes = source.type === 'bytes';
			const hwm = strategy.highWaterMark !== undefined ? +strategy.highWaterMark : bytes ? 0 : 1;
			if (isNaN(hwm) || hwm < 0) throw new RangeError('bad highWaterMark');
			const c = this._c = new ReadableStreamDefaultController(this, source, hwm, strategy.size);
			if (bytes) { c.byobRequest = null; }
			P.resolve().then(() => source.start ? source.start(c) : undefined).then(() => {
				c._started = true;
				c._callPull();
			}, e => c.error(e));
		}
		get locked() { return !!this._reader; }
		_close() {
			if (this._state !== 'readable') return;
			this._state = 'closed';
			const r = this._reader;
			if (r) {
				for (const q of r._reqs || []) q.resolve({ value: undefined, done: true });
				if (r._reqs) r._reqs = [];
				r._closed.resolve();
			}
		}
		_error(e) {
			if (this._state !== 'readable') return;
			this._state = 'errored';
			this._storedError = e;
			const r = this._reader;
			if (r) {
				for (const q of r._reqs || []) q.reject(e);
				if (r._reqs) r._reqs = [];
				r._closed.reject(e);
			}
		}
		cancel(reason) {
			if (this._reader) return P.reject(new TypeError('the stream is locked'));
			return this._cancel(reason);
		}
		_cancel(reason) {
			this._disturbed = true;
			if (this._state === 'closed') return P.resolve();
			if (this._state === 'errored') return P.reject(this._storedError);
			this._c._q = [];
			this._close();
			const src = this._c._src;
			return P.resolve(src.cancel ? src.cancel(reason) : undefined).then(() => undefined);
		}
		getReader(opts) {
			if (this._reader) throw new TypeError('the stream is locked');
			return opts && opts.mode === 'byob' ? new ReadableStreamBYOBReader(this) :
				new ReadableStreamDefaultReader(this);
		}
		tee() {
			const reader = this.getReader();
			let c1, c2, canceled = 0;
			const cancelBoth = deferred();
			let reading = false;
			const pull = () => {
				if (reading) return P.resolve();
				reading = true;
				return reader.read().then(({ value, done }) => {
					reading = false;
					if (done) { try { c1.close(); } catch (e) {} try { c2.close(); } catch (e) {} return; }
					try { c1.enqueue(value); } catch (e) {}
					try { c2.enqueue(value); } catch (e) {}
				}, e => { c1.error(e); c2.error(e); });
			};
			const cancel = r => { if (++canceled === 2) cancelBoth.resolve(reader.cancel(r)); return cancelBoth.promise; };
			const b1 = new ReadableStream({ start(c) { c1 = c; }, pull, cancel });
			const b2 = new ReadableStream({ start(c) { c2 = c; }, pull, cancel });
			return [b1, b2];
		}
		pipeTo(dest, opts = {}) {
			const reader = this.getReader(), writer = dest.getWriter();
			const signal = opts.signal;
			return new P((resolve, reject) => {
				let done = false;
				const finish = (err, isErr) => {
					if (done) return;
					done = true;
					reader.releaseLock();
					writer.releaseLock();
					isErr ? reject(err) : resolve();
				};
				if (signal) {
					const abort = () => {
						const r = signal.reason;
						const acts = [];
						if (!opts.preventAbort) acts.push(dest.abort(r));
						if (!opts.preventCancel) acts.push(this._cancel(r));
						P.all(acts).then(() => finish(r, true), e => finish(e, true));
					};
					if (signal.aborted) { abort(); return; }
					signal.addEventListener('abort', abort);
				}
				const step = () => reader.read().then(({ value, done: d }) => {
					if (done) return;
					if (d) {
						if (opts.preventClose) finish();
						else writer.close().then(() => finish(), e => finish(e, true));
						return;
					}
					return writer.write(value).then(step, e => {
						if (!opts.preventCancel) this._cancel(e);
						finish(e, true);
					});
				}, e => {
					if (!opts.preventAbort) writer.abort(e).catch(() => {});
					finish(e, true);
				});
				step();
			});
		}
		pipeThrough(t, opts) {
			this.pipeTo(t.writable, opts).catch(() => {});
			return t.readable;
		}
		values(opts = {}) {
			const reader = this.getReader();
			return {
				next: () => reader.read().then(r => { if (r.done) reader.releaseLock(); return r; }),
				return: v => {
					if (!opts.preventCancel) { const p = reader.cancel(v); reader.releaseLock(); return p.then(() => ({ value: v, done: true })); }
					reader.releaseLock();
					return P.resolve({ value: v, done: true });
				},
				[Symbol.asyncIterator]() { return this; },
			};
		}
		[Symbol.asyncIterator](opts) { return this.values(opts); }
		static from(it) {
			if (it instanceof ReadableStream) return it;
			const iter = it[Symbol.asyncIterator] ? it[Symbol.asyncIterator]() : it[Symbol.iterator]();
			return new ReadableStream({
				pull(c) {
					return P.resolve(iter.next()).then(r => r.done ? c.close() : P.resolve(r.value).then(v => c.enqueue(v)));
				},
				cancel(r) { if (iter.return) return iter.return(r); },
			}, { highWaterMark: 0 });
		}
	}

	class ReadableStreamDefaultReader {
		constructor(stream) {
			if (!(stream instanceof ReadableStream)) throw new TypeError('not a ReadableStream');
			if (stream._reader) throw new TypeError('the stream is locked');
			this._s = stream;
			this._reqs = [];
			this._closed = deferred();
			stream._reader = this;
			if (stream._state === 'closed') this._closed.resolve();
			if (stream._state === 'errored') this._closed.reject(stream._storedError);
		}
		get closed() { return this._closed.promise; }
		read() {
			const s = this._s;
			if (!s) return P.reject(new TypeError('released'));
			s._disturbed = true;
			if (s._state === 'closed') return P.resolve({ value: undefined, done: true });
			if (s._state === 'errored') return P.reject(s._storedError);
			const d = deferred();
			s._c._pull(d);
			return d.promise;
		}
		cancel(reason) { return this._s ? this._s._cancel(reason) : P.reject(new TypeError('released')); }
		releaseLock() {
			const s = this._s;
			if (!s) return;
			for (const q of this._reqs) q.reject(new TypeError('released'));
			this._reqs = [];
			if (s._state === 'readable') this._closed.reject(new TypeError('released'));
			this._closed = deferred();
			this._closed.reject(new TypeError('released'));
			s._reader = null;
			this._s = null;
		}
	}
	/* a BYOB reader: the chunks (bytes) copied into the view given */
	class ReadableStreamBYOBReader extends ReadableStreamDefaultReader {
		read(view) {
			if (!ArrayBuffer.isView(view) || !view.byteLength) return P.reject(new TypeError('a view is needed'));
			if (this._rest && this._rest.byteLength) return P.resolve(this._fill(view, this._rest));
			return super.read().then(r => {
				if (r.done) return { value: new view.constructor(view.buffer, view.byteOffset, 0), done: true };
				const b = r.value instanceof ArrayBuffer ? new Uint8Array(r.value) :
					new Uint8Array(r.value.buffer, r.value.byteOffset, r.value.byteLength);
				return this._fill(view, b);
			});
		}
		_fill(view, bytes) {
			const dst = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
			const n = Math.min(dst.length, bytes.length) - (Math.min(dst.length, bytes.length) % (view.BYTES_PER_ELEMENT || 1));
			dst.set(bytes.subarray(0, n));
			this._rest = bytes.subarray(n);
			return { value: new view.constructor(view.buffer, view.byteOffset, n / (view.BYTES_PER_ELEMENT || 1)), done: false };
		}
	}

	class WritableStreamDefaultController {
		constructor(stream, sink, hwm, size) {
			this._s = stream; this._sink = sink; this._hwm = hwm; this._size = size || (() => 1);
			this._abort = new G.AbortController();
		}
		get signal() { return this._abort.signal; }
		error(e) { this._s._error(e); }
	}
	class WritableStream {
		constructor(sink = {}, strategy = {}) {
			this._state = 'writable';
			this._writer = null;
			this._queue = [];
			this._inflight = 0;
			this._storedError = undefined;
			const hwm = strategy.highWaterMark !== undefined ? +strategy.highWaterMark : 1;
			this._c = new WritableStreamDefaultController(this, sink, hwm, strategy.size);
			this._chain = P.resolve().then(() => sink.start ? sink.start(this._c) : undefined)
				.catch(e => this._error(e));
		}
		get locked() { return !!this._writer; }
		_error(e) {
			if (this._state === 'errored') return;
			this._state = 'errored';
			this._storedError = e;
			if (this._writer) { this._writer._closed.reject(e); this._writer._ready.reject(e); }
		}
		_write(chunk) {
			if (this._state !== 'writable') return P.reject(this._storedError || new TypeError('the stream is closed'));
			this._inflight++;
			const p = this._chain = this._chain.then(() => {
				if (this._state === 'errored') throw this._storedError;
				return this._c._sink.write ? this._c._sink.write(chunk, this._c) : undefined;
			}).then(() => { this._inflight--; }, e => { this._inflight--; this._error(e); throw e; });
			return p;
		}
		_close() {
			if (this._state !== 'writable') return P.reject(new TypeError('the stream is closed'));
			this._state = 'closing';
			return this._chain = this._chain.then(() => this._c._sink.close ? this._c._sink.close() : undefined)
				.then(() => { this._state = 'closed'; if (this._writer) this._writer._closed.resolve(); },
					e => { this._error(e); throw e; });
		}
		abort(reason) {
			if (this._writer) return P.reject(new TypeError('the stream is locked'));
			return this._abortNow(reason);
		}
		_abortNow(reason) {
			if (this._state === 'closed' || this._state === 'errored') return P.resolve();
			this._c._abort.abort(reason);
			this._error(reason);
			return P.resolve(this._c._sink.abort ? this._c._sink.abort(reason) : undefined).then(() => undefined);
		}
		close() {
			if (this._writer) return P.reject(new TypeError('the stream is locked'));
			return this._close();
		}
		getWriter() {
			if (this._writer) throw new TypeError('the stream is locked');
			return new WritableStreamDefaultWriter(this);
		}
	}
	class WritableStreamDefaultWriter {
		constructor(stream) {
			if (stream._writer) throw new TypeError('the stream is locked');
			this._s = stream;
			stream._writer = this;
			this._closed = deferred();
			this._ready = deferred();
			this._ready.resolve();
			if (stream._state === 'errored') { this._closed.reject(stream._storedError); }
			if (stream._state === 'closed') this._closed.resolve();
		}
		get closed() { return this._closed.promise; }
		get ready() { return this._ready.promise; }
		get desiredSize() { const s = this._s; return !s || s._state === 'errored' ? null : s._c._hwm - s._inflight; }
		write(chunk) { return this._s ? this._s._write(chunk) : P.reject(new TypeError('released')); }
		close() { return this._s ? this._s._close() : P.reject(new TypeError('released')); }
		abort(r) { return this._s ? this._s._abortNow(r) : P.reject(new TypeError('released')); }
		releaseLock() { if (this._s) { this._s._writer = null; this._s = null; } }
	}

	class TransformStream {
		constructor(transformer = {}, wStrategy = {}, rStrategy = {}) {
			let rc;
			this.readable = new ReadableStream({ start(c) { rc = c; },
				cancel: r => transformer.cancel ? transformer.cancel(r) : undefined }, rStrategy);
			const ctrl = {
				enqueue: c => rc.enqueue(c),
				error: e => { rc.error(e); this.writable._error(e); },
				terminate: () => { try { rc.close(); } catch (e) {} },
				get desiredSize() { return rc.desiredSize; },
			};
			const started = P.resolve().then(() => transformer.start ? transformer.start(ctrl) : undefined);
			this.writable = new WritableStream({
				write: chunk => started.then(() => transformer.transform ?
					transformer.transform(chunk, ctrl) : ctrl.enqueue(chunk)),
				close: () => started.then(() => transformer.flush ? transformer.flush(ctrl) : undefined)
					.then(() => { try { rc.close(); } catch (e) {} }),
				abort: r => rc.error(r),
			}, wStrategy);
		}
	}
	class TextEncoderStream extends TransformStream {
		constructor() {
			const enc = new G.TextEncoder();
			super({ transform(chunk, c) { const s = String(chunk); if (s) c.enqueue(enc.encode(s)); } });
			this.encoding = 'utf-8';
		}
	}
	class TextDecoderStream extends TransformStream {
		constructor(label = 'utf-8', opts = {}) {
			const dec = new G.TextDecoder(label, opts);
			super({
				transform(chunk, c) { const s = dec.decode(chunk, { stream: true }); if (s) c.enqueue(s); },
				flush(c) { const s = dec.decode(); if (s) c.enqueue(s); },
			});
			this.encoding = dec.encoding;
		}
	}
	Object.assign(G, { ReadableStream, ReadableStreamDefaultReader, ReadableStreamBYOBReader,
		ReadableStreamDefaultController, WritableStream, WritableStreamDefaultWriter,
		WritableStreamDefaultController, TransformStream, CountQueuingStrategy,
		ByteLengthQueuingStrategy, TextEncoderStream, TextDecoderStream });

	/* a Response's / a Blob's body as a stream (its bytes, one chunk) */
	/* (read at the first read only: a page may test "response.body" and then call json()) */
	const bytesStream = get => new ReadableStream({
		pull(c) {
			return get().then(buf => { if (buf.byteLength) c.enqueue(new Uint8Array(buf)); c.close(); },
				e => c.error(e));
		},
	}, { highWaterMark: 0 });
	for (const C of [G.Response]) {
		if (!C) continue;
		const bodyKey = Symbol('body');
		Object.defineProperty(C.prototype, 'body', { configurable: true,
			get() {
				if (this[bodyKey] === undefined) {
					const self = this;
					const has = typeof self.arrayBuffer === 'function' && self._body !== '' &&
						self.status !== 204 && self.status !== 304;
					Object.defineProperty(this, bodyKey, { value: has ?
						bytesStream(() => self.arrayBuffer()) : null });
				}
				return this[bodyKey];
			},
			set(v) {} });
	}
	streamOfBytes = bytesStream;
}


/* ---- Blob / File (bytes), FileReader, blob: URLs ------------------------------------------ */
{
	const OldBlob = G.Blob;
	const enc = new G.TextEncoder(), dec = new G.TextDecoder();
	const bytesOf = b => b._b ? b._b : enc.encode(b._s || '');
	class Blob {
		constructor(parts = [], opts = {}) {
			if (parts === null || typeof parts !== 'object' || !(Symbol.iterator in parts))
				throw new TypeError('Blob: parts must be a sequence');
			const chunks = [];
			let n = 0;
			const endings = opts && opts.endings === 'native';
			for (const p of parts) {
				let b;
				if (p instanceof ArrayBuffer) b = new Uint8Array(p.slice(0));
				else if (ArrayBuffer.isView(p)) b = new Uint8Array(p.buffer.slice(p.byteOffset, p.byteOffset + p.byteLength));
				else if (OldBlob && p instanceof OldBlob) b = bytesOf(p);
				else b = enc.encode(endings ? String(p).replace(/\r\n|\r/g, '\n') : String(p));
				chunks.push(b);
				n += b.length;
			}
			const all = new Uint8Array(n);
			let o = 0;
			for (const c of chunks) { all.set(c, o); o += c.length; }
			Object.defineProperty(this, '_b', { value: all });
			const t = opts && opts.type !== undefined ? String(opts.type) : '';
			Object.defineProperty(this, 'type', { value: /^[\x20-\x7e]*$/.test(t) ? t.toLowerCase() : '', enumerable: true });
		}
		get size() { return this._b.length; }
		get _s() { return dec.decode(this._b); }
		text() { return Promise.resolve(dec.decode(this._b)); }
		arrayBuffer() { return Promise.resolve(this._b.slice().buffer); }
		bytes() { return Promise.resolve(this._b.slice()); }
		slice(start = 0, end = this._b.length, type = '') {
			const n = this._b.length;
			const rel = v => v < 0 ? Math.max(n + v, 0) : Math.min(v, n);
			return new Blob([this._b.subarray(rel(start), rel(end))], { type });
		}
		stream() { return streamOfBytes(() => this.arrayBuffer()); }
		get [Symbol.toStringTag]() { return 'Blob'; }
	}
	/* dom.js's own Blobs (fetch's blob(), XHR) are Blobs too, with the same methods */
	if (OldBlob) {
		Object.setPrototypeOf(Blob.prototype, OldBlob.prototype);
		def(OldBlob.prototype, {
			arrayBuffer() { return Promise.resolve(bytesOf(this).slice().buffer); },
			bytes() { return Promise.resolve(bytesOf(this).slice()); },
			stream() { return streamOfBytes(() => this.arrayBuffer()); },
		});
		Object.defineProperty(Blob, Symbol.hasInstance, { value: v => v instanceof OldBlob });
	}
	class File extends Blob {
		constructor(parts, name, opts = {}) {
			super(parts, opts);
			if (name === undefined) throw new TypeError('File: a name is needed');
			Object.defineProperty(this, 'name', { value: String(name), enumerable: true });
			Object.defineProperty(this, 'lastModified', { value: opts.lastModified !== undefined ? +opts.lastModified : Date.now(), enumerable: true });
			Object.defineProperty(this, 'webkitRelativePath', { value: '', enumerable: true });
		}
		get [Symbol.toStringTag]() { return 'File'; }
	}
	G.Blob = Blob;
	G.File = File;

	class FileReader extends G.EventTarget {
		constructor() {
			super();
			this.readyState = 0;
			this.result = null;
			this.error = null;
			this._id = 0;
		}
		_read(blob, how, label) {
			if (!(blob instanceof Blob)) throw new TypeError('FileReader: not a Blob');
			if (this.readyState === 1) throw domError('already reading', 'InvalidStateError');
			this.readyState = 1;
			this.result = null;
			this.error = null;
			const id = ++this._id;
			const ev = t => { const e = new G.ProgressEvent(t, { lengthComputable: true, loaded: blob.size, total: blob.size }); this.dispatchEvent(e); };
			task(() => {
				if (id !== this._id) return;
				ev('loadstart');
				const b = bytesOf(blob);
				task(() => {
					if (id !== this._id) return;
					switch (how) {
					case 'text': this.result = new G.TextDecoder(label || 'utf-8').decode(b); break;
					case 'buffer': this.result = b.slice().buffer; break;
					case 'binary': { let s = ''; for (let i = 0; i < b.length; i += 8192) s += String.fromCharCode.apply(null, b.subarray(i, i + 8192)); this.result = s; break; }
					case 'url': {
						let s = '';
						for (let i = 0; i < b.length; i += 8192) s += String.fromCharCode.apply(null, b.subarray(i, i + 8192));
						this.result = 'data:' + (blob.type || 'application/octet-stream') + ';base64,' + G.btoa(s);
						break;
					}
					}
					this.readyState = 2;
					ev('progress');
					ev('load');
					if (this.readyState === 2) ev('loadend');
				});
			});
		}
		readAsText(b, label) { this._read(b, 'text', label); }
		readAsArrayBuffer(b) { this._read(b, 'buffer'); }
		readAsBinaryString(b) { this._read(b, 'binary'); }
		readAsDataURL(b) { this._read(b, 'url'); }
		abort() {
			if (this.readyState !== 1) { this.result = null; return; }
			this._id++;
			this.readyState = 2;
			this.result = null;
			this.dispatchEvent(new G.ProgressEvent('abort'));
			this.dispatchEvent(new G.ProgressEvent('loadend'));
		}
	}
	def(FileReader, { EMPTY: 0, LOADING: 1, DONE: 2 });
	def(FileReader.prototype, { EMPTY: 0, LOADING: 1, DONE: 2 });
	for (const t of ['loadstart', 'progress', 'load', 'abort', 'error', 'loadend'])
		handlerProperty(FileReader.prototype, t);
	G.FileReader = FileReader;
	G.FileReaderSync = undefined;
	delete G.FileReaderSync;

	/* blob: URLs -- fetch() reads them */
	const blobURLs = new Map();
	def(G.URL, {
		createObjectURL(obj) {
			if (!(obj instanceof Blob)) throw new TypeError('createObjectURL: not a Blob');
			const u = 'blob:' + G.location.origin + '/' + G.crypto.randomUUID();
			blobURLs.set(u, obj);
			return u;
		},
		revokeObjectURL(u) { blobURLs.delete(String(u)); },
	});
	/* (Onyx: <a download href="blob:...">, clicked or click()ed: the bytes saved -- the
	 * frontend asks where; qjs.c n_download) */
	Object.defineProperty(G, '__onyxBlobDownload', { configurable: true, value(u, name) {
		const b = blobURLs.get(String(u).replace(/#.*$/, ''));
		if (b) N.download(bytesOf(b), name || '', b.type || '');
	} });
	const nativeFetch = G.fetch;
	G.fetch = function fetch(input, init) {
		const u = typeof input === 'string' ? input : input && input.url !== undefined ? input.url : String(input);
		if (/^blob:/i.test(u)) {
			const b = blobURLs.get(u.replace(/#.*$/, ''));
			if (!b) return Promise.reject(new TypeError('Failed to fetch'));
			return b.arrayBuffer().then(buf => new G.Response(buf, { status: 200,
				headers: { 'Content-Type': b.type, 'Content-Length': String(b.size) } }));
		}
		return nativeFetch.call(this, input, init);
	};
}


/* ---- custom elements: customElements.define, the upgrades, the lifecycle callbacks ----------
 * A defined name's elements get the class's prototype (TAGS: the wrappers qjs.c makes), its
 * constructor runs on createElement / new, and on the elements already there (an upgrade)
 * once they are in the document; connectedCallback / disconnectedCallback on insertion and
 * removal (the scripts' DOM calls here, the parser's through qjs.c's ceHook);
 * attributeChangedCallback for observedAttributes. Customized built-in elements
 * ({ extends }) are recorded but not upgraded, as in Safari. */
const ceDefs = new Map();		/* name -> definition */
const ceByCtor = new Map();		/* constructor -> definition */
const ceState = new WeakMap();		/* element -> { def, connected } (upgraded ones) */
const ceWaiting = new Map();		/* name -> [resolve, promise] of whenDefined */
let ceDefining = false;
const RESERVED_CE = ['annotation-xml', 'color-profile', 'font-face', 'font-face-src',
	'font-face-uri', 'font-face-format', 'font-face-name', 'missing-glyph'];
function validCEName(n) {
	return /^[a-z][.0-9_a-z·À-ÖØ-öø-ͽͿ-῿‌‍‿⁀⁰-↏Ⰰ-⿯、-퟿豈-﷏ﷰ-�-]*-[.0-9_a-z·À-ÖØ-öø-ͽͿ-῿‌‍‿⁀⁰-↏Ⰰ-⿯、-퟿豈-﷏ﷰ-�\u{10000}-\u{effff}-]*$/u.test(n) &&
		!RESERVED_CE.includes(n);
}
function ceCall(el, def, cb, args) {
	const f = def.callbacks[cb];
	if (typeof f === 'function') {
		try { f.apply(el, args); } catch (e) { report(e); }
	}
}
function ceIsConnected(el) {
	/* (Onyx: shadow-including -- a shadow root's elements through its host) */
	for (let n = el; n; n = N.parent(n) || N.shadowHost(n))
		if (N.type(n) === DOCUMENT_NODE)
			return n === G.document;
	return false;
}
/* the constructor run on an element already there */
function ceUpgrade(el, def) {
	if (ceState.has(el) || def.failed.has(el)) return;
	Object.setPrototypeOf(el, def.ctor.prototype);
	def.building.push(el);	/* (the element being upgraded: its constructor runs) */
	try {
		const r = Reflect.construct(def.ctor, []);
		if (r !== el) throw domError('custom element constructor returned another object', 'InvalidStateError');
	} catch (e) {
		def.failed.add(el);
		report(e);
		return;
	} finally {
		def.building.pop();
	}
	ceState.set(el, { def, connected: false });
	if (def.observed.size)
		for (const k of def.observed) {
			const v = N.attr(el, k);
			if (v !== null) ceCall(el, def, 'attributeChangedCallback', [k, null, v, null]);
		}
	ceCheckOne(el);
}
/* connected / disconnected as the element is now */
function ceCheckOne(el) {
	const s = ceState.get(el);
	if (!s) {
		const def = N.nsURI(el) === NS_HTML ? ceDefs.get(N.lname(el)) : undefined;
		if (def && !def.builtin && ceIsConnected(el)) ceUpgrade(el, def);
		return;
	}
	const now = ceIsConnected(el);
	if (now !== s.connected) {
		s.connected = now;
		ceCall(el, s.def, now ? 'connectedCallback' : 'disconnectedCallback', []);
	}
}
/* the element and its descendants (in tree order) */
function ceCheck(n) {
	if (!ceDefs.size || !n) return;
	const t = N.type(n);
	if (t === ELEMENT_NODE) ceCheckOne(n);
	if (t === ELEMENT_NODE || t === DOCUMENT_FRAGMENT_NODE || t === DOCUMENT_NODE) {
		const all = Element.prototype.querySelectorAll.call(n, '*');
		for (const e of all)
			ceCheckOne(e);
		/* (Onyx: and the shadow trees in it, shadow-including tree order) */
		if (shadowSeen) {
			if (t === ELEMENT_NODE && N.shadowRoot(n)) ceCheck(N.shadowRoot(n));
			for (const e of all) if (N.shadowRoot(e)) ceCheck(N.shadowRoot(e));
		}
	}
}
/* the elements a removal took out of the document: their disconnectedCallback */
function ceRemoved(n) {
	if (!ceDefs.size || (N.type(n) !== ELEMENT_NODE && N.type(n) !== DOCUMENT_FRAGMENT_NODE)) return;
	const all = [...(N.type(n) === ELEMENT_NODE ? [n] : []), ...Element.prototype.querySelectorAll.call(n, '*')];
	for (const e of all) {
		if (ceState.has(e)) ceCheckOne(e);
		if (shadowSeen && N.shadowRoot(e)) ceRemoved(N.shadowRoot(e));	/* (Onyx) */
	}
}

/* the natives the DOM calls go through: insertion and removal seen */
{
	const insert = N.insert, remove = N.remove, setHTML = N.setHTML, setText = N.setText;
	N.insert = function (p, c, ref) {
		if (!ceDefs.size) return insert(p, c, ref);
		const frag = N.type(c) === DOCUMENT_FRAGMENT_NODE ? N.children(c) : null;
		let r;
		ceInScript++;	/* (qjs.c's hook sees this insertion too: skipped) */
		try { r = insert(p, c, ref); } finally { ceInScript--; }
		if (frag) for (const k of frag) ceCheck(k); else ceCheck(c);
		return r;
	};
	N.remove = function (p, c) {
		const r = remove(p, c);
		if (ceDefs.size) ceRemoved(c);
		return r;
	};
	const replacing = (fn) => function (el, ...args) {
		if (!ceDefs.size) return fn(el, ...args);
		const old = N.children(el);
		ceInScript++;
		let r;
		try { r = fn(el, ...args); } finally { ceInScript--; }
		for (const k of old) ceRemoved(k);
		ceCheck(el);
		return r;
	};
	N.setHTML = replacing(setHTML);
	N.setText = replacing(setText);
}
let ceInScript = 0;
/* the parser's insertions (qjs.c: js_handle_new_element) */
function ceParserHook(el) {
	if (ceInScript || !ceDefs.size) return;
	ceCheck(el);
}

/* new MyElement(), createElement('my-element'), and the upgrades: the HTMLElement
 * constructor makes (or hands back) the element */
{
	const OldHTMLElement = HTMLElement;
	const HTMLElementCE = function HTMLElement() {
		const nt = new.target;
		if (!nt) throw new TypeError("Failed to construct 'HTMLElement': use 'new'");
		const def = ceByCtor.get(nt);
		if (!def) throw new TypeError('Illegal constructor');
		if (def.building.length) {
			const el = def.building[def.building.length - 1];
			if (el !== null) {
				def.building[def.building.length - 1] = null;	/* (the constructor ran once) */
				Object.setPrototypeOf(el, nt.prototype);
				return el;
			}
		}
		const el = mainCreate.createElement.call(G.document, def.name);
		Object.setPrototypeOf(el, nt.prototype);
		ceState.set(el, { def, connected: false });
		return el;
	};
	HTMLElementCE.prototype = OldHTMLElement.prototype;
	Object.defineProperty(OldHTMLElement.prototype, 'constructor', { value: HTMLElementCE,
		writable: true, configurable: true });
	Object.setPrototypeOf(HTMLElementCE, Object.getPrototypeOf(OldHTMLElement));
	G.HTMLElement = HTMLElementCE;
}

class CustomElementRegistry {
	define(name, ctor, options) {
		name = String(name);
		if (typeof ctor !== 'function' || !ctor.prototype)
			throw new TypeError('customElements.define: not a constructor');
		if (!validCEName(name))
			throw domError('"' + name + '" is not a valid custom element name', 'SyntaxError');
		if (ceDefs.has(name))
			throw domError('"' + name + '" has already been defined', 'NotSupportedError');
		if (ceByCtor.has(ctor))
			throw domError('this constructor has already been defined', 'NotSupportedError');
		if (ceDefining)
			throw domError('customElements.define: already defining', 'NotSupportedError');
		const ext = options && options.extends !== undefined ? String(options.extends) : null;
		ceDefining = true;
		let def;
		try {
			const proto = ctor.prototype;
			if (proto === null || typeof proto !== 'object') throw new TypeError('prototype is not an object');
			const callbacks = {};
			for (const k of ['connectedCallback', 'disconnectedCallback', 'adoptedCallback',
					'attributeChangedCallback', 'connectedMoveCallback'])
				callbacks[k] = proto[k];
			let observed = [];
			if (typeof callbacks.attributeChangedCallback === 'function' && ctor.observedAttributes !== undefined)
				observed = [...ctor.observedAttributes].map(String);
			def = { name, ctor, callbacks, observed: new Set(observed), builtin: ext,
				failed: new WeakSet(), building: [], formAssociated: !!ctor.formAssociated };
		} finally {
			ceDefining = false;
		}
		ceDefs.set(name, def);
		ceByCtor.set(ctor, def);
		if (!ext) {
			if (!ceHooked) { N.ceHook(ceParserHook); ceHooked = true; }
			/* the elements already in the document, in tree order */
			for (const e of [...G.document.getElementsByTagName(name)])
				if (N.nsURI(e) === NS_HTML) ceCheckOne(e);
		}
		const w = ceWaiting.get(name);
		if (w) { ceWaiting.delete(name); w[0](ctor); }
	}
	get(name) { const d = ceDefs.get(String(name)); return d ? d.ctor : undefined; }
	getName(ctor) { const d = ceByCtor.get(ctor); return d ? d.name : null; }
	whenDefined(name) {
		name = String(name);
		if (!validCEName(name))
			return Promise.reject(domError('"' + name + '" is not a valid custom element name', 'SyntaxError'));
		const d = ceDefs.get(name);
		if (d) return Promise.resolve(d.ctor);
		let w = ceWaiting.get(name);
		if (!w) {
			let res;
			const p = new Promise(r => { res = r; });
			w = [res, p];
			ceWaiting.set(name, w);
		}
		return w[1];
	}
	upgrade(root) {
		const all = N.type(root) === ELEMENT_NODE ? [root] : [];
		all.push(...Element.prototype.querySelectorAll.call(root, '*'));
		for (const e of all) {
			const def = N.nsURI(e) === NS_HTML ? ceDefs.get(N.lname(e)) : undefined;
			if (def && !def.builtin) ceUpgrade(e, def);
		}
	}
}
let ceHooked = false;
const customElements = new CustomElementRegistry();
G.CustomElementRegistry = CustomElementRegistry;
Object.defineProperty(G, 'customElements', { value: customElements, writable: true, configurable: true });
/* createElement of a defined name: its constructor runs */
{
	const create = G.Document.prototype.createElement;
	def(G.Document.prototype, {
		createElement(name, options) {
			if (ceDefs.size && isMainDoc(this)) {
				const d = ceDefs.get(String(name).toLowerCase());
				if (d && !d.builtin) {
					const el = new d.ctor();
					return el;
				}
			}
			return create.call(this, name, options);
		},
	});
}
/* ElementInternals: the form-associated custom elements' value and validity */
class ElementInternals {
	constructor(el) { Object.defineProperty(this, '_el', { value: el }); this._value = null; this._msg = ''; this._flags = {}; }
	setFormValue(v) { this._value = v; }
	get form() { return this._el.closest ? this._el.closest('form') : null; }
	setValidity(flags = {}, message = '') { this._flags = Object.assign({}, flags); this._msg = String(message); }
	get willValidate() { return true; }
	get validity() {
		const f = this._flags, v = {};
		for (const k of ['valueMissing', 'typeMismatch', 'patternMismatch', 'tooLong', 'tooShort',
				'rangeUnderflow', 'rangeOverflow', 'stepMismatch', 'badInput', 'customError'])
			v[k] = !!f[k];
		v.valid = !Object.values(v).some(Boolean);
		return v;
	}
	get validationMessage() { return this.validity.valid ? '' : this._msg; }
	checkValidity() {
		if (this.validity.valid) return true;
		this._el.dispatchEvent(new G.Event('invalid', { cancelable: true }));
		return false;
	}
	reportValidity() { return this.checkValidity(); }
	get labels() { return []; }
	get states() { return this._states || (this._states = new Set()); }
	get shadowRoot() { return N.shadowRoot(this._el); }	/* (Onyx: open or closed) */
}
G.ElementInternals = ElementInternals;
def(HTMLElement.prototype, {
	attachInternals() {
		const s = ceState.get(this);
		if (!s) throw domError('attachInternals: not a custom element', 'NotSupportedError');
		if (s.internals) throw domError('attachInternals: already attached', 'NotSupportedError');
		return (s.internals = new ElementInternals(this));
	},
});

/* ---- shadow DOM: attachShadow, ShadowRoot, slots, events across the shadow trees -----------
 * A shadow root is a libdom document fragment its host keeps (qjs.c's N.attachShadow...;
 * the parser's <template shadowrootmode> makes them too): no child of its host, so the
 * host's children, its serialization and the document's selectors never see it. NetSurf
 * draws the flat tree and scopes the styles to each tree (html/onyx_shadow.c). Here the DOM
 * of it: ShadowRoot (mode, host, innerHTML, adoptedStyleSheets, getHTML...), <slot> (the
 * named assignment and the manual one: assignedNodes / assignedElements / assign(),
 * slotchange), element.slot / assignedSlot / part, getRootNode({ composed }), isConnected
 * through the hosts, the events' path through the slots and the hosts with their target
 * (and relatedTarget) retargeted at each node, composedPath() hiding the closed trees, a
 * non-composed event stopping at its shadow root, document.activeElement retargeted,
 * delegatesFocus; the custom elements of a shadow tree upgraded and connected. */
const SR_CLOSED = 1, SR_DELEGATES_FOCUS = 2, SR_CLONABLE = 4, SR_SERIALIZABLE = 8,
	SR_MANUAL = 16, SR_DECLARATIVE = 32;
let shadowSeen = false;
/* the document has shadow roots (a script attached one, or the parser) */
function shadowOn() { return shadowSeen || (shadowSeen = N.hasShadow()); }
function isShadowRoot(n) {
	return n !== null && n !== undefined && N.type(n) === DOCUMENT_FRAGMENT_NODE &&
		N.shadowHost(n) !== null;
}
function treeRoot(n) { let r = n; for (let p; (p = N.parent(r));) r = p; return r; }
function composedParent(n) { const p = N.parent(n); return p !== null ? p : N.shadowHost(n); }
function shadowIncludingRoot(n) { let r = n; for (let p; (p = composedParent(r));) r = p; return r; }
/* a is a shadow-including inclusive ancestor of b */
function shadowIncludingAncestor(a, b) {
	for (let n = b; n; n = composedParent(n)) if (n === a) return true;
	return false;
}
/* the DOM standard's retarget: a, as seen from b */
function retarget(a, b) {
	for (;;) {
		if (!(a instanceof Node)) return a;
		const root = treeRoot(a), host = N.shadowHost(root);
		if (host === null) return a;
		if (b instanceof Node && shadowIncludingAncestor(root, b)) return a;
		a = host;
	}
}

/* slots: the named assignment (the first slot of the name in tree order), the manual one */
function isSlot(n) { return N.type(n) === ELEMENT_NODE && N.nsURI(n) === NS_HTML && N.lname(n) === 'slot'; }
function slotName(s) { return N.attr(s, 'name') || ''; }
function slotsOf(root) {
	return N.descendants(root).filter(e => N.nsURI(e) === NS_HTML && N.lname(e) === 'slot');
}
function slottableName(n) {
	const t = N.type(n);
	return t === ELEMENT_NODE ? N.attr(n, 'slot') || '' : t === TEXT_NODE ? '' : null;
}
const manualSlots = new WeakMap();	/* slot -> the nodes assign() gave it */
function assignedSlotOf(n) {
	const p = N.parent(n);
	if (p === null || N.type(p) !== ELEMENT_NODE) return null;
	const root = N.shadowRoot(p);
	if (root === null) return null;
	const name = slottableName(n);
	if (name === null) return null;
	if (N.shadowFlags(root) & SR_MANUAL) {
		for (const s of slotsOf(root)) {
			const m = manualSlots.get(s);
			if (m && m.includes(n)) return s;
		}
		return null;
	}
	for (const s of slotsOf(root)) if (slotName(s) === name) return s;
	return null;
}
function slotAssigned(slot) {
	const root = treeRoot(slot), host = N.shadowHost(root), out = [];
	if (host === null) return out;
	if (N.shadowFlags(root) & SR_MANUAL) {
		for (const n of manualSlots.get(slot) || [])
			if (N.parent(n) === host && assignedSlotOf(n) === slot) out.push(n);
		return out;
	}
	const name = slotName(slot);
	if (slotsOf(root).find(s => slotName(s) === name) !== slot) return out;
	for (const c of N.children(host)) if (slottableName(c) === name) out.push(c);
	return out;
}
/* the flattened slottables: a slot assigned is replaced by its own, a slot without any by
 * its fallback content */
function slotFlattened(slot) {
	if (!isShadowRoot(treeRoot(slot))) return [];
	let list = slotAssigned(slot);
	if (list.length === 0)
		list = N.children(slot).filter(c => slottableName(c) !== null);
	const out = [];
	for (const n of list) {
		if (isSlot(n) && isShadowRoot(treeRoot(n))) out.push(...slotFlattened(n));
		else out.push(n);
	}
	return out;
}

/* slotchange: the slots whose assigned nodes changed, told at the next microtask */
const slotLast = new WeakMap();
const slotPending = new Set();
let slotQueued = false;
function scheduleSlots(root) {
	if (!isShadowRoot(root)) return;
	slotPending.add(root);
	if (!slotQueued) {
		slotQueued = true;
		Promise.resolve().then(flushSlots);
	}
}
function flushSlots() {
	slotQueued = false;
	const roots = [...slotPending];
	slotPending.clear();
	for (const root of roots) {
		for (const s of slotsOf(root)) {
			const now = slotAssigned(s), old = slotLast.get(s) || [];
			slotLast.set(s, now);
			if (now.length !== old.length || now.some((n, i) => n !== old[i])) {
				try { s.dispatchEvent(new G.Event('slotchange', { bubbles: true })); }
				catch (e) { report(e); }
			}
		}
	}
}
/* a change under p: its shadow tree's slots (p a host), p's own tree's (a slot moved) */
function slotsTouched(p) {
	if (p === null || p === undefined) return;
	if (N.type(p) === ELEMENT_NODE) {
		const r = N.shadowRoot(p);
		if (r !== null) scheduleSlots(r);
	}
	const root = treeRoot(p);
	if (N.type(root) === DOCUMENT_FRAGMENT_NODE) scheduleSlots(root);
}
{
	const insert = N.insert, remove = N.remove, setHTML = N.setHTML, setText = N.setText,
		setAttr = N.setAttr, removeAttr = N.removeAttr;
	N.insert = function (p, c, ref) {
		const old = shadowSeen ? N.parent(c) : null;
		const r = insert(p, c, ref);
		if (shadowOn()) { slotsTouched(p); if (old) slotsTouched(old); }
		return r;
	};
	N.remove = function (p, c) {
		const r = remove(p, c);
		if (shadowSeen) slotsTouched(p);
		return r;
	};
	N.setHTML = function (el, ...args) {
		const r = setHTML(el, ...args);
		if (shadowOn()) slotsTouched(el);
		return r;
	};
	N.setText = function (el, ...args) {
		const r = setText(el, ...args);
		if (shadowSeen) slotsTouched(el);
		return r;
	};
	const attrTouched = (el, k) => {
		if (shadowSeen && (k === 'slot' || k === 'name')) {
			slotsTouched(N.parent(el));
			slotsTouched(el);
		}
	};
	N.setAttr = function (el, k, v) { const r = setAttr(el, k, v); attrTouched(el, k); return r; };
	N.removeAttr = function (el, k) { const r = removeAttr(el, k); attrTouched(el, k); return r; };
}

/* a shadow root's children replaced by html parsed in its host's context */
function setRootHTML(root, html) {
	const removed = I.observers && I.observers.size ? N.children(root) : [];
	N.setHTML(root, String(html), N.shadowHost(root));
	if (I.observers && I.observers.size)
		I.childListRecord(root, N.children(root), removed);
}
/* getHTML({ serializableShadowRoots, shadowRoots }): the shadow roots asked for serialized
 * as declarative shadow roots */
function getHTMLOf(n, opts) {
	const want = new Set(opts && opts.shadowRoots ? opts.shadowRoots : []);
	const ser = !!(opts && opts.serializableShadowRoots);
	if (!shadowSeen || (!ser && want.size === 0)) return innerHTMLOf(n);
	const out = [];
	const inner = (e) => {
		const r = N.type(e) === ELEMENT_NODE ? N.shadowRoot(e) : null;
		if (r !== null && ((ser && (N.shadowFlags(r) & SR_SERIALIZABLE)) || want.has(r))) {
			const f = N.shadowFlags(r);
			out.push('<template shadowrootmode="', f & SR_CLOSED ? 'closed' : 'open', '"');
			if (f & SR_DELEGATES_FOCUS) out.push(' shadowrootdelegatesfocus=""');
			if (f & SR_SERIALIZABLE) out.push(' shadowrootserializable=""');
			if (f & SR_CLONABLE) out.push(' shadowrootclonable=""');
			out.push('>');
			kids(r);
			out.push('</template>');
		}
		kids(isTemplate(e) ? N.templateContent(e) || e : e);
	};
	const kids = (p) => {
		for (const c of N.children(p)) {
			if (N.type(c) !== ELEMENT_NODE) { serializeNode(c, out); continue; }
			const ns = N.nsURI(c);
			const tag = ns === NS_HTML || ns === NS_SVG || ns === NS_MATHML ? N.lname(c) : N.qname(c);
			out.push('<', tag);
			for (const [q, v, ans, local] of N.attrsNS(c))
				out.push(' ', attrName(q, ans, local), '="', escapeAttr(v), '"');
			out.push('>');
			if (ns === NS_HTML && VOID.has(tag)) continue;
			inner(c);
			out.push('</', tag, '>');
		}
	};
	inner(n);
	return out.join('');
}

class ShadowRoot extends G.DocumentFragment {
	constructor() { throw new TypeError('Illegal constructor'); }
	get mode() { return N.shadowFlags(this) & SR_CLOSED ? 'closed' : 'open'; }
	get host() { return N.shadowHost(this); }
	get delegatesFocus() { return !!(N.shadowFlags(this) & SR_DELEGATES_FOCUS); }
	get clonable() { return !!(N.shadowFlags(this) & SR_CLONABLE); }
	get serializable() { return !!(N.shadowFlags(this) & SR_SERIALIZABLE); }
	get slotAssignment() { return N.shadowFlags(this) & SR_MANUAL ? 'manual' : 'named'; }
	get innerHTML() { return innerHTMLOf(this); }
	set innerHTML(v) { setRootHTML(this, v === null ? '' : String(v)); }
	setHTMLUnsafe(html) { setRootHTML(this, String(html)); }
	getHTML(opts) { return getHTMLOf(this, opts); }
	get activeElement() {
		const f = rawActive();
		if (!f || !f.isConnected) return null;
		for (let a = f; a;) {
			const r = treeRoot(a);
			if (r === this) return a;
			const h = N.shadowHost(r);
			if (h === null) return null;
			a = h;
		}
		return null;
	}
	get styleSheets() {
		const out = [];
		for (const e of N.descendants(this))
			if (N.nsURI(e) === NS_HTML && N.lname(e) === 'style' && e.sheet) out.push(e.sheet);
		return out;
	}
	get adoptedStyleSheets() { return this._adopted || (this._adopted = []); }
	set adoptedStyleSheets(v) {
		const list = Array.from(v || []);
		for (const s of list)
			if (!(s instanceof G.CSSStyleSheet) || !s._constructed)
				throw domError('not a constructed sheet', 'NotAllowedError');
		for (const s of this._adopted || []) {
			const set = sheetRoots.get(s);
			if (set) set.delete(this);
		}
		for (const s of list) {
			let set = sheetRoots.get(s);
			if (!set) sheetRoots.set(s, set = new Set());
			set.add(this);
		}
		Object.defineProperty(this, '_adopted', { value: list, writable: true, configurable: true });
		N.shadowSheets(this, list.map(s => s._src || ''));
	}
	get fullscreenElement() { return null; }
	get pictureInPictureElement() { return null; }
	get pointerLockElement() { return null; }
	getAnimations() { return G.document.getAnimations().filter(a => a.effect && a.effect.target && a.effect.target.getRootNode && a.effect.target.getRootNode() === this); }
	elementFromPoint(x, y) { const e = G.document.elementFromPoint(x, y); return e ? retarget(e, this) : null; }
	elementsFromPoint(x, y) { const e = this.elementFromPoint(x, y); return e ? [e] : []; }
	getSelection() { return G.getSelection ? G.getSelection() : null; }
	get onslotchange() { return this._onslotchange || null; }
	set onslotchange(fn) { setHandler(this, 'slotchange', fn); }
}
G.ShadowRoot = ShadowRoot;
if (N.shadowProto) N.shadowProto(ShadowRoot.prototype);	/* (Onyx: not in a worker's natives) */

/* an on* property kept as a listener */
const handlerFns = new WeakMap();
function setHandler(el, type, fn) {
	let m = handlerFns.get(el);
	if (!m) handlerFns.set(el, m = new Map());
	const old = m.get(type);
	if (old) el.removeEventListener(type, old);
	m.set(type, typeof fn === 'function' ? fn : null);
	if (typeof fn === 'function') el.addEventListener(type, fn);
	Object.defineProperty(el, '_on' + type, { value: m.get(type), writable: true, configurable: true });
}

/* a constructed sheet changed (replaceSync, insertRule...): the shadow roots that adopted
 * it take its new text */
const sheetRoots = new WeakMap();	/* CSSStyleSheet -> Set of shadow roots */
{
	const write = G.CSSStyleSheet.prototype._write;
	def(G.CSSStyleSheet.prototype, {
		_write() {
			write.call(this);
			const set = sheetRoots.get(this);
			if (set)
				for (const r of set)
					N.shadowSheets(r, (r._adopted || []).map(s => s._src || ''));
		},
	});
}

/* the element's own document.activeElement (dom.js keeps it: the focused element) */
const activeDesc = Object.getOwnPropertyDescriptor(G.Document.prototype, 'activeElement');
function rawActive() { return activeDesc && G.document ? activeDesc.get.call(G.document) : null; }
def(G.Document.prototype, {
	get activeElement() {
		const a = activeDesc.get.call(this);
		return shadowSeen && a ? retarget(a, this) : a;
	},
});

def(Element.prototype, {
	attachShadow(init) {
		if (init === null || typeof init !== 'object')
			throw new TypeError("Failed to execute 'attachShadow' on 'Element': 1 argument required");
		const mode = String(init.mode);
		if (mode !== 'open' && mode !== 'closed')
			throw new TypeError("attachShadow: the mode must be 'open' or 'closed'");
		if (N.nsURI(this) !== NS_HTML)
			throw domError('attachShadow: this element does not support it', 'NotSupportedError');
		const d = ceDefs.get(N.lname(this));
		if (d && d.ctor && Array.isArray(d.ctor.disabledFeatures) && d.ctor.disabledFeatures.includes('shadow'))
			throw domError('attachShadow: disabled for this custom element', 'NotSupportedError');
		const cur = N.shadowRoot(this);
		if (cur !== null) {
			const f = N.shadowFlags(cur);
			if ((f & SR_DECLARATIVE) && ((f & SR_CLOSED) ? 'closed' : 'open') === mode && !cur._undeclared) {
				/* a declarative shadow root: emptied and given to the script */
				for (let c; (c = N.first(cur));) N.remove(cur, c);
				Object.defineProperty(cur, '_undeclared', { value: true });
				return cur;
			}
			throw domError('attachShadow: the element is already a shadow host', 'NotSupportedError');
		}
		let flags = mode === 'closed' ? SR_CLOSED : 0;
		if (init.delegatesFocus) flags |= SR_DELEGATES_FOCUS;
		if (init.clonable) flags |= SR_CLONABLE;
		if (init.serializable) flags |= SR_SERIALIZABLE;
		if (init.slotAssignment === 'manual') flags |= SR_MANUAL;
		const r = N.attachShadow(this, flags);
		if (r === null)
			throw domError("attachShadow: '" + N.lname(this) + "' cannot be a shadow host", 'NotSupportedError');
		shadowSeen = true;
		return r;
	},
	get shadowRoot() {
		const r = N.shadowRoot(this);
		return r !== null && !(N.shadowFlags(r) & SR_CLOSED) ? r : null;
	},
	get slot() { return N.attr(this, 'slot') || ''; },
	set slot(v) { this.setAttribute('slot', v); },
	get assignedSlot() { return openSlot(assignedSlotOf(this)); },
	get part() {
		let l = this._partList;
		if (!l) Object.defineProperty(this, '_partList', { value: l = new G.DOMTokenList(this, 'part') });
		return l;
	},
	set part(v) { this.setAttribute('part', v); },
	getHTML(opts) { return getHTMLOf(this, opts); },
});
/* a shadow root is not cloned, imported nor adopted */
{
	const clone = Node.prototype.cloneNode, imp = G.Document.prototype.importNode,
		adopt = G.Document.prototype.adoptNode;
	def(Node.prototype, {
		cloneNode(deep) {
			if (isShadowRoot(this)) throw domError('cloneNode: a shadow root', 'NotSupportedError');
			return clone.call(this, deep);
		},
	});
	def(G.Document.prototype, {
		importNode(n, deep) {
			if (isShadowRoot(n)) throw domError('importNode: a shadow root', 'NotSupportedError');
			return imp.call(this, n, deep);
		},
		adoptNode(n) {
			if (isShadowRoot(n)) throw domError('adoptNode: a shadow root', 'HierarchyRequestError');
			return adopt.call(this, n);
		},
	});
}
/* assignedSlot: null for a slot in a closed shadow tree */
function openSlot(s) {
	return s !== null && !(N.shadowFlags(treeRoot(s)) & SR_CLOSED) ? s : null;
}
def(G.Text.prototype, {
	get assignedSlot() { return openSlot(assignedSlotOf(this)); },
});
def(Node.prototype, {
	getRootNode(opts) {
		return opts && opts.composed ? shadowIncludingRoot(this) : treeRoot(this);
	},
	get isConnected() {
		for (let n = this; n; n = composedParent(n))
			if (N.type(n) === DOCUMENT_NODE) return true;
		return false;
	},
});
def(HTMLElement.prototype, {
	get onslotchange() { return this._onslotchange || null; },
	set onslotchange(fn) { setHandler(this, 'slotchange', fn); },
});
{
	const focus = HTMLElement.prototype.focus;
	def(HTMLElement.prototype, {
		focus(opts) {
			const r = shadowSeen ? N.shadowRoot(this) : null;
			if (r !== null && (N.shadowFlags(r) & SR_DELEGATES_FOCUS)) {
				const f = r.querySelector('a[href], area[href], button:not([disabled]), input:not([disabled]):not([type=hidden]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"]), [contenteditable]');
				if (f) return f.focus(opts);
			}
			return focus.call(this, opts);
		},
	});
}

class HTMLSlotElement extends HTMLElement {
	get name() { return N.attr(this, 'name') || ''; }
	set name(v) { this.setAttribute('name', v); }
	assignedNodes(opts) {
		return opts && opts.flatten ? slotFlattened(this) : slotAssigned(this);
	}
	assignedElements(opts) {
		return this.assignedNodes(opts).filter(n => N.type(n) === ELEMENT_NODE);
	}
	assign(...nodes) {
		for (const n of nodes)
			if (!(n instanceof G.Element) && !(n instanceof G.Text))
				throw new TypeError("assign: not an Element or a Text");
		/* a node is assigned to one slot at a time */
		const root = treeRoot(this);
		for (const s of isShadowRoot(root) ? slotsOf(root) : []) {
			const m = manualSlots.get(s);
			if (m && s !== this) manualSlots.set(s, m.filter(n => !nodes.includes(n)));
		}
		manualSlots.set(this, [...new Set(nodes)]);
		scheduleSlots(root);
		N.setAttr(this, 'data-onyx-assign', String(Date.now()));	/* (drawn again) */
		N.removeAttr(this, 'data-onyx-assign');
	}
}
elementClass('HTMLSlotElement', ['slot'], HTMLElement, HTMLSlotElement);

/* the events: their path through the slots and the hosts (the DOM standard's "get the
 * parent"), their target retargeted at each node, a non-composed event stopping at the
 * shadow root of its target's tree */
function eventParentOf(n, ev, root0) {
	if (n === G) return null;
	const t = N.type(n);
	if (t === DOCUMENT_NODE) return n === G.document ? G : null;
	const p = N.parent(n);
	if (p !== null) {
		if (N.type(p) === ELEMENT_NODE && N.shadowRoot(p) !== null) {
			const s = assignedSlotOf(n);
			if (s !== null) return s;
		}
		return p;
	}
	const host = N.shadowHost(n);
	if (host !== null) return !ev.composed && n === root0 ? null : host;
	return null;
}
function shadowDispatch(target, ev, invoke) {
	const root0 = treeRoot(target), path = [];
	for (let n = target; n; n = eventParentOf(n, ev, root0)) {
		if (n === G && ev.type === 'load' && target !== G) break;
		path.push(n);
	}
	const rel = ev.relatedTarget instanceof Node ? ev.relatedTarget : null;
	const tg = path.map(n => retarget(target, n));
	ev._path = path;
	ev._shadow = true;
	ev._stop = ev._stopNow = false;
	for (let i = path.length - 1; i >= 0 && !ev._stop; i--) {
		ev.target = tg[i];
		if (rel !== null) ev.relatedTarget = retarget(rel, path[i]);
		ev.eventPhase = tg[i] === path[i] ? 2 : 1;
		invoke(path[i], ev, true);
	}
	for (let i = 0; i < path.length && !ev._stop; i++) {
		const at = tg[i] === path[i];
		if (!at && !ev.bubbles) continue;
		ev.target = tg[i];
		if (rel !== null) ev.relatedTarget = retarget(rel, path[i]);
		ev.eventPhase = at ? 2 : 3;
		invoke(path[i], ev, false);
	}
	ev.eventPhase = 0;
	ev.currentTarget = null;
	ev.target = retarget(target, G.document);
	if (rel !== null) ev.relatedTarget = retarget(rel, G.document);
	return !ev.defaultPrevented;
}
if (I.shadowHook) {
	I.shadowHook.dispatch = shadowDispatch;
	I.shadowHook.on = shadowOn;
}
/* composedPath(): the path as its current target may see it (the closed trees it is not in
 * hidden) */
{
	const composedPath = G.Event.prototype.composedPath;
	const visible = (n, cur) => {
		for (let r = treeRoot(n); ;) {
			const host = N.shadowHost(r);
			if (host === null) return true;
			if ((N.shadowFlags(r) & SR_CLOSED) && !(cur instanceof Node && shadowIncludingAncestor(r, cur)))
				return false;
			r = treeRoot(host);
		}
	};
	def(G.Event.prototype, {
		composedPath() {
			if (!this._shadow || !this._path) return composedPath.call(this);
			const cur = this.currentTarget;
			if (!cur) return [];
			return this._path.filter(n => !(n instanceof Node) || visible(n, cur));
		},
	});
}

/* ---- microdata: itemScope / itemProp / itemValue, properties, document.getItems ------------ */
{
	const proto = HTMLElement.prototype;
	Object.defineProperty(proto, 'itemScope', { configurable: true,
		get() { return N.attr(this, 'itemscope') !== null; },
		set(v) { this.toggleAttribute('itemscope', !!v); } });
	for (const [p, a] of [['itemType', 'itemtype'], ['itemProp', 'itemprop'], ['itemRef', 'itemref']])
		Object.defineProperty(proto, p, { configurable: true,
			get() { return new G.DOMTokenList(this, a); },
			set(v) { this.setAttribute(a, v); } });
	Object.defineProperty(proto, 'itemId', { configurable: true,
		get() {
			const v = N.attr(this, 'itemid');
			if (v === null) return '';
			try { return new URL(v, N.url()).href; } catch (e) { return v; }
		},
		set(v) { this.setAttribute('itemid', v); } });
	const URL_ATTR = { audio: 'src', embed: 'src', iframe: 'src', img: 'src', source: 'src',
		track: 'src', video: 'src', a: 'href', area: 'href', link: 'href', object: 'data' };
	const abs = v => { try { return new URL(v, N.url()).href; } catch (e) { return v; } };
	Object.defineProperty(proto, 'itemValue', { configurable: true,
		get() {
			if (N.attr(this, 'itemprop') === null) return null;
			if (N.attr(this, 'itemscope') !== null) return this;
			const t = N.lname(this);
			if (t === 'meta') return N.attr(this, 'content') || '';
			if (URL_ATTR[t]) { const v = N.attr(this, URL_ATTR[t]); return v === null ? '' : abs(v); }
			if (t === 'data' || t === 'meter') return N.attr(this, 'value') || '';
			if (t === 'time') { const d = N.attr(this, 'datetime'); return d !== null ? d : this.textContent; }
			return this.textContent;
		},
		set(v) {
			if (N.attr(this, 'itemprop') === null) throw domError('itemValue: no itemprop', 'InvalidAccessError');
			if (N.attr(this, 'itemscope') !== null) throw domError('itemValue: an item', 'InvalidAccessError');
			const t = N.lname(this), s = String(v);
			if (t === 'meta') this.setAttribute('content', s);
			else if (URL_ATTR[t]) this.setAttribute(URL_ATTR[t], s);
			else if (t === 'data' || t === 'meter') this.setAttribute('value', s);
			else if (t === 'time') this.setAttribute('datetime', s);
			else this.textContent = s;
		} });
	/* the item's properties: its descendants (and itemref's elements) with itemprop, not
	 * inside a nested item */
	function propertiesOf(item) {
		const root = item.getRootNode(), out = [], seen = new Set();
		const pending = [...N.children(item)];
		for (const id of (N.attr(item, 'itemref') || '').split(/\s+/).filter(Boolean)) {
			const e = root.getElementById ? root.getElementById(id) :
				Element.prototype.querySelector.call(root, '#' + G.CSS.escape(id));
			if (e) pending.push(e);
		}
		while (pending.length) {
			const e = pending.shift();
			if (N.type(e) !== ELEMENT_NODE || seen.has(e) || e === item) continue;
			seen.add(e);
			if (N.attr(e, 'itemprop') !== null && (N.attr(e, 'itemprop') || '').trim()) out.push(e);
			if (N.attr(e, 'itemscope') === null) pending.unshift(...N.children(e));
		}
		const pos = (a, b) => a.compareDocumentPosition ? (a.compareDocumentPosition(b) & 4 ? -1 : 1) : 0;
		return out.sort(pos);
	}
	class HTMLPropertiesCollection {
		constructor(els) {
			Object.defineProperty(this, '_e', { value: els });
			els.forEach((e, i) => { this[i] = e; });
			for (const e of els)
				for (const n of (N.attr(e, 'itemprop') || '').split(/\s+/).filter(Boolean))
					if (!(n in this)) Object.defineProperty(this, n, { value: this.namedItem(n),
						configurable: true });
		}
		get length() { return this._e.length; }
		item(i) { return this._e[i] || null; }
		namedItem(n) {
			const list = this._e.filter(e => (N.attr(e, 'itemprop') || '').split(/\s+/).includes(n));
			list.getValues = () => list.map(e => e.itemValue);
			return list;
		}
		get names() {
			const s = [];
			for (const e of this._e)
				for (const n of (N.attr(e, 'itemprop') || '').split(/\s+/).filter(Boolean))
					if (!s.includes(n)) s.push(n);
			return s;
		}
		[Symbol.iterator]() { return this._e[Symbol.iterator](); }
	}
	G.HTMLPropertiesCollection = HTMLPropertiesCollection;
	Object.defineProperty(proto, 'properties', { configurable: true,
		get() { return new HTMLPropertiesCollection(N.attr(this, 'itemscope') !== null ? propertiesOf(this) : []); } });
	def(G.Document.prototype, {
		getItems(types) {
			const want = (types === undefined ? '' : String(types)).split(/\s+/).filter(Boolean);
			return [...Element.prototype.querySelectorAll.call(this, '[itemscope]')].filter(e =>
				N.attr(e, 'itemprop') === null && (!want.length ||
					want.every(t => (N.attr(e, 'itemtype') || '').split(/\s+/).includes(t))));
		},
	});
}

/* ---- <ol reversed> (NetSurf's layout numbers it backwards), start, type ------------------ */
if (G.HTMLOListElement) {
	const P = G.HTMLOListElement.prototype;
	Object.defineProperty(P, 'reversed', { configurable: true,
		get() { return N.attr(this, 'reversed') !== null; },
		set(v) { this.toggleAttribute('reversed', !!v); } });
	Object.defineProperty(P, 'start', { configurable: true,
		get() {
			const v = parseInt(N.attr(this, 'start'), 10);
			if (!isNaN(v)) return v;
			return N.attr(this, 'reversed') !== null ?
				N.children(this).filter(c => N.type(c) === ELEMENT_NODE && N.lname(c) === 'li').length : 1;
		},
		set(v) { this.setAttribute('start', String(Math.trunc(+v) || 0)); } });
	if (!('type' in P))
		Object.defineProperty(P, 'type', { configurable: true,
			get() { return N.attr(this, 'type') || ''; },
			set(v) { this.setAttribute('type', v); } });
}

/* ---- :read-write / :read-only, :defined ----------------------------------------------------- */
{
	const TEXTISH = ['text', 'search', 'url', 'tel', 'email', 'password', 'date', 'month', 'week',
		'time', 'datetime-local', 'number'];
	const readWrite = e => {
		const t = N.lname(e);
		if (N.nsURI(e) !== NS_HTML) return false;
		if (t === 'input')
			return TEXTISH.includes(inputType(e)) && N.attr(e, 'readonly') === null && !e.disabled;
		if (t === 'textarea')
			return N.attr(e, 'readonly') === null && !e.disabled;
		return !!e.isContentEditable;
	};
	const prev = N.internals && N.internals.pseudo;
	if (N.internals) N.internals.pseudo = (e, name, arg) => {
		switch (name) {
		case 'read-write': return readWrite(e);
		case 'read-only': return !readWrite(e);
		case 'defined': return N.nsURI(e) !== NS_HTML || !N.lname(e).includes('-') || ceState.has(e);
		}
		return prev ? prev(e, name, arg) : false;
	};
}

/* ---- performance: marks, measures, PerformanceObserver ------------------------------------ */
{
	const perf = G.performance;
	const entries = [];
	const perfObservers = new Set();
	class PerformanceEntry {
		constructor(name, type, start, duration) {
			Object.defineProperty(this, 'name', { value: name, enumerable: true });
			Object.defineProperty(this, 'entryType', { value: type, enumerable: true });
			Object.defineProperty(this, 'startTime', { value: start, enumerable: true });
			Object.defineProperty(this, 'duration', { value: duration, enumerable: true });
		}
		toJSON() { return { name: this.name, entryType: this.entryType, startTime: this.startTime, duration: this.duration, detail: this.detail }; }
	}
	class PerformanceMark extends PerformanceEntry {
		constructor(name, opts = {}) {
			if (name === undefined) throw new TypeError('PerformanceMark: a name is needed');
			const t = opts && opts.startTime !== undefined ? +opts.startTime : perf.now();
			if (t < 0) throw new TypeError('PerformanceMark: negative startTime');
			super(String(name), 'mark', t, 0);
			Object.defineProperty(this, 'detail', { value: opts && opts.detail !== undefined ? structuredClone(opts.detail) : null, enumerable: true });
		}
	}
	class PerformanceMeasure extends PerformanceEntry {}
	/* an observer's entries: its callback in a task */
	const deliver = (o, e) => {
		o._queue.push(e);
		if (o._pending) return;
		o._pending = true;
		task(() => {
			o._pending = false;
			if (!o._queue.length) return;
			const list = new PerformanceObserverEntryList(o._queue.splice(0));
			o._cb.call(o, list, o, { droppedEntriesCount: 0 });
		});
	};
	const record = e => {
		entries.push(e);
		for (const o of perfObservers)
			if (o._types.has(e.entryType)) deliver(o, e);
	};
	const TIMING_NAMES = ['navigationStart', 'unloadEventStart', 'unloadEventEnd',
		'redirectStart', 'redirectEnd', 'fetchStart', 'domainLookupStart', 'domainLookupEnd',
		'connectStart', 'connectEnd', 'secureConnectionStart', 'requestStart', 'responseStart',
		'responseEnd', 'domLoading', 'domInteractive', 'domContentLoadedEventStart',
		'domContentLoadedEventEnd', 'domComplete', 'loadEventStart', 'loadEventEnd'];
	const markTime = (v, what) => {
		if (v === undefined) return undefined;
		if (typeof v === 'number') return v;
		const m = entries.filter(e => e.entryType === 'mark' && e.name === String(v)).pop();
		/* a PerformanceTiming attribute's name ("navigationStart", bbc.com): its time
		 * from the navigation's start (Onyx: the navigation's events are not timed, 0) */
		if (!m && TIMING_NAMES.includes(String(v))) {
			const tm = perf.timing || {};
			return tm[v] > 0 && tm.navigationStart > 0 ? tm[v] - tm.navigationStart : 0;
		}
		if (!m) throw domError('performance.' + what + ': no mark "' + v + '"', 'SyntaxError');
		return m.startTime;
	};
	const byType = t => entries.filter(e => e.entryType === t);
	def(perf, {
		mark(name, opts) { const m = new PerformanceMark(name, opts); record(m); return m; },
		measure(name, a, b) {
			let start = 0, end = perf.now(), detail = null;
			if (a !== null && typeof a === 'object') {
				if (a.start !== undefined) start = markTime(a.start, 'measure');
				if (a.end !== undefined) end = markTime(a.end, 'measure');
				if (a.duration !== undefined) {
					if (a.start !== undefined) end = start + +a.duration;
					else start = end - +a.duration;
				}
				detail = a.detail !== undefined ? structuredClone(a.detail) : null;
			} else {
				if (a !== undefined) start = markTime(a, 'measure');
				if (b !== undefined) end = markTime(b, 'measure');
			}
			const m = new PerformanceMeasure(String(name), 'measure', start, end - start);
			Object.defineProperty(m, 'detail', { value: detail, enumerable: true });
			record(m);
			return m;
		},
		clearMarks(name) {
			for (let i = entries.length - 1; i >= 0; i--)
				if (entries[i].entryType === 'mark' && (name === undefined || entries[i].name === String(name)))
					entries.splice(i, 1);
		},
		clearMeasures(name) {
			for (let i = entries.length - 1; i >= 0; i--)
				if (entries[i].entryType === 'measure' && (name === undefined || entries[i].name === String(name)))
					entries.splice(i, 1);
		},
		getEntries() { return entries.slice().sort((a, b) => a.startTime - b.startTime); },
		getEntriesByType(t) { return byType(String(t)).sort((a, b) => a.startTime - b.startTime); },
		getEntriesByName(n, t) {
			return entries.filter(e => e.name === String(n) && (t === undefined || e.entryType === String(t)))
				.sort((a, b) => a.startTime - b.startTime);
		},
		toJSON() { return { timeOrigin: perf.timeOrigin }; },
	});
	class PerformanceObserverEntryList {
		constructor(list) { Object.defineProperty(this, '_l', { value: list }); }
		getEntries() { return this._l.slice(); }
		getEntriesByType(t) { return this._l.filter(e => e.entryType === String(t)); }
		getEntriesByName(n, t) { return this._l.filter(e => e.name === String(n) && (t === undefined || e.entryType === String(t))); }
	}
	const SUPPORTED = Object.freeze(['mark', 'measure']);
	class PerformanceObserver {
		constructor(cb) {
			if (typeof cb !== 'function') throw new TypeError('PerformanceObserver: a callback is needed');
			this._cb = cb; this._types = new Set(); this._queue = []; this._pending = false;
		}
		observe(opts = {}) {
			const types = opts.entryTypes ? [...opts.entryTypes].map(String) : opts.type !== undefined ? [String(opts.type)] : null;
			if (!types) throw new TypeError('PerformanceObserver.observe: entryTypes or type');
			if (opts.entryTypes && opts.type !== undefined) throw new TypeError('PerformanceObserver.observe: entryTypes and type');
			if (!opts.entryTypes) this._types.clear();
			for (const t of types) if (SUPPORTED.includes(t)) this._types.add(t);
			if (!this._types.size) return;
			perfObservers.add(this);
			if (opts.buffered && opts.type !== undefined)
				for (const e of byType(String(opts.type))) deliver(this, e);
		}
		disconnect() { perfObservers.delete(this); this._queue = []; }
		takeRecords() { return this._queue.splice(0); }
		static get supportedEntryTypes() { return SUPPORTED; }
	}
	Object.assign(G, { PerformanceEntry, PerformanceMark, PerformanceMeasure, PerformanceObserver,
		PerformanceObserverEntryList });
}

/* ---- XMLHttpRequest: responseType 'document', responseXML (DOMParser on the text) ---------- */
if (G.XMLHttpRequest) {
	const P = G.XMLHttpRequest.prototype;
	const resp = Object.getOwnPropertyDescriptor(P, 'response');
	const docOf = xhr => {
		if (xhr.readyState < 4 || typeof xhr._res !== 'string') return null;
		if (xhr._doc !== undefined) return xhr._doc;
		const ct = (xhr._mime || xhr.getResponseHeader('content-type') || '').toLowerCase();
		let type = null;
		if (/text\/html/.test(ct)) type = xhr.responseType === 'document' ? 'text/html' : null;
		else if (/[+/]xml\b|^\s*$/.test(ct) || /xml/.test(ct)) type = ct.includes('svg') ? 'image/svg+xml' : 'application/xml';
		let d = null;
		if (type) {
			try {
				d = new G.DOMParser().parseFromString(xhr._res, type);
				if (type !== 'text/html' && d.querySelector && d.querySelector('parsererror')) d = null;
			} catch (e) { d = null; }
		}
		Object.defineProperty(xhr, '_doc', { value: d, configurable: true });
		return d;
	};
	def(P, {
		get response() {
			if (this.responseType === 'document') return docOf(this);
			return resp.get.call(this);
		},
		get responseXML() {
			if (this.responseType !== '' && this.responseType !== 'document')
				throw domError('responseXML: responseType is ' + this.responseType, 'InvalidStateError');
			return docOf(this);
		},
	});
	const open = P.open;
	P.open = function () { delete this._doc; return open.apply(this, arguments); };
}

/* attribute changes: <details> / <dialog> open, the custom elements' observedAttributes */
ceAttributeChanged = (el, k, old, now) => {
	if (k === 'open') openChanged(el, old, now);
	if (ceDefs.size) {
		const s = ceState.get(el);
		if (s && s.def.observed.has(k))
			ceCall(el, s.def, 'attributeChangedCallback', [k, old, now, null]);
	}
};

return { serialize: outerHTMLOf, parseFragment, structuredClone };
})
