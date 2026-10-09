//
// main.cpp -- onyxrun: Onyx programs' Pi binaries run on this PC (docs/APP-RUNNER-STUDY.md).
//
//   onyxrun [options] PROGRAM [ARGS...]
//     PROGRAM   an Onyx path (SD:/bin/echo, SD:/apps/clock.app/main), a /bin tool's name (echo) or an app's (clock)
//     --root DIR   the folder standing for SD: (default: the sdcard/ beside the runner or above the working folder)
//     --ram DIR    the folder standing for RAM: (default: a folder in the host's temporary folder)
//     --cwd PATH   the program's working folder (default SD:/)
//     --trace      every system call on stderr (also ONYXRUN_TRACE=1)
//     --console    no graphics server: a console program alone (the default for SD:/bin tools)
//     --screen WxH the screen's size (default 1280x800); --headless: no window on the PC
//     --shot FILE.bmp [--shot-after MS]   the screen written there (after MS, default 5000), then the end
//     --input "SCRIPT"   input played (display.cpp: wait MS; move X Y; click X Y; key TEXT; ...)
//     --desktop    the whole desktop: Elegant, then the session (SD:/bin/session: the wallpaper, the menu bar, the
//                  dock...) and the system's services that need no network (clockd, clipd, printd, notifyd); the
//                  runner ends with Elegant (its window closed). PROGRAM, if given, is started too.
//
// An app (SD:/apps/...) is run with the graphics server: Elegant (SD:/bin/elegant --serve) is started first, its
// screen shown in a window of the PC (or kept in memory: --headless).
//
// The runner's own messages start with "onyxrun:" on stderr; the program's output is on stdout; the runner ends with
// the program it started, with its exit status.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include "display.h"
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
		 "usage: onyxrun [--root DIR] [--ram DIR] [--cwd PATH] [--trace] [--console | --screen WxH] [--headless]\n"
		 "               [--shot FILE.bmp [--shot-after MS]] [--input SCRIPT] PROGRAM [ARGS...]\n"
		 "       onyxrun --desktop [options] [PROGRAM [ARGS...]]   (the whole desktop)\n"
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

// A process ended: the runner's own (the one it started: no parent) -> the runner ends with its status. In
// --desktop, the runner ends with the graphics server instead.
static bool s_FollowServer = false;
void runner_follow_server (void) { s_FollowServer = true; }

void runner_proc_ended (Proc *P)
{
	if (s_FollowServer ? P->name == "elegant" : P->ppid == 0) runner_exit (P->status);
}

int main (int argc, char **argv)
{
	std::string root, ram, cwd = "SD:/";
	DisplayOpts D;
	int gui = -1;				// -1: an app gets the graphics server, a /bin tool not
	bool desktop = false;
	D.shotAfterMs = 5000;
	int i = 1;
	g_Run.trace = getenv ("ONYXRUN_TRACE") && atoi (getenv ("ONYXRUN_TRACE")) != 0;
	for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
	{
		if (!strcmp (argv[i], "--root") && i + 1 < argc) root = argv[++i];
		else if (!strcmp (argv[i], "--ram") && i + 1 < argc) ram = argv[++i];
		else if (!strcmp (argv[i], "--cwd") && i + 1 < argc) cwd = argv[++i];
		else if (!strcmp (argv[i], "--trace")) g_Run.trace = true;
		else if (!strcmp (argv[i], "--console")) gui = 0;
		else if (!strcmp (argv[i], "--desktop")) { desktop = true; gui = 1; }
		else if (!strcmp (argv[i], "--gui")) gui = 1;
		else if (!strcmp (argv[i], "--headless")) D.headless = true;
		else if (!strcmp (argv[i], "--screen") && i + 1 < argc) { sscanf (argv[++i], "%dx%d", &D.w, &D.h); gui = gui < 0 ? 1 : gui; }
		else if (!strcmp (argv[i], "--shot") && i + 1 < argc) D.shot = argv[++i];
		else if (!strcmp (argv[i], "--shot-after") && i + 1 < argc) D.shotAfterMs = atoi (argv[++i]);
		else if (!strcmp (argv[i], "--input") && i + 1 < argc) D.input = argv[++i];
		else usage ();
	}
	if (i >= argc && !desktop) usage ();
	if (root.empty ()) root = find_root (argv[0]);
	if (root.empty ()) { rlog ("no SD card folder (sdcard/ with lib/appkit.so): give --root"); return 2; }
	std::error_code ec;
	if (ram.empty ()) ram = (fs::temp_directory_path (ec) / "onyxrun-ram").string ();
	fs::create_directories (ram, ec);
	g_Run.root = fs::absolute (root, ec).string ();
	g_Run.ramRoot = fs::absolute (ram, ec).string ();

	// the program: an Onyx path, a /bin tool's name, an app's name
	std::string prog = i < argc ? argv[i] : "", onyx;
	if (desktop && prog.empty ()) prog = "SD:/bin/session";
	if (prog.find (':') != std::string::npos && prog.size () > 2 && prog[1] != ':') onyx = onyx_abs (prog.c_str (), "SD:/");
	else if (fs::is_regular_file (host_path (("SD:/bin/" + prog).c_str (), "SD:/"), ec)) onyx = "SD:/bin/" + prog;
	else if (fs::is_regular_file (host_path (("SD:/apps/" + prog + ".app/main").c_str (), "SD:/"), ec)) onyx = "SD:/apps/" + prog + ".app/main";
	else { rlog ("no program %s (not an Onyx path, nor in SD:/bin, nor SD:/apps)", prog.c_str ()); return 2; }

	if (gui < 0) gui = onyx.compare (0, 8, "SD:/apps") == 0 ? 1 : 0;
	if (gui)
	{
		if (D.w < 640 || D.h < 480 || D.w > 2560 || D.h > 1600) { rlog ("--screen: 640x480 .. 2560x1600"); return 2; }
		D.title = "Onyx -- " + prog;
		display_start (D);
		SpawnOpts e;
		e.path = "SD:/bin/elegant";
		e.argv = { "SD:/bin/elegant", "--serve" };
		e.ppid = -1;				// (not the runner's own program: its end does not end the runner)
		std::string why;
		if (!proc_spawn (e, &why)) { rlog ("cannot start Elegant: %s", why.c_str ()); return 2; }
		extern int ws_active (void);
		int t = 0;
		for (; t < 200 && !ws_active (); t++) std::this_thread::sleep_for (std::chrono::milliseconds (50));
		if (!ws_active ()) { rlog ("Elegant did not take the display"); return 2; }
		if (desktop)
		{
			// the session and the services without network (autostart's), none of them the runner's own program
			const char *svc[][2] = { { "SD:/bin/session", "" }, { "SD:/apps/clockd.app/main", "clockd" }, { "SD:/apps/clipd.app/main", "clipd" },
						 { "SD:/apps/printd.app/main", "printd" } };
			for (auto &sv : svc)
			{
				if (prog == sv[0]) continue;
				SpawnOpts so;
				so.path = sv[0]; so.argv = { sv[0] }; so.name = sv[1]; so.ppid = -1;
				std::string w;
				if (!proc_spawn (so, &w)) rlog ("%s: %s", sv[0], w.c_str ());
			}
			extern void runner_follow_server (void);
			runner_follow_server ();		// (the runner ends with Elegant)
		}
	}

	SpawnOpts o;
	if (desktop) o.ppid = -1;
	o.path = onyx;
	o.argv.push_back (onyx);
	for (int k = i + 1; k < argc; k++) o.argv.push_back (argv[k]);
	o.cwd = onyx_abs (cwd.c_str (), "SD:/");
	std::string why;
	Proc *P = proc_spawn (o, &why);
	if (!P) { rlog ("%s: %s", onyx.c_str (), why.c_str ()); return 2; }
	for (;;) std::this_thread::sleep_for (std::chrono::seconds (3600));
}
