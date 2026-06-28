#include "wtk/splitter.h"

namespace wtk {

// ---- the draggable divider -------------------------------------------------
// A thin child widget sitting between the two panes. While the button is held it
// sets catchOutside (wtk's mouse-capture flag) so it keeps receiving moves even when
// the cursor wanders onto a pane, and returns true to block the panes underneath.
// It is kept topmost (bringToFront in setPanes) so it wins the hit-test on its strip.
class SplitGrip : public Widget
{
public:
	Splitter *sp;
	SplitGrip (Splitter *s) : Widget (0, 0, 1, 1), sp (s) {}

	void onDraw () override
	{
		unsigned c = pressed ? C_FACE_DN : (hover ? sp->gripHi : sp->gripCol);
		canvas.clear (c);
		// a short centred hint line so the grip reads as draggable
		if (sp->vertical) canvas.fillRect (width / 2 - 10, height / 2, 20, 2, C_BORDER);
		else              canvas.fillRect (width / 2, height / 2 - 10, 2, 20, C_BORDER);
	}

	bool onMouse (int mx, int my, int bl, int /*br*/, int /*bm*/, int /*wheel*/) override
	{
		if (mx < 0)					// mouse left us
		{
			if (hover) { hover = false; invalidate (true); }
			if (!pressed) catchOutside = false;
			return false;
		}
		bool wasHover = hover; hover = true;
		if (bl)
		{
			pressed = true; catchOutside = true;	// capture for the whole drag
			// (left+mx) / (top+my) is the cursor in the SPLITTER's coordinate space,
			// because mx/my arrive relative to this grip's own origin.
			int pos = sp->vertical ? (top + my) : (left + mx);
			sp->setSplit (pos - sp->grip / 2);	// centre the grip under the cursor
		}
		else if (pressed)
		{
			pressed = false; catchOutside = false; invalidate (true);
		}
		if (hover != wasHover) invalidate (true);
		return true;					// consume (also blocks panes while captured)
	}
};

// ---- Splitter --------------------------------------------------------------
Splitter::Splitter (int l, int t, int w, int h, bool vertical_, int split_, unsigned bg_)
  : Widget (l, t, w, h), vertical (vertical_), grip (6), minA (32), minB (32),
    gripCol (C_FACE), gripHi (C_FACE_HI), bg (bg_), paneA (0), paneB (0)
{
	split = (split_ >= 0) ? split_ : ((vertical ? h : w) / 2);
	gripW = new SplitGrip (this);
	addChild (gripW);
}

void Splitter::setPanes (Widget *a, Widget *b)
{
	paneA = a; paneB = b;
	addChild (a);
	addChild (b);
	gripW->bringToFront ();			// keep the divider topmost (hit-test priority)
	layout ();
}

void Splitter::setSplit (int s)
{
	split = s;				// layout() clamps it to [minA, total-grip-minB]
	layout ();
	invalidate (true);
}

void Splitter::layout ()
{
	if (paneA == 0 || paneB == 0 || gripW == 0) return;
	int total = vertical ? height : width;
	int s = split;
	int hi = total - grip - minB;
	if (s > hi) s = hi;
	if (s < minA) s = minA;
	if (s < 0) s = 0;
	split = s;

	if (vertical)
	{
		paneA->left = 0; paneA->top = 0;          paneA->resizeTo (width, s);
		gripW->left = 0; gripW->top = s;          gripW->resizeTo (width, grip);
		paneB->left = 0; paneB->top = s + grip;   paneB->resizeTo (width, height - s - grip);
	}
	else
	{
		paneA->left = 0;        paneA->top = 0; paneA->resizeTo (s, height);
		gripW->left = s;        gripW->top = 0; gripW->resizeTo (grip, height);
		paneB->left = s + grip; paneB->top = 0; paneB->resizeTo (width - s - grip, height);
	}
}

void Splitter::onDraw () { canvas.clear (bg); }	// safety backdrop (panes + grip cover it)

} // namespace wtk
