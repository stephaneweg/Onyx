//
// main.cpp -- onyxrun: an Onyx program run on a PC, its Pi binary unchanged (docs/APP-RUNNER-STUDY.md).
//
//   onyxrun [options] PROGRAM [ARGS...]
//     PROGRAM   an Onyx path (SD:/bin/echo, SD:/apps/clock.app/main), a /bin tool's name (echo) or an app's (clock)
//     --root DIR   the folder standing for SD: (default: the sdcard/ beside the runner or above the working folder)
//     --ram DIR    the folder standing for RAM: (default: a folder in the host's temporary folder)
//     --cwd PATH   the program's working folder (default SD:/)
//     --trace      every system call on stderr (also ONYXRUN_TRACE=1)
//
// The runner's own messages start with "onyxrun:" on stderr; the program's output is on stdout; the exit status is
// the program's.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <filesystem>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace fs = std::filesystem;

u64 el0_main_return (void);

static void usage (void)
{
	fprintf (stderr,
		 "onyxrun -- an Onyx program's Pi binary run on this PC\n"
		 "usage: onyxrun [--root DIR] [--ram DIR] [--cwd PATH] [--trace] PROGRAM [ARGS...]\n"
		 "  PROGRAM: SD:/bin/echo, SD:/apps/clock.app/main, echo (a /bin tool), clock (an app)\n");
	exit (2);
}

static std::string find_root (const char *argv0)
{
	std::error_code ec;
	std::vector<fs::path> tries;
	fs::path exe = fs::absolute (argv0, ec).parent_path ();
	for (fs::path p = exe; !p.empty (); p = p.parent_path ())
	{
		tries.push_back (p / "sdcard");
		if (p == p.parent_path ()) break;
	}
	for (fs::path p = fs::current_path (ec); !p.empty (); p = p.parent_path ())
	{
		tries.push_back (p / "sdcard");
		if (p == p.parent_path ()) break;
	}
	for (auto &t : tries)
		if (fs::exists (t / "lib" / "appkit.so", ec)) return t.string ();
	return "";
}

// app.txt's "stack = N" (bytes, K or M), 0 if none
static u64 app_stack (const std::string &appDir)
{
	FILE *f = fopen ((appDir + "/app.txt").c_str (), "r");
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

int main (int argc, char **argv)
{
	std::string root, ram, cwd = "SD:/";
	int i = 1;
	g_Proc.trace = getenv ("ONYXRUN_TRACE") && atoi (getenv ("ONYXRUN_TRACE")) != 0;
	for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
	{
		if (!strcmp (argv[i], "--root") && i + 1 < argc) root = argv[++i];
		else if (!strcmp (argv[i], "--ram") && i + 1 < argc) ram = argv[++i];
		else if (!strcmp (argv[i], "--cwd") && i + 1 < argc) cwd = argv[++i];
		else if (!strcmp (argv[i], "--trace")) g_Proc.trace = true;
		else usage ();
	}
	if (i >= argc) usage ();
	if (root.empty ()) root = find_root (argv[0]);
	if (root.empty ()) { rlog ("no SD card folder (sdcard/ with lib/appkit.so): give --root"); return 2; }
	std::error_code ec;
	if (ram.empty ()) ram = (fs::temp_directory_path (ec) / "onyxrun-ram").string ();
	fs::create_directories (ram, ec);

	Process &P = g_Proc;
	P.root = fs::absolute (root, ec).string ();
	P.ramRoot = fs::absolute (ram, ec).string ();
	P.cwd = "SD:/";
	P.pid = 2;

	// the program: an Onyx path, a /bin tool's name, an app's name
	std::string prog = argv[i];
	std::string onyx;
	if (prog.find (':') != std::string::npos && prog.size () > 2 && prog[1] != ':') onyx = onyx_abs (prog.c_str ());
	else if (!host_path (("SD:/bin/" + prog).c_str ()).empty () && fs::is_regular_file (host_path (("SD:/bin/" + prog).c_str ()), ec))
		onyx = "SD:/bin/" + prog;
	else if (fs::is_regular_file (host_path (("SD:/apps/" + prog + ".app/main").c_str ()), ec))
		onyx = "SD:/apps/" + prog + ".app/main";
	else { rlog ("no program %s (not an Onyx path, nor in SD:/bin, nor SD:/apps)", prog.c_str ()); return 2; }
	std::string host = host_path (onyx.c_str ());
	if (host.empty () || !fs::is_regular_file (host, ec)) { rlog ("no file %s", onyx.c_str ()); return 2; }
	P.path = onyx;
	size_t app = onyx.find (".app/");
	if (app != std::string::npos)
	{
		size_t s = onyx.rfind ('/', app);
		P.name = onyx.substr (s + 1, app - s - 1);
	}
	else P.name = onyx.substr (onyx.rfind ('/') + 1);
	P.cwd = onyx_abs (cwd.c_str ());

	// argv, environment
	P.argv.insert (P.argv.end (), onyx.begin (), onyx.end ());
	P.argv.push_back (0);
	for (int k = i + 1; k < argc; k++) { P.argv.insert (P.argv.end (), argv[k], argv[k] + strlen (argv[k])); P.argv.push_back (0); }
	const char *env[] = { "PATH=SD:/bin", "HOME=SD:/", "TERM=vt100", "TZ=UTC" };
	for (const char *e : env) { P.env.insert (P.env.end (), e, e + strlen (e)); P.env.push_back (0); }

	if (!hm_init ()) return 2;
	kapi_init_table ();
	if (!load_appkit ()) return 2;
	u64 entry = load_program (host.c_str ());
	if (entry == 0) return 2;

	// the main stack (app.txt's stack, 8 MB at least, 64 MB at most) below USER_STACK_TOP; the heap at USER_HEAP_BASE
	u64 stack = app != std::string::npos ? app_stack (host_path (onyx.substr (0, app + 4).c_str ())) : 0;
	if (stack < EL0_USTACK_MIN) stack = EL0_USTACK_MIN;
	if (stack > EL0_USTACK_MAX) stack = EL0_USTACK_MAX;
	stack = (stack + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
	if (!hm_map (USER_STACK_TOP - stack, stack, MEM_R | MEM_W, "stack of the main thread")) return 2;
	P.heapBase = P.heapBrk = P.heapTop = USER_HEAP_BASE;

	if (P.trace) rlog ("%s: entry %llx", onyx.c_str (), (unsigned long long) entry);
	cpu_run_main (entry, USER_STACK_TOP, el0_main_return ());
}
