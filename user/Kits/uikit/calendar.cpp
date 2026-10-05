//
// uikit/calendar.cpp -- Calendar (month grid) and DatePicker (date field + drop-down
// calendar).
//
#include "uikit/calendar.h"
#include "uikit/lang.h"

namespace uikit {

static const char *const MONTHS[12] = { "January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December" };
static const char *const DOWS[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };

int Calendar::daysIn (int y, int m)
{
	static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
	return d[(m - 1) % 12];
}
int Calendar::dayOfWeek (int y, int m, int d)		// Zeller -> 0 = Monday
{
	if (m < 3) { m += 12; y--; }
	int k = y % 100, j = y / 100;
	int h = (d + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;	// 0 = Saturday
	return (h + 5) % 7;
}

Calendar::Calendar (int l, int t, int y, int m, int d, Action cb_)
  : Widget (l, t, CAL_W, CAL_H), year (y), month (m), day (d), viewYear (y), viewMonth (m), cb (cb_)
{ canFocus = true; }

void Calendar::setDate (int y, int m, int d)
{ year = viewYear = y; month = viewMonth = m; day = d; invalidate (true); }

// A card: a raised header (the month, chevrons), the week days, the days -- the chosen one in
// the accent, today ringed, the weekend in a muted red.
void Calendar::onDraw ()
{
	int fh = uk_fh ();
	const unsigned WEEKEND = 0x00B0504A;
	if (transparent)					// (floating: a DatePicker's drop-down)
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		uk_popup (canvas, 0, 0, width, height, 6, C_FIELD);
		if (hasFocus) uk_rline (canvas, 0, 0, width, height, 6, C_ACCENT, 200);
	}
	else
	{
		canvas.clear (bgColor ());
		uk_rbox (canvas, 0, 0, width, height, 6, C_FIELD, C_FIELD);
		uk_rline (canvas, 0, 0, width, height, 6, hasFocus ? C_ACCENT : uk_tone (C_FACE, 72), hasFocus ? 255 : 210);
	}
	uk_rbox (canvas, 1, 1, width - 2, CAL_HDR - 1, 5, uk_tone (C_FACE, 166), uk_tone (C_FACE, 128), 255, UK_TL | UK_TR);
	for (int i = 1; i < width - 1; i++) canvas.pixel (i, CAL_HDR, uk_tone (C_FACE, 100));
	uk_glyph (canvas, WKG_CHEV_LEFT, 13, CAL_HDR / 2, 9, C_TEXT);
	uk_glyph (canvas, WKG_CHEV_RIGHT, width - 14, CAL_HDR / 2, 9, C_TEXT);
	char t[48]; int p = 0;
	const char *mn = TR (MONTHS[viewMonth - 1]);
	for (int i = 0; mn[i] && p < 40; i++) t[p++] = mn[i];
	t[p++] = ' ';
	int y = viewYear; t[p++] = (char) ('0' + y / 1000 % 10); t[p++] = (char) ('0' + y / 100 % 10);
	t[p++] = (char) ('0' + y / 10 % 10); t[p++] = (char) ('0' + y % 10); t[p] = '\0';
	uk_text_c (canvas, 0, 0, width, CAL_HDR, t, C_TEXT, 2);
	unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 140);
	for (int i = 0; i < 7; i++)
		canvas.text (1 + i * CAL_CELL_W + (CAL_CELL_W - uk_tw (TR (DOWS[i]))) / 2, CAL_HDR + 2, TR (DOWS[i]),
			     i >= 5 ? uk_mix (C_FIELD, WEEKEND, 190) : dim);
	int ty = 0, tm = 0, td = 0;
	kapi_get_datetime (&ty, &tm, &td, 0, 0, 0);
	int first = dayOfWeek (viewYear, viewMonth, 1), nd = daysIn (viewYear, viewMonth);
	for (int d = 1; d <= nd; d++)
	{
		int cell = first + d - 1, cx = 1 + (cell % 7) * CAL_CELL_W, cy = CAL_HDR + 18 + (cell / 7) * CAL_CELL_H;
		bool isSel = viewYear == year && viewMonth == month && d == day;
		bool isToday = viewYear == ty && viewMonth == tm && d == td;
		if (isSel) uk_hilite (canvas, cx + 2, cy, CAL_CELL_W - 4, CAL_CELL_H - 1, 5, true);
		if (isToday) uk_rline (canvas, cx + 2, cy, CAL_CELL_W - 4, CAL_CELL_H - 1, 5, isSel ? uk_tone (C_ACCENT, 50) : C_ACCENT, 255);
		char b[3] = { (char) (d >= 10 ? '0' + d / 10 : ' '), (char) ('0' + d % 10), 0 };
		unsigned ink = isSel ? C_SEL_TEXT : (cell % 7) >= 5 ? WEEKEND : C_FIELD_TEXT;
		const char *bs = uk_textface () && b[0] == ' ' ? b + 1 : b;	// (a face: the digits alone, centred)
		canvas.text (cx + (CAL_CELL_W - uk_tw (bs)) / 2, cy + (CAL_CELL_H - fh) / 2, bs, ink);
	}
}

static void shift_month (int &y, int &m, int by)
{
	m += by;
	while (m < 1) { m += 12; y--; }
	while (m > 12) { m -= 12; y++; }
}

bool Calendar::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; return false; }
	if (wheel) { shift_month (viewYear, viewMonth, wheel > 0 ? -1 : 1); invalidate (true); return true; }
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (my < CAL_HDR)
		{
			if (mx < 24) shift_month (viewYear, viewMonth, -1);
			else if (mx >= width - 24) shift_month (viewYear, viewMonth, 1);
			invalidate (true);
			return true;
		}
		int gy = my - CAL_HDR - 18;
		if (gy < 0) return true;
		int cell = (gy / CAL_CELL_H) * 7 + (mx - 1) / CAL_CELL_W;
		int d = cell - dayOfWeek (viewYear, viewMonth, 1) + 1;
		if (d >= 1 && d <= daysIn (viewYear, viewMonth))
		{
			year = viewYear; month = viewMonth; day = d;
			invalidate (true);
			if (cb) cb (*this);
		}
	}
	else if (!bl) pressed = false;
	return true;
}

bool Calendar::onKey (long k)
{
	int step = 0;
	switch (k)
	{
	case KEY_PGUP: shift_month (viewYear, viewMonth, -1); invalidate (true); return true;
	case KEY_PGDN: shift_month (viewYear, viewMonth, 1);  invalidate (true); return true;
	case KEY_LEFT: step = -1; break;
	case KEY_RIGHT: step = 1; break;
	case KEY_UP: step = -7; break;
	case KEY_DOWN: step = 7; break;
	case KEY_ENTER: if (cb) cb (*this); return true;
	default: return false;
	}
	day += step;					// move the selection, across months
	while (day < 1) { shift_month (year, month, -1); day += daysIn (year, month); }
	while (day > daysIn (year, month)) { day -= daysIn (year, month); shift_month (year, month, 1); }
	viewYear = year; viewMonth = month;
	invalidate (true);
	return true;
}

// ---- DatePicker ----------------------------------------------------------------------------
static void dp_pick (Widget &w)
{
	DatePicker *dp = (DatePicker *) w.parent;
	Calendar &c = (Calendar &) w;
	bool changed = c.year != dp->year || c.month != dp->month || c.day != dp->day;
	dp->year = c.year; dp->month = c.month; dp->day = c.day;
	dp->setOpen (false);
	if (changed && dp->cb) dp->cb (*dp);
}

DatePicker::DatePicker (int l, int t, int w, int h, int y, int m, int d, Action cb_)
  : Widget (l, t, w, h), year (y), month (m), day (d), cb (cb_), open (false), cal (0), rowH (h), origW (w)
{ canFocus = true; transparent = true; }	// magenta around the drop-down calendar = see-through

DatePicker::~DatePicker ()
{ if (cal && !cal->parent) delete cal; }	// (closed: the calendar is not a child, ours to delete)

void DatePicker::format (char *o) const
{
	o[0] = (char) ('0' + year / 1000 % 10); o[1] = (char) ('0' + year / 100 % 10);
	o[2] = (char) ('0' + year / 10 % 10); o[3] = (char) ('0' + year % 10); o[4] = '-';
	o[5] = (char) ('0' + month / 10); o[6] = (char) ('0' + month % 10); o[7] = '-';
	o[8] = (char) ('0' + day / 10); o[9] = (char) ('0' + day % 10); o[10] = '\0';
}

void DatePicker::setOpen (bool o)
{
	if (o == open) return;
	open = o;
	catchOutside = o;					// while open, grab clicks anywhere (to close)
	int w = width < CAL_W ? CAL_W : width;
	if (o)
	{
		resizeTo (w, rowH + 2 + CAL_H);
		if (!cal)
		{
			cal = new Calendar (0, rowH + 2, year, month, day, dp_pick);
			cal->transparent = true;		// (its rounded corners: see-through)
		}
		cal->setDate (year, month, day);
		cal->pressed = false;
		addChild (cal);
		cal->setFocus ();
		bringToFront ();
	}
	else
	{
		// (the calendar is kept, not deleted: a day picked closes it from inside its own
		// handleMouse, which still runs -- and ours remembers it as the last one handled)
		if (cal) removeChild (cal);
		resizeTo (origW, rowH);
		setFocus ();
	}
	invalidate (true);
	if (parent) parent->invalidate (true);
}

void DatePicker::onDraw ()
{
	int fh = uk_fh ();
	canvas.clear (UK_TRANSPARENT_KEY);
	canvas.fillRect (0, 0, width, rowH, bgColor ());
	uk_sunken (canvas, 0, 0, width, rowH, 4, disabled ? uk_tone (C_FACE, 150) : C_FIELD, (hasFocus || open) && !disabled);
	char t[12]; format (t);
	canvas.text (7, (rowH - fh) / 2, t, disabled ? C_DIS : C_FIELD_TEXT);
	int bw = 20, bx = width - bw - 2, bh = rowH - 6;			// the button: a small calendar
	uk_raised (canvas, bx, 3, bw - 1, bh, 3, C_BUTTON, disabled ? UK_DISABLED : open ? UK_PRESSED : UK_NORMAL);
	int gx = bx + (bw - 1 - 11) / 2, gy = 3 + (bh - 10) / 2;
	uk_rbox (canvas, gx, gy, 11, 10, 2, 0x00FFFFFF, uk_tone (C_FIELD, 120));
	uk_rbox (canvas, gx, gy, 11, 3, 1, 0x00D05048, 0x00B8403A, 255, UK_TL | UK_TR);
	uk_rline (canvas, gx, gy, 11, 10, 2, uk_tone (C_FACE, 70), 200);
	for (int i = 0; i < 3; i++) canvas.fillRect (gx + 2 + i * 3, gy + 5, 2, 2, C_TEXT);
}

bool DatePicker::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0) { pressed = false; return false; }
	if (disabled) return true;
	if (bl && !pressed)
	{
		pressed = true;
		bool inField = mx >= 0 && mx < width && my >= 0 && my < rowH;
		if (open && !inField) setOpen (false);		// a click outside the calendar: cancel
		else if (inField) setOpen (!open);
	}
	else if (!bl) pressed = false;
	return true;
}

bool DatePicker::onKey (long k)
{
	if (k == 27 && open) { setOpen (false); return true; }
	if ((k == KEY_ENTER || k == ' ' || k == KEY_DOWN) && !open) { setOpen (true); return true; }
	return false;
}

} // namespace uikit
