//
// alarmsview.h -- the Clock's Alarms tab and the alarm editor (04 D9-D12; 03 step 7). One translation unit with main.cpp.
//
//   NextAlarmBar  "Next alarm: tomorrow 07:00 -- in 18 h 26 min", "Snoozed until 12:44 -- Tea", "No alarm set"
//   AlarmList     one row an alarm, sorted by time: the time, the label, the repeat, its state (snoozed, missed,
//                 invalid), an on / off switch; a click selects, a click on the switch toggles, a double click edits;
//                 Space toggles, Enter edits, Delete deletes (no question: 02 #13), Ctrl+N a new one (20 at most)
//   EditVeil      New Alarm / Edit Alarm: the time (an LcdDisplay and two Spins), the label (40 characters), the days
//                 (seven toggles, Every day, Weekdays; none: once), the sound (Chimes, Beeps, Marimba, Test); Enter OK,
//                 Esc Cancel
// Every change: alarms.txt written (alarms_write: the tokens, never the shown words) and clockd told (RELOAD).
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

static const char *const SOUNDKEY[3] = { "chimes", "beeps", "marimba" };		// (alarms.txt's tokens)
static const char *const SOUNDNAME[3] = { TRN ("Chimes"), TRN ("Beeps"), TRN ("Marimba") };
static int sound_index (const char *t) { for (int i = 0; i < 3; i++) if (!strcmp (t, SOUNDKEY[i])) return i; return 0; }
static int sound_test (const char *token);		// (ring.h) one round of the sound -> 1 played, 0 busy, -1 no sound

// "Once", "Every day", "Weekdays", "Weekends", else the days: "Mon, Wed, Fri" (the system's language)
static void repeat_text (unsigned m, char *o, int cap)
{
	switch (alarm_repeat_kind (m))
	{
	case REP_ONCE:     snprintf (o, cap, "%s", TR ("Once")); return;
	case REP_DAILY:    snprintf (o, cap, "%s", TR ("Every day")); return;
	case REP_WEEKDAYS: snprintf (o, cap, "%s", TR ("Weekdays")); return;
	case REP_WEEKENDS: snprintf (o, cap, "%s", TR ("Weekends")); return;
	}
	o[0] = 0;
	for (int i = 0; i < 7; i++) if (m & (1u << i)) { if (o[0]) cat (o, cap, ", "); cat (o, cap, TR (SHORTDAY[i])); }
}
static void minute_hm (long minute, char *o, int cap) { long m = minute % 1440; if (m < 0) m += 1440; clk_fmt_hm ((int) (m / 60), (int) (m % 60), o, cap); }
static bool shown_on (const Alarm &a) { return a.valid && a.on && !alarm_passed (a, g_now.minute); }
static bool snoozed (const Alarm &a) { return a.on && a.snooze > g_now.minute; }
static const char *label_of (const Alarm &a) { return a.label[0] ? a.label : TR ("Alarm"); }

// ---- the order shown (by time; the invalid ones last), the selection (an id) ------------------------------------------
static int g_ord[ALARMS_MAX], g_nord;
static int g_alSel = -1;				// the selected alarm's id (-1 none)
static void alarms_sort ()
{
	g_nord = g_al.n;
	for (int i = 0; i < g_al.n; i++) g_ord[i] = i;
	for (int i = 1; i < g_nord; i++)			// (insertion: stable, the file's order among equals)
	{
		int k = g_ord[i], j = i;
		const Alarm &a = g_al.a[k];
		int ka = a.valid ? a.hh * 60 + a.mm : 9999;
		while (j > 0)
		{
			const Alarm &b = g_al.a[g_ord[j - 1]];
			if ((b.valid ? b.hh * 60 + b.mm : 9999) <= ka) break;
			g_ord[j] = g_ord[j - 1]; j--;
		}
		g_ord[j] = k;
	}
}
static int sel_row () { for (int r = 0; r < g_nord; r++) if (g_al.a[g_ord[r]].id == g_alSel) return r; return -1; }
static Alarm *sel_alarm () { int i = alarm_find (g_al, g_alSel); return i >= 0 ? &g_al.a[i] : 0; }

static void alarms_changed ();
static void alarm_toggle (int id);
static void alarm_edit_open (int id);

// ---- the next alarm ----------------------------------------------------------------------------------------------------
class NextAlarmBar : public Widget
{
public:
	char bold[128], rest[128]; bool none; int idx;
	NextAlarmBar (int x, int y, int w, int h) : Widget (x, y, w, h), none (true), idx (-1) { bold[0] = rest[0] = 0; anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT; }
	// the text for now -> printed when it changed
	void update ()
	{
		char b[128], r[128], hm[8];
		long at = alarms_next (g_al, g_now.minute, &idx);
		bool no = at < 0;
		b[0] = r[0] = 0;
		if (!no)
		{
			const Alarm &a = g_al.a[idx];
			minute_hm (at, hm, sizeof hm);
			if (a.snooze == at)
			{
				snprintf (b, sizeof b, TR ("Snoozed until %s"), hm);
				snprintf (r, sizeof r, "  \xE2\x80\x94  %s", label_of (a));
			}
			else
			{
				long mins = at - g_now.minute;
				char in[64];
				if (mins >= 60) snprintf (in, sizeof in, TR ("in %d h %d min"), (int) (mins / 60), (int) (mins % 60));
				else snprintf (in, sizeof in, TR ("in %d min"), (int) mins);
				snprintf (b, sizeof b, "%s %s %s", TR ("Next alarm:"), day_word (clk_day_of (at)), hm);
				snprintf (r, sizeof r, "  \xE2\x80\x94  %s", in);
			}
		}
		if (no == none && !strcmp (b, bold) && !strcmp (r, rest)) return;
		none = no; snprintf (bold, sizeof bold, "%s", b); snprintf (rest, sizeof rest, "%s", r);
		if (none) say ("next none");
		else								// (in English: what the PC tests compare)
		{
			const Alarm &a = g_al.a[idx];
			long k = clk_day_of (at) - g_now.day, mins = at - g_now.minute;
			if (a.snooze == at) say ("next snoozed %s (%s)", hm, a.label);
			else say ("next %s %s in %ld h %ld min", k == 0 ? "today" : k == 1 ? "tomorrow" : DAYNAME[clk_wday (clk_day_of (at))], hm, mins / 60, mins % 60);
		}
		invalidate (true);
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		unsigned f = none ? uk_tone (C_BG, 120) : uk_mix (C_BG, C_ACCENT, 46);
		uk_rbox (canvas, 0, 0, width, height, 8, f, f);
		uk_rline (canvas, 0, 0, width, height, 8, none ? uk_tone (C_BG, 96) : uk_mix (C_BG, C_ACCENT, 120));
		unsigned ink = uk_ink_for (f);
		if (none)
		{
			draw_bell (canvas, 24, height / 2, 18, uk_mix (f, ink, 110)); draw_slash (canvas, 24, height / 2, 18, uk_mix (f, ink, 110), f);
			uk_text_l (canvas, 44, 0, height, TR ("No alarm set"), uk_mix (f, ink, 150));
			return;
		}
		draw_bell (canvas, 24, height / 2, 18, C_ACCENT);
		UkFaceScope sc (face (14));
		uk_text_l (canvas, 44, 0, height, bold, ink, 2);
		uk_text_l (canvas, 44 + uk_tw (bold, 2), 0, height, rest, ink);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0) return false;
		if (bl && !pressed) { pressed = true; if (!none && idx >= 0 && idx < g_al.n) { g_alSel = g_al.a[idx].id; alarms_changed (); } }
		else if (!bl) pressed = false;
		return true;
	}
};

// ---- the list ----------------------------------------------------------------------------------------------------------
class AlarmList : public Widget
{
public:
	enum { ROW = 60 };
	int top; UkBarDrag bar; unsigned lastClick; int lastRow;
	AlarmList (int x, int y, int w, int h) : Widget (x, y, w, h), top (0), lastClick (0), lastRow (-1) { canFocus = true; anchor = ANCHOR_FILL; }
	unsigned bgColor () override { return C_BG; }
	int contentH () const { return g_nord * ROW + 8; }
	int rowW () { return width - (contentH () > height ? UK_SBW + 2 : 0); }
	void clampTop () { int most = contentH () - height; if (top > most) top = most; if (top < 0) top = 0; }
	void showSel ()
	{
		int r = sel_row ();
		if (r < 0) return;
		int y = 4 + r * ROW;
		if (y < top) top = y - 4;
		if (y + ROW > top + height) top = y + ROW - height + 4;
		clampTop ();
	}
	void selectRow (int r)
	{
		if (g_nord == 0) return;
		if (r < 0) r = 0;
		if (r >= g_nord) r = g_nord - 1;
		g_alSel = g_al.a[g_ord[r]].id;
		alarms_changed ();
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD, hasFocus);
		unsigned ink = C_FIELD_TEXT, d = uk_mix (C_FIELD, C_FIELD_TEXT, 140), faint = uk_mix (C_FIELD, C_FIELD_TEXT, 30), off = uk_mix (C_FIELD, C_FIELD_TEXT, 105);
		if (g_nord == 0)
		{
			draw_bell (canvas, width / 2, height / 2 - 34, 46, uk_mix (C_FIELD, C_FIELD_TEXT, 60));
			{ UkFaceScope sc (face (15)); uk_text_c (canvas, 0, height / 2 + 4, width, 22, TR ("No alarms"), ink, 2); }
			uk_text_c (canvas, 0, height / 2 + 28, width, 20, TR ("Create one with + New Alarm (Ctrl+N)."), d);
			uk_text_c (canvas, 0, height / 2 + 48, width, 20, TR ("Alarms ring even when the Clock is closed."), d);
			return;
		}
		clampTop ();
		int rw = rowW (), sr = sel_row ();
		for (int r = 0; r < g_nord; r++)
		{
			const Alarm &a = g_al.a[g_ord[r]];
			int y = 4 + r * ROW - top;
			if (y + ROW < 0 || y > height) continue;
			if (r == sr) { unsigned t = uk_mix (C_FIELD, C_ACCENT, hasFocus ? 90 : 60); uk_rbox (canvas, 5, y, rw - 10, ROW - 3, 7, t, t); }
			else if (r + 1 < g_nord && r + 1 != sr) canvas.fillRect (16, y + ROW - 2, rw - 32, 1, faint);
			bool on = shown_on (a);
			char t[8], rp[96];
			if (a.valid) clk_fmt_hm (a.hh, a.mm, t, sizeof t); else snprintf (t, sizeof t, "--:--");
			{ UkFaceScope sc (face (30)); uk_text (canvas, 18, y + (ROW - 3 - uk_fh ()) / 2, t, on ? ink : off, 2); }
			int x = 132;
			{ UkFaceScope sc (face (14)); uk_text (canvas, x, y + 10, a.valid ? label_of (a) : TR ("Invalid"), on || !a.valid ? ink : off, 2); }
			if (!a.valid) { uk_text (canvas, x, y + 32, TR ("Cannot be read: correct alarms.txt or delete it"), RED ()); continue; }
			repeat_text (a.days, rp, sizeof rp);
			uk_text (canvas, x, y + 32, rp, d);
			int rx = x + uk_tw (rp);
			char s[64] = "", hm[8];
			unsigned sc = C_ACCENT;
			if (snoozed (a)) { minute_hm (a.snooze, hm, sizeof hm); snprintf (s, sizeof s, TR ("Snoozed until %s"), hm); }
			else if (a.missed >= 0) { minute_hm (a.missed, hm, sizeof hm); snprintf (s, sizeof s, TR ("Missed at %s"), hm); sc = RED (); }
			if (s[0])
			{
				uk_text (canvas, rx, y + 32, DOT, d); rx += uk_tw (DOT);
				uk_text (canvas, rx, y + 32, s, sc, 2);
			}
			uk_switch_mark (canvas, rw - 64, y + (ROW - 3 - 24) / 2, 46, 24, on, UK_NORMAL);
		}
		UkThumb th = uk_thumb (contentH (), height, top, height - 4);
		if (th.show) uk_draw_vscroll (canvas, width - UK_SBW - 2, 2, UK_SBW, height - 4, th, C_FIELD);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		long pos = top;
		if (bar.mouse (mx, my, bl, width - UK_SBW - 2, UK_SBW, 2, height - 4, contentH (), height, &pos)) { top = (int) pos; invalidate (true); return true; }
		if (mx < 0) return false;
		if (wheel) { top -= wheel * ROW; clampTop (); invalidate (true); return true; }
		if (bl && !pressed)
		{
			pressed = true; setFocus ();
			int r = (my + top - 4) / ROW;
			if (my + top < 4 || r >= g_nord) { invalidate (true); return true; }
			unsigned now = kapi_get_ticks ();
			bool dbl = r == lastRow && now - lastClick < 40;
			lastRow = r; lastClick = now;
			selectRow (r);
			int rw = rowW ();
			const Alarm &a = g_al.a[g_ord[r]];
			if (mx >= rw - 70 && mx < rw - 12 && a.valid) { alarm_toggle (a.id); lastRow = -1; }	// (the switch)
			else if (dbl && a.valid) alarm_edit_open (a.id);
		}
		else if (!bl) pressed = false;
		return true;
	}
	bool onKey (long k) override
	{
		if (kapi_get_modifiers () & MOD_CTRL) return false;
		int page = height / ROW > 1 ? height / ROW - 1 : 1, r = sel_row ();
		switch (k)
		{
		case KEY_UP:   selectRow (r - 1); return true;
		case KEY_DOWN: selectRow (r + 1); return true;
		case KEY_HOME: selectRow (0); return true;
		case KEY_END:  selectRow (g_nord - 1); return true;
		case KEY_PGUP: selectRow (r - page); return true;
		case KEY_PGDN: selectRow (r + page); return true;
		}
		return false;
	}
};

// ---- the editor --------------------------------------------------------------------------------------------------------
static void ed_changed (Widget &);
static void ed_ok (Widget &);
static void ed_cancel (Widget &);
static void ed_delete (Widget &);
static void ed_every (Widget &);
static void ed_weekdays (Widget &);
static void ed_test (Widget &);
class EditVeil : public Veil
{
public:
	enum { X0 = 108 };
	int id;					// the alarm edited (0: a new one)
	LcdDisplay *lcd;
	Spin *hh, *mm;
	Textbox *label;
	ToolButton *day[7];
	SegmentedControl *snd;
	Button *del;
	char next[96]; bool noSound;
	EditVeil () : Veil (460, 352, TR ("New Alarm")), id (0), noSound (false)
	{
		int y = th ();
		next[0] = 0;
		lcd = new LcdDisplay (0, 0, 128, 54, "07:00");
		lcd->face = face (34); lcd->centred = true;
		place (lcd, X0, y + 10);
		hh = new Spin (0, 0, 72, 30, 0, 23, 7, ed_changed);
		place (hh, X0 + 140, y + 30);
		mm = new Spin (0, 0, 72, 30, 0, 59, 0, ed_changed);
		place (mm, X0 + 222, y + 30);
		label = new Textbox (0, 0, cw - 20 - X0, 30, "");
		label->maxLen = 160;
		place (label, X0, y + 92);
		for (int i = 0; i < 7; i++)
		{
			day[i] = tool_button (0, WKT_NONE, TR (SHORTDAY[i]), ed_changed, 44, 30);
			day[i]->setToggle (true, false);
			place (day[i], X0 + i * 48, y + 136);
		}
		ToolButton *ev = tool_button (TR ("Every day of the week"), WKT_NONE, TR ("Every day"), ed_every, 0, 26);
		place (ev, X0, y + 172);
		place (tool_button (TR ("Monday to Friday"), WKT_NONE, TR ("Weekdays"), ed_weekdays, 0, 26), X0 + ev->width + 6, y + 172);
		static const char *names[3];
		for (int i = 0; i < 3; i++) names[i] = TR (SOUNDNAME[i]);
		snd = new SegmentedControl (0, 0, 240, 30, names, 3, 0, ed_changed);
		place (snd, X0, y + 214);
		place (tool_button (TR ("Play the sound once"), WKT_PLAY, TR ("Test"), ed_test), X0 + 250, y + 214);
		del = new Button (0, 0, 100, 32, TR ("Delete"), ed_delete);
		place (del, 20, ch - 46);
		place (new Button (0, 0, 104, 32, TR ("Cancel"), ed_cancel), cw - 20 - 96 - 10 - 104, ch - 46);
		place (new Button (0, 0, 96, 32, "OK", ed_ok), cw - 20 - 96, ch - 46);
	}
	unsigned days () const { unsigned m = 0; for (int i = 0; i < 7; i++) if (day[i]->on) m |= 1u << i; return m; }
	// the LCD and the "Next:" line from the choices
	void refresh ()
	{
		char t[8];
		clk_fmt_hm (hh->value, mm->value, t, sizeof t);
		lcd->setText (t);
		Alarm a; alarm_new (g_al, a);
		a.hh = hh->value; a.mm = mm->value; a.days = days ();
		a.date = alarm_once_day (a.hh, a.mm, g_now.minute);
		long at = alarm_next (a, g_now.minute + 1);
		if (at >= 0) { minute_hm (at, t, sizeof t); snprintf (next, sizeof next, TR ("Next: %s %s"), day_word (clk_day_of (at)), t); }
		else next[0] = 0;
		invalidate (true);
	}
	void drawCard () override
	{
		unsigned d = dim_on (C_BG);
		int y = cy + th ();
		const char *capt[4] = { TR ("Time"), TR ("Label"), TR ("Repeat"), TR ("Sound") };
		const int cy4[4] = { 22, 92, 136, 214 };
		for (int i = 0; i < 4; i++) uk_text_l (canvas, cx + 20, y + cy4[i], 30, capt[i], C_TEXT, 2);
		uk_text (canvas, cx + X0 + 140, y + 12, TR ("Hours"), d);
		uk_text (canvas, cx + X0 + 222, y + 12, TR ("Minutes"), d);
		if (noSound) uk_text_l (canvas, cx + X0 + 140, y + 64, 22, TR ("Sound unavailable"), AMBER ());
		else uk_text_l (canvas, cx + X0 + 140, y + 64, 22, next, d);
		canvas.fillRect (cx + 16, cy + ch - 56, cw - 32, 1, uk_tone (C_BG, 104));
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) { ed_ok (*this); return true; }
		if (k == 27) { ed_cancel (*this); return true; }
		return false;
	}
	void onHidden () override;
};

// ---- the tab -----------------------------------------------------------------------------------------------------------
static NextAlarmBar *g_next;
static AlarmList *g_list;
static FootText *g_alFoot;
static ToolButton *g_aNew, *g_aEdit, *g_aDel;
static EditVeil *g_edit;
static bool g_alPrint = true;				// the rows to print (the PC tests read them)

void EditVeil::onHidden () { g_list->setFocus (); }

// The list, the bar, the footer after a change (or a new minute); the rows printed once a change.
static void alarms_changed ()
{
	alarms_sort ();
	if (alarm_find (g_al, g_alSel) < 0) g_alSel = g_nord ? g_al.a[g_ord[0]].id : -1;
	int on = 0;
	for (int i = 0; i < g_al.n; i++) on += shown_on (g_al.a[i]);
	char s[96];
	if (g_al.n == 0) snprintf (s, sizeof s, "%s", TR ("No alarms"));
	else if (g_al.n == 1) snprintf (s, sizeof s, TR ("1 alarm \xC2\xB7 %d on"), on);
	else snprintf (s, sizeof s, TR ("%d alarms \xC2\xB7 %d on"), g_al.n, on);
	g_alFoot->set (s);
	if (g_alSaveFailed) g_alFoot->say (TR ("Not saved: the card is full or read-only"), true);
	else g_alFoot->say ("");
	Alarm *a = sel_alarm ();
	g_aNew->setDisabled (g_al.n >= ALARMS_MAX);
	g_aEdit->setDisabled (!a || !a->valid);
	g_aDel->setDisabled (!a);
	g_list->showSel ();
	g_list->invalidate (true);
	g_next->update ();
	if (!g_alPrint) return;
	g_alPrint = false;
	for (int r = 0; r < g_nord; r++)
	{
		const Alarm &x = g_al.a[g_ord[r]];
		char t[8], rp[96], st[32] = "", hm[8];
		if (!x.valid) { say ("alarm %d invalid", x.id); continue; }
		clk_fmt_hm (x.hh, x.mm, t, sizeof t);
		repeat_text (x.days, rp, sizeof rp);
		if (snoozed (x)) { minute_hm (x.snooze, hm, sizeof hm); snprintf (st, sizeof st, " snoozed %s", hm); }
		else if (x.missed >= 0) { minute_hm (x.missed, hm, sizeof hm); snprintf (st, sizeof st, " missed %s", hm); }
		say ("alarm %d %s %s [%s] %s%s", x.id, t, label_of (x), rp, shown_on (x) ? "on" : "off", st);
	}
}
// written, clockd told, shown again
static void alarms_commit ()
{
	alarms_write ();
	g_alPrint = true;
	alarms_changed ();
}
static void alarm_toggle (int id)
{
	int i = alarm_find (g_al, id);
	if (i < 0 || !g_al.a[i].valid) return;
	Alarm &a = g_al.a[i];
	bool on = !shown_on (a);
	a.on = on; a.missed = -1;
	if (!on) a.snooze = -1;
	else if (!(a.days & AL_EVERYDAY)) a.date = alarm_once_day (a.hh, a.mm, g_now.minute);	// (once: its next time)
	say ("alarm %d turned %s", id, on ? "on" : "off");
	alarms_commit ();
}
static void alarm_delete (int id)
{
	int r = sel_row ();
	if (!alarm_remove (g_al, id)) return;
	say ("alarm %d deleted", id);
	alarms_sort ();
	if (r >= g_nord) r = g_nord - 1;
	g_alSel = r >= 0 ? g_al.a[g_ord[r]].id : -1;
	alarms_commit ();
}

static void alarm_edit_open (int id)
{
	tab_show (TAB_ALARMS);
	EditVeil *v = g_edit;
	Alarm a;
	int i = id ? alarm_find (g_al, id) : -1;
	if (id && (i < 0 || !g_al.a[i].valid)) return;
	if (!id && g_al.n >= ALARMS_MAX) { say ("alarm refused (20 already)"); return; }
	if (i >= 0) a = g_al.a[i]; else alarm_new (g_al, a);
	v->id = i >= 0 ? id : 0;
	snprintf (v->title, sizeof v->title, "%s", v->id ? TR ("Edit Alarm") : TR ("New Alarm"));
	v->hh->setValue (a.hh); v->mm->setValue (a.mm);
	v->label->setText (a.label);
	for (int d = 0; d < 7; d++) v->day[d]->setOn ((a.days >> d) & 1);
	v->snd->select (sound_index (a.sound));
	v->del->hidden = !v->id;
	v->noSound = false;
	v->refresh ();
	v->show (g_root, v->hh);
	if (v->id) say ("editor edit %d", v->id); else say ("editor new");
}
static void ed_changed (Widget &) { g_edit->noSound = false; g_edit->refresh (); }
static void ed_every (Widget &) { for (int d = 0; d < 7; d++) g_edit->day[d]->setOn (true); g_edit->refresh (); }
static void ed_weekdays (Widget &) { for (int d = 0; d < 7; d++) g_edit->day[d]->setOn (d < 5); g_edit->refresh (); }
static void ed_cancel (Widget &) { g_edit->hide (); say ("editor cancelled"); }
static void ed_delete (Widget &)
{
	int id = g_edit->id;
	g_edit->hide ();
	if (id) alarm_delete (id);
}
static void ed_test (Widget &)
{
	int r = sound_test (SOUNDKEY[g_edit->snd->selected < 0 ? 0 : g_edit->snd->selected]);
	g_edit->noSound = r != 1;
	g_edit->invalidate (true);
}
static void ed_ok (Widget &)
{
	EditVeil *v = g_edit;
	v->hh->NumericUpDown::onKey (KEY_ENTER);		// (digits typed, not yet committed: taken)
	v->mm->NumericUpDown::onKey (KEY_ENTER);
	Alarm a;
	int i = v->id ? alarm_find (g_al, v->id) : -1;
	if (i >= 0) a = g_al.a[i]; else alarm_new (g_al, a);
	a.valid = true; a.hh = v->hh->value; a.mm = v->mm->value;
	alarm_set_label (a, v->label->text);
	a.days = v->days ();
	snprintf (a.sound, sizeof a.sound, "%s", SOUNDKEY[v->snd->selected < 0 ? 0 : v->snd->selected]);
	a.date = a.days ? -1 : alarm_once_day (a.hh, a.mm, g_now.minute);
	a.on = true; a.snooze = -1; a.missed = -1;
	if (i >= 0) g_al.a[i] = a;
	else if ((i = alarm_add (g_al, a)) < 0) { say ("alarm refused (20 already)"); v->hide (); return; }
	g_alSel = g_al.a[i].id;
	v->hide ();
	say ("alarm %d saved", g_alSel);
	alarms_commit ();
}

static void tb_new (Widget &) { alarm_edit_open (0); }
static void tb_edit (Widget &) { if (g_alSel >= 0) alarm_edit_open (g_alSel); }
static void tb_delete (Widget &) { if (g_alSel >= 0) alarm_delete (g_alSel); }

static void alarms_build (Page *p)
{
	int w = p->width, h = p->height;
	g_next = new NextAlarmBar (12, 8, w - 24, 40);
	p->addChild (g_next);
	g_list = new AlarmList (12, 56, w - 24, h - FOOT - 56);
	p->addChild (g_list);
	g_alFoot = new FootText (14, h - FOOT + 8, w - 360, 30);
	g_alFoot->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	p->addChild (g_alFoot);
	ToolButton *b[3];
	b[0] = g_aDel = tool_button (TR ("Delete the alarm (Delete)"), WKT_TRASH, TR ("Delete"), tb_delete);
	b[1] = g_aEdit = tool_button (TR ("Edit the alarm (Enter)"), WKT_NONE, TR ("Edit"), tb_edit);
	b[2] = g_aNew = tool_button (TR ("A new alarm (Ctrl+N)"), WKT_PLUS, TR ("New Alarm"), tb_new);
	int x = w - 12;
	for (int i = 0; i < 3; i++)
	{
		x -= b[i]->width;
		b[i]->left = x; b[i]->top = h - FOOT + 8;
		b[i]->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM;
		p->addChild (b[i]);
		x -= 6;
	}
	g_edit = new EditVeil ();
	alarms_changed ();
}
static void alarms_shown ()
{
	g_list->setFocus ();
	alarms_changed ();
}
static void alarms_tick ()
{
	static long s_min = -1;
	if (g_now.minute == s_min) return;			// (a new minute: the next alarm's line, a once alarm gone by)
	s_min = g_now.minute;
	alarms_changed ();
}
static bool alarms_key (long k, bool ctrl)
{
	if (ctrl && k == UK_CTRL ('N')) { alarm_edit_open (0); return true; }
	if (ctrl) return false;
	if (k == KEY_ENTER) { if (g_alSel >= 0) alarm_edit_open (g_alSel); return true; }
	if (k == ' ') { if (g_alSel >= 0) alarm_toggle (g_alSel); return true; }
	if (k == KEY_DEL) { if (g_alSel >= 0) alarm_delete (g_alSel); return true; }
	if (k == KEY_UP || k == KEY_DOWN || k == KEY_HOME || k == KEY_END || k == KEY_PGUP || k == KEY_PGDN) { g_list->setFocus (); return g_list->onKey (k); }
	return false;
}
static void m_al_new () { alarm_edit_open (0); }
static void m_al_edit () { tab_show (TAB_ALARMS); if (g_alSel >= 0) alarm_edit_open (g_alSel); }
static void m_al_toggle () { tab_show (TAB_ALARMS); if (g_alSel >= 0) alarm_toggle (g_alSel); }
static void m_al_delete () { tab_show (TAB_ALARMS); if (g_alSel >= 0) alarm_delete (g_alSel); }
static void alarms_menu (Menu &m)
{
	m.menu (TR ("Alarm"));
	m.item (TR ("New Alarm\xE2\x80\xA6"), "^N", 0, m_al_new);
	m.item (TR ("Edit Alarm\xE2\x80\xA6"), "Enter", 0, m_al_edit);
	m.item (TR ("Turn On / Off"), TR ("Space"), 0, m_al_toggle);
	m.item (TR ("Delete Alarm"), "Del", 0, m_al_delete);
}
