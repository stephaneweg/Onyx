/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: the DOM for QuickJS -- Node, Element, Document, events, selectors, classList,
 * style, dataset, innerHTML, the window's objects -- written on the natives of qjs.c (N:
 * libdom's tree, the layout's boxes, the window). This file is one function expression:
 * qjs.c evaluates it and calls it with N.
 */
(function (N) {
'use strict';

const G = globalThis;
const NativePromise = Promise;	/* (a page's polyfill may replace Promise: queueMicrotask's own) */
const LISTENERS = Symbol('listeners');
const HANDLERS = Symbol('handlers');

const ELEMENT_NODE = 1, ATTRIBUTE_NODE = 2, TEXT_NODE = 3, CDATA_SECTION_NODE = 4,
	PROCESSING_INSTRUCTION_NODE = 7, COMMENT_NODE = 8, DOCUMENT_NODE = 9,
	DOCUMENT_TYPE_NODE = 10, DOCUMENT_FRAGMENT_NODE = 11;

const VOID_ELEMENTS = new Set(['area', 'base', 'br', 'col', 'embed', 'hr', 'img',
	'input', 'link', 'meta', 'param', 'source', 'track', 'wbr']);

function def(obj, props) {
	for (const k of Object.keys(props)) {
		const d = Object.getOwnPropertyDescriptor(props, k);
		d.enumerable = false;
		d.configurable = true;
		Object.defineProperty(obj, k, d);
	}
}

function lower(s) { return String(s).toLowerCase(); }
function isNode(v) { return v instanceof Node; }
function isElement(v) { return v instanceof Element; }

/* ---- errors -------------------------------------------------------------------------- */

class DOMException extends Error {
	constructor(message = '', name = 'Error') {
		super(message);
		this.name = name;
	}
}
G.DOMException = DOMException;

/* ---- events ---------------------------------------------------------------------------- */

class Event {
	constructor(type, init = {}) {
		this.type = String(type);
		this.bubbles = !!init.bubbles;
		this.cancelable = !!init.cancelable;
		this.composed = !!init.composed;
		this.defaultPrevented = false;
		this.isTrusted = false;
		this.timeStamp = N.now();
		this.target = null;
		this.currentTarget = null;
		this.eventPhase = 0;
		this._stop = false;
		this._stopNow = false;
	}
	get srcElement() { return this.target; }
	get returnValue() { return !this.defaultPrevented; }
	set returnValue(v) { if (!v) this.preventDefault(); }
	get cancelBubble() { return this._stop; }
	set cancelBubble(v) { if (v) this._stop = true; }
	preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
	stopPropagation() { this._stop = true; }
	stopImmediatePropagation() { this._stop = true; this._stopNow = true; }
	composedPath() { return this._path ? this._path.slice() : []; }
	initEvent(type, bubbles, cancelable) {
		this.type = String(type);
		this.bubbles = !!bubbles;
		this.cancelable = !!cancelable;
	}
}
def(Event, { NONE: 0, CAPTURING_PHASE: 1, AT_TARGET: 2, BUBBLING_PHASE: 3 });
def(Event.prototype, { NONE: 0, CAPTURING_PHASE: 1, AT_TARGET: 2, BUBBLING_PHASE: 3 });

class CustomEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		this.detail = init.detail === undefined ? null : init.detail;
	}
	initCustomEvent(type, bubbles, cancelable, detail) {
		this.initEvent(type, bubbles, cancelable);
		this.detail = detail;
	}
}
class UIEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		this.view = init.view || null;
		this.detail = init.detail || 0;
	}
}
class MouseEvent extends UIEvent {
	constructor(type, init = {}) {
		super(type, init);
		this.clientX = init.clientX || 0;
		this.clientY = init.clientY || 0;
		this.screenX = init.screenX || this.clientX;
		this.screenY = init.screenY || this.clientY;
		this.pageX = this.clientX + (G.scrollX || 0);
		this.pageY = this.clientY + (G.scrollY || 0);
		this.offsetX = this.clientX;
		this.offsetY = this.clientY;
		this.x = this.clientX;
		this.y = this.clientY;
		this.button = init.button || 0;
		this.buttons = init.buttons || 0;
		this.ctrlKey = !!init.ctrlKey;
		this.shiftKey = !!init.shiftKey;
		this.altKey = !!init.altKey;
		this.metaKey = !!init.metaKey;
		this.relatedTarget = init.relatedTarget || null;
	}
	getModifierState(k) {
		return (k === 'Shift' && this.shiftKey) || (k === 'Control' && this.ctrlKey) ||
			(k === 'Alt' && this.altKey) || (k === 'Meta' && this.metaKey);
	}
}
class PointerEvent extends MouseEvent {
	constructor(type, init = {}) {
		super(type, init);
		this.pointerId = init.pointerId || 1;
		this.pointerType = init.pointerType || 'mouse';
		this.isPrimary = true;
		this.width = 1;
		this.height = 1;
		this.pressure = init.pressure || 0;
	}
}
class WheelEvent extends MouseEvent {
	constructor(type, init = {}) {
		super(type, init);
		this.deltaX = init.deltaX || 0;
		this.deltaY = init.deltaY || 0;
		this.deltaZ = init.deltaZ || 0;
		this.deltaMode = init.deltaMode || 0;
	}
}
class KeyboardEvent extends UIEvent {
	constructor(type, init = {}) {
		super(type, init);
		this.key = init.key || '';
		this.code = init.code || '';
		this.keyCode = init.keyCode || 0;
		this.which = this.keyCode;
		this.charCode = type === 'keypress' ? this.keyCode : 0;
		this.location = init.location || 0;
		this.repeat = !!init.repeat;
		this.isComposing = false;
		this.ctrlKey = !!init.ctrlKey;
		this.shiftKey = !!init.shiftKey;
		this.altKey = !!init.altKey;
		this.metaKey = !!init.metaKey;
	}
	getModifierState(k) {
		return (k === 'Shift' && this.shiftKey) || (k === 'Control' && this.ctrlKey) ||
			(k === 'Alt' && this.altKey) || (k === 'Meta' && this.metaKey);
	}
}
class FocusEvent extends UIEvent {
	constructor(type, init = {}) {
		super(type, init);
		this.relatedTarget = init.relatedTarget || null;
	}
}
class InputEvent extends UIEvent {
	constructor(type, init = {}) {
		super(type, init);
		this.data = init.data === undefined ? null : init.data;
		this.inputType = init.inputType || '';
		this.isComposing = false;
	}
}
class ErrorEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		this.message = init.message || '';
		this.error = init.error;
	}
}
class ProgressEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		this.lengthComputable = !!init.lengthComputable;
		this.loaded = init.loaded || 0;
		this.total = init.total || 0;
	}
}
class PopStateEvent extends Event {
	constructor(type, init = {}) { super(type, init); this.state = init.state; }
}
class HashChangeEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		this.oldURL = init.oldURL || '';
		this.newURL = init.newURL || '';
	}
}
class MessageEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		this.data = init.data;
		this.origin = init.origin || '';
		this.source = init.source || null;
	}
}
class SubmitEvent extends Event {
	constructor(type, init = {}) { super(type, init); this.submitter = init.submitter || null; }
}
/* Onyx: core-js (bundled by many sites: bbc.co.uk's consent script) replaces the native
 * Promise with its own when PromiseRejectionEvent is missing -- and its polyfill looped
 * through microtasks for ever (1.5 GB in a minute: the Pi's "out of memory") */
class PromiseRejectionEvent extends Event {
	constructor(type, init = {}) { super(type, init); this.promise = init.promise; this.reason = init.reason; }
}
Object.assign(G, { Event, CustomEvent, UIEvent, MouseEvent, PointerEvent, WheelEvent,
	KeyboardEvent, FocusEvent, InputEvent, ErrorEvent, ProgressEvent, PopStateEvent,
	HashChangeEvent, MessageEvent, SubmitEvent, PromiseRejectionEvent, TouchEvent: undefined });
delete G.TouchEvent;

/* the event's path: its target, its ancestors, the document, the window */
function eventParent(n) {
	if (n === G)
		return null;
	if (n instanceof Document)
		return G;
	if (n instanceof Node)
		return N.parent(n) || (n === G.document ? G : null);
	return null;
}

function listenersOf(target, create) {
	let l = target[LISTENERS];
	if (!l && create) {
		l = new Map();
		Object.defineProperty(target, LISTENERS, { value: l, configurable: true });
	}
	return l;
}

/* the window's events whose on* attributes are the body's (<body onload>) */
const WINDOW_EVENTS = new Set(['load', 'scroll', 'resize', 'hashchange', 'popstate',
	'message', 'pageshow', 'pagehide', 'beforeunload', 'unload', 'online', 'offline']);

/* an on* handler, set as a property or an attribute */
function inlineHandler(node, type) {
	const h = node[HANDLERS] && node[HANDLERS].get(type);
	if (typeof h === 'function')
		return h;
	if (node === G && WINDOW_EVENTS.has(type)) {
		const b = G.document.body;
		return b ? inlineHandler(b, type) : null;
	}
	if (node instanceof Element) {
		const src = N.attr(node, 'on' + type);
		if (src !== null && src !== '') {
			let cache = node[HANDLERS];
			if (!cache) {
				cache = new Map();
				Object.defineProperty(node, HANDLERS, { value: cache, configurable: true });
			}
			const key = '#' + type;
			const old = cache.get(key);
			if (old && old.src === src)
				return old.fn;
			let fn;
			try {
				fn = new Function('event', src);
			} catch (e) {
				report(e);
				fn = null;
			}
			cache.set(key, { src, fn });
			return fn;
		}
	}
	return null;
}

function report(e) {
	N.log('Uncaught ' + (e && e.stack ? e + '\n' + e.stack : String(e)));
}

function invoke(node, ev, capture) {
	const l = listenersOf(node, false);
	const list = l && l.get(ev.type);
	ev.currentTarget = node;
	if (list) {
		for (const rec of list.slice()) {
			if (ev._stopNow)
				break;
			if (rec.removed)
				continue;
			if (rec.capture !== capture)	/* (at the target: capture first) */
				continue;
			if (rec.once)
				node.removeEventListener(ev.type, rec.fn, rec);
			try {
				if (typeof rec.fn === 'function')
					rec.fn.call(node, ev);
				else if (rec.fn && typeof rec.fn.handleEvent === 'function')
					rec.fn.handleEvent(ev);
			} catch (e) {
				report(e);
			}
		}
	}
	if (!capture && !ev._stopNow) {
		const h = inlineHandler(node, ev.type);
		if (h) {
			try {
				const r = h.call(node, ev);
				if (r === false)
					ev.preventDefault();
			} catch (e) {
				report(e);
			}
		}
	}
}

function dispatch(target, ev) {
	const path = [];
	for (let n = target; n; n = eventParent(n))
		path.push(n);
	ev.target = target;
	ev._path = path;
	ev._stop = ev._stopNow = false;
	for (let i = path.length - 1; i > 0 && !ev._stop; i--) {
		ev.eventPhase = Event.CAPTURING_PHASE;
		invoke(path[i], ev, true);
	}
	if (!ev._stop) {
		ev.eventPhase = Event.AT_TARGET;
		invoke(target, ev, true);
		if (!ev._stop)
			invoke(target, ev, false);
	}
	if (ev.bubbles) {
		for (let i = 1; i < path.length && !ev._stop; i++) {
			ev.eventPhase = Event.BUBBLING_PHASE;
			invoke(path[i], ev, false);
		}
	}
	ev.eventPhase = 0;
	ev.currentTarget = null;
	return !ev.defaultPrevented;
}

class EventTarget {
	addEventListener(type, fn, opts) {
		if (!fn)
			return;
		const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
		const l = listenersOf(this, true);
		let list = l.get(type);
		if (!list)
			l.set(type, list = []);
		if (list.some(r => r.fn === fn && r.capture === capture))
			return;
		list.push({ fn, capture, once: !!(opts && opts.once),
			passive: !!(opts && opts.passive) });
		if (opts && opts.signal && typeof opts.signal.addEventListener === 'function')
			opts.signal.addEventListener('abort', () =>
				this.removeEventListener(type, fn, opts));
	}
	removeEventListener(type, fn, opts) {
		const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
		const l = listenersOf(this, false);
		const list = l && l.get(type);
		if (!list)
			return;
		const i = list.findIndex(r => r.fn === fn && r.capture === capture);
		if (i >= 0) {
			list[i].removed = true;
			list.splice(i, 1);
		}
	}
	dispatchEvent(ev) {
		if (!(ev instanceof Event))
			throw new TypeError('not an Event');
		return dispatch(this, ev);
	}
}
G.EventTarget = EventTarget;

/* on* properties: a handler per type */
const HANDLER_TYPES = ['click', 'dblclick', 'mousedown', 'mouseup', 'mousemove',
	'mouseover', 'mouseout', 'mouseenter', 'mouseleave', 'contextmenu', 'wheel',
	'keydown', 'keyup', 'keypress', 'focus', 'blur', 'focusin', 'focusout', 'input',
	'change', 'submit', 'reset', 'load', 'error', 'abort', 'scroll', 'resize',
	'select', 'pointerdown', 'pointerup', 'pointermove', 'touchstart', 'touchend',
	'touchmove', 'animationend', 'transitionend', 'beforeunload', 'unload',
	'hashchange', 'popstate', 'message', 'toggle', 'play', 'pause', 'ended'];
function defineHandlers(proto) {
	for (const t of HANDLER_TYPES) {
		Object.defineProperty(proto, 'on' + t, {
			configurable: true,
			get() { const m = this[HANDLERS]; return (m && m.get(t)) || null; },
			set(fn) {
				let m = this[HANDLERS];
				if (!m) {
					m = new Map();
					Object.defineProperty(this, HANDLERS, { value: m, configurable: true });
				}
				m.set(t, typeof fn === 'function' ? fn : null);
			}
		});
	}
}

/* ---- collections -------------------------------------------------------------------- */

class NodeList extends Array {
	item(i) { return this[i] === undefined ? null : this[i]; }
	static get [Symbol.species]() { return Array; }
}
class HTMLCollection extends NodeList {
	namedItem(name) {
		return this.find(e => e.id === name || e.getAttribute('name') === name) || null;
	}
}
function nodeList(arr) { const l = new NodeList(); for (const x of arr) l.push(x); return l; }
function htmlCollection(arr) {
	const l = new HTMLCollection();
	for (const x of arr) l.push(x);
	return l;
}
G.NodeList = NodeList;
G.HTMLCollection = HTMLCollection;

/* ---- mutation observers ---------------------------------------------------------------- */

const observers = new Set();
let mutationQueued = false;

function queueMutation(rec) {
	if (observers.size === 0)
		return;
	for (const mo of observers) {
		for (const [target, opts] of mo._targets) {
			let n = rec.target, direct = true, hit = false;
			while (n) {
				if (n === target && (direct || opts.subtree)) {
					hit = true;
					break;
				}
				direct = false;
				n = N.parent(n);
			}
			if (!hit)
				continue;
			if (rec.type === 'attributes' && !opts.attributes)
				continue;
			if (rec.type === 'attributes' && opts.attributeFilter &&
					!opts.attributeFilter.includes(rec.attributeName))
				continue;
			if (rec.type === 'childList' && !opts.childList)
				continue;
			if (rec.type === 'characterData' && !opts.characterData)
				continue;
			mo._records.push(rec);
			break;
		}
	}
	if (!mutationQueued) {
		mutationQueued = true;
		Promise.resolve().then(() => {
			mutationQueued = false;
			for (const mo of observers) {
				if (mo._records.length) {
					const recs = mo._records;
					mo._records = [];
					try { mo._cb(recs, mo); } catch (e) { report(e); }
				}
			}
		});
	}
}

class MutationObserver {
	constructor(cb) { this._cb = cb; this._targets = new Map(); this._records = []; }
	observe(target, opts = {}) {
		const o = Object.assign({}, opts);
		if (o.attributeOldValue || o.attributeFilter)
			o.attributes = true;
		if (o.characterDataOldValue)
			o.characterData = true;
		this._targets.set(target, o);
		observers.add(this);
	}
	disconnect() { this._targets.clear(); this._records = []; observers.delete(this); }
	takeRecords() { const r = this._records; this._records = []; return r; }
}
G.MutationObserver = MutationObserver;
G.WebKitMutationObserver = MutationObserver;

function childListRecord(target, added, removed, prev, next) {
	queueMutation({ type: 'childList', target, addedNodes: nodeList(added),
		removedNodes: nodeList(removed), previousSibling: prev || null,
		nextSibling: next || null, attributeName: null, oldValue: null });
}

/* ---- Node -------------------------------------------------------------------------------- */

function toNode(v) {
	return isNode(v) ? v : N.createText(String(v));
}

function nodesToNode(args) {
	if (args.length === 1)
		return toNode(args[0]);
	const f = N.createFragment();
	for (const a of args)
		N.insert(f, toNode(a), null);
	return f;
}

class Node extends EventTarget {
	constructor() { super(); throw new TypeError('Illegal constructor'); }
	get nodeType() { return N.type(this); }
	get nodeName() {
		const t = N.type(this);
		if (t === ELEMENT_NODE)
			return this.tagName;
		if (t === TEXT_NODE) return '#text';
		if (t === COMMENT_NODE) return '#comment';
		if (t === DOCUMENT_NODE) return '#document';
		if (t === DOCUMENT_FRAGMENT_NODE) return '#document-fragment';
		return N.name(this);
	}
	get nodeValue() {
		const t = N.type(this);
		return (t === TEXT_NODE || t === COMMENT_NODE || t === CDATA_SECTION_NODE) ?
			N.value(this) : null;
	}
	set nodeValue(v) {
		const t = N.type(this);
		if (t === TEXT_NODE || t === COMMENT_NODE || t === CDATA_SECTION_NODE)
			this.data = v;
	}
	get textContent() {
		const t = N.type(this);
		if (t === DOCUMENT_NODE || t === DOCUMENT_TYPE_NODE)
			return null;
		return N.text(this);
	}
	set textContent(v) {
		const t = N.type(this);
		if (t === DOCUMENT_NODE || t === DOCUMENT_TYPE_NODE)
			return;
		const removed = observers.size ? N.children(this) : [];
		N.setText(this, v === null || v === undefined ? '' : String(v));
		if (observers.size)
			childListRecord(this, N.children(this), removed);
	}
	get parentNode() { return N.parent(this); }
	get parentElement() {
		const p = N.parent(this);
		return p && N.type(p) === ELEMENT_NODE ? p : null;
	}
	get childNodes() { return nodeList(N.children(this)); }
	get firstChild() { return N.first(this); }
	get lastChild() { return N.last(this); }
	get previousSibling() { return N.prev(this); }
	get nextSibling() { return N.next(this); }
	get ownerDocument() { return N.type(this) === DOCUMENT_NODE ? null : N.document(); }
	get isConnected() {
		for (let n = this; n; n = N.parent(n))
			if (N.type(n) === DOCUMENT_NODE)
				return true;
		return false;
	}
	get baseURI() { return N.url(); }
	hasChildNodes() { return N.first(this) !== null; }
	getRootNode() { let n = this; for (let p; (p = N.parent(n)); n = p); return n; }
	contains(other) {
		for (let n = other; n; n = N.parent(n))
			if (n === this)
				return true;
		return false;
	}
	appendChild(c) { return this.insertBefore(c, null); }
	insertBefore(c, ref) {
		if (!isNode(c))
			throw new TypeError('not a Node');
		const added = N.type(c) === DOCUMENT_FRAGMENT_NODE ? N.children(c) : [c];
		const oldParent = N.parent(c);
		if (oldParent && observers.size)
			childListRecord(oldParent, [], [c]);
		N.insert(this, c, ref || null);
		if (observers.size)
			childListRecord(this, added, [], null, ref || null);
		return c;
	}
	removeChild(c) {
		if (!isNode(c) || N.parent(c) !== this)
			throw new DOMException('not a child', 'NotFoundError');
		N.remove(this, c);
		if (observers.size)
			childListRecord(this, [], [c]);
		return c;
	}
	replaceChild(nc, oc) {
		if (!isNode(oc) || N.parent(oc) !== this)
			throw new DOMException('not a child', 'NotFoundError');
		if (nc === oc)
			return oc;
		this.insertBefore(nc, oc);
		this.removeChild(oc);
		return oc;
	}
	cloneNode(deep) { return N.clone(this, !!deep); }
	isSameNode(o) { return o === this; }
	isEqualNode(o) {
		if (!isNode(o) || N.type(o) !== N.type(this))
			return false;
		if (N.type(this) === ELEMENT_NODE)
			return this.outerHTML === o.outerHTML;
		return this.nodeValue === o.nodeValue && this.textContent === o.textContent;
	}
	compareDocumentPosition(o) {
		if (o === this)
			return 0;
		if (this.contains(o))
			return 20;	/* CONTAINED_BY | FOLLOWING */
		if (o.contains(this))
			return 10;	/* CONTAINS | PRECEDING */
		const all = allNodes(N.document());
		const a = all.indexOf(this), b = all.indexOf(o);
		if (a < 0 || b < 0)
			return 1;	/* DISCONNECTED */
		return a < b ? 4 : 2;
	}
	normalize() {}
	lookupNamespaceURI() { return 'http://www.w3.org/1999/xhtml'; }
}
def(Node, { ELEMENT_NODE, ATTRIBUTE_NODE, TEXT_NODE, CDATA_SECTION_NODE,
	PROCESSING_INSTRUCTION_NODE, COMMENT_NODE, DOCUMENT_NODE, DOCUMENT_TYPE_NODE,
	DOCUMENT_FRAGMENT_NODE, DOCUMENT_POSITION_DISCONNECTED: 1,
	DOCUMENT_POSITION_PRECEDING: 2, DOCUMENT_POSITION_FOLLOWING: 4,
	DOCUMENT_POSITION_CONTAINS: 8, DOCUMENT_POSITION_CONTAINED_BY: 16 });
def(Node.prototype, { ELEMENT_NODE, TEXT_NODE, COMMENT_NODE, DOCUMENT_NODE,
	DOCUMENT_FRAGMENT_NODE });
G.Node = Node;

function allNodes(root) {
	const out = [];
	(function walk(n) { out.push(n); for (const c of N.children(n)) walk(c); })(root);
	return out;
}

/* ---- the ParentNode / ChildNode mixins ------------------------------------------------ */

const ParentNode = {
	get children() { return htmlCollection(N.children(this).filter(isElement)); },
	get firstElementChild() {
		for (let c = N.first(this); c; c = N.next(c))
			if (N.type(c) === ELEMENT_NODE) return c;
		return null;
	},
	get lastElementChild() {
		for (let c = N.last(this); c; c = N.prev(c))
			if (N.type(c) === ELEMENT_NODE) return c;
		return null;
	},
	get childElementCount() { return N.children(this).filter(isElement).length; },
	append(...nodes) { if (nodes.length) this.appendChild(nodesToNode(nodes)); },
	prepend(...nodes) { if (nodes.length) this.insertBefore(nodesToNode(nodes), N.first(this)); },
	replaceChildren(...nodes) {
		for (let c; (c = N.first(this));)
			this.removeChild(c);
		this.append(...nodes);
	},
	querySelector(sel) {
		const list = parseSelector(sel);
		for (const e of N.descendants(this))
			if (matchList(e, list, this))
				return e;
		return null;
	},
	querySelectorAll(sel) {
		const list = parseSelector(sel);
		return nodeList(N.descendants(this).filter(e => matchList(e, list, this)));
	},
	getElementsByTagName(tag) {
		const t = lower(tag);
		return htmlCollection(N.descendants(this).filter(e => t === '*' || e.localName === t));
	},
	getElementsByClassName(names) {
		const want = String(names).split(/\s+/).filter(Boolean);
		return htmlCollection(N.descendants(this).filter(e => {
			const cls = (N.attr(e, 'class') || '').split(/\s+/);
			return want.every(w => cls.includes(w));
		}));
	},
};

const ChildNode = {
	before(...nodes) {
		const p = N.parent(this);
		if (p && nodes.length) p.insertBefore(nodesToNode(nodes), this);
	},
	after(...nodes) {
		const p = N.parent(this);
		if (p && nodes.length) p.insertBefore(nodesToNode(nodes), N.next(this));
	},
	replaceWith(...nodes) {
		const p = N.parent(this);
		if (!p) return;
		if (nodes.length) p.insertBefore(nodesToNode(nodes), this);
		p.removeChild(this);
	},
	remove() {
		const p = N.parent(this);
		if (p) p.removeChild(this);
	},
	get nextElementSibling() {
		for (let c = N.next(this); c; c = N.next(c))
			if (N.type(c) === ELEMENT_NODE) return c;
		return null;
	},
	get previousElementSibling() {
		for (let c = N.prev(this); c; c = N.prev(c))
			if (N.type(c) === ELEMENT_NODE) return c;
		return null;
	},
};

function mixin(cls, m) {
	for (const k of Object.keys(m))
		Object.defineProperty(cls.prototype, k,
			Object.assign(Object.getOwnPropertyDescriptor(m, k),
				{ enumerable: false, configurable: true }));
}

/* ---- CharacterData, Text, Comment ------------------------------------------------------ */

class CharacterData extends Node {
	get data() { return N.value(this); }
	set data(v) {
		const old = N.value(this);
		N.setValue(this, String(v));
		if (observers.size)
			queueMutation({ type: 'characterData', target: this, oldValue: old,
				addedNodes: nodeList([]), removedNodes: nodeList([]) });
	}
	get length() { return this.data.length; }
	appendData(s) { this.data += s; }
	insertData(o, s) { const d = this.data; this.data = d.slice(0, o) + s + d.slice(o); }
	deleteData(o, n) { const d = this.data; this.data = d.slice(0, o) + d.slice(o + n); }
	replaceData(o, n, s) { const d = this.data; this.data = d.slice(0, o) + s + d.slice(o + n); }
	substringData(o, n) { return this.data.substr(o, n); }
}
mixin(CharacterData, {
	get nextElementSibling() { return ChildNode.nextElementSibling.call(this); },
	get previousElementSibling() { return ChildNode.previousElementSibling.call(this); },
});
for (const k of ['before', 'after', 'replaceWith', 'remove'])
	Object.defineProperty(CharacterData.prototype, k, { value: ChildNode[k],
		configurable: true, writable: true });

class Text extends CharacterData {
	constructor(s = '') { return N.createText(String(s)); }
	get wholeText() { return this.data; }
	splitText(o) {
		const d = this.data;
		const t = N.createText(d.slice(o));
		this.data = d.slice(0, o);
		const p = N.parent(this);
		if (p) p.insertBefore(t, N.next(this));
		return t;
	}
}
class Comment extends CharacterData {
	constructor(s = '') { return N.createComment(String(s)); }
}
class CDATASection extends Text {}
G.CharacterData = CharacterData;
G.Text = Text;
G.Comment = Comment;
G.CDATASection = CDATASection;

class DocumentType extends Node {
	get name() { return N.name(this); }
	get publicId() { return ''; }
	get systemId() { return ''; }
}
G.DocumentType = DocumentType;

class DocumentFragment extends Node {
	constructor() { return N.createFragment(); }
	getElementById(id) {
		return N.descendants(this).find(e => N.attr(e, 'id') === id) || null;
	}
}
mixin(DocumentFragment, ParentNode);
G.DocumentFragment = DocumentFragment;

/* ---- DOMTokenList (classList, relList) ---------------------------------------------- */

class DOMTokenList {
	constructor(el, attr) { this._el = el; this._attr = attr; }
	_get() { return (N.attr(this._el, this._attr) || '').split(/\s+/).filter(Boolean); }
	_set(list) {
		const v = list.join(' ');
		const old = N.attr(this._el, this._attr);
		if (old === v) return;
		N.setAttr(this._el, this._attr, v);
		if (observers.size)
			queueMutation({ type: 'attributes', target: this._el,
				attributeName: this._attr, oldValue: old,
				addedNodes: nodeList([]), removedNodes: nodeList([]) });
	}
	get length() { return this._get().length; }
	get value() { return N.attr(this._el, this._attr) || ''; }
	set value(v) { N.setAttr(this._el, this._attr, String(v)); }
	item(i) { const l = this._get(); return i < l.length ? l[i] : null; }
	contains(t) { return this._get().includes(String(t)); }
	add(...ts) {
		const l = this._get();
		let changed = false;
		for (const t of ts) {
			const s = String(t);
			if (s === '' || /\s/.test(s))
				throw new DOMException('bad token', 'SyntaxError');
			if (!l.includes(s)) { l.push(s); changed = true; }
		}
		if (changed) this._set(l);
	}
	remove(...ts) {
		const l = this._get();
		const r = l.filter(x => !ts.map(String).includes(x));
		if (r.length !== l.length) this._set(r);
	}
	toggle(t, force) {
		const s = String(t);
		const has = this.contains(s);
		const want = force === undefined ? !has : !!force;
		if (want && !has) this.add(s);
		else if (!want && has) this.remove(s);
		return want;
	}
	replace(a, b) {
		const l = this._get();
		const i = l.indexOf(String(a));
		if (i < 0) return false;
		l[i] = String(b);
		this._set(l.filter((x, k) => l.indexOf(x) === k));
		return true;
	}
	supports() { return true; }
	forEach(fn, thisArg) { this._get().forEach((t, i) => fn.call(thisArg, t, i, this)); }
	entries() { return this._get().entries(); }
	keys() { return this._get().keys(); }
	values() { return this._get().values(); }
	[Symbol.iterator]() { return this._get()[Symbol.iterator](); }
	toString() { return this.value; }
}
G.DOMTokenList = DOMTokenList;

/* ---- CSSStyleDeclaration (an element's style attribute) ---------------------------- */

function kebab(p) {
	if (p === 'cssFloat')
		return 'float';
	if (p.startsWith('--'))
		return p;
	let s = p.replace(/[A-Z]/g, c => '-' + c.toLowerCase());
	if (/^(webkit|moz|ms|o)-/.test(s))
		s = '-' + s;
	return s;
}

/* What libcss understands (N.cssKept): a property it knows, a value it accepts for it, a
 * selector it parses -- element.style and CSS.supports answer from it, as a browser does
 * (an unknown property is not in element.style; an invalid value is not set). */
const CSS_KNOWN = new Map();
function cssValid(k, v) {
	if (k.startsWith('--'))
		return true;
	v = String(v);
	if (/[;{}]/.test(v.replace(/"[^"]*"|'[^']*'|\([^)]*\)/g, '')))
		return false;
	const r = N.cssKept(k + ': ' + v, true);
	return r !== null && r[1] > 0;
}
function cssKnown(k) {
	if (k.startsWith('--'))
		return true;
	let known = CSS_KNOWN.get(k);
	if (known === undefined) {
		known = cssValid(k, 'inherit');
		CSS_KNOWN.set(k, known);
	}
	return known;
}
function cssSelectorValid(sel) {
	const r = N.cssKept(String(sel) + ' { color: red }', false);
	return r !== null && r[0] > 0;
}
/* CSS.supports (conditionText): "(prop: value)", "selector(...)", "not", "and", "or" */
function cssSupports(text) {
	const s = String(text).trim();
	let i = 0;
	const ws = () => { while (i < s.length && /\s/.test(s[i])) i++; };
	const group = () => {		/* s[i] is '(': its inside, i past the ')' */
		let depth = 0, start = i + 1;
		for (; i < s.length; i++) {
			if (s[i] === '(') depth++;
			else if (s[i] === ')' && --depth === 0) { i++; return s.slice(start, i - 1); }
		}
		return null;
	};
	const cond = () => {
		ws();
		if (/^not\b/i.test(s.slice(i))) { i += 3; return !cond(); }
		let r = term();
		for (;;) {
			ws();
			const m = /^(and|or)\b/i.exec(s.slice(i));
			if (!m) return r;
			i += m[1].length;
			const t = term();
			r = m[1].toLowerCase() === 'and' ? r && t : r || t;
		}
	};
	const term = () => {
		ws();
		if (/^selector\(/i.test(s.slice(i))) {
			i += 8;
			const g = group();
			return g !== null && cssSelectorValid(g);
		}
		if (s[i] !== '(') return false;
		const g = group();
		if (g === null) return false;
		const inner = g.trim();
		if (/^(not\b|\(|selector\()/i.test(inner))
			return cssSupports(inner);
		const c = inner.indexOf(':');
		if (c < 0) return false;
		return cssValid(inner.slice(0, c).trim().toLowerCase(), inner.slice(c + 1).trim());
	};
	try {
		const r = cond();
		ws();
		return i >= s.length && r;
	} catch (e) {
		return false;
	}
}

/* ---- a minimal CSSOM (read-only, from a <style>'s text): what libcss keeps ----------------- */
const FONT_FACE_DESC = new Set(['font-family', 'src', 'font-style', 'font-weight', 'unicode-range']);

function cssSplit(text) {	/* the top-level rules: [{ prelude, body (null: a statement) }] */
	const out = [];
	text = String(text).replace(/\/\*[\s\S]*?\*\//g, '');
	let i = 0, start = 0, quote = null, depth = 0, bodyStart = -1, prelude = '';
	for (; i < text.length; i++) {
		const c = text[i];
		if (quote) { if (c === '\\') i++; else if (c === quote) quote = null; continue; }
		if (c === '"' || c === "'") { quote = c; continue; }
		if (c === '{') {
			if (depth++ === 0) { prelude = text.slice(start, i).trim(); bodyStart = i + 1; }
		} else if (c === '}') {
			if (depth > 0 && --depth === 0) {
				out.push({ prelude, body: text.slice(bodyStart, i) });
				start = i + 1;
			}
		} else if (c === ';' && depth === 0) {
			const st = text.slice(start, i).trim();
			if (st) out.push({ prelude: st, body: null });
			start = i + 1;
		}
	}
	return out;
}

function cssDeclObject(body, ok) {	/* a rule's declarations kept (ok (name, value)) */
	const o = { length: 0, cssText: '' };
	const vals = new Map();
	for (const [k, d] of parseDecls(body)) {
		if (!ok(k, d.v)) continue;
		vals.set(k, d.v);
		o[o.length++] = k;
		o[k.replace(/-([a-z])/g, (m, c) => c.toUpperCase())] = d.v;
	}
	o.cssText = [...vals].map(([k, v]) => k + ': ' + v + ';').join(' ');
	o.getPropertyValue = k => vals.get(String(k).toLowerCase()) || '';
	o.item = i => o[i] || '';
	return o;
}

function cssRuleList(text) {
	const rules = [];
	for (const r of cssSplit(text)) {
		const whole = r.prelude + (r.body === null ? ';' : ' {' + r.body + '}');
		const kept = N.cssKept(whole, false);
		if (kept === null || kept[0] === 0)
			continue;		/* (libcss dropped it: an unknown rule, a bad selector) */
		const at = /^@([\w-]+)\s*(.*)$/s.exec(r.prelude);
		let rule;
		if (!at) {
			rule = { type: 1, selectorText: r.prelude, style: cssDeclObject(r.body || '', cssValid) };
		} else {
			const name = at[1].toLowerCase();
			if (name === 'font-face')
				rule = { type: 5, style: cssDeclObject(r.body || '', k => FONT_FACE_DESC.has(k)) };
			else if (name === 'page')
				rule = { type: 6, selectorText: at[2], style: cssDeclObject(r.body || '', cssValid) };
			else if (name === 'media')
				rule = { type: 4, media: { mediaText: at[2] }, conditionText: at[2], cssRules: cssRuleList(r.body || '') };
			else if (name === 'supports')
				rule = { type: 12, conditionText: at[2], cssRules: cssRuleList(r.body || '') };
			else if (name === 'import')
				rule = { type: 3, href: at[2].replace(/^url\(|\)$|["']/g, '') };
			else if (name === 'namespace')
				rule = { type: 10 };
			else
				continue;
		}
		rule.cssText = whole;
		rules.push(rule);
	}
	return rules;
}

function cssSheetOf(el) {
	const rules = cssRuleList(el.textContent || '');
	return { type: 'text/css', disabled: false, ownerNode: el, href: null, title: null,
		cssRules: rules, rules, media: { mediaText: N.attr(el, 'media') || '' },
		insertRule() { return 0; }, deleteRule() {} };
}

function parseDecls(text) {
	const out = new Map();
	if (!text)
		return out;
	let depth = 0, quote = null, start = 0;
	const parts = [];
	for (let i = 0; i < text.length; i++) {
		const c = text[i];
		if (quote) { if (c === quote) quote = null; continue; }
		if (c === '"' || c === "'") quote = c;
		else if (c === '(') depth++;
		else if (c === ')') depth--;
		else if (c === ';' && depth === 0) { parts.push(text.slice(start, i)); start = i + 1; }
	}
	parts.push(text.slice(start));
	for (const part of parts) {
		const i = part.indexOf(':');
		if (i < 0) continue;
		const k = part.slice(0, i).trim().toLowerCase();
		let v = part.slice(i + 1).trim();
		let pri = '';
		const m = v.match(/\s*!\s*important\s*$/i);
		if (m) { pri = 'important'; v = v.slice(0, m.index).trim(); }
		if (k) out.set(k.startsWith('--') ? part.slice(0, i).trim() : k, { v, pri });
	}
	return out;
}

function serializeDecls(map) {
	const out = [];
	for (const [k, d] of map)
		out.push(k + ': ' + d.v + (d.pri ? ' !important' : '') + ';');
	return out.join(' ');
}

class CSSStyleDeclaration {
	constructor(el) {
		Object.defineProperty(this, '_el', { value: el });
	}
	_map() { return this._el ? parseDecls(N.attr(this._el, 'style')) : new Map(); }
	_write(map) {
		if (!this._el) return;
		const s = serializeDecls(map);
		if (s) N.setAttr(this._el, 'style', s);
		else N.removeAttr(this._el, 'style');
	}
	get cssText() { return this._el ? (N.attr(this._el, 'style') || '') : ''; }
	set cssText(v) { if (this._el) N.setAttr(this._el, 'style', String(v)); }
	get length() { return this._map().size; }
	item(i) { return [...this._map().keys()][i] || ''; }
	getPropertyValue(p) {
		const d = this._map().get(p.startsWith('--') ? p : lower(p));
		return d ? d.v : '';
	}
	getPropertyPriority(p) {
		const d = this._map().get(lower(p));
		return d ? d.pri : '';
	}
	setProperty(p, v, pri) {
		const k = p.startsWith('--') ? p : lower(p);
		const map = this._map();
		if (v === null || v === undefined || v === '')
			map.delete(k);
		else if (cssValid(k, v))
			map.set(k, { v: String(v), pri: pri ? 'important' : '' });
		else
			return;		/* (an invalid value is not set: the old one stays) */
		this._write(map);
	}
	removeProperty(p) {
		const k = p.startsWith('--') ? p : lower(p);
		const map = this._map();
		const d = map.get(k);
		map.delete(k);
		this._write(map);
		return d ? d.v : '';
	}
}
G.CSSStyleDeclaration = CSSStyleDeclaration;

const STYLE_OWN = new Set(['cssText', 'length', 'item', 'getPropertyValue',
	'getPropertyPriority', 'setProperty', 'removeProperty', '_el', '_map', '_write',
	'parentRule', 'constructor']);

function styleProxy(el) {
	return new Proxy(new CSSStyleDeclaration(el), {
		get(t, p, r) {
			if (typeof p !== 'string' || STYLE_OWN.has(p) || p in Object.prototype)
				return Reflect.get(t, p, t);
			if (/^\d+$/.test(p))
				return t.item(+p);
			return t.getPropertyValue(kebab(p));
		},
		set(t, p, v) {
			if (typeof p !== 'string' || STYLE_OWN.has(p))
				return Reflect.set(t, p, v, t);
			t.setProperty(kebab(p), v);
			return true;
		},
		has(t, p) {
			if (typeof p !== 'string')
				return p in t;
			return STYLE_OWN.has(p) || p in t || /^\d+$/.test(p) || cssKnown(kebab(p));
		},
	});
}

/* ---- Attr, NamedNodeMap --------------------------------------------------------------- */

class Attr {
	constructor(el, name, value) {
		this.ownerElement = el;
		this.name = this.localName = this.nodeName = name;
		this._value = value;
		this.specified = true;
		this.namespaceURI = null;
		this.prefix = null;
	}
	get value() { return this._value; }
	set value(v) {
		this._value = String(v);
		if (this.ownerElement) this.ownerElement.setAttribute(this.name, v);
	}
	get nodeValue() { return this._value; }
	set nodeValue(v) { this.value = v; }
	get nodeType() { return ATTRIBUTE_NODE; }
}
G.Attr = Attr;

class NamedNodeMap extends Array {
	getNamedItem(name) { return this.find(a => a.name === lower(name)) || null; }
	item(i) { return this[i] || null; }
	static get [Symbol.species]() { return Array; }
}
G.NamedNodeMap = NamedNodeMap;

/* ---- the selectors ----------------------------------------------------------------------- */

const selectorCache = new Map();

function parseSelector(text) {
	text = String(text);
	let list = selectorCache.get(text);
	if (list)
		return list;
	list = new SelectorParser(text).list();
	if (selectorCache.size > 500)
		selectorCache.clear();
	selectorCache.set(text, list);
	return list;
}

class SelectorParser {
	constructor(s) { this.s = s; this.i = 0; }
	err() { throw new DOMException("'" + this.s + "' is not a valid selector", 'SyntaxError'); }
	ws() { while (this.i < this.s.length && /\s/.test(this.s[this.i])) this.i++; }
	peek() { return this.s[this.i]; }
	ident() {
		const m = /^-?(?:[_a-zA-Z -￿]|\\.)(?:[-_a-zA-Z0-9 -￿]|\\.)*/.exec(this.s.slice(this.i));
		if (!m) return null;
		this.i += m[0].length;
		return m[0].replace(/\\(.)/g, '$1');
	}
	string() {
		const q = this.s[this.i];
		let out = '';
		this.i++;
		while (this.i < this.s.length && this.s[this.i] !== q) {
			if (this.s[this.i] === '\\') this.i++;
			out += this.s[this.i++];
		}
		this.i++;
		return out;
	}
	list() {
		const out = [];
		for (;;) {
			this.ws();
			out.push(this.complex());
			this.ws();
			if (this.peek() === ',') { this.i++; continue; }
			if (this.i < this.s.length && this.peek() !== ')') this.err();
			return out;
		}
	}
	/* a complex selector: compounds and their combinators, right to left at match */
	complex() {
		const parts = [];
		let comb = null;
		this.ws();
		if ('>+~'.includes(this.peek())) {	/* a relative selector (:has) */
			comb = this.s[this.i++];
			this.ws();
			parts.push({ comb: 'scope', c: null });
		}
		for (;;) {
			const c = this.compound();
			parts.push({ comb: comb || ' ', c });
			const save = this.i;
			this.ws();
			const ch = this.peek();
			if (ch === '>' || ch === '+' || ch === '~') {
				this.i++;
				this.ws();
				comb = ch;
			} else if (this.i > save && ch !== undefined && ch !== ',' && ch !== ')') {
				comb = ' ';
			} else {
				this.i = save;
				return parts;
			}
		}
	}
	compound() {
		const c = { tag: null, ids: [], classes: [], attrs: [], pseudos: [] };
		let any = false;
		if (this.peek() === '*') { this.i++; any = true; }
		else {
			const t = this.ident();
			if (t) { c.tag = t.toLowerCase(); any = true; }
		}
		if (this.peek() === '|') { this.i++; const t = this.ident(); c.tag = t ? t.toLowerCase() : null; }
		for (;;) {
			const ch = this.peek();
			if (ch === '#') { this.i++; const id = this.ident(); if (id === null) this.err(); c.ids.push(id); }
			else if (ch === '.') { this.i++; const cl = this.ident(); if (cl === null) this.err(); c.classes.push(cl); }
			else if (ch === '[') c.attrs.push(this.attr());
			else if (ch === ':') c.pseudos.push(this.pseudo());
			else break;
			any = true;
		}
		if (!any) this.err();
		return c;
	}
	attr() {
		this.i++;
		this.ws();
		const name = this.ident();
		if (name === null) this.err();
		this.ws();
		let op = null, value = null, flag = null;
		const m = /^([~|^$*]?=)/.exec(this.s.slice(this.i));
		if (m) {
			op = m[1];
			this.i += op.length;
			this.ws();
			const ch = this.peek();
			value = (ch === '"' || ch === "'") ? this.string() : this.ident();
			if (value === null) this.err();
			this.ws();
			const f = /^[iIsS](?=\s*\])/.exec(this.s.slice(this.i));
			if (f) { flag = f[0].toLowerCase(); this.i++; this.ws(); }
		}
		if (this.peek() !== ']') this.err();
		this.i++;
		return { name: name.toLowerCase(), op, value, ci: flag === 'i' };
	}
	pseudo() {
		this.i++;
		let element = false;
		if (this.peek() === ':') { this.i++; element = true; }
		const name = this.ident();
		if (name === null) this.err();
		const p = { name: name.toLowerCase(), element, arg: null };
		if (this.peek() === '(') {
			this.i++;
			this.ws();
			if (['not', 'is', 'where', 'matches', '-webkit-any', 'has'].includes(p.name)) {
				p.arg = this.list();
			} else if (/^nth-/.test(p.name)) {
				const m = /^([^)]*?)(\s+of\s+([^)]*))?\s*(?=\))/.exec(this.s.slice(this.i));
				if (!m) this.err();
				p.arg = parseNth(m[1].trim());
				if (m[3]) p.of = parseSelector(m[3]);
				this.i += m[0].length;
			} else {
				const start = this.i;
				let depth = 1;
				while (this.i < this.s.length) {
					if (this.s[this.i] === '(') depth++;
					else if (this.s[this.i] === ')' && --depth === 0) break;
					this.i++;
				}
				p.arg = this.s.slice(start, this.i).trim();
			}
			this.ws();
			if (this.peek() !== ')') this.err();
			this.i++;
		}
		return p;
	}
}

function parseNth(s) {
	s = s.replace(/\s+/g, '').toLowerCase();
	if (s === 'odd') return [2, 1];
	if (s === 'even') return [2, 0];
	const m = /^([+-]?\d*)n([+-]\d+)?$/.exec(s);
	if (m) {
		const a = m[1] === '' || m[1] === '+' ? 1 : m[1] === '-' ? -1 : parseInt(m[1], 10);
		return [a, m[2] ? parseInt(m[2], 10) : 0];
	}
	if (/^[+-]?\d+$/.test(s)) return [0, parseInt(s, 10)];
	throw new DOMException("bad :nth '" + s + "'", 'SyntaxError');
}

function nthMatch(ab, pos) {
	const [a, b] = ab;
	if (a === 0) return pos === b;
	const n = (pos - b) / a;
	return Number.isInteger(n) && n >= 0;
}

function elementSiblings(e) {
	const p = N.parent(e);
	return p ? N.children(p).filter(isElement) : [e];
}

function matchList(e, list, scope) {
	for (const cx of list)
		if (matchComplex(e, cx, cx.length - 1, scope))
			return true;
	return false;
}

function matchComplex(e, parts, k, scope) {
	const part = parts[k];
	if (part.comb === 'scope')
		return e === scope;
	if (!matchCompound(e, part.c, scope))
		return false;
	if (k === 0)
		return true;
	const comb = part.comb;
	if (comb === '>') {
		const p = N.parent(e);
		return !!(p && (parts[k - 1].comb === 'scope' ? p === scope :
			N.type(p) === ELEMENT_NODE && matchComplex(p, parts, k - 1, scope)));
	}
	if (comb === ' ') {
		for (let p = N.parent(e); p; p = N.parent(p)) {
			if (parts[k - 1].comb === 'scope') { if (p === scope) return true; continue; }
			if (N.type(p) === ELEMENT_NODE && matchComplex(p, parts, k - 1, scope))
				return true;
		}
		return false;
	}
	if (comb === '+') {
		let s = N.prev(e);
		while (s && N.type(s) !== ELEMENT_NODE) s = N.prev(s);
		if (!s) return false;
		return parts[k - 1].comb === 'scope' ? s === scope : matchComplex(s, parts, k - 1, scope);
	}
	if (comb === '~') {
		for (let s = N.prev(e); s; s = N.prev(s)) {
			if (N.type(s) !== ELEMENT_NODE) continue;
			if (parts[k - 1].comb === 'scope' ? s === scope : matchComplex(s, parts, k - 1, scope))
				return true;
		}
		return false;
	}
	return false;
}

function matchCompound(e, c, scope) {
	if (N.type(e) !== ELEMENT_NODE)
		return false;
	if (c.tag && c.tag !== '*' && e.localName !== c.tag)
		return false;
	for (const id of c.ids)
		if (N.attr(e, 'id') !== id) return false;
	if (c.classes.length) {
		const cls = (N.attr(e, 'class') || '').split(/\s+/);
		for (const k of c.classes)
			if (!cls.includes(k)) return false;
	}
	for (const a of c.attrs) {
		let v = N.attr(e, a.name);
		if (v === null) return false;
		if (!a.op) continue;
		let want = a.value;
		if (a.ci) { v = v.toLowerCase(); want = want.toLowerCase(); }
		switch (a.op) {
		case '=': if (v !== want) return false; break;
		case '~=': if (!v.split(/\s+/).includes(want)) return false; break;
		case '|=': if (v !== want && !v.startsWith(want + '-')) return false; break;
		case '^=': if (!want || !v.startsWith(want)) return false; break;
		case '$=': if (!want || !v.endsWith(want)) return false; break;
		case '*=': if (!want || !v.includes(want)) return false; break;
		}
	}
	for (const p of c.pseudos)
		if (!matchPseudo(e, p, scope)) return false;
	return true;
}

function matchPseudo(e, p, scope) {
	if (p.element)
		return false;
	switch (p.name) {
	case 'not': return !matchList(e, p.arg, scope);
	case 'is': case 'where': case 'matches': case '-webkit-any': return matchList(e, p.arg, scope);
	case 'has': return N.descendants(e).some(d => matchList(d, p.arg, e)) ||
		p.arg.some(cx => cx[0].comb === 'scope' && cx[1] && cx[1].comb !== ' ' &&
			elementSiblings(e).some(s => s !== e && matchComplex(s, cx, cx.length - 1, e)));
	case 'root': return N.parent(e) === N.document();
	case 'scope': return scope && scope.nodeType === ELEMENT_NODE ? e === scope : N.parent(e) === N.document();
	case 'empty': return N.children(e).every(c => N.type(c) === COMMENT_NODE ||
		(N.type(c) === TEXT_NODE && N.value(c) === ''));
	case 'first-child': return elementSiblings(e)[0] === e;
	case 'last-child': { const s = elementSiblings(e); return s[s.length - 1] === e; }
	case 'only-child': return elementSiblings(e).length === 1;
	case 'first-of-type': return elementSiblings(e).filter(s => s.localName === e.localName)[0] === e;
	case 'last-of-type': { const s = elementSiblings(e).filter(x => x.localName === e.localName); return s[s.length - 1] === e; }
	case 'only-of-type': return elementSiblings(e).filter(s => s.localName === e.localName).length === 1;
	case 'nth-child': case 'nth-last-child': case 'nth-of-type': case 'nth-last-of-type': {
		let s = elementSiblings(e);
		if (p.name.endsWith('of-type')) s = s.filter(x => x.localName === e.localName);
		if (p.of) { if (!matchList(e, p.of, scope)) return false; s = s.filter(x => matchList(x, p.of, scope)); }
		if (p.name.startsWith('nth-last')) s = s.reverse();
		return nthMatch(p.arg, s.indexOf(e) + 1);
	}
	case 'checked': return !!(e.checked || (e.localName === 'option' && e.selected));
	case 'disabled': return N.attr(e, 'disabled') !== null;
	case 'enabled': return ['input', 'button', 'select', 'textarea', 'option', 'fieldset'].includes(e.localName) &&
		N.attr(e, 'disabled') === null;
	case 'required': return N.attr(e, 'required') !== null;
	case 'optional': return ['input', 'select', 'textarea'].includes(e.localName) && N.attr(e, 'required') === null;
	case 'read-only': return N.attr(e, 'readonly') !== null;
	case 'read-write': return ['input', 'textarea'].includes(e.localName) && N.attr(e, 'readonly') === null;
	case 'placeholder-shown': return N.attr(e, 'placeholder') !== null && !e.value;
	case 'link': case 'any-link': return ['a', 'area', 'link'].includes(e.localName) && N.attr(e, 'href') !== null;
	case 'visited': case 'hover': case 'active': case 'focus-visible': case 'target-within':
		return false;
	case 'focus': return G.document.activeElement === e;
	case 'focus-within': { const a = G.document.activeElement; return !!a && e.contains(a); }
	case 'target': { const h = decodeURIComponent((G.location.hash || '').slice(1)); return !!h && N.attr(e, 'id') === h; }
	case 'defined': return true;
	case 'lang': {
		for (let n = e; n && N.type(n) === ELEMENT_NODE; n = N.parent(n)) {
			const l = N.attr(n, 'lang');
			if (l !== null) return lower(l) === lower(p.arg) || lower(l).startsWith(lower(p.arg) + '-');
		}
		return false;
	}
	case 'dir': return lower(p.arg) === 'ltr';
	default: return false;
	}
}

/* ---- serialising (innerHTML, outerHTML) ------------------------------------------------ */

function escText(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/ /g, '&nbsp;'); }
function escAttr(s) { return s.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/ /g, '&nbsp;'); }

function serialize(n, out) {
	const t = N.type(n);
	if (t === TEXT_NODE) {
		const p = N.parent(n);
		const pn = p && N.type(p) === ELEMENT_NODE ? p.localName : '';
		const d = N.value(n);
		out.push(['script', 'style', 'xmp', 'iframe', 'noembed', 'noframes', 'plaintext', 'noscript'].includes(pn) ? d : escText(d));
	} else if (t === COMMENT_NODE) {
		out.push('<!--' + N.value(n) + '-->');
	} else if (t === ELEMENT_NODE) {
		const tag = n.localName;
		out.push('<' + tag);
		for (const [k, v] of N.attrs(n))
			out.push(' ' + k + '="' + escAttr(v) + '"');
		out.push('>');
		if (VOID_ELEMENTS.has(tag))
			return;
		for (const c of N.children(n))
			serialize(c, out);
		out.push('</' + tag + '>');
	} else if (t === DOCUMENT_FRAGMENT_NODE || t === DOCUMENT_NODE) {
		for (const c of N.children(n))
			serialize(c, out);
	} else if (t === DOCUMENT_TYPE_NODE) {
		out.push('<!DOCTYPE ' + N.name(n) + '>');
	}
}

const RAW_TEXT = new Set(['script', 'style', 'xmp', 'iframe', 'noembed', 'noframes', 'noscript', 'plaintext']);

function innerHTML(n) {
	const out = [];
	for (const c of N.children(n))
		serialize(c, out);
	return out.join('');
}

/* the nodes a piece of HTML makes, in a fragment */
function fragmentFromHTML(html) {
	const holder = N.create('div');
	N.setHTML(holder, String(html));
	const f = N.createFragment();
	for (let c; (c = N.first(holder));)
		N.insert(f, c, null);
	return f;
}

/* ---- DOMRect ---------------------------------------------------------------------------- */

class DOMRectReadOnly {
	constructor(x = 0, y = 0, w = 0, h = 0) {
		this.x = x; this.y = y; this.width = w; this.height = h;
	}
	get top() { return Math.min(this.y, this.y + this.height); }
	get left() { return Math.min(this.x, this.x + this.width); }
	get right() { return Math.max(this.x, this.x + this.width); }
	get bottom() { return Math.max(this.y, this.y + this.height); }
	toJSON() { return { x: this.x, y: this.y, width: this.width, height: this.height,
		top: this.top, left: this.left, right: this.right, bottom: this.bottom }; }
	static fromRect(r = {}) { return new this(r.x, r.y, r.width, r.height); }
}
class DOMRect extends DOMRectReadOnly {}
G.DOMRectReadOnly = DOMRectReadOnly;
G.DOMRect = DOMRect;

/* ---- Element ------------------------------------------------------------------------------ */

class Element extends Node {
	get tagName() { return N.name(this).toUpperCase(); }
	get localName() { return N.name(this).toLowerCase(); }
	get namespaceURI() { return 'http://www.w3.org/1999/xhtml'; }
	get prefix() { return null; }
	get id() { return N.attr(this, 'id') || ''; }
	set id(v) { this.setAttribute('id', v); }
	get className() { return N.attr(this, 'class') || ''; }
	set className(v) { this.setAttribute('class', v); }
	get classList() {
		let l = this._classList;
		if (!l) Object.defineProperty(this, '_classList', { value: l = new DOMTokenList(this, 'class') });
		return l;
	}
	set classList(v) { this.setAttribute('class', v); }
	get slot() { return N.attr(this, 'slot') || ''; }
	getAttribute(name) { return N.attr(this, lower(name)); }
	getAttributeNS(ns, name) { return N.attr(this, lower(name)); }
	setAttribute(name, value) {
		const k = lower(name);
		const old = observers.size ? N.attr(this, k) : null;
		N.setAttr(this, k, String(value));
		if (observers.size)
			queueMutation({ type: 'attributes', target: this, attributeName: k, oldValue: old,
				addedNodes: nodeList([]), removedNodes: nodeList([]) });
	}
	setAttributeNS(ns, name, value) { this.setAttribute(name.replace(/^.*:/, ''), value); }
	removeAttribute(name) {
		const k = lower(name);
		const old = N.attr(this, k);
		if (old === null) return;
		N.removeAttr(this, k);
		if (observers.size)
			queueMutation({ type: 'attributes', target: this, attributeName: k, oldValue: old,
				addedNodes: nodeList([]), removedNodes: nodeList([]) });
	}
	removeAttributeNS(ns, name) { this.removeAttribute(name); }
	hasAttribute(name) { return N.attr(this, lower(name)) !== null; }
	hasAttributeNS(ns, name) { return this.hasAttribute(name); }
	hasAttributes() { return N.attrs(this).length > 0; }
	toggleAttribute(name, force) {
		const has = this.hasAttribute(name);
		const want = force === undefined ? !has : !!force;
		if (want && !has) this.setAttribute(name, '');
		else if (!want && has) this.removeAttribute(name);
		return want;
	}
	getAttributeNames() { return N.attrs(this).map(a => a[0]); }
	get attributes() {
		const m = new NamedNodeMap();
		for (const [k, v] of N.attrs(this))
			m.push(new Attr(this, k, v));
		return m;
	}
	getAttributeNode(name) {
		const v = N.attr(this, lower(name));
		return v === null ? null : new Attr(this, lower(name), v);
	}
	get innerHTML() { return innerHTML(this); }
	set innerHTML(v) {
		const removed = observers.size ? N.children(this) : [];
		v = v === null ? '' : String(v);
		/* Onyx: the fragment parser's context -- a raw text element's markup is its text
		 * (a tag manager's script.innerHTML = code lost every "<...>" of the code: bbc's
		 * consent stub became a SyntaxError), an RCDATA one's has its entities decoded */
		const tag = N.name(this).toLowerCase();
		if (RAW_TEXT.has(tag)) {
			this.textContent = v;
		} else if (tag === 'textarea' || tag === 'title') {
			const d = N.create('div');
			N.setHTML(d, v.replace(/</g, '&lt;'));
			this.textContent = N.text(d);
		} else {
			N.setHTML(this, v);
		}
		if (observers.size)
			childListRecord(this, N.children(this), removed);
	}
	get outerHTML() { const out = []; serialize(this, out); return out.join(''); }
	set outerHTML(v) {
		const p = N.parent(this);
		if (!p) return;
		p.insertBefore(fragmentFromHTML(v), this);
		p.removeChild(this);
	}
	insertAdjacentHTML(pos, html) { this.insertAdjacentElement(pos, fragmentFromHTML(html)); }
	insertAdjacentText(pos, text) { this.insertAdjacentElement(pos, N.createText(String(text))); }
	insertAdjacentElement(pos, node) {
		const p = N.parent(this);
		switch (lower(pos)) {
		case 'beforebegin': if (p) p.insertBefore(node, this); break;
		case 'afterbegin': this.insertBefore(node, N.first(this)); break;
		case 'beforeend': this.appendChild(node); break;
		case 'afterend': if (p) p.insertBefore(node, N.next(this)); break;
		default: throw new DOMException('bad position', 'SyntaxError');
		}
		return node;
	}
	matches(sel) { return matchList(this, parseSelector(sel), this); }
	webkitMatchesSelector(sel) { return this.matches(sel); }
	msMatchesSelector(sel) { return this.matches(sel); }
	closest(sel) {
		const list = parseSelector(sel);
		for (let n = this; n && N.type(n) === ELEMENT_NODE; n = N.parent(n))
			if (matchList(n, list, n)) return n;
		return null;
	}
	getBoundingClientRect() { const r = N.rect(this); return new DOMRect(r[0], r[1], r[2], r[3]); }
	getClientRects() { const r = this.getBoundingClientRect(); return r.width || r.height ? [r] : []; }
	get offsetWidth() { return N.rect(this)[2]; }
	get offsetHeight() { return N.rect(this)[3]; }
	get offsetTop() { return N.rect(this)[1] + (G.scrollY || 0); }
	get offsetLeft() { return N.rect(this)[0] + (G.scrollX || 0); }
	get offsetParent() { return N.boxed(this) ? G.document.body : null; }
	get clientWidth() {
		if (this === G.document.documentElement) return G.innerWidth;
		return N.rect(this)[2];
	}
	get clientHeight() {
		if (this === G.document.documentElement) return G.innerHeight;
		return N.rect(this)[3];
	}
	get clientTop() { return 0; }
	get clientLeft() { return 0; }
	get scrollWidth() {
		if (this === G.document.documentElement || this === G.document.body) return N.scroll()[4];
		return N.rect(this)[2];
	}
	get scrollHeight() {
		if (this === G.document.documentElement || this === G.document.body) return N.scroll()[5];
		return N.rect(this)[3];
	}
	get scrollTop() {
		return this === G.document.documentElement || this === G.document.body ? N.scroll()[1] : 0;
	}
	set scrollTop(v) {
		if (this === G.document.documentElement || this === G.document.body)
			N.scrollTo(N.scroll()[0], +v || 0);
	}
	get scrollLeft() {
		return this === G.document.documentElement || this === G.document.body ? N.scroll()[0] : 0;
	}
	set scrollLeft(v) {
		if (this === G.document.documentElement || this === G.document.body)
			N.scrollTo(+v || 0, N.scroll()[1]);
	}
	scrollIntoView(arg) {
		const r = N.rect(this);
		const s = N.scroll();
		let y = s[1] + r[1];
		if (arg && typeof arg === 'object' && arg.block === 'center') y -= (s[3] - r[3]) / 2;
		else if (arg && typeof arg === 'object' && arg.block === 'end') y -= s[3] - r[3];
		else if (arg === false) y -= s[3] - r[3];
		N.scrollTo(s[0], Math.max(0, Math.round(y)));
		scheduleObservers();
	}
	scroll(x, y) {}
	scrollTo(x, y) {}
	scrollBy(x, y) {}
	requestFullscreen() { return Promise.reject(new DOMException('no', 'NotAllowedError')); }
	attachShadow() { return this; }
	get shadowRoot() { return null; }
	animate() {
		const a = { finished: Promise.resolve(), onfinish: null, cancel() {}, play() {},
			pause() {}, finish() {}, reverse() {}, playState: 'finished' };
		setTimeout(() => { if (a.onfinish) a.onfinish(); }, 0);
		return a;
	}
	getAnimations() { return []; }
	releasePointerCapture() {}
	setPointerCapture() {}
	hasPointerCapture() { return false; }
}
mixin(Element, ParentNode);
mixin(Element, ChildNode);
G.Element = Element;

/* ---- HTMLElement --------------------------------------------------------------------- */

function reflectString(proto, prop, attr = prop.toLowerCase()) {
	Object.defineProperty(proto, prop, { configurable: true,
		get() { return N.attr(this, attr) || ''; },
		set(v) { this.setAttribute(attr, v); } });
}
function reflectBool(proto, prop, attr = prop.toLowerCase()) {
	Object.defineProperty(proto, prop, { configurable: true,
		get() { return N.attr(this, attr) !== null; },
		set(v) { this.toggleAttribute(attr, !!v); } });
}
function reflectURL(proto, prop, attr = prop.toLowerCase()) {
	Object.defineProperty(proto, prop, { configurable: true,
		get() {
			const v = N.attr(this, attr);
			if (v === null) return '';
			try { return new URL(v, N.url()).href; } catch (e) { return v; }
		},
		set(v) { this.setAttribute(attr, v); } });
}
function reflectNumber(proto, prop, attr, dflt) {
	Object.defineProperty(proto, prop, { configurable: true,
		get() { const v = parseInt(N.attr(this, attr), 10); return isNaN(v) ? dflt : v; },
		set(v) { this.setAttribute(attr, String(v)); } });
}

class HTMLElement extends Element {
	get dataset() {
		const el = this;
		return new Proxy({}, {
			get(t, p) {
				if (typeof p !== 'string') return undefined;
				const v = N.attr(el, 'data-' + p.replace(/[A-Z]/g, c => '-' + c.toLowerCase()));
				return v === null ? undefined : v;
			},
			set(t, p, v) {
				el.setAttribute('data-' + String(p).replace(/[A-Z]/g, c => '-' + c.toLowerCase()), v);
				return true;
			},
			deleteProperty(t, p) {
				el.removeAttribute('data-' + String(p).replace(/[A-Z]/g, c => '-' + c.toLowerCase()));
				return true;
			},
			has(t, p) {
				return typeof p === 'string' &&
					N.attr(el, 'data-' + p.replace(/[A-Z]/g, c => '-' + c.toLowerCase())) !== null;
			},
			ownKeys() {
				return N.attrs(el).filter(a => a[0].startsWith('data-'))
					.map(a => a[0].slice(5).replace(/-([a-z])/g, (m, c) => c.toUpperCase()));
			},
			getOwnPropertyDescriptor(t, p) {
				const v = N.attr(el, 'data-' + String(p).replace(/[A-Z]/g, c => '-' + c.toLowerCase()));
				return v === null ? undefined : { value: v, writable: true, enumerable: true, configurable: true };
			},
		});
	}
	get style() {
		let s = this._style;
		if (!s) Object.defineProperty(this, '_style', { value: s = styleProxy(this) });
		return s;
	}
	set style(v) { this.setAttribute('style', v); }
	get innerText() { return N.text(this); }
	set innerText(v) { this.textContent = v; }
	get outerText() { return N.text(this); }
	get isContentEditable() { return false; }
	get tabIndex() {
		const v = parseInt(N.attr(this, 'tabindex'), 10);
		if (!isNaN(v)) return v;
		return ['a', 'button', 'input', 'select', 'textarea'].includes(this.localName) ? 0 : -1;
	}
	set tabIndex(v) { this.setAttribute('tabindex', String(v)); }
	focus() {
		const old = activeElement;
		if (old === this) return;
		activeElement = this;
		if (old) { dispatch(old, new FocusEvent('blur')); dispatch(old, new FocusEvent('focusout', { bubbles: true })); }
		dispatch(this, new FocusEvent('focus'));
		dispatch(this, new FocusEvent('focusin', { bubbles: true }));
	}
	blur() {
		if (activeElement !== this) return;
		activeElement = null;
		dispatch(this, new FocusEvent('blur'));
		dispatch(this, new FocusEvent('focusout', { bubbles: true }));
	}
	click() {
		const ev = new MouseEvent('click', { bubbles: true, cancelable: true });
		if (dispatch(this, ev))
			activate(this);
	}
}
for (const p of ['title', 'lang', 'dir', 'accessKey', 'translate', 'autocapitalize', 'enterKeyHint', 'inputMode', 'nonce'])
	reflectString(HTMLElement.prototype, p);
reflectBool(HTMLElement.prototype, 'hidden');
reflectBool(HTMLElement.prototype, 'draggable');
reflectBool(HTMLElement.prototype, 'inert');
Object.defineProperty(HTMLElement.prototype, 'contentEditable', { configurable: true,
	get() { return N.attr(this, 'contenteditable') || 'inherit'; },
	set(v) { this.setAttribute('contenteditable', v); } });
defineHandlers(HTMLElement.prototype);
G.HTMLElement = HTMLElement;

let activeElement = null;

/* a click's default action for elements that have one (a script's click()) */
function activate(el) {
	for (let n = el; n && N.type(n) === ELEMENT_NODE; n = N.parent(n)) {
		const tag = n.localName;
		if (tag === 'a' && N.attr(n, 'href') !== null) {
			const href = N.attr(n, 'href');
			if (/^javascript:/i.test(href)) {
				try { (0, eval)(decodeURIComponent(href.slice(11))); } catch (e) { report(e); }
			} else if (href.startsWith('#')) {
				G.location.hash = href;
			} else {
				N.navigate(href);
			}
			return;
		}
		if ((tag === 'button' && (N.attr(n, 'type') || 'submit').toLowerCase() === 'submit') ||
			(tag === 'input' && ['submit', 'image'].includes((N.attr(n, 'type') || '').toLowerCase()))) {
			const f = n.form;
			if (f) f.requestSubmit(n);
			return;
		}
		if (tag === 'input' && ['checkbox', 'radio'].includes((N.attr(n, 'type') || '').toLowerCase())) {
			n.checked = (N.attr(n, 'type') || '').toLowerCase() === 'radio' ? true : !n.checked;
			dispatch(n, new Event('input', { bubbles: true }));
			dispatch(n, new Event('change', { bubbles: true }));
			return;
		}
		if (tag === 'label') {
			const c = n.control;
			if (c && c !== el) c.click();
			return;
		}
		if (tag === 'summary') {
			const d = N.parent(n);
			if (d && d.localName === 'details') d.toggleAttribute('open');
			return;
		}
	}
}

/* ---- the element classes ------------------------------------------------------------- */

const TAGS = {};

function htmlClass(name, tags, body) {
	const cls = body || class extends HTMLElement {};
	Object.defineProperty(cls, 'name', { value: name });
	G[name] = cls;
	for (const t of tags)
		TAGS[t] = cls.prototype;
	return cls;
}

class HTMLAnchorElement extends HTMLElement {
	get href() {
		const v = N.attr(this, 'href');
		if (v === null) return '';
		try { return new URL(v, N.url()).href; } catch (e) { return v; }
	}
	set href(v) { this.setAttribute('href', v); }
	get _u() { try { return new URL(this.href); } catch (e) { return null; } }
	get protocol() { const u = this._u; return u ? u.protocol : ''; }
	get host() { const u = this._u; return u ? u.host : ''; }
	get hostname() { const u = this._u; return u ? u.hostname : ''; }
	get port() { const u = this._u; return u ? u.port : ''; }
	get pathname() { const u = this._u; return u ? u.pathname : ''; }
	get search() { const u = this._u; return u ? u.search : ''; }
	get hash() { const u = this._u; return u ? u.hash : ''; }
	get origin() { const u = this._u; return u ? u.origin : ''; }
	get text() { return N.text(this); }
	get relList() { return new DOMTokenList(this, 'rel'); }
	toString() { return this.href; }
}
for (const p of ['target', 'rel', 'download', 'hreflang', 'type', 'referrerPolicy', 'ping'])
	reflectString(HTMLAnchorElement.prototype, p);
htmlClass('HTMLAnchorElement', ['a'], HTMLAnchorElement);
htmlClass('HTMLAreaElement', ['area'], class extends HTMLAnchorElement {});

class HTMLImageElement extends HTMLElement {
	get complete() { return true; }
	get naturalWidth() { return N.rect(this)[2]; }
	get naturalHeight() { return N.rect(this)[3]; }
	get width() { const v = parseInt(N.attr(this, 'width'), 10); return isNaN(v) ? N.rect(this)[2] : v; }
	set width(v) { this.setAttribute('width', String(v)); }
	get height() { const v = parseInt(N.attr(this, 'height'), 10); return isNaN(v) ? N.rect(this)[3] : v; }
	set height(v) { this.setAttribute('height', String(v)); }
	get currentSrc() { return this.src; }
	decode() { return Promise.resolve(); }
}
reflectURL(HTMLImageElement.prototype, 'src');
for (const p of ['alt', 'srcset', 'sizes', 'loading', 'decoding', 'crossOrigin', 'useMap', 'referrerPolicy'])
	reflectString(HTMLImageElement.prototype, p);
reflectBool(HTMLImageElement.prototype, 'isMap');
htmlClass('HTMLImageElement', ['img'], HTMLImageElement);
G.Image = function Image(w, h) {
	const i = N.create('img');
	if (w !== undefined) i.width = w;
	if (h !== undefined) i.height = h;
	return i;
};

/* form controls: their value is NetSurf's control's (what the user typed) */
function controlValue(el) {
	const v = N.formValue(el);
	return v === null ? (N.attr(el, 'value') || '') : v;
}

class HTMLInputElement extends HTMLElement {
	get type() {
		const t = lower(N.attr(this, 'type') || 'text');
		return ['hidden', 'text', 'search', 'tel', 'url', 'email', 'password', 'date', 'month',
			'week', 'time', 'datetime-local', 'number', 'range', 'color', 'checkbox', 'radio',
			'file', 'submit', 'image', 'reset', 'button'].includes(t) ? t : 'text';
	}
	set type(v) { this.setAttribute('type', v); }
	get value() {
		if (this.type === 'checkbox' || this.type === 'radio')
			return N.attr(this, 'value') === null ? 'on' : N.attr(this, 'value');
		return controlValue(this);
	}
	set value(v) {
		if (this.type === 'checkbox' || this.type === 'radio') { this.setAttribute('value', v); return; }
		N.setFormValue(this, v === null ? '' : String(v));
	}
	get valueAsNumber() { const n = parseFloat(this.value); return isNaN(n) ? NaN : n; }
	set valueAsNumber(n) { this.value = String(n); }
	get checked() { const c = N.formChecked(this); return c === null ? N.attr(this, 'checked') !== null : c; }
	set checked(v) {
		if (v && this.type === 'radio' && this.name) {
			const scope = this.form || G.document;
			for (const r of scope.querySelectorAll('input[type=radio]'))
				if (r !== this && r.name === this.name && r.form === this.form) N.setFormChecked(r, false);
		}
		N.setFormChecked(this, !!v);
	}
	get form() { return this.closest('form'); }
	get labels() { return labelsOf(this); }
	get files() { return []; }
	get validity() { return { valid: this.checkValidity(), valueMissing: this.required && !this.value }; }
	get validationMessage() { return ''; }
	get willValidate() { return true; }
	checkValidity() { return !(this.required && !this.value); }
	reportValidity() { return this.checkValidity(); }
	setCustomValidity() {}
	select() {}
	setSelectionRange() {}
	get selectionStart() { return this.value.length; }
	get selectionEnd() { return this.value.length; }
	stepUp(n = 1) { this.value = String((parseFloat(this.value) || 0) + n * (parseFloat(this.step) || 1)); }
	stepDown(n = 1) { this.stepUp(-n); }
	showPicker() {}
}
for (const p of ['name', 'placeholder', 'min', 'max', 'step', 'pattern', 'accept', 'autocomplete', 'inputMode', 'list', 'formAction', 'formMethod', 'dirName'])
	reflectString(HTMLInputElement.prototype, p);
for (const p of ['disabled', 'required', 'readOnly', 'multiple', 'autofocus', 'formNoValidate'])
	reflectBool(HTMLInputElement.prototype, p);
reflectNumber(HTMLInputElement.prototype, 'maxLength', 'maxlength', -1);
reflectNumber(HTMLInputElement.prototype, 'minLength', 'minlength', -1);
reflectNumber(HTMLInputElement.prototype, 'size', 'size', 20);
Object.defineProperty(HTMLInputElement.prototype, 'defaultValue', { configurable: true,
	get() { return N.attr(this, 'value') || ''; }, set(v) { this.setAttribute('value', v); } });
Object.defineProperty(HTMLInputElement.prototype, 'defaultChecked', { configurable: true,
	get() { return N.attr(this, 'checked') !== null; }, set(v) { this.toggleAttribute('checked', !!v); } });
htmlClass('HTMLInputElement', ['input'], HTMLInputElement);

class HTMLTextAreaElement extends HTMLElement {
	get type() { return 'textarea'; }
	get value() { const v = N.formValue(this); return v === null ? N.text(this) : v; }
	set value(v) { N.setFormValue(this, v === null ? '' : String(v)); }
	get defaultValue() { return N.text(this); }
	set defaultValue(v) { this.textContent = v; }
	get form() { return this.closest('form'); }
	get textLength() { return this.value.length; }
	get labels() { return labelsOf(this); }
	checkValidity() { return !(this.required && !this.value); }
	reportValidity() { return this.checkValidity(); }
	setCustomValidity() {}
	select() {}
	setSelectionRange() {}
	get selectionStart() { return this.value.length; }
	get selectionEnd() { return this.value.length; }
}
for (const p of ['name', 'placeholder', 'wrap', 'autocomplete'])
	reflectString(HTMLTextAreaElement.prototype, p);
for (const p of ['disabled', 'required', 'readOnly', 'autofocus'])
	reflectBool(HTMLTextAreaElement.prototype, p);
reflectNumber(HTMLTextAreaElement.prototype, 'rows', 'rows', 2);
reflectNumber(HTMLTextAreaElement.prototype, 'cols', 'cols', 20);
reflectNumber(HTMLTextAreaElement.prototype, 'maxLength', 'maxlength', -1);
htmlClass('HTMLTextAreaElement', ['textarea'], HTMLTextAreaElement);

class HTMLOptionElement extends HTMLElement {
	constructor(text = '', value, defSel, sel) {
		const o = N.create('option');
		if (text) o.textContent = text;
		if (value !== undefined) o.setAttribute('value', value);
		if (defSel) o.setAttribute('selected', '');
		return o;
	}
	get value() { const v = N.attr(this, 'value'); return v === null ? N.text(this).trim() : v; }
	set value(v) { this.setAttribute('value', v); }
	get text() { return N.text(this); }
	set text(v) { this.textContent = v; }
	get label() { return N.attr(this, 'label') || this.text; }
	get selected() { const c = N.formChecked(this); return c === null ? N.attr(this, 'selected') !== null : c; }
	set selected(v) { N.setFormChecked(this, !!v); }
	get defaultSelected() { return N.attr(this, 'selected') !== null; }
	get index() { const s = this.closest('select'); return s ? [...s.options].indexOf(this) : 0; }
	get form() { return this.closest('form'); }
}
reflectBool(HTMLOptionElement.prototype, 'disabled');
htmlClass('HTMLOptionElement', ['option'], HTMLOptionElement);
G.Option = HTMLOptionElement;

class HTMLSelectElement extends HTMLElement {
	get type() { return N.attr(this, 'multiple') !== null ? 'select-multiple' : 'select-one'; }
	get options() {
		const opts = htmlCollection(this.getElementsByTagName('option'));
		const sel = this;
		opts.add = (o, before) => sel.add(o, before);
		opts.remove = i => sel.remove(i);
		Object.defineProperty(opts, 'selectedIndex', { get: () => sel.selectedIndex, set: v => { sel.selectedIndex = v; } });
		return opts;
	}
	get length() { return this.getElementsByTagName('option').length; }
	get selectedOptions() { return htmlCollection([...this.options].filter(o => o.selected)); }
	get selectedIndex() {
		const o = [...this.options];
		const i = o.findIndex(x => x.selected);
		return i >= 0 ? i : (o.length && this.type === 'select-one' ? 0 : -1);
	}
	set selectedIndex(i) {
		[...this.options].forEach((o, k) => { o.selected = k === i; });
	}
	get value() { const i = this.selectedIndex; return i >= 0 ? this.options[i].value : ''; }
	set value(v) {
		let found = false;
		for (const o of this.options) {
			const hit = !found && o.value === String(v);
			o.selected = hit;
			if (hit) found = true;
		}
	}
	item(i) { return this.options[i] || null; }
	namedItem(n) { return this.options.namedItem(n); }
	add(o, before) {
		const ref = typeof before === 'number' ? this.options[before] : before;
		this.insertBefore(o, ref || null);
	}
	remove(i) {
		if (i === undefined) { ChildNode.remove.call(this); return; }
		const o = this.options[i];
		if (o) o.remove();
	}
	get form() { return this.closest('form'); }
	get labels() { return labelsOf(this); }
	checkValidity() { return !(this.required && !this.value); }
	reportValidity() { return this.checkValidity(); }
	setCustomValidity() {}
}
reflectString(HTMLSelectElement.prototype, 'name');
for (const p of ['disabled', 'required', 'multiple', 'autofocus'])
	reflectBool(HTMLSelectElement.prototype, p);
reflectNumber(HTMLSelectElement.prototype, 'size', 'size', 0);
htmlClass('HTMLSelectElement', ['select'], HTMLSelectElement);

class HTMLButtonElement extends HTMLElement {
	get type() { const t = lower(N.attr(this, 'type') || 'submit'); return ['submit', 'reset', 'button'].includes(t) ? t : 'submit'; }
	set type(v) { this.setAttribute('type', v); }
	get value() { return N.attr(this, 'value') || ''; }
	set value(v) { this.setAttribute('value', v); }
	get form() { return this.closest('form'); }
	get labels() { return labelsOf(this); }
	checkValidity() { return true; }
}
reflectString(HTMLButtonElement.prototype, 'name');
reflectBool(HTMLButtonElement.prototype, 'disabled');
reflectBool(HTMLButtonElement.prototype, 'autofocus');
htmlClass('HTMLButtonElement', ['button'], HTMLButtonElement);

function labelsOf(el) {
	const out = [];
	const id = N.attr(el, 'id');
	if (id)
		for (const l of G.document.querySelectorAll('label'))
			if (N.attr(l, 'for') === id) out.push(l);
	const p = el.closest('label');
	if (p && !out.includes(p)) out.push(p);
	return nodeList(out);
}

class HTMLLabelElement extends HTMLElement {
	get htmlFor() { return N.attr(this, 'for') || ''; }
	set htmlFor(v) { this.setAttribute('for', v); }
	get control() {
		const f = N.attr(this, 'for');
		if (f) return G.document.getElementById(f);
		return this.querySelector('input,select,textarea,button');
	}
	get form() { const c = this.control; return c ? c.form : null; }
}
htmlClass('HTMLLabelElement', ['label'], HTMLLabelElement);

class HTMLFormElement extends HTMLElement {
	get elements() {
		const els = htmlCollection(this.querySelectorAll('input,select,textarea,button,fieldset,output,object'));
		const id = N.attr(this, 'id');
		if (id)
			for (const e of G.document.querySelectorAll('[form]'))
				if (N.attr(e, 'form') === id && !els.includes(e)) els.push(e);
		return new Proxy(els, { get(t, p) {
			if (p in t) return t[p];
			if (typeof p === 'string') return t.namedItem(p) || undefined;
			return undefined;
		} });
	}
	get length() { return this.elements.length; }
	get action() {
		const v = N.attr(this, 'action');
		try { return new URL(v || '', N.url()).href; } catch (e) { return v || ''; }
	}
	set action(v) { this.setAttribute('action', v); }
	get method() { const m = lower(N.attr(this, 'method') || 'get'); return m === 'post' ? 'post' : m === 'dialog' ? 'dialog' : 'get'; }
	set method(v) { this.setAttribute('method', v); }
	submit() { submitForm(this, null); }
	requestSubmit(submitter) {
		const ev = new SubmitEvent('submit', { bubbles: true, cancelable: true, submitter: submitter || null });
		if (dispatch(this, ev))
			submitForm(this, submitter);
	}
	reset() {
		if (!dispatch(this, new Event('reset', { bubbles: true, cancelable: true }))) return;
		for (const e of this.elements) {
			if (e.localName === 'input') {
				if (e.type === 'checkbox' || e.type === 'radio') e.checked = e.defaultChecked;
				else e.value = e.defaultValue;
			} else if (e.localName === 'textarea') e.value = e.defaultValue;
		}
	}
	checkValidity() { return [...this.elements].every(e => !e.checkValidity || e.checkValidity()); }
	reportValidity() { return this.checkValidity(); }
}
for (const p of ['name', 'target', 'enctype', 'encoding', 'acceptCharset', 'autocomplete', 'rel'])
	reflectString(HTMLFormElement.prototype, p);
reflectBool(HTMLFormElement.prototype, 'noValidate');
htmlClass('HTMLFormElement', ['form'], HTMLFormElement);

/* a form sent: by NetSurf (its method, its encoding); a form a script made: GET here */
function submitForm(form, submitter) {
	if (form.method === 'dialog') return;
	if (N.submit(form, submitter || null)) return;
	const data = new FormData(form, submitter);
	const u = new URL(form.action || N.url());
	u.search = new URLSearchParams(data).toString();
	N.navigate(u.href);
}

class FormData {
	constructor(form, submitter) {
		this._e = [];
		if (form) {
			for (const e of form.elements) {
				const name = N.attr(e, 'name');
				if (!name || N.attr(e, 'disabled') !== null) continue;
				const tag = e.localName;
				if (tag === 'input') {
					const t = e.type;
					if ((t === 'checkbox' || t === 'radio') && !e.checked) continue;
					if (['submit', 'image', 'button', 'reset', 'file'].includes(t) && e !== submitter) continue;
					this._e.push([name, e.value]);
				} else if (tag === 'select') {
					for (const o of e.options) if (o.selected) this._e.push([name, o.value]);
				} else if (tag === 'textarea') {
					this._e.push([name, e.value]);
				} else if (tag === 'button' && e === submitter) {
					this._e.push([name, e.value]);
				}
			}
		}
	}
	append(k, v) { this._e.push([String(k), String(v)]); }
	set(k, v) { this.delete(k); this.append(k, v); }
	get(k) { const e = this._e.find(x => x[0] === k); return e ? e[1] : null; }
	getAll(k) { return this._e.filter(x => x[0] === k).map(x => x[1]); }
	has(k) { return this._e.some(x => x[0] === k); }
	delete(k) { this._e = this._e.filter(x => x[0] !== k); }
	entries() { return this._e.map(x => x.slice())[Symbol.iterator](); }
	keys() { return this._e.map(x => x[0])[Symbol.iterator](); }
	values() { return this._e.map(x => x[1])[Symbol.iterator](); }
	forEach(fn, t) { for (const [k, v] of this._e) fn.call(t, v, k, this); }
	[Symbol.iterator]() { return this.entries(); }
}
G.FormData = FormData;

htmlClass('HTMLFieldSetElement', ['fieldset'], class extends HTMLElement {
	get elements() { return htmlCollection(this.querySelectorAll('input,select,textarea,button')); }
	get form() { return this.closest('form'); }
});
htmlClass('HTMLOutputElement', ['output'], class extends HTMLElement {
	get value() { return N.text(this); }
	set value(v) { this.textContent = v; }
});
htmlClass('HTMLScriptElement', ['script'], class extends HTMLElement {
	get src() { const v = N.attr(this, 'src'); try { return v ? new URL(v, N.url()).href : ''; } catch (e) { return v || ''; } }
	set src(v) { this.setAttribute('src', v); }
	get text() { return N.text(this); }
	set text(v) { this.textContent = v; }
	get type() { return N.attr(this, 'type') || ''; }
	set type(v) { this.setAttribute('type', v); }
	get async() { return N.attr(this, 'async') !== null; }
	set async(v) { this.toggleAttribute('async', !!v); }
	get defer() { return N.attr(this, 'defer') !== null; }
	set defer(v) { this.toggleAttribute('defer', !!v); }
	static supports(t) { return t === 'classic' || t === 'module'; }
});
htmlClass('HTMLLinkElement', ['link'], class extends HTMLElement {
	get href() { const v = N.attr(this, 'href'); try { return v ? new URL(v, N.url()).href : ''; } catch (e) { return v || ''; } }
	set href(v) { this.setAttribute('href', v); }
	get rel() { return N.attr(this, 'rel') || ''; }
	set rel(v) { this.setAttribute('rel', v); }
	get relList() { return new DOMTokenList(this, 'rel'); }
	get sheet() { return null; }
});
htmlClass('HTMLStyleElement', ['style'], class extends HTMLElement {
	get sheet() { return cssSheetOf(this); }
	get media() { return N.attr(this, 'media') || ''; }
	set media(v) { this.setAttribute('media', v); }
});
for (const [name, tags] of [['HTMLMetaElement', ['meta']],
		['HTMLHeadElement', ['head']], ['HTMLHtmlElement', ['html']],
		['HTMLBodyElement', ['body']], ['HTMLDivElement', ['div']],
		['HTMLSpanElement', ['span']], ['HTMLParagraphElement', ['p']],
		['HTMLHeadingElement', ['h1', 'h2', 'h3', 'h4', 'h5', 'h6']],
		['HTMLUListElement', ['ul']], ['HTMLOListElement', ['ol']], ['HTMLLIElement', ['li']],
		['HTMLDListElement', ['dl']], ['HTMLPreElement', ['pre']], ['HTMLQuoteElement', ['blockquote', 'q']],
		['HTMLBRElement', ['br']], ['HTMLHRElement', ['hr']], ['HTMLTableElement', ['table']],
		['HTMLTableRowElement', ['tr']], ['HTMLTableCellElement', ['td', 'th']],
		['HTMLTableSectionElement', ['thead', 'tbody', 'tfoot']], ['HTMLTableCaptionElement', ['caption']],
		['HTMLTableColElement', ['col', 'colgroup']], ['HTMLTitleElement', ['title']],
		['HTMLBaseElement', ['base']], ['HTMLPictureElement', ['picture']],
		['HTMLSourceElement', ['source']], ['HTMLTrackElement', ['track']],
		['HTMLMapElement', ['map']], ['HTMLObjectElement', ['object']], ['HTMLEmbedElement', ['embed']],
		['HTMLParamElement', ['param']], ['HTMLLegendElement', ['legend']],
		['HTMLDataListElement', ['datalist']], ['HTMLOptGroupElement', ['optgroup']],
		['HTMLProgressElement', ['progress']], ['HTMLMeterElement', ['meter']],
		['HTMLTimeElement', ['time']], ['HTMLDataElement', ['data']],
		['HTMLModElement', ['ins', 'del']], ['HTMLSlotElement', ['slot']],
		['HTMLMenuElement', ['menu']], ['HTMLFontElement', ['font']],
		['HTMLUnknownElement', []]])
	htmlClass(name, tags);
Object.defineProperty(G.HTMLTitleElement.prototype, 'text', { configurable: true,
	get() { return N.text(this); }, set(v) { this.textContent = v; } });
for (const k of ['href', 'target'])
	reflectString(G.HTMLBaseElement.prototype, k);
for (const k of ['name', 'content', 'httpEquiv', 'charset'])
	reflectString(G.HTMLMetaElement.prototype, k, k === 'httpEquiv' ? 'http-equiv' : k.toLowerCase());
for (const k of ['value', 'max'])
	Object.defineProperty(G.HTMLProgressElement.prototype, k, { configurable: true,
		get() { return parseFloat(N.attr(this, k)) || (k === 'max' ? 1 : 0); },
		set(v) { this.setAttribute(k, String(v)); } });

htmlClass('HTMLTemplateElement', ['template'], class extends HTMLElement {
	get content() {
		let f = this._content;
		if (!f) {
			f = N.createFragment();
			for (let c; (c = N.first(this));)
				N.insert(f, c, null);
			Object.defineProperty(this, '_content', { value: f });
		}
		return f;
	}
});
htmlClass('HTMLDetailsElement', ['details'], class extends HTMLElement {});
reflectBool(G.HTMLDetailsElement.prototype, 'open');
htmlClass('HTMLDialogElement', ['dialog'], class extends HTMLElement {
	show() { this.setAttribute('open', ''); }
	showModal() { this.setAttribute('open', ''); }
	close(v) {
		this.removeAttribute('open');
		if (v !== undefined) this.returnValue = v;
		dispatch(this, new Event('close'));
	}
});
reflectBool(G.HTMLDialogElement.prototype, 'open');
htmlClass('HTMLIFrameElement', ['iframe', 'frame'], class extends HTMLElement {
	get contentWindow() { return null; }
	get contentDocument() { return null; }
});
reflectURL(G.HTMLIFrameElement.prototype, 'src');
for (const k of ['name', 'width', 'height', 'allow', 'loading', 'srcdoc'])
	reflectString(G.HTMLIFrameElement.prototype, k);
htmlClass('HTMLCanvasElement', ['canvas'], class extends HTMLElement {
	getContext() { return null; }
	toDataURL() { return 'data:,'; }
	toBlob(cb) { setTimeout(() => cb(null), 0); }
	get width() { return parseInt(N.attr(this, 'width'), 10) || 300; }
	set width(v) { this.setAttribute('width', String(v)); }
	get height() { return parseInt(N.attr(this, 'height'), 10) || 150; }
	set height(v) { this.setAttribute('height', String(v)); }
});
class HTMLMediaElement extends HTMLElement {
	play() { return Promise.reject(new DOMException('not supported', 'NotSupportedError')); }
	pause() {}
	load() {}
	canPlayType() { return ''; }
	get paused() { return true; }
	get currentTime() { return 0; }
	set currentTime(v) {}
	get duration() { return NaN; }
	get readyState() { return 0; }
	get volume() { return 1; }
	set volume(v) {}
}
for (const k of ['muted', 'autoplay', 'loop', 'controls'])
	reflectBool(HTMLMediaElement.prototype, k);
reflectURL(HTMLMediaElement.prototype, 'src');
G.HTMLMediaElement = HTMLMediaElement;
htmlClass('HTMLVideoElement', ['video'], class extends HTMLMediaElement {});
htmlClass('HTMLAudioElement', ['audio'], class extends HTMLMediaElement {});
G.Audio = function Audio(src) { const a = N.create('audio'); if (src) a.src = src; return a; };

/* SVG elements: their own classes (no rendering yet) */
class SVGElement extends Element {
	get dataset() { return HTMLElement.prototype.__lookupGetter__('dataset').call(this); }
	get style() { return HTMLElement.prototype.__lookupGetter__('style').call(this); }
	get className() { return { baseVal: N.attr(this, 'class') || '', animVal: N.attr(this, 'class') || '' }; }
	get ownerSVGElement() { return this.closest('svg'); }
	getBBox() { const r = N.rect(this); return new DOMRect(0, 0, r[2], r[3]); }
	focus() {}
	blur() {}
}
defineHandlers(SVGElement.prototype);
G.SVGElement = SVGElement;
G.SVGSVGElement = class SVGSVGElement extends SVGElement {};
for (const t of ['svg'])
	TAGS[t] = G.SVGSVGElement.prototype;
for (const t of ['path', 'g', 'circle', 'rect', 'line', 'polyline', 'polygon', 'ellipse', 'use', 'defs', 'symbol', 'text', 'tspan', 'lineargradient', 'radialgradient', 'stop', 'clippath', 'mask', 'pattern', 'image', 'foreignobject'])
	TAGS[t] = SVGElement.prototype;

/* ---- Document ------------------------------------------------------------------------ */

class Document extends Node {
	constructor() { super(); }
	get documentElement() {
		for (const c of N.children(this))
			if (N.type(c) === ELEMENT_NODE) return c;
		return null;
	}
	get head() {
		const h = this.documentElement;
		return h ? N.children(h).find(c => c.localName === 'head') || null : null;
	}
	get body() {
		const h = this.documentElement;
		return h ? N.children(h).find(c => c.localName === 'body' || c.localName === 'frameset') || null : null;
	}
	set body(b) {
		const h = this.documentElement, old = this.body;
		if (!h) return;
		if (old) h.replaceChild(b, old);
		else h.appendChild(b);
	}
	get title() {
		const t = this.querySelector('title');
		return t ? N.text(t).replace(/\s+/g, ' ').trim() : '';
	}
	set title(v) {
		let t = this.querySelector('title');
		if (!t && this.head) { t = N.create('title'); this.head.appendChild(t); }
		if (t) t.textContent = v;
	}
	get doctype() {
		for (const c of N.children(this))
			if (N.type(c) === DOCUMENT_TYPE_NODE) return c;
		return null;
	}
	get URL() { return N.url(); }
	get documentURI() { return N.url(); }
	get baseURI() {
		const b = this.querySelector('base[href]');
		if (b) try { return new URL(N.attr(b, 'href'), N.url()).href; } catch (e) {}
		return N.url();
	}
	get location() { return G.location; }
	set location(v) { G.location.href = v; }
	get domain() { try { return new URL(N.url()).hostname; } catch (e) { return ''; } }
	get origin() { try { return new URL(N.url()).origin; } catch (e) { return 'null'; } }
	get referrer() { return ''; }
	get cookie() { return N.cookie(); }
	set cookie(v) { N.setCookie(String(v)); }
	get readyState() { return readyState; }
	get defaultView() { return G; }
	get activeElement() { return activeElement || this.body; }
	get characterSet() { return 'UTF-8'; }
	get charset() { return 'UTF-8'; }
	get inputEncoding() { return 'UTF-8'; }
	get contentType() { return 'text/html'; }
	get compatMode() { return 'CSS1Compat'; }
	get hidden() { return false; }
	get visibilityState() { return 'visible'; }
	get scrollingElement() { return this.documentElement; }
	get forms() { return this.getElementsByTagName('form'); }
	get images() { return this.getElementsByTagName('img'); }
	get links() { return htmlCollection(this.querySelectorAll('a[href],area[href]')); }
	get scripts() { return this.getElementsByTagName('script'); }
	get styleSheets() { return [...this.querySelectorAll('style')].map(cssSheetOf); }
	get fonts() { return fontFaces; }
	get currentScript() { return N.currentScript(); }
	get fullscreenElement() { return null; }
	get pictureInPictureElement() { return null; }
	get implementation() {
		return { hasFeature: () => true, createHTMLDocument: () => this,
			createDocumentType: () => null };
	}
	hasFocus() { return true; }
	getElementById(id) { return N.byId(String(id)); }
	getElementsByName(name) { return nodeList(N.descendants(this).filter(e => N.attr(e, 'name') === name)); }
	createElement(tag) { return N.create(lower(tag)); }
	createElementNS(ns, tag) { return N.create(tag.replace(/^.*:/, '').toLowerCase()); }
	createTextNode(s) { return N.createText(String(s)); }
	createComment(s) { return N.createComment(String(s)); }
	createDocumentFragment() { return N.createFragment(); }
	createAttribute(name) { return new Attr(null, lower(name), ''); }
	createEvent(type) {
		const t = lower(type);
		if (t.startsWith('mouse')) return new MouseEvent('');
		if (t.startsWith('keyboard')) return new KeyboardEvent('');
		if (t.startsWith('custom')) return new CustomEvent('');
		if (t.startsWith('ui')) return new UIEvent('');
		return new Event('');
	}
	createRange() { return new Range(); }
	createTreeWalker(root, what = 0xffffffff, filter = null) { return new TreeWalker(root, what, filter); }
	createNodeIterator(root, what = 0xffffffff, filter = null) { return new TreeWalker(root, what, filter); }
	importNode(n, deep) { return N.clone(n, !!deep); }
	adoptNode(n) { const p = N.parent(n); if (p) p.removeChild(n); return n; }
	elementFromPoint(x, y) {
		let best = null;
		for (const e of N.descendants(this)) {
			const r = N.rect(e);
			if (r[2] > 0 && r[3] > 0 && x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3])
				best = e;
		}
		return best;
	}
	elementsFromPoint(x, y) { const e = this.elementFromPoint(x, y); return e ? [e] : []; }
	getSelection() { return G.getSelection(); }
	write(...s) { N.write(s.join('')); }
	writeln(...s) { N.write(s.join('') + '\n'); }
	open() { return this; }
	close() {}
	execCommand() { return false; }
	queryCommandSupported() { return false; }
	exitFullscreen() { return Promise.resolve(); }
}
mixin(Document, ParentNode);
defineHandlers(Document.prototype);
G.Document = Document;
G.HTMLDocument = Document;

let readyState = 'loading';

class TreeWalker {
	constructor(root, what, filter) {
		this.root = root;
		this.whatToShow = what;
		this.filter = filter;
		this.currentNode = root;
		this._list = null;
	}
	_nodes() {
		if (!this._list) {
			this._list = allNodes(this.root).filter(n => {
				if (!(this.whatToShow & (1 << (N.type(n) - 1)))) return false;
				if (!this.filter) return true;
				const f = typeof this.filter === 'function' ? this.filter : this.filter.acceptNode.bind(this.filter);
				return f(n) === 1;
			});
		}
		return this._list;
	}
	nextNode() {
		const l = this._nodes(), i = l.indexOf(this.currentNode);
		const n = l[i + 1];
		if (n) this.currentNode = n;
		return n || null;
	}
	previousNode() {
		const l = this._nodes(), i = l.indexOf(this.currentNode);
		const n = i > 0 ? l[i - 1] : null;
		if (n) this.currentNode = n;
		return n;
	}
	firstChild() { const c = N.first(this.currentNode); if (c) this.currentNode = c; return c; }
	nextSibling() { const c = N.next(this.currentNode); if (c) this.currentNode = c; return c; }
	parentNode() { const c = N.parent(this.currentNode); if (c) this.currentNode = c; return c; }
}
G.TreeWalker = TreeWalker;
G.NodeIterator = TreeWalker;
G.NodeFilter = { FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3, SHOW_ALL: 0xffffffff,
	SHOW_ELEMENT: 1, SHOW_TEXT: 4, SHOW_COMMENT: 128 };

class Range {
	constructor() { this.startContainer = this.endContainer = G.document; this.startOffset = this.endOffset = 0; this.collapsed = true; }
	setStart(n, o) { this.startContainer = n; this.startOffset = o; }
	setEnd(n, o) { this.endContainer = n; this.endOffset = o; }
	selectNode(n) { this.startContainer = this.endContainer = n; }
	selectNodeContents(n) { this.startContainer = this.endContainer = n; }
	collapse() {}
	cloneRange() { return new Range(); }
	deleteContents() {}
	createContextualFragment(html) { return fragmentFromHTML(html); }
	getBoundingClientRect() { return isElement(this.startContainer) ? this.startContainer.getBoundingClientRect() : new DOMRect(); }
	getClientRects() { return []; }
	toString() { return ''; }
	detach() {}
}
G.Range = Range;

/* ---- the window -------------------------------------------------------------------------- */

/* location */
class Location {
	get href() { return N.url(); }
	set href(v) { this.assign(v); }
	_u() { try { return new URL(N.url()); } catch (e) { return new URL('about:blank'); } }
	get protocol() { return this._u().protocol; }
	get host() { return this._u().host; }
	get hostname() { return this._u().hostname; }
	get port() { return this._u().port; }
	get pathname() { return this._u().pathname; }
	get search() { return this._u().search; }
	get hash() { return hashNow; }
	set hash(v) {
		let h = String(v);
		if (h && h[0] !== '#') h = '#' + h;
		const old = N.url().replace(/#.*$/, '') + hashNow;
		hashNow = h === '#' ? '' : h;
		const id = decodeURIComponent(hashNow.slice(1));
		const target = id ? (G.document.getElementById(id) || G.document.querySelector('a[name="' + id.replace(/"/g, '\\"') + '"]')) : null;
		if (target) target.scrollIntoView();
		else if (!id) N.scrollTo(0, 0);
		dispatch(G, new HashChangeEvent('hashchange', { oldURL: old, newURL: N.url().replace(/#.*$/, '') + hashNow }));
	}
	get origin() { return this._u().origin; }
	get ancestorOrigins() { return []; }
	assign(v) {
		const u = new URL(String(v), N.url());
		const here = new URL(N.url());
		if (u.hash && u.href.replace(/#.*$/, '') === here.href.replace(/#.*$/, '')) { this.hash = u.hash; return; }
		N.navigate(u.href);
	}
	replace(v) { this.assign(v); }
	reload() { N.reload(); }
	toString() { return this.href; }
}
let hashNow = (() => { try { return new URL(N.url()).hash; } catch (e) { return ''; } })();
const location = new Location();

const historyState = { state: null };
const history = {
	get length() { return 1; },
	get state() { return historyState.state; },
	scrollRestoration: 'auto',
	pushState(state, title, url) { historyState.state = state; },
	replaceState(state, title, url) { historyState.state = state; },
	back() { N.history(-1); },
	forward() { N.history(1); },
	go(n) { if (n) N.history(n); else N.reload(); },
};

const navigator = {
	get userAgent() { return N.userAgent(); },
	appName: 'Netscape',
	appCodeName: 'Mozilla',
	get appVersion() { return N.userAgent().replace(/^Mozilla\//, ''); },
	product: 'Gecko',
	productSub: '20030107',
	vendor: 'Google Inc.',
	vendorSub: '',
	platform: 'Linux armv8l',
	language: 'fr-FR',
	languages: ['fr-FR', 'fr', 'en-US', 'en'],
	cookieEnabled: true,
	onLine: true,
	doNotTrack: null,
	hardwareConcurrency: 4,
	maxTouchPoints: 0,
	deviceMemory: 4,
	pdfViewerEnabled: false,
	webdriver: false,
	plugins: [],
	mimeTypes: [],
	javaEnabled() { return false; },
	sendBeacon() { return true; },
	vibrate() { return false; },
	userAgentData: undefined,
	clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve('') },
	permissions: { query: () => Promise.resolve({ state: 'denied', onchange: null }) },
	mediaDevices: undefined,
	serviceWorker: undefined,
	geolocation: {
		getCurrentPosition(ok, err) { if (err) setTimeout(() => err({ code: 1, message: 'denied' }), 0); },
		watchPosition(ok, err) { this.getCurrentPosition(ok, err); return 0; },
		clearWatch() {},
	},
};

const screen = {
	get width() { return N.scroll()[2]; },
	get height() { return N.scroll()[3]; },
	get availWidth() { return N.scroll()[2]; },
	get availHeight() { return N.scroll()[3]; },
	colorDepth: 24,
	pixelDepth: 24,
	orientation: { type: 'landscape-primary', angle: 0, addEventListener() {}, removeEventListener() {} },
};

/* timers */
const timers = new Map();
function setTimeout(fn, ms, ...args) {
	const f = typeof fn === 'function' ? () => fn(...args) : () => (0, eval)(String(fn));
	return N.timer(f, +ms || 0, false);
}
function setInterval(fn, ms, ...args) {
	const f = typeof fn === 'function' ? () => fn(...args) : () => (0, eval)(String(fn));
	return N.timer(f, +ms || 0, true);
}
function clearTimeout(id) { if (id) N.clearTimer(id); }
const performance = {
	now() { return N.now() - startTime; },
	get timeOrigin() { return Date.now() - this.now(); },
	timing: {},
	navigation: { type: 0, redirectCount: 0 },
	mark() {}, measure() {}, clearMarks() {}, clearMeasures() {},
	getEntries() { return []; }, getEntriesByType() { return []; }, getEntriesByName() { return []; },
};
const startTime = N.now();
let rafId = 0;
const rafs = new Map();
function requestAnimationFrame(cb) {
	const id = ++rafId;
	rafs.set(id, N.timer(() => { rafs.delete(id); cb(performance.now()); }, 16, false));
	return id;
}
function cancelAnimationFrame(id) { const t = rafs.get(id); if (t) { N.clearTimer(t); rafs.delete(id); } }

/* media queries: the viewport's width and height, a screen, no motion preference */
function mediaMatches(q) {
	const w = G.innerWidth, h = G.innerHeight;
	return String(q).split(',').some(part => {
		let s = part.trim().toLowerCase();
		let not = false;
		if (s.startsWith('not ')) { not = true; s = s.slice(4); }
		s = s.replace(/^only\s+/, '');
		let ok = true;
		for (const cond of s.split(/\s+and\s+/)) {
			const c = cond.trim();
			if (c === 'screen' || c === 'all' || c === '') continue;
			if (c === 'print' || c === 'speech') { ok = false; continue; }
			const m = /^\(\s*([a-z-]+)\s*(?::\s*([^)]+))?\)$/.exec(c);
			if (!m) { ok = false; continue; }
			const name = m[1], val = (m[2] || '').trim();
			const px = v => {
				const n = parseFloat(v);
				if (/em$/.test(v)) return n * 16;
				return n;
			};
			switch (name) {
			case 'min-width': ok = ok && w >= px(val); break;
			case 'max-width': ok = ok && w <= px(val); break;
			case 'width': ok = ok && w === px(val); break;
			case 'min-height': ok = ok && h >= px(val); break;
			case 'max-height': ok = ok && h <= px(val); break;
			case 'orientation': ok = ok && (val === 'landscape' ? w >= h : w < h); break;
			case 'prefers-color-scheme': ok = ok && val === 'light'; break;
			case 'prefers-reduced-motion': ok = ok && val === 'no-preference'; break;
			case 'prefers-contrast': ok = ok && val === 'no-preference'; break;
			case 'hover': ok = ok && (val === '' || val === 'hover'); break;
			case 'any-hover': ok = ok && (val === '' || val === 'hover'); break;
			case 'pointer': case 'any-pointer': ok = ok && (val === '' || val === 'fine'); break;
			case 'min-resolution': case 'max-resolution': break;
			case '-webkit-min-device-pixel-ratio': ok = ok && 1 >= parseFloat(val); break;
			case '-webkit-max-device-pixel-ratio': ok = ok && 1 <= parseFloat(val); break;
			case 'display-mode': ok = ok && val === 'browser'; break;
			case 'color': break;
			default: ok = false;
			}
		}
		return not ? !ok : ok;
	});
}
const mediaLists = new Set();
class MediaQueryList extends EventTarget {
	constructor(q) { super(); this.media = String(q); this._last = mediaMatches(q); this.onchange = null; }
	get matches() { return mediaMatches(this.media); }
	addListener(fn) { this.addEventListener('change', fn); mediaLists.add(this); }
	removeListener(fn) { this.removeEventListener('change', fn); }
}
function matchMedia(q) { const m = new MediaQueryList(q); mediaLists.add(m); return m; }
G.MediaQueryList = MediaQueryList;

function checkMedia() {
	for (const m of mediaLists) {
		const now = mediaMatches(m.media);
		if (now !== m._last) {
			m._last = now;
			const ev = new Event('change');
			ev.matches = now;
			ev.media = m.media;
			dispatch(m, ev);
			if (typeof m.onchange === 'function') try { m.onchange(ev); } catch (e) { report(e); }
		}
	}
}

/* getComputedStyle: the box's own values for a few properties, else the style attribute */
function getComputedStyle(el, pseudo) {
	const decl = new CSSStyleDeclaration(null);
	const get = p => {
		const k = p.startsWith('--') ? p : kebab(p);
		const inline = el.getAttribute ? parseDecls(N.attr(el, 'style')).get(k) : null;
		if (pseudo) return '';
		const native = N.cstyle(el, k);
		if (native) return native;
		if (inline) return inline.v;
		return COMPUTED_DEFAULTS[k] || '';
	};
	return new Proxy(decl, {
		get(t, p) {
			if (p === 'getPropertyValue') return k => get(k);
			if (p === 'getPropertyPriority') return () => '';
			if (p === 'length') return 0;
			if (p === 'item') return () => '';
			if (p === 'cssText') return '';
			if (typeof p !== 'string') return undefined;
			return get(p);
		},
	});
}
const COMPUTED_DEFAULTS = { 'transition-duration': '0s', 'animation-duration': '0s',
	'transition-delay': '0s', 'animation-name': 'none', 'transform': 'none',
	'overflow': 'visible', 'overflow-x': 'visible', 'overflow-y': 'visible', 'z-index': 'auto',
	'box-sizing': 'content-box', 'float': 'none', 'direction': 'ltr' };

/* storage: per document, in memory */
/* localStorage is kept (N.storage: a file per origin, written a turn after a change);
 * sessionStorage lives as long as the page */
class Storage {
	constructor(origin) {
		Object.defineProperty(this, '_m', { value: new Map() });
		Object.defineProperty(this, '_o', { value: origin || null });
		Object.defineProperty(this, '_q', { value: false, writable: true });
		if (this._o) {
			const j = N.storage(this._o);
			if (j) try {
				const o = JSON.parse(j);
				for (const k of Object.keys(o)) this._m.set(k, String(o[k]));
			} catch (e) { /* (a damaged file: start empty) */ }
		}
	}
	_save() {
		if (!this._o || this._q) return;
		this._q = true;
		N.timer(() => {
			this._q = false;
			const o = {};
			for (const [k, v] of this._m) o[k] = v;
			N.storage(this._o, JSON.stringify(o));
		}, 0, false);
	}
	get length() { return this._m.size; }
	key(i) { return [...this._m.keys()][i] ?? null; }
	getItem(k) { const v = this._m.get(String(k)); return v === undefined ? null : v; }
	setItem(k, v) { this._m.set(String(k), String(v)); this._save(); }
	removeItem(k) { if (this._m.delete(String(k))) this._save(); }
	clear() { if (this._m.size) { this._m.clear(); this._save(); } }
}
function storageOrigin() {
	try {
		const u = new URL(N.url());
		return u.protocol === 'file:' ? 'file' : u.origin;
	} catch (e) { return null; }
}
function storageProxy(origin) {
	return new Proxy(new Storage(origin), {
		get(t, p) {
			if (p in t || typeof p !== 'string') { const v = t[p]; return typeof v === 'function' ? v.bind(t) : v; }
			return t.getItem(p) ?? undefined;
		},
		set(t, p, v) { t.setItem(p, v); return true; },
		deleteProperty(t, p) { t.removeItem(p); return true; },
		ownKeys(t) { return [...t._m.keys()]; },
		getOwnPropertyDescriptor(t, p) { const v = t.getItem(p); return v === null ? undefined : { value: v, enumerable: true, configurable: true, writable: true }; },
	});
}
G.Storage = Storage;

/* console */
function fmt(args) {
	return args.map(a => {
		if (typeof a === 'string') return a;
		if (a instanceof Error) return a + (a.stack ? '\n' + a.stack : '');
		if (a instanceof Node) return '<' + (a.nodeName || 'node') + '>';
		try { return JSON.stringify(a); } catch (e) { return String(a); }
	}).join(' ');
}
const counts = new Map(), times = new Map();
const console = {
	log: (...a) => N.log(fmt(a)),
	info: (...a) => N.log(fmt(a)),
	debug: (...a) => N.log(fmt(a)),
	warn: (...a) => N.log('warning: ' + fmt(a)),
	error: (...a) => N.log('error: ' + fmt(a)),
	trace: (...a) => N.log('trace: ' + fmt(a)),
	dir: (...a) => N.log(fmt(a)),
	dirxml: (...a) => N.log(fmt(a)),
	table: (...a) => N.log(fmt(a)),
	group: (...a) => N.log(fmt(a)),
	groupCollapsed: (...a) => N.log(fmt(a)),
	groupEnd() {},
	assert: (c, ...a) => { if (!c) N.log('assertion failed: ' + fmt(a)); },
	count: (l = 'default') => { counts.set(l, (counts.get(l) || 0) + 1); N.log(l + ': ' + counts.get(l)); },
	countReset: (l = 'default') => counts.delete(l),
	time: (l = 'default') => times.set(l, N.now()),
	timeEnd: (l = 'default') => { N.log(l + ': ' + (N.now() - (times.get(l) || N.now())) + 'ms'); times.delete(l); },
	timeLog: (l = 'default') => N.log(l + ': ' + (N.now() - (times.get(l) || N.now())) + 'ms'),
	clear() {},
};

/* base64 */
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function btoa(s) {
	s = String(s);
	let out = '';
	for (let i = 0; i < s.length; i += 3) {
		const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2);
		if (a > 255 || b > 255 || c > 255) throw new DOMException('not Latin-1', 'InvalidCharacterError');
		out += B64[a >> 2] + B64[((a & 3) << 4) | (b >> 4 || 0)] +
			(isNaN(b) ? '=' : B64[((b & 15) << 2) | (c >> 6 || 0)]) +
			(isNaN(c) ? '=' : B64[c & 63]);
	}
	return out;
}
function atob(s) {
	s = String(s).replace(/[\s=]/g, '');
	let out = '', bits = 0, n = 0;
	for (const ch of s) {
		const v = B64.indexOf(ch);
		if (v < 0) throw new DOMException('bad base64', 'InvalidCharacterError');
		bits = (bits << 6) | v;
		n += 6;
		if (n >= 8) { n -= 8; out += String.fromCharCode((bits >> n) & 255); }
	}
	return out;
}

/* URL, URLSearchParams */
class URLSearchParams {
	constructor(init = '') {
		this._e = [];
		if (init instanceof URLSearchParams) this._e = init._e.map(x => x.slice());
		else if (init instanceof FormData) for (const [k, v] of init) this._e.push([k, v]);
		else if (typeof init === 'object' && init !== null) {
			if (Symbol.iterator in init) for (const [k, v] of init) this._e.push([String(k), String(v)]);
			else for (const k of Object.keys(init)) this._e.push([k, String(init[k])]);
		} else {
			let s = String(init);
			if (s[0] === '?') s = s.slice(1);
			for (const part of s.split('&')) {
				if (!part) continue;
				const i = part.indexOf('=');
				const dec = x => { try { return decodeURIComponent(x.replace(/\+/g, ' ')); } catch (e) { return x; } };
				this._e.push(i < 0 ? [dec(part), ''] : [dec(part.slice(0, i)), dec(part.slice(i + 1))]);
			}
		}
	}
	append(k, v) { this._e.push([String(k), String(v)]); this._upd(); }
	delete(k) { this._e = this._e.filter(x => x[0] !== k); this._upd(); }
	get(k) { const e = this._e.find(x => x[0] === k); return e ? e[1] : null; }
	getAll(k) { return this._e.filter(x => x[0] === k).map(x => x[1]); }
	has(k) { return this._e.some(x => x[0] === k); }
	set(k, v) {
		const i = this._e.findIndex(x => x[0] === k);
		if (i < 0) this._e.push([String(k), String(v)]);
		else { this._e[i][1] = String(v); this._e = this._e.filter((x, j) => j <= i || x[0] !== k); }
		this._upd();
	}
	sort() { this._e.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0); this._upd(); }
	get size() { return this._e.length; }
	forEach(fn, t) { for (const [k, v] of this._e) fn.call(t, v, k, this); }
	entries() { return this._e.map(x => x.slice())[Symbol.iterator](); }
	keys() { return this._e.map(x => x[0])[Symbol.iterator](); }
	values() { return this._e.map(x => x[1])[Symbol.iterator](); }
	[Symbol.iterator]() { return this.entries(); }
	toString() {
		const enc = x => encodeURIComponent(x).replace(/%20/g, '+').replace(/[!'()~]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase());
		return this._e.map(([k, v]) => enc(k) + '=' + enc(v)).join('&');
	}
	_upd() { if (this._url) { const s = this.toString(); this._url._search = s ? '?' + s : ''; } }
}

const DEFAULT_PORTS = { 'http:': '80', 'https:': '443', 'ftp:': '21', 'ws:': '80', 'wss:': '443' };
class URL {
	constructor(url, base) {
		url = String(url).trim();
		let m = /^([a-zA-Z][a-zA-Z0-9+.-]*):/.exec(url);
		if (!m) {
			if (base === undefined) throw new TypeError("Invalid URL: '" + url + "'");
			const b = base instanceof URL ? base : new URL(String(base));
			this._resolve(url, b);
			return;
		}
		this._parse(url);
	}
	_parse(url) {
		const m = /^([a-zA-Z][a-zA-Z0-9+.-]*:)(?:\/\/(?:([^:@\/?#]*)(?::([^@\/?#]*))?@)?(\[[^\]]*\]|[^:\/?#]*)(?::(\d*))?)?([^?#]*)(\?[^#]*)?(#.*)?$/.exec(url);
		if (!m) throw new TypeError("Invalid URL: '" + url + "'");
		this._protocol = m[1].toLowerCase();
		this._username = m[2] || '';
		this._password = m[3] || '';
		this._hostname = (m[4] || '').toLowerCase();
		this._port = m[5] && m[5] !== DEFAULT_PORTS[this._protocol] ? m[5] : '';
		this._special = this._protocol in DEFAULT_PORTS || this._protocol === 'file:';
		let path = m[6] || '';
		if (this._special) path = normPath(path || '/');
		this._pathname = path;
		this._search = m[7] && m[7] !== '?' ? m[7] : '';
		this._hash = m[8] && m[8] !== '#' ? m[8] : '';
		this._opaque = !this._special && !url.slice(this._protocol.length).startsWith('//');
	}
	_resolve(rel, b) {
		Object.assign(this, { _protocol: b._protocol, _username: b._username, _password: b._password,
			_hostname: b._hostname, _port: b._port, _special: b._special, _opaque: false });
		const m = /^([^?#]*)(\?[^#]*)?(#.*)?$/.exec(rel);
		let path = m[1];
		if (rel.startsWith('//')) { this._parse(b._protocol + rel); return; }
		if (path === '') {
			this._pathname = b._pathname;
			this._search = m[2] !== undefined ? (m[2] === '?' ? '' : m[2]) : b._search;
		} else {
			if (path[0] !== '/') path = b._pathname.replace(/[^/]*$/, '') + path;
			this._pathname = normPath(path);
			this._search = m[2] && m[2] !== '?' ? m[2] : '';
		}
		this._hash = m[3] && m[3] !== '#' ? m[3] : '';
	}
	get protocol() { return this._protocol; }
	set protocol(v) { this._protocol = String(v).replace(/:?$/, ':').toLowerCase(); }
	get username() { return this._username; }
	get password() { return this._password; }
	get hostname() { return this._hostname; }
	set hostname(v) { this._hostname = String(v).toLowerCase(); }
	get port() { return this._port; }
	set port(v) { this._port = String(v) === DEFAULT_PORTS[this._protocol] ? '' : String(v); }
	get host() { return this._hostname + (this._port ? ':' + this._port : ''); }
	set host(v) { const [h, p] = String(v).split(':'); this._hostname = h.toLowerCase(); this._port = p || ''; }
	get origin() { return this._special && this._protocol !== 'file:' ? this._protocol + '//' + this.host : 'null'; }
	get pathname() { return this._pathname; }
	set pathname(v) { this._pathname = normPath(String(v)[0] === '/' ? String(v) : '/' + v); }
	get search() { return this._search; }
	set search(v) { const s = String(v); this._search = s && s !== '?' ? (s[0] === '?' ? s : '?' + s) : ''; if (this._params) this._params._e = new URLSearchParams(this._search)._e; }
	get searchParams() {
		if (!this._params) { this._params = new URLSearchParams(this._search); this._params._url = this; }
		return this._params;
	}
	get hash() { return this._hash; }
	set hash(v) { const s = String(v); this._hash = s && s !== '#' ? (s[0] === '#' ? s : '#' + s) : ''; }
	get href() {
		if (this._opaque) return this._protocol + this._pathname + this._search + this._hash;
		const auth = this._username ? this._username + (this._password ? ':' + this._password : '') + '@' : '';
		return this._protocol + '//' + auth + this.host + this._pathname + this._search + this._hash;
	}
	set href(v) { this._parse(String(v)); }
	toString() { return this.href; }
	toJSON() { return this.href; }
	static canParse(u, b) { try { new URL(u, b); return true; } catch (e) { return false; } }
	static createObjectURL() { return 'blob:onyx'; }
	static revokeObjectURL() {}
}
function normPath(p) {
	const out = [];
	const parts = p.split('/');
	for (let i = 0; i < parts.length; i++) {
		const s = parts[i];
		if (s === '..') { if (out.length > 1) out.pop(); if (i === parts.length - 1) out.push(''); }
		else if (s === '.') { if (i === parts.length - 1) out.push(''); }
		else out.push(s);
	}
	let r = out.join('/');
	if (r[0] !== '/') r = '/' + r;
	return r;
}

/* TextEncoder / TextDecoder (UTF-8) */
class TextEncoder {
	get encoding() { return 'utf-8'; }
	encode(s = '') {
		const u = unescape(encodeURIComponent(String(s)));
		const a = new Uint8Array(u.length);
		for (let i = 0; i < u.length; i++) a[i] = u.charCodeAt(i);
		return a;
	}
}
class TextDecoder {
	constructor(enc = 'utf-8') { this.encoding = lower(enc); }
	decode(buf) {
		if (!buf) return '';
		if (this.encoding === 'utf-8' || this.encoding === 'utf8') return N.utf8(buf);
		const a = buf instanceof Uint8Array ? buf : new Uint8Array(buf.buffer || buf);
		let s = '';
		for (let i = 0; i < a.length; i++) s += String.fromCharCode(a[i]);
		if (this.encoding === 'utf-8' || this.encoding === 'utf8')
			try { return decodeURIComponent(escape(s)); } catch (e) { return s; }
		return s;
	}
}

/* observers of the layout: intersections, sizes */
const intersectionObservers = new Set();
let observersQueued = false;

function scheduleObservers() {
	if (observersQueued) return;
	observersQueued = true;
	N.timer(() => { observersQueued = false; checkObservers(); }, 0, false);
}

function marginPx(s, size) {
	const v = parseFloat(s) || 0;
	return /%$/.test(s) ? v * size / 100 : v;
}

class IntersectionObserver {
	constructor(cb, opts = {}) {
		this._cb = cb;
		this.root = opts.root || null;
		this.rootMargin = opts.rootMargin || '0px';
		const t = opts.threshold === undefined ? [0] : [].concat(opts.threshold);
		this.thresholds = t.map(Number).sort((a, b) => a - b);
		this._targets = new Map();
	}
	observe(el) {
		if (!this._targets.has(el)) this._targets.set(el, undefined);
		intersectionObservers.add(this);
		scheduleObservers();
	}
	unobserve(el) { this._targets.delete(el); }
	disconnect() { this._targets.clear(); intersectionObservers.delete(this); }
	takeRecords() { return []; }
	_check() {
		const s = N.scroll();
		const m = this.rootMargin.split(/\s+/);
		const [mt, mr, mb, ml] = [m[0], m[1] || m[0], m[2] || m[0], m[3] || m[1] || m[0]];
		let root = { x: 0, y: 0, w: s[2], h: s[3] };
		if (this.root && this.root.getBoundingClientRect) {
			const r = N.rect(this.root);
			root = { x: r[0], y: r[1], w: r[2], h: r[3] };
		}
		const top = root.y - marginPx(mt, root.h), bottom = root.y + root.h + marginPx(mb, root.h);
		const left = root.x - marginPx(ml, root.w), right = root.x + root.w + marginPx(mr, root.w);
		const entries = [];
		for (const [el, last] of this._targets) {
			const r = N.rect(el);
			const boxed = N.boxed(el);
			const ix = Math.max(0, Math.min(right, r[0] + r[2]) - Math.max(left, r[0]));
			const iy = Math.max(0, Math.min(bottom, r[1] + r[3]) - Math.max(top, r[1]));
			const area = r[2] * r[3];
			const hit = boxed && r[0] + r[2] >= left && r[0] <= right && r[1] + r[3] >= top && r[1] <= bottom;
			const ratio = !hit ? 0 : area > 0 ? (ix * iy) / area : 1;
			const level = this.thresholds.filter(t => (t === 0 ? hit : ratio >= t)).length;
			if (last === undefined || level !== last) {
				this._targets.set(el, level);
				if (last === undefined && level === 0 && entries.length > 50) continue;
				entries.push({
					target: el, isIntersecting: hit && level > 0, intersectionRatio: ratio,
					boundingClientRect: new DOMRect(r[0], r[1], r[2], r[3]),
					intersectionRect: new DOMRect(Math.max(left, r[0]), Math.max(top, r[1]), ix, iy),
					rootBounds: new DOMRect(left, top, right - left, bottom - top),
					time: performance.now(),
				});
			}
		}
		if (entries.length)
			try { this._cb(entries, this); } catch (e) { report(e); }
	}
}
G.IntersectionObserver = IntersectionObserver;
G.IntersectionObserverEntry = function IntersectionObserverEntry() {};

const resizeObservers = new Set();
class ResizeObserver {
	constructor(cb) { this._cb = cb; this._targets = new Map(); }
	observe(el) { this._targets.set(el, null); resizeObservers.add(this); scheduleObservers(); }
	unobserve(el) { this._targets.delete(el); }
	disconnect() { this._targets.clear(); resizeObservers.delete(this); }
	_check() {
		const entries = [];
		for (const [el, last] of this._targets) {
			const r = N.rect(el);
			const key = r[2] + 'x' + r[3];
			if (key === last) continue;
			this._targets.set(el, key);
			const size = [{ inlineSize: r[2], blockSize: r[3] }];
			entries.push({ target: el, contentRect: new DOMRect(0, 0, r[2], r[3]),
				borderBoxSize: size, contentBoxSize: size, devicePixelContentBoxSize: size });
		}
		if (entries.length)
			try { this._cb(entries, this); } catch (e) { report(e); }
	}
}
G.ResizeObserver = ResizeObserver;

function checkObservers() {
	for (const o of [...intersectionObservers]) o._check();
	for (const o of [...resizeObservers]) o._check();
}

/* ---- the network: fetch, Headers, Request, Response, XMLHttpRequest ---------------------
 * On N.request(method, url, body, headerLines, binary, cb (error, { status, statusText, url,
 * headers: [[name, value]...], body })): NetSurf's low-level cache and the Onyx fetcher (in a
 * thread of its own), the callback on the page's thread. The body goes as text (no NUL). */
const hkey = n => String(n).trim().toLowerCase();
class Headers {
	constructor(init) {
		this._m = new Map();
		if (init == null) return;
		if (init instanceof Headers || Array.isArray(init) || typeof init[Symbol.iterator] === 'function')
			for (const [k, v] of init) this.append(k, v);
		else if (typeof init === 'object')
			for (const k of Object.keys(init)) this.append(k, init[k]);
	}
	append(n, v) {
		const k = hkey(n), o = this._m.get(k);
		v = String(v).trim();
		this._m.set(k, o === undefined ? v : o + (k === 'set-cookie' ? '\n' : ', ') + v);
	}
	set(n, v) { this._m.set(hkey(n), String(v).trim()); }
	get(n) { const v = this._m.get(hkey(n)); return v === undefined ? null : v; }
	has(n) { return this._m.has(hkey(n)); }
	delete(n) { this._m.delete(hkey(n)); }
	getSetCookie() { const v = this._m.get('set-cookie'); return v ? v.split('\n') : []; }
	forEach(cb, self) { for (const [k, v] of this) cb.call(self, v, k, this); }
	*entries() { yield* [...this._m.entries()].sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)); }
	*keys() { for (const [k] of this.entries()) yield k; }
	*values() { for (const [, v] of this.entries()) yield v; }
	[Symbol.iterator]() { return this.entries(); }
}
const headerLines = h => [...h].map(([k, v]) => k + ': ' + v);

/* a request's body as the text sent, its Content-Type set when the caller gave none */
function encodeBody(b, h) {
	if (b == null) return null;
	const type = t => { if (!h.has('content-type')) h.set('content-type', t); };
	if (typeof b === 'string') { type('text/plain;charset=UTF-8'); return b; }
	if (b instanceof URLSearchParams) { type('application/x-www-form-urlencoded;charset=UTF-8'); return b.toString(); }
	if (b instanceof FormData) {
		type('application/x-www-form-urlencoded;charset=UTF-8');	/* (multipart: not yet) */
		const u = new URLSearchParams();
		for (const [k, v] of b._e) u.append(k, v instanceof Blob ? v._s : v);
		return u.toString();
	}
	if (b instanceof Blob) { if (b.type) type(b.type); return b._s; }
	if (b instanceof ArrayBuffer || ArrayBuffer.isView(b)) return N.utf8(b);
	type('text/plain;charset=UTF-8');
	return String(b);
}

const absURL = u => { try { return new URL(String(u), G.location.href).href; } catch (e) { return String(u); } };

class Request {
	constructor(input, init = {}) {
		const src = input instanceof Request ? input : null;
		this.url = src ? src.url : absURL(input);
		this.method = String(init.method || (src ? src.method : 'GET')).toUpperCase();
		this.headers = new Headers(init.headers || (src ? src.headers : undefined));
		this.signal = init.signal || (src ? src.signal : null) || new AbortSignal();
		this.credentials = init.credentials || (src ? src.credentials : 'same-origin');
		this.mode = init.mode || (src ? src.mode : 'cors');
		this.cache = init.cache || (src ? src.cache : 'default');
		this.redirect = init.redirect || (src ? src.redirect : 'follow');
		this.referrer = 'about:client';
		const body = init.body !== undefined ? init.body : (src ? src._body : null);
		this._body = this.method === 'GET' || this.method === 'HEAD' ? null : encodeBody(body, this.headers);
		this.bodyUsed = false;
	}
	clone() { return new Request(this); }
	text() { this.bodyUsed = true; return Promise.resolve(this._body || ''); }
	json() { return this.text().then(JSON.parse); }
}

const STATUS_TEXT = { 200: 'OK', 201: 'Created', 204: 'No Content', 301: 'Moved Permanently',
	302: 'Found', 304: 'Not Modified', 400: 'Bad Request', 401: 'Unauthorized',
	403: 'Forbidden', 404: 'Not Found', 500: 'Internal Server Error', 503: 'Service Unavailable' };

class Response {
	constructor(body = null, init = {}) {
		this._body = body == null ? '' : body;	/* a string or an ArrayBuffer */
		this.status = init.status === undefined ? 200 : init.status;
		this.statusText = init.statusText !== undefined ? String(init.statusText) : '';
		this.headers = new Headers(init.headers);
		this.url = '';
		this.redirected = false;
		this.type = 'basic';
		this.bodyUsed = false;
	}
	get ok() { return this.status >= 200 && this.status < 300; }
	get body() { return null; }		/* (no streams) */
	_take() {
		if (this.bodyUsed) return Promise.reject(new TypeError('Body has already been consumed'));
		this.bodyUsed = true;
		return Promise.resolve(this._body);
	}
	text() { return this._take().then(b => (typeof b === 'string' ? b : N.utf8(b))); }
	json() { return this.text().then(JSON.parse); }
	arrayBuffer() { return this._take().then(b => (typeof b === 'string' ? new TextEncoder().encode(b).buffer : b)); }
	bytes() { return this.arrayBuffer().then(b => new Uint8Array(b)); }
	blob() { return this.text().then(t => new Blob([t], { type: this.headers.get('content-type') || '' })); }
	formData() {
		return this.text().then(t => { const f = new FormData(); for (const [k, v] of new URLSearchParams(t)) f.append(k, v); return f; });
	}
	clone() {
		if (this.bodyUsed) throw new TypeError('Body has already been consumed');
		const r = new Response(this._body, this);
		r.headers = new Headers(this.headers);
		r.url = this.url; r.redirected = this.redirected; r.type = this.type;
		return r;
	}
	static json(d, init = {}) {
		const r = new Response(JSON.stringify(d), init);
		if (!r.headers.has('content-type')) r.headers.set('content-type', 'application/json');
		return r;
	}
	static error() { const r = new Response(null, { status: 0 }); r.type = 'error'; return r; }
	static redirect(u, st = 302) { const r = new Response(null, { status: st }); r.headers.set('location', absURL(u)); return r; }
}

function fetch(input, init = {}) {
	let req;
	try { req = new Request(input, init); } catch (e) { return Promise.reject(e); }
	return new Promise((resolve, reject) => {
		const sig = req.signal;
		const aborted = () => sig.reason !== undefined ? sig.reason : new DOMException('The operation was aborted.', 'AbortError');
		if (sig.aborted) { reject(aborted()); return; }
		let id = 0;
		const onAbort = () => { if (id > 0) N.abortRequest(id); id = 0; reject(aborted()); };
		id = N.request(req.method, req.url, req._body, headerLines(req.headers), true, (err, r) => {
			sig.removeEventListener('abort', onAbort);
			if (id === 0) return;			/* (aborted) */
			id = 0;
			if (err != null) { reject(new TypeError('Failed to fetch')); return; }
			const res = new Response(r.body, { status: r.status,
				statusText: r.statusText || STATUS_TEXT[r.status] || '', headers: r.headers });
			res.url = r.url;
			res.redirected = r.url !== req.url;
			resolve(res);
		});
		if (id < 0) { id = 0; reject(new TypeError('Failed to fetch')); return; }
		sig.addEventListener('abort', onAbort);
	});
}

class XMLHttpRequest extends EventTarget {
	constructor() {
		super();
		this.readyState = 0;
		this.status = 0;
		this.statusText = '';
		this.responseType = '';
		this.responseURL = '';
		this.timeout = 0;
		this.withCredentials = false;
		this.upload = new EventTarget();
		for (const t of ['readystatechange', 'loadstart', 'progress', 'load', 'error',
				 'abort', 'timeout', 'loadend'])
			this['on' + t] = null;
		this._id = 0; this._rh = []; this._h = null; this._res = null; this._sent = false;
	}
	_fire(t, loaded = 0, total = 0) {
		const ev = t === 'readystatechange' ? new Event(t)
			: new ProgressEvent(t, { lengthComputable: total > 0, loaded, total });
		dispatch(this, ev);
		const h = this['on' + t];
		if (typeof h === 'function') try { h.call(this, ev); } catch (e) { report(e); }
	}
	_state(s) { this.readyState = s; this._fire('readystatechange'); }
	open(method, url) {
		if (this._id > 0) N.abortRequest(this._id);
		this._id = 0;
		this._m = String(method).toUpperCase();
		this._u = String(url);
		this._rh = []; this._h = null; this._res = null; this._sent = false;
		this.status = 0; this.statusText = ''; this.responseURL = '';
		this._state(1);
	}
	setRequestHeader(n, v) {
		if (this.readyState !== 1 || this._sent) throw new DOMException('The object is in an invalid state.', 'InvalidStateError');
		this._rh.push([String(n), String(v)]);
	}
	send(body = null) {
		if (this.readyState !== 1 || this._sent) throw new DOMException('The object is in an invalid state.', 'InvalidStateError');
		this._sent = true;
		const h = new Headers();
		for (const [k, v] of this._rh) h.append(k, v);
		const b = this._m === 'GET' || this._m === 'HEAD' ? null : encodeBody(body, h);
		const bin = this.responseType === 'arraybuffer' || this.responseType === 'blob';
		this._fire('loadstart');
		const id = N.request(this._m, absURL(this._u), b, headerLines(h), bin, (err, r) => this._done(id, err, r));
		this._id = id;
		if (id < 0) { this._id = 0; setTimeout(() => this._fail('error'), 0); return; }
		if (this.timeout > 0)
			this._to = setTimeout(() => {
				if (this._id !== id) return;
				N.abortRequest(id); this._id = 0; this._fail('timeout');
			}, this.timeout);
	}
	_done(id, err, r) {
		if (this._id !== id) return;			/* (aborted, or open() again) */
		this._id = 0;
		if (this._to) { clearTimeout(this._to); this._to = 0; }
		if (err != null) { this._fail('error'); return; }
		this.status = r.status;
		this.statusText = r.statusText || STATUS_TEXT[r.status] || '';
		this.responseURL = r.url;
		this._h = r.headers;
		this._res = r.body;
		const n = typeof r.body === 'string' ? r.body.length : r.body.byteLength;
		this._state(2);
		this._state(3);
		this._fire('progress', n, n);
		this._state(4);
		this._fire('load', n, n);
		this._fire('loadend', n, n);
	}
	_fail(t) {
		this.status = 0; this.statusText = ''; this._res = null; this._h = null;
		this._state(4);
		this._fire(t);
		this._fire('loadend');
	}
	abort() {
		if (this._id > 0) {
			N.abortRequest(this._id);
			this._id = 0;
			this._fail('abort');
		}
		this.readyState = 0;
	}
	getResponseHeader(n) {
		if (!this._h) return null;
		const k = hkey(n), v = this._h.filter(([a]) => hkey(a) === k).map(x => x[1]);
		return v.length ? v.join(', ') : null;
	}
	getAllResponseHeaders() {
		return this._h ? this._h.map(([a, b]) => hkey(a) + ': ' + b + '\r\n').join('') : '';
	}
	overrideMimeType(m) { this._mime = m; }
	get responseText() {
		if (this.responseType !== '' && this.responseType !== 'text')
			throw new DOMException('The object is in an invalid state.', 'InvalidStateError');
		return this.readyState >= 3 && typeof this._res === 'string' ? this._res : '';
	}
	get response() {
		if (this.readyState < 4 || this._res == null)
			return this.responseType === '' || this.responseType === 'text' ? this.responseText : null;
		switch (this.responseType) {
		case '': case 'text': return this._res;
		case 'json': try { return JSON.parse(this._res); } catch (e) { return null; }
		case 'arraybuffer': return this._res;
		case 'blob': return new Blob([N.utf8(this._res)], { type: this.getResponseHeader('content-type') || '' });
		default: return null;			/* ('document': not yet) */
		}
	}
	get responseXML() { return null; }
}
def(XMLHttpRequest, { UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4 });

class AbortSignal extends EventTarget {
	constructor() { super(); this.aborted = false; this.reason = undefined; this.onabort = null; }
	throwIfAborted() { if (this.aborted) throw this.reason; }
	static abort(r) { const s = new AbortSignal(); s.aborted = true; s.reason = r; return s; }
	static timeout(ms) { const c = new AbortController(); setTimeout(() => c.abort(), ms); return c.signal; }
}
class AbortController {
	constructor() { this.signal = new AbortSignal(); }
	abort(reason) {
		const s = this.signal;
		if (s.aborted) return;
		s.aborted = true;
		s.reason = reason === undefined ? new DOMException('aborted', 'AbortError') : reason;
		const ev = new Event('abort');
		dispatch(s, ev);
		if (typeof s.onabort === 'function') try { s.onabort(ev); } catch (e) { report(e); }
	}
}

class Blob {
	constructor(parts = [], opts = {}) { this._s = parts.map(String).join(''); this.type = opts.type || ''; }
	get size() { return this._s.length; }
	text() { return Promise.resolve(this._s); }
	slice(a, b, t) { return new Blob([this._s.slice(a, b)], { type: t }); }
}
class File extends Blob {
	constructor(parts, name, opts = {}) { super(parts, opts); this.name = name; this.lastModified = Date.now(); }
}

/* the fonts a document loads: always ready (NetSurf loads them itself) */
const fontFaces = {
	ready: Promise.resolve(),
	status: 'loaded',
	check() { return true; },
	load() { return Promise.resolve([]); },
	add() {}, delete() {}, clear() {},
	forEach() {},
	addEventListener() {}, removeEventListener() {},
	[Symbol.iterator]() { return [][Symbol.iterator](); },
};
G.FontFace = class FontFace {
	constructor(family, source) { this.family = family; this.status = 'loaded'; this.loaded = Promise.resolve(this); }
	load() { return Promise.resolve(this); }
};

/* ---- the global object ------------------------------------------------------------------- */

Object.assign(G, {
	window: G, self: G, top: G, parent: G, frames: G, opener: null,
	location, history, navigator, screen, console, performance,
	setTimeout, setInterval, clearTimeout, clearInterval: clearTimeout,
	requestAnimationFrame, cancelAnimationFrame,
	webkitRequestAnimationFrame: requestAnimationFrame,
	requestIdleCallback: cb => setTimeout(() => cb({ didTimeout: false, timeRemaining: () => 10 }), 1),
	cancelIdleCallback: clearTimeout,
	queueMicrotask: fn => { NativePromise.resolve().then(fn).catch(report); },
	matchMedia, getComputedStyle, atob, btoa,
	URL, URLSearchParams, TextEncoder, TextDecoder, fetch, XMLHttpRequest, Headers, Request, Response,
	AbortController, AbortSignal, Blob, File,
	Location, Storage,
	localStorage: storageProxy(storageOrigin()),
	sessionStorage: storageProxy(),
	name: '',
	status: '',
	closed: false,
	isSecureContext: true,
	origin: location.origin,
	devicePixelRatio: 1,
	crossOriginIsolated: false,
	crypto: {
		getRandomValues(a) { for (let i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 256 ** (a.BYTES_PER_ELEMENT || 1)); return a; },
		randomUUID() { return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => { const r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 3 | 8)).toString(16); }); },
		subtle: {},
	},
	CSS: { supports: (a, b) => b === undefined ? cssSupports(a) :
		cssValid(String(a).trim().toLowerCase(), b), escape: s => String(s).replace(/([^a-zA-Z0-9_ -￿-])/g, '\\$1') },
	customElements: {
		_d: new Map(),
		define(name, cls) { this._d.set(name, cls); },
		get(name) { return this._d.get(name); },
		whenDefined(name) { return Promise.resolve(this._d.get(name)); },
		upgrade() {},
	},
	getSelection() {
		return { rangeCount: 0, isCollapsed: true, type: 'None', toString: () => '',
			removeAllRanges() {}, addRange() {}, getRangeAt() { return new Range(); },
			collapse() {}, selectAllChildren() {}, empty() {} };
	},
	alert(m) { N.log('alert: ' + m); },
	confirm(m) { N.log('confirm: ' + m); return true; },
	prompt(m, d) { N.log('prompt: ' + m); return d === undefined ? null : String(d); },
	print() {},
	open(url) { if (url) N.navigate(String(url)); return null; },
	close() {},
	stop() {},
	focus() {},
	blur() {},
	postMessage(data, origin) {
		setTimeout(() => dispatch(G, new MessageEvent('message', { data, origin: location.origin, source: G })), 0);
	},
	scrollTo(x, y) {
		if (x && typeof x === 'object') { y = x.top === undefined ? G.scrollY : x.top; x = x.left === undefined ? G.scrollX : x.left; }
		N.scrollTo(Math.round(+x || 0), Math.round(+y || 0));
		scheduleObservers();
	},
	scrollBy(x, y) {
		if (x && typeof x === 'object') { y = x.top || 0; x = x.left || 0; }
		N.scrollTo(Math.round(G.scrollX + (+x || 0)), Math.round(G.scrollY + (+y || 0)));
		scheduleObservers();
	},
	addEventListener: EventTarget.prototype.addEventListener,
	removeEventListener: EventTarget.prototype.removeEventListener,
	dispatchEvent: EventTarget.prototype.dispatchEvent,
});
G.scroll = G.scrollTo;
for (const [k, i] of [['scrollX', 0], ['scrollY', 1], ['pageXOffset', 0], ['pageYOffset', 1],
		['innerWidth', 2], ['innerHeight', 3], ['outerWidth', 2], ['outerHeight', 3]])
	Object.defineProperty(G, k, { configurable: true, get: () => N.scroll()[i] });
Object.defineProperty(G, 'screenX', { value: 0, configurable: true });
Object.defineProperty(G, 'screenY', { value: 0, configurable: true });
Object.defineProperty(G, 'document', { configurable: true, get: () => N.document() });
defineHandlers(G);
for (const k of ['onload', 'onscroll', 'onresize'])
	if (!(k in G)) G[k] = null;

/* ---- the browser's events --------------------------------------------------------------- */

const KEY_CODES = { Enter: 13, Escape: 27, Backspace: 8, Tab: 9, ' ': 32, ArrowLeft: 37,
	ArrowUp: 38, ArrowRight: 39, ArrowDown: 40, Delete: 46, Home: 36, End: 35,
	PageUp: 33, PageDown: 34 };

function browserEvent(target, type, init) {
	let ev;
	switch (type) {
	case 'click': case 'dblclick': case 'mousedown': case 'mouseup': case 'contextmenu':
		ev = new MouseEvent(type, Object.assign({ bubbles: true, cancelable: true, view: G, detail: 1 }, init));
		break;
	case 'mousemove': case 'mouseover': case 'mouseout':
		ev = new MouseEvent(type, Object.assign({ bubbles: true, cancelable: true, view: G }, init));
		break;
	case 'pointerdown': case 'pointerup':
		ev = new PointerEvent(type, Object.assign({ bubbles: true, cancelable: true, view: G }, init));
		break;
	case 'keydown': case 'keyup': case 'keypress': {
		const key = init.key || '';
		const code = KEY_CODES[key] || (key.length === 1 ? key.toUpperCase().charCodeAt(0) : 0);
		ev = new KeyboardEvent(type, Object.assign({ bubbles: true, cancelable: true, view: G, keyCode: code,
			code: key.length === 1 ? (/[a-z]/i.test(key) ? 'Key' + key.toUpperCase() : /\d/.test(key) ? 'Digit' + key : '') : key }, init, { keyCode: code }));
		break;
	}
	case 'DOMContentLoaded':
		ev = new Event(type, { bubbles: true });
		break;
	case 'readystatechange': case 'load': case 'pageshow':
		ev = new Event(type);
		break;
	case 'scroll':
		ev = new Event(type, { bubbles: target !== null });
		break;
	case 'resize':
		ev = new UIEvent(type);
		break;
	case 'input': case 'change':
		ev = new Event(type, { bubbles: true });
		break;
	case 'submit':
		ev = new SubmitEvent(type, { bubbles: true, cancelable: true });
		break;
	case 'focus': case 'blur':
		ev = new FocusEvent(type);
		break;
	case 'wheel':
		ev = new WheelEvent(type, Object.assign({ bubbles: true, cancelable: true }, init));
		break;
	default:
		ev = new Event(type, { bubbles: true, cancelable: true });
	}
	ev.isTrusted = true;
	return ev;
}

/* The pointer's moves (Onyx: "onyx:hover" at every move, with the element under it): the
 * element left gets mouseout (bubbling) and mouseleave (it and each ancestor the new one is
 * not in, not bubbling), the one entered mouseover and mouseenter, then mousemove. */
let hovered = null;
function hoverTo(el, init) {
	const old = hovered;
	if (el !== old) {
		hovered = el;
		const path = n => { const a = []; for (; n; n = n.parentNode) if (n instanceof Element) a.push(n); return a; };
		const oldPath = old && old.isConnected ? path(old) : [], newPath = path(el);
		const mev = (type, rel, bubbles) =>
			new MouseEvent(type, Object.assign({ bubbles, cancelable: bubbles, view: G }, init, { relatedTarget: rel }));
		if (oldPath.length) {
			dispatch(old, mev('mouseout', el, true));
			for (const n of oldPath) if (!newPath.includes(n)) dispatch(n, mev('mouseleave', el, false));
		}
		if (el) {
			dispatch(el, mev('mouseover', old, true));
			for (const n of newPath.slice().reverse()) if (!oldPath.includes(n)) dispatch(n, mev('mouseenter', old, false));
		}
	}
	if (el) dispatch(el, browserEvent(el, 'mousemove', init));
}

/* ---- ES modules: <script type="module"> (qjs.c: moduleSource, moduleRun) -------------------
 * A module and every module its imports name are fetched in parallel (the loader in qjs.c
 * compiles them from their sources: it cannot wait for the network), then it runs. Deferred:
 * after the document is parsed, in the document's order, before DOMContentLoaded; an async
 * one as soon as it is ready. */
const MOD_FETCHED = new Map();		/* url -> Promise<text | null> */
let modInline = 0, modBusy = 0, modParsed = false;
const MOD_QUEUE = [];
let modChain = NativePromise.resolve();
const MOD_IMPORT = /(?:^|[;\n\r}])\s*(?:import|export)\s*(?:[\w*{}\s,$]+?\s*from\s*)?(['"])([^'"\n]+)\1|\bimport\s*\(\s*(['"])([^'"\n]+)\3\s*\)/g;

function modFetch(url) {
	let p = MOD_FETCHED.get(url);
	if (!p) {
		p = fetch(url).then(r => (r.ok || r.status === 0) ? r.text() : null).then(t => {
			if (t !== null)
				N.moduleSource(url, t);
			return t;
		}, () => null);
		MOD_FETCHED.set(url, p);
	}
	return p;
}

/* the module's imports, and theirs, fetched (in parallel) */
function modGraph(url, text, seen) {
	const deps = [];
	for (const m of text.matchAll(MOD_IMPORT)) {
		const spec = m[2] || m[4];
		if (!/^(\.{0,2}\/|[a-z][a-z0-9+.-]*:)/i.test(spec))
			continue;		/* (a bare specifier: no import map) */
		let u;
		try { u = new URL(spec, url).href; } catch (e) { continue; }
		if (seen.has(u))
			continue;
		seen.add(u);
		deps.push(modFetch(u).then(t => t !== null ? modGraph(u, t, seen) : null));
	}
	return NativePromise.all(deps);
}

async function modRun(url, text) {
	await modGraph(url, text, new Set([url]));
	for (let i = 0; i < 20; i++) {
		const r = N.moduleRun(url, text);
		if (!Array.isArray(r))
			return r;
		const got = await NativePromise.all(r.map(u => modFetch(u).then(t =>
			t !== null ? modGraph(u, t, new Set([u])).then(() => true) : false)));
		if (!got.some(x => x))
			break;			/* (they cannot be fetched) */
	}
	return false;
}

function moduleScript(node) {
	const src = N.attr(node, 'src');
	let url;
	try {
		url = src ? new URL(src, location.href).href :
			location.href.split('#')[0] + '#module-' + (++modInline);
	} catch (e) { return; }
	const text = src ? modFetch(url) : NativePromise.resolve(node.textContent || '');
	const job = async () => {
		const t = await text;
		if (t === null) {
			dispatch(node, browserEvent(node, 'error', {}));
			return;
		}
		const ok = await modRun(url, t);
		if (src)
			dispatch(node, browserEvent(node, ok ? 'load' : 'error', {}));
	};
	if (src && node.hasAttribute('async')) {
		modBusy++;
		job().catch(report).finally(() => modBusy--);
		return;
	}
	MOD_QUEUE.push(job);
	if (modParsed)
		modFlush();
}

function modFlush() {
	const jobs = MOD_QUEUE.splice(0);
	if (!jobs.length)
		return modChain;
	modBusy++;
	modChain = modChain.then(async () => {
		for (const j of jobs) {
			try { await j(); } catch (e) { report(e); }
		}
	}).finally(() => modBusy--);
	return modChain;
}

function browserDispatch(target, type, init) {
	if (type === 'onyx:module') {
		if (target instanceof Element)
			moduleScript(target);
		return true;
	}
	if (type === 'onyx:interactive') {
		/* the document parsed: its modules run, then DOMContentLoaded */
		readyState = 'interactive';
		modParsed = true;
		const fire = () => {
			dispatch(G.document, browserEvent(G.document, 'readystatechange', {}));
			dispatch(G.document, browserEvent(G.document, 'DOMContentLoaded', {}));
		};
		if (MOD_QUEUE.length || modBusy)
			modFlush().then(fire);
		else
			fire();
		return true;
	}
	if (type === 'onyx:complete') {
		/* laid out, its images in: the window's load (after the modules) */
		const fire = () => {
			readyState = 'complete';
			dispatch(G.document, browserEvent(G.document, 'readystatechange', {}));
			dispatch(G, browserEvent(null, 'load', {}));
			dispatch(G, browserEvent(null, 'pageshow', {}));
			scheduleObservers();
		};
		if (modBusy)
			modChain.then(fire);
		else
			fire();
		return true;
	}
	let t = target === null ? G : target;
	if (t instanceof CharacterData)		/* (a text's element is the target) */
		t = N.parent(t) || G;
	if (type === 'onyx:hover') {
		hoverTo(t instanceof Element ? t : null, init || {});
		return true;
	}
	if (type === 'mousedown' && t instanceof HTMLElement) {
		const f = t.closest('a,button,input,select,textarea,[tabindex]');
		if (f && f !== activeElement) f.focus();
	}
	const ev = browserEvent(target, type, init || {});
	const ok = dispatch(t, ev);
	if (type === 'scroll' || type === 'resize' || type === 'load') {
		if (type === 'resize') checkMedia();
		scheduleObservers();
	}
	return ok;
}

/* ---- the natives' setup ------------------------------------------------------------------- */

N.setup({
	node: Node.prototype,
	element: HTMLElement.prototype,
	text: Text.prototype,
	comment: Comment.prototype,
	document: Document.prototype,
	fragment: DocumentFragment.prototype,
	doctype: DocumentType.prototype,
	tags: TAGS,
	dispatch: browserDispatch,
});

/* the document's own wrapper made now: window.document is it */
N.document();
})
