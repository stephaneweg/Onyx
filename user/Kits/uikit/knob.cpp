//
// uikit/knob.cpp -- Knob (uikit/knob.h): the dial drawn from its geometry (uikit/vpaint.h: anti-aliased),
// the theme's colours; a vertical drag, the wheel, the keys.
//
#include "uikit/knob.h"
#include "uikit/vpaint.h"

namespace uikit {

#define KNOB_DBL_TICKS	40			// a double click: two presses within this (100 Hz ticks)

Knob::Knob (int l, int t, int w, int h, int lo, int hi, int val, Action cb)
  : Widget (l, t, w, h), value (val), vmin (lo), vmax (hi > lo ? hi : lo + 1), step (1), def (val), hasDef (false),
    bipolar (false), showValue (false), arcColor (UK_AUTO), face (0), format (0), onChange (cb),
    m_drag (false), m_fine (false), m_y0 (0), m_v0 (0), m_lastClick (0)
{
	canFocus = true;
	m_label[0] = '\0';
	step = (vmax - vmin) / 100; if (step < 1) step = 1;
	if (value < vmin) value = vmin;
	if (value > vmax) value = vmax;
}

void Knob::setLabel (const char *s)
{
	int i = 0; if (s) for (; s[i] && i < (int) sizeof m_label - 1; i++) m_label[i] = s[i];
	m_label[i] = '\0';
	invalidate (true);
}

void Knob::setRange (int lo, int hi)
{
	vmin = lo; vmax = hi > lo ? hi : lo + 1;
	step = (vmax - vmin) / 100; if (step < 1) step = 1;
	setValue (value);
	invalidate (true);
}

void Knob::setValue (int v, bool fire)
{
	if (v < vmin) v = vmin;
	if (v > vmax) v = vmax;
	if (v == value) return;
	value = v;
	invalidate (true);
	if (fire && onChange) onChange (*this);
}

void Knob::valueText (char *out, int cap) const
{
	if (cap < 1) return;
	if (format) { out[0] = '\0'; format (value, out, cap); return; }
	char t[12]; int n = 0, p = 0, v = value;
	if (v < 0 && p < cap - 1) { out[p++] = '-'; v = -v; }
	if (v == 0) t[n++] = '0';
	while (v && n < 11) { t[n++] = (char) ('0' + v % 10); v /= 10; }
	while (n && p < cap - 1) out[p++] = t[--n];
	out[p] = '\0';
}

void Knob::onDraw ()
{
	UkFaceScope sc (face);
	unsigned bg = bgColor (), ink = uk_ink_for (bg);
	canvas.clear (bg);
	int lh = uk_fh (), capH = lines () * lh;
	int d = width < height - capH ? width : height - capH;		// the dial's size
	if (d < 12) d = 12;
	int cx = V (width) / 2, cy = V (d) / 2;				// (1/16 px)
	int tw = d / 11 < 2 ? 2 : d / 11;				// the track's width, px
	int r = V (d) / 2 - V (tw) / 2 - 8;				// its radius (to its middle)
	int rb = r - V (tw) / 2 - V (d < 32 ? 2 : 3);			// the cap's
	int range = vmax - vmin, t = range > 0 ? (int) ((long long) (value - vmin) * 270 / range) : 0;
	int a = 225 - t;						// the value's angle (225: the start, -45: the end)
	unsigned acc = arcColor == UK_AUTO ? C_ACCENT : arcColor;
	if (disabled) acc = uk_mix (bg, acc, 110);
	unsigned track = hasFocus && !disabled ? uk_mix (bg, C_ACCENT, 70) : uk_mix (bg, ink, uk_bright (bg) < 100 ? 44 : 38);
	VPath p;
	p.arc (cx, cy, r, -45, 225, V (tw)); p.fill (canvas, track);		// the track
	p.clear ();
	if (bipolar)							// (from the zero: the top of a symmetric range)
	{
		int z = vmin < 0 && vmax > 0 ? 225 - (int) ((long long) -vmin * 270 / range) : 90;
		if (a != z) p.arc (cx, cy, r, a < z ? a : z, a < z ? z : a, V (tw));
	}
	else if (t > 0) p.arc (cx, cy, r, a, 225, V (tw));
	p.fill (canvas, acc);							// the value
	unsigned body = uk_tone (C_BUTTON, (hover || m_drag) && !disabled ? 150 : 138);
	p.clear (); p.circle (cx, cy + 8, rb); p.fill (canvas, uk_tone (C_BUTTON, 70), 120);	// (its shadow)
	p.clear (); p.circle (cx, cy, rb); p.fill (canvas, uk_tone (C_BUTTON, 96));		// the rim
	p.clear (); p.circle (cx, cy - 4, rb - 12); p.fill (canvas, body);			// the cap
	unsigned pin = disabled ? uk_mix (body, uk_ink_on (body), 110) : uk_ink_on (body);
	int pw = d >= 40 ? V (2) : 26, c = uk_cos (a), s = uk_sin (a);
	int r0 = rb * 30 / 100, r1 = rb - 18;
	p.clear (); p.line (cx + r0 * c / 16384, cy - r0 * s / 16384, cx + r1 * c / 16384, cy - r1 * s / 16384, pw);
	p.fill (canvas, pin);							// the pointer
	int y = d;								// the captions
	if (m_label[0]) { uk_text_c (canvas, 0, y, width, lh, m_label, disabled ? uk_mix (bg, ink, 90) : uk_mix (bg, ink, 170)); y += lh; }
	if (showValue)
	{
		char b[32]; valueText (b, sizeof b);
		uk_text_c (canvas, 0, y, width, lh, b, disabled ? uk_mix (bg, ink, 110) : ink);
	}
}

bool Knob::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	bool in = mx >= 0 && my >= 0 && mx < width && my < height;
	if (m_drag)							// (catchOutside: the whole drag)
	{
		if (!bl) { m_drag = false; catchOutside = false; pressed = false; hover = in; invalidate (true); return true; }
		bool fine = (kapi_get_modifiers () & MOD_SHIFT) != 0;
		if (fine != m_fine) { m_fine = fine; m_y0 = my; m_v0 = value; }	// (no jump when Shift changes)
		int span = fine ? 1000 : 200;
		setValue (m_v0 + (int) ((long long) (m_y0 - my) * (vmax - vmin) / span), true);
		return true;
	}
	if (!in) { if (hover) { hover = false; invalidate (true); } return false; }
	if (disabled) return true;
	if (!hover) { hover = true; invalidate (true); }
	if (wheel) { setValue (value + (wheel > 0 ? step : -step), true); return true; }
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		unsigned now = kapi_get_ticks ();
		if (hasDef && m_lastClick && now - m_lastClick < KNOB_DBL_TICKS)	// a double click: the default
		{ m_lastClick = 0; setValue (def, true); return true; }
		m_lastClick = now;
		m_drag = true; catchOutside = true;
		m_fine = (kapi_get_modifiers () & MOD_SHIFT) != 0;
		m_y0 = my; m_v0 = value;
		invalidate (true);
	}
	else if (!bl) pressed = false;
	return true;
}

bool Knob::onKey (long k)
{
	if (disabled) return false;
	switch (k)
	{
	case KEY_UP: case KEY_RIGHT:   setValue (value + step, true); return true;
	case KEY_DOWN: case KEY_LEFT:  setValue (value - step, true); return true;
	case KEY_PGUP:                 setValue (value + 10 * step, true); return true;
	case KEY_PGDN:                 setValue (value - 10 * step, true); return true;
	case KEY_HOME:                 setValue (vmin, true); return true;
	case KEY_END:                  setValue (vmax, true); return true;
	case KEY_DEL:                  if (hasDef) { setValue (def, true); return true; } return false;
	}
	return false;
}

} // namespace uikit
