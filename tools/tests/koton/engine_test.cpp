// tools/tests/koton/engine_test.cpp -- Koton's engine on the PC, under AddressSanitizer / LeakSanitizer:
// a song built in code (every module kind: chords on the chord track, a chord articulation, a melodic
// line, a riff, drums, a polyrhythm, polychords with the emergent melody, a cadence, a melodic ring),
// saved as .kson, reloaded, saved again (the two texts must be equal), compiled, then played through
// the engine and MeltySynth into /tmp/koton_engine_test.wav. Also: a Koton .sq-shaped document, the
// theory (cadences, suggestions, roman numerals), and no leak at the end.
//   sh tools/tests/koton/engine_run.sh [file.sf2]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/theory.h"
#include "../../../user/json.hpp"

using namespace kt;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static void addChord (Project &p, int deg, int colour, int beats)
{
	PatternModule *m = new PatternModule;
	RootQ c = diatonicChord (p.key, deg, colour, 0, 0);
	m->degree = deg; m->diatonicColour = colour; m->root = c.root; m->quality = c.quality; m->beatsPerBar = beats; m->voiceLeadMode = 1;
	p.tracks[p.chordTrackIndex ()].items.push (Item (0, m));
}

static void buildSong (Project &p)
{
	p.key.tonicLetter = 3; p.key.accidental = 1; p.key.mode = 1;		// F# minor
	p.tempo[0].bpm = 108; p.swingPercent = 54; p.humanizePercent = 30;
	Marker mk; mk.beat = 0; mk.name = "Intro"; p.markers.push (mk);
	// tracks: lead (riff), comp (articulation), line, drums, perc (polydrum), bells (polychord), chords
	const char *names[] = { "Lead", "Comp", "Counter", "Drums", "Poly perc", "Bells", "Pad" };
	int progs[] = { 108, 0, 73, 0, 0, 14, 89 };
	for (int i = 0; i < 7; i++) { Track t; t.name = names[i]; t.instrument = progs[i]; if (i == 3 || i == 4) t.type = TRACK_DRUM; p.tracks.push (t); }
	Track ch; ch.name = "Chords"; ch.type = TRACK_CHORD; p.tracks.push (ch);
	const int degs[] = { 0, 3, 4, 5, 6, 2, 4, 0 };
	for (int i = 0; i < 8; i++) addChord (p, degs[i], i % 2 ? 2 : 3, 8);
	revoiceTrack (p.tracks[p.chordTrackIndex ()]);
	// a riff
	Riff r; r.id = newId (); r.name = "Kora"; r.spq = 24; r.lengthSlices = 8 * 4 * 24;
	int mel[] = { 66, 69, 73, 71, 69, 68, 66, 64, 66, 69, 71, 73, 76, 73, 71, 69 };
	for (int i = 0; i < 16; i++) r.notes.push (RiffNote (mel[i] - 12, i * 48, 40));
	p.riffs.push (r);
	PlayRiffModule *pr = new PlayRiffModule; pr->riffId = r.id;
	p.tracks[0].items.push (Item (0, pr));
	PlayRiffModule *pr2 = new PlayRiffModule; pr2->riffId = r.id;
	p.tracks[0].items.push (Item (0, pr2));
	// a chord articulation over the 16 bars, bossa-like custom grid
	ArticulationModule *a = new ArticulationModule; a->beats = 4; a->lengthBeats = 64; a->style = 16; a->bass = true; a->voiceLeadMode = 1;
	p.tracks[1].items.push (Item (0, a));
	// a melodic line (rhythm only), wave contour
	MelodicLineModule *ml = new MelodicLineModule; ml->beatsPerBar = 32; ml->contour = 0; ml->continuity = 40;
	Vec<RiffNote> rn; for (int i = 0; i < 32; i++) if (i % 3 != 2) rn.push (RiffNote (0, i * 4, 3));
	ml->rhythm.setNotes (rn, 4, 32 * 4);
	p.tracks[2].items.push (Item (16, ml));
	// drums: a groove with a fill, then a catalog-like motif
	DrumModule *d = new DrumModule; d->style = 7; d->repeats = 8; d->fillLast = true;
	p.tracks[3].items.push (Item (0, d));
	DrumModule *d2 = new DrumModule; d2->repeats = 8;
	Vec<RiffNote> dn; dn.push (RiffNote (0, 0, 1)); dn.push (RiffNote (1, 8, 1)); dn.push (RiffNote (2, 4, 1)); dn.push (RiffNote (2, 12, 1));
	d2->custom.setNotes (dn, 4, 16); d2->style = DRUM_CUSTOM_STYLE;
	p.tracks[3].items.push (Item (0, d2));
	// a polyrhythm E(3,8) E(5,12) E(7,16)
	PolyDrumModule *pd = new PolyDrumModule; pd->beats = 4; pd->repeats = 12;
	int lanes[] = { 29, 35, 40 }, ks[] = { 3, 5, 7 }, ns[] = { 8, 12, 16 };
	for (int i = 0; i < 3; i++) { EuclidLayer l; l.lane = lanes[i]; l.hits = ks[i]; l.steps = ns[i]; l.rotation = i; pd->layers.push (l); }
	p.tracks[4].items.push (Item (16, pd));
	// polychords with the emergent melody
	PolyChordModule *pc = new PolyChordModule; pc->beats = 32; pc->cycleBeats = 4; pc->octave = 5; pc->monodicPick = true;
	for (int i = 0; i < 3; i++) { EuclidChordLayer l; l.hits = ks[i]; l.steps = ns[i]; l.toneIndex = i; pc->layers.push (l); }
	p.tracks[5].items.push (Item (32, pc));
	// a cadence and a melodic ring on the pad track
	CadenceModule *cm = new CadenceModule; cm->beatsPerBar = 4; cm->style = 19;
	Vec<RootQ> cad = cadence (p.key, 0, 4, 3, 7);
	for (int i = 0; i < cad.size (); i++) { CadenceChord c; c.root = cad[i].root; c.quality = cad[i].quality; cm->chords.push (c); }
	p.tracks[6].items.push (Item (0, cm));
	MelodicPolyModule *mp = new MelodicPolyModule; mp->beats = 4; mp->repeats = 4;
	EuclidVoice v; v.hits = 5; v.steps = 8; v.legato = true; mp->layers.push (v);
	p.tracks[6].items.push (Item (0, mp));
}

static void theory ()
{
	Key k; k.tonicLetter = 3; k.accidental = 1; k.mode = 1;			// F# minor
	char b[32];
	RootQ i = diatonicChord (k, 0, 3, 0, 0);
	chordLabel (i.root, i.quality, k, b, sizeof b); CHECK (!strcmp (b, "F#m9"));
	romanNumeral (k, i.root, i.quality, 0, b, sizeof b); CHECK (!strcmp (b, "i"));
	RootQ e7 = { 4, 8 };							// E7 in F# minor: V/III
	romanNumeral (k, e7.root, e7.quality, -1, b, sizeof b); CHECK (!strcmp (b, "V/III") || (printf ("got %s\n", b), false));
	CHECK (chordFunction (k, 11, 1) == FUNC_SUBDOMINANT);			// Bm: iv
	Vec<RootQ> c1 = cadence (k, 0, 8, 22, 1234), c2 = cadence (k, 0, 8, 22, 1234);
	CHECK (c1.size () == 8 && c2.size () == 8);
	for (int j = 0; j < 8; j++) CHECK (c1[j].root == c2[j].root && c1[j].quality == c2[j].quality);
	CHECK (c1.back ().root == 6);						// resolves on the tonic
	int prev[] = { 0, 4 };
	Vec<Suggestion> s = suggestNext (prev, 2, 3, 4, MOOD_AUTO, k);
	CHECK (s.size () > 0 && s[0].recommended);
	NetRandom r (42); CHECK (r.next (100) == 66 || (printf ("NetRandom(42).Next(100) = %d\n", r.next (100)), false));	// .NET gives 66
	DegColour dc = degColour (k, i.root, i.quality); CHECK (dc.degree == 0 && dc.colour == 3);
	CHECK (qualityIndex ("Majeur") == 0 && qualityIndex ("7 (dom)") == 8 && qualityIndex ("m7♭5") == 9 && qualityIndex ("Maj7") == 6);
}

static void sqDocument ()
{
	// the shape Koton for Windows writes (System.Text.Json, IncludeFields)
	const char *sq =
		"{\"Project\":{\"Tempo\":[{\"Beat\":0,\"Bpm\":112}],\"Tracks\":["
		"{\"Name\":\"lead\",\"Type\":0,\"Instrument\":12,\"Volume\":0.8,\"Items\":[{\"SilenceBefore\":0,\"Module\":{\"$type\":\"PlayRiff\",\"Id\":\"a\",\"RiffId\":\"r1\"}},"
		"{\"SilenceBefore\":0,\"Module\":{\"$type\":\"Repeat\",\"Count\":2}}]},"
		"{\"Name\":\"Accords\",\"Type\":2,\"Items\":[{\"SilenceBefore\":0,\"Module\":{\"$type\":\"Pattern\",\"Root\":6,\"Quality\":17,\"Degree\":0,\"BeatsPerBar\":4,\"Repeats\":2,"
		"\"CustomSlices\":[{\"NotesLow\":18446744073709551615,\"NotesHigh\":0}],\"CustomSlicesPerQuarter\":4,\"CustomNotes\":null}}]}],"
		"\"Key\":{\"TonicLetter\":3,\"Accidental\":1,\"Mode\":1,\"FullMode\":-1},\"TimeSigNum\":4,\"TimeSigDen\":4,\"Unknown\":{\"x\":1}},"
		"\"Riffs\":[{\"Id\":\"r1\",\"Name\":\"Riff\",\"Notes\":[{\"Note\":48,\"Start\":0,\"Length\":24,\"Bend\":null,\"Voice\":0,\"GlideFromNote\":48,\"GlideDurationSlices\":0}],"
		"\"LengthSlices\":96,\"SlicesPerQuarter\":24}]}";
	Project p; char err[128];
	CHECK (loadProject (sq, strlen (sq), p, err, sizeof err) || (printf ("%s\n", err), false));
	CHECK (p.tracks.size () == 2 && p.tracks[0].items.size () == 1);	// the Repeat is dropped
	CHECK (p.tracks[1].type == TRACK_CHORD);
	const PatternModule *pg = (const PatternModule *) p.tracks[1].items[0].module;
	CHECK (pg && pg->custom.slices.size () == 1 && pg->custom.slices[0].lo == 18446744073709551615ull);
	CHECK (fabs (p.itemLength (p.tracks[0].items[0]) - 4) < 1e-9);
	int root, q;
	CHECK (chordAt (p, 5, &root, &q, 0) && root == 6 && q == 17);
	CHECK (!chordAt (p, 9, &root, &q, 0));
}

static void writeWav (const char *path, const short *s, int frames, int rate)
{
	FILE *f = fopen (path, "wb"); if (!f) return;
	unsigned data = frames * 4, riff = 36 + data;
	fwrite ("RIFF", 1, 4, f); fwrite (&riff, 4, 1, f); fwrite ("WAVEfmt ", 1, 8, f);
	unsigned fmtLen = 16; unsigned short pcm = 1, chs = 2, bits = 16, align = 4; unsigned br = rate * 4;
	fwrite (&fmtLen, 4, 1, f); fwrite (&pcm, 2, 1, f); fwrite (&chs, 2, 1, f); fwrite (&rate, 4, 1, f); fwrite (&br, 4, 1, f);
	fwrite (&align, 2, 1, f); fwrite (&bits, 2, 1, f); fwrite ("data", 1, 4, f); fwrite (&data, 4, 1, f);
	fwrite (s, 4, frames, f); fclose (f);
}

int main (int argc, char **argv)
{
	theory ();
	sqDocument ();
	seedIds (7);
	Project p; buildSong (p);
	// the modules render
	for (int t = 0; t < p.tracks.size (); t++)
		for (int i = 0; i < p.tracks[t].items.size (); i++)
		{
			int carry[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
			Riff cell;
			Riff r = renderModule (*p.tracks[t].items[i].module, p, p.itemStart (p.tracks[t], i), carry, &cell);
			printf ("%-10s %-18s at %5.1f, %4.1f beats: %3d notes (spq %d)\n", p.tracks[t].name.c (), p.tracks[t].items[i].module->typeName (),
				p.itemStart (p.tracks[t], i), p.itemLength (p.tracks[t].items[i]), r.notes.size (), r.spq);
			if (p.tracks[t].type != TRACK_CHORD) CHECK (r.notes.size () > 0);
		}
	// .kson round trip
	json::Writer w1 (true); saveProject (p, w1);
	Project q; char err[128];
	CHECK (loadProject (w1.data (), w1.size (), q, err, sizeof err) || (printf ("%s\n", err), false));
	json::Writer w2 (true); saveProject (q, w2);
	CHECK (w1.size () == w2.size () && !memcmp (w1.data (), w2.data (), w1.size ()));
	FILE *f = fopen ("/tmp/koton_engine_test.kson", "wb"); if (f) { fwrite (w1.data (), 1, w1.size (), f); fclose (f); }
	// compile
	CompiledSong *cs = compileSong (q, 44100);
	int events = 0; for (int t = 0; t < cs->tracks.size (); t++) events += cs->tracks[t].events.size ();
	printf ("compiled: %d tracks, %d slices, %.1f s, %d events\n", cs->tracks.size (), cs->totalSlices, cs->totalSamples () / 44100.0, events);
	CHECK (events > 500);
	// play it
	const char *sfPath = argc > 1 ? argv[1] : "/opt/sf2/GeneralUser-GS.sf2";
	FILE *sff = fopen (sfPath, "rb");
	if (!sff) { printf ("no SoundFont at %s: the audio part is skipped\n", sfPath); delete cs; }
	else
	{
		fseek (sff, 0, SEEK_END); long n = ftell (sff); fseek (sff, 0, SEEK_SET);
		char *buf = (char *) malloc (n); if (fread (buf, 1, n, sff) != (size_t) n) n = 0; fclose (sff);
		char e2[128]; ms::SoundFont *sf = ms::soundfont_load (buf, n, e2, sizeof e2); free (buf);
		CHECK (sf != 0);
		for (int k = 0; k < ms::soundfont_preset_count (sf); k++)
			if (ms::soundfont_preset_bank (sf, k) == 128) g_kitPrograms.push (ms::soundfont_preset_patch (sf, k));
		{
			Engine e; e.init (sf, 44100);
			CHECK (e.ensureTracks (cs->tracks.size ()));
			for (int t = 0; t < q.tracks.size (); t++) { e.mix[t].volume = (float) q.tracks[t].volume; e.mix[t].reverb = q.tracks[t].reverbSend (); }
			e.post (CMD_SONG, 0, 0, 0, 0, cs);
			e.post (CMD_PLAY, 0);
			int frames = (int) (cs->totalSamples () + 44100);
			short *pcm = (short *) malloc (frames * 4);
			float L[512], R[512]; double sum = 0; float peak = 0; int maxVoices = 0;
			for (int done = 0; done < frames; )
			{
				int nn = frames - done > 512 ? 512 : frames - done;
				e.render (L, R, nn);
				for (int k = 0; k < nn; k++) { sum += L[k] * L[k]; if (fabsf (L[k]) > peak) peak = fabsf (L[k]); }
				toS16 (L, R, pcm + 2 * done, nn, 1.0f);
				if (e.activeVoices > maxVoices) maxVoices = e.activeVoices;
				done += nn;
			}
			writeWav ("/tmp/koton_engine_test.wav", pcm, frames, 44100);
			free (pcm);
			double rms = sqrt (sum / frames);
			printf ("played: %.1f s, peak %.3f, rms %.4f, up to %d voices -> /tmp/koton_engine_test.wav\n", frames / 44100.0, peak, rms, maxVoices);
			CHECK (rms > 0.01 && peak < 4 && maxVoices > 4);
			for (CompiledSong *r; (r = e.retired ()); ) delete r;
		}
		ms::soundfont_free (sf);
	}
	printf (g_fail ? "koton engine: %d FAILED\n" : "koton engine: all passed\n", g_fail);
	return g_fail != 0;
}
