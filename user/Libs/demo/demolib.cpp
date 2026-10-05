// demolib.cpp -- a C++ test library for the shared-library loader (docs/SHARED-LIBS-PLAN.md): an
// export table, a class with virtuals called across the boundary, a constructor in .init_array, a
// pointer table in data, a variable in its data, and the importer's allocator (librt.cpp).
// -> SD:/lib/demo.so (and demo2.so, the same with DEMO_BUILD=2: "another build" of the file).
// MIT (as Onyx).
#include "demo.h"

#ifndef DEMO_BUILD
#define DEMO_BUILD	1
#endif

struct Rect : Shape { int area () override { return w * h; } };

static int s_ctor_ran;				// set by a static constructor: .init_array works
static struct Init { Init () { s_ctor_ran = 42; } } s_init;
static const char *const s_names[] = { "zero", "one", "two" };	// pointers in data: RELATIVE
static int s_counter = 7;			// .data: each process has its own

extern "C" {
int onyx_lib_init (const TLibImports *imp);	// (librt.cpp)

static int lib_init (const TLibImports *imp)	{ return onyx_lib_init (imp) < 0 ? -1 : 0; }
static int ctor_ran (void)			{ return s_ctor_ran; }
static const char *name (int i)			{ return s_names[i]; }
static Shape *make_rect (int w, int h)		{ Rect *r = new Rect; r->w = w; r->h = h; return r; }
static int area_of (Shape *s)			{ return s->area (); }	// may be the caller's override
static void destroy (Shape *s)			{ delete s; }
static int *counter (void)			{ return &s_counter; }
static void crash (void)			{ *(volatile int *) 8 = 1; }
static int build (void)				{ return DEMO_BUILD; }

__attribute__ ((visibility ("default")))
extern const TDemoTable onyx_lib_table;
const TDemoTable onyx_lib_table =
{
	DEMO_VERSION, sizeof (TDemoTable), lib_init,
	ctor_ran, name, make_rect, area_of,
	destroy, counter, crash, build,
};
}
