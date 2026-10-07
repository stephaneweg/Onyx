//
// ai_place.cpp -- Koton's AI composition (see ai.h), the reply side: the replies read as Koton's
// System.Text.Json contracts (AiArrangement and its converters: a note as [a,b,c] or as an object,
// numbers as strings, polyChords / polyDrums as one object or an array), then placed on the project
// as AiArrangementPlacer (BuildFresh / Develop / AddTrack), ChordModelOps.AddAiChord,
// TimelineHelper.ApplyAiDrum / ApplyAiRiff and AiPolyPlacer do.
//
// Deliberate differences with Koton (each marked "Onyx:"): the default track names follow the UI
// language (AiRequest::english); "develop" puts the new accompaniment blocks under the development
// (Koton lays them from bar 1 when the piece had no accompaniment track); a stray scalar in a note
// list is skipped (Koton adds a note 0 at beat 0); an add-track reply with nothing to add is an error.
//
#include "ai.h"
#include "gen.h"
#include "compile.h"
#include "json.hpp"
#include <stdio.h>
#include <string.h>

namespace kt {

using namespace aidet;
typedef json::Value V;

// ---- small helpers ---------------------------------------------------------------------------------------------
static void setErr (char *err, int cap, const char *msg) { if (err && cap > 0) snprintf (err, cap, "%s", msg); }
static bool blank (const char *s)
{
	if (!s) return true;
	for (; *s; s++) if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') return false;
	return true;
}
static void trimTo (const char *s, Str &out)
{
	if (!s) { out = Str (); return; }
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
	int n = (int) strlen (s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
	out.set (s, n);
}

// UTF-8 code points (for the case-insensitive names and the style-name slugs)
static unsigned nextCp (const unsigned char *&p)
{
	unsigned cp;
	if (*p < 0x80) cp = *p++;
	else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 31u) << 6) | (p[1] & 63u); p += 2; }
	else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { cp = ((p[0] & 15u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u); p += 3; }
	else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = ((p[0] & 7u) << 18) | ((p[1] & 63u) << 12) | ((p[2] & 63u) << 6) | (p[3] & 63u); p += 4; }
	else cp = *p++;
	return cp;
}
static unsigned lowerCp (unsigned cp)
{
	if (cp >= 'A' && cp <= 'Z') return cp + 32;
	if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 0x20;
	return cp;
}
static bool letterOrDigit (unsigned cp)
{
	if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9');
	if (cp >= 0xC0 && cp <= 0x24F) return cp != 0xD7 && cp != 0xF7;
	return (cp >= 0x370 && cp < 0x2000) || cp >= 0x3040;
}
static int putCp (char *o, unsigned cp)
{
	if (cp < 0x80) { o[0] = (char) cp; return 1; }
	if (cp < 0x800) { o[0] = (char) (0xC0 | (cp >> 6)); o[1] = (char) (0x80 | (cp & 63)); return 2; }
	if (cp < 0x10000) { o[0] = (char) (0xE0 | (cp >> 12)); o[1] = (char) (0x80 | ((cp >> 6) & 63)); o[2] = (char) (0x80 | (cp & 63)); return 3; }
	o[0] = (char) (0xF0 | (cp >> 18)); o[1] = (char) (0x80 | ((cp >> 12) & 63)); o[2] = (char) (0x80 | ((cp >> 6) & 63)); o[3] = (char) (0x80 | (cp & 63)); return 4;
}
// StringComparer.OrdinalIgnoreCase (ASCII + Latin-1 letters)
static bool ieq (const char *a, const char *b)
{
	const unsigned char *p = (const unsigned char *) (a ? a : ""), *q = (const unsigned char *) (b ? b : "");
	while (*p && *q) if (lowerCp (nextCp (p)) != lowerCp (nextCp (q))) return false;
	return !*p && !*q;
}

// a string -> value map with case-insensitive keys (C#'s Dictionary<string, T> (OrdinalIgnoreCase))
template <class T> struct NameMap
{
	struct E { Str k; T v; };
	Vec<E> e;
	T *find (const char *k) { for (int i = 0; i < e.size (); i++) if (ieq (e[i].k, k)) return &e[i].v; return 0; }
	void set (const char *k, const T &v) { T *x = find (k); if (x) *x = v; else { E n; n.k = k; n.v = v; e.push (n); } }
};

// ---- reading the JSON as Koton's contracts ---------------------------------------------------------------------------
// AiJson.ReadNum: a number, a numeric string, else 0
static double readNum (const V &v)
{
	if (v.isNum ()) return (double) v.n;
	if (v.isStr ()) { json::num_t d; if (V::parseNumber (v.s, &d)) return (double) d; }
	return 0;
}
// AiJson.ReadStr / FlexibleStringConverter: a string, or a number / bool as text; else null
static Str readStr (const V &v)
{
	if (v.isStr ()) return Str (v.s);
	if (v.isBool ()) return Str (v.b ? "true" : "false");
	if (v.isNum ())
	{
		char b[40]; double d = (double) v.n;
		if (d > -1e15 && d < 1e15 && d == (double) (long long) d) snprintf (b, sizeof b, "%lld", (long long) d);
		else snprintf (b, sizeof b, "%.15g", d);
		return Str (b);
	}
	return Str ();
}
static int gInt (const V &o, const char *k, int def) { const V *v = o.find (k); return (v && !v->isNull () && !v->isArr () && !v->isObj ()) ? iround (readNum (*v)) : def; }
static bool gBool (const V &o, const char *k, bool def) { const V *v = o.find (k); return v ? v->asBool (def) : def; }
static Str gStr (const V &o, const char *k) { const V *v = o.find (k); return v ? readStr (*v) : Str (); }

// the elements of a list: an array's containers (a stray scalar is skipped -- Onyx), or one object
static void elems (const V *v, Vec<const V *> &out)
{
	out.clear ();
	if (!v) return;
	if (v->isArr ()) { for (const V *c = v->first (); c; c = c->next) if (c->isArr () || c->isObj ()) out.push (c); }
	else if (v->isObj ()) out.push (v);
}
static void lowerAscii (const char *s, char *o, int cap)
{
	int i = 0;
	for (; s && s[i] && i + 1 < cap; i++) o[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char) (s[i] + 32) : s[i];
	o[i] = 0;
}

// one [a, start, length] entry (a riff note, an articulation event, a melodic-cell note)
struct ANote { int a; double start, length; };
static void rdNotes (const V *list, const char *const *aKeys, int defA, Vec<ANote> &out)
{
	Vec<const V *> es; elems (list, es);
	for (int i = 0; i < es.size (); i++)
	{
		const V &e = *es[i];
		ANote n; n.a = defA; n.start = 0; n.length = 1;
		if (e.isArr ())
		{
			int k = 0;
			for (const V *c = e.first (); c; c = c->next, k++)
			{
				double v = readNum (*c);
				if (k == 0) n.a = iround (v); else if (k == 1) n.start = v; else if (k == 2) n.length = v;
			}
		}
		else
			for (const V *c = e.first (); c; c = c->next)
			{
				char k[24]; lowerAscii (c->key, k, sizeof k);
				double v = readNum (*c);
				bool isA = false;
				for (int j = 0; aKeys[j]; j++) if (!strcmp (k, aKeys[j])) isA = true;
				if (isA) n.a = iround (v);
				else if (!strcmp (k, "start")) n.start = v;
				else if (!strcmp (k, "length") || !strcmp (k, "len") || !strcmp (k, "dur")) n.length = v;
			}
		out.push (n);
	}
}
static const char *const kPitchKeys[] = { "pitch", 0 };
static const char *const kVoiceKeys[] = { "voice", "voix", 0 };
static const char *const kDegreeKeys[] = { "degree", "deg", "degre", 0 };

struct AChord { int measure, degree; Str quality; };
static void rdChords (const V *list, Vec<AChord> &out)
{
	Vec<const V *> es; elems (list, es);
	for (int i = 0; i < es.size (); i++)
	{
		const V &e = *es[i];
		AChord c; c.measure = 1; c.degree = 1;
		if (e.isArr ())
		{
			int k = 0;
			for (const V *x = e.first (); x; x = x->next, k++)
			{
				if (k == 0) c.measure = iround (readNum (*x));
				else if (k == 1) c.degree = iround (readNum (*x));
				else if (k == 2) c.quality = readStr (*x);
			}
		}
		else
			for (const V *x = e.first (); x; x = x->next)
			{
				char k[24]; lowerAscii (x->key, k, sizeof k);
				if (!strcmp (k, "measure") || !strcmp (k, "mes")) c.measure = iround (readNum (*x));
				else if (!strcmp (k, "degree") || !strcmp (k, "deg")) c.degree = iround (readNum (*x));
				else if (!strcmp (k, "quality") || !strcmp (k, "qual")) c.quality = readStr (*x);
			}
		out.push (c);
	}
}

struct ASection { Str name; int measures; };
struct AArt { Str section, style, name; Vec<ANote> motif, cell; };
struct ALine { Str section, track, anchor, contour; int instrument, fromMeasure, measures, reg, voice; Vec<double> durations; };
struct ARiff { Str section, track; int instrument, fromMeasure, measures, motifBars, repeats; Vec<ANote> notes; };
struct APolyChordRing { int toneIndex, hits, steps, rotation, octave; Str contour; bool legato; };
struct APolyChordSpec { int fromMeasure, measures, cycleBeats, octave; Str mode, restart; bool openVoicing; Vec<APolyChordRing> layers; };
struct APolyDrumRing { int lane, hits, steps, rotation, accentLane; };
struct APolyDrumSpec { int fromMeasure, measures, kit, cycleBeats; Vec<APolyDrumRing> layers; };

struct AArrangement
{
	bool hasMeter, hasKey; int num, den; Str tonic, mode; int bpm, chordInstrument;
	Vec<ASection> sections;
	Vec<AChord> chords;
	Vec<AArt> articulation;
	Vec<ALine> melodicLines;
	Vec<ARiff> riffs, drums;
	bool hasPolyChords, hasPolyDrums;
	Vec<APolyChordSpec> polyChords;
	Vec<APolyDrumSpec> polyDrums;
	AArrangement () : hasMeter (false), hasKey (false), num (4), den (4), bpm (0), chordInstrument (-1), hasPolyChords (false), hasPolyDrums (false) {}
};

static void rdRiffList (const V *list, Vec<ARiff> &out)
{
	Vec<const V *> es; elems (list, es);
	for (int i = 0; i < es.size (); i++)
	{
		const V &o = *es[i];
		if (!o.isObj ()) continue;
		ARiff r;
		r.section = gStr (o, "section"); r.track = gStr (o, "track");
		r.instrument = gInt (o, "instrument", -1); r.fromMeasure = gInt (o, "fromMeasure", 1); r.measures = gInt (o, "measures", 1);
		r.motifBars = gInt (o, "motifBars", 0); r.repeats = gInt (o, "repeats", 0);
		rdNotes (o.find ("notes"), kPitchKeys, 0, r.notes);
		out.push (r);
	}
}

static bool parseRoot (const char *reply, json::Doc &d, char *err, int errcap)
{
	Str clean; cleanJson (reply, clean);
	if (!d.parse (clean.c (), clean.len (), json::TOLERANT))
	{
		char m[160]; snprintf (m, sizeof m, "The reply is not valid JSON (%s, line %d).", d.error (), d.errLine ());
		setErr (err, errcap, m);
		return false;
	}
	if (!d.root ().isObj ()) { setErr (err, errcap, "The reply is not a JSON object."); return false; }
	return true;
}

// AiArrangement.Parse (requireChords) / ParseTrack
static bool parseArrangement (const char *reply, bool requireChords, AArrangement &a, char *err, int errcap)
{
	json::Doc d;
	if (!parseRoot (reply, d, err, errcap)) return false;
	const V &r = d.root ();
	const V *meter = r.find ("meter");
	if (meter && meter->isObj ()) { a.hasMeter = true; a.num = gInt (*meter, "num", 4); a.den = gInt (*meter, "den", 4); }
	const V *key = r.find ("key");
	if (key && key->isObj ()) { a.hasKey = true; a.tonic = gStr (*key, "tonic"); a.mode = gStr (*key, "mode"); }
	a.bpm = gInt (r, "bpm", 0);
	a.chordInstrument = gInt (r, "chordInstrument", -1);
	Vec<const V *> es;
	elems (r.find ("sections"), es);
	for (int i = 0; i < es.size (); i++) if (es[i]->isObj ()) { ASection s; s.name = gStr (*es[i], "name"); s.measures = gInt (*es[i], "measures", 0); a.sections.push (s); }
	rdChords (r.find ("chords"), a.chords);
	elems (r.find ("articulation"), es);
	for (int i = 0; i < es.size (); i++)
	{
		if (!es[i]->isObj ()) continue;
		const V &o = *es[i]; AArt t;
		t.section = gStr (o, "section"); t.style = gStr (o, "style"); t.name = gStr (o, "name");
		rdNotes (o.find ("motif"), kVoiceKeys, 0, t.motif);
		rdNotes (o.find ("melodicCell"), kDegreeKeys, 1, t.cell);
		a.articulation.push (t);
	}
	elems (r.find ("melodicLines"), es);
	for (int i = 0; i < es.size (); i++)
	{
		if (!es[i]->isObj ()) continue;
		const V &o = *es[i]; ALine l;
		l.section = gStr (o, "section"); l.track = gStr (o, "track");
		l.instrument = gInt (o, "instrument", -1); l.fromMeasure = gInt (o, "fromMeasure", 1); l.measures = gInt (o, "measures", 1);
		const V *du = o.find ("durations");
		if (du && du->isArr ()) for (const V *c = du->first (); c; c = c->next) l.durations.push (readNum (*c));
		l.anchor = gStr (o, "anchor"); l.contour = gStr (o, "contour");
		l.reg = gInt (o, "register", 0); l.voice = gInt (o, "voice", 1);
		a.melodicLines.push (l);
	}
	rdRiffList (r.find ("riffs"), a.riffs);
	rdRiffList (r.find ("drums"), a.drums);
	// the polyrhythmic options: absent (or null) = not asked; one object = one entry (SingleOrListConverter)
	const V *pc = r.find ("polyChords");
	if (pc && !pc->isNull ())
	{
		a.hasPolyChords = true;
		elems (pc, es);
		for (int i = 0; i < es.size (); i++)
		{
			if (!es[i]->isObj ()) continue;
			const V &o = *es[i]; APolyChordSpec s;
			s.fromMeasure = gInt (o, "fromMeasure", 0); s.measures = gInt (o, "measures", 0);
			s.cycleBeats = gInt (o, "cycleBeats", 4); s.octave = gInt (o, "octave", 4);
			s.mode = gStr (o, "mode"); s.restart = gStr (o, "restart"); s.openVoicing = gBool (o, "openVoicing", false);
			Vec<const V *> ls; elems (o.find ("layers"), ls);
			for (int j = 0; j < ls.size (); j++)
			{
				if (!ls[j]->isObj ()) continue;
				const V &l = *ls[j]; APolyChordRing g;
				g.toneIndex = gInt (l, "toneIndex", 0); g.contour = gStr (l, "contour"); g.hits = gInt (l, "hits", 3);
				g.steps = gInt (l, "steps", 8); g.rotation = gInt (l, "rotation", 0); g.octave = gInt (l, "octave", 0);
				g.legato = gBool (l, "legato", false);
				s.layers.push (g);
			}
			a.polyChords.push (s);
		}
	}
	const V *pd = r.find ("polyDrums");
	if (pd && !pd->isNull ())
	{
		a.hasPolyDrums = true;
		elems (pd, es);
		for (int i = 0; i < es.size (); i++)
		{
			if (!es[i]->isObj ()) continue;
			const V &o = *es[i]; APolyDrumSpec s;
			s.fromMeasure = gInt (o, "fromMeasure", 0); s.measures = gInt (o, "measures", 0);
			s.kit = gInt (o, "kit", 0); s.cycleBeats = gInt (o, "cycleBeats", 4);
			Vec<const V *> ls; elems (o.find ("layers"), ls);
			for (int j = 0; j < ls.size (); j++)
			{
				if (!ls[j]->isObj ()) continue;
				const V &l = *ls[j]; APolyDrumRing g;
				g.lane = gInt (l, "lane", 0); g.hits = gInt (l, "hits", 3); g.steps = gInt (l, "steps", 8);
				g.rotation = gInt (l, "rotation", 0); g.accentLane = gInt (l, "accentLane", -1);
				s.layers.push (g);
			}
			a.polyDrums.push (s);
		}
	}
	if (requireChords && a.chords.size () == 0) { setErr (err, errcap, "No chords in the reply."); return false; }
	return true;
}

// ---- AiTranslate: the notes ------------------------------------------------------------------------------------------------
static Vec<RiffNote> buildArticulationNotes (const Vec<ANote> &motif, int spq)
{
	Vec<RiffNote> notes;
	for (int i = 0; i < motif.size (); i++)
	{
		int row = iclamp (motif[i].a, 0, CUSTOM_VOICE_COUNT - 1);
		int start = imax (0, iround (motif[i].start * spq));
		int len = imax (1, iround (motif[i].length * spq));
		notes.push (RiffNote (row, start, len));
	}
	return notes;
}

// the melodic cell: rows = diatonic degrees over two octaves (degree 1 = the chord's anchor)
static Vec<RiffNote> buildMelodicCellNotes (const Vec<ANote> &cell, int spq)
{
	Vec<RiffNote> notes;
	for (int i = 0; i < cell.size (); i++)
	{
		int row = iclamp (cell[i].a - 1, 0, 13);		// PatternGenerator.MelodicRowCount - 1 (14 rows in Koton)
		int start = imax (0, iround (cell[i].start * spq));
		int len = imax (1, iround (cell[i].length * spq));
		notes.push (RiffNote (row, start, len));
	}
	return notes;
}

// explicit notes -> a riff's notes (Note = MIDI - 12), a motif shorter than the phrase tiled to fill it
static Vec<RiffNote> buildRiffNotes (const Vec<ANote> &notes, int totalSlices, int spq, int barSlices, double startOffsetTemps)
{
	Vec<RiffNote> one;
	for (int i = 0; i < notes.size (); i++)
	{
		int start = imax (0, iround ((notes[i].start - startOffsetTemps) * spq));
		if (start >= totalSlices) continue;
		int len = imax (1, iround (notes[i].length * spq));
		if (start + len > totalSlices) len = totalSlices - start;
		if (len >= 1) one.push (RiffNote (iclamp (notes[i].a - 12, 0, 95), start, len));
	}
	if (one.size () == 0 || barSlices < 1) return one;
	int maxEnd = 0; for (int i = 0; i < one.size (); i++) maxEnd = imax (maxEnd, one[i].start + one[i].length);
	int motif = imax (barSlices, ((maxEnd + barSlices - 1) / barSlices) * barSlices);
	if (motif >= totalSlices) return one;
	Vec<RiffNote> outp;
	for (int off = 0; off < totalSlices; off += motif)
		for (int i = 0; i < one.size (); i++)
		{
			int s = off + one[i].start;
			if (s >= totalSlices) continue;
			int len = imin (one[i].length, totalSlices - s);
			if (len >= 1) outp.push (RiffNote (one[i].note, s, len));
		}
	return outp;
}

// a rhythm motif (durations in beats, negative = a rest) tiled over totalSlices, voice row 0
static Vec<RiffNote> buildRhythmNotes (const Vec<double> &durations, int totalSlices, int spq)
{
	Vec<RiffNote> notes;
	if (totalSlices <= 0) return notes;
	Vec<double> one; one.push (1.0);
	const Vec<double> &durs = durations.size () > 0 ? durations : one;
	int pos = 0, i = 0, guard = 0;
	while (pos < totalSlices && guard++ < 100000)
	{
		double d = durs[i % durs.size ()]; i++;
		if (d < 0) { pos += imax (1, iround (-d * spq)); continue; }
		int len = imax (1, iround (d * spq));
		if (pos + len > totalSlices) len = totalSlices - pos;
		if (len <= 0) break;
		notes.push (RiffNote (0, pos, len));
		pos += len;
	}
	return notes;
}

// the pitch classes of a chord / of the scale, in Koton's (HashSet insertion) order
struct PcList { int pc[12]; int n; PcList () : n (0) {} void add (int p) { for (int i = 0; i < n; i++) if (pc[i] == p) return; pc[n++] = p; } bool has (int p) const { for (int i = 0; i < n; i++) if (pc[i] == p) return true; return false; } };
static PcList chordPcs (const Key &k, int degree1, const char *quality)
{
	PcList s; int n[16];
	int c = chordNotes (rootPc (k, degree1), 4, aidet::qualityIndex (quality), 0, false, n);
	for (int i = 0; i < c; i++) s.add (imod (n[i], 12));
	return s;
}
static PcList scalePcs (const Key &k)
{
	PcList s; const int *sc = modeScale (effectiveMode (k));
	for (int i = 0; i < 7; i++) s.add (imod (tonicPc (k) + sc[i], 12));
	return s;
}
static int snapMidiToPcs (int midi, const PcList &pcs)
{
	if (pcs.n == 0) return midi;
	int best = midi, bestAbs = 0x7fffffff;
	for (int i = 0; i < pcs.n; i++)
	{
		int delta = imod (pcs.pc[i] - midi, 12);
		if (delta > 6) delta -= 12;
		if (iabs (delta) < bestAbs) { bestAbs = iabs (delta); best = midi + delta; }
	}
	return best;
}

// DrumPattern.CompressPeriodic: the smallest whole-beat period that repeats exactly
static void compressPeriodic (const Vec<RiffNote> &notes, int totalLen, int spq, Vec<RiffNote> &unit, int *unitLen, int *repeats)
{
	unit = notes; *unitLen = imax (1, totalLen); *repeats = 1;
	if (notes.size () == 0 || totalLen <= 0 || spq <= 0) return;
	auto has = [&] (int note, int start, int len) {
		for (int i = 0; i < notes.size (); i++) if (notes[i].note == note && notes[i].start == start && notes[i].length == len) return true;
		return false;
	};
	for (int P = spq; P < totalLen; P += spq)
	{
		if (totalLen % P != 0) continue;
		int reps = totalLen / P;
		bool ok = true;
		for (int i = 0; i < notes.size () && ok; i++) if (!has (notes[i].note, notes[i].start % P, notes[i].length)) ok = false;
		for (int i = 0; i < notes.size () && ok; i++)
		{
			if (notes[i].start >= P) continue;
			for (int k = 1; k < reps && ok; k++) if (!has (notes[i].note, notes[i].start + k * P, notes[i].length)) ok = false;
		}
		if (ok)
		{
			unit.clear ();
			for (int i = 0; i < notes.size (); i++) if (notes[i].start < P) unit.push (notes[i]);
			*unitLen = P; *repeats = reps;
			return;
		}
	}
}

// ---- tracks ---------------------------------------------------------------------------------------------------------------
struct Names { const char *chords, *drums, *accomp, *melody, *riff, *polyPerc, *polyLine; };
static const Names kNamesFr = { "Accords", "Batterie", "Accompagnement", "Mélodie", "Riff", "Percussions poly", "Ligne poly" };
static const Names kNamesEn = { "Chords", "Drums", "Accompaniment", "Melody", "Riff", "Poly percussion", "Poly line" };

// ChordModelOps.EnsureChordTrackSimple: a chord track, pinned last; its index
static int ensureChordTrack (Project &p, const Names &nm)
{
	int ci = p.chordTrackIndex ();
	if (ci < 0) { Track t; t.name = nm.chords; t.type = TRACK_CHORD; t.instrument = 0; p.tracks.push (move (t)); ci = p.tracks.size () - 1; }
	if (ci != p.tracks.size () - 1)
	{
		Track t (move (p.tracks[ci]));
		p.tracks.removeAt (ci);
		p.tracks.push (move (t));
		ci = p.tracks.size () - 1;
	}
	return ci;
}

// a new track for an element request: before the chord track when it is pinned last
static int addTrackBeforeChords (Project &p, const Track &t)
{
	int ci = p.chordTrackIndex ();
	if (ci >= 0 && ci == p.tracks.size () - 1) { p.tracks.insert (ci, t); return ci; }
	p.tracks.push (t);
	return p.tracks.size () - 1;
}

static int getOrCreateInstrTrack (Project &p, const char *role, int instr, bool reuse)
{
	if (reuse)
		for (int i = 0; i < p.tracks.size (); i++)
			if (p.tracks[i].type == TRACK_INSTRUMENT && ieq (p.tracks[i].name, role)) return i;
	Track t; t.name = role; t.type = TRACK_INSTRUMENT; t.instrument = instr;
	p.tracks.push (move (t));
	return p.tracks.size () - 1;
}

static int getOrCreateDrumTrack (Project &p, bool reuse, const Names &nm)
{
	if (reuse) for (int i = 0; i < p.tracks.size (); i++) if (p.tracks[i].type == TRACK_DRUM) return i;
	Track t; t.name = nm.drums; t.type = TRACK_DRUM; t.instrument = 128;	// InstrumentCatalog.DrumIndex
	p.tracks.push (move (t));
	return p.tracks.size () - 1;
}

// TimelineHelper.SnapToBarline / AddSectionMarkers
static double snapToBarline (const Project &p, double beat)
{
	int bpb = imax (1, p.barBeats ());
	double phase = p.pickupBeats > 1e-6 ? p.pickupBeats - bpb * ifloor (p.pickupBeats / bpb) : 0;
	if (beat < 0) beat = 0;
	if (phase > 1e-6 && beat < phase * 0.5) return 0;
	int m = iround ((beat - phase) / bpb);
	if (m < 0) m = 0;
	return phase + m * (double) bpb;
}
struct SecStart { const char *name; int startBar; };
static void addSectionMarkers (Project &p, const Vec<SecStart> &secs, double baseBeats)
{
	int bpb = imax (1, p.barBeats ());
	for (int i = 0; i < secs.size (); i++)
	{
		if (blank (secs[i].name)) continue;
		double beat = snapToBarline (p, baseBeats + imax (0, secs[i].startBar) * (double) bpb);
		bool already = false;
		for (int j = 0; j < p.markers.size (); j++) if (dabs (p.markers[j].beat - beat) < 1e-6) { already = true; break; }
		if (already) continue;
		Marker m; m.beat = beat; trimTo (secs[i].name, m.name);
		p.markers.push (m);
	}
	p.markers.sort ([] (const Marker &a, const Marker &b) { return a.beat < b.beat; });
}

// UniqueUserStyleName: a file-safe, unique name for an articulation saved as a user chord style
static void uniqueUserStyleName (const Project &p, const char *preferred, const char *section, Str &out)
{
	Str b0; trimTo (!blank (preferred) ? preferred : section, b0);
	char buf[128]; int o = 0;
	const unsigned char *q = (const unsigned char *) b0.c ();
	while (*q && o + 5 < (int) sizeof buf)
	{
		unsigned cp = nextCp (q);
		o += putCp (buf + o, letterOrDigit (cp) ? lowerCp (cp) : '_');
	}
	buf[o] = 0;
	// Trim ('_'), at most 24 characters
	char *s = buf; while (*s == '_') s++;
	int n = (int) strlen (s); while (n > 0 && s[n - 1] == '_') n--; s[n] = 0;
	if (!*s) s = (char *) "ia";
	char base[128]; int bo = 0, bc = 0;
	for (const unsigned char *r = (const unsigned char *) s; *r && bc < 24; bc++) { unsigned cp = nextCp (r); bo += putCp (base + bo, cp); }
	base[bo] = 0;
	auto used = [&] (const char *nm) { for (int i = 0; i < p.userChordStyles.size (); i++) if (ieq (p.userChordStyles[i].name, nm)) return true; return false; };
	if (!used (base)) { out = base; return; }
	for (int k = 2; ; k++)
	{
		char nm[160]; snprintf (nm, sizeof nm, "%s_%d", base, k);
		if (!used (nm)) { out = nm; return; }
	}
}

// ---- ChordModelOps.AddAiChord ------------------------------------------------------------------------------------------------
static PatternModule *newChordLike (const PatternModule *prev, int barTemps)
{
	PatternModule *pg = new PatternModule;
	pg->beatsPerBar = barTemps; pg->repeats = 1;
	if (prev)
	{
		pg->style = prev->style; pg->userStyleName = prev->userStyleName;
		pg->custom = prev->custom;			// (the anacrusis trim of Koton's copy: the AI's chords carry no grid)
		pg->openVoicing = prev->openVoicing;
		pg->voiceLeadMode = prev->voiceLeadMode != 0 ? prev->voiceLeadMode : 1;
		pg->bass = prev->bass; pg->bassPerBeat = prev->bassPerBeat;
		pg->climbMode = prev->climbMode; pg->heldMode = prev->heldMode; pg->halveDurations = prev->halveDurations;
		pg->diatonicColour = prev->diatonicColour; pg->suspension = prev->suspension; pg->modeOverride = prev->modeOverride; pg->octave = prev->octave;
		pg->beatsPerBar = imax (1, prev->beatsPerBar * imax (1, prev->repeats));
	}
	return pg;
}

// the chord (degree-locked when the quality is the degree's own: a 7th keeps its colour; a secondary
// dominant or a borrowed chord stays a fixed chord, Degree -1)
static void lockDegree (const Key &k, int &degree, int quality, int &colour, int &suspension)
{
	bool reqMin = false, reqDim = false;
	chordShape (quality, &reqMin, &reqDim, 0, 0);
	bool diaDim = diatonicIsDim (k, degree);
	bool diaMin = !diaDim && diatonicThird (k, degree) == 3;
	if (reqMin != diaMin || reqDim != diaDim) degree = -1;
	else { DegColour t = colourForQuality (quality); colour = t.colour; suspension = t.suspension; }
}

static PatternModule *addAiChord (Project &p, int chordTrack, const AChord &c, int beats, const PatternModule *prev)
{
	PatternModule *pg = newChordLike (prev, p.barBeats ());
	pg->beatsPerBar = imax (1, beats); pg->repeats = 1;
	int deg = iclamp (c.degree, 1, 7);
	pg->degree = deg - 1;
	pg->root = rootPc (p.key, deg);
	pg->quality = aidet::qualityIndex (c.quality);
	lockDegree (p.key, pg->degree, pg->quality, pg->diatonicColour, pg->suspension);
	p.tracks[chordTrack].items.push (Item (0, pg));
	return pg;
}

// ---- AiPolyModules ------------------------------------------------------------------------------------------------------------
template <class T> struct PolySpan { const T *spec; int from, measures; bool filler; };
enum { MAX_SPAN = 4 };				// AiPolyModules.MaxSpanMeasures

template <class T> static void addFiller (Vec<PolySpan<T> > &out, const T *spec, int from, int count)
{
	for (int m = from; m < from + count; m += MAX_SPAN)
	{
		PolySpan<T> s; s.spec = spec; s.from = m; s.measures = imin (MAX_SPAN, from + count - m); s.filler = true;
		out.push (s);
	}
}

template <class T> static Vec<PolySpan<T> > plan (const Vec<T> &specs, int totalMeasures, bool fillGaps)
{
	Vec<PolySpan<T> > res;
	if (specs.size () == 0 || totalMeasures < 1) return res;
	bool legacyWhole = specs.size () == 1 && specs[0].fromMeasure <= 0 && specs[0].measures <= 0;
	int cursor = 1;
	for (int i = 0; i < specs.size (); i++)
	{
		const T &s = specs[i];
		int from = s.fromMeasure >= 1 ? imax (s.fromMeasure, cursor) : cursor;
		if (from > totalMeasures) continue;
		int len = legacyWhole ? totalMeasures : iclamp (s.measures > 0 ? s.measures : MAX_SPAN, 1, MAX_SPAN);
		len = imin (len, totalMeasures - from + 1);
		if (len < 1) continue;
		PolySpan<T> sp; sp.spec = &s; sp.from = from; sp.measures = len; sp.filler = false;
		res.push (sp);
		cursor = from + len;
	}
	if (!fillGaps || res.size () == 0) return res;
	Vec<PolySpan<T> > full;
	int at = 1;
	const T *prev = 0;
	for (int i = 0; i < res.size (); i++)
	{
		if (res[i].from > at) addFiller (full, prev ? prev : res[0].spec, at, res[i].from - at);
		full.push (res[i]);
		at = res[i].from + res[i].measures;
		prev = res[i].spec;
	}
	if (at <= totalMeasures) addFiller (full, res.back ().spec, at, totalMeasures - at + 1);
	return full;
}

static int kitCount () { return g_kitPrograms.size (); }

static PolyDrumModule *buildPolyDrums (const APolyDrumSpec &spec, int totalBeats)
{
	int total = imax (1, totalBeats);
	int cycle = iclamp (spec.cycleBeats > 0 ? spec.cycleBeats : 4, 1, imin (32, total));
	PolyDrumModule *m = new PolyDrumModule;
	m->kit = iclamp (spec.kit, 0, imax (0, kitCount () - 1));
	m->beats = cycle;
	m->repeats = imax (1, total / cycle);
	for (int i = 0; i < spec.layers.size (); i++)
	{
		const APolyDrumRing &l = spec.layers[i];
		int steps = iclamp (l.steps, 2, 32), hits = iclamp (l.hits, 1, steps);
		EuclidLayer e;
		e.lane = iclamp (l.lane, 0, DRUM_LANES - 1);
		e.accentLane = (l.accentLane >= 0 && l.accentLane < DRUM_LANES) ? l.accentLane : -1;
		e.hits = hits; e.steps = steps; e.rotation = imod (l.rotation, steps);
		m->layers.push (e);
	}
	if (m->layers.size () == 0) { EuclidLayer e; e.lane = 0; e.hits = 4; e.steps = 8; m->layers.push (e); }
	return m;
}

static int parsePolyMode (const char *s)
{
	char n[64]; lowerAscii (s ? s : "", n, sizeof n);
	char k[64]; int o = 0; for (int i = 0; n[i] && o + 1 < (int) sizeof k; i++) if ((n[i] >= 'a' && n[i] <= 'z') || (n[i] >= '0' && n[i] <= '9')) k[o++] = n[i];
	k[o] = 0;
	if (!k[0]) return PC_ONE_RING_PER_TONE;
	if (!strcmp (k, "1") || strstr (k, "sweep") || strstr (k, "balay") || strstr (k, "parcour")) return PC_ONE_RING_SWEEP;
	return PC_ONE_RING_PER_TONE;
}
static int parseRestart (const char *s)
{
	char n[64]; int o = 0;
	const unsigned char *p = (const unsigned char *) (s ? s : "");
	while (*p && o + 5 < (int) sizeof n) { unsigned cp = lowerCp (nextCp (p)); if (letterOrDigit (cp)) o += putCp (n + o, cp); }
	n[o] = 0;
	static const struct { const char *w; int r; } t[] = {
		{ "1", RESTART_GRAVE }, { "grave", RESTART_GRAVE }, { "low", RESTART_GRAVE }, { "bass", RESTART_GRAVE }, { "basse", RESTART_GRAVE },
		{ "2", RESTART_AIGU }, { "aigu", RESTART_AIGU }, { "high", RESTART_AIGU }, { "top", RESTART_AIGU },
		{ "3", RESTART_TONIC }, { "tonic", RESTART_TONIC }, { "tonique", RESTART_TONIC }, { "fondamentale", RESTART_TONIC }, { "root", RESTART_TONIC },
		{ "4", RESTART_TIERCE }, { "tierce", RESTART_TIERCE }, { "third", RESTART_TIERCE },
		{ "5", RESTART_QUINTE }, { "quinte", RESTART_QUINTE }, { "fifth", RESTART_QUINTE } };
	for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp (n, t[i].w)) return t[i].r;
	return RESTART_NEAREST;
}

static PolyChordModule *buildPolyChords (const APolyChordSpec &spec)
{
	PolyChordModule *m = new PolyChordModule;
	m->cycleBeats = iclamp (spec.cycleBeats > 0 ? spec.cycleBeats : 4, 1, 32);
	m->octave = iclamp (spec.octave > 0 ? spec.octave : 4, 1, 7);
	m->openVoicing = spec.openVoicing;
	m->mode = parsePolyMode (spec.mode);
	m->restart = parseRestart (spec.restart);
	for (int i = 0; i < spec.layers.size (); i++)
	{
		const APolyChordRing &l = spec.layers[i];
		int steps = iclamp (l.steps, 2, 32), hits = iclamp (l.hits, 1, steps);
		EuclidChordLayer e;
		e.hits = hits; e.steps = steps; e.rotation = imod (l.rotation, steps);
		e.toneIndex = iclamp (l.toneIndex, -8, 11);
		e.contour = iclamp (contourIndex (l.contour), 0, 5);
		e.octave = iclamp (l.octave, -3, 3);
		e.legato = l.legato;
		m->layers.push (e);
	}
	if (m->layers.size () == 0)
	{
		EuclidChordLayer a; a.hits = 3; a.steps = 8; a.toneIndex = 0; m->layers.push (a);
		EuclidChordLayer b; b.hits = 5; b.steps = 8; b.toneIndex = 1; m->layers.push (b);
	}
	return m;
}

static PolyChordItem polyChordItem (const Project &p, const AChord &c, int beats)
{
	int deg = iclamp (c.degree, 1, 7);
	PolyChordItem it;
	it.beats = imax (1, beats);
	it.root = rootPc (p.key, deg);
	it.quality = aidet::qualityIndex (c.quality);
	it.degree = deg - 1;
	lockDegree (p.key, it.degree, it.quality, it.diatonicColour, it.suspension);
	return it;
}

static bool fitChordSpan (PolyChordModule &m, int targetBeats)
{
	if (m.chords.size () == 0 || targetBeats < 1) return false;
	int sum = 0; for (int i = 0; i < m.chords.size (); i++) sum += imax (1, m.chords[i].beats);
	int delta = targetBeats - sum;
	if (delta != 0) m.chords.back ().beats = imax (1, m.chords.back ().beats + delta);
	return true;
}

// ---- AiArrangementPlacer.Place ---------------------------------------------------------------------------------------------------
enum { ART_SPQ = 4 };

static void addAccompaniment (Project &p, const AArrangement &a, int barTemps, int totalMeasures, const Vec<const char *> &secOfMeasure,
			      NameMap<int> &styleOfSection, NameMap<Vec<RiffNote> > &motifOfSection, NameMap<Str> &nameOfSection,
			      NameMap<Vec<RiffNote> > &cellOfSection, double baseBeats, const Names &nm)
{
	int track = -1;
	int meas = 1;
	auto secAt = [&] (int m) -> const char * { return m < secOfMeasure.size () ? secOfMeasure[m] : 0; };
	while (meas <= totalMeasures)
	{
		const char *sec = secAt (meas);
		int run = 1;
		while (meas + run <= totalMeasures)
		{
			const char *s2 = secAt (meas + run);
			if ((s2 == 0) != (sec == 0) || (s2 && strcmp (s2, sec))) break;	// (an ordinal comparison, as Koton's ==)
			run++;
		}
		if (sec)
		{
			int *st = styleOfSection.find (sec);
			int style = st ? *st : -1;
			Vec<RiffNote> *motif = motifOfSection.find (sec);
			Str *userStyle = nameOfSection.find (sec);
			Vec<RiffNote> *cell = cellOfSection.find (sec);
			int totalBeats = run * barTemps;
			ArticulationModule *ca = new ArticulationModule;
			ca->beats = barTemps;				// the AI's motif is one bar: it loops
			ca->lengthBeats = totalBeats;
			ca->style = motif && motif->size () > 0 ? CUSTOM_STYLE : imax (0, style);
			ca->voiceLeadMode = 1;				// a smooth move from one chord to the next
			if (motif && motif->size () > 0)
			{
				ca->custom.setNotes (*motif, ART_SPQ, barTemps * ART_SPQ);
				if (userStyle) ca->userStyleName = *userStyle;
			}
			if (cell && cell->size () > 0)
			{
				int barSlices = barTemps * ART_SPQ;
				Vec<RiffNote> tiled;
				for (int b = 0; b < run; b++)
					for (int i = 0; i < cell->size (); i++)
					{
						const RiffNote &n = (*cell)[i];
						if (n.start >= barSlices) continue;
						int len = imin (n.length, barSlices - n.start);
						if (len >= 1) tiled.push (RiffNote (n.note, b * barSlices + n.start, len));
					}
				if (tiled.size () > 0) ca->melodic.setNotes (tiled, ART_SPQ, run * barSlices);
			}
			if (track < 0)
			{
				int instr = a.chordInstrument >= 0 && a.chordInstrument <= 127 ? a.chordInstrument : 0;
				track = getOrCreateInstrTrack (p, nm.accomp, instr, true);
			}
			// Onyx: + baseBeats, so that a development's accompaniment lies under the development
			double startBeats = baseBeats + (meas - 1) * (double) barTemps;
			double already = p.trackEnd (p.tracks[track]);
			p.tracks[track].items.push (Item (dmax (0, startBeats - already), ca));
		}
		meas += run;
	}
}

// the riff harmony fix: a note on a beat must be a chord tone, another one a scale tone
static void correctRiffHarmony (const Project &p, Vec<RiffNote> &notes, int riffStartBeat, int barTemps, int rspq,
				const Vec<const AChord *> &chordAtMeasure, const PcList &scale)
{
	for (int i = 0; i < notes.size (); i++)
	{
		RiffNote &n = notes[i];
		double absBeat = (riffStartBeat * rspq + n.start) / (double) rspq;
		int meas = ifloor (absBeat / barTemps) + 1;
		if (meas < 1 || meas >= chordAtMeasure.size () || !chordAtMeasure[meas]) continue;
		double beatInBar = absBeat - (meas - 1) * barTemps;
		double fr = beatInBar - ifloor (beatInBar + 0.5);
		bool onBeat = dabs (fr) < 0.06;
		PcList target = onBeat ? chordPcs (p.key, chordAtMeasure[meas]->degree, chordAtMeasure[meas]->quality) : scale;
		int midi = n.note + 12;
		if (target.has (imod (midi, 12))) continue;
		n.note = iclamp (snapMidiToPcs (midi, target) - 12, 0, 95);
	}
}

// models sometimes give ABSOLUTE starts: when the earliest note is at / after the entry's bar, make them relative
static double absoluteShift (const Vec<ANote> &notes, int relBeat)
{
	if (relBeat <= 0 || notes.size () == 0) return 0;
	double minStart = 1e300;
	for (int i = 0; i < notes.size (); i++) if (notes[i].start < minStart) minStart = notes[i].start;
	return minStart >= relBeat - 0.5 ? relBeat : 0;
}

template <class T> static void stableSortByFrom (Vec<const T *> &v) { v.sort ([] (const T *a, const T *b) { return a->fromMeasure < b->fromMeasure; }); }

static void place (Project &p, const AArrangement &a, bool fixRiffNotes, bool append, bool overlay, const Names &nm)
{
	int barTemps = p.barBeats ();
	bool keepExisting = append || overlay;
	bool reuseTracks = append && !overlay;

	double baseBeats = 0;
	if (append && !overlay)
	{
		double maxEnd = 0;
		for (int t = 0; t < p.tracks.size (); t++) maxEnd = dmax (maxEnd, p.trackEnd (p.tracks[t]));
		baseBeats = iceil (maxEnd / imax (1, barTemps) - 1e-6) * (double) barTemps;
	}

	// sections -> the section of each bar (1-based), the markers
	Vec<const char *> secOfMeasure; secOfMeasure.push (0);
	Vec<SecStart> markerSecs;
	int m = 1, maxSecMeasure = 0;
	for (int i = 0; i < a.sections.size (); i++)
	{
		int len = imax (1, a.sections[i].measures);
		const char *name = a.sections[i].name.null () ? 0 : a.sections[i].name.c ();
		for (int k = 0; k < len; k++) { while (secOfMeasure.size () <= m + k) secOfMeasure.push (0); secOfMeasure[m + k] = name; maxSecMeasure = imax (maxSecMeasure, m + k); }
		SecStart s; s.name = name; s.startBar = m - 1; markerSecs.push (s);
		m += len;
	}
	addSectionMarkers (p, markerSecs, baseBeats);

	// the articulation of each section: a custom one-bar motif (saved as a user chord style too), else a named style
	NameMap<int> styleOfSection;
	NameMap<Vec<RiffNote> > motifOfSection, cellOfSection;
	NameMap<Str> nameOfSection;
	for (int i = 0; i < a.articulation.size (); i++)
	{
		const AArt &art = a.articulation[i];
		if (blank (art.section)) continue;
		if (art.motif.size () > 0)
		{
			Vec<RiffNote> mnotes = buildArticulationNotes (art.motif, ART_SPQ);
			motifOfSection.set (art.section, mnotes);
			Str nmx; uniqueUserStyleName (p, art.name, art.section, nmx);
			nameOfSection.set (art.section, nmx);
			int beats = imax (1, barTemps);
			UserStyle us; us.name = nmx; us.spb = ART_SPQ; us.beats = beats; us.notes = mnotes; us.hasNotes = true;
			us.slices = notes::toSlices (mnotes, imax (1, beats * ART_SPQ));
			p.userChordStyles.push (us);
		}
		else if (!blank (art.style)) styleOfSection.set (art.section, styleIndex (art.style));
		if (art.cell.size () > 0) cellOfSection.set (art.section, buildMelodicCellNotes (art.cell, ART_SPQ));
	}

	if (!keepExisting) p.tracks.clear ();
	int chordTrack = ensureChordTrack (p, nm);
	if (!keepExisting && a.chordInstrument >= 0 && a.chordInstrument <= 127) p.tracks[chordTrack].instrument = a.chordInstrument;
	double chordEndBefore = p.trackEnd (p.tracks[chordTrack]);
	int chordPreCount = p.tracks[chordTrack].items.size ();

	// the chords: one module a bar (the last chord held over a bar without one; several in a bar share it)
	int lastMeasure = 0;
	for (int i = 0; i < a.chords.size (); i++) lastMeasure = imax (lastMeasure, imax (1, a.chords[i].measure));
	int totalMeasures = imax (lastMeasure, maxSecMeasure);
	Vec<const AChord *> chordAtMeasure; chordAtMeasure.resize (totalMeasures + 1);
	for (int i = 0; i <= totalMeasures; i++) chordAtMeasure[i] = 0;

	// the polychord option: a suite of PolyChord modules (one per entry, 1..4 bars), the progression unchanged
	Vec<PolySpan<APolyChordSpec> > chordPlan;
	if (a.hasPolyChords) chordPlan = plan (a.polyChords, totalMeasures, true);
	bool polyPath = chordPlan.size () > 0;
	Vec<PolyChordModule *> polyModules;
	Vec<int> polyOfMeasure; polyOfMeasure.resize (totalMeasures + 2);
	for (int i = 0; i < polyOfMeasure.size (); i++) polyOfMeasure[i] = -1;
	for (int i = 0; i < chordPlan.size (); i++)
	{
		polyModules.push (buildPolyChords (*chordPlan[i].spec));
		for (int mm = chordPlan[i].from; mm < chordPlan[i].from + chordPlan[i].measures && mm < polyOfMeasure.size (); mm++) polyOfMeasure[mm] = i;
	}

	const PatternModule *prevPg = 0; const AChord *lastSingle = 0;
	for (int meas = 1; meas <= totalMeasures; meas++)
	{
		PolyChordModule *polyChord = polyPath && polyOfMeasure[meas] >= 0 ? polyModules[polyOfMeasure[meas]] : 0;
		Vec<const AChord *> list;
		for (int i = 0; i < a.chords.size (); i++) if (imax (1, a.chords[i].measure) == meas) list.push (&a.chords[i]);
		chordAtMeasure[meas] = list.size () > 0 ? list[0] : lastSingle;
		if (list.size () > 0)
		{
			int k = list.size ();
			for (int ci = 0; ci < k; ci++)
			{
				int part = imax (1, barTemps / k + (ci < barTemps % k ? 1 : 0));
				if (polyChord) polyChord->chords.push (polyChordItem (p, *list[ci], part));
				else if (!polyPath) prevPg = addAiChord (p, chordTrack, *list[ci], part, prevPg);
				lastSingle = list[ci];
			}
		}
		else if (lastSingle)
		{
			if (polyChord) polyChord->chords.push (polyChordItem (p, *lastSingle, barTemps));
			else if (!polyPath) prevPg = addAiChord (p, chordTrack, *lastSingle, barTemps, prevPg);
		}
	}
	// the accompaniment lives on its own instrument track (the chord lane is silent): one block a section
	if (!polyPath && totalMeasures > 0)
		addAccompaniment (p, a, barTemps, totalMeasures, secOfMeasure, styleOfSection, motifOfSection, nameOfSection, cellOfSection,
				  append && !overlay ? baseBeats : 0, nm);

	if (polyPath)
	{
		double cur = 0;
		for (int i = 0; i < chordPlan.size (); i++)
		{
			PolyChordModule *mod = polyModules[i];
			int targetBeats = chordPlan[i].measures * barTemps;
			if (!fitChordSpan (*mod, targetBeats)) { delete mod; polyModules[i] = 0; continue; }	// no chord in that span
			double start = (chordPlan[i].from - 1) * (double) barTemps;
			p.tracks[chordTrack].items.push (Item (dmax (0, start - cur), mod));
			polyModules[i] = 0;
			cur = dmax (start, cur) + polyChordTotalBeats (*mod);
		}
		// the voicing of every item is computed (Inversion / OctaveShift), chained over the whole chord track
		revoiceTrack (p.tracks[chordTrack]);
	}
	// develop: the new chords start at baseBeats
	if (append && p.tracks[chordTrack].items.size () > chordPreCount)
		p.tracks[chordTrack].items[chordPreCount].silenceBefore += dmax (0, baseBeats - chordEndBefore);

	// melodic lines -> one instrument track per role, in 4-bar blocks
	{
		const int spq = 4;
		NameMap<Vec<const ALine *> > groups; Vec<Str> order;
		for (int i = 0; i < a.melodicLines.size (); i++)
		{
			const ALine &line = a.melodicLines[i];
			Str role; if (blank (line.track)) role = nm.melody; else trimTo (line.track, role);
			Vec<const ALine *> *g = groups.find (role);
			if (!g) { Vec<const ALine *> v; v.push (&line); groups.set (role, v); order.push (role); }
			else g->push (&line);
		}
		for (int r = 0; r < order.size (); r++)
		{
			Vec<const ALine *> lines = *groups.find (order[r]);
			stableSortByFrom (lines);
			int instr = 73;
			for (int i = 0; i < lines.size (); i++) if (lines[i]->instrument >= 0 && lines[i]->instrument <= 127) { instr = lines[i]->instrument; break; }
			int track = getOrCreateInstrTrack (p, order[r], instr, reuseTracks);
			double cursor = p.trackEnd (p.tracks[track]);
			for (int i = 0; i < lines.size (); i++)
			{
				const ALine &line = *lines[i];
				double lineStart = baseBeats + (imax (1, line.fromMeasure) - 1) * (double) barTemps;
				int lineBars = imax (1, line.measures);
				int contour = contourIndex (line.contour), anchor = anchorIndex (line.anchor);
				for (int b = 0; b < lineBars; b += 4)
				{
					int cb = imin (4, lineBars - b);
					int cBeats = cb * barTemps, cSlices = cBeats * spq;
					double startBeat = lineStart + b * barTemps;
					MelodicLineModule *ml = new MelodicLineModule;
					ml->beatsPerBar = cBeats; ml->voiceCount = 1; ml->contour = contour; ml->anchor = anchor; ml->registerShift = line.reg;
					ml->rhythm.setNotes (buildRhythmNotes (line.durations, cSlices, spq), spq, cSlices);
					p.tracks[track].items.push (Item (dmax (0, startBeat - cursor), ml));
					cursor = startBeat + cBeats;
				}
			}
		}
	}

	// riffs (explicit notes) -> one instrument track per role, Riff objects played by PlayRiff modules, 4-bar blocks
	if (a.riffs.size () > 0)
	{
		const int rspq = 24;
		PcList scale = scalePcs (p.key);
		NameMap<Vec<const ARiff *> > groups; Vec<Str> order;
		for (int i = 0; i < a.riffs.size (); i++)
		{
			const ARiff &rf = a.riffs[i];
			Str role; if (blank (rf.track)) role = nm.melody; else trimTo (rf.track, role);
			Vec<const ARiff *> *g = groups.find (role);
			if (!g) { Vec<const ARiff *> v; v.push (&rf); groups.set (role, v); order.push (role); }
			else g->push (&rf);
		}
		for (int r = 0; r < order.size (); r++)
		{
			Vec<const ARiff *> list = *groups.find (order[r]);
			stableSortByFrom (list);
			int instr = 73;
			for (int i = 0; i < list.size (); i++) if (list[i]->instrument >= 0 && list[i]->instrument <= 127) { instr = list[i]->instrument; break; }
			int track = getOrCreateInstrTrack (p, order[r], instr, reuseTracks);
			double cursor = p.trackEnd (p.tracks[track]);
			for (int i = 0; i < list.size (); i++)
			{
				const ARiff &rf = *list[i];
				int relBeat = (imax (1, rf.fromMeasure) - 1) * barTemps;
				double rfStart = baseBeats + relBeat;
				int rfBars = imax (1, rf.measures);
				int lenSlices = rfBars * barTemps * rspq;
				double sub = absoluteShift (rf.notes, relBeat);
				Vec<RiffNote> full = buildRiffNotes (rf.notes, lenSlices, rspq, barTemps * rspq, sub);
				if (fixRiffNotes) correctRiffHarmony (p, full, relBeat, barTemps, rspq, chordAtMeasure, scale);
				int barSlices = barTemps * rspq;
				for (int b = 0; b < rfBars; b += 4)
				{
					int cb = imin (4, rfBars - b);
					int blockStart = b * barSlices, blockLen = cb * barSlices;
					Riff riff;
					riff.id = newId ();
					Text name; name.add (order[r]); name.add (" "); name.add (rf.section.c ()); name.take (riff.name);
					riff.spq = rspq; riff.lengthSlices = blockLen;
					for (int k = 0; k < full.size (); k++)
					{
						const RiffNote &n = full[k];
						if (n.start < blockStart || n.start >= blockStart + blockLen) continue;
						int ns = n.start - blockStart, nl = imin (n.length, blockLen - ns);
						if (nl >= 1) riff.notes.push (RiffNote (n.note, ns, nl));
					}
					PlayRiffModule *pr = new PlayRiffModule; pr->riffId = riff.id;
					p.riffs.push (riff);
					double startBeat = rfStart + b * barTemps;
					p.tracks[track].items.push (Item (dmax (0, startBeat - cursor), pr));
					cursor = startBeat + cb * barTemps;
				}
			}
		}
	}

	// the polyrhythmic drums (a suite of PolyDrum modules; a span with no entry stays silent), else the drum phrases
	Vec<PolySpan<APolyDrumSpec> > drumPlan;
	if (a.hasPolyDrums) drumPlan = plan (a.polyDrums, imax (1, totalMeasures), false);
	if (drumPlan.size () > 0)
	{
		int dtrack = getOrCreateDrumTrack (p, reuseTracks, nm);
		double cursor = p.trackEnd (p.tracks[dtrack]);
		for (int i = 0; i < drumPlan.size (); i++)
		{
			PolyDrumModule *pdm = buildPolyDrums (*drumPlan[i].spec, drumPlan[i].measures * barTemps);
			if (pdm->kit > 0) p.tracks[dtrack].drumKit = pdm->kit;
			double start = baseBeats + (drumPlan[i].from - 1) * (double) barTemps;
			p.tracks[dtrack].items.push (Item (dmax (0, start - cursor), pdm));
			cursor = dmax (start, cursor) + polyDrumTotalBeats (*pdm);
		}
	}
	else if (a.drums.size () > 0)
	{
		const int dspq = 4;
		int dtrack = getOrCreateDrumTrack (p, reuseTracks, nm);
		Vec<const ARiff *> dlist;
		for (int i = 0; i < a.drums.size (); i++) dlist.push (&a.drums[i]);
		stableSortByFrom (dlist);
		double cursor = p.trackEnd (p.tracks[dtrack]);
		for (int i = 0; i < dlist.size (); i++)
		{
			const ARiff &rf = *dlist[i];
			int relBeat = (imax (1, rf.fromMeasure) - 1) * barTemps;
			double startBeat = baseBeats + relBeat;
			int totalBeats = imax (1, rf.measures) * barTemps;
			int lenSlices = totalBeats * dspq;
			double sub = absoluteShift (rf.notes, relBeat);
			Vec<RiffNote> dnotes;
			for (int k = 0; k < rf.notes.size (); k++)
			{
				int start = imax (0, iround ((rf.notes[k].start - sub) * dspq));
				if (start >= lenSlices) continue;
				int len = imax (1, iround (rf.notes[k].length * dspq));
				if (start + len > lenSlices) len = lenSlices - start;
				if (len < 1) continue;
				dnotes.push (RiffNote (laneForKey (rf.notes[k].a), start, len));	// GM key -> lane
			}
			// a groove = one short motif looped: the declared motifBars / repeats, else the content's own length
			int barSlices = imax (1, barTemps * dspq);
			int sectionBars = imax (1, rf.measures);
			int unitLen, reps;
			if (rf.motifBars > 0)
			{
				unitLen = rf.motifBars * barSlices;
				reps = rf.repeats > 0 ? rf.repeats : imax (1, sectionBars / imax (1, rf.motifBars));
			}
			else
			{
				int maxEnd = 0; for (int k = 0; k < dnotes.size (); k++) maxEnd = imax (maxEnd, dnotes[k].end ());
				int contentBars = imax (1, iceil ((double) maxEnd / barSlices));
				unitLen = contentBars * barSlices; reps = 1;
				if (unitLen < lenSlices && lenSlices % unitLen == 0) reps = lenSlices / unitLen;
				else unitLen = lenSlices;
			}
			Vec<RiffNote> unitNotes;
			for (int k = 0; k < dnotes.size (); k++) if (dnotes[k].start < unitLen) unitNotes.push (RiffNote (dnotes[k].note, dnotes[k].start, imin (dnotes[k].length, unitLen - dnotes[k].start)));
			Vec<RiffNote> u2; int u2len, u2reps;
			compressPeriodic (unitNotes, unitLen, dspq, u2, &u2len, &u2reps);
			bool canSplit = u2len > 0 && barSlices % u2len == 0 && sectionBars > 4;
			if (!canSplit)
			{
				DrumModule *dpm = new DrumModule;
				dpm->kit = 0; dpm->style = DRUM_CUSTOM_STYLE; dpm->beatsPerBar = imax (1, u2len / dspq); dpm->repeats = reps * u2reps;
				dpm->custom.setNotes (u2, dspq, u2len);
				p.tracks[dtrack].items.push (Item (dmax (0, startBeat - cursor), dpm));
				cursor = startBeat + totalBeats;
			}
			else
			{
				int perBar = barSlices / u2len;
				for (int b = 0; b < sectionBars; b += 4)
				{
					int cb = imin (4, sectionBars - b);
					DrumModule *dpm = new DrumModule;
					dpm->kit = 0; dpm->style = DRUM_CUSTOM_STYLE; dpm->beatsPerBar = imax (1, u2len / dspq); dpm->repeats = perBar * cb;
					dpm->custom.setNotes (u2, dspq, u2len);
					double bStart = startBeat + b * barTemps;
					p.tracks[dtrack].items.push (Item (dmax (0, bStart - cursor), dpm));
					cursor = bStart + cb * barTemps;
				}
			}
		}
	}
	for (int i = 0; i < polyModules.size (); i++) delete polyModules[i];	// (only the unplaced ones are left)
	ensureChordTrack (p, nm);						// re-pin the chord track at the bottom
}

// ---- the element replies: a drum groove (ApplyAiDrum), a riff (ApplyAiRiff) ------------------------------------------------------
struct AGroove { int motifBars, repeats; Vec<ANote> notes; };
static bool parseGroove (const char *reply, AGroove &g, char *err, int errcap)
{
	json::Doc d;
	if (!parseRoot (reply, d, err, errcap)) return false;
	const V *o = &d.root ();
	// Onyx: a groove wrapped as {"drums": [ {...} ]} (the add-drums shape) is taken too
	if (!o->find ("notes"))
	{
		Vec<const V *> es; elems (o->find ("drums"), es);
		if (es.size () > 0 && es[0]->isObj ()) o = es[0];
	}
	g.motifBars = gInt (*o, "motifBars", 0); g.repeats = gInt (*o, "repeats", 0);
	rdNotes (o->find ("notes"), kPitchKeys, 0, g.notes);
	if (g.notes.size () == 0) { setErr (err, errcap, "No drum notes in the reply."); return false; }
	return true;
}

struct ARiffReply { Vec<ANote> notes; Vec<AChord> chords; Str articulation; };
static bool parseRiffReply (const char *reply, ARiffReply &r, char *err, int errcap)
{
	json::Doc d;
	if (!parseRoot (reply, d, err, errcap)) return false;
	const V &o = d.root ();
	rdNotes (o.find ("notes"), kPitchKeys, 0, r.notes);
	rdChords (o.find ("chords"), r.chords);
	r.articulation = gStr (o, "articulation");
	if (r.notes.size () == 0) { setErr (err, errcap, "No notes in the reply."); return false; }
	return true;
}

// ---- AiPolyPlacer.BuildFresh ---------------------------------------------------------------------------------------------------------
struct APolyLayer { int lane, voice, hits, steps, rotation; bool legato; };
struct APolyrhythm
{
	int bpm; Str tonic, mode;
	bool hasDrum, hasMelodic, hasChord;
	int kit, instrument; Vec<APolyLayer> drumLayers, melLayers;
	Str chordRoot, chordQuality; int chordOctave;
	APolyrhythm () : bpm (108), tonic ("C"), mode ("major"), hasDrum (false), hasMelodic (false), hasChord (false), kit (0), instrument (12),
		chordRoot ("C"), chordQuality ("major"), chordOctave (3) {}
};
static bool parsePolyrhythm (const char *reply, APolyrhythm &a, char *err, int errcap)
{
	json::Doc d;
	if (!parseRoot (reply, d, err, errcap)) return false;
	const V &r = d.root ();
	a.bpm = gInt (r, "bpm", 108);
	const V *k = r.find ("key");
	if (k && k->isObj ()) { Str t = gStr (*k, "tonic"), m = gStr (*k, "mode"); if (!t.null ()) a.tonic = t; if (!m.null ()) a.mode = m; }
	const V *dr = r.find ("drum");
	if (dr && dr->isObj ())
	{
		a.hasDrum = true; a.kit = gInt (*dr, "kit", 0);
		Vec<const V *> ls; elems (dr->find ("layers"), ls);
		for (int i = 0; i < ls.size (); i++) if (ls[i]->isObj ())
		{
			APolyLayer l; l.lane = gInt (*ls[i], "lane", 0); l.voice = 0; l.hits = gInt (*ls[i], "hits", 3); l.steps = gInt (*ls[i], "steps", 8);
			l.rotation = gInt (*ls[i], "rotation", 0); l.legato = false; a.drumLayers.push (l);
		}
	}
	const V *me = r.find ("melodic");
	if (me && me->isObj ())
	{
		a.hasMelodic = true; a.instrument = gInt (*me, "instrument", 12);
		Vec<const V *> ls; elems (me->find ("layers"), ls);
		for (int i = 0; i < ls.size (); i++) if (ls[i]->isObj ())
		{
			APolyLayer l; l.lane = 0; l.voice = gInt (*ls[i], "voice", 0); l.hits = gInt (*ls[i], "hits", 3); l.steps = gInt (*ls[i], "steps", 8);
			l.rotation = gInt (*ls[i], "rotation", 0); l.legato = gBool (*ls[i], "legato", false); a.melLayers.push (l);
		}
	}
	const V *ch = r.find ("chord");
	if (ch && ch->isObj ())
	{
		a.hasChord = true;
		Str t = gStr (*ch, "root"), q = gStr (*ch, "quality");
		if (!t.null ()) a.chordRoot = t;
		if (!q.null ()) a.chordQuality = q;
		a.chordOctave = gInt (*ch, "octave", 3);
	}
	if (a.drumLayers.size () == 0 && a.melLayers.size () == 0) { setErr (err, errcap, "No rings in the reply."); return false; }
	return true;
}

// AiPolyPlacer.ParseTonic: "C", "Do", "Ré", "F#", "Bb", "H"...
static void polyTonic (const char *s0, int *letter, int *acc)
{
	*letter = 0; *acc = 0;
	Str s; trimTo (s0, s);
	if (s.empty ()) return;
	char t[32]; snprintf (t, sizeof t, "%s", s.c ());
	int n = (int) strlen (t);
	if (n > 1)
	{
		if (t[n - 1] == '#') { *acc = 1; t[--n] = 0; }
		else if (n >= 4 && !strcmp (t + n - 3, "♯")) { *acc = 1; n -= 3; t[n] = 0; }
		else if (t[n - 1] == 'b') { *acc = -1; t[--n] = 0; }
		else if (n >= 4 && !strcmp (t + n - 3, "♭")) { *acc = -1; n -= 3; t[n] = 0; }
	}
	Str u; trimTo (t, u);
	static const struct { const char *w; int l; } names[] = {
		{ "c", 0 }, { "do", 0 }, { "d", 1 }, { "re", 1 }, { "ré", 1 }, { "e", 2 }, { "mi", 2 }, { "f", 3 }, { "fa", 3 },
		{ "g", 4 }, { "sol", 4 }, { "a", 5 }, { "la", 5 }, { "b", 6 }, { "si", 6 }, { "h", 6 } };
	for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) if (ieq (u, names[i].w)) { *letter = names[i].l; return; }
}
static int polyQuality (const char *q)
{
	Str t; trimTo (q, t);
	static const struct { const char *w; int q; } names[] = {
		{ "min", 1 }, { "minor", 1 }, { "mineur", 1 }, { "m", 1 }, { "dim", 2 }, { "diminished", 2 }, { "diminué", 2 }, { "°", 2 },
		{ "aug", 3 }, { "augmented", 3 }, { "augmenté", 3 }, { "+", 3 }, { "sus2", 4 }, { "sus4", 5 } };
	for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) if (ieq (t, names[i].w)) return names[i].q;
	return 0;
}

static void buildPolyrhythm (Project &p, const APolyrhythm &a, int durationBeats, const Names &nm)
{
	p = Project ();
	int dur = imax (1, durationBeats);
	double bpm = dmax (30, dmin (400, a.bpm > 0 ? a.bpm : 108));
	p.tempo.clear (); TempoChange tc = { 0, bpm }; p.tempo.push (tc);
	int letter, acc; polyTonic (a.tonic, &letter, &acc);
	Str md; trimTo (a.mode, md);
	p.key.tonicLetter = letter; p.key.accidental = acc; p.key.fullMode = -1;
	p.key.mode = (md.len () >= 3 && (md.c ()[0] | 32) == 'm' && (md.c ()[1] | 32) == 'i' && (md.c ()[2] | 32) == 'n') ? 1 : 0;
	p.timeSigNum = 4; p.timeSigDen = 4;

	int cycleBeats = imax (2, imin (dur, 8));
	int repeats = imax (1, iround (dur / (double) cycleBeats));
	PolyDrumModule *pd = new PolyDrumModule;
	pd->kit = imax (0, imin (kitCount () - 1, a.kit));
	pd->beats = cycleBeats; pd->repeats = repeats;
	for (int i = 0; i < a.drumLayers.size (); i++)
	{
		const APolyLayer &l = a.drumLayers[i];
		int steps = imax (2, imin (32, l.steps)), hits = imax (1, imin (steps, l.hits));
		EuclidLayer e; e.lane = imax (0, imin (DRUM_LANES - 1, l.lane)); e.hits = hits; e.steps = steps; e.rotation = imod (l.rotation, steps);
		pd->layers.push (e);
	}
	Track dt; dt.name = nm.polyPerc; dt.type = TRACK_DRUM; dt.instrument = 0; dt.drumKit = pd->kit; dt.volume = 1.0;
	dt.items.push (Item (0, pd));
	p.tracks.push (move (dt));

	MelodicPolyModule *mp = new MelodicPolyModule;
	mp->beats = cycleBeats; mp->repeats = repeats;
	for (int i = 0; i < a.melLayers.size (); i++)
	{
		const APolyLayer &l = a.melLayers[i];
		int steps = imax (2, imin (32, l.steps)), hits = imax (1, imin (steps, l.hits));
		EuclidVoice v; v.voice = imax (0, imin (MelodicLineModule::MaxVoices - 1, l.voice)); v.hits = hits; v.steps = steps;
		v.rotation = imod (l.rotation, steps); v.legato = l.legato;
		mp->layers.push (v);
	}
	if (mp->layers.size () == 0) { EuclidVoice v; v.voice = 0; v.hits = 3; v.steps = 8; mp->layers.push (v); }
	for (int i = 0; i < mp->layers.size (); i++) mp->layers[i].voice = i;		// MelodicEuclid.Renumber
	int instr = a.hasMelodic ? a.instrument : 12;
	if (instr < 0 || instr >= 128) instr = 12;
	Track mt; mt.name = nm.polyLine; mt.type = TRACK_INSTRUMENT; mt.instrument = instr; mt.volume = 0.9;
	mt.items.push (Item (0, mp));
	p.tracks.push (move (mt));

	// the held chord (optional): the harmony the melodic rings read
	if (a.hasChord)
	{
		int cl, ca; polyTonic (a.chordRoot, &cl, &ca);
		static const int letterPc[7] = { 0, 2, 4, 5, 7, 9, 11 };
		PatternModule *pg = new PatternModule;
		pg->root = imod (letterPc[cl] + ca, 12);
		pg->octave = imax (1, imin (7, a.chordOctave > 0 ? a.chordOctave : 3));
		pg->quality = polyQuality (a.chordQuality); pg->inversion = 0; pg->degree = 0;
		pg->style = 0; pg->beatsPerBar = imax (1, dur); pg->repeats = 1; pg->bass = true; pg->bassPerBeat = false;
		Track ct; ct.name = nm.chords; ct.type = TRACK_CHORD; ct.instrument = 46; ct.volume = 0.55;
		ct.items.push (Item (0, pg));
		p.tracks.push (move (ct));
	}
}

// ---- the public entry points -------------------------------------------------------------------------------------------------------------
static int countRings (const Vec<APolyChordSpec> &v) { int n = 0; for (int i = 0; i < v.size (); i++) n += v[i].layers.size (); return n; }
static int countRings (const Vec<APolyDrumSpec> &v) { int n = 0; for (int i = 0; i < v.size (); i++) n += v[i].layers.size (); return n; }

bool aiCheckReply (const AiRequest &r, const char *replyText, char *summary, int cap)
{
	char err[200] = "";
	switch (r.kind)
	{
	case AI_COMPOSE: case AI_DEVELOP: case AI_ADD_TRACK: case AI_ADD_DRUMS:
	{
		AArrangement a;
		bool piece = r.kind == AI_COMPOSE || r.kind == AI_DEVELOP;
		if (!parseArrangement (replyText, piece, a, err, sizeof err)) { setErr (summary, cap, err); return false; }
		Text t;
		if (piece)
			t.fmt ("OK: %d section(s), %d chord(s), %d melodic line(s), %d riff(s), %d drum part(s).", a.sections.size (), a.chords.size (),
			       a.melodicLines.size (), a.riffs.size (), a.drums.size ());
		else
		{
			if (a.melodicLines.size () + a.riffs.size () + a.drums.size () == 0) { setErr (summary, cap, "Nothing to add in the reply."); return false; }
			t.fmt ("OK: %d melodic line(s), %d riff(s), %d drum part(s).", a.melodicLines.size (), a.riffs.size (), a.drums.size ());
		}
		if (a.hasPolyChords) t.fmt (" Polyrhythmic chords: %d block(s), %d ring(s).", a.polyChords.size (), countRings (a.polyChords));
		if (a.hasPolyDrums) t.fmt (" Polyrhythmic drums: %d block(s), %d ring(s).", a.polyDrums.size (), countRings (a.polyDrums));
		setErr (summary, cap, t.c ());
		return true;
	}
	case AI_DRUM_GROOVE:
	{
		AGroove g;
		if (!parseGroove (replyText, g, err, sizeof err)) { setErr (summary, cap, err); return false; }
		char b[160]; snprintf (b, sizeof b, "OK: %d drum note(s), a %d-bar motif x %d.", g.notes.size (), g.motifBars > 0 ? g.motifBars : 1, g.repeats > 0 ? g.repeats : 1);
		setErr (summary, cap, b);
		return true;
	}
	case AI_RIFF:
	{
		ARiffReply rr;
		if (!parseRiffReply (replyText, rr, err, sizeof err)) { setErr (summary, cap, err); return false; }
		char b[160]; snprintf (b, sizeof b, "OK: %d note(s), %d chord(s).", rr.notes.size (), rr.chords.size ());
		setErr (summary, cap, b);
		return true;
	}
	case AI_POLYRHYTHM:
	{
		APolyrhythm a;
		if (!parsePolyrhythm (replyText, a, err, sizeof err)) { setErr (summary, cap, err); return false; }
		char b[160]; snprintf (b, sizeof b, "OK: %d drum ring(s), %d melodic ring(s)%s.", a.drumLayers.size (), a.melLayers.size (), a.hasChord ? ", a held chord" : "");
		setErr (summary, cap, b);
		return true;
	}
	}
	setErr (summary, cap, "Unknown AI request.");
	return false;
}

bool aiApplyReply (Project &p, const AiRequest &r, const char *replyText, char *err, int errcap)
{
	const Names &nm = r.english ? kNamesEn : kNamesFr;
	if (!replyText) replyText = "";
	switch (r.kind)
	{
	case AI_COMPOSE:
	{
		AArrangement a;
		if (!parseArrangement (replyText, true, a, err, errcap)) return false;
		// AiArrangementPlacer.BuildFresh: a new project, its meter / key / tempo from the reply
		Project np;
		int num = a.hasMeter ? a.num : 4; if (num < 1) num = 4;
		int den = a.hasMeter ? a.den : 4; if (den != 2 && den != 4 && den != 8 && den != 16) den = 4;
		np.timeSigNum = num; np.timeSigDen = den;
		np.key = a.hasKey ? parseKey (a.tonic.null () ? 0 : a.tonic.c (), a.mode.null () ? 0 : a.mode.c ()) : Key ();
		if (a.bpm >= 20 && a.bpm <= 400 && np.tempo.size () > 0) np.tempo[0].bpm = a.bpm;
		place (np, a, true, false, false, nm);
		p = np;
		return true;
	}
	case AI_DEVELOP:
	{
		AArrangement a;
		if (!parseArrangement (replyText, true, a, err, errcap)) return false;
		place (p, a, true, true, false, nm);
		return true;
	}
	case AI_ADD_TRACK:
	case AI_ADD_DRUMS:
	{
		AArrangement a;
		if (!parseArrangement (replyText, false, a, err, errcap)) return false;
		if (a.melodicLines.size () + a.riffs.size () + a.drums.size () == 0 && !a.hasPolyDrums)
		{ setErr (err, errcap, "Nothing to add in the reply."); return false; }
		place (p, a, false, false, true, nm);
		return true;
	}
	case AI_DRUM_GROOVE:
	{
		AGroove g;
		if (!parseGroove (replyText, g, err, errcap)) return false;
		int barTemps = p.barBeats ();
		const int dspq = 4;
		int barSlices = imax (1, barTemps * dspq);
		int motifBars = g.motifBars > 0 ? g.motifBars : 1;
		int unitLen = motifBars * barSlices;
		int reps = g.repeats > 0 ? g.repeats : 1;
		Vec<RiffNote> notes;
		for (int i = 0; i < g.notes.size (); i++)
		{
			int start = imax (0, iround (g.notes[i].start * dspq));
			if (start >= unitLen) continue;
			int len = imax (1, iround (g.notes[i].length * dspq));
			if (start + len > unitLen) len = unitLen - start;
			if (len < 1) continue;
			notes.push (RiffNote (laneForKey (g.notes[i].a), start, len));
		}
		if (notes.size () == 0) { setErr (err, errcap, "No drum notes in the reply."); return false; }
		Vec<RiffNote> u2; int u2len, u2reps;
		compressPeriodic (notes, unitLen, dspq, u2, &u2len, &u2reps);
		// the target: the given drum module, else a new one (on the given drum track, else on a new drum track)
		DrumModule *dp = 0;
		if (r.track >= 0 && r.track < p.tracks.size () && r.item >= 0 && r.item < p.tracks[r.track].items.size ())
		{
			Module *m = p.tracks[r.track].items[r.item].module;
			if (m && m->kind == M_DRUMKIT) dp = (DrumModule *) m;
		}
		if (!dp)
		{
			int t = (r.track >= 0 && r.track < p.tracks.size () && p.tracks[r.track].type == TRACK_DRUM) ? r.track : -1;
			if (t < 0) { Track nt; nt.name = nm.drums; nt.type = TRACK_DRUM; nt.instrument = 128; t = addTrackBeforeChords (p, nt); }
			dp = new DrumModule;
			p.tracks[t].items.push (Item (0, dp));
		}
		dp->style = DRUM_CUSTOM_STYLE;
		dp->beatsPerBar = imax (1, u2len / dspq);
		dp->repeats = reps * u2reps;
		dp->custom.setNotes (u2, dspq, u2len);
		return true;
	}
	case AI_RIFF:
	{
		ARiffReply rr;
		if (!parseRiffReply (replyText, rr, err, errcap)) return false;
		int barTemps = p.barBeats ();
		double start, lenBeats; int measures; bool exists;
		riffTarget (p, r, &start, &lenBeats, &measures, &exists);
		const int rspq = 24;
		int totalSlices = imax (1, measures * barTemps * rspq);
		Vec<RiffNote> notes = buildRiffNotes (rr.notes, totalSlices, rspq, barTemps * rspq, 0);
		if (notes.size () == 0) { setErr (err, errcap, "The notes of the reply are out of range."); return false; }
		bool hasChords = chordsUnder (p, start, lenBeats, barTemps).size () > 0;
		int track = r.track, item = r.item;
		if (!exists)
		{
			bool instr = track >= 0 && track < p.tracks.size () && p.tracks[track].type == TRACK_INSTRUMENT;
			if (!instr) { Track t; t.name = nm.riff; t.type = TRACK_INSTRUMENT; t.instrument = 0; track = addTrackBeforeChords (p, t); }
			Riff nr; nr.id = newId (); nr.name = nm.riff; nr.spq = rspq; nr.lengthSlices = totalSlices;
			PlayRiffModule *pr = new PlayRiffModule; pr->riffId = nr.id;
			p.riffs.push (nr);
			p.tracks[track].items.push (Item (0, pr));
			item = p.tracks[track].items.size () - 1;
			start = p.itemStart (p.tracks[track], item);
		}
		Riff *riff = p.riffById (((PlayRiffModule *) p.tracks[track].items[item].module)->riffId);
		riff->notes = notes; riff->lengthSlices = totalSlices; riff->spq = rspq;
		// no chord under the riff: the AI's progression laid on the chord track, from the riff's start
		if (!hasChords && rr.chords.size () > 0)
		{
			int ct = ensureChordTrack (p, nm);
			double chordEndBefore = p.trackEnd (p.tracks[ct]);
			int preCount = p.tracks[ct].items.size ();
			int lastMeasure = 1;
			for (int i = 0; i < rr.chords.size (); i++) lastMeasure = imax (lastMeasure, imax (1, rr.chords[i].measure));
			const PatternModule *prev = 0; const AChord *lastSingle = 0;
			for (int m = 1; m <= imax (measures, lastMeasure); m++)
			{
				Vec<const AChord *> list;
				for (int i = 0; i < rr.chords.size (); i++) if (imax (1, rr.chords[i].measure) == m) list.push (&rr.chords[i]);
				if (list.size () > 0)
				{
					int k = list.size ();
					for (int ci = 0; ci < k; ci++)
					{
						int part = imax (1, barTemps / k + (ci < barTemps % k ? 1 : 0));
						prev = addAiChord (p, ct, *list[ci], part, prev);
						lastSingle = list[ci];
					}
				}
				else if (lastSingle) prev = addAiChord (p, ct, *lastSingle, barTemps, prev);
			}
			if (p.tracks[ct].items.size () > preCount) p.tracks[ct].items[preCount].silenceBefore += dmax (0, start - chordEndBefore);
			revoiceTrack (p.tracks[ct]);
		}
		return true;
	}
	case AI_POLYRHYTHM:
	{
		APolyrhythm a;
		if (!parsePolyrhythm (replyText, a, err, errcap)) return false;
		Project np;
		buildPolyrhythm (np, a, imax (1, r.measures) * 4, nm);	// the user's length wins (Koton forces durationBeats)
		p = np;
		return true;
	}
	}
	setErr (err, errcap, "Unknown AI request.");
	return false;
}

} // namespace kt
