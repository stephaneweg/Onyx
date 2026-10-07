//
// appkit_ws.inc -- AppKit's window calls: each kapi_* window call's body (appkit_calls.inc's KAPI_WS),
// speaking the protocol of elegant.h to Elegant, the graphics server -- the windows are no longer the
// kernel's (2026-10-05). Compiled into AppKit only (appkit.c): no program is rebuilt, none sees it.
//
// A call that finds no server (it is being started, or started again) waits for it, 5 s at most.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "elegant.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

#define WS_KT	((const struct TKApiTable *) KAPI_TABLE_VA)

// (v94) A program's windows: its first (0, the one it always had) and its others (1 .. EL_WINDOWS_MORE:
// kapi_win_new); the window calls act on the one kapi_win_select chose (0 until then). What each one
// is, kept for Elegant started again (below).
struct ws_win
{
	int	 made;				// the program has (asked for) it
	int	 cw, ch;			// its canvas: its row in pixels, its rows (kapi_draw_text)
	int	 x, y, lw, lh;			// where it was asked, its client size (0: as made)
	unsigned flags;
	char	 title[64];
	long	 handler[3]; int handler_set;	// (bits: EL_HANDLER_*)
	int	 alpha, cursor;
	int	 sizable_set, sizable, min_w, min_h;
};
static struct ws_win ws__w[EL_WINDOWS_MORE + 1];
static int ws__cur;				// the window the calls act on
static int ws__lost;				// no server came back: the calls fail at once

#define ws__cw	(ws__w[ws__cur].cw)
#define ws__ch	(ws__w[ws__cur].ch)

static unsigned *ws_canvas_va (int win)		// where a window's canvas is in this program
{
	return win == 0 ? (unsigned *) EL_VA_CANVAS : (unsigned *) EL_VA_WIN (win, 0);
}

static long ws_raw (int op, long a0, long a1, long a2, long a3, const void *in, unsigned in_len, void *out, unsigned cap)
{
	struct kapi_ws_call c;
	__builtin_memset (&c, 0, sizeof c);
	c.op = op | ws__cur << EL_OP_WINDOW_SHIFT; c.in = in; c.in_len = in_len; c.out = out; c.out_cap = cap;
	c.a[0] = a0; c.a[1] = a1; c.a[2] = a2; c.a[3] = a3;
	long r = WS_KT->ws_ctl (KAPI_WS_CALL, (long) &c, 0, 0);
	if (r != -KAPI_ESRCH || ws__lost) return r;
	int t = 0;				// no server: the kernel is starting it (5 s at most)
	for (; t < 100 && WS_KT->ws_ctl (KAPI_WS_ACTIVE, 0, 0, 0) <= 0; t++) WS_KT->msleep (50);
	if (t == 100) { ws__lost = 1; return r; }
	return WS_KT->ws_ctl (KAPI_WS_CALL, (long) &c, 0, 0);
}

// ---- Elegant started again ----------------------------------------------------------------------
// The kernel starts Elegant again when it ends. The new one knows nothing of this program's windows
// (but its first one, kept in the kernel): what the program asked for is kept here and asked again
// (each window with its flags and title, its size, its handlers, its menu (the first window's), its
// opacity, its frame's resizing, its pointer); the first window's pixels are the same memory (the
// kernel kept the buffers: the new server takes them as they are) and it goes back where it was (the
// server kept its place in the kernel). Seen at the program's next call: a request that finds no
// server or no window, or a present answered by another server.
static char ws__menu[2048]; static long ws__menu_handler; static int ws__menu_set;	// (the first window's)
static long ws__server;				// the server's pid, as the last present saw it
static int ws__replaying;

static unsigned ws_len (const char *s);

static volatile struct el_shot *ws__shot;	// (the first window as last presented: below, ws_shot_ask)
static unsigned char *ws__shot_copy;
static unsigned ws__shot_cap;
static void ws_shot_ask (void);

static void ws_replay_one (void)		// (the current window)
{
	struct ws_win *W = &ws__w[ws__cur];
	if (!W->made) return;
	if (ws__cur == 0) ws__shot = 0;
	struct el_create c;
	__builtin_memset (&c, 0, sizeof c);
	c.flags = W->flags;
	for (unsigned i = 0; W->title[i] != 0 && i + 1 < sizeof c.title; i++) c.title[i] = W->title[i];
	if (ws_raw (EL_OP_CREATE, W->x, W->y, W->cw, W->ch, &c, sizeof c, 0, 0) == 1)
	{
		if (W->lw > 0) ws_raw (EL_OP_RESIZE, W->lw, W->lh, 0, 0, 0, 0, 0, 0);
		for (int k = 0; k < 3; k++)
			if (W->handler_set & (1 << k)) ws_raw (EL_OP_HANDLER, k, W->handler[k], 0, 0, 0, 0, 0, 0);
		if (ws__cur == 0 && ws__menu_set) ws_raw (EL_OP_MENU_SET, ws__menu_handler, 0, 0, 0, ws__menu, ws_len (ws__menu), 0, 0);
		if (W->alpha >= 0) ws_raw (EL_OP_ALPHA, W->alpha, 0, 0, 0, 0, 0, 0, 0);
		if (W->sizable_set) ws_raw (EL_OP_RESIZABLE, W->sizable, W->min_w, W->min_h, 0, 0, 0, 0, 0);
		if (W->cursor >= 0) ws_raw (EL_OP_CURSOR, W->cursor, 0, 0, 0, 0, 0, 0, 0);
		if (ws__cur == 0) ws_shot_ask ();
	}
}

static struct el_tray ws__tray; static long ws__tray_handler; static int ws__tray_set;	// (v95: the status area's icon)

static void ws_replay (int all)
{
	if (ws__replaying) return;
	ws__replaying = 1;
	if (all && ws__tray_set) ws_raw (EL_OP_TRAY_SET, ws__tray_handler, 0, 0, 0, &ws__tray, sizeof ws__tray, 0, 0);
	int cur = ws__cur;
	for (int w = 0; w <= EL_WINDOWS_MORE; w++)
	{
		if (!ws__w[w].made || (!all && w != cur)) continue;
		ws__cur = w;
		ws_replay_one ();
	}
	ws__cur = cur;
	ws__replaying = 0;
}

static long ws_call (int op, long a0, long a1, long a2, long a3, const void *in, unsigned in_len, void *out, unsigned cap)
{
	long r = ws_raw (op, a0, a1, a2, a3, in, in_len, out, cap);
	if (r != EL_E_NOWINDOW || !ws__w[ws__cur].made || ws__replaying || ws__lost) return r;
	ws_replay (0);				// (a server started again that does not have this window)
	return ws_raw (op, a0, a1, a2, a3, in, in_len, out, cap);
}

// The window as last presented, for who reads it from outside (elegant.h, EL_OP_SHOT): where the
// control page and the copy are in the canvas' memory (asked after the window is made or grown: 0
// before, and with an Elegant that has none); the copy made at each present Elegant wants.
static void ws_shot_ask (void)
{
	struct el_shot_info i;
	ws__shot = 0;
	if (ws_raw (EL_OP_SHOT, 0, 0, 0, 0, 0, 0, &i, sizeof i) != 1 || i.cap == 0) return;
	ws__shot_copy = (unsigned char *) EL_VA_CANVAS + i.copy_off;
	ws__shot_cap = i.cap;
	ws__shot = (volatile struct el_shot *) ((unsigned char *) EL_VA_CANVAS + i.ctl_off);
}

static void ws_shot_take (void)
{
	volatile struct el_shot *c = ws__shot;
	if (ws__cur != 0) return;			// (the first window's only)
	if (c == 0 || c->magic != EL_SHOT_MAGIC || !c->want) return;
	unsigned seq = c->seq;			// (this program alone writes it)
	__atomic_store_n ((unsigned *) &c->seq, seq + 1, __ATOMIC_SEQ_CST);	// (odd: being made)
	unsigned n = c->bytes;
	if (n > ws__shot_cap) n = ws__shot_cap;
	if (n > 0) WS_KT->memcpy (ws__shot_copy, (const void *) EL_VA_CANVAS, n);
	__atomic_store_n ((unsigned *) &c->seq, seq + 2, __ATOMIC_SEQ_CST);
}

static void ws_present (void)
{
	ws_shot_take ();
	long server = WS_KT->ws_ctl (KAPI_WS_KICK, ws__cur, 0, 0);
	if (server <= 0) return;
	if (ws__server != 0 && server != ws__server)	// another server than before: the windows asked again
	{
		ws_replay (1);
		WS_KT->ws_ctl (KAPI_WS_KICK, ws__cur, 0, 0);
	}
	ws__server = server;
}
#define WS_OP(op, a0, a1, a2, a3)	ws_call (op, (long) (a0), (long) (a1), (long) (a2), (long) (a3), 0, 0, 0, 0)

static void ws_copy (void *d, const void *s, unsigned long n)	// (the kernel's memcpy: AppKit links none)
{
	WS_KT->memcpy (d, s, n);
}

static unsigned ws_len (const char *s)
{
	unsigned n = 0;
	if (s != 0) while (s[n] != 0) n++;
	return n;
}

static unsigned *ws_create (int x, int y, int w, int h, const char *t, unsigned f)
{
	struct el_create c;
	__builtin_memset (&c, 0, sizeof c);
	c.flags = f;
	for (unsigned i = 0; t != 0 && t[i] != 0 && i + 1 < sizeof c.title; i++) c.title[i] = t[i];
	if (ws_raw (EL_OP_CREATE, x, y, w, h, &c, sizeof c, 0, 0) != 1) return 0;
	if (ws__cur == 0) ws_shot_ask ();
	struct ws_win *W = &ws__w[ws__cur];
	if (W->cw == 0)						// (a second call: the window it has)
	{
		W->cw = w; W->ch = h;
		W->x = x; W->y = y; W->flags = f;
		__builtin_memset (W->title, 0, sizeof W->title);
		for (unsigned i = 0; i + 1 < sizeof W->title && c.title[i] != 0; i++) W->title[i] = c.title[i];
		W->alpha = W->cursor = -1;
		W->made = 1;
	}
	return ws_canvas_va (ws__cur);
}

static unsigned *ws_resize (int w, int h)
{
	if (WS_OP (EL_OP_RESIZE, w, h, 0, 0) != 1) return 0;
	ws__w[ws__cur].lw = w; ws__w[ws__cur].lh = h;
	return ws_canvas_va (ws__cur);
}

static void ws_move (int x, int y)
{
	ws__w[ws__cur].x = x; ws__w[ws__cur].y = y;
	WS_OP (EL_OP_MOVE, x, y, 0, 0);
}

static void ws_handler (int kind, gui_handler fn)
{
	ws__w[ws__cur].handler[kind] = (long) fn; ws__w[ws__cur].handler_set |= 1 << kind;
	WS_OP (EL_OP_HANDLER, kind, fn, 0, 0);
}

static void ws_alpha (int a)
{
	ws__w[ws__cur].alpha = a < 0 ? 0 : a > 255 ? 255 : a;
	WS_OP (EL_OP_ALPHA, a, 0, 0, 0);
}

static int ws_resizable (int on, int min_w, int min_h)
{
	struct ws_win *W = &ws__w[ws__cur];
	W->sizable_set = 1; W->sizable = on; W->min_w = min_w; W->min_h = min_h;
	return (int) WS_OP (EL_OP_RESIZABLE, on, min_w, min_h, 0);
}

static int ws_cursor (int shape)
{
	if (shape >= 0 && shape < KAPI_CURSOR_COUNT) ws__w[ws__cur].cursor = shape;
	return (int) WS_OP (EL_OP_CURSOR, shape, 0, 0, 0);
}

static unsigned *ws_grow (int w, int h, int *stride)
{
	int row = 0;
	if (ws__cur == 0) ws__shot = 0;		// (the canvas may become another memory)
	long r = ws_call (EL_OP_GROW, w, h, 0, 0, 0, 0, &row, sizeof row);
	if (ws__cur == 0) ws_shot_ask ();
	if (r != 1) return 0;
	if (row > 0)
	{
		if (h > ws__ch || row != ws__cw) ws__ch = h > ws__ch ? h : ws__ch;
		ws__cw = row;
		ws__w[ws__cur].lw = w; ws__w[ws__cur].lh = h;
	}
	if (stride != 0) *stride = row;
	return ws_canvas_va (ws__cur);
}

static int ws_get_chrome (struct kapi_chrome *out)
{
	struct el_frame f;
	if (ws_call (EL_OP_FRAME, 0, 0, 0, 0, 0, 0, &f, sizeof f) != 1 || out == 0) return 0;
	__builtin_memset (out, 0, sizeof *out);
	out->content = ws_canvas_va (ws__cur);
	out->content_w = f.content_w; out->content_h = f.content_h;
	if (f.frame_w > 0)
	{
		out->active = ws__cur == 0 ? (unsigned *) EL_VA_FRAME : (unsigned *) EL_VA_WIN (ws__cur, 1);
		out->inactive = ws__cur == 0 ? (unsigned *) EL_VA_FRAME_OFF : (unsigned *) EL_VA_WIN (ws__cur, 2);
		out->chrome_w = f.frame_w; out->chrome_h = f.frame_h;
		out->inset_l = f.inset_l; out->inset_r = f.inset_r; out->inset_t = f.inset_t; out->inset_b = f.inset_b;
	}
	for (unsigned i = 0; i + 1 < sizeof out->title && f.title[i] != 0; i++) out->title[i] = f.title[i];
	return 1;
}

// Text in the window's canvas, in the kernel's bitmap font: the kernel still draws it, into the
// canvas as into any buffer of the program's.
static void ws_draw_text (int x, int y, const char *s, unsigned c)
{
	if (ws__cw > 0) WS_KT->draw_text_buf (ws_canvas_va (ws__cur), ws__cw, ws__ch, x, y, s, c);
}

static int ws_app_text (int op, char *b, unsigned size)		// (kapi_list_windows)
{
	static char s_Text[KAPI_WS_DATA_MAX];
	if (b == 0 || size == 0) return 0;
	b[0] = 0;
	long n = ws_call (op, 0, 0, 0, 0, 0, 0, s_Text, sizeof s_Text);
	if (n <= 0) return 0;
	s_Text[sizeof s_Text - 1] = 0;
	unsigned i = 0;
	for (; i + 1 < size && s_Text[i] != 0; i++) b[i] = s_Text[i];
	b[i] = 0;
	return (int) n;
}

static int ws_app_name (int op, const char *name)		// (kapi_raise_app, kapi_toggle_app)
{
	unsigned n = ws_len (name);
	if (n == 0 || n > 63) return 0;
	return (int) ws_call (op, 0, 0, 0, 0, name, n, 0, 0);
}

static int ws_toggle_app (const char *name)
{
	if (name == 0 || name[0] == 0) return -1;
	if (ws_app_name (EL_OP_APP_CLOSE, name) == 1) return 0;		// it ran: asked to close
	return WS_KT->launch (name) ? 1 : -1;
}

static int ws_set_menu (const char *spec, gui_handler h)
{
	unsigned n = ws_len (spec);
	if (n > 2047) n = 2047;
	if (ws__cur == 0)
	{
		for (unsigned i = 0; i < n; i++) ws__menu[i] = spec[i];
		ws__menu[n] = 0; ws__menu_handler = (long) h; ws__menu_set = 1;
	}
	return (int) ws_call (EL_OP_MENU_SET, (long) h, 0, 0, 0, spec, n, 0, 0);
}

static unsigned ws_get_menu (char *buf, unsigned cap, char *title, unsigned tcap)
{
	static struct el_menu s_Menu;
	__builtin_memset (&s_Menu, 0, (unsigned) (sizeof s_Menu - sizeof s_Menu.spec) + 1);
	long serial = ws_call (EL_OP_MENU_GET, 0, 0, 0, 0, 0, 0, &s_Menu, sizeof s_Menu);
	if (serial < 0) serial = 0;
	s_Menu.spec[sizeof s_Menu.spec - 1] = 0; s_Menu.title[sizeof s_Menu.title - 1] = 0;
	if (buf != 0 && cap > 0)
	{
		unsigned i = 0;
		for (; i + 1 < cap && s_Menu.spec[i] != 0; i++) buf[i] = s_Menu.spec[i];
		buf[i] = 0;
	}
	if (title != 0 && tcap > 0)
	{
		unsigned i = 0;
		for (; i + 1 < tcap && s_Menu.title[i] != 0; i++) title[i] = s_Menu.title[i];
		title[i] = 0;
	}
	return (unsigned) serial;
}

static int ws_win_list (struct kapi_win_info *out, int max)
{
	static unsigned char s_List[KAPI_WS_DATA_MAX];
	if (out == 0 || max <= 0) return 0;
	long n = ws_call (EL_OP_WIN_LIST, max, 0, 0, 0, 0, 0, s_List, sizeof s_List);
	if (n <= 0) return 0;
	if (n > max) n = max;
	ws_copy (out, s_List, (unsigned long) n * sizeof *out);
	return (int) n;
}

static int ws_win_geometry (struct kapi_win_geom *out)
{
	struct kapi_win_geom g;
	if (out == 0 || ws_call (EL_OP_GEOMETRY, 0, 0, 0, 0, 0, 0, &g, sizeof g) != 0) return -1;
	*out = g;
	return 0;
}

static int ws_drag_begin (int type, const void *data, unsigned len, const char *label)
{
	static unsigned char s_Drag[KAPI_WS_DATA_MAX];
	struct el_drag d;
	__builtin_memset (&d, 0, sizeof d);
	d.type = type;
	for (unsigned i = 0; label != 0 && label[i] != 0 && i + 1 < sizeof d.label; i++) d.label[i] = label[i];
	if (data == 0) len = 0;
	if (len > EL_DRAG_MAX) len = (unsigned) EL_DRAG_MAX;
	ws_copy (s_Drag, &d, sizeof d);
	if (len > 0) ws_copy (s_Drag + sizeof d, data, len);
	return ws_call (EL_OP_DRAG_BEGIN, 0, 0, 0, 0, s_Drag, (unsigned) sizeof d + len, 0, 0) == 1;
}

static int ws_drag_data (int *type, void *buf, unsigned cap)
{
	static unsigned char s_Drag[KAPI_WS_DATA_MAX];
	long n = ws_call (EL_OP_DRAG_DATA, 0, 0, 0, 0, 0, 0, s_Drag, sizeof s_Drag);
	if (n < 0) return 0;
	struct el_drag d;
	ws_copy (&d, s_Drag, sizeof d);
	if (type != 0) *type = d.type;
	if (buf != 0 && cap > 0) ws_copy (buf, s_Drag + sizeof d, (unsigned long) n < cap ? (unsigned long) n : cap);
	return (int) n;
}

static unsigned *ws_wallpaper_buffer (int *w, int *h)
{
	int wh[2] = { 0, 0 };
	if (ws_call (EL_OP_WALLPAPER_BUF, 0, 0, 0, 0, 0, 0, wh, sizeof wh) != 1) return 0;
	if (w != 0) *w = wh[0];
	if (h != 0) *h = wh[1];
	return (unsigned *) EL_VA_WALLPAPER;
}

static int ws_win_read (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride)
{
	struct el_read r;
	if (dst == 0) return -1;
	if (id == KAPI_WIN_DESKTOP && stride != w) return -1;
	long st = ws_call (EL_OP_WIN_READ, (long) id, part, ((long) x << 32) | (unsigned) y, ((long) w << 32) | (unsigned) h,
			   0, 0, &r, sizeof r);
	if (st != 0) return -1;
	if (r.w <= 0 || r.h <= 0) return 0;
	if (stride < r.w) return -1;
	const unsigned *src = (const unsigned *) EL_VA_XFER;
	for (int k = 0; k < r.h; k++)
		ws_copy (dst + (unsigned long) k * stride, src + (unsigned long) k * r.w, (unsigned long) r.w * 4);
	return 0;
}

static void ws_cursor_pos (int *x, int *y)
{
	int pt[2] = { 0, 0 };
	ws_call (EL_OP_POINTER, 0, 0, 0, 0, 0, 0, pt, sizeof pt);
	if (x != 0) *x = pt[0];
	if (y != 0) *y = pt[1];
}

// ---- a program's other windows (v94) ----------------------------------------------------------------

static int ws_win_new (int x, int y, int w, int h, const char *t, unsigned f, unsigned **canvas)
{
	if (canvas != 0) *canvas = 0;
	int win = 1;
	while (win <= EL_WINDOWS_MORE && ws__w[win].made) win++;
	if (win > EL_WINDOWS_MORE) return -1;
	int cur = ws__cur;
	ws__cur = win;
	__builtin_memset (&ws__w[win], 0, sizeof ws__w[win]);
	unsigned *px = ws_create (x, y, w, h, t, f);
	ws__cur = cur;
	if (px == 0) { __builtin_memset (&ws__w[win], 0, sizeof ws__w[win]); return -1; }
	if (canvas != 0) *canvas = px;
	return win;
}

static int ws_win_select (int win)
{
	if (win < 0) return ws__cur;
	if (win > EL_WINDOWS_MORE || (win != 0 && !ws__w[win].made)) return -1;
	int was = ws__cur;
	ws__cur = win;
	return was;
}

static void ws_win_destroy (int win)
{
	if (win <= 0 || win > EL_WINDOWS_MORE || !ws__w[win].made) return;
	int cur = ws__cur;
	ws__cur = win;
	ws_raw (EL_OP_DESTROY, 0, 0, 0, 0, 0, 0, 0, 0);
	__builtin_memset (&ws__w[win], 0, sizeof ws__w[win]);
	ws__cur = cur == win ? 0 : cur;
}

// ---- the status area's icon (v95) -----------------------------------------------------------------

static int ws_tray_set (const unsigned *px, const char *tip, gui_handler h)
{
	if (px == 0) return 0;
	WS_KT->memset (&ws__tray, 0, sizeof ws__tray);		// (the kernel's: AppKit links none)
	for (unsigned i = 0; tip != 0 && tip[i] != 0 && i + 1 < sizeof ws__tray.tip; i++) ws__tray.tip[i] = tip[i];
	ws_copy (ws__tray.px, px, sizeof ws__tray.px);
	ws__tray_handler = (long) h; ws__tray_set = 1;
	return ws_raw (EL_OP_TRAY_SET, (long) h, 0, 0, 0, &ws__tray, sizeof ws__tray, 0, 0) == 1;
}

static void ws_tray_clear (void)
{
	ws__tray_set = 0;
	ws_raw (EL_OP_TRAY_CLEAR, 0, 0, 0, 0, 0, 0, 0, 0);
}

static int ws_tray_list (struct kapi_tray_info *out, int max)
{
	static unsigned char s_List[KAPI_WS_DATA_MAX];
	if (out == 0 || max <= 0) return 0;
	long n = ws_raw (EL_OP_TRAY_LIST, max, 0, 0, 0, 0, 0, s_List, sizeof s_List);
	if (n <= 0) return 0;
	if (n > max) n = max;
	ws_copy (out, s_List, (unsigned long) n * sizeof *out);
	return (int) n;
}

static int ws_tray_icon (unsigned pid, unsigned *px)
{
	if (px == 0) return 0;
	return ws_raw (EL_OP_TRAY_ICON, (long) pid, 0, 0, 0, 0, 0, px, KAPI_TRAY_PX * KAPI_TRAY_PX * 4) == 1;
}

#undef ws__cw
#undef ws__ch
#undef WS_KT
#pragma GCC diagnostic pop
