//
// show.h -- the slide show, full screen: each slide's layers composited by the GPU at the screen's size,
// its effects played click by click (an object appearing, fading, flying in, wiped, zoomed, floating in;
// growing, pulsing, spinning; going out) -- each a layer's matrix, opacity or clip changing, nothing drawn
// again --, the transitions between slides (fade, push, wipe, cover, uncover, split, zoom, dissolve: the
// two slides as two textures). The keys: Space / Enter / Right / Down / Page Down / a click: on; Left / Up /
// Page Up / Backspace / a right click: back; Home, End; a number then Enter: that slide; B / W: a black / white
// screen; Esc: the end. The presenter's console (View > Presenter View): the slide, the next one, the notes
// large, the time, on the one screen.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_show_h
#define _slides_show_h

#include "editor.h"

namespace sl {

static volatile long g_shKeys[16]; static volatile int g_shKn;
static volatile int g_shClick, g_shRClick, g_shMoved;
static void sh_key (unsigned long, int ev, gui_value v) { if (ev == GUI_EVENT_KEY && g_shKn < 16) g_shKeys[g_shKn++] = (long) v; }
static void sh_ptr (unsigned long, int ev, gui_value v)
{
	if (ev == GUI_EVENT_PTR_MOVE) g_shMoved = 1;
	if (ev == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (v) & 1)) g_shClick = 1;
	if (ev == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (v) & 2)) g_shRClick = 1;
}
static long sh_next_key () { if (!g_shKn) return 0; long k = g_shKeys[0]; for (int i = 1; i < g_shKn; i++) g_shKeys[i - 1] = g_shKeys[i]; g_shKn--; return k; }

static inline float ease (float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t < 0.5f ? 2 * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) / 2; }

// ---- the effects' timeline -----------------------------------------------------------------------------------------
// The effects of a slide in steps (a click starts one; With / After previous follow in it); each effect's start
// and end in ms from its step's start.
struct FxTime { int step; int t0, t1; };
static int fx_plan (const Slide &s, FxTime *ft)
{
	int step = 0, prevStart = 0, prevEnd = 0, n = 0;
	for (int i = 0; i < s.anim.n && i < 128; i++)
	{
		const Anim &a = s.anim[i];
		if (a.start == ST_CLICK && i > 0) { step++; prevStart = prevEnd = 0; }
		int t0 = a.start == ST_AFTER ? prevEnd + a.delay : a.start == ST_WITH ? prevStart + a.delay : a.delay;
		if (a.start == ST_CLICK) t0 = a.delay;
		ft[i].step = step; ft[i].t0 = t0; ft[i].t1 = t0 + (a.fx == FX_APPEAR ? 1 : imax (1, a.dur));
		prevStart = t0; if (ft[i].t1 > prevEnd) prevEnd = ft[i].t1;
		n = step + 1;
	}
	// the first effect "On click"? then step 0 waits for a click; else it plays at once
	return n;
}
// an object's top-left in px at scale sc (the slide's own px)
static inline float vx_of (const Object *o, float sc) { return o->x * sc; }
static inline float vy_of (const Object *o, float sc) { return o->y * sc; }
// The moves of the objects at (step, ms into it): a hidden entrance, an effect in progress, an exit done.
static int fx_moves (const Slide &s, const FxTime *ft, int step, int ms, float sc, float sx, float sy, Move *mv, int max)
{
	int n = 0;
	for (int i = 0; i < s.anim.n && n < max; i++)
	{
		const Anim &a = s.anim[i];
		const Object *o = s.by_id (a.obj); if (!o) continue;
		// its progress: -1 not begun, 0..1 playing, 2 done
		float t;
		if (ft[i].step > step) t = -1;
		else if (ft[i].step < step) t = 2;
		else t = ms < ft[i].t0 ? -1 : ms >= ft[i].t1 ? 2 : (float) (ms - ft[i].t0) / (ft[i].t1 - ft[i].t0);
		Move m = move_of (o->id);
		bool found = false;
		for (int k = 0; k < n; k++) if (mv[k].id == o->id) { m = mv[k]; found = true; break; }
		if (a.cls == AC_ENTRANCE)
		{
			if (t < 0) m.alpha = 0;
			else if (t <= 1)
			{
				float e = ease (t);
				float ox = vx_of (o, sc), oy = vy_of (o, sc), ow = o->w * sc, oh = o->h * sc;
				switch (a.fx)
				{
				case FX_APPEAR: break;
				case FX_FADE: m.alpha = (int) (255 * t); break;
				case FX_FLY:
				{
					// (from: the side named, past the screen's edge)
					if (a.dir == DIR_RIGHT) m.dx = (1 - e) * (g_deck.sw * sc + 2 * sx - ox + 10);
					else if (a.dir == DIR_UP) m.dy = -(1 - e) * (oy + oh + sy + 10);
					else if (a.dir == DIR_LEFT) m.dx = -(1 - e) * (ox + ow + sx + 10);
					else m.dy = (1 - e) * (g_deck.sh * sc + 2 * sy - oy + 10);
					break;
				}
				case FX_WIPE:
				{
					int X = (int) (sx + ox), Y = (int) (sy + oy), W = (int) ceilf (ow), H = (int) ceilf (oh);
					if (a.dir == DIR_RIGHT) { int w = (int) (W * e); m.clip[0] = X + W - w; m.clip[1] = Y - 4; m.clip[2] = imax (1, w); m.clip[3] = H + 8; }
					else if (a.dir == DIR_UP) { int h = (int) (H * e); m.clip[0] = X - 4; m.clip[1] = Y; m.clip[2] = W + 8; m.clip[3] = imax (1, h); }
					else if (a.dir == DIR_DOWN || a.dir == DIR_NONE) { int h = (int) (H * e); m.clip[0] = X - 4; m.clip[1] = Y + H - h; m.clip[2] = W + 8; m.clip[3] = imax (1, h); }
					else { int w = (int) (W * e); m.clip[0] = X - 4; m.clip[1] = Y - 4; m.clip[2] = imax (1, w + 4); m.clip[3] = H + 8; }
					break;
				}
				case FX_ZOOM: m.k = 0.3f + 0.7f * e; m.alpha = (int) (255 * fminf (1, t * 1.5f)); break;
				case FX_FLOAT: m.dy = (1 - e) * oh * 0.4f + (1 - e) * 40; m.alpha = (int) (255 * t); break;
				case FX_GROW: m.k = 0.1f + 0.9f * e; break;
				case FX_SPIN: m.rot = (int) (-360 * (1 - e)); m.alpha = (int) (255 * t); break;
				default: m.alpha = (int) (255 * t); break;
				}
			}
		}
		else if (a.cls == AC_EMPHASIS)
		{
			if (t >= 0 && t <= 1)
			{
				switch (a.fx)
				{
				case FX_SPIN: m.rot = (int) (360 * ease (t)); break;
				case FX_GROW: m.k = 1 + 0.18f * sinf (t * 3.14159265f); break;
				default: m.k = 1 + 0.09f * sinf (t * 6.2831853f * 1.0f) * (t < 0.5f ? 1 : 1); break;	// (pulse)
				}
			}
			else if (t > 1 && a.fx == FX_GROW) m.k = 1;
		}
		else
		{
			if (t > 1) m.alpha = 0;
			else if (t >= 0)
			{
				float e = ease (t);
				switch (a.fx)
				{
				case FX_FLY: m.dy = e * (g_deck.sh * sc + 2 * sy - o->y * sc + 10); break;
				case FX_ZOOM: m.k = 1 - 0.7f * e; m.alpha = (int) (255 * (1 - t)); break;
				case FX_SPIN: m.rot = (int) (360 * e); m.alpha = (int) (255 * (1 - t)); break;
				case FX_APPEAR: m.alpha = 0; break;
				default: m.alpha = (int) (255 * (1 - t)); break;
				}
			}
		}
		if (found) { for (int k = 0; k < n; k++) if (mv[k].id == o->id) mv[k] = m; }
		else mv[n++] = m;
	}
	return n;
}

// ---- the show ---------------------------------------------------------------------------------------------------------
struct Show
{
	Compositor C;
	unsigned *fb; int W, H;
	unsigned *tgt; int tstride;
	float sc, sx, sy;
	bool presenter;
	unsigned startT;
	// the slides shown (the hidden ones skipped)
	int next_shown (int i, int dir) { i += dir; while (i >= 0 && i < g_deck.slides.n && g_deck.slides[i]->hidden) i += dir; return i; }

	void place (int w, int h)
	{
		sc = fminf ((float) w / g_deck.sw, (float) h / g_deck.sh);
		sx = floorf ((w - g_deck.sw * sc) / 2); sy = floorf ((h - g_deck.sh * sc) / 2);
	}
	// A slide's frame at its state (step, ms) composited into tgt.
	void compose (int slide, int step, int ms, bool blank, unsigned blankC)
	{
		if (blank) { for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) tgt[(long) y * tstride + x] = blankC; return; }
		Slide &s = *g_deck.slides[slide];
		Frame F; C.frame++;
		build_frame (C, g_deck, s, slide, sc, false, F);
		FxTime ft[128]; fx_plan (s, ft);
		Move mv[128]; int n = fx_moves (s, ft, step, ms, sc, sx, sy, mv, 128);
		composite_frame (C, F, tgt, W, H, tstride, sx, sy, 0x000000, mv, n);
		C.sweep (6);
	}
	void present ()
	{
		for (int y = 0; y < H; y++) memcpy (fb + (long) y * W, tgt + (long) y * tstride, (size_t) W * 4);
		kapi_present_fb ();
	}
	// The slide as it ends (every effect done) / begins, into a texture for a transition.
	gpc_tex *snapshot (int slide, int step, int ms)
	{
		compose (slide, step, ms, false, 0);
		return gpc_tex_create (C.g, W, H, tgt, tstride);
	}
	// The transition from texture A to B: t 0..1.
	void transition_frame (gpc_tex *A, gpc_tex *B, const Transition &tr, float t)
	{
		float e = ease (t);
		gpc_layer L[3]; int n = 0;
		auto layer = [&] (gpc_tex *tx, float dx, float dy, float k, int alpha, int cx, int cy, int cw, int ch) -> gpc_layer & {
			gpc_layer &g = L[n++]; gpc_layer_init (&g, tx);
			gpc_matrix_identity (&g.m);
			gpc_matrix_translate (&g.m, W / 2.0f + dx, H / 2.0f + dy); gpc_matrix_scale (&g.m, k, k); gpc_matrix_translate (&g.m, -W / 2.0f, -H / 2.0f);
			g.opacity = (unsigned) alpha; g.clip[0] = cx; g.clip[1] = cy; g.clip[2] = cw; g.clip[3] = ch;
			return g;
		};
		int dir = tr.dir ? tr.dir : DIR_LEFT;
		float ux = dir == DIR_LEFT ? 1 : dir == DIR_RIGHT ? -1 : 0, uy = dir == DIR_UP ? 1 : dir == DIR_DOWN ? -1 : 0;
		// (DIR_LEFT: the new slide comes from the right, going left)
		switch (tr.type)
		{
		case TR_FADE: case TR_DISSOLVE: layer (A, 0, 0, 1, 255, 0, 0, 0, 0); layer (B, 0, 0, 1, (int) (255 * e), 0, 0, 0, 0); break;
		case TR_PUSH: layer (A, -ux * W * e, -uy * H * e, 1, 255, 0, 0, 0, 0); layer (B, ux * W * (1 - e), uy * H * (1 - e), 1, 255, 0, 0, 0, 0); break;
		case TR_COVER: layer (A, 0, 0, 1, 255, 0, 0, 0, 0); layer (B, ux * W * (1 - e), uy * H * (1 - e), 1, 255, 0, 0, 0, 0); break;
		case TR_UNCOVER: layer (B, 0, 0, 1, 255, 0, 0, 0, 0); layer (A, -ux * W * e, -uy * H * e, 1, 255, 0, 0, 0, 0); break;
		case TR_WIPE:
		{
			layer (A, 0, 0, 1, 255, 0, 0, 0, 0);
			int w = (int) (W * e), h = (int) (H * e);
			if (dir == DIR_LEFT) layer (B, 0, 0, 1, 255, W - w, 0, imax (1, w), H);
			else if (dir == DIR_RIGHT) layer (B, 0, 0, 1, 255, 0, 0, imax (1, w), H);
			else if (dir == DIR_UP) layer (B, 0, 0, 1, 255, 0, H - h, W, imax (1, h));
			else layer (B, 0, 0, 1, 255, 0, 0, W, imax (1, h));
			break;
		}
		case TR_SPLIT: { layer (A, 0, 0, 1, 255, 0, 0, 0, 0); int w = (int) (W * e); layer (B, 0, 0, 1, 255, (W - w) / 2, 0, imax (1, w), H); break; }
		case TR_ZOOM: layer (A, 0, 0, 1 + 0.15f * e, (int) (255 * (1 - e)), 0, 0, 0, 0); layer (B, 0, 0, 0.6f + 0.4f * e, (int) (255 * e), 0, 0, 0, 0); break;
		default: layer (B, 0, 0, 1, 255, 0, 0, 0, 0); break;
		}
		gpc_target tg; tg.pixels = tgt; tg.w = W; tg.h = H; tg.stride = tstride; tg.flags = 0;
		gpc_composite (C.g, &tg, L, n, 0x000000, GPC_C_CLEAR);
	}

	// ---- the presenter's console ----------------------------------------------------------------------------------
	void text (Canvas &cv, int x, int y, const char *s, int px, unsigned c, bool bold = false, int maxW = 100000)
	{
		fnt::Font *f = fnt::get (family_of ("DejaVu Sans"), bold ? fnt::BOLD : 0, px * 64);
		if (!f) return;
		float X = (float) x; Surf su; su.px = cv.px; su.w = cv.w; su.h = cv.h; su.stride = cv.stride; su.pm = false; su.clip_all ();
		for (const char *p = s; *p; )
		{
			int l; unsigned ch = ss::u8_dec (p, (int) strlen (p), &l); p += l > 0 ? l : 1;
			float a = fnt::advance (f, ch) / 64.0f;
			if (X + a > x + maxW) break;
			surf_glyph (su, f, X, y + f->ascent / 64, ch, c); X += a;
		}
	}
	int text_w (const char *s, int px, bool bold = false)
	{
		fnt::Font *f = fnt::get (family_of ("DejaVu Sans"), bold ? fnt::BOLD : 0, px * 64); if (!f) return 0;
		float w = 0; for (const char *p = s; *p; ) { int l; unsigned ch = ss::u8_dec (p, (int) strlen (p), &l); p += l > 0 ? l : 1; w += fnt::advance (f, ch) / 64.0f; }
		return (int) w;
	}
	void console (int slide, int step, int ms, int steps, bool blank, unsigned blankC, int next)
	{
		unsigned BG = 0x1A1C20, BG2 = 0x282B30, FG = 0xECECEC, FG2 = 0x969AA0, PEACH = 0xF0A86E;
		for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) tgt[(long) y * tstride + x] = BG;
		// the current slide, at the left
		int cw = W * 59 / 100, ch = cw * g_deck.sh / g_deck.sw, cx = 24, cy = 72;
		if (cy + ch > H * 62 / 100) { ch = H * 62 / 100 - cy; cw = ch * g_deck.sw / g_deck.sh; }
		float osc = sc, osx = sx, osy = sy;
		sc = (float) cw / g_deck.sw; sx = (float) cx; sy = (float) cy;
		if (blank) { for (int y = cy; y < cy + ch; y++) for (int x = cx; x < cx + cw; x++) tgt[(long) y * tstride + x] = blankC; }
		else
		{
			Slide &s = *g_deck.slides[slide];
			Frame F; C.frame++;
			build_frame (C, g_deck, s, slide, sc, false, F);
			FxTime ft[128]; fx_plan (s, ft);
			Move mv[128]; int n = fx_moves (s, ft, step, ms, sc, sx, sy, mv, 128);
			composite_frame (C, F, tgt, W, H, tstride, sx, sy, 0, mv, n);
			// (the clear above was undone by GPC_C_CLEAR: the console's background drawn again round the slide)
			for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (x < cx || x >= cx + cw || y < cy || y >= cy + ch) tgt[(long) y * tstride + x] = BG;
		}
		Canvas cv; cv.adopt (tgt, W, H, tstride);
		// the next slide
		int nx = cx + cw + 24, nw = W - nx - 24, nh = nw * g_deck.sh / g_deck.sw;
		text (cv, nx, cy - 4, "Next", 14, FG2, true);
		if (next >= 0 && next < g_deck.slides.n)
		{
			unsigned *px = (unsigned *) malloc ((size_t) nw * nh * 4);
			Compositor &K = C; K.frame++;
			flatten_slide (K, g_deck, *g_deck.slides[next], next, px, nw, nh, nw);
			for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) tgt[(long) (cy + 18 + y) * tstride + nx + x] = px[y * nw + x];
			free (px);
		}
		else text (cv, nx, cy + 30, "The end of the show", 16, FG2);
		// the steps still to come on this slide
		Slide &s = *g_deck.slides[slide];
		char t[160];
		int ey = cy + 18 + nh + 26;
		if (s.anim.n) { snprintf (t, sizeof t, "Effects: %d of %d clicks played", imin (step, steps), steps); text (cv, nx, ey, t, 13, FG2); }
		// the top bar: where we are, the time, the clock
		for (int y = 0; y < 48; y++) for (int x = 0; x < W; x++) tgt[(long) y * tstride + x] = BG2;
		int shown = 0, at = 0; for (int i = 0; i < g_deck.slides.n; i++) if (!g_deck.slides[i]->hidden) { shown++; if (i <= slide) at++; }
		snprintf (t, sizeof t, "Slide %d of %d", at, shown); text (cv, 20, 13, t, 20, FG, true);
		const char *sec = section_name (slide); if (sec[0]) { snprintf (t, sizeof t, "\xC2\xB7  %s", sec); text (cv, 30 + text_w ("Slide 00 of 00", 20, true), 16, t, 15, FG2); }
		unsigned el = (kapi_get_ticks () - startT) / 100;
		snprintf (t, sizeof t, "%02u:%02u:%02u", el / 3600, el / 60 % 60, el % 60);
		text (cv, (W - text_w (t, 28, true)) / 2, 8, t, 28, FG, true);
		int yy, mo, dd, hh, mi, ss_; (void) yy; (void) mo; (void) dd;
		if (now_time (&hh, &mi, &ss_)) { snprintf (t, sizeof t, "%02d:%02d", hh, mi); text (cv, W - 20 - text_w (t, 20, true), 13, t, 20, FG2, true); }
		for (int x = 0; x < W * at / imax (1, shown); x++) for (int y = 48; y < 52; y++) tgt[(long) y * tstride + x] = PEACH;
		// the notes
		int ny = cy + ch + 28, nh2 = H - ny - 20;
		for (int y = ny; y < ny + nh2; y++) for (int x = 20; x < W - 20; x++) tgt[(long) y * tstride + x] = BG2;
		text (cv, 32, ny + 8, "Notes", 13, FG2, true);
		Buf nb; s.notes.text_utf8 (nb);
		int ly = ny + 32, px = H >= 900 ? 22 : 18;
		const char *p = nb.str ();
		while (*p && ly + px < ny + nh2)
		{
			// a line: the words that fit
			const char *e = p, *cut = 0;
			while (*e && *e != '\n')
			{
				char line[512]; int n = (int) (e - p) + 1; if (n > 510) break;
				memcpy (line, p, n); line[n] = 0;
				if (text_w (line, px) > W - 80) break;
				if (*e == ' ') cut = e;
				e++;
			}
			if (*e && *e != '\n' && cut) e = cut;
			char line[512]; int n = imin ((int) (e - p), 510); memcpy (line, p, n); line[n] = 0;
			text (cv, 36, ly, line, px, FG);
			ly += px + px / 2;
			p = e; while (*p == ' ') p++; if (*p == '\n') p++;
		}
		text (cv, 24, H - 18, "Esc: end   \xC2\xB7   Space / \xE2\x86\x92: next   \xC2\xB7   \xE2\x86\x90: back   \xC2\xB7   B / W: black / white screen   \xC2\xB7   number + Enter: go to a slide", 12, FG2);
		sc = osc; sx = osx; sy = osy;
	}
	static const char *section_name (int slide) { for (int k = slide; k >= 0; k--) if (g_deck.slides[k]->section[0]) return g_deck.slides[k]->section; return ""; }
	static bool now_time (int *h, int *m, int *s)
	{
		int y, mo, d, hh, mi, ss_;
		if (!g_now) return false;
		g_now (&y, &mo, &d, &hh, &mi, &ss_); *h = hh; *m = mi; *s = ss_; return true;
	}
	static void (*g_now) (int *, int *, int *, int *, int *, int *);

	// ---- the loop ------------------------------------------------------------------------------------------------------
	void run (int from, bool presenterView)
	{
		if (!g_deck.slides.n) return;
		presenter = presenterView;
		fb = kapi_fullscreen_begin (&W, &H);
		if (!fb) return;
		kapi_set_key_handler (sh_key); kapi_set_pointer_handler (sh_ptr);
		g_shKn = 0; g_shClick = g_shRClick = g_shMoved = 0;
		C.init ();
		tgt = gpc_target_alloc (C.g, W, H, &tstride);
		if (!tgt) { kapi_fullscreen_end (); return; }
		place (W, H);
		startT = kapi_get_ticks ();
		int slide = from; if (g_deck.slides[slide]->hidden) slide = next_shown (slide, 1);
		if (slide < 0 || slide >= g_deck.slides.n) slide = next_shown (-1, 1);
		if (slide < 0 || slide >= g_deck.slides.n) { gpc_target_free (C.g, tgt); kapi_fullscreen_end (); return; }
		FxTime ft[128]; int steps = fx_plan (*g_deck.slides[slide], ft);
		bool firstAuto = g_deck.slides[slide]->anim.n && g_deck.slides[slide]->anim[0].start != ST_CLICK;
		int step = firstAuto ? 0 : -1;			// -1: the slide shown, its first click's effects not begun
		unsigned stepT = kapi_get_ticks (), slideDoneT = 0;
		bool blank = false; unsigned blankC = 0; int typed = 0;
		bool redraw = true, ended = false;
		for (;;)
		{
			kapi_pump_wait (10);
			if (wk_quit ()) break;
			unsigned now = kapi_get_ticks ();
			int ms = (int) (now - stepT) * 10;
			long k = sh_next_key ();
			bool next = false, back = false;
			if (g_shClick) { g_shClick = 0; next = true; }
			if (g_shRClick) { g_shRClick = 0; back = true; }
			if (k == 27) break;
			if (k == ' ' || k == KEY_ENTER || k == KEY_RIGHT || k == KEY_DOWN || k == KEY_PGDN || k == 'n' || k == 'N')
			{
				if (k == KEY_ENTER && typed > 0) { int target = -1, shownI = 0; for (int i = 0; i < g_deck.slides.n; i++) if (!g_deck.slides[i]->hidden && ++shownI == typed) target = i; typed = 0; if (target >= 0) { slide = target; steps = fx_plan (*g_deck.slides[slide], ft); step = 99; stepT = now; ended = false; redraw = true; } continue; }
				next = true;
			}
			if (k == KEY_LEFT || k == KEY_UP || k == KEY_PGUP || k == KEY_BACKSPACE || k == 'p' || k == 'P') back = true;
			if (k >= '0' && k <= '9') { typed = typed * 10 + (int) (k - '0'); continue; }
			if (k == 'b' || k == 'B' || k == '.') { blank = !blank; blankC = 0; redraw = true; }
			if (k == 'w' || k == 'W' || k == ',') { blank = !blank; blankC = 0xFFFFFF; redraw = true; }
			if (k == KEY_HOME) { slide = next_shown (-1, 1); steps = fx_plan (*g_deck.slides[slide], ft); step = -1; stepT = now; ended = false; redraw = true; }
			if (k == KEY_END) { int l = next_shown (g_deck.slides.n, -1); if (l >= 0) { slide = l; steps = fx_plan (*g_deck.slides[slide], ft); step = 99; ended = false; redraw = true; } }
			if (blank && (next || back)) { blank = false; redraw = true; next = back = false; }
			if (ended)
			{
				if (next) break;
				if (back) { ended = false; step = 99; redraw = true; }
				else continue;
			}
			// the step playing: its end
			int stepEnd = 0;
			if (step >= 0 && step < steps) for (int i = 0; i < g_deck.slides[slide]->anim.n; i++) if (ft[i].step == step && ft[i].t1 > stepEnd) stepEnd = ft[i].t1;
			bool playing = step >= 0 && step < steps && ms < stepEnd + 20;
			if (next)
			{
				if (playing) { stepT = now - (unsigned) (stepEnd / 10 + 3); redraw = true; }		// (the effect finished at once)
				else if (step + 1 < steps) { step++; stepT = now; redraw = true; }
				else
				{
					int ns = next_shown (slide, 1);
					if (ns >= g_deck.slides.n) { ended = true; draw_end (); continue; }
					go_to (slide, step, ns, &steps, ft);
					slide = ns; stepT = kapi_get_ticks (); slideDoneT = 0;
					step = g_deck.slides[slide]->anim.n && g_deck.slides[slide]->anim[0].start != ST_CLICK ? 0 : -1;
					redraw = true;
				}
			}
			else if (back)
			{
				int ps = next_shown (slide, -1);
				if (ps >= 0) { slide = ps; steps = fx_plan (*g_deck.slides[slide], ft); step = 99; stepT = now; redraw = true; }
			}
			// an automatic step (After previous chains are timed inside a step); the slide going on by itself
			if (!playing && step + 1 < steps && step >= 0 && g_deck.slides[slide]->anim[first_of (*g_deck.slides[slide], ft, step + 1)].start != ST_CLICK) { step++; stepT = now; redraw = true; }
			Slide &cs = *g_deck.slides[slide];
			bool allDone = step >= steps - 1 && !playing;
			if (allDone && cs.tr.after >= 0)
			{
				if (!slideDoneT) slideDoneT = now;
				if ((int) (now - slideDoneT) * 10 >= cs.tr.after)
				{
					int ns = next_shown (slide, 1);
					if (ns >= g_deck.slides.n) { ended = true; draw_end (); continue; }
					go_to (slide, step, ns, &steps, ft);
					slide = ns; stepT = kapi_get_ticks (); slideDoneT = 0;
					step = g_deck.slides[slide]->anim.n && g_deck.slides[slide]->anim[0].start != ST_CLICK ? 0 : -1;
					redraw = true;
				}
			}
			if (playing || redraw || presenter)
			{
				int tms = (int) (kapi_get_ticks () - stepT) * 10;
				if (presenter) { static unsigned lastP; if (!(playing || redraw) && now - lastP < 50) continue; lastP = now; console (slide, step, tms, steps, blank, blankC, next_shown (slide, 1)); }
				else compose (slide, step, tms, blank, blankC);
				present ();
				redraw = false;
			}
		}
		C.drop_all ();
		gpc_target_free (C.g, tgt);
		kapi_fullscreen_end ();
	}
	static int first_of (const Slide &s, const FxTime *ft, int step) { for (int i = 0; i < s.anim.n; i++) if (ft[i].step == step) return i; return 0; }
	// From one slide (as it ends) to the next (as it begins): its transition played.
	void go_to (int from, int step, int to, int *steps, FxTime *ft)
	{
		const Transition &tr = g_deck.slides[to]->tr;
		*steps = fx_plan (*g_deck.slides[to], ft);
		if (tr.type == TR_NONE || presenter) return;
		FxTime fo[128]; fx_plan (*g_deck.slides[from], fo);
		(void) step;
		gpc_tex *A = snapshot (from, 99, 1 << 20);
		int st0 = g_deck.slides[to]->anim.n && g_deck.slides[to]->anim[0].start != ST_CLICK ? 0 : -1;
		gpc_tex *B = snapshot (to, st0, 0);
		unsigned t0 = kapi_get_ticks ();
		int dur = imax (100, tr.dur);
		for (;;)
		{
			kapi_pump_wait (5);
			float t = (float) ((kapi_get_ticks () - t0) * 10) / dur;
			if (t > 1) t = 1;
			transition_frame (A, B, tr, t);
			present ();
			if (t >= 1 || g_shKn || g_shClick) break;
		}
		gpc_tex_destroy (C.g, A); gpc_tex_destroy (C.g, B);
	}
	void draw_end ()
	{
		for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) tgt[(long) y * tstride + x] = 0;
		Canvas cv; cv.adopt (tgt, W, H, tstride);
		const char *t = "The end of the show. A click or a key: back.";
		text (cv, (W - text_w (t, 18)) / 2, H / 2 - 12, t, 18, 0xA0A0A0);
		present ();
	}
};
void (*Show::g_now) (int *, int *, int *, int *, int *, int *);

} // namespace sl

#endif
