//
// uikit.h -- a retained-mode widget toolkit for Onyx apps, drawn entirely in the
// app's own canvas and driven by the kernel's pointer stream (ABI v22,
// kapi_set_pointer_handler) + key events (kapi_set_key_handler).
//
// Why user-side: widgets are an application-level abstraction. The kernel provides
// only the primitives -- a canvas + raw pointer/key events -- so new widgets can be
// added here without touching the kernel or the ABI. (Same principle as httpc.h.)
//
// Memory model (the important part): there is NO user malloc. The widgets live in a
// FIXED POOL the CALLER provides (`ui_widget pool[N]` in the app's .bss). So all
// widget memory is in the app's own address space (8 GB region), and it is freed
// automatically when the app exits -- the whole address space (incl. .bss) is
// reclaimed by the kernel on task teardown. Nothing to free by hand, nothing leaks,
// and there is no kernel object behind a widget (unlike a socket).
//
// Usage:
//   static ui_widget   g_pool[32];
//   static ui_context  ui;
//   static void on_evt (unsigned long s, int ev, long v) { ui_on_event (&ui, s, ev, v); }
//   ...
//   unsigned *fb = kapi_create_window (W, H, "demo");
//   ui_init (&ui, g_pool, 32, fb, W, H);
//   int bOK = ui_button (&ui, 20, 20, 80, 28, "OK", on_ok);   // create once (retained)
//   kapi_set_pointer_handler (on_evt);
//   kapi_set_key_handler (on_evt);
//   while (!should_exit ()) {
//       pump_events ();
//       if (ui_dirty (&ui)) { ui_background (&ui); ui_draw (&ui); present (); }
//       msleep (16);
//   }
//
#ifndef ONYX_UIKIT_H
#define ONYX_UIKIT_H

#include "kapi.h"

enum { UI_LABEL = 0, UI_BUTTON, UI_CHECKBOX };	// widget kinds (extensible)

#define UI_FLAG_DISABLED	1

typedef void (*ui_cb) (int id);			// fired on click / toggle, with the widget id

typedef struct
{
	int   type;
	int   x, y, w, h;
	char  text[64];
	int   state;				// checkbox: 0/1
	int   flags;				// UI_FLAG_*
	ui_cb cb;
	unsigned char hover, pressed, focused;	// runtime (toolkit-managed)
} ui_widget;

typedef struct
{
	ui_widget *pool;			// caller-provided storage (app .bss)
	int   cap, n;
	unsigned *fb; int W, H;			// the app's canvas
	int   fw, fh;				// font metrics
	int   hover, pressed, focus;		// widget indices, -1 = none
	int   dirty;				// 1 if a redraw is needed
	unsigned col_bg, col_face, col_face_hi, col_face_dn, col_border, col_text, col_accent, col_dis;
} ui_context;

// ---- helpers -----------------------------------------------------------------

static inline int  uk__len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void uk__cpy (char *d, const char *s)
{ int i = 0; if (s) while (s[i] && i < 63) { d[i] = s[i]; i++; } d[i] = '\0'; }

static inline void ui_fill (ui_context *ui, int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h; yy++) if (yy >= 0 && yy < ui->H)
		for (int xx = x; xx < x + w; xx++) if (xx >= 0 && xx < ui->W)
			ui->fb[yy * ui->W + xx] = c;
}
static inline void ui_frame (ui_context *ui, int x, int y, int w, int h, unsigned c)
{
	ui_fill (ui, x, y, w, 1, c); ui_fill (ui, x, y + h - 1, w, 1, c);
	ui_fill (ui, x, y, 1, h, c); ui_fill (ui, x + w - 1, y, 1, h, c);
}
static inline void ui_background (ui_context *ui) { ui_fill (ui, 0, 0, ui->W, ui->H, ui->col_bg); }

// ---- setup + widget creation (retained: call once) ---------------------------

static inline void ui_init (ui_context *ui, ui_widget *pool, int cap, unsigned *fb, int W, int H)
{
	ui->pool = pool; ui->cap = cap; ui->n = 0;
	ui->fb = fb; ui->W = W; ui->H = H;
	ui->fw = kapi_font_width ();  if (ui->fw < 1) ui->fw = 8;
	ui->fh = kapi_font_height (); if (ui->fh < 1) ui->fh = 16;
	ui->hover = ui->pressed = ui->focus = -1;
	ui->dirty = 1;
	ui->col_bg      = 0x00202830;
	ui->col_face    = 0x00566074;
	ui->col_face_hi = 0x00697690;	// hover
	ui->col_face_dn = 0x00404a5a;	// pressed / disabled face
	ui->col_border  = 0x00161c24;
	ui->col_text    = 0x00ffffff;
	ui->col_accent  = 0x0060ff90;	// focus ring / check mark
	ui->col_dis     = 0x008891a0;	// disabled text
}

static inline int ui__add (ui_context *ui, int type, int x, int y, int w, int h,
			   const char *t, ui_cb cb)
{
	if (ui->n >= ui->cap) return -1;
	ui_widget *p = &ui->pool[ui->n];
	p->type = type; p->x = x; p->y = y; p->w = w; p->h = h;
	uk__cpy (p->text, t); p->state = 0; p->flags = 0; p->cb = cb;
	p->hover = p->pressed = p->focused = 0;
	ui->dirty = 1;
	return ui->n++;
}
static inline int ui_label  (ui_context *ui, int x, int y, int w, int h, const char *t)
{ return ui__add (ui, UI_LABEL, x, y, w, h, t, 0); }
static inline int ui_button (ui_context *ui, int x, int y, int w, int h, const char *t, ui_cb cb)
{ return ui__add (ui, UI_BUTTON, x, y, w, h, t, cb); }
static inline int ui_checkbox (ui_context *ui, int x, int y, int w, int h, const char *t, int chk, ui_cb cb)
{ int id = ui__add (ui, UI_CHECKBOX, x, y, w, h, t, cb); if (id >= 0) ui->pool[id].state = chk ? 1 : 0; return id; }

// ---- accessors ---------------------------------------------------------------

static inline int  ui_checked (ui_context *ui, int id) { return (id >= 0 && id < ui->n) ? ui->pool[id].state : 0; }
static inline void ui_set_checked (ui_context *ui, int id, int v) { if (id >= 0 && id < ui->n) { ui->pool[id].state = v ? 1 : 0; ui->dirty = 1; } }
static inline void ui_set_text (ui_context *ui, int id, const char *t) { if (id >= 0 && id < ui->n) { uk__cpy (ui->pool[id].text, t); ui->dirty = 1; } }
static inline void ui_set_enabled (ui_context *ui, int id, int en)
{ if (id >= 0 && id < ui->n) { if (en) ui->pool[id].flags &= ~UI_FLAG_DISABLED; else ui->pool[id].flags |= UI_FLAG_DISABLED; ui->dirty = 1; } }
static inline int  ui_dirty (ui_context *ui) { return ui->dirty; }
static inline void ui_mark_dirty (ui_context *ui) { ui->dirty = 1; }

// ---- event handling ----------------------------------------------------------

// Topmost interactive widget at (x,y); -1 if none. Labels + disabled are skipped.
static inline int ui__at (ui_context *ui, int x, int y)
{
	for (int i = ui->n - 1; i >= 0; i--)
	{
		ui_widget *p = &ui->pool[i];
		if (p->type == UI_LABEL || (p->flags & UI_FLAG_DISABLED)) continue;
		if (x >= p->x && x < p->x + p->w && y >= p->y && y < p->y + p->h) return i;
	}
	return -1;
}

// Activate a widget (button: callback; checkbox: toggle + callback).
static inline void ui__activate (ui_context *ui, int id)
{
	if (id < 0 || id >= ui->n) return;
	ui_widget *p = &ui->pool[id];
	if (p->type == UI_CHECKBOX) p->state ^= 1;
	if (p->cb) p->cb (id);
	ui->dirty = 1;
}

// Feed kernel GUI events (pointer stream + keys) here. Updates hover/press/focus,
// fires callbacks. Safe to call from the app's pointer + key handlers (same fn).
static inline void ui_on_event (ui_context *ui, unsigned long sender, int ev, long v)
{
	(void) sender;
	switch (ev)
	{
	case GUI_EVENT_PTR_MOVE:
	case GUI_EVENT_PTR_ENTER:
	{
		int h = ui__at (ui, GUI_PTR_X (v), GUI_PTR_Y (v));
		if (h != ui->hover) { ui->hover = h; ui->dirty = 1; }
		break;
	}
	case GUI_EVENT_PTR_LEAVE:
		if (ui->hover != -1) { ui->hover = -1; ui->dirty = 1; }
		break;
	case GUI_EVENT_PTR_DOWN:
		if (GUI_PTR_CHANGED (v) & 1)
		{
			ui->pressed = ui__at (ui, GUI_PTR_X (v), GUI_PTR_Y (v));
			ui->focus = ui->pressed;
			ui->dirty = 1;
		}
		break;
	case GUI_EVENT_PTR_UP:
		if (GUI_PTR_CHANGED (v) & 1)
		{
			int h = ui__at (ui, GUI_PTR_X (v), GUI_PTR_Y (v));
			if (ui->pressed != -1 && h == ui->pressed) ui__activate (ui, ui->pressed);
			ui->pressed = -1; ui->dirty = 1;
		}
		break;
	case GUI_EVENT_KEY:
		if ((v == KEY_ENTER || v == ' ') && ui->focus >= 0) ui__activate (ui, ui->focus);
		break;
	default: break;
	}
}

// ---- drawing -----------------------------------------------------------------

// Render all widgets into the canvas. The app draws its own content first (and
// usually ui_background() to clear), then calls this; widgets sit on top.
static inline void ui_draw (ui_context *ui)
{
	for (int i = 0; i < ui->n; i++)
	{
		ui_widget *p = &ui->pool[i];
		int dis = p->flags & UI_FLAG_DISABLED;
		unsigned tcol = dis ? ui->col_dis : ui->col_text;

		if (p->type == UI_LABEL)
		{
			kapi_draw_text (p->x, p->y + (p->h - ui->fh) / 2, p->text, tcol);
		}
		else if (p->type == UI_BUTTON)
		{
			unsigned face = ui->col_face;
			if (dis || i == ui->pressed) face = ui->col_face_dn;
			else if (i == ui->hover)     face = ui->col_face_hi;
			ui_fill (ui, p->x, p->y, p->w, p->h, face);
			ui_frame (ui, p->x, p->y, p->w, p->h, ui->col_border);
			if (i == ui->focus && !dis) ui_frame (ui, p->x + 1, p->y + 1, p->w - 2, p->h - 2, ui->col_accent);
			int tw = uk__len (p->text) * ui->fw;
			kapi_draw_text (p->x + (p->w - tw) / 2, p->y + (p->h - ui->fh) / 2, p->text, tcol);
		}
		else if (p->type == UI_CHECKBOX)
		{
			int bs = ui->fh, by = p->y + (p->h - bs) / 2;
			ui_fill (ui, p->x, by, bs, bs, (!dis && i == ui->hover) ? ui->col_face_hi : ui->col_face);
			ui_frame (ui, p->x, by, bs, bs, ui->col_border);
			if (p->state) ui_fill (ui, p->x + 3, by + 3, bs - 6, bs - 6, ui->col_accent);
			kapi_draw_text (p->x + bs + 6, p->y + (p->h - ui->fh) / 2, p->text, tcol);
		}
	}
	ui->dirty = 0;
}

#endif // ONYX_UIKIT_H
