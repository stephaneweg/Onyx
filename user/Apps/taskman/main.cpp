//
// taskman -- the Task Manager, in tabs (as Windows' own):
//   Processes  every task in a grid that scrolls (wtk's DataGrid): its name, an app or a kernel task,
//              its state, the memory it owns, an app's system calls per second (kapi v74 proc_stats);
//              a click on a title sorts. Up / Down select; Enter (a double click, Bring to Front)
//              raises an app's window; k or Delete (End Task) stops the selected app (kernel tasks
//              are protected); r refreshes now.
//   Memory     what the Memory Monitor showed, drawn: the memory in use, free, the apps', the
//              system's; the use over the last minute; what uses it (the system, the largest apps).
//   Processor, Network: to come with the kernel's counters (the cores' load, the bytes by app).
// Everything from kapi_list_procs ("<pid> <a|k> <state> <pages> <name>" a line), kapi_meminfo and
// kapi_ram_detail, read twice a second. The window resizes: the views follow.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "ft/wtkface.h"			// FreeType's text (DejaVu Sans) for every widget
#include "applib.h"
#include <stdio.h>
#include <string.h>

using namespace wtk;

#define W	560
#define H	460
#define TOP	46
#define FOOT	44
#define MAXP	192
#define NAMEL	32
#define HIST	60			// the memory's samples kept: one a second

struct Proc { int pid; char kind, state; int pages, rate; char name[NAMEL]; };
static Proc g_p[MAXP]; static int g_np = 0;
static int  g_order[MAXP];		// the grid's rows -> g_p, sorted
static int  g_sortCol = 3; static bool g_sortDesc = true;	// (the memory, the largest first)
static unsigned long g_total, g_free, g_apps, g_detected, g_pool, g_poolFree, g_above4g;	// KB
static unsigned g_pageKb = 64, g_segs;
static int  g_hist[HIST], g_nhist = 0;	// the memory in use, per thousand of the total
static int  g_tab = 0;

class MemView;
static DataGrid *g_grid;
static MemView *g_mem;
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
		if (n) g_np++;
	}
	kapi_meminfo (&g_total, &g_free, &g_apps, &g_pageKb);
	if (!g_pageKb) g_pageKb = 64;
	kapi_ram_detail (&g_detected, &g_pool, &g_poolFree, &g_above4g, &g_segs);
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
		wk_rbox (canvas, x, y, w, 62, 6, C_FIELD, C_FIELD);
		wk_rline (canvas, x, y, w, 62, 6, wk_tone (C_BG, 72), 200);
		unsigned dim = wk_mix (C_FIELD, C_FIELD_TEXT, 130);
		wk_text_l (canvas, x + 10, y + 5, 18, label, dim);
		char v[24]; mb (v, sizeof v, kb);
		wk_text_l (canvas, x + 10, y + 22, 20, v, C_FIELD_TEXT, 2);
		if (sub) wk_text_l (canvas, x + 10, y + 41, 18, sub, dim);
	}
	void onDraw () override
	{
		unsigned bg = bgColor (), dim = wk_mix (bg, C_TEXT, 150);
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
		wk_text_l (canvas, 2, y, 18, "MEMORY IN USE, THE LAST MINUTE", dim, 2); y += 22;
		int gh = height - y - 104; if (gh < 50) gh = 50;
		wk_sunken (canvas, 0, y, width, gh, 5, C_FIELD);
		unsigned line = wk_mix (C_FIELD, C_FIELD_TEXT, 22);
		for (int k = 1; k < 4; k++) canvas.fillRect (4, y + gh * k / 4, width - 8, 1, line);
		if (g_nhist > 1)
		{
			int iw = width - 12, ih = gh - 12, px = 0, py = 0;
			for (int i = 0; i < g_nhist; i++)
			{
				int x = 6 + (HIST - g_nhist + i) * iw / (HIST - 1), yy = y + 6 + ih - g_hist[i] * ih / 1000;
				canvas.fillRect (x - 1, yy, 3, y + 6 + ih - yy, wk_mix (C_FIELD, C_ACCENT, 46));
				if (i) { VPath p; p.line (V (px), V (py), V (x), V (yy), V (2)); p.fill (canvas, C_ACCENT, 255); }
				px = x; py = yy;
			}
		}
		y += gh + 10;
		// what uses it: the system, the four largest apps, the others -- then what is free
		wk_text_l (canvas, 2, y, 18, "WHAT USES IT", dim, 2); y += 22;
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
		wk_rbox (canvas, 0, y, width, 18, 4, C_FIELD, C_FIELD);
		int x = 1;
		for (int k = 0; k < ns && g_total; k++)
		{
			int w = (int) ((unsigned long long) kb[k] * (width - 2) / g_total);
			if (w < 1 && kb[k]) w = 1;
			if (x + w > width - 1) w = width - 1 - x;
			if (w > 0) canvas.fillRect (x, y + 1, w, 16, SEG_COL[k]);
			x += w;
		}
		wk_rline (canvas, 0, y, width, 18, 4, wk_tone (C_BG, 72), 220);
		y += 26;
		int lx = 2;
		for (int k = 0; k < ns; k++)
		{
			char v[24], t[72]; mb (v, sizeof v, kb[k]); snprintf (t, sizeof t, "%s  %s", nm[k], v);
			int w = wk_text_w (t) + 30;
			if (lx + w > width && lx > 2) { lx = 2; y += 20; }
			wk_rbox (canvas, lx, y + 4, 10, 10, 2, SEG_COL[k], SEG_COL[k]);
			wk_text_l (canvas, lx + 16, y, 18, t, C_TEXT);
			lx += w;
		}
	}
};

// ---- the window --------------------------------------------------------------------------------------------
static void show_tab (void)
{
	bool procs = g_tab == 0;
	g_grid->hidden = !procs; ((Widget *) g_mem)->hidden = procs;
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
	g_grid->setRows (g_np);
	int s = -1;
	for (int r = 0; r < g_np; r++) { const Proc *p = row_proc (r); if (p->pid == keep && !strcmp (p->name, keepName)) { s = r; break; } }
	g_grid->sel = s;
	g_grid->sortCol = g_sortCol; g_grid->sortDesc = g_sortDesc;
	g_grid->invalidate (true);
	((Widget *) g_mem)->invalidate (true);
	const Proc *p = row_proc (s);
	bool app = p && p->kind == 'a';
	g_btRaise->disabled = g_btKill->disabled = !app;
	g_btRaise->invalidate (true); g_btKill->invalidate (true);
	char t[96];
	if (g_tab == 0) snprintf (t, sizeof t, "%d task%s", g_np, g_np == 1 ? "" : "s");
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
	void onDraw () override { Root::onDraw (); canvas.fillRect (0, TOP - 1, width, 1, wk_tone (bg, 100)); }
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
	ft_wtk_install ("DejaVu Sans", 13);		// (FreeType's text: wk_fw / wk_fh follow it)
	TaskRoot root;
	if (root.canvas.px == 0) return 1;

	static const char *const TABS[4] = { "Processes", "Memory", "Processor", "Network" };
	g_tabs = new SegmentedControl (10, 10, 400, 28, TABS, 4, 0, on_tab);
	g_tabs->setEnabled (2, false); g_tabs->setEnabled (3, false);	// (with the kernel's counters)
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
