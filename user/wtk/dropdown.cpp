#include "wtk/dropdown.h"

namespace wtk {

Dropdown::Dropdown (int l, int t, int w, int h, const char *const *options, int n, int initial, Action cb_)
  : Widget (l, t, w, h), opts (options), nopts (n), sel (initial), rowH (h), open (false), cb (cb_)
{ canFocus = true; }

void Dropdown::setOptions (const char *const *options, int n, int initial)
{ opts = options; nopts = n; sel = (initial >= 0 && initial < n) ? initial : 0; invalidate (true); }

void Dropdown::setOpen (bool o)
{
	if (o == open) return;
	open = o;
	catchOutside = o;					// while open, grab clicks anywhere (to close)
	resizeTo (width, o ? rowH + nopts * rowH : rowH);	// grow downward / shrink back
	if (o) bringToFront ();					// draw the list over later siblings
	invalidate (true);
	if (parent) parent->invalidate (true);			// repaint behind a closing popup
}

void Dropdown::onDraw ()
{
	int fh = wk_fh ();
	canvas.fillRect (0, 0, width, rowH, 0x00203040);
	canvas.frameRect (0, 0, width, rowH, 0x00708CA8);
	if (sel >= 0 && sel < nopts) canvas.text (6, (rowH - fh) / 2, opts[sel], 0x00E6E6E6);
	canvas.text (width - 12, (rowH - fh) / 2, open ? "^" : "v", 0x0090B0C8);
	if (open)
		for (int i = 0; i < nopts; i++)
		{
			int ry = rowH + i * rowH;
			canvas.fillRect (0, ry, width, rowH, i == sel ? 0x00355070 : 0x00182838);
			canvas.frameRect (0, ry, width, rowH, 0x00405468);
			canvas.text (6, ry + (rowH - fh) / 2, opts[i], 0x00E6E6E6);
		}
}

bool Dropdown::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0) { if (hover) { hover = false; invalidate (true); } pressed = false; return false; }
	bool wh = hover; hover = true; if (hover != wh) invalidate (true);
	if (bl && !pressed)
	{
		pressed = true;
		if (open)
		{
			if (mx >= 0 && mx < width && my >= rowH && my < rowH + nopts * rowH)
			{
				int ns = (my - rowH) / rowH;
				if (ns >= 0 && ns < nopts) sel = ns;
				setOpen (false);
				if (cb) cb (*this);
			}
			else setOpen (false);			// box or outside -> close
		}
		else if (mx >= 0 && mx < width && my >= 0 && my < rowH)
			setOpen (true);
	}
	else if (!bl) pressed = false;
	return true;
}

} // namespace wtk
