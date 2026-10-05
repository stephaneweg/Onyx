//
// calendar/dialogs.h -- the event editor (title, all day, start and end, repeat -- the days of the
// week --, until, reminder, category, place, notes; Delete / Cancel / Save), the task editor, and a
// question with buttons of its own ("Only this one" / "All of them").
//
#ifndef _calendar_dialogs_h
#define _calendar_dialogs_h

static void dlg_btn (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }
static void centre (Modal *m)
{
	Root *r = Root::current ();
	if (r) { m->left = (r->width - m->width) / 2; m->top = (r->height - m->height) / 2; if (m->top < 4) m->top = 4; }
}
static Label *dlabel (Widget *parent, int x, int y, int w, const char *s)
{
	Label *l = new Label (x, y, w, 28, s, C_TEXT, C_FACE);
	parent->addChild (l);
	return l;
}

// "9:30", "930", "9h30", "9" -> minutes (-1: not a time).
static int parse_hm (const char *s)
{
	int h = 0, m = 0, nd = 0; const char *p = s;
	while (*p == ' ') p++;
	while (*p >= '0' && *p <= '9') { h = h * 10 + (*p++ - '0'); nd++; }
	if (nd == 0) return -1;
	if (nd >= 3) { m = h % 100; h /= 100; }
	else if (*p == ':' || *p == 'h' || *p == 'H' || *p == '.')
	{
		p++; int k = 0;
		while (*p >= '0' && *p <= '9' && k < 2) { m = m * 10 + (*p++ - '0'); k++; }
	}
	while (*p == ' ') p++;
	if ((p[0] == 'p' || p[0] == 'P') && h < 12) h += 12;
	if ((p[0] == 'a' || p[0] == 'A') && h == 12) h = 0;
	if (h > 23 || m > 59) return -1;
	return h * 60 + m;
}

// A question with its own buttons: returns the tag of the one clicked (0: cancelled).
class ChoiceDialog : public Modal
{
public:
	const char *m_title, *m_text;
	ChoiceDialog (const char *title, const char *text, const char *a, const char *b)
		: Modal (440, 150), m_title (title), m_text (text)
	{
		centre (this);
		Button *c = new Button (width - 100, height - 42, 86, 30, "Cancel", dlg_btn); c->tag = 0; addChild (c);
		Button *x = new Button (width - 100 - 8 - 130, height - 42, 130, 30, b, dlg_btn); x->tag = 2; addChild (x);
		AccentButton *y = new AccentButton (width - 100 - 8 - 130 - 8 - 130, height - 42, 130, 30, a, dlg_btn); y->tag = 1; addChild (y);
	}
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { close (1); return true; } return false; }
	void onDraw () override { drawBox (m_title); uk_text_l (canvas, 20, titleH () + 16, g_fh + 4, m_text, C_TEXT); }
};
static int choose (const char *title, const char *text, const char *a, const char *b)
{
	ChoiceDialog d (title, text, a, b);
	return d.run ();
}

// The days of the week, each a round toggle (a weekly repetition's).
class WeekdayPicker : public Widget
{
public:
	int mask = 0;
	WeekdayPicker (int l, int t) : Widget (l, t, 7 * 34, 30) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		static const char *const L[7] = { "M", "T", "W", "T", "F", "S", "S" };
		for (int i = 0; i < 7; i++)
		{
			int x = i * 34;
			bool on = mask & (1 << i);
			if (on) disc (canvas, x, 1, 28, C_ACCENT);
			else { disc (canvas, x, 1, 28, uk_mix (C_FACE, C_TEXT, 70)); disc (canvas, x + 1, 2, 26, uk_mix (C_FACE, 0x00FFFFFF, 90)); }
			uk_text_c (canvas, x, 1, 28, 28, L[i], on ? uk_ink_on (C_ACCENT) : C_TEXT, 2);
		}
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0) { pressed = false; return false; }
		if (bl && !pressed) { pressed = true; int i = mx / 34; if (i >= 0 && i < 7) { mask ^= 1 << i; if (!mask) mask = 1 << i; invalidate (true); } }
		else if (!bl) pressed = false;
		return true;
	}
};

// ---- the event editor --------------------------------------------------------------------------------------

static const char *const REPEAT[] = { "Does not repeat", "Every day", "Every weekday (Mon - Fri)", "Every week",
				      "Every 2 weeks", "Every month", "Every year", "Custom (as imported)" };
static const int   REMIND_MIN[] = { -1, 0, 5, 10, 15, 30, 60, 120, 1440, 2880 };
static const char *const REMIND[] = { "No reminder", "At the start", "5 minutes before", "10 minutes before", "15 minutes before",
				      "30 minutes before", "1 hour before", "2 hours before", "1 day before", "2 days before" };
static const char *g_catNames[MAXCAT + 1];

static void ed_changed (Widget &w);

class EventDialog : public Modal
{
public:
	Event e;				// the copy edited
	bool isNew;
	HintBox *title, *t0, *t1, *place;
	Checkbox *allDay, *untilOn;
	DatePicker *d0, *d1, *until;
	Dropdown *repeat, *remind, *cat;
	WeekdayPicker *wdays;
	Label *lUntil, *lDays, *err;
	Textarea *notes;
	int nopts;
	Widget *markRow; bool rowsUp = false;

	EventDialog (const Event &src, bool isNew_) : Modal (600, 560), e (src), isNew (isNew_)
	{
		centre (this);
		int x = 20, lw = 84, y = titleH () + 14, W2 = width - 40;
		title = new HintBox (x, y, W2, 32, "Add a title");
		title->maxLen = 90; title->setText (e.title); title->tag = 1;
		addChild (title);
		y += 44;
		allDay = new Checkbox (x + lw, y, 120, 26, "All day", e.allDay, ed_changed, C_FACE);
		addChild (allDay);
		y += 34;
		int sd = e.start / 1440, ed = e.allDay ? (e.end - 1) / 1440 : e.end / 1440;
		if (ed < sd) ed = sd;
		char hm[8];
		dlabel (this, x, y, lw, "Starts");
		d0 = new DatePicker (x + lw, y, 130, 28, dn_y (sd), dn_m (sd), dn_d (sd), ed_changed); addChild (d0);
		fmt_hm (e.start, hm, sizeof hm);
		t0 = new HintBox (x + lw + 138, y, 64, 28, "9:00"); t0->maxLen = 7; t0->setText (hm); addChild (t0);
		dlabel (this, x + lw + 216, y, 44, "Ends");
		d1 = new DatePicker (x + lw + 260, y, 130, 28, dn_y (ed), dn_m (ed), dn_d (ed), 0); addChild (d1);
		fmt_hm (e.end, hm, sizeof hm);
		t1 = new HintBox (x + lw + 398, y, 64, 28, "10:00"); t1->maxLen = 7; t1->setText (hm); addChild (t1);
		y += 40;

		dlabel (this, x, y, lw, "Repeat");
		int ro = repeat_option ();
		nopts = ro == 7 ? 8 : 7;
		repeat = new Dropdown (x + lw, y, 240, 28, REPEAT, nopts, ro, ed_changed); addChild (repeat);
		untilOn = new Checkbox (x + lw + 256, y, 70, 28, "Until", e.until != 0, ed_changed, C_FACE); addChild (untilOn);
		int ud = e.until ? e.until : add_months (sd, 3);
		until = new DatePicker (x + lw + 334, y, 140, 28, dn_y (ud), dn_m (ud), dn_d (ud), 0); addChild (until);
		lUntil = 0;
		y += 38;
		lDays = dlabel (this, x, y, lw, "On");
		wdays = new WeekdayPicker (x + lw, y);
		wdays->mask = e.wmask ? e.wmask : 1 << wday (sd);
		addChild (wdays);
		y += 40;
		markRow = wdays;					// (the rows after it move up when it is hidden)

		dlabel (this, x, y, lw, "Reminder");
		int ri = 0; for (int i = 0; i < 10; i++) if (REMIND_MIN[i] == e.remind) ri = i;
		remind = new Dropdown (x + lw, y, 190, 28, REMIND, 10, ri, 0); addChild (remind);
		dlabel (this, x + lw + 206, y, 80, "Category");
		for (int i = 0; i < g_ncat; i++) g_catNames[i] = g_cat[i].name;
		g_catNames[g_ncat] = "None";
		cat = new Dropdown (x + lw + 290, y, 176, 28, g_catNames, g_ncat + 1, e.cat >= 0 && e.cat < g_ncat ? e.cat : g_ncat, 0); addChild (cat);
		y += 40;
		dlabel (this, x, y, lw, "Place");
		place = new HintBox (x + lw, y, W2 - lw, 28, "Add a place"); place->maxLen = 90; place->setText (e.place); addChild (place);
		y += 38;
		dlabel (this, x, y, lw, "Notes");
		notes = new Textarea (x + lw, y, W2 - lw, height - y - 64, 600);
		notes->setContent (e.notes); addChild (notes);

		err = new Label (x, height - 44, 250, 28, "", RED, C_FACE); addChild (err);
		if (!isNew)
		{
			AccentButton *del = new AccentButton (x, height - 46, 96, 32, "Delete", dlg_btn, false, true); del->tag = 3; addChild (del);
			err->left = x + 106;
		}
		Button *cancel = new Button (width - 20 - 100 - 8 - 96, height - 46, 96, 32, "Cancel", dlg_btn); cancel->tag = 0; addChild (cancel);
		AccentButton *save = new AccentButton (width - 20 - 100, height - 46, 100, 32, "Save", dlg_btn); save->tag = 1; addChild (save);
		sync ();
		title->setFocus ();
	}
	int repeat_option () const
	{
		if (e.freq == RP_NONE) return 0;
		if (e.count) return 7;
		if (e.freq == RP_DAILY && e.interval == 1) return 1;
		if (e.freq == RP_WEEKLY && e.interval == 1 && e.wmask == 0x1F) return 2;
		if (e.freq == RP_WEEKLY && e.interval == 1) return 3;
		if (e.freq == RP_WEEKLY && e.interval == 2) return 4;
		if (e.freq == RP_MONTHLY && e.interval == 1) return 5;
		if (e.freq == RP_YEARLY && e.interval == 1) return 6;
		return 7;
	}
	void sync ()						// what shows follows the choices
	{
		t0->hidden = t1->hidden = allDay->checked;
		bool rep = repeat->sel != 0;
		untilOn->hidden = !rep; until->hidden = !rep || !untilOn->checked;
		bool weekly = repeat->sel == 3 || repeat->sel == 4;
		lDays->hidden = wdays->hidden = !weekly;
		if (rowsUp == weekly)				// the rows below: up when the days' row is hidden
		{
			int dy = weekly ? 40 : -40;
			for (Widget *w = markRow->nextSib; w; w = w->nextSib)
			{
				if (w == err) break;
				w->top += dy;
				if (w == notes) w->resizeTo (w->width, w->height - dy);
			}
			rowsUp = !weekly;
		}
		invalidate (true);
	}
	void onChanged () { sync (); }
	bool collect ()						// the fields -> e; false (and why) if they do not hold
	{
		scpy (e.title, sizeof e.title, title->text);
		scpy (e.place, sizeof e.place, place->text);
		scpy (e.notes, sizeof e.notes, notes->content ());
		e.allDay = allDay->checked;
		int sd = days_from_civil (d0->year, d0->month, d0->day), ed = days_from_civil (d1->year, d1->month, d1->day);
		if (e.allDay)
		{
			if (ed < sd) { err->setText ("It ends before it starts."); return false; }
			e.start = sd * 1440; e.end = (ed + 1) * 1440;
		}
		else
		{
			int a = parse_hm (t0->text), b = parse_hm (t1->text);
			if (a < 0 || b < 0) { err->setText ("A time is not valid (9:30)."); return false; }
			e.start = sd * 1440 + a; e.end = ed * 1440 + b;
			if (e.end <= e.start) { err->setText ("It ends before it starts."); return false; }
		}
		switch (repeat->sel)
		{
		case 0: e.freq = RP_NONE; break;
		case 1: e.freq = RP_DAILY; e.interval = 1; e.wmask = 0; break;
		case 2: e.freq = RP_WEEKLY; e.interval = 1; e.wmask = 0x1F; break;
		case 3: e.freq = RP_WEEKLY; e.interval = 1; e.wmask = wdays->mask; break;
		case 4: e.freq = RP_WEEKLY; e.interval = 2; e.wmask = wdays->mask; break;
		case 5: e.freq = RP_MONTHLY; e.interval = 1; e.wmask = 0; break;
		case 6: e.freq = RP_YEARLY; e.interval = 1; e.wmask = 0; break;
		default: break;					// (custom: as it came)
		}
		if (repeat->sel != 7) e.count = 0;
		e.until = e.freq != RP_NONE && untilOn->checked ? days_from_civil (until->year, until->month, until->day) : 0;
		if (e.until && e.until < sd) { err->setText ("The repetition ends before it starts."); return false; }
		e.remind = REMIND_MIN[remind->sel];
		e.cat = cat->sel < g_ncat ? cat->sel : -1;
		return true;
	}
	void onButton (int tag) override
	{
		if (tag == 1 && !collect ()) { err->invalidate (true); return; }
		close (tag);
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { onButton (1); return true; }
		return false;
	}
	void onDraw () override { drawBox (isNew ? "New event" : "Edit event"); }
};
static void ed_changed (Widget &w)
{
	for (Widget *p = w.parent; p; p = p->parent)
		if (p->modal) { ((EventDialog *) p)->onChanged (); return; }
}

// ---- the task editor -------------------------------------------------------------------------------------

class TaskDialog : public Modal
{
public:
	Task t; bool isNew;
	HintBox *title; Checkbox *dueOn; DatePicker *due; Dropdown *cat; Checkbox *done;
	TaskDialog (const Task &src, bool isNew_) : Modal (480, 250), t (src), isNew (isNew_)
	{
		centre (this);
		int x = 20, lw = 84, y = titleH () + 14;
		title = new HintBox (x, y, width - 40, 32, "What is there to do?"); title->maxLen = 120; title->setText (t.title); addChild (title);
		y += 44;
		dueOn = new Checkbox (x, y, lw, 28, "Due", t.due != 0, 0, C_FACE); addChild (dueOn);
		int d = t.due ? t.due : g_today;
		due = new DatePicker (x + lw, y, 140, 28, dn_y (d), dn_m (d), dn_d (d), 0); addChild (due);
		done = new Checkbox (x + lw + 160, y, 100, 28, "Done", t.done, 0, C_FACE); addChild (done);
		y += 38;
		dlabel (this, x, y, lw, "Category");
		for (int i = 0; i < g_ncat; i++) g_catNames[i] = g_cat[i].name;
		g_catNames[g_ncat] = "None";
		cat = new Dropdown (x + lw, y, 180, 28, g_catNames, g_ncat + 1, t.cat >= 0 && t.cat < g_ncat ? t.cat : g_ncat, 0); addChild (cat);
		if (!isNew) { AccentButton *del = new AccentButton (x, height - 46, 96, 32, "Delete", dlg_btn, false, true); del->tag = 3; addChild (del); }
		Button *cancel = new Button (width - 20 - 100 - 8 - 96, height - 46, 96, 32, "Cancel", dlg_btn); cancel->tag = 0; addChild (cancel);
		AccentButton *save = new AccentButton (width - 20 - 100, height - 46, 100, 32, "Save", dlg_btn); save->tag = 1; addChild (save);
		title->setFocus ();
	}
	void onButton (int tag) override
	{
		if (tag == 1)
		{
			if (!title->text[0]) return;
			scpy (t.title, sizeof t.title, title->text);
			t.due = dueOn->checked ? days_from_civil (due->year, due->month, due->day) : 0;
			t.done = done->checked;
			t.cat = cat->sel < g_ncat ? cat->sel : -1;
		}
		close (tag);
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { onButton (1); return true; }
		return false;
	}
	void onDraw () override { drawBox (isNew ? "New task" : "Edit task"); }
};

#endif
