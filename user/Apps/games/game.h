//
// game.h -- small shared kit for the uikit games (Solitaire, FreeCell, Pipes, Arkanoid,
// Invaders): a full-window GameView widget with press / release / move edges and a
// fixed-rate tick, sound effects on the kernel synth, a PRNG and text helpers.
//
// Sound: sfx_* acquire the audio output on first use (silently no sound if another app
// owns it) and play short notes on voices 12..15; sfx_tick () (called by GameView every
// frame) stops them when their time is up, and plays queued notes (little jingles).
// The kernel silences the output when the app exits.
//
// Held keys: key events only say "pressed" -- action games poll kapi_key_held (ABI v48)
// in tick () to move while an arrow is held.
//
#ifndef _onyx_game_h
#define _onyx_game_h

#include "audiokit/audiokit.h"	// (the effects: AudioKit's voices, ak_fm_*)
#include "appkit/appkit.h"
#include "uikit/uikit.h"

// Milliseconds (kapi_get_ticks counts HZ = 100 ticks per second).
static inline unsigned gms (void) { return kapi_get_ticks () * 10u; }

// ---- PRNG (xorshift32) ---------------------------------------------------------------
static unsigned g_rng_state = 0x9E3779B9u;
static inline void rng_seed (unsigned s) { g_rng_state = s ? s : 0x9E3779B9u; }
static inline unsigned rng (void)
{
	unsigned x = g_rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return g_rng_state = x;
}
static inline int rng_n (int n) { return n > 0 ? (int) (rng () % (unsigned) n) : 0; }

// ---- sound effects -------------------------------------------------------------------
#define SFX_V0		12			// voices 12..15
#define SFX_NV		4
#define SFX_Q		24
struct SfxNote { unsigned at, hz, ms; int wave, vol; };
static int      g_sfx_state;			// 0 not tried, 1 ours, -1 unavailable
static bool     g_sfx_mute;
static unsigned g_sfx_end[SFX_NV];		// ticks when the voice stops (0 = idle)
static int      g_sfx_next;
static SfxNote  g_sfx_q[SFX_Q];			// queued notes (jingles)
static int      g_sfx_qn;

static inline bool sfx_ready (void)
{
	if (g_sfx_mute) return false;
	g_sfx_state = 1;				// (AudioKit's voices: its player takes the output when a note sounds)
	return g_sfx_state == 1;
}
// Play hz (Hz) for ms milliseconds now, on the next free effect voice.
static inline void sfx (unsigned hz, unsigned ms, int wave = SOUND_SQUARE, int vol = 90)
{
	if (!sfx_ready () || hz == 0) return;
	int v = g_sfx_next; g_sfx_next = (g_sfx_next + 1) % SFX_NV;
	ak_fm_start (SFX_V0 + v, hz * 1000u, wave, vol);
	g_sfx_end[v] = gms () + ms;
}
// Queue a note to start delay ms from now (a jingle = several of these).
static inline void sfx_later (unsigned delay, unsigned hz, unsigned ms, int wave = SOUND_SQUARE, int vol = 90)
{
	if (!sfx_ready () || g_sfx_qn >= SFX_Q) return;
	SfxNote &n = g_sfx_q[g_sfx_qn++];
	n.at = gms () + delay; n.hz = hz; n.ms = ms; n.wave = wave; n.vol = vol;
}
static inline void sfx_tick (void)
{
	if (g_sfx_state != 1) return;
	unsigned now = gms ();
	for (int i = 0; i < g_sfx_qn; )
		if ((int) (now - g_sfx_q[i].at) >= 0)
		{
			SfxNote n = g_sfx_q[i];
			g_sfx_q[i] = g_sfx_q[--g_sfx_qn];
			sfx (n.hz, n.ms, n.wave, n.vol);
		}
		else i++;
	for (int v = 0; v < SFX_NV; v++)
		if (g_sfx_end[v] && (int) (now - g_sfx_end[v]) >= 0) { ak_fm_stop (SFX_V0 + v); g_sfx_end[v] = 0; }
}
static inline void sfx_set_mute (bool m)
{
	g_sfx_mute = m;
	if (m && g_sfx_state == 1) { for (int v = 0; v < SFX_NV; v++) ak_fm_stop (SFX_V0 + v); g_sfx_qn = 0; }
}
// Two stock jingles.
static inline void sfx_win (void)
{ static const unsigned n[] = { 523, 659, 784, 1047 }; for (int i = 0; i < 4; i++) sfx_later (i * 120, n[i], i == 3 ? 360 : 110); }
static inline void sfx_lose (void)
{ static const unsigned n[] = { 392, 330, 262, 196 }; for (int i = 0; i < 4; i++) sfx_later (i * 160, n[i], 150, SOUND_TRIANGLE, 120); }

// ---- text ------------------------------------------------------------------------------
static inline int gtext_w (const char *s, int scale = 1)
{
	uikit::Font &f = uikit::font ();
	int cw = f.valid () ? f.width () : uikit::uk_fw ();
	return uikit::uk_len (s) * cw * scale;
}
static inline int gtext_h (int scale = 1)
{
	uikit::Font &f = uikit::font ();
	return (f.valid () ? f.height () : uikit::uk_fh ()) * scale;
}
static inline void gtext (uikit::Canvas &c, int x, int y, const char *s, unsigned col, int scale = 1, int style = 0)
{
	uikit::Font &f = uikit::font ();
	if (f.valid ()) c.drawFont (x, y, s, f, col, scale, style);
	else c.text (x, y, s, col);
}
// Centred at cx, with a 1-px dark shadow.
static inline void gtext_c (uikit::Canvas &c, int cx, int y, const char *s, unsigned col, int scale = 1, int style = 2)
{
	int x = cx - gtext_w (s, scale) / 2;
	gtext (c, x + scale, y + scale, s, 0x00000000, scale, style);
	gtext (c, x, y, s, col, scale, style);
}
// Integer -> decimal (returns the length).
static inline int gitoa (long v, char *b)
{
	char t[24]; int n = 0, k = 0;
	if (v < 0) { b[k++] = '-'; v = -v; }
	do { t[n++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (n) b[k++] = t[--n];
	b[k] = '\0';
	return k;
}
static inline void gcat (char *d, const char *s) { int n = uikit::uk_len (d); while (*s) d[n++] = *s++; d[n] = '\0'; }
static inline void gcatn (char *d, long v) { char t[24]; gitoa (v, t); gcat (d, t); }

// ---- scaling a game's picture to its window --------------------------------------------------------------
// A game drawn at its own size (lw x lh) shown in a window of another size -- a desktop window resized, PocketUI's
// filled window (docs/POCKETUI-TECH-STUDY.md section 6): scaled to fit, its aspect kept, centred. Twice its size or
// more: by a whole factor, each pixel a square (crisp); otherwise (a little bigger, or smaller -- a pocket's
// 800 x 480): filtered (bilinear). gscale_fit chooses the place, gscale_blit copies (src: lw x lh, srcStride a row).
struct GScale { int x, y, w, h; };		// the picture's place in the window
static inline GScale gscale_fit (int lw, int lh, int ww, int wh)
{
	GScale g;
	long sx = (long) ww * 65536 / lw, sy = (long) wh * 65536 / lh, s = sx < sy ? sx : sy;
	if (s >= 2 * 65536) s &= ~65535L;			// (a whole factor)
	g.w = (int) (lw * s >> 16); g.h = (int) (lh * s >> 16);
	if (g.w < 1) g.w = 1;
	if (g.h < 1) g.h = 1;
	g.x = (ww - g.w) / 2; g.y = (wh - g.h) / 2;
	return g;
}
static inline void gscale_blit (const unsigned *src, int lw, int lh, int srcStride, unsigned *dst, int dstStride, GScale g)
{
	if (g.w % lw == 0 && g.h % lh == 0 && g.w / lw == g.h / lh)	// a whole factor: nearest
	{
		int k = g.w / lw;
		for (int y = 0; y < g.h; y++)
		{
			const unsigned *s = src + (y / k) * srcStride; unsigned *d = dst + (g.y + y) * dstStride + g.x;
			for (int x = 0; x < lw; x++) { unsigned c = s[x]; for (int i = 0; i < k; i++) *d++ = c; }
		}
		return;
	}
	static int *xs; static unsigned char *xa; static int xcap;	// (the columns' sources and weights)
	if (xcap < g.w) { delete[] xs; delete[] xa; xs = new int[g.w]; xa = new unsigned char[g.w]; xcap = g.w; }
	for (int x = 0; x < g.w; x++)
	{
		long f = ((long) (2 * x + 1) * lw << 15) / g.w - 32768; if (f < 0) f = 0;
		xs[x] = (int) (f >> 16); xa[x] = (unsigned char) (f >> 8 & 255);
		if (xs[x] >= lw - 1) { xs[x] = lw - 1; xa[x] = 0; }
	}
	for (int y = 0; y < g.h; y++)
	{
		long f = ((long) (2 * y + 1) * lh << 15) / g.h - 32768; if (f < 0) f = 0;
		int y0 = (int) (f >> 16); unsigned ay = (unsigned) (f >> 8 & 255);
		if (y0 >= lh - 1) { y0 = lh - 1; ay = 0; }
		const unsigned *r0 = src + y0 * srcStride, *r1 = y0 + 1 < lh ? r0 + srcStride : r0;
		unsigned *d = dst + (g.y + y) * dstStride + g.x;
		for (int x = 0; x < g.w; x++)
		{
			int x0 = xs[x], x1 = x0 + 1 < lw ? x0 + 1 : x0; unsigned ax = xa[x];
			unsigned a = r0[x0], b = r0[x1], c = r1[x0], e = r1[x1], out = 0;
			for (int sh = 0; sh < 24; sh += 8)
			{
				unsigned t = ((a >> sh & 255) * (256 - ax) + (b >> sh & 255) * ax) >> 8;
				unsigned u = ((c >> sh & 255) * (256 - ax) + (e >> sh & 255) * ax) >> 8;
				out |= ((t * (256 - ay) + u * ay) >> 8) << sh;
			}
			d[x] = out;
		}
	}
}

// The window around the picture: each band in the colour of the picture's edge beside it (a card table's green,
// the night of a shooter), so the game seems to fill the window
static inline void gscale_bands (uikit::Canvas &cv, const unsigned *src, int lw, int lh, int srcStride, GScale g)
{
	if (g.x > 0)
	{
		cv.fillRect (0, 0, g.x, cv.h, src[(lh / 2) * srcStride]);
		cv.fillRect (g.x + g.w, 0, cv.w - g.x - g.w, cv.h, src[(lh / 2) * srcStride + lw - 1]);
	}
	if (g.y > 0)
	{
		cv.fillRect (0, 0, cv.w, g.y, src[lw / 2]);
		cv.fillRect (0, g.y + g.h, cv.w, cv.h - g.y - g.h, src[(lh - 1) * srcStride + lw / 2]);
	}
}

// ---- the view ----------------------------------------------------------------------------
// Subclass and override paint (the whole view, into `canvas`), the mouse edges, key and
// tick (dt in ms, ~60 Hz). Call redraw () when something changed (tick-driven games can
// just redraw every tick).
// Scaled (GameRoot::scaleToFit, the window not at the game's size): the view covers the window, but paint (), the
// handlers and tick () see the game's own size (width x height, canvas) and its coordinates, as always.
class GameView : public uikit::Widget
{
public:
	int  mx, my;					// last pointer position (view coords)
	bool lb, rb;					// buttons held
	GameView (int l, int t, int w, int h) : uikit::Widget (l, t, w, h), mx (0), my (0), lb (false), rb (false), m_last (0),
		m_lw (0), m_lh (0)
	{ canFocus = true; }
	virtual void paint () = 0;
	virtual void press (int, int, bool) {}		// (x, y, right button?)
	virtual void release (int, int, bool) {}
	virtual void move (int, int) {}			// pointer moved (buttons in lb / rb)
	virtual bool key (long) { return false; }
	virtual void tick (unsigned) {}
	void redraw () { invalidate (true); }

	// Shown in ww x wh: at the game's own size (lw x lh, its first) it is drawn as always; otherwise scaled.
	void fitTo (int ww, int wh)
	{
		if (!m_lw) { m_lw = width; m_lh = height; }
		left = top = 0;
		if (ww == m_lw && wh == m_lh) { m_off.release (); resizeTo (ww, wh); return; }
		if (!m_off.px) m_off.alloc (m_lw, m_lh);
		m_g = gscale_fit (m_lw, m_lh, ww, wh);
		resizeTo (ww, wh);
		invalidate (true);
	}
	bool scaled () const { return m_off.px != 0; }

	void onDraw () override
	{
		if (!scaled ()) { paint (); return; }
		Logical l (this);
		swap (canvas, m_off);				// (paint () draws into the game's own canvas)
		paint ();
		swap (canvas, m_off);
		l.end ();
		gscale_bands (canvas, m_off.px, m_lw, m_lh, m_off.stride, m_g);
		gscale_blit (m_off.px, m_lw, m_lh, m_off.stride, canvas.px, canvas.stride, m_g);
	}
	bool onMouse (int x, int y, int bl, int br, int, int) override
	{
		if (scaled () && x >= 0)
		{
			x = (x - m_g.x) * m_lw / m_g.w; y = (y - m_g.y) * m_lh / m_g.h;
			if (x < 0) x = 0;
			if (y < 0) y = 0;
			if (x >= m_lw) x = m_lw - 1;
			if (y >= m_lh) y = m_lh - 1;
		}
		Logical l (this);
		if (x < 0) { if (lb) release (mx, my, false); if (rb) release (mx, my, true); lb = rb = false; return false; }
		bool moved = x != mx || y != my;
		mx = x; my = y;
		if (bl && !lb) { lb = true; setFocus (); press (x, y, false); }
		else if (!bl && lb) { lb = false; release (x, y, false); }
		if (br && !rb) { rb = true; press (x, y, true); }
		else if (!br && rb) { rb = false; release (x, y, true); }
		if (moved) move (x, y);
		return true;
	}
	bool onKey (long k) override { Logical l (this); return key (k); }
	void step ()
	{
		unsigned now = gms ();
		unsigned dt = m_last ? now - m_last : 16;
		if (dt > 100) dt = 100;
		m_last = now;
		sfx_tick ();
		Logical l (this);
		tick (dt);
	}
private:
	unsigned m_last;
	int m_lw, m_lh;					// the game's own size (0: never fitted)
	uikit::Canvas m_off;				// scaled: the game's picture at its own size
	GScale m_g;					// ... and its place in the view
	static void swap (uikit::Canvas &a, uikit::Canvas &b)	// (a Canvas is not copied: it may own its pixels)
	{
		unsigned *p = a.px; a.px = b.px; b.px = p;
		int t = a.w; a.w = b.w; b.w = t; t = a.h; a.h = b.h; b.h = t;
		t = a.stride; a.stride = b.stride; b.stride = t; t = a.capH; a.capH = b.capH; b.capH = t;
		bool o = a.owns; a.owns = b.owns; b.owns = o; void *e = a.ext; a.ext = b.ext; b.ext = e;
	}
	// While the game's code runs, the view's size is the game's own (scaled: it covers the window)
	struct Logical
	{
		GameView *v; int w, h; bool on;
		Logical (GameView *g) : v (g), w (g->width), h (g->height), on (g->scaled ()) { if (on) { v->width = v->m_lw; v->height = v->m_lh; } }
		void end () { if (on) { v->width = w; v->height = h; on = false; } }
		~Logical () { end (); }
	};
};

// A Root that ticks its GameView and routes every key to it. scaleToFit (): the window resizable (PocketUI fills
// it), the view scaled to it -- for a game drawn at a fixed size.
class GameRoot : public uikit::Root
{
public:
	GameView *view;
	GameRoot (int w, int h, const char *title) : uikit::Root (w, h, title), view (0), m_scale (false) {}
	void onTick () override { if (view) view->step (); }
	bool onKey (long k) override { return view && !view->hasFocus ? view->key (k) : false; }
	void scaleToFit () { m_scale = true; setResizable (true); setMinSize (width / 2, height / 2); }
	void onResized () override { if (m_scale && view) view->fitTo (width, height); }
private:
	bool m_scale;
};

// ---- a game in a plain window (no Root): its own pixels, scaled to the window --------------------------------
// For the games that draw straight into their window's canvas: gwin_create (w, h, title) makes the window (resizable:
// a frame dragged, maximised, PocketUI's fill) and gives the game ITS pixels (w x h, w a row) to draw into as before;
// gwin_present () shows them -- copied when the window is at the game's size, scaled to it otherwise (gscale_*); a
// click handler given to gwin_on_click (in the place of uk_win_on_click) gets GUI_EVENT_CANVAS_CLICK / _MOTION in
// the game's coordinates.
static int g_gw_lw, g_gw_lh, g_gw_ww, g_gw_wh, g_gw_stride, g_gw_btn;
static unsigned *g_gw_px, *g_gw_win;
static GScale g_gw_g;
static gui_handler g_gw_click;
static bool g_gw_max;
static int g_gw_rx, g_gw_ry, g_gw_rw, g_gw_rh;		// (maximised: the place and size to go back to)
static inline void gwin_size (int x, int y, int w, int h)	// the window made w x h at x, y
{
	if (w < 1 || h < 1) return;
	if (w != g_gw_ww || h != g_gw_wh)
	{
		int stride = w;
		unsigned *p = uk_win_resize2 (w, h, &stride);
		if (p == 0) return;
		g_gw_win = p; g_gw_ww = w; g_gw_wh = h; g_gw_stride = stride;
	}
	uk_win_move (x, y);
	uikit::uk_decorate_window ();
	g_gw_g = gscale_fit (g_gw_lw, g_gw_lh, g_gw_ww, g_gw_wh);
}
static inline void gwin_pointer (unsigned long, int ev, long v)
{
	if (ev == GUI_EVENT_WINRESIZE) { g_gw_max = false; gwin_size (GUI_WINRESIZE_X (v), GUI_WINRESIZE_Y (v), GUI_WINRESIZE_W (v), GUI_WINRESIZE_H (v)); return; }
	if (ev == GUI_EVENT_WINCTL)
	{
		if (v != KAPI_FRAME_MAXIMISE) return;
		struct kapi_win_geom g;
		if (uk_win_geometry (&g) != 0) return;
		if (!g_gw_max) { g_gw_rx = g.x; g_gw_ry = g.y; g_gw_rw = g_gw_ww; g_gw_rh = g_gw_wh; gwin_size (g.ax, g.ay, g.aw - (g.w - g.cw), g.ah - (g.h - g.ch)); }
		else gwin_size (g_gw_rx, g_gw_ry, g_gw_rw, g_gw_rh);
		g_gw_max = !g_gw_max;
		return;
	}
	if (ev != GUI_EVENT_PTR_DOWN && ev != GUI_EVENT_PTR_MOVE && ev != GUI_EVENT_PTR_UP) return;
	int x = (GUI_PTR_X (v) - g_gw_g.x) * g_gw_lw / g_gw_g.w, y = (GUI_PTR_Y (v) - g_gw_g.y) * g_gw_lh / g_gw_g.h;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= g_gw_lw) x = g_gw_lw - 1;
	if (y >= g_gw_lh) y = g_gw_lh - 1;
	int held = GUI_PTR_BUTTONS (v) & 3, was = g_gw_btn;
	g_gw_btn = held;
	if (!g_gw_click) return;
	long pos = ((long) x << 16) | (y & 0xFFFF);
	if (ev == GUI_EVENT_PTR_DOWN && (held & ~was)) g_gw_click (0, GUI_EVENT_CANVAS_CLICK, ((long) (held & ~was & 1 ? 1 : 2) << 32) | pos);
	else if (ev == GUI_EVENT_PTR_MOVE && held) g_gw_click (0, GUI_EVENT_CANVAS_MOTION, ((long) held << 32) | pos);
}
static inline unsigned *gwin_create (int w, int h, const char *title)
{
	g_gw_win = uk_win_create (w, h, title);
	if (g_gw_win == 0) return 0;
	g_gw_lw = g_gw_ww = g_gw_stride = w; g_gw_lh = g_gw_wh = h;
	g_gw_px = new unsigned[w * h];
	for (int i = 0; i < w * h; i++) g_gw_px[i] = 0;
	g_gw_g = gscale_fit (w, h, w, h);
	uk_win_resizable (1, w / 2, h / 2);
	uk_win_on_pointer (gwin_pointer);
	return g_gw_px;
}
static inline void gwin_on_click (gui_handler fn) { g_gw_click = fn; }
static inline void gwin_present (void)
{
	if (g_gw_ww == g_gw_lw && g_gw_wh == g_gw_lh)
		for (int y = 0; y < g_gw_lh; y++) for (int x = 0; x < g_gw_lw; x++) g_gw_win[y * g_gw_stride + x] = g_gw_px[y * g_gw_lw + x];
	else
	{
		uikit::Canvas cv; cv.adopt (g_gw_win, g_gw_ww, g_gw_wh, g_gw_stride);
		gscale_bands (cv, g_gw_px, g_gw_lw, g_gw_lh, g_gw_lw, g_gw_g);
		gscale_blit (g_gw_px, g_gw_lw, g_gw_lh, g_gw_lw, g_gw_win, g_gw_stride, g_gw_g);
	}
	uk_win_present ();
}

#endif
