// demolib.cpp -- a C++ test library for the shared-library loader (docs/SHARED-LIBS-PLAN.md):
// an export table, a class with virtuals called across the boundary, a constructor in
// .init_array, a pointer table in data, and the importer's allocator. MIT (as Onyx).
typedef unsigned long size_t;
struct TLibImports { unsigned size; void *(*alloc) (size_t); void (*free) (void *); };
static const TLibImports *s_imp;
void *operator new (size_t n)		{ return s_imp->alloc (n); }
void operator delete (void *p)		{ s_imp->free (p); }
void operator delete (void *p, size_t)	{ s_imp->free (p); }

struct Shape { virtual ~Shape () {} virtual int area () { return 0; } int w, h; };
struct Rect : Shape { int area () override { return w * h; } };

static int s_ctor_ran;				// set by a static constructor: .init_array works
static struct Init { Init () { s_ctor_ran = 42; } } s_init;
static const char *const s_names[] = { "zero", "one", "two" };	// pointers in data: RELATIVE

extern "C" {
typedef void (*ctor_fn) (void);
extern ctor_fn __lib_init_array_start[], __lib_init_array_end[];
static int lib_init (const TLibImports *imp)
{
	static int done;
	if (done) return 0;
	s_imp = imp;
	for (ctor_fn *f = __lib_init_array_start; f < __lib_init_array_end; f++) (*f) ();
	done = 1;
	return 0;
}
static int ctor_ran (void)			{ return s_ctor_ran; }
static const char *name (int i)			{ return s_names[i]; }
static Shape *make_rect (int w, int h)		{ Rect *r = new Rect; r->w = w; r->h = h; return r; }
static int area_of (Shape *s)			{ return s->area (); }	// may be the app's override

struct TDemoTable
{
	unsigned version, size;
	int (*init) (const TLibImports *);
	int (*ctor_ran) (void);
	const char *(*name) (int);
	Shape *(*make_rect) (int, int);
	int (*area_of) (Shape *);
};
__attribute__ ((visibility ("default")))
extern const TDemoTable onyx_lib_table;
const TDemoTable onyx_lib_table = { 1, sizeof (TDemoTable), lib_init, ctor_ran, name, make_rect, area_of };
}
