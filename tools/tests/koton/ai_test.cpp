// tools/tests/koton/ai_test.cpp -- Koton's AI composition on the PC, under AddressSanitizer / LeakSanitizer:
// the prompts (every kind, every option, the piece / theme contexts), canned replies shaped like real
// Gemini answers to Koton's prompt (melodic lines + articulation motifs + drums; riffs + a chord voice,
// fenced and half in object form; polychords / polydrums; a develop; an added track; a groove; a riff; a
// polyrhythmic piece; malformed ones) placed on projects that are then checked (tracks, degree-locked
// chords resolving through chordAt and a key change, every module rendering, compileSong, a .kson round
// trip), and the /bin/llm protocol (request JSON -> URL / headers / body per provider; provider answers ->
// text or error; the result line -> aiParseLlmOutput), including a 60+ KB reply end to end.
//   sh tools/tests/koton/ai_run.sh            (--dump DIR: also write the prompts there)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/ai.h"
#include "engine/compile.h"
#include "engine/theory.h"
#include "../../../user/json.hpp"

#define LLM_PROTO_ONLY
#include "../../../user/bin/llm.cpp"

using namespace kt;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
static bool has (const char *s, const char *sub) { return s && sub && strstr (s, sub) != 0; }
static int count (const char *s, const char *sub) { int n = 0; for (const char *p = strstr (s, sub); p; p = strstr (p + 1, sub)) n++; return n; }
static const char *g_dump = 0;
static void dump (const char *name, const Str &sys, const Str &usr)
{
	if (!g_dump) return;
	char path[512]; snprintf (path, sizeof path, "%s/%s.txt", g_dump, name);
	FILE *f = fopen (path, "wb"); if (!f) return;
	fputs (sys.c (), f); fputs ("\n\n", f); fputs (usr.c (), f); fclose (f);
}

// ---- canned replies -----------------------------------------------------------------------------------------
// 1. "Compose" in melodic-line mode (Gemini's minified answer): D minor bossa, 24 bars, 4 sections, articulation
//    motifs with melodic cells, a named style, lines for 3 roles, drums per section. Bar 4 is A7 (a fixed
//    chord: V in minor), bar 17 is E7 (V/V), bar 18 holds two chords, "Min9" is not a Koton name (an alias).
static const char *R_BOSSA = R"JSON({"meter":{"num":4,"den":4},"key":{"tonic":"D","mode":"minor"},"bpm":132,"chordInstrument":24,"sections":[{"name":"Intro","measures":4},{"name":"Theme A","measures":8},{"name":"Bridge","measures":8},{"name":"Outro","measures":4}],"chords":[[1,1,"Min7"],[2,4,"Min7"],[3,2,"m7♭5"],[4,5,"7 (dom)"],[5,1,"Min7"],[6,4,"Min7"],[7,7,"7 (dom)"],[8,3,"Maj7"],[9,6,"Maj7"],[10,2,"m7♭5"],[11,5,"7 (dom)"],[12,1,"Min7"],[13,4,"Min7"],[14,7,"7 (dom)"],[15,3,"Maj7"],[16,6,"Maj7"],[17,2,"7 (dom)"],[18,5,"7sus4"],[18,5,"7 (dom)"],[19,1,"Min9"],[20,5,"7 (dom)"],[21,1,"Min7"],[22,4,"m9"],[23,5,"7 (dom)"],[24,1,"Min7"]],"articulation":[{"section":"Intro","name":"bossa_intro","motif":[[0,0,1.5],[2,0.5,0.5],[3,0.5,0.5],[4,0.5,0.5],[0,1.5,0.5],[2,2,1],[3,2,1],[4,2,1],[0,3,1],[2,3.5,0.5],[3,3.5,0.5],[4,3.5,0.5]],"melodicCell":[[1,0,1],[3,1,1],[5,2,1],[8,3,1]]},{"section":"Theme A","name":"bossa_theme","motif":[[0,0,1],[1,0.5,0.5],[2,0.5,0.5],[3,0.5,0.5],[0,1.5,1],[2,2,0.5],[4,2,0.5],[1,3,0.5],[2,3.5,0.5],[3,3.5,0.5]],"melodicCell":[[5,0,0.5],[6,0.5,0.5],[5,1,1],[3,2,2]]},{"section":"Bridge","style":"Ballade (arpège tenu)"},{"section":"Outro","name":"outro","motif":[[0,0,4],[1,0,4],[2,0,4],[3,0,4],[4,0,4]]}],"melodicLines":[{"section":"Theme A","track":"Lead","instrument":73,"fromMeasure":5,"measures":8,"durations":[1,0.5,0.5,1,-1,0.5,0.5,2,-2],"anchor":"Tierce","contour":"Vague (arcs)","register":0},{"section":"Bridge","track":"Lead","instrument":73,"fromMeasure":13,"measures":8,"durations":[2,1,1,-0.5,0.5,1.5,1.5],"anchor":"Quinte","contour":"Montante","register":0},{"section":"Theme A","track":"Counter-melody","instrument":71,"fromMeasure":5,"measures":8,"durations":[-2,1,1,-2,2],"anchor":"Fondamentale","contour":"2","register":-12},{"section":"Intro","track":"Bass","instrument":32,"fromMeasure":1,"measures":24,"durations":[1.5,0.5,2],"anchor":"Fondamentale","contour":"Statique","register":-24}],"drums":[{"section":"Intro","fromMeasure":1,"measures":4,"motifBars":1,"repeats":4,"notes":[[36,0,0.5],[36,1.5,0.5],[36,2,0.5],[36,3.5,0.5],[37,0,0.25],[37,0.75,0.25],[37,1.5,0.25],[37,2.5,0.25],[37,3,0.25],[42,0,0.5],[42,0.5,0.5],[42,1,0.5],[42,1.5,0.5],[42,2,0.5],[42,2.5,0.5],[42,3,0.5],[42,3.5,0.5]]},{"section":"Theme A","fromMeasure":5,"measures":8,"motifBars":2,"repeats":4,"notes":[[36,0,0.5],[36,1.5,0.5],[36,2,0.5],[36,3.5,0.5],[36,4,0.5],[36,5.5,0.5],[36,6,0.5],[36,7.5,0.5],[37,0,0.25],[37,0.75,0.25],[37,1.5,0.25],[37,2.5,0.25],[37,3,0.25],[37,4.5,0.25],[37,5.25,0.25],[37,6.5,0.25],[37,7,0.25],[70,0,0.25],[70,0.5,0.25],[70,1,0.25],[70,1.5,0.25],[70,2,0.25],[70,2.5,0.25],[70,3,0.25],[70,3.5,0.25],[70,4,0.25],[70,4.5,0.25],[70,5,0.25],[70,5.5,0.25],[70,6,0.25],[70,6.5,0.25],[70,7,0.25],[70,7.5,0.25]]},{"section":"Bridge","fromMeasure":13,"measures":8,"motifBars":1,"repeats":8,"notes":[[36,0,1],[51,0,0.5],[51,1,0.5],[51,2,0.5],[51,3,0.5],[37,1,0.25],[37,3,0.25]]},{"section":"Outro","fromMeasure":21,"measures":4,"motifBars":4,"repeats":1,"notes":[[36,0,1],[42,0,0.5],[42,1,0.5],[36,4,1],[42,4,0.5],[36,8,1],[36,12,2],[49,12,2]]}]})JSON";

// 2. riff mode + "chords as a voice", as a model that did not obey the format perfectly: text and a ```json
//    fence around it, the key in French, the tempo as a string, a chord and a note as objects, one riff with
//    ABSOLUTE starts, one with a one-bar motif (tiled), a wrong note on a beat (B natural over Bb: fixed),
//    drums with no motifBars (the motif found), empty articulation.
static const char *R_RIFFS = "Voici une proposition :\n```json\n" R"JSON({"meter":{"num":3,"den":4},"key":{"tonic":"Si bémol","mode":"majeur"},"bpm":"96","chordInstrument":0,"sections":[{"name":"Verse","measures":8},{"name":"Chorus","measures":8}],"chords":[{"measure":1,"degree":1,"quality":"Maj7"},[2,6,"Min7"],[3,2,"Min7"],[4,5,"7 (dom)"],[5,1,"Maj7"],[6,4,"Maj7"],[7,2,"Min7"],[8,5,"7sus4"],[9,4,"Maj7"],[10,5,"7 (dom)"],[11,3,"Min7"],[12,6,"Min7"],[13,2,"Min7"],[14,5,"7 (dom)"],[15,1,"add9"],[16,1,"Maj7"]],"articulation":[],"riffs":[{"section":"Verse","track":"Lead","instrument":73,"fromMeasure":1,"measures":8,"notes":[[70,0,1],[74,1,1],[77,2,1],{"pitch":76,"start":3,"length":1.5},[74,4.5,0.5],[72,5,1],[71,6,1],[67,7,2],[69,9,1],[70,10,1],[72,11,1],[74,12,3],[77,15,1],[76,16,1],[74,17,1],[72,18,2],[70,21,3]]},{"section":"Chorus","track":"Lead","instrument":73,"fromMeasure":9,"measures":8,"notes":[[75,24,1],[74,25,1],[72,26,1],[70,27,3],[72,30,1],[74,31,1],[75,32,1],[77,33,3],[79,36,1.5],[77,37.5,0.5],[75,38,1],[74,39,2],[70,42,3],[72,45,1],[74,46,1],[70,47,1]]},{"section":"Verse","track":"Accords","instrument":0,"fromMeasure":1,"measures":16,"notes":[[46,0,3],[62,1,0.5],[65,1.5,0.5],[69,2,1]]},{"section":"Chorus","track":"Pad","instrument":89,"fromMeasure":9,"measures":8,"notes":[{"pitch":"62","start":"0","length":"6"},{"pitch":65,"start":6,"len":6},{"pitch":67,"start":12,"dur":6},{"pitch":65,"start":18,"length":6}]}],"drums":[{"section":"Verse","fromMeasure":1,"measures":8,"notes":[[36,0,0.5],[42,1,0.5],[42,2,0.5]]},{"section":"Chorus","fromMeasure":9,"measures":8,"motifBars":2,"repeats":4,"notes":[[36,0,0.5],[38,1,0.5],[38,2,0.5],[36,3,0.5],[38,4,0.5],[49,3,1],[46,5,0.5]]}],"polyChords":null})JSON" "\n```\nBonne écoute !";

// 3. the polyrhythmic options: polyChords as ONE object (the legacy form: the whole piece), polyDrums as an
//    array with a silent gap (bars 5-8, 13-16)
static const char *R_POLY = R"JSON({"meter":{"num":4,"den":4},"key":{"tonic":"A","mode":"minor"},"bpm":100,"chordInstrument":46,"sections":[{"name":"A","measures":8},{"name":"B","measures":8}],"chords":[[1,1,"Min7"],[2,6,"Maj7"],[3,3,"Maj7"],[4,7,"7 (dom)"],[5,1,"Min7"],[6,4,"Min7"],[7,5,"7 (dom)"],[8,1,"Min7"],[9,6,"Maj7"],[10,7,"7 (dom)"],[11,3,"Maj7"],[12,6,"Maj7"],[13,4,"Min7"],[14,5,"7sus4"],[15,5,"7 (dom)"],[16,1,"Min9"]],"articulation":[],"melodicLines":[{"section":"A","track":"Lead","instrument":108,"fromMeasure":1,"measures":16,"durations":[0.5,0.5,1,-1,1],"anchor":"1","contour":"4","register":12}],"polyChords":{"cycleBeats":4,"octave":4,"mode":"perTone","restart":"nearest","openVoicing":true,"layers":[{"toneIndex":0,"contour":0,"hits":3,"steps":8,"rotation":0,"octave":-1,"legato":true},{"toneIndex":1,"hits":5,"steps":12,"rotation":1},{"toneIndex":2,"hits":7,"steps":16,"rotation":2},{"toneIndex":4,"hits":4,"steps":9,"rotation":0,"octave":1}]},"polyDrums":[{"fromMeasure":1,"measures":4,"kit":0,"cycleBeats":4,"layers":[{"lane":29,"hits":3,"steps":8,"rotation":0,"accentLane":31},{"lane":35,"hits":5,"steps":12,"rotation":1},{"lane":40,"hits":7,"steps":16,"rotation":2}]},{"fromMeasure":9,"measures":4,"kit":0,"cycleBeats":4,"layers":[{"lane":0,"hits":4,"steps":16},{"lane":22,"hits":5,"steps":8,"rotation":3},{"lane":34,"hits":9,"steps":16}]}]})JSON";

// 3b. polyChords as an array with gaps: the gaps are filled with the neighbouring entry (the chords must cover the piece)
static const char *R_POLY_GAPS = R"JSON({"meter":{"num":4,"den":4},"key":{"tonic":"C","mode":"major"},"bpm":90,"sections":[{"name":"A","measures":16}],"chords":[[1,1,"Maj7"],[2,6,"Min7"],[3,2,"Min7"],[4,5,"7 (dom)"],[5,1,"Maj7"],[9,4,"Maj7"],[13,5,"7 (dom)"],[16,1,"Maj7"]],"articulation":[],"polyChords":[{"fromMeasure":1,"measures":4,"cycleBeats":3,"layers":[{"toneIndex":0,"hits":3,"steps":8},{"toneIndex":2,"hits":5,"steps":7}]},{"fromMeasure":9,"measures":4,"cycleBeats":5,"mode":"sweep","restart":"tierce","layers":[{"contour":"Montante","hits":4,"steps":9}]}]})JSON";

// 4. develop: 8 more bars, the "Lead" track reused, a new "Strings" role
static const char *R_DEVELOP = R"JSON({"meter":{"num":4,"den":4},"key":{"tonic":"D","mode":"minor"},"bpm":132,"sections":[{"name":"Development","measures":8}],"chords":[[1,6,"Maj7"],[2,7,"7 (dom)"],[3,3,"Maj7"],[4,6,"Maj7"],[5,4,"Min7"],[6,2,"m7♭5"],[7,5,"7 (dom)"],[8,1,"Min7"]],"articulation":[{"section":"Development","name":"dev","motif":[[0,0,2],[1,0,2],[2,0,2],[3,0,2],[0,2,2],[2,2,2],[3,2,2]]}],"melodicLines":[{"section":"Development","track":"Lead","instrument":73,"fromMeasure":1,"measures":8,"durations":[0.5,0.5,0.5,0.5,2],"anchor":"Septième","contour":"Descendante","register":0},{"section":"Development","track":"Strings","instrument":48,"fromMeasure":1,"measures":8,"durations":[4],"anchor":"Tierce","contour":"Statique","register":-12}],"drums":[{"section":"Development","fromMeasure":1,"measures":8,"motifBars":1,"repeats":8,"notes":[[36,0,1],[38,2,1],[42,0,0.5],[42,1,0.5],[42,2,0.5],[42,3,0.5]]}]})JSON";

// 5. add a track: a full melody (a flute) / a rhythm-only line / a drum groove over the whole piece
static const char *R_ADD_MELODY = R"JSON({"riffs":[{"track":"Flute","instrument":73,"fromMeasure":1,"measures":8,"notes":[[74,0,2],[77,2,1],[76,3,1],[74,4,3],[72,8,1],[70,9,1],[69,10,2],[70,12,4]]},{"track":"Flute","instrument":73,"fromMeasure":9,"measures":8,"notes":[[77,0,1],[79,1,1],[81,2,2],[79,4,2],[77,6,2]]}]})JSON";
static const char *R_ADD_LINE = R"JSON({"melodicLines":[{"track":"Cello","instrument":42,"fromMeasure":1,"measures":16,"durations":[2,1,-1],"anchor":"Fondamentale","contour":"Vague (arcs)","register":-12}]})JSON";
static const char *R_ADD_DRUMS = R"JSON({"drums":[{"fromMeasure":1,"measures":16,"motifBars":1,"repeats":16,"notes":[[36,0,0.5],[42,0.5,0.5],[38,1,0.5],[42,1.5,0.5],[36,2,0.5],[42,2.5,0.5]]}]})JSON";

// 6. a groove for one drum module / 7. a riff (with a progression when none is under it)
static const char *R_GROOVE = R"JSON({"motifBars":1,"repeats":8,"notes":[[36,0,0.5],[38,1,0.5],[36,2,0.5],[38,3,0.5],[42,0,0.5],[42,0.5,0.5],[42,1,0.5],[42,1.5,0.5],[42,2,0.5],[42,2.5,0.5],[42,3,0.5],[42,3.5,0.5],[54,1,0.25],[54,3,0.25]]})JSON";
static const char *R_RIFF = R"JSON({"notes":[[62,0,1],[65,1,0.5],[67,1.5,0.5],[69,2,2],[72,4,1],[70,5,1],[69,6,1],[67,7,1]],"chords":[[1,1,"Min7"],[2,4,"Min7"]],"articulation":"Valse (basse-accord-accord)"})JSON";

// 8. a polyrhythmic piece (AiPolyDialog)
static const char *R_POLYRHYTHM = R"JSON({"bpm":112,"key":{"tonic":"Ré","mode":"minor"},"durationBeats":32,"drum":{"kit":0,"layers":[{"lane":29,"hits":3,"steps":8,"rotation":0,"stepSlices":12},{"lane":35,"hits":5,"steps":12,"rotation":1},{"lane":40,"hits":7,"steps":16,"rotation":2},{"lane":0,"hits":4,"steps":16}]},"melodic":{"instrument":108,"layers":[{"voice":0,"hits":3,"steps":7,"rotation":0,"legato":true},{"voice":2,"hits":5,"steps":9,"rotation":2}]},"chord":{"root":"D","quality":"minor","octave":3}})JSON";

// ---- checks on a placed project -------------------------------------------------------------------------------
static int trackNamed (const Project &p, const char *name)
{
	for (int i = 0; i < p.tracks.size (); i++) if (p.tracks[i].name == name) return i;
	return -1;
}

// every module renders notes; the song compiles with events on every sounding track; a .kson round trip is exact
static void checkPlays (const Project &p, const char *what)
{
	int carryDummy = 0; (void) carryDummy;
	for (int t = 0; t < p.tracks.size (); t++)
	{
		const Track &tr = p.tracks[t];
		double cur = 0;
		int carry[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
		for (int i = 0; i < tr.items.size (); i++)
		{
			cur += tr.items[i].silenceBefore;
			const Module *m = tr.items[i].module;
			CHECK (m != 0);
			if (!m) continue;
			CHECK (p.itemLength (tr.items[i]) > 0);
			Riff cell;
			Riff r = renderModule (*m, p, cur, carry, &cell);
			if (tr.type != TRACK_CHORD || m->kind != M_PATTERN)
			{
				if (r.notes.size () == 0) printf ("  (%s: track %s item %d: %s renders no note)\n", what, tr.name.c (), i, m->typeName ());
				CHECK (r.notes.size () > 0);
			}
			cur += p.itemLength (tr.items[i]);
		}
	}
	CompiledSong *cs = compileSong (p, 44100);
	CHECK (cs != 0);
	CHECK (cs->totalSlices >= iceil (p.totalBeats () * CSPB));
	for (int t = 0; t < cs->tracks.size (); t++)
	{
		if (cs->tracks[t].silent) continue;
		int ons = 0; for (int e = 0; e < cs->tracks[t].events.size (); e++) if (cs->tracks[t].events[e].kind == EV_ON) ons++;
		if (!ons) printf ("  (%s: compiled track %d has no note)\n", what, t);
		CHECK (ons > 0);
	}
	delete cs;
	json::Writer w1 (true); saveProject (p, w1);
	Project q; char err[128];
	CHECK (loadProject (w1.data (), w1.size (), q, err, sizeof err));
	json::Writer w2 (true); saveProject (q, w2);
	// (the loader clamps a track's GM program to 0..127: a drum track's 128 -- Koton's DrumIndex -- reads as 127)
	Str a1 (w1.data ());
	for (char *x = (char *) strstr (a1.c (), "\"Instrument\": 128,"); x; x = strstr (x + 1, "\"Instrument\": 128,")) x[16] = '7';
	bool same = !strcmp (a1.c (), w2.data ());
	if (!same && g_dump)
	{
		char path[512];
		snprintf (path, sizeof path, "%s/%s_1.kson", g_dump, what); FILE *f = fopen (path, "wb"); if (f) { fwrite (w1.data (), 1, w1.size (), f); fclose (f); }
		snprintf (path, sizeof path, "%s/%s_2.kson", g_dump, what); f = fopen (path, "wb"); if (f) { fwrite (w2.data (), 1, w2.size (), f); fclose (f); }
	}
	CHECK (same);
}

// the chord track is last; a degree-locked chord's (degree, colour, suspension) resolves to its own quality in
// the key (the colour fix: a Maj7 on a degree is not played as a triad); returns the locked / fixed counts
static void checkChordTrack (const Project &p, int *locked, int *fixed)
{
	*locked = *fixed = 0;
	int ci = p.chordTrackIndex ();
	CHECK (ci == p.tracks.size () - 1);
	if (ci < 0) return;
	for (int i = 0; i < p.tracks[ci].items.size (); i++)
	{
		const Module *m = p.tracks[ci].items[i].module;
		if (m->kind == M_PATTERN)
		{
			const PatternModule *pg = (const PatternModule *) m;
			if (pg->degree >= 0)
			{
				(*locked)++;
				RootQ d = diatonicChord (p.key, pg->degree, pg->diatonicColour, pg->suspension, pg->modeOverride);
				if (d.root != pg->root || d.quality != pg->quality)
					printf ("  (degree %d colour %d: %d/%d resolves to %d/%d)\n", pg->degree, pg->diatonicColour, pg->root, pg->quality, d.root, d.quality);
				CHECK (d.root == pg->root && d.quality == pg->quality);
			}
			else (*fixed)++;
		}
		else if (m->kind == M_POLYCHORD)
		{
			const PolyChordModule *pc = (const PolyChordModule *) m;
			for (int j = 0; j < pc->chords.size (); j++)
			{
				const PolyChordItem &it = pc->chords[j];
				if (it.degree >= 0)
				{
					(*locked)++;
					RootQ d = diatonicChord (p.key, it.degree, it.diatonicColour, it.suspension, it.modeOverride);
					CHECK (d.root == it.root && d.quality == it.quality);
				}
				else (*fixed)++;
			}
		}
	}
}

static bool chordIs (const Project &p, double beat, int root, int quality)
{
	int r = -1, q = -1, inv = 0;
	bool ok = chordAt (p, beat, &r, &q, &inv);
	if (!ok || r != root || q != quality) printf ("  (chordAt %.2f = %d/%d, expected %d/%d)\n", beat, r, q, root, quality);
	return ok && r == root && q == quality;
}

// ---- the tests --------------------------------------------------------------------------------------------------
static void testCompose (Project &out)
{
	AiRequest r; r.kind = AI_COMPOSE; r.style = "bossa nova"; r.measures = 24; r.intention = "une soirée à Rio, mélancolique";
	r.fullMelody = false; r.drums = true;
	Str sys, usr;
	CHECK (aiBuildPrompt (out, r, sys, usr));
	dump ("compose_lines_drums", sys, usr);
	CHECK (has (sys, "Tu es un compositeur assistant. Tu renvoies UNIQUEMENT un objet JSON (aucune prose) décrivant un morceau à poser sur une timeline.\n"));
	CHECK (has (sys, "\"melodicLines\": [ { \"section\": string"));
	CHECK (!has (sys, "\"riffs\": ["));
	CHECK (has (sys, "\"drums\": [ { \"section\": string, \"fromMeasure\": int"));
	CHECK (has (sys, "- BATTERIE ('drums') : un GROOVE"));
	CHECK (has (sys, "en ANGLAIS. Les clés JSON"));
	CHECK (has (sys, "- quality (accords) : Majeur, Mineur, Diminué, Augmenté, Sus2, Sus4, Maj7, Min7, 7 (dom), m7♭5, dim7, 6,"));
	CHECK (has (sys, "- contour : 0=Vague, 1=Montante, 2=Descendante, 3=Statique, 4=Zigzag, 5=Aléatoire, 6=Thue-Morse, 7=L-système, 8=Fractale\n"));
	CHECK (has (sys, "- anchor : 0=Défaut, 1=Fondamentale, 2=Tierce, 3=Quinte, 4=Septième, 5=Neuvième\n"));
	CHECK (has (sys, "- CELLULE MÉLODIQUE ('melodicCell'"));
	CHECK (!has (sys, "polyChords") && !has (sys, "MODÈLE POLYRYTHMIQUE"));
	CHECK (!strcmp (usr, "Compose un morceau dans le style : « bossa nova », d'environ 24 mesures. Intention musicale : « une soirée à Rio, mélancolique ». Choisis tonalité, métrique et tempo cohérents. Renvoie UNIQUEMENT l'objet JSON."));
	Str full; aiFullPrompt (sys, usr, full);
	CHECK (full.len () == sys.len () + 2 + usr.len ());

	char summary[200];
	CHECK (aiCheckReply (r, R_BOSSA, summary, sizeof summary));
	CHECK (has (summary, "OK: 4 section(s), 25 chord(s), 4 melodic line(s), 0 riff(s), 4 drum part(s)."));
	char err[200] = "";
	Project p;
	bool ok = aiApplyReply (p, r, R_BOSSA, err, sizeof err);
	if (!ok) printf ("  compose: %s\n", err);
	CHECK (ok);
	// key / meter / tempo from the reply
	CHECK (p.key.tonicLetter == 1 && p.key.accidental == 0 && p.key.mode == 1);
	CHECK (p.timeSigNum == 4 && p.timeSigDen == 4 && p.mainBpm () == 132);
	// the tracks: accompaniment, the three roles, drums, then the chord track
	CHECK (p.tracks.size () == 6);
	int acc = trackNamed (p, "Accompaniment"), lead = trackNamed (p, "Lead"), cm = trackNamed (p, "Counter-melody"), bass = trackNamed (p, "Bass"), dr = trackNamed (p, "Drums");
	CHECK (acc >= 0 && lead >= 0 && cm >= 0 && bass >= 0 && dr >= 0);
	int ci = p.chordTrackIndex ();
	CHECK (ci == 5 && p.tracks[ci].name == "Chords" && p.tracks[ci].instrument == 24);
	CHECK (p.tracks[acc].instrument == 24 && p.tracks[lead].instrument == 73 && p.tracks[cm].instrument == 71 && p.tracks[bass].instrument == 32);
	CHECK (p.tracks[dr].type == TRACK_DRUM && p.tracks[dr].instrument == 128);
	// the chords: 25 modules (bar 18 holds two), 96 beats; A7 fixed on bar 4, E7 (V/V) fixed on bar 17
	CHECK (p.tracks[ci].items.size () == 25);
	CHECK (p.trackEnd (p.tracks[ci]) == 96);
	int locked, fixed; checkChordTrack (p, &locked, &fixed);
	CHECK (locked >= 15 && fixed >= 5);
	CHECK (chordIs (p, 0.5, 2, 7));			// Dm7
	CHECK (chordIs (p, 12.5, 9, 8));		// A7 (the harmonic minor's V: a fixed chord)
	CHECK (chordIs (p, 28.5, 5, 6));		// Fmaj7 on III (degree-locked, colour 7th)
	CHECK (chordIs (p, 64.5, 4, 8));		// E7 = V/V
	CHECK (chordIs (p, 68.5, 9, 23));		// bar 18, first half: A7sus4
	CHECK (chordIs (p, 70.5, 9, 8));		// bar 18, second half: A7
	CHECK (chordIs (p, 72.5, 2, 17));		// "Min9" -> m9
	{
		const PatternModule *f = (const PatternModule *) p.tracks[ci].items[7].module;	// bar 8: Fmaj7
		CHECK (f->degree == 2 && f->diatonicColour == 2 && f->quality == 6);
		const PatternModule *a7 = (const PatternModule *) p.tracks[ci].items[3].module;
		CHECK (a7->degree == -1 && a7->root == 9 && a7->quality == 8);
		const PatternModule *c1 = (const PatternModule *) p.tracks[ci].items[0].module, *c2 = (const PatternModule *) p.tracks[ci].items[1].module;
		CHECK (c1->voiceLeadMode == 0 && c2->voiceLeadMode == 1);	// NewChordLike: the chain voice-leads from the 2nd
	}
	// the accompaniment: one chord articulation a section, the motifs saved as user chord styles
	CHECK (p.tracks[acc].items.size () == 4);
	{
		const ArticulationModule *a0 = (const ArticulationModule *) p.tracks[acc].items[0].module;
		CHECK (a0->kind == M_ARTICULATION && a0->style == CUSTOM_STYLE && a0->beats == 4 && a0->lengthBeats == 16 && a0->voiceLeadMode == 1);
		CHECK (a0->userStyleName == "bossa_intro" && a0->custom.notes.size () == 12 && a0->custom.spq == 4);
		CHECK (a0->melodic.notes.size () == 16);			// the 4-note cell tiled over the 4 bars
		const ArticulationModule *a2 = (const ArticulationModule *) p.tracks[acc].items[2].module;
		CHECK (a2->style == 19 && a2->lengthBeats == 32);		// "Ballade (arpège tenu)"
	}
	CHECK (p.userChordStyles.size () == 3 && p.userChordStyles[0].name == "bossa_intro" && p.userChordStyles[2].name == "outro");
	// the lines: 4-bar blocks from their bar
	CHECK (p.tracks[lead].items.size () == 4 && p.tracks[lead].items[0].silenceBefore == 16);
	CHECK (p.tracks[bass].items.size () == 6 && p.trackEnd (p.tracks[bass]) == 96);
	{
		const MelodicLineModule *ml = (const MelodicLineModule *) p.tracks[lead].items[0].module;
		CHECK (ml->beatsPerBar == 16 && ml->anchor == 2 && ml->contour == 0 && ml->rhythm.spq == 4);
		const MelodicLineModule *c = (const MelodicLineModule *) p.tracks[cm].items[0].module;
		CHECK (c->contour == 2 && c->anchor == 1 && c->registerShift == -12);
		const MelodicLineModule *l2 = (const MelodicLineModule *) p.tracks[lead].items[2].module;
		CHECK (l2->anchor == 3 && l2->contour == 1);
	}
	// the drums: the intro's one-bar motif x 4, the theme's 2-bar motif, the bridge in 4-bar blocks
	{
		const DrumModule *d0 = (const DrumModule *) p.tracks[dr].items[0].module;
		CHECK (d0->style == DRUM_CUSTOM_STYLE && d0->custom.spq == 4 && moduleBeats (d0, p) == 16);
		CHECK (p.trackEnd (p.tracks[dr]) == 96);
	}
	// the markers
	CHECK (p.markers.size () == 4 && p.markers[1].beat == 16 && p.markers[2].name == "Bridge" && p.markers[3].beat == 80);
	checkPlays (p, "compose");
	// a key change: the degree-locked chords follow, keeping their colour
	Project t = p; Key e; e.tonicLetter = 2; e.mode = 1;
	CHECK (transposeProject (t, e, 0, 1));
	CHECK (chordIs (t, 28.5, 7, 6));		// Fmaj7 -> Gmaj7 (III of E minor)
	CHECK (chordIs (t, 0.5, 4, 7));			// Dm7 -> Em7
	// (a FIXED chord is moved twice by transposeProject -- by the interval, then again by resolveChordDegrees'
	// tonic shift: A7 -> C#7 instead of B7. Koton's C# does the same: ChordModelOps.TransposeProject.)
	out = p;
}

static void testRiffs (Project &out)
{
	AiRequest r; r.kind = AI_COMPOSE; r.style = "valse jazz"; r.measures = 16; r.fullMelody = true; r.chordsVoice = true; r.drums = true;
	Str sys, usr;
	CHECK (aiBuildPrompt (out, r, sys, usr));
	dump ("compose_riffs_chordvoice", sys, usr);
	CHECK (has (sys, "\"riffs\": [ { \"section\": string") && !has (sys, "\"melodicLines\""));
	CHECK (has (sys, "- ACCORDS EN VOIX DÉDIÉE") && !has (sys, "- CELLULE MÉLODIQUE"));
	CHECK (has (sys, "- MÉLODIE = des RIFFS") && !has (sys, "- contour :"));
	r.fullMelody = false;
	CHECK (aiBuildPrompt (out, r, sys, usr));
	dump ("compose_lines_chordvoice", sys, usr);
	CHECK (has (sys, "\"register\": int } ],\n  \"riffs\": ["));	// lines + the riffs block for the chord voice
	r.fullMelody = true;

	char err[200] = "";
	Project p;
	bool ok = aiApplyReply (p, r, R_RIFFS, err, sizeof err);
	if (!ok) printf ("  riffs: %s\n", err);
	CHECK (ok);
	CHECK (p.key.tonicLetter == 6 && p.key.accidental == -1 && p.key.mode == 0);		// "Si bémol" "majeur"
	CHECK (p.timeSigNum == 3 && p.barBeats () == 3 && p.mainBpm () == 96);
	int lead = trackNamed (p, "Lead"), chv = trackNamed (p, "Accords"), pad = trackNamed (p, "Pad"), dr = trackNamed (p, "Drums");
	CHECK (lead >= 0 && chv >= 0 && pad >= 0 && dr >= 0);
	CHECK (p.tracks[chv].type == TRACK_INSTRUMENT);			// the chord VOICE: an instrument track
	// Koton lays an accompaniment even with the chord voice (the sections have no articulation: style 0)
	int acc = trackNamed (p, "Accompaniment");
	CHECK (acc >= 0 && p.tracks[acc].items.size () == 2);
	// the lead: 2 riffs of 8 bars -> 4 PlayRiff blocks of 4 bars (12 beats); the chorus's absolute starts made relative
	CHECK (p.tracks[lead].items.size () == 4);
	CHECK (p.riffs.size () >= 4 + 4 + 2);
	{
		const PlayRiffModule *pr = (const PlayRiffModule *) p.tracks[lead].items[2].module;
		const Riff *rf = p.riffById (pr->riffId);
		CHECK (rf && rf->spq == 24 && rf->lengthSlices == 12 * 24 && rf->notes.size () > 0 && rf->notes[0].start == 0 && rf->notes[0].note == 75 - 12);
		CHECK (rf && rf->name == "Lead Chorus");
		// the harmony fix: every note on a beat is a chord tone of its bar's chord
		const PlayRiffModule *pr0 = (const PlayRiffModule *) p.tracks[lead].items[0].module;
		const Riff *r0 = p.riffById (pr0->riffId);
		bool allOk = true;
		for (int i = 0; r0 && i < r0->notes.size (); i++)
		{
			const RiffNote &n = r0->notes[i];
			if (n.start % 24) continue;
			int root, q, inv; chordAt (p, n.start / 24.0, &root, &q, &inv);
			int cn[16]; int c = chordNotes (root, 4, q, 0, false, cn); bool in = false;
			for (int k = 0; k < c; k++) if (imod (cn[k], 12) == imod (n.note + 12, 12)) in = true;
			if (!in) { allOk = false; printf ("  (lead note %d at beat %d is not in %d/%d)\n", n.note + 12, n.start / 24, root, q); }
		}
		CHECK (allOk);
	}
	// the chord voice: a one-bar motif tiled over 16 bars; the pad's object-form notes (numbers as strings)
	{
		const Riff *rf = p.riffById (((const PlayRiffModule *) p.tracks[chv].items[0].module)->riffId);
		CHECK (rf && rf->notes.size () == 4 * 4);
		const Riff *pd = p.riffById (((const PlayRiffModule *) p.tracks[pad].items[0].module)->riffId);
		CHECK (pd && pd->notes.size () == 2 && pd->notes[0].note == 50 && pd->notes[0].length == 6 * 24);
		CHECK (p.tracks[pad].items[0].silenceBefore == 24);		// from bar 9
	}
	// the verse drums had no motifBars: the one-bar motif is found and looped (8 bars in two 4-bar blocks)
	{
		const DrumModule *d0 = (const DrumModule *) p.tracks[dr].items[0].module;
		CHECK (d0->style == DRUM_CUSTOM_STYLE && d0->custom.notes.size () == 3);
		CHECK (p.trackEnd (p.tracks[dr]) == 48);
	}
	int locked, fixed; checkChordTrack (p, &locked, &fixed);
	CHECK (locked + fixed == 16);
	CHECK (chordIs (p, 42.5, 10, 13));		// bar 15: Bb add9
	checkPlays (p, "riffs");
	out = p;
}

static void testPoly ()
{
	AiRequest r; r.kind = AI_COMPOSE; r.style = "afro-cubain"; r.measures = 16; r.drums = true; r.polyChords = true; r.polyDrums = true; r.chordsVoice = true;
	Str sys, usr; Project empty;
	CHECK (aiBuildPrompt (empty, r, sys, usr));
	dump ("compose_poly", sys, usr);
	CHECK (has (sys, "\"polyChords\": [ { \"fromMeasure\": int, \"measures\": int(1..4),") && has (sys, "\"polyDrums\": [ {"));
	CHECK (count (sys, "- MODÈLE POLYRYTHMIQUE") == 1);
	CHECK (has (sys, "- ACCORDS EN POLYRYTHME") && has (sys, "- BATTERIE EN POLYRYTHME"));
	CHECK (!has (sys, "- ACCORDS EN VOIX DÉDIÉE"));		// polyChords wins over the chord voice
	CHECK (!has (sys, "  \"drums\": ["));			// the polydrums replace the drum phrases
	CHECK (has (sys, "(1 à 4 MAXIMUM, jamais plus)"));

	char err[200] = "", summary[200];
	CHECK (aiCheckReply (r, R_POLY, summary, sizeof summary));
	CHECK (has (summary, "Polyrhythmic chords: 1 block(s), 4 ring(s). Polyrhythmic drums: 2 block(s), 6 ring(s)."));
	Project p;
	CHECK (aiApplyReply (p, r, R_POLY, err, sizeof err));
	int ci = p.chordTrackIndex ();
	CHECK (ci == p.tracks.size () - 1 && p.tracks[ci].instrument == 46);
	CHECK (p.tracks[ci].items.size () == 1 && p.tracks[ci].items[0].module->kind == M_POLYCHORD);	// the legacy object: the whole piece
	const PolyChordModule *pc = (const PolyChordModule *) p.tracks[ci].items[0].module;
	CHECK (pc->chords.size () == 16 && polyChordTotalBeats (*pc) == 64 && pc->layers.size () == 4 && pc->openVoicing);
	CHECK (pc->layers[0].octave == -1 && pc->layers[0].legato && pc->layers[3].toneIndex == 4);
	bool voiced = false; for (int i = 1; i < pc->chords.size (); i++) if (pc->chords[i].inversion || pc->chords[i].octaveShift) voiced = true;
	CHECK (voiced);						// revoiced (the voice-leading pass ran)
	CHECK (trackNamed (p, "Accompaniment") < 0);		// the poly path has its own sounding modules
	int dr = trackNamed (p, "Drums");
	CHECK (dr >= 0 && p.tracks[dr].items.size () == 2);
	CHECK (p.tracks[dr].items[1].silenceBefore == 16);	// bars 5-8 stay silent
	const PolyDrumModule *pd = (const PolyDrumModule *) p.tracks[dr].items[0].module;
	CHECK (pd->layers[0].accentLane == 31 && pd->beats == 4 && pd->repeats == 4);
	CHECK (chordIs (p, 12.5, 7, 8));			// bar 4: G7 (VII7 of A minor, degree-locked)
	int locked, fixed; checkChordTrack (p, &locked, &fixed);
	checkPlays (p, "poly");

	// an array with gaps: filled with the neighbouring entry, 4-bar modules
	Project q;
	CHECK (aiApplyReply (q, r, R_POLY_GAPS, err, sizeof err));
	ci = q.chordTrackIndex ();
	CHECK (q.tracks[ci].items.size () == 4);
	for (int i = 0; i < q.tracks[ci].items.size (); i++) CHECK (moduleBeats (q.tracks[ci].items[i].module, q) == 16 && q.tracks[ci].items[i].silenceBefore == 0);
	const PolyChordModule *g2 = (const PolyChordModule *) q.tracks[ci].items[1].module;	// a filler: the first entry's rings
	CHECK (g2->cycleBeats == 3 && g2->layers.size () == 2);
	const PolyChordModule *g3 = (const PolyChordModule *) q.tracks[ci].items[2].module;
	CHECK (g3->mode == PC_ONE_RING_SWEEP && g3->restart == RESTART_TIERCE && g3->layers[0].contour == 1);
	CHECK (chordIs (q, 20.5, 0, 6));			// bars 5-8 hold the Cmaj7 of bar 5
	checkPlays (q, "poly gaps");
}

static void testDevelop (Project &bossa, const Project &riffs)
{
	// the prompt: the theme = a riff (here the chorus lead), its chords and notes
	AiRequest r; r.kind = AI_DEVELOP; r.style = "valse jazz"; r.measures = 16; r.fullMelody = true;
	r.track = trackNamed (riffs, "Lead"); r.item = 2;
	Str sys, usr;
	CHECK (aiBuildPrompt (riffs, r, sys, usr));
	dump ("develop", sys, usr);
	CHECK (has (usr, "DÉVELOPPE / VARIE le thème ci-dessous sur environ 16 mesures"));
	CHECK (has (usr, "Style : « valse jazz ».\n\nTHÈME à développer — tonalité Si♭ majeur majeur, mesure 3/4, 4 mesures.\nAccords du thème : m1=degré 4 Maj7, m2=degré 5 7 (dom), m3=degré 3 Min7, m4=degré 6 Min7.\nNotes du thème (pitchMIDI@débutEnTemps x duréeEnTemps) : 75@0x1 74@1x1 70@2x1 69@3x3 72@6x1"));
	CHECK (has (usr, ".\n\nRenvoie UNIQUEMENT l'objet JSON (mêmes clés que le schéma)."));
	// no theme given: the last riff of the piece
	AiRequest r2 = r; r2.track = -1; r2.item = -1;
	CHECK (aiBuildPrompt (riffs, r2, sys, usr));
	CHECK (has (usr, "THÈME à développer"));
	Project none; char err[200] = "";
	CHECK (!aiBuildPrompt (none, r2, sys, usr, err, sizeof err) && has (err, "No riff"));

	// the reply appended after the bossa's 24 bars
	AiRequest d; d.kind = AI_DEVELOP;
	int leadBefore = bossa.tracks[trackNamed (bossa, "Lead")].items.size ();
	int tracksBefore = bossa.tracks.size ();
	bool ok = aiApplyReply (bossa, d, R_DEVELOP, err, sizeof err);
	if (!ok) printf ("  develop: %s\n", err);
	CHECK (ok);
	int ci = bossa.chordTrackIndex ();
	CHECK (ci == bossa.tracks.size () - 1 && bossa.tracks.size () == tracksBefore + 1);	// + "Strings"
	CHECK (bossa.tracks[ci].items.size () == 25 + 8 && bossa.trackEnd (bossa.tracks[ci]) == 96 + 32);
	CHECK (chordIs (bossa, 96.5, 10, 6));			// bar 25: Bbmaj7 (VI)
	int lead = trackNamed (bossa, "Lead"), acc = trackNamed (bossa, "Accompaniment"), str = trackNamed (bossa, "Strings");
	CHECK (bossa.tracks[lead].items.size () == leadBefore + 2 && bossa.trackEnd (bossa.tracks[lead]) == 128);
	CHECK (bossa.itemStart (bossa.tracks[lead], leadBefore) == 96);
	CHECK (str >= 0 && bossa.itemStart (bossa.tracks[str], 0) == 96);
	CHECK (bossa.tracks[acc].items.size () == 5 && bossa.itemStart (bossa.tracks[acc], 4) == 96);
	CHECK (bossa.markers.size () == 5 && bossa.markers[4].beat == 96 && bossa.markers[4].name == "Development");
	int dr = trackNamed (bossa, "Drums");
	CHECK (bossa.trackEnd (bossa.tracks[dr]) == 128);
	int locked, fixed; checkChordTrack (bossa, &locked, &fixed);
	checkPlays (bossa, "develop");
}

static void testAddTrack (Project &p)
{
	// the full-piece context in the prompt
	AiRequest r; r.kind = AI_ADD_TRACK; r.intention = "une flûte qui répond au lead"; r.fullMelody = true;
	Str sys, usr;
	CHECK (aiBuildPrompt (p, r, sys, usr));
	dump ("add_track_melody", sys, usr);
	CHECK (has (sys, "Tu AJOUTES UNE nouvelle voix instrumentale") && has (sys, "doit être rédigé en ANGLAIS et nommer l'instrument (ex. \"Flute\", \"Cello\")."));
	CHECK (has (sys, "(0 à measures×3). Couvre les 16 mesures, en une ou plusieurs entrées"));
	CHECK (has (usr, "MORCEAU ACTUEL — tonalité Si♭ majeur majeur, mesure 3/4, 96 BPM, 16 mesures (3 temps/mesure).\n"));
	CHECK (has (usr, "Accords [mesure,degré,qualité] : [1,1,Maj7] [2,6,Min7] [3,2,Min7] [4,5,7 (dom)]"));
	CHECK (has (usr, "PISTE «Lead» (GM 73, ") && has (usr, " notes) [pitchMIDI@débutTemps xduréeTemps] : 70@0x1 74@1x1 77@2x1 "));
	CHECK (has (usr, "BATTERIE «Drums» (GM 128, ") && has (usr, "36@0x0.04 "));
	CHECK (has (usr, "\n\nIntention pour la nouvelle voix : « une flûte qui répond au lead ». Renvoie UNIQUEMENT le JSON de la nouvelle piste."));
	r.fullMelody = false;
	CHECK (aiBuildPrompt (p, r, sys, usr));
	dump ("add_track_line", sys, usr);
	CHECK (has (sys, "- 'contour' ∈ { Vague (arcs), Montante, Descendante, Statique (pivot), Zigzag, Aléatoire, Thue-Morse, L-système, Fractale (1/f) }. 'anchor' ∈ { Défaut (au plus proche), Fondamentale,"));
	AiRequest rd; rd.kind = AI_ADD_DRUMS; rd.intention = "brosses";
	CHECK (aiBuildPrompt (p, rd, sys, usr));
	dump ("add_drums", sys, usr);
	CHECK (has (sys, "Tu es un batteur assistant. Tu AJOUTES UNE piste de batterie/percussions") && has (sys, "(0 à motifBars×3), en TEMPS."));

	int n0 = p.tracks.size ();
	char err[200] = "";
	r.fullMelody = true;
	CHECK (aiApplyReply (p, r, R_ADD_MELODY, err, sizeof err));
	int fl = trackNamed (p, "Flute");
	CHECK (fl >= 0 && p.tracks.size () == n0 + 1 && p.chordTrackIndex () == p.tracks.size () - 1);
	CHECK (p.tracks[fl].items.size () == 4 && p.trackEnd (p.tracks[fl]) == 48 && p.itemStart (p.tracks[fl], 2) == 24);
	CHECK (p.tracks[p.chordTrackIndex ()].items.size () == 16);	// no chord added
	r.fullMelody = false;
	CHECK (aiApplyReply (p, r, R_ADD_LINE, err, sizeof err));
	CHECK (trackNamed (p, "Cello") >= 0);
	CHECK (aiApplyReply (p, rd, R_ADD_DRUMS, err, sizeof err));
	int drums = 0; for (int i = 0; i < p.tracks.size (); i++) if (p.tracks[i].type == TRACK_DRUM) drums++;
	CHECK (drums == 2);							// a new drum track over the same bars
	CHECK (!aiApplyReply (p, r, "{\"riffs\":[]}", err, sizeof err) && has (err, "Nothing to add"));
	checkPlays (p, "add track");
}

static void testElements (const Project &bossa)
{
	// a groove for the bossa's first drum module
	Project p = bossa;
	int dr = trackNamed (p, "Drums");
	AiRequest g; g.kind = AI_DRUM_GROOVE; g.intention = "samba"; g.track = dr; g.item = 0;
	Str sys, usr;
	CHECK (aiBuildPrompt (p, g, sys, usr));
	dump ("drum_groove", sys, usr);
	CHECK (has (sys, "(0 à motifBars×4).\n") && !strcmp (usr, "Mesure 4/4 (4 temps par mesure), tonalité D minor (Aeolian). Intention du groove : « samba ». Compose un groove cohérent et musical. Renvoie UNIQUEMENT le JSON."));
	g.keyText = "Ré mineur"; g.meterText = "4/4 swing";
	CHECK (aiBuildPrompt (p, g, sys, usr) && has (usr, "Mesure 4/4 swing (4 temps par mesure), tonalité Ré mineur."));
	char err[200] = "";
	CHECK (aiApplyReply (p, g, R_GROOVE, err, sizeof err));
	const DrumModule *d = (const DrumModule *) p.tracks[dr].items[0].module;
	// kick-snare every 2 beats, hats every half beat: one bar compresses to a 2-beat unit, x 8 bars x 2
	CHECK (d->style == DRUM_CUSTOM_STYLE && d->beatsPerBar == 2 && d->repeats == 16 && d->custom.notes.size () == 7);
	// a groove with no target: a new drum track, before the chord track
	AiRequest g2 = g; g2.track = -1; g2.item = -1;
	int n0 = p.tracks.size ();
	CHECK (aiApplyReply (p, g2, R_GROOVE, err, sizeof err));
	CHECK (p.tracks.size () == n0 + 1 && p.chordTrackIndex () == p.tracks.size () - 1 && p.tracks[n0 - 1].type == TRACK_DRUM);
	CHECK (!aiApplyReply (p, g2, "{\"motifBars\":1,\"notes\":[]}", err, sizeof err) && has (err, "No drum notes"));

	// a riff on an empty project: no chord under it -> the AI's progression goes on the chord track
	Project e;
	AiRequest rr; rr.kind = AI_RIFF; rr.measures = 2; rr.intention = "une phrase de guitare";
	CHECK (aiBuildPrompt (e, rr, sys, usr));
	dump ("riff_no_chords", sys, usr);
	CHECK (has (sys, "\"articulation\": \"style\" }") && has (sys, "sur 2 mesure(s) ET la mélodie") && has (sys, "(0 à 2×4)."));
	CHECK (has (usr, "Mesure 4/4 (4 temps par mesure), tonalité C major, 2 mesure(s). Intention : « une phrase de guitare »."));
	CHECK (aiApplyReply (e, rr, R_RIFF, err, sizeof err));
	CHECK (e.tracks.size () == 2 && e.tracks[0].name == "Riff" && e.chordTrackIndex () == 1);
	CHECK (e.riffs.size () == 1 && e.riffs[0].notes.size () == 8 && e.riffs[0].lengthSlices == 2 * 4 * 24);
	CHECK (e.tracks[1].items.size () == 2);		// C major: degree 1 "Min7" -> a fixed Cm7
	CHECK (chordIs (e, 0.5, 0, 7) && chordIs (e, 4.5, 5, 7));
	checkPlays (e, "riff");
	// the same riff again: now chords are under it, the prompt lists them and none is added
	AiRequest r3 = rr; r3.track = 0; r3.item = 0;
	CHECK (aiBuildPrompt (e, r3, sys, usr));
	dump ("riff_with_chords", sys, usr);
	CHECK (has (usr, "Accords présents [mesure, degré, qualité] : [1,1,Min7] [2,4,Min7] . Intention"));
	CHECK (has (sys, "Schéma EXACT : { \"notes\": [ [hauteur MIDI, début, durée], ... ] }  (la MÉLODIE du riff)"));
	CHECK (aiApplyReply (e, r3, R_RIFF, err, sizeof err));
	CHECK (e.tracks[1].items.size () == 2 && e.riffs.size () == 1);
	CHECK (!aiApplyReply (e, r3, "{\"notes\":[]}", err, sizeof err) && has (err, "No notes"));
	CHECK (!aiApplyReply (e, r3, "{\"notes\":[[60,40,1]]}", err, sizeof err) && has (err, "out of range"));

	// a polyrhythmic piece
	AiRequest pr; pr.kind = AI_POLYRHYTHM; pr.style = "gnawa"; pr.measures = 8; pr.intention = "transe";
	CHECK (aiBuildPrompt (e, pr, sys, usr));
	dump ("polyrhythm", sys, usr);
	CHECK (has (sys, "Tu es un compositeur assistant SPÉCIALISÉ en RYTHMES POLYRYTHMIQUES."));
	CHECK (!strcmp (usr, "Compose une pièce POLYRYTHMIQUE de 8 mesure(s) en 4/4 (soit 32 temps). Style : « gnawa ». Intention : « transe ». Renvoie UNIQUEMENT le JSON, minifié."));
	Project q;
	CHECK (aiApplyReply (q, pr, R_POLYRHYTHM, err, sizeof err));
	CHECK (q.tracks.size () == 3 && q.tracks[0].type == TRACK_DRUM && q.tracks[1].instrument == 108 && q.tracks[2].type == TRACK_CHORD);
	CHECK (q.key.tonicLetter == 1 && q.key.mode == 1 && q.mainBpm () == 112);
	const PolyDrumModule *pd = (const PolyDrumModule *) q.tracks[0].items[0].module;
	CHECK (pd->beats == 8 && pd->repeats == 4 && pd->layers.size () == 4);
	const MelodicPolyModule *mp = (const MelodicPolyModule *) q.tracks[1].items[0].module;
	CHECK (mp->layers.size () == 2 && mp->layers[1].voice == 1 && mp->layers[0].legato);	// renumbered
	CHECK (chordIs (q, 1, 2, 1));
	checkPlays (q, "polyrhythm");
}

static void testMalformed ()
{
	AiRequest r; r.kind = AI_COMPOSE;
	char err[200];
	Project p; p.tempo[0].bpm = 77;
	const char *bad[] = {
		"Désolé, je ne peux pas composer cela.",
		"{\"meter\":{\"num\":4,\"den\":4},\"chords\":[[1,1,\"Maj7\"],[2,4",		// truncated
		"{\"meter\":{\"num\":4,\"den\":4},\"sections\":[{\"name\":\"A\",\"measures\":4}]}",	// no chords
		"", 0, "[1,2,3]", "```json\n```" };
	for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
	{
		err[0] = 0;
		CHECK (!aiApplyReply (p, r, bad[i], err, sizeof err));
		CHECK (err[0] != 0);
		CHECK (p.mainBpm () == 77);						// untouched
	}
	char s[200];
	CHECK (!aiCheckReply (r, bad[2], s, sizeof s) && has (s, "No chords"));
	// an array around the object, a trailing comma, a comment, junk after the object: tolerated
	const char *ok[] = {
		"[{\"chords\":[[1,1,\"Majeur\"],[2,5,\"7\"],],\"sections\":[{\"name\":\"A\",\"measures\":2}]}]",
		"{\"chords\":[[1,1,\"Majeur\"]] // un seul accord\n}",
		"{\"chords\":[[1,1,\"Majeur\"],[2,5,\"Majeur\"]]}4}]}]}",
		"```\n{\"chords\":[[\"1\",\"4\",\"sus4\"]]}\n```" };
	for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++)
	{
		Project q; err[0] = 0;
		bool good = aiApplyReply (q, r, ok[i], err, sizeof err);
		if (!good) printf ("  tolerant #%u: %s\n", i, err);
		CHECK (good);
		CHECK (q.chordTrackIndex () >= 0 && q.tracks[q.chordTrackIndex ()].items.size () >= 1);
	}
	// the translations
	CHECK (aidet::qualityIndex ("Maj7") == 6 && aidet::qualityIndex ("min7") == 7 && aidet::qualityIndex ("m7b5") == 9 && aidet::qualityIndex ("7") == 8);
	CHECK (aidet::qualityIndex ("Diminué") == 2 && aidet::qualityIndex ("diminished") == 2 && aidet::qualityIndex ("7#9") == 19 && aidet::qualityIndex ("?") == 0);
	CHECK (aidet::contourIndex ("montante") == 1 && aidet::contourIndex ("7") == 7 && aidet::contourIndex ("Falling") == 2 && aidet::contourIndex ("99") == 8);
	CHECK (aidet::anchorIndex ("Neuvième") == 5 && aidet::anchorIndex ("third") == 2);
	CHECK (aidet::styleIndex ("Valse") == 12 && aidet::styleIndex ("Personnalisé…") == 28 && aidet::styleIndex ("bossa") == 16);
	Key k = aidet::parseKey ("Do", "mineur"); CHECK (k.tonicLetter == 0 && k.mode == 1);
	k = aidet::parseKey ("Ré", "major"); CHECK (k.tonicLetter == 1 && k.accidental == 0);
	k = aidet::parseKey ("F#", "minor"); CHECK (k.tonicLetter == 3 && k.accidental == 1);
	k = aidet::parseKey ("Eb", "Major"); CHECK (k.tonicLetter == 2 && k.accidental == -1);
	k = aidet::parseKey ("Sol dièse", "min"); CHECK (k.tonicLetter == 4 && k.accidental == 1 && k.mode == 1);
	char b[32];
	aidet::num2 (1.0 / 24, b, sizeof b); CHECK (!strcmp (b, "0.04"));
	aidet::num2 (0.125, b, sizeof b); CHECK (!strcmp (b, "0.13"));
	aidet::num2 (2.5, b, sizeof b); CHECK (!strcmp (b, "2.5"));
	aidet::num2 (132, b, sizeof b); CHECK (!strcmp (b, "132"));
	aidet::num2 (-0.5, b, sizeof b); CHECK (!strcmp (b, "-0.5"));
	Str kn; Key fs; fs.tonicLetter = 3; fs.accidental = 1; fs.mode = 1; aidet::frenchKeyName (fs, kn); CHECK (kn == "Fa♯ mineur");
	Key bb; bb.tonicLetter = 6; bb.accidental = -1; aidet::frenchKeyName (bb, kn); CHECK (kn == "Si♭ majeur");
}

// ---- the /bin/llm protocol -------------------------------------------------------------------------------------------
static bool buildFor (const char *provider, const char *model, const char *key, const char *url, int thinking, llm::Call &c, json::Doc &body, char *err, int cap)
{
	Str sys ("Tu es un compositeur assistant. « JSON » \"quoted\"\nline 2"), usr ("Compose un morceau : « bossa ».");
	json::Writer w (false);
	aiBuildRequestJson (provider, model, key, sys, usr, 0.7, thinking, w, url);
	static json::Doc rd; llm::Request r;
	if (!llm::parseRequest (rd, w.data (), w.size (), r, err, cap)) return false;
	CHECK (!strcmp (r.system, sys.c ()) && !strcmp (r.user, usr.c ()) && r.json && r.temperature > 0.6999 && r.temperature < 0.7001 && r.thinking == thinking);
	if (!llm::buildCall (r, c, err, cap)) return false;
	CHECK (body.parse (c.body.data (), c.body.size ()));			// the body is strict JSON
	return true;
}

static void testLlm ()
{
	char err[512];
	{
		llm::Call c; json::Doc b;
		CHECK (buildFor ("gemini", "gemini-2.5-flash", "AIzaKEY", 0, 2048, c, b, err, sizeof err));
		CHECK (!strcmp (c.url, "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent"));
		CHECK (has (c.headers, "x-goog-api-key: AIzaKEY\r\n") && !has (c.headers, "Authorization"));
		const json::Value &r = b.root ();
		CHECK (has (r["systemInstruction"]["parts"][0]["text"].asStr (), "\"quoted\"\nline 2"));
		CHECK (json::seq (r["contents"][0]["role"].asStr (), "user") && has (r["contents"][0]["parts"][0]["text"].asStr (), "« bossa »"));
		CHECK (json::seq (r["generationConfig"]["responseMimeType"].asStr (), "application/json"));
		CHECK (r["generationConfig"]["temperature"].asDouble () > 0.6999 && r["generationConfig"]["temperature"].asDouble () < 0.7001 && r["generationConfig"]["thinkingConfig"]["thinkingBudget"].asInt () == 2048);
		CHECK (!r["generationConfig"].has ("maxOutputTokens"));
	}
	{
		llm::Call c; json::Doc b;
		CHECK (buildFor ("gemini", "models/gemini 2", "K", 0, -1, c, b, err, sizeof err));
		CHECK (has (c.url, "/models/gemini%202:generateContent") && !b.root ()["generationConfig"].has ("thinkingConfig"));
		llm::Call c2; json::Doc b2;
		CHECK (buildFor ("gemini", "gemini-2.5-pro", "K", 0, 99999, c2, b2, err, sizeof err));
		CHECK (b2.root ()["generationConfig"]["thinkingConfig"]["thinkingBudget"].asInt () == 24576);
	}
	{
		llm::Call c; json::Doc b;
		CHECK (buildFor ("groq", "llama-3.3-70b-versatile", "gsk_K", 0, -1, c, b, err, sizeof err));
		CHECK (!strcmp (c.url, "https://api.groq.com/openai/v1/chat/completions") && has (c.headers, "Authorization: Bearer gsk_K\r\n"));
		const json::Value &r = b.root ();
		CHECK (json::seq (r["model"].asStr (), "llama-3.3-70b-versatile") && json::seq (r["messages"][0]["role"].asStr (), "system"));
		CHECK (json::seq (r["messages"][1]["role"].asStr (), "user") && json::seq (r["response_format"]["type"].asStr (), "json_object"));
		llm::Call m; json::Doc mb;
		CHECK (buildFor ("mistral", "mistral-small-latest", "K", 0, -1, m, mb, err, sizeof err) && !strcmp (m.url, "https://api.mistral.ai/v1/chat/completions"));
	}
	{
		llm::Call c; json::Doc b;
		CHECK (buildFor ("claude", "claude-opus-4-8", "sk-ant", 0, -1, c, b, err, sizeof err));
		CHECK (!strcmp (c.url, "https://api.anthropic.com/v1/messages") && has (c.headers, "x-api-key: sk-ant\r\n") && has (c.headers, "anthropic-version: 2023-06-01\r\n"));
		const json::Value &r = b.root ();
		CHECK (r["max_tokens"].asInt () == 32000 && has (r["system"].asStr (), "compositeur") && !r.has ("temperature"));
		CHECK (json::seq (r["messages"][0]["role"].asStr (), "user"));
	}
	{
		llm::Call c; json::Doc b;
		CHECK (!buildFor ("openai-compatible", "local", "", 0, -1, c, b, err, sizeof err) && has (err, "url"));
		llm::Call c2; json::Doc b2;
		CHECK (buildFor ("openai-compatible", "qwen3", "", "http://192.168.1.20:8080/v1/chat/completions", -1, c2, b2, err, sizeof err));
		CHECK (!strcmp (c2.url, "http://192.168.1.20:8080/v1/chat/completions") && !has (c2.headers, "Authorization"));
		llm::Call c3; json::Doc b3;
		CHECK (!buildFor ("gemini", "gemini-2.5-flash", "", 0, -1, c3, b3, err, sizeof err) && has (err, "no API key"));
		llm::Call c4; json::Doc b4;
		CHECK (!buildFor ("bard", "x", "k", 0, -1, c4, b4, err, sizeof err) && has (err, "unknown provider"));
		llm::Call c5; json::Doc b5;
		CHECK (!buildFor ("groq", "", "k", 0, -1, c5, b5, err, sizeof err) && has (err, "no model"));
	}
	// the answers
	{
		json::Writer t (false);
		const char *gem = "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"thinking...\",\"thought\":true},{\"text\":\"{\\\"chords\\\":[[1,1,\"},{\"text\":\"\\\"Maj7\\\"]]}\"}],\"role\":\"model\"},\"finishReason\":\"STOP\",\"index\":0}],\"usageMetadata\":{\"promptTokenCount\":4000}}";
		CHECK (llm::extractText (llm::API_GEMINI, "Gemini", 200, gem, strlen (gem), t, err, sizeof err));
		CHECK (!strcmp (t.data (), "{\"chords\":[[1,1,\"Maj7\"]]}"));
		json::Writer t2 (false);
		const char *cut = "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"{\\\"chords\\\":[\"}]},\"finishReason\":\"MAX_TOKENS\"}]}";
		CHECK (!llm::extractText (llm::API_GEMINI, "Gemini", 200, cut, strlen (cut), t2, err, sizeof err) && has (err, "token limit"));
		const char *e400 = "{\n  \"error\": {\n    \"code\": 400,\n    \"message\": \"API key not valid. Please pass a valid API key.\",\n    \"status\": \"INVALID_ARGUMENT\"\n  }\n}\n";
		CHECK (!llm::extractText (llm::API_GEMINI, "Gemini", 400, e400, strlen (e400), t2, err, sizeof err));
		CHECK (!strcmp (err, "Gemini answered 400: API key not valid. Please pass a valid API key."));
		const char *blocked = "{\"promptFeedback\":{\"blockReason\":\"SAFETY\"}}";
		CHECK (!llm::extractText (llm::API_GEMINI, "Gemini", 200, blocked, strlen (blocked), t2, err, sizeof err) && has (err, "SAFETY"));
		const char *empty = "{\"candidates\":[{\"content\":{\"parts\":[]},\"finishReason\":\"RECITATION\"}]}";
		CHECK (!llm::extractText (llm::API_GEMINI, "Gemini", 200, empty, strlen (empty), t2, err, sizeof err) && has (err, "RECITATION"));
		const char *html = "<html><body>502 Bad Gateway</body></html>";
		CHECK (!llm::extractText (llm::API_OPENAI, "Groq", 502, html, strlen (html), t2, err, sizeof err) && has (err, "Groq answered 502: <html>"));
		CHECK (!llm::extractText (llm::API_OPENAI, "Groq", 401, "", 0, t2, err, sizeof err) && has (err, "invalid API key"));
		json::Writer t3 (false);
		const char *oa = "{\"id\":\"x\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"{\\\"notes\\\":[[60,0,1]]}\"},\"finish_reason\":\"stop\"}]}";
		CHECK (llm::extractText (llm::API_OPENAI, "Mistral", 200, oa, strlen (oa), t3, err, sizeof err) && !strcmp (t3.data (), "{\"notes\":[[60,0,1]]}"));
		const char *oal = "{\"choices\":[{\"message\":{\"content\":\"{\\\"no\"},\"finish_reason\":\"length\"}]}";
		CHECK (!llm::extractText (llm::API_OPENAI, "Groq", 200, oal, strlen (oal), t2, err, sizeof err) && has (err, "token limit"));
		const char *e429 = "{\"error\":{\"message\":\"Rate limit reached\",\"type\":\"tokens\"}}";
		CHECK (!llm::extractText (llm::API_OPENAI, "Groq", 429, e429, strlen (e429), t2, err, sizeof err) && !strcmp (err, "Groq answered 429: Rate limit reached"));
		json::Writer t4 (false);
		const char *cl = "{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"{\\\"motifBars\\\":1,\"},{\"type\":\"text\",\"text\":\"\\\"notes\\\":[[36,0,1]]}\"}],\"stop_reason\":\"end_turn\"}";
		CHECK (llm::extractText (llm::API_CLAUDE, "Claude", 200, cl, strlen (cl), t4, err, sizeof err) && !strcmp (t4.data (), "{\"motifBars\":1,\"notes\":[[36,0,1]]}"));
		const char *ref = "{\"content\":[],\"stop_reason\":\"refusal\"}";
		CHECK (!llm::extractText (llm::API_CLAUDE, "Claude", 200, ref, strlen (ref), t2, err, sizeof err) && has (err, "refused"));
	}
	// the result line, after progress lines, read back by the app
	{
		const char *text = "{\"chords\":[[1,1,\"Maj7\"]],\"sections\":[{\"name\":\"Thème « A »\",\"measures\":4}]}\n";
		json::Writer w (false);
		llm::writeResult (w, true, text, strlen (text));
		Str out ("llm: connecting to generativelanguage.googleapis.com\nllm: sending 23123 bytes\nllm: waiting for the answer (5 s)\nllm: receiving 8192 bytes\n");
		out.append (w.data ());
		Str t, e, pr;
		CHECK (aiParseLlmOutput (out.c (), out.len (), t, e, &pr));
		CHECK (!strcmp (t.c (), text) && pr == "receiving 8192 bytes");
		// while it runs: only progress (no result yet)
		CHECK (!aiParseLlmOutput (out.c (), 90, t, e, &pr) && pr == "sending 23123 bytes");
		json::Writer we (false);
		llm::writeResult (we, false, "Gemini answered 400: API key not valid.", 39);
		CHECK (!aiParseLlmOutput (we.data (), we.size (), t, e) && e == "Gemini answered 400: API key not valid.");
		CHECK (!aiParseLlmOutput ("llm: connecting to x\n", 21, t, e) && has (e, "no answer"));
	}
	// the download mode's request and result
	{
		json::Writer rq (false);
		aiBuildFetchJson ("https://example.org/GeneralUser-GS.sf2", "SD:/koton/soundfonts/GeneralUser-GS.sf2", rq);
		json::Doc d; CHECK (d.parse (rq.data (), rq.size ()));
		CHECK (json::seq (d.root ()["fetch"].asStr (), "https://example.org/GeneralUser-GS.sf2") && d.root ()["timeout"].asInt () == 120);
		json::Writer w (false);
		llm::writeFetchResult (w, true, 31234567, 0);
		Str out ("llm: connecting to example.org\nllm: receiving 1048576 bytes of 31234567\nllm: writing SD:/x.sf2\n"); out.append (w.data ());
		unsigned long n = 0; Str e, pr;
		CHECK (aiParseFetchOutput (out.c (), out.len (), &n, e, &pr) && n == 31234567 && pr == "writing SD:/x.sf2");
		llm::writeFetchResult (w, false, 0, "the server answered 404");
		CHECK (!aiParseFetchOutput (w.data (), w.size (), &n, e) && e == "the server answered 404");
	}
}

// a 60+ KB reply (8 roles x 32 bars of sixteenths + drums) through a Gemini envelope, the helper's result, the app
static void testBigReply ()
{
	json::Writer a (false);
	a.raw ("{\"meter\":{\"num\":4,\"den\":4},\"key\":{\"tonic\":\"E\",\"mode\":\"minor\"},\"bpm\":120,\"sections\":[");
	for (int s = 0; s < 4; s++) { char b[64]; snprintf (b, sizeof b, "%s{\"name\":\"S%d\",\"measures\":8}", s ? "," : "", s + 1); a.raw (b); }
	a.raw ("],\"chords\":[");
	static const int degs[8] = { 1, 6, 3, 7, 4, 1, 5, 1 };
	for (int m = 0; m < 32; m++) { char b[48]; snprintf (b, sizeof b, "%s[%d,%d,\"Min7\"]", m ? "," : "", m + 1, degs[m % 8]); a.raw (b); }
	a.raw ("],\"articulation\":[],\"riffs\":[");
	static const int scale[7] = { 64, 66, 67, 69, 71, 72, 74 };
	for (int role = 0; role < 8; role++)
		for (int s = 0; s < 4; s++)
		{
			char b[160]; snprintf (b, sizeof b, "%s{\"section\":\"S%d\",\"track\":\"Voice %d\",\"instrument\":%d,\"fromMeasure\":%d,\"measures\":8,\"notes\":[",
					       role || s ? "," : "", s + 1, role + 1, 40 + role, s * 8 + 1);
			a.raw (b);
			for (int k = 0; k < 8 * 16; k++)
			{
				int pitch = scale[(k * (role + 1) + s) % 7] - 12 * (role % 3);
				snprintf (b, sizeof b, "%s[%d,%g,0.25]", k ? "," : "", pitch, k * 0.25);
				a.raw (b);
			}
			a.raw ("]}");
		}
	a.raw ("],\"drums\":[{\"fromMeasure\":1,\"measures\":32,\"motifBars\":1,\"repeats\":32,\"notes\":[[36,0,0.5],[38,1,0.5],[36,2,0.5],[38,3,0.5]]}]}");
	CHECK (a.size () > 60000);
	// the Gemini answer (the text split in two parts), as the helper receives it
	json::Writer g (false);
	g.beginObj (); g.key ("candidates"); g.beginArr (); g.beginObj (); g.key ("content"); g.beginObj (); g.key ("parts"); g.beginArr ();
	unsigned long half = a.size () / 2;
	g.beginObj (); g.key ("text"); g.str (a.data (), (unsigned) half); g.endObj ();
	g.beginObj (); g.key ("text"); g.str (a.data () + half, (unsigned) (a.size () - half)); g.endObj ();
	g.endArr (); g.key ("role"); g.str ("model"); g.endObj (); g.key ("finishReason"); g.str ("STOP"); g.endObj (); g.endArr (); g.endObj ();
	json::Writer text (false); char err[300];
	CHECK (llm::extractText (llm::API_GEMINI, "Gemini", 200, g.data (), g.size (), text, err, sizeof err));
	CHECK (text.size () == a.size () && !memcmp (text.data (), a.data (), a.size ()));
	json::Writer res (false);
	llm::writeResult (res, true, text.data (), text.size ());
	Str out ("llm: receiving 70000 bytes\n"); out.append (res.data ());
	Str t, e;
	CHECK (aiParseLlmOutput (out.c (), out.len (), t, e));
	AiRequest r; r.kind = AI_COMPOSE; r.fullMelody = true;
	Project p;
	CHECK (aiApplyReply (p, r, t.c (), err, sizeof err));
	CHECK (p.tracks.size () == 8 + 1 + 1 + 1);			// 8 voices, the accompaniment, drums, chords
	CHECK (p.riffs.size () == 8 * 4 * 2 && p.totalBeats () == 128);
	printf ("  big reply: %lu bytes of JSON, %lu in the Gemini envelope, %d riffs\n", a.size (), g.size (), p.riffs.size ());
	checkPlays (p, "big reply");
}

int main (int argc, char **argv)
{
	setvbuf (stdout, 0, _IONBF, 0);
	for (int i = 1; i + 1 < argc; i++) if (!strcmp (argv[i], "--dump")) g_dump = argv[i + 1];
	seedIds (12345);
	g_kitPrograms.clear (); g_kitPrograms.push (0); g_kitPrograms.push (8); g_kitPrograms.push (16);
	Project empty, bossa, riffs;
	bossa = empty; testCompose (bossa);
	testRiffs (riffs);
	testPoly ();
	testElements (bossa);
	testAddTrack (riffs);
	testDevelop (bossa, riffs);
	testMalformed ();
	testLlm ();
	testBigReply ();
	g_kitPrograms = Vec<int> ();
	printf ("%s: %d checks, %d failures\n", g_fail ? "FAILED" : "OK", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
