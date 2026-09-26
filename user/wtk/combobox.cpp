#include "wtk/combobox.h"

namespace wtk {

Combobox::Combobox (int l, int t, int w, int h, const char *s, Action enter, Action pick_)
  : Textbox (l, t, w, h, s, enter), nopts (0), picked (-1), rowH (h), open (false), onPick (pick_)
{ canFocus = true; }

void Combobox::clearOptions () { setOpen (false); nopts = 0; picked = -1; invalidate (true); }

void Combobox::addOption (const char *s)
{
	if (nopts >= MAXOPT) return;
	int i = 0; for (; s[i] && i < 63; i++) opts[nopts][i] = s[i]; opts[nopts][i] = '\0';
	nopts++;
}

void Combobox::pick (int i)
{
	if (i < 0 || i >= nopts) return;
	picked = i; setText (opts[i]);
	if (onPick) onPick (*this);
}

void Combobox::setOpen (bool o)
{
	if (o && nopts == 0) o = false;
	if (o == open) return;
	open = o;
	catchOutside = o;					// while open, grab clicks anywhere (to close)
	resizeTo (width, o ? rowH + nopts * rowH : rowH);	// grow downward / shrink back
	if (o) bringToFront ();
	invalidate (true);
	if (parent) parent->invalidate (true);			// repaint behind a closing list
}

void Combobox::onDraw ()
{
	// The edit part: a Textbox drawn in the top row, left of the arrow button.
	int h = height, w = width;
	height = rowH; width = w - ARROW_W + 1;
	Textbox::onDraw ();
	height = h; width = w;
	int fh = wk_fh (), ax = w - ARROW_W;
	// The drop button: a raised face with a triangle (dimmed when there is nothing to list).
	canvas.fillRect (ax, 0, ARROW_W, rowH, open ? C_FACE_DN : (hover && nopts ? C_FACE_HI : C_FACE));
	canvas.frameRect (ax, 0, ARROW_W, rowH, C_BORDER);
	unsigned tc = nopts ? C_TEXT : C_DIS;
	int cx = ax + ARROW_W / 2, cy = rowH / 2;
	for (int r = 0; r < 4; r++)				// 7-px wide triangle, down (up when open)
		canvas.fillRect (cx - 3 + r, open ? cy + 1 - r : cy - 1 + r, 7 - 2 * r, 1, tc);
	(void) fh;
	if (open)
		for (int i = 0; i < nopts; i++)
		{
			int ry = rowH + i * rowH;
			canvas.fillRect (0, ry, w, rowH, i == picked ? C_FACE_HI : C_FIELD);
			canvas.frameRect (0, ry, w, rowH, C_BORDER);
			canvas.text (6, ry + (rowH - fh) / 2, opts[i], C_TEXT);
		}
}

bool Combobox::onMouse (int mx, int my, int bl, int br, int bm, int wheel)
{
	if (mx < 0 && !open) return Textbox::onMouse (mx, my, bl, br, bm, wheel);
	if (bl && !pressed)
	{
		bool inBox = mx >= 0 && mx < width && my >= 0 && my < rowH;
		if (open)
		{
			pressed = true;
			if (mx >= 0 && mx < width && my >= rowH && my < rowH + nopts * rowH)
			{
				int i = (my - rowH) / rowH;
				setOpen (false);
				pick (i);
			}
			else setOpen (false);				// the box or outside -> close
			if (inBox && mx < width - ARROW_W) { pressed = false; return Textbox::onMouse (mx, my, bl, br, bm, wheel); }
			return true;
		}
		if (inBox && mx >= width - ARROW_W) { pressed = true; setFocus (); setOpen (true); return true; }
	}
	if (mx < 0 || my >= rowH) { if (!bl) pressed = false; return open; }
	return Textbox::onMouse (mx, my, bl, br, bm, wheel);
}

bool Combobox::onKey (long k)
{
	if (open && (k == 27 || k == KEY_ENTER)) { setOpen (false); return true; }
	if (k == KEY_DOWN || k == KEY_UP)
	{
		if (nopts == 0) return false;
		int i = picked < 0 ? (k == KEY_DOWN ? 0 : nopts - 1) : (picked + (k == KEY_DOWN ? 1 : nopts - 1)) % nopts;
		pick (i);
		return true;
	}
	return Textbox::onKey (k);
}

} // namespace wtk
