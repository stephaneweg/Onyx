//
// taskman -- the Task Manager: every task in a grid that scrolls (wtk's DataGrid: its state, its
// name, an app or a kernel task, an app's system calls per second -- kapi v74 proc_stats, the pid
// found by name in kapi_list_procs), refreshed twice a second. Up / Down select; Enter (a double
// click, Bring to Front) raises an app's window; k or Delete (End Task) kills the selected app
// (kernel tasks are protected); r refreshes now. The window resizes: the grid follows.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "ft/wtkface.h"			// FreeType's text (DejaVu Sans) for every widget
#include "applib.h"
#include <stdio.h>
#include <string.h>

using namespace wtk;

#define W	460
#define H	400
#define FOOT	44
#define MAXT	160
#define NAMEL	32

static char g_name[MAXT][NAMEL];
static char g_state[MAXT];		// R/S/B/N
static char g_kernel[MAXT];		// 1 = kernel task (not killable)
static int  g_rate[MAXT];		// system calls per second, -1 unknown
static int  g_count = 0;

static DataGrid *g_grid;
static Button *g_btRaise, *g_btKill;
static Label *g_lbCount;
static Root *g_root;

static void refresh (void)
{
	char keep[NAMEL] = "";				// the selection: kept by its name
	if (g_grid->sel >= 0 && g_grid->sel < g_count) strcpy (keep, g_name[g_grid->sel]);

	static char buf[8192];
	kapi_list_tasks (buf, sizeof (buf));
	g_count = 0;
	int i = 0;
	while (buf[i] && g_count < MAXT)
	{
		int ls = i; while (buf[i] && buf[i] != '\n') i++;
		int le = i; if (buf[i] == '\n') i++;
		if (le - ls >= 3)
		{
			g_state[g_count] = buf[ls];
			g_kernel[g_count] = (buf[ls + 1] == 'k');
			int p = 0;
			for (int k = ls + 3; k < le && p < NAMEL - 1; k++) g_name[g_count][p++] = buf[k];
			g_name[g_count][p] = '\0';
			g_count++;
		}
	}

	// The system calls per second: each app row's pid from kapi_list_procs ("<pid> <a|k> <state>
	// <pages> <name>" a line), by its name.
	static char procs[8192];
	kapi_list_procs (procs, sizeof (procs));
	for (int t = 0; t < g_count; t++)
	{
		g_rate[t] = -1;
		if (g_kernel[t]) continue;
		for (int j = 0; procs[j]; )
		{
			int ls = j; while (procs[j] && procs[j] != '\n') j++;
			int le = j; if (procs[j] == '\n') j++;
			int k = ls, pid = 0;
			while (k < le && procs[k] >= '0' && procs[k] <= '9') pid = pid * 10 + (procs[k++] - '0');
			for (int f = 0; f < 4 && k < le; f++)		// past pid, kind, state, pages
			{
				while (k < le && procs[k] == ' ') k++;
				if (f < 3) while (k < le && procs[k] != ' ') k++;
			}
			int n = 0; while (g_name[t][n] && k + n < le && procs[k + n] == g_name[t][n]) n++;
			struct kapi_syscall_stats ss;
			if (pid > 0 && g_name[t][n] == 0 && k + n == le && kapi_proc_stats (pid, &ss) == 0)
			{
				g_rate[t] = (int) ss.rate;
				break;
			}
		}
	}

	g_grid->setRows (g_count);
	int s = -1;
	for (int t = 0; t < g_count && keep[0]; t++) if (!strcmp (g_name[t], keep)) { s = t; break; }
	if (s < 0 && g_count) s = g_grid->sel >= 0 && g_grid->sel < g_count ? g_grid->sel : 0;
	if (s != g_grid->sel) g_grid->sel = s;
	g_grid->invalidate (true);
	bool app = s >= 0 && !g_kernel[s];
	if (g_btRaise->disabled == app) { g_btRaise->disabled = g_btKill->disabled = !app; g_btRaise->invalidate (true); g_btKill->invalidate (true); }
	char t[40]; snprintf (t, sizeof t, "%d task%s", g_count, g_count == 1 ? "" : "s");
	if (strcmp (t, g_lbCount->text)) g_lbCount->setText (t);
}

static const char *cell (DataGrid &, int row, int col, char *buf, int cap)
{
	if (row < 0 || row >= g_count) return "";
	switch (col)
	{
	case 0: return g_name[row];
	case 1: return g_state[row] == 'R' ? "Running" : g_state[row] == 'S' ? "Sleeping" : g_state[row] == 'B' ? "Waiting" : g_state[row] == 'N' ? "New" : "?";
	case 2: return g_kernel[row] ? "Kernel" : "App";
	default: if (g_rate[row] < 0) return ""; snprintf (buf, (size_t) cap, "%d", g_rate[row]); return buf;
	}
}

static void do_raise (void) { int s = g_grid->sel; if (s >= 0 && s < g_count && !g_kernel[s]) kapi_raise_app (g_name[s]); }
static void do_kill (void) { int s = g_grid->sel; if (s >= 0 && s < g_count && !g_kernel[s]) { kapi_kill (g_name[s]); refresh (); } }
static void on_raise (Widget &) { do_raise (); g_grid->setFocus (); }
static void on_kill (Widget &) { do_kill (); g_grid->setFocus (); }
static void on_select (Widget &) { refresh (); }

class TaskRoot : public Root
{
public:
	int frames;
	TaskRoot () : Root (W, H, "Task Manager"), frames (0) {}
	void onTick () override { if (++frames >= 30) { frames = 0; refresh (); } }	// ~twice a second
	bool onKey (long k) override
	{
		switch (k)
		{
		case 'k': case 'K': case KEY_DEL: do_kill (); return true;
		case 'r': case 'R': refresh (); return true;
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
	g_root = &root;

	g_grid = new DataGrid (8, 8, W - 16, H - FOOT - 8);
	g_grid->anchor = ANCHOR_FILL;
	g_grid->setColumns (4);
	g_grid->setColumn (0, "Task", 190);
	g_grid->setColumn (1, "State", 84);
	g_grid->setColumn (2, "Kind", 64);
	g_grid->setColumn (3, "Calls / s", 90, GRID_RIGHT);
	g_grid->cellText = cell;
	g_grid->sortable = false;
	g_grid->onSelect = on_select;
	g_grid->onActivate = on_raise;
	g_grid->emptyText = "No task";
	root.addChild (g_grid);

	g_lbCount = new Label (10, H - FOOT + 12, 140, 22, "", C_DIS, root.bg);
	g_lbCount->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM;
	root.addChild (g_lbCount);
	g_btRaise = new Button (W - 8 - 2 * 124 - 6, H - FOOT + 7, 124, 30, "Bring to Front", on_raise);
	g_btRaise->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM; g_btRaise->tip = "The app's window in front (Enter, a double click)";
	root.addChild (g_btRaise);
	g_btKill = new Button (W - 8 - 124, H - FOOT + 7, 124, 30, "End Task", on_kill);
	g_btKill->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM; g_btKill->tip = "Stop the app (k, Delete); a kernel task cannot be stopped";
	root.addChild (g_btKill);

	root.setResizable (true);
	refresh ();
	g_grid->setFocus ();
	root.run ();
	return 0;
}
