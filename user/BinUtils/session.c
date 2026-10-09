//
// session -- the interface's session: the programs of the mode (desktop, pocket, console), and the switch from one
// mode to another (docs/POCKETUI-TECH-STUDY.md section 8; SystemKit's session.h).
//
//   session                      the mode's programs started: SD:/etc/session/<mode> run as init runs the autostart
//                                (one shell command a line, `sleep`, `wait`); the mode is the running server's
//                                (system.ini's "shell =" unless its server failed and the kernel started Elegant).
//                                SD:/etc/autostart's line `session` does it at boot.
//   session start [MODE]         the same (MODE: that mode's file)
//   session mode                 the mode chosen (system.ini) and the one running
//   session list [MODE]          the programs of a mode's file (the current mode's)
//   session switch MODE [--force] [--no-ask] [--wait S] [--keep PID[,PID...]]
//                                the switch: the open programs asked to close (an unsaved document asked, as their
//                                close box does), waited for S seconds (5); the ones still open listed in
//                                SD:/tmp/session.wait and exit status 2 -- nothing switched -- unless --force (ended);
//                                --no-ask: not asked again, only waited for. Then the session's programs ended, "shell
//                                =" written, the kernel's graphics server switched (KAPI_WS_SWITCH), the new mode's
//                                file run; printd started again (it holds the server's UIKit). The caller and its
//                                parents (the Mode applet, the Control Panel, the terminal) and the --keep programs
//                                are left open meanwhile, then ended. Its server failed: the desktop, "shell =
//                                desktop" put back, a notification. (--switch MODE: the same.)
//   session migrate              a card from before the sessions: the autostart's desktop lines moved into
//                                SD:/etc/session/desktop, once (what `pkg commit` does at boot)
// Exit status: 0, or SystemKit's SESSION_* (1 fell back to the desktop, 2 programs still open, 3 refused, 4 bad
// arguments, 5 failed).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "uikit/win.h"

#define MAXP		48		// programs looked at
#define WAIT_MS		5000		// the open programs' time to close (--wait)
#define END_MS		2000		// the session's programs' time to end before they are killed

// The desktop's programs when its file is missing (a card whose SD:/etc/session/desktop was removed).
static const char DESKTOP_DEFAULT[] = "run voronoy\nrun menubar\nrun notifyd\nrun dock\nrun agenda\n";

static void put (const char *s) { ax_puts (s); }
static void putn (int v) { char b[16]; ax_itoa (v, b); ax_puts (b); }
static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static int seq (const char *a, const char *b) { return ax_streq (a, b); }
static void scpy (char *d, int cap, const char *s) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

// ---- the arguments --------------------------------------------------------------------------------------------
static char g_args[512];
static char *g_argv[24];
static int g_argc;

static void split_args (void)
{
	int n = kapi_get_args (g_args, sizeof g_args - 1);
	if (n < 0) n = 0;
	g_args[n < (int) sizeof g_args - 1 ? n : (int) sizeof g_args - 1] = 0;
	for (char *p = g_args; *p && g_argc < 24; )
	{
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') *p++ = 0;
		if (!*p) break;
		g_argv[g_argc++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
	}
}
static int to_int (const char *s, int *out)
{
	int v = 0, k = 0;
	for (; s[k] >= '0' && s[k] <= '9'; k++) v = v * 10 + (s[k] - '0');
	if (k == 0) return 0;
	*out = v;
	return k;
}

// ---- the processes --------------------------------------------------------------------------------------------
struct Proc { int pid; char name[48]; };
static struct Proc g_procs[128];
static int g_nprocs;

// kapi_list_procs' lines "<pid> <a|k> <state> <pages> <name>" -> the user processes (a)
static void read_procs (void)
{
	static char b[8192];
	g_nprocs = 0;
	int n = kapi_list_procs (b, sizeof b - 1);
	if (n <= 0) return;
	b[sizeof b - 1] = 0;
	for (char *l = b; *l && g_nprocs < 128; )
	{
		char *e = l; while (*e && *e != '\n') e++;
		char c = *e; *e = 0;
		int pid = 0, k = to_int (l, &pid);
		char *p = l + k;
		while (*p == ' ') p++;
		char kind = *p;
		for (int w = 0; w < 2; w++)					// the kind, the state
		{
			while (*p && *p != ' ') p++;
			while (*p == ' ') p++;
		}
		// (the kernel's line has the process's pages before its name: "<pid> <a|k> <state> <pages> <name>" -- read
		//  as the name's start it hid every program from the switch: the old session's shells stayed, 2026-10-09)
		{ char *q = p; while (*q >= '0' && *q <= '9') q++; if (q > p && *q == ' ') { while (*q == ' ') q++; p = q; } }
		if (k > 0 && kind == 'a' && *p) { g_procs[g_nprocs].pid = pid; scpy (g_procs[g_nprocs].name, 48, p); g_nprocs++; }
		*e = c;
		l = *e ? e + 1 : e;
	}
}
static const char *proc_name (int pid)
{
	for (int i = 0; i < g_nprocs; i++) if (g_procs[i].pid == pid) return g_procs[i].name;
	return 0;
}

// The session's programs: the names in the three modes' files, notifyd (started by the first notification) -- one
// a line.
static char g_sess[2048];
static void read_session_programs (void)
{
	int o = 0;
	g_sess[0] = 0;
	for (int m = 0; m < session_modes (); m++)
	{
		char b[1024];
		session_programs (m, b, sizeof b);
		if (m == SESSION_DESKTOP && b[0] == 0)
		{
			char path[64]; session_file (m, path, sizeof path);
			void *f = kapi_open (path);
			if (f) kapi_close (f);
			else scpy (b, sizeof b, "voronoy\nmenubar\nnotifyd\ndock\nagenda\n");
		}
		for (int i = 0; b[i] && o < (int) sizeof g_sess - 1; i++) g_sess[o++] = b[i];
	}
	const char *more = "notifyd\n";
	for (int i = 0; more[i] && o < (int) sizeof g_sess - 1; i++) g_sess[o++] = more[i];
	g_sess[o] = 0;
}
static int is_session_program (const char *name)
{
	if (!name) return 0;
	for (const char *l = g_sess; *l; )
	{
		int k = 0;
		while (name[k] && l[k] == name[k]) k++;
		if (name[k] == 0 && (l[k] == '\n' || l[k] == 0)) return 1;
		while (*l && *l != '\n') l++;
		if (*l) l++;
	}
	return 0;
}

// ---- running a session file -----------------------------------------------------------------------------------
// One line as init runs it: SD:bin/<first word> <the rest>; `sleep <s>` pauses; `wait <command>` waits for its
// end; a `session` line is skipped (no session inside a session).
static void run_line (char *line)
{
	while (*line == ' ' || *line == '\t') line++;
	int n = slen (line);
	while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
	if (*line == 0 || *line == '#') return;
	char *args = line;
	while (*args && *args != ' ' && *args != '\t') args++;
	if (*args) { *args++ = 0; while (*args == ' ' || *args == '\t') args++; }
	if (seq (line, "sleep"))
	{
		int s = 0; to_int (args, &s);
		kapi_msleep ((unsigned) s * 1000);
		return;
	}
	if (seq (line, "session")) return;
	int wait = 0;
	if (seq (line, "wait") && *args)
	{
		wait = 1; line = args;
		while (*args && *args != ' ' && *args != '\t') args++;
		if (*args) { *args++ = 0; while (*args == ' ' || *args == '\t') args++; }
	}
	char path[128]; int p = 0;
	path[0] = 0;
	ax_strcat (path, sizeof path, &p, "SD:bin/");
	ax_strcat (path, sizeof path, &p, line);
	if (wait)
	{
		void *proc = kapi_spawn (path, args, 0, 0);
		if (proc) kapi_wait (proc);
		else { put ("session: cannot run "); ax_putln (path); }
		return;
	}
	if (!kapi_exec (path, args)) { put ("session: cannot run "); ax_putln (path); }
}
// The console's file as it was before its shell (phase P9: `run terminal`, `run gamelib`, nothing else): the package
// manager kept it -- a config file it saw changed -- and wrote the new one beside it (.new), or the system's update
// waits for its restart: no consolehome, so nothing answered the menu's button (F10, the pad's Home; the user,
// 2026-10-09). That old default, consolehome installed: the file written anew (the old one kept as console.old).
static const char CONSOLE_DEFAULT[] =
	"# SD:/etc/session/console -- the console session's programs (/bin/session): consolehome is the console's shell\n"
	"# (the home, the menu over an app: the pad's Home, F10, the Super key). Written by session in place of the file\n"
	"# of before the shell (run terminal, run gamelib), kept as console.old.\n"
	"run consolehome\n";
static int console_was_default (const char *b, int n, const char *path)
{
	int terminal = 0, gamelib = 0, other = 0;
	for (int i = 0; i < n; )
	{
		int s = i;
		while (i < n && b[i] != '\n') i++;
		int e = i++;
		while (s < e && (b[s] == ' ' || b[s] == '\t')) s++;
		while (e > s && (b[e - 1] == ' ' || b[e - 1] == '\t' || b[e - 1] == '\r')) e--;
		if (s == e || b[s] == '#') continue;
		char l[48]; int k = 0;
		for (int j = s; j < e && k < 47; j++) l[k++] = b[j];
		l[k] = 0;
		if (seq (l, "run terminal")) terminal = 1;
		else if (seq (l, "run gamelib")) gamelib = 1;
		else other = 1;
	}
	if (!terminal || !gamelib || other) return 0;
	void *f = kapi_open ("SD:/apps/consolehome.app/main");
	if (!f) return 0;
	kapi_close (f);
	char old[72]; int k = 0;
	for (int j = 0; path[j] && k < 60; j++) old[k++] = path[j];
	old[k] = 0;
	const char *dot = ".old";
	for (int j = 0; dot[j]; j++) old[k++] = dot[j];
	old[k] = 0;
	kapi_save_file (old, b, (unsigned) n);
	kapi_save_file (path, CONSOLE_DEFAULT, (unsigned) slen (CONSOLE_DEFAULT));
	put ("session: "); put (path); ax_putln (" was the console's file of before its shell: written anew (the old one: console.old)");
	return 1;
}
static int run_file (int m)
{
	static char buf[16384];
	char path[64];
	session_file (m, path, sizeof path);
	int n = 0;
	void *f = kapi_open (path);
	if (f)
	{
		for (;;)
		{
			int r = kapi_read (f, buf + n, (unsigned) (sizeof buf - 1 - n));
			if (r <= 0) break;
			n += r;
			if (n >= (int) sizeof buf - 1) break;
		}
		kapi_close (f);
	}
	else if (m == SESSION_DESKTOP)
	{
		put ("session: no "); put (path); ax_putln (": the desktop's own programs");
		scpy (buf, sizeof buf, DESKTOP_DEFAULT); n = slen (buf);
	}
	else { put ("session: no "); ax_putln (path); return 0; }
	buf[n] = 0;
	if (m == SESSION_CONSOLE && console_was_default (buf, n, path)) { scpy (buf, sizeof buf, CONSOLE_DEFAULT); n = slen (buf); }
	put ("session: "); put (session_mode_name (m)); ax_putln (" -- its programs started");
	int start = 0;
	for (int i = 0; i <= n; i++)
		if (i == n || buf[i] == '\n')
		{
			buf[i] = 0;
			if (i > start) run_line (&buf[start]);
			start = i + 1;
		}
	return 1;
}

// The mode the server running gives (its UIKit asked); no server: the one chosen.
static int running_mode (void)
{
	struct uk_win_server_info si;
	char *z = (char *) &si;
	for (unsigned i = 0; i < sizeof si; i++) z[i] = 0;
	si.size = sizeof si;
	if (uk_win_server (&si) > 0 && si.mode >= 0 && si.mode < session_modes ()) return si.mode;
	return session_mode ();
}

static int parse_mode (const char *s, int def)
{
	if (s == 0) return def;
	int m = session_mode_find (s);
	if (m < 0) { put ("session: no mode \""); put (s); ax_putln ("\" (desktop, pocket, console)"); }
	return m;
}

// ---- the switch -----------------------------------------------------------------------------------------------
struct App { int pid; unsigned win; char title[48]; };

static int g_keep[16], g_nkeep;
static int kept (int pid) { for (int i = 0; i < g_nkeep; i++) if (g_keep[i] == pid) return 1; return 0; }
static void keep (int pid) { if (pid > 0 && !kept (pid) && g_nkeep < 16) g_keep[g_nkeep++] = pid; }

// Is pid one of this process's parents (a program whose tree holds it: the Mode applet, the Control Panel, the
// terminal that typed it)? Ending it before the end would end this switch with it.
static int is_parent (int pid, int me)
{
	int kids[64];
	int n = kapi_proc_tree (pid, KAPI_TREE_LIST, kids, 64);
	for (int i = 0; i < n && i < 64; i++) if (kids[i] == me) return 1;
	return 0;
}

// The programs ended: asked (force 0), killed after ms (force 1 at once).
static void end_programs (const int *pids, int n, int ms)
{
	for (int i = 0; i < n; i++) kapi_kill_pid (pids[i], ms > 0 ? 0 : 1);
	for (int t = 0; ms > 0 && t < ms; t += 100)
	{
		read_procs ();
		int left = 0;
		for (int i = 0; i < n; i++) if (proc_name (pids[i])) left++;
		if (!left) return;
		kapi_msleep (100);
	}
	read_procs ();
	for (int i = 0; i < n; i++) if (proc_name (pids[i])) kapi_kill_pid (pids[i], 1);
}

static void write_waiting (const struct App *a, int n)
{
	static char b[4096];
	int o = 0;
	b[0] = 0;
	for (int i = 0; i < n; i++)
	{
		const char *nm = proc_name (a[i].pid);
		ax_strcat (b, sizeof b, &o, nm ? nm : "?");
		ax_strcat (b, sizeof b, &o, "\t");
		ax_strcat (b, sizeof b, &o, a[i].title);
		ax_strcat (b, sizeof b, &o, "\n");
	}
	kapi_save_file (SESSION_WAITING, b, (unsigned) o);
}

static int do_switch (int to, int flags, int waitMs)
{
	int me = kapi_getpid (0);
	if (me < 0) me = 0;
	kapi_remove (SESSION_WAITING);
	int from = running_mode ();
	if (to == from)
	{
		if (session_mode () != to) session_set_mode (to);
		put ("session: the mode is "); put (session_mode_name (to)); ax_putln (" already");
		return SESSION_SWITCHED;
	}
	read_session_programs ();
	read_procs ();

	// 1. the open programs (a window that is not the system's, not the session's programs, not kept) asked to close
	static struct kapi_win_info wins[64];
	int nw = uk_win_list (wins, 64);
	static struct App apps[MAXP];
	int na = 0;
	for (int i = 0; i < nw; i++)
	{
		int pid = (int) wins[i].pid;
		if (pid <= 0 || pid == me || (wins[i].flags & WIN_FLAG_SYSTEM) || kept (pid)) continue;
		if (is_session_program (proc_name (pid))) continue;
		int k = 0;
		while (k < na && apps[k].pid != pid) k++;
		if (k < na) { if (wins[i].id < apps[k].win) { apps[k].win = wins[i].id; scpy (apps[k].title, 48, wins[i].title); } continue; }
		if (is_parent (pid, me)) { keep (pid); continue; }
		if (na >= MAXP) continue;
		apps[na].pid = pid; apps[na].win = wins[i].id; scpy (apps[na].title, 48, wins[i].title);
		na++;
	}
	if (na > 0)
	{
		if (!(flags & SESSION_NO_ASK)) for (int i = 0; i < na; i++) uk_win_close (apps[i].win);
		for (int t = 0; t < waitMs; t += 100)
		{
			read_procs ();
			int left = 0;
			for (int i = 0; i < na; i++) if (proc_name (apps[i].pid)) apps[left++] = apps[i];
			na = left;
			if (!na) break;
			kapi_msleep (100);
		}
		read_procs ();
		int left = 0;
		for (int i = 0; i < na; i++) if (proc_name (apps[i].pid)) apps[left++] = apps[i];
		na = left;
	}
	if (na > 0)
	{
		if (!(flags & SESSION_FORCE))
		{
			write_waiting (apps, na);
			for (int i = 0; i < na; i++)
			{
				put ("session: "); put (apps[i].title[0] ? apps[i].title : proc_name (apps[i].pid));
				ax_putln (" is waiting for an answer");
			}
			ax_putln ("session: nothing switched (--force ends them, their documents lost)");
			return SESSION_WAITING_APPS;
		}
		int pids[MAXP];
		for (int i = 0; i < na; i++) pids[i] = apps[i].pid;
		end_programs (pids, na, 0);
	}

	// 2. the session's programs ended (and printd, which holds the server's UIKit: started again after)
	int pids[MAXP], np = 0, printd = 0;
	read_procs ();
	for (int i = 0; i < g_nprocs && np < MAXP; i++)
	{
		int pid = g_procs[i].pid;
		if (pid == me || kept (pid)) continue;
		if (seq (g_procs[i].name, "printd")) { printd = 1; pids[np++] = pid; continue; }
		if (is_session_program (g_procs[i].name))
		{
			int par = is_parent (pid, me);
			put (par ? "session: kept until the end (it started this switch): " : "session: ending "); ax_putln (g_procs[i].name);
			if (par) keep (pid); else pids[np++] = pid;
		}
	}
	end_programs (pids, np, END_MS);

	// 3. "shell =", 4. the kernel's switch
	int old = session_mode ();
	if (!session_set_mode (to)) { ax_putln ("session: SD:/etc/system.ini not written"); run_file (from); return SESSION_FAILED; }
	put ("session: switching to "); put (session_mode_name (to)); ax_putln ("...");
	long r = kapi_ws_ctl (KAPI_WS_SWITCH, 0, 0, 0);
	int now = to, answer = SESSION_SWITCHED;
	if (r == 1)
	{
		put ("session: the "); put (session_mode_name (to)); ax_putln ("'s server failed: the desktop");
		session_set_mode (SESSION_DESKTOP);
		now = SESSION_DESKTOP; answer = SESSION_FELL_BACK;
	}
	else if (r == -KAPI_ENOSYS || r == -KAPI_EINVAL)
	{
		ax_putln ("session: this kernel cannot switch its server (kapi v97): restarting");
		kapi_msleep (500);
		kapi_reboot ();
		return SESSION_SWITCHED;
	}
	else if (r < 0)
	{
		put ("session: refused ("); putn ((int) r);
		ax_putln (r == -KAPI_EBUSY ? ": a full-screen program has the display, or a switch is under way)" : ": no server took the display)");
		if (r == -KAPI_EBUSY) session_set_mode (old); else session_set_mode (SESSION_DESKTOP);
		if (r == -KAPI_EBUSY) { run_file (from); if (printd) kapi_exec ("SD:bin/run", "printd"); }
		return r == -KAPI_EBUSY ? SESSION_REFUSED : SESSION_FAILED;
	}
	for (int t = 0; t < 5000 && kapi_ws_ctl (KAPI_WS_ACTIVE, 0, 0, 0) <= 0; t += 100) kapi_msleep (100);

	// 5. the new session
	run_file (now);
	if (printd) kapi_exec ("SD:bin/run", "printd");
	if (answer == SESSION_FELL_BACK) notify ("Onyx", "The interface asked for could not start: the desktop instead.");
	put ("session: the mode is "); ax_putln (session_mode_name (now));
	// the programs kept open (the caller, its parents): their windows were the other server's
	if (g_nkeep > 0) end_programs (g_keep, g_nkeep, END_MS);
	return answer;
}

// ---- the commands ---------------------------------------------------------------------------------------------
static void usage (void)
{
	ax_putln ("usage: session                       the mode's programs started (SD:/etc/session/<mode>)");
	ax_putln ("       session start [MODE]          the same, that mode's file");
	ax_putln ("       session mode                  the mode chosen and the one running");
	ax_putln ("       session list [MODE]           the programs of a mode's file");
	ax_putln ("       session switch MODE [--force] [--no-ask] [--wait S] [--keep PID[,PID]]");
	ax_putln ("       session migrate               the autostart's desktop lines into SD:/etc/session/desktop");
	ax_putln ("MODE: desktop, pocket, console");
}

int main (void)
{
	split_args ();
	const char *cmd = g_argc > 0 ? g_argv[0] : "start";
	if (seq (cmd, "--switch")) cmd = "switch";
	if (seq (cmd, "help") || seq (cmd, "-h") || seq (cmd, "--help")) { usage (); return 0; }
	if (seq (cmd, "start"))
	{
		int run = running_mode (), want = session_mode ();
		int m = parse_mode (g_argc > 1 ? g_argv[1] : 0, run);
		if (m < 0) return SESSION_BAD;
		if (g_argc <= 1 && run != want)
		{
			put ("session: SD:/etc/system.ini asks for "); put (session_mode_name (want));
			put (", the server running is the "); put (session_mode_name (run)); ax_putln ("'s");
		}
		return run_file (m) ? 0 : SESSION_FAILED;
	}
	if (seq (cmd, "mode"))
	{
		put ("chosen: "); ax_putln (session_mode_name (session_mode ()));
		put ("running: "); ax_putln (session_mode_name (running_mode ()));
		return 0;
	}
	if (seq (cmd, "list"))
	{
		int m = parse_mode (g_argc > 1 ? g_argv[1] : 0, session_mode ());
		if (m < 0) return SESSION_BAD;
		static char b[2048];
		session_programs (m, b, sizeof b);
		put (b);
		return 0;
	}
	if (seq (cmd, "migrate"))
	{
		int r = session_migrate ();
		ax_putln (r > 0 ? "session: SD:/etc/autostart split: the desktop's programs in SD:/etc/session/desktop (the old file: autostart.old)"
			: r == 0 ? "session: nothing to split (a `session` line, or no autostart)" : "session: the autostart could not be split (left as it was)");
		return r < 0 ? SESSION_FAILED : 0;
	}
	if (seq (cmd, "switch"))
	{
		if (g_argc < 2) { usage (); return SESSION_BAD; }
		int to = parse_mode (g_argv[1], -1);
		if (to < 0) return SESSION_BAD;
		int flags = 0, waitMs = WAIT_MS;
		for (int i = 2; i < g_argc; i++)
		{
			const char *a = g_argv[i];
			if (seq (a, "--force")) flags |= SESSION_FORCE;
			else if (seq (a, "--no-ask")) flags |= SESSION_NO_ASK;
			else if (seq (a, "--wait") && i + 1 < g_argc)
			{
				int s = 0;
				if (!to_int (g_argv[++i], &s)) { usage (); return SESSION_BAD; }
				waitMs = s * 1000;
			}
			else if (seq (a, "--keep") && i + 1 < g_argc)
			{
				for (const char *p = g_argv[++i]; *p; )
				{
					int pid = 0, k = to_int (p, &pid);
					if (k == 0) { usage (); return SESSION_BAD; }
					keep (pid);
					p += k;
					if (*p == ',') p++;
				}
			}
			else { put ("session: what is "); ax_putln (a); usage (); return SESSION_BAD; }
		}
		return do_switch (to, flags, waitMs);
	}
	usage ();
	return SESSION_BAD;
}
