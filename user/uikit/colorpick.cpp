#include "uikit/colorpick.h"

namespace uikit {

enum { PAL_COLS = 8, PAL_ROWS = 5, PAL_CELL = 18 };

// 40-colour palette: a grey ramp + four hue rows at increasing brightness (matches the
// old applib ax_palette so themes look the same).
static unsigned pal_color (int i)
{
	static const unsigned pal[PAL_COLS * PAL_ROWS] = {
		0x00000000,0x00202020,0x00404040,0x00606060,0x00909090,0x00C0C0C0,0x00E8E8E8,0x00FFFFFF,
		0x00400000,0x00800000,0x00C02020,0x00FF4040,0x00FF8080,0x00FFC0C0,0x00FFE0A0,0x00FFD040,
		0x00204000,0x00308020,0x0040C040,0x0060FF60,0x00A0FFA0,0x0020C0A0,0x0040E0D0,0x00A0FFE0,
		0x00002040,0x00204080,0x004078C0,0x004890E0,0x0080B0FF,0x00A0C8FF,0x00C0D8FF,0x00E0ECFF,
		0x00200040,0x00502080,0x008040C0,0x00B060E0,0x00D0A0FF,0x00FF60C0,0x00FFA0D8,0x00FFD0A0,
	};
	return (i >= 0 && i < PAL_COLS * PAL_ROWS) ? pal[i] : 0;
}

ColorPicker::ColorPicker (int l, int t, int w, int h, unsigned initial, Action cb_)
  : Widget (l, t, w, h), color (initial), open (false), cb (cb_), m_boxW (w), m_boxH (h)
{ canFocus = true; }

enum { PAL_PAD = 4, PAL_GAP = 2 };	// the palette panel: its padding, its gap below the swatch

void ColorPicker::setOpen (bool o)
{
	if (o == open) return;
	open = o;
	catchOutside = o;
	transparent = o;					// (the panel's rounded corners: see-through)
	int gw = PAL_COLS * PAL_CELL + 2 * PAL_PAD, gh = PAL_ROWS * PAL_CELL + 2 * PAL_PAD;
	resizeTo (o ? (m_boxW > gw ? m_boxW : gw) : m_boxW, o ? m_boxH + PAL_GAP + gh : m_boxH);
	if (o) bringToFront ();
	invalidate (true);
	if (parent) parent->invalidate (true);
}

// The swatch: the colour in a rounded well (a dark line, a light one inside); open, the palette
// below it in a floating panel.
void ColorPicker::onDraw ()
{
	canvas.clear (open ? UK_TRANSPARENT_KEY : bgColor ());
	if (open) canvas.fillRect (0, 0, m_boxW, m_boxH, bgColor ());
	uk_rbox (canvas, 0, 0, m_boxW, m_boxH, 4, color, color);
	uk_rline (canvas, 1, 1, m_boxW - 2, m_boxH - 2, 3, 0x00FFFFFF, 150);
	uk_rline (canvas, 0, 0, m_boxW, m_boxH, 4, hover || open ? C_ACCENT : uk_tone (C_FACE, 60), 230);
	if (open)
	{
		int y0 = m_boxH + PAL_GAP;
		uk_popup (canvas, 0, y0, PAL_COLS * PAL_CELL + 2 * PAL_PAD, PAL_ROWS * PAL_CELL + 2 * PAL_PAD, 6, C_FIELD);
		for (int r = 0; r < PAL_ROWS; r++)
			for (int c = 0; c < PAL_COLS; c++)
			{
				unsigned pc = pal_color (r * PAL_COLS + c);
				int gx = PAL_PAD + c * PAL_CELL + 2, gy = y0 + PAL_PAD + r * PAL_CELL + 2, cs = PAL_CELL - 4;
				uk_rbox (canvas, gx, gy, cs, cs, 3, pc, pc);
				uk_rline (canvas, gx, gy, cs, cs, 3, pc == color ? C_ACCENT : uk_tone (C_FACE, 70), pc == color ? 255 : 150);
			}
	}
}

bool ColorPicker::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0) { pressed = false; if (hover) { hover = false; invalidate (true); } return false; }
	bool h = mx < m_boxW && my < m_boxH;
	if (h != hover) { hover = h; invalidate (true); }
	if (bl && !pressed)
	{
		pressed = true;
		if (open)
		{
			int gx0 = PAL_PAD, gy0 = m_boxH + PAL_GAP + PAL_PAD;
			if (mx >= gx0 && mx < gx0 + PAL_COLS * PAL_CELL && my >= gy0 && my < gy0 + PAL_ROWS * PAL_CELL)
			{
				int c = (mx - gx0) / PAL_CELL, r = (my - gy0) / PAL_CELL;
				color = pal_color (r * PAL_COLS + c);
				setOpen (false);
				if (cb) cb (*this);
			}
			else setOpen (false);
		}
		else if (mx >= 0 && mx < m_boxW && my >= 0 && my < m_boxH)
			setOpen (true);
	}
	else if (!bl) pressed = false;
	return true;
}

} // namespace uikit
