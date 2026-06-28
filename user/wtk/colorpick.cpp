#include "wtk/colorpick.h"

namespace wtk {

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

void ColorPicker::setOpen (bool o)
{
	if (o == open) return;
	open = o;
	catchOutside = o;
	int gw = PAL_COLS * PAL_CELL, gh = PAL_ROWS * PAL_CELL;
	resizeTo (o ? (m_boxW > gw ? m_boxW : gw) : m_boxW, o ? m_boxH + 2 + gh : m_boxH);
	if (o) bringToFront ();
	invalidate (true);
	if (parent) parent->invalidate (true);
}

void ColorPicker::onDraw ()
{
	canvas.fillRect (0, 0, m_boxW, m_boxH, color);
	canvas.frameRect (0, 0, m_boxW, m_boxH, 0x00FFFFFF);
	canvas.frameRect (-1, -1, m_boxW + 2, m_boxH + 2, 0x00000000);
	if (open)
		for (int r = 0; r < PAL_ROWS; r++)
			for (int c = 0; c < PAL_COLS; c++)
			{
				int gx = c * PAL_CELL, gy = m_boxH + 2 + r * PAL_CELL;
				canvas.fillRect (gx, gy, PAL_CELL, PAL_CELL, pal_color (r * PAL_COLS + c));
				canvas.frameRect (gx, gy, PAL_CELL, PAL_CELL, 0x00303030);
			}
}

bool ColorPicker::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0) { pressed = false; return false; }
	if (bl && !pressed)
	{
		pressed = true;
		if (open)
		{
			int gy0 = m_boxH + 2;
			if (mx >= 0 && mx < PAL_COLS * PAL_CELL && my >= gy0 && my < gy0 + PAL_ROWS * PAL_CELL)
			{
				int c = mx / PAL_CELL, r = (my - gy0) / PAL_CELL;
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

} // namespace wtk
