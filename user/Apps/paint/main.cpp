//
// paint -- a tiny drawing app (wtk port). A colour palette runs along the top (its own
// widget); below it a paint-canvas widget holds the persistent drawing: drag the mouse
// to paint with the selected colour (right-drag erases). Keys: [ ] change brush size,
// c clears, s saves the canvas to a BMP via the wtk save dialog.
//
#include "kapi.h"
#include "wtk/wtk.h"		// recursive widget toolkit + wk_file_save
#include "applib.h"

using namespace wtk;

#define W	420
#define H	300
#define PAL_H	26
#define NSW	8

static unsigned g_col = 0x00e05050;
static int g_brush = 3;

static const unsigned SW[NSW] = {
	0x00000000, 0x00ffffff, 0x00e05050, 0x0050c060,
	0x004080e0, 0x00e0c040, 0x00c060d0, 0x0040c0c0
};

// The drawing surface (below the palette). Its canvas IS the painting -- onDraw clears it
// once (to white); strokes write straight into the canvas and push up via invalidate(false)
// so the picture persists across frames and across an overlaid modal dialog.
class PaintArea : public Widget
{
public:
	PaintArea (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override { canvas.clear (0x00f0f0f0); }	// runs once (never re-dirtied)
	void clearCanvas () { canvas.clear (0x00f0f0f0); invalidate (false); }
	void stroke (int lx, int ly, unsigned col)
	{ canvas.fillRect (lx - g_brush, ly - g_brush, g_brush * 2, g_brush * 2, col); invalidate (false); }
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		if (mx < 0) return false;
		if (bl || br) stroke (mx, my, br ? 0x00f0f0f0 : g_col);	// right = erase
		return true;
	}
};

// The colour palette + brush/help text. A press picks a swatch.
class PaletteBar : public Widget
{
public:
	PaletteBar (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (0x00303840);
		for (int i = 0; i < NSW; i++)
		{
			int x = 4 + i * 26;
			canvas.fillRect (x, 3, 22, PAL_H - 6, SW[i]);
			if (SW[i] == g_col) { canvas.fillRect (x, 3, 22, 2, 0x00ffffff); canvas.fillRect (x, PAL_H - 5, 22, 2, 0x00ffffff); }
		}
		char b[24]; int p = 0;
		const char *t = "brush "; for (int i = 0; t[i]; i++) b[p++] = t[i];
		p += ax_itoa (g_brush, b + p); b[p] = '\0';
		canvas.text (NSW * 26 + 12, 8, b, 0x00d0d0d0);
		canvas.text (W - 150, 8, "[ ] size  c clear  s save", 0x0090a0a0);
	}
	bool onMouse (int mx, int /*my*/, int bl, int, int, int) override
	{
		if (mx < 0) { pressed = false; return false; }
		if (bl && !pressed)				// pick a colour on the press edge
		{
			pressed = true;
			int i = (mx - 4) / 26;
			if (i >= 0 && i < NSW) { g_col = SW[i]; invalidate (true); }
		}
		else if (!bl) pressed = false;
		return true;
	}
};

static PaintArea  *g_area = 0;
static PaletteBar *g_palette = 0;

static void save_bmp (const char *path)
{
	// 24-bpp BMP of the canvas area (bottom-up, BGR, 4-byte row padding).
	static unsigned char bmp[W * (H - PAL_H) * 3 + 64];
	const unsigned *src = g_area->canvas.px;
	int dw = W, dh = H - PAL_H;
	int rowb = dw * 3, pad = (4 - rowb % 4) % 4, stride = rowb + pad;
	int imgsz = stride * dh, total = 54 + imgsz;

	for (int i = 0; i < 54; i++) bmp[i] = 0;
	bmp[0] = 'B'; bmp[1] = 'M';
	bmp[2] = total; bmp[3] = total >> 8; bmp[4] = total >> 16; bmp[5] = total >> 24;
	bmp[10] = 54;
	bmp[14] = 40; bmp[18] = dw; bmp[19] = dw >> 8; bmp[20] = dw >> 16;
	bmp[22] = dh; bmp[23] = dh >> 8; bmp[24] = dh >> 16;
	bmp[26] = 1; bmp[28] = 24;
	int o = 54;
	for (int y = dh - 1; y >= 0; y--)
	{
		for (int x = 0; x < dw; x++)
		{
			unsigned c = src[y * W + x];
			bmp[o++] = c & 0xFF; bmp[o++] = (c >> 8) & 0xFF; bmp[o++] = (c >> 16) & 0xFF;
		}
		for (int x = 0; x < pad; x++) bmp[o++] = 0;
	}
	kapi_save_file (path, bmp, (unsigned) total);
}

// Window: global keys ([ ] brush size, c clear, s save). No widget steals focus, so keys
// fall through to the Root's onKey.
class PaintRoot : public Root
{
public:
	PaintRoot (int w, int h, const char *t) : Root (w, h, t) {}
	bool onKey (long key) override
	{
		if (key == '[') { if (g_brush > 1) { g_brush--; g_palette->invalidate (true); } }
		else if (key == ']') { if (g_brush < 20) { g_brush++; g_palette->invalidate (true); } }
		else if (key == 'c' || key == 'C') g_area->clearCanvas ();
		else if (key == 's' || key == 'S')
		{
			char path[100];
			if (wk_file_save (path, sizeof path, "SD:/", "paint.bmp")) save_bmp (path);
		}
		else return false;
		return true;
	}
};

int main (void)
{
	PaintRoot root (W, H, "paint");
	if (root.canvas.px == 0) return 1;

	g_palette = new PaletteBar (0, 0, W, PAL_H);          root.addChild (g_palette);
	g_area    = new PaintArea (0, PAL_H, W, H - PAL_H);   root.addChild (g_area);

	root.run ();
	return 0;
}
