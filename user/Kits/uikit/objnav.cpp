//
// uikit/objnav.cpp -- a canvas of objects by the keys (uikit/objnav.h): the D-pad from object to object, the move and
// the size modes, their frame and their hints.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "uikit/objnav.h"
#include "uikit/paint.h"
#include "uikit/theme.h"
#include "uikit/text.h"
#include "uikit/lang.h"
#include "appkit/appkit.h"

namespace uikit {

// The object nearest in a direction, from the chosen one's middle: straight ahead first (the focus ring's measure).
static int nearest (const UkObjRect *r, int n, int sel, int dx, int dy)
{
	if (n <= 0) return -1;
	if (sel < 0 || sel >= n)				// none chosen: the first one from the top left
	{
		int best = 0;
		for (int i = 1; i < n; i++) if (r[i].y + r[i].x / 4 < r[best].y + r[best].x / 4) best = i;
		return best;
	}
	long cx = r[sel].x + r[sel].w / 2, cy = r[sel].y + r[sel].h / 2;
	int best = -1; long bestD = 0;
	for (int i = 0; i < n; i++)
	{
		if (i == sel) continue;
		long mx = r[i].x + r[i].w / 2, my = r[i].y + r[i].h / 2;
		long along = dx ? (mx - cx) * dx : (my - cy) * dy, across = dx ? my - cy : mx - cx;
		if (along <= 0) continue;
		if (across < 0) across = -across;
		long d = along + 2 * across;			// (no squares: a slide's units are large)
		if (best < 0 || d < bestD) { best = i; bestD = d; }
	}
	return best;
}

void uk_objnav_begin (UkObjNav &nav, int mode, const UkObjRect &rect)
{
	nav.mode = mode == UK_OBJ_SIZE ? UK_OBJ_SIZE : UK_OBJ_MOVE;
	nav.from = nav.now = rect;
}

void uk_objnav_end (UkObjNav &nav) { nav.mode = UK_OBJ_PICK; }

int uk_objnav_key (UkObjNav &nav, long key, const UkObjRect *rects, int n, const UkObjRect *bounds)
{
	if (nav.sel >= n) nav.sel = n - 1;
	bool arrow = key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN;
	int dx = key == KEY_LEFT ? -1 : key == KEY_RIGHT ? 1 : 0, dy = key == KEY_UP ? -1 : key == KEY_DOWN ? 1 : 0;
	bool m = key == 'm' || key == 'M', s = key == 's' || key == 'S';
	if (nav.mode == UK_OBJ_PICK)
	{
		if (arrow)
		{
			int i = nearest (rects, n, nav.sel, dx, dy);
			if (i < 0 || i == nav.sel) return UK_OBJ_USED;
			nav.sel = i;
			return UK_OBJ_PICKED;
		}
		if ((m || s) && nav.sel >= 0 && nav.sel < n && !(kapi_get_modifiers () & (MOD_CTRL | MOD_ALT)))
		{
			uk_objnav_begin (nav, s ? UK_OBJ_SIZE : UK_OBJ_MOVE, rects[nav.sel]);
			return UK_OBJ_BEGAN;
		}
		return UK_OBJ_NO;
	}
	// a mode
	if (key == KEY_ENTER) { nav.mode = UK_OBJ_PICK; return UK_OBJ_KEPT; }
	if (key == 27) { nav.mode = UK_OBJ_PICK; nav.now = nav.from; return UK_OBJ_CANCELLED; }
	if (m || s) { nav.mode = s ? UK_OBJ_SIZE : UK_OBJ_MOVE; return UK_OBJ_USED; }
	if (!arrow) return UK_OBJ_USED;				// (a mode takes every key: nothing is typed, no other command)
	int d = (kapi_get_modifiers () & MOD_SHIFT) ? nav.fine : nav.step;
	if (d < 1) d = 1;
	UkObjRect r = nav.now;
	if (nav.mode == UK_OBJ_MOVE) { r.x += dx * d; r.y += dy * d; }
	else { r.w += dx * d; r.h += dy * d; }
	if (r.w < nav.minW) r.w = nav.minW;
	if (r.h < nav.minH) r.h = nav.minH;
	if (bounds != 0)
	{
		if (nav.mode == UK_OBJ_MOVE)
		{
			if (r.x + r.w > bounds->x + bounds->w) r.x = bounds->x + bounds->w - r.w;
			if (r.y + r.h > bounds->y + bounds->h) r.y = bounds->y + bounds->h - r.h;
			if (r.x < bounds->x) r.x = bounds->x;
			if (r.y < bounds->y) r.y = bounds->y;
		}
		else
		{
			if (r.x + r.w > bounds->x + bounds->w) r.w = bounds->x + bounds->w - r.x;
			if (r.y + r.h > bounds->y + bounds->h) r.h = bounds->y + bounds->h - r.y;
			if (r.w < nav.minW) r.w = nav.minW;
			if (r.h < nav.minH) r.h = nav.minH;
		}
	}
	if (r.x == nav.now.x && r.y == nav.now.y && r.w == nav.now.w && r.h == nav.now.h) return UK_OBJ_USED;
	nav.now = r;
	return UK_OBJ_CHANGED;
}

int uk_objnav_hints (const UkObjNav &nav, UkPadHint *out, const char **mode)
{
	int n = 0;
	if (mode) *mode = 0;
	if (nav.mode == UK_OBJ_PICK)
	{
		out[n++] = { "+", TR ("object") };
		if (nav.sel >= 0) { out[n++] = { "[M]", TR ("move") }; out[n++] = { "[S]", TR ("size") }; }
		return n;
	}
	if (mode) *mode = nav.mode == UK_OBJ_MOVE ? TR ("MOVE") : TR ("SIZE");
	static char a[24], b[24];
	for (int w = 0; w < 2; w++)				// "8 px", "+ 1 px"
	{
		char *p = w ? b : a; int v = w ? nav.hintFine : nav.hintStep, k = 0; char t[12];
		if (w) { *p++ = '+'; *p++ = ' '; }
		if (v < 0) v = 0;
		do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v && k < 10);
		while (k) *p++ = t[--k];
		*p++ = ' '; *p++ = 'p'; *p++ = 'x'; *p = 0;
	}
	out[n++] = { "+", a };
	out[n++] = { "[Shift]", b };
	out[n++] = { "A", TR ("done") };
	out[n++] = { "B", TR ("cancel") };
	out[n++] = { nav.mode == UK_OBJ_MOVE ? "[S]" : "[M]", nav.mode == UK_OBJ_MOVE ? TR ("size") : TR ("move") };
	return n;
}

static void dashes (Canvas &cv, int x, int y, int len, bool vertical, unsigned c)
{
	for (int i = 0; i < len; i += 7)
	{
		int l = len - i < 4 ? len - i : 4;
		if (vertical) cv.fillRect (x, y + i, 1, l, c); else cv.fillRect (x + i, y, l, 1, c);
	}
}

static void handle (Canvas &cv, int x, int y, unsigned a)
{
	cv.fillRect (x - 3, y - 3, 7, 7, a);
	cv.fillRect (x - 2, y - 2, 5, 5, 0x00FFFFFFu);
}

void uk_objnav_draw (Canvas &cv, const UkObjNav &nav, const UkObjRect &now, const UkObjRect *from, const char *label, const UkObjRect *bounds)
{
	unsigned a = C_ACCENT;
	bool mode = nav.mode != UK_OBJ_PICK;
	if (mode && bounds != 0)				// the page's centre lines, lit while the object is on them
	{
		int cx = now.x + now.w / 2, cy = now.y + now.h / 2, bx = bounds->x + bounds->w / 2, by = bounds->y + bounds->h / 2;
		if (cx >= bx - 1 && cx <= bx + 1) dashes (cv, bx, bounds->y, bounds->h, true, a);
		if (cy >= by - 1 && cy <= by + 1) dashes (cv, bounds->x, by, bounds->w, false, a);
	}
	if (mode && from != 0 && (from->x != now.x || from->y != now.y || from->w != now.w || from->h != now.h))
		uk_rline (cv, from->x, from->y, from->w, from->h, 0, UK_FLAT ? (unsigned) UK_N_TEXT3 : C_DIS, 255);
	if (now.w <= 0 || now.h <= 0) return;
	uk_rline (cv, now.x - 1, now.y - 1, now.w + 2, now.h + 2, 0, a, 255);
	uk_rline (cv, now.x - 2, now.y - 2, now.w + 4, now.h + 4, 0, a, 255);
	int x0 = now.x - 1, x1 = now.x + now.w, xm = now.x + now.w / 2, y0 = now.y - 1, y1 = now.y + now.h, ym = now.y + now.h / 2;
	if (nav.mode == UK_OBJ_SIZE) handle (cv, x1, y1, a);	// (the size mode: the corner the D-pad moves)
	else
	{
		handle (cv, x0, y0, a); handle (cv, x1, y0, a); handle (cv, x0, y1, a); handle (cv, x1, y1, a);
		if (now.w >= 40) { handle (cv, xm, y0, a); handle (cv, xm, y1, a); }
		if (now.h >= 40) { handle (cv, x0, ym, a); handle (cv, x1, ym, a); }
	}
	if (mode && label != 0 && label[0])
	{
		int w = uk_tw (label, 2) + 16, h = 20, lx = now.x + now.w / 2 - w / 2, ly = now.y - h - 8;
		if (ly < 2) ly = now.y + now.h + 8;
		if (ly + h > cv.h - 2) ly = cv.h - h - 2;
		if (lx + w > cv.w - 2) lx = cv.w - w - 2;
		if (lx < 2) lx = 2;
		uk_rbox (cv, lx, ly, w, h, 6, a, a);
		uk_text_c (cv, lx, ly, w, h, label, 0x00FFFFFFu, 2);
	}
}

} // namespace uikit
