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
       CMD_DELETE, CMD_EDIT_SKETCH, CMD_ROLL_HERE, CMD_ROLL_END, CMD_DEL_ELEMENT, CMD_SHAPES, CMD_SK_START };

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
		static const char *const nm[N] = { "Box", "Cylinder", "Sphere", "Torus", "Pyramid", "Prism", "Taper" };
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
	{ -4, I_SHAPES, "Shapes" }, { T_SKETCH, I_SKETCH, "Sketch" }, { T_EXTRUDE, I_EXTRUDE, "Extrude" }, { -1, 0, 0 },
	{ T_FILLET, I_FILLET, "Fillet" }, { T_CHAMFER, I_CHAMFER, "Chamfer" }, { T_MOVE, I_MOVE, "Move" }, { -1, 0, 0 },
	{ T_UNION, I_UNION, "Union" }, { T_SUB, I_SUB, "Subtract" }, { T_INT, I_INT, "Intersect" }, { -1, 0, 0 }, { T_MEASURE, I_MEASURE, "Measure" }, { -2, 0, 0 } };
static const ToolDef SKTOOLS[] = {
	{ T_LINE, I_LINE, "Line" }, { T_RECT, I_RECT, "Rectangle" }, { T_CIRCLE, I_CIRCLE, "Circle" }, { T_ARC, I_ARC, "Arc" }, { -3, I_CLOSE, "Close" }, { -2, 0, 0 } };

class Ribbon : public Widget
{
public:
	enum { H = 62, MAXHIT = 32 };
	struct Zone { int x, y, w, h, id; bool tool; } z[MAXHIT]; int nz, hot, down;
	ABtn *bExport, *bCancel, *bFinish;
	Ribbon (int w) : Widget (0, 0, w, H), nz (0), hot (-1), down (-1)
	{
		bExport = new ABtn (w - 108, 14, 98, 28, "Export", CMD_EXPORT, true, I_EXPORT);
		bCancel = new ABtn (w - 232, 14, 84, 28, "Cancel", CMD_SK_CANCEL);
		bFinish = new ABtn (w - 140, 14, 130, 28, "Finish sketch", CMD_SK_FINISH, true);
		for (ABtn *b : { bExport, bCancel, bFinish }) { b->anchor = ANCHOR_TOP | ANCHOR_RIGHT; addChild (b); }
		sync ();
	}
	void sync () { bExport->hidden = A.sketching; bCancel->hidden = bFinish->hidden = !A.sketching; invalidate (true); }
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
		const ToolDef *list = A.sketching ? SKTOOLS : TOOLS;
		for (int i = 0; list[i].tool != -2; i++)
		{
			if (list[i].tool == -1) { uk_etch_v (canvas, x + 4, 8, 46, C_BG); x += 10; continue; }
			// (the shapes' button: an arrow -- a click unfolds them)
			bool more = list[i].tool == -4; int tl = list[i].tool, ic = list[i].ic;
			const char *nm = list[i].name;
			int bw = uk_tw (nm) + (more ? 26 : 14); if (bw < 54) bw = 54;
			bool on = tl == A.tool || (more && shape_tool (A.tool)), isHot = nz == hot;
			unsigned bg = C_BG;
			if (on) { bg = soft_col (C_BG); uk_rbox (canvas, x, 4, bw, 54, 6, bg, bg); uk_rline (canvas, x, 4, bw, 54, 6, C_ACCENT); }
			else if (isHot) { bg = uk_tone (C_BG, 150); uk_rbox (canvas, x, 4, bw, 54, 6, bg, bg); }
			icon (canvas, ic, x + bw / 2 - 13, 8, 26, ink, C_ACCENT, bg);
			int tx = x + (bw - uk_tw (nm) - (more ? 10 : 0)) / 2; uk_text (canvas, tx, 38, nm, C_TEXT);
			if (more) uk_glyph (canvas, WKG_CHEV_DOWN, tx + uk_tw (nm) + 6, 46, 7, C_TEXT);
			zone (x, 4, bw, 54, more ? CMD_SHAPES : list[i].tool == -3 ? CMD_SK_CLOSE : tl, !more && list[i].tool != -3); x += bw + 3;
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
	Timeline (int l, int t, int w) : Widget (l, t, w, H), scroll (0), hot (-1), lastI (-1), shownSel (-1), shownN (0), pressX (0), pressScroll (0), pressI (-1), lastT (0), wasL (false), wasR (false), moved (false) {}
	int count () const { return A.sketching ? (int) A.sk.els.size () : (int) A.doc.feats.size (); }
	int sel () const { return A.sketching ? A.selEl : A.selFeat; }
	// what a step, or an element, says of itself
	void text (int i, char *out, int cap) const
	{
		char t[96], a[24], b[24], c[24];
		if (!A.sketching)
		{
			const Feature &f = A.doc.feats[i];
			if (f.failed && i < A.doc.upto) snprintf (t, sizeof t, "%s", f.err); else describe (A.doc, f, t, sizeof t);
			snprintf (out, cap, "%s  \xC2\xB7  %s%s", f.name, t, i >= A.doc.upto ? "  (rolled back)" : ""); return;
		}
		static const char *const EN[] = { "Line", "Arc", "Circle", "Rectangle", "Close" };
		const SkEl &e = A.sk.els[i]; int k = 0; for (int j = 0; j <= i; j++) if (A.sk.els[j].kind == e.kind) k++;
		if (e.kind == SK_LINE) { fmt (e.len, a); fmt (e.a, b); snprintf (t, sizeof t, e.rel ? "%s \xC2\xB7 turn %s\xC2\xB0" : "%s \xC2\xB7 at %s\xC2\xB0", a, b); }
		else if (e.kind == SK_ARC) { fmt (e.r, a); fmt (fabs (e.sweep), b); snprintf (t, sizeof t, "R %s \xC2\xB7 %s\xC2\xB0", a, b); }
		else if (e.kind == SK_CIRCLE) { fmt (e.w, a); fmt (e.x, b); fmt (e.y, c); snprintf (t, sizeof t, "\xC3\x98 %s \xC2\xB7 at %s, %s", a, b, c); }
		else if (e.kind == SK_RECT) { fmt (fabs (e.w), a); fmt (fabs (e.h), b); snprintf (t, sizeof t, "%s \xC3\x97 %s", a, b); }
		else snprintf (t, sizeof t, "back to the start");
		snprintf (out, cap, "%s %d  \xC2\xB7  %s", EN[e.kind], k, t);
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
		static const int EI[] = { I_LINE, I_ARC, I_CIRCLE, I_RECT, I_CLOSE };
		unsigned ink = uk_mix (C_FIELD_TEXT, C_FIELD, 40);
		for (int i = 0; i < n; i++)
		{
			int x = x0 + i * CELL - scroll;
			if (x < x0 || x + CELL > x1 + 6) continue;
			bool on = i == sel (), off = !A.sketching && i >= A.doc.upto, bad = !A.sketching && A.doc.feats[i].failed && !off;
			unsigned bg = C_FIELD;
			if (on) { bg = soft_col (C_FIELD); uk_rbox (canvas, x + 1, 5, CELL - 4, H - 10, 6, bg, bg); uk_rline (canvas, x + 1, 5, CELL - 4, H - 10, 6, C_ACCENT); }
			else if (i == hot) { bg = uk_tone (C_FIELD, 112); uk_rbox (canvas, x + 1, 5, CELL - 4, H - 10, 6, bg, bg); }
			int ic = A.sketching ? EI[A.sk.els[i].kind] : icon_of_kind (A.doc.feats[i].kind);
			icon (canvas, ic, x + (CELL - 2 - 24) / 2, (H - 24) / 2, 24, off ? uk_mix (ink, bg, 170) : ink, off ? uk_mix (C_ACCENT, bg, 170) : C_ACCENT, bg);
			if (bad) icon (canvas, I_WARN, x + CELL - 19, H - 20, 14, 0x4A3A10, C_ACCENT, bg);
		}
		if (!A.sketching && n)							// where the part is rebuilt up to
		{
			int mx = x0 + A.doc.upto * CELL - scroll - 2;
			if (mx >= x0 - 2 && mx <= x1) { canvas.fillRect (mx, 6, 2, H - 12, C_ACCENT); VPath p; int xy[6] = { (mx - 4) * 16, 2 * 16, (mx + 6) * 16, 2 * 16, (mx + 1) * 16, 8 * 16 }; p.poly (xy, 3); p.fill (canvas, C_ACCENT); }
		}
		if (arrows)
		{
			uk_glyph (canvas, WKG_CHEV_LEFT, 8 + ARROW / 2, H / 2, 10, scroll > 0 ? C_FIELD_TEXT : uk_mix (C_FIELD_TEXT, C_FIELD, 190));
			uk_glyph (canvas, WKG_CHEV_RIGHT, x1 + ARROW / 2, H / 2, 10, scroll < most ? C_FIELD_TEXT : uk_mix (C_FIELD_TEXT, C_FIELD, 190));
		}
		if (!n) { UkFaceScope fs (g_small); uk_text (canvas, 14, (H - uk_fh ()) / 2, A.sketching ? "The sketch's elements come here, in the order they are drawn." : "The steps you make come here: click one to change its values.", uk_mix (C_FIELD_TEXT, C_FIELD, 140)); }
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
			snprintf (t, sizeof t, "%d closed outline%s", A.ev.nclosed, A.ev.nclosed == 1 ? "" : "s");
			if (A.ev.nclosed) icon (canvas, I_CHECK, tx, 6, 14, C_GREEN, C_GREEN, C_FIELD);
			uk_text (canvas, tx + 18, 7, t, A.ev.nclosed ? 0x286E38 : uk_mix (C_FIELD_TEXT, C_FIELD, 110));
			if (A.ev.nopen) { snprintf (t, sizeof t, "%d still open", A.ev.nopen); icon (canvas, I_WARN, tx, 22, 14, 0x4A3A10, C_ACCENT, C_FIELD); }
			else snprintf (t, sizeof t, "none open");
			uk_text (canvas, tx + 18, 23, t, A.ev.nopen ? 0x8A5A10 : uk_mix (C_FIELD_TEXT, C_FIELD, 110));
		}
		else if (n) { snprintf (t, sizeof t, "%d step%s", n, n == 1 ? "" : "s"); uk_text (canvas, tx, 7, "History", C_FIELD_TEXT, 2); uk_text (canvas, tx, 23, t, uk_mix (C_FIELD_TEXT, C_FIELD, 110)); }
	}
	void pick (int i, bool dbl, bool right, int mx, int my)
	{
		if (A.sketching) { A.selEl = A.selEl == i && !right ? -1 : i; sk_arm (); ui (R_ALL); return; }
		if (A.tool != T_SELECT) tool_set (T_SELECT);
		A.selFeat = i; A.selBody = -1; ui (R_ALL);
		if (right)
		{
			Root *r = Root::current (); int ax = 0, ay = 0; for (Widget *w = this; w && w != r; w = w->parent) { ax += w->left; ay += w->top; }
			PopupMenu pm (ax + mx, ay + my - 110);
			if (A.doc.feats[i].kind == F_SKETCH) pm.add ("Edit sketch", CMD_EDIT_SKETCH);
			pm.add ("Roll back to here", CMD_ROLL_HERE, i + 1 != A.doc.upto); pm.add ("Roll to the end", CMD_ROLL_END, A.doc.upto < (int) A.doc.feats.size ());
			pm.separator (); pm.add ("Delete", CMD_DELETE);
			int c = pm.run (); if (c > 0) cmd (c);
		}
		else if (dbl && A.doc.feats[i].kind == F_SKETCH) cmd (CMD_EDIT_SKETCH);
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		bool press = bl && !wasL, release = !bl && wasL, rpress = br && !wasR; wasL = bl != 0; wasR = br != 0;
		if (!in) { if (hot >= 0) { hot = -1; invalidate (true); } pressI = -1; return false; }
		int x0, x1; bool arrows; room (&x0, &x1, &arrows);
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
			if (q.name) { snprintf (q.name, 24, "%s", q.tb->text); A.doc.changes++; ui (0); if (g_view) g_view->invalidate (true); return; }
			double v = parse (q.tb->text);
			if (q.mag) v = (*q.val < 0 ? -1 : 1) * fabs (v);
			*q.val = v; if (q.typed >= 0) A.typed[q.typed] = true;
			p->applied ();
		}
	}
	// A value changed (a field, the operation, a check box): what it belongs to is made again.
	void applied ()
	{
		if (A.sketching) { sk_eval (); }
		else if (A.hasPend) { auto_op (); preview_update (); if (A.prevErr) set_hint (A.prevErr); else tool_hint (); }
		else if (feat) { A.doc.touch (); }
		if (g_view) g_view->invalidate (true);
		ui (0);
	}
	static void onEnter (Widget &) { cmd (A.tool == T_SKETCH && !A.sketching ? CMD_SK_START : CMD_OK); }
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
		}
		if (w.tag >= 4 && w.tag <= 6) { if (g_view) g_view->invalidate (true); ui (0); return; }
		if (!A.hasPend && !A.sketching && f) undo_push ();
		p->applied (); ui (R_PANELS);
	}
	static void onSeg (Widget &w)
	{
		SegmentedControl &s = (SegmentedControl &) w; Props *p = self (w);
		if (w.tag == 1) { tool_set (s.selected == 0 ? T_FILLET : T_CHAMFER); return; }
		if (w.tag == 2 && p->el) { p->el->sweep = (s.selected == 0 ? 1 : -1) * fabs (p->el->sweep); p->applied (); }
		if (w.tag == 3) { A.skPlane = s.selected; ui (R_ALL); }
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
	void okCancel (const char *ok = "OK")
	{
		ABtn *c = new ABtn (12, height - 40, 90, 28, "Cancel", CMD_CANCEL); c->bg = panel_col (); c->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (c);
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
			Textbox *w = field (y, "Width", &f.w, "mm", 0, true); y += 32; field (y, "Depth", &f.d, "mm", 1, true); y += 32;
			Textbox *h = field (y, "Height", &f.h, "mm", 2, true); y += 44;
			if (making) (A.step == 2 ? h : w)->setFocus ();
			y -= 12; break;
		}
		case F_CYL:
		{
			Textbox *d = field (y, "Diameter", &f.w, "mm", 0); y += 32; Textbox *h = field (y, f.h < 0 || f.through ? "Depth" : "Height", &f.h, "mm", 1, true); y += 34;
			if (making) (A.step == 2 ? h : d)->setFocus ();
			check (y, "Through the whole body", f.through, 2); y += 26; break;
		}
		case F_PYRAMID: case F_PRISM: case F_TAPER:
		{
			Textbox *n = field (y, "Sides", &f.n, 0, -1); y += 32;
			Textbox *d = field (y, "Base radius", &f.w, "mm", 0); y += 32; Textbox *top = 0;
			if (f.kind == F_TAPER) { top = field (y, "Top radius", &f.d, "mm", 2); y += 32; }
			Textbox *h = field (y, "Height", &f.h, "mm", 1, true); y += 32;
			// (the field of what is being set has the keyboard: the sides first, Tab for the radius)
			if (making) { Textbox *t = A.step <= 1 ? n : A.step == 2 && top ? top : h; t->setFocus (); t->caret = (int) strlen (t->text); (void) d; }
			break;
		}
		case F_SPHERE: { Textbox *r = field (y, "Radius", &f.w, "mm", 0); y += 32; if (making) r->setFocus (); break; }
		case F_TORUS:
		{
			Textbox *r = field (y, "Ring radius", &f.w, "mm", 0); y += 32; Textbox *t = field (y, "Tube radius", &f.d, "mm", 1); y += 32;
			if (making) (A.step == 2 ? t : r)->setFocus ();
			break;
		}
		case F_EXTRUDE:
		{
			Textbox *h = field (y, f.h < 0 || f.through ? "Depth" : "Height", &f.h, "mm", 1, true); y += 34; if (making) h->setFocus ();
			check (y, "Through the whole body", f.through, 2); y += 34; break;
		}
		case F_FILLET: case F_CHAMFER:
		{
			static const char *const FC[] = { "Fillet", "Chamfer" };
			if (making) { SegmentedControl *s = new SegmentedControl (12, y, width - 24, 28, FC, 2, f.kind == F_FILLET ? 0 : 1, onSeg); s->tag = 1; addChild (s); y += 40; }
			Textbox *r = field (y, f.kind == F_FILLET ? "Radius" : "Size", &f.r, "mm", 0); y += 42; if (making) r->setFocus ();
			char t[48]; label (12, y, "Edges", UK_AUTO, 2); snprintf (t, sizeof t, "%d chosen", (int) f.edges.size ()); label (width - 12 - 70, y + 2, t, dim_col (), 0, true); y += 24;
			if (f.edges.empty ()) label (12, y, "Click an edge of a body", dim_col (), 0, true);
			else { Body *b = A.doc.body (f.target); snprintf (t, sizeof t, "on %s", body_name (f.target)); label (12, y, t, dim_col (), 0, true); (void) b; }
			y += 30;
			if (making) { infoY = y; info[0] = "Straight edges and edges on"; info[1] = "a circle can be rounded; the"; info[2] = "others show in grey."; info[3] = 0; y += 70; }
			break;
		}
		case F_MOVE:
		{
			y -= 4; label (12, y, "Move", UK_AUTO, 2); y += 20;
			Textbox *x = field (y, "Along X", &f.mv.x, "mm", 0); y += 29; field (y, "Along Y", &f.mv.y, "mm", 1); y += 29; field (y, "Along Z", &f.mv.z, "mm", 2); y += 34;
			label (12, y, "Turn, about its centre", UK_AUTO, 2); y += 20;
			field (y, "Around X", &f.rot.x, "\xC2\xB0", -1); y += 29; field (y, "Around Y", &f.rot.y, "\xC2\xB0", -1); y += 29; field (y, "Around Z", &f.rot.z, "\xC2\xB0", -1); y += 34;
			label (12, y, "Scale (1: as it is)", UK_AUTO, 2); y += 20;
			field (y, "Along X", &f.sc.x, "\xC3\x97", -1); y += 29; field (y, "Along Y", &f.sc.y, "\xC3\x97", -1); y += 29; field (y, "Along Z", &f.sc.z, "\xC3\x97", -1); y += 32;
			check (y, "Clone: the original stays", f.clone, 8); y += 28;
			if (making && A.step == 1) x->setFocus ();
			break;
		}
		case F_COMBINE: { char t[64]; snprintf (t, sizeof t, "%s, with another body", OP_NAME[f.op]); label (12, y, t, dim_col ()); y += 30; break; }
		case F_SKETCH: { char t[64]; describe (A.doc, f, t, sizeof t); label (12, y, t, dim_col ()); y += 30; break; }
		}
		if (makes_solid (f.kind) && f.kind != F_EXTRUDE)		// where it is, on its plane
		{
			y += 4; field (y, f.kind == F_BOX && !f.centred ? "Corner X" : "Centre X", &f.x, "mm", -1); y += 32; field (y, "Y", &f.y, "mm", -1); y += 40;
		}
		if (makes_solid (f.kind))
		{
			opY = y; y += 20 + 4 * 28 + 6 + 12;
			if (f.op != OP_NEW && A.doc.body (f.target))
			{
				label (12, y + 2, f.op == OP_UNION ? "Joined to" : f.op == OP_SUB ? "Cut from" : "Kept inside", dim_col ());
				label (width - 12 - 92, y + 2, body_name (f.target)); swY = -(y + 1); y += 30;
			}
			if (f.kind == F_BOX) { check (y, "Centred on the first click", f.centred, 1); y += 28; }
		}
		return y;
	}
	void rebuild ()
	{
		while (firstChild) { Widget *c = firstChild; removeChild (c); delete c; }
		nb = nl = 0; opY = swY = infoY = -1; headIcon = -1; title[0] = 0; sub[0][0] = sub[1][0] = 0; subAt = -1; feat = 0; el = 0;
		int y = 64; char t[96];
		if (A.sketching)
		{
			static const char *const EN[] = { "Line", "Arc", "Circle", "Rectangle", "Close" }; static const int EI[] = { I_LINE, I_ARC, I_CIRCLE, I_RECT, I_CLOSE };
			bool edit = A.selEl >= 0 && A.selEl < (int) A.sk.els.size ();
			SkEl &e = edit ? A.sk.els[A.selEl] : A.cur; el = &e;
			if (e.kind == SK_ARC && !edit) head (EI[e.kind], "Arc", "Start", "Centre \xC2\xB7 End", A.curStep == 0 ? 0 : 1);
			else head (EI[e.kind], EN[e.kind], edit ? "selected" : 0);
			bool chained = e.chain;
			if (e.kind != SK_CLOSE && !chained) { field (y, e.kind == SK_CIRCLE ? "Centre X" : "Starts at X", &e.x, "mm", -1, false, 96); y += 32; field (y, "Y", &e.y, "mm", -1, false, 96); y += 32; }
			else if (e.kind != SK_CLOSE) { label (12, y + 5, "Starts at"); label (width - 12 - 118, y + 5, "the end before", 0x2862B0); y += 32; }
			Textbox *first = 0;
			if (e.kind == SK_LINE) { first = field (y, "Length", &e.len, "mm", 0); y += 32; field (y, "Angle", &e.a, "\xC2\xB0", 1); y += 34; if (chained) { check (y, "From the line before", e.rel, 7); y += 28; } }
			else if (e.kind == SK_ARC)
			{
				first = field (y, "Radius", &e.r, "mm", 0); y += 32; field (y, "Centre at", &e.ca, "\xC2\xB0", -1); y += 32; Textbox *sw = field (y, "Sweep", &e.sweep, "\xC2\xB0", 1, true); y += 40;
				if (!edit && A.curStep == 2) first = sw;
				static const char *const LR[] = { "Left", "Right" };
				label (12, y + 5, "Turns"); SegmentedControl *s = new SegmentedControl (width - 12 - 112, y, 112, 26, LR, 2, e.sweep < 0 ? 1 : 0, onSeg); s->tag = 2; addChild (s); y += 40;
			}
			else if (e.kind == SK_CIRCLE) { first = field (y, "Diameter", &e.w, "mm", 0); y += 40; }
			else if (e.kind == SK_RECT) { first = field (y, "Width", &e.w, "mm", 0); y += 32; field (y, "Height", &e.h, "mm", 1); y += 40; }
			if (first && !edit && A.curStep > 0) first->setFocus ();
			check (y, "Snap to points and angles", A.snap, 3); y += 38;
			infoY = y; info[0] = "Each element starts from a"; info[1] = "point and keeps its own values."; info[2] = "Change one: what was drawn"; info[3] = "after it follows.";
			if (edit) { ABtn *d = new ABtn (12, height - 40, width - 24, 28, "Delete this element", CMD_DEL_ELEMENT); d->bg = panel_col (); d->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (d); }
		}
		else if (A.tool == T_SKETCH)			// where the sketch goes: a face clicked, or a plane of the axes
		{
			head (I_SKETCH, "Sketch", "its plane");
			label (12, y, "Click a flat face of a body,", dim_col (), 0, true); label (12, y + 16, "or draw on a plane of the axes:", dim_col (), 0, true); y += 42;
			static const char *const PL[] = { "XY", "XZ", "YZ" };
			label (12, y + 5, "Plane"); SegmentedControl *s = new SegmentedControl (width - 12 - 126, y, 126, 28, PL, 3, A.skPlane, onSeg); s->tag = 3; addChild (s); y += 38;
			Textbox *o = field (y, "Offset", &A.skOffset, "mm", -1); o->setFocus (); y += 34;
			label (12, y, A.skPlane == 0 ? "XY: the ground, seen from above;" : A.skPlane == 1 ? "XZ: upright, seen from the front;" : "YZ: upright, seen from the right;", dim_col (), 0, true);
			label (12, y + 16, A.skPlane == 0 ? "the offset is its height (Z)." : A.skPlane == 1 ? "the offset is along Y." : "the offset is along X.", dim_col (), 0, true); y += 46;
			ABtn *b = new ABtn (12, y, width - 24, 28, "Start the sketch", CMD_SK_START, true); b->bg = panel_col (); addChild (b);
			ABtn *c = new ABtn (12, height - 40, 90, 28, "Cancel", CMD_CANCEL); c->bg = panel_col (); c->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (c);
		}
		else if (A.hasPend)
		{
			Feature &f = A.pend; feat = 0;
			snprintf (t, sizeof t, "%s %d", KIND_NAME[f.kind], A.doc.count (f.kind, (int) A.doc.feats.size ()) + 1);
			if (f.kind == F_BOX) head (I_BOX, t, "Base", "Height", A.step == 2 ? 1 : 0);
			else if (f.kind == F_CYL) head (I_CYL, t, "Circle", "Depth", A.step == 2 ? 1 : 0);
			else if (f.kind == F_TORUS) head (I_TORUS, t, "Ring", "Tube", A.step == 2 ? 1 : 0);
			else if (f.kind == F_SPHERE) head (I_SPHERE, t, "Radius");
			else if (f.kind == F_TAPER) head (I_TAPER, t, "Base", "Top \xC2\xB7 Height", A.step >= 2 ? 1 : 0);
			else if (round_kind (f.kind)) head (icon_of_kind (f.kind), t, "Base", "Height", A.step == 2 ? 1 : 0);
			else head (icon_of_kind (f.kind), t);
			featureFields (f, y, true);
			okCancel ();
		}
		else if (A.selFeat >= 0 && A.selFeat < (int) A.doc.feats.size ())
		{
			Feature &f = A.doc.feats[A.selFeat]; feat = &f;
			head (icon_of_kind (f.kind), f.name, A.selFeat >= A.doc.upto ? "rolled back" : "a step of the history");
			y = featureFields (f, y, false);
			if (f.failed && A.selFeat < A.doc.upto) { label (12, y, "This step could not be made:", 0xB03A30, 0, true); label (12, y + 16, f.err, 0xB03A30, 0, true); y += 40; }
			if (f.kind == F_SKETCH) { ABtn *e = new ABtn (12, y, width - 24, 28, "Edit sketch", CMD_EDIT_SKETCH); e->bg = panel_col (); addChild (e); }
			ABtn *d = new ABtn (12, height - 40, width - 24, 28, "Delete this step", CMD_DELETE); d->bg = panel_col (); d->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (d);
		}
		else if (A.selBody >= 0 && A.doc.body (A.selBody))
		{
			Body &bd = *A.doc.body (A.selBody); BodyProp &p = A.doc.prop (A.selBody);
			int steps = 0; for (int i = 0; i < A.doc.upto; i++) if (A.doc.feats[i].target == bd.id || i == bd.id) steps++;
			snprintf (t, sizeof t, "Body \xC2\xB7 %d step%s", steps, steps == 1 ? "" : "s"); head (-2, p.name, t);
			label (12, y + 5, "Name"); Textbox *tb = new Textbox (width - 12 - 122, y, 122, 26, p.name); tb->maxLen = 22; tb->changed = onChanged; addChild (tb);
			{ Bind q = { tb, 0, -1, false, p.name }; b[nb++] = q; } y += 34;
			label (12, y + 5, "Colour"); swY = y + 3; y += 42;
			label (12, y, "Measures", UK_AUTO, 2); y += 22;
			char a[24], c[24], d[24]; V3 sz = bd.mesh.hi - bd.mesh.lo; fmt (sz.x, a, 12); fmt (sz.y, c, 12); fmt (sz.z, d, 12);
			auto rowv = [&] (const char *k, const char *v) { label (12, y + 3, k, dim_col ()); label (-12, y + 3, v); y += 23; };
			snprintf (t, sizeof t, "%s \xC3\x97 %s \xC3\x97 %s mm", a, c, d); rowv ("Size", t);
			snprintf (t, sizeof t, "%.1f cm\xC2\xB3", bd.m.Volume () / 1000); rowv ("Volume", t);
			snprintf (t, sizeof t, "%.0f cm\xC2\xB2", bd.m.SurfaceArea () / 100); rowv ("Surface", t);
			snprintf (t, sizeof t, "%d", bd.mesh.tris ()); rowv ("Triangles", t);
			y += 14; label (12, y, "Display", UK_AUTO, 2); y += 24;
			check (y, "Edges", A.showEdges, 4); y += 25; check (y, "Grid", A.showGrid, 5); y += 25; check (y, "See through", A.seeThrough, 6); y += 37;
			label (12, y, "Print check", UK_AUTO, 2); y += 24;
			bool ok = bd.m.Status () == Manifold::Error::NoError && !bd.m.IsEmpty ();
			infoY = -(y); info[0] = ok ? "Closed solid, ready to print" : "Not a closed solid"; info[1] = ok ? "" : "!";
		}
		else
		{
			head (-1, "3DForge", A.doc.feats.empty () ? "A new part" : "Nothing selected");
			if (A.doc.feats.empty ())
			{
				label (12, y, "Start with a shape:", UK_AUTO, 2); y += 26;
				const char *const s[] = { "Box: click, move away, click,", "move up, click.", "", "Sketch: draw an outline on a", "face, then Extrude it.", "",
							  "Each step stays in the history:", "click it to change its values." };
				for (const char *q : s) { if (*q) label (12, y, q, dim_col (), 0, true); y += *q ? 16 : 8; }
				y += 14;
			}
			label (12, y, "Display", UK_AUTO, 2); y += 24;
			check (y, "Edges", A.showEdges, 4); y += 25; check (y, "Grid", A.showGrid, 5); y += 25; check (y, "See through", A.seeThrough, 6); y += 25;
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
		if (opY >= 0)				// the operation: one of four
		{
			const Feature &f = A.hasPend ? A.pend : *feat;
			uk_text (canvas, 12, opY, "Operation", C_TEXT, 2);
			uk_sunken (canvas, 12, opY + 20, width - 24, 4 * 28 + 6, 4, C_FIELD);
			static const int OI[4] = { I_NEWBODY, I_UNION, I_SUB, I_INT };
			for (int i = 0; i < 4; i++)
			{
				int y = opY + 23 + i * 28; bool on = i == f.op; unsigned bg = on ? C_ACCENT : C_FIELD, ink = on ? 0xFFFFFF : C_FIELD_TEXT;
				bool can = i == OP_NEW || A.doc.body (f.target) || !A.doc.bodies.empty ();
				if (on) uk_rbox (canvas, 15, y, width - 30, 28, 4, bg, bg);
				icon (canvas, OI[i], 22, y + 4, 20, on ? 0xFFFFFF : uk_mix (C_FIELD_TEXT, bg, can ? 40 : 170), on ? 0xFFFFFF : C_ACCENT, bg);
				uk_text (canvas, 52, y + (28 - uk_fh ()) / 2, OP_NAME[i], can ? ink : uk_mix (C_FIELD_TEXT, bg, 150), on ? 2 : 0);
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
			item ("mm", dim_col (), 0, 0);
			item (A.gpu ? "GPU" : "CPU", dim_col (), 0, 0);
			item (A.snap ? "Snap 1 mm" : "Snap off", A.snap ? dim_col () : uk_mix (C_TEXT, C_BG, 170), &snapX, &snapW);
			item (A.showGrid ? "Grid 10 mm" : "Grid off", A.showGrid ? dim_col () : uk_mix (C_TEXT, C_BG, 170), &gridX, &gridW);
			int n = A.doc.tris (); if (n >= 1000) snprintf (t, sizeof t, "%d %03d triangles", n / 1000, n % 1000); else snprintf (t, sizeof t, "%d triangles", n);
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
