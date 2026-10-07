//
// bar.h -- Critters' skill bar and status line (the window's, included by main.cpp only; 04-ux-design.md §2.1, D4-D8,
// D13): the slots (a role: its key, its count, its picture; or Pause, Fast forward, All explode), the bar's face with
// its etched separators, the status line (the chosen role and its count, what is under the pointer, Out, Saved, Time;
// "only blockers are left" in amber) and the minimap (the whole level, the creatures as dots, the visible frame).
// Each is repainted only when what it shows changed.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _critters_bar_h
#define _critters_bar_h

enum { K_PAUSE = 8, K_FAST, K_NUKE, NSLOT };

// The face behind the bar's widgets: the etched lines between its groups
class BarFace : public Widget
{
public:
	BarFace (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_etch_v (canvas, 362, 8, height - 16, C_BG); uk_etch_v (canvas, 460, 8, height - 16, C_BG); uk_etch_v (canvas, 602, 8, height - 16, C_BG);
	}
};

// A slot of the skill bar (D4, D6)
class SkillSlot : public Widget
{
public:
	int kind, count; bool sel, lit, pulse, absent, in;
	SkillSlot (int l, int t, int w, int h, int k) : Widget (l, t, w, h), kind (k), count (0), sel (false), lit (false), pulse (false), absent (k == critters::R_BASHER || k == critters::R_MINER), in (false) {}
	void set (int c, bool s, bool l, bool p)
	{
		if (c == count && s == sel && l == lit && p == pulse) return;
		count = c; sel = s; lit = l; pulse = p; invalidate (true);
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		bool role = kind < 8, off = role && (count == 0 || absent);
		if (pulse) uk_rbox (canvas, 0, 0, width, height, 8, uk_tone (0xFF9A30, 170), 0xFF9A30);
		if (sel || lit) { uk_raised (canvas, 2, 2, width - 4, height - 4, 6, uk_tone (C_ACCENT, 150), UK_PRESSED); uk_rline (canvas, 1, 1, width - 2, height - 2, 7, C_ACCENT); uk_rline (canvas, 2, 2, width - 4, height - 4, 6, C_ACCENT); }
		else uk_raised (canvas, 2, 2, width - 4, height - 4, 6, off ? uk_tone (C_BG, 120) : uk_tone (C_BG, 150), in && !off ? UK_HOT : UK_NORMAL);
		unsigned ink = sel || lit ? uk_ink_for (uk_tone (C_ACCENT, 150)) : C_TEXT;
		const char *key = kind == K_PAUSE ? "P" : kind == K_FAST ? "F" : "N";
		char kb[4]; if (role) { snprintf (kb, sizeof kb, "%d", kind + 1); key = kb; }
		{ UkFaceScope sc (face (10)); uk_text (canvas, 7, 5, key, sel || lit ? ink : off ? uk_mix (C_BG, C_DIS, 160) : C_DIS, 2); }
		if (role)
		{
			char b[8]; if (absent) snprintf (b, sizeof b, "\xE2\x80\x93"); else snprintf (b, sizeof b, "%d", count);
			{ UkFaceScope sc (face (17)); uk_text_c (canvas, 0, 6, width, 24, b, off ? uk_mix (C_BG, C_DIS, 170) : ink, 2); }
			role_icon (canvas, kind, width / 2, 62, 34, off);
		}
		else if (kind == K_NUKE) nuke_icon (canvas, width / 2, 50, 26, false);
		else uk_tool_glyph (canvas, kind == K_PAUSE ? WKT_PAUSE : WKT_FORWARD, width / 2 - 11, 39, 22, ink);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { if (in) { in = false; invalidate (true); slot_hover (-1); } pressed = false; return false; }
		if (!in) { in = true; invalidate (true); slot_hover (kind); }
		if (wheel) { slot_wheel (wheel); return true; }
		if (bl && !pressed) { pressed = true; slot_action (kind); }
		else if (!bl) pressed = false;
		(void) my;
		return true;
	}
};

// The status line (D7, D13): left the chosen role and what is under the pointer (or a slot's name, or the amber
// message), right Out, Saved, Time
struct StatusInfo
{
	int role, left, out, saved, needed, timeLeft, nunder; bool underRed, onlyBlockers;
	char under[120];
};
class StatusLine : public Widget
{
public:
	StatusInfo s;
	StatusLine (int l, int t, int w, int h) : Widget (l, t, w, h) { memset (&s, 0, sizeof s); s.role = -1; }
	void set (const StatusInfo &n) { if (memcmp (&n, &s, sizeof s)) { s = n; invalidate (true); } }
	int seg (int xr, const char *cap, const char *val, unsigned ink)	// right-aligned "Caption value" -> its left
	{
		int vw = uk_text_w (val, 2), cw = uk_text_w (cap);
		uk_text_l (canvas, xr - vw, 0, height, val, ink, 2); uk_text_l (canvas, xr - vw - 6 - cw, 0, height, cap, C_DIS);
		return xr - vw - 6 - cw;
	}
	void onDraw () override
	{
		unsigned bg = uk_tone (C_BG, 120);
		canvas.clear (bg);
		uk_etch_h (canvas, 0, height - 2, width, bg);
		char b[64], t[16];
		mmss ((s.timeLeft + critters::STEPS_PER_SEC - 1) / critters::STEPS_PER_SEC, t, sizeof t);
		int x = seg (width - 12, TR ("Time"), t, s.timeLeft < 30 * critters::STEPS_PER_SEC ? 0xD03A2A : C_TEXT);
		uk_etch_v (canvas, x - 14, 5, height - 10, bg);
		snprintf (b, sizeof b, "%d / %d", s.saved, s.needed);
		x = seg (x - 28, TR ("Saved"), b, s.saved >= s.needed ? GREEN : C_TEXT);
		uk_etch_v (canvas, x - 14, 5, height - 10, bg);
		snprintf (b, sizeof b, "%d", s.out); x = seg (x - 28, TR ("Out"), b, s.onlyBlockers ? 0xC07800 : C_TEXT);
		int lx = 10, room = x - 24;
		char fit[200];
		if (s.onlyBlockers)
		{
			nuke_icon (canvas, lx + 8, height / 2, 16, false);
			uk_text_fit (TR ("Only blockers are left: N (All explode) ends the level."), room - lx - 22, fit, sizeof fit, 2);
			uk_text_l (canvas, lx + 22, 0, height, fit, AMBER, 2);
			return;
		}
		if (s.role >= 0)
		{
			const char *nm = TR (ROLE_NAME[s.role]);
			role_icon (canvas, s.role, lx + 9, height / 2, 22, false);
			uk_text_l (canvas, lx + 24, 0, height, nm, C_TEXT, 2); lx += 24 + uk_text_w (nm, 2) + 6;
			snprintf (b, sizeof b, TR ("%d left"), s.left); uk_text_l (canvas, lx, 0, height, b, C_DIS); lx += uk_text_w (b) + 14;
		}
		if (s.under[0])
		{
			if (s.role >= 0) { uk_etch_v (canvas, lx - 6, 5, height - 10, bg); lx += 6; }
			uk_glyph (canvas, WKG_RING, lx + 4, height / 2, 10, s.underRed ? RED : C_DIS); lx += 14;
			uk_text_fit (s.under, room - lx, fit, sizeof fit);
			uk_text_l (canvas, lx, 0, height, fit, s.underRed ? RED : C_TEXT); lx += uk_text_w (fit) + 6;
			if (s.nunder > 1 && lx + 50 < room) { snprintf (b, sizeof b, TR ("(%d here)"), s.nunder); uk_text_l (canvas, lx, 0, height, b, C_DIS); }
		}
	}
};

// The minimap (D8): the whole level scaled (sx = 174 / width, sy = min (84 / height, 2 sx)), each sample the colour of
// its block's first non-empty pixel; the hatches, the exits, the creatures as dots, the visible frame. The picture of
// the terrain is made again only when the terrain changed (at most every 10 steps: main.cpp); click / drag: scroll.
class MiniMap : public Widget
{
public:
	Canvas img; bool stale; double sx, sy; int x0, y0, mw, mh;
	MiniMap (int l, int t, int w, int h) : Widget (l, t, w, h), stale (true), sx (1), sy (1), x0 (0), y0 (0), mw (0), mh (0) {}
	void rebuild (const critters::Terrain &t)
	{
		int iw = width - 8, ih = height - 8;
		sx = (double) iw / t.w; sy = fmin ((double) ih / t.h, 2 * sx); if (sx > 1) sx = 1; if (sy > 1) sy = 1;
		mw = (int) (t.w * sx); mh = (int) (t.h * sy); x0 = 4 + (iw - mw) / 2; y0 = 4 + (ih - mh) / 2;
		if (mw < 1 || mh < 1 || !img.alloc (mw, mh)) return;
		int bw = (int) (1 / sx) + 1, bh = (int) (1 / sy) + 1;
		unsigned sky = uk_mix (t.bg, 0, 60);
		for (int y = 0; y < mh; y++) for (int x = 0; x < mw; x++)
		{
			int lx = (int) (x / sx), ly = (int) (y / sy), best = -1;
			for (int dy = 0; dy < bh && best < 0; dy++) for (int dx = 0; dx < bw; dx++)
			{ int X = lx + dx, Y = ly + dy; if (X < t.w && Y < t.h && t.m[Y * t.w + X]) { best = Y * t.w + X; break; } }
			img.px[y * img.stride + x] = best >= 0 ? t.col[best] : sky;
		}
		stale = false;
	}
	void onDraw () override;			// (main.cpp: it reads the world and the view)
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { pressed = false; return false; }
		if (bl) { pressed = true; if (sx > 0) minimap_scroll ((int) ((mx - x0) / sx)); }
		else pressed = false;
		(void) my;
		return true;
	}
};

#endif
