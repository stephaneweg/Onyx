//
// memmon -- a memory monitor. Shows total / used / free RAM (kapi_meminfo), the
// memory owned by user apps, a usage bar, and the processes ranked by the number of
// 64 KB pages they own (kapi_list_procs). Refreshes ~once a second. Canvas-drawn
// (no widgets needed -- it's a read-only display).
//
#include "kapi.h"
#include "uikit.hpp"
#include "applib.h"

#define W	440
#define H	380
#define MAXP	40

static unsigned *fb;
static int g_fw = 8, g_fh = 16;

static void fill (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}
static void frame (int x, int y, int w, int h, unsigned c)
{ fill (x, y, w, 1, c); fill (x, y + h - 1, w, 1, c); fill (x, y, 1, h, c); fill (x + w - 1, y, 1, h, c); }

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
	kapi_draw_text (x, y, line, 0x00d0d8e0);
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

	fill (0, 0, W, H, 0x00181c24);
	kapi_draw_text (8, 8, "Memory monitor", 0x00ffffff);

	// usage bar (used / total)
	int bx = 8, by = 30, bw = W - 16, bh = 22;
	fill (bx, by, bw, bh, 0x00283040); frame (bx, by, bw, bh, 0x00404a5a);
	int fillw = total ? (int) ((unsigned long) (bw - 2) * used / total) : 0;
	fill (bx + 1, by + 1, fillw, bh - 2, 0x00d08040);
	int pc = total ? (int) (used * 100 / total) : 0;
	char pct[8]; ax_itoa (pc, pct);
	char pl[12]; int q = 0; for (int i = 0; pct[i]; i++) pl[q++] = pct[i]; pl[q++] = '%'; pl[q] = '\0';
	kapi_draw_text (bx + bw / 2 - 12, by + (bh - g_fh) / 2, pl, 0x00ffffff);

	int y = 62;
	kv (8, y, "Detected: ", detected / 1024, "MB");  y += g_fh;	// physical board RAM
	kv (8, y, "Managed:  ", total / 1024,    "MB");  y += g_fh;	// low + full high zone
	kv (8, y, "App pool: ", apppool / 1024,  "MB");  y += g_fh;	// zone backing app frames
	kv (8, y, "App free: ", appfree / 1024,  "MB");  y += g_fh;
	kv (8, y, "Above 4G: ", above4g / 1024,  "MB");  y += g_fh;	// RAM reclaimed > 4 GB
	kv (8, y, "Segments: ", (unsigned long) nsegs, ""); y += g_fh;	// high-zone segment count
	kv (8, y, "Free:     ", freekb / 1024,   "MB");  y += g_fh;
	kv (8, y, "Page:     ", pagekb,          "KB");  y += g_fh + 6;

	kapi_draw_text (8, y, "By pages owned:", 0x0090a0b0); y += g_fh;
	for (int i = 0; i < g_np && y < H - g_fh; i++)
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
		kapi_draw_text (8, y, line, g_pkind[i] == 'k' ? 0x00808890 : 0x00c8d0c0);
		y += g_fh;
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "memmon");
	if (fb == 0) return 1;
	ui::decorate_window ();
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	int tick = 0;
	while (!should_exit ())
	{
		pump_events ();
		if (tick % 10 == 0) { parse_procs (); redraw (); }	// ~1 s (10 x 100 ms)
		tick++;
		msleep (100);
	}
	return 0;
}
