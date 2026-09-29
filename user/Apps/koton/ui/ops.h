//
// ui/ops.h -- the editors' operations on the model, ported from Koton Studio's UI helpers
// (TimelineHelper.cs, ChordDegrees.cs's ChordDegreeChoices, TimelineScreen's cadence helpers):
// the drum catalog (Data/drums/catalog.json + the built-in grooves as the "Standard" category), a
// catalog motif applied to a drum block, "Customise", euclidean rhythms written on one drum lane or
// one voice of a melodic line and rotated, the degree vocabulary of the chord editors (the seven
// degrees, then the secondary dominants V/x), a cadence's chords voice-led, the user styles saved
// in the project.
//
#ifndef _koton_ops_h
#define _koton_ops_h

#include "ui/doc.h"

namespace kui {

// ---- the drum catalog -------------------------------------------------------------------------------------
static const char *const CUSTOM_CAT = "Personnalisé";		// Koton's name, saved in the files (CatCategory)
static const char *const STANDARD_CAT = "Standard";

struct DrumMotif { Str name; int spq, beats; Vec<RiffNote> notes; int lengthSlices () const { return imax (1, beats) * imax (1, spq); } };
struct DrumCategory { Str name; Vec<DrumMotif> motifs; };

struct DrumCatalog
{
	Vec<DrumCategory> cats;
	bool loaded;
	DrumCatalog () : loaded (false) {}
	void load ()
	{
		if (loaded) return;
		loaded = true;
		// the built-in grooves first (one 4/4 bar each), as Koton does
		DrumCategory &std = cats.add ();
		std.name = STANDARD_CAT;
		for (int i = 0; i < DRUM_STYLE_COUNT - 1; i++)
		{
			DrumMotif &m = std.motifs.add ();
			m.name = g_drumStyleNames[i]; m.spq = SPQ; m.beats = 4; m.notes = laneNotesForStyle (i, 4);
		}
		// then the catalog shipped with the app
		static const char *const paths[] = { "SD:/apps/koton.app/drums.json", "SD:/koton/drums.json" };
		for (unsigned k = 0; k < sizeof paths / sizeof paths[0]; k++)
		{
			void *f = kapi_open (paths[k]);
			if (!f) continue;
			unsigned n = kapi_fsize (f);
			char *buf = (char *) malloc (n + 1);
			int got = buf ? kapi_read (f, buf, n) : 0;
			kapi_close (f);
			if (!buf) return;
			if (got < 0) got = 0;
			json::Doc d;
			if (d.parse (buf, (unsigned long) got, json::TOLERANT))
			{
				const json::Value &cs = d.root ()["categories"];
				for (const json::Value *c = cs.first (); c; c = c->next)
				{
					DrumCategory &dc = cats.add ();
					dc.name = (*c)["name"].asStr ("?");
					for (const json::Value *m = (*c)["motifs"].first (); m; m = m->next)
					{
						DrumMotif &dm = dc.motifs.add ();
						dm.name = (*m)["name"].asStr ("?");
						dm.spq = imax (1, (*m)["spq"].asInt (4)); dm.beats = imax (1, (*m)["beats"].asInt (4));
						for (const json::Value *t = (*m)["notes"].first (); t; t = t->next)
							if (t->count >= 3) dm.notes.push (RiffNote ((*t)[0].asInt (0), (*t)[1].asInt (0), (*t)[2].asInt (1)));
					}
				}
			}
			free (buf);
			return;
		}
	}
	const DrumMotif *find (const char *cat, const char *name) const
	{
		for (int i = 0; i < cats.size (); i++)
			if (!strcasecmp (cats[i].name, cat))
				for (int j = 0; j < cats[i].motifs.size (); j++) if (!strcasecmp (cats[i].motifs[j].name, name)) return &cats[i].motifs[j];
		return 0;
	}
};
extern DrumCatalog g_drumCatalog;

// the shown name of a category (the files keep Koton's French ones)
static inline const char *catDisplayName (const char *c)
{
	if (!strcmp (c, CUSTOM_CAT)) return "Custom";
	if (!strcmp (c, "Afrique")) return "Africa";
	if (!strcmp (c, "Australie")) return "Australia";
	if (!strcmp (c, "Amérique latine")) return "Latin America";
	return c;
}

static inline bool drumIsCustom (const DrumModule &dp)
{
	return dp.catCategory == CUSTOM_CAT || (dp.catCategory.empty () && dp.style == DRUM_CUSTOM_STYLE);
}

// a catalog motif (or a saved project motif, category "Personnalisé") applied to a drum block
static inline void applyDrumCatalog (Project &p, DrumModule &dp, const char *category, const char *motif)
{
	if (!category) return;
	dp.catCategory = category; dp.catMotif = motif ? motif : "";
	if (!strcmp (category, CUSTOM_CAT))
	{
		dp.style = DRUM_CUSTOM_STYLE;
		if (motif && motif[0] && strcmp (motif, CUSTOM_CAT))
		{
			for (int i = 0; i < p.userDrumStyles.size (); i++)
				if (p.userDrumStyles[i].name == motif)
				{
					const UserStyle &u = p.userDrumStyles[i];
					int spb = u.spb > 0 ? u.spb : SPQ, beats = imax (1, u.beats);
					int oldTotal = imax (1, dp.beatsPerBar) * imax (1, dp.repeats);
					dp.beatsPerBar = beats;
					dp.repeats = imax (1, iround (oldTotal / (double) beats));
					dp.custom.setNotes (u.notes, spb, beats * spb);
					return;
				}
			return;
		}
		if (dp.custom.notes.size () == 0) { Vec<RiffNote> none; dp.custom.setNotes (none, SPQ, dp.beatsPerBar * SPQ); }
		return;
	}
	const DrumMotif *m = g_drumCatalog.find (category, dp.catMotif);
	if (!m) return;
	dp.style = DRUM_CUSTOM_STYLE;
	int beats = imax (1, m->beats), oldTotal = imax (1, dp.beatsPerBar) * imax (1, dp.repeats);
	dp.beatsPerBar = beats;
	dp.repeats = imax (1, iround (oldTotal / (double) beats));
	dp.custom.setNotes (m->notes, m->spq, m->lengthSlices ());
}

// "Customise": the current motif copied into an editable one
static inline void customizeDrum (DrumModule &dp)
{
	const DrumMotif *m = g_drumCatalog.find (dp.catCategory, dp.catMotif);
	if (m && m->notes.size ()) dp.custom.setNotes (m->notes, m->spq, m->lengthSlices ());
	else if (!(dp.style == DRUM_CUSTOM_STYLE && dp.custom.notes.size ()))
	{
		int style = dp.style != DRUM_CUSTOM_STYLE ? dp.style : 0;
		Vec<RiffNote> n = laneNotesForStyle (style, dp.beatsPerBar);
		dp.custom.setNotes (n, SPQ, dp.beatsPerBar * SPQ);
	}
	dp.style = DRUM_CUSTOM_STYLE;
	dp.catCategory = CUSTOM_CAT; dp.catMotif = CUSTOM_CAT;
}

// E(k,n) as notes on one row, tiled over `total` slices (EuclideanRhythm.Build)
static inline void euclidBuild (int row, int k, int n, int rotation, int step, int total, bool legato, Vec<RiffNote> &out)
{
	n = imax (1, n); step = imax (1, step);
	if (row < 0 || total <= 0) return;
	Vec<bool> pat; euclidPattern (k, n, rotation, pat);
	int cycle = n * step;
	Vec<int> on;
	for (int at = 0; at < total; at += cycle)
		for (int i = 0; i < n; i++)
		{
			if (!pat[i]) continue;
			int s = at + i * step;
			if (s >= total) break;
			on.push (s);
		}
	for (int i = 0; i < on.size (); i++)
	{
		int s = on[i];
		int len = legato ? (i + 1 < on.size () ? on[i + 1] - s : total - s) : imin (step, total - s);
		out.push (RiffNote (row, s, len));
	}
}
static inline void sortNotes (Vec<RiffNote> &n) { n.sort ([] (const RiffNote &a, const RiffNote &b) { return a.start != b.start ? a.start < b.start : a.note < b.note; }); }

static inline int drumUnit (const DrumModule &dp)
{
	if (dp.custom.slices.size ()) return dp.custom.slices.size ();
	int n = notes::lengthOf (dp.custom.notes);
	return n > 0 ? n : imax (1, dp.beatsPerBar) * SPQ;
}
// E(k,n) written on ONE drum lane over the whole block (the others kept)
static inline void applyEuclideanDrum (DrumModule &dp, int lane, int k, int n, int rotation, int step)
{
	if (lane < 0) return;
	if (!drumIsCustom (dp)) customizeDrum (dp);
	int oldUnit = drumUnit (dp), total = oldUnit * imax (1, dp.repeats);
	int spq = dp.custom.spq > 0 ? dp.custom.spq : SPQ;
	// (the step is in 24ths of a beat: rescaled to the motif's resolution)
	Vec<RiffNote> out;
	for (int r = 0; r < imax (1, dp.repeats); r++)
		for (int i = 0; i < dp.custom.notes.size (); i++)
		{
			const RiffNote &x = dp.custom.notes[i];
			if (x.note != lane) out.push (RiffNote (x.note, r * oldUnit + x.start, x.length));
		}
	// everything at 24 slices a beat so the triplets fit
	if (spq != SPQ)
	{
		for (int i = 0; i < out.size (); i++) { out[i].start = out[i].start * SPQ / spq; out[i].length = imax (1, out[i].length * SPQ / spq); }
		total = total * SPQ / spq;
	}
	euclidBuild (lane, k, n, rotation, step, total, false, out);
	sortNotes (out);
	dp.repeats = 1;
	dp.custom.setNotes (out, SPQ, total);
}
static inline void rotateDrumLane (DrumModule &dp, int lane, int delta24)
{
	if (lane < 0 || !delta24) return;
	if (!drumIsCustom (dp)) customizeDrum (dp);
	if (!dp.custom.notes.size ()) return;
	int spq = dp.custom.spq > 0 ? dp.custom.spq : SPQ;
	int delta = delta24 * spq / SPQ; if (!delta) delta = delta24 > 0 ? 1 : -1;
	int unit = drumUnit (dp);
	for (int i = 0; i < dp.custom.notes.size (); i++)
	{
		RiffNote &x = dp.custom.notes[i];
		if (x.note == lane) x.start = imod (x.start + delta, unit);
	}
	sortNotes (dp.custom.notes);
	dp.custom.setNotes (dp.custom.notes, spq, unit);
}

// ---- melodic lines: the rhythm at 24 slices a beat -----------------------------------------------------------
static inline void lineNotesAt24 (const MelodicLineModule &ml, int exceptVoice, Vec<RiffNote> &out)
{
	int old = ml.rhythm.spq > 0 ? ml.rhythm.spq : 4;
	for (int i = 0; i < ml.rhythm.notes.size (); i++)
	{
		const RiffNote &x = ml.rhythm.notes[i];
		if (x.note == exceptVoice) continue;
		out.push (old == SPQ ? x : RiffNote (x.note, x.start * SPQ / old, imax (1, x.length * SPQ / old)));
	}
}
static inline void applyEuclideanMelodic (MelodicLineModule &ml, int voice, int k, int n, int rotation, int step)
{
	if (voice < 0) return;
	int total = imax (1, ml.beatsPerBar) * SPQ;
	Vec<RiffNote> out; lineNotesAt24 (ml, voice, out);
	euclidBuild (voice, k, n, rotation, step, total, false, out);
	sortNotes (out);
	if (voice >= ml.voiceCount) ml.voiceCount = voice + 1;
	ml.rhythm.setNotes (out, SPQ, total);
}
static inline void rotateMelodicVoice (MelodicLineModule &ml, int voice, int delta)
{
	if (voice < 0 || !delta || !ml.rhythm.notes.size ()) return;
	int unit = imax (1, ml.beatsPerBar) * SPQ;
	Vec<RiffNote> out; lineNotesAt24 (ml, voice, out);
	int old = ml.rhythm.spq > 0 ? ml.rhythm.spq : 4;
	for (int i = 0; i < ml.rhythm.notes.size (); i++)
	{
		const RiffNote &x = ml.rhythm.notes[i];
		if (x.note != voice) continue;
		int s = old == SPQ ? x.start : x.start * SPQ / old, len = old == SPQ ? x.length : imax (1, x.length * SPQ / old);
		out.push (RiffNote (x.note, imod (s + delta, unit), len));
	}
	sortNotes (out);
	ml.rhythm.setNotes (out, SPQ, unit);
}

// ---- the degree vocabulary of the chord editors (ChordDegreeChoices) --------------------------------------------
struct DegreeChoices
{
	enum { SECONDARY_BASE = 8 };
	char names[16][24];
	const char *ptrs[16];
	int n, targets[5], nTargets;
	Key key;
	static void roman (const Key &k, int degree, char *b, int cap)
	{
		static const char *const U[7] = { "I", "II", "III", "IV", "V", "VI", "VII" }, *const L[7] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
		int d = imod (degree, 7);
		snprintf (b, cap, "%s", diatonicThird (k, d) == 4 ? U[d] : L[d]);
	}
	void build (const Key &k)
	{
		key = k; n = 0; nTargets = 0;
		snprintf (names[n++], 24, "Manual (fixed chord)");
		for (int d = 0; d < 7; d++) roman (k, d, names[n++], 24);
		static const int sec[5] = { 1, 2, 3, 4, 5 };
		for (int i = 0; i < 5; i++)
		{
			if (diatonicIsDim (k, sec[i])) continue;
			char r[8]; roman (k, sec[i], r, sizeof r);
			snprintf (names[n++], 24, "V/%s", r);
			targets[nTargets++] = sec[i];
		}
		for (int i = 0; i < n; i++) ptrs[i] = names[i];
	}
	int indexOf (int degree, int rootPc, int quality) const
	{
		if (degree >= 0) return imin (7, degree + 1);
		int t = secondaryDominantTarget (key, rootPc, quality);
		for (int i = 0; i < nTargets; i++) if (targets[i] == t) return SECONDARY_BASE + i;
		return 0;
	}
	bool trySecondary (int index, int *root, int *quality) const
	{
		int i = index - SECONDARY_BASE;
		if (i < 0 || i >= nTargets) return false;
		*root = secondaryDominantRoot (key, targets[i]);
		*quality = qualityIndex ("7 (dom)");
		return true;
	}
};

// the chord's quality from its colour trio (degree-locked: from the key)
static inline void applyDiatonic (PatternModule &pg, const Key &k)
{
	if (pg.degree < 0) { pg.quality = qualityForColour (pg.diatonicColour, pg.suspension, pg.modeOverride); return; }
	RootQ c = diatonicChord (k, pg.degree, pg.diatonicColour, pg.suspension, pg.modeOverride);
	pg.root = c.root; pg.quality = c.quality;
}
static inline void syncColourTrio (PatternModule &pg)
{
	DegColour d = colourForQuality (pg.quality);
	if (qualityForColour (d.colour, d.suspension, d.mode) != pg.quality) return;
	pg.diatonicColour = d.colour; pg.suspension = d.suspension; pg.modeOverride = d.mode;
}
// a new chord, degree-locked when it is diatonic
static inline PatternModule *makeChord (const Key &k, int root, int quality, int beats)
{
	PatternModule *pg = new PatternModule;
	pg->id = newId ();
	pg->root = imod (root, 12); pg->quality = quality;
	DegColour dc = degColour (k, pg->root, quality);
	pg->degree = dc.degree; pg->diatonicColour = dc.colour; pg->suspension = dc.suspension; pg->modeOverride = dc.mode;
	pg->octave = 4; pg->beatsPerBar = imax (1, beats); pg->repeats = 1;
	return pg;
}

// ---- cadences ------------------------------------------------------------------------------------------------
struct CadTuple { int root, quality, inversion, shift; };
static inline void makeCadenceChords (const Key &k, const Vec<CadTuple> &ch, bool voiceLead, int octave, Vec<CadenceChord> &out)
{
	out.clear ();
	int prevHeld = -1000;
	for (int i = 0; i < ch.size (); i++)
	{
		int deg = degreeOf (k, ch[i].root);
		bool diat = diatonicChord (k, deg).root == ch[i].root;
		int inv = voiceLead ? ch[i].inversion : 0, shift = voiceLead ? ch[i].shift : 0;
		int notes[16]; int nn = chordNotes (ch[i].root, octave + shift, ch[i].quality, inv, false, notes);
		int held = nn - 1;
		if (prevHeld != -1000 && nn > 0)
		{
			int bd = 1 << 30;
			for (int j = 0; j < nn; j++) { int d = iabs (notes[j] - prevHeld); if (d < bd) { bd = d; held = j; } }
		}
		if (nn > 0) prevHeld = notes[held];
		CadenceChord c; c.root = ch[i].root; c.quality = ch[i].quality; c.inversion = inv; c.octaveShift = shift; c.heldVoice = held; c.degree = diat ? deg : -1;
		out.push (c);
	}
}
static inline void buildCadenceChords (const Key &k, int startDeg, int numChords, int style, int octave, int anchor, int seed, Vec<CadTuple> &out)
{
	out.clear ();
	Vec<RootQ> ch = cadence (k, startDeg, numChords, style, seed);
	if (!ch.size ()) return;
	Vec<ShiftInv> vl = voiceLead (ch, octave, anchor);
	for (int i = 0; i < ch.size (); i++) { CadTuple t; t.root = ch[i].root; t.quality = ch[i].quality; t.inversion = vl[i].inversion; t.shift = vl[i].shift; out.push (t); }
}
static inline void revoiceCadence (const Key &k, CadenceModule &cm)
{
	Vec<RootQ> basics;
	for (int i = 0; i < cm.chords.size (); i++) { RootQ r; r.root = cm.chords[i].root; r.quality = cm.chords[i].quality; basics.push (r); }
	Vec<CadTuple> t;
	if (cm.voiceLeadMode == 0) for (int i = 0; i < basics.size (); i++) { CadTuple x = { basics[i].root, basics[i].quality, 0, 0 }; t.push (x); }
	else
	{
		Vec<ShiftInv> vl = voiceLead (basics, cm.octave, cm.voiceLeadMode - 1);
		for (int i = 0; i < basics.size (); i++) { CadTuple x = { basics[i].root, basics[i].quality, vl[i].inversion, vl[i].shift }; t.push (x); }
	}
	makeCadenceChords (k, t, cm.voiceLeadMode != 0, cm.octave, cm.chords);
}
static inline void regenCadence (const Project &p, CadenceModule &cm, int seed)
{
	int measureBeats = imax (1, p.barBeats ());
	int cpm = imax (1, imin (cm.chordsPerMeasure, measureBeats));
	int chordBeats = imax (1, iround (measureBeats / (double) cpm));
	int numChords = imax (1, cm.measures) * cpm;
	cm.beatsPerBar = chordBeats;
	int anchor = cm.voiceLeadMode <= 1 ? 0 : cm.voiceLeadMode - 1;
	Vec<CadTuple> t; buildCadenceChords (p.key, cm.startDegree, numChords, cm.cadenceStyle, cm.octave, anchor, seed, t);
	if (!t.size ()) return;
	makeCadenceChords (p.key, t, cm.voiceLeadMode != 0, cm.octave, cm.chords);
}
// the chord track's last chord's degree (to continue from it)
static inline int lastChordDegree (const Project &p)
{
	int ci = p.chordTrackIndex ();
	if (ci < 0) return 0;
	const Track &t = p.tracks[ci];
	for (int i = t.items.size () - 1; i >= 0; i--)
		if (t.items[i].module && t.items[i].module->kind == M_PATTERN) return degreeOf (p.key, imod (((PatternModule *) t.items[i].module)->root, 12));
	return 0;
}
// a cadence's chords appended to the chord track (the chord editor's "Cadence..."): -> the first new index
static inline int insertCadence (Doc &d, int startDeg, int style, int measures, int chordsPerMeasure, int seed)
{
	Project &p = d.p;
	int ci = p.chordTrackIndex ();
	if (ci < 0) return -1;
	int measureBeats = imax (1, p.barBeats ());
	int cpm = imax (1, imin (chordsPerMeasure, measureBeats));
	int chordBeats = imax (1, iround (measureBeats / (double) cpm));
	Vec<CadTuple> t; buildCadenceChords (p.key, startDeg, imax (1, measures) * cpm, style, 4, 0, seed, t);
	int first = -1;
	for (int i = 0; i < t.size (); i++)
	{
		int at = d.append (ci, makeChord (p.key, t[i].root, t[i].quality, chordBeats));
		if (first < 0) first = at;
	}
	revoiceTrack (p.tracks[ci]);
	return first;
}

// ---- user styles saved in the project --------------------------------------------------------------------------
static inline void saveUserStyle (Vec<UserStyle> &list, const char *name, const Vec<RiffNote> &notes, int spb, int beats)
{
	UserStyle u; u.name = name; u.spb = imax (1, spb); u.beats = imax (1, beats); u.notes = notes; u.hasNotes = true;
	u.slices = notes::toSlices (notes, imax (u.beats * u.spb, notes::lengthOf (notes)));
	for (int i = 0; i < list.size (); i++) if (list[i].name == name) { list[i] = u; return; }
	list.push (u);
}
// the notes of a user style (its note list, else its grid)
static inline Vec<RiffNote> userStyleNotes (const UserStyle &u) { return u.notes.size () || u.hasNotes ? u.notes : notes::fromSlices (u.slices); }

// the chord under the start of a block (else the key's tonic chord): what a grid previews on
static inline void chordUnder (const Project &p, double beat, double len, int *root, int *quality)
{
	Vec<ChordSeg> s = segments (p, beat, len);
	if (s.size ()) { *root = s[0].root; *quality = s[0].quality; return; }
	RootQ d = diatonicChord (p.key, 0); *root = d.root; *quality = d.quality;
}

// ---- the next chord (HarmonySuggest) -----------------------------------------------------------------------------
// the last 2-3 chord degrees up to and including item `upTo`, the bar where the next chord lands
static inline int chordContext (const Project &p, int t, int upTo, int *prevDegs, int *barIndex)
{
	const Track &tr = p.tracks[t];
	int degs[512]; int n = 0; double beats = 0;
	int bpb = imax (1, p.barBeats ());
	for (int i = 0; i < tr.items.size () && n < 512; i++)
	{
		const Module *m = tr.items[i].module;
		if (m && m->kind == M_PATTERN) { const PatternModule *pg = (const PatternModule *) m; degs[n++] = pg->degree >= 0 ? pg->degree : degreeOf (p.key, imod (pg->root, 12)); }
		else if (m && m->kind == M_CADENCE && ((const CadenceModule *) m)->chords.size ()) { const CadenceChord &c = ((const CadenceModule *) m)->chords.back (); degs[n++] = c.degree >= 0 ? c.degree : degreeOf (p.key, imod (c.root, 12)); }
		else if (m && m->kind == M_POLYCHORD && ((const PolyChordModule *) m)->chords.size ()) { const PolyChordItem &c = ((const PolyChordModule *) m)->chords.back (); degs[n++] = c.degree >= 0 ? c.degree : degreeOf (p.key, imod (c.root, 12)); }
		beats += tr.items[i].silenceBefore + p.itemLength (tr.items[i]);
		if (i == upTo) break;
	}
	int take = imin (3, n);
	for (int i = 0; i < take; i++) prevDegs[i] = degs[n - take + i];
	*barIndex = (int) (beats / bpb + 1e-6);
	return take;
}
// a new chord shaped like `prev` (its length, octave, voicing)
static inline PatternModule *newChordLike (const PatternModule *prev, int barBeats)
{
	PatternModule *pg = new PatternModule;
	pg->id = newId ();
	pg->beatsPerBar = prev ? imax (1, prev->beatsPerBar * imax (1, prev->repeats)) : imax (1, barBeats);
	pg->repeats = 1;
	if (prev) { pg->octave = prev->octave; pg->openVoicing = prev->openVoicing; pg->voiceLeadMode = prev->voiceLeadMode; }
	return pg;
}
static inline void applySuggestion (PatternModule &pg, const Key &k, bool keepFigured, const Suggestion &s)
{
	if (s.deg >= 0)
	{
		RootQ c = diatonicChord (k, s.deg, s.colour, 0, 0);
		pg.root = c.root; pg.quality = c.quality; pg.diatonicColour = s.colour; pg.suspension = 0; pg.modeOverride = 0;
		pg.degree = keepFigured ? s.deg : -1;
	}
	else { RootQ c = suggestionChord (s, k); pg.root = c.root; pg.quality = c.quality; pg.degree = -1; pg.diatonicColour = 0; pg.suspension = 0; pg.modeOverride = 0; }
}
// a chord inserted right after item i of track t (the next ones move later) -> its index
static inline int insertChordAfter (Doc &d, int t, int i, PatternModule *pg)
{
	Track &tr = d.p.tracks[t];
	int at = iclamp (i + 1, 0, tr.items.size ());
	tr.items.insert (at, Item (0, pg));
	revoiceTrack (tr);
	return at;
}
// "Chain N bars": the top-ranked continuation appended `bars` times
static inline int chainProgression (Doc &d, int t, int i, int bars)
{
	Project &p = d.p;
	int after = i;
	for (int n = 0; n < imax (1, bars); n++)
	{
		int prev[3], bar; int np = chordContext (p, t, after, prev, &bar);
		if (!np || prev[np - 1] < 0) break;
		Vec<Suggestion> r = suggestNext (prev, np, bar, 4, MOOD_AUTO, p.key);
		if (!r.size ()) break;
		const Module *pm = p.tracks[t].items[after].module;
		const PatternModule *prevPg = pm && pm->kind == M_PATTERN ? (const PatternModule *) pm : 0;
		PatternModule *pg = newChordLike (prevPg, p.barBeats ());
		applySuggestion (*pg, p.key, !prevPg || prevPg->degree >= 0, r[0]);
		after = insertChordAfter (d, t, after, pg);
	}
	return after;
}

} // namespace kui

#endif
