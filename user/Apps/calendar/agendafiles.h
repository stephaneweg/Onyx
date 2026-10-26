//
// calendar/agendafiles.h -- the Calendar's files on the card and the two it keeps for the others, beside
// calendar.ics: agenda.txt (the desktop's agenda widget: the next month's appointments, "YYYYMMDD|[H:MM ]title"
// lines in Latin-1) and reminders.txt (the widget sends them as notifications: "YYYYMMDDHHMM|text"). Shared by the
// Calendar and PocketQI (Pocket writes appointments and tasks through model.h, then these): included after
// model.h and uikit/lang.h. A Calendar that runs reads calendar.ics again when another program changed it
// (cal_file_sig: its onTick).
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
#ifndef _calendar_agendafiles_h
#define _calendar_agendafiles_h

#define CAL_ICS		"SD:/apps/calendar.app/calendar.ics"
#define AGENDA_TXT	"SD:/apps/calendar.app/agenda.txt"
#define REMIND_TXT	"SD:/apps/calendar.app/reminders.txt"

// A whole file, ended by a 0 (new[]; 0: no such file); len: its size
static char *cal_read_file (const char *path, int *len)
{
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned sz = kapi_fsize (f);
	char *b = new char[sz + 1];
	int got = kapi_read (f, b, sz);
	kapi_close (f);
	if (got < 0) got = 0;
	b[got] = '\0';
	if (len) *len = got;
	return b;
}

// calendar.ics' signature (its bytes hashed; 0: no file) -- a change made by another program seen
static unsigned cal_file_sig (void)
{
	int n = 0;
	char *t = cal_read_file (CAL_ICS, &n);
	if (!t) return 0;
	unsigned h = 2166136261u;
	for (int i = 0; i < n; i++) { h ^= (unsigned char) t[i]; h *= 16777619u; }
	delete [] t;
	return h ? h : 1;
}

// What a reminder says: "Dentist at 17:30, Dr. Peeters".
static void cal_reminder_text (const Event &e, int start, char *t, int cap)
{
	t[0] = '\0';
	scat (t, cap, e.title[0] ? e.title : TR ("(No title)"));
	if (e.allDay) scat (t, cap, start / 1440 > (start - e.remind) / 1440 ? TR (" (tomorrow)") : TR (" (today)"));
	else { char hm[8]; fmt_hm (start, hm, sizeof hm); scat (t, cap, TR (" at ")); scat (t, cap, hm); }
	if (e.place[0]) { scat (t, cap, ", "); scat (t, cap, e.place); }
}

// agenda.txt and reminders.txt from the events in memory, today being the day number `today`
static void cal_write_agenda (int today)
{
	static Occ occ[400];
	int n = occurrences (today, today + 31, occ, 400, true);
	int cap = n * 120 + 16, len = 0;
	char *b = new char[cap];
	for (int i = 0; i < n; i++)
	{
		const Event &e = g_ev[occ[i].ev];
		int dn = occ[i].start / 1440; if (dn < today) dn = today;
		char l[140] = "";
		scatn (l, sizeof l, dn_y (dn)); scat2 (l, sizeof l, dn_m (dn)); scat2 (l, sizeof l, dn_d (dn)); scat (l, sizeof l, "|");
		if (!e.allDay && occ[i].end - occ[i].start < 1440) { char hm[8]; fmt_hm (occ[i].start, hm, sizeof hm); scat (l, sizeof l, hm); scat (l, sizeof l, " "); }
		char ti[100]; to_latin1 (e.title, ti, sizeof ti);
		scat (l, sizeof l, ti); scat (l, sizeof l, "\n");
		for (int k = 0; l[k] && len < cap - 1; k++) b[len++] = l[k];
	}
	kapi_save_file (AGENDA_TXT, b, (unsigned) len);
	delete [] b;

	// reminders.txt: the reminders of the next month, "YYYYMMDDHHMM|text" in order -- the agenda
	// widget (always running) sends them as notifications, the calendar open or not.
	n = occurrences (today, today + 31, occ, 400, true);
	struct R { int at, k; } r[400]; int nr = 0;
	for (int i = 0; i < n; i++)
	{
		const Event &e = g_ev[occ[i].ev];
		if (e.remind < 0) continue;
		r[nr].at = occ[i].start - e.remind; r[nr].k = i; nr++;
	}
	for (int i = 1; i < nr; i++) { R t = r[i]; int j = i; while (j > 0 && r[j - 1].at > t.at) { r[j] = r[j - 1]; j--; } r[j] = t; }
	cap = nr * 200 + 16; len = 0;
	b = new char[cap];
	for (int i = 0; i < nr; i++)
	{
		const Occ &o = occ[r[i].k]; const Event &e = g_ev[o.ev];
		int at = r[i].at;
		char l[240] = "";
		scatn (l, sizeof l, dn_y (at / 1440)); scat2 (l, sizeof l, dn_m (at / 1440)); scat2 (l, sizeof l, dn_d (at / 1440));
		scat2 (l, sizeof l, at % 1440 / 60); scat2 (l, sizeof l, at % 60); scat (l, sizeof l, "|");
		char rt[200], r1[200]; cal_reminder_text (e, o.start, rt, sizeof rt); to_latin1 (rt, r1, sizeof r1);
		scat (l, sizeof l, r1);
		scat (l, sizeof l, "\n");
		for (int k = 0; l[k] && len < cap - 1; k++) b[len++] = l[k];
	}
	kapi_save_file (REMIND_TXT, b, (unsigned) len);
	delete [] b;
}

#endif
