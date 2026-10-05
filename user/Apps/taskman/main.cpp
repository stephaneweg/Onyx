//
// taskman -- the Task Manager, in tabs (as Windows' own):
//   Processes  every task in a grid that scrolls (uikit's DataGrid): its name, an app or a kernel task,
//              its state, the memory it owns, an app's system calls per second (kapi v74 proc_stats);
//              a click on a title sorts. Up / Down select; Enter (a double click, Bring to Front)
//              raises an app's window; k or Delete (End Task) stops the selected app (kernel tasks
//              are protected); r refreshes now.
//   Memory     what the Memory Monitor showed, drawn: the memory in use, free, the apps', the
//              system's; the use over the last minute; what uses it (the system, the largest apps).
//   Processor  a panel a core: what it does (the system's, the sound's, an app core and the app that
//              holds it, the network's), its load now and over the last minute (kapi v80 cpu_stats).
//   Network    the bytes received and sent, the two rates now and over the last minute, then the
//              apps that use the network: their bytes, their rates (kapi v80 net_stats).
// Everything from kapi_list_procs ("<pid> <a|k> <state> <pages> <name>" a line), kapi_meminfo and
// kapi_ram_detail, read twice a second; the loads and the rates are a second's. The window resizes:
// the views follow. On a kernel older than v80 the last two tabs stay grey.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "ft/uikitface.h"			// FreeType's text (DejaVu Sans) for every widget
#include "applib.h"
#include <stdio.h>
#include <string.h>

using namespace uikit;

#define W	560
#define H	460
#define TOP	46
#define FOOT	44
#define MAXP	192
#define NAMEL	32
#define HIST	60			// the memory's samples kept: one a second

struct Proc { int pid; char kind, state; int pages, rate; char name[NAMEL]; unsigned long long rx, tx; int rxRate, txRate; };
static Proc g_p[MAXP]; static int g_np = 0;
static int  g_order[MAXP];		// the grid's rows -> g_p, sorted
static int  g_sortCol = 3; static bool g_sortDesc = true;	// (the memory, the largest first)
static unsigned long g_total, g_free, g_apps, g_detected, g_pool, g_poolFree, g_above4g;	// KB
static unsigned g_pageKb = 64, g_segs;
static int  g_hist[HIST], g_nhist = 0;	// the memory in use, per thousand of the total
static int  g_tab = 0;
// the cores (kapi v80): the load over the last second, per thousand; a minute of them
static bool g_cpuOk, g_netOk;
static struct kapi_cpu_stats g_cpu, g_cpuPrev;
static int  g_load[KAPI_CPU_CORES], g_cpuHist[KAPI_CPU_CORES][HIST], g_ncpuHist = 0;
// the network: the totals, the rates over the last second (bytes a second); a minute of them
static struct kapi_net_stats g_net, g_netPrev;
static unsigned long long g_netPrevUs;
static int  g_rxRate, g_txRate, g_rxHist[HIST], g_txHist[HIST], g_nnetHist = 0;
static struct { int pid; unsigned long long rx, tx; int rxRate, txRate; } g_pn[MAXP];	// by process, a second ago
static int  g_npn = 0;
static char g_netLine[96];

class MemView; class CpuView; class NetView;
static DataGrid *g_grid;
static MemView *g_mem;
static CpuView *g_cpuView;
static NetView *g_netView;
static SegmentedControl *g_tabs;
static Button *g_btRaise, *g_btKill;
static Label *g_lbInfo;

// ---- the data ----------------------------------------------------------------------------------------------
static int cmp (const Proc &a, const Proc &b)
{
	switch (g_sortCol)
	{
	case 1: if (a.kind != b.kind) return a.kind < b.kind ? -1 : 1; break;
	case 2: if (a.state != b.state) return a.state < b.state ? -1 : 1; break;
	case 3: if (a.pages != b.pages) return a.pages < b.pages ? -1 : 1; break;
	case 4: if (a.rate != b.rate) return a.rate < b.rate ? -1 : 1; break;
	}
	for (int i = 0; ; i++)				// (by name, whatever the case)
	{
		char x = a.name[i], y = b.name[i];
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return x < y ? -1 : 1;
		if (!x) return 0;
	}
}
static void sort_rows (void)
{
	for (int i = 0; i < g_np; i++) g_order[i] = i;
	for (int i = 1; i < g_np; i++)
		for (int j = i; j > 0; j--)
		{
			int c = cmp (g_p[g_order[j - 1]], g_p[g_order[j]]);
			if (g_sortDesc ? c >= 0 : c <= 0) break;	// (in order already)
			int t = g_order[j]; g_order[j] = g_order[j - 1]; g_order[j - 1] = t;
		}
}
static void read_data (void)
{
	static char buf[16384];
	kapi_list_procs (buf, sizeof buf);
	g_np = 0;
	for (int i = 0; buf[i] && g_np < MAXP; )
	{
		Proc &p = g_p[g_np];
		p.pid = 0; while (buf[i] >= '0' && buf[i] <= '9') p.pid = p.pid * 10 + (buf[i++] - '0');
		while (buf[i] == ' ') i++;
		p.kind = buf[i] && buf[i] != '\n' ? buf[i++] : '?';
		while (buf[i] == ' ') i++;
		p.state = buf[i] && buf[i] != '\n' ? buf[i++] : '?';
		while (buf[i] == ' ') i++;
		p.pages = 0; while (buf[i] >= '0' && buf[i] <= '9') p.pages = p.pages * 10 + (buf[i++] - '0');
		while (buf[i] == ' ') i++;
		int n = 0; while (buf[i] && buf[i] != '\n') { if (n < NAMEL - 1) p.name[n++] = buf[i]; i++; }
		p.name[n] = '\0';
		if (buf[i] == '\n') i++;
		p.rate = -1;
		struct kapi_syscall_stats ss;
		if (p.kind == 'a' && p.pid > 0 && kapi_proc_stats (p.pid, &ss) == 0) p.rate = (int) ss.rate;
		p.rx = p.tx = 0; p.rxRate = p.txRate = 0;
		struct kapi_net_stats ns;
		if (g_netOk && p.kind == 'a' && p.pid > 0 && kapi_net_stats (p.pid, &ns) == 0) { p.rx = ns.rx_bytes; p.tx = ns.tx_bytes; }
		for (int k = 0; k < g_npn; k++) if (g_pn[k].pid == p.pid) { p.rxRate = g_pn[k].rxRate; p.txRate = g_pn[k].txRate; break; }
		if (n) g_np++;
	}
	kapi_meminfo (&g_total, &g_free, &g_apps, &g_pageKb);
	if (!g_pageKb) g_pageKb = 64;
	kapi_ram_detail (&g_detected, &g_pool, &g_poolFree, &g_above4g, &g_segs);
}
// Once a second: the cores' load and the network's rates, from the counters a second ago.
static void push (int *h, int &n, int v) { if (n == HIST) { memmove (h, h + 1, (HIST - 1) * sizeof (int)); n--; } h[n++] = v; }
static int rate_of (unsigned long long now, unsigned long long was, unsigned long long us)
{
	if (now <= was || !us) return 0;
	unsigned long long r = (now - was) * 1000000ULL / us;
	return r > 0x7FFFFFFF ? 0x7FFFFFFF : (int) r;
}
static void sample_stats (void)
{
	if (g_cpuOk && kapi_cpu_stats (&g_cpu) == 0)
	{
		unsigned long long us = g_cpuPrev.now_us && g_cpu.now_us > g_cpuPrev.now_us ? g_cpu.now_us - g_cpuPrev.now_us : 0;
		int nh = g_ncpuHist;
		for (unsigned c = 0; c < g_cpu.cores && c < KAPI_CPU_CORES; c++)
		{
			unsigned long long b = g_cpu.core[c].busy_us, w = g_cpuPrev.core[c].busy_us;
			int v = us && b > w ? (int) ((b - w) * 1000 / us) : 0;
			g_load[c] = v > 1000 ? 1000 : v;
			if (us) { nh = g_ncpuHist; push (g_cpuHist[c], nh, g_load[c]); }
		}
		g_ncpuHist = nh;
		g_cpuPrev = g_cpu;
	}
	if (g_netOk && kapi_net_stats (0, &g_net) == 0)
	{
		unsigned long long now = g_cpu.now_us, us = g_netPrevUs && now > g_netPrevUs ? now - g_netPrevUs : 0;
		if (us)
		{
			g_rxRate = rate_of (g_net.rx_bytes, g_netPrev.rx_bytes, us);
			g_txRate = rate_of (g_net.tx_bytes, g_netPrev.tx_bytes, us);
			int n = g_nnetHist; push (g_rxHist, n, g_rxRate);
			push (g_txHist, g_nnetHist, g_txRate);
		}
		for (int i = 0; i < g_np; i++)			// each app: its rates, from its bytes a second ago
		{
			Proc &p = g_p[i];
			p.rxRate = p.txRate = 0;
			for (int k = 0; k < g_npn && us; k++)
				if (g_pn[k].pid == p.pid) { p.rxRate = rate_of (p.rx, g_pn[k].rx, us); p.txRate = rate_of (p.tx, g_pn[k].tx, us); break; }
		}
		g_npn = 0;
		for (int i = 0; i < g_np; i++)
			if (g_p[i].rx || g_p[i].tx) { g_pn[g_npn].pid = g_p[i].pid; g_pn[g_npn].rx = g_p[i].rx; g_pn[g_npn].tx = g_p[i].tx; g_pn[g_npn].rxRate = g_p[i].rxRate; g_pn[g_npn].txRate = g_p[i].txRate; g_npn++; }
		g_netPrev = g_net; g_netPrevUs = now;
		// the address and the name, for the foot
		static char info[2048];
		char ip[24] = "", host[40] = "";
		int n = kapi_net_info (info, sizeof info);
		if (n < 0) n = 0;
		info[n < (int) sizeof info ? n : (int) sizeof info - 1] = 0;
		for (char *l = info; l && *l; )
		{
			char *e = strchr (l, '\n'); if (e) *e = 0;
			if (!strncmp (l, "ip ", 3)) snprintf (ip, sizeof ip, "%s", l + 3);
			else if (!strncmp (l, "hostname ", 9)) snprintf (host, sizeof host, "%s", l + 9);
			l = e ? e + 1 : 0;
		}
		if (ip[0]) snprintf (g_netLine, sizeof g_netLine, "%s  -  %s  -  %u socket%s open", ip, host, g_net.sockets, g_net.sockets == 1 ? "" : "s");
		else snprintf (g_netLine, sizeof g_netLine, "The network is down");
	}
}
static void bytes_text (char *o, int cap, unsigned long long b)		// "812 bytes", "34.2 KB", "118 MB", "2.31 GB"
{
	if (b < 1024) snprintf (o, (size_t) cap, "%u bytes", (unsigned) b);
	else if (b < 1024 * 1024) snprintf (o, (size_t) cap, "%u.%u KB", (unsigned) (b / 1024), (unsigned) (b % 1024 * 10 / 1024));
	else if (b < 100ULL * 1024 * 1024) snprintf (o, (size_t) cap, "%u.%u MB", (unsigned) (b >> 20), (unsigned) ((b & 0xFFFFF) * 10 >> 20));
	else if (b < 1024ULL * 1024 * 1024) snprintf (o, (size_t) cap, "%u MB", (unsigned) (b >> 20));
	else snprintf (o, (size_t) cap, "%u.%02u GB", (unsigned) (b >> 30), (unsigned) ((b & 0x3FFFFFFF) * 100 >> 30));
}
static void rate_text (char *o, int cap, int r)				// "0", "850 B/s", "12.4 KB/s", "1.2 MB/s"
{
	if (r <= 0) snprintf (o, (size_t) cap, "0");
	else if (r < 1024) snprintf (o, (size_t) cap, "%d B/s", r);
	else if (r < 1024 * 1024) snprintf (o, (size_t) cap, "%d.%d KB/s", r / 1024, r % 1024 * 10 / 1024);
	else snprintf (o, (size_t) cap, "%d.%d MB/s", r >> 20, (r & 0xFFFFF) * 10 >> 20);
}
static unsigned long used_kb (void) { return g_total > g_free ? g_total - g_free : 0; }
static void mb (char *o, int cap, unsigned long kb)		// "118.4 MB" (a whole number from 100 MB)
{
	if (kb >= 102400) snprintf (o, (size_t) cap, "%lu MB", (kb + 512) / 1024);
	else snprintf (o, (size_t) cap, "%lu.%lu MB", kb / 1024, kb % 1024 * 10 / 1024);
}

// ---- Processes ---------------------------------------------------------------------------------------------
static const Proc *row_proc (int row) { return row >= 0 && row < g_np ? &g_p[g_order[row]] : 0; }
static const char *cell (DataGrid &, int row, int col, char *buf, int cap)
{
	const Proc *p = row_proc (row);
	if (!p) return "";
	switch (col)
	{
	case 0: return p->name;
	case 1: return p->kind == 'k' ? "Kernel" : "App";
	case 2: return p->state == 'R' ? "Running" : p->state == 'S' ? "Sleeping" : p->state == 'B' ? "Waiting" : p->state == 'N' ? "New" : "?";
	case 3: if (!p->pages) return ""; mb (buf, cap, (unsigned long) p->pages * g_pageKb); return buf;
	default: if (p->rate < 0) return ""; snprintf (buf, (size_t) cap, "%d", p->rate); return buf;
	}
}

// ---- Memory ------------------------------------------------------------------------------------------------
static const unsigned SEG_COL[6] = { 0x005E6FB8, 0x003D86DA, 0x004A9FB0, 0x005E9F58, 0x00B5A040, 0x00A0A0A8 };

class MemView : public Widget
{
public:
	MemView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void tile (int x, int y, int w, const char *label, unsigned long kb, const char *sub)
	{
		uk_rbox (canvas, x, y, w, 62, 6, C_FIELD, C_FIELD);
		uk_rline (canvas, x, y, w, 62, 6, uk_tone (C_BG, 72), 200);
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 130);
		uk_text_l (canvas, x + 10, y + 5, 18, label, dim);
		char v[24]; mb (v, sizeof v, kb);
		uk_text_l (canvas, x + 10, y + 22, 20, v, C_FIELD_TEXT, 2);
		if (sub) uk_text_l (canvas, x + 10, y + 41, 18, sub, dim);
	}
	void onDraw () override
	{
		unsigned bg = bgColor (), dim = uk_mix (bg, C_TEXT, 150);
		canvas.clear (bg);
		unsigned long used = used_kb (), sys = used > g_apps ? used - g_apps : 0;
		// the figures
		int gap = 10, tw = (width - 3 * gap) / 4;
		char pct[24]; snprintf (pct, sizeof pct, "%lu %% of %lu MB", g_total ? used * 100 / g_total : 0, g_total / 1024);
		tile (0, 0, tw, "In use", used, pct);
		tile (tw + gap, 0, tw, "Free", g_free, 0);
		tile (2 * (tw + gap), 0, tw, "Apps", g_apps, 0);
		tile (3 * (tw + gap), 0, width - 3 * (tw + gap), "System", sys, "kernel and GPU");
		// the use over the last minute
		int y = 74;
		uk_text_l (canvas, 2, y, 18, "MEMORY IN USE, THE LAST MINUTE", dim, 2); y += 22;
		int gh = height - y - 104; if (gh < 50) gh = 50;
		uk_sunken (canvas, 0, y, width, gh, 5, C_FIELD);
		unsigned line = uk_mix (C_FIELD, C_FIELD_TEXT, 22);
		for (int k = 1; k < 4; k++) canvas.fillRect (4, y + gh * k / 4, width - 8, 1, line);
		if (g_nhist > 1)
		{
			int iw = width - 12, ih = gh - 12, px = 0, py = 0;
			for (int i = 0; i < g_nhist; i++)
			{
				int x = 6 + (HIST - g_nhist + i) * iw / (HIST - 1), yy = y + 6 + ih - g_hist[i] * ih / 1000;
				canvas.fillRect (x - 1, yy, 3, y + 6 + ih - yy, uk_mix (C_FIELD, C_ACCENT, 46));
				if (i) { VPath p; p.line (V (px), V (py), V (x), V (yy), V (2)); p.fill (canvas, C_ACCENT, 255); }
				px = x; py = yy;
			}
		}
		y += gh + 10;
		// what uses it: the system, the four largest apps, the others -- then what is free
		uk_text_l (canvas, 2, y, 18, "WHAT USES IT", dim, 2); y += 22;
		int top[4], nt = 0;
		for (int k = 0; k < 4; k++)			// the largest app not taken yet, four times
		{
			int best = -1;
			for (int i = 0; i < g_np; i++)
			{
				if (g_p[i].kind != 'a' || !g_p[i].pages) continue;
				bool taken = false; for (int j = 0; j < nt; j++) if (top[j] == i) taken = true;
				if (!taken && (best < 0 || g_p[i].pages > g_p[best].pages)) best = i;
			}
			if (best < 0) break;
			top[nt++] = best;
		}
		unsigned long kb[6]; const char *nm[6]; int ns = 0;
		kb[ns] = sys; nm[ns++] = "System";
		unsigned long shown = 0;
		for (int k = 0; k < nt; k++) { kb[ns] = (unsigned long) g_p[top[k]].pages * g_pageKb; shown += kb[ns]; nm[ns++] = g_p[top[k]].name; }
		if (g_apps > shown) { kb[ns] = g_apps - shown; nm[ns++] = "the other apps"; }
		uk_rbox (canvas, 0, y, width, 18, 4, C_FIELD, C_FIELD);
		int x = 1;
		for (int k = 0; k < ns && g_total; k++)
		{
			int w = (int) ((unsigned long long) kb[k] * (width - 2) / g_total);
			if (w < 1 && kb[k]) w = 1;
			if (x + w > width - 1) w = width - 1 - x;
			if (w > 0) canvas.fillRect (x, y + 1, w, 16, SEG_COL[k]);
			x += w;
		}
		uk_rline (canvas, 0, y, width, 18, 4, uk_tone (C_BG, 72), 220);
		y += 26;
		int lx = 2;
		for (int k = 0; k < ns; k++)
		{
			char v[24], t[72]; mb (v, sizeof v, kb[k]); snprintf (t, sizeof t, "%s  %s", nm[k], v);
			int w = uk_text_w (t) + 30;
			if (lx + w > width && lx > 2) { lx = 2; y += 20; }
			uk_rbox (canvas, lx, y + 4, 10, 10, 2, SEG_COL[k], SEG_COL[k]);
			uk_text_l (canvas, lx + 16, y, 18, t, C_TEXT);
			lx += w;
		}
	}
};

// A minute of samples as a line over its filled area, in the box (x, y, w, h) already drawn: the newest
// at the right, `top` the value of the box's top.
static void plot (Canvas &cv, int x, int y, int w, int h, const int *hist, int n, int top, unsigned col, bool fill)
{
	if (n < 2 || top <= 0) return;
	int iw = w - 12, ih = h - 12, px = 0, py = 0;
	for (int i = 0; i < n; i++)
	{
		int v = hist[i] > top ? top : hist[i];
		int xx = x + 6 + (HIST - n + i) * iw / (HIST - 1), yy = y + 6 + ih - (int) ((long long) v * ih / top);
		if (fill) cv.fillRect (xx - 1, yy, 3, y + 6 + ih - yy, uk_mix (C_FIELD, col, 46));
		if (i) { VPath p; p.line (V (px), V (py), V (xx), V (yy), V (2)); p.fill (cv, col, 255); }
		px = xx; py = yy;
	}
}
static void plot_box (Canvas &cv, int x, int y, int w, int h)
{
	uk_sunken (cv, x, y, w, h, 5, C_FIELD);
	unsigned line = uk_mix (C_FIELD, C_FIELD_TEXT, 22);
	for (int k = 1; k < 4; k++) cv.fillRect (x + 4, y + h * k / 4, w - 8, 1, line);
}
static void text_r (Canvas &cv, int right, int y, int h, const char *t, unsigned c, int style = 0)
{
	uk_text_l (cv, right - uk_text_w (t, style), y, h, t, c, style);
}

// ---- Processor ---------------------------------------------------------------------------------------------
class CpuView : public Widget
{
public:
	CpuView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		unsigned bg = bgColor (), dim = uk_mix (bg, C_TEXT, 150);
		canvas.clear (bg);
		int n = (int) g_cpu.cores; if (n > KAPI_CPU_CORES) n = KAPI_CPU_CORES;
		if (n <= 0) { uk_text_c (canvas, 0, 0, width, height, "No core to show", dim); return; }
		int cols = n > 1 ? 2 : 1, rows = (n + cols - 1) / cols, gap = 10;
		int cw = (width - (cols - 1) * gap) / cols, ch = (height - (rows - 1) * gap) / rows;
		if (ch < 96) ch = 96;
		for (int c = 0; c < n; c++)
		{
			int x = (c % cols) * (cw + gap), y = (c / cols) * (ch + gap);
			const struct kapi_cpu_core &k = g_cpu.core[c];
			char t[24], what[80];
			snprintf (t, sizeof t, "Core %d", c);
			uk_text_l (canvas, x + 2, y, 20, t, C_TEXT, 2);
			switch (k.role)
			{
			case KAPI_CORE_SYSTEM:	snprintf (what, sizeof what, "the system and every app"); break;
			case KAPI_CORE_SOUND:	snprintf (what, sizeof what, "the sound"); break;
			case KAPI_CORE_NETWORK:	snprintf (what, sizeof what, "the network"); break;
			default:
			{
				const char *owner = 0;
				for (int i = 0; i < g_np && k.pid; i++) if (g_p[i].pid == (int) k.pid) { owner = g_p[i].name; break; }
				if (owner) snprintf (what, sizeof what, "an app core: %.40s", owner);
				else snprintf (what, sizeof what, k.pid ? "an app core: in use" : "an app core: free");
			}
			}
			uk_text_l (canvas, x + 2 + uk_text_w (t, 2) + 10, y + 1, 18, what, dim);
			char pct[16]; snprintf (pct, sizeof pct, "%d %%", (g_load[c] + 5) / 10);
			text_r (canvas, x + cw - 2, y, 20, pct, C_TEXT, 2);
			int gy = y + 24, gh = ch - 24;
			plot_box (canvas, x, gy, cw, gh);
			plot (canvas, x, gy, cw, gh, g_cpuHist[c], g_ncpuHist, 1000, k.role == KAPI_CORE_NETWORK ? uk_mix (C_FIELD, C_ACCENT, 120) : C_ACCENT, true);
		}
	}
};

// ---- Network -----------------------------------------------------------------------------------------------
#define C_SENT	0x00D08A30		// what is sent: its line, its figures' mark

class NetView : public Widget
{
public:
	NetView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void tile (int x, int y, int w, const char *label, const char *value, const char *sub, unsigned mark)
	{
		uk_rbox (canvas, x, y, w, 62, 6, C_FIELD, C_FIELD);
		uk_rline (canvas, x, y, w, 62, 6, uk_tone (C_BG, 72), 200);
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 130);
		uk_text_l (canvas, x + 10, y + 5, 18, label, dim);
		if (mark) uk_rbox (canvas, x + w - 20, y + 9, 10, 10, 2, mark, mark);
		uk_text_l (canvas, x + 10, y + 22, 20, value, C_FIELD_TEXT, 2);
		if (sub) uk_text_l (canvas, x + 10, y + 41, 18, sub, dim);
	}
	void onDraw () override
	{
		unsigned bg = bgColor (), dim = uk_mix (bg, C_TEXT, 150);
		canvas.clear (bg);
		char a[24], b[24];
		int gap = 10, tw = (width - 3 * gap) / 4;
		rate_text (a, sizeof a, g_rxRate); tile (0, 0, tw, "Receiving", a, 0, C_ACCENT);
		rate_text (a, sizeof a, g_txRate); tile (tw + gap, 0, tw, "Sending", a, 0, C_SENT);
		bytes_text (a, sizeof a, g_net.rx_bytes); tile (2 * (tw + gap), 0, tw, "Received", a, "since the start", 0);
		bytes_text (a, sizeof a, g_net.tx_bytes); tile (3 * (tw + gap), 0, width - 3 * (tw + gap), "Sent", a, "since the start", 0);
		// the two rates over the last minute, to the largest of them (64 KB/s at least)
		int y = 74;
		int top = 64 * 1024;
		for (int i = 0; i < g_nnetHist; i++) { if (g_rxHist[i] > top) top = g_rxHist[i]; if (g_txHist[i] > top) top = g_txHist[i]; }
		uk_text_l (canvas, 2, y, 18, "THE LAST MINUTE", dim, 2);
		rate_text (a, sizeof a, top); snprintf (b, sizeof b, "top: %s", a);
		text_r (canvas, width - 2, y, 18, b, dim);
		y += 22;
		// the apps: as many rows as fit under a graph of 70 pixels at least
		int ord[MAXP], no = 0;
		for (int i = 0; i < g_np; i++) if (g_p[i].rx || g_p[i].tx) ord[no++] = i;
		for (int i = 1; i < no; i++)				// the busiest now first, then the most bytes
			for (int j = i; j > 0; j--)
			{
				const Proc &p = g_p[ord[j - 1]], &q = g_p[ord[j]];
				long long pr = (long long) p.rxRate + p.txRate, qr = (long long) q.rxRate + q.txRate;
				if (pr > qr || (pr == qr && p.rx + p.tx >= q.rx + q.tx)) break;
				int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t;
			}
		int rowH = 20, listH = 22 + 20 + (no ? no : 1) * rowH;
		int gh = height - y - 10 - listH;
		if (gh < 70) { gh = 70; }
		plot_box (canvas, 0, y, width, gh);
		plot (canvas, 0, y, width, gh, g_rxHist, g_nnetHist, top, C_ACCENT, true);
		plot (canvas, 0, y, width, gh, g_txHist, g_nnetHist, top, C_SENT, false);
		y += gh + 10;
		uk_text_l (canvas, 2, y, 18, "BY APP", dim, 2); y += 22;
		int c4 = width - 2, c3 = c4 - 92, c2 = c3 - 92, c1 = c2 - 92;		// the columns' right edges
		if (c1 < 150) { c1 = c2; }						// (a narrow window: no "Received" column)
		uk_text_l (canvas, 2, y, 18, "App", dim);
		if (c1 != c2) text_r (canvas, c1, y, 18, "Received", dim);
		text_r (canvas, c2, y, 18, c1 != c2 ? "Sent" : "Received", dim);
		text_r (canvas, c3, y, 18, "Receiving", dim);
		text_r (canvas, c4, y, 18, "Sending", dim);
		y += 20;
		canvas.fillRect (0, y - 2, width, 1, uk_tone (bg, 100));
		if (!no) { uk_text_l (canvas, 2, y, 18, "No app has used the network", dim); return; }
		for (int i = 0; i < no && y + rowH <= height; i++, y += rowH)
		{
			const Proc &p = g_p[ord[i]];
			uk_text_l (canvas, 2, y, 18, p.name, C_TEXT);
			if (c1 != c2) { bytes_text (a, sizeof a, p.rx); text_r (canvas, c1, y, 18, a, C_TEXT); bytes_text (a, sizeof a, p.tx); }
			else bytes_text (a, sizeof a, p.rx);
			text_r (canvas, c2, y, 18, a, C_TEXT);
			rate_text (a, sizeof a, p.rxRate); text_r (canvas, c3, y, 18, a, p.rxRate ? C_TEXT : dim);
			rate_text (a, sizeof a, p.txRate); text_r (canvas, c4, y, 18, a, p.txRate ? C_TEXT : dim);
		}
	}
};

// ---- the window --------------------------------------------------------------------------------------------
static void show_tab (void)
{
	bool procs = g_tab == 0;
	g_grid->hidden = !procs; ((Widget *) g_mem)->hidden = g_tab != 1;
	((Widget *) g_cpuView)->hidden = g_tab != 2; ((Widget *) g_netView)->hidden = g_tab != 3;
	g_btRaise->hidden = g_btKill->hidden = !procs;
	if (g_grid->parent) g_grid->parent->invalidate (true);
}
static void refresh (bool sample)
{
	int keep = -1; const Proc *was = row_proc (g_grid->sel);
	if (was) keep = was->pid;
	char keepName[NAMEL] = ""; if (was) strcpy (keepName, was->name);
	read_data ();
	sort_rows ();
	if (sample && g_total)
	{
		if (g_nhist == HIST) { memmove (g_hist, g_hist + 1, (HIST - 1) * sizeof (int)); g_nhist--; }
		g_hist[g_nhist++] = (int) (used_kb () * 1000 / g_total);
	}
	if (sample) sample_stats ();
	g_grid->setRows (g_np);
	int s = -1;
	for (int r = 0; r < g_np; r++) { const Proc *p = row_proc (r); if (p->pid == keep && !strcmp (p->name, keepName)) { s = r; break; } }
	g_grid->sel = s;
	g_grid->sortCol = g_sortCol; g_grid->sortDesc = g_sortDesc;
	g_grid->invalidate (true);
	((Widget *) g_mem)->invalidate (true);
	((Widget *) g_cpuView)->invalidate (true);
	((Widget *) g_netView)->invalidate (true);
	const Proc *p = row_proc (s);
	bool app = p && p->kind == 'a';
	g_btRaise->disabled = g_btKill->disabled = !app;
	g_btRaise->invalidate (true); g_btKill->invalidate (true);
	char t[96];
	if (g_tab == 0) snprintf (t, sizeof t, "%d task%s", g_np, g_np == 1 ? "" : "s");
	else if (g_tab == 2) snprintf (t, sizeof t, "%u cores  -  the load over the last second, and the last minute", g_cpu.cores);
	else if (g_tab == 3) snprintf (t, sizeof t, "%s", g_netLine);
	else snprintf (t, sizeof t, "Detected %lu MB  -  the apps' pool %lu MB, %lu free  -  pages of %u KB", g_detected / 1024, g_pool / 1024, g_poolFree / 1024, g_pageKb);
	if (strcmp (t, g_lbInfo->text)) g_lbInfo->setText (t);
}
static void do_raise (void) { const Proc *p = row_proc (g_grid->sel); if (p && p->kind == 'a') kapi_raise_app (p->name); }
static void do_kill (void) { const Proc *p = row_proc (g_grid->sel); if (p && p->kind == 'a') { kapi_kill (p->name); refresh (false); } }
static void on_raise (Widget &) { do_raise (); g_grid->setFocus (); }
static void on_kill (Widget &) { do_kill (); g_grid->setFocus (); }
static void on_select (Widget &) { refresh (false); }
static void on_sort (Widget &)					// a title clicked: that column, or the other way round
{
	int c = g_grid->clickedCol;
	if (c == g_sortCol) g_sortDesc = !g_sortDesc; else { g_sortCol = c; g_sortDesc = c >= 3; }
	refresh (false);
}
static void on_tab (Widget &) { if (g_tabs->selected >= 0) { g_tab = g_tabs->selected; show_tab (); refresh (false); } }

class TaskRoot : public Root
{
public:
	int frames, half;
	TaskRoot () : Root (W, H, "Task Manager"), frames (0), half (0) {}
	void onDraw () override { Root::onDraw (); canvas.fillRect (0, TOP - 1, width, 1, uk_tone (bg, 100)); }
	void onTick () override { if (++frames >= 30) { frames = 0; half ^= 1; refresh (half == 0); } }	// twice a second; a sample a second
	bool onKey (long k) override
	{
		if (g_tab != 0) return false;
		switch (k)
		{
		case 'k': case 'K': case KEY_DEL: do_kill (); return true;
		case 'r': case 'R': refresh (false); return true;
		case KEY_ENTER: do_raise (); return true;
		}
		return ((Widget *) g_grid)->onKey (k);
	}
};

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (FreeType's text: uk_fw / uk_fh follow it)
	TaskRoot root;
	if (root.canvas.px == 0) return 1;

	static const char *const TABS[4] = { "Processes", "Memory", "Processor", "Network" };
	g_tabs = new SegmentedControl (10, 10, 400, 28, TABS, 4, 0, on_tab);
	g_cpuOk = kapi_cpu_stats (&g_cpu) == 0; g_cpuPrev = g_cpu;	// (kapi v80: an older kernel has neither)
	g_netOk = kapi_net_stats (0, &g_net) == 0; g_netPrev = g_net; g_netPrevUs = g_cpu.now_us;
	g_tabs->setEnabled (2, g_cpuOk); g_tabs->setEnabled (3, g_netOk);
	root.addChild (g_tabs);

	g_grid = new DataGrid (10, TOP + 8, W - 20, H - TOP - FOOT - 8);
	g_grid->anchor = ANCHOR_FILL;
	g_grid->setColumns (5);
	g_grid->setColumn (0, "Task", 176);
	g_grid->setColumn (1, "Kind", 70);
	g_grid->setColumn (2, "State", 84);
	g_grid->setColumn (3, "Memory", 100, GRID_RIGHT);
	g_grid->setColumn (4, "Calls / s", 80, GRID_RIGHT);
	g_grid->cellText = cell;
	g_grid->sortable = true; g_grid->onSort = on_sort;
	g_grid->onSelect = on_select;
	g_grid->onActivate = on_raise;
	g_grid->emptyText = "No task";
	root.addChild (g_grid);

	g_mem = new MemView (10, TOP + 8, W - 20, H - TOP - FOOT - 8);
	g_mem->anchor = ANCHOR_FILL;
	root.addChild (g_mem);

	g_cpuView = new CpuView (10, TOP + 8, W - 20, H - TOP - FOOT - 8);
	g_cpuView->anchor = ANCHOR_FILL;
	root.addChild (g_cpuView);
	g_netView = new NetView (10, TOP + 8, W - 20, H - TOP - FOOT - 8);
	g_netView->anchor = ANCHOR_FILL;
	root.addChild (g_netView);

	g_lbInfo = new Label (12, H - FOOT + 12, W - 24, 22, "", C_DIS, root.bg);
	g_lbInfo->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM | ANCHOR_RIGHT;
	root.addChild (g_lbInfo);
	g_btRaise = new Button (W - 10 - 2 * 124 - 6, H - FOOT + 7, 124, 30, "Bring to Front", on_raise);
	g_btRaise->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM; g_btRaise->tip = "The app's window in front (Enter, a double click)";
	root.addChild (g_btRaise);
	g_btKill = new Button (W - 10 - 124, H - FOOT + 7, 124, 30, "End Task", on_kill);
	g_btKill->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM; g_btKill->tip = "Stop the app (k, Delete); a kernel task cannot be stopped";
	root.addChild (g_btKill);

	root.setResizable (true);
	show_tab ();
	refresh (true);
	g_grid->setFocus ();
	root.run ();
	return 0;
}
