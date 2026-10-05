//
// 3dforge -- Onyx's small parametric CAD (docs/3dforge: the mock-ups the user chose): solid bodies made of simple
// steps -- a box or a cylinder by click, move, click; a sketch on a face and its extrusion -- joined, cut or
// intersected, their edges rounded or chamfered; every step keeps its values in a history that is replayed when
// one changes. The geometry is Manifold's (Libs/manifold: boolean operations on closed meshes), the view is drawn
// by the GPU. Files: .3df (the history, as text); exports: STL and OBJ (the meshes), DXF, SVG and PDF (a flat drawing
// seen from a side, at the part's size), PNG (a picture). The shapes: box, cylinder, pyramid, prism,
// tapered prism (their base of as many sides as wanted), torus, sphere.
//
// Pieces: fdoc.h (the document: the steps, their replay, the sketch, the fillets, the file, the exports), frender.h
// (the camera, the frame for the GPU or the processor, picking), fview.h (the tools, the view, the bodies' panel over
// it), fui.h (the tools' bar, the timeline, the selection's panel, the status bar), ficons.h (the icons).
//
// MIT licence (Onyx).
//
#define ONYX_HOSTED_NEW 1		// (libstdc++'s operator new: the program is full of std::vector, and so is Manifold)
#include <math.h>
#include <strings.h>
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"
#include "docguard.h"
#include "fui.h"
#include "fdraw.h"

using namespace uikit;
using namespace forge;

static int W = 1006, H = 701;
static Ribbon *g_ribbon; static Timeline *g_time; static Props *g_props; static StatusBar *g_status;
static bool g_rebuild = true;
static const char *DOCS = "SD:/docs/3d";

static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1; return b; }
static bool ends_with (const char *s, const char *e) { size_t a = strlen (s), b = strlen (e); return a >= b && !strcasecmp (s + a - b, e); }
static bool changed_doc () { return A.doc.changes != A.saved; }

// What the window shows again. The right panel's controls are made again at the next turn of the loop, never from
// inside one of their own callbacks.
static void app_refresh (int what)
{
	if (what & R_PANELS) g_rebuild = true;
	else if ((what & R_FIELDS) && g_props && !g_rebuild) g_props->sync ();
	for (Widget *w : { (Widget *) g_time, (Widget *) g_status, (Widget *) g_ribbon, (Widget *) g_props, (Widget *) g_view }) if (w) w->invalidate (true);
}

// ---- the file ---------------------------------------------------------------------------------------------------------
static bool load_path (const char *path)
{
	void *f = kapi_open (path);
	if (!f) return false;
	unsigned n = kapi_fsize (f); std::string s (n, 0);
	int got = n ? kapi_read (f, &s[0], n) : 0; kapi_close (f);
	if (got != (int) n) return false;
	Doc d;
	if (!d.load (s.c_str ())) return false;
	A.doc = d; A.saved = A.doc.changes; A.undo.clear (); A.redo.clear (); A.selFeat = A.selBody = -1;
	snprintf (A.path, sizeof A.path, "%s", path);
	if (!A.doc.bodies.empty ()) A.selBody = A.doc.bodies[0].id;
	A.sketching = false; tool_set (T_SELECT); if (g_view) g_view->home ();
	return true;
}
static void cmd_save_as ();
static void cmd_save ()
{
	if (!A.path[0]) { cmd_save_as (); return; }
	std::string s = A.doc.save ();
	if (kapi_save_file (A.path, s.data (), (unsigned) s.size ()) >= 0) A.saved = A.doc.changes;
	else uk_messagebox ("Save", "The file could not be written.", MB_OK);
	ui (0);
}
static void cmd_save_as ()
{
	char def[80], path[200]; snprintf (def, sizeof def, "%s", A.path[0] ? base_name (A.path) : "part.3df");
	kapi_mkdir (DOCS);
	if (!uk_file_save (path, sizeof path, DOCS, def)) return;
	if (!ends_with (path, ".3df")) { size_t k = strlen (path); snprintf (path + k, sizeof path - k, ".3df"); }
	snprintf (A.path, sizeof A.path, "%s", path); cmd_save ();
}
static bool guard () { return doc_confirm (A.path[0] ? base_name (A.path) : "this part", changed_doc (), cmd_save); }
static void cmd_new ()
{
	if (!guard ()) return;
	if (A.sketching) sketch_end (false);
	A.doc = Doc (); A.saved = 0; A.path[0] = 0; A.undo.clear (); A.redo.clear (); A.selFeat = A.selBody = -1;
	tool_set (T_SELECT); g_view->home ();
}
static void cmd_open ()
{
	char path[200];
	if (!guard ()) return;
	if (A.sketching) sketch_end (false);
	if (uk_file_open (path, sizeof path, DOCS) && !load_path (path)) uk_messagebox ("Open", "That file is not a 3DForge part (.3df).", MB_OK);
}

// ---- Export: the meshes (STL, OBJ), a flat drawing from a side (DXF, SVG, PDF), a picture (PNG) ----------------------------
class ExportDialog : public Modal
{
public:
	enum { STL, OBJ, DXF, SVG, PDF, PNG };
	SegmentedControl *fmtS, *whatS, *curveS; Dropdown *viewD; Checkbox *bin, *hid; Textbox *name, *folder; Button *browse;
	int tris, nlines, pw, ph; unsigned bytes; bool closed; std::string data; double dw, dh;
	int yWhat, yCurve, yView, yCheck, yName, yFolder, ySum;
	static void changed (Widget &w) { ((ExportDialog *) w.parent)->recompute (); }
	static void button (Widget &w) { ((ExportDialog *) w.parent)->onButton (w.tag); }
	ExportDialog () : Modal (460, 440), tris (0), nlines (0), pw (0), ph (0), bytes (0), closed (true), dw (0), dh (0)
	{
		static const char *const F[] = { "STL", "OBJ", "DXF", "SVG", "PDF", "PNG" }, *const WH[] = { "Whole part", "Selected body" }, *const C[] = { "Draft", "Fine", "Very fine" };
		static const char *const V[] = { "Front", "Back", "Left", "Right", "Top", "Bottom", "As on screen" };
		int x = 20, w = width - 40;
		fmtS = new SegmentedControl (x + 110, titleH () + 18, w - 110, 28, F, 6, 0, changed); addChild (fmtS);
		whatS = new SegmentedControl (x + 110, 0, w - 110, 28, WH, 2, 0, changed); addChild (whatS);
		if (!A.doc.body (A.selBody)) whatS->setEnabled (1, false);
		curveS = new SegmentedControl (x + 110, 0, w - 110, 28, C, 3, A.doc.segs <= 48 ? 0 : A.doc.segs <= 96 ? 1 : 2, changed); addChild (curveS);
		bin = new Checkbox (x + 110, 0, w - 110, 20, "Binary file (smaller)", true, changed, C_FACE); addChild (bin);
		hid = new Checkbox (x + 110, 0, w - 110, 20, "Hidden edges too, dashed", false, changed, C_FACE); addChild (hid);
		char def[80]; snprintf (def, sizeof def, "%s", A.path[0] ? base_name (A.path) : "part"); char *dot = strrchr (def, '.'); if (dot) *dot = 0;
		name = new Textbox (x + 110, 0, w - 110, 28, def); addChild (name);
		folder = new Textbox (x + 110, 0, w - 110 - 86, 28, DOCS); folder->maxLen = 180; addChild (folder);
		browse = new Button (x + w - 78, 0, 78, 28, "Browse...", button); browse->tag = 2; addChild (browse);
		ABtn *c = new ABtn (width - 220, height - 44, 92, 28, "Cancel", 0); c->bg = C_FACE; addChild (c);
		ABtn *o = new ABtn (width - 118, height - 44, 98, 28, "Export", 1, true); o->bg = C_FACE; addChild (o);
		viewD = new Dropdown (x + 110, 0, 180, 28, V, 7, 0, changed); addChild (viewD);		// (last: its list opens over the rest)
		recompute ();
	}
	bool flat () const { return fmtS->selected >= DXF; }
	int segs () const { return curveS->selected == 0 ? 48 : curveS->selected == 1 ? 96 : 192; }
	// The rows, for the format chosen: a mesh has no view, a drawing no "binary".
	void place ()
	{
		int f = fmtS->selected, y = titleH () + 18 + 40;
		yWhat = y; whatS->top = y; y += 40;
		yCurve = y; curveS->top = y; y += 60;
		yView = flat () ? y : -1; viewD->hidden = !flat (); if (flat ()) { viewD->top = y; y += 40; }
		bool c1 = f == STL, c2 = f == DXF || f == SVG || f == PDF;
		bin->hidden = !c1; hid->hidden = !c2; yCheck = c1 || c2 ? y : -1; if (c1 || c2) { bin->top = hid->top = y; y += 34; }
		yName = y; name->top = y; y += 38;
		yFolder = y; folder->top = browse->top = y; y += 44;
		ySum = y;
	}
	void recompute ()
	{
		place ();
		Doc d; d.feats = A.doc.feats; d.props = A.doc.props; d.upto = A.doc.upto; d.segs = segs (); d.rebuild ();
		int f = fmtS->selected, only = whatS->selected == 1 ? A.selBody : -1;
		static const double VA[7][2] = { { -90, 0 }, { 90, 0 }, { 180, 0 }, { 0, 0 }, { -90, 90 }, { -90, -90 }, { 0, 0 } };
		double az = viewD->sel == 6 ? A.cam.az : VA[viewD->sel][0], el = viewD->sel == 6 ? A.cam.el : VA[viewD->sel][1];
		tris = nlines = pw = ph = 0; data.clear ();
		if (f == STL || f == OBJ) data = d.exportMesh (f == OBJ, bin->checked, only, &tris);
		else if (f == PNG) data = picture_png (d, only, az, el, 1024, &pw, &ph);
		else
		{
			Drawing g;
			if (make_drawing (d, only, az, el, hid->checked, g))
			{
				nlines = (int) g.segs.size (); dw = g.w; dh = g.h;
				data = f == DXF ? drawing_dxf (g) : f == SVG ? drawing_svg (g) : drawing_pdf (g, name->text);
			}
		}
		bytes = (unsigned) data.size ();
		if (f == STL && !tris) { data.clear (); bytes = 0; }
		closed = true; for (Body &b : d.bodies) if (b.m.Status () != Manifold::Error::NoError) closed = false;
		invalidate (true);
	}
	void onButton (int tag) override
	{
		if (tag == 2) { char p[200]; if (uk_folder_open (p, sizeof p, folder->text)) { folder->setText (p); invalidate (true); } return; }
		if (tag == 0) { close (0); return; }
		static const char *const EXT[] = { ".stl", ".obj", ".dxf", ".svg", ".pdf", ".png" };
		char path[300]; const char *ext = EXT[fmtS->selected];
		if (fmtS->selected == PDF) recompute ();			// (its title: the name as it is now)
		kapi_mkdir (folder->text);
		snprintf (path, sizeof path, "%s/%s%s", folder->text, name->text, ends_with (name->text, ext) ? "" : ext);
		if (!bytes) { uk_messagebox ("Export", "There is nothing to export yet.", MB_OK); return; }
		if (kapi_save_file (path, data.data (), bytes) < 0) { uk_messagebox ("Export", "The file could not be written.", MB_OK); return; }
		close (1);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { onButton (1); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Export"); int x = 20, w = width - 40, f = fmtS->selected; char t[120], sz[32];
		auto lab = [&] (const char *s, int yy) { uk_text (canvas, x, yy + (28 - uk_fh ()) / 2, s, C_TEXT); };
		lab ("Format", titleH () + 18); lab ("What", yWhat); lab ("Curves", yCurve);
		{ UkFaceScope fs (g_small); snprintf (t, sizeof t, "%d sides to a circle", segs ()); uk_text (canvas, x + 110, yCurve + 34, t, dim_col ()); }
		if (yView >= 0)
		{
			lab ("Seen from", yView);
			UkFaceScope fs (g_small); uk_text (canvas, x + 110 + 190, yView + (28 - uk_fh ()) / 2, f == PNG ? "1024 pixels" : "at its size, in mm", dim_col ());
		}
		lab ("Name", yName); lab ("Folder", yFolder);
		unsigned pc = panel_col (); uk_rbox (canvas, x, ySum, w, 34, 6, pc, pc); uk_rline (canvas, x, ySum, w, 34, 6, uk_tone (C_BG, 96));
		bool ok = bytes && (flat () || closed);
		icon (canvas, ok ? I_CHECK : I_WARN, x + 10, ySum + 8, 18, ok ? C_GREEN : 0x4A3A10, C_ACCENT, pc);
		if (bytes >= 1024 * 1024) snprintf (sz, sizeof sz, "%.1f MB", bytes / 1048576.0); else snprintf (sz, sizeof sz, "%u KB", (bytes + 1023) / 1024);
		if (!bytes) snprintf (t, sizeof t, "Nothing to export yet");
		else if (f == PNG) snprintf (t, sizeof t, "%d \xC3\x97 %d pixels \xC2\xB7 %s", pw, ph, sz);
		else if (flat ()) snprintf (t, sizeof t, "%d lines \xC2\xB7 %.1f \xC3\x97 %.1f mm \xC2\xB7 %s", nlines, dw, dh, sz);
		else snprintf (t, sizeof t, "%d triangles \xC2\xB7 %s \xC2\xB7 %s", tris, sz, closed ? "closed solid" : "not closed");
		uk_text (canvas, x + 36, ySum + (34 - uk_fh ()) / 2, t, C_TEXT);
	}
};
// (the dialog's two buttons are ABtn: their id comes here)
static ExportDialog *g_export;
static void cmd_export ()
{
	if (A.sketching) return;
	ExportDialog d; Root *rt = Root::current (); d.left = (rt->width - d.width) / 2; d.top = (rt->height - d.height) / 2;
	g_export = &d; int r = d.run (); g_export = 0;
	if (r == 1) set_hint ("Exported."); ui (R_ALL);
}

// ---- the commands ---------------------------------------------------------------------------------------------------------
// Enter, or OK: the step being made goes on, or is done.
static void confirm ()
{
	if (A.sketching)
	{
		SkEl &e = A.cur;
		if (A.selEl >= 0 || A.curStep == 0) return;
		if (e.kind == SK_ARC && A.curStep == 1) { if (e.r > 1e-6) { A.curStep = 2; memset (A.typed, 0, sizeof A.typed); A.typed[0] = true; tool_hint (); ui (R_ALL); } return; }
		bool ok = e.kind == SK_LINE ? e.len > 1e-6 : e.kind == SK_ARC ? fabs (e.sweep) > 1e-6 && e.r > 1e-6 : e.kind == SK_CIRCLE ? e.w > 1e-6 : fabs (e.w) > 1e-6 && fabs (e.h) > 1e-6;
		if (ok) sk_add (e);
		return;
	}
	if (!A.hasPend) return;
	Feature &f = A.pend;
	if (f.kind == F_BOX || round_kind (f.kind)) { g_view->shapeNext (0, 0, false); return; }
	if (f.kind == F_COMBINE || (f.kind == F_MOVE && A.step == 0)) return;
	commit_pend ();
}
static void cancel ()
{
	if (A.sketching)
	{
		if (A.selEl >= 0) { A.selEl = -1; ui (R_ALL); return; }
		A.chain = false; sk_arm (); tool_hint (); ui (R_ALL); return;
	}
	tool_set (T_SELECT);
}
namespace forge {
static void cmd (int id)
{
	if (g_export) { g_export->onButton (id); return; }
	switch (id)
	{
	case CMD_NEW: cmd_new (); break;
	case CMD_OPEN: cmd_open (); break;
	case CMD_SAVE: cmd_save (); break;
	case CMD_UNDO: do_undo (false); g_view->invalidate (true); break;
	case CMD_REDO: do_undo (true); g_view->invalidate (true); break;
	case CMD_EXPORT: cmd_export (); break;
	case CMD_SK_CANCEL: sketch_end (false); break;
	case CMD_SK_FINISH: sketch_end (true); break;
	case CMD_SK_CLOSE: sk_close (); break;
	case CMD_SK_START:				// a sketch on a plane of the axes, moved along its normal
	{
		double o = A.skOffset;
		Plane pl = A.skPlane == 1 ? plane_of (V3 (0, o, 0), V3 (0, -1, 0)) : A.skPlane == 2 ? plane_of (V3 (o, 0, 0), V3 (1, 0, 0)) : plane_of (V3 (0, 0, o), V3 (0, 0, 1));
		sketch_begin (pl, -1); break;
	}
	case CMD_OK: confirm (); break;
	case CMD_CANCEL: cancel (); break;
	case CMD_DELETE: if (!A.sketching && A.selFeat >= 0) delete_feature (A.selFeat); break;
	case CMD_DEL_ELEMENT:
		if (A.sketching && A.selEl >= 0 && A.selEl < (int) A.sk.els.size ()) { A.sk.els.erase (A.sk.els.begin () + A.selEl); A.selEl = -1; sk_eval (); A.chain = false; sk_arm (); ui (R_ALL); }
		break;
	case CMD_EDIT_SKETCH:
		if (!A.sketching && A.selFeat >= 0 && A.doc.feats[A.selFeat].kind == F_SKETCH) sketch_begin (A.doc.feats[A.selFeat].pl, A.doc.feats[A.selFeat].target, A.selFeat);
		break;
	case CMD_ROLL_HERE: if (A.selFeat >= 0) { A.doc.upto = A.selFeat + 1; A.doc.touch (); ui (R_ALL); } break;
	case CMD_ROLL_END: A.doc.upto = (int) A.doc.feats.size (); A.doc.touch (); ui (R_ALL); break;
	}
	if (g_view) g_view->invalidate (true);
}
} // namespace forge

class ForgeRoot : public Root
{
public:
	ForgeRoot () : Root (W, H, "3DForge") {}
	void onTick () override
	{
		if (!g_rebuild || !g_props) return;
		g_rebuild = false; g_props->rebuild (); g_ribbon->sync ();
	}
	bool onKey (long k) override
	{
		if (kapi_get_modifiers () & (MOD_CTRL | MOD_ALT)) return false;
		if (k == 27) { cancel (); return true; }
		if (k == KEY_ENTER) { confirm (); return true; }
		if (k == KEY_DEL) { cmd (A.sketching ? CMD_DEL_ELEMENT : CMD_DELETE); return true; }
		return false;
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		char path[200];
		if (type != DND_FILES || !doc_first_path (data, path, sizeof path) || !guard ()) return;
		if (!load_path (path)) uk_messagebox ("Open", "That file is not a 3DForge part (.3df).", MB_OK);
	}
};

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	FtTextFace *sm = new FtTextFace; sm->open ("DejaVu Sans", 11); g_small = sm;
	FtTextFace *bg = new FtTextFace; bg->open ("DejaVu Sans", 15); g_big = bg;
	FtTextFace *ti = new FtTextFace; ti->open ("DejaVu Sans", 9); g_tiny = ti;
	int sw = 0, sh = 0; kapi_screen_size (&sw, &sh);
	if (sw > 0) W = sw - 18 < 820 ? 820 : sw - 18 > 1240 ? 1240 : sw - 18;
	if (sh > 0) H = sh - 67 < 560 ? 560 : sh - 67 > 820 ? 820 : sh - 67;
	ForgeRoot root;
	if (root.canvas.px == 0) return 1;
	root.attach ();
	uikit::init ();
	A.refresh = app_refresh;

	int top = Ribbon::H + 8, bh = H - top - StatusBar::H - 8;
	g_ribbon = new Ribbon (W);
	int vh = bh - Timeline::H - 6;				// the view, the timeline under it; the selection at the right
	g_time = new Timeline (8, top + vh + 6, W - 8 - 232);
	g_props = new Props (W - 224, top, 216, bh);
	g_status = new StatusBar (H - StatusBar::H, W);
	g_view = new View (8, top, W - 8 - 232, vh);
	root.addChild (g_ribbon); root.addChild (g_time); root.addChild (g_props); root.addChild (g_status); root.addChild (g_view);
	g_ribbon->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_time->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	g_props->anchor = ANCHOR_TOP | ANCHOR_BOTTOM | ANCHOR_RIGHT;
	g_status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	g_view->anchor = ANCHOR_FILL;
	root.setResizable (true); root.setMinSize (820, 560);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New", "^N", UK_CTRL ('N'), cmd_new);
	menu.item ("Open...", "^O", UK_CTRL ('O'), cmd_open);
	menu.separator ();
	menu.item ("Save", "^S", UK_CTRL ('S'), cmd_save);
	menu.item ("Save As...", "", 0, cmd_save_as);
	menu.separator ();
	menu.item ("Export...", "^E", UK_CTRL ('E'), cmd_export);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", UK_CTRL ('Z'), [] { cmd (CMD_UNDO); });
	menu.item ("Redo", "^Y", UK_CTRL ('Y'), [] { cmd (CMD_REDO); });
	menu.separator ();
	menu.item ("Delete Step", "Del", 0, [] { cmd (CMD_DELETE); });
	menu.item ("Roll Back to the Step", "", 0, [] { cmd (CMD_ROLL_HERE); });
	menu.item ("Roll to the End", "", 0, [] { cmd (CMD_ROLL_END); });
	menu.menu ("View");
	menu.item ("Home", "", 0, [] { g_view->home (); });
	menu.item ("Fit", "", 0, [] { g_view->fit (); });
	menu.separator ();
	menu.item ("Top", "", 0, [] { g_view->lookAlong (-90, 90); });
	menu.item ("Front", "", 0, [] { g_view->lookAlong (-90, 0); });
	menu.item ("Right", "", 0, [] { g_view->lookAlong (0, 0); });
	menu.separator ();
	menu.item ("Edges", "", 0, [] { A.showEdges = !A.showEdges; ui (R_ALL); });
	menu.item ("Grid", "", 0, [] { A.showGrid = !A.showGrid; ui (R_ALL); });
	menu.item ("See Through", "", 0, [] { A.seeThrough = !A.seeThrough; ui (R_ALL); });
	menu.item ("Draw with the Processor", "", 0, [] { g_gpuOff = !g_gpuOff; ui (R_ALL); });
	menu.menu ("Create");
	menu.item ("Box", "", 0, [] { tool_set (T_BOX); });
	menu.item ("Cylinder", "", 0, [] { tool_set (T_CYL); });
	menu.item ("Pyramid", "", 0, [] { tool_set (T_PYRAMID); });
	menu.item ("Prism", "", 0, [] { tool_set (T_PRISM); });
	menu.item ("Tapered Prism", "", 0, [] { tool_set (T_TAPER); });
	menu.item ("Torus", "", 0, [] { tool_set (T_TORUS); });
	menu.item ("Sphere", "", 0, [] { tool_set (T_SPHERE); });
	menu.separator ();
	menu.item ("Sketch", "", 0, [] { tool_set (T_SKETCH); });
	menu.item ("Extrude", "", 0, [] { tool_set (T_EXTRUDE); });
	menu.menu ("Modify");
	menu.item ("Fillet", "", 0, [] { tool_set (T_FILLET); });
	menu.item ("Chamfer", "", 0, [] { tool_set (T_CHAMFER); });
	menu.item ("Move", "", 0, [] { tool_set (T_MOVE); });
	menu.separator ();
	menu.item ("Union", "", 0, [] { tool_set (T_UNION); });
	menu.item ("Subtract", "", 0, [] { tool_set (T_SUB); });
	menu.item ("Intersect", "", 0, [] { tool_set (T_INT); });
	menu.separator ();
	menu.item ("Measure", "", 0, [] { tool_set (T_MEASURE); });
	menu.menu ("Help");
	menu.item ("About 3DForge", "", 0, [] { uk_messagebox ("3DForge", "A small parametric CAD for Onyx.\nGeometry: Manifold (Apache-2.0), Clipper2 (BSL-1.0).", MB_OK); });
	menu.publish ();

	tool_set (T_SELECT);
	char args[200]; int n = kapi_get_args (args, sizeof args);
	if (n > 0 && args[0] && !load_path (args)) uk_messagebox ("Open", "That file is not a 3DForge part (.3df).", MB_OK);
	root.fitWorkArea ();
	root.run ();
	return 0;
}
