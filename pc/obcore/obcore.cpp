//
// obcore.cpp -- obcore.dll: Onyx BASIC on Windows. The same compiler (bascomp.cpp), VM
// (basvm.cpp) and screen (basscreen.h) as on Onyx, driven by OnyxBasic.exe (.NET) through a
// small C API:
//   * ob_check () compiles a source (the editor's syntax check); ob_words () lists the words.
//   * ob_run () runs a program on the calling thread (a worker thread of the .NET side): the
//     window, its picture and the controls are the .NET side's, reached through callbacks;
//     keys, the mouse and control events come back through ob_key / ob_keyheld / ob_mouse /
//     ob_event, ob_stop () ends the run.
//   * SD:/ is a folder of the PC (the program's files, CHAIN, OPEN ...); sound = waveOut.
// Strings are Latin-1 like on Onyx (the VM already turned BASIC's code page 437 into it);
// paths given to the .NET side are UTF-16.
//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "basic/basscreen.h"

#define OBAPI extern "C" __declspec (dllexport)

// ---- the .NET side ----------------------------------------------------------------------------------
struct ObCallbacks
{
	int  (*openWindow) (int w, int h, const char *title);
	void (*resizeWindow) (int w, int h);
	void (*present) (const unsigned *px, int w, int h, int sx, int sy);
	int  (*control) (int id, int kind, int x, int y, int w, int h, const char *text, int val);
	void (*setText) (int id, const char *s);
	int  (*getText) (int id, char *buf, int cap);
	int  (*getValue) (int id);
	void (*setValue) (int id, int v);
	void (*notify) (const char *title, const char *text);
	int  (*msgbox) (const char *title, const char *text, int buttons);
	int  (*clipboard) (char *buf, int cap);
	void (*setClipboard) (const char *s);
	int  (*fileDialog) (int save, const wchar_t *dir, const wchar_t *name, wchar_t *out, int cap);
};

// ---- sound: 16 voices mixed into waveOut (44.1 kHz, mono, 16 bits) ------------------------------------
namespace snd {
	enum { RATE = 44100, NBUF = 4, FRAMES = 1024, VOICES = 16 };
	struct Voice { double freq, phase; int wave, vol; unsigned lfsr; bool on; };
	static Voice v[VOICES];
	static CRITICAL_SECTION lock;
	static HWAVEOUT out = 0;
	static HANDLE thread = 0, ready = 0;
	static volatile bool quit = false;
	static WAVEHDR hdr[NBUF];
	static short buf[NBUF][FRAMES];

	static void mix (short *o)
	{
		EnterCriticalSection (&lock);
		for (int i = 0; i < FRAMES; i++)
		{
			double s = 0;
			for (int k = 0; k < VOICES; k++)
			{
				Voice &a = v[k];
				if (!a.on) continue;
				double p = a.phase, x;
				switch (a.wave)
				{
				case 1: x = sin (p * 6.283185307179586); break;			// sine
				case 2: x = p < 0.5 ? 4 * p - 1 : 3 - 4 * p; break;		// triangle
				case 3: x = 2 * p - 1; break;					// saw
				case 4: x = (a.lfsr & 1) ? 1 : -1; break;			// noise
				default: x = p < 0.5 ? 1 : -1; break;				// square
				}
				s += x * a.vol / 255.0 * 0.22;
				a.phase += a.freq / RATE;
				if (a.phase >= 1) { a.phase -= floor (a.phase); a.lfsr = (a.lfsr >> 1) ^ (-(int) (a.lfsr & 1) & 0xB400u); }
			}
			s = s > 1 ? 1 : s < -1 ? -1 : s;
			o[i] = (short) (s * 30000);
		}
		LeaveCriticalSection (&lock);
	}
	static DWORD WINAPI run (LPVOID)
	{
		while (!quit)
		{
			WaitForSingleObject (ready, 50);
			for (int b = 0; b < NBUF; b++)
				if (hdr[b].dwFlags & WHDR_DONE) { mix (buf[b]); hdr[b].dwFlags &= ~WHDR_DONE; waveOutWrite (out, &hdr[b], sizeof hdr[b]); }
		}
		return 0;
	}
	static bool open ()
	{
		if (out) return true;
		static bool init = false;
		if (!init) { InitializeCriticalSection (&lock); init = true; }
		WAVEFORMATEX f; memset (&f, 0, sizeof f);
		f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 1; f.nSamplesPerSec = RATE; f.wBitsPerSample = 16;
		f.nBlockAlign = 2; f.nAvgBytesPerSec = RATE * 2;
		ready = CreateEventW (0, FALSE, FALSE, 0);
		if (waveOutOpen (&out, WAVE_MAPPER, &f, (DWORD_PTR) ready, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { out = 0; return false; }
		for (int k = 0; k < VOICES; k++) { v[k].on = false; v[k].lfsr = 0xACE1u; }
		for (int b = 0; b < NBUF; b++)
		{
			memset (&hdr[b], 0, sizeof hdr[b]);
			hdr[b].lpData = (LPSTR) buf[b]; hdr[b].dwBufferLength = FRAMES * 2;
			waveOutPrepareHeader (out, &hdr[b], sizeof hdr[b]);
			mix (buf[b]); waveOutWrite (out, &hdr[b], sizeof hdr[b]);
		}
		quit = false;
		thread = CreateThread (0, 0, run, 0, 0, 0);
		return true;
	}
	static void close ()
	{
		if (!out) return;
		quit = true; SetEvent (ready);
		WaitForSingleObject (thread, 1000); CloseHandle (thread); thread = 0;
		waveOutReset (out);
		for (int b = 0; b < NBUF; b++) waveOutUnprepareHeader (out, &hdr[b], sizeof hdr[b]);
		waveOutClose (out); out = 0;
		CloseHandle (ready); ready = 0;
	}
	static void set (int voice, double freq, int wave, int vol)
	{
		if (!out) return;
		EnterCriticalSection (&lock);
		for (int k = 0; k < VOICES; k++)
		{
			if (voice >= 0 && k != voice) continue;
			if (freq <= 0) v[k].on = false;
			else { if (!v[k].on) v[k].phase = 0; v[k].freq = freq; v[k].wave = wave; v[k].vol = vol < 0 ? 0 : vol > 255 ? 255 : vol; v[k].on = true; }
		}
		LeaveCriticalSection (&lock);
	}
}

// ---- paths: SD:/ -> the SD folder; relative -> the current directory ------------------------------------
static int wlen (const wchar_t *s) { int n = 0; while (s && s[n]) n++; return n; }
static void wcpy (wchar_t *d, const wchar_t *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

// ---- the host ------------------------------------------------------------------------------------------
class PcHost : public bas::ScreenHost
{
public:
	ObCallbacks cb;
	wchar_t sd[MAX_PATH], cwd[MAX_PATH];
	char args[512];
	volatile bool stop;
	bool dirtyPic; unsigned lastPresent;
	volatile bool held[0x200];
	int nctl, audio;

	PcHost () : stop (false), dirtyPic (false), lastPresent (0), nctl (0), audio (0)
	{
		sd[0] = cwd[0] = 0; args[0] = 0;
		for (int i = 0; i < 0x200; i++) held[i] = false;
	}

	// SD:/a/b -> <sd>\a\b; C:\x or \\srv\x as they are; other -> <cwd>\name. Latin-1 -> UTF-16.
	void toWin (const char *p, wchar_t *o, int cap)
	{
		int n = 0;
		auto put = [&] (wchar_t c) { if (n < cap - 1) o[n++] = c; };
		auto puts = [&] (const wchar_t *s) { for (; *s; s++) put (*s); };
		bool sdp = (p[0] == 'S' || p[0] == 's') && (p[1] == 'D' || p[1] == 'd') && p[2] == ':';
		if (sdp) { puts (sd); p += 3; while (*p == '/' || *p == '\\') p++; if (n && o[n - 1] != '\\') put ('\\'); }
		else if (!((p[0] && p[1] == ':') || (p[0] == '\\' && p[1] == '\\')))
		{ puts (cwd); if (n && o[n - 1] != '\\') put ('\\'); }
		for (; *p; p++) put (*p == '/' ? L'\\' : (wchar_t) (unsigned char) *p);
		o[n] = 0;
	}
	// A path of the PC -> SD:/... when it is in the SD folder (else as it is), UTF-16 -> Latin-1.
	void fromWin (const wchar_t *w, char *o, int cap)
	{
		int n = 0, ls = wlen (sd);
		auto put = [&] (char c) { if (n < cap - 1) o[n++] = c; };
		bool in = ls > 0 && _wcsnicmp (w, sd, ls) == 0 && (w[ls] == '\\' || w[ls] == 0);
		if (in) { const char *p = "SD:/"; while (*p) put (*p++); w += ls; while (*w == '\\') w++; }
		for (; *w; w++) put (*w == '\\' && in ? '/' : *w < 256 ? (char) *w : '?');
		o[n] = 0;
	}

	// ---- the platform ------------------------------------------------------------------------------------
	bool openWindow (int w, int h) override { return cb.openWindow (w, h, title[0] ? title : "Onyx BASIC") != 0; }
	void resizeWindow (int w, int h) override { cb.resizeWindow (w, h); }
	void markDirty () override { dirtyPic = true; }
	void present (bool force) override
	{
		unsigned now = nowMs ();
		if (!dirtyPic || (!force && now - lastPresent < 15)) return;	// ~60 Hz at most
		lastPresent = now; dirtyPic = false;
		cb.present (visible (), W, H, sx, sy);
	}
	void pumpEvents () override {}		// (the .NET side pushes the input from its own thread)
	bool stopRequested () override { return stop; }
	unsigned nowMs () override { return timeGetTime (); }
	void sleepRaw (int ms) override { Sleep (ms > 0 ? (DWORD) ms : 1); }
	bool keyHeld (int key) override { return key > 0 && key < 0x200 && held[key]; }
	bool soundReady () override { if (audio == 0) audio = snd::open () ? 1 : -1; return audio == 1; }
	int note (int voice, double freq, int wave, int vol) override
	{
		if (freq > 0 && !soundReady ()) return -1;
		snd::set (voice, freq, wave, vol);
		return 0;
	}
	void endSound () override { if (audio == 1) { snd::set (-1, 0, 0, 0); Sleep (30); snd::close (); } audio = 0; }

	double timer () override
	{
		SYSTEMTIME t; GetLocalTime (&t);
		return t.wHour * 3600.0 + t.wMinute * 60 + t.wSecond + t.wMilliseconds / 1000.0;
	}
	static void two (char *p, int v) { p[0] = (char) ('0' + v / 10 % 10); p[1] = (char) ('0' + v % 10); }
	void date (char *o) override
	{
		SYSTEMTIME t; GetLocalTime (&t);
		two (o, t.wMonth); o[2] = '-'; two (o + 3, t.wDay); o[5] = '-'; two (o + 6, t.wYear / 100); two (o + 8, t.wYear % 100); o[10] = 0;
	}
	void time (char *o) override
	{
		SYSTEMTIME t; GetLocalTime (&t);
		two (o, t.wHour); o[2] = ':'; two (o + 3, t.wMinute); o[5] = ':'; two (o + 6, t.wSecond); o[8] = 0;
	}
	unsigned seed () override { return timeGetTime () * 2654435761u; }

	// ---- controls, dialogs, clipboard (the .NET side) ------------------------------------------------------
	int control (int kind, int x, int y, int w, int h, const char *text, int val) override
	{
		if (!ensureWindow ()) return 0;
		int id = nctl + 1;
		if (!cb.control (id, kind, x, y, w, h, text, val)) return 0;
		nctl = id;
		return id;
	}
	void setText (int id, const char *s) override { cb.setText (id, s); }
	int getText (int id, char *buf, int cap) override { buf[0] = 0; int n = cb.getText (id, buf, cap); if (n < 0) n = 0; if (n > cap - 1) n = cap - 1; buf[n] = 0; return n; }
	int getValue (int id) override { return cb.getValue (id); }
	void setValue (int id, int v) override { cb.setValue (id, v); }
	void notify (const char *t, const char *m) override { cb.notify (t, m); }
	int msgbox (const char *t, const char *m, int b) override { present (true); return cb.msgbox (t, m, b); }
	int clipboard (char *buf, int cap) override { buf[0] = 0; int n = cb.clipboard (buf, cap); if (n < 0) n = 0; if (n > cap - 1) n = cap - 1; buf[n] = 0; return n; }
	void setClipboard (const char *s) override { cb.setClipboard (s); }
	bool fileDialog (bool save, const char *dir, char *o, int cap) override
	{
		wchar_t wd[MAX_PATH], wn[MAX_PATH], wo[MAX_PATH];
		toWin (dir[0] ? dir : "SD:/", wd, MAX_PATH);
		int i = 0; for (; o[i] && i < MAX_PATH - 1; i++) wn[i] = (unsigned char) o[i];
		wn[i] = 0;
		wo[0] = 0;
		if (!cb.fileDialog (save ? 1 : 0, wd, wn, wo, MAX_PATH) || !wo[0]) return false;
		fromWin (wo, o, cap);
		return true;
	}
	bool launch (const char *) override { return false; }		// (Onyx apps: not on a PC)
	bool exec (const char *, const char *) override { return false; }
	// SHELL "command": cmd.exe in the current directory, its output on the screen (the console's
	// code page -- 850 / 437 -- is close to BASIC's).
	int shell (const char *cmd) override
	{
		if (!cmd[0]) return -1;
		wchar_t line[1200]; int n = 0;
		const wchar_t *pre = L"cmd.exe /c ";
		for (; *pre; pre++) line[n++] = *pre;
		for (const char *p = cmd; *p && n < 1190; p++) line[n++] = (unsigned char) *p;
		line[n] = 0;
		SECURITY_ATTRIBUTES sa = { sizeof sa, 0, TRUE };
		HANDLE rd, wr;
		if (!CreatePipe (&rd, &wr, &sa, 0)) return -1;
		SetHandleInformation (rd, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW si; memset (&si, 0, sizeof si); si.cb = sizeof si;
		si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle (STD_INPUT_HANDLE);
		PROCESS_INFORMATION pi;
		if (!CreateProcessW (0, line, 0, 0, TRUE, CREATE_NO_WINDOW, 0, cwd[0] ? cwd : 0, &si, &pi)) { CloseHandle (rd); CloseHandle (wr); return -1; }
		CloseHandle (wr);
		char b[512]; DWORD got;
		while (ReadFile (rd, b, sizeof b, &got, 0) && got > 0)
		{
			int m = 0; for (DWORD i = 0; i < got; i++) if (b[i] != '\r') b[m++] = b[i];
			out (b, m); present (true);
			if (stopped ()) break;
		}
		CloseHandle (rd);
		WaitForSingleObject (pi.hProcess, 5000);
		DWORD rc = 0; GetExitCodeProcess (pi.hProcess, &rc);
		CloseHandle (pi.hProcess); CloseHandle (pi.hThread);
		return (int) rc;
	}

	// ---- files -------------------------------------------------------------------------------------------
	char *load (const char *path, int *len) override
	{
		*len = 0;
		wchar_t w[MAX_PATH]; toWin (path, w, MAX_PATH);
		FILE *f = _wfopen (w, L"rb");
		if (!f) return 0;
		fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
		if (n < 0) n = 0;
		char *b = new char[n + 1];
		size_t r = fread (b, 1, (size_t) n, f); fclose (f);
		*len = (int) r; b[r] = 0;
		return b;
	}
	bool save (const char *path, const char *d, int n) override
	{
		wchar_t w[MAX_PATH]; toWin (path, w, MAX_PATH);
		FILE *f = _wfopen (w, L"wb");
		if (!f) return false;
		bool ok = fwrite (d, 1, (size_t) n, f) == (size_t) n;
		return fclose (f) == 0 && ok;
	}
	bool remove (const char *p) override
	{
		wchar_t w[MAX_PATH]; toWin (p, w, MAX_PATH);
		return DeleteFileW (w) || RemoveDirectoryW (w);
	}
	bool rename (const char *a, const char *b) override
	{
		wchar_t wa[MAX_PATH], wb[MAX_PATH]; toWin (a, wa, MAX_PATH); toWin (b, wb, MAX_PATH);
		return MoveFileW (wa, wb) != 0;
	}
	bool makeDir (const char *p) override { wchar_t w[MAX_PATH]; toWin (p, w, MAX_PATH); return CreateDirectoryW (w, 0) != 0; }
	bool exists (const char *p) override { wchar_t w[MAX_PATH]; toWin (p, w, MAX_PATH); return GetFileAttributesW (w) != INVALID_FILE_ATTRIBUTES; }
	bool chdir (const char *p) override
	{
		wchar_t w[MAX_PATH]; toWin (p, w, MAX_PATH);
		DWORD a = GetFileAttributesW (w);
		if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return false;
		wchar_t full[MAX_PATH];
		if (!GetFullPathNameW (w, MAX_PATH, full, 0)) return false;
		int n = wlen (full); while (n > 3 && full[n - 1] == '\\') full[--n] = 0;
		wcpy (cwd, full, MAX_PATH);
		return true;
	}
	int listDir (const char *dir, int index, char *o, int cap) override
	{
		o[0] = 0;
		wchar_t w[MAX_PATH]; toWin (dir[0] ? dir : ".", w, MAX_PATH - 3);
		int n = wlen (w); if (n && w[n - 1] != '\\') w[n++] = '\\';
		w[n++] = '*'; w[n] = 0;
		WIN32_FIND_DATAW e;
		HANDLE h = FindFirstFileW (w, &e);
		if (h == INVALID_HANDLE_VALUE) return 0;
		int i = 0;
		do
		{
			if (e.cFileName[0] == '.') continue;
			if (i++ == index)
			{
				int k = 0;
				for (const wchar_t *c = e.cFileName; *c && k < cap - 2; c++) o[k++] = *c < 256 ? (char) *c : '?';
				if (e.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) o[k++] = '/';
				o[k] = 0;
				break;
			}
		} while (FindNextFileW (h, &e));
		FindClose (h);
		return (int) strlen (o);
	}
	const char *command () override { return args; }
};

static PcHost *volatile g_host = 0;

// ---- the API ---------------------------------------------------------------------------------------------
static void setErr (const bas::Error &e, int *line, char *msg, int cap)
{
	if (line) *line = e.line;
	if (msg && cap > 0) { int i = 0; for (; e.msg[i] && i < cap - 1; i++) msg[i] = e.msg[i]; msg[i] = 0; }
}

// Compile only: 0 = fine, else 1 + the line and the message.
OBAPI int ob_check (const char *src, int *line, char *msg, int cap)
{
	bas::Error e; e.line = 0; e.msg[0] = 0;
	bas::Program *p = bas::compile (src, &e);
	if (!p) { setErr (e, line, msg, cap); return 1; }
	bas::destroy (p);
	if (line) *line = 0;
	if (msg && cap > 0) msg[0] = 0;
	return 0;
}

// The language's words, space-separated (keywords, then the built-in functions).
OBAPI int ob_words (char *buf, int cap) { return bas::wordList (buf, cap); }

// Run a program (its source, Latin-1). sd: the folder SD:/ stands for; cwd: the current
// directory (the program's folder); title: the window's. 0 = ended, 2 = syntax error,
// 3 = runtime error (line + message).
OBAPI int ob_run (const char *src, const wchar_t *sd, const wchar_t *cwd, const char *args, const char *title,
		  const ObCallbacks *cb, int *line, char *msg, int cap)
{
	if (line) *line = 0;
	if (msg && cap > 0) msg[0] = 0;
	bas::Error e; e.line = 0; e.msg[0] = 0;
	bas::Program *p = bas::compile (src, &e);
	if (!p) { setErr (e, line, msg, cap); return 2; }
	PcHost *h = new PcHost;
	h->cb = *cb;
	wcpy (h->sd, sd, MAX_PATH);
	{ int n = wlen (h->sd); while (n > 3 && h->sd[n - 1] == '\\') h->sd[--n] = 0; }
	wcpy (h->cwd, cwd && cwd[0] ? cwd : sd, MAX_PATH);
	{ int i = 0; for (; args && args[i] && i < (int) sizeof h->args - 1; i++) h->args[i] = args[i]; h->args[i] = 0; }
	{ int i = 0; for (; title && title[i] && i < (int) sizeof h->title - 1; i++) h->title[i] = title[i]; h->title[i] = 0; }
	timeBeginPeriod (1);
	g_host = h;
	int r = bas::run (p, *h, &e);
	g_host = 0;
	timeEndPeriod (1);
	bas::destroy (p);
	if (r) setErr (e, line, msg, cap);
	h->endSound ();
	delete h;
	return r ? 3 : 0;
}

// Input from the window (any thread): a key (Latin-1 character or K_* code; 3 = Ctrl+C), a
// key held down / released, the mouse in the program's pixels (x < 0: buttons only), a
// control's event; ob_stop: the window was closed.
OBAPI void ob_key (int k) { PcHost *h = g_host; if (h) h->pushKey (k); }
OBAPI void ob_keyheld (int k, int down) { PcHost *h = g_host; if (h && k > 0 && k < 0x200) h->held[k] = down != 0; }
OBAPI void ob_mouse (int x, int y, int b) { PcHost *h = g_host; if (h) h->setMouse (x, y, b); }
OBAPI void ob_event (int id) { PcHost *h = g_host; if (h) h->pushEvent (id); }
OBAPI void ob_stop (void) { PcHost *h = g_host; if (h) h->stop = true; }
