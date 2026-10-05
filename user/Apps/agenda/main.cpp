//
// agenda -- a desktop widget: the next appointments of the calendar app (its notes in
// SD:/apps/calendar.app/agenda.txt, "YYYYMMDD|note" lines), today first. It sits on
// the desktop (WIN_FLAG_BACKMOST: every window covers it) and re-reads the file every
// few seconds, so a note typed in the calendar shows up by itself.
//   * Click an appointment: the calendar opens on that day.
//   * Drag the title: move the widget; its place is kept in its config.ini.
// It also sends the calendar's reminders (SD:/apps/calendar.app/reminders.txt, "YYYYMMDDHHMM|text"
// lines the calendar writes) as notifications when their minute comes, the calendar open or not;
// it is the "agenda" IPC service, so the calendar knows it need not send them itself.
// It is part of the wallpaper (the modernised CDE): no card, no shadow -- its text and an etched
// line straight on the desktop (a see-through window, WIN_FLAG_ALPHA), the ink chosen from the
// wallpaper's brightness under it (kapi_wallpaper_buffer): engraved (dark, a light line below) on
// a light wallpaper, white with a soft shadow on a dark one.
//
#include "appkit/appkit.h"
#include "applib.h"
#include "uikit/uikit.h"
#include "notify.h"

using namespace uikit;

#define AGENDA		"SD:/apps/calendar.app/agenda.txt"
#define CONFIG		"SD:/apps/agenda.app/config.ini"
#define REMINDERS	"SD:/apps/calendar.app/reminders.txt"
#define W		344
#define HDR		34			// the title, its etched line
#define ROW		21
#define NROWS		6
#define H		(HDR + 4 + NROWS * ROW + 8)
#define MAXEV		64
#define CATCH		0xFE000000u		// almost see-through: the clicks still land on the widget

// The ink, from the wallpaper under the widget (light: engraved; dark: white, a soft shadow).
static bool     g_light = false;
static unsigned g_back = 0x00304058, g_ink = 0x00FAFCFF, g_dim = 0x00B8C4D0;

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
	static char buf[16384];
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

// ---- reminders ---------------------------------------------------------------------------------------

// A date and time as minutes since 1970 (days_from_civil: Howard Hinnant's).
static long minutes_of (int y, int m, int d, int h, int mi)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (era * 146097 + doe - 719468) * 1440 + h * 60 + mi;
}
static long now_minutes (void)
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, 0);
	return minutes_of (y, mo, d, h, mi);
}

// Every few seconds: the reminders whose minute has come since the last look are notified.
static void reminders (void)
{
	static long last = -1;
	long now = now_minutes ();
	if (last < 0) { last = now; return; }		// (started now: what is past stays past)
	if (now == last) return;
	static char buf[16384];
	int n = 0;
	void *f = kapi_open (REMINDERS);
	if (f) { n = kapi_read (f, buf, sizeof buf - 1); kapi_close (f); }
	if (n < 0) n = 0;
	buf[n] = '\0';
	for (int i = 0; i < n; )
	{
		int ls = i; while (i < n && buf[i] != '\n') i++;
		int le = i++;
		if (le - ls < 14 || buf[ls + 12] != '|') continue;
		int v[5] = { 0, 0, 0, 0, 0 }, w[5] = { 4, 2, 2, 2, 2 }, k = ls;
		for (int j = 0; j < 5; j++) for (int c = 0; c < w[j]; c++) v[j] = v[j] * 10 + (buf[k++] - '0');
		long at = minutes_of (v[0], v[1], v[2], v[3], v[4]);
		if (at <= last || at > now) continue;
		char text[200]; int p = 0;
		for (int c = ls + 13; c < le && buf[c] != '\r' && p < 199; c++) text[p++] = buf[c];
		text[p] = '\0';
		notify ("Calendar", text);
	}
	last = now;
}

// The wallpaper under the widget (a sample every 4 px): its mean colour -> the ink. False: the
// same as before.
static bool read_back (int wx, int wy)
{
	int ww = 0, wh = 0;
	unsigned *wall = kapi_wallpaper_buffer (&ww, &wh);
	unsigned r = 0, g = 0, b = 0, n = 0;
	for (int y = wy; wall && y < wy + H && y < wh; y += 4)
		for (int x = wx; x < wx + W && x < ww; x += 4)
		{
			if (x < 0 || y < 0) continue;
			unsigned c = wall[(long) y * ww + x];
			r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
		}
	unsigned back = n && (r | g | b) ? ((r / n) << 16) | ((g / n) << 8) | (b / n) : 0x00304058;	// (none drawn: dark)
	if (back == g_back) return false;
	g_back = back;
	g_light = uk_bright (back) > 128;
	g_ink = g_light ? 0x00182232 : 0x00FAFCFF;
	g_dim = uk_mix (g_ink, back, 97);
	return true;
}

// Text straight on the wallpaper: engraved (a light line below) or with a soft shadow, each glyph
// pixel blended (the canvas is see-through there).
static void wall_text (Canvas &cv, int x, int y, const char *s, unsigned ink, int style)
{
	Font &f = font ();
	if (!f.valid ()) { cv.text (x, y, s, ink); return; }
	int gw = f.width (), gh = f.height ();
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; s[i]; i++)
		{
			const unsigned char *gl = f.glyph ((unsigned char) s[i], style);
			if (!gl) continue;
			for (int ry = 0; ry < gh; ry++)
				for (int rx = 0; rx < gw; rx++)
				{
					if (!(gl[ry] & (0x80 >> rx))) continue;
					int px = x + i * gw + rx, py = y + ry;
					if (pass == 1) { uk_blend_px (cv, px, py, ink, 255); continue; }
					if (g_light) uk_blend_px (cv, px, py + 1, uk_tone (g_back, 205), 200);	// engraved
					else						// a soft shadow
					{
						uk_blend_px (cv, px + 1, py + 1, 0, 150);
						uk_blend_px (cv, px + 2, py + 2, 0, 60);
						uk_blend_px (cv, px, py + 2, 0, 40);
						uk_blend_px (cv, px + 2, py, 0, 40);
					}
				}
		}
}

// "Tue 29 Sep" (the year too when it is not this one).
static void format_date (const Ev &e, char *out)
{
	int y = e.key / 10000, m = (e.key / 100) % 100, d = e.key % 100, p = 0;
	auto put = [&] (const char *s) { for (int i = 0; s[i] && p < 22; i++) out[p++] = s[i]; };
	put (DOW[dow (y, m, d)]); put (" ");
	char dd[3] = { (char) ('0' + d / 10), (char) ('0' + d % 10), 0 };
	put (dd[0] == '0' ? dd + 1 : dd); put (" "); put (MON[(m - 1) % 12]);
	if (y != g_today / 10000) { char yy[6] = { ' ', (char) ('0' + y / 1000 % 10), (char) ('0' + y / 100 % 10), (char) ('0' + y / 10 % 10), (char) ('0' + y % 10), 0 }; put (yy); }
	out[p] = '\0';
}

class AgendaRoot : public Root
{
public:
	int hot = -1;				// hovered row
	bool moving = false; int grabX = 0, grabY = 0, winX = 0, winY = 0;
	unsigned lastPoll = 0;

	AgendaRoot (int x, int y) : Root (x, y, W, H, "agenda",
		WIN_FLAG_BORDERLESS | WIN_FLAG_BACKMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA), winX (x), winY (y) {}

	void onDraw () override
	{
		int fh = uk_fh (), fw = uk_fw ();
		canvas.clear (CATCH);
		uk_paint_alpha (true);
		uk_rbox (canvas, 12, 9, 16, 17, 3, 0x00FFFFFF, 0x00E8E8E8);		// a small calendar
		uk_rbox (canvas, 12, 9, 16, 6, 3, 0x00D23A30, 0x00C0322C, 255, UK_TL | UK_TR);
		uk_rline (canvas, 12, 9, 16, 17, 3, 0x00000000, 90);
		const char *a = "Next appointments";
		wall_text (canvas, 36, 5 + (26 - fh) / 2, a, g_ink, 2);
		if (g_nev)
		{
			char t[12]; int p = 0; t[p++] = '('; p += ax_itoa (g_nev, t + p); t[p++] = ')'; t[p] = '\0';
			wall_text (canvas, 36 + uk_len (a) * fw + 6, 5 + (26 - fh) / 2, t, g_dim, 0);
		}
		for (int i = 10; i < W - 10; i++)					// the etched line
		{
			uk_blend_px (canvas, i, 33, g_light ? uk_tone (g_back, 90) : 0, g_light ? 200 : 110);
			uk_blend_px (canvas, i, 34, g_light ? uk_tone (g_back, 190) : 0x00FFFFFF, g_light ? 200 : 70);
		}
		int maxc = (W - 110) / fw;
		for (int r = 0; r < NROWS && r < g_nev; r++)
		{
			int y = HDR + 4 + r * ROW;
			const Ev &e = g_ev[r];
			if (r == hot) uk_rbox (canvas, 6, y, W - 12, ROW, 6, g_light ? 0 : 0x00FFFFFF, g_light ? 0 : 0x00FFFFFF, 34);
			char date[24], note[128];
			format_date (e, date);
			int p = 0; for (; e.note[p] && p < maxc && p < 127; p++) note[p] = e.note[p];
			note[p] = '\0';
			if (e.note[p] && p > 2) { note[p - 1] = '.'; note[p - 2] = '.'; }
			int ty = y + (ROW - fh) / 2;
			if (e.key == g_today)
			{
				uk_rbox (canvas, 12, y + 1, 54, ROW - 3, 6, uk_tone (C_ACCENT, 150), C_ACCENT);
				uk_text_c (canvas, 12, y + 1, 54, ROW - 3, "Today", C_SEL_TEXT);
				wall_text (canvas, 100, ty, note, g_ink, 2);
			}
			else
			{
				wall_text (canvas, 14, ty, date, g_dim, 0);
				wall_text (canvas, 100, ty, note, g_ink, 0);
			}
		}
		if (g_nev == 0) wall_text (canvas, 14, HDR + 8, "No upcoming appointments.", g_dim, 0);
		if (g_nev > NROWS) wall_text (canvas, W - 3 * fw - 8, H - fh - 2, "...", g_dim, 0);
		uk_paint_alpha (false);
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
				if (read_back (winX, winY)) invalidate (true);	// (the wallpaper there)
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
		bool b = read_back (winX, winY);		// (a new wallpaper)
		if (reload () || b) invalidate (true);
		reminders ();
	}
};

int main (void)
{
	int x = 12, y = 40;				// below the menu bar, top-left
	if (app_ini_load_path (CONFIG) >= 0) { x = app_ini_get_int (0, "x", x); y = app_ini_get_int (0, "y", y); }
	kapi_ipc_register ("agenda");
	reload ();
	reminders ();
	read_back (x, y);
	AgendaRoot root (x, y);
	if (root.canvas.px == 0) return 1;
	root.run ();
	return 0;
}
