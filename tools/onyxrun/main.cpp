//
// main.cpp -- onyxrun: Onyx programs' Pi binaries run on this PC (docs/APP-RUNNER-STUDY.md).
//
//   onyxrun [options] PROGRAM [ARGS...]
//     PROGRAM   an Onyx path (SD:/bin/echo, SD:/apps/clock.app/main), a /bin tool's name (echo) or an app's (clock)
//     --root DIR   the folder standing for SD: (default: the sdcard/ beside the runner or above the working folder)
//     --ram DIR    the folder standing for RAM: (default: a folder in the host's temporary folder)
//     --cwd PATH   the program's working folder (default SD:/)
//     --trace      every system call on stderr (also ONYXRUN_TRACE=1)
//
// The runner's own messages start with "onyxrun:" on stderr; the program's output is on stdout; the runner ends with
// the program it started, with its exit status.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <filesystem>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static void usage (void)
{
	fprintf (stderr,
		 "onyxrun -- Onyx programs' Pi binaries run on this PC\n"
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

[[noreturn]] static void runner_exit (int status)
{
	fflush (stdout);
	fflush (stderr);
#ifdef _WIN32
	ExitProcess ((unsigned) status);
#endif
	_exit (status & 0xFF);
}

// A process ended: the runner's own (the one it started: no parent) -> the runner ends with its status.
void runner_proc_ended (Proc *P)
{
	if (P->ppid == 0) runner_exit (P->status);
}

int main (int argc, char **argv)
{
	std::string root, ram, cwd = "SD:/";
	int i = 1;
	g_Run.trace = getenv ("ONYXRUN_TRACE") && atoi (getenv ("ONYXRUN_TRACE")) != 0;
	for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
	{
		if (!strcmp (argv[i], "--root") && i + 1 < argc) root = argv[++i];
		else if (!strcmp (argv[i], "--ram") && i + 1 < argc) ram = argv[++i];
		else if (!strcmp (argv[i], "--cwd") && i + 1 < argc) cwd = argv[++i];
		else if (!strcmp (argv[i], "--trace")) g_Run.trace = true;
		else usage ();
	}
	if (i >= argc) usage ();
	if (root.empty ()) root = find_root (argv[0]);
	if (root.empty ()) { rlog ("no SD card folder (sdcard/ with lib/appkit.so): give --root"); return 2; }
	std::error_code ec;
	if (ram.empty ()) ram = (fs::temp_directory_path (ec) / "onyxrun-ram").string ();
	fs::create_directories (ram, ec);
	g_Run.root = fs::absolute (root, ec).string ();
	g_Run.ramRoot = fs::absolute (ram, ec).string ();

	// the program: an Onyx path, a /bin tool's name, an app's name
	std::string prog = argv[i], onyx;
	if (prog.find (':') != std::string::npos && prog.size () > 2 && prog[1] != ':') onyx = onyx_abs (prog.c_str (), "SD:/");
	else if (fs::is_regular_file (host_path (("SD:/bin/" + prog).c_str (), "SD:/"), ec)) onyx = "SD:/bin/" + prog;
	else if (fs::is_regular_file (host_path (("SD:/apps/" + prog + ".app/main").c_str (), "SD:/"), ec)) onyx = "SD:/apps/" + prog + ".app/main";
	else { rlog ("no program %s (not an Onyx path, nor in SD:/bin, nor SD:/apps)", prog.c_str ()); return 2; }

	SpawnOpts o;
	o.path = onyx;
	o.argv.push_back (onyx);
	for (int k = i + 1; k < argc; k++) o.argv.push_back (argv[k]);
	o.cwd = onyx_abs (cwd.c_str (), "SD:/");
	std::string why;
	Proc *P = proc_spawn (o, &why);
	if (!P) { rlog ("%s: %s", onyx.c_str (), why.c_str ()); return 2; }
	for (;;) std::this_thread::sleep_for (std::chrono::seconds (3600));
}
