//
// sidebar.h -- the panel at the right, four tabs: Slide (its layout, the theme, the colours and fonts, the
// background, the master's objects, the slide's size, hidden), Shape (the fill, its transparency, the line,
// the corners, the shadow, the arrow heads, the place and size in cm, the rotation, the order, the alignment;
// a picture's, a table's, a chart's own), Text (the font, the size, bold..., the colour, the alignment, the
// list, the level, the spacing, the anchor, the autofit) and Animate (the transition to the slide and how it
// goes on; the slide's effects in their order, the one chosen's settings). Rows built again from the state
// at each change, drawn and clicked here: a list dropping, a colour palette, a number with its arrows (or
// typed), a tick, a row of buttons.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_sidebar_h
#define _slides_sidebar_h

#include "panes.h"

namespace sl {

enum { SB_SLIDE, SB_SHAPE, SB_TEXT, SB_ANIM };
static int g_sbTab = SB_SLIDE;
static int g_animSel = -1;			// the effect chosen in the list
static void (*g_onPlayEffects) ();		// Animate's Play: the slide shown from here
static void (*g_onChartData) (Object *o);	// a chart's data edited
static void (*g_onSidebarCmd) (int cmd);	// a command of main.cpp's (insert a row...)

// The app's icons (ss::ToolButton draws them from 200 on: ui_base.h's g_appIcon)
enum { SL_NEWSLIDE = 200, SL_LAYOUT, SL_DUPSLIDE, SL_DELSLIDE, SL_TEXTBOX, SL_SHAPES, SL_LINE, SL_ARROW, SL_PICTURE, SL_TABLE,
       SL_CHART, SL_SHOW, SL_FORWARD, SL_BACKWARD, SL_FRONT, SL_BACK, SL_AL_LEFT, SL_AL_CENTER, SL_AL_RIGHT, SL_AL_TOP,
       SL_AL_MIDDLE, SL_AL_BOTTOM, SL_SPACING, SL_ANCHOR_T, SL_ANCHOR_M, SL_ANCHOR_B, SL_DIST_H, SL_DIST_V, SL_REMOVE, SL_UP, SL_DOWN };
static void slides_icon (Canvas &cv, int k, int x, int y, unsigned ink, bool off)
{
	unsigned dim = wk_mix (ink, C_BG, 150);
	if (off) ink = dim;
	int A = off ? 110 : 255;
	unsigned acc = off ? dim : 0x2E6E80, peach = off ? dim : 0xF0A86E, blue = off ? dim : 0x6EA0D2, green = off ? dim : 0x4E9E5C, red = off ? dim : 0xC84A40;
	VPath p; int X = V (x), Y = V (y);
	auto slide = [&] (int sx, int sy, int w, int h) {
		p.clear (); p.rect (X + V (sx), Y + V (sy), V (w), V (h)); p.fill (cv, 0xFFFFFF, A);
		p.clear (); int f[8] = { X + V (sx), Y + V (sy), X + V (sx + w), Y + V (sy), X + V (sx + w), Y + V (sy + h), X + V (sx), Y + V (sy + h) }; p.polyline (f, 4, 18, true); p.fill (cv, ink);
	};
	auto badge = [&] (unsigned c, bool plus) {
		p.clear (); p.circle (X + V (15), Y + V (15), V (4) + 8); p.fill (cv, c, A);
		p.clear (); p.rect (X + V (13), Y + V (15) - 10, V (4) + 4, 20); if (plus) p.rect (X + V (15) - 10, Y + V (13), 20, V (4) + 4); p.fill (cv, 0xFFFFFF);
	};
	switch (k)
	{
	case SL_NEWSLIDE: slide (1, 3, 15, 11); p.clear (); p.rect (X + V (3), Y + V (5), V (6), 30); p.fill (cv, acc); p.clear (); p.rect (X + V (3), Y + V (8), V (9), 20); p.rect (X + V (3), Y + V (11), V (7), 20); p.fill (cv, dim); badge (green, true); break;
	case SL_LAYOUT: slide (1, 3, 18, 14); p.clear (); p.rect (X + V (3), Y + V (5), V (14), V (3)); p.fill (cv, acc); p.clear (); p.rect (X + V (3), Y + V (10), V (6), V (5)); p.fill (cv, wk_mix (acc, 0xFFFFFF, 140), A); p.clear (); p.rect (X + V (11), Y + V (10), V (6), V (5)); p.fill (cv, wk_mix (peach, 0xFFFFFF, 80), A); break;
	case SL_DUPSLIDE: slide (6, 2, 13, 10); slide (1, 7, 13, 10); p.clear (); p.rect (X + V (3), Y + V (9), V (5), 30); p.fill (cv, acc); break;
	case SL_DELSLIDE: slide (1, 3, 15, 11); badge (red, false); break;
	case SL_TEXTBOX:
		slide (2, 2, 16, 16);
		wr::icon_letter (cv, x + 10, y + 15, 'A', wr::g_icFam >= 0 ? wr::g_icFam : fnt::find ("DejaVu Sans"), fnt::BOLD, 12, ink);
		for (int c = 0; c < 4; c++) { p.clear (); p.rect (X + V (c & 1 ? 17 : 1), Y + V (c & 2 ? 17 : 1), V (2), V (2)); p.fill (cv, acc); }
		break;
	case SL_SHAPES:
		p.clear (); p.rect (X + V (1), Y + V (8), V (10), V (10)); p.fill (cv, blue, A);
		p.clear (); p.circle (X + V (14), Y + V (13), V (5)); p.fill (cv, peach, A);
		{ int t[6] = { X + V (9), Y + V (1), X + V (15), Y + V (11), X + V (3), Y + V (11) }; p.clear (); p.poly (t, 3); p.fill (cv, green, A * 9 / 10); }
		break;
	case SL_LINE: case SL_ARROW:
		p.clear (); p.line (X + V (3), Y + V (17), X + V (k == SL_ARROW ? 14 : 17), Y + V (k == SL_ARROW ? 6 : 3), 30); p.fill (cv, ink);
		if (k == SL_ARROW) { p.clear (); p.arrowHead (X + V (17), Y + V (3), 45, V (6), V (4)); p.fill (cv, ink); }
		p.clear (); p.circle (X + V (3), Y + V (17), V (2)); p.fill (cv, acc);
		break;
	case SL_PICTURE: slide (1, 3, 18, 14); { int m[6] = { X + V (2), Y + V (16), X + V (8), Y + V (8), X + V (14), Y + V (16) }; p.clear (); p.poly (m, 3); p.fill (cv, green, A); } p.clear (); p.circle (X + V (14), Y + V (7), V (2)); p.fill (cv, peach, A); break;
	case SL_TABLE:
		slide (1, 3, 18, 14);
		p.clear (); p.rect (X + V (1), Y + V (3), V (18), V (4)); p.fill (cv, acc, A);
		p.clear (); p.rect (X + V (7), Y + V (3), 16, V (14)); p.rect (X + V (13), Y + V (3), 16, V (14)); p.rect (X + V (1), Y + V (11), V (18), 16); p.fill (cv, ink);
		break;
	case SL_CHART:
		p.clear (); p.rect (X + V (2), Y + V (18), V (17), 20); p.rect (X + V (2), Y + V (2), 20, V (17)); p.fill (cv, ink);
		p.clear (); p.rect (X + V (5), Y + V (10), V (3), V (8)); p.fill (cv, acc, A);
		p.clear (); p.rect (X + V (10), Y + V (5), V (3), V (13)); p.fill (cv, peach, A);
		p.clear (); p.rect (X + V (15), Y + V (8), V (3), V (10)); p.fill (cv, blue, A);
		break;
	case SL_SHOW: { int t[6] = { X + V (5), Y + V (3), X + V (17), Y + V (10), X + V (5), Y + V (17) }; p.clear (); p.poly (t, 3); p.fill (cv, off ? dim : 0xFFFFFF); break; }
	case SL_FORWARD: case SL_FRONT:
		p.clear (); p.rect (X + V (1), Y + V (1), V (12), V (12)); p.fill (cv, wk_mix (acc, 0xFFFFFF, 140), A);
		p.clear (); p.rect (X + V (6), Y + V (6), V (12), V (12)); p.fill (cv, blue, A);
		if (k == SL_FRONT) { p.clear (); p.rect (X + V (9), Y + V (9), V (6), V (6)); p.fill (cv, 0xFFFFFF, A); }
		break;
	case SL_BACKWARD: case SL_BACK:
		p.clear (); p.rect (X + V (6), Y + V (6), V (12), V (12)); p.fill (cv, blue, A);
		p.clear (); p.rect (X + V (1), Y + V (1), V (12), V (12)); p.fill (cv, wk_mix (acc, 0xFFFFFF, 140), A);
		if (k == SL_BACK) { p.clear (); p.rect (X + V (4), Y + V (4), V (6), V (6)); p.fill (cv, 0xFFFFFF, A); }
		break;
	case SL_AL_LEFT: case SL_AL_CENTER: case SL_AL_RIGHT:
	{
		int lx = k == SL_AL_LEFT ? 2 : k == SL_AL_CENTER ? 10 : 18;
		p.clear (); p.rect (X + V (lx) - 10, Y + V (1), 20, V (18)); p.fill (cv, ink);
		int w1 = 13, w2 = 8;
		int x1 = k == SL_AL_LEFT ? 3 : k == SL_AL_CENTER ? 10 - w1 / 2 : 17 - w1, x2 = k == SL_AL_LEFT ? 3 : k == SL_AL_CENTER ? 10 - w2 / 2 : 17 - w2;
		p.clear (); p.rect (X + V (x1), Y + V (4), V (w1), V (5)); p.fill (cv, blue, A);
		p.clear (); p.rect (X + V (x2), Y + V (11), V (w2), V (5)); p.fill (cv, peach, A);
		break;
	}
	case SL_AL_TOP: case SL_AL_MIDDLE: case SL_AL_BOTTOM:
	{
		int ly = k == SL_AL_TOP ? 2 : k == SL_AL_MIDDLE ? 10 : 18;
		p.clear (); p.rect (X + V (1), Y + V (ly) - 10, V (18), 20); p.fill (cv, ink);
		int h1 = 13, h2 = 8;
		int y1 = k == SL_AL_TOP ? 3 : k == SL_AL_MIDDLE ? 10 - h1 / 2 : 17 - h1, y2 = k == SL_AL_TOP ? 3 : k == SL_AL_MIDDLE ? 10 - h2 / 2 : 17 - h2;
		p.clear (); p.rect (X + V (4), Y + V (y1), V (5), V (h1)); p.fill (cv, blue, A);
		p.clear (); p.rect (X + V (11), Y + V (y2), V (5), V (h2)); p.fill (cv, peach, A);
		break;
	}
	case SL_DIST_H: case SL_DIST_V:
		for (int i = 0; i < 3; i++) { p.clear (); if (k == SL_DIST_H) p.rect (X + V (1 + i * 7), Y + V (5), V (4), V (10)); else p.rect (X + V (5), Y + V (1 + i * 7), V (10), V (4)); p.fill (cv, i == 1 ? peach : blue, A); }
		break;
	case SL_REMOVE: p.clear (); p.line (X + V (5), Y + V (5), X + V (15), Y + V (15), 40); p.line (X + V (15), Y + V (5), X + V (5), Y + V (15), 40); p.fill (cv, red); break;
	case SL_UP: case SL_DOWN:
		p.clear (); p.line (X + V (10), Y + V (k == SL_UP ? 16 : 4), X + V (10), Y + V (k == SL_UP ? 8 : 12), 34); p.arrowHead (X + V (10), Y + V (k == SL_UP ? 3 : 17), k == SL_UP ? 90 : -90, V (7), V (5)); p.fill (cv, ink);
		break;
	case SL_SPACING:
		for (int i = 0; i < 3; i++) { p.clear (); p.rect (X + V (8), Y + V (3 + i * 6), V (11), 26); p.fill (cv, ink); }
		p.clear (); p.rect (X + V (3) - 12, Y + V (3), 24, V (14)); p.arrowHead (X + V (3), Y + V (2), 90, V (3), V (3)); p.arrowHead (X + V (3), Y + V (18), -90, V (3), V (3)); p.fill (cv, acc);
		break;
	case SL_ANCHOR_T: case SL_ANCHOR_M: case SL_ANCHOR_B:
	{
		int ty = k == SL_ANCHOR_T ? 3 : k == SL_ANCHOR_M ? 8 : 13;
		slide (1, 1, 18, 18);
		p.clear (); p.rect (X + V (5), Y + V (ty), V (10), 26); p.rect (X + V (5), Y + V (ty + 3), V (8), 26); p.fill (cv, acc);
		break;
	}
	}
}

// ---- the rows ----------------------------------------------------------------------------------------------------------
enum { R_SECTION, R_PICK, R_COLOR, R_SPIN, R_CHECK, R_BUTTONS, R_THEMES, R_EFFECTS, R_BUTTON, R_INFO };
struct SRow
{
	int type, id;
	char label[40], value[64];
	unsigned color; bool autoColor;
	double num, step, lo, hi; int dec; const char *unit;
	bool on;
	int nbtn; int icon[8]; bool btnOn[8];
	bool half;				// shares the line with the row before (its right half)
	int y, h, x, w;				// placed by layout ()
};
// The ids of the rows (sidebar_act's)
enum {
	I_LAYOUT = 1, I_THEME, I_FONT_MAJOR, I_FONT_MINOR, I_BG_TYPE, I_BG_C1, I_BG_C2, I_BG_ANGLE, I_MASTER_OBJ, I_SIZE, I_HIDDEN, I_RESET, I_FOOTER, I_NUMBER,
	I_FILL_TYPE, I_FILL_C1, I_FILL_C2, I_FILL_ANGLE, I_FILL_ALPHA, I_LINE_TYPE, I_LINE_C, I_LINE_W, I_HEAD0, I_HEAD1, I_RADIUS, I_SHADOW, I_SHAPE,
	I_X, I_Y, I_W, I_H, I_ROT, I_ORDER, I_ALIGN, I_TBL_HEAD, I_TBL_BAND, I_TBL_ROWS, I_CHART_TYPE, I_CHART_DATA, I_CHART_LEGEND, I_CHART_LABELS, I_CHART_TITLE,
	I_FONT, I_SIZE_T, I_STYLE, I_COLOR, I_ALIGN_T, I_BULLET, I_LEVEL, I_SPACING, I_BEFORE, I_AFTER, I_ANCHOR, I_FIT, I_WRAP, I_FLIP,
	I_TR_TYPE, I_TR_DIR, I_TR_DUR, I_TR_ADV, I_TR_AFTER, I_TR_ALL, I_FX_LIST, I_FX_ADD, I_FX_TOOLS, I_FX_EFFECT, I_FX_START, I_FX_DELAY, I_FX_DUR, I_FX_DIR, I_FX_PARA, I_FX_PLAY,
	I_CROP, I_MASTER_VIEW, I_PH_TITLE, I_PH_TEXT, I_PH_PIC, I_LAYOUT_NAME
};
static const char *const FILL_NAMES[] = { "None", "Solid", "Gradient" };
static const char *const LINE_NAMES[] = { "None", "Solid", "Dashes", "Dots" };
static const char *const HEAD_NAMES[] = { "None", "Arrow", "Open arrow", "Dot" };
static const char *const BULLET_NAMES[] = { "None", "Bullets", "Numbers" };
static const char *const ANCHOR_NAMES[] = { "Top", "Middle", "Bottom" };
static const char *const FIT_NAMES[] = { "None", "Shrink the text", "Grow the box" };
static const char *const DIR_NAMES[] = { "-", "From the left", "From the right", "From the top", "From the bottom" };
static const char *const START_NAMES[] = { "On click", "With previous", "After previous" };
static const char *const FX_NAMES[FX_COUNT] = { "Appear", "Fade", "Fly in", "Wipe", "Zoom", "Float", "Grow", "Pulse", "Spin", "Colour" };
static const char *const CLASS_NAMES[] = { "Entrance", "Emphasis", "Exit" };
static const char *const CHART_NAMES[CH_COUNT] = { "Columns", "Bars", "Lines", "Pie", "Area" };
static const char *const SHAPE_NAMES[SH_COUNT] = { "Rectangle", "Rounded rectangle", "Ellipse", "Triangle", "Right triangle", "Diamond", "Pentagon",
	"Hexagon", "Octagon", "Star (5)", "Star (4)", "Heart", "Arrow right", "Arrow left", "Arrow up", "Arrow down", "Arrow left-right", "Chevron",
	"Pentagon arrow", "Callout", "Cloud", "Plus", "Parallelogram", "Trapezoid", "Can", "Donut", "Document", "Terminator" };
static const char *const SIZE_NAMES[] = { "16:9 widescreen", "16:10", "4:3 standard", "A4 landscape" };
static const int SIZE_W[] = { 28000, 28000, 28000, 29700 }, SIZE_H[] = { 15750, 17500, 21000, 21000 };
static const char *const ADV_NAMES[] = { "On click", "After a time" };

class Sidebar : public Widget
{
public:
	Vec<SRow> rows;
	int m_top, m_total, m_hot, m_edit; char m_buf[24];
	Sidebar (int l, int t, int w, int h) : Widget (l, t, w, h), m_top (0), m_total (0), m_hot (-1), m_edit (-1) { canFocus = true; m_buf[0] = 0; }
	unsigned bgColor () override { return PANEL_C; }

	// ---- building ---------------------------------------------------------------------------------------------
	SRow &add (int type, int id, const char *label)
	{
		SRow r; memset (&r, 0, sizeof r);
		r.type = type; r.id = id; scpy (r.label, label, sizeof r.label); r.step = 1; r.lo = -1e9; r.hi = 1e9; r.unit = "";
		rows.push (r);
		return rows[rows.n - 1];
	}
	void section (const char *t) { add (R_SECTION, 0, t); }
	void pick (int id, const char *label, const char *value) { SRow &r = add (R_PICK, id, label); scpy (r.value, value, sizeof r.value); }
	void color (int id, const char *label, unsigned c, bool half = false) { SRow &r = add (R_COLOR, id, label); r.color = c; r.half = half; }
	void spin (int id, const char *label, double v, double step, double lo, double hi, int dec, const char *unit, bool half = false)
	{ SRow &r = add (R_SPIN, id, label); r.num = v; r.step = step; r.lo = lo; r.hi = hi; r.dec = dec; r.unit = unit; r.half = half; }
	void check (int id, const char *label, bool on, bool half = false) { SRow &r = add (R_CHECK, id, label); r.on = on; r.half = half; }
	void buttons (int id, const char *label, const int *icons, int n, const bool *on = 0)
	{ SRow &r = add (R_BUTTONS, id, label); r.nbtn = n; for (int i = 0; i < n && i < 8; i++) { r.icon[i] = icons[i]; r.btnOn[i] = on && on[i]; } }
	void button (int id, const char *label) { add (R_BUTTON, id, label); }
	void info (const char *t) { add (R_INFO, 0, t); }

	void build ()
	{
		rows.clear ();
		Slide *s = cur_slide ();
		if (!s) return;
		switch (g_sbTab)
		{
		case SB_SLIDE: build_slide (*s); break;
		case SB_SHAPE: build_shape (); break;
		case SB_TEXT: build_text (); break;
		case SB_ANIM: build_anim (*s); break;
		}
		layout ();
	}
	void build_slide (Slide &s)
	{
		if (g_master) { build_master (s); return; }
		section ("Layout");
		pick (I_LAYOUT, "", LAYOUT_NAMES[s.layout]);
		button (I_RESET, "Reset to the layout");
		button (I_MASTER_VIEW, "Master and layouts...");
		section ("Theme");
		add (R_THEMES, I_THEME, "");
		pick (I_FONT_MAJOR, "Headings", g_deck.theme.major);
		pick (I_FONT_MINOR, "Body", g_deck.theme.minor);
		section ("Background");
		Fill bg = s.bg.type == FILL_INHERIT ? g_deck.masterBg : s.bg;
		pick (I_BG_TYPE, "", s.bg.type == FILL_INHERIT ? "The theme's" : bg.type == FILL_GRADIENT ? "Gradient" : "Solid colour");
		if (s.bg.type != FILL_INHERIT)
		{
			color (I_BG_C1, bg.type == FILL_GRADIENT ? "From" : "Colour", bg.c1);
			if (bg.type == FILL_GRADIENT) { color (I_BG_C2, "To", bg.c2, true); spin (I_BG_ANGLE, "Angle", bg.angle, 15, 0, 359, 0, "\xB0"); }
		}
		check (I_MASTER_OBJ, "The master's objects", s.masterObjects);
		section ("On every slide");
		check (I_NUMBER, "Slide number", g_deck.number);
		check (I_FOOTER, "Footer", g_deck.footer && g_deck.footerText[0]);
		section ("This slide");
		check (I_HIDDEN, "Hidden in the show", s.hidden);
		section ("Slide size");
		int k = 0; for (int i = 0; i < 4; i++) if (g_deck.sw == SIZE_W[i] && g_deck.sh == SIZE_H[i]) k = i;
		pick (I_SIZE, "", SIZE_NAMES[k]);
	}
	// The master view's Slide tab: the master (its background, the theme) or a layout (its name, its placeholders)
	void build_master (Slide &s)
	{
		(void) s;
		if (g_cur == 0)
		{
			section ("The master");
			info ("Its objects: on every slide.");
			info ("The samples' formats are");
			info ("the styles all text follows.");
		}
		else
		{
			section ("Layout");
			pick (I_LAYOUT_NAME, "Name", g_deck.layout[g_cur - 1].name);
			info ("A text box drawn here, or");
			info ("a picture: a placeholder.");
			button (I_PH_TITLE, "Add a title placeholder");
			button (I_PH_TEXT, "Add a text placeholder");
			button (I_PH_PIC, "Add a picture placeholder");
		}
		button (I_MASTER_VIEW, "Close the master view");
		section ("Theme");
		add (R_THEMES, I_THEME, "");
		pick (I_FONT_MAJOR, "Headings", g_deck.theme.major);
		pick (I_FONT_MINOR, "Body", g_deck.theme.minor);
		if (g_cur == 0)
		{
			section ("Background");
			const Fill &bg = g_deck.masterBg;
			pick (I_BG_TYPE, "", bg.type == FILL_GRADIENT ? "Gradient" : "Solid colour");
			color (I_BG_C1, bg.type == FILL_GRADIENT ? "From" : "Colour", bg.c1);
			if (bg.type == FILL_GRADIENT) { color (I_BG_C2, "To", bg.c2, true); spin (I_BG_ANGLE, "Angle", bg.angle, 15, 0, 359, 0, "\xB0"); }
		}
		section ("On every slide");
		check (I_NUMBER, "Slide number", g_deck.number);
		check (I_FOOTER, "Footer", g_deck.footer && g_deck.footerText[0]);
	}
	void build_shape ()
	{
		Object *o = g_sel.n ? obj_of (g_sel[0]) : 0;
		if (!o) { info ("Select an object on the slide"); info ("(a click; Shift: several)."); return; }
		if (o->kind != OB_LINE && o->kind != OB_TABLE && o->kind != OB_CHART)
		{
			section ("Fill");
			int ft = o->fill.type == FILL_GRADIENT ? 2 : o->fill.type == FILL_SOLID ? 1 : 0;
			pick (I_FILL_TYPE, "", FILL_NAMES[ft]);
			if (ft) color (I_FILL_C1, ft == 2 ? "From" : "Colour", o->fill.c1);
			if (ft == 2) { color (I_FILL_C2, "To", o->fill.c2, true); spin (I_FILL_ANGLE, "Angle", o->fill.angle, 15, 0, 359, 0, "\xB0"); }
			if (ft) spin (I_FILL_ALPHA, "Transparency", (255 - o->fill.alpha) * 100 / 255, 5, 0, 100, 0, " %");
		}
		if (o->kind != OB_TABLE && o->kind != OB_CHART)
		{
			section ("Line");
			pick (I_LINE_TYPE, "", LINE_NAMES[(int) o->line.type]);
			if (o->line.type != LN_NONE) { color (I_LINE_C, "Colour", o->line.color); spin (I_LINE_W, "Width", o->line.width / 35.278, 0.25, 0.25, 20, 2, " pt", true); }
			if (o->kind == OB_LINE) { pick (I_HEAD0, "Start", HEAD_NAMES[(int) o->line.head0]); pick (I_HEAD1, "End", HEAD_NAMES[(int) o->line.head1]); }
		}
		if (o->kind == OB_SHAPE || o->kind == OB_PICTURE)
		{
			section (o->kind == OB_PICTURE ? "Picture" : "Shape");
			pick (I_SHAPE, "", SHAPE_NAMES[(int) o->shape]);
			if (o->shape == SH_ROUND) spin (I_RADIUS, "Corners", o->radius / 10, 5, 0, 50, 0, " %");
			if (o->kind == OB_PICTURE) spin (I_CROP, "Crop (sides)", o->crop[0] / 10, 2, 0, 45, 0, " %");
		}
		if (o->kind != OB_LINE) check (I_SHADOW, "Shadow", o->shadow);
		if (o->kind == OB_TABLE && o->tbl)
		{
			section ("Table");
			check (I_TBL_HEAD, "Heading row", o->tbl->header);
			check (I_TBL_BAND, "Banded rows", o->tbl->banded);
			int ic[4] = { 0, 0, 0, 0 }; (void) ic;
			pick (I_TBL_ROWS, "Rows, columns", "Insert or delete...");
		}
		if (o->kind == OB_CHART && o->chart)
		{
			section ("Chart");
			pick (I_CHART_TYPE, "Type", CHART_NAMES[o->chart->type]);
			check (I_CHART_LEGEND, "Legend", o->chart->legend);
			check (I_CHART_LABELS, "Values on the bars", o->chart->labels, true);
			button (I_CHART_DATA, "Edit the data...");
		}
		section ("Position and size (cm)");
		spin (I_X, "X", o->x / 1000.0, 0.1, -50, 100, 2, ""); spin (I_Y, "Y", o->y / 1000.0, 0.1, -50, 100, 2, "", true);
		spin (I_W, "W", o->w / 1000.0, 0.1, 0, 100, 2, ""); spin (I_H, "H", o->h / 1000.0, 0.1, 0, 100, 2, "", true);
		if (o->kind != OB_LINE) spin (I_ROT, "Rotation", o->rot, 15, 0, 359, 0, "\xB0");
		if (o->kind != OB_LINE && o->kind != OB_TABLE) { bool fl[2] = { o->flipH, o->flipV }; static const int fi[2] = { SL_DIST_H, SL_DIST_V }; (void) fl; (void) fi; check (I_FLIP, "Flipped", o->flipH, false); }
		section ("Arrange");
		static const int ord[4] = { SL_FRONT, SL_FORWARD, SL_BACKWARD, SL_BACK };
		buttons (I_ORDER, "", ord, 4);
		static const int al[8] = { SL_AL_LEFT, SL_AL_CENTER, SL_AL_RIGHT, SL_AL_TOP, SL_AL_MIDDLE, SL_AL_BOTTOM, SL_DIST_H, SL_DIST_V };
		buttons (I_ALIGN, "", al, g_sel.n >= 3 ? 8 : 6);
	}
	void build_text ()
	{
		Object *o = g_edit ? obj_of (g_edit) : g_sel.n ? obj_of (g_sel[0]) : 0;
		if (!o || o->kind == OB_PICTURE || o->kind == OB_LINE || o->kind == OB_CHART) { info ("Select a text, a shape or a table,"); info ("or click into its text."); return; }
		CharFmt f = shown_format ();
		ParaFmt pf = shown_para ();
		section ("Character");
		pick (I_FONT, "", g_deck.font_name (f.font));
		spin (I_SIZE_T, "Size", f.size / 10.0, 2, 4, 400, 1, " pt");
		color (I_COLOR, "Colour", f.color, true);
		int st[6] = { 300, 301, 302, 303, 304, 305 }; bool on[6] = { (f.flags & CF_BOLD) != 0, (f.flags & CF_ITALIC) != 0, (f.flags & CF_UNDER) != 0, (f.flags & CF_STRIKE) != 0, (f.flags & CF_SUPER) != 0, (f.flags & CF_SUB) != 0 };
		buttons (I_STYLE, "", st, 6, on);
		section ("Paragraph");
		int als[4] = { 306, 307, 308, 309 }; bool alo[4] = { pf.align == AL_LEFT, pf.align == AL_CENTER, pf.align == AL_RIGHT, pf.align == AL_JUSTIFY };
		buttons (I_ALIGN_T, "", als, 4, alo);
		pick (I_BULLET, "List", BULLET_NAMES[iclamp (pf.bullet, 0, 2)]);
		spin (I_LEVEL, "Level", pf.level + 1, 1, 1, 5, 0, "", true);
		spin (I_SPACING, "Line spacing", pf.spacing / 100.0, 0.1, 0.5, 3, 2, "");
		spin (I_BEFORE, "Before", pf.before / 10.0, 2, 0, 200, 0, " pt"); spin (I_AFTER, "After", pf.after / 10.0, 2, 0, 200, 0, " pt", true);
		if (!o->tbl)
		{
			section ("Text box");
			int an[3] = { SL_ANCHOR_T, SL_ANCHOR_M, SL_ANCHOR_B }; bool ano[3] = { o->tb.anchor == AN_TOP, o->tb.anchor == AN_MIDDLE, o->tb.anchor == AN_BOTTOM };
			buttons (I_ANCHOR, "", an, 3, ano);
			pick (I_FIT, "Autofit", FIT_NAMES[(int) o->tb.fit]);
			check (I_WRAP, "Wrap the text", o->tb.wrap);
		}
	}
	void build_anim (Slide &s)
	{
		section ("Transition to this slide");
		pick (I_TR_TYPE, "", TR_NAMES[(int) s.tr.type]);
		if (s.tr.type == TR_PUSH || s.tr.type == TR_WIPE || s.tr.type == TR_COVER || s.tr.type == TR_UNCOVER) pick (I_TR_DIR, "", DIR_NAMES[iclamp (s.tr.dir, 1, 4)]);
		if (s.tr.type != TR_NONE) spin (I_TR_DUR, "Duration", s.tr.dur / 1000.0, 0.1, 0.1, 5, 1, " s");
		pick (I_TR_ADV, "Next slide", ADV_NAMES[s.tr.after >= 0]);
		if (s.tr.after >= 0) spin (I_TR_AFTER, "After", s.tr.after / 1000.0, 1, 0, 600, 0, " s", true);
		button (I_TR_ALL, "Apply to every slide");
		section ("Effects on this slide");
		add (R_EFFECTS, I_FX_LIST, "");
		pick (I_FX_ADD, "", g_sel.n ? "Add an effect..." : "(select an object to add one)");
		static const int tools[3] = { SL_REMOVE, SL_UP, SL_DOWN };
		buttons (I_FX_TOOLS, "", tools, 3);
		if (g_animSel >= 0 && g_animSel < s.anim.n)
		{
			Anim &a = s.anim[g_animSel];
			Object *o = s.by_id (a.obj);
			char t[64]; snprintf (t, sizeof t, "%s  \xB7  %s", FX_NAMES[(int) a.fx], o ? (o->name[0] ? o->name : "Object") : "?");
			section (t);
			pick (I_FX_EFFECT, "Effect", FX_NAMES[(int) a.fx]);
			pick (I_FX_START, "Start", START_NAMES[(int) a.start]);
			if (a.fx == FX_FLY || a.fx == FX_WIPE) pick (I_FX_DIR, "From", DIR_NAMES[iclamp (a.dir, 1, 4)]);
			spin (I_FX_DELAY, "Delay", a.delay / 1000.0, 0.1, 0, 30, 1, " s");
			spin (I_FX_DUR, "Duration", a.dur / 1000.0, 0.1, 0.1, 10, 1, " s", true);
			if (o && !o->tb.empty () && o->tb.p.n > 1) check (I_FX_PARA, "By paragraph", a.byPara);
		}
		button (I_FX_PLAY, "\xBB  Play the slide from here");
	}
	void layout ()
	{
		int y = 44;
		int W = width - 20;
		for (int i = 0; i < rows.n; i++)
		{
			SRow &r = rows[i];
			int h = r.type == R_SECTION ? 26 : r.type == R_THEMES ? 3 * 60 + 6 : r.type == R_EFFECTS ? imax (1, cur_slide ()->anim.n) * 36 + 10 : r.type == R_INFO ? 20 : 28;
			if (r.half && i > 0)
			{
				// the row before keeps the left half
				SRow &p = rows[i - 1];
				p.w = W / 2 - 4; r.x = 10 + W / 2 + 4; r.w = W / 2 - 4; r.y = p.y; r.h = p.h;
				continue;
			}
			r.x = 10; r.w = W; r.y = y; r.h = h;
			y += h + (r.type == R_SECTION ? 0 : 4);
		}
		m_total = y + 10;
	}

	// ---- drawing ------------------------------------------------------------------------------------------------
	void onDraw () override
	{
		canvas.clear (PANEL_C);
		wk_etch_v (canvas, 0, 0, height, PANEL_C);
		int maxTop = imax (0, m_total - height); if (m_top > maxTop) m_top = maxTop;
		for (int i = 0; i < rows.n; i++) draw_row (rows[i], i);
		// the tabs (over the rows: they stay)
		canvas.fillRect (1, 0, width - 1, 40, PANEL_C);
		static const char *T[4] = { "Slide", "Shape", "Text", "Animate" };
		int tw = (width - 16) / 4;
		wk_sunken (canvas, 8, 8, width - 16, 26, 6, C_FIELD);
		for (int k = 0; k < 4; k++)
		{
			if (k == g_sbTab) wk_rbox (canvas, 10 + k * tw, 10, tw - 4, 22, 5, HANDLE_C, HANDLE_C);
			wk_text_c (canvas, 8 + k * tw, 8, tw, 26, T[k], k == g_sbTab ? 0xFFFFFF : C_FIELD_TEXT);
		}
		if (m_total > height) { WkThumb t = wk_thumb (m_total - 40, height - 40, m_top, height - 48); wk_draw_vscroll (canvas, width - WK_SBW - 2, 44, WK_SBW, height - 48, t, PANEL_C); }
	}
	void field_box (int x, int y, int w, int h, bool hot) { wk_sunken (canvas, x, y, w, h, 4, C_FIELD, hot); }
	void draw_row (SRow &r, int i)
	{
		int y = r.y - m_top;
		if (y + r.h < 40 || y > height) return;
		bool hot = i == m_hot;
		unsigned ink = C_TEXT, dim = wk_mix (PANEL_C, C_TEXT, 150);
		int lx = r.x, cx = r.x, cw = r.w;
		if (r.label[0] && r.type != R_SECTION && r.type != R_CHECK && r.type != R_BUTTON && r.type != R_INFO)
		{
			int lw = r.half || r.w < 150 ? wk_text_w (r.label) + 8 : 96;
			wk_text_l (canvas, lx, y, r.h, r.label, dim);
			cx += lw; cw -= lw;
		}
		switch (r.type)
		{
		case R_SECTION:
			canvas.fillRect (1, y + 2, width - 2, r.h - 4, wk_mix (PANEL_C, 0xFFFFFF, 90));
			wk_glyph (canvas, WKG_CHEV_DOWN, 14, y + r.h / 2, 7, dim);
			wk_text_l (canvas, 24, y, r.h, L1 (r.label), ink, 1);
			break;
		case R_INFO: wk_text_l (canvas, lx, y, r.h, r.label, dim); break;
		case R_PICK:
			wk_raised (canvas, cx, y + 2, cw, r.h - 4, 5, C_BUTTON, hot ? WK_HOT : WK_NORMAL);
			{ char v[64]; scpy (v, L1 (r.value), sizeof v); while (wk_text_w (v) > cw - 30 && strlen (v) > 3) { int n = (int) strlen (v); v[n - 4] = '.'; v[n - 3] = '.'; v[n - 2] = 0; }
			  wk_text_l (canvas, cx + 8, y + 2, r.h - 4, v, C_BUTTON_TEXT); }
			wk_glyph (canvas, WKG_CHEV_DOWN, cx + cw - 12, y + r.h / 2, 8, C_BUTTON_TEXT);
			break;
		case R_COLOR:
		{
			int w = imin (cw, 54);
			wk_raised (canvas, cx, y + 2, w, r.h - 4, 5, C_BUTTON, hot ? WK_HOT : WK_NORMAL);
			unsigned c = g_deck.rgb (r.color);
			canvas.fillRect (cx + 5, y + 7, w - 22, r.h - 14, r.color == AUTO ? 0xFFFFFF : c);
			canvas.frameRect (cx + 5, y + 7, w - 22, r.h - 14, wk_mix (c, 0, 80));
			wk_glyph (canvas, WKG_CHEV_DOWN, cx + w - 9, y + r.h / 2, 7, C_BUTTON_TEXT);
			break;
		}
		case R_SPIN:
		{
			field_box (cx, y + 2, cw, r.h - 4, m_edit == i);
			char t[48];
			if (m_edit == i) snprintf (t, sizeof t, "%s_", m_buf);
			else snprintf (t, sizeof t, "%.*f%s", r.dec, r.num, r.unit);
			wk_text_l (canvas, cx + 6, y + 2, r.h - 4, t, C_FIELD_TEXT);
			wk_glyph (canvas, WKG_CHEV_UP, cx + cw - 10, y + r.h / 2 - 5, 7, dim);
			wk_glyph (canvas, WKG_CHEV_DOWN, cx + cw - 10, y + r.h / 2 + 5, 7, dim);
			break;
		}
		case R_CHECK:
			wk_check_mark (canvas, lx, y + (r.h - 16) / 2, 16, r.on, hot ? WK_HOT : WK_NORMAL);
			wk_text_l (canvas, lx + 24, y, r.h, r.label, ink);
			break;
		case R_BUTTON:
			wk_raised (canvas, lx, y + 2, r.w, r.h - 4, 5, C_BUTTON, hot ? WK_HOT : WK_NORMAL);
			wk_text_c (canvas, lx, y + 2, r.w, r.h - 4, r.label, C_BUTTON_TEXT);
			break;
		case R_BUTTONS:
			for (int b = 0; b < r.nbtn; b++)
			{
				int bx = cx + b * 34;
				if (r.btnOn[b]) wk_rbox (canvas, bx, y + 1, 30, r.h - 2, 4, wk_mix (PANEL_C, C_ACCENT, 70), wk_mix (PANEL_C, C_ACCENT, 70));
				else wk_raised (canvas, bx, y + 1, 30, r.h - 2, 4, wk_mix (PANEL_C, 0xFFFFFF, 90), WK_NORMAL);
				ss::sheet_icon (canvas, r.icon[b] >= 300 ? text_icon (r.icon[b]) : r.icon[b], bx + 5, y + (r.h - 20) / 2, ink, false);
			}
			break;
		case R_THEMES:
		{
			int tw = (r.w - 10) / 2, th = 50;
			for (int k = 0; k < NTHEMES; k++)
			{
				int tx = r.x + (k % 2) * (tw + 10), ty = y + (k / 2) * 60;
				const ThemeDef &T = THEMES[k];
				bool cur = !strcmp (g_deck.theme.name, T.name);
				if (cur) wk_rbox (canvas, tx - 3, ty - 3, tw + 6, th + 6, 5, HANDLE_C, HANDLE_C);
				canvas.fillRect (tx, ty, tw, th - 14, 0xFFFFFF); canvas.frameRect (tx, ty, tw, th - 14, 0xC0B4AC);
				canvas.fillRect (tx, ty, 4, th - 14, T.acc1);
				canvas.fillRect (tx + 10, ty + 7, tw / 2, 5, T.dk2); canvas.fillRect (tx + 10, ty + 15, 18, 3, T.acc2);
				canvas.fillRect (tx + 10, ty + 23, tw * 6 / 10, 2, 0xD2CEC8);
				wk_text_l (canvas, tx + 2, ty + th - 14, 14, L1 (T.name), cur ? 0xFFFFFF : ink);
			}
			break;
		}
		case R_EFFECTS:
		{
			Slide *s = cur_slide ();
			wk_sunken (canvas, r.x, y, r.w, r.h - 4, 5, C_FIELD);
			if (!s->anim.n) { wk_text_l (canvas, r.x + 10, y + 4, 26, "No effect yet.", dim); break; }
			int n = 0;
			for (int k = 0; k < s->anim.n; k++)
			{
				Anim &a = s->anim[k];
				int ry = y + 4 + k * 36;
				if (k == g_animSel) wk_rbox (canvas, r.x + 3, ry, r.w - 6, 34, 4, wk_mix (C_FIELD, HANDLE_C, 70), wk_mix (C_FIELD, HANDLE_C, 70));
				if (a.start == ST_CLICK || k == 0) n++;
				char num[8]; snprintf (num, sizeof num, "%d", n);
				if (a.start == ST_CLICK || k == 0) wk_text_l (canvas, r.x + 8, ry, 18, num, dim, 1);
				unsigned dc = a.cls == AC_ENTRANCE ? 0x3C9650 : a.cls == AC_EMPHASIS ? 0xD2A028 : 0xBE463C;
				VPath p; p.circle (V (r.x + 30), V (ry + 9), V (5)); p.fill (canvas, dc);
				Object *o = s->by_id (a.obj);
				char t[80]; snprintf (t, sizeof t, "%s  \xB7  %s", FX_NAMES[(int) a.fx], o ? (o->name[0] ? o->name : "Object") : "?");
				wk_text_l (canvas, r.x + 42, ry, 18, L1 (t), C_FIELD_TEXT, k == g_animSel ? 1 : 0);
				wk_text_l (canvas, r.x + 42, ry + 16, 16, START_NAMES[(int) a.start], dim);
				int bw = imin (r.w - 150, a.dur / 40);
				canvas.fillRect (r.x + r.w - 10 - imax (bw, 6), ry + 7, imax (bw, 6), 6, wk_mix (dc, 0xFFFFFF, 90));
			}
			break;
		}
		}
	}
	static int text_icon (int k)
	{
		switch (k)
		{
		case 300: return wr::IC_BOLD; case 301: return wr::IC_ITALIC; case 302: return wr::IC_UNDER; case 303: return wr::IC_STRIKE;
		case 304: return wr::IC_SUPER; case 305: return wr::IC_SUB; case 306: return wr::IC_LEFT; case 307: return wr::IC_CENTER;
		case 308: return wr::IC_RIGHT; case 309: return wr::IC_JUSTIFY; case 310: return wr::IC_CUT; case 311: return wr::IC_UNDO; case 312: return wr::IC_REDO;
		}
		return 0;
	}

	// ---- the mouse, the keys ----------------------------------------------------------------------------------------
	int row_at (int mx, int my)
	{
		for (int i = 0; i < rows.n; i++) { SRow &r = rows[i]; int y = r.y - m_top; if (my >= y && my < y + r.h && mx >= r.x && mx < r.x + r.w) return i; }
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (wheel && in) { m_top = imax (0, imin (m_total - height, m_top - wheel * 40)); invalidate (true); return true; }
		int hot = in && my >= 40 ? row_at (mx, my) : -1;
		if (hot != m_hot) { m_hot = hot; invalidate (true); }
		if (bl && in && !pressed) { pressed = true; return true; }
		if (!bl && pressed)
		{
			pressed = false;
			if (!in) return true;
			if (my < 40) { int tw = (width - 16) / 4; int k = (mx - 8) / tw; if (k >= 0 && k < 4) { g_sbTab = k; commit_edit (); m_top = 0; build (); invalidate (true); } return true; }
			if (hot >= 0) click (hot, mx, my);
			return true;
		}
		return in;
	}
	bool onKey (long k) override
	{
		if (m_edit < 0) return false;
		int n = (int) strlen (m_buf);
		if (k == 27) { m_edit = -1; invalidate (true); return true; }
		if (k == KEY_ENTER || k == KEY_TAB) { commit_edit (); return true; }
		if (k == KEY_BACKSPACE) { if (n) m_buf[n - 1] = 0; invalidate (true); return true; }
		if (((k >= '0' && k <= '9') || k == '.' || k == ',' || k == '-') && n < 12) { m_buf[n] = k == ',' ? '.' : (char) k; m_buf[n + 1] = 0; invalidate (true); return true; }
		return true;
	}
	void commit_edit ()
	{
		if (m_edit < 0 || m_edit >= rows.n) { m_edit = -1; return; }
		SRow r = rows[m_edit]; m_edit = -1;
		if (!m_buf[0]) { invalidate (true); return; }
		double v = strtod (m_buf, 0);
		if (v < r.lo) v = r.lo; if (v > r.hi) v = r.hi;
		act (r, 0, v);
	}
	void click (int i, int mx, int my)
	{
		SRow &r = rows[i];
		int y = r.y - m_top;
		int cx = r.x, cw = r.w;
		if (r.label[0] && r.type != R_SECTION && r.type != R_CHECK && r.type != R_BUTTON && r.type != R_INFO) { int lw = r.half || r.w < 150 ? wk_text_w (r.label) + 8 : 96; cx += lw; cw -= lw; }
		if (m_edit >= 0 && m_edit != i) commit_edit ();
		SRow rr = rows[i];			// (act may rebuild the rows)
		switch (rr.type)
		{
		case R_SPIN:
			if (mx >= cx + cw - 18) { double v = rr.num + (my < y + r.h / 2 ? rr.step : -rr.step); if (v < rr.lo) v = rr.lo; if (v > rr.hi) v = rr.hi; act (rr, 0, v); }
			else { m_edit = i; m_buf[0] = 0; setFocus (); invalidate (true); }
			break;
		case R_CHECK: act (rr, 0, rr.on ? 0 : 1); break;
		case R_BUTTON: act (rr, 0, 0); break;
		case R_BUTTONS: { int b = (mx - cx) / 34; if (b >= 0 && b < rr.nbtn) act (rr, b, 0); break; }
		case R_PICK: { int ax, ay; below (cx, y + r.h, &ax, &ay); pick_list (rr, ax, ay, cw); break; }
		case R_COLOR: { int ax, ay; below (cx, y + r.h, &ax, &ay); pick_color (rr, ax, ay); break; }
		case R_THEMES: { int tw = (r.w - 10) / 2; int k = ((my - y) / 60) * 2 + ((mx - r.x) / (tw + 10)); if (k >= 0 && k < NTHEMES) act (rr, k, 0); break; }
		case R_EFFECTS: { int k = (my - y - 4) / 36; Slide *s = cur_slide (); if (k >= 0 && k < s->anim.n) { g_animSel = k; build (); invalidate (true); } break; }
		}
	}
	void below (int x, int y, int *ax, int *ay)
	{
		*ax = x; *ay = y;
		for (Widget *w = this; w && w->parent; w = w->parent) { *ax += w->left; *ay += w->top; }
	}
	static const char *const *g_listItems; static int g_listN;
	static void list_row (Canvas &cv, int i, int x, int y, int, int h, unsigned ink) { wk_text_l (cv, x, y, h, g_listItems[i], ink); }
	static const char *font_item (int i) { return fnt::name (i); }
	static void font_row (Canvas &cv, int i, int x, int y, int w, int h, unsigned ink)
	{
		fnt::Font *f = fnt::get (i, 0, 15 * 64);
		if (!f) { wk_text_l (cv, x, y, h, fnt::name (i), ink); return; }
		int base = y + (h + ((f->ascent - f->descent) >> 6)) / 2;
		Canvas sub; sub.adopt (cv.px, x + w < cv.w ? x + w : cv.w, cv.h, cv.stride);
		fnt::draw_str (sub, f, x << 6, base, fnt::name (i), ink);
	}
	int list (int x, int y, int w, const char *const *items, int n, int sel)
	{
		g_listItems = items; g_listN = n;
		ListPopup lp (x, y, imax (w, 160), n, 26, sel, list_row);
		return lp.pick ();
	}
	void pick_list (SRow &r, int x, int y, int w);
	void pick_color (SRow &r, int x, int y);
	void act (const SRow &r, int sub, double v);
};
const char *const *Sidebar::g_listItems; int Sidebar::g_listN;

// The colours offered: the theme's ten (kept as the theme's: a new theme recolours them), then the office palette.
static unsigned g_palette[70];
static void palette_make ()
{
	ss::make_palettes ();
	static const int order[10] = { TC_LT1, TC_DK1, TC_LT2, TC_DK2, TC_ACC1, TC_ACC2, TC_ACC3, TC_ACC4, TC_ACC5, TC_ACC6 };
	for (int i = 0; i < 10; i++) g_palette[i] = g_deck.theme.col[order[i]];
	for (int i = 0; i < 60; i++) g_palette[10 + i] = ss::g_cols[i];
}
static unsigned palette_code (long c)
{
	static const int order[10] = { TC_LT1, TC_DK1, TC_LT2, TC_DK2, TC_ACC1, TC_ACC2, TC_ACC3, TC_ACC4, TC_ACC5, TC_ACC6 };
	if (c == (long) ss::AUTO) return AUTO;
	for (int i = 0; i < 10; i++) if ((unsigned) c == g_deck.theme.col[order[i]]) return THEME | order[i];
	return (unsigned) c & 0xFFFFFF;
}
void Sidebar::pick_color (SRow &r, int x, int y)
{
	palette_make ();
	ColorPopup p (x, y, g_palette, 70, 10, r.id == I_COLOR ? "Automatic" : "No Colour");
	long c = p.pick ();
	if (c == -1) return;
	unsigned code = palette_code (c);
	if (code == AUTO && r.id != I_COLOR) { SRow t = r; act (t, 1, 0); return; }		// (no colour: the fill / line off)
	SRow t = r; t.color = code; act (t, 0, 0);
}

static Sidebar *g_sidebar;

} // namespace sl

#endif
