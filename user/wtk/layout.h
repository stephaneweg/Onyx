//
// wtk/layout.h -- automatic layout containers (Panels that re-flow their children on
// resize and on child add/remove, via the Widget::layout() hook). The children stay
// passive: they never recompute their own geometry -- the container places them, so a
// resize cascades down for free (e.g. a calculator filling the secondary section).
//
//   VerticalStackPanel   -- single column, children top->bottom, full width, each keeps
//                           its own HEIGHT (no fixed row height).
//   HorizontalStackPanel -- single row,   children left->right, full height, each keeps
//                           its own WIDTH  (no fixed column width).
//   UniformGridLayout    -- cols columns of uniform cells, filled in reading order
//                           (left->right, top->bottom). Cell WIDTH derives from the
//                           panel width; cell HEIGHT is either a fixed `rowH` (px) or,
//                           when `rows > 0`, the panel height divided into `rows` rows.
//                           A child may span several columns/rows via its colSpan /
//                           rowSpan (default 1) -- e.g. a calculator display spanning 4
//                           of 5 columns. Reflows on resize / child add.
//                           NB: rowSpan only sizes the child taller; it does not reserve
//                           the cells below for following children (simple flow model).
//
// All take a `pad` (outer margin) and `gap` (inter-child spacing), public for tuning.
//
#ifndef _wtk_layout_h
#define _wtk_layout_h

#include "wtk/panel.h"

namespace wtk {

class VerticalStackPanel : public Panel
{
public:
	int pad, gap;
	VerticalStackPanel (int l, int t, int w, int h, unsigned bg = C_BG, int pad_ = 0, int gap_ = 4)
	  : Panel (l, t, w, h, bg), pad (pad_), gap (gap_) {}
	void layout () override;
};

class HorizontalStackPanel : public Panel
{
public:
	int pad, gap;
	HorizontalStackPanel (int l, int t, int w, int h, unsigned bg = C_BG, int pad_ = 0, int gap_ = 4)
	  : Panel (l, t, w, h, bg), pad (pad_), gap (gap_) {}
	void layout () override;
};

class UniformGridLayout : public Panel
{
public:
	int cols, rows, rowH, pad, gap;		// rows > 0 -> cell H = height/rows; else rowH (px)
	UniformGridLayout (int l, int t, int w, int h, int cols_, int rowH_,
			   unsigned bg = C_BG, int pad_ = 4, int gap_ = 4)
	  : Panel (l, t, w, h, bg), cols (cols_ < 1 ? 1 : cols_), rows (0), rowH (rowH_),
	    pad (pad_), gap (gap_) {}
	void setRows (int n) { rows = n; layout (); invalidate (true); }	// divide height into n rows
	void layout () override;
};

} // namespace wtk

#endif
