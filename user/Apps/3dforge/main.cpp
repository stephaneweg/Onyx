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
// Manufacture for a filament printer (ffdm.h): the nozzle's path made from the layers' height, the shell and the infill,
// looked at layer by layer and played; no printer's file yet. Its values: SD:/apps/3dforge.app/filament.ini.
// Manufacture for a resin printer (fprint.h; the process is chosen in the setup): the body on the plate, its supports,
// its layers cut and looked at, the printer's file written. The resin is kept in SD:/apps/3dforge.app/print.ini; more
// printers of the same family: SD:/apps/3dforge.app/printers.ini.
// Manufacture (fcam.h): the body in its stock, an origin, a flat end mill; a clearing in levels and a contour, their
// moves shown, checked, written as G-code for a GRBL router. The setup is kept in the .3df, the tool and the machine
// in SD:/apps/3dforge.app/cam.ini.
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
static const char *DOCS = "SD:/docs/3d", *CAM_INI = "SD:/apps/3dforge.app/cam.ini", *PRINT_INI = "SD:/apps/3dforge.app/print.ini", *FDM_INI = "SD:/apps/3dforge.app/filament.ini", *PRINTERS_INI = "SD:/apps/3dforge.app/printers.ini";

static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1; return b; }
static bool ends_with (const char *s, const char *e) { size_t a = strlen (s), b = strlen (e); return a >= b && !strcasecmp (s + a - b, e); }
static bool changed_doc () { return A.doc.changes != A.saved; }

// What the window shows again. The right panel's controls are made again at the next turn of the loop, never from
// inside one of their own callbacks.
static void app_refresh (int what)
{
	if (what & R_PANELS) g_rebuild = true;
	else if ((what & R_FIELDS) && g_props && !g_rebuild) g_props->sync ();
	if (g_ribbon) g_ribbon->sync ();			// (its buttons follow what is going on: a sketch, values not yet validated)
	for (Widget *w : { (Widget *) g_time, (Widget *) g_status, (Widget *) g_ribbon, (Widget *) g_props, (Widget *) g_view }) if (w) w->invalidate (true);
}

// ---- the file ---------------------------------------------------------------------------------------------------------
static bool read_text (const char *path, std::string &s)
{
	void *f = kapi_open (path);
	if (!f) return false;
	unsigned n = kapi_fsize (f); s.assign (n, 0);
	int got = n ? kapi_read (f, &s[0], n) : 0; kapi_close (f);
	return got == (int) n;
}
// A part without a setup yet: Design, the tool and the machine as they were left.
static void job_reset ()
{
	A.camMode = A.camSim = A.camDirty = false; A.camSel = A.camPage = 0; A.job = CamSetup (); A.paths = CamPaths ();
	std::string s; if (read_text (CAM_INI, s)) cam_presets_take (A.job, s.c_str ());
	// (a resin printer's: its resin as it was left)
	A.gen = 0; A.print = PrintSetup (); A.pjob = PrintJob (); A.slicer = PrintSlicer (); A.sliced = A.printDirty = A.wantFile = A.layerPlay = A.layer3d = false; A.layer = 0;
	A.placed = A.held = Manifold (); A.placedMesh = A.supMesh = A.cutMesh = RMesh ();
	if (read_text (PRINT_INI, s)) { PrintSetup t; print_load (t, s.c_str ()); A.print.resin = t.resin; A.print.machine = t.machine; }
	// (a filament printer's: its values as they were left)
	A.fdm = FdmSettings (); A.fmach = FDM_MACHINE0; A.fjob = FdmJob (); A.fdmReady = false; A.fdmDrawn = -1;
	if (read_text (FDM_INI, s)) fdm_load (A.fdm, A.fmach, s.c_str ());
	A.fdmPct = A.fdm.infill * 100;
}
namespace forge { static void fdm_presets_save () { A.fdm.infill = A.fdmPct / 100; std::string s = fdm_save (A.fdm, A.fmach); kapi_save_file (FDM_INI, s.data (), (unsigned) s.size ()); } }
namespace forge { static void print_presets_save () { PrintSetup t; t.on = true; t.resin = A.print.resin; t.machine = A.print.machine; std::string s = print_save (t); kapi_save_file (PRINT_INI, s.data (), (unsigned) s.size ()); } }
namespace forge { static void cam_presets_save () { std::string s = cam_presets (A.job); kapi_save_file (CAM_INI, s.data (), (unsigned) s.size ()); } }
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
	job_reset (); cam_load (A.job, s.c_str ()); print_load (A.print, s.c_str ());
	if (strstr (s.c_str (), "\nmanufacture resin")) A.gen = 1;
	if (strstr (s.c_str (), "\nmanufacture filament")) { A.gen = 2; fdm_load (A.fdm, A.fmach, s.c_str ()); A.fdmPct = A.fdm.infill * 100; }
	snprintf (A.path, sizeof A.path, "%s", path);
	if (!A.doc.bodies.empty ()) A.selBody = A.doc.bodies[0].id;
	A.sketching = false; tool_set (T_SELECT); if (g_view) g_view->home ();
	return true;
}
static void cmd_save_as ();
static void cmd_save ()
{
	if (!A.path[0]) { cmd_save_as (); return; }
	std::string s = A.doc.save () + cam_save (A.job) + print_save (A.print) + (A.gen == 1 ? "manufacture resin\n" : A.gen == 2 ? "manufacture filament\n" + fdm_save (A.fdm, A.fmach) : std::string ());
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
	job_reset (); A.caption[0] = 0;
	tool_set (T_SELECT); g_view->home ();
}
static void cmd_open ()
{
	char path[200];
	if (!guard ()) return;
	if (A.sketching) sketch_end (false);
	if (uk_file_open (path, sizeof path, DOCS) && !load_path (path)) uk_messagebox ("Open", "That file is not a 3DForge part (.3df).", MB_OK);
	A.caption[0] = 0; ui (R_ALL);
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
static Modal *g_export;
static void cmd_export ()
{
	if (A.sketching || A.camMode) return;
	ExportDialog d; Root *rt = Root::current (); d.left = (rt->width - d.width) / 2; d.top = (rt->height - d.height) / 2;
	g_export = &d; int r = d.run (); g_export = 0;
	if (r == 1) set_hint ("Exported."); ui (R_ALL);
}

// ---- G-code: what was checked, how it starts, where it goes -----------------------------------------------------------------
class GcodeDialog : public Modal
{
public:
	Textbox *name, *folder; Button *browse; ABtn *save;
	std::string code; int lines; bool bad;
	static void button (Widget &w) { ((GcodeDialog *) w.parent)->onButton (w.tag); }
	GcodeDialog () : Modal (560, 452), lines (0)
	{
		int x = 20, w = width - 40;
		char def[80]; snprintf (def, sizeof def, "%s", A.path[0] ? base_name (A.path) : "part"); char *dot = strrchr (def, '.'); if (dot) *dot = 0;
		code = cam_gcode (A.job, A.paths, A.path[0] ? base_name (A.path) : "part", &lines);
		bad = A.paths.hitFast || A.paths.gouge || A.paths.moves.empty ();
		name = new Textbox (x + 70, height - 122, 200, 28, def); addChild (name);
		folder = new Textbox (x + 70, height - 88, w - 70 - 86, 28, DOCS); folder->maxLen = 180; addChild (folder);
		browse = new Button (x + w - 78, height - 88, 78, 28, "Browse...", button); browse->tag = 2; addChild (browse);
		ABtn *c = new ABtn (width - 220, height - 44, 92, 28, "Cancel", 0); c->bg = C_FACE; addChild (c);
		save = new ABtn (width - 118, height - 44, 98, 28, "Save", 1, true); save->bg = C_FACE; addChild (save);
		if (bad) save->hidden = true;
	}
	void onButton (int tag) override
	{
		if (tag == 2) { char p[200]; if (uk_folder_open (p, sizeof p, folder->text)) { folder->setText (p); invalidate (true); } return; }
		if (tag == 0 || bad) { close (0); return; }
		char path[300]; kapi_mkdir (folder->text);
		snprintf (path, sizeof path, "%s/%s%s", folder->text, name->text, ends_with (name->text, ".nc") || ends_with (name->text, ".gcode") ? "" : ".nc");
		if (kapi_save_file (path, code.data (), (unsigned) code.size ()) < 0) { uk_messagebox ("G-code", "The file could not be written.", MB_OK); return; }
		close (1);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { onButton (1); return true; } return false; }
	void onDraw () override
	{
		drawBox ("G-code"); int x = 20, w = width - 40, y = titleH () + 14; char t[160], a[24], b[24], c[24];
		const CamPaths &p = A.paths; const CamSetup &s = A.job;
		V3 o = cam_origin (s, p.lo, p.hi);
		auto row = [&] (int state, const char *txt)		// 0 right, 1 to look at, 2 wrong: nothing is written
		{
			icon (canvas, state ? I_WARN : I_CHECK, x + 2, y, 17, state == 2 ? 0xB03A30 : state ? 0x4A3A10 : C_GREEN, C_ACCENT, C_FACE);
			uk_text (canvas, x + 28, y + (17 - uk_fh ()) / 2, txt, state == 2 ? 0xB03A30 : C_TEXT, state == 2 ? 2 : 0); y += 23;
		};
		if (p.moves.empty ()) row (2, "There is no operation to write yet.");
		else
		{
			row (p.hitFast ? 2 : 0, p.hitFast ? "A fast move goes through matter." : "No fast move goes through matter.");
			row (p.gouge ? 2 : 0, p.gouge ? "The tool cuts into the body." : "The body is never cut into.");
			fmt (p.lowestZ - o.z, a, 12); fmt (p.lowestZ - p.lo.z, b, 12);
			if (p.lowestZ < p.lo.z - 1e-6) { fmt (p.lo.z - p.lowestZ, b, 12); snprintf (t, sizeof t, "The lowest point is Z %s: %s mm under the stock (a spoil board).", a, b); }
			else snprintf (t, sizeof t, "The lowest point is Z %s: %s mm above the stock's underside.", a, b);
			row (p.lowestZ < p.lo.z - 1e-6 ? 1 : 0, t);
			fmt (s.machine.tx, a, 12); fmt (s.machine.ty, b, 12); fmt (s.machine.tz, c, 12);
			snprintf (t, sizeof t, p.outside ? "Larger than the machine's travel (%s \xC3\x97 %s \xC3\x97 %s mm)." : "Within the machine's travel (%s \xC3\x97 %s \xC3\x97 %s mm).", a, b, c); row (p.outside ? 1 : 0, t);
			fmt (s.tool.flute, a, 12);
			snprintf (t, sizeof t, p.tooDeep ? "Deeper than the tool's cutting length (%s mm)." : "Within the tool's cutting length (%s mm).", a); row (p.tooDeep ? 1 : 0, t);
		}
		y += 4; int bh = height - 136 - y - 30;
		uk_sunken (canvas, x, y, w, bh, 5, C_FIELD);
		{
			UkFaceScope fs (g_small); int ly = y + 6; size_t i = 0;
			while (i < code.size () && ly + uk_fh () < y + bh - 2)
			{
				size_t e = code.find ('\n', i); if (e == std::string::npos) e = code.size ();
				char line[96]; size_t n = e - i < sizeof line - 1 ? e - i : sizeof line - 1; memcpy (line, code.data () + i, n); line[n] = 0;
				uk_text (canvas, x + 10, ly, line, line[0] == '(' ? dim_col () : C_FIELD_TEXT); ly += uk_fh () + 1; i = e + 1;
			}
		}
		y += bh + 8;
		{
			UkFaceScope fs (g_small); int h = (int) (p.minutes / 60), m = (int) (p.minutes + 0.5) % 60;
			if (h) snprintf (a, sizeof a, "%d h %02d", h, m); else snprintf (a, sizeof a, "%d min", m);
			snprintf (t, sizeof t, "%d lines \xC2\xB7 %u KB \xC2\xB7 %.1f m cut \xC2\xB7 about %s \xC2\xB7 %s, %d rpm", lines, (unsigned) (code.size () + 1023) / 1024, p.length / 1000, a, s.tool.name, (int) s.tool.rpm);
			uk_text (canvas, x, y, t, dim_col ());
		}
		uk_text (canvas, x, height - 122 + (28 - uk_fh ()) / 2, "Name", C_TEXT); uk_text (canvas, x, height - 88 + (28 - uk_fh ()) / 2, "Folder", C_TEXT);
		{ UkFaceScope fs (g_small); uk_text (canvas, x + 280, height - 122 + (28 - uk_fh ()) / 2, bad ? "Nothing is written until this is put right." : ".nc \xC2\xB7 GRBL, millimetres \xC2\xB7 try it in the air first", bad ? 0xB03A30 : dim_col ()); }
	}
};
static void cmd_gcode ()
{
	if (!A.camMode) return;
	if (A.camDirty) cam_refresh ();
	GcodeDialog d; Root *rt = Root::current (); d.left = (rt->width - d.width) / 2; d.top = (rt->height - d.height) / 2;
	g_export = &d; int r = d.run (); g_export = 0;
	if (r == 1) set_hint ("G-code written. Run it in the air first, the spindle well above the stock."); ui (R_ALL);
}
// ---- the print file: what was checked, where it goes ------------------------------------------------------------------------
class PrintDialog : public Modal
{
public:
	Textbox *name, *folder; Button *browse; ABtn *save; bool bad;
	static void button (Widget &w) { ((PrintDialog *) w.parent)->onButton (w.tag); }
	PrintDialog () : Modal (520, 330)
	{
		int x = 20, w = width - 40;
		char def[80]; snprintf (def, sizeof def, "%s", A.path[0] ? base_name (A.path) : "part"); char *dot = strrchr (def, '.'); if (dot) *dot = 0;
		bad = A.pjob.tooLarge || !A.pjob.onPlate || A.pjob.file.layers.empty ();
		name = new Textbox (x + 70, height - 122, 200, 28, def); addChild (name);
		folder = new Textbox (x + 70, height - 88, w - 70 - 86, 28, DOCS); folder->maxLen = 180; addChild (folder);
		browse = new Button (x + w - 78, height - 88, 78, 28, "Browse...", button); browse->tag = 2; addChild (browse);
		ABtn *c = new ABtn (width - 220, height - 44, 92, 28, "Cancel", 0); c->bg = C_FACE; addChild (c);
		save = new ABtn (width - 118, height - 44, 98, 28, "Save", 1, true); save->bg = C_FACE; addChild (save);
		if (bad) save->hidden = true;
	}
	void onButton (int tag) override
	{
		if (tag == 2) { char p[200]; if (uk_folder_open (p, sizeof p, folder->text)) { folder->setText (p); invalidate (true); } return; }
		if (tag == 0 || bad) { close (0); return; }
		char path[300], ext[12]; snprintf (ext, sizeof ext, ".%s", A.print.machine.ext); kapi_mkdir (folder->text);
		snprintf (path, sizeof path, "%s/%s%s", folder->text, name->text, ends_with (name->text, ext) ? "" : ext);
		std::string bytes = PRINT_FORMATS[0].write (A.pjob.file);
		if (kapi_save_file (path, bytes.data (), (unsigned) bytes.size ()) < 0) { uk_messagebox ("Print file", "The file could not be written.", MB_OK); return; }
		close (1);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { onButton (1); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Print file"); int x = 20, y = titleH () + 14; char t[160], a[24]; const PrintJob &j = A.pjob;
		auto row = [&] (int state, const char *txt)
		{
			icon (canvas, state ? I_WARN : I_CHECK, x + 2, y, 17, state == 2 ? 0xB03A30 : state ? 0x4A3A10 : C_GREEN, C_ACCENT, C_FACE);
			uk_text (canvas, x + 28, y + (17 - uk_fh ()) / 2, txt, state == 2 ? 0xB03A30 : C_TEXT, state == 2 ? 2 : 0); y += 23;
		};
		row (j.tooLarge ? 2 : 0, j.tooLarge ? "It does not fit the printer." : "It fits the plate and the room.");
		row (j.onPlate ? 0 : 2, j.onPlate ? "The first layers lie on the plate." : "Nothing lies on the plate: lower the body, or give it supports.");
		if (j.islands) { fmt (j.islandAt, a, 12); snprintf (t, sizeof t, "%d part%s in mid-air, the first at %s mm: it needs a support.", j.islands, j.islands > 1 ? "s start" : " starts", a); row (1, t); }
		else row (0, "Each layer rests on the one before.");
		y += 6;
		{
			UkFaceScope fs (g_small); int mn = (int) (j.minutes + 0.5); if (mn >= 60) snprintf (a, sizeof a, "%d h %02d", mn / 60, mn % 60); else snprintf (a, sizeof a, "%d min", mn);
			snprintf (t, sizeof t, "%d layers of %.3g mm \xC2\xB7 %.1f ml of resin \xC2\xB7 about %s", (int) j.file.layers.size (), A.print.resin.layer, j.volume, a); uk_text (canvas, x, y, t, dim_col ()); y += 17;
			snprintf (t, sizeof t, "%s \xC2\xB7 %s \xC2\xB7 %.3g s a layer, %.3g s the first %d", A.print.machine.name, A.print.resin.name, A.print.resin.exposure, A.print.resin.bottomExposure, (int) (A.print.resin.bottomLayers + 0.5));
			uk_text (canvas, x, y, t, dim_col ());
		}
		uk_text (canvas, x, height - 122 + (28 - uk_fh ()) / 2, "Name", C_TEXT); uk_text (canvas, x, height - 88 + (28 - uk_fh ()) / 2, "Folder", C_TEXT);
		{ UkFaceScope fs (g_small); snprintf (t, sizeof t, bad ? "Nothing is written until this is put right." : ".%s", A.print.machine.ext); uk_text (canvas, x + 280, height - 122 + (28 - uk_fh ()) / 2, t, bad ? 0xB03A30 : dim_col ()); }
	}
};
static void print_file_dialog ()
{
	PrintDialog d; Root *rt = Root::current (); d.left = (rt->width - d.width) / 2; d.top = (rt->height - d.height) / 2;
	g_export = &d; int r = d.run (); g_export = 0;
	if (r == 1) set_hint ("The print file is written."); ui (R_ALL);
}
// The file asked: once the layers are cut.
static void cmd_print_file ()
{
	if (!resin ()) return;
	if (A.printDirty) print_refresh ();
	if (A.sliced) { print_file_dialog (); return; }
	A.wantFile = true; print_slice_start (); set_hint ("Cutting the layers..."); ui (0);
}
// What Manufacture makes: a router's G-code, or the layers of one of the resin printers.
static void cmd_gen ()
{
	if (!A.camMode) return;
	std::string ini; read_text (PRINTERS_INI, ini); std::vector<PrintMachine> ms = print_machines (ini.c_str ());
	PopupMenu pm (g_props->left + 110, g_props->top + 112); char t[80];
	pm.add ("Milling \xC2\xB7 G-code for a router (GRBL)", 2000);
	pm.add ("Filament printing \xC2\xB7 the nozzle's path", 1999);
	for (size_t i = 0; i < ms.size () && i < 30; i++) { snprintf (t, sizeof t, "Resin printing \xC2\xB7 %s", ms[i].name); pm.add (t, 2001 + (int) i); }
	int c = pm.run ();
	if (c == 2000) gen_set (0);
	else if (c == 1999) gen_set (2);
	else if (c > 2000 && c - 2001 < (int) ms.size ())
	{
		bool other = strcmp (A.print.machine.name, ms[c - 2001].name) != 0; A.print.machine = ms[c - 2001];
		if (other) print_presets_save ();
		gen_set (1);
	}
	if (g_view) g_view->fit ();
}
static void cmd_print_preset ()
{
	PopupMenu pm (g_props->left + 12, g_props->top + 92);
	for (int i = 0; i < 3; i++) pm.add (PRINT_RESINS[i].name, 3000 + i);
	int c = pm.run ();
	if (c >= 3000 && c < 3003) { A.print.resin = PRINT_RESINS[c - 3000]; print_presets_save (); print_touch (); ui (R_ALL); }
}
// Which body is cut: one of the part's.
static void cmd_cam_body ()
{
	PopupMenu pm (g_props->left + 12, g_props->top + 112);
	for (size_t i = 0; i < A.doc.bodies.size () && i < 40; i++) pm.add (body_name (A.doc.bodies[i].id), 1000 + (int) i);
	int c = pm.run ();
	if (c >= 1000 && c - 1000 < (int) A.doc.bodies.size () && A.gen >= 1) { A.print.body = A.doc.bodies[c - 1000].id; A.print.tips.clear (); A.doc.changes++; print_refresh (); if (g_view) g_view->fit (); ui (R_ALL); return; }
	if (c >= 1000 && c - 1000 < (int) A.doc.bodies.size ()) { A.job.body = A.doc.bodies[c - 1000].id; A.doc.changes++; cam_refresh (); if (g_view) g_view->fit (); ui (R_ALL); }
}

// ---- the commands ---------------------------------------------------------------------------------------------------------
// Enter, or OK: the step being made goes on, or is done.
static void confirm ()
{
	if (A.sketching)
	{
		SkEl &e = A.cur;
		if (A.selEl >= 0 || A.curStep == 0) return;
		if (e.kind == SK_SPLINE) { sk_spline_end (); return; }
		if (e.kind == SK_ARC && A.tool == T_ARC3 && A.curStep == 1) return;
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
	if (printer ()) { if (A.layerPlay) { A.layerPlay = false; A.fdmDrawn = -1; g_view->invalidate (true); ui (0); } return; }
	if (A.camMode) { if (A.camSim) { A.camSim = false; ui (R_ALL); } else cam_revert (); return; }
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
	case CMD_UNDO: if (!A.camMode) do_undo (false); break;
	case CMD_REDO: if (!A.camMode) do_undo (true); break;
	case CMD_MODE_DESIGN: cam_enter (false); break;
	case CMD_MODE_CAM: cam_enter (true); if (A.camMode) g_view->fit (); break;
	case CMD_PR_SETUP: print_page (0); break;
	case CMD_PR_RESIN: print_page (1); break;
	case CMD_PR_SUPPORTS: print_page (2); break;
	case CMD_PR_LAYERS: print_page (3); break;
	case CMD_PR_FILE: cmd_print_file (); break;
	case CMD_PR_GENERATE:
		if (!resin ()) break;
		if (A.printDirty) print_refresh ();
		print_supports_auto (A.placed, A.print, A.print.tips); A.doc.changes++; print_refresh ();
		if (A.print.tips.empty ()) set_hint ("Nothing hangs: no support is needed as the body is.");
		ui (R_ALL); break;
	case CMD_PR_CLEAR: if (resin ()) { A.print.tips.clear (); A.doc.changes++; print_refresh (); ui (R_ALL); } break;
	case CMD_PR_PRESET: if (resin ()) cmd_print_preset (); break;
	case CMD_GEN: cmd_gen (); break;
	case CMD_CAM_VALIDATE: if (A.camMode && A.gen == 0) { cam_refresh (); ui (R_ALL); } break;
	case CMD_CAM_REVERT: cam_revert (); break;
	case CMD_CAM_SETUP: if (A.camMode) { A.camSel = 0; A.camPage = 0; A.camSim = false; cam_hint (); ui (R_ALL); } break;
	case CMD_CAM_TOOL: if (A.camMode) { A.camPage = 1; A.camSim = false; cam_hint (); ui (R_ALL); } break;
	case CMD_CAM_CLEAR: if (A.camMode) cam_add (CAM_CLEAR); break;
	case CMD_CAM_CONTOUR: if (A.camMode) cam_add (CAM_CONTOUR); break;
	case CMD_CAM_SIM:
		if (!A.camMode) break;
		if (A.camDirty) cam_refresh ();
		if (A.paths.hm.empty ()) { set_hint ("Add a clearing or a contour first: there is nothing to simulate."); ui (0); break; }
		A.camSim = !A.camSim; A.simPlay = false; if (A.camSim) sim_to (A.paths.moves.size ());
		set_hint (A.camSim ? "What is left of the stock. Play (at the left) runs the cut; drag the bar to go through it. Esc: back to the moves." : ""); if (!A.camSim) cam_hint ();
		ui (R_ALL); break;
	case CMD_GCODE: if (A.gen == 1) cmd_print_file (); else if (A.gen == 0) cmd_gcode (); break;
	case CMD_CAM_BODY: if (A.camMode) cmd_cam_body (); break;
	case CMD_CAM_DELETE: if (A.camMode) cam_delete_op (); break;
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
	case CMD_DELETE: if (printer ()) break; if (A.camMode) cam_delete_op (); else if (!A.sketching && A.selFeat >= 0) delete_feature (A.selFeat); break;
	case CMD_DEL_ELEMENT:
		if (A.sketching && A.selEl >= 0 && A.selEl < (int) A.sk.els.size ()) { A.sk.els.erase (A.sk.els.begin () + A.selEl); A.selEl = -1; sk_eval (); A.chain = false; sk_arm (); ui (R_ALL); }
		break;
	case CMD_EDIT_SKETCH:
		if (!A.camMode && !A.sketching && A.selFeat >= 0 && A.doc.feats[A.selFeat].kind == F_SKETCH) sketch_begin (A.doc.feats[A.selFeat].pl, A.doc.feats[A.selFeat].target, A.selFeat);
		break;
	case CMD_ROLL_HERE: if (!A.camMode && A.selFeat >= 0) { A.doc.upto = A.selFeat + 1; A.doc.touch (); ui (R_ALL); } break;
	case CMD_ROLL_END: if (A.camMode) break; A.doc.upto = (int) A.doc.feats.size (); A.doc.touch (); ui (R_ALL); break;
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
		// (Manufacture: the moves made again once the values have stopped changing for a moment)
		if (A.camMode && A.gen == 0 && A.camSim && A.simPlay && kapi_get_ticks () - A.simT >= 3)		// (the cut played)
		{
			size_t n = A.paths.moves.size (), by = n / 300 < 1 ? 1 : n / 300; A.simT = kapi_get_ticks ();
			sim_to (A.simAt + by); if (A.simAt >= n) A.simPlay = false;
			g_view->invalidate (true);
		}
		if (filament ())	// (a filament printer's: the body put again, its path made again; the path played -- a layer drawn, then the next)
		{
			if (A.printDirty && kapi_get_ticks () - A.camDirtyT > 40) { print_refresh (); if (A.camPage == 3) fdm_start (); ui (0); g_view->invalidate (true); }
			if (A.layerPlay && A.fdmReady && A.camPage == 3 && kapi_get_ticks () - A.layerT >= 2)
			{
				int n = (int) A.fjob.layers.size (); A.layerT = kapi_get_ticks ();
				double total = A.layer < n ? fdm_layer_length (A.fjob.layers[A.layer]) : 0, by = total / 50 < 6 ? 6 : total / 50;
				if (A.fdmDrawn < 0) A.fdmDrawn = 0;
				A.fdmDrawn += by;
				if (A.fdmDrawn >= total) { if (A.layer < n - 1) { A.layer++; A.fdmDrawn = 0; ui (0); } else { A.layerPlay = false; A.fdmDrawn = -1; ui (0); } }
				g_view->invalidate (true);
			}
		}
		if (resin ())		// (a resin printer's: the body put again; the layers cut a few at a turn; the layers played)
		{
			if (A.printDirty && kapi_get_ticks () - A.camDirtyT > 40) { print_refresh (); if (A.camPage == 3) print_slice_start (); ui (0); g_view->invalidate (true); }
			if (A.slicer.running ())
			{
				// (layers for about three hundredths of a second, then the window lives again)
				unsigned t0 = kapi_get_ticks (); bool done = false;
				for (int k = 0; k < 60 && !done; k++) { done = A.slicer.step (1); if (kapi_get_ticks () - t0 >= 3) break; }
				if (done)
				{
					A.sliced = true; int n = (int) A.pjob.file.layers.size (); if (A.layer > n - 1) A.layer = n - 1;
					if (A.wantFile) { A.wantFile = false; print_hint (); ui (0); g_view->invalidate (true); print_file_dialog (); }
					else print_hint ();
				}
				ui (0); g_view->invalidate (true);
			}
			else if (A.layerPlay && A.sliced && kapi_get_ticks () - A.layerT >= 4)
			{
				int n = (int) A.pjob.file.layers.size (); A.layerT = kapi_get_ticks (); A.layer += n > 300 ? n / 150 : 1;
				if (A.layer >= n - 1) { A.layer = n - 1; A.layerPlay = false; }
				ui (0); g_view->invalidate (true);
			}
		}
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
	menu.menu ("Manufacture");
	menu.item ("Design", "", 0, [] { cmd (CMD_MODE_DESIGN); });
	menu.item ("Manufacture", "", 0, [] { cmd (CMD_MODE_CAM); });
	menu.separator ();
	menu.item ("Setup", "", 0, [] { cmd (CMD_CAM_SETUP); });
	menu.item ("Tool and Machine", "", 0, [] { cmd (CMD_CAM_TOOL); });
	menu.separator ();
	menu.item ("New Clearing", "", 0, [] { cmd (CMD_CAM_CLEAR); });
	menu.item ("New Contour", "", 0, [] { cmd (CMD_CAM_CONTOUR); });
	menu.separator ();
	menu.item ("Simulate", "", 0, [] { cmd (CMD_CAM_SIM); });
	menu.item ("G-code / Print File...", "^G", UK_CTRL ('G'), [] { cmd (CMD_GCODE); });
	menu.separator ();
	menu.item ("Process: Milling", "", 0, [] { gen_set (0); if (A.camMode) g_view->fit (); });
	menu.item ("Process: Resin Printing", "", 0, [] { gen_set (1); if (A.camMode) g_view->fit (); });
	menu.item ("Supports", "", 0, [] { cmd (CMD_PR_SUPPORTS); });
	menu.item ("Generate Supports", "", 0, [] { cmd (CMD_PR_GENERATE); });
	menu.item ("Layers", "", 0, [] { cmd (CMD_PR_LAYERS); });
	menu.item ("Process: Filament Printing", "", 0, [] { gen_set (2); if (A.camMode) g_view->fit (); });
	menu.item ("Filament", "", 0, [] { cmd (CMD_PR_RESIN); });
	menu.menu ("Help");
	menu.item ("About 3DForge", "", 0, [] { uk_messagebox ("3DForge", "A small parametric CAD for Onyx.\nGeometry: Manifold (Apache-2.0), Clipper2 (BSL-1.0).", MB_OK); });
	menu.publish ();

	job_reset (); tool_set (T_SELECT);
	char args[200]; int n = kapi_get_args (args, sizeof args);
	if (n > 0 && args[0] && !load_path (args)) uk_messagebox ("Open", "That file is not a 3DForge part (.3df).", MB_OK);
	root.fitWorkArea ();
	root.run ();
	return 0;
}
