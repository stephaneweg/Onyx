//
// gcemu -- the Onyx Nintendo GameCube emulator (the core: user/gc) -- in progress.
//
//   gcemu <game.iso | game.gcm | program.dol> [--fullscreen]   (without one: the Game Library)
//   * The machine (the Gekko CPU, Flipper) runs on an app core (core 2 or 3, user/emucore.h)
//     when one is free. Its graphics are high-level: each frame's triangles and textures are
//     drawn by the GPU (kapi v54 gpu_render) straight into the window; a program that writes
//     its framebuffer itself is shown from it.
//   * A disc image (1.4 GB) is not loaded: the DVD's reads are done from the file on demand
//     (kapi v57 seek) by the main thread for the app core (a request, then the core waits).
//   * Keys: arrows = the stick, X = A, C = B, S = X, A = Y, Z = Z, Enter = Start, Q / W = L / R,
//     I J K L = the C stick, T F G H = the D-pad; a USB gamepad (user/gamepad.h). F11: full
//     screen (Esc back), F12: the speed, P: pause.
//   * The CPU: the JIT (user/gc/gc_jit.cpp: the PowerPC code translated to AArch64, in memory
//     from kapi v58 code_alloc); Game > Interpreter (or --interp) runs the interpreter instead.
//   * The sound: the machine's audio (the AI's DMA, the Zelda microcode's music) resampled to
//     SOUND_RATE, pushed by the app core into emucore's ring, written to the kapi sound queue
//     here; with it, the sound paces the game (Sound > Sound On / Off).
//   * The memory card (slot A): <game>.sav beside the disc image (2 MB, Dolphin's .raw; the same
//     file as NintendoEMU's), written back a few seconds after the game saved, and on exit.
//   * The TEV renderer's diagnostic: F12's third line (gpu_render2's result, the batches drawn /
//     recorded, the ones left out and why, the programs / textures the kernel refused); F9 dumps
//     the frame shown -- what the recorder made and the picture the GPU drew -- into
//     SD:/gcdump/frame_<n>.gxf (tools/tests/gc/gcv3d.cpp --replay draws it on a PC); --diag writes
//     F12's lines of every second into SD:/gcdump/diag.txt (saved every 5 s), dumps a frame every
//     60 s and quits after 200 s (a test run from a remote shell; a dump holds the machine while
//     it is written); --diag=<folder> writes there instead (e.g.
//     FTP:<pc>:2121 -- a PC's FTP server, through ftpfs). --tevbuf draws the TEV frames into a
//     buffer of ours, then copies them (a test: the GPU no longer writes the window's pixels).
//
#include "kapi.h"
#include "launch.h"
#include "gamepad.h"
#include "wtk/wtk.h"
#include "gc/gc.h"
#include "wtk/dialog.h"
#include "emucore.h"
#include "gxv3d.h"

using namespace wtk;

static gc::Machine *g_m = 0;
static char g_path[256];
static int g_zoom = 1;
static bool g_paused = false;
static unsigned *g_fs = 0; static int g_fsw, g_fsh, g_fsStride;
static Root *g_root = 0;
static bool g_stats = false;
static char g_statText[128] = "";
static char g_statState[200] = "";				// (F12, 2nd line) where the game is: gc::Machine::status
static char g_statTev[160] = "";				// (F12, 3rd line) the TEV renderer's diagnostic
static char g_statPerf[160] = "";				// (F12, 4th line) where the time goes (TEV)
static volatile bool g_dumpReq = false;				// (F9) the frame shown, dumped
static char g_note[200] = ""; static unsigned g_noteUntil = 0;	// (a message a few seconds)
static bool g_diag = false;					// --diag[=<folder>]
static char g_dumpDir[128] = "SD:/gcdump";			// (where F9 / --diag write: a folder, or FTP:host:port/...)
static bool g_tevBuf = false;					// --tevbuf: the TEV frames drawn into a buffer of ours, then copied
static EmuCore g_ec;
static int g_stride;
static bool g_loading = true;
static char g_loadName[64];
static bool g_gpu = false;
static gxv3d::Rec g_rec;					// the TEV renderer (kapi v61): the draws recorded by the machine,
static gxv3d::Out g_out;					// drawn here
static bool g_tevOk = false;					// (it could start)
static volatile bool g_wantTev = true;				// the View menu's choice, applied between fields
static int g_gpuTex[gc::Machine::MAX_TEX];
static unsigned g_lastSerial = 0, g_gfxAge = 1000;
static int g_fbW = 640, g_fbH = 480;
static void *g_disc = 0;					// the disc image's file
static bool g_onCore = false;					// the machine runs on an app core
static gc::Jit *g_jit = 0;					// the JIT (0: none: an older kernel)
static volatile int g_wantJit = 1;				// the menu's choice, applied between fields
static void *code_alloc (unsigned n) { return kapi_code_alloc (n); }
static bool g_sound = true;
static int g_audio = 0;						// 0 not tried, 1 ours, -1 none
static volatile bool g_audioOn = false;				// the machine's sound is kept
static volatile unsigned g_audioMade = 0;			// frames of sound the machine made
static char g_sav_path[260];
static unsigned char *g_card = 0; static unsigned g_cardSize = 0;

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
static bool ends (const char *s, const char *e)
{
	int a = slen (s), b = slen (e);
	if (a < b) return false;
	for (int i = 0; i < b; i++) { char c = s[a - b + i]; if (c >= 'A' && c <= 'Z') c = (char) (c + 32); if (c != e[i]) return false; }
	return true;
}
static unsigned fps100 (void) { return g_m && g_m->pal ? 5000 : 5994; }

// ---- the disc: reads from the file, done by the main thread for the app core ------------------------------------
static volatile int g_rdState = 0;				// 0 idle, 1 asked, 2 done
static volatile unsigned g_rdOff, g_rdLen; static unsigned char * volatile g_rdDst; static volatile bool g_rdOk;

static bool read_now (unsigned off, unsigned len, unsigned char *dst)
{
	if (!g_disc || kapi_seek (g_disc, off) != 0) return false;
	while (len)
	{
		unsigned k = len > 0x40000 ? 0x40000 : len;
		int got = kapi_read (g_disc, dst, k);
		if (got <= 0) return false;
		dst += got; len -= (unsigned) got;
	}
	return true;
}
static bool disc_read (void *, gc::u32 off, gc::u32 len, gc::u8 *dst)
{
	if (!g_onCore) return read_now (off, len, dst);		// (the main thread itself)
	g_rdOff = off; g_rdLen = len; g_rdDst = dst;
	ec_fence (); g_rdState = 1; ec_sev ();
	while (g_rdState != 2) ec_wfe ();
	ec_fence ();
	g_rdState = 0;
	return g_rdOk;
}
static void serve_reads (void)
{
	if (g_rdState != 1) return;
	ec_fence ();
	g_rdOk = read_now (g_rdOff, g_rdLen, g_rdDst);
	ec_fence (); g_rdState = 2; ec_sev ();
}

// ---- drawing a frame ----------------------------------------------------------------------------------------------
static bool gpu_frame (unsigned *px, int w, int h, int stride)
{
	if (!g_gpu || g_m->gfxReady < 0) return false;
	const gc::GFrame &F = g_m->gfxFrame[g_m->gfxReady];
	for (int i = 0; i < gc::Machine::MAX_TEX; i++)
	{
		gc::GTexture &T = g_m->tex[i];
		if (!T.dirty) continue;
		// (the machine runs on the app core meanwhile and may give this slot another texture:
		// its size read once, and never more pixels than the slot holds -- the kernel reads them)
		int w = *(volatile int *) &T.w, h = *(volatile int *) &T.h;
		const unsigned *px = *(unsigned *const volatile *) &T.px;
		int cap = *(volatile int *) &T.cap;
		if (w <= 0 || h <= 0 || !px || w * h > cap) continue;
		int h2 = kapi_gpu_texture (g_gpuTex[i], px, w, h, w);
		if (h2 < 0 && g_gpuTex[i] >= 0) h2 = kapi_gpu_texture (-1, px, w, h, w);
		g_gpuTex[i] = h2;
		T.dirty = false;
	}
	static struct kapi_gpu_batch bt[gc::GFrame::MAXB];
	int nb = 0;
	for (int i = 0; i < F.nb; i++)
	{
		const gc::GBatch &B = F.b[i];
		struct kapi_gpu_batch &o = bt[nb];
		o.first = B.first; o.count = B.count; o.flags = B.flags;
		o.texture = B.tex >= 0 ? g_gpuTex[B.tex] : -1;
		if (B.tex >= 0 && o.texture < 0) continue;
		for (int k = 0; k < 16; k++) o.matrix[k] = B.m[k];
		nb++;
	}
	struct kapi_gpu_frame fr = { px, w, h, stride, F.clear, 0 };
	return kapi_gpu_render (&fr, (const struct kapi_gpu_vertex3 *) F.v, (unsigned) F.nv, bt, (unsigned) nb) == 0;
}

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

// (the TEV: the machine's last frame is taken -- Out::prepare -- only while the machine waits
// between two fields, then drawn from our own arrays at any time: the GPU may draw while the next
// field runs; the old paths read the machine as they draw: only while it waits)
static void draw_into (unsigned *px, int w, int h, int stride)
{
	if (g_m->gpu && g_gfxAge < 30)
	{
		if (ec_pending (&g_ec) == 0) g_out.prepare (g_rec, *g_m, w, h);
		if (g_out.prepared (w, h))
		{
			if (!g_tevBuf) { if (g_out.submit (px, stride)) return; }
			else								// (a test: the GPU not writing the window's pixels itself)
			{
				static unsigned *buf = 0; static int cap = 0;
				if (cap < w * h) { delete [] buf; buf = new unsigned[w * h]; cap = buf ? w * h : 0; }
				if (buf && g_out.submit (buf, w))
				{
					for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) px[(long) y * stride + x] = buf[y * w + x];
					return;
				}
			}
		}
	}
	if (!g_m->gpu && g_gfxAge < 30 && gpu_frame (px, w, h, stride)) return;
	fb_frame (px, w, h, stride);
}
// the size the frames are drawn at: the window's, or full screen's 4:3 area
static void target_size (int *w, int *h)
{
	if (g_fs)
	{
		int ow = g_fsw, oh = g_fsw * 3 / 4;
		if (oh > g_fsh) { oh = g_fsh; ow = g_fsh * 4 / 3; }
		*w = ow; *h = oh;
	}
	else { *w = g_root->width; *h = g_root->height; }
}

// F12's lines at the bottom: where the game is, the speed, the TEV renderer's diagnostic
static void overlay (Canvas &c)
{
	int n = g_m->gpu ? 4 : 2, y = c.h - n * 20;
	c.fillRect (0, y, c.w, n * 20, 0);
	c.text (4, y + 2, g_statState, 0x00A0FFA0);
	c.text (4, y + 22, g_statText, 0x00FFFF60);
	if (n == 4) { c.text (4, y + 42, g_statTev, g_out.st.ret == 0 ? 0x00A0C0FF : 0x00FF8080); c.text (4, y + 62, g_statPerf, 0x00C0C0C0); }
}
static void note (const char *s) { scpy (g_note, s, sizeof g_note); g_noteUntil = kapi_get_ticks () + 300; }

// F9 / --diag: the frame just drawn -- the recorder's frame, its programs and textures, the picture
// the GPU made -- into SD:/gcdump/frame_<n>.gxf (the machine waits: nothing moves meanwhile)
static void dump_frame (const unsigned *px, int w, int h, int stride)
{
	static int count = 0;
	if (!g_m->gpu || g_rec.ready < 0) { note ("F9: no frame of the TEV renderer to dump"); return; }
	const gxv3d::Frame &F = g_rec.frame[g_rec.ready];
	unsigned long long n = gxv3d::dumpFrame (g_rec, *g_m, F, px, w, h, stride, g_out.st.ret, 0);
	unsigned char *d = n < 0x7FFFFFFF ? new unsigned char[n] : 0;
	if (!d) { note ("F9: not enough memory for the dump"); return; }
	gxv3d::dumpFrame (g_rec, *g_m, F, px, w, h, stride, g_out.st.ret, d);
	kapi_mkdir (g_dumpDir);
	char path[200]; int k = 0; path[0] = 0;
	cat (path, &k, g_dumpDir); cat (path, &k, "/frame_"); fmt_num (path, &k, (unsigned) ++count, 0); cat (path, &k, ".gxf");
	int r = kapi_save_file (path, d, (unsigned) n);
	delete [] d;
	char msg[260]; k = 0; msg[0] = 0;
	cat (msg, &k, r >= 0 ? "dumped: " : "the dump failed: "); cat (msg, &k, path);
	note (msg);
}
static void after_draw (const unsigned *px, int w, int h, int stride)
{
	if (!g_dumpReq || ec_pending (&g_ec) != 0) return;		// (the machine running: later)
	g_dumpReq = false;
	dump_frame (px, w, h, stride);
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
			canvas.text (8, height / 2 - 24, "Starting...", 0xFFFFFF);
			canvas.text (8, height / 2 - 6, g_loadName, 0xA0A0A0);
			return;
		}
		draw_into (canvas.px, width, height, canvas.stride);
		after_draw (canvas.px, width, height, canvas.stride);
		if (g_paused) canvas.text (8, 8, "Paused", 0xFFFFFF);
		if (g_stats) overlay (canvas);
		if (g_note[0] && kapi_get_ticks () < g_noteUntil) canvas.text (8, 24, g_note, 0x00FFFF60);
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
		int ow = g_fsw, oh = g_fsw * 3 / 4;
		if (oh > g_fsh) { oh = g_fsh; ow = g_fsh * 4 / 3; }
		unsigned *p = g_fs + (long) ((g_fsh - oh) / 2) * g_fsStride + (g_fsw - ow) / 2;
		draw_into (p, ow, oh, g_fsStride);
		after_draw (p, ow, oh, g_fsStride);
		Canvas c; c.adopt (g_fs, g_fsw, g_fsh, g_fsStride);
		if (g_stats) overlay (c);
		if (g_note[0] && kapi_get_ticks () < g_noteUntil) c.text (8, 24, g_note, 0x00FFFF60);
		kapi_present_fb ();
	}
	else { g_root->invalidate (true); g_root->draw (); kapi_present (); }
}

static void set_zoom (int z)
{
	g_zoom = z;
	int w = z == 1 ? 640 : 960, h = z == 1 ? 480 : 720;
	g_root->canvas.adopt (kapi_resize_window (w, h), w, h, g_stride);
	wtk::wk_decorate_window ();
	g_root->width = w; g_root->height = h;
	g_root->invalidate (true);
}
static void on_zoom1 () { set_zoom (1); }
static void on_zoom2 () { set_zoom (2); }
static void on_full () { full_screen (!g_fs); }
static void on_pause () { g_paused = !g_paused; g_root->invalidate (true); }
static void on_stats () { g_stats = !g_stats; g_root->invalidate (true); }
static void on_dump () { g_dumpReq = true; g_root->invalidate (true); }
static void on_interp () { g_wantJit = !g_wantJit; }
static void on_tev () { g_wantTev = !g_wantTev; }
static void card_save (void);
static void on_quit () { ec_hold (&g_ec); card_save (); kapi_exit (0); }	// (the card written first)

bool EmuRoot::onKey (long k)
{
	if (g_loading) return Root::onKey (k);
	if (k == KEY_F1 + 10) { on_full (); return true; }
	if (k == 27 && g_fs) { full_screen (false); return true; }
	if (k == 'p' || k == 'P') { on_pause (); return true; }
	if (k == KEY_F1 + 11) { on_stats (); return true; }
	if (k == KEY_F1 + 8) { on_dump (); return true; }
	return Root::onKey (k);
}

// the pad: its buttons, the stick, the C stick, the triggers -- packed into 3 words for the core
static volatile unsigned g_padW[3];
static void pad_state (void)
{
	unsigned b = 0; int x = 0, y = 0, cx = 0, cy = 0, l = 0, r = 0;
	if (kapi_key_held (KEY_RIGHT)) x += 90;
	if (kapi_key_held (KEY_LEFT)) x -= 90;
	if (kapi_key_held (KEY_UP)) y += 90;
	if (kapi_key_held (KEY_DOWN)) y -= 90;
	if (kapi_key_held ('x')) b |= gc::Machine::PAD_A;
	if (kapi_key_held ('c')) b |= gc::Machine::PAD_B;
	if (kapi_key_held ('s')) b |= gc::Machine::PAD_X;
	if (kapi_key_held ('a')) b |= gc::Machine::PAD_Y;
	if (kapi_key_held ('z')) b |= gc::Machine::PAD_Z;
	if (kapi_key_held (KEY_ENTER)) b |= gc::Machine::PAD_START;
	if (kapi_key_held ('q')) { b |= gc::Machine::PAD_L; l = 255; }
	if (kapi_key_held ('w')) { b |= gc::Machine::PAD_R; r = 255; }
	if (kapi_key_held ('t')) b |= gc::Machine::PAD_UP;
	if (kapi_key_held ('g')) b |= gc::Machine::PAD_DOWN;
	if (kapi_key_held ('f')) b |= gc::Machine::PAD_LEFT;
	if (kapi_key_held ('h')) b |= gc::Machine::PAD_RIGHT;
	if (kapi_key_held ('l')) cx += 90;
	if (kapi_key_held ('j')) cx -= 90;
	if (kapi_key_held ('i')) cy += 90;
	if (kapi_key_held ('k')) cy -= 90;
	struct pad_input in;
	if (pad_read (0, &in))
	{
		unsigned p = in.buttons;
		if (p & PAD_A) b |= gc::Machine::PAD_A;
		if (p & PAD_X) b |= gc::Machine::PAD_B;
		if (p & PAD_B) b |= gc::Machine::PAD_X;
		if (p & PAD_Y) b |= gc::Machine::PAD_Y;
		if (p & (PAD_L | PAD_R)) b |= gc::Machine::PAD_Z;
		if (p & PAD_L2) { b |= gc::Machine::PAD_L; l = 255; }
		if (p & PAD_R2) { b |= gc::Machine::PAD_R; r = 255; }
		if (p & PAD_START) b |= gc::Machine::PAD_START;
		if (p & PAD_UP) b |= gc::Machine::PAD_UP;
		if (p & PAD_DOWN) b |= gc::Machine::PAD_DOWN;
		if (p & PAD_LEFT) b |= gc::Machine::PAD_LEFT;
		if (p & PAD_RIGHT) b |= gc::Machine::PAD_RIGHT;
		if (in.lx || in.ly) { x = in.lx * 100 / 1000; y = -in.ly * 100 / 1000; }
		if (in.rx || in.ry) { cx = in.rx * 100 / 1000; cy = -in.ry * 100 / 1000; }
	}
	g_padW[0] = b; g_padW[1] = (unsigned) (x & 0xFF) | (unsigned) (y & 0xFF) << 8 | (unsigned) (cx & 0xFF) << 16 | (unsigned) (cy & 0xFF) << 24;
	g_padW[2] = (unsigned) l | (unsigned) r << 8;
}

// One field of the machine: on the app core (no kapi call except the disc reads it asks for).
static void gc_frame (EmuCore *ec)
{
	unsigned w1 = g_padW[1], w2 = g_padW[2];
	gc::GxGpu *wantGpu = g_wantTev && g_tevOk ? &g_rec : 0;
	if (wantGpu != g_m->gpu) { g_m->gpu = wantGpu; g_m->gxsDirty = true; g_rec.ready = -1; }	// (the renderer changed)
	gc::Jit *want = g_wantJit ? g_jit : 0;
	if (want != g_m->jit) { g_m->jit = want; g_m->jitFlush = true; }	// (what ran meanwhile may have changed the code)
	g_m->setPad (0, g_padW[0], (signed char) w1, (signed char) (w1 >> 8), (signed char) (w1 >> 16), (signed char) (w1 >> 24), (int) (w2 & 255), (int) (w2 >> 8));
	g_m->runFrame ();
	unsigned *d = ec_back (ec);
	int n = g_m->fbW * g_m->fbH;
	for (int i = 0; i < n; i++) d[i] = g_m->fb[i];
	ec_publish (ec);
	static short pcm[4096 * 2];
	int k;
	while ((k = g_m->audioRead (pcm, 4096)) > 0) { g_audioMade += (unsigned) k; if (g_audioOn) ec_audio_push (ec, pcm, k); }
}

// ---- the memory card: <game>.sav --------------------------------------------------------------------------------
static void card_load (void)
{
	g_cardSize = gc::Machine::CARD_SIZE;
	void *f = kapi_open (g_sav_path);
	unsigned n = f ? kapi_fsize (f) : 0;
	if (f && n >= 512 * 1024 && n <= 16 * 1024 * 1024 && (n & (n - 1)) == 0) g_cardSize = n;	// (a Dolphin .raw of another size)
	g_card = new unsigned char[g_cardSize];
	for (unsigned i = 0; i < g_cardSize; i++) g_card[i] = 0xFF;			// (blank: the game formats it)
	if (f) { if (n == g_cardSize) kapi_read (f, g_card, g_cardSize); kapi_close (f); }
	g_m->cardInsert (0, g_card, g_cardSize);
}
static void card_save (void)
{
	if (!g_card || !g_m->card[0].dirty) return;
	if (kapi_save_file (g_sav_path, g_card, g_cardSize) >= 0) g_m->card[0].dirty = false;
}
// F12's third line: gpu_render2's last result, the batches drawn / recorded, left out (the program
// refused, a texture missing, nothing visible, over the vertex limit), the recorder's own, the
// programs / textures the kernel refused (with the last error)
static void num (char *d, int *k, int v) { if (v < 0) { cat (d, k, "-"); v = -v; } fmt_num (d, k, (unsigned) v, 0); }
static void tev_line (void)
{
	const gxv3d::Out::Stats &s = g_out.st;
	char *d = g_statTev; int k = 0; d[0] = 0;
	cat (d, &k, "TEV: render2 "); if (s.ret == 1) cat (d, &k, "-"); else num (d, &k, s.ret);
	cat (d, &k, "  batches "); num (d, &k, (int) s.drawn); cat (d, &k, "/"); num (d, &k, (int) s.recorded);
	cat (d, &k, "  out: prog "); num (d, &k, (int) s.noProg); cat (d, &k, " tex "); num (d, &k, (int) s.noTex);
	cat (d, &k, " empty "); num (d, &k, (int) s.empty); cat (d, &k, " limit "); num (d, &k, (int) s.limit);
	cat (d, &k, "  rec skipped "); num (d, &k, (int) g_rec.skipped);
	cat (d, &k, "  refused: progs "); num (d, &k, (int) s.progFails); if (s.progFails) { cat (d, &k, " ("); num (d, &k, s.progErr); cat (d, &k, ")"); }
	cat (d, &k, " tex "); num (d, &k, (int) s.texFails); if (s.texFails) { cat (d, &k, " ("); num (d, &k, s.texErr); cat (d, &k, ")"); }
}

// F12's fourth line: where the time went in the last second -- a field's on the app core: the GX
// (the FIFO's commands, all in), its primitives (the vertices decoded, the state, the textures
// looked up, the recorder), the textures, the recorder (the vertex stage: transformed, lit, into
// the frame) and its vertices; a frame's on the main thread: the kernel's arrays made
// (Out::prepare), then gpu_render2 (the clipping, the GPU)
static void perf_line (unsigned fields)
{
	static unsigned long long pDraw = 0, pVerts = 0, pPrep = 0, pGpu = 0, pGVerts = 0; static unsigned pFrames = 0;
	static unsigned long long pFifo = 0, pPrim = 0, pTex = 0;
	const gxv3d::Out::Stats &s = g_out.st;
	unsigned long long rate = gxv3d::clockRate ();
	unsigned long long dDraw = g_rec.drawTicks - pDraw, dVerts = g_rec.drawVerts - pVerts;
	unsigned long long dPrep = s.prepTicks - pPrep, dGpu = s.gpuTicks - pGpu, dGV = s.gpuVerts - pGVerts;
	unsigned dF = s.frames - pFrames;
	unsigned long long dFifo = g_m->timeFifo - pFifo, dPrim = g_m->timePrim - pPrim, dTex = g_m->timeTex - pTex;
	pDraw += dDraw; pVerts += dVerts; pPrep += dPrep; pGpu += dGpu; pGVerts += dGV; pFrames += dF;
	pFifo += dFifo; pPrim += dPrim; pTex += dTex;
	unsigned fl = fields ? fields : 1, fr = dF ? dF : 1;
	char *d = g_statPerf; int k = 0; d[0] = 0;
	cat (d, &k, "a field: gx "); fmt_num (d, &k, (unsigned) (dFifo * 10000 / rate / fl), 1);
	cat (d, &k, " ms (prims "); fmt_num (d, &k, (unsigned) (dPrim * 10000 / rate / fl), 1);
	cat (d, &k, ", textures "); fmt_num (d, &k, (unsigned) (dTex * 10000 / rate / fl), 1);
	cat (d, &k, "), rec "); fmt_num (d, &k, (unsigned) (dDraw * 10000 / rate / fl), 1);
	cat (d, &k, " ms, "); fmt_num (d, &k, (unsigned) (dVerts / fl), 0); cat (d, &k, " vertices  a frame: prep ");
	fmt_num (d, &k, (unsigned) (dPrep * 10000 / rate / fr), 1); cat (d, &k, " ms, gpu_render2 ");
	fmt_num (d, &k, (unsigned) (dGpu * 10000 / rate / fr), 1); cat (d, &k, " ms, ");
	fmt_num (d, &k, (unsigned) (dGV / fr), 0); cat (d, &k, " vertices");
}

// --diag: F12's lines every second into SD:/gcdump/diag.txt (saved every 5 s: less time taken from the
// game), a frame dumped every 60 s, the end after 200 s
static void diag_tick (void)
{
	static char *log = 0; static unsigned n = 0, secs = 0;
	enum { CAP = 256 * 1024 };
	static char path[200];
	if (!log) { log = new char[CAP]; kapi_mkdir (g_dumpDir); int k = 0; path[0] = 0; cat (path, &k, g_dumpDir); cat (path, &k, "/diag.txt"); }
	if (!g_m->gpu) tev_line ();
	secs++;
	const char *lines[4] = { g_statState, g_statText, g_statTev, g_statPerf };
	char t[16]; int k = 0; t[0] = 0; fmt_num (t, &k, secs, 0); cat (t, &k, " s\n");
	for (int j = 0; j < 5; j++)
	{
		const char *s = j == 0 ? t : lines[j - 1];
		for (int i = 0; s[i] && n < CAP - 2; i++) log[n++] = s[i];
		if (j) log[n++] = '\n';
	}
	if (secs % 5 == 0 || secs >= 200) kapi_save_file (path, log, n);
	if (secs % 60 == 0) g_dumpReq = true;
	if (secs >= 200) on_quit ();
}

static void on_sound ()
{
	g_sound = !g_sound;
	if (!g_sound && g_audio == 1) { g_audioOn = false; kapi_sound_release (); g_audio = 0; }
}

int main (void)
{
	char args[256] = "";
	kapi_get_args (args, sizeof args);
	int i = 0; while (args[i] == ' ') i++;
	int n = 0;
	bool wantFull = false;
	if (args[i] == '"') { i++; while (args[i] && args[i] != '"' && n < 255) g_path[n++] = args[i++]; if (args[i]) i++; }
	else while (args[i] && n < 255 && !(args[i] == ' ' && args[i + 1] == '-' && args[i + 2] == '-')) g_path[n++] = args[i++];
	while (n > 0 && g_path[n - 1] == ' ') n--;
	g_path[n] = 0;
	if (!g_path[0]) { lx_launch ("gamelib", ""); return 0; }
	for (; args[i]; i++)
	{
		if (args[i] == '-' && args[i + 1] == '-' && args[i + 2] == 'f' && args[i + 3] == 'u' && args[i + 4] == 'l' && args[i + 5] == 'l') wantFull = true;
		if (args[i] == '-' && args[i + 1] == '-' && args[i + 2] == 'i' && args[i + 3] == 'n' && args[i + 4] == 't') g_wantJit = 0;
		if (args[i] == '-' && args[i + 1] == '-' && args[i + 2] == 't' && args[i + 3] == 'e' && args[i + 4] == 'v' && args[i + 5] == 'b') g_tevBuf = true;
		if (args[i] == '-' && args[i + 1] == '-' && args[i + 2] == 'd' && args[i + 3] == 'i' && args[i + 4] == 'a' && args[i + 5] == 'g')
		{
			g_diag = true;
			if (args[i + 6] == '=')					// --diag=<folder>
			{
				int j = i + 7, k = 0;
				while (args[j] && args[j] != ' ' && k < (int) sizeof g_dumpDir - 1) g_dumpDir[k++] = args[j++];
				while (k > 0 && g_dumpDir[k - 1] == '/') k--;
				g_dumpDir[k] = 0;
			}
		}
	}

	{ int b = slen (g_path); while (b > 0 && g_path[b - 1] != '/' && g_path[b - 1] != ':') b--; scpy (g_loadName, g_path + b, sizeof g_loadName); }
	char title[64]; scpy (title, g_loadName, sizeof title);
	{ int e = slen (title); while (e > 0 && title[e - 1] != '.') e--; if (e > 1) title[e - 1] = 0; }
	g_stride = 960;
	EmuRoot root (960, 720, title);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	set_zoom (g_zoom);
	root.attach ();
	root.invalidate (true); root.draw (); kapi_present ();

	g_m = new gc::Machine;
	g_m->setAudioRate (SOUND_RATE);
	scpy (g_sav_path, g_path, sizeof g_sav_path);
	{
		int e = slen (g_sav_path), d = e;
		while (d > 0 && g_sav_path[d - 1] != '.' && g_sav_path[d - 1] != '/') d--;
		if (d > 0 && g_sav_path[d - 1] == '.') e = d - 1;
		scpy (g_sav_path + e, ".sav", sizeof g_sav_path - e);
	}
	card_load ();
	bool ok = false;
	void *f = kapi_open (g_path);
	if (!f) { g_loading = false; wk_messagebox ("GameCube", "Cannot open the file.", MB_OK); return 1; }
	unsigned sz = kapi_fsize (f);
	if (ends (g_path, ".dol"))
	{
		unsigned char *d = new unsigned char[sz + 1];
		int r = kapi_read (f, d, sz);
		kapi_close (f);
		ok = r > 0 && g_m->loadDol (d, (unsigned) r);
		delete [] d;
	}
	else
	{
		g_disc = f;
		g_m->discRead = disc_read;
		ok = g_m->loadDiscImage (sz);
		if (ok && g_m->title[0]) scpy (title, g_m->title, sizeof title);
	}
	if (!ok)
	{
		g_loading = false;
		wk_messagebox ("GameCube", "Not a GameCube disc image (.iso / .gcm) or program (.dol), or its start failed.", MB_OK);
		return 1;
	}

	gc::codeAlloc = code_alloc;				// (the JIT's code memory: taken here, not on the app core)
	if (g_m->jitEnable ()) g_jit = g_m->jit;
	if (!g_wantJit) g_m->jit = 0;

	g_gpu = kapi_gpu_info (0, 0) == 1 && kapi_gpu_texture (-2, 0, 0, 0, 0) != -1;
	for (int k = 0; k < gc::Machine::MAX_TEX; k++) g_gpuTex[k] = -1;
	g_tevOk = g_gpu && KT->version >= 61 && g_rec.init () && g_out.init ();
	if (g_tevOk && g_wantTev) g_m->gpu = &g_rec;

	static Menu menu;
	menu.menu ("Game");
	menu.item ("Pause",        "P",   0, on_pause);
	if (g_jit) menu.item ("Interpreter (no JIT)", "", 0, on_interp);
	menu.separator ();
	menu.item ("Quit",         "^Q",  WK_CTRL ('Q'), on_quit);
	menu.menu ("Sound");
	menu.item ("Sound On / Off", "", 0, on_sound);
	menu.menu ("View");
	menu.item ("Full Screen",  "F11", 0, on_full);
	menu.item ("Size 640 x 480", "",  0, on_zoom1);
	menu.item ("Size 960 x 720", "",  0, on_zoom2);
	if (g_tevOk) menu.item ("TEV Shaders On / Off", "", 0, on_tev);
	menu.separator ();
	menu.item ("Show Speed",   "F12", 0, on_stats);
	if (g_tevOk) menu.item ("Dump the Frame (TEV)", "F9", 0, on_dump);
	menu.publish ();
	if (wantFull) full_screen (true);

	if (!ec_init (&g_ec, gc::Machine::FB_MAX_W, gc::Machine::FB_MAX_H, gc_frame)) return 1;
	g_onCore = ec_on_core (&g_ec);
	g_loading = false;

	unsigned t0 = kapi_get_ticks (), asked = 0, lastSave = kapi_get_ticks ();
	unsigned freeFrames = 0, rate = SOUND_RATE, owner = 0, stQueued = 0;
	static short pcm[4096 * 2];
	unsigned long long stT = now_us (), drawUs = 0, stEmuUs = 0; unsigned stDone = 0, stShown = 0;
	while (!should_exit ())
	{
		pump_events ();
		pad_state ();
		serve_reads ();
		// (paused: drawn only once the frame being made is done -- the machine then waits and the GPU
		// may read its frame and textures; drawn while it runs, they may change under the kernel)
		if (g_paused) { ec_pump (&g_ec); if (ec_pending (&g_ec) == 0) show_frame (); kapi_msleep (20); t0 = kapi_get_ticks (); asked = 0; continue; }
		if (g_sound && g_audio == 0) { g_audio = kapi_sound_acquire () == 1 ? 1 : -1; g_audioOn = g_audio == 1; }
		// a new image, the machine between two fields: taken before the next field is asked for --
		// else, slower than real time, the next one was always asked first and nothing was shown.
		// The TEV's frame is prepared now and drawn after the request (the GPU then works while the
		// machine runs); the other pictures, and a frame to dump, are drawn now.
		bool shown = false, later = false;
		if (ec_pending (&g_ec) == 0 && ec_take (&g_ec))
		{
			g_fbW = g_m->fbW; g_fbH = g_m->fbH;
			if (g_m->gfxSerial != g_lastSerial) { g_lastSerial = g_m->gfxSerial; g_gfxAge = 0; } else if (g_gfxAge < 1000) g_gfxAge++;
			unsigned long long d0 = now_us ();
			int tw, th; target_size (&tw, &th);
			if (g_m->gpu && g_gfxAge < 30 && !g_dumpReq && g_out.prepare (g_rec, *g_m, tw, th)) later = true;
			else { show_frame (); stShown++; shown = true; }
			drawUs += now_us () - d0;
		}
		// with sound the game's audio paces it (kept ~60 ms ahead), else -- no sound of ours, or
		// the game makes none yet -- the clock
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
		if (audio && g_audioMade > 0)
		{
			if (queued + ec_audio_count (&g_ec) < 2600 && ec_pending (&g_ec) == 0) ec_request (&g_ec, 1);
			t0 = kapi_get_ticks (); asked = 0;
		}
		else
		{
			unsigned fps = fps100 ();
			unsigned due = (unsigned) ((unsigned long long) (kapi_get_ticks () - t0) * fps / 10000);
			if (due - asked > 4 && due > asked) asked = due - 1;
			if (asked < due && ec_pending (&g_ec) == 0) { ec_request (&g_ec, 1); asked++; }
		}
		if (kapi_get_ticks () - lastSave > 300 && ec_pending (&g_ec) == 0) { card_save (); lastSave = kapi_get_ticks (); }	// (3 s)
		if (later) { unsigned long long d0 = now_us (); show_frame (); drawUs += now_us () - d0; stShown++; shown = true; }
		ec_pump (&g_ec);
		serve_reads ();
		if (!shown && g_rdState != 1) kapi_msleep (1);
		unsigned long long tn = now_us ();
		if (tn - stT >= 1000000)
		{
			unsigned long long el = tn - stT;
			unsigned doneNow = g_ec.done, stEmu = doneNow - stDone;
			unsigned long long emuNow = g_ec.emuUs, emuUs = emuNow - stEmuUs;
			int k = 0;
			fmt_num (g_statText, &k, (unsigned) ((unsigned long long) stEmu * 10000000ull / el), 1); cat (g_statText, &k, " fields/s  emu ");
			fmt_num (g_statText, &k, stEmu ? (unsigned) (emuUs / stEmu / 100) : 0, 1); cat (g_statText, &k, " ms  draw ");
			fmt_num (g_statText, &k, stShown ? (unsigned) (drawUs / stShown / 100) : 0, 1); cat (g_statText, &k, " ms");
			cat (g_statText, &k, g_gfxAge >= 30 ? "  framebuffer" : g_m->gpu ? "  TEV" : "  GPU");
			if (g_m->gpu) { cat (g_statText, &k, " "); fmt_num (g_statText, &k, (unsigned) g_rec.nProg, 0); cat (g_statText, &k, " progs"); }
			cat (g_statText, &k, g_m->jit ? "  JIT" : "  interpreter");
			if (g_sound && g_audio == 1 && g_audioMade) { cat (g_statText, &k, "  sound "); fmt_num (g_statText, &k, stQueued * 1000 / SOUND_RATE, 0); cat (g_statText, &k, " ms"); }
			if (ec_on_core (&g_ec)) { cat (g_statText, &k, "  core "); fmt_num (g_statText, &k, (unsigned) g_ec.core, 0); }
			g_m->status (g_statState, sizeof g_statState);	// (read while it may run: a diagnostic)
			stT = tn; stDone = doneNow; stEmuUs = emuNow; drawUs = 0; stShown = 0;
			if (g_m->gpu) { tev_line (); perf_line (stEmu); }
			if (g_diag) diag_tick ();
		}
	}
	ec_shutdown (&g_ec);
	card_save ();
	if (g_fs) kapi_fullscreen_end ();
	if (g_disc) kapi_close (g_disc);
	return 0;
}
