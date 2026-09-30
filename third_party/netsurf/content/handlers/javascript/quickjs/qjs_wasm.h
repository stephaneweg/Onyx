/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: WebAssembly (qjs_wasm.c, wasm.js, on wasm3) and Web Crypto (qjs_crypto.c,
 * crypto.js, on mbedTLS) for the scripts of a page or a worker.
 */

#ifndef NETSURF_QJS_WASM_H
#define NETSURF_QJS_WASM_H

#include <stdbool.h>
#include "quickjs.h"

/**
 * The WebAssembly namespace (its Module, Instance, Memory, Table, Global classes, in C)
 * made in a context, then wasm.js run with the natives (validate, compile, instantiate,
 * the streaming forms, the error classes).
 */
void qjs_wasm_setup(JSContext *ctx, JSValueConst natives);

/** A context's scripts gone (before it is freed): what its WebAssembly holds of it let go
 * (the store, its runtime and memories, are freed once none of its objects is left). */
void qjs_wasm_context_gone(JSContext *ctx);

/**
 * crypto.getRandomValues / randomUUID from a CTR-DRBG seeded by the best entropy there is
 * (the Pi's hardware RNG, getrandom on the PC bench) and crypto.subtle (crypto.js) on
 * mbedTLS: the natives (N.crypto*) then crypto.js run with them.
 */
void qjs_crypto_setup(JSContext *ctx, JSValueConst natives);

/* from qjs.c */

/** Whether the script running in a context has had its time (NetSurf's script_timeout). */
bool qjs_ctx_timed_out(JSContext *ctx);

#endif
