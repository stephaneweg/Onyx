//
// calendar -- the Onyx planner: your appointments and your tasks, by the day, the week or the month.
//
// On the left, a month to jump to a day, your calendars (the categories: Work, Personal...; a
// click shows or hides one) and your tasks (the round box ticks one off; a double click edits it;
// the field below adds one). In the middle, the period chosen on top (Day / Week / Month, Today,
// < and >): a day and a week as a grid of the hours -- the events as blocks in their category's
// colour, the all-day ones in a row on top, now as a red line; a double click on an empty slot (or
// a drag down it) makes an event there, a drag moves one, its bottom edge resizes it, a double
// click opens it --, the month as a grid of days (a double click on a day opens it). An event:
// its title, all day or its times, a repetition (every day, weekday, week -- the days --, two
// weeks, month, year; until a date), a reminder (a notification, while the calendar runs), a
// category, a place, notes.
//
// Its text is drawn with FreeType (DejaVu Sans; UTF-8 -- the bitmap font, Latin-1, without the card's
// TrueType fonts). Kept as iCalendar in SD:/apps/calendar.app/calendar.ics (model.h); File > Import / Export
// iCalendar exchange .ics files with Google Calendar, Outlook, Thunderbird. agenda.txt (the
// coming appointments, "YYYYMMDD|HH:MM title") is written for the desktop's agenda widget; the
// old calendar's notes in it are taken over the first time (agendafiles.h). An argument "YYYYMMDD" opens that day.
// calendar.ics changed by another program (PocketQI's Pocket notes appointments and tasks) is read again.
//
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "fontkit/uikitface.h"

using namespace uikit;

#include "model.h"
#include "agendafiles.h"		// (the paths, agenda.txt and reminders.txt: shared with PocketQI)
#include "views.h"
#include "dialogs.h"

#define W		1000
#define H		660
#define TB_H		48
#define SIDE_W		272
// The kind of files the Import and Export dialogs offer (uikit/dialog.h).
static const char *const ICS_KINDS = "iCalendar files|*.ics|All files|*";

static int g_view = 1;				// 0 day, 1 week, 2 month
static int g_anchor;				// the day the period is around
static Title *g_title;
static SegmentedControl *g_seg;
static TimeGrid *g_grid;
static MonthGrid *g_month;
static Calendar *g_mini;
static CategoryList *g_cats;
static TaskList *g_tasks;
static Heading *g_tasksHead;
static HintBox *g_addTask;
static Panel *g_side, *g_main;
// A date said day first ("9 octobre") in French, the month first otherwise
static bool day_first (void) { const char *l = uk_lang (); return l && l[0] == 'f' && l[1] == 'r'; }

// ---- the file ------------------------------------------------------------------------------------------

static void default_categories (void)
{
	cat_add ("Work", 0x003B82F6); cat_add ("Personal", 0x0010B981); cat_add ("Family", 0x00F59E0B);
	cat_add ("Sport", 0x008B5CF6); cat_add ("Birthdays", 0x00EC4899);
}

// The old calendar's notes ("YYYYMMDD|note", a time in the note: a timed event) become events.
static void take_over_agenda (void)
{
	char *t = cal_read_file (AGENDA_TXT, 0);
	if (!t) return;
	for (char *p = t; *p; )
	{
		char *l = p; while (*p && *p != '\n') p++;
		if (*p) *p++ = '\0';
		int n = slen (l); if (n && l[n - 1] == '\r') l[--n] = '\0';
		if (n < 10 || l[8] != '|') continue;
		int dn = days_from_civil (atoi_n (l, 4), atoi_n (l + 4, 2), atoi_n (l + 6, 2));
		Event &e = ev_new ();
		char *note = l + 9;
		int at = -1;							// "... 17:30 ..." -> 17:30
		for (int i = 0; note[i]; i++)
			if (note[i] >= '0' && note[i] <= '9' && (note[i + 1] == ':' || (note[i + 2] == ':' && note[i + 1] >= '0' && note[i + 1] <= '9')))
			{ int m = parse_hm (note + i); if (m >= 0) { at = m; int k = i; while (note[k] && note[k] != ' ') k++; while (note[k] == ' ') k++; for (int j = i; ; j++) { note[j] = note[k + j - i]; if (!note[j]) break; } while (i > 0 && note[i - 1] == ' ') note[--i] = '\0'; } break; }
		if (g_utf8) to_utf8 (note, e.title, sizeof e.title); else scpy (e.title, sizeof e.title, note);
		e.cat = cat_find ("Personal");
		if (at >= 0) { e.start = dn * 1440 + at; e.end = e.start + 60; }
		else { e.allDay = true; e.start = dn * 1440; e.end = e.start + 1440; }
	}
	delete [] t;
}

static unsigned g_icsSig;			// calendar.ics as read or written last (another program's change seen)

static void save (void)
{
	int len = 0;
	char *t = ics_write (&len);
	kapi_save_file (CAL_ICS, t, (unsigned) len);
	delete [] t;
	cal_write_agenda (g_today);
	g_icsSig = cal_file_sig ();
}

static void load (void)
{
	char *t = cal_read_file (CAL_ICS, 0);
	if (t) { ics_read (t, false); delete [] t; }
	else { default_categories (); take_over_agenda (); save (); }
	if (g_ncat == 0) default_categories ();
	g_icsSig = cal_file_sig ();
}

// ---- what is shown ------------------------------------------------------------------------------------

static void refresh (void)
{
	char t[96] = "";
	int y, m, d; civil_from_days (g_anchor, y, m, d);
	if (g_grid->hidden != (g_view == 2)) g_main->invalidate (true);
	g_grid->hidden = g_view == 2; g_month->hidden = g_view != 2;
	if (g_view == 2)
	{
		g_month->year = y; g_month->month = m; g_month->sel = g_anchor;
		scat (t, sizeof t, TR (MONTH[m - 1])); scat (t, sizeof t, " "); scatn (t, sizeof t, y);
		g_month->invalidate (true);
	}
	else
	{
		int d0 = g_view == 1 ? g_anchor - wday (g_anchor) : g_anchor;
		g_grid->ndays = g_view == 1 ? 7 : 1; g_grid->day0 = d0;
		if (g_view == 0)
		{
			if (day_first ())		// "Mercredi 9 octobre 2026"
			{
				scat (t, sizeof t, TR (WDAY[wday (d0)])); scat (t, sizeof t, " "); scatn (t, sizeof t, d); scat (t, sizeof t, " ");
				scat (t, sizeof t, TR (MONTH[m - 1])); scat (t, sizeof t, " "); scatn (t, sizeof t, y);
			}
			else
			{
				scat (t, sizeof t, TR (WDAY[wday (d0)])); scat (t, sizeof t, ", "); scat (t, sizeof t, TR (MONTH[m - 1]));
				scat (t, sizeof t, " "); scatn (t, sizeof t, d); scat (t, sizeof t, ", "); scatn (t, sizeof t, y);
			}
		}
		else
		{
			int d1 = d0 + 6;
			if (day_first ())		// "6 - 12 octobre 2026", "29 septembre - 5 octobre 2026"
			{
				scatn (t, sizeof t, dn_d (d0));
				if (dn_m (d1) != dn_m (d0)) { scat (t, sizeof t, " "); scat (t, sizeof t, TR (MONTH[dn_m (d0) - 1])); }
				scat (t, sizeof t, " - "); scatn (t, sizeof t, dn_d (d1)); scat (t, sizeof t, " "); scat (t, sizeof t, TR (MONTH[dn_m (d1) - 1]));
				scat (t, sizeof t, " "); scatn (t, sizeof t, dn_y (d1));
			}
			else
			{
				scat (t, sizeof t, TR (MONTH[dn_m (d0) - 1])); scat (t, sizeof t, " "); scatn (t, sizeof t, dn_d (d0)); scat (t, sizeof t, " - ");
				if (dn_m (d1) != dn_m (d0)) { scat (t, sizeof t, TR (MONTH[dn_m (d1) - 1])); scat (t, sizeof t, " "); }
				scatn (t, sizeof t, dn_d (d1)); scat (t, sizeof t, ", "); scatn (t, sizeof t, dn_y (d1));
			}
		}
		g_grid->invalidate (true);
	}
	g_title->set (t);
	g_seg->select (g_view == 0 ? 0 : g_view == 1 ? 1 : 2);
	g_mini->setDate (y, m, d);
	int open = 0; for (int i = 0; i < g_ntk; i++) if (!g_tk[i].done) open++;
	char c[16] = ""; if (open) scatn (c, sizeof c, open);
	g_tasksHead->setRight (c);
	g_tasks->invalidate (true); g_cats->invalidate (true);
}
static void goto_day (int dn, int view) { g_anchor = dn; if (view >= 0) g_view = view; refresh (); }
static void data_changed (bool s) { if (s) save (); refresh (); }
static void sel_changed (void) { g_grid->invalidate (true); g_month->invalidate (true); }

static void step (int dir)
{
	if (g_view == 0) g_anchor += dir;
	else if (g_view == 1) g_anchor += 7 * dir;
	else g_anchor = add_months (g_anchor, dir);
	refresh ();
}

// ---- events ---------------------------------------------------------------------------------------------

static void run_editor (Event &src, int index, int occStart)
{
	EventDialog d (src, index < 0);
	int r = d.run ();
	if (r == 1)
	{
		if (index < 0) { Event &e = ev_new (); e = d.e; g_sel = g_nev - 1; g_selStart = e.start; goto_day (e.start / 1440, -1); }
		else if (g_ev[index].freq != RP_NONE)		// an occurrence of a series was edited
		{
			int c = choose (TR ("Change a repeating event"), TR ("Change only this occurrence, or all of them?"), TR ("Only this one"), TR ("All of them"));
			if (c == 0) return;
			Event &orig = g_ev[index];
			if (c == 1)					// this one: out of the series, an event of its own
			{
				if (orig.nex < MAXEX) orig.exdate[orig.nex++] = occStart / 1440;
				Event one = d.e; new_uid (one.uid, sizeof one.uid);
				one.freq = RP_NONE; one.nex = 0; one.until = 0; one.count = 0;
				Event &n = ev_new (); n = one;
				g_sel = g_nev - 1; g_selStart = one.start;
			}
			else						// all: the series moved by as much as this one was
			{
				int dur = d.e.end - d.e.start, st = orig.start + (d.e.start - occStart);
				orig = d.e; orig.start = st; orig.end = st + dur;
				g_selStart = d.e.start;
			}
		}
		else g_ev[index] = d.e;
		data_changed (true);
	}
	else if (r == 3 && index >= 0)
	{
		Event &e = g_ev[index];
		if (e.freq != RP_NONE)
		{
			int c = choose (TR ("Delete a repeating event"), TR ("Delete only this occurrence, or all of them?"), TR ("Only this one"), TR ("All of them"));
			if (c == 0) return;
			if (c == 1 && e.nex < MAXEX) { e.exdate[e.nex++] = occStart / 1440; g_sel = -1; data_changed (true); return; }
		}
		ev_delete (index); g_sel = -1;
		data_changed (true);
	}
}
static void open_event (int ev, int occStart)
{
	if (ev < 0 || ev >= g_nev) return;
	Event copy = g_ev[ev];
	if (copy.freq != RP_NONE)				// (shown on the occurrence's day)
	{
		int delta = occStart - copy.start;
		copy.start += delta; copy.end += delta;
	}
	run_editor (copy, ev, occStart);
}
static void new_event_at (int start, bool allDay, int end)
{
	Event e; ev_init (e);
	e.cat = g_ncat ? 0 : -1;
	for (int i = 0; i < g_ncat; i++) if (g_cat[i].shown) { e.cat = i; break; }
	if (allDay) { e.allDay = true; e.start = start / 1440 * 1440; e.end = e.start + 1440; }
	else { e.start = start; e.end = end > start ? end : start + 60; e.remind = 15; }
	run_editor (e, -1, e.start);
}
static void event_moved (int ev, int oldOcc, int ns, int ne)
{
	Event &e = g_ev[ev];
	if (e.freq != RP_NONE)
	{
		int c = choose (TR ("Move a repeating event"), TR ("Move only this occurrence, or all of them?"), TR ("Only this one"), TR ("All of them"));
		if (c == 0) { refresh (); return; }
		if (c == 1)
		{
			if (e.nex < MAXEX) e.exdate[e.nex++] = oldOcc / 1440;
			Event one = e; new_uid (one.uid, sizeof one.uid);
			one.freq = RP_NONE; one.nex = 0; one.until = 0; one.count = 0; one.start = ns; one.end = ne;
			Event &n = ev_new (); n = one;
			g_sel = g_nev - 1; g_selStart = ns;
			data_changed (true);
			return;
		}
		int dur = ne - ns;
		e.start += ns - oldOcc; e.end = e.start + dur;
	}
	else { e.start = ns; e.end = ne; }
	g_selStart = ns;
	data_changed (true);
}

// ---- tasks ------------------------------------------------------------------------------------------------

static void task_edit (int i)
{
	if (i < 0 || i >= g_ntk) return;
	TaskDialog d (g_tk[i], false);
	int r = d.run ();
	if (r == 1) { g_tk[i] = d.t; data_changed (true); }
	else if (r == 3) { tk_delete (i); data_changed (true); }
}
static void on_add_task (Widget &)
{
	if (!g_addTask->text[0]) return;
	Task &t = tk_new ();
	scpy (t.title, sizeof t.title, g_addTask->text);
	g_addTask->setText ("");
	data_changed (true);
}

// ---- the toolbar and the menus ------------------------------------------------------------------------

static void on_new (Widget &) { new_event_at (g_anchor * 1440 + (g_anchor == g_today ? (g_nowMin / 60 + 1) * 60 : 9 * 60), false); }
static void on_today (Widget &) { goto_day (g_today, -1); if (g_view != 2) g_grid->scrollToHour (g_nowMin / 60 > 2 ? g_nowMin / 60 - 2 : 0); }
static void on_prev (Widget &) { step (-1); }
static void on_next (Widget &) { step (1); }
static void on_seg (Widget &) { g_view = g_seg->selected; refresh (); }
static void on_mini (Widget &) { goto_day (days_from_civil (g_mini->year, g_mini->month, g_mini->day), -1); }

static void chevron (Canvas &cv, int id, int x, int y, int s, unsigned ink, bool)
{ uk_glyph (cv, id ? WKG_CHEV_RIGHT : WKG_CHEV_LEFT, x + s / 2, y + s / 2, s - 4, ink); }

static void m_new ()   { Widget w (0, 0, 0, 0); on_new (w); }
static void m_task ()
{
	Task t; t.title[0] = '\0'; new_uid (t.uid, sizeof t.uid); t.due = g_anchor; t.done = false; t.cat = -1;
	TaskDialog d (t, true);
	if (d.run () == 1) { Task &n = tk_new (); n = d.t; data_changed (true); }
}
static void m_import ()
{
	char path[256];
	if (!uk_file_open (path, sizeof path, "SD:/", ICS_KINDS)) return;
	char *t = cal_read_file (path, 0);
	if (!t) { uk_messagebox (TR ("Import"), TR ("The file could not be read."), MB_OK); return; }
	int n = ics_read (t, true);
	delete [] t;
	data_changed (true);
	char m[80] = ""; scatn (m, sizeof m, n); scat (m, sizeof m, n == 1 ? TR (" event or task imported.") : TR (" events and tasks imported."));
	uk_messagebox (TR ("Import"), m, MB_OK);
}
static void m_export ()
{
	char path[256];
	if (!uk_file_save (path, sizeof path, "SD:/docs", "calendar.ics", ICS_KINDS)) return;
	int len = 0;
	char *t = ics_write (&len);
	int ok = kapi_save_file (path, t, (unsigned) len);
	delete [] t;
	uk_messagebox (TR ("Export"), ok >= 0 ? TR ("The calendar was exported.") : TR ("The file could not be written."), MB_OK);
}
static void m_edit ()  { if (g_sel >= 0) open_event (g_sel, g_selStart); }
static void m_delete ()
{
	if (g_sel < 0 || g_sel >= g_nev) return;
	Event &e = g_ev[g_sel];
	if (e.freq != RP_NONE)
	{
		int c = choose (TR ("Delete a repeating event"), TR ("Delete only this occurrence, or all of them?"), TR ("Only this one"), TR ("All of them"));
		if (c == 0) return;
		if (c == 1 && e.nex < MAXEX) { e.exdate[e.nex++] = g_selStart / 1440; g_sel = -1; data_changed (true); return; }
	}
	ev_delete (g_sel); g_sel = -1;
	data_changed (true);
}
static void m_day ()   { g_view = 0; refresh (); }
static void m_week ()  { g_view = 1; refresh (); }
static void m_month () { g_view = 2; refresh (); }
static void m_today () { Widget w (0, 0, 0, 0); on_today (w); }
static void m_prev ()  { step (-1); }
static void m_next ()  { step (1); }

// ---- reminders: a notification when one is due (while the calendar runs) --------------------------------

static void reminders (void)
{
	static int lastMin = -1;
	int now = g_today * 1440 + g_nowMin;
	if (now == lastMin) return;
	if (kapi_ipc_lookup ("agenda")) { lastMin = now; return; }	// (the agenda widget sends them)
	if (lastMin < 0) { lastMin = now; return; }
	static Occ occ[200];
	int n = occurrences (g_today, g_today + 3, occ, 200, true);
	for (int i = 0; i < n; i++)
	{
		const Event &e = g_ev[occ[i].ev];
		if (e.remind < 0) continue;
		int at = occ[i].start - e.remind;
		if (at > lastMin && at <= now)
		{
			char t[200]; cal_reminder_text (e, occ[i].start, t, sizeof t);
			notify (TR ("Calendar"), t);
		}
	}
	lastMin = now;
}

static SidePanel *g_sp; static Root *g_rootp;
static void lay_out (void)			// the side panel's place, the period's panel beside it
{
	if (!g_sp || !g_rootp) return;
	int w = g_rootp->width, h = g_rootp->height - TB_H;
	g_sp->place (0, TB_H, SIDE_W, h);
	int sw = g_sp->reservedWidth ();
	g_main->left = sw; g_main->top = TB_H; g_main->resizeTo (w - sw > 1 ? w - sw : 1, h > 1 ? h : 1);
	g_rootp->invalidate (true);
}
class CalRoot : public Root
{
public:
	CalRoot () : Root (W, H, TR ("Calendar")) {}
	void onResized () override { lay_out (); }
	void onSizeClass (int) override { lay_out (); }
	void onTick () override
	{
		int y = 0, mo = 0, d = 0, h = 0, mi = 0;
		kapi_get_datetime (&y, &mo, &d, &h, &mi, 0);
		int today = days_from_civil (y, mo, d), now = h * 60 + mi;
		if (today != g_today || now != g_nowMin)
		{
			bool newDay = today != g_today;
			g_today = today; g_nowMin = now;
			if (g_view != 2) g_grid->invalidate (true);
			if (newDay) { refresh (); cal_write_agenda (g_today); }
			reminders ();
		}
		// calendar.ics changed by another program (PocketQI's agenda): read again, every 2 s, no dialog open
		static unsigned seen;
		if (kapi_get_ticks () - seen >= 200)
		{
			seen = kapi_get_ticks ();
			for (Widget *c = firstChild; c; c = c->nextSib) if (c->modal && !c->hidden) return;
			if (cal_file_sig () != g_icsSig)
			{
				g_nev = g_ntk = g_ncat = 0;
				load ();
				refresh ();
			}
		}
	}
};

int main (void)
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, 0);
	if (y < 1970) { y = 2026; mo = 1; d = 1; }
	g_today = days_from_civil (y, mo, d); g_nowMin = h * 60 + mi;
	g_anchor = g_today;
	char a[32]; int n = kapi_get_args (a, sizeof a);
	if (n >= 8 && a[0] >= '1' && a[0] <= '9') { int ay = atoi_n (a, 4), am = atoi_n (a + 4, 2), ad = atoi_n (a + 6, 2); if (am >= 1 && am <= 12 && ad >= 1 && ad <= 31) { g_anchor = days_from_civil (ay, am, ad); g_view = 0; } }
	if (app_ini_load ("config.ini") >= 0)
	{
		g_utcOffset = app_ini_get_int ("calendar", "utc_offset", g_utcOffset);
		g_euDst = app_ini_get_int ("calendar", "eu_dst", 1) != 0;
		const char *v = app_ini_get ("calendar", "view", "week");
		if (n < 8) g_view = ieq (v, "day") ? 0 : ieq (v, "month") ? 2 : 1;
	}
	g_utf8 = ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();
	load ();

	CalRoot root;
	if (root.canvas.px == 0) return 1;
	g_rootp = &root;
	g_fh = uk_fh ();

	// The toolbar: New event, Today, < >, the period; Day / Week / Month on the right.
	ToolBar *tb = new ToolBar (0, 0, W, TB_H);
	tb->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	root.addChild (tb);
	tb->space (12);
	int nbw = uk_tw (TR ("New event"), 2) + 50; if (nbw < 132) nbw = 132;		// (as wide as its words: the system's language)
	AccentButton *nb = new AccentButton (0, 0, nbw, 32, TR ("New event"), on_new, true);
	nb->tip = TR ("A new event (Ctrl+N) -- or double-click a free slot");
	tb->add (nb, 0);
	tb->space (18);
	int tdw = uk_tw (TR ("Today")) + 28; if (tdw < 74) tdw = 74;
	Button *tdy = new Button (0, 0, tdw, 30, TR ("Today"), on_today); tdy->tip = TR ("Back to today (Ctrl+T)");
	tb->add (tdy, 0);
	tb->space (6);
	ToolButton *pv = (new ToolButton (30, 30, TR ("Previous"), on_prev))->setIcon (chevron, 0);
	ToolButton *nx = (new ToolButton (30, 30, TR ("Next"), on_next))->setIcon (chevron, 1);
	tb->add (pv, 0); tb->add (nx, 2);
	tb->space (10);
	g_title = new Title (0, 0, 380, 30);
	tb->add (g_title, 0);
	static const char *const VIEWS[] = { TR ("Day"), TR ("Week"), TR ("Month") };
	g_seg = new SegmentedControl (0, 0, 240, 30, VIEWS, 3, g_view, on_seg);
	tb->addRight (g_seg, 12);

	// The side bar.
	int bodyH = H - TB_H;
	// (P7) A navigation SidePanel (uikit/sidepanel.h) of free content: whole on the desktop and a wide screen, a
	// drawer otherwise (pocket, console: the tab at the left edge); the period's panel takes what it leaves.
	g_side = new Panel (0, 0, SIDE_W, bodyH, C_BG);
	g_sp = new SidePanel (0, TB_H, SIDE_W, bodyH, UK_SP_LEFT, UK_SP_NAVIGATION);
	g_sp->setColors (C_BG, UK_AUTO);
	g_sp->setContent (g_side);
	g_sp->setName (TR ("Calendars"));
	g_sp->onPresentation = [] (SidePanel &, int) { lay_out (); };
	int sy = 10;
	int yy, mm, dd; civil_from_days (g_anchor, yy, mm, dd);
	g_mini = new Calendar ((SIDE_W - CAL_W) / 2, sy, yy, mm, dd, on_mini);
	g_mini->tip = TR ("Pick a day to go to it");
	g_side->addChild (g_mini);
	sy += CAL_H + 16;
	g_side->addChild (new Heading (16, sy, SIDE_W - 32, g_fh + 6, TR ("CALENDARS")));
	sy += g_fh + 10;
	g_cats = new CategoryList (12, sy, SIDE_W - 24, g_ncat * CategoryList::rowH ());
	g_cats->tip = TR ("Click a calendar to show or hide its events");
	g_side->addChild (g_cats);
	sy += g_ncat * CategoryList::rowH () + 14;
	g_tasksHead = new Heading (16, sy, SIDE_W - 32, g_fh + 6, TR ("TASKS"));
	g_side->addChild (g_tasksHead);
	sy += g_fh + 10;
	g_tasks = new TaskList (12, sy, SIDE_W - 24, bodyH - sy - 48);
	g_tasks->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_BOTTOM;
	g_tasks->tip = TR ("The round box ticks a task off; a double click edits it");
	g_side->addChild (g_tasks);
	g_addTask = new HintBox (12, bodyH - 40, SIDE_W - 24, 30, TR ("+ Add a task"), on_add_task);
	g_addTask->maxLen = 120; g_addTask->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM;
	g_side->addChild (g_addTask);

	// The period.
	Panel *mainP = g_main = new Panel (SIDE_W, TB_H, W - SIDE_W, bodyH, C_FIELD);
	root.addChild (mainP);
	root.addChild (g_sp);				// (over the period: a drawer and its tab)
	g_grid = new TimeGrid (0, 0, W - SIDE_W, bodyH); g_grid->anchor = ANCHOR_FILL;
	g_grid->scrollY = 8 * g_grid->hourH;
	mainP->addChild (g_grid);
	g_month = new MonthGrid (8, 4, W - SIDE_W - 16, bodyH - 12); g_month->anchor = ANCHOR_FILL;
	mainP->addChild (g_month);

	static Menu menu;
	menu.menu (TR ("File"));
	menu.item (TR ("New Event"), "^N", UK_CTRL ('N'), m_new);
	menu.item (TR ("New Task..."), "", 0, m_task);
	menu.separator ();
	menu.item (TR ("Import iCalendar..."), "", 0, m_import);
	menu.item (TR ("Export iCalendar..."), "", 0, m_export);
	menu.menu (TR ("Event"));
	menu.item (TR ("Open"), "Enter", 0, m_edit);
	menu.item (TR ("Delete"), "", 0, m_delete);
	menu.menu (TR ("View"));
	menu.item (TR ("Day"), "", 0, m_day);
	menu.item (TR ("Week"), "", 0, m_week);
	menu.item (TR ("Month"), "", 0, m_month);
	menu.separator ();
	menu.item (TR ("Today"), "^T", UK_CTRL ('T'), m_today);
	menu.item (TR ("Previous"), "", 0, m_prev);
	menu.item (TR ("Next"), "", 0, m_next);
	menu.publish ();

	// in a narrow window the bar keeps what matters: Today and the period's title give way (their ranks)
	tb->setPriority (tdy, UK_TB_IF_ROOM, 1);
	tb->setPriority (g_title, UK_TB_IF_ROOM, 2);
	root.setResizable (true);
	root.fitWorkArea ();
	lay_out ();
	refresh ();
	g_grid->setFocus ();
	root.run ();
	return 0;
}
