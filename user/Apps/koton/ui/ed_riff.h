//
// ui/ed_riff.h -- the RIFF editor (Koton's piano roll): the riff's notes on a keyboard of 96 notes
// (C0..B7), the chords of the chord track over it (their names above the grid, their tones shaded
// in the rows, the key's scale lighter than the notes outside it), the tools (draw, select, erase),
// the snap and the drawn length, the riff's length and name, transposition, "fit to the chords"
// (every note moved to the nearest tone of the chord under it), and step recording from a USB MIDI
// keyboard. Also the GENERATOR block's editor (an IPC generator plugin) and the factory that
// makes the editor of a block.
//
#ifndef _koton_ed_riff_h
#define _koton_ed_riff_h

#include "ui/ed_rhythm.h"

namespace kui {

static const char *const s_snapNames[] = { "Bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 triplet", "1/8 triplet", "1/16 triplet", "Off" };
static const int s_snapBeats24[] = { -1, 48, 24, 12, 6, 3, 16, 8, 4, 1 };	// in 24ths of a beat (-1: a bar)
static const char *const s_noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

class RiffEditor : public Editor
{
public:
	enum { T_TOOL = 1, T_SNAP, T_LEN, T_BEATS, T_NAME, T_DOWN12, T_DOWN1, T_UP1, T_UP12, T_FIT, T_REC, T_CLEAR, T_QUANT };
	static int s_tool, s_snap, s_len;
	NoteGrid *grid;
	Textbox *name;
	Checkbox *rec;
	Vec<ChordSeg> segs;
	int recCol;			// step recording: where the next note goes
	unsigned recLastUs;		// the last note's time (notes within 40 ms make a chord)
	int program;

	RiffEditor (int l, int t, int w0, int h0, int tr, int it) : Editor (l, t, w0, h0, tr, it), grid (0), name (0), rec (0), recCol (0), recLastUs (0), program (0)
	{
		Riff *r = riff ();
		const Project &p = g_doc.p;
		const Track &tk = p.tracks[track];
		program = tk.instrument;
		static const char *const tools[3] = { "Draw", "Select", "Erase" };
		int x = 16, y = 8;
		Tabs *tb = new Tabs (x, y, 210, 28, tools, 3, s_tool, trampoline); tb->tag = T_TOOL; addChild (tb);
		x += 222;
		label (x, y + 3, 40, "Snap"); dropdown (x + 40, y + 2, 116, T_SNAP, s_snapNames, 10, s_snap); x += 166;
		label (x, y + 3, 50, "Length"); dropdown (x + 50, y + 2, 116, T_LEN, s_snapNames, 9, s_len); x += 176;
		label (x, y + 3, 44, "Beats"); number (x + 44, y + 2, 76, T_BEATS, 1, 1024, r ? imax (1, iround (r->beats ())) : 4); x += 132;
		label (x, y + 3, 44, "Name");
		name = new Textbox (x + 44, y + 2, 170, 24, r ? r->name.c () : "", trampoline); name->tag = T_NAME; addChild (name);
		x += 226;
		// the second row: transposition, the chords, step recording
		int x2 = 16, y2 = y + 34;
		Button *b;
		b = button (x2, y2, 44, "-12", T_DOWN12); b->tip = "Down an octave";
		b = button (x2 + 48, y2, 36, "-1", T_DOWN1); b->tip = "Down a semitone";
		b = button (x2 + 88, y2, 36, "+1", T_UP1); b->tip = "Up a semitone";
		b = button (x2 + 128, y2, 44, "+12", T_UP12); b->tip = "Up an octave";
		x2 += 184;
		b = button (x2, y2, 140, "Fit to the chords", T_FIT); b->tip = "Every note moved to the nearest tone of the chord under it";
		x2 += 148;
		b = button (x2, y2, 90, "Quantise", T_QUANT); b->tip = "The notes' starts on the snap";
		x2 += 104;
		rec = check (x2, y2 + 2, 150, "Step record", T_REC, false);
		rec->tip = "Notes played on a USB MIDI keyboard are written at the cursor, one after the other";
		button (width - 86, y2, 70, "Clear", T_CLEAR);
		// the grid
		int gy = 76;
		grid = makeGrid (16, gy, width - 32, height - gy - 6);
		grid->rows = 96; grid->rowH = 26; grid->labelW = 52; grid->pxPerCol = 26; grid->pads = true;	// (Koton Studio's pads)
		grid->bottomUp = true; grid->keyboard = true;
		grid->headerH = 22;
		grid->rowLabel = keyLabel; grid->rowShade = shade; grid->drawHeader = header; grid->onAudition = audition;
		grid->noteColour = trackColour (track);
		grid->tool = s_tool;
		if (r)
		{
			grid->notes = &r->notes;
			grid->spb = imax (1, r->spq); grid->cols = imax (1, r->lengthSlices);
		}
		grid->beatsPerBar = imax (1, p.barBeats ());
		applySnap ();
		segs = segments (p, startBeat (), lenBeats ());
		// the first note in view (else middle C)
		// (the rows are tall: the first note's pitch, what shows first at the left)
		int first = -1;
		if (r) for (int i = 0; i < r->notes.size (); i++) if (first < 0 || r->notes[i].start < r->notes[first].start) first = i;
		grid->centreRow (first >= 0 ? r->notes[first].note : 48);
	}
	const char *title () override { return "Riff"; }
	PlayRiffModule *pr () { return (PlayRiffModule *) module (); }
	Riff *riff () { PlayRiffModule *m = pr (); return m ? g_doc.p.riffById (m->riffId) : 0; }
	CompiledSong *previewSong () override
	{
		Riff *r = riff ();
		if (!r) return 0;
		return compileModulePreview (g_doc.p, track, item, SOUND_RATE);
	}
	int snapSlices (int which)
	{
		Riff *r = riff (); int spq = r ? imax (1, r->spq) : SPQ;
		int v = s_snapBeats24[iclamp (which, 0, 9)];
		if (v < 0) return imax (1, g_doc.p.barBeats () * spq);
		return imax (1, v * spq / SPQ);
	}
	void applySnap ()
	{
		grid->snapCols = snapSlices (s_snap);
		grid->drawLen = snapSlices (s_len);
	}

	static const char *keyLabel (NoteGrid &g, int r)
	{
		(void) g;
		static char b[8];
		int midi = r + 12;
		if (midi % 12 != 0) return "";
		snprintf (b, sizeof b, "C%d", midi / 12 - 1);
		return b;
	}
	// a cell's shade: the chord's tones (the root stronger), the key's scale, the rest darker
	static unsigned shade (NoteGrid &g, int row, int col)
	{
		RiffEditor *e = (RiffEditor *) g.ctx;
		const Project &p = g_doc.p;
		int pc = (row + 12) % 12;
		unsigned base = g.padColour (row, col);
		double beat = col / (double) imax (1, g.spb);
		for (int i = 0; i < e->segs.size (); i++)
		{
			const ChordSeg &s = e->segs[i];
			double a = s.start - e->startBeat ();
			if (beat >= a - 1e-9 && beat < a + s.len - 1e-9)
			{
				int iv[16]; int n = chordNotes (s.root, 4, s.quality, 0, false, iv);
				for (int k = 0; k < n; k++)
					if (iv[k] % 12 == pc)
					{
						unsigned fc = funcColour (chordFunction (p.key, imod (s.root, 12), s.quality));
						return mixc (base, fc, k == 0 ? 70 : 40);
					}
				break;
			}
		}
		const int *sc = modeScale (effectiveMode (p.key));
		int t = tonicPc (p.key);
		for (int k = 0; k < 7; k++) if ((t + sc[k]) % 12 == pc) return base;
		return darker (base, 30);
	}
	// the chords over the grid
	static void header (NoteGrid &g, Canvas &cv, int x0, int w)
	{
		RiffEditor *e = (RiffEditor *) g.ctx;
		const Project &p = g_doc.p;
		double sb = e->startBeat ();
		for (int i = 0; i < e->segs.size (); i++)
		{
			const ChordSeg &s = e->segs[i];
			int c0 = (int) ((s.start - sb) * g.spb + 0.5), c1 = (int) ((s.start + s.len - sb) * g.spb + 0.5);
			int a = imax (x0, g.colX (c0)) + 1, b = imin (x0 + w, g.colX (c1)) - 1;
			if (b - a < 4) continue;
			unsigned fc = funcColour (chordFunction (p.key, imod (s.root, 12), s.quality));
			box (cv, a, 2, b - a, g.headerH - 4, 4, mixc (fc, PANEL2, 90));
			char nm[24]; chordLabel (s.root, s.quality, p.key, nm, sizeof nm);
			textFit (cv, a + 5, 2, b - a - 8, g.headerH - 4, nm, 0xFFFFFF, 2);
		}
		if (!e->segs.size ()) textL (cv, x0 + 6, 0, g.headerH, "no chord under this riff: write chords on the chord track", FAINT);
		// the step-record cursor
		if (e->rec && e->rec->checked)
		{
			int x = g.colX (e->recCol);
			if (x >= x0 && x <= x0 + w) { vline (cv, x, 0, g.headerH, REC); tri (cv, x, g.headerH - 5, 4, 1, REC); }
		}
	}
	static void audition (NoteGrid &g, int row, bool on)
	{
		RiffEditor *e = (RiffEditor *) g.ctx;
		if (on) g_audio.noteOn (row + 12, 100, e->program, false); else g_audio.noteOff (row + 12);
	}
	void gridEdited (NoteGrid &g) override
	{
		(void) g;
		Riff *r = riff ();
		if (r) sortNotes (r->notes);
		grid->selected.clear ();
		done ();
	}
	// a note from a MIDI keyboard: step recording
	bool midiNote (int note, int vel, bool on) override
	{
		Riff *r = riff ();
		if (!r || !rec->checked || !on || vel == 0) return false;
		if (note < 12 || note >= 108) return true;
		unsigned now = kapi_clock_us ();
		bool chord = recLastUs && now - recLastUs < 40000;
		int len = snapSlices (s_len);
		int at = chord ? recCol - len : recCol;
		if (at < 0) at = 0;
		if (at >= r->lengthSlices) { at = 0; recCol = 0; }
		begin ();
		r->notes.push (RiffNote (note - 12, at, imin (len, r->lengthSlices - at)));
		sortNotes (r->notes);
		if (!chord) recCol = at + len;
		if (recCol >= r->lengthSlices) recCol = 0;
		recLastUs = now;
		done ();
		grid->invalidate (true);
		return true;
	}
	void transpose (int d)
	{
		Riff *r = riff ();
		if (!r) return;
		begin ();
		bool sel = grid->selected.size () > 0;
		for (int i = 0; i < r->notes.size (); i++)
			if (!sel || grid->selected.contains (i)) r->notes[i].note = iclamp (r->notes[i].note + d, 0, 95);
		done ();
		grid->invalidate (true);
	}
	void fitToChords ()
	{
		Riff *r = riff ();
		if (!r) return;
		begin ();
		double sb = startBeat ();
		for (int i = 0; i < r->notes.size (); i++)
		{
			RiffNote &n = r->notes[i];
			int root, q, inv;
			if (!chordAt (g_doc.p, sb + n.start / (double) imax (1, r->spq), &root, &q, &inv)) continue;
			int iv[16]; int c = chordNotes (root, 4, q, 0, false, iv);
			int midi = n.note + 12, best = midi, bd = 99;
			for (int d = 0; d <= 6 && bd == 99; d++)
				for (int s = -1; s <= 1; s += 2)
				{
					int m = midi + s * d;
					for (int k = 0; k < c; k++) if (imod (iv[k] - m, 12) == 0) { if (d < bd) { bd = d; best = m; } }
					if (!d) break;
				}
			n.note = iclamp (best - 12, 0, 95);
		}
		done ();
		grid->invalidate (true);
	}
	void quantise ()
	{
		Riff *r = riff ();
		if (!r) return;
		begin ();
		int sc = imax (1, grid->snapCols);
		for (int i = 0; i < r->notes.size (); i++)
		{
			RiffNote &n = r->notes[i];
			n.start = iclamp (((n.start + sc / 2) / sc) * sc, 0, imax (0, r->lengthSlices - 1));
			if (n.start + n.length > r->lengthSlices) n.length = imax (1, r->lengthSlices - n.start);
		}
		sortNotes (r->notes);
		done ();
		grid->invalidate (true);
	}
	void control (int tag, Widget &w) override
	{
		Riff *r = riff ();
		if (!r) return;
		switch (tag)
		{
		case T_TOOL: s_tool = ((Tabs &) w).sel; grid->tool = s_tool; break;
		case T_SNAP: s_snap = ((Dropdown &) w).sel; applySnap (); break;
		case T_LEN: s_len = ((Dropdown &) w).sel; applySnap (); break;
		case T_BEATS:
		{
			begin ();
			int n = imax (1, ((NumericUpDown &) w).value) * imax (1, r->spq);
			r->lengthSlices = n;
			for (int i = r->notes.size () - 1; i >= 0; i--)
				if (r->notes[i].start >= n) r->notes.removeAt (i);
				else if (r->notes[i].end () > n) r->notes[i].length = n - r->notes[i].start;
			done (); rebuild ();
		} break;
		case T_NAME:
			if (strcmp (name->text, r->name.c ())) { begin (); r->name = name->text; done (); }
			break;
		case T_DOWN12: transpose (-12); break;
		case T_DOWN1: transpose (-1); break;
		case T_UP1: transpose (1); break;
		case T_UP12: transpose (12); break;
		case T_FIT: fitToChords (); break;
		case T_QUANT: quantise (); break;
		case T_REC: recCol = 0; recLastUs = 0; grid->invalidate (true); break;
		case T_CLEAR: begin (); r->notes.clear (); done (); grid->selected.clear (); grid->invalidate (true); break;
		}
	}
};
int RiffEditor::s_tool = TOOL_DRAW, RiffEditor::s_snap = 4, RiffEditor::s_len = 3;

// ---- a generator plugin's block --------------------------------------------------------------------------------------------
class GeneratorEditor : public Editor
{
public:
	enum { T_BEATS = 1, T_OPEN };
	GeneratorEditor (int l, int t, int w0, int h0, int tr, int it) : Editor (l, t, w0, h0, tr, it)
	{
		GeneratorModule *g = gen ();
		formStart (16, 10, 290);
		fSection ("GENERATOR PLUGIN");
		fNum ("Beats", T_BEATS, 1, 1024, iround (g->durationBeats));
		fButton ("Open the plugin's editor", T_OPEN);
		fNote ("A generator plugin (a process of its own) writes this block's notes from the chords under it.", 3);
	}
	const char *title () override { return "Generator"; }
	GeneratorModule *gen () { return (GeneratorModule *) module (); }
	void drawExtra (Canvas &cv) override
	{
		GeneratorModule *g = gen ();
		int x = formRight () + 24;
		sectionAt (cv, x, 14, width - x - 16, "PLUGIN");
		char b[160]; snprintf (b, sizeof b, "%s", g->generatorId.empty () ? "(none)" : g->generatorId.c ());
		textL (cv, x, 40, 26, b, TEXT, 2);
		snprintf (b, sizeof b, "state: %d bytes", g->state.len ());
		textL (cv, x, 68, 22, b, DIM);
	}
	void control (int tag, Widget &w) override
	{
		GeneratorModule *g = gen ();
		if (tag == T_BEATS) { begin (); g->durationBeats = imax (1, ((NumericUpDown &) w).value); done (); }
		else if (tag == T_OPEN && s_openGenerator) s_openGenerator (track, item);
	}
	static void (*s_openGenerator) (int track, int item);	// (the plugin host, main.cpp)
};
void (*GeneratorEditor::s_openGenerator) (int, int) = 0;

// ---- the editor of a block --------------------------------------------------------------------------------------------------
static inline Editor *makeEditor (int l, int t, int w, int h, int tr, int it)
{
	Module *m = g_doc.module (tr, it);
	if (!m) return 0;
	switch (m->kind)
	{
	case M_PLAYRIFF: return new RiffEditor (l, t, w, h, tr, it);
	case M_PATTERN: return new ChordEditor (l, t, w, h, tr, it);
	case M_ARTICULATION: return new ArticulationEditor (l, t, w, h, tr, it);
	case M_CADENCE: return new CadenceEditor (l, t, w, h, tr, it);
	case M_DRUMKIT: return new DrumEditor (l, t, w, h, tr, it);
	case M_MELODICLINE: return new MelodicLineEditor (l, t, w, h, tr, it);
	case M_POLYDRUM: case M_MELODICPOLY: case M_POLYCHORD: return new PolyEditor (l, t, w, h, tr, it);
	case M_GENERATOR: return new GeneratorEditor (l, t, w, h, tr, it);
	}
	return 0;
}

} // namespace kui

#endif
