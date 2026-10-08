//
// session_tool_test.cpp -- /bin/session (user/BinUtils/session.c) on the PC: the tool compiled with main renamed
// session_main, linked with UIKit's window calls (win.cpp, port.cpp: the stand-in kernel's), SystemKit inline and the
// desktop simulator's fakekapi.o; this file stands in for the kernel's processes, windows and graphics server:
//   HPROCS="pid:name[:flags],...": the processes -- flags w (a window), s (a system window), x (does not close when
//          asked: an unsaved document's question), p (a parent of the tool: its tree holds it); the tool is pid 50
//   HSWITCH=n: KAPI_WS_SWITCH's answer (default 0)
// It logs "h: close <pid>", "h: kill <pid> <force>", "h: switch" on stderr, then "h: exit <status>". SIM_ARGS: the
// tool's arguments. Run by tools/tests/session/run.sh.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "appkit/appkit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

extern "C" int session_main (void);

struct HP { int pid; std::string name; bool win, sys, stubborn, parent, alive; };
static std::vector<HP> g_p;
static HP *find (int pid) { for (auto &p : g_p) if (p.pid == pid) return &p; return 0; }

static int h_list_procs (char *b, unsigned n)
{
	std::string s = "0 k R 0 idle\n50 a R session\n";
	int c = 2;
	for (auto &p : g_p) if (p.alive) { s += std::to_string (p.pid) + " a S " + p.name + "\n"; c++; }
	snprintf (b, n, "%s", s.c_str ());
	return c;
}
static int h_kill_pid (int pid, int force)
{
	fprintf (stderr, "h: kill %d %d\n", pid, force);
	HP *p = find (pid);
	if (!p || !p->alive) return 0;
	p->alive = false;
	return 1;
}
static int h_win_list (struct kapi_win_info *o, int max)
{
	int n = 0;
	for (auto &p : g_p)
		if (p.alive && p.win && n < max)
		{
			memset (&o[n], 0, sizeof o[n]);
			o[n].id = (unsigned) p.pid * 10; o[n].pid = (unsigned) p.pid;
			o[n].flags = p.sys ? WIN_FLAG_SYSTEM : 0;
			snprintf (o[n].title, sizeof o[n].title, "%c%s - doc", p.name[0] - 32, p.name.c_str () + 1);
			n++;
		}
	return n;
}
static int h_win_close (unsigned id)
{
	HP *p = find ((int) id / 10);
	fprintf (stderr, "h: close %d\n", (int) id / 10);
	if (!p) return -1;
	if (!p->stubborn) p->alive = false;
	return 0;
}
static void h_msleep (unsigned) {}		// (the tool's waits count their steps: no time needed; the script's steps not run)
static int h_getpid (int which) { return which == 0 ? 50 : 1; }
static int h_proc_tree (int pid, int op, int *out, unsigned cap)
{
	HP *p = find (pid);
	if (!p) return -KAPI_ESRCH;
	if (op == KAPI_TREE_LIST && p->parent) { if (cap > 0) out[0] = 50; return 1; }
	return 0;
}
extern "C" long sim_ws_ctl (int op, long, long, long)
{
	if (op == KAPI_WS_SWITCH)
	{
		fprintf (stderr, "h: switch\n");
		return getenv ("HSWITCH") ? atol (getenv ("HSWITCH")) : 0;
	}
	if (op == KAPI_WS_ACTIVE) return 1;
	return -KAPI_ENOSYS;
}

int main ()
{
	const char *e = getenv ("HPROCS");
	std::string s = e ? e : "";
	size_t i = 0;
	while (i < s.size ())
	{
		size_t j = s.find (',', i); if (j == std::string::npos) j = s.size ();
		std::string it = s.substr (i, j - i);
		size_t a = it.find (':'), b = it.find (':', a + 1);
		HP p; p.pid = atoi (it.c_str ()); p.name = it.substr (a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
		std::string f = b == std::string::npos ? "" : it.substr (b + 1);
		p.win = f.find ('w') != std::string::npos || f.find ('s') != std::string::npos;
		p.sys = f.find ('s') != std::string::npos; p.stubborn = f.find ('x') != std::string::npos;
		p.parent = f.find ('p') != std::string::npos; p.alive = true;
		g_p.push_back (p);
		i = j + 1;
	}
	TKApiTable *T = (TKApiTable *) KAPI_TABLE_VA;
	T->list_procs = h_list_procs; T->kill_pid = h_kill_pid; T->win_list = h_win_list; T->win_close = h_win_close;
	T->getpid = h_getpid; T->proc_tree = h_proc_tree; T->msleep = h_msleep;
	int r = session_main ();
	fprintf (stderr, "h: exit %d\n", r);
	return 0;
}
