//
// k_proc.cpp -- the processes' system calls (onyxrun.h, kobj.h), as kernel/sys/kapi.cpp and procx.cpp do them:
// launch / exec / exec_as (fire and forget), spawn / wait / proc_done (a console child with its streams), spawn_ex /
// spawn_ex2 / proc_wait (v75), list_procs / list_tasks, kill / kill_pid, proc_tree.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include "kobj.h"
#include <filesystem>
#include <string.h>

namespace fs = std::filesystem;

// argv = path, then args split as a shell does (blanks; "double quotes" group, removed) -- procx.cpp ArgvFrom
static std::vector<std::string> argv_from (const std::string &path, const std::string &args)
{
	std::vector<std::string> v { path };
	size_t i = 0, n = args.size ();
	while (i < n)
	{
		while (i < n && (args[i] == ' ' || args[i] == '\t')) i++;
		if (i >= n) break;
		bool q = false, any = false;
		std::string w;
		while (i < n && (q || (args[i] != ' ' && args[i] != '\t')))
		{
			if (args[i] == '"') { q = !q; any = true; i++; continue; }
			w += args[i++];
			any = true;
		}
		if (any) v.push_back (w);
	}
	return v;
}

static std::vector<std::string> block_strings (u64 va)
{
	std::vector<std::string> v;
	for (int k = 0; k < 4096; k++)
	{
		std::string s;
		if (!gstr (va, s, 65536) || s.empty ()) break;
		v.push_back (s);
		va += s.size () + 1;
	}
	return v;
}

static Proc *start (const std::string &path, const std::vector<std::string> &argv, const std::string &name,
		    std::shared_ptr<Obj> in, std::shared_ptr<Obj> out, const std::vector<std::string> *env = 0,
		    const std::string &cwd = "")
{
	Proc *me = cur ();
	SpawnOpts o;
	o.path = path;
	o.argv = argv;
	if (!o.argv.empty ()) o.argv[0] = onyx_abs (path.c_str (), me->cwd);
	o.name = name;
	o.in = in; o.out = out;
	o.ppid = me->pid;
	o.cwd = cwd;
	if (env) o.env = *env;
	std::string why;
	Proc *P = proc_spawn (o, &why);
	if (!P && g_Run.trace) rlog ("%s: cannot start %s: %s", me->name.c_str (), path.c_str (), why.c_str ());
	return P;
}

// ---- fire and forget ----------------------------------------------------------------------------------------------
static int k_exec_as (u64 path, u64 args, u64 name)
{
	std::string p, a, n;
	if (!gstr (path, p, 1024)) return 0;
	if (args) gstr (args, a, 1024);
	if (name) gstr (name, n, 64);
	return start (p, argv_from (p, a), n, 0, 0) ? 1 : 0;
}
static int k_exec (u64 path, u64 args) { return k_exec_as (path, args, 0); }

// An app by its name: SD:/apps/<name>.app/main, or its main.<ext> run by its runner (SD:/etc/runners.ini).
static int k_launch (u64 name)
{
	std::string n;
	if (!gstr (name, n, 64) || n.empty ()) return 0;
	std::string dir = "SD:/apps/" + n + ".app/";
	std::error_code ec;
	std::string h = host_path ((dir + "main").c_str (), "SD:/");
	if (!h.empty () && fs::is_regular_file (h, ec)) return start (dir + "main", { dir + "main" }, n, 0, 0) ? 1 : 0;
	static const char *runners[][2] = { { "bax", "SD:/bin/basic" }, { "bas", "SD:/bin/basic" } };
	for (auto &r : runners)
	{
		std::string m = dir + "main." + r[0];
		h = host_path (m.c_str (), "SD:/");
		if (!h.empty () && fs::is_regular_file (h, ec)) return start (r[1], { r[1], m }, n, 0, 0) ? 1 : 0;
	}
	return 0;
}

// ---- console children ---------------------------------------------------------------------------------------------
static std::shared_ptr<Obj> stream_arg (u64 h, bool *bad)
{
	if (!h) return 0;
	auto o = h_get (cur (), h);
	if (!o) *bad = true;
	return o;
}

static u64 proc_handle (Proc *P)
{
	auto o = std::make_shared<Obj> ();
	o->kind = H_PROC;
	o->proc = P;
	return h_add (cur (), o);
}

static u64 k_spawn (u64 path, u64 args, u64 in, u64 out)
{
	std::string p, a;
	if (!gstr (path, p, 1024)) return 0;
	if (args) gstr (args, a, 1024);
	bool bad = false;
	auto I = stream_arg (in, &bad), O = stream_arg (out, &bad);
	if (bad) return 0;
	Proc *P = start (p, argv_from (p, a), "", I, O);
	return P ? proc_handle (P) : 0;
}

static int k_wait (u64 h)
{
	auto o = h_get (cur (), h, H_PROC);
	if (!o) return -1;
	int st;
	while ((st = proc_wait_end (o->proc, 50)) == -1 && !o->proc->ended) check_dying ();
	h_drop (cur (), h);
	return o->proc->status;
}

static int k_proc_done (u64 h)
{
	auto o = h_get (cur (), h, H_PROC);
	return o && o->proc->ended ? 1 : 0;
}

static long long spawn_ex (u64 attr)
{
	const struct kapi_spawn_attr *a = G<const struct kapi_spawn_attr> (attr, MEM_R);
	if (!a) return -KAPI_EFAULT;
	std::string p, cwd;
	if (!gstr ((u64) a->path, p, 1024)) return -KAPI_EFAULT;
	std::vector<std::string> argv = a->argv ? block_strings ((u64) a->argv) : std::vector<std::string> { p };
	if (argv.empty ()) argv.push_back (p);
	std::vector<std::string> env;
	if (a->envp) env = block_strings ((u64) a->envp);
	if (a->cwd) gstr ((u64) a->cwd, cwd, 1024);
	bool bad = false;
	auto I = stream_arg ((u64) a->in, &bad), O = stream_arg ((u64) a->out, &bad);
	if (bad) return -KAPI_EBADF;
	std::string h = host_path (p.c_str (), cur ()->cwd);
	std::error_code ec;
	if (h.empty () || !fs::is_regular_file (h, ec)) return -KAPI_ENOENT;
	Proc *P = start (p, argv, "", I, O, a->envp ? &env : 0, cwd.empty () ? "" : onyx_abs (cwd.c_str (), cur ()->cwd));
	return P ? (long long) proc_handle (P) : -KAPI_ENOMEM;
}
static long long k_spawn_ex (u64 attr) { return spawn_ex (attr); }
static long long k_spawn_ex2 (u64 attr, u64 handles, unsigned n) { (void) handles; (void) n; return spawn_ex (attr); }
static int k_get_handles (u64, unsigned) { return 0; }

static int k_proc_wait (u64 h, unsigned flags, u64 out)
{
	auto o = h_get (cur (), h, H_PROC);
	if (!o) return -KAPI_EBADF;
	Proc *P = o->proc;
	if (flags & KAPI_WAIT_NOHANG)
	{
		if (!P->ended) return 0;
	}
	else while (proc_wait_end (P, 50) == -1 && !P->ended) check_dying ();
	if (out)
	{
		struct kapi_proc_status *s = G<struct kapi_proc_status> (out, MEM_W);
		if (!s) return -KAPI_EFAULT;
		memset (s, 0, sizeof *s);
		s->code = P->status; s->reason = P->reason; s->pid = P->pid;
	}
	if (!(flags & KAPI_WAIT_KEEP)) h_drop (cur (), h);
	return 1;
}

// ---- the list, kills ---------------------------------------------------------------------------------------------
static int k_list_procs (u64 buf, unsigned cap)
{
	std::string s;
	int n = 0;
	for (Proc *P : proc_list ())
	{
		if (P->ended) continue;
		s += std::to_string (P->pid) + " a R " + std::to_string (P->mem.regions.size ()) + " " + P->name + "\n";
		n++;
	}
	gstr_out (buf, cap, s);
	return n;
}

static int k_list_tasks (u64 buf, unsigned cap)
{
	std::string s;
	for (Proc *P : proc_list ()) if (!P->ended) s += "Ra " + P->name + "\n";
	return gstr_out (buf, cap, s);
}

// A clean close for a process with a window (its should_exit, its pump woken), else the hard end.
static void kill_proc (Proc *P, bool force)
{
	extern bool ws_has_window (Proc *P);	// (k_ws.cpp)
	if (!force && ws_has_window (P))
	{
		std::lock_guard<std::mutex> L (P->m);
		P->exitAsked = true;
		P->pumpCV.notify_all ();
		return;
	}
	if (!P->dying.exchange (true)) { P->status = -9; P->reason = KAPI_PROC_KILLED; }
	cpu_stop_all (P);
}

static int k_kill_pid (int pid, int force)
{
	Proc *P = proc_find (pid);
	if (!P || P->ended) return 0;
	if (P == cur ()) return -1;
	kill_proc (P, force != 0);
	return 1;
}

static int k_kill (u64 name)
{
	std::string n;
	if (!gstr (name, n, 64)) return 0;
	for (Proc *P : proc_list ())
		if (!P->ended && P != cur () && strcasecmp (P->name.c_str (), n.c_str ()) == 0) { kill_proc (P, false); return 1; }
	return 0;
}

KAPI (launch, k_launch);
KAPI (exec, k_exec);
KAPI (exec_as, k_exec_as);
KAPI (spawn, k_spawn);
KAPI (wait, k_wait);
KAPI (proc_done, k_proc_done);
KAPI (spawn_ex, k_spawn_ex);
KAPI (spawn_ex2, k_spawn_ex2);
KAPI (get_handles, k_get_handles);
KAPI (proc_wait, k_proc_wait);
KAPI (list_procs, k_list_procs);
KAPI (list_tasks, k_list_tasks);
KAPI (kill_pid, k_kill_pid);
KAPI (kill, k_kill);
