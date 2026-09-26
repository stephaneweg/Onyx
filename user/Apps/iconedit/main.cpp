//
// iconedit/main.cpp -- the icon editor: draw Onyx icons (24-bit BMP, magenta 0xFF00FF =
// transparent -- the desktop's icon convention; app icons are 40x40,
// SD:/apps/<name>.app/icon.bmp). The enlarged pixel grid in the middle, the tools on the
// left, the palette and live previews (1x, 2x, on light and dark) on the right.
// Left button = the primary colour, right button = the secondary one.
// Tools: Pen (P), Line (L), Rectangle (R), Filled box (B), Ellipse (O), Fill (F), Picker
// (K: click a pixel to take its colour), Eraser (E, paints transparency). ^Z undo, ^Y
// redo, G grid. Image menu: flip, rotate, shift, clear. New sizes 16 / 24 / 32 / 40 / 48 /
// 64. Opens and saves 24-bit BMP; drop a BMP on the window to open it.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "bmp.hpp"
#include "docguard.h"

using namespace wtk;

#define W	700
#define H	520
#define TOOL_W	78
#define SIDE_W	170
#define MAXS	64
#define TRANSP	0x00FF00FFu
#define NUNDO	24

enum { T_PEN, T_LINE, T_RECT, T_BOX, T_ELLIPSE, T_FILL, T_PICK, T_ERASE, NTOOL };
static const char *const TOOL_NAME[NTOOL] = { "Pen", "Line", "Rect", "Box", "Ellipse", "Fill", "Picker", "Eraser" };
static const char TOOL_KEY[NTOOL] = { 'p', 'l', 'r', 'b', 'o', 'f', 'k', 'e' };

static unsigned g_img[MAXS * MAXS];
static int      g_w = 40, g_h = 40;
static unsigned g_undo[NUNDO][MAXS * MAXS]; static int g_uw[NUNDO], g_uh[NUNDO];
static int      g_nundo, g_nredo;
static unsigned g_redo[NUNDO][MAXS * MAXS]; static int g_rw[NUNDO], g_rh[NUNDO];
static int      g_tool = T_PEN, g_prevTool = T_PEN;
static unsigned g_col[2] = { 0x00000000, TRANSP };	// primary, secondary
static bool     g_gridOn = true;
static char     g_path[128] = "SD:/icon.bmp";
static unsigned g_saved;
static int      g_hx = -1, g_hy = -1;		// hovered pixel

static unsigned hash_img () { return doc_hash ((const char *) g_img, (unsigned) (g_w * g_h * 4)) ^ (unsigned) (g_w * 131 + g_h); }
static bool changed () { return hash_img () != g_saved; }

static const unsigned PALETTE[] = {
	0x00000000, 0x00404040, 0x00808080, 0x00C0C0C0, 0x00FFFFFF, 0x00800000, 0x00FF0000, 0x00FF8080,
	0x00804000, 0x00FF8000, 0x00FFC080, 0x00808000, 0x00FFFF00, 0x00FFFF80, 0x00008000, 0x0000FF00,
	0x0080FF80, 0x00008080, 0x0000FFFF, 0x0080FFFF, 0x00000080, 0x000000FF, 0x008080FF, 0x00400080,
	0x008000FF, 0x00C080FF, 0x00800040, 0x00FF0080, 0x00FF80C0, 0x00566074, 0x00303D4D, 0x005A6E88 };
#define NPAL ((int) (sizeof PALETTE / sizeof PALETTE[0]))

static void push_undo ()
{
	if (g_nundo == NUNDO) { for (int i = 1; i < NUNDO; i++) { kapi_memcpy (g_undo[i - 1], g_undo[i], sizeof g_undo[0]); g_uw[i - 1] = g_uw[i]; g_uh[i - 1] = g_uh[i]; } g_nundo--; }
	kapi_memcpy (g_undo[g_nundo], g_img, sizeof g_img); g_uw[g_nundo] = g_w; g_uh[g_nundo] = g_h; g_nundo++;
	g_nredo = 0;
}

class Grid;
class Preview;
static Grid    *g_grid;
static Preview *g_prev;
static Label   *g_status;
static void refresh ();

// ---- drawing primitives on the image -------------------------------------------------
static void put (int x, int y, unsigned c) { if (x >= 0 && x < g_w && y >= 0 && y < g_h) g_img[y * MAXS + x] = c; }
static void img_line (int x0, int y0, int x1, int y1, unsigned c, unsigned *dst = g_img)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
	for (;;)
	{
		if (x0 >= 0 && x0 < g_w && y0 >= 0 && y0 < g_h) dst[y0 * MAXS + x0] = c;
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x0 += sx; }
		if (e2 < dx) { err += dx; y0 += sy; }
	}
}
static void img_rect (int x0, int y0, int x1, int y1, unsigned c, bool fill, unsigned *dst = g_img)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
		if (fill || y == y0 || y == y1 || x == x0 || x == x1)
			if (x >= 0 && x < g_w && y >= 0 && y < g_h) dst[y * MAXS + x] = c;
}
// Ellipse inscribed in the box (midpoint test per pixel: the outline = inside pixels
// with an outside 4-neighbour).
static void img_ellipse (int x0, int y0, int x1, int y1, unsigned c, unsigned *dst = g_img)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	long cx2 = x0 + x1, cy2 = y0 + y1, rx2 = x1 - x0 + 1, ry2 = y1 - y0 + 1;	// doubled centre / diameters
	auto inside = [&] (int x, int y) -> bool
	{
		long dx = 2 * x + 1 - cx2 - 1, dy = 2 * y + 1 - cy2 - 1;		// doubled offsets of the pixel centre
		return dx * dx * ry2 * ry2 + dy * dy * rx2 * rx2 <= rx2 * rx2 * ry2 * ry2;
	};
	for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
		if (inside (x, y) && (!inside (x - 1, y) || !inside (x + 1, y) || !inside (x, y - 1) || !inside (x, y + 1)))
			if (x >= 0 && x < g_w && y >= 0 && y < g_h) dst[y * MAXS + x] = c;
}
static void img_fill (int x, int y, unsigned c)
{
	unsigned old = g_img[y * MAXS + x];
	if (old == c) return;
	static short stk[MAXS * MAXS * 4][2];
	int sp = 0;
	stk[sp][0] = (short) x; stk[sp][1] = (short) y; sp++;
	while (sp)
	{
		sp--;
		int px = stk[sp][0], py = stk[sp][1];
		if (px < 0 || px >= g_w || py < 0 || py >= g_h || g_img[py * MAXS + px] != old) continue;
		g_img[py * MAXS + px] = c;
		if (sp + 4 <= MAXS * MAXS * 4)
		{
			stk[sp][0] = (short) (px + 1); stk[sp][1] = (short) py; sp++;
			stk[sp][0] = (short) (px - 1); stk[sp][1] = (short) py; sp++;
			stk[sp][0] = (short) px; stk[sp][1] = (short) (py + 1); sp++;
			stk[sp][0] = (short) px; stk[sp][1] = (short) (py - 1); sp++;
		}
	}
}

static unsigned checker (int x, int y, int cell) { return (((x / cell) + (y / cell)) & 1) ? 0x00C8CCD2 : 0x00E8EBEF; }

// ---- the pixel grid ------------------------------------------------------------------------
class Grid : public Widget
{
public:
	int cell, ox, oy;
	bool down; int btn; int sx, sy, lx, ly;	// drag state (image coords)
	unsigned over[MAXS * MAXS];		// preview of a shape being dragged
	bool showOver;
	Grid (int l, int t, int w, int h) : Widget (l, t, w, h), down (false), showOver (false) { canFocus = true; layoutCells (); }
	void layoutCells ()
	{
		int s = g_w > g_h ? g_w : g_h;
		cell = (width < height ? width : height) / s;
		if (cell < 2) cell = 2;
		ox = (width - cell * g_w) / 2; oy = (height - cell * g_h) / 2;
	}
	void onDraw () override
	{
		layoutCells ();
		Canvas &c = canvas;
		c.clear (0x00283038);
		const unsigned *src = showOver ? over : g_img;
		for (int y = 0; y < g_h; y++) for (int x = 0; x < g_w; x++)
		{
			unsigned v = src[y * MAXS + x];
			int px = ox + x * cell, py = oy + y * cell;
			if (v == TRANSP)
			{
				int h = cell / 2 > 0 ? cell / 2 : 1;
				c.fillRect (px, py, cell, cell, 0x00E8EBEF);
				c.fillRect (px, py, h, h, 0x00C8CCD2); c.fillRect (px + h, py + h, cell - h, cell - h, 0x00C8CCD2);
			}
			else c.fillRect (px, py, cell, cell, v);
		}
		if (g_gridOn && cell >= 5)
		{
			for (int x = 0; x <= g_w; x++) c.fillRect (ox + x * cell, oy, 1, g_h * cell, (x % 8) ? 0x00A0A8B0 : 0x00707880);
			for (int y = 0; y <= g_h; y++) c.fillRect (ox, oy + y * cell, g_w * cell, 1, (y % 8) ? 0x00A0A8B0 : 0x00707880);
		}
		if (g_hx >= 0) c.frameRect (ox + g_hx * cell - 1, oy + g_hy * cell - 1, cell + 2, cell + 2, 0x00FFE040);
		c.frameRect (ox - 1, oy - 1, g_w * cell + 2, g_h * cell + 2, 0x00101418);
	}
	bool toImg (int mx, int my, int &x, int &y)
	{
		x = (mx - ox) / cell; y = (my - oy) / cell;
		if (mx < ox) x = -1;
		if (my < oy) y = -1;
		return x >= 0 && x < g_w && y >= 0 && y < g_h;
	}
	void shapePreview (int x, int y)
	{
		kapi_memcpy (over, g_img, sizeof over);
		unsigned c = g_col[btn];
		if (g_tool == T_LINE) img_line (sx, sy, x, y, c, over);
		else if (g_tool == T_RECT) img_rect (sx, sy, x, y, c, false, over);
		else if (g_tool == T_BOX) img_rect (sx, sy, x, y, c, true, over);
		else if (g_tool == T_ELLIPSE) img_ellipse (sx, sy, x, y, c, over);
		showOver = true;
	}
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		int x, y;
		bool in = toImg (mx, my, x, y);
		int hx = in ? x : -1, hy = in ? y : -1;
		if (hx != g_hx || hy != g_hy) { g_hx = hx; g_hy = hy; refresh (); }
		if (!down && (bl || br) && in)
		{
			setFocus ();
			down = true; btn = bl ? 0 : 1; sx = lx = x; sy = ly = y;
			unsigned c = g_col[btn];
			switch (g_tool)
			{
			case T_PICK:
				g_col[btn] = g_img[y * MAXS + x];
				g_tool = g_prevTool; down = false; refresh (); return true;
			case T_FILL: push_undo (); img_fill (x, y, c); down = false; refresh (); return true;
			case T_PEN: push_undo (); put (x, y, c); break;
			case T_ERASE: push_undo (); put (x, y, TRANSP); break;
			default: push_undo (); shapePreview (x, y); break;
			}
			refresh ();
			return true;
		}
		if (down && (bl || br))
		{
			if (x < 0) x = 0;
			if (x >= g_w) x = g_w - 1;
			if (y < 0) y = 0;
			if (y >= g_h) y = g_h - 1;
			if (x == lx && y == ly) return true;
			if (g_tool == T_PEN) img_line (lx, ly, x, y, g_col[btn]);
			else if (g_tool == T_ERASE) img_line (lx, ly, x, y, TRANSP);
			else shapePreview (x, y);
			lx = x; ly = y;
			refresh ();
			return true;
		}
		if (down && !bl && !br)
		{
			down = false;
			if (showOver) { kapi_memcpy (g_img, over, sizeof over); showOver = false; }
			refresh ();
		}
		return in;
	}
};

// ---- previews (1x and 2x on light and dark backgrounds) ------------------------------------
class Preview : public Widget
{
public:
	Preview (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void blit (int x0, int y0, int scale, unsigned bg, bool chk)
	{
		for (int y = 0; y < g_h * scale; y++) for (int x = 0; x < g_w * scale; x++)
		{
			unsigned v = g_img[(y / scale) * MAXS + x / scale];
			canvas.pixel (x0 + x, y0 + y, v == TRANSP ? (chk ? checker (x, y, 4) : bg) : v);
		}
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		int s2 = g_w <= 40 ? 2 : 1;
		blit (4, 4, 1, 0x00F0F0F0, false);
		blit (4 + g_w + 8, 4, 1, 0x00202830, false);
		blit (4, 4 + g_h + 8, s2, 0x00303D4D, false);
	}
};

// ---- colour swatches -------------------------------------------------------------------------
class Palette : public Widget
{
public:
	enum { SW = 18, COLS = 8 };
	Palette (int l, int t) : Widget (l, t, COLS * SW + 2, (NPAL / COLS + 1) * SW + 2 + 40) {}
	void onDraw () override
	{
		Canvas &c = canvas;
		c.clear (C_BG);
		for (int i = 0; i <= NPAL; i++)
		{
			int x = 1 + (i % COLS) * SW, y = 1 + (i / COLS) * SW;
			if (i == NPAL)			// transparency
			{ c.fillRect (x, y, SW - 2, SW - 2, 0x00E8EBEF); c.fillRect (x, y, 8, 8, 0x00C8CCD2); c.fillRect (x + 8, y + 8, SW - 10, SW - 10, 0x00C8CCD2); }
			else c.fillRect (x, y, SW - 2, SW - 2, PALETTE[i]);
			c.frameRect (x - 1, y - 1, SW, SW, 0x00101418);
		}
		// primary over secondary
		int by = (NPAL / COLS + 1) * SW + 8;
		for (int k = 1; k >= 0; k--)
		{
			int x = 8 + k * 14, y = by + k * 10;
			if (g_col[k] == TRANSP) { c.fillRect (x, y, 24, 20, 0x00E8EBEF); c.fillRect (x, y, 12, 10, 0x00C8CCD2); c.fillRect (x + 12, y + 10, 12, 10, 0x00C8CCD2); }
			else c.fillRect (x, y, 24, 20, g_col[k]);
			c.frameRect (x, y, 24, 20, k ? 0x00808890 : 0x00FFFFFF);
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		if (!(bl || br) || pressed) { if (!bl && !br) pressed = false; return mx >= 0 && my >= 0 && mx < width && my < height; }
		int i = (my - 1) / SW * COLS + (mx - 1) / SW;
		if (mx < 1 || mx >= COLS * SW + 1 || i < 0 || i > NPAL) return false;
		pressed = true;
		g_col[bl ? 0 : 1] = i == NPAL ? TRANSP : PALETTE[i];
		refresh ();
		return true;
	}
};
static Palette *g_pal;
static Button  *g_toolBtn[NTOOL];

static void refresh ()
{
	g_grid->invalidate (true); g_prev->invalidate (true); g_pal->invalidate (true);
	for (int i = 0; i < NTOOL; i++) { bool on = g_tool == i; if (g_toolBtn[i]->pressed != on) { g_toolBtn[i]->pressed = on; g_toolBtn[i]->invalidate (true); } }
	char t[128]; int n = 0;
	auto cat = [&] (const char *s) { while (*s && n < 126) t[n++] = *s++; t[n] = 0; };
	auto num = [&] (long v) { char b[16]; int k = 0; if (v == 0) b[k++] = '0'; while (v) { b[k++] = (char) ('0' + v % 10); v /= 10; } while (k) { t[n++] = b[--k]; } t[n] = 0; };
	num (g_w); cat (" x "); num (g_h); cat ("   ");
	cat (TOOL_NAME[g_tool]);
	if (g_hx >= 0)
	{
		cat ("   ("); num (g_hx); cat (", "); num (g_hy); cat (")  ");
		unsigned v = g_img[g_hy * MAXS + g_hx];
		if (v == TRANSP) cat ("transparent");
		else { static const char hx[] = "0123456789ABCDEF"; cat ("#"); for (int s = 20; s >= 0; s -= 4) { char d[2] = { hx[(v >> s) & 15], 0 }; cat (d); } }
	}
	cat ("   "); cat (g_path);
	if (changed ()) cat (" *");
	g_status->setText (t);
}

// ---- file ------------------------------------------------------------------------------------
static bool save_bmp (const char *path)
{
	int row = (g_w * 3 + 3) & ~3, size = 54 + row * g_h;
	static unsigned char out[54 + MAXS * 4 * MAXS];
	for (int i = 0; i < size; i++) out[i] = 0;
	auto u32 = [&] (int at, unsigned v) { out[at] = (unsigned char) v; out[at + 1] = (unsigned char) (v >> 8); out[at + 2] = (unsigned char) (v >> 16); out[at + 3] = (unsigned char) (v >> 24); };
	out[0] = 'B'; out[1] = 'M'; u32 (2, (unsigned) size); u32 (10, 54); u32 (14, 40);
	u32 (18, (unsigned) g_w); u32 (22, (unsigned) g_h); out[26] = 1; out[28] = 24; u32 (34, (unsigned) (row * g_h));
	u32 (38, 2835); u32 (42, 2835);
	for (int y = 0; y < g_h; y++)
	{
		unsigned char *p = out + 54 + (g_h - 1 - y) * row;
		for (int x = 0; x < g_w; x++)
		{
			unsigned v = g_img[y * MAXS + x];
			p[x * 3] = (unsigned char) v; p[x * 3 + 1] = (unsigned char) (v >> 8); p[x * 3 + 2] = (unsigned char) (v >> 16);
		}
	}
	if (kapi_save_file (path, out, (unsigned) size) < 0) { wk_messagebox ("Save", "Cannot write the file.", MB_OK); return false; }
	g_saved = hash_img ();
	return true;
}
static bool load_bmp (const char *path)
{
	int w, h;
	unsigned *px = ui::bmp_decode (path, &w, &h);
	if (px == 0) { wk_messagebox ("Open", "Not a 24-bit BMP file.", MB_OK); return false; }
	if (w > MAXS || h > MAXS) { delete[] px; wk_messagebox ("Open", "Icons can be at most 64 x 64 pixels.", MB_OK); return false; }
	g_w = w; g_h = h;
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) g_img[y * MAXS + x] = px[y * w + x] & 0x00FFFFFF;
	delete[] px;
	g_nundo = g_nredo = 0;
	int i = 0; for (; path[i] && i < (int) sizeof g_path - 1; i++) g_path[i] = path[i];
	g_path[i] = 0;
	g_saved = hash_img ();
	return true;
}

static void on_save_as ();
static void on_save () { if (save_bmp (g_path)) refresh (); }
static void on_save_as ()
{
	char p[128];
	if (wk_file_save (p, sizeof p, "SD:/", g_path)) { int i = 0; for (; p[i] && i < 127; i++) g_path[i] = p[i]; g_path[i] = 0; on_save (); }
}
static void new_icon (int s)
{
	if (!doc_confirm (g_path, changed (), on_save)) return;
	g_w = g_h = s;
	for (int i = 0; i < MAXS * MAXS; i++) g_img[i] = TRANSP;
	g_nundo = g_nredo = 0;
	const char *d = "SD:/icon.bmp"; int i = 0; for (; d[i]; i++) g_path[i] = d[i]; g_path[i] = 0;
	g_saved = hash_img ();
	refresh ();
}
static void on_new16 () { new_icon (16); }
static void on_new24 () { new_icon (24); }
static void on_new32 () { new_icon (32); }
static void on_new40 () { new_icon (40); }
static void on_new48 () { new_icon (48); }
static void on_new64 () { new_icon (64); }
static void on_open ()
{
	if (!doc_confirm (g_path, changed (), on_save)) return;
	char p[128];
	if (wk_file_open (p, sizeof p, "SD:/apps/")) load_bmp (p);
	refresh ();
}

// ---- edit / image -----------------------------------------------------------------------------
static void on_undo ()
{
	if (!g_nundo) return;
	kapi_memcpy (g_redo[g_nredo], g_img, sizeof g_img); g_rw[g_nredo] = g_w; g_rh[g_nredo] = g_h; if (g_nredo < NUNDO - 1) g_nredo++;
	g_nundo--; kapi_memcpy (g_img, g_undo[g_nundo], sizeof g_img); g_w = g_uw[g_nundo]; g_h = g_uh[g_nundo];
	refresh ();
}
static void on_redo ()
{
	if (!g_nredo) return;
	int keep = g_nredo;
	kapi_memcpy (g_undo[g_nundo], g_img, sizeof g_img); g_uw[g_nundo] = g_w; g_uh[g_nundo] = g_h; if (g_nundo < NUNDO - 1) g_nundo++;
	g_nredo = keep - 1; kapi_memcpy (g_img, g_redo[g_nredo], sizeof g_img); g_w = g_rw[g_nredo]; g_h = g_rh[g_nredo];
	refresh ();
}
static unsigned g_tmp[MAXS * MAXS];
static void transform (int kind)
{
	push_undo ();
	kapi_memcpy (g_tmp, g_img, sizeof g_img);
	int w = g_w, h = g_h;
	if (kind == 2) { g_w = h; g_h = w; }			// rotate: swap
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
	{
		unsigned v = g_tmp[y * MAXS + x];
		int nx = x, ny = y;
		switch (kind)
		{
		case 0: nx = w - 1 - x; break;					// flip horizontal
		case 1: ny = h - 1 - y; break;					// flip vertical
		case 2: nx = h - 1 - y; ny = x; break;				// rotate 90 clockwise
		case 3: nx = (x + w - 1) % w; break;				// shift left
		case 4: nx = (x + 1) % w; break;				// shift right
		case 5: ny = (y + h - 1) % h; break;				// shift up
		case 6: ny = (y + 1) % h; break;				// shift down
		}
		g_img[ny * MAXS + nx] = v;
	}
	refresh ();
}
static void on_fliph () { transform (0); }
static void on_flipv () { transform (1); }
static void on_rot () { transform (2); }
static void on_sl () { transform (3); }
static void on_sr () { transform (4); }
static void on_su () { transform (5); }
static void on_sd () { transform (6); }
static void on_clear () { push_undo (); for (int i = 0; i < MAXS * MAXS; i++) g_img[i] = TRANSP; refresh (); }
static void on_grid () { g_gridOn = !g_gridOn; refresh (); }
static void on_more ()
{
	unsigned c = g_col[0] == TRANSP ? 0x00808080 : g_col[0];
	if (wk_color_dialog (&c, "Primary colour")) { g_col[0] = c & 0x00FFFFFF; if (g_col[0] == TRANSP) g_col[0] = 0x00FE00FE; }
	refresh ();
}
static void btn_more (Widget &) { on_more (); }
static void btn_tool (Widget &w) { if (w.tag == T_PICK && g_tool != T_PICK) g_prevTool = g_tool; g_tool = w.tag; refresh (); g_grid->setFocus (); }
static void on_swap () { unsigned t = g_col[0]; g_col[0] = g_col[1]; g_col[1] = t; refresh (); }

class IconRoot : public Root
{
public:
	IconRoot () : Root (W, H, "Icon Editor") {}
	bool onKey (long k) override
	{
		for (int i = 0; i < NTOOL; i++) if (k == TOOL_KEY[i] || k == TOOL_KEY[i] - 32)
		{ if (i == T_PICK && g_tool != T_PICK) g_prevTool = g_tool; g_tool = i; refresh (); return true; }
		if (k == 'g' || k == 'G') { on_grid (); return true; }
		if (k == 'x' || k == 'X') { on_swap (); return true; }
		return false;
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		char p[128];
		if (type != DND_FILES || !doc_first_path (data, p, sizeof p)) return;
		if (!doc_confirm (g_path, changed (), on_save)) return;
		load_bmp (p); refresh ();
	}
};

int main (void)
{
	IconRoot root;
	if (root.canvas.px == 0) return 1;
	root.setBg (C_BG);
	for (int i = 0; i < MAXS * MAXS; i++) g_img[i] = TRANSP;
	int fh = wk_fh ();
	for (int i = 0; i < NTOOL; i++)
	{
		g_toolBtn[i] = new Button (6, 8 + i * 34, TOOL_W - 12, 28, TOOL_NAME[i], btn_tool);
		g_toolBtn[i]->tag = i;
		root.addChild (g_toolBtn[i]);
	}
	int gridW = W - TOOL_W - SIDE_W, gridH = H - fh - 16;
	g_grid = new Grid (TOOL_W, 4, gridW, gridH);
	root.addChild (g_grid);
	int sx = W - SIDE_W + 6;
	g_pal = new Palette (sx, 8);
	root.addChild (g_pal);
	root.addChild (new Button (sx + 60, 8 + g_pal->height - 36, 90, 26, "More...", btn_more));
	root.addChild (new Label (sx, 8 + g_pal->height + 6, SIDE_W - 12, fh + 2, "Preview", C_DIS, C_BG));
	g_prev = new Preview (sx - 4, 8 + g_pal->height + fh + 10, SIDE_W - 4, 2 * MAXS + 20);
	root.addChild (g_prev);
	root.addChild (new Label (sx, H - fh * 5 - 20, SIDE_W - 10, fh + 2, "Left: 1st colour", C_DIS, C_BG));
	root.addChild (new Label (sx, H - fh * 4 - 18, SIDE_W - 10, fh + 2, "Right: 2nd colour", C_DIS, C_BG));
	root.addChild (new Label (sx, H - fh * 3 - 16, SIDE_W - 10, fh + 2, "X: swap them", C_DIS, C_BG));
	g_status = new Label (6, H - fh - 8, W - 12, fh + 2, "", C_TEXT, C_BG);
	root.addChild (g_status);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New 40 x 40 (app icon)", "^N", WK_CTRL ('N'), on_new40);
	menu.item ("New 16 x 16", "", 0, on_new16);
	menu.item ("New 24 x 24", "", 0, on_new24);
	menu.item ("New 32 x 32", "", 0, on_new32);
	menu.item ("New 48 x 48", "", 0, on_new48);
	menu.item ("New 64 x 64", "", 0, on_new64);
	menu.separator ();
	menu.item ("Open...",    "^O", WK_CTRL ('O'), on_open);
	menu.item ("Save",       "^S", WK_CTRL ('S'), on_save);
	menu.item ("Save As...", "",   0,             on_save_as);
	menu.menu ("Edit");
	menu.item ("Undo",       "^Z", WK_CTRL ('Z'), on_undo);
	menu.item ("Redo",       "^Y", WK_CTRL ('Y'), on_redo);
	menu.separator ();
	menu.item ("Swap Colours", "X", 0, on_swap);
	menu.item ("More Colours...", "", 0, on_more);
	menu.menu ("Image");
	menu.item ("Flip Horizontal", "", 0, on_fliph);
	menu.item ("Flip Vertical",   "", 0, on_flipv);
	menu.item ("Rotate 90",       "", 0, on_rot);
	menu.separator ();
	menu.item ("Shift Left",  "", 0, on_sl);
	menu.item ("Shift Right", "", 0, on_sr);
	menu.item ("Shift Up",    "", 0, on_su);
	menu.item ("Shift Down",  "", 0, on_sd);
	menu.separator ();
	menu.item ("Clear",        "", 0, on_clear);
	menu.item ("Grid On / Off", "G", 0, on_grid);
	menu.publish ();

	char args[128];
	if (kapi_get_args (args, sizeof args) > 0 && args[0]) load_bmp (args);
	else g_saved = hash_img ();
	refresh ();
	g_grid->setFocus ();
	root.run ();
	return 0;
}
