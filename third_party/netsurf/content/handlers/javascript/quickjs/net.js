/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: the web's real-time and background APIs, run after html5.js (qjs_net.c calls this
 * function with the natives N and, in a worker's context, W: the worker's { url, name, type,
 * kind, source }; null in a document's):
 *
 *  - WebSocket (RFC 6455, ws: / wss:): the connection is user/netsurf/onyx_ws.c's thread
 *    (N.wsOpen...); text and binary messages (Blob or ArrayBuffer: binaryType),
 *    bufferedAmount, protocol, extensions (permessage-deflate), the closing handshake,
 *    CloseEvent;
 *  - EventSource (Server-Sent Events): the stream parsed as it comes, named events, id /
 *    Last-Event-ID, retry and the reconnection;
 *  - fetch's Response.body a ReadableStream fed as the bytes arrive (the Response given as
 *    soon as the head is in), request bodies from a stream; XMLHttpRequest's readyState
 *    HEADERS_RECEIVED / LOADING, its progress events with the bytes come so far, its
 *    responseText while it loads, upload events;
 *  - Worker and SharedWorker: each a context of its own (qjs.c: qjs_worker_create) on the
 *    UI thread; postMessage with the structured clone (QuickJS's serializer between the
 *    realms; Blob, File, Error, ImageData wrapped here); a worker's global scope (self,
 *    postMessage, importScripts, close, onmessage, onconnect, location, no DOM); module
 *    workers; BroadcastChannel between a page and its workers.
 *
 * Every feature said to be there works: nothing answers "supported" without doing it.
 */
(function (N, W) {
'use strict';

const G = globalThis;
const isWorker = W !== null && W !== undefined;

function report(e) {
	N.log('Uncaught ' + (e && e.stack ? e + '\n' + e.stack : String(e)));
}
/* a task (not a microtask): the event loop's next turn */
function task(fn) { N.timer(() => { try { fn(); } catch (e) { report(e); } }, 0, false); }
function domError(message, name) { return new G.DOMException(message, name); }
function def(obj, props) {
	for (const k of Object.keys(props)) {
		const d = Object.getOwnPropertyDescriptor(props, k);
		d.enumerable = false;
		d.configurable = true;
		Object.defineProperty(obj, k, d);
	}
}
/* an on<type> property as an event listener of its own */
function handlerProperty(obj, type, onSet) {
	const key = Symbol('on' + type);
	Object.defineProperty(obj, 'on' + type, { configurable: true, enumerable: true,
		get() { return this[key] ? this[key].fn : null; },
		set(fn) {
			if (this[key]) this.removeEventListener(type, this[key].l);
			this[key] = null;
			if (typeof fn === 'function') {
				const self = this;
				const l = function (ev) {
					const r = fn.call(self, ev);
					if (r === false) ev.preventDefault();
				};
				this[key] = { fn, l };
				this.addEventListener(type, l);
				if (onSet) onSet.call(this);
			}
		} });
}
function baseURL() {
	try { if (!isWorker && G.document && G.document.baseURI) return G.document.baseURI; } catch (e) { /* */ }
	return G.location.href;
}
const enc = new G.TextEncoder();
const STATUS_TEXT = { 200: 'OK', 201: 'Created', 204: 'No Content', 206: 'Partial Content',
	301: 'Moved Permanently', 302: 'Found', 304: 'Not Modified', 400: 'Bad Request',
	401: 'Unauthorized', 403: 'Forbidden', 404: 'Not Found', 500: 'Internal Server Error',
	502: 'Bad Gateway', 503: 'Service Unavailable' };
/* a Blob's bytes (html5.js' Blob keeps them; dom.js' older one a string) */
function blobBytes(b) { return b._b ? b._b : enc.encode(b._s || ''); }
function concat(parts) {
	let n = 0;
	for (const p of parts) n += p.byteLength;
	const all = new Uint8Array(n);
	let o = 0;
	for (const p of parts) { all.set(p, o); o += p.byteLength; }
	return all.buffer;
}
/* a stream read to its end: its bytes (an ArrayBuffer) */
async function readAll(stream) {
	const r = stream.getReader(), parts = [];
	for (;;) {
		const { value, done } = await r.read();
		if (done) break;
		if (value === undefined || value === null) continue;
		if (typeof value === 'string') parts.push(enc.encode(value));
		else if (value instanceof ArrayBuffer) parts.push(new Uint8Array(value));
		else if (ArrayBuffer.isView(value)) parts.push(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
		else parts.push(enc.encode(String(value)));
	}
	return concat(parts);
}

/* ---- the structured clone between two realms ----------------------------------------------
 * QuickJS's serializer (qjs_net.c) writes objects, arrays, typed arrays and buffers, Map, Set,
 * Date, RegExp, BigInt, the primitive wrappers and cycles; what it does not know is wrapped
 * here (Blob, File, Error, ImageData) and a function, a symbol or a DOM node is refused
 * (DataCloneError), as structuredClone does. */
const WIRE = '__onyxWire';
function cloneError(what) { return domError(what + ' could not be cloned.', 'DataCloneError'); }
function toWire(value, transfer) {
	let special = false;
	const memo = new Map();
	const walk = v => {
		if (v === null || typeof v !== 'object') {
			if (typeof v === 'function') throw cloneError('A function');
			if (typeof v === 'symbol') throw cloneError('A symbol');
			return v;
		}
		if (memo.has(v)) return memo.get(v);
		let r;
		if ((G.Node && v instanceof G.Node) || v === G) throw cloneError('A platform object');
		if (G.Blob && v instanceof G.Blob) {
			special = true;
			r = { [WIRE]: 'blob', b: blobBytes(v).slice(), type: v.type || '',
				file: Object.prototype.toString.call(v) === '[object File]', name: v.name, lm: v.lastModified };
		} else if (v instanceof Error) {
			special = true;
			r = { [WIRE]: 'error', name: v.name, message: v.message, stack: v.stack };
		} else if (G.ImageData && v instanceof G.ImageData) {
			special = true;
			r = { [WIRE]: 'imagedata', data: new Uint8ClampedArray(v.data), w: v.width, h: v.height };
		} else if (v instanceof ArrayBuffer || ArrayBuffer.isView(v) || v instanceof Date ||
				v instanceof RegExp || v instanceof Boolean || v instanceof Number ||
				v instanceof String) {
			r = v;			/* (the serializer's own) */
		} else if (v instanceof Map) {
			r = new Map();
			memo.set(v, r);
			for (const [k, x] of v) r.set(walk(k), walk(x));
			return r;
		} else if (v instanceof Set) {
			r = new Set();
			memo.set(v, r);
			for (const x of v) r.add(walk(x));
			return r;
		} else if (Array.isArray(v)) {
			r = new Array(v.length);
			memo.set(v, r);
			for (const k of Object.keys(v)) r[k] = walk(v[k]);
			return r;
		} else if (v instanceof Promise || v instanceof WeakMap || v instanceof WeakSet ||
				(typeof WeakRef !== 'undefined' && v instanceof WeakRef) ||
				(G.MessagePort && v instanceof G.MessagePort)) {
			throw cloneError(Object.prototype.toString.call(v));
		} else {
			r = {};
			memo.set(v, r);
			for (const k of Object.keys(v)) r[k] = walk(v[k]);
			return r;
		}
		memo.set(v, r);
		return r;
	};
	const out = walk(value);
	/* the buffers transferred are detached (the other side has its copy) */
	const detach = [];
	for (const t of transfer && typeof transfer === 'object' && !Array.isArray(transfer) ? transfer.transfer || [] : transfer || [])
		if (t instanceof ArrayBuffer && typeof t.transfer === 'function') detach.push(t);
	return { value: special ? { [WIRE]: 'root', v: out } : out, detach };
}
function sent(w) { for (const t of w.detach) try { t.transfer(); } catch (e) { /* */ } }
function fromWire(v) {
	if (v === null || typeof v !== 'object' || v[WIRE] !== 'root') return v;
	const memo = new Map();
	const walk = x => {
		if (x === null || typeof x !== 'object') return x;
		if (memo.has(x)) return memo.get(x);
		let r = x;
		switch (x[WIRE]) {
		case 'blob':
			r = x.file ? new G.File([x.b], x.name, { type: x.type, lastModified: x.lm }) : new G.Blob([x.b], { type: x.type });
			break;
		case 'error': {
			const C = { EvalError, RangeError, ReferenceError, SyntaxError, TypeError, URIError }[x.name] || Error;
			r = new C(x.message);
			if (x.stack) r.stack = x.stack;
			break;
		}
		case 'imagedata':
			r = G.ImageData ? new G.ImageData(x.data, x.w, x.h) : x;
			break;
		default:
			memo.set(x, x);
			if (x instanceof Map) {
				const e = [...x];
				x.clear();
				for (const [k, y] of e) x.set(walk(k), walk(y));
			} else if (x instanceof Set) {
				const e = [...x];
				x.clear();
				for (const y of e) x.add(walk(y));
			} else if (Array.isArray(x) || Object.getPrototypeOf(x) === Object.prototype) {
				for (const k of Object.keys(x)) x[k] = walk(x[k]);
			}
			return x;
		}
		memo.set(x, r);
		return r;
	};
	return walk(v.v);
}

/* ---- WebSocket ---------------------------------------------------------------------------- */

class CloseEvent extends G.Event {
	constructor(type, init = {}) {
		super(type, init);
		this.wasClean = !!init.wasClean;
		this.code = init.code === undefined ? 0 : (init.code & 0xffff);
		this.reason = init.reason === undefined ? '' : String(init.reason);
	}
}
G.CloseEvent = CloseEvent;

const WS_TOKEN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
class WebSocket extends G.EventTarget {
	constructor(url, protocols) {
		super();
		if (url === undefined) throw new TypeError("Failed to construct 'WebSocket': 1 argument required");
		let u;
		try { u = new URL(String(url), baseURL()); } catch (e) {
			throw domError("Failed to construct 'WebSocket': The URL '" + url + "' is invalid.", 'SyntaxError');
		}
		let scheme = u.protocol;
		if (scheme === 'http:') scheme = 'ws:';
		else if (scheme === 'https:') scheme = 'wss:';
		if (scheme !== 'ws:' && scheme !== 'wss:')
			throw domError("Failed to construct 'WebSocket': The URL's scheme must be either 'http', 'https', 'ws', or 'wss'.", 'SyntaxError');
		if (u.hash)
			throw domError("Failed to construct 'WebSocket': The URL contains a fragment identifier.", 'SyntaxError');
		const href = scheme + u.href.slice(u.protocol.length);
		const list = protocols === undefined ? [] : typeof protocols === 'string' ? [protocols] :
			Array.from(protocols, String);
		const seen = new Set();
		for (const p of list) {
			if (!WS_TOKEN.test(p) || seen.has(p.toLowerCase()))
				throw domError("Failed to construct 'WebSocket': The subprotocol '" + p + "' is invalid.", 'SyntaxError');
			seen.add(p.toLowerCase());
		}
		Object.defineProperty(this, 'url', { value: href, enumerable: true });
		this._state = 0;
		this._protocol = '';
		this._extensions = '';
		this._binaryType = 'blob';
		this._after = 0;		/* (bytes sent once closing: bufferedAmount) */
		this._origin = scheme + '//' + u.host;
		this._id = N.wsOpen(href, list, (type, a, b, c) => this._event(type, a, b, c));
		if (!this._id)
			task(() => this._fail());
	}
	get readyState() { return this._state; }
	get protocol() { return this._protocol; }
	get extensions() { return this._extensions; }
	get bufferedAmount() { return (this._id ? N.wsBuffered(this._id) : 0) + this._after; }
	get binaryType() { return this._binaryType; }
	set binaryType(v) { if (v === 'blob' || v === 'arraybuffer') this._binaryType = v; }
	send(data) {
		if (this._state === 0)
			throw domError("Failed to execute 'send' on 'WebSocket': Still in CONNECTING state.", 'InvalidStateError');
		let bytes, binary = false, n;
		if (typeof data === 'string') {
			bytes = typeof data.toWellFormed === 'function' ? data.toWellFormed() : data;
			n = -1;
		} else if (data instanceof ArrayBuffer || ArrayBuffer.isView(data)) {
			bytes = data;
			binary = true;
			n = data.byteLength;
		} else if (G.Blob && data instanceof G.Blob) {
			bytes = blobBytes(data);
			binary = true;
			n = bytes.byteLength;
		} else {
			bytes = String(data);
			n = -1;
		}
		if (this._state !== 1) {
			this._after += n >= 0 ? n : enc.encode(bytes).length;
			return;
		}
		N.wsSend(this._id, bytes, binary);
	}
	close(code, reason) {
		if (code !== undefined) {
			code = Number(code) >>> 0 & 0xffff;
			if (code !== 1000 && (code < 3000 || code > 4999))
				throw domError("Failed to execute 'close' on 'WebSocket': The code must be either 1000, or between 3000 and 4999. " + code + ' is neither.', 'InvalidAccessError');
		}
		let r = '';
		if (reason !== undefined) {
			r = String(reason);
			if (enc.encode(r).length > 123)
				throw domError("Failed to execute 'close' on 'WebSocket': The message must not be greater than 123 bytes.", 'SyntaxError');
		}
		if (this._state >= 2) return;
		if (this._state === 0) {		/* (not open yet: the connection failed) */
			this._state = 2;
			if (this._id) N.netAbort(this._id);
			this._id = 0;
			task(() => this._fail());
			return;
		}
		this._state = 2;
		N.wsClose(this._id, code === undefined ? 0 : code, r);
	}
	_fail() {
		this._state = 3;
		this.dispatchEvent(new G.Event('error'));
		this.dispatchEvent(new CloseEvent('close', { wasClean: false, code: 1006, reason: '' }));
	}
	_event(type, a, b, c) {
		switch (type) {
		case 'open':
			if (this._state !== 0) return;
			this._state = 1;
			this._protocol = a || '';
			this._extensions = b || '';
			this.dispatchEvent(new G.Event('open'));
			break;
		case 'message': {
			if (this._state !== 1) return;
			let data = a;
			if (b === true && this._binaryType === 'blob') data = new G.Blob([a]);
			this.dispatchEvent(new G.MessageEvent('message', { data, origin: this._origin }));
			break;
		}
		case 'error':
			this.dispatchEvent(new G.Event('error'));
			break;
		case 'close':
			this._state = 3;
			this._id = 0;
			this.dispatchEvent(new CloseEvent('close', { wasClean: !!c, code: a, reason: b || '' }));
			break;
		}
	}
	get [Symbol.toStringTag]() { return 'WebSocket'; }
}
def(WebSocket, { CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 });
def(WebSocket.prototype, { CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 });
for (const t of ['open', 'message', 'error', 'close'])
	handlerProperty(WebSocket.prototype, t);
G.WebSocket = WebSocket;

/* ---- EventSource (Server-Sent Events) ------------------------------------------------------- */

class EventSource extends G.EventTarget {
	constructor(url, init) {
		super();
		if (url === undefined) throw new TypeError("Failed to construct 'EventSource': 1 argument required");
		let u;
		try { u = new URL(String(url), baseURL()); } catch (e) {
			throw domError("Failed to construct 'EventSource': Cannot open an EventSource to '" + url + "'. The URL is invalid.", 'SyntaxError');
		}
		if (u.protocol !== 'http:' && u.protocol !== 'https:')
			throw domError("Failed to construct 'EventSource': The URL's scheme must be 'http' or 'https'.", 'SyntaxError');
		Object.defineProperty(this, 'url', { value: u.href, enumerable: true });
		Object.defineProperty(this, 'withCredentials', { value: !!(init && init.withCredentials), enumerable: true });
		this._state = 0;
		this._origin = u.origin;
		this._retry = 3000;
		this._last = '';		/* the last event ID string (sent as Last-Event-ID) */
		this._id = 0;
		this._timer = 0;
		task(() => this._connect());
	}
	get readyState() { return this._state; }
	_connect() {
		this._timer = 0;
		if (this._state === 2) return;
		this._buf = '';
		this._data = '';
		this._type = '';
		this._idBuf = this._last;
		this._cr = false;
		this._bom = true;
		const id = this._id = N.sseOpen(this.url, this._last, this.withCredentials,
			(type, a, b) => this._event(id, type, a, b));
		if (!id) this._lost();
	}
	/* the connection lost: again after retry ms (CONNECTING) */
	_lost() {
		if (this._state === 2) return;
		this._id = 0;
		this._state = 0;
		this.dispatchEvent(new G.Event('error'));
		if (this._state === 2) return;
		this._timer = setTimeout(() => this._connect(), this._retry);
	}
	/* a fatal failure: CLOSED, no reconnection */
	_failed() {
		if (this._id) N.netAbort(this._id);
		this._id = 0;
		this._state = 2;
		this.dispatchEvent(new G.Event('error'));
	}
	_event(id, type, a, b) {
		if (id !== this._id || this._state === 2) return;
		switch (type) {
		case 'open':
			if (a !== 200 || !/^text\/event-stream\s*(;|$)/i.test(b || '')) { this._failed(); return; }
			this._state = 1;
			this.dispatchEvent(new G.Event('open'));
			break;
		case 'data':
			this._parse(a);
			break;
		case 'close':
			this._lost();
			break;
		}
	}
	_parse(text) {
		if (this._bom) { this._bom = false; if (text.charCodeAt(0) === 0xFEFF) text = text.slice(1); }
		if (this._cr && text[0] === '\n') text = text.slice(1);	/* (a CRLF cut in two) */
		this._cr = text.endsWith('\r');
		const lines = text.split(/\r\n|\r|\n/);
		lines.pop();			/* (the text ends with a line's end: an empty last one) */
		for (const line of lines) {
			if (this._state === 2 || this._id === 0) return;
			if (line === '') { this._dispatch(); continue; }
			if (line[0] === ':') continue;
			const i = line.indexOf(':');
			const field = i < 0 ? line : line.slice(0, i);
			let value = i < 0 ? '' : line.slice(i + 1);
			if (value[0] === ' ') value = value.slice(1);
			switch (field) {
			case 'event': this._type = value; break;
			case 'data': this._data += value + '\n'; break;
			case 'id': if (value.indexOf('\0') < 0) this._idBuf = value; break;
			case 'retry': if (/^\d+$/.test(value)) this._retry = parseInt(value, 10); break;
			}
		}
	}
	_dispatch() {
		this._last = this._idBuf;
		if (this._data === '') { this._type = ''; return; }
		const data = this._data.slice(0, -1);
		const type = this._type || 'message';
		this._data = '';
		this._type = '';
		this.dispatchEvent(new G.MessageEvent(type, { data, origin: this._origin, lastEventId: this._last }));
	}
	close() {
		if (this._id) N.netAbort(this._id);
		this._id = 0;
		if (this._timer) clearTimeout(this._timer);
		this._timer = 0;
		this._state = 2;
	}
	get [Symbol.toStringTag]() { return 'EventSource'; }
}
def(EventSource, { CONNECTING: 0, OPEN: 1, CLOSED: 2 });
def(EventSource.prototype, { CONNECTING: 0, OPEN: 1, CLOSED: 2 });
for (const t of ['open', 'message', 'error'])
	handlerProperty(EventSource.prototype, t);
G.EventSource = EventSource;

/* ---- fetch: the response as it comes ----------------------------------------------------------
 * The Response is given once its head is in; its body waits for the rest. Response.body is a
 * ReadableStream: from the moment it is read the bytes go to it as they come (N.requestMode:
 * qjs.c's pcb), else text() / json() / arrayBuffer() get the whole body at the end. */
const Resp = G.Response, Req = G.Request, Hdrs = G.Headers;
const NET = Symbol('net');
const hdrLines = h => { const a = []; for (const [k, v] of h) a.push(k + ': ' + v); return a; };

/* the body of a response still coming: its request's id, its parts, its readers */
function netState(id) {
	return { id, done: false, body: null, error: null, waiters: [], stream: null, ctrl: null,
		pend: null, parts: null, followers: 0 };
}
function streamOp(st, op, v) {
	if (!st.stream) return;
	if (!st.ctrl) { if (st.pend) st.pend.push([op, v]); return; }
	try {
		if (op === 'chunk') st.ctrl.enqueue(v);
		else if (op === 'close') st.ctrl.close();
		else st.ctrl.error(v);
	} catch (e) { /* (cancelled) */ }
}
function netChunk(st, ab) {
	if (!st || !ab || !ab.byteLength) return;
	const u8 = new Uint8Array(ab);
	if (st.parts) st.parts.push(u8);
	streamOp(st, 'chunk', u8);
}
function netFinish(st, err, body) {
	if (!st || st.done) return;
	st.done = true;
	st.id = 0;
	if (err) {
		st.error = err;
		streamOp(st, 'error', err);
		for (const w of st.waiters) w[1](err);
	} else {
		if (body == null) body = st.parts ? concat(st.parts) : new ArrayBuffer(0);
		else if (typeof body === 'string') body = enc.encode(body).buffer;
		st.body = body;
		st.parts = null;
		streamOp(st, 'close');
		for (const w of st.waiters) w[0](body);
	}
	st.waiters.length = 0;
}
function netWhole(st) {
	if (st.done) return st.error ? Promise.reject(st.error) : Promise.resolve(st.body);
	return new Promise((a, b) => st.waiters.push([a, b]));
}
function netStream(st, fromWhole) {
	st.pend = [];
	st.stream = new G.ReadableStream({
		start(c) {
			st.ctrl = c;
			const p = st.pend;
			st.pend = null;
			for (const [op, v] of p) streamOp(st, op, v);
		},
		pull: fromWhole ? undefined : undefined,
		cancel() { if (st.id) N.abortRequest(st.id); netFinish(st, new TypeError('cancelled'), null); },
	});
	if (fromWhole) {
		netWhole(fromWhole).then(b => { streamOp(st, 'chunk', new Uint8Array(b.slice(0))); streamOp(st, 'close'); },
			e => streamOp(st, 'error', e));
	} else if (st.done) {
		if (st.error) streamOp(st, 'error', st.error);
		else {
			if (st.body.byteLength) streamOp(st, 'chunk', new Uint8Array(st.body));
			streamOp(st, 'close');
		}
	} else if (st.id) {
		if (st.followers) st.parts = [];	/* (a clone still wants the whole) */
		netChunk(st, N.requestMode(st.id, 1));
	}
	return st.stream;
}

if (Resp) {
	const P = Resp.prototype;
	const bodyDesc = Object.getOwnPropertyDescriptor(P, 'body');
	const origTake = P._take, origClone = P.clone;
	Object.defineProperty(P, 'body', { configurable: true, enumerable: true,
		get() {
			const st = this[NET];
			if (!st) return bodyDesc && bodyDesc.get ? bodyDesc.get.call(this) : null;
			if (this.status === 204 || this.status === 304) return null;
			if (!st.stream) netStream(st, st.follow || null);
			return st.stream;
		},
		set(v) {} });
	P._take = function () {
		const st = this[NET];
		if (!st) return origTake.call(this);
		if (this.bodyUsed) return Promise.reject(new TypeError('Body has already been consumed.'));
		if (st.stream && st.stream.locked) return Promise.reject(new TypeError('Body is locked.'));
		this.bodyUsed = true;
		if (st.stream) return readAll(st.stream);
		if (st.follow) return netWhole(st.follow).then(b => b.slice(0));
		return netWhole(st);
	};
	P.clone = function () {
		const st = this[NET];
		if (!st) return origClone.call(this);
		if (this.bodyUsed || (st.stream && st.stream.locked)) throw new TypeError('Body has already been consumed.');
		const c = new Resp(null, { status: this.status, statusText: this.statusText, headers: new Hdrs(this.headers) });
		c.url = this.url;
		c.redirected = this.redirected;
		c.type = this.type;
		const cs = netState(0);
		if (st.stream) {
			const [a, b] = st.stream.tee();
			st.stream = a;
			cs.stream = b;
		} else {
			const root = st.follow || st;
			root.followers++;
			cs.follow = root;
		}
		c[NET] = cs;
		return c;
	};
	/* (a binary body stays binary: dom.js made its Blob from the text) */
	P.blob = function () {
		return this.arrayBuffer().then(b => new G.Blob([b], { type: this.headers.get('content-type') || '' }));
	};
}

/* ---- CORS (Onyx): the scripts' cross-origin requests, as the Fetch Standard says ----------------
 * A request from the page's origin to another (http / https) is a CORS request (fetch's `mode`
 * 'cors', XMLHttpRequest): it carries an Origin header; one that is not "simple" (a method other
 * than GET / HEAD / POST, a header not CORS-safelisted, a Content-Type other than the three form
 * ones) is preceded by a preflight (OPTIONS with Access-Control-Request-Method / -Headers, its
 * answer cached for its Access-Control-Max-Age); the answer is given to the script only when its
 * Access-Control-Allow-Origin names the page's origin (or "*" without credentials, and
 * Access-Control-Allow-Credentials: true with them), and the script sees its CORS-safelisted
 * headers and those of Access-Control-Expose-Headers only (Response.type 'cors'). The
 * credentials (cookies): fetch's `credentials` ('same-origin' by default: none to another
 * origin; 'include'; 'omit'), XHR's withCredentials -- without them the fetcher sends no Cookie
 * and keeps no Set-Cookie (X-Onyx-Credentials: omit). `mode` 'same-origin' refuses another
 * origin; 'no-cors' allows only a simple request and gives an opaque response (status 0, no
 * headers, no body). Same-origin requests, file:, data: and blob: are as before. */
const CORS_SAFE_HEADERS = new Set(['accept', 'accept-language', 'content-language', 'content-type', 'range']);
const CORS_SAFE_TYPES = new Set(['application/x-www-form-urlencoded', 'multipart/form-data', 'text/plain']);
const CORS_SAFE_RESPONSE = new Set(['cache-control', 'content-language', 'content-length', 'content-type',
	'expires', 'last-modified', 'pragma']);
const corsPreflights = new Map();		/* key -> expiry (Date.now() ms) */

function pageOrigin() {
	try { return new URL(G.location.href).origin; } catch (e) { return 'null'; }
}
/* the request's CORS state: null when it is not cross-origin (or not http(s)) */
function corsOf(url) {
	let u;
	try { u = new URL(url); } catch (e) { return null; }
	if (u.protocol !== 'http:' && u.protocol !== 'https:') return null;
	const origin = pageOrigin();
	if (u.origin === origin) return null;
	return { origin };
}
function corsUnsafeHeaders(h) {
	const names = [];
	for (const [k, v] of h) {
		const n = k.toLowerCase();
		if (!CORS_SAFE_HEADERS.has(n) || String(v).length > 128) { names.push(n); continue; }
		if (n === 'content-type' && !CORS_SAFE_TYPES.has(String(v).split(';')[0].trim().toLowerCase()))
			names.push(n);
		else if (n === 'range' && !/^bytes=\d+-\d*$/.test(String(v).trim()))
			names.push(n);
	}
	return names.sort();
}
const corsSimpleMethod = m => m === 'GET' || m === 'HEAD' || m === 'POST';
/* a response head's [[name, value]...] -> a lower-cased name -> value lookup */
function headGet(headers, name) {
	const v = [];
	for (const [k, x] of headers || []) if (String(k).toLowerCase() === name) v.push(String(x));
	return v.length ? v.join(', ') : null;
}
/* the CORS check of a response (its head's headers): true when the script may read it */
function corsAllows(headers, origin, cred) {
	const acao = headGet(headers, 'access-control-allow-origin');
	if (acao == null) return false;
	const a = acao.trim();
	if (a === '*') return !cred;
	if (a !== origin) return false;
	return !cred || (headGet(headers, 'access-control-allow-credentials') || '').trim() === 'true';
}
/* the headers a script sees of a CORS response */
function corsFilter(headers, cred) {
	const expose = new Set();
	let all = false;
	for (const n of (headGet(headers, 'access-control-expose-headers') || '').split(',')) {
		const t = n.trim().toLowerCase();
		if (t === '*') all = !cred; else if (t) expose.add(t);
	}
	return (headers || []).filter(([k]) => {
		const n = String(k).toLowerCase();
		if (n === 'set-cookie' || n === 'set-cookie2') return false;
		return all || CORS_SAFE_RESPONSE.has(n) || expose.has(n);
	});
}
/* a preflight when the request needs one: resolves when the server allows it */
function corsPreflight(method, url, h, origin, cred) {
	const unsafe = corsUnsafeHeaders(h);
	if (corsSimpleMethod(method) && unsafe.length === 0) return Promise.resolve();
	const key = origin + ' ' + (cred ? 'c ' : '- ') + method + ' ' + url + ' ' + unsafe.join(',');
	const exp = corsPreflights.get(key);
	if (exp !== undefined && exp > Date.now()) return Promise.resolve();
	return new Promise((resolve, reject) => {
		const lines = ['Origin: ' + origin, 'Access-Control-Request-Method: ' + method,
			'X-Onyx-Credentials: omit', 'X-Onyx-Mode: cors', 'Accept: */*'];
		if (unsafe.length) lines.push('Access-Control-Request-Headers: ' + unsafe.join(','));
		const id = N.request('OPTIONS', url, null, lines, false, (err, r) => {
			if (err != null || !r || r.status < 200 || r.status > 299 ||
			    !corsAllows(r.headers, origin, cred)) {
				reject(new TypeError('Failed to fetch (CORS preflight)'));
				return;
			}
			const list = n => (headGet(r.headers, n) || '').split(',').map(s => s.trim()).filter(s => s);
			const methods = list('access-control-allow-methods');
			const hdrs = list('access-control-allow-headers').map(s => s.toLowerCase());
			const star = !cred;
			if (!corsSimpleMethod(method) && !methods.includes(method) && !(star && methods.includes('*'))) {
				reject(new TypeError('Failed to fetch (CORS: method not allowed)'));
				return;
			}
			for (const n of unsafe)
				if (!hdrs.includes(n) && !(star && hdrs.includes('*') && n !== 'authorization')) {
					reject(new TypeError('Failed to fetch (CORS: header ' + n + ' not allowed)'));
					return;
				}
			const ma = parseInt(headGet(r.headers, 'access-control-max-age') || '5', 10);
			corsPreflights.set(key, Date.now() + Math.min(isNaN(ma) ? 5 : Math.max(ma, 0), 7200) * 1000);
			resolve();
		});
		if (id < 0) reject(new TypeError('Failed to fetch'));
	});
}

function streamingFetch(input, init) {
	init = init || {};
	if (G.ReadableStream && init.body instanceof G.ReadableStream) {
		/* (a request body as a stream: read whole, then sent) */
		return readAll(init.body).then(b => streamingFetch(input, Object.assign({}, init, { body: b })));
	}
	let req;
	try { req = new Req(input, init); } catch (e) { return Promise.reject(e); }
	/* Onyx: CORS -- the mode, the credentials, the preflight */
	const cors = corsOf(req.url), mode = req.mode || 'cors';
	const cred = req.credentials === 'include' || (!cors && req.credentials !== 'omit');
	const lines = hdrLines(req.headers);
	if (!cred) lines.push('X-Onyx-Credentials: omit');
	if (cors) {
		if (mode === 'same-origin')
			return Promise.reject(new TypeError('Failed to fetch (mode: same-origin)'));
		if (mode === 'no-cors') {
			if (!corsSimpleMethod(req.method))
				return Promise.reject(new TypeError('Failed to fetch (mode: no-cors)'));
			lines.push('X-Onyx-Mode: no-cors');
			cors.opaque = true;
		} else {
			lines.push('Origin: ' + cors.origin);
			return corsPreflight(req.method, req.url, req.headers, cors.origin, cred)
				.then(() => netFetch(req, lines, cors, cred));
		}
	}
	return netFetch(req, lines, cors, cred);
}
function netFetch(req, lines, cors, cred) {
	return new Promise((resolve, reject) => {
		const sig = req.signal;
		const aborted = () => sig.reason !== undefined ? sig.reason : domError('The operation was aborted.', 'AbortError');
		if (sig.aborted) { reject(aborted()); return; }
		let id = 0, res = null, st = null;
		const head = h => {
			if (cors && cors.opaque) {		/* (no-cors: an opaque response) */
				res = new Resp(null, { status: 0 });
				res.type = 'opaque';
				if (id > 0) N.abortRequest(id);
				id = 0;
				resolve(res);
				return false;
			}
			if (cors && !corsAllows(h.headers, cors.origin, cred)) {
				if (id > 0) N.abortRequest(id);
				id = 0;
				reject(new TypeError('Failed to fetch (CORS: no Access-Control-Allow-Origin for ' +
					cors.origin + ')'));
				return false;
			}
			res = new Resp(null, { status: h.status, statusText: h.statusText || STATUS_TEXT[h.status] || '',
				headers: cors ? corsFilter(h.headers, cred) : h.headers });
			res.url = h.url;
			res.redirected = h.url.replace(/#.*$/, '') !== req.url.replace(/#.*$/, '');
			if (cors) res.type = 'cors';
			st = netState(id);
			res[NET] = st;
			resolve(res);
			return true;
		};
		const onAbort = () => {
			const e = aborted();
			if (id > 0) N.abortRequest(id);
			id = 0;
			if (!res) reject(e);
			else netFinish(st, e, null);
		};
		id = N.request(req.method, req.url, req._body, lines, true, (err, r) => {
			sig.removeEventListener('abort', onAbort);
			if (id === 0) return;			/* (aborted) */
			if (err != null) {
				id = 0;
				const e = new TypeError('Failed to fetch');
				if (!res) reject(e);
				else netFinish(st, e, null);
				return;
			}
			if (!res && !head(r)) return;
			id = 0;
			netFinish(st, null, r.body);
		}, (kind, v) => {
			if (id === 0) return;
			if (kind === 0) { if (!res) head(v); }
			else if (kind === 1) netChunk(st, v);
		}, 0);
		if (id < 0) { id = 0; reject(new TypeError('Failed to fetch')); return; }
		sig.addEventListener('abort', onAbort);
	});
}
{
	const prevFetch = G.fetch;		/* (html5.js': blob: URLs, then dom.js') */
	G.fetch = function fetch(input, init) {
		const u = typeof input === 'string' ? input : input && input.url !== undefined ? input.url : String(input);
		if (/^blob:/i.test(u) || !Req || !Resp) return prevFetch.call(this, input, init);
		return streamingFetch(input, init);
	};
}

/* ---- XMLHttpRequest: HEADERS_RECEIVED, LOADING, the progress with the bytes come ------------- */
if (G.XMLHttpRequest && Req) {
	const X = G.XMLHttpRequest.prototype;
	const absURL = u => { try { return new URL(String(u), baseURL()).href; } catch (e) { return String(u); } };
	const upEvent = (x, t, n) => {
		const ev = new G.ProgressEvent(t, { lengthComputable: n > 0, loaded: n, total: n });
		try { x.upload.dispatchEvent(ev); } catch (e) { report(e); }
		const h = x.upload['on' + t];
		if (typeof h === 'function') try { h.call(x.upload, ev); } catch (e) { report(e); }
	};
	X.send = function send(body = null) {
		if (this.readyState !== 1 || this._sent)
			throw domError('The object is in an invalid state.', 'InvalidStateError');
		this._sent = true;
		const h = new Hdrs();
		for (const [k, v] of this._rh) h.append(k, v);
		const noBody = this._m === 'GET' || this._m === 'HEAD';
		const rq = new Req(absURL(this._u), { method: noBody ? 'GET' : 'POST', headers: h, body: noBody ? null : body });
		const b = noBody ? null : rq._body;
		const bin = this.responseType === 'arraybuffer' || this.responseType === 'blob';
		this._headSeen = false;
		this._loaded = 0;
		this._total = 0;
		this._upN = b != null ? enc.encode(b).length : 0;
		/* Onyx: CORS -- withCredentials, the Origin, the preflight, the check of the answer */
		const cors = corsOf(rq.url), cred = !cors || !!this.withCredentials;
		const lines = hdrLines(rq.headers);
		this._cors = cors;
		this._cred = cred;
		if (!cred) lines.push('X-Onyx-Credentials: omit');
		if (cors) lines.push('Origin: ' + cors.origin);
		this._fire('loadstart');
		if (this._upN) upEvent(this, 'loadstart', 0);
		const go = () => {
			const id = N.request(this._m, rq.url, b, lines, bin,
				(err, r) => this._done(id, err, r),
				(kind, v) => { if (this._id === id) { if (kind === 0) this._head(v); else if (kind === 2) this._progress(v); } },
				2);
			this._id = id;
			if (id < 0) { this._id = 0; setTimeout(() => this._fail('error'), 0); return; }
			if (this.timeout > 0)
				this._to = setTimeout(() => {
					if (this._id !== id) return;
					N.abortRequest(id); this._id = 0; this._fail('timeout');
				}, this.timeout);
		};
		if (!cors) { go(); return; }
		const gen = this._gen = (this._gen || 0) + 1;
		corsPreflight(this._m, rq.url, rq.headers, cors.origin, cred).then(() => {
			if (this._gen === gen && this._sent && this.readyState !== 0) go();
		}, () => {
			if (this._gen === gen && this._sent && this.readyState !== 0) this._fail('error');
		});
	};
	X._head = function (h) {
		if (this._headSeen) return;
		if (this._cors && !corsAllows(h.headers, this._cors.origin, this._cred)) {
			/* Onyx: CORS -- the answer not for this origin: a network error */
			const id = this._id;
			this._id = 0;
			if (id > 0) N.abortRequest(id);
			if (this._to) { clearTimeout(this._to); this._to = 0; }
			this._fail('error');
			return;
		}
		if (this._cors) h = Object.assign({}, h, { headers: corsFilter(h.headers, this._cred) });
		this._headSeen = true;
		if (this._upN) {
			upEvent(this, 'progress', this._upN);
			upEvent(this, 'load', this._upN);
			upEvent(this, 'loadend', this._upN);
		}
		this.status = h.status;
		this.statusText = h.statusText || STATUS_TEXT[h.status] || '';
		this.responseURL = h.url;
		this._h = h.headers;
		const ce = this.getResponseHeader('content-encoding'), cl = this.getResponseHeader('content-length');
		this._total = !ce && cl && /^\d+$/.test(cl.trim()) ? +cl : 0;
		this._state(2);
	};
	X._progress = function (n) {
		if (!this._headSeen || n === this._loaded) return;
		this._loaded = n;
		this._state(3);
		this._fire('progress', n, this._total);
	};
	const origDone = X._done;
	X._done = function (id, err, r) {
		if (this._id !== id) return;
		if (err == null && r && this._cors) {		/* (Onyx: CORS) */
			if (!this._headSeen && !corsAllows(r.headers, this._cors.origin, this._cred))
				err = 'CORS';
			else
				r = Object.assign({}, r, { headers: corsFilter(r.headers, this._cred) });
		}
		if (err != null || !this._headSeen) {
			if (err == null && this._upN) { upEvent(this, 'progress', this._upN); upEvent(this, 'load', this._upN); upEvent(this, 'loadend', this._upN); }
			return origDone.call(this, id, err, r);
		}
		this._id = 0;
		if (this._to) { clearTimeout(this._to); this._to = 0; }
		this.status = r.status;
		this.statusText = r.statusText || STATUS_TEXT[r.status] || '';
		this.responseURL = r.url;
		this._h = r.headers;
		this._res = r.body;
		const n = r.body == null ? 0 : typeof r.body === 'string' ? r.body.length : r.body.byteLength;
		if (this.readyState !== 3 || n !== this._loaded) {
			this._loaded = n;
			this._state(3);
			this._fire('progress', n, this._total || n);
		}
		this._state(4);
		this._fire('load', n, this._total || n);
		this._fire('loadend', n, this._total || n);
	};
	const rt = Object.getOwnPropertyDescriptor(X, 'responseText');
	if (rt && rt.get)
		Object.defineProperty(X, 'responseText', { configurable: true, enumerable: true,
			get() {
				if (this.readyState === 3 && this._id > 0 && (this.responseType === '' || this.responseType === 'text')) {
					const s = N.requestSoFar(this._id);
					if (s != null) return s;
				}
				return rt.get.call(this);
			} });
}

/* ---- workers: the page's side -------------------------------------------------------------------- */

function errorEvent(info, error) {
	const ev = new G.ErrorEvent('error', { message: info.message || '', error, cancelable: true });
	ev.filename = info.filename || '';
	ev.lineno = info.lineno || 0;
	ev.colno = info.colno || 0;
	return ev;
}

/* the script fetched, then the worker's context made (its messages queued meanwhile) */
function startWorker(obj, url, name, type, kind) {
	G.fetch(url).then(r => {
		if (!r.ok && r.status !== 0) throw new Error('the script could not be loaded (' + r.status + ')');
		return r.text();
	}).then(src => {
		if (obj._dead) return;
		const id = N.workerNew(url, name, type, kind, src, (k, data, port) => obj._recv(k, data, port));
		if (!id) throw new Error('the worker could not be made');
		obj._id = id;
		for (const [w, k, p] of obj._q) N.workerPost(id, w, k, p);
		obj._q = [];
	}).catch(e => {
		if (!obj._dead) obj._error({ message: String(e && e.message || e), filename: url });
	});
}
function workerURL(url, what) {
	if (url === undefined) throw new TypeError("Failed to construct '" + what + "': 1 argument required");
	try { return new URL(String(url), baseURL()).href; } catch (e) {
		throw domError("Failed to construct '" + what + "': Script at '" + url + "' cannot be accessed.", 'SyntaxError');
	}
}

class Worker extends G.EventTarget {
	constructor(url, opts) {
		super();
		const href = workerURL(url, 'Worker');
		opts = opts || {};
		this._id = 0;
		this._q = [];
		this._dead = false;
		startWorker(this, href, opts.name === undefined ? '' : String(opts.name),
			opts.type === 'module' ? 'module' : 'classic', 'dedicated');
	}
	postMessage(message, transfer) {
		const w = toWire(message, transfer);
		if (this._dead) return;
		if (this._id) N.workerPost(this._id, w.value, 0, 0);
		else this._q.push([w.value, 0, 0]);
		sent(w);
	}
	terminate() {
		this._dead = true;
		if (this._id) N.workerTerminate(this._id);
		this._id = 0;
		this._q = [];
	}
	_recv(kind, data) {
		if (this._dead) return;
		if (kind === 0) this.dispatchEvent(new G.MessageEvent('message', { data: fromWire(data) }));
		else if (kind === 4) this.dispatchEvent(new G.MessageEvent('messageerror', { data: null }));
		else if (kind === 1) this._error(data || {});
	}
	_error(info) {
		const ev = errorEvent(info, null);
		this.dispatchEvent(ev);
		if (!ev.defaultPrevented) N.log('Uncaught (in worker) ' + (info.message || 'error'));
	}
	get [Symbol.toStringTag]() { return 'Worker'; }
}
for (const t of ['message', 'messageerror', 'error'])
	handlerProperty(Worker.prototype, t);

/* a port of a SharedWorker (the page's end) */
class SharedPort extends G.EventTarget {
	constructor(entry, pid) {
		super();
		this._e = entry;
		this._pid = pid;
		this._started = false;
		this._closed = false;
		this._q = [];
	}
	postMessage(message, transfer) {
		const w = toWire(message, transfer);
		if (this._closed) return;
		sharedSend(this._e, w.value, 0, this._pid);
		sent(w);
	}
	start() {
		if (this._started) return;
		this._started = true;
		const q = this._q;
		this._q = [];
		for (const ev of q) this.dispatchEvent(ev);
	}
	close() { this._closed = true; }
	_deliver(ev) {
		if (this._closed) return;
		if (this._started) this.dispatchEvent(ev);
		else this._q.push(ev);
	}
	get [Symbol.toStringTag]() { return 'MessagePort'; }
}
handlerProperty(SharedPort.prototype, 'message', function () { this.start(); });
handlerProperty(SharedPort.prototype, 'messageerror');

const sharedWorkers = new Map();	/* url + name -> the worker's entry (this document's) */
function sharedSend(e, v, kind, port) {
	if (e._dead) return;
	if (e._id) N.workerPost(e._id, v, kind, port);
	else e._q.push([v, kind, port]);
}
class SharedWorker extends G.EventTarget {
	constructor(url, opts) {
		super();
		const href = workerURL(url, 'SharedWorker');
		const name = typeof opts === 'string' ? opts : opts && opts.name !== undefined ? String(opts.name) : '';
		const type = opts && typeof opts === 'object' && opts.type === 'module' ? 'module' : 'classic';
		const key = href + '\n' + name;
		let e = sharedWorkers.get(key);
		if (!e) {
			e = { _id: 0, _q: [], _dead: false, ports: new Map(), next: 0,
				_recv(kind, data, port) {
					const p = this.ports.get(port);
					if (kind === 1) {
						for (const x of this.ports.values()) {
							const ev = errorEvent(data || {}, null);
							x.sw.dispatchEvent(ev);
						}
						return;
					}
					if (!p) return;
					if (kind === 0) p.port._deliver(new G.MessageEvent('message', { data: fromWire(data) }));
					else if (kind === 4) p.port._deliver(new G.MessageEvent('messageerror', { data: null }));
				},
				_error(info) {
					for (const x of this.ports.values()) x.sw.dispatchEvent(errorEvent(info, null));
				} };
			sharedWorkers.set(key, e);
			startWorker(e, href, name, type, 'shared');
		}
		const pid = ++e.next;
		const port = new SharedPort(e, pid);
		e.ports.set(pid, { port, sw: this });
		Object.defineProperty(this, 'port', { value: port, enumerable: true });
		sharedSend(e, undefined, 2, pid);	/* (the worker's connect) */
	}
	get [Symbol.toStringTag]() { return 'SharedWorker'; }
}
handlerProperty(SharedWorker.prototype, 'error');

G.Worker = Worker;
G.SharedWorker = SharedWorker;

/* ---- BroadcastChannel: the page's and its workers' ------------------------------------------------ */
let bcDeliver = () => {};
if (G.BroadcastChannel) {
	const BC = G.BroadcastChannel, origPost = BC.prototype.postMessage;
	let relaying = false;
	BC.prototype.postMessage = function postMessage(message) {
		origPost.call(this, message);		/* (this context's channels; closed: throws) */
		if (!relaying) N.broadcast(this.name, toWire(message).value);
	};
	bcDeliver = (name, data) => {
		const tmp = new BC(name);
		relaying = true;
		try { origPost.call(tmp, fromWire(data)); } finally { relaying = false; tmp.close(); }
	};
}

if (!isWorker) {
	N.netHook((kind, data, port, name) => { if (kind === 3) bcDeliver(name, data); });
	return;
}

/* ---- a worker's global scope ------------------------------------------------------------------------ */

const shared = W.kind === 'shared';
{
	/* no DOM in a worker */
	const drop = ['document', 'window', 'localStorage', 'sessionStorage', 'alert', 'confirm',
		'prompt', 'print', 'open', 'history', 'screen', 'frames', 'parent', 'top', 'opener',
		'frameElement', 'customElements', 'getComputedStyle', 'matchMedia', 'scroll', 'scrollTo',
		'scrollBy', 'getSelection', 'visualViewport', 'innerWidth', 'innerHeight', 'outerWidth',
		'outerHeight', 'scrollX', 'scrollY', 'pageXOffset', 'pageYOffset', 'screenX', 'screenY',
		'devicePixelRatio', 'IntersectionObserver', 'ResizeObserver', 'MutationObserver',
		'DOMParser', 'XMLSerializer', 'Image', 'Audio', 'Option', 'CSS', 'postMessage',
		'onmessage', 'onmessageerror', 'onerror', 'onload', 'onscroll', 'onresize', 'frames',
		'webkitRequestAnimationFrame', 'external', 'clientInformation', 'styleMedia'];
	const dom = /^(HTML\w*|SVG\w*|MathML\w*)Element$|^(Node|Element|Document|DocumentFragment|Text|Comment|CharacterData|Attr|NodeList|HTMLCollection|DocumentType|ShadowRoot|Range|StaticRange|AbstractRange|Selection|TreeWalker|NodeIterator|NodeFilter|NamedNodeMap|DOMTokenList|CSSStyleDeclaration|StyleSheet|CSSStyleSheet|CSSRule|MediaQueryList|ProcessingInstruction|CDATASection|XMLDocument|HTMLDocument|Window|DOMImplementation|Location|History|Screen|Storage)$/;
	for (const k of Object.getOwnPropertyNames(G))
		if (dom.test(k)) drop.push(k);
	for (const k of drop) {
		try { delete G[k]; } catch (e) { /* */ }
	}
}

class WorkerGlobalScope extends G.EventTarget {}
class DedicatedWorkerGlobalScope extends WorkerGlobalScope {}
class SharedWorkerGlobalScope extends WorkerGlobalScope {}
G.WorkerGlobalScope = WorkerGlobalScope;
if (shared) G.SharedWorkerGlobalScope = SharedWorkerGlobalScope;
else G.DedicatedWorkerGlobalScope = DedicatedWorkerGlobalScope;
try {
	Object.setPrototypeOf(G, (shared ? SharedWorkerGlobalScope : DedicatedWorkerGlobalScope).prototype);
} catch (e) { /* */ }
G.self = G;
Object.defineProperty(G, 'name', { value: W.name, writable: true, configurable: true, enumerable: true });

/* an exception of the worker's code: the worker's onerror, then the Worker's */
function workerError(e) {
	const info = { message: 'Uncaught ' + (e && e.name && e.message !== undefined ? e.name + ': ' + e.message : String(e)),
		filename: W.url, lineno: 0, colno: 0 };
	if (e && typeof e.stack === 'string') {
		const m = /:(\d+):?(\d+)?\)?\s*$/m.exec(e.stack.split('\n')[1] || '');
		if (m) { info.lineno = +m[1]; info.colno = +(m[2] || 0); }
	}
	const ev = errorEvent(info, e);
	try { G.dispatchEvent(ev); } catch (x) { /* */ }
	report(e);
	if (!ev.defaultPrevented) N.workerPostParent(info, 1, 0);
}
/* the listeners of message / connect: their exceptions reported as the worker's errors */
{
	const add = G.addEventListener, remove = G.removeEventListener, wrapped = new WeakMap();
	const wrap = fn => {
		let w = wrapped.get(fn);
		if (!w) {
			w = function (ev) { try { return fn.call(this, ev); } catch (e) { workerError(e); } };
			wrapped.set(fn, w);
		}
		return w;
	};
	G.addEventListener = function addEventListener(type, fn, opts) {
		if ((type === 'message' || type === 'connect') && typeof fn === 'function') fn = wrap(fn);
		return add.call(this, type, fn, opts);
	};
	G.removeEventListener = function removeEventListener(type, fn, opts) {
		if (typeof fn === 'function' && wrapped.has(fn)) fn = wrapped.get(fn);
		return remove.call(this, type, fn, opts);
	};
}
for (const t of shared ? ['connect', 'error'] : ['message', 'messageerror', 'error'])
	handlerProperty(G, t);

G.close = function close() { N.workerClose(); };
if (!shared)
	G.postMessage = function postMessage(message, transfer) {
		const w = toWire(message, transfer);
		N.workerPostParent(w.value, 0, 0);
		sent(w);
	};

/* importScripts: the sources fetched before the script ran (its literal URLs), else read now */
const preloaded = new Map();
G.importScripts = function importScripts(...urls) {
	for (const u of urls) {
		let abs;
		try { abs = new URL(String(u), G.location.href).href; } catch (e) {
			throw domError("Failed to execute 'importScripts': The URL '" + u + "' is invalid.", 'SyntaxError');
		}
		let src = preloaded.get(abs);
		if (src === undefined) {
			src = N.importSync(abs);
			if (src == null)
				throw domError("Failed to execute 'importScripts': The script at '" + abs + "' failed to load.", 'NetworkError');
		}
		N.evalScript(src, abs);
	}
};

/* the ports of a shared worker's connections */
class WorkerPort extends G.EventTarget {
	constructor(pid) {
		super();
		this._pid = pid;
		this._started = false;
		this._closed = false;
		this._q = [];
	}
	postMessage(message, transfer) {
		const w = toWire(message, transfer);
		if (this._closed) return;
		N.workerPostParent(w.value, 0, this._pid);
		sent(w);
	}
	start() {
		if (this._started) return;
		this._started = true;
		const q = this._q;
		this._q = [];
		for (const ev of q) this._run(ev);
	}
	close() { this._closed = true; }
	_run(ev) { try { this.dispatchEvent(ev); } catch (e) { workerError(e); } }
	_deliver(ev) {
		if (this._closed) return;
		if (this._started) this._run(ev);
		else this._q.push(ev);
	}
	get [Symbol.toStringTag]() { return 'MessagePort'; }
}
handlerProperty(WorkerPort.prototype, 'message', function () { this.start(); });
const ports = new Map();

function receive(kind, data, port) {
	if (kind === 2) {
		const p = new WorkerPort(port);
		ports.set(port, p);
		G.dispatchEvent(new G.MessageEvent('connect', { data: '', ports: [p], source: p }));
		return;
	}
	const ev = kind === 0 ? new G.MessageEvent('message', { data: fromWire(data) }) :
		new G.MessageEvent('messageerror', { data: null });
	if (port) {
		const p = ports.get(port);
		if (p) p._deliver(ev);
	} else {
		G.dispatchEvent(ev);
	}
}
let booted = false;
const early = [];
N.netHook((kind, data, port, name) => {
	if (kind === 3) { bcDeliver(name, data); return; }
	if (!booted) { early.push([kind, data, port]); return; }
	receive(kind, data, port);
});

/* a module worker: its graph fetched as the modules ask (N.moduleRun: the missing ones) */
async function runModule(url, src) {
	N.moduleSource(url, src);
	for (let i = 0; i < 30; i++) {
		const r = N.moduleRun(url, src);
		if (!Array.isArray(r)) {
			if (r === false) throw new Error('the module failed');
			return;
		}
		const got = await Promise.all(r.map(u => G.fetch(u).then(x => (x.ok ? x.text() : null)).then(t => {
			if (t === null) return false;
			N.moduleSource(u, t);
			return true;
		}, () => false)));
		if (!got.some(x => x)) throw new TypeError('Failed to fetch a module of ' + url);
	}
}

/* the worker's script, a task after its context was made */
task(async () => {
	try {
		if (W.type === 'module') {
			await runModule(W.url, W.source);
		} else {
			const urls = new Set();
			for (const m of W.source.matchAll(/importScripts\s*\(([^)]*)\)/g))
				for (const s of m[1].matchAll(/(['"`])([^'"`\n]+)\1/g)) {
					try { urls.add(new URL(s[2], W.url).href); } catch (e) { /* */ }
				}
			if (urls.size)
				await Promise.all([...urls].map(u => G.fetch(u).then(r => (r.ok ? r.text() : null))
					.then(t => { if (t !== null) preloaded.set(u, t); }, () => {})));
			N.evalScript(W.source, W.url);
		}
	} catch (e) {
		workerError(e);
	}
	booted = true;
	for (const [k, d, p] of early.splice(0)) receive(k, d, p);
});
})
