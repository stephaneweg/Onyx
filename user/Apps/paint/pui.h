//
// pui.h -- Paint's controls (docs/paint: the mock-ups): the ribbon (Clipboard, Image -- Select and its
// kinds, Crop, Resize, Rotate --, Tools, Brushes, the Shapes' gallery, Colours), the options bar under
// it (what the current tool takes: sizes, opacity, hardness, the selection's mode and tolerance, the
// fill's colour / gradient / pattern, the text's font...), the layers' panel (the current layer's blend
// mode and opacity; each layer's eye, thumbnail, name, mode; New, Duplicate, Delete, Up, Down, Merge,
// Properties) and the status bar (the pointer, the selection, the picture's size, the layer; the grid,
// fit, the zoom's slider). Text in FreeType's DejaVu Sans (wtk's face).
//
// MIT licence (Onyx).
//
#ifndef _paint_pui_h
#define _paint_pui_h

#include "picons.h"

namespace pd {

using namespace wtk;

// The tones of the parts (from the theme's background).
static unsigned rib_bg () { return wk_mix (C_BG, 0xFFFFFF, 150); }
static unsigned opt_bg () { return wk_mix (C_BG, 0xFFFFFF, 80); }
static unsigned pan_bg () { return wk_mix (C_BG, 0xFFFFFF, 110); }
static void hot_box (Canvas &cv, int x, int y, int w, int h, unsigned bg, bool on, bool hot, bool down = false)
{
	if (on) { wk_rbox (cv, x, y, w, h, 5, wk_mix (bg, C_ACCENT, 56), wk_mix (bg, C_ACCENT, 56)); wk_rline (cv, x, y, w, h, 5, wk_mix (bg, C_ACCENT, 166)); }
	else if (hot || down) { wk_rbox (cv, x, y, w, h, 5, down ? wk_mix (bg, 0x000000, 20) : wk_mix (bg, 0xFFFFFF, 200), down ? wk_mix (bg, 0x000000, 20) : wk_mix (bg, 0xFFFFFF, 200)); wk_rline (cv, x, y, w, h, 5, wk_mix (bg, 0x000000, 50)); }
}
static void chevron (Canvas &cv, int cx, int cy, unsigned c) { wk_glyph (cv, WKG_CHEV_DOWN, cx, cy, 7, c); }
static void disc (Canvas &cv, int cx, int cy, int r, unsigned c) { VPath p; p.circle (V (cx), V (cy), V (r)); p.fill (cv, c); }
static void ring2 (Canvas &cv, int cx, int cy, int r, unsigned c, int w = 16) { VPath p; p.circle (V (cx), V (cy), V (r)); p.hole (V (cx), V (cy), V (r) - w); p.fill (cv, c); }
static void fmt_int (char *b, int v, const char *suffix)
{
	int n = 0; char t[12]; int j = 0; if (v < 0) { b[n++] = '-'; v = -v; }
	do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) b[n++] = t[--j];
	for (const char *s = suffix; s && *s; s++) b[n++] = *s;
	b[n] = 0;
}
// A slider: a track, its part up to the value in the accent, a knob.
static void slider_draw (Canvas &cv, int x, int y, int w, int h, int v, int lo, int hi, unsigned bg)
{
	int cy = y + h / 2, k = x + (int) ((long long) (v - lo) * (w - 1) / pmax (1, hi - lo));
	wk_rbox (cv, x, cy - 2, w, 4, 2, wk_mix (bg, 0x000000, 50), wk_mix (bg, 0x000000, 50));
	if (k > x) wk_rbox (cv, x, cy - 2, k - x + 2, 4, 2, C_ACCENT, C_ACCENT);
	disc (cv, k, cy, 7, wk_mix (bg, 0x000000, 90)); disc (cv, k, cy, 6, 0xFFFFFF);
}
static int slider_val (int mx, int x, int w, int lo, int hi) { return pclamp (lo + (int) ((long long) (mx - x) * (hi - lo) / pmax (1, w - 1)), lo, hi); }

// A brush's stroke, drawn small (the ribbon's button, the gallery).
static void brush_sample (Canvas &cv, int x, int y, int w, int h, int kind, unsigned col)
{
	unsigned char *cov = new unsigned char[(unsigned) w * h];
	for (int i = 0; i < w * h; i++) cov[i] = 0;
	float r = kind == BR_PENCIL ? 0.7f : kind == BR_AIR ? 5.5f : kind == BR_CALLIG ? 6 : kind >= BR_FIRST_PATTERN ? 6 : 3.4f;
	unsigned seed = 7;
	for (int i = 0; i <= 120; i++)
	{
		float t = i / 120.0f, cx = 4 + (w - 8) * t, cy = h / 2.0f + sinf (t * 6.2832f) * h * 0.26f;
		for (int yy = (int) (cy - r - 1); yy <= (int) (cy + r + 1); yy++)
			for (int xx = (int) (cx - r - 1); xx <= (int) (cx + r + 1); xx++)
			{
				if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
				float c;
				if (kind == BR_AIR) { seed = seed * 1103515245u + 12345u; c = ((seed >> 8) & 255) < 22 && (xx - cx) * (xx - cx) + (yy - cy) * (yy - cy) < r * r ? 0.9f : 0; }
				else c = Stroke::shape (kind == BR_PENCIL ? BR_BRUSH : kind, xx + 0.5f - cx, yy + 0.5f - cy, r, 0.3f);
				int ci = (int) (c * 255);
				if (kind == BR_CRAYON) { unsigned g = hash2 (xx, yy) & 255; ci = g < 90 ? 0 : ci; }
				if (kind >= BR_FIRST_PATTERN && !pat_on (kind, xx, yy)) ci = 0;
				unsigned char &v = cov[yy * w + xx];
				if (kind == BR_AIR) { if (ci) v = (unsigned char) pmin (255, v + 120); }
				else if (ci > v) v = (unsigned char) ci;
			}
	}
	int op = kind == BR_MARKER ? 140 : 255;
	for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (cov[j * w + i]) wk_blend_px (cv, x + i, y + j, col, cov[j * w + i] * op / 255);
	delete[] cov;
}
// A gradient's colours in a bar, over a checkerboard.
static void grad_draw (Canvas &cv, int x, int y, int w, int h, int gi)
{
	if (gi < 0 || gi >= g_ngrads) return;
	Gradient &g = g_grads[gi];
	grad_dyn (g, g_col1, g_col2);
	for (int i = 0; i < w; i++)
	{
		unsigned c = grad_at (g, w > 1 ? i / (float) (w - 1) : 0), a = c >> 24;
		for (int j = 0; j < h; j++)
		{
			unsigned ck = (((i >> 2) ^ (j >> 2)) & 1) ? 0xD6D6D6 : 0xFFFFFF;
			cv.pixel (x + i, y + j, a == 255 ? c & 0xFFFFFF : wk_mix (ck, c & 0xFFFFFF, (int) a + 1));
		}
	}
	cv.frameRect (x, y, w, h, 0x8C8480);
}
// The gradient's shapes' little pictures.
static void gshape_draw (Canvas &cv, int x, int y, int s, int kind)
{
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			float u = i / (float) (s - 1), v = j / (float) (s - 1), t;
			switch (kind)
			{
			case GS_BILINEAR: t = fabsf (u - 0.5f) * 2; break;
			case GS_RADIAL: t = sqrtf ((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2; break;
			case GS_SQUARE: t = fmaxf (fabsf (u - 0.5f), fabsf (v - 0.5f)) * 2; break;
			case GS_CONICAL: t = atan2f (v - 0.5f, u - 0.5f) / 6.2832f; t -= floorf (t); break;
			default: t = u;
			}
			cv.pixel (x + i, y + j, wk_mix (0x3C3860, 0xFAFAFA, (int) (fminf (1, t) * 256)));
		}
	cv.frameRect (x, y, s, s, 0x8C8480);
}

// ---- the ribbon ----------------------------------------------------------------------------------------------
enum { RIBBON_H = 96 };
enum { C_PASTE, C_CUT, C_COPY, C_UNDO, C_REDO, C_SELECT, C_CROP, C_RESIZE, C_ROTATE, C_TOOL, C_ADJUST, C_BRUSHES, C_SHAPE,
       C_COL1, C_COL2, C_PALETTE, C_CUSTOM, C_EDITCOL };
static void (*g_ribbonCmd) (int cmd, int arg, int btn, int x, int y);	// (x, y: where a popup opens, in the window)

static const unsigned PALETTE[20] = {
	0x000000, 0x7F7F7F, 0x880015, 0xED1C24, 0xFF7F27, 0xFFF200, 0x22B14C, 0x00A2E8, 0x3F48CC, 0xA349A4,
	0xFFFFFF, 0xC3C3C3, 0xB97A57, 0xFFAEC9, 0xFFC90E, 0xEFE4B0, 0xB5E61D, 0x99D9EA, 0x7092BE, 0xC8BFE7 };
static unsigned g_custom[10];					// (0xFF000000 | colour; 0: empty)
static int g_activeCol = 1;					// the palette sets colour 1 or 2

class Ribbon : public Widget
{
public:
	Ribbon (int l, int t, int w) : Widget (l, t, w, RIBBON_H), m_n (0), m_ng (0), m_hot (-1), m_down (-1), m_btn (0) {}
	unsigned bgColor () override { return rib_bg (); }
	void onDraw () override
	{
		unsigned bg = rib_bg ();
		layoutCells ();
		canvas.fillRect (0, 0, width, height, bg);
		canvas.fillRect (0, height - 1, width, 1, wk_mix (bg, 0x000000, 40));
		unsigned ink = wk_ink_for (bg), dim = wk_mix (bg, ink, 150);
		for (int g = 0; g < m_ng; g++)
		{
			wk_text_c (canvas, m_gx[g], height - 22, m_gw[g], 18, m_gname[g], dim);
			if (g < m_ng - 1) canvas.fillRect (m_gx[g] + m_gw[g] + 5, 10, 1, height - 22, wk_mix (bg, 0x000000, 40));
		}
		// the shapes' gallery: a white box
		canvas.fillRect (m_shX, 7, 5 * 24 + 4, 3 * 22 + 2, 0xFFFFFF); canvas.frameRect (m_shX, 7, 5 * 24 + 4, 3 * 22 + 2, wk_mix (bg, 0x000000, 60));
		for (int i = 0; i < m_n; i++) drawCell (i, ink, bg);
	}
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		int hot = cellAt (mx, my);
		if (hot != m_hot) { m_hot = hot; tip = hot >= 0 ? m_cell[hot].tip : 0; invalidate (true); }
		int btn = bl ? 1 : br ? 2 : 0;
		if (btn && m_down < 0 && hot >= 0) { m_down = hot; m_btn = btn; invalidate (true); }
		else if (!btn && m_down >= 0)
		{
			int d = m_down; m_down = -1; invalidate (true);
			if (d == hot && g_ribbonCmd)
			{
				int ax = 0, ay = 0;
				for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
				const Cell &c = m_cell[d];
				g_ribbonCmd (c.cmd, c.arg, m_btn, ax + c.x, ay + c.y + c.h + 2);
			}
		}
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
private:
	struct Cell { int x, y, w, h, cmd, arg; const char *tip; const char *label; int icon; };
	enum { MAXC = 96 };
	Cell m_cell[MAXC]; int m_n;
	int m_gx[8], m_gw[8], m_ng; const char *m_gname[8];
	int m_hot, m_down, m_btn, m_shX;

	void add (int x, int y, int w, int h, int cmd, int arg, const char *tip, int ic = -1, const char *label = 0)
	{
		if (m_n >= MAXC) return;
		Cell &c = m_cell[m_n++];
		c.x = x; c.y = y; c.w = w; c.h = h; c.cmd = cmd; c.arg = arg; c.tip = tip; c.icon = ic; c.label = label;
	}
	void group (int x, int w, const char *name) { m_gx[m_ng] = x; m_gw[m_ng] = w; m_gname[m_ng] = name; m_ng++; }
	void layoutCells ()
	{
		m_n = 0; m_ng = 0;
		int x = 10, top = 6;
		// Clipboard
		add (x, top, 46, 64, C_PASTE, 0, "Paste (Ctrl+V); right click: as a new layer, a file as a layer", I_PASTE, "Paste");
		add (x + 50, top, 28, 26, C_CUT, 0, "Cut (Ctrl+X)", I_CUT);
		add (x + 50, top + 30, 28, 26, C_COPY, 0, "Copy (Ctrl+C)", I_COPY);
		add (x + 80, top, 28, 26, C_UNDO, 0, "Undo (Ctrl+Z)", I_UNDO);
		add (x + 80, top + 30, 28, 26, C_REDO, 0, "Redo (Ctrl+Y)", I_REDO);
		group (x, 108, "Clipboard"); x += 120;
		// Image
		add (x, top, 48, 64, C_SELECT, 0, "Select: a rectangle, free-form, the magic wand (S, L, W)", g_selKind == SK_FREE ? I_LASSO : g_selKind == SK_WAND ? I_WAND : I_SELECT, "Select");
		add (x + 52, top - 2, 84, 22, C_CROP, 0, "Crop to the selection", I_CROP, "Crop");
		add (x + 52, top + 21, 84, 22, C_RESIZE, 0, "Resize the picture or its canvas (Ctrl+W)", I_RESIZE, "Resize");
		add (x + 52, top + 44, 84, 22, C_ROTATE, 0, "Rotate or flip (the selection, or the picture)", I_ROTATE, "Rotate");
		group (x, 136, "Image"); x += 146;
		// Tools: 2 rows of 4
		static const int TI[8] = { T_PENCIL, T_FILL, T_TEXT, T_ERASER, T_PICKER, T_ZOOM, T_GRADIENT, -1 };
		static const int TIC[8] = { I_PENCIL, I_FILL, I_TEXT, I_ERASER, I_PICKER, I_ZOOM, I_GRADIENT, I_FX };
		static const char *const TT[8] = { "Pencil (P)", "Fill: a colour, a gradient, a pattern (F)", "Text (T)", "Eraser (E)",
			"Colour picker (K)", "Magnifier: left in, right out (Z)", "Gradient (G)", "Adjust: brightness, hue, blur..." };
		for (int i = 0; i < 8; i++) add (x + (i % 4) * 31, top + 2 + (i / 4) * 32, 28, 28, TI[i] < 0 ? C_ADJUST : C_TOOL, TI[i], TT[i], TIC[i]);
		group (x, 122, "Tools"); x += 134;
		// Brushes
		add (x, top, 62, 64, C_BRUSHES, 0, "Brushes (B): pencil, brush, soft, calligraphy, airbrush, marker, crayon, patterns", -1, "Brushes");
		group (x, 62, "Brushes"); x += 74;
		// Shapes
		m_shX = x;
		for (int k = 0; k < SH_COUNT; k++) add (x + 2 + (k % 5) * 24, 8 + (k / 5) * 22, 24, 22, C_SHAPE, k, SHAPE_NAMES[k]);
		group (x, 124, "Shapes"); x += 136;
		// Colours
		add (x, top, 40, 64, C_COL1, 0, "Colour 1: the left button's (the palette sets the chosen one)", -1, "1");
		add (x + 42, top, 40, 64, C_COL2, 0, "Colour 2: the right button's, the shapes' inside, the text's background", -1, "2");
		int px0 = x + 90;
		for (int i = 0; i < 20; i++) add (px0 + (i % 10) * 21, 7 + (i / 10) * 21, 19, 19, C_PALETTE, i, "Left: colour 1 (or the chosen one) / right: colour 2");
		for (int i = 0; i < 10; i++) add (px0 + i * 21, 7 + 2 * 21, 19, 19, C_CUSTOM, i, "A colour of yours (Edit adds them)");
		add (px0 + 214, top, 46, 64, C_EDITCOL, 0, "Edit colours: any colour (it joins yours)", I_EDITCOL, "Edit");
		group (x, 90 + 214 + 46, "Colours");
	}
	int cellAt (int mx, int my) const
	{
		for (int i = 0; i < m_n; i++) { const Cell &c = m_cell[i]; if (mx >= c.x && my >= c.y && mx < c.x + c.w && my < c.y + c.h) return i; }
		return -1;
	}
	bool isOn (const Cell &c) const
	{
		switch (c.cmd)
		{
		case C_SELECT: return g_tool == T_SELECT;
		case C_TOOL: return g_tool == c.arg;
		case C_BRUSHES: return g_tool == T_BRUSH;
		case C_SHAPE: return g_tool == T_SHAPE && g_shape == c.arg;
		case C_COL1: return g_activeCol == 1;
		case C_COL2: return g_activeCol == 2;
		}
		return false;
	}
	void drawCell (int i, unsigned ink, unsigned bg)
	{
		const Cell &c = m_cell[i];
		bool hot = i == m_hot, down = i == m_down, on = isOn (c);
		if (c.cmd == C_PALETTE || c.cmd == C_CUSTOM)
		{
			int cx = c.x + c.w / 2, cy = c.y + c.h / 2;
			bool empty = c.cmd == C_CUSTOM && !(g_custom[c.arg] >> 24);
			unsigned col = c.cmd == C_PALETTE ? PALETTE[c.arg] : g_custom[c.arg] & 0xFFFFFF;
			if (hot) disc (canvas, cx, cy, 10, C_ACCENT);
			if (empty) { disc (canvas, cx, cy, 9, wk_mix (bg, 0x000000, 50)); disc (canvas, cx, cy, 8, wk_mix (bg, 0xFFFFFF, 120)); }
			else { disc (canvas, cx, cy, 9, wk_mix (col, 0x000000, 60)); disc (canvas, cx, cy, 8, col); }
			return;
		}
		if (c.cmd == C_SHAPE)
		{
			if (on) canvas.fillRect (c.x, c.y, c.w, c.h, wk_mix (0xFFFFFF, C_ACCENT, 70));
			else if (hot) canvas.fillRect (c.x, c.y, c.w, c.h, wk_mix (0xFFFFFF, C_ACCENT, 30));
			shape_icon (canvas, c.arg, c.x + 4, c.y + 3, 16, 16, 0x343030);
			return;
		}
		hot_box (canvas, c.x, c.y, c.w, c.h, bg, on, hot, down);
		if (c.cmd == C_COL1 || c.cmd == C_COL2)
		{
			unsigned col = (c.cmd == C_COL1 ? g_col1 : g_col2) & 0xFFFFFF;
			int cx = c.x + c.w / 2, cy = c.y + 21;
			disc (canvas, cx, cy, 15, wk_mix (bg, ink, 70)); disc (canvas, cx, cy, 14, wk_mix (col, 0x000000, 70)); disc (canvas, cx, cy, 13, col);
			wk_text_c (canvas, c.x, c.y + 40, c.w, 18, c.label, ink);
			return;
		}
		if (c.cmd == C_BRUSHES)
		{
			brush_sample (canvas, c.x + 6, c.y + 6, c.w - 12, 30, g_brush, C_ACCENT);
			wk_text_c (canvas, c.x, c.y + 36, c.w, 18, c.label, ink);
			chevron (canvas, c.x + c.w / 2, c.y + 57, ink);
			return;
		}
		if (c.label && c.h >= 50)					// (a big button: its icon, its name below)
		{
			icon (canvas, c.icon, c.x + (c.w - 20) / 2, c.y + 10, ink);
			wk_text_c (canvas, c.x, c.y + 36, c.w, 18, c.label, ink);
			if (c.cmd == C_SELECT) chevron (canvas, c.x + c.w / 2, c.y + 57, ink);
			return;
		}
		if (c.label)							// (a wide small one: the icon, the name after it)
		{
			icon (canvas, c.icon, c.x + 3, c.y + (c.h - 20) / 2, ink);
			wk_text_l (canvas, c.x + 27, c.y, c.h, c.label, ink);
			if (c.cmd == C_ROTATE) chevron (canvas, c.x + c.w - 8, c.y + c.h / 2, ink);
			return;
		}
		icon (canvas, c.icon, c.x + (c.w - 20) / 2, c.y + (c.h - 20) / 2, ink);
	}
};

// ---- the options bar ------------------------------------------------------------------------------------------
enum { OPT_H = 36 };
// The options (O_*: a toggle or a choice the app does; the sliders set their value themselves).
enum { O_SIZE, O_OPACITY, O_HARD, O_TOL,					// sliders
       O_BRUSHKIND, O_SELRECT, O_SELFREE, O_SELWAND, O_SELNEW, O_SELADD, O_SELSUB, O_CONTIG, O_ALLLAYERS,
       O_SELALL, O_INVERT, O_DESELECT, O_FILLCOL, O_FILLGRAD, O_FILLPAT, O_GRADLIST, O_GSHAPE, O_GREPEAT, O_GREVERSE,
       O_PATTERN, O_GERASE, O_FONT, O_TSIZE, O_BOLD, O_ITALIC, O_UNDER, O_ALIGN, O_SMOOTH, O_TBACK, O_OUTLINE, O_FILLSH };
static void (*g_optCmd) (int opt, int arg, int x, int y);

class OptionsBar : public Widget
{
public:
	OptionsBar (int l, int t, int w) : Widget (l, t, w, OPT_H), m_n (0), m_hot (-1), m_drag (-1) {}
	unsigned bgColor () override { return opt_bg (); }
	void onDraw () override
	{
		unsigned bg = opt_bg ();
		canvas.fillRect (0, 0, width, height, bg);
		canvas.fillRect (0, height - 1, width, 1, wk_mix (bg, 0x000000, 40));
		layoutItems ();
		for (int i = 0; i < m_n; i++) drawItem (i, bg);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (m_drag >= 0)
		{
			It &s = m_it[m_drag];
			setSlider (s.arg, slider_val (mx, s.sx, s.sw, s.lo, s.hi));
			if (!bl) { m_drag = -1; catchOutside = false; pressed = false; }
			invalidate (true);
			return true;
		}
		int hot = -1;
		for (int i = 0; i < m_n; i++) if (m_it[i].kind != K_LABEL && m_it[i].kind != K_SEP && mx >= m_it[i].x && mx < m_it[i].x + m_it[i].w && my >= 4 && my < height - 4) hot = i;
		if (hot != m_hot) { m_hot = hot; invalidate (true); }
		if (bl && !pressed && hot >= 0)
		{
			pressed = true;
			It &it = m_it[hot];
			if (it.kind == K_SLIDER) { m_drag = hot; catchOutside = true; setSlider (it.arg, slider_val (mx, it.sx, it.sw, it.lo, it.hi)); invalidate (true); return true; }
			int ax = 0, ay = 0;
			for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
			if (g_optCmd) g_optCmd (it.arg, it.val, ax + it.x, ay + height);
			if (it.kind == K_DROP || it.kind == K_GRAD) pressed = false;	// (a popup took the release)
			invalidate (true);
		}
		if (!bl) pressed = false;
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
private:
	enum { K_LABEL, K_TOGGLE, K_SLIDER, K_DROP, K_SEP, K_GRAD, K_GSHAPE, K_ICON, K_GLYPH };
	struct It { int kind, x, w, arg, val; bool on; const char *text; int sx, sw, lo, hi, v; const char *suffix; int icon; };
	enum { MAXI = 48 };
	It m_it[MAXI]; int m_n, m_hot, m_drag, m_x;

	static int *sliderVar (int opt)
	{
		switch (opt)
		{
		case O_SIZE: return &g_sizes[g_tool];
		case O_OPACITY: return &g_opac[g_tool];
		case O_HARD: return g_tool == T_ERASER ? &g_eraseHard : &g_hard;
		case O_TOL: return &g_tol;
		}
		return 0;
	}
	void setSlider (int opt, int v) { int *p = sliderVar (opt); if (p && *p != v) { *p = v; if (g_optCmd) g_optCmd (opt, v, -1, -1); } }

	It &push (int kind, int w) { It &i = m_it[m_n < MAXI - 1 ? m_n++ : m_n]; i.kind = kind; i.x = m_x; i.w = w; i.arg = -1; i.val = 0; i.on = false; i.text = 0; i.suffix = 0; i.icon = -1; m_x += w; return i; }
	void label (const char *s) { push (K_LABEL, wk_tw (s) + 8).text = s; }
	void ico (int ic) { push (K_ICON, 30).icon = ic; }
	void sep () { m_x += 6; push (K_SEP, 1); m_x += 9; }
	void toggle (int opt, const char *s, bool on, int val = 0) { It &i = push (K_TOGGLE, wk_tw (s) + 14); i.arg = opt; i.on = on; i.text = s; i.val = val; m_x += 4; }
	void glyph (int opt, const char *s, bool on, int style, int val = 0) { It &i = push (K_GLYPH, 26); i.arg = opt; i.on = on; i.text = s; i.val = val; i.sw = style; m_x += 4; }
	void drop (int opt, const char *s, int w) { It &i = push (K_DROP, w); i.arg = opt; i.text = s; m_x += 8; }
	void slider (int opt, const char *s, int w, int lo, int hi, const char *suffix)
	{
		int *p = sliderVar (opt);
		char mx[16]; fmt_int (mx, hi, suffix);
		It &i = push (K_SLIDER, wk_tw (s) + 10 + w + 10 + wk_tw (mx) + 4);
		i.arg = opt; i.text = s; i.lo = lo; i.hi = hi; i.v = p ? *p : 0; i.suffix = suffix;
		i.sx = i.x + wk_tw (s) + 10; i.sw = w;
		m_x += 6;
	}
	void layoutItems ()
	{
		m_n = 0; m_x = 10;
		switch (g_tool)
		{
		case T_SELECT:
			ico (g_selKind == SK_FREE ? I_LASSO : g_selKind == SK_WAND ? I_WAND : I_SELECT);
			toggle (O_SELRECT, "Rectangle", g_selKind == SK_RECT); toggle (O_SELFREE, "Free-form", g_selKind == SK_FREE); toggle (O_SELWAND, "Magic wand", g_selKind == SK_WAND);
			sep (); toggle (O_SELNEW, "New", g_selMode == SEL_REPLACE); toggle (O_SELADD, "Add (Shift)", g_selMode == SEL_ADD); toggle (O_SELSUB, "Subtract (Alt)", g_selMode == SEL_SUB);
			if (g_selKind == SK_WAND) { sep (); slider (O_TOL, "Tolerance", 90, 0, 255, ""); toggle (O_CONTIG, "Contiguous", g_contig); toggle (O_ALLLAYERS, "All layers", g_allLayers); }
			else { sep (); toggle (O_SELALL, "Select All", false); toggle (O_INVERT, "Invert", false); toggle (O_DESELECT, "Deselect", false); }
			break;
		case T_PENCIL:
			ico (I_PENCIL); slider (O_SIZE, "Size", 110, 1, 64, " px"); sep (); slider (O_OPACITY, "Opacity", 100, 1, 100, " %");
			break;
		case T_BRUSH:
			ico (I_BRUSH); drop (O_BRUSHKIND, BRUSH_NAMES[g_brush], 140);
			sep (); slider (O_SIZE, "Size", 110, 1, 200, " px"); sep (); slider (O_OPACITY, "Opacity", 100, 1, 100, " %");
			if (g_brush == BR_SOFT) { sep (); slider (O_HARD, "Hardness", 90, 0, 100, " %"); }
			break;
		case T_ERASER:
			ico (I_ERASER); slider (O_SIZE, "Size", 110, 1, 200, " px"); sep (); slider (O_OPACITY, "Opacity", 100, 1, 100, " %");
			sep (); slider (O_HARD, "Hardness", 90, 0, 100, " %");
			break;
		case T_FILL:
			ico (I_FILL);
			toggle (O_FILLCOL, "Colour", g_fillMode == FM_COLOUR); toggle (O_FILLGRAD, "Gradient", g_fillMode == FM_GRADIENT); toggle (O_FILLPAT, "Pattern", g_fillMode == FM_PATTERN);
			sep ();
			if (g_fillMode == FM_GRADIENT) gradOptions ();
			else if (g_fillMode == FM_PATTERN) { drop (O_PATTERN, BRUSH_NAMES[g_pattern], 110); sep (); }
			slider (O_TOL, "Tolerance", 56, 0, 255, ""); toggle (O_CONTIG, "Contiguous", g_contig); toggle (O_ALLLAYERS, "All layers", g_allLayers);
			if (g_fillMode != FM_GRADIENT) { sep (); slider (O_OPACITY, "Opacity", 70, 1, 100, " %"); }
			break;
		case T_GRADIENT:
			ico (I_GRADIENT); gradOptions (); slider (O_OPACITY, "Opacity", 90, 1, 100, " %");
			sep (); toggle (O_GERASE, "Erase (fade the layer)", g_gradErase);
			break;
		case T_TEXT:
		{
			ico (I_TEXT); text_init ();
			drop (O_FONT, g_tnfam ? g_tfamNames[pclamp (g_tfam, 0, g_tnfam - 1)] : "(no fonts)", 170);
			static char sz[12]; fmt_int (sz, g_tsize, " px"); drop (O_TSIZE, sz, 74);
			glyph (O_BOLD, "B", g_tbold, 2); glyph (O_ITALIC, "I", g_titalic, 1); glyph (O_UNDER, "U", g_tunder, 0);
			sep (); glyph (O_ALIGN, "L", g_talign == 0, 0, 0); glyph (O_ALIGN, "C", g_talign == 1, 0, 1); glyph (O_ALIGN, "R", g_talign == 2, 0, 2);
			sep (); toggle (O_SMOOTH, "Smooth edges", g_tsmooth); toggle (O_TBACK, "Background (colour 2)", g_tback);
			sep (); label (g_text.on ? "Enter: a new line.  Esc: cancel.  Click outside: put it down." : "Click on the picture, then type.");
			break;
		}
		case T_SHAPE:
			ico (I_OUTLINE); toggle (O_OUTLINE, "Outline (colour 1)", g_outline); toggle (O_FILLSH, "Fill (colour 2)", g_fillShape);
			sep (); slider (O_SIZE, "Width", 100, 1, 32, " px"); sep (); slider (O_OPACITY, "Opacity", 90, 1, 100, " %");
			sep (); label ("Shift: a square, a circle, a line at 45 degrees.");
			break;
		case T_PICKER: ico (I_PICKER); label ("Click: colour 1, right click: colour 2 -- from what is seen."); break;
		case T_ZOOM: ico (I_ZOOM); label ("Click: zoom in, right click: zoom out.  Ctrl + wheel anywhere."); break;
		}
	}
	void gradOptions ()
	{
		It &g = push (K_GRAD, 100); g.arg = O_GRADLIST; m_x += 8;
		for (int k = 0; k < GS_COUNT; k++) { It &i = push (K_GSHAPE, 24); i.arg = O_GSHAPE; i.val = k; i.on = g_gshape == k; m_x += 2; }
		m_x += 6;
		drop (O_GREPEAT, GREPEAT_NAMES[g_grepeat], 98);
		toggle (O_GREVERSE, "Reverse", g_greverse);
		sep ();
	}
	void drawItem (int i, unsigned bg)
	{
		It &it = m_it[i];
		unsigned ink = wk_ink_for (bg), dim = wk_mix (bg, ink, 150);
		int y = 6, h = height - 12;
		bool hot = i == m_hot;
		switch (it.kind)
		{
		case K_LABEL: wk_text_l (canvas, it.x, 0, height, it.text, dim); break;
		case K_ICON: icon (canvas, it.icon, it.x, (height - 20) / 2, ink); break;
		case K_SEP: canvas.fillRect (it.x, 9, 1, height - 18, wk_mix (bg, 0x000000, 40)); break;
		case K_TOGGLE: case K_GLYPH:
		{
			unsigned f = it.on ? wk_mix (bg, C_ACCENT, 64) : hot ? 0xFFFFFF : wk_mix (bg, 0xFFFFFF, 190);
			wk_rbox (canvas, it.x, y, it.w, h, 4, f, f);
			wk_rline (canvas, it.x, y, it.w, h, 4, it.on ? wk_mix (bg, C_ACCENT, 170) : wk_mix (bg, 0x000000, 50));
			wk_text_c (canvas, it.x, y, it.w, h, it.text, ink, it.kind == K_GLYPH ? it.sw : 0);
			if (it.kind == K_GLYPH && it.arg == O_UNDER) canvas.fillRect (it.x + 9, y + h - 6, it.w - 18, 1, ink);
			break;
		}
		case K_DROP: case K_GRAD:
		{
			wk_rbox (canvas, it.x, y, it.w, h, 4, 0xFFFFFF, 0xFFFFFF); wk_rline (canvas, it.x, y, it.w, h, 4, hot ? C_ACCENT : wk_mix (bg, 0x000000, 50));
			if (it.kind == K_GRAD) grad_draw (canvas, it.x + 5, y + 5, it.w - 28, h - 10, g_grad);
			else { char b[64]; wk_text_fit (it.text, it.w - 30, b, sizeof b); wk_text_l (canvas, it.x + 8, y, h, b, 0x202020); }
			chevron (canvas, it.x + it.w - 12, y + h / 2, 0x202020);
			break;
		}
		case K_GSHAPE:
		{
			unsigned f = it.on ? wk_mix (bg, C_ACCENT, 64) : hot ? 0xFFFFFF : wk_mix (bg, 0xFFFFFF, 190);
			wk_rbox (canvas, it.x, y, it.w, h, 4, f, f); wk_rline (canvas, it.x, y, it.w, h, 4, it.on ? wk_mix (bg, C_ACCENT, 170) : wk_mix (bg, 0x000000, 50));
			gshape_draw (canvas, it.x + 3, y + (h - 18) / 2, 18, it.val);
			break;
		}
		case K_SLIDER:
		{
			int *p = sliderVar (it.arg); it.v = p ? *p : 0;
			wk_text_l (canvas, it.x, 0, height, it.text, dim);
			slider_draw (canvas, it.sx, 0, it.sw, height, it.v, it.lo, it.hi, bg);
			char b[16]; fmt_int (b, it.v, it.suffix); wk_text_l (canvas, it.sx + it.sw + 12, 0, height, b, ink);
			break;
		}
		}
	}
};

// ---- the layers' panel ---------------------------------------------------------------------------------------
enum { PANEL_W = 264, ROW_H = 54, THUMB_W = 54, THUMB_H = 36, PHEAD = 102 };
enum { L_ADD, L_DUP, L_DEL, L_UP, L_DOWN, L_MERGE, L_PROPS, L_SELECT, L_TOGGLE, L_RENAME, L_MENU, L_OPACITY, L_BLEND };
static void (*g_layerCmd) (int cmd, int arg, int x, int y);

class LayersPanel : public Widget
{
public:
	LayersPanel (int l, int t, int h) : Widget (l, t, PANEL_W, h), m_hot (-1), m_hotBtn (-1), m_top (0), m_last (0), m_lastRow (-1), m_drag (false), m_hotBlend (false) {}
	unsigned bgColor () override { return pan_bg (); }
	int listTop () const { return PHEAD; }
	int listH () const { return height - PHEAD - 44; }
	void sync () { invalidate (true); }
	void onDraw () override
	{
		unsigned bg = pan_bg (), ink = wk_ink_for (bg), dim = wk_mix (bg, ink, 150);
		canvas.fillRect (0, 0, width, height, bg);
		canvas.fillRect (0, 0, 1, height, wk_mix (bg, 0x000000, 40));
		wk_text_l (canvas, 14, 6, 26, "Layers", ink, 2);
		const Layer &cl = D.lay[D.cur];
		// the blend mode, the opacity
		wk_text_l (canvas, 14, 38, 24, "Blend", dim);
		wk_rbox (canvas, 70, 38, width - 84, 24, 4, 0xFFFFFF, 0xFFFFFF); wk_rline (canvas, 70, 38, width - 84, 24, 4, m_hotBlend ? C_ACCENT : wk_mix (bg, 0x000000, 50));
		char bn[48]; scpy (bn, BLEND_NAMES[pclamp (cl.blend, 0, NBLENDS - 1)], sizeof bn);
		if (cl.clip && (cl.blend == GPC_B_MASK || cl.blend == GPC_B_CUTOUT)) { int n = slen (bn); scpy (bn + n, " (layer below)", (int) sizeof bn - n); }
		wk_text_l (canvas, 78, 38, 24, bn, 0x202020);
		chevron (canvas, width - 26, 50, 0x202020);
		wk_text_l (canvas, 14, 68, 24, "Opacity", dim);
		int sw = width - 84 - 62;
		slider_draw (canvas, 74, 68, sw, 24, (cl.opacity * 100 + 127) / 255, 0, 100, bg);
		char op[8]; fmt_int (op, (cl.opacity * 100 + 127) / 255, " %");
		wk_rbox (canvas, width - 62, 68, 48, 24, 4, 0xFFFFFF, 0xFFFFFF); wk_rline (canvas, width - 62, 68, 48, 24, 4, wk_mix (bg, 0x000000, 50));
		wk_text_c (canvas, width - 62, 68, 48, 24, op, 0x202020);
		// the list: the top layer first
		int y0 = listTop (), lh = listH ();
		wk_rbox (canvas, 8, y0, width - 16, lh, 6, C_FIELD, C_FIELD); wk_rline (canvas, 8, y0, width - 16, lh, 6, wk_mix (bg, 0x000000, 45));
		int rows = (lh - 8) / ROW_H;
		m_top = pclamp (m_top, 0, pmax (0, D.n - rows));
		for (int r = 0; r < rows; r++)
		{
			int li = D.n - 1 - (m_top + r);
			if (li < 0) break;
			int y = y0 + 4 + r * ROW_H;
			const Layer &l = D.lay[li];
			bool sel = li == D.cur;
			if (sel) wk_hilite (canvas, 11, y, width - 22, ROW_H - 2, 5, true);
			else if (r == m_hot) wk_rbox (canvas, 11, y, width - 22, ROW_H - 2, 5, wk_mix (C_FIELD, C_ACCENT, 30), wk_mix (C_FIELD, C_ACCENT, 30));
			else if (r & 1) wk_rbox (canvas, 11, y, width - 22, ROW_H - 2, 5, wk_mix (C_FIELD, 0x000000, 7), wk_mix (C_FIELD, 0x000000, 7));
			unsigned tc = sel ? C_SEL_TEXT : C_FIELD_TEXT, dc = sel ? wk_mix (C_SEL_TEXT, C_ACCENT, 60) : wk_mix (C_FIELD, C_FIELD_TEXT, 150);
			icon (canvas, l.visible ? I_EYE : I_EYEOFF, 16, y + (ROW_H - 2 - 20) / 2, tc);
			if (is_clip_mask (li)) { canvas.fillRect (44, y + 2, 2, ROW_H - 6, dc); canvas.fillRect (44, y + ROW_H - 6, 8, 2, dc); }
			thumb (li, is_clip_mask (li) ? 54 : 44, y + 8);
			int tx = (is_clip_mask (li) ? 54 : 44) + THUMB_W + 10;
			char nm[40]; wk_text_fit (l.name, width - tx - 40, nm, sizeof nm, sel ? 2 : 0);
			wk_text (canvas, tx, y + 8, nm, tc, sel ? 2 : 0);
			char md[48]; scpy (md, BLEND_NAMES[pclamp (l.blend, 0, NBLENDS - 1)], sizeof md);
			if (l.opacity < 255 && l.blend != GPC_B_MASK) { int n = slen (md); char o2[12]; fmt_int (o2, (l.opacity * 100 + 127) / 255, " %"); scpy (md + n, " - ", (int) sizeof md - n); n = slen (md); scpy (md + n, o2, (int) sizeof md - n); }
			wk_text (canvas, tx, y + 27, md, dc);
			if (l.blend == GPC_B_MASK || l.blend == GPC_B_CUTOUT) { ring2 (canvas, width - 30, y + ROW_H / 2, 7, dc, 20); disc (canvas, width - 30, y + ROW_H / 2, 3, dc); }
		}
		if (D.n > rows)
		{
			WkThumb t = wk_thumb (D.n, rows, m_top, lh - 8);
			wk_draw_vscroll (canvas, width - WK_SBW - 10, y0 + 4, WK_SBW, lh - 8, t, C_FIELD);
		}
		// the buttons
		static const int BI[7] = { I_ADD, I_DUP, I_TRASH, I_UP, I_DOWN, I_MERGE, I_PROPS };
		int by = height - 38, bw = (width - 20) / 7;
		for (int i = 0; i < 7; i++)
		{
			int x = 10 + i * bw;
			bool off = (i == 2 && D.n < 2) || (i == 3 && D.cur >= D.n - 1) || (i == 4 && D.cur == 0) || (i == 5 && D.cur == 0) || (i < 2 && D.n >= MAXLAYERS);
			if (i == m_hotBtn && !off) hot_box (canvas, x, by, bw - 2, 30, bg, false, true);
			icon (canvas, BI[i], x + (bw - 22) / 2, by + 5, ink, off);
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		int y0 = listTop (), rows = (listH () - 8) / ROW_H, sw = width - 84 - 62;
		if (m_drag)
		{
			if (g_layerCmd) g_layerCmd (L_OPACITY, slider_val (mx, 74, sw, 0, 100), 0, 0);
			if (!bl) { m_drag = false; catchOutside = false; pressed = false; }
			return true;
		}
		if (wheel && in && my >= y0) { m_top = pclamp (m_top - wheel, 0, pmax (0, D.n - rows)); invalidate (true); return true; }
		int bw = (width - 20) / 7, by = height - 38;
		int hb = in && my >= by && my < by + 30 && mx >= 10 && mx < 10 + 7 * bw ? (mx - 10) / bw : -1;
		int hr = in && my >= y0 + 4 && my < y0 + 4 + rows * ROW_H ? (my - y0 - 4) / ROW_H : -1;
		if (hr >= 0 && D.n - 1 - (m_top + hr) < 0) hr = -1;
		bool hbl = in && my >= 38 && my < 62 && mx >= 70 && mx < width - 14;
		if (hb != m_hotBtn || hr != m_hot || hbl != m_hotBlend)
		{
			m_hotBtn = hb; m_hot = hr; m_hotBlend = hbl;
			tip = hb >= 0 ? BTN_TIPS[hb] : hr >= 0 ? "Click: the layer drawn on; the eye: shown / hidden; double click: its properties; right click: more" : hbl ? "How the layer mixes with what is under it" : 0;
			invalidate (true);
		}
		if ((bl || br) && !pressed && in)
		{
			pressed = true;
			int ax = 0, ay = 0;
			for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
			// (a popup takes the button's release: the press is over when it returns)
			if (hbl && bl) { if (g_layerCmd) g_layerCmd (L_BLEND, D.cur, ax + 70, ay + 64); pressed = false; return true; }
			if (bl && my >= 68 && my < 92 && mx >= 66 && mx < 74 + sw + 8) { m_drag = true; catchOutside = true; if (g_layerCmd) g_layerCmd (L_OPACITY, slider_val (mx, 74, sw, 0, 100), 0, 0); return true; }
			if (hb >= 0 && bl) { if (g_layerCmd) g_layerCmd (L_ADD + hb, 0, 0, 0); if (hb == L_PROPS) pressed = false; return true; }
			if (hr >= 0)
			{
				int li = D.n - 1 - (m_top + hr);
				if (br) { if (g_layerCmd) { g_layerCmd (L_SELECT, li, 0, 0); g_layerCmd (L_MENU, li, ax + mx, ay + my); } pressed = false; return true; }
				if (mx < 40) { if (g_layerCmd) g_layerCmd (L_TOGGLE, li, 0, 0); return true; }
				unsigned t = kapi_get_ticks ();
				bool dbl = hr == m_lastRow && t - m_last < 45;
				m_last = t; m_lastRow = hr;
				if (g_layerCmd) g_layerCmd (dbl ? L_RENAME : L_SELECT, li, 0, 0);
				if (dbl) pressed = false;
			}
			return true;
		}
		if (!bl && !br) pressed = false;
		return in;
	}
private:
	int m_hot, m_hotBtn, m_top; unsigned m_last; int m_lastRow; bool m_drag, m_hotBlend;
	static const char *const BTN_TIPS[7];
	// A layer's thumbnail: its pixels (the nearest ones) over a checkerboard, the picture's proportions.
	void thumb (int li, int x, int y)
	{
		const Layer &l = D.lay[li];
		int tw = THUMB_W, th = THUMB_H;
		if (D.w * THUMB_H > D.h * THUMB_W) th = pmax (4, D.h * THUMB_W / pmax (1, D.w)); else tw = pmax (4, D.w * THUMB_H / pmax (1, D.h));
		int ox2 = x + (THUMB_W - tw) / 2, oy2 = y + (THUMB_H - th) / 2;
		for (int j = 0; j < th; j++)
			for (int i = 0; i < tw; i++)
			{
				unsigned c = l.px[(unsigned) (j * D.h / th) * D.w + i * D.w / tw], a = c >> 24;
				unsigned ck = ((i >> 2) ^ (j >> 2)) & 1 ? 0xD6D6D6 : 0xFFFFFF;
				canvas.pixel (ox2 + i, oy2 + j, a == 255 ? c & 0xFFFFFF : a ? wk_mix (ck, c & 0xFFFFFF, (int) a + 1) : ck);
			}
		canvas.frameRect (ox2 - 1, oy2 - 1, tw + 2, th + 2, wk_mix (C_FIELD, 0, 90));
	}
};
const char *const LayersPanel::BTN_TIPS[7] = { "New layer (Ctrl+L)", "Duplicate the layer", "Delete the layer", "Move the layer up", "Move the layer down",
	"Merge the layer into the one below", "The layer's properties: name, blend mode, opacity" };

// ---- the status bar --------------------------------------------------------------------------------------------
enum { STATUS_H = 28 };
class StatusBar : public Widget
{
public:
	CanvasView *view;
	void (*zoomCmd) (int what);			// -1 out, +1 in, 0 the list (x of the popup in popX), 2 grid, 3 fit, 100+: a zoom (%)
	int popX;
	StatusBar (int l, int t, int w, CanvasView *v) : Widget (l, t, w, STATUS_H), view (v), zoomCmd (0), popX (0), m_hot (0), m_drag (false) {}
	unsigned bgColor () override { return opt_bg (); }
	void onDraw () override
	{
		unsigned bg = opt_bg (), ink = wk_ink_for (bg), dim = wk_mix (bg, ink, 150);
		canvas.fillRect (0, 0, width, height, bg);
		canvas.fillRect (0, 0, width, 1, wk_mix (bg, 0x000000, 40));
		char b[64];
		int x = 10;
		icon (canvas, I_POINTER, x, 4, ink); x += 24;
		if (view->ptrX >= 0) { fmt2 (b, view->ptrX, view->ptrY, ", "); wk_text_l (canvas, x, 0, height, b, ink); }
		x += 100;
		Rect s = D.fl.px ? float_rect () : g_sel;
		icon (canvas, I_SELSIZE, x, 4, ink); x += 24;
		if (!s.empty ()) { fmt2 (b, s.x1 - s.x0, s.y1 - s.y0, " x "); wk_text_l (canvas, x, 0, height, b, ink); }
		x += 116;
		icon (canvas, I_IMGSIZE, x, 4, ink); x += 24;
		fmt2 (b, D.w, D.h, " x "); wk_text_l (canvas, x, 0, height, b, ink);
		x += 104;
		const Layer &l = D.lay[D.cur];
		char lt[96]; scpy (lt, "Layer: ", sizeof lt); int n = slen (lt); scpy (lt + n, l.name, (int) sizeof lt - n); n = slen (lt);
		scpy (lt + n, " (", (int) sizeof lt - n); n = slen (lt); scpy (lt + n, BLEND_NAMES[pclamp (l.blend, 0, NBLENDS - 1)], (int) sizeof lt - n); n = slen (lt);
		scpy (lt + n, l.visible ? ")" : ", hidden)", (int) sizeof lt - n);
		if (x < zx () - 30) { char f[96]; wk_text_fit (lt, zx () - 16 - x, f, sizeof f); wk_text_l (canvas, x, 0, height, f, dim); }
		// the right: grid, fit, - slider +, the zoom
		int gx = zx ();
		hot_box (canvas, gx, 3, 24, height - 6, bg, g_grid, m_hot == 4); icon (canvas, I_GRID, gx + 2, 4, ink);
		hot_box (canvas, gx + 28, 3, 24, height - 6, bg, false, m_hot == 5); icon (canvas, I_FIT, gx + 30, 4, ink);
		int mx = gx + 62;
		hot_box (canvas, mx, 3, 18, height - 6, bg, false, m_hot == 1); wk_glyph (canvas, WKG_MINUS, mx + 9, height / 2, 8, ink);
		slider_draw (canvas, mx + 26, 0, 140, height, zpos (view->zoom), 0, 1000, bg);
		hot_box (canvas, mx + 174, 3, 18, height - 6, bg, false, m_hot == 2); wk_glyph (canvas, WKG_PLUS, mx + 183, height / 2, 8, ink);
		char z[12]; fmt_int (z, view->zoom, " %");
		hot_box (canvas, mx + 198, 3, 64, height - 6, bg, false, m_hot == 3);
		wk_text_c (canvas, mx + 198, 0, 64, height, z, ink);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int gx = zx (), m0 = gx + 62;
		if (m_drag)
		{
			if (zoomCmd) zoomCmd (100 + zval (pclamp ((mx - m0 - 26) * 1000 / 139, 0, 1000)));
			if (!bl) { m_drag = false; catchOutside = false; pressed = false; }
			return true;
		}
		bool in = my >= 0 && my < height;
		int hot = !in ? 0 : mx >= gx && mx < gx + 24 ? 4 : mx >= gx + 28 && mx < gx + 52 ? 5 : mx >= m0 && mx < m0 + 18 ? 1 : mx >= m0 + 174 && mx < m0 + 192 ? 2 : mx >= m0 + 198 && mx < m0 + 262 ? 3 : mx >= m0 + 20 && mx < m0 + 172 ? 6 : 0;
		if (hot != m_hot) { m_hot = hot; tip = hot == 4 ? "The pixel grid (Ctrl+G; from 300 %)" : hot == 5 ? "The whole picture in the window" : hot == 6 ? "Zoom (Ctrl + wheel on the picture)" : 0; invalidate (true); }
		if (bl && !pressed && hot)
		{
			pressed = true; popX = m0 + 198;
			if (hot == 6) { m_drag = true; catchOutside = true; if (zoomCmd) zoomCmd (100 + zval (pclamp ((mx - m0 - 26) * 1000 / 139, 0, 1000))); }
			else if (zoomCmd) { zoomCmd (hot == 1 ? -1 : hot == 2 ? 1 : hot == 3 ? 0 : hot == 4 ? 2 : 3); if (hot == 3) pressed = false; }
		}
		if (!bl) pressed = false;
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
private:
	int m_hot; bool m_drag;
	int zx () const { return width - 340; }
	// the zoom's slider: logarithmic, 12 % .. 3200 % on 0..1000
	static int zpos (int z) { return pclamp ((int) (logf (z / 12.0f) / logf (3200 / 12.0f) * 1000 + 0.5f), 0, 1000); }
	static int zval (int p) { return pclamp ((int) (12.0f * expf (p / 1000.0f * logf (3200 / 12.0f)) + 0.5f), 12, 3200); }
	static void fmt2 (char *b, int a, int c, const char *sep)
	{
		char t1[16], t2[16]; fmt_int (t1, a, 0); fmt_int (t2, c, " px");
		scpy (b, t1, 48); int n = slen (b); scpy (b + n, sep, 48 - n); n = slen (b); scpy (b + n, t2, 48 - n);
	}
};

} // namespace pd

#endif
