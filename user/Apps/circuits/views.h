//
// views.h -- Circuits' owner-drawn views (the window's, included by main.cpp after the game's state): the levels list
// (the worlds as headers, discs, padlocks, stars), the level's card (Turtle Quest's), the message bar, the truth table
// (the goal, after a Check what the board gives, the current row -- a click on a row sets the switches), the gate
// count, and the two cards over the board: the lesson of a new gate or idea, the result of a won Check.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _circuits_views_h
#define _circuits_views_h

// ---- the levels list: the packs as headers, their levels under them --------------------------------------------------
class LevelList : public Widget
{
public:
	int sc, hot;					// the scroll (px), the row under the pointer (-1)
	enum { HEAD = 30, ROW = 28 };
	LevelList (int l, int t, int w, int h) : Widget (l, t, w, h), sc (0), hot (-1) {}
	int total () { int n = 0; for (int p = 0; p < g_npacks; p++) n += HEAD + g_packs[p]->n * ROW; return n; }
	int view () { return height - 4; }
	void clamp () { int m = total () - view (); if (sc > m) sc = m; if (sc < 0) sc = 0; }
	// The row at y (the list's own coordinates, scrolled): *p, *l (-1: a header) -> its index, -1 none
	int row_at (int y, int *P, int *Lv)
	{
		int yy = -sc, idx = 0;
		for (int p = 0; p < g_npacks; p++)
		{
			if (y >= yy && y < yy + HEAD) { *P = p; *Lv = -1; return idx; }
			yy += HEAD; idx++;
			for (int l = 0; l < g_packs[p]->n; l++, yy += ROW, idx++)
				if (y >= yy && y < yy + ROW) { *P = p; *Lv = l; return idx; }
		}
		return -1;
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		Canvas clip; clip.adopt (canvas.px + 2 * canvas.stride + 2, width - 4, height - 4, canvas.stride);
		clamp ();
		bool scroll = total () > view ();
		int y = -sc, rw = clip.w - (scroll ? UK_SBW + 2 : 0), idx = 0;
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 130);
		for (int p = 0; p < g_npacks; p++, idx++)
		{
			const Pack &pk = *g_packs[p];
			int got = 0; bool any = false;
			for (int l = 0; l < pk.n; l++) { got += g_pr.stars (pk.lv[l]->id); if (level_open (g_pr, pk, l)) any = true; }
			if (y > -HEAD && y < clip.h)
			{
				clip.fillRect (0, y, rw, HEAD, uk_mix (C_FIELD, C_FIELD_TEXT, 14));
				char t[16]; snprintf (t, sizeof t, "%d/%d", got, 3 * pk.n); int tw = uk_tw (t);
				char f[90]; uk_text_fit (pk.titleOf (g_lang), rw - 10 - tw - 34, f, sizeof f, 2);
				uk_text_l (clip, 10, y, HEAD, f, any ? C_FIELD_TEXT : dim, 2);
				if (any) { star (clip, rw - tw - 22, y + HEAD / 2, 6, STAR_GOLD, true); uk_text_l (clip, rw - tw - 12, y, HEAD, t, dim); }
				else padlock (clip, rw - 18, y + HEAD / 2, dim);
				clip.fillRect (0, y + HEAD - 1, rw, 1, uk_mix (C_FIELD, C_FIELD_TEXT, 36));
			}
			y += HEAD;
			for (int l = 0; l < pk.n; l++, y += ROW, idx++)
			{
				if (y <= -ROW || y >= clip.h) continue;
				const Level &L = *pk.lv[l];
				bool open = level_open (g_pr, pk, l), sel = p == g_pack && l == g_level;
				int s = g_pr.stars (L.id);
				unsigned ink = open ? C_FIELD_TEXT : C_DIS;
				if (sel) { uk_hilite (clip, 3, y + 2, rw - 6, ROW - 4, 5); ink = uk_hilite_ink (); }
				else if (idx + 1 == hot && open) clip.fillRect (3, y + 2, rw - 6, ROW - 4, uk_mix (C_FIELD, C_ACCENT, 26));
				int cx = 20, cy = y + ROW / 2;
				if (!open) { VPath d; d.circle (V (cx), V (cy), V (10)); d.fill (clip, uk_mix (C_FIELD, C_FIELD_TEXT, 26)); padlock (clip, cx, cy, uk_mix (C_FIELD, C_FIELD_TEXT, 110)); }
				else
				{
					VPath d; d.circle (V (cx), V (cy), V (10)); d.fill (clip, sel ? 0x00FFFFFF : s ? OK_GREEN : uk_mix (C_FIELD, C_FIELD_TEXT, 70));
					char num[8]; snprintf (num, sizeof num, "%d", l + 1);
					UkFaceScope fs (g_small); uk_text_c (clip, cx - 10, cy - 10, 20, 20, num, sel ? C_ACCENT : 0x00FFFFFF, 2);
				}
				char f[90]; uk_text_fit (L.titleOf (g_lang), rw - 38 - 54, f, sizeof f, sel ? 2 : 0);
				uk_text_l (clip, 36, y, ROW, f, ink, sel ? 2 : 0);
				if (open)
					for (int k = 0; k < 3; k++) star (clip, rw - 44 + k * 14, cy, 6, k < s ? STAR_GOLD : uk_mix (sel ? C_ACCENT : C_FIELD, ink, 80), k < s);
			}
		}
		if (scroll)
		{
			UkThumb th = uk_thumb (total (), view (), sc, height - 4);
			uk_draw_vscroll (canvas, width - UK_SBW - 2, 2, UK_SBW, height - 4, th, C_FIELD);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { if (hot >= 0) { hot = -1; invalidate (true); } return false; }
		if (wheel) { sc -= wheel * ROW; clamp (); invalidate (true); return true; }
		int p = -1, l = -1, i = row_at (my - 2, &p, &l);
		if (i != hot) { hot = i; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (i >= 0 && l >= 0) level_clicked (p, l); }
		if (!bl) pressed = false;
		return true;
	}
	void showSel ()					// the current level's row in view
	{
		int y = 0;
		for (int p = 0; p < g_npacks; p++)
		{
			if (p == g_pack) { int ry = y + HEAD + g_level * ROW; if (ry < sc + (g_level == 0 ? HEAD : 0)) sc = ry - (g_level == 0 ? HEAD : 0); if (ry + ROW > sc + view ()) sc = ry + ROW - view (); break; }
			y += HEAD + g_packs[p]->n * ROW;
		}
		clamp (); invalidate (true);
	}
};

// ---- the level's card: its number and title, what to do, the hint (Turtle Quest's) --------------------------------------
class Card : public Widget
{
public:
	int btnRoom;					// the room Hint and Lesson take at its right (0: they are on the bench)
	Card (int l, int t, int w, int h) : Widget (l, t, w, h), btnRoom (180) {}
	int textW () { return width - 28 - btnRoom; }
	void hint_text (char *h, int cap) { snprintf (h, cap, "%s %s", TR ("Hint:"), g_L->hintOf (g_lang)); }
	int need ()
	{
		if (!g_L) return 66;
		int h = 7 + face_h (g_big) + 3 + wrap_count (g_L->textOf (g_lang), textW (), 4) * (uk_fh () + 2) + 8;
		if (g_showHint) { char t[600]; hint_text (t, sizeof t); h += 2 + wrap_count (t, width - 28, 3) * (uk_fh () + 2); }
		return h < 66 ? 66 : h;
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (!g_L) return;
		uk_rbox (canvas, 0, 0, width, height, 10, uk_mix (C_FIELD, 0xFFF6D8, 120), uk_mix (C_FIELD, 0xFFEFC2, 120));
		uk_rline (canvas, 0, 0, width, height, 10, 0xE0C77A);
		char t[120], f[120]; snprintf (t, sizeof t, "%d.%d  %s", g_pack + 1, g_level + 1, g_L->titleOf (g_lang));
		{ UkFaceScope fs (g_big); uk_text_fit (t, textW (), f, sizeof f, 2); uk_text (canvas, 14, 7, f, 0x3A2E10, 2); }
		int y = 7 + face_h (g_big) + 3;
		int n = wrap_draw (canvas, 14, y, textW (), g_L->textOf (g_lang), 0x3A3A3A, 4, uk_fh () + 2);
		if (g_showHint) { char h[600]; hint_text (h, sizeof h); wrap_draw (canvas, 14, y + n * (uk_fh () + 2) + 2, width - 28, h, 0x8A5A00, 3, uk_fh () + 2); }
	}
};

// ---- the message under the board (Turtle Quest's) --------------------------------------------------------------------
class MsgBar : public Widget
{
public:
	int kind, stars; char text[400];
	bool roomForNext;
	MsgBar (int l, int t, int w, int h) : Widget (l, t, w, h), kind (M_NONE), stars (0), roomForNext (false) { text[0] = 0; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (kind == M_NONE || !text[0]) return;
		unsigned face = kind == M_OK ? 0xDFF3DA : kind == M_ERR ? 0xFBE0DC : 0xE4ECF7, edge = kind == M_OK ? 0x4E9A57 : kind == M_ERR ? 0xC8463B : 0x5B7FB5;
		uk_rbox (canvas, 0, 0, width, height, 10, face, face);
		uk_rline (canvas, 0, 0, width, height, 10, edge);
		int x = 14;
		if (kind == M_OK && stars) for (int k = 0; k < 3; k++) { star (canvas, x + 10, height / 2, 10, k < stars ? STAR_GOLD : 0xC9D6C5, true); x += 23; }
		else if (kind == M_OK) { VPath d; d.circle (V (x + 10), V (height / 2), V (10)); d.fill (canvas, edge); uk_glyph (canvas, WKG_CHECK, x + 10, height / 2, 12, 0xFFFFFF); x += 26; }
		else { VPath d; d.circle (V (x + 10), V (height / 2), V (10)); d.fill (canvas, edge); uk_text_c (canvas, x, height / 2 - 10, 20, 20, kind == M_ERR ? "!" : "i", 0xFFFFFF, 2); x += 26; }
		int room = width - x - 16 - (roomForNext ? 140 : 0);
		int lh = uk_fh () + 1, n = wrap_count (text, room, 2), y = (height - n * lh) / 2;
		wrap_draw (canvas, x + 6, y, room, text, uk_mix (edge, 0, 140), 2, lh);
	}
};

// ---- the truth table: the goal, what the board gives after a Check, the current row ------------------------------------
class TruthTable : public Widget
{
public:
	int hot, rh;					// the row under the pointer; the rows' height (RH; less on a low screen)
	enum { RH = 21, Y0 = 46, MARK = 26 };
	TruthTable (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1), rh (RH) {}
	int need () { return g_L ? Y0 + g_L->rows () * rh + 8 : 60; }
	int cols () { return g_L->ninputs + g_L->noutputs * (g_checked ? 2 : 1); }
	int colW () { int cw = (width - 16 - MARK) / cols (); return cw > 44 ? 44 : cw; }
	int x0 () { return (width - (cols () * colW () + MARK)) / 2 + 4; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		if (!g_L) return;
		const Level &L = *g_L;
		int rows = L.rows (), nc = cols (), cw = colW (), tw = nc * cw + MARK, X0 = x0 ();
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 130), grid = uk_mix (C_FIELD, C_FIELD_TEXT, 30);
		int xo = X0 + L.ninputs * cw, xg = xo + L.noutputs * cw;
		{
			UkFaceScope fs (g_small);
			uk_text_c (canvas, X0, 6, L.ninputs * cw, 14, L.ninputs > 1 ? TR ("IN") : TRC ("one", "IN"), dim, 2);
			uk_text_c (canvas, xo, 6, L.noutputs * cw, 14, TR ("GOAL"), dim, 2);
			if (g_checked) uk_text_c (canvas, xg, 6, L.noutputs * cw, 14, TR ("YOURS"), dim, 2);
		}
		for (int k = 0; k < L.ninputs; k++) uk_text_c (canvas, X0 + k * cw, 22, cw, 20, L.inName[k], C_FIELD_TEXT, 2);
		for (int k = 0; k < L.noutputs; k++) uk_text_c (canvas, xo + k * cw, 22, cw, 20, L.outName[k], C_FIELD_TEXT, 2);
		if (g_checked) for (int k = 0; k < L.noutputs; k++) uk_text_c (canvas, xg + k * cw, 22, cw, 20, L.outName[k], C_FIELD_TEXT, 2);
		canvas.fillRect (X0 - 4, Y0 - 2, tw + 2, 1, uk_mix (C_FIELD, C_FIELD_TEXT, 70));
		for (int r = 0; r < rows; r++)
		{
			int y = Y0 + r * rh;
			bool wrong = g_checked && ((g_res.wrong >> r) & 1);
			if (wrong) uk_rbox (canvas, X0 - 4, y + 1, tw + 2, rh - 2, 4, 0x00FBE0DC, 0x00FBE0DC);
			else if (r == hot) canvas.fillRect (X0 - 4, y + 1, tw + 2, rh - 2, uk_mix (C_FIELD, C_ACCENT, 26));
			else if (r & 1) canvas.fillRect (X0 - 4, y + 1, tw + 2, rh - 2, uk_mix (C_FIELD, C_FIELD_TEXT, 8));
			if (r == (int) g_row) uk_rline (canvas, X0 - 4, y + 1, tw + 2, rh - 2, 4, wrong ? ERR_RED : C_ACCENT, 255);
			for (int k = 0; k < L.ninputs; k++)
			{ int b = (r >> (L.ninputs - 1 - k)) & 1; uk_text_c (canvas, X0 + k * cw, y, cw, rh, b ? "1" : "0", b ? C_FIELD_TEXT : dim); }
			for (int k = 0; k < L.noutputs; k++)
			{ int b = (L.want[k] >> r) & 1; uk_text_c (canvas, xo + k * cw, y, cw, rh, b ? "1" : "0", b ? C_FIELD_TEXT : dim, 2); }
			int mx = X0 + nc * cw + MARK / 2 - 2, my = y + rh / 2;
			if (g_checked)
			{
				for (int k = 0; k < L.noutputs; k++)
				{
					int b = (g_res.got[k] >> r) & 1, wb = (L.want[k] >> r) & 1;
					uk_text_c (canvas, xg + k * cw, y, cw, rh, b ? "1" : "0", b != wb ? ERR_RED : b ? C_FIELD_TEXT : dim, 2);
				}
				if (wrong) { VPath d; d.circle (V (mx), V (my), V (7)); d.fill (canvas, ERR_RED); uk_glyph (canvas, WKG_CLOSE, mx, my, 8, 0x00FFFFFF); }
				else uk_glyph (canvas, WKG_CHECK, mx, my, 11, OK_GREEN);
			}
			else if (r == (int) g_row) uk_glyph (canvas, WKG_LEFT, mx, my, 9, C_ACCENT);
		}
		canvas.fillRect (xo - 2, 22, 1, rows * rh + 24, grid);
		if (g_checked) canvas.fillRect (xg, 22, 1, rows * rh + 24, grid);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0 || !g_L) { if (hot >= 0) { hot = -1; invalidate (true); } return false; }
		int r = my >= Y0 ? (my - Y0) / rh : -1;
		if (r >= g_L->rows ()) r = -1;
		if (r != hot) { hot = r; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (r >= 0) set_row ((unsigned) r); }
		if (!bl) pressed = false;
		return true;
	}
};

// ---- the gate count under the table, the stars it needs ----------------------------------------------------------------
static const char *gates_word (int n) { return n == 1 ? TR ("gate") : TR ("gates"); }
class CountView : public Widget
{
public:
	CountView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (!g_L) return;
		int n = g_c.gates ();
		char t[16]; snprintf (t, sizeof t, "%d", n);
		int nw;
		{ UkFaceScope fs (g_huge); uk_text (canvas, 4, 0, t, n <= g_L->par3 ? OK_GREEN : C_TEXT, 2); nw = uk_tw (t, 2); }
		int x = 4 + 6 + nw;
		uk_text (canvas, x, 4, gates_word (n), C_TEXT, 2);
		char a[40], b[40]; snprintf (a, sizeof a, "\xE2\x89\xA4 %d", g_L->par3); snprintf (b, sizeof b, "\xE2\x89\xA4 %d", g_L->par2);
		int y = 22; unsigned dim = uk_mix (C_BG, C_TEXT, 170);
		for (int k = 0; k < 3; k++) star (canvas, x + 6 + k * 12, y + 8, 5, STAR_GOLD, true);
		uk_text (canvas, x + 42, y, a, dim);
		int x2 = x + 42 + uk_tw (a) + 14;
		for (int k = 0; k < 2; k++) star (canvas, x2 + 6 + k * 12, y + 8, 5, STAR_GOLD, true);
		uk_text (canvas, x2 + 30, y, b, dim);
		if (n)
		{
			char d[120]; snprintf (d, sizeof d, TR ("Depth %d \xC2\xB7 Step (F8) to watch it"), g_ev.maxDepth);
			char f[120]; uk_text_fit (d, width - 8, f, sizeof f); uk_text (canvas, 4, 46, f, dim);
		}
	}
};

// ---- the lesson card: a new gate (its symbol, its truth table) or a new idea (the level's table), over the board -----------
static void on_card_ok (Widget &);
static void on_result_stay (Widget &);
static void on_result_next (Widget &);
class LessonCard : public Widget
{
public:
	const Lesson *ls;
	Button *ok;
	LessonCard (int l, int t, int w, int h) : Widget (l, t, w, h), ls (0)
	{ ok = new Button (w - 120, h - 46, 104, 30, "OK", on_card_ok); addChild (ok); }
	void place () { ok->left = width - 120; ok->top = height - 46; }
	// a small truth table in a lilac box at (tx, ty): names, rows of bits; the outputs' 1 in green
	int table (int tx, int ty, int ni, int no, const char *const *names, const unsigned *want)
	{
		int rows = 1 << ni, nc = ni + no, cw = nc > 4 ? 28 : 34, RH = rows > 8 ? 15 : rows > 4 ? 18 : 21;
		uk_rbox (canvas, tx - 8, ty - 4, nc * cw + 16, (rows + 1) * RH + 12, 6, 0x00F1ECFA, 0x00F1ECFA);
		for (int k = 0; k < nc; k++) uk_text_c (canvas, tx + k * cw, ty, cw, RH, names[k], 0x004B2E83, 2);
		canvas.fillRect (tx, ty + RH, nc * cw, 1, 0x00C9B8E6);
		for (int r = 0; r < rows; r++)
			for (int k = 0; k < nc; k++)
			{
				int b = k < ni ? (r >> (ni - 1 - k)) & 1 : (want[k - ni] >> r) & 1;
				uk_text_c (canvas, tx + k * cw, ty + RH + 2 + r * RH, cw, RH, b ? "1" : "0", k >= ni && b ? WIRE_1 : 0x00303030, k >= ni ? 2 : 0);
			}
		return nc * cw;
	}
	void onDraw () override
	{
		canvas.clear (BOARD_BG);			// (its rounded corners over the board)
		if (!ls) return;
		uk_rbox (canvas, 0, 0, width, height, 14, 0xFFFFFF, 0xF6F4FB);
		uk_rline (canvas, 0, 0, width, height, 14, 0x8E6CC8);
		uk_rbox (canvas, 0, 0, width, 44, 14, 0x8E6CC8, 0x7A58B8, 255, UK_TL | UK_TR);
		char t[160];
		if (ls->gate >= 0) snprintf (t, sizeof t, "%s  %s", TR ("New gate:"), gate_word (ls->gate));
		else snprintf (t, sizeof t, "%s  %s", TR ("New idea:"), ls->title[g_lang]);
		{ UkFaceScope fs (g_big); char f[160]; uk_text_fit (t, width - 36, f, sizeof f, 2); uk_text_l (canvas, 18, 0, 44, f, 0xFFFFFF, 2); }
		int textY = 196, textX = 20, textW = width - 40;
		if (ls->gate >= 0)			// the symbol, big, with its pins named; its table beside it
		{
			int L = 46, T = 70, sc = 18, g = ls->gate;
			int l = (L + sc) * 16, r = (L + sc * 41 / 10) * 16, cy = (T + 2 * sc) * 16, h2 = sc * 16 * 145 / 100;
			VPath s1;
			if (g == P_NOT) s1.line (V (L - 8), cy, V (L + sc + 4), cy, V (2) + 8);
			else { s1.line (V (L - 8), V (T + sc), V (L + sc + 4), V (T + sc), V (2) + 8); s1.line (V (L - 8), V (T + 3 * sc), V (L + sc + 4), V (T + 3 * sc), V (2) + 8); }
			s1.line (r - V (2), cy, V (L + sc * 5 + 10), cy, V (2) + 8); s1.fill (canvas, WIRE_0);
			draw_gate_shape (canvas, g, l, cy - h2, r, cy + h2, sc * 16 * 3 / 10, GATE_FACE, GATE_INK, V (2));
			if (g == P_NOT) uk_text_l (canvas, L - 24, cy / 16 - 10, 20, "A", C_TEXT, 2);
			else { uk_text_l (canvas, L - 24, T + sc - 10, 20, "A", C_TEXT, 2); uk_text_l (canvas, L - 24, T + 3 * sc - 10, 20, "B", C_TEXT, 2); }
			uk_text_l (canvas, L + sc * 5 + 16, cy / 16 - 10, 30, "Out", C_TEXT, 2);
			{ UkFaceScope fs (g_small); uk_text_c (canvas, L + sc + 2, cy / 16 - 8, sc * 2, 16, gate_word (g), GATE_INK, 2); }
			int ni = g == P_NOT ? 1 : 2; unsigned want = 0;
			for (int rr = 0; rr < (1 << ni); rr++)
			{
				int a = ni == 2 ? rr >> 1 : rr, b = rr & 1, o = 0;
				switch (g) { case P_NOT: o = !a; break; case P_AND: o = a & b; break; case P_OR: o = a | b; break;
					     case P_XOR: o = a ^ b; break; case P_NAND: o = !(a & b); break; case P_NOR: o = !(a | b); break; }
				want |= (unsigned) o << rr;
			}
			const char *nm1[2] = { "A", "Out" }, *nm2[3] = { "A", "B", "Out" };
			table (236, 60, ni, 1, ni == 1 ? nm1 : nm2, &want);
		}
		else if (g_L)				// an idea: the level's own table at the left, the text beside it
		{
			const char *names[2 * MAXIO];
			for (int k = 0; k < g_L->ninputs; k++) names[k] = g_L->inName[k];
			for (int k = 0; k < g_L->noutputs; k++) names[g_L->ninputs + k] = g_L->outName[k];
			int w = table (30, 62, g_L->ninputs, g_L->noutputs, names, g_L->want);
			textX = 30 + w + 30; textY = 60; textW = width - textX - 20;
		}
		wrap_draw (canvas, textX, textY, textW, ls->text[g_lang], 0x00303030, ls->gate >= 0 ? 6 : 12, uk_fh () + 3);
	}
	bool onMouse (int, int, int, int, int, int) override { return true; }	// (the board under it is not clicked)
	unsigned bgColor () override { return 0x00F6F4FB; }			// (what its buttons' corners blend into)
};

// ---- the result card of a won Check ----------------------------------------------------------------------------------------
class ResultCard : public Widget
{
public:
	int stars, gates; bool record, last;
	Button *stay, *next;
	ResultCard (int l, int t, int w, int h) : Widget (l, t, w, h), stars (0), gates (0), record (false), last (false)
	{
		stay = new Button (18, h - 46, 170, 30, "", on_result_stay); addChild (stay);
		next = new Button (w - 158, h - 46, 140, 30, "", on_result_next); addChild (next);
	}
	void place () { stay->left = 18; stay->top = height - 46; next->left = width - 158; next->top = height - 46; }
	void onDraw () override
	{
		canvas.clear (BOARD_BG);
		if (!g_L) return;
		uk_rbox (canvas, 0, 0, width, height, 14, 0xFFFFFF, 0xF3FAF3);
		uk_rline (canvas, 0, 0, width, height, 14, 0x4E9A57);
		uk_rbox (canvas, 0, 0, width, 44, 14, 0x55A85E, 0x3E8E48, 255, UK_TL | UK_TR);
		{ UkFaceScope fs (g_big); uk_text_l (canvas, 18, 0, 44, TR ("Level complete!"), 0xFFFFFF, 2); }
		int cx = width / 2;
		for (int k = 0; k < 3; k++) star (canvas, cx - 52 + k * 52, 86, k == 1 ? 26 : 22, k < stars ? STAR_GOLD : 0x00D5DDD3, true);
		int y = 120;
		if (record) { uk_text_c (canvas, 0, 114, width, 16, TR ("New record!"), 0x00B07A00, 2); y = 132; }
		char t[160]; snprintf (t, sizeof t, TR ("%d %s \xE2\x80\x94 every row right"), gates, gates_word (gates));
		{ UkFaceScope fs (g_big); uk_text_c (canvas, 0, y, width, 26, t, 0x00264F2C, 2); }
		char s[300];
		if (last) snprintf (s, sizeof s, "%s", TR ("You finished every level!"));
		else if (stars < 3) snprintf (s, sizeof s, TR ("Three stars need %d %s or fewer. The best known circuit is smaller: can you find it?"), g_L->par3, gates_word (g_L->par3));
		else snprintf (s, sizeof s, TR ("The best possible: %d %s."), gates, gates_word (gates));
		wrap_draw (canvas, 25, y + 32, width - 50, s, 0x00405040, 3, 19, 0, true);
	}
	bool onMouse (int, int, int, int, int, int) override { return true; }
	unsigned bgColor () override { return 0x00F3FAF3; }
};

#endif
