//
// uikit/segmented.cpp -- SegmentedControl (uikit/segmented.h): one rounded field, its segments split by
// etched lines, the chosen one the accent's (rounded only at the pill's ends).
//
#include "uikit/segmented.h"

namespace uikit {

SegmentedControl::SegmentedControl (int l, int t, int w, int h, const char *const *labels, int n, int sel, Action cb)
  : Widget (l, t, w, h), selected (sel), equalWidths (true), onChange (cb), m_n (0), m_hot (-1)
{
	canFocus = true;
	setLabels (labels, n);
	if (selected >= m_n) selected = m_n - 1;
}

void SegmentedControl::setLabels (const char *const *labels, int n)
{
	if (n < 0) n = 0;
	if (n > MAXSEG) n = MAXSEG;
	m_n = n;
	for (int i = 0; i < n; i++)
	{
		int k = 0; const char *s = labels ? labels[i] : 0;
		for (; s && s[k] && k < 31; k++) m_label[i][k] = s[k];
		m_label[i][k] = '\0';
		m_off[i] = false;
	}
	if (selected >= m_n) selected = m_n - 1;
	invalidate (true);
}

void SegmentedControl::setEnabled (int i, bool on)
{ if (i >= 0 && i < m_n && m_off[i] == on) { m_off[i] = !on; invalidate (true); } }

void SegmentedControl::select (int i, bool fire)
{
	if (i < -1 || i >= m_n || i == selected) return;
	selected = i;
	invalidate (true);
	if (fire && onChange) onChange (*this);
}

// The segments' edges: equal, or by their texts' widths (+ padding), the rest shared out.
void SegmentedControl::place ()
{
	m_x[0] = 0;
	if (m_n == 0) return;
	if (equalWidths)
	{
		for (int i = 1; i <= m_n; i++) m_x[i] = width * i / m_n;
		return;
	}
	int tw[MAXSEG], sum = 0;
	for (int i = 0; i < m_n; i++) { tw[i] = uk_tw (m_label[i]) + 20; sum += tw[i]; }
	if (sum >= width)				// (too narrow: scaled down)
	{
		int acc = 0;
		for (int i = 0; i < m_n; i++) { acc += tw[i]; m_x[i + 1] = (int) ((long long) width * acc / sum); }
		return;
	}
	int extra = width - sum, x = 0;
	for (int i = 0; i < m_n; i++) { x += tw[i] + extra / m_n + (i < extra % m_n ? 1 : 0); m_x[i + 1] = x; }
	m_x[m_n] = width;
}

int SegmentedControl::segmentAt (int mx) const
{
	for (int i = 0; i < m_n; i++) if (mx >= m_x[i] && mx < m_x[i + 1]) return i;
	return -1;
}

void SegmentedControl::onDraw ()
{
	place ();
	unsigned bg = bgColor (), face = C_BUTTON;
	canvas.clear (bg);
	const int R = height / 2 < 6 ? height / 2 : 6;
	uk_rbox (canvas, 0, 0, width, height, R, uk_tone (face, 146), uk_tone (face, 122));
	for (int i = 0; i < m_n; i++)
	{
		int x0 = m_x[i], x1 = m_x[i + 1];
		bool sel = i == selected, hot = i == m_hot && !disabled && !m_off[i];
		int corners = (i == 0 ? UK_TL | UK_BL : 0) | (i == m_n - 1 ? UK_TR | UK_BR : 0);
		if (sel)
		{
			unsigned a = disabled ? uk_mix (face, C_ACCENT, 120) : C_ACCENT;
			uk_rbox (canvas, x0, 0, x1 - x0, height, R, uk_tone (a, hot ? 150 : 140), uk_tone (a, hot ? 126 : 116), 255, corners);
		}
		else if (hot) uk_rbox (canvas, x0, 0, x1 - x0, height, R, uk_tone (face, 164), uk_tone (face, 138), 255, corners);
		else if (i > 0 && i - 1 != selected) uk_etch_v (canvas, x0, 4, height - 8, face);	// (the separator)
		unsigned ink = sel ? uk_ink_on (C_ACCENT) : C_BUTTON_TEXT;
		if (disabled || m_off[i]) ink = uk_mix (sel ? C_ACCENT : face, ink, 110);
		char b[32];
		uk_text_fit (m_label[i], x1 - x0 - 8, b, sizeof b);
		uk_text_c (canvas, x0, 0, x1 - x0, height, b, ink, sel ? 2 : 0);
	}
	uk_rline (canvas, 0, 0, width, height, R, hasFocus && !disabled ? C_ACCENT : uk_tone (face, 72), hasFocus ? 255 : 200);
}

bool SegmentedControl::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	bool in = mx >= 0 && my >= 0 && mx < width && my < height;
	place ();
	int hot = in ? segmentAt (mx) : -1;
	if (hot != m_hot) { m_hot = hot; invalidate (true); }
	if (!in) { pressed = false; return false; }
	if (disabled) return true;
	if (wheel) { int i = selected + (wheel > 0 ? -1 : 1); while (i >= 0 && i < m_n && m_off[i]) i += wheel > 0 ? -1 : 1; if (i >= 0 && i < m_n) select (i, true); return true; }
	if (bl && !pressed) { pressed = true; setFocus (); if (hot >= 0 && !m_off[hot]) select (hot, true); }
	else if (!bl) pressed = false;
	return true;
}

bool SegmentedControl::onKey (long k)
{
	if (disabled || (k != KEY_LEFT && k != KEY_RIGHT)) return false;
	int d = k == KEY_LEFT ? -1 : 1, i = selected + d;
	while (i >= 0 && i < m_n && m_off[i]) i += d;
	if (i >= 0 && i < m_n) select (i, true);
	return true;
}

} // namespace uikit
