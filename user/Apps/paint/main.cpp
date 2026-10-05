//
// paint -- Onyx's Paint: a picture of transparent layers, drawn the way the desktop's other apps look
// (docs/paint: the mock-ups the user chose). The ribbon (Clipboard -- Image: select, crop, resize,
// rotate -- Tools: pencil, fill, text, eraser, colour picker, magnifier, gradient, adjust -- Brushes --
// Shapes -- Colours), the options bar of the current tool, the canvas (composited by the GPU: the
// layers as textures with their blend modes; 12 % - 3200 %), the layers' panel, the status bar.
//
// Pieces: pdoc.h (the layers, their blend modes, undo by tiles), psel.h (the selection: rectangle,
// lasso, magic wand), raster.h (the shapes), pbrush.h (the brushes), pgrad.h (the gradients, GIMP's
// .ggr), ptext.h (the Text tool: FreeType), padjust.h (the adjustments), pview.h (the canvas, its
// tools, gpucomp), picons.h / pui.h (the icons, the ribbon, the options, the layers, the status bar),
// pfile.h (OpenRaster, the pictures, the exports).
//
// Files: File > Save writes OpenRaster (.ora: the layers, their blend modes -- GIMP and Krita open it);
// Open reads it, or a PNG, JPEG, BMP, GIF (WebP, PCX) picture; File > Export writes what is shown,
// flattened, as PNG, JPEG, BMP or GIF. A picture named on the command line, or dropped on the window,
// is opened. Closed with unsaved changes, the picture is kept in SD:/apps/paint.app/recovered.ora and
// offered back at the next start. Your gradients: SD:/apps/paint.app/gradients/*.ggr.
//
// MIT licence (Onyx).
//
#include <math.h>
#include "printerkit/printerkit.h"
#include "kapi.h"
#include "uikit/uikit.h"
#include "ft/uikitface.h"
#include "clipboard.h"
#include "docguard.h"
#include "pui.h"
#include "padjust.h"
#include "pfile.h"

using namespace uikit;
using namespace pd;

static int W = 1100, H = 740;			// (the window: as much as the screen takes, at most this)
static const char *RECOVER = "SD:/apps/paint.app/recovered.ora";
static const char *RECOVER_NAME = "SD:/apps/paint.app/recovered.txt";

static CanvasView *g_view;
static Ribbon *g_ribbon;
static OptionsBar *g_opts;
static LayersPanel *g_layers;
static StatusBar *g_status;
static char g_path[200];			// "" : not saved yet (a picture opened: its path, Save asks for an .ora)
static unsigned g_saved;
static unsigned *g_clip; static int g_clipW, g_clipH;	// the copied pixels

static bool changed_doc () { return D.changes != g_saved; }
static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1; return b; }
static void focus_view () { if (g_view) g_view->setFocus (); }
static void centre (Modal *m) { Root *r = Root::current (); m->left = ((r ? r->width : m->width) - m->width) / 2; m->top = ((r ? r->height : m->height) - m->height) / 2; }
static void act_close (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->close (w.tag); }
static void ok_cancel (Modal *m, int okTag = 1)
{
	Button *b = new Button (m->width - 196, m->height - 44, 86, 30, "OK", act_close); b->tag = okTag; m->addChild (b);
	b = new Button (m->width - 102, m->height - 44, 86, 30, "Cancel", act_close); b->tag = 0; m->addChild (b);
}

static void refresh ()
{
	if (!g_view) return;
	g_view->invalidate (true);
	g_ribbon->invalidate (true);
	g_opts->invalidate (true);
	g_layers->sync ();
	g_status->invalidate (true);
}

// ---- files ------------------------------------------------------------------------------------------------
static void doc_loaded ()
{
	sel_clear ();
	g_saved = D.changes;
	g_view->sx = g_view->sy = 0;
	g_view->zoomFit ();
	refresh ();
}
static bool open_path (const char *path)
{
	if (!doc_open (path)) { uk_messagebox ("Open", "That file is not a picture Paint can read (PNG, JPEG, BMP, GIF, WebP, PCX, OpenRaster).", MB_OK); return false; }
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
	if (ok) g_saved = D.changes; else uk_messagebox ("Save", "The file could not be written.", MB_OK);
	refresh (); focus_view ();
}
static void cmd_save_as ()
{
	settle ();
	char def[80], path[200];
	with_ext (def, sizeof def, g_path[0] ? base_name (g_path) : "Untitled", ".ora");
	if (uk_file_save (path, sizeof path, "SD:/", def))
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
	if (uk_file_save (path, sizeof path, "SD:/", def))
	{
		if (!ends_with (path, ".png") && !ends_with (path, ".jpg") && !ends_with (path, ".jpeg") && !ends_with (path, ".bmp") && !ends_with (path, ".gif"))
		{ int k = slen (path); scpy (path + k, ext, (int) sizeof path - k); }
		unsigned n; unsigned char *b = export_bytes (path, &n);
		if (kapi_save_file (path, b, n) < 0) uk_messagebox ("Export", "The file could not be written.", MB_OK);
		delete[] b;
	}
	focus_view ();
}
// File > Print: the Print dialog (printerkit/printerkit.h: the printer, the paper...), then the picture -- as the
// screen shows it, its layers flattened -- on one page: at its size (96 pixels an inch), made smaller if
// it does not fit in what the printer prints, centred.
static void cmd_print ()
{
	settle ();
	const char *name = g_path[0] ? base_name (g_path) : "Untitled";
	PrintSetup ps; print_setup_default (&ps);
	if (D.w > D.h) print_setup_paper (&ps, 0, PRINT_LANDSCAPE);
	PrintDialogInfo di = { sizeof di, name, 1, 0, 0, 0, 0 };
	if (print_dialog (&ps, &di))
	{
		PrintJob *j = print_begin (&ps, name);
		if (j && print_page (j, 0, 0))
		{
			float l = ps.margin_l > 18 ? ps.margin_l : 18, t = ps.margin_t > 18 ? ps.margin_t : 18;
			float r = ps.margin_r > 18 ? ps.margin_r : 18, b = ps.margin_b > 18 ? ps.margin_b : 18;
			float aw = ps.paper_w - l - r, ah = ps.paper_h - t - b, w = D.w * 0.75f, h = D.h * 0.75f;
			float k = aw / w < ah / h ? aw / w : ah / h;
			if (k < 1) { w *= k; h *= k; }
			unsigned *flat = flatten ();
			print_image (j, flat, D.w, D.h, l + (aw - w) / 2, t + (ah - h) / 2, w, h, PRINT_IMG_ALPHA);
			delete[] flat;
		}
		if (!j || print_end (j) < 0) uk_messagebox ("Print", "The picture could not be put in the print queue.", MB_OK);
	}
	focus_view ();
}
static void cmd_export_png () { export_as (".png"); }
static void cmd_export_jpg () { export_as (".jpg"); }
static void cmd_export_bmp () { export_as (".bmp"); }
static void cmd_export_gif () { export_as (".gif"); }

static void fmt (char *b, int v) { fmt_int (b, v, 0); }

// ---- the New dialog -------------------------------------------------------------------------------------------
class NewDialog : public Modal
{
public:
	Textbox *tw, *th; RadioButton *white, *clear;
	NewDialog () : Modal (360, 236)
	{
		centre (this);
		int y = titleH () + 18;
		tw = new Textbox (120, y, 100, 28, "800"); tw->maxLen = 5; addChild (tw);
		th = new Textbox (120, y + 38, 100, 28, "560"); th->maxLen = 5; addChild (th);
		white = new RadioButton (24, y + 84, 120, 24, "White", 1, true, 0, C_FACE); addChild (white);
		clear = new RadioButton (160, y + 84, 170, 24, "Transparent", 1, false, 0, C_FACE); addChild (clear);
		ok_cancel (this);
	}
	void onDraw () override
	{
		drawBox ("New Picture");
		int y = titleH () + 18;
		uk_text_l (canvas, 24, y, 28, "Width", C_TEXT); uk_text_l (canvas, 230, y, 28, "px", uk_mix (C_FACE, C_TEXT, 150));
		uk_text_l (canvas, 24, y + 38, 28, "Height", C_TEXT); uk_text_l (canvas, 230, y + 38, 28, "px", uk_mix (C_FACE, C_TEXT, 150));
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
		undo_clear ();
		g_path[0] = 0;
		doc_loaded ();
	}
	focus_view ();
}
static void cmd_open ()
{
	if (!guard ()) { focus_view (); return; }
	char path[200];
	if (uk_file_open (path, sizeof path, "SD:/")) open_path (path);
	focus_view ();
}

// ---- edits ----------------------------------------------------------------------------------------------------
static void cmd_undo () { settle (); if (undo ()) sel_clear (); refresh (); focus_view (); }
static void cmd_redo () { settle (); if (redo ()) sel_clear (); refresh (); focus_view (); }
static void set_tool (int t)
{
	if (t != T_SELECT) float_commit (false);
	if (t != g_tool) { grad_end (true); text_end (true); }
	if (t == T_PICKER && g_tool != T_PICKER) g_prevTool = g_tool;
	g_tool = t;
	refresh (); focus_view ();
}
static void set_select (int kind) { g_selKind = kind; set_tool (T_SELECT); }
// The selection's pixels (the floating ones, or the layer's inside the selection) -> g_clip.
static void copy_sel ()
{
	if (D.fl.px)
	{
		clip_clear ();
		delete[] g_clip; g_clipW = D.fl.w; g_clipH = D.fl.h; g_clip = new unsigned[(unsigned) g_clipW * g_clipH];
		for (int i = 0; i < g_clipW * g_clipH; i++) g_clip[i] = D.fl.px[i];
		return;
	}
	Rect r = g_sel; r.clip (D.w, D.h);
	if (r.empty ()) return;
	clip_clear ();
	delete[] g_clip; g_clipW = r.x1 - r.x0; g_clipH = r.y1 - r.y0; g_clip = new unsigned[(unsigned) g_clipW * g_clipH];
	for (int y = 0; y < g_clipH; y++)
		for (int x = 0; x < g_clipW; x++)
		{
			unsigned c = D.lay[D.cur].px[(unsigned) (r.y0 + y) * D.w + r.x0 + x]; int m = sel_at (r.x0 + x, r.y0 + y);
			g_clip[y * g_clipW + x] = m == 255 ? c : (c & 0xFFFFFF) | ((c >> 24) * (unsigned) m / 255) << 24;
		}
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
	settle ();
	int x = pclamp (g_view->imX (0), 0, pmax (0, D.w - 1)), y = pclamp (g_view->imY (0), 0, pmax (0, D.h - 1));
	float_new (px, w, h, x, y);
	g_tool = T_SELECT;
	refresh (); focus_view ();
}
// Pixels (their own now) as a new layer above the current one, floating (moved, then put down by a
// click outside), centred on what the view shows.
static void layer_from_pixels (const char *name, unsigned *px, int w, int h)
{
	settle ();
	if (D.n >= MAXLAYERS) { delete[] px; uk_messagebox ("Layers", "The picture has as many layers as it can take.", MB_OK); return; }
	rec_whole ();
	for (int k = D.n; k > D.cur + 1; k--) D.lay[k] = D.lay[k - 1];
	layer_init (D.lay[D.cur + 1], name, new_px (D.w, D.h, 0));
	D.n++; D.cur++;
	compose_all ();
	int cx = g_view->imX (g_view->viewW () / 2), cy = g_view->imY (g_view->viewH () / 2);
	cx = pclamp (cx, 0, D.w); cy = pclamp (cy, 0, D.h);
	float_new (px, w, h, w >= D.w ? (D.w - w) / 2 : cx - w / 2, h >= D.h ? (D.h - h) / 2 : cy - h / 2);
	g_tool = T_SELECT;
}
static void cmd_paste_layer ()
{
	unsigned *px = 0; int w = 0, h = 0;
	char path[200]; int cut = 0;
	if (clip_get_file (path, sizeof path, &cut) && img_is_image_name (path))
	{
		ImgFrames im;
		if (img_load (path, &im)) { for (int i = 1; i < im.n; i++) delete[] im.px[i]; px = im.px[0]; w = im.w; h = im.h; }
	}
	if (!px && g_clip) { w = g_clipW; h = g_clipH; px = new unsigned[(unsigned) w * h]; for (int i = 0; i < w * h; i++) px[i] = g_clip[i]; }
	if (px) layer_from_pixels ("Pasted", px, w, h);
	refresh (); focus_view ();
}
// A picture file opened as a new layer (its name the file's).
static void cmd_open_layer ()
{
	char path[200];
	if (uk_file_open (path, sizeof path, "SD:/"))
	{
		ImgFrames im;
		if (!img_load (path, &im)) uk_messagebox ("Open as Layer", "That file is not a picture Paint can read (PNG, JPEG, BMP, GIF, WebP, PCX).", MB_OK);
		else
		{
			for (int i = 1; i < im.n; i++) delete[] im.px[i];
			char nm[32]; scpy (nm, base_name (path), sizeof nm);
			int n = slen (nm), dot = n; while (dot > 0 && nm[dot - 1] != '.') dot--;
			if (dot > 1) nm[dot - 1] = 0;
			layer_from_pixels (nm, im.px[0], im.w, im.h);
		}
	}
	refresh (); focus_view ();
}
static void cmd_delete () { g_view->deleteSelection (); refresh (); focus_view (); }
static void cmd_select_all () { settle (); g_tool = T_SELECT; sel_rect (mkrect (0, 0, D.w, D.h)); refresh (); focus_view (); }
static void cmd_deselect () { settle (); sel_clear (); refresh (); focus_view (); }
static void cmd_invert_sel () { settle (); sel_invert (); refresh (); focus_view (); }

// ---- the picture's changes ------------------------------------------------------------------------------------
static void new_size (int w, int h)
{
	D.w = w; D.h = h;
	delete[] D.ov.px; D.ov.px = new_px (w, h, 0); D.ov.r = norect ();
	sel_clear ();
	compose_all ();
}
static void cmd_crop ()
{
	float_commit (true);
	Rect r = g_sel; r.clip (D.w, D.h);
	if (r.empty ()) { uk_messagebox ("Crop", "Select the part to keep first (the Select tool).", MB_OK); focus_view (); return; }
	settle ();
	rec_whole ();
	int w = r.x1 - r.x0, h = r.y1 - r.y0;
	for (int k = 0; k < D.n; k++)
	{
		unsigned *n = new unsigned[(unsigned) w * h];
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) n[y * w + x] = D.lay[k].px[(unsigned) (r.y0 + y) * D.w + r.x0 + x];
		delete[] D.lay[k].px; D.lay[k].px = n;
	}
	new_size (w, h);
	g_view->zoomFit (); refresh (); focus_view ();
}
// Rotate (1 right, -1 left, 2 half a turn) or flip (3 horizontal, 4 vertical): the selection, or the picture.
static void transform (int op)
{
	grad_end (true); text_end (true);
	if (has_sel () || D.fl.px)
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

// The Resize dialog: the picture scaled (by pixels or a percentage), or its canvas made bigger / smaller.
// Tab goes from one field to the next.
class ResizeDialog : public Modal
{
public:
	SegmentedControl *mode; RadioButton *byPct, *byPx;
	Textbox *tw, *th; Checkbox *keep; Dropdown *resample, *anchor;
	int w0, h0; bool busy;
	ResizeDialog () : Modal (430, 380), w0 (D.w), h0 (D.h), busy (false)
	{
		centre (this);
		static const char *const MODES[2] = { "Resize the picture", "Canvas size" };
		static const char *const RES[2] = { "Sharp (the nearest pixel)", "Smooth (averaged)" };
		static const char *const ANCH[3] = { "Top left", "Centre", "Bottom right" };
		int y = titleH () + 14;
		mode = new SegmentedControl (22, y, 300, 30, MODES, 2, 0, onMode); addChild (mode);
		y += 46;
		byPct = new RadioButton (22, y, 170, 24, "By percentage", 1, false, onUnit, C_FACE); addChild (byPct);
		byPx = new RadioButton (200, y, 170, 24, "By pixels", 1, true, onUnit, C_FACE); addChild (byPx);
		y += 40;
		char b[16];
		fmt (b, D.w); tw = new Textbox (112, y, 100, 28, b); tw->changed = onW; tw->maxLen = 5; addChild (tw);
		fmt (b, D.h); th = new Textbox (112, y + 38, 100, 28, b); th->changed = onH; th->maxLen = 5; addChild (th);
		keep = new Checkbox (22, y + 80, 260, 24, "Keep the proportions", true, 0, C_FACE); addChild (keep);
		resample = new Dropdown (130, y + 116, 250, 28, RES, 2, 1, 0); addChild (resample);
		anchor = new Dropdown (130, y + 116, 250, 28, ANCH, 3, 1, 0); anchor->hidden = true; addChild (anchor);
		ok_cancel (this);
	}
	bool pct () const { return byPct->checked; }
	static ResizeDialog *of (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; return (ResizeDialog *) p; }
	static void onMode (Widget &w)
	{
		ResizeDialog *d = of (w);
		bool canvasMode = d->mode->selected == 1;
		d->resample->hidden = canvasMode; d->anchor->hidden = !canvasMode;
		if (canvasMode && d->pct ()) { d->byPx->select (); }
		d->invalidate (true);
	}
	static void onUnit (Widget &w)
	{
		ResizeDialog *d = of (w);
		char b[16];
		d->busy = true;
		if (d->pct ()) { d->tw->setText ("100"); d->th->setText ("100"); }
		else { fmt (b, d->w0); d->tw->setText (b); fmt (b, d->h0); d->th->setText (b); }
		d->busy = false;
		d->invalidate (true);
	}
	static void onW (Widget &w)
	{
		ResizeDialog *d = of (w);
		if (d->busy) return;
		if (!d->keep->checked) { d->invalidate (true); return; }
		char b[16]; int v = parse_int (d->tw->text);
		fmt (b, d->pct () ? v : pmax (1, (int) ((long long) v * d->h0 / pmax (1, d->w0))));
		d->busy = true; d->th->setText (b); d->busy = false;
		d->invalidate (true);
	}
	static void onH (Widget &w)
	{
		ResizeDialog *d = of (w);
		if (d->busy) return;
		if (!d->keep->checked) { d->invalidate (true); return; }
		char b[16]; int v = parse_int (d->th->text);
		fmt (b, d->pct () ? v : pmax (1, (int) ((long long) v * d->w0 / pmax (1, d->h0))));
		d->busy = true; d->tw->setText (b); d->busy = false;
		d->invalidate (true);
	}
	int outW () const { int v = parse_int (tw->text); return pclamp (pct () ? (int) ((long long) w0 * v / 100) : v, 1, 8192); }
	int outH () const { int v = parse_int (th->text); return pclamp (pct () ? (int) ((long long) h0 * v / 100) : v, 1, 8192); }
	void onDraw () override
	{
		drawBox ("Resize");
		int y = titleH () + 100;
		unsigned dim = uk_mix (C_FACE, C_TEXT, 150);
		uk_text_l (canvas, 22, y, 28, "Width", C_TEXT); uk_text_l (canvas, 222, y, 28, pct () ? "%" : "px", dim);
		uk_text_l (canvas, 22, y + 38, 28, "Height", C_TEXT); uk_text_l (canvas, 222, y + 38, 28, pct () ? "%" : "px", dim);
		char b[64]; fmt_int (b, outW (), " x "); int n = slen (b); fmt_int (b + n, outH (), " px"); uk_text_l (canvas, 262, y + 18, 28, b, dim);
		uk_text_l (canvas, 22, y + 116, 28, mode->selected == 1 ? "Anchor" : "Resampling", C_TEXT);
		uk_text_l (canvas, 22, height - 44, 30, "Tab: the next field", dim);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { close (1); return true; } return false; }
};
static void cmd_resize ()
{
	settle ();
	ResizeDialog d;
	if (d.run () != 1) { focus_view (); return; }
	int w = d.outW (), h = d.outH ();
	if (w == D.w && h == D.h) { focus_view (); return; }
	rec_whole ();
	bool canvasMode = d.mode->selected == 1;
	for (int k = 0; k < D.n; k++)
	{
		unsigned *p = D.lay[k].px, *n;
		if (!canvasMode) n = scale (p, D.w, D.h, w, h, d.resample->sel == 1);
		else
		{
			n = new_px (w, h, k == 0 && (p[0] >> 24) == 255 ? 0xFFFFFFFFu : 0);	// (an opaque background: grown white)
			int a = d.anchor->sel;
			int ox = a == 1 ? (w - D.w) / 2 : a == 2 ? w - D.w : 0, oy = a == 1 ? (h - D.h) / 2 : a == 2 ? h - D.h : 0;
			for (int y = 0; y < D.h; y++) for (int x = 0; x < D.w; x++)
			{
				int X = x + ox, Y = y + oy;
				if (X >= 0 && Y >= 0 && X < w && Y < h) n[(unsigned) Y * w + X] = p[(unsigned) y * D.w + x];
			}
		}
		delete[] p; D.lay[k].px = n;
	}
	new_size (w, h);
	g_view->zoomFit (); refresh (); focus_view ();
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
		char b[32] = "Layer "; fmt_int (b + 6, i, 0);
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
	const Layer s = D.lay[D.cur];
	unsigned *px = new unsigned[(unsigned) D.w * D.h];
	for (int i = 0; i < D.w * D.h; i++) px[i] = s.px[i];
	char nm[32]; scpy (nm, s.name, 26); int n = slen (nm); scpy (nm + n, " copy", 32 - n);
	layer_insert (D.cur + 1, nm, px);
	D.cur++; D.lay[D.cur].visible = s.visible; D.lay[D.cur].opacity = s.opacity; D.lay[D.cur].blend = s.blend; D.lay[D.cur].clip = s.clip;
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
// The layer merged into the one below, with its blend mode and opacity (a clip mask: the layer below
// keeps only what the mask lets through).
static void cmd_layer_merge ()
{
	settle ();
	if (D.cur == 0) return;
	rec_whole ();
	Layer &up = D.lay[D.cur], &lo = D.lay[D.cur - 1];
	if (up.visible)
		for (int y = 0; y < D.h; y++)
			for (int x = 0; x < D.w; x++)
			{
				unsigned i = (unsigned) y * D.w + x;
				if (is_clip_mask (D.cur)) { lo.px[i] = clipped_px (D.cur - 1, lo.px[i], x, y, false); continue; }
				unsigned s = premul (up.px[i]);
				if (up.blend == GPC_B_MASK) { lo.px[i] = unpremul (gpc_blend_pixel (s, premul (lo.px[i]), GPC_B_MASK, 1)); continue; }
				if (up.opacity < 255) { unsigned o = (unsigned) up.opacity; s = ((((s >> 24) * o + 127) / 255) << 24) | (((((s >> 16) & 255) * o + 127) / 255) << 16) | (((((s >> 8) & 255) * o + 127) / 255) << 8) | (((s & 255) * o + 127) / 255); }
				if (!s) continue;
				lo.px[i] = unpremul (gpc_blend_pixel (s, premul (lo.px[i]), (unsigned) up.blend, 1));
			}
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
// A layer's properties changed (no pixel): shown again (a clip mask's: the layer under it sent again).
static void props_changed () { D.changes++; compose_all (); refresh (); }

// The layer's properties: its name, its blend mode (a mask on the layer below only), its opacity, shown.
class LayerDialog : public Modal
{
public:
	Textbox *name; Dropdown *blend; Checkbox *clip, *vis; Slider *op;
	LayerDialog () : Modal (420, 300)
	{
		centre (this);
		const Layer &l = D.lay[D.cur];
		int y = titleH () + 16;
		name = new Textbox (120, y, 276, 28, l.name); addChild (name);
		blend = new Dropdown (120, y + 40, 276, 28, BLEND_NAMES, NBLENDS, l.blend, onBlend); addChild (blend);
		clip = new Checkbox (120, y + 76, 280, 24, "On the layer below only", l.clip, 0, C_FACE); addChild (clip);
		op = new Slider (120, y + 112, 220, 24, 0, 100, (l.opacity * 100 + 127) / 255, onOp, C_FACE); addChild (op);
		vis = new Checkbox (120, y + 148, 200, 24, "Shown", l.visible, 0, C_FACE); addChild (vis);
		clip->disabled = !(l.blend == GPC_B_MASK || l.blend == GPC_B_CUTOUT) || D.cur == 0;
		ok_cancel (this);
	}
	static void onOp (Widget &w) { w.parent->invalidate (true); }
	static void onBlend (Widget &w)
	{
		LayerDialog *d = (LayerDialog *) w.parent;
		d->clip->disabled = !(d->blend->sel == GPC_B_MASK || d->blend->sel == GPC_B_CUTOUT) || D.cur == 0;
		d->invalidate (true);
	}
	void onDraw () override
	{
		drawBox ("Layer Properties");
		int y = titleH () + 16;
		uk_text_l (canvas, 22, y, 28, "Name", C_TEXT);
		uk_text_l (canvas, 22, y + 40, 28, "Blend", C_TEXT);
		uk_text_l (canvas, 22, y + 112, 24, "Opacity", C_TEXT);
		char b[8]; fmt_int (b, op->value, " %"); uk_text_l (canvas, 350, y + 112, 24, b, C_TEXT);
		const char *hint = blend->sel == GPC_B_MASK ? "Keeps what is under it where it is opaque." : blend->sel == GPC_B_CUTOUT ? "Removes what is under it where it is opaque." : "";
		uk_text_l (canvas, 22, height - 44, 30, hint, uk_mix (C_FACE, C_TEXT, 150));
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
		l.opacity = d.op->value * 255 / 100; l.visible = d.vis->checked; l.blend = d.blend->sel; l.clip = d.clip->checked && !d.clip->disabled;
		props_changed ();
	}
	refresh (); focus_view ();
}
static void blend_popup (int x, int y)
{
	Layer &l = D.lay[D.cur];
	PopupMenu m (x, y);
	for (int b = 0; b < NBLENDS; b++) { if (b == GPC_B_MASK) m.separator (); m.add (BLEND_NAMES[b], b + 1, true, l.blend == b ? "*" : 0); }
	m.separator ();
	m.add ("On the layer below only", 100, (l.blend == GPC_B_MASK || l.blend == GPC_B_CUTOUT) && D.cur > 0, l.clip ? "*" : 0);
	int r = m.run ();
	if (r >= 1 && r <= NBLENDS) { l.blend = r - 1; props_changed (); }
	else if (r == 100) { l.clip = !l.clip; props_changed (); }
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
	case L_PROPS: cmd_layer_props (); break;
	case L_SELECT: if (arg != D.cur) { settle (); D.cur = arg; compose_all (); } refresh (); focus_view (); break;
	case L_TOGGLE: D.lay[arg].visible = !D.lay[arg].visible; props_changed (); focus_view (); break;
	case L_RENAME: if (arg != D.cur) { settle (); D.cur = arg; } cmd_layer_props (); break;
	case L_OPACITY:
		if (D.lay[D.cur].opacity != arg * 255 / 100)
		{
			D.lay[D.cur].opacity = arg * 255 / 100; D.changes++;
			if (is_clip_mask (D.cur)) compose_all ();
			refresh ();
		}
		break;
	case L_BLEND: blend_popup (x, y); break;
	case L_MENU:
	{
		PopupMenu m (x, y);
		m.add ("New Layer", 1); m.add ("New Layer from a File...", 9); m.add ("Paste as New Layer", 10); m.add ("Duplicate", 2); m.add ("Delete", 3, D.n > 1);
		m.separator ();
		m.add ("Move Up", 4, D.cur < D.n - 1); m.add ("Move Down", 5, D.cur > 0); m.add ("Merge Down", 6, D.cur > 0);
		m.separator ();
		m.add (D.lay[D.cur].visible ? "Hide" : "Show", 7); m.add ("Properties...", 8);
		switch (m.run ())
		{
		case 1: cmd_layer_new (); break; case 2: cmd_layer_dup (); break; case 3: cmd_layer_del (); break;
		case 4: cmd_layer_up (); break; case 5: cmd_layer_down (); break; case 6: cmd_layer_merge (); break;
		case 7: layer_cmd (L_TOGGLE, D.cur, 0, 0); break; case 8: cmd_layer_props (); break;
		case 9: cmd_open_layer (); break; case 10: cmd_paste_layer (); break;
		default: focus_view ();
		}
		break;
	}
	}
}

// ---- the adjustments --------------------------------------------------------------------------------------------
class AdjustDialog : public Modal
{
public:
	struct Spec { const char *label; int lo, hi, def; const char *suffix; };
	int kind, n; Slider *s[3]; Dropdown *ch[3]; Dropdown *scope; int sc;
	static int rows (int k) { int n = 0; while (n < 3 && spec (k, n).label) n++; return n; }
	static Spec spec (int k, int i)
	{
		static const Spec S[AJ_COUNT][3] = {
			{ { "Brightness", -100, 100, 0, "" }, { "Contrast", -100, 100, 0, "" }, { 0, 0, 0, 0, 0 } },
			{ { "Hue", -180, 180, 0, " deg" }, { "Saturation", -100, 100, 0, "" }, { "Lightness", -100, 100, 0, "" } },
			{ { "Amount", 0, 100, 100, " %" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
			{ { "Hue", 0, 360, 30, " deg" }, { "Saturation", 0, 100, 50, " %" }, { "Lightness", -100, 100, 0, "" } },
			{ { "Red from", 0, 4, 0, 0 }, { "Green from", 0, 4, 1, 0 }, { "Blue from", 0, 4, 2, 0 } },
			{ { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
			{ { "Amount", 0, 100, 100, " %" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
			{ { "Levels", 2, 16, 4, "" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
			{ { "Level", 0, 255, 128, "" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
			{ { "Radius", 1, 20, 2, " px" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
			{ { "Amount", 0, 300, 80, " %" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
			{ { "Block", 2, 64, 8, " px" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } } };
		return S[k][i];
	}
	AdjustDialog (int k) : Modal (460, 0), kind (k), n (rows (k)), sc (has_sel () ? SC_SELECTION : SC_LAYER)
	{
		resizeTo (width, titleH () + 18 + n * 44 + 44 + (k == AJ_REMAP || k == AJ_INVERT ? 28 : 0) + 66);
		centre (this);
		int y = titleH () + 18;
		for (int i = 0; i < 3; i++) { s[i] = 0; ch[i] = 0; }
		for (int i = 0; i < n; i++)
		{
			Spec p = spec (k, i);
			if (k == AJ_REMAP) { ch[i] = new Dropdown (140, y + i * 44, 200, 28, CHANNEL_NAMES, 5, p.def, onChange); addChild (ch[i]); }
			else { s[i] = new Slider (140, y + i * 44, 220, 26, p.lo, p.hi, p.def, onChange, C_FACE); addChild (s[i]); }
		}
		scope = new Dropdown (140, y + n * 44 + 4, 200, 28, SCOPE_NAMES, 3, sc, onScope); addChild (scope);
		ok_cancel (this);
	}
	int val (int i) const { return i >= n ? 0 : kind == AJ_REMAP ? ch[i]->sel : s[i]->value; }
	static AdjustDialog *of (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; return (AdjustDialog *) p; }
	static void onChange (Widget &w) { AdjustDialog *d = of (w); d->apply (); d->invalidate (true); }
	static void onScope (Widget &w)
	{
		AdjustDialog *d = of (w);
		adjust_restore (); adjust_shown ();
		d->sc = d->scope->sel;
		if (d->sc == SC_SELECTION && !has_sel ()) { d->sc = SC_LAYER; d->scope->sel = SC_LAYER; }
		adjust_begin (d->sc);
		d->apply (); d->invalidate (true);
	}
	void apply ()
	{
		adjust_apply (kind, val (0), val (1), val (2));
		if (g_view) g_view->invalidate (true);
	}
	void onDraw () override
	{
		char t[48]; scpy (t, ADJUST_NAMES[kind], sizeof t); int tn = slen (t); if (tn > 3 && t[tn - 1] == '.') t[tn - 3] = 0;
		drawBox (t);
		int y = titleH () + 18;
		for (int i = 0; i < n; i++)
		{
			Spec p = spec (kind, i);
			uk_text_l (canvas, 22, y + i * 44, 28, p.label, C_TEXT);
			if (kind != AJ_REMAP) { char b[16]; fmt_int (b, s[i]->value, p.suffix); uk_text_l (canvas, 372, y + i * 44, 26, b, C_TEXT); }
		}
		uk_text_l (canvas, 22, y + n * 44 + 4, 28, "Apply to", C_TEXT);
		const char *hint = kind == AJ_REMAP ? "Each channel taken from another one (or black, white)." : kind == AJ_INVERT ? "Each colour made its opposite." : 0;
		if (hint) uk_text_l (canvas, 22, y + n * 44 + 40, 26, hint, uk_mix (C_FACE, C_TEXT, 150));
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { close (1); return true; } return false; }
};
static void run_adjust (int kind)
{
	settle ();
	AdjustDialog d (kind);
	adjust_begin (d.sc);
	d.apply ();
	if (d.run () == 1) adjust_commit (); else adjust_cancel ();
	refresh (); focus_view ();
}
static void adjust_popup (int x, int y)
{
	PopupMenu m (x, y);
	for (int i = 0; i < AJ_COUNT; i++) { if (i == AJ_INVERT || i == AJ_FIRST_FILTER) m.separator (); m.add (ADJUST_NAMES[i], i + 1); }
	int r = m.run ();
	if (r > 0) run_adjust (r - 1); else focus_view ();
}
static void cmd_aj_bright () { run_adjust (AJ_BRIGHT); }
static void cmd_aj_hue () { run_adjust (AJ_HUE); }
static void cmd_aj_desat () { run_adjust (AJ_DESAT); }
static void cmd_aj_colorize () { run_adjust (AJ_COLORIZE); }
static void cmd_aj_remap () { run_adjust (AJ_REMAP); }
static void cmd_aj_invert () { run_adjust (AJ_INVERT); }
static void cmd_aj_sepia () { run_adjust (AJ_SEPIA); }
static void cmd_aj_poster () { run_adjust (AJ_POSTER); }
static void cmd_aj_thresh () { run_adjust (AJ_THRESH); }
static void cmd_aj_blur () { run_adjust (AJ_BLUR); }
static void cmd_aj_sharpen () { run_adjust (AJ_SHARPEN); }
static void cmd_aj_pixel () { run_adjust (AJ_PIXEL); }

// ---- the brushes' gallery, the gradients' list ------------------------------------------------------------------
// A floating panel of cells (a brush's stroke, a gradient's bar) -> the one clicked, -1.
class Gallery : public Modal
{
public:
	enum { BRUSHES, GRADIENTS, PATTERNS };
	int kind, hot, cur;
	Gallery (int k, int x, int y) : Modal (k == GRADIENTS ? 320 : 430, 10), kind (k), hot (-1), cur (0)
	{
		left = x; top = y;
		if (k == BRUSHES) { resizeTo (width, 30 + 3 * 62 + 38 + 2 * 62 + 8); cur = g_brush; }
		else if (k == PATTERNS) { resizeTo (width, 30 + 2 * 62 + 8); cur = g_pattern; }
		else { resizeTo (width, 10 + g_ngrads * 30 + 40); cur = g_grad; }
		Root *r = Root::current ();
		if (r && left + width > r->width) left = r->width - width;
		if (r && top + height > r->height) top = r->height - height;
	}
	int cellAt (int mx, int my, int *cx = 0, int *cy = 0, int *cw = 0, int *ch = 0) const
	{
		if (kind == GRADIENTS)
		{
			int i = (my - 6) / 30;
			if (mx < 4 || mx >= width - 4 || my < 6) return -1;
			if (i >= 0 && i < g_ngrads) { if (cx) { *cx = 4; *cy = 6 + i * 30; *cw = width - 8; *ch = 28; } return i; }
			if (my >= 6 + g_ngrads * 30 + 6 && my < height - 4) { if (cx) { *cx = 4; *cy = 6 + g_ngrads * 30 + 6; *cw = width - 8; *ch = 28; } return 1000; }
			return -1;
		}
		int base = kind == PATTERNS ? BR_FIRST_PATTERN : 0;
		for (int i = 0; i < (kind == PATTERNS ? (int) NPATTERNS : (int) BR_COUNT); i++)
		{
			int k = base + i, row, col, y0;
			if (kind == BRUSHES && k >= BR_FIRST_PATTERN) { int j = k - BR_FIRST_PATTERN; row = j / 3; col = j % 3; y0 = 30 + 3 * 62 + 38; }
			else { row = i / 3; col = i % 3; y0 = 30; }
			int x = 10 + col * 138, y = y0 + row * 62;
			if (mx >= x && mx < x + 132 && my >= y && my < y + 58) { if (cx) { *cx = x; *cy = y; *cw = 132; *ch = 58; } return k; }
		}
		return -1;
	}
	void onDraw () override
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		uk_popup (canvas, 0, 0, width, height, 8, C_FIELD);
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 150);
		if (kind == GRADIENTS)
		{
			for (int i = 0; i < g_ngrads; i++)
			{
				int y = 6 + i * 30;
				if (i == cur) uk_hilite (canvas, 4, y, width - 8, 28, 4, true);
				else if (i == hot) uk_rbox (canvas, 4, y, width - 8, 28, 4, uk_mix (C_FIELD, C_ACCENT, 40), uk_mix (C_FIELD, C_ACCENT, 40));
				grad_draw (canvas, 12, y + 6, 100, 16, i);
				uk_text_l (canvas, 122, y, 28, g_grads[i].name, i == cur ? C_SEL_TEXT : C_FIELD_TEXT);
				if (g_grads[i].user) uk_text_l (canvas, width - 50, y, 28, "yours", i == cur ? C_SEL_TEXT : dim);
			}
			int y = 6 + g_ngrads * 30;
			canvas.fillRect (10, y + 2, width - 20, 1, uk_mix (C_FIELD, 0x000000, 40));
			if (hot == 1000) uk_rbox (canvas, 4, y + 6, width - 8, 28, 4, uk_mix (C_FIELD, C_ACCENT, 40), uk_mix (C_FIELD, C_ACCENT, 40));
			uk_text_l (canvas, 14, y + 6, 28, "Edit gradients...", C_FIELD_TEXT);
			uk_text_l (canvas, width - 80, y + 6, 28, "GIMP .ggr", dim);
			return;
		}
		uk_text (canvas, 14, 8, kind == BRUSHES ? "Brushes" : "Patterns", dim, 2);
		if (kind == BRUSHES)
		{
			int y = 30 + 3 * 62 + 4;
			canvas.fillRect (12, y, width - 24, 1, uk_mix (C_FIELD, 0x000000, 40));
			uk_text (canvas, 14, y + 10, "Patterns (colour 1, on the picture's grid)", dim, 2);
		}
		int base = kind == PATTERNS ? BR_FIRST_PATTERN : 0;
		for (int i = 0; i < (kind == PATTERNS ? (int) NPATTERNS : (int) BR_COUNT); i++)
		{
			int k = base + i, x, y, w, h;
			cellAt (-1, -1);
			// (the cell's place: asked by a point inside it)
			int row, col, y0;
			if (kind == BRUSHES && k >= BR_FIRST_PATTERN) { int j = k - BR_FIRST_PATTERN; row = j / 3; col = j % 3; y0 = 30 + 3 * 62 + 38; }
			else { row = i / 3; col = i % 3; y0 = 30; }
			x = 10 + col * 138; y = y0 + row * 62; w = 132; h = 58;
			if (k == cur) { uk_rbox (canvas, x, y, w, h, 5, uk_mix (C_FIELD, C_ACCENT, 60), uk_mix (C_FIELD, C_ACCENT, 60)); uk_rline (canvas, x, y, w, h, 5, uk_mix (C_FIELD, C_ACCENT, 150)); }
			else if (k == hot) uk_rbox (canvas, x, y, w, h, 5, uk_mix (C_FIELD, C_ACCENT, 30), uk_mix (C_FIELD, C_ACCENT, 30));
			brush_sample (canvas, x + 10, y + 4, w - 20, 32, k, 0x3C3860);
			uk_text_c (canvas, x, y + 36, w, 20, BRUSH_NAMES[k], C_FIELD_TEXT);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int h = mx >= 0 && my >= 0 && mx < width && my < height ? cellAt (mx, my) : -1;
		if (h != hot) { hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (mx < 0 || my < 0 || mx >= width || my >= height) close (0); }
		else if (!bl && pressed) { pressed = false; if (hot >= 0) close (hot + 1); }
		return true;
	}
	bool onKey (long k) override { if (k == 27) close (0); return true; }
};

// ---- the gradient editor ------------------------------------------------------------------------------------------
// The gradients' list (the presets, yours), the one chosen: its name, its bar with the stops under it
// (click under the bar: a new stop; drag: move it, off the bar: remove it) and the midpoints above,
// the chosen stop's colour, position and opacity. A preset is copied to be changed. OK keeps yours on the card.
class GradEditor : public Modal
{
public:
	int gi, stop, drag; bool dragMid; Textbox *name, *pos; Slider *op; Button *col, *bNew, *bCopy, *bDel, *bRev, *bEven;
	Gradient keep[GMAX]; int nkeep, keepCur;
	enum { LW = 210 };
	GradEditor () : Modal (680, 452), gi (pclamp (g_grad, 0, g_ngrads - 1)), stop (0), drag (-1), dragMid (false), nkeep (g_ngrads), keepCur (g_grad)
	{
		centre (this);
		for (int i = 0; i < g_ngrads; i++) keep[i] = g_grads[i];
		int X = LW + 32, y = titleH () + 14;
		name = new Textbox (X + 60, y, width - X - 78, 28, ""); name->changed = onName; addChild (name);
		pos = new Textbox (X + 330, y + 200, 70, 28, ""); pos->changed = onPos; addChild (pos);
		col = new Button (X + 84, y + 200, 60, 28, "", onCol); addChild (col);
		op = new Slider (X + 84, y + 240, 200, 26, 0, 100, 100, onOp, C_FACE); addChild (op);
		int by = y + 290;
		bRev = new Button (X, by, 120, 28, "Reverse", onRev); addChild (bRev);
		bEven = new Button (X + 128, by, 140, 28, "Space evenly", onEven); addChild (bEven);
		int ly = height - 44;
		bNew = new Button (16, ly, 64, 30, "New", onNew); addChild (bNew);
		bCopy = new Button (86, ly, 64, 30, "Copy", onCopy); addChild (bCopy);
		bDel = new Button (156, ly, 70, 30, "Delete", onDel); addChild (bDel);
		ok_cancel (this);
		sync ();
	}
	Gradient &G () { return g_grads[gi]; }
	static GradEditor *of (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; return (GradEditor *) p; }
	int barX () const { return LW + 32; }
	int barY () const { return titleH () + 14 + 64; }
	int barW () const { return width - LW - 32 - 18; }
	void sync ()
	{
		Gradient &g = G (); grad_dyn (g, g_col1, g_col2);
		stop = pclamp (stop, 0, g.n - 1);
		name->setText (g.name);
		char b[12]; fmt_int (b, g.pos[stop] / 10, " %"); pos->setText (b);
		op->value = (int) ((g.col[stop] >> 24) * 100 / 255);
		bool ed = g.user; name->disabled = pos->disabled = col->disabled = op->disabled = bRev->disabled = bEven->disabled = !ed; bDel->disabled = !ed;
		invalidate (true);
	}
	// a preset: copied first (yours then)
	bool editable () { if (G ().user) return true; uk_messagebox ("Gradients", "A preset cannot be changed: Copy it first (the copy is yours).", MB_OK); return false; }
	static void onName (Widget &w) { GradEditor *e = of (w); if (e->G ().user) { scpy (e->G ().name, e->name->text, sizeof e->G ().name); e->invalidate (true); } }
	static void onPos (Widget &w)
	{
		GradEditor *e = of (w); Gradient &g = e->G ();
		if (!g.user || e->stop == 0 || e->stop == g.n - 1) return;
		int v = pclamp (parse_int (e->pos->text) * 10, g.pos[e->stop - 1], g.pos[e->stop + 1]);
		g.pos[e->stop] = v; e->invalidate (true);
	}
	static void onOp (Widget &w) { GradEditor *e = of (w); Gradient &g = e->G (); if (!g.user) return; g.col[e->stop] = (g.col[e->stop] & 0xFFFFFF) | (unsigned) (e->op->value * 255 / 100) << 24; e->invalidate (true); }
	static void onCol (Widget &w)
	{
		GradEditor *e = of (w); Gradient &g = e->G ();
		if (!e->editable ()) return;
		unsigned c = g.col[e->stop] & 0xFFFFFF;
		if (uk_color_dialog (&c, "Stop Colour")) g.col[e->stop] = (g.col[e->stop] & 0xFF000000u) | c;
		e->invalidate (true);
	}
	static void onRev (Widget &w)
	{
		GradEditor *e = of (w); Gradient &g = e->G (); if (!g.user) return;
		for (int a = 0, b = g.n - 1; a < b; a++, b--) { int p = g.pos[a]; g.pos[a] = 1000 - g.pos[b]; g.pos[b] = 1000 - p; unsigned c = g.col[a]; g.col[a] = g.col[b]; g.col[b] = c; }
		if (g.n & 1) g.pos[g.n / 2] = 1000 - g.pos[g.n / 2];
		for (int a = 0, b = g.n - 2; a < b; a++, b--) { int m = g.mid[a]; g.mid[a] = 1000 - g.mid[b]; g.mid[b] = 1000 - m; }
		if ((g.n - 1) & 1) g.mid[(g.n - 2) / 2] = 1000 - g.mid[(g.n - 2) / 2];
		e->sync ();
	}
	static void onEven (Widget &w) { GradEditor *e = of (w); Gradient &g = e->G (); if (!g.user) return; for (int i = 0; i < g.n; i++) { g.pos[i] = i * 1000 / (g.n - 1); g.mid[i] = 500; } e->sync (); }
	void addCopy (const Gradient &src, const char *nm)
	{
		if (g_ngrads >= GMAX) return;
		Gradient &g = g_grads[g_ngrads] = src;
		grad_dyn (g, g_col1, g_col2);
		g.dyn = 0; g.user = true; g.file[0] = 0;
		scpy (g.name, nm, sizeof g.name);
		gi = g_ngrads++; stop = 0;
		sync ();
	}
	static void onNew (Widget &w) { GradEditor *e = of (w); e->addCopy (g_grads[0], "My gradient"); }
	static void onCopy (Widget &w) { GradEditor *e = of (w); char nm[40]; scpy (nm, e->G ().name, 32); int n = slen (nm); scpy (nm + n, " copy", 40 - n); e->addCopy (e->G (), nm); }
	static void onDel (Widget &w) { GradEditor *e = of (w); if (!e->G ().user) return; grad_remove (e->gi); e->gi = pclamp (e->gi - 1, 0, g_ngrads - 1); e->stop = 0; e->sync (); }
	void onDraw () override
	{
		drawBox ("Gradient Editor");
		unsigned dim = uk_mix (C_FACE, C_TEXT, 150);
		// the list
		int lx = 16, ly = titleH () + 14, lh = height - ly - 56;
		uk_rbox (canvas, lx, ly, LW, lh, 5, C_FIELD, C_FIELD); uk_rline (canvas, lx, ly, LW, lh, 5, uk_mix (C_FACE, 0x000000, 60));
		int rows = (lh - 8) / 34, top = pmax (0, gi - rows + 1);
		for (int i = top; i < g_ngrads && i - top < rows; i++)
		{
			int y = ly + 4 + (i - top) * 34;
			if (i == gi) uk_hilite (canvas, lx + 3, y, LW - 6, 32, 4, true);
			grad_draw (canvas, lx + 8, y + 4, 54, 24, i);
			char b[40]; uk_text_fit (g_grads[i].name, LW - 80, b, sizeof b);
			uk_text_l (canvas, lx + 70, y, 32, b, i == gi ? C_SEL_TEXT : C_FIELD_TEXT);
		}
		// the gradient
		Gradient &g = G (); grad_dyn (g, g_col1, g_col2);
		int X = barX (), Y = barY (), BW = barW ();
		uk_text_l (canvas, X, titleH () + 14, 28, "Name", C_TEXT);
		grad_draw (canvas, X, Y + 14, BW, 52, gi);
		for (int i = 0; i + 1 < g.n; i++)				// the midpoints
		{
			int mx = X + (g.pos[i] + (g.pos[i + 1] - g.pos[i]) * g.mid[i] / 1000) * (BW - 1) / 1000;
			VPath p; int d[8] = { V (mx), V (Y + 1), V (mx + 5), V (Y + 7), V (mx), V (Y + 13), V (mx - 5), V (Y + 7) }; p.poly (d, 4); p.fill (canvas, 0xFFFFFF);
			p.clear (); p.polyline (d, 4, 16, true); p.fill (canvas, 0x303030);
		}
		for (int i = 0; i < g.n; i++)					// the stops
		{
			int sx = X + g.pos[i] * (BW - 1) / 1000, sy = Y + 68;
			int d[10] = { V (sx), V (sy), V (sx + 8), V (sy + 10), V (sx + 8), V (sy + 24), V (sx - 8), V (sy + 24), V (sx - 8), V (sy + 10) };
			VPath p; p.poly (d, 5); p.fill (canvas, i == stop ? C_ACCENT : 0xFFFFFF);
			p.clear (); p.polyline (d, 5, 16, true); p.fill (canvas, 0x303030);
			canvas.fillRect (sx - 5, sy + 11, 10, 10, g.col[i] & 0xFFFFFF); canvas.frameRect (sx - 5, sy + 11, 10, 10, 0x303030);
		}
		uk_text (canvas, X, Y + 100, g.user ? "Click under the bar: a new stop.  Drag a stop: move it (off the bar: remove it)." : "A preset: Copy it to change it.", dim);
		int sy = titleH () + 14 + 200;
		uk_text_l (canvas, X, sy, 28, "Colour", C_TEXT);
		canvas.fillRect (col->left + 6, col->top + 6, col->width - 12, col->height - 12, g.col[stop] & 0xFFFFFF);
		uk_text_l (canvas, X + 160, sy, 28, "Position", C_TEXT);
		uk_text_l (canvas, X, sy + 40, 26, "Opacity", C_TEXT);
		char b[8]; fmt_int (b, op->value, " %"); uk_text_l (canvas, X + 296, sy + 40, 26, b, C_TEXT);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		Gradient &g = G ();
		int X = barX (), Y = barY (), BW = barW ();
		if (drag >= 0)
		{
			if (!bl) { drag = -1; catchOutside = false; return true; }
			if (dragMid) { int a = g.pos[drag], b = g.pos[drag + 1]; if (b > a) g.mid[drag] = pclamp (((mx - X) * 1000 / (BW - 1) - a) * 1000 / (b - a), 20, 980); invalidate (true); return true; }
			if ((my > Y + 130 || my < Y - 20) && drag > 0 && drag < g.n - 1 && g.n > 2)		// (off the bar: removed)
			{
				for (int i = drag; i + 1 < g.n; i++) { g.pos[i] = g.pos[i + 1]; g.col[i] = g.col[i + 1]; g.mid[i] = g.mid[i + 1]; }
				g.n--; drag = -1; stop = 0; catchOutside = false; sync (); return true;
			}
			if (drag > 0 && drag < g.n - 1) g.pos[drag] = pclamp ((mx - X) * 1000 / (BW - 1), g.pos[drag - 1], g.pos[drag + 1]);
			char b[12]; fmt_int (b, g.pos[stop] / 10, " %"); pos->setText (b);
			invalidate (true);
			return true;
		}
		if (bl && !pressed && mx >= 16 && mx < 16 + LW && my >= titleH () + 14 && my < height - 56)	// (the list)
		{
			pressed = true;
			int lh = height - (titleH () + 14) - 56, rows = (lh - 8) / 34, top = pmax (0, gi - rows + 1);
			int i = top + (my - titleH () - 18) / 34;
			if (i >= 0 && i < g_ngrads) { gi = i; stop = 0; sync (); }
			return true;
		}
		if (bl && !pressed && g.user && mx >= X - 8 && mx < X + BW + 8)
		{
			if (my >= Y && my < Y + 14)				// (a midpoint)
				for (int i = 0; i + 1 < g.n; i++)
				{
					int m = X + (g.pos[i] + (g.pos[i + 1] - g.pos[i]) * g.mid[i] / 1000) * (BW - 1) / 1000;
					if (mx >= m - 6 && mx <= m + 6) { pressed = true; drag = i; dragMid = true; catchOutside = true; return true; }
				}
			if (my >= Y + 66 && my < Y + 96)				// (a stop, or a new one)
			{
				pressed = true;
				int best = -1, bd = 9;
				for (int i = 0; i < g.n; i++) { int sx = X + g.pos[i] * (BW - 1) / 1000, d = mx > sx ? mx - sx : sx - mx; if (d < bd) { bd = d; best = i; } }
				if (best < 0 && g.n < GMAXSTOPS)
				{
					int p = pclamp ((mx - X) * 1000 / (BW - 1), 0, 1000), at = 1;
					while (at < g.n && g.pos[at] < p) at++;
					unsigned c = grad_at (g, p / 1000.0f);
					for (int i = g.n; i > at; i--) { g.pos[i] = g.pos[i - 1]; g.col[i] = g.col[i - 1]; g.mid[i] = g.mid[i - 1]; }
					g.pos[at] = p; g.col[at] = c; g.mid[at] = 500; g.mid[at - 1] = 500; g.n++;
					best = at;
				}
				if (best >= 0) { stop = best; drag = best; dragMid = false; catchOutside = true; sync (); }
				return true;
			}
		}
		if (!bl) pressed = false;
		return Modal::onMouse (mx, my, bl, 0, 0, 0);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { close (1); return true; } return false; }
	void finish (bool ok)
	{
		if (!ok) { for (int i = 0; i < g_ngrads; i++) if (g_grads[i].user && i >= nkeep) {} g_ngrads = nkeep; for (int i = 0; i < nkeep; i++) g_grads[i] = keep[i]; g_grad = keepCur; return; }
		for (int i = 0; i < g_ngrads; i++) if (g_grads[i].user) grad_save (g_grads[i]);
		g_grad = gi;
	}
};
static void cmd_gradients ()
{
	settle ();
	GradEditor e;
	e.finish (e.run () == 1);
	refresh (); focus_view ();
}

// ---- colours, the ribbon, the options ---------------------------------------------------------------------------
static void add_custom (unsigned c)
{
	c |= 0xFF000000u;
	for (int i = 0; i < 10; i++) if (g_custom[i] == c) return;
	for (int i = 9; i > 0; i--) g_custom[i] = g_custom[i - 1];
	g_custom[0] = c;
}
static void colours_changed () { if (g_text.on) text_preview (); if (g_gj.on) grad_preview (); }
static void cmd_edit_colours ()
{
	unsigned c = (g_activeCol == 2 ? g_col2 : g_col1) & 0xFFFFFF;
	if (uk_color_dialog (&c, "Edit Colours"))
	{
		if (g_activeCol == 2) g_col2 = c | 0xFF000000u; else g_col1 = c | 0xFF000000u;
		add_custom (c);
		colours_changed ();
	}
	refresh (); focus_view ();
}
static void cmd_swap_colours () { unsigned t = g_col1; g_col1 = g_col2; g_col2 = t; colours_changed (); refresh (); focus_view (); }
static void cmd_grid () { g_grid = !g_grid; if (g_grid && g_view->zoom < 300) g_view->setZoom (400); refresh (); focus_view (); }
static void cmd_fit () { g_view->zoomFit (); refresh (); focus_view (); }
static void cmd_zoom_in () { g_view->zoomStep (1); refresh (); focus_view (); }
static void cmd_zoom_out () { g_view->zoomStep (-1); refresh (); focus_view (); }
static void cmd_zoom_100 () { g_view->setZoom (100); refresh (); focus_view (); }

static void rotate_popup (int x, int y)
{
	PopupMenu m (x, y);
	m.add ("Rotate Right 90", 1); m.add ("Rotate Left 90", 2); m.add ("Rotate 180", 3);
	m.separator ();
	m.add ("Flip Vertical", 4); m.add ("Flip Horizontal", 5);
	switch (m.run ()) { case 1: transform (1); break; case 2: transform (-1); break; case 3: transform (2); break; case 4: transform (4); break; case 5: transform (3); break; default: focus_view (); }
}
static void select_popup (int x, int y)
{
	PopupMenu m (x, y);
	m.add ("Rectangle", 1, true, "S"); m.add ("Free-form (lasso)", 2, true, "L"); m.add ("Magic wand", 3, true, "W");
	m.separator ();
	m.add ("Select All", 4, true, "Ctrl+A"); m.add ("Invert Selection", 5, true, "Ctrl+I"); m.add ("Deselect", 6, has_sel (), "Esc");
	m.separator ();
	m.add ("Delete", 7, has_sel () || D.fl.px, "Del"); m.add ("Crop to Selection", 8, has_sel ());
	switch (m.run ())
	{
	case 1: set_select (SK_RECT); break; case 2: set_select (SK_FREE); break; case 3: set_select (SK_WAND); break;
	case 4: cmd_select_all (); break; case 5: cmd_invert_sel (); break; case 6: cmd_deselect (); break;
	case 7: cmd_delete (); break; case 8: cmd_crop (); break;
	default: focus_view ();
	}
}
static void brushes_popup (int x, int y)
{
	Gallery g (Gallery::BRUSHES, x, y);
	int r = g.run ();
	if (r > 0) { g_brush = r - 1; if (g_opac[T_BRUSH] == BRUSH_OPACITY[g_brush == BR_MARKER ? BR_BRUSH : BR_MARKER] || g_brush == BR_MARKER) g_opac[T_BRUSH] = BRUSH_OPACITY[g_brush]; }
	set_tool (T_BRUSH);
}
static void ribbon_cmd (int cmd, int arg, int btn, int x, int y)
{
	switch (cmd)
	{
	case C_PASTE:
		if (btn == 2) { PopupMenu m (x, y); m.add ("Paste", 1, true, "Ctrl+V"); m.add ("Paste as New Layer", 2); m.add ("Open as Layer...", 3); int r = m.run (); if (r == 1) cmd_paste (); else if (r == 2) cmd_paste_layer (); else if (r == 3) cmd_open_layer (); else focus_view (); }
		else cmd_paste ();
		break;
	case C_CUT: cmd_cut (); break;
	case C_COPY: cmd_copy (); break;
	case C_UNDO: cmd_undo (); break;
	case C_REDO: cmd_redo (); break;
	case C_SELECT: select_popup (x, y); break;
	case C_CROP: cmd_crop (); break;
	case C_RESIZE: cmd_resize (); break;
	case C_ROTATE: rotate_popup (x, y); break;
	case C_TOOL: set_tool (arg); break;
	case C_ADJUST: adjust_popup (x, y); break;
	case C_BRUSHES: brushes_popup (x, y); break;
	case C_SHAPE: g_shape = arg; set_tool (T_SHAPE); break;
	case C_COL1: g_activeCol = 1; refresh (); focus_view (); break;
	case C_COL2: g_activeCol = 2; refresh (); focus_view (); break;
	case C_PALETTE: case C_CUSTOM:
	{
		unsigned c = cmd == C_PALETTE ? PALETTE[arg] : g_custom[arg];
		if (cmd == C_CUSTOM && !(c >> 24)) { cmd_edit_colours (); break; }
		c |= 0xFF000000u;
		if (btn == 2 || (btn == 1 && g_activeCol == 2)) g_col2 = c; else g_col1 = c;
		colours_changed ();
		refresh (); focus_view ();
		break;
	}
	case C_EDITCOL: cmd_edit_colours (); break;
	}
}
static void opt_cmd (int opt, int arg, int x, int y)
{
	switch (opt)
	{
	case O_SIZE: case O_OPACITY: case O_HARD: case O_TOL:
		if (g_gj.on && opt == O_OPACITY) { g_gj.opacity = g_opac[g_tool] * 255 / 100; grad_preview (); }
		g_view->invalidate (true); return;
	case O_BRUSHKIND: brushes_popup (x, y); return;
	case O_SELRECT: set_select (SK_RECT); return;
	case O_SELFREE: set_select (SK_FREE); return;
	case O_SELWAND: set_select (SK_WAND); return;
	case O_SELNEW: g_selMode = SEL_REPLACE; break;
	case O_SELADD: g_selMode = SEL_ADD; break;
	case O_SELSUB: g_selMode = SEL_SUB; break;
	case O_CONTIG: g_contig = !g_contig; break;
	case O_ALLLAYERS: g_allLayers = !g_allLayers; break;
	case O_SELALL: cmd_select_all (); return;
	case O_INVERT: cmd_invert_sel (); return;
	case O_DESELECT: cmd_deselect (); return;
	case O_FILLCOL: g_fillMode = FM_COLOUR; break;
	case O_FILLGRAD: g_fillMode = FM_GRADIENT; break;
	case O_FILLPAT: g_fillMode = FM_PATTERN; break;
	case O_GRADLIST:
	{
		Gallery g (Gallery::GRADIENTS, x, y);
		int r = g.run ();
		if (r == 1001) { cmd_gradients (); return; }
		if (r > 0) { g_grad = r - 1; if (g_gj.on) grad_preview (); }
		break;
	}
	case O_GSHAPE: g_gshape = arg; if (g_gj.on) grad_preview (); break;
	case O_GREPEAT:
	{
		PopupMenu m (x, y);
		for (int i = 0; i < 3; i++) m.add (GREPEAT_NAMES[i], i + 1, true, g_grepeat == i ? "*" : 0);
		int r = m.run ();
		if (r > 0) { g_grepeat = r - 1; if (g_gj.on) grad_preview (); }
		break;
	}
	case O_GREVERSE: g_greverse = !g_greverse; if (g_gj.on) grad_preview (); break;
	case O_PATTERN: { Gallery g (Gallery::PATTERNS, x, y); int r = g.run (); if (r > 0) g_pattern = r - 1; break; }
	case O_GERASE: g_gradErase = !g_gradErase; break;
	case O_FONT:
	{
		// the families: a list of at most 16 a page (PopupMenu's), the page around the current one
		text_init ();
		if (!g_tnfam) break;
		int first = pclamp (g_tfam - 7, 0, pmax (0, g_tnfam - 15));
		PopupMenu m (x, y);
		if (first > 0) m.add ("(more above)", 900);
		for (int i = first; i < g_tnfam && i < first + 14; i++) m.add (g_tfamNames[i], i + 1, true, i == g_tfam ? "*" : 0);
		if (first + 14 < g_tnfam) m.add ("(more below)", 901);
		int r = m.run ();
		if (r == 900) g_tfam = pmax (0, first - 1); else if (r == 901) g_tfam = pmin (g_tnfam - 1, first + 14);
		else if (r > 0) g_tfam = r - 1;
		if (g_text.on) text_preview ();
		break;
	}
	case O_TSIZE:
	{
		static const int S[13] = { 8, 10, 12, 14, 16, 20, 24, 32, 40, 48, 64, 96, 144 };
		static const char *const N[13] = { "8", "10", "12", "14", "16", "20", "24", "32", "40", "48", "64", "96", "144" };
		PopupMenu m (x, y);
		for (int i = 0; i < 13; i++) m.add (N[i], i + 1, true, g_tsize == S[i] ? "*" : 0);
		int r = m.run ();
		if (r > 0) { g_tsize = S[r - 1]; if (g_text.on) text_preview (); }
		break;
	}
	case O_BOLD: g_tbold = !g_tbold; if (g_text.on) text_preview (); break;
	case O_ITALIC: g_titalic = !g_titalic; if (g_text.on) text_preview (); break;
	case O_UNDER: g_tunder = !g_tunder; if (g_text.on) text_preview (); break;
	case O_ALIGN: g_talign = arg; if (g_text.on) text_preview (); break;
	case O_SMOOTH: g_tsmooth = !g_tsmooth; if (g_text.on) text_preview (); break;
	case O_TBACK: g_tback = !g_tback; if (g_text.on) text_preview (); break;
	case O_OUTLINE: g_outline = !g_outline; if (!g_outline && !g_fillShape) g_fillShape = true; break;
	case O_FILLSH: g_fillShape = !g_fillShape; if (!g_outline && !g_fillShape) g_outline = true; break;
	}
	refresh (); focus_view ();
}
static void zoom_cmd (int what)
{
	if (what >= 100) { g_view->setZoom (what - 100); refresh (); return; }
	if (what == 2) { cmd_grid (); return; }
	if (what == 3) { cmd_fit (); return; }
	if (what) { g_view->zoomStep (what); refresh (); focus_view (); return; }
	PopupMenu m (g_status->popX, g_status->top - 9 * 26);
	static const int Z[8] = { 25, 50, 100, 200, 400, 800, 1600, 3200 };
	static const char *const N[8] = { "25 %", "50 %", "100 %", "200 %", "400 %", "800 %", "1600 %", "3200 %" };
	m.add ("Fit", 1);
	for (int i = 0; i < 8; i++) m.add (N[i], i + 2);
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
		if (kapi_get_modifiers () & (MOD_CTRL | MOD_ALT)) return false;
		switch (k)			// (the tools' letters)
		{
		case 's': case 'S': set_select (SK_RECT); return true;
		case 'l': case 'L': set_select (SK_FREE); return true;
		case 'w': case 'W': set_select (SK_WAND); return true;
		case 'p': case 'P': set_tool (T_PENCIL); return true;
		case 'b': case 'B': set_tool (T_BRUSH); return true;
		case 'e': case 'E': set_tool (T_ERASER); return true;
		case 'f': case 'F': set_tool (T_FILL); return true;
		case 'g': case 'G': set_tool (T_GRADIENT); return true;
		case 't': case 'T': set_tool (T_TEXT); return true;
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
	ft_uikit_install ("DejaVu Sans", 13);
	int sw = 0, sh = 0; kapi_screen_size (&sw, &sh);
	if (sw > 0) W = pclamp (sw - 16, 760, 1180);
	if (sh > 0) H = pclamp (sh - 64, 520, 780);
	PaintRoot root;
	root.attach ();				// (a question asked before run (): its clicks and keys)
	uikit::init ();
	doc_new (800, 560, true);
	grad_presets (); grad_load_user ();
	g_col1 = 0xFF2B2B33;

	int bodyY = RIBBON_H + OPT_H, bodyH = H - bodyY - STATUS_H;
	g_ribbon = new Ribbon (0, 0, W);
	g_opts = new OptionsBar (0, RIBBON_H, W);
	g_view = new CanvasView (0, bodyY, W - PANEL_W, bodyH);
	g_layers = new LayersPanel (W - PANEL_W, bodyY, bodyH);
	g_status = new StatusBar (0, H - STATUS_H, W, g_view);
	root.addChild (g_ribbon); root.addChild (g_opts); root.addChild (g_layers); root.addChild (g_status);
	root.addChild (g_view);			// (last: on top -- a drag ended over another part still reaches it)
	g_ribbon->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_opts->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_view->anchor = ANCHOR_FILL;
	g_layers->anchor = ANCHOR_TOP | ANCHOR_BOTTOM | ANCHOR_RIGHT;
	g_status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.setResizable (true);
	g_changed = refresh; g_toolChanged = tool_changed;
	g_ribbonCmd = ribbon_cmd; g_layerCmd = layer_cmd; g_optCmd = opt_cmd; g_status->zoomCmd = zoom_cmd;

	static Menu menu;
	menu.menu ("File");
	menu.item ("New...", "^N", UK_CTRL ('N'), cmd_new);
	menu.item ("Open...", "^O", UK_CTRL ('O'), cmd_open);
	menu.item ("Open as Layer...", "", 0, cmd_open_layer);
	menu.separator ();
	menu.item ("Save", "^S", UK_CTRL ('S'), cmd_save);
	menu.item ("Save As...", "", 0, cmd_save_as);
	menu.separator ();
	menu.item ("Export as PNG...", "^E", UK_CTRL ('E'), cmd_export_png);
	menu.item ("Export as JPEG...", "", 0, cmd_export_jpg);
	menu.item ("Export as BMP...", "", 0, cmd_export_bmp);
	menu.item ("Export as GIF...", "", 0, cmd_export_gif);
	menu.separator ();
	menu.item ("Print...", "^P", UK_CTRL ('P'), cmd_print);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", UK_CTRL ('Z'), cmd_undo);
	menu.item ("Redo", "^Y", UK_CTRL ('Y'), cmd_redo);
	menu.separator ();
	menu.item ("Cut", "^X", UK_CTRL ('X'), cmd_cut);
	menu.item ("Copy", "^C", UK_CTRL ('C'), cmd_copy);
	menu.item ("Paste", "^V", UK_CTRL ('V'), cmd_paste);
	menu.item ("Paste as New Layer", "", 0, cmd_paste_layer);
	menu.item ("Delete", "Del", 0, cmd_delete);
	menu.separator ();
	menu.item ("Select All", "^A", UK_CTRL ('A'), cmd_select_all);
	menu.item ("Invert Selection", "^I", UK_CTRL ('I'), cmd_invert_sel);
	menu.item ("Deselect", "Esc", 0, cmd_deselect);
	menu.item ("Crop to Selection", "", 0, cmd_crop);
	menu.menu ("Image");
	menu.item ("Rotate Right 90", "", 0, cmd_rot_right);
	menu.item ("Rotate Left 90", "", 0, cmd_rot_left);
	menu.item ("Rotate 180", "", 0, cmd_rot_half);
	menu.item ("Flip Horizontal", "", 0, cmd_flip_h);
	menu.item ("Flip Vertical", "", 0, cmd_flip_v);
	menu.separator ();
	menu.item ("Resize...", "^W", UK_CTRL ('W'), cmd_resize);
	menu.menu ("Layers");
	menu.item ("New Layer", "^L", UK_CTRL ('L'), cmd_layer_new);
	menu.item ("Duplicate Layer", "", 0, cmd_layer_dup);
	menu.item ("Delete Layer", "", 0, cmd_layer_del);
	menu.separator ();
	menu.item ("Move Up", "", 0, cmd_layer_up);
	menu.item ("Move Down", "", 0, cmd_layer_down);
	menu.item ("Merge Down", "", 0, cmd_layer_merge);
	menu.item ("Flatten", "", 0, cmd_flatten);
	menu.separator ();
	menu.item ("Layer Properties...", "", 0, cmd_layer_props);
	menu.menu ("Colours");
	menu.item ("Brightness / Contrast...", "", 0, cmd_aj_bright);
	menu.item ("Hue / Saturation...", "", 0, cmd_aj_hue);
	menu.item ("Desaturate...", "", 0, cmd_aj_desat);
	menu.item ("Colorize...", "", 0, cmd_aj_colorize);
	menu.item ("Remap the Channels...", "", 0, cmd_aj_remap);
	menu.separator ();
	menu.item ("Invert Colours...", "", 0, cmd_aj_invert);
	menu.item ("Sepia...", "", 0, cmd_aj_sepia);
	menu.item ("Posterize...", "", 0, cmd_aj_poster);
	menu.item ("Threshold...", "", 0, cmd_aj_thresh);
	menu.separator ();
	menu.item ("Edit Colours...", "", 0, cmd_edit_colours);
	menu.item ("Swap Colours", "X", 0, cmd_swap_colours);
	menu.item ("Gradients...", "", 0, cmd_gradients);
	menu.menu ("Filters");
	menu.item ("Blur...", "", 0, cmd_aj_blur);
	menu.item ("Sharpen...", "", 0, cmd_aj_sharpen);
	menu.item ("Pixelate...", "", 0, cmd_aj_pixel);
	menu.menu ("View");
	menu.item ("Zoom In", "+", 0, cmd_zoom_in);
	menu.item ("Zoom Out", "-", 0, cmd_zoom_out);
	menu.item ("Actual Size (100%)", "", 0, cmd_zoom_100);
	menu.item ("Fit the Window", "", 0, cmd_fit);
	menu.separator ();
	menu.item ("Grid", "^G", UK_CTRL ('G'), cmd_grid);
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
			if (uk_messagebox ("Paint", "Paint was closed with unsaved changes. Open the recovered picture?", MB_YESNO) == 1 && doc_open (RECOVER))
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
