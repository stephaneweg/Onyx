#include "wtk/dropdown.h"

namespace wtk {

enum { DD_ARROW_W = 20 };	// the drop button (as Combobox::ARROW_W)

Dropdown::Dropdown (int l, int t, int w, int h, const char *const *options, int n, int initial, Action cb_)
  : Widget (l, t, w, h), opts (options), nopts (n), sel (initial), rowH (h), open (false), cb (cb_)
{ canFocus = true; }

void Dropdown::setOptions (const char *const *options, int n, int initial)
{ setOpen (false); opts = options; nopts = n; sel = (initial >= 0 && initial < n) ? initial : 0; invalidate (true); }

void Dropdown::setOpen (bool o)
{
	if (o && nopts == 0) o = false;
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
	int fh = wk_fh (), ax = width - DD_ARROW_W;
	canvas.clear (disabled ? C_FACE_DN : C_FIELD);
	// The box: like a Textbox (field colour, border, accent frame when focused).
	canvas.frameRect (0, 0, ax + 1, rowH, C_BORDER);
	if (hasFocus && !disabled) canvas.frameRect (1, 1, ax - 1, rowH - 2, C_ACCENT);
	if (sel >= 0 && sel < nopts) canvas.text (4, (rowH - fh) / 2, opts[sel], disabled ? C_DIS : C_TEXT);
	// The drop button: a raised face with a triangle (down; up while open).
	canvas.fillRect (ax, 0, DD_ARROW_W, rowH, open ? C_FACE_DN : (hover && !disabled ? C_FACE_HI : C_FACE));
	canvas.frameRect (ax, 0, DD_ARROW_W, rowH, C_BORDER);
	unsigned tc = (nopts && !disabled) ? C_TEXT : C_DIS;
	int cx = ax + DD_ARROW_W / 2, cy = rowH / 2;
	for (int r = 0; r < 4; r++)
		canvas.fillRect (cx - 3 + r, open ? cy + 1 - r : cy - 1 + r, 7 - 2 * r, 1, tc);
	if (open)
		for (int i = 0; i < nopts; i++)
		{
			int ry = rowH + i * rowH;
			canvas.fillRect (0, ry, width, rowH, i == sel ? C_FACE_HI : C_FIELD);
			canvas.frameRect (0, ry, width, rowH, C_BORDER);
			canvas.text (6, ry + (rowH - fh) / 2, opts[i], C_TEXT);
		}
}

bool Dropdown::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0 && !open) { if (hover) { hover = false; invalidate (true); } pressed = false; return false; }
	bool wh = hover; hover = mx >= 0 && my >= 0 && mx < width && my < rowH; if (hover != wh) invalidate (true);
	if (disabled) return true;
	if (bl && !pressed)
	{
		pressed = true;
		if (open)
		{
			if (mx >= 0 && mx < width && my >= rowH && my < rowH + nopts * rowH)
			{
				int ns = (my - rowH) / rowH;
				bool changed = ns >= 0 && ns < nopts && ns != sel;
				if (ns >= 0 && ns < nopts) sel = ns;
				setOpen (false);
				if (changed && cb) cb (*this);
			}
			else setOpen (false);			// box or outside -> close
		}
		else if (mx >= 0 && mx < width && my >= 0 && my < rowH)
		{ setFocus (); setOpen (true); }
	}
	else if (!bl) pressed = false;
	return true;
}

bool Dropdown::onKey (long k)
{
	if (disabled || nopts == 0) return false;
	if (k == KEY_DOWN || k == KEY_UP)
	{
		int ns = sel + (k == KEY_DOWN ? 1 : -1);
		if (ns < 0) ns = 0;
		if (ns >= nopts) ns = nopts - 1;
		if (ns != sel) { sel = ns; invalidate (true); if (cb) cb (*this); }
		return true;
	}
	if (k == KEY_ENTER || k == ' ') { setOpen (!open); return true; }
	if (k == 27 && open) { setOpen (false); return true; }
	return false;
}

} // namespace wtk
