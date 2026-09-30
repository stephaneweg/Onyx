/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: the WebAssembly namespace's JavaScript part, run by qjs_wasm.c with the natives N
 * and the namespace WA (its Module, Instance, Memory, Table, Global classes and validate
 * are qjs_wasm.c's, on wasm3):
 *
 *  - CompileError, LinkError, RuntimeError (the errors qjs_wasm.c throws);
 *  - compile / instantiate: the bytes copied and compiled when called, a rejected promise
 *    on an error; the imports read a microtask later (as a browser does once its
 *    compilation is done: a script may still fill the import object after the call);
 *  - compileStreaming / instantiateStreaming from a Response (or a promise of one): an ok
 *    response, 'application/wasm' over http(s) (a file: or data: URL has no type).
 */
(function (N, WA) {
'use strict';

const G = globalThis;

function errorClass(name) {
	const C = class extends Error {
		constructor(message, options) {
			super(message, options);
		}
	};
	Object.defineProperty(C, 'name', { value: name, configurable: true });
	Object.defineProperty(C.prototype, 'name', { value: name, writable: true, configurable: true });
	Object.defineProperty(WA, name, { value: C, writable: true, configurable: true });
	return C;
}
const CompileError = errorClass('CompileError');
const LinkError = errorClass('LinkError');
const RuntimeError = errorClass('RuntimeError');
N.wasmErrors(CompileError, LinkError, RuntimeError);

function def(name, fn) {
	Object.defineProperty(WA, name, { value: fn, writable: true, configurable: true });
}

def('compile', function compile(bytes) {
	try {
		return Promise.resolve(new WA.Module(bytes));
	} catch (e) {
		return Promise.reject(e);
	}
});

def('instantiate', function instantiate(source, imports) {
	if (source instanceof WA.Module)
		return Promise.resolve().then(() => new WA.Instance(source, imports));
	let module;
	try {
		module = new WA.Module(source);
	} catch (e) {
		return Promise.reject(e);
	}
	return Promise.resolve().then(() => ({ module, instance: new WA.Instance(module, imports) }));
});

/* the bytes of a Response (or a promise of one) */
async function responseBytes(source) {
	const r = await source;
	if (typeof G.Response !== 'function' || !(r instanceof G.Response))
		throw new TypeError('WebAssembly: a Response is expected');
	if (!r.ok)
		throw new TypeError('WebAssembly: the response failed (HTTP status ' + r.status + ')');
	const type = (r.headers.get('content-type') || '').split(';')[0].trim().toLowerCase();
	if (type !== 'application/wasm' && /^https?:/i.test(r.url || ''))
		throw new TypeError("WebAssembly: incorrect response MIME type, 'application/wasm' is expected");
	if (r.bodyUsed)
		throw new TypeError('WebAssembly: the response body was already read');
	return new Uint8Array(await r.arrayBuffer());
}

def('compileStreaming', function compileStreaming(source) {
	return responseBytes(source).then(b => new WA.Module(b));
});

def('instantiateStreaming', function instantiateStreaming(source, imports) {
	return responseBytes(source).then(b => {
		const module = new WA.Module(b);
		return Promise.resolve().then(() => ({ module, instance: new WA.Instance(module, imports) }));
	});
});
})
