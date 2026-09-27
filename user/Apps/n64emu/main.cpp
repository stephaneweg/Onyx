//
// n64emu -- the Onyx Nintendo 64 emulator (the core: user/n64).
//
//   n64emu <rom.z64 | rom.n64 | rom.v64> [--fullscreen]   (without a ROM: the Game Library)
//   * The machine (the R4300 CPU, the RCP) runs on an app core (core 2 or 3, user/emucore.h)
//     when one is free. Its graphics are high-level: each frame's triangles and textures are
//     drawn by the GPU (kapi v53 gpu_render) straight into the window, at the window's size
//     (sharper than the console's 320 x 240). A game that draws its picture with the CPU is
//     shown from its framebuffer.
//   * Keys: arrows = the stick, X = A, C = B, Z = Z, Enter = Start, Q / W = L / R, I J K L =
//     the C buttons, T F G H = the D-pad; a USB gamepad (user/gamepad.h): the left stick, A
//     (bottom) = A, X (left) = B, L2 / R2 = Z, L / R, Start, the right stick = the C buttons,
//     the D-pad. F11: full screen (Esc back; the GPU then renders straight into the displayed
//     framebuffer, kapi v55, at the screen's resolution), F12: the speed, P: pause.
//   * The cartridge's save (SRAM / EEPROM) is <rom>.sav beside the ROM.
//   * The sound: the audio tasks of Zelda Ocarina of Time / Majora's Mask (their microcode, at a
//     high level: user/n64/n64_audio.cpp); other games run silent. With sound, the pace is the
//     audio queue's (as snesemu), else the clock. Sound > Sound On / Off.
//
#include "kapi.h"
#include "launch.h"
#include "gamepad.h"
#include "wtk/wtk.h"
#include "n64/n64.h"
#include "wtk/dialog.h"
#include "emucore.h"
#include "basic/bas3d.h"

using namespace wtk;

static n64::Machine *g_m = 0;
static unsigned char *g_rom = 0;
static char g_rom_path[256], g_sav_path[260];
static int g_zoom = 2;
static bool g_paused = false;
static bool g_sound = true;
static int g_audio = 0;						// 0 not tried, 1 ours, -1 none
static volatile bool g_audioOn = false;				// the machine's sound is kept
static unsigned *g_fs = 0; static int g_fsw, g_fsh, g_fsStride;	// (full screen: the back buffer, or the screen itself)
static Root *g_root = 0;
static bool g_stats = false;
static char g_statText[128] = "";
static EmuCore g_ec;
static int g_stride;
static bool g_loading = true;
static unsigned g_loadDone = 0, g_loadSize = 1;
static char g_loadName[64];
static bool g_gpu = false;					// kapi v53 usable
static bool g_gpuOn = true;					// View > Draw with the GPU (off: the software renderer)
static int g_gpuTex[n64::Machine::MAX_TEX];			// the machine's texture -> the GPU's handle
static unsigned g_lastSerial = 0, g_gfxAge = 1000;		// frames since the last graphics frame
static int g_fbW = 320, g_fbH = 240;

static inline unsigned long long now_us (void)
{
	unsigned long long c, f;
	asm volatile ("mrs %0, cntpct_el0" : "=r" (c));
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? c * 1000000ull / f : 0;
}
static void fmt_num (char *d, int *n, unsigned v, int decimals)
{
	char t[16]; int k = 0;
	unsigned ip, fp, div = 1;
	for (int i = 0; i < decimals; i++) div *= 10;
	ip = v / div; fp = v % div;
	do { t[k++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	while (k) d[(*n)++] = t[--k];
	if (decimals) { d[(*n)++] = '.'; for (unsigned m = div / 10; m; m /= 10) d[(*n)++] = (char) ('0' + (fp / m) % 10); }
	d[*n] = 0;
}
static void cat (char *d, int *n, const char *s) { while (*s) d[(*n)++] = *s++; d[*n] = 0; }
static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static unsigned fps100 (void) { return g_m && g_m->pal ? 5000 : 6000; }

// ---- the saves ------------------------------------------------------------------------------------------------------
static void save_ram (void)
{
	if (!g_m) return;
	if (g_m->saveType == 1 && g_m->sramDirty)
	{ if (kapi_save_file (g_sav_path, (const char *) g_m->sram, 0x8000) >= 0) g_m->sramDirty = false; }
	else if (g_m->saveType >= 2 && g_m->eepromDirty)
	{ if (kapi_save_file (g_sav_path, (const char *) g_m->eeprom, (unsigned) g_m->eepromSize) >= 0) g_m->eepromDirty = false; }
}
static void load_ram (void)
{
	void *f = kapi_open (g_sav_path);
	if (!f) return;
	unsigned n = kapi_fsize (f);
	if (n == 512 || n == 2048)				// an EEPROM
	{
		kapi_read (f, g_m->eeprom, n);
		g_m->eepromSize = (int) n; g_m->saveType = n == 512 ? 2 : 3;
	}
	else kapi_read (f, g_m->sram, n < sizeof g_m->sram ? n : sizeof g_m->sram);
	kapi_close (f);
}

// ---- drawing a frame ---------------------------------------------------------------------------------------------
// The last graphics frame by the GPU into pixels (w x h, stride): its new textures uploaded first.
static bool gpu_frame (unsigned *px, int w, int h, int stride)
{
	if (!g_gpu || g_m->gfxReady < 0) return false;
	const n64::GFrame &F = g_m->gfxFrame[g_m->gfxReady];
	for (int i = 0; i < n64::Machine::MAX_TEX; i++)
	{
		n64::GTexture &T = g_m->tex[i];
		if (!T.dirty || T.w <= 0) continue;
		int h2 = kapi_gpu_texture (g_gpuTex[i], T.px, T.w, T.h, T.w);
		if (h2 < 0 && g_gpuTex[i] >= 0) h2 = kapi_gpu_texture (-1, T.px, T.w, T.h, T.w);
		g_gpuTex[i] = h2;
		T.dirty = false;
	}
	static struct kapi_gpu_batch bt[n64::GFrame::MAXB];
	int nb = 0;
	for (int i = 0; i < F.nb; i++)
	{
		const n64::GBatch &B = F.b[i];
		struct kapi_gpu_batch &o = bt[nb];
		o.first = B.first; o.count = B.count; o.flags = B.flags;
		o.texture = B.tex >= 0 ? g_gpuTex[B.tex] : -1;
		if (B.tex >= 0 && o.texture < 0) continue;		// (its texture could not be made)
		for (int k = 0; k < 16; k++) o.matrix[k] = B.m[k];
		nb++;
	}
	struct kapi_gpu_frame fr = { px, w, h, stride, F.clear, 0 };
	return kapi_gpu_render (&fr, (const struct kapi_gpu_vertex3 *) F.v, (unsigned) F.nv, bt, (unsigned) nb) == 0;
}

// The CPU's framebuffer (the VI's picture), scaled into pixels.
static void fb_frame (unsigned *px, int w, int h, int stride)
{
	const unsigned *s = ec_front (&g_ec);
	int sw = g_fbW, sh = g_fbH;
	if (sw <= 0 || sh <= 0) return;
	for (int y = 0; y < h; y++)
	{
		const unsigned *r = s + (y * sh / h) * sw;
		unsigned *d = px + (long) y * stride;
		for (int x = 0; x < w; x++) d[x] = r[x * sw / w];
	}
}

// The last graphics frame by the software renderer of the BASIC 3D (View > Draw with the GPU off:
// slow, to tell a GPU problem from another one), at the console's size, then scaled.
static bool sw_frame (unsigned *px, int w, int h, int stride)
{
	if (g_m->gfxReady < 0) return false;
	const n64::GFrame &F = g_m->gfxFrame[g_m->gfxReady];
	int W = F.width, H = F.height;
	if (W <= 0 || H <= 0 || W > n64::FB_MAX_W || H > n64::FB_MAX_H) return false;
	static unsigned *pic = 0; static float *zb = 0;
	if (!pic) { pic = new unsigned[n64::FB_MAX_W * n64::FB_MAX_H]; zb = new float[n64::FB_MAX_W * n64::FB_MAX_H]; }
	static bas::G3Batch bt[n64::GFrame::MAXB];
	static bas::G3Texture tx[n64::Machine::MAX_TEX];
	for (int i = 0; i < F.nb; i++)
	{
		const n64::GBatch &B = F.b[i];
		bt[i].first = B.first; bt[i].count = B.count; bt[i].flags = B.flags;
		bt[i].texture = B.tex >= 0 ? B.tex + 1 : 0;
		for (int k = 0; k < 16; k++) bt[i].m[k] = B.m[k];
	}
	for (int i = 0; i < n64::Machine::MAX_TEX; i++) { tx[i].px = g_m->tex[i].px; tx[i].w = g_m->tex[i].w; tx[i].h = g_m->tex[i].h; }
	bas::swRender (pic, W, H, W, zb, (const bas::G3Vertex *) F.v, F.nv, bt, F.nb, F.clear, false,
		       [] (int t) -> const bas::G3Texture * { return &tx[t - 1]; });
	for (int y = 0; y < h; y++)
	{
		const unsigned *r = pic + (y * H / h) * W;
		unsigned *d = px + (long) y * stride;
		for (int x = 0; x < w; x++) d[x] = r[x * W / w];
	}
	return true;
}

static void draw_into (unsigned *px, int w, int h, int stride)
{
	if (g_gfxAge < 30 && g_gpuOn && gpu_frame (px, w, h, stride)) return;
	if (g_gfxAge < 30 && (!g_gpuOn || !g_gpu) && sw_frame (px, w, h, stride)) return;
	fb_frame (px, w, h, stride);
}

class EmuRoot : public Root
{
public:
	EmuRoot (int w, int h, const char *t) : Root (w, h, t) {}
	void onDraw () override
	{
		if (g_loading)
		{
			canvas.clear (0);
			canvas.text (8, height / 2 - 24, "Loading...", 0xFFFFFF);
			canvas.text (8, height / 2 - 6, g_loadName, 0xA0A0A0);
			int bw = width - 16;
			canvas.fillRect (8, height / 2 + 14, bw, 6, 0x404040);
			canvas.fillRect (8, height / 2 + 14, (int) ((long long) bw * g_loadDone / g_loadSize), 6, 0x40C040);
			return;
		}
		draw_into (canvas.px, width, height, canvas.stride);
		if (g_paused) canvas.text (8, 8, "Paused", 0xFFFFFF);
		if (g_stats) { canvas.fillRect (0, height - 20, width, 20, 0); canvas.text (4, height - 18, g_statText, 0x00FFFF60); }
	}
	bool onKey (long k) override;
};

static void full_screen (bool on)
{
	if (on && !g_fs)
	{
		g_fs = kapi_fullscreen_begin (&g_fsw, &g_fsh);
		g_fsStride = g_fsw;
		if (g_fs)
		{
			// the GPU renders straight into the displayed framebuffer (v55): no copy a frame
			int w, h, st;
			unsigned *scr = kapi_fullscreen_direct (&w, &h, &st);
			if (scr) { g_fs = scr; g_fsw = w; g_fsh = h; g_fsStride = st; }
			for (int y = 0; y < g_fsh; y++) for (int x = 0; x < g_fsw; x++) g_fs[(long) y * g_fsStride + x] = 0;
		}
	}
	else if (!on && g_fs) { kapi_fullscreen_end (); g_fs = 0; g_root->invalidate (true); }
}
static void show_frame (void)
{
	if (g_fs)
	{
		int ow = g_fsw, oh = g_fsw * 3 / 4;				// 4:3, centred
		if (oh > g_fsh) { oh = g_fsh; ow = g_fsh * 4 / 3; }
		unsigned *p = g_fs + (long) ((g_fsh - oh) / 2) * g_fsStride + (g_fsw - ow) / 2;
		draw_into (p, ow, oh, g_fsStride);
		if (g_stats) { Canvas c; c.adopt (g_fs, g_fsw, g_fsh, g_fsStride); c.fillRect (0, g_fsh - 20, g_fsw, 20, 0); c.text (4, g_fsh - 18, g_statText, 0x00FFFF60); }
		kapi_present_fb ();
	}
	else { g_root->invalidate (true); g_root->draw (); kapi_present (); }
}

static void set_zoom (int z)
{
	g_zoom = z;
	g_root->canvas.adopt (kapi_resize_window (320 * z, 240 * z), 320 * z, 240 * z, g_stride);
	wtk::wk_decorate_window ();					// the frame follows
	g_root->width = 320 * z; g_root->height = 240 * z;
	g_root->invalidate (true);
}
static void on_zoom1 () { set_zoom (1); }
static void on_zoom2 () { set_zoom (2); }
static void on_zoom3 () { set_zoom (3); }
static void on_full () { full_screen (!g_fs); }
static void on_sound ()
{
	g_sound = !g_sound;
	if (!g_sound && g_audio == 1) { g_audioOn = false; kapi_sound_release (); g_audio = 0; }
}
static void on_pause () { g_paused = !g_paused; g_root->invalidate (true); }
static void on_gpu () { g_gpuOn = !g_gpuOn; g_root->invalidate (true); }
static void on_stats () { g_stats = !g_stats; g_root->invalidate (true); }
static void on_reset () { ec_hold (&g_ec); save_ram (); g_m->reset (); load_ram (); ec_resume (&g_ec); }
static void on_quit () { kapi_exit (0); }

bool EmuRoot::onKey (long k)
{
	if (g_loading) return Root::onKey (k);
	if (k == KEY_F1 + 10) { on_full (); return true; }
	if (k == 27 && g_fs) { full_screen (false); return true; }
	if (k == 'p' || k == 'P') { on_pause (); return true; }
	if (k == KEY_F1 + 11) { on_stats (); return true; }
	return Root::onKey (k);
}

// the controller: the buttons (16 bits), the stick x, y (bytes) packed for the machine
static int pad_state (void)
{
	int b = 0, x = 0, y = 0;
	if (kapi_key_held (KEY_RIGHT)) x += 80;
	if (kapi_key_held (KEY_LEFT)) x -= 80;
	if (kapi_key_held (KEY_UP)) y += 80;
	if (kapi_key_held (KEY_DOWN)) y -= 80;
	if (kapi_key_held ('x')) b |= n64::BTN_A;
	if (kapi_key_held ('c')) b |= n64::BTN_B;
	if (kapi_key_held ('z')) b |= n64::BTN_Z;
	if (kapi_key_held (KEY_ENTER)) b |= n64::BTN_START;
	if (kapi_key_held ('q')) b |= n64::BTN_L;
	if (kapi_key_held ('w')) b |= n64::BTN_R;
	if (kapi_key_held ('i')) b |= n64::BTN_CUP;
	if (kapi_key_held ('k')) b |= n64::BTN_CDOWN;
	if (kapi_key_held ('j')) b |= n64::BTN_CLEFT;
	if (kapi_key_held ('l')) b |= n64::BTN_CRIGHT;
	if (kapi_key_held ('t')) b |= n64::BTN_DUP;
	if (kapi_key_held ('g')) b |= n64::BTN_DDOWN;
	if (kapi_key_held ('f')) b |= n64::BTN_DLEFT;
	if (kapi_key_held ('h')) b |= n64::BTN_DRIGHT;
	struct pad_input in;
	if (pad_read (0, &in))
	{
		unsigned p = in.buttons;
		if (p & PAD_A) b |= n64::BTN_A;
		if (p & PAD_X) b |= n64::BTN_B;
		if (p & (PAD_L2 | PAD_R2)) b |= n64::BTN_Z;
		if (p & PAD_L) b |= n64::BTN_L;
		if (p & PAD_R) b |= n64::BTN_R;
		if (p & PAD_START) b |= n64::BTN_START;
		if (p & PAD_UP) b |= n64::BTN_DUP;
		if (p & PAD_DOWN) b |= n64::BTN_DDOWN;
		if (p & PAD_LEFT) b |= n64::BTN_DLEFT;
		if (p & PAD_RIGHT) b |= n64::BTN_DRIGHT;
		if (p & PAD_Y) b |= n64::BTN_CUP;
		if (p & PAD_B) b |= n64::BTN_CRIGHT;
		if (in.rx > 500) b |= n64::BTN_CRIGHT;
		if (in.rx < -500) b |= n64::BTN_CLEFT;
		if (in.ry > 500) b |= n64::BTN_CDOWN;
		if (in.ry < -500) b |= n64::BTN_CUP;
		if (in.lx || in.ly) { x = in.lx * 80 / 1000; y = -in.ly * 80 / 1000; }
	}
	return (b & 0xFFFF) | (x & 0xFF) << 16 | (y & 0xFF) << 24;
}

// One frame of the machine: on the app core (no kapi call, no allocation here).
static void n64_frame (EmuCore *ec)
{
	int p = ec->btn;
	g_m->setPad (0, (unsigned) p & 0xFFFF, (signed char) (p >> 16), (signed char) (p >> 24));
	g_m->runFrame ();
	unsigned *d = ec_back (ec);
	int n = g_m->fbW * g_m->fbH;
	for (int i = 0; i < n; i++) d[i] = g_m->fb[i];
	ec_publish (ec);
	static short pcm[4096 * 2];
	int k;
	while ((k = g_m->audioRead (pcm, 4096)) > 0) if (g_audioOn) ec_audio_push (ec, pcm, k);
}

int main (void)
{
	char args[256] = "";
	kapi_get_args (args, sizeof args);
	int i = 0; while (args[i] == ' ') i++;
	int n = 0;
	bool wantFull = false;
	if (args[i] == '"') { i++; while (args[i] && args[i] != '"' && n < 255) g_rom_path[n++] = args[i++]; if (args[i]) i++; }
	else while (args[i] && n < 255 && !(args[i] == ' ' && args[i + 1] == '-' && args[i + 2] == '-')) g_rom_path[n++] = args[i++];
	while (n > 0 && g_rom_path[n - 1] == ' ') n--;
	g_rom_path[n] = 0;
	if (!g_rom_path[0]) { lx_launch ("gamelib", ""); return 0; }
	for (; args[i]; i++)
		if (args[i] == '-' && args[i + 1] == '-' && args[i + 2] == 'f' && args[i + 3] == 'u' && args[i + 4] == 'l' && args[i + 5] == 'l') wantFull = true;

	void *f = kapi_open (g_rom_path);
	if (!f) return 1;
	unsigned sz = kapi_fsize (f);
	g_rom = new unsigned char[sz + 1];
	int r = kapi_read (f, g_rom, sz < 16 ? sz : 16);
	if (r <= 0) { kapi_close (f); return 1; }
	char title[64];
	{ int b = slen (g_rom_path); while (b > 0 && g_rom_path[b - 1] != '/' && g_rom_path[b - 1] != ':') b--; scpy (g_loadName, g_rom_path + b, sizeof g_loadName); }
	{ scpy (title, g_loadName, sizeof title); int e = slen (title); while (e > 0 && title[e - 1] != '.') e--; if (e > 1) title[e - 1] = 0; }
	g_stride = 320 * 3;
	EmuRoot root (320 * 3, 240 * 3, title);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	set_zoom (g_zoom);
	root.attach ();
	g_loadSize = sz ? sz : 1; g_loadDone = (unsigned) r;
	root.invalidate (true); root.draw (); kapi_present ();
	while ((unsigned) r < sz)
	{
		unsigned k = sz - (unsigned) r > 0x100000 ? 0x100000 : sz - (unsigned) r;
		int got = kapi_read (f, g_rom + r, k);
		if (got <= 0) break;
		r += got; g_loadDone = (unsigned) r;
		pump_events ();
		if (should_exit ()) { kapi_close (f); return 0; }
		root.invalidate (true); root.draw (); kapi_present ();
	}
	kapi_close (f);
	g_m = new n64::Machine;
	if (!g_m->load (g_rom, (unsigned) r))
	{
		g_loading = false;
		wk_messagebox ("N64", "Not a Nintendo 64 ROM (.z64 / .n64 / .v64).", MB_OK);
		return 1;
	}
	delete [] g_rom; g_rom = 0;					// (the machine keeps its own copy)
	scpy (g_sav_path, g_rom_path, sizeof g_sav_path);
	int e = slen (g_sav_path), d = e; while (d > 0 && g_sav_path[d - 1] != '.' && g_sav_path[d - 1] != '/') d--;
	if (d > 0 && g_sav_path[d - 1] == '.') e = d - 1;
	scpy (g_sav_path + e, ".sav", sizeof g_sav_path - e);
	load_ram ();

	g_gpu = kapi_gpu_info (0, 0) == 1 && kapi_gpu_texture (-2, 0, 0, 0, 0) != -1;
	for (int k = 0; k < n64::Machine::MAX_TEX; k++) g_gpuTex[k] = -1;

	static Menu menu;
	menu.menu ("Game");
	menu.item ("Pause",        "P",   0, on_pause);
	menu.item ("Reset",        "",    0, on_reset);
	menu.separator ();
	menu.item ("Quit",         "^Q",  WK_CTRL ('Q'), on_quit);
	menu.menu ("View");
	menu.item ("Full Screen",  "F11", 0, on_full);
	menu.item ("Zoom 1x",      "",    0, on_zoom1);
	menu.item ("Zoom 2x",      "",    0, on_zoom2);
	menu.item ("Zoom 3x",      "",    0, on_zoom3);
	menu.separator ();
	menu.item ("Show Speed",   "F12", 0, on_stats);
	menu.item ("Draw with the GPU On / Off", "", 0, on_gpu);
	menu.menu ("Sound");
	menu.item ("Sound On / Off", "", 0, on_sound);
	menu.publish ();
	if (wantFull) full_screen (true);

	static short pcm[4096 * 2];
	unsigned rate = SOUND_RATE, freeFrames = 0, owner = 0, stQueued = 0;
	g_m->setAudioRate (SOUND_RATE);
	if (!ec_init (&g_ec, n64::FB_MAX_W, n64::FB_MAX_H, n64_frame)) return 1;
	g_loading = false;

	// Lockstep: a frame is asked for when the clock says so and the machine is idle; its picture
	// is drawn once it is done (the machine waits meanwhile: the GPU reads its frame and textures).
	unsigned t0 = kapi_get_ticks (), asked = 0, lastSave = kapi_get_ticks ();
	unsigned long long stT = now_us (), drawUs = 0, stEmuUs = 0; unsigned stDone = 0, stShown = 0;
	while (!should_exit ())
	{
		pump_events ();
		g_ec.btn = pad_state ();
		if (g_paused) { show_frame (); kapi_msleep (20); t0 = kapi_get_ticks (); asked = 0; continue; }
		if (g_sound && g_audio == 0) { g_audio = kapi_sound_acquire () == 1 ? 1 : -1; g_audioOn = g_audio == 1; }
		unsigned fps = fps100 ();
		// with sound the game's audio paces it (kept ~60 ms ahead; a game without sound of ours
		// leaves the queue empty: then the clock), else the clock
		bool audio = g_sound && g_audio == 1;
		unsigned queued = 0;
		if (audio)
		{
			kapi_sound_status (&rate, &freeFrames, &owner);
			static unsigned cap = 0; if (freeFrames > cap) cap = freeFrames;
			queued = cap - freeFrames;
			int k = ec_audio_pop (&g_ec, pcm, freeFrames < 4096 ? (int) freeFrames : 4096);
			if (k > 0) { kapi_sound_write (pcm, (unsigned) k); queued += (unsigned) k; }
			stQueued = queued;
		}
		if (audio && g_m->audioTasks > 0)
		{
			if (queued + ec_audio_count (&g_ec) < 2600 && ec_pending (&g_ec) == 0) ec_request (&g_ec, 1);
			t0 = kapi_get_ticks (); asked = 0;
		}
		else
		{
			unsigned due = (unsigned) ((unsigned long long) (kapi_get_ticks () - t0) * fps / 10000);
			if (due - asked > 4 && due > asked) asked = due - 1;		// far behind: do not race
			if (asked < due && ec_pending (&g_ec) == 0) { ec_request (&g_ec, 1); asked++; }
		}
		ec_pump (&g_ec);
		if (ec_pending (&g_ec) == 0 && ec_take (&g_ec))
		{
			g_fbW = g_m->fbW; g_fbH = g_m->fbH;
			if (g_m->gfxSerial != g_lastSerial) { g_lastSerial = g_m->gfxSerial; g_gfxAge = 0; } else if (g_gfxAge < 1000) g_gfxAge++;
			unsigned long long d0 = now_us ();
			show_frame ();
			drawUs += now_us () - d0; stShown++;
		}
		else kapi_msleep (1);
		unsigned long long tn = now_us ();
		if (tn - stT >= 1000000)
		{
			unsigned long long el = tn - stT;
			unsigned doneNow = g_ec.done, stEmu = doneNow - stDone;
			unsigned long long emuNow = g_ec.emuUs, emuUs = emuNow - stEmuUs;
			int k = 0;
			fmt_num (g_statText, &k, (unsigned) ((unsigned long long) stEmu * 10000000ull / el), 1); cat (g_statText, &k, " fps  emu ");
			fmt_num (g_statText, &k, stEmu ? (unsigned) (emuUs / stEmu / 100) : 0, 1); cat (g_statText, &k, " ms  draw ");
			fmt_num (g_statText, &k, stShown ? (unsigned) (drawUs / stShown / 100) : 0, 1); cat (g_statText, &k, " ms");
			cat (g_statText, &k, g_gfxAge < 30 ? (g_gpuOn && g_gpu ? "  GPU" : "  software") : "  framebuffer");
			if (g_sound && g_audio == 1 && g_m->audioTasks) { cat (g_statText, &k, "  sound "); fmt_num (g_statText, &k, stQueued * 1000 / SOUND_RATE, 0); cat (g_statText, &k, " ms"); }
			if (ec_on_core (&g_ec)) { cat (g_statText, &k, "  core "); fmt_num (g_statText, &k, (unsigned) g_ec.core, 0); }
			stT = tn; stDone = doneNow; stEmuUs = emuNow; drawUs = 0; stShown = 0;
		}
		if (kapi_get_ticks () - lastSave > 500 && ec_pending (&g_ec) == 0) { save_ram (); lastSave = kapi_get_ticks (); }
	}
	ec_shutdown (&g_ec);
	save_ram ();
	if (g_fs) kapi_fullscreen_end ();
	return 0;
}
