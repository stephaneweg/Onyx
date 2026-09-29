//
// ui/ed_rhythm.h -- the rhythm editors, as Koton Studio's: the DRUMS (a catalog motif -- the built-in
// grooves and the shipped ones -- or a drawn one: a grid of the 47 GM percussion lanes; a euclidean
// rhythm written on one lane, a lane shifted), the MELODIC LINE (the rhythm of up to three voices,
// drawn; the pitches are the engine's, from the harmony, shaped by a contour, an anchor, a tension
// slope...) and the three POLYRHYTHM blocks (drum rings, melodic rings, poly chords): the rings
// drawn as a wheel -- click a ring to pick it, a step to set / clear a hit --, the list of rings and
// the picked ring's settings.
//
#ifndef _koton_ed_rhythm_h
#define _koton_ed_rhythm_h

#include "ui/ed_chord.h"

namespace kui {

static const char *const s_unitNames[3] = { "Eighths", "Sixteenths", "Eighth triplets" };
static const int s_unitSlices[3] = { 12, 6, 8 };		// at 24 slices a beat

// a drum lane's family colour (Koton's DrumColors)
static inline unsigned laneColour (int lane)
{
	switch (lane)
	{
	case 0: case 12: return 0xE2604C;				// kicks
	case 1: case 5: case 6: case 13: return 0xE8A04A;		// snares, rim, clap
	case 2: case 3: case 4: return 0xE2CE5C;			// hi-hats
	case 7: case 8: case 9: case 14: case 15: case 16: return 0x7CC86E;	// toms
	case 10: case 11: case 17: case 18: case 19: case 20: case 21: return 0x6CB4E8;	// cymbals
	}
	return 0xB08CE0;						// percussion
}

// the preview line of a euclidean rhythm: "x.x..x.. tresillo - on the beats: 2"
static inline void euclidPreview (int k, int n, int rot, int unit, char *b, int cap)
{
	Vec<bool> pat; euclidPattern (k, n, rot, pat);
	int o = 0, onBeat = 0, st = s_unitSlices[iclamp (unit, 0, 2)];
	for (int i = 0; i < pat.size () && o < cap - 48; i++)
	{
		o += snprintf (b + o, cap - o, "%s", pat[i] ? "\xE2\x97\x8F" : "\xC2\xB7");	// ● ·
		if (pat[i] && (i * st) % SPQ == 0) onBeat++;
	}
	const char *nm = euclidName (pat);
	snprintf (b + o, cap - o, "%s%s%s   on the beats: %d", nm ? "  " : "", nm ? nm : "", "", onBeat);
}

// ---- the drums ---------------------------------------------------------------------------------------------------------
class DrumEditor : public Editor
{
public:
	enum { T_CAT = 1, T_MOTIF, T_CUSTOMISE, T_DENSITY, T_FILL, T_BEATS, T_REPEATS, T_RES, T_SAVE, T_CLEAR,
		T_ELANE, T_EUNIT, T_EK, T_EN, T_EROT, T_EAPPLY, T_ELEFT, T_ERIGHT };
	static int s_lane, s_unit, s_k, s_n, s_rot;
	NoteGrid *grid;
	Vec<RiffNote> m_preview;
	int nCats;
	char m_eu[240];

	DrumEditor (int l, int t, int w0, int h0, int tr, int it) : Editor (l, t, w0, h0, tr, it), grid (0), nCats (0)
	{
		DrumModule *dp = drum ();
		Project &p = g_doc.p;
		g_drumCatalog.load ();
		nCats = g_drumCatalog.cats.size ();
		// the category and the motif shown
		const char *cat, *motif;
		if (!dp->catCategory.empty ()) { cat = dp->catCategory; motif = dp->catMotif; }
		else if (dp->style == DRUM_CUSTOM_STYLE) { cat = CUSTOM_CAT; motif = CUSTOM_CAT; }
		else { cat = STANDARD_CAT; motif = g_drumStyleNames[iclamp (dp->style, 0, DRUM_STYLE_COUNT - 1)]; }
		const char *cn[80]; int nc = 0, csel = 0;
		for (int i = 0; i < nCats && nc < 79; i++) { if (g_drumCatalog.cats[i].name == cat) csel = nc; cn[nc++] = catDisplayName (g_drumCatalog.cats[i].name); }
		if (!strcmp (cat, CUSTOM_CAT)) csel = nc;
		cn[nc++] = "Custom (drawn)";
		const char *mn[160]; int nm = 0, msel = 0;
		if (csel == nCats)
		{
			mn[nm++] = "Custom (drawn)";
			for (int i = 0; i < p.userDrumStyles.size () && nm < 159; i++) { if (p.userDrumStyles[i].name == motif) msel = nm; mn[nm++] = p.userDrumStyles[i].name.c (); }
		}
		else
		{
			const DrumCategory &c = g_drumCatalog.cats[csel];
			for (int i = 0; i < c.motifs.size () && nm < 159; i++) { if (c.motifs[i].name == motif) msel = nm; mn[nm++] = c.motifs[i].name.c (); }
		}
		formStart (16, 10, 290);
		fSection ("THE GROOVE");
		fDrop ("Category", T_CAT, strings (cn, nc), nc, csel);
		fDrop ("Motif", T_MOTIF, strings (mn, nm), nm, msel);
		if (!drumIsCustom (*dp)) fButton ("Customise (draw it)", T_CUSTOMISE);
		fDrop ("Density", T_DENSITY, g_densityNames, 4, iclamp (dp->density, 0, 3));
		fCheck ("A fill on the last bar", T_FILL, dp->fillLast);
		fNum ("Beats / bar", T_BEATS, 1, 32, dp->beatsPerBar);
		fNum ("Repeats", T_REPEATS, 1, 256, dp->repeats);
		fNote ("The kit is the track's: choose it in the track header.", 2);
		// the right: the bars, the grid
		int x = formRight () + 24, w = width - x - 16;
		bool custom = drumIsCustom (*dp);
		int spb = custom ? (dp->custom.spq > 0 ? dp->custom.spq : SPQ) : SPQ;
		if (custom)
		{
			label (x, 11, 76, "Resolution");
			dropdown (x + 78, 10, 110, T_RES, resolutionNames (), 8, resolutionIndex (spb));
			button (x + 200, 9, 130, "Save motif...", T_SAVE);
			button (x + w - 70, 9, 70, "Clear", T_CLEAR);
		}
		else addHint (x, 12, w - 10, "A catalog motif (shown below). Customise it to draw on it.");
		// the euclidean tool (one lane)
		int ey = 44;
		label (x, ey + 1, 52, "Euclid");
		dropdown (x + 54, ey, 150, T_ELANE, g_laneNames, DRUM_LANES, s_lane);
		label (x + 214, ey + 1, 14, "E(");
		number (x + 230, ey, 64, T_EK, 0, 32, s_k);
		label (x + 298, ey + 1, 8, ",");
		number (x + 306, ey, 64, T_EN, 1, 32, s_n);
		label (x + 374, ey + 1, 30, ") rot");
		number (x + 408, ey, 64, T_EROT, -32, 32, s_rot);
		dropdown (x + 480, ey, 140, T_EUNIT, s_unitNames, 3, s_unit);
		button (x + 628, ey - 1, 70, "Apply", T_EAPPLY);
		Button *b = button (x + 708, ey - 1, 34, "<", T_ELEFT); b->tip = "Shift this lane a step earlier";
		b = button (x + 746, ey - 1, 34, ">", T_ERIGHT); b->tip = "Shift this lane a step later";
		euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu);
		int gy = 102, gh = height - gy - 8;
		grid = makeGrid (x, gy, w, gh);
		grid->rows = DRUM_LANES; grid->rowH = 16; grid->labelW = 120;
		grid->oneShot = true; grid->rowLabel = laneLabel; grid->rowColour = laneRowColour;
		grid->spb = spb; grid->beatsPerBar = imax (1, p.barBeats ());
		grid->snapCols = spb >= 8 ? spb / 4 : 1;
		grid->onAudition = audition;
		if (custom) { grid->notes = &dp->custom.notes; grid->cols = drumUnit (*dp); }
		else
		{
			if (dp->custom.notes.size ()) { m_preview = dp->custom.notes; grid->spb = dp->custom.spq > 0 ? dp->custom.spq : SPQ; grid->cols = drumUnit (*dp); }
			else { m_preview = laneNotesForStyle (dp->style, imax (1, dp->beatsPerBar)); grid->cols = imax (1, dp->beatsPerBar) * SPQ; }
			grid->notes = &m_preview; grid->readOnly = true;
		}
		grid->fitWidth ();
	}
	const char *title () override { return "Drums"; }
	DrumModule *drum () { return (DrumModule *) module (); }
	static const char *laneLabel (NoteGrid &g, int r) { (void) g; return r >= 0 && r < DRUM_LANES ? g_laneNames[r] : ""; }
	static unsigned laneRowColour (NoteGrid &g, int r) { (void) g; return laneColour (r); }
	static void audition (NoteGrid &g, int row, bool on)
	{
		Editor *e = (Editor *) g.ctx;
		if (!on) return;
		const Track &t = e->trk ();
		int prog = (t.drumKit >= 0 && t.drumKit < g_kitPrograms.size ()) ? g_kitPrograms[t.drumKit] : 0;
		g_audio.noteOn (keyForLane (row), 100, prog, true);
	}
	void drawExtra (Canvas &cv) override
	{
		int x = formRight () + 24;
		textL (cv, x + 54, 72, 24, m_eu, ACC);
	}
	void gridEdited (NoteGrid &g) override
	{
		DrumModule *dp = drum ();
		dp->custom.setNotes (dp->custom.notes, g.spb, g.cols);
		done ();
	}
	void control (int tag, Widget &w) override
	{
		DrumModule *dp = drum ();
		Project &p = g_doc.p;
		int v = 0;
		switch (tag)
		{
		case T_BEATS: case T_REPEATS: case T_EK: case T_EN: case T_EROT: v = ((NumericUpDown &) w).value; break;
		case T_FILL: v = ((Checkbox &) w).checked; break;
		case T_CUSTOMISE: case T_SAVE: case T_CLEAR: case T_EAPPLY: case T_ELEFT: case T_ERIGHT: break;
		default: v = ((Dropdown &) w).sel; break;
		}
		switch (tag)
		{
		case T_CAT:
		{
			begin ();
			if (v >= nCats) applyDrumCatalog (p, *dp, CUSTOM_CAT, 0);
			else
			{
				const DrumCategory &c = g_drumCatalog.cats[v];
				applyDrumCatalog (p, *dp, c.name, c.motifs.size () ? c.motifs[0].name.c () : "");
			}
			done (); rebuild ();
		} break;
		case T_MOTIF:
		{
			begin ();
			const char *cat = dp->catCategory.empty () ? STANDARD_CAT : dp->catCategory.c ();
			Str catS (cat);
			if (catS == CUSTOM_CAT) applyDrumCatalog (p, *dp, CUSTOM_CAT, v == 0 ? CUSTOM_CAT : p.userDrumStyles[v - 1].name.c ());
			else
			{
				const DrumMotif *m = 0;
				for (int i = 0; i < nCats; i++) if (g_drumCatalog.cats[i].name == catS && v < g_drumCatalog.cats[i].motifs.size ()) m = &g_drumCatalog.cats[i].motifs[v];
				if (m) { Str mn (m->name); applyDrumCatalog (p, *dp, catS, mn); }
			}
			done (); rebuild ();
		} break;
		case T_CUSTOMISE: begin (); customizeDrum (*dp); done (); rebuild (); break;
		case T_DENSITY: begin (); dp->density = v; done (); break;
		case T_FILL: begin (); dp->fillLast = v != 0; done (); break;
		case T_BEATS: begin (); dp->beatsPerBar = imax (1, v); done (); rebuild (); break;
		case T_REPEATS: begin (); dp->repeats = imax (1, v); done (); break;
		case T_RES:
		{
			begin ();
			int from = dp->custom.spq > 0 ? dp->custom.spq : SPQ, to = resolutionAt (v);
			int cols = imax (1, iround (drumUnit (*dp) * (double) to / from));
			rescale (dp->custom.notes, from, to, cols);
			dp->custom.setNotes (dp->custom.notes, to, cols);
			done (); rebuild ();
		} break;
		case T_SAVE:
		{
			char name[64]; snprintf (name, sizeof name, "%s", dp->catMotif.empty () || dp->catMotif == CUSTOM_CAT ? "My groove" : dp->catMotif.c ());
			if (!ArticulationEditor::s_prompt || !ArticulationEditor::s_prompt ("Save the drum motif", "Its name (in this song's Custom motifs):", name, sizeof name) || !name[0]) break;
			begin ();
			int spb = dp->custom.spq > 0 ? dp->custom.spq : SPQ;
			saveUserStyle (p.userDrumStyles, name, dp->custom.notes, spb, imax (1, drumUnit (*dp) / spb));
			dp->catCategory = CUSTOM_CAT; dp->catMotif = name;
			done (); rebuild ();
		} break;
		case T_CLEAR: begin (); dp->custom.notes.clear (); dp->custom.setNotes (dp->custom.notes, dp->custom.spq > 0 ? dp->custom.spq : SPQ, drumUnit (*dp)); done (); rebuild (); break;
		case T_ELANE: s_lane = v; break;
		case T_EUNIT: s_unit = v; euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu); invalidate (true); break;
		case T_EK: s_k = v; euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu); invalidate (true); break;
		case T_EN: s_n = imax (1, v); euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu); invalidate (true); break;
		case T_EROT: s_rot = v; euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu); invalidate (true); break;
		case T_EAPPLY: begin (); applyEuclideanDrum (*dp, s_lane, s_k, s_n, s_rot, s_unitSlices[s_unit]); done (); rebuild (); break;
		case T_ELEFT: case T_ERIGHT: begin (); rotateDrumLane (*dp, s_lane, (tag == T_ELEFT ? -1 : 1) * s_unitSlices[s_unit]); done (); rebuild (); break;
		}
	}
};
int DrumEditor::s_lane = 0, DrumEditor::s_unit = 0, DrumEditor::s_k = 3, DrumEditor::s_n = 8, DrumEditor::s_rot = 0;

// ---- the melodic line ----------------------------------------------------------------------------------------------------
class MelodicLineEditor : public Editor
{
public:
	enum { T_VOICES = 1, T_BEATS, T_CONTOUR, T_ANCHOR, T_VAR, T_PRESERVE, T_MOTIF, T_APPLYALL, T_CONT, T_TENSION, T_AMP, T_ORN, T_WAVE,
		T_RES, T_SAVE, T_CLEAR, T_EVOICE, T_EUNIT, T_EK, T_EN, T_EROT, T_EAPPLY, T_ELEFT, T_ERIGHT };
	static int s_voice, s_unit, s_k, s_n, s_rot;
	NoteGrid *grid;
	int nSaved;
	char m_eu[240];

	MelodicLineEditor (int l, int t, int w0, int h0, int tr, int it) : Editor (l, t, w0, h0, tr, it), grid (0), nSaved (0)
	{
		MelodicLineModule *ml = line ();
		Project &p = g_doc.p;
		static const char *const voices[3] = { "1 voice", "2 voices", "3 voices" };
		formStart (16, 10, 290);
		fSection ("THE LINE (you draw its rhythm)");
		fDrop ("Voices", T_VOICES, voices, 3, iclamp (ml->voiceCount - 1, 0, 2));
		fNum ("Beats", T_BEATS, 1, 1024, ml->beatsPerBar);
		fDrop ("Contour", T_CONTOUR, g_contourNames, 9, iclamp (ml->contour, 0, 8));
		fDrop ("Anchor", T_ANCHOR, g_anchorNames, 6, iclamp (ml->anchor, 0, 5));
		fDrop ("Variation", T_VAR, g_variationNames, 5, iclamp (ml->variation, 0, 4));
		const char *mn[80]; int nm = 0, msel = -1;
		nSaved = imin (79, p.userMelodicLines.size ());
		for (int i = 0; i < nSaved; i++) { if (p.userMelodicLines[i].name == ml->lineName) msel = i; mn[nm++] = p.userMelodicLines[i].name.c (); }
		mn[nm++] = "Custom";
		fDrop ("Motif", T_MOTIF, strings (mn, nm), nm, msel >= 0 ? msel : nSaved);
		if (!ml->lineName.empty ()) fButton ("Apply to the lines with this motif", T_APPLYALL);
		fCheck ("Preserve (kept by Apply)", T_PRESERVE, ml->preserve);
		fSection ("ITS SHAPE");
		fSlider ("Continuity", T_CONT, 0, 100, ml->continuity);
		fSlider ("Tension", T_TENSION, -12, 12, ml->tensionSlope);
		fSlider ("Amplitude", T_AMP, 2, 24, ml->amplitude);
		fSlider ("Ornaments", T_ORN, 0, 100, ml->ornaments);
		fSlider ("Wave", T_WAVE, 0, 32, ml->waveLength);
		fNote ("Draw only the RHYTHM: the engine picks the pitches from the harmony (chord tones on the strong beats).", 3);
		int x = formRight () + 24, w = width - x - 16;
		int spb = ml->rhythm.spq > 0 ? ml->rhythm.spq : 4;
		label (x, 11, 76, "Resolution");
		dropdown (x + 78, 10, 110, T_RES, resolutionNames (), 8, resolutionIndex (spb));
		button (x + 200, 9, 130, "Save motif...", T_SAVE);
		button (x + w - 70, 9, 70, "Clear", T_CLEAR);
		static const char *const vn[3] = { "Voice 1", "Voice 2", "Voice 3" };
		int ey = 44;
		label (x, ey + 1, 52, "Euclid");
		dropdown (x + 54, ey, 100, T_EVOICE, vn, 3, s_voice);
		label (x + 164, ey + 1, 14, "E(");
		number (x + 180, ey, 64, T_EK, 0, 32, s_k);
		label (x + 248, ey + 1, 8, ",");
		number (x + 256, ey, 64, T_EN, 1, 32, s_n);
		label (x + 324, ey + 1, 30, ") rot");
		number (x + 358, ey, 64, T_EROT, -32, 32, s_rot);
		dropdown (x + 430, ey, 140, T_EUNIT, s_unitNames, 3, s_unit);
		button (x + 578, ey - 1, 70, "Apply", T_EAPPLY);
		button (x + 658, ey - 1, 34, "<", T_ELEFT);
		button (x + 696, ey - 1, 34, ">", T_ERIGHT);
		euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu);
		int gy = 102, gh = height - gy - 8;
		grid = makeGrid (x, gy, w, imin (gh, 3 * 34 + 40));
		grid->notes = &ml->rhythm.notes;
		grid->rows = iclamp (ml->voiceCount, 1, 3); grid->rowH = 34; grid->labelW = 80;
		grid->rowLabel = voiceLabel; grid->noteColour = 0xE0708A;
		grid->spb = spb; grid->beatsPerBar = imax (1, p.barBeats ());
		grid->cols = imax (1, ml->beatsPerBar) * spb;
		grid->drawLen = imax (1, spb / 2); grid->snapCols = spb >= 8 ? spb / 4 : 1;
		grid->fitWidth ();
	}
	const char *title () override { return "Melodic line"; }
	MelodicLineModule *line () { return (MelodicLineModule *) module (); }
	static const char *voiceLabel (NoteGrid &g, int r) { (void) g; static const char *const v[3] = { "Voice 1", "Voice 2", "Voice 3" }; return r >= 0 && r < 3 ? v[r] : ""; }
	void drawExtra (Canvas &cv) override { textL (cv, formRight () + 24 + 54, 72, 24, m_eu, ACC); }
	void gridEdited (NoteGrid &g) override
	{
		MelodicLineModule *ml = line ();
		ml->rhythm.setNotes (ml->rhythm.notes, g.spb, g.cols);
		done ();
	}
	void control (int tag, Widget &w) override
	{
		MelodicLineModule *ml = line ();
		Project &p = g_doc.p;
		int v = 0;
		switch (tag)
		{
		case T_BEATS: case T_EK: case T_EN: case T_EROT: v = ((NumericUpDown &) w).value; break;
		case T_PRESERVE: v = ((Checkbox &) w).checked; break;
		case T_CONT: case T_TENSION: case T_AMP: case T_ORN: case T_WAVE: v = ((Slider &) w).value; break;
		case T_APPLYALL: case T_SAVE: case T_CLEAR: case T_EAPPLY: case T_ELEFT: case T_ERIGHT: break;
		default: v = ((Dropdown &) w).sel; break;
		}
		switch (tag)
		{
		case T_VOICES: begin (); ml->voiceCount = v + 1; done (); rebuild (); break;
		case T_BEATS: begin (); ml->beatsPerBar = imax (1, v); done (); rebuild (); break;
		case T_CONTOUR: begin (); ml->contour = v; done (); break;
		case T_ANCHOR: begin (); ml->anchor = v; done (); break;
		case T_VAR: begin (); ml->variation = v; done (); break;
		case T_PRESERVE: begin (); ml->preserve = v != 0; done (); break;
		case T_CONT: begin (); ml->continuity = v; done (); break;
		case T_TENSION: begin (); ml->tensionSlope = v; done (); break;
		case T_AMP: begin (); ml->amplitude = v; done (); break;
		case T_ORN: begin (); ml->ornaments = v; done (); break;
		case T_WAVE: begin (); ml->waveLength = v; done (); break;
		case T_MOTIF:
			begin ();
			if (v >= nSaved) ml->lineName = "";
			else
			{
				const UserStyle &u = p.userMelodicLines[v];
				ml->lineName = u.name;
				ml->rhythm.setNotes (userStyleNotes (u), u.spb, u.beats * u.spb);
				ml->beatsPerBar = imax (1, u.beats);
				int mv = 0; for (int i = 0; i < ml->rhythm.notes.size (); i++) mv = imax (mv, ml->rhythm.notes[i].note);
				ml->voiceCount = iclamp (imax (ml->voiceCount, mv + 1), 1, 3);
			}
			done (); rebuild ();
			break;
		case T_APPLYALL:
			begin ();
			for (int t = 0; t < p.tracks.size (); t++)
				for (int i = 0; i < p.tracks[t].items.size (); i++)
				{
					Module *m = p.tracks[t].items[i].module;
					if (!m || m == ml || m->kind != M_MELODICLINE) continue;
					MelodicLineModule *o = (MelodicLineModule *) m;
					if (o->lineName == ml->lineName && !o->preserve) { o->rhythm = ml->rhythm; o->voiceCount = ml->voiceCount; o->beatsPerBar = ml->beatsPerBar; }
				}
			done ();
			break;
		case T_RES:
		{
			begin ();
			int from = ml->rhythm.spq > 0 ? ml->rhythm.spq : 4, to = resolutionAt (v), cols = imax (1, ml->beatsPerBar) * to;
			rescale (ml->rhythm.notes, from, to, cols);
			ml->rhythm.setNotes (ml->rhythm.notes, to, cols);
			done (); rebuild ();
		} break;
		case T_SAVE:
		{
			char name[64]; snprintf (name, sizeof name, "%s", ml->lineName.empty () ? "My line" : ml->lineName.c ());
			if (!ArticulationEditor::s_prompt || !ArticulationEditor::s_prompt ("Save the melodic motif", "Its name (in this song's motifs):", name, sizeof name) || !name[0]) break;
			begin ();
			saveUserStyle (p.userMelodicLines, name, ml->rhythm.notes, ml->rhythm.spq > 0 ? ml->rhythm.spq : 4, imax (1, ml->beatsPerBar));
			ml->lineName = name;
			done (); rebuild ();
		} break;
		case T_CLEAR: begin (); ml->rhythm.notes.clear (); ml->rhythm.setNotes (ml->rhythm.notes, ml->rhythm.spq > 0 ? ml->rhythm.spq : 4, imax (1, ml->beatsPerBar) * (ml->rhythm.spq > 0 ? ml->rhythm.spq : 4)); done (); rebuild (); break;
		case T_EVOICE: s_voice = v; break;
		case T_EUNIT: s_unit = v; euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu); invalidate (true); break;
		case T_EK: s_k = v; euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu); invalidate (true); break;
		case T_EN: s_n = imax (1, v); euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu); invalidate (true); break;
		case T_EROT: s_rot = v; euclidPreview (s_k, s_n, s_rot, s_unit, m_eu, sizeof m_eu); invalidate (true); break;
		case T_EAPPLY: begin (); applyEuclideanMelodic (*ml, s_voice, s_k, s_n, s_rot, s_unitSlices[s_unit]); done (); rebuild (); break;
		case T_ELEFT: case T_ERIGHT: begin (); rotateMelodicVoice (*ml, s_voice, (tag == T_ELEFT ? -1 : 1) * s_unitSlices[s_unit]); done (); rebuild (); break;
		}
	}
};
int MelodicLineEditor::s_voice = 0, MelodicLineEditor::s_unit = 0, MelodicLineEditor::s_k = 3, MelodicLineEditor::s_n = 8, MelodicLineEditor::s_rot = 0;

// ---- the rings ----------------------------------------------------------------------------------------------------------------
// One ring per layer, the outer one first: its steps as dots, its hits filled in the layer's colour.
struct RingData { int steps; Vec<bool> pat; bool muted; unsigned colour; };

class RingsView : public Widget
{
public:
	Vec<RingData> rings;
	int sel;
	double phase;			// the playhead's turn (0..1, < 0: none)
	void (*onPick) (int ring);
	void (*onToggle) (int ring, int step);
	RingsView (int l, int t, int w, int h) : Widget (l, t, w, h), sel (0), phase (-1), onPick (0), onToggle (0) {}
	int cx () const { return width / 2; }
	int cy () const { return height / 2; }
	int rMax () const { return imin (width, height) / 2 - 14; }
	int gap () const { int n = imax (1, rings.size ()); return imax (10, imin (26, (rMax () - 24) / n)); }
	int radius (int i) const { return rMax () - i * gap (); }
	static void polar (int cx, int cy, int r, double turn, int *x, int *y)
	{
		double a = turn * 6.283185307179586 - 1.5707963267948966;
		*x = cx + (int) floor (r * cos (a) + 0.5); *y = cy + (int) floor (r * sin (a) + 0.5);
	}
	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (parent ? parent->bgColor () : PANEL);
		disc (cv, cx (), cy (), rMax () + 10, LANE);
		for (int i = 0; i < rings.size (); i++)
		{
			const RingData &rd = rings[i];
			int r = radius (i);
			unsigned c = rd.muted ? mixc (rd.colour, LANE, 170) : rd.colour;
			ringArc (cv, cx (), cy (), r, 0, 360, i == sel ? 2 : 1, i == sel ? lighter (c, 40) : mixc (c, LANE, 150));
			int n = imax (1, rd.steps);
			// the hits joined (the polygon)
			int px0 = -1, py0 = -1, fx = -1, fy = -1;
			for (int k = 0; k < n; k++)
			{
				if (k >= rd.pat.size () || !rd.pat[k]) continue;
				int x, y; polar (cx (), cy (), r, k / (double) n, &x, &y);
				if (px0 >= 0) aline (cv, px0, py0, x, y, 1, c, 110); else { fx = x; fy = y; }
				px0 = x; py0 = y;
			}
			if (px0 >= 0 && fx >= 0 && (px0 != fx || py0 != fy)) aline (cv, px0, py0, fx, fy, 1, c, 110);
			for (int k = 0; k < n; k++)
			{
				int x, y; polar (cx (), cy (), r, k / (double) n, &x, &y);
				bool on = k < rd.pat.size () && rd.pat[k];
				if (on) disc (cv, x, y, i == sel ? 6 : 5, c);
				else disc (cv, x, y, 2, i == sel ? DIM : FAINT);
			}
		}
		if (phase >= 0)
		{
			int x, y; polar (cx (), cy (), rMax () + 6, phase, &x, &y);
			aline (cv, cx (), cy (), x, y, 2, PLAY, 200);
		}
		disc (cv, cx (), cy (), 4, DIM);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel;
		if (m_b.edge (bl, 0) != 1) return true;
		double dx = mx - cx (), dy = my - cy (), d = sqrt (dx * dx + dy * dy);
		int best = -1; double bd = 1e9;
		for (int i = 0; i < rings.size (); i++) { double e = fabs (d - radius (i)); if (e < bd) { bd = e; best = i; } }
		if (best < 0 || bd > gap () * 0.6) return true;
		if (best != sel) { sel = best; if (onPick) onPick (best); invalidate (true); return true; }
		// on the picked ring: the nearest step toggled
		int n = imax (1, rings[best].steps);
		double turn = atan2 (dy, dx) / 6.283185307179586 + 0.25; if (turn < 0) turn += 1;
		int k = ((int) floor (turn * n + 0.5)) % n;
		if (onToggle) onToggle (best, k);
		return true;
	}
private:
	Buttons m_b;
};

// the list of rings: a row each (its colour, name, E(k,n), muted)
class RingList : public Widget
{
public:
	enum { ROW = 26 };
	Vec<Str> names;
	Vec<RingData> *rings;
	int sel;
	void (*onPick) (int ring);
	void (*onMute) (int ring);
	RingList (int l, int t, int w, int h) : Widget (l, t, w, h), rings (0), sel (0), onPick (0), onMute (0) {}
	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (parent ? parent->bgColor () : PANEL);
		box (cv, 0, 0, width, height, 6, FIELD);
		for (int i = 0; i < names.size (); i++)
		{
			int y = 3 + i * ROW;
			if (y + ROW > height) break;
			const RingData &rd = (*rings)[i];
			if (i == sel) box (cv, 3, y, width - 6, ROW - 2, 4, PANEL2);
			disc (cv, 16, y + ROW / 2 - 1, 5, rd.muted ? FAINT : rd.colour);
			textFit (cv, 28, y, width - 110, ROW - 2, names[i], rd.muted ? FAINT : TEXT);
			int k = 0; for (int s = 0; s < rd.pat.size (); s++) if (rd.pat[s]) k++;
			char b[24]; snprintf (b, sizeof b, "E(%d,%d)", k, rd.steps);
			textR (cv, width - 40, y, ROW - 2, b, DIM);
			box (cv, width - 32, y + 4, 24, ROW - 10, 4, rd.muted ? REC : FACE);
			textC (cv, width - 32, y + 4, 24, ROW - 10, "M", rd.muted ? 0xFFFFFF : DIM);
		}
		if (!names.size ()) textC (cv, 0, 0, width, 30, "(no ring: add one)", FAINT);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel;
		if (m_b.edge (bl, 0) != 1) return true;
		int i = (my - 3) / ROW;
		if (i < 0 || i >= names.size ()) return true;
		if (mx >= width - 34) { if (onMute) onMute (i); return true; }
		if (onPick) onPick (i);
		return true;
	}
private:
	Buttons m_b;
};

// ---- the three polyrhythm blocks -------------------------------------------------------------------------------------------------
class PolyEditor : public Editor
{
public:
	enum { T_BEATS = 1, T_REPEATS, T_ADD, T_REMOVE, T_LANE, T_ACCENT, T_HITS, T_STEPS, T_ROT, T_MUTE, T_VOICE, T_OCT, T_LEGATO, T_TONE, T_CONTOUR, T_SEED,
		T_PMODE, T_RESTART, T_TOTAL, T_POCT, T_OPEN, T_VLA, T_MONO, T_STRAT, T_MSEED, T_AVOID,
		T_CADD, T_CREMOVE, T_CDEG, T_CROOT, T_CCOL, T_CSUSP, T_CMODE, T_CBEATS, T_EUCLID };
	static int s_sel, s_chord;
	RingsView *view;
	RingList *list;
	DegreeChoices choices;
	int kind;

	PolyEditor (int l, int t, int w0, int h0, int tr, int it) : Editor (l, t, w0, h0, tr, it), view (0), list (0), kind (module ()->kind)
	{
		Module *m = module ();
		formStart (16, 10, 290);
		if (kind == M_POLYDRUM)
		{
			PolyDrumModule *pd = (PolyDrumModule *) m;
			fSection ("POLYRHYTHM (drum rings)");
			fNum ("Cycle (beats)", T_BEATS, 1, 64, pd->beats);
			fNum ("Repeats", T_REPEATS, 1, 256, pd->repeats);
			fNote ("Each ring is a lane playing E(hits, steps) over the same cycle: 3 against 4, 5 against 8... they meet again at the cycle's end.", 4);
		}
		else if (kind == M_MELODICPOLY)
		{
			MelodicPolyModule *mp = (MelodicPolyModule *) m;
			fSection ("MELODIC RINGS");
			fNum ("Cycle (beats)", T_BEATS, 1, 64, mp->beats);
			fNum ("Repeats", T_REPEATS, 1, 256, mp->repeats);
			fNote ("Each ring gives the rhythm of a voice; the pitches come from the harmony, as a melodic line's.", 3);
		}
		else
		{
			PolyChordModule *pc = (PolyChordModule *) m;
			choices.build (g_doc.p.key);
			static const char *const modes[2] = { "One ring per chord tone", "One ring sweeping the tones" };
			static const char *const restarts[6] = { "Nearest", "Lowest", "Highest", "Root", "Third", "Fifth" };
			static const char *const vla[3] = { "Auto (least motion)", "Close to the bass", "Close at the top" };
			static const char *const strat[4] = { "Highest note", "Lowest note", "Auto (voice leading)", "Random" };
			fSection ("POLY CHORDS");
			fDrop ("Mode", T_PMODE, modes, 2, pc->mode);
			fDrop ("On a new chord", T_RESTART, restarts, 6, iclamp (pc->restart, 0, 5));
			fNum ("Length (beats)", T_TOTAL, 1, 1024, iround (polyChordTotalBeats (*pc)));
			fNum ("Cycle (beats)", T_BEATS, 1, 64, pc->cycleBeats);
			fNum ("Octave", T_POCT, 1, 7, pc->octave);
			fCheck ("Open voicing", T_OPEN, pc->openVoicing);
			fDrop ("Voice leading", T_VLA, vla, 3, iclamp (pc->voiceLeadAnchor, 0, 2));
			fCheck ("Emergent melody (one ring at a time)", T_MONO, pc->monodicPick);
			if (pc->monodicPick)
			{
				fDrop ("Strategy", T_STRAT, strat, 4, iclamp (pc->monodicStrategy, 0, 3));
				fNum ("Seed", T_MSEED, 0, 99999, pc->monodicSeed);
				fCheck ("Avoid repeated notes", T_AVOID, pc->monodicAvoidRepeat);
			}
			formColumn ();
			fSection ("ITS CHORDS");
			s_chord = iclamp (s_chord, 0, imax (0, pc->chords.size () - 1));
			rowY (60);						// (the chips: drawn in drawExtra)
			if (pc->chords.size ())
			{
				PolyChordItem &c = pc->chords[s_chord];
				fDrop ("Degree", T_CDEG, choices.ptrs, choices.n, choices.indexOf (c.degree, imod (c.root, 12), c.quality));
				Dropdown *rd = fDrop ("Root", T_CROOT, g_rootNames, 12, imod (c.root, 12)); rd->disabled = c.degree >= 0;
				fDrop ("Colour", T_CCOL, g_colourNames, 5, c.diatonicColour);
				fDrop ("Suspension", T_CSUSP, g_suspensionNames, 3, c.suspension);
				fDrop ("Force", T_CMODE, g_modeOverrideNames, 6, c.modeOverride);
				fNum ("Beats", T_CBEATS, 1, 64, c.beats);
			}
			fButtons ("Add a chord", T_CADD, "Remove it", T_CREMOVE);
		}
		// the wheel and the list of rings
		int x = formRight () + 24, avail = width - x - 16;
		int ws = imin (height - 16, imin (340, avail / 2));
		view = new RingsView (x, 8, ws, ws);
		view->onPick = pickRing; view->onToggle = toggleStep;
		addChild (view);
		int lx = x + ws + 20, lw = imin (360, width - lx - 16);
		addSection (lx, 12, lw, "RINGS");
		list = new RingList (lx, 34, lw, 4 + 5 * RingList::ROW);
		list->onPick = pickRing; list->onMute = muteRing;
		addChild (list);
		int by = 34 + list->height + 8;
		button (lx, by, (lw - 8) / 2, "Add a ring", T_ADD);
		button (lx + (lw - 8) / 2 + 8, by, (lw - 8) / 2, "Remove it", T_REMOVE);
		// the picked ring's settings
		fill ();
		int n = ringCount ();
		s_sel = iclamp (s_sel, 0, imax (0, n - 1));
		if (n)
		{
			// (beside the list when there is room, else under it)
			int cy = by + 36, cx = lx, cw = lw;
			if (lx + lw + 20 + 280 <= width - 8) { cx = lx + lw + 20; cw = imin (320, width - cx - 16); cy = 34; addSection (cx, 12, cw, "THE PICKED RING"); }
			int hw = (cw - 8) / 2;
			label (cx, cy + 1, 40, "Hits"); number (cx + 40, cy, hw - 40, T_HITS, 0, 64, ringHits (s_sel));
			label (cx + hw + 8, cy + 1, 40, "Steps"); number (cx + hw + 48, cy, hw - 40, T_STEPS, 1, 64, ringSteps (s_sel));
			cy += ROW_H;
			label (cx, cy + 1, 40, "Rot."); number (cx + 40, cy, hw - 40, T_ROT, -64, 64, ringRot (s_sel));
			if (kind == M_POLYDRUM)
			{
				EuclidLayer &L = ((PolyDrumModule *) m)->layers[s_sel];
				check (cx + hw + 8, cy + 1, hw, "Muted", T_MUTE, L.muted);
				cy += ROW_H;
				label (cx, cy + 1, 50, "Lane"); dropdown (cx + 50, cy, cw - 50, T_LANE, g_laneNames, DRUM_LANES, iclamp (L.lane, 0, DRUM_LANES - 1));
				cy += ROW_H;
				const char *acc[DRUM_LANES + 1]; acc[0] = "No accent";
				for (int i = 0; i < DRUM_LANES; i++) acc[i + 1] = g_laneNames[i];
				label (cx, cy + 1, 50, "Accent"); dropdown (cx + 50, cy, cw - 50, T_ACCENT, strings (acc, DRUM_LANES + 1), DRUM_LANES + 1, L.accentLane + 1);
			}
			else if (kind == M_MELODICPOLY)
			{
				EuclidVoice &V = ((MelodicPolyModule *) m)->layers[s_sel];
				check (cx + hw + 8, cy + 1, hw, "Muted", T_MUTE, V.muted);
				cy += ROW_H;
				static const char *const vn[3] = { "Voice 1", "Voice 2", "Voice 3" };
				label (cx, cy + 1, 40, "Voice"); dropdown (cx + 40, cy, hw - 40, T_VOICE, vn, 3, iclamp (V.voice, 0, 2));
				label (cx + hw + 8, cy + 1, 50, "Octave"); number (cx + hw + 58, cy, hw - 50, T_OCT, -3, 3, V.octave);
				cy += ROW_H;
				check (cx, cy + 1, hw, "Legato", T_LEGATO, V.legato);
			}
			else
			{
				PolyChordModule *pc = (PolyChordModule *) m;
				EuclidChordLayer &C = pc->layers[s_sel];
				check (cx + hw + 8, cy + 1, hw, "Muted", T_MUTE, C.muted);
				cy += ROW_H;
				if (pc->mode == PC_ONE_RING_PER_TONE) { label (cx, cy + 1, 40, "Tone"); number (cx + 40, cy, hw - 40, T_TONE, 0, 7, C.toneIndex); }
				else
				{
					static const char *const cn[6] = { "Wave", "Rising", "Falling", "Static", "Zigzag", "Random" };
					label (cx, cy + 1, 56, "Contour"); dropdown (cx + 56, cy, hw - 56, T_CONTOUR, cn, 6, iclamp (C.contour, 0, 5));
				}
				label (cx + hw + 8, cy + 1, 50, "Octave"); number (cx + hw + 58, cy, hw - 50, T_OCT, -3, 3, C.octave);
				cy += ROW_H;
				check (cx, cy + 1, hw, "Legato", T_LEGATO, C.legato);
				if (pc->mode != PC_ONE_RING_PER_TONE) { label (cx + hw + 8, cy + 1, 40, "Seed"); number (cx + hw + 48, cy, hw - 40, T_SEED, 0, 9999, C.randomSeed); }
			}
			cy += ROW_H + 4;
			addHint (cx, cy, cw, "Click a ring to pick it; on the picked ring, click a step to set or clear a hit.");
		}
	}
	const char *title () override { return kind == M_POLYDRUM ? "Polyrhythm" : kind == M_MELODICPOLY ? "Melodic rings" : "Poly chords"; }

	int ringCount ()
	{
		Module *m = module ();
		return kind == M_POLYDRUM ? ((PolyDrumModule *) m)->layers.size () : kind == M_MELODICPOLY ? ((MelodicPolyModule *) m)->layers.size () : ((PolyChordModule *) m)->layers.size ();
	}
	// the shared fields of a ring, whatever its kind
	struct RingRef { int *hits, *steps, *rotation; bool *muted, *customMode; Vec<int> *customHits; };
	RingRef ref (int i)
	{
		Module *m = module (); RingRef r;
		if (kind == M_POLYDRUM) { EuclidLayer &L = ((PolyDrumModule *) m)->layers[i]; r.hits = &L.hits; r.steps = &L.steps; r.rotation = &L.rotation; r.muted = &L.muted; r.customMode = &L.customMode; r.customHits = &L.customHits; }
		else if (kind == M_MELODICPOLY) { EuclidVoice &L = ((MelodicPolyModule *) m)->layers[i]; r.hits = &L.hits; r.steps = &L.steps; r.rotation = &L.rotation; r.muted = &L.muted; r.customMode = &L.customMode; r.customHits = &L.customHits; }
		else { EuclidChordLayer &L = ((PolyChordModule *) m)->layers[i]; r.hits = &L.hits; r.steps = &L.steps; r.rotation = &L.rotation; r.muted = &L.muted; r.customMode = &L.customMode; r.customHits = &L.customHits; }
		return r;
	}
	int ringHits (int i) { return *ref (i).hits; }
	int ringSteps (int i) { return *ref (i).steps; }
	int ringRot (int i) { return *ref (i).rotation; }

	void fill ()
	{
		Module *m = module ();
		view->rings.clear (); list->names.clear ();
		int n = ringCount ();
		for (int i = 0; i < n; i++)
		{
			RingRef r = ref (i);
			RingData &d = view->rings.add ();
			effectivePattern (*r.customMode, *r.customHits, *r.hits, *r.steps, *r.rotation, d.pat);
			d.steps = d.pat.size (); d.muted = *r.muted; d.colour = trackColour (i + 1);
			char b[64];
			if (kind == M_POLYDRUM) { snprintf (b, sizeof b, "%s", g_laneNames[iclamp (((PolyDrumModule *) m)->layers[i].lane, 0, DRUM_LANES - 1)]); }
			else if (kind == M_MELODICPOLY) snprintf (b, sizeof b, "Voice %d", ((MelodicPolyModule *) m)->layers[i].voice + 1);
			else
			{
				PolyChordModule *pc = (PolyChordModule *) m;
				if (pc->mode == PC_ONE_RING_PER_TONE) snprintf (b, sizeof b, "Tone %d", pc->layers[i].toneIndex + 1);
				else snprintf (b, sizeof b, "Sweep %d", i + 1);
			}
			if (*r.customMode) { size_t l = strlen (b); snprintf (b + l, sizeof b - l, " (drawn)"); }
			list->names.push (Str (b));
		}
		list->rings = &view->rings;
		view->sel = list->sel = iclamp (s_sel, 0, imax (0, n - 1));
		view->invalidate (true); list->invalidate (true);
	}
	void tick () override
	{
		// the playhead's turn when the song plays this block
		double ph = -1;
		if (g_audio.isPlaying ())
		{
			double b = g_audio.playheadBeat () - startBeat (), len = lenBeats ();
			int cyc = 4;
			Module *m = module ();
			if (kind == M_POLYDRUM) cyc = imax (1, ((PolyDrumModule *) m)->beats > 0 ? ((PolyDrumModule *) m)->beats : ((PolyDrumModule *) m)->beatsPerBar);
			else if (kind == M_MELODICPOLY) cyc = imax (1, ((MelodicPolyModule *) m)->beats > 0 ? ((MelodicPolyModule *) m)->beats : ((MelodicPolyModule *) m)->beatsPerBar);
			else cyc = imax (1, ((PolyChordModule *) m)->cycleBeats);
			if (b >= 0 && b < len) ph = fmod (b, cyc) / cyc;
		}
		if (fabs (ph - view->phase) > 0.002) { view->phase = ph; view->invalidate (true); }
	}
	void drawExtra (Canvas &cv) override
	{
		if (kind != M_POLYCHORD) return;
		PolyChordModule *pc = (PolyChordModule *) module ();
		const Key &k = g_doc.p.key;
		// the chord chips under "ITS CHORDS" (the second column: x = 16 + 290 + 16)
		int x0 = 16 + 290 + 16, x = x0, y = 36;
		m_chipN = 0;
		for (int i = 0; i < pc->chords.size () && m_chipN < 32; i++)
		{
			const PolyChordItem &c = pc->chords[i];
			char nm[24]; chordLabel (c.root, c.quality, k, nm, sizeof nm);
			char b[32]; snprintf (b, sizeof b, "%s  %d", nm, c.beats);
			int w = tw (b) + 16;
			if (x + w > x0 + 290) { x = x0; y += 28; }
			if (y > 36 + 28) break;
			unsigned fc = funcColour (chordFunction (k, imod (c.root, 12), c.quality));
			box (cv, x, y, w, 24, 5, i == s_chord ? fc : mixc (fc, PANEL, 150));
			textC (cv, x, y, w, 24, b, 0xFFFFFF);
			m_chip[m_chipN].x = x; m_chip[m_chipN].y = y; m_chip[m_chipN].w = w; m_chipN++;
			x += w + 6;
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		if (kind == M_POLYCHORD && m_cb.edge (bl, 0) == 1)
			for (int i = 0; i < m_chipN; i++)
				if (mx >= m_chip[i].x && mx < m_chip[i].x + m_chip[i].w && my >= m_chip[i].y && my < m_chip[i].y + 24) { s_chord = i; rebuild (); return true; }
		return Editor::onMouse (mx, my, bl, br, bm, wheel);
	}
	static void pickRing (int i) { PolyEditor *e = (PolyEditor *) g_editor; s_sel = i; e->rebuild (); }
	static void muteRing (int i)
	{
		PolyEditor *e = (PolyEditor *) g_editor;
		e->begin (); bool *m = e->ref (i).muted; *m = !*m; e->done ();
		e->fill ();
	}
	static void toggleStep (int i, int k)
	{
		PolyEditor *e = (PolyEditor *) g_editor;
		RingRef r = e->ref (i);
		e->begin ();
		if (!*r.customMode)			// drawn from now on: its current hits kept
		{
			Vec<bool> pat; effectivePattern (false, *r.customHits, *r.hits, *r.steps, *r.rotation, pat);
			r.customHits->clear ();
			for (int s = 0; s < pat.size (); s++) if (pat[s]) r.customHits->push (s);
			*r.customMode = true;
		}
		int at = r.customHits->indexOf (k);
		if (at >= 0) r.customHits->removeAt (at); else { r.customHits->push (k); r.customHits->sort ([] (int a, int b) { return a < b; }); }
		*r.hits = r.customHits->size ();
		e->done ();
		e->fill ();
	}
	void control (int tag, Widget &w) override
	{
		Module *m = module ();
		int v = 0;
		switch (tag)
		{
		case T_BEATS: case T_REPEATS: case T_HITS: case T_STEPS: case T_ROT: case T_OCT: case T_TONE: case T_SEED: case T_TOTAL: case T_POCT: case T_MSEED: case T_CBEATS:
			v = ((NumericUpDown &) w).value; break;
		case T_MUTE: case T_LEGATO: case T_OPEN: case T_MONO: case T_AVOID: v = ((Checkbox &) w).checked; break;
		case T_ADD: case T_REMOVE: case T_CADD: case T_CREMOVE: break;
		default: v = ((Dropdown &) w).sel; break;
		}
		begin ();
		bool rb = false;
		switch (tag)
		{
		case T_BEATS:
			if (kind == M_POLYDRUM) ((PolyDrumModule *) m)->beats = imax (1, v);
			else if (kind == M_MELODICPOLY) ((MelodicPolyModule *) m)->beats = imax (1, v);
			else ((PolyChordModule *) m)->cycleBeats = imax (1, v);
			break;
		case T_REPEATS:
			if (kind == M_POLYDRUM) ((PolyDrumModule *) m)->repeats = imax (1, v);
			else if (kind == M_MELODICPOLY) ((MelodicPolyModule *) m)->repeats = imax (1, v);
			break;
		case T_ADD:
			if (kind == M_POLYDRUM) { EuclidLayer L; L.lane = 2; L.hits = 3; L.steps = 8; ((PolyDrumModule *) m)->layers.push (L); s_sel = ((PolyDrumModule *) m)->layers.size () - 1; }
			else if (kind == M_MELODICPOLY) { EuclidVoice V; V.voice = 0; ((MelodicPolyModule *) m)->layers.push (V); s_sel = ((MelodicPolyModule *) m)->layers.size () - 1; }
			else { EuclidChordLayer C; C.toneIndex = ((PolyChordModule *) m)->layers.size (); ((PolyChordModule *) m)->layers.push (C); s_sel = ((PolyChordModule *) m)->layers.size () - 1; }
			rb = true; break;
		case T_REMOVE:
			if (ringCount ())
			{
				if (kind == M_POLYDRUM) ((PolyDrumModule *) m)->layers.removeAt (s_sel);
				else if (kind == M_MELODICPOLY) ((MelodicPolyModule *) m)->layers.removeAt (s_sel);
				else ((PolyChordModule *) m)->layers.removeAt (s_sel);
				s_sel = imax (0, s_sel - 1);
			}
			rb = true; break;
		case T_HITS: { RingRef r = ref (s_sel); *r.hits = imax (0, v); *r.customMode = false; } break;
		case T_STEPS: { RingRef r = ref (s_sel); *r.steps = imax (1, v); *r.customMode = false; } break;
		case T_ROT: { RingRef r = ref (s_sel); *r.rotation = v; *r.customMode = false; } break;
		case T_MUTE: *ref (s_sel).muted = v != 0; break;
		case T_LANE: ((PolyDrumModule *) m)->layers[s_sel].lane = v; break;
		case T_ACCENT: ((PolyDrumModule *) m)->layers[s_sel].accentLane = v - 1; break;
		case T_VOICE: ((MelodicPolyModule *) m)->layers[s_sel].voice = v; break;
		case T_OCT: if (kind == M_MELODICPOLY) ((MelodicPolyModule *) m)->layers[s_sel].octave = v; else ((PolyChordModule *) m)->layers[s_sel].octave = v; break;
		case T_LEGATO: if (kind == M_MELODICPOLY) ((MelodicPolyModule *) m)->layers[s_sel].legato = v != 0; else ((PolyChordModule *) m)->layers[s_sel].legato = v != 0; break;
		case T_TONE: ((PolyChordModule *) m)->layers[s_sel].toneIndex = v; break;
		case T_CONTOUR: ((PolyChordModule *) m)->layers[s_sel].contour = v; break;
		case T_SEED: ((PolyChordModule *) m)->layers[s_sel].randomSeed = v; break;
		case T_PMODE: ((PolyChordModule *) m)->mode = v; rb = true; break;
		case T_RESTART: ((PolyChordModule *) m)->restart = v; break;
		case T_TOTAL: ((PolyChordModule *) m)->beats = imax (1, v); break;
		case T_POCT: ((PolyChordModule *) m)->octave = v; break;
		case T_OPEN: ((PolyChordModule *) m)->openVoicing = v != 0; break;
		case T_VLA: ((PolyChordModule *) m)->voiceLeadAnchor = v; break;
		case T_MONO: ((PolyChordModule *) m)->monodicPick = v != 0; rb = true; break;
		case T_STRAT: ((PolyChordModule *) m)->monodicStrategy = v; break;
		case T_MSEED: ((PolyChordModule *) m)->monodicSeed = v; break;
		case T_AVOID: ((PolyChordModule *) m)->monodicAvoidRepeat = v != 0; break;
		case T_CADD:
		{
			PolyChordModule *pc = (PolyChordModule *) m;
			PolyChordItem c;
			if (pc->chords.size ()) { c = pc->chords[iclamp (s_chord, 0, pc->chords.size () - 1)]; c.degree = c.degree >= 0 ? (c.degree + 3) % 7 : -1; }
			else c.degree = 0;
			if (c.degree >= 0) { RootQ q = diatonicChord (g_doc.p.key, c.degree, c.diatonicColour, c.suspension, c.modeOverride); c.root = q.root; c.quality = q.quality; }
			pc->chords.insert (pc->chords.size () ? s_chord + 1 : 0, c);
			s_chord = pc->chords.size () > 1 ? s_chord + 1 : 0;
			rb = true;
		} break;
		case T_CREMOVE: { PolyChordModule *pc = (PolyChordModule *) m; if (pc->chords.size ()) { pc->chords.removeAt (s_chord); s_chord = imax (0, s_chord - 1); } rb = true; } break;
		case T_CDEG: case T_CROOT: case T_CCOL: case T_CSUSP: case T_CMODE: case T_CBEATS:
		{
			PolyChordModule *pc = (PolyChordModule *) m;
			if (!pc->chords.size ()) break;
			PolyChordItem &c = pc->chords[s_chord];
			const Key &k = g_doc.p.key;
			if (tag == T_CDEG)
			{
				int r, q;
				if (choices.trySecondary (v, &r, &q)) { c.degree = -1; c.root = r; if (q >= 0) c.quality = q; }
				else c.degree = v <= 0 ? -1 : v - 1;
				rb = true;
			}
			else if (tag == T_CROOT) c.root = v;
			else if (tag == T_CCOL) c.diatonicColour = v;
			else if (tag == T_CSUSP) c.suspension = v;
			else if (tag == T_CMODE) c.modeOverride = v;
			else c.beats = imax (1, v);
			if (tag != T_CBEATS)
			{
				if (c.degree >= 0) { RootQ q = diatonicChord (k, c.degree, c.diatonicColour, c.suspension, c.modeOverride); c.root = q.root; c.quality = q.quality; }
				else if (tag != T_CDEG) c.quality = qualityForColour (c.diatonicColour, c.suspension, c.modeOverride);
			}
			invalidate (true);
		} break;
		}
		done ();
		if (rb) rebuild (); else fill ();
	}
private:
	Buttons m_cb;
	struct Chip { int x, y, w; } m_chip[32];
	int m_chipN = 0;
};
int PolyEditor::s_sel = 0, PolyEditor::s_chord = 0;

} // namespace kui

#endif
