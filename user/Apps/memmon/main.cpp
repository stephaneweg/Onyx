//
// memmon -- a memory monitor. Shows total / used / free RAM (kapi_meminfo), the
// memory owned by user apps, a usage bar, and the processes ranked by the number of
// 64 KB pages they own (kapi_list_procs). Refreshes ~once a second. Canvas-drawn
// (no widgets needed -- it's a read-only display).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "applib.h"

#define W	440
#define H	380
#define MAXP	40

#define USED	0x00D08040	// the usage bar's fill (the data's colour)

static unsigned *fb;
static wtk::Canvas g_cv;		// the window's canvas (the painter draws on it)
static int g_fw = 8, g_fh = 16;

// "label" + value + " unit" at (x,y).
static void kv (int x, int y, const char *label, unsigned long val, const char *unit)
{
	char num[16]; ax_itoa ((int) val, num);
	char line[72]; int p = 0;
	for (int i = 0; label[i]; i++) line[p++] = label[i];
	for (int i = 0; num[i];   i++) line[p++] = num[i];
	line[p++] = ' ';
	for (int i = 0; unit[i];  i++) line[p++] = unit[i];
	line[p] = '\0';
	g_cv.text (x, y, line, wtk::C_TEXT);
}

// ---- process table (parsed from kapi_list_procs, sorted by pages) ------------

static int  g_np = 0;
static char g_pname[MAXP][32];
static int  g_ppages[MAXP];
static char g_pkind[MAXP];

static int puint (const char *s, int *pi)
{ int v = 0, i = *pi; while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; } *pi = i; return v; }

static void parse_procs (void)
{
	static char buf[4096];
	kapi_list_procs (buf, sizeof buf);
	g_np = 0;
	int i = 0;
	while (buf[i] && g_np < MAXP)			// "<pid> <a|k> <state> <pages> <name>"
	{
		puint (buf, &i);					// pid (unused here)
		while (buf[i] == ' ') i++;
		char kind = buf[i] ? buf[i++] : '?';
		while (buf[i] == ' ') i++;
		if (buf[i]) i++;					// state char
		while (buf[i] == ' ') i++;
		int pages = puint (buf, &i);
		while (buf[i] == ' ') i++;
		int n = 0; while (buf[i] && buf[i] != '\n' && n < 31) g_pname[g_np][n++] = buf[i++];
		g_pname[g_np][n] = '\0';
		if (buf[i] == '\n') i++;
		g_ppages[g_np] = pages; g_pkind[g_np] = kind; g_np++;
	}
	for (int a = 0; a < g_np; a++)			// selection sort, pages descending
	{
		int m = a;
		for (int b = a + 1; b < g_np; b++) if (g_ppages[b] > g_ppages[m]) m = b;
		if (m != a)
		{
			int tp = g_ppages[a]; g_ppages[a] = g_ppages[m]; g_ppages[m] = tp;
			char tk = g_pkind[a]; g_pkind[a] = g_pkind[m]; g_pkind[m] = tk;
			char tn[32];
			for (int k = 0; k < 32; k++) tn[k] = g_pname[a][k];
			for (int k = 0; k < 32; k++) g_pname[a][k] = g_pname[m][k];
			for (int k = 0; k < 32; k++) g_pname[m][k] = tn[k];
		}
	}
}

static void redraw (void)
{
	unsigned long total = 0, freekb = 0, appkb = 0; unsigned pagekb = 0;
	kapi_meminfo (&total, &freekb, &appkb, &pagekb);
	unsigned long used = total > freekb ? total - freekb : 0;

	// ABI v33: physical RAM detected by the firmware, the app page pool (HIGH zone)
	// total/free, the bytes reclaimed above 4GB, and the high-segment count.
	unsigned long detected = 0, apppool = 0, appfree = 0, above4g = 0; unsigned nsegs = 0;
	kapi_ram_detail (&detected, &apppool, &appfree, &above4g, &nsegs);

	using namespace wtk;
	g_cv.clear (C_BG);
	wk_rbox (g_cv, 0, 0, W, 24, 0, wk_tone (C_FACE, 170), wk_tone (C_FACE, 130));	// the header
	wk_etch_h (g_cv, 0, 24, W, C_FACE);
	wk_text_l (g_cv, 8, 0, 24, "Memory monitor", C_TEXT, 2);

	// usage bar (used / total): a sunken track, the used part in its own colour
	int bx = 8, by = 30, bw = W - 16, bh = 22;
	wk_sunken (g_cv, bx, by, bw, bh, 5, C_FIELD);
	int fillw = total ? (int) ((unsigned long) (bw - 2) * used / total) : 0;
	if (fillw > 0) wk_rbox (g_cv, bx + 1, by + 1, fillw, bh - 2, 4, wk_tone (USED, 160), wk_tone (USED, 112));
	int pc = total ? (int) (used * 100 / total) : 0;
	char pct[8]; ax_itoa (pc, pct);
	char pl[12]; int q = 0; for (int i = 0; pct[i]; i++) pl[q++] = pct[i]; pl[q++] = '%'; pl[q] = '\0';
	int tx = bx + bw / 2 - 12;					// its ink: on the fill, or on the track
	g_cv.text (tx, by + (bh - g_fh) / 2, pl, fillw > tx + 16 - bx ? wk_ink_on (USED) : C_FIELD_TEXT);

	int y = 62;
	kv (8, y, "Detected: ", detected / 1024, "MB");  y += g_fh;	// physical board RAM
	kv (8, y, "Managed:  ", total / 1024,    "MB");  y += g_fh;	// low + full high zone
	kv (8, y, "App pool: ", apppool / 1024,  "MB");  y += g_fh;	// zone backing app frames
	kv (8, y, "App free: ", appfree / 1024,  "MB");  y += g_fh;
	kv (8, y, "Above 4G: ", above4g / 1024,  "MB");  y += g_fh;	// RAM reclaimed > 4 GB
	kv (8, y, "Segments: ", (unsigned long) nsegs, ""); y += g_fh;	// high-zone segment count
	kv (8, y, "Free:     ", freekb / 1024,   "MB");  y += g_fh;
	kv (8, y, "Page:     ", pagekb,          "KB");  y += g_fh + 6;

	wk_text_l (g_cv, 8, y, g_fh, "By pages owned:", C_TEXT, 2); y += g_fh + 4;
	wk_sunken (g_cv, 4, y - 3, W - 8, H - y - 1, 4, C_FIELD);	// the processes: a field
	unsigned dim = wk_mix (C_FIELD, C_FIELD_TEXT, 130);
	for (int i = 0; i < g_np && y < H - g_fh - 4; i++)
	{
		if (g_ppages[i] == 0 && g_pkind[i] == 'k') continue;	// skip 0-page kernel tasks
		char num[12]; ax_itoa (g_ppages[i], num);
		char line[72]; int p = 0;
		int nl = 0; while (num[nl]) nl++;
		for (int s = 0; s < 4 - nl; s++) line[p++] = ' ';
		for (int s = 0; num[s]; s++) line[p++] = num[s];
		line[p++] = 'p'; line[p++] = ' '; line[p++] = ' ';
		for (int s = 0; g_pname[i][s] && p < 68; s++) line[p++] = g_pname[i][s];
		line[p] = '\0';
		g_cv.text (8, y, line, g_pkind[i] == 'k' ? dim : C_FIELD_TEXT);
		y += g_fh;
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "memmon");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();			// (reads the theme: the palette)
	g_cv.adopt (fb, W, H);
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	int tick = 0;
	while (!should_exit ())
	{
		pump_events ();
		if (tick % 10 == 0) { parse_procs (); redraw (); }	// ~1 s (10 x 100 ms)
		tick++;
		present ();				// (the frame drawn into the canvas: shown -- the compositor redraws only what it is told)
		msleep (100);
	}
	return 0;
}
