//
// calendar/model.h -- the planner's data: events (timed or all-day, a category, a place, notes, a
// repetition, a reminder), tasks (a due date, done or not) and the categories (a name, a colour,
// shown or hidden). Kept as iCalendar (RFC 5545) in SD:/apps/calendar.app/calendar.ics -- the
// format Google Calendar and Outlook export -- so importing and exporting are the same code:
// VEVENT (SUMMARY, LOCATION, DESCRIPTION, DTSTART / DTEND, RRULE, EXDATE, CATEGORIES, VALARM),
// VTODO (SUMMARY, DUE, STATUS, CATEGORIES); the categories' colours in X-ONYX-CATEGORY lines.
//
// Times are local, in minutes: day number * 1440 + minutes into the day (day 0 = 1970-01-01). A
// time in UTC (a "Z" -- Google's) is brought to local time by config.ini's utc_offset (minutes)
// and eu_dst (the European summer time).
//
#ifndef _calendar_model_h
#define _calendar_model_h

// ---- strings (freestanding: no libc) ---------------------------------------------------------------

static int  slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scat (char *d, int cap, const char *s) { int n = slen (d), i = 0; while (s[i] && n + 1 < cap) d[n++] = s[i++]; d[n] = '\0'; }
static void scpy (char *d, int cap, const char *s) { d[0] = '\0'; scat (d, cap, s); }
static void scatn (char *d, int cap, int v) { char b[16]; int n = 0; if (v < 0) { b[n++] = '-'; v = -v; } char t[12]; int k = 0; do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v); while (k) b[n++] = t[--k]; b[n] = '\0'; scat (d, cap, b); }
static void scat2 (char *d, int cap, int v) { char b[3] = { (char) ('0' + v / 10 % 10), (char) ('0' + v % 10), 0 }; scat (d, cap, b); }
static char lc (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
static bool ieq (const char *a, const char *b) { while (*a && lc (*a) == lc (*b)) { a++; b++; } return !*a && !*b; }
static bool starts (const char *s, const char *p) { while (*p) if (*s++ != *p++) return false; return true; }
static int  atoi_n (const char *s, int n) { int v = 0; for (int i = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0'); return v; }

// ---- dates -------------------------------------------------------------------------------------------

static int days_from_civil (int y, int m, int d)		// (Howard Hinnant's)
{
	y -= m <= 2;
	int era = (y >= 0 ? y : y - 399) / 400;
	int yoe = y - era * 400;
	int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}
static void civil_from_days (int z, int &y, int &m, int &d)
{
	z += 719468;
	int era = (z >= 0 ? z : z - 146096) / 146097;
	int doe = z - era * 146097;
	int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int mp = (5 * doy + 2) / 153;
	d = doy - (153 * mp + 2) / 5 + 1;
	m = mp + (mp < 10 ? 3 : -9);
	y += m <= 2;
}
static int wday (int dn) { int w = (dn + 3) % 7; return w < 0 ? w + 7 : w; }		// 0 = Monday
static int days_in (int y, int m) { static const int D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : D[m - 1]; }
static int dn_y (int dn) { int y, m, d; civil_from_days (dn, y, m, d); return y; }
static int dn_m (int dn) { int y, m, d; civil_from_days (dn, y, m, d); return m; }
static int dn_d (int dn) { int y, m, d; civil_from_days (dn, y, m, d); return d; }
static int add_months (int dn, int k)				// the same day k months on (clamped to the month)
{
	int y, m, d; civil_from_days (dn, y, m, d);
	int t = y * 12 + (m - 1) + k; y = t / 12; m = t % 12 + 1;
	if (d > days_in (y, m)) d = days_in (y, m);
	return days_from_civil (y, m, d);
}

static const char *const MONTH[12] = { "January", "February", "March", "April", "May", "June", "July",
				       "August", "September", "October", "November", "December" };
static const char *const MON3[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *const WDAY[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
static const char *const WD3[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static const char *const WD2[7] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };	// (iCalendar's)

static void fmt_hm (int mins, char *o, int cap)			// "9:05"
{
	o[0] = '\0'; mins %= 1440; if (mins < 0) mins += 1440;
	scatn (o, cap, mins / 60); scat (o, cap, ":"); scat2 (o, cap, mins % 60);
}

// ---- the data ----------------------------------------------------------------------------------------

enum { RP_NONE, RP_DAILY, RP_WEEKLY, RP_MONTHLY, RP_YEARLY };
enum { MAXCAT = 12, MAXEX = 16 };

struct Category { char name[32]; unsigned color; bool shown; };
struct Event
{
	char title[96], place[96], notes[600], uid[64];
	int  start, end;			// minutes (all-day: whole days, end the next midnight)
	bool allDay;
	int  cat;				// category (-1: none)
	int  freq, interval, wmask;		// repetition: RP_*, every `interval`, weekly: the days (bit 0 Monday)
	int  until, count;			// the last day of the repetition (0: none), how many times (0: no limit)
	int  exdate[MAXEX], nex;		// days skipped
	int  remind;				// minutes before (-1: no reminder)
};
struct Task { char title[128], uid[64]; int due; bool done; int cat; };	// due: a day number (0: none)

static Category g_cat[MAXCAT];
static int      g_ncat = 0;
static Event   *g_ev = 0;
static int      g_nev = 0, g_capev = 0;
static Task    *g_tk = 0;
static int      g_ntk = 0, g_captk = 0;
static int      g_utcOffset = 60;		// minutes east of UTC (config.ini)
static bool     g_euDst = true;
static unsigned g_uidSeq = 0;

static void new_uid (char *o, int cap)
{
	o[0] = '\0'; scat (o, cap, "onyx-"); scatn (o, cap, (int) (kapi_get_ticks () & 0x7FFFFFFF));
	scat (o, cap, "-"); scatn (o, cap, (int) ++g_uidSeq); scat (o, cap, "@onyx");
}
static void ev_init (Event &e)
{
	e.title[0] = e.place[0] = e.notes[0] = '\0'; new_uid (e.uid, sizeof e.uid);
	e.start = e.end = 0; e.allDay = false; e.cat = -1;
	e.freq = RP_NONE; e.interval = 1; e.wmask = 0; e.until = 0; e.count = 0; e.nex = 0; e.remind = -1;
}
static Event &ev_new ()
{
	if (g_nev == g_capev)
	{
		int nc = g_capev ? g_capev * 2 : 64;
		Event *n = new Event[nc];
		for (int i = 0; i < g_nev; i++) n[i] = g_ev[i];
		delete [] g_ev; g_ev = n; g_capev = nc;
	}
	Event &e = g_ev[g_nev++];
	ev_init (e);
	return e;
}
static void ev_delete (int i) { if (i < 0 || i >= g_nev) return; for (int k = i; k + 1 < g_nev; k++) g_ev[k] = g_ev[k + 1]; g_nev--; }
static Task &tk_new ()
{
	if (g_ntk == g_captk)
	{
		int nc = g_captk ? g_captk * 2 : 32;
		Task *n = new Task[nc];
		for (int i = 0; i < g_ntk; i++) n[i] = g_tk[i];
		delete [] g_tk; g_tk = n; g_captk = nc;
	}
	Task &t = g_tk[g_ntk++];
	t.title[0] = '\0'; new_uid (t.uid, sizeof t.uid); t.due = 0; t.done = false; t.cat = -1;
	return t;
}
static void tk_delete (int i) { if (i < 0 || i >= g_ntk) return; for (int k = i; k + 1 < g_ntk; k++) g_tk[k] = g_tk[k + 1]; g_ntk--; }
static int cat_find (const char *name) { for (int i = 0; i < g_ncat; i++) if (ieq (g_cat[i].name, name)) return i; return -1; }
static int cat_add (const char *name, unsigned color)
{
	int i = cat_find (name);
	if (i >= 0 || g_ncat >= MAXCAT) return i;
	scpy (g_cat[g_ncat].name, sizeof g_cat[0].name, name);
	g_cat[g_ncat].color = color; g_cat[g_ncat].shown = true;
	return g_ncat++;
}
static unsigned cat_color (int c) { return c >= 0 && c < g_ncat ? g_cat[c].color : 0x00788A99; }
static bool cat_shown (int c) { return c < 0 || c >= g_ncat || g_cat[c].shown; }

// ---- repetition ------------------------------------------------------------------------------------

static bool excluded (const Event &e, int dn) { for (int i = 0; i < e.nex; i++) if (e.exdate[i] == dn) return true; return false; }

// Does the rule put an occurrence on day dn (not counting COUNT and EXDATE)?
static bool rule_hits (const Event &e, int dn)
{
	int sd = e.start / 1440;
	if (dn < sd) return false;
	int iv = e.interval < 1 ? 1 : e.interval;
	switch (e.freq)
	{
	case RP_DAILY:  return (dn - sd) % iv == 0;
	case RP_WEEKLY:
	{
		int mask = e.wmask ? e.wmask : 1 << wday (sd);
		if (!(mask & (1 << wday (dn)))) return false;
		int w0 = (sd - wday (sd)) / 7, w1 = (dn - wday (dn)) / 7;
		return (w1 - w0) % iv == 0;
	}
	case RP_MONTHLY:
	{
		int y0, m0, d0, y, m, d; civil_from_days (sd, y0, m0, d0); civil_from_days (dn, y, m, d);
		int months = (y - y0) * 12 + (m - m0);
		if (months % iv) return false;
		return d == d0 || (d0 > days_in (y, m) && d == days_in (y, m));
	}
	case RP_YEARLY:
	{
		int y0, m0, d0, y, m, d; civil_from_days (sd, y0, m0, d0); civil_from_days (dn, y, m, d);
		return m == m0 && (d == d0 || (m == 2 && d0 == 29 && d == 28 && days_in (y, 2) == 28)) && (y - y0) % iv == 0;
	}
	}
	return dn == sd;
}
static bool occurs_on (const Event &e, int dn)
{
	if (e.freq == RP_NONE) return dn == e.start / 1440;
	if (e.until && dn > e.until) return false;
	if (!rule_hits (e, dn) || excluded (e, dn)) return false;
	if (e.count > 0)						// the n-th one at most
	{
		int n = 0;
		for (int k = e.start / 1440; k <= dn && n <= e.count; k++) if (rule_hits (e, k)) n++;
		if (n > e.count) return false;
	}
	return true;
}

// An event's occurrences that touch the days [d0, d1).
struct Occ { int ev, start, end; };
static int occurrences (int d0, int d1, Occ *out, int max, bool all = false)
{
	int n = 0;
	for (int i = 0; i < g_nev && n < max; i++)
	{
		const Event &e = g_ev[i];
		if (!all && !cat_shown (e.cat)) continue;
		int dur = e.end - e.start; if (dur < 0) dur = 0;
		int span = (dur + 1439) / 1440;
		if (e.freq == RP_NONE)
		{
			if (e.start < d1 * 1440 && (e.end > d0 * 1440 || (dur == 0 && e.start >= d0 * 1440)))
				{ out[n].ev = i; out[n].start = e.start; out[n].end = e.end; n++; }
			continue;
		}
		int from = d0 - span; if (from < e.start / 1440) from = e.start / 1440;
		for (int dn = from; dn < d1 && n < max; dn++)
		{
			if (!occurs_on (e, dn)) continue;
			int s = dn * 1440 + (e.start - (e.start / 1440) * 1440), en = s + dur;
			if (s < d1 * 1440 && (en > d0 * 1440 || (dur == 0 && s >= d0 * 1440)))
				{ out[n].ev = i; out[n].start = s; out[n].end = en; n++; }
		}
	}
	for (int i = 1; i < n; i++)				// by start, all-day first
	{
		Occ t = out[i]; int j = i;
		while (j > 0 && (out[j - 1].start > t.start || (out[j - 1].start == t.start && !g_ev[out[j - 1].ev].allDay && g_ev[t.ev].allDay)))
			{ out[j] = out[j - 1]; j--; }
		out[j] = t;
	}
	return n;
}

// ---- iCalendar -----------------------------------------------------------------------------------------

// European summer time: from the last Sunday of March to the last Sunday of October.
static bool eu_summer (int dn)
{
	int y = dn_y (dn);
	int mar = days_from_civil (y, 3, 31); mar -= wday (mar) == 6 ? 0 : wday (mar) + 1;
	int oct = days_from_civil (y, 10, 31); oct -= wday (oct) == 6 ? 0 : wday (oct) + 1;
	return dn >= mar && dn < oct;
}
static int utc_to_local (int t) { return t + g_utcOffset + (g_euDst && eu_summer (t / 1440) ? 60 : 0); }

// "20260928" or "20260928T093000" (+ "Z": UTC) -> minutes; *allDay: a date alone.
static int ics_time (const char *v, bool *allDay)
{
	int y = atoi_n (v, 4), m = atoi_n (v + 4, 2), d = atoi_n (v + 6, 2);
	if (m < 1 || m > 12 || d < 1 || d > 31) { if (allDay) *allDay = true; return 0; }
	int t = days_from_civil (y, m, d) * 1440;
	if (v[8] != 'T') { if (allDay) *allDay = true; return t; }
	if (allDay) *allDay = false;
	t += atoi_n (v + 9, 2) * 60 + atoi_n (v + 11, 2);
	if (v[15] == 'Z') t = utc_to_local (t);
	return t;
}
static void ics_unescape (const char *s, char *o, int cap)
{
	int n = 0;
	for (; *s && n + 1 < cap; s++)
	{
		if (*s == '\\' && s[1]) { s++; o[n++] = (*s == 'n' || *s == 'N') ? '\n' : *s; }
		else o[n++] = *s;
	}
	o[n] = '\0';
}
// UTF-8 (the file) <-> Latin-1 (the screen's font).
static void from_utf8 (const char *in, char *out, int cap)
{
	int o = 0;
	const unsigned char *s = (const unsigned char *) in;
	while (*s && o + 1 < cap)
	{
		unsigned c = *s;
		if (c < 0x80) { out[o++] = (char) c; s++; continue; }
		int n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		bool ok = n > 0;
		for (int k = 1; ok && k <= n; k++) if ((s[k] & 0xC0) != 0x80) ok = false;
		if (!ok) { out[o++] = (char) c; s++; continue; }
		unsigned cp = c & (n == 1 ? 0x1F : n == 2 ? 0x0F : 0x07);
		for (int k = 1; k <= n; k++) cp = (cp << 6) | (s[k] & 0x3F);
		out[o++] = cp == 0x2019 ? '\'' : cp == 0x2013 || cp == 0x2014 ? '-' : cp <= 0xFF ? (char) cp : '?';
		s += n + 1;
	}
	out[o] = '\0';
}

static void parse_rrule (Event &e, const char *v)
{
	e.freq = RP_NONE; e.interval = 1; e.wmask = 0; e.until = 0; e.count = 0;
	while (*v)
	{
		char k[16], val[64]; int i = 0;
		while (*v && *v != '=' && *v != ';' && i < 15) k[i++] = *v++;
		k[i] = '\0'; i = 0;
		if (*v == '=') v++;
		while (*v && *v != ';' && i < 63) val[i++] = *v++;
		val[i] = '\0';
		if (*v == ';') v++;
		if (ieq (k, "FREQ")) e.freq = ieq (val, "DAILY") ? RP_DAILY : ieq (val, "WEEKLY") ? RP_WEEKLY : ieq (val, "MONTHLY") ? RP_MONTHLY : ieq (val, "YEARLY") ? RP_YEARLY : RP_NONE;
		else if (ieq (k, "INTERVAL")) { e.interval = atoi_n (val, 4); if (e.interval < 1) e.interval = 1; }
		else if (ieq (k, "COUNT")) e.count = atoi_n (val, 5);
		else if (ieq (k, "UNTIL")) { bool ad; e.until = ics_time (val, &ad) / 1440; }
		else if (ieq (k, "BYDAY"))
			for (const char *p = val; *p; )
			{
				while (*p == '+' || *p == '-' || (*p >= '0' && *p <= '9')) p++;
				for (int d = 0; d < 7; d++) if (p[0] == WD2[d][0] && p[1] == WD2[d][1]) e.wmask |= 1 << d;
				while (*p && *p != ',') p++;
				if (*p == ',') p++;
			}
	}
}

static unsigned hexcol (const char *s)
{
	unsigned v = 0;
	if (*s == '#') s++;
	for (int i = 0; i < 6 && s[i]; i++)
	{
		char c = s[i];
		v = v * 16 + (unsigned) (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
	}
	return v;
}

// Read an iCalendar text: its events, tasks and categories added (import: the ones whose UID we
// already have are skipped). Returns how many things were added.
static int ics_read (char *text, bool import)
{
	int added = 0;
	int kind = 0;					// 0 none, 1 VEVENT, 2 VTODO, 3 VALARM (in an event)
	Event *e = 0; Task *t = 0;
	int evIndex = -1;
	// Unfold (a line starting with a space or a tab continues the one before) as we go.
	char *p = text;
	static char line[4096];
	while (*p)
	{
		int n = 0;
		for (;;)
		{
			while (*p && *p != '\n' && *p != '\r') { if (n < (int) sizeof line - 1) line[n++] = *p; p++; }
			if (*p == '\r') p++;
			if (*p == '\n') p++;
			if (*p == ' ' || *p == '\t') { p++; continue; }
			break;
		}
		line[n] = '\0';
		if (!n) continue;
		// NAME[;PARAMS]:VALUE
		char name[40]; int i = 0;
		while (line[i] && line[i] != ';' && line[i] != ':' && i < 39) { name[i] = line[i]; i++; }
		name[i] = '\0';
		const char *params = line + i, *val = line + i;
		bool q = false;
		while (*val && (q || *val != ':')) { if (*val == '"') q = !q; val++; }
		if (*val == ':') val++;
		bool isDate = false;
		for (const char *s = params; s < val; s++) if (starts (s, "VALUE=DATE") && !starts (s, "VALUE=DATE-")) isDate = true;

		if (ieq (name, "BEGIN"))
		{
			if (ieq (val, "VEVENT")) { kind = 1; e = &ev_new (); evIndex = g_nev - 1; e->uid[0] = '\0'; }
			else if (ieq (val, "VTODO")) { kind = 2; t = &tk_new (); t->uid[0] = '\0'; }
			else if (ieq (val, "VALARM") && kind == 1) kind = 3;
			continue;
		}
		if (ieq (name, "END"))
		{
			if (ieq (val, "VALARM")) { kind = 1; continue; }
			if (kind == 1 && e)
			{
				bool dup = false;
				if (import) for (int k = 0; k < g_nev - 1; k++) if (e->uid[0] && ieq (g_ev[k].uid, e->uid)) dup = true;
				if (!e->uid[0]) new_uid (e->uid, sizeof e->uid);
				if (e->end <= e->start) e->end = e->allDay ? e->start + 1440 : e->start + 60;
				if (dup) ev_delete (evIndex); else added++;
			}
			if (kind == 2 && t)
			{
				bool dup = false;
				if (import) for (int k = 0; k < g_ntk - 1; k++) if (t->uid[0] && ieq (g_tk[k].uid, t->uid)) dup = true;
				if (!t->uid[0]) new_uid (t->uid, sizeof t->uid);
				if (dup || !t->title[0]) tk_delete (g_ntk - 1); else added++;
			}
			kind = 0; e = 0; t = 0;
			continue;
		}
		if (ieq (name, "X-ONYX-CATEGORY") && kind == 0)	// "name;RRGGBB;1"
		{
			char nm[32]; int k = 0; const char *s = val;
			while (*s && *s != ';' && k < 31) nm[k++] = *s++;
			nm[k] = '\0';
			unsigned col = 0x00788A99; bool shown = true;
			if (*s == ';') { s++; col = hexcol (s); while (*s && *s != ';') s++; }
			if (*s == ';') shown = s[1] != '0';
			int c = cat_add (nm, col);
			if (c >= 0 && !import) { g_cat[c].color = col; g_cat[c].shown = shown; }
			continue;
		}
		char tmp[1200]; ics_unescape (val, tmp, sizeof tmp);
		char v[1200]; from_utf8 (tmp, v, sizeof v);
		if (kind == 1 && e)
		{
			if (ieq (name, "SUMMARY")) scpy (e->title, sizeof e->title, v);
			else if (ieq (name, "LOCATION")) scpy (e->place, sizeof e->place, v);
			else if (ieq (name, "DESCRIPTION")) scpy (e->notes, sizeof e->notes, v);
			else if (ieq (name, "UID")) scpy (e->uid, sizeof e->uid, v);
			else if (ieq (name, "DTSTART")) { bool ad = isDate; e->start = ics_time (val, &ad); e->allDay = ad || isDate; }
			else if (ieq (name, "DTEND")) { bool ad; e->end = ics_time (val, &ad); }
			else if (ieq (name, "DURATION"))		// "PT1H30M", "P1D"
			{
				int mins = 0, num = 0;
				for (const char *s = val; *s; s++)
				{
					if (*s >= '0' && *s <= '9') { num = num * 10 + (*s - '0'); continue; }
					if (*s == 'W') mins += num * 7 * 1440; else if (*s == 'D') mins += num * 1440;
					else if (*s == 'H') mins += num * 60; else if (*s == 'M') mins += num;
					num = 0;
				}
				e->end = e->start + mins;
			}
			else if (ieq (name, "RRULE")) parse_rrule (*e, val);
			else if (ieq (name, "EXDATE"))
				for (const char *s = val; *s && e->nex < MAXEX; )
				{
					bool ad; e->exdate[e->nex++] = ics_time (s, &ad) / 1440;
					while (*s && *s != ',') s++;
					if (*s == ',') s++;
				}
			else if (ieq (name, "CATEGORIES"))
			{
				char nm[32]; int k = 0; for (const char *s = v; *s && *s != ',' && k < 31; s++) nm[k++] = *s; nm[k] = '\0';
				int c = cat_find (nm);
				if (c < 0 && nm[0]) { static const unsigned NEWC[] = { 0x003B82F6, 0x0010B981, 0x00F59E0B, 0x00EF4444, 0x008B5CF6, 0x00EC4899 }; c = cat_add (nm, NEWC[g_ncat % 6]); }
				e->cat = c;
			}
		}
		else if (kind == 3 && e && ieq (name, "TRIGGER"))	// "-PT15M", "-P1D", "PT0S"
		{
			int mins = 0, num = 0;
			for (const char *s = val; *s; s++)
			{
				if (*s >= '0' && *s <= '9') { num = num * 10 + (*s - '0'); continue; }
				if (*s == 'W') mins += num * 7 * 1440; else if (*s == 'D') mins += num * 1440;
				else if (*s == 'H') mins += num * 60; else if (*s == 'M') mins += num;
				num = 0;
			}
			e->remind = mins;
		}
		else if (kind == 2 && t)
		{
			if (ieq (name, "SUMMARY")) scpy (t->title, sizeof t->title, v);
			else if (ieq (name, "UID")) scpy (t->uid, sizeof t->uid, v);
			else if (ieq (name, "DUE")) { bool ad; t->due = ics_time (val, &ad) / 1440; }
			else if (ieq (name, "STATUS")) t->done = ieq (val, "COMPLETED");
			else if (ieq (name, "COMPLETED")) t->done = true;
			else if (ieq (name, "CATEGORIES")) t->cat = cat_find (v);
		}
	}
	if (kind == 1 && e) ev_delete (evIndex);		// (an unfinished event: dropped)
	if (kind == 2 && t) tk_delete (g_ntk - 1);
	return added;
}

// ---- writing ---------------------------------------------------------------------------------------------

struct Out
{
	char *b; int n, cap;
	void grow (int need) { if (n + need < cap) return; int nc = (cap + need) * 2; char *x = new char[nc]; for (int i = 0; i < n; i++) x[i] = b[i]; delete [] b; b = x; cap = nc; }
	void raw (const char *s) { int l = slen (s); grow (l + 4); for (int i = 0; i < l; i++) b[n++] = s[i]; }
	// A content line: escaped, Latin-1 -> UTF-8, folded at 74 bytes.
	void prop (const char *name, const char *value, bool escape = true)
	{
		char t[2600]; int k = 0;
		for (int i = 0; name[i]; i++) t[k++] = name[i];
		t[k++] = ':';
		for (const unsigned char *s = (const unsigned char *) value; *s && k < (int) sizeof t - 4; s++)
		{
			if (escape && (*s == '\\' || *s == ';' || *s == ',')) { t[k++] = '\\'; t[k++] = (char) *s; }
			else if (escape && *s == '\n') { t[k++] = '\\'; t[k++] = 'n'; }
			else if (*s == '\r') continue;
			else if (*s >= 0x80) { t[k++] = (char) (0xC0 | (*s >> 6)); t[k++] = (char) (0x80 | (*s & 0x3F)); }
			else t[k++] = (char) *s;
		}
		grow (k + k / 70 * 3 + 4);
		int col = 0;
		for (int i = 0; i < k; i++)
		{
			if (col >= 74 && ((unsigned char) t[i] & 0xC0) != 0x80) { b[n++] = '\r'; b[n++] = '\n'; b[n++] = ' '; col = 1; }
			b[n++] = t[i]; col++;
		}
		b[n++] = '\r'; b[n++] = '\n';
	}
};
static void ics_date (int t, bool dateOnly, char *o)		// "20260928" / "20260928T093000"
{
	int y, m, d; civil_from_days (t / 1440, y, m, d);
	o[0] = '\0'; scatn (o, 16, y); scat2 (o, 16, m); scat2 (o, 16, d);
	if (!dateOnly) { int mm = t % 1440; scat (o, 20, "T"); scat2 (o, 20, mm / 60); scat2 (o, 20, mm % 60); scat (o, 20, "00"); }
}
static const char *HEX = "0123456789ABCDEF";

// The whole calendar (onlyCats: the categories to write, a bit each; -1 all) as iCalendar text.
static char *ics_write (int *len, unsigned onlyCats = 0xFFFFFFFFu)
{
	Out o = { new char[8192], 0, 8192 };
	o.raw ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Onyx//Calendar 2.0//EN\r\nCALSCALE:GREGORIAN\r\n");
	for (int c = 0; c < g_ncat; c++)
	{
		char v[64]; scpy (v, sizeof v, g_cat[c].name); scat (v, sizeof v, ";");
		char h[8]; for (int k = 0; k < 6; k++) h[k] = HEX[(g_cat[c].color >> (20 - 4 * k)) & 15]; h[6] = '\0';
		scat (v, sizeof v, h); scat (v, sizeof v, g_cat[c].shown ? ";1" : ";0");
		o.prop ("X-ONYX-CATEGORY", v, false);
	}
	char b[64];
	for (int i = 0; i < g_nev; i++)
	{
		const Event &e = g_ev[i];
		if (e.cat >= 0 && !(onlyCats & (1u << e.cat))) continue;
		o.raw ("BEGIN:VEVENT\r\n");
		o.prop ("UID", e.uid);
		o.prop ("SUMMARY", e.title);
		ics_date (e.start, e.allDay, b); o.prop (e.allDay ? "DTSTART;VALUE=DATE" : "DTSTART", b, false);
		ics_date (e.end, e.allDay, b);   o.prop (e.allDay ? "DTEND;VALUE=DATE" : "DTEND", b, false);
		if (e.place[0]) o.prop ("LOCATION", e.place);
		if (e.notes[0]) o.prop ("DESCRIPTION", e.notes);
		if (e.cat >= 0 && e.cat < g_ncat) o.prop ("CATEGORIES", g_cat[e.cat].name);
		if (e.freq != RP_NONE)
		{
			static const char *const F[] = { "", "DAILY", "WEEKLY", "MONTHLY", "YEARLY" };
			char r[160] = "FREQ="; scat (r, sizeof r, F[e.freq]);
			if (e.interval > 1) { scat (r, sizeof r, ";INTERVAL="); scatn (r, sizeof r, e.interval); }
			if (e.freq == RP_WEEKLY && e.wmask)
			{
				scat (r, sizeof r, ";BYDAY="); bool f = true;
				for (int k = 0; k < 7; k++) if (e.wmask & (1 << k)) { if (!f) scat (r, sizeof r, ","); scat (r, sizeof r, WD2[k]); f = false; }
			}
			if (e.until) { ics_date (e.until * 1440, true, b); scat (r, sizeof r, ";UNTIL="); scat (r, sizeof r, b); }
			else if (e.count) { scat (r, sizeof r, ";COUNT="); scatn (r, sizeof r, e.count); }
			o.prop ("RRULE", r, false);
			for (int k = 0; k < e.nex; k++) { ics_date (e.exdate[k] * 1440 + (e.allDay ? 0 : e.start % 1440), e.allDay, b); o.prop (e.allDay ? "EXDATE;VALUE=DATE" : "EXDATE", b, false); }
		}
		if (e.remind >= 0)
		{
			o.raw ("BEGIN:VALARM\r\nACTION:DISPLAY\r\n");
			o.prop ("DESCRIPTION", e.title);
			char tr[32] = "-PT"; scatn (tr, sizeof tr, e.remind); scat (tr, sizeof tr, "M");
			o.prop ("TRIGGER", tr, false);
			o.raw ("END:VALARM\r\n");
		}
		o.raw ("END:VEVENT\r\n");
	}
	for (int i = 0; i < g_ntk; i++)
	{
		const Task &t = g_tk[i];
		if (t.cat >= 0 && !(onlyCats & (1u << t.cat))) continue;
		o.raw ("BEGIN:VTODO\r\n");
		o.prop ("UID", t.uid);
		o.prop ("SUMMARY", t.title);
		if (t.due) { ics_date (t.due * 1440, true, b); o.prop ("DUE;VALUE=DATE", b, false); }
		o.prop ("STATUS", t.done ? "COMPLETED" : "NEEDS-ACTION", false);
		if (t.cat >= 0 && t.cat < g_ncat) o.prop ("CATEGORIES", g_cat[t.cat].name);
		o.raw ("END:VTODO\r\n");
	}
	o.raw ("END:VCALENDAR\r\n");
	*len = o.n;
	return o.b;
}

#endif
