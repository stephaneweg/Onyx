//
// today.h -- what the launcher's Today line and Today column tell (docs/COMPACT-SHELL-STUDY.md section 6.2): the
// agenda -- the Calendar's next appointments, SD:/apps/calendar.app/agenda.txt ("YYYYMMDD|[HH:MM ]what" lines: the
// month ahead, which the Calendar writes for the desktop's Agenda widget), read again when it changes -- and the
// date's words. The notifications are the shell's own (it serves "notify": main.cpp's g_notes). No agenda (no
// Calendar, nothing planned): the line is not drawn, the column says so. Part of main.cpp (one unit).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//

#define AGENDA_FILE	"SD:/apps/calendar.app/agenda.txt"
#define MAXAG		40

struct AgEv { int date; int hm; char what[96]; };	// date YYYYMMDD, hm minutes into the day (-1: the whole day)
static AgEv g_ag[MAXAG];
static int g_nag;
static unsigned g_agSig = 1;				// (the file's signature: read again when it changes)

static const char *const WDS[] = { TRN ("Sun"), TRN ("Mon"), TRN ("Tue"), TRN ("Wed"), TRN ("Thu"), TRN ("Fri"), TRN ("Sat") };
static const char *const MOS[] = { TRN ("Jan"), TRN ("Feb"), TRN ("Mar"), TRN ("Apr"), TRN ("May"), TRN ("Jun"), TRN ("Jul"),
				   TRN ("Aug"), TRN ("Sep"), TRN ("Oct"), TRN ("Nov"), TRN ("Dec") };

static int weekday (int y, int m, int d)		// 0 Sunday
{
	int a = (14 - m) / 12, yr = y - a, mm = m + 12 * a - 2;
	return (d + yr + yr / 4 - yr / 100 + yr / 400 + 31 * mm / 12) % 7;
}
static int day_number (int ymd)				// days since 1970-01-01 (Howard Hinnant's days_from_civil)
{
	int y = ymd / 10000, m = ymd / 100 % 100, d = ymd % 100;
	y -= m <= 2;
	int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
	int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
static int today_ymd (int *hm = 0)
{
	int y = 2026, mo = 1, d = 1, h = 0, mi = 0;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, 0);
	if (hm) *hm = h * 60 + mi;
	return y * 10000 + mo * 100 + d;
}
static void num_cat (char *o, int cap, int *k, int v)
{
	char d[12]; int i = 0;
	do { d[i++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (i) { char c[2] = { d[--i], 0 }; lx_cat (o, cap, k, c); }
}
// "Thu 8 Oct" (French: "jeu. 8 oct.").
static void short_date (char *o, int cap, int ymd)
{
	int k = 0; o[0] = 0;
	int y = ymd / 10000, m = ymd / 100 % 100, d = ymd % 100;
	lx_cat (o, cap, &k, TR (WDS[weekday (y, m, d)])); lx_cat (o, cap, &k, " ");
	num_cat (o, cap, &k, d); lx_cat (o, cap, &k, " ");
	lx_cat (o, cap, &k, TR (MOS[(m + 11) % 12]));
}
static void hm_text (char *o, int hm) { o[0] = (char) ('0' + hm / 600); o[1] = (char) ('0' + hm / 60 % 10); o[2] = ':'; o[3] = (char) ('0' + hm % 60 / 10); o[4] = (char) ('0' + hm % 10); o[5] = 0; }

// agenda.txt read again when it changed (or the day did) -> true: changed.
static bool agenda_load (void)
{
	static char b[8192];
	int n = -1;
	void *h = kapi_open (AGENDA_FILE);
	if (h != 0) { n = kapi_read (h, b, sizeof b - 1); kapi_close (h); }
	if (n < 0) n = 0;
	b[n] = 0;
	int today = today_ymd ();
	unsigned sig = (unsigned) n * 2654435761u ^ (unsigned) today;
	for (int i = 0; i < n; i++) sig = sig * 31 + (unsigned char) b[i];
	if (sig == g_agSig) return false;
	g_agSig = sig;
	g_nag = 0;
	for (int i = 0; i < n && g_nag < MAXAG; )
	{
		int s = i;
		while (i < n && b[i] != '\n') i++;
		int e = i++;
		while (e > s && (b[e - 1] == '\r' || b[e - 1] == ' ')) e--;
		if (e - s < 10 || b[s + 8] != '|') continue;
		int date = 0; bool ok = true;
		for (int k = 0; k < 8; k++) { char c = b[s + k]; if (c < '0' || c > '9') ok = false; date = date * 10 + (c - '0'); }
		if (!ok || date < today) continue;
		AgEv &v = g_ag[g_nag];
		const char *p = b + s + 9;
		v.hm = -1;
		if (e - (s + 9) >= 6 && p[0] >= '0' && p[0] <= '2' && p[1] >= '0' && p[1] <= '9' && p[2] == ':' && p[3] >= '0' && p[3] <= '5' && p[4] >= '0' && p[4] <= '9' && p[5] == ' ')
		{ v.hm = ((p[0] - '0') * 10 + p[1] - '0') * 60 + (p[3] - '0') * 10 + p[4] - '0'; p += 6; }
		int m = 0;
		while (p < b + e && m < (int) sizeof v.what - 1) v.what[m++] = *p++;
		v.what[m] = 0;
		if (!m) continue;
		v.date = date;
		g_nag++;
	}
	for (int i = 1; i < g_nag; i++)				// by date, then the time (the whole day first)
		for (int j = i; j > 0 && (g_ag[j].date < g_ag[j - 1].date || (g_ag[j].date == g_ag[j - 1].date && g_ag[j].hm < g_ag[j - 1].hm)); j--)
		{ AgEv t = g_ag[j]; g_ag[j] = g_ag[j - 1]; g_ag[j - 1] = t; }
	return true;
}

// The next appointment: today's next one at a time, else today's of the whole day, else the next day's -> its index, -1.
static int agenda_next (void)
{
	int now, today = today_ymd (&now);
	for (int i = 0; i < g_nag; i++) if (g_ag[i].date == today && g_ag[i].hm >= now) return i;
	for (int i = 0; i < g_nag; i++) if (g_ag[i].date == today && g_ag[i].hm < 0) return i;
	for (int i = 0; i < g_nag; i++) if (g_ag[i].date > today) return i;
	return -1;
}

// When it is, from now: "in 1 h 26", "in 25 min", "now", "tomorrow", "in 3 days" ("" today, of the whole day).
static void agenda_when (const AgEv &v, char *o, int cap)
{
	int now, today = today_ymd (&now), k = 0;
	o[0] = 0;
	if (v.date == today)
	{
		if (v.hm < 0) return;
		int m = v.hm - now;
		if (m <= 0) { lx_cat (o, cap, &k, TR ("now")); return; }
		lx_cat (o, cap, &k, TR ("in")); lx_cat (o, cap, &k, " ");
		if (m >= 60) { num_cat (o, cap, &k, m / 60); lx_cat (o, cap, &k, " h"); if (m % 60) { lx_cat (o, cap, &k, " "); num_cat (o, cap, &k, m % 60); } }
		else { num_cat (o, cap, &k, m); lx_cat (o, cap, &k, " min"); }
		return;
	}
	int days = day_number (v.date) - day_number (today);
	if (days == 1) { lx_cat (o, cap, &k, TR ("tomorrow")); return; }
	lx_cat (o, cap, &k, TR ("in")); lx_cat (o, cap, &k, " "); num_cat (o, cap, &k, days); lx_cat (o, cap, &k, " "); lx_cat (o, cap, &k, TR ("days"));
}
