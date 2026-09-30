//
// ui/grid.h -- NoteGrid: the editors' grid of rows x slices, drawn by hand in a canvas the size of
// the view -- the chord articulation's voice rows (bass, 1, 3, 5, 7, 9...), the drum lanes, a
// melodic line's rhythm rows, a melodic cell's degrees, and (rows = pitches, bottom up, with a
// keyboard and the harmony's shading) the piano roll. The notes are edited in place (a
// Vec<RiffNote>, Note = the row): draw (click, drag the length), move (drag a note), resize (drag
// its right end), erase (right-click, or the Erase tool), one-shot rows (drums: a click toggles a
// hit). Scroll: the wheel (rows), Shift+wheel (time); Ctrl+wheel: zoom.
//
#ifndef _koton_grid_h
#define _koton_grid_h

#include "ui/palette.h"
#include "engine/model.h"

namespace kui {

enum { TOOL_DRAW = 0, TOOL_SELECT = 1, TOOL_ERASE = 2 };

class NoteGrid : public Widget
{
public:
	Vec<RiffNote> *notes;		// what is edited (not owned)
	int rows, rowH, labelW, headerH;
	int cols;			// the length, in slices
	int spb, beatsPerBar;		// slices per beat, beats per bar (the lines)
	int pxPerCol;			// zoom
	int drawLen;			// the length of a drawn note (slices)
	int snapCols;			// starts, moves and lengths in steps of this many slices
	bool oneShot;			// a hit per click (drums): drawn short, toggled
	bool bottomUp;			// row 0 at the bottom (the piano roll)
	bool keyboard;			// the rows are pitches: a keyboard as labels
	bool readOnly;			// shown, not edited
	bool pads;			// Koton Studio's look (the piano roll): a rounded pad per cell, tinted by its
					// pitch class, the beat's first slice lighter; the notes teal; every row named
	int tool;
	int playCol;			// the playhead (-1: none)
	int colOffsetBeats;		// (for the callbacks: the grid's column 0 is this absolute beat * spb)
	unsigned noteColour;
	void *ctx;
	const char *(*rowLabel) (NoteGrid &g, int row);
	unsigned (*rowShade) (NoteGrid &g, int row, int col);	// a cell's background (0: the default)
	unsigned (*rowColour) (NoteGrid &g, int row);		// a row's notes' colour (0: noteColour)
	void (*drawHeader) (NoteGrid &g, Canvas &cv, int x0, int w);	// above the grid (headerH px)
	void (*onChange) (NoteGrid &g);
	void (*onBegin) (NoteGrid &g);		// before an edit (the app's undo checkpoint)
	void (*onAudition) (NoteGrid &g, int row, bool on);	// a note pressed / released (to hear it)
	Vec<int> selected;		// selected note indices

	NoteGrid (int l, int t, int w, int h) : Widget (l, t, w, h), notes (0), rows (8), rowH (22), labelW (70), headerH (0), cols (64), spb (4),
		beatsPerBar (4), pxPerCol (18), drawLen (1), snapCols (1), oneShot (false), bottomUp (false), keyboard (false), readOnly (false), pads (false), tool (TOOL_DRAW), playCol (-1),
		colOffsetBeats (0), noteColour (GREEN), ctx (0), rowLabel (0), rowShade (0), rowColour (0), drawHeader (0), onChange (0), onBegin (0), onAudition (0),
		m_sx (0), m_sy (0), m_drag (0), m_note (-1), m_grabCol (0), m_grabRow (0), m_orig (), m_auditionRow (-1)
	{ canFocus = true; anchor = ANCHOR_FILL; }

	void setScrollRow (int firstRow) { m_sy = imax (0, imin (firstRow * rowH, imax (0, rows * rowH - gridH ()))); invalidate (true); }
	void centreRow (int row)
	{
		int y = rowToY0 (row);				// in content px
		m_sy = imax (0, imin (y - gridH () / 2, imax (0, rows * rowH - gridH ())));
		invalidate (true);
	}
	int gridH () const { return height - headerH - 10; }
	int gridW () const { return width - labelW - 10; }
	int colX (int c) const { return labelW + c * pxPerCol - m_sx; }
	int colAt (int x) const { return (x - labelW + m_sx) / pxPerCol; }
	int rowToY0 (int r) const { return (bottomUp ? rows - 1 - r : r) * rowH; }		// content px
	int rowY (int r) const { return headerH + rowToY0 (r) - m_sy; }
	int rowAt (int y) const { int k = (y - headerH + m_sy) / rowH; return bottomUp ? rows - 1 - k : k; }
	int scrollXpx () const { return m_sx; }
	void fitWidth ()			// the zoom so the whole length shows
	{
		int w = gridW ();
		pxPerCol = imax (2, imin (40, w / imax (1, cols)));
		m_sx = 0;
		invalidate (true);
	}

	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (pads ? 0x1C1C22 : LANE);
		int gw = gridW (), gh = gridH ();
		int beat = imax (1, spb), bar = beat * imax (1, beatsPerBar);
		int firstRow = m_sy / rowH, lastRow = imin (rows - 1, (m_sy + gh) / rowH + 1);
		// the cells
		for (int k = firstRow; k <= lastRow; k++)
		{
			int r = bottomUp ? rows - 1 - k : k;
			int y = headerH + k * rowH - m_sy;
			int c0 = imax (0, colAt (labelW)), c1 = imin (cols, colAt (labelW + gw) + 1);
			for (int c = c0; c < c1; c++)
			{
				int x = colX (c);
				unsigned bg = (c / beat) & 1 ? 0x262B34 : 0x2A303A;
				if (pads) bg = padColour (r, c);
				if (rowShade) { unsigned s = rowShade (*this, r, c); if (s) bg = s; }
				if (pads) { box (cv, x + 1, y + 1, pxPerCol - 2, rowH - 2, pxPerCol >= 10 ? 3 : 0, bg); continue; }
				if (pxPerCol >= 6) cv.fillRect (x + 1, y + 1, pxPerCol - 1, rowH - 1, bg);
				else cv.fillRect (x, y + 1, pxPerCol, rowH - 1, bg);		// (narrow columns: no gaps)
			}
		}
		// the lines: beats, bars
		for (int c = imax (0, colAt (labelW)); c <= imin (cols, colAt (labelW + gw) + 1); c++)
		{
			int x = colX (c);
			if (x < labelW || x > labelW + gw || pads) continue;
			if (c % bar == 0) vline (cv, x, headerH, headerH + gh, GRID_BAR);
			else if (c % beat == 0) vline (cv, x, headerH, headerH + gh, GRID_BEAT);
		}
		// the end of the pattern
		int ex = colX (cols);
		if (ex < labelW + gw) cv.fillRect (ex + 1, headerH, labelW + gw - ex, gh, BG);
		// the notes
		if (notes)
			for (int i = 0; i < notes->size (); i++)
			{
				const RiffNote &n = (*notes)[i];
				if (n.note < 0 || n.note >= rows) continue;
				int y = rowY (n.note);
				if (y + rowH < headerH || y > headerH + gh) continue;
				int x = colX (n.start), w = oneShot ? pxPerCol : n.length * pxPerCol;
				if (x + w < labelW || x > labelW + gw) continue;
				bool sel = selected.contains (i);
				unsigned rc = rowColour ? rowColour (*this, n.note) : 0;
				unsigned c = sel ? NOTE_SEL : rc ? rc : pads ? PAD_ON : noteColour;
				int x0 = imax (x + 1, labelW), x1 = imin (x + w - 1, labelW + gw);
				if (pads) { box (cv, x0, y + 1, x1 - x0, rowH - 2, 3, c); continue; }
				box (cv, x0, y + 2, x1 - x0, rowH - 3, 3, c);
				if (x0 == x + 1) box (cv, x0, y + 2, imin (3, x1 - x0), rowH - 3, 1, lighter (c, 130));
			}
		// the header (above)
		if (headerH > 0)
		{
			cv.fillRect (0, 0, width, headerH, PANEL2);
			if (drawHeader) drawHeader (*this, cv, labelW, gw);
			else
				for (int c = 0; c < cols; c += beat)
				{
					int x = colX (c);
					if (x < labelW || x > labelW + gw) continue;
					char b[16]; snprintf (b, sizeof b, "%d.%d", c / bar + 1, (c % bar) / beat + 1);
					textL (cv, x + 3, 0, headerH, b, c % bar ? FAINT : DIM);
				}
		}
		// the labels (left)
		cv.fillRect (0, headerH, labelW, gh, PANEL2);
		for (int k = firstRow; k <= lastRow; k++)
		{
			int r = bottomUp ? rows - 1 - k : k;
			int y = headerH + k * rowH - m_sy;
			if (y + rowH <= headerH || y >= headerH + gh) continue;
			if (keyboard && pads)
			{
				int pc = (r + 12) % 12;
				bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
				static const char *const nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
				char b[8]; snprintf (b, sizeof b, "%s%d", nm[pc], (r + 12) / 12 - 1);
				textL (cv, 6, y, rowH, b, pc == 0 ? 0xF2F2FF : black ? 0x808088 : 0xBBBBBB, pc == 0 ? 2 : 0);
			}
			else if (keyboard)
			{
				int pc = (r + 12) % 12;
				bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
				cv.fillRect (0, y, labelW, rowH, black ? 0x1A1C20 : 0xCED2D8);
				hline (cv, 0, labelW, y, 0x7C8086);
				if (rowLabel) { const char *s = rowLabel (*this, r); if (s && s[0]) textR (cv, labelW - 4, y, rowH, s, black ? 0xC8C8CD : 0x28282C); }
			}
			else if (pads) { if (rowLabel) textL (cv, 8, y, rowH, rowLabel (*this, r), 0xBBBBBB); }
			else
			{
				hline (cv, 0, labelW, y + rowH - 1, LINE);
				if (rowLabel) textL (cv, 8, y, rowH, rowLabel (*this, r), TEXT);
			}
		}
		// the playhead
		if (playCol >= 0)
		{
			int x = colX (playCol);
			if (x >= labelW && x <= labelW + gw) vline (cv, x, 0, headerH + gh, PLAY);
		}
		// the scroll bars
		int total = rows * rowH;
		WkThumb tv = wk_thumb (total, gh, m_sy, gh);
		if (tv.show) wk_scroll_bar (cv, width - 10, headerH, 10, gh, true, tv.y, tv.h, LANE, WK_NORMAL);
		WkThumb thh = wk_thumb (cols * pxPerCol + 20, gw, m_sx, gw);
		cv.fillRect (0, height - 10, width, 10, PANEL);
		if (thh.show) wk_scroll_bar (cv, labelW, height - 10, gw, 10, false, thh.y, thh.h, PANEL, WK_NORMAL);
		frame (cv, 0, 0, width, height, 0, LINE);
	}

	// a pad's colour (Koton Studio's): C rows lighter, the black keys' darker; the beat's first slice lighter
	// (rows with their own colour -- the drum lanes' families: that colour, dark, behind)
	unsigned padColour (int row, int col)
	{
		if (!keyboard && rowColour) { unsigned c = rowColour (*this, row); if (c) return mixc (0x232429, c, col % imax (1, spb) == 0 ? 56 : 28); }
		int pc = keyboard ? (row + 12) % 12 : 2;
		bool down = col % imax (1, spb) == 0;
		if (pc == 0) return down ? 0x41414F : 0x343440;
		if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10) return down ? 0x2E2E38 : 0x22222A;
		return down ? 0x393946 : 0x2C2C36;
	}

	int noteAt (int col, int row) const
	{
		if (!notes) return -1;
		for (int i = notes->size () - 1; i >= 0; i--)
		{
			const RiffNote &n = (*notes)[i];
			int len = oneShot ? 1 : n.length;
			if (n.note == row && col >= n.start && col < n.start + len) return i;
		}
		return -1;
	}

	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) bm;
		bool rightDown = false;
		int e = m_btn.edge (bl, br, &rightDown);
		if (wheel)
		{
			unsigned mods = kapi_get_modifiers ();
			if (mods & MOD_CTRL) { int c = colAt (mx); pxPerCol = iclamp (pxPerCol + (wheel > 0 ? 2 : -2), 3, 48); m_sx = imax (0, labelW + c * pxPerCol - mx); }
			else if (mods & MOD_SHIFT) m_sx = imax (0, imin (m_sx - wheel * pxPerCol * 4, imax (0, cols * pxPerCol + 20 - gridW ())));
			else m_sy = imax (0, imin (m_sy - wheel * rowH * 2, imax (0, rows * rowH - gridH ())));
			invalidate (true);
			return true;
		}
		if (e == 1) setFocus ();
		// the scroll bars
		if ((e == 1 || m_drag == 5 || m_drag == 6) && bl)
		{
			if (m_drag == 5 || (e == 1 && mx >= width - 10 && my >= headerH))
			{
				m_drag = 5;
				int gh = gridH (), total = rows * rowH;
				WkThumb tv = wk_thumb (total, gh, m_sy, gh);
				m_sy = (int) wk_thumb_pos (my - headerH, gh, total, gh, tv.h);
				invalidate (true); return true;
			}
			if (m_drag == 6 || (e == 1 && my >= height - 10))
			{
				m_drag = 6;
				int gw = gridW ();
				WkThumb th_ = wk_thumb (cols * pxPerCol + 20, gw, m_sx, gw);
				m_sx = (int) wk_thumb_pos (mx - labelW, gw, cols * pxPerCol + 20, gw, th_.h);
				invalidate (true); return true;
			}
		}
		if (my < headerH || mx < labelW)
		{
			if (e == 1 && mx < labelW && my >= headerH && onAudition) { m_auditionRow = rowAt (my); onAudition (*this, m_auditionRow, true); }
			if (e == -1 && m_auditionRow >= 0 && onAudition) { onAudition (*this, m_auditionRow, false); m_auditionRow = -1; }
			return true;
		}
		int col = colAt (mx), row = rowAt (my);
		if (!notes || readOnly)
		{
			if (e == 1 && onAudition && row >= 0 && row < rows) { m_auditionRow = row; onAudition (*this, row, true); }
			if (e == -1 && m_auditionRow >= 0 && onAudition) { onAudition (*this, m_auditionRow, false); m_auditionRow = -1; }
			return true;
		}
		if (rightDown || (e == 1 && tool == TOOL_ERASE))
		{
			int i = noteAt (col, row);
			if (i >= 0) { begin (); erase (i); changed (); }
			m_drag = tool == TOOL_ERASE ? 4 : 0;
			return true;
		}
		if (e == 1)
		{
			if (row < 0 || row >= rows || col < 0 || col >= cols) return true;
			int i = noteAt (col, row);
			if (oneShot)
			{
				begin ();
				if (i >= 0) { erase (i); }
				else { notes->push (RiffNote (row, col, 1)); if (onAudition) onAudition (*this, row, true); }
				changed ();
				m_drag = 0;
				return true;
			}
			if (i >= 0)
			{
				begin ();
				RiffNote &n = (*notes)[i];
				selected.clear (); selected.push (i);
				m_note = i; m_orig = n; m_grabCol = col; m_grabRow = row;
				m_drag = (col >= n.start + n.length - 1 && n.length > 1 && mx >= colX (n.start + n.length) - pxPerCol / 2) ? 2 : (tool == TOOL_SELECT ? 3 : 3);
				if (onAudition) { onAudition (*this, n.note, true); m_auditionRow = n.note; }
				invalidate (true);
				return true;
			}
			if (tool == TOOL_SELECT) { selected.clear (); invalidate (true); return true; }
			begin ();
			int sc = imax (1, snapCols);
			int start = col - col % sc;
			notes->push (RiffNote (row, start, imax (1, imin (drawLen, cols - start))));
			m_note = notes->size () - 1; m_orig = (*notes)[m_note]; m_grabCol = start; m_grabRow = row;
			selected.clear (); selected.push (m_note);
			m_drag = 1;
			if (onAudition) { onAudition (*this, row, true); m_auditionRow = row; }
			invalidate (true);
			return true;
		}
		if (e == -1)
		{
			if (m_auditionRow >= 0 && onAudition) onAudition (*this, m_auditionRow, false);
			m_auditionRow = -1;
			if (m_drag >= 1 && m_drag <= 3) changed ();
			m_drag = 0; m_note = -1;
			return true;
		}
		if (bl && m_drag == 4) { int i = noteAt (col, row); if (i >= 0) { begin (); erase (i); changed (); } return true; }
		if (bl && m_note >= 0 && m_note < notes->size ())
		{
			RiffNote &n = (*notes)[m_note];
			int sc = imax (1, snapCols);
			if (m_drag == 1 || m_drag == 2)			// the length
			{
				int len = col - n.start + 1;
				len = ((len + sc - 1) / sc) * sc;
				n.length = iclamp (len, 1, cols - n.start);
			}
			else if (m_drag == 3)				// the note moved
			{
				int d = col - m_grabCol; d = d >= 0 ? (d / sc) * sc : -(((-d) / sc) * sc);
				int ns = iclamp (m_orig.start + d, 0, imax (0, cols - n.length));
				int nr = iclamp (m_orig.note + (row - m_grabRow), 0, rows - 1);
				if (nr != n.note && onAudition) { onAudition (*this, n.note, false); onAudition (*this, nr, true); m_auditionRow = nr; }
				n.start = ns; n.note = nr;
			}
			invalidate (true);
		}
		return true;
	}

	bool onKey (long k) override
	{
		if (!notes || readOnly) return false;
		if ((k == KEY_DEL || k == KEY_BACKSPACE) && selected.size ())
		{
			begin ();
			selected.sort ([] (int a, int b) { return a > b; });
			for (int i = 0; i < selected.size (); i++) if (selected[i] < notes->size ()) notes->removeAt (selected[i]);
			selected.clear ();
			changed ();
			return true;
		}
		if ((k == KEY_UP || k == KEY_DOWN) && selected.size ())
		{
			begin ();
			int d = (k == KEY_UP) == bottomUp ? 1 : -1;
			if (!bottomUp) d = k == KEY_UP ? -1 : 1;
			for (int i = 0; i < selected.size (); i++) { RiffNote &n = (*notes)[selected[i]]; n.note = iclamp (n.note + d, 0, rows - 1); }
			changed ();
			return true;
		}
		if ((k == KEY_LEFT || k == KEY_RIGHT) && selected.size ())
		{
			begin ();
			for (int i = 0; i < selected.size (); i++) { RiffNote &n = (*notes)[selected[i]]; n.start = iclamp (n.start + (k == KEY_LEFT ? -imax (1, snapCols) : imax (1, snapCols)), 0, cols - 1); }
			changed ();
			return true;
		}
		return false;
	}

private:
	Buttons m_btn;
	int m_sx, m_sy, m_drag, m_note, m_grabCol, m_grabRow;
	RiffNote m_orig;
	int m_auditionRow;
	void erase (int i)
	{
		notes->removeAt (i);
		selected.clear ();
	}
	void changed () { invalidate (true); if (onChange) onChange (*this); }
	void begin () { if (onBegin) onBegin (*this); }
};

} // namespace kui

#endif
