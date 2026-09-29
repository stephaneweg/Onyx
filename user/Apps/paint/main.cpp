//
// paint -- Onyx's Paint, in the way of Windows 11's: a picture of transparent layers drawn to the
// pixel. The ribbon (Edit: paste, cut, copy, undo, redo -- Image: select, crop, resize, rotate / flip
// -- Tools: pencil, fill, eraser, colour picker, magnifier, brush -- Shapes: line, rectangle, rounded
// rectangle, ellipse, the polygons inscribed in the ellipse of their box, stars, arrow, heart, their
// outline and fill -- Size -- Colours: colour 1 and 2, a palette of twenty, ten of your own, Edit
// colours -- View: the pixel grid, the whole picture), the canvas (zoomed from 12 % to 3200 %), the
// layers' panel (shown / hidden, opacity, add, duplicate, delete, move, merge), the status bar.
//
// Pieces: pdoc.h (the layers, the composite, undo by tiles), raster.h (the drawing), pview.h (the
// canvas and its tools), pui.h (the ribbon, the layers' panel, the status bar, the icons), pfile.h
// (OpenRaster, the pictures, the exports).
//
// Files: File > Save writes the working format, OpenRaster (.ora: the layers kept -- GIMP and Krita
// open it); Open reads it, or a PNG, JPEG, BMP, GIF (WebP, PCX) picture; File > Export writes what
// is visible, flattened, as PNG, JPEG, BMP or GIF. A picture named on the command line, or dropped on
// the window, is opened. Closed with unsaved changes, the picture is kept in
// SD:/apps/paint.app/recovered.ora and offered back at the next start.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "clipboard.h"
#include "docguard.h"
#include "pui.h"
#include "pfile.h"

using namespace wtk;
using namespace pd;

#define W 1000
#define H 700
static const char *RECOVER = "SD:/apps/paint.app/recovered.ora";
static const char *RECOVER_NAME = "SD:/apps/paint.app/recovered.txt";

static CanvasView *g_view;
static Ribbon *g_ribbon;
static LayersPanel *g_layers;
static StatusBar *g_status;
static char g_path[200];			// "" : not saved yet (a picture opened: its path, Save asks for an .ora)
static unsigned g_saved;
static unsigned *g_clip; static int g_clipW, g_clipH;	// the copied pixels

static bool changed_doc () { return D.changes != g_saved; }
static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1; return b; }
static void focus_view () { if (g_view) g_view->setFocus (); }

static void refresh ()
{
	if (!g_view) return;
	g_view->invalidate (true);
	g_ribbon->invalidate (true);
	g_layers->sync ();
	g_status->invalidate (true);
}

// ---- files ------------------------------------------------------------------------------------------------
static void doc_loaded ()
{
	g_sel = norect ();
	g_saved = D.changes;
	g_view->sx = g_view->sy = 0;
	g_view->zoomFit ();
	refresh ();
}
static bool open_path (const char *path)
{
	if (!doc_open (path)) { wk_messagebox ("Open", "That file is not a picture Paint can read (PNG, JPEG, BMP, GIF, WebP, PCX, OpenRaster).", MB_OK); return false; }
	scpy (g_path, path, sizeof g_path);
	doc_loaded ();
	return true;
}
static void with_ext (char *out, int cap, const char *name, const char *ext)
{
	scpy (out, name, cap);
	int n = slen (out), dot = n; while (dot > 0 && out[dot - 1] != '.') dot--;
	if (dot > 0) out[dot - 1] = 0;
	n = slen (out); scpy (out + n, ext, cap - n);
}
static void cmd_save_as ();
static void cmd_save ()
{
	settle ();
	if (!g_path[0] || !ends_with (g_path, ".ora")) { cmd_save_as (); return; }
	unsigned n; unsigned char *b = ora_save (&n);
	bool ok = kapi_save_file (g_path, b, n) >= 0;
	delete[] b;
	if (ok) g_saved = D.changes; else wk_messagebox ("Save", "The file could not be written.", MB_OK);
	refresh (); focus_view ();
}
static void cmd_save_as ()
{
	settle ();
	char def[80], path[200];
	with_ext (def, sizeof def, g_path[0] ? base_name (g_path) : "Untitled", ".ora");
	if (wk_file_save (path, sizeof path, "SD:/", def))
	{
		if (!ends_with (path, ".ora")) { int k = slen (path); scpy (path + k, ".ora", (int) sizeof path - k); }
		scpy (g_path, path, sizeof g_path);
		cmd_save ();
		return;
	}
	focus_view ();
}
static void save_for_guard () { cmd_save (); }
static bool guard () { settle (); return doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard); }
static void export_as (const char *ext)
{
	settle ();
	char def[80], path[200];
	with_ext (def, sizeof def, g_path[0] ? base_name (g_path) : "Untitled", ext);
	if (wk_file_save (path, sizeof path, "SD:/", def))
	{
		if (!ends_with (path, ".png") && !ends_with (path, ".jpg") && !ends_with (path, ".jpeg") && !ends_with (path, ".bmp") && !ends_with (path, ".gif"))
		{ int k = slen (path); scpy (path + k, ext, (int) sizeof path - k); }
		unsigned n; unsigned char *b = export_bytes (path, &n);
		if (kapi_save_file (path, b, n) < 0) wk_messagebox ("Export", "The file could not be written.", MB_OK);
		delete[] b;
	}
	focus_view ();
}
static void cmd_export_png () { export_as (".png"); }
static void cmd_export_jpg () { export_as (".jpg"); }
static void cmd_export_bmp () { export_as (".bmp"); }
static void cmd_export_gif () { export_as (".gif"); }

// ---- the New dialog -------------------------------------------------------------------------------------------
class NewDialog : public Modal
{
public:
	Textbox *tw, *th; RadioButton *white, *clear;
	NewDialog () : Modal (340, 230)
	{
		Root *r = Root::current (); left = ((r ? r->width : 340) - width) / 2; top = ((r ? r->height : 230) - height) / 2;
		tw = new Textbox (150, 50, 90, 26, "640"); addChild (tw);
		th = new Textbox (150, 84, 90, 26, "480"); addChild (th);
		white = new RadioButton (40, 124, 120, 24, "White", 1, true, 0, C_FACE); addChild (white);
		clear = new RadioButton (170, 124, 150, 24, "Transparent", 1, false, 0, C_FACE); addChild (clear);
		Button *b = new Button (width - 184, height - 42, 82, 28, "OK", act); b->tag = 1; addChild (b);
		b = new Button (width - 94, height - 42, 82, 28, "Cancel", act); b->tag = 0; addChild (b);
	}
	static void act (Widget &w) { ((Modal *) w.parent)->close (w.tag); }
	void onDraw () override
	{
		drawBox ("New Picture");
		canvas.text (24, 56, "Width:", C_TEXT); canvas.text (250, 56, "pixels", C_TEXT);
		canvas.text (24, 90, "Height:", C_TEXT); canvas.text (250, 90, "pixels", C_TEXT);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { close (1); return true; } return false; }
};
static void cmd_new ()
{
	if (!guard ()) { focus_view (); return; }
	NewDialog d;
	if (d.run () == 1)
	{
		int w = pclamp (parse_int (d.tw->text), 1, 8192), h = pclamp (parse_int (d.th->text), 1, 8192);
		doc_new (w, h, d.white->checked);
		undo_clear (); compose_all ();
		g_path[0] = 0;
		doc_loaded ();
	}
	focus_view ();
}
static void cmd_open ()
{
	if (!guard ()) { focus_view (); return; }
	char path[200];
	if (wk_file_open (path, sizeof path, "SD:/")) open_path (path);
	focus_view ();
}

// ---- edits ----------------------------------------------------------------------------------------------------
static void cmd_undo () { settle (); if (undo ()) { g_sel = norect (); } refresh (); focus_view (); }
static void cmd_redo () { settle (); if (redo ()) { g_sel = norect (); } refresh (); focus_view (); }
static void set_tool (int t)
{
	if (t != T_SELECT) settle ();
	if (t == T_PICKER && g_tool != T_PICKER) g_prevTool = g_tool;
	g_tool = t;
	refresh (); focus_view ();
}
static void copy_sel ()
{
	if (D.fl.px)						// (the floating pixels)
	{
		clip_clear ();					// (the clipboard is Paint's now: not a file copied before)
		delete[] g_clip; g_clipW = D.fl.w; g_clipH = D.fl.h; g_clip = new unsigned[(unsigned) g_clipW * g_clipH];
		for (int i = 0; i < g_clipW * g_clipH; i++) g_clip[i] = D.fl.px[i];
		return;
	}
	Rect r = g_sel; r.clip (D.w, D.h);
	if (r.empty ()) return;
	clip_clear ();
	delete[] g_clip; g_clipW = r.x1 - r.x0; g_clipH = r.y1 - r.y0; g_clip = new unsigned[(unsigned) g_clipW * g_clipH];
	for (int y = 0; y < g_clipH; y++) for (int x = 0; x < g_clipW; x++) g_clip[y * g_clipW + x] = D.lay[D.cur].px[(unsigned) (r.y0 + y) * D.w + r.x0 + x];
}
static void cmd_copy () { copy_sel (); focus_view (); }
static void cmd_cut () { copy_sel (); g_view->deleteSelection (); refresh (); focus_view (); }
static void cmd_paste ()
{
	unsigned *px = 0; int w = 0, h = 0;
	char path[200]; int cut = 0;
	if (clip_get_file (path, sizeof path, &cut) && img_is_image_name (path))	// (a picture file copied in the File Viewer)
	{
		ImgFrames im;
		if (img_load (path, &im)) { for (int i = 1; i < im.n; i++) delete[] im.px[i]; px = im.px[0]; w = im.w; h = im.h; }
	}
	if (!px && g_clip) { w = g_clipW; h = g_clipH; px = new unsigned[(unsigned) w * h]; for (int i = 0; i < w * h; i++) px[i] = g_clip[i]; }
	if (!px) { focus_view (); return; }
	int x = pclamp (g_view->imX (0), 0, pmax (0, D.w - 1)), y = pclamp (g_view->imY (0), 0, pmax (0, D.h - 1));
	float_new (px, w, h, x, y);
	g_tool = T_SELECT;
	refresh (); focus_view ();
}
static void cmd_delete () { g_view->deleteSelection (); refresh (); focus_view (); }
static void cmd_select_all () { settle (); g_tool = T_SELECT; g_sel = mkrect (0, 0, D.w, D.h); refresh (); focus_view (); }
static void cmd_deselect () { settle (); g_sel = norect (); refresh (); focus_view (); }

// ---- the picture's changes ------------------------------------------------------------------------------------
static void new_size (int w, int h)
{
	D.w = w; D.h = h;
	delete[] D.comp; D.comp = new_px (w, h, 0);
	delete[] D.ov.px; D.ov.px = new_px (w, h, 0); D.ov.r = norect ();
}
static void cmd_crop ()
{
	float_commit (true);					// (a moved selection: put down, still selected)
	Rect r = g_sel; r.clip (D.w, D.h);
	if (r.empty ()) { wk_messagebox ("Crop", "Select the part to keep first (the Select tool).", MB_OK); focus_view (); return; }
	rec_whole ();
	int w = r.x1 - r.x0, h = r.y1 - r.y0;
	for (int k = 0; k < D.n; k++)
	{
		unsigned *n = new unsigned[(unsigned) w * h];
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) n[y * w + x] = D.lay[k].px[(unsigned) (r.y0 + y) * D.w + r.x0 + x];
		delete[] D.lay[k].px; D.lay[k].px = n;
	}
	new_size (w, h);
	g_sel = norect ();
	compose_all (); g_view->zoomFit (); refresh (); focus_view ();
}
// Rotate (1 right, -1 left, 2 half a turn) or flip (3 horizontal, 4 vertical): the selection, or the picture.
static void transform (int op)
{
	if (!g_sel.empty () || D.fl.px)
	{
		if (!float_lift ()) return;
		Rect old = float_rect ();
		if (op == 3) flip_h (D.fl.px, D.fl.w, D.fl.h);
		else if (op == 4) flip_v (D.fl.px, D.fl.w, D.fl.h);
		else if (op == 2) { flip_h (D.fl.px, D.fl.w, D.fl.h); flip_v (D.fl.px, D.fl.w, D.fl.h); }
		else
		{
			unsigned *n = rotate (D.fl.px, D.fl.w, D.fl.h, op == 1);
			delete[] D.fl.px; D.fl.px = n;
			int cx = D.fl.x + D.fl.w / 2, cy = D.fl.y + D.fl.h / 2, t = D.fl.w; D.fl.w = D.fl.h; D.fl.h = t;
			D.fl.x = cx - D.fl.w / 2; D.fl.y = cy - D.fl.h / 2;
		}
		old.add (float_rect ()); compose (old);
		g_sel = float_rect ();
		g_tool = T_SELECT;
	}
	else
	{
		rec_whole ();
		int w = D.w, h = D.h;
		for (int k = 0; k < D.n; k++)
		{
			unsigned *p = D.lay[k].px;
			if (op == 3) flip_h (p, w, h);
			else if (op == 4) flip_v (p, w, h);
			else if (op == 2) { flip_h (p, w, h); flip_v (p, w, h); }
			else { D.lay[k].px = rotate (p, w, h, op == 1); delete[] p; }
		}
		if (op == 1 || op == -1) new_size (h, w);
		compose_all ();
		if (op == 1 || op == -1) g_view->zoomFit ();
	}
	refresh (); focus_view ();
}
static void cmd_rot_right () { transform (1); }
static void cmd_rot_left () { transform (-1); }
static void cmd_rot_half () { transform (2); }
static void cmd_flip_h () { transform (3); }
static void cmd_flip_v () { transform (4); }

// The Resize dialog: the picture scaled, or its canvas made bigger / smaller.
class ResizeDialog : public Modal
{
public:
	RadioButton *scaleIt, *canvasIt, *anchorTL, *anchorC;
	Textbox *tw, *th; Checkbox *keep, *smooth;
	int w0, h0;
	ResizeDialog () : Modal (380, 330), w0 (D.w), h0 (D.h)
	{
		Root *r = Root::current (); left = ((r ? r->width : 380) - width) / 2; top = ((r ? r->height : 330) - height) / 2;
		scaleIt = new RadioButton (24, 44, 176, 24, "Resize the picture", 1, true, 0, C_FACE); addChild (scaleIt);
		canvasIt = new RadioButton (214, 44, 150, 24, "Canvas size", 1, false, 0, C_FACE); addChild (canvasIt);
		char b[16];
		fmt (b, D.w); tw = new Textbox (150, 86, 90, 26, b); tw->cb = onW; addChild (tw);
		fmt (b, D.h); th = new Textbox (150, 120, 90, 26, b); th->cb = onH; addChild (th);
		keep = new Checkbox (24, 158, 200, 24, "Keep the proportions", true, 0, C_FACE); addChild (keep);
		smooth = new Checkbox (24, 186, 250, 24, "Smooth (else sharp pixels)", false, 0, C_FACE); addChild (smooth);
		anchorTL = new RadioButton (24, 238, 150, 24, "Top left", 2, true, 0, C_FACE); addChild (anchorTL);
		anchorC = new RadioButton (180, 238, 150, 24, "Centre", 2, false, 0, C_FACE); addChild (anchorC);
		Button *bt = new Button (width - 184, height - 42, 82, 28, "OK", act); bt->tag = 1; addChild (bt);
		bt = new Button (width - 94, height - 42, 82, 28, "Cancel", act); bt->tag = 0; addChild (bt);
	}
	static void fmt (char *b, int v) { int n = 0; char t[12]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) b[n++] = t[--j]; b[n] = 0; }
	static void act (Widget &w) { ((Modal *) w.parent)->close (w.tag); }
	static void onW (Widget &w)
	{
		ResizeDialog *d = (ResizeDialog *) w.parent;
		if (d->keep->checked) { char b[16]; fmt (b, pmax (1, (int) ((long long) parse_int (d->tw->text) * d->h0 / pmax (1, d->w0)))); d->th->setText (b); }
	}
	static void onH (Widget &w)
	{
		ResizeDialog *d = (ResizeDialog *) w.parent;
		if (d->keep->checked) { char b[16]; fmt (b, pmax (1, (int) ((long long) parse_int (d->th->text) * d->w0 / pmax (1, d->h0)))); d->tw->setText (b); }
	}
	void onDraw () override
	{
		drawBox ("Resize");
		canvas.text (24, 92, "Width:", C_TEXT); canvas.text (250, 92, "pixels", C_TEXT);
		canvas.text (24, 126, "Height:", C_TEXT); canvas.text (250, 126, "pixels", C_TEXT);
		canvas.text (24, 216, "A new canvas keeps the picture at:", wk_mix (C_FACE, C_TEXT, 170));
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { onW (*tw); close (1); return true; } return false; }
};
static void cmd_resize ()
{
	settle ();
	ResizeDialog d;
	if (d.run () != 1) { focus_view (); return; }
	int w = pclamp (parse_int (d.tw->text), 1, 8192), h = pclamp (parse_int (d.th->text), 1, 8192);
	if (w == D.w && h == D.h) { focus_view (); return; }
	rec_whole ();
	for (int k = 0; k < D.n; k++)
	{
		unsigned *p = D.lay[k].px, *n;
		if (d.scaleIt->checked) n = scale (p, D.w, D.h, w, h, d.smooth->checked);
		else
		{
			n = new_px (w, h, k == 0 && (p[0] >> 24) == 255 ? 0xFFFFFFFFu : 0);	// (an opaque background: grown white)
			int ox = d.anchorC->checked ? (w - D.w) / 2 : 0, oy = d.anchorC->checked ? (h - D.h) / 2 : 0;
			for (int y = 0; y < D.h; y++) for (int x = 0; x < D.w; x++)
			{
				int X = x + ox, Y = y + oy;
				if (X >= 0 && Y >= 0 && X < w && Y < h) n[(unsigned) Y * w + X] = p[(unsigned) y * D.w + x];
			}
		}
		delete[] p; D.lay[k].px = n;
	}
	new_size (w, h);
	g_sel = norect ();
	compose_all (); g_view->zoomFit (); refresh (); focus_view ();
}

// ---- layers ---------------------------------------------------------------------------------------------------
static void layer_insert (int at, const char *name, unsigned *px)
{
	for (int k = D.n; k > at; k--) D.lay[k] = D.lay[k - 1];
	layer_init (D.lay[at], name, px);
	D.n++;
}
static void next_name (char *out, int cap)
{
	for (int i = 2; i < 100; i++)
	{
		char b[32] = "Layer "; int n = 6, v = i; char t[4]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) b[n++] = t[--j]; b[n] = 0;
		bool used = false; for (int k = 0; k < D.n; k++) { int q = 0; while (b[q] && b[q] == D.lay[k].name[q]) q++; if (!b[q] && !D.lay[k].name[q]) used = true; }
		if (!used) { scpy (out, b, cap); return; }
	}
	scpy (out, "Layer", cap);
}
static void cmd_layer_new ()
{
	settle ();
	if (D.n >= MAXLAYERS) return;
	rec_whole ();
	char nm[32]; next_name (nm, sizeof nm);
	layer_insert (D.cur + 1, nm, new_px (D.w, D.h, 0));
	D.cur++;
	compose_all (); refresh (); focus_view ();
}
static void cmd_layer_dup ()
{
	settle ();
	if (D.n >= MAXLAYERS) return;
	rec_whole ();
	const Layer &s = D.lay[D.cur];
	unsigned *px = new unsigned[(unsigned) D.w * D.h];
	for (int i = 0; i < D.w * D.h; i++) px[i] = s.px[i];
	char nm[32]; scpy (nm, s.name, 26); int n = slen (nm); scpy (nm + n, " copy", 32 - n);
	int vis = s.visible, op = s.opacity;
	layer_insert (D.cur + 1, nm, px);
	D.cur++; D.lay[D.cur].visible = vis; D.lay[D.cur].opacity = op;
	compose_all (); refresh (); focus_view ();
}
static void cmd_layer_del ()
{
	settle ();
	if (D.n < 2) return;
	rec_whole ();
	delete[] D.lay[D.cur].px;
	for (int k = D.cur; k < D.n - 1; k++) D.lay[k] = D.lay[k + 1];
	D.n--;
	if (D.cur >= D.n) D.cur = D.n - 1;
	compose_all (); refresh (); focus_view ();
}
static void layer_move (int dir)
{
	settle ();
	int to = D.cur + dir;
	if (to < 0 || to >= D.n) return;
	rec_whole ();
	Layer t = D.lay[D.cur]; D.lay[D.cur] = D.lay[to]; D.lay[to] = t;
	D.cur = to;
	compose_all (); refresh (); focus_view ();
}
static void cmd_layer_up () { layer_move (1); }
static void cmd_layer_down () { layer_move (-1); }
static void cmd_layer_merge ()
{
	settle ();
	if (D.cur == 0) return;
	rec_whole ();
	Layer &up = D.lay[D.cur], &lo = D.lay[D.cur - 1];
	if (up.visible) for (int i = 0; i < D.w * D.h; i++) if (up.px[i] >> 24) lo.px[i] = over (lo.px[i], up.px[i], (unsigned) up.opacity);
	delete[] up.px;
	for (int k = D.cur; k < D.n - 1; k++) D.lay[k] = D.lay[k + 1];
	D.n--; D.cur--;
	compose_all (); refresh (); focus_view ();
}
static void cmd_flatten ()
{
	settle ();
	if (D.n < 2) return;
	rec_whole ();
	unsigned *f = flatten ();
	for (int k = 0; k < D.n; k++) delete[] D.lay[k].px;
	D.n = 1; D.cur = 0;
	layer_init (D.lay[0], "Background", f);
	compose_all (); refresh (); focus_view ();
}
// The layer's properties: its name, its opacity, shown or not.
class LayerDialog : public Modal
{
public:
	Textbox *name; Slider *op; Checkbox *vis;
	LayerDialog () : Modal (360, 210)
	{
		Root *r = Root::current (); left = ((r ? r->width : 360) - width) / 2; top = ((r ? r->height : 210) - height) / 2;
		const Layer &l = D.lay[D.cur];
		name = new Textbox (110, 44, 226, 26, l.name); addChild (name);
		op = new Slider (110, 82, 180, 24, 0, 100, (l.opacity * 100 + 127) / 255, onOp, C_FACE); addChild (op);
		vis = new Checkbox (24, 118, 200, 24, "Shown", l.visible, 0, C_FACE); addChild (vis);
		Button *b = new Button (width - 184, height - 42, 82, 28, "OK", act); b->tag = 1; addChild (b);
		b = new Button (width - 94, height - 42, 82, 28, "Cancel", act); b->tag = 0; addChild (b);
	}
	static void act (Widget &w) { ((Modal *) w.parent)->close (w.tag); }
	static void onOp (Widget &w) { w.parent->invalidate (true); }
	void onDraw () override
	{
		drawBox ("Layer Properties");
		canvas.text (24, 50, "Name:", C_TEXT);
		canvas.text (24, 86, "Opacity:", C_TEXT);
		char b[8]; int n = 0, v = op->value; char t[4]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) b[n++] = t[--j]; b[n++] = '%'; b[n] = 0;
		canvas.text (300, 86, b, C_TEXT);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { close (1); return true; } return false; }
};
static void cmd_layer_props ()
{
	settle ();
	LayerDialog d;
	if (d.run () == 1)
	{
		Layer &l = D.lay[D.cur];
		if (d.name->text[0]) scpy (l.name, d.name->text, sizeof l.name);
		l.opacity = d.op->value * 255 / 100; l.visible = d.vis->checked;
		D.changes++;
		compose_all ();
	}
	refresh (); focus_view ();
}
static void layer_cmd (int cmd, int arg, int x, int y)
{
	switch (cmd)
	{
	case L_ADD: cmd_layer_new (); break;
	case L_DUP: cmd_layer_dup (); break;
	case L_DEL: cmd_layer_del (); break;
	case L_UP: cmd_layer_up (); break;
	case L_DOWN: cmd_layer_down (); break;
	case L_MERGE: cmd_layer_merge (); break;
	case L_SELECT: if (arg != D.cur) { settle (); g_sel = norect (); D.cur = arg; compose_all (); } refresh (); focus_view (); break;
	case L_TOGGLE: D.lay[arg].visible = !D.lay[arg].visible; D.changes++; compose_all (); refresh (); focus_view (); break;
	case L_RENAME: if (arg != D.cur) { settle (); D.cur = arg; } cmd_layer_props (); break;
	case L_OPACITY: D.lay[D.cur].opacity = arg * 255 / 100; D.changes++; compose_all (); refresh (); break;
	case L_MENU:
	{
		PopupMenu m (x, y);
		m.add ("New Layer", 1); m.add ("Duplicate", 2); m.add ("Delete", 3, D.n > 1);
		m.separator ();
		m.add ("Move Up", 4, D.cur < D.n - 1); m.add ("Move Down", 5, D.cur > 0); m.add ("Merge Down", 6, D.cur > 0);
		m.separator ();
		m.add (D.lay[D.cur].visible ? "Hide" : "Show", 7); m.add ("Properties...", 8);
		switch (m.run ())
		{
		case 1: cmd_layer_new (); break; case 2: cmd_layer_dup (); break; case 3: cmd_layer_del (); break;
		case 4: cmd_layer_up (); break; case 5: cmd_layer_down (); break; case 6: cmd_layer_merge (); break;
		case 7: layer_cmd (L_TOGGLE, D.cur, 0, 0); break; case 8: cmd_layer_props (); break;
		default: focus_view ();
		}
		break;
	}
	}
}

// ---- colours, sizes, the ribbon ---------------------------------------------------------------------------------
static void add_custom (unsigned c)
{
	c |= 0xFF000000u;
	for (int i = 0; i < 10; i++) if (g_custom[i] == c) return;
	for (int i = 9; i > 0; i--) g_custom[i] = g_custom[i - 1];
	g_custom[0] = c;
}
static void cmd_edit_colours ()
{
	unsigned c = (g_activeCol == 2 ? g_col2 : g_col1) & 0xFFFFFF;
	if (wk_color_dialog (&c, "Edit Colours"))
	{
		if (g_activeCol == 2) g_col2 = c | 0xFF000000u; else g_col1 = c | 0xFF000000u;
		add_custom (c);
	}
	refresh (); focus_view ();
}
static void cmd_swap_colours () { unsigned t = g_col1; g_col1 = g_col2; g_col2 = t; refresh (); focus_view (); }
static void cmd_grid () { g_grid = !g_grid; if (g_grid && g_view->zoom < 300) g_view->setZoom (400); refresh (); focus_view (); }
static void cmd_fit () { g_view->zoomFit (); refresh (); focus_view (); }
static void cmd_zoom_in () { g_view->zoomStep (1); refresh (); focus_view (); }
static void cmd_zoom_out () { g_view->zoomStep (-1); refresh (); focus_view (); }
static void cmd_zoom_100 () { g_view->setZoom (100); refresh (); focus_view (); }

static void size_popup (int x, int y)
{
	PopupMenu m (x, y);
	static const int S[8] = { 1, 2, 3, 5, 8, 12, 20, 32 };
	static const char *const N[8] = { "1 px", "2 px", "3 px", "5 px", "8 px", "12 px", "20 px", "32 px" };
	for (int i = 0; i < 8; i++) m.add (N[i], i + 1, true, g_sizes[g_tool] == S[i] ? "*" : 0);
	int r = m.run ();
	if (r > 0) g_sizes[g_tool == T_SELECT || g_tool == T_FILL || g_tool == T_PICKER || g_tool == T_ZOOM ? T_PENCIL : g_tool] = S[r - 1];
	refresh (); focus_view ();
}
static void rotate_popup (int x, int y)
{
	PopupMenu m (x, y);
	m.add ("Rotate Right 90", 1); m.add ("Rotate Left 90", 2); m.add ("Rotate 180", 3);
	m.separator ();
	m.add ("Flip Vertical", 4); m.add ("Flip Horizontal", 5);
	switch (m.run ()) { case 1: transform (1); break; case 2: transform (-1); break; case 3: transform (2); break; case 4: transform (4); break; case 5: transform (3); break; default: focus_view (); }
}
static void ribbon_cmd (int cmd, int arg, int btn, int x, int y)
{
	switch (cmd)
	{
	case C_PASTE: cmd_paste (); break;
	case C_CUT: cmd_cut (); break;
	case C_COPY: cmd_copy (); break;
	case C_UNDO: cmd_undo (); break;
	case C_REDO: cmd_redo (); break;
	case C_SELECT: set_tool (T_SELECT); break;
	case C_CROP: cmd_crop (); break;
	case C_RESIZE: cmd_resize (); break;
	case C_ROTATE: rotate_popup (x, y); break;
	case C_TOOL: set_tool (arg); break;
	case C_SHAPE: g_shape = arg; set_tool (T_SHAPE); break;
	case C_OUTLINE: g_outline = !g_outline; if (!g_outline && !g_fillShape) g_fillShape = true; refresh (); focus_view (); break;
	case C_FILLSHAPE: g_fillShape = !g_fillShape; if (!g_outline && !g_fillShape) g_outline = true; refresh (); focus_view (); break;
	case C_SIZE: size_popup (x, y); break;
	case C_COL1: g_activeCol = 1; refresh (); focus_view (); break;
	case C_COL2: g_activeCol = 2; refresh (); focus_view (); break;
	case C_PALETTE: case C_CUSTOM:
	{
		unsigned c = cmd == C_PALETTE ? PALETTE[arg] : g_custom[arg];
		if (cmd == C_CUSTOM && !(c >> 24)) { cmd_edit_colours (); break; }
		c |= 0xFF000000u;
		if (btn == 2 || (btn == 1 && g_activeCol == 2)) g_col2 = c; else g_col1 = c;
		refresh (); focus_view ();
		break;
	}
	case C_EDITCOL: cmd_edit_colours (); break;
	case C_GRID: cmd_grid (); break;
	case C_FIT: cmd_fit (); break;
	}
}
static void zoom_cmd (int what)
{
	if (what) { g_view->zoomStep (what); refresh (); focus_view (); return; }
	PopupMenu m (g_status->popX, g_status->top - 8 * 26);
	static const int Z[7] = { 25, 50, 100, 200, 400, 800, 1600 };
	static const char *const N[7] = { "25 %", "50 %", "100 %", "200 %", "400 %", "800 %", "1600 %" };
	m.add ("Fit", 1);
	for (int i = 0; i < 7; i++) m.add (N[i], i + 2);
	int r = m.run ();
	if (r == 1) g_view->zoomFit (); else if (r > 1) g_view->setZoom (Z[r - 2]);
	refresh (); focus_view ();
}
static void tool_changed () { refresh (); }

// ---- the window --------------------------------------------------------------------------------------------------
class PaintRoot : public Root
{
public:
	PaintRoot () : Root (W, H, "Paint") {}
	bool onKey (long k) override
	{
		if (kapi_get_modifiers () & MOD_CTRL) return false;
		switch (k)			// (the tools' letters)
		{
		case 's': case 'S': set_tool (T_SELECT); return true;
		case 'p': case 'P': set_tool (T_PENCIL); return true;
		case 'b': case 'B': set_tool (T_BRUSH); return true;
		case 'e': case 'E': set_tool (T_ERASER); return true;
		case 'f': case 'F': set_tool (T_FILL); return true;
		case 'k': case 'K': set_tool (T_PICKER); return true;
		case 'z': case 'Z': set_tool (T_ZOOM); return true;
		case 'u': case 'U': set_tool (T_SHAPE); return true;
		case 'x': case 'X': cmd_swap_colours (); return true;
		case '+': cmd_zoom_in (); return true;
		case '-': cmd_zoom_out (); return true;
		}
		return false;
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		char path[200];
		if (type != DND_FILES || !doc_first_path (data, path, sizeof path)) return;
		if (!guard ()) return;
		open_path (path);
	}
};

int main (void)
{
	PaintRoot root;
	root.attach ();				// (a question asked before run (): its clicks and keys)
	wtk::init ();
	doc_new (640, 480, true);			// (whole at 100 % in the window)
	compose_all ();

	g_ribbon = new Ribbon (0, 0, W);
	g_view = new CanvasView (0, RIBBON_H, W - PANEL_W, H - RIBBON_H - STATUS_H);
	g_layers = new LayersPanel (W - PANEL_W, RIBBON_H, H - RIBBON_H - STATUS_H);
	g_status = new StatusBar (0, H - STATUS_H, W, g_view);
	root.addChild (g_ribbon); root.addChild (g_layers); root.addChild (g_status);
	root.addChild (g_view);			// (last: on top -- a drag ended over another part still reaches it)
	g_ribbon->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_view->anchor = ANCHOR_FILL;
	g_layers->anchor = ANCHOR_TOP | ANCHOR_BOTTOM | ANCHOR_RIGHT;
	g_status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.setResizable (true);
	g_changed = refresh; g_toolChanged = tool_changed;
	g_ribbonCmd = ribbon_cmd; g_layerCmd = layer_cmd; g_status->zoomCmd = zoom_cmd;

	static Menu menu;
	menu.menu ("File");
	menu.item ("New...", "^N", WK_CTRL ('N'), cmd_new);
	menu.item ("Open...", "^O", WK_CTRL ('O'), cmd_open);
	menu.separator ();
	menu.item ("Save", "^S", WK_CTRL ('S'), cmd_save);
	menu.item ("Save As...", "", 0, cmd_save_as);
	menu.separator ();
	menu.item ("Export as PNG...", "^E", WK_CTRL ('E'), cmd_export_png);
	menu.item ("Export as JPEG...", "", 0, cmd_export_jpg);
	menu.item ("Export as BMP...", "", 0, cmd_export_bmp);
	menu.item ("Export as GIF...", "", 0, cmd_export_gif);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", WK_CTRL ('Z'), cmd_undo);
	menu.item ("Redo", "^Y", WK_CTRL ('Y'), cmd_redo);
	menu.separator ();
	menu.item ("Cut", "^X", WK_CTRL ('X'), cmd_cut);
	menu.item ("Copy", "^C", WK_CTRL ('C'), cmd_copy);
	menu.item ("Paste", "^V", WK_CTRL ('V'), cmd_paste);
	menu.item ("Delete", "Del", 0, cmd_delete);
	menu.separator ();
	menu.item ("Select All", "^A", WK_CTRL ('A'), cmd_select_all);
	menu.item ("Deselect", "Esc", 0, cmd_deselect);
	menu.item ("Crop to Selection", "", 0, cmd_crop);
	menu.menu ("Image");
	menu.item ("Rotate Right 90", "", 0, cmd_rot_right);
	menu.item ("Rotate Left 90", "", 0, cmd_rot_left);
	menu.item ("Rotate 180", "", 0, cmd_rot_half);
	menu.item ("Flip Horizontal", "", 0, cmd_flip_h);
	menu.item ("Flip Vertical", "", 0, cmd_flip_v);
	menu.separator ();
	menu.item ("Resize...", "^W", WK_CTRL ('W'), cmd_resize);
	menu.menu ("Layers");
	menu.item ("New Layer", "^L", WK_CTRL ('L'), cmd_layer_new);
	menu.item ("Duplicate Layer", "", 0, cmd_layer_dup);
	menu.item ("Delete Layer", "", 0, cmd_layer_del);
	menu.separator ();
	menu.item ("Move Up", "", 0, cmd_layer_up);
	menu.item ("Move Down", "", 0, cmd_layer_down);
	menu.item ("Merge Down", "", 0, cmd_layer_merge);
	menu.item ("Flatten", "", 0, cmd_flatten);
	menu.separator ();
	menu.item ("Layer Properties...", "", 0, cmd_layer_props);
	menu.menu ("View");
	menu.item ("Zoom In", "+", 0, cmd_zoom_in);
	menu.item ("Zoom Out", "-", 0, cmd_zoom_out);
	menu.item ("Actual Size (100%)", "", 0, cmd_zoom_100);
	menu.item ("Fit the Window", "", 0, cmd_fit);
	menu.separator ();
	menu.item ("Grid", "^G", WK_CTRL ('G'), cmd_grid);
	menu.menu ("Colours");
	menu.item ("Edit Colours...", "", 0, cmd_edit_colours);
	menu.item ("Swap Colours", "X", 0, cmd_swap_colours);
	menu.publish ();

	// A picture named on the command line, else the one kept at the last close.
	char args[200];
	int an = kapi_get_args (args, sizeof args);
	bool opened = an > 0 && args[0] && open_path (args);
	if (!opened)
	{
		void *f = kapi_open (RECOVER);
		if (f)
		{
			kapi_close (f);
			if (wk_messagebox ("Paint", "Paint was closed with unsaved changes. Open the recovered picture?", MB_YESNO) == 1 && doc_open (RECOVER))
			{
				g_path[0] = 0;
				unsigned n; unsigned char *b = read_all (RECOVER_NAME, &n);
				if (b) { int k = (int) n < (int) sizeof g_path - 1 ? (int) n : (int) sizeof g_path - 1; for (int i = 0; i < k; i++) g_path[i] = (char) b[i]; g_path[k] = 0; delete[] b; }
				doc_loaded ();
				g_saved = D.changes + 1;
				opened = true;
			}
			kapi_remove (RECOVER); kapi_remove (RECOVER_NAME);
		}
	}
	if (!opened) doc_loaded ();
	g_view->setFocus ();
	root.run ();

	settle ();
	if (changed_doc ())
	{
		unsigned n; unsigned char *b = ora_save (&n);
		kapi_save_file (RECOVER, b, n);
		delete[] b;
		if (g_path[0]) kapi_save_file (RECOVER_NAME, g_path, (unsigned) slen (g_path));
	}
	return 0;
}
