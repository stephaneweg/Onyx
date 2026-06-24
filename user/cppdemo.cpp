//
// cppdemo.cpp -- proof that C++/OO apps work on Onyx: a global object with a
// constructor (runs via crt0 .init_array), a small class hierarchy with a virtual
// method (vtables, no RTTI), and objects allocated with `new` / freed with `delete`
// (operator new/delete -> the umm user allocator -> kapi_sbrk). Built with g++
// -nostdlib -fno-exceptions -fno-rtti. No STL.
//
#include "kapi.h"
#include "onyxpp.hpp"

#define W	360
#define H	280
static unsigned *fb;

struct Shape
{
	int x, y, sz; unsigned col;
	Shape (int X, int Y, int S, unsigned C) : x (X), y (Y), sz (S), col (C) {}
	virtual ~Shape () {}
	virtual void draw () = 0;
	void px (int xx, int yy, unsigned c) { if (xx >= 0 && xx < W && yy >= 0 && yy < H) fb[yy * W + xx] = c; }
};

struct Square : Shape
{
	Square (int X, int Y, int S, unsigned C) : Shape (X, Y, S, C) {}
	void draw () override { for (int j = 0; j < sz; j++) for (int i = 0; i < sz; i++) px (x + i, y + j, col); }
};

struct Disc : Shape
{
	Disc (int X, int Y, int S, unsigned C) : Shape (X, Y, S, C) {}
	void draw () override
	{ int r = sz / 2; for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i*i + j*j <= r*r) px (x + i, y + j, col); }
};

// A global object with a constructor -> exercises crt0's .init_array walk.
struct Banner { const char *text; Banner () : text ("C++ demo: new + virtual + .init_array") {} } g_banner;

#define N	6
static Shape *g_shapes[N];

int main (void)
{
	fb = kapi_create_window (W, H, "cppdemo");
	if (fb == 0) return 1;

	for (int i = 0; i < N; i++)			// heap-allocate a mix of subclasses
	{
		int x = 16 + i * 56, y = 70 + (i % 2) * 90, s = 44;
		unsigned c = 0x00F08040u + (unsigned) i * 0x00102008u;
		g_shapes[i] = (i & 1) ? (Shape *) new Disc (x, y, s, c)
				      : (Shape *) new Square (x, y, s, c);
	}

	while (!should_exit ())
	{
		pump_events ();
		for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) fb[y * W + x] = 0x00202830;
		kapi_draw_text (8, 8, g_banner.text, 0x00FFFFFF);	// proves the ctor ran
		for (int i = 0; i < N; i++) g_shapes[i]->draw ();	// virtual dispatch
		msleep (16);
	}

	for (int i = 0; i < N; i++) delete g_shapes[i];		// operator delete -> umm_free
	return 0;
}
