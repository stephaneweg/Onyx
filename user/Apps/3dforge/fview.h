//
// 3dforge/fview.h -- the tools and the view. The App holds what the user is doing: the tool and its step, the step
// being made (not yet in the history) and its preview, the sketch being drawn, what is selected. The View widget
// shows the bodies (frender.h: the GPU), turns, pans and zooms, and gives its clicks to the tool:
//
//   The shapes (the bar's Shapes button unfolds them). Each can also be made without the pointer: once chosen, its
//   values and its place are fields at the right, a ghost shows it, OK makes it.
//   Box       click (a face, or the ground), move away, click, move up, click
//   Cylinder  click its centre, its radius, its depth
//   Pyramid, Prism   click the centre, give the base's sides (4), move away for its radius, click, move for the height
//   Taper     the same with one more move and click after the base's radius: the top's
//   Torus     click the centre, move away for the ring's radius, click, move off the ring for the tube's, click
//   Sphere    click its centre, move away for its radius, click
//   Sketch    click a flat face (or the ground) -- or choose a plane of the axes (XY, XZ, YZ) and its offset at the
//             right: the view turns to it, the tools become Line, Rectangle, Circle, Arc
//   Extrude   the sketch (the one selected, else the last), then its height
//   Fillet, Chamfer   click edges (straight ones, circles), give the size
//   Move      click a body, move, click; its fields also turn it (around X, Y, Z, about its centre) and scale it
//   Union, Subtract, Intersect   click a body, then the other
//
// Every value is also a field at the right (fui.h): typed there, the pointer no longer changes it.
//
// MIT licence (Onyx).
//
#ifndef _3dforge_fview_h
#define _3dforge_fview_h

#include "frender.h"
#include "ficons.h"
#include "fcam.h"

namespace forge {

enum { T_SELECT, T_BOX, T_CYL, T_SKETCH, T_EXTRUDE, T_FILLET, T_CHAMFER, T_MOVE, T_UNION, T_SUB, T_INT, T_MEASURE,
       T_PYRAMID, T_PRISM, T_TAPER, T_TORUS, T_SPHERE,
       T_LINE, T_RECT, T_CIRCLE, T_ARC, T_ARC3, T_SPLINE, T_POINT };		// (from T_LINE on: the sketch's)
static inline bool shape_tool (int t) { return t == T_BOX || t == T_CYL || (t >= T_PYRAMID && t <= T_SPHERE); }
enum { R_FIELDS = 1, R_PANELS = 2, R_ALL = 3 };		// what the window must show again

static TextFace *g_small, *g_big, *g_tiny;		// (11 px; 15 px, used bold; 9 px: the cube's faces)
static unsigned ACC () { return C_ACCENT; }
static const unsigned C_AMBER = 0xE8962C, C_RED = 0xE0524A, C_GREEN = 0x3C9650, C_LINE = 0x1E5CBA;

struct App
{
	Doc doc;
	int tool, step, lastShape;		// (lastShape: the one the bar's "more shapes" tile shows)
	Feature pend; bool hasPend, opAuto;	// the step being made
	RMesh prev; bool hasPrev; const char *prevErr;
	bool typed[4];				// a value typed in its field: the pointer leaves it alone
	// the sketch being drawn
	bool sketching; int skEdit; Feature sk; SkEval ev;
	int skPlane; double skOffset;		// the Sketch tool's plane, chosen at the right: 0 XY, 1 XZ, 2 YZ; moved by
	SkEl cur; int curStep; bool chain; double sweep0;
	bool snapOn = false; V2 snapP;		// (the sketch's pointer is held by a point: shown)
	bool ghost = false;			// (a shape not yet clicked: shown once one of its values was typed)
	V2 arcC, arcM; bool rectCentre = false;	// (an arc being placed: its centre, or its end and its middle; rectangles from their centre)
	// what is selected, what the pointer is on
	int selBody, selFeat, selEl;
	int hovBody, hovFace, hovChain;
	bool showEdges, showGrid, seeThrough, snap;
	Cam cam, camSaved;
	V3 mA, mB; int mN;			// Measure's points
	std::vector<std::string> undo, redo;
	// Manufacture (fcam.h): the setup and its operations, their moves; what the panel shows -- camSel: 0 the setup,
	// k: its operation k - 1; camPage: 0 the setup, 1 the tool, 2 the operation
	CamSetup job; CamPaths paths; RMesh stockMesh;
	bool camMode = false, camSim = false, camDirty = false; int camSel = 0, camPage = 0; unsigned camDirtyT = 0;
	char hint[200], path[200], caption[96];
	unsigned saved; bool gpu;
	void (*refresh) (int what);
	App () : tool (T_SELECT), step (0), lastShape (T_PYRAMID), hasPend (false), opAuto (true), hasPrev (false), prevErr (0), sketching (false), skEdit (-1), skPlane (0), skOffset (0),
		 curStep (0), chain (false), sweep0 (0), selBody (-1), selFeat (-1), selEl (-1), hovBody (-1), hovFace (-1), hovChain (-1),
		 showEdges (true), showGrid (true), seeThrough (false), snap (true), mN (0), saved (0), gpu (false), refresh (0)
	{ hint[0] = path[0] = caption[0] = 0; memset (typed, 0, sizeof typed); }
};
static App A;

static void fmt (double v, char *out, int cap = 24)
{
	if (fabs (v) < 5e-7) v = 0;
	snprintf (out, cap, "%.3f", v);
	char *e = out + strlen (out) - 1;
	while (e > out && *e == '0') *e-- = 0;
	if (e > out && *e == '.') *e = 0;
}
static double snapv (double v, double step = 1) { return A.snap ? floor (v / step + 0.5) * step : v; }
static void set_hint (const char *s) { snprintf (A.hint, sizeof A.hint, "%s", s); }
static void ui (int what) { if (A.refresh) A.refresh (what); }
static const char *body_name (int id) { for (BodyProp &p : A.doc.props) if (p.id == id) return p.name; return "the body"; }
static bool body_shown (int id) { for (BodyProp &p : A.doc.props) if (p.id == id) return p.visible; return true; }

// ---- undo: the whole document, as its file's text --------------------------------------------------------------------
static void undo_push ()
{
	A.undo.push_back (A.doc.save ()); if (A.undo.size () > 50) A.undo.erase (A.undo.begin ());
	A.redo.clear ();
}
static void tool_set (int t);
static void do_undo (bool redo)
{
	std::vector<std::string> &from = redo ? A.redo : A.undo, &to = redo ? A.undo : A.redo;
	if (from.empty () || A.sketching) return;
	to.push_back (A.doc.save ()); std::string s = from.back (); from.pop_back ();
	unsigned ch = A.doc.changes + 1; A.doc.load (s.c_str ()); A.doc.changes = ch;
	A.selFeat = A.selBody = -1; tool_set (T_SELECT);
}

// ---- the step being made ---------------------------------------------------------------------------------------------
// The step at which a shape's height is pulled (a tapered prism has its top's radius before).
static int height_step () { return A.pend.kind == F_TAPER ? 3 : A.pend.kind == F_SPHERE ? 1 : 2; }
static void preview_update ()
{
	A.hasPrev = false; A.prevErr = 0;
	if (!A.hasPend) return;
	Feature &f = A.pend; const char *why = "";
	if (f.kind == F_FILLET || f.kind == F_CHAMFER)
	{
		if (f.edges.empty ()) return;
		std::vector<Manifold> adds, cuts;
		if (!A.doc.corners (f, &adds, &cuts, &why)) { A.prevErr = why; return; }
		Manifold all; for (Manifold &m : adds) all += m; for (Manifold &m : cuts) all += m;
		build_mesh (all, A.prev); A.hasPrev = true; return;
	}
	if (f.kind == F_MOVE)				// (the body where it would be: a ghost)
	{
		Body *b = A.doc.body (f.target);
		if (!b || A.step < 1) return;
		if (fabs (f.sc.x) < 1e-6 || fabs (f.sc.y) < 1e-6 || fabs (f.sc.z) < 1e-6) { A.prevErr = "A scale of 0 would leave nothing"; return; }
		build_mesh (moved (*b, f), A.prev); A.hasPrev = true; return;
	}
	if (makes_solid (f.kind) && (A.step >= height_step () || (A.step == 0 && f.kind != F_EXTRUDE && A.ghost)))
	{
		Manifold m;
		if (!A.doc.solid (f, &m, &why)) { if (*why) A.prevErr = why; return; }
		build_mesh (m, A.prev); A.hasPrev = true;
	}
}
static void auto_op ()
{
	if (!A.opAuto) return;
	Feature &f = A.pend;
	if (f.target >= 0 && A.doc.body (f.target)) f.op = (f.through || f.h < 0) ? OP_SUB : OP_UNION;
	else f.op = OP_NEW;
}
static void caption_set ()
{
	A.caption[0] = 0;
	if (A.sketching) { snprintf (A.caption, sizeof A.caption, "%s \xC2\xB7 %s", A.sk.name, A.sk.target >= 0 ? "on a face" : "on the ground"); return; }
	if (!A.hasPend || A.step == 0) return;
	Feature &f = A.pend; char nm[40]; snprintf (nm, sizeof nm, "%s %d", KIND_NAME[f.kind], A.doc.count (f.kind, (int) A.doc.feats.size ()) + 1);
	if (f.kind == F_FILLET || f.kind == F_CHAMFER) snprintf (A.caption, sizeof A.caption, "%s \xC2\xB7 %d edge%s", nm, (int) f.edges.size (), f.edges.size () == 1 ? "" : "s");
	else if (f.kind == F_BOX || round_kind (f.kind))
	{
		if (f.target >= 0) snprintf (A.caption, sizeof A.caption, "%s \xC2\xB7 on a face of %s", nm, body_name (f.target));
		else snprintf (A.caption, sizeof A.caption, "%s \xC2\xB7 on the ground", nm);
	}
	else snprintf (A.caption, sizeof A.caption, "%s", nm);
}
static void sk_eval () { sketch_eval (A.sk.els, A.doc.segs, A.ev); }

static void tool_hint ()
{
	switch (A.tool)
	{
	case T_SELECT: set_hint (A.doc.feats.empty () ? "Pick a tool to start: Box, Cylinder or Sketch. Drag turns the view, the wheel zooms."
						     : "Pick a tool, or click a body, a face or an edge. Drag turns the view, the wheel zooms."); break;
	case T_BOX: set_hint (A.step == 0 ? "Click the centre of the box (a face, or the ground) \xE2\x80\x94 or set its values at the right and press OK." : A.step == 1 ? "Move away to set the width and the depth, then click."
			      : "Move up to set the height and click \xE2\x80\x94 or type it and press Enter."); break;
	case T_CYL: set_hint (A.step == 0 ? "Click the centre (a face, or the ground) \xE2\x80\x94 or set its values at the right and press OK." : A.step == 1 ? "Move away to set the diameter, then click."
			      : A.pend.op == OP_SUB ? "Pushed into the body: Subtract is chosen. Click to finish." : "Move to set the height and click \xE2\x80\x94 or type it and press Enter."); break;
	case T_PYRAMID: case T_PRISM: case T_TAPER:
		set_hint (A.step == 0 ? "Click the centre of the base (a face, or the ground) \xE2\x80\x94 or set its values at the right and press OK." : A.step == 1 ? "Type the number of sides (0: round), move away to set the base's radius, then click."
			  : A.step < height_step () ? "Move to set the top's radius (its distance from the centre), then click."
			  : "Move up or down to set the height and click \xE2\x80\x94 or type it and press Enter."); break;
	case T_TORUS: set_hint (A.step == 0 ? "Click the centre (a face, or the ground) \xE2\x80\x94 or set its values at the right and press OK." : A.step == 1 ? "Move away to set the ring's radius, then click."
				: "Move off the ring to set the tube's radius and click \xE2\x80\x94 or type it and press Enter."); break;
	case T_SPHERE: set_hint (A.step == 0 ? "Click the centre (a face, or the ground) \xE2\x80\x94 or set its radius at the right and press OK." : "Move away to set the radius and click \xE2\x80\x94 or type it and press Enter."); break;
	case T_SKETCH: set_hint ("Click the flat face to draw on \xE2\x80\x94 or choose a plane at the right and start."); break;
	case T_EXTRUDE: set_hint ("Move to set the height and click \xE2\x80\x94 or type it and press Enter."); break;
	case T_FILLET: case T_CHAMFER: set_hint ("Click the edges, then drag the arrow or type the size. Enter to finish."); break;
	case T_MOVE: set_hint (A.step == 0 ? "Click the body to move, turn or scale." : "Move it and click \xE2\x80\x94 or type its move, its turns and its scale at the right, then Enter."); break;
	case T_UNION: case T_SUB: case T_INT: set_hint (A.step == 0 ? "Click the body to keep." : A.tool == T_SUB ? "Click the body to cut out of it." : "Click the other body."); break;
	case T_MEASURE: set_hint (A.mN == 2 ? "Click again to measure something else." : A.mN == 1 ? "Click the second point." : "Click two points to measure between them."); break;
	case T_LINE: set_hint (A.curStep == 0 ? "Line: click where it starts." : "Line: click where it ends \xE2\x80\x94 or type its length and angle. Esc ends the run."); break;
	case T_RECT: set_hint (A.rectCentre ? (A.curStep == 0 ? "Rectangle: click its centre." : "Rectangle: click a corner \xE2\x80\x94 or type its width and height.")
					    : A.curStep == 0 ? "Rectangle: click a corner." : "Rectangle: click the opposite corner."); break;
	case T_CIRCLE: set_hint (A.curStep == 0 ? "Circle: click its centre." : "Circle: click to set its diameter."); break;
	case T_ARC: set_hint (A.curStep == 0 ? "Arc: click its centre." : A.curStep == 2 ? "Arc: click where it ends \xE2\x80\x94 or type its angle (+ clockwise)."
			      : A.cur.chain ? "Arc: click the centre. It starts at the end of the last element." : "Arc: click where it starts: that sets its radius."); break;
	case T_ARC3: set_hint (A.curStep == 0 ? "Arc: click where it starts." : A.curStep == 1 ? "Arc: click where it ends." : "Arc: move its middle and click \xE2\x80\x94 or type its radius."); break;
	case T_SPLINE: set_hint (A.curStep == 0 ? "Spline: click where it starts." : "Spline: click its points. Enter, or the last point clicked again, ends it."); break;
	case T_POINT: set_hint ("Point: click to place a mark to snap to. It is part of no outline."); break;
	}
}
// The element a sketch tool is about to place: chained to the run when there is one.
static void sk_arm ()
{
	A.cur = SkEl (); memset (A.typed, 0, sizeof A.typed); A.sweep0 = 0;
	A.cur.kind = A.tool == T_ARC || A.tool == T_ARC3 ? SK_ARC : A.tool == T_RECT ? SK_RECT : A.tool == T_CIRCLE ? SK_CIRCLE : A.tool == T_SPLINE ? SK_SPLINE
		     : A.tool == T_POINT ? SK_POINT : SK_LINE;
	bool chained = A.chain && A.ev.has && (A.tool == T_LINE || A.tool == T_ARC || A.tool == T_ARC3 || A.tool == T_SPLINE);
	A.cur.chain = chained; A.curStep = chained ? 1 : 0; A.cur.rel = A.tool == T_RECT && A.rectCentre;
	if (chained) { A.cur.x = A.ev.cur.x; A.cur.y = A.ev.cur.y; A.arcC = A.arcM = A.ev.cur; if (A.tool == T_SPLINE) A.cur.pts.assign (1, A.ev.cur); }
}
static void extrude_begin ()
{
	int sk = -1;
	if (A.selFeat >= 0 && A.selFeat < (int) A.doc.feats.size () && A.doc.feats[A.selFeat].kind == F_SKETCH) sk = A.selFeat;
	else for (int i = A.doc.upto - 1; i >= 0 && sk < 0; i--) if (A.doc.feats[i].kind == F_SKETCH && !A.doc.used (i)) sk = i;
	if (sk < 0) for (int i = A.doc.upto - 1; i >= 0 && sk < 0; i--) if (A.doc.feats[i].kind == F_SKETCH) sk = i;
	if (sk < 0) { A.tool = T_SELECT; set_hint ("Extrude needs a sketch: draw one first (Sketch)."); return; }
	Feature &f = A.pend; f.kind = F_EXTRUDE; f.sketch = sk; f.target = A.doc.body (A.doc.feats[sk].target) ? A.doc.feats[sk].target : -1;
	f.h = 10; A.hasPend = true; A.step = 2; auto_op (); preview_update ();
}
static void tool_set (int t)
{
	if (A.sketching && t < T_LINE) return;
	if (A.camMode && t != T_SELECT) return;			// (Manufacture has its own tools)
	A.tool = t; A.step = 0; A.ghost = false; A.hasPend = false; A.hasPrev = false; A.prevErr = 0; A.pend = Feature (); A.opAuto = true; A.mN = 0;
	A.hovChain = A.hovFace = A.hovBody = -1; memset (A.typed, 0, sizeof A.typed);
	// a shape: ready to be made as it is (its values at the right, a ghost at the origin), or drawn with the pointer
	Feature &p = A.pend;
	if (t == T_BOX) { p.kind = F_BOX; p.w = p.d = p.h = 20; }
	if (t == T_CYL) { p.kind = F_CYL; p.w = 20; p.h = 20; }
	if (t == T_PYRAMID) { p.kind = F_PYRAMID; p.n = 4; p.w = 10; p.h = 20; }
	if (t == T_PRISM) { p.kind = F_PRISM; p.n = 4; p.w = 10; p.h = 20; }
	if (t == T_TAPER) { p.kind = F_TAPER; p.n = 4; p.w = 10; p.d = 5; p.h = 20; }
	if (t == T_TORUS) { p.kind = F_TORUS; p.w = 15; p.d = 4; p.h = 1; }
	if (t == T_SPHERE) { p.kind = F_SPHERE; p.w = 10; p.h = 1; }
	if (shape_tool (t)) { A.hasPend = true; preview_update (); }
	if (t == T_EXTRUDE) extrude_begin ();
	if (t == T_FILLET || t == T_CHAMFER) { A.pend.kind = t == T_FILLET ? F_FILLET : F_CHAMFER; A.pend.r = t == T_FILLET ? 3 : 2; A.hasPend = true; A.step = 1; }
	if (t == T_MOVE) A.pend.kind = F_MOVE;
	if (t == T_UNION || t == T_SUB || t == T_INT) { A.pend.kind = F_COMBINE; A.pend.op = t == T_UNION ? OP_UNION : t == T_SUB ? OP_SUB : OP_INT; }
	if (A.sketching) sk_arm ();
	if (A.tool != T_SELECT || !A.hint[0] || t == T_SELECT) { if (!(t == T_EXTRUDE && A.tool == T_SELECT)) tool_hint (); }
	caption_set (); ui (R_ALL);
}
// The step made: into the history. false: it could not be done (the hint says why).
static bool commit_pend ()
{
	Feature f = A.pend; const char *why = "";
	if (makes_solid (f.kind)) { Manifold m; if (!A.doc.solid (f, &m, &why)) { set_hint (why); ui (R_PANELS); return false; } }
	if ((f.kind == F_FILLET || f.kind == F_CHAMFER) && f.edges.empty ()) { set_hint ("Click an edge first."); ui (R_PANELS); return false; }
	undo_push ();
	A.doc.upto = (int) A.doc.feats.size (); A.doc.name_new (f); A.doc.feats.push_back (f); A.doc.upto++;
	A.doc.touch ();
	Feature &g = A.doc.feats.back ();
	if (g.failed)
	{
		static char msg[96]; snprintf (msg, sizeof msg, "%s", g.err);
		A.doc.feats.pop_back (); A.doc.upto--; A.doc.rebuild (); A.undo.pop_back ();
		set_hint (msg); ui (R_PANELS); return false;
	}
	int idx = (int) A.doc.feats.size () - 1;
	A.selFeat = idx; A.selBody = A.doc.body (idx) ? idx : g.target;
	tool_set (T_SELECT);
	return true;
}
// A step removed: the steps after it are numbered again (a body is known by the step that made it).
static void delete_feature (int k)
{
	if (k < 0 || k >= (int) A.doc.feats.size ()) return;
	undo_push ();
	A.doc.feats.erase (A.doc.feats.begin () + k);
	auto fix = [k] (int &v) { if (v == k) v = -2; else if (v > k) v--; };
	for (Feature &f : A.doc.feats) { fix (f.target); fix (f.tool); fix (f.sketch); if (f.target == -2) f.target = -1; }
	for (size_t i = 0; i < A.doc.props.size (); i++)
	{
		if (A.doc.props[i].id == k) { A.doc.props.erase (A.doc.props.begin () + i); i--; }
		else if (A.doc.props[i].id > k) A.doc.props[i].id--;
	}
	if (A.doc.upto > k) A.doc.upto--;
	A.doc.touch (); A.selFeat = -1; A.selBody = -1; tool_set (T_SELECT);
}

// ---- the sketch --------------------------------------------------------------------------------------------------------
static void cam_face (const Plane &pl)
{
	A.cam.el = asin (pl.n.z > 1 ? 1 : pl.n.z < -1 ? -1 : pl.n.z) * 180 / PI;
	A.cam.az = fabs (pl.n.z) > 0.999 ? -90 : atan2 (pl.n.y, pl.n.x) * 180 / PI;
	A.cam.set (); A.cam.t = A.cam.t - pl.n * dot (A.cam.t - pl.o, pl.n);
}
static void sketch_begin (const Plane &pl, int target, int edit = -1)
{
	A.sketching = true; A.skEdit = edit;
	if (edit >= 0) A.sk = A.doc.feats[edit];
	else { A.sk = Feature (); A.sk.kind = F_SKETCH; A.sk.pl = pl; A.sk.target = target; A.doc.name_new (A.sk); }
	A.camSaved = A.cam; cam_face (A.sk.pl);
	A.chain = false; A.selEl = -1; A.selFeat = -1; sk_eval ();
	tool_set (T_LINE);
}
static void sketch_end (bool keep)
{
	if (!A.sketching) return;
	int idx = -1;
	if (keep && !A.sk.els.empty ())
	{
		undo_push ();
		if (A.skEdit >= 0) { A.doc.feats[A.skEdit] = A.sk; idx = A.skEdit; }
		else { A.doc.upto = (int) A.doc.feats.size (); A.doc.feats.push_back (A.sk); A.doc.upto++; idx = A.doc.upto - 1; }
		A.doc.touch ();
	}
	int w = A.cam.w, h = A.cam.h; A.cam = A.camSaved; A.cam.w = w; A.cam.h = h; A.cam.set ();
	A.sketching = false; A.selEl = -1; A.selFeat = idx; A.tool = T_SELECT;
	tool_set (T_SELECT);
}
static void sk_add (const SkEl &e)
{
	A.sk.els.push_back (e); sk_eval ();
	if (e.kind != SK_POINT) A.chain = (e.kind == SK_LINE || e.kind == SK_ARC || e.kind == SK_SPLINE) && A.ev.has;	// (a run that closed itself is over)
	A.selEl = -1; sk_arm (); tool_hint (); ui (R_ALL);
}
// The spline being placed is done: the points clicked (the one that follows the pointer is not one).
static void sk_spline_end ()
{
	SkEl e = A.cur; if (!e.pts.empty ()) e.pts.pop_back ();
	if (e.pts.empty ()) { sk_arm (); tool_hint (); ui (R_ALL); return; }
	sk_add (e);
}
// An angle (degrees) as the pointer gives it: on a multiple of 45 when near one, else by steps of 5.
static double snap_angle (double a) { double s = floor (a / 45 + 0.5) * 45; return fabs (a - s) <= 4 ? s : floor (a / 5 + 0.5) * 5; }
// The point of the sketch nearest to a place of the plane, within r: an element's start, end or centre, the middle
// of a line, a rectangle's corners, a spline's points, the origin.
static bool sk_near (V2 p, double r, V2 *out)
{
	bool got = false; double best = r;
	for (const SkShape &s : A.ev.shapes)
		for (const V2 &q : { s.start, s.end, s.centre })
		{
			double d = hypot (q.x - p.x, q.y - p.y);
			if (d < best && (q.x != 0 || q.y != 0 || &q != &s.centre)) { best = d; *out = q; got = true; }
		}
	auto cand = [&] (const V2 &q) { double d = hypot (q.x - p.x, q.y - p.y); if (d < best) { best = d; *out = q; got = true; } };
	for (size_t i = 0; i < A.ev.shapes.size () && i < A.sk.els.size (); i++)
	{
		const SkShape &s = A.ev.shapes[i]; const SkEl &e = A.sk.els[i];
		if (e.kind == SK_LINE || e.kind == SK_CLOSE) cand (V2 ((s.start.x + s.end.x) / 2, (s.start.y + s.end.y) / 2));
		if (e.kind == SK_SPLINE) for (const V2 &q : e.pts) cand (q);
		if (e.kind == SK_RECT) { cand (V2 (s.start.x, s.end.y)); cand (V2 (s.end.x, s.start.y)); cand (V2 ((s.start.x + s.end.x) / 2, (s.start.y + s.end.y) / 2)); }
	}
	cand (V2 (0, 0));
	return got;
}
// The pointer at p (plane) while an element is being placed: its values follow; click: the step is done.
static void sk_point (V2 p, bool click)
{
	SkEl &e = A.cur; double near = 8 / A.cam.scale; V2 q;
	bool snapped = A.snap && sk_near (p, near, &q);
	if (snapped) p = q; else { p.x = snapv (p.x); p.y = snapv (p.y); }
	A.snapOn = snapped; A.snapP = p;
	if (A.curStep == 0)
	{
		e.x = p.x; e.y = p.y; A.arcC = A.arcM = p;
		if (click)
		{
			if (e.kind == SK_POINT) { sk_add (e); return; }
			if (e.kind == SK_SPLINE) e.pts.assign (1, p);
			A.curStep = 1; tool_hint (); ui (R_ALL);
		}
		return;
	}
	if (e.kind == SK_SPLINE)				// its next point follows the pointer; a click keeps it
	{
		if (e.pts.empty ()) e.pts.push_back (p);
		e.pts.back () = p;
		if (click)
		{
			V2 prev = e.pts.size () > 1 ? e.pts[e.pts.size () - 2] : V2 (e.x, e.y);
			if (hypot (p.x - prev.x, p.y - prev.y) < near * 0.5 + 1e-6) { if (e.pts.size () > 1) sk_spline_end (); return; }
			e.pts.push_back (p); ui (R_FIELDS);
		}
		return;
	}
	if (e.kind == SK_ARC && A.tool == T_ARC3)		// its end, then its middle: on the line that cuts the chord in two
	{
		if (A.curStep == 1)
		{
			A.arcC = A.arcM = p; e.r = 0; e.sweep = 0;
			if (click && hypot (p.x - e.x, p.y - e.y) > 1e-6) { A.curStep = 2; tool_hint (); ui (R_ALL); }
			return;
		}
		double cx = A.arcC.x - e.x, cy = A.arcC.y - e.y, c = hypot (cx, cy), nx = -cy / c, ny = cx / c;
		V2 M ((e.x + A.arcC.x) / 2, (e.y + A.arcC.y) / 2);
		double h = (p.x - M.x) * nx + (p.y - M.y) * ny;
		if (A.snap && !snapped) h = snapv (h, 0.5);
		if (A.typed[0] && e.r >= c / 2) { double hh = e.r - sqrt (e.r * e.r - c * c / 4); h = h < 0 ? -hh : hh; }
		A.arcM = V2 (M.x + nx * h, M.y + ny * h);
		if (fabs (h) < 1e-6) { e.sweep = 0; if (!A.typed[0]) e.r = 0; }
		else
		{
			double r = (c * c / 4 + h * h) / (2 * fabs (h)), s = h > 0 ? 1 : -1, k = s * (r - fabs (h));
			V2 C (M.x - nx * k, M.y - ny * k);
			e.r = r; e.ca = atan2 (C.y - e.y, C.x - e.x) * 180 / PI;
			e.sweep = s * 2 * atan2 (c / 2, r - fabs (h)) * 180 / PI;	// (bulging to the left of start -> end: clockwise)
		}
		if (click && fabs (e.sweep) > 1e-6) { sk_add (e); return; }
		if (click) ui (R_FIELDS);
		return;
	}
	if (e.kind == SK_ARC && !e.chain && A.curStep == 1)	// an arc from its centre: where it starts, which sets its radius
	{
		V2 C = A.arcC; double d = hypot (p.x - C.x, p.y - C.y);
		if (d > 1e-9)
		{
			double a = atan2 (p.y - C.y, p.x - C.x), r = A.typed[0] ? e.r : snapped ? d : snapv (d, 0.5);
			if (A.snap && !snapped) a = snap_angle (a * 180 / PI) * PI / 180;
			e.r = r; e.x = C.x + r * cos (a); e.y = C.y + r * sin (a); e.ca = a * 180 / PI + 180;
		}
		if (click && e.r > 1e-6 && d > 1e-9) { A.curStep = 2; A.sweep0 = 0; e.sweep = 0; tool_hint (); ui (R_ALL); return; }
		if (click) ui (R_FIELDS);
		return;
	}
	double dx = p.x - e.x, dy = p.y - e.y, dist = hypot (dx, dy);
	if (e.kind == SK_LINE)
	{
		double a = atan2 (dy, dx) * 180 / PI, l = dist;
		if (!snapped && A.snap) { a = snap_angle (a); l = snapv (l); }
		if (!A.typed[0]) e.len = l;
		if (!A.typed[1]) e.a = a;
		if (click && e.len > 1e-6) { sk_add (e); return; }
	}
	else if (e.kind == SK_RECT)
	{
		if (!A.typed[0]) e.w = e.rel ? 2 * fabs (dx) : dx; if (!A.typed[1]) e.h = e.rel ? 2 * fabs (dy) : dy;
		if (click && fabs (e.w) > 1e-6 && fabs (e.h) > 1e-6) { sk_add (e); return; }
	}
	else if (e.kind == SK_CIRCLE)
	{
		if (!A.typed[0]) e.w = 2 * (snapped ? dist : snapv (dist, 0.5));
		if (click && e.w > 1e-6) { sk_add (e); return; }
	}
	else if (A.curStep == 1)				// an arc's centre
	{
		if (!A.typed[0]) e.r = snapped ? dist : snapv (dist, 0.5);
		e.ca = atan2 (dy, dx) * 180 / PI;
		if (A.snap && !snapped) e.ca = snap_angle (e.ca);
		if (click && e.r > 1e-6) { A.curStep = 2; A.sweep0 = 0; e.sweep = 0; tool_hint (); ui (R_ALL); return; }
	}
	else						// ... its end: the sweep, unwrapped so that it can pass half a turn
	{
		double ca = e.ca * PI / 180; V2 C (e.x + e.r * cos (ca), e.y + e.r * sin (ca));
		double a = atan2 (p.y - C.y, p.x - C.x) * 180 / PI - (e.ca + 180), d = a - A.sweep0;
		while (d > 180) d -= 360; while (d < -180) d += 360;
		double sw = A.sweep0 + d; if (sw > 360) sw = 360; if (sw < -360) sw = -360;
		A.sweep0 = sw;
		if (A.snap && !snapped) sw = snap_angle (sw);
		if (!A.typed[1]) e.sweep = -sw;				// (kept clockwise)
		if (click && fabs (e.sweep) > 1e-6) { sk_add (e); return; }
	}
	if (click) ui (R_FIELDS);
}
static void sk_close ()
{
	if (!A.sketching || !A.ev.has) return;
	SkEl e; e.kind = SK_CLOSE; e.chain = true; A.sk.els.push_back (e); sk_eval (); A.chain = false; sk_arm (); tool_hint (); ui (R_ALL);
}

// ---- Manufacture ---------------------------------------------------------------------------------------------------------
static CamOp *cam_op () { return A.camSel >= 1 && A.camSel <= (int) A.job.ops.size () ? &A.job.ops[A.camSel - 1] : 0; }
static void cam_hint ()
{
	CamOp *op = cam_op (); A.prevErr = 0; char a[24], b[24], c[24];
	V3 s = A.paths.hi - A.paths.lo; fmt (s.x, a, 12); fmt (s.y, b, 12); fmt (s.z, c, 12);
	if (A.camPage == 0) { set_hint ("Click one of the stock's points to put the origin there."); snprintf (A.caption, sizeof A.caption, "Setup \xC2\xB7 stock %s \xC3\x97 %s \xC3\x97 %s", a, b, c); }
	else if (A.camPage == 1) { set_hint ("The tool and the machine are remembered: the next part starts with them."); snprintf (A.caption, sizeof A.caption, "Tool \xC2\xB7 %s", A.job.tool.name); }
	else if (op)
	{
		if (op->failed) { A.prevErr = op->err; set_hint (op->err); }
		else if (op->useFace && A.camDirty) set_hint ("Click the flat face, turned up, to work on.");
		else set_hint (op->kind == CAM_CLEAR ? "Clearing: the stock removed level by level around the body. Its values are at the right."
						     : "Contour: the tool follows the outline, a pass a step down. Tabs hold the part at the end.");
		snprintf (A.caption, sizeof A.caption, "%s \xC2\xB7 %d min", op->name, (int) (op->minutes + 0.5));
	}
}
// The stock and the moves made again (it can take a moment: asked a little after the last change, main.cpp).
static void cam_refresh ()
{
	A.camDirty = false;
	V3 lo, hi; cam_stock (A.doc, A.job, &lo, &hi);
	build_mesh (Manifold::Cube ({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}).Translate ({lo.x, lo.y, lo.z}), A.stockMesh);
	A.paths.lo = lo; A.paths.hi = hi;
	if (A.job.ops.empty () || !cam_compute (A.doc, A.job, A.paths)) { A.paths.moves.clear (); A.paths.hm.clear (); }
	cam_hint ();
}
static void cam_touch () { A.camDirty = true; A.camDirtyT = kapi_get_ticks (); A.doc.changes++; }
static void cam_enter (bool on)
{
	if (A.sketching || on == A.camMode) return;
	tool_set (T_SELECT);
	if (on)
	{
		if (A.doc.bodies.empty ()) { set_hint ("Manufacture needs a body: make one first."); ui (R_ALL); return; }
		if (!A.job.on || !A.doc.body (A.job.body)) { A.job.on = true; A.job.body = A.doc.body (A.selBody) ? A.selBody : A.doc.bodies[0].id; }
		A.camMode = true; A.camSel = 0; A.camPage = 0; A.camSim = false; cam_refresh ();
	}
	else { A.camMode = false; A.caption[0] = 0; A.prevErr = 0; tool_hint (); }
	ui (R_ALL);
}
static void cam_add (int kind)
{
	CamOp o; o.kind = kind; int n = 1; for (const CamOp &q : A.job.ops) if (q.kind == kind) n++;
	snprintf (o.name, sizeof o.name, "%s %d", kind == CAM_CLEAR ? "Clearing" : "Contour", n);
	if (kind == CAM_CONTOUR) o.stepdown = 2;
	A.job.ops.push_back (o); A.camSel = (int) A.job.ops.size (); A.camPage = 2; A.camSim = false; A.doc.changes++;
	cam_refresh (); ui (R_ALL);
}
static void cam_delete_op ()
{
	if (!cam_op ()) return;
	A.job.ops.erase (A.job.ops.begin () + A.camSel - 1); A.camSel = 0; A.camPage = 0; A.doc.changes++; cam_refresh (); ui (R_ALL);
}

// ---- the view ----------------------------------------------------------------------------------------------------------
class View : public Widget
{
public:
	Scene sc;
	int pressX, pressY, lastX, lastY, drag;		// drag: 0 none, 1 pressed, 2 turning, 3 panning, 4 the fillet's arrow
	int btn;
	bool wasL, wasR, pristine;		// pristine: the view as fit () left it (a resize fits it again)
	int fitW, fitH;
	double arrowX, arrowY;
	View (int l, int t, int w, int h) : Widget (l, t, w, h), pressX (0), pressY (0), lastX (0), lastY (0), drag (0), btn (0), wasL (false), wasR (false), pristine (true), fitW (0), fitH (0), arrowX (-1), arrowY (-1)
	{ catchOutside = true; A.cam.w = w; A.cam.h = h; home (); }

	void home () { A.cam.az = -58; A.cam.el = 28; A.cam.set (); fit (); }
	void fit ()
	{
		V3 lo (0, 0, 0), hi (100, 60, 40); bool any = false;
		for (const Body &b : A.doc.bodies)
		{
			if (!any) { lo = b.mesh.lo; hi = b.mesh.hi; any = true; continue; }
			lo = V3 (std::min (lo.x, b.mesh.lo.x), std::min (lo.y, b.mesh.lo.y), std::min (lo.z, b.mesh.lo.z));
			hi = V3 (std::max (hi.x, b.mesh.hi.x), std::max (hi.y, b.mesh.hi.y), std::max (hi.z, b.mesh.hi.z));
		}
		double diag = len (hi - lo); if (diag < 10) diag = 10;
		A.cam.t = (lo + hi) * 0.5; A.cam.scale = std::min (width, height) * 0.82 / diag;
		pristine = true; fitW = width; fitH = height; invalidate (true);
	}
	void lookAlong (double az, double el) { A.cam.az = az; A.cam.el = el; A.cam.set (); invalidate (true); }
	double radius () const
	{
		// (the depth the frame holds, either side of the view's centre: the bodies, and the grid as far as it is drawn
		//  -- zoomed out, it was cut off in the distance)
		double r = 300;
		for (const Body &b : A.doc.bodies) r = std::max (r, std::max (len (b.mesh.lo - A.cam.t), len (b.mesh.hi - A.cam.t)) * 1.5);
		r = std::max (r, std::max (width, height) / A.cam.scale * 2.5);
		return r + len (A.cam.t);
	}

	// ---- what is under the pointer -------------------------------------------------------------------------------
	// The plane a tool starts on: the flat face under the pointer, else the ground.
	Plane planeAt (int mx, int my, int *target)
	{
		Hit h; *target = -1;
		if (pick (A.doc, A.cam, mx, my, &h))
		{
			const Body &b = A.doc.bodies[h.body]; int g = b.mesh.grp[h.tri];
			if (b.mesh.flat[g]) { *target = b.id; return plane_of (h.p, b.mesh.gn[g]); }
		}
		return Plane ();
	}
	// The edge near the pointer (a chain of a shown body, not hidden by a face): body's index and chain, or -1.
	bool edgeAt (int mx, int my, int *body, int *chain)
	{
		Hit h; bool hit = pick (A.doc, A.cam, mx, my, &h); double best = 8; *body = *chain = -1;
		for (size_t bi = 0; bi < A.doc.bodies.size (); bi++)
		{
			const Body &b = A.doc.bodies[bi];
			if (!body_shown (b.id)) continue;
			if (A.hasPend && A.pend.target >= 0 && b.id != A.pend.target) continue;
			for (size_t ci = 0; ci < b.mesh.chains.size (); ci++)
			{
				const Chain &c = b.mesh.chains[ci]; int n = (int) c.pts.size ();
				for (int k = 0; k < (c.closed ? n : n - 1); k++)
				{
					double x0, y0, z0, x1, y1, z1; A.cam.screen (b.mesh.v[c.pts[k]], &x0, &y0, &z0); A.cam.screen (b.mesh.v[c.pts[(k + 1) % n]], &x1, &y1, &z1);
					double dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy, t = l2 > 1e-9 ? ((mx - x0) * dx + (my - y0) * dy) / l2 : 0;
					if (t < 0) t = 0; if (t > 1) t = 1;
					double d = hypot (mx - (x0 + dx * t), my - (y0 + dy * t)), z = z0 + (z1 - z0) * t;
					if (d < best && (!hit || z > h.depth - 1.0)) { best = d; *body = (int) bi; *chain = (int) ci; }
				}
			}
		}
		return *chain >= 0;
	}

	// ---- the tools' clicks and moves -----------------------------------------------------------------------------
	V3 baseCentre () const
	{
		const Feature &f = A.pend;
		if (f.kind == F_BOX) return f.centred ? f.pl.at (f.x, f.y, f.z) : f.pl.at (f.x + f.w / 2, f.y + f.d / 2, f.z);
		if (round_kind (f.kind)) return f.pl.at (f.x, f.y, f.z);
		if (f.kind == F_EXTRUDE && f.sketch >= 0 && f.sketch < (int) A.doc.feats.size ())
		{
			const Feature &sk = A.doc.feats[f.sketch]; SkEval ev; sketch_eval (sk.els, 24, ev);
			double x = 0, y = 0; int n = 0;
			for (auto &poly : ev.closed) for (auto &p : poly) { x += p.x; y += p.y; n++; }
			return n ? sk.pl.at (x / n, y / n) : sk.pl.o;
		}
		return V3 ();
	}
	V3 pendNormal () const
	{
		if (A.pend.kind == F_EXTRUDE && A.pend.sketch >= 0 && A.pend.sketch < (int) A.doc.feats.size ()) return A.doc.feats[A.pend.sketch].pl.n;
		return A.pend.pl.n;
	}
	void heightFromPointer (int mx, int my)
	{
		Feature &f = A.pend;
		int hi = f.kind == F_BOX ? 2 : 1;
		if (f.kind == F_TORUS) f.h = 1;
		else if (!A.typed[hi] && !f.through) { double h = snapv (A.cam.along (mx, my, baseCentre (), pendNormal ())); f.h = h; }
		auto_op (); preview_update ();
	}
	// A shape's step done (a click, or Enter in a field): the next one, or the shape made. true: it is made.
	bool shapeNext (int mx, int my, bool pointer)
	{
		Feature &f = A.pend; int last = f.kind == F_TORUS ? 2 : height_step ();		// (the sphere's: 1)
		if (A.step < 1 || A.step >= last) return commit_pend ();		// (0: made as its fields say, without the pointer)
		if (fabs (f.w) < 1e-6 || (f.kind == F_BOX && fabs (f.d) < 1e-6)) return false;
		if (A.step == 2 && f.kind == F_TAPER && f.d < 0) return false;
		A.step++;
		if (A.step == 2 && f.kind == F_TAPER && !pointer && f.d < 1e-6) f.d = f.w / 2;
		if (A.step == 2 && f.kind == F_TORUS && f.d < 1e-6) f.d = f.w / 4 < 0.5 ? 0.5 : f.w / 4;
		if (A.step == height_step () && f.kind != F_TORUS && f.kind != F_SPHERE) { if (pointer) heightFromPointer (mx, my); else if (fabs (f.h) < 1e-6) f.h = 10; }
		auto_op (); preview_update (); tool_hint (); caption_set (); ui (R_ALL);
		return false;
	}
	// ---- Manufacture in the view: the stock, the origin's 27 points, the face an operation works on, the moves ----
	V3 stockPoint (int k) const
	{
		const V3 &lo = A.paths.lo, &hi = A.paths.hi;
		return V3 (lo.x + (hi.x - lo.x) * (k % 3) / 2, lo.y + (hi.y - lo.y) * (k / 3 % 3) / 2, lo.z + (hi.z - lo.z) * (k / 9) / 2);
	}
	bool camFacePick () const { CamOp *op = cam_op (); return A.camMode && A.camPage == 2 && op && op->useFace; }
	void camMove (int mx, int my)
	{
		Hit h; int hb = -1, hf = -1;
		if (camFacePick () && pick (A.doc, A.cam, mx, my, &h) && A.doc.bodies[h.body].id == A.job.body) { hb = h.body; hf = A.doc.bodies[h.body].mesh.grp[h.tri]; }
		if (hb != A.hovBody || hf != A.hovFace) { A.hovBody = hb; A.hovFace = hf; invalidate (true); }
	}
	void camClick (int mx, int my)
	{
		if (A.camPage == 0)
		{
			int best = -1; double bd = 12;
			for (int k = 0; k < 27; k++) { double x, y; sx (stockPoint (k), &x, &y); double d = hypot (mx - x, my - y); if (d < bd) { bd = d; best = k; } }
			if (best >= 0) { A.job.origin = best; A.doc.changes++; ui (R_ALL); }
		}
		else if (camFacePick ())
		{
			Hit h; CamOp *op = cam_op ();
			if (pick (A.doc, A.cam, mx, my, &h) && A.doc.bodies[h.body].id == A.job.body)
			{
				const RMesh &m = A.doc.bodies[h.body].mesh; int g = m.grp[h.tri];
				if (m.flat[g] && m.gn[g].z > 0.9999) { op->facePt = h.p; cam_refresh (); ui (R_ALL); }
				else { set_hint ("Choose a flat face turned up."); ui (0); }
			}
		}
		invalidate (true);
	}
	void camScene ()
	{
		const CamSetup &c = A.job; double bias = 2.0 / A.cam.scale; (void) c;
		if (!A.camSim && A.stockMesh.tris ())			// the stock: a see-through block
		{
			sc.batch (KAPI_GPU_B_CULL_BACK | KAPI_GPU_B_BLEND (KAPI_GPU_BLEND_ALPHA) | KAPI_GPU_B_NOZWRITE, true);
			scene_body (sc, A.stockMesh, 0xECC882, 44);
			sc.batch (KAPI_GPU_B_ZFUNC (KAPI_GPU_Z_LEQUAL) | KAPI_GPU_B_NOZWRITE, false);
			scene_edges (sc, A.stockMesh, 0x966E28, 0.6, 200);
		}
		const CamPaths &p = A.paths;
		if (A.camSim && !p.hm.empty ())				// what is left of the stock: its height, cell by cell
		{
			sc.batch (0, true);
			int s = 1; while ((p.nx / s) * (p.ny / s) > 14000) s++;
			double x0 = p.lo.x - c.tool.dia, y0 = p.lo.y - c.tool.dia;
			auto hgt = [&] (int i, int j) { if (i >= p.nx) i = p.nx - 1; if (j >= p.ny) j = p.ny - 1; return (double) p.hm[(size_t) j * p.nx + i]; };
			for (int j = 0; j + s <= p.ny; j += s) for (int i = 0; i + s <= p.nx; i += s)
			{
				double h00 = hgt (i, j), h10 = hgt (i + s, j), h11 = hgt (i + s, j + s), h01 = hgt (i, j + s);
				if (h00 < -1e8 || h10 < -1e8 || h11 < -1e8 || h01 < -1e8) continue;
				V3 a (x0 + (i + 0.5) * p.cell, y0 + (j + 0.5) * p.cell, h00), b (x0 + (i + s + 0.5) * p.cell, y0 + (j + 0.5) * p.cell, h10);
				V3 d (x0 + (i + s + 0.5) * p.cell, y0 + (j + s + 0.5) * p.cell, h11), e (x0 + (i + 0.5) * p.cell, y0 + (j + s + 0.5) * p.cell, h01);
				V3 n = unit (cross (b - a, e - a)); unsigned col = shade_rgb (0xD9B77E, lit (A.cam, n));
				sc.vert (a.x, a.y, a.z, col); sc.vert (b.x, b.y, b.z, col); sc.vert (d.x, d.y, d.z, col);
				sc.vert (a.x, a.y, a.z, col); sc.vert (d.x, d.y, d.z, col); sc.vert (e.x, e.y, e.z, col);
			}
			return;
		}
		if (p.moves.size () < 2) return;				// the moves: cuts blue, fast moves amber; the chosen operation's stronger
		sc.batch (KAPI_GPU_B_ZFUNC (KAPI_GPU_Z_LEQUAL) | KAPI_GPU_B_NOZWRITE | KAPI_GPU_B_BLEND (KAPI_GPU_BLEND_ALPHA), false);
		size_t skip = p.moves.size () > 30000 ? p.moves.size () / 30000 + 1 : 1; int only = A.camPage == 2 ? A.camSel - 1 : -1;
		for (size_t k = 1; k < p.moves.size (); k++)
		{
			const CamMove &m = p.moves[k];
			if (skip > 1 && k % skip && m.kind == p.moves[k - 1].kind && len (m.p - p.moves[k - 1].p) < 0.6) continue;
			bool on = only < 0 || m.op == only;
			sc.line (p.moves[k - 1].p, m.p, (m.kind ? 0.55 : 0.45) * SS, m.kind ? 0x1E6EDC : 0xE2962C, on ? (m.kind ? 255 : 200) : 60, bias);
		}
	}
	void camOverlays ()
	{
		V3 o = cam_origin (A.job, A.paths.lo, A.paths.hi), ax, ay; cam_axes (A.job, &ax, &ay);
		if (A.camPage == 0)
			for (int k = 0; k < 27; k++) { double x, y; sx (stockPoint (k), &x, &y); ov_dot (canvas, x, y, k == A.job.origin ? 0 : 3.2, 0xFFFFFF, 0x966E28, 1.4); }
		double L = 70 / A.cam.scale; if (L < 12) L = 12; double x0, y0; sx (o, &x0, &y0);
		const struct { V3 v; unsigned c; const char *n; } axis[3] = { { ax, 0xD6483E, "X" }, { ay, 0x40A04C, "Y" }, { V3 (0, 0, 1), 0x3A7AD6, "Z" } };
		for (int i = 0; i < 3; i++)
		{
			double x1, y1; sx (o + axis[i].v * L, &x1, &y1); ov_arrow (canvas, x0, y0, x1, y1, axis[i].c, false);
			uk_text (canvas, (int) (x0 + (x1 - x0) * 1.18) - 4, (int) (y0 + (y1 - y0) * 1.18) - uk_fh () / 2, axis[i].n, axis[i].c, 2);
		}
		ov_dot (canvas, x0, y0, 5, 0xFFFFFF, 0x282C36, 2);
	}

	void toolMove (int mx, int my)
	{
		Feature &f = A.pend; V3 p;
		if (A.camMode) { camMove (mx, my); return; }
		if (A.sketching)
		{
			if (A.cam.onPlane (mx, my, A.sk.pl, &p)) { sk_point (A.sk.pl.to (p), false); ui (R_FIELDS); }
			invalidate (true); return;
		}
		switch (A.tool)
		{
		case T_SELECT: case T_SKETCH: case T_MOVE: case T_UNION: case T_SUB: case T_INT:
		{
			Hit h; int hb = -1, hf = -1;
			if ((A.tool != T_MOVE || A.step == 0) && pick (A.doc, A.cam, mx, my, &h)) { hb = h.body; hf = A.doc.bodies[h.body].mesh.grp[h.tri]; }
			if (hb != A.hovBody || hf != A.hovFace) { A.hovBody = hb; A.hovFace = hf; invalidate (true); }
			if (A.tool == T_MOVE && A.step == 1 && A.cam.onPlane (mx, my, plane_of (A.mA, V3 (0, 0, 1)), &p))
			{
				if (!A.typed[0]) f.mv.x = snapv (p.x - A.mA.x); if (!A.typed[1]) f.mv.y = snapv (p.y - A.mA.y);
				preview_update (); ui (R_FIELDS); invalidate (true);
			}
			break;
		}
		case T_BOX: case T_CYL: case T_PYRAMID: case T_PRISM: case T_TAPER: case T_TORUS: case T_SPHERE:
			if (A.step == 1 && A.cam.onPlane (mx, my, f.pl, &p))
			{
				V2 q = f.pl.to (p);
				if (f.kind == F_BOX)
				{
					double w = snapv (q.x) - f.x, d = snapv (q.y) - f.y;
					if (f.centred) { w = 2 * fabs (w); d = 2 * fabs (d); }
					if (!A.typed[0]) f.w = w; if (!A.typed[1]) f.d = d;
				}
				else if (!A.typed[0]) f.w = (f.kind == F_CYL ? 2 : 1) * snapv (hypot (q.x - f.x, q.y - f.y), 0.5);
				// a base with sides: a corner of it points to the pointer
				if ((f.kind == F_PYRAMID || f.kind == F_PRISM || f.kind == F_TAPER) && f.n >= 2.5 && !A.typed[3] && hypot (q.x - f.x, q.y - f.y) > 1e-6)
				{
					double a = atan2 (q.y - f.y, q.x - f.x) * 180 / PI;
					if (A.snap) a = snap_angle (a);
					f.turn = a;
				}
				if (f.kind == F_SPHERE) preview_update ();
				ui (R_FIELDS); invalidate (true);
			}
			else if (A.step == 2 && (f.kind == F_TAPER || f.kind == F_TORUS) && A.cam.onPlane (mx, my, f.pl, &p))
			{
				// the top's radius: how far from the centre -- the tube's: how far from the ring
				V2 q = f.pl.to (p); double dist = hypot (q.x - f.x, q.y - f.y);
				if (f.kind == F_TAPER) { if (!A.typed[2]) f.d = snapv (dist, 0.5); }
				else if (!A.typed[1]) { double r = snapv (fabs (dist - f.w), 0.5); f.d = r < 0.5 ? 0.5 : r; }
				preview_update (); ui (R_FIELDS); invalidate (true);
			}
			else if (A.step == height_step () && f.kind != F_SPHERE) { heightFromPointer (mx, my); tool_hint (); ui (R_FIELDS); invalidate (true); }
			break;
		case T_EXTRUDE:
			if (A.step == 2) { heightFromPointer (mx, my); ui (R_FIELDS); invalidate (true); }
			break;
		case T_FILLET: case T_CHAMFER:
		{
			int b, c; edgeAt (mx, my, &b, &c);
			if (b != A.hovBody || c != A.hovChain) { A.hovBody = b; A.hovChain = c; invalidate (true); }
			break;
		}
		}
	}
	void toolClick (int mx, int my)
	{
		Feature &f = A.pend; V3 p; Hit h;
		if (A.camMode) { camClick (mx, my); return; }
		if (A.sketching)
		{
			if (A.cam.onPlane (mx, my, A.sk.pl, &p)) sk_point (A.sk.pl.to (p), true);
			invalidate (true); return;
		}
		switch (A.tool)
		{
		case T_SELECT:
			if (pick (A.doc, A.cam, mx, my, &h)) { A.selBody = A.doc.bodies[h.body].id; A.selFeat = -1; }
			else { A.selBody = -1; A.selFeat = -1; }
			ui (R_ALL); break;
		case T_BOX: case T_CYL: case T_PYRAMID: case T_PRISM: case T_TAPER: case T_TORUS: case T_SPHERE:
			if (A.step == 0)
			{
				int target; Plane pl = planeAt (mx, my, &target);
				if (!A.cam.onPlane (mx, my, pl, &p)) break;
				// (the sizes follow the pointer from here -- but the ones typed, and what has no step of its own)
				V2 q = pl.to (p); f.pl = pl; f.target = target; f.x = snapv (q.x); f.y = snapv (q.y);
				if (target < 0 && A.snap && hypot (q.x, q.y) < 9 / A.cam.scale) f.x = f.y = 0;		// (near the origin: on it)
				if (!A.typed[0]) f.w = 0;
				if (f.kind == F_BOX && !A.typed[1]) f.d = 0;
				A.hasPend = true; A.step = 1; A.hasPrev = false; auto_op ();
			}
			else { if (shapeNext (mx, my, true)) { invalidate (true); return; } }
			tool_hint (); caption_set (); ui (R_ALL); break;
		case T_SKETCH: { int target; Plane pl = planeAt (mx, my, &target); sketch_begin (pl, target); break; }
		case T_EXTRUDE: if (A.step == 2) commit_pend (); break;
		case T_FILLET: case T_CHAMFER:
		{
			int b, c;
			if (!edgeAt (mx, my, &b, &c)) break;
			const Body &bd = A.doc.bodies[b]; const Chain &ch = bd.mesh.chains[c];
			if (ch.kind == 0) { set_hint ("This edge cannot be rounded: only straight edges, circles and arcs."); ui (R_PANELS); break; }
			V3 m = chain_mid (bd.mesh, ch); bool had = false;
			for (size_t i = 0; i < f.edges.size (); i++) if (chain_near (bd.mesh, f.edges[i], 0.05) == c) { f.edges.erase (f.edges.begin () + i); had = true; break; }
			if (!had) { f.edges.push_back (m); f.target = bd.id; }
			if (f.edges.empty ()) f.target = -1;
			preview_update (); if (A.prevErr) set_hint (A.prevErr); else tool_hint ();
			caption_set (); ui (R_ALL); break;
		}
		case T_MOVE:
			if (A.step == 0)
			{
				if (!pick (A.doc, A.cam, mx, my, &h)) break;
				f.target = A.doc.bodies[h.body].id; A.mA = h.p; A.hasPend = true; A.step = 1; A.selBody = f.target; preview_update ();
				tool_hint (); caption_set (); ui (R_ALL);
			}
			else commit_pend ();
			break;
		case T_UNION: case T_SUB: case T_INT:
			if (!pick (A.doc, A.cam, mx, my, &h)) break;
			if (A.step == 0) { f.target = A.doc.bodies[h.body].id; A.selBody = f.target; A.hasPend = true; A.step = 1; tool_hint (); ui (R_ALL); }
			else if (A.doc.bodies[h.body].id != f.target) { f.tool = A.doc.bodies[h.body].id; commit_pend (); }
			break;
		case T_MEASURE:
			if (!pick (A.doc, A.cam, mx, my, &h)) { if (!A.cam.onPlane (mx, my, Plane (), &h.p)) break; }
			if (A.mN >= 2) A.mN = 0;
			(A.mN == 0 ? A.mA : A.mB) = h.p; A.mN++;
			tool_hint (); ui (R_ALL); break;
		}
		invalidate (true);
	}

	// ---- the bodies: a small panel floating over the view's top left corner ------------------------------------------
	enum { BX = 10, BY = 44, BW = 168, BROW = 24 };
	// (under the bodies: the sketches of the part as it is rebuilt -- a click shows one, "Edit" or a double click opens it)
	int sketchAt (int k) const { for (int i = 0; i < A.doc.upto && i < (int) A.doc.feats.size (); i++) if (A.doc.feats[i].kind == F_SKETCH && k-- == 0) return i; return -1; }
	int sketchCount () const { int n = 0; for (int i = 0; i < A.doc.upto && i < (int) A.doc.feats.size (); i++) if (A.doc.feats[i].kind == F_SKETCH) n++; return n; }
	int bodiesPart () const { return A.doc.bodies.empty () ? 0 : 24 + BROW * (int) A.doc.bodies.size (); }
	int bodiesH () const
	{
		if (A.sketching || A.camMode) return 0;
		int ns = sketchCount (), h = bodiesPart () + (ns ? 24 + BROW * ns : 0);
		return h ? h + 6 : 0;
	}
	void drawBodies ()
	{
		int h = bodiesH (); if (!h) return;
		uk_rbox (canvas, BX, BY, BW, h, 8, 0xFFFFFF, 0xFFFFFF, 232); uk_rline (canvas, BX, BY, BW, h, 8, 0xB0B4BE);
		int ns = sketchCount (), sy = BY + bodiesPart ();
		if (ns)
		{
			if (sy > BY) canvas.fillRect (BX + 8, sy + 1, BW - 16, 1, 0xDCDFE5);
			{ UkFaceScope fs (g_small); uk_text (canvas, BX + 10, sy + 6, "Sketches", 0x5A5E68, 2); }
			for (int k = 0; k < ns; k++)
			{
				int i = sketchAt (k), y = sy + 24 + k * BROW; unsigned bg = 0xFFFFFF; const Feature &f = A.doc.feats[i];
				if (i == A.selFeat) { bg = uk_mix (C_ACCENT, 0xFFFFFF, 190); uk_rbox (canvas, BX + 4, y, BW - 8, BROW, 5, bg, bg); }
				icon (canvas, I_SKETCH, BX + 8, y + 3, 18, 0x464C5A, C_ACCENT, bg);
				char fit[32]; uk_text_fit (f.name, BW - 34 - 40, fit, sizeof fit);
				uk_text (canvas, BX + 32, y + (BROW - uk_fh ()) / 2, fit, 0x1A1A1E, i == A.selFeat ? 2 : 0);
				UkFaceScope fs (g_small); uk_text (canvas, BX + BW - 10 - uk_tw ("Edit"), y + (BROW - uk_fh ()) / 2, "Edit", 0x2862B0);
			}
		}
		if (A.doc.bodies.empty ()) return;
		{ UkFaceScope fs (g_small); uk_text (canvas, BX + 10, BY + 6, "Bodies", 0x5A5E68, 2); }
		for (size_t i = 0; i < A.doc.bodies.size (); i++)
		{
			int id = A.doc.bodies[i].id, y = BY + 24 + (int) i * BROW; BodyProp &p = A.doc.prop (id); unsigned bg = 0xFFFFFF;
			if (id == A.selBody) { bg = uk_mix (C_ACCENT, 0xFFFFFF, 190); uk_rbox (canvas, BX + 4, y, BW - 8, BROW, 5, bg, bg); }
			icon (canvas, I_EYE, BX + 8, y + 3, 18, p.visible ? 0x464C5A : 0xB4B8C0, C_ACCENT, bg);
			uk_rbox (canvas, BX + 32, y + 5, 14, 14, 3, p.colour, p.colour); uk_rline (canvas, BX + 32, y + 5, 14, 14, 3, uk_tone (p.colour, 70));
			char fit[32]; uk_text_fit (p.name, BW - 62, fit, sizeof fit);
			uk_text (canvas, BX + 54, y + (BROW - uk_fh ()) / 2, fit, p.visible ? 0x1A1A1E : 0x8A8E96, id == A.selBody ? 2 : 0);
		}
	}
	// A press in it: the eye shows / hides, the name selects. true: it was in the panel.
	bool bodiesPress (int mx, int my)
	{
		int h = bodiesH ();
		if (!h || mx < BX || mx >= BX + BW || my < BY || my >= BY + h) return false;
		int sy = BY + bodiesPart (), k = (my - sy - 24) / BROW;
		if (my >= sy + 24 && k < sketchCount ())			// a sketch: shown; "Edit", or pressed twice: opened
		{
			static int lastK = -1; static unsigned lastT = 0; unsigned now = kapi_get_ticks ();
			int i = sketchAt (k); bool open = mx >= BX + BW - 44 || (lastK == i && now - lastT < 45); lastK = i; lastT = now;
			if (A.tool != T_SELECT) tool_set (T_SELECT);
			A.selFeat = i; A.selBody = -1;
			if (open) { lastK = -1; sketch_begin (A.doc.feats[i].pl, A.doc.feats[i].target, i); }
			ui (R_ALL); return true;
		}
		int i = (my - BY - 24) / BROW;
		if (my >= BY + 24 && my < sy && i < (int) A.doc.bodies.size ())
		{
			int id = A.doc.bodies[i].id;
			if (mx < BX + 30) { BodyProp &p = A.doc.prop (id); p.visible = !p.visible; A.doc.changes++; }
			else { A.selBody = id; A.selFeat = -1; if (A.tool != T_SELECT) tool_set (T_SELECT); }
			ui (R_ALL);
		}
		return true;
	}

	// ---- the mouse -----------------------------------------------------------------------------------------------------
	bool cubeHit (int mx, int my, double *az, double *el);
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		bool inside = mx >= 0 && my >= 0 && mx < width && my < height;
		bool pressL = bl && !wasL, releaseL = !bl && wasL, pressR = (br || bm) && !wasR;
		wasL = bl != 0; wasR = (br || bm) != 0;
		if (!inside && !drag) return false;
		if (wheel && inside)
		{
			V3 before = A.cam.under (mx, my);
			A.cam.scale *= wheel > 0 ? 1.15 : 1 / 1.15;
			if (A.cam.scale < 0.05) A.cam.scale = 0.05; if (A.cam.scale > 400) A.cam.scale = 400;
			A.cam.t = A.cam.t + (before - A.cam.under (mx, my)); pristine = false; invalidate (true);
		}
		if (pressL)
		{
			// the view's own first: the bodies' panel, the cube, home / fit / see-through
			if (bodiesPress (mx, my)) { drag = 0; invalidate (true); return true; }
			double az, el;
			if (!A.sketching && cubeHit (mx, my, &az, &el)) { lookAlong (az, el); drag = 0; return true; }
			int bx = width - 40, by = 124;
			if (mx >= bx && mx < bx + 30 && my >= by && my < by + 88)
			{
				int k = (my - by - 2) / 28;
				if (k == 0) { if (A.sketching) cam_face (A.sk.pl); else home (); } else if (k == 1) fit (); else { A.seeThrough = !A.seeThrough; ui (R_PANELS); }
				invalidate (true); return true;
			}
			// the fillet's arrow
			if ((A.tool == T_FILLET || A.tool == T_CHAMFER) && arrowX >= 0 && hypot (mx - arrowX, my - arrowY) < 12) { drag = 4; pressX = mx; pressY = my; return true; }
			pressX = lastX = mx; pressY = lastY = my; drag = (kapi_get_modifiers () & MOD_SHIFT) ? 3 : 1;
			return true;
		}
		if (pressR) { lastX = mx; lastY = my; drag = 3; return true; }
		if (drag == 3 && !bl && !br && !bm) { drag = 0; return true; }
		if (drag == 4)
		{
			if (!bl) { drag = 0; return true; }
			if (!A.pend.edges.empty () && A.doc.body (A.pend.target))
			{
				double x, y; A.cam.screen (A.pend.edges[0], &x, &y);
				double r = snapv (hypot (mx - x, my - y) / A.cam.scale * 0.8, 0.5); if (r < 0.5) r = 0.5;
				if (r != A.pend.r) { A.pend.r = r; preview_update (); if (A.prevErr) set_hint (A.prevErr); else tool_hint (); ui (R_FIELDS); invalidate (true); }
			}
			return true;
		}
		if (drag == 1 && bl && (abs (mx - pressX) > 4 || abs (my - pressY) > 4)) drag = A.sketching ? 3 : 2;
		if (drag == 2 || drag == 3)
		{
			int dx = mx - lastX, dy = my - lastY; lastX = mx; lastY = my;
			if (drag == 2)
			{
				A.cam.az -= dx * 0.45; A.cam.el += dy * 0.45;
				if (A.cam.el > 90) A.cam.el = 90; if (A.cam.el < -90) A.cam.el = -90;
				A.cam.set ();
			}
			else { A.cam.t = A.cam.t - A.cam.r * (dx / A.cam.scale) + A.cam.u * (dy / A.cam.scale); pristine = false; }
			if (dx || dy) invalidate (true);
			if (releaseL && drag == 2) drag = 0;
			if (!bl && !br && !bm) drag = 0;
			return true;
		}
		if (releaseL && drag == 1) { drag = 0; if (inside) toolClick (mx, my); return true; }
		if (!bl && inside) toolMove (mx, my);
		return inside;
	}

	// ---- the drawing ---------------------------------------------------------------------------------------------------
	void sx (const V3 &p, double *x, double *y) const { A.cam.screen (p, x, y); }
	void line3 (const V3 &a, const V3 &b, double w, unsigned c, bool dashed = false)
	{
		double x0, y0, x1, y1; sx (a, &x0, &y0); sx (b, &x1, &y1);
		if (dashed) ov_dash (canvas, x0, y0, x1, y1, w, c); else ov_line (canvas, x0, y0, x1, y1, w, c);
	}
	void tag3 (const V3 &p, int dx, int dy, const char *s, bool active = false, unsigned frame = 0)
	{
		double x, y; sx (p, &x, &y); ov_tag (canvas, (int) x + dx, (int) y + dy, s, active, frame);
	}
	void chainDraw (const RMesh &m, const Chain &c, double w, unsigned col)
	{
		int n = (int) c.pts.size ();
		for (int k = 0; k < (c.closed ? n : n - 1); k++) line3 (m.v[c.pts[k]], m.v[c.pts[(k + 1) % n]], w, col);
	}
	void sketchDraw (const Feature &sk, const SkEval &ev, bool editing);
	void drawCube ();
	void overlays ();
	void chrome ();
	void onDraw () override
	{
		A.cam.w = width; A.cam.h = height;
		if (pristine && !A.sketching && (fitW != width || fitH != height)) fit ();
		const Cam &c = A.cam;
		sc.begin (c, radius ());
		sc.backdrop (0xF7F8FA, 0xE2E6ED);
		// the grid: the ground's, or the sketch's plane
		if (A.showGrid || A.sketching)
		{
			sc.batch (KAPI_GPU_B_ZFUNC (KAPI_GPU_Z_LESS), false);
			Plane pl = A.sketching ? A.sk.pl : Plane ();
			double step = A.sketching ? 5 : 10; if (c.scale * step < 6) step *= 5; if (c.scale * step < 6) step *= 2;
			double reach = std::max (width, height) / c.scale * 0.75; V2 mid = pl.to (c.t);
			int n = (int) (reach / step) + 1; if (n > 60) n = 60;
			double cx = floor (mid.x / step) * step, cy = floor (mid.y / step) * step, bias = -2.5 / c.scale;
			for (int i = -n; i <= n; i++)
			{
				double a = cx + i * step, b = cy + i * step;
				bool ma = fabs (fmod (fabs (a), step * 5)) < 1e-6 || fabs (fmod (fabs (a), step * 5) - step * 5) < 1e-6;
				bool mb = fabs (fmod (fabs (b), step * 5)) < 1e-6 || fabs (fmod (fabs (b), step * 5) - step * 5) < 1e-6;
				sc.line (pl.at (a, cy - n * step), pl.at (a, cy + n * step), 0.5 * SS, ma ? 0xB0B7C4 : 0xCBD0D9, 255, bias);
				sc.line (pl.at (cx - n * step, b), pl.at (cx + n * step, b), 0.5 * SS, mb ? 0xB0B7C4 : 0xCBD0D9, 255, bias);
			}
			if (!A.sketching)
			{
				sc.line (V3 (0, 0, 0), V3 (cx + n * step, 0, 0), 0.9 * SS, 0xD65C52, 255, bias / 2);
				sc.line (V3 (0, 0, 0), V3 (0, cy + n * step, 0), 0.9 * SS, 0x50A85A, 255, bias / 2);
			}
			else						// the sketch's two axes through its origin, in the colours of the model's
			{
				auto colour = [] (const V3 &d) { return fabs (d.x) > 0.9 ? 0xD6483Eu : fabs (d.y) > 0.9 ? 0x40A04Cu : fabs (d.z) > 0.9 ? 0x3A7AD6u : 0x808890u; };
				double far = (n + 2) * step + std::max (fabs (cx), fabs (cy));
				sc.line (pl.at (-far, 0), pl.at (far, 0), 1.1 * SS, colour (pl.u), 255, bias / 2);
				sc.line (pl.at (0, -far), pl.at (0, far), 1.1 * SS, colour (pl.v), 255, bias / 2);
			}
		}
		// the bodies
		bool thru = A.seeThrough, fade = A.sketching;
		unsigned flags = KAPI_GPU_B_CULL_BACK | (thru ? KAPI_GPU_B_BLEND (KAPI_GPU_BLEND_ALPHA) | KAPI_GPU_B_NOZWRITE : 0);
		for (size_t bi = 0; bi < A.doc.bodies.size (); bi++)
		{
			const Body &b = A.doc.bodies[bi]; BodyProp &bp = A.doc.prop (b.id);
			if (A.camMode ? b.id != A.job.body || A.camSim : !bp.visible) continue;	// (Manufacture: the body cut, alone; simulated: what is left)
			sc.batch (flags, true);
			int tintFace = -1; unsigned tint = ACC ();
			if (camFacePick ())					// the face the operation works on; the one pointed
			{
				tintFace = (int) bi == A.hovBody && A.hovFace >= 0 && b.mesh.flat[A.hovFace] && b.mesh.gn[A.hovFace].z > 0.9999 ? A.hovFace : cam_face (b.mesh, cam_op ()->facePt);
				if (tintFace != A.hovFace || (int) bi != A.hovBody) tint = C_AMBER;
			}
			bool planeTool = shape_tool (A.tool) || A.tool == T_SKETCH;
			if ((int) bi == A.hovBody && A.hovFace >= 0 && planeTool && A.step == 0 && b.mesh.flat[A.hovFace]) tintFace = A.hovFace;
			unsigned col = bp.colour;
			if (!A.sketching && A.tool == T_SELECT && b.id == A.selBody) col = mix_rgb (col, ACC (), 0.18);
			if ((A.tool == T_MOVE || A.tool == T_UNION || A.tool == T_SUB || A.tool == T_INT) && (((int) bi == A.hovBody) || b.id == A.pend.target)) col = mix_rgb (col, ACC (), 0.35);
			scene_body (sc, b.mesh, col, thru ? 120 : 255, tintFace, tint, fade ? 0.5 : 0, 0xECEFF4);
			if (A.showEdges)
			{
				sc.batch (KAPI_GPU_B_ZFUNC (KAPI_GPU_Z_LEQUAL) | KAPI_GPU_B_NOZWRITE, false);
				scene_edges (sc, b.mesh, fade ? 0x8C98AC : 0x222E42, 0.6);
			}
		}
		if (A.camMode) camScene ();
		// what the tool would make
		if (A.hasPrev && A.prev.tris ())
		{
			unsigned col = (A.pend.kind == F_FILLET || A.pend.kind == F_CHAMFER) ? ACC () : A.pend.op == OP_SUB ? 0xEC6858 : 0x60A0EC;
			unsigned edge = A.pend.op == OP_SUB && A.pend.kind != F_FILLET && A.pend.kind != F_CHAMFER ? 0xAA2C22 : 0x1E54AA;
			if (A.pend.kind == F_MOVE) col = 0x60A0EC;
			sc.batch (KAPI_GPU_B_CULL_BACK | KAPI_GPU_B_BLEND (KAPI_GPU_BLEND_ALPHA) | KAPI_GPU_B_NOZWRITE | KAPI_GPU_B_ZFUNC (KAPI_GPU_Z_ALWAYS), true);
			scene_body (sc, A.prev, col, 150);
			sc.batch (KAPI_GPU_B_ZFUNC (KAPI_GPU_Z_ALWAYS) | KAPI_GPU_B_NOZWRITE, false);
			scene_edges (sc, A.prev, edge, 0.6);
		}
		A.gpu = scene_show (sc, 0xF0F2F6, canvas.px, canvas.stride);
		overlays ();
	}
};

// The orientation cube: its faces turned as the view, their names; a click on one looks at the model from there.
static const struct { V3 n; const char *name; double az, el; } CUBE[6] = {
	{ V3 (0, 0, 1), "TOP", -90, 90 }, { V3 (0, 0, -1), "BOTTOM", -90, -90 }, { V3 (0, -1, 0), "FRONT", -90, 0 },
	{ V3 (0, 1, 0), "BACK", 90, 0 }, { V3 (1, 0, 0), "RIGHT", 0, 0 }, { V3 (-1, 0, 0), "LEFT", 180, 0 } };
static void cube_face (const Cam &c, int f, double cx, double cy, double s, int *xy)
{
	V3 n = CUBE[f].n, a = fabs (n.z) > 0.5 ? V3 (1, 0, 0) : V3 (0, 0, 1), b = cross (n, a);
	const double sg[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
	for (int k = 0; k < 4; k++)
	{
		V3 p = (n + a * sg[k][0] + b * sg[k][1]) * 0.5;
		xy[k * 2] = (int) ((cx + dot (p, c.r) * s) * 16); xy[k * 2 + 1] = (int) ((cy - dot (p, c.u) * s) * 16);
	}
}
bool View::cubeHit (int mx, int my, double *az, double *el)
{
	double cx = width - 56, cy = 58;
	if (hypot (mx - cx, my - cy) > 46) return false;
	for (int f = 0; f < 6; f++)
	{
		if (dot (CUBE[f].n, A.cam.d) <= 0.05) continue;
		int xy[8]; cube_face (A.cam, f, cx, cy, 40, xy); bool in = true; int sign = 0;
		for (int k = 0; k < 4 && in; k++)
		{
			double x0 = xy[k * 2] / 16.0, y0 = xy[k * 2 + 1] / 16.0, x1 = xy[(k + 1) % 4 * 2] / 16.0, y1 = xy[(k + 1) % 4 * 2 + 1] / 16.0;
			double cr = (x1 - x0) * (my - y0) - (y1 - y0) * (mx - x0); int s = cr > 0 ? 1 : -1;
			if (sign == 0) sign = s; else if (s != sign) in = false;
		}
		if (in) { *az = CUBE[f].az; *el = CUBE[f].el; return true; }
	}
	return false;
}
void View::drawCube ()
{
	double cx = width - 56, cy = 58;
	if (A.sketching)
	{
		uk_rbox (canvas, width - 82, 14, 64, 64, 4, 0xFCFCFD, 0xFCFCFD); uk_rline (canvas, width - 82, 14, 64, 64, 4, 0x8C929E);
		UkFaceScope fs (g_small); const char *nm = "FACE";
		for (int f = 0; f < 6; f++) if (dot (CUBE[f].n, A.sk.pl.n) > 0.99) nm = CUBE[f].name;
		uk_text (canvas, width - 50 - uk_tw (nm, 2) / 2, 46 - uk_fh () / 2, nm, 0x686870, 2);
		return;
	}
	VPath p; p.ellipse ((int) (cx * 16), (int) ((cy + 37) * 16), 42 * 16, 11 * 16); p.fill (canvas, 0x7882A0, 36);
	for (int f = 0; f < 6; f++)
	{
		double k = dot (CUBE[f].n, A.cam.d);
		if (k <= 0.02) continue;
		int xy[8]; cube_face (A.cam, f, cx, cy, 40, xy);
		double l = lit (A.cam, CUBE[f].n); unsigned col = shade_rgb (0xF4F5F8, l > 1.02 ? 1.02 : l * 0.98 + 0.02);
		p.clear (); p.poly (xy, 4); p.fill (canvas, col);
		p.clear (); p.polyline (xy, 4, 16, true); p.fill (canvas, 0x8C929E);
		if (k > 0.35)
		{
			UkFaceScope fs (g_tiny); const char *nm = CUBE[f].name;
			int mx = (xy[0] + xy[2] + xy[4] + xy[6]) / 64, my = (xy[1] + xy[3] + xy[5] + xy[7]) / 64;
			uk_text (canvas, mx - uk_tw (nm, 2) / 2, my - uk_fh () / 2, nm, uk_mix (0x686870, col, (int) ((1 - k) * 200)), 2);
		}
	}
}
// A sketch on its plane: its elements, the closed outlines filled; editing: its points, its values, the element
// being placed.
void View::sketchDraw (const Feature &sk, const SkEval &ev, bool editing)
{
	const Plane &pl = sk.pl;
	auto P = [&] (const V2 &q, double *x, double *y) { sx (pl.at (q.x, q.y), x, y); };
	for (const SimplePolygon &poly : ev.closed)
	{
		std::vector<int> xy; xy.reserve (poly.size () * 2);
		for (auto &q : poly) { double x, y; P (V2 (q.x, q.y), &x, &y); if (fabs (x) > 6000 || fabs (y) > 6000) { xy.clear (); break; } xy.push_back ((int) (x * 16)); xy.push_back ((int) (y * 16)); }
		if (xy.size () >= 6) { VPath p; p.poly (&xy[0], (int) xy.size () / 2); p.fill (canvas, ACC (), editing ? 56 : 34); }
	}
	for (size_t i = 0; i < ev.shapes.size (); i++)
	{
		const SkShape &s = ev.shapes[i]; bool sel = editing && (int) i == A.selEl;
		unsigned col = sel ? C_AMBER : s.outline >= 0 || !editing ? C_LINE : 0x5A6E8C;
		for (size_t k = 0; k + 1 < s.pts.size (); k++)
		{
			double x0, y0, x1, y1; P (s.pts[k], &x0, &y0); P (s.pts[k + 1], &x1, &y1);
			ov_line (canvas, x0, y0, x1, y1, sel ? 2.4 : 1.7, col);
		}
	}
	if (!editing) return;
	for (size_t i = 0; i < ev.shapes.size (); i++)
	{
		const SkShape &s = ev.shapes[i]; const SkEl &e = sk.els[i]; double x, y; char a[24], t[48];
		if (e.kind == SK_POINT)				// a mark: a small cross
		{
			unsigned c = (int) i == A.selEl ? C_AMBER : 0x7A5AB0; P (s.start, &x, &y);
			ov_line (canvas, x - 6, y, x + 6, y, 1.3, c); ov_line (canvas, x, y - 6, x, y + 6, 1.3, c); ov_dot (canvas, x, y, 2.6, 0xFFFFFF, c, 1.5); continue;
		}
		if (e.kind == SK_SPLINE) for (const V2 &q : e.pts) { P (q, &x, &y); ov_dot (canvas, x, y, 2.4, 0xFFFFFF, C_LINE, 1.2); }
		P (s.start, &x, &y); ov_dot (canvas, x, y, 3.2, 0xFFFFFF, C_LINE);
		P (s.end, &x, &y); ov_dot (canvas, x, y, 3.2, 0xFFFFFF, C_LINE);
		if (e.kind == SK_ARC || e.kind == SK_CIRCLE) { P (s.centre, &x, &y); ov_dot (canvas, x, y, 2.2, C_LINE, C_LINE, 1); }
		// its value, beside it
		t[0] = 0;
		if (e.kind == SK_LINE) { fmt (e.len, a); snprintf (t, sizeof t, "%s", a); V2 m ((s.start.x + s.end.x) / 2, (s.start.y + s.end.y) / 2); P (m, &x, &y); double nx = -(s.end.y - s.start.y), ny = s.end.x - s.start.x, l = hypot (nx, ny); if (l > 0) { x += nx / l * 16; y += -ny / l * -16; } }
		else if (e.kind == SK_ARC) { fmt (e.r, a); snprintf (t, sizeof t, "R %s", a); P (s.pts[s.pts.size () / 2], &x, &y); double cx2, cy2; P (s.centre, &cx2, &cy2); double l = hypot (x - cx2, y - cy2); if (l > 0) { x += (x - cx2) / l * 26; y += (y - cy2) / l * 16; } }
		else if (e.kind == SK_CIRCLE) { fmt (e.w, a); snprintf (t, sizeof t, "\xC3\x98 %s", a); P (V2 (s.centre.x, s.centre.y + e.w / 2), &x, &y); y -= 16; }
		else if (e.kind == SK_RECT) { char b[24]; fmt (fabs (e.w), a); fmt (fabs (e.h), b); snprintf (t, sizeof t, "%s \xC3\x97 %s", a, b); P (e.rel ? V2 (e.x, e.y + fabs (e.h) / 2) : V2 (e.x + e.w / 2, e.y + (e.h > 0 ? e.h : 0)), &x, &y); y -= 16; }
		if (t[0]) ov_tag (canvas, (int) x, (int) y, t, false, 0);
	}
	// the element being placed
	if (A.curStep > 0)
	{
		std::vector<SkEl> one (1, A.cur); one[0].chain = false; SkEval e1; sketch_eval (one, A.doc.segs, e1);
		const SkEl &e = A.cur; double x, y, x1, y1; char a[24], t[48];
		if (!e1.shapes.empty ())
		{
			const SkShape &s = e1.shapes[0];
			if (!(e.kind == SK_ARC && A.curStep == 1))
				for (size_t k = 0; k + 1 < s.pts.size (); k++) { P (s.pts[k], &x, &y); P (s.pts[k + 1], &x1, &y1); ov_line (canvas, x, y, x1, y1, 2.4, C_AMBER); }
			if (e.kind == SK_SPLINE) for (const V2 &q : e.pts) { P (q, &x, &y); ov_dot (canvas, x, y, 3, 0xFFFFFF, C_AMBER, 1.6); }
			else if (e.kind == SK_ARC && A.tool == T_ARC3)		// the chord, the line its middle slides on
			{
				double x2, y2; P (V2 (e.x, e.y), &x, &y); P (A.arcC, &x1, &y1); ov_dash (canvas, x, y, x1, y1, 1, C_AMBER);
				ov_dot (canvas, x, y, 3.2, 0xFFFFFF, C_AMBER, 1.8); ov_dot (canvas, x1, y1, 3.2, 0xFFFFFF, C_AMBER, 1.8);
				if (A.curStep == 2)
				{
					P (A.arcM, &x2, &y2); ov_dash (canvas, (x + x1) / 2, (y + y1) / 2, x2, y2, 1, C_AMBER); ov_dot (canvas, x2, y2, 3.2, 0xFFFFFF, C_AMBER, 1.8);
					if (e.r > 1e-6) { fmt (e.r, a); snprintf (t, sizeof t, "R %s", a); ov_tag (canvas, (int) x2, (int) y2 - 18, t, true, C_AMBER); }
				}
			}
			else if (e.kind == SK_ARC)
			{
				double ca = e.ca * PI / 180; V2 C (e.x + e.r * cos (ca), e.y + e.r * sin (ca));
				P (V2 (e.x, e.y), &x, &y); P (C, &x1, &y1); ov_dash (canvas, x, y, x1, y1, 1, C_AMBER);
				if (A.curStep == 2) { double x2, y2; P (s.end, &x2, &y2); ov_dash (canvas, x1, y1, x2, y2, 1, C_AMBER); }
				ov_dot (canvas, x1, y1, 3.2, 0xFFFFFF, C_AMBER, 1.8);
				if (A.curStep == 1) { fmt (e.r, a); snprintf (t, sizeof t, "R %s", a); ov_tag (canvas, (int) (x + x1) / 2, (int) (y + y1) / 2 - 16, t, true, C_AMBER); }
				else { fmt (e.sweep, a); snprintf (t, sizeof t, "%s\xC2\xB0", a); P (s.pts[s.pts.size () / 2], &x, &y); ov_tag (canvas, (int) (x + (x - x1) * 0.5), (int) (y + (y - y1) * 0.5), t, true, C_AMBER); }
			}
			else if (e.kind == SK_LINE)
			{
				P (s.start, &x, &y); P (s.end, &x1, &y1); char b[24]; fmt (e.len, a); fmt (e.a, b); snprintf (t, sizeof t, "%s \xC2\xB7 %s\xC2\xB0", a, b);
				ov_tag (canvas, (int) (x + x1) / 2, (int) (y + y1) / 2 - 18, t, true, C_AMBER);
			}
			else if (e.kind == SK_CIRCLE) { fmt (e.w, a); snprintf (t, sizeof t, "\xC3\x98 %s", a); P (V2 (e.x, e.y + e.w / 2), &x, &y); ov_tag (canvas, (int) x, (int) y - 16, t, true, C_AMBER); }
			else if (e.kind == SK_RECT) { char b[24]; fmt (fabs (e.w), a); fmt (fabs (e.h), b); snprintf (t, sizeof t, "%s \xC3\x97 %s", a, b); P (e.rel ? V2 (e.x, e.y + fabs (e.h) / 2) : V2 (e.x + e.w / 2, e.y + (e.h > 0 ? e.h : 0)), &x, &y); ov_tag (canvas, (int) x, (int) y - 16, t, true, C_AMBER); }
			// the run would close here
			if ((e.kind == SK_LINE || (e.kind == SK_ARC && A.curStep == 2)) && A.chain)
			{
				V2 first; bool got = false;
				for (int i = (int) A.sk.els.size () - 1; i >= 0 && !got; i--) if (!A.sk.els[i].chain || i == 0) { first = A.ev.shapes[i].start; got = true; }
				if (got && hypot (s.end.x - first.x, s.end.y - first.y) < 1e-6)
				{
					P (first, &x, &y); ov_dot (canvas, x, y, 6.5, 0xFFFFFF, C_GREEN, 2); ov_dot (canvas, x, y, 3, 0xFFFFFF, C_GREEN, 1.8);
					UkFaceScope fs (g_small); uk_text (canvas, (int) x - uk_tw ("closes the outline") - 12, (int) y + 8, "closes the outline", 0x287838);
				}
			}
		}
	}
	else if (A.curStep == 0 && A.tool >= T_LINE) { double x, y; P (V2 (A.cur.x, A.cur.y), &x, &y); ov_dot (canvas, x, y, 3.2, 0xFFFFFF, C_AMBER, 1.8); }
	if (A.snapOn && A.selEl < 0)			// held by a point: a green ring on it
	{
		double x, y; P (A.snapP, &x, &y); const double d = 6.5;
		ov_line (canvas, x - d, y, x, y - d, 1.5, C_GREEN); ov_line (canvas, x, y - d, x + d, y, 1.5, C_GREEN); ov_line (canvas, x + d, y, x, y + d, 1.5, C_GREEN); ov_line (canvas, x, y + d, x - d, y, 1.5, C_GREEN);
	}
}
void View::overlays ()
{
	const Feature &f = A.pend; char a[24], t[64];
	arrowX = arrowY = -1;
	if (A.camMode) { camOverlays (); chrome (); return; }
	// the sketches not yet used; the one being drawn
	if (A.sketching) sketchDraw (A.sk, A.ev, true);
	else
		for (int i = 0; i < A.doc.upto && i < (int) A.doc.feats.size (); i++)
		{
			const Feature &sk = A.doc.feats[i];
			if (sk.kind != F_SKETCH || (A.doc.used (i) && A.selFeat != i && !(A.hasPend && f.kind == F_EXTRUDE && f.sketch == i))) continue;
			SkEval ev; sketch_eval (sk.els, A.doc.segs, ev); sketchDraw (sk, ev, false);
		}
	// the Sketch tool, before its plane is chosen: the one the panel proposes, shown in the model
	if (A.tool == T_SKETCH && !A.sketching)
	{
		double o = A.skOffset;
		Plane pl = A.skPlane == 1 ? plane_of (V3 (0, o, 0), V3 (0, -1, 0)) : A.skPlane == 2 ? plane_of (V3 (o, 0, 0), V3 (1, 0, 0)) : plane_of (V3 (0, 0, o), V3 (0, 0, 1));
		V3 q[4] = { pl.at (-70, -50), pl.at (70, -50), pl.at (70, 50), pl.at (-70, 50) }; int xy[8]; bool ok = true;
		for (int i = 0; i < 4; i++) { double x, y; sx (q[i], &x, &y); if (fabs (x) > 6000 || fabs (y) > 6000) ok = false; xy[i * 2] = (int) (x * 16); xy[i * 2 + 1] = (int) (y * 16); }
		if (ok) { VPath p; p.poly (xy, 4); p.fill (canvas, ACC (), 40); }
		for (int i = 0; i < 4; i++) line3 (q[i], q[(i + 1) % 4], 1.6, ACC ());
		line3 (pl.at (0, 0), pl.at (30, 0), 2, 0xD6483E); line3 (pl.at (0, 0), pl.at (0, 30), 2, 0x40A04C);
		tag3 (q[3], 30, -14, A.skPlane == 1 ? "XZ" : A.skPlane == 2 ? "YZ" : "XY");
	}
	// a box, a cylinder: the base, the sizes; the height's arrow
	if (A.hasPend && (f.kind == F_BOX || round_kind (f.kind)) && A.step >= 1)
	{
		Plane pl = f.pl; pl.o = pl.o + pl.n * f.z; unsigned col = f.op == OP_SUB ? C_RED : ACC ();
		double ta = f.turn * PI / 180, tc = cos (ta), ts = sin (ta);
		auto at = [&] (double x, double y) { double dx = x - f.x, dy = y - f.y; return pl.at (f.x + dx * tc - dy * ts, f.y + dx * ts + dy * tc); };	// (turned)
		if (f.kind == F_BOX)
		{
			double x0 = f.centred ? f.x - fabs (f.w) / 2 : std::min (f.x, f.x + f.w), y0 = f.centred ? f.y - fabs (f.d) / 2 : std::min (f.y, f.y + f.d), w = fabs (f.w), d = fabs (f.d);
			V3 c0 = at (x0, y0), c1 = at (x0 + w, y0), c2 = at (x0 + w, y0 + d), c3 = at (x0, y0 + d);
			line3 (c0, c1, 2, col); line3 (c1, c2, 2, col); line3 (c2, c3, 2, col); line3 (c3, c0, 2, col);
			double px, py; sx (pl.at (f.x, f.y), &px, &py); ov_dot (canvas, px, py, 3.2, 0xFFFFFF, col);
			fmt (w, a); tag3 ((c0 + c1) * 0.5, 0, 18, a, A.step == 1 && !A.typed[0], C_AMBER);
			fmt (d, a); tag3 ((c1 + c2) * 0.5, 22, 4, a);
		}
		else
		{
			bool torus = f.kind == F_TORUS, cyl = f.kind == F_CYL; double rad = cyl ? f.w / 2 : f.w;
			int ns = !cyl && !torus && f.n >= 2.5 ? (int) (f.n + 0.5) : 48;				// (the base as it will be: its sides)
			auto ring = [&] (double r, unsigned c, double lw) { V3 prev; for (int i = 0; i <= ns; i++) { double an = 2 * PI * i / ns + (ns < 48 ? ta : 0); V3 p = pl.at (f.x + r * cos (an), f.y + r * sin (an)); if (i) line3 (prev, p, lw, c); prev = p; } };
			ring (rad, col, 2);
			double px, py; sx (pl.at (f.x, f.y), &px, &py); ov_dot (canvas, px, py, 3.2, 0xFFFFFF, col);
			fmt (f.w, a); snprintf (t, sizeof t, cyl ? "\xC3\x98 %s" : "R %s", a); tag3 (pl.at (f.x, f.y + rad), 0, -18, t, A.step == 1, C_AMBER);
			if (f.kind == F_TAPER && A.step == 2)							// the top's outline, where the base is
			{ ring (f.d, C_AMBER, 2); fmt (f.d, a); snprintf (t, sizeof t, "top R %s", a); tag3 (pl.at (f.x + f.d, f.y), 44, 0, t, true, C_AMBER); }
			if (torus && A.step == 2)								// the tube: from the ring outward
			{
				line3 (pl.at (f.x + f.w, f.y), pl.at (f.x + f.w + f.d, f.y), 2, C_AMBER);
				fmt (f.d, a); snprintf (t, sizeof t, "tube R %s", a); tag3 (pl.at (f.x + f.w + f.d, f.y), 52, 0, t, true, C_AMBER);
			}
		}
	}
	if (A.hasPend && makes_solid (f.kind) && f.kind != F_TORUS && f.kind != F_SPHERE && A.step == height_step ())
	{
		V3 c = baseCentre (), n = pendNormal (); unsigned col = f.op == OP_SUB ? C_RED : C_AMBER;
		double h = f.through ? -fabs (f.h ? f.h : 10) : f.h;
		V3 tip = c + n * h; double x0, y0, x1, y1; sx (tip, &x0, &y0);
		V3 far = tip + n * ((h < 0 ? -1 : 1) * 46 / A.cam.scale); sx (far, &x1, &y1);
		if (hypot (x1 - x0, y1 - y0) < 20) { x1 = x0; y1 = y0 - 46; }
		ov_arrow (canvas, x0, y0, x1, y1, col, true);
		if (f.through) snprintf (t, sizeof t, "through"); else { fmt (fabs (f.h), a); snprintf (t, sizeof t, "%s mm", a); }
		ov_tag (canvas, (int) x1 + 44, (int) y1 + 14, t, true, col);
	}
	// the edges: chosen, pointed
	if (A.hasPend && (f.kind == F_FILLET || f.kind == F_CHAMFER))
	{
		Body *b = A.doc.body (f.target);
		if (b) for (const V3 &p : f.edges) { int ci = chain_near (b->mesh, p); if (ci >= 0) chainDraw (b->mesh, b->mesh.chains[ci], 3, ACC ()); }
		if (A.hovChain >= 0 && A.hovBody >= 0 && A.hovBody < (int) A.doc.bodies.size ())
		{
			const RMesh &m = A.doc.bodies[A.hovBody].mesh;
			if (A.hovChain < (int) m.chains.size ()) chainDraw (m, m.chains[A.hovChain], 3, m.chains[A.hovChain].kind ? C_AMBER : 0x9A9AA2);
		}
		if (b && !f.edges.empty ())
		{
			double x0, y0; sx (f.edges[0], &x0, &y0);
			double l = f.r * A.cam.scale / 0.8; if (l < 26) l = 26; if (l > 160) l = 160;
			double x1 = x0 - l * 0.62, y1 = y0 - l * 0.78;
			ov_arrow (canvas, x0, y0, x1, y1, C_AMBER, false); ov_dot (canvas, x1, y1, 5, 0xFFFFFF, C_AMBER, 2);
			arrowX = x1; arrowY = y1;
			fmt (f.r, a); snprintf (t, sizeof t, f.kind == F_FILLET ? "R %s" : "%s", a); ov_tag (canvas, (int) x1 - 36, (int) y1 - 10, t, true, C_AMBER);
		}
	}
	// a move: from where to where
	if (A.hasPend && f.kind == F_MOVE && A.step == 1)
	{
		line3 (A.mA, A.mA + f.mv, 1.6, C_AMBER, true); double x, y; sx (A.mA + f.mv, &x, &y); ov_dot (canvas, x, y, 4, 0xFFFFFF, C_AMBER, 2);
		char b[24], c[24]; fmt (f.mv.x, a); fmt (f.mv.y, b); fmt (f.mv.z, c); snprintf (t, sizeof t, "%s, %s, %s", a, b, c); ov_tag (canvas, (int) x, (int) y - 20, t, true, C_AMBER);
	}
	if (A.tool == T_MEASURE && A.mN >= 1)
	{
		double x, y; sx (A.mA, &x, &y); ov_dot (canvas, x, y, 4, 0xFFFFFF, C_AMBER, 2);
		if (A.mN == 2)
		{
			line3 (A.mA, A.mB, 1.6, C_AMBER); double x1, y1; sx (A.mB, &x1, &y1); ov_dot (canvas, x1, y1, 4, 0xFFFFFF, C_AMBER, 2);
			fmt (len (A.mB - A.mA), a); snprintf (t, sizeof t, "%s mm", a); ov_tag (canvas, (int) (x + x1) / 2, (int) (y + y1) / 2 - 16, t, false, 0);
		}
	}
	chrome ();
}
// The view's own: the bodies, the cube, its buttons, the axes, what is being made, what went wrong.
void View::chrome ()
{
	drawBodies ();
	drawCube ();
	int bx = width - 40, by = 124;
	uk_rbox (canvas, bx, by, 30, 88, 6, 0xFFFFFF, 0xFFFFFF, 215); uk_rline (canvas, bx, by, 30, 88, 6, 0xB0B4BE);
	icon (canvas, I_HOME, bx + 6, by + 6, 18, 0x464C5A, ACC (), 0xFFFFFF); icon (canvas, I_FIT, bx + 6, by + 34, 18, 0x464C5A, ACC (), 0xFFFFFF);
	icon (canvas, I_SHADED, bx + 6, by + 62, 18, A.seeThrough ? ACC () : 0x464C5A, ACC (), 0xFFFFFF);
	if (!A.sketching)
	{
		double ox = 34, oy = height - 34;
		const struct { V3 v; unsigned c; const char *n; } ax[3] = { { V3 (1, 0, 0), 0xD6483E, "X" }, { V3 (0, 1, 0), 0x40A04C, "Y" }, { V3 (0, 0, 1), 0x3A7AD6, "Z" } };
		for (int i = 0; i < 3; i++)
		{
			double x = ox + dot (ax[i].v, A.cam.r) * 24, y = oy - dot (ax[i].v, A.cam.u) * 24;
			ov_line (canvas, ox, oy, x, y, 2, ax[i].c);
			UkFaceScope fs (g_small); uk_text (canvas, (int) (ox + (x - ox) * 1.38) - 4, (int) (oy + (y - oy) * 1.38) - uk_fh () / 2, ax[i].n, ax[i].c, 2);
		}
		ov_dot (canvas, ox, oy, 2, 0x5A5E68, 0x5A5E68, 1);
	}
	if (A.caption[0])
	{
		UkFaceScope fs (g_small); int w = uk_tw (A.caption) + 22;
		uk_rbox (canvas, 10, 10, w, 24, 12, 0xFFFFFF, 0xFFFFFF, 225); uk_rline (canvas, 10, 10, w, 24, 12, 0xB0B4BE);
		uk_text (canvas, 21, 10 + (24 - uk_fh ()) / 2, A.caption, 0x3C404C);
	}
	if (A.prevErr)
	{
		UkFaceScope fs (g_small); int w = uk_tw (A.prevErr) + 44, x = (width - w) / 2, y = height - 40;
		uk_rbox (canvas, x, y, w, 26, 13, 0xFFF4E0, 0xFFF4E0); uk_rline (canvas, x, y, w, 26, 13, C_AMBER);
		icon (canvas, I_WARN, x + 10, y + 4, 18, 0x4A3A10, ACC (), 0xFFF4E0); uk_text (canvas, x + 34, y + (26 - uk_fh ()) / 2, A.prevErr, 0x4A3A10);
	}
	uk_rline (canvas, 0, 0, width, height, 5, 0x9698A0);
}

} // namespace forge

#endif
