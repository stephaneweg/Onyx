//
// demoC.c -- fire effect, ported from temp/fire.bas (x86 FreeBASIC + asm) to C.
//
// Algorithm (the classic "fire"):
//   - an 80-wide intensity field; the bottom row is re-seeded each frame with
//     pseudo-random bytes (the LCG seed*0x8405+1, high bits = the random byte);
//   - propagation: each cell becomes the average of four cells just below it,
//     minus 1 (cooling), written one row up -> flames rise and fade;
//   - render: the field is scaled x4 horizontally / x2 vertically through a
//     palette. `mode` chooses the dominant channel (red / green / blue fire),
//     which is what the original's "Color" button toggled via fcolor.
//
// No heap in userland (-nostdlib): the field + palette are static arrays; the
// pixel buffer is the window canvas the kernel maps in.
//
#include "kapi.h"

#define W	320			// window client size
#define H	240
#define FW	80			// fire field width
#define FH	100			// fire field height (rendered x2 -> 200 px)
#define FIRE_PX	200			// rendered fire height in pixels (FH * 2)

static unsigned	     *fb;
static unsigned char  field[FW * (FH + 2)];	// +2 rows of margin for the neighbour reads
static unsigned	      palette[256];
static unsigned	      seed = 0x1234u;
static int	      mode = 0;			// 0 = red, 1 = green, 2 = blue

static void build_palette (int m)
{
	for (int i = 0; i < 256; i++)
	{
		// Three overlapping ramps: black -> primary -> +secondary -> white.
		int hot  = i * 3;             if (hot  > 255) hot  = 255;
		int mid  = (i < 85)  ? 0 : (i - 85)  * 3; if (mid  > 255) mid  = 255;
		int cold = (i < 170) ? 0 : (i - 170) * 3; if (cold > 255) cold = 255;

		int r, g, b;
		if (m == 0)      { r = hot;  g = mid;  b = cold; }	// red fire
		else if (m == 1) { g = hot;  r = mid;  b = cold; }	// green fire
		else             { b = hot;  r = mid;  g = cold; }	// blue fire

		palette[i] = ((unsigned) r << 16) | ((unsigned) g << 8) | (unsigned) b;
	}
}

static void seed_bottom_row (void)
{
	unsigned base = (FH - 1) * FW;
	for (int c = 0; c < FW; c++)
	{
		unsigned long long prod = (unsigned long long) seed * 0x8405ULL;
		seed = (unsigned) prod + 1;			// LCG, as in the asm
		field[base + c] = (unsigned char) ((prod >> 32) & 0xFF);
	}
}

static void propagate (void)
{
	for (int r = 1; r < FH; r++)
	{
		unsigned row = (unsigned) r * FW;
		for (int c = 0; c < FW; c++)
		{
			unsigned idx = row + c;
			int sum = field[idx] + field[idx + 1]
				+ field[idx + 2] + field[idx + FW + 1];
			int v = sum >> 2;			// average of four
			if (v > 0) v--;				// cool by one
			field[idx - FW] = (unsigned char) v;	// write one row up
		}
	}
}

// "Color" button handler: cycle the fire palette (red -> green -> blue), exactly
// like the original btnClick (fcolor = (fcolor+1) mod 3). Runs in this app's
// context when we pump events.
static void on_color (unsigned long sender, int event, long value)
{
	(void) sender; (void) event; (void) value;
	mode = (mode + 1) % 3;
	build_palette (mode);
}

static void render (void)
{
	for (int dy = 0; dy < FIRE_PX; dy++)
	{
		const unsigned char *srow = &field[(dy >> 1) * FW];
		unsigned *drow = &fb[dy * W];
		for (int dx = 0; dx < W; dx++)
		{
			drow[dx] = palette[srow[dx >> 2]];	// x4 wide, x2 tall
		}
	}

	// Strip below the fire (where the original placed its "Color" button).
	for (int y = FIRE_PX; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			fb[y * W + x] = 0x00202028;
		}
	}
}

int main (void)
{
	fb = create_window (W, H, "Fire demo");
	if (fb == 0)
	{
		return 1;
	}

	build_palette (mode);

	// "Color" button in the strip below the fire (like the original at y=205).
	add_button (10, FIRE_PX + 5, 80, 26, "Color", on_color);

	// Animation loop: render, dispatch GUI events (the Color click), and check
	// whether the window's close box asked us to quit -> clean exit.
	while (!should_exit ())
	{
		seed_bottom_row ();
		propagate ();
		render ();
		present ();
		pump_events ();				// dispatch on_color() if clicked
		msleep (20);				// ~50 fps cap
	}

	return 0;					// task terminates: frees window
}
