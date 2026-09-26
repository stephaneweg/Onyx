//
// game.h -- small shared kit for the wtk games (Solitaire, FreeCell, Pipes, Arkanoid,
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

#include "kapi.h"
#include "wtk/wtk.h"

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
	if (g_sfx_state == 0) g_sfx_state = kapi_sound_acquire () == 1 ? 1 : -1;
	return g_sfx_state == 1;
}
// Play hz (Hz) for ms milliseconds now, on the next free effect voice.
static inline void sfx (unsigned hz, unsigned ms, int wave = SOUND_SQUARE, int vol = 90)
{
	if (!sfx_ready () || hz == 0) return;
	int v = g_sfx_next; g_sfx_next = (g_sfx_next + 1) % SFX_NV;
	kapi_sound_start (SFX_V0 + v, hz * 1000u, wave, vol);
	g_sfx_end[v] = kapi_get_ticks () + ms;
}
// Queue a note to start delay ms from now (a jingle = several of these).
static inline void sfx_later (unsigned delay, unsigned hz, unsigned ms, int wave = SOUND_SQUARE, int vol = 90)
{
	if (!sfx_ready () || g_sfx_qn >= SFX_Q) return;
	SfxNote &n = g_sfx_q[g_sfx_qn++];
	n.at = kapi_get_ticks () + delay; n.hz = hz; n.ms = ms; n.wave = wave; n.vol = vol;
}
static inline void sfx_tick (void)
{
	if (g_sfx_state != 1) return;
	unsigned now = kapi_get_ticks ();
	for (int i = 0; i < g_sfx_qn; )
		if ((int) (now - g_sfx_q[i].at) >= 0)
		{
			SfxNote n = g_sfx_q[i];
			g_sfx_q[i] = g_sfx_q[--g_sfx_qn];
			sfx (n.hz, n.ms, n.wave, n.vol);
		}
		else i++;
	for (int v = 0; v < SFX_NV; v++)
		if (g_sfx_end[v] && (int) (now - g_sfx_end[v]) >= 0) { kapi_sound_stop (SFX_V0 + v); g_sfx_end[v] = 0; }
}
static inline void sfx_set_mute (bool m)
{
	g_sfx_mute = m;
	if (m && g_sfx_state == 1) { for (int v = 0; v < SFX_NV; v++) kapi_sound_stop (SFX_V0 + v); g_sfx_qn = 0; }
}
// Two stock jingles.
static inline void sfx_win (void)
{ static const unsigned n[] = { 523, 659, 784, 1047 }; for (int i = 0; i < 4; i++) sfx_later (i * 120, n[i], i == 3 ? 360 : 110); }
static inline void sfx_lose (void)
{ static const unsigned n[] = { 392, 330, 262, 196 }; for (int i = 0; i < 4; i++) sfx_later (i * 160, n[i], 150, SOUND_TRIANGLE, 120); }

// ---- text ------------------------------------------------------------------------------
static inline int gtext_w (const char *s, int scale = 1)
{
	wtk::Font &f = wtk::font ();
	int cw = f.valid () ? f.width () : wtk::wk_fw ();
	return wtk::wk_len (s) * cw * scale;
}
static inline int gtext_h (int scale = 1)
{
	wtk::Font &f = wtk::font ();
	return (f.valid () ? f.height () : wtk::wk_fh ()) * scale;
}
static inline void gtext (wtk::Canvas &c, int x, int y, const char *s, unsigned col, int scale = 1, int style = 0)
{
	wtk::Font &f = wtk::font ();
	if (f.valid ()) c.drawFont (x, y, s, f, col, scale, style);
	else c.text (x, y, s, col);
}
// Centred at cx, with a 1-px dark shadow.
static inline void gtext_c (wtk::Canvas &c, int cx, int y, const char *s, unsigned col, int scale = 1, int style = 2)
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
static inline void gcat (char *d, const char *s) { int n = wtk::wk_len (d); while (*s) d[n++] = *s++; d[n] = '\0'; }
static inline void gcatn (char *d, long v) { char t[24]; gitoa (v, t); gcat (d, t); }

// ---- the view ----------------------------------------------------------------------------
// Subclass and override paint (the whole view, into `canvas`), the mouse edges, key and
// tick (dt in ms, ~60 Hz). Call redraw () when something changed (tick-driven games can
// just redraw every tick).
class GameView : public wtk::Widget
{
public:
	int  mx, my;					// last pointer position (view coords)
	bool lb, rb;					// buttons held
	GameView (int l, int t, int w, int h) : wtk::Widget (l, t, w, h), mx (0), my (0), lb (false), rb (false), m_last (0)
	{ canFocus = true; }
	virtual void paint () = 0;
	virtual void press (int, int, bool) {}		// (x, y, right button?)
	virtual void release (int, int, bool) {}
	virtual void move (int, int) {}			// pointer moved (buttons in lb / rb)
	virtual bool key (long) { return false; }
	virtual void tick (unsigned) {}
	void redraw () { invalidate (true); }

	void onDraw () override { paint (); }
	bool onMouse (int x, int y, int bl, int br, int, int) override
	{
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
	bool onKey (long k) override { return key (k); }
	void step ()
	{
		unsigned now = kapi_get_ticks ();
		unsigned dt = m_last ? now - m_last : 16;
		if (dt > 100) dt = 100;
		m_last = now;
		sfx_tick ();
		tick (dt);
	}
private:
	unsigned m_last;
};

// A Root that ticks its GameView and routes every key to it.
class GameRoot : public wtk::Root
{
public:
	GameView *view;
	GameRoot (int w, int h, const char *title) : wtk::Root (w, h, title), view (0) {}
	void onTick () override { if (view) view->step (); }
	bool onKey (long k) override { return view && !view->hasFocus ? view->key (k) : false; }
};

#endif
