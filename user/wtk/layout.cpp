#include "wtk/layout.h"

namespace wtk {

// Children flow downward; each keeps its own height, spans the panel width.
void VerticalStackPanel::layout ()
{
	int y = pad;
	for (Widget *c = firstChild; c; c = c->nextSib)
	{
		c->left = pad; c->top = y;
		c->resizeTo (width - 2 * pad, c->height);
		y += c->height + gap;
	}
}

// Children flow rightward; each keeps its own width, spans the panel height.
void HorizontalStackPanel::layout ()
{
	int x = pad;
	for (Widget *c = firstChild; c; c = c->nextSib)
	{
		c->left = x; c->top = pad;
		c->resizeTo (c->width, height - 2 * pad);
		x += c->width + gap;
	}
}

// Uniform cells in reading order, honouring per-child colSpan/rowSpan. Cell width from
// the panel width; cell height = height/rows (rows > 0) or the fixed rowH.
void UniformGridLayout::layout ()
{
	int cw = (width - 2 * pad - (cols - 1) * gap) / cols;
	if (cw < 1) cw = 1;
	int ch;
	if (rows > 0)
	{
		ch = (height - 2 * pad - (rows - 1) * gap) / rows;
		if (ch < 1) ch = 1;
	}
	else
	{
		ch = rowH;
	}
	int col = 0, row = 0;
	for (Widget *c = firstChild; c; c = c->nextSib)
	{
		int cs = c->colSpan; if (cs < 1) cs = 1; if (cs > cols) cs = cols;
		int rs = c->rowSpan; if (rs < 1) rs = 1;
		if (col + cs > cols) { col = 0; row++; }	// wrap if it overruns the row
		c->left = pad + col * (cw + gap);
		c->top  = pad + row * (ch + gap);
		c->resizeTo (cs * cw + (cs - 1) * gap, rs * ch + (rs - 1) * gap);
		col += cs;
		if (col >= cols) { col = 0; row++; }
	}
}

} // namespace wtk
