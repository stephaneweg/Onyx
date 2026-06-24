//
// paint -- a tiny drawing app. A colour palette runs along the top; below it, drag
// the mouse to paint with the selected colour (right-drag erases). Keys: [ ] change
// brush size, c clears, s saves the canvas to SD:/paint.bmp.
//
#include "kapi.h"
#include "uikit.hpp"
#include "uidialog.hpp"		// ui::FileDialog (user-side modal)
#include "applib.h"

#define W	420
#define H	300
#define PAL_H	26
#define NSW	8

static unsigned *fb;
static unsigned g_col = 0x00e05050;
static int g_brush = 3;

static const unsigned SW[NSW] = {
	0x00000000, 0x00ffffff, 0x00e05050, 0x0050c060,
	0x004080e0, 0x00e0c040, 0x00c060d0, 0x0040c0c0
};

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void clear_canvas (void) { fill_rect (0, PAL_H, W, H - PAL_H, 0x00f0f0f0); }

static void draw_palette (void)
{
	fill_rect (0, 0, W, PAL_H, 0x00303840);
	for (int i = 0; i < NSW; i++)
	{
		int x = 4 + i * 26;
		fill_rect (x, 3, 22, PAL_H - 6, SW[i]);
		if (SW[i] == g_col) { fill_rect (x, 3, 22, 2, 0x00ffffff); fill_rect (x, PAL_H - 5, 22, 2, 0x00ffffff); }
	}
	char b[24]; int p = 0;
	const char *t = "brush "; for (int i = 0; t[i]; i++) b[p++] = t[i];
	p += ax_itoa (g_brush, b + p); b[p] = '\0';
	kapi_draw_text (NSW * 26 + 12, 8, b, 0x00d0d0d0);
	kapi_draw_text (W - 150, 8, "[ ] size  c clear  s save", 0x0090a0a0);
}

static void paint_at (int x, int y, unsigned col)
{
	if (y < PAL_H + g_brush) return;
	fill_rect (x - g_brush, y - g_brush, g_brush * 2, g_brush * 2, col);
}

static ui::Ui *g_ui = 0;		// decorates the window + anchors the save dialog

// Input via the pointer stream (so the user-side file dialog can swap it cleanly).
// A press picks a palette colour; a press or button-held drag paints (right = erase).
static void on_ptr (unsigned long s, int ev, long val)
{
	if (g_ui) g_ui->onEvent (s, ev, val);
	if (ev != GUI_EVENT_PTR_DOWN && ev != GUI_EVENT_PTR_MOVE) return;
	unsigned btn = (unsigned) GUI_PTR_BUTTONS (val);
	int x = GUI_PTR_X (val), y = GUI_PTR_Y (val);

	if (y < PAL_H)						// palette: pick a colour on a press
	{
		if (ev == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (val) & 1))
		{
			int i = (x - 4) / 26;
			if (i >= 0 && i < NSW) g_col = SW[i];
		}
		return;
	}
	if (btn & 3) paint_at (x, y, (btn & 2) ? 0x00f0f0f0 : g_col);	// right = erase
}

static void save_bmp (const char *path)
{
	// 24-bpp BMP of the canvas area (bottom-up, BGR, 4-byte row padding).
	static unsigned char bmp[W * (H - PAL_H) * 3 + 64];
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
			unsigned c = fb[(PAL_H + y) * W + x];
			bmp[o++] = c & 0xFF; bmp[o++] = (c >> 8) & 0xFF; bmp[o++] = (c >> 16) & 0xFF;
		}
		for (int x = 0; x < pad; x++) bmp[o++] = 0;
	}
	kapi_save_file (path, bmp, (unsigned) total);
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	if (key == '[') { if (g_brush > 1) g_brush--; }
	else if (key == ']') { if (g_brush < 20) g_brush++; }
	else if (key == 'c' || key == 'C') clear_canvas ();
	else if (key == 's' || key == 'S')
	{
		char path[100];
		// The file dialog is an in-canvas overlay; back up the canvas pixels and
		// restore them after, so the painting isn't clobbered, then save.
		unsigned *bak = new unsigned[W * H];
		if (bak) for (int i = 0; i < W * H; i++) bak[i] = fb[i];
		ui::FileDialog fd (*g_ui, "SD:/", "paint.bmp", true);
		bool ok = fd.run (g_ui);
		kapi_set_pointer_handler (on_ptr);		// re-claim input from the modal
		kapi_set_key_handler (on_key);
		if (bak) { for (int i = 0; i < W * H; i++) fb[i] = bak[i]; delete[] bak; present (); }
		if (ok) { fd.getResult (path, sizeof path); save_bmp (path); }
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "paint");
	if (fb == 0) return 1;
	ui::Ui ui (fb, W, H); g_ui = &ui;	// decorates the window; anchors the save dialog
	clear_canvas ();
	kapi_set_pointer_handler (on_ptr);
	kapi_set_key_handler (on_key);
	while (!should_exit ())
	{
		pump_events ();
		draw_palette ();		// redraw only the palette; canvas persists
		msleep (16);
	}
	return 0;
}
