// demo.h -- the test library of the shared-library loader (SD:/lib/demo.so; /bin/libtest runs it;
// docs/SHARED-LIBS-PLAN.md section 6): its export table, and the class both sides know. MIT (as Onyx).
#ifndef ONYX_DEMO_H
#define ONYX_DEMO_H

#include "lib.h"

// A class with virtuals: the library makes Rect (its own vtable), a program derives its own (the
// program's vtable, called by the library).
struct Shape
{
	virtual ~Shape () {}
	virtual int area () { return 0; }
	int w, h;
};

#define DEMO_VERSION	2			// the table's version

struct TDemoTable
{
	unsigned version, size;
	int (*init) (const TLibImports *);
	// --- version 1 ---
	int (*ctor_ran) (void);			// 42: the library's static constructors ran
	const char *(*name) (int i);		// "zero", "one", "two": pointers relocated in its data
	Shape *(*make_rect) (int w, int h);	// new Rect, in the library
	int (*area_of) (Shape *s);		// s->area (): the library's or the caller's override
	// --- version 2 ---
	void (*destroy) (Shape *s);		// delete s, in the library
	int *(*counter) (void);			// a variable in the library's data: this process's own
	void (*crash) (void);			// a fault inside the library
	int (*build) (void);			// which build of the library this is (DEMO_BUILD)
};

#endif
