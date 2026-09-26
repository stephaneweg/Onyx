//
// agenda -- a desktop widget: the next appointments of the calendar app (its notes in
// SD:/apps/calendar.app/agenda.txt, "YYYYMMDD|note" lines), today first. It sits on
// the desktop (WIN_FLAG_BACKMOST: every window covers it) and re-reads the file every
// few seconds, so a note typed in the calendar shows up by itself.
//   * Click an appointment: the calendar opens on that day.
//   * Drag the title bar: move the widget; its place is kept in its config.ini.
//
#include "kapi.h"
#include "applib.h"
#include "wtk/wtk.h"

using namespace wtk;

#define AGENDA		"SD:/apps/calendar.app/agenda.txt"
#define CONFIG		"SD:/apps/agenda.app/config.ini"
#define W		340
#define HDR		24			// title bar
#define ROW		20
#define NROWS		6
#define H		(HDR + NROWS * ROW + 8)
#define MAXEV		64

static const unsigned A_BG = 0x001C232C, A_EDGE = 0x00485870, A_HDR = 0x00303D4D,
	A_TXT = 0x00E0E6EE, A_DIM = 0x008A96A8, A_TODAY = 0x0060FF90, A_HOT = 0x00355070;

struct Ev { int key; char note[96]; };		// key = YYYYMMDD
static Ev   g_ev[MAXEV];
static int  g_nev = 0;
static int  g_today = 0;
static unsigned g_sig = 0;			// cheap signature of the file (size + sum)

static const char *MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
			       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *DOW[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static int dow (int y, int m, int d)		// 0 = Sunday (Zeller, as in calendar)
{
	if (m < 3) { m += 12; y--; }
	int k = y % 100, j = y / 100;
	int h = (d + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
	return (h + 6) % 7;
}

// Re-read agenda.txt; true if the upcoming list changed.
static bool reload (void)
{
	int y = 0, mo = 0, d = 0;
	kapi_get_datetime (&y, &mo, &d, 0, 0, 0);
	int today = y * 10000 + mo * 100 + d;
	static char buf[4096];
	int n = 0;
	void *f = kapi_open (AGENDA);
	if (f) { n = kapi_read (f, buf, sizeof buf - 1); kapi_close (f); }
	if (n < 0) n = 0;
	buf[n] = '\0';
	unsigned sig = (unsigned) n * 2654435761u;
	for (int i = 0; i < n; i++) sig = sig * 31 + (unsigned char) buf[i];
	if (sig == g_sig && today == g_today) return false;
	g_sig = sig; g_today = today;

	g_nev = 0;
	for (int i = 0; i < n; )
	{
		int ls = i; while (i < n && buf[i] != '\n') i++;
		int le = i++;
		if (le - ls < 10 || buf[ls + 8] != '|') continue;
		int key = 0; bool ok = true;
		for (int k = 0; k < 8; k++) { char c = buf[ls + k]; if (c < '0' || c > '9') ok = false; key = key * 10 + (c - '0'); }
		if (!ok || key < today || g_nev >= MAXEV) continue;	// past days: not "upcoming"
		Ev &e = g_ev[g_nev++];
		e.key = key;
		int p = 0;
		for (int k = ls + 9; k < le && buf[k] != '\r' && p < (int) sizeof e.note - 1; k++) e.note[p++] = buf[k];
		e.note[p] = '\0';
	}
	for (int i = 1; i < g_nev; i++)			// sort by date (insertion)
	{
		Ev t = g_ev[i]; int j = i - 1;
		while (j >= 0 && g_ev[j].key > t.key) { g_ev[j + 1] = g_ev[j]; j--; }
		g_ev[j + 1] = t;
	}
	return true;
}

// "Fri 26 Sep - note" (or "Today - note"), clipped to maxc characters.
static void format (const Ev &e, char *out, int maxc)
{
	int y = e.key / 10000, m = (e.key / 100) % 100, d = e.key % 100;
	int p = 0;
	auto put = [&] (const char *s) { for (int i = 0; s[i] && p < maxc; i++) out[p++] = s[i]; };
	if (e.key == g_today) put ("Today");
	else
	{
		put (DOW[dow (y, m, d)]); put (" ");
		char dd[3] = { (char) ('0' + d / 10), (char) ('0' + d % 10), 0 };
		put (dd[0] == '0' ? dd + 1 : dd); put (" "); put (MON[(m - 1) % 12]);
		if (y != g_today / 10000) { char yy[6] = { ' ', (char) ('0' + y / 1000 % 10), (char) ('0' + y / 100 % 10), (char) ('0' + y / 10 % 10), (char) ('0' + y % 10), 0 }; put (yy); }
	}
	put (" - "); put (e.note);
	if (p >= maxc && maxc > 3) { out[maxc - 2] = '.'; out[maxc - 1] = '.'; p = maxc; }
	out[p] = '\0';
}

class AgendaRoot : public Root
{
public:
	int hot = -1;				// hovered row
	bool moving = false; int grabX = 0, grabY = 0, winX = 0, winY = 0;
	unsigned lastPoll = 0;

	AgendaRoot (int x, int y) : Root (x, y, W, H, "agenda",
		WIN_FLAG_BORDERLESS | WIN_FLAG_BACKMOST | WIN_FLAG_SYSTEM), winX (x), winY (y) {}

	void onDraw () override
	{
		int fw = kapi_font_width (), fh = kapi_font_height ();
		if (fw < 1) fw = 8;
		if (fh < 1) fh = 16;
		canvas.clear (A_BG);
		canvas.frameRect (0, 0, W, H, A_EDGE);
		canvas.fillRect (1, 1, W - 2, HDR - 1, A_HDR);
		char t[48]; int p = 0;
		const char *a = "Next appointments";
		for (int i = 0; a[i]; i++) t[p++] = a[i];
		if (g_nev) { t[p++] = ' '; t[p++] = '('; p += ax_itoa (g_nev, t + p); t[p++] = ')'; }
		t[p] = '\0';
		canvas.text (8, (HDR - fh) / 2, t, A_TXT);
		canvas.text (9, (HDR - fh) / 2, t, A_TXT);		// bold
		int maxc = (W - 16) / fw;
		for (int r = 0; r < NROWS && r < g_nev; r++)
		{
			int y = HDR + 4 + r * ROW;
			if (r == hot) canvas.fillRect (2, y - 1, W - 4, ROW, A_HOT);
			char line[128]; format (g_ev[r], line, maxc < 127 ? maxc : 127);
			canvas.text (8, y + (ROW - fh) / 2, line, g_ev[r].key == g_today ? A_TODAY : A_TXT);
		}
		if (g_nev == 0) canvas.text (8, HDR + 8, "No upcoming appointments.", A_DIM);
		if (g_nev > NROWS) canvas.text (W - 3 * fw - 6, H - fh - 2, "...", A_DIM);
	}

	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { if (hot >= 0) { hot = -1; invalidate (true); } return false; }
		int sx = 0, sy = 0;
		kapi_cursor_pos (&sx, &sy);			// screen coords (the window moves)
		if (moving)
		{
			if (!bl)
			{
				moving = false;
				char cfg[64]; int p = 0;
				const char *a = "x = "; for (int i = 0; a[i]; i++) cfg[p++] = a[i];
				p += ax_itoa (winX, cfg + p); cfg[p++] = '\n';
				a = "y = "; for (int i = 0; a[i]; i++) cfg[p++] = a[i];
				p += ax_itoa (winY, cfg + p); cfg[p++] = '\n';
				kapi_save_file (CONFIG, cfg, (unsigned) p);
			}
			else
			{
				winX += sx - grabX; winY += sy - grabY; grabX = sx; grabY = sy;
				kapi_move_window (winX, winY);
			}
			return true;
		}
		int row = (my >= HDR + 4) ? (my - HDR - 4) / ROW : -1;
		if (row >= g_nev || row >= NROWS) row = -1;
		if (row != hot) { hot = row; invalidate (true); }
		if (bl && !pressed)
		{
			pressed = true;
			if (my < HDR) { moving = true; grabX = sx; grabY = sy; }
			else if (row >= 0)				// open the calendar on that day
			{
				char k[12]; int p = ax_itoa (g_ev[row].key, k); k[p] = '\0';
				kapi_exec ("SD:apps/calendar.app/main", k);
			}
		}
		if (!bl) pressed = false;
		return true;
	}

	void onTick () override
	{
		unsigned now = kapi_get_ticks ();
		if (now - lastPoll < 300) return;		// every ~3 s
		lastPoll = now;
		if (reload ()) invalidate (true);
	}
};

int main (void)
{
	int x = 12, y = 40;				// below the menu bar, top-left
	if (app_ini_load_path (CONFIG) >= 0) { x = app_ini_get_int (0, "x", x); y = app_ini_get_int (0, "y", y); }
	reload ();
	AgendaRoot root (x, y);
	if (root.canvas.px == 0) return 1;
	root.run ();
	return 0;
}
