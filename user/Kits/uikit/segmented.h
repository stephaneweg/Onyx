//
// uikit/segmented.h -- SegmentedControl: a row of mutually exclusive labelled segments drawn as one
// pill (a radio group, a small tab strip): the chosen one filled with the accent. Click a segment,
// or Left / Right when focused; onChange fires when `selected` changes. The labels are copied.
//
//   static const char *const MODES[] = { "Articulation", "Melodic cell", "Voicing" };
//   SegmentedControl *sc = new SegmentedControl (x, y, 300, 26, MODES, 3, 0, onMode);
//
#ifndef _uikit_segmented_h
#define _uikit_segmented_h

#include "uikit/widget.h"

namespace uikit {

class SegmentedControl : public Widget
{
public:
	static const int MAXSEG = 12;
	int	 selected;			// the chosen segment (-1: none)
	bool	 equalWidths;			// every segment as wide (default); false: each its text's width,
						// the room left shared out
	Action	 onChange;

	SegmentedControl (int l, int t, int w, int h, const char *const *labels, int n, int sel = 0, Action cb = 0);
	void setLabels (const char *const *labels, int n);
	void select (int i, bool fire = false);	// repainted; onChange if fire and it changed
	int  count () const { return m_n; }
	const char *label (int i) const { return i >= 0 && i < m_n ? m_label[i] : ""; }
	void setEnabled (int i, bool on);	// a segment greyed out (not chosen by a click)
	int  segmentAt (int mx) const;		// the segment under x (-1: none)

	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	char	 m_label[MAXSEG][32];
	bool	 m_off[MAXSEG];
	int	 m_n, m_hot, m_x[MAXSEG + 1];	// the segments' edges (m_x[i] .. m_x[i + 1])
	void	 place ();
};

} // namespace uikit

#endif
