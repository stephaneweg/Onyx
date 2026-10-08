//
// eyes -- a desktop gadget: two googly eyes whose pupils follow the mouse pointer
// (uk_win_cursor_pos gives the cursor relative to this window). Drag the title bar to
// move it like any window. Drawn (and presented) when a pupil moves: the screen, and the
// remote desktop (rdpd sends a window again when it presents), follow.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"

#define W	180
#define H	110

static unsigned *fb;

static unsigned isqrt (unsigned n)
{
	if (n == 0) return 0;
	unsigned x = n, y = (x + 1) / 2;
	while (y < x) { x = y; y = (x + n / x) / 2; }
	return x;
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void disc (int cx, int cy, int r, unsigned c)
{
	for (int y = -r; y <= r; y++)
		for (int x = -r; x <= r; x++)
			if (x * x + y * y <= r * r) fill_rect (cx + x, cy + y, 1, 1, c);
}

// Where an eye's pupil goes for the pointer at (mx, my).
static void pupil (int ex, int ey, int R, int pr, int mx, int my, int *px, int *py)
{
	int dx = mx - ex, dy = my - ey;
	int maxoff = R - pr - 2;
	unsigned d = isqrt ((unsigned) (dx * dx + dy * dy));
	if ((int) d > maxoff && d > 0)
	{
		*px = ex + dx * maxoff / (int) d;
		*py = ey + dy * maxoff / (int) d;
	}
	else { *px = ex + dx; *py = ey + dy; }
}

static void draw_eye (int ex, int ey, int R, int pr, int px, int py)
{
	disc (ex, ey, R, 0x00ffffff);			// white
	disc (px, py, pr, 0x00101018);			// pupil
}

int main (void)
{
	fb = uk_win_create (W, H, "eyes");
	if (fb == 0) return 1;
	uikit::uk_decorate_window ();			// (reads the theme: the palette)

	int last[4] = { -1, -1, -1, -1 };
	while (!should_exit ())
	{
		pump_events ();
		int mx, my, p[4];
		uk_win_cursor_pos (&mx, &my);
		pupil (50, 55, 34, 12, mx, my, &p[0], &p[1]);
		pupil (130, 55, 34, 12, mx, my, &p[2], &p[3]);
		if (p[0] != last[0] || p[1] != last[1] || p[2] != last[2] || p[3] != last[3])
		{
			unsigned bg = uikit::C_BG;
			for (int i = 0; i < W * H; i++) fb[i] = bg;		// the theme's face
			draw_eye (50, 55, 34, 12, p[0], p[1]);
			draw_eye (130, 55, 34, 12, p[2], p[3]);
			for (int i = 0; i < 4; i++) last[i] = p[i];
			uk_win_present ();
		}
		msleep (16);
	}
	return 0;
}
