/*
 * tools/tests/netsurf/jit/qjsrun.c -- QuickJS-ng (third_party/quickjs-ng-0.17.0) alone, for
 * measuring the engine and testing changes to it without the browser: runs script files in
 * one context (print, console.log, a minimal $262 for test262, performance.now), then the
 * promise jobs. Built for the PC and for AArch64 Linux (run by qemu-aarch64): run.sh.
 *
 *   qjsrun [-q] file.js...        exit status 0 unless a script threw
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "quickjs.h"

static int quiet;

static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int i;

	(void) this_val;
	for (i = 0; i < argc; i++) {
		const char *s = JS_ToCString(ctx, argv[i]);
		if (i)
			putchar(' ');
		fputs(s ? s : "?", stdout);
		JS_FreeCString(ctx, s);
	}
	putchar('\n');
	fflush(stdout);
	return JS_UNDEFINED;
}

static JSValue js_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct timespec t;

	(void) this_val;
	(void) argc;
	(void) argv;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return JS_NewFloat64(ctx, t.tv_sec * 1e3 + t.tv_nsec / 1e6);
}

static JSValue js_eval_script(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	size_t len;
	const char *s;
	JSValue r;

	(void) this_val;
	if (argc < 1)
		return JS_UNDEFINED;
	s = JS_ToCStringLen(ctx, &len, argv[0]);
	if (s == NULL)
		return JS_EXCEPTION;
	r = JS_Eval(ctx, s, len, "<evalScript>", JS_EVAL_TYPE_GLOBAL);
	JS_FreeCString(ctx, s);
	return r;
}

static JSValue js_gc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void) this_val;
	(void) argc;
	(void) argv;
	JS_RunGC(JS_GetRuntime(ctx));
	return JS_UNDEFINED;
}

static char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *b;
	long n;

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t) n + 1);
	if (b == NULL || fread(b, 1, (size_t) n, f) != (size_t) n) {
		fclose(f);
		free(b);
		return NULL;
	}
	b[n] = 0;
	fclose(f);
	*len = (size_t) n;
	return b;
}

static int report(JSContext *ctx)
{
	JSValue e = JS_GetException(ctx);
	const char *s = JS_ToCString(ctx, e);
	JSValue st = JS_IsObject(e) ? JS_GetPropertyStr(ctx, e, "stack") : JS_UNDEFINED;
	const char *ss = JS_IsString(st) ? JS_ToCString(ctx, st) : NULL;

	printf("Uncaught %s\n%s", s ? s : "?", ss ? ss : "");
	JS_FreeCString(ctx, s);
	if (ss)
		JS_FreeCString(ctx, ss);
	JS_FreeValue(ctx, st);
	JS_FreeValue(ctx, e);
	return 1;
}

int main(int argc, char **argv)
{
	JSRuntime *rt = JS_NewRuntime();
	JSContext *ctx = JS_NewContext(rt);
	JSValue g = JS_GetGlobalObject(ctx), o, r;
	int i, status = 0;

	JS_SetMaxStackSize(rt, 4 * 1024 * 1024);
	JS_SetPropertyStr(ctx, g, "print", JS_NewCFunction(ctx, js_print, "print", 1));
	o = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, o, "log", JS_NewCFunction(ctx, js_print, "log", 1));
	JS_SetPropertyStr(ctx, g, "console", o);
	o = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, o, "now", JS_NewCFunction(ctx, js_now, "now", 0));
	JS_SetPropertyStr(ctx, g, "performance", o);
	o = JS_NewObject(ctx);	/* test262's host object, the parts its harness needs */
	JS_SetPropertyStr(ctx, o, "evalScript", JS_NewCFunction(ctx, js_eval_script, "evalScript", 1));
	JS_SetPropertyStr(ctx, o, "gc", JS_NewCFunction(ctx, js_gc, "gc", 0));
	JS_SetPropertyStr(ctx, o, "global", JS_DupValue(ctx, g));
	JS_SetPropertyStr(ctx, g, "$262", o);
	JS_SetPropertyStr(ctx, g, "self", JS_DupValue(ctx, g));	/* (UMD bundles look for it) */
	for (i = 1; i < argc; i++) {
		size_t len;
		char *src;

		if (strcmp(argv[i], "-q") == 0) {
			quiet = 1;
			continue;
		}
		src = read_file(argv[i], &len);
		if (src == NULL) {
			printf("cannot read %s\n", argv[i]);
			return 2;
		}
		r = JS_Eval(ctx, src, len, argv[i], JS_EVAL_TYPE_GLOBAL);
		free(src);
		if (JS_IsException(r)) {
			status = report(ctx);
			break;
		}
		JS_FreeValue(ctx, r);
		for (;;) {	/* the promise jobs */
			JSContext *c;
			int e = JS_ExecutePendingJob(rt, &c);
			if (e <= 0) {
				if (e < 0)
					status = report(c);
				break;
			}
		}
	}
	JS_FreeValue(ctx, g);
	JS_FreeContext(ctx);
	JS_FreeRuntime(rt);
	return status;
}
