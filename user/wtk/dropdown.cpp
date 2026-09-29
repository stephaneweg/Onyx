#include "wtk/dropdown.h"

namespace wtk {

// The open list: a floating panel DD_GAP px below the box, its rows DD_PAD px inside it.
enum { DD_GAP = 2, DD_PAD = 3 };

Dropdown::Dropdown (int l, int t, int w, int h, const char *const *options, int n, int initial, Action cb_)
  : Widget (l, t, w, h), opts (options), nopts (n), sel (initial), rowH (h), open (false), cb (cb_), m_hot (-1)
{ canFocus = true; }

void Dropdown::setOptions (const char *const *options, int n, int initial)
{ setOpen (false); opts = options; nopts = n; sel = (initial >= 0 && initial < n) ? initial : 0; invalidate (true); }

int Dropdown::listTop () const { return rowH + DD_GAP + DD_PAD; }

int Dropdown::rowAt (int mx, int my) const
{
	if (!open || mx < 0 || mx >= width || my < listTop ()) return -1;
	int i = (my - listTop ()) / rowH;
	return i < nopts ? i : -1;
}

void Dropdown::setOpen (bool o)
{
	if (o && nopts == 0) o = false;
	if (o == open) return;
	open = o;
	m_hot = -1;
	catchOutside = o;					// while open, grab clicks anywhere (to close)
	transparent = o;					// (the list's rounded corners: see-through)
	resizeTo (width, o ? listTop () + nopts * rowH + DD_PAD : rowH);	// grow downward / shrink back
	if (o) bringToFront ();					// draw the list over later siblings
	invalidate (true);
	if (parent) parent->invalidate (true);			// repaint behind a closing popup
}

// Its options in a floating panel below a box: the box a raised button (the chosen option, a
// chevron), the list light, the row under the pointer in the accent.
void wk_draw_option_list (Canvas &cv, int y0, int w, int rowH, const char *const *opts, int n,
			  int sel, int hot)
{
	int fh = wk_fh ();
	wk_popup (cv, 0, y0, w, n * rowH + 2 * DD_PAD, 6, C_FIELD);
	for (int i = 0; i < n; i++)
	{
		int ry = y0 + DD_PAD + i * rowH;
		bool h = i == hot || (hot < 0 && i == sel);
		if (h) wk_hilite (cv, DD_PAD, ry, w - 2 * DD_PAD, rowH, 4, i == hot);
		cv.text (10, ry + (rowH - fh) / 2, opts[i], h ? wk_hilite_ink (i == hot) : C_FIELD_TEXT);
	}
}

void Dropdown::onDraw ()
{
	int fh = wk_fh ();
	canvas.clear (WK_TRANSPARENT_KEY);
	canvas.fillRect (0, 0, width, rowH, bgColor ());
	int st = disabled ? WK_DISABLED : open ? WK_PRESSED : hover ? WK_HOT : WK_NORMAL;
	if (hasFocus && !disabled && !open) st |= WK_FOCUS;
	wk_raised (canvas, 0, 0, width, rowH, 5, C_BUTTON, st);
	int d = open ? 1 : 0;
	unsigned dis = wk_mix (C_BUTTON, C_BUTTON_TEXT, 110);
	if (sel >= 0 && sel < nopts) canvas.text (9 + d, (rowH - fh) / 2 + d, opts[sel], disabled ? dis : C_BUTTON_TEXT);
	wk_glyph (canvas, open ? WKG_CHEV_UP : WKG_CHEV_DOWN, width - 13 + d, rowH / 2 + d, 9,
		  (nopts && !disabled) ? C_BUTTON_TEXT : dis);
	if (open) wk_draw_option_list (canvas, rowH + DD_GAP, width, rowH, opts, nopts, sel, m_hot);
}

bool Dropdown::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0 && !open) { if (hover) { hover = false; invalidate (true); } pressed = false; return false; }
	bool wh = hover; hover = mx >= 0 && my >= 0 && mx < width && my < rowH; if (hover != wh) invalidate (true);
	if (disabled) return true;
	int hot = rowAt (mx, my);
	if (open && hot != m_hot) { m_hot = hot; invalidate (true); }
	if (bl && !pressed)
	{
		pressed = true;
		if (open)
		{
			if (hot >= 0)
			{
				bool changed = hot != sel;
				sel = hot;
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
		if (ns != sel) { sel = ns; m_hot = -1; invalidate (true); if (cb) cb (*this); }
		return true;
	}
	if (k == KEY_ENTER || k == ' ') { setOpen (!open); return true; }
	if (k == 27 && open) { setOpen (false); return true; }
	return false;
}

} // namespace wtk
