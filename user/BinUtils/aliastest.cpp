//
// aliastest -- the tests of kapi v97 on the Pi (docs/POCKETUI-TECH-STUDY.md section 3.9, phase P1): a
// library under a second name (kapi_lib_open_as), the graphics server chosen from SD:/etc/system.ini
// "shell =", the switch of the server (KAPI_WS_SWITCH). It uses the loader's test library and its
// second build: SD:/lib/demo2.so is aliased as SD:/lib/demo.so (/bin/libtest's child says which build
// a program gets: 41 the first, 42 the second).
//
//   aliastest                    from a shell: the checks of a program that is not the server --
//                                kapi_lib_open_as refused (-EPERM), and whether demo.so is aliased now
//                                (the kernel's image list, a program's bind, unload refused); PASS /
//                                FAIL lines, exit 0 only if all passed
//   aliastest --expect 1|2       the same, demo.so expected to give the first (1) or the second (2) build
//   aliastest --switch           KAPI_WS_SWITCH: the server ended, "shell =" read again, its server
//                                started -- says what came of it (0 the one asked for, 1 Elegant instead)
//   aliastest --serve ...        A STAND-IN GRAPHICS SERVER: copied to SD:/bin/pocketui, the kernel starts
//                                it for shell = pocket or console ("--serve [--restart] --mode <m>").
//                                It registers, checks lib_open_as as the server (refusals, the alias
//                                set, asked again), and writes its PASS / FAIL lines to
//                                SD:/tmp/aliastest.txt. mode pocket: it takes the display and shows a
//                                band at the top of the screen (green: its checks passed, red: one
//                                failed; blue when started again after a crash, --restart),
//                                answers every program's request with an error, ends on
//                                KAPI_WS_IN_QUIT (a switch); the key 'x' makes it exit as a crash
//                                would (the kernel starts it again with --restart). mode console: it
//                                never takes the display (the kernel kills it after 5 s and starts
//                                Elegant: the fallback).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "appkit/appkit.h"
#include "onyxpp.hpp"
#include "lib.h"
#include "demo/demo.h"

#define LIB		"SD:/lib/demo.so"		// the alias
#define LIB2		"SD:/lib/demo2.so"		// the real library
#define LIBTEST		"SD:/bin/libtest"
#define REPORT		"SD:/tmp/aliastest.txt"

static int slen (const char *s)			{ int n = 0; while (s[n]) n++; return n; }
static bool seq (const char *a, const char *b)	{ while (*a && *a == *b) { a++; b++; } return *a == *b; }
static bool has (const char *s, const char *w)	// the word w among the words of s
{
	int n = slen (w);
	for (const char *p = s; *p; p++)
	{
		if (p != s && p[-1] != ' ') continue;
		int k = 0;
		while (k < n && p[k] == w[k]) k++;
		if (k == n && (p[n] == 0 || p[n] == ' ')) return true;
	}
	return false;
}

// ---- the lines: to the console (a shell), or into a buffer written to REPORT (the server) -------------

static char s_Report[4096];
static int s_nReport, s_failed;
static bool s_bServer;

static void put (const char *s)
{
	if (!s_bServer) { kapi_stdout_write (s, (unsigned) slen (s)); return; }
	for (; *s && s_nReport < (int) sizeof s_Report - 1; s++) s_Report[s_nReport++] = *s;
}
static void putn (long v)
{
	char b[24]; int i = 0;
	bool neg = v < 0;
	unsigned long u = neg ? (unsigned long) -v : (unsigned long) v;
	do { b[i++] = (char) ('0' + u % 10); u /= 10; } while (u != 0);
	if (neg) put ("-");
	while (i > 0) { char c[2] = { b[--i], 0 }; put (c); }
}
static void check (const char *name, bool ok, long v)
{
	put (ok ? "PASS " : "FAIL "); put (name);
	if (!ok) { put (" ("); putn (v); put (")"); s_failed++; }
	put ("\n");
}
static void report_save (void)
{
	kapi_mkdir ("SD:/tmp");
	kapi_save_file (REPORT, s_Report, (unsigned) s_nReport);
}

// ---- a program (not the server) -----------------------------------------------------------------------

static int client (int expect)
{
	int err = 0;
	const void *t = kapi_lib_open_as (LIB2, LIB, 1, &err);
	if (err == -KAPI_ENOSYS) { put ("aliastest: this kernel has no kapi_lib_open_as (kapi v97)\n"); return 1; }
	check ("lib_open_as refused to a program that is not the server", t == 0 && err == -KAPI_EPERM, err);

	struct kapi_image_info info;
	int n = kapi_image_list (LIB, &info, 1);
	bool aliased = n == 1 && (info.flags & KAPI_IMG_ALIAS) != 0;
	put (aliased ? "demo.so is aliased: " : "demo.so is not aliased now\n");
	if (aliased)
	{
		const char *alias = info.path + slen (info.path) + 1;
		put (alias); put (" -> "); put (info.path); put ("\n");
		check ("the alias's real path is demo2.so", seq (info.path, "sd:/lib/demo2.so") && seq (alias, "sd:/lib/demo.so"), 0);
		check ("unload of the alias refused", kapi_image_unload (LIB) == -KAPI_EBUSY, 0);
	}
	// a program's open of demo.so: which build it gets (/bin/libtest's child)
	void *p = kapi_spawn (LIBTEST, "--child build " LIB, 0, 0);
	int b = p != 0 ? kapi_wait (p) : -1000;
	put ("a program opening demo.so gets build "); putn (b - 40); put ("\n");
	if (expect != 0) check ("the build expected", b == 40 + expect, b);
	else check ("the build follows the alias", b == (aliased ? 42 : 41), b);
	return s_failed == 0 ? 0 : 1;
}

static int do_switch (void)
{
	put ("switching the graphics server (SD:/etc/system.ini \"shell =\")...\n");
	long r = kapi_ws_ctl (KAPI_WS_SWITCH, 0, 0, 0);
	put (r == 0 ? "the server asked for has the display\n"
	   : r == 1 ? "its server failed: Elegant has the display (the desktop)\n"
	   : r == -KAPI_EBUSY ? "refused: a full-screen program has the display, or a start is under way\n"
	   : r == -KAPI_EIO ? "no server took the display: the console\n"
	   : r == -KAPI_ENOSYS || r == -KAPI_EINVAL ? "this kernel has no KAPI_WS_SWITCH (kapi v97)\n" : "failed\n");
	if (r < 0) { put ("("); putn (r); put (")\n"); }
	return r >= 0 ? 0 : 1;
}

// ---- the stand-in server ------------------------------------------------------------------------------

static struct kapi_ws_req s_Req;			// (4 KB of data: not on the stack)

static int serve (const char *args)
{
	s_bServer = true;
	bool restart = has (args, "--restart"), console = has (args, "console");
	put ("aliastest --serve: "); put (args); put ("\n");
	long r = kapi_ws_ctl (KAPI_WS_REGISTER, 0, 0, 0);
	check ("the role (KAPI_WS_REGISTER)", r == 1, r);
	if (r != 1) { report_save (); return 1; }
	int err = 0;
	check ("lib_open_as: an alias on appkit.so refused",
	       kapi_lib_open_as (LIB2, "SD:/lib/appkit.so", 1, &err) == 0 && err == -KAPI_EPERM, err);
	check ("lib_open_as: appkit.so itself refused",
	       kapi_lib_open_as ("SD:/lib/appkit.so", "SD:/lib/x.so", 1, &err) == 0 && err == -KAPI_EPERM, err);
	check ("lib_open_as: an alias outside SD:/lib/ refused",
	       kapi_lib_open_as (LIB2, "SD:/bin/demo.so", 1, &err) == 0 && err == -KAPI_EPERM, err);
	check ("lib_open_as: path = alias refused", kapi_lib_open_as (LIB2, LIB2, 1, &err) == 0 && err == -KAPI_EINVAL, err);
	check ("lib_open_as: a version too new refused", kapi_lib_open_as (LIB2, LIB, 99, &err) == 0 && err == -KAPI_ENOTSUP, err);
	const TDemoTable *t = (const TDemoTable *) kapi_lib_open_as (LIB2, LIB, 1, &err);
	check ("lib_open_as (demo2.so as demo.so)", t != 0, err);
	if (t != 0)
	{
		check ("its table is demo2's", t->init (lib_cxx_imports ()) == 0 && t->build () == 2, 0);
		check ("asked again: the same table", kapi_lib_open_as (LIB2, LIB, 1, &err) == t, err);
		check ("a second alias on it refused", kapi_lib_open_as (LIB2, "SD:/lib/demo3.so", 1, &err) == 0 && err == -KAPI_EBUSY, err);
		check ("lib_open of the alias: the same table", kapi_lib_open (LIB, 1, &err) == t, err);
	}
	if (restart) put ("(started again by the kernel: --restart)\n");
	report_save ();
	if (console)						// the fallback's test: never takes the display
	{
		for (;;) kapi_msleep (1000);
	}

	struct kapi_ws_display D;
	if (kapi_ws_ctl (KAPI_WS_DISPLAY, 1, (long) &D, 0) != 0) { put ("FAIL the display\n"); report_save (); return 1; }
	const int band = 48;
	unsigned *px = new unsigned[(unsigned long) D.w * band];
	unsigned colour = s_failed != 0 ? 0xC02020 : restart ? 0x2050C0 : 0x20A040;
	for (long i = 0; i < (long) D.w * band; i++) px[i] = colour;
	struct kapi_ws_present P;
	__builtin_memset (&P, 0, sizeof P);
	P.pixels = px; P.stride = D.w; P.x = 0; P.y = 0; P.w = D.w; P.h = band;
	for (;;)
	{
		long pending = kapi_ws_ctl (KAPI_WS_WAIT, 500, 0, 0);
		if (pending < 0) break;					// (the display taken back)
		kapi_ws_ctl (KAPI_WS_PRESENT, (long) &P, 0, 0);		// (the band, again: not silent)
		struct kapi_ws_input in[16];
		long n = kapi_ws_ctl (KAPI_WS_INPUT, (long) in, 16, 0);
		for (long i = 0; i < n; i++)
		{
			if (in[i].type == KAPI_WS_IN_QUIT)
			{
				kapi_ws_ctl (KAPI_WS_DISPLAY, 0, 0, 0);
				return 0;
			}
			if (in[i].type == KAPI_WS_IN_KEY && in[i].keys[0] == 'x') kapi_exit (3);	// "a crash"
		}
		while (kapi_ws_ctl (KAPI_WS_NEXT, (long) &s_Req, 0, 0) == 1)	// no windows here: every request fails
		{
			struct kapi_ws_reply R;
			__builtin_memset (&R, 0, sizeof R);
			R.id = s_Req.id; R.status = -1;
			kapi_ws_ctl (KAPI_WS_REPLY, (long) &R, 0, 0);
		}
	}
	return 0;
}

int main (void)
{
	static char args[256];
	kapi_get_args (args, sizeof args);
	if (has (args, "--serve")) return serve (args);
	if (has (args, "--switch")) return do_switch ();
	int expect = 0;
	if (has (args, "--expect"))
	{
		const char *p = args;
		while (*p && !(p[0] == '1' || p[0] == '2')) p++;
		expect = *p ? *p - '0' : 0;
	}
	return client (expect);
}
