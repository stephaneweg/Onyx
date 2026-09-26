//
// wtk/calendar.h -- Calendar: a month grid to pick a date (WPF Calendar); DatePicker: a
// date field that opens a Calendar below it (WPF DatePicker).
//   Calendar: < / > (or Page Up / Page Down) change the month; click a day (or arrows +
//   Enter) picks it: year/month/day + cb. Today is outlined.
//   DatePicker: shows YYYY-MM-DD; click to open the calendar, pick a day to close it
//   (Esc or a click outside cancels); cb fires on a new date.
//
#ifndef _wtk_calendar_h
#define _wtk_calendar_h

#include "wtk/widget.h"

namespace wtk {

class Calendar : public Widget
{
public:
	int year, month, day;			// the selected date (month 1..12)
	int viewYear, viewMonth;		// the month shown
	Action cb;
	Calendar (int l, int t, int y, int m, int d, Action cb_);	// size: CAL_W x CAL_H
	void setDate (int y, int m, int d);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
	static int daysIn (int y, int m);
	static int dayOfWeek (int y, int m, int d);	// 0 = Monday
};
enum { CAL_CELL_W = 28, CAL_CELL_H = 20, CAL_HDR = 24, CAL_W = 7 * CAL_CELL_W + 2,
       CAL_H = CAL_HDR + 18 + 6 * CAL_CELL_H + 4 };

class DatePicker : public Widget
{
public:
	int year, month, day; Action cb; bool open;
	DatePicker (int l, int t, int w, int h, int y, int m, int d, Action cb_);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
	void setOpen (bool o);
	void format (char *out) const;		// "YYYY-MM-DD"
	Calendar *cal;
	int rowH, origW;
};

} // namespace wtk

#endif
