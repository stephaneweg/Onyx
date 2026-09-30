//
// ui/arrange.h -- the arrangement: ONE widget whose canvas is the size of the view (never the size of
// the song) and that draws only what is visible, from the model: the ruler (bars, the loop), the
// section markers, the tempo, the track headers (name, sound, mute / solo, volume, pan), the lanes
// with their blocks (each a module: its label and a thumbnail of the notes it plays), the chord
// track pinned at the bottom (each chord: its name, its roman numeral, coloured by function), the
// playhead. Mouse: select a block (its editor opens below), drag it (snapped), drag its right edge
// (its length), double-click an empty place (a block of the track's kind there), right-click (a
// menu), the wheel (scroll; Ctrl: zoom).
//
#ifndef _koton_arrange_h
#define _koton_arrange_h

#include "ui/palette.h"
#include "ui/audio.h"

namespace kui {

// ---- labels ----------------------------------------------------------------------------------------------
void moduleLabel (const Project &p, const Module &m, char *buf, int cap)
{
	switch (m.kind)
	{
	case M_PLAYRIFF: { const Riff *r = p.riffById (((const PlayRiffModule &) m).riffId); snprintf (buf, cap, "%s", r ? r->name.c () : "Riff"); return; }
	case M_PATTERN: { const PatternModule &c = (const PatternModule &) m; chordLabel (c.root, c.quality, p.key, buf, cap); return; }
	case M_ARTICULATION:
	{
		const ArticulationModule &a = (const ArticulationModule &) m;
		if (a.style == CUSTOM_STYLE && !a.userStyleName.empty ()) snprintf (buf, cap, "%s", a.userStyleName.c ());
		else if (a.style == CUSTOM_STYLE) snprintf (buf, cap, "Drawn accompaniment");
		else snprintf (buf, cap, "%s", g_styleNames[iclamp (a.style, 0, STYLE_COUNT - 1)]);
		return;
	}
	case M_DRUMKIT:
	{
		const DrumModule &d = (const DrumModule &) m;
		if (!d.catMotif.empty () && d.catMotif != "Personnalisé") snprintf (buf, cap, "%s", d.catMotif.c ());
		else if (d.custom.notes.size ()) snprintf (buf, cap, "Drawn groove");
		else snprintf (buf, cap, "%s%s", g_drumStyleNames[iclamp (d.style, 0, DRUM_STYLE_COUNT - 1)], d.fillLast ? " + fill" : "");
		return;
	}
	case M_CADENCE: snprintf (buf, cap, "Cadence: %s", g_cadenceStyles[iclamp (((const CadenceModule &) m).cadenceStyle, 0, CADENCE_STYLE_COUNT - 1)]); return;
	case M_MELODICLINE: { const MelodicLineModule &l = (const MelodicLineModule &) m; snprintf (buf, cap, "%s", !l.lineName.empty () ? l.lineName.c () : g_contourNames[iclamp (l.contour, 0, 8)]); return; }
	case M_POLYDRUM:
	{
		const PolyDrumModule &pd = (const PolyDrumModule &) m;
		int o = 0; buf[0] = 0;
		for (int i = 0; i < pd.layers.size () && o < cap - 12; i++) o += snprintf (buf + o, cap - o, "%sE(%d,%d)", i ? " " : "", pd.layers[i].hits, pd.layers[i].steps);
		if (!pd.layers.size ()) snprintf (buf, cap, "Polyrhythm");
		return;
	}
	case M_MELODICPOLY: snprintf (buf, cap, "Melodic rings (%d)", ((const MelodicPolyModule &) m).layers.size ()); return;
	case M_POLYCHORD: { const PolyChordModule &pc = (const PolyChordModule &) m; snprintf (buf, cap, "Poly chords (%d rings)%s", pc.layers.size (), pc.monodicPick ? ", melody" : ""); return; }
	case M_GENERATOR: snprintf (buf, cap, "%s", ((const GeneratorModule &) m).generatorId.c ()); return;
	}
	buf[0] = 0;
}

// ---- the thumbnails, made again when the song changes ----------------------------------------------------------
struct Thumb { int track, item; int lo, hi; Vec<RiffNote> notes; int spq; double beats; };

class ArrangeView : public Widget
{
public:
	enum { HEADER_W = 252, RULER_H = 26, MARK_H = 20, TEMPO_H = 22, TRACK_H = 74, SMALL_H = 30, CHORD_H = 72 };
	double ppb;			// pixels per beat
	double scrollBeat;		// the first beat shown
	int scrollY;			// the lanes' vertical scroll (px)
	double snap;			// beats (0: off)
	bool loopOn; double loopA, loopB;
	double cursorBeat;		// where Play starts (the last click on the ruler)
	void (*onSelect) ();		// a block (or nothing) selected
	void (*onEdited) ();		// the song changed
	void (*onPlayFrom) (double beat);

	ArrangeView (int l, int t, int w, int h) : Widget (l, t, w, h), ppb (21.5), scrollBeat (0), scrollY (0), snap (1), loopOn (false),
		loopA (0), loopB (0), cursorBeat (0), onSelect (0), onEdited (0), onPlayFrom (0), m_rev (0), m_drag (DRAG_NONE), m_dragT (-1),
		m_dragI (-1), m_grab (0), m_orig (0), m_lastClickT (0), m_lastClickX (-100), m_lastPlayX (-1)
	{ canFocus = true; anchor = ANCHOR_FILL; }

	// ---- geometry ----
	int laneX () const { return HEADER_W; }
	int laneW () const { return width - HEADER_W - 12; }
	int lanesTop () const { return RULER_H + MARK_H + TEMPO_H; }
	int chordTop () const { return height - CHORD_H - 12; }
	int trackH (int t) const { return g_doc.p.tracks[t].collapsed ? SMALL_H : TRACK_H; }
	double beatAtX (int x) const { return scrollBeat + (x - HEADER_W) / ppb; }
	int xAtBeat (double b) const { return HEADER_W + (int) ((b - scrollBeat) * ppb + (b >= scrollBeat ? 0.5 : -0.5)); }
	double snapBeat (double b) const { if (snap <= 0) return b; return floor (b / snap + 0.5) * snap; }
	// the track at y (-1 none); its top in *top
	int trackAtY (int y, int *top = 0) const
	{
		const Project &p = g_doc.p;
		int ci = p.chordTrackIndex ();
		if (ci >= 0 && y >= chordTop () && y < chordTop () + CHORD_H) { if (top) *top = chordTop (); return ci; }
		int yy = lanesTop () - scrollY;
		for (int t = 0; t < p.tracks.size (); t++)
		{
			if (t == ci) continue;
			int h = trackH (t);
			if (y >= yy && y < yy + h && y >= lanesTop () && y < chordTop ()) { if (top) *top = yy; return t; }
			yy += h;
		}
		return -1;
	}
	int contentH () const { int h = 0; const Project &p = g_doc.p; int ci = p.chordTrackIndex (); for (int t = 0; t < p.tracks.size (); t++) if (t != ci) h += trackH (t); return h; }
	void ensureVisible (double beat)
	{
		if (beat < scrollBeat || beat > scrollBeat + laneW () / ppb - 1) { scrollBeat = beat - 2; if (scrollBeat < 0) scrollBeat = 0; invalidate (true); }
	}

	// ---- drawing ------------------------------------------------------------------------------------------------
	void onDraw () override
	{
		Canvas &cv = canvas;
		const Project &p = g_doc.p;
		if (m_rev != g_doc.revision) rebuildThumbs ();
		cv.clear (BG);
		int fh = th ();
		int bb = p.barBeats ();
		double lastBeat = scrollBeat + laneW () / ppb;
		double songEnd = dmax (p.totalBeats (), 16);
		// ---- the header column's top
		cv.fillRect (0, 0, HEADER_W, lanesTop (), PANEL);
		hline (cv, 0, HEADER_W, lanesTop () - 1, LINE);
		textL (cv, 12, 0, RULER_H, "ARRANGEMENT", DIM, 2);
		textL (cv, 12, RULER_H, MARK_H, "Sections", DIM);
		char b[64];
		textL (cv, 12, RULER_H + MARK_H, TEMPO_H, "Tempo", DIM);
		snprintf (b, sizeof b, "%d bpm", (int) (p.mainBpm () + 0.5));
		textR (cv, HEADER_W - 12, RULER_H + MARK_H, TEMPO_H, b, TEXT);
		// ---- the ruler
		int lx = laneX (), lw = laneW ();
		cv.fillRect (lx, 0, width - lx, RULER_H, PANEL2);
		int step = ppb * bb < 40 ? (ppb * bb < 20 ? 4 : 2) : 1;		// label every n bars
		for (int bar = (int) (scrollBeat / bb); bar * bb <= lastBeat + bb; bar++)
		{
			int x = xAtBeat (bar * bb);
			if (x < lx - 1 || x > lx + lw) continue;
			vline (cv, x, 12, RULER_H, FAINT);
			if (bar % step == 0) { snprintf (b, sizeof b, "%d", bar + 1); textL (cv, x + 4, 1, fh, b, TEXT); }
			if (ppb >= 12) for (int q = 1; q < bb; q++) { int qx = xAtBeat (bar * bb + q); if (qx > lx && qx < lx + lw) vline (cv, qx, RULER_H - 6, RULER_H, LINE); }
		}
		if (loopOn || loopB > loopA)
		{
			int a = xAtBeat (loopA), e = xAtBeat (loopB);
			if (a < lx) a = lx;
			if (e > lx + lw) e = lx + lw;
			if (e > a) blendRect (cv, a, 0, e - a, 6, loopOn ? ACC : FAINT, loopOn ? 170 : 120);
		}
		// ---- the markers lane
		cv.fillRect (lx, RULER_H, width - lx, MARK_H, LANE2);
		for (int i = 0; i < p.markers.size (); i++)
		{
			int x = xAtBeat (p.markers[i].beat);
			if (x < lx - 200 || x > lx + lw) continue;
			int w = tw (p.markers[i].name.c (), 2) + 16;
			blendRect (cv, x, RULER_H + 2, w, MARK_H - 4, PLAY, 50);
			tri (cv, x + 4, RULER_H + 7, 4, 1, PLAY);
			textL (cv, x + 10, RULER_H + 2, MARK_H - 4, p.markers[i].name.c (), PLAY, 2);
		}
		// ---- the tempo lane: its line, the changes
		int ty = RULER_H + MARK_H;
		cv.fillRect (lx, ty, width - lx, TEMPO_H, LANE);
		{
			double lo = 1e9, hi = -1e9;
			for (int i = 0; i < p.tempo.size (); i++) { lo = dmin (lo, p.tempo[i].bpm); hi = dmax (hi, p.tempo[i].bpm); }
			if (hi - lo < 20) { double m = (hi + lo) / 2; lo = m - 10; hi = m + 10; }
			int prevY = -1, prevX = lx;
			for (int i = 0; i < p.tempo.size (); i++)
			{
				int x = imax (lx, xAtBeat (p.tempo[i].beat));
				int y = ty + TEMPO_H - 5 - (int) ((p.tempo[i].bpm - lo) / (hi - lo) * (TEMPO_H - 10));
				if (prevY >= 0) { hline (cv, prevX, x, prevY, PLAY); vline (cv, x, imin (prevY, y), imax (prevY, y) + 1, PLAY); }
				if (x > lx) disc (cv, x, y, 3, PLAY);
				prevY = y; prevX = x;
			}
			if (prevY >= 0) hline (cv, prevX, lx + lw, prevY, PLAY);
		}
		// ---- the tracks (scrolled), then the chord track pinned
		int ci = p.chordTrackIndex ();
		int yy = lanesTop () - scrollY;
		for (int t = 0; t < p.tracks.size (); t++)
		{
			if (t == ci) continue;
			int h = trackH (t);
			if (yy + h > lanesTop () && yy < chordTop ()) drawTrack (cv, t, yy, h, songEnd);
			yy += h;
		}
		cv.fillRect (0, yy > lanesTop () ? yy : lanesTop (), width, 0, BG);
		// (clip what ran under the ruler / the chord track)
		if (ci >= 0) drawChordTrack (cv, ci, chordTop ());
		// the header's top again over a lane scrolled under it
		// ---- the vertical scroll bar, the horizontal one
		{
			int ch = contentH (), vh = chordTop () - lanesTop ();
			WkThumb tb = wk_thumb (ch, vh, scrollY, vh);
			if (tb.show) wk_scroll_bar (cv, width - 10, lanesTop (), 10, vh, true, tb.y, tb.h, BG, WK_NORMAL);
			double total = dmax (songEnd + 16, lastBeat - scrollBeat);
			WkThumb hb = wk_thumb ((long) (total * 16), (long) ((lastBeat - scrollBeat) * 16), (long) (scrollBeat * 16), lw);
			cv.fillRect (0, height - 12, width, 12, PANEL);
			wk_scroll_bar (cv, lx, height - 11, lw, 10, false, hb.y, hb.show ? hb.h : 0, PANEL, WK_NORMAL);
		}
		// ---- the playhead and the cursor
		double ph = g_audio.isPlaying () ? g_audio.playheadBeat () : cursorBeat;
		int px = xAtBeat (ph);
		if (px >= lx && px <= lx + lw)
		{
			vline (cv, px, 0, height - 12, g_audio.isPlaying () ? PLAY : mixc (PLAY, BG, 110));
			for (int i = 0; i < 6; i++) hline (cv, px - 6 + i, px + 7 - i, i, PLAY);
		}
		m_lastPlayX = px;
	}

	void drawHeader (Canvas &cv, int t, int y, int h)
	{
		const Track &tr = g_doc.p.tracks[t];
		bool sel = g_doc.selTrack == t;
		unsigned col = tr.type == TRACK_CHORD ? FUNC_T : trackColour (t);
		cv.fillRect (0, y, HEADER_W, h, sel ? PANEL2 : PANEL);
		hline (cv, 0, HEADER_W, y + h - 1, LINE);
		cv.fillRect (0, y, 5, h - 1, col);
		int fh = th ();
		textFit (cv, 14, y + 4, HEADER_W - 110, fh + 2, tr.name.c (), TEXT, 2);
		// M S buttons
		const char *lab[2] = { "M", "S" };
		bool on[2] = { tr.mute, tr.solo };
		unsigned oc[2] = { 0xDCAA3C, 0x50B4DC };
		if (tr.type != TRACK_CHORD)
			for (int j = 0; j < 2; j++)
			{
				int bx = HEADER_W - 62 + j * 26;
				box (cv, bx, y + 5, 22, 18, 3, on[j] ? oc[j] : FACE);
				textC (cv, bx, y + 5, 22, 18, lab[j], on[j] ? 0x141414 : DIM, 2);
			}
		if (h <= SMALL_H) return;
		// the sound (click: choose)
		wk_sunken (cv, 12, y + 28, HEADER_W - 70, 20, 4, FIELD);
		textFit (cv, 18, y + 28, HEADER_W - 96, 20, instrumentName (tr), TEXT);
		tri (cv, HEADER_W - 70, y + 38, 3, 1, DIM);
		if (tr.type == TRACK_CHORD) { textL (cv, 14, y + 50, 20, "the harmony every track reads", FAINT); return; }
		// the volume bar
		int vx = 12, vw = HEADER_W - 110, vy = y + 56;
		box (cv, vx, vy + 4, vw, 4, 2, FIELD);
		int fill = (int) (dmin (1.5, tr.volume) / 1.5 * vw);
		box (cv, vx, vy + 4, fill, 4, 2, col);
		box (cv, vx + fill - 5, vy, 10, 12, 3, lighter (FACE, 80));
		char b[16]; int db = tr.volume > 0.0001 ? (int) (20 * log10 (tr.volume) * 10) : -999;
		if (db <= -600) snprintf (b, sizeof b, "-inf"); else snprintf (b, sizeof b, "%+.1f", db / 10.0);
		textL (cv, vx + vw + 6, vy - 2, 16, b, DIM);
		// the pan knob
		int kx = HEADER_W - 30, ky = y + 50, r = 13;
		ringArc (cv, kx, ky, r, -45, 225, 3, FIELD);
		int a = (int) (90 - tr.pan * 135);
		if (tr.pan >= 0) ringArc (cv, kx, ky, r, a, 90, 3, col); else ringArc (cv, kx, ky, r, 90, a, 3, col);
		disc (cv, kx, ky, r - 4, FACE);
		aline (cv, kx + wk_cos (a) * (r - 11) / 16384, ky - wk_sin (a) * (r - 11) / 16384, kx + wk_cos (a) * (r - 4) / 16384, ky - wk_sin (a) * (r - 4) / 16384, 2, TEXT);
		textC (cv, kx - 16, ky + r + 1, 32, 12, "PAN", FAINT);
	}

	void drawBlockNotes (Canvas &cv, const Thumb &th_, int x0, int y0, int w, int h, unsigned c, double startBeat)
	{
		if (!th_.notes.size () || th_.spq <= 0) return;
		int lo = th_.lo, hi = th_.hi; if (hi - lo < 12) { int m = (hi + lo) / 2; lo = m - 6; hi = m + 6; }
		for (int i = 0; i < th_.notes.size (); i++)
		{
			const RiffNote &n = th_.notes[i];
			double b = n.start / (double) th_.spq, e = n.end () / (double) th_.spq;
			int nx = xAtBeat (startBeat + b), ne = xAtBeat (startBeat + e);
			if (ne < x0 || nx > x0 + w) continue;
			if (ne - nx < 2) ne = nx + 2;
			int ny = y0 + h - 3 - (n.note - lo) * (h - 6) / (hi - lo);
			cv.fillRect (imax (nx, x0), ny, imin (ne, x0 + w) - imax (nx, x0), 2, c);
		}
	}

	void drawTrack (Canvas &cv, int t, int y, int h, double songEnd)
	{
		const Project &p = g_doc.p;
		const Track &tr = p.tracks[t];
		int lx = laneX (), lw = laneW (), bb = p.barBeats ();
		// clip to the lanes' band (between the tempo lane and the chord track)
		int cy0 = imax (y, lanesTop ()), cy1 = imin (y + h, chordTop ());
		if (cy1 <= cy0) return;
		Canvas sub; sub.adopt (cv.px + (long) cy0 * cv.stride, cv.w, cy1 - cy0, cv.stride);
		int oy = y - cy0;			// the track's top in the sub canvas
		drawHeader (sub, t, oy, h);
		sub.fillRect (lx, oy, width - lx, h, (t & 1) ? LANE2 : LANE);
		hline (sub, lx, width, oy + h - 1, LINE);
		for (int bar = (int) (scrollBeat / bb); bar * bb <= scrollBeat + lw / ppb + bb; bar++)
		{
			int x = xAtBeat (bar * bb);
			if (x >= lx && x <= lx + lw) vline (sub, x, oy, oy + h - 1, GRID_BEAT);
		}
		unsigned col = trackColour (t);
		double c = 0;
		for (int i = 0; i < tr.items.size (); i++)
		{
			c += tr.items[i].silenceBefore;
			double len = p.itemLength (tr.items[i]);
			int x0 = xAtBeat (c) + 1, x1 = xAtBeat (c + len) - 1;
			if (x1 >= lx && x0 <= lx + lw && tr.items[i].module)
			{
				bool sel = g_doc.sel.track == t && g_doc.sel.item == i;
				int bx = imax (x0, lx), bw = imin (x1, lx + lw) - bx;
				int by = oy + 4, bh = h - 9;
				if (h <= SMALL_H) { by = oy + 3; bh = h - 7; }
				box (sub, bx, by, bw, bh, 4, darker (col, sel ? 70 : 110));
				box (sub, bx, by, bw, imin (16, bh), 4, darker (col, sel ? 20 : 50));
				if (sel) frame (sub, bx, by, bw, bh, 4, lighter (col, 140));
				char lab[96];
				moduleLabel (p, *tr.items[i].module, lab, sizeof lab);
				textFit (sub, bx + 6, by, bw - 10, 16, lab, 0xFAFAFA, 2);
				const Thumb *tt = thumbFor (t, i);
				if (tt && bh > 24)
				{
					Canvas nc; int ny0 = by + 18;
					nc.adopt (sub.px + (long) ny0 * sub.stride, sub.w, imax (0, by + bh - 3 - ny0), sub.stride);
					drawBlockNotes (nc, *tt, bx, 0, bw, nc.h, lighter (col, 120), c);
				}
				// the resize handle
				if (x1 <= lx + lw) vline (sub, x1 - 1, by + 4, by + bh - 4, lighter (col, 60));
			}
			c += len;
		}
		(void) songEnd;
	}

	void drawChordTrack (Canvas &cv, int t, int y)
	{
		const Project &p = g_doc.p;
		const Track &tr = p.tracks[t];
		int lx = laneX (), lw = laneW ();
		drawHeader (cv, t, y, CHORD_H);
		hline (cv, 0, width, y, ACC);
		cv.fillRect (lx, y + 1, width - lx, CHORD_H - 1, 0x16191F);
		double c = 0;
		char name[32], roman[24];
		for (int i = 0; i < tr.items.size (); i++)
		{
			c += tr.items[i].silenceBefore;
			double len = p.itemLength (tr.items[i]);
			const Module *m = tr.items[i].module;
			int x0 = xAtBeat (c) + 2, x1 = xAtBeat (c + len) - 2;
			if (x1 >= lx && x0 <= lx + lw && m)
			{
				int bx = imax (x0, lx), bw = imin (x1, lx + lw) - bx;
				bool sel = g_doc.sel.track == t && g_doc.sel.item == i;
				int by = y + 6, bh = CHORD_H - 12;
				if (m->kind == M_PATTERN)
				{
					const PatternModule &pg = (const PatternModule &) *m;
					int fn = chordFunction (p.key, pg.root, pg.quality);
					unsigned fc = funcColour (fn);
					gbox (cv, bx, by, bw, bh, 5, lighter (fc, 30), darker (fc, 70));
					if (sel) frame (cv, bx, by, bw, bh, 5, 0xFFFFFF);
					chordLabel (pg.root, pg.quality, p.key, name, sizeof name);
					romanNumeral (p.key, pg.root, pg.quality, pg.degree, roman, sizeof roman);
					textFit (cv, bx + 8, by + 3, bw - 12, 16, name, 0xFFFFFF, 2);
					static const char *const fnames[4] = { "tonic", "subdom.", "dominant", "" };
					if (bw > 110) textR (cv, bx + bw - 8, by + 3, 16, fnames[fn], mixc (fc, 0xFFFFFF, 170));
					if (bw > 24) textC (cv, bx, by + 18, bw, bh - 22, roman, 0xFFFFFF, 2);
					// the voicing: dots on a 2-octave strip
					int n[16]; int cn = chordNotes (pg.root, pg.octave, pg.quality, pg.inversion, pg.openVoicing, n);
					if (bw > 50) for (int k = 0; k < cn; k++) disc (cv, bx + 10 + (n[k] - n[0]) * 30 / 24, by + bh - 8, 2, 0xFFFFFF, 200);
				}
				else
				{
					unsigned fc = FUNC_O;
					gbox (cv, bx, by, bw, bh, 5, lighter (fc, 30), darker (fc, 70));
					if (sel) frame (cv, bx, by, bw, bh, 5, 0xFFFFFF);
					moduleLabel (p, *m, name, sizeof name);
					textFit (cv, bx + 8, by + 3, bw - 12, 16, name, 0xFFFFFF, 2);
					// the chords inside, as a strip of names
					double s = c;
					Vec<ChordSeg> segs = segments (p, s, len);
					for (int k = 0; k < segs.size (); k++)
					{
						int sx = xAtBeat (segs[k].start), ex = xAtBeat (segs[k].start + segs[k].len);
						if (ex < bx || sx > bx + bw) continue;
						char cl[16]; chordLabel (segs[k].root, segs[k].quality, p.key, cl, sizeof cl);
						textFit (cv, imax (sx, bx) + 4, by + 22, imin (ex, bx + bw) - imax (sx, bx) - 6, 16, cl, 0xFFFFFF);
						if (k) vline (cv, sx, by + 22, by + bh - 4, mixc (fc, 0xFFFFFF, 120));
					}
				}
			}
			c += len;
		}
	}

	// ---- the thumbnails ----
	void rebuildThumbs ()
	{
		m_rev = g_doc.revision;
		m_thumbs.clear ();
		const Project &p = g_doc.p;
		for (int t = 0; t < p.tracks.size (); t++)
		{
			if (p.tracks[t].type == TRACK_CHORD) continue;
			int carry[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
			double c = 0;
			for (int i = 0; i < p.tracks[t].items.size (); i++)
			{
				const Item &it = p.tracks[t].items[i];
				c += it.silenceBefore;
				if (it.module)
				{
					Thumb &th_ = m_thumbs.add ();
					th_.track = t; th_.item = i;
					Riff cell;
					Riff r = renderModule (*it.module, p, c, carry, &cell);
					th_.notes = move (r.notes); th_.spq = r.spq; th_.beats = p.itemLength (it);
					if (cell.notes.size () && cell.spq == r.spq) th_.notes.append (cell.notes);
					th_.lo = 127; th_.hi = 0;
					for (int k = 0; k < th_.notes.size (); k++) { th_.lo = imin (th_.lo, th_.notes[k].note); th_.hi = imax (th_.hi, th_.notes[k].note); }
				}
				c += p.itemLength (it);
			}
		}
	}
	const Thumb *thumbFor (int t, int i) const
	{
		for (int k = 0; k < m_thumbs.size (); k++) if (m_thumbs[k].track == t && m_thumbs[k].item == i) return &m_thumbs[k];
		return 0;
	}

	// ---- the mouse ------------------------------------------------------------------------------------------------------
	enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_LOOP, DRAG_VOLUME, DRAG_PAN, DRAG_VSCROLL, DRAG_HSCROLL, DRAG_RULER };

	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) bm;
		bool rightDown = false;
		int e = m_btn.edge (bl, br, &rightDown);
		if (wheel)
		{
			if (kapi_get_modifiers () & MOD_CTRL)
			{
				double at = beatAtX (mx);
				ppb = dmax (2, dmin (160, ppb * (wheel > 0 ? 1.25 : 0.8)));
				scrollBeat = dmax (0, at - (mx - HEADER_W) / ppb);
			}
			else if ((kapi_get_modifiers () & MOD_SHIFT) || my > chordTop () + CHORD_H)
				scrollBeat = dmax (0, scrollBeat - wheel * 4.0 / (ppb / 21.5));
			else scrollBy (-wheel * 40);
			invalidate (true);
			return true;
		}
		if (e == 1) setFocus ();
		if (e == 1 || rightDown)
		{
			// the scroll bars
			if (mx >= width - 12 && my >= lanesTop () && my < chordTop ()) { m_drag = DRAG_VSCROLL; dragScrollV (my); return true; }
			if (my >= height - 12 && mx >= laneX ()) { m_drag = DRAG_HSCROLL; dragScrollH (mx); return true; }
			// the ruler: the cursor (click), the loop (drag)
			if (my < RULER_H && mx >= laneX ())
			{
				double b = snapBeat (beatAtX (mx));
				if (rightDown) { loopOn = !loopOn; if (onEdited) onEdited (); invalidate (true); return true; }
				m_drag = DRAG_RULER; m_grab = b; cursorBeat = dmax (0, b);
				if (g_audio.isPlaying () && onPlayFrom) onPlayFrom (cursorBeat);
				invalidate (true);
				return true;
			}
			if (my >= RULER_H && my < RULER_H + MARK_H && mx >= laneX ())
			{
				if (isDouble (mx)) addMarker (snapBeat (beatAtX (mx)));
				return true;
			}
			int top = 0;
			int t = trackAtY (my, &top);
			if (t < 0) return true;
			// the header
			if (mx < HEADER_W)
			{
				if (g_doc.selTrack != t) { g_doc.selTrack = t; invalidate (true); }
				if (rightDown) { headerMenu (t, mx, my); return true; }
				headerClick (t, mx, my - top);
				return true;
			}
			double b = beatAtX (mx);
			int i = g_doc.itemAt (t, b);
			g_doc.selTrack = t;
			if (rightDown) { laneMenu (t, i, b, mx, my); return true; }
			if (i >= 0)
			{
				select (t, i);
				double s = g_doc.itemStart (t, i), len = g_doc.itemLen (t, i);
				if (mx >= xAtBeat (s + len) - 6) { m_drag = DRAG_RESIZE; m_orig = len; }
				else { m_drag = DRAG_MOVE; m_grab = b - s; m_orig = s; }
				m_dragT = t; m_dragI = i; m_moved = false;
			}
			else
			{
				select (-1, -1);
				if (isDouble (mx)) insertDefault (t, snapBeat (b));
			}
			return true;
		}
		if (e == -1)
		{
			if ((m_drag == DRAG_MOVE || m_drag == DRAG_RESIZE) && m_moved && onEdited) onEdited ();
			if (m_drag == DRAG_RULER && onEdited) onEdited ();
			m_drag = DRAG_NONE;
			return true;
		}
		if (bl && m_drag != DRAG_NONE)
		{
			switch (m_drag)
			{
			case DRAG_VSCROLL: dragScrollV (my); break;
			case DRAG_HSCROLL: dragScrollH (mx); break;
			case DRAG_RULER:
			{
				double b = snapBeat (beatAtX (mx));
				if (fabs (b - m_grab) >= (snap > 0 ? snap : 0.25)) { loopA = dmin (m_grab, b); loopB = dmax (m_grab, b); loopOn = true; }
				invalidate (true);
			} break;
			case DRAG_MOVE:
			{
				double nb = snapBeat (beatAtX (mx) - m_grab);
				if (nb < 0) nb = 0;
				if (fabs (nb - g_doc.itemStart (m_dragT, m_dragI)) > 1e-9)
				{
					if (!m_moved) { g_doc.checkpoint (); m_moved = true; }
					g_doc.moveItem (m_dragT, m_dragI, nb);
					g_doc.changed (); invalidate (true);
				}
			} break;
			case DRAG_RESIZE:
			{
				double s = g_doc.itemStart (m_dragT, m_dragI);
				double want = snapBeat (beatAtX (mx)) - s;
				double mn = snap > 0 ? snap : 0.25;
				if (want < mn) want = mn;
				double cur = g_doc.itemLen (m_dragT, m_dragI);
				if (fabs (want - cur) > 1e-9)
				{
					if (!m_moved) { g_doc.checkpoint (); m_moved = true; }
					resizeModule (m_dragT, m_dragI, want);
					g_doc.changed (); invalidate (true);
				}
			} break;
			case DRAG_VOLUME: headerDragVolume (mx); break;
			case DRAG_PAN: headerDragPan (my); break;
			}
			return true;
		}
		return true;
	}

	bool onKey (long k) override
	{
		if (k == KEY_DEL || k == KEY_BACKSPACE) { deleteSelected (); return true; }
		if (k == WK_CTRL ('D')) { duplicateSelected (); return true; }
		if (k == KEY_LEFT || k == KEY_RIGHT)
		{
			if (!g_doc.sel.valid ()) return false;
			int t = g_doc.sel.track, n = g_doc.p.tracks[t].items.size ();
			int i = g_doc.sel.item + (k == KEY_LEFT ? -1 : 1);
			if (i >= 0 && i < n) select (t, i);
			return true;
		}
		return false;
	}

	void select (int t, int i)
	{
		g_doc.sel.track = t; g_doc.sel.item = i;
		if (t >= 0) g_doc.selTrack = t;
		invalidate (true);
		if (onSelect) onSelect ();
	}
	void deleteSelected ()
	{
		if (!g_doc.sel.valid ()) return;
		g_doc.checkpoint ();
		g_doc.removeItem (g_doc.sel.track, g_doc.sel.item);
		g_doc.changed ();
		select (-1, -1);
		if (onEdited) onEdited ();
	}
	void duplicateSelected ()
	{
		if (!g_doc.sel.valid ()) return;
		g_doc.checkpoint ();
		int t = g_doc.sel.track, i = g_doc.duplicateItem (t, g_doc.sel.item);
		g_doc.changed ();
		if (i >= 0) select (t, i);
		if (onEdited) onEdited ();
	}

	void scrollBy (int dy)
	{
		int maxS = imax (0, contentH () - (chordTop () - lanesTop ()));
		scrollY = iclamp (scrollY + dy, 0, maxS);
	}
	void dragScrollV (int my)
	{
		int ch = contentH (), vh = chordTop () - lanesTop ();
		WkThumb tb = wk_thumb (ch, vh, scrollY, vh);
		scrollY = (int) wk_thumb_pos (my - lanesTop (), vh, ch, vh, tb.h);
		invalidate (true);
	}
	void dragScrollH (int mx)
	{
		double songEnd = dmax (g_doc.p.totalBeats (), 16), view = laneW () / ppb;
		double total = dmax (songEnd + 16, view);
		WkThumb hb = wk_thumb ((long) (total * 16), (long) (view * 16), (long) (scrollBeat * 16), laneW ());
		scrollBeat = wk_thumb_pos (mx - laneX (), laneW (), (long) (total * 16), (long) (view * 16), hb.h) / 16.0;
		invalidate (true);
	}

	// ---- the header's controls ----
	void headerClick (int t, int x, int y)
	{
		Track &tr = g_doc.p.tracks[t];
		if (y >= 5 && y < 23 && tr.type != TRACK_CHORD)
		{
			for (int j = 0; j < 2; j++)
				if (x >= HEADER_W - 62 + j * 26 && x < HEADER_W - 40 + j * 26)
				{
					if (j == 0) tr.mute = !tr.mute; else tr.solo = !tr.solo;
					g_doc.dirty = true; invalidate (true);
					if (onEdited) onEdited ();
					return;
				}
		}
		if (tr.collapsed) return;
		if (y >= 28 && y < 48 && x < HEADER_W - 58) { chooseSound (t); return; }
		if (tr.type == TRACK_CHORD) return;
		if (y >= 52 && y < 72 && x < HEADER_W - 98) { m_drag = DRAG_VOLUME; m_dragT = t; headerDragVolume (x); return; }
		if (x >= HEADER_W - 46 && y >= 34) { m_drag = DRAG_PAN; m_dragT = t; m_grab = my0 = y; m_orig = tr.pan; return; }
		if (isDouble (x)) renameTrack (t);
	}
	int my0;
	void headerDragVolume (int mx)
	{
		Track &tr = g_doc.p.tracks[m_dragT];
		double v = (mx - 12) / (double) (HEADER_W - 110) * 1.5;
		tr.volume = dmax (0, dmin (1.5, v));
		g_doc.dirty = true; invalidate (true);
		if (onEdited) onEdited ();
	}
	void headerDragPan (int my)
	{
		int top = 0; trackAtY (my, &top);
		Track &tr = g_doc.p.tracks[m_dragT];
		tr.pan = dmax (-1, dmin (1, m_orig + (m_grab - (my - top)) / 60.0));
		if (fabs (tr.pan) < 0.04) tr.pan = 0;
		g_doc.dirty = true; invalidate (true);
		if (onEdited) onEdited ();
	}

	void chooseSound (int t);			// (dialogs.h)
	void renameTrack (int t);
	void headerMenu (int t, int mx, int my);
	void laneMenu (int t, int i, double beat, int mx, int my);
	void insertDefault (int t, double beat);
	void addMarker (double beat);
	void resizeModule (int t, int i, double beats);

private:
	unsigned m_rev;
	Vec<Thumb> m_thumbs;
	Buttons m_btn;
	int m_drag, m_dragT, m_dragI;
	double m_grab, m_orig;
	bool m_moved;
	unsigned m_lastClickT; int m_lastClickX;
	int m_lastPlayX;
	bool isDouble (int x)
	{
		unsigned now = kapi_get_ticks ();
		bool d = now - m_lastClickT < 40 && iabs (x - m_lastClickX) < 6;
		m_lastClickT = d ? 0 : now; m_lastClickX = x;
		return d;
	}
};

// ---- a module's length set by dragging its edge ------------------------------------------------------------------
void ArrangeView::resizeModule (int t, int i, double beats)
{
	Project &p = g_doc.p;
	Module *m = g_doc.module (t, i);
	if (!m) return;
	double old = g_doc.itemLen (t, i);
	int bb = p.barBeats ();
	switch (m->kind)
	{
	case M_PLAYRIFF:
	{
		Riff *r = p.riffById (((PlayRiffModule *) m)->riffId);
		if (r) r->lengthSlices = imax (1, iround (beats * r->spq));
	} break;
	case M_PATTERN:
	{
		PatternModule *pg = (PatternModule *) m;
		if (pg->style == CUSTOM_STYLE && pg->custom.slices.size ()) pg->repeats = imax (1, iround (beats / (pg->custom.slices.size () / (double) pg->custom.spq)));
		else { pg->beatsPerBar = imax (1, iround (beats)); pg->repeats = 1; }
	} break;
	case M_ARTICULATION: ((ArticulationModule *) m)->lengthBeats = dmax (0.25, beats); break;
	case M_MELODICLINE: ((MelodicLineModule *) m)->beatsPerBar = imax (1, iround (beats)); break;
	case M_DRUMKIT:
	{
		DrumModule *d = (DrumModule *) m;
		double unit = (d->style == DRUM_CUSTOM_STYLE && d->custom.slices.size ()) ? d->custom.slices.size () / (double) d->custom.spq : d->beatsPerBar;
		d->repeats = imax (1, iround (beats / dmax (0.25, unit)));
	} break;
	case M_POLYDRUM: { PolyDrumModule *pd = (PolyDrumModule *) m; pd->repeats = imax (1, iround (beats / imax (1, pd->beats))); } break;
	case M_MELODICPOLY: { MelodicPolyModule *mp = (MelodicPolyModule *) m; mp->repeats = imax (1, iround (beats / imax (1, mp->beats))); } break;
	case M_POLYCHORD: ((PolyChordModule *) m)->beats = dmax (1, iround (beats)); break;
	case M_GENERATOR: ((GeneratorModule *) m)->durationBeats = dmax (0.25, beats); break;
	case M_CADENCE: break;
	}
	(void) bb;
	g_doc.lengthChanged (t, i, old);
}

} // namespace kui

#endif
