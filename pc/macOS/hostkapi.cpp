//
// pc/macOS/hostkapi.cpp -- the Onyx kernel's ABI table (kern/kapi_abi.h) on a POSIX host, so that Ledger
// (user/Apps/ledger), Letters (user/Apps/letters, which prints Ledger's documents), uikit and FreeType build
// for macOS from the Onyx sources, unchanged: the table is put where the apps look for it (KAPI_TABLE_VA)
// before any constructor of theirs runs, and filled with host equivalents of what they call. This file is
// the POSIX half (it also builds on Linux: pc/macOS/check.sh); the window's half is cocoa.mm.
//
//   files       "SD:/..." is the card, made of the user's folders over the read-only one in Ledger.app
//               (ONYX_SD_BASE: Contents/Resources/sd -- the fonts, the theme, the templates, the demo
//               company): "SD:/docs/..." is ~/Documents/Onyx Ledger (ONYX_DOCS: the books, the invoices,
//               the reports, seen in the Finder), the rest ~/Library/Application Support/Onyx Ledger
//               (ONYX_SD: the apps' settings, Ledger's templates -- copied there at the first start, to be
//               changed). A file is read from the user's folder, else the bundle's, and written in the
//               user's; a folder lists both. "HOME:/..." is the user's home folder, "MAC:/..." the whole
//               Mac ("MAC:/Volumes/USB/...").
//   programs    "SD:/apps/letters.app/main" is Ledger.app/Contents/Helpers/Letters.app; its arguments go
//               in ONYX_ARGS (a path with spaces stays whole). A program the bundle does not have (the
//               Spreadsheet, the File Viewer) -> the file or folder named is shown by macOS instead
//               (Numbers / Excel, the Finder).
//   threads     pthreads; wait_word: a condition variable, re-checked every millisecond.
//   the rest    no sound, MIDI, network or other processes' services (Ledger and Letters use none).
//
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <crt_externs.h>
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "host.h"

extern char **environ;

static TKApiTable *T;

// ---- paths ---------------------------------------------------------------------------------------------------
static std::string g_root, g_docs, g_base, g_helpers, g_home, g_app;	// the card (writable), its docs/, the bundle's
									// card, its helper apps, ~, the app's name

static bool is_dir (const std::string &p) { struct stat s; return !p.empty () && stat (p.c_str (), &s) == 0 && S_ISDIR (s.st_mode); }
static bool exists (const std::string &p) { struct stat s; return !p.empty () && stat (p.c_str (), &s) == 0; }
static void mkdirs (const std::string &p)		// (mkdir -p)
{
	for (size_t i = 1; i <= p.size (); i++)
		if (i == p.size () || p[i] == '/') mkdir (p.substr (0, i).c_str (), 0755);
}
static std::string parent (const std::string &p) { size_t s = p.find_last_of ('/'); return s == std::string::npos || s == 0 ? "/" : p.substr (0, s); }
static std::string exe_path ()
{
	char b[4096];
#ifdef __APPLE__
	uint32_t n = sizeof b;
	if (_NSGetExecutablePath (b, &n) != 0) return "";
	char r[4096];
	return realpath (b, r) ? r : b;
#else
	ssize_t n = readlink ("/proc/self/exe", b, sizeof b - 1);
	if (n <= 0) return "";
	b[n] = 0;
	return b;
#endif
}
static std::string env_or (const char *name, const std::string &def)
{
	const char *e = getenv (name);
	std::string s = e && e[0] ? e : def;
	while (s.size () > 1 && s.back () == '/') s.pop_back ();
	setenv (name, s.c_str (), 1);			// (the programs it starts: the same card)
	return s;
}
static void run (const char *const argv[])
{
	pid_t pid;
	if (posix_spawn (&pid, argv[0], 0, 0, (char **) argv, environ) == 0) { int st; waitpid (pid, &st, 0); }
}
// the user's language, two letters ("fr"): the Mac's first preferred language (Linux: $LANG)
static std::string system_language ()
{
	std::string l;
#ifdef __APPLE__
	CFArrayRef langs = CFLocaleCopyPreferredLanguages ();
	if (langs && CFArrayGetCount (langs) > 0)
	{
		char b[32];
		if (CFStringGetCString ((CFStringRef) CFArrayGetValueAtIndex (langs, 0), b, sizeof b, kCFStringEncodingUTF8)) l = b;
	}
	if (langs) CFRelease (langs);
#else
	const char *e = getenv ("LANG");
	if (e) l = e;
#endif
	l = l.substr (0, 2);
	for (auto &c : l) c = (char) tolower ((unsigned char) c);
	return l.size () == 2 ? l : "en";
}
static void init_paths ()
{
	const char *h = getenv ("HOME");
	g_home = h && h[0] ? h : "/tmp";
	std::string exe = exe_path ();
	// the app's name: ".../Ledger.app/Contents/MacOS/Ledger" -> "ledger"; the bundle: Ledger.app (Letters.app
	// is in its Contents/Helpers)
	size_t a = exe.rfind (".app/Contents/MacOS/"), hp = exe.find ("/Contents/Helpers/");
	if (a != std::string::npos) { size_t s = exe.rfind ('/', a); g_app = exe.substr (s + 1, a - s - 1); }
	else { size_t s = exe.find_last_of ('/'); g_app = exe.substr (s + 1); }
	for (auto &c : g_app) c = (char) tolower ((unsigned char) c);
	std::string bundle = hp != std::string::npos ? exe.substr (0, hp) : a != std::string::npos ? exe.substr (0, a + 4) : "";
	g_base = env_or ("ONYX_SD_BASE", bundle.empty () ? parent (exe) + "/sd" : bundle + "/Contents/Resources/sd");
	g_helpers = env_or ("ONYX_HELPERS", bundle.empty () ? parent (exe) + "/helpers" : bundle + "/Contents/Helpers");
	g_root = env_or ("ONYX_SD", g_home + "/Library/Application Support/Onyx Ledger");
	g_docs = env_or ("ONYX_DOCS", g_home + "/Documents/Onyx Ledger");
	mkdirs (g_root); mkdirs (g_docs);
	// the first start: the app's templates (Ledger's printing) copied to the user's folder, to be changed; and its
	// language, the Mac's when the app has its words in it (SD:/apps/<app>.app/lang/<code>.txt)
	std::string mine = g_root + "/apps/" + g_app + ".app", given = g_base + "/apps/" + g_app + ".app";
	if (!is_dir (mine + "/templates") && is_dir (given + "/templates"))
	{
		mkdirs (mine);
		std::string src = given + "/templates", dst = mine + "/";
		const char *const cp[] = { "/bin/cp", "-R", src.c_str (), dst.c_str (), 0 };
		run (cp);
	}
	if (!exists (mine + "/lang.txt"))
	{
		std::string code = system_language ();
		if (code != "en" && exists (given + "/lang/" + code + ".txt"))
		{
			mkdirs (mine);
			FILE *f = fopen ((mine + "/lang.txt").c_str (), "wb");
			if (f) { fputs (code.c_str (), f); fclose (f); }
		}
	}
}
// "SD:/a//b/" -> volume "SD", rest "a/b" (false: no volume of ours)
static bool split (const char *p, std::string &vol, std::string &rest)
{
	std::string s (p ? p : "");
	size_t c = s.find (':');
	if (c != std::string::npos && c > 0 && c < 6 && s.find ('/') > c) { vol = s.substr (0, c); s.erase (0, c + 1); }
	else vol = "SD";
	for (auto &ch : vol) ch = (char) toupper ((unsigned char) ch);
	rest.clear ();
	size_t i = 0;
	while (i < s.size ())
	{
		while (i < s.size () && s[i] == '/') i++;
		size_t j = s.find ('/', i); if (j == std::string::npos) j = s.size ();
		std::string part = s.substr (i, j - i);
		if (part == "..") { size_t k = rest.find_last_of ('/'); rest = k == std::string::npos ? "" : rest.substr (0, k); }
		else if (!part.empty () && part != ".") { if (!rest.empty ()) rest += '/'; rest += part; }
		i = j;
	}
	return vol == "SD" || vol == "HOME" || vol == "MAC";
}
static std::string join (const std::string &dir, const std::string &r) { return r.empty () ? dir : dir + "/" + r; }
static std::string card_writable (const std::string &r)	// a card path's place in the user's folders
{
	if (r == "docs") return g_docs;
	if (!r.compare (0, 5, "docs/")) return g_docs + "/" + r.substr (5);
	return join (g_root, r);
}
std::string host_path (const char *p, bool forWrite)
{
	std::string vol, r;
	if (!split (p, vol, r)) return "";
	if (vol == "MAC") return "/" + r;
	if (vol == "HOME") return join (g_home, r);
	std::string w = card_writable (r);
	if (forWrite || exists (w)) return w;
	std::string b = join (g_base, r);
	return exists (b) ? b : w;
}
static std::string base_path (const char *p)		// the bundle's copy of a card path ("" when not the card)
{
	std::string vol, r;
	if (!split (p, vol, r) || vol != "SD") return "";
	return join (g_base, r);
}
std::string onyx_path (const std::string &h)
{
	if (h == g_docs) return "SD:/docs";
	if (h.size () > g_docs.size () && !h.compare (0, g_docs.size () + 1, g_docs + "/")) return "SD:/docs/" + h.substr (g_docs.size () + 1);
	if (h.size () > g_root.size () && !h.compare (0, g_root.size () + 1, g_root + "/")) return "SD:/" + h.substr (g_root.size () + 1);
	if (h.size () > g_home.size () && !h.compare (0, g_home.size () + 1, g_home + "/")) return "HOME:/" + h.substr (g_home.size () + 1);
	return "MAC:" + h;
}
static std::string app_onyx_arg (const std::string &a)	// an argument macOS gives: a host path made an Onyx one
{
	return !a.empty () && a[0] == '/' && exists (a) ? onyx_path (a) : a;
}

bool host_open (const std::string &hp)
{
	if (hp.empty () || !exists (hp)) return false;
#ifdef __APPLE__
	const char *argv[] = { "/usr/bin/open", hp.c_str (), 0 };
#else
	const char *argv[] = { "/usr/bin/xdg-open", hp.c_str (), 0 };
#endif
	pid_t pid;
	if (posix_spawn (&pid, argv[0], 0, 0, (char **) argv, environ) != 0) return false;
	int st; waitpid (pid, &st, 0);
	return WIFEXITED (st) && WEXITSTATUS (st) == 0;
}

// ---- time -------------------------------------------------------------------------------------------------------
unsigned host_ticks ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) ((unsigned long long) t.tv_sec * 100 + (unsigned long long) t.tv_nsec / 10000000);
}
static unsigned h_get_ticks (void) { return host_ticks (); }
static void h_msleep (unsigned ms) { usleep ((useconds_t) ms * 1000); }
static void h_yield (void) { sched_yield (); }
static int h_get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *se)
{
	time_t now = time (0); struct tm t; localtime_r (&now, &t);
	if (y) *y = t.tm_year + 1900; if (mo) *mo = t.tm_mon + 1; if (d) *d = t.tm_mday;
	if (h) *h = t.tm_hour; if (mi) *mi = t.tm_min; if (se) *se = t.tm_sec;
	return 1;
}
static int h_random (void *buf, unsigned len)
{
#ifdef __APPLE__
	arc4random_buf (buf, len);
	return (int) len;
#else
	int f = ::open ("/dev/urandom", O_RDONLY);
	if (f < 0) return 0;
	unsigned got = 0;
	while (got < len) { ssize_t n = ::read (f, (char *) buf + got, len - got); if (n <= 0) break; got += (unsigned) n; }
	::close (f);
	return (int) got;
#endif
}

// ---- files --------------------------------------------------------------------------------------------------------
static void *f_open (const char *p)
{
	std::string h = host_path (p);
	if (h.empty () || is_dir (h)) return 0;
	return fopen (h.c_str (), "rb");
}
static int f_read (void *h, void *b, unsigned n) { return h ? (int) fread (b, 1, n, (FILE *) h) : -1; }
static unsigned long long f_fsize64 (void *h)
{
	if (!h) return 0;
	struct stat s;
	return fstat (fileno ((FILE *) h), &s) == 0 ? (unsigned long long) s.st_size : 0;
}
static unsigned f_fsize (void *h) { unsigned long long n = f_fsize64 (h); return n > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned) n; }
static int f_seek (void *h, unsigned long long pos) { return h && fseeko ((FILE *) h, (off_t) pos, SEEK_SET) == 0 ? 0 : -1; }
static void f_close (void *h) { if (h) fclose ((FILE *) h); }
static FILE *open_write (const char *p, const char *mode)
{
	std::string h = host_path (p, true);
	if (h.empty ()) return 0;
	if (!is_dir (parent (h)) && is_dir (parent (base_path (p)))) mkdirs (parent (h));	// (a folder of the bundle's card)
	return fopen (h.c_str (), mode);
}
static int save_file (const char *p, const void *b, unsigned n)
{
	FILE *f = open_write (p, "wb");
	if (!f) return -1;
	size_t w = n ? fwrite (b, 1, n, f) : 0;
	bool ok = fclose (f) == 0 && w == n;
	return ok ? (int) n : -1;		// (as the kernel's: the bytes written, or -1)
}
static int f_mkdir (const char *p)
{
	std::string h = host_path (p, true);
	if (h.empty ()) return -1;
	if (!is_dir (parent (h))) { std::string b = base_path (p); if (b.empty () || !is_dir (parent (b))) return -1; mkdirs (parent (h)); }
	return mkdir (h.c_str (), 0755) == 0 ? 0 : -1;
}
static int f_remove (const char *p)
{
	std::string h = host_path (p, true);
	if (h.empty () || !exists (h)) return -1;
	return (is_dir (h) ? rmdir (h.c_str ()) : unlink (h.c_str ())) == 0 ? 0 : -1;
}
static int f_rename (const char *a, const char *b)
{
	std::string ha = host_path (a, true), hb = host_path (b, true);
	if (ha.empty () || hb.empty () || !exists (ha) || exists (hb)) return -1;
	if (!is_dir (parent (hb))) mkdirs (parent (hb));
	return ::rename (ha.c_str (), hb.c_str ()) == 0 ? 0 : -1;
}
struct Dir { std::vector<kapi_dirent> e; size_t at; };
static void list_into (const std::string &h, std::vector<kapi_dirent> &out)
{
	DIR *d = opendir (h.c_str ());
	if (!d) return;
	while (struct dirent *de = readdir (d))
	{
		if (de->d_name[0] == '.') continue;			// (., .., .DS_Store, hidden files)
		bool dup = false;
		for (auto &x : out) if (!strcmp (x.name, de->d_name)) { dup = true; break; }
		if (dup) continue;
		std::string full = h + "/" + de->d_name;
		struct stat s;
		if (stat (full.c_str (), &s) != 0) continue;
		kapi_dirent e; memset (&e, 0, sizeof e);
		snprintf (e.name, sizeof e.name, "%s", de->d_name);
		e.is_dir = S_ISDIR (s.st_mode) ? 1 : 0;
		e.size = e.is_dir ? 0 : (s.st_size > 0xFFFFFFFFll ? 0xFFFFFFFFu : (unsigned) s.st_size);
		out.push_back (e);
	}
	closedir (d);
}
static void *f_opendir (const char *p)
{
	std::string h = host_path (p, true), b = base_path (p);
	bool hd = is_dir (h), bd = !b.empty () && is_dir (b);
	if (!hd && !bd) return 0;
	Dir *d = new Dir; d->at = 0;
	if (hd) list_into (h, d->e);
	if (bd) list_into (b, d->e);
	std::sort (d->e.begin (), d->e.end (), [] (const kapi_dirent &x, const kapi_dirent &y) { return strcasecmp (x.name, y.name) < 0; });
	return d;
}
static int f_readdir (void *h, struct kapi_dirent *e)
{
	Dir *d = (Dir *) h;
	if (!d) return -1;
	if (d->at >= d->e.size ()) return 0;
	*e = d->e[d->at++];
	return 1;
}
static void f_closedir (void *h) { delete (Dir *) h; }
static int app_dir (char *b, unsigned n) { snprintf (b, n, "SD:/apps/%s.app/", g_app.c_str ()); return (int) strlen (b); }	// (its length, as the kernel's)
static int h_chdir (const char *) { return 1; }
static int h_getcwd (char *b, unsigned n) { snprintf (b, n, "SD:/"); return 1; }

// ---- console ------------------------------------------------------------------------------------------------------
static int h_write (int fd, const void *b, unsigned n) { ssize_t w = ::write (fd == 2 ? 2 : 1, b, n); (void) w; return (int) n; }
static int stdout_write (const void *b, unsigned n) { return h_write (1, b, n); }
static int stdin_read (void *b, unsigned n) { ssize_t r = ::read (0, b, n); return r > 0 ? (int) r : 0; }

// ---- threads, futex -----------------------------------------------------------------------------------------------
struct Thread { pthread_t t; int (*fn) (void *); void *arg; int code; };
static std::vector<Thread *> g_threads;		// tid = index + 2 (1: the main thread)
static pthread_mutex_t g_thLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_mainThread;
static void *thread_tramp (void *p) { Thread *t = (Thread *) p; t->code = t->fn (t->arg); return 0; }
static int thread_create (int (*fn) (void *), void *arg, unsigned stack, const char *)
{
	Thread *t = new Thread; t->fn = fn; t->arg = arg; t->code = 0;
	pthread_attr_t at; pthread_attr_init (&at);
	pthread_attr_setstacksize (&at, stack < 262144 ? 262144 : (stack + 16383) & ~16383u);
	int e = pthread_create (&t->t, &at, thread_tramp, t);
	pthread_attr_destroy (&at);
	if (e) { delete t; return -1; }
	pthread_mutex_lock (&g_thLock);
	int tid = -1;
	for (size_t i = 0; i < g_threads.size (); i++) if (!g_threads[i]) { g_threads[i] = t; tid = (int) i + 2; break; }
	if (tid < 0) { g_threads.push_back (t); tid = (int) g_threads.size () + 1; }
	pthread_mutex_unlock (&g_thLock);
	return tid;
}
static int thread_priority (int, int) { return 0; }
static int thread_join (int tid, unsigned timeout, int *code)
{
	if (tid < 2) return -1;
	pthread_mutex_lock (&g_thLock);
	Thread *t = tid - 2 < (int) g_threads.size () ? g_threads[tid - 2] : 0;
	pthread_mutex_unlock (&g_thLock);
	if (!t) return -1;
	if (timeout != KAPI_WAIT_FOREVER)
	{
		unsigned t0 = host_ticks ();
		for (;;)			// (no timed join everywhere: the thread's end polled)
		{
			if (pthread_kill (t->t, 0) == ESRCH) break;
			if ((host_ticks () - t0) * 10 >= timeout) return 1;
			usleep (1000);
		}
	}
	pthread_join (t->t, 0);
	if (code) *code = t->code;
	pthread_mutex_lock (&g_thLock); g_threads[tid - 2] = 0; pthread_mutex_unlock (&g_thLock);
	delete t;
	return 0;
}
static void thread_exit (int code)
{
	pthread_t me = pthread_self ();
	pthread_mutex_lock (&g_thLock);
	for (Thread *t : g_threads) if (t && pthread_equal (t->t, me)) t->code = code;
	pthread_mutex_unlock (&g_thLock);
	pthread_exit (0);
}
static int thread_self (void)
{
	pthread_t me = pthread_self ();
	if (pthread_equal (me, g_mainThread)) return 1;
	pthread_mutex_lock (&g_thLock);
	int r = 0;
	for (size_t i = 0; i < g_threads.size (); i++) if (g_threads[i] && pthread_equal (g_threads[i]->t, me)) r = (int) i + 2;
	pthread_mutex_unlock (&g_thLock);
	return r;
}
static int core_acquire (void) { return -1; }
static int core_run (int, void (*) (void *), void *, void *) { return -1; }
static int core_state (int) { return KAPI_CORE_NOTYOURS; }
static void core_release (int) { }

static pthread_mutex_t g_wordLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wordCond = PTHREAD_COND_INITIALIZER;
static int wait_word (volatile unsigned *addr, unsigned expected, unsigned timeout)
{
	if (!addr || ((unsigned long) addr & 3)) return -1;
	if (*addr != expected) return 0;
	if (timeout == 0) return 1;
	unsigned t0 = host_ticks ();
	pthread_mutex_lock (&g_wordLock);
	int r = 0;
	for (;;)
	{
		if (*addr != expected) break;
		if (timeout != KAPI_WAIT_FOREVER && (host_ticks () - t0) * 10 >= timeout) { r = 1; break; }
		struct timeval now; gettimeofday (&now, 0);
		struct timespec until; until.tv_sec = now.tv_sec; until.tv_nsec = now.tv_usec * 1000 + 1000000;	// (1 ms: a change
		if (until.tv_nsec >= 1000000000) { until.tv_sec++; until.tv_nsec -= 1000000000; }		//  nobody woke us for)
		pthread_cond_timedwait (&g_wordCond, &g_wordLock, &until);
	}
	pthread_mutex_unlock (&g_wordLock);
	return r;
}
static int wake_word (volatile unsigned *addr)
{
	if (!addr) return -1;
	pthread_mutex_lock (&g_wordLock); pthread_cond_broadcast (&g_wordCond); pthread_mutex_unlock (&g_wordLock);
	return 1;
}

// ---- processes ---------------------------------------------------------------------------------------------------
struct Proc { pid_t pid; std::string name; bool done; int code; };
static std::vector<Proc *> g_procs;
static pthread_mutex_t g_procLock = PTHREAD_MUTEX_INITIALIZER;
static std::string g_args;

static bool reap (Proc *p)			// (its end noted) -> true: it ended
{
	if (p->done) return true;
	int st;
	if (waitpid (p->pid, &st, WNOHANG) == p->pid) { p->done = true; p->code = WIFEXITED (st) ? WEXITSTATUS (st) : 128 + WTERMSIG (st); }
	return p->done;
}
static bool executable (const std::string &p) { struct stat s; return !p.empty () && stat (p.c_str (), &s) == 0 && S_ISREG (s.st_mode) && (s.st_mode & 0111); }
// an Onyx program -> its executable ("" when the Mac has none): "SD:/apps/letters.app/main" is the helper
// Ledger.app/Contents/Helpers/Letters.app (its Contents/MacOS/Letters)
static std::string program (const char *path)
{
	std::string h = host_path (path);
	if (executable (h)) return h;
	std::string vol, r;
	if (!split (path, vol, r)) return "";
	size_t a = r.rfind (".app");
	if (a == std::string::npos || (a + 4 != r.size () && r.compare (a, std::string::npos, ".app/main"))) return "";
	size_t s = r.find_last_of ('/', a);
	std::string x = r.substr (s == std::string::npos ? 0 : s + 1, a - (s == std::string::npos ? 0 : s + 1)), X = x;
	if (X.empty ()) return "";
	{
		std::string lx = x; for (auto &c : lx) c = (char) tolower ((unsigned char) c);
		if (lx == g_app) return exe_path ();		// (the app itself, started again: Ledger in another language)
	}
	X[0] = (char) toupper ((unsigned char) X[0]);
	std::string m = g_helpers + "/" + X + ".app/Contents/MacOS/" + X;
	if (executable (m)) return m;
	m = g_helpers + "/" + x + ".app/Contents/MacOS/main";
	return executable (m) ? m : "";
}
static Proc *start (const char *path, const char *args, const char *name, int in, int out)
{
	std::string exe = program (path);
	if (exe.empty ()) return 0;
	std::vector<std::string> env;
	for (char **e = environ; *e; e++) if (strncmp (*e, "ONYX_ARGS=", 10)) env.push_back (*e);
	env.push_back (std::string ("ONYX_ARGS=") + (args ? args : ""));
	std::vector<char *> envp; for (auto &e : env) envp.push_back ((char *) e.c_str ()); envp.push_back (0);
	char *argv[] = { (char *) exe.c_str (), 0 };
	posix_spawn_file_actions_t fa; posix_spawn_file_actions_init (&fa);
	if (in >= 0) posix_spawn_file_actions_adddup2 (&fa, in, 0);
	if (out >= 0) { posix_spawn_file_actions_adddup2 (&fa, out, 1); posix_spawn_file_actions_adddup2 (&fa, out, 2); }
	pid_t pid;
	int e = posix_spawn (&pid, exe.c_str (), &fa, 0, argv, envp.data ());
	posix_spawn_file_actions_destroy (&fa);
	if (e) return 0;
	Proc *p = new Proc; p->pid = pid; p->done = false; p->code = 0;
	if (name && name[0]) p->name = name;
	else
	{
		std::string s = path ? path : "";
		size_t k = s.rfind (".app/"); if (k != std::string::npos) s.erase (k);	// ("SD:/apps/letters.app/main" -> "letters")
		k = s.find_last_of ("/:"); p->name = k == std::string::npos ? s : s.substr (k + 1);
	}
	pthread_mutex_lock (&g_procLock); g_procs.push_back (p); pthread_mutex_unlock (&g_procLock);
	return p;
}
static int exec_as (const char *path, const char *args, const char *name)
{
	if (start (path, args, name, -1, -1)) return 1;
	// a program the Mac does not have: the file (or folder) it was to open, shown by macOS
	std::string a = args ? args : "";
	while (!a.empty () && a[0] == ' ') a.erase (0, 1);
	if (a.empty () || a[0] == '-') return 0;
	return host_open (host_path (a.c_str ())) ? 1 : 0;
}
static int h_exec (const char *path, const char *args) { return exec_as (path, args, 0); }
static int list_procs (char *buf, unsigned size)
{
	std::string s = std::to_string (getpid ()) + " a R 0 " + g_app + "\n";
	pthread_mutex_lock (&g_procLock);
	for (Proc *p : g_procs) if (!reap (p)) s += std::to_string (p->pid) + " a R 0 " + p->name + "\n";
	pthread_mutex_unlock (&g_procLock);
	if (!size) return 0;
	unsigned n = (unsigned) s.size () < size ? (unsigned) s.size () : size - 1;
	memcpy (buf, s.data (), n); buf[n] = 0;
	return (int) n;
}
static int kill_pid (int pid, int)
{
	int r = 0;
	pthread_mutex_lock (&g_procLock);
	for (Proc *p : g_procs) if (p->pid == pid && !reap (p)) { ::kill (p->pid, SIGTERM); r = 1; }
	pthread_mutex_unlock (&g_procLock);
	return r;
}
static int h_kill (const char *name)
{
	int r = 0;
	pthread_mutex_lock (&g_procLock);
	for (Proc *p : g_procs) if (name && p->name == name && !reap (p)) { ::kill (p->pid, SIGTERM); r++; }
	pthread_mutex_unlock (&g_procLock);
	return r;
}
static int get_args (char *b, unsigned n)
{
	if (!n) return 0;
	snprintf (b, n, "%s", g_args.c_str ());
	return (int) strlen (b);
}
static void init_args ()
{
	const char *e = getenv ("ONYX_ARGS");			// (started by another Onyx program: its arguments, whole)
	if (e) { g_args = e; unsetenv ("ONYX_ARGS"); return; }
	std::vector<std::string> av;
#ifdef __APPLE__
	int n = *_NSGetArgc (); char **v = *_NSGetArgv ();
	for (int i = 1; i < n; i++) av.push_back (v[i]);
#else
	FILE *f = fopen ("/proc/self/cmdline", "rb");
	if (f)
	{
		std::string all; char c[4096]; size_t k;
		while ((k = fread (c, 1, sizeof c, f)) > 0) all.append (c, k);
		fclose (f);
		size_t i = all.find ('\0');				// (argv[0] skipped)
		while (i != std::string::npos && i + 1 < all.size ()) { size_t j = all.find ('\0', i + 1); av.push_back (all.substr (i + 1, j - i - 1)); i = j; }
	}
#endif
	for (auto &a : av)
	{
		if (!a.compare (0, 5, "-psn_") || !a.compare (0, 10, "-NSDocumentRevisionsDebugMode")) continue;	// (macOS's own)
		if (!g_args.empty ()) g_args += ' ';
		g_args += app_onyx_arg (a);
	}
}

// streams: in-memory pipes (a child's stdin / stdout when spawned), files written
struct Stream
{
	int kind;				// 0 a pipe, 1 a file written
	pthread_mutex_t m;
	std::string buf; bool eof;
	int osWrite;				// a child's stdin: what is written goes there
	FILE *f;
	int refs;
};
static Stream *new_stream (int kind)
{
	Stream *s = new Stream; s->kind = kind; pthread_mutex_init (&s->m, 0); s->eof = false; s->osWrite = -1; s->f = 0; s->refs = 1;
	return s;
}
static void unref (Stream *s)
{
	pthread_mutex_lock (&s->m); int r = --s->refs; pthread_mutex_unlock (&s->m);
	if (r > 0) return;
	if (s->osWrite >= 0) ::close (s->osWrite);
	if (s->f) fclose (s->f);
	pthread_mutex_destroy (&s->m);
	delete s;
}
static void *h_pipe (void) { return new_stream (0); }
static void *file_out (const char *p, int append)
{
	FILE *f = open_write (p, append ? "ab" : "wb");
	if (!f) return 0;
	Stream *s = new_stream (1); s->f = f;
	return s;
}
static int stream_write (void *h, const void *b, unsigned n)
{
	Stream *s = (Stream *) h;
	if (!s) return -1;
	if (s->kind == 1) return (int) fwrite (b, 1, n, s->f);
	pthread_mutex_lock (&s->m);
	int r = (int) n;
	if (s->osWrite >= 0) { ssize_t w = ::write (s->osWrite, b, n); r = w < 0 ? -1 : (int) w; }
	else s->buf.append ((const char *) b, n);
	pthread_mutex_unlock (&s->m);
	return r;
}
static int stream_read_nb (void *h, void *b, unsigned n)
{
	Stream *s = (Stream *) h;
	if (!s || s->kind != 0) return 0;
	pthread_mutex_lock (&s->m);
	int r;
	if (!s->buf.empty ()) { r = (int) (s->buf.size () < n ? s->buf.size () : n); memcpy (b, s->buf.data (), r); s->buf.erase (0, r); }
	else r = s->eof ? 0 : -1;
	pthread_mutex_unlock (&s->m);
	return r;
}
static int stream_read (void *h, void *b, unsigned n)
{
	for (;;) { int r = stream_read_nb (h, b, n); if (r >= 0) return r; usleep (2000); }
}
static void stream_eof (void *h)
{
	Stream *s = (Stream *) h;
	if (!s) return;
	pthread_mutex_lock (&s->m);
	s->eof = true;
	if (s->osWrite >= 0) { ::close (s->osWrite); s->osWrite = -1; }
	pthread_mutex_unlock (&s->m);
}
static void stream_close (void *h)
{
	Stream *s = (Stream *) h;
	if (!s) return;
	if (s->kind == 1 && s->f) { fclose (s->f); s->f = 0; }
	stream_eof (s); unref (s);
}
struct Reader { int rd; Stream *to; };
static void *reader (void *p)
{
	Reader *r = (Reader *) p;
	char b[4096]; ssize_t n;
	while ((n = ::read (r->rd, b, sizeof b)) > 0) { pthread_mutex_lock (&r->to->m); r->to->buf.append (b, (size_t) n); pthread_mutex_unlock (&r->to->m); }
	pthread_mutex_lock (&r->to->m); r->to->eof = true; pthread_mutex_unlock (&r->to->m);
	::close (r->rd); unref (r->to); delete r;
	return 0;
}
static void *spawn (const char *path, const char *args, void *in, void *out)
{
	Stream *si = (Stream *) in, *so = (Stream *) out;
	int pin[2] = { -1, -1 }, pout[2] = { -1, -1 };
	if (si && si->kind == 0 && pipe (pin) != 0) return 0;
	if (so && so->kind == 0 && pipe (pout) != 0) { if (pin[0] >= 0) { ::close (pin[0]); ::close (pin[1]); } return 0; }
	if (pin[1] >= 0) fcntl (pin[1], F_SETFD, FD_CLOEXEC);
	if (pout[0] >= 0) fcntl (pout[0], F_SETFD, FD_CLOEXEC);
	Proc *p = start (path, args, 0, pin[0], pout[1]);
	if (pin[0] >= 0) ::close (pin[0]);
	if (pout[1] >= 0) ::close (pout[1]);
	if (!p) { if (pin[1] >= 0) ::close (pin[1]); if (pout[0] >= 0) ::close (pout[0]); return 0; }
	if (pin[1] >= 0)
	{
		pthread_mutex_lock (&si->m);
		if (!si->buf.empty ()) { ssize_t w = ::write (pin[1], si->buf.data (), si->buf.size ()); (void) w; si->buf.clear (); }
		if (si->eof) ::close (pin[1]); else si->osWrite = pin[1];
		pthread_mutex_unlock (&si->m);
	}
	if (pout[0] >= 0)
	{
		Reader *r = new Reader; r->rd = pout[0]; r->to = so;
		pthread_mutex_lock (&so->m); so->refs++; pthread_mutex_unlock (&so->m);
		pthread_t t;
		if (pthread_create (&t, 0, reader, r) == 0) pthread_detach (t);
	}
	return p;
}
static int proc_done (void *h)
{
	Proc *p = (Proc *) h;
	if (!p) return 1;
	pthread_mutex_lock (&g_procLock); bool d = reap (p); pthread_mutex_unlock (&g_procLock);
	return d ? 1 : 0;
}
static int h_wait (void *h)
{
	Proc *p = (Proc *) h;
	if (!p) return -1;
	while (!proc_done (p)) usleep (5000);
	return p->code;
}

// ---- IPC: this process's own services only (no other Onyx program to talk to: no clipd, notifyd...) ------------------
static std::vector<std::string> g_services;
static int ipc_register (const char *name) { g_services.push_back (name ? name : ""); return 1; }
static int ipc_lookup (const char *) { return 0; }
static int mailbox_send (int, int, const void *, unsigned) { return 0; }
static int mailbox_recv (int *, int *, void *, unsigned, int blocking) { if (blocking) usleep (50000); return -1; }
static int surface_create (int, int) { return -1; }
static unsigned *surface_map (int) { return 0; }
static int surface_size (int, int *, int *) { return 0; }
static void surface_present (int) { }
static int surface_destroy (int) { return 0; }

// ---- no sound, MIDI or network ------------------------------------------------------------------------------------------
static int sound_acquire (void) { return 0; }
static void sound_release (void) { }
static int sound_config (int, int) { return -1; }
static struct kapi_sound_ring *sound_map (void) { return 0; }
static int sound_write (const short *, unsigned) { return 0; }
static int sound_status (unsigned *rate, unsigned *freeF, unsigned *owner) { if (rate) *rate = SOUND_RATE; if (freeF) *freeF = 0; if (owner) *owner = 0; return 0; }
static int sound_volume (int, int) { return 10; }
static int midi_read (struct kapi_midi_event *, int) { return 0; }
static int midi_devices (void) { return 0; }
static int net_status (char *ip, unsigned cap) { if (ip && cap) ip[0] = 0; return 0; }
static int net_info (char *b, unsigned cap) { if (b && cap) b[0] = 0; return 0; }
static int tcp_connect (const char *, unsigned) { return -1; }
static int tcp_send (int, const void *, unsigned) { return -1; }
static int tcp_recv (int, void *, unsigned) { return -1; }
static void tcp_close (int) { }

// ---- the rest ------------------------------------------------------------------------------------------------------------------
static int list_apps (char *b, unsigned n) { if (n) b[0] = 0; return 0; }
static int list_windows (char *b, unsigned n) { if (n) b[0] = 0; return 0; }
static int launch (const char *) { return 0; }
static int raise_app (const char *) { return 0; }
static int toggle_app (const char *) { return 0; }
static int wheel_speed = 3;
static void set_wheel (int n) { wheel_speed = n; }
static int get_wheel (void) { return wheel_speed; }
static int meminfo (unsigned long *t, unsigned long *f, unsigned long *a, unsigned *pg)
{
	if (t) *t = 8ul << 20; if (f) *f = 4ul << 20; if (a) *a = 0; if (pg) *pg = 4;	// (KB: as a large Pi's)
	return 1;
}
static void *h_sbrk (long) { return (void *) -1; }
static void *k_memset (void *d, int c, unsigned long n) { return memset (d, c, n); }
static void *k_memcpy (void *d, const void *s, unsigned long n) { return memcpy (d, s, n); }
static void *k_memmove (void *d, const void *s, unsigned long n) { return memmove (d, s, n); }
static void h_exit (int s) { exit (s); }

static void unimplemented (void)
{
	char b[200];
	snprintf (b, sizeof b, "This program called an Onyx function the Mac does not have (from %p).", __builtin_return_address (0));
	gui_fatal (b);
}

// the table at KAPI_TABLE_VA (where the apps' appkit.h reads it): taken only if that place is free
static void *place_table ()
{
	const size_t size = 65536;
#ifdef __APPLE__
	mach_vm_address_t a = (mach_vm_address_t) KAPI_TABLE_VA;
	if (mach_vm_allocate (mach_task_self (), &a, size, VM_FLAGS_FIXED) != KERN_SUCCESS) return 0;
	return (void *) a;
#else
	void *p = mmap ((void *) KAPI_TABLE_VA, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	return p == (void *) KAPI_TABLE_VA ? p : 0;
#endif
}

static void setup (void)
{
	void *p = place_table ();
	if (!p) { fprintf (stderr, "onyx: the kernel's table could not be placed at %#llx\n", (unsigned long long) KAPI_TABLE_VA); _exit (3); }
	T = (TKApiTable *) p;
	void **slots = (void **) T;
	for (size_t i = 0; i < sizeof (TKApiTable) / sizeof (void *); i++) slots[i] = (void *) unimplemented;
	T->version = KAPI_ABI_VERSION;
	// (v75) the POSIX entries absent here: 0, so appkit.h's wrappers return -KAPI_ENOSYS
	for (size_t i = __builtin_offsetof (TKApiTable, vm_map) / 8; i < sizeof (TKApiTable) / 8; i++) ((void **) T)[i] = 0;
	g_mainThread = pthread_self ();
	signal (SIGPIPE, SIG_IGN);
	init_paths ();
	init_args ();
	T->msleep = h_msleep; T->get_ticks = h_get_ticks; T->yield = h_yield; T->exit = h_exit;
	T->open = f_open; T->read = f_read; T->write = h_write; T->fsize = f_fsize; T->fsize64 = f_fsize64; T->seek = f_seek; T->close = f_close;
	T->save_file = save_file; T->opendir = f_opendir; T->readdir = f_readdir; T->closedir = f_closedir;
	T->mkdir = f_mkdir; T->remove = f_remove; T->rename = f_rename; T->app_dir = app_dir; T->chdir = h_chdir; T->getcwd = h_getcwd;
	T->stdout_write = stdout_write; T->stdin_read = stdin_read; T->get_args = get_args; T->get_datetime = h_get_datetime; T->random = h_random;
	T->pipe = h_pipe; T->file_out = file_out; T->stream_read = stream_read; T->stream_read_nb = stream_read_nb; T->stream_write = stream_write;
	T->stream_close = stream_close; T->stream_eof = stream_eof; T->spawn = spawn; T->proc_done = proc_done; T->wait = h_wait;
	T->exec = h_exec; T->exec_as = exec_as; T->list_procs = list_procs; T->kill_pid = kill_pid; T->kill = h_kill;
	T->thread_create = thread_create; T->thread_join = thread_join; T->thread_exit = thread_exit; T->thread_self = thread_self;
	T->thread_priority = thread_priority; T->core_acquire = core_acquire; T->core_run = core_run; T->core_state = core_state; T->core_release = core_release;
	T->wait_word = wait_word; T->wake_word = wake_word;
	T->ipc_register = ipc_register; T->ipc_lookup = ipc_lookup; T->mailbox_send = mailbox_send; T->mailbox_recv = mailbox_recv;
	T->surface_create = surface_create; T->surface_map = surface_map; T->surface_size = surface_size;
	T->surface_present = surface_present; T->surface_destroy = surface_destroy;
	T->sound_acquire = sound_acquire; T->sound_release = sound_release; T->sound_write = sound_write; T->sound_status = sound_status;
	T->sound_config = sound_config; T->sound_map = sound_map; T->sound_volume = sound_volume;
	T->midi_read = midi_read; T->midi_devices = midi_devices;
	T->net_status = net_status; T->net_info = net_info; T->tcp_connect = tcp_connect; T->tcp_send = tcp_send; T->tcp_recv = tcp_recv; T->tcp_close = tcp_close;
	T->list_apps = list_apps; T->list_windows = list_windows; T->launch = launch; T->raise_app = raise_app; T->toggle_app = toggle_app;
	T->set_wheel_speed = set_wheel; T->get_wheel_speed = get_wheel; T->meminfo = meminfo; T->sbrk = h_sbrk;
	T->memset = k_memset; T->memcpy = k_memcpy; T->memmove = k_memmove;
	gui_setup (T);
}

// Before every other constructor (the apps' globals may call the kapi): this object is the first one
// linked (build.sh, check.sh) -- both Mach-O and ELF run the files' initialisers in the link's order --,
// and a file's static objects are made in their order: this one last here, after the strings it fills.
static struct HostKapiInit { HostKapiInit () { setup (); } } s_init;
