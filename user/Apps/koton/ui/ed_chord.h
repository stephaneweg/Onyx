//
// ui/ed_chord.h -- the harmony editors, as Koton Studio's: the CHORD (a block of the chord track:
// only the harmony -- its degree in the key or a fixed root, its colour, suspension, forced mode,
// its length, the voicing intention -- with a keyboard showing it and the next-chord co-pilot), the
// ACCOMPANIMENT (a chord articulation on an instrument track: how the chord track's chords are
// played -- a style or a drawn voice grid, the bass, the voicing, the voice leading -- and a melodic
// cell over the chord's degrees) and the legacy CADENCE block.
//
#ifndef _koton_ed_chord_h
#define _koton_ed_chord_h

#include "ui/editor.h"

namespace kui {

static const char *const s_vlChord[] = { "None (root position)", "Auto (least motion)", "Close at the top", "Close to the bass", "Fixed inversion" };
static const char *const s_vlArtic[] = { "None (fixed inversion)", "Auto (least motion)", "Close at the top", "Close to the bass", "As the chord says" };
static const int s_vlUiToModel[5] = { 0, 1, 3, 2, 4 };
static inline int vlUiIndex (int model) { for (int i = 0; i < 5; i++) if (s_vlUiToModel[i] == model) return i; return 0; }
static const char *const s_bassNames[] = { "None", "Per bar (held)", "Per beat" };
static const char *const s_climbNames[] = { "Arpeggio up", "Arpeggio down", "Alberti (1-5-3)", "Mixed" };
static const char *const s_heldNames[] = { "Single note", "Block chord", "Root + fifth", "Root + third" };
static const char *const s_openNames[] = { "No", "Yes", "As the chord says" };
static const char *const s_dirNames[] = { "Auto", "Rising", "Falling" };
static const char *const s_melRows[MELODIC_ROW_COUNT] = { "1", "2", "3", "4", "5", "6", "7", "1'", "2'", "3'", "4'", "5'", "6'", "7'", "1''", "2''", "3''", "4''", "5''", "6''", "7''" };

// ---- the next-chord cards ---------------------------------------------------------------------------------------
class SuggestStrip : public Widget
{
public:
	Vec<Suggestion> list;
	Key key;
	void (*onPick) (int index, bool replace);
	SuggestStrip (int l, int t, int w, int h) : Widget (l, t, w, h), onPick (0), m_hot (-1) {}
	int cardW () const { return imin (150, (width - 8 * 5) / 6); }
	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (parent ? parent->bgColor () : PANEL);
		int cw = cardW ();
		for (int i = 0; i < list.size () && i < 6; i++)
		{
			const Suggestion &s = list[i];
			RootQ c = suggestionChord (s, key);
			int x = i * (cw + 8);
			unsigned fc = funcColour (chordFunction (key, c.root, c.quality));
			gbox (cv, x, 0, cw, height, 6, i == m_hot ? lighter (PANEL2, 20) : PANEL2, PANEL2);
			frame (cv, x, 0, cw, height, 6, s.recommended ? ACC : LINE);
			box (cv, x + 6, 6, 40, height - 12, 5, fc);
			char r[16]; romanNumeral (key, c.root, c.quality, s.deg, r, sizeof r);
			textC (cv, x + 6, 6, 40, height - 12, r, 0xFFFFFF, 2);
			char nm[16]; chordLabel (c.root, c.quality, key, nm, sizeof nm);
			textFit (cv, x + 54, 6, cw - 58, (height - 12) / 2, nm, TEXT, 2);
			textFit (cv, x + 54, 6 + (height - 12) / 2, cw - 58, (height - 12) / 2, s.effect ? s.effect : "", DIM);
		}
		if (!list.size ()) textL (cv, 0, 0, height, "(no suggestion: this chord is not on a degree of the key)", FAINT);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) my; (void) bm; (void) wheel;
		bool rd = false; int e = m_b.edge (bl, br, &rd);
		int cw = cardW (), i = mx / (cw + 8);
		if (i >= list.size () || i >= 6 || mx - i * (cw + 8) > cw) i = -1;
		if (i != m_hot) { m_hot = i; invalidate (true); }
		if (i >= 0 && (e == 1 || rd) && onPick) onPick (i, rd);
		return true;
	}
private:
	Buttons m_b; int m_hot;
};

// ---- the chord --------------------------------------------------------------------------------------------------
class ChordEditor : public Editor
{
public:
	enum { T_DEG = 1, T_ROOT, T_COLOUR, T_SUSP, T_MODE, T_BEATS, T_OPEN, T_VL, T_INV, T_STYLE, T_OCT, T_BASS, T_CLIMB, T_HELD, T_HALVE,
		T_MOOD, T_CHAIN, T_CADENCE, T_PROG };
	DegreeChoices choices;
	MiniKeys *keys;
	SuggestStrip *strip;
	Dropdown *rootDd;
	int mood;
	static int s_mood;

	ChordEditor (int l, int t, int w0, int h0, int tr, int it) : Editor (l, t, w0, h0, tr, it), keys (0), strip (0), rootDd (0), mood (s_mood)
	{
		PatternModule *pg = pat ();
		const Project &p = g_doc.p;
		choices.build (p.key);
		// Koton normalises a legacy chord once: its length in BeatsPerBar, one repeat
		if (pg->repeats > 1 && pg->style != CUSTOM_STYLE) { pg->beatsPerBar = imax (1, pg->beatsPerBar * pg->repeats); pg->repeats = 1; }	// (the same length)
		if (pg->degree < 0) syncColourTrio (*pg);
		formStart (16, 10, 290);
		fSection ("THE CHORD");
		fDrop ("Degree", T_DEG, choices.ptrs, choices.n, choices.indexOf (pg->degree, imod (pg->root, 12), pg->quality));
		rootDd = fDrop ("Root", T_ROOT, g_rootNames, 12, imod (pg->root, 12));
		rootDd->disabled = pg->degree >= 0;
		fDrop ("Colour", T_COLOUR, g_colourNames, 5, pg->diatonicColour);
		fDrop ("Suspension", T_SUSP, g_suspensionNames, 3, pg->suspension);
		fDrop ("Force", T_MODE, g_modeOverrideNames, 6, pg->modeOverride);
		fNum ("Beats", T_BEATS, 1, 64, pg->beatsPerBar);
		fSection ("VOICING (read by the accompaniments)");
		fCheck ("Open voicing (spread)", T_OPEN, pg->openVoicing);
		fDrop ("Voice leading", T_VL, s_vlChord, 5, vlUiIndex (pg->voiceLeadMode));
		if (pg->voiceLeadMode == 4) fNum ("Inversion", T_INV, 0, 6, pg->inversion);
		if (p.tracks[track].type != TRACK_CHORD)		// a legacy chord block on an instrument track: how it plays
		{
			fSection ("HOW IT IS PLAYED");
			fDrop ("Style", T_STYLE, g_styleNames, STYLE_COUNT, pg->style);
			fNum ("Octave", T_OCT, 0, 8, pg->octave);
			fDrop ("Bass", T_BASS, s_bassNames, 3, !pg->bass ? 0 : pg->bassPerBeat ? 2 : 1);
			fDrop ("Climb", T_CLIMB, s_climbNames, 4, pg->climbMode);
			fDrop ("Held note", T_HELD, s_heldNames, 4, pg->heldMode);
			fCheck ("Halve the durations", T_HALVE, pg->halveDurations);
		}
		// the right: the chord on a keyboard, the next chord
		int x = formRight () + 24, w = width - x - 16;
		addSection (x, 14, w, "THE CHORD ON THE KEYBOARD");
		keys = new MiniKeys (x, 40, imin (w - 260, 3 * 7 * 20), 76);
		keys->lowC = 36; keys->octaves = 4;
		addChild (keys);
		int kx = x + keys->width + 16;
		addHint (kx, 104, w - (kx - x), "Listen plays the chord. The chord track is silent: the accompaniments play it.");
		addSection (x, 134, w, "SUGGEST THE NEXT CHORD");
		dropdown (x, 158, 170, T_MOOD, g_moodNames, MOOD_COUNT, mood);
		button (x + 180, 157, 150, "Chain 4 bars", T_CHAIN);
		button (x + 340, 157, 120, "Cadence...", T_CADENCE);
		strip = new SuggestStrip (x, 194, w, 52);
		strip->onPick = pick;
		addChild (strip);
		addHint (x, 254, w, "Click a card: that chord is added after this one. Right-click: this chord becomes it.");
		refresh ();
	}
	const char *title () override { return "Chord"; }
	PatternModule *pat () { return (PatternModule *) module (); }

	void refresh () override
	{
		PatternModule *pg = pat ();
		if (!pg) return;
		const Project &p = g_doc.p;
		keys->clear ();
		int n[16]; int c = chordNotes (pg->root, pg->octave, pg->quality, pg->inversion, pg->openVoicing, n);
		for (int i = 0; i < c; i++) keys->light (n[i]);
		keys->litColour = lighter (funcColour (chordFunction (p.key, imod (pg->root, 12), pg->quality)), 60);
		int prev[3], bar; int np = chordContext (p, track, item, prev, &bar);
		strip->key = p.key;
		strip->list.clear ();
		if (np && prev[np - 1] >= 0) strip->list = suggestNext (prev, np, bar, 4, mood, p.key);
		strip->invalidate (true);
		invalidate (true);
	}
	void drawExtra (Canvas &cv) override
	{
		PatternModule *pg = pat ();
		if (!pg) return;
		const Project &p = g_doc.p;
		int kx = keys->left + keys->width + 16;
		char nm[24], rn[16];
		chordLabel (pg->root, pg->quality, p.key, nm, sizeof nm);
		romanNumeral (p.key, imod (pg->root, 12), pg->quality, pg->degree, rn, sizeof rn);
		int fn = chordFunction (p.key, imod (pg->root, 12), pg->quality);
		static const char *const fnames[4] = { "tonic", "subdominant", "dominant", "other" };
		textL (cv, kx, 38, 30, nm, TEXT, 2);
		box (cv, kx, 70, 56, 26, 5, funcColour (fn));
		textC (cv, kx, 70, 56, 26, rn, 0xFFFFFF, 2);
		textL (cv, kx + 66, 70, 26, fnames[iclamp (fn, 0, 3)], DIM);
	}
	CompiledSong *previewSong () override
	{
		PatternModule *pg = pat ();
		if (!pg) return 0;
		Riff r; r.spq = SPQ; r.lengthSlices = imax (1, pg->beatsPerBar) * SPQ;
		int n[16]; int c = chordNotes (pg->root, pg->octave, pg->quality, pg->inversion, pg->openVoicing, n);
		for (int i = 0; i < c; i++) if (n[i] >= 12 && n[i] < 108) r.notes.push (RiffNote (n[i] - 12, 0, r.lengthSlices));
		return compileRiffPreview (g_doc.p, r, 0, false, SOUND_RATE);
	}
	void chordChanged () { revoiceTrack (trk ()); done (); refresh (); }

	void control (int tag, Widget &w) override
	{
		PatternModule *pg = pat ();
		if (!pg) return;
		const Key &k = g_doc.p.key;
		int v = 0;
		if (tag == T_BEATS || tag == T_INV || tag == T_OCT) v = ((NumericUpDown &) w).value;
		else if (tag == T_OPEN || tag == T_HALVE) v = ((Checkbox &) w).checked;
		else if (tag != T_CHAIN && tag != T_CADENCE && tag != T_PROG) v = ((Dropdown &) w).sel;
		switch (tag)
		{
		case T_DEG:
		{
			begin ();
			int r, q;
			if (choices.trySecondary (v, &r, &q))
			{
				pg->degree = -1; pg->root = r; applyDiatonic (*pg, k);
				if (q >= 0) pg->quality = q;
				syncColourTrio (*pg);
			}
			else { pg->degree = v <= 0 ? -1 : v - 1; applyDiatonic (*pg, k); }
			chordChanged ();
			rebuild ();
		} break;
		case T_ROOT: begin (); pg->root = v; applyDiatonic (*pg, k); chordChanged (); break;
		case T_COLOUR: begin (); pg->diatonicColour = v; applyDiatonic (*pg, k); chordChanged (); break;
		case T_SUSP: begin (); pg->suspension = v; applyDiatonic (*pg, k); chordChanged (); break;
		case T_MODE: begin (); pg->modeOverride = v; applyDiatonic (*pg, k); chordChanged (); break;
		case T_BEATS:
			begin ();
			pg->beatsPerBar = imax (1, v); pg->repeats = 1;
			if (pg->style == CUSTOM_STYLE && trk ().type == TRACK_CHORD) pg->style = 0;	// (a drawn grid's length would win: the chord track plays no style)
			chordChanged ();
			break;
		case T_OPEN: begin (); pg->openVoicing = v != 0; chordChanged (); break;
		case T_VL: begin (); pg->voiceLeadMode = s_vlUiToModel[iclamp (v, 0, 4)]; chordChanged (); rebuild (); break;
		case T_INV: begin (); pg->inversion = imax (0, v); chordChanged (); break;
		case T_STYLE: begin (); pg->style = v; chordChanged (); break;
		case T_OCT: begin (); pg->octave = v; chordChanged (); break;
		case T_BASS: begin (); pg->bass = v > 0; pg->bassPerBeat = v == 2; chordChanged (); break;
		case T_CLIMB: begin (); pg->climbMode = v; chordChanged (); break;
		case T_HELD: begin (); pg->heldMode = v; chordChanged (); break;
		case T_HALVE: begin (); pg->halveDurations = v != 0; chordChanged (); break;
		case T_MOOD: mood = s_mood = v; refresh (); break;
		case T_CHAIN:
		{
			begin ();
			int last = chainProgression (g_doc, track, item, 4);
			done ();
			if (last != item) { g_doc.sel.track = track; g_doc.sel.item = last; }
			rebuild ();
		} break;
		case T_CADENCE: if (s_cadenceDialog) s_cadenceDialog (); break;
		}
	}
	static void (*s_cadenceDialog) ();		// (dialogs.h)
	static void pick (int i, bool replace)
	{
		ChordEditor *e = (ChordEditor *) g_editor;
		if (!e || i < 0 || i >= e->strip->list.size ()) return;
		PatternModule *cur = e->pat ();
		const Key &k = g_doc.p.key;
		Suggestion s = e->strip->list[i];
		e->begin ();
		if (replace) { applySuggestion (*cur, k, cur->degree >= 0, s); revoiceTrack (e->trk ()); }
		else
		{
			PatternModule *pg = newChordLike (cur, g_doc.p.barBeats ());
			applySuggestion (*pg, k, cur->degree >= 0, s);
			int at = insertChordAfter (g_doc, e->track, e->item, pg);
			g_doc.sel.track = e->track; g_doc.sel.item = at;
		}
		e->done ();
		e->rebuild ();
	}
};
int ChordEditor::s_mood = MOOD_AUTO;
void (*ChordEditor::s_cadenceDialog) () = 0;

// ---- the accompaniment (a chord articulation) --------------------------------------------------------------------------
class ArticulationEditor : public Editor
{
public:
	enum { T_CELL = 1, T_TOTAL, T_STYLE, T_OCT, T_BASS, T_BASSBEAT, T_OPEN, T_HALVE, T_VL, T_INV, T_DIR, T_TAB,
		T_RES, T_SEED, T_COPY, T_SAVE, T_CLEAR, T_CUSTOMISE, T_MOCT, T_MANCHOR, T_MRES, T_MCLEAR, T_APPLYALL };
	static int s_tab;
	NoteGrid *grid;
	int nBuiltin;
	int seedSel;

	ArticulationEditor (int l, int t, int w0, int h0, int tr, int it) : Editor (l, t, w0, h0, tr, it), grid (0), nBuiltin (STYLE_COUNT), seedSel (0)
	{
		ArticulationModule *ca = art ();
		Project &p = g_doc.p;
		formStart (16, 10, 290);
		fSection ("HOW THE CHORDS ARE PLAYED");
		fNum ("Cell (beats)", T_CELL, 1, 64, iround (ca->beats));
		fNum ("Length", T_TOTAL, 1, 1024, iround (articulationTotalBeats (*ca)));
		// the built-in styles, then the project's user styles
		const char *names[STYLE_COUNT + 64]; int n = 0;
		for (int i = 0; i < STYLE_COUNT; i++) names[n++] = i == CUSTOM_STYLE ? "Custom (drawn)" : g_styleNames[i];
		for (int i = 0; i < p.userChordStyles.size () && n < STYLE_COUNT + 64; i++) names[n++] = p.userChordStyles[i].name.c ();
		int sel = ca->style;
		if (ca->style == CUSTOM_STYLE && !ca->userStyleName.empty ())
			for (int i = 0; i < p.userChordStyles.size (); i++) if (p.userChordStyles[i].name == ca->userStyleName) sel = STYLE_COUNT + i;
		fDrop ("Style", T_STYLE, strings (names, n), n, sel);
		fNum ("Octave", T_OCT, 0, 8, ca->octave);
		fCheck ("Bass", T_BASS, ca->bass);
		fCheck ("Bass on every beat", T_BASSBEAT, ca->bassPerBeat);
		fDrop ("Open voicing", T_OPEN, s_openNames, 3, ca->openVoicingMode == 0 && ca->openVoicing ? 1 : iclamp (ca->openVoicingMode, 0, 2));
		fCheck ("Halve the durations", T_HALVE, ca->halveDurations);
		fDrop ("Voice leading", T_VL, s_vlArtic, 5, vlUiIndex (ca->voiceLeadMode));
		if (ca->voiceLeadMode == 0) fNum ("Inversion", T_INV, 0, 6, ca->inversion);
		else fDrop ("Tendency", T_DIR, s_dirNames, 3, iclamp (ca->voiceLeadDirection, 0, 2));
		fNote ("It plays the chord track's chord under it, whatever it is: stretch the block over as many chords as you want.", 3);
		// the right: the accompaniment grid / the melodic cell
		int x = formRight () + 24, w = width - x - 16;
		static const char *const tabs[2] = { "Accompaniment", "Melodic cell" };
		Tabs *tb = new Tabs (x, 10, 300, 28, tabs, 2, s_tab, trampoline); tb->tag = T_TAB; addChild (tb);
		int gy = 84, gh = height - gy - 8;
		if (s_tab == 0)
		{
			if (ca->style != CUSTOM_STYLE)
			{
				addHint (x, 52, w, "A built-in style plays the chords. To draw your own, customise it: the style becomes a grid of the chord's voices (bass, 1, 3, 5, 7...), repeated every cell.");
				button (x, 100, 220, "Customise this style", T_CUSTOMISE);
			}
			else
			{
				int spb = ca->custom.spq > 0 ? ca->custom.spq : 4;
				label (x, 51, 76, "Resolution");
				dropdown (x + 78, 50, 100, T_RES, resolutionNames (), 8, resolutionIndex (spb));
				label (x + 190, 51, 70, "Start from");
				dropdown (x + 262, 50, 180, T_SEED, g_styleNames, CUSTOM_STYLE, seedSel);
				button (x + 448, 49, 60, "Copy", T_COPY)->tip = "The grid made from this style";
				int bx = x + 516;
				button (bx, 49, 110, "Save style...", T_SAVE); bx += 118;
				if (!ca->userStyleName.empty ()) { button (bx, 49, 110, "Apply to all", T_APPLYALL)->tip = "Every accompaniment with this style gets this grid"; bx += 118; }
				button (imax (bx, x + w - 64), 49, 64, "Clear", T_CLEAR);
				grid = makeGrid (x, gy, w, gh);
				grid->notes = &ca->custom.notes;
				grid->rows = CUSTOM_VOICE_COUNT; grid->rowH = imax (12, imin (22, (gh - 20) / CUSTOM_VOICE_COUNT));
				grid->labelW = 62; grid->bottomUp = true;
				grid->spb = spb; grid->beatsPerBar = imax (1, p.barBeats ());
				grid->cols = imax (1, iround (ca->beats)) * spb;
				grid->drawLen = imax (1, spb / 2); grid->snapCols = 1;
				grid->rowLabel = voiceLabel; grid->noteColour = GREEN;
				grid->fitWidth ();
			}
		}
		else
		{
			int spb = ca->melodic.spq > 0 ? ca->melodic.spq : 4;
			label (x, 51, 56, "Octave");
			number (x + 58, 50, 70, T_MOCT, 1, 8, ca->melodicOctave);
			label (x + 144, 51, 110, "Degree 1 on the");
			static const char *const anch[2] = { "Chord's root", "Inversion's bass" };
			dropdown (x + 256, 50, 150, T_MANCHOR, anch, 2, iclamp (ca->melodicAnchor, 0, 1));
			label (x + 422, 51, 76, "Resolution");
			dropdown (x + 500, 50, 110, T_MRES, resolutionNames (), 8, resolutionIndex (spb));
			button (x + w - 70, 49, 70, "Clear", T_MCLEAR);
			grid = makeGrid (x, gy, w, gh);
			grid->notes = &ca->melodic.notes;
			grid->rows = MELODIC_ROW_COUNT; grid->rowH = 16;
			grid->labelW = 44; grid->bottomUp = true;
			grid->spb = spb; grid->beatsPerBar = imax (1, p.barBeats ());
			grid->cols = imax (1, iround (articulationTotalBeats (*ca))) * spb;
			grid->drawLen = imax (1, spb / 2);
			grid->rowLabel = melLabel; grid->noteColour = BLUE_NOTE;
			grid->fitWidth ();
			grid->centreRow (7);
		}
	}
	const char *title () override { return "Accompaniment"; }
	ArticulationModule *art () { return (ArticulationModule *) module (); }
	static const char *voiceLabel (NoteGrid &g, int r) { (void) g; return r >= 0 && r < CUSTOM_VOICE_COUNT ? g_customVoiceNames[r] : ""; }
	static const char *melLabel (NoteGrid &g, int r) { (void) g; return r >= 0 && r < MELODIC_ROW_COUNT ? s_melRows[r] : ""; }

	int chordLen ()
	{
		int root, q; chordUnder (g_doc.p, startBeat (), lenBeats (), &root, &q);
		int n[16]; return imax (1, chordNotes (root, 4, q, 0, false, n));
	}
	void gridEdited (NoteGrid &g) override
	{
		ArticulationModule *ca = art ();
		if (s_tab == 0) ca->custom.setNotes (ca->custom.notes, g.spb, g.cols);
		else ca->melodic.setNotes (ca->melodic.notes, g.spb, g.cols);
		done ();
	}
	void control (int tag, Widget &w) override
	{
		ArticulationModule *ca = art ();
		Project &p = g_doc.p;
		if (!ca) return;
		int v = 0;
		switch (tag)
		{
		case T_CELL: case T_TOTAL: case T_OCT: case T_INV: case T_MOCT: v = ((NumericUpDown &) w).value; break;
		case T_BASS: case T_BASSBEAT: case T_HALVE: v = ((Checkbox &) w).checked; break;
		case T_TAB: v = ((Tabs &) w).sel; break;
		case T_COPY: case T_SAVE: case T_CLEAR: case T_CUSTOMISE: case T_MCLEAR: case T_APPLYALL: break;
		default: v = ((Dropdown &) w).sel; break;
		}
		switch (tag)
		{
		case T_CELL: begin (); ca->beats = imax (1, v); done (); rebuild (); break;
		case T_TOTAL: begin (); ca->lengthBeats = imax (1, v); done (); rebuild (); break;
		case T_STYLE:
			begin ();
			if (v < STYLE_COUNT) { ca->style = v; ca->userStyleName = ""; }
			else
			{
				int u = v - STYLE_COUNT;
				if (u < p.userChordStyles.size ())
				{
					const UserStyle &us = p.userChordStyles[u];
					ca->style = CUSTOM_STYLE; ca->userStyleName = us.name;
					ca->custom.setNotes (userStyleNotes (us), us.spb, us.beats * us.spb);
					if (us.beats > 0) ca->beats = us.beats;
				}
			}
			done (); rebuild ();
			break;
		case T_OCT: begin (); ca->octave = v; done (); break;
		case T_BASS: begin (); ca->bass = v != 0; done (); break;
		case T_BASSBEAT: begin (); ca->bassPerBeat = v != 0; done (); break;
		case T_OPEN: begin (); ca->openVoicingMode = v; ca->openVoicing = v == 1; done (); break;
		case T_HALVE: begin (); ca->halveDurations = v != 0; done (); break;
		case T_VL: begin (); ca->voiceLeadMode = s_vlUiToModel[iclamp (v, 0, 4)]; done (); rebuild (); break;
		case T_INV: begin (); ca->inversion = imax (0, v); done (); break;
		case T_DIR: begin (); ca->voiceLeadDirection = v; done (); break;
		case T_TAB: s_tab = v; rebuild (); break;
		case T_CUSTOMISE:
		{
			begin ();
			int old = ca->style;
			Vec<RiffNote> n = voiceNotesForCustom (old, imax (1, iround (ca->beats)), chordLen (), 4);
			ca->style = CUSTOM_STYLE; ca->userStyleName = "";
			ca->custom.setNotes (n, 4, imax (1, iround (ca->beats)) * 4);
			done (); rebuild ();
		} break;
		case T_RES:
		{
			begin ();
			int from = ca->custom.spq > 0 ? ca->custom.spq : 4, to = resolutionAt (v), cols = imax (1, iround (ca->beats)) * to;
			rescale (ca->custom.notes, from, to, cols);
			ca->custom.setNotes (ca->custom.notes, to, cols);
			done (); rebuild ();
		} break;
		case T_SEED: seedSel = v; break;
		case T_COPY:
		{
			begin ();
			int spb = ca->custom.spq > 0 ? ca->custom.spq : 4, beats = imax (1, iround (ca->beats));
			Vec<RiffNote> n = voiceNotesForCustom (seedSel, beats, chordLen (), spb);
			ca->custom.setNotes (n, spb, beats * spb);
			done (); rebuild ();
		} break;
		case T_SAVE:
		{
			char name[64]; snprintf (name, sizeof name, "%s", ca->userStyleName.empty () ? "My style" : ca->userStyleName.c ());
			if (!s_prompt || !s_prompt ("Save the accompaniment style", "Its name (in this song's style list):", name, sizeof name) || !name[0]) break;
			begin ();
			int spb = ca->custom.spq > 0 ? ca->custom.spq : 4;
			saveUserStyle (p.userChordStyles, name, ca->custom.notes, spb, imax (1, iround (ca->beats)));
			ca->userStyleName = name;
			done (); rebuild ();
		} break;
		case T_APPLYALL:			// every accompaniment using this user style gets this grid
		{
			begin ();
			int spb = ca->custom.spq > 0 ? ca->custom.spq : 4;
			saveUserStyle (p.userChordStyles, ca->userStyleName, ca->custom.notes, spb, imax (1, iround (ca->beats)));
			for (int t = 0; t < p.tracks.size (); t++)
				for (int i = 0; i < p.tracks[t].items.size (); i++)
				{
					Module *m = p.tracks[t].items[i].module;
					if (!m || m == ca || m->kind != M_ARTICULATION) continue;
					ArticulationModule *o = (ArticulationModule *) m;
					if (o->userStyleName == ca->userStyleName) { o->style = CUSTOM_STYLE; o->custom = ca->custom; o->beats = ca->beats; }
				}
			done ();
		} break;
		case T_CLEAR: begin (); ca->custom.notes.clear (); ca->custom.setNotes (ca->custom.notes, ca->custom.spq > 0 ? ca->custom.spq : 4, imax (1, iround (ca->beats)) * (ca->custom.spq > 0 ? ca->custom.spq : 4)); done (); rebuild (); break;
		case T_MOCT: begin (); ca->melodicOctave = v; done (); break;
		case T_MANCHOR: begin (); ca->melodicAnchor = v; done (); break;
		case T_MRES:
		{
			begin ();
			int from = ca->melodic.spq > 0 ? ca->melodic.spq : 4, to = resolutionAt (v), cols = imax (1, iround (articulationTotalBeats (*ca))) * to;
			rescale (ca->melodic.notes, from, to, cols);
			ca->melodic.setNotes (ca->melodic.notes, to, cols);
			done (); rebuild ();
		} break;
		case T_MCLEAR:
		{
			begin ();
			int spb = ca->melodic.spq > 0 ? ca->melodic.spq : 4;
			ca->melodic.notes.clear (); ca->melodic.slices.clear (); ca->melodic.hasNotes = false; ca->melodic.spq = spb;
			done (); rebuild ();
		} break;
		}
	}
	// a text prompt (dialogs.h): title, prompt, the text (in / out) -> OK
	static bool (*s_prompt) (const char *title, const char *prompt, char *text, int cap);
};
int ArticulationEditor::s_tab = 0;
bool (*ArticulationEditor::s_prompt) (const char *, const char *, char *, int) = 0;

// ---- the legacy cadence block --------------------------------------------------------------------------------------------
class CadenceEditor : public Editor
{
public:
	enum { T_CSTYLE = 1, T_START, T_MEAS, T_CPM, T_REGEN, T_STYLE, T_OCT, T_VL, T_OPEN, T_BASS, T_CLIMB, T_HELD, T_HALVE };
	CadenceEditor (int l, int t, int w0, int h0, int tr, int it) : Editor (l, t, w0, h0, tr, it)
	{
		CadenceModule *cm = cad ();
		formStart (16, 10, 290);
		fSection ("THE CADENCE (press Regenerate)");
		fDrop ("Cadence", T_CSTYLE, g_cadenceStyles, CADENCE_STYLE_COUNT, cm->cadenceStyle);
		static const char *const degs[7] = { "I (tonic)", "ii", "iii", "IV", "V", "vi", "vii" };
		fDrop ("From degree", T_START, degs, 7, iclamp (cm->startDegree, 0, 6));
		fNum ("Bars", T_MEAS, 1, 64, cm->measures);
		fNum ("Chords / bar", T_CPM, 1, 8, cm->chordsPerMeasure);
		fButton ("Regenerate a variant", T_REGEN);
		fSection ("HOW IT IS PLAYED");
		fDrop ("Rhythm", T_STYLE, g_styleNames, STYLE_COUNT, iclamp (cm->style, 0, STYLE_COUNT - 1));
		fNum ("Octave", T_OCT, 0, 8, cm->octave);
		static const char *const vl[4] = { "None", "Auto", "Close to the bass", "Close at the top" };
		fDrop ("Voice leading", T_VL, vl, 4, iclamp (cm->voiceLeadMode, 0, 3));
		fCheck ("Open voicing (spread)", T_OPEN, cm->openVoicing);
		fDrop ("Bass", T_BASS, s_bassNames, 3, !cm->bass ? 0 : cm->bassPerBeat ? 2 : 1);
		fDrop ("Climb", T_CLIMB, s_climbNames, 4, cm->climbMode);
		fDrop ("Held note", T_HELD, s_heldNames, 4, cm->heldMode);
		fCheck ("Halve the durations", T_HALVE, cm->halveDurations);
	}
	const char *title () override { return "Cadence"; }
	CadenceModule *cad () { return (CadenceModule *) module (); }
	void drawExtra (Canvas &cv) override
	{
		CadenceModule *cm = cad ();
		const Key &k = g_doc.p.key;
		int x = formRight () + 24, w = width - x - 16;
		sectionAt (cv, x, 14, w, "ITS CHORDS");
		int cx = x, cy = 40;
		for (int i = 0; i < cm->chords.size (); i++)
		{
			const CadenceChord &c = cm->chords[i];
			char nm[24], rn[16]; chordLabel (c.root, c.quality, k, nm, sizeof nm); romanNumeral (k, imod (c.root, 12), c.quality, c.degree, rn, sizeof rn);
			int cw = 110;
			if (cx + cw > x + w) { cx = x; cy += 64; }
			unsigned fc = funcColour (chordFunction (k, imod (c.root, 12), c.quality));
			gbox (cv, cx, cy, cw, 56, 6, fc, darker (fc, 40));
			textL (cv, cx + 8, cy + 4, 20, nm, 0xFFFFFF, 2);
			textC (cv, cx, cy + 24, cw, 28, rn, 0xFFFFFF, 2);
			cx += cw + 8;
		}
		if (!cm->chords.size ()) textL (cv, x, 40, 24, "(empty: press Regenerate)", FAINT);
		wrapText (cv, x, cy + 72, w, "A cadence block is Koton's older way; the chord track's \"Cadence...\" writes the chords on the chord track, where every accompaniment reads them.", FAINT, 17);
	}
	void control (int tag, Widget &w) override
	{
		CadenceModule *cm = cad ();
		int v = 0;
		if (tag == T_MEAS || tag == T_CPM || tag == T_OCT) v = ((NumericUpDown &) w).value;
		else if (tag == T_OPEN || tag == T_HALVE) v = ((Checkbox &) w).checked;
		else if (tag != T_REGEN) v = ((Dropdown &) w).sel;
		switch (tag)
		{
		case T_CSTYLE: cm->cadenceStyle = v; break;			// (applies at Regenerate)
		case T_START: cm->startDegree = v; break;
		case T_MEAS: cm->measures = imax (1, v); break;
		case T_CPM: cm->chordsPerMeasure = imax (1, v); break;
		case T_REGEN: begin (); regenCadence (g_doc.p, *cm, (int) kapi_get_ticks ()); done (); invalidate (true); break;
		case T_STYLE: begin (); cm->style = v; done (); break;
		case T_OCT: begin (); cm->octave = v; done (); break;
		case T_VL: begin (); cm->voiceLeadMode = v; revoiceCadence (g_doc.p.key, *cm); done (); invalidate (true); break;
		case T_OPEN: begin (); cm->openVoicing = v != 0; done (); break;
		case T_BASS: begin (); cm->bass = v > 0; cm->bassPerBeat = v == 2; done (); break;
		case T_CLIMB: begin (); cm->climbMode = v; done (); break;
		case T_HELD: begin (); cm->heldMode = v; done (); break;
		case T_HALVE: begin (); cm->halveDurations = v != 0; done (); break;
		}
	}
};

} // namespace kui

#endif
