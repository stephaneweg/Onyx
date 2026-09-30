/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: Intl for QuickJS (intl.js, compiled in as qjs_intl_js.h with the locale data,
 * third_party/cldr-48/intl-data.txt). Included by qjs.c (and the PC test runner,
 * tools/tests/netsurf/intl/qjsintl.c); qjs_intl_init (ctx) is called on every new context
 * before dom.js: it evaluates intl.js' boot part (the Intl object, whose members load the rest at
 * their first use, and the built-ins' locale methods). The implementation part is compiled once
 * per process into bytecode and read back in each context that uses Intl.
 *
 * The natives of the implementation: data (key) -> a record of the locale data (JSON text, or
 * null); zone () -> the host's time zone name (the PC: TZ or /etc/localtime; Onyx: none, intl.js
 * guesses it from the Date's offset and the locale's region).
 *
 * On Onyx the clock (gettimeofday) gives the local time: the offset from UTC of SD:/etc/system.ini
 * (timezone=, minutes) is given to QuickJS (js_onyx_utc_offset_min, quickjs.c) so that Date's
 * time values are UTC and its local time is the clock's.
 */
#ifndef QJS_INTL_H
#define QJS_INTL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <unistd.h>
#endif
#include "quickjs.h"
#include "qjs_intl_js.h"	/* qjs_intl_js[] (intl.js), qjs_intl_data[] (the locale data) */

#define QJS_INTL_MARK "//@@INTL-IMPL@@"

/* data (key): the line "key\t<json>" of the data */
static JSValue qjs_intl_data_fn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *key, *p, *e;
	size_t kl;
	JSValue r = JS_NULL;
	(void) this_val;
	if (argc < 1)
		return JS_NULL;
	key = JS_ToCStringLen(ctx, &kl, argv[0]);
	if (key == NULL)
		return JS_EXCEPTION;
	for (p = qjs_intl_data; (p = strstr(p, key)) != NULL; p += kl) {
		if ((p == qjs_intl_data || p[-1] == '\n') && p[kl] == '\t') {
			p += kl + 1;
			e = strchr(p, '\n');
			if (e == NULL)
				e = p + strlen(p);
			r = JS_NewStringLen(ctx, p, (size_t) (e - p));
			break;
		}
	}
	JS_FreeCString(ctx, key);
	return r;
}

/* zone (): the host's IANA time zone name, or null */
static JSValue qjs_intl_zone_fn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void) this_val; (void) argc; (void) argv;
#if defined(__linux__)
	{
		const char *tz = getenv("TZ");
		char buf[256];
		ssize_t n;
		char *z;
		if (tz != NULL && *tz != '\0')
			return JS_NewString(ctx, tz[0] == ':' ? tz + 1 : tz);
		n = readlink("/etc/localtime", buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			z = strstr(buf, "zoneinfo/");
			if (z != NULL)
				return JS_NewString(ctx, z + 9);
		}
	}
#endif
	return JS_NULL;
}

/* the implementation's bytecode, made once per process */
static uint8_t *qjs_intl_bc;
static size_t qjs_intl_bc_len;

/* load (): the implementation, evaluated in this context -> its members */
static JSValue qjs_intl_load(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *impl = strstr(qjs_intl_js, QJS_INTL_MARK);
	JSValue fn, natives, r;
	(void) this_val; (void) argc; (void) argv;
	if (impl == NULL)
		return JS_ThrowInternalError(ctx, "intl.js: no implementation");
	if (qjs_intl_bc == NULL) {
		JSValue obj = JS_Eval(ctx, impl, strlen(impl), "intl.js",
				JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
		if (JS_IsException(obj))
			return obj;
		uint8_t *bc = JS_WriteObject(ctx, &qjs_intl_bc_len, obj, JS_WRITE_OBJ_BYTECODE);
		if (bc != NULL) {
			/* kept in the process's heap: the runtime that made it goes with its window */
			qjs_intl_bc = malloc(qjs_intl_bc_len);
			if (qjs_intl_bc != NULL)
				memcpy(qjs_intl_bc, bc, qjs_intl_bc_len);
			js_free(ctx, bc);
		}
		fn = JS_EvalFunction(ctx, obj);
	} else {
		JSValue obj = JS_ReadObject(ctx, qjs_intl_bc, qjs_intl_bc_len, JS_READ_OBJ_BYTECODE);
		if (JS_IsException(obj))
			return obj;
		fn = JS_EvalFunction(ctx, obj);
	}
	if (JS_IsException(fn))
		return fn;
	natives = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, natives, "data", JS_NewCFunction(ctx, qjs_intl_data_fn, "data", 1));
	JS_SetPropertyStr(ctx, natives, "zone", JS_NewCFunction(ctx, qjs_intl_zone_fn, "zone", 0));
	r = JS_Call(ctx, fn, JS_UNDEFINED, 1, (JSValueConst *) &natives);
	JS_FreeValue(ctx, natives);
	JS_FreeValue(ctx, fn);
	return r;
}

#if !defined(__linux__)
/* Onyx: the clock's offset from UTC (quickjs.c) from SD:/etc/system.ini's timezone= */
extern int js_onyx_utc_offset_min;
static void qjs_intl_onyx_tz(void)
{
	static int done;
	char line[128];
	FILE *f;
	if (done)
		return;
	done = 1;
	f = fopen("SD:/etc/system.ini", "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof(line), f) != NULL) {
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, "timezone", 8) == 0) {
			p += 8;
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p == '=') {
				js_onyx_utc_offset_min = atoi(p + 1);
				break;
			}
		}
	}
	fclose(f);
}
#endif

static void qjs_intl_error(JSContext *ctx)
{
	JSValue e = JS_GetException(ctx);
	const char *s = JS_ToCString(ctx, e);
	fprintf(stderr, "intl.js: %s\n", s != NULL ? s : "?");
	if (s != NULL)
		JS_FreeCString(ctx, s);
	JS_FreeValue(ctx, e);
}

/* on a new context: the Intl object and the built-ins (the boot part of intl.js) */
static void qjs_intl_init(JSContext *ctx)
{
	const char *impl = strstr(qjs_intl_js, QJS_INTL_MARK);
	JSValue boot, load, r;
#if !defined(__linux__)
	qjs_intl_onyx_tz();
#endif
	static char *bootsrc;	/* the boot part, NUL-terminated (JS_Eval wants it) */
	static size_t bootlen;
	if (impl == NULL)
		return;
	if (bootsrc == NULL) {
		bootlen = (size_t) (impl - qjs_intl_js);
		bootsrc = malloc(bootlen + 1);
		if (bootsrc == NULL)
			return;
		memcpy(bootsrc, qjs_intl_js, bootlen);
		bootsrc[bootlen] = '\0';
	}
	boot = JS_Eval(ctx, bootsrc, bootlen, "intl.js", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(boot)) {
		qjs_intl_error(ctx);
		return;
	}
	load = JS_NewCFunction(ctx, qjs_intl_load, "load", 0);
	r = JS_Call(ctx, boot, JS_UNDEFINED, 1, (JSValueConst *) &load);
	if (JS_IsException(r))
		qjs_intl_error(ctx);
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, load);
	JS_FreeValue(ctx, boot);
}

#endif
