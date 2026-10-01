//
// pc/Jet/winkapi.cpp -- the Onyx kernel's ABI table (kern/kapi_abi.h) on Windows, for Jet Browser (NetSurf on
// Onyx: user/netsurf, third_party/netsurf, wtk) built for Windows from the Onyx sources. Grown from Koton's
// (pc/Koton/winkapi.cpp): the table is put where the apps look for it (KAPI_TABLE_VA) before any constructor
// runs, and filled with Win32 equivalents of what the browser calls.
//
//   the window     a Windows window; its client area is the app's canvas (no Onyx frame: get_chrome says
//                  "borderless"). When Windows resizes it, the app hears the frame's maximise button
//                  (GUI_EVENT_WINCTL KAPI_FRAME_MAXIMISE: wtk's Root fills the work area = the client area).
//   files          "SD:/x" and "/x" are the folder of Jet.exe (so "/res/..." is res\, "/data/..." data\);
//                  "RAM:/x" is data\ram\x (kept across launches: a RAM: that never restarts); "C:/..." and
//                  "/C:/..." a Windows path. The C library's file calls of NetSurf are wrapped at the link
//                  (__wrap_fopen, __wrap_open, __wrap_stat... below): the same paths as on the Pi.
//   network        Winsock (tcp_*, net_resolve); a lock around the socket table (the downloads' threads).
//   threads        Win32 threads; mutexes, events, barriers; wait_word (WaitOnAddress); post + pump_wait.
//   sound          waveOut: the PCM stream (sound_write, sound_status; 44100 Hz stereo) for <video> / <audio>.
//   no GPU         gpu_info answers 0: Choices' gpu_compositing falls back to the CPU path.
//   logs           stderr / stdout go to data\jet.log (a new one each launch), or to a console window
//                  (--console, or a file "console" beside Jet.exe). Switches: --perf, --jsdebug,
//                  --netdebug, NAME=value (any NS_* variable), or the empty files perf / jsdebug / netdebug
//                  beside Jet.exe. --maximised: the window opens maximised; --sharp: the page's pixels 1:1 on
//                  a scaled display (Windows' scaling otherwise: the size of 100 %, a little blurred).
//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <bcrypt.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#undef MOD_ALT
#undef MB_OK
#undef MB_OKCANCEL
#undef MB_YESNO
#undef MB_YESNOCANCEL
#undef SHUTDOWN_RESTART
#undef MOD_SHIFT
#undef MOD_CONTROL
#include "regex.h"		// (pc/Jet/compat)
#include "iconv.h"
#include "kapi.h"

static TKApiTable *T;

// ---- text: UTF-8 <-> UTF-16 --------------------------------------------------------------------------------
static std::wstring wide (const char *s, int n = -1)
{
	if (!s) return std::wstring ();
	int k = MultiByteToWideChar (CP_UTF8, 0, s, n, 0, 0);
	std::wstring w (k > 0 ? k : 0, L'\0');
	if (k > 0) MultiByteToWideChar (CP_UTF8, 0, s, n, &w[0], k);
	while (!w.empty () && w.back () == L'\0') w.pop_back ();
	return w;
}
static std::string utf8 (const wchar_t *s, int n = -1)
{
	if (!s) return std::string ();
	int k = WideCharToMultiByte (CP_UTF8, 0, s, n, 0, 0, 0, 0);
	std::string u (k > 0 ? k : 0, '\0');
	if (k > 0) WideCharToMultiByte (CP_UTF8, 0, s, n, &u[0], k, 0, 0);
	while (!u.empty () && u.back () == '\0') u.pop_back ();
	return u;
}

// ---- paths ---------------------------------------------------------------------------------------------------
static std::wstring g_root __attribute__ ((init_priority (101)));	// "SD:" (no trailing separator)

static void init_root ()
{
	wchar_t b[MAX_PATH * 2];
	DWORD n = GetEnvironmentVariableW (L"JET_ROOT", b, MAX_PATH * 2);	// (a test: another folder)
	if (n > 0 && n < MAX_PATH * 2) { g_root = b; return; }
	n = GetModuleFileNameW (0, b, MAX_PATH * 2);
	std::wstring p (b, n);
	size_t s = p.find_last_of (L"\\/");
	g_root = s == std::wstring::npos ? L"." : p.substr (0, s);
}
static bool exists (const std::wstring &p) { return GetFileAttributesW (p.c_str ()) != INVALID_FILE_ATTRIBUTES; }
static bool prefix_ci (const std::string &s, const char *p)
{
	size_t n = strlen (p);
	return s.size () >= n && _strnicmp (s.c_str (), p, n) == 0;
}
// an Onyx path -> a Windows one
static std::wstring winpath (const char *p)
{
	std::string s (p ? p : "");
	std::wstring out;
	if (s.size () >= 3 && s[0] == '/' && isalpha ((unsigned char) s[1]) && s[2] == ':') s.erase (0, 1);	// "/C:/..." (a file: URL's)
	size_t c = s.find (':');
	if (c == 1 && isalpha ((unsigned char) s[0])) out = wide (s.c_str ());			// C:/...
	else if (prefix_ci (s, "RAM:"))							// RAM:/... -> data\ram\...
	{
		std::string r = s.substr (4);
		while (!r.empty () && (r[0] == '/' || r[0] == '\\')) r.erase (0, 1);
		out = g_root + L"\\data\\ram" + (r.empty () ? L"" : L"\\" + wide (r.c_str ()));
	}
	else if (c != std::string::npos && c < 5 && s.find ('/') > c)			// SD:/..., SD1:/...
	{
		std::string r = s.substr (c + 1);
		while (!r.empty () && (r[0] == '/' || r[0] == '\\')) r.erase (0, 1);
		// (Jet's downloads, docs/06 §38: "SD:/Downloads" is the user's own Downloads folder,
		// %USERPROFILE%\Downloads, when it is there -- else Downloads\ beside Jet.exe)
		const wchar_t *home = _wgetenv (L"USERPROFILE");
		DWORD at = home ? GetFileAttributesW ((std::wstring (home) + L"\\Downloads").c_str ()) : INVALID_FILE_ATTRIBUTES;
		if (prefix_ci (r, "Downloads") && (r.size () == 9 || r[9] == '/' || r[9] == '\\') &&
		    at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY))
			out = std::wstring (home) + L"\\Downloads" + wide (r.substr (9).c_str ());
		else
			out = g_root + (r.empty () ? L"" : L"\\" + wide (r.c_str ()));
	}
	else if (!s.empty () && (s[0] == '/' || s[0] == '\\')) out = g_root + wide (s.c_str ());
	else if (s.empty ()) out = g_root;
	else out = g_root + L"\\" + wide (s.c_str ());
	for (auto &ch : out) if (ch == L'/') ch = L'\\';
	while (out.size () > 3 && out.back () == L'\\') out.pop_back ();
	return out;
}
static std::string slashes (std::string s) { for (auto &c : s) if (c == '\\') c = '/'; return s; }

// ---- the C library's file calls, on Onyx paths ---------------------------------------------------------------
// The link wraps them (jet.mk: -Wl,--wrap=fopen ...): every call of NetSurf, FreeType, PlutoVG and the Onyx glue
// to fopen, open, stat... comes here with an Onyx path, as on the Pi -- "/res/Messages" is res\Messages beside
// Jet.exe, "/data/Cookies" data\Cookies, "RAM:/jet/cache" data\ram\jet\cache, "C:/x" or "/C:/x" itself -- in
// UTF-8 (the wide calls of Windows), files always binary. And the precise clock for the timings (gettimeofday).
extern "C" {
FILE *__wrap_fopen (const char *path, const char *mode)
{
	if (!path || !mode) { errno = EINVAL; return 0; }
	std::string m (mode);
	if (m.find ('b') == std::string::npos && m.find ('t') == std::string::npos) m += 'b';
	return _wfopen (winpath (path).c_str (), wide (m.c_str ()).c_str ());
}
int __wrap_open (const char *path, int flags, ...)
{
	int mode = 0;
	if (flags & O_CREAT) { va_list a; va_start (a, flags); mode = va_arg (a, int); va_end (a); }
	return _wopen (winpath (path).c_str (), flags | O_BINARY | O_NOINHERIT, mode ? (mode & (_S_IREAD | _S_IWRITE)) : _S_IREAD | _S_IWRITE);
}
int __wrap_stat (const char *path, struct stat *st)
{
	struct _stat64 t;
	std::wstring w = winpath (path);
	if (_wstat64 (w.c_str (), &t) != 0) return -1;
	memset (st, 0, sizeof *st);
	st->st_mode = t.st_mode; st->st_size = (decltype (st->st_size)) t.st_size;
	st->st_mtime = (decltype (st->st_mtime)) t.st_mtime; st->st_atime = (decltype (st->st_atime)) t.st_atime;
	st->st_ctime = (decltype (st->st_ctime)) t.st_ctime; st->st_nlink = 1;
	return 0;
}
int __wrap_access (const char *path, int mode) { return _waccess (winpath (path).c_str (), mode & 6); }	// (X_OK: not a Windows notion)
int __wrap_mkdir (const char *path) { return _wmkdir (winpath (path).c_str ()); }
int __wrap_rmdir (const char *path) { return _wrmdir (winpath (path).c_str ()); }
int __wrap_unlink (const char *path) { return _wunlink (winpath (path).c_str ()); }
int __wrap_remove (const char *path)
{
	std::wstring w = winpath (path);
	DWORD a = GetFileAttributesW (w.c_str ());
	if (a == INVALID_FILE_ATTRIBUTES) { errno = ENOENT; return -1; }
	return (a & FILE_ATTRIBUTE_DIRECTORY) ? _wrmdir (w.c_str ()) : _wunlink (w.c_str ());
}
int __wrap_rename (const char *a, const char *b)
{
	return MoveFileExW (winpath (a).c_str (), winpath (b).c_str (), MOVEFILE_REPLACE_EXISTING) ? 0 : (errno = EACCES, -1);
}
struct jw_DIR { HANDLE h; WIN32_FIND_DATAW d; bool first, done; struct dirent e; };
DIR *__wrap_opendir (const char *path)
{
	std::wstring w = winpath (path);
	if (w.empty () || w.back () != L'\\') w += L'\\';
	jw_DIR *d = new jw_DIR;
	d->h = FindFirstFileW ((w + L"*").c_str (), &d->d);
	if (d->h == INVALID_HANDLE_VALUE) { delete d; errno = ENOENT; return 0; }
	d->first = true; d->done = false;
	return (DIR *) d;
}
struct dirent *__wrap_readdir (DIR *dd)
{
	jw_DIR *d = (jw_DIR *) dd;
	if (!d || d->done) return 0;
	if (!d->first && !FindNextFileW (d->h, &d->d)) { d->done = true; return 0; }
	d->first = false;
	memset (&d->e, 0, sizeof d->e);
	std::string n = utf8 (d->d.cFileName);
	snprintf (d->e.d_name, sizeof d->e.d_name, "%s", n.c_str ());
	d->e.d_namlen = (unsigned short) strlen (d->e.d_name);
	return &d->e;
}
int __wrap_closedir (DIR *dd) { jw_DIR *d = (jw_DIR *) dd; if (d) { FindClose (d->h); delete d; } return 0; }
int __wrap_gettimeofday (struct timeval *tv, void *)
{
	FILETIME ft; GetSystemTimePreciseAsFileTime (&ft);
	unsigned long long t = ((unsigned long long) ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10 - 11644473600000000ULL;
	tv->tv_sec = (long) (t / 1000000); tv->tv_usec = (long) (t % 1000000);
	return 0;
}
// POSIX regex: none (as on the Pi: newlib has none either -- NetSurf's few uses do without)
int regcomp (regex_t *preg, const char *, int) { if (preg) preg->re_nsub = 0; return 1; }
int regexec (const regex_t *, const char *, size_t, regmatch_t *, int) { return REG_NOMATCH; }
size_t regerror (int, const regex_t *, char *buf, size_t n) { if (buf && n) buf[0] = 0; return 0; }
void regfree (regex_t *) { }
// QuickJS's Date uses Windows' own time zone here (quickjs.c without __ONYX__); this is only what qjs_intl.h
// reads from SD:/etc/system.ini's timezone= on Onyx (the clock's offset), unused
int js_onyx_utc_offset_min;
// TLS 1.3's PSA random bytes (MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG, the Pi's configuration): user/tls/onyx_tls.hpp
// has them as a weak function, which a PE link does not resolve -- the same here, from Windows' RNG
int mbedtls_psa_external_get_random (void *, unsigned char *out, size_t size, size_t *olen)
{
	*olen = 0;
	if (size && BCryptGenRandom (0, out, (ULONG) size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return -148;	// PSA_ERROR_INSUFFICIENT_ENTROPY
	*olen = size;
	return 0;
}
// iconv: a passthrough, as on the Pi (user/netsurf/compat/onyx_compat.c)
iconv_t iconv_open (const char *, const char *) { return (iconv_t) 1; }
int iconv_close (iconv_t) { return 0; }
size_t iconv (iconv_t, char **inbuf, size_t *inleft, char **outbuf, size_t *outleft)
{
	if (inbuf == 0 || *inbuf == 0) return 0;			// (reset / flush)
	while (*inleft > 0 && *outleft > 0) { **outbuf = **inbuf; (*inbuf)++; (*outbuf)++; (*inleft)--; (*outleft)--; }
	if (*inleft > 0) { errno = E2BIG; return (size_t) -1; }
	return 0;
}
}

// ---- time -------------------------------------------------------------------------------------------------------
static LARGE_INTEGER g_qpf, g_qp0;
static unsigned long long now_us ()
{
	LARGE_INTEGER c; QueryPerformanceCounter (&c);
	return (unsigned long long) ((c.QuadPart - g_qp0.QuadPart) * 1000000.0 / (double) g_qpf.QuadPart);
}
static unsigned h_get_ticks (void) { return (unsigned) (now_us () / 10000); }
static void h_msleep (unsigned ms) { Sleep (ms); }
static void h_yield (void) { SwitchToThread (); }
static int h_get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *se)
{
	SYSTEMTIME t; GetLocalTime (&t);
	if (y) *y = t.wYear; if (mo) *mo = t.wMonth; if (d) *d = t.wDay;
	if (h) *h = t.wHour; if (mi) *mi = t.wMinute; if (se) *se = t.wSecond;
	return 1;
}
static int h_random (void *buf, unsigned len)
{
	return BCryptGenRandom (0, (PUCHAR) buf, len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? (int) len : 0;
}
static int set_timezone (int) { return 0; }

// ---- files (the kapi's) -------------------------------------------------------------------------------------------
static void *f_open (const char *p)
{
	std::wstring w = winpath (p);
	DWORD a = GetFileAttributesW (w.c_str ());
	if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) return 0;
	return _wfopen (w.c_str (), L"rb");
}
static int f_read (void *h, void *b, unsigned n) { return h ? (int) fread (b, 1, n, (FILE *) h) : -1; }
static unsigned long long f_fsize64 (void *h)
{
	if (!h) return 0;
	FILE *f = (FILE *) h; long long at = _ftelli64 (f);
	_fseeki64 (f, 0, SEEK_END); long long n = _ftelli64 (f); _fseeki64 (f, at, SEEK_SET);
	return n < 0 ? 0 : (unsigned long long) n;
}
static unsigned f_fsize (void *h) { unsigned long long n = f_fsize64 (h); return n > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned) n; }
static int f_seek (void *h, unsigned long long pos) { return h && _fseeki64 ((FILE *) h, (long long) pos, SEEK_SET) == 0 ? 0 : -1; }
static void f_close (void *h) { if (h) fclose ((FILE *) h); }
static int save_file (const char *p, const void *b, unsigned n)
{
	std::wstring w = winpath (p), tmp = w + L".~tmp";
	FILE *f = _wfopen (tmp.c_str (), L"wb");
	if (!f) return -1;
	size_t wr = n ? fwrite (b, 1, n, f) : 0;
	if (fclose (f) != 0 || wr != n) { DeleteFileW (tmp.c_str ()); return -1; }
	if (!MoveFileExW (tmp.c_str (), w.c_str (), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW (tmp.c_str ()); return -1; }
	return (int) n;			// (as the kernel's: the bytes written, or -1)
}
static int f_mkdir (const char *p) { return CreateDirectoryW (winpath (p).c_str (), 0) ? 0 : -1; }
static int f_remove (const char *p) { return __wrap_remove (p); }
static int f_rename (const char *a, const char *b) { return MoveFileExW (winpath (a).c_str (), winpath (b).c_str (), 0) ? 0 : -1; }
struct Dir { HANDLE h; WIN32_FIND_DATAW d; bool first; };
static void *f_opendir (const char *p)
{
	Dir *d = new Dir;
	std::wstring w = winpath (p);
	if (w.empty () || w.back () != L'\\') w += L'\\';
	d->h = FindFirstFileW ((w + L"*").c_str (), &d->d);
	if (d->h == INVALID_HANDLE_VALUE) { delete d; return 0; }
	d->first = true;
	return d;
}
static int f_readdir (void *h, struct kapi_dirent *e)
{
	Dir *d = (Dir *) h;
	if (!d) return -1;
	for (;;)
	{
		if (!d->first && !FindNextFileW (d->h, &d->d)) return 0;
		d->first = false;
		if (!wcscmp (d->d.cFileName, L".") || !wcscmp (d->d.cFileName, L"..")) continue;
		std::string n = utf8 (d->d.cFileName);
		bool dir = (d->d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		snprintf (e->name, sizeof e->name, "%s", n.c_str ());
		e->is_dir = dir ? 1 : 0;
		e->size = dir ? 0 : (d->d.nFileSizeHigh ? 0xFFFFFFFFu : d->d.nFileSizeLow);
		return 1;
	}
}
static void f_closedir (void *h) { Dir *d = (Dir *) h; if (d) { FindClose (d->h); delete d; } }
static int app_dir (char *b, unsigned n) { snprintf (b, n, "SD:/"); return 1; }
static int h_chdir (const char *) { return 1; }
static int h_getcwd (char *b, unsigned n) { snprintf (b, n, "SD:/"); return 1; }
// (v71) a volume's room: RAM: is a folder here (1 GB said, as a Pi's RAM: of a few hundred MB would
// leave the caches their default budgets); SD: the disk's
static int vol_info (const char *path, struct kapi_vol_info *o)
{
	if (!o) return -1;
	memset (o, 0, sizeof *o);
	std::string s (path ? path : "");
	ULARGE_INTEGER fr, tot, tf;
	if (prefix_ci (s, "RAM:"))
	{
		o->total = 1ull << 30; o->free = o->total; o->flags = KAPI_VOL_RAM;
		snprintf (o->type, sizeof o->type, "RAM");
		return 0;
	}
	if (!GetDiskFreeSpaceExW (g_root.c_str (), &fr, &tot, &tf)) return -1;
	o->total = tot.QuadPart; o->free = fr.QuadPart; o->used = tot.QuadPart - tf.QuadPart;
	snprintf (o->type, sizeof o->type, "NTFS");
	return 0;
}

// ---- console ------------------------------------------------------------------------------------------------------
static int h_write (int fd, const void *b, unsigned n)
{
	fwrite (b, 1, n, fd == 2 ? stderr : stdout);
	return (int) n;
}
static int stdout_write (const void *b, unsigned n) { fwrite (b, 1, n, stdout); return (int) n; }
static int stdin_read (void *, unsigned) { return 0; }

// ---- threads and their synchronisation ------------------------------------------------------------------------------
struct Thread { HANDLE h; DWORD id; int (*fn) (void *); void *arg; int code; };
enum { MAX_THREADS = 256 };
static Thread *g_threads[MAX_THREADS];			// tid = index + 2
static CRITICAL_SECTION g_thLock;
static HANDLE g_mainThread;
static DWORD g_mainId;
static DWORD WINAPI thread_tramp (LPVOID p) { Thread *t = (Thread *) p; t->code = t->fn (t->arg); return (DWORD) t->code; }
static int thread_create (int (*fn) (void *), void *arg, unsigned stack, const char *)
{
	Thread *t = new Thread; t->fn = fn; t->arg = arg; t->code = 0;
	EnterCriticalSection (&g_thLock);
	int slot = -1;
	for (int i = 0; i < MAX_THREADS; i++) if (!g_threads[i]) { slot = i; break; }
	if (slot < 0) { LeaveCriticalSection (&g_thLock); delete t; return -2; }
	g_threads[slot] = t;
	if (stack == 0) stack = 256 * 1024;
	t->h = CreateThread (0, stack < 65536 ? 65536 : stack, thread_tramp, t, STACK_SIZE_PARAM_IS_A_RESERVATION, &t->id);
	if (!t->h) { g_threads[slot] = 0; LeaveCriticalSection (&g_thLock); delete t; return -1; }
	LeaveCriticalSection (&g_thLock);
	return slot + 2;
}
static Thread *thread_of (int tid)
{
	if (tid < 2 || tid - 2 >= MAX_THREADS) return 0;
	EnterCriticalSection (&g_thLock);
	Thread *t = g_threads[tid - 2];
	LeaveCriticalSection (&g_thLock);
	return t;
}
static int thread_priority (int tid, int prio)
{
	HANDLE h = tid == 0 ? GetCurrentThread () : tid == 1 ? g_mainThread : 0;
	if (!h) { Thread *t = thread_of (tid); if (!t) return -2; h = t->h; }
	if (prio >= 0) SetThreadPriority (h, prio > 0 ? THREAD_PRIORITY_TIME_CRITICAL : THREAD_PRIORITY_NORMAL);
	return 0;
}
static int thread_self (void);
static int thread_join (int tid, unsigned timeout, int *code)
{
	if (tid == thread_self ()) return -3;
	Thread *t = thread_of (tid);
	if (!t) return -2;
	if (WaitForSingleObject (t->h, timeout == KAPI_WAIT_FOREVER ? INFINITE : timeout) != WAIT_OBJECT_0) return -1;
	if (code) *code = t->code;
	CloseHandle (t->h);
	EnterCriticalSection (&g_thLock); g_threads[tid - 2] = 0; LeaveCriticalSection (&g_thLock);
	delete t;
	return 0;
}
static void h_exit (int s);
static void thread_exit (int code) { if (GetCurrentThreadId () == g_mainId) h_exit (code); ExitThread ((DWORD) code); }
static int thread_self (void)
{
	DWORD me = GetCurrentThreadId ();
	if (me == g_mainId) return 1;
	EnterCriticalSection (&g_thLock);
	int r = 0;
	for (int i = 0; i < MAX_THREADS; i++) if (g_threads[i] && g_threads[i]->id == me) { r = i + 2; break; }
	LeaveCriticalSection (&g_thLock);
	return r;
}
// mutexes, events, barriers: a table of Win32 objects (handles 1 .. 256)
struct Sync { int kind; HANDLE h; CRITICAL_SECTION cs; CONDITION_VARIABLE cv; unsigned count, in, gen; };	// kind: 1 mutex, 2 event, 3 barrier
enum { MAX_SYNC = 256 };
static Sync *g_sync[MAX_SYNC + 1];
static CRITICAL_SECTION g_syncLock;
static int sync_add (Sync *s)
{
	EnterCriticalSection (&g_syncLock);
	for (int i = 1; i <= MAX_SYNC; i++) if (!g_sync[i]) { g_sync[i] = s; LeaveCriticalSection (&g_syncLock); return i; }
	LeaveCriticalSection (&g_syncLock);
	return -1;
}
static Sync *sync_get (int h, int kind)
{
	if (h < 1 || h > MAX_SYNC) return 0;
	EnterCriticalSection (&g_syncLock);
	Sync *s = g_sync[h];
	LeaveCriticalSection (&g_syncLock);
	return s && s->kind == kind ? s : 0;
}
static int mutex_create (void)
{
	Sync *s = new Sync (); s->kind = 1; s->h = CreateMutexW (0, FALSE, 0);
	int h = sync_add (s);
	if (h < 0) { CloseHandle (s->h); delete s; }
	return h;
}
static int mutex_lock (int h, unsigned timeout)
{
	Sync *s = sync_get (h, 1);
	if (!s) return -2;
	DWORD r = WaitForSingleObject (s->h, timeout == KAPI_WAIT_FOREVER ? INFINITE : timeout);
	return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED ? 0 : r == WAIT_TIMEOUT ? -1 : -2;
}
static int mutex_unlock (int h) { Sync *s = sync_get (h, 1); if (!s) return -2; return ReleaseMutex (s->h) ? 0 : -1; }
static int event_create (int manual, int initial)
{
	Sync *s = new Sync (); s->kind = 2; s->h = CreateEventW (0, manual ? TRUE : FALSE, initial ? TRUE : FALSE, 0);
	int h = sync_add (s);
	if (h < 0) { CloseHandle (s->h); delete s; }
	return h;
}
static int event_set (int h) { Sync *s = sync_get (h, 2); if (!s) return -2; SetEvent (s->h); return 0; }
static int event_reset (int h) { Sync *s = sync_get (h, 2); if (!s) return -2; ResetEvent (s->h); return 0; }
static int event_wait (int h, unsigned timeout)
{
	Sync *s = sync_get (h, 2);
	if (!s) return -2;
	DWORD r = WaitForSingleObject (s->h, timeout == KAPI_WAIT_FOREVER ? INFINITE : timeout);
	return r == WAIT_OBJECT_0 ? 0 : r == WAIT_TIMEOUT ? -1 : -2;
}
static int barrier_create (unsigned count)
{
	if (count < 1) return -1;
	Sync *s = new Sync (); s->kind = 3; s->count = count; s->in = 0; s->gen = 0;
	InitializeCriticalSection (&s->cs); InitializeConditionVariable (&s->cv);
	int h = sync_add (s);
	if (h < 0) delete s;
	return h;
}
static int barrier_wait (int h)
{
	Sync *s = sync_get (h, 3);
	if (!s) return -2;
	EnterCriticalSection (&s->cs);
	unsigned gen = s->gen;
	if (++s->in == s->count) { s->in = 0; s->gen++; WakeAllConditionVariable (&s->cv); LeaveCriticalSection (&s->cs); return 1; }
	while (gen == s->gen) SleepConditionVariableCS (&s->cv, &s->cs, INFINITE);
	LeaveCriticalSection (&s->cs);
	return 0;
}
static int sync_close (int h)
{
	if (h < 1 || h > MAX_SYNC) return -2;
	EnterCriticalSection (&g_syncLock);
	Sync *s = g_sync[h]; g_sync[h] = 0;
	LeaveCriticalSection (&g_syncLock);
	if (!s) return -2;
	if (s->kind != 3) CloseHandle (s->h);		// (a barrier's: kept -- a waiter may still be in it)
	else return 0;
	delete s;
	return 0;
}
// (no app cores on Windows)
static int core_acquire (void) { return -1; }
static int core_run (int, void (*) (void *), void *, void *) { return -1; }
static int core_state (int) { return KAPI_CORE_NOTYOURS; }
static void core_release (int) { }

static int wait_word (volatile unsigned *addr, unsigned expected, unsigned timeout)
{
	if (!addr || ((unsigned long long) addr & 3)) return -1;
	if (*addr != expected) return 0;
	if (timeout == 0) return 1;
	ULONGLONG t0 = GetTickCount64 ();
	for (;;)
	{
		unsigned e = expected;
		WaitOnAddress ((volatile VOID *) addr, &e, 4, 10);	// (a wake: at once; a plain store: within 10 ms, as
		if (*addr != expected) return 0;			//  the Onyx kernel's tick re-reads sleeping words)
		if (timeout != KAPI_WAIT_FOREVER && GetTickCount64 () - t0 >= timeout) return 1;
	}
}
static int wake_word (volatile unsigned *addr) { if (!addr) return -1; WakeByAddressAll ((PVOID) addr); return 1; }
// executable memory (the JIT's: AArch64 only, so never asked for on a PC -- given anyway)
static void *code_alloc (unsigned long size)
{
	return VirtualAlloc (0, ((size_t) size + 0xFFFF) & ~(size_t) 0xFFFF, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
}

// ---- the window ---------------------------------------------------------------------------------------------------
static HWND g_hwnd;
static unsigned *g_canvas; static int g_cw, g_ch, g_stride;
static CRITICAL_SECTION g_canvasLock;		// (WM_PAINT may come while the canvas is replaced)
static char g_title[64] = "Jet Browser";
static gui_handler g_ptr, g_key, g_click, g_menuFn;
static volatile bool g_quit;
static int g_btn;				// the buttons held (1 left, 2 right, 4 middle)
static bool g_tracking;
static bool g_altUsed;				// Alt held with another key since it went down
static HMENU g_menu;
static std::string g_menuSpec;
static std::string g_drop;			// the files dropped last (kapi_drag_data)
struct Ev { int to; int ev; long long v; };	// to: 0 pointer, 1 key, 2 menu, 3 click
static std::vector<Ev> g_evq;
static int g_clientW, g_clientH;		// the window's client area now
static bool g_sizeDirty;			// the client area changed: the app told (pump)
static int g_maxSent;				// KAPI_FRAME_MAXIMISE events sent so far
static unsigned g_bgFill = 0xFFFFFF;

static long long ptrval (int x, int y, int buttons, int changed, int wheel)
{
	if (x < 0) x = 0; if (y < 0) y = 0; if (x > 0xFFFF) x = 0xFFFF; if (y > 0xFFFF) y = 0xFFFF;
	return ((long long) (wheel & 0xFF) << 48) | ((long long) (changed & 0xFF) << 40) | ((long long) (buttons & 0xFF) << 32)
		| ((long long) x << 16) | (long long) y;
}
static void blit (HDC dc)
{
	EnterCriticalSection (&g_canvasLock);
	if (g_canvas)
	{
		BITMAPINFO bi; memset (&bi, 0, sizeof bi);
		bi.bmiHeader.biSize = sizeof bi.bmiHeader;
		bi.bmiHeader.biWidth = g_stride; bi.bmiHeader.biHeight = -g_ch;
		bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
		SetDIBitsToDevice (dc, 0, 0, g_cw, g_ch, 0, 0, 0, g_ch, g_canvas, &bi, DIB_RGB_COLORS);
	}
	HBRUSH br = CreateSolidBrush (RGB ((g_bgFill >> 16) & 255, (g_bgFill >> 8) & 255, g_bgFill & 255));
	RECT r;
	if (g_clientW > g_cw) { r.left = g_cw; r.top = 0; r.right = g_clientW; r.bottom = g_clientH; FillRect (dc, &r, br); }
	if (g_clientH > g_ch) { r.left = 0; r.top = g_ch; r.right = g_cw < g_clientW ? g_cw : g_clientW; r.bottom = g_clientH; FillRect (dc, &r, br); }
	DeleteObject (br);
	LeaveCriticalSection (&g_canvasLock);
}
static long key_code (WPARAM vk)
{
	switch (vk)
	{
	case VK_UP: return KEY_UP; case VK_DOWN: return KEY_DOWN; case VK_LEFT: return KEY_LEFT; case VK_RIGHT: return KEY_RIGHT;
	case VK_HOME: return KEY_HOME; case VK_END: return KEY_END; case VK_PRIOR: return KEY_PGUP; case VK_NEXT: return KEY_PGDN;
	case VK_DELETE: return KEY_DEL;
	}
	if (vk >= VK_F1 && vk <= VK_F12) return KEY_F1 + (long) (vk - VK_F1);
	return 0;
}
static void push (int to, int ev, long long v) { g_evq.push_back (Ev { to, ev, v }); }
static void mouse (int ev, LPARAM lp, int changed, int wheel)
{
	int x = (short) LOWORD (lp), y = (short) HIWORD (lp);
	push (0, ev, ptrval (x, y, g_btn, changed, wheel));
	if (ev == GUI_EVENT_PTR_DOWN)		// (the legacy click handler too)
		push (3, GUI_EVENT_CANVAS_CLICK, ((long long) g_btn << 32) | ((long long) (x & 0xFFFF) << 16) | (y & 0xFFFF));
}
static LRESULT CALLBACK wndproc (HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m)
	{
	case WM_CLOSE: g_quit = true; PostMessageW (h, WM_NULL, 0, 0); return 0;
	case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint (h, &ps); blit (dc); EndPaint (h, &ps); return 0; }
	case WM_ERASEBKGND: return 1;
	case WM_SIZE:
		if (w != SIZE_MINIMIZED)
		{
			int cw = LOWORD (l), ch = HIWORD (l);
			if (cw > 0 && ch > 0 && (cw != g_clientW || ch != g_clientH)) { g_clientW = cw; g_clientH = ch; g_sizeDirty = true; }
		}
		return 0;
	case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *) l; mm->ptMinTrackSize.x = 480; mm->ptMinTrackSize.y = 360; return 0; }
	case WM_MOUSEMOVE:
		if (!g_tracking) { TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent (&t); g_tracking = true; push (0, GUI_EVENT_PTR_ENTER, ptrval ((short) LOWORD (l), (short) HIWORD (l), g_btn, 0, 0)); }
		mouse (GUI_EVENT_PTR_MOVE, l, 0, 0);
		if (g_btn) push (3, GUI_EVENT_CANVAS_MOTION, ((long long) g_btn << 32) | ((long long) (LOWORD (l)) << 16) | HIWORD (l));
		return 0;
	case WM_MOUSELEAVE: g_tracking = false; if (!g_btn) push (0, GUI_EVENT_PTR_LEAVE, 0); return 0;
	case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
	case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK:
	{
		int b = (m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK) ? 1 : (m == WM_RBUTTONDOWN || m == WM_RBUTTONDBLCLK) ? 2 : 4;
		g_btn |= b; SetCapture (h); SetFocus (h);
		mouse (GUI_EVENT_PTR_DOWN, l, b, 0);
		return 0;
	}
	case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
	{
		int b = m == WM_LBUTTONUP ? 1 : m == WM_RBUTTONUP ? 2 : 4;
		g_btn &= ~b; if (!g_btn) ReleaseCapture ();
		mouse (GUI_EVENT_PTR_UP, l, b, 0);
		return 0;
	}
	case WM_CAPTURECHANGED:
		if ((HWND) l != h && g_btn)		// (lost: the buttons let go)
		{
			POINT p; GetCursorPos (&p); ScreenToClient (h, &p);
			for (int b = 1; b <= 4; b <<= 1) if (g_btn & b) { g_btn &= ~b; push (0, GUI_EVENT_PTR_UP, ptrval (p.x, p.y, g_btn, b, 0)); }
		}
		return 0;
	case WM_MOUSEWHEEL:
	{
		static int acc;
		acc += GET_WHEEL_DELTA_WPARAM (w);
		int notches = acc / WHEEL_DELTA; acc -= notches * WHEEL_DELTA;
		if (!notches) return 0;
		POINT p = { (short) LOWORD (l), (short) HIWORD (l) }; ScreenToClient (h, &p);
		push (0, GUI_EVENT_PTR_WHEEL, ptrval (p.x, p.y, g_btn, 0, notches));
		return 0;
	}
	case WM_KEYDOWN: case WM_SYSKEYDOWN:
	{
		if (w == VK_F4 && (GetKeyState (VK_MENU) & 0x8000)) break;	// (Alt+F4: Windows closes the window)
		if (w == VK_MENU && !(l & (1 << 30))) g_altUsed = false;		// (Alt down, afresh)
		else if (w != VK_MENU && (GetKeyState (VK_MENU) & 0x8000)) g_altUsed = true;
		long k = key_code (w);
		if (k) { push (1, GUI_EVENT_KEY, k); return 0; }
		// Ctrl with + = - 0 (the zoom, docs/06 §38): Windows gives no character (or a control one)
		// -- the key's own, as the Pi's kernel sends it, Ctrl read by get_modifiers
		if ((GetKeyState (VK_CONTROL) & 0x8000) && !(GetKeyState (VK_MENU) & 0x8000))
		{
			long z = w == VK_OEM_PLUS || w == VK_ADD ? '+' : w == VK_OEM_MINUS || w == VK_SUBTRACT ? '-' :
				 w == '0' || w == VK_NUMPAD0 ? '0' : 0;
			if (z) { push (1, GUI_EVENT_KEY, z); return 0; }
		}
		break;
	}
	case WM_SYSCOMMAND:
		// Alt let go after Alt+Left (Back), Alt+Right...: not the menu bar's keyboard mode -- a modal loop of
		// Windows' that would hold the browser until Esc. Alt alone (or F10) still opens it.
		if ((w & 0xFFF0) == SC_KEYMENU && l == 0 && g_altUsed) { g_altUsed = false; return 0; }
		if ((w & 0xFFF0) == SC_KEYMENU) g_altUsed = false;
		break;
	case WM_SYSCHAR:
		return 0;				// (Alt+letter: no menu beep)
	case WM_CHAR:
		// The keys as Onyx's (kapi.h): ASCII, Latin-1 (0xA0..0xFF: é, ç, à...), the euro as 0x80 -- the
		// codes above are the arrows and F-keys (0x100..), so no other characters
		if (w == 0x20AC) w = 0x80;
		else if ((w >= 0x80 && w < 0xA0) || w > 0xFF) return 0;
		if ((GetKeyState (VK_CONTROL) & 0x8000) && !(GetKeyState (VK_MENU) & 0x8000) &&
		    (w == 0x1F || w == '+' || w == '-' || w == '0' || w == '='))
			return 0;	// (Ctrl+- and the like: sent from WM_KEYDOWN, above)
		push (1, GUI_EVENT_KEY, (long long) w);
		return 0;
	case WM_COMMAND:
		if (HIWORD (w) == 0 && LOWORD (w) >= 1000) { push (2, GUI_EVENT_MENU, (long long) LOWORD (w) - 1000); return 0; }
		break;
	case WM_DROPFILES:
	{
		HDROP d = (HDROP) w;
		UINT n = DragQueryFileW (d, 0xFFFFFFFF, 0, 0);
		std::string all;
		for (UINT i = 0; i < n; i++)
		{
			wchar_t f[MAX_PATH * 2];
			if (DragQueryFileW (d, i, f, MAX_PATH * 2)) { if (!all.empty ()) all += '\n'; all += "file:///" + slashes (utf8 (f)); }
		}
		POINT p; DragQueryPoint (d, &p); DragFinish (d);
		g_drop = all;
		push (0, GUI_EVENT_DROP, ((long long) DND_F_COPY << 32) | ((long long) (p.x & 0xFFFF) << 16) | (p.y & 0xFFFF));
		return 0;
	}
	}
	return DefWindowProcW (h, m, w, l);
}
static void apply_menu ();
static unsigned *make_canvas (int w, int h)
{
	unsigned *px = (unsigned *) malloc ((size_t) w * h * 4);
	if (!px) return 0;
	for (long i = 0; i < (long) w * h; i++) px[i] = g_bgFill;
	return px;
}
static unsigned *create_ex (int x, int y, int w, int h, const char *title, unsigned flags)
{
	(void) x; (void) y; (void) flags;
	if (g_hwnd) return 0;				// (one window a process)
	snprintf (g_title, sizeof g_title, "%s", title ? title : "Jet Browser");
	if (!strcmp (g_title, "Jet")) snprintf (g_title, sizeof g_title, "Jet Browser");
	WNDCLASSEXW wc; memset (&wc, 0, sizeof wc);
	wc.cbSize = sizeof wc; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW (0);
	wc.hCursor = LoadCursor (0, IDC_ARROW); wc.lpszClassName = L"OnyxJetWindow";
	wc.hIcon = LoadIconW (wc.hInstance, MAKEINTRESOURCEW (1));
	if (!wc.hIcon) wc.hIcon = LoadIcon (0, IDI_APPLICATION);
	wc.style = CS_DBLCLKS;
	RegisterClassExW (&wc);
	RECT r = { 0, 0, w, h };
	AdjustWindowRect (&r, WS_OVERLAPPEDWINDOW, TRUE);
	unsigned *px = make_canvas (w, h);
	if (!px) return 0;
	EnterCriticalSection (&g_canvasLock); g_canvas = px; g_cw = w; g_ch = h; g_stride = w; LeaveCriticalSection (&g_canvasLock);
	g_hwnd = CreateWindowExW (WS_EX_ACCEPTFILES, L"OnyxJetWindow", wide (g_title).c_str (), WS_OVERLAPPEDWINDOW,
				  CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);
	if (!g_hwnd) return 0;
	DragAcceptFiles (g_hwnd, TRUE);
	apply_menu ();
	ShowWindow (g_hwnd, getenv ("JET_MAXIMISED") ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
	RECT c; GetClientRect (g_hwnd, &c); g_clientW = c.right; g_clientH = c.bottom;
	g_sizeDirty = g_clientW != g_cw || g_clientH != g_ch;	// (the menu bar took some: fitted at the first pump)
	UpdateWindow (g_hwnd);
	return g_canvas;
}
static unsigned *create (int w, int h, const char *t) { return create_ex (0, 0, w, h, t, 0); }
static unsigned *resize2 (int w, int h, int *stride)
{
	if (w < 1 || h < 1) return 0;
	unsigned *px = make_canvas (w, h);
	if (!px) return 0;
	EnterCriticalSection (&g_canvasLock);
	unsigned *old = g_canvas;
	g_canvas = px; g_cw = w; g_ch = h; g_stride = w;
	LeaveCriticalSection (&g_canvasLock);
	free (old);
	if (stride) *stride = w;
	return px;
}
static unsigned *resize (int w, int h) { return resize2 (w, h, 0); }
static void move_window (int, int) { }
static void h_present (void)
{
	if (!g_hwnd) return;
	HDC dc = GetDC (g_hwnd); blit (dc); ReleaseDC (g_hwnd, dc);
}
static void screen_size (int *w, int *h)
{
	RECT r; SystemParametersInfoW (SPI_GETWORKAREA, 0, &r, 0);
	if (w) *w = r.right - r.left;
	if (h) *h = r.bottom - r.top;
}
static int screen_native (int *w, int *h) { if (w) *w = GetSystemMetrics (SM_CXSCREEN); if (h) *h = GetSystemMetrics (SM_CYSCREEN); return 1; }
static int win_geometry (struct kapi_win_geom *g)
{
	if (!g_hwnd) return -1;
	RECT c; GetClientRect (g_hwnd, &c);
	int cw = c.right > 0 ? c.right : g_cw, ch = c.bottom > 0 ? c.bottom : g_ch;
	g->x = 0; g->y = 0; g->w = g_cw; g->h = g_ch; g->cw = g_cw; g->ch = g_ch;	// (no frame of Onyx's)
	g->ax = 0; g->ay = 0; g->aw = cw; g->ah = ch;			// the work area: the client area
	g->state = (IsIconic (g_hwnd) ? KAPI_WIN_MINIMISED : 0) | (GetForegroundWindow () == g_hwnd ? KAPI_WIN_KEYS : 0);
	return 0;
}
static int win_minimise (unsigned) { if (g_hwnd) ShowWindow (g_hwnd, SW_MINIMIZE); return 0; }
static int win_list (struct kapi_win_info *, int) { return 0; }
static int desk (int, int) { return 1 << 8 | 0; }
static int win_desk (unsigned, int) { return 0; }
static int get_chrome (struct kapi_chrome *o)
{
	if (!g_hwnd) return 0;
	memset (o, 0, sizeof *o);
	o->content = g_canvas; o->content_w = g_cw; o->content_h = g_ch;	// active = 0: no Onyx frame drawn
	snprintf (o->title, sizeof o->title, "%s", g_title);
	return 1;
}
static void set_ptr (gui_handler f) { g_ptr = f; }
static void set_key (gui_handler f) { g_key = f; }
static void set_click (gui_handler f) { g_click = f; }
static unsigned get_mods (void)
{
	unsigned m = 0;
	if (GetKeyState (VK_CONTROL) & 0x8000) m |= 1;
	if (GetKeyState (VK_SHIFT) & 0x8000) m |= 2;
	if (GetKeyState (VK_MENU) & 0x8000) m |= 4;
	return m;
}
static void cursor_pos (int *x, int *y)
{
	POINT p; GetCursorPos (&p);
	if (g_hwnd) ScreenToClient (g_hwnd, &p);
	if (x) *x = p.x; if (y) *y = p.y;
}
static int font_w (void) { return 8; }
static int font_h (void) { return 16; }
static void draw_text_buf (unsigned *, int, int, int, int, const char *, unsigned) { }
static void draw_text (int, int, const char *, unsigned) { }
struct Post { void (*fn) (void *, long); void *ctx; long v; };
static std::vector<Post> g_posts;
static CRITICAL_SECTION g_postLock;
static void pump (void)
{
	MSG msg;
	while (PeekMessageW (&msg, 0, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT) { g_quit = true; continue; }
		TranslateMessage (&msg);
		DispatchMessageW (&msg);
	}
	if (g_sizeDirty && g_ptr)		// the window's new size: as the frame's maximise button (wtk's
	{					// Root fills the work area, i.e. the client area); once maximised,
		g_sizeDirty = false;		// twice: back, then maximised to the new size
		if (g_clientW != g_cw || g_clientH != g_ch)
		{
			if (g_maxSent++ > 0) push (0, GUI_EVENT_WINCTL, KAPI_FRAME_MAXIMISE), g_maxSent++;
			push (0, GUI_EVENT_WINCTL, KAPI_FRAME_MAXIMISE);
		}
	}
	std::vector<Ev> q; q.swap (g_evq);
	for (size_t i = 0; i < q.size (); i++)
	{
		const Ev &e = q[i];
		if (e.to == 0 && g_ptr) g_ptr (0, e.ev, (gui_value) e.v);
		else if (e.to == 1 && g_key) g_key (0, e.ev, (gui_value) e.v);
		else if (e.to == 2 && g_menuFn) g_menuFn (0, e.ev, (gui_value) e.v);
		else if (e.to == 3 && g_click) g_click (0, e.ev, (gui_value) e.v);
	}
	std::vector<Post> p;
	EnterCriticalSection (&g_postLock); p.swap (g_posts); LeaveCriticalSection (&g_postLock);
	for (size_t i = 0; i < p.size (); i++) p[i].fn (p[i].ctx, p[i].v);
}
static int post (void (*fn) (void *, long), void *ctx, long v)
{
	EnterCriticalSection (&g_postLock);
	if (g_posts.size () >= 4096) { LeaveCriticalSection (&g_postLock); return -1; }
	g_posts.push_back (Post { fn, ctx, v });
	LeaveCriticalSection (&g_postLock);
	if (g_hwnd) PostMessageW (g_hwnd, WM_NULL, 0, 0);
	return 0;
}
static int pump_wait (unsigned timeout)
{
	bool posted;
	EnterCriticalSection (&g_postLock); posted = !g_posts.empty (); LeaveCriticalSection (&g_postLock);
	if (g_evq.empty () && !posted && !g_sizeDirty && !g_quit)
		MsgWaitForMultipleObjectsEx (0, 0, timeout == KAPI_WAIT_FOREVER ? INFINITE : timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
	size_t before = g_evq.size ();
	pump ();
	return (int) before;
}
static int h_should_exit (void) { return g_quit ? 1 : 0; }
static void h_exit (int s)
{
	fflush (stdout); fflush (stderr);
	ExitProcess ((UINT) s);
}

// the menu bar: "M<title>" / "I<id>\t<label>\t<shortcut>" / "-", a line each
static void apply_menu ()
{
	if (!g_hwnd || g_menuSpec.empty ()) return;
	HMENU bar = CreateMenu (), cur = 0;
	size_t i = 0;
	while (i < g_menuSpec.size ())
	{
		size_t j = g_menuSpec.find ('\n', i); if (j == std::string::npos) j = g_menuSpec.size ();
		std::string l = g_menuSpec.substr (i, j - i); i = j + 1;
		if (l.empty ()) continue;
		if (l[0] == 'M') { cur = CreatePopupMenu (); AppendMenuW (bar, MF_POPUP | MF_STRING, (UINT_PTR) cur, wide (l.c_str () + 1).c_str ()); }
		else if (l[0] == '-' && cur) AppendMenuW (cur, MF_SEPARATOR, 0, 0);
		else if (l[0] == 'I' && cur)
		{
			size_t t1 = l.find ('\t'); if (t1 == std::string::npos) continue;
			size_t t2 = l.find ('\t', t1 + 1);
			int id = atoi (l.c_str () + 1);
			std::string label = l.substr (t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
			std::string sc = t2 == std::string::npos ? "" : l.substr (t2 + 1);
			if (sc.size () == 2 && sc[0] == '^') sc = std::string ("Ctrl+") + sc[1];
			std::string s; for (char c : label) { if (c == '&') s += '&'; s += c; }
			if (!sc.empty ()) s += "\t" + sc;
			AppendMenuW (cur, MF_STRING, 1000 + id, wide (s.c_str ()).c_str ());
		}
	}
	HMENU old = g_menu; g_menu = bar;
	SetMenu (g_hwnd, bar);
	if (old) DestroyMenu (old);
	DrawMenuBar (g_hwnd);
}
static int set_menu (const char *spec, gui_handler fn)
{
	g_menuSpec = spec ? spec : ""; g_menuFn = fn;
	apply_menu ();
	if (g_hwnd) { RECT c; GetClientRect (g_hwnd, &c); if (c.right != g_clientW || c.bottom != g_clientH) { g_clientW = c.right; g_clientH = c.bottom; g_sizeDirty = true; } }
	return 1;
}
static unsigned get_menu (char *, unsigned, char *, unsigned) { return 0; }
static int menu_command (int id)
{
	if (id == MENU_QUIT) { g_quit = true; return 1; }
	push (2, GUI_EVENT_MENU, id);
	return 1;
}
static int drag_data (int *type, void *buf, unsigned cap)
{
	if (type) *type = DND_FILES;
	unsigned n = (unsigned) g_drop.size ();
	memcpy (buf, g_drop.data (), n < cap ? n : cap);
	return (int) n;
}
static int drag_begin (int, const void *, unsigned, const char *) { return 0; }

// ---- the clipboard ---------------------------------------------------------------------------------------------
static int clipboard_set (int type, const void *data, unsigned len)
{
	(void) type;
	std::wstring w = wide ((const char *) data, (int) len);
	std::wstring crlf; for (wchar_t c : w) { if (c == L'\n') crlf += L'\r'; crlf += c; }
	if (!OpenClipboard (g_hwnd)) return 0;
	EmptyClipboard ();
	HGLOBAL g = GlobalAlloc (GMEM_MOVEABLE, (crlf.size () + 1) * sizeof (wchar_t));
	if (g) { memcpy (GlobalLock (g), crlf.c_str (), (crlf.size () + 1) * sizeof (wchar_t)); GlobalUnlock (g); SetClipboardData (CF_UNICODETEXT, g); }
	CloseClipboard ();
	return g ? 1 : 0;
}
static int clipboard_get (int *type, void *buf, unsigned cap, unsigned *serial)
{
	if (serial) *serial = GetClipboardSequenceNumber ();
	if (type) *type = 0;
	if (!OpenClipboard (g_hwnd)) return 0;
	std::string u;
	HANDLE h = GetClipboardData (CF_UNICODETEXT);
	if (h) { const wchar_t *w = (const wchar_t *) GlobalLock (h); if (w) u = utf8 (w); GlobalUnlock (h); }
	CloseClipboard ();
	std::string o; for (char c : u) if (c != '\r') o += c;
	if (o.empty ()) return 0;
	if (type) *type = 1;
	if (buf) memcpy (buf, o.data (), o.size () < cap ? o.size () : cap);
	return (int) o.size ();
}

// Jet Browser's Copy Image (docs/06 §40, frontends/framebuffer/onyx_edit.c): the picture -- w x h pixels
// 0xAARRGGBB -- on the Windows clipboard as a DIB (CF_DIB: 32-bit, bottom-up; Paint, Word, mail take it).
// 1 done.
extern "C" int onyx_win_clip_image (const unsigned *px, int w, int h)
{
	if (!px || w <= 0 || h <= 0) return 0;
	SIZE_T bytes = sizeof (BITMAPINFOHEADER) + (SIZE_T) w * h * 4;
	HGLOBAL g = GlobalAlloc (GMEM_MOVEABLE, bytes);
	if (!g) return 0;
	unsigned char *p = (unsigned char *) GlobalLock (g);
	BITMAPINFOHEADER bi = {};
	bi.biSize = sizeof bi; bi.biWidth = w; bi.biHeight = h;	// (positive: bottom-up)
	bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB;
	bi.biSizeImage = (DWORD) ((SIZE_T) w * h * 4);
	memcpy (p, &bi, sizeof bi);
	unsigned *d = (unsigned *) (p + sizeof bi);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			unsigned c = px[(size_t) (h - 1 - y) * w + x], a = c >> 24;
			if (a != 255)		// (laid on white: CF_DIB's readers ignore the alpha)
			{
				unsigned r = (((c >> 16) & 255) * a + 255 * (255 - a)) / 255;
				unsigned gg = (((c >> 8) & 255) * a + 255 * (255 - a)) / 255;
				unsigned b = ((c & 255) * a + 255 * (255 - a)) / 255;
				c = r << 16 | gg << 8 | b;
			}
			d[(size_t) y * w + x] = c | 0xFF000000u;	// (BGRA in memory: 0xAARRGGBB as a word)
		}
	GlobalUnlock (g);
	if (!OpenClipboard (g_hwnd)) { GlobalFree (g); return 0; }
	EmptyClipboard ();
	bool ok = SetClipboardData (CF_DIB, g) != 0;
	CloseClipboard ();
	if (!ok) GlobalFree (g);
	return ok ? 1 : 0;
}

// ---- the network (Winsock) ----------------------------------------------------------------------------------------------------
enum { MAX_SOCKS = 1024 };
static SOCKET g_socks[MAX_SOCKS];
static CRITICAL_SECTION g_sockLock;
static bool net_up ()
{
	static volatile LONG st;
	if (!st) { WSADATA d; InterlockedExchange (&st, WSAStartup (MAKEWORD (2, 2), &d) == 0 ? 1 : -1); }
	return st > 0;
}
static bool g_netdebug;
static int net_status (char *ip, unsigned cap) { if (ip && cap) snprintf (ip, cap, "%s", "this PC"); return net_up () ? 1 : 0; }
static int net_info (char *b, unsigned cap) { if (b && cap) b[0] = 0; return 0; }
static int net_resolve (const char *host, char *ip, unsigned cap)
{
	if (!net_up () || !host) return 0;
	addrinfo hints; memset (&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
	addrinfo *res = 0;
	if (getaddrinfo (host, 0, &hints, &res) != 0 || !res) return 0;
	char b[64] = "";
	inet_ntop (AF_INET, &((sockaddr_in *) res->ai_addr)->sin_addr, b, sizeof b);
	freeaddrinfo (res);
	if (ip && cap) snprintf (ip, cap, "%s", b);
	return b[0] ? 1 : 0;
}
static int tcp_connect (const char *host, unsigned port)
{
	if (!net_up ()) return -1;
	ULONGLONG t0 = GetTickCount64 ();
	addrinfo hints; memset (&hints, 0, sizeof hints); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
	addrinfo *res = 0;
	if (getaddrinfo (host, std::to_string (port).c_str (), &hints, &res) != 0) { if (g_netdebug) fprintf (stderr, "net: %s: no such host\n", host); return -2; }
	SOCKET s = INVALID_SOCKET;
	for (int pass = 0; pass < 2 && s == INVALID_SOCKET; pass++)		// (IPv4 first: as the Pi, which has no IPv6)
		for (addrinfo *a = res; a && s == INVALID_SOCKET; a = a->ai_next)
		{
			if ((pass == 0) != (a->ai_family == AF_INET)) continue;
			s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
			if (s == INVALID_SOCKET) continue;
			if (connect (s, a->ai_addr, (int) a->ai_addrlen) != 0) { closesocket (s); s = INVALID_SOCKET; }
		}
	freeaddrinfo (res);
	if (s == INVALID_SOCKET) { if (g_netdebug) fprintf (stderr, "net: %s:%u: connect failed\n", host, port); return -3; }
	u_long nb = 1; ioctlsocket (s, FIONBIO, &nb);
	BOOL nd = TRUE; setsockopt (s, IPPROTO_TCP, TCP_NODELAY, (const char *) &nd, sizeof nd);
	EnterCriticalSection (&g_sockLock);
	for (int i = 1; i < MAX_SOCKS; i++)
		if (g_socks[i] == 0 || g_socks[i] == INVALID_SOCKET)
		{
			g_socks[i] = s;
			LeaveCriticalSection (&g_sockLock);
			if (g_netdebug) fprintf (stderr, "net: %s:%u connected (#%d, %u ms)\n", host, port, i, (unsigned) (GetTickCount64 () - t0));
			return i;
		}
	LeaveCriticalSection (&g_sockLock);
	closesocket (s);
	return -1;
}
static SOCKET sock (int h)
{
	if (h <= 0 || h >= MAX_SOCKS) return INVALID_SOCKET;
	EnterCriticalSection (&g_sockLock);
	SOCKET s = g_socks[h];
	LeaveCriticalSection (&g_sockLock);
	return s ? s : INVALID_SOCKET;
}
static int tcp_send (int h, const void *b, unsigned n)
{
	SOCKET s = sock (h);
	if (s == INVALID_SOCKET) return -1;
	unsigned done = 0;
	while (done < n)
	{
		int w = send (s, (const char *) b + done, (int) (n - done), 0);
		if (w > 0) { done += (unsigned) w; continue; }
		if (WSAGetLastError () == WSAEWOULDBLOCK)
		{
			fd_set f; FD_ZERO (&f); FD_SET (s, &f); timeval tv = { 5, 0 };
			if (select (0, 0, &f, 0, &tv) <= 0) return done ? (int) done : -1;	// (5 s with no room: as the kernel's)
			continue;
		}
		return done ? (int) done : -1;
	}
	return (int) done;
}
static int tcp_recv (int h, void *b, unsigned n)
{
	SOCKET s = sock (h);
	if (s == INVALID_SOCKET) return -1;
	int r = recv (s, (char *) b, (int) n, 0);
	if (r > 0) return r;
	if (r == 0) return -1;				// (closed)
	return WSAGetLastError () == WSAEWOULDBLOCK ? 0 : -1;
}
static void tcp_close (int h)
{
	if (h <= 0 || h >= MAX_SOCKS) return;
	EnterCriticalSection (&g_sockLock);
	SOCKET s = g_socks[h];
	g_socks[h] = 0;
	LeaveCriticalSection (&g_sockLock);
	if (s && s != INVALID_SOCKET) closesocket (s);
}

// ---- the rest ------------------------------------------------------------------------------------------------------------------
static int gpu_info (char *b, unsigned cap) { if (b && cap) snprintf (b, cap, "no GPU (Windows: the CPU path)"); return 0; }
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
	MEMORYSTATUSEX m; m.dwLength = sizeof m; GlobalMemoryStatusEx (&m);
	if (t) *t = (unsigned long) (m.ullTotalPhys / 1024); if (f) *f = (unsigned long) (m.ullAvailPhys / 1024);
	if (a) *a = 0; if (pg) *pg = 4;
	return 1;
}
static void *h_sbrk (long) { return (void *) -1; }
static void *k_memset (void *d, int c, unsigned long n) { return memset (d, c, n); }
static void *k_memcpy (void *d, const void *s, unsigned long n) { return memcpy (d, s, n); }
static void *k_memmove (void *d, const void *s, unsigned long n) { return memmove (d, s, n); }
static int ipc_lookup (const char *) { return 0; }
static int ipc_register (const char *) { return 1; }
static int mailbox_send (int, int, const void *, unsigned) { return 0; }
static int mailbox_recv (int *, int *, void *, unsigned, int) { return -1; }
static int exec_as (const char *, const char *, const char *) { return 0; }
static int h_exec (const char *, const char *) { return 0; }
static int list_procs (char *b, unsigned n) { if (n) b[0] = 0; return 0; }
// streams: only kapi_file_out (a file written in pieces) is used
struct Stream { FILE *f; };
static void *file_out (const char *p, int append)
{
	FILE *f = _wfopen (winpath (p).c_str (), append ? L"ab" : L"wb");
	if (!f) return 0;
	Stream *s = new Stream; s->f = f;
	return s;
}
static int stream_write (void *h, const void *b, unsigned n) { Stream *s = (Stream *) h; return s && s->f ? (int) fwrite (b, 1, n, s->f) : -1; }
static int stream_read (void *, void *, unsigned) { return 0; }
static int stream_read_nb (void *, void *, unsigned) { return 0; }
static void stream_eof (void *) { }
static void stream_close (void *h) { Stream *s = (Stream *) h; if (s) { if (s->f) fclose (s->f); delete s; } }
static int surface_create (int, int) { return -1; }
static unsigned *surface_map (int) { return 0; }
static int surface_size (int, int *, int *) { return 0; }

// ---- sound (the PCM stream: <video>, <audio>) on waveOut: 44100 Hz s16 stereo -----------------------
// sound_write queues at most 0.5 s (as the Pi's), each write a buffer of its own given to waveOut,
// the played ones freed at the next calls; sound_status's free frames from waveOutGetPosition.
static HWAVEOUT g_wo;
static CRITICAL_SECTION g_woLock;
static bool g_woLockInit;
static long long g_woWritten;
static std::vector<WAVEHDR *> g_woBufs;
static const long long WO_CAP = 22050;
static long long wo_played (void)
{
	MMTIME t; t.wType = TIME_SAMPLES;
	if (!g_wo || waveOutGetPosition (g_wo, &t, sizeof t) != MMSYSERR_NOERROR || t.wType != TIME_SAMPLES) return g_woWritten;
	return (long long) t.u.sample;
}
static void wo_reap (void)
{
	for (size_t i = 0; i < g_woBufs.size (); )
		if (g_woBufs[i]->dwFlags & WHDR_DONE)
		{
			waveOutUnprepareHeader (g_wo, g_woBufs[i], sizeof (WAVEHDR));
			free (g_woBufs[i]->lpData); delete g_woBufs[i];
			g_woBufs.erase (g_woBufs.begin () + i);
		}
		else i++;
}
static int sound_acquire (void)
{
	if (!g_woLockInit) { InitializeCriticalSection (&g_woLock); g_woLockInit = true; }
	EnterCriticalSection (&g_woLock);
	int r = 0;
	if (!g_wo)
	{
		WAVEFORMATEX f = {}; f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 2; f.nSamplesPerSec = 44100;
		f.wBitsPerSample = 16; f.nBlockAlign = 4; f.nAvgBytesPerSec = 44100 * 4;
		if (waveOutOpen (&g_wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) { g_woWritten = 0; r = 1; }
		else { g_wo = 0; r = -1; }
	}
	LeaveCriticalSection (&g_woLock);
	return r;
}
static void sound_release (void)
{
	if (!g_woLockInit) return;
	EnterCriticalSection (&g_woLock);
	if (g_wo)
	{
		waveOutReset (g_wo);
		wo_reap ();
		waveOutClose (g_wo);
		g_wo = 0;
	}
	LeaveCriticalSection (&g_woLock);
}
static int sound_write (const short *p, unsigned n)
{
	if (!g_woLockInit) return -1;
	EnterCriticalSection (&g_woLock);
	if (!g_wo) { LeaveCriticalSection (&g_woLock); return -1; }
	wo_reap ();
	long long q = g_woWritten - wo_played ();
	if (q < 0) q = 0;
	if (n > WO_CAP - q) n = q >= WO_CAP ? 0 : (unsigned) (WO_CAP - q);
	if (n)
	{
		WAVEHDR *h = new WAVEHDR (); h->dwBufferLength = n * 4; h->lpData = (LPSTR) malloc (n * 4);
		memcpy (h->lpData, p, n * 4);
		waveOutPrepareHeader (g_wo, h, sizeof *h);
		waveOutWrite (g_wo, h, sizeof *h);
		g_woBufs.push_back (h);
		g_woWritten += n;
	}
	LeaveCriticalSection (&g_woLock);
	return (int) n;
}
static int sound_status (unsigned *r, unsigned *f, unsigned *o)
{
	if (r) *r = 44100;
	if (f) *f = 0;
	if (o) *o = 0;
	if (!g_woLockInit) return -1;
	EnterCriticalSection (&g_woLock);
	if (g_wo)
	{
		long long q = g_woWritten - wo_played ();
		if (q < 0) q = 0;
		if (f) *f = (unsigned) (q >= WO_CAP ? 0 : WO_CAP - q);
		if (o) *o = 1;
	}
	LeaveCriticalSection (&g_woLock);
	return 0;
}
static int sound_config (int, int) { return 0; }	// (waveOut's own latency: not known; the queue is the delay)

static void unimplemented (void)
{
	char b[200];
	snprintf (b, sizeof b, "Jet Browser called an Onyx function this Windows build does not have (from %p).", __builtin_return_address (0));
	fprintf (stderr, "%s\n", b); fflush (stderr);
	MessageBoxA (g_hwnd, b, "Jet Browser for Windows", MB_ICONERROR);
	ExitProcess (2);
}

// ---- the command line, the switches, the log --------------------------------------------------------------------------------------
static std::string g_argsText __attribute__ ((init_priority (101)));
static bool beside (const wchar_t *name) { return exists (g_root + L"\\" + name); }
static void setup_switches ()
{
	int n = 0;
	LPWSTR *av = CommandLineToArgvW (GetCommandLineW (), &n);
	bool console = beside (L"console"), perf = beside (L"perf"), jsdebug = beside (L"jsdebug");
	g_netdebug = beside (L"netdebug");
	std::string url;
	for (int i = 1; av && i < n; i++)
	{
		std::string a = utf8 (av[i]);
		if (a == "--console" || a == "-console") console = true;
		else if (a == "--perf" || a == "-perf") perf = true;
		else if (a == "--jsdebug" || a == "-jsdebug") jsdebug = true;
		else if (a == "--netdebug" || a == "-netdebug") g_netdebug = true;
		else if (a == "--maximised" || a == "--maximized") _putenv ("JET_MAXIMISED=1");
		else if (a == "--sharp") SetProcessDPIAware ();		// (1:1 pixels on a scaled display: small, not blurred)
		else if (a.size () > 3 && a.compare (0, 3, "NS_") == 0 && a.find ('=') != std::string::npos) _putenv (a.c_str ());
		else if (url.empty ())
		{
			if (a.size () > 2 && a[1] == ':' && (a[2] == '\\' || a[2] == '/')) a = "file:///" + slashes (a);	// a file opened with Jet
			url = a;
		}
	}
	if (av) LocalFree (av);
	g_argsText = url;
	if (perf) _putenv ("NS_PERF=1");
	if (jsdebug) _putenv ("NS_JSDEBUG=1");
	if (g_netdebug) _putenv ("NS_NETDEBUG=1");
	// the data folder (the user's: Cookies, History, the caches -- data\ram is RAM:)
	CreateDirectoryW ((g_root + L"\\data").c_str (), 0);
	CreateDirectoryW ((g_root + L"\\data\\ram").c_str (), 0);
	// the log: a console window, or data\jet.log (stderr: NetSurf's messages, ONYX-PERF, the scripts' console)
	if (console && AllocConsole ())
	{
		freopen ("CONOUT$", "w", stdout);
		freopen ("CONOUT$", "w", stderr);
		SetConsoleOutputCP (CP_UTF8);
		SetConsoleTitleW (L"Jet Browser -- log");
	}
	else
	{
		std::wstring log = g_root + L"\\data\\jet.log";
		FILE *f = _wfopen (log.c_str (), L"wb");			// (a new log each launch)
		if (f) fclose (f);
		_wfreopen (log.c_str (), L"ab", stderr);			// (both appending: their lines in order)
		_wfreopen (log.c_str (), L"ab", stdout);
	}
	setvbuf (stderr, 0, _IONBF, 0);
	setvbuf (stdout, 0, _IONBF, 0);
	SYSTEMTIME t; GetLocalTime (&t);
	fprintf (stderr, "Jet Browser for Windows -- %04d-%02d-%02d %02d:%02d:%02d -- %s%s%s%s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
		 perf ? "perf " : "", jsdebug ? "jsdebug " : "", g_netdebug ? "netdebug " : "", url.empty () ? "(home page)" : url.c_str ());
}
static int get_args (char *b, unsigned n)
{
	snprintf (b, n, "%s", g_argsText.c_str ());
	return (int) strlen (b);
}

static void setup (void)
{
	void *p = VirtualAlloc ((void *) KAPI_TABLE_VA, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (p != (void *) KAPI_TABLE_VA) { MessageBoxA (0, "The Onyx table could not be placed.", "Jet Browser for Windows", MB_ICONERROR); ExitProcess (3); }
	T = (TKApiTable *) p;
	void **slots = (void **) T;
	for (size_t i = 0; i < sizeof (TKApiTable) / sizeof (void *); i++) slots[i] = (void *) unimplemented;
	T->version = KAPI_ABI_VERSION;
	_fmode = _O_BINARY;
	QueryPerformanceFrequency (&g_qpf); QueryPerformanceCounter (&g_qp0);
	InitializeCriticalSection (&g_thLock); InitializeCriticalSection (&g_postLock); InitializeCriticalSection (&g_sockLock);
	InitializeCriticalSection (&g_syncLock); InitializeCriticalSection (&g_canvasLock);
	DuplicateHandle (GetCurrentProcess (), GetCurrentThread (), GetCurrentProcess (), &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
	g_mainId = GetCurrentThreadId ();
	timeBeginPeriod (1);
	init_root ();
	SetCurrentDirectoryW (g_root.c_str ());
	setup_switches ();
	T->create_window = create; T->create_window_ex = create_ex; T->resize_window = resize; T->resize_window2 = resize2; T->move_window = move_window;
	T->set_pointer_handler = set_ptr; T->set_key_handler = set_key; T->set_click_handler = set_click; T->screen_size = screen_size;
	T->screen_native = screen_native; T->set_timezone = set_timezone;
	T->font_width = font_w; T->font_height = font_h; T->present = h_present; T->pump_events = pump; T->pump_wait = pump_wait; T->post = post;
	T->msleep = h_msleep; T->get_ticks = h_get_ticks; T->should_exit = h_should_exit; T->yield = h_yield; T->exit = h_exit;
	T->draw_text = draw_text; T->draw_text_buf = draw_text_buf; T->get_chrome = get_chrome; T->win_geometry = win_geometry;
	T->win_minimise = win_minimise; T->win_list = win_list; T->desk = desk; T->win_desk = win_desk; T->cursor_pos = cursor_pos; T->get_modifiers = get_mods;
	T->set_menu = set_menu; T->get_menu = get_menu; T->menu_command = menu_command; T->drag_data = drag_data; T->drag_begin = drag_begin;
	T->clipboard_set = clipboard_set; T->clipboard_get = clipboard_get;
	T->open = f_open; T->read = f_read; T->write = h_write; T->fsize = f_fsize; T->fsize64 = f_fsize64; T->seek = f_seek; T->close = f_close;
	T->save_file = save_file; T->opendir = f_opendir; T->readdir = f_readdir; T->closedir = f_closedir; T->vol_info = vol_info;
	T->mkdir = f_mkdir; T->remove = f_remove; T->rename = f_rename; T->app_dir = app_dir; T->chdir = h_chdir; T->getcwd = h_getcwd;
	T->stdout_write = stdout_write; T->stdin_read = stdin_read; T->get_args = get_args; T->get_datetime = h_get_datetime; T->random = h_random;
	T->file_out = file_out; T->stream_read = stream_read; T->stream_read_nb = stream_read_nb; T->stream_write = stream_write;
	T->stream_close = stream_close; T->stream_eof = stream_eof;
	T->exec = h_exec; T->exec_as = exec_as; T->list_procs = list_procs;
	T->thread_create = thread_create; T->thread_join = thread_join; T->thread_exit = thread_exit; T->thread_self = thread_self;
	T->sound_acquire = sound_acquire; T->sound_release = sound_release; T->sound_write = sound_write;
	T->sound_status = sound_status; T->sound_config = sound_config;	// (<video>, <audio>: waveOut)
	T->thread_priority = thread_priority; T->core_acquire = core_acquire; T->core_run = core_run; T->core_state = core_state; T->core_release = core_release;
	T->mutex_create = mutex_create; T->mutex_lock = mutex_lock; T->mutex_unlock = mutex_unlock;
	T->event_create = event_create; T->event_set = event_set; T->event_reset = event_reset; T->event_wait = event_wait;
	T->barrier_create = barrier_create; T->barrier_wait = barrier_wait; T->sync_close = sync_close;
	T->wait_word = wait_word; T->wake_word = wake_word; T->code_alloc = code_alloc;
	T->ipc_register = ipc_register; T->ipc_lookup = ipc_lookup; T->mailbox_send = mailbox_send; T->mailbox_recv = mailbox_recv;
	T->surface_create = surface_create; T->surface_map = surface_map; T->surface_size = surface_size;
	T->net_status = net_status; T->net_info = net_info; T->net_resolve = net_resolve;
	T->tcp_connect = tcp_connect; T->tcp_send = tcp_send; T->tcp_recv = tcp_recv; T->tcp_close = tcp_close;
	T->gpu_info = gpu_info;
	T->list_apps = list_apps; T->list_windows = list_windows; T->launch = launch; T->raise_app = raise_app; T->toggle_app = toggle_app;
	T->set_wheel_speed = set_wheel; T->get_wheel_speed = get_wheel; T->meminfo = meminfo; T->sbrk = h_sbrk;
	T->memset = k_memset; T->memcpy = k_memcpy; T->memmove = k_memmove;
}

// (before every other constructor: the apps' globals may call the kapi)
static struct WinKapiInit { WinKapiInit () { setup (); } } s_init __attribute__ ((init_priority (101)));
