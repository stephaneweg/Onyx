//
// paint -- a tiny drawing app (wtk). The paint-canvas widget holds the persistent
// drawing: drag the mouse to paint with the selected colour (right-drag erases). A thin
// status strip at the bottom shows the current colour and brush size.
//
// Commands are in the system menu bar (wtk::Menu): File (New ^N clears, Open... ^O loads
// a 24-bpp BMP, Save ^S, Save As...), Brush (Smaller [, Larger ], sizes 1/3/6/12), Color
// (the 8 colours). The app name menu has Quit (^Q). A BMP path as argument is opened
// (fileassoc.ini: bmp = paint); a BMP dropped on the window too -- after asking to save
// unsaved changes (docguard.h).
//
#include "kapi.h"
#include "bmp.hpp"
#include "wtk/wtk.h"		// recursive widget toolkit + wk_file_open / wk_file_save + Menu
#include "applib.h"
#include "docguard.h"

using namespace wtk;

#define W	420
#define H	300
#define ST_H	18			// status strip
#define AREA_H	(H - ST_H)
#define NSW	8
#define PAPER	0x00f0f0f0

static unsigned g_col = 0x00e05050;
static int g_brush = 3;

static const unsigned SW[NSW] = {
	0x00000000, 0x00ffffff, 0x00e05050, 0x0050c060,
	0x004080e0, 0x00e0c040, 0x00c060d0, 0x0040c0c0
};
static const char *SWNAME[NSW] = { "Black", "White", "Red", "Green", "Blue", "Yellow", "Purple", "Cyan" };

// The drawing surface. Its canvas IS the painting -- onDraw clears it once (to paper);
// strokes write straight into the canvas and push up via invalidate(false) so the picture
// persists across frames and across an overlaid modal dialog.
class PaintArea : public Widget
{
public:
	bool ready = false;				// the canvas holds the picture already
	PaintArea (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override { if (!ready) canvas.clear (PAPER); ready = true; }	// once
	void clearCanvas () { canvas.clear (PAPER); ready = true; invalidate (false); }
	void stroke (int lx, int ly, unsigned col)
	{ canvas.fillRect (lx - g_brush, ly - g_brush, g_brush * 2, g_brush * 2, col); invalidate (false); }
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		if (mx < 0) return false;
		if (bl || br) stroke (mx, my, br ? PAPER : g_col);	// right = erase
		return true;
	}
};

// Status strip: current colour swatch + name + brush size (information only).
class StatusBar : public Widget
{
public:
	StatusBar (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (0x00303840);
		canvas.fillRect (6, 3, 28, ST_H - 6, g_col);
		canvas.frameRect (6, 3, 28, ST_H - 6, 0x00a0a8b0);
		const char *name = "custom";
		for (int i = 0; i < NSW; i++) if (SW[i] == g_col) name = SWNAME[i];
		char b[48]; int p = 0;
		for (int i = 0; name[i]; i++) b[p++] = name[i];
		const char *t = "   brush "; for (int i = 0; t[i]; i++) b[p++] = t[i];
		p += ax_itoa (g_brush, b + p); b[p] = '\0';
		canvas.text (42, (ST_H - wk_fh ()) / 2, b, 0x00d0d0d0);
	}
};

static PaintArea *g_area = 0;
static StatusBar *g_status = 0;
static char      g_path[100] = "";		// the open / last saved file ("" = none yet)
static unsigned  g_saved;			// doc_hash of the picture as last loaded / saved

static unsigned pic_hash (void)
{
	return doc_hash (g_area->canvas.px, (unsigned) (g_area->canvas.stride * AREA_H * 4));
}
static void mark_saved (void) { g_saved = pic_hash (); }
static bool changed (void)    { return pic_hash () != g_saved; }

static void save_bmp (const char *path)
{
	// 24-bpp BMP of the canvas area (bottom-up, BGR, 4-byte row padding).
	static unsigned char bmp[W * AREA_H * 3 + 64];
	const unsigned *src = g_area->canvas.px;
	int dw = W, dh = AREA_H;
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
			unsigned c = src[y * g_area->canvas.stride + x];
			bmp[o++] = c & 0xFF; bmp[o++] = (c >> 8) & 0xFF; bmp[o++] = (c >> 16) & 0xFF;
		}
		for (int x = 0; x < pad; x++) bmp[o++] = 0;
	}
	if (kapi_save_file (path, bmp, (unsigned) total) >= 0)
	{
		int i = 0; for (; path[i] && i < (int) sizeof g_path - 1; i++) g_path[i] = path[i];
		g_path[i] = '\0';
		mark_saved ();
	}
}

// Load a 24-bpp BMP into the canvas (top-left aligned, clipped; the rest is paper).
static void load_bmp (const char *path)
{
	int bw = 0, bh = 0;
	unsigned *img = ui::bmp_decode (path, &bw, &bh);
	if (img == 0) { wk_messagebox ("Open", "Not a 24-bit uncompressed BMP.", MB_OK); return; }
	g_area->canvas.clear (PAPER);
	for (int y = 0; y < bh && y < AREA_H; y++)
		for (int x = 0; x < bw && x < W; x++)
			g_area->canvas.px[y * g_area->canvas.stride + x] = img[y * bw + x];
	delete [] img;
	g_area->invalidate (false);
	int i = 0; for (; path[i] && i < (int) sizeof g_path - 1; i++) g_path[i] = path[i];
	g_path[i] = '\0';
	mark_saved ();
}

// ---- menu commands ------------------------------------------------------------------------
static void set_brush (int b) { if (b < 1) b = 1; if (b > 20) b = 20; g_brush = b; g_status->invalidate (true); }
static void set_col (int i)   { g_col = SW[i]; g_status->invalidate (true); }

static void onSaveAs ()  { char path[100]; if (wk_file_save (path, sizeof path, "SD:/", g_path[0] ? g_path : "paint.bmp")) save_bmp (path); }
static void onSave ()    { if (g_path[0]) save_bmp (g_path); else onSaveAs (); }
static const char *doc_name (void) { return g_path[0] ? g_path : "the picture"; }
static void onNew ()
{
	if (!doc_confirm (doc_name (), changed (), onSave)) return;
	g_area->clearCanvas (); g_path[0] = '\0'; mark_saved ();
}
static void onOpen ()
{
	if (!doc_confirm (doc_name (), changed (), onSave)) return;
	char path[100]; if (wk_file_open (path, sizeof path, "SD:/")) load_bmp (path);
}
static void onSmaller () { set_brush (g_brush - 1); }
static void onLarger ()  { set_brush (g_brush + 1); }
static void onB1 ()  { set_brush (1); }
static void onB3 ()  { set_brush (3); }
static void onB6 ()  { set_brush (6); }
static void onB12 () { set_brush (12); }
static void c0 () { set_col (0); } static void c1 () { set_col (1); } static void c2 () { set_col (2); }
static void c3 () { set_col (3); } static void c4 () { set_col (4); } static void c5 () { set_col (5); }
static void c6 () { set_col (6); } static void c7 () { set_col (7); }

// The window: a BMP dropped on it replaces the picture.
class PaintRoot : public Root
{
public:
	PaintRoot () : Root (W, H, "paint") {}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		char path[100];
		if (type != DND_FILES || !doc_first_path (data, path, sizeof path)) return;
		void *d = kapi_opendir (path);
		if (d) { kapi_closedir (d); return; }		// a folder: nothing to open
		if (!doc_confirm (doc_name (), changed (), onSave)) return;
		load_bmp (path);
	}
};

int main (void)
{
	PaintRoot root;
	if (root.canvas.px == 0) return 1;

	g_area   = new PaintArea (0, 0, W, AREA_H);   root.addChild (g_area);
	g_status = new StatusBar (0, AREA_H, W, ST_H); root.addChild (g_status);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New",         "^N", WK_CTRL ('N'), onNew);
	menu.item ("Open...",     "^O", WK_CTRL ('O'), onOpen);
	menu.item ("Save",        "^S", WK_CTRL ('S'), onSave);
	menu.item ("Save As...",  "",   0,             onSaveAs);
	menu.menu ("Brush");
	menu.item ("Smaller",     "[",  '[',           onSmaller);
	menu.item ("Larger",      "]",  ']',           onLarger);
	menu.separator ();
	menu.item ("Fine (1)",    "",   0,             onB1);
	menu.item ("Normal (3)",  "",   0,             onB3);
	menu.item ("Thick (6)",   "",   0,             onB6);
	menu.item ("Huge (12)",   "",   0,             onB12);
	menu.menu ("Color");
	static MenuAction cols[NSW] = { c0, c1, c2, c3, c4, c5, c6, c7 };
	for (int i = 0; i < NSW; i++) menu.item (SWNAME[i], "", 0, cols[i]);
	menu.publish ();

	g_area->clearCanvas ();			// paper now (not at the first draw): a clean baseline
	mark_saved ();
	char args[100];
	int an = kapi_get_args (args, sizeof args);
	if (an > 0 && args[0] != '\0') load_bmp (args);

	root.run ();
	return 0;
}
