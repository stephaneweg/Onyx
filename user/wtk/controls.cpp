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

// A filled disc (radius r) centred at (cx, cy).
static void disc (Canvas &cv, int cx, int cy, int r, unsigned c)
{
	for (int dy = -r; dy <= r; dy++)
	{
		int dx = 0; while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
		cv.fillRect (cx - dx, cy + dy, 2 * dx + 1, 1, c);
	}
}

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
	int fh = wk_fh (), r = fh / 2 - 1, cy = height / 2;
	disc (canvas, r + 1, cy, r + 1, C_BORDER);
	disc (canvas, r + 1, cy, r, (!disabled && hover) ? C_FACE_HI : C_FACE);
	if (checked) disc (canvas, r + 1, cy, r / 2, C_ACCENT);
	if (hasFocus) canvas.frameRect (2 * r + 5, (height - fh) / 2 - 1, wk_len (text) * wk_fw () + 4, fh + 2, C_FACE);
	canvas.text (2 * r + 7, (height - fh) / 2, text, disabled ? C_DIS : C_TEXT);
}

bool RadioButton::onMouse (int mx, int, int bl, int, int, int)
{
	if (mx < 0) { if (hover) { hover = false; invalidate (true); } pressed = false; return false; }
	if (disabled) return true;
	bool wh = hover; hover = true;
	if (bl && !pressed) { pressed = true; setFocus (); }
	else if (!bl && pressed) { pressed = false; select (); }
	if (hover != wh) invalidate (true);
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
  : Widget (l, t, w, h), bg (bg_), frame (0x00485870)
{ copy_text (title, title_, sizeof title); }

void GroupBox::onDraw ()
{
	int fh = wk_fh (), fw = wk_fw (), y = fh / 2;
	canvas.clear (bg);
	canvas.frameRect (0, y, width, height - y, frame);
	int tw = wk_len (title) * fw;
	if (tw) { canvas.fillRect (8, 0, tw + 8, fh, bg); canvas.text (12, 0, title, C_TEXT); }
}

// ---- ToggleSwitch --------------------------------------------------------------------------
ToggleSwitch::ToggleSwitch (int l, int t, int w, int h, const char *s, bool on_, Action cb_, unsigned bg_)
  : Widget (l, t, w, h), on (on_), cb (cb_), bg (bg_)
{ canFocus = true; copy_text (text, s, sizeof text); }

void ToggleSwitch::onDraw ()
{
	canvas.clear (bg);
	int fh = wk_fh (), ph = fh, pw = 2 * fh, py = (height - ph) / 2, r = ph / 2;
	unsigned track = on ? 0x0040A060 : 0x00404A5A;
	if (disabled) track = 0x00303840;
	disc (canvas, r, py + r, r, track);				// the pill: two discs + a bar
	disc (canvas, pw - r - 1, py + r, r, track);
	canvas.fillRect (r, py, pw - 2 * r, ph + 1, track);
	int kx = on ? pw - r - 1 : r;					// the knob
	disc (canvas, kx, py + r, r - 2, (!disabled && hover) ? 0x00FFFFFF : 0x00E0E6EE);
	if (hasFocus) canvas.frameRect (pw + 4, (height - fh) / 2 - 1, wk_len (text) * wk_fw () + 4, fh + 2, C_FACE);
	canvas.text (pw + 6, (height - fh) / 2, text, disabled ? C_DIS : C_TEXT);
}

bool ToggleSwitch::onMouse (int mx, int, int bl, int, int, int)
{
	if (mx < 0) { if (hover) { hover = false; invalidate (true); } pressed = false; return false; }
	if (disabled) return true;
	bool wh = hover; hover = true;
	if (bl && !pressed) { pressed = true; setFocus (); }
	else if (!bl && pressed) { pressed = false; on = !on; if (cb) cb (*this); invalidate (true); }
	if (hover != wh) invalidate (true);
	return true;
}

bool ToggleSwitch::onKey (long k)
{
	if (k == ' ' || k == KEY_ENTER) { on = !on; if (cb) cb (*this); invalidate (true); return true; }
	return false;
}

// ---- NumericUpDown -------------------------------------------------------------------------
NumericUpDown::NumericUpDown (int l, int t, int w, int h, int lo, int hi, int val, int step_, Action cb_)
  : Widget (l, t, w, h), value (val), vmin (lo), vmax (hi), step (step_ > 0 ? step_ : 1), cb (cb_), m_elen (-1)
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

void NumericUpDown::onDraw ()
{
	int fh = wk_fh (), fw = wk_fw (), bw = 16;
	canvas.clear (C_FIELD);
	canvas.frameRect (0, 0, width, height, hasFocus ? C_ACCENT : C_BORDER);
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
	canvas.text (width - bw - 6 - p * fw, (height - fh) / 2, b, disabled ? C_DIS : C_TEXT);	// right-aligned
	if (m_elen >= 0) canvas.fillRect (width - bw - 5, (height - fh) / 2, 2, fh, C_ACCENT);	// caret
	int bx = width - bw, hh = height / 2;
	canvas.fillRect (bx, 1, bw - 1, hh - 1, C_FACE);
	canvas.fillRect (bx, hh, bw - 1, height - hh - 1, C_FACE);
	canvas.fillRect (bx, hh, bw - 1, 1, C_BORDER);
	for (int i = 0; i < 4; i++)				// the arrows
	{
		canvas.fillRect (bx + bw / 2 - i - 1, hh / 2 - 2 + i, 2 * i + 1, 1, C_TEXT);
		canvas.fillRect (bx + bw / 2 - i - 1, hh + (height - hh) / 2 + 1 - i, 2 * i + 1, 1, C_TEXT);
	}
}

bool NumericUpDown::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; return false; }
	if (disabled) return true;
	if (wheel) { commit (); setValue (value + (wheel > 0 ? step : -step)); return true; }
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (mx >= width - 16) { commit (); setValue (value + (my < height / 2 ? step : -step)); }
	}
	else if (!bl) pressed = false;
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
