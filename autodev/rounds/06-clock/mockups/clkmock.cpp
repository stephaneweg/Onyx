//
// clkmock.cpp -- the UX Designer's mock-ups of the Clock (AutoDev round 6), drawn by UIKit on the PC (the desktop
// simulator: fakekapi.cpp, FreeType's DejaVu Sans). A THROWAWAY: not the app, not the Developer's code, not a
// documentation screenshot. It lays out the Clock's window with UIKit's real widgets (SegmentedControl, ToolButton,
// Button, NumericUpDown, Textbox, LcdDisplay, DataGrid) and the few drawn ones 04-ux-design.md adds (HereCard,
// CityList, NextAlarmBar, AlarmList, TimerRing, StopwatchFace, Veil + its cards). Nothing runs: each scene's state
// (MOCK_SCENE) is set by hand at the simulator's fixed time, Monday 2026-09-28 12:34:00, Brussels (UTC+2, summer).
// Built and run by mockups.sh.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "fontkit/uikitface.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uikit;

static const int W = 560, H = 440;			// the client area (04 §1: 560 x 440 by default)
static const int TOP = 48, FOOT = 46;			// the tab bar's band, the footer's
static const unsigned GREEN = 0x2E9A44, RED = 0xC0302A, AMBER = 0xB06A00;

static const char *SC (void) { return getenv ("MOCK_SCENE") ? getenv ("MOCK_SCENE") : "world"; }
static bool scene (const char *s) { return !strcmp (SC (), s); }
static bool scene_pre (const char *s) { return !strncmp (SC (), s, strlen (s)); }

static FtTextFace *g_face[80];
static TextFace *face (int px) { if (px < 6) px = 6; if (px > 79) px = 79; if (!g_face[px]) { g_face[px] = new FtTextFace; g_face[px]->open ("DejaVu Sans", px); } return g_face[px]; }
static int VV (double x) { return (int) lround (x * 16); }
static unsigned dim_on (unsigned bg) { return uk_mix (bg, uk_ink_for (bg), 150); }

// ---- the data shown (the fixtures of 03 §8.3, at the simulator's fixed time) -----------------------------------------
static int g_hh = 12, g_mm = 34, g_ss = 0;		// here's wall time
static int HERE_OFF = 120;				// Brussels in summer (0: no zone set -- the clock on UTC)
struct City { const char *name; int off, stdoff; };
static const City CITIES[] = { { "Tokyo", 540, 540 }, { "New York", -240, -300 }, { "London", 60, 0 }, { "Los Angeles", -420, -480 } };
static int g_ncity = 3;
static bool g_nozone = false;

static const char *const DAYS[7] = { TRN ("Monday"), TRN ("Tuesday"), TRN ("Wednesday"), TRN ("Thursday"), TRN ("Friday"), TRN ("Saturday"), TRN ("Sunday") };
static const char *const MONTHS[12] = { TRN ("January"), TRN ("February"), TRN ("March"), TRN ("April"), TRN ("May"), TRN ("June"), TRN ("July"),
					TRN ("August"), TRN ("September"), TRN ("October"), TRN ("November"), TRN ("December") };
static const char *const SHORTDAY[7] = { TRN ("Mon"), TRN ("Tue"), TRN ("Wed"), TRN ("Thu"), TRN ("Fri"), TRN ("Sat"), TRN ("Sun") };
// The cities' names (SystemKit's zone table, English) given to TR () where shown:
// TR: Brussels
// TR: London
// TR: Vienna
// TR: Warsaw
// TR: Lisbon
// TR: Athens
static const char *const ZONES[] = { "Brussels", "Paris", "Amsterdam", "Luxembourg", "Berlin", "Zurich", "Vienna", "Rome", "Madrid", "Stockholm",
				     "Warsaw", "London", "Dublin", "Lisbon", "Helsinki", "Athens", "New York", "Toronto", "Chicago", "Denver",
				     "Los Angeles", "Tokyo", "UTC" };
static const int ZOFF[] = { 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 60, 60, 60, 180, 180, -240, -240, -300, -360, -420, 540, 0 };

static void utc_text (int off, char *o, int cap)
{
	if (off % 60) snprintf (o, cap, "UTC%c%d:%02d", off < 0 ? '-' : '+', abs (off) / 60, abs (off) % 60);
	else if (off) snprintf (o, cap, "UTC%c%d", off < 0 ? '-' : '+', abs (off) / 60);
	else snprintf (o, cap, "UTC");
}
static void diff_text (int d, char *o, int cap)		// "+7 h", "−6 h", "+5 h 30", "Same time"
{
	if (d == 0) { snprintf (o, cap, "%s", TR ("Same time")); return; }
	const char *sg = d < 0 ? "\xE2\x88\x92" : "+"; d = abs (d);
	if (d % 60) snprintf (o, cap, "%s%d h %02d", sg, d / 60, d % 60); else snprintf (o, cap, "%s%d h", sg, d / 60);
}

// ---- small drawings (VPath: anti-aliased, any size) --------------------------------------------------------------------
static void bell (Canvas &cv, double cx, double cy, double s, unsigned c, bool waves = false, int a = 255)
{
	VPath p;
	p.circle (VV (cx), VV (cy - s * 0.12), VV (s * 0.30));
	int body[8] = { VV (cx - s * 0.30), VV (cy - s * 0.12), VV (cx + s * 0.30), VV (cy - s * 0.12), VV (cx + s * 0.40), VV (cy + s * 0.26), VV (cx - s * 0.40), VV (cy + s * 0.26) };
	p.poly (body, 4);
	p.rrect (VV (cx - s * 0.48), VV (cy + s * 0.20), VV (s * 0.96), VV (s * 0.12), VV (s * 0.06));
	p.circle (VV (cx), VV (cy + s * 0.40), VV (s * 0.10));
	p.circle (VV (cx), VV (cy - s * 0.45), VV (s * 0.07));
	p.fill (cv, c, a);
	if (waves)
	{
		VPath w;
		for (int k = 0; k < 2; k++)
		{
			double r = s * (0.66 + 0.2 * k);
			w.arc (VV (cx), VV (cy), VV (r), -28, 28, VV (s * 0.06 + 0.6));
			w.arc (VV (cx), VV (cy), VV (r), 152, 208, VV (s * 0.06 + 0.6));
		}
		w.fill (cv, c, a);
	}
}
static void slash (Canvas &cv, double cx, double cy, double s, unsigned c, unsigned under)
{
	VPath p; p.line (VV (cx - s * 0.45), VV (cy - s * 0.45), VV (cx + s * 0.45), VV (cy + s * 0.45), VV (s * 0.16)); p.fill (cv, under);
	VPath q; q.line (VV (cx - s * 0.45), VV (cy - s * 0.45), VV (cx + s * 0.45), VV (cy + s * 0.45), VV (s * 0.07)); q.fill (cv, c);
}
static void globe (Canvas &cv, double cx, double cy, double r, unsigned c, int a = 255)
{
	VPath p; int w = VV (r * 0.07 + 0.5);
	p.arc (VV (cx), VV (cy), VV (r), 0, 360, w);
	p.line (VV (cx - r), VV (cy), VV (cx + r), VV (cy), w);
	for (int k = 0; k < 2; k++)
	{
		double yy = cy + (k ? 0.5 : -0.5) * r, hw = sqrt (1 - 0.25) * r;
		p.line (VV (cx - hw), VV (yy), VV (cx + hw), VV (yy), w);
	}
	for (int m = 0; m < 2; m++)
	{
		int xy[2 * 25]; double rx = m ? r * 0.45 : 0.01;
		if (!m) { p.line (VV (cx), VV (cy - r), VV (cx), VV (cy + r), w); continue; }
		for (int i = 0; i < 25; i++) { double t = M_PI * 2 * i / 24; xy[2 * i] = VV (cx + rx * cos (t)); xy[2 * i + 1] = VV (cy + r * sin (t)); }
		p.polyline (xy, 24, w, true);
	}
	p.fill (cv, c, a);
}
static void sun (Canvas &cv, double cx, double cy, double r)
{
	VPath p; p.circle (VV (cx), VV (cy), VV (r * 0.5));
	for (int i = 0; i < 8; i++) { double t = M_PI / 4 * i; p.line (VV (cx + cos (t) * r * 0.72), VV (cy + sin (t) * r * 0.72), VV (cx + cos (t) * r), VV (cy + sin (t) * r), VV (r * 0.16)); }
	p.fill (cv, 0xF0A020);
}
static void moon (Canvas &cv, double cx, double cy, double r)
{
	VPath p; p.circle (VV (cx), VV (cy), VV (r * 0.8)); p.hole (VV (cx + r * 0.45), VV (cy - r * 0.3), VV (r * 0.68)); p.fill (cv, 0x5A6A9A);
}
static void hourglass (Canvas &cv, double cx, double cy, double s, unsigned c)
{
	VPath p;
	p.rrect (VV (cx - s * 0.36), VV (cy - s * 0.5), VV (s * 0.72), VV (s * 0.1), VV (s * 0.04));
	p.rrect (VV (cx - s * 0.36), VV (cy + s * 0.4), VV (s * 0.72), VV (s * 0.1), VV (s * 0.04));
	int a[6] = { VV (cx - s * 0.28), VV (cy - s * 0.4), VV (cx + s * 0.28), VV (cy - s * 0.4), VV (cx), VV (cy - s * 0.02) };
	int b[6] = { VV (cx), VV (cy + s * 0.02), VV (cx + s * 0.28), VV (cy + s * 0.4), VV (cx - s * 0.28), VV (cy + s * 0.4) };
	p.polyline (a, 3, VV (s * 0.06), true); p.polyline (b, 3, VV (s * 0.06), true);
	int sand[6] = { VV (cx - s * 0.2), VV (cy + s * 0.37), VV (cx + s * 0.2), VV (cy + s * 0.37), VV (cx), VV (cy + s * 0.16) };
	p.poly (sand, 3);
	p.fill (cv, c);
}
static void warn (Canvas &cv, int cx, int cy, int s)
{
	VPath p; int t[6] = { VV (cx), VV (cy - s * 0.5), VV (cx + s * 0.55), VV (cy + s * 0.45), VV (cx - s * 0.55), VV (cy + s * 0.45) }; p.poly (t, 3); p.fill (cv, 0xE0A020);
	VPath q; q.line (VV (cx), VV (cy - s * 0.18), VV (cx), VV (cy + s * 0.12), VV (s * 0.12)); q.circle (VV (cx), VV (cy + s * 0.29), VV (s * 0.07)); q.fill (cv, 0x000000);
}
static void speaker_off (Canvas &cv, double cx, double cy, double s, unsigned c)
{
	VPath p;
	int b[12] = { VV (cx - s * 0.5), VV (cy - s * 0.16), VV (cx - s * 0.28), VV (cy - s * 0.16), VV (cx), VV (cy - s * 0.42),
		      VV (cx), VV (cy + s * 0.42), VV (cx - s * 0.28), VV (cy + s * 0.16), VV (cx - s * 0.5), VV (cy + s * 0.16) };
	p.poly (b, 6);
	p.line (VV (cx + s * 0.14), VV (cy - s * 0.18), VV (cx + s * 0.5), VV (cy + s * 0.18), VV (s * 0.1));
	p.line (VV (cx + s * 0.14), VV (cy + s * 0.18), VV (cx + s * 0.5), VV (cy - s * 0.18), VV (s * 0.1));
	p.fill (cv, c);
}
static void pin (Canvas &cv, double cx, double cy, double s, unsigned c)		// a map pin: "here"
{
	VPath p; p.circle (VV (cx), VV (cy - s * 0.18), VV (s * 0.3));
	int t[6] = { VV (cx - s * 0.26), VV (cy - s * 0.04), VV (cx + s * 0.26), VV (cy - s * 0.04), VV (cx), VV (cy + s * 0.5) }; p.poly (t, 3);
	p.hole (VV (cx), VV (cy - s * 0.18), VV (s * 0.12));
	p.fill (cv, c);
}
static void chev_icon (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool off) { (void) off; uk_glyph (cv, id ? WKG_CHEV_DOWN : WKG_CHEV_UP, x + size / 2, y + size / 2, size - 6, ink); }

// ---- the window ---------------------------------------------------------------------------------------------------------
class ClockRoot : public Root
{
public:
	ClockRoot () : Root (W, H, TR ("Clock")) {}
	void onDraw () override { Root::onDraw (); canvas.fillRect (0, TOP - 1, width, 1, uk_tone (bg, 100)); }
};

// The footer's text: what the tab holds at the left (dim), what happened (a check, then the words).
class FootText : public Widget
{
public:
	char l[120], msg[80];
	FootText (int x, int y, int w, int h, const char *s) : Widget (x, y, w, h) { snprintf (l, sizeof l, "%s", s); msg[0] = 0; }
	void onDraw () override
	{
		canvas.clear (C_BG);
		unsigned d = uk_mix (C_BG, C_TEXT, 170);
		uk_text_l (canvas, 0, 0, height, l, d);
		if (msg[0]) { int x = uk_tw (l) + 18; uk_glyph (canvas, WKG_CHECK, x + 5, height / 2, 10, GREEN); uk_text_l (canvas, x + 14, 0, height, msg, GREEN); }
	}
};

// ---- World ----------------------------------------------------------------------------------------------------------------
class HereCard : public Widget
{
public:
	HereCard (int x, int y, int w, int h) : Widget (x, y, w, h) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		char t[32], b[120];
		snprintf (t, sizeof t, "%02d:%02d:%02d", g_hh, g_mm, g_ss);
		{ UkFaceScope sc (face (54)); uk_text_c (canvas, 0, 0, width, 66, t, C_TEXT, 2); }
		snprintf (b, sizeof b, "%s %d %s 2026", TR (DAYS[0]), 28, TR (MONTHS[8]));
		{ UkFaceScope sc (face (16)); uk_text_c (canvas, 0, 64, width, 24, b, C_TEXT); }
		unsigned d = dim_on (C_BG);
		if (g_nozone) return;	// (the warning and its button: the window's widgets)
		char u[16]; utc_text (HERE_OFF, u, sizeof u);
		snprintf (b, sizeof b, "%s  \xC2\xB7  %s  \xC2\xB7  %s", TR ("Brussels"), u, TR ("Summer time"));
		int w = uk_tw (b) + 18, x = (width - w) / 2;
		pin (canvas, x + 6, 104, 14, C_ACCENT);
		uk_text_l (canvas, x + 18, 92, 24, b, d);
	}
};

class CityList : public Widget
{
public:
	enum { ROW = 50 };
	int sel;
	CityList (int x, int y, int w, int h) : Widget (x, y, w, h), sel (0) { canFocus = true; }
	unsigned bgColor () override { return C_BG; }
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD, hasFocus);
		unsigned ink = C_FIELD_TEXT, d = uk_mix (C_FIELD, C_FIELD_TEXT, 140), faint = uk_mix (C_FIELD, C_FIELD_TEXT, 30);
		if (g_ncity == 0)
		{
			globe (canvas, width / 2, height / 2 - 30, 26, uk_mix (C_FIELD, C_FIELD_TEXT, 70));
			{ UkFaceScope sc (face (15)); uk_text_c (canvas, 0, height / 2 + 4, width, 22, TR ("No cities yet"), ink, 2); }
			uk_text_c (canvas, 0, height / 2 + 28, width, 20, TR ("Add the cities you want to follow with + Add City (Ctrl+N)."), d);
			return;
		}
		for (int i = 0; i < g_ncity; i++)
		{
			const City &c = CITIES[i];
			int y = 4 + i * ROW;
			if (i == sel) { unsigned t = uk_mix (C_FIELD, C_ACCENT, hasFocus ? 90 : 60); uk_rbox (canvas, 5, y, width - 10, ROW - 3, 7, t, t); }
			else if (i + 1 < g_ncity && i + 1 != sel) canvas.fillRect (44, y + ROW - 2, width - 58, 1, faint);
			int m = ((g_hh * 60 + g_mm - HERE_OFF + c.off) % 1440 + 1440) % 1440, dd = (g_hh * 60 + g_mm - HERE_OFF + c.off) < 0 ? -1 : (g_hh * 60 + g_mm - HERE_OFF + c.off) >= 1440 ? 1 : 0;
			int hh = m / 60, cy = y + (ROW - 3) / 2;
			if (hh >= 7 && hh < 19) sun (canvas, 24, cy, 10); else moon (canvas, 24, cy, 10);
			{ UkFaceScope sc (face (15)); uk_text (canvas, 44, y + 5, TR (c.name), ink, 2); }
			char df[32], u[16], l2[96];
			utc_text (c.off, u, sizeof u);
			if (g_nozone) snprintf (l2, sizeof l2, "%s", u);
			else { diff_text (c.off - HERE_OFF, df, sizeof df); snprintf (l2, sizeof l2, "%s  \xC2\xB7  %s", df, u); }
			int x = 44;
			if (dd)
			{
				const char *w = dd > 0 ? TR ("Tomorrow") : TR ("Yesterday");
				uk_text (canvas, x, y + 27, w, C_ACCENT, 2); x += uk_tw (w, 2);
				uk_text (canvas, x, y + 27, "  \xC2\xB7  ", d); x += uk_tw ("  \xC2\xB7  ");
			}
			uk_text (canvas, x, y + 27, l2, d);
			char t[8]; snprintf (t, sizeof t, "%02d:%02d", hh, m % 60);
			UkFaceScope sc (face (26)); uk_text (canvas, width - 18 - uk_tw (t, 2), y + (ROW - 3 - uk_fh ()) / 2, t, ink, 2);
		}
	}
};

// ---- Alarms ---------------------------------------------------------------------------------------------------------------
struct Al { int hh, mm; const char *label; unsigned days; bool on; int state; };	// state: 0, 1 snoozed, 2 missed, 3 invalid
enum { ST_NONE, ST_SNOOZED, ST_MISSED, ST_INVALID };
static Al g_al[6] = { { 7, 0, "School", 31, true, 0 }, { 9, 0, "Gym", 96, false, 0 }, { 14, 30, "Medicine", 0, true, 0 } };
static int g_nal = 3, g_alsel = 0;
// The alarms' labels in the fixtures (the user's own words: never translated by the app; the French mock-ups show
// a French user's): TR: School  TR: Gym  TR: Medicine  TR: Wake up
static const char *lbl (const char *s) { return TR (s); }

static void repeat_text (unsigned m, char *o, int cap)
{
	if (m == 0) { snprintf (o, cap, "%s", TR ("Once")); return; }
	if (m == 127) { snprintf (o, cap, "%s", TR ("Every day")); return; }
	if (m == 31) { snprintf (o, cap, "%s", TR ("Weekdays")); return; }
	if (m == 96) { snprintf (o, cap, "%s", TR ("Weekends")); return; }
	o[0] = 0;
	for (int i = 0; i < 7; i++) if (m & (1u << i)) { if (o[0]) strncat (o, ", ", cap - strlen (o) - 1); strncat (o, TR (SHORTDAY[i]), cap - strlen (o) - 1); }
}

class NextAlarmBar : public Widget
{
public:
	char bold[96], rest[96]; bool none;
	NextAlarmBar (int x, int y, int w, int h) : Widget (x, y, w, h), none (false) { bold[0] = rest[0] = 0; }
	void onDraw () override
	{
		canvas.clear (C_BG);
		unsigned f = none ? uk_tone (C_BG, 120) : uk_mix (C_BG, C_ACCENT, 46);
		uk_rbox (canvas, 0, 0, width, height, 8, f, f);
		uk_rline (canvas, 0, 0, width, height, 8, none ? uk_tone (C_BG, 96) : uk_mix (C_BG, C_ACCENT, 120));
		unsigned ink = uk_ink_for (f);
		if (none)
		{
			bell (canvas, 24, height / 2, 18, uk_mix (f, ink, 110)); slash (canvas, 24, height / 2, 18, uk_mix (f, ink, 110), f);
			uk_text_l (canvas, 44, 0, height, TR ("No alarm set"), uk_mix (f, ink, 150));
			return;
		}
		bell (canvas, 24, height / 2, 18, C_ACCENT);
		{ UkFaceScope sc (face (14)); uk_text_l (canvas, 44, 0, height, bold, ink, 2); int x = 44 + uk_tw (bold, 2); uk_text_l (canvas, x, 0, height, rest, ink); }
	}
};

class AlarmList : public Widget
{
public:
	enum { ROW = 60 };
	AlarmList (int x, int y, int w, int h) : Widget (x, y, w, h) { canFocus = true; }
	unsigned bgColor () override { return C_BG; }
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD, hasFocus);
		unsigned ink = C_FIELD_TEXT, d = uk_mix (C_FIELD, C_FIELD_TEXT, 140), faint = uk_mix (C_FIELD, C_FIELD_TEXT, 30), off = uk_mix (C_FIELD, C_FIELD_TEXT, 105);
		if (g_nal == 0)
		{
			bell (canvas, width / 2, height / 2 - 34, 46, uk_mix (C_FIELD, C_FIELD_TEXT, 60));
			{ UkFaceScope sc (face (15)); uk_text_c (canvas, 0, height / 2 + 4, width, 22, TR ("No alarms"), ink, 2); }
			uk_text_c (canvas, 0, height / 2 + 28, width, 20, TR ("Create one with + New Alarm (Ctrl+N)."), d);
			uk_text_c (canvas, 0, height / 2 + 48, width, 20, TR ("Alarms ring even when the Clock is closed."), d);
			return;
		}
		for (int i = 0; i < g_nal; i++)
		{
			const Al &a = g_al[i];
			int y = 4 + i * ROW;
			if (i == g_alsel) { unsigned t = uk_mix (C_FIELD, C_ACCENT, hasFocus ? 90 : 60); uk_rbox (canvas, 5, y, width - 10, ROW - 3, 7, t, t); }
			else if (i + 1 < g_nal && i + 1 != g_alsel) canvas.fillRect (16, y + ROW - 2, width - 32, 1, faint);
			char t[8], r[96];
			if (a.state == ST_INVALID) snprintf (t, sizeof t, "--:--"); else snprintf (t, sizeof t, "%02d:%02d", a.hh, a.mm);
			{ UkFaceScope sc (face (30)); uk_text (canvas, 18, y + (ROW - 3 - uk_fh ()) / 2, t, a.on && a.state != ST_INVALID ? ink : off, 2); }
			int x = 132;
			{ UkFaceScope sc (face (14)); uk_text (canvas, x, y + 10, a.state == ST_INVALID ? TR ("Invalid") : a.label[0] ? lbl (a.label) : TR ("Alarm"), a.on ? ink : off, 2); }
			if (a.state == ST_INVALID) { uk_text (canvas, x, y + 32, TR ("Cannot be read: correct alarms.txt or delete it"), RED); continue; }
			repeat_text (a.days, r, sizeof r);
			uk_text (canvas, x, y + 32, r, d); int rx = x + uk_tw (r);
			if (a.state)
			{
				char s[64];
				if (a.state == ST_SNOOZED) snprintf (s, sizeof s, TR ("Snoozed until %s"), "12:40"); else snprintf (s, sizeof s, TR ("Missed at %s"), "07:00");
				uk_text (canvas, rx, y + 32, "  \xC2\xB7  ", d); rx += uk_tw ("  \xC2\xB7  ");
				uk_text (canvas, rx, y + 32, s, a.state == ST_SNOOZED ? C_ACCENT : RED, 2);
			}
			uk_switch_mark (canvas, width - 64, y + (ROW - 3 - 24) / 2, 46, 24, a.on, UK_NORMAL);
		}
	}
};

// ---- Timer ----------------------------------------------------------------------------------------------------------------
static int g_tset = 300, g_tleft = 192; static bool g_trun = true, g_tpaused = false;
class TimerRing : public Widget
{
public:
	TimerRing (int x, int y, int w, int h) : Widget (x, y, w, h) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		int cx = width / 2, cy = height / 2, r = width / 2 - 14, rw = 14;
		VPath tr; tr.arc (V (cx), V (cy), V (r), 0, 360, V (rw)); tr.fill (canvas, uk_tone (C_BG, 108));
		int deg = g_tset ? (int) lround (360.0 * g_tleft / g_tset) : 0;
		unsigned arc = g_tpaused ? uk_mix (C_BG, C_ACCENT, 140) : C_ACCENT;
		if (deg > 0)
		{
			VPath a; a.arc (V (cx), V (cy), V (r), 90 - deg, 90, V (rw)); a.fill (canvas, arc);
			double t = (90 - deg) * M_PI / 180; VPath k; k.circle (VV (cx + r * cos (t)), VV (cy - r * sin (t)), V (4)); k.fill (canvas, 0xFFFFFF);
		}
		char b[16], s[64];
		int left = g_tleft;
		if (left >= 3600) snprintf (b, sizeof b, "%d:%02d:%02d", left / 3600, left / 60 % 60, left % 60); else snprintf (b, sizeof b, "%02d:%02d", left / 60, left % 60);
		unsigned d = dim_on (C_BG);
		snprintf (s, sizeof s, TR ("of %s"), "05:00");
		uk_text_c (canvas, 0, cy - 50, width, 20, s, d);
		{ UkFaceScope sc (face (46)); uk_text_c (canvas, 0, cy - 30, width, 56, b, C_TEXT, 2); }
		if (g_tpaused) { UkFaceScope sc (face (14)); uk_text_c (canvas, 0, cy + 28, width, 22, TR ("Paused"), C_ACCENT, 2); }
		else if (g_trun)
		{
			snprintf (s, sizeof s, TR ("Ends at %s"), "12:37");
			int w = uk_tw (s) + 18, x = (width - w) / 2;
			bell (canvas, x + 6, cy + 39, 13, d); uk_text_l (canvas, x + 18, cy + 28, 22, s, d);
		}
	}
};

// ---- Stopwatch ------------------------------------------------------------------------------------------------------------
static int g_swcs = 4107, g_curlap = 445; static bool g_swrun = true;
static const int LAPS[3] = { 1204, 2386, 3662 };	// totals, hundredths
static int g_nlaps = 3;
static void fmt_cs (int cs, char *o, int cap) { if (cs >= 360000) snprintf (o, cap, "%d:%02d:%02d.%02d", cs / 360000, cs / 6000 % 60, cs / 100 % 60, cs % 100); else snprintf (o, cap, "%02d:%02d.%02d", cs / 6000, cs / 100 % 60, cs % 100); }
class StopwatchFace : public Widget
{
public:
	StopwatchFace (int x, int y, int w, int h) : Widget (x, y, w, h) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		char a[16], b[8], s[64], l[16];
		snprintf (a, sizeof a, "%02d:%02d", g_swcs / 6000, g_swcs / 100 % 60); snprintf (b, sizeof b, ".%02d", g_swcs % 100);
		int wa, wb, asc_a, asc_b;
		{ UkFaceScope sc (face (60)); wa = uk_tw (a, 2); asc_a = face (60)->ascent (); }
		{ UkFaceScope sc (face (36)); wb = uk_tw (b, 2); asc_b = face (36)->ascent (); }
		int x = (width - wa - wb) / 2, base = 8 + asc_a;
		{ UkFaceScope sc (face (60)); uk_text (canvas, x, base - asc_a, a, C_TEXT, 2); }
		{ UkFaceScope sc (face (36)); uk_text (canvas, x + wa, base - asc_b, b, uk_mix (C_BG, C_TEXT, 190), 2); }
		if (g_nlaps) { fmt_cs (g_curlap, l, sizeof l); snprintf (s, sizeof s, TR ("Lap %d"), g_nlaps + 1); strncat (s, "  \xC2\xB7  ", sizeof s - strlen (s) - 1); strncat (s, l, sizeof s - strlen (s) - 1); }
		else snprintf (s, sizeof s, "%s", g_swcs ? "" : TR ("Space: start \xC2\xB7 L: lap \xC2\xB7 R: reset"));
		{ UkFaceScope sc (face (14)); uk_text_c (canvas, 0, height - 24, width, 22, s, dim_on (C_BG)); }
	}
};
static int lap_cs (int i) { return LAPS[i] - (i ? LAPS[i - 1] : 0); }
static int g_fast = 1, g_slow = 2;
static const char *lap_cell (DataGrid &, int row, int col, char *buf, int cap)
{
	int i = g_nlaps - 1 - row;
	if (col == 0) { snprintf (buf, cap, "%d", i + 1); return buf; }
	if (col == 1) { fmt_cs (lap_cs (i), buf, cap); return buf; }
	if (col == 2) { fmt_cs (LAPS[i], buf, cap); return buf; }
	return "";
}
static bool lap_draw (DataGrid &, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool selected)
{
	int i = g_nlaps - 1 - row; (void) selected; (void) ink;
	bool f = i == g_fast && g_nlaps >= 3, s = i == g_slow && g_nlaps >= 3;
	if (col == 1 && (f || s))
	{
		char b[16]; fmt_cs (lap_cs (i), b, sizeof b);
		uk_text (cv, x + w - 10 - uk_tw (b, 2), y + (h - uk_fh ()) / 2, b, f ? GREEN : RED, 2);
		return true;
	}
	if (col != 3 || !(f || s)) return false;
	const char *t = f ? TR ("Fastest") : TR ("Slowest");
	unsigned c = f ? GREEN : RED, bg = uk_mix (C_FIELD, c, 40);
	int tw = uk_tw (t, 2) + 26, py = y + 3, ph = h - 6;
	uk_rbox (cv, x + 8, py, tw, ph, ph / 2, bg, bg);
	uk_glyph (cv, f ? WKG_UP : WKG_DOWN, x + 18, py + ph / 2, 8, c);
	uk_text_l (cv, x + 26, py, ph, t, c, 2);
	return true;
}

// ---- the overlays: a veil over the window (what is under it, dimmed), a card on it -----------------------------------------
class Veil : public Widget
{
public:
	int cw, ch; const char *title; int kind;
	int cx, cy;		// the card's place (the widgets on it are placed from these)
	Veil (int w, int h, const char *t, int k) : Widget (0, 0, W, H), cw (w), ch (h), title (t), kind (k)
	{ cx = (W - cw) / 2; cy = (H - ch) / 2; }
	int th () const { return uk_fh () + 12; }
	void onDraw () override
	{
		// what the window shows under the veil (the siblings already composed into the root's canvas), dimmed
		Canvas &pc = parent->canvas;
		for (int y = 0; y < height && y < pc.h; y++) for (int x = 0; x < width && x < pc.w; x++)
			canvas.px[y * canvas.stride + x] = uk_mix (pc.px[y * pc.stride + x], 0x000000, 96);
		uk_rbox (canvas, cx, cy, cw, ch, 9, C_BG, C_BG);
		uk_title_strip (canvas, cx + 1, cy + 1, cw - 2, th (), title, 8);
		uk_rline (canvas, cx, cy, cw, ch, 9, UK_OUTLINE == 2 ? 0 : uk_tone (C_FRAME_ACTIVE, 44), 255);
		int y = cy + th ();
		unsigned d = dim_on (C_BG);
		if (kind == 1 || kind == 4)					// the ring
		{
			bell (canvas, W / 2, y + 36, 38, C_ACCENT, true);
			{ UkFaceScope sc (face (50)); uk_text_c (canvas, cx, y + 60, cw, 60, "07:00", C_TEXT, 2); }
			{ UkFaceScope sc (face (18)); uk_text_c (canvas, cx, y + 120, cw, 26, lbl ("School"), C_TEXT, 2); }
			char r[96]; repeat_text (31, r, sizeof r); strncat (r, "  \xC2\xB7  ", sizeof r - strlen (r) - 1); strncat (r, TR ("Chimes"), sizeof r - strlen (r) - 1);
			uk_text_c (canvas, cx, y + 146, cw, 20, r, d);
			if (kind == 4)
			{
				const char *s = TR ("Sound unavailable: the sound output is busy");
				int w = uk_tw (s) + 22, x = (W - w) / 2;
				speaker_off (canvas, x + 8, y + 180, 16, AMBER); uk_text_l (canvas, x + 22, y + 170, 20, s, AMBER);
			}
			uk_text_c (canvas, cx, cy + ch - 28, cw, 20, TR ("Enter: snooze \xC2\xB7 Esc: stop"), d);
		}
		if (kind == 2)							// time's up
		{
			hourglass (canvas, W / 2, y + 36, 40, C_ACCENT);
			{ UkFaceScope sc (face (28)); uk_text_c (canvas, cx, y + 62, cw, 36, TR ("Time's up"), C_TEXT, 2); }
			char s[96]; snprintf (s, sizeof s, TR ("The %s timer ended at %s"), "05:00", "12:39");
			uk_text_c (canvas, cx, y + 100, cw, 20, s, d);
			uk_text_c (canvas, cx, cy + ch - 28, cw, 20, TR ("Enter or Esc: stop \xC2\xB7 +: one more minute"), d);
		}
		if (kind == 3)							// the alarm editor: its captions
		{
			const char *cap[4] = { TR ("Time"), TR ("Label"), TR ("Repeat"), TR ("Sound") };
			const int cy4[4] = { 22, 92, 136, 214 };
			for (int i = 0; i < 4; i++) uk_text_l (canvas, cx + 20, y + cy4[i], 30, cap[i], C_TEXT, 2);
			uk_text (canvas, cx + 108 + 140, y + 12, TR ("Hours"), d); uk_text (canvas, cx + 108 + 140 + 82, y + 12, TR ("Minutes"), d);
			char s[80]; snprintf (s, sizeof s, TR ("Next: %s %s"), TR ("tomorrow"), "07:00");
			uk_text_l (canvas, cx + 108 + 140, y + 64, 22, s, d);
			canvas.fillRect (cx + 16, cy + ch - 56, cw - 32, 1, uk_tone (C_BG, 104));
		}
		if (kind == 5)							// add a city: the search glyph, the keys
		{
			uk_tool_glyph (canvas, WKT_SEARCH, cx + 20, y + 19, 16, d);
			uk_text_l (canvas, cx + 20, cy + ch - 84, 20, TR ("Type to filter \xC2\xB7 Enter: add \xC2\xB7 Esc: cancel"), d);
		}
	}
};

// ---- the scenes --------------------------------------------------------------------------------------------------------------
static ClockRoot *g_root;
static SegmentedControl *g_tabs;
static void add (Widget *w) { g_root->addChild (w); }
static ToolButton *tool (int x, int y, int w, int h, const char *tip, int glyph, const char *text, bool raised = true)
{
	ToolButton *b = new ToolButton (w, h, tip);
	if (glyph != WKT_NONE) b->setGlyph (glyph);
	if (text) b->setText (text);
	if (!w) b->fitWidth ();
	b->raised = raised; b->left = x; b->top = y; add (b); return b;
}
// Footer buttons from the right edge leftward (in the order given: the first is the rightmost).
static int g_fx;
static ToolButton *foot (const char *tip, int glyph, const char *text, int w = 0)
{
	ToolButton *b = new ToolButton (w, 30, tip);
	if (glyph != WKT_NONE) b->setGlyph (glyph);
	if (text) b->setText (text);
	if (!w) { b->fitWidth (); b->resizeTo (b->width + 8, b->height); }
	b->raised = true; g_fx -= b->width; b->left = g_fx; b->top = H - FOOT + 8; g_fx -= 6; add (b); return b;
}

static void world ()
{
	add (new HereCard (12, TOP + 6, W - 24, 122));
	if (g_nozone)
	{
		const char *s = TR ("Time zone not set");
		Button *lr = new Button (0, 0, 0, 28, TR ("Language & Region\xE2\x80\xA6"));
		int bw = uk_tw (lr->text) + 30, sw; { UkFaceScope sc (face (13)); sw = uk_tw (s, 2) + 26; }
		int x = (W - sw - 12 - bw) / 2;
		class Warn : public Widget { public: const char *s; Warn (int x, int y, int w, int h, const char *t) : Widget (x, y, w, h), s (t) {}
			void onDraw () override { canvas.clear (C_BG); warn (canvas, 9, height / 2, 16); uk_text_l (canvas, 24, 0, height, s, AMBER, 2); } };
		add (new Warn (x, TOP + 6 + 92, sw, 28, s));
		lr->left = x + sw + 12; lr->top = TOP + 6 + 92; lr->resizeTo (bw, 28); add (lr);
	}
	CityList *cl = new CityList (12, TOP + 134, W - 24, H - FOOT - TOP - 134); add (cl);
	if (g_ncity) cl->setFocus (); else cl->sel = -1;
	char s[64]; if (g_ncity) snprintf (s, sizeof s, TR ("%d cities \xC2\xB7 up to 12"), g_ncity); else snprintf (s, sizeof s, "%s", TR ("No cities"));
	add (new FootText (14, H - FOOT + 8, 200, 30, s));
	g_fx = W - 12;
	ToolButton *dn = foot (TR ("Move down"), WKT_NONE, 0, 30); dn->setIcon (chev_icon, 1);
	ToolButton *up = foot (TR ("Move up"), WKT_NONE, 0, 30); up->setIcon (chev_icon, 0);
	g_fx -= 6;
	ToolButton *rm = foot (TR ("Remove the city (Delete)"), WKT_MINUS, TR ("Remove"));
	foot (TR ("Add a city (Ctrl+N)"), WKT_PLUS, TR ("Add City"));
	up->setDisabled (true); if (!g_ncity) { dn->setDisabled (true); rm->setDisabled (true); }
}

static void alarms ()
{
	NextAlarmBar *nb = new NextAlarmBar (12, TOP + 8, W - 24, 40); add (nb);
	if (scene_pre ("alarms-states"))
	{ snprintf (nb->bold, sizeof nb->bold, TR ("Snoozed until %s"), "12:40"); snprintf (nb->rest, sizeof nb->rest, "  \xE2\x80\x94  %s", TR ("Medicine")); }
	else if (g_nal == 0) nb->none = true;
	else { snprintf (nb->bold, sizeof nb->bold, "%s %s %s", TR ("Next alarm:"), TR ("today"), "14:30"); snprintf (nb->rest, sizeof nb->rest, "  \xE2\x80\x94  %s", TR ("in 1 h 56 min")); }
	AlarmList *al = new AlarmList (12, TOP + 56, W - 24, H - FOOT - TOP - 56); add (al); if (g_nal) al->setFocus ();
	char s[64]; int on = 0; for (int i = 0; i < g_nal; i++) on += g_al[i].on;
	if (g_nal) snprintf (s, sizeof s, TR ("%d alarms \xC2\xB7 %d on"), g_nal, on); else snprintf (s, sizeof s, "%s", TR ("No alarms"));
	add (new FootText (14, H - FOOT + 8, 220, 30, s));
	g_fx = W - 12;
	ToolButton *del = foot (TR ("Delete the alarm (Delete)"), WKT_TRASH, TR ("Delete"));
	ToolButton *ed = foot (TR ("Edit the alarm (Enter)"), WKT_NONE, TR ("Edit"));
	foot (TR ("A new alarm (Ctrl+N)"), WKT_PLUS, TR ("New Alarm"));
	if (!g_nal) { del->setDisabled (true); ed->setDisabled (true); }
}

static NumericUpDown *g_nud[3];
static void timer ()
{
	add (new TimerRing (14, TOP + 8, 262, 262));
	int x0 = 296, cw = W - 12 - x0;
	class Cap : public Widget { public: const char *s; bool b; Cap (int x, int y, int w, const char *t, bool bold) : Widget (x, y, w, 22), s (t), b (bold) {}
		void onDraw () override { canvas.clear (C_BG); uk_text_l (canvas, 0, 0, height, s, b ? C_TEXT : dim_on (C_BG), b ? 2 : 0); } };
	add (new Cap (x0, TOP + 16, cw, TR ("Duration"), true));
	const char *unit[3] = { TR ("hours"), TR ("minutes"), TR ("seconds") };
	int v[3] = { 0, g_tset / 60, 0 }, hi[3] = { 23, 59, 59 }, bw = (cw - 2 * 10) / 3;
	for (int i = 0; i < 3; i++)
	{
		g_nud[i] = new NumericUpDown (x0 + i * (bw + 10), TOP + 42, bw, 30, 0, hi[i], v[i], 1, 0);
		g_nud[i]->disabled = g_trun; add (g_nud[i]);
		class UCap : public Widget { public: const char *s; UCap (int x, int y, int w, const char *t) : Widget (x, y, w, 18), s (t) {}
			void onDraw () override { canvas.clear (C_BG); uk_text_c (canvas, 0, 0, width, height, s, dim_on (C_BG)); } };
		add (new UCap (x0 + i * (bw + 10), TOP + 74, bw, unit[i]));
	}
	add (new Cap (x0, TOP + 104, cw, TR ("Presets"), true));
	static const char *const PR[5] = { "1 min", "3 min", "5 min", "10 min", "15 min" };
	int pw = (cw - 4 * 5) / 5;
	for (int i = 0; i < 5; i++) { ToolButton *p = tool (x0 + i * (pw + 5), TOP + 130, pw, 30, TR ("Set (a double click: set and start)"), WKT_NONE, PR[i]); p->setToggle (true, i == 2); }
	int by = TOP + 186;
	ToolButton *go = tool (x0, by, cw, 42, TR ("Start / Pause (Space)"), g_trun && !g_tpaused ? WKT_PAUSE : WKT_PLAY, g_trun && !g_tpaused ? TR ("Pause") : g_tpaused ? TR ("Resume") : TR ("Start"));
	go->filled = true; go->setToggle (true, true);
	ToolButton *rs = tool (x0, by + 52, cw, 36, TR ("Reset (R)"), WKT_TO_START, TR ("Reset"));
	rs->setDisabled (g_trun && !g_tpaused);
	add (new FootText (14, H - FOOT + 8, W - 28, 30, TR ("Space: start / pause \xC2\xB7 R: reset \xC2\xB7 it rings even with the Clock closed")));
}

static DataGrid *g_grid;
static void stopwatch ()
{
	add (new StopwatchFace (0, TOP + 4, W, 104));
	int bw = 164, gx = (W - 2 * bw - 14) / 2, by = TOP + 112;
	bool zero = g_swcs == 0;
	ToolButton *lap = tool (gx, by, bw, 38, g_swrun ? TR ("Lap (L)") : TR ("Reset (R)"), g_swrun ? WKT_NONE : WKT_TO_START, g_swrun ? TR ("Lap") : TR ("Reset"));
	lap->setDisabled (zero);
	ToolButton *go = tool (gx + bw + 14, by, bw, 38, TR ("Start / Stop (Space)"), g_swrun ? WKT_STOP : WKT_PLAY, g_swrun ? TR ("Stop") : TR ("Start"));
	go->filled = true; go->setToggle (true, true);
	g_grid = new DataGrid (12, TOP + 162, W - 24, H - FOOT - TOP - 162);
	g_grid->setColumns (4);
	g_grid->setColumn (0, TR ("Lap"), 70); g_grid->setColumn (1, TR ("Lap time"), 130, GRID_RIGHT); g_grid->setColumn (2, TR ("Total"), 130, GRID_RIGHT);
	g_grid->setColumn (3, "", W - 24 - 2 - 330 - 12);
	g_grid->cellText = lap_cell; g_grid->cellDraw = lap_draw; g_grid->stripes = true;
	g_grid->emptyText = TR ("No laps yet: Lap (L) while it runs");
	g_grid->setRows (g_nlaps); add (g_grid);
	char s[64]; if (g_nlaps) snprintf (s, sizeof s, TR ("%d laps"), g_nlaps); else snprintf (s, sizeof s, "%s", TR ("No laps"));
	FootText *ft = new FootText (14, H - FOOT + 8, 300, 30, s); add (ft);
	if (scene_pre ("stopwatch-copied")) snprintf (ft->msg, sizeof ft->msg, "%s", TR ("Laps copied"));
	g_fx = W - 12;
	ToolButton *cp = foot (TR ("Copy the laps as text (Ctrl+C)"), WKT_COPY, TR ("Copy Laps")); cp->setDisabled (!g_nlaps);
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();
	ClockRoot root; g_root = &root;
	if (root.canvas.px == 0) return 1;

	static const char *const TABS[4] = { TRN ("World"), TRN ("Alarms"), TRN ("Timer"), TRN ("Stopwatch") };
	const char *tabs[4]; for (int i = 0; i < 4; i++) tabs[i] = TR (TABS[i]);
	int tab = scene_pre ("alarms") || scene_pre ("ring") || scene_pre ("edit") ? 1 : scene_pre ("timer") || scene_pre ("timesup") ? 2 : scene_pre ("stopwatch") ? 3 : 0;
	g_tabs = new SegmentedControl ((W - 440) / 2, 10, 440, 28, tabs, 4, tab); add (g_tabs);

	if (scene_pre ("world-empty")) g_ncity = 0;
	if (scene_pre ("world-nozone")) { g_nozone = true; HERE_OFF = 0; g_hh = 10; }
	if (scene_pre ("world-late")) { g_hh = 23; g_mm = 30; g_ncity = 4; }
	if (scene_pre ("cities")) g_ncity = 3;
	if (scene_pre ("alarms-empty")) g_nal = 0;
	if (scene_pre ("alarms-states"))
	{
		g_al[0] = (Al) { 7, 0, "School", 31, true, ST_MISSED }; g_al[1] = (Al) { 12, 30, "Medicine", 0, true, ST_SNOOZED };
		g_al[2] = (Al) { 0, 0, "", 0, false, ST_INVALID }; g_al[3] = (Al) { 22, 15, "", 0x15, true, 0 }; g_nal = 4; g_alsel = 1;
	}
	if (scene_pre ("timer-paused")) g_tpaused = true;
	if (scene_pre ("timesup")) { g_tleft = 0; g_trun = false; }
	if (scene_pre ("stopwatch-zero")) { g_swcs = 0; g_nlaps = 0; g_swrun = false; }
	if (scene_pre ("stopwatch-copied")) g_swrun = false;

	switch (tab) { case 0: world (); break; case 1: alarms (); break; case 2: timer (); break; default: stopwatch (); }

	// the overlays
	if (scene_pre ("ring"))
	{
		bool ns = scene_pre ("ring-nosound");
		Veil *v = new Veil (380, ns ? 308 : 280, TR ("Alarm"), ns ? 4 : 1); add (v);
		int bw = 168, b2 = 120, x = (W - bw - 12 - b2) / 2, y = v->cy + v->ch - 76;
		char s[48]; snprintf (s, sizeof s, TR ("Snooze %d min"), 10);
		Button *sn = new Button (x, y, bw, 36, s); add (sn); sn->setFocus ();
		add (new Button (x + bw + 12, y, b2, 36, TR ("Stop")));
	}
	if (scene_pre ("timesup"))
	{
		Veil *v = new Veil (380, 236, TR ("Timer"), 2); add (v);
		int bw = 120, x = (W - 2 * bw - 12) / 2, y = v->cy + v->ch - 76;
		add (new Button (x, y, bw, 36, "+1 min"));
		Button *st = new Button (x + bw + 12, y, bw, 36, TR ("Stop")); add (st); st->setFocus ();
	}
	if (scene_pre ("edit"))
	{
		Veil *v = new Veil (460, 352, TR ("Edit Alarm"), 3); add (v);
		int x = v->cx + 108, y = v->cy + v->th ();
		LcdDisplay *lcd = new LcdDisplay (x, y + 10, 128, 54, "07:00"); lcd->face = face (34); lcd->centred = true; add (lcd);
		NumericUpDown *h = new NumericUpDown (x + 140, y + 30, 72, 30, 0, 23, 7, 1, 0); add (h); h->setFocus ();
		add (new NumericUpDown (x + 222, y + 30, 72, 30, 0, 59, 0, 1, 0));
		Textbox *tb = new Textbox (x, y + 92, v->cx + v->cw - 20 - x, 30, lbl ("School")); tb->maxLen = 160; add (tb);
		int dw = 44;
		for (int i = 0; i < 7; i++) { ToolButton *d = tool (x + i * (dw + 4), y + 136, dw, 30, 0, WKT_NONE, TR (SHORTDAY[i])); d->setToggle (true, i < 5); }
		ToolButton *ev = tool (x, y + 172, 0, 26, TR ("Every day of the week"), WKT_NONE, TR ("Every day")); ev->resizeTo (ev->width + 10, ev->height);
		ToolButton *wd = tool (x + ev->width + 6, y + 172, 0, 26, TR ("Monday to Friday"), WKT_NONE, TR ("Weekdays")); wd->resizeTo (wd->width + 10, wd->height);
		static const char *SND[3]; SND[0] = TR ("Chimes"); SND[1] = TR ("Beeps"); SND[2] = TR ("Marimba");
		add (new SegmentedControl (x, y + 214, 240, 30, SND, 3, 0));
		tool (x + 250, y + 214, 0, 30, TR ("Play the sound once"), WKT_PLAY, TR ("Test"));
		int by = v->cy + v->ch - 46;
		add (new Button (v->cx + 20, by, 100, 32, TR ("Delete")));
		Button *ok = new Button (v->cx + v->cw - 20 - 96, by, 96, 32, "OK"); add (ok);
		add (new Button (v->cx + v->cw - 20 - 96 - 10 - 104, by, 104, 32, TR ("Cancel")));
	}
	if (scene_pre ("cities"))
	{
		Veil *v = new Veil (380, 364, TR ("Add a City"), 5); add (v);
		int x = v->cx + 44, y = v->cy + v->th () + 10;
		Textbox *tb = new Textbox (x, y, v->cx + v->cw - 20 - x, 28, ""); add (tb);
		DataGrid *g = new DataGrid (v->cx + 20, y + 38, v->cw - 40, 200);
		g->setColumns (3); g->setColumn (0, TR ("City"), 170); g->setColumn (1, TR ("Time"), 70, GRID_RIGHT); g->setColumn (2, "UTC", v->cw - 40 - 2 - 240 - 12, GRID_RIGHT);
		g->stripes = true;
		static int idx[24]; static int n = 0;
		for (int i = 0; i < 23; i++) if (strcmp (ZONES[i], "Tokyo") && strcmp (ZONES[i], "New York") && strcmp (ZONES[i], "London") && strcmp (ZONES[i], "Brussels")) idx[n++] = i;
		for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (strcmp (TR (ZONES[idx[j]]), TR (ZONES[idx[i]])) < 0) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
		g->cellText = [] (DataGrid &, int row, int col, char *buf, int cap) -> const char *
		{
			int z = idx[row];
			if (col == 0) return TR (ZONES[z]);
			if (col == 1) { int m = ((12 * 60 + 34 - 120 + ZOFF[z]) % 1440 + 1440) % 1440; snprintf (buf, cap, "%02d:%02d", m / 60, m % 60); return buf; }
			utc_text (ZOFF[z], buf, cap); return buf;
		};
		g->setRows (n); g->setSel (0); add (g); g->setFocus ();
		int by = v->cy + v->ch - 46;
		Button *ad = new Button (v->cx + v->cw - 20 - 96, by, 96, 32, TR ("Add")); add (ad);
		add (new Button (v->cx + v->cw - 20 - 96 - 10 - 104, by, 104, 32, TR ("Cancel")));
	}
	root.run ();
	return 0;
}
