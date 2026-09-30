/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Onyx: WebAssembly for the scripts, on wasm3 (third_party/wasm3-0.9.2: an interpreter,
 * MIT) -- the standard JavaScript API: the WebAssembly namespace, its Module, Instance,
 * Memory, Table and Global classes (here, in C), validate / compile / instantiate and the
 * streaming forms, CompileError / LinkError / RuntimeError (wasm.js, compiled in as
 * qjs_wasm_js.h).
 *
 * A context (a page's or a worker's) has an environment (the function types, its Module
 * objects parsed in it) and at most one store: a wasm3 runtime holding every instance,
 * memory, table and global the context's scripts made while any of them is alive. An
 * instance is a module loaded into the store's runtime under a name of its own; a memory,
 * table or global made by `new` is a small module made here that exports it. An import
 * that is a Memory, a Table or a Global (or a function exported by the same store) is
 * linked by wasm3 itself: the import is renamed to the module and export that hold it.
 * A JavaScript function import is a raw function of wasm3 calling it (qw_host_call).
 *
 * The store is a JavaScript object of its own (hidden) which every other object of it
 * references and which owns the JavaScript values Wasm holds (the imported functions,
 * the externref values): the garbage collector sees the cycles through them, and when
 * nothing of a store is left its runtime -- the memories, the compiled code -- is freed.
 * A memory's buffer is an ArrayBuffer over the linear memory; it holds the store too (a
 * reference in C) and is detached when the memory grows or moves (checked each time Wasm
 * returns to JavaScript: after a call and before an import runs).
 *
 * A runaway module is stopped by m3_Yield (wasm3 calls it at each call): past the page's
 * script_timeout the call traps.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "quickjs.h"

#include "javascript/quickjs/qjs_wasm.h"
#include "javascript/quickjs/qjs_net.h"	/* qjs_eval_cached, qjs_ctx_closed */
#include "qjs_wasm_js.h"	/* wasm.js, as a C string (the build makes it) */

#include "wasm3.h"
#include "m3_env.h"
#include "m3_compile.h"
#include "m3_validate.h"

/** a store's linear memories together (a page's), past this memory.grow fails */
#define QW_STORE_MEMORY (512u * 1024 * 1024)
/** the Wasm value stack of a store (its locals and operands) */
#define QW_STACK (256 * 1024)
#define QW_HASH 256
/** a garbage collection each time the Wasm memories grew this much (qw_pressure) */
#define QW_GC_STEP (64u * 1024 * 1024)

enum { QW_COMPILE, QW_LINK, QW_RUNTIME };

/* the trap a JavaScript import's exception rides out of Wasm on (the exception is kept
 * in the store till the call returns), and the one a script out of its time stops with */
static const char qw_js_exception[] = "[trap] a JavaScript exception";
static const char qw_timeout[] = "[trap] the script ran out of time";

struct qw_blob {			/* a module's bytes (wasm3 reads them while it lives) */
	int refs;
	uint32_t len;
	uint8_t bytes[];
};

struct qw_keep {			/* what a store frees with its runtime */
	struct qw_keep *next;
	struct qw_blob *blob;
	char name[];
};

struct qw_wref {			/* a JavaScript object wrapping a wasm3 entity (weak) */
	struct qw_wref *next;
	void *key;
	JSValue obj;			/* not counted: the object takes itself out when freed */
};

struct qw_import {			/* a JavaScript function a module imports */
	struct qw_import *next;
	struct qw_store *s;
	JSValue fn;			/* owned by the store object (JS_UNDEFINED once it is gone) */
	IM3Function foreign, local;	/* a trampoline: another store's function, its stand-in */
};

struct qw_ctx;

struct qw_store {
	struct qw_store *nexts;		/* all the stores (qw_stores) */
	struct qw_ctx *c;
	IM3Runtime rt;
	int refs;			/* its store object, its wrappers, its memories' buffers */
	JSValue obj;			/* the store object (not counted), JS_UNDEFINED once freed */
	unsigned seq;			/* names its modules */
	struct qw_keep *keep;
	struct qw_wref *wrefs[QW_HASH];
	struct qw_memory *bufs;		/* the memories whose buffer was asked for */
	struct qw_import *imports;
	JSValue *ext;			/* externref values (index 0: null) */
	uint32_t next, capext;
	JSValue extmap;			/* Map: value -> index */
	JSValue exc;			/* a JavaScript import's exception, on its way out */
	bool has_exc;
};

struct qw_ctx {
	struct qw_ctx *next;
	JSContext *ctx;			/* NULL once the context is gone */
	int refs;			/* the context, its Module objects, its store */
	IM3Environment env;
	IM3Runtime scratch;		/* validation only */
	struct qw_store *store;
	JSValue err[3];			/* CompileError, LinkError, RuntimeError (wasm.js) */
};

struct qw_module {
	struct qw_ctx *c;
	struct qw_blob *blob;
	IM3Module m;			/* parsed and validated: what Module.exports... read */
};

struct qw_instance {
	struct qw_store *s;
	JSValue store, exports;
};

struct qw_func {
	struct qw_wref w;
	struct qw_store *s;
	JSValue store;
	IM3Function f;
};

struct qw_memory {
	struct qw_wref w;
	struct qw_store *s;
	JSValue store;
	IM3Memory mem;
	const char *modname, *expname;	/* how an import finds it (the store's strings) */
	JSValue buffer;
	uint8_t *bdata;
	size_t blen;
	struct qw_memory *bnext;
	bool inbufs;
};

struct qw_table {
	struct qw_wref w;
	struct qw_store *s;
	JSValue store;
	IM3Table t;
	const char *modname, *expname;
};

struct qw_global {
	struct qw_wref w;
	struct qw_store *s;
	JSValue store;
	IM3Global g;
	const char *modname, *expname;
};

static JSClassID qw_store_class, qw_module_class, qw_instance_class, qw_func_class,
	qw_memory_class, qw_table_class, qw_global_class;
static struct qw_ctx *qw_ctxs;
static struct qw_store *qw_stores;
static uint8_t *qw_bc;
static size_t qw_bc_len;
static JSContext *qw_running;		/* the context whose Wasm runs (m3_Yield) */
static unsigned qw_yields;
static void qw_debug(const char *what, struct qw_store *s);


/* ---- contexts, stores ------------------------------------------------------------------ */

static struct qw_ctx *qw_ctx_of(JSContext *ctx)
{
	struct qw_ctx *c;

	for (c = qw_ctxs; c != NULL; c = c->next)
		if (c->ctx == ctx)
			return c;
	return NULL;
}

static void qw_ctx_unref(struct qw_ctx *c)
{
	struct qw_ctx **pp;

	if (--c->refs > 0)
		return;
	for (pp = &qw_ctxs; *pp != NULL; pp = &(*pp)->next)
		if (*pp == c) {
			*pp = c->next;
			break;
		}
	if (c->scratch != NULL)
		m3_FreeRuntime(c->scratch);
	m3_FreeEnvironment(c->env);
	free(c);
}

static void qw_blob_unref(struct qw_blob *b)
{
	if (b != NULL && --b->refs == 0)
		free(b);
}

static void qw_store_unref(struct qw_store *s)
{
	struct qw_keep *k;
	struct qw_import *im;
	struct qw_store **pp;

	if (--s->refs > 0)
		return;
	if (s->c->store == s)
		s->c->store = NULL;
	for (pp = &qw_stores; *pp != NULL; pp = &(*pp)->nexts)
		if (*pp == s) {
			*pp = s->nexts;
			break;
		}
	m3_FreeRuntime(s->rt);	/* its modules, memories, tables, compiled code */
	qw_debug("freed", s);
	while ((k = s->keep) != NULL) {
		s->keep = k->next;
		qw_blob_unref(k->blob);
		free(k);
	}
	while ((im = s->imports) != NULL) {
		s->imports = im->next;
		free(im);
	}
	free(s->ext);
	qw_ctx_unref(s->c);
	free(s);
}

/** a string (or a module's bytes) kept as long as the store: the modules' names */
static const char *qw_keep_name(struct qw_store *s, const char *name, struct qw_blob *blob)
{
	size_t n = strlen(name);
	struct qw_keep *k = malloc(sizeof *k + n + 1);

	if (k == NULL)
		return NULL;
	memcpy(k->name, name, n + 1);
	k->blob = blob;
	if (blob != NULL)
		blob->refs++;
	k->next = s->keep;
	s->keep = k;
	return k->name;
}

/* (the PC bench: NS_WASMDEBUG=1 logs the stores made and freed) */
static void qw_debug(const char *what, struct qw_store *s)
{
#ifdef ONYX_HOST_SIM
	static int on = -1;
	struct qw_store *x;
	int n = 0;

	if (on < 0)
		on = getenv("NS_WASMDEBUG") != NULL;
	if (!on)
		return;
	for (x = qw_stores; x != NULL; x = x->nexts)
		n++;
	fprintf(stderr, "wasm: store %p %s, %d live\n", (void *) s, what, n);
#else
	(void) what;
	(void) s;
#endif
}

static unsigned qw_hash(void *key)
{
	return (unsigned) (((uintptr_t) key >> 3) * 2654435761u) & (QW_HASH - 1);
}

static JSValue qw_wref_get(struct qw_store *s, void *key)
{
	struct qw_wref *w;

	for (w = s->wrefs[qw_hash(key)]; w != NULL; w = w->next)
		if (w->key == key)
			return JS_DupValue(s->c->ctx, w->obj);
	return JS_UNDEFINED;
}

static void qw_wref_add(struct qw_store *s, struct qw_wref *w, void *key, JSValueConst obj)
{
	unsigned h = qw_hash(key);

	w->key = key;
	w->obj = obj;
	w->next = s->wrefs[h];
	s->wrefs[h] = w;
}

static void qw_wref_remove(struct qw_store *s, struct qw_wref *w)
{
	struct qw_wref **pp;

	if (w->key == NULL)
		return;
	for (pp = &s->wrefs[qw_hash(w->key)]; *pp != NULL; pp = &(*pp)->next)
		if (*pp == w) {
			*pp = w->next;
			break;
		}
	w->key = NULL;
}

static void qw_store_finalizer(JSRuntime *rt, JSValue val)
{
	struct qw_store *s = JS_GetOpaque(val, qw_store_class);
	struct qw_import *im;
	uint32_t i;

	if (s == NULL)
		return;
	/* nothing of the store can run Wasm any more (they all held this object) */
	for (im = s->imports; im != NULL; im = im->next) {
		JS_FreeValueRT(rt, im->fn);
		im->fn = JS_UNDEFINED;
	}
	for (i = 1; i < s->next; i++)
		JS_FreeValueRT(rt, s->ext[i]);
	s->next = 1;
	JS_FreeValueRT(rt, s->extmap);
	s->extmap = JS_UNDEFINED;
	if (s->has_exc)
		JS_FreeValueRT(rt, s->exc);
	s->has_exc = false;
	s->obj = JS_UNDEFINED;
	if (s->c->store == s)
		s->c->store = NULL;
	qw_store_unref(s);
}

static void qw_store_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark)
{
	struct qw_store *s = JS_GetOpaque(val, qw_store_class);
	struct qw_import *im;
	uint32_t i;

	if (s == NULL)
		return;
	for (im = s->imports; im != NULL; im = im->next)
		JS_MarkValue(rt, im->fn, mark);
	for (i = 1; i < s->next; i++)
		JS_MarkValue(rt, s->ext[i], mark);
	JS_MarkValue(rt, s->extmap, mark);
}

/**
 * Linear memories live outside QuickJS's heap, so its collector does not see them grow:
 * past QW_GC_STEP more bytes of Wasm memory in all the stores since the last collection,
 * one is run (a page making instances and dropping them has them freed).
 */
static void qw_pressure(JSContext *ctx)
{
	static uint64_t at_last_gc;
	uint64_t total = 0;
	struct qw_store *s;

	for (s = qw_stores; s != NULL; s = s->nexts)
		total += m3_GetResourceUsage(s->rt, c_m3Limit_MemoryBytes);
	if (total < at_last_gc)
		at_last_gc = total;
	if (total - at_last_gc > QW_GC_STEP) {
		JS_RunGC(JS_GetRuntime(ctx));
		total = 0;
		for (s = qw_stores; s != NULL; s = s->nexts)
			total += m3_GetResourceUsage(s->rt, c_m3Limit_MemoryBytes);
		at_last_gc = total;
	}
}

/** a new store; *obj: the reference to its object */
static struct qw_store *qw_store_new(JSContext *ctx, struct qw_ctx *c, JSValue *obj)
{
	struct qw_store *s;

	qw_pressure(ctx);
	s = calloc(1, sizeof *s);
	if (s == NULL) {
		JS_ThrowOutOfMemory(ctx);
		return NULL;
	}
	s->rt = m3_NewRuntime(c->env, QW_STACK, s);
	if (s->rt == NULL) {
		free(s);
		JS_ThrowOutOfMemory(ctx);
		return NULL;
	}
	m3_SetResourceLimit(s->rt, c_m3Limit_MemoryBytes, QW_STORE_MEMORY);
	m3_SetValidation(s->rt, false);	/* (every body was validated when compiled) */
	s->c = c;
	c->refs++;
	s->refs = 1;
	s->next = 1;
	s->extmap = JS_UNDEFINED;
	s->nexts = qw_stores;
	qw_stores = s;
	qw_debug("made", s);
	s->obj = JS_NewObjectClass(ctx, qw_store_class);
	if (JS_IsException(s->obj)) {
		s->obj = JS_UNDEFINED;
		qw_store_unref(s);
		return NULL;
	}
	JS_SetOpaque(s->obj, s);
	*obj = s->obj;	/* (the new object's reference) */
	return s;
}

/** the context's store for what `new Memory / Table / Global` make (made if it has
 * none); *obj: a reference to its object */
static struct qw_store *qw_store_get(JSContext *ctx, struct qw_ctx *c, JSValue *obj)
{
	struct qw_store *s = c->store;

	if (s == NULL) {
		s = qw_store_new(ctx, c, obj);
		c->store = s;
		return s;
	}
	*obj = JS_DupValue(ctx, s->obj);
	return s;
}


/* ---- errors ----------------------------------------------------------------------------- */

static JSValue qw_throw(JSContext *ctx, int kind, const char *fmt, ...)
{
	struct qw_ctx *c = qw_ctx_of(ctx);
	char msg[300];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	if (c != NULL && JS_IsFunction(ctx, c->err[kind])) {
		JSValue m = JS_NewString(ctx, msg);
		JSValue e = JS_CallConstructor(ctx, c->err[kind], 1, (JSValueConst *) &m);

		JS_FreeValue(ctx, m);
		if (JS_IsException(e))
			return e;
		return JS_Throw(ctx, e);
	}
	return JS_ThrowTypeError(ctx, "%s", msg);
}

/** wasm3's answer as the exception the standard asks for */
static JSValue qw_throw_result(JSContext *ctx, struct qw_store *s, M3Result r, int kind)
{
	if (r == qw_js_exception && s != NULL && s->has_exc) {
		s->has_exc = false;
		return JS_Throw(ctx, s->exc);
	}
	if (r == qw_timeout)
		return JS_ThrowInternalError(ctx, "interrupted");
	if (r == m3Err_trapStackOverflow)
		return JS_ThrowRangeError(ctx, "Maximum call stack size exceeded");
	if (r == m3Err_mallocFailed || r == m3Err_mallocFailedCodePage)
		return JS_ThrowOutOfMemory(ctx);
	if (strncmp(r, "[trap] ", 7) == 0)
		return qw_throw(ctx, QW_RUNTIME, "%s", r + 7);
	return qw_throw(ctx, kind, "%s", r);
}

static JSValue qw_nonew(JSContext *ctx)
{
	return JS_ThrowTypeError(ctx, "WebAssembly: a constructor must be called with 'new'");
}


/* ---- memories' buffers -------------------------------------------------------------------- */

static uint8_t *qw_mem_data(IM3Memory mem, size_t *len)
{
	*len = mem->mallocated != NULL ? mem->mallocated->length : 0;
	return mem->mallocated != NULL ? (uint8_t *) (mem->mallocated + 1) : NULL;
}

/* an ArrayBuffer over a memory: it holds the store; detached (or freed) it lets go */
static void *qw_buffer_release(JSRuntime *rt, void *opaque, void *ptr, size_t size)
{
	(void) rt;
	(void) ptr;
	if (size != 0)
		return NULL;	/* (not resizable) */
	qw_store_unref(opaque);
	return NULL;
}

static void qw_buffer_drop(JSContext *ctx, struct qw_memory *m)
{
	JSValue b = m->buffer;

	m->buffer = JS_UNDEFINED;
	if (!JS_IsUndefined(b)) {
		JS_DetachArrayBuffer(ctx, b);
		JS_FreeValue(ctx, b);
	}
}

/** Wasm is back to JavaScript: the buffers of the memories that grew or moved detached */
static void qw_refresh(JSContext *ctx, struct qw_store *s)
{
	struct qw_memory *m, **pp = &s->bufs;
	size_t len;
	uint8_t *d;

	while ((m = *pp) != NULL) {
		d = qw_mem_data(m->mem, &len);
		if (d != m->bdata || len != m->blen) {
			qw_buffer_drop(ctx, m);
			*pp = m->bnext;
			m->inbufs = false;
			continue;
		}
		pp = &m->bnext;
	}
}

static void qw_bufs_remove(struct qw_memory *m)
{
	struct qw_memory **pp;

	if (!m->inbufs)
		return;
	for (pp = &m->s->bufs; *pp != NULL; pp = &(*pp)->bnext)
		if (*pp == m) {
			*pp = m->bnext;
			break;
		}
	m->inbufs = false;
}


/* ---- values between JavaScript and Wasm ------------------------------------------------- */

static JSValue qw_func_wrap(JSContext *ctx, struct qw_store *s, JSValueConst sobj, IM3Function f);
static IM3Function qw_trampoline(JSContext *ctx, struct qw_store *s, JSValueConst v,
		struct qw_func *fn);
static const void *qw_host_call(IM3Runtime runtime, IM3ImportContext ic, uint64_t *sp,
		void *mem);

static const char *qw_type_name(uint8_t t)
{
	switch (t) {
	case c_m3Type_i32: return "i32";
	case c_m3Type_i64: return "i64";
	case c_m3Type_f32: return "f32";
	case c_m3Type_f64: return "f64";
	case c_m3Type_funcref: return "anyfunc";
	case c_m3Type_externref: return "externref";
	case c_m3Type_v128: return "v128";
	default: return "exnref";
	}
}

/** a JavaScript value as an externref (0: null) */
static int qw_to_extern(JSContext *ctx, struct qw_store *s, JSValueConst v, void **out)
{
	JSValue r;
	int64_t idx;

	if (JS_IsNull(v)) {
		*out = NULL;
		return 0;
	}
	if (JS_IsUndefined(s->extmap)) {
		JSValue g = JS_GetGlobalObject(ctx);
		JSValue mc = JS_GetPropertyStr(ctx, g, "Map");

		s->extmap = JS_CallConstructor(ctx, mc, 0, NULL);
		JS_FreeValue(ctx, mc);
		JS_FreeValue(ctx, g);
		if (JS_IsException(s->extmap)) {
			s->extmap = JS_UNDEFINED;
			return -1;
		}
	}
	{
		JSValue get = JS_GetPropertyStr(ctx, s->extmap, "get");
		r = JS_Call(ctx, get, s->extmap, 1, &v);
		JS_FreeValue(ctx, get);
	}
	if (JS_IsException(r))
		return -1;
	if (!JS_IsUndefined(r)) {
		JS_ToInt64(ctx, &idx, r);
		JS_FreeValue(ctx, r);
		*out = (void *) (uintptr_t) idx;
		return 0;
	}
	if (s->next >= s->capext) {
		uint32_t cap = s->capext ? s->capext * 2 : 64;
		JSValue *e = realloc(s->ext, cap * sizeof *e);
		if (e == NULL) {
			JS_ThrowOutOfMemory(ctx);
			return -1;
		}
		s->ext = e;
		s->capext = cap;
	}
	idx = s->next;
	s->ext[s->next++] = JS_DupValue(ctx, v);
	{
		JSValue set = JS_GetPropertyStr(ctx, s->extmap, "set");
		JSValue args[2] = { JS_DupValue(ctx, v), JS_NewInt64(ctx, idx) };
		r = JS_Call(ctx, set, s->extmap, 2, (JSValueConst *) args);
		JS_FreeValue(ctx, set);
		JS_FreeValue(ctx, args[0]);
		JS_FreeValue(ctx, args[1]);
		JS_FreeValue(ctx, r);
	}
	*out = (void *) (uintptr_t) idx;
	return 0;
}

/** ToWebAssemblyValue: a JavaScript value into a Wasm slot of type t */
static int qw_to_wasm(JSContext *ctx, struct qw_store *s, uint8_t t, JSValueConst v, uint64_t *slot)
{
	switch (t) {
	case c_m3Type_i32: {
		int32_t x;
		if (JS_ToInt32(ctx, &x, v))
			return -1;
		*slot = 0;
		*(int32_t *) slot = x;
		return 0;
	}
	case c_m3Type_i64: {
		int64_t x;
		if (JS_ToBigInt64(ctx, &x, v))
			return -1;
		*(int64_t *) slot = x;
		return 0;
	}
	case c_m3Type_f32: {
		double d;
		if (JS_ToFloat64(ctx, &d, v))
			return -1;
		*slot = 0;
		*(float *) slot = (float) d;
		return 0;
	}
	case c_m3Type_f64: {
		double d;
		if (JS_ToFloat64(ctx, &d, v))
			return -1;
		*(double *) slot = d;
		return 0;
	}
	case c_m3Type_funcref: {
		struct qw_func *fn;
		if (JS_IsNull(v)) {
			*slot = 0;
			return 0;
		}
		fn = JS_GetOpaque(v, qw_func_class);
		if (fn == NULL) {
			JS_ThrowTypeError(ctx, "WebAssembly: a funcref must be null or a function "
					"exported by WebAssembly");
			return -1;
		}
		if (fn->s != s) {	/* (another instance's store) */
			IM3Function f = qw_trampoline(ctx, s, v, fn);
			if (f == NULL)
				return -1;
			*slot = (uint64_t) (uintptr_t) f;
			return 0;
		}
		*slot = (uint64_t) (uintptr_t) Function_Implementation(fn->f);
		return 0;
	}
	case c_m3Type_externref: {
		void *p;
		if (qw_to_extern(ctx, s, v, &p))
			return -1;
		*slot = (uint64_t) (uintptr_t) p;
		return 0;
	}
	default:
		JS_ThrowTypeError(ctx, "WebAssembly: a %s value cannot cross to JavaScript",
				qw_type_name(t));
		return -1;
	}
}

/** ToJSValue: a Wasm slot of type t as a JavaScript value */
static JSValue qw_to_js(JSContext *ctx, struct qw_store *s, uint8_t t, const uint64_t *slot)
{
	switch (t) {
	case c_m3Type_i32: return JS_NewInt32(ctx, *(const int32_t *) slot);
	case c_m3Type_i64: return JS_NewBigInt64(ctx, *(const int64_t *) slot);
	case c_m3Type_f32: return JS_NewFloat64(ctx, *(const float *) slot);
	case c_m3Type_f64: return JS_NewFloat64(ctx, *(const double *) slot);
	case c_m3Type_funcref: {
		IM3Function f = (IM3Function) (uintptr_t) *slot;
		struct qw_import *im;

		if (f == NULL)
			return JS_NULL;
		for (im = s->imports; im != NULL; im = im->next)	/* (a trampoline) */
			if (im->local == f && !JS_IsUndefined(im->fn))
				return JS_DupValue(ctx, im->fn);
		return qw_func_wrap(ctx, s, s->obj, f);
	}
	case c_m3Type_externref: {
		uintptr_t i = (uintptr_t) *slot;
		if (i == 0)
			return JS_NULL;
		if (i >= s->next)
			return JS_UNDEFINED;
		return JS_DupValue(ctx, s->ext[i]);
	}
	default:
		return JS_ThrowTypeError(ctx, "WebAssembly: a %s value cannot cross to JavaScript",
				qw_type_name(t));
	}
}


/* ---- running Wasm ------------------------------------------------------------------------ */

/* Onyx: wasm3 calls this at each call; past the script's time the call traps */
M3Result m3_Yield(void)
{
	if ((++qw_yields & 0x3fff) == 0 && qw_running != NULL && qjs_ctx_timed_out(qw_running))
		return qw_timeout;
	return m3Err_none;
}

/** a JavaScript function a module imports, called from Wasm (a raw function of wasm3) */
static const void *qw_host_call(IM3Runtime runtime, IM3ImportContext ic, uint64_t *sp, void *mem)
{
	struct qw_import *im = ic->userdata;
	struct qw_store *s = im->s;
	JSContext *ctx = s->c->ctx;
	IM3FuncType t = ic->function->funcType;
	JSValue argv[16], *args = argv, r;
	unsigned i, nargs = t->numArgs, nrets = t->numRets;

	(void) runtime;
	(void) mem;
	if (ctx == NULL || qjs_ctx_closed(ctx) || JS_IsUndefined(im->fn))
		return m3Err_trapAbort;
	qw_refresh(ctx, s);
	if (nargs > 16) {
		args = malloc(nargs * sizeof *args);
		if (args == NULL)
			return m3Err_mallocFailed;
	}
	for (i = 0; i < nargs; i++) {
		args[i] = qw_to_js(ctx, s, t->types[nrets + i], &sp[nrets + i]);
		if (JS_IsException(args[i])) {
			while (i-- > 0)
				JS_FreeValue(ctx, args[i]);
			goto exc;
		}
	}
	r = JS_Call(ctx, im->fn, JS_UNDEFINED, nargs, (JSValueConst *) args);
	for (i = 0; i < nargs; i++)
		JS_FreeValue(ctx, args[i]);
	if (args != argv)
		free(args);
	args = argv;
	if (JS_IsException(r))
		goto exc;
	if (nrets == 1) {
		int e = qw_to_wasm(ctx, s, t->types[0], r, &sp[0]);
		JS_FreeValue(ctx, r);
		if (e)
			goto exc;
	} else if (nrets > 1) {
		/* several results: an iterable of them (taken as an array) */
		JSValue g = JS_GetGlobalObject(ctx);
		JSValue ac = JS_GetPropertyStr(ctx, g, "Array");
		JSValue from = JS_GetPropertyStr(ctx, ac, "from");
		JSValue a = JS_Call(ctx, from, ac, 1, (JSValueConst *) &r);
		int64_t n = -1;

		JS_FreeValue(ctx, from);
		JS_FreeValue(ctx, ac);
		JS_FreeValue(ctx, g);
		JS_FreeValue(ctx, r);
		if (JS_IsException(a))
			goto exc;
		{
			JSValue l = JS_GetPropertyStr(ctx, a, "length");
			JS_ToInt64(ctx, &n, l);
			JS_FreeValue(ctx, l);
		}
		if (n != nrets) {
			JS_FreeValue(ctx, a);
			JS_ThrowTypeError(ctx, "WebAssembly: an import returned %d values, %u expected",
					(int) n, nrets);
			goto exc;
		}
		for (i = 0; i < nrets; i++) {
			JSValue v = JS_GetPropertyUint32(ctx, a, i);
			int e = qw_to_wasm(ctx, s, t->types[i], v, &sp[i]);
			JS_FreeValue(ctx, v);
			if (e) {
				JS_FreeValue(ctx, a);
				goto exc;
			}
		}
		JS_FreeValue(ctx, a);
	} else {
		JS_FreeValue(ctx, r);
	}
	return m3Err_none;
exc:
	if (args != argv)
		free(args);
	if (s->has_exc)
		JS_FreeValue(ctx, s->exc);
	s->exc = JS_GetException(ctx);
	s->has_exc = true;
	return qw_js_exception;
}

/** a Wasm function called from JavaScript: its arguments converted, its results back */
static JSValue qw_call(JSContext *ctx, struct qw_store *s, IM3Function f, int argc,
		JSValueConst *argv)
{
	IM3FuncType t = f->funcType;
	unsigned i, nargs = t->numArgs, nrets = t->numRets;
	uint64_t slots[16], rslots[16], *a = slots, *rs = rslots;
	const void *ptrs[16], **pa = ptrs, *rptrs[16], **pr = rptrs;
	JSContext *outer;
	M3Result r;
	JSValue ret = JS_UNDEFINED;

	if (nargs > 16 || nrets > 16) {
		a = malloc((nargs + nrets + 1) * sizeof *a);
		rs = a + nargs;
		pa = malloc((nargs + nrets + 1) * sizeof *pa);
		pr = pa + nargs;
		if (a == NULL || pa == NULL) {
			free(a);
			free(pa);
			return JS_ThrowOutOfMemory(ctx);
		}
	}
	for (i = 0; i < nargs; i++) {
		if (qw_to_wasm(ctx, s, t->types[nrets + i], i < (unsigned) argc ? argv[i] :
				JS_UNDEFINED, &a[i])) {
			ret = JS_EXCEPTION;
			goto done;
		}
		pa[i] = &a[i];
	}
	for (i = 0; i < nrets; i++) {
		if (t->types[i] == c_m3Type_v128 || t->types[i] == c_m3Type_exnref) {
			ret = JS_ThrowTypeError(ctx, "WebAssembly: a %s result cannot cross to "
					"JavaScript", qw_type_name(t->types[i]));
			goto done;
		}
		pr[i] = &rs[i];
	}
	if (!f->compiled) {
		r = CompileFunction(f);
		if (r) {
			ret = qw_throw_result(ctx, s, r, QW_COMPILE);
			goto done;
		}
	}
	outer = qw_running;
	qw_running = ctx;
	r = m3_Call(f, nargs, pa);
	if (!r && nrets > 0)
		r = m3_GetResults(f, nrets, pr);
	qw_running = outer;
	qw_refresh(ctx, s);
	if (r) {
		ret = qw_throw_result(ctx, s, r, QW_RUNTIME);
		goto done;
	}
	if (nrets == 1) {
		ret = qw_to_js(ctx, s, t->types[0], &rs[0]);
	} else if (nrets > 1) {
		ret = JS_NewArray(ctx);
		for (i = 0; i < nrets && !JS_IsException(ret); i++) {
			JSValue v = qw_to_js(ctx, s, t->types[i], &rs[i]);
			if (JS_IsException(v)) {
				JS_FreeValue(ctx, ret);
				ret = JS_EXCEPTION;
				break;
			}
			JS_SetPropertyUint32(ctx, ret, i, v);
		}
	}
done:
	if (a != slots) {
		free(a);
		free(pa);
	}
	return ret;
}


/* ---- exported functions ------------------------------------------------------------------ */

static void qw_func_finalizer(JSRuntime *rt, JSValue val)
{
	struct qw_func *fn = JS_GetOpaque(val, qw_func_class);

	if (fn == NULL)
		return;
	qw_wref_remove(fn->s, &fn->w);
	JS_FreeValueRT(rt, fn->store);
	qw_store_unref(fn->s);
	free(fn);
}

static void qw_func_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark)
{
	struct qw_func *fn = JS_GetOpaque(val, qw_func_class);

	if (fn != NULL)
		JS_MarkValue(rt, fn->store, mark);
}

static JSValue qw_func_callfn(JSContext *ctx, JSValueConst obj, JSValueConst this_val,
		int argc, JSValueConst *argv, int flags)
{
	struct qw_func *fn = JS_GetOpaque(obj, qw_func_class);

	(void) this_val;
	if (fn == NULL || (flags & JS_CALL_FLAG_CONSTRUCTOR))
		return JS_ThrowTypeError(ctx, "WebAssembly: not a constructor");
	return qw_call(ctx, fn->s, Function_Implementation(fn->f), argc, argv);
}

/** the function object of a Wasm function (the same one each time it is asked) */
static JSValue qw_func_wrap(JSContext *ctx, struct qw_store *s, JSValueConst sobj, IM3Function f)
{
	JSValue obj, proto, g, fc;
	struct qw_func *fn;
	char name[16];

	f = Function_Implementation(f);
	obj = qw_wref_get(s, f);
	if (!JS_IsUndefined(obj))
		return obj;
	g = JS_GetGlobalObject(ctx);
	fc = JS_GetPropertyStr(ctx, g, "Function");
	proto = JS_GetPropertyStr(ctx, fc, "prototype");
	JS_FreeValue(ctx, fc);
	JS_FreeValue(ctx, g);
	obj = JS_NewObjectProtoClass(ctx, proto, qw_func_class);
	JS_FreeValue(ctx, proto);
	if (JS_IsException(obj))
		return obj;
	fn = calloc(1, sizeof *fn);
	if (fn == NULL) {
		JS_FreeValue(ctx, obj);
		return JS_ThrowOutOfMemory(ctx);
	}
	fn->s = s;
	s->refs++;
	fn->store = JS_DupValue(ctx, sobj);
	fn->f = f;
	JS_SetOpaque(obj, fn);
	qw_wref_add(s, &fn->w, f, obj);
	/* its name is its index in its module, its length the number of its parameters */
	snprintf(name, sizeof name, "%u", (unsigned) (f - f->module->functions));
	JS_DefinePropertyValueStr(ctx, obj, "length", JS_NewInt32(ctx, f->funcType->numArgs),
			JS_PROP_CONFIGURABLE);
	JS_DefinePropertyValueStr(ctx, obj, "name", JS_NewString(ctx, name), JS_PROP_CONFIGURABLE);
	return obj;
}


/* ---- bytes ------------------------------------------------------------------------------- */

/** a BufferSource's bytes copied into a blob (NULL: an exception is thrown) */
static struct qw_blob *qw_bytes(JSContext *ctx, JSValueConst v)
{
	size_t len = (size_t) -1, off = 0, blen = 0, bpe;
	uint8_t *p;
	struct qw_blob *b;
	JSValue ab;

	if (JS_IsArrayBuffer(v)) {
		ab = JS_DupValue(ctx, v);
	} else if (JS_GetTypedArrayType(v) >= 0) {
		ab = JS_GetTypedArrayBuffer(ctx, v, &off, &len, &bpe);
		if (JS_IsException(ab))
			return NULL;
	} else {	/* a DataView */
		ab = JS_IsObject(v) ? JS_GetPropertyStr(ctx, v, "buffer") : JS_UNDEFINED;
		if (JS_IsException(ab))
			return NULL;
		if (!JS_IsArrayBuffer(ab)) {
			JS_FreeValue(ctx, ab);
			JS_ThrowTypeError(ctx, "WebAssembly: the argument must be a BufferSource");
			return NULL;
		}
		{
			int64_t o = 0, l = 0;
			JSValue y = JS_GetPropertyStr(ctx, v, "byteOffset");
			JS_ToInt64(ctx, &o, y);
			JS_FreeValue(ctx, y);
			y = JS_GetPropertyStr(ctx, v, "byteLength");
			JS_ToInt64(ctx, &l, y);
			JS_FreeValue(ctx, y);
			off = o > 0 ? (size_t) o : 0;
			len = l > 0 ? (size_t) l : 0;
		}
	}
	/* (the bytes stay valid: v holds its buffer) */
	p = JS_GetArrayBuffer(ctx, &blen, ab);
	JS_FreeValue(ctx, ab);
	if (p == NULL && JS_HasException(ctx))
		return NULL;
	if (len == (size_t) -1)
		len = blen;
	if (off > blen || len > blen - off) {
		JS_ThrowTypeError(ctx, "WebAssembly: the BufferSource is out of its buffer");
		return NULL;
	}
	if (len > 0x7fffffff) {
		JS_ThrowRangeError(ctx, "WebAssembly: the module is too large");
		return NULL;
	}
	b = malloc(sizeof *b + len + 1);
	if (b == NULL) {
		JS_ThrowOutOfMemory(ctx);
		return NULL;
	}
	b->refs = 1;
	b->len = (uint32_t) len;
	if (len > 0)
		memcpy(b->bytes, p + off, len);
	return b;
}

/** a module parsed from a blob, its function bodies validated (NULL: thrown) */
static IM3Module qw_parse(JSContext *ctx, struct qw_ctx *c, struct qw_blob *b, bool validate)
{
	IM3Module m = NULL;
	M3Result r;
	uint32_t i;

	r = m3_ParseModule(c->env, &m, b->bytes, b->len);
	if (r) {
		qw_throw(ctx, QW_COMPILE, "%s", r);
		return NULL;
	}
	if (!validate)
		return m;
	if (c->scratch == NULL)
		c->scratch = m3_NewRuntime(c->env, 4096, NULL);
	if (c->scratch == NULL) {
		m3_FreeModule(m);
		JS_ThrowOutOfMemory(ctx);
		return NULL;
	}
	/* (the validator is the runtime's; the module is not loaded into it) */
	m->runtime = c->scratch;
	for (i = m->numFuncImports; i < m->numFunctions && !r; i++)
		r = ValidateFunction(&m->functions[i]);
	m->runtime = NULL;
	if (r) {
		m3_FreeModule(m);
		qw_throw(ctx, QW_COMPILE, "%s", r);
		return NULL;
	}
	return m;
}


/* ---- WebAssembly.Module ------------------------------------------------------------------ */

static void qw_module_finalizer(JSRuntime *rt, JSValue val)
{
	struct qw_module *mo = JS_GetOpaque(val, qw_module_class);

	(void) rt;
	if (mo == NULL)
		return;
	m3_FreeModule(mo->m);
	qw_blob_unref(mo->blob);
	qw_ctx_unref(mo->c);
	free(mo);
}

static JSValue qw_new_of(JSContext *ctx, JSValueConst new_target, JSClassID cls)
{
	JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype"), obj;

	if (JS_IsException(proto))
		return proto;
	if (!JS_IsObject(proto)) {
		JS_FreeValue(ctx, proto);
		proto = JS_GetClassProto(ctx, cls);
	}
	obj = JS_NewObjectProtoClass(ctx, proto, cls);
	JS_FreeValue(ctx, proto);
	return obj;
}

static JSValue qw_module_ctor(JSContext *ctx, JSValueConst new_target, int argc,
		JSValueConst *argv)
{
	struct qw_ctx *c = qw_ctx_of(ctx);
	struct qw_module *mo;
	struct qw_blob *b;
	IM3Module m;
	JSValue obj;

	if (JS_IsUndefined(new_target))
		return qw_nonew(ctx);
	if (c == NULL)
		return JS_ThrowTypeError(ctx, "WebAssembly: no longer available here");
	b = qw_bytes(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
	if (b == NULL)
		return JS_EXCEPTION;
	m = qw_parse(ctx, c, b, true);
	if (m == NULL) {
		qw_blob_unref(b);
		return JS_EXCEPTION;
	}
	mo = calloc(1, sizeof *mo);
	obj = mo != NULL ? qw_new_of(ctx, new_target, qw_module_class) : JS_ThrowOutOfMemory(ctx);
	if (JS_IsException(obj)) {
		free(mo);
		m3_FreeModule(m);
		qw_blob_unref(b);
		return obj;
	}
	mo->c = c;
	c->refs++;
	mo->blob = b;
	mo->m = m;
	JS_SetOpaque(obj, mo);
	return obj;
}

static const char *qw_kind_name(int kind)
{
	static const char *const k[] = { "function", "table", "memory", "global", "tag" };

	return kind >= 0 && kind <= 4 ? k[kind] : "?";
}

static JSValue qw_desc(JSContext *ctx, const char *module, const char *name, int kind)
{
	JSValue o = JS_NewObject(ctx);

	if (module != NULL)
		JS_SetPropertyStr(ctx, o, "module", JS_NewString(ctx, module));
	JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, name != NULL ? name : ""));
	JS_SetPropertyStr(ctx, o, "kind", JS_NewString(ctx, qw_kind_name(kind)));
	return o;
}

static JSValue qw_module_exports(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	struct qw_module *mo = JS_GetOpaque2(ctx, argc > 0 ? argv[0] : JS_UNDEFINED,
			qw_module_class);
	JSValue a;
	uint32_t i;

	(void) this_val;
	if (mo == NULL)
		return JS_EXCEPTION;
	a = JS_NewArray(ctx);
	for (i = 0; i < mo->m->numExports; i++)
		JS_SetPropertyUint32(ctx, a, i, qw_desc(ctx, NULL, mo->m->exports[i].name,
				mo->m->exports[i].kind));
	return a;
}

static JSValue qw_module_imports(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	struct qw_module *mo = JS_GetOpaque2(ctx, argc > 0 ? argv[0] : JS_UNDEFINED,
			qw_module_class);
	IM3Module m;
	JSValue a;
	uint32_t i, n = 0;

	(void) this_val;
	if (mo == NULL)
		return JS_EXCEPTION;
	m = mo->m;
	a = JS_NewArray(ctx);
	for (i = 0; i < m->numFuncImports; i++)
		JS_SetPropertyUint32(ctx, a, n++, qw_desc(ctx, m->functions[i].import.moduleUtf8,
				m->functions[i].import.fieldUtf8, d_externalKind_function));
	for (i = 0; i < m->numTables; i++)
		if (m->tables[i]->imported)
			JS_SetPropertyUint32(ctx, a, n++, qw_desc(ctx, m->tables[i]->import.moduleUtf8,
					m->tables[i]->import.fieldUtf8, d_externalKind_table));
	for (i = 0; i < m->numMemories; i++)
		if (m->memories[i]->imported)
			JS_SetPropertyUint32(ctx, a, n++, qw_desc(ctx, m->memories[i]->import.moduleUtf8,
					m->memories[i]->import.fieldUtf8, d_externalKind_memory));
	for (i = 0; i < m->numGlobals; i++)
		if (m->globals[i].imported)
			JS_SetPropertyUint32(ctx, a, n++, qw_desc(ctx, m->globals[i].import.moduleUtf8,
					m->globals[i].import.fieldUtf8, d_externalKind_global));
#if d_m3HasExceptionHandling
	for (i = 0; i < m->numTags; i++)
		if (m->tags[i].imported)
			JS_SetPropertyUint32(ctx, a, n++, qw_desc(ctx, m->tags[i].import.moduleUtf8,
					m->tags[i].import.fieldUtf8, d_externalKind_tag));
#endif
	return a;
}

/* the custom sections named so: walked in the module's bytes */
static int qw_leb(const uint8_t **p, const uint8_t *end, uint32_t *v)
{
	uint32_t r = 0;
	int shift = 0;

	while (*p < end && shift < 35) {
		uint8_t b = *(*p)++;
		r |= (uint32_t) (b & 0x7f) << shift;
		if (!(b & 0x80)) {
			*v = r;
			return 0;
		}
		shift += 7;
	}
	return -1;
}

static JSValue qw_module_sections(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	struct qw_module *mo = JS_GetOpaque2(ctx, argc > 0 ? argv[0] : JS_UNDEFINED,
			qw_module_class);
	const char *want;
	size_t wlen;
	const uint8_t *p, *end;
	JSValue a;
	uint32_t n = 0;

	(void) this_val;
	if (mo == NULL)
		return JS_EXCEPTION;
	if (argc < 2)
		return JS_ThrowTypeError(ctx, "WebAssembly.Module.customSections: a name is needed");
	want = JS_ToCStringLen(ctx, &wlen, argv[1]);
	if (want == NULL)
		return JS_EXCEPTION;
	a = JS_NewArray(ctx);
	p = mo->blob->bytes + 8;
	end = mo->blob->bytes + mo->blob->len;
	while (p < end) {
		uint8_t id = *p++;
		uint32_t size, nlen;
		const uint8_t *sec;

		if (qw_leb(&p, end, &size) || size > (size_t) (end - p))
			break;
		sec = p;
		p += size;
		if (id != 0)
			continue;
		if (qw_leb(&sec, p, &nlen) || nlen > (size_t) (p - sec))
			continue;
		if (nlen == wlen && memcmp(sec, want, nlen) == 0)
			JS_SetPropertyUint32(ctx, a, n++, JS_NewArrayBufferCopy(ctx, sec + nlen,
					(size_t) (p - sec - nlen)));
	}
	JS_FreeCString(ctx, want);
	return a;
}


/* ---- WebAssembly.Memory / Table / Global -------------------------------------------------- */

static void qw_memory_finalizer(JSRuntime *rt, JSValue val)
{
	struct qw_memory *m = JS_GetOpaque(val, qw_memory_class);

	if (m == NULL)
		return;
	qw_wref_remove(m->s, &m->w);
	qw_bufs_remove(m);
	JS_FreeValueRT(rt, m->buffer);	/* (the ArrayBuffer holds the store on its own) */
	JS_FreeValueRT(rt, m->store);
	qw_store_unref(m->s);
	free(m);
}

static void qw_memory_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark)
{
	struct qw_memory *m = JS_GetOpaque(val, qw_memory_class);

	if (m != NULL) {
		JS_MarkValue(rt, m->store, mark);
		JS_MarkValue(rt, m->buffer, mark);
	}
}

static void qw_table_finalizer(JSRuntime *rt, JSValue val)
{
	struct qw_table *t = JS_GetOpaque(val, qw_table_class);

	if (t == NULL)
		return;
	qw_wref_remove(t->s, &t->w);
	JS_FreeValueRT(rt, t->store);
	qw_store_unref(t->s);
	free(t);
}

static void qw_global_finalizer(JSRuntime *rt, JSValue val)
{
	struct qw_global *g = JS_GetOpaque(val, qw_global_class);

	if (g == NULL)
		return;
	qw_wref_remove(g->s, &g->w);
	JS_FreeValueRT(rt, g->store);
	qw_store_unref(g->s);
	free(g);
}

static void qw_table_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark)
{
	struct qw_table *t = JS_GetOpaque(val, qw_table_class);

	if (t != NULL)
		JS_MarkValue(rt, t->store, mark);
}

static void qw_global_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark)
{
	struct qw_global *g = JS_GetOpaque(val, qw_global_class);

	if (g != NULL)
		JS_MarkValue(rt, g->store, mark);
}

/* the object of a memory, table or global (the same each time); size: its struct's */
static JSValue qw_entity_wrap(JSContext *ctx, struct qw_store *s, JSValueConst sobj,
		JSClassID cls, void *key, JSValueConst proto, size_t size, void **out)
{
	JSValue obj = qw_wref_get(s, key);
	struct qw_wref *w;

	*out = NULL;
	if (!JS_IsUndefined(obj))
		return obj;
	obj = JS_IsUndefined(proto) ? JS_NewObjectClass(ctx, cls) :
		JS_NewObjectProtoClass(ctx, proto, cls);
	if (JS_IsException(obj))
		return obj;
	w = calloc(1, size);
	if (w == NULL) {
		JS_FreeValue(ctx, obj);
		return JS_ThrowOutOfMemory(ctx);
	}
	JS_SetOpaque(obj, w);
	qw_wref_add(s, w, key, obj);
	s->refs++;
	/* (every such struct starts with its wref, then its store and store object) */
	if (cls == qw_memory_class) {
		struct qw_memory *m = (struct qw_memory *) w;
		m->s = s;
		m->store = JS_DupValue(ctx, sobj);
		m->mem = key;
		m->buffer = JS_UNDEFINED;
	} else if (cls == qw_table_class) {
		struct qw_table *t = (struct qw_table *) w;
		t->s = s;
		t->store = JS_DupValue(ctx, sobj);
		t->t = key;
	} else {
		struct qw_global *g = (struct qw_global *) w;
		g->s = s;
		g->store = JS_DupValue(ctx, sobj);
		g->g = key;
	}
	*out = w;
	return obj;
}

static void qw_put_leb(uint8_t **p, uint64_t v)
{
	do {
		uint8_t b = v & 0x7f;
		v >>= 7;
		*(*p)++ = b | (v ? 0x80 : 0);
	} while (v);
}

/** a module made here (b: its bytes, the reference taken) loaded into the store */
static IM3Module qw_load_made(JSContext *ctx, struct qw_store *s, struct qw_blob *b)
{
	IM3Module m = NULL;
	M3Result r;
	char name[32];
	const char *kept;

	r = m3_ParseModule(s->c->env, &m, b->bytes, b->len);
	if (r) {
		qw_blob_unref(b);
		JS_ThrowTypeError(ctx, "WebAssembly: %s", r);
		return NULL;
	}
	snprintf(name, sizeof name, "\x01wasm#%u", ++s->seq);
	kept = qw_keep_name(s, name, b);
	qw_blob_unref(b);
	if (kept == NULL) {
		m3_FreeModule(m);
		JS_ThrowOutOfMemory(ctx);
		return NULL;
	}
	m3_SetModuleName(m, kept);
	r = m3_LoadModule(s->rt, m);
	if (r) {
		if (r == m3Err_mallocFailed || r == m3Err_memoryLimitExceeded ||
				r == m3Err_tableLimitExceeded)
			JS_ThrowRangeError(ctx, "WebAssembly: %s", r);
		else
			JS_ThrowTypeError(ctx, "WebAssembly: %s", r);
		return NULL;
	}
	return m;
}

/**
 * A module exporting one memory (kind 2), table (1) or global (3), loaded into the store:
 * what `new WebAssembly.Memory / Table / Global` make. def: the section's entry (after
 * its count); returns the module (NULL: thrown).
 */
static IM3Module qw_make_holder(JSContext *ctx, struct qw_store *s, int kind,
		const uint8_t *def, size_t deflen)
{
	static const uint8_t head[8] = { 0, 'a', 's', 'm', 1, 0, 0, 0 };
	static const uint8_t secid[4] = { 0, 4, 5, 6 };	/* table, memory, global sections */
	static const char exname[4] = { 0, 't', 'm', 'g' };
	struct qw_blob *b = malloc(sizeof *b + 8 + deflen + 32);
	uint8_t *p;

	if (b == NULL) {
		JS_ThrowOutOfMemory(ctx);
		return NULL;
	}
	p = b->bytes;
	memcpy(p, head, 8);
	p += 8;
	*p++ = secid[kind];
	qw_put_leb(&p, deflen + 1);
	*p++ = 1;
	memcpy(p, def, deflen);
	p += deflen;
	*p++ = 7;	/* the export section */
	*p++ = 5;
	*p++ = 1;
	*p++ = 1;
	*p++ = (uint8_t) exname[kind];
	*p++ = (uint8_t) kind;
	*p++ = 0;
	b->refs = 1;
	b->len = (uint32_t) (p - b->bytes);
	return qw_load_made(ctx, s, b);
}

static uint8_t qw_valtype_code(uint8_t t)
{
	switch (t) {
	case c_m3Type_i32: return 0x7f;
	case c_m3Type_i64: return 0x7e;
	case c_m3Type_f32: return 0x7d;
	case c_m3Type_f64: return 0x7c;
	case c_m3Type_funcref: return 0x70;
	case c_m3Type_externref: return 0x6f;
	default: return 0;
	}
}

/**
 * A function of another store as a funcref of this one (put in a table, given as an
 * argument): a trampoline -- a module of this store importing the function object and
 * exporting it -- made once per function. Its calls go through JavaScript to the other
 * store (whose runtime runs its code on its own stack and memory). Emscripten's
 * addFunction puts a small instance's export in the main instance's table so.
 */
static IM3Function qw_trampoline(JSContext *ctx, struct qw_store *s, JSValueConst v,
		struct qw_func *fn)
{
	static const uint8_t head[8] = { 0, 'a', 's', 'm', 1, 0, 0, 0 };
	/* the import section: "t" "f", a function of type 0; the export section: "f" */
	static const uint8_t rest[] = { 2, 7, 1, 1, 't', 1, 'f', 0, 0, 7, 5, 1, 1, 'f', 0, 0 };
	IM3Function target = Function_Implementation(fn->f), local;
	IM3FuncType t = target->funcType;
	struct qw_import *im;
	struct qw_blob *b;
	IM3Module m;
	uint8_t *p, *ty, *q;
	unsigned i, n = t->numArgs + t->numRets;
	M3Result r;

	for (im = s->imports; im != NULL; im = im->next)
		if (im->local != NULL && im->foreign == target)
			return im->local;
	b = malloc(sizeof *b + 64 + 2 * n);
	ty = malloc(16 + n);
	if (b == NULL || ty == NULL) {
		free(b);
		free(ty);
		JS_ThrowOutOfMemory(ctx);
		return NULL;
	}
	/* the type section: one function type, its parameters then its results */
	q = ty;
	*q++ = 1;
	*q++ = 0x60;
	qw_put_leb(&q, t->numArgs);
	for (i = 0; i < t->numArgs; i++)
		if ((*q++ = qw_valtype_code(t->types[t->numRets + i])) == 0)
			break;
	qw_put_leb(&q, t->numRets);
	for (i = 0; i < t->numRets; i++)
		if ((*q++ = qw_valtype_code(t->types[i])) == 0)
			break;
	for (i = 0; i < t->numArgs; i++)
		if (qw_valtype_code(t->types[t->numRets + i]) == 0)
			n = 0;
	for (i = 0; i < t->numRets; i++)
		if (qw_valtype_code(t->types[i]) == 0)
			n = 0;
	if (n == 0 && t->numArgs + t->numRets > 0) {
		free(ty);
		free(b);
		JS_ThrowTypeError(ctx, "WebAssembly: this function cannot cross to another "
				"instance's store");
		return NULL;
	}
	p = b->bytes;
	memcpy(p, head, 8);
	p += 8;
	*p++ = 1;
	qw_put_leb(&p, (uint64_t) (q - ty));
	memcpy(p, ty, (size_t) (q - ty));
	p += q - ty;
	free(ty);
	memcpy(p, rest, sizeof rest);
	p += sizeof rest;
	b->refs = 1;
	b->len = (uint32_t) (p - b->bytes);
	m = qw_load_made(ctx, s, b);
	if (m == NULL)
		return NULL;
	local = &m->functions[0];
	im = calloc(1, sizeof *im);
	if (im == NULL) {
		JS_ThrowOutOfMemory(ctx);
		return NULL;
	}
	im->s = s;
	im->fn = JS_DupValue(ctx, v);
	im->foreign = target;
	im->local = local;
	im->next = s->imports;
	s->imports = im;
	r = CompileRawFunction(m, local, (const void *) qw_host_call, im);
	if (r) {
		JS_ThrowTypeError(ctx, "WebAssembly: %s", r);
		return NULL;
	}
	return local;
}

/* a descriptor's integer member (initial / minimum / maximum); -1: absent, -2: thrown */
static int64_t qw_desc_int(JSContext *ctx, JSValueConst d, const char *name, uint64_t max)
{
	JSValue v = JS_GetPropertyStr(ctx, d, name);
	double x;

	if (JS_IsException(v))
		return -2;
	if (JS_IsUndefined(v))
		return -1;
	if (JS_ToFloat64(ctx, &x, v)) {
		JS_FreeValue(ctx, v);
		return -2;
	}
	JS_FreeValue(ctx, v);
	if (!(x >= 0) || x > (double) max) {
		JS_ThrowTypeError(ctx, "WebAssembly: the descriptor's %s is out of range", name);
		return -2;
	}
	return (int64_t) x;
}

/* the limits of a memory or table descriptor, written as Wasm's */
static int qw_limits(JSContext *ctx, JSValueConst d, uint64_t max, uint8_t **p,
		uint64_t *initial, int64_t *maximum)
{
	int64_t ini = qw_desc_int(ctx, d, "initial", max), mn, mx;

	if (ini == -2)
		return -1;
	mn = qw_desc_int(ctx, d, "minimum", max);
	if (mn == -2)
		return -1;
	if (ini >= 0 && mn >= 0) {
		JS_ThrowTypeError(ctx, "WebAssembly: 'initial' and 'minimum' are both given");
		return -1;
	}
	if (ini < 0)
		ini = mn;
	if (ini < 0) {
		JS_ThrowTypeError(ctx, "WebAssembly: the descriptor needs 'initial'");
		return -1;
	}
	mx = qw_desc_int(ctx, d, "maximum", max);
	if (mx == -2)
		return -1;
	if (mx >= 0 && mx < ini) {
		JS_ThrowRangeError(ctx, "WebAssembly: 'maximum' is below 'initial'");
		return -1;
	}
	*(*p)++ = mx >= 0 ? 1 : 0;
	qw_put_leb(p, (uint64_t) ini);
	if (mx >= 0)
		qw_put_leb(p, (uint64_t) mx);
	*initial = (uint64_t) ini;
	*maximum = mx;
	return 0;
}

static JSValue qw_memory_ctor(JSContext *ctx, JSValueConst new_target, int argc,
		JSValueConst *argv)
{
	struct qw_ctx *c = qw_ctx_of(ctx);
	JSValueConst d = argc > 0 ? argv[0] : JS_UNDEFINED;
	uint8_t def[32], *p = def;
	uint64_t ini;
	int64_t mx;
	struct qw_store *s;
	struct qw_memory *m;
	JSValue sobj, proto, obj;
	IM3Module hm;

	if (JS_IsUndefined(new_target))
		return qw_nonew(ctx);
	if (c == NULL)
		return JS_ThrowTypeError(ctx, "WebAssembly: no longer available here");
	if (!JS_IsObject(d))
		return JS_ThrowTypeError(ctx, "WebAssembly.Memory: a descriptor is needed");
	{
		JSValue sh = JS_GetPropertyStr(ctx, d, "shared");
		int shared = JS_ToBool(ctx, sh);
		JS_FreeValue(ctx, sh);
		if (shared)
			return JS_ThrowTypeError(ctx, "WebAssembly.Memory: shared memories are "
					"not supported (no threads)");
	}
	if (qw_limits(ctx, d, 65536, &p, &ini, &mx))
		return JS_EXCEPTION;
	s = qw_store_get(ctx, c, &sobj);
	if (s == NULL)
		return JS_EXCEPTION;
	hm = qw_make_holder(ctx, s, d_externalKind_memory, def, (size_t) (p - def));
	if (hm == NULL) {
		JS_FreeValue(ctx, sobj);
		return JS_EXCEPTION;
	}
	proto = JS_GetPropertyStr(ctx, new_target, "prototype");
	obj = qw_entity_wrap(ctx, s, sobj, qw_memory_class, hm->memories[0],
			JS_IsObject(proto) ? proto : JS_UNDEFINED, sizeof *m, (void **) &m);
	JS_FreeValue(ctx, proto);
	JS_FreeValue(ctx, sobj);
	if (m != NULL) {
		m->modname = hm->name;
		m->expname = "m";
	}
	return obj;
}

static JSValue qw_memory_buffer(JSContext *ctx, JSValueConst this_val)
{
	struct qw_memory *m = JS_GetOpaque2(ctx, this_val, qw_memory_class);
	size_t len, have;
	uint8_t *d;

	if (m == NULL)
		return JS_EXCEPTION;
	qw_refresh(ctx, m->s);
	if (!JS_IsUndefined(m->buffer) && JS_GetArrayBuffer(ctx, &have, m->buffer) != NULL)
		return JS_DupValue(ctx, m->buffer);
	JS_FreeValue(ctx, JS_GetException(ctx));	/* (JS_GetArrayBuffer: a detached one) */
	qw_buffer_drop(ctx, m);
	d = qw_mem_data(m->mem, &len);
	m->buffer = JS_NewArrayBuffer(ctx, d, len, 0, qw_buffer_release, m->s, false);
	if (JS_IsException(m->buffer)) {
		m->buffer = JS_UNDEFINED;
		return JS_EXCEPTION;
	}
	m->s->refs++;	/* (the ArrayBuffer's: qw_buffer_release) */
	m->bdata = d;
	m->blen = len;
	if (!m->inbufs) {
		m->bnext = m->s->bufs;
		m->s->bufs = m;
		m->inbufs = true;
	}
	return JS_DupValue(ctx, m->buffer);
}

static JSValue qw_memory_grow(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	struct qw_memory *m = JS_GetOpaque2(ctx, this_val, qw_memory_class);
	double delta;
	u64 old;
	M3Result r;

	if (m == NULL)
		return JS_EXCEPTION;
	if (JS_ToFloat64(ctx, &delta, argc > 0 ? argv[0] : JS_UNDEFINED))
		return JS_EXCEPTION;
	if (!(delta >= 0) || delta > 4294967295.0 || delta != (double) (uint32_t) delta)
		return JS_ThrowTypeError(ctx, "WebAssembly.Memory.grow: an unsigned 32-bit "
				"number of pages is expected");
	old = m->mem->numPages;
	if ((uint64_t) delta > m->mem->maxPages - old)
		return JS_ThrowRangeError(ctx, "WebAssembly.Memory.grow: past the maximum");
	r = ResizeMemory(m->s->rt, m->mem, old + (u64) delta);
	if (r)
		return JS_ThrowRangeError(ctx, "WebAssembly.Memory.grow: %s", r);
	/* (the buffer is detached even when nothing moved: the standard's) */
	qw_bufs_remove(m);
	qw_buffer_drop(ctx, m);
	qw_refresh(ctx, m->s);
	return JS_NewFloat64(ctx, (double) old);
}

static uint8_t qw_parse_reftype(JSContext *ctx, JSValueConst v)
{
	const char *s = JS_ToCString(ctx, v);
	uint8_t t = 0;

	if (s == NULL)
		return 0;
	if (strcmp(s, "anyfunc") == 0 || strcmp(s, "funcref") == 0)
		t = c_m3Type_funcref;
	else if (strcmp(s, "externref") == 0)
		t = c_m3Type_externref;
	JS_FreeCString(ctx, s);
	if (t == 0)
		JS_ThrowTypeError(ctx, "WebAssembly: an element type 'anyfunc' or 'externref' "
				"is expected");
	return t;
}

static JSValue qw_table_ctor(JSContext *ctx, JSValueConst new_target, int argc,
		JSValueConst *argv)
{
	struct qw_ctx *c = qw_ctx_of(ctx);
	JSValueConst d = argc > 0 ? argv[0] : JS_UNDEFINED;
	uint8_t def[32], *p = def, et;
	uint64_t ini, init = 0;
	int64_t mx;
	struct qw_store *s;
	struct qw_table *t;
	JSValue sobj, proto, obj, ev;
	IM3Module hm;
	uint32_t i;

	if (JS_IsUndefined(new_target))
		return qw_nonew(ctx);
	if (c == NULL)
		return JS_ThrowTypeError(ctx, "WebAssembly: no longer available here");
	if (!JS_IsObject(d))
		return JS_ThrowTypeError(ctx, "WebAssembly.Table: a descriptor is needed");
	ev = JS_GetPropertyStr(ctx, d, "element");
	if (JS_IsException(ev))
		return ev;
	et = qw_parse_reftype(ctx, ev);
	JS_FreeValue(ctx, ev);
	if (et == 0)
		return JS_EXCEPTION;
	*p++ = et == c_m3Type_funcref ? 0x70 : 0x6f;
	if (qw_limits(ctx, d, 10000000, &p, &ini, &mx))
		return JS_EXCEPTION;
	s = qw_store_get(ctx, c, &sobj);
	if (s == NULL)
		return JS_EXCEPTION;
	if (argc > 1 && !JS_IsUndefined(argv[1]) &&
			qw_to_wasm(ctx, s, et, argv[1], &init)) {
		JS_FreeValue(ctx, sobj);
		return JS_EXCEPTION;
	}
	hm = qw_make_holder(ctx, s, d_externalKind_table, def, (size_t) (p - def));
	if (hm == NULL) {
		JS_FreeValue(ctx, sobj);
		return JS_EXCEPTION;
	}
	for (i = 0; i < hm->tables[0]->size; i++)
		hm->tables[0]->elements[i] = (void *) (uintptr_t) init;
	proto = JS_GetPropertyStr(ctx, new_target, "prototype");
	obj = qw_entity_wrap(ctx, s, sobj, qw_table_class, hm->tables[0],
			JS_IsObject(proto) ? proto : JS_UNDEFINED, sizeof *t, (void **) &t);
	JS_FreeValue(ctx, proto);
	JS_FreeValue(ctx, sobj);
	if (t != NULL) {
		t->modname = hm->name;
		t->expname = "t";
	}
	return obj;
}

static JSValue qw_table_length(JSContext *ctx, JSValueConst this_val)
{
	struct qw_table *t = JS_GetOpaque2(ctx, this_val, qw_table_class);

	if (t == NULL)
		return JS_EXCEPTION;
	return JS_NewUint32(ctx, t->t->size);
}

static int qw_table_index(JSContext *ctx, struct qw_table *t, JSValueConst v, uint32_t *i)
{
	double x;

	if (JS_ToFloat64(ctx, &x, v))
		return -1;
	if (!(x >= 0) || x != (double) (uint32_t) x) {
		JS_ThrowTypeError(ctx, "WebAssembly.Table: an index is expected");
		return -1;
	}
	if ((uint32_t) x >= t->t->size) {
		JS_ThrowRangeError(ctx, "WebAssembly.Table: index out of bounds");
		return -1;
	}
	*i = (uint32_t) x;
	return 0;
}

static JSValue qw_table_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qw_table *t = JS_GetOpaque2(ctx, this_val, qw_table_class);
	uint32_t i;
	uint64_t slot;

	if (t == NULL || qw_table_index(ctx, t, argc > 0 ? argv[0] : JS_UNDEFINED, &i))
		return JS_EXCEPTION;
	slot = (uint64_t) (uintptr_t) t->t->elements[i];
	return qw_to_js(ctx, t->s, BaseTypeOf(t->t->type), &slot);
}

static JSValue qw_table_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qw_table *t = JS_GetOpaque2(ctx, this_val, qw_table_class);
	uint32_t i;
	uint64_t slot = 0;

	if (t == NULL || qw_table_index(ctx, t, argc > 0 ? argv[0] : JS_UNDEFINED, &i))
		return JS_EXCEPTION;
	if (argc > 1 && qw_to_wasm(ctx, t->s, BaseTypeOf(t->t->type), argv[1], &slot))
		return JS_EXCEPTION;
	t->t->elements[i] = (void *) (uintptr_t) slot;
	return JS_UNDEFINED;
}

static JSValue qw_table_grow(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qw_table *t = JS_GetOpaque2(ctx, this_val, qw_table_class);
	IM3Runtime rt;
	double delta;
	uint64_t slot = 0, max;
	uint32_t old, i;
	void **el;

	if (t == NULL)
		return JS_EXCEPTION;
	if (JS_ToFloat64(ctx, &delta, argc > 0 ? argv[0] : JS_UNDEFINED))
		return JS_EXCEPTION;
	if (!(delta >= 0) || delta != (double) (uint32_t) delta)
		return JS_ThrowTypeError(ctx, "WebAssembly.Table.grow: a number of elements is expected");
	if (argc > 1 && !JS_IsUndefined(argv[1]) &&
			qw_to_wasm(ctx, t->s, BaseTypeOf(t->t->type), argv[1], &slot))
		return JS_EXCEPTION;
	old = t->t->size;
	max = t->t->hasMax ? t->t->maxSize : d_m3MaxSaneTableSize;
	if ((uint64_t) delta > max - old)
		return JS_ThrowRangeError(ctx, "WebAssembly.Table.grow: past the maximum");
	if (delta == 0)
		return JS_NewUint32(ctx, old);
	rt = t->s->rt;
	el = realloc(t->t->elements, ((size_t) old + (size_t) delta) * sizeof *el);
	if (el == NULL)
		return JS_ThrowRangeError(ctx, "WebAssembly.Table.grow: out of memory");
	for (i = old; i < old + (uint32_t) delta; i++)
		el[i] = (void *) (uintptr_t) slot;
	t->t->elements = el;
	t->t->size = old + (uint32_t) delta;
	rt->tableElementsUsed += (uint32_t) delta;
	return JS_NewUint32(ctx, old);
}

static uint8_t qw_parse_valtype(JSContext *ctx, JSValueConst v, uint8_t *code)
{
	static const struct { const char *n; uint8_t t, code; } types[] = {
		{ "i32", c_m3Type_i32, 0x7f }, { "i64", c_m3Type_i64, 0x7e },
		{ "f32", c_m3Type_f32, 0x7d }, { "f64", c_m3Type_f64, 0x7c },
		{ "anyfunc", c_m3Type_funcref, 0x70 }, { "funcref", c_m3Type_funcref, 0x70 },
		{ "externref", c_m3Type_externref, 0x6f },
	};
	const char *s = JS_ToCString(ctx, v);
	unsigned i;

	if (s == NULL)
		return 0;
	for (i = 0; i < sizeof types / sizeof types[0]; i++)
		if (strcmp(s, types[i].n) == 0) {
			JS_FreeCString(ctx, s);
			*code = types[i].code;
			return types[i].t;
		}
	JS_FreeCString(ctx, s);
	JS_ThrowTypeError(ctx, "WebAssembly.Global: an unknown value type");
	return 0;
}

static void *qw_global_cell(IM3Global g)
{
	return g->resolved != NULL ? (void *) &g->resolved->i64Value : (void *) &g->i64Value;
}

static JSValue qw_global_ctor(JSContext *ctx, JSValueConst new_target, int argc,
		JSValueConst *argv)
{
	struct qw_ctx *c = qw_ctx_of(ctx);
	JSValueConst d = argc > 0 ? argv[0] : JS_UNDEFINED;
	uint8_t def[16], *p = def, t, code = 0;
	uint64_t v = 0;
	int mut;
	struct qw_store *s;
	struct qw_global *g;
	JSValue sobj, proto, obj, x;
	IM3Module hm;

	if (JS_IsUndefined(new_target))
		return qw_nonew(ctx);
	if (c == NULL)
		return JS_ThrowTypeError(ctx, "WebAssembly: no longer available here");
	if (!JS_IsObject(d))
		return JS_ThrowTypeError(ctx, "WebAssembly.Global: a descriptor is needed");
	x = JS_GetPropertyStr(ctx, d, "mutable");
	mut = JS_ToBool(ctx, x);
	JS_FreeValue(ctx, x);
	x = JS_GetPropertyStr(ctx, d, "value");
	if (JS_IsException(x))
		return x;
	t = qw_parse_valtype(ctx, x, &code);
	JS_FreeValue(ctx, x);
	if (t == 0)
		return JS_EXCEPTION;
	s = qw_store_get(ctx, c, &sobj);
	if (s == NULL)
		return JS_EXCEPTION;
	if (argc > 1 && !JS_IsUndefined(argv[1]) && qw_to_wasm(ctx, s, t, argv[1], &v)) {
		JS_FreeValue(ctx, sobj);
		return JS_EXCEPTION;
	}
	*p++ = code;
	*p++ = mut ? 1 : 0;
	switch (t) {	/* initialised to 0; the value is written after */
	case c_m3Type_i32: *p++ = 0x41; *p++ = 0; break;
	case c_m3Type_i64: *p++ = 0x42; *p++ = 0; break;
	case c_m3Type_f32: *p++ = 0x43; memset(p, 0, 4); p += 4; break;
	case c_m3Type_f64: *p++ = 0x44; memset(p, 0, 8); p += 8; break;
	default: *p++ = 0xd0; *p++ = code; break;
	}
	*p++ = 0x0b;
	hm = qw_make_holder(ctx, s, d_externalKind_global, def, (size_t) (p - def));
	if (hm == NULL) {
		JS_FreeValue(ctx, sobj);
		return JS_EXCEPTION;
	}
	memcpy(&hm->globals[0].i64Value, &v, t == c_m3Type_i32 || t == c_m3Type_f32 ? 4 : 8);
	proto = JS_GetPropertyStr(ctx, new_target, "prototype");
	obj = qw_entity_wrap(ctx, s, sobj, qw_global_class, &hm->globals[0],
			JS_IsObject(proto) ? proto : JS_UNDEFINED, sizeof *g, (void **) &g);
	JS_FreeValue(ctx, proto);
	JS_FreeValue(ctx, sobj);
	if (g != NULL) {
		g->modname = hm->name;
		g->expname = "g";
	}
	return obj;
}

static JSValue qw_global_value(JSContext *ctx, JSValueConst this_val)
{
	struct qw_global *g = JS_GetOpaque2(ctx, this_val, qw_global_class);
	uint64_t slot = 0;
	uint8_t t;

	if (g == NULL)
		return JS_EXCEPTION;
	t = BaseTypeOf(g->g->type);
	memcpy(&slot, qw_global_cell(g->g), t == c_m3Type_i32 || t == c_m3Type_f32 ? 4 :
			(t == c_m3Type_i64 || t == c_m3Type_f64 ? 8 : sizeof(void *)));
	return qw_to_js(ctx, g->s, t, &slot);
}

static JSValue qw_global_set_value(JSContext *ctx, JSValueConst this_val, JSValueConst v)
{
	struct qw_global *g = JS_GetOpaque2(ctx, this_val, qw_global_class);
	uint64_t slot = 0;
	uint8_t t;

	if (g == NULL)
		return JS_EXCEPTION;
	if (!g->g->isMutable)
		return JS_ThrowTypeError(ctx, "WebAssembly.Global: the global is immutable");
	t = BaseTypeOf(g->g->type);
	if (qw_to_wasm(ctx, g->s, t, v, &slot))
		return JS_EXCEPTION;
	memcpy(qw_global_cell(g->g), &slot, t == c_m3Type_i32 || t == c_m3Type_f32 ? 4 :
			(t == c_m3Type_i64 || t == c_m3Type_f64 ? 8 : sizeof(void *)));
	return JS_UNDEFINED;
}

static JSValue qw_global_valueof(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	(void) argc;
	(void) argv;
	return qw_global_value(ctx, this_val);
}


/* ---- WebAssembly.Instance ------------------------------------------------------------------ */

static void qw_instance_finalizer(JSRuntime *rt, JSValue val)
{
	struct qw_instance *in = JS_GetOpaque(val, qw_instance_class);

	if (in == NULL)
		return;
	JS_FreeValueRT(rt, in->exports);
	JS_FreeValueRT(rt, in->store);
	qw_store_unref(in->s);
	free(in);
}

static void qw_instance_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark)
{
	struct qw_instance *in = JS_GetOpaque(val, qw_instance_class);

	if (in != NULL) {
		JS_MarkValue(rt, in->exports, mark);
		JS_MarkValue(rt, in->store, mark);
	}
}

/* an import renamed to the module and export that hold what it is linked to */
static int qw_rename(JSContext *ctx, M3ImportInfo *imp, const char *mod, const char *field)
{
	size_t ml = strlen(mod) + 1, fl = strlen(field) + 1;
	char *m = m3_Malloc("import", ml), *f = m3_Malloc("import", fl);

	if (m == NULL || f == NULL) {
		m3_Free(m);
		m3_Free(f);
		JS_ThrowOutOfMemory(ctx);
		return -1;
	}
	memcpy(m, mod, ml);
	memcpy(f, field, fl);
	FreeImportInfo(imp);
	imp->moduleUtf8 = m;
	imp->fieldUtf8 = f;
	return 0;
}

/* Get(Get(importObject, module), name) */
static JSValue qw_import_value(JSContext *ctx, JSValueConst imports, const M3ImportInfo *imp)
{
	JSValue o = JS_GetPropertyStr(ctx, imports, imp->moduleUtf8 ? imp->moduleUtf8 : ""), v;

	if (JS_IsException(o))
		return o;
	if (!JS_IsObject(o)) {
		JS_FreeValue(ctx, o);
		return JS_ThrowTypeError(ctx, "WebAssembly.Instance: the import module '%s' is "
				"not an object", imp->moduleUtf8 ? imp->moduleUtf8 : "");
	}
	v = JS_GetPropertyStr(ctx, o, imp->fieldUtf8 ? imp->fieldUtf8 : "");
	JS_FreeValue(ctx, o);
	return v;
}

struct qw_imp {			/* an import and the value the import object gives */
	int kind;			/* d_externalKind_* */
	uint32_t index;			/* in the module's functions, tables, memories, globals */
	JSValue v;
};

/** the exports object of an instance (frozen, null prototype) */
static JSValue qw_exports(JSContext *ctx, struct qw_store *s, JSValueConst sobj, IM3Module m)
{
	JSValue ex = JS_NewObjectProto(ctx, JS_NULL), v;
	uint32_t i;

	for (i = 0; i < m->numExports; i++) {
		const M3Export *e = &m->exports[i];
		void *w;

		switch (e->kind) {
		case d_externalKind_function:
			v = qw_func_wrap(ctx, s, sobj, &m->functions[e->index]);
			break;
		case d_externalKind_memory:
			v = qw_entity_wrap(ctx, s, sobj, qw_memory_class, m->memories[e->index],
					JS_UNDEFINED, sizeof(struct qw_memory), &w);
			if (w != NULL) {
				((struct qw_memory *) w)->modname = m->name;
				((struct qw_memory *) w)->expname = e->name;
			}
			break;
		case d_externalKind_table:
			v = qw_entity_wrap(ctx, s, sobj, qw_table_class, m->tables[e->index],
					JS_UNDEFINED, sizeof(struct qw_table), &w);
			if (w != NULL) {
				((struct qw_table *) w)->modname = m->name;
				((struct qw_table *) w)->expname = e->name;
			}
			break;
		case d_externalKind_global: {
			IM3Global g = &m->globals[e->index];
			v = qw_entity_wrap(ctx, s, sobj, qw_global_class,
					g->resolved != NULL ? g->resolved : g,
					JS_UNDEFINED, sizeof(struct qw_global), &w);
			if (w != NULL) {
				((struct qw_global *) w)->modname = m->name;
				((struct qw_global *) w)->expname = e->name;
			}
			break;
		}
		default:	/* (a tag: WebAssembly.Tag is not there yet) */
			continue;
		}
		if (JS_IsException(v)) {
			JS_FreeValue(ctx, ex);
			return v;
		}
		JS_DefinePropertyValueStr(ctx, ex, e->name, v, JS_PROP_ENUMERABLE);
	}
	JS_FreezeObject(ctx, ex);
	return ex;
}

/** an instance of a module: its imports read, its store chosen, linked, loaded, started */
static JSValue qw_instantiate(JSContext *ctx, JSValueConst new_target, struct qw_module *mo,
		JSValueConst imports)
{
	struct qw_ctx *c = mo->c;
	struct qw_store *s = NULL;
	struct qw_instance *in;
	struct qw_imp *imps = NULL;
	uint32_t nimps = 0, i, k;
	IM3Module m = NULL, pm = mo->m;
	JSValue sobj = JS_UNDEFINED, obj, ex;
	M3Result r;
	char name[32];
	const char *kept;

	/* the imports, in the module's order (functions, tables, memories, globals: wasm3's),
	 * each read once from the import object */
	nimps = pm->numFuncImports;
	for (i = 0; i < pm->numTables; i++)
		nimps += pm->tables[i]->imported;
	for (i = 0; i < pm->numMemories; i++)
		nimps += pm->memories[i]->imported;
	for (i = 0; i < pm->numGlobals; i++)
		nimps += pm->globals[i].imported;
	if (nimps > 0 && !JS_IsObject(imports))
		return JS_ThrowTypeError(ctx, "WebAssembly.Instance: the module has imports: an "
				"import object is needed");
	if (nimps > 0) {
		imps = calloc(nimps, sizeof *imps);
		if (imps == NULL)
			return JS_ThrowOutOfMemory(ctx);
	}
	k = 0;
	for (i = 0; i < pm->numFuncImports; i++, k++) {
		imps[k].kind = d_externalKind_function;
		imps[k].index = i;
		imps[k].v = qw_import_value(ctx, imports, &pm->functions[i].import);
		if (JS_IsException(imps[k].v))
			goto fail;
	}
	for (i = 0; i < pm->numTables; i++)
		if (pm->tables[i]->imported) {
			imps[k].kind = d_externalKind_table;
			imps[k].index = i;
			imps[k].v = qw_import_value(ctx, imports, &pm->tables[i]->import);
			if (JS_IsException(imps[k++].v))
				goto fail;
		}
	for (i = 0; i < pm->numMemories; i++)
		if (pm->memories[i]->imported) {
			imps[k].kind = d_externalKind_memory;
			imps[k].index = i;
			imps[k].v = qw_import_value(ctx, imports, &pm->memories[i]->import);
			if (JS_IsException(imps[k++].v))
				goto fail;
		}
	for (i = 0; i < pm->numGlobals; i++)
		if (pm->globals[i].imported) {
			imps[k].kind = d_externalKind_global;
			imps[k].index = i;
			imps[k].v = qw_import_value(ctx, imports, &pm->globals[i].import);
			if (JS_IsException(imps[k++].v))
				goto fail;
		}

	/* the store: that of the Memory, Table or Global objects imported (wasm3 links
	 * within a runtime), else that of a function exported by WebAssembly (a direct
	 * call), else a store of its own -- freed with it */
	for (k = 0; k < nimps; k++) {
		struct qw_store *o = NULL;
		void *p;

		if ((p = JS_GetOpaque(imps[k].v, qw_memory_class)) != NULL)
			o = ((struct qw_memory *) p)->s;
		else if ((p = JS_GetOpaque(imps[k].v, qw_table_class)) != NULL)
			o = ((struct qw_table *) p)->s;
		else if ((p = JS_GetOpaque(imps[k].v, qw_global_class)) != NULL)
			o = ((struct qw_global *) p)->s;
		if (o == NULL)
			continue;
		if (o->c != c || (s != NULL && o != s)) {
			qw_throw(ctx, QW_LINK, "the imported memories, tables and globals belong "
					"to different instances' stores");
			goto fail;
		}
		s = o;
	}
	for (k = 0; s == NULL && k < nimps; k++) {
		struct qw_func *fn = JS_GetOpaque(imps[k].v, qw_func_class);
		if (fn != NULL && fn->s->c == c)
			s = fn->s;
	}
	if (s != NULL) {
		qw_pressure(ctx);
		sobj = JS_DupValue(ctx, s->obj);
	} else {
		s = qw_store_new(ctx, c, &sobj);
		if (s == NULL)
			goto fail;
	}

	m = qw_parse(ctx, c, mo->blob, false);
	if (m == NULL)
		goto fail;
	for (k = 0; k < nimps; k++) {
		JSValueConst v = imps[k].v;

		switch (imps[k].kind) {
		case d_externalKind_function: {
			/* a function exported by this store is linked to it (a direct call); any
			 * other callable is called through qw_host_call once loaded */
			IM3Function f = &m->functions[imps[k].index];
			struct qw_func *fn;

			if (!JS_IsFunction(ctx, v)) {
				qw_throw(ctx, QW_LINK, "import %s.%s: a function is expected",
						f->import.moduleUtf8, f->import.fieldUtf8);
				goto fail;
			}
			fn = JS_GetOpaque(v, qw_func_class);
			if (fn != NULL && fn->s == s) {
				IM3Function target = Function_Implementation(fn->f);
				if (target->funcType != f->funcType) {
					qw_throw(ctx, QW_LINK, "import %s.%s: the function's type does "
							"not match", f->import.moduleUtf8, f->import.fieldUtf8);
					goto fail;
				}
				f->resolved = target;
				JS_FreeValue(ctx, imps[k].v);
				imps[k].v = JS_UNDEFINED;
			}
			break;
		}
		case d_externalKind_table: {
			IM3Table t = m->tables[imps[k].index];
			struct qw_table *tt = JS_GetOpaque(v, qw_table_class);

			if (tt == NULL) {
				qw_throw(ctx, QW_LINK, "import %s.%s: a WebAssembly.Table is expected",
						t->import.moduleUtf8, t->import.fieldUtf8);
				goto fail;
			}
			if (qw_rename(ctx, &t->import, tt->modname, tt->expname))
				goto fail;
			break;
		}
		case d_externalKind_memory: {
			IM3Memory mem = m->memories[imps[k].index];
			struct qw_memory *mm = JS_GetOpaque(v, qw_memory_class);

			if (mm == NULL) {
				qw_throw(ctx, QW_LINK, "import %s.%s: a WebAssembly.Memory is expected",
						mem->import.moduleUtf8, mem->import.fieldUtf8);
				goto fail;
			}
			if (qw_rename(ctx, &mem->import, mm->modname, mm->expname))
				goto fail;
			break;
		}
		case d_externalKind_global: {
			IM3Global g = &m->globals[imps[k].index];
			struct qw_global *gg = JS_GetOpaque(v, qw_global_class);
			uint8_t t = BaseTypeOf(g->type);
			uint64_t slot = 0;

			if (gg != NULL) {
				if (qw_rename(ctx, &g->import, gg->modname, gg->expname))
					goto fail;
				break;
			}
			/* a value: only for an immutable global, a Number (a BigInt for i64) */
			if (g->isMutable ||
					(t == c_m3Type_i64 && !JS_IsBigInt(v)) ||
					((t == c_m3Type_i32 || t == c_m3Type_f32 || t == c_m3Type_f64) &&
					 !JS_IsNumber(v))) {
				qw_throw(ctx, QW_LINK, "import %s.%s: a %s is expected",
						g->import.moduleUtf8, g->import.fieldUtf8, g->isMutable ?
						"mutable WebAssembly.Global" : t == c_m3Type_i64 ?
						"BigInt or a WebAssembly.Global" :
						"number or a WebAssembly.Global");
				goto fail;
			}
			if (qw_to_wasm(ctx, s, t, v, &slot))
				goto fail;
			if (t == c_m3Type_i32 || t == c_m3Type_f32)
				memcpy(&g->i64Value, &slot, 4);
			else if (t == c_m3Type_i64 || t == c_m3Type_f64)
				memcpy(&g->i64Value, &slot, 8);
			else
				g->refValue = (void *) (uintptr_t) slot;
			break;
		}
		}
	}

	/* loaded into the store's runtime under a name of its own (the runtime owns it now) */
	snprintf(name, sizeof name, "\x01wasm#%u", ++s->seq);
	kept = qw_keep_name(s, name, mo->blob);
	if (kept == NULL) {
		JS_ThrowOutOfMemory(ctx);
		goto fail;
	}
	m3_SetModuleName(m, kept);
	r = m3_LoadModule(s->rt, m);
	m = NULL;
	if (r) {
		if (r == m3Err_unknownImport || r == m3Err_incompatibleImportType)
			qw_throw(ctx, QW_LINK, "%s", r);
		else if (r == m3Err_mallocFailed || r == m3Err_memoryLimitExceeded)
			JS_ThrowRangeError(ctx, "WebAssembly.Instance: %s", r);
		else
			qw_throw(ctx, QW_RUNTIME, "%s", r);
		goto fail;
	}
	/* the JavaScript functions it imports, bound as raw functions of wasm3 */
	{
		IM3Module lm = m3_FindModule(s->rt, kept);

		for (k = 0; k < nimps; k++) {
			struct qw_import *im;

			if (imps[k].kind != d_externalKind_function || JS_IsUndefined(imps[k].v))
				continue;
			im = calloc(1, sizeof *im);
			if (im == NULL) {
				JS_ThrowOutOfMemory(ctx);
				goto fail;
			}
			im->s = s;
			im->fn = imps[k].v;	/* (the store object owns it now) */
			imps[k].v = JS_UNDEFINED;
			im->next = s->imports;
			s->imports = im;
			r = CompileRawFunction(lm, &lm->functions[imps[k].index],
					(const void *) qw_host_call, im);
			if (r) {
				qw_throw(ctx, QW_LINK, "%s", r);
				goto fail;
			}
		}
		ex = qw_exports(ctx, s, sobj, lm);
		if (JS_IsException(ex))
			goto fail;
		/* the start function */
		{
			JSContext *outer = qw_running;

			qw_running = ctx;
			r = m3_RunStart(lm);
			qw_running = outer;
			qw_refresh(ctx, s);
			if (r) {
				JS_FreeValue(ctx, ex);
				qw_throw_result(ctx, s, r, QW_RUNTIME);
				goto fail;
			}
		}
	}
	in = calloc(1, sizeof *in);
	obj = in != NULL ? (JS_IsUndefined(new_target) ? JS_NewObjectClass(ctx, qw_instance_class) :
		qw_new_of(ctx, new_target, qw_instance_class)) : JS_ThrowOutOfMemory(ctx);
	if (JS_IsException(obj)) {
		free(in);
		JS_FreeValue(ctx, ex);
		goto fail;
	}
	in->s = s;
	s->refs++;
	in->store = sobj;
	in->exports = ex;
	JS_SetOpaque(obj, in);
	for (k = 0; k < nimps; k++)
		JS_FreeValue(ctx, imps[k].v);
	free(imps);
	return obj;

fail:
	if (m != NULL)
		m3_FreeModule(m);
	for (k = 0; imps != NULL && k < nimps; k++)
		JS_FreeValue(ctx, imps[k].v);
	free(imps);
	JS_FreeValue(ctx, sobj);
	return JS_EXCEPTION;
}

static JSValue qw_instance_ctor(JSContext *ctx, JSValueConst new_target, int argc,
		JSValueConst *argv)
{
	struct qw_module *mo;

	if (JS_IsUndefined(new_target))
		return qw_nonew(ctx);
	mo = JS_GetOpaque(argc > 0 ? argv[0] : JS_UNDEFINED, qw_module_class);
	if (mo == NULL)
		return JS_ThrowTypeError(ctx, "WebAssembly.Instance: a WebAssembly.Module is "
				"expected");
	return qw_instantiate(ctx, new_target, mo, argc > 1 ? argv[1] : JS_UNDEFINED);
}

static JSValue qw_instance_exports(JSContext *ctx, JSValueConst this_val)
{
	struct qw_instance *in = JS_GetOpaque2(ctx, this_val, qw_instance_class);

	if (in == NULL)
		return JS_EXCEPTION;
	return JS_DupValue(ctx, in->exports);
}


/* ---- WebAssembly.validate and the natives -------------------------------------------------- */

static JSValue qw_validate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qw_ctx *c = qw_ctx_of(ctx);
	struct qw_blob *b;
	IM3Module m;

	(void) this_val;
	if (c == NULL)
		return JS_FALSE;
	b = qw_bytes(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
	if (b == NULL)
		return JS_EXCEPTION;
	m = qw_parse(ctx, c, b, true);
	qw_blob_unref(b);
	if (m == NULL) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return JS_FALSE;
	}
	m3_FreeModule(m);
	return JS_TRUE;
}

/* N.wasmErrors(CompileError, LinkError, RuntimeError): wasm.js's classes, thrown here */
static JSValue n_wasm_errors(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qw_ctx *c = qw_ctx_of(ctx);
	int i;

	(void) this_val;
	if (c == NULL)
		return JS_UNDEFINED;
	for (i = 0; i < 3 && i < argc; i++) {
		JS_FreeValue(ctx, c->err[i]);
		c->err[i] = JS_DupValue(ctx, argv[i]);
	}
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry qw_natives[] = {
	JS_CFUNC_DEF("wasmErrors", 3, n_wasm_errors),
};

static const JSCFunctionListEntry qw_module_proto[] = {
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Module", JS_PROP_CONFIGURABLE),
};
static const JSCFunctionListEntry qw_module_static[] = {
	JS_CFUNC_DEF("exports", 1, qw_module_exports),
	JS_CFUNC_DEF("imports", 1, qw_module_imports),
	JS_CFUNC_DEF("customSections", 2, qw_module_sections),
};
static const JSCFunctionListEntry qw_instance_proto[] = {
	JS_CGETSET_DEF("exports", qw_instance_exports, NULL),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Instance", JS_PROP_CONFIGURABLE),
};
static const JSCFunctionListEntry qw_memory_proto[] = {
	JS_CGETSET_DEF("buffer", qw_memory_buffer, NULL),
	JS_CFUNC_DEF("grow", 1, qw_memory_grow),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Memory", JS_PROP_CONFIGURABLE),
};
static const JSCFunctionListEntry qw_table_proto[] = {
	JS_CGETSET_DEF("length", qw_table_length, NULL),
	JS_CFUNC_DEF("get", 1, qw_table_get),
	JS_CFUNC_DEF("set", 1, qw_table_set),
	JS_CFUNC_DEF("grow", 1, qw_table_grow),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Table", JS_PROP_CONFIGURABLE),
};
static const JSCFunctionListEntry qw_global_proto[] = {
	JS_CGETSET_DEF("value", qw_global_value, qw_global_set_value),
	JS_CFUNC_DEF("valueOf", 0, qw_global_valueof),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Global", JS_PROP_CONFIGURABLE),
};
static const JSCFunctionListEntry qw_ns[] = {
	JS_CFUNC_DEF("validate", 1, qw_validate),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly", JS_PROP_CONFIGURABLE),
};

static JSClassDef qw_classdefs[] = {
	{ .class_name = "WebAssemblyStore", .finalizer = qw_store_finalizer, .gc_mark = qw_store_mark },
	{ .class_name = "Module", .finalizer = qw_module_finalizer },
	{ .class_name = "Instance", .finalizer = qw_instance_finalizer, .gc_mark = qw_instance_mark },
	{ .class_name = "Function", .finalizer = qw_func_finalizer, .gc_mark = qw_func_mark,
	  .call = qw_func_callfn },
	{ .class_name = "Memory", .finalizer = qw_memory_finalizer, .gc_mark = qw_memory_mark },
	{ .class_name = "Table", .finalizer = qw_table_finalizer, .gc_mark = qw_table_mark },
	{ .class_name = "Global", .finalizer = qw_global_finalizer, .gc_mark = qw_global_mark },
};

/* a class of the namespace: its constructor, its prototype's members */
static void qw_class(JSContext *ctx, JSValueConst ns, JSClassID cls, const char *name,
		JSCFunction *ctor, int length, const JSCFunctionListEntry *proto_list, int nproto,
		const JSCFunctionListEntry *static_list, int nstatic)
{
	JSValue proto = JS_NewObject(ctx);
	JSValue fn = JS_NewCFunction2(ctx, ctor, name, length, JS_CFUNC_constructor, 0);

	JS_SetPropertyFunctionList(ctx, proto, proto_list, nproto);
	if (static_list != NULL)
		JS_SetPropertyFunctionList(ctx, fn, static_list, nstatic);
	JS_SetConstructor(ctx, fn, proto);
	JS_SetClassProto(ctx, cls, proto);
	JS_DefinePropertyValueStr(ctx, ns, name, fn, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
}

/* exported interface documented in qjs_wasm.h */
void qjs_wasm_setup(JSContext *ctx, JSValueConst natives)
{
	JSRuntime *rt = JS_GetRuntime(ctx);
	JSClassID *ids[7] = { &qw_store_class, &qw_module_class, &qw_instance_class,
		&qw_func_class, &qw_memory_class, &qw_table_class, &qw_global_class };
	struct qw_ctx *c;
	JSValue ns, fn, r, g;
	int i;

	if (qw_store_class == 0)
		for (i = 0; i < 7; i++)
			JS_NewClassID(rt, ids[i]);
	if (!JS_IsRegisteredClass(rt, qw_store_class))
		for (i = 0; i < 7; i++)
			JS_NewClass(rt, *ids[i], &qw_classdefs[i]);
	c = calloc(1, sizeof *c);
	if (c == NULL)
		return;
	c->env = m3_NewEnvironment();
	if (c->env == NULL) {
		free(c);
		return;
	}
	c->ctx = ctx;
	c->refs = 1;
	for (i = 0; i < 3; i++)
		c->err[i] = JS_UNDEFINED;
	c->next = qw_ctxs;
	qw_ctxs = c;

	ns = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, ns, qw_ns, sizeof qw_ns / sizeof qw_ns[0]);
	qw_class(ctx, ns, qw_module_class, "Module", qw_module_ctor, 1, qw_module_proto,
			sizeof qw_module_proto / sizeof qw_module_proto[0], qw_module_static,
			sizeof qw_module_static / sizeof qw_module_static[0]);
	qw_class(ctx, ns, qw_instance_class, "Instance", qw_instance_ctor, 1, qw_instance_proto,
			sizeof qw_instance_proto / sizeof qw_instance_proto[0], NULL, 0);
	qw_class(ctx, ns, qw_memory_class, "Memory", qw_memory_ctor, 1, qw_memory_proto,
			sizeof qw_memory_proto / sizeof qw_memory_proto[0], NULL, 0);
	qw_class(ctx, ns, qw_table_class, "Table", qw_table_ctor, 1, qw_table_proto,
			sizeof qw_table_proto / sizeof qw_table_proto[0], NULL, 0);
	qw_class(ctx, ns, qw_global_class, "Global", qw_global_ctor, 1, qw_global_proto,
			sizeof qw_global_proto / sizeof qw_global_proto[0], NULL, 0);
	g = JS_GetGlobalObject(ctx);
	JS_DefinePropertyValueStr(ctx, g, "WebAssembly", JS_DupValue(ctx, ns),
			JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
	JS_FreeValue(ctx, g);

	JS_SetPropertyFunctionList(ctx, natives, qw_natives,
			sizeof qw_natives / sizeof qw_natives[0]);
	fn = qjs_eval_cached(ctx, qjs_wasm_js, sizeof(qjs_wasm_js) - 1, "wasm.js", &qw_bc,
			&qw_bc_len);
	if (JS_IsException(fn)) {
		JSValue e = JS_GetException(ctx);
		const char *msg = JS_ToCString(ctx, e);
		fprintf(stderr, "JS wasm.js: %s\n", msg ? msg : "?");
		JS_FreeCString(ctx, msg);
		JS_FreeValue(ctx, e);
		JS_FreeValue(ctx, ns);
		return;
	}
	{
		JSValue args[2] = { JS_DupValue(ctx, natives), ns };
		r = JS_Call(ctx, fn, JS_UNDEFINED, 2, (JSValueConst *) args);
		JS_FreeValue(ctx, args[0]);
	}
	if (JS_IsException(r)) {
		JSValue e = JS_GetException(ctx);
		const char *msg = JS_ToCString(ctx, e);
		fprintf(stderr, "JS wasm.js setup: %s\n", msg ? msg : "?");
		JS_FreeCString(ctx, msg);
		JS_FreeValue(ctx, e);
	}
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, fn);
	JS_FreeValue(ctx, ns);
}

/* exported interface documented in qjs_wasm.h */
void qjs_wasm_context_gone(JSContext *ctx)
{
	struct qw_ctx *c = qw_ctx_of(ctx);
	int i;

	if (c == NULL)
		return;
	for (i = 0; i < 3; i++) {
		JS_FreeValue(ctx, c->err[i]);
		c->err[i] = JS_UNDEFINED;
	}
	if (c->store != NULL && c->store->has_exc) {
		JS_FreeValue(ctx, c->store->exc);
		c->store->has_exc = false;
	}
	if (qw_running == ctx)
		qw_running = NULL;
	c->ctx = NULL;
	qw_ctx_unref(c);
}
