//
// proc.cpp -- the Onyx processes in the runner (onyxrun.h): the table, a process made and started (its memory, the
// kapi page, AppKit, its ELF, its stack and heap, its main thread), its end (every thread stopped, its handles
// closed, its memory given back), the wait for it.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include "kobj.h"
#include <filesystem>
#include <chrono>
#include <string.h>

namespace fs = std::filesystem;

std::mutex g_ProcsM;
static std::vector<Proc *> s_Procs;
static int s_NextPid = 2;			// (1: the kernel's init on the Pi)

Proc *proc_find (int pid)
{
	std::lock_guard<std::mutex> L (g_ProcsM);
	for (Proc *P : s_Procs) if (P->pid == pid) return P;
	return 0;
}

std::vector<Proc *> proc_list (void)
{
	std::lock_guard<std::mutex> L (g_ProcsM);
	return s_Procs;
}

// app.txt's "stack = N" (bytes, K or M), 0 if none
static u64 app_stack (const std::string &hostAppDir)
{
	FILE *f = fopen ((hostAppDir + "/app.txt").c_str (), "r");
	if (!f) return 0;
	char line[256];
	u64 v = 0;
	while (fgets (line, sizeof line, f))
	{
		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (strncmp (p, "stack", 5) != 0) continue;
		p = strchr (p, '=');
		if (!p) continue;
		char *e;
		v = strtoull (p + 1, &e, 0);
		while (*e == ' ') e++;
		if (*e == 'K' || *e == 'k') v <<= 10;
		else if (*e == 'M' || *e == 'm') v <<= 20;
	}
	fclose (f);
	return v;
}

static void block (std::vector<char> &b, const std::vector<std::string> &v)
{
	b.clear ();
	for (auto &s : v) { b.insert (b.end (), s.begin (), s.end ()); b.push_back (0); }
}

// The system's default environment (procx.cpp): the built-in one, then SD:/etc/environment's "K=V" lines.
static std::vector<std::string> default_env (void)
{
	std::vector<std::string> v = { "HOME=SD:/home", "PATH=SD:/bin", "TMPDIR=RAM:/tmp", "LANG=C.UTF-8" };
	std::string h = host_path ("SD:/etc/environment", "SD:/");
	FILE *f = h.empty () ? 0 : fopen (h.c_str (), "r");
	char line[512];
	while (f && fgets (line, sizeof line, f))
	{
		std::string l = line;
		while (!l.empty () && (l.back () == '\n' || l.back () == '\r')) l.pop_back ();
		size_t eq = l.find ('=');
		if (l.empty () || l[0] == '#' || eq == std::string::npos || eq == 0) continue;
		for (auto &e : v) if (e.compare (0, eq + 1, l, 0, eq + 1) == 0) { e = l; l.clear (); break; }
		if (!l.empty ()) v.push_back (l);
	}
	if (f) fclose (f);
	return v;
}

Proc *proc_spawn (const SpawnOpts &o, std::string *why)
{
	Proc *parent = o.ppid ? proc_find (o.ppid) : 0;
	std::string cwd = !o.cwd.empty () ? o.cwd : parent ? parent->cwd : std::string ("SD:/");
	std::string onyx = onyx_abs (o.path.c_str (), cwd);
	std::string host = host_path (onyx.c_str (), cwd);
	std::error_code ec;
	if (onyx.empty () || host.empty () || !fs::is_regular_file (host, ec)) { if (why) *why = "no such program"; return 0; }

	Proc *P = new Proc ();
	P->ppid = o.ppid;
	P->path = onyx;
	size_t app = onyx.find (".app/");
	if (!o.name.empty ()) P->name = o.name;
	else if (app != std::string::npos)
	{
		size_t s = onyx.rfind ('/', app);
		P->name = onyx.substr (s + 1, app - s - 1);
	}
	else P->name = onyx.substr (onyx.rfind ('/') + 1);
	P->cwd = cwd;
	std::vector<std::string> av = o.argv;
	if (av.empty ()) av.push_back (onyx);
	block (P->argv, av);
	if (!o.env.empty ()) block (P->env, o.env);
	else if (parent) P->env = parent->env;
	else block (P->env, default_env ());
	P->stdinStream = o.in ? o.in : obj_console_in ();
	P->stdoutStream = o.out ? o.out : obj_console_out ();

	if (!P->mem.init ()) { if (why) *why = "no host memory"; delete P; return 0; }
	u64 entry = 0;
	bool failed = false;
	{
		PLock L (P);
		kapi_init_table (P->mem);
		std::string w;
		if (!load_appkit (P, &w) || (entry = load_program (P, host, &w)) == 0)
		{
			if (why) *why = w;
			P->mem.fini ();
			failed = true;
		}
	}
	if (failed) { delete P; return 0; }
	if (P->stdoutStream && P->stdoutStream->pipe) { std::lock_guard<std::mutex> L (P->stdoutStream->pipe->m); P->stdoutStream->pipe->writers++; }
	{
		PLock L (P);
		u64 stack = app != std::string::npos ? app_stack (host_path (onyx.substr (0, app + 4).c_str (), cwd)) : 0;
		if (stack < EL0_USTACK_MIN) stack = EL0_USTACK_MIN;
		if (stack > EL0_USTACK_MAX) stack = EL0_USTACK_MAX;
		stack = ALIGN_UP (stack);
		P->mem.map (USER_STACK_TOP - stack, stack, MEM_R | MEM_W, "stack of the main thread", KAPI_VMK_STACK);
		P->heapBase = P->heapBrk = P->heapTop = USER_HEAP_BASE;
	}
	{
		std::lock_guard<std::mutex> L (g_ProcsM);
		P->pid = s_NextPid++;
		s_Procs.push_back (P);
	}
	if (g_Run.trace) rlog ("%s: pid %d, entry %llx", onyx.c_str (), P->pid, (unsigned long long) entry);
	cpu_thread_start (P, entry, 0, USER_STACK_TOP, el0_main_return (), 0, "main");
	return P;
}

// The process has ended (its main thread is gone): its handles closed, its console's end said, waiters told. Its
// memory is given back when its last thread is gone (proc_thread_gone).
static void proc_mark_ended (Proc *P)
{
	static std::mutex s_M;
	{
		std::lock_guard<std::mutex> L (s_M);
		if (P->ended) return;
	}
	h_drop_all (P);
	if (P->stdoutStream && P->stdoutStream->pipe)
	{
		Pipe &Q = *P->stdoutStream->pipe;
		std::lock_guard<std::mutex> L (Q.m);
		if (--Q.writers <= 0) { Q.eof = true; Q.cv.notify_all (); }
	}
	P->stdinStream.reset ();
	P->stdoutStream.reset ();
	for (auto &f : P->atEnd) f ();
	P->atEnd.clear ();
	{
		std::lock_guard<std::mutex> L (P->m);
		P->ended = true;
		P->endCV.notify_all ();
	}
	if (g_Run.trace) rlog ("%s (pid %d) ended: %d", P->name.c_str (), P->pid, P->status);
	extern void runner_proc_ended (Proc *P);	// (main.cpp: the runner's own program -> the runner ends)
	runner_proc_ended (P);
}

void proc_thread_gone (Proc *P, Thread *T)
{
	bool isMain, last;
	{
		std::lock_guard<std::mutex> L (P->lockM);
		isMain = !P->threads.empty () && P->threads[0] == T;
		int live = 0;
		for (Thread *t : P->threads) if (t != T && !cpu_thread_done (t)) live++;
		last = live == 0;
	}
	if (isMain && !P->dying.exchange (true))	// (the main thread returned or exited: the process ends with it)
	{
		P->status = cpu_thread_code (T);
		P->reason = KAPI_PROC_EXITED;
	}
	if (!isMain) { extern void thread_ended (Proc *P, int tid); thread_ended (P, cpu_thread_tid (T)); }
	if (isMain || P->dying) cpu_stop_all (P);
	{
		std::lock_guard<std::mutex> L (P->m);
		P->pumpCV.notify_all ();
	}
	if (isMain || (last && P->dying)) proc_mark_ended (P);
	if (last && P->ended)
	{
		PLock L (P);
		P->mem.fini ();
	}
}

void check_dying (void)
{
	Proc *P = cur ();
	if (P && P->dying) cpu_exit_thread (P->status);
}

void proc_exit (Proc *P, int status, int reason)
{
	if (!P->dying.exchange (true)) { P->status = status; P->reason = reason; }
	cpu_stop_all (P);
	{
		std::lock_guard<std::mutex> L (P->m);
		P->pumpCV.notify_all ();
	}
	// the threads end at their next stop; those blocked in a host wait end when it returns. The caller's own
	// thread (a system call of P's): ends now.
	if (cur () == P) cpu_exit_thread (status);
}

int proc_wait_end (Proc *P, int timeoutMs)
{
	std::unique_lock<std::mutex> L (P->m);
	if (timeoutMs < 0) P->endCV.wait (L, [P] { return P->ended.load (); });
	else P->endCV.wait_for (L, std::chrono::milliseconds (timeoutMs), [P] { return P->ended.load (); });
	return P->ended ? P->status : -1;
}
