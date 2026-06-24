//
// uikit.hpp -- a C++ retained-mode widget toolkit for Onyx apps. The object-oriented
// successor to uikit.h (C): same model -- widgets drawn entirely in the app's own
// canvas, driven by the kernel's pointer stream (ABI v22) + key events -- but as a
// small class hierarchy instead of a tagged struct.
//
// Memory: widgets are heap objects (operator new -> umm over kapi_sbrk, see
// onyxpp.hpp), owned by the ui::Ui container and freed in its destructor. All of it
// lives in the app's address space, reclaimed by the kernel when the app exits.
//
// Usage:
//   #include "uikit.hpp"
//   static ui::Ui *g_ui;
//   static void on_evt (unsigned long s, int ev, long v) { g_ui->onEvent (s, ev, v); }
//   static void on_ok  (ui::Widget &) { ... }
//   ...
//   unsigned *fb = kapi_create_window (W, H, "demo");
//   ui::Ui ui (fb, W, H); g_ui = &ui;
//   ui.button (20, 20, 80, 28, "OK", on_ok);
//   kapi_set_pointer_handler (on_evt); kapi_set_key_handler (on_evt);
//   while (!should_exit ()) { pump_events ();
//       if (ui.dirty ()) { ui.background (); ui.drawAll (); present (); } msleep (16); }
//
#ifndef ONYX_UIKIT_HPP
#define ONYX_UIKIT_HPP

#include "kapi.h"
#include "onyxpp.hpp"		// operator new/delete (umm) + C++ runtime stubs

namespace ui {

class Ui;
class Widget;

typedef void (*Action) (Widget &);	// fired on click / toggle / Enter, with the widget

// ---- small helpers -----------------------------------------------------------
static inline int  uk_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void uk_cpy (char *d, const char *s)
{ int i = 0; if (s) while (s[i] && i < 63) { d[i] = s[i]; i++; } d[i] = '\0'; }

// ============================================================================
// Widget -- base class. `text` is the caption (label/button) or the editable
// value (textbox). `tag` is free user data (e.g. an action code for a dispatch
// callback). Runtime hover/pressed/focused are managed by Ui.
// ============================================================================
class Widget
{
public:
	int  x, y, w, h;
	bool disabled;
	int  tag;
	Action cb;
	char text[64];
	bool hover, pressed, focused;

	Widget (int x_, int y_, int w_, int h_, const char *t, Action cb_)
	  : x (x_), y (y_), w (w_), h (h_), disabled (false), tag (0), cb (cb_),
	    hover (false), pressed (false), focused (false) { uk_cpy (text, t); }
	virtual ~Widget () {}

	bool contains (int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
	const char *getText () const { return text; }
	void setText (const char *t);				// (defined after Ui: marks dirty)

	virtual bool focusable () const { return !disabled; }	// Label overrides -> false
	virtual void draw (Ui &ui) = 0;
	virtual void onPress (Ui &ui, int px, int py) { (void) ui; (void) px; (void) py; }
	virtual void onDrag  (Ui &ui, int px, int py) { (void) ui; (void) px; (void) py; }	// button held + moved
	virtual void activate (Ui &ui) { (void) ui; if (cb) cb (*this); }
	virtual void key (Ui &ui, long k);			// default: Enter/Space activate
};

class Label : public Widget
{
public:
	Label (int x, int y, int w, int h, const char *t) : Widget (x, y, w, h, t, 0) {}
	bool focusable () const override { return false; }
	void draw (Ui &ui) override;
};

class Button : public Widget
{
public:
	Button (int x, int y, int w, int h, const char *t, Action cb) : Widget (x, y, w, h, t, cb) {}
	void draw (Ui &ui) override;
};

class Checkbox : public Widget
{
public:
	bool checked;
	Checkbox (int x, int y, int w, int h, const char *t, bool chk, Action cb)
	  : Widget (x, y, w, h, t, cb), checked (chk) {}
	void draw (Ui &ui) override;
	void activate (Ui &ui) override { checked = !checked; if (cb) cb (*this); (void) ui; }
};

class Textbox : public Widget
{
public:
	int  caret;
	bool password;
	Textbox (int x, int y, int w, int h, const char *t)
	  : Widget (x, y, w, h, t, 0), caret (uk_len (t)), password (false) {}
	void draw (Ui &ui) override;
	void onPress (Ui &ui, int px, int py) override;
	void activate (Ui &ui) override { caret = uk_len (text); (void) ui; }
	void key (Ui &ui, long k) override;			// editing: caret/insert/delete
};

// Horizontal slider: integer value in [vmin,vmax]; drag the thumb. cb fires on change.
class Slider : public Widget
{
public:
	int value, vmin, vmax;
	Slider (int x, int y, int w, int h, int lo, int hi, int val, Action cb)
	  : Widget (x, y, w, h, "", cb), value (val), vmin (lo), vmax (hi) {}
	void draw (Ui &ui) override;
	void onPress (Ui &ui, int px, int py) override { (void) py; setFromX (ui, px); }
	void onDrag  (Ui &ui, int px, int py) override { (void) py; setFromX (ui, px); }
	void setFromX (Ui &ui, int px);				// value from cursor x (+ cb, + dirty)
};

// Progress bar: non-interactive; fills proportionally to value in [vmin,vmax].
class Progress : public Widget
{
public:
	int value, vmin, vmax;
	Progress (int x, int y, int w, int h, int lo, int hi, int val)
	  : Widget (x, y, w, h, "", 0), value (val), vmin (lo), vmax (hi) {}
	bool focusable () const override { return false; }
	void draw (Ui &ui) override;
};

// ============================================================================
// Ui -- the widget container + event/draw pump. Owns its widgets.
// ============================================================================
class Ui
{
public:
	static const int MAXW = 64;
	Widget  *items[MAXW];
	int      n;
	unsigned *fb; int W, H;
	int      fw, fh;
	int      hover, pressed, focus;		// widget indices, -1 = none
	bool     dirtyFlag;
	unsigned col_bg, col_face, col_face_hi, col_face_dn, col_border, col_text,
		 col_accent, col_dis, col_field;

	Ui (unsigned *fb_, int W_, int H_)
	  : n (0), fb (fb_), W (W_), H (H_), hover (-1), pressed (-1), focus (-1), dirtyFlag (true)
	{
		fw = kapi_font_width ();  if (fw < 1) fw = 8;
		fh = kapi_font_height (); if (fh < 1) fh = 16;
		col_bg      = 0x00202830; col_face    = 0x00566074; col_face_hi = 0x00697690;
		col_face_dn = 0x00404a5a; col_border  = 0x00161c24; col_text    = 0x00ffffff;
		col_accent  = 0x0060ff90; col_dis     = 0x008891a0; col_field   = 0x00141a22;
	}
	~Ui () { for (int i = 0; i < n; i++) delete items[i]; }

	bool dirty () const { return dirtyFlag; }
	void markDirty () { dirtyFlag = true; }

	// drawing primitives (clipped to the canvas)
	void fill (int x, int y, int w, int h, unsigned c)
	{
		for (int yy = y; yy < y + h; yy++) if (yy >= 0 && yy < H)
			for (int xx = x; xx < x + w; xx++) if (xx >= 0 && xx < W)
				fb[yy * W + xx] = c;
	}
	void frame (int x, int y, int w, int h, unsigned c)
	{ fill (x, y, w, 1, c); fill (x, y + h - 1, w, 1, c); fill (x, y, 1, h, c); fill (x + w - 1, y, 1, h, c); }
	void background () { fill (0, 0, W, H, col_bg); }

	// widget factories: allocate, store, return a typed reference
	Label    &label    (int x, int y, int w, int h, const char *t)
	{ Label    *p = new Label    (x, y, w, h, t); add (p); return *p; }
	Button   &button   (int x, int y, int w, int h, const char *t, Action cb)
	{ Button   *p = new Button   (x, y, w, h, t, cb); add (p); return *p; }
	Checkbox &checkbox (int x, int y, int w, int h, const char *t, bool chk, Action cb)
	{ Checkbox *p = new Checkbox (x, y, w, h, t, chk, cb); add (p); return *p; }
	Textbox  &textbox  (int x, int y, int w, int h, const char *t)
	{ Textbox  *p = new Textbox  (x, y, w, h, t); add (p); return *p; }
	Slider   &slider   (int x, int y, int w, int h, int lo, int hi, int val, Action cb)
	{ Slider   *p = new Slider   (x, y, w, h, lo, hi, val, cb); add (p); return *p; }
	Progress &progress (int x, int y, int w, int h, int lo, int hi, int val)
	{ Progress *p = new Progress (x, y, w, h, lo, hi, val); add (p); return *p; }

	void setFocus (Widget &widget) { int i = indexOf (&widget); if (i >= 0) { focus = i; dirtyFlag = true; } }

	void onEvent (unsigned long sender, int ev, long v);
	void drawAll ()
	{ for (int i = 0; i < n; i++) items[i]->draw (*this); dirtyFlag = false; }

private:
	friend class Widget; friend class Textbox;
	int add (Widget *p) { if (n >= MAXW) { delete p; return -1; } items[n] = p; dirtyFlag = true; return n++; }
	int indexOf (Widget *p) const { for (int i = 0; i < n; i++) if (items[i] == p) return i; return -1; }
	int hitIndex (int px, int py) const		// topmost focusable widget at (px,py)
	{
		for (int i = n - 1; i >= 0; i--)
			if (items[i]->focusable () && items[i]->contains (px, py)) return i;
		return -1;
	}
	int nextFocus (int from) const
	{
		for (int k = 1; k <= n; k++) { int i = (from + k) % n; if (items[i]->focusable ()) return i; }
		return -1;
	}
};

// ---- Widget methods needing Ui -----------------------------------------------
inline void Widget::setText (const char *t) { uk_cpy (text, t); }
inline void Widget::key (Ui &ui, long k)
{ if (k == KEY_ENTER || k == ' ') activate (ui); }

// ---- event handling ----------------------------------------------------------
inline void Ui::onEvent (unsigned long sender, int ev, long v)
{
	(void) sender;
	switch (ev)
	{
	case GUI_EVENT_PTR_MOVE:
	case GUI_EVENT_PTR_ENTER:
	{
		if (pressed >= 0 && pressed < n)	// button held -> drag the pressed widget (slider)
		{
			items[pressed]->onDrag (*this, GUI_PTR_X (v), GUI_PTR_Y (v));
			dirtyFlag = true;
			break;
		}
		int h = hitIndex (GUI_PTR_X (v), GUI_PTR_Y (v));
		if (h != hover) { hover = h; dirtyFlag = true; }
		break;
	}
	case GUI_EVENT_PTR_LEAVE:
		if (hover != -1) { hover = -1; dirtyFlag = true; }
		break;
	case GUI_EVENT_PTR_DOWN:
		if (GUI_PTR_CHANGED (v) & 1)
		{
			pressed = hitIndex (GUI_PTR_X (v), GUI_PTR_Y (v));
			focus = pressed;
			if (pressed >= 0) items[pressed]->onPress (*this, GUI_PTR_X (v), GUI_PTR_Y (v));
			dirtyFlag = true;
		}
		break;
	case GUI_EVENT_PTR_UP:
		if (GUI_PTR_CHANGED (v) & 1)
		{
			int h = hitIndex (GUI_PTR_X (v), GUI_PTR_Y (v));
			if (pressed != -1 && h == pressed) items[pressed]->activate (*this);
			pressed = -1; dirtyFlag = true;
		}
		break;
	case GUI_EVENT_KEY:
		if (v == KEY_TAB) { focus = nextFocus (focus < 0 ? -1 : focus); dirtyFlag = true; }
		else if (focus >= 0 && focus < n) { items[focus]->key (*this, v); dirtyFlag = true; }
		break;
	default: break;
	}
}

// ---- per-widget drawing + textbox editing ------------------------------------
inline void Label::draw (Ui &ui)
{
	kapi_draw_text (x, y + (h - ui.fh) / 2, text, disabled ? ui.col_dis : ui.col_text);
}

inline void Button::draw (Ui &ui)
{
	unsigned face = ui.col_face;
	if (disabled || pressed)     face = ui.col_face_dn;
	else if (hover)              face = ui.col_face_hi;
	ui.fill (x, y, w, h, face);
	ui.frame (x, y, w, h, ui.col_border);
	if (focused && !disabled) ui.frame (x + 1, y + 1, w - 2, h - 2, ui.col_accent);
	int tw = uk_len (text) * ui.fw;
	kapi_draw_text (x + (w - tw) / 2, y + (h - ui.fh) / 2, text, disabled ? ui.col_dis : ui.col_text);
}

inline void Checkbox::draw (Ui &ui)
{
	int bs = ui.fh, by = y + (h - bs) / 2;
	ui.fill (x, by, bs, bs, (!disabled && hover) ? ui.col_face_hi : ui.col_face);
	ui.frame (x, by, bs, bs, ui.col_border);
	if (checked) ui.fill (x + 3, by + 3, bs - 6, bs - 6, ui.col_accent);
	kapi_draw_text (x + bs + 6, y + (h - ui.fh) / 2, text, disabled ? ui.col_dis : ui.col_text);
}

inline void Textbox::onPress (Ui &ui, int px, int py)
{
	(void) py;
	int rel = (px - (x + 4)) / (ui.fw > 0 ? ui.fw : 8);
	int len = uk_len (text);
	caret = rel < 0 ? 0 : (rel > len ? len : rel);
}

inline void Textbox::key (Ui &ui, long k)
{
	(void) ui;
	int len = uk_len (text);
	if (k >= 32 && k <= 126)			// insert printable at caret
	{
		if (len < 63)
		{
			if (caret < 0) caret = 0;
			if (caret > len) caret = len;
			for (int i = len; i > caret; i--) text[i] = text[i - 1];
			text[caret] = (char) k; text[len + 1] = '\0'; caret++;
		}
	}
	else if (k == KEY_BACKSPACE) { if (caret > 0) { for (int i = caret - 1; i < len; i++) text[i] = text[i + 1]; caret--; } }
	else if (k == KEY_DEL)       { if (caret < len) for (int i = caret; i < len; i++) text[i] = text[i + 1]; }
	else if (k == KEY_LEFT)      { if (caret > 0) caret--; }
	else if (k == KEY_RIGHT)     { if (caret < len) caret++; }
	else if (k == KEY_HOME)        caret = 0;
	else if (k == KEY_END)         caret = len;
	else if (k == KEY_ENTER)     { if (cb) cb (*this); }
}

inline void Textbox::draw (Ui &ui)
{
	const int pad = 4;
	ui.fill (x, y, w, h, disabled ? ui.col_face_dn : ui.col_field);
	ui.frame (x, y, w, h, ui.col_border);
	if (focused && !disabled) ui.frame (x + 1, y + 1, w - 2, h - 2, ui.col_accent);

	int fw = ui.fw > 0 ? ui.fw : 8;
	int maxvis = (w - 2 * pad) / fw; if (maxvis < 1) maxvis = 1; if (maxvis > 63) maxvis = 63;
	int len = uk_len (text), start = 0;
	if (caret > maxvis - 1) start = caret - (maxvis - 1);
	if (start < 0) start = 0;

	char vis[64]; int j = 0;
	for (int c = start; c < len && j < maxvis; c++) vis[j++] = password ? '*' : text[c];
	vis[j] = '\0';
	int ty = y + (h - ui.fh) / 2;
	kapi_draw_text (x + pad, ty, vis, disabled ? ui.col_dis : ui.col_text);
	if (focused && !disabled)
	{
		int cx = x + pad + (caret - start) * fw;
		if (cx >= x + pad && cx < x + w - 1) ui.fill (cx, ty, 1, ui.fh, ui.col_accent);
	}
}

inline void Slider::setFromX (Ui &ui, int px)
{
	int range = (vmax > vmin) ? vmax - vmin : 1;
	int t = px - x;
	if (t < 0) t = 0;
	if (t > w) t = w;
	int nv = vmin + range * t / (w > 0 ? w : 1);
	if (nv < vmin) nv = vmin;
	if (nv > vmax) nv = vmax;
	if (nv != value) { value = nv; if (cb) cb (*this); }
	ui.markDirty ();
}

inline void Slider::draw (Ui &ui)
{
	int midy = y + h / 2;
	ui.fill (x, midy - 1, w, 3, ui.col_border);			// track
	int range = (vmax > vmin) ? vmax - vmin : 1;
	int tx = x + (value - vmin) * (w - 8) / range;
	if (tx < x) tx = x;
	if (tx > x + w - 8) tx = x + w - 8;
	ui.fill  (tx, y, 8, h, (hover || pressed) ? ui.col_face_hi : ui.col_face);	// thumb
	ui.frame (tx, y, 8, h, ui.col_border);
	if (focused && !disabled) ui.frame (tx - 1, y - 1, 10, h + 2, ui.col_accent);
}

inline void Progress::draw (Ui &ui)
{
	ui.fill  (x, y, w, h, ui.col_field);
	int range = (vmax > vmin) ? vmax - vmin : 1;
	int fw = (value - vmin) * w / range;
	if (fw < 0) fw = 0;
	if (fw > w) fw = w;
	if (fw > 0) ui.fill (x, y, fw, h, ui.col_accent);
	ui.frame (x, y, w, h, ui.col_border);
}

} // namespace ui

#endif // ONYX_UIKIT_HPP
