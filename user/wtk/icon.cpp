#include "wtk/icon.h"
#include "bmp.hpp"		// ui::bmp_decode
// operator delete[] resolves at link from the app's onyxpp.hpp (see canvas.cpp).

namespace wtk {

Icon::Icon (int l, int t, int w, int h, const char *bmp, const char *label, Action cb_, unsigned bg_)
  : Widget (l, t, w, h), pix (0), iw (0), ih (0), badged (false), cb (cb_), bg (bg_)
{
	canFocus = true;
	int i = 0; if (label) for (; label[i] && i < 63; i++) text[i] = label[i]; text[i] = '\0';
	if (bmp && bmp[0]) pix = ui::bmp_decode (bmp, &iw, &ih);
}

Icon::~Icon () { delete [] pix; }

void Icon::setBadge (bool b) { if (b != badged) { badged = b; invalidate (true); } }

void Icon::setIcon (const char *bmp)
{
	delete [] pix; pix = 0; iw = 0; ih = 0;
	if (bmp && bmp[0]) pix = ui::bmp_decode (bmp, &iw, &ih);
	invalidate (true);
}

void Icon::onDraw ()
{
	canvas.clear (pressed ? 0x00405468 : (hover ? 0x00303F50 : bg));
	int fh = wk_fh (), labelH = text[0] ? fh + 2 : 0;
	if (pix && iw > 0 && ih > 0)
	{
		int ix = (width - iw) / 2, iy = ((height - labelH) - ih) / 2;
		for (int yy = 0; yy < ih; yy++)
		{
			int dy = iy + yy; if (dy < 0 || dy >= height) continue;
			for (int xx = 0; xx < iw; xx++)
			{
				int dx = ix + xx; if (dx < 0 || dx >= width) continue;
				unsigned p = pix[yy * iw + xx];
				if (p == WK_TRANSPARENT_KEY) continue;
				canvas.px[dy * canvas.stride + dx] = p;	// stride, not width (grow-only buffers)
			}
		}
	}
	if (text[0])
	{
		int fw = wk_fw (), tx = (width - wk_len (text) * fw) / 2; if (tx < 0) tx = 0;
		canvas.text (tx, height - fh, text, C_TEXT);
	}
	if (badged) for (int r = 0; r < 8; r++) canvas.fillRect (width - 2 - r, 2 + r, r + 1, 1, C_ACCENT);
}

bool Icon::onMouse (int mx, int /*my*/, int bl, int, int, int)
{
	if (disabled) return false;
	if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
	bool wh = hover, wp = pressed; hover = true;
	if (bl && !pressed) pressed = true;
	else if (!bl && pressed) { pressed = false; if (cb) cb (*this); }
	if (hover != wh || pressed != wp) invalidate (true);
	return true;
}

} // namespace wtk
