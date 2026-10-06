//
// notelist.h -- Notes' list of notes (user/Apps/notes/main.cpp): an owner-drawn UIKit widget, since
// uikit::ListBox shows text only. Its head ("Notes" and the count in a badge), then a row of 56 px a note
// (AutoDev round 1, 04-ux-design.md section 2.2): the colour's dot, the title in bold (the first line; "New
// Note" in bold italic while empty), the date at the right (the model's notes_date_label), the next line
// below it, a pin when the note is on the desktop; the selected row a tinted rounded box (Mail's). It
// scrolls with UIKit's own bar. It reads the app's Notes (notesmodel.h) and tells the app what the user
// did through three callbacks: a row picked (a click, the arrows), Enter / Tab (to the editor), Delete.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef NOTES_NOTELIST_H
#define NOTES_NOTELIST_H

#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "Apps/notes/notesmodel.h"
#include <stdio.h>

// A note's colour dot (the list's, the tool bar's colour buttons): d px round at (x, y), the dot's
// gradient and a darker rim.
static inline void notes_draw_dot (uikit::Canvas &cv, int x, int y, int d, int c)
{
	unsigned dot = notes_colour_dot (c);
	uikit::uk_rbox (cv, x, y, d, d, d / 2, uikit::uk_tone (dot, 150), dot);
	uikit::uk_rline (cv, x, y, d, d, d / 2, uikit::uk_tone (dot, 90), 160);
}

class NoteList : public uikit::Widget
{
public:
	enum { HEAD = 44, ROW = 56 };
	const Notes *notes;			// the app's list (newest first)
	int	   sel;				// the selected row, -1 none
	int	   scroll;			// px scrolled
	long long  now;				// YYYYMMDDHHMMSS: the dates are told against it
	void	 (*onPick) (int row);		// a row chosen (a click, the arrows, Home / End...)
	void	 (*onEnter) ();			// Enter / Tab: to the editor
	void	 (*onDelete) ();		// Delete

	NoteList (int l, int t, int w, int h, const Notes *n)
		: Widget (l, t, w, h), notes (n), sel (-1), scroll (0), now (0), onPick (0), onEnter (0), onDelete (0)
	{ canFocus = true; tip = 0; }

	unsigned bgColor () override { return uikit::C_FIELD; }
	int  count () const { return notes ? notes->count : 0; }
	int  contentH () const { return count () * ROW + 8; }
	int  viewH () const { return height - HEAD > 0 ? height - HEAD : 0; }
	bool overflows () const { return contentH () > viewH (); }
	void clampScroll ()
	{
		int most = contentH () - viewH ();
		if (scroll > most) scroll = most;
		if (scroll < 0) scroll = 0;
	}
	// The selected row brought into view.
	void showSel ()
	{
		if (sel < 0) return;
		int y = 4 + sel * ROW;
		if (y < scroll) scroll = y - 4;
		else if (y + ROW > scroll + viewH ()) scroll = y + ROW - viewH ();
		clampScroll ();
	}

	void onDraw () override
	{
		using namespace uikit;
		canvas.clear (C_FIELD);
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 140), faint = uk_mix (C_FIELD, C_FIELD_TEXT, 30);
		int n = count (), right = width - (overflows () ? UK_SBW + 2 : 0);
		clampScroll ();
		// the rows (under the head: drawn first, the head over them)
		for (int r = 0; r < n; r++)
		{
			int y = HEAD + 4 + r * ROW - scroll;
			if (y + ROW <= HEAD) continue;
			if (y >= height) break;
			const NoteInfo &e = notes->n[r];
			bool isSel = r == sel;
			if (isSel)
			{
				unsigned t = uk_mix (C_FIELD, C_ACCENT, hasFocus ? 90 : 60);
				uk_rbox (canvas, 6, y, right - 12, ROW - 4, 7, t, t);
			}
			else if (r + 1 < n && r + 1 != sel) canvas.fillRect (34, y + ROW - 3, right - 46, 1, faint);
			notes_draw_dot (canvas, 16, y + 11, 10, e.colour);
			char date[24] = "", f[NOTE_TITLE + 8];
			if (e.saved) notes_date_label (e.modified, now, date, sizeof date);
			int dw = date[0] ? uk_tw (date) : 0;
			if (e.title[0])
			{
				uk_text_fit (e.title, right - 34 - dw - 22, f, sizeof f, 2);
				uk_text (canvas, 34, y + 5, f, C_FIELD_TEXT, 2);
			}
			else uk_text (canvas, 34, y + 5, "New Note", uk_mix (C_FIELD, C_FIELD_TEXT, 170), 3);
			if (dw) uk_text (canvas, right - dw - 14, y + 5, date, isSel ? uk_mix (C_FIELD, C_FIELD_TEXT, 190) : dim);
			const char *pv = e.preview[0] ? e.preview : !e.saved && !e.title[0] ? "Now" : "";
			if (*pv)
			{
				uk_text_fit (pv, right - 34 - 14 - (e.pinned ? 22 : 0), f, sizeof f);
				uk_text (canvas, 34, y + 26, f, dim);
			}
			if (e.pinned) uk_tool_glyph (canvas, WKT_PIN, right - 30, y + 26, 15, isSel ? C_ACCENT : uk_mix (C_FIELD, C_FIELD_TEXT, 160));
		}
		// the head: what is listed, and how many
		canvas.fillRect (0, 0, width, HEAD, C_FIELD);
		uk_text (canvas, 14, (HEAD - uk_fh ()) / 2, "Notes", C_FIELD_TEXT, 2);
		char c[16]; snprintf (c, sizeof c, "%d", n);
		int cw = uk_tw (c);
		unsigned badge = uk_mix (C_FIELD, C_FIELD_TEXT, 36);
		uk_rbox (canvas, width - cw - 30, 13, cw + 16, 19, 9, badge, badge);
		uk_text (canvas, width - cw - 22, 13 + (19 - uk_fh ()) / 2, c, C_FIELD_TEXT);
		canvas.fillRect (0, HEAD - 1, width, 1, faint);
		if (overflows ())
			uk_draw_vscroll (canvas, width - UK_SBW, HEAD, UK_SBW, viewH (), uk_thumb (contentH (), viewH (), scroll, viewH ()), C_FIELD, m_bar.held);
	}

	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		long pos = scroll;
		if (m_bar.mouse (mx, my, bl, width - uikit::UK_SBW, uikit::UK_SBW, HEAD, viewH (), contentH (), viewH (), &pos))
		{
			if (pos != scroll) { scroll = (int) pos; invalidate (true); }
			return true;
		}
		if (mx < 0) { m_down = false; return false; }
		if (wheel)
		{
			scroll -= (signed char) wheel * ROW;
			clampScroll (); invalidate (true);
			return true;
		}
		bool press = bl && !m_down;
		m_down = bl != 0;
		if (!press) return true;
		if (!hasFocus) { setFocus (); invalidate (true); }
		if (my < HEAD) return true;
		int r = (my - HEAD - 4 + scroll) / ROW;
		if (r >= 0 && r < count () && onPick) onPick (r);
		return true;
	}

	bool onKey (long k) override
	{
		int n = count (), page = viewH () / ROW > 1 ? viewH () / ROW - 1 : 1, to = sel;
		switch (k)
		{
		case KEY_UP:   to = sel - 1; break;
		case KEY_DOWN: to = sel + 1; break;
		case KEY_PGUP: to = sel - page; break;
		case KEY_PGDN: to = sel + page; break;
		case KEY_HOME: to = 0; break;
		case KEY_END:  to = n - 1; break;
		case KEY_ENTER: case KEY_TAB: if (onEnter) onEnter (); return true;
		case KEY_DEL:  if (onDelete) onDelete (); return true;
		default: return false;
		}
		if (to < 0) to = 0;
		if (to > n - 1) to = n - 1;
		if (to != sel && to >= 0 && onPick) onPick (to);
		return true;
	}

private:
	uikit::UkBarDrag m_bar;
	bool m_down = false;
};

#endif
