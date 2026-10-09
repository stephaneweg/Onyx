//
// display.cpp -- the PC's screen and input for the runner (display.h): a frame buffer of the screen's size, shown in
// a Win32 window on Windows (scaled to the window: a 1280 x 800 screen in a smaller window stays whole), kept in
// memory otherwise (--headless, and on Linux for now); --shot writes it as a .bmp; --input plays a script of input.
// The PC's keys become what a Pi's USB keyboard gives the server (kern/wsrv.h): the cooked string -- characters in
// UTF-8, Enter "\n", Backspace 0x7F, Tab, Esc, VT100 escapes for the arrows, Home, End, PgUp, PgDn, Delete and F1-F12
// (user/Servers/common/wm/window.cpp NextKey) --, the modifiers, and the keys held (key_held).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "display.h"
#include <thread>
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#endif

static std::mutex s_M;
struct Fb { u32 *p = 0; size_t n = 0; u32 *data () { return p; } const u32 *data () const { return p; } size_t size () const { return n; }
	    u32 &operator[] (size_t i) { return p[i]; } };
static Fb s_Fb;					// (host shared memory: a full-screen program may draw into it, fullscreen_direct)
static int s_W = 0, s_H = 0;
static std::atomic<bool> s_Direct { false };	// a program draws straight into the screen: repainted 60 times a second
static DisplayOpts s_Opts;

void display_size (int *w, int *h) { *w = s_W; *h = s_H; }

bool display_grab (u32 *dst, int w, int h)
{
	if (w != s_W || h != s_H) return false;
	std::lock_guard<std::mutex> L (s_M);
	memcpy (dst, s_Fb.data (), (size_t) w * h * 4);
	return true;
}

bool display_write_bmp (const std::string &path)
{
	std::vector<u32> px;
	{ std::lock_guard<std::mutex> L (s_M); px.assign (s_Fb.data (), s_Fb.data () + s_Fb.size ()); }
	FILE *f = fopen (path.c_str (), "wb");
	if (!f) return false;
	u32 size = 54 + (u32) px.size () * 4;
	u8 h[54] = { 'B', 'M' };
	auto put32 = [&] (int o, u32 v) { h[o] = (u8) v; h[o + 1] = (u8) (v >> 8); h[o + 2] = (u8) (v >> 16); h[o + 3] = (u8) (v >> 24); };
	put32 (2, size); put32 (10, 54); put32 (14, 40); put32 (18, (u32) s_W); put32 (22, (u32) -s_H);	// (top-down)
	h[26] = 1; h[28] = 32; put32 (34, (u32) px.size () * 4);
	fwrite (h, 1, 54, f);
	for (auto &p : px) p &= 0x00FFFFFF;
	fwrite (px.data (), 4, px.size (), f);
	fclose (f);
	return true;
}

#ifdef _WIN32
static HWND s_Wnd = 0;

void display_present (const u32 *pixels, int stride, int x, int y, int w, int h)
{
	{
		std::lock_guard<std::mutex> L (s_M);
		for (int r = 0; r < h; r++) memcpy (&s_Fb[(size_t) (y + r) * s_W + x], pixels + (size_t) (y + r) * stride + x, (size_t) w * 4);
	}
	if (s_Wnd)
	{
		RECT rc;
		GetClientRect (s_Wnd, &rc);
		int cw = rc.right, ch = rc.bottom;
		RECT d = { x * cw / s_W, y * ch / s_H, (x + w) * cw / s_W + 1, (y + h) * ch / s_H + 1 };
		InvalidateRect (s_Wnd, &d, FALSE);
	}
}

static void to_screen (LPARAM lp, int *x, int *y)
{
	RECT rc;
	GetClientRect (s_Wnd, &rc);
	int cx = GET_X_LPARAM (lp), cy = GET_Y_LPARAM (lp);
	*x = rc.right > 0 ? cx * s_W / rc.right : cx;
	*y = rc.bottom > 0 ? cy * s_H / rc.bottom : cy;
	*x = *x < 0 ? 0 : *x >= s_W ? s_W - 1 : *x;
	*y = *y < 0 ? 0 : *y >= s_H ? s_H - 1 : *y;
}

static unsigned s_Buttons = 0;
static int s_LastX = 0, s_LastY = 0;

static unsigned mods_now (void)
{
	return ((GetKeyState (VK_CONTROL) & 0x8000) ? 1u : 0u) | ((GetKeyState (VK_SHIFT) & 0x8000) ? 2u : 0u)
	     | ((GetKeyState (VK_MENU) & 0x8000) ? 4u : 0u) | (((GetKeyState (VK_LWIN) | GetKeyState (VK_RWIN)) & 0x8000) ? 8u : 0u);
}

// A Windows virtual key -> the key_held code (KEY_* of appkit.h, 'a'..'z', '0'..'9', ' ', 27), 0 none.
static int held_code (WPARAM vk)
{
	if (vk >= 'A' && vk <= 'Z') return (int) (vk - 'A' + 'a');
	if (vk >= '0' && vk <= '9') return (int) vk;
	switch (vk)
	{
	case VK_UP: return 0x100; case VK_DOWN: return 0x101; case VK_LEFT: return 0x102; case VK_RIGHT: return 0x103;
	case VK_HOME: return 0x104; case VK_END: return 0x105; case VK_PRIOR: return 0x106; case VK_NEXT: return 0x107;
	case VK_DELETE: return 0x108; case VK_RETURN: return 13; case VK_ESCAPE: return 27; case VK_SPACE: return ' ';
	case VK_TAB: return 9; case VK_BACK: return 8;
	}
	if (vk >= VK_F1 && vk <= VK_F12) return (int) (0x110 + (vk - VK_F1));
	return 0;
}

// The keys WM_CHAR does not give: their escape sequence (with the xterm modifier form when Ctrl / Shift / Alt).
static const char *escape_of (WPARAM vk)
{
	switch (vk)
	{
	case VK_UP: return "A"; case VK_DOWN: return "B"; case VK_RIGHT: return "C"; case VK_LEFT: return "D";
	case VK_HOME: return "1~"; case VK_END: return "4~"; case VK_PRIOR: return "5~"; case VK_NEXT: return "6~";
	case VK_DELETE: return "3~"; case VK_INSERT: return "2~";
	case VK_F1: return "[A"; case VK_F2: return "[B"; case VK_F3: return "[C"; case VK_F4: return "[D"; case VK_F5: return "[E";
	case VK_F6: return "17~"; case VK_F7: return "18~"; case VK_F8: return "19~"; case VK_F9: return "20~";
	case VK_F10: return "21~"; case VK_F11: return "23~"; case VK_F12: return "24~";
	}
	return 0;
}

static wchar_t s_HighSurrogate = 0;

static LRESULT CALLBACK wnd_proc (HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint (w, &ps);
		RECT rc;
		GetClientRect (w, &rc);
		BITMAPINFO bi;
		memset (&bi, 0, sizeof bi);
		bi.bmiHeader.biSize = sizeof bi.bmiHeader;
		bi.bmiHeader.biWidth = s_W; bi.bmiHeader.biHeight = -s_H;
		bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
		SetStretchBltMode (dc, HALFTONE);
		{
			std::lock_guard<std::mutex> L (s_M);
			StretchDIBits (dc, 0, 0, rc.right, rc.bottom, 0, 0, s_W, s_H, s_Fb.data (), &bi, DIB_RGB_COLORS, SRCCOPY);
		}
		EndPaint (w, &ps);
		return 0;
	}
	case WM_TIMER:
		if (s_Direct) InvalidateRect (w, 0, FALSE);
		return 0;
	case WM_MOUSEMOVE:
		to_screen (lp, &s_LastX, &s_LastY);
		ws_input_pointer (s_LastX, s_LastY, s_Buttons, 0);
		return 0;
	case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
	{
		unsigned bit = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) ? 1 : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) ? 2 : 4;
		bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
		if (down) { s_Buttons |= bit; SetCapture (w); } else { s_Buttons &= ~bit; if (!s_Buttons) ReleaseCapture (); }
		to_screen (lp, &s_LastX, &s_LastY);
		ws_input_pointer (s_LastX, s_LastY, s_Buttons, 0);
		return 0;
	}
	case WM_MOUSEWHEEL:
		ws_input_pointer (s_LastX, s_LastY, s_Buttons, GET_WHEEL_DELTA_WPARAM (wp) / WHEEL_DELTA);
		return 0;
	case WM_KEYDOWN: case WM_SYSKEYDOWN:
	{
		ws_input_mods (mods_now ());
		int hc = held_code (wp);
		if (hc && !(lp & (1 << 30))) ws_input_held (hc, true);
		const char *e = escape_of (wp);
		if (e)
		{
			unsigned m = mods_now () & 7;
			char s[16];
			if (m && e[0] != '[')
			{
				int x = 1 + ((m & 2) ? 1 : 0) + ((m & 4) ? 2 : 0) + ((m & 1) ? 4 : 0);
				size_t n = strlen (e);
				if (e[n - 1] == '~') snprintf (s, sizeof s, "\x1b[%.*s;%d~", (int) n - 1, e, x);
				else snprintf (s, sizeof s, "\x1b[1;%d%s", x, e);
			}
			else snprintf (s, sizeof s, "\x1b[%s", e);
			ws_input_key (s);
			return 0;
		}
		if (msg == WM_SYSKEYDOWN && wp != VK_F4) return 0;
		break;
	}
	case WM_KEYUP: case WM_SYSKEYUP:
	{
		ws_input_mods (mods_now ());
		int hc = held_code (wp);
		if (hc) ws_input_held (hc, false);
		if (msg == WM_SYSKEYUP) return 0;
		break;
	}
	case WM_CHAR:
	{
		wchar_t c = (wchar_t) wp;
		if (c >= 0xD800 && c < 0xDC00) { s_HighSurrogate = c; return 0; }
		u32 cp = c;
		if (c >= 0xDC00 && c < 0xE000 && s_HighSurrogate) cp = 0x10000 + (((u32) s_HighSurrogate - 0xD800) << 10) + (c - 0xDC00);
		s_HighSurrogate = 0;
		if (cp == '\r') cp = '\n';			// Enter
		else if (cp == 8) cp = 0x7F;			// Backspace
		char s[8]; int n = 0;
		if (cp < 0x80) s[n++] = (char) cp;
		else if (cp < 0x800) { s[n++] = (char) (0xC0 | (cp >> 6)); s[n++] = (char) (0x80 | (cp & 63)); }
		else if (cp < 0x10000) { s[n++] = (char) (0xE0 | (cp >> 12)); s[n++] = (char) (0x80 | ((cp >> 6) & 63)); s[n++] = (char) (0x80 | (cp & 63)); }
		else { s[n++] = (char) (0xF0 | (cp >> 18)); s[n++] = (char) (0x80 | ((cp >> 12) & 63)); s[n++] = (char) (0x80 | ((cp >> 6) & 63)); s[n++] = (char) (0x80 | (cp & 63)); }
		s[n] = 0;
		ws_input_key (s);
		return 0;
	}
	case WM_KILLFOCUS:
		ws_input_mods (0);
		return 0;
	case WM_CLOSE:
		ws_quit_request ();
		DestroyWindow (w);
		return 0;
	case WM_DESTROY:
		PostQuitMessage (0);
		return 0;
	}
	return DefWindowProcW (w, msg, wp, lp);
}

static void window_thread (void)
{
	HINSTANCE inst = GetModuleHandleW (0);
	WNDCLASSW wc;
	memset (&wc, 0, sizeof wc);
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursor (0, IDC_ARROW);
	wc.lpszClassName = L"OnyxRunner";
	RegisterClassW (&wc);
	RECT r = { 0, 0, s_W, s_H };
	// (a screen larger than the PC's work area: the window made smaller, the picture scaled)
	RECT work;
	SystemParametersInfoW (SPI_GETWORKAREA, 0, &work, 0);
	int ww = s_W, wh = s_H;
	while (ww > work.right - work.left - 40 || wh > work.bottom - work.top - 60) { ww = ww * 3 / 4; wh = wh * 3 / 4; }
	r.right = ww; r.bottom = wh;
	DWORD style = WS_OVERLAPPEDWINDOW;
	AdjustWindowRect (&r, style, FALSE);
	std::wstring title (s_Opts.title.begin (), s_Opts.title.end ());
	s_Wnd = CreateWindowW (L"OnyxRunner", title.c_str (), style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
	ShowWindow (s_Wnd, SW_SHOW);
	SetTimer (s_Wnd, 1, 16, 0);
	ShowCursor (FALSE);			// (the server draws the pointer)
	MSG m;
	while (GetMessageW (&m, 0, 0, 0) > 0) { TranslateMessage (&m); DispatchMessageW (&m); }
	s_Wnd = 0;
}
#else
void display_present (const u32 *pixels, int stride, int x, int y, int w, int h)
{
	std::lock_guard<std::mutex> L (s_M);
	for (int r = 0; r < h; r++) memcpy (&s_Fb[(size_t) (y + r) * s_W + x], pixels + (size_t) (y + r) * stride + x, (size_t) w * 4);
}
#endif

// ---- the script of input (--input) -----------------------------------------------------------------------------
// "wait MS; move X Y; down X Y; up X Y; click X Y; rclick X Y; wheel X Y N; key TEXT; esc; enter; tab; bs; mods N;
//  shot FILE.bmp; waitfile FILE (until it exists); include FILE (its steps played here)"; TEXT is typed as it is
//  ("\n" written \n). waitfile + include let a test look at a shot before it says where to click.
static void input_thread (std::string script)
{
	unsigned buttons = 0;
	size_t i = 0;
	while (i < script.size ())
	{
		size_t j = script.find (';', i);
		if (j == std::string::npos) j = script.size ();
		std::string step = script.substr (i, j - i);
		i = j + 1;
		while (!step.empty () && step[0] == ' ') step.erase (0, 1);
		char cmd[16] = {};
		int a = 0, b = 0, c = 0;
		sscanf (step.c_str (), "%15s %d %d %d", cmd, &a, &b, &c);
		std::string arg = step.find (' ') != std::string::npos ? step.substr (step.find (' ') + 1) : "";
		if (!strcmp (cmd, "wait")) std::this_thread::sleep_for (std::chrono::milliseconds (a));
		else if (!strcmp (cmd, "move")) ws_input_pointer (a, b, buttons, 0);
		else if (!strcmp (cmd, "down")) { buttons |= 1; ws_input_pointer (a, b, buttons, 0); }
		else if (!strcmp (cmd, "up")) { buttons &= ~1u; ws_input_pointer (a, b, buttons, 0); }
		else if (!strcmp (cmd, "click") || !strcmp (cmd, "rclick"))
		{
			unsigned bit = cmd[0] == 'r' ? 2 : 1;
			ws_input_pointer (a, b, buttons, 0);
			std::this_thread::sleep_for (std::chrono::milliseconds (30));
			ws_input_pointer (a, b, buttons | bit, 0);
			std::this_thread::sleep_for (std::chrono::milliseconds (60));
			ws_input_pointer (a, b, buttons, 0);
		}
		else if (!strcmp (cmd, "wheel")) ws_input_pointer (a, b, buttons, c);
		else if (!strcmp (cmd, "key"))
		{
			std::string t;
			for (size_t k = 0; k < arg.size (); k++)
				if (arg[k] == '\\' && k + 1 < arg.size () && arg[k + 1] == 'n') { t += '\n'; k++; } else t += arg[k];
			ws_input_key (t.c_str ());
		}
		else if (!strcmp (cmd, "esc")) ws_input_key ("\x1b");
		else if (!strcmp (cmd, "enter")) ws_input_key ("\n");
		else if (!strcmp (cmd, "tab")) ws_input_key ("\t");
		else if (!strcmp (cmd, "bs")) ws_input_key ("\x7f");
		else if (!strcmp (cmd, "mods")) ws_input_mods ((unsigned) a);
		else if (!strcmp (cmd, "shot")) { display_write_bmp (arg); rlog ("screen written to %s", arg.c_str ()); }
		else if (!strcmp (cmd, "waitfile"))
		{
			for (int t = 0; t < 6000; t++)
			{
				FILE *f = fopen (arg.c_str (), "rb");
				if (f) { fclose (f); break; }
				std::this_thread::sleep_for (std::chrono::milliseconds (10));
			}
		}
		else if (!strcmp (cmd, "include"))
		{
			FILE *f = fopen (arg.c_str (), "rb");
			std::string more;
			char b[512];
			size_t n;
			while (f && (n = fread (b, 1, sizeof b, f)) > 0) more.append (b, n);
			if (f) fclose (f);
			for (auto &ch : more) if (ch == '\n' || ch == '\r') ch = ';';
			if (i > script.size ()) i = script.size ();
			script.insert (i, more + ";");
		}
		else if (cmd[0]) rlog ("--input: unknown step \"%s\"", step.c_str ());
	}
}

u8 *display_screen_memory (u64 *len) { *len = ALIGN_UP ((u64) s_Fb.n * 4); return (u8 *) s_Fb.p; }
void display_direct (bool on) { s_Direct = on; }

bool display_start (const DisplayOpts &o)
{
	s_Opts = o;
	s_W = o.w; s_H = o.h;
	s_Fb.n = (size_t) s_W * s_H;
	s_Fb.p = (u32 *) host_shared_alloc (ALIGN_UP ((u64) s_Fb.n * 4));
	for (size_t k = 0; k < s_Fb.n; k++) s_Fb.p[k] = 0x00202830;
#ifdef _WIN32
	if (!o.headless) std::thread (window_thread).detach ();
#endif
	if (!o.input.empty ()) std::thread (input_thread, o.input).detach ();
	if (!o.shot.empty ())
		std::thread ([o] {
			std::this_thread::sleep_for (std::chrono::milliseconds (o.shotAfterMs));
			if (display_write_bmp (o.shot)) rlog ("screen written to %s", o.shot.c_str ());
			fflush (stdout); fflush (stderr);
#ifdef _WIN32
			ExitProcess (0);
#else
			_Exit (0);
#endif
		}).detach ();
	return true;
}
