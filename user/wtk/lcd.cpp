//
// wtk/lcd.cpp -- LcdDisplay (wtk/lcd.h).
//
#include "wtk/lcd.h"
#include "wtk/font.h"

namespace wtk {

static bool lcd_set (char *d, const char *s)		// (copy; false: unchanged)
{
	char t[32]; int i = 0;
	for (; s && s[i] && i < 31; i++) t[i] = s[i];
	t[i] = '\0';
	bool same = true;
	for (int k = 0; k <= i; k++) if (d[k] != t[k]) { same = false; break; }
	if (same) return false;
	for (int k = 0; k <= i; k++) d[k] = t[k];
	return true;
}

LcdDisplay::LcdDisplay (int l, int t, int w, int h, const char *text, const char *caption)
  : Widget (l, t, w, h), face (0), smallFace (0), scale (0), ink (WK_AUTO), centred (false)
{
	m_text[0] = m_cap[0] = m_sub[0] = '\0';
	lcd_set (m_text, text); lcd_set (m_cap, caption);
}

void LcdDisplay::setText (const char *s)    { if (lcd_set (m_text, s)) invalidate (true); }
void LcdDisplay::setCaption (const char *s) { if (lcd_set (m_cap, s)) invalidate (true); }
void LcdDisplay::setSub (const char *s)     { if (lcd_set (m_sub, s)) invalidate (true); }

void LcdDisplay::onDraw ()
{
	bool dark = wk_bright (C_BG) < 110;
	unsigned well = dark ? wk_tone (C_BG, 74) : wk_mix (C_FIELD, C_ACCENT, 30);
	unsigned dig = ink != WK_AUTO ? ink : dark ? wk_tone (C_ACCENT, 186) : wk_tone (C_ACCENT, 58);
	if (disabled) dig = wk_mix (well, dig, 110);
	unsigned dim = wk_mix (well, dig, 130);
	canvas.clear (bgColor ());
	wk_sunken (canvas, 0, 0, width, height, 5, well, false);
	int right = width - 8;					// the small lines' column, at the right
	{
		WkFaceScope sc (smallFace);
		int lh = wk_fh (), cw = 0;
		int a = m_cap[0] ? wk_tw (m_cap) : 0, b = m_sub[0] ? wk_tw (m_sub) : 0;
		cw = a > b ? a : b;
		if (cw > 0)
		{
			int x = right - cw, n = (m_cap[0] ? 1 : 0) + (m_sub[0] ? 1 : 0), y = (height - n * lh) / 2;
			if (m_cap[0]) { wk_text (canvas, x, y, m_cap, dim); y += lh; }
			if (m_sub[0]) wk_text (canvas, x, y, m_sub, dig);
			right = x - 10;
		}
	}
	int x0 = 10;
	if (face)							// the digits: a face...
	{
		WkFaceScope sc (face);
		int tw = wk_tw (m_text), x = centred ? x0 + (right - x0 - tw) / 2 : x0;
		wk_text_clip (canvas, x, (height - wk_fh ()) / 2, m_text, dig, 0, 2, 2, right - 2 + 6, height - 4);
		return;
	}
	Font &f = font ();						// ... or the bitmap font, scaled
	if (!f.valid ()) { canvas.text (x0, (height - wk_bfh ()) / 2, m_text, dig); return; }
	int n = 0; while (m_text[n]) n++;
	int sc = scale > 0 ? scale : (height - 2) / f.height ();
	if (scale <= 0) while (sc > 1 && n * f.width () * sc > right - x0) sc--;	// (auto: as large as fits)
	if (sc < 1) sc = 1;
	int tw = n * f.width () * sc, x = centred ? x0 + (right - x0 - tw) / 2 : x0;
	int cw = right + 4 < width - 2 ? right + 4 - 2 : width - 4;	// (clipped to the digits' room)
	if (cw < 1 || height < 5) return;
	Canvas room; room.adopt (canvas.px + 2 * canvas.stride + 2, cw, height - 4, canvas.stride);
	room.drawFont (x - 2, (height - f.height () * sc) / 2 - 2, m_text, f, dig, sc, 2);
}

} // namespace wtk
