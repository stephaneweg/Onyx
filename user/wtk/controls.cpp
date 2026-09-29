//
// wtk/controls.cpp -- RadioButton, GroupBox, ToggleSwitch, NumericUpDown.
//
#include "wtk/radio.h"
#include "wtk/groupbox.h"
#include "wtk/toggle.h"
#include "wtk/numeric.h"

namespace wtk {

static void copy_text (char *d, const char *s, int cap)
{ int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

// ---- RadioButton ---------------------------------------------------------------------------
RadioButton::RadioButton (int l, int t, int w, int h, const char *s, int group_, bool chk, Action cb_, unsigned bg_)
  : Widget (l, t, w, h), group (group_), checked (chk), cb (cb_), bg (bg_)
{ canFocus = true; copy_text (text, s, sizeof text); }

void RadioButton::select ()
{
	if (parent)
		for (Widget *c = parent->firstChild; c; c = c->nextSib)
		{
			RadioButton *r = c != this ? c->asRadio () : 0;
			if (r && r->group == group && r->checked) { r->checked = false; r->invalidate (true); }
		}
	bool was = checked;
	checked = true;
	invalidate (true);
	if (!was && cb) cb (*this);
}

void RadioButton::onDraw ()
{
	canvas.clear (bg);
	int fh = wk_fh (), bs = fh - 2 < height ? fh - 2 : height, by = (height - bs) / 2;
	int st = disabled ? WK_DISABLED : pressed ? WK_PRESSED : hover ? WK_HOT : WK_NORMAL;
	if (hasFocus && !disabled) st |= WK_FOCUS;
	wk_radio_mark (canvas, 0, by, bs, checked, st);
	canvas.text (bs + 7, (height - fh) / 2, text, disabled ? C_DIS : C_TEXT);
}

bool RadioButton::onMouse (int mx, int, int bl, int, int, int)
{
	if (mx < 0) { if (hover || pressed) { hover = false; pressed = false; invalidate (true); } return false; }
	if (disabled) return true;
	bool wh = hover, wp = pressed; hover = true;
	if (bl && !pressed) { pressed = true; setFocus (); }
	else if (!bl && pressed) { pressed = false; select (); }
	if (hover != wh || pressed != wp) invalidate (true);
	return true;
}

bool RadioButton::onKey (long k)
{
	if (k == ' ' || k == KEY_ENTER) { select (); return true; }
	return false;
}

RadioButton *wk_radio_checked (Widget *parent, int group)
{
	for (Widget *c = parent ? parent->firstChild : 0; c; c = c->nextSib)
	{
		RadioButton *r = c->asRadio ();
		if (r && r->group == group && r->checked) return r;
	}
	return 0;
}

// ---- GroupBox ------------------------------------------------------------------------------
GroupBox::GroupBox (int l, int t, int w, int h, const char *title_, unsigned bg_)
  : Widget (l, t, w, h), bg (bg_), frame (0)
{ copy_text (title, title_, sizeof title); }

// An etched rounded frame (or a plain line in `frame`, when the app sets a colour), the title
// in bold over it.
void GroupBox::onDraw ()
{
	int fh = wk_fh (), y = fh / 2;
	canvas.clear (bg);
	if (frame) wk_rline (canvas, 0, y, width, height - y, 6, frame, 255);
	else wk_etch_box (canvas, 0, y, width, height - y, 6, bg);
	if (title[0])
	{
		int tw = wk_text_w (title, 2);
		canvas.fillRect (8, 0, tw + 8, fh, bg);
		wk_text_l (canvas, 12, 0, fh, title, C_TEXT, 2);
	}
}

// ---- ToggleSwitch --------------------------------------------------------------------------
ToggleSwitch::ToggleSwitch (int l, int t, int w, int h, const char *s, bool on_, Action cb_, unsigned bg_)
  : Widget (l, t, w, h), on (on_), cb (cb_), bg (bg_)
{ canFocus = true; copy_text (text, s, sizeof text); }

void ToggleSwitch::onDraw ()
{
	canvas.clear (bg);
	int fh = wk_fh (), ph = fh + 2 < height ? fh + 2 : height, pw = 2 * ph - 2, py = (height - ph) / 2;
	int st = disabled ? WK_DISABLED : pressed ? WK_PRESSED : hover ? WK_HOT : WK_NORMAL;
	if (hasFocus && !disabled) st |= WK_FOCUS;
	wk_switch_mark (canvas, 0, py, pw, ph, on, st);
	canvas.text (pw + 8, (height - fh) / 2, text, disabled ? C_DIS : C_TEXT);
}

bool ToggleSwitch::onMouse (int mx, int, int bl, int, int, int)
{
	if (mx < 0) { if (hover || pressed) { hover = false; pressed = false; invalidate (true); } return false; }
	if (disabled) return true;
	bool wh = hover, wp = pressed; hover = true;
	if (bl && !pressed) { pressed = true; setFocus (); }
	else if (!bl && pressed) { pressed = false; on = !on; if (cb) cb (*this); }
	if (hover != wh || pressed != wp) invalidate (true);
	return true;
}

bool ToggleSwitch::onKey (long k)
{
	if (k == ' ' || k == KEY_ENTER) { on = !on; if (cb) cb (*this); invalidate (true); return true; }
	return false;
}

// ---- NumericUpDown -------------------------------------------------------------------------
NumericUpDown::NumericUpDown (int l, int t, int w, int h, int lo, int hi, int val, int step_, Action cb_)
  : Widget (l, t, w, h), value (val), vmin (lo), vmax (hi), step (step_ > 0 ? step_ : 1), cb (cb_), m_elen (-1), m_down (0)
{ canFocus = true; if (value < vmin) value = vmin; if (value > vmax) value = vmax; }

void NumericUpDown::setValue (int v)
{
	if (v < vmin) v = vmin;
	if (v > vmax) v = vmax;
	if (v == value) { invalidate (true); return; }
	value = v;
	invalidate (true);
	if (cb) cb (*this);
}

void NumericUpDown::commit ()
{
	if (m_elen < 0) return;
	int v = 0, i = 0; bool neg = false;
	if (m_elen > 0 && m_edit[0] == '-') { neg = true; i = 1; }
	for (; i < m_elen; i++) v = v * 10 + (m_edit[i] - '0');
	m_elen = -1;
	setValue (neg ? -v : v);
}

enum { NUD_BW = 18 };		// the arrow buttons' width

void NumericUpDown::onDraw ()
{
	int fh = wk_fh (), bw = NUD_BW;
	canvas.clear (bgColor ());
	wk_sunken (canvas, 0, 0, width, height, 4, disabled ? wk_tone (C_FACE, 150) : C_FIELD, hasFocus && !disabled);
	char b[16]; int p = 0;
	if (m_elen >= 0) { for (int i = 0; i < m_elen; i++) b[p++] = m_edit[i]; }
	else
	{
		int v = value; if (v < 0) { b[p++] = '-'; v = -v; }
		char t[12]; int n = 0; if (v == 0) t[n++] = '0';
		while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
		while (n) b[p++] = t[--n];
	}
	b[p] = '\0';
	canvas.text (width - bw - 8 - wk_tw (b), (height - fh) / 2, b, disabled ? C_DIS : C_FIELD_TEXT);	// right-aligned
	if (m_elen >= 0) canvas.fillRect (width - bw - 7, (height - fh) / 2, 2, fh, C_ACCENT);	// caret
	int bx = width - bw - 2, hh = (height - 4) / 2;			// the arrows: two small buttons
	int su = disabled ? WK_DISABLED : m_down == 1 ? WK_PRESSED : WK_NORMAL;
	int sd = disabled ? WK_DISABLED : m_down == 2 ? WK_PRESSED : WK_NORMAL;
	wk_raised (canvas, bx, 2, bw, hh, 3, C_BUTTON, su);
	wk_raised (canvas, bx, 2 + hh, bw, height - 4 - hh, 3, C_BUTTON, sd);
	unsigned gc = disabled ? wk_mix (C_BUTTON, C_BUTTON_TEXT, 110) : C_BUTTON_TEXT;
	wk_glyph (canvas, WKG_UP, bx + bw / 2, 2 + hh / 2, 8, gc);
	wk_glyph (canvas, WKG_DOWN, bx + bw / 2, 2 + hh + (height - 4 - hh) / 2, 8, gc);
}

bool NumericUpDown::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; if (m_down) { m_down = 0; invalidate (true); } return false; }
	if (disabled) return true;
	if (wheel) { commit (); setValue (value + (wheel > 0 ? step : -step)); return true; }
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (mx >= width - NUD_BW - 2)
		{
			m_down = my < height / 2 ? 1 : 2;
			commit (); setValue (value + (m_down == 1 ? step : -step));
		}
	}
	else if (!bl) { pressed = false; if (m_down) { m_down = 0; invalidate (true); } }
	return true;
}

bool NumericUpDown::onKey (long k)
{
	switch (k)
	{
	case KEY_UP:   commit (); setValue (value + step); return true;
	case KEY_DOWN: commit (); setValue (value - step); return true;
	case KEY_PGUP: commit (); setValue (value + 10 * step); return true;
	case KEY_PGDN: commit (); setValue (value - 10 * step); return true;
	case KEY_ENTER: commit (); return true;
	case 27: m_elen = -1; invalidate (true); return true;
	case KEY_BACKSPACE:
		if (m_elen < 0) m_elen = 0;
		else if (m_elen > 0) m_elen--;
		invalidate (true); return true;
	}
	if ((k >= '0' && k <= '9') || (k == '-' && vmin < 0))
	{
		if (m_elen < 0) m_elen = 0;
		if (k == '-' && m_elen > 0) return true;
		if (m_elen < 11) m_edit[m_elen++] = (char) k;
		invalidate (true);
		return true;
	}
	return false;
}

} // namespace wtk
