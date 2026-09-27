//
// nemucore.cpp -- the native half of NintendoEMU (nemucore.dll): the emulator cores of Onyx
// (user/gb, gba, nes, snes, n64, gc -- the very same sources as the Onyx apps) behind a small C
// API for the .NET front end, plus what Windows gives them: the sound (waveOut), the pads
// (XInput) and the files (UTF-8 paths). Portable apart from that: it also builds on Linux for
// the test (tools/tests/run_nemu_test.sh).
//
//   void *h = ne_open (path, 44100, err, n);   a ROM / disc image (the system from its extension)
//   the game's thread: ne_set_keys (h, vk[256]); ne_run_frame (h, draw); ne_audio (h, pcm, n)
//   the window:        ne_video (h, px, cap, &w, &h) (the last finished picture), ne_aspect (h)
//   ne_close (h)                                the battery save written beside the ROM (<rom>.sav)
//
// The Nintendo 64 and GameCube pictures are 3D: each frame's triangles and textures (what the
// Pi's GPU draws on Onyx) are drawn here by the BASIC 3D's software renderer (user/basic/bas3d.h),
// ne_set_scale times the console's resolution. The GameCube's CPU is interpreted (the JIT is
// AArch64 code), so it runs slowly.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
#define NE_API extern "C" __declspec(dllexport)
#else
#include <pthread.h>
#include <unistd.h>
#define NE_API extern "C" __attribute__ ((visibility ("default")))
#endif
#include "gb/gb.h"
#include "gba/gba.h"
#include "nes/nes.h"
#include "snes/snes.h"
#include "n64/n64.h"
#include "gc/gc.h"
#include "basic/bas3d.h"

enum { NE_NONE, NE_GB, NE_GBC, NE_GBA, NE_NES, NE_SNES, NE_N64, NE_GC };

// ---- files (UTF-8 paths) -------------------------------------------------------------------------
static FILE *u8open (const char *path, const char *mode)
{
#ifdef _WIN32
	wchar_t wp[1024], wm[8];
	if (!MultiByteToWideChar (CP_UTF8, 0, path, -1, wp, 1024)) return 0;
	MultiByteToWideChar (CP_UTF8, 0, mode, -1, wm, 8);
	return _wfopen (wp, wm);
#else
	return fopen (path, mode);
#endif
}
static bool u8seek (FILE *f, unsigned long long off)
{
#ifdef _WIN32
	return _fseeki64 (f, (long long) off, SEEK_SET) == 0;
#else
	return fseeko (f, (off_t) off, SEEK_SET) == 0;
#endif
}
static long long u8size (FILE *f)
{
#ifdef _WIN32
	_fseeki64 (f, 0, SEEK_END); long long n = _ftelli64 (f); _fseeki64 (f, 0, SEEK_SET); return n;
#else
	fseeko (f, 0, SEEK_END); long long n = (long long) ftello (f); fseeko (f, 0, SEEK_SET); return n;
#endif
}
static unsigned char *slurp (const char *path, int *n, int max)
{
	FILE *f = u8open (path, "rb");
	if (!f) return 0;
	long long sz = u8size (f);
	if (sz <= 0 || sz > max) { fclose (f); return 0; }
	unsigned char *d = (unsigned char *) malloc ((size_t) sz + 16);
	if (d && fread (d, 1, (size_t) sz, f) != (size_t) sz) { free (d); d = 0; }
	fclose (f);
	if (d) { memset (d + sz, 0, 16); *n = (int) sz; }
	return d;
}
static bool write_file (const char *path, const void *d, int n)
{
	FILE *f = u8open (path, "wb");
	if (!f) return false;
	bool ok = fwrite (d, 1, (size_t) n, f) == (size_t) n;
	return fclose (f) == 0 && ok;
}
static char low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static bool ends (const char *s, const char *e)
{
	size_t a = strlen (s), b = strlen (e);
	if (a < b) return false;
	for (size_t i = 0; i < b; i++) if (low (s[a - b + i]) != e[i]) return false;
	return true;
}

// The system of a file, from its extension (0: none of ours).
NE_API int ne_system_of (const char *path)
{
	if (ends (path, ".gbc")) return NE_GBC;
	if (ends (path, ".gb")) return NE_GB;
	if (ends (path, ".gba")) return NE_GBA;
	if (ends (path, ".nes")) return NE_NES;
	if (ends (path, ".sfc") || ends (path, ".smc")) return NE_SNES;
	if (ends (path, ".z64") || ends (path, ".n64") || ends (path, ".v64")) return NE_N64;
	if (ends (path, ".iso") || ends (path, ".gcm") || ends (path, ".dol")) return NE_GC;
	return NE_NONE;
}

// ---- the pads (XInput), as the Onyx pads: PAD_* bits named after their place --------------------
enum
{
	PAD_UP = 1 << 0, PAD_DOWN = 1 << 1, PAD_LEFT = 1 << 2, PAD_RIGHT = 1 << 3,
	PAD_A = 1 << 4, PAD_B = 1 << 5, PAD_X = 1 << 6, PAD_Y = 1 << 7,
	PAD_L = 1 << 8, PAD_R = 1 << 9, PAD_L2 = 1 << 10, PAD_R2 = 1 << 11,
	PAD_SELECT = 1 << 12, PAD_START = 1 << 13, PAD_L3 = 1 << 14, PAD_R3 = 1 << 15
};
struct Pad { bool on; unsigned buttons; int lx, ly, rx, ry; };	// sticks -1000..1000, y down

#ifdef _WIN32
typedef DWORD (WINAPI *XGetState) (DWORD, XINPUT_STATE *);
static XGetState s_xget = 0; static bool s_xtried = false;
static int stick (int v, int dead)
{
	if (v > -dead && v < dead) return 0;
	int r = (int) ((long long) v * 1000 / 32767);
	return r > 1000 ? 1000 : r < -1000 ? -1000 : r;
}
#endif
// Other USB pads (DirectInput / HID ones: the Windows joystick API), through a mapping the front end
// sets (Options > Gamepad...): for each PAD_* bit, its source -- 0 none, 1..32 a button, 100 + axis
// x 2 + (0: its low end, 1: its high end), 200 + the hat's direction (0 up, 1 right, 2 down, 3 left);
// and the sticks' axes (0 X, 1 Y, 2 Z, 3 R, 4 U, 5 V; -1 none). The defaults: the usual generic HID
// order (1 top, 2 right, 3 bottom, 4 left, 5 L1, 6 R1, 7 L2, 8 R2, 9 select, 10 start, 11 L3, 12 R3),
// the d-pad on the hat, the left stick on X / Y, the right one on Z / R.
enum { NMAP = 16 };
static int s_map[NMAP] = { 200, 202, 203, 201, 3, 2, 4, 1, 5, 6, 7, 8, 9, 10, 11, 12 };
static int s_axes[4] = { 0, 1, 2, 3 };
NE_API void ne_pad_map (const int *map, int n, const int *axes)
{
	for (int i = 0; i < n && i < NMAP; i++) s_map[i] = map[i];
	if (axes) for (int i = 0; i < 4; i++) s_axes[i] = axes[i];
}

#ifdef _WIN32
struct Joy { bool on; char name[64]; unsigned buttons; int axis[6]; int pov; };	// axes -1000..1000, pov: -1 or 0..35999
static int s_joy = -1; static DWORD s_joyNext = 0;
static int axis_norm (DWORD v, UINT lo, UINT hi) { if (hi <= lo) return 0; long long r = ((long long) v - lo) * 2000 / (hi - lo) - 1000; return r > 1000 ? 1000 : r < -1000 ? -1000 : (int) r; }
static bool joy_read (int id, Joy *j)
{
	JOYINFOEX ji; memset (&ji, 0, sizeof ji); ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL;
	if (joyGetPosEx ((UINT) id, &ji) != JOYERR_NOERROR) return false;
	JOYCAPSA c; memset (&c, 0, sizeof c);
	if (joyGetDevCapsA ((UINT) id, &c, sizeof c) != JOYERR_NOERROR) return false;
	j->on = true;
	strncpy (j->name, c.szPname, sizeof j->name - 1); j->name[sizeof j->name - 1] = 0;
	j->buttons = ji.dwButtons;
	j->axis[0] = axis_norm (ji.dwXpos, c.wXmin, c.wXmax); j->axis[1] = axis_norm (ji.dwYpos, c.wYmin, c.wYmax);
	j->axis[2] = c.wNumAxes > 2 ? axis_norm (ji.dwZpos, c.wZmin, c.wZmax) : 0;
	j->axis[3] = c.wNumAxes > 3 ? axis_norm (ji.dwRpos, c.wRmin, c.wRmax) : 0;
	j->axis[4] = c.wNumAxes > 4 ? axis_norm (ji.dwUpos, c.wUmin, c.wUmax) : 0;
	j->axis[5] = c.wNumAxes > 5 ? axis_norm (ji.dwVpos, c.wVmin, c.wVmax) : 0;
	j->pov = (c.wCaps & JOYCAPS_HASPOV) && ji.dwPOV <= 35999 ? (int) ji.dwPOV : -1;
	return true;
}
// the first USB pad Windows sees (looked for again every 2 s while there is none)
static bool joy_first (Joy *j)
{
	memset (j, 0, sizeof *j);
	if (s_joy >= 0 && joy_read (s_joy, j)) return true;
	s_joy = -1;
	DWORD now = GetTickCount ();
	if ((int) (now - s_joyNext) < 0) return false;
	s_joyNext = now + 2000;
	UINT n = joyGetNumDevs ();
	for (UINT i = 0; i < n && i < 16; i++) if (joy_read ((int) i, j)) { s_joy = (int) i; return true; }
	return false;
}
static bool joy_source (const Joy &j, int src)
{
	if (src >= 1 && src <= 32) return (j.buttons >> (src - 1)) & 1;
	if (src >= 100 && src < 112) { int v = j.axis[(src - 100) / 2]; return (src & 1) ? v > 500 : v < -500; }
	if (src >= 200 && src < 204 && j.pov >= 0)
	{
		int d = j.pov / 100, c = (src - 200) * 90;			// (a diagonal counts for both)
		int diff = d - c; if (diff < 0) diff = -diff; if (diff > 180) diff = 360 - diff;
		return diff <= 45;
	}
	return false;
}
// the raw state for the mapping dialog: its name; the buttons; the axes; the hat (-1 or 0..35999)
NE_API int ne_joy_raw (char *name, int cap, unsigned *buttons, int *axes, int *pov)
{
	Joy j;
	s_joyNext = 0;						// (the dialog: look now)
	if (!joy_first (&j)) { if (cap > 0) name[0] = 0; return 0; }
	strncpy (name, j.name, (size_t) cap - 1); name[cap - 1] = 0;
	*buttons = j.buttons; for (int i = 0; i < 6; i++) axes[i] = j.axis[i]; *pov = j.pov;
	return 1;
}
#else
NE_API int ne_joy_raw (char *name, int cap, unsigned *buttons, int *axes, int *pov)
{ (void) axes; if (cap > 0) name[0] = 0; *buttons = 0; *pov = -1; return 0; }
#endif

static Pad read_pad (int port)
{
	Pad p; memset (&p, 0, sizeof p);
#ifdef _WIN32
	if (!s_xtried)
	{
		s_xtried = true;
		const char *dll[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
		for (int i = 0; i < 3 && !s_xget; i++)
		{
			HMODULE m = LoadLibraryA (dll[i]);
			if (m) s_xget = (XGetState) (void *) GetProcAddress (m, "XInputGetState");
		}
	}
	XINPUT_STATE st;
	if (!s_xget || s_xget ((DWORD) port, &st) != ERROR_SUCCESS)
	{
		// not an Xbox-style pad: another USB pad, through the mapping
		Joy j;
		if (port != 0 || !joy_first (&j)) return p;
		p.on = true;
		for (int i = 0; i < NMAP; i++) if (joy_source (j, s_map[i])) p.buttons |= 1u << i;
		auto ax = [&] (int k) { int a = s_axes[k]; int v = a >= 0 && a < 6 ? j.axis[a] : 0; return v > -150 && v < 150 ? 0 : v; };
		p.lx = ax (0); p.ly = ax (1); p.rx = ax (2); p.ry = ax (3);
		return p;
	}
	const XINPUT_GAMEPAD &g = st.Gamepad;
	unsigned w = g.wButtons, b = 0;
	if (w & XINPUT_GAMEPAD_DPAD_UP) b |= PAD_UP;
	if (w & XINPUT_GAMEPAD_DPAD_DOWN) b |= PAD_DOWN;
	if (w & XINPUT_GAMEPAD_DPAD_LEFT) b |= PAD_LEFT;
	if (w & XINPUT_GAMEPAD_DPAD_RIGHT) b |= PAD_RIGHT;
	if (w & XINPUT_GAMEPAD_A) b |= PAD_A;
	if (w & XINPUT_GAMEPAD_B) b |= PAD_B;
	if (w & XINPUT_GAMEPAD_X) b |= PAD_X;
	if (w & XINPUT_GAMEPAD_Y) b |= PAD_Y;
	if (w & XINPUT_GAMEPAD_LEFT_SHOULDER) b |= PAD_L;
	if (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) b |= PAD_R;
	if (g.bLeftTrigger > 64) b |= PAD_L2;
	if (g.bRightTrigger > 64) b |= PAD_R2;
	if (w & XINPUT_GAMEPAD_BACK) b |= PAD_SELECT;
	if (w & XINPUT_GAMEPAD_START) b |= PAD_START;
	if (w & XINPUT_GAMEPAD_LEFT_THUMB) b |= PAD_L3;
	if (w & XINPUT_GAMEPAD_RIGHT_THUMB) b |= PAD_R3;
	p.on = true; p.buttons = b;
	p.lx = stick (g.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE); p.ly = -stick (g.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
	p.rx = stick (g.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE); p.ry = -stick (g.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
#else
	(void) port;
#endif
	return p;
}

// ---- a game ------------------------------------------------------------------------------------------
// Windows virtual keys (the front end's key states: vk[code] != 0 while held)
enum { VK_BS = 0x08, VK_ENT = 0x0D, VK_L = 0x25, VK_U = 0x26, VK_R = 0x27, VK_D = 0x28 };

struct Lock
{
#ifdef _WIN32
	CRITICAL_SECTION cs;
	Lock () { InitializeCriticalSection (&cs); } ~Lock () { DeleteCriticalSection (&cs); }
	void take () { EnterCriticalSection (&cs); } void give () { LeaveCriticalSection (&cs); }
#else
	void take () {} void give () {}
#endif
};

struct Emu
{
	int sys;
	char path[1024], sav[1024];
	unsigned char *rom; int romSize;
	FILE *disc;
	gb::Machine *gb; gba::Machine *gba; nes::Machine *nes; snes::Machine *snes; n64::Machine *n64; gc::Machine *gc;
	unsigned char keys[256];
	int scale;
	unsigned lastSerial, gfxAge;
	// the picture: drawn into back, then swapped with front (under lock) for the window
	unsigned *pic[2]; int picCap[2], picW[2], picH[2]; int front; unsigned serial;
	float *zb; int zbCap;
	struct Tri *tris; int trisCap;
	struct Gl *gl;						// the OpenGL renderer (Windows), 0: the software one
	Lock lock;
	int saveTick;
};

static bool gl_draw (Emu *e);
static void gl_free (Emu *e);

static void set_sav_path (Emu *e)
{
	memcpy (e->sav, e->path, sizeof e->sav - 8); e->sav[sizeof e->sav - 8] = 0;
	int n = (int) strlen (e->sav), d = n;
	while (d > 0 && e->sav[d - 1] != '.' && e->sav[d - 1] != '/' && e->sav[d - 1] != '\\') d--;
	if (d > 0 && e->sav[d - 1] == '.') n = d - 1;
	strcpy (e->sav + n, ".sav");
}

static bool disc_read (void *ctx, gc::u32 off, gc::u32 len, gc::u8 *dst)
{
	FILE *f = (FILE *) ctx;
	return u8seek (f, off) && fread (dst, 1, len, f) == len;
}

static void load_save (Emu *e)
{
	int n = 0;
	unsigned char *d = slurp (e->sav, &n, 1 << 20);
	if (!d) return;
	switch (e->sys)
	{
	case NE_GB: case NE_GBC: if (e->gb->battery && e->gb->sram) e->gb->setSaveRam (d, n); break;
	case NE_GBA: e->gba->setSaveRam (d, n); break;
	case NE_NES: if (e->nes->battery) e->nes->setSaveRam (d, n); break;
	case NE_SNES: if (e->snes->sramSize) e->snes->setSaveRam (d, n); break;
	case NE_N64:
		if (n == 512 || n == 2048) { memcpy (e->n64->eeprom, d, (size_t) n); e->n64->eepromSize = n; e->n64->saveType = n == 512 ? 2 : 3; }
		else memcpy (e->n64->sram, d, (size_t) n < sizeof e->n64->sram ? (size_t) n : sizeof e->n64->sram);
		break;
	}
	free (d);
}

// the battery save, if it changed (as the Onyx apps: the same .sav files)
NE_API void ne_save (void *h)
{
	Emu *e = (Emu *) h;
	if (!e) return;
	switch (e->sys)
	{
	case NE_GB: case NE_GBC:
		if (e->gb->battery && e->gb->sram && e->gb->sramDirty && write_file (e->sav, e->gb->sram, e->gb->sramSize)) e->gb->sramDirty = false;
		break;
	case NE_GBA:
		if (e->gba->saveType != gba::SAVE_NONE && e->gba->saveSize && e->gba->saveDirty && write_file (e->sav, e->gba->save, e->gba->saveSize)) e->gba->saveDirty = false;
		break;
	case NE_NES:
		if (e->nes->battery && e->nes->sramDirty && write_file (e->sav, e->nes->sram, (int) sizeof e->nes->sram)) e->nes->sramDirty = false;
		break;
	case NE_SNES:
		if (e->snes->sramSize && e->snes->sramDirty && write_file (e->sav, e->snes->sram, (int) e->snes->sramSize)) e->snes->sramDirty = false;
		break;
	case NE_N64:
		if (e->n64->saveType == 1 && e->n64->sramDirty) { if (write_file (e->sav, e->n64->sram, 0x8000)) e->n64->sramDirty = false; }
		else if (e->n64->saveType >= 2 && e->n64->eepromDirty && write_file (e->sav, e->n64->eeprom, e->n64->eepromSize)) e->n64->eepromDirty = false;
		break;
	}
}

NE_API void ne_close (void *h)
{
	Emu *e = (Emu *) h;
	if (!e) return;
	ne_save (e);
	gl_free (e);
	delete e->gb; delete e->gba; delete e->nes; delete e->snes; delete e->n64; delete e->gc;
	if (e->disc) fclose (e->disc);
	free (e->rom); free (e->pic[0]); free (e->pic[1]); free (e->zb); free (e->tris);
	delete e;
}

static void seterr (char *err, int cap, const char *s) { if (err && cap > 0) { strncpy (err, s, (size_t) cap - 1); err[cap - 1] = 0; } }

NE_API void *ne_open (const char *path, int audioRate, char *err, int errCap)
{
	int sys = ne_system_of (path);
	if (!sys) { seterr (err, errCap, "Not a ROM of a system NintendoEMU knows (by its extension)."); return 0; }
	Emu *e = new Emu ();					// (zeroed: no constructor of its own)
	e->sys = sys; e->scale = 2; e->gfxAge = 1000;
	strncpy (e->path, path, sizeof e->path - 1);
	set_sav_path (e);
	bool ok = false;
	if (sys == NE_GC)
	{
		e->gc = new gc::Machine;
		if (ends (path, ".dol"))
		{
			e->rom = slurp (path, &e->romSize, 64 << 20);
			ok = e->rom && e->gc->loadDol (e->rom, (unsigned) e->romSize);
		}
		else
		{
			e->disc = u8open (path, "rb");
			long long sz = e->disc ? u8size (e->disc) : 0;
			if (e->disc && sz > 0 && sz < 0x100000000ll)
			{
				e->gc->discRead = disc_read; e->gc->discCtx = e->disc;
				ok = e->gc->loadDiscImage ((unsigned) sz);
			}
		}
		if (!ok) { seterr (err, errCap, "Not a GameCube disc image (.iso / .gcm) or program (.dol), or its start failed."); ne_close (e); return 0; }
		return e;
	}
	e->rom = slurp (path, &e->romSize, 128 << 20);
	if (!e->rom) { seterr (err, errCap, "Cannot read the file."); ne_close (e); return 0; }
	switch (sys)
	{
	case NE_GB: case NE_GBC: e->gb = new gb::Machine; e->gb->setAudioRate (audioRate); ok = e->gb->load (e->rom, e->romSize); break;
	case NE_GBA: e->gba = new gba::Machine; e->gba->setAudioRate (audioRate); ok = e->gba->load (e->rom, e->romSize); break;
	case NE_NES: e->nes = new nes::Machine; e->nes->setAudioRate (audioRate); ok = e->nes->load (e->rom, e->romSize); break;
	case NE_SNES: e->snes = new snes::Machine; e->snes->setAudioRate (audioRate); ok = e->snes->load (e->rom, e->romSize); break;
	case NE_N64: e->n64 = new n64::Machine; ok = e->n64->load (e->rom, (unsigned) e->romSize); e->n64->setAudioRate (audioRate); break;
	}
	if (!ok)
	{
		seterr (err, errCap, sys == NE_NES ? "Not a NES ROM, or its cartridge (mapper) is not supported."
			: sys == NE_SNES ? "Not a Super Nintendo ROM, or it needs an enhancement chip (Super FX, SA-1, DSP...)."
			: "Not a ROM of this system.");
		ne_close (e); return 0;
	}
	load_save (e);
	return e;
}

NE_API int ne_system (void *h) { Emu *e = (Emu *) h; return e ? (e->gb && e->gb->cgb ? NE_GBC : e->sys) : 0; }

// the title in the ROM's header ("" if none)
NE_API void ne_title (void *h, char *out, int cap)
{
	Emu *e = (Emu *) h; const char *t = "";
	if (e->gb) t = e->gb->title; else if (e->gba) t = e->gba->title; else if (e->snes) t = e->snes->title;
	else if (e->n64) t = e->n64->title; else if (e->gc) t = e->gc->title;
	strncpy (out, t, (size_t) cap - 1); out[cap - 1] = 0;
}

// the frames a second (x 1000)
NE_API int ne_fps1000 (void *h)
{
	Emu *e = (Emu *) h;
	switch (e->sys)
	{
	case NE_GB: case NE_GBC: case NE_GBA: return 59727;
	case NE_NES: return e->nes->pal ? 50007 : 60099;
	case NE_SNES: return e->snes->pal ? 50007 : 60099;
	case NE_N64: return e->n64->pal ? 50000 : 60000;
	case NE_GC: return e->gc->pal ? 50000 : 59940;
	}
	return 60000;
}

// the picture's shape on screen (width / height x 1000)
NE_API int ne_aspect1000 (void *h)
{
	Emu *e = (Emu *) h;
	if (e->sys == NE_GB || e->sys == NE_GBC) return 160 * 1000 / 144;
	if (e->sys == NE_GBA) return 1500;
	return 1333;
}

NE_API void ne_set_scale (void *h, int s) { Emu *e = (Emu *) h; if (e) e->scale = s < 1 ? 1 : s > 4 ? 4 : s; }
NE_API void ne_set_keys (void *h, const unsigned char *vk) { Emu *e = (Emu *) h; if (e) memcpy (e->keys, vk, 256); }

NE_API void ne_reset (void *h)
{
	Emu *e = (Emu *) h;
	if (e->gb) e->gb->reset (); else if (e->gba) e->gba->reset (); else if (e->nes) e->nes->reset ();
	else if (e->snes) e->snes->reset (); else if (e->n64) e->n64->reset ();
	// (the GameCube: the front end reopens the game)
}

// ---- the input, as each Onyx app maps it -------------------------------------------------------------
static bool K (Emu *e, int vk) { return e->keys[vk] != 0; }
static unsigned dpad_from_stick (const Pad &p)
{
	unsigned b = p.buttons;
	if (p.lx > 500) b |= PAD_RIGHT;
	if (p.lx < -500) b |= PAD_LEFT;
	if (p.ly > 500) b |= PAD_DOWN;
	if (p.ly < -500) b |= PAD_UP;
	return b;
}
static void apply_input (Emu *e)
{
	Pad pd = read_pad (0);
	unsigned p = dpad_from_stick (pd);
	switch (e->sys)
	{
	case NE_GB: case NE_GBC:
	{
		int b = 0;
		if (K (e, VK_R) || (p & PAD_RIGHT)) b |= gb::BTN_RIGHT;
		if (K (e, VK_L) || (p & PAD_LEFT)) b |= gb::BTN_LEFT;
		if (K (e, VK_U) || (p & PAD_UP)) b |= gb::BTN_UP;
		if (K (e, VK_D) || (p & PAD_DOWN)) b |= gb::BTN_DOWN;
		if (K (e, 'X') || (p & (PAD_B | PAD_Y))) b |= gb::BTN_A;
		if (K (e, 'Z') || (p & (PAD_A | PAD_X))) b |= gb::BTN_B;
		if (K (e, VK_ENT) || (p & PAD_START)) b |= gb::BTN_START;
		if (K (e, VK_BS) || (p & PAD_SELECT)) b |= gb::BTN_SELECT;
		e->gb->setButtons (b);
		break;
	}
	case NE_GBA:
	{
		int b = 0;
		if (K (e, VK_R) || (p & PAD_RIGHT)) b |= gba::BTN_RIGHT;
		if (K (e, VK_L) || (p & PAD_LEFT)) b |= gba::BTN_LEFT;
		if (K (e, VK_U) || (p & PAD_UP)) b |= gba::BTN_UP;
		if (K (e, VK_D) || (p & PAD_DOWN)) b |= gba::BTN_DOWN;
		if (K (e, 'X') || (p & (PAD_B | PAD_Y))) b |= gba::BTN_A;
		if (K (e, 'Z') || (p & (PAD_A | PAD_X))) b |= gba::BTN_B;
		if (K (e, VK_ENT) || (p & PAD_START)) b |= gba::BTN_START;
		if (K (e, VK_BS) || (p & PAD_SELECT)) b |= gba::BTN_SELECT;
		if (K (e, 'A') || (p & (PAD_L | PAD_L2))) b |= gba::BTN_L;
		if (K (e, 'S') || (p & (PAD_R | PAD_R2))) b |= gba::BTN_R;
		e->gba->setButtons (b);
		break;
	}
	case NE_NES:
	{
		int b = 0;
		if (K (e, VK_R) || (p & PAD_RIGHT)) b |= nes::BTN_RIGHT;
		if (K (e, VK_L) || (p & PAD_LEFT)) b |= nes::BTN_LEFT;
		if (K (e, VK_U) || (p & PAD_UP)) b |= nes::BTN_UP;
		if (K (e, VK_D) || (p & PAD_DOWN)) b |= nes::BTN_DOWN;
		if (K (e, 'X') || (p & (PAD_B | PAD_Y))) b |= nes::BTN_A;
		if (K (e, 'Z') || (p & (PAD_A | PAD_X))) b |= nes::BTN_B;
		if (K (e, VK_ENT) || (p & PAD_START)) b |= nes::BTN_START;
		if (K (e, VK_BS) || (p & PAD_SELECT)) b |= nes::BTN_SELECT;
		e->nes->setButtons (b);
		break;
	}
	case NE_SNES:
	{
		int b = 0;
		if (K (e, VK_R) || (p & PAD_RIGHT)) b |= snes::BTN_RIGHT;
		if (K (e, VK_L) || (p & PAD_LEFT)) b |= snes::BTN_LEFT;
		if (K (e, VK_U) || (p & PAD_UP)) b |= snes::BTN_UP;
		if (K (e, VK_D) || (p & PAD_DOWN)) b |= snes::BTN_DOWN;
		if (K (e, 'X') || (p & PAD_B)) b |= snes::BTN_A;
		if (K (e, 'Z') || (p & PAD_A)) b |= snes::BTN_B;
		if (K (e, 'S') || (p & PAD_Y)) b |= snes::BTN_X;
		if (K (e, 'A') || (p & PAD_X)) b |= snes::BTN_Y;
		if (K (e, 'Q') || (p & (PAD_L | PAD_L2))) b |= snes::BTN_L;
		if (K (e, 'W') || (p & (PAD_R | PAD_R2))) b |= snes::BTN_R;
		if (K (e, VK_ENT) || (p & PAD_START)) b |= snes::BTN_START;
		if (K (e, VK_BS) || (p & PAD_SELECT)) b |= snes::BTN_SELECT;
		e->snes->setButtons (b);
		break;
	}
	case NE_N64:
	{
		int b = 0, x = 0, y = 0;
		unsigned q = pd.buttons;
		if (K (e, VK_R)) x += 80;
		if (K (e, VK_L)) x -= 80;
		if (K (e, VK_U)) y += 80;
		if (K (e, VK_D)) y -= 80;
		if (K (e, 'X') || (q & PAD_A)) b |= n64::BTN_A;
		if (K (e, 'C') || (q & PAD_X)) b |= n64::BTN_B;
		if (K (e, 'Z') || (q & (PAD_L2 | PAD_R2))) b |= n64::BTN_Z;
		if (K (e, VK_ENT) || (q & PAD_START)) b |= n64::BTN_START;
		if (K (e, 'Q') || (q & PAD_L)) b |= n64::BTN_L;
		if (K (e, 'W') || (q & PAD_R)) b |= n64::BTN_R;
		if (K (e, 'I') || (q & PAD_Y) || pd.ry < -500) b |= n64::BTN_CUP;
		if (K (e, 'K') || pd.ry > 500) b |= n64::BTN_CDOWN;
		if (K (e, 'J') || pd.rx < -500) b |= n64::BTN_CLEFT;
		if (K (e, 'L') || (q & PAD_B) || pd.rx > 500) b |= n64::BTN_CRIGHT;
		if (K (e, 'T') || (q & PAD_UP)) b |= n64::BTN_DUP;
		if (K (e, 'G') || (q & PAD_DOWN)) b |= n64::BTN_DDOWN;
		if (K (e, 'F') || (q & PAD_LEFT)) b |= n64::BTN_DLEFT;
		if (K (e, 'H') || (q & PAD_RIGHT)) b |= n64::BTN_DRIGHT;
		if (pd.lx || pd.ly) { x = pd.lx * 80 / 1000; y = -pd.ly * 80 / 1000; }
		e->n64->setPad (0, (unsigned) b, x, y);
		break;
	}
	case NE_GC:
	{
		int b = 0, x = 0, y = 0, cx = 0, cy = 0, l = 0, r = 0;
		unsigned q = pd.buttons;
		typedef gc::Machine M;
		if (K (e, VK_R)) x += 90;
		if (K (e, VK_L)) x -= 90;
		if (K (e, VK_U)) y += 90;
		if (K (e, VK_D)) y -= 90;
		if (K (e, 'X') || (q & PAD_A)) b |= M::PAD_A;
		if (K (e, 'C') || (q & PAD_X)) b |= M::PAD_B;
		if (K (e, 'S') || (q & PAD_B)) b |= M::PAD_X;
		if (K (e, 'A') || (q & PAD_Y)) b |= M::PAD_Y;
		if (K (e, 'Z') || (q & (PAD_L | PAD_R))) b |= M::PAD_Z;
		if (K (e, VK_ENT) || (q & PAD_START)) b |= M::PAD_START;
		if (K (e, 'Q') || (q & PAD_L2)) { b |= M::PAD_L; l = 255; }
		if (K (e, 'W') || (q & PAD_R2)) { b |= M::PAD_R; r = 255; }
		if (K (e, 'T') || (q & PAD_UP)) b |= M::PAD_UP;
		if (K (e, 'G') || (q & PAD_DOWN)) b |= M::PAD_DOWN;
		if (K (e, 'F') || (q & PAD_LEFT)) b |= M::PAD_LEFT;
		if (K (e, 'H') || (q & PAD_RIGHT)) b |= M::PAD_RIGHT;
		if (K (e, 'L')) cx += 90;
		if (K (e, 'J')) cx -= 90;
		if (K (e, 'I')) cy += 90;
		if (K (e, 'K')) cy -= 90;
		if (pd.lx || pd.ly) { x = pd.lx * 100 / 1000; y = -pd.ly * 100 / 1000; }
		if (pd.rx || pd.ry) { cx = pd.rx * 100 / 1000; cy = -pd.ry * 100 / 1000; }
		e->gc->setPad (0, (unsigned) b, x, y, cx, cy, l, r);
		break;
	}
	}
}

// ---- the picture ---------------------------------------------------------------------------------------
static unsigned *back_buffer (Emu *e, int w, int h)
{
	int b = 1 - e->front;
	if (e->picCap[b] < w * h)
	{
		free (e->pic[b]);
		e->pic[b] = (unsigned *) malloc ((size_t) w * h * 4);
		e->picCap[b] = e->pic[b] ? w * h : 0;
		if (!e->pic[b]) return 0;
	}
	e->picW[b] = w; e->picH[b] = h;
	return e->pic[b];
}
static void publish (Emu *e)
{
	e->lock.take ();
	e->front = 1 - e->front; e->serial++;
	e->lock.give ();
}
static void copy_fb (Emu *e, const unsigned *src, int w, int h)
{
	unsigned *d = back_buffer (e, w, h);
	if (!d) return;
	memcpy (d, src, (size_t) w * h * 4);
	publish (e);
}

// a 3D frame (the N64's, the GameCube's) by the software renderer, scale x its resolution: the
// triangles transformed and clipped once, then drawn in bands of 16 rows by every core of the PC
// (the bands are disjoint: each draws all the triangles in order, the same picture as one thread)
struct Tri { bas::G3Clip c[3]; unsigned flags; const bas::G3Texture *T; };
struct RenderJob
{
	unsigned *dst; float *zb; int W, H; unsigned clear;
	const Tri *tris; int n;
	int bands; volatile int next;
};
enum { BAND = 16, MAX_THREADS = 16 };
static void render_bands (RenderJob *j)
{
	for (;;)
	{
		int b = __atomic_fetch_add (&j->next, 1, __ATOMIC_RELAXED);
		if (b >= j->bands) return;
		int y0 = b * BAND, y1 = y0 + BAND < j->H ? y0 + BAND : j->H;
		for (int y = y0; y < y1; y++)
		{
			unsigned *d = j->dst + (long long) y * j->W; float *z = j->zb + (long long) y * j->W;
			for (int x = 0; x < j->W; x++) { d[x] = j->clear; z[x] = 1.0f; }
		}
		for (int i = 0; i < j->n; i++)
			bas::g3raster (j->dst, j->W, j->H, j->W, j->zb, j->tris[i].c, j->tris[i].flags, j->tris[i].T, y0, y1);
	}
}
#ifdef _WIN32
static DWORD WINAPI band_thread (LPVOID p) { render_bands ((RenderJob *) p); return 0; }
static int cpu_count () { SYSTEM_INFO si; GetSystemInfo (&si); return (int) si.dwNumberOfProcessors; }
#else
static void *band_thread (void *p) { render_bands ((RenderJob *) p); return 0; }
static int cpu_count () { return (int) sysconf (_SC_NPROCESSORS_ONLN); }
#endif

template <typename FR, typename TX>
static void render3d (Emu *e, const FR &fr, TX *tex, int ntex)
{
	int W = fr.width * e->scale, H = fr.height * e->scale;
	if (W <= 0 || H <= 0) return;
	unsigned *d = back_buffer (e, W, H);
	if (!d) return;
	if (e->zbCap < W * H)
	{
		free (e->zb); e->zb = (float *) malloc ((size_t) W * H * 4);
		e->zbCap = e->zb ? W * H : 0;
		if (!e->zb) return;
	}
	static bas::G3Batch bt[4096];
	static bas::G3Texture tx[1024];
	int nb = fr.nb < 4096 ? fr.nb : 4096;
	for (int i = 0; i < nb; i++) { memcpy (&bt[i], &fr.b[i], sizeof bt[i]); bt[i].texture = fr.b[i].tex >= 0 ? fr.b[i].tex + 1 : 0; }
	for (int i = 0; i < ntex && i < 1024; i++) { tx[i].px = tex[i].px; tx[i].w = tex[i].w; tx[i].h = tex[i].h; }
	int n = 0;
	bas::swTriangles ((const bas::G3Vertex *) fr.v, fr.nv, bt, nb, [] (int t) -> const bas::G3Texture * { return &tx[t - 1]; },
		[&] (const bas::G3Clip *c, unsigned flags, const bas::G3Texture *T)
	{
		if (n >= e->trisCap)
		{
			int cap = e->trisCap ? e->trisCap * 2 : 16384;
			Tri *t = (Tri *) realloc (e->tris, (size_t) cap * sizeof (Tri));
			if (!t) return;
			e->tris = t; e->trisCap = cap;
		}
		Tri &o = e->tris[n++];
		o.c[0] = c[0]; o.c[1] = c[1]; o.c[2] = c[2]; o.flags = flags; o.T = T;
	});
	RenderJob j = { d, e->zb, W, H, fr.clear, e->tris, n, (H + BAND - 1) / BAND, 0 };
	int nt = cpu_count (); if (nt > MAX_THREADS) nt = MAX_THREADS; if (nt > j.bands) nt = j.bands;
#ifdef _WIN32
	HANDLE th[MAX_THREADS]; int k = 0;
	for (int i = 1; i < nt; i++) { th[k] = CreateThread (0, 0, band_thread, &j, 0, 0); if (th[k]) k++; }
	render_bands (&j);
	if (k) WaitForMultipleObjects ((DWORD) k, th, TRUE, INFINITE);
	for (int i = 0; i < k; i++) CloseHandle (th[i]);
#else
	pthread_t th[MAX_THREADS]; int k = 0;
	for (int i = 1; i < nt; i++) if (pthread_create (&th[k], 0, band_thread, &j) == 0) k++;
	render_bands (&j);
	for (int i = 0; i < k; i++) pthread_join (th[i], 0);
#endif
	publish (e);
}

static void draw (Emu *e)
{
	if (e->gl && (e->sys == NE_N64 || e->sys == NE_GC) && gl_draw (e)) return;
	switch (e->sys)
	{
	case NE_GB: case NE_GBC: copy_fb (e, e->gb->fb, gb::W, gb::H); break;
	case NE_GBA: copy_fb (e, e->gba->fb, gba::W, gba::H); break;
	case NE_NES: copy_fb (e, e->nes->fb, nes::W, nes::H); break;
	case NE_SNES: copy_fb (e, e->snes->fb, snes::W, e->snes->height > 0 ? e->snes->height : 224); break;
	case NE_N64:
		static_assert (sizeof (n64::GVertex) == sizeof (bas::G3Vertex) && sizeof (n64::GBatch) == sizeof (bas::G3Batch), "layout");
		if (e->gfxAge < 30 && e->n64->gfxReady >= 0)
		{
			render3d (e, e->n64->gfxFrame[e->n64->gfxReady], e->n64->tex, n64::Machine::MAX_TEX);
			int b = e->front;					// (the picture just published, back to the machine)
			if (e->pic[b]) e->n64->fbSnapshot (e->pic[b], e->picW[b], e->picH[b], e->picW[b]);
		}
		else copy_fb (e, e->n64->fb, e->n64->fbW, e->n64->fbH);
		break;
	case NE_GC:
		static_assert (sizeof (gc::GVertex) == sizeof (bas::G3Vertex) && sizeof (gc::GBatch) == sizeof (bas::G3Batch), "layout");
		if (e->gfxAge < 30 && e->gc->gfxReady >= 0) render3d (e, e->gc->gfxFrame[e->gc->gfxReady], e->gc->tex, gc::Machine::MAX_TEX);
		else copy_fb (e, e->gc->fb, e->gc->fbW, e->gc->fbH);
		break;
	}
}

// One video frame of the game (draw = 0: not drawn, to catch up). The game's own thread.
NE_API int ne_run_frame (void *h, int drawIt)
{
	Emu *e = (Emu *) h;
	if (!e) return 0;
	apply_input (e);
	switch (e->sys)
	{
	case NE_GB: case NE_GBC: e->gb->runFrame (); break;
	case NE_GBA: e->gba->runFrame (); break;
	case NE_NES: e->nes->runFrame (); break;
	case NE_SNES: e->snes->runFrame (); break;
	case NE_N64:
		e->n64->runFrame ();
		if (e->n64->gfxSerial != e->lastSerial) { e->lastSerial = e->n64->gfxSerial; e->gfxAge = 0; } else if (e->gfxAge < 1000) e->gfxAge++;
		break;
	case NE_GC:
		if (e->gc->halted) return 0;
		e->gc->runFrame ();
		if (e->gc->gfxSerial != e->lastSerial) { e->lastSerial = e->gc->gfxSerial; e->gfxAge = 0; } else if (e->gfxAge < 1000) e->gfxAge++;
		break;
	}
	if (drawIt) draw (e);
	if (++e->saveTick >= 300) { e->saveTick = 0; ne_save (e); }	// every ~5 s, if it changed
	return 1;
}

// the sound made so far: s16 stereo frames at ne_open's rate (0: none)
NE_API int ne_audio (void *h, short *lr, int maxFrames)
{
	Emu *e = (Emu *) h;
	if (e->gb) return e->gb->audioRead (lr, maxFrames);
	if (e->gba) return e->gba->audioRead (lr, maxFrames);
	if (e->nes) return e->nes->audioRead (lr, maxFrames);
	if (e->snes) return e->snes->audioRead (lr, maxFrames);
	if (e->n64) return e->n64->audioRead (lr, maxFrames);
	return 0;
}

// The last picture (the window's thread): its size, copied into px if it fits (cap pixels).
// Returns its serial (a new picture: a new number), 0 none yet.
NE_API unsigned ne_video (void *h, unsigned *px, int cap, int *w, int *hh)
{
	Emu *e = (Emu *) h;
	e->lock.take ();
	int f = e->front; unsigned s = e->serial;
	*w = e->picW[f]; *hh = e->picH[f];
	if (s && px && e->pic[f] && e->picW[f] * e->picH[f] <= cap) memcpy (px, e->pic[f], (size_t) e->picW[f] * e->picH[f] * 4);
	e->lock.give ();
	return s;
}

// a GameCube game's state, for the speed display (to tell a slow game from a stuck one); "" others
NE_API void ne_status (void *h, char *out, int cap)
{
	Emu *e = (Emu *) h;
	if (cap > 0) out[0] = 0;
	if (e && e->gc) e->gc->status (out, cap);
}

// what the speed display shows: 1 = the 3D renderer drew the last picture
NE_API int ne_is_3d (void *h) { Emu *e = (Emu *) h; return (e->sys == NE_N64 || e->sys == NE_GC) && e->gfxAge < 30; }
NE_API int ne_halted (void *h, char *msg, int cap)
{
	Emu *e = (Emu *) h;
	bool hl = e->gc ? e->gc->halted : e->n64 ? e->n64->halted : false;
	if (hl && msg && cap > 0) { strncpy (msg, e->gc ? e->gc->haltMsg : "", (size_t) cap - 1); msg[cap - 1] = 0; }
	return hl ? 1 : 0;
}

// ---- the OpenGL renderer (Windows): the N64 / GameCube frames drawn by the PC's GPU --------------------
// What the Pi's GPU does on Onyx (kapi_gpu_render): the triangles in clip space, the batch's matrix
// applied, colour = texel x colour + colour2, alpha test, blending, depth -- straight into the game
// window at its size (sharp, and no CPU for the pixels). The context lives on the game's thread
// (ne_gl_attach from there); the window only draws the bars. A picture drawn by the game's CPU (its
// framebuffer) is shown as a texture.
#ifdef _WIN32
#include <GL/gl.h>
typedef char GLchar;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_MIRRORED_REPEAT 0x8370
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_BGRA 0x80E1
#define GLF(ret, name, args) typedef ret (APIENTRY *PFN_##name) args; static PFN_##name name##_;
GLF (GLuint, glCreateShader, (GLenum))
GLF (void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))
GLF (void, glCompileShader, (GLuint))
GLF (void, glGetShaderiv, (GLuint, GLenum, GLint *))
GLF (void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
GLF (GLuint, glCreateProgram, (void))
GLF (void, glAttachShader, (GLuint, GLuint))
GLF (void, glBindAttribLocation, (GLuint, GLuint, const GLchar *))
GLF (void, glLinkProgram, (GLuint))
GLF (void, glGetProgramiv, (GLuint, GLenum, GLint *))
GLF (void, glUseProgram, (GLuint))
GLF (GLint, glGetUniformLocation, (GLuint, const GLchar *))
GLF (void, glUniform1i, (GLint, GLint))
GLF (void, glUniform1f, (GLint, GLfloat))
GLF (void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))
GLF (void, glEnableVertexAttribArray, (GLuint))
GLF (void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))
typedef BOOL (APIENTRY *PFN_wglSwapIntervalEXT) (int);

struct Gl
{
	HWND hwnd; HDC dc; HGLRC rc;
	GLuint prog; GLint uM, uTex, uUseTex, uAlphaRef;
	GLuint tex[1024]; bool have[1024];
	GLuint fbTex;
	unsigned *snap; int snapCap; unsigned snapTick;		// (N64: the picture read back)
	char err[256];
};

static const char *VS =
	"#version 120\n"
	"attribute vec4 aPos; attribute vec2 aTex; attribute vec4 aCol; attribute vec4 aCol2;\n"
	"uniform mat4 uM;\n"
	"varying vec2 vTex; varying vec4 vCol; varying vec4 vCol2;\n"
	"void main () { gl_Position = uM * aPos; vTex = aTex; vCol = aCol; vCol2 = aCol2; }\n";
static const char *FS =
	"#version 120\n"
	"uniform sampler2D uTex; uniform float uUseTex; uniform float uAlphaRef;\n"
	"varying vec2 vTex; varying vec4 vCol; varying vec4 vCol2;\n"
	"void main () {\n"
	"  vec4 c = vCol;\n"
	"  if (uUseTex > 0.5) c *= texture2D (uTex, vTex);\n"
	"  c = min (c + vCol2, vec4 (1.0));\n"
	"  if (c.a < uAlphaRef) discard;\n"
	"  gl_FragColor = c;\n"
	"}\n";

static void *glproc (const char *n)
{
	void *p = (void *) wglGetProcAddress (n);
	if (p == 0 || p == (void *) 1 || p == (void *) 2 || p == (void *) 3 || p == (void *) -1)
	{
		HMODULE m = GetModuleHandleA ("opengl32.dll");
		p = m ? (void *) GetProcAddress (m, n) : 0;
	}
	return p;
}
static GLuint gl_shader (Gl *g, GLenum type, const char *src)
{
	GLuint s = glCreateShader_ (type);
	glShaderSource_ (s, 1, &src, 0);
	glCompileShader_ (s);
	GLint ok = 0; glGetShaderiv_ (s, GL_COMPILE_STATUS, &ok);
	if (!ok) { glGetShaderInfoLog_ (s, sizeof g->err, 0, g->err); return 0; }
	return s;
}

static void gl_free (Emu *e)
{
	Gl *g = e->gl;
	if (!g) return;
	if (g->rc) { wglMakeCurrent (0, 0); wglDeleteContext (g->rc); }
	if (g->dc) ReleaseDC (g->hwnd, g->dc);
	free (g->snap);
	delete g; e->gl = 0;
}

// The game's window (its picture control) -> OpenGL on the calling thread (the game's). 1: done,
// 0: not available (err says why; the software renderer is used).
NE_API int ne_gl_attach (void *h, void *hwnd, char *err, int cap)
{
	Emu *e = (Emu *) h;
	gl_free (e);
	if (!hwnd) return 0;
	Gl *g = new Gl (); g->hwnd = (HWND) hwnd;
	auto fail = [&] (const char *why) { seterr (err, cap, g->err[0] ? g->err : why); gl_free (e); return 0; };
	e->gl = g;
	g->dc = GetDC (g->hwnd);
	PIXELFORMATDESCRIPTOR pfd; memset (&pfd, 0, sizeof pfd);
	pfd.nSize = sizeof pfd; pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.iLayerType = PFD_MAIN_PLANE;
	int pf = g->dc ? ChoosePixelFormat (g->dc, &pfd) : 0;
	if (!pf || !SetPixelFormat (g->dc, pf, &pfd)) return fail ("no OpenGL pixel format");
	g->rc = wglCreateContext (g->dc);
	if (!g->rc || !wglMakeCurrent (g->dc, g->rc)) return fail ("no OpenGL context");
#define GLL(name) name##_ = (PFN_##name) glproc (#name); if (!name##_) return fail ("OpenGL 2.0 is needed (" #name ")");
	GLL (glCreateShader) GLL (glShaderSource) GLL (glCompileShader) GLL (glGetShaderiv) GLL (glGetShaderInfoLog)
	GLL (glCreateProgram) GLL (glAttachShader) GLL (glBindAttribLocation) GLL (glLinkProgram) GLL (glGetProgramiv)
	GLL (glUseProgram) GLL (glGetUniformLocation) GLL (glUniform1i) GLL (glUniform1f) GLL (glUniformMatrix4fv)
	GLL (glEnableVertexAttribArray) GLL (glVertexAttribPointer)
	PFN_wglSwapIntervalEXT swap = (PFN_wglSwapIntervalEXT) glproc ("wglSwapIntervalEXT");
	if (swap) swap (0);						// (the game's thread keeps its own pace)
	GLuint vs = gl_shader (g, GL_VERTEX_SHADER, VS), fs = vs ? gl_shader (g, GL_FRAGMENT_SHADER, FS) : 0;
	if (!vs || !fs) return fail ("shader");
	g->prog = glCreateProgram_ ();
	glAttachShader_ (g->prog, vs); glAttachShader_ (g->prog, fs);
	glBindAttribLocation_ (g->prog, 0, "aPos"); glBindAttribLocation_ (g->prog, 1, "aTex");
	glBindAttribLocation_ (g->prog, 2, "aCol"); glBindAttribLocation_ (g->prog, 3, "aCol2");
	glLinkProgram_ (g->prog);
	GLint ok = 0; glGetProgramiv_ (g->prog, GL_LINK_STATUS, &ok);
	if (!ok) return fail ("shader link");
	glUseProgram_ (g->prog);
	g->uM = glGetUniformLocation_ (g->prog, "uM"); g->uTex = glGetUniformLocation_ (g->prog, "uTex");
	g->uUseTex = glGetUniformLocation_ (g->prog, "uUseTex"); g->uAlphaRef = glGetUniformLocation_ (g->prog, "uAlphaRef");
	glUniform1i_ (g->uTex, 0);
	glGenTextures (1024, g->tex); glGenTextures (1, &g->fbTex);
	for (int i = 0; i < 4; i++) glEnableVertexAttribArray_ ((GLuint) i);
	return 1;
}
NE_API void ne_gl_detach (void *h) { gl_free ((Emu *) h); }

// the picture's place in the window: its shape kept, centred (GL's y from the bottom)
static void gl_rect (Emu *e, int *x, int *y, int *w, int *h, int *W, int *H)
{
	RECT r; GetClientRect (e->gl->hwnd, &r);
	*W = r.right; *H = r.bottom;
	double a = ne_aspect1000 (e) / 1000.0;
	int pw = *W, ph = (int) (*W / a);
	if (ph > *H) { ph = *H; pw = (int) (*H * a); }
	*x = (*W - pw) / 2; *y = (*H - ph) / 2; *w = pw; *h = ph;
}

template <typename TX>
static void gl_textures (Gl *g, TX *tex, int n)
{
	for (int i = 0; i < n && i < 1024; i++)
	{
		TX &T = tex[i];
		if (T.w <= 0 || !T.px || (!T.dirty && g->have[i])) continue;
		glBindTexture (GL_TEXTURE_2D, g->tex[i]);
		glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, T.w, T.h, 0, GL_BGRA, GL_UNSIGNED_BYTE, T.px);
		T.dirty = false; g->have[i] = true;
	}
}

static const GLenum ZF[8] = { GL_LESS, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
static GLint gl_wrap (unsigned m) { return m == 1 ? GL_CLAMP_TO_EDGE : m == 2 ? GL_MIRRORED_REPEAT : GL_REPEAT; }

template <typename FR, typename TX>
static void gl_frame (Emu *e, const FR &fr, TX *tex, int ntex)
{
	Gl *g = e->gl;
	int x, y, w, h, W, H; gl_rect (e, &x, &y, &w, &h, &W, &H);
	gl_textures (g, tex, ntex);
	glDisable (GL_SCISSOR_TEST);
	glViewport (0, 0, W, H);
	glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT);		// (the bars)
	glEnable (GL_SCISSOR_TEST); glScissor (x, H - y - h, w, h);
	glViewport (x, H - y - h, w, h);
	glClearColor (((fr.clear >> 16) & 255) / 255.0f, ((fr.clear >> 8) & 255) / 255.0f, (fr.clear & 255) / 255.0f, 1);
	glDepthMask (GL_TRUE); glClearDepth (1.0);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable (GL_DEPTH_TEST);
	const char *v = (const char *) fr.v;
	glVertexAttribPointer_ (0, 4, GL_FLOAT, GL_FALSE, 32, v);
	glVertexAttribPointer_ (1, 2, GL_FLOAT, GL_FALSE, 32, v + 16);
	glVertexAttribPointer_ (2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, v + 24);
	glVertexAttribPointer_ (3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, v + 28);
	static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	for (int i = 0; i < fr.nb; i++)
	{
		const auto &B = fr.b[i];
		if (B.count < 3 || (int) (B.first + B.count) > fr.nv) continue;
		unsigned f = B.flags;
		glUniformMatrix4fv_ (g->uM, 1, GL_TRUE, (f & bas::G3_NOMATRIX) ? I : B.m);	// (row-major)
		bool t = B.tex >= 0 && B.tex < ntex && B.tex < 1024 && g->have[B.tex];
		glUniform1f_ (g->uUseTex, t ? 1.0f : 0.0f);
		if (t)
		{
			glBindTexture (GL_TEXTURE_2D, g->tex[B.tex]);
			GLint fl = (f & bas::G3_LINEAR) ? GL_LINEAR : GL_NEAREST;
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fl);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fl);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap ((f >> bas::G3_WRAP_S_SHIFT) & 3));
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap ((f >> bas::G3_WRAP_T_SHIFT) & 3));
		}
		glUniform1f_ (g->uAlphaRef, (f & bas::G3_ALPHATEST) ? (float) ((f >> 19) & 255) / 255.0f - 0.5f / 255 : -1.0f);
		unsigned zf = f & bas::G3_ZFUNC;
		glDepthFunc (ZF[zf]);
		glDepthMask ((f & bas::G3_NOZWRITE) || zf == 7 ? GL_FALSE : GL_TRUE);
		if (f & (bas::G3_CULL_BACK | bas::G3_CULL_FRONT)) { glEnable (GL_CULL_FACE); glCullFace ((f & bas::G3_CULL_BACK) ? GL_BACK : GL_FRONT); }
		else glDisable (GL_CULL_FACE);
		switch ((f >> bas::G3_BLEND_SHIFT) & 15)
		{
		case 1: glEnable (GL_BLEND); glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
		case 2: glEnable (GL_BLEND); glBlendFunc (GL_SRC_ALPHA, GL_ONE); break;
		case 3: glEnable (GL_BLEND); glBlendFunc (GL_DST_COLOR, GL_ZERO); break;
		case 4: glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
		default: glDisable (GL_BLEND); break;
		}
		glDrawArrays (GL_TRIANGLES, (GLint) B.first, (GLsizei) (B.count / 3 * 3));
	}
	// (N64: the picture handed back to the machine every 4th frame -- a game may read its
	// framebuffer, Ocarina of Time's pause background)
	if (e->n64 && (++g->snapTick & 3) == 0)
	{
		if (g->snapCap < w * h) { free (g->snap); g->snap = (unsigned *) malloc ((size_t) w * h * 4); g->snapCap = g->snap ? w * h : 0; }
		if (g->snap)
		{
			glReadPixels (x, H - y - h, w, h, GL_BGRA, GL_UNSIGNED_BYTE, g->snap);
			e->n64->fbSnapshot (g->snap + (long) (h - 1) * w, w, h, -w);	// (bottom-up rows)
		}
	}
	SwapBuffers (g->dc);
}

// the game's CPU-drawn picture (its framebuffer), as a texture over the picture's place
static void gl_fb (Emu *e, const unsigned *px, int fw, int fh)
{
	Gl *g = e->gl;
	int x, y, w, h, W, H; gl_rect (e, &x, &y, &w, &h, &W, &H);
	glDisable (GL_SCISSOR_TEST); glDisable (GL_DEPTH_TEST); glDisable (GL_BLEND); glDisable (GL_CULL_FACE);
	glViewport (0, 0, W, H);
	glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT);
	glViewport (x, H - y - h, w, h);
	glBindTexture (GL_TEXTURE_2D, g->fbTex);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0, GL_BGRA, GL_UNSIGNED_BYTE, px);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	struct V { float x, y, z, w, s, t; unsigned char c[4], c2[4]; };
	static const V q[6] = {
		{ -1, 1, 0, 1, 0, 0, {255,255,255,255}, {0,0,0,255} }, { 1, 1, 0, 1, 1, 0, {255,255,255,255}, {0,0,0,255} },
		{ 1, -1, 0, 1, 1, 1, {255,255,255,255}, {0,0,0,255} }, { -1, 1, 0, 1, 0, 0, {255,255,255,255}, {0,0,0,255} },
		{ 1, -1, 0, 1, 1, 1, {255,255,255,255}, {0,0,0,255} }, { -1, -1, 0, 1, 0, 1, {255,255,255,255}, {0,0,0,255} } };
	const char *v = (const char *) q;
	glVertexAttribPointer_ (0, 4, GL_FLOAT, GL_FALSE, 32, v);
	glVertexAttribPointer_ (1, 2, GL_FLOAT, GL_FALSE, 32, v + 16);
	glVertexAttribPointer_ (2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, v + 24);
	glVertexAttribPointer_ (3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, v + 28);
	static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	glUniformMatrix4fv_ (g->uM, 1, GL_FALSE, I);
	glUniform1f_ (g->uUseTex, 1.0f); glUniform1f_ (g->uAlphaRef, -1.0f);
	glDrawArrays (GL_TRIANGLES, 0, 6);
	SwapBuffers (g->dc);
}

static bool gl_draw (Emu *e)
{
	if (e->n64)
	{
		if (e->gfxAge < 30 && e->n64->gfxReady >= 0) gl_frame (e, e->n64->gfxFrame[e->n64->gfxReady], e->n64->tex, n64::Machine::MAX_TEX);
		else gl_fb (e, e->n64->fb, e->n64->fbW, e->n64->fbH);
	}
	else
	{
		if (e->gfxAge < 30 && e->gc->gfxReady >= 0) gl_frame (e, e->gc->gfxFrame[e->gc->gfxReady], e->gc->tex, gc::Machine::MAX_TEX);
		else gl_fb (e, e->gc->fb, e->gc->fbW, e->gc->fbH);
	}
	e->lock.take (); e->serial++; e->lock.give ();		// (a new picture: for the speed)
	return true;
}
#else
struct Gl {};
static bool gl_draw (Emu *) { return false; }
static void gl_free (Emu *) {}
NE_API int ne_gl_attach (void *, void *, char *err, int cap) { seterr (err, cap, "no OpenGL here"); return 0; }
NE_API void ne_gl_detach (void *) {}
#endif

// the last picture again (paused: the window was covered): OpenGL only
NE_API void ne_redraw (void *h) { Emu *e = (Emu *) h; if (e && e->gl) gl_draw (e); }

// ---- the library's pictures ------------------------------------------------------------------------------
// A picture for the library (160 x 144): a 2D game run ~7 s unseen, its screen kept (-> 1); a
// Nintendo 64 game: its header's name (-> 2, the front end draws a label); a GameCube disc: its
// banner (96 x 32 into banner, -> 3) and its full name. 0: not readable.
static unsigned be32 (const unsigned char *p) { return (unsigned) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static bool fread_at (FILE *f, unsigned off, void *dst, unsigned n) { return u8seek (f, off) && fread (dst, 1, n, f) == n; }

NE_API int ne_thumb (const char *path, unsigned *px, unsigned *banner, char *name, int cap)
{
	int sys = ne_system_of (path);
	name[0] = 0;
	if (sys == NE_N64)
	{
		FILE *f = u8open (path, "rb"); if (!f) return 0;
		unsigned char h[64]; bool ok = fread (h, 1, 64, f) == 64; fclose (f);
		if (!ok) return 0;
		int n = 0;
		for (int i = 0; i < 20 && n < cap - 1; i++)
		{
			int k = 0x20 + i;
			if (h[0] == 0x37) k ^= 1; else if (h[0] == 0x40) k ^= 3;
			name[n++] = h[k] >= ' ' && h[k] < 127 ? (char) h[k] : ' ';
		}
		while (n > 0 && name[n - 1] == ' ') n--;
		name[n] = 0;
		return 2;
	}
	if (sys == NE_GC)
	{
		if (ends (path, ".dol")) return 2;
		FILE *f = u8open (path, "rb"); if (!f) return 0;
		static unsigned char hdr[0x440];
		unsigned char *fst = 0, *bnr = 0;
		if (!fread_at (f, 0, hdr, sizeof hdr) || be32 (hdr + 0x1C) != 0xC2339F3D) { fclose (f); return 0; }
		unsigned fstOff = be32 (hdr + 0x424), fstSize = be32 (hdr + 0x428);
		if (fstSize > 0 && fstSize < 0x100000)
		{
			fst = (unsigned char *) malloc (fstSize);
			if (fst && !fread_at (f, fstOff, fst, fstSize)) { free (fst); fst = 0; }
		}
		if (fst)
		{
			unsigned n = be32 (fst + 8);
			const char *names = (const char *) fst + n * 12;
			for (unsigned i = 1; i < n && (i + 1) * 12 <= fstSize; i++)
			{
				const unsigned char *en = fst + i * 12;
				if (en[0] != 0) continue;
				unsigned no = be32 (en) & 0xFFFFFF;
				if ((const unsigned char *) names + no + 12 > fst + fstSize) continue;
				const char *nm = names + no; const char *want = "opening.bnr"; int k = 0;
				while (want[k] && low (nm[k]) == want[k]) k++;
				if (want[k] || nm[k]) continue;
				bnr = (unsigned char *) malloc (0x1960);
				if (bnr && !fread_at (f, be32 (en + 4), bnr, 0x1960)) { free (bnr); bnr = 0; }
				break;
			}
			free (fst);
		}
		fclose (f);
		int nl = 0;
		if (bnr)
		{
			for (int ty = 0; ty < 32; ty += 4)
				for (int tx = 0; tx < 96; tx += 4)
					for (int y = 0; y < 4; y++)
						for (int x = 0; x < 4; x++)
						{
							const unsigned char *q = bnr + 0x20 + ((ty / 4) * 24 + tx / 4) * 32 + (y * 4 + x) * 2;
							unsigned v = (unsigned) q[0] << 8 | q[1], col;
							if (v & 0x8000) col = 0xFF000000u | ((v >> 10) & 31) << 19 | ((v >> 5) & 31) << 11 | (v & 31) << 3;
							else
							{
								unsigned a = (v >> 12) & 7;
								col = (a * 255 / 7) << 24 | ((v >> 8) & 15) * 17 << 16 | ((v >> 4) & 15) * 17 << 8 | (v & 15) * 17;
							}
							banner[(ty + y) * 96 + tx + x] = col;
						}
			for (; nl < 64 && nl < cap - 1 && bnr[0x1860 + nl] >= ' '; nl++) name[nl] = (char) bnr[0x1860 + nl];
			if (!nl) for (; nl < 32 && nl < cap - 1 && bnr[0x1820 + nl] >= ' '; nl++) name[nl] = (char) bnr[0x1820 + nl];
			free (bnr);
		}
		if (!nl) for (; nl < 64 && nl < cap - 1 && hdr[0x20 + nl] >= ' '; nl++) name[nl] = (char) hdr[0x20 + nl];
		name[nl] = 0;
		return bnr ? 3 : 2;
	}
	if (!sys) return 0;
	char err[8];
	Emu *e = (Emu *) ne_open (path, 8000, err, sizeof err);
	if (!e) return 0;
	unsigned char none[256]; memset (none, 0, sizeof none); ne_set_keys (e, none);
	static short pcm[4096];
	for (int i = 0; i < 420; i++)
	{
		switch (e->sys)
		{
		case NE_GB: case NE_GBC: e->gb->runFrame (); break;
		case NE_GBA: e->gba->runFrame (); break;
		case NE_NES: e->nes->runFrame (); break;
		case NE_SNES: e->snes->runFrame (); break;
		}
		ne_audio (e, pcm, 2048);
	}
	enum { TW = 160, TH = 144 };
	for (int i = 0; i < TW * TH; i++) px[i] = 0;
	if (e->gba)							// 240 x 160 -> 160 x 107, centred
		for (int y = 0, oy = (TH - 107) / 2; y < 107; y++)
			for (int x = 0; x < TW; x++) px[(oy + y) * TW + x] = e->gba->fb[(y * 160 / 107) * gba::W + x * 3 / 2];
	else if (e->snes)						// 256 x 224 -> 160 x 140
		for (int y = 0, oy = (TH - 140) / 2; y < 140; y++)
			for (int x = 0; x < TW; x++) px[(oy + y) * TW + x] = e->snes->fb[(y * 224 / 140) * snes::W + x * 256 / TW];
	else if (e->nes)						// 256 x 240 -> 154 x 144
		for (int y = 0, ox = (TW - 154) / 2; y < TH; y++)
			for (int x = 0; x < 154; x++) px[y * TW + ox + x] = e->nes->fb[(y * 240 / TH) * nes::W + x * 256 / 154];
	else for (int i = 0; i < TW * TH; i++) px[i] = e->gb->fb[i];
	for (int i = 0; i < TW * TH; i++) px[i] |= 0xFF000000u;
	ne_title (e, name, cap);
	// (the thumbnail's run must not touch the game's save)
	if (e->gb) e->gb->sramDirty = false;
	if (e->gba) e->gba->saveDirty = false;
	if (e->nes) e->nes->sramDirty = false;
	if (e->snes) e->snes->sramDirty = false;
	ne_close (e);
	return 1;
}

// ---- the sound out (Windows: waveOut, a queue of small buffers) -------------------------------------------
#ifdef _WIN32
enum { NBUF = 32, BUF_FRAMES = 512 };
static HWAVEOUT s_wo = 0; static WAVEHDR s_hdr[NBUF]; static short s_mem[NBUF][BUF_FRAMES * 2];
static bool s_sent[NBUF]; static int s_cur = 0, s_fill = 0, s_rate = 44100;
#endif

NE_API int ne_audio_open (int rate)
{
#ifdef _WIN32
	if (s_wo) return 1;
	WAVEFORMATEX wf; memset (&wf, 0, sizeof wf);
	wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = (DWORD) rate; wf.wBitsPerSample = 16;
	wf.nBlockAlign = 4; wf.nAvgBytesPerSec = (DWORD) rate * 4;
	if (waveOutOpen (&s_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { s_wo = 0; return 0; }
	s_rate = rate; s_cur = 0; s_fill = 0;
	for (int i = 0; i < NBUF; i++) { memset (&s_hdr[i], 0, sizeof s_hdr[i]); s_sent[i] = false; }
	return 1;
#else
	(void) rate; return 0;
#endif
}
#ifdef _WIN32
static bool buf_busy (int i) { return s_sent[i] && !(s_hdr[i].dwFlags & WHDR_DONE); }
#endif
// the sound queued, in frames (not played yet)
NE_API int ne_audio_queued (void)
{
#ifdef _WIN32
	if (!s_wo) return 0;
	int n = 0;
	for (int i = 0; i < NBUF; i++) if (buf_busy (i)) n += BUF_FRAMES;
	return n + s_fill;
#else
	return 0;
#endif
}
NE_API void ne_audio_write (const short *lr, int frames)
{
#ifdef _WIN32
	if (!s_wo) return;
	while (frames > 0)
	{
		if (s_fill == 0 && buf_busy (s_cur)) return;		// full: dropped
		int k = BUF_FRAMES - s_fill; if (k > frames) k = frames;
		memcpy (&s_mem[s_cur][s_fill * 2], lr, (size_t) k * 4);
		s_fill += k; lr += k * 2; frames -= k;
		if (s_fill == BUF_FRAMES)
		{
			WAVEHDR &hd = s_hdr[s_cur];
			if (s_sent[s_cur]) waveOutUnprepareHeader (s_wo, &hd, sizeof hd);
			memset (&hd, 0, sizeof hd);
			hd.lpData = (LPSTR) s_mem[s_cur]; hd.dwBufferLength = BUF_FRAMES * 4;
			waveOutPrepareHeader (s_wo, &hd, sizeof hd);
			waveOutWrite (s_wo, &hd, sizeof hd);
			s_sent[s_cur] = true;
			s_cur = (s_cur + 1) % NBUF; s_fill = 0;
		}
	}
#else
	(void) lr; (void) frames;
#endif
}
NE_API void ne_audio_close (void)
{
#ifdef _WIN32
	if (!s_wo) return;
	waveOutReset (s_wo);
	for (int i = 0; i < NBUF; i++) if (s_sent[i]) waveOutUnprepareHeader (s_wo, &s_hdr[i], sizeof s_hdr[i]);
	waveOutClose (s_wo); s_wo = 0;
#endif
}

// a pad for the front end's own use (the library's navigation): PAD_* bits, 0 none
NE_API unsigned ne_pad_buttons (int port) { Pad p = read_pad (port); return p.on ? dpad_from_stick (p) : 0; }
