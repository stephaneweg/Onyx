//
// pc/Koton/winkapi.cpp -- the Onyx kernel's ABI table (kern/kapi_abi.h) on Windows, so that Koton (user/Apps/
// koton), wtk, FreeType, Koton's plugins (user/Apps/kp_*) and its AI helper (user/bin/llm.cpp) build for
// Windows from the Onyx sources, unchanged: the table is put where the apps look for it (KAPI_TABLE_VA)
// before any constructor runs, and filled with Win32 equivalents of what those programs call.
//
//   the window     a Windows window; its client area is the app's canvas (the Onyx frame is not drawn:
//                  get_chrome says "borderless"), the app's menu bar a Windows menu. The window's size is
//                  the "work area": wtk's maximised Root follows it (GUI_EVENT_DISPLAY_RESIZE).
//   files          "SD:/..." is the folder of Koton.exe (ONYX_SD, inherited by the programs it starts);
//                  "C:/..." a Windows path. A program ".../main" or "SD:/bin/llm" is main.exe, llm.exe.
//   sound          the mapped PCM ring (kapi_sound_map) played through WASAPI (shared mode, event driven).
//   MIDI           every MIDI input of Windows (winmm), as the kernel's USB MIDI events.
//   processes      CreateProcess (in a job: they end with Koton); pipes; kill by name / pid.
//   IPC            named services, mailboxes (a ring per process in a named mapping), surfaces (named
//                  mappings), wait_word (WaitOnAddress, re-checked every millisecond as the Onyx kernel
//                  re-reads sleeping words: a word another process changed wakes its sleeper).
//   network        Winsock (the AI helper's HTTPS: mbedTLS over it, as on Onyx).
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
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <bcrypt.h>
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
static std::wstring g_root __attribute__ ((init_priority (101)));	// "SD:" (no trailing separator; made before setup runs)

static void init_root ()
{
	wchar_t b[MAX_PATH * 2];
	DWORD n = GetEnvironmentVariableW (L"ONYX_SD", b, MAX_PATH * 2);
	if (n > 0 && n < MAX_PATH * 2) { g_root = b; return; }
	n = GetModuleFileNameW (0, b, MAX_PATH * 2);
	std::wstring p (b, n);
	size_t s = p.find_last_of (L"\\/");
	g_root = s == std::wstring::npos ? L"." : p.substr (0, s);
	SetEnvironmentVariableW (L"ONYX_SD", g_root.c_str ());	// (the programs Koton starts: the same card)
}
static bool exists (const std::wstring &p) { return GetFileAttributesW (p.c_str ()) != INVALID_FILE_ATTRIBUTES; }
// an Onyx path -> a Windows one
static std::wstring winpath (const char *p)
{
	std::string s (p ? p : "");
	std::wstring out;
	size_t c = s.find (':');
	if (c == 1 && s.size () >= 2 && isalpha ((unsigned char) s[0])) out = wide (s.c_str ());	// C:/...
	else if (c != std::string::npos && c < 5)						// SD:/..., SD1:/...
	{
		std::string r = s.substr (c + 1);
		while (!r.empty () && (r[0] == '/' || r[0] == '\\')) r.erase (0, 1);
		out = g_root + (r.empty () ? L"" : L"\\" + wide (r.c_str ()));
	}
	else if (!s.empty () && (s[0] == '/' || s[0] == '\\')) out = g_root + wide (s.c_str ());
	else out = g_root + L"\\" + wide (s.c_str ());
	for (auto &ch : out) if (ch == L'/') ch = L'\\';
	while (out.size () > 3 && out.back () == L'\\') out.pop_back ();
	return out;
}
// a program: ".../main" -> ".../main.exe"
static std::wstring program (const char *p)
{
	std::wstring w = winpath (p);
	if (!exists (w) || (GetFileAttributesW (w.c_str ()) & FILE_ATTRIBUTE_DIRECTORY))
		if (exists (w + L".exe")) w += L".exe";
	return w;
}
static std::string slashes (std::string s) { for (auto &c : s) if (c == '\\') c = '/'; return s; }

// ---- time -------------------------------------------------------------------------------------------------------
static unsigned h_get_ticks (void) { return (unsigned) (GetTickCount64 () / 10); }
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

// ---- files --------------------------------------------------------------------------------------------------------
static void *f_open (const char *p)
{
	std::wstring w = winpath (p);
	if (GetFileAttributesW (w.c_str ()) & FILE_ATTRIBUTE_DIRECTORY) { if (!exists (w + L".exe")) return 0; w += L".exe"; }
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
	FILE *f = _wfopen (winpath (p).c_str (), L"wb");
	if (!f) return -1;
	size_t w = n ? fwrite (b, 1, n, f) : 0;
	fclose (f);
	return w == n ? (int) n : -1;		// (as the kernel's: the bytes written, or -1)
}
static int f_mkdir (const char *p) { return CreateDirectoryW (winpath (p).c_str (), 0) ? 0 : -1; }
static int f_remove (const char *p)
{
	std::wstring w = winpath (p);
	DWORD a = GetFileAttributesW (w.c_str ());
	if (a == INVALID_FILE_ATTRIBUTES) return -1;
	return (a & FILE_ATTRIBUTE_DIRECTORY ? RemoveDirectoryW (w.c_str ()) : DeleteFileW (w.c_str ())) ? 0 : -1;
}
static int f_rename (const char *a, const char *b) { return MoveFileExW (winpath (a).c_str (), winpath (b).c_str (), 0) ? 0 : -1; }
struct Dir { HANDLE h; WIN32_FIND_DATAW d; bool first; };
static void *f_opendir (const char *p)
{
	Dir *d = new Dir;
	std::wstring w = winpath (p);
	if (w.empty () || w.back () != L'\\') w += L'\\';		// ("C:\\" already ends with one)
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
		if (d->d.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
		std::string n = utf8 (d->d.cFileName);
		bool dir = (d->d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		if (!dir && n.size () > 4 && !_stricmp (n.c_str () + n.size () - 4, ".exe")) n.resize (n.size () - 4);	// (programs: as on the card)
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

// ---- console ------------------------------------------------------------------------------------------------------
static int out_to (DWORD which, const void *b, unsigned n)
{
	HANDLE h = GetStdHandle (which);
	DWORD w = 0;
	if (h && h != INVALID_HANDLE_VALUE) WriteFile (h, b, n, &w, 0);
	return (int) n;
}
static int h_write (int fd, const void *b, unsigned n) { return out_to (fd == 2 ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE, b, n); }
static int stdout_write (const void *b, unsigned n) { return out_to (STD_OUTPUT_HANDLE, b, n); }
static int stdin_read (void *b, unsigned n)
{
	HANDLE h = GetStdHandle (STD_INPUT_HANDLE);
	DWORD r = 0;
	if (!h || h == INVALID_HANDLE_VALUE || !ReadFile (h, b, n, &r, 0)) return 0;
	return (int) r;
}

// ---- threads, futex -----------------------------------------------------------------------------------------------
struct Thread { HANDLE h; int (*fn) (void *); void *arg; int code; };
static std::vector<Thread *> g_threads;		// tid = index + 2
static CRITICAL_SECTION g_thLock;
static HANDLE g_mainThread;
static DWORD WINAPI thread_tramp (LPVOID p) { Thread *t = (Thread *) p; t->code = t->fn (t->arg); return (DWORD) t->code; }
static int thread_create (int (*fn) (void *), void *arg, unsigned stack, const char *)
{
	Thread *t = new Thread; t->fn = fn; t->arg = arg; t->code = 0;
	t->h = CreateThread (0, stack < 65536 ? 65536 : stack, thread_tramp, t, 0, 0);
	if (!t->h) { delete t; return -1; }
	EnterCriticalSection (&g_thLock);
	int tid = -1;
	for (size_t i = 0; i < g_threads.size (); i++) if (!g_threads[i]) { g_threads[i] = t; tid = (int) i + 2; break; }
	if (tid < 0) { g_threads.push_back (t); tid = (int) g_threads.size () + 1; }
	LeaveCriticalSection (&g_thLock);
	return tid;
}
static HANDLE thread_handle (int tid)
{
	if (tid == 0) return GetCurrentThread ();
	if (tid == 1) return g_mainThread;
	EnterCriticalSection (&g_thLock);
	HANDLE h = tid - 2 < (int) g_threads.size () && g_threads[tid - 2] ? g_threads[tid - 2]->h : 0;
	LeaveCriticalSection (&g_thLock);
	return h;
}
static int thread_priority (int tid, int prio)
{
	HANDLE h = thread_handle (tid);
	if (!h) return -1;
	SetThreadPriority (h, prio > 0 ? THREAD_PRIORITY_TIME_CRITICAL : THREAD_PRIORITY_NORMAL);
	return 0;
}
static int thread_join (int tid, unsigned timeout, int *code)
{
	if (tid < 2) return -1;
	EnterCriticalSection (&g_thLock);
	Thread *t = tid - 2 < (int) g_threads.size () ? g_threads[tid - 2] : 0;
	LeaveCriticalSection (&g_thLock);
	if (!t) return -1;
	if (WaitForSingleObject (t->h, timeout == KAPI_WAIT_FOREVER ? INFINITE : timeout) != WAIT_OBJECT_0) return 1;
	if (code) *code = t->code;
	CloseHandle (t->h);
	EnterCriticalSection (&g_thLock); g_threads[tid - 2] = 0; LeaveCriticalSection (&g_thLock);
	delete t;
	return 0;
}
static void thread_exit (int code) { ExitThread ((DWORD) code); }
static int thread_self (void)
{
	if (GetCurrentThreadId () == GetThreadId (g_mainThread)) return 1;
	DWORD me = GetCurrentThreadId ();
	EnterCriticalSection (&g_thLock);
	int r = 0;
	for (size_t i = 0; i < g_threads.size (); i++) if (g_threads[i] && GetThreadId (g_threads[i]->h) == me) r = (int) i + 2;
	LeaveCriticalSection (&g_thLock);
	return r;
}
// (no app cores on Windows: Koton's engine runs on a real-time thread)
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
		WaitOnAddress ((volatile VOID *) addr, &e, 4, 1);		// (a wake from this process: at once;
		if (*addr != expected) return 0;				//  a change by another one: within 1 ms)
		if (timeout != KAPI_WAIT_FOREVER && GetTickCount64 () - t0 >= timeout) return 1;
	}
}
static int wake_word (volatile unsigned *addr) { if (!addr) return -1; WakeByAddressAll ((PVOID) addr); return 1; }

// ---- the window ---------------------------------------------------------------------------------------------------
static HWND g_hwnd;
static unsigned *g_canvas; static int g_cw, g_ch, g_stride;
static char g_title[64] = "Onyx";
static gui_handler g_ptr, g_key, g_click, g_menuFn;
static volatile bool g_quit;
static int g_btn;				// the buttons held (1 left, 2 right, 4 middle)
static bool g_tracking;
static HMENU g_menu;
static std::string g_menuSpec;
static std::string g_drop;			// the files dropped last (kapi_drag_data)
struct Ev { int to; int ev; long long v; };	// to: 0 pointer, 1 key, 2 menu
static std::vector<Ev> g_evq;
static int g_clientW, g_clientH;		// the window's client area now
static unsigned g_bgFill = 0x1B1E24;

static long long ptrval (int x, int y, int buttons, int changed, int wheel)
{
	if (x < 0) x = 0; if (y < 0) y = 0; if (x > 0xFFFF) x = 0xFFFF; if (y > 0xFFFF) y = 0xFFFF;
	return ((long long) (wheel & 0xFF) << 48) | ((long long) (changed & 0xFF) << 40) | ((long long) (buttons & 0xFF) << 32)
		| ((long long) x << 16) | (long long) y;
}
static void blit (HDC dc)
{
	if (!g_canvas) return;
	BITMAPINFO bi; memset (&bi, 0, sizeof bi);
	bi.bmiHeader.biSize = sizeof bi.bmiHeader;
	bi.bmiHeader.biWidth = g_stride; bi.bmiHeader.biHeight = -g_ch;
	bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
	SetDIBitsToDevice (dc, 0, 0, g_cw, g_ch, 0, 0, 0, g_ch, g_canvas, &bi, DIB_RGB_COLORS);
	HBRUSH br = CreateSolidBrush (RGB ((g_bgFill >> 16) & 255, (g_bgFill >> 8) & 255, g_bgFill & 255));
	RECT r;
	if (g_clientW > g_cw) { r.left = g_cw; r.top = 0; r.right = g_clientW; r.bottom = g_clientH; FillRect (dc, &r, br); }
	if (g_clientH > g_ch) { r.left = 0; r.top = g_ch; r.right = g_cw < g_clientW ? g_cw : g_clientW; r.bottom = g_clientH; FillRect (dc, &r, br); }
	DeleteObject (br);
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
	case WM_CLOSE: g_quit = true; return 0;
	case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint (h, &ps); blit (dc); EndPaint (h, &ps); return 0; }
	case WM_ERASEBKGND: return 1;
	case WM_SIZE:
		if (w != SIZE_MINIMIZED)
		{
			int cw = LOWORD (l), ch = HIWORD (l);
			if (cw > 0 && ch > 0 && (cw != g_clientW || ch != g_clientH))
			{
				g_clientW = cw; g_clientH = ch;
				push (0, GUI_EVENT_DISPLAY_RESIZE, ((long long) cw << 16) | ch);
			}
		}
		return 0;
	case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *) l; mm->ptMinTrackSize.x = 900; mm->ptMinTrackSize.y = 600; return 0; }
	case WM_MOUSEMOVE:
		if (!g_tracking) { TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent (&t); g_tracking = true; push (0, GUI_EVENT_PTR_ENTER, ptrval ((short) LOWORD (l), (short) HIWORD (l), g_btn, 0, 0)); }
		mouse (GUI_EVENT_PTR_MOVE, l, 0, 0);
		if (g_btn) push (3, GUI_EVENT_CANVAS_MOTION, ((long long) g_btn << 32) | ((long long) (LOWORD (l)) << 16) | HIWORD (l));
		return 0;
	case WM_MOUSELEAVE: g_tracking = false; if (!g_btn) push (0, GUI_EVENT_PTR_LEAVE, 0); return 0;
	case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
	{
		int b = m == WM_LBUTTONDOWN ? 1 : m == WM_RBUTTONDOWN ? 2 : 4;
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
		long k = key_code (w);
		if (k) { push (1, GUI_EVENT_KEY, k); return 0; }
		break;
	}
	case WM_CHAR:
		if (w >= 0xD800 && w <= 0xDFFF) return 0;		// (no characters beyond the BMP)
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
			if (DragQueryFileW (d, i, f, MAX_PATH * 2)) { if (!all.empty ()) all += '\n'; all += slashes (utf8 (f)); }
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
	unsigned *px = (unsigned *) calloc ((size_t) w * h, 4);
	if (!px) return 0;
	for (long i = 0; i < (long) w * h; i++) px[i] = g_bgFill;
	return px;
}
static unsigned *create_ex (int x, int y, int w, int h, const char *title, unsigned flags)
{
	(void) x; (void) y; (void) flags;
	if (g_hwnd) return 0;				// (one window a process)
	snprintf (g_title, sizeof g_title, "%s", title ? title : "Onyx");
	WNDCLASSEXW wc; memset (&wc, 0, sizeof wc);
	wc.cbSize = sizeof wc; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW (0);
	wc.hCursor = LoadCursor (0, IDC_ARROW); wc.lpszClassName = L"OnyxWindow";
	wc.hIcon = LoadIconW (wc.hInstance, MAKEINTRESOURCEW (1));
	if (!wc.hIcon) wc.hIcon = LoadIcon (0, IDI_APPLICATION);
	RegisterClassExW (&wc);
	RECT r = { 0, 0, w, h };
	AdjustWindowRect (&r, WS_OVERLAPPEDWINDOW, TRUE);
	g_hwnd = CreateWindowExW (WS_EX_ACCEPTFILES, L"OnyxWindow", wide (g_title).c_str (), WS_OVERLAPPEDWINDOW,
				  CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);
	if (!g_hwnd) return 0;
	DragAcceptFiles (g_hwnd, TRUE);
	g_canvas = make_canvas (w, h); g_cw = w; g_ch = h; g_stride = w;
	apply_menu ();
	ShowWindow (g_hwnd, SW_SHOWMAXIMIZED);
	RECT c; GetClientRect (g_hwnd, &c); g_clientW = c.right; g_clientH = c.bottom;
	UpdateWindow (g_hwnd);
	return g_canvas;
}
static unsigned *create (int w, int h, const char *t) { return create_ex (0, 0, w, h, t, 0); }
static unsigned *resize2 (int w, int h, int *stride)
{
	if (w < 1 || h < 1) return 0;
	unsigned *px = make_canvas (w, h);
	if (!px) return 0;
	unsigned *old = g_canvas;
	g_canvas = px; g_cw = w; g_ch = h; g_stride = w;
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
	if (h) *h = r.bottom - r.top + 60;		// (Koton keeps 60 px for Onyx's menu bar and dock)
}
static int win_geometry (struct kapi_win_geom *g)
{
	if (!g_hwnd) return -1;
	RECT c; GetClientRect (g_hwnd, &c);
	int cw = c.right > 0 ? c.right : g_cw, ch = c.bottom > 0 ? c.bottom : g_ch;
	g->x = 0; g->y = 0; g->w = g_cw; g->h = g_ch; g->cw = g_cw; g->ch = g_ch;	// (no frame of Onyx's)
	g->ax = 0; g->ay = 0; g->aw = cw; g->ah = ch;			// the work area: the client area
	g->state = KAPI_WIN_KEYS;
	return 0;
}
static int win_minimise (unsigned) { if (g_hwnd) ShowWindow (g_hwnd, SW_MINIMIZE); return 0; }
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
	EnterCriticalSection (&g_postLock); g_posts.push_back (Post { fn, ctx, v }); LeaveCriticalSection (&g_postLock);
	if (g_hwnd) PostMessageW (g_hwnd, WM_NULL, 0, 0);
	return 0;
}
static int pump_wait (unsigned timeout)
{
	if (g_evq.empty ()) MsgWaitForMultipleObjects (0, 0, FALSE, timeout == KAPI_WAIT_FOREVER ? INFINITE : timeout, QS_ALLINPUT);
	pump ();
	return 0;
}
static int h_should_exit (void) { return g_quit ? 1 : 0; }
static void h_exit (int s) { ExitProcess ((UINT) s); }

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
static int set_menu (const char *spec, gui_handler fn) { g_menuSpec = spec ? spec : ""; g_menuFn = fn; apply_menu (); return 1; }
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
	memcpy (buf, o.data (), o.size () < cap ? o.size () : cap);
	return (int) o.size ();
}

// ---- processes ---------------------------------------------------------------------------------------------------
struct Proc { HANDLE h; DWORD pid; std::string name; };
static std::vector<Proc *> g_procs;
static CRITICAL_SECTION g_procLock;
static HANDLE g_job;
static std::string g_argsText __attribute__ ((init_priority (101)));

static HANDLE job ()
{
	if (g_job) return g_job;
	g_job = CreateJobObjectW (0, 0);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION li; memset (&li, 0, sizeof li);
	li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	SetInformationJobObject (g_job, JobObjectExtendedLimitInformation, &li, sizeof li);
	return g_job;
}
static Proc *start (const char *path, const char *args, const char *name, HANDLE in, HANDLE out, HANDLE err)
{
	std::wstring exe = program (path);
	if (!exists (exe)) return 0;
	std::wstring cmd = L"\"" + exe + L"\"";
	if (args && args[0]) cmd += L" " + wide (args);
	STARTUPINFOW si; memset (&si, 0, sizeof si); si.cb = sizeof si;
	bool pipes = in || out || err;
	if (pipes)
	{
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = in ? in : GetStdHandle (STD_INPUT_HANDLE);
		si.hStdOutput = out ? out : GetStdHandle (STD_OUTPUT_HANDLE);
		si.hStdError = err ? err : GetStdHandle (STD_ERROR_HANDLE);
	}
	PROCESS_INFORMATION pi;
	std::vector<wchar_t> c (cmd.begin (), cmd.end ()); c.push_back (0);
	if (!CreateProcessW (exe.c_str (), c.data (), 0, 0, pipes ? TRUE : FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, 0, g_root.c_str (), &si, &pi)) return 0;
	AssignProcessToJobObject (job (), pi.hProcess);
	ResumeThread (pi.hThread);
	CloseHandle (pi.hThread);
	Proc *p = new Proc; p->h = pi.hProcess; p->pid = pi.dwProcessId;
	if (name && name[0]) p->name = name;
	else { std::string s = slashes (path ? path : ""); size_t k = s.find_last_of ("/:"); p->name = k == std::string::npos ? s : s.substr (k + 1); }
	EnterCriticalSection (&g_procLock); g_procs.push_back (p); LeaveCriticalSection (&g_procLock);
	return p;
}
static bool alive (HANDLE h) { return h && WaitForSingleObject (h, 0) == WAIT_TIMEOUT; }
static int exec_as (const char *path, const char *args, const char *name) { return start (path, args, name, 0, 0, 0) ? 1 : 0; }
static int h_exec (const char *path, const char *args) { return exec_as (path, args, 0); }
static int list_procs (char *buf, unsigned size)
{
	std::string s = std::to_string (GetCurrentProcessId ()) + " a R 0 self\n";
	EnterCriticalSection (&g_procLock);
	for (Proc *p : g_procs) if (alive (p->h)) s += std::to_string (p->pid) + " a R 0 " + p->name + "\n";
	LeaveCriticalSection (&g_procLock);
	unsigned n = (unsigned) s.size () < size ? (unsigned) s.size () : size - 1;
	memcpy (buf, s.data (), n); buf[n] = 0;
	return (int) n;
}
static int kill_pid (int pid, int)
{
	int r = 0;
	EnterCriticalSection (&g_procLock);
	for (Proc *p : g_procs) if ((int) p->pid == pid && alive (p->h)) { TerminateProcess (p->h, 1); r = 1; }
	LeaveCriticalSection (&g_procLock);
	return r;
}
static int h_kill (const char *name)
{
	int r = 0;
	EnterCriticalSection (&g_procLock);
	for (Proc *p : g_procs) if (name && p->name == name && alive (p->h)) { TerminateProcess (p->h, 1); r++; }
	LeaveCriticalSection (&g_procLock);
	return r;
}
static int get_args (char *b, unsigned n)
{
	snprintf (b, n, "%s", g_argsText.c_str ());
	return (int) strlen (b);
}

// streams: in-memory pipes (a child's stdin / stdout when spawned), files written
struct Stream
{
	int kind;				// 0 a pipe, 1 a file written
	CRITICAL_SECTION cs;
	std::string buf; bool eof;
	HANDLE osWrite;				// a child's stdin: what is written goes there
	FILE *f;
	LONG refs;
};
static Stream *new_stream (int kind) { Stream *s = new Stream; s->kind = kind; InitializeCriticalSection (&s->cs); s->eof = false; s->osWrite = 0; s->f = 0; s->refs = 1; return s; }
static void unref (Stream *s)
{
	if (InterlockedDecrement (&s->refs) > 0) return;
	if (s->osWrite) CloseHandle (s->osWrite);
	if (s->f) fclose (s->f);
	DeleteCriticalSection (&s->cs);
	delete s;
}
static void *h_pipe (void) { return new_stream (0); }
static void *file_out (const char *p, int append)
{
	FILE *f = _wfopen (winpath (p).c_str (), append ? L"ab" : L"wb");
	if (!f) return 0;
	Stream *s = new_stream (1); s->f = f;
	return s;
}
static int stream_write (void *h, const void *b, unsigned n)
{
	Stream *s = (Stream *) h;
	if (!s) return -1;
	if (s->kind == 1) return (int) fwrite (b, 1, n, s->f);
	if (s->osWrite) { DWORD w = 0; return WriteFile (s->osWrite, b, n, &w, 0) ? (int) w : -1; }
	EnterCriticalSection (&s->cs); s->buf.append ((const char *) b, n); LeaveCriticalSection (&s->cs);
	return (int) n;
}
static int stream_read_nb (void *h, void *b, unsigned n)
{
	Stream *s = (Stream *) h;
	if (!s || s->kind != 0) return 0;
	EnterCriticalSection (&s->cs);
	int r;
	if (!s->buf.empty ()) { r = (int) (s->buf.size () < n ? s->buf.size () : n); memcpy (b, s->buf.data (), r); s->buf.erase (0, r); }
	else r = s->eof ? 0 : -1;
	LeaveCriticalSection (&s->cs);
	return r;
}
static int stream_read (void *h, void *b, unsigned n)
{
	for (;;) { int r = stream_read_nb (h, b, n); if (r >= 0) return r; Sleep (2); }
}
static void stream_eof (void *h)
{
	Stream *s = (Stream *) h;
	if (!s) return;
	EnterCriticalSection (&s->cs);
	s->eof = true;
	if (s->osWrite) { CloseHandle (s->osWrite); s->osWrite = 0; }
	LeaveCriticalSection (&s->cs);
}
static void stream_close (void *h) { Stream *s = (Stream *) h; if (s) { if (s->kind == 1) { fclose (s->f); s->f = 0; } stream_eof (s); unref (s); } }
struct Reader { HANDLE rd; Stream *to; };
static DWORD WINAPI reader (LPVOID p)
{
	Reader *r = (Reader *) p;
	char b[4096]; DWORD n;
	while (ReadFile (r->rd, b, sizeof b, &n, 0) && n > 0) { EnterCriticalSection (&r->to->cs); r->to->buf.append (b, n); LeaveCriticalSection (&r->to->cs); }
	EnterCriticalSection (&r->to->cs); r->to->eof = true; LeaveCriticalSection (&r->to->cs);
	CloseHandle (r->rd); unref (r->to); delete r;
	return 0;
}
static void *spawn (const char *path, const char *args, void *in, void *out)
{
	SECURITY_ATTRIBUTES sa = { sizeof sa, 0, TRUE };
	HANDLE inR = 0, inW = 0, outR = 0, outW = 0;
	Stream *si = (Stream *) in, *so = (Stream *) out;
	if (si && si->kind == 0) { CreatePipe (&inR, &inW, &sa, 0); SetHandleInformation (inW, HANDLE_FLAG_INHERIT, 0); }
	if (so && so->kind == 0) { CreatePipe (&outR, &outW, &sa, 1 << 16); SetHandleInformation (outR, HANDLE_FLAG_INHERIT, 0); }
	Proc *p = start (path, args, 0, inR, outW, outW);
	if (inR) CloseHandle (inR);
	if (outW) CloseHandle (outW);
	if (!p) { if (inW) CloseHandle (inW); if (outR) CloseHandle (outR); return 0; }
	if (inW)
	{
		EnterCriticalSection (&si->cs);
		if (!si->buf.empty ()) { DWORD w; WriteFile (inW, si->buf.data (), (DWORD) si->buf.size (), &w, 0); si->buf.clear (); }
		if (si->eof) CloseHandle (inW); else si->osWrite = inW;
		LeaveCriticalSection (&si->cs);
	}
	if (outR)
	{
		Reader *r = new Reader; r->rd = outR; r->to = so; InterlockedIncrement (&so->refs);
		HANDLE t = CreateThread (0, 0, reader, r, 0, 0); if (t) CloseHandle (t);
	}
	return p;
}
static int proc_done (void *h) { Proc *p = (Proc *) h; return !p || !alive (p->h) ? 1 : 0; }
static int h_wait (void *h)
{
	Proc *p = (Proc *) h;
	if (!p) return -1;
	WaitForSingleObject (p->h, INFINITE);
	DWORD c = 0; GetExitCodeProcess (p->h, &c);
	return (int) c;
}

// ---- IPC: services, mailboxes, surfaces ---------------------------------------------------------------------------------
static HANDLE named_mutex (const std::string &n) { return CreateMutexA (0, FALSE, ("Local\\OnyxWin." + n).c_str ()); }
static void *named_map (const std::string &n, unsigned size, bool create, HANDLE *hOut = 0)
{
	std::string name = "Local\\OnyxWin." + n;
	HANDLE h = create ? CreateFileMappingA (INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, size, name.c_str ())
			  : OpenFileMappingA (FILE_MAP_ALL_ACCESS, FALSE, name.c_str ());
	if (!h) return 0;
	void *p = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (!p) { CloseHandle (h); return 0; }
	if (hOut) *hOut = h;			// (else kept open for the process's life: the mapping lives on)
	return p;
}
static bool pid_alive (int pid)
{
	if (pid <= 0) return false;
	if ((DWORD) pid == GetCurrentProcessId ()) return true;
	HANDLE h = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) pid);
	if (!h) return false;
	bool a = WaitForSingleObject (h, 0) == WAIT_TIMEOUT;
	CloseHandle (h);
	return a;
}
struct Services { struct { char name[32]; int pid; } s[64]; };
static Services *services (HANDLE *mx)
{
	static Services *sv; static HANDLE m;
	if (!sv) { sv = (Services *) named_map ("services", sizeof (Services), true); m = named_mutex ("services"); }
	*mx = m;
	return sv;
}
static int ipc_register (const char *name)
{
	HANDLE mx; Services *sv = services (&mx);
	if (!sv) return 0;
	WaitForSingleObject (mx, INFINITE);
	int me = (int) GetCurrentProcessId (), r = -1, free_ = -1;
	for (int i = 0; i < 64 && r < 0; i++)
	{
		if (sv->s[i].pid && !strncmp (sv->s[i].name, name, 31)) r = sv->s[i].pid == me ? 1 : pid_alive (sv->s[i].pid) ? 0 : (sv->s[i].pid = me, 1);
		else if (free_ < 0 && (!sv->s[i].pid || !pid_alive (sv->s[i].pid))) free_ = i;
	}
	if (r < 0 && free_ >= 0) { snprintf (sv->s[free_].name, 32, "%s", name); sv->s[free_].pid = me; r = 1; }
	ReleaseMutex (mx);
	return r < 0 ? 0 : r;
}
static int ipc_lookup (const char *name)
{
	HANDLE mx; Services *sv = services (&mx);
	if (!sv) return 0;
	WaitForSingleObject (mx, INFINITE);
	int r = 0;
	for (int i = 0; i < 64; i++) if (sv->s[i].pid && !strncmp (sv->s[i].name, name, 31) && pid_alive (sv->s[i].pid)) { r = sv->s[i].pid; break; }
	ReleaseMutex (mx);
	return r;
}
// a mailbox: 32 messages of <= 512 bytes, a ring in a named mapping of its process
enum { MB_SLOTS = 32, MB_MAX = 512 };
struct Mailbox { volatile LONG head, tail; struct { int from, type; unsigned len; char data[MB_MAX]; } m[MB_SLOTS]; };
struct MbRef { int pid; Mailbox *mb; HANDLE mx, ev; };
static std::vector<MbRef> g_mbs;
static CRITICAL_SECTION g_mbLock;
static MbRef *mailbox (int pid)
{
	EnterCriticalSection (&g_mbLock);
	for (auto &r : g_mbs) if (r.pid == pid) { LeaveCriticalSection (&g_mbLock); return &r; }
	std::string n = std::to_string (pid);
	MbRef r; r.pid = pid;
	r.mb = (Mailbox *) named_map ("mb." + n, sizeof (Mailbox), true);
	r.mx = named_mutex ("mbm." + n);
	r.ev = CreateEventA (0, FALSE, FALSE, ("Local\\OnyxWin.mbe." + n).c_str ());
	if (!r.mb || !r.mx) { LeaveCriticalSection (&g_mbLock); return 0; }
	g_mbs.reserve (256);
	g_mbs.push_back (r);
	MbRef *p = &g_mbs.back ();
	LeaveCriticalSection (&g_mbLock);
	return p;
}
static int mailbox_send (int pid, int type, const void *in, unsigned len)
{
	if (len > MB_MAX || !pid_alive (pid)) return 0;
	MbRef *r = mailbox (pid);
	if (!r) return 0;
	WaitForSingleObject (r->mx, INFINITE);
	int ok = 0;
	if (r->mb->head - r->mb->tail < MB_SLOTS)
	{
		auto &m = r->mb->m[r->mb->head % MB_SLOTS];
		m.from = (int) GetCurrentProcessId (); m.type = type; m.len = len;
		if (len) memcpy (m.data, in, len);
		r->mb->head = r->mb->head + 1;
		ok = 1;
	}
	ReleaseMutex (r->mx);
	if (ok && r->ev) SetEvent (r->ev);
	return ok;
}
static int mailbox_recv (int *from, int *type, void *buf, unsigned cap, int blocking)
{
	MbRef *r = mailbox ((int) GetCurrentProcessId ());
	if (!r) return -1;
	for (;;)
	{
		WaitForSingleObject (r->mx, INFINITE);
		int n = -1;
		if (r->mb->tail != r->mb->head)
		{
			auto &m = r->mb->m[r->mb->tail % MB_SLOTS];
			if (from) *from = m.from;
			if (type) *type = m.type;
			n = (int) (m.len < cap ? m.len : cap);
			if (n) memcpy (buf, m.data, n);
			r->mb->tail = r->mb->tail + 1;
		}
		ReleaseMutex (r->mx);
		if (n >= 0 || !blocking) return n;
		WaitForSingleObject (r->ev, 50);
	}
}
static int register_shell (void) { return 0; }
static int shell_request (int, const void *, unsigned) { return -1; }
// surfaces: named mappings, "<id>", 64 bytes of header (w, h) then the pixels
struct Surf { int id; char *base; HANDLE h; };
static std::vector<Surf> g_surfs;
static CRITICAL_SECTION g_sfLock;
static int surface_create (int w, int h)
{
	static int n;
	if (w < 1 || h < 1) return -1;
	int id = (int) ((((GetCurrentProcessId () >> 2) & 0x7FFF) << 16) | ((++n) & 0xFFFF));
	HANDLE mh;
	char *p = (char *) named_map ("sf." + std::to_string (id), 64 + (unsigned) w * h * 4, true, &mh);
	if (!p) return -1;
	((int *) p)[0] = 0x46525553; ((int *) p)[1] = w; ((int *) p)[2] = h;
	EnterCriticalSection (&g_sfLock); g_surfs.push_back (Surf { id, p, mh }); LeaveCriticalSection (&g_sfLock);
	return id;
}
static char *surf_base (int id)
{
	EnterCriticalSection (&g_sfLock);
	for (auto &s : g_surfs) if (s.id == id) { LeaveCriticalSection (&g_sfLock); return s.base; }
	LeaveCriticalSection (&g_sfLock);
	HANDLE mh;
	char *p = (char *) named_map ("sf." + std::to_string (id), 0, false, &mh);
	if (!p) return 0;
	EnterCriticalSection (&g_sfLock); g_surfs.push_back (Surf { id, p, mh }); LeaveCriticalSection (&g_sfLock);
	return p;
}
static unsigned *surface_map (int id) { char *b = surf_base (id); return b ? (unsigned *) (b + 64) : 0; }
static int surface_size (int id, int *w, int *h)
{
	char *b = surf_base (id);
	if (!b) return 0;
	if (w) *w = ((int *) b)[1];
	if (h) *h = ((int *) b)[2];
	return 1;
}
static void surface_present (int) { }
static int surface_destroy (int id)
{
	EnterCriticalSection (&g_sfLock);
	for (size_t i = 0; i < g_surfs.size (); i++)
		if (g_surfs[i].id == id) { UnmapViewOfFile (g_surfs[i].base); CloseHandle (g_surfs[i].h); g_surfs.erase (g_surfs.begin () + i); break; }
	LeaveCriticalSection (&g_sfLock);
	return 0;
}

// ---- sound: the mapped ring, played by WASAPI ------------------------------------------------------------------------------
static struct kapi_sound_ring *g_ring;
static HANDLE g_audioThread;
static volatile bool g_audioStop;
static unsigned g_audioPlayed;			// (the ring's wr seen moving lately: an underrun counts)

static void take (short *dst, unsigned n)
{
	kapi_sound_ring *r = g_ring;
	unsigned rd = r->rd, have = r->wr - rd, k = have < n ? have : n, mask = r->frames - 1;
	for (unsigned i = 0; i < k; i++) { unsigned j = ((rd + i) & mask) * 2; dst[i * 2] = r->data[j]; dst[i * 2 + 1] = r->data[j + 1]; }
	for (unsigned i = k; i < n; i++) dst[i * 2] = dst[i * 2 + 1] = 0;
	if (k < n && g_audioPlayed) r->dry = r->dry + 1;
	g_audioPlayed = have ? 50 : g_audioPlayed ? g_audioPlayed - 1 : 0;
	MemoryBarrier ();
	r->rd = rd + k;
	WakeByAddressAll ((PVOID) &r->rd);
}
static DWORD WINAPI audio_wasapi (LPVOID)
{
	CoInitializeEx (0, COINIT_MULTITHREADED);
	IMMDeviceEnumerator *en = 0; IMMDevice *dev = 0; IAudioClient *ac = 0; IAudioRenderClient *rc = 0;
	HANDLE ev = CreateEventW (0, FALSE, FALSE, 0);
	WAVEFORMATEX wf; memset (&wf, 0, sizeof wf);
	wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = SOUND_RATE; wf.wBitsPerSample = 16;
	wf.nBlockAlign = 4; wf.nAvgBytesPerSec = SOUND_RATE * 4;
	UINT32 frames = 0;
	bool ok = SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), 0, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void **) &en))
		&& SUCCEEDED (en->GetDefaultAudioEndpoint (eRender, eConsole, &dev))
		&& SUCCEEDED (dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, 0, (void **) &ac))
		&& SUCCEEDED (ac->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
					      | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 100000 /* 10 ms */, 0, &wf, 0))
		&& SUCCEEDED (ac->SetEventHandle (ev)) && SUCCEEDED (ac->GetBufferSize (&frames))
		&& SUCCEEDED (ac->GetService (__uuidof (IAudioRenderClient), (void **) &rc));
	if (ok)
	{
		BYTE *d;
		if (SUCCEEDED (rc->GetBuffer (frames, &d))) rc->ReleaseBuffer (frames, AUDCLNT_BUFFERFLAGS_SILENT);
		ac->Start ();
		SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_TIME_CRITICAL);
		while (!g_audioStop)
		{
			WaitForSingleObject (ev, 20);
			UINT32 pad = 0;
			if (FAILED (ac->GetCurrentPadding (&pad))) break;
			UINT32 n = frames - pad;
			if (!n) continue;
			if (SUCCEEDED (rc->GetBuffer (n, &d))) { take ((short *) d, n); rc->ReleaseBuffer (n, 0); }
		}
		ac->Stop ();
	}
	else		// no WASAPI (or it refused 44.1 kHz): winmm, 10 ms buffers
	{
		HWAVEOUT wo;
		if (waveOutOpen (&wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR)
		{
			enum { NB = 6, BF = 441 };
			static short pcm[NB][BF * 2]; WAVEHDR hd[NB]; memset (hd, 0, sizeof hd);
			for (int i = 0; i < NB; i++) { hd[i].lpData = (LPSTR) pcm[i]; hd[i].dwBufferLength = BF * 4; waveOutPrepareHeader (wo, &hd[i], sizeof hd[i]); hd[i].dwFlags |= WHDR_DONE; }
			while (!g_audioStop)
			{
				bool any = false;
				for (int i = 0; i < NB; i++) if (hd[i].dwFlags & WHDR_DONE) { take (pcm[i], BF); waveOutWrite (wo, &hd[i], sizeof hd[i]); any = true; }
				if (!any) Sleep (2);
			}
			waveOutReset (wo);
			for (int i = 0; i < NB; i++) waveOutUnprepareHeader (wo, &hd[i], sizeof hd[i]);
			waveOutClose (wo);
		}
		else		// no output at all: the ring still drained in time (the song goes on, silent)
		{
			static short pcm[441 * 2];
			ULONGLONG t0 = GetTickCount64 (), done = 0;
			while (!g_audioStop)
			{
				ULONGLONG due = (GetTickCount64 () - t0) * SOUND_RATE / 1000;
				while (done + 441 <= due) { take (pcm, 441); done += 441; }
				Sleep (5);
			}
		}
	}
	if (rc) rc->Release (); if (ac) ac->Release (); if (dev) dev->Release (); if (en) en->Release ();
	CloseHandle (ev);
	CoUninitialize ();
	return 0;
}
static int sound_acquire (void)
{
	if (g_audioThread) return 1;
	if (!g_ring)
	{
		g_ring = (kapi_sound_ring *) _aligned_malloc (sizeof (kapi_sound_ring), 64);
		memset (g_ring, 0, sizeof *g_ring);
		g_ring->magic = KAPI_SOUND_RING_MAGIC; g_ring->frames = KAPI_SOUND_RING_FRAMES; g_ring->rate = SOUND_RATE;
		g_ring->chunk = 256; g_ring->ahead = 2;
	}
	g_audioStop = false;
	g_audioThread = CreateThread (0, 0, audio_wasapi, 0, 0, 0);
	return g_audioThread ? 1 : 0;
}
static void sound_release (void)
{
	if (!g_audioThread) return;
	g_audioStop = true;
	WaitForSingleObject (g_audioThread, 2000);
	CloseHandle (g_audioThread); g_audioThread = 0;
}
static int sound_config (int chunk, int ahead)
{
	if (!g_ring) return -1;
	g_ring->chunk = chunk <= 0 ? 1024 : chunk; g_ring->ahead = ahead <= 0 ? 4 : ahead;
	return (int) ((g_ring->ahead + 1) * g_ring->chunk);
}
static struct kapi_sound_ring *sound_map (void) { return g_audioThread ? g_ring : 0; }
static int sound_write (const short *f, unsigned n) { return g_ring ? (int) kapi_sound_ring_write (g_ring, f, n) : 0; }
static int sound_status (unsigned *rate, unsigned *freeF, unsigned *owner)
{
	if (rate) *rate = SOUND_RATE;
	if (freeF) *freeF = g_ring ? g_ring->frames - (g_ring->wr - g_ring->rd) : 0;
	if (owner) *owner = GetCurrentProcessId ();
	return g_audioThread ? 1 : 0;
}
static int sound_volume (int, int) { return 10; }

// ---- MIDI input (winmm): every input device, as the kernel's USB MIDI events -------------------------------------------------
static std::vector<kapi_midi_event> g_midi;
static CRITICAL_SECTION g_midiLock;
static std::vector<HMIDIIN> g_midiIn;
static UINT g_midiCount = 0xFFFFFFFF;
static unsigned g_midiCheck;
static void CALLBACK midi_cb (HMIDIIN, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR)
{
	if (msg != MIM_DATA) return;
	unsigned char st = (unsigned char) (p1 & 0xFF), d1 = (unsigned char) ((p1 >> 8) & 0x7F), d2 = (unsigned char) ((p1 >> 16) & 0x7F);
	if (st < 0x80 || st == 0xF8 || st == 0xFE) return;			// (clock, active sensing: not kept)
	kapi_midi_event e; memset (&e, 0, sizeof e);
	e.time_us = (unsigned) (GetTickCount64 () * 1000);
	e.status = st; e.data1 = d1; e.data2 = d2; e.device = (unsigned char) (inst + 1);
	int t = st >> 4;
	e.length = (unsigned char) (t == 0xC || t == 0xD ? 2 : st >= 0xF0 ? 1 : 3);
	EnterCriticalSection (&g_midiLock);
	if (g_midi.size () < 1024) g_midi.push_back (e);
	LeaveCriticalSection (&g_midiLock);
}
static void midi_open ()
{
	UINT n = midiInGetNumDevs ();
	if (n == g_midiCount) return;
	for (HMIDIIN h : g_midiIn) { midiInStop (h); midiInClose (h); }
	g_midiIn.clear ();
	g_midiCount = n;
	for (UINT i = 0; i < n && i < KAPI_MIDI_DEVICES; i++)
	{
		HMIDIIN h;
		if (midiInOpen (&h, i, (DWORD_PTR) midi_cb, i, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) { midiInStart (h); g_midiIn.push_back (h); }
	}
}
static int midi_read (struct kapi_midi_event *ev, int max)
{
	unsigned now = h_get_ticks ();
	if (now - g_midiCheck >= 200 || g_midiCount == 0xFFFFFFFF) { g_midiCheck = now; midi_open (); }
	if (!ev || max < 0) return -1;
	EnterCriticalSection (&g_midiLock);
	int n = (int) g_midi.size () < max ? (int) g_midi.size () : max;
	for (int i = 0; i < n; i++) ev[i] = g_midi[i];
	g_midi.erase (g_midi.begin (), g_midi.begin () + n);
	LeaveCriticalSection (&g_midiLock);
	return n;
}
static int midi_devices (void) { if (g_midiCount == 0xFFFFFFFF) midi_open (); return (int) g_midiIn.size (); }

// ---- the network (Winsock) ----------------------------------------------------------------------------------------------------
static std::vector<SOCKET> g_socks;
static bool net_up ()
{
	static int st;
	if (!st) { WSADATA d; st = WSAStartup (MAKEWORD (2, 2), &d) == 0 ? 1 : -1; }
	return st > 0;
}
static int net_status (char *ip, unsigned cap) { if (ip && cap) snprintf (ip, cap, "%s", "this PC"); return net_up () ? 1 : 0; }
static int net_info (char *b, unsigned cap) { if (b && cap) b[0] = 0; return 0; }
static int tcp_connect (const char *host, unsigned port)
{
	if (!net_up ()) return -1;
	addrinfo hints; memset (&hints, 0, sizeof hints); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
	addrinfo *res = 0;
	if (getaddrinfo (host, std::to_string (port).c_str (), &hints, &res) != 0) return -2;
	SOCKET s = INVALID_SOCKET;
	for (addrinfo *a = res; a && s == INVALID_SOCKET; a = a->ai_next)
	{
		s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
		if (s == INVALID_SOCKET) continue;
		if (connect (s, a->ai_addr, (int) a->ai_addrlen) != 0) { closesocket (s); s = INVALID_SOCKET; }
	}
	freeaddrinfo (res);
	if (s == INVALID_SOCKET) return -3;
	u_long nb = 1; ioctlsocket (s, FIONBIO, &nb);
	for (size_t i = 0; i < g_socks.size (); i++) if (g_socks[i] == INVALID_SOCKET) { g_socks[i] = s; return (int) i; }
	g_socks.push_back (s);
	return (int) g_socks.size () - 1;
}
static SOCKET sock (int h) { return h >= 0 && h < (int) g_socks.size () ? g_socks[h] : INVALID_SOCKET; }
static int tcp_send (int h, const void *b, unsigned n)
{
	SOCKET s = sock (h);
	if (s == INVALID_SOCKET) return -1;
	unsigned done = 0;
	while (done < n)
	{
		int w = send (s, (const char *) b + done, (int) (n - done), 0);
		if (w > 0) { done += (unsigned) w; continue; }
		if (WSAGetLastError () == WSAEWOULDBLOCK) { fd_set f; FD_ZERO (&f); FD_SET (s, &f); timeval tv = { 5, 0 }; select (0, 0, &f, 0, &tv); continue; }
		return -1;
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
static void tcp_close (int h) { SOCKET s = sock (h); if (s != INVALID_SOCKET) { closesocket (s); g_socks[h] = INVALID_SOCKET; } }

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
	MEMORYSTATUSEX m; m.dwLength = sizeof m; GlobalMemoryStatusEx (&m);
	if (t) *t = (unsigned long) (m.ullTotalPhys / 1024); if (f) *f = (unsigned long) (m.ullAvailPhys / 1024);
	if (a) *a = 0; if (pg) *pg = 4;
	return 1;
}
static void *h_sbrk (long) { return (void *) -1; }
static void *k_memset (void *d, int c, unsigned long n) { return memset (d, c, n); }
static void *k_memcpy (void *d, const void *s, unsigned long n) { return memcpy (d, s, n); }
static void *k_memmove (void *d, const void *s, unsigned long n) { return memmove (d, s, n); }

static void unimplemented (void)
{
	char b[160];
	snprintf (b, sizeof b, "This program called an Onyx function Windows does not have (from %p).", __builtin_return_address (0));
	MessageBoxA (g_hwnd, b, "Onyx for Windows", MB_ICONERROR);
	ExitProcess (2);
}

static void args_text ()
{
	int n = 0;
	LPWSTR *av = CommandLineToArgvW (GetCommandLineW (), &n);
	std::string s;
	for (int i = 1; av && i < n; i++) { if (!s.empty ()) s += ' '; s += utf8 (av[i]); }
	if (av) LocalFree (av);
	// a path given by Windows (an opened file): Onyx's slashes
	if (s.size () > 2 && s[1] == ':' && (s[2] == '\\' || s[2] == '/')) s = slashes (s);
	g_argsText = s;
}

static void setup (void)
{
	void *p = VirtualAlloc ((void *) KAPI_TABLE_VA, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (p != (void *) KAPI_TABLE_VA) { MessageBoxA (0, "The Onyx table could not be placed.", "Onyx for Windows", MB_ICONERROR); ExitProcess (3); }
	T = (TKApiTable *) p;
	void **slots = (void **) T;
	for (size_t i = 0; i < sizeof (TKApiTable) / sizeof (void *); i++) slots[i] = (void *) unimplemented;
	T->version = KAPI_ABI_VERSION;
	InitializeCriticalSection (&g_thLock); InitializeCriticalSection (&g_procLock); InitializeCriticalSection (&g_mbLock);
	InitializeCriticalSection (&g_sfLock); InitializeCriticalSection (&g_midiLock); InitializeCriticalSection (&g_postLock);
	DuplicateHandle (GetCurrentProcess (), GetCurrentThread (), GetCurrentProcess (), &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
	timeBeginPeriod (1);
	init_root ();
	args_text ();
	T->create_window = create; T->create_window_ex = create_ex; T->resize_window = resize; T->resize_window2 = resize2; T->move_window = move_window;
	T->set_pointer_handler = set_ptr; T->set_key_handler = set_key; T->set_click_handler = set_click; T->screen_size = screen_size;
	T->font_width = font_w; T->font_height = font_h; T->present = h_present; T->pump_events = pump; T->pump_wait = pump_wait; T->post = post;
	T->msleep = h_msleep; T->get_ticks = h_get_ticks; T->should_exit = h_should_exit; T->yield = h_yield; T->exit = h_exit;
	T->draw_text = draw_text; T->draw_text_buf = draw_text_buf; T->get_chrome = get_chrome; T->win_geometry = win_geometry;
	T->win_minimise = win_minimise; T->desk = desk; T->win_desk = win_desk; T->cursor_pos = cursor_pos; T->get_modifiers = get_mods;
	T->set_menu = set_menu; T->get_menu = get_menu; T->menu_command = menu_command; T->drag_data = drag_data; T->drag_begin = drag_begin;
	T->clipboard_set = clipboard_set; T->clipboard_get = clipboard_get;
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
	T->register_shell = register_shell; T->shell_request = shell_request;
	T->surface_create = surface_create; T->surface_map = surface_map; T->surface_size = surface_size;
	T->surface_present = surface_present; T->surface_destroy = surface_destroy;
	T->sound_acquire = sound_acquire; T->sound_release = sound_release; T->sound_write = sound_write; T->sound_status = sound_status;
	T->sound_config = sound_config; T->sound_map = sound_map; T->sound_volume = sound_volume;
	T->midi_read = midi_read; T->midi_devices = midi_devices;
	T->net_status = net_status; T->net_info = net_info; T->tcp_connect = tcp_connect; T->tcp_send = tcp_send; T->tcp_recv = tcp_recv; T->tcp_close = tcp_close;
	T->list_apps = list_apps; T->list_windows = list_windows; T->launch = launch; T->raise_app = raise_app; T->toggle_app = toggle_app;
	T->set_wheel_speed = set_wheel; T->get_wheel_speed = get_wheel; T->meminfo = meminfo; T->sbrk = h_sbrk;
	T->memset = k_memset; T->memcpy = k_memcpy; T->memmove = k_memmove;
}

// (before every other constructor: the apps' globals may call the kapi)
static struct WinKapiInit { WinKapiInit () { setup (); } } s_init __attribute__ ((init_priority (101)));
