//
// pview.h -- Paint's canvas: the picture at the zoom (from 1/8 to 32 times) on a grey desk, its
// transparent parts over a checkerboard, the pixel grid (the Grid toggle, from 300 %), the
// selection's dashed frame; and the tools -- Select (a rectangle; dragged inside, the pixels lift and
// move; a click outside puts them down), Pencil, Brush, Eraser (to transparent), Fill, Colour picker,
// Magnifier, the Shapes (dragged in a box; Shift: a square box -- a circle, a regular polygon --, a
// line at 45 degrees). The left button draws with colour 1, the right one with colour 2 (a shape:
// its outline colour 1, its inside colour 2 -- the right button swaps them).
//
#ifndef _paint_pview_h
#define _paint_pview_h

#include "wtk/wtk.h"
#include "raster.h"

namespace pd {

using namespace wtk;

enum { T_SELECT, T_PENCIL, T_BRUSH, T_ERASER, T_FILL, T_PICKER, T_ZOOM, T_SHAPE, T_COUNT };
static const char *const TOOL_NAMES[T_COUNT] = { "Select", "Pencil", "Brush", "Eraser", "Fill", "Colour picker", "Magnifier", "Shapes" };
static int g_tool = T_PENCIL, g_prevTool = T_PENCIL;
static int g_shape = SH_RECT;
static int g_sizes[T_COUNT] = { 1, 1, 5, 9, 1, 1, 1, 2 };	// each tool's width
static bool g_outline = true, g_fillShape = false;
static unsigned g_col1 = 0xFF000000u, g_col2 = 0xFFFFFFFFu;
static Rect g_sel;						// the selection (empty: none)
static bool g_grid;
static void (*g_changed) ();					// the app's: something to show again (status, layers)
static void (*g_toolChanged) ();				// ... the tool changed (the picker's end)

static const int ZOOMS[] = { 12, 25, 33, 50, 67, 100, 150, 200, 300, 400, 600, 800, 1200, 1600, 2400, 3200 };
enum { NZOOMS = sizeof ZOOMS / sizeof ZOOMS[0], MARGIN = 24, SBW = 13 };

static inline int fdiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static void notify () { if (g_changed) g_changed (); }
// What a pixel becomes when it is cleared (moved away, deleted, erased): on the bottom layer where it is
// opaque, colour 2 (as classic Paint); elsewhere, transparent.
static inline unsigned cleared (unsigned old) { return D.cur == 0 && (old >> 24) == 255 ? g_col2 : 0; }

// ---- the floating selection -----------------------------------------------------------------------------
static Rect float_rect () { return mkrect (D.fl.x, D.fl.y, D.fl.x + D.fl.w, D.fl.y + D.fl.h); }
// The selection's pixels of the current layer lifted (they float; the layer is clear there).
static bool float_lift ()
{
	if (D.fl.px) return true;
	Rect r = g_sel; r.clip (D.w, D.h);
	if (r.empty ()) return false;
	rec_begin (); rec_touch (r);
	D.fl.w = r.x1 - r.x0; D.fl.h = r.y1 - r.y0; D.fl.x = r.x0; D.fl.y = r.y0;
	D.fl.px = new unsigned[(unsigned) D.fl.w * D.fl.h];
	unsigned *L = D.lay[D.cur].px;
	for (int y = 0; y < D.fl.h; y++)
		for (int x = 0; x < D.fl.w; x++)
		{
			unsigned &p = L[(unsigned) (r.y0 + y) * D.w + r.x0 + x];
			D.fl.px[(unsigned) y * D.fl.w + x] = p; p = cleared (p);
		}
	g_sel = r;
	compose (r);
	return true;
}
// Put the floating pixels down on the current layer (over what is there); keepSel: still selected.
static void float_commit (bool keepSel)
{
	if (!D.fl.px) return;
	Rect r = float_rect (), rc = r;
	rc.clip (D.w, D.h);
	if (!rc.empty ())
	{
		rec_touch (rc);
		unsigned *L = D.lay[D.cur].px;
		for (int y = rc.y0; y < rc.y1; y++)
			for (int x = rc.x0; x < rc.x1; x++)
			{
				unsigned f = D.fl.px[(unsigned) (y - D.fl.y) * D.fl.w + (x - D.fl.x)];
				if (f >> 24) { unsigned &p = L[(unsigned) y * D.w + x]; p = (f >> 24) == 255 ? f : over (p, f, 255); }
			}
	}
	delete[] D.fl.px; D.fl.px = 0;
	rec_end ();
	compose (r);
	g_sel = keepSel ? rc : norect ();
}
// The floating pixels dropped (Delete): the layer stays clear where they came from.
static void float_discard ()
{
	if (!D.fl.px) return;
	Rect r = float_rect ();
	delete[] D.fl.px; D.fl.px = 0;
	rec_end ();
	compose (r);
	g_sel = norect ();
}
// New floating pixels (a paste) at (x, y): they are the operation's own (put down by float_commit).
static void float_new (unsigned *px, int w, int h, int x, int y)
{
	float_commit (false);
	D.fl.px = px; D.fl.w = w; D.fl.h = h; D.fl.x = x; D.fl.y = y;
	rec_begin ();
	g_sel = float_rect ();
	compose (g_sel);
}
// Everything pending put down (before an undo, a change of layer, of tool...).
static void settle () { float_commit (false); }

// ---- the canvas ----------------------------------------------------------------------------------------
class CanvasView : public Widget
{
public:
	int zoom, sx, sy;					// %, scrolled (px)
	int ptrX, ptrY;						// the pointer's pixel (-1: outside)
	CanvasView (int l, int t, int w, int h) : Widget (l, t, w, h), zoom (100), sx (0), sy (0), ptrX (-1), ptrY (-1),
		m_btn (0), m_drag (0), m_ax (0), m_ay (0), m_lx (0), m_ly (0), m_barHot (0), m_hoverX (-1), m_hoverY (-1), m_col (0), m_colCap (0)
	{ canFocus = true; }

	int viewW () const { return width - SBW; }
	int viewH () const { return height - SBW; }
	int zw () const { return (int) ((long long) D.w * zoom / 100); }
	int zh () const { return (int) ((long long) D.h * zoom / 100); }
	int docW () const { return zw () + 2 * MARGIN; }
	int docH () const { return zh () + 2 * MARGIN; }
	int ox () const { return docW () <= viewW () ? (viewW () - zw ()) / 2 : MARGIN - sx; }
	int oy () const { return docH () <= viewH () ? (viewH () - zh ()) / 2 : MARGIN - sy; }
	int toX (int ix) const { return ox () + (int) ((long long) ix * zoom / 100); }
	int toY (int iy) const { return oy () + (int) ((long long) iy * zoom / 100); }
	int imX (int vx) const { return fdiv ((vx - ox ()) * 100, zoom); }
	int imY (int vy) const { return fdiv ((vy - oy ()) * 100, zoom); }
	void clampScroll () { sx = pclamp (sx, 0, pmax (0, docW () - viewW ())); sy = pclamp (sy, 0, pmax (0, docH () - viewH ())); }

	// The zoom changed, the pixel under (vx, vy) of the view kept there.
	void setZoom (int z, int vx = -1, int vy = -1)
	{
		z = pclamp (z, ZOOMS[0], ZOOMS[NZOOMS - 1]);
		if (vx < 0) { vx = viewW () / 2; vy = viewH () / 2; }
		int ix = imX (vx), iy = imY (vy);
		zoom = z;
		sx = 0; sy = 0;
		if (docW () > viewW ()) sx = MARGIN + (int) ((long long) ix * zoom / 100) - vx;
		if (docH () > viewH ()) sy = MARGIN + (int) ((long long) iy * zoom / 100) - vy;
		clampScroll ();
		invalidate (true); notify ();
	}
	void zoomStep (int dir, int vx = -1, int vy = -1)
	{
		int k = 0; while (k < NZOOMS - 1 && ZOOMS[k] < zoom) k++;
		if (dir > 0) { while (k < NZOOMS - 1 && ZOOMS[k] <= zoom) k++; }
		else { while (k > 0 && ZOOMS[k] >= zoom) k--; }
		setZoom (ZOOMS[k], vx, vy);
	}
	// The whole picture in the view.
	void zoomFit ()
	{
		int zx = (viewW () - 2 * MARGIN) * 100 / pmax (1, D.w), zy = (viewH () - 2 * MARGIN) * 100 / pmax (1, D.h);
		int z = pmin (zx, zy), best = ZOOMS[0];
		for (int i = 0; i < NZOOMS; i++) if (ZOOMS[i] <= z) best = ZOOMS[i];
		setZoom (pmin (best, 100));
	}

	// ---- drawing ----
	void onDraw () override
	{
		int vw = viewW (), vh = viewH ();
		unsigned desk = wk_mix (wk_tone (C_BG, 96), 0x8C9098, 110);
		canvas.fillRect (0, 0, vw, vh, desk);
		int X0 = ox (), Y0 = oy ();
		int ix0 = pmax (0, X0), iy0 = pmax (0, Y0), ix1 = pmin (vw, X0 + zw ()), iy1 = pmin (vh, Y0 + zh ());
		// a shadow, then the pixels
		unsigned sh = wk_mix (desk, 0, 70);
		fillClip (X0 + 3, Y0 + 3, zw (), zh (), sh);
		if (ix1 > ix0 && iy1 > iy0)
		{
			if (m_colCap < vw) { delete[] m_col; m_colCap = vw + 64; m_col = new int[m_colCap]; }
			for (int x = ix0; x < ix1; x++) m_col[x] = pmin (D.w - 1, imX (x));
			for (int y = iy0; y < iy1; y++)
			{
				int iy = pmin (D.h - 1, imY (y));
				const unsigned *s = D.comp + (unsigned) iy * D.w;
				unsigned *d = canvas.px + y * canvas.stride;
				int cy = (y - Y0) / 10 & 1;
				for (int x = ix0; x < ix1; x++)
				{
					unsigned c = s[m_col[x]], a = c >> 24;
					if (a == 255) { d[x] = c & 0xFFFFFF; continue; }
					unsigned ck = (((x - X0) / 10 & 1) ^ cy) ? 0xD8D8D8 : 0xFFFFFF;	// (the checkerboard)
					d[x] = a ? wk_mix (ck, c & 0xFFFFFF, (int) a + 1) : ck;
				}
			}
			if (g_grid && zoom >= 300) drawGrid (ix0, iy0, ix1, iy1);
		}
		// the selection's dashed frame
		Rect s = D.fl.px ? float_rect () : g_sel;
		if (!s.empty ()) dashed (toX (s.x0) - 1, toY (s.y0) - 1, toX (s.x1), toY (s.y1));
		// the brush's reach under the pointer (the eraser, a wide brush)
		if (m_btn == 0 && m_hoverX >= 0 && (g_tool == T_ERASER || (g_tool == T_BRUSH && g_sizes[T_BRUSH] > 1)))
		{
			int sz = g_sizes[g_tool], r0 = -(sz / 2);
			int x0 = toX (m_hoverX + r0), y0 = toY (m_hoverY + r0), x1 = toX (m_hoverX + r0 + sz), y1 = toY (m_hoverY + r0 + sz);
			frame (x0, y0, x1 - x0, y1 - y0, 0x000000); frame (x0 + 1, y0 + 1, x1 - x0 - 2, y1 - y0 - 2, 0xFFFFFF);
		}
		drawBars ();
	}

	// ---- the mouse ----
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		if (mx < 0 && !m_btn && !m_drag)
		{
			if (ptrX != -1 || m_hoverX != -1) { ptrX = ptrY = -1; m_hoverX = -1; invalidate (true); notify (); }
			if (m_barHot) { m_barHot = 0; invalidate (true); }
			return false;
		}
		unsigned mods = kapi_get_modifiers ();
		if (wheel)
		{
			if (mods & MOD_CTRL) zoomStep (wheel > 0 ? 1 : -1, mx, my);
			else if (mods & MOD_SHIFT) { sx -= wheel * 48; clampScroll (); invalidate (true); }
			else { sy -= wheel * 48; clampScroll (); invalidate (true); }
			return true;
		}
		int btn = bl ? 1 : br ? 2 : 0;
		// the scroll bars
		int hot = mx >= viewW () && my < viewH () ? 1 : my >= viewH () && mx < viewW () ? 2 : 0;
		if (!m_btn && !m_drag && hot != m_barHot) { m_barHot = hot; invalidate (true); }
		if (m_drag == 1 || m_drag == 2)
		{
			if (m_drag == 1) sy = (int) wk_thumb_pos (my, viewH (), docH (), viewH (), wk_thumb (docH (), viewH (), sy, viewH ()).h);
			else sx = (int) wk_thumb_pos (mx, viewW (), docW (), viewW (), wk_thumb (docW (), viewW (), sx, viewW ()).h);
			clampScroll (); invalidate (true);
			if (!btn) { m_drag = 0; catchOutside = false; }
			return true;
		}
		if (btn && !m_btn && hot) { m_drag = hot; catchOutside = true; return onMouse (mx, my, bl, br, 0, 0); }
		if (m_drag == 3)						// (the middle of a pan: the space bar / drag)
		{
			sx -= mx - m_lx; sy -= my - m_ly; m_lx = mx; m_ly = my; clampScroll (); invalidate (true);
			if (!btn) { m_drag = 0; catchOutside = false; }
			return true;
		}
		int ix = imX (mx), iy = imY (my);
		bool inside = ix >= 0 && iy >= 0 && ix < D.w && iy < D.h;
		int px2 = inside ? ix : -1, py2 = inside ? iy : -1;
		if (px2 != ptrX || py2 != ptrY) { ptrX = px2; ptrY = py2; notify (); }
		if (ix != m_hoverX || iy != m_hoverY) { m_hoverX = inside ? ix : -1; m_hoverY = iy; if (g_tool == T_ERASER || g_tool == T_BRUSH) invalidate (true); }
		if (btn && !m_btn) { setFocus (); m_btn = btn; catchOutside = true; press (ix, iy, btn, mods); }
		else if (m_btn && btn) drag (ix, iy, mods);
		else if (m_btn && !btn) { drag (ix, iy, mods); release (ix, iy, mods); m_btn = 0; catchOutside = false; }
		return true;
	}

	// ---- the keys ----
	bool onKey (long k) override
	{
		unsigned mods = kapi_get_modifiers ();
		int step = mods & MOD_SHIFT ? 10 : 1;
		if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN)
		{
			if (!g_sel.empty () || D.fl.px)				// (the selection nudged)
			{
				if (!D.fl.px && !float_lift ()) return true;
				Rect old = float_rect ();
				D.fl.x += k == KEY_LEFT ? -step : k == KEY_RIGHT ? step : 0;
				D.fl.y += k == KEY_UP ? -step : k == KEY_DOWN ? step : 0;
				old.add (float_rect ()); compose (old); g_sel = float_rect ();
			}
			else { sx += (k == KEY_LEFT ? -40 : k == KEY_RIGHT ? 40 : 0); sy += (k == KEY_UP ? -40 : k == KEY_DOWN ? 40 : 0); clampScroll (); }
			invalidate (true); notify ();
			return true;
		}
		if (k == KEY_DEL || k == KEY_BACKSPACE) { deleteSelection (); return true; }
		if (k == 27) { settle (); g_sel = norect (); invalidate (true); notify (); return true; }
		if (k == KEY_PGUP || k == KEY_PGDN) { sy += (k == KEY_PGUP ? -1 : 1) * (viewH () - 40); clampScroll (); invalidate (true); return true; }
		return false;
	}

	// The selection's pixels cleared (or the floating ones dropped).
	void deleteSelection ()
	{
		if (D.fl.px) { float_discard (); invalidate (true); notify (); return; }
		Rect r = g_sel; r.clip (D.w, D.h);
		if (r.empty ()) return;
		rec_begin (); rec_touch (r);
		for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) { unsigned &q = D.lay[D.cur].px[(unsigned) y * D.w + x]; q = cleared (q); }
		rec_end (); compose (r);
		invalidate (true); notify ();
	}

private:
	int m_btn, m_drag;					// the button held (1 left, 2 right); 1 / 2 a bar's thumb, 3 a pan
	int m_ax, m_ay, m_lx, m_ly;				// the press's pixel, the last one
	int m_barHot, m_hoverX, m_hoverY;
	int *m_col, m_colCap;					// (each view column's pixel)
	int m_mode;						// Select: 0 a new rectangle, 1 moving the floating pixels
	int m_fx, m_fy;						// ... their place at the press
	bool m_eraseOpaque;

	unsigned drawColour (int btn) const { return btn == 2 ? g_col2 : g_col1; }

	void press (int ix, int iy, int btn, unsigned mods)
	{
		m_ax = m_lx = ix; m_ay = m_ly = iy;
		if (g_tool != T_SELECT) settle ();
		switch (g_tool)
		{
		case T_SELECT:
		{
			Rect f = D.fl.px ? float_rect () : g_sel;
			bool in = !f.empty () && ix >= f.x0 && ix < f.x1 && iy >= f.y0 && iy < f.y1;
			if (in && (D.fl.px || float_lift ())) { m_mode = 1; m_fx = D.fl.x; m_fy = D.fl.y; }
			else { settle (); g_sel = norect (); m_mode = 0; }
			break;
		}
		case T_PENCIL: case T_BRUSH: case T_ERASER:
		{
			m_eraseOpaque = false;					// (the eraser on an opaque background: colour 2)
			if (g_tool == T_ERASER && D.cur == 0)
			{
				m_eraseOpaque = true;
				for (int i = 0; i < D.w * D.h; i++) if ((D.lay[0].px[i] >> 24) != 255) { m_eraseOpaque = false; break; }
			}
			rec_begin ();
			stroke (ix, iy, ix, iy, btn);
			break;
		}
		case T_FILL:
		{
			Target t = target (D.lay[D.cur].px, D.w, D.h);
			rec_begin ();
			Rect r = flood (t, ix, iy, drawColour (btn), rec_touch);
			rec_end ();
			compose (r);
			break;
		}
		case T_PICKER:
			if (ix >= 0 && iy >= 0 && ix < D.w && iy < D.h)
			{
				unsigned c = D.comp[(unsigned) iy * D.w + ix];
				if (c >> 24) { if (btn == 2) g_col2 = c | 0xFF000000u; else g_col1 = c | 0xFF000000u; }
			}
			break;
		case T_ZOOM: break;
		case T_SHAPE: shapePreview (ix, iy, btn, mods); break;
		}
		invalidate (true); notify ();
	}
	void drag (int ix, int iy, unsigned mods)
	{
		if (ix == m_lx && iy == m_ly) return;
		switch (g_tool)
		{
		case T_SELECT:
			if (m_mode == 1)
			{
				Rect old = float_rect ();
				D.fl.x = m_fx + ix - m_ax; D.fl.y = m_fy + iy - m_ay;
				old.add (float_rect ()); compose (old);
				g_sel = float_rect ();
			}
			else
			{
				int x0 = pclamp (pmin (m_ax, ix), 0, D.w), y0 = pclamp (pmin (m_ay, iy), 0, D.h);
				int x1 = pclamp (pmax (m_ax, ix) + 1, 0, D.w), y1 = pclamp (pmax (m_ay, iy) + 1, 0, D.h);
				g_sel = mkrect (x0, y0, x1, y1);
			}
			break;
		case T_PENCIL: case T_BRUSH: case T_ERASER: stroke (m_lx, m_ly, ix, iy, m_btn); break;
		case T_SHAPE: shapePreview (ix, iy, m_btn, mods); break;
		default: break;
		}
		m_lx = ix; m_ly = iy;
		invalidate (true); notify ();
	}
	void release (int ix, int iy, unsigned mods)
	{
		(void) mods;
		switch (g_tool)
		{
		case T_SELECT:
			if (m_mode == 0 && ix == m_ax && iy == m_ay) g_sel = norect ();	// (a click: nothing selected)
			break;
		case T_PENCIL: case T_BRUSH: case T_ERASER: rec_end (); break;
		case T_SHAPE: shapeCommit (); break;
		case T_PICKER: g_tool = g_prevTool == T_PICKER ? T_PENCIL : g_prevTool; if (g_toolChanged) g_toolChanged (); break;
		case T_ZOOM:
		{
			int vx = toX (ix), vy = toY (iy);
			zoomStep (m_btn == 2 ? -1 : 1, vx, vy);
			break;
		}
		default: break;
		}
		invalidate (true); notify ();
	}

	// A segment of the pencil (square pixels), the brush (round), the eraser (square, to transparent).
	void stroke (int x0, int y0, int x1, int y1, int btn)
	{
		int sz = g_sizes[g_tool];
		Rect r = mkrect (pmin (x0, x1) - sz, pmin (y0, y1) - sz, pmax (x0, x1) + sz + 1, pmax (y0, y1) + sz + 1);
		rec_touch (r);
		Target t = target (D.lay[D.cur].px, D.w, D.h);
		unsigned c = g_tool == T_ERASER ? (m_eraseOpaque ? g_col2 : 0) : drawColour (btn);
		line (t, x0, y0, x1, y1, sz, g_tool == T_BRUSH, c);
		compose (t.dirty);
	}

	// The shape between the press and (ix, iy), into the overlay (put down on release).
	void shapePreview (int ix, int iy, int btn, unsigned mods)
	{
		Rect old = D.ov.r;
		for (int y = old.y0; y < old.y1; y++) for (int x = old.x0; x < old.x1; x++) D.ov.px[(unsigned) y * D.w + x] = 0;
		int x1 = ix, y1 = iy;
		if (mods & MOD_SHIFT)
		{
			int dx = x1 - m_ax, dy = y1 - m_ay, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
			if (g_shape == SH_LINE)					// (0, 45, 90 degrees)
			{
				if (ax > 2 * ay) y1 = m_ay; else if (ay > 2 * ax) x1 = m_ax;
				else { int m = pmax (ax, ay); x1 = m_ax + (dx < 0 ? -m : m); y1 = m_ay + (dy < 0 ? -m : m); }
			}
			else { int m = pmax (ax, ay); x1 = m_ax + (dx < 0 ? -m : m); y1 = m_ay + (dy < 0 ? -m : m); }
		}
		Target t = target (D.ov.px, D.w, D.h);
		unsigned oc = btn == 2 ? g_col2 : g_col1, fc = btn == 2 ? g_col1 : g_col2;
		bool outline = g_outline || g_shape == SH_LINE || !g_fillShape;
		shape (t, g_shape, m_ax, m_ay, x1, y1, g_sizes[T_SHAPE], outline, oc, g_fillShape && g_shape != SH_LINE, fc);
		D.ov.r = t.dirty; D.ov.eraser = false;
		old.add (t.dirty);
		compose (old);
	}
	void shapeCommit ()
	{
		Rect r = D.ov.r;
		if (r.empty ()) return;
		rec_begin (); rec_touch (r);
		unsigned *L = D.lay[D.cur].px;
		for (int y = r.y0; y < r.y1; y++)
			for (int x = r.x0; x < r.x1; x++)
			{
				unsigned &o = D.ov.px[(unsigned) y * D.w + x];
				if (o >> 24) L[(unsigned) y * D.w + x] = o;
				o = 0;
			}
		rec_end ();
		D.ov.r = norect ();
		compose (r);
	}

	void fillClip (int x, int y, int w, int h, unsigned c)
	{
		int x1 = pmin (x + w, viewW ()), y1 = pmin (y + h, viewH ());
		x = pmax (x, 0); y = pmax (y, 0);
		if (x1 > x && y1 > y) canvas.fillRect (x, y, x1 - x, y1 - y, c);
	}
	void frame (int x, int y, int w, int h, unsigned c)
	{ fillClip (x, y, w, 1, c); fillClip (x, y + h - 1, w, 1, c); fillClip (x, y, 1, h, c); fillClip (x + w - 1, y, 1, h, c); }
	void dashed (int x0, int y0, int x1, int y1)
	{
		auto px = [&] (int x, int y, int k) { if (x >= 0 && y >= 0 && x < viewW () && y < viewH ()) canvas.px[y * canvas.stride + x] = (k / 4) & 1 ? 0xFFFFFF : 0x000000; };
		for (int x = x0; x <= x1; x++) { px (x, y0, x - x0); px (x, y1, x - x0); }
		for (int y = y0; y <= y1; y++) { px (x0, y, y - y0); px (x1, y, y - y0); }
	}
	void gridPx (int x, int y)
	{
		unsigned &p = canvas.px[y * canvas.stride + x];
		p = wk_bright (p) > 110 ? wk_mix (p, 0x000000, 70) : wk_mix (p, 0xFFFFFF, 80);
	}
	void drawGrid (int ix0, int iy0, int ix1, int iy1)
	{
		for (int i = pmax (0, imX (ix0)); i <= D.w; i++)
		{
			int x = toX (i); if (x >= ix1) break;
			if (x < ix0) continue;
			for (int y = iy0; y < iy1; y++) gridPx (x, y);
		}
		for (int j = pmax (0, imY (iy0)); j <= D.h; j++)
		{
			int y = toY (j); if (y >= iy1) break;
			if (y < iy0) continue;
			for (int x = ix0; x < ix1; x++) gridPx (x, y);
		}
	}
	void drawBars ()
	{
		unsigned bg = C_BG;
		canvas.fillRect (viewW (), 0, SBW, height, bg);
		canvas.fillRect (0, viewH (), width, SBW, bg);
		WkThumb t = wk_thumb (docH (), viewH (), sy, viewH () - 2);
		wk_scroll_bar (canvas, viewW () + 1, 1, SBW - 2, viewH () - 2, true, t.y, t.show ? t.h : 0, bg, m_barHot == 1 || m_drag == 1 ? WK_HOT : WK_NORMAL);
		WkThumb u = wk_thumb (docW (), viewW (), sx, viewW () - 2);
		wk_scroll_bar (canvas, 1, viewH () + 1, viewW () - 2, SBW - 2, false, u.y, u.show ? u.h : 0, bg, m_barHot == 2 || m_drag == 2 ? WK_HOT : WK_NORMAL);
	}
};

} // namespace pd

#endif
