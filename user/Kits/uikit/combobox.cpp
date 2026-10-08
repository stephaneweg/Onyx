#include "uikit/combobox.h"
#include "uikit/dropdown.h"		// uk_draw_option_list
#include "uikit/lift.h"		// the open list over everything

namespace uikit {

enum { CB_GAP = 2, CB_PAD = 3 };	// the open list (as Dropdown's)

Combobox::Combobox (int l, int t, int w, int h, const char *s, Action enter, Action pick_)
  : Textbox (l, t, w, h, s, enter), nopts (0), picked (-1), rowH (h), open (false), onPick (pick_), m_hot (-1)
{ canFocus = true; padR = ARROW_W; }

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

int Combobox::rowAt (int mx, int my) const
{
	int top = rowH + CB_GAP + CB_PAD;
	if (!open || mx < 0 || mx >= width || my < top) return -1;
	int i = (my - top) / rowH;
	return i < nopts ? i : -1;
}

void Combobox::setOpen (bool o)
{
	if (o && nopts == 0) o = false;
	if (o == open) return;
	open = o;
	m_hot = -1;
	catchOutside = o;					// while open, grab clicks anywhere (to close)
	transparent = o;					// (the list's rounded corners: see-through)
	if (o) uk_lift (this);					// out of its parents: only the window clips it now
	resizeTo (width, o ? rowH + CB_GAP + 2 * CB_PAD + nopts * rowH : rowH);	// grow downward / shrink back
	if (o) bringToFront ();
	else uk_unlift (this);					// back where it was
	invalidate (true);
	if (parent) parent->invalidate (true);			// repaint behind a closing list
}

void Combobox::onDraw ()
{
	// The edit part: a Textbox in the top row (its text clear of the arrow), then the arrow.
	int h = height;
	height = rowH;
	Textbox::onDraw ();
	height = h;
	int ax = width - ARROW_W, bh = rowH - 6;
	int st = !nopts || disabled ? UK_DISABLED : open ? UK_PRESSED : m_arrowHot ? UK_HOT : UK_NORMAL;
	uk_raised (canvas, ax, 3, ARROW_W - 3, bh, 3, C_BUTTON, st);
	int d = open ? 1 : 0;
	uk_glyph (canvas, open ? WKG_CHEV_UP : WKG_CHEV_DOWN, ax + (ARROW_W - 3) / 2 + d, 3 + bh / 2 + d, 8,
		  nopts && !disabled ? C_BUTTON_TEXT : uk_mix (C_BUTTON, C_BUTTON_TEXT, 110));
	if (open)
	{
		canvas.fillRect (0, rowH, width, height - rowH, UK_TRANSPARENT_KEY);
		const char *p[MAXOPT];
		for (int i = 0; i < nopts; i++) p[i] = opts[i];
		uk_draw_option_list (canvas, rowH + CB_GAP, width, rowH, p, nopts, picked, m_hot);
	}
}

bool Combobox::onMouse (int mx, int my, int bl, int br, int bm, int wheel)
{
	bool ah = mx >= width - ARROW_W && mx < width && my >= 0 && my < rowH;
	if (ah != m_arrowHot) { m_arrowHot = ah; invalidate (true); }
	if (mx < 0 && !open) return Textbox::onMouse (mx, my, bl, br, bm, wheel);
	int hot = rowAt (mx, my);
	if (open && hot != m_hot) { m_hot = hot; invalidate (true); }
	if (bl && !pressed)
	{
		bool inBox = mx >= 0 && mx < width && my >= 0 && my < rowH;
		if (open)
		{
			pressed = true;
			setOpen (false);				// the box, a row or outside: close
			if (hot >= 0) pick (hot);
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

} // namespace uikit
