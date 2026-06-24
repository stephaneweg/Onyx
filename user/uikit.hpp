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
#include "bmp.hpp"		// user-side BMP decoder (ui::bmp_decode) for Icon

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
	void setRect (int X, int Y, int W, int H) { x = X; y = Y; w = W; h = H; }	// reposition/resize
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

// Multi-line editable text area: own heap buffer ('\n'-separated lines), caret-driven
// vertical+horizontal scroll, click to position. Used by tinypad (body) + demoE.
class Textarea : public Widget
{
public:
	char *buf; int cap, len, caret, top, left, rows, cols;	// rows/cols: visible, set in draw
	bool readonly;
	Textarea (int x, int y, int w, int h, int capacity)
	  : Widget (x, y, w, h, "", 0), cap (capacity < 16 ? 16 : capacity),
	    len (0), caret (0), top (0), left (0), rows (1), cols (1), readonly (false)
	{ buf = new char[cap]; buf[0] = '\0'; }
	~Textarea () override { delete[] buf; }
	const char *content () const { return buf; }		// the whole text (getText is the 64B caption)
	int  length () const { return len; }
	void setContent (const char *s);
	// scroll state (so an external Scrollbar can drive/reflect the view)
	int  lineCount () const;				// total lines (>=1)
	int  longestLine () const;				// longest line length (in columns)
	int  topLine () const { return top; }
	int  leftCol () const { return left; }
	int  maxTop () const  { int m = lineCount () - rows;   return m < 0 ? 0 : m; }
	int  maxLeft () const { int m = longestLine () - cols; return m < 0 ? 0 : m; }
	void setTop (int t)  { int m = maxTop ();  top  = t < 0 ? 0 : (t > m ? m : t); }
	void setLeft (int l) { int m = maxLeft (); left = l < 0 ? 0 : (l > m ? m : l); }
	void draw (Ui &ui) override;
	void onPress (Ui &ui, int px, int py) override;
	void key (Ui &ui, long k) override;
private:
	int  lineStart (int i) const { while (i > 0 && buf[i - 1] != '\n') i--; return i; }
	int  lineEnd   (int i) const { while (buf[i] != '\0' && buf[i] != '\n') i++; return i; }
	void insertAt (int ch);
	void deleteAt (int i);
	void ensureVisible (int vr, int vc);			// scroll caret into view (on caret move only)
};

// Draggable scrollbar (vertical or horizontal), value in [0,vmax]; cb fires on change.
// Standalone (the app wires the value to whatever it scrolls).
class Scrollbar : public Widget
{
public:
	bool vertical; int value, vmax;
	Scrollbar (int x, int y, int w, int h, bool vert, int maxv, int val, Action cb)
	  : Widget (x, y, w, h, "", cb), vertical (vert), value (val), vmax (maxv < 1 ? 1 : maxv) {}
	void draw (Ui &ui) override;
	void onPress (Ui &ui, int px, int py) override { setFromXY (ui, px, py); }
	void onDrag  (Ui &ui, int px, int py) override { setFromXY (ui, px, py); }
	void setFromXY (Ui &ui, int px, int py);
};

// Clickable icon: a magenta-keyed BMP (decoded user-side) centred above an optional
// label; hover/press highlight + an optional "running" badge (green triangle). The
// shell (panel/applist) uses these. setRect repositions/hides; setIcon swaps the image.
class Icon : public Widget
{
public:
	unsigned *pix; int iw, ih;	// decoded image (0 = none / placeholder)
	bool visible, badged;
	Icon (int x, int y, int w, int h, const char *bmp, const char *label, Action cb)
	  : Widget (x, y, w, h, label, cb), pix (0), iw (0), ih (0), visible (true), badged (false)
	{ if (bmp != 0 && bmp[0] != '\0') setIcon (bmp); }
	~Icon () override { delete[] pix; }
	void setIcon (const char *bmp);				// (re)load; 0/"" clears
	void setRect (int X, int Y, int W, int H) { x = X; y = Y; w = W; h = H; visible = (W > 0 && H > 0); }
	void setBadge (bool b) { badged = b; }
	bool focusable () const override { return visible && !disabled; }
	void draw (Ui &ui) override;
};

// 9-slice bitmap skin (user-side port of the kernel CSkin): a BMP holding `count`
// states stacked vertically (button.bmp: normal/hover/pressed), margins mark the fixed
// corners/edges, the middle tiles. Magenta (0xFF00FF) is transparent.
class Skin
{
public:
	unsigned *pix; int imgW, imgH, count, sw, sh, ml, mr, mt, mb;
	Skin () : pix (0), imgW (0), imgH (0), count (1), sw (0), sh (0), ml (0), mr (0), mt (0), mb (0) {}
	~Skin () { delete[] pix; }
	bool valid () const { return pix != 0 && sh > 0; }
	bool load (const char *path, int cnt, int l, int r, int t, int b)
	{
		delete[] pix; pix = bmp_decode (path, &imgW, &imgH);
		if (pix == 0) { sh = 0; return false; }
		count = cnt < 1 ? 1 : cnt; sw = imgW; sh = imgH / count;
		ml = l; mr = r; mt = t; mb = b;
		return sh > 0;
	}
	// Draw into a raw 0x00RRGGBB target (fb, W x H) -- the content canvas or a window-
	// chrome buffer. `tint` multiplies each pixel (0xFFFFFF = untouched); magenta is
	// the transparency key (skipped). blit = a magenta-keyed sub-rect; drawOn = 9-slice.
	void blit (unsigned *fb, int W, int H, int sx, int sy, int bw, int bh, int dx, int dy,
		   unsigned tint = 0xFFFFFF);
	void drawOn (unsigned *fb, int W, int H, int state, int x, int y, int w, int h,
		     unsigned tint = 0xFFFFFF);
};

// The shared button skin, loaded once on first use (flat fallback if absent).
static inline Skin &buttonSkin ()
{
	static Skin s; static bool tried = false;
	if (!tried) { tried = true; s.load ("SD:/skins/button.bmp", 3, 6, 6, 6, 6); }
	return s;
}
// The window-chrome skins (loaded once): wings.bmp = frame + title bar (grayscale, tinted
// per focus), closebgs.bmp = close box. Margins match the kernel's old LoadSkin calls.
static inline Skin &windowSkin () { static Skin s; static bool t = false; if (!t) { t = true; s.load ("SD:/skins/wings.bmp", 1, 7, 7, 32, 7); } return s; }
static inline Skin &closeSkin ()  { static Skin s; static bool t = false; if (!t) { t = true; s.load ("SD:/skins/closebgs.bmp", 3, 5, 5, 5, 5); } return s; }

// Tint copies of the window chrome (active = warm gold, inactive = muted slate), matching
// the kernel's old WIN_SKIN_TINT_* so focus reads at a glance.
#define UI_CHROME_TINT_ACTIVE	0x00FFC878
#define UI_CHROME_TINT_INACTIVE	0x008090A0

// Plot a clipped line into a raw 0x00RRGGBB buffer (for the close-box X glyph).
static inline void uk_line (unsigned *fb, int W, int H, int x0, int y0, int x1, int y1, unsigned c)
{
	int dx = x1 - x0, dy = y1 - y0;
	int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
	int steps = adx > ady ? adx : ady; if (steps < 1) steps = 1;
	for (int i = 0; i <= steps; i++)
	{
		int px = x0 + dx * i / steps, py = y0 + dy * i / steps;
		if (px >= 0 && px < W && py >= 0 && py < H) fb[py * W + px] = c;
	}
}

// Draw the standard window chrome (title bar + borders + close box + title text) into
// BOTH the active and inactive copies of this window's chrome buffer -- the user-side
// window decorations. The compositor blits the copy matching focus; the kernel keeps
// chrome BEHAVIOUR (title-bar drag, close-box hit-test). No-op for a borderless window
// or if the app has none. Drawn once (guarded); call after kapi_create_window. uikit
// apps get this automatically from the Ui constructor; app-drawn apps call it directly.
static inline void decorate_window ()
{
	static bool s_done = false;
	if (s_done) return;
	s_done = true;

	struct kapi_chrome c;
	if (!kapi_get_chrome (&c) || c.active == 0) return;	// no window / borderless
	Skin &ws = windowSkin ();
	Skin &cs = closeSkin ();
	int W = c.chrome_w, H = c.chrome_h, T = c.inset_t;
	int fh = kapi_font_height (); if (fh < 1) fh = 16;

	unsigned *bufs[2]  = { c.active, c.inactive };
	unsigned  tints[2] = { UI_CHROME_TINT_ACTIVE, UI_CHROME_TINT_INACTIVE };
	for (int k = 0; k < 2; k++)
	{
		unsigned *fb = bufs[k];
		if (fb == 0) continue;
		for (int i = 0; i < W * H; i++) fb[i] = 0x00FF00FF;	// magenta = transparent key
		if (ws.valid ()) ws.drawOn (fb, W, H, 0, 0, 0, W, H, tints[k]);

		// Close box [x] at the right of the title bar -- matches the kernel CloseBoxRect
		// (square inset = titlebar height - 10, 5 px from the top, ~4-5 px from the right).
		int nSize = T - 10; if (nSize < 6) nSize = 6;
		int cbx1 = W - 5, cbx0 = cbx1 - nSize, cby0 = 5, cby1 = cby0 + nSize;
		if (cs.valid ()) cs.drawOn (fb, W, H, 0, cbx0, cby0, cbx1 - cbx0 + 1, cby1 - cby0 + 1);
		uk_line (fb, W, H, cbx0 + 3, cby0 + 3, cbx1 - 3, cby1 - 3, 0x00FFFFFF);
		uk_line (fb, W, H, cbx1 - 3, cby0 + 3, cbx0 + 3, cby1 - 3, 0x00FFFFFF);

		// Title text, vertically centred in the title bar (inside the left border).
		kapi_draw_text_buf (fb, W, H, c.inset_l + 2, (T - fh) / 2, c.title, 0x00FFFFFF);
	}
}

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
		decorate_window ();		// draw this window's chrome (once; no-op if borderless)
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
	Textarea &textarea (int x, int y, int w, int h, int capacity)
	{ Textarea *p = new Textarea (x, y, w, h, capacity); add (p); return *p; }
	Scrollbar &scrollbar (int x, int y, int w, int h, bool vert, int maxv, int val, Action cb)
	{ Scrollbar *p = new Scrollbar (x, y, w, h, vert, maxv, val, cb); add (p); return *p; }
	Icon &icon (int x, int y, int w, int h, const char *bmp, const char *label, Action cb)
	{ Icon *p = new Icon (x, y, w, h, bmp, label, cb); add (p); return *p; }

	void setFocus (Widget &widget) { int i = indexOf (&widget); if (i >= 0) { focus = i; dirtyFlag = true; } }

	void onEvent (unsigned long sender, int ev, long v);
	void drawAll ()
	{
		for (int i = 0; i < n; i++)
		{
			// sync each widget's runtime flags from the Ui's tracked indices, so
			// hover/press highlights, the focus ring and the textbox/area caret render.
			items[i]->hover   = (i == hover);
			items[i]->pressed = (i == pressed);
			items[i]->focused = (i == focus);
			items[i]->draw (*this);
		}
		dirtyFlag = false;
	}

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
	int tw = uk_len (text) * ui.fw;
	int tx = x + (w - tw) / 2, ty = y + (h - ui.fh) / 2;
	Skin &bs = buttonSkin ();
	if (bs.valid () && !disabled)				// skinned: 9-slice bitmap + black label
	{
		bs.drawOn (ui.fb, ui.W, ui.H, pressed ? 2 : (hover ? 1 : 0), x, y, w, h);
		if (focused) ui.frame (x + 1, y + 1, w - 2, h - 2, ui.col_accent);
		kapi_draw_text (tx, ty, text, 0x00000000);
		return;
	}
	unsigned face = ui.col_face;				// flat fallback (no skin / disabled)
	if (disabled || pressed)     face = ui.col_face_dn;
	else if (hover)              face = ui.col_face_hi;
	ui.fill (x, y, w, h, face);
	ui.frame (x, y, w, h, ui.col_border);
	if (focused && !disabled) ui.frame (x + 1, y + 1, w - 2, h - 2, ui.col_accent);
	kapi_draw_text (tx, ty, text, disabled ? ui.col_dis : ui.col_text);
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

// ---- Textarea ----------------------------------------------------------------
inline void Textarea::insertAt (int ch)
{
	if (len >= cap - 1) return;
	for (int i = len; i > caret; i--) buf[i] = buf[i - 1];
	buf[caret] = (char) ch; len++; caret++; buf[len] = '\0';
}
inline void Textarea::deleteAt (int i)
{
	if (i < 0 || i >= len) return;
	for (int j = i; j < len; j++) buf[j] = buf[j + 1];
	len--;
}
inline void Textarea::setContent (const char *s)
{
	len = 0; caret = 0; top = 0; left = 0;
	if (s) while (s[len] != '\0' && len < cap - 1) { buf[len] = s[len]; len++; }
	buf[len] = '\0';
}
inline int Textarea::lineCount () const
{ int n = 1; for (int i = 0; i < len; i++) if (buf[i] == '\n') n++; return n; }
inline int Textarea::longestLine () const
{ int best = 0, c = 0; for (int i = 0; i <= len; i++) { if (i == len || buf[i] == '\n') { if (c > best) best = c; c = 0; } else c++; } return best; }
// Scroll the caret into view -- called only when the caret moves (typing/click/keys),
// NOT from draw(), so dragging a scrollbar away from the caret sticks.
inline void Textarea::ensureVisible (int vr, int vc)
{
	int cl = 0; for (int i = 0; i < caret; i++) if (buf[i] == '\n') cl++;
	int cc = caret - lineStart (caret);
	if (cl < top) top = cl;
	if (cl >= top + vr) top = cl - vr + 1;
	if (cc < left) left = cc;
	if (cc >= left + vc) left = cc - vc + 1;
	if (top < 0) top = 0;
	if (left < 0) left = 0;
}
inline void Textarea::onPress (Ui &ui, int px, int py)
{
	const int pad = 4, fw = ui.fw > 0 ? ui.fw : 8;
	int row = (py - (y + 2)) / ui.fh;
	int col = (px - (x + pad)) / fw;
	if (row < 0) row = 0;
	if (col < 0) col = 0;
	int wantLine = top + row, wantCol = left + col, i = 0, line = 0;
	while (line < wantLine && buf[i] != '\0') { if (buf[i] == '\n') line++; i++; }
	int le = lineEnd (i), c = i + wantCol;
	if (c > le) c = le;
	caret = c;
	ensureVisible ((h - 4) / ui.fh, (w - 2 * pad) / fw);
}
inline void Textarea::key (Ui &ui, long k)
{
	if (k >= 32 && k <= 126)          { if (!readonly) insertAt ((int) k); }
	else if (k == KEY_ENTER)          { if (!readonly) insertAt ('\n'); }
	else if (k == KEY_TAB)            { if (!readonly) { insertAt (' '); insertAt (' '); } }
	else if (k == KEY_BACKSPACE)      { if (!readonly && caret > 0) { deleteAt (caret - 1); caret--; } }
	else if (k == KEY_DEL)            { if (!readonly) deleteAt (caret); }
	else if (k == KEY_LEFT)           { if (caret > 0) caret--; }
	else if (k == KEY_RIGHT)          { if (caret < len) caret++; }
	else if (k == KEY_HOME)             caret = lineStart (caret);
	else if (k == KEY_END)              caret = lineEnd (caret);
	else if (k == KEY_UP)
	{
		int ls = lineStart (caret), col = caret - ls;
		if (ls > 0) { int pls = lineStart (ls - 1), ple = ls - 1, c = pls + col; caret = c > ple ? ple : c; }
	}
	else if (k == KEY_DOWN)
	{
		int ls = lineStart (caret), col = caret - ls, le = lineEnd (caret);
		if (buf[le] == '\n') { int nls = le + 1, nle = lineEnd (nls), c = nls + col; caret = c > nle ? nle : c; }
	}
	ensureVisible ((h - 4) / ui.fh, (w - 8) / (ui.fw > 0 ? ui.fw : 8));
}
inline void Textarea::draw (Ui &ui)
{
	const int pad = 4;
	int fw = ui.fw > 0 ? ui.fw : 8;
	rows = (h - 4) / ui.fh; if (rows < 1) rows = 1;			// cache visible extent
	cols = (w - 2 * pad) / fw; if (cols < 1) cols = 1; if (cols > 159) cols = 159;
	int mt = maxTop ();  if (top  > mt) top  = mt; if (top  < 0) top  = 0;	// clamp (no caret-snap)
	int ml = maxLeft (); if (left > ml) left = ml; if (left < 0) left = 0;

	ui.fill  (x, y, w, h, disabled ? ui.col_face_dn : ui.col_field);
	ui.frame (x, y, w, h, ui.col_border);
	if (focused && !disabled) ui.frame (x + 1, y + 1, w - 2, h - 2, ui.col_accent);

	int i = 0, line = 0;
	while (line < top && buf[i] != '\0') { if (buf[i] == '\n') line++; i++; }
	for (int r = 0; r < rows; r++)
	{
		int le = lineEnd (i);
		char vis[160]; int j = 0;
		for (int c = i + left; c < le && j < cols; c++) vis[j++] = buf[c];
		vis[j] = '\0';
		if (j > 0) kapi_draw_text (x + pad, y + 2 + r * ui.fh, vis, disabled ? ui.col_dis : ui.col_text);
		if (buf[le] != '\n') break;			// end of buffer
		i = le + 1;
	}
	if (focused && !disabled)				// caret
	{
		int cl = 0; for (int c = 0; c < caret; c++) if (buf[c] == '\n') cl++;
		int cc = caret - lineStart (caret);
		int cx = x + pad + (cc - left) * fw, cy = y + 2 + (cl - top) * ui.fh;
		if (cy >= y && cy < y + h - 2 && cx >= x + pad && cx < x + w - 1) ui.fill (cx, cy, 1, ui.fh, ui.col_accent);
	}
}

// ---- Scrollbar ---------------------------------------------------------------
inline void Scrollbar::setFromXY (Ui &ui, int px, int py)
{
	int pos = vertical ? (py - y) : (px - x), span = vertical ? h : w;
	if (pos < 0) pos = 0;
	if (pos > span) pos = span;
	int nv = vmax * pos / (span > 0 ? span : 1);
	if (nv < 0) nv = 0;
	if (nv > vmax) nv = vmax;
	if (nv != value) { value = nv; if (cb) cb (*this); }
	ui.markDirty ();
}
inline void Scrollbar::draw (Ui &ui)
{
	ui.fill  (x, y, w, h, ui.col_field);
	ui.frame (x, y, w, h, ui.col_border);
	int span = vertical ? h : w;
	int thumb = span / 5; if (thumb < 14) thumb = 14; if (thumb > span) thumb = span;
	int pos = (span - thumb) * value / (vmax > 0 ? vmax : 1);
	unsigned face = (hover || pressed) ? ui.col_face_hi : ui.col_face;
	if (vertical) ui.fill (x + 1, y + pos, w - 2, thumb, face);
	else          ui.fill (x + pos, y + 1, thumb, h - 2, face);
}

// ---- Icon --------------------------------------------------------------------
inline void Icon::setIcon (const char *bmp)
{
	delete[] pix; pix = 0; iw = 0; ih = 0;
	if (bmp != 0 && bmp[0] != '\0') pix = bmp_decode (bmp, &iw, &ih);
}
inline void Icon::draw (Ui &ui)
{
	if (!visible) return;
	if (pressed)    ui.fill (x, y, w, h, 0x00405468);		// hover/press highlight
	else if (hover) ui.fill (x, y, w, h, 0x00303f50);

	int labelH = (text[0] != '\0') ? ui.fh + 2 : 0;
	if (pix != 0 && iw > 0 && ih > 0)				// magenta-keyed image, centred
	{
		int ix = x + (w - iw) / 2, iy = y + ((h - labelH) - ih) / 2;
		for (int yy = 0; yy < ih; yy++)
			for (int xx = 0; xx < iw; xx++)
			{
				unsigned c = pix[yy * iw + xx];
				if (c == 0x00FF00FF) continue;		// transparent key
				int px = ix + xx, py = iy + yy;
				if (px >= x && px < x + w && py >= y && py < y + h &&
				    px >= 0 && px < ui.W && py >= 0 && py < ui.H)
					ui.fb[py * ui.W + px] = c;
			}
	}
	else								// placeholder square
	{
		int m = 6;
		ui.fill  (x + m, y + m, w - 2 * m, h - 2 * m - labelH, 0x00808890);
		ui.frame (x + m, y + m, w - 2 * m, h - 2 * m - labelH, ui.col_border);
	}

	if (text[0] != '\0')
	{
		int tw = uk_len (text) * ui.fw;
		kapi_draw_text (x + (w - tw) / 2, y + h - ui.fh, text, ui.col_text);
	}
	if (badged)							// "running" green triangle, bottom-left
		for (int t = 0; t < 9; t++) ui.fill (x + 2, y + h - 2 - t, (8 - t) + 1, 1, 0x0040E060);
}

// Multiply a 0x00RRGGBB pixel by a 0x00RRGGBB tint (per channel / 255). 0xFFFFFF = no-op.
static inline unsigned uk_tint (unsigned c, unsigned t)
{
	if (t == 0xFFFFFF) return c;
	unsigned r = (((c >> 16) & 0xFF) * ((t >> 16) & 0xFF)) / 255;
	unsigned g = (((c >> 8)  & 0xFF) * ((t >> 8)  & 0xFF)) / 255;
	unsigned b = (( c        & 0xFF) * ( t        & 0xFF)) / 255;
	return (r << 16) | (g << 8) | b;
}

// Copy a (bw x bh) sub-rect of the skin image at (sx,sy) to (dx,dy) in the target
// buffer, skipping the magenta key, multiplying by `tint`, clipped to (W,H).
inline void Skin::blit (unsigned *fb, int W, int H, int sx, int sy, int bw, int bh,
			int dx, int dy, unsigned tint)
{
	for (int yy = 0; yy < bh; yy++)
	{
		int py = dy + yy, syy = sy + yy;
		if (py < 0 || py >= H || syy < 0 || syy >= imgH) continue;
		for (int xx = 0; xx < bw; xx++)
		{
			int px = dx + xx, sxx = sx + xx;
			if (px < 0 || px >= W || sxx < 0 || sxx >= imgW) continue;
			unsigned c = pix[syy * imgW + sxx];
			if (c == 0x00FF00FF) continue;			// transparent key
			fb[py * W + px] = uk_tint (c, tint);
		}
	}
}

// 9-slice draw of state `state` into the (x,y,w,h) box of (fb,W,H): fixed corners,
// edges tiled along one axis, the centre filled with the skin's middle pixel (port of
// the kernel CSkin::DrawOn). `tint` recolours a grayscale skin (e.g. window chrome).
inline void Skin::drawOn (unsigned *fb, int W, int H, int state, int x, int y, int w, int h,
			  unsigned tint)
{
	if (!valid ()) return;
	int sy = sh * (state % count);
	if (w == sw && h == sh) { blit (fb, W, H, 0, sy, sw, sh, x, y, tint); return; }	// exact size

	int midW = sw - ml - mr, midH = sh - mt - mb;		// source middle band
	int outW = w  - ml - mr, outH = h  - mt - mb;		// dest middle band

	if (outW > 0 && outH > 0)				// centre fill (single middle pixel)
	{
		unsigned c = uk_tint (pix[(sy + sh / 2) * imgW + (sw / 2)], tint);
		for (int yy = y + mt; yy < y + mt + outH; yy++) if (yy >= 0 && yy < H)
			for (int xx = x + ml; xx < x + ml + outW; xx++) if (xx >= 0 && xx < W)
				fb[yy * W + xx] = c;
	}
	if (midW > 0 && outW > 0)				// top/bottom edges, tiled horizontally
	{
		int fillR = x + w - mr;
		for (int tx = x + ml; tx < fillR; tx += midW)
		{
			int tcw = midW; if (tx + tcw > fillR) tcw = fillR - tx;
			if (mt > 0) blit (fb, W, H, ml, sy,          tcw, mt, tx, y,        tint);
			if (mb > 0) blit (fb, W, H, ml, sy + sh - mb, tcw, mb, tx, y + h - mb, tint);
		}
	}
	if (midH > 0 && outH > 0)				// left/right edges, tiled vertically
	{
		int fillB = y + h - mb;
		for (int ty = y + mt; ty < fillB; ty += midH)
		{
			int tch = midH; if (ty + tch > fillB) tch = fillB - ty;
			if (ml > 0) blit (fb, W, H, 0,       sy + mt, ml, tch, x,          ty, tint);
			if (mr > 0) blit (fb, W, H, sw - mr, sy + mt, mr, tch, x + w - mr, ty, tint);
		}
	}
	if (ml > 0 && mt > 0) blit (fb, W, H, 0,       sy,          ml, mt, x,          y,        tint); // corners
	if (mr > 0 && mt > 0) blit (fb, W, H, sw - mr, sy,          mr, mt, x + w - mr, y,        tint);
	if (ml > 0 && mb > 0) blit (fb, W, H, 0,       sy + sh - mb, ml, mb, x,          y + h - mb, tint);
	if (mr > 0 && mb > 0) blit (fb, W, H, sw - mr, sy + sh - mb, mr, mb, x + w - mr, y + h - mb, tint);
}

} // namespace ui

#endif // ONYX_UIKIT_HPP
