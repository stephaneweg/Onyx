/*
 * tools/tests/netsurf/intl/qjsintl.c -- QuickJS with NetSurf's Intl (qjs_intl.h), on the PC: runs
 * script files in one context (print / console.log to stdout), for intl.js' tests and test262.
 *
 *   qjsintl [-m] file.js ...      (-m: time a context's creation, the first Intl use)
 *
 * Built by tools/tests/netsurf/intl/build.sh. Exit status 1 when a script throws.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "quickjs.h"
#include "qjs_intl.h"

static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int i;
	(void) this_val;
	for (i = 0; i < argc; i++) {
		const char *s = JS_ToCString(ctx, argv[i]);
		if (i)
			putchar(' ');
		fputs(s ? s : "<?>", stdout);
		JS_FreeCString(ctx, s);
	}
	putchar('\n');
	return JS_UNDEFINED;
}

static char *readfile(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *b;
	long n;
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(n + 1);
	*len = fread(b, 1, n, f);
	b[*len] = 0;
	fclose(f);
	return b;
}

static double now_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void report(JSContext *ctx)
{
	JSValue e = JS_GetException(ctx);
	const char *s = JS_ToCString(ctx, e);
	JSValue st = JS_IsObject(e) ? JS_GetPropertyStr(ctx, e, "stack") : JS_UNDEFINED;
	const char *ss = JS_IsUndefined(st) ? NULL : JS_ToCString(ctx, st);
	printf("EXCEPTION: %s\n%s", s ? s : "?", ss ? ss : "");
	JS_FreeCString(ctx, s);
	if (ss)
		JS_FreeCString(ctx, ss);
	JS_FreeValue(ctx, st);
	JS_FreeValue(ctx, e);
}

int main(int argc, char **argv)
{
	JSRuntime *rt = JS_NewRuntime();
	JSContext *ctx;
	JSValue g, console;
	int i, fail = 0, measure = 0;
	double t0, t1;

	if (argc > 1 && strcmp(argv[1], "-m") == 0) {
		measure = 1;
		argv++; argc--;
	}
	JS_SetMaxStackSize(rt, 4 * 1024 * 1024);
	if (measure) {
		/* a context and the boot, 20 times; the first Intl use twice (compile, then bytecode) */
		int k;
		t0 = now_ms();
		for (k = 0; k < 20; k++) {
			JSContext *c = JS_NewContext(rt);
			JS_FreeContext(c);
		}
		t1 = now_ms();
		printf("context: %.3f ms\n", (t1 - t0) / 20);
		t0 = now_ms();
		for (k = 0; k < 20; k++) {
			JSContext *c = JS_NewContext(rt);
			qjs_intl_init(c);
			JS_FreeContext(c);
		}
		t1 = now_ms();
		printf("context + intl boot: %.3f ms\n", (t1 - t0) / 20);
		for (k = 0; k < 3; k++) {
			JSContext *c = JS_NewContext(rt);
			JSValue r;
			qjs_intl_init(c);
			t0 = now_ms();
			r = JS_Eval(c, "new Intl.DateTimeFormat('en').format(0)", 39, "m", 0);
			t1 = now_ms();
			printf("first Intl use (%s): %.3f ms\n", k ? "bytecode" : "compile", t1 - t0);
			JS_FreeValue(c, r);
			JS_FreeContext(c);
		}
		printf("bytecode: %zu bytes\n", qjs_intl_bc_len);
	}
	ctx = JS_NewContext(rt);
	qjs_intl_init(ctx);
	g = JS_GetGlobalObject(ctx);
	JS_SetPropertyStr(ctx, g, "print", JS_NewCFunction(ctx, js_print, "print", 1));
	console = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_print, "log", 1));
	JS_SetPropertyStr(ctx, g, "console", console);
	JS_FreeValue(ctx, g);
	for (i = 1; i < argc; i++) {
		size_t len;
		char *src = readfile(argv[i], &len);
		JSValue r;
		if (!src) {
			printf("cannot read %s\n", argv[i]);
			fail = 1;
			continue;
		}
		r = JS_Eval(ctx, src, len, argv[i], JS_EVAL_TYPE_GLOBAL);
		free(src);
		if (JS_IsException(r)) {
			report(ctx);
			fail = 1;
		}
		JS_FreeValue(ctx, r);
		for (;;) {
			JSContext *c;
			int e = JS_ExecutePendingJob(rt, &c);
			if (e <= 0) {
				if (e < 0) { report(c); fail = 1; }
				break;
			}
		}
		if (fail)
			break;
	}
	JS_FreeContext(ctx);
	JS_FreeRuntime(rt);
	return fail;
}
