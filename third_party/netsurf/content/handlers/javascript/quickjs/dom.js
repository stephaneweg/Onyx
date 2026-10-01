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
/* (Onyx: or another realm's node -- a same-origin frame's: N.isNode, qjs_frames.c) */
function isNode(v) { return v instanceof Node || (N.isNode !== undefined && N.isNode(v)); }
function isElement(v) { return v instanceof Element; }

/* ---- errors -------------------------------------------------------------------------- */

/* Onyx: the legacy codes (testharness.js' assert_throws_dom checks them) */
const DOM_EXCEPTION_CODES = { IndexSizeError: 1, HierarchyRequestError: 3,
	WrongDocumentError: 4, InvalidCharacterError: 5, NoModificationAllowedError: 7,
	NotFoundError: 8, NotSupportedError: 9, InUseAttributeError: 10, InvalidStateError: 11,
	SyntaxError: 12, InvalidModificationError: 13, NamespaceError: 14, InvalidAccessError: 15,
	TypeMismatchError: 17, SecurityError: 18, NetworkError: 19, AbortError: 20,
	URLMismatchError: 21, QuotaExceededError: 22, TimeoutError: 23, InvalidNodeTypeError: 24,
	DataCloneError: 25 };
class DOMException extends Error {
	constructor(message = '', name = 'Error') {
		super(message);
		this.name = name;
	}
	get code() { return DOM_EXCEPTION_CODES[this.name] || 0; }
}
{
	const k = { INDEX_SIZE_ERR: 1, DOMSTRING_SIZE_ERR: 2, HIERARCHY_REQUEST_ERR: 3,
		WRONG_DOCUMENT_ERR: 4, INVALID_CHARACTER_ERR: 5, NO_DATA_ALLOWED_ERR: 6,
		NO_MODIFICATION_ALLOWED_ERR: 7, NOT_FOUND_ERR: 8, NOT_SUPPORTED_ERR: 9,
		INUSE_ATTRIBUTE_ERR: 10, INVALID_STATE_ERR: 11, SYNTAX_ERR: 12,
		INVALID_MODIFICATION_ERR: 13, NAMESPACE_ERR: 14, INVALID_ACCESS_ERR: 15,
		VALIDATION_ERR: 16, TYPE_MISMATCH_ERR: 17, SECURITY_ERR: 18, NETWORK_ERR: 19,
		ABORT_ERR: 20, URL_MISMATCH_ERR: 21, QUOTA_EXCEEDED_ERR: 22, TIMEOUT_ERR: 23,
		INVALID_NODE_TYPE_ERR: 24, DATA_CLONE_ERR: 25 };
	for (const n of Object.keys(k)) {
		Object.defineProperty(DOMException, n, { value: k[n], enumerable: true });
		Object.defineProperty(DOMException.prototype, n, { value: k[n], enumerable: true });
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
	/* (Onyx: empty once dispatched, as the DOM says) */
	composedPath() { return this._path && this.eventPhase !== 0 ? this._path.slice() : []; }
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
/* Onyx: CSS animations' and transitions' events (the animations' timeline sends them:
 * onyx_anim.c; a script can make and dispatch them too) */
class AnimationEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		this.animationName = String(init.animationName ?? '');
		this.elapsedTime = +init.elapsedTime || 0;
		this.pseudoElement = String(init.pseudoElement ?? '');
	}
}
class TransitionEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		this.propertyName = String(init.propertyName ?? '');
		this.elapsedTime = +init.elapsedTime || 0;
		this.pseudoElement = String(init.pseudoElement ?? '');
	}
}
/* Onyx: the attributes on the prototypes, as the IDL has them ('clientX' in
 * MouseEvent.prototype: feature tests -- browserscore.dev -- look there); a constructor's
 * value kept in a symbol of the object */
function protoFields(C, names) {
	for (const k of names) {
		if (Object.getOwnPropertyDescriptor(C.prototype, k)) continue;
		const slot = Symbol(k);
		Object.defineProperty(C.prototype, k, { configurable: true, enumerable: true,
			get() { return this[slot]; },
			set(v) { Object.defineProperty(this, slot, { value: v, writable: true, configurable: true }); } });
	}
}
protoFields(UIEvent, ['view', 'detail']);
protoFields(MouseEvent, ['clientX', 'clientY', 'screenX', 'screenY', 'pageX', 'pageY', 'offsetX',
	'offsetY', 'x', 'y', 'button', 'buttons', 'ctrlKey', 'shiftKey', 'altKey', 'metaKey', 'relatedTarget']);
protoFields(PointerEvent, ['pointerId', 'pointerType', 'isPrimary', 'width', 'height', 'pressure']);
protoFields(WheelEvent, ['deltaX', 'deltaY', 'deltaZ', 'deltaMode']);
protoFields(KeyboardEvent, ['key', 'code', 'keyCode', 'which', 'charCode', 'location', 'repeat',
	'isComposing', 'ctrlKey', 'shiftKey', 'altKey', 'metaKey']);
protoFields(FocusEvent, ['relatedTarget']);
protoFields(InputEvent, ['data', 'inputType', 'isComposing']);
protoFields(ErrorEvent, ['message', 'error']);
protoFields(ProgressEvent, ['lengthComputable', 'loaded', 'total']);
protoFields(PopStateEvent, ['state']);
protoFields(HashChangeEvent, ['oldURL', 'newURL']);
protoFields(MessageEvent, ['data', 'origin', 'source']);
protoFields(SubmitEvent, ['submitter']);
protoFields(PromiseRejectionEvent, ['promise', 'reason']);
protoFields(AnimationEvent, ['animationName', 'elapsedTime', 'pseudoElement']);
protoFields(TransitionEvent, ['propertyName', 'elapsedTime', 'pseudoElement']);
Object.assign(G, { Event, CustomEvent, UIEvent, MouseEvent, PointerEvent, WheelEvent,
	KeyboardEvent, FocusEvent, InputEvent, ErrorEvent, ProgressEvent, PopStateEvent,
	HashChangeEvent, MessageEvent, SubmitEvent, PromiseRejectionEvent, AnimationEvent,
	TransitionEvent, TouchEvent: undefined });
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
		const b = G.document && G.document.body;	/* (Onyx: a worker has no document) */
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

/* Onyx: shadow DOM -- html5.js's dispatch across the shadow trees (the event's path through
 * the slots and the hosts, its target retargeted at each), once the document has some */
const shadowHook = { dispatch: null, on: () => false };
function dispatch(target, ev) {
	if (shadowHook.dispatch !== null && target instanceof Node && shadowHook.on())
		return shadowHook.dispatch(target, ev, invoke);
	const path = [];
	for (let n = target; n; n = eventParent(n)) {
		/* (Onyx: an element's load event stops at the document -- the window is not in
		 * its path (HTML): duckduckgo.com's capturing window load listener added itself
		 * again at each image's load) */
		if (n === G && ev.type === 'load' && target !== G)
			break;
		path.push(n);
	}
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
		/* (Onyx: called unbound -- const add = addEventListener; add(...) -- it is the
		 * window's, as in a browser) */
		const l = listenersOf(this == null ? G : this, true);
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
		const l = listenersOf(this == null ? G : this, false);
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
		return dispatch(this == null ? G : this, ev);
	}
}
G.EventTarget = EventTarget;

/* on* properties: a handler per type */
const HANDLER_TYPES = ['click', 'dblclick', 'mousedown', 'mouseup', 'mousemove',
	'mouseover', 'mouseout', 'mouseenter', 'mouseleave', 'contextmenu', 'wheel',
	'keydown', 'keyup', 'keypress', 'focus', 'blur', 'focusin', 'focusout', 'input',
	'change', 'submit', 'reset', 'load', 'error', 'abort', 'scroll', 'resize',
	/* (Onyx: no ontouchstart / ontouchend / ontouchmove: a mouse, not a touch screen,
	 * as Chrome on a desktop -- 'ontouchstart' in window told pages there was one) */
	'select', 'pointerdown', 'pointerup', 'pointermove', 'animationend', 'transitionend',
	'beforeunload', 'unload',
	'animationstart', 'animationiteration', 'animationcancel', 'transitionrun',
	'transitionstart', 'transitioncancel', 'beforetoggle',	/* (the Popover API's) */
	'hashchange', 'popstate', 'message', 'toggle', 'play', 'pause', 'ended'];
function defineHandlers(proto, types = HANDLER_TYPES) {
	for (const t of types) {
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

/* Onyx: a node of another document (another frame's, a DOMParser's) is adopted when it is
 * inserted, as in browsers: libdom cannot move a node between documents (its import keeps the
 * old document as the copy's owner), so the subtree is made again by the target document and
 * the original taken out of its parent; the copy is inserted (and returned) */
function recreateIn(doc, n) {
	const t = N.type(n);
	let c;
	if (t === 1) {
		c = N.createIn(doc, 'elementNS', N.nsURI(n), N.qname(n));
		for (const a of N.attrsNS(n)) N.setAttrNS(c, a[2], a[0], a[1]);
	} else if (t === 3) {
		c = N.createIn(doc, 'text', N.value(n));
	} else if (t === 8) {
		c = N.createIn(doc, 'comment', N.value(n));
	} else if (t === 11) {
		c = N.createIn(doc, 'fragment');
	} else {
		throw new DOMException('This node cannot be adopted.', 'NotSupportedError');
	}
	for (const k of N.children(n)) N.insert(c, recreateIn(doc, k), null);
	return c;
}
function insertAdopting(p, c, ref) {
	try {
		N.insert(p, c, ref);
		return c;
	} catch (e) {
		if (N.type(c) === 9) throw e;
		const doc = N.type(p) === 9 ? p : N.ownerDoc(p), own = N.ownerDoc(c);
		if (!doc || !own || own === doc) throw e;
		const copy = recreateIn(doc, c), old = N.parent(c);
		if (old) N.remove(old, c);
		N.insert(p, copy, ref);
		return copy;
	}
}

function nodesToNode(args) {
	if (args.length === 1)
		return toNode(args[0]);
	const f = N.createFragment();
	for (const a of args)
		insertAdopting(f, toNode(a), null);
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
		let added = N.type(c) === DOCUMENT_FRAGMENT_NODE ? N.children(c) : [c];
		const oldParent = N.parent(c);
		if (oldParent && observers.size)
			childListRecord(oldParent, [], [c]);
		const ins = insertAdopting(this, c, ref || null);	/* (Onyx) */
		if (ins !== c) {
			c = ins;
			added = N.type(c) === DOCUMENT_FRAGMENT_NODE ? N.children(c) : [c];
		}
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
		/* Onyx: "#id" from libdom's getElementById (bliss's $('#x'): css3test.com asks
		 * it thousands of times, each a walk of the whole tree in JS) */
		const id = typeof sel === 'string' && /^#[a-zA-Z_][-\w]*$/.test(sel) ? sel.slice(1) : null;
		if (id !== null && (this === G.document || this.getRootNode() === G.document)) {
			const e = G.document.getElementById(id);
			if (e && e !== this && (this === G.document || this.contains(e)))
				return e;
			if (!e || this === G.document)
				return null;
		}
		const list = parseSelector(sel);
		for (let e = N.nextElement(this, this); e; e = N.nextElement(e, this))
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
		/* Onyx: the classes matched by the native walk (qjs.c n_descendants) */
		names = String(names);
		if (!/[^\t\n\f\r ]/.test(names)) return htmlCollection([]);
		return htmlCollection(N.descendants(this, names));
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
	contains(t) { return N.hasToken(this._el, this._attr, String(t)); }
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
	/* (Onyx: nor "!important" -- a value, not a declaration: CSS.supports("color", "red !important") is false) */
	if (/[;{}!]/.test(v) && /[;{}!]/.test(v.replace(/"[^"]*"|'[^']*'|\([^)]*\)/g, '')))	/* (Onyx: the strings cut only when one is there) */
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
		/* (Onyx: the end of the text closes the blocks still open, as CSS Syntax does:
		 * "selector(:nth-child(even of :not([hidden]))" is supported in a browser) */
		return s.slice(start) + ')'.repeat(Math.max(0, depth - 1));
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
			return g !== null && g.trim() !== "" && cssSelectorValid(g);
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

/* ---- the CSSOM (Onyx): style sheets and their rules ------------------------------------------
 * A sheet's rules are read from its text -- a <style>'s content, a <link>'s loaded sheet
 * (N.sheetText), a constructed sheet's replace() text -- when first asked for, and kept as
 * libcss keeps them: a rule, a declaration or a descriptor libcss drops (an invalid selector,
 * an unknown at-rule, a value its property's grammar refuses) is not there (N.cssKept).
 * Changes -- insertRule, deleteRule, replaceSync, a rule's style -- are written back to the
 * sheet's text: a <style>'s content (NetSurf styles the page again), a constructed sheet's
 * adopted <style> (document.adoptedStyleSheets); a linked sheet changes in memory only. */

/* the top-level items of a CSS text: [{ prelude, body }] (body null: a statement ending
 * with ';'; the comments dropped, the strings and brackets kept whole) */
function cssItems(text) {
	const out = [];
	text = String(text);
	let i = 0, start = 0, quote = null, depth = 0, paren = 0, bodyStart = -1, prelude = '';
	const n = text.length;
	for (; i < n; i++) {
		const c = text[i];
		if (quote) {
			if (c === '\\') i++;
			else if (c === quote) quote = null;
			continue;
		}
		if (c === '/' && text[i + 1] === '*') {
			const e = text.indexOf('*/', i + 2);
			if (depth === 0) {
				text = text.slice(0, i) + ' ' + (e < 0 ? '' : text.slice(e + 2));
				i--;
				continue;
			}
			i = e < 0 ? n : e + 1;
			continue;
		}
		if (c === '"' || c === "'") { quote = c; continue; }
		if (c === '(' || c === '[') { paren++; continue; }
		if ((c === ')' || c === ']') && paren > 0) { paren--; continue; }
		if (c === '{') {
			if (depth++ === 0) { prelude = text.slice(start, i).trim(); bodyStart = i + 1; }
		} else if (c === '}') {
			if (depth > 0 && --depth === 0) {
				out.push({ prelude, body: text.slice(bodyStart, i) });
				start = i + 1;
				paren = 0;
			}
		} else if (c === ';' && depth === 0 && paren === 0) {
			const st = text.slice(start, i).trim();
			if (st) out.push({ prelude: st, body: null });
			start = i + 1;
		}
	}
	if (depth > 0)		/* (an unclosed block at the end: closed there) */
		out.push({ prelude, body: text.slice(bodyStart) });
	else {
		const st = text.slice(start).trim();
		if (st) out.push({ prelude: st, body: null });
	}
	return out;
}

/* a declaration's name and value ("color: red !important" -> ['color', 'red !important']) */
function cssDeclSplit(t) {
	const i = t.indexOf(':');
	if (i < 0) return null;
	const k = t.slice(0, i).trim();
	return k ? [k.startsWith('--') ? k : k.toLowerCase(), t.slice(i + 1).trim()] : null;
}

function cssKeptText(text) {
	const r = N.cssKept(text, false);
	return r === null ? [0, 0] : r;
}

/* ---- MediaList, CSSRuleList, StyleSheetList ---- */
class MediaList {
	constructor(text, onchange) {
		Object.defineProperty(this, '_l', { value: [], writable: true });
		Object.defineProperty(this, '_c', { value: onchange || null });
		this._set(text || '');
	}
	_set(text) {
		const l = [];
		let depth = 0, s = 0;
		text = String(text);
		for (let i = 0; i <= text.length; i++) {
			const c = text[i];
			if (c === '(') depth++;
			else if (c === ')') depth--;
			else if ((c === ',' && depth === 0) || i === text.length) {
				const m = text.slice(s, i).trim().replace(/\s+/g, ' ');
				if (m) l.push(m);
				s = i + 1;
			}
		}
		for (let i = 0; i < this._l.length; i++) delete this[i];
		this._l = l;
		l.forEach((m, i) => Object.defineProperty(this, i, { value: m, configurable: true, enumerable: true }));
	}
	get mediaText() { return this._l.join(', '); }
	set mediaText(v) { this._set(v === null ? '' : v); if (this._c) this._c(); }
	get length() { return this._l.length; }
	item(i) { return this._l[i] === undefined ? null : this._l[i]; }
	appendMedium(m) { m = String(m).trim(); if (m && !this._l.includes(m)) { this._set(this._l.concat([m]).join(',')); if (this._c) this._c(); } }
	deleteMedium(m) {
		const i = this._l.indexOf(String(m).trim());
		if (i < 0) throw new DOMException('not in the list', 'NotFoundError');
		const l = this._l.slice(); l.splice(i, 1); this._set(l.join(',')); if (this._c) this._c();
	}
	toString() { return this.mediaText; }
	[Symbol.iterator]() { return this._l[Symbol.iterator](); }
}
G.MediaList = MediaList;

class CSSRuleList {
	constructor(rules) {
		Object.defineProperty(this, '_r', { value: rules || [], writable: true });
		this._fill();
	}
	_fill() {
		for (const k of Object.keys(this)) delete this[k];
		this._r.forEach((r, i) => Object.defineProperty(this, i, { value: r, configurable: true, enumerable: true }));
	}
	get length() { return this._r.length; }
	item(i) { return this._r[i] || null; }
	[Symbol.iterator]() { return this._r[Symbol.iterator](); }
}
G.CSSRuleList = CSSRuleList;

class StyleSheetList {
	constructor(sheets) {
		sheets.forEach((s, i) => Object.defineProperty(this, i, { value: s, enumerable: true }));
		Object.defineProperty(this, '_s', { value: sheets });
	}
	get length() { return this._s.length; }
	item(i) { return this._s[i] || null; }
	[Symbol.iterator]() { return this._s[Symbol.iterator](); }
}
G.StyleSheetList = StyleSheetList;

/* ---- the rules ---- */
const RULE_OWN = Symbol('rule');
class CSSRule {
	constructor(key) {
		if (key !== RULE_OWN) throw new TypeError('Illegal constructor');
		Object.defineProperty(this, '_parent', { value: null, writable: true });
		Object.defineProperty(this, '_sheet', { value: null, writable: true });
	}
	get parentRule() { return this._parent; }
	get parentStyleSheet() { return this._sheet; }
	/* (Onyx: the legacy type numbers, 0 for the newer rules) */
	get type() {
		return ({ CSSStyleRule: 1, CSSImportRule: 3, CSSMediaRule: 4, CSSFontFaceRule: 5,
			CSSPageRule: 6, CSSKeyframesRule: 7, CSSKeyframeRule: 8, CSSMarginRule: 9,
			CSSNamespaceRule: 10, CSSCounterStyleRule: 11, CSSSupportsRule: 12,
			CSSFontFeatureValuesRule: 14 })[this.constructor.name] || 0;
	}
	get cssText() { return ''; }
	set cssText(v) { /* (no effect, as in browsers) */ }
	_changed() { if (this._sheet) this._sheet._changed(); }
}
Object.assign(CSSRule, { STYLE_RULE: 1, CHARSET_RULE: 2, IMPORT_RULE: 3, MEDIA_RULE: 4,
	FONT_FACE_RULE: 5, PAGE_RULE: 6, KEYFRAMES_RULE: 7, KEYFRAME_RULE: 8, MARGIN_RULE: 9,
	NAMESPACE_RULE: 10, COUNTER_STYLE_RULE: 11, SUPPORTS_RULE: 12, FONT_FEATURE_VALUES_RULE: 14 });
for (const k of Object.keys(CSSRule)) Object.defineProperty(CSSRule.prototype, k, { value: CSSRule[k] });
G.CSSRule = CSSRule;

function cssRuleType(r) { return r.constructor.TYPE || 0; }

/* rules that hold declarations (a style rule, @font-face, @page, a keyframe, @position-try...):
 * rule.style over its valid declarations */
function declMixin(cls, validOf) {
	Object.defineProperty(cls.prototype, 'style', {
		get() {
			let s = this._style;
			if (!s) Object.defineProperty(this, '_style', { value: s = styleProxyOf(new CSSStyleDeclaration(null, this)) });
			return s;
		},
		set(v) { this.style.cssText = v; },
		configurable: true,
	});
	cls.prototype._declValid = validOf;
	cls.prototype._decls = function () {
		if (!this._dm) {
			const m = new Map();
			for (const it of cssItems(this._body || '')) {
				if (it.body !== null) continue;
				const d = cssDeclSplit(it.prelude);
				if (!d) continue;
				let v = d[1], pri = '';
				const im = v.match(/\s*!\s*important\s*$/i);
				if (im) { pri = 'important'; v = v.slice(0, im.index).trim(); }
				if (this._declValid(d[0], v))
					m.set(d[0], { v, pri });
			}
			Object.defineProperty(this, '_dm', { value: m, writable: true });
		}
		return this._dm;
	};
	cls.prototype._setDecls = function (m) {
		if (!this._dm) Object.defineProperty(this, '_dm', { value: m, writable: true });
		else this._dm = m;
		this._body = serializeDecls(m);
		this._changed();
	};
}
const propValid = (k, v) => k.startsWith('--') || cssValid(k, v);

class CSSGroupingRule extends CSSRule {
	get cssRules() {
		if (!this._rl) Object.defineProperty(this, '_rl', { value: new CSSRuleList(cssParseRules(this._body || '', this, this._sheet, this._childKind())) });
		return this._rl;
	}
	_childKind() { return 'rules'; }
	insertRule(text, index) {
		const l = this.cssRules;
		index = index === undefined ? 0 : index >>> 0;
		if (index > l._r.length) throw new DOMException('index out of range', 'IndexSizeError');
		const r = cssParseRules(String(text), this, this._sheet, this._childKind());
		if (r.length !== 1) throw new DOMException('invalid rule', 'SyntaxError');
		l._r.splice(index, 0, r[0]);
		l._fill();
		this._changed();
		return index;
	}
	deleteRule(index) {
		const l = this.cssRules;
		index >>>= 0;
		if (index >= l._r.length) throw new DOMException('index out of range', 'IndexSizeError');
		l._r[index]._parent = null;
		l._r.splice(index, 1);
		l._fill();
		this._changed();
	}
	_inner() { return this._rl ? this._rl._r.map(r => '  ' + r.cssText).join('\n') : (this._body || '').trim(); }
}
G.CSSGroupingRule = CSSGroupingRule;

class CSSConditionRule extends CSSGroupingRule {
	get conditionText() { return this._cond; }
}
G.CSSConditionRule = CSSConditionRule;

class CSSStyleRule extends CSSGroupingRule {
	static get TYPE() { return 1; }
	get type() { return 1; }
	get selectorText() { return this._sel; }
	set selectorText(v) {
		v = String(v).trim();
		if (cssSelectorValid(v)) { this._sel = v; this._changed(); }
	}
	get cssText() {
		const d = serializeDecls(this._decls());
		const nested = this._rl && this._rl._r.length ? ' ' + this._rl._r.map(r => r.cssText).join(' ') : '';
		return this._sel + ' { ' + (d ? d + ' ' : '') + (nested ? nested.trim() + ' ' : '') + '}';
	}
	_childKind() { return 'nested'; }
}
declMixin(CSSStyleRule, propValid);
G.CSSStyleRule = CSSStyleRule;

class CSSMediaRule extends CSSConditionRule {
	static get TYPE() { return 4; }
	get type() { return 4; }
	get media() {
		if (!this._ml) Object.defineProperty(this, '_ml', { value: new MediaList(this._cond, () => { this._cond = this._ml.mediaText; this._changed(); }) });
		return this._ml;
	}
	get cssText() { return '@media ' + this.media.mediaText + ' {\n' + this._inner() + '\n}'; }
}
G.CSSMediaRule = CSSMediaRule;

class CSSSupportsRule extends CSSConditionRule {
	static get TYPE() { return 12; }
	get type() { return 12; }
	get cssText() { return '@supports ' + this._cond + ' {\n' + this._inner() + '\n}'; }
}
G.CSSSupportsRule = CSSSupportsRule;

class CSSContainerRule extends CSSConditionRule {
	get type() { return 0; }
	get containerName() { const m = /^\s*(?!not\b|style\b|scroll-state\b)([-\w]+)\s+/i.exec(this._cond); return m && !this._cond.trim().startsWith('(') ? m[1] : ''; }
	get containerQuery() { const nm = this.containerName; return nm ? this._cond.trim().slice(nm.length).trim() : this._cond.trim(); }
	get cssText() { return '@container ' + this._cond + ' {\n' + this._inner() + '\n}'; }
}
G.CSSContainerRule = CSSContainerRule;

class CSSLayerBlockRule extends CSSGroupingRule {
	get type() { return 0; }
	get name() { return this._name; }
	get cssText() { return '@layer' + (this._name ? ' ' + this._name : '') + ' {\n' + this._inner() + '\n}'; }
}
G.CSSLayerBlockRule = CSSLayerBlockRule;

class CSSLayerStatementRule extends CSSRule {
	get type() { return 0; }
	get nameList() { return Object.freeze(this._name.split(',').map(s => s.trim()).filter(Boolean)); }
	get cssText() { return '@layer ' + this.nameList.join(', ') + ';'; }
}
G.CSSLayerStatementRule = CSSLayerStatementRule;

class CSSScopeRule extends CSSGroupingRule {
	get type() { return 0; }
	get start() { const m = /^\s*\(([\s\S]*?)\)/.exec(this._cond); return m ? m[1].trim() : null; }
	get end() { const m = /\bto\s*\(([\s\S]*)\)\s*$/i.exec(this._cond); return m ? m[1].trim() : null; }
	get cssText() { return '@scope' + (this._cond ? ' ' + this._cond : '') + ' {\n' + this._inner() + '\n}'; }
}
G.CSSScopeRule = CSSScopeRule;

class CSSStartingStyleRule extends CSSGroupingRule {
	get type() { return 0; }
	get cssText() { return '@starting-style {\n' + this._inner() + '\n}'; }
}
G.CSSStartingStyleRule = CSSStartingStyleRule;

class CSSImportRule extends CSSRule {
	static get TYPE() { return 3; }
	get type() { return 3; }
	get href() { return this._href; }
	get media() {
		if (!this._ml) Object.defineProperty(this, '_ml', { value: new MediaList(this._media) });
		return this._ml;
	}
	get layerName() { return this._layer; }
	get supportsText() { return this._supports; }
	get styleSheet() { return null; }
	get cssText() { return '@import url("' + this._href + '")' + (this._layer !== null ? ' layer' + (this._layer ? '(' + this._layer + ')' : '') : '') + (this._supports !== null ? ' supports(' + this._supports + ')' : '') + (this._media ? ' ' + this._media : '') + ';'; }
}
G.CSSImportRule = CSSImportRule;

class CSSNamespaceRule extends CSSRule {
	static get TYPE() { return 10; }
	get type() { return 10; }
	get namespaceURI() { return this._uri; }
	get prefix() { return this._prefix; }
	get cssText() { return '@namespace ' + (this._prefix ? this._prefix + ' ' : '') + 'url("' + this._uri + '");'; }
}
G.CSSNamespaceRule = CSSNamespaceRule;

/* descriptor rules: valid as libcss keeps "@rule prelude { name: value }" */
function descValid(at) {
	return function (k, v) { return cssKeptText(at + ' ' + (this._name || '') + ' { ' + k + ': ' + v + ' }')[1] > 0; };
}

class CSSFontFaceRule extends CSSRule {
	static get TYPE() { return 5; }
	get type() { return 5; }
	get cssText() { const d = serializeDecls(this._decls()); return '@font-face { ' + (d ? d + ' ' : '') + '}'; }
}
declMixin(CSSFontFaceRule, function (k, v) { return cssKeptText('@font-face { ' + k + ': ' + v + ' }')[1] > 0; });
G.CSSFontFaceRule = CSSFontFaceRule;

class CSSPageRule extends CSSGroupingRule {
	static get TYPE() { return 6; }
	get type() { return 6; }
	get selectorText() { return this._sel; }
	set selectorText(v) { this._sel = String(v).trim(); this._changed(); }
	get cssText() { const d = serializeDecls(this._decls()); return '@page' + (this._sel ? ' ' + this._sel : '') + ' { ' + (d ? d + ' ' : '') + '}'; }
	_childKind() { return 'margins'; }
}
declMixin(CSSPageRule, function (k, v) { return cssKeptText('@page { ' + k + ': ' + v + ' }')[1] > 0; });
G.CSSPageRule = CSSPageRule;

class CSSMarginRule extends CSSRule {
	static get TYPE() { return 9; }
	get type() { return 9; }
	get name() { return this._name; }
	get cssText() { const d = serializeDecls(this._decls()); return '@' + this._name + ' { ' + (d ? d + ' ' : '') + '}'; }
}
declMixin(CSSMarginRule, propValid);
G.CSSMarginRule = CSSMarginRule;

class CSSKeyframeRule extends CSSRule {
	static get TYPE() { return 8; }
	get type() { return 8; }
	get keyText() { return this._sel; }
	set keyText(v) {
		v = String(v).trim();
		if (!keyframeSelectorValid(v)) throw new DOMException('invalid keyframe selector', 'SyntaxError');
		this._sel = v; this._changed();
	}
	get cssText() { const d = serializeDecls(this._decls()); return this._sel + ' { ' + (d ? d + ' ' : '') + '}'; }
}
declMixin(CSSKeyframeRule, (k, v) => !/!\s*important/i.test(v) && propValid(k, v));
G.CSSKeyframeRule = CSSKeyframeRule;

function keyframeSelectorValid(t) {
	return String(t).split(',').every(s => /^\s*(from|to|(?:[a-z-]+\s+)?[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?%)\s*$/i.test(s) &&
		(!/%/.test(s) || (v => v >= 0 && v <= 100)(parseFloat(s.replace(/^[^\d.+-]*/, '')))));
}

class CSSKeyframesRule extends CSSRule {
	static get TYPE() { return 7; }
	get type() { return 7; }
	get name() { return this._name; }
	set name(v) { this._name = String(v); this._changed(); }
	get cssRules() {
		if (!this._rl) Object.defineProperty(this, '_rl', { value: new CSSRuleList(cssParseRules(this._body || '', this, this._sheet, 'keyframes')) });
		return this._rl;
	}
	get length() { return this.cssRules.length; }
	appendRule(text) {
		const r = cssParseRules(String(text), this, this._sheet, 'keyframes');
		if (r.length === 1) { this.cssRules._r.push(r[0]); this.cssRules._fill(); this._changed(); }
	}
	_find(k) {
		const want = String(k).split(',').map(s => s.trim().toLowerCase().replace(/^from$/, '0%').replace(/^to$/, '100%')).join(',');
		const l = this.cssRules._r;
		for (let i = l.length - 1; i >= 0; i--) {
			const got = l[i]._sel.split(',').map(s => s.trim().toLowerCase().replace(/^from$/, '0%').replace(/^to$/, '100%')).join(',');
			if (got === want) return i;
		}
		return -1;
	}
	findRule(k) { const i = this._find(k); return i < 0 ? null : this.cssRules._r[i]; }
	deleteRule(k) { const i = this._find(k); if (i >= 0) { this.cssRules._r.splice(i, 1); this.cssRules._fill(); this._changed(); } }
	get cssText() { return '@keyframes ' + this._name + ' {\n' + this.cssRules._r.map(r => '  ' + r.cssText).join('\n') + '\n}'; }
	[Symbol.iterator]() { return this.cssRules[Symbol.iterator](); }
}
G.CSSKeyframesRule = CSSKeyframesRule;

/* the descriptor at-rules: their descriptors as attributes (camelCase), '' when absent */
function descRule(name, cls, at, descs, type) {
	Object.defineProperty(cls, 'TYPE', { get: () => type || 0 });
	Object.defineProperty(cls.prototype, 'type', { get: () => type || 0, configurable: true });
	declMixin(cls, descValid(at));
	for (const d of descs) {
		const js = d.replace(/-([a-z])/g, (m, c) => c.toUpperCase());
		Object.defineProperty(cls.prototype, js, {
			get() { const x = this._decls().get(d); return x ? x.v : ''; },
			set(v) {
				v = String(v);
				if (!this._declValid(d, v)) return;
				const m = new Map(this._decls()); m.set(d, { v, pri: '' }); this._setDecls(m);
			},
			configurable: true,
		});
	}
	Object.defineProperty(cls.prototype, 'cssText', {
		get() { const d = serializeDecls(this._decls()); return at + (this._name ? ' ' + this._name : '') + ' { ' + (d ? d + ' ' : '') + '}'; },
		configurable: true,
	});
	G[name] = cls;
}
class CSSCounterStyleRule extends CSSRule {
	get name() { return this._name; }
	set name(v) { this._name = String(v); this._changed(); }
}
descRule('CSSCounterStyleRule', CSSCounterStyleRule, '@counter-style', ['system', 'symbols',
	'additive-symbols', 'negative', 'prefix', 'suffix', 'range', 'pad', 'speak-as', 'fallback'], 11);
class CSSPropertyRule extends CSSRule {
	get name() { return this._name; }
	get inherits() { const x = this._decls().get('inherits'); return x ? x.v.trim() === 'true' : false; }
	get initialValue() { const x = this._decls().get('initial-value'); return x ? x.v : null; }
}
descRule('CSSPropertyRule', CSSPropertyRule, '@property', ['syntax']);
class CSSFontPaletteValuesRule extends CSSRule {
	get name() { return this._name; }
}
descRule('CSSFontPaletteValuesRule', CSSFontPaletteValuesRule, '@font-palette-values', ['font-family', 'base-palette', 'override-colors']);
class CSSViewTransitionRule extends CSSRule {
	get types() { const x = this._decls().get('types'); return x && x.v !== 'none' ? x.v.split(/\s+/) : []; }
}
descRule('CSSViewTransitionRule', CSSViewTransitionRule, '@view-transition', ['navigation']);
class CSSPositionTryRule extends CSSRule {
	get type() { return 0; }
	get name() { return this._name; }
	get cssText() { const d = serializeDecls(this._decls()); return '@position-try ' + this._name + ' { ' + (d ? d + ' ' : '') + '}'; }
}
declMixin(CSSPositionTryRule, (k, v) => !/!\s*important/i.test(v) && propValid(k, v));
G.CSSPositionTryRule = CSSPositionTryRule;

class CSSFontFeatureValuesMap extends Map {}
G.CSSFontFeatureValuesMap = CSSFontFeatureValuesMap;
class CSSFontFeatureValuesRule extends CSSRule {
	static get TYPE() { return 14; }
	get type() { return 14; }
	get fontFamily() { return this._name; }
	set fontFamily(v) { this._name = String(v); this._changed(); }
	_map(at) {
		const m = new CSSFontFeatureValuesMap();
		for (const it of cssItems(this._body || '')) {
			if (it.body === null || it.prelude.toLowerCase() !== '@' + at) continue;
			for (const d of cssItems(it.body)) {
				const kv = d.body === null && cssDeclSplit(d.prelude);
				if (kv && /^\d+(\s+\d+)*$/.test(kv[1])) m.set(kv[0], kv[1].split(/\s+/).map(Number));
			}
		}
		return m;
	}
	get annotation() { return this._map('annotation'); }
	get ornaments() { return this._map('ornaments'); }
	get stylistic() { return this._map('stylistic'); }
	get swash() { return this._map('swash'); }
	get characterVariant() { return this._map('character-variant'); }
	get styleset() { return this._map('styleset'); }
	get historicalForms() { return this._map('historical-forms'); }
	get cssText() { return '@font-feature-values ' + this._name + ' { ' + (this._body || '').trim() + ' }'; }
}
G.CSSFontFeatureValuesRule = CSSFontFeatureValuesRule;

class CSSNestedDeclarations extends CSSRule {
	get type() { return 0; }
	get cssText() { return serializeDecls(this._decls()); }
}
declMixin(CSSNestedDeclarations, propValid);
G.CSSNestedDeclarations = CSSNestedDeclarations;

/* a rule object of a class, its fields set */
function mkRule(cls, fields, parent, sheet) {
	const r = Object.create(cls.prototype);
	Object.defineProperty(r, '_parent', { value: parent && parent instanceof CSSRule ? parent : null, writable: true });
	Object.defineProperty(r, '_sheet', { value: sheet || null, writable: true });
	for (const k of Object.keys(fields))
		Object.defineProperty(r, k, { value: fields[k], writable: true });
	return r;
}

/* The rules of a text (kind: 'sheet', 'rules' -- in a grouping rule --, 'nested' -- in a
 * style rule --, 'keyframes', 'margins' -- in @page), as libcss keeps them. */
function cssParseRules(text, parent, sheet, kind) {
	const rules = [];
	const top = kind === 'sheet';
	for (const it of cssItems(text)) {
		const whole = it.prelude + (it.body === null ? ';' : ' {' + it.body + '}');
		const at = /^@([\w-]+)\s*([\s\S]*)$/.exec(it.prelude);
		if (kind === 'keyframes') {
			if (it.body !== null && !at && keyframeSelectorValid(it.prelude))
				rules.push(mkRule(CSSKeyframeRule, { _sel: it.prelude.replace(/\s+/g, ' '), _body: it.body }, parent, sheet));
			continue;
		}
		if (kind === 'margins') {
			if (at && it.body !== null && /^(top|bottom|left|right)-/.test(at[1].toLowerCase()) && cssKeptText('@page { @' + at[1] + ' {} }')[0] >= 0)
				rules.push(mkRule(CSSMarginRule, { _name: at[1].toLowerCase(), _body: it.body }, parent, sheet));
			continue;
		}
		if (!at) {
			if (it.body === null) {
				if (kind === 'nested' && cssDeclSplit(it.prelude)) continue;	/* (the rule's own) */
				continue;
			}
			let sel = it.prelude.replace(/\s+/g, ' ').trim();
			if (kind === 'nested') {
				/* a nested rule: its selector as libcss reads it with & as the parent */
				const test = /&/.test(sel) ? sel.replace(/&/g, ':root') : ':root ' + sel;
				if (!cssSelectorValid(test)) continue;
			} else if (cssKeptText(sel + ' {}')[0] === 0) {
				continue;
			}
			rules.push(mkRule(CSSStyleRule, { _sel: sel, _body: it.body }, parent, sheet));
			continue;
		}
		const name = at[1].toLowerCase(), pre = at[2].trim();
		const kept = () => cssKeptText(whole)[0] > 0;
		let r = null;
		switch (name) {
		case 'media':
			if (it.body !== null && kept()) r = mkRule(CSSMediaRule, { _cond: pre, _body: it.body }, parent, sheet);
			break;
		case 'supports':
			/* (kept in the CSSOM whether its condition holds or not) */
			if (it.body !== null && pre) r = mkRule(CSSSupportsRule, { _cond: pre.replace(/\s+/g, ' '), _body: it.body }, parent, sheet);
			break;
		case 'container':
			if (it.body !== null && kept()) r = mkRule(CSSContainerRule, { _cond: pre, _body: it.body }, parent, sheet);
			break;
		case 'layer':
			if (it.body === null) {
				if (pre && /^[-\w]+(\.[-\w]+)*(\s*,\s*[-\w]+(\.[-\w]+)*)*$/.test(pre)) r = mkRule(CSSLayerStatementRule, { _name: pre }, parent, sheet);
			} else if (!pre || /^[-\w]+(\.[-\w]+)*$/.test(pre)) {
				r = mkRule(CSSLayerBlockRule, { _name: pre, _body: it.body }, parent, sheet);
			}
			break;
		case 'scope':
			if (it.body !== null && kept()) r = mkRule(CSSScopeRule, { _cond: pre, _body: it.body }, parent, sheet);
			break;
		case 'starting-style':
			if (it.body !== null && kept()) r = mkRule(CSSStartingStyleRule, { _body: it.body }, parent, sheet);
			break;
		case 'font-face':
			if (it.body !== null && top) r = mkRule(CSSFontFaceRule, { _body: it.body }, parent, sheet);
			break;
		case 'page':
			if (it.body !== null && kept()) r = mkRule(CSSPageRule, { _sel: pre, _body: it.body }, parent, sheet);
			break;
		case 'keyframes': case '-webkit-keyframes':
			if (it.body !== null && kept()) r = mkRule(CSSKeyframesRule, { _name: pre.replace(/^["']|["']$/g, ''), _body: it.body }, parent, sheet);
			break;
		case 'counter-style':
			if (it.body !== null && kept()) r = mkRule(CSSCounterStyleRule, { _name: pre, _body: it.body }, parent, sheet);
			break;
		case 'property':
			if (it.body !== null && kept()) r = mkRule(CSSPropertyRule, { _name: pre, _body: it.body }, parent, sheet);
			break;
		case 'font-feature-values':
			if (it.body !== null && kept()) r = mkRule(CSSFontFeatureValuesRule, { _name: pre, _body: it.body }, parent, sheet);
			break;
		case 'font-palette-values':
			if (it.body !== null && kept()) r = mkRule(CSSFontPaletteValuesRule, { _name: pre, _body: it.body }, parent, sheet);
			break;
		case 'position-try':
			if (it.body !== null && kept()) r = mkRule(CSSPositionTryRule, { _name: pre, _body: it.body }, parent, sheet);
			break;
		case 'view-transition':
			if (it.body !== null && kept()) r = mkRule(CSSViewTransitionRule, { _body: it.body }, parent, sheet);
			break;
		case 'import': {
			if (!top || it.body !== null || rules.some(x => !(x instanceof CSSImportRule || x instanceof CSSLayerStatementRule))) break;
			const m = /^(?:url\(\s*["']?([^"')]*)["']?\s*\)|["']([^"']*)["'])\s*([\s\S]*)$/i.exec(pre);
			if (!m) break;
			let rest = m[3], layer = null, sup = null;
			const lm = /^layer(?:\(\s*([^)]*)\))?\s*/i.exec(rest);
			if (lm) { layer = lm[1] ? lm[1].trim() : ''; rest = rest.slice(lm[0].length); }
			const sm = /^supports\(([\s\S]*?)\)\s*(?=$|[^)])/i.exec(rest);
			if (sm) { sup = sm[1].trim(); rest = rest.slice(sm[0].length); }
			let href = m[1] !== undefined ? m[1] : m[2];
			try { href = new URL(href, (sheet && sheet.href) || N.url()).href; } catch (e) { /* (as written) */ }
			r = mkRule(CSSImportRule, { _href: href, _media: rest.trim(), _layer: layer, _supports: sup }, parent, sheet);
			break;
		}
		case 'namespace': {
			if (!top || it.body !== null) break;
			const m = /^(?:([-\w]+)\s+)?(?:url\(\s*["']?([^"')]*)["']?\s*\)|["']([^"']*)["'])\s*$/i.exec(pre);
			if (m) r = mkRule(CSSNamespaceRule, { _prefix: m[1] || '', _uri: m[2] !== undefined ? m[2] : m[3] }, parent, sheet);
			break;
		}
		default:
			break;		/* (@charset, an unknown at-rule: not in the CSSOM) */
		}
		if (r) rules.push(r);
	}
	return rules;
}

/* ---- the style sheets ---- */
class StyleSheet {
	constructor(key) { if (key !== RULE_OWN) throw new TypeError('Illegal constructor'); }
	get type() { return 'text/css'; }
	get href() { return this._href || null; }
	get ownerNode() { return this._owner || null; }
	get parentStyleSheet() { return null; }
	get title() { return this._owner ? N.attr(this._owner, 'title') : null; }
	get media() {
		if (!this._ml) Object.defineProperty(this, '_ml', { value: new MediaList(this._owner ? N.attr(this._owner, 'media') || '' : this._mediaText || '') });
		return this._ml;
	}
	get disabled() { return !!this._disabled; }
	set disabled(v) { this._disabled = !!v; }
}
G.StyleSheet = StyleSheet;

class CSSStyleSheet extends StyleSheet {
	constructor(options) {
		super(RULE_OWN);
		options = options || {};
		Object.defineProperty(this, '_constructed', { value: true });
		Object.defineProperty(this, '_src', { value: '', writable: true });
		this._mediaText = typeof options.media === 'string' ? options.media : '';
		this._disabled = !!options.disabled;
		this._href = null;
	}
	/* the text the rules come from */
	_text() {
		if (this._owner) {
			if (this._owner.localName === 'link') {
				const r = N.sheetText(this._owner);
				return r ? r[0] : '';
			}
			return this._owner.textContent || '';
		}
		return this._src;
	}
	get cssRules() {
		const t = this._text();
		if (!this._rl || (this._owner && this._owner.localName === 'style' && t !== this._seen)) {
			Object.defineProperty(this, '_rl', { value: new CSSRuleList(cssParseRules(t, this, this, 'sheet')), writable: true, configurable: true });
			Object.defineProperty(this, '_seen', { value: t, writable: true, configurable: true });
		}
		return this._rl;
	}
	get rules() { return this.cssRules; }
	get ownerRule() { return null; }
	insertRule(text, index) {
		const l = this.cssRules;
		index = index === undefined ? 0 : index >>> 0;
		if (index > l._r.length) throw new DOMException('index out of range', 'IndexSizeError');
		const r = cssParseRules(String(text), this, this, 'sheet');
		if (r.length !== 1) throw new DOMException('invalid rule: ' + text, 'SyntaxError');
		l._r.splice(index, 0, r[0]);
		l._fill();
		this._changed();
		return index;
	}
	deleteRule(index) {
		const l = this.cssRules;
		index >>>= 0;
		if (index >= l._r.length) throw new DOMException('index out of range', 'IndexSizeError');
		l._r[index]._sheet = null;
		l._r.splice(index, 1);
		l._fill();
		this._changed();
	}
	addRule(sel, style, index) {
		this.insertRule((sel || 'undefined') + ' { ' + (style || '') + ' }', index === undefined ? this.cssRules.length : index);
		return -1;
	}
	removeRule(index) { this.deleteRule(index === undefined ? 0 : index); }
	replaceSync(text) {
		if (!this._constructed) throw new DOMException('not a constructed sheet', 'NotAllowedError');
		this._src = String(text).replace(/@import[^;]*;/gi, '');
		this._rl = null;
		this._write();
	}
	replace(text) {
		try { this.replaceSync(text); return NativePromise.resolve(this); }
		catch (e) { return NativePromise.reject(e); }
	}
	/* a rule changed: the text again from the rules, written back */
	_changed() {
		if (!this._rl) return;
		const t = this._rl._r.map(r => r.cssText).join('\n');
		if (this._owner && this._owner.localName === 'style') {
			this._seen = t;
			this._owner.textContent = t;
		} else if (!this._owner) {
			this._src = t;
			this._write();
		}
	}
	/* a constructed sheet adopted by the document: its <style> (NetSurf styles with it) */
	_write() {
		for (const el of adoptedEls) if (el._onyxSheet === this) el.textContent = this._src;
	}
}
G.CSSStyleSheet = CSSStyleSheet;

const sheetOfEl = new WeakMap();
function cssSheetOf(el) {
	let s = sheetOfEl.get(el);
	if (!s) {
		s = Object.create(CSSStyleSheet.prototype);
		Object.defineProperty(s, '_owner', { value: el });
		Object.defineProperty(s, '_constructed', { value: false });
		if (el.localName === 'link') {
			const r = N.sheetText(el);
			Object.defineProperty(s, '_href', { value: r ? r[1] : el.href || null, writable: true });
		}
		sheetOfEl.set(el, s);
	}
	return s;
}

/* the sheets of the document, in its order: <style>s, <link rel=stylesheet>s loaded */
function documentSheets(doc) {
	const out = [];
	for (const el of doc.querySelectorAll('style, link')) {
		if (el.hasAttribute('data-onyx-adopted')) continue;
		if (el.localName === 'link') {
			if (!/(^|\s)stylesheet(\s|$)/i.test(N.attr(el, 'rel') || '') || !N.sheetText(el)) continue;
		}
		out.push(cssSheetOf(el));
	}
	return new StyleSheetList(out);
}

/* document.adoptedStyleSheets: each sheet realized as a <style data-onyx-adopted> at the end
 * of the <head> (NetSurf styles the page with it) */
const adoptedEls = new Set();
let adoptedList = [];
function setAdopted(list) {
	list = Array.from(list || []);
	for (const s of list)
		if (!(s instanceof CSSStyleSheet) || !s._constructed)
			throw new DOMException('not a constructed sheet', 'NotAllowedError');
	for (const el of adoptedEls) el.remove();
	adoptedEls.clear();
	adoptedList = list;
	const head = document.head || document.documentElement;
	if (!head) return;
	for (const s of list) {
		const el = document.createElement('style');
		el.setAttribute('data-onyx-adopted', '');
		el._onyxSheet = s;
		el.textContent = s._src;
		head.appendChild(el);
		adoptedEls.add(el);
	}
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

/* an element's style attribute, or (Onyx) a CSSOM rule's declarations (rule: its _decls /
 * _setDecls / _declValid) */
class CSSStyleDeclaration {
	constructor(el, rule) {
		Object.defineProperty(this, '_el', { value: el });
		Object.defineProperty(this, '_rule', { value: rule || null });
	}
	_map() {
		if (this._rule) return new Map(this._rule._decls());
		return this._el ? parseDecls(N.attr(this._el, 'style')) : new Map();
	}
	_valid(k, v) { return this._rule ? this._rule._declValid(k, v) : cssValid(k, v); }
	_write(map) {
		if (this._rule) { this._rule._setDecls(map); return; }
		if (!this._el) return;
		const s = serializeDecls(map);
		if (s) N.setAttr(this._el, 'style', s);
		else N.removeAttr(this._el, 'style');
	}
	get parentRule() { return this._rule; }
	get cssText() {
		if (this._rule) return serializeDecls(this._rule._decls());
		return this._el ? (N.attr(this._el, 'style') || '') : '';
	}
	set cssText(v) {
		if (this._rule) {
			const m = new Map();
			for (const [k, d] of parseDecls(String(v))) if (this._valid(k, d.v)) m.set(k, d);
			this._rule._setDecls(m);
			return;
		}
		if (this._el) N.setAttr(this._el, 'style', String(v));
	}
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
		else if (this._valid(k, String(v)))
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
	'getPropertyPriority', 'setProperty', 'removeProperty', '_el', '_rule', '_map', '_write',
	'_valid', 'parentRule', 'constructor']);

function styleProxy(el) { return styleProxyOf(new CSSStyleDeclaration(el)); }
function styleProxyOf(decl) {
	return new Proxy(decl, {
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
	for (let i = 0; i < c.classes.length; i++)	/* (Onyx: in C, no array per node) */
		if (!N.hasToken(e, 'class', c.classes[i])) return false;
	for (const a of c.attrs) {
		let v = N.attr(e, a.name);
		if (v === null) return false;
		if (!a.op) continue;
		let want = a.value;
		if (a.ci) { v = v.toLowerCase(); want = want.toLowerCase(); }
		switch (a.op) {
		case '=': if (v !== want) return false; break;
		case '~=': if (a.ci ? !v.split(/\s+/).includes(want) : !N.hasToken(e, a.name, want)) return false; break;
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
	/* (Onyx: read-only, read-write, defined: html5.js, N.internals.pseudo) */
	case 'placeholder-shown': return N.attr(e, 'placeholder') !== null && !e.value;
	case 'link': case 'any-link': return ['a', 'area', 'link'].includes(e.localName) && N.attr(e, 'href') !== null;
	case 'visited': case 'hover': case 'active': case 'focus-visible': case 'target-within':
		return false;
	case 'focus': return G.document.activeElement === e;
	case 'focus-within': { const a = G.document.activeElement; return !!a && e.contains(a); }
	case 'target': { const h = decodeURIComponent((G.location.hash || '').slice(1)); return !!h && N.attr(e, 'id') === h; }
	case 'lang': {
		for (let n = e; n && N.type(n) === ELEMENT_NODE; n = N.parent(n)) {
			const l = N.attr(n, 'lang');
			if (l !== null) return lower(l) === lower(p.arg) || lower(l).startsWith(lower(p.arg) + '-');
		}
		return false;
	}
	case 'dir': return lower(p.arg) === 'ltr';
	/* Onyx: the pseudo-classes html5.js knows (:valid, :invalid, :in-range, :open...) */
	default: return !!(N.internals && N.internals.pseudo && N.internals.pseudo(e, p.name, p.arg));
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
	/* (Onyx: an attribute's name as given: lower case for an HTML element, as is for an SVG
	 * one -- viewBox, preserveAspectRatio) */
	_an(name) { return lower(name); }
	getAttribute(name) { return N.attr(this, this._an(name)); }
	getAttributeNS(ns, name) { return N.attr(this, this._an(name)); }
	setAttribute(name, value) {
		const k = this._an(name);
		const old = observers.size ? N.attr(this, k) : null;
		N.setAttr(this, k, String(value));
		if (observers.size)
			queueMutation({ type: 'attributes', target: this, attributeName: k, oldValue: old,
				addedNodes: nodeList([]), removedNodes: nodeList([]) });
	}
	setAttributeNS(ns, name, value) { this.setAttribute(name.replace(/^.*:/, ''), value); }
	removeAttribute(name) {
		const k = this._an(name);
		const old = N.attr(this, k);
		if (old === null) return;
		N.removeAttr(this, k);
		if (observers.size)
			queueMutation({ type: 'attributes', target: this, attributeName: k, oldValue: old,
				addedNodes: nodeList([]), removedNodes: nodeList([]) });
	}
	removeAttributeNS(ns, name) { this.removeAttribute(name); }
	hasAttribute(name) { return N.attr(this, this._an(name)) !== null; }
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
		const v = N.attr(this, this._an(name));
		return v === null ? null : new Attr(this, this._an(name), v);
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
	getBoundingClientRect() { const r = N.rect(this, true); return new DOMRect(r[0], r[1], r[2], r[3]); }
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
	/* (Onyx: an element's own scroller's -- overflow: auto / scroll -- N.boxScroll) */
	get scrollWidth() {
		if (this === G.document.documentElement || this === G.document.body) return N.scroll()[4];
		return N.boxScroll(this)[2];
	}
	get scrollHeight() {
		if (this === G.document.documentElement || this === G.document.body) return N.scroll()[5];
		return N.boxScroll(this)[3];
	}
	get scrollTop() {
		return this === G.document.documentElement || this === G.document.body ? N.scroll()[1] : N.boxScroll(this)[1];
	}
	set scrollTop(v) {
		if (this === G.document.documentElement || this === G.document.body)
			N.scrollTo(N.scroll()[0], +v || 0);
		else
			N.boxScrollTo(this, N.boxScroll(this)[0], Math.round(+v || 0));
	}
	get scrollLeft() {
		return this === G.document.documentElement || this === G.document.body ? N.scroll()[0] : N.boxScroll(this)[0];
	}
	set scrollLeft(v) {
		if (this === G.document.documentElement || this === G.document.body)
			N.scrollTo(+v || 0, N.scroll()[1]);
		else
			N.boxScrollTo(this, Math.round(+v || 0), N.boxScroll(this)[1]);
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
	/* (Onyx: an element's scroller moved) */
	scroll(x, y) { this.scrollTo(x, y); }
	scrollTo(x, y) {
		const s = N.boxScroll(this);
		if (x !== null && typeof x === 'object') { y = x.top === undefined ? s[1] : x.top; x = x.left === undefined ? s[0] : x.left; }
		N.boxScrollTo(this, Math.round(+x || 0), Math.round(+y || 0));
	}
	scrollBy(x, y) {
		const s = N.boxScroll(this);
		if (x !== null && typeof x === 'object') { y = x.top || 0; x = x.left || 0; }
		N.boxScrollTo(this, s[0] + Math.round(+x || 0), s[1] + Math.round(+y || 0));
	}
	requestFullscreen() { return Promise.reject(new DOMException('no', 'NotAllowedError')); }
	attachShadow() { return this; }
	get shadowRoot() { return null; }
	animate(keyframes, options) { return elementAnimate(this, keyframes, options); }
	getAnimations() { return animationsOf(this); }
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

class DOMStringMap {}
G.DOMStringMap = DOMStringMap;

class HTMLElement extends Element {
	get dataset() {
		const el = this;
		/* (Onyx: an instance of DOMStringMap -- Facebook's loader checks) */
		return new Proxy(Object.create(DOMStringMap.prototype), {
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
		/* (Onyx: a text field's: the browser's caret in it too) */
		if (this.localName === 'input' || this.localName === 'textarea')
			N.focusControl(this);
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
	/* (Onyx: CSSOM View's x / y: the image's border box in the page) */
	get x() { return Math.round(this.getBoundingClientRect().left + (G.scrollX || 0)); }
	get y() { return Math.round(this.getBoundingClientRect().top + (G.scrollY || 0)); }
	/* (Onyx: the picture's state and natural size, NetSurf's for a displayed image; an image
	 * a script loads -- new Image() -- is canvas.js's) */
	_img() { return this.isConnected ? N.image(this) : null; }
	get complete() {
		if (!N.attr(this, 'src')) return true;
		const st = this._img();
		return st ? st[0] !== 0 : true;
	}
	get naturalWidth() { const st = this._img(); return st ? st[1] : 0; }
	get naturalHeight() { const st = this._img(); return st ? st[2] : 0; }
	get width() {
		const v = parseInt(N.attr(this, 'width'), 10);
		if (!isNaN(v)) return v;
		if (this.isConnected && N.boxed(this)) return N.rect(this)[2];
		return this.naturalWidth;
	}
	set width(v) { this.setAttribute('width', String(v)); }
	get height() {
		const v = parseInt(N.attr(this, 'height'), 10);
		if (!isNaN(v)) return v;
		if (this.isConnected && N.boxed(this)) return N.rect(this)[3];
		return this.naturalHeight;
	}
	set height(v) { this.setAttribute('height', String(v)); }
	get currentSrc() { return this.src; }
	get src() { const v = N.attr(this, 'src'); try { return v ? new URL(v, N.url()).href : ''; } catch (e) { return v || ''; } }
	set src(v) { this.setAttribute('src', v); }
	decode() {
		if (this.complete) return this.naturalWidth || !N.attr(this, 'src') ? Promise.resolve() :
			Promise.reject(new DOMException('The source image cannot be decoded.', 'EncodingError'));
		return new Promise((res, rej) => {
			const ok = () => { this.removeEventListener('error', ko); res(); };
			const ko = () => { this.removeEventListener('load', ok); rej(new DOMException('The source image cannot be decoded.', 'EncodingError')); };
			this.addEventListener('load', ok, { once: true });
			this.addEventListener('error', ko, { once: true });
		});
	}
}
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
	get sheet() {
		return /(^|\s)stylesheet(\s|$)/i.test(N.attr(this, 'rel') || '') && N.sheetText(this) ?
			cssSheetOf(this) : null;
	}
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
	/* (Onyx: net.js gives the frame's WindowProxy and, same-origin, its document) */
	get contentWindow() { return null; }
	get contentDocument() { return null; }
	get sandbox() { return new DOMTokenList(this, 'sandbox'); }
	set sandbox(v) { this.setAttribute('sandbox', String(v)); }
	getSVGDocument() { return null; }
});
reflectURL(G.HTMLIFrameElement.prototype, 'src');
for (const k of ['name', 'width', 'height', 'allow', 'loading', 'srcdoc', 'referrerPolicy',
		'scrolling', 'frameBorder', 'marginWidth', 'marginHeight', 'align', 'longDesc'])
	reflectString(G.HTMLIFrameElement.prototype, k);
reflectBool(G.HTMLIFrameElement.prototype, 'allowFullscreen');
reflectBool(G.HTMLIFrameElement.prototype, 'credentialless');
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
	_an(name) { return String(name); }
	get tagName() { return N.name(this); }
	get localName() { return N.name(this); }
	get namespaceURI() { return 'http://www.w3.org/2000/svg'; }
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
/* (the SVG namespace's classes: html5.js's TAGS['svg:*'], TAGS['svg:svg']) */

/* ---- Document ------------------------------------------------------------------------ */

class Document extends Node {
	constructor() { super(); }
	/* (Onyx: the Web Animations API) */
	get timeline() { return documentTimeline; }
	getAnimations() { return animationsOf(null); }
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
	get hidden() { return N.viewHidden(); }	/* (Onyx: the window minimised, elsewhere, covered) */
	get visibilityState() { return N.viewHidden() ? 'hidden' : 'visible'; }
	get scrollingElement() { return this.documentElement; }
	get forms() { return this.getElementsByTagName('form'); }
	get images() { return this.getElementsByTagName('img'); }
	get links() { return htmlCollection(this.querySelectorAll('a[href],area[href]')); }
	get scripts() { return this.getElementsByTagName('script'); }
	get styleSheets() { return documentSheets(this); }
	get adoptedStyleSheets() {
		/* (a live array: a push() adopts the sheet, as the ObservableArray does) */
		return new Proxy(adoptedList.slice(), {
			set(t, k, v) { t[k] = v; if (k !== 'length' || v < adoptedList.length) NativePromise.resolve().then(() => setAdopted(t)); return true; },
		});
	}
	set adoptedStyleSheets(v) { setAdopted(v); }
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
	createElementNS(ns, tag) {
		tag = String(tag);
		if (ns == null || ns === '' || ns === 'http://www.w3.org/1999/xhtml')
			return N.create(tag.replace(/^.*:/, '').toLowerCase());
		return N.create(tag, String(ns));	/* (Onyx: in its namespace: React's <svg>) */
	}
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
	createNodeIterator(root, what = 0xffffffff, filter = null) { return new NodeIterator(root, what, filter); }
	importNode(n, deep) { return N.clone(n, !!deep); }
	adoptNode(n) { const p = N.parent(n); if (p) p.removeChild(n); return n; }
	elementFromPoint(x, y) {
		/* (Onyx: the browser's hit test -- as painted, the hidden ones passed over) */
		if (this === G.document && N.hitNode) {
			x = +x; y = +y;
			if (!(x >= 0 && y >= 0 && x < G.innerWidth && y < G.innerHeight)) return null;
			const e = N.hitNode(Math.floor(x), Math.floor(y));
			if (e) return e;
		}
		let best = null;
		for (const e of N.descendants(this)) {
			const r = N.rect(e, true);
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

/* Onyx: TreeWalker and NodeIterator as the DOM standard walks them -- live, from the current
 * node (a snapshot of the root's nodes was taken: Lit sets currentNode to a template's
 * contents, outside its document root, and walked the document instead: its components'
 * slots kept their "name$lit$" marker names) */
function traversalFilter(w, n) {
	if (!(w.whatToShow & (1 << (N.type(n) - 1)))) return 3;	/* FILTER_SKIP */
	const f = w.filter;
	if (f === null || f === undefined) return 1;
	const r = typeof f === 'function' ? f(n) : f.acceptNode(n);
	return +r;
}
class TreeWalker {
	constructor(root, what, filter) {
		this.root = root;
		this.whatToShow = what >>> 0;
		this.filter = filter === undefined ? null : filter;
		this.currentNode = root;
	}
	parentNode() {
		let node = this.currentNode;
		while (node !== null && node !== this.root) {
			node = N.parent(node);
			if (node !== null && traversalFilter(this, node) === 1) {
				this.currentNode = node;
				return node;
			}
		}
		return null;
	}
	_children(first) {
		let node = first ? N.first(this.currentNode) : N.last(this.currentNode);
		while (node !== null) {
			const r = traversalFilter(this, node);
			if (r === 1) {
				this.currentNode = node;
				return node;
			}
			if (r === 3) {
				const child = first ? N.first(node) : N.last(node);
				if (child !== null) {
					node = child;
					continue;
				}
			}
			for (;;) {
				const sib = first ? N.next(node) : N.prev(node);
				if (sib !== null) {
					node = sib;
					break;
				}
				const parent = N.parent(node);
				if (parent === null || parent === this.root || parent === this.currentNode)
					return null;
				node = parent;
			}
		}
		return null;
	}
	firstChild() { return this._children(true); }
	lastChild() { return this._children(false); }
	_siblings(next) {
		let node = this.currentNode;
		if (node === this.root) return null;
		for (;;) {
			let sib = next ? N.next(node) : N.prev(node);
			while (sib !== null) {
				node = sib;
				const r = traversalFilter(this, node);
				if (r === 1) {
					this.currentNode = node;
					return node;
				}
				sib = next ? N.first(node) : N.last(node);
				if (r === 2 || sib === null) sib = next ? N.next(node) : N.prev(node);
			}
			node = N.parent(node);
			if (node === null || node === this.root) return null;
			if (traversalFilter(this, node) === 1) return null;
		}
	}
	nextSibling() { return this._siblings(true); }
	previousSibling() { return this._siblings(false); }
	previousNode() {
		let node = this.currentNode;
		while (node !== this.root) {
			let sib = N.prev(node);
			while (sib !== null) {
				node = sib;
				let r = traversalFilter(this, node);
				while (r !== 2 && N.first(node) !== null) {
					node = N.last(node);
					r = traversalFilter(this, node);
				}
				if (r === 1) {
					this.currentNode = node;
					return node;
				}
				sib = N.prev(node);
			}
			const parent = N.parent(node);
			if (parent === null) return null;
			node = parent;
			if (traversalFilter(this, node) === 1) {
				this.currentNode = node;
				return node;
			}
		}
		return null;
	}
	nextNode() {
		let node = this.currentNode, r = 1;
		for (;;) {
			while (r !== 2 && N.first(node) !== null) {
				node = N.first(node);
				r = traversalFilter(this, node);
				if (r === 1) {
					this.currentNode = node;
					return node;
				}
			}
			let temp = node, found = false;
			while (temp !== null) {
				if (temp === this.root) return null;
				const sib = N.next(temp);
				if (sib !== null) {
					node = sib;
					found = true;
					break;
				}
				temp = N.parent(temp);
			}
			if (!found) return null;	/* (the end of a tree the root is not in) */
			r = traversalFilter(this, node);
			if (r === 1) {
				this.currentNode = node;
				return node;
			}
		}
	}
}
/* the node after n in root's subtree in tree order, and the one before (NodeIterator) */
function traversalFollowing(n, root) {
	const c = N.first(n);
	if (c !== null) return c;
	for (let t = n; t !== null && t !== root; t = N.parent(t)) {
		const s = N.next(t);
		if (s !== null) return s;
	}
	return null;
}
function traversalPreceding(n, root) {
	if (n === root) return null;
	let s = N.prev(n);
	if (s === null) return N.parent(n);
	for (let c; (c = N.last(s)) !== null;) s = c;
	return s;
}
class NodeIterator {
	constructor(root, what, filter) {
		this.root = root;
		this.whatToShow = what >>> 0;
		this.filter = filter === undefined ? null : filter;
		this.referenceNode = root;
		this.pointerBeforeReferenceNode = true;
	}
	_traverse(next) {
		let node = this.referenceNode, before = this.pointerBeforeReferenceNode;
		for (;;) {
			if (next) {
				if (!before) {
					node = traversalFollowing(node, this.root);
					if (node === null) return null;
				} else {
					before = false;
				}
			} else if (before) {
				node = traversalPreceding(node, this.root);
				if (node === null) return null;
			} else {
				before = true;
			}
			if (traversalFilter(this, node) === 1) break;
		}
		this.referenceNode = node;
		this.pointerBeforeReferenceNode = before;
		return node;
	}
	nextNode() { return this._traverse(true); }
	previousNode() { return this._traverse(false); }
	detach() {}
}
G.TreeWalker = TreeWalker;
G.NodeIterator = NodeIterator;
G.NodeFilter = { FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3, SHOW_ALL: 0xffffffff,
	SHOW_ELEMENT: 1, SHOW_ATTRIBUTE: 2, SHOW_TEXT: 4, SHOW_CDATA_SECTION: 8,
	SHOW_PROCESSING_INSTRUCTION: 64, SHOW_COMMENT: 128, SHOW_DOCUMENT: 256,
	SHOW_DOCUMENT_TYPE: 512, SHOW_DOCUMENT_FRAGMENT: 1024 };

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
	get vendor() { return /Chrome\//.test(N.userAgent()) ? 'Google Inc.' : ''; },	/* (Onyx: NetSurf's UA: none) */
	vendorSub: '',
	get platform() { return /Windows/.test(N.userAgent()) ? 'Win32' : 'Linux aarch64'; },	/* (Onyx: "Desktop site") */
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

/* (Onyx: a Screen, its attributes on the prototype) */
class Screen extends EventTarget {
	get width() { return N.scroll()[2]; }
	get height() { return N.scroll()[3]; }
	get availWidth() { return N.scroll()[2]; }
	get availHeight() { return N.scroll()[3]; }
	get availLeft() { return 0; }
	get availTop() { return 0; }
	get colorDepth() { return 24; }
	get pixelDepth() { return 24; }
	get isExtended() { return false; }
	get orientation() { return SCREEN_ORIENTATION; }
}
const SCREEN_ORIENTATION = { type: 'landscape-primary', angle: 0, addEventListener() {}, removeEventListener() {} };
G.Screen = Screen;
const screen = new Screen();

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

/* ---- animations (Onyx: html/onyx_anim.c) --------------------------------------------------
 * requestAnimationFrame paced by the content's frames (N.frame asks for one, "onyx:frame"
 * runs the callbacks with the frame's time); the CSS transitions' and animations' events
 * ("onyx:anim"); the Web Animations API on the same engine: element.animate() makes a
 * script's animation there (N.animate), an Animation reads its state (N.animInfo) and
 * controls it (N.animCtl); getAnimations() lists the transitions and CSS animations too. */
let rafId = 0;
const rafs = new Map();
function requestAnimationFrame(cb) {
	const id = ++rafId;
	rafs.set(id, cb);
	N.frame();
	return id;
}
function cancelAnimationFrame(id) { rafs.delete(id); }
function runFrames(time) {
	if (!rafs.size)
		return;
	const ts = time - startTime;
	const list = [...rafs.values()];
	rafs.clear();
	for (const cb of list) {
		try { cb(ts); } catch (e) { report(e); }
	}
}

/* (AnimationEvent / TransitionEvent: defined with the other events, above) */
class AnimationPlaybackEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		this.currentTime = init.currentTime === undefined ? null : init.currentTime;
		this.timelineTime = init.timelineTime === undefined ? null : init.timelineTime;
	}
}

class AnimationTimeline {
	get currentTime() { return performance.now(); }
	get duration() { return null; }
}
class DocumentTimeline extends AnimationTimeline {
	constructor(opts) {
		super();
		Object.defineProperty(this, '_origin', { value: (opts && +opts.originTime) || 0 });
	}
	get currentTime() { return performance.now() - this._origin; }
}
const documentTimeline = new DocumentTimeline();

const ANIM_DIRS = ['normal', 'reverse', 'alternate', 'alternate-reverse'];
const ANIM_FILLS = ['none', 'forwards', 'backwards', 'both'];
const ANIMS = new Map();		/* id -> its Animation (those the scripts have) */

/* an easing as the engine reads it (libcss's canonical spelling) */
function easingText(e) {
	let s = String(e === undefined ? 'linear' : e).trim().toLowerCase();
	s = s.replace(/\s*,\s*/g, ',').replace(/\(\s+/g, '(').replace(/\s+\)/g, ')').replace(/\s+/g, ' ');
	s = s.replace(/^steps\((\d+),(start|end)\)$/, (m, n, p) => 'steps(' + n + ',jump-' + p + ')');
	if (!/^(linear|ease|ease-in|ease-out|ease-in-out|step-start|step-end|cubic-bezier\([^)]*\)|steps\([^)]*\)|linear\([^)]*\))$/.test(s))
		throw new TypeError("Failed to parse easing '" + e + "'");
	return s;
}

function cssName(p) {
	if (p === 'cssFloat') return 'float';
	if (p === 'cssOffset') return 'offset';
	return p.startsWith('--') ? p : kebab(p);
}

/* keyframes (the array or the property-indexed form) into [{offset, easing, obj, css}] */
function normalizeKeyframes(k) {
	let frames = [];
	if (k == null)
		return frames;
	if (Array.isArray(k) || (typeof k[Symbol.iterator] === 'function' && typeof k !== 'string')) {
		for (const f of k) {
			const obj = {};
			for (const p of Object.keys(f))
				if (p !== 'offset' && p !== 'easing' && p !== 'composite')
					obj[p] = f[p];
			frames.push({ offset: f.offset == null ? null : +f.offset,
				easing: f.easing === undefined ? 'linear' : easingText(f.easing), obj });
		}
	} else {
		/* { opacity: [0, 1], transform: [...] }: the values spread evenly */
		const props = Object.keys(k).filter(p => p !== 'offset' && p !== 'easing' && p !== 'composite');
		let n = 0;
		for (const p of props)
			n = Math.max(n, Array.isArray(k[p]) ? k[p].length : 1);
		for (let i = 0; i < n; i++)
			frames.push({ offset: null, easing: 'linear', obj: {} });
		for (const p of props) {
			const vals = Array.isArray(k[p]) ? k[p] : [k[p]];
			vals.forEach((v, i) => {
				const at = vals.length === 1 ? n - 1 : Math.round(i * (n - 1) / (vals.length - 1));
				frames[at].obj[p] = v;
			});
		}
		const offs = Array.isArray(k.offset) ? k.offset : k.offset != null ? [k.offset] : [];
		offs.forEach((o, i) => { if (frames[i] && o != null) frames[i].offset = +o; });
		const eas = Array.isArray(k.easing) ? k.easing : k.easing != null ? [k.easing] : [];
		frames.forEach((f, i) => { if (eas.length) f.easing = easingText(eas[i % eas.length]); });
	}
	/* the missing offsets: 0, 1 at the ends, evenly between (a single one: 1) */
	if (frames.length === 1 && frames[0].offset == null)
		frames[0].offset = 1;
	else if (frames.length > 1) {
		if (frames[0].offset == null) frames[0].offset = 0;
		if (frames[frames.length - 1].offset == null) frames[frames.length - 1].offset = 1;
		for (let i = 1; i < frames.length - 1; i++) {
			if (frames[i].offset != null)
				continue;
			let j = i;
			while (frames[j].offset == null) j++;
			const a = frames[i - 1].offset, b = frames[j].offset;
			for (let m = i; m < j; m++)
				frames[m].offset = a + (b - a) * (m - i + 1) / (j - i + 1);
			i = j;
		}
	}
	for (let i = 0; i < frames.length; i++) {
		const f = frames[i];
		if (f.offset < 0 || f.offset > 1 || (i > 0 && f.offset < frames[i - 1].offset))
			throw new TypeError('Offsets must be monotonically non-decreasing and in [0, 1]');
		f.css = Object.keys(f.obj).map(p => cssName(p) + ':' + String(f.obj[p])).join(';');
	}
	return frames;
}

function normalizeTiming(o) {
	const t = { delay: 0, endDelay: 0, duration: 0, durationRaw: 'auto', iterations: 1,
		iterationStart: 0, direction: 0, fill: 0, fillName: 'auto', easing: 'linear',
		playbackRate: 1, id: '' };
	if (typeof o === 'number' || (o != null && typeof o !== 'object')) {
		t.duration = +o || 0;
		t.durationRaw = t.duration;
	} else if (o) {
		if (o.duration !== undefined && o.duration !== 'auto') {
			t.duration = +o.duration;
			t.durationRaw = t.duration;
			if (!(t.duration >= 0))
				throw new TypeError('Invalid duration');
		}
		if (o.delay !== undefined) t.delay = +o.delay || 0;
		if (o.endDelay !== undefined) t.endDelay = +o.endDelay || 0;
		if (o.iterations !== undefined) {
			t.iterations = +o.iterations;
			if (!(t.iterations >= 0))
				throw new TypeError('Invalid iterations');
		}
		if (o.iterationStart !== undefined) t.iterationStart = +o.iterationStart || 0;
		if (o.direction !== undefined) t.direction = Math.max(0, ANIM_DIRS.indexOf(o.direction));
		if (o.fill !== undefined) {
			t.fillName = String(o.fill);
			t.fill = Math.max(0, ANIM_FILLS.indexOf(t.fillName));
		}
		if (o.easing !== undefined) t.easing = easingText(o.easing);
		if (o.playbackRate !== undefined) t.playbackRate = +o.playbackRate;
		if (o.id !== undefined) t.id = String(o.id);
	}
	return t;
}

class AnimationEffect {
	getTiming() {
		const t = this._timing;
		return { delay: t.delay, endDelay: t.endDelay, fill: t.fillName,
			iterationStart: t.iterationStart, iterations: t.iterations,
			duration: t.durationRaw, direction: ANIM_DIRS[t.direction], easing: t.easing };
	}
	getComputedTiming() {
		const t = this._timing, a = this._anim;
		const i = a && a._id ? N.animInfo(a._id) : null;
		const active = t.duration * t.iterations;
		return Object.assign(this.getTiming(), {
			fill: ANIM_FILLS[t.fill], duration: t.duration,
			activeDuration: active, endTime: Math.max(0, t.delay + active + t.endDelay),
			localTime: i && !isNaN(i[0]) ? i[0] : null,
			progress: i && !isNaN(i[4]) ? i[4] : null,
			currentIteration: i && !isNaN(i[5]) ? i[5] : null });
	}
	updateTiming(o) {
		const cur = this.getTiming();
		this._timing = normalizeTiming(Object.assign(cur, o || {}));
		if (this._anim)
			this._anim._remake();
	}
}
class KeyframeEffect extends AnimationEffect {
	constructor(target, keyframes, options) {
		super();
		if (target instanceof KeyframeEffect) {
			const src = target;
			this.target = src.target;
			this._frames = src._frames.slice();
			this._timing = Object.assign({}, src._timing);
		} else {
			this.target = target || null;
			this._frames = normalizeKeyframes(keyframes);
			this._timing = normalizeTiming(options);
		}
		this._anim = null;
		this.composite = 'replace';
		this.iterationComposite = 'replace';
		this.pseudoElement = null;
	}
	getKeyframes() {
		return this._frames.map(f => Object.assign({}, f.obj, { offset: f.offset,
			computedOffset: f.offset, easing: f.easing, composite: 'auto' }));
	}
	setKeyframes(k) {
		this._frames = normalizeKeyframes(k);
		if (this._anim)
			this._anim._remake();
	}
}

class Animation extends EventTarget {
	constructor(effect = null, timeline) {
		super();
		Object.defineProperty(this, '_id', { value: 0, writable: true });
		this._effect = effect;
		if (effect) effect._anim = this;
		this.timeline = timeline === undefined ? documentTimeline : timeline;
		this.id = effect && effect._timing ? effect._timing.id : '';
		this.onfinish = null;
		this.oncancel = null;
		this.onremove = null;
		this._rate = effect && effect._timing ? effect._timing.playbackRate : 1;
		this._newPromises();
	}
	_newPromises() {
		this._finished = new Promise((ok, no) => { this._fok = ok; this._fno = no; });
		this._finished.catch(() => {});	/* (a cancel: not an unhandled rejection) */
		this._ready = Promise.resolve(this);
	}
	_make(hold) {
		const e = this._effect;
		if (!e || !e.target || !e._frames.length)
			return false;
		const t = e._timing;
		const id = N.animate(e.target, e._frames.map(f => f.css), e._frames.map(f => f.offset),
			e._frames.map(f => f.easing), { delay: t.delay, endDelay: t.endDelay,
			duration: t.duration, iterations: t.iterations, iterationStart: t.iterationStart,
			direction: t.direction, fill: t.fill, easing: t.easing, playbackRate: this._rate });
		if (!id)
			return false;
		this._id = id;
		ANIMS.set(id, this);
		if (hold !== undefined)
			N.animCtl(id, 5, hold);
		return true;
	}
	_remake() {
		/* (new keyframes or timing: made again at the same time) */
		if (!this._id)
			return;
		const i = N.animInfo(this._id);
		const t = i ? i[0] : 0, state = i ? i[7] : 'idle';
		if (state === 'idle')
			return;
		N.animCtl(this._id, 2);
		ANIMS.delete(this._id);
		this._id = 0;
		this._make(isNaN(t) ? 0 : t);
		if (state === 'paused')
			N.animCtl(this._id, 1);
	}
	_info() {
		/* (a finished animation without a fill is gone from the engine: its state kept
		 * here, play() makes it again) */
		const i = this._id ? N.animInfo(this._id) : null;
		if (!i && this._id) {
			ANIMS.delete(this._id);
			this._id = 0;
		}
		return i;
	}
	get effect() { return this._effect; }
	set effect(e) { this._effect = e; if (e) e._anim = this; this._remake(); }
	get playState() { const i = this._info(); return i ? i[7] : (this._state || 'idle'); }
	get pending() { return false; }
	get replaceState() { return this._removed ? 'removed' : 'active'; }
	get currentTime() {
		const i = this._info();
		if (i) return !isNaN(i[0]) ? i[0] : null;
		return this._state === 'finished' ? this._held : null;
	}
	set currentTime(v) {
		if (v === null)
			return;
		if (!this._id && !this._make())
			return;
		N.animCtl(this._id, 5, +v);
		N.animCtl(this._id, 1);
	}
	get startTime() { const i = this._info(); return i && !isNaN(i[1]) ? i[1] - startTime : null; }
	set startTime(v) {
		if (v === null)
			return;
		if (!this._id && !this._make())
			return;
		N.animCtl(this._id, 7, +v + startTime);
	}
	get playbackRate() { const i = this._info(); return i ? i[2] : this._rate; }
	set playbackRate(v) { this._rate = +v; if (this._id) N.animCtl(this._id, 6, +v); }
	updatePlaybackRate(v) { this.playbackRate = v; }
	get finished() { return this._finished; }
	get ready() { return this._ready; }
	play() {
		if (!this._id || !this._info()) {
			if (this._state === 'finished')
				this._newPromises();
			this._state = '';
			this._make();
			return;
		}
		if (this.playState === 'finished' || this.playState === 'idle')
			this._newPromises();
		N.animCtl(this._id, 0);
	}
	pause() {
		if (!this._id && !this._make())
			return;
		N.animCtl(this._id, 1);
	}
	cancel() {
		if (this._id && this._info() && this.playState !== 'idle')
			N.animCtl(this._id, 2);
		else if (this._state === 'finished') {
			this._state = 'idle';
			this._onCancel();
		}
	}
	finish() {
		if (!this._id && !this._make())
			return;
		if (!N.animCtl(this._id, 3) || this.effect && this.effect._timing &&
		    this.effect._timing.iterations === Infinity && this.playbackRate > 0)
			throw new DOMException('Cannot finish an infinite animation', 'InvalidStateError');
	}
	reverse() {
		if (!this._info() && this._state === 'finished') {
			/* (gone at its end: made again, playing backwards from there) */
			this._rate = -this._rate;
			this._state = '';
			this._newPromises();
			this._make();
			return;
		}
		if (!this._id && !this._make())
			return;
		if (this.playState === 'finished')
			this._newPromises();
		N.animCtl(this._id, 4);
	}
	persist() {}
	commitStyles() {
		const e = this._effect;
		if (!e || !e.target || !e.target.style)
			return;
		const cs = getComputedStyle(e.target);
		const props = new Set();
		for (const f of e._frames)
			for (const p of Object.keys(f.obj))
				props.add(cssName(p));
		for (const p of props)
			e.target.style.setProperty(p, cs.getPropertyValue(p));
	}
	_onFinish(held) {
		this._state = 'finished';
		this._held = held;
		const ok = this._fok;
		ok(this);
		const ev = new AnimationPlaybackEvent('finish', { currentTime: this.currentTime,
			timelineTime: documentTimeline.currentTime });
		if (typeof this.onfinish === 'function')
			try { this.onfinish.call(this, ev); } catch (e) { report(e); }
		dispatch(this, ev);
	}
	_onCancel() {
		const no = this._fno;
		const err = new DOMException('The user aborted a request.', 'AbortError');
		this._newPromises();
		no(err);
		const ev = new AnimationPlaybackEvent('cancel', { timelineTime: documentTimeline.currentTime });
		if (typeof this.oncancel === 'function')
			try { this.oncancel.call(this, ev); } catch (e) { report(e); }
		dispatch(this, ev);
		ANIMS.delete(this._id);
	}
}
class CSSAnimation extends Animation {
	get animationName() { const i = this._info(); return i ? i[8] : this._name; }
}
class CSSTransition extends Animation {
	get transitionProperty() { const i = this._info(); return i ? i[8] : this._name; }
}

/* the Animation of an engine's id: the one the scripts have, else one made for it */
function animationFor(id) {
	let a = ANIMS.get(id);
	if (a)
		return a;
	const i = N.animInfo(id);
	if (!i)
		return null;
	const effect = new KeyframeEffect(i[9], [], { delay: i[10], duration: i[11],
		iterations: i[12], endDelay: i[13], direction: ANIM_DIRS[i[14]] || 'normal',
		fill: ANIM_FILLS[i[15]] || 'none' });
	a = new (i[6] === 0 ? CSSTransition : i[6] === 1 ? CSSAnimation : Animation)(effect);
	a._id = id;
	a._name = i[8];
	ANIMS.set(id, a);
	return a;
}

function animationsOf(target) {
	return N.animList(target).map(animationFor).filter(a => a && (a.playState !== 'finished' ||
		(a.effect && a.effect._timing && (a.effect._timing.fill & 1))));
}

function elementAnimate(el, keyframes, options) {
	const effect = new KeyframeEffect(el, keyframes, options);
	const a = new Animation(effect);
	a._make();
	return a;
}

/* the engine's news: an event on an element, or an Animation's finish / cancel */
function animEvent(target, init) {
	const t = init.type;
	if (t === 'finish' || t === 'cancel') {
		const a = ANIMS.get(init.id);
		if (a)
			t === 'finish' ? a._onFinish(init.elapsed) : a._onCancel();
		return;
	}
	if (t === 'remove') {
		const a = ANIMS.get(init.id);
		if (a) {
			a._removed = true;
			ANIMS.delete(init.id);
			a._id = 0;
			a._state = 'finished';
			const ev = new AnimationPlaybackEvent('remove', { timelineTime: documentTimeline.currentTime });
			if (typeof a.onremove === 'function')
				try { a.onremove.call(a, ev); } catch (e) { report(e); }
			dispatch(a, ev);
		}
		return;
	}
	if (!(target instanceof Element))
		return;
	const ev = t.startsWith('transition') ?
		new TransitionEvent(t, { bubbles: true, cancelable: t === 'transitionend',
			propertyName: init.name, elapsedTime: init.elapsed }) :
		new AnimationEvent(t, { bubbles: true, animationName: init.name,
			elapsedTime: init.elapsed });
	ev.isTrusted = true;
	dispatch(target, ev);
}

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
/* Onyx: a media query list as libcss reads and evaluates it (N.mediaMatch: the page's
 * viewport; libcss's range syntax, aspect-ratio, orientation, hover...): { media, matches } --
 * media its text with "not all" for a query libcss could not read, as a browser serializes
 * it. mediaMatches above without a page's style context. */
const MEDIA_INFO = new Map();
function mediaInfo(q) {
	q = String(q);
	if (q.trim() === '') return { media: '', matches: true };
	const key = q + '\u0000' + G.innerWidth + 'x' + G.innerHeight;
	let info = MEDIA_INFO.get(key);
	if (info) return info;
	const r = N.mediaMatch(q, G.innerWidth, G.innerHeight);
	if (r === null) return { media: q, matches: mediaMatches(q) };
	const parts = [];
	let depth = 0, start = 0;
	for (let i = 0; i <= q.length; i++) {
		const ch = q[i];
		if (ch === '(' || ch === '[' || ch === '{') depth++;
		else if (ch === ')' || ch === ']' || ch === '}') depth--;
		else if (i === q.length || (ch === ',' && depth <= 0)) {
			parts.push(q.slice(start, i).trim().replace(/\s+/g, ' '));
			start = i + 1;
		}
	}
	const media = parts.map((p, i) => i >= r[1] || ((r[2] >>> i) & 1) ? 'not all' : p).join(', ');
	info = { media, matches: r[0] };
	if (MEDIA_INFO.size > 256) MEDIA_INFO.clear();
	MEDIA_INFO.set(key, info);
	return info;
}
const mediaLists = new Set();
class MediaQueryList extends EventTarget {
	constructor(q) {
		super();
		Object.defineProperty(this, '_q', { value: String(q) });
		Object.defineProperty(this, '_last', { value: mediaInfo(q).matches, writable: true });
		Object.defineProperty(this, '_oc', { value: null, writable: true });
	}
	get media() { return mediaInfo(this._q).media; }
	get matches() { return mediaInfo(this._q).matches; }
	get onchange() { return this._oc; }
	set onchange(fn) { this._oc = typeof fn === 'function' ? fn : null; }
	addListener(fn) { this.addEventListener('change', fn); mediaLists.add(this); }
	removeListener(fn) { this.removeEventListener('change', fn); }
}
class MediaQueryListEvent extends Event {
	constructor(type, init = {}) {
		super(type, init);
		Object.defineProperty(this, '_m', { value: { media: String(init.media ?? ''), matches: !!init.matches } });
	}
	get media() { return this._m.media; }
	get matches() { return this._m.matches; }
}
G.MediaQueryListEvent = MediaQueryListEvent;
function matchMedia(q) { const m = new MediaQueryList(q); mediaLists.add(m); return m; }
G.MediaQueryList = MediaQueryList;

function checkMedia() {
	for (const m of mediaLists) {
		const now = mediaInfo(m._q).matches;
		if (now !== m._last) {
			m._last = now;
			const ev = new MediaQueryListEvent('change', { matches: now, media: m.media });
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
	/* (Onyx: the proxy's target is an empty object -- the Storage's own non-configurable
	 * fields left out of ownKeys broke the Proxy invariants: Object.keys(localStorage)
	 * threw "target property must be present in proxy ownKeys" on Facebook) */
	const st = new Storage(origin);
	return new Proxy(Object.create(Storage.prototype), {
		get(t, p) {
			if (p in st || typeof p !== 'string') { const v = st[p]; return typeof v === 'function' ? v.bind(st) : v; }
			return st.getItem(p) ?? undefined;
		},
		set(t, p, v) { st.setItem(p, v); return true; },
		has(t, p) { return p in st || (typeof p === 'string' && st.getItem(p) !== null); },
		deleteProperty(t, p) { st.removeItem(p); return true; },
		ownKeys(t) { return [...st._m.keys()]; },
		getOwnPropertyDescriptor(t, p) { const v = typeof p === 'string' ? st.getItem(p) : null; return v === null ? undefined : { value: v, enumerable: true, configurable: true, writable: true }; },
		defineProperty(t, p, d) { if ('value' in d) st.setItem(p, d.value); return true; },
	});
}
G.Storage = Storage;

/* console (Onyx: its arguments made text only when the log is read -- N.logOn -- and an
 * object's text bounded hard: JSON's, but 40 values, 4 levels and 160 characters at most, and
 * no toJSON() but a Date's. Vue's development build hands console.warn whole component trees
 * (reactive proxies: each value read is a tracked get): browserscore.dev's 1600 warnings cost
 * seconds with the log on at 2000 values each (and a minute before any bound); the kmsg line
 * is cut at ~128 characters anyway. A string argument is kept whole: nothing to format.) */
const FMT_CUT = {};
const FMT_VALUES = 40, FMT_DEPTH = 4, FMT_CHARS = 160, FMT_LINE = 256;
function fmtJSON(a, chars) {
	let n = 0, left = chars;
	const stack = [];
	const room = s => { if ((left -= s.length) < 0) throw FMT_CUT; return s; };
	const val = (v, inArray) => {
		if (++n > FMT_VALUES) throw FMT_CUT;
		if (v instanceof Date) v = v.toJSON();
		switch (typeof v) {
		case 'string': return room(JSON.stringify(v.length > left - 2 ? v.slice(0, Math.max(0, left - 6)) + '\u2026' : v));
		case 'number': return room(isFinite(v) ? String(v) : 'null');
		case 'boolean': return room(String(v));
		case 'bigint': throw new TypeError('BigInt');
		case 'undefined': case 'function': case 'symbol': return inArray ? room('null') : undefined;
		}
		if (v === null) return room('null');
		if (stack.includes(v)) throw new TypeError('cyclic');
		if (stack.length >= FMT_DEPTH) throw FMT_CUT;
		room('{}');
		stack.push(v);
		let out;
		if (Array.isArray(v)) {
			const items = [];
			for (let i = 0; i < v.length; i++) items.push(val(v[i], true));
			out = '[' + items.join(',') + ']';
		} else {
			const items = [];
			for (const k of Object.keys(v)) {
				const s = val(v[k], false);
				if (s !== undefined) items.push(room(JSON.stringify(k) + ':') + s);
			}
			out = '{' + items.join(',') + '}';
		}
		stack.pop();
		return out;
	};
	return val(a, false);
}
function fmtName(a) {
	let c;
	try { c = a && a.constructor && a.constructor.name; } catch (e) {}
	return '[' + (typeof c === 'string' && c ? c : 'object') + ' ...]';
}
/* (a line's objects formatted while it is under FMT_LINE characters, the others named: Vue's
 * warning text alone is longer than what kmsg shows) */
function fmt(args) {
	let len = 0;
	return args.map(a => {
		let s;
		if (typeof a === 'string') s = a;
		else if (a instanceof Error) s = a + (a.stack ? '\n' + a.stack : '');
		else if (a instanceof Node) s = '<' + (a.nodeName || 'node') + '>';
		else if (len >= FMT_LINE && a !== null && (typeof a === 'object' || typeof a === 'function')) s = fmtName(a);
		else {
			try {
				s = fmtJSON(a, Math.max(16, Math.min(FMT_CHARS, FMT_LINE - len)));
			} catch (e) {
				if (e === FMT_CUT) s = fmtName(a);
				else try { s = String(a); } catch (e2) { s = fmtName(a); }
			}
		}
		if (s === undefined) s = typeof a === 'function' ? 'function ' + (a.name || '') + '()' : String(a);
		len += s.length + 1;
		return s;
	}).join(' ');
}
const counts = new Map(), times = new Map();
const clog = (pre, a) => { if (N.logOn()) N.log(pre + fmt(a)); };
const console = {
	log: (...a) => clog('', a),
	info: (...a) => clog('', a),
	debug: (...a) => clog('', a),
	warn: (...a) => clog('warning: ', a),
	error: (...a) => clog('error: ', a),
	trace: (...a) => clog('trace: ', a),
	dir: (...a) => clog('', a),
	dirxml: (...a) => clog('', a),
	table: (...a) => clog('', a),
	group: (...a) => clog('', a),
	groupCollapsed: (...a) => clog('', a),
	groupEnd() {},
	assert: (c, ...a) => { if (!c) clog('assertion failed: ', a); },
	count: (l = 'default') => { counts.set(l, (counts.get(l) || 0) + 1); clog(l + ': ' + counts.get(l), []); },
	countReset: (l = 'default') => counts.delete(l),
	time: (l = 'default') => times.set(l, N.now()),
	timeEnd: (l = 'default') => { clog(l + ': ' + (N.now() - (times.get(l) || N.now())) + 'ms', []); times.delete(l); },
	timeLog: (l = 'default') => clog(l + ': ' + (N.now() - (times.get(l) || N.now())) + 'ms', []),
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
/* Onyx: URL and URLSearchParams as the WHATWG URL Standard says (the basic URL parser's state
 * machine, its host parser -- IPv4 numbers, IPv6, IDN labels to punycode --, the percent-encode
 * sets, the setters through the state overrides). The WPT's url tests guided it
 * (tools/tests/netsurf/urltest.sh). */
const SPECIAL = { 'ftp': 21, 'file': null, 'http': 80, 'https': 443, 'ws': 80, 'wss': 443 };
const isSpecial = s => Object.prototype.hasOwnProperty.call(SPECIAL, s);
const ALPHA = c => (c >= 0x41 && c <= 0x5a) || (c >= 0x61 && c <= 0x7a);
const DIGIT = c => c >= 0x30 && c <= 0x39;
const HEXD = c => DIGIT(c) || (c >= 0x41 && c <= 0x46) || (c >= 0x61 && c <= 0x66);
const PE_C0 = c => c < 0x20 || c > 0x7e;
const PE_FRAG = c => PE_C0(c) || c === 0x20 || c === 0x22 || c === 0x3c || c === 0x3e || c === 0x60;
const PE_QUERY = c => PE_C0(c) || c === 0x20 || c === 0x22 || c === 0x23 || c === 0x3c || c === 0x3e;
const PE_SQUERY = c => PE_QUERY(c) || c === 0x27;
const PE_PATH = c => PE_QUERY(c) || c === 0x3f || c === 0x5e || c === 0x60 || c === 0x7b || c === 0x7d;
const PE_USER = c => PE_PATH(c) || c === 0x2f || c === 0x3a || c === 0x3b || c === 0x3d || c === 0x40 ||
	(c >= 0x5b && c <= 0x5d) || c === 0x7c;
const PE_COMP = c => PE_USER(c) || (c >= 0x24 && c <= 0x26) || c === 0x2b || c === 0x2c;
const PE_FORM = c => PE_COMP(c) || c === 0x21 || (c >= 0x27 && c <= 0x29) || c === 0x7e;

function utf8Bytes(cp) {
	if (cp < 0x80) return [cp];
	if (cp < 0x800) return [0xc0 | (cp >> 6), 0x80 | (cp & 63)];
	if (cp < 0x10000) return [0xe0 | (cp >> 12), 0x80 | ((cp >> 6) & 63), 0x80 | (cp & 63)];
	return [0xf0 | (cp >> 18), 0x80 | ((cp >> 12) & 63), 0x80 | ((cp >> 6) & 63), 0x80 | (cp & 63)];
}
const HEX = '0123456789ABCDEF';
function pctByte(b) { return '%' + HEX[b >> 4] + HEX[b & 15]; }
function pctEncodeCp(cp, set, spaceAsPlus) {
	if (spaceAsPlus && cp === 0x20) return '+';
	if (!set(cp)) return String.fromCodePoint(cp);
	let s = '';
	for (const b of utf8Bytes(cp)) s += pctByte(b);
	return s;
}
function pctEncodeStr(str, set, spaceAsPlus) {
	let out = '';
	for (const ch of toUSV(str)) out += pctEncodeCp(ch.codePointAt(0), set, spaceAsPlus);
	return out;
}
function toUSV(s) {	/* lone surrogates to U+FFFD (Onyx: the native toWellFormed -- a regexp with a
			 * look-behind made each new URL slow) */
	return String(s).toWellFormed();
}
function pctDecodeBytes(s) {	/* the bytes of a string, its %XX decoded */
	const bytes = [];
	for (const ch of s) {
		const cp = ch.codePointAt(0);
		for (const b of utf8Bytes(cp)) bytes.push(b);
	}
	const out = [];
	for (let i = 0; i < bytes.length; i++) {
		if (bytes[i] === 0x25 && i + 2 < bytes.length && HEXD(bytes[i + 1]) && HEXD(bytes[i + 2])) {
			out.push(parseInt(String.fromCharCode(bytes[i + 1], bytes[i + 2]), 16));
			i += 2;
		} else out.push(bytes[i]);
	}
	return out;
}
function utf8DecodeLossy(bytes) {
	let s = '';
	for (let i = 0; i < bytes.length;) {
		const b = bytes[i];
		let n = 0, cp = 0, min = 0;
		if (b < 0x80) { s += String.fromCharCode(b); i++; continue; }
		else if (b >= 0xc2 && b < 0xe0) { n = 1; cp = b & 31; min = 0x80; }
		else if (b >= 0xe0 && b < 0xf0) { n = 2; cp = b & 15; min = 0x800; }
		else if (b >= 0xf0 && b < 0xf5) { n = 3; cp = b & 7; min = 0x10000; }
		else { s += '\ufffd'; i++; continue; }
		let j = 1;
		for (; j <= n; j++) {
			const c = bytes[i + j];
			if (c === undefined || (c & 0xc0) !== 0x80) break;
			if (j === 1 && ((b === 0xe0 && c < 0xa0) || (b === 0xed && c > 0x9f) ||
				(b === 0xf0 && c < 0x90) || (b === 0xf4 && c > 0x8f))) break;
			cp = (cp << 6) | (c & 63);
		}
		if (j <= n) { s += '\ufffd'; i += j; continue; }
		s += cp >= min ? String.fromCodePoint(cp) : '\ufffd';
		i += n + 1;
	}
	return s;
}

/* ---- hosts ---- */
function punyEncode(input) {	/* RFC 3492 */
	const base = 36, tMin = 1, tMax = 26, skew = 38, damp = 700;
	const cps = Array.from(input, c => c.codePointAt(0));
	let n = 128, delta = 0, bias = 72, out = '';
	for (const c of cps) if (c < 128) out += String.fromCharCode(c);
	const b = out.length;
	let h = b;
	if (b > 0) out += '-';
	const adapt = (d, numPoints, first) => {
		d = first ? Math.floor(d / damp) : d >> 1;
		d += Math.floor(d / numPoints);
		let k = 0;
		while (d > ((base - tMin) * tMax) >> 1) { d = Math.floor(d / (base - tMin)); k += base; }
		return k + Math.floor((base - tMin + 1) * d / (d + skew));
	};
	const digit = d => String.fromCharCode(d + 22 + 75 * (d < 26));
	while (h < cps.length) {
		let m = Infinity;
		for (const c of cps) if (c >= n && c < m) m = c;
		delta += (m - n) * (h + 1);
		n = m;
		for (const c of cps) {
			if (c < n) delta++;
			if (c === n) {
				let q = delta;
				for (let k = base; ; k += base) {
					const t = k <= bias ? tMin : k >= bias + tMax ? tMax : k - bias;
					if (q < t) break;
					out += digit(t + (q - t) % (base - t));
					q = Math.floor((q - t) / (base - t));
				}
				out += digit(q);
				bias = adapt(delta, h + 1, h === b);
				delta = 0;
				h++;
			}
		}
		delta++; n++;
	}
	return out;
}
function punyDecodeValid(s) {	/* whether an xn-- label decodes (a label that does not: failure) */
	const base = 36;
	let i = 0, n = 128, bias = 72, out = [];
	const d = s.lastIndexOf('-');
	if (d > 0) for (let j = 0; j < d; j++) { const c = s.charCodeAt(j); if (c >= 128) return false; out.push(c); }
	for (let p = d > 0 ? d + 1 : 0; p < s.length;) {
		const oldi = i;
		for (let w = 1, k = base; ; k += base) {
			if (p >= s.length) return false;
			const c = s.charCodeAt(p++);
			const dg = c - 48 < 10 ? c - 22 : c - 65 < 26 ? c - 65 : c - 97 < 26 ? c - 97 : base;
			if (dg >= base) return false;
			i += dg * w;
			const t = k <= bias ? 1 : k >= bias + 26 ? 26 : k - bias;
			if (dg < t) break;
			w *= base - t;
		}
		const len = out.length + 1;
		let delta = i - oldi;
		delta = oldi === 0 ? Math.floor(delta / 700) : delta >> 1;
		delta += Math.floor(delta / len);
		let k = 0;
		while (delta > 455) { delta = Math.floor(delta / 35); k += 36; }
		bias = k + Math.floor(36 * delta / (delta + 38));
		n += Math.floor(i / len);
		i %= len;
		if (n > 0x10ffff) return false;
		out.splice(i++, 0, n);
	}
	return true;
}
function domainToASCII(domain) {
	/* UTS 46, simplified: mapped by NFC and lower case, the full stops folded, each
	 * non-ASCII label as punycode */
	let d = domain.normalize ? domain.normalize('NFKC') : domain;
	d = d.replace(/[。．｡]/g, '.').replace(/[­​⁠﻿͏᠋-᠍︀-️]/g, '').toLowerCase();
	/* disallowed: bidi and format controls, noncharacters, a few symbols UTS 46 refuses */
	if (/[‎‏‪-‮⁦-⁩۝․-…⿰-⿿￹-￻﷐-﷯￾￿]|[\ud83f\ud87f\ud8bf\ud8ff\ud93f\ud97f\ud9bf\ud9ff\uda3f\uda7f\udabf\udaff\udb3f\udb7f\udbbf\udbff][\udffe\udfff]/.test(d))
		return null;
	if (d === '') return null;
	const labels = d.split('.');
	const out = [];
	for (const l of labels) {
		if (/[^\x00-\x7f]/.test(l)) {
			if (/[�]/.test(l) || FORBIDDEN_DOMAIN.test(l)) return null;
			if (/^[‌‍]|[‌‍]$/.test(l)) return null;
			out.push('xn--' + punyEncode(l));
		} else {
			if (l.startsWith('xn--') && !punyDecodeValid(l.slice(4))) return null;
			out.push(l);
		}
	}
	return out.join('.');
}
const FORBIDDEN_HOST = /[\x00\t\n\r #/:<>?@[\\\]^|]/;
const FORBIDDEN_DOMAIN = /[\x00-\x1f\t\n\r #%/:<>?@[\\\]^|\x7f]/;
function parseIPv4Number(s) {
	if (s === '') return NaN;
	let r = 10;
	if (s.length >= 2 && (s.startsWith('0x') || s.startsWith('0X'))) { s = s.slice(2); r = 16; }
	else if (s.length >= 2 && s[0] === '0') { s = s.slice(1); r = 8; }
	if (s === '') return 0;
	const re = r === 10 ? /^[0-9]+$/ : r === 16 ? /^[0-9a-fA-F]+$/ : /^[0-7]+$/;
	if (!re.test(s)) return NaN;
	return parseInt(s, r);
}
function endsInNumber(host) {
	const parts = host.split('.');
	if (parts[parts.length - 1] === '') { if (parts.length === 1) return false; parts.pop(); }
	const last = parts[parts.length - 1];
	if (last !== '' && /^[0-9]+$/.test(last)) return true;
	return !isNaN(parseIPv4Number(last));
}
function parseIPv4(host) {
	const parts = host.split('.');
	if (parts[parts.length - 1] === '' && parts.length > 1) parts.pop();
	if (parts.length > 4) return null;
	const nums = [];
	for (const p of parts) {
		const n = parseIPv4Number(p);
		if (isNaN(n)) return null;
		nums.push(n);
	}
	for (let i = 0; i < nums.length - 1; i++) if (nums[i] > 255) return null;
	if (nums[nums.length - 1] >= 256 ** (5 - nums.length)) return null;
	let ipv4 = nums[nums.length - 1];
	for (let i = 0; i < nums.length - 1; i++) ipv4 += nums[i] * 256 ** (3 - i);
	return ipv4;
}
function serializeIPv4(a) {
	const out = [];
	for (let i = 0; i < 4; i++) { out.unshift(String(a % 256)); a = Math.floor(a / 256); }
	return out.join('.');
}
function parseIPv6(input) {
	const addr = [0, 0, 0, 0, 0, 0, 0, 0];
	let piece = 0, compress = null, p = 0;
	const c = i => input.charCodeAt(i);
	if (c(p) === 0x3a) {
		if (c(p + 1) !== 0x3a) return null;
		p += 2; piece++; compress = piece;
	}
	while (p < input.length) {
		if (piece === 8) return null;
		if (c(p) === 0x3a) {
			if (compress !== null) return null;
			p++; piece++; compress = piece; continue;
		}
		let value = 0, length = 0;
		while (length < 4 && p < input.length && HEXD(c(p))) { value = value * 16 + parseInt(input[p], 16); p++; length++; }
		if (c(p) === 0x2e) {
			if (length === 0) return null;
			p -= length;
			if (piece > 6) return null;
			let seen = 0;
			while (p < input.length) {
				let v4 = null;
				if (seen > 0) { if (c(p) === 0x2e && seen < 4) p++; else return null; }
				if (!DIGIT(c(p))) return null;
				while (DIGIT(c(p))) {
					const n = c(p) - 48;
					if (v4 === null) v4 = n; else if (v4 === 0) return null; else v4 = v4 * 10 + n;
					if (v4 > 255) return null;
					p++;
				}
				addr[piece] = addr[piece] * 256 + v4;
				seen++;
				if (seen === 2 || seen === 4) piece++;
			}
			if (seen !== 4) return null;
			break;
		} else if (c(p) === 0x3a) {
			p++;
			if (p >= input.length) return null;
		} else if (p < input.length) return null;
		addr[piece] = value;
		piece++;
	}
	if (compress !== null) {
		let swaps = piece - compress;
		piece = 7;
		while (piece !== 0 && swaps > 0) {
			const t = addr[compress + swaps - 1];
			addr[compress + swaps - 1] = addr[piece];
			addr[piece] = t;
			piece--; swaps--;
		}
	} else if (piece !== 8) return null;
	return addr;
}
function serializeIPv6(a) {
	let best = -1, bestLen = 1;
	for (let i = 0; i < 8;) {
		if (a[i] !== 0) { i++; continue; }
		let j = i;
		while (j < 8 && a[j] === 0) j++;
		if (j - i > bestLen) { best = i; bestLen = j - i; }
		i = j;
	}
	let out = '', ignore0 = false;
	for (let i = 0; i < 8; i++) {
		if (ignore0 && a[i] === 0) continue;
		ignore0 = false;
		if (best === i) { out += i === 0 ? '::' : ':'; ignore0 = true; continue; }
		out += a[i].toString(16);
		if (i !== 7) out += ':';
	}
	return '[' + out + ']';
}
function parseHost(input, notSpecial) {
	if (input[0] === '[') {
		if (input[input.length - 1] !== ']') return null;
		const a = parseIPv6(input.slice(1, -1));
		return a ? serializeIPv6(a) : null;
	}
	if (notSpecial) {
		if (FORBIDDEN_HOST.test(input)) return null;
		return pctEncodeStr(input, PE_C0);
	}
	const domain = utf8DecodeLossy(pctDecodeBytes(input));
	const ascii = domainToASCII(domain);
	if (ascii === null || ascii === '' || FORBIDDEN_DOMAIN.test(ascii)) return null;
	if (endsInNumber(ascii)) {
		const v4 = parseIPv4(ascii);
		return v4 === null ? null : serializeIPv4(v4);
	}
	return ascii;
}

/* ---- the basic URL parser ---- */
function isWinLetter(s, normalizedOnly) {
	return s.length === 2 && ALPHA(s.charCodeAt(0)) && (s[1] === ':' || (!normalizedOnly && s[1] === '|'));
}
function startsWithWinLetter(cps, p) {
	if (cps.length - p < 2) return false;
	if (!ALPHA(cps[p]) || (cps[p + 1] !== 0x3a && cps[p + 1] !== 0x7c)) return false;
	if (cps.length - p === 2) return true;
	const c = cps[p + 2];
	return c === 0x2f || c === 0x5c || c === 0x3f || c === 0x23;
}
function shortenPath(u) {
	const path = u.path;
	if (u.scheme === 'file' && path.length === 1 && isWinLetter(path[0], true)) return;
	path.pop();
}
const SINGLE_DOT = s => s === '.' || s.toLowerCase() === '%2e';
const DOUBLE_DOT = s => { s = s.toLowerCase(); return s === '..' || s === '.%2e' || s === '%2e.' || s === '%2e%2e'; };

function basicParse(input, base, url, stateOverride) {
	if (!url) {
		url = { scheme: '', username: '', password: '', host: null, port: null, path: [],
			opaque: false, query: null, fragment: null };
		input = input.replace(/^[\x00-\x20]+|[\x00-\x20]+$/g, '');
	}
	input = input.replace(/[\t\n\r]/g, '');
	let state = stateOverride || 'scheme start';
	let buffer = '', atSign = false, inside = false, passwordSeen = false;
	const cps = Array.from(toUSV(input), ch => ch.codePointAt(0));
	const EOF = -1;
	for (let p = 0; ; p++) {
		const c = p < cps.length ? cps[p] : EOF;
		const ch = c === EOF ? '' : String.fromCodePoint(c);
		switch (state) {
		case 'scheme start':
			if (c !== EOF && ALPHA(c)) { buffer += ch.toLowerCase(); state = 'scheme'; }
			else if (!stateOverride) { state = 'no scheme'; p--; }
			else return null;
			break;
		case 'scheme':
			if (c !== EOF && (ALPHA(c) || DIGIT(c) || c === 0x2b || c === 0x2d || c === 0x2e)) buffer += ch.toLowerCase();
			else if (c === 0x3a) {
				if (stateOverride) {
					if (isSpecial(url.scheme) !== isSpecial(buffer)) return url;
					if ((url.username !== '' || url.password !== '' || url.port !== null) && buffer === 'file') return url;
					if (url.scheme === 'file' && url.host === '') return url;
				}
				url.scheme = buffer;
				if (stateOverride) {
					if (url.port === SPECIAL[url.scheme]) url.port = null;
					return url;
				}
				buffer = '';
				if (url.scheme === 'file') state = 'file';
				else if (isSpecial(url.scheme) && base && base.scheme === url.scheme) state = 'special relative or authority';
				else if (isSpecial(url.scheme)) state = 'special authority slashes';
				else if (cps[p + 1] === 0x2f) { state = 'path or authority'; p++; }
				else { url.opaque = true; url.path = ''; state = 'opaque path'; }
			} else if (!stateOverride) { buffer = ''; state = 'no scheme'; p = -1; }
			else return null;
			break;
		case 'no scheme':
			if (!base || (base.opaque && c !== 0x23)) return null;
			if (base.opaque && c === 0x23) {
				url.scheme = base.scheme; url.path = base.path; url.opaque = true;
				url.query = base.query; url.fragment = ''; state = 'fragment';
			} else if (base.scheme !== 'file') { state = 'relative'; p--; }
			else { state = 'file'; p--; }
			break;
		case 'special relative or authority':
			if (c === 0x2f && cps[p + 1] === 0x2f) { state = 'special authority ignore slashes'; p++; }
			else { state = 'relative'; p--; }
			break;
		case 'path or authority':
			if (c === 0x2f) state = 'authority';
			else { state = 'path'; p--; }
			break;
		case 'relative':
			url.scheme = base.scheme;
			if (c === 0x2f) state = 'relative slash';
			else if (isSpecial(url.scheme) && c === 0x5c) state = 'relative slash';
			else {
				url.username = base.username; url.password = base.password; url.host = base.host;
				url.port = base.port; url.path = base.path.slice(); url.query = base.query;
				if (c === 0x3f) { url.query = ''; state = 'query'; }
				else if (c === 0x23) { url.fragment = ''; state = 'fragment'; }
				else if (c !== EOF) { url.query = null; shortenPath(url); state = 'path'; p--; }
			}
			break;
		case 'relative slash':
			if (isSpecial(url.scheme) && (c === 0x2f || c === 0x5c)) state = 'special authority ignore slashes';
			else if (c === 0x2f) state = 'authority';
			else {
				url.username = base.username; url.password = base.password; url.host = base.host;
				url.port = base.port; state = 'path'; p--;
			}
			break;
		case 'special authority slashes':
			if (c === 0x2f && cps[p + 1] === 0x2f) { state = 'special authority ignore slashes'; p++; }
			else { state = 'special authority ignore slashes'; p--; }
			break;
		case 'special authority ignore slashes':
			if (c !== 0x2f && c !== 0x5c) { state = 'authority'; p--; }
			break;
		case 'authority':
			if (c === 0x40) {
				if (atSign) buffer = '%40' + buffer;
				atSign = true;
				for (const bc of buffer) {
					const bcp = bc.codePointAt(0);
					if (bcp === 0x3a && !passwordSeen) { passwordSeen = true; continue; }
					const enc = pctEncodeCp(bcp, PE_USER);
					if (passwordSeen) url.password += enc; else url.username += enc;
				}
				buffer = '';
			} else if (c === EOF || c === 0x2f || c === 0x3f || c === 0x23 || (isSpecial(url.scheme) && c === 0x5c)) {
				if (atSign && buffer === '') return null;
				p -= Array.from(buffer).length + 1;
				buffer = '';
				state = 'host';
			} else buffer += ch;
			break;
		case 'host':
		case 'hostname':
			if (stateOverride && url.scheme === 'file') { p--; state = 'file host'; }
			else if (c === 0x3a && !inside) {
				if (buffer === '') return null;
				if (stateOverride === 'hostname') return null;
				const h = parseHost(buffer, !isSpecial(url.scheme));
				if (h === null) return null;
				url.host = h; buffer = ''; state = 'port';
			} else if (c === EOF || c === 0x2f || c === 0x3f || c === 0x23 || (isSpecial(url.scheme) && c === 0x5c)) {
				p--;
				if (isSpecial(url.scheme) && buffer === '') return null;
				if (stateOverride && buffer === '' && (url.username !== '' || url.password !== '' || url.port !== null)) return null;
				const h = parseHost(buffer, !isSpecial(url.scheme));
				if (h === null) return null;
				url.host = h; buffer = ''; state = 'path start';
				if (stateOverride) return url;
			} else {
				if (c === 0x5b) inside = true;
				if (c === 0x5d) inside = false;
				buffer += ch;
			}
			break;
		case 'port':
			if (c !== EOF && DIGIT(c)) buffer += ch;
			else if (c === EOF || c === 0x2f || c === 0x3f || c === 0x23 || (isSpecial(url.scheme) && c === 0x5c) || stateOverride) {
				if (buffer !== '') {
					const port = parseInt(buffer, 10);
					if (port > 65535) return null;
					url.port = port === SPECIAL[url.scheme] ? null : port;
					buffer = '';
					if (stateOverride) return url;
				}
				if (stateOverride) return null;
				state = 'path start'; p--;
			} else return null;
			break;
		case 'file':
			url.scheme = 'file';
			url.host = '';
			if (c === 0x2f || c === 0x5c) state = 'file slash';
			else if (base && base.scheme === 'file') {
				url.host = base.host; url.path = base.path.slice(); url.query = base.query;
				if (c === 0x3f) { url.query = ''; state = 'query'; }
				else if (c === 0x23) { url.fragment = ''; state = 'fragment'; }
				else if (c !== EOF) {
					url.query = null;
					if (!startsWithWinLetter(cps, p)) shortenPath(url);
					else url.path = [];
					state = 'path'; p--;
				}
			} else { state = 'path'; p--; }
			break;
		case 'file slash':
			if (c === 0x2f || c === 0x5c) state = 'file host';
			else {
				if (base && base.scheme === 'file') {
					url.host = base.host;
					if (!startsWithWinLetter(cps, p) && base.path.length > 0 && isWinLetter(base.path[0], true))
						url.path.push(base.path[0]);
				}
				state = 'path'; p--;
			}
			break;
		case 'file host':
			if (c === EOF || c === 0x2f || c === 0x5c || c === 0x3f || c === 0x23) {
				p--;
				if (!stateOverride && isWinLetter(buffer, false)) state = 'path';
				else if (buffer === '') {
					url.host = '';
					if (stateOverride) return url;
					state = 'path start';
				} else {
					let h = parseHost(buffer, false);
					if (h === null) return null;
					if (h === 'localhost') h = '';
					url.host = h;
					if (stateOverride) return url;
					buffer = ''; state = 'path start';
				}
			} else buffer += ch;
			break;
		case 'path start':
			if (isSpecial(url.scheme)) { state = 'path'; if (c !== 0x2f && c !== 0x5c) p--; }
			else if (!stateOverride && c === 0x3f) { url.query = ''; state = 'query'; }
			else if (!stateOverride && c === 0x23) { url.fragment = ''; state = 'fragment'; }
			else if (c !== EOF) { state = 'path'; if (c !== 0x2f) p--; }
			else if (stateOverride && url.host === null) url.path.push('');
			break;
		case 'path':
			if (c === EOF || c === 0x2f || (isSpecial(url.scheme) && c === 0x5c) ||
			    (!stateOverride && (c === 0x3f || c === 0x23))) {
				if (DOUBLE_DOT(buffer)) {
					shortenPath(url);
					if (c !== 0x2f && !(isSpecial(url.scheme) && c === 0x5c)) url.path.push('');
				} else if (SINGLE_DOT(buffer) && c !== 0x2f && !(isSpecial(url.scheme) && c === 0x5c)) {
					url.path.push('');
				} else if (!SINGLE_DOT(buffer)) {
					if (url.scheme === 'file' && url.path.length === 0 && isWinLetter(buffer, false))
						buffer = buffer[0] + ':';
					url.path.push(buffer);
				}
				buffer = '';
				if (c === 0x3f) { url.query = ''; state = 'query'; }
				if (c === 0x23) { url.fragment = ''; state = 'fragment'; }
			} else buffer += pctEncodeCp(c, PE_PATH);
			break;
		case 'opaque path':
			if (c === 0x3f) { url.query = ''; state = 'query'; }
			else if (c === 0x23) { url.fragment = ''; state = 'fragment'; }
			else if (c === 0x20) {
				const nx = cps[p + 1];
				if (nx === 0x3f || nx === 0x23) url.path += '%20'; else url.path += ' ';
			} else if (c !== EOF) url.path += pctEncodeCp(c, PE_C0);
			break;
		case 'query':
			if ((!stateOverride && c === 0x23) || c === EOF) {
				url.query += pctEncodeStr(buffer, isSpecial(url.scheme) ? PE_SQUERY : PE_QUERY);
				buffer = '';
				if (c === 0x23) { url.fragment = ''; state = 'fragment'; }
			} else if (c !== EOF) buffer += ch;
			break;
		case 'fragment':
			if (c !== EOF) url.fragment += pctEncodeCp(c, PE_FRAG);
			break;
		}
		if (c === EOF && p >= cps.length) break;
	}
	return url;
}

function serializePath(u) {
	if (u.opaque) return u.path;
	let s = '';
	for (const seg of u.path) s += '/' + seg;
	return s;
}
function serializeURL(u, noFragment) {
	let out = u.scheme + ':';
	if (u.host !== null) {
		out += '//';
		if (u.username !== '' || u.password !== '') {
			out += u.username;
			if (u.password !== '') out += ':' + u.password;
			out += '@';
		}
		out += u.host;
		if (u.port !== null) out += ':' + u.port;
	}
	if (u.host === null && !u.opaque && u.path.length > 1 && u.path[0] === '') out += '/.';
	out += serializePath(u);
	if (u.query !== null) out += '?' + u.query;
	if (!noFragment && u.fragment !== null) out += '#' + u.fragment;
	return out;
}

/* ---- application/x-www-form-urlencoded ---- */
function formParse(s) {
	const out = [];
	for (const seq of String(s).split('&')) {
		if (seq === '') continue;
		const i = seq.indexOf('=');
		let name = i < 0 ? seq : seq.slice(0, i), value = i < 0 ? '' : seq.slice(i + 1);
		const dec = x => utf8DecodeLossy(pctDecodeBytes(x.replace(/\+/g, ' ')));
		out.push([dec(name), dec(value)]);
	}
	return out;
}
const formSerialize = list => list.map(([k, v]) => pctEncodeStr(k, PE_FORM, true) + '=' + pctEncodeStr(v, PE_FORM, true)).join('&');

class URLSearchParams {
	constructor(init = '') {
		this._e = [];
		this._url = null;
		if (init instanceof URLSearchParams) this._e = init._e.map(x => x.slice());
		else if (typeof FormData !== 'undefined' && init instanceof FormData) for (const [k, v] of init) this._e.push([toUSV(k), toUSV(v)]);
		else if ((typeof init === 'object' && init !== null) || typeof init === 'function') {
			if (typeof init[Symbol.iterator] === 'function') {
				for (const pair of init) {
					const a = Array.from(pair);
					if (a.length !== 2) throw new TypeError("Failed to construct 'URLSearchParams': a sequence must be of pairs");
					this._e.push([toUSV(a[0]), toUSV(a[1])]);
				}
			} else for (const k of Object.keys(init)) this._e.push([toUSV(k), toUSV(init[k])]);
		} else {
			let s = toUSV(init);
			if (s[0] === '?') s = s.slice(1);
			this._e = formParse(s);
		}
	}
	append(k, v) { this._e.push([toUSV(k), toUSV(v)]); this._upd(); }
	delete(k, v) {
		k = toUSV(k);
		this._e = this._e.filter(x => !(x[0] === k && (v === undefined || x[1] === toUSV(v))));
		this._upd();
	}
	get(k) { k = toUSV(k); const e = this._e.find(x => x[0] === k); return e ? e[1] : null; }
	getAll(k) { k = toUSV(k); return this._e.filter(x => x[0] === k).map(x => x[1]); }
	has(k, v) { k = toUSV(k); return this._e.some(x => x[0] === k && (v === undefined || x[1] === toUSV(v))); }
	set(k, v) {
		k = toUSV(k); v = toUSV(v);
		const i = this._e.findIndex(x => x[0] === k);
		if (i < 0) this._e.push([k, v]);
		else { this._e[i][1] = v; this._e = this._e.filter((x, j) => j <= i || x[0] !== k); }
		this._upd();
	}
	sort() {
		/* stable, by UTF-16 code units */
		this._e = this._e.map((x, i) => [x, i]).sort((a, b) => a[0][0] < b[0][0] ? -1 : a[0][0] > b[0][0] ? 1 : a[1] - b[1]).map(x => x[0]);
		this._upd();
	}
	get size() { return this._e.length; }
	forEach(fn, t) { for (let i = 0; i < this._e.length; i++) fn.call(t, this._e[i][1], this._e[i][0], this); }
	*entries() { for (let i = 0; i < this._e.length; i++) yield [this._e[i][0], this._e[i][1]]; }
	*keys() { for (let i = 0; i < this._e.length; i++) yield this._e[i][0]; }
	*values() { for (let i = 0; i < this._e.length; i++) yield this._e[i][1]; }
	[Symbol.iterator]() { return this.entries(); }
	toString() { return formSerialize(this._e); }
	_upd() {
		if (!this._url) return;
		const s = this.toString();
		this._url._u.query = s === '' ? null : s;
		if (s === '') stripTrailingSpaces(this._url._u);
	}
	get [Symbol.toStringTag]() { return 'URLSearchParams'; }
}
function stripTrailingSpaces(u) {
	if (!u.opaque || u.fragment !== null || u.query !== null) return;
	u.path = u.path.replace(/ +$/, '');
}

/* Onyx: new URL(url, base)'s parses kept (a copy handed out each time): the spec's parser in
 * JS is slow, and pages make the same URLs again and again (browserscore.dev: a quarter of
 * its time in new URL, its features' links made at each render) */
const URL_PARSED = new Map();
function urlParseCached(url, base) {
	const key = base === undefined ? url : url + '\u0000' + base;
	let u = URL_PARSED.get(key);
	if (u === undefined) {
		let b = null;
		if (base !== undefined) {
			b = URL_PARSED.get(base);
			if (b === undefined) {
				b = basicParse(base, null);
				URL_PARSED.set(base, b);
			}
			if (!b) throw new TypeError("Failed to construct 'URL': Invalid base URL");
		}
		/* "#fragment" of a base (no code point to percent-encode): the base, that fragment */
		if (b && /^#[\x21\x23-\x3b\x3d\x3f-\x5f\x61-\x7e]*$/.test(url))
			u = { ...b, path: Array.isArray(b.path) ? b.path.slice() : b.path, fragment: url.slice(1) };
		else
			u = basicParse(url, b);
		/* Onyx: 8192 kept (1024 before: browserscore.dev's 3000 links each render cleared
		 * the table again and again -- each URL parsed anew) */
		if (URL_PARSED.size >= 8192)
			URL_PARSED.clear();
		URL_PARSED.set(key, u);
	}
	if (!u) throw new TypeError("Failed to construct 'URL': Invalid URL");
	return u;
}
function urlCopy(u) { return { ...u, path: Array.isArray(u.path) ? u.path.slice() : u.path }; }

/* Onyx: a URL holds the kept parse (_c, shared, never changed) until something reads or
 * changes its record (_u: a copy made then, its own); its href made once per kept parse --
 * new URL(link, base).href is what pages do (browserscore.dev: 3000 of them a render) */
const URL_HREF = new WeakMap();
class URL {
	constructor(url, base) {
		this._c = urlParseCached(toUSV(url), base === undefined ? undefined : toUSV(base));
		this._m = null;
		this._q = null;
	}
	get _u() { return this._m || (this._m = urlCopy(this._c)); }
	set _u(u) { this._m = u; }
	get _r() { return this._m || this._c; }	/* (to read) */
	static parse(url, base) { try { return new URL(url, base); } catch (e) { return null; } }
	static canParse(url, base) { try { new URL(url, base); return true; } catch (e) { return false; } }
	get href() {
		if (this._m) return serializeURL(this._m);
		let h = URL_HREF.get(this._c);
		if (h === undefined) URL_HREF.set(this._c, h = serializeURL(this._c));
		return h;
	}
	set href(v) {
		const u = basicParse(toUSV(v), null);
		if (!u) throw new TypeError("Failed to set the 'href' property on 'URL': Invalid URL");
		this._u = u;
		if (this._q) this._q._e = formParse(u.query || '');
	}
	get origin() {
		const u = this._r;
		if (u.scheme === 'blob') {
			try { const p = new URL(serializePath(u)); if (p.protocol === 'http:' || p.protocol === 'https:') return p.origin; } catch (e) {}
			return 'null';
		}
		if (u.scheme === 'file' || !isSpecial(u.scheme)) return 'null';
		return u.scheme + '://' + u.host + (u.port !== null ? ':' + u.port : '');
	}
	get protocol() { return this._r.scheme + ':'; }
	set protocol(v) { basicParse(toUSV(v) + ':', null, this._u, 'scheme start'); }
	get username() { return this._r.username; }
	set username(v) {
		const u = this._u;
		if (u.host === null || u.host === '' || u.scheme === 'file') return;
		u.username = pctEncodeStr(v, PE_USER);
	}
	get password() { return this._r.password; }
	set password(v) {
		const u = this._u;
		if (u.host === null || u.host === '' || u.scheme === 'file') return;
		u.password = pctEncodeStr(v, PE_USER);
	}
	get host() {
		const u = this._r;
		if (u.host === null) return '';
		return u.port === null ? u.host : u.host + ':' + u.port;
	}
	set host(v) { if (!this._u.opaque) basicParse(toUSV(v), null, this._u, 'host'); }
	get hostname() { return this._r.host === null ? '' : this._r.host; }
	set hostname(v) { if (!this._u.opaque) basicParse(toUSV(v), null, this._u, 'hostname'); }
	get port() { return this._r.port === null ? '' : String(this._r.port); }
	set port(v) {
		const u = this._u;
		if (u.host === null || u.host === '' || u.scheme === 'file') return;
		v = toUSV(v);
		if (v === '') u.port = null;
		else basicParse(v, null, this._u, 'port');
	}
	get pathname() { return serializePath(this._r); }
	set pathname(v) {
		const u = this._u;
		if (u.opaque) return;
		const save = u.path;
		u.path = [];
		if (basicParse(toUSV(v), null, u, 'path start') === null) u.path = save;
	}
	get search() { const q = this._r.query; return q === null || q === '' ? '' : '?' + q; }
	set search(v) {
		const u = this._u;
		v = toUSV(v);
		if (v === '') { u.query = null; if (this._q) this._q._e = []; stripTrailingSpaces(u); return; }
		if (v[0] === '?') v = v.slice(1);
		u.query = '';
		basicParse(v, null, u, 'query');
		if (this._q) this._q._e = formParse(v);
	}
	get searchParams() {
		if (!this._q) { this._q = new URLSearchParams(this._r.query || ''); this._q._url = this; }
		return this._q;
	}
	get hash() { const f = this._r.fragment; return f === null || f === '' ? '' : '#' + f; }
	set hash(v) {
		const u = this._u;
		v = toUSV(v);
		if (v === '') { u.fragment = null; stripTrailingSpaces(u); return; }
		if (v[0] === '#') v = v.slice(1);
		u.fragment = '';
		basicParse(v, null, u, 'fragment');
	}
	toString() { return this.href; }
	toJSON() { return this.href; }
	get [Symbol.toStringTag]() { return 'URL'; }
	static createObjectURL() { return 'blob:onyx'; }
	static revokeObjectURL() {}
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
			const r = N.rect(this.root, true);
			root = { x: r[0], y: r[1], w: r[2], h: r[3] };
		}
		const top = root.y - marginPx(mt, root.h), bottom = root.y + root.h + marginPx(mb, root.h);
		const left = root.x - marginPx(ml, root.w), right = root.x + root.w + marginPx(mr, root.w);
		const entries = [];
		for (const [el, last] of this._targets) {
			const r = N.rect(el, true);
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
/* (Onyx: the entries and sizes as their classes, the attributes on the prototypes) */
class ResizeObserverSize {
	constructor(i, b) { Object.defineProperty(this, '_s', { value: [i, b] }); }
	get inlineSize() { return this._s[0]; }
	get blockSize() { return this._s[1]; }
}
class ResizeObserverEntry {
	constructor(el, w, h) {
		const size = Object.freeze([new ResizeObserverSize(w, h)]);
		Object.defineProperty(this, '_e', { value: { target: el, rect: new DOMRect(0, 0, w, h), size } });
	}
	get target() { return this._e.target; }
	get contentRect() { return this._e.rect; }
	get borderBoxSize() { return this._e.size; }
	get contentBoxSize() { return this._e.size; }
	get devicePixelContentBoxSize() { return this._e.size; }
}
G.ResizeObserverSize = ResizeObserverSize;
G.ResizeObserverEntry = ResizeObserverEntry;
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
			entries.push(new ResizeObserverEntry(el, r[2], r[3]));
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

/* Onyx: the CSS Font Loading API -- FontFace (a font from a URL or bytes), document.fonts
 * (the faces a script adds: fetched, given to NetSurf's font code, the page laid out again;
 * the @font-face rules are NetSurf's own) */
function fontWeights(w) {
	const t = String(w == null ? '400' : w).trim().split(/\s+/).map(x =>
		x === 'normal' ? 400 : x === 'bold' ? 700 : parseInt(x, 10)).filter(x => x > 0);
	return t.length ? [t[0], t[t.length - 1]] : [400, 400];
}
class FontFace {
	constructor(family, source, desc = {}) {
		this.family = String(family).replace(/^["']|["']$/g, '');
		this.style = desc.style || 'normal';
		this.weight = String(desc.weight || 'normal');
		this.stretch = desc.stretch || 'normal';
		this.unicodeRange = desc.unicodeRange || 'U+0-10FFFF';
		this.display = desc.display || 'auto';
		this.featureSettings = desc.featureSettings || 'normal';
		this.variant = desc.variant || 'normal';
		this.variationSettings = desc.variationSettings || 'normal';
		this.ascentOverride = desc.ascentOverride || 'normal';
		this.descentOverride = desc.descentOverride || 'normal';
		this.lineGapOverride = desc.lineGapOverride || 'normal';
		this.status = 'unloaded';
		this._src = source;
		this._bytes = null;
		let ok, ko;
		this.loaded = new NativePromise((a, b) => { ok = a; ko = b; });
		this.loaded.catch(() => {});
		this._ok = ok; this._ko = ko;
		if (typeof source !== 'string') {
			const u8 = source instanceof ArrayBuffer ? new Uint8Array(source) :
				ArrayBuffer.isView(source) ? new Uint8Array(source.buffer, source.byteOffset, source.byteLength) : null;
			if (u8) { this._bytes = u8; this.status = 'loaded'; ok(this); }
			else { this.status = 'error'; ko(new DOMException('bad font source', 'SyntaxError')); }
		}
	}
	load() {
		if (this.status !== 'unloaded') return this.loaded;
		this.status = 'loading';
		/* the first url() of the source whose format NetSurf reads */
		const urls = [...String(this._src).matchAll(/url\(\s*(['"]?)([^'")]+)\1\s*\)(?:\s*format\(\s*['"]?([\w-]+)['"]?\s*\))?/g)]
			.filter(m => !m[3] || /^(woff2?|truetype|opentype)(-variations)?$/i.test(m[3]) ||
				/^(collection)$/i.test(m[3]) === false && !/embedded|svg/i.test(m[3]))
			.map(m => m[2]);
		const fail = () => { this.status = 'error'; this._ko(new DOMException('A network error occurred.', 'NetworkError')); };
		if (!urls.length) { fail(); return this.loaded; }
		let url;
		try { url = new URL(urls[0], document.baseURI || location.href).href; } catch (e) { fail(); return this.loaded; }
		const id = N.request('GET', url, null, ['X-Onyx-Dest: font'], true, (err, r) => {
			if (err != null || !r || r.status >= 400 || !r.body) { fail(); return; }
			const b = r.body;
			this._bytes = b instanceof Uint8Array ? b : b instanceof ArrayBuffer ? new Uint8Array(b) :
				Uint8Array.from(String(b), c => c.charCodeAt(0) & 255);
			this.status = 'loaded';
			this._ok(this);
		});
		if (id < 0) fail();
		return this.loaded;
	}
	_register() {
		if (this._given || !this._bytes) return;
		this._given = true;
		const [w1, w2] = fontWeights(this.weight);
		N.addFontFace(this.family, w1, w2, /italic|oblique/.test(this.style), this._bytes);
	}
}
protoFields(FontFace, ['family', 'style', 'weight', 'stretch', 'unicodeRange', 'display',
	'featureSettings', 'variant', 'variationSettings', 'ascentOverride', 'descentOverride',
	'lineGapOverride', 'status', 'loaded']);
class FontFaceSet extends EventTarget {
	constructor() {
		super();
		this._set = new Set();
		this._pending = 0;
		this.status = 'loaded';
		this._ready = NativePromise.resolve(this);
	}
	get ready() { return this._ready; }
	get size() { return this._set.size; }
	add(face) {
		if (!(face instanceof FontFace)) throw new TypeError('not a FontFace');
		if (this._set.has(face)) return this;
		this._set.add(face);
		if (face.status === 'loaded') face._register();
		else this._track(face);
		return this;
	}
	_track(face) {
		if (face.status === 'unloaded') face.load();
		if (face.status !== 'loading') { face._register(); return; }
		if (this._pending++ === 0) {
			this.status = 'loading';
			let done;
			this._ready = new NativePromise(r => { done = r; });
			this._done = done;
			dispatch(this, new Event('loading'));
		}
		const end = () => {
			face._register();
			if (--this._pending === 0) {
				this.status = 'loaded';
				dispatch(this, new Event('loadingdone'));
				this._done(this);
			}
		};
		face.loaded.then(end, end);
	}
	delete(face) { return this._set.delete(face); }
	clear() { this._set.clear(); }
	has(face) { return this._set.has(face); }
	check() { return true; }
	load(font) {
		const fams = String(font || '').split(',').map(f => f.trim().split(/\s+/).pop().replace(/^["']|["']$/g, '').toLowerCase());
		const faces = [...this._set].filter(f => fams.some(x => f.family.toLowerCase() === x || String(font).toLowerCase().includes(f.family.toLowerCase())));
		for (const f of faces) if (f.status === 'unloaded') this._track(f);
		return NativePromise.all(faces.map(f => f.loaded.catch(() => f))).then(() => faces);
	}
	forEach(fn, t) { for (const f of this._set) fn.call(t, f, f, this); }
	entries() { return [...this._set].map(f => [f, f])[Symbol.iterator](); }
	values() { return this._set.values(); }
	keys() { return this._set.values(); }
	[Symbol.iterator]() { return this._set.values(); }
}
const fontFaces = new FontFaceSet();
G.FontFace = FontFace;
G.FontFaceSet = FontFaceSet;

/* Onyx: the visual viewport -- the layout viewport (no pinch zoom) */
class VisualViewport extends EventTarget {
	get offsetLeft() { return 0; }
	get offsetTop() { return 0; }
	get pageLeft() { return G.scrollX || 0; }
	get pageTop() { return G.scrollY || 0; }
	get width() { return G.innerWidth; }
	get height() { return G.innerHeight; }
	get scale() { return 1; }
}
defineHandlers(VisualViewport.prototype, ['resize', 'scroll', 'scrollend']);
G.VisualViewport = VisualViewport;

/* ---- the global object ------------------------------------------------------------------- */

Object.assign(G, {
	window: G, self: G, top: G, parent: G, frames: G, opener: null,
	location, history, navigator, screen, console, performance,
	setTimeout, setInterval, clearTimeout, clearInterval: clearTimeout,
	requestAnimationFrame, cancelAnimationFrame,
	webkitRequestAnimationFrame: requestAnimationFrame,
	webkitCancelAnimationFrame: cancelAnimationFrame,
	TransitionEvent, AnimationEvent, AnimationPlaybackEvent, Animation, CSSAnimation,
	CSSTransition, AnimationEffect, KeyframeEffect, AnimationTimeline, DocumentTimeline,
	requestIdleCallback: cb => setTimeout(() => cb({ didTimeout: false, timeRemaining: () => 10 }), 1),
	cancelIdleCallback: clearTimeout,
	queueMicrotask: fn => { NativePromise.resolve().then(fn).catch(report); },
	matchMedia, getComputedStyle, atob, btoa,
	/* (Onyx: a tab's window is not moved or resized by its scripts, as in browsers) */
	moveTo() {}, moveBy() {}, resizeTo() {}, resizeBy() {},
	visualViewport: new VisualViewport(),
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
	CSS: { supports: (a, b) => b === undefined ?
		/* (Onyx: as the spec, a condition that does not parse is tried again in parentheses:
		 * CSS.supports("color: red")) */
		cssSupports(a) || cssSupports("(" + a + ")") :
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
		ev = new MouseEvent(type, Object.assign({ bubbles: true, cancelable: true, composed: true, view: G, detail: 1 }, init));
		break;
	case 'mousemove': case 'mouseover': case 'mouseout':
		ev = new MouseEvent(type, Object.assign({ bubbles: true, cancelable: true, composed: true, view: G }, init));
		break;
	case 'pointerdown': case 'pointerup':
		ev = new PointerEvent(type, Object.assign({ bubbles: true, cancelable: true, composed: true, view: G }, init));
		break;
	case 'keydown': case 'keyup': case 'keypress': {
		const key = init.key || '';
		const code = KEY_CODES[key] || (key.length === 1 ? key.toUpperCase().charCodeAt(0) : 0);
		ev = new KeyboardEvent(type, Object.assign({ bubbles: true, cancelable: true, composed: true, view: G, keyCode: code,
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
	case 'input':
		ev = new InputEvent(type, { bubbles: true, composed: true });
		break;
	case 'change':
		ev = new Event(type, { bubbles: true });
		break;
	case 'submit':
		ev = new SubmitEvent(type, { bubbles: true, cancelable: true });
		break;
	case 'focus': case 'blur':
		ev = new FocusEvent(type, { composed: true });
		break;
	case 'wheel':
		ev = new WheelEvent(type, Object.assign({ bubbles: true, cancelable: true, composed: true, view: G }, init));
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
		/* (Onyx: through the shadow roots to their hosts) */
		const path = n => { const a = []; for (; n; n = n.parentNode || N.shadowHost(n)) if (n instanceof Element) a.push(n); return a; };
		const oldPath = old && old.isConnected ? path(old) : [], newPath = path(el);
		const mev = (type, rel, bubbles) =>
			new MouseEvent(type, Object.assign({ bubbles, cancelable: bubbles, composed: true, view: G }, init, { relatedTarget: rel }));
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
/* (the specifiers a module names: N.moduleImports, in C, finds what the regular expression
 * /(?:^|[;\n\r}])\s*(?:import|export)\s*(?:[\w*{}\s,$]+?\s*from\s*)?(['"])([^'"\n]+)\1|
 * \bimport\s*\(\s*(['"])([^'"\n]+)\3\s*\)/g finds -- its backtracking over a big module was
 * 13 % of github.com's scripts) */

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
	for (const spec of N.moduleImports(text)) {
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

/* Onyx: a dynamic import() (qjs.c rewrites it): the module graph fetched first, then the
 * engine's own import (a classic Function's: not rewritten) finds every source */
const nativeImport = new Function('u', 'return import(u)');
G.__onyxImport = (base, spec) => {
	let url;
	try { url = new URL(String(spec), base || document.baseURI || location.href).href; }
	catch (e) { return NativePromise.reject(new TypeError('Failed to resolve module specifier ' + spec)); }
	return modFetch(url).then(t => {
		if (t === null)
			throw new TypeError('Failed to fetch dynamically imported module: ' + url);
		return modGraph(url, t, new Set([url]));
	}).then(() => nativeImport(url));
};

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
	if (type === 'onyx:frame') {		/* (Onyx: requestAnimationFrame) */
		runFrames(init.time);
		return true;
	}
	if (type === 'onyx:visibility') {	/* (Onyx: the window hidden / shown again) */
		dispatch(G.document, browserEvent(G.document, 'visibilitychange', {}));
		return true;
	}
	if (type === 'onyx:anim') {		/* (Onyx: transitions, animations) */
		animEvent(target, init);
		return true;
	}
	const ev = browserEvent(target, type, init || {});
	const ok = dispatch(t, ev);
	/* Onyx: the focus is mousedown's default action, after it as in Chrome (a script
	 * that prevents it keeps the focus where it is: an autocomplete list's items): to
	 * the focusable element pressed, else away from the focused one */
	if (type === 'mousedown' && ok && t instanceof Element) {
		const f = t.closest('a[href],area[href],button,input,select,textarea,iframe,summary,[tabindex],[contenteditable]');
		if (f && f !== activeElement) f.focus();
		else if (!f && activeElement && activeElement !== G.document.body)
			activeElement.blur();
	}
	if (type === 'scroll' || type === 'resize' || type === 'load') {
		if (type === 'resize') checkMedia();
		scheduleObservers();
	}
	return ok;
}

/* ---- the natives' setup ------------------------------------------------------------------- */

/* Onyx: dom.js's internals html5.js builds on (MutationObserver records, dispatch...) */
N.internals = { queueMutation, observers, childListRecord, dispatch, report, activate,
	shadowHook };

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
