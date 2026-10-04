//
// view.h -- the slide being edited: its layers composited by the GPU (render.h) over Letters' grey desk,
// then what the editor adds over them: the selection (its handles, its turning handle), the smart guides
// while an object is moved (the slide's edges and middle, the other objects' edges and middles), the
// text's caret and selection, the frame being drawn by an insert tool. The mouse: select (a click, Shift
// adds, a drag around), move (the guides snap it; Alt: freely), resize (Shift: the ratio kept), turn
// (Shift: by 15 degrees), a double click edits the text (a table: its cell); the keys: the arrows nudge
// (Ctrl: finely), Delete, Tab to the next object, typing edits the text, Esc goes back out.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_view_h
#define _slides_view_h

#include "editor.h"

namespace sl {

enum { TOOL_SELECT, TOOL_TEXT, TOOL_SHAPE, TOOL_LINE, TOOL_ARROW };
static int g_tool = TOOL_SELECT, g_toolShape = SH_ROUND;
static void (*g_onTool) ();			// the toolbar shows the tool
static void (*g_onContext) (int x, int y);	// a right click (window coordinates)
static void (*g_onPictureWanted) (Object *ph);	// a picture placeholder double-clicked
static const unsigned DESK = 0x8A8684;		// Letters' grey round the page
static const unsigned GUIDE_C = 0xE23C8C;	// the smart guides
static const unsigned HANDLE_C = 0x4992A7;	// the selection (the theme's teal)

struct Geo { int id, x, y, w, h, rot; };
struct GuideLine { bool vertical; float at; float a, b; };

class SlideView : public Widget
{
public:
	Compositor C;
	Frame F;
	float sc, sx, sy;		// px a hmm, the slide's top-left in the view
	int zoom;			// % (0: fit the view)
	unsigned *tgt; int tgtW, tgtH, tgtStride;
	bool dirty;			// the frame built again
	SlideView (int l, int t, int w, int h) : Widget (l, t, w, h), sc (0.03f), sx (0), sy (0), zoom (0), tgt (0), tgtW (0), tgtH (0), tgtStride (0),
		dirty (true), m_mode (M_NONE), m_handle (-1), m_down (false), m_lastClickT (0), m_clicks (0), m_blinkT (0), m_caretOn (true), m_scrollX (0), m_scrollY (0)
	{ canFocus = true; }

	void wake () { dirty = true; invalidate (true); }
	// hmm <-> view px
	float vx (float hx) const { return sx + hx * sc; }
	float vy (float hy) const { return sy + hy * sc; }
	float hx (float px) const { return (px - sx) / sc; }
	float hy (float py) const { return (py - sy) / sc; }

	void fit ()
	{
		int W = width, H = height;
		float fs = fminf ((W - 48.0f) / g_deck.sw, (H - 40.0f) / g_deck.sh);
		if (fs < 0.001f) fs = 0.001f;
		sc = zoom ? zoom / 100.0f * (96.0f / 2540.0f) : fs;		// (100 %: 96 px an inch)
		float sw = g_deck.sw * sc, sh = g_deck.sh * sc;
		sx = sw <= W ? (W - sw) / 2 : 16 - m_scrollX; sy = sh <= H ? (H - sh) / 2 : 16 - m_scrollY;
		sx = floorf (sx); sy = floorf (sy);
	}
	int zoom_pct () const { return (int) (sc / (96.0f / 2540.0f) * 100 + 0.5f); }

	// ---- drawing -------------------------------------------------------------------------------------------------
	void onDraw () override
	{
		fit ();
		Slide *s = cur_slide ();
		if (!s) { canvas.clear (DESK); return; }
		C.init ();
		if (!tgt || tgtW != width || tgtH != height)
		{
			if (tgt) gpc_target_free (C.g, tgt);
			tgtW = width; tgtH = height;
			tgt = gpc_target_alloc (C.g, tgtW, tgtH, &tgtStride);
		}
		C.frame++;
		build_frame (C, g_deck, *s, g_cur, sc, true, F);
		// the moves in progress: the layers only shifted (nothing drawn again)
		Move mv[64]; int nmv = 0;
		if (m_mode == M_MOVE)
			for (int k = 0; k < g_sel.n && nmv < 64; k++) { mv[nmv] = move_of (g_sel[k]); mv[nmv].dx = m_dx * sc; mv[nmv].dy = m_dy * sc; nmv++; }
		// the slide's shadow, then the layers
		if (tgt)
		{
			for (int y = 0; y < tgtH; y++) { unsigned *r = tgt + (long) y * tgtStride; for (int x = 0; x < tgtW; x++) r[x] = DESK; }
			composite_frame (C, F, tgt, tgtW, tgtH, tgtStride, sx, sy, DESK, mv, nmv);
			for (int y = 0; y < height; y++) memcpy (canvas.px + (long) y * canvas.stride, tgt + (long) y * tgtStride, (size_t) width * 4);
		}
		C.sweep (3);
		fnt::trim ();
		// a soft edge round the slide
		float sw = g_deck.sw * sc, sh = g_deck.sh * sc;
		for (int k = 1; k <= 3; k++) { int a = 60 / k; frame_line (sx - k, sy - k, sw + 2 * k, sh + 2 * k, 0x000000, a); }
		draw_overlay ();
	}
	void frame_line (float x, float y, float w, float h, unsigned c, int a)
	{
		for (int i = (int) x; i < (int) (x + w); i++) { wk_blend_px (canvas, i, (int) y, c, a); wk_blend_px (canvas, i, (int) (y + h - 1), c, a); }
		for (int j = (int) y; j < (int) (y + h); j++) { wk_blend_px (canvas, (int) x, j, c, a); wk_blend_px (canvas, (int) (x + w - 1), j, c, a); }
	}
	// An object's local point (hmm, from its top-left, unrotated) -> view px (its rotation about its centre).
	void to_view (const Object &o, float lx, float ly, float *X, float *Y) const
	{
		float cx = o.x + o.w / 2.0f, cy = o.y + o.h / 2.0f;
		float px = o.x + lx - cx, py = o.y + ly - cy;
		if (m_mode == M_MOVE && is_sel (o.id)) { cx += m_dx; cy += m_dy; }
		float a = o.kind == OB_LINE ? 0 : o.rot * 0.01745329f, ca = cosf (a), sa = sinf (a);
		*X = vx (cx + px * ca - py * sa); *Y = vy (cy + px * sa + py * ca);
	}
	// view px -> an object's local point
	void to_local (const Object &o, float X, float Y, float *lx, float *ly) const
	{
		float cx = o.x + o.w / 2.0f, cy = o.y + o.h / 2.0f;
		float px = hx (X) - cx, py = hy (Y) - cy;
		float a = o.kind == OB_LINE ? 0 : -o.rot * 0.01745329f, ca = cosf (a), sa = sinf (a);
		*lx = px * ca - py * sa + o.w / 2.0f; *ly = px * sa + py * ca + o.h / 2.0f;
	}
	void quad (const Object &o, float x0, float y0, float x1, float y1, unsigned c, int alpha)	// a local rectangle filled
	{
		int pts[8]; float X, Y;
		to_view (o, x0, y0, &X, &Y); pts[0] = Q (X); pts[1] = Q (Y);
		to_view (o, x1, y0, &X, &Y); pts[2] = Q (X); pts[3] = Q (Y);
		to_view (o, x1, y1, &X, &Y); pts[4] = Q (X); pts[5] = Q (Y);
		to_view (o, x0, y1, &X, &Y); pts[6] = Q (X); pts[7] = Q (Y);
		VPath p; p.poly (pts, 4); p.fill (canvas, c, alpha);
	}
	void outline (const Object &o, unsigned c, int w16, int alpha = 255)
	{
		int pts[8]; float X, Y;
		to_view (o, 0, 0, &X, &Y); pts[0] = Q (X); pts[1] = Q (Y);
		to_view (o, (float) o.w, 0, &X, &Y); pts[2] = Q (X); pts[3] = Q (Y);
		to_view (o, (float) o.w, (float) o.h, &X, &Y); pts[4] = Q (X); pts[5] = Q (Y);
		to_view (o, 0, (float) o.h, &X, &Y); pts[6] = Q (X); pts[7] = Q (Y);
		VPath p; p.polyline (pts, 4, w16, true); p.fill (canvas, c, alpha);
	}
	void handle_at (float X, float Y, bool round = false)
	{
		VPath p;
		if (round) { p.circle (Q (X), Q (Y), Q (5)); p.fill (canvas, 0xFFFFFF); p.clear (); p.circle (Q (X), Q (Y), Q (5)); p.hole (Q (X), Q (Y), Q (3.6f)); p.fill (canvas, HANDLE_C); return; }
		p.rrect (Q (X - 4), Q (Y - 4), Q (8), Q (8), Q (1.5f)); p.fill (canvas, 0xFFFFFF);
		p.clear (); int b[8] = { Q (X - 4), Q (Y - 4), Q (X + 4), Q (Y - 4), Q (X + 4), Q (Y + 4), Q (X - 4), Q (Y + 4) }; p.polyline (b, 4, 20, true); p.fill (canvas, HANDLE_C);
	}
	// The handles: 0..7 the corners and sides (clockwise from the top-left), 8 the turning one; a line: 0, 1 its ends.
	int nhandles (const Object &o) const { return o.kind == OB_LINE ? 2 : 9; }
	void handle_pos (const Object &o, int h, float *X, float *Y) const
	{
		if (o.kind == OB_LINE)
		{
			bool end = h == 1;
			float lx = (o.flipH ? !end : end) ? (float) o.w : 0, ly = (o.flipV ? !end : end) ? (float) o.h : 0;
			to_view (o, lx, ly, X, Y); return;
		}
		static const float HX[8] = { 0, 0.5f, 1, 1, 1, 0.5f, 0, 0 }, HY[8] = { 0, 0, 0, 0.5f, 1, 1, 1, 0.5f };
		if (h == 8) { to_view (o, o.w / 2.0f, -22.0f / sc, X, Y); return; }
		to_view (o, HX[h] * o.w, HY[h] * o.h, X, Y);
	}
	void draw_overlay ()
	{
		Slide *s = cur_slide (); if (!s) return;
		// the text being edited: its selection, its caret
		if (g_edit)
		{
			Object *o = obj_of (g_edit);
			Object stand; float bx, by;
			if (o && edit_box (stand, &bx, &by))
			{
				TextLayout L; layout_object (g_deck, stand, L);
				// a frame round the text box (a cell: its own)
				Object box; box.x = (int) bx; box.y = (int) by; box.w = stand.w; box.h = stand.h; box.rot = o->rot; box.id = o->id;
				if (o->tbl) { box.x = o->x; box.y = o->y; box.w = o->w; box.h = o->h; }
				outline (box, HANDLE_C, 14, 200);
				Object &frameObj = *o;
				float ox = bx - o->x + stand.tb.inset[0], oy = by - o->y + stand.tb.inset[1];
				if (has_tsel ())
				{
					TPos a = tsel_a (), b = tsel_b ();
					for (int li = 0; li < L.line.n; li++)
					{
						const LLine &ln = L.line[li]; const LPara &P = L.pp[ln.para];
						if (ln.para < a.p || ln.para > b.p) continue;
						int s0 = ln.para == a.p ? imax (a.o, ln.a) : ln.a, s1 = ln.para == b.p ? imin (b.o, ln.b) : ln.b;
						if (ln.para == a.p && a.o > ln.b) continue;
						if (ln.para == b.p && b.o < ln.a) continue;
						if (s1 < s0) continue;
						float x0 = ln.x + P.cx[s0], x1 = ln.x + P.cx[s1];
						if (s1 == s0 || (ln.para < b.p && s1 == ln.b)) x1 += 120;
						float top = text_top (stand, L);
						quad (frameObj, ox + x0, oy + top + ln.base - ln.asc, ox + x1, oy + top + ln.base + ln.desc, 0x3D8EB9, 90);
					}
				}
				if (m_caretOn && !has_tsel ())
				{
					float cx, cy0, cy1; caret_xy (stand, L, g_caret, &cx, &cy0, &cy1);
					float w = fmaxf (1.2f / sc, 15);
					quad (frameObj, ox + cx - w / 2, oy + cy0, ox + cx + w / 2, oy + cy1, 0x101010, 255);
				}
			}
		}
		// the selection
		for (int k = 0; k < g_sel.n; k++)
		{
			Object *o = obj_of (g_sel[k]); if (!o) continue;
			if (g_edit == o->id) { if (!o->tbl) { for (int h = 0; h < nhandles (*o); h++) { float X, Y; handle_pos (*o, h, &X, &Y); if (h < 8) handle_at (X, Y); } } continue; }
			if (o->kind != OB_LINE) outline (*o, HANDLE_C, 20);
			if (m_mode == M_MOVE) continue;
			for (int h = 0; h < nhandles (*o); h++)
			{
				float X, Y; handle_pos (*o, h, &X, &Y);
				if (h == 8)
				{
					float tx, ty; handle_pos (*o, 1, &tx, &ty);
					VPath p; p.line (Q (tx), Q (ty), Q (X), Q (Y), 18); p.fill (canvas, HANDLE_C);
					p.clear (); p.circle (Q (X), Q (Y), Q (5.5f)); p.fill (canvas, 0x78C878); p.clear (); p.circle (Q (X), Q (Y), Q (5.5f)); p.hole (Q (X), Q (Y), Q (4.3f)); p.fill (canvas, 0x3C783C);
				}
				else handle_at (X, Y, o->kind == OB_LINE);
			}
		}
		// the rubber band, the frame being drawn
		if (m_mode == M_RUBBER || m_mode == M_CREATE)
		{
			float x0 = fminf (m_x0, m_x1), y0 = fminf (m_y0, m_y1), x1 = fmaxf (m_x0, m_x1), y1 = fmaxf (m_y0, m_y1);
			if (m_mode == M_CREATE && (g_tool == TOOL_LINE || g_tool == TOOL_ARROW))
			{ VPath p; p.line (Q (m_x0), Q (m_y0), Q (m_x1), Q (m_y1), 24); if (g_tool == TOOL_ARROW) p.arrowHead (Q (m_x1), Q (m_y1), -(int) (atan2f (m_y1 - m_y0, m_x1 - m_x0) * 57.3f), Q (10), Q (5)); p.fill (canvas, HANDLE_C); }
			else
			{
				VPath p; p.rect (Q (x0), Q (y0), Q (x1 - x0), Q (y1 - y0)); p.fill (canvas, HANDLE_C, 40);
				p.clear (); int b[8] = { Q (x0), Q (y0), Q (x1), Q (y0), Q (x1), Q (y1), Q (x0), Q (y1) }; p.polyline (b, 4, 16, true); p.fill (canvas, HANDLE_C);
			}
		}
		// the guides
		for (int i = 0; i < m_nguide; i++)
		{
			const GuideLine &g = m_guide[i];
			VPath p;
			if (g.vertical) { float X = vx (g.at); for (float y = vy (g.a); y < vy (g.b); y += 8) p.line (Q (X), Q (y), Q (X), Q (fminf (y + 4, vy (g.b))), 20); }
			else { float Y = vy (g.at); for (float x = vx (g.a); x < vx (g.b); x += 8) p.line (Q (x), Q (Y), Q (fminf (x + 4, vx (g.b))), Q (Y), 20); }
			p.fill (canvas, GUIDE_C);
		}
		// the position, while moving or sizing
		if ((m_mode == M_MOVE || m_mode == M_RESIZE) && g_sel.n)
		{
			Object *o = obj_of (g_sel[0]);
			if (o)
			{
				char t[64];
				if (m_mode == M_MOVE) snprintf (t, sizeof t, "X %.2f  Y %.2f cm", (o->x + m_dx) / 1000.0, (o->y + m_dy) / 1000.0);
				else snprintf (t, sizeof t, "W %.2f  H %.2f cm", o->w / 1000.0, o->h / 1000.0);
				int tw = wk_text_w (t) + 14;
				int x = imin ((int) m_mx + 14, width - tw - 4), y = imin ((int) m_my + 18, height - 24);
				wk_rbox (canvas, x, y, tw, 20, 4, 0x303030, 0x303030); wk_text_l (canvas, x + 7, y, 20, t, 0xFFFFFF);
			}
		}
	}

	// ---- hit tests -------------------------------------------------------------------------------------------------
	bool hit_obj (const Object &o, float X, float Y, float tolPx = 4) const
	{
		if (o.kind == OB_LINE)
		{
			float ax, ay, bx, by; handle_pos (o, 0, &ax, &ay); handle_pos (o, 1, &bx, &by);
			float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
			float t = l2 > 0 ? ((X - ax) * dx + (Y - ay) * dy) / l2 : 0; t = fminf (1, fmaxf (0, t));
			float ex = ax + t * dx - X, ey = ay + t * dy - Y;
			return ex * ex + ey * ey <= (tolPx + 3) * (tolPx + 3);
		}
		float lx, ly; to_local (o, X, Y, &lx, &ly);
		float t = tolPx / sc;
		return lx >= -t && ly >= -t && lx <= o.w + t && ly <= o.h + t;
	}
	Object *hit (float X, float Y)
	{
		Slide *s = cur_slide (); if (!s) return 0;
		for (int i = s->obj.n - 1; i >= 0; i--) if (hit_obj (*s->obj[i], X, Y, is_sel (s->obj[i]->id) ? 4 : 0)) return s->obj[i];
		return 0;
	}
	int hit_handle (float X, float Y, Object **which)
	{
		for (int k = 0; k < g_sel.n; k++)
		{
			Object *o = obj_of (g_sel[k]); if (!o || (o->tbl && g_edit == o->id)) continue;
			for (int h = nhandles (*o) - 1; h >= 0; h--)
			{
				if (g_edit == o->id && h == 8) continue;
				float hx_, hy_; handle_pos (*o, h, &hx_, &hy_);
				if (fabsf (X - hx_) <= 6 && fabsf (Y - hy_) <= 6) { *which = o; return h; }
			}
		}
		return -1;
	}
	// A table's cell under a point: false if none.
	bool cell_at (Object &o, float X, float Y, int *r, int *c)
	{
		if (!o.tbl) return false;
		float lx, ly; to_local (o, X, Y, &lx, &ly);
		int rh[64]; table_heights (g_deck, o, rh);
		float x = 0, y = 0; *r = *c = -1;
		for (int i = 0; i < o.tbl->cols; i++) { if (lx >= x && lx < x + o.tbl->colW[i]) *c = i; x += o.tbl->colW[i]; }
		for (int i = 0; i < o.tbl->rows && i < 64; i++) { if (ly >= y && ly < y + rh[i]) *r = i; y += rh[i]; }
		return *r >= 0 && *c >= 0;
	}
	// The text position under a point of the edited text.
	bool text_pos (float X, float Y, TPos *t)
	{
		Object *o = obj_of (g_edit); if (!o) return false;
		Object stand; float bx, by;
		if (!edit_box (stand, &bx, &by)) return false;
		float lx, ly; to_local (*o, X, Y, &lx, &ly);
		lx -= bx - o->x + stand.tb.inset[0]; ly -= by - o->y + stand.tb.inset[1];
		TextLayout L; layout_object (g_deck, stand, L);
		*t = pos_at (stand, L, lx, ly);
		return true;
	}

	// ---- snapping ----------------------------------------------------------------------------------------------------
	GuideLine m_guide[4]; int m_nguide = 0;
	// The move (dx, dy) snapped: the moving box's edges and middle to the slide's and the other objects'.
	void snap_move (float *dx, float *dy)
	{
		m_nguide = 0;
		if (kapi_get_modifiers () & 4) return;		// (Alt: free)
		float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
		for (int k = 0; k < m_ngeo; k++) { x0 = fminf (x0, (float) m_geo[k].x); y0 = fminf (y0, (float) m_geo[k].y); x1 = fmaxf (x1, (float) (m_geo[k].x + m_geo[k].w)); y1 = fmaxf (y1, (float) (m_geo[k].y + m_geo[k].h)); }
		float mvx[3] = { x0 + *dx, (x0 + x1) / 2 + *dx, x1 + *dx }, mvy[3] = { y0 + *dy, (y0 + y1) / 2 + *dy, y1 + *dy };
		Vec<float> cx, cy; Vec<float> cxa, cxb, cya, cyb;
		cx.push (0); cx.push (g_deck.sw / 2.0f); cx.push ((float) g_deck.sw);
		cy.push (0); cy.push (g_deck.sh / 2.0f); cy.push ((float) g_deck.sh);
		for (int i = 0; i < 3; i++) { cxa.push (0); cxb.push ((float) g_deck.sh); cya.push (0); cyb.push ((float) g_deck.sw); }
		Slide *s = cur_slide ();
		for (int i = 0; s && i < s->obj.n; i++)
		{
			Object *o = s->obj[i]; if (is_sel (o->id) || o->rot) continue;
			float ox[3] = { (float) o->x, o->x + o->w / 2.0f, (float) (o->x + o->w) }, oy[3] = { (float) o->y, o->y + o->h / 2.0f, (float) (o->y + o->h) };
			for (int k = 0; k < 3; k++) { cx.push (ox[k]); cxa.push ((float) o->y); cxb.push ((float) (o->y + o->h)); cy.push (oy[k]); cya.push ((float) o->x); cyb.push ((float) (o->x + o->w)); }
		}
		float tol = 7 / sc, best = tol; int bi = -1, bk = 0;
		for (int i = 0; i < cx.n; i++) for (int k = 0; k < 3; k++) { float d = fabsf (cx[i] - mvx[k]); if (d < best) { best = d; bi = i; bk = k; } }
		if (bi >= 0)
		{
			*dx += cx[bi] - mvx[bk];
			GuideLine g; g.vertical = true; g.at = cx[bi]; g.a = fminf (cxa[bi], y0 + *dy) - 300; g.b = fmaxf (cxb[bi], y1 + *dy) + 300;
			if (bi < 3) { g.a = 0; g.b = (float) g_deck.sh; }
			m_guide[m_nguide++] = g;
		}
		best = tol; bi = -1;
		for (int i = 0; i < cy.n; i++) for (int k = 0; k < 3; k++) { float d = fabsf (cy[i] - mvy[k]); if (d < best) { best = d; bi = i; bk = k; } }
		if (bi >= 0)
		{
			*dy += cy[bi] - mvy[bk];
			GuideLine g; g.vertical = false; g.at = cy[bi]; g.a = fminf (cya[bi], x0 + *dx) - 300; g.b = fmaxf (cyb[bi], x1 + *dx) + 300;
			if (bi < 3) { g.a = 0; g.b = (float) g_deck.sw; }
			m_guide[m_nguide++] = g;
		}
	}

	// ---- the mouse ----------------------------------------------------------------------------------------------------
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		m_mx = (float) mx; m_my = (float) my;
		if (wheel && in)
		{
			if (kapi_get_modifiers () & MOD_CTRL) { set_zoom (zoom_pct () + (wheel > 0 ? 10 : -10)); return true; }
			float sh = g_deck.sh * sc;
			if (sh > height) { m_scrollY = fmaxf (0, fminf (sh + 32 - height, m_scrollY - wheel * 40)); invalidate (true); }
			else if (g_onWheelSlide) g_onWheelSlide (wheel > 0 ? -1 : 1);
			return true;
		}
		if (br && in && !m_rdown) { m_rdown = true; setFocus (); Object *o = hit ((float) mx, (float) my); if (o && !is_sel (o->id)) { end_edit (); g_sel.clear (); g_sel.push (o->id); notify (); invalidate (true); } if (g_onContext) { int ax = mx, ay = my; for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; } g_onContext (ax, ay); } return true; }
		if (!br) m_rdown = false;
		if (bl && !m_down)
		{
			if (!in) return false;
			m_down = true; setFocus ();
			unsigned now = kapi_get_ticks ();
			bool dbl = now - m_lastClickT < 40 && fabsf (mx - m_lastX) < 5 && fabsf (my - m_lastY) < 5;
			m_clicks = dbl ? m_clicks + 1 : 1;
			m_lastClickT = now; m_lastX = (float) mx; m_lastY = (float) my;
			press ((float) mx, (float) my, dbl);
			return true;
		}
		if (bl && m_down) { drag ((float) mx, (float) my); return true; }
		if (!bl && m_down) { m_down = false; release ((float) mx, (float) my); return true; }
		return in;
	}
	void press (float X, float Y, bool dbl)
	{
		bool shift = kapi_get_modifiers () & MOD_SHIFT;
		m_x0 = m_x1 = X; m_y0 = m_y1 = Y; m_dx = m_dy = 0; m_nguide = 0;
		// an insert tool: a frame drawn
		if (g_tool != TOOL_SELECT) { m_mode = M_CREATE; return; }
		// the edited text: the caret placed, a selection begun
		if (g_edit)
		{
			Object *eo = obj_of (g_edit);
			Object *h = 0; int hh = hit_handle (X, Y, &h);
			if (eo && hh < 0 && hit_obj (*eo, X, Y))
			{
				if (eo->tbl) { int r, c; if (cell_at (*eo, X, Y, &r, &c) && (r != g_cellR || c != g_cellC)) { begin_edit (eo->id, r, c); } }
				TPos t; if (text_pos (X, Y, &t))
				{
					if (m_clicks == 2) { select_word (t); }
					else if (m_clicks >= 3) { TextBody *tb = edit_body (); g_anchor = tpos (t.p, 0); g_caret = tpos (t.p, tb->p[t.p]->len); }
					else { g_caret = t; if (!shift) g_anchor = t; }
					g_goalX = -1; g_typeSet = false;
				}
				m_mode = M_TEXTSEL; blink_reset (); notify (); invalidate (true);
				return;
			}
			if (hh < 0) { end_edit (); notify (); }
		}
		// a handle
		Object *ho = 0; int h = hit_handle (X, Y, &ho);
		if (h >= 0)
		{
			m_handle = h; m_mode = h == 8 ? M_ROTATE : M_RESIZE; m_hobj = ho->id;
			save_geo (); begin_change (UK_MOVE);
			return;
		}
		Object *o = hit (X, Y);
		if (o && dbl)
		{
			// a double click: the text edited (a picture placeholder: a picture asked for)
			if (o->ph == PH_PICTURE && o->kind != OB_PICTURE) { if (g_onPictureWanted) g_onPictureWanted (o); m_mode = M_NONE; return; }
			if (o->kind == OB_TEXT || o->kind == OB_SHAPE || o->kind == OB_TABLE)
			{
				int r = -1, c = -1;
				if (o->tbl && !cell_at (*o, X, Y, &r, &c)) return;
				begin_edit (o->id, r, c);
				TPos t; if (text_pos (X, Y, &t)) { g_caret = g_anchor = t; }
				m_mode = M_TEXTSEL; blink_reset (); notify (); invalidate (true);
				return;
			}
		}
		if (o)
		{
			if (shift) { int i = g_sel.find (o->id); if (i >= 0) g_sel.erase (i); else g_sel.push (o->id); m_mode = M_NONE; notify (); invalidate (true); return; }
			m_wasSel = is_sel (o->id) && g_sel.n == 1;
			if (!is_sel (o->id)) { g_sel.clear (); g_sel.push (o->id); }
			m_mode = M_MOVE; save_geo ();
			notify (); invalidate (true);
			return;
		}
		if (!shift) g_sel.clear ();
		m_mode = M_RUBBER;
		notify (); invalidate (true);
	}
	void drag (float X, float Y)
	{
		m_x1 = X; m_y1 = Y;
		bool shift = kapi_get_modifiers () & MOD_SHIFT;
		switch (m_mode)
		{
		case M_MOVE:
		{
			float dx = (X - m_x0) / sc, dy = (Y - m_y0) / sc;
			if (fabsf (X - m_x0) < 3 && fabsf (Y - m_y0) < 3 && m_dx == 0 && m_dy == 0) return;
			if (shift) { if (fabsf (dx) > fabsf (dy)) dy = 0; else dx = 0; }
			snap_move (&dx, &dy);
			m_dx = dx; m_dy = dy;
			invalidate (true);
			break;
		}
		case M_RESIZE: resize_to (X, Y, shift); dirty = true; invalidate (true); break;
		case M_ROTATE:
		{
			Object *o = obj_of (m_hobj); if (!o) break;
			float cx = vx (o->x + o->w / 2.0f), cy = vy (o->y + o->h / 2.0f);
			int a = (int) (atan2f (X - cx, cy - Y) * 57.29578f);
			if (shift) a = (a + (a >= 0 ? 7 : -7)) / 15 * 15;
			a = ((a % 360) + 360) % 360;
			o->rot = (short) a;
			invalidate (true);
			break;
		}
		case M_TEXTSEL:
		{
			TPos t; if (text_pos (X, Y, &t)) { g_caret = t; blink_reset (); invalidate (true); }
			break;
		}
		default: invalidate (true);
		}
	}
	void release (float X, float Y)
	{
		int mode = m_mode; m_mode = M_NONE; m_nguide = 0;
		switch (mode)
		{
		case M_MOVE:
			if (m_dx != 0 || m_dy != 0)
			{
				float dx = m_dx, dy = m_dy;
				begin_change (UK_MOVE);
				for (int k = 0; k < g_sel.n; k++) { Object *o = obj_of (g_sel[k]); if (o) { o->x += (int) lroundf (dx); o->y += (int) lroundf (dy); } }
				m_dx = m_dy = 0;
				done_change (); if (g_onSlides) g_onSlides ();
			}
			else if (m_clicks == 1)
			{
				// a click on a selected text object again: its text edited (as in PowerPoint)
				Object *o = hit (X, Y);
				if (o && g_sel.n == 1 && g_sel[0] == o->id && m_wasSel && (o->kind == OB_TEXT || (o->kind == OB_SHAPE)))
				{ begin_edit (o->id); TPos t; if (text_pos (X, Y, &t)) g_caret = g_anchor = t; blink_reset (); notify (); }
			}
			break;
		case M_RESIZE: case M_ROTATE: done_change (); if (g_onSlides) g_onSlides (); break;
		case M_RUBBER:
		{
			float x0 = hx (fminf (m_x0, X)), y0 = hy (fminf (m_y0, Y)), x1 = hx (fmaxf (m_x0, X)), y1 = hy (fmaxf (m_y0, Y));
			Slide *s = cur_slide ();
			if (s && fabsf (X - m_x0) > 3)
				for (int i = 0; i < s->obj.n; i++) { Object *o = s->obj[i]; if (o->x >= x0 && o->y >= y0 && o->x + o->w <= x1 && o->y + o->h <= y1 && !is_sel (o->id)) g_sel.push (o->id); }
			notify ();
			break;
		}
		case M_CREATE: create (X, Y); break;
		}
		dirty = true;
		invalidate (true);
	}
	void create (float X, float Y)
	{
		bool small = fabsf (X - m_x0) < 4 && fabsf (Y - m_y0) < 4;
		int x0 = (int) hx (fminf (m_x0, X)), y0 = (int) hy (fminf (m_y0, Y)), x1 = (int) hx (fmaxf (m_x0, X)), y1 = (int) hy (fmaxf (m_y0, Y));
		Object *o = 0;
		if (g_tool == TOOL_LINE || g_tool == TOOL_ARROW)
		{
			if (small) { x0 = (int) hx (m_x0); y0 = (int) hy (m_y0); o = make_line (x0, y0, x0 + 5000, y0, g_tool == TOOL_ARROW); }
			else o = make_line ((int) hx (m_x0), (int) hy (m_y0), (int) hx (X), (int) hy (Y), g_tool == TOOL_ARROW);
		}
		else if (g_tool == TOOL_TEXT)
		{
			if (small) { x1 = x0 + 8000; y1 = y0 + 1200; }
			o = make_text_box (x0, y0, x1 - x0, imax (y1 - y0, 800));
		}
		else
		{
			if (small) { x1 = x0 + 5000; y1 = y0 + 3000; }
			if (kapi_get_modifiers () & MOD_SHIFT) { int s = imax (x1 - x0, y1 - y0); x1 = x0 + s; y1 = y0 + s; }
			o = make_shape (g_toolShape, x0, y0, imax (x1 - x0, 200), imax (y1 - y0, 200));
		}
		bool edit = g_tool == TOOL_TEXT;
		g_tool = TOOL_SELECT; if (g_onTool) g_onTool ();
		if (o) add_object (o, edit);
		blink_reset ();
	}
	void save_geo ()
	{
		m_ngeo = 0;
		for (int k = 0; k < g_sel.n && m_ngeo < 64; k++) { Object *o = obj_of (g_sel[k]); if (!o) continue; Geo g; g.id = o->id; g.x = o->x; g.y = o->y; g.w = o->w; g.h = o->h; g.rot = o->rot; m_geo[m_ngeo++] = g; }
	}
	void resize_to (float X, float Y, bool keep)
	{
		Object *o = obj_of (m_hobj); if (!o) return;
		Geo g; bool found = false;
		for (int k = 0; k < m_ngeo; k++) if (m_geo[k].id == o->id) { g = m_geo[k]; found = true; }
		if (!found) return;
		if (o->kind == OB_LINE)
		{
			// the end dragged; the other kept
			float ax, ay;
			bool end = m_handle == 1;
			// the fixed end (hmm), from the original geometry
			bool fx = o->flipH, fy = o->flipV;
			float ex0 = (float) ((fx ? !end : end) ? g.x : g.x + g.w), ey0 = (float) ((fy ? !end : end) ? g.y : g.y + g.h);
			// (the fixed end is the other one)
			ex0 = (float) (((fx ? end : !end)) ? g.x + g.w : g.x); ey0 = (float) (((fy ? end : !end)) ? g.y + g.h : g.y);
			ax = hx (X); ay = hy (Y);
			if (keep) { if (fabsf (ax - ex0) > fabsf (ay - ey0)) ay = ey0; else ax = ex0; }
			float sx0 = end ? ex0 : ax, sy0 = end ? ey0 : ay, sx1 = end ? ax : ex0, sy1 = end ? ay : ey0;
			o->x = (int) fminf (sx0, sx1); o->y = (int) fminf (sy0, sy1); o->w = (int) fabsf (sx1 - sx0); o->h = (int) fabsf (sy1 - sy0);
			o->flipH = sx1 < sx0; o->flipV = sy1 < sy0;
			return;
		}
		// in the object's own axes: the side or corner dragged, the opposite one kept
		Object t; t.x = g.x; t.y = g.y; t.w = g.w; t.h = g.h; t.rot = (short) g.rot; t.kind = o->kind;
		float lx, ly; to_local (t, X, Y, &lx, &ly);
		static const int SX[8] = { -1, 0, 1, 1, 1, 0, -1, -1 }, SY[8] = { -1, -1, -1, 0, 1, 1, 1, 0 };
		int sxh = SX[m_handle], syh = SY[m_handle];
		float L0 = 0, T0 = 0, R0 = (float) g.w, B0 = (float) g.h;
		if (sxh < 0) L0 = fminf (lx, R0 - 100); if (sxh > 0) R0 = fmaxf (lx, L0 + 100);
		if (syh < 0) T0 = fminf (ly, B0 - 100); if (syh > 0) B0 = fmaxf (ly, T0 + 100);
		bool ratio = keep || (o->kind == OB_PICTURE && sxh && syh);
		if (ratio && sxh && syh && g.h > 0)
		{
			float ar = (float) g.w / g.h, nw = R0 - L0, nh = B0 - T0;
			if (nw / nh > ar) nw = nh * ar; else nh = nw / ar;
			if (sxh < 0) L0 = R0 - nw; else R0 = L0 + nw;
			if (syh < 0) T0 = B0 - nh; else B0 = T0 + nh;
		}
		// the new box's centre back in the slide's axes (the rotation about the old centre)
		float ncx = (L0 + R0) / 2 - g.w / 2.0f, ncy = (T0 + B0) / 2 - g.h / 2.0f;
		float a = g.rot * 0.01745329f, ca = cosf (a), sa = sinf (a);
		float wcx = g.x + g.w / 2.0f + ncx * ca - ncy * sa, wcy = g.y + g.h / 2.0f + ncx * sa + ncy * ca;
		o->w = (int) (R0 - L0); o->h = (int) (B0 - T0);
		o->x = (int) (wcx - o->w / 2.0f); o->y = (int) (wcy - o->h / 2.0f);
		if (o->kind == OB_TABLE && o->tbl && g.w > 0)			// (a table: its columns scaled)
		{
			int sum = 0; for (int c = 0; c < o->tbl->cols; c++) sum += o->tbl->colW[c];
			for (int c = 0; c < o->tbl->cols; c++) o->tbl->colW[c] = o->tbl->colW[c] * o->w / (sum ? sum : 1);
		}
	}
	void select_word (TPos t)
	{
		TextBody *tb = edit_body (); if (!tb) return;
		const Para *q = tb->p[t.p];
		int a = t.o, b = t.o;
		while (a > 0 && word_ch (q->ch[a - 1])) a--;
		while (b < q->len && word_ch (q->ch[b])) b++;
		g_anchor = tpos (t.p, a); g_caret = tpos (t.p, b);
	}

	// ---- the keys ------------------------------------------------------------------------------------------------------
	bool onKey (long k) override
	{
		unsigned mods = kapi_get_modifiers ();
		bool shift = mods & MOD_SHIFT, ctrl = mods & MOD_CTRL;
		if (g_edit) return text_key (k, shift, ctrl);
		Slide *s = cur_slide (); if (!s) return false;
		if (k == 27) { if (g_tool != TOOL_SELECT) { g_tool = TOOL_SELECT; if (g_onTool) g_onTool (); } else g_sel.clear (); notify (); invalidate (true); return true; }
		if (k == KEY_DEL || k == KEY_BACKSPACE) { if (g_sel.n) { cmd_delete_objects (); invalidate (true); } return true; }
		if (k == KEY_TAB)
		{
			if (!s->obj.n) return true;
			int i = g_sel.n ? s->index_of (g_sel[0]) : -1;
			i = shift ? (i <= 0 ? s->obj.n - 1 : i - 1) : (i + 1) % s->obj.n;
			g_sel.clear (); g_sel.push (s->obj[i]->id); notify (); invalidate (true);
			return true;
		}
		if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN)
		{
			if (!g_sel.n) { if (g_onWheelSlide) g_onWheelSlide (k == KEY_LEFT || k == KEY_UP ? -1 : 1); return true; }
			int step = ctrl ? 20 : 250;
			int dx = k == KEY_LEFT ? -step : k == KEY_RIGHT ? step : 0, dy = k == KEY_UP ? -step : k == KEY_DOWN ? step : 0;
			begin_change (UK_MOVE);
			for (int q = 0; q < g_sel.n; q++) { Object *o = obj_of (g_sel[q]); if (o) { o->x += dx; o->y += dy; } }
			done_change (); if (g_onSlides) g_onSlides (); invalidate (true);
			return true;
		}
		if (k == KEY_PGDN || k == KEY_PGUP) { if (g_onWheelSlide) g_onWheelSlide (k == KEY_PGUP ? -1 : 1); return true; }
		if (k == KEY_ENTER || k == KEY_F1 + 1)		// (Enter, F2: the selected object's text edited)
		{
			Object *o = sel_one ();
			if (o && (o->kind == OB_TEXT || o->kind == OB_SHAPE)) { begin_edit (o->id); blink_reset (); notify (); invalidate (true); }
			else if (o && o->tbl) { begin_edit (o->id, 0, 0); blink_reset (); notify (); invalidate (true); }
			return true;
		}
		// typing with a text object selected: its text edited (at its end)
		if (k >= 32 && k < 0x100 && k != 127)
		{
			Object *o = sel_one ();
			if (o && (o->kind == OB_TEXT || o->kind == OB_SHAPE))
			{
				begin_edit (o->id);
				// an empty placeholder: typed into; else added at the end
				return text_key (k, shift, ctrl);
			}
		}
		return false;
	}
	bool text_key (long k, bool shift, bool ctrl)
	{
		TextBody *tb = edit_body (); if (!tb) { end_edit (); return false; }
		blink_reset ();
		Object stand; float bx, by; edit_box (stand, &bx, &by);
		TextLayout L; layout_object (g_deck, stand, L);
		auto moved = [&] (TPos t) { g_caret = t; if (!shift) g_anchor = t; g_typeSet = false; notify (); invalidate (true); };
		switch (k)
		{
		case 27: end_edit (); notify (); invalidate (true); return true;
		case KEY_LEFT:
			if (has_tsel () && !shift) { moved (tsel_a ()); return true; }
			if (g_caret.o > 0) { int o = g_caret.o - 1; if (ctrl) { const Para *q = tb->p[g_caret.p]; while (o > 0 && !word_ch (q->ch[o])) o--; while (o > 0 && word_ch (q->ch[o - 1])) o--; } moved (tpos (g_caret.p, o)); }
			else if (g_caret.p > 0) moved (tpos (g_caret.p - 1, tb->p[g_caret.p - 1]->len));
			g_goalX = -1; return true;
		case KEY_RIGHT:
			if (has_tsel () && !shift) { moved (tsel_b ()); return true; }
			if (g_caret.o < tb->p[g_caret.p]->len) { int o = g_caret.o + 1; if (ctrl) { const Para *q = tb->p[g_caret.p]; while (o < q->len && word_ch (q->ch[o])) o++; while (o < q->len && !word_ch (q->ch[o])) o++; } moved (tpos (g_caret.p, o)); }
			else if (g_caret.p + 1 < tb->p.n) moved (tpos (g_caret.p + 1, 0));
			g_goalX = -1; return true;
		case KEY_UP: case KEY_DOWN:
		{
			float cx, y0, y1; caret_xy (stand, L, g_caret, &cx, &y0, &y1);
			if (g_goalX < 0) g_goalX = cx;
			int li = line_of (L, g_caret) + (k == KEY_UP ? -1 : 1);
			if (li < 0) { moved (tpos (0, 0)); return true; }
			if (li >= L.line.n) { int lp = tb->p.n - 1; moved (tpos (lp, tb->p[lp]->len)); return true; }
			float top = text_top (stand, L);
			float g = g_goalX; TPos t = pos_at (stand, L, g, top + L.line[li].base);
			moved (t); g_goalX = g;
			return true;
		}
		case KEY_HOME: { int li = line_of (L, g_caret); moved (ctrl ? tpos (0, 0) : tpos (g_caret.p, L.line.n ? L.line[li].a : 0)); g_goalX = -1; return true; }
		case KEY_END:
		{
			if (ctrl) { int lp = tb->p.n - 1; moved (tpos (lp, tb->p[lp]->len)); return true; }
			int li = line_of (L, g_caret);
			int e = L.line.n ? L.line[li].b : tb->p[g_caret.p]->len;
			if (L.line.n && !L.line[li].last && e > L.line[li].a) e--;
			moved (tpos (g_caret.p, e)); g_goalX = -1; return true;
		}
		case KEY_BACKSPACE: case KEY_DEL:
		{
			begin_change (UK_TYPE);
			if (has_tsel ()) { TPos a = tsel_a (); tb_delete (*tb, a, tsel_b ()); g_caret = g_anchor = a; }
			else if (k == KEY_BACKSPACE)
			{
				if (g_caret.o > 0) { TPos a = tpos (g_caret.p, g_caret.o - 1); tb_delete (*tb, a, g_caret); g_caret = g_anchor = a; }
				else if (g_caret.p > 0)
				{
					// at a list paragraph's start: its level out first, then its bullet off
					Para *q = tb->p[g_caret.p];
					if (q->pf.level > 0) q->pf.level--;
					else { TPos a = tpos (g_caret.p - 1, tb->p[g_caret.p - 1]->len); tb_delete (*tb, a, g_caret); g_caret = g_anchor = a; }
				}
			}
			else
			{
				if (g_caret.o < tb->p[g_caret.p]->len) tb_delete (*tb, g_caret, tpos (g_caret.p, g_caret.o + 1));
				else if (g_caret.p + 1 < tb->p.n) tb_delete (*tb, g_caret, tpos (g_caret.p + 1, 0));
			}
			g_typeSet = false;
			done_change (); invalidate (true);
			return true;
		}
		case KEY_TAB:
		{
			// a list's level (a body; a paragraph's start or a selection), else a tab character
			Object *o = obj_of (g_edit);
			bool list = o && (o->ph == PH_BODY || o->ph == PH_BODY2 || shown_para ().bullet != BU_NONE);
			if (o && o->tbl && g_cellR >= 0 && !ctrl)
			{
				int r = g_cellR, c = g_cellC + (shift ? -1 : 1);
				if (c >= o->tbl->cols) { c = 0; r++; } if (c < 0) { c = o->tbl->cols - 1; r--; }
				if (r >= o->tbl->rows) { begin_change (); add_table_row (o); done_change (); }
				if (r >= 0) { begin_edit (o->id, r, c); TextBody *nb = edit_body (); g_anchor = tpos (0, 0); g_caret = tpos (nb->p.n - 1, nb->p[nb->p.n - 1]->len); }
				notify (); invalidate (true);
				return true;
			}
			if (list && (has_tsel () || g_caret.o == 0)) { apply_para (pf_level, shift ? -1 : 1); invalidate (true); return true; }
			if (shift) return true;
			unsigned t = '\t'; type (&t, 1); return true;
		}
		case KEY_ENTER:
		{
			if (shift) { unsigned n = 0x0B; (void) n; }			// (a line break: a new paragraph in this version)
			unsigned n = '\n'; type (&n, 1); return true;
		}
		}
		if (ctrl && k == WK_CTRL ('A')) { int lp = tb->p.n - 1; g_anchor = tpos (0, 0); g_caret = tpos (lp, tb->p[lp]->len); notify (); invalidate (true); return true; }
		if (k >= 32 && k != 127 && k < 0x110000 && !(k >= 0x100 && k < 0x120))
		{
			unsigned c = (unsigned) k;
			type (&c, 1);
			return true;
		}
		return false;
	}
	// Text typed at the caret (the selection replaced), in the format chosen for it.
	void type (const unsigned *s, int n)
	{
		TextBody *tb = edit_body (); if (!tb) return;
		begin_change (UK_TYPE);
		if (has_tsel ()) { TPos a = tsel_a (); tb_delete (*tb, a, tsel_b ()); g_caret = g_anchor = a; }
		CharFmt f = g_typeSet ? g_typeFmt : tb->p[g_caret.p]->fmt_at (g_caret.o);
		g_caret = g_anchor = tb_insert (*tb, g_caret, s, n, f);
		g_goalX = -1;
		done_change (); invalidate (true);
	}
	static void add_table_row (Object *o)
	{
		Table *t = o->tbl, *n = new Table (t->rows + 1, t->cols);
		for (int c = 0; c < t->cols; c++) n->colW[c] = t->colW[c];
		for (int r = 0; r < t->rows; r++) { n->rowH[r] = t->rowH[r]; for (int c = 0; c < t->cols; c++) { n->at (r, c).copy_from (t->at (r, c)); n->cfill[r * t->cols + c] = t->cfill[r * t->cols + c]; } }
		n->rowH[t->rows] = t->rowH[t->rows - 1];
		for (int c = 0; c < t->cols; c++) { n->at (t->rows, c).copy_from (t->at (t->rows - 1, c)); TextBody &b = n->at (t->rows, c); for (int i = 0; i < b.p.n; i++) b.p[i]->len = 0; while (b.p.n > 1) { delete b.p[b.p.n - 1]; b.p.n--; } }
		n->header = t->header; n->banded = t->banded;
		delete t; o->tbl = n;
	}

	// ---- the caret's blink ---------------------------------------------------------------------------------------------
	void blink_reset () { m_blinkT = kapi_get_ticks (); m_caretOn = true; }
	void tick ()
	{
		if (!g_edit) return;
		unsigned t = kapi_get_ticks ();
		bool on = ((t - m_blinkT) / 53) % 2 == 0;
		if (on != m_caretOn && hasFocus) { m_caretOn = on; invalidate (true); }
	}
	void set_zoom (int z) { zoom = z <= 0 ? 0 : iclamp (z, 10, 400); m_scrollX = m_scrollY = 0; invalidate (true); if (g_onZoom) g_onZoom (); }
	static void (*g_onZoom) ();
	static void (*g_onWheelSlide) (int dir);
	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); invalidate (true); }

	enum { M_NONE, M_MOVE, M_RESIZE, M_ROTATE, M_RUBBER, M_CREATE, M_TEXTSEL };
	int m_mode, m_handle, m_hobj;
	bool m_down, m_rdown = false, m_wasSel = false;
	float m_x0, m_y0, m_x1, m_y1, m_dx, m_dy, m_mx = 0, m_my = 0, m_lastX = 0, m_lastY = 0;
	unsigned m_lastClickT; int m_clicks;
	unsigned m_blinkT; bool m_caretOn;
	float m_scrollX, m_scrollY;
	Geo m_geo[64]; int m_ngeo = 0;
};
void (*SlideView::g_onZoom) ();
void (*SlideView::g_onWheelSlide) (int);

} // namespace sl

#endif
