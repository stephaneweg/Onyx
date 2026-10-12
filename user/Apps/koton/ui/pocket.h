//
// ui/pocket.h -- Koton in the POCKET mode on a small screen (640 x 480, a D-pad, no pointer: the Waveshare
// PocketTerm35; docs/compact-shell/mockups/pocketterm/apps/ANSWERS.md, Q4): it PLAYS a project and MIXES it.
// Composing is not offered there. One widget over the whole window (the desktop's parts stay made, hidden: every
// command of main.cpp works as it is), two views under L / R:
//
//   Song    the tracks as rows of 48 px -- the name, mute and solo, their blocks, the playhead;
//           Up / Down the track, Left / Right the playhead bar by bar, A mute, S solo;
//   Mixer   a strip a track and the master's -- the level, the meters;
//           Left / Right the track, Up / Down its level (Shift: fine), A mute, S solo;
//   both    Space play / pause, Home to the start, B (Esc) stops; the transport is in the bar, always.
//
// Everything else (the piano roll, the sound chain and the effects, the generators, moving blocks) answers with the
// "not in pocket" card: why, what can be done here, Open FM Tracker beside Back to the song, and where the rest is.
// The desktop never makes this widget (main.cpp: g_pk).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _koton_pocket_h
#define _koton_pocket_h

#include "chrome.h"
#include "uikit/lang.h"
#include "uikit/padhint.h"

namespace kui {

class PocketKoton : public Widget
{
public:
	enum { BAR = 40, TABS = 40, ROW = 48, LX = 150, COL = 53, STRIP = 80, GAP = 8 };
	int view;				// 0 the song, 1 the mixer
	bool card; int cardSel;			// the "not in pocket" card; its chosen button (0 Open FM Tracker, 1 Back to the song)
	int mixSel;				// the mixer's chosen strip (an index of strips (); the last: the master)
	int rowTop, stripFirst;			// the first track row shown, the first strip shown
	SegmentedControl *seg;
	float lv[ENGINE_MAX_TRACKS + 1][2];	// the meters shown (decaying)

	PocketKoton (int w, int h) : Widget (0, 0, w, h), view (0), card (false), cardSel (1), mixSel (0), rowTop (0), stripFirst (0), seg (0)
	{
		canFocus = true;
		for (int i = 0; i <= ENGINE_MAX_TRACKS; i++) lv[i][0] = lv[i][1] = 0;
		static const char *L2[2];
		L2[0] = TR ("Song"); L2[1] = TR ("Mixer");
		seg = new SegmentedControl (44, BAR + 5, 192, 30, L2, 2, 0, [] (Widget &w) {
			PocketKoton *p = (PocketKoton *) w.parent;
			p->view = ((SegmentedControl &) w).selected; p->card = false; p->setFocus (); p->invalidate (true);
		});
		addChild (seg);
	}
	unsigned bgColor () override { return c0 (); }

	// ---- the colours: Onyx Noir's tokens in the flat look, the studio's shades in another theme ----
	static unsigned c0 () { return uk_flat () ? (unsigned) UK_N_BG0 : BG; }
	static unsigned c1 () { return uk_flat () ? (unsigned) UK_N_BG1 : PANEL; }
	static unsigned c2 () { return uk_flat () ? (unsigned) UK_N_BG2 : PANEL2; }
	static unsigned c3 () { return uk_flat () ? (unsigned) UK_N_BG3 : FACE; }
	static unsigned cl () { return uk_flat () ? (unsigned) UK_N_LINE : LINE; }
	static unsigned t1 () { return uk_flat () ? (unsigned) UK_N_TEXT1 : TEXT; }
	static unsigned t2 () { return uk_flat () ? (unsigned) UK_N_TEXT2 : DIM; }
	static unsigned t3 () { return uk_flat () ? (unsigned) UK_N_TEXT3 : FAINT; }
	static unsigned ca () { return uk_flat () ? (unsigned) UK_N_ACCENT : ACC; }
	static unsigned colOf (int t) { return g_doc.p.tracks[t].type == TRACK_CHORD ? FUNC_T : trackColour (t); }

	// ---- the song's figures ----
	int tracks () const { return g_doc.p.tracks.size (); }
	enum { MAXT = 255 };
	int strips (int *map) const			// the tracks that sound (the chord track is silent), then the master (-1): map[MAXT + 1]
	{
		int n = 0;
		for (int t = 0; t < tracks () && n < MAXT; t++) if (g_doc.p.tracks[t].type != TRACK_CHORD) map[n++] = t;
		map[n] = -1;
		return n + 1;
	}
	int selTrack () const { return iclamp (g_doc.selTrack, 0, imax (0, tracks () - 1)); }
	double beatNow () const { return g_audio.isPlaying () ? g_audio.playheadBeat () : (g_arrange ? g_arrange->cursorBeat : 0); }
	bool anySolo () const { for (int t = 0; t < tracks (); t++) if (g_doc.p.tracks[t].solo) return true; return false; }
	bool silent (int t) const { const Track &tr = g_doc.p.tracks[t]; return tr.mute || (anySolo () && !tr.solo); }

	void mixTouched () { g_doc.dirty = true; if (g_onEdited) g_onEdited (); invalidate (true); }
	void toggle (int t, bool solo)
	{
		if (t < 0 || t >= tracks () || g_doc.p.tracks[t].type == TRACK_CHORD) return;
		Track &tr = g_doc.p.tracks[t];
		if (solo) tr.solo = !tr.solo; else tr.mute = !tr.mute;
		snprintf (g_status, sizeof g_status, "%s", "");
		mixTouched ();
	}
	void showCard () { card = true; cardSel = 1; setFocus (); invalidate (true); }

	// ---- drawing ----
	static void chip (Canvas &cv, int x, int y, const char *lab, bool on, unsigned onCol)
	{
		box (cv, x, y, 20, 22, 5, on ? onCol : c3 ());
		textC (cv, x, y, 20, 22, lab, on ? 0x141416 : t2 (), 2);
	}
	static void dbText (double v, char *b, int cap)
	{
		if (v <= 0.0001) { snprintf (b, cap, "-inf dB"); return; }
		snprintf (b, cap, "%+.1f dB", 20 * log10 (v));
	}
	void drawBar (Canvas &cv)
	{
		const Project &p = g_doc.p;
		cv.fillRect (0, 0, width, BAR, c0 ());
		hline (cv, 0, width, BAR - 1, cl ());
		// the transport, from the right: the position, stop, play, to the start
		char pos[32], tim[32]; positionText (beatNow (), pos, sizeof pos, tim, sizeof tim);
		int xr = width - 14;
		textR (cv, xr, 0, BAR, pos, t1 (), 2);
		xr -= 86;
		static const int IC[3] = { TopBar::B_STOP, TopBar::B_PLAY, TopBar::B_REW };
		for (int k = 0; k < 3; k++)
		{
			bool on = IC[k] == TopBar::B_PLAY && g_audio.isPlaying ();
			box (cv, xr - 34, 5, 34, 30, 8, on ? ca () : c2 ());
			TopBar::icon (cv, IC[k], xr - 34, 5 - 2, 34, on ? 0xFFFFFF : t1 ());
			xr -= 40;
		}
		// the song's name, its tempo and meter
		char name[96]; const char *f = g_doc.path[0] ? g_doc.path : TR ("Untitled song");
		const char *s = strrchr (f, '/'); if (s) f = s + 1;
		snprintf (name, sizeof name, "%s", f);
		char *dot = strrchr (name, '.'); if (dot && g_doc.path[0]) *dot = 0;
		char sub[48]; snprintf (sub, sizeof sub, "%d bpm   %d / %d", iround (p.mainBpm ()), p.timeSigNum, p.timeSigDen);
		int sw = tw (sub), room = xr - 14 - 12 - sw;
		int nw = imin (tw (name, 2), imax (40, room));
		textFit (cv, 14, 0, nw, BAR, name, t1 (), 2);
		textL (cv, 14 + nw + 12, 0, BAR, sub, t2 ());
	}
	void drawTabs (Canvas &cv)
	{
		int y = BAR;
		cv.fillRect (0, y, width, TABS, c0 ());
		uk_pad_glyph (cv, 14, y + 9, 22, 'L');
		uk_pad_glyph (cv, seg->left + seg->width + 8, y + 9, 22, 'R');
		const Project &p = g_doc.p;
		int ms = (int) (AudioHost::beatToSeconds (p, p.totalBeats ()) * 1000);
		char b[64]; snprintf (b, sizeof b, tracks () == 1 ? TR ("%d track") : TR ("%d tracks"), tracks ());
		char c[96]; snprintf (c, sizeof c, "%s   %d : %02d", b, ms / 60000, (ms / 1000) % 60);
		textR (cv, width - 14, y, TABS, c, t2 ());
		if (!g_audio.running)						// (no SoundFont, or the output is another app's: said here)
			textR (cv, width - 14 - tw (c) - 14, y, TABS, TR ("No sound"), uk_flat () ? (unsigned) UK_N_WARNING : PLAY, 2);
	}
	void drawSong (Canvas &cv)
	{
		const Project &p = g_doc.p;
		int y0 = BAR + TABS + 2, bb = imax (1, p.barBeats ());
		int rows = imax (1, (height - (y0 + 18)) / ROW), n = tracks (), st = selTrack ();
		if (st < rowTop) rowTop = st;
		if (st >= rowTop + rows) rowTop = st - rows + 1;
		rowTop = iclamp (rowTop, 0, imax (0, n - rows));
		int cols = imax (1, (width - LX - 14) / COL);			// a column: four bars
		double beat = beatNow ();
		int bar = (int) (beat / bb), page = cols * 4, first = bar / page * page;
		double ppb = (double) COL / (4 * bb);
		for (int b = 0; b <= cols; b++)
		{
			int x = LX + b * COL;
			if (b < cols) { char t[12]; snprintf (t, sizeof t, "%d", first + b * 4 + 1); textL (cv, x + 3, y0, 16, t, t3 ()); }
			vline (cv, x, y0 + 16, y0 + 18 + imin (rows, n - rowTop) * ROW, cl ());
		}
		for (int k = 0; k < rows && rowTop + k < n; k++)
		{
			int t = rowTop + k, y = y0 + 18 + k * ROW;
			const Track &tr = p.tracks[t];
			bool foc = t == st;
			unsigned col = colOf (t);
			if (foc) { box (cv, 8, y, width - 16, ROW - 4, 8, c2 ()); frame (cv, 8, y, width - 16, ROW - 4, 8, ca ()); frame (cv, 9, y + 1, width - 18, ROW - 6, 7, ca ()); }
			cv.fillRect (16, y + 8, 4, ROW - 20, col);
			textFit (cv, 28, y, 62, ROW - 4, tr.name.c (), t1 (), foc ? 2 : 0);
			if (tr.type != TRACK_CHORD) { chip (cv, 96, y + 11, "M", tr.mute, 0xF1B33B); chip (cv, 120, y + 11, "S", tr.solo, 0x50B4DC); }
			double c = 0;
			for (int i = 0; i < tr.items.size (); i++)
			{
				double s = c + tr.items[i].silenceBefore, len = p.itemLength (tr.items[i]);
				c = s + len;
				int x0 = LX + (int) ((s - first * bb) * ppb + 0.5), x1 = LX + (int) ((s + len - first * bb) * ppb + 0.5);
				if (x1 <= LX || x0 >= LX + cols * COL) continue;
				x0 = imax (x0, LX); x1 = imin (x1, LX + cols * COL);
				if (x1 - x0 > 5) box (cv, x0 + 2, y + 8, x1 - x0 - 4, ROW - 20, 5, darker (col, silent (t) ? 178 : 96));
				else cv.fillRect (x0 + 1, y + 8, imax (1, x1 - x0 - 1), ROW - 20, darker (col, silent (t) ? 178 : 96));
			}
		}
		if (!n) textC (cv, 0, y0 + 40, width, 30, TR ("No track in this song."), t2 ());
		int px = LX + (int) ((beat - first * bb) * ppb + 0.5);
		if (px >= LX && px < width - 8) cv.fillRect (px, y0 + 12, 2, imin (rows, imax (1, n - rowTop)) * ROW + 6, t1 ());
		if (n > rows)							// more tracks than rows: where one is
		{
			int ty = y0 + 18, hh = rows * ROW - 4, kh = hh * rows / n, ky = ty + (hh - kh) * rowTop / (n - rows);
			box (cv, width - 6, ky, 3, kh, 1, c3 ());
		}
	}
	static int meterPx (float v, int fh)
	{
		float db = v > 1e-5f ? 20.0f * log10f (v) : -90;
		return iclamp ((int) ((db + 48) / 48 * fh), 0, fh);
	}
	void drawMixer (Canvas &cv)
	{
		int map[MAXT + 1], n = strips (map);
		int y0 = BAR + TABS + 12, sh = imin (318, height - y0 - 6);
		int fit = imax (1, (width - 14) / (STRIP + GAP));
		mixSel = iclamp (mixSel, 0, n - 1);
		if (mixSel < stripFirst) stripFirst = mixSel;
		if (mixSel >= stripFirst + fit) stripFirst = mixSel - fit + 1;
		stripFirst = iclamp (stripFirst, 0, imax (0, n - fit));
		int x00 = n <= fit ? (width - (n * (STRIP + GAP) - GAP)) / 2 : 14;
		for (int k = 0; k < fit && stripFirst + k < n; k++)
		{
			int i = stripFirst + k, t = map[i], x = x00 + k * (STRIP + GAP);
			bool foc = i == mixSel, master = t < 0;
			unsigned col = master ? t1 () : colOf (t);
			box (cv, x, y0, STRIP, sh, 10, foc ? c2 () : c1 ());
			if (foc) { frame (cv, x, y0, STRIP, sh, 10, ca ()); frame (cv, x + 1, y0 + 1, STRIP - 2, sh - 2, 9, ca ()); }
			cv.fillRect (x + 10, y0 + 10, STRIP - 20, 3, col);
			const char *nm = master ? TR ("Master") : g_doc.p.tracks[t].name.c ();
			int nw = imin (tw (nm, foc ? 2 : 0), STRIP - 8);
			textFit (cv, x + (STRIP - nw) / 2, y0 + 18, STRIP - 8, 22, nm, t1 (), foc ? 2 : 0);
			double v = master ? (double) g_audio.engine.masterGain : g_doc.p.tracks[t].volume;
			int fx = x + 24, fy = y0 + 52, fh = sh - 122;
			if (fh < 40) fh = 40;
			box (cv, fx, fy, 8, fh, 4, c3 ());
			int lvp = (int) (dmin (1.5, dmax (0, v)) / 1.5 * fh);
			box (cv, fx, fy + fh - lvp, 8, lvp, 4, foc ? ca () : t3 ());
			box (cv, fx - 8, fy + fh - lvp - 6, 24, 12, 4, t1 ());
			for (int m = 0; m < 2; m++)
			{
				int mh = meterPx (lv[master ? ENGINE_MAX_TRACKS : imin (t, ENGINE_MAX_TRACKS - 1)][m], fh);
				cv.fillRect (x + 50 + m * 7, fy, 5, fh, c3 ());
				cv.fillRect (x + 50 + m * 7, fy + fh - mh, 5, mh, uk_flat () ? (unsigned) UK_N_SUCCESS : GREEN);
			}
			char b[24]; dbText (v, b, sizeof b);
			textC (cv, x, fy + fh + 6, STRIP, 18, b, t2 ());
			if (!master) { chip (cv, x + 16, y0 + sh - 34, "M", g_doc.p.tracks[t].mute, 0xF1B33B); chip (cv, x + 42, y0 + sh - 34, "S", g_doc.p.tracks[t].solo, 0x50B4DC); }
		}
		if (n > fit)							// more strips than fit: where one is
		{
			int tx = 14, ww = width - 28, kw = ww * fit / n, kx = tx + (ww - kw) * stripFirst / (n - fit);
			box (cv, kx, height - 5, kw, 3, 1, c3 ());
		}
	}
	// the card's place and its two buttons
	void cardRect (int *x, int *y, int *w, int *h) const { *w = imin (476, width - 28); *h = 240; *x = (width - *w) / 2; *y = BAR + TABS + 26; if (*y + *h > height - 4) *y = imax (BAR, height - 4 - *h); }
	void cardButtons (int bx[2], int bw[2], int *by) const
	{
		int x, y, w, h; cardRect (&x, &y, &w, &h);
		bw[1] = tw (TR ("Back to the song"), 2) + 24 + 30; bw[0] = tw (TR ("Open FM Tracker")) + 24;
		bx[1] = x + w - 22 - bw[1]; bx[0] = bx[1] - 8 - bw[0]; *by = y + h - 100;
	}
	void drawCard (Canvas &cv)
	{
		blendRect (cv, 0, BAR + TABS, width, height - BAR - TABS, c0 (), 150);		// what is behind, dimmed
		int x, y, w, h; cardRect (&x, &y, &w, &h);
		box (cv, x, y, w, h, 14, c1 ()); frame (cv, x, y, w, h, 14, cl ());
		disc (cv, x + 38, y + 36, 15, c3 ());
		textC (cv, x + 23, y + 21, 30, 30, "i", ca (), 2);
		{ UkFaceScope sc (g_cardFace); textL (cv, x + 68, y + 20, 32, TR ("Writing notes needs a pointer"), t1 (), 2); }
		wrapText (cv, x + 22, y + 68, w - 44, TR ("In pocket, Koton plays a project and mixes it: levels, mute, solo, the tempo, the export to WAV. The piano roll, the sound chains and the generators are drawn and dragged with a mouse."), t2 (), 21);
		int bx[2], bw[2], by; cardButtons (bx, bw, &by);
		for (int i = 0; i < 2; i++)
		{
			bool sel = i == cardSel;
			box (cv, bx[i], by, bw[i], 34, 8, i == 1 ? ca () : c3 ());
			if (sel) { frame (cv, bx[i] - 3, by - 3, bw[i] + 6, 40, 10, uk_flat () ? (unsigned) UK_N_ACCENT_HI : ACC2); frame (cv, bx[i] - 2, by - 2, bw[i] + 4, 38, 9, uk_flat () ? (unsigned) UK_N_ACCENT_HI : ACC2); }
			if (i == 0) textC (cv, bx[0], by, bw[0], 34, TR ("Open FM Tracker"), t1 ());
			else { textL (cv, bx[1] + 12, by, 34, TR ("Back to the song"), 0xFFFFFF, 2); uk_pad_glyph (cv, bx[1] + bw[1] - 30, by + 7, 20, 'B'); }
		}
		wrapText (cv, x + 22, y + h - 54, w - 44, TR ("All of Koton: the desktop mode, with a mouse (Control Panel > Mode)."), t3 (), 19);
	}
	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (c0 ());
		drawBar (cv); drawTabs (cv);
		if (view == 0) drawSong (cv); else drawMixer (cv);
		if (card) drawCard (cv);
	}

	// ---- each turn: the meters fall, the playhead moves ----
	void tick ()
	{
		bool moved = false;
		for (int i = 0; i <= ENGINE_MAX_TRACKS; i++)
			for (int m = 0; m < 2; m++)
			{
				float in = i == ENGINE_MAX_TRACKS ? (m ? g_audio.engine.masterPeakR : g_audio.engine.masterPeakL) : (m ? g_audio.engine.peakR[i] : g_audio.engine.peakL[i]);
				if (!g_audio.isPlaying ()) in = 0;
				float v = lv[i][m] * 0.86f; if (in > v) v = in;
				if (v < 1e-4f) v = 0;
				if (v != lv[i][m]) { lv[i][m] = v; moved = true; }
			}
		if (seg && seg->selected != view) seg->select (view);
		static bool wasPlaying; static char lastPos[32];
		char pos[32], tim[32]; positionText (beatNow (), pos, sizeof pos, tim, sizeof tim);
		bool playing = g_audio.isPlaying ();
		if ((moved && view == 1) || playing != wasPlaying || strcmp (pos, lastPos)) { wasPlaying = playing; snprintf (lastPos, sizeof lastPos, "%s", pos); invalidate (true); }
	}

	// ---- the keys ----
	void moveBar (int d)
	{
		const Project &p = g_doc.p;
		int bb = imax (1, p.barBeats ());
		int bar = (int) (beatNow () / bb + 1e-6) + d, last = imax (0, (int) ((p.totalBeats () - 1e-6) / bb));
		double beat = (double) iclamp (bar, 0, last) * bb;
		if (g_arrange) g_arrange->cursorBeat = beat;
		if (g_audio.isPlaying ()) g_audio.seek (g_doc, beat);
		invalidate (true);
	}
	bool cardKey (long k)
	{
		if (k == KEY_LEFT || k == KEY_RIGHT || k == '\t') { cardSel = !cardSel; invalidate (true); return true; }
		if (k == 27 || ((k == KEY_ENTER || k == ' ') && cardSel == 1)) { card = false; invalidate (true); return true; }
		if (k == KEY_ENTER || k == ' ')					// Open FM Tracker (already open: brought in front)
		{
			card = false; invalidate (true);
			if (uk_win_app_raise ("fmtracker") == 0) lx_launch ("fmtracker", 0);
			return true;
		}
		return true;							// (nothing else while the card shows)
	}
	bool onKey (long k) override
	{
		unsigned m = kapi_get_modifiers ();
		if (m & (MOD_CTRL | MOD_ALT)) return false;			// (the Menu's shortcuts, Alt + D-pad)
		if (card) return cardKey (k);
		int map[MAXT + 1], n = strips (map);
		switch (k)
		{
		case ' ': if (g_cmd.playStop) g_cmd.playStop (); return true;
		case KEY_HOME: if (g_cmd.rewind) g_cmd.rewind (); invalidate (true); return true;
		case 27: if (g_audio.isPlaying () && g_cmd.stop) g_cmd.stop (); invalidate (true); return true;
		case KEY_DEL: case KEY_BACKSPACE: return true;		// (no block is deleted here)
		case 's': case 'S': toggle (view == 0 ? selTrack () : map[iclamp (mixSel, 0, n - 1)], true); return true;
		case KEY_ENTER: case 'm': case 'M': toggle (view == 0 ? selTrack () : map[iclamp (mixSel, 0, n - 1)], false); return true;
		}
		if (view == 0)
		{
			if (k == KEY_UP || k == KEY_DOWN) { g_doc.selTrack = iclamp (selTrack () + (k == KEY_UP ? -1 : 1), 0, imax (0, tracks () - 1)); invalidate (true); return true; }
			if (k == KEY_LEFT || k == KEY_RIGHT) { moveBar (k == KEY_LEFT ? -1 : 1); return true; }
			return false;
		}
		if (k == KEY_LEFT || k == KEY_RIGHT)
		{
			mixSel = iclamp (mixSel + (k == KEY_LEFT ? -1 : 1), 0, n - 1);
			if (map[mixSel] >= 0) g_doc.selTrack = map[mixSel];
			invalidate (true); return true;
		}
		if (k == KEY_UP || k == KEY_DOWN)
		{
			double d = ((m & MOD_SHIFT) ? 0.01 : 0.05) * (k == KEY_UP ? 1 : -1);
			int t = map[iclamp (mixSel, 0, n - 1)];
			if (t < 0) { g_audio.engine.masterGain = (float) dmin (1.5, dmax (0, g_audio.engine.masterGain + d)); invalidate (true); }
			else { Track &tr = g_doc.p.tracks[t]; tr.volume = dmin (1.5, dmax (0, tr.volume + d)); mixTouched (); }
			return true;
		}
		return false;
	}
	// a pointer, when there is one: the transport's three buttons, a row, a strip, the card's buttons
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel;
		int e = m_b.edge (bl, 0);
		if (e != 1) return true;
		setFocus ();
		if (card)
		{
			int bx[2], bw[2], by; cardButtons (bx, bw, &by);
			for (int i = 0; i < 2; i++) if (mx >= bx[i] && mx < bx[i] + bw[i] && my >= by && my < by + 34) { cardSel = i; cardKey (KEY_ENTER); }
			return true;
		}
		if (my < BAR)
		{
			int xr = width - 14 - 86;
			for (int k = 0; k < 3; k++, xr -= 40)
				if (mx >= xr - 34 && mx < xr) { if (k == 0 && g_cmd.stop) g_cmd.stop (); if (k == 1 && g_cmd.playStop) g_cmd.playStop (); if (k == 2 && g_cmd.rewind) g_cmd.rewind (); }
			invalidate (true); return true;
		}
		if (view == 0)
		{
			int y0 = BAR + TABS + 20, t = my >= y0 ? rowTop + (my - y0) / ROW : -1;
			if (t >= 0 && t < tracks ())
			{
				g_doc.selTrack = t;
				if (mx >= 96 && mx < 116) toggle (t, false); else if (mx >= 120 && mx < 140) toggle (t, true);
				invalidate (true);
			}
		}
		else
		{
			int map[MAXT + 1], n = strips (map), fit = imax (1, (width - 14) / (STRIP + GAP));
			int x00 = n <= fit ? (width - (n * (STRIP + GAP) - GAP)) / 2 : 14;
			int k = mx >= x00 ? (mx - x00) / (STRIP + GAP) : -1;
			if (k >= 0 && stripFirst + k < n) { mixSel = stripFirst + k; if (map[mixSel] >= 0) g_doc.selTrack = map[mixSel]; invalidate (true); }
		}
		return true;
	}
	static TextFace *g_cardFace;
private:
	Buttons m_b;
};
TextFace *PocketKoton::g_cardFace = 0;

} // namespace kui

#endif
