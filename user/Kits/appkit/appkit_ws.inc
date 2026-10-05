//
// appkit_ws.inc -- AppKit's window calls when Elegant, the graphics server, owns the display: each
// kapi_* window call's other body (appkit_calls.inc's KAPI_WS), speaking the protocol of elegant.h
// instead of calling the kernel's window manager. Compiled into AppKit only (appkit.c): no program
// is rebuilt, none sees it.
//
// Which way a program's window calls go is settled at its first one, for its whole life: Elegant
// owns the display then (the kernel's KAPI_WS_ACTIVE), or the kernel's window manager does.
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

#define WS_KT	((const struct TKApiTable *) KAPI_TABLE_VA)

static int ws__mode;			// 0 not settled, 1 Elegant, 2 the kernel's window manager
static int ws__cw, ws__ch;		// the canvas: its row in pixels, its rows (kapi_draw_text)

static int ws_on (void)
{
	if (ws__mode == 0)
		ws__mode = WS_KT->version >= 89 && WS_KT->ws_ctl != 0 && WS_KT->ws_ctl (KAPI_WS_ACTIVE, 0, 0, 0) > 0 ? 1 : 2;
	return ws__mode == 1;
}

static long ws_call (int op, long a0, long a1, long a2, long a3, const void *in, unsigned in_len, void *out, unsigned cap)
{
	struct kapi_ws_call c;
	__builtin_memset (&c, 0, sizeof c);
	c.op = op; c.in = in; c.in_len = in_len; c.out = out; c.out_cap = cap;
	c.a[0] = a0; c.a[1] = a1; c.a[2] = a2; c.a[3] = a3;
	return WS_KT->ws_ctl (KAPI_WS_CALL, (long) &c, 0, 0);
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
	if (ws_call (EL_OP_CREATE, x, y, w, h, &c, sizeof c, 0, 0) != 1) return 0;
	if (ws__cw == 0) { ws__cw = w; ws__ch = h; }		// (a second call: the window it has)
	return (unsigned *) EL_VA_CANVAS;
}

static unsigned *ws_resize (int w, int h)
{
	return WS_OP (EL_OP_RESIZE, w, h, 0, 0) == 1 ? (unsigned *) EL_VA_CANVAS : 0;
}

static unsigned *ws_grow (int w, int h, int *stride)
{
	int row = 0;
	if (ws_call (EL_OP_GROW, w, h, 0, 0, 0, 0, &row, sizeof row) != 1) return 0;
	if (row > 0)
	{
		if (h > ws__ch || row != ws__cw) ws__ch = h > ws__ch ? h : ws__ch;
		ws__cw = row;
	}
	if (stride != 0) *stride = row;
	return (unsigned *) EL_VA_CANVAS;
}

static int ws_get_chrome (struct kapi_chrome *out)
{
	struct el_frame f;
	if (ws_call (EL_OP_FRAME, 0, 0, 0, 0, 0, 0, &f, sizeof f) != 1 || out == 0) return 0;
	__builtin_memset (out, 0, sizeof *out);
	out->content = (unsigned *) EL_VA_CANVAS;
	out->content_w = f.content_w; out->content_h = f.content_h;
	if (f.frame_w > 0)
	{
		out->active = (unsigned *) EL_VA_FRAME;
		out->inactive = (unsigned *) EL_VA_FRAME_OFF;
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
	if (ws__cw > 0) WS_KT->draw_text_buf ((unsigned *) EL_VA_CANVAS, ws__cw, ws__ch, x, y, s, c);
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

static void ws_cursor_pos (int *x, int *y)
{
	int pt[2] = { 0, 0 };
	ws_call (EL_OP_POINTER, 0, 0, 0, 0, 0, 0, pt, sizeof pt);
	if (x != 0) *x = pt[0];
	if (y != 0) *y = pt[1];
}

#undef WS_KT
