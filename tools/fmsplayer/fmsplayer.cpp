//
// fmsplayer.cpp -- FM Song player for Windows: plays .FMS files (FM Song / Onyx fmtracker)
// with EXACTLY the Onyx code: the file format of user/Apps/fmtracker/fms.h and the FM
// synthesizer of kernel/sys/sound.cpp (compiled in its PC mode), out through waveOut.
//
//   * Open... (or drop files on the window, or give them on the command line); the other
//     .FMS files of the same folder fill the playlist; double-click one to play it.
//   * Play / Pause, Stop, Previous / Next; "Loop song" repeats the song, otherwise the
//     next one starts. The window shows the song's title / author / comment, the position
//     (pattern, row, time) and the note of each of the 8 channels.
//
// Build (MinGW-w64): see build.sh -- one static .exe, no DLL.
//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

// ---- the Onyx synthesizer, on a PC --------------------------------------------------------------
typedef int16_t s16; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;
typedef uint64_t u64; typedef int64_t s64; typedef bool boolean;
#undef TRUE
#undef FALSE
#define TRUE true
#define FALSE false
#define IRQ_LEVEL 0
static CRITICAL_SECTION g_synthLock;
struct CSpinLock { CSpinLock (int) {} void Acquire () { EnterCriticalSection (&g_synthLock); } void Release () { LeaveCriticalSection (&g_synthLock); } };
#define SND_RATE	44100
#define SND_VOICES	16
#define SOUND_HOST_TEST
#include "../../kernel/sys/sound.cpp"
#include "../../user/Apps/fmtracker/fms.h"

#define PID 1
#define BUF_FRAMES 1024
#define NBUF 4

// ---- the song + sequencer (sample-accurate, runs on the audio thread) -------------------------
static FmsSong g_song;
static bool g_loaded = false, g_playing = false, g_paused = false, g_loop = true, g_ended = false;
static int g_pat = 0, g_row = 0, g_shownPat = 0, g_shownRow = 0;
static unsigned g_rowLeft = 0;				// frames left in the current row
static unsigned long long g_frames = 0;			// frames played (time display)
static unsigned char g_chanNote[FMS_CH];		// the note sounding per channel (display)
static CRITICAL_SECTION g_seqLock;

static void upload_all ()
{
	for (int c = 0; c < FMS_CH; c++)
	{
		struct kapi_fm_instrument k;
		fms_to_kapi (&g_song.ins[c], &k);
		SoundInstrument (PID, c, &k);
	}
}
static void play_row ()
{
	FmsPattern &p = g_song.pat[g_pat];
	for (int c = 0; c < FMS_CH; c++)
	{
		unsigned char v = p.n[c * p.rows + g_row];
		if (v & 128) { SoundStop (PID, c); g_chanNote[c] = 0; }
		else if ((v & 7) == 0) continue;
		else if (p.mute[c]) { SoundStop (PID, c); g_chanNote[c] = 0; }
		else { SoundStart (PID, c, fms_note_mhz (v), SOUND_FM, 220); g_chanNote[c] = v; }
	}
	g_shownPat = g_pat; g_shownRow = g_row;
}
// Next row; false at the end of the song (no loop).
static bool advance ()
{
	if (++g_row >= g_song.pat[g_pat].rows)
	{
		g_row = 0;
		if (++g_pat >= g_song.npat)
		{
			g_pat = 0;
			if (!g_loop) return false;
		}
	}
	return true;
}
static void fill (s16 *out, unsigned frames)
{
	EnterCriticalSection (&g_seqLock);
	unsigned done = 0;
	while (done < frames)
	{
		if (g_playing && !g_paused && g_loaded)
		{
			if (g_rowLeft == 0)
			{
				play_row ();
				g_rowLeft = (unsigned) g_song.pat[g_pat].speed * (SND_RATE / 20);
				if (!advance ()) { g_ended = true; }
			}
			unsigned n = frames - done < g_rowLeft ? frames - done : g_rowLeft;
			s_Lock.Acquire (); Render (out + done * 2, n); s_Lock.Release ();
			g_rowLeft -= n; done += n; g_frames += n;
			if (g_ended && g_rowLeft == 0) { g_playing = false; SoundStop (PID, -1); }
		}
		else { s_Lock.Acquire (); Render (out + done * 2, frames - done); s_Lock.Release (); done = frames; }	// (releases fade out)
	}
	LeaveCriticalSection (&g_seqLock);
}

// ---- waveOut ---------------------------------------------------------------------------------
static HWAVEOUT g_wo; static WAVEHDR g_hdr[NBUF]; static s16 g_buf[NBUF][BUF_FRAMES * 2];
static HANDLE g_event; static volatile bool g_quit = false;
static DWORD WINAPI audio_thread (LPVOID)
{
	for (int i = 0; i < NBUF; i++) { fill (g_buf[i], BUF_FRAMES); waveOutWrite (g_wo, &g_hdr[i], sizeof (WAVEHDR)); }
	while (!g_quit)
	{
		WaitForSingleObject (g_event, 100);
		for (int i = 0; i < NBUF; i++)
			if (g_hdr[i].dwFlags & WHDR_DONE)
			{
				fill (g_buf[i], BUF_FRAMES);
				waveOutWrite (g_wo, &g_hdr[i], sizeof (WAVEHDR));
			}
	}
	return 0;
}
static bool audio_open ()
{
	WAVEFORMATEX f = {};
	f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 2; f.nSamplesPerSec = SND_RATE; f.wBitsPerSample = 16;
	f.nBlockAlign = 4; f.nAvgBytesPerSec = SND_RATE * 4;
	g_event = CreateEvent (0, FALSE, FALSE, 0);
	if (waveOutOpen (&g_wo, WAVE_MAPPER, &f, (DWORD_PTR) g_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return false;
	for (int i = 0; i < NBUF; i++)
	{
		g_hdr[i] = {}; g_hdr[i].lpData = (LPSTR) g_buf[i]; g_hdr[i].dwBufferLength = sizeof g_buf[i];
		waveOutPrepareHeader (g_wo, &g_hdr[i], sizeof (WAVEHDR));
	}
	CreateThread (0, 0, audio_thread, 0, 0, 0);
	return true;
}

// ---- files / playlist --------------------------------------------------------------------------------
#define MAXLIST 512
static char g_dir[MAX_PATH] = "", g_list[MAXLIST][MAX_PATH]; static int g_nlist = 0, g_cur = -1;
static HWND g_wnd, g_lb, g_info, g_view, g_loopBox, g_playBtn;

static bool load_file (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f) return false;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *b = (unsigned char *) malloc (n + 1);
	n = (long) fread (b, 1, n, f); fclose (f);
	static FmsSong s; s.npat = 0;
	bool ok = fms_parse (b, (int) n, &s);
	free (b);
	if (!ok) { fms_clear (&s); return false; }
	EnterCriticalSection (&g_seqLock);
	SoundStop (PID, -1);
	fms_clear (&g_song);
	g_song = s; s.npat = 0;
	g_loaded = true; g_playing = false; g_paused = false; g_ended = false;
	g_pat = g_row = 0; g_rowLeft = 0; g_frames = 0;
	memset (g_chanNote, 0, sizeof g_chanNote);
	upload_all ();
	LeaveCriticalSection (&g_seqLock);
	return true;
}
static int ci_cmp (const char *a, const char *b) { return _stricmp (a, b); }
static void scan_folder (const char *path)
{
	char dir[MAX_PATH]; strncpy (dir, path, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
	char *slash = strrchr (dir, '\\'); if (!slash) slash = strrchr (dir, '/');
	if (slash) *slash = 0; else strcpy (dir, ".");
	strcpy (g_dir, dir);
	char pat[MAX_PATH + 8]; snprintf (pat, sizeof pat, "%s\\*.fms", dir);
	WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA (pat, &fd);
	g_nlist = 0;
	if (h != INVALID_HANDLE_VALUE)
	{
		do { if (g_nlist < MAXLIST) snprintf (g_list[g_nlist++], MAX_PATH, "%s\\%s", dir, fd.cFileName); } while (FindNextFileA (h, &fd));
		FindClose (h);
	}
	qsort (g_list, g_nlist, MAX_PATH, [] (const void *a, const void *b) { return ci_cmp ((const char *) a, (const char *) b); });
	SendMessage (g_lb, LB_RESETCONTENT, 0, 0);
	for (int i = 0; i < g_nlist; i++)
	{
		const char *base = strrchr (g_list[i], '\\'); base = base ? base + 1 : g_list[i];
		SendMessageA (g_lb, LB_ADDSTRING, 0, (LPARAM) base);
	}
}
static void update_info ()
{
	char t[400];
	if (!g_loaded) snprintf (t, sizeof t, "Open an .FMS song (File button), or drop one here.");
	else snprintf (t, sizeof t, "%s\r\n%s%s\r\n%s", g_song.title[0] ? g_song.title : "(untitled)",
		       g_song.author[0] ? "by " : "", g_song.author, g_song.comment);
	SetWindowTextA (g_info, t);
	SetWindowTextA (g_playBtn, g_playing && !g_paused ? "Pause" : "Play");
}
static bool play_index (int i)
{
	if (i < 0 || i >= g_nlist) return false;
	if (!load_file (g_list[i])) { MessageBoxA (g_wnd, "This is not an FM Song (.FMS) file.", "FM Song Player", MB_ICONWARNING); return false; }
	g_cur = i;
	SendMessage (g_lb, LB_SETCURSEL, i, 0);
	EnterCriticalSection (&g_seqLock); g_playing = true; LeaveCriticalSection (&g_seqLock);
	update_info ();
	return true;
}
static void open_path (const char *path)
{
	scan_folder (path);
	for (int i = 0; i < g_nlist; i++) if (!ci_cmp (g_list[i], path)) { play_index (i); return; }
	// not a .fms name: try it anyway
	if (load_file (path)) { EnterCriticalSection (&g_seqLock); g_playing = true; LeaveCriticalSection (&g_seqLock); update_info (); }
}

// ---- the window --------------------------------------------------------------------------------------
enum { ID_OPEN = 100, ID_PLAY, ID_STOP, ID_PREV, ID_NEXT, ID_LOOP, ID_LIST, ID_TIMER };
static HFONT g_font, g_mono;

static void on_command (int id, int code)
{
	switch (id)
	{
	case ID_OPEN:
	{
		char p[MAX_PATH] = "";
		OPENFILENAMEA o = {}; o.lStructSize = sizeof o; o.hwndOwner = g_wnd;
		o.lpstrFilter = "FM Song (*.fms)\0*.fms\0All files\0*.*\0"; o.lpstrFile = p; o.nMaxFile = MAX_PATH;
		o.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		if (GetOpenFileNameA (&o)) open_path (p);
		break;
	}
	case ID_PLAY:
		EnterCriticalSection (&g_seqLock);
		if (!g_loaded) { LeaveCriticalSection (&g_seqLock); on_command (ID_OPEN, 0); return; }
		if (!g_playing) { g_playing = true; g_paused = false; g_ended = false; }
		else { g_paused = !g_paused; if (g_paused) SoundStop (PID, -1); }
		LeaveCriticalSection (&g_seqLock);
		update_info ();
		break;
	case ID_STOP:
		EnterCriticalSection (&g_seqLock);
		g_playing = false; g_paused = false; g_pat = g_row = 0; g_rowLeft = 0; g_frames = 0;
		SoundStop (PID, -1); memset (g_chanNote, 0, sizeof g_chanNote);
		LeaveCriticalSection (&g_seqLock);
		update_info ();
		break;
	case ID_PREV: if (g_nlist) play_index (g_cur > 0 ? g_cur - 1 : g_nlist - 1); break;
	case ID_NEXT: if (g_nlist) play_index (g_cur + 1 < g_nlist ? g_cur + 1 : 0); break;
	case ID_LOOP: g_loop = SendMessage (g_loopBox, BM_GETCHECK, 0, 0) == BST_CHECKED; break;
	case ID_LIST: if (code == LBN_DBLCLK) play_index ((int) SendMessage (g_lb, LB_GETCURSEL, 0, 0)); break;
	}
}

static void paint_view (HDC dc, RECT r)
{
	HBRUSH bg = CreateSolidBrush (RGB (16, 24, 32)); FillRect (dc, &r, bg); DeleteObject (bg);
	SetBkMode (dc, TRANSPARENT);
	HFONT old = (HFONT) SelectObject (dc, g_mono);
	int cw = (r.right - r.left) / FMS_CH;
	EnterCriticalSection (&g_seqLock);
	for (int c = 0; c < FMS_CH; c++)
	{
		int x = r.left + c * cw;
		bool on = g_playing && !g_paused && g_chanNote[c];
		RECT cell = { x + 2, r.top + 2, x + cw - 2, r.bottom - 2 };
		HBRUSH b = CreateSolidBrush (on ? RGB (48, 90, 48) : RGB (32, 44, 58)); FillRect (dc, &cell, b); DeleteObject (b);
		char name[16]; snprintf (name, sizeof name, "%d %s", c + 1, g_loaded ? g_song.ins[c].name : "");
		SetTextColor (dc, RGB (200, 210, 225)); TextOutA (dc, x + 6, r.top + 6, name, (int) strlen (name));
		char n[4] = ""; if (on) fms_note_text (g_chanNote[c], n);
		SetTextColor (dc, RGB (240, 240, 160)); TextOutA (dc, x + 6, r.top + 28, n, (int) strlen (n));
	}
	char pos[120];
	unsigned secs = (unsigned) (g_frames / SND_RATE);
	if (g_loaded) snprintf (pos, sizeof pos, "Pattern %d/%d   row %d/%d   %u:%02u%s", g_shownPat + 1, g_song.npat, g_shownRow,
				g_song.pat[g_shownPat].rows, secs / 60, secs % 60, g_paused ? "   (paused)" : g_playing ? "" : "   (stopped)");
	else pos[0] = 0;
	LeaveCriticalSection (&g_seqLock);
	SetTextColor (dc, RGB (160, 176, 192)); TextOutA (dc, r.left + 6, r.bottom - 22, pos, (int) strlen (pos));
	SelectObject (dc, old);
}

static LRESULT CALLBACK wndproc (HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m)
	{
	case WM_COMMAND: on_command (LOWORD (w), HIWORD (w)); return 0;
	case WM_DROPFILES:
	{
		char p[MAX_PATH];
		if (DragQueryFileA ((HDROP) w, 0, p, MAX_PATH)) open_path (p);
		DragFinish ((HDROP) w);
		return 0;
	}
	case WM_TIMER:
		InvalidateRect (g_view, 0, FALSE);
		if (g_ended && !g_playing && !g_loop && g_nlist && g_cur >= 0) { g_ended = false; on_command (ID_NEXT, 0); }
		update_info ();
		return 0;
	case WM_DESTROY: PostQuitMessage (0); return 0;
	}
	return DefWindowProcA (h, m, w, l);
}
static LRESULT CALLBACK viewproc (HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_PAINT)
	{
		PAINTSTRUCT ps; HDC dc = BeginPaint (h, &ps);
		RECT r; GetClientRect (h, &r);
		HDC mem = CreateCompatibleDC (dc); HBITMAP bmp = CreateCompatibleBitmap (dc, r.right, r.bottom);
		HGDIOBJ ob = SelectObject (mem, bmp);
		paint_view (mem, r);
		BitBlt (dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
		SelectObject (mem, ob); DeleteObject (bmp); DeleteDC (mem);
		EndPaint (h, &ps);
		return 0;
	}
	if (m == WM_ERASEBKGND) return 1;
	return DefWindowProcA (h, m, w, l);
}

int WINAPI WinMain (HINSTANCE inst, HINSTANCE, LPSTR cmd, int show)
{
	InitializeCriticalSection (&g_synthLock);
	InitializeCriticalSection (&g_seqLock);
	fms_new (&g_song); fms_clear (&g_song);
	SoundAcquire (PID);
	if (!audio_open ()) { MessageBoxA (0, "Cannot open the sound output.", "FM Song Player", MB_ICONERROR); return 1; }

	WNDCLASSA wc = {}; wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "OnyxFmsPlayer";
	wc.hCursor = LoadCursor (0, IDC_ARROW); wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
	wc.hIcon = LoadIcon (0, IDI_APPLICATION);
	RegisterClassA (&wc);
	WNDCLASSA vc = {}; vc.lpfnWndProc = viewproc; vc.hInstance = inst; vc.lpszClassName = "OnyxFmsView"; vc.hCursor = LoadCursor (0, IDC_ARROW);
	RegisterClassA (&vc);
	g_wnd = CreateWindowExA (WS_EX_ACCEPTFILES, "OnyxFmsPlayer", "FM Song Player (Onyx)", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
				 CW_USEDEFAULT, CW_USEDEFAULT, 720, 520, 0, 0, inst, 0);
	g_font = CreateFontA (-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
	g_mono = CreateFontA (-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
	auto btn = [&] (const char *t, int id, int x, int y, int w) {
		HWND b = CreateWindowA ("BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, 30, g_wnd, (HMENU) (INT_PTR) id, inst, 0);
		SendMessage (b, WM_SETFONT, (WPARAM) g_font, TRUE); return b; };
	btn ("Open...", ID_OPEN, 10, 10, 90);
	g_playBtn = btn ("Play", ID_PLAY, 110, 10, 80);
	btn ("Stop", ID_STOP, 196, 10, 80);
	btn ("<< Prev", ID_PREV, 290, 10, 80);
	btn ("Next >>", ID_NEXT, 376, 10, 80);
	g_loopBox = CreateWindowA ("BUTTON", "Loop song", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 472, 14, 110, 22, g_wnd, (HMENU) ID_LOOP, inst, 0);
	SendMessage (g_loopBox, WM_SETFONT, (WPARAM) g_font, TRUE);
	SendMessage (g_loopBox, BM_SETCHECK, BST_CHECKED, 0);
	g_info = CreateWindowA ("STATIC", "", WS_CHILD | WS_VISIBLE, 10, 50, 690, 58, g_wnd, 0, inst, 0);
	SendMessage (g_info, WM_SETFONT, (WPARAM) g_font, TRUE);
	g_view = CreateWindowA ("OnyxFmsView", "", WS_CHILD | WS_VISIBLE, 10, 112, 690, 84, g_wnd, 0, inst, 0);
	g_lb = CreateWindowA ("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 10, 206, 690, 270, g_wnd, (HMENU) ID_LIST, inst, 0);
	SendMessage (g_lb, WM_SETFONT, (WPARAM) g_font, TRUE);
	update_info ();
	ShowWindow (g_wnd, show);
	SetTimer (g_wnd, ID_TIMER, 50, 0);

	// command line: a file to play
	if (cmd && cmd[0])
	{
		char p[MAX_PATH]; const char *c = cmd; int k = 0;
		if (*c == '"') { c++; while (*c && *c != '"' && k < MAX_PATH - 1) p[k++] = *c++; }
		else while (*c && k < MAX_PATH - 1) p[k++] = *c++;
		p[k] = 0;
		if (p[0]) open_path (p);
	}
	MSG msg;
	while (GetMessage (&msg, 0, 0, 0)) { TranslateMessage (&msg); DispatchMessage (&msg); }
	g_quit = true;
	waveOutReset (g_wo);
	return 0;
}
