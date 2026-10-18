//
// 3dforge/fui.h -- the window around the view: the tools' bar (a picture and its name each; the sketch's own while a
// sketch is drawn), under the view the timeline (the history as a row of pictures; the sketch's elements), at the right
// the selection -- the step being made or edited: its values as fields, its operation --, the status bar. (The bodies
// are listed in the view itself: fview.h.)
//
// MIT licence (Onyx).
//
#ifndef _3dforge_fui_h
#define _3dforge_fui_h

#include "fview.h"

namespace forge {

enum { CMD_NEW = 100, CMD_OPEN, CMD_SAVE, CMD_UNDO, CMD_REDO, CMD_EXPORT, CMD_SK_CANCEL, CMD_SK_FINISH, CMD_SK_CLOSE, CMD_OK, CMD_CANCEL,
       CMD_DELETE, CMD_EDIT_SKETCH, CMD_ROLL_HERE, CMD_ROLL_END, CMD_DEL_ELEMENT, CMD_SHAPES, CMD_SK_START,
       CMD_MODE_DESIGN, CMD_MODE_CAM, CMD_CAM_SETUP, CMD_CAM_TOOL, CMD_CAM_CLEAR, CMD_CAM_CONTOUR, CMD_CAM_SIM, CMD_GCODE, CMD_CAM_BODY, CMD_CAM_DELETE,
       CMD_CAM_VALIDATE, CMD_CAM_REVERT, CMD_CANVAS_IMPORT, CMD_CANVAS_DELETE, CMD_PR_SETUP, CMD_PR_RESIN, CMD_PR_SUPPORTS, CMD_PR_LAYERS, CMD_PR_FILE, CMD_PR_GENERATE, CMD_PR_CLEAR, CMD_PR_PRESET, CMD_GEN, CMD_SK_CONS };
static void cam_presets_save ();			// (main.cpp)
static void print_presets_save ();
static void fdm_presets_save ();

static unsigned soft_col (unsigned bg);
// The shapes, unfolded under their button: a picture and its name each. run (): the tool chosen, -1 none.
class ShapesPopup : public Modal
{
public:
	enum { TW = 72, TH = 62, COLS = 4, N = 7 };
	int hot;
	ShapesPopup (int x, int y) : Modal (COLS * TW + 12, 2 * TH + 12), hot (-1) { left = x; top = y; }
	static const int *tools () { static const int t[N] = { T_BOX, T_CYL, T_SPHERE, T_TORUS, T_PYRAMID, T_PRISM, T_TAPER }; return t; }
	int at (int mx, int my) const
	{
		if (mx < 6 || my < 6 || mx >= width - 6 || my >= height - 6) return -1;
		int i = (my - 6) / TH * COLS + (mx - 6) / TW; return i < N ? i : -1;
	}
	void onDraw () override
	{
		static const int ic[N] = { I_BOX, I_CYL, I_SPHERE, I_TORUS, I_PYRAMID, I_PRISM, I_TAPER };
		static const char *const nm[N] = { TR ("Box"), TR ("Cylinder"), TR ("Sphere"), TR ("Torus"), TR ("Pyramid"), TR ("Prism"), TR ("Taper") };
		unsigned face = uk_tone (C_BG, 176); canvas.clear (UK_TRANSPARENT_KEY); uk_popup (canvas, 0, 0, width, height, 8, face);
		UkFaceScope fs (g_small);
		for (int i = 0; i < N; i++)
		{
			int x = 6 + i % COLS * TW, y = 6 + i / COLS * TH; unsigned bg = face;
			if (i == hot) { bg = soft_col (face); uk_rbox (canvas, x + 2, y + 2, TW - 4, TH - 4, 6, bg, bg); uk_rline (canvas, x + 2, y + 2, TW - 4, TH - 4, 6, C_ACCENT); }
			icon (canvas, ic[i], x + TW / 2 - 14, y + 7, 28, uk_mix (C_TEXT, bg, 40), C_ACCENT, bg);
			uk_text (canvas, x + (TW - uk_tw (nm[i])) / 2, y + 40, nm[i], C_TEXT);
		}
	}
	bool wasL = false;
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int h = at (mx, my); bool press = bl && !wasL; wasL = bl != 0;
		if (h != hot) { hot = h; invalidate (true); }
		if (press) close (h >= 0 ? tools ()[h] : -1);
		return true;
	}
	bool onKey (long k) override { if (k == 27) close (-1); return true; }
};
static void cmd (int id);				// (main.cpp)
static View *g_view;

static unsigned panel_col () { return uk_tone (C_BG, 121); }
static unsigned soft_col (unsigned bg) { return uk_mix (C_ACCENT, bg, 180); }
static unsigned dim_col () { return uk_mix (C_TEXT, C_BG, 110); }

// A push button in the accent (the one that finishes: OK, Export, Finish sketch) or plain, with an icon if wanted.
class ABtn : public Widget
{
public:
	char text[40]; bool accent; int ic, id; unsigned bg;
	ABtn (int l, int t, int w, int h, const char *s, int id_, bool accent_ = false, int icon_ = -1) : Widget (l, t, w, h), accent (accent_), ic (icon_), id (id_), bg (UK_AUTO)
	{ snprintf (text, sizeof text, "%s", s); }
	unsigned bgColor () override { return bg == UK_AUTO ? Widget::bgColor () : bg; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		unsigned face = accent ? C_ACCENT : C_BUTTON, ink = accent ? 0xFFFFFF : C_BUTTON_TEXT;
		uk_raised (canvas, 0, 0, width, height, 5, face, pressed ? UK_PRESSED : hover ? UK_HOT : UK_NORMAL);
		int tw = uk_tw (text, accent ? 2 : 0), x = (width - tw - (ic >= 0 ? 26 : 0)) / 2;
		if (ic >= 0) { icon (canvas, ic, x, (height - 18) / 2, 18, ink, ink, face); x += 26; }
		uk_text (canvas, x, (height - uk_fh ()) / 2 + (pressed ? 1 : 0), text, ink, accent ? 2 : 0);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != hover) { hover = in; invalidate (true); }
		if (in && bl && !pressed) { pressed = true; invalidate (true); }
		else if (!bl && pressed) { pressed = false; invalidate (true); if (in) cmd (id); }
		return in;
	}
};

// ---- the tools' bar ---------------------------------------------------------------------------------------------------
struct ToolDef { int tool, ic; const char *name; };
static const ToolDef TOOLS[] = {
	{ -4, I_SHAPES, TRN ("Shapes") }, { T_SKETCH, I_SKETCH, TRN ("Sketch") }, { T_EXTRUDE, I_EXTRUDE, TRN ("Extrude") }, { -1, 0, 0 },
	{ T_FILLET, I_FILLET, TRN ("Fillet") }, { T_CHAMFER, I_CHAMFER, TRN ("Chamfer") }, { T_MOVE, I_MOVE, TRN ("Move") }, { -1, 0, 0 },
	{ T_UNION, I_UNION, TRN ("Union") }, { T_SUB, I_SUB, TRN ("Subtract") }, { T_INT, I_INT, TRN ("Intersect") }, { -1, 0, 0 }, { T_MEASURE, I_MEASURE, TRN ("Measure") }, { -2, 0, 0 } };
static const ToolDef CAMTOOLS[] = {
	{ -10, I_CAM_SETUP, TRN ("Setup") }, { -11, I_CAM_TOOL, TRN ("Tool") }, { -1, 0, 0 }, { -12, I_CAM_CLEAR, TRN ("Clearing") }, { -13, I_CAM_CONTOUR, TRN ("Contour") }, { -1, 0, 0 },
	{ -14, I_PLAY, TRN ("Simulate") }, { -2, 0, 0 } };
static const ToolDef PRINTTOOLS[] = {
	{ -20, I_PR_PLATE, TRN ("Setup") }, { -21, I_PR_RESIN, TRN ("Resin") }, { -1, 0, 0 }, { -22, I_PR_SUP, TRN ("Supports") }, { -1, 0, 0 }, { -23, I_PR_LAYERS, TRN ("Layers") }, { -2, 0, 0 } };
static const ToolDef FDMTOOLS[] = {
	{ -20, I_PR_PLATE, TRN ("Setup") }, { -21, I_PR_FIL, TRN ("Filament") }, { -1, 0, 0 }, { -23, I_PR_LAYERS, TRN ("Layers") }, { -2, 0, 0 } };
static const ToolDef SKTOOLS[] = {
	{ T_LINE, I_LINE, TRN ("Line") }, { T_MIDLINE, I_MIDLINE, TRN ("Midpoint line") }, { T_RECT, I_RECT, TRN ("Rectangle") }, { T_CIRCLE, I_CIRCLE, TRN ("Circle") }, { T_ARC, I_ARC, TRN ("Arc") }, { T_ARC3, I_ARC3, TRN ("3-pt arc") },
	{ T_SPLINE, I_SPLINE, TRN ("Spline") }, { T_POINT, I_POINT, TRN ("Point") }, { -3, I_CLOSE, TRN ("Close") }, { -1, 0, 0 }, { -5, I_CONS, TRN ("Construction") }, { -2, 0, 0 } };

class Ribbon : public Widget
{
public:
	enum { H = 62, MAXHIT = 32 };
	struct Zone { int x, y, w, h, id; bool tool; } z[MAXHIT]; int nz, hot, down;
	ABtn *bExport, *bCancel, *bFinish, *bGcode, *bPrint, *bValid, *bRevert;
	Ribbon (int w) : Widget (0, 0, w, H), nz (0), hot (-1), down (-1)
	{
		bExport = new ABtn (w - 108, 14, 98, 28, TR ("Export"), CMD_EXPORT, true, I_EXPORT);
		bCancel = new ABtn (w - 232, 14, 84, 28, TR ("Cancel"), CMD_SK_CANCEL);
		bFinish = new ABtn (w - 140, 14, 130, 28, TR ("Finish sketch"), CMD_SK_FINISH, true);
		bGcode = new ABtn (w - 118, 14, 108, 28, TR ("G-code"), CMD_GCODE, true, I_GCODE);
		bPrint = new ABtn (w - 128, 14, 118, 28, TR ("Print file"), CMD_PR_FILE, true, I_GCODE);
		// (milling: values changed and not yet computed -- beside G-code, as a sketch's Cancel and Finish)
		bRevert = new ABtn (w - 118 - 10 - 100 - 8 - 84, 14, 84, 28, TR ("Cancel"), CMD_CAM_REVERT);
		bValid = new ABtn (w - 118 - 10 - 100, 14, 100, 28, TR ("Validate"), CMD_CAM_VALIDATE, true);
		for (ABtn *b : { bExport, bCancel, bFinish, bGcode, bPrint, bValid, bRevert }) { b->anchor = ANCHOR_TOP | ANCHOR_RIGHT; addChild (b); }
		sync ();
	}
	void sync () { bExport->hidden = A.sketching || A.camMode; bGcode->hidden = !A.camMode || A.gen != 0; bPrint->hidden = !resin ();
		bValid->hidden = bRevert->hidden = !(A.camMode && A.gen == 0 && A.camDirty); bCancel->hidden = bFinish->hidden = !A.sketching; invalidate (true); }
	void zone (int x, int y, int w, int h, int id, bool tool) { if (nz < MAXHIT) { Zone q = { x, y, w, h, id, tool }; z[nz++] = q; } }
	void onDraw () override
	{
		canvas.clear (C_BG); nz = 0;
		int x = 8; unsigned ink = uk_mix (C_TEXT, C_BG, 40);
		const int small[] = { I_NEW, I_OPEN, I_SAVE, -1, I_UNDO, I_REDO }, ids[] = { CMD_NEW, CMD_OPEN, CMD_SAVE, 0, CMD_UNDO, CMD_REDO };
		for (int i = 0; i < 6; i++)
		{
			if (small[i] < 0) { uk_etch_v (canvas, x + 3, 16, 32, C_BG); x += 9; continue; }
			bool off = (small[i] == I_UNDO && A.undo.empty ()) || (small[i] == I_REDO && A.redo.empty ());
			if (nz == hot && !off) uk_rbox (canvas, x, 14, 28, 30, 5, uk_tone (C_BG, 150), uk_tone (C_BG, 150));
			icon (canvas, small[i], x + 4, 19, 20, off ? C_DIS : ink, C_ACCENT, C_BG);
			zone (x, 14, 28, 30, ids[i], false); x += 28;
		}
		uk_etch_v (canvas, x + 3, 8, 46, C_BG); x += 12;
		UkFaceScope fs (g_small);
		if (!A.sketching)				// the app's two halves: Design, Manufacture
		{
			static const char *const MD[2] = { TR ("Design"), TR ("Manufacture") }; int sw = uk_tw (MD[1], 2) + 16;	// (one over the other: the bar is short)
			uk_sunken (canvas, x, 8, sw, 46, 5, C_FIELD);
			for (int i = 0; i < 2; i++)
			{
				int sy = 9 + i * 22; bool on = (i == 1) == A.camMode;
				if (on) uk_rbox (canvas, x + 1, sy, sw - 2, 22, 4, C_ACCENT, C_ACCENT);
				uk_text (canvas, x + (sw - uk_tw (MD[i], on ? 2 : 0)) / 2, sy + (22 - uk_fh ()) / 2, MD[i], on ? 0xFFFFFF : C_FIELD_TEXT, on ? 2 : 0);
				zone (x, sy, sw, 22, i ? CMD_MODE_CAM : CMD_MODE_DESIGN, false);
			}
			x += sw + 6; uk_etch_v (canvas, x, 8, 46, C_BG); x += 7;
		}
		const ToolDef *list = A.sketching ? SKTOOLS : A.camMode ? (A.gen == 2 ? FDMTOOLS : A.gen == 1 ? PRINTTOOLS : CAMTOOLS) : TOOLS;
		for (int i = 0; list[i].tool != -2; i++)
		{
			if (list[i].tool == -1) { uk_etch_v (canvas, x + 4, 8, 46, C_BG); x += 10; continue; }
			// (the shapes' button: an arrow -- a click unfolds them)
			bool more = list[i].tool == -4; int tl = list[i].tool, ic = list[i].ic;
			const char *nm = TR (list[i].name);
			int bw = uk_tw (nm) + (more ? 22 : 10); if (bw < 48) bw = 48;
			bool camTile = tl <= -10;
			bool on = tl <= -20 ? A.camPage == -20 - tl : camTile ? (tl == -10 ? A.camPage == 0 && !A.camSim : tl == -11 ? A.camPage == 1 && !A.camSim : tl == -14 ? A.camSim
						     : A.camPage == 2 && cam_op () && cam_op ()->kind == (tl == -12 ? CAM_CLEAR : CAM_CONTOUR) && !A.camSim)
					  : tl == -5 ? (A.selEl >= 0 && A.selEl < (int) A.sk.els.size () ? A.sk.els[A.selEl].cons : A.cons) : tl == A.tool || (more && shape_tool (A.tool));
			bool isHot = nz == hot;
			unsigned bg = C_BG;
			if (on) { bg = soft_col (C_BG); uk_rbox (canvas, x, 4, bw, 54, 6, bg, bg); uk_rline (canvas, x, 4, bw, 54, 6, C_ACCENT); }
			else if (isHot) { bg = uk_tone (C_BG, 150); uk_rbox (canvas, x, 4, bw, 54, 6, bg, bg); }
			icon (canvas, ic, x + bw / 2 - 13, 8, 26, ink, C_ACCENT, bg);
			int tx = x + (bw - uk_tw (nm) - (more ? 10 : 0)) / 2; uk_text (canvas, tx, 38, nm, C_TEXT);
			if (more) uk_glyph (canvas, WKG_CHEV_DOWN, tx + uk_tw (nm) + 6, 46, 7, C_TEXT);
			zone (x, 4, bw, 54, tl <= -20 ? CMD_PR_SETUP + (-20 - tl) : camTile ? CMD_CAM_SETUP + (-10 - tl) : more ? CMD_SHAPES : list[i].tool == -3 ? CMD_SK_CLOSE : list[i].tool == -5 ? CMD_SK_CONS : tl, !camTile && !more && list[i].tool != -3 && list[i].tool != -5); x += bw + 2;
		}
		uk_etch_h (canvas, 0, H - 2, width, C_BG);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int h = -1;
		for (int i = 0; i < nz; i++) if (mx >= z[i].x && mx < z[i].x + z[i].w && my >= z[i].y && my < z[i].y + z[i].h) h = i;
		if (h != hot) { hot = h; invalidate (true); }
		if (bl && down < 0 && h >= 0) down = h;
		else if (!bl && down >= 0)
		{
			int d = down; down = -1;
			if (d == h && z[d].id == CMD_SHAPES)
			{
				Root *r = Root::current (); int ax = 0, ay = 0; for (Widget *w = this; w && w != r; w = w->parent) { ax += w->left; ay += w->top; }
				ShapesPopup pm (ax + z[d].x, ay + z[d].y + z[d].h + 2);
				int c = pm.run (); hot = -1; if (c > 0) tool_set (c); else invalidate (true);
			}
			else if (d == h) { if (z[d].tool) tool_set (A.tool == z[d].id && !A.sketching ? T_SELECT : z[d].id); else cmd (z[d].id); }
		}
		if (!bl) down = -1;
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
};

// ---- the timeline: the history as a row of pictures, from the left; the sketch's elements while one is drawn ------------
// A click on a step shows it at the right (its values can be changed there); a double click on a sketch opens it; the
// right button: roll back to it, delete it. The blue bar: where the part is rebuilt up to. Longer than its room, it
// moves by its arrows, the wheel, or dragged.
class Timeline : public Widget
{
public:
	enum { H = 46, CELL = 38, ARROW = 22 };
	int scroll, hot, lastI, shownSel, shownN, pressX, pressScroll, pressI; unsigned lastT; bool wasL, wasR, moved;
	bool barDrag = false, barHot = false;		// (the blue bar -- where the part is rebuilt up to -- held, pointed)
	Timeline (int l, int t, int w) : Widget (l, t, w, H), scroll (0), hot (-1), lastI (-1), shownSel (-1), shownN (0), pressX (0), pressScroll (0), pressI (-1), lastT (0), wasL (false), wasR (false), moved (false) {}
	int count () const { return filament () ? 1 : resin () ? 2 : A.camMode ? 1 + (int) A.job.ops.size () : A.sketching ? (int) A.sk.els.size () : (int) A.doc.feats.size (); }
	int sel () const { return A.camMode ? A.camSel : A.sketching ? A.selEl : A.selFeat; }
	// what a step, or an element, says of itself
	void text (int i, char *out, int cap) const
	{
		char t[96], a[24], b[24], c[24];
		if (printer ())					// the setup, the supports
		{
			if (i == 0) snprintf (out, cap, TR ("Setup  \xC2\xB7  %s \xC2\xB7 %s"), body_name (A.print.body), A.gen == 2 ? TR ("a filament printer") : A.print.machine.name);
			else if (A.print.tips.empty ()) snprintf (out, cap, TR ("Supports  \xC2\xB7  none"));
			else snprintf (out, cap, TR ("Supports  \xC2\xB7  %d pillars%s"), (int) A.print.tips.size (), A.print.raft ? TR (" \xC2\xB7 a raft") : "");
			return;
		}
		if (A.camMode)					// the setup, then its operations
		{
			if (i == 0) { V3 s = A.paths.hi - A.paths.lo; fmt (s.x, a, 12); fmt (s.y, b, 12); fmt (s.z, c, 12); snprintf (out, cap, TR ("Setup  \xC2\xB7  %s \xC2\xB7 stock %s \xC3\x97 %s \xC3\x97 %s"), body_name (A.job.body), a, b, c); return; }
			const CamOp &o = A.job.ops[i - 1]; fmt (A.job.tool.dia, a, 12);
			if (o.failed) snprintf (out, cap, "%s  \xC2\xB7  %s", o.name, o.err);
			else snprintf (out, cap, TR ("%s  \xC2\xB7  \xC3\x98 %s flat \xC2\xB7 %d min%s"), o.name, a, (int) (o.minutes + 0.5), o.useFace ? TR (" \xC2\xB7 one face") : "");
			return;
		}
		if (!A.sketching)
		{
			const Feature &f = A.doc.feats[i];
			if (f.failed && i < A.doc.upto) snprintf (t, sizeof t, "%s", f.err); else describe (A.doc, f, t, sizeof t);
			snprintf (out, cap, "%s  \xC2\xB7  %s%s", f.name, t, i >= A.doc.upto ? TR ("  (rolled back)") : ""); return;
		}
		static const char *const EN[] = { TR ("Line"), TR ("Arc"), TR ("Circle"), TR ("Rectangle"), TR ("Close"), TR ("Spline"), TR ("Point") };
		const SkEl &e = A.sk.els[i]; int k = 0; for (int j = 0; j <= i; j++) if (A.sk.els[j].kind == e.kind) k++;
		if (e.kind == SK_LINE) { fmt (e.len, a); fmt (e.a, b); snprintf (t, sizeof t, e.rel ? TR ("%s \xC2\xB7 turn %s\xC2\xB0") : TR ("%s \xC2\xB7 at %s\xC2\xB0"), a, b); }
		else if (e.kind == SK_ARC) { fmt (e.r, a); fmt (e.sweep, b); snprintf (t, sizeof t, "R %s \xC2\xB7 %s\xC2\xB0", a, b); }
		else if (e.kind == SK_SPLINE) snprintf (t, sizeof t, TR ("through %d points"), (int) e.pts.size () + 1);
		else if (e.kind == SK_POINT) { fmt (e.x, a); fmt (e.y, b); snprintf (t, sizeof t, TR ("a mark at %s, %s"), a, b); }
		else if (e.kind == SK_CIRCLE) { fmt (e.w, a); fmt (e.x, b); fmt (e.y, c); snprintf (t, sizeof t, TR ("\xC3\x98 %s \xC2\xB7 at %s, %s"), a, b, c); }
		else if (e.kind == SK_RECT) { fmt (fabs (e.w), a); fmt (fabs (e.h), b); snprintf (t, sizeof t, e.rel ? TR ("%s \xC3\x97 %s \xC2\xB7 from its centre") : "%s \xC3\x97 %s", a, b); }
		else snprintf (t, sizeof t, TR ("back to the start"));
		snprintf (out, cap, "%s %d  \xC2\xB7  %s%s", EN[e.kind], k, t, e.cons ? TR (" \xC2\xB7 construction") : "");
	}
	// the cells' room: [x0, x1), the arrows shown when they do not all fit
	void room (int *x0, int *x1, bool *arrows) const
	{
		int textW = width > 620 ? 250 : 0; *x1 = width - 8 - textW; *x0 = 8;
		*arrows = count () * CELL + 10 > *x1 - *x0;
		if (*arrows) { *x0 += ARROW; *x1 -= ARROW; }
	}
	int cellAt (int mx) const
	{
		int x0, x1; bool ar; room (&x0, &x1, &ar);
		if (mx < x0 || mx >= x1) return -1;
		int i = (mx - x0 + scroll) / CELL; return i < count () ? i : -1;
	}
	void onDraw () override
	{
		canvas.clear (C_BG); uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		int x0, x1; bool arrows; room (&x0, &x1, &arrows);
		int n = count (), most = n * CELL + 10 - (x1 - x0); if (most < 0) most = 0;
		if (n > shownN) scroll = most;						// (a step added: the end shown)
		else if (sel () != shownSel && sel () >= 0)				// (one selected: brought into view)
		{
			if (sel () * CELL < scroll) scroll = sel () * CELL;
			if ((sel () + 1) * CELL + 10 > scroll + (x1 - x0)) scroll = (sel () + 1) * CELL + 10 - (x1 - x0);
		}
		shownSel = sel (); shownN = n;
		if (scroll > most) scroll = most; if (scroll < 0) scroll = 0;
		static const int EI[] = { I_LINE, I_ARC, I_CIRCLE, I_RECT, I_CLOSE, I_SPLINE, I_POINT };
		unsigned ink = uk_mix (C_FIELD_TEXT, C_FIELD, 40);
		for (int i = 0; i < n; i++)
		{
			int x = x0 + i * CELL - scroll;
			if (x < x0 || x + CELL > x1 + 6) continue;
			bool on = i == sel (), off = !A.camMode && !A.sketching && i >= A.doc.upto;
			bool bad = printer () ? false : A.camMode ? i > 0 && A.job.ops[i - 1].failed : !A.sketching && A.doc.feats[i].failed && !off;
			unsigned bg = C_FIELD;
			if (on) { bg = soft_col (C_FIELD); uk_rbox (canvas, x + 1, 5, CELL - 4, H - 10, 6, bg, bg); uk_rline (canvas, x + 1, 5, CELL - 4, H - 10, 6, C_ACCENT); }
			else if (i == hot) { bg = uk_tone (C_FIELD, 112); uk_rbox (canvas, x + 1, 5, CELL - 4, H - 10, 6, bg, bg); }
			int ic = printer () ? (i == 0 ? I_PR_PLATE : I_PR_SUP) : A.camMode ? (i == 0 ? I_CAM_SETUP : A.job.ops[i - 1].kind == CAM_CLEAR ? I_CAM_CLEAR : I_CAM_CONTOUR) : A.sketching ? EI[A.sk.els[i].kind] : icon_of_kind (A.doc.feats[i].kind);
			icon (canvas, ic, x + (CELL - 2 - 24) / 2, (H - 24) / 2, 24, off ? uk_mix (ink, bg, 170) : ink, off ? uk_mix (C_ACCENT, bg, 170) : C_ACCENT, bg);
			if (bad) icon (canvas, I_WARN, x + CELL - 19, H - 20, 14, 0x4A3A10, C_ACCENT, bg);
		}
		if (!A.sketching && !A.camMode && n)					// where the part is rebuilt up to
		{
			int mx = x0 + A.doc.upto * CELL - scroll - 2;
			if (mx >= x0 - 2 && mx <= x1)			// (a bar with a grip at its top: it can be dragged along the steps)
			{
				int g = barDrag || barHot ? 7 : 5; canvas.fillRect (mx - (barDrag ? 1 : 0), 6, barDrag ? 4 : 2, H - 12, C_ACCENT);
				VPath p; int xy[6] = { (mx + 1 - g) * 16, 2 * 16, (mx + 1 + g) * 16, 2 * 16, (mx + 1) * 16, (4 + g) * 16 }; p.poly (xy, 3); p.fill (canvas, C_ACCENT);
			}
		}
		if (arrows)
		{
			uk_glyph (canvas, WKG_CHEV_LEFT, 8 + ARROW / 2, H / 2, 10, scroll > 0 ? C_FIELD_TEXT : uk_mix (C_FIELD_TEXT, C_FIELD, 190));
			uk_glyph (canvas, WKG_CHEV_RIGHT, x1 + ARROW / 2, H / 2, 10, scroll < most ? C_FIELD_TEXT : uk_mix (C_FIELD_TEXT, C_FIELD, 190));
		}
		if (!n) { UkFaceScope fs (g_small); uk_text (canvas, 14, (H - uk_fh ()) / 2, A.sketching ? TR ("The sketch's elements come here, in the order they are drawn.") : TR ("The steps you make come here: click one to change its values."), uk_mix (C_FIELD_TEXT, C_FIELD, 140)); }
		// at the right: the step pointed or selected; a sketch: its outlines
		int tx = (arrows ? x1 + ARROW : x1) + 8, tw = width - 10 - tx; char t[160], fit[160];
		if (tw < 80) return;
		canvas.fillRect (tx - 6, 8, 1, H - 16, uk_tone (C_FIELD, 100));
		UkFaceScope fs (g_small); int show = hot >= 0 ? hot : sel ();
		if (show >= 0 && show < n)
		{
			text (show, t, sizeof t); char *dot = strstr (t, "  \xC2\xB7  ");
			if (dot) { *dot = 0; uk_text (canvas, tx, 7, t, C_FIELD_TEXT, 2); uk_text_fit (dot + 6, tw, fit, sizeof fit); uk_text (canvas, tx, 23, fit, uk_mix (C_FIELD_TEXT, C_FIELD, 110)); }
		}
		else if (A.sketching)
		{
			snprintf (t, sizeof t, TR ("%d closed outline%s"), A.ev.nclosed, A.ev.nclosed == 1 ? "" : "s");
			if (A.ev.nclosed) icon (canvas, I_CHECK, tx, 6, 14, C_GREEN, C_GREEN, C_FIELD);
			uk_text (canvas, tx + 18, 7, t, A.ev.nclosed ? 0x286E38 : uk_mix (C_FIELD_TEXT, C_FIELD, 110));
			if (A.ev.nopen) { snprintf (t, sizeof t, TR ("%d still open"), A.ev.nopen); icon (canvas, I_WARN, tx, 22, 14, 0x4A3A10, C_ACCENT, C_FIELD); }
			else snprintf (t, sizeof t, TR ("none open"));
			uk_text (canvas, tx + 18, 23, t, A.ev.nopen ? 0x8A5A10 : uk_mix (C_FIELD_TEXT, C_FIELD, 110));
		}
		else if (n) { snprintf (t, sizeof t, TR ("%d step%s"), n, n == 1 ? "" : "s"); uk_text (canvas, tx, 7, TR ("History"), C_FIELD_TEXT, 2); uk_text (canvas, tx, 23, t, uk_mix (C_FIELD_TEXT, C_FIELD, 110)); }
	}
	void pick (int i, bool dbl, bool right, int mx, int my)
	{
		if (printer ()) { print_page (i == 0 ? 0 : 2); return; }
		if (A.camMode)
		{
			A.camSel = i; A.camPage = i == 0 ? 0 : 2; A.camSim = false; cam_hint (); ui (R_ALL);
			if (right && i > 0)
			{
				Root *r = Root::current (); int ax = 0, ay = 0; for (Widget *w = this; w && w != r; w = w->parent) { ax += w->left; ay += w->top; }
				PopupMenu pm (ax + mx, ay + my - 40); pm.add (TR ("Delete"), CMD_CAM_DELETE);
				int c = pm.run (); if (c > 0) cmd (c);
			}
			return;
		}
		if (A.sketching) { A.selEl = A.selEl == i && !right ? -1 : i; sk_arm (); ui (R_ALL); return; }
		if (A.tool != T_SELECT) tool_set (T_SELECT);
		A.selFeat = i; A.selBody = -1; A.selCanvas = -1; ui (R_ALL);
		if (right)
		{
			Root *r = Root::current (); int ax = 0, ay = 0; for (Widget *w = this; w && w != r; w = w->parent) { ax += w->left; ay += w->top; }
			PopupMenu pm (ax + mx, ay + my - 110);
			if (A.doc.feats[i].kind == F_SKETCH) pm.add (TR ("Edit sketch"), CMD_EDIT_SKETCH);
			pm.add (TR ("Roll back to here"), CMD_ROLL_HERE, i + 1 != A.doc.upto); pm.add (TR ("Roll to the end"), CMD_ROLL_END, A.doc.upto < (int) A.doc.feats.size ());
			pm.separator (); pm.add (TR ("Delete"), CMD_DELETE);
			int c = pm.run (); if (c > 0) cmd (c);
		}
		else if (dbl && A.doc.feats[i].kind == F_SKETCH) cmd (CMD_EDIT_SKETCH);
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		bool press = bl && !wasL, release = !bl && wasL, rpress = br && !wasR; wasL = bl != 0; wasR = br != 0;
		int x0, x1; bool arrows; room (&x0, &x1, &arrows);
		// the bar dragged: the part is rebuilt up to the step it is left after (and back to the end the same way)
		bool design = !A.sketching && !A.camMode && count () > 0; int bar = x0 + A.doc.upto * CELL - scroll - 1;
		if (barDrag)
		{
			if (!bl || !design) { barDrag = false; invalidate (true); return true; }
			if (mx < x0 + 8 && scroll > 0) scroll -= CELL / 2; if (mx > x1 - 8) scroll += CELL / 2;	// (near an end: the row moves)
			int k = (mx - x0 + scroll + CELL / 2) / CELL; if (k < 0) k = 0; if (k > count ()) k = count ();
			if (k != A.doc.upto)
			{
				A.doc.upto = k; A.doc.touch (); if (A.selFeat >= k) A.selFeat = -1; if (!A.doc.body (A.selBody)) A.selBody = -1;
				set_hint (k < count () ? TR ("The part as it was after that step. Drag the bar back to the end for the rest.") : TR ("The whole history again."));
				ui (R_ALL); if (g_view) g_view->invalidate (true);
			}
			invalidate (true); return true;
		}
		if (!in) { if (hot >= 0 || barHot) { hot = -1; barHot = false; invalidate (true); } pressI = -1; return false; }
		bool onBar = design && abs (mx - bar) <= 5 && mx >= x0 - 4 && mx <= x1 + 4;
		if (onBar != barHot) { barHot = onBar; invalidate (true); }
		if (press && onBar) { if (A.tool != T_SELECT) tool_set (T_SELECT); barDrag = true; pressI = -1; invalidate (true); return true; }
		int h = cellAt (mx); if (h != hot) { hot = h; invalidate (true); }
		if (wheel) { scroll -= wheel * CELL * 2; invalidate (true); return true; }
		if (press)
		{
			if (arrows && mx < x0) { scroll -= CELL * 3; invalidate (true); return true; }
			if (arrows && mx >= x1 && mx < x1 + ARROW) { scroll += CELL * 3; invalidate (true); return true; }
			pressX = mx; pressScroll = scroll; pressI = h; moved = false;
		}
		if (bl && pressI >= -1 && abs (mx - pressX) > 5) { moved = true; scroll = pressScroll - (mx - pressX); invalidate (true); }	// dragged
		if (release && !moved && pressI >= 0 && pressI == h)
		{
			unsigned now = kapi_get_ticks (); bool dbl = h == lastI && now - lastT < 45; lastI = h; lastT = now;
			pick (h, dbl, false, mx, my);
		}
		if (rpress && h >= 0) pick (h, false, true, mx, my);
		return true;
	}
};

// ---- the right: the selection ---------------------------------------------------------------------------------------------
class Props : public Widget
{
public:
	enum { MAXB = 12, MAXL = 32 };
	struct Bind { Textbox *tb; double *val; int typed; bool mag; char *name; } b[MAXB]; int nb;
	struct Lab { int x, y; char s[72]; unsigned col; int style; bool small; } lab[MAXL]; int nl;
	int opY, swY, headIcon; char title[40], sub[2][24]; int subAt;
	Feature *feat; SkEl *el;		// what the fields edit
	int infoY; const char *info[4];
	Props (int l, int t, int w, int h) : Widget (l, t, w, h), nb (0), nl (0), opY (-1), swY (-1), headIcon (-1), subAt (-1), feat (0), el (0), infoY (-1)
	{ title[0] = 0; sub[0][0] = sub[1][0] = 0; }
	unsigned bgColor () override { return panel_col (); }

	static Props *self (Widget &w) { Widget *p = w.parent; return (Props *) p; }
	static double parse (const char *s) { char t[64]; int n = 0; for (; *s && n < 62; s++) t[n++] = *s == ',' ? '.' : *s; t[n] = 0; return atof (t); }
	// A field's text changed: its value follows.
	static void onChanged (Widget &w)
	{
		Props *p = self (w);
		for (int i = 0; i < p->nb; i++)
		{
			Bind &q = p->b[i];
			if (q.tb != &w) continue;
			if (q.name) { snprintf (q.name, 24, "%s", q.tb->text); A.doc.changes++; if (resin ()) print_presets_save (); else if (A.camMode && A.gen == 0) cam_presets_save (); ui (0); if (g_view) g_view->invalidate (true); return; }
			double v = parse (q.tb->text);
			if (q.mag) v = (*q.val < 0 ? -1 : 1) * fabs (v);
			*q.val = v; if (q.typed >= 0) A.typed[q.typed] = true;
			p->applied ();
		}
	}
	// A value changed (a field, the operation, a check box): what it belongs to is made again.
	void applied ()
	{
		if (canvasPage ()) { Canvas3 &c = A.doc.canvases[A.selCanvas]; if (c.w < 1) c.w = 1; if (c.opacity < 0) c.opacity = 0; if (c.opacity > 100) c.opacity = 100;
			A.doc.changes++; if (g_view) g_view->invalidate (true); ui (0); return; }
		if (!A.camMode && !A.sketching && A.hasPend && A.step == 0) A.ghost = true;		// (a shape made from its values: shown from now)
		if (printer ())				// (the body on the plate, what holds it, its layers: made again a little after)
		{
			PrintSetup &s = A.print;
			if (A.gen == 2 && A.camPage == 1)		// a filament printer's values, kept within what makes sense
			{
				FdmSettings &f = A.fdm;
				if (f.layer < 0.05) f.layer = 0.05; if (f.width < 0.1) f.width = 0.1; if (f.walls < 1) f.walls = 1; if (f.top < 0) f.top = 0; if (f.bottom < 0) f.bottom = 0;
				if (A.fdmPct < 0) A.fdmPct = 0; if (A.fdmPct > 100) A.fdmPct = 100; if (f.skirt < 0) f.skirt = 0;
				fdm_presets_save (); print_touch (); if (g_view) g_view->invalidate (true); ui (0); return;
			}
			if (A.gen == 2 && A.camPage == 0) fdm_presets_save ();
			if (A.camPage == 0)		// moved or lifted: its supports' tips go with it; tilted or turned: they are to make again
			{
				if (s.tiltX != A.pAt[3] || s.tiltY != A.pAt[4] || s.turn != A.pAt[5]) s.tips.clear ();
				else for (V3 &t : s.tips) t = t + V3 (s.x - A.pAt[0], s.y - A.pAt[1], s.lift - A.pAt[2]);
				A.pAt[0] = s.x; A.pAt[1] = s.y; A.pAt[2] = s.lift; A.pAt[3] = s.tiltX; A.pAt[4] = s.tiltY; A.pAt[5] = s.turn;
			}
			if (A.camPage == 1 && A.gen == 1)	// (one lift for the first layers and the others; the header's speeds are its second stage's)
			{
				s.resin.bottom = s.resin.normal; s.resin.liftSpeed = s.resin.normal.up2; s.resin.retractSpeed = s.resin.normal.down2;
				if (s.resin.layer < 0.01) s.resin.layer = 0.01;
				print_presets_save ();
			}
			print_touch (); if (g_view) g_view->invalidate (true);
			ui (0); return;
		}
		if (A.camMode)				// (the moves are made again a little after the last change: main.cpp)
		{
			cam_touch (); if (A.camPage == 1) cam_presets_save ();
			if (g_view) g_view->invalidate (true);
			ui (0); return;
		}
		if (A.sketching) { sk_eval (); }
		else if (A.hasPend) { auto_op (); preview_update (); if (A.prevErr) set_hint (A.prevErr); else tool_hint (); }
		else if (feat) { A.doc.touch (); }
		if (g_view) g_view->invalidate (true);
		ui (0);
	}
	static void onPattern (Widget &w) { A.fdm.pattern = ((Dropdown &) w).sel; fdm_presets_save (); print_touch (); if (g_view) g_view->invalidate (true); ui (0); }
	static void onEnter (Widget &) { if (filament ()) { print_refresh (); if (A.camPage == 3) fdm_start (); ui (0); return; } if (resin ()) { print_refresh (); if (A.camPage == 3) print_slice_start (); ui (0); return; } if (A.camMode) { cam_refresh (); ui (0); return; } cmd (A.tool == T_SKETCH && !A.sketching ? CMD_SK_START : CMD_OK); }
	static void onDrop (Widget &w) { A.job.xdir = ((Dropdown &) w).sel; A.doc.changes++; cam_hint (); ui (R_ALL); }
	int camOrgY = -1, camSumY = -1, camSizeY = -1;
	void seg2 (int y, const char *name, const char *a, const char *b, int sel, int tag, int sw = 150)
	{
		static const char *lab[8][2]; static int n = 0; const char **l = lab[n++ & 7]; l[0] = a; l[1] = b;
		if (name) label (12, y + 5, name);
		SegmentedControl *s = new SegmentedControl (name ? width - 12 - sw : 12, y, name ? sw : width - 24, 26, l, 2, sel, onSeg); s->tag = tag; addChild (s);
	}
	// Manufacture's pages: the setup, the tool and the machine, an operation.
	// A canvas is what the panel shows (nothing else is selected, no tool is at work).
	bool canvasPage () const { return !A.camMode && !A.sketching && !A.hasPend && A.tool == T_SELECT && A.selCanvas >= 0 && A.selCanvas < (int) A.doc.canvases.size () && A.selFeat < 0 && A.selBody < 0; }
	// The body cut or printed, and what makes it: a router (G-code), a resin printer (its layers).
	int bodyProcess (int y, int body)
	{
		int hw = (width - 24 - 8) / 2;
		label (12, y, TR ("Body"), UK_AUTO, 2); label (12 + hw + 8, y, TR ("Process"), UK_AUTO, 2); y += 20;
		ABtn *b = new ABtn (12, y, hw, 26, body_name (body), CMD_CAM_BODY); b->bg = panel_col (); addChild (b);
		ABtn *g = new ABtn (12 + hw + 8, y, hw, 26, A.gen == 2 ? TR ("Filament") : A.gen == 1 ? TR ("Resin print") : TR ("Milling"), CMD_GEN); g->bg = panel_col (); addChild (g);
		return y + 34;
	}
	int prSumY = -1, prInfoY = -1;
	// A filament printer's pages: the setup on the bed, the filament's values, the layers.
	void fdmPages (int y)
	{
		PrintSetup &s = A.print; FdmSettings &f = A.fdm;
		if (A.camPage == 0)
		{
			head (I_PR_PLATE, TR ("Setup"), TR ("the body, on the bed"));
			y = bodyProcess (y, s.body);
			label (12, y, TR ("Bed"), UK_AUTO, 2); y += 20;
			field (y, TR ("Width"), &A.fmach.bedX, TR ("mm"), -1); y += 27; field (y, TR ("Depth"), &A.fmach.bedY, TR ("mm"), -1); y += 27; field (y, TR ("Height"), &A.fmach.sizeZ, TR ("mm"), -1); y += 33;
			label (12, y, TR ("On the bed"), UK_AUTO, 2); y += 20;
			field (y, "X", &s.x, TR ("mm"), -1); y += 27; field (y, "Y", &s.y, TR ("mm"), -1); y += 27;
			field (y, TR ("Tilted, X"), &s.tiltX, "\xC2\xB0", -1); y += 27; field (y, TR ("Tilted, Y"), &s.tiltY, "\xC2\xB0", -1); y += 27; field (y, TR ("Turned"), &s.turn, "\xC2\xB0", -1); y += 34;
			prSumY = y;
		}
		else if (A.camPage == 1)
		{
			head (I_PR_FIL, TR ("Filament"), TR ("layers, shell, infill"));
			label (12, y, TR ("Layers"), UK_AUTO, 2); y += 20;
			field (y, TR ("Height"), &f.layer, TR ("mm"), -1); y += 27; field (y, TR ("Line's width"), &f.width, TR ("mm"), -1); y += 33;
			label (12, y, TR ("Shell"), UK_AUTO, 2); y += 20;
			field (y, TR ("Walls"), &f.walls, 0, -1); y += 27; field (y, TR ("Solid above"), &f.top, TR ("lay."), -1); y += 27; field (y, TR ("Solid below"), &f.bottom, TR ("lay."), -1); y += 33;
			label (12, y, TR ("Infill"), UK_AUTO, 2); y += 20;
			field (y, TR ("Share"), &A.fdmPct, "%", -1); y += 27; field (y, TR ("Turned"), &f.angle, "\xC2\xB0", -1); y += 29;
			static const char *const PT[3] = { TR ("Lines"), TR ("Grid"), TR ("Honeycomb") };
			label (12, y + 5, TR ("As")); int py = y; y += 37;
			label (12, y, TR ("Skirt"), UK_AUTO, 2); y += 20;
			field (y, TR ("Loops"), &f.skirt, 0, -1); y += 27; field (y, TR ("Away by"), &f.skirtGap, TR ("mm"), -1);
			Dropdown *d = new Dropdown (width - 12 - 122, py, 122, 26, PT, 3, f.pattern < 0 || f.pattern > 2 ? 0 : f.pattern, onPattern); addChild (d);	// (last: its list opens over the rest)
		}
		else
		{
			head (I_PR_LAYERS, TR ("Layers"), TR ("the nozzle's path"));
			prInfoY = y; y += 4 * 22 + 12;
			check (y, TR ("Show it in the 3D view"), A.layer3d, 33); y += 32;
			label (12, y, TR ("Play draws the layer as the"), dim_col (), 0, true); y += 15; label (12, y, TR ("printer would, then the next."), dim_col (), 0, true); y += 24;
			label (12, y, TR ("The file for a printer comes"), dim_col (), 0, true); y += 15; label (12, y, TR ("later: the path is what is shown."), dim_col (), 0, true);
		}
	}
	void printPages (int y)
	{
		PrintSetup &s = A.print; char t[96]; prSumY = prInfoY = -1;
		if (A.gen == 2) { fdmPages (y); return; }
		if (A.camPage == 0)
		{
			head (I_PR_PLATE, TR ("Setup"), TR ("the body, on the plate"));
			y = bodyProcess (y, s.body);
			label (12, y, TR ("Printer"), UK_AUTO, 2); y += 19; label (12, y, s.machine.name); y += 18;
			snprintf (t, sizeof t, "%d \xC3\x97 %d \xC2\xB7 %.3g \xC2\xB5m \xC2\xB7 .%s", s.machine.resX, s.machine.resY, s.machine.pixel, s.machine.ext); label (12, y, t, dim_col (), 0, true); y += 15;
			snprintf (t, sizeof t, TR ("room %.4g \xC3\x97 %.4g \xC3\x97 %.4g mm"), s.machine.resX * s.machine.pixel / 1000, s.machine.resY * s.machine.pixel / 1000, s.machine.sizeZ); label (12, y, t, dim_col (), 0, true); y += 24;
			label (12, y, TR ("On the plate"), UK_AUTO, 2); y += 20;
			field (y, "X", &s.x, TR ("mm"), -1); y += 27; field (y, "Y", &s.y, TR ("mm"), -1); y += 27; field (y, TR ("Lifted"), &s.lift, TR ("mm"), -1); y += 27;
			field (y, TR ("Tilted, X"), &s.tiltX, "\xC2\xB0", -1); y += 27; field (y, TR ("Tilted, Y"), &s.tiltY, "\xC2\xB0", -1); y += 27; field (y, TR ("Turned"), &s.turn, "\xC2\xB0", -1); y += 32;
			check (y, TR ("Mirror the picture"), s.mirror, 30); y += 28;
			prSumY = y;
		}
		else if (A.camPage == 1)
		{
			head (I_PR_RESIN, TR ("Resin"), TR ("exposures, the lift"));
			PrintResin &r = s.resin;
			ABtn *b = new ABtn (12, y, width - 24, 26, r.name, CMD_PR_PRESET); b->bg = panel_col (); addChild (b); y += 34;
			label (12, y, TR ("Layers"), UK_AUTO, 2); y += 20;
			field (y, TR ("Height"), &r.layer, TR ("mm"), -1); y += 27; field (y, TR ("Exposure"), &r.exposure, "s", -1); y += 27; field (y, TR ("Light off"), &r.off, "s", -1); y += 31;
			label (12, y, TR ("First layers"), UK_AUTO, 2); y += 20;
			field (y, TR ("How many"), &r.bottomLayers, 0, -1); y += 27; field (y, TR ("Exposure"), &r.bottomExposure, "s", -1); y += 27; field (y, TR ("Then, over"), &r.transition, TR ("lay."), -1); y += 31;
			label (12, y, TR ("Lift, after each layer"), UK_AUTO, 2); y += 20;
			field (y, TR ("Slowly"), &r.normal.h1, TR ("mm"), -1); y += 27; field (y, TR ("... at"), &r.normal.up1, "/s", -1); y += 27;
			field (y, TR ("Then"), &r.normal.h2, TR ("mm"), -1); y += 27; field (y, TR ("... at"), &r.normal.up2, "/s", -1); y += 27;
			field (y, TR ("Back down at"), &r.normal.down2, "/s", -1);
		}
		else if (A.camPage == 2)
		{
			head (I_PR_SUP, TR ("Supports"), TR ("what holds the body"));
			ABtn *g = new ABtn (12, y, width - 24, 28, TR ("Generate"), CMD_PR_GENERATE, true); g->bg = panel_col (); addChild (g); y += 38;
			label (12, y, TR ("Where"), UK_AUTO, 2); y += 20;
			field (y, TR ("Overhang"), &s.supAngle, "\xC2\xB0", -1); y += 27; field (y, TR ("A pillar every"), &s.supEvery, TR ("mm"), -1); y += 31;
			check (y, TR ("Also under low points"), s.supLow, 31); y += 30;
			label (12, y, TR ("Pillars"), UK_AUTO, 2); y += 20;
			field (y, TR ("Diameter"), &s.supDia, TR ("mm"), -1); y += 27; field (y, TR ("Tip"), &s.supTip, TR ("mm"), -1); y += 27; field (y, TR ("Into the body"), &s.supInto, TR ("mm"), -1); y += 33;
			check (y, TR ("A raft under it all"), s.raft, 32); y += 26;
			field (y, TR ("Thick"), &s.raftThick, TR ("mm"), -1); y += 27; field (y, TR ("Wider by"), &s.raftMore, TR ("mm"), -1); y += 33;
			prSumY = y;
			ABtn *d = new ABtn (12, height - 40, width - 24, 28, TR ("Remove them all"), CMD_PR_CLEAR); d->bg = panel_col (); d->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (d);
		}
		else
		{
			head (I_PR_LAYERS, TR ("Layers"), TR ("what the screen shows"));
			prInfoY = y; y += 4 * 22 + 8 + 20 + 3 * 21 + 12;
			check (y, TR ("Show it in the 3D view"), A.layer3d, 33); y += 30;
			ABtn *w = new ABtn (12, height - 40, width - 24, 28, TR ("Write the print file..."), CMD_PR_FILE, true); w->bg = panel_col (); w->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (w);
		}
	}
	// Print's own in the panel: whether it fits, the supports' sum, the layers' numbers and what was checked.
	void printDraw (unsigned pc)
	{
		const PrintSetup &s = A.print; char t[96], a[24], b[24], c[24]; unsigned ic = uk_mix (pc, 0xFFFFFF, 110);
		V3 sz = A.placedMesh.tris () ? A.placedMesh.hi - A.placedMesh.lo : V3 (0, 0, 0); double hw, hh, hz; plate_half (&hw, &hh, &hz);
		bool fits = A.placedMesh.tris () && A.placedMesh.lo.x >= -hw - 1e-6 && A.placedMesh.hi.x <= hw + 1e-6 && A.placedMesh.lo.y >= -hh - 1e-6 && A.placedMesh.hi.y <= hh + 1e-6 && A.placedMesh.hi.z <= hz + 1e-6;
		double lh = A.gen == 2 ? A.fdm.layer : s.resin.layer;
		if (prInfoY >= 0 && A.gen == 2)			// a filament printer's layers: what the path comes to
		{
			const FdmJob &j = A.fjob; int y = prInfoY, n = (int) j.layers.size ();
			auto row = [&] (const char *k, const char *v) { uk_text (canvas, 12, y, k, dim_col ()); uk_text (canvas, width - 12 - uk_tw (v), y, v, C_TEXT); y += 22; };
			if (!A.fdmReady) { row (TR ("Layers"), j.err[0] ? "\xE2\x80\x94" : "..."); return; }
			snprintf (t, sizeof t, "%d", n); row (TR ("Layers"), t); fmt (n * A.fdm.layer, a, 12); snprintf (t, sizeof t, TR ("%s mm"), a); row (TR ("Height"), t);
			snprintf (t, sizeof t, "%.1f m", j.length / 1000); row (TR ("Line drawn"), t); snprintf (t, sizeof t, TR ("%.2f m \xC2\xB7 %.1f cm\xC2\xB3"), j.filament / 1000, j.volume / 1000); row (TR ("Filament"), t);
			return;
		}
		if (prSumY >= 0)
		{
			uk_rbox (canvas, 12, prSumY, width - 24, 42, 6, ic, ic); uk_rline (canvas, 12, prSumY, width - 24, 42, 6, uk_tone (C_BG, 104));
			UkFaceScope fs (g_small);
			if (A.camPage == 0)
			{
				icon (canvas, fits ? I_CHECK : I_WARN, 20, prSumY + 12, 17, fits ? C_GREEN : 0x4A3A10, C_ACCENT, ic);
				fmt (sz.x, a, 12); fmt (sz.y, b, 12); fmt (sz.z, c, 12); snprintf (t, sizeof t, "%s \xC3\x97 %s \xC3\x97 %s: %s", a, b, c, fits ? TR ("it fits") : TR ("too large"));
				uk_text (canvas, 44, prSumY + 6, t, fits ? 0x286E38 : 0x8A5A10);
				snprintf (t, sizeof t, TR ("%d layers of %.3g mm"), (int) ceil ((A.placedMesh.tris () ? A.placedMesh.hi.z : 0) / lh - 1e-6), lh); uk_text (canvas, 44, prSumY + 22, t, dim_col ());
			}
			else
			{
				bool floats = A.placedMesh.tris () && A.placedMesh.lo.z > s.resin.layer && s.tips.empty ();
				icon (canvas, floats ? I_WARN : I_CHECK, 20, prSumY + 12, 17, floats ? 0x4A3A10 : C_GREEN, C_ACCENT, ic);
				if (s.tips.empty ()) snprintf (t, sizeof t, floats ? TR ("Nothing holds the body") : TR ("No supports")); else snprintf (t, sizeof t, TR ("%d pillars%s"), (int) s.tips.size (), s.raft ? TR (" \xC2\xB7 a raft") : "");
				uk_text (canvas, 44, prSumY + 6, t, floats ? 0x8A5A10 : 0x286E38);
				uk_text (canvas, 44, prSumY + 22, floats ? TR ("it is lifted off the plate") : A.placedMesh.tris () && A.placedMesh.lo.z < 0.3 ? TR ("the body lies on the plate") : TR ("click the body to add one"), dim_col ());
			}
		}
		if (prInfoY >= 0)
		{
			const PrintJob &j = A.pjob; int y = prInfoY, n = (int) j.file.layers.size (); bool ok = A.sliced;
			auto row = [&] (const char *k, const char *v) { uk_text (canvas, 12, y, k, dim_col ()); uk_text (canvas, width - 12 - uk_tw (v), y, v, C_TEXT); y += 22; };
			if (ok) snprintf (t, sizeof t, "%d", n); else snprintf (t, sizeof t, TR ("%d of %d..."), j.done, j.total); row (TR ("Layers"), t);
			fmt (n * s.resin.layer, a, 12); snprintf (t, sizeof t, TR ("%s mm"), a); row (TR ("Height"), ok ? t : "\xE2\x80\x94");
			snprintf (t, sizeof t, TR ("%.1f ml"), j.volume); row (TR ("Resin"), ok ? t : "\xE2\x80\x94");
			int mn = (int) (j.minutes + 0.5); if (mn >= 60) snprintf (t, sizeof t, "%d h %02d", mn / 60, mn % 60); else snprintf (t, sizeof t, TR ("%d min"), mn); row (TR ("Time"), ok ? t : "\xE2\x80\x94");
			y += 8; uk_text (canvas, 12, y, TR ("Checked"), C_TEXT, 2); y += 20;
			UkFaceScope fs (g_small);
			auto chk = [&] (bool good, const char *txt) { icon (canvas, good ? I_CHECK : I_WARN, 12, y, 15, good ? C_GREEN : 0x4A3A10, C_ACCENT, pc); uk_text (canvas, 34, y + 1, txt, good ? C_TEXT : 0x8A5A10); y += 21; };
			if (!ok) { uk_text (canvas, 12, y, j.err[0] ? j.err : TR ("cutting the layers..."), dim_col ()); return; }
			chk (!j.tooLarge, j.tooLarge ? TR ("It does not fit the printer") : TR ("It fits the plate and the room"));
			chk (j.onPlate, j.onPlate ? TR ("The first layers lie on the plate") : TR ("Nothing lies on the plate"));
			if (j.islands) { fmt (j.islandAt, a, 12); snprintf (t, sizeof t, j.islands > 1 ? TR ("%d parts start in mid-air (at %s mm)") : TR ("%d part starts in mid-air (at %s mm)"), j.islands, a); chk (false, t); }
			else chk (true, TR ("Each layer rests on the last"));
		}
	}
	void camPages (int y)
	{
		CamSetup &c = A.job; CamOp *op = cam_op (); char t[96];
		camOrgY = camSumY = camSizeY = -1; prSumY = prInfoY = -1;
		if (A.gen >= 1) { printPages (y); return; }
		if (A.camPage == 0)
		{
			head (I_CAM_SETUP, TR ("Setup"), TR ("the body, its stock"));
			y = bodyProcess (y, c.body);
			label (12, y, TR ("Stock"), UK_AUTO, 2); y += 20; seg2 (y, 0, TR ("Around it"), TR ("Fixed size"), c.fixed ? 1 : 0, 10); y += 32;
			if (!c.fixed) { field (y, TR ("Sides, more"), &c.side, TR ("mm"), -1); y += 28; field (y, TR ("Top, more"), &c.top, TR ("mm"), -1); y += 28; field (y, TR ("Under, more"), &c.under, TR ("mm"), -1); y += 28; }
			else
			{
				// (its size, then the body moved in it: from the middle, its underside on the stock's -- 0: there)
				field (y, TR ("Along X"), &c.size.x, TR ("mm"), -1); y += 26; field (y, TR ("Along Y"), &c.size.y, TR ("mm"), -1); y += 26; field (y, TR ("Height"), &c.size.z, TR ("mm"), -1); y += 26;
				field (y, TR ("Moved, X"), &c.off.x, TR ("mm"), -1); y += 26; field (y, TR ("Moved, Y"), &c.off.y, TR ("mm"), -1); y += 26; field (y, TR ("Moved, Z"), &c.off.z, TR ("mm"), -1); y += 28;
			}
			if (!c.fixed) { camSizeY = y; y += 22; }
			label (12, y, TR ("Origin"), UK_AUTO, 2); y += 19; camOrgY = y; y += 64;
			if (!c.fixed) { label (12, y, TR ("Axes"), UK_AUTO, 2); y += 20; }
			static const char *const DIR[4] = { TR ("right"), TR ("back"), TR ("left"), TR ("front") };
			label (12, y + 5, TR ("X goes")); Dropdown *d = new Dropdown (width - 12 - 96, y, 96, 26, DIR, 4, c.xdir & 3, onDrop);
			snprintf (t, sizeof t, TR ("Y goes %s; Z goes up."), DIR[(c.xdir + 1) & 3]);
			if (!c.fixed) { label (12, y + 31, t, dim_col (), 0, true); y += 52; } else y += 34;
			if (!c.fixed) { label (12, y, TR ("Heights above the stock"), UK_AUTO, 2); y += 20; }
			field (y, c.fixed ? TR ("Safe, above") : TR ("Safe"), &c.safe, TR ("mm"), -1); y += 28; field (y, c.fixed ? TR ("Retract, above") : TR ("Retract"), &c.retract, TR ("mm"), -1);
			addChild (d);					// (last: its list opens over what is under it)
		}
		else if (A.camPage == 1)
		{
			head (I_CAM_TOOL, TR ("Tool"), TR ("a flat end mill"));
			label (12, y, TR ("Tool"), UK_AUTO, 2); y += 20;
			label (12, y + 5, TR ("Name")); Textbox *n = new Textbox (width - 12 - 146, y, 146, 26, c.tool.name); n->maxLen = 22; n->changed = onChanged; addChild (n);
			{ Bind q = { n, 0, -1, false, c.tool.name }; b[nb++] = q; } y += 29;
			field (y, TR ("Diameter"), &c.tool.dia, TR ("mm"), -1); y += 28; field (y, TR ("Cutting length"), &c.tool.flute, TR ("mm"), -1); y += 28;
			field (y, TR ("Spindle"), &c.tool.rpm, TR ("rpm"), -1); y += 28; field (y, TR ("Cutting feed"), &c.tool.feed, TR ("/min"), -1); y += 28;
			field (y, TR ("Plunge feed"), &c.tool.plunge, TR ("/min"), -1); y += 28; field (y, TR ("Travel"), &c.tool.travel, TR ("/min"), -1); y += 36;
			label (12, y, TR ("Machine"), UK_AUTO, 2); y += 20;
			label (12, y + 5, TR ("Name")); Textbox *m = new Textbox (width - 12 - 146, y, 146, 26, c.machine.name); m->maxLen = 22; m->changed = onChanged; addChild (m);
			{ Bind q = { m, 0, -1, false, c.machine.name }; b[nb++] = q; } y += 29;
			field (y, TR ("Travel X"), &c.machine.tx, TR ("mm"), -1); y += 28; field (y, TR ("Travel Y"), &c.machine.ty, TR ("mm"), -1); y += 28; field (y, TR ("Travel Z"), &c.machine.tz, TR ("mm"), -1); y += 28;
			field (y, TR ("Spindle, most"), &c.machine.rpmMax, TR ("rpm"), -1); y += 28; field (y, TR ("Spindle's wait"), &c.machine.dwell, "s", -1);
		}
		else if (op)
		{
			bool clear = op->kind == CAM_CLEAR;
			head (clear ? I_CAM_CLEAR : I_CAM_CONTOUR, op->name, clear ? TR ("roughing, by levels") : TR ("the outline, to depth"));
			seg2 (y, TR ("On"), TR ("The body"), TR ("A face"), op->useFace ? 1 : 0, 11); y += 30;
			if (op->useFace)
			{
				const Body *b = A.doc.body (c.body); int g = b ? cam_face (b->mesh, op->facePt) : -1; char a[24]; fmt (op->facePt.z, a, 12);
				if (g >= 0) snprintf (t, sizeof t, TR ("the face at height %s"), a); else snprintf (t, sizeof t, TR ("click a flat face turned up"));
				label (12, y, t, g >= 0 ? dim_col () : 0xB03A30, 0, true); y += 20;
			}
			else y += 4;
			if (clear)
			{
				field (y, TR ("Step over"), &op->stepover, TR ("mm"), -1); y += 28; field (y, TR ("Step down"), &op->stepdown, TR ("mm"), -1); y += 28;
				field (y, TR ("Left on walls"), &op->leaveR, TR ("mm"), -1); y += 28; field (y, TR ("Left on floors"), &op->leaveZ, TR ("mm"), -1); y += 32;
				seg2 (y, TR ("Cut"), TR ("Climb"), TR ("Convent."), op->climb ? 0 : 1, 12); y += 34;
				if (!op->useFace)
				{
					check (y, TR ("To the lowest flat face"), op->lowestAuto, 20); y += 26;
					if (!op->lowestAuto) { field (y, TR ("Lowest (model Z)"), &op->lowest, TR ("mm"), -1); y += 30; }
				}
			}
			else
			{
				seg2 (y, TR ("Side"), TR ("Outside"), TR ("Inside"), op->inside ? 1 : 0, 13); y += 32; seg2 (y, TR ("Cut"), TR ("Climb"), TR ("Convent."), op->climb ? 0 : 1, 12); y += 34;
				field (y, TR ("Step down"), &op->stepdown, TR ("mm"), -1); y += 30;
				check (y, TR ("From the stock's top"), op->fromAuto, 21); y += 26;
				if (!op->fromAuto) { field (y, TR ("From (model Z)"), &op->from, TR ("mm"), -1); y += 30; }
				if (!op->useFace)
				{
					field (y, TR ("Under it, by"), &op->under, TR ("mm"), -1); y += 32;
					check (y, TR ("Tabs"), op->tabs, 22); y += 26;
					if (op->tabs) { field (y, TR ("How many"), &op->ntabs, 0, -1); y += 28; field (y, TR ("Width"), &op->tabw, TR ("mm"), -1); y += 28; field (y, TR ("Height"), &op->tabh, TR ("mm"), -1); y += 30; }
				}
			}
			camSumY = y + 4;
			ABtn *d = new ABtn (12, height - 40, width - 24, 28, TR ("Delete this operation"), CMD_CAM_DELETE); d->bg = panel_col (); d->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (d);
		}
	}
	static void onCheck (Widget &w)
	{
		Props *p = self (w); Checkbox &c = (Checkbox &) w; Feature *f = A.hasPend ? &A.pend : p->feat;
		switch (w.tag)
		{
		case 1: if (f) { f->centred = c.checked; } break;
		case 2: if (f) { f->through = c.checked; if (f->through && f->h > 0) f->h = -f->h; } break;
		case 3: A.snap = c.checked; ui (R_PANELS); return;
		case 4: A.showEdges = c.checked; break;
		case 5: A.showGrid = c.checked; break;
		case 6: A.seeThrough = c.checked; break;
		case 7: if (p->el) { p->el->rel = c.checked; } break;
		case 8: if (f) { f->clone = c.checked; } break;
		case 19: if (p->el) { p->el->cons = c.checked; if (p->el == &A.cur) A.cons = c.checked; } break;
		case 17: if (f) { f->r2 = c.checked ? (f->r2 > 0 ? f->r2 : f->r) : 0; ui (R_PANELS); } break;	// (asymmetric: the second size starts as the first)
		case 18: if (f) { f->swap = c.checked; } break;
		case 34: if (p->canvasPage ()) { A.doc.canvases[A.selCanvas].visible = c.checked; A.doc.changes++; if (g_view) g_view->invalidate (true); ui (0); } return;
		case 30: A.print.mirror = c.checked; break;
		case 31: A.print.supLow = c.checked; break;
		case 32: A.print.raft = c.checked; break;
		case 33: A.layer3d = c.checked; A.layerPlay = false; if (g_view) g_view->invalidate (true); ui (0); return;
		case 20: if (cam_op ()) cam_op ()->lowestAuto = c.checked; break;
		case 21: if (cam_op ()) cam_op ()->fromAuto = c.checked; break;
		case 22: if (cam_op ()) cam_op ()->tabs = c.checked; break;
		}
		if (w.tag >= 4 && w.tag <= 6) { if (g_view) g_view->invalidate (true); ui (0); return; }
		if (w.tag >= 30) { print_touch (); if (g_view) g_view->invalidate (true); ui (0); return; }
		if (w.tag >= 20) { cam_touch (); ui (R_PANELS); return; }
		if (!A.hasPend && !A.sketching && f) undo_push ();
		p->applied (); ui (R_PANELS);
	}
	static void onSeg (Widget &w)
	{
		SegmentedControl &s = (SegmentedControl &) w; Props *p = self (w);
		if (w.tag == 1) { tool_set (s.selected == 0 ? T_FILLET : T_CHAMFER); return; }
		if (w.tag == 2 && p->el) { p->el->sweep = (s.selected == 0 ? 1 : -1) * fabs (p->el->sweep); p->applied (); }
		if (w.tag == 14 && p->el)			// a rectangle from a corner, or from its centre (kept where it is)
		{
			SkEl &e = *p->el; bool c = s.selected == 1;
			if (c != e.rel && A.curStep > 0) { double x0 = e.rel ? e.x - fabs (e.w) / 2 : e.w < 0 ? e.x + e.w : e.x, y0 = e.rel ? e.y - fabs (e.h) / 2 : e.h < 0 ? e.y + e.h : e.y;
				e.w = fabs (e.w); e.h = fabs (e.h); e.x = c ? x0 + e.w / 2 : x0; e.y = c ? y0 + e.h / 2 : y0; }
			if (c != e.rel && A.selEl >= 0) { double x0 = e.rel ? e.x - fabs (e.w) / 2 : e.w < 0 ? e.x + e.w : e.x, y0 = e.rel ? e.y - fabs (e.h) / 2 : e.h < 0 ? e.y + e.h : e.y;
				e.w = fabs (e.w); e.h = fabs (e.h); e.x = c ? x0 + e.w / 2 : x0; e.y = c ? y0 + e.h / 2 : y0; }
			e.rel = c; A.rectCentre = c; p->applied (); tool_hint (); ui (R_PANELS);
		}
		if (w.tag == 3) { A.skPlane = s.selected; ui (R_ALL); }
		if (w.tag == 16 && p->canvasPage ()) { A.doc.canvases[A.selCanvas].plane = s.selected; A.doc.changes++; if (g_view) g_view->invalidate (true); ui (R_ALL); }
		if (w.tag == 10) { A.job.fixed = s.selected == 1; if (A.job.fixed) A.job.size = A.paths.hi - A.paths.lo; cam_touch (); ui (R_ALL); }
		if (w.tag == 11 && cam_op ()) { cam_op ()->useFace = s.selected == 1; cam_touch (); if (s.selected == 1) set_hint (TR ("Click the flat face, turned up, to work on.")); ui (R_ALL); }
		if (w.tag == 12 && cam_op ()) { cam_op ()->climb = s.selected == 0; cam_touch (); }
		if (w.tag == 13 && cam_op ()) { cam_op ()->inside = s.selected == 1; cam_touch (); }
	}

	void label (int x, int y, const char *s, unsigned col = UK_AUTO, int style = 0, bool small = false)
	{ if (nl < MAXL) { Lab &l = lab[nl++]; l.x = x; l.y = y; l.col = col; l.style = style; l.small = small; snprintf (l.s, sizeof l.s, "%s", s); } }
	Textbox *field (int y, const char *name, double *val, const char *unit, int typed, bool mag = false, int fw = 96)
	{
		char t[32]; fmt (mag ? fabs (*val) : *val, t);
		label (12, y + (26 - uk_fh ()) / 2, name);
		Textbox *tb = new Textbox (width - 12 - fw, y, fw - (unit ? 30 : 0), 26, t, onEnter); tb->changed = onChanged; addChild (tb);
		if (unit) label (width - 12 - 26, y + (26 - uk_fh ()) / 2 + 1, unit, dim_col (), 0, true);
		if (nb < MAXB) { Bind q = { tb, val, typed, mag, 0 }; b[nb++] = q; }
		return tb;
	}
	void check (int y, const char *s, bool on, int tag) { Checkbox *c = new Checkbox (12, y, width - 24, 20, s, on, onCheck, panel_col ()); c->tag = tag; addChild (c); }
	void okCancel (const char *ok = TR ("OK"))
	{
		ABtn *c = new ABtn (12, height - 40, 90, 28, TR ("Cancel"), CMD_CANCEL); c->bg = panel_col (); c->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (c);
		ABtn *o = new ABtn (width - 102, height - 40, 90, 28, ok, CMD_OK, true); o->bg = panel_col (); o->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (o);
	}
	void head (int ic, const char *t, const char *s0 = 0, const char *s1 = 0, int at = -1)
	{ headIcon = ic; snprintf (title, sizeof title, "%s", t); snprintf (sub[0], 24, "%s", s0 ? s0 : ""); snprintf (sub[1], 24, "%s", s1 ? s1 : ""); subAt = at; }

	// The fields of a step (the one being made, or one of the history).
	int featureFields (Feature &f, int y, bool making)
	{
		switch (f.kind)
		{
		case F_BOX:
		{
			Textbox *w = field (y, TR ("Width"), &f.w, TR ("mm"), 0, true); y += 32; field (y, TR ("Depth"), &f.d, TR ("mm"), 1, true); y += 32;
			Textbox *h = field (y, TR ("Height"), &f.h, TR ("mm"), 2, true); y += 44;
			if (making) (A.step == 2 ? h : w)->setFocus ();
			y -= 12; break;
		}
		case F_CYL:
		{
			Textbox *d = field (y, TR ("Diameter"), &f.w, TR ("mm"), 0); y += 32; Textbox *h = field (y, f.h < 0 || f.through ? TR ("Depth") : TR ("Height"), &f.h, TR ("mm"), 1, true); y += 34;
			if (making) (A.step == 2 ? h : d)->setFocus ();
			check (y, TR ("Through the whole body"), f.through, 2); y += 26; break;
		}
		case F_PYRAMID: case F_PRISM: case F_TAPER:
		{
			Textbox *n = field (y, TR ("Sides"), &f.n, 0, -1); y += 32;
			Textbox *d = field (y, TR ("Base radius"), &f.w, TR ("mm"), 0); y += 32; Textbox *top = 0;
			if (f.kind == F_TAPER) { top = field (y, TR ("Top radius"), &f.d, TR ("mm"), 2); y += 32; }
			Textbox *h = field (y, TR ("Height"), &f.h, TR ("mm"), 1, true); y += 32;
			// (the field of what is being set has the keyboard: the sides first, Tab for the radius)
			if (making) { Textbox *t = A.step <= 1 ? n : A.step == 2 && top ? top : h; t->setFocus (); t->caret = (int) strlen (t->text); (void) d; }
			break;
		}
		case F_SPHERE: { Textbox *r = field (y, TR ("Radius"), &f.w, TR ("mm"), 0); y += 32; if (making) r->setFocus (); break; }
		case F_TORUS:
		{
			Textbox *r = field (y, TR ("Ring radius"), &f.w, TR ("mm"), 0); y += 32; Textbox *t = field (y, TR ("Tube radius"), &f.d, TR ("mm"), 1); y += 32;
			if (making) (A.step == 2 ? t : r)->setFocus ();
			break;
		}
		case F_EXTRUDE:
		{
			Textbox *h = field (y, f.h < 0 || f.through ? TR ("Depth") : TR ("Height"), &f.h, TR ("mm"), 1, true); y += 34; if (making) h->setFocus ();
			check (y, TR ("Through the whole body"), f.through, 2); y += 34; break;
		}
		case F_FILLET: case F_CHAMFER:
		{
			static const char *const FC[] = { TR ("Fillet"), TR ("Chamfer") };
			if (making) { SegmentedControl *s = new SegmentedControl (12, y, width - 24, 28, FC, 2, f.kind == F_FILLET ? 0 : 1, onSeg); s->tag = 1; addChild (s); y += 40; }
			Textbox *r = field (y, f.kind == F_FILLET ? TR ("Radius") : TR ("Size"), &f.r, TR ("mm"), 0); y += 32; if (making) r->setFocus ();
			// (asymmetric: a second size, on the second face -- the first is on the upper face, a hole's rim's flat face)
			check (y, TR ("Two sizes"), f.r2 > 0, 17); y += 26;
			if (f.r2 > 0)
			{
				field (y, f.kind == F_FILLET ? TR ("Radius 2") : TR ("Size 2"), &f.r2, TR ("mm"), -1); y += 30;
				check (y, TR ("Swap the faces"), f.swap, 18); y += 26;
			}
			y += 10;
			char t[48]; label (12, y, TR ("Edges"), UK_AUTO, 2); snprintf (t, sizeof t, TR ("%d chosen"), (int) f.edges.size ()); label (width - 12 - 70, y + 2, t, dim_col (), 0, true); y += 24;
			if (f.edges.empty ()) label (12, y, TR ("Click an edge of a body"), dim_col (), 0, true);
			else { Body *b = A.doc.body (f.target); snprintf (t, sizeof t, TR ("on %s"), body_name (f.target)); label (12, y, t, dim_col (), 0, true); (void) b; }
			y += 30;
			if (making) { infoY = y; info[0] = TR ("Straight edges, circles and"); info[1] = TR ("arcs can be rounded; the"); info[2] = TR ("others show in grey."); info[3] = 0; y += 70; }
			break;
		}
		case F_MOVE:
		{
			y -= 4; label (12, y, TR ("Move"), UK_AUTO, 2); y += 20;
			Textbox *x = field (y, TR ("Along X"), &f.mv.x, TR ("mm"), 0); y += 29; field (y, TR ("Along Y"), &f.mv.y, TR ("mm"), 1); y += 29; field (y, TR ("Along Z"), &f.mv.z, TR ("mm"), 2); y += 34;
			label (12, y, TR ("Turn, about its centre"), UK_AUTO, 2); y += 20;
			field (y, TR ("Around X"), &f.rot.x, "\xC2\xB0", -1); y += 29; field (y, TR ("Around Y"), &f.rot.y, "\xC2\xB0", -1); y += 29; field (y, TR ("Around Z"), &f.rot.z, "\xC2\xB0", -1); y += 34;
			label (12, y, TR ("Scale (1: as it is)"), UK_AUTO, 2); y += 20;
			field (y, TR ("Along X"), &f.sc.x, "\xC3\x97", -1); y += 29; field (y, TR ("Along Y"), &f.sc.y, "\xC3\x97", -1); y += 29; field (y, TR ("Along Z"), &f.sc.z, "\xC3\x97", -1); y += 32;
			check (y, TR ("Clone: the original stays"), f.clone, 8); y += 28;
			if (making && A.step == 1) x->setFocus ();
			break;
		}
		case F_COMBINE: { char t[64]; snprintf (t, sizeof t, TR ("%s, with another body"), TR (OP_NAME[f.op])); label (12, y, t, dim_col ()); y += 30; break; }
		case F_SKETCH: { char t[64]; describe (A.doc, f, t, sizeof t); label (12, y, t, dim_col ()); y += 30; break; }
		}
		if (makes_solid (f.kind) && f.kind != F_EXTRUDE)		// where it is, on its plane
		{
			y += 4; field (y, f.kind == F_BOX && !f.centred ? TR ("Corner X") : TR ("Centre X"), &f.x, TR ("mm"), -1); y += 28; field (y, "Y", &f.y, TR ("mm"), -1); y += 28;
			// (off its plane: up from the ground, out of the face it was put on -- less than 0: sunk into it)
			field (y, f.target >= 0 ? TR ("Off the face") : f.pl.n.z > 0.9999 && fabs (f.pl.o.z) < 1e-9 ? "Z" : TR ("Off the plane"), &f.z, TR ("mm"), -1); y += 28;
			if (f.kind == F_BOX || ((f.kind == F_PYRAMID || f.kind == F_PRISM || f.kind == F_TAPER) && f.n >= 2.5)) { field (y, TR ("Turned"), &f.turn, "\xC2\xB0", 3); y += 28; }
			y += 6;
		}
		if (makes_solid (f.kind))
		{
			opY = y; y += 20 + 4 * 28 + 6 + 12;
			if (f.op != OP_NEW && A.doc.body (f.target))
			{
				label (12, y + 2, f.op == OP_UNION ? TR ("Joined to") : f.op == OP_SUB ? TR ("Cut from") : TR ("Kept inside"), dim_col ());
				label (width - 12 - 92, y + 2, body_name (f.target)); swY = -(y + 1); y += 30;
			}
			if (f.kind == F_BOX) { check (y, TR ("Centred on the first click"), f.centred, 1); y += 28; }
		}
		return y;
	}
	void rebuild ()
	{
		while (firstChild) { Widget *c = firstChild; removeChild (c); delete c; }
		nb = nl = 0; opY = swY = infoY = -1; headIcon = -1; title[0] = 0; sub[0][0] = sub[1][0] = 0; subAt = -1; feat = 0; el = 0;
		int y = 64; char t[96];
		camOrgY = camSumY = camSizeY = -1;
		if (A.camMode) camPages (y);
		else if (A.sketching)
		{
			static const char *const EN[] = { TR ("Line"), TR ("Arc"), TR ("Circle"), TR ("Rectangle"), TR ("Close"), TR ("Spline"), TR ("Point") };
			static const int EI[] = { I_LINE, I_ARC, I_CIRCLE, I_RECT, I_CLOSE, I_SPLINE, I_POINT };
			bool edit = A.selEl >= 0 && A.selEl < (int) A.sk.els.size ();
			SkEl &e = edit ? A.sk.els[A.selEl] : A.cur; el = &e;
			if (e.kind == SK_ARC && !edit && A.tool == T_ARC3) head (I_ARC3, TR ("3-point arc"), TR ("Ends"), TR ("Middle"), A.curStep < 2 ? 0 : 1);
			else if (e.kind == SK_ARC && !edit && e.chain) head (EI[e.kind], TR ("Arc"), TR ("Centre"), TR ("End"), A.curStep < 2 ? 0 : 1);
			else if (e.kind == SK_ARC && !edit) head (EI[e.kind], TR ("Arc"), TR ("Centre"), TR ("Start \xC2\xB7 End"), A.curStep == 0 ? 0 : 1);
			else if (A.tool == T_MIDLINE && !edit) head (I_MIDLINE, TR ("Midpoint line"));
			else head (EI[e.kind], EN[e.kind], edit ? TR ("selected") : 0);
			bool chained = e.chain;
			const char *xn = e.kind == SK_CIRCLE || (e.kind == SK_RECT && e.rel) || (e.kind == SK_ARC && !edit && A.tool == T_ARC && A.curStep == 0) ? TR ("Centre X")
					 : e.kind == SK_RECT ? TR ("Corner X") : e.kind == SK_POINT ? TR ("At X") : TR ("Starts at X");
			bool mid = A.tool == T_MIDLINE && !edit;		// (a line drawn from its middle: placed, its middle is what moves)
			if (mid) xn = TR ("Middle X");
			double *fx = mid && A.curStep == 1 ? &A.arcC.x : &e.x, *fy = mid && A.curStep == 1 ? &A.arcC.y : &e.y;
			if (e.kind != SK_CLOSE && !chained) { field (y, xn, fx, TR ("mm"), -1, false, 96); y += 32; field (y, "Y", fy, TR ("mm"), -1, false, 96); y += 32; }
			else if (e.kind != SK_CLOSE) { label (12, y + 5, TR ("Starts at")); label (width - 12 - 118, y + 5, TR ("the end before"), 0x2862B0); y += 32; }
			Textbox *first = 0;
			if (e.kind == SK_LINE) { first = field (y, TR ("Length"), &e.len, TR ("mm"), 0); y += 32; field (y, TR ("Angle"), &e.a, "\xC2\xB0", 1); y += 34; if (chained) { check (y, TR ("From the line before"), e.rel, 7); y += 28; } }
			else if (e.kind == SK_ARC)
			{
				first = field (y, TR ("Radius"), &e.r, TR ("mm"), 0); y += 32; field (y, TR ("Centre at"), &e.ca, "\xC2\xB0", -1); y += 32; Textbox *sw = field (y, TR ("Angle"), &e.sweep, "\xC2\xB0", 1); y += 30;
				if (!edit && A.curStep == 2 && A.tool != T_ARC3) first = sw;
				label (12, y, TR ("+ clockwise, \xE2\x88\x92 anticlockwise"), dim_col (), 0, true); y += 26;
			}
			else if (e.kind == SK_SPLINE)
			{
				snprintf (t, sizeof t, TR ("through %d points"), (int) e.pts.size () + (edit ? 1 : 0)); label (12, y + 2, t); y += 24;
				if (!edit) { label (12, y, TR ("Enter, or the last point clicked"), dim_col (), 0, true); y += 15; label (12, y, TR ("again, ends it."), dim_col (), 0, true); y += 15; }
				y += 12;
			}
			else if (e.kind == SK_POINT) { label (12, y, TR ("A mark to snap to: it is part"), dim_col (), 0, true); y += 15; label (12, y, TR ("of no outline."), dim_col (), 0, true); y += 27; }
			else if (e.kind == SK_CIRCLE) { first = field (y, TR ("Diameter"), &e.w, TR ("mm"), 0); y += 40; }
			else if (e.kind == SK_RECT)
			{
				first = field (y, TR ("Width"), &e.w, TR ("mm"), 0); y += 32; field (y, TR ("Height"), &e.h, TR ("mm"), 1); y += 36;
				static const char *const RC[] = { TR ("Corner"), TR ("Centre") };
				label (12, y + 5, TR ("From its")); SegmentedControl *s = new SegmentedControl (width - 12 - 132, y, 132, 26, RC, 2, e.rel ? 1 : 0, onSeg); s->tag = 14; addChild (s); y += 40;
			}
			if (first && !edit && A.curStep > 0) first->setFocus ();
			if (e.kind != SK_CLOSE && e.kind != SK_POINT) { check (y, TR ("Construction (part of no outline)"), e.cons, 19); y += 28; }
			check (y, TR ("Snap to points and angles"), A.snap, 3); y += 38;
			infoY = y; info[0] = TR ("Each element starts from a"); info[1] = TR ("point and keeps its own values."); info[2] = TR ("Change one: what was drawn"); info[3] = TR ("after it follows.");
			if (edit) { ABtn *d = new ABtn (12, height - 40, width - 24, 28, TR ("Delete this element"), CMD_DEL_ELEMENT); d->bg = panel_col (); d->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (d); }
		}
		else if (A.tool == T_SKETCH)			// where the sketch goes: a face clicked, or a plane of the axes
		{
			head (I_SKETCH, TR ("Sketch"), TR ("its plane"));
			label (12, y, TR ("Click a flat face of a body,"), dim_col (), 0, true); label (12, y + 16, TR ("or draw on a plane of the axes:"), dim_col (), 0, true); y += 42;
			static const char *const PL[] = { "XY", "XZ", "YZ" };
			label (12, y + 5, TR ("Plane")); SegmentedControl *s = new SegmentedControl (width - 12 - 126, y, 126, 28, PL, 3, A.skPlane, onSeg); s->tag = 3; addChild (s); y += 38;
			Textbox *o = field (y, TR ("Offset"), &A.skOffset, TR ("mm"), -1); o->setFocus (); y += 34;
			label (12, y, A.skPlane == 0 ? TR ("XY: the ground, seen from above;") : A.skPlane == 1 ? TR ("XZ: upright, seen from the front;") : TR ("YZ: upright, seen from the right;"), dim_col (), 0, true);
			label (12, y + 16, A.skPlane == 0 ? TR ("the offset is its height (Z).") : A.skPlane == 1 ? TR ("the offset is along Y.") : TR ("the offset is along X."), dim_col (), 0, true); y += 46;
			ABtn *b = new ABtn (12, y, width - 24, 28, TR ("Start the sketch"), CMD_SK_START, true); b->bg = panel_col (); addChild (b);
			ABtn *c = new ABtn (12, height - 40, 90, 28, TR ("Cancel"), CMD_CANCEL); c->bg = panel_col (); c->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (c);
		}
		else if (A.hasPend)
		{
			Feature &f = A.pend; feat = 0;
			snprintf (t, sizeof t, "%s %d", TR (KIND_NAME[f.kind]), A.doc.count (f.kind, (int) A.doc.feats.size ()) + 1);
			if (f.kind == F_BOX) head (I_BOX, t, TR ("Base"), TR ("Height"), A.step == 2 ? 1 : 0);
			else if (f.kind == F_CYL) head (I_CYL, t, TR ("Circle"), TR ("Depth"), A.step == 2 ? 1 : 0);
			else if (f.kind == F_TORUS) head (I_TORUS, t, TR ("Ring"), TR ("Tube"), A.step == 2 ? 1 : 0);
			else if (f.kind == F_SPHERE) head (I_SPHERE, t, TR ("Radius"));
			else if (f.kind == F_TAPER) head (I_TAPER, t, TR ("Base"), TR ("Top \xC2\xB7 Height"), A.step >= 2 ? 1 : 0);
			else if (round_kind (f.kind)) head (icon_of_kind (f.kind), t, TR ("Base"), TR ("Height"), A.step == 2 ? 1 : 0);
			else head (icon_of_kind (f.kind), t);
			featureFields (f, y, true);
			okCancel ();
		}
		else if (canvasPage ())			// a canvas: a picture on a plane, to draw over
		{
			Canvas3 &c = A.doc.canvases[A.selCanvas]; char a[24], d[24];
			head (I_SKETCH, c.name, TR ("a canvas, to draw over"));
			label (12, y + 5, TR ("Name")); Textbox *tb = new Textbox (width - 12 - 122, y, 122, 26, c.name); tb->maxLen = 22; tb->changed = onChanged; addChild (tb);
			{ Bind q = { tb, 0, -1, false, c.name }; b[nb++] = q; } y += 34;
			static const char *const PL[] = { "XY", "XZ", "YZ" };
			label (12, y + 5, TR ("Plane")); SegmentedControl *s = new SegmentedControl (width - 12 - 126, y, 126, 28, PL, 3, c.plane, onSeg); s->tag = 16; addChild (s); y += 36;
			field (y, c.plane == 0 ? TR ("Height (Z)") : c.plane == 1 ? TR ("Along Y") : TR ("Along X"), &c.off, TR ("mm"), -1); y += 34;
			label (12, y, TR ("On its plane"), UK_AUTO, 2); y += 20;
			field (y, "X", &c.x, TR ("mm"), -1); y += 28; field (y, "Y", &c.y, TR ("mm"), -1); y += 28; field (y, TR ("Width"), &c.w, TR ("mm"), -1); y += 28; field (y, TR ("Turned"), &c.turn, "\xC2\xB0", -1); y += 30;
			fmt (c.w, a, 12); fmt (c.w * c.aspect, d, 12); snprintf (t, sizeof t, TR ("%s \xC3\x97 %s mm"), a, d); label (12, y, TR ("Its size follows its width."), dim_col (), 0, true); y += 24;
			label (12, y, TR ("Seen"), UK_AUTO, 2); y += 20;
			field (y, TR ("Opacity"), &c.opacity, "%", -1); y += 30; check (y, TR ("Shown"), c.visible, 34); y += 30;
			label (12, y, TR ("Drag it in the view to move it,"), dim_col (), 0, true); y += 15; label (12, y, TR ("a corner to size it."), dim_col (), 0, true);
			ABtn *dl = new ABtn (12, height - 40, width - 24, 28, TR ("Remove this canvas"), CMD_CANVAS_DELETE); dl->bg = panel_col (); dl->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (dl);
		}
		else if (A.selFeat >= 0 && A.selFeat < (int) A.doc.feats.size ())
		{
			Feature &f = A.doc.feats[A.selFeat]; feat = &f;
			head (icon_of_kind (f.kind), f.name, A.selFeat >= A.doc.upto ? TR ("rolled back") : TR ("a step of the history"));
			y = featureFields (f, y, false);
			if (f.failed && A.selFeat < A.doc.upto) { label (12, y, TR ("This step could not be made:"), 0xB03A30, 0, true); label (12, y + 16, f.err, 0xB03A30, 0, true); y += 40; }
			if (f.kind == F_SKETCH) { ABtn *e = new ABtn (12, y, width - 24, 28, TR ("Edit sketch"), CMD_EDIT_SKETCH); e->bg = panel_col (); addChild (e); }
			ABtn *d = new ABtn (12, height - 40, width - 24, 28, TR ("Delete this step"), CMD_DELETE); d->bg = panel_col (); d->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (d);
		}
		else if (A.selBody >= 0 && A.doc.body (A.selBody))
		{
			Body &bd = *A.doc.body (A.selBody); BodyProp &p = A.doc.prop (A.selBody);
			int steps = 0; for (int i = 0; i < A.doc.upto; i++) if (A.doc.feats[i].target == bd.id || i == bd.id) steps++;
			snprintf (t, sizeof t, TR ("Body \xC2\xB7 %d step%s"), steps, steps == 1 ? "" : "s"); head (-2, p.name, t);
			label (12, y + 5, TR ("Name")); Textbox *tb = new Textbox (width - 12 - 122, y, 122, 26, p.name); tb->maxLen = 22; tb->changed = onChanged; addChild (tb);
			{ Bind q = { tb, 0, -1, false, p.name }; b[nb++] = q; } y += 34;
			label (12, y + 5, TR ("Colour")); swY = y + 3; y += 42;
			label (12, y, TR ("Measures"), UK_AUTO, 2); y += 22;
			char a[24], c[24], d[24]; V3 sz = bd.mesh.hi - bd.mesh.lo; fmt (sz.x, a, 12); fmt (sz.y, c, 12); fmt (sz.z, d, 12);
			auto rowv = [&] (const char *k, const char *v) { label (12, y + 3, k, dim_col ()); label (-12, y + 3, v); y += 23; };
			snprintf (t, sizeof t, TR ("%s \xC3\x97 %s \xC3\x97 %s mm"), a, c, d); rowv (TR ("Size"), t);
			snprintf (t, sizeof t, TR ("%.1f cm\xC2\xB3"), bd.m.Volume () / 1000); rowv (TR ("Volume"), t);
			snprintf (t, sizeof t, TR ("%.0f cm\xC2\xB2"), bd.m.SurfaceArea () / 100); rowv (TR ("Surface"), t);
			snprintf (t, sizeof t, "%d", bd.mesh.tris ()); rowv (TR ("Triangles"), t);
			y += 14; label (12, y, TR ("Display"), UK_AUTO, 2); y += 24;
			check (y, TR ("Edges"), A.showEdges, 4); y += 25; check (y, TR ("Grid"), A.showGrid, 5); y += 25; check (y, TR ("See through"), A.seeThrough, 6); y += 37;
			label (12, y, TR ("Print check"), UK_AUTO, 2); y += 24;
			bool ok = bd.m.Status () == Manifold::Error::NoError && !bd.m.IsEmpty ();
			infoY = -(y); info[0] = ok ? TR ("Closed solid, ready to print") : TR ("Not a closed solid"); info[1] = ok ? "" : "!";
		}
		else
		{
			head (-1, "3DForge", A.doc.feats.empty () ? TR ("A new part") : TR ("Nothing selected"));
			if (A.doc.feats.empty ())
			{
				label (12, y, TR ("Start with a shape:"), UK_AUTO, 2); y += 26;
				const char *const s[] = { TR ("Box: click, move away, click,"), TR ("move up, click."), "", TR ("Sketch: draw an outline on a"), TR ("face, then Extrude it."), "",
							  TR ("Each step stays in the history:"), TR ("click it to change its values.") };
				for (const char *q : s) { if (*q) label (12, y, q, dim_col (), 0, true); y += *q ? 16 : 8; }
				y += 14;
			}
			label (12, y, TR ("Display"), UK_AUTO, 2); y += 24;
			check (y, TR ("Edges"), A.showEdges, 4); y += 25; check (y, TR ("Grid"), A.showGrid, 5); y += 25; check (y, TR ("See through"), A.seeThrough, 6); y += 25;
		}
		invalidate (true);
	}
	// The fields show their values again (the pointer moved): but the one being typed.
	void sync ()
	{
		for (int i = 0; i < nb; i++)
		{
			Bind &q = b[i];
			if (!q.val || (q.typed >= 0 && A.typed[q.typed])) continue;
			char t[32]; fmt (q.mag ? fabs (*q.val) : *q.val, t);
			if (strcmp (t, q.tb->text)) { q.tb->setText (t); q.tb->caret = (int) strlen (t); q.tb->invalidate (true); }
		}
	}
	void onDraw () override
	{
		unsigned pc = panel_col (); canvas.clear (C_BG);
		uk_rbox (canvas, 0, 0, width, height, 6, pc, pc); uk_rline (canvas, 0, 0, width, height, 6, uk_tone (C_BG, 96));
		int tx = 12;
		if (headIcon >= 0) { icon (canvas, headIcon, 12, 12, 26, uk_mix (C_TEXT, pc, 40), C_ACCENT, pc); tx = 48; }
		else if (headIcon == -2) { unsigned c = A.doc.prop (A.selBody).colour; uk_rbox (canvas, 12, 14, 22, 22, 4, c, c); uk_rline (canvas, 12, 14, 22, 22, 4, uk_tone (c, 70)); tx = 44; }
		{ UkFaceScope fs (g_big); uk_text (canvas, tx, 10, title, C_TEXT, 2); }
		{
			UkFaceScope fs (g_small); int x = tx;
			for (int i = 0; i < 2; i++)
			{
				if (!sub[i][0]) continue;
				bool done = subAt > i; unsigned col = subAt < 0 ? dim_col () : subAt == i ? C_ACCENT : done ? C_GREEN : uk_mix (C_TEXT, pc, 150);
				if (done) { icon (canvas, I_CHECK, x, 31, 11, C_GREEN, C_GREEN, pc); x += 13; }
				uk_text (canvas, x, 30, sub[i], col); x += uk_tw (sub[i]) + 10;
			}
		}
		canvas.fillRect (10, 52, width - 20, 1, uk_tone (C_BG, 104));
		for (int i = 0; i < nl; i++)
		{
			Lab &l = lab[i]; UkFaceScope fs (l.small ? g_small : (TextFace *) 0);
			int x = l.x < 0 ? width + l.x - uk_tw (l.s, l.style) : l.x;
			uk_text (canvas, x, l.y, l.s, l.col == UK_AUTO ? C_TEXT : l.col, l.style);
		}
		if (printer ()) printDraw (pc);
		else if (A.camMode)			// Manufacture's own: the stock's size, the origin's 27 points, an operation's sum
		{
			char t[96], a[24], b[24], c[24];
			if (camSizeY >= 0)
			{
				V3 s = A.paths.hi - A.paths.lo; fmt (s.x, a, 12); fmt (s.y, b, 12); fmt (s.z, c, 12); snprintf (t, sizeof t, TR ("%s \xC3\x97 %s \xC3\x97 %s mm"), a, b, c);
				uk_text (canvas, 12, camSizeY, TR ("Size"), dim_col ()); uk_text (canvas, width - 12 - uk_tw (t), camSizeY, t, C_TEXT);
			}
			if (camOrgY >= 0)
			{
				static const char *const LN[3] = { TR ("Top"), TR ("Middle"), TR ("Bottom") }; UkFaceScope fs (g_small);
				for (int li = 0; li < 3; li++)
				{
					int gx = 12 + li * 66; uk_text (canvas, gx + (56 - uk_tw (LN[li])) / 2, camOrgY, LN[li], dim_col ());
					uk_sunken (canvas, gx, camOrgY + 15, 56, 44, 4, C_FIELD);
					for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++)
					{
						int k = i + 3 * (2 - j) + 9 * (2 - li); bool on = k == A.job.origin;
						ov_dot (canvas, gx + 10 + i * 18, camOrgY + 23 + j * 14, on ? 4.2 : 3, on ? C_ACCENT : uk_tone (C_FIELD, 100), on ? uk_tone (C_ACCENT, 80) : uk_tone (C_FIELD, 70), 1.2);
					}
				}
			}
			CamOp *op = cam_op ();
			if (camSumY >= 0 && op)
			{
				unsigned ic = uk_mix (pc, 0xFFFFFF, 110); bool bad = op->failed;
				uk_rbox (canvas, 12, camSumY, width - 24, 42, 6, ic, ic); uk_rline (canvas, 12, camSumY, width - 24, 42, 6, uk_tone (C_BG, 104));
				icon (canvas, bad || A.camDirty ? I_WARN : I_CHECK, 20, camSumY + 12, 17, bad || A.camDirty ? 0x4A3A10 : C_GREEN, C_ACCENT, ic);
				UkFaceScope fs (g_small);
				if (A.camDirty) { uk_text (canvas, 44, camSumY + 6, TR ("changed"), 0x8A5A10); uk_text (canvas, 44, camSumY + 22, TR ("Validate to compute"), dim_col ()); }
				else if (bad) { char fit[64]; uk_text_fit (op->err, width - 70, fit, sizeof fit); uk_text (canvas, 44, camSumY + 13, fit, 0x8A5A10); }
				else
				{
					snprintf (t, sizeof t, TR ("%.1f m cut \xC2\xB7 %d min"), op->length / 1000, (int) (op->minutes + 0.5)); uk_text (canvas, 44, camSumY + 6, t, 0x286E38);
					V3 o = cam_origin (A.job, A.paths.lo, A.paths.hi); double lowz = 1e30;
					for (const CamMove &m : A.paths.moves) if (m.op == A.camSel - 1 && m.p.z < lowz) lowz = m.p.z;
					fmt (lowz - o.z, a, 12); snprintf (t, sizeof t, TR ("never below Z %s"), a); uk_text (canvas, 44, camSumY + 22, t, dim_col ());
				}
			}
		}
		if (opY >= 0)				// the operation: one of four
		{
			const Feature &f = A.hasPend ? A.pend : *feat;
			uk_text (canvas, 12, opY, TR ("Operation"), C_TEXT, 2);
			uk_sunken (canvas, 12, opY + 20, width - 24, 4 * 28 + 6, 4, C_FIELD);
			static const int OI[4] = { I_NEWBODY, I_UNION, I_SUB, I_INT };
			for (int i = 0; i < 4; i++)
			{
				int y = opY + 23 + i * 28; bool on = i == f.op; unsigned bg = on ? C_ACCENT : C_FIELD, ink = on ? 0xFFFFFF : C_FIELD_TEXT;
				bool can = i == OP_NEW || A.doc.body (f.target) || !A.doc.bodies.empty ();
				if (on) uk_rbox (canvas, 15, y, width - 30, 28, 4, bg, bg);
				icon (canvas, OI[i], 22, y + 4, 20, on ? 0xFFFFFF : uk_mix (C_FIELD_TEXT, bg, can ? 40 : 170), on ? 0xFFFFFF : C_ACCENT, bg);
				uk_text (canvas, 52, y + (28 - uk_fh ()) / 2, TR (OP_NAME[i]), can ? ink : uk_mix (C_FIELD_TEXT, bg, 150), on ? 2 : 0);
			}
		}
		if (swY > 0)				// the body's colour
			for (int i = 0; i < 5; i++)
			{
				int x = width - 12 - 122 + i * 25; unsigned c = BODY_COLOURS[i];
				uk_rbox (canvas, x, swY, 20, 20, 4, c, c); uk_rline (canvas, x, swY, 20, 20, 4, uk_tone (c, 70));
				if (A.doc.prop (A.selBody).colour == c) { uk_rline (canvas, x - 2, swY - 2, 24, 24, 6, C_ACCENT); uk_rline (canvas, x - 3, swY - 3, 26, 26, 7, C_ACCENT); }
			}
		else if (swY < -1)			// the body a step applies to
		{
			const Feature &f = A.hasPend ? A.pend : *feat; unsigned c = A.doc.prop (f.target).colour;
			uk_rbox (canvas, width - 12 - 112, -swY + 2, 14, 14, 3, c, c); uk_rline (canvas, width - 12 - 112, -swY + 2, 14, 14, 3, uk_tone (c, 70));
		}
		if (infoY > 0)
		{
			unsigned ic = uk_mix (pc, 0xFFFFFF, 110); int n = 0; while (n < 4 && info[n]) n++;
			uk_rbox (canvas, 12, infoY, width - 24, n * 16 + 14, 6, ic, ic); uk_rline (canvas, 12, infoY, width - 24, n * 16 + 14, 6, uk_tone (C_BG, 104));
			UkFaceScope fs (g_small); for (int i = 0; i < n; i++) uk_text (canvas, 22, infoY + 7 + i * 16, info[i], dim_col ());
		}
		else if (infoY < -1)
		{
			bool ok = !info[1][0];
			icon (canvas, ok ? I_CHECK : I_WARN, 12, -infoY - 1, 18, ok ? C_GREEN : 0x4A3A10, C_ACCENT, pc);
			uk_text (canvas, 36, -infoY, info[0], ok ? 0x286E38 : 0x8A5A10);
		}
	}
	bool wasL = false;
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool press = bl && !wasL; wasL = bl != 0;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (!in || !press) return in;
		if (A.camMode && A.gen == 0 && camOrgY >= 0 && my >= camOrgY + 15 && my < camOrgY + 59)		// one of the origin's 27 points
		{
			int li = (mx - 12) / 66, i = ((mx - 12) % 66 - 1) / 18, j = (my - camOrgY - 16) / 14;
			if (mx >= 12 && li < 3 && i >= 0 && i < 3 && j >= 0 && j < 3) { A.job.origin = i + 3 * (2 - j) + 9 * (2 - li); A.doc.changes++; if (g_view) g_view->invalidate (true); ui (0); }
			return true;
		}
		if (opY >= 0 && my >= opY + 23 && my < opY + 23 + 4 * 28 && mx >= 12 && mx < width - 12)
		{
			int i = (my - opY - 23) / 28; Feature &f = A.hasPend ? A.pend : *feat;
			if (i != OP_NEW && !A.doc.body (f.target))
			{
				if (A.doc.bodies.empty ()) return true;
				f.target = A.doc.body (A.selBody) ? A.selBody : A.doc.bodies[0].id;
			}
			if (!A.hasPend) undo_push ();
			f.op = i; A.opAuto = false; applied (); ui (R_PANELS);
			return true;
		}
		if (swY > 0 && my >= swY && my < swY + 20)
			for (int i = 0; i < 5; i++)
			{
				int x = width - 12 - 122 + i * 25;
				if (mx >= x && mx < x + 20) { A.doc.prop (A.selBody).colour = BODY_COLOURS[i]; A.doc.changes++; if (g_view) g_view->invalidate (true); ui (R_PANELS); }
			}
		return true;
	}
};

// ---- the status bar ----------------------------------------------------------------------------------------------------
class StatusBar : public Widget
{
public:
	enum { H = 27 };
	int snapX, snapW, gridX, gridW;
	StatusBar (int t, int w) : Widget (0, t, w, H), snapX (0), snapW (0), gridX (0), gridW (0) {}
	void onDraw () override
	{
		canvas.clear (C_BG); uk_etch_h (canvas, 0, 0, width, C_BG);
		char t[64]; int rx = width - 12;
		{
			UkFaceScope fs (g_small); int ty = (H - uk_fh ()) / 2 + 1;
			auto item = [&] (const char *s, unsigned col, int *x0, int *w0)
			{
				int w = uk_tw (s); rx -= w; uk_text (canvas, rx, ty, s, col); if (x0) { *x0 = rx - 6; *w0 = w + 12; }
				rx -= 7; uk_etch_v (canvas, rx, 7, 14, C_BG); rx -= 7;
			};
			item (TR ("mm"), dim_col (), 0, 0);
			item (A.gpu ? "GPU" : "CPU", dim_col (), 0, 0);
			item (A.snap ? TR ("Snap 1 mm") : TR ("Snap off"), A.snap ? dim_col () : uk_mix (C_TEXT, C_BG, 170), &snapX, &snapW);
			item (A.showGrid ? TR ("Grid 10 mm") : TR ("Grid off"), A.showGrid ? dim_col () : uk_mix (C_TEXT, C_BG, 170), &gridX, &gridW);
			int n = A.doc.tris (); if (n >= 1000) snprintf (t, sizeof t, TR ("%d %03d triangles"), n / 1000, n % 1000); else snprintf (t, sizeof t, TR ("%d triangles"), n);
			item (t, dim_col (), 0, 0);
		}
		icon (canvas, I_INFO, 10, 5, 17, C_ACCENT, C_ACCENT, C_BG);
		char fit[200]; uk_text_fit (A.hint, rx - 40, fit, sizeof fit); uk_text (canvas, 33, (H - uk_fh ()) / 2 + 1, fit, uk_mix (C_TEXT, C_BG, 30));
	}
	bool wasL = false;
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool press = bl && !wasL; wasL = bl != 0;
		if (mx < 0 || my < 0 || mx >= width || my >= height) return false;
		if (press && mx >= snapX && mx < snapX + snapW) { A.snap = !A.snap; ui (R_PANELS); }
		if (press && mx >= gridX && mx < gridX + gridW) { A.showGrid = !A.showGrid; if (g_view) g_view->invalidate (true); ui (R_PANELS); }
		return true;
	}
};

} // namespace forge

#endif
