#include "uikit/dropdown.h"
#include "uikit/lift.h"		// the open list over everything

namespace uikit {

// The open list: a floating panel DD_GAP px below the box, its rows DD_PAD px inside it.
enum { DD_GAP = 2, DD_PAD = 3 };

Dropdown::Dropdown (int l, int t, int w, int h, const char *const *options, int n, int initial, Action cb_)
  : Widget (l, t, w, h), opts (options), nopts (n), sel (initial), rowH (h), open (false), cb (cb_), m_hot (-1),
    m_up (false), m_top0 (t)
{ canFocus = true; }

void Dropdown::setOptions (const char *const *options, int n, int initial)
{ setOpen (false); opts = options; nopts = n; sel = (initial >= 0 && initial < n) ? initial : 0; invalidate (true); }

int Dropdown::listTop () const { return m_up ? DD_PAD : rowH + DD_GAP + DD_PAD; }

int Dropdown::rowAt (int mx, int my) const
{
	if (!open || mx < 0 || mx >= width || my < listTop ()) return -1;
	int i = (my - listTop ()) / rowH;
	return i < nopts ? i : -1;			// (upward: the box's row is past the last one)
}

void Dropdown::setOpen (bool o)
{
	if (o && nopts == 0) o = false;
	if (o == open) return;
	open = o;
	m_hot = -1;
	catchOutside = o;					// while open, grab clicks anywhere (to close)
	transparent = o;					// (the list's rounded corners: see-through)
	int list = DD_GAP + 2 * DD_PAD + nopts * rowH;
	if (o)
	{
		uk_lift (this);						// out of its parents: only the window clips it now
		// the room below the box and above it, within every parent (each clips its children)
		int below = 1 << 30, above = 1 << 30, y = top;
		for (const Widget *p = parent; p; p = p->parent)
		{
			int yc = y - p->scrollY;
			if (p->height - (yc + rowH) < below) below = p->height - (yc + rowH);
			if (yc < above) above = yc;
			y = p->top + yc;
		}
		m_top0 = top;
		m_up = below < list && above > below;
		if (m_up) top -= list;				// (the box stays where it was)
	}
	else if (m_up) { top = m_top0; m_up = false; }
	resizeTo (width, o ? rowH + list : rowH);		// grow / shrink back
	if (o) bringToFront ();					// draw the list over later siblings
	else uk_unlift (this);					// back where it was
	invalidate (true);
	if (parent) parent->invalidate (true);			// repaint behind a closing popup
}

// Its options in a floating panel below a box: the box a raised button (the chosen option, a
// chevron), the list light, the row under the pointer in the accent.
void uk_draw_option_list (Canvas &cv, int y0, int w, int rowH, const char *const *opts, int n,
			  int sel, int hot)
{
	int fh = uk_fh ();
	uk_popup (cv, 0, y0, w, n * rowH + 2 * DD_PAD, 6, C_FIELD);
	for (int i = 0; i < n; i++)
	{
		int ry = y0 + DD_PAD + i * rowH;
		bool h = i == hot || (hot < 0 && i == sel);
		if (h) uk_hilite (cv, DD_PAD, ry, w - 2 * DD_PAD, rowH, 4, i == hot);
		cv.text (10, ry + (rowH - fh) / 2, opts[i], h ? uk_hilite_ink (i == hot) : C_FIELD_TEXT);
	}
}

void Dropdown::onDraw ()
{
	int fh = uk_fh (), by = boxY ();
	canvas.clear (UK_TRANSPARENT_KEY);
	canvas.fillRect (0, by, width, rowH, bgColor ());
	int st = disabled ? UK_DISABLED : open ? UK_PRESSED : hover ? UK_HOT : UK_NORMAL;
	if (hasFocus && !disabled && !open) st |= UK_FOCUS;
	uk_raised (canvas, 0, by, width, rowH, 5, C_BUTTON, st);
	int d = open ? 1 : 0;
	unsigned dis = uk_mix (C_BUTTON, C_BUTTON_TEXT, 110);
	if (sel >= 0 && sel < nopts) canvas.text (9 + d, by + (rowH - fh) / 2 + d, opts[sel], disabled ? dis : C_BUTTON_TEXT);
	uk_glyph (canvas, open != m_up ? WKG_CHEV_UP : WKG_CHEV_DOWN, width - 13 + d, by + rowH / 2 + d, 9,
		  (nopts && !disabled) ? C_BUTTON_TEXT : dis);
	if (open) uk_draw_option_list (canvas, m_up ? 0 : rowH + DD_GAP, width, rowH, opts, nopts, sel, m_hot);
}

bool Dropdown::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0 && !open) { if (hover) { hover = false; invalidate (true); } pressed = false; return false; }
	int by = boxY ();
	bool wh = hover; hover = mx >= 0 && my >= by && mx < width && my < by + rowH; if (hover != wh) invalidate (true);
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
		else if (mx >= 0 && mx < width && my >= by && my < by + rowH)
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

} // namespace uikit
