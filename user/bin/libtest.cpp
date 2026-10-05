//
// libtest -- the tests of the kernel's shared libraries (kapi v83 lib_open; docs/SHARED-LIBS-PLAN.md
// section 6, "Pi, step 1a") against the test library SD:/lib/demo.so (user/demo) and its second
// build SD:/lib/demo2.so. Prints a `PASS name` / `FAIL name: reason` line per test and exits 0 only
// if all passed.
//   usage: libtest [starts]         every test; `starts`: the starts of the leak test (default 200)
//          libtest --child <what> <library>   (its own children)
// The parent maps no library itself: the children do, so that it can watch a library come and go.
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

#define SELF		"SD:/bin/libtest"
#define LIB		"SD:/lib/demo.so"
#define LIB2		"SD:/lib/demo2.so"
#define SCRATCH		"SD:/lib/libtest-scratch.so"	// (a copy the test replaces while it is mapped)

static int slen (const char *s)			{ int n = 0; while (s[n]) n++; return n; }
static bool seq (const char *a, const char *b)	{ while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void put (const char *s)			{ kapi_stdout_write (s, (unsigned) slen (s)); }
static void putn (long v)
{
	char b[24]; int i = 0;
	bool neg = v < 0;
	unsigned long u = neg ? (unsigned long) -v : (unsigned long) v;
	do { b[i++] = (char) ('0' + u % 10); u /= 10; } while (u != 0);
	if (neg) put ("-");
	while (i > 0) { char c[2] = { b[--i], 0 }; put (c); }
}

static int s_failed;
static void pass (const char *name)		{ put ("PASS "); put (name); put ("\n"); }
static void fail (const char *name, const char *why, long v)
{
	s_failed++;
	put ("FAIL "); put (name); put (": "); put (why); put (" ("); putn (v); put (")\n");
}

// ---- a child: one use of the library, the result in its exit status (0: good) ---------------------

struct Tri : Shape { int area () override { return w * h / 2; } };	// the program's own class

static const TDemoTable *open_demo (const char *lib, unsigned min, int *err)
{
	return (const TDemoTable *) kapi_lib_open (lib, min, err);
}

// The checks of one process: the number of the first one that fails.
static int child_basic (const char *lib)
{
	int err = 0;
	const TDemoTable *t = open_demo (lib, 1, &err);
	if (t == 0) return 1;
	if (t->version != DEMO_VERSION || t->size != sizeof (TDemoTable)) return 2;
	if ((unsigned long) t < 0x400000000UL || (unsigned long) t >= 0x800000000UL) return 3;	// in the arena
	if (open_demo (lib, 1, &err) != t) return 4;			// opened again: the same table
	if (t->init (lib_cxx_imports ()) != 0) return 5;
	if (t->init (lib_cxx_imports ()) != 0) return 6;		// twice: nothing done, 0
	if (t->ctor_ran () != 42) return 7;				// its .init_array ran
	if (!seq (t->name (2), "two") || !seq (t->name (0), "zero")) return 8;	// relocated pointers
	Shape *r = t->make_rect (3, 4);					// new in the library
	if (r == 0 || t->area_of (r) != 12 || r->area () != 12) return 9;	// the library's vtable, both ways
	delete r;							// ... delete in the program
	Tri *tri = new Tri; tri->w = 6; tri->h = 4;			// new in the program
	if (t->area_of (tri) != 12) return 10;				// the program's vtable, called by the library
	tri->h = 5;
	if (t->area_of (tri) != 15) return 11;
	t->destroy (tri);						// ... delete in the library
	for (int i = 0; i < 2000; i++)					// one heap: no damage either way
	{
		Shape *a = t->make_rect (i, 2); Tri *b = new Tri; b->w = b->h = i;
		if (t->area_of (a) != i * 2) return 12;
		t->destroy (b); delete a;
	}
	int *c = t->counter ();						// its data: ours, writable
	if (*c != 7) return 13;
	*c = 1234;
	if (*t->counter () != 1234) return 14;
	return 0;
}

static int child_main (const char *what, const char *lib)
{
	if (seq (what, "basic")) return child_basic (lib);
	int err = 0;
	const TDemoTable *t = open_demo (lib, 1, &err);
	if (t == 0 || t->init (lib_cxx_imports ()) != 0) return 90;
	if (seq (what, "quick")) return t->ctor_ran () == 42 && *t->counter () == 7 ? 0 : 91;
	if (seq (what, "hold"))					// keeps the library a while; says which build it has
	{
		int b0 = t->build ();
		for (int i = 0; i < 30; i++) { kapi_msleep (100); if (t->build () != b0 || t->ctor_ran () != 42) return 92; }
		return 40 + b0;					// 41: the first build, 42: the second
	}
	if (seq (what, "build")) return 40 + t->build ();
	if (seq (what, "crash")) { t->crash (); return 93; }	// (not reached: the process dies in the library)
	return 94;
}

// ---- the parent ---------------------------------------------------------------------------------------

static void *start (const char *what, const char *lib)
{
	char args[160]; int k = 0;
	const char *part[3] = { "--child", what, lib };
	for (int p = 0; p < 3; p++)
	{
		if (p) args[k++] = ' ';
		for (int i = 0; part[p][i] && k < (int) sizeof args - 2; i++) args[k++] = part[p][i];
	}
	args[k] = '\0';
	return kapi_spawn (SELF, args, 0, 0);
}
static int run (const char *what, const char *lib)
{
	void *p = start (what, lib);
	return p != 0 ? kapi_wait (p) : -1000;
}

#define MAX_IMAGES	96
static struct kapi_image_info s_img[MAX_IMAGES];

// The images whose path ends with `tail`: how many, the first one's entry in *out.
static int images_of (const char *tail, struct kapi_image_info *out, unsigned skip_flags)
{
	int n = kapi_image_list (0, s_img, MAX_IMAGES), found = 0;
	int tl = slen (tail);
	for (int i = 0; i < n && i < MAX_IMAGES; i++)
	{
		int pl = slen (s_img[i].path);
		if (pl < tl || !seq (s_img[i].path + pl - tl, tail) || (s_img[i].flags & skip_flags)) continue;
		if (found++ == 0 && out != 0) *out = s_img[i];
	}
	return found;
}

static bool copy_file (const char *from, const char *to)
{
	void *h = kapi_open (from);
	if (h == 0) return false;
	unsigned n = kapi_fsize (h);
	char *buf = new char[n + 1];
	bool ok = kapi_read (h, buf, n) == (int) n;
	kapi_close (h);
	ok = ok && kapi_save_file (to, buf, n) == (int) n;
	delete [] buf;
	return ok;
}

static unsigned long free_kb (void)
{
	unsigned long f = 0;
	kapi_meminfo (0, &f, 0, 0);
	return f;
}

int main (void)
{
	static char args[256];
	kapi_get_args (args, sizeof args);
	if (args[0] == '-' && args[1] == '-' && args[2] == 'c')		// --child <what> <library>
	{
		char *what = args + 7; while (*what == ' ') what++;
		char *lib = what; while (*lib && *lib != ' ') lib++;
		if (*lib) *lib++ = '\0';
		kapi_exit (child_main (what, lib));
	}
	int starts = 0;
	for (const char *p = args; *p >= '0' && *p <= '9'; p++) starts = starts * 10 + (*p - '0');
	if (starts == 0) starts = 200;
	if (kapi_abi_version () < 83) { put ("FAIL kernel: no shared libraries (kapi v83)\n"); return 1; }

	struct kapi_image_info info;
	int r;

	// 1..4: one process's use of the library (the table, init, constructors, relocated data, the
	// vtables both ways, one heap, its own data)
	r = run ("basic", LIB);
	if (r == 0) pass ("basic (table, init twice, constructors, relocations, vtables both ways, one heap, own data)");
	else fail ("basic", "check failed, number", r);
	r = run ("basic", "demo");				// a bare name: SD:/lib/demo.so
	if (r == 0) pass ("bare name"); else fail ("bare name", "check failed, number", r);

	// 5: the errors
	{
		int err = 0;
		const void *t = kapi_lib_open (LIB, 99, &err);
		if (t == 0 && err == -KAPI_ENOTSUP) pass ("too old: -ENOTSUP"); else fail ("too old", "err", err);
		t = kapi_lib_open ("nosuch", 1, &err);
		if (t == 0 && err == -KAPI_ENOENT) pass ("no such library: -ENOENT"); else fail ("no such library", "err", err);
		t = kapi_lib_open (SELF, 1, &err);		// a program is not a library
		if (t == 0 && err == -KAPI_EINVAL) pass ("a program refused: -EINVAL"); else fail ("a program refused", "err", err);
		void *p = kapi_spawn (LIB, "", 0, 0);		// a library is not a program: its start fails
		if (p != 0) kapi_wait (p);			// (the kernel log says why)
		if (images_of ("/lib/demo.so", 0, 0) == 0) pass ("a library is not run as a program; nothing stays loaded");
		else fail ("nothing stays loaded", "images", 1);
	}

	// 6: two processes at once: one image, referenced twice
	{
		void *a = start ("hold", LIB), *b = start ("hold", LIB);
		kapi_msleep (1500);
		int n = images_of ("/lib/demo.so", &info, 0);
		if (n == 1 && info.refs == 2 && (info.flags & KAPI_IMG_LIB)) pass ("two processes share one image");
		else fail ("two processes share one image", n == 1 ? "refs" : "images", n == 1 ? (long) info.refs : n);
		int ra = kapi_wait (a), rb = kapi_wait (b);
		if (ra == 41 && rb == 41) pass ("both ran it to their end"); else fail ("both ran", "status", ra * 1000 + rb);
		if (images_of ("/lib/demo.so", 0, 0) == 0) pass ("freed with its last process"); else fail ("freed with its last process", "images", 1);
	}

	// 7: a fault inside the library kills only that process
	{
		void *a = start ("hold", LIB);
		kapi_msleep (300);
		r = run ("crash", LIB);
		int ra = kapi_wait (a);
		if (r != 93 && r != 0 && ra == 41) pass ("a fault in the library: only that process dies");
		else fail ("a fault in the library", "status", r * 1000 + ra);
	}

	// 8: the file replaced while a process runs it: that process keeps the old one, a new process
	// gets the new one, the old one goes with its last process
	if (!copy_file (LIB, SCRATCH)) fail ("replace", "cannot copy the library", 0);
	else
	{
		void *a = start ("hold", SCRATCH);
		kapi_msleep (800);
		bool ok = copy_file (LIB2, SCRATCH);		// (the write unnames the image)
		int nOld = images_of ("/lib/libtest-scratch.so", &info, 0);
		bool bGone = nOld == 1 && (info.flags & KAPI_IMG_UNNAMED);
		r = run ("build", SCRATCH);
		int ra = kapi_wait (a);
		if (ok && bGone && r == 42 && ra == 41) pass ("replaced while in use: old process keeps the old build, a new one gets the new");
		else fail ("replaced while in use", !ok ? "copy" : !bGone ? "old image not unnamed" : "status", r * 1000 + ra);
		if (images_of ("/lib/libtest-scratch.so", 0, 0) == 0) pass ("the old build freed with its process");
		else fail ("the old build freed with its process", "images", 1);
		kapi_remove (SCRATCH);
	}

	// 9: preloaded: kept with no process, shared by a start
	{
		r = kapi_image_preload (LIB);
		for (int i = 0; i < 50 && images_of ("/lib/demo.so", &info, KAPI_IMG_LOADING) == 0; i++) kapi_msleep (100);
		int n = images_of ("/lib/demo.so", &info, 0);
		if (r == 0 && n == 1 && (info.flags & KAPI_IMG_KEPT) && (info.flags & KAPI_IMG_LIB) && info.refs == 0)
			pass ("preload keeps a library");
		else fail ("preload keeps a library", "r / images", r * 1000 + n);
		r = run ("quick", LIB);
		n = images_of ("/lib/demo.so", &info, 0);
		if (r == 0 && n == 1 && (info.flags & KAPI_IMG_KEPT)) pass ("a start shares the preloaded library");
		else fail ("a start shares the preloaded library", "status", r);
	}

	// 10: starts and ends in a loop: nothing leaks (the library stays preloaded: its frames do not
	// move; then unloaded: all of it comes back)
	{
		run ("quick", LIB);
		unsigned long f0 = free_kb ();
		int bad = 0;
		for (int i = 0; i < starts; i++) if (run ("quick", LIB) != 0) bad++;
		kapi_msleep (500);
		unsigned long f1 = free_kb ();
		long d = (long) f0 - (long) f1;
		if (bad == 0 && d < 256 && d > -256) pass ("starts in a loop: no leak");
		else fail ("starts in a loop", bad ? "failed starts" : "free memory changed, KB", bad ? bad : d);
		r = kapi_image_unload (LIB);
		if (r == 0 && images_of ("/lib/demo.so", 0, 0) == 0) pass ("unload frees it");
		else fail ("unload frees it", "r", r);
		// loaded and freed again and again: the arena's range comes back (the same place each time)
		bad = 0;
		for (int i = 0; i < 20; i++) if (run ("quick", LIB) != 0) bad++;
		if (bad == 0 && images_of ("/lib/demo.so", 0, 0) == 0) pass ("load / free in a loop");
		else fail ("load / free in a loop", "failed starts", bad);
	}

	put (s_failed == 0 ? "libtest: all passed\n" : "libtest: FAILED\n");
	return s_failed == 0 ? 0 : 1;
}
