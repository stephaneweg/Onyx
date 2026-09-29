//
// ui/doc.h -- the open song: the project, its file, undo / redo (JSON snapshots of the whole
// project, bounded), the selection, and the edits of the timeline -- a module placed at a beat
// (the items after it keep their places: positions are relative, a silence absorbs the change),
// moved, resized, duplicated, deleted; tracks added, removed, moved. The views call these and
// then Doc::changed (): the song is compiled again, the views redraw.
//
#ifndef _koton_doc_h
#define _koton_doc_h

#include "engine/compile.h"
#include "json.hpp"
#include <stdio.h>

namespace kui {

using namespace kt;

// ---- General MIDI names (a SoundFont's own names are used when it has them) -------------------------------
static const char *const s_gm[128] = {
	"Acoustic Grand Piano", "Bright Piano", "Electric Grand", "Honky-tonk", "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
	"Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
	"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
	"Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar", "Muted Guitar", "Overdrive Guitar", "Distortion Guitar", "Guitar Harmonics",
	"Acoustic Bass", "Finger Bass", "Pick Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
	"Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
	"String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
	"Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
	"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
	"Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Square Lead", "Saw Lead", "Calliope Lead", "Chiff Lead", "Charang", "Voice Lead", "Fifths Lead", "Bass + Lead",
	"New Age Pad", "Warm Pad", "Polysynth", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
	"Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-fi",
	"Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
	"Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
	"Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone", "Helicopter", "Applause", "Gunshot" };
static const char *const s_gmFamilies[16] = { "Piano", "Chromatic percussion", "Organ", "Guitar", "Bass", "Strings", "Ensemble", "Brass",
	"Reed", "Pipe", "Synth lead", "Synth pad", "Synth effects", "Ethnic", "Percussive", "Sound effects" };

struct SoundNames			// filled by the audio host from the SoundFont (else General MIDI)
{
	char gm[128][40];
	int nKits; char kit[64][40];
	SoundNames () : nKits (0) { for (int i = 0; i < 128; i++) snprintf (gm[i], sizeof gm[i], "%s", s_gm[i]); }
};
extern SoundNames g_names;

static inline const char *instrumentName (const Track &t)
{
	if (!t.instrumentPlugin.id.empty ()) return t.instrumentPlugin.id.c ();
	if (t.type == TRACK_CHORD) return "Harmony (silent)";
	if (t.type == TRACK_DRUM) return (t.drumKit >= 0 && t.drumKit < g_names.nKits) ? g_names.kit[t.drumKit] : "Standard kit";
	return g_names.gm[iclamp (t.instrument, 0, 127)];
}

// ---- a module's label on its block --------------------------------------------------------------------------
static inline const char *kindName (int kind)
{
	switch (kind)
	{
	case M_PLAYRIFF: return "Riff";
	case M_PATTERN: return "Chord";
	case M_DRUMKIT: return "Drums";
	case M_CADENCE: return "Cadence";
	case M_MELODICLINE: return "Melodic line";
	case M_POLYDRUM: return "Polyrhythm";
	case M_MELODICPOLY: return "Melodic rings";
	case M_POLYCHORD: return "Poly chords";
	case M_ARTICULATION: return "Accompaniment";
	case M_GENERATOR: return "Generator";
	}
	return "?";
}

void moduleLabel (const Project &p, const Module &m, char *buf, int cap);	// (arrange.h)

// ---- the document -------------------------------------------------------------------------------------------------
enum { MAX_UNDO = 40 };

struct Selection { int track, item; Selection () : track (-1), item (-1) {} bool valid () const { return track >= 0 && item >= 0; } };

class Doc
{
public:
	Project p;
	char path[256];
	bool dirty;
	Selection sel;
	int selTrack;				// the track whose header is selected (inserts go there)
	unsigned revision;			// bumped at every change: the views and the player follow it

	Doc () : dirty (false), selTrack (0), revision (1), m_nUndo (0), m_nRedo (0) { path[0] = 0; for (int i = 0; i < MAX_UNDO; i++) m_undo[i] = m_redo[i] = 0; }
	~Doc () { clearHistory (); }

	void clearHistory ()
	{
		for (int i = 0; i < MAX_UNDO; i++) { free (m_undo[i]); free (m_redo[i]); m_undo[i] = m_redo[i] = 0; }
		m_nUndo = m_nRedo = 0;
	}
	// before an edit: the current state goes on the undo stack
	void checkpoint ()
	{
		char *snap = snapshot ();
		if (!snap) return;
		if (m_nUndo == MAX_UNDO) { free (m_undo[0]); for (int i = 1; i < MAX_UNDO; i++) m_undo[i - 1] = m_undo[i]; m_nUndo--; }
		m_undo[m_nUndo++] = snap;
		for (int i = 0; i < m_nRedo; i++) { free (m_redo[i]); m_redo[i] = 0; }
		m_nRedo = 0;
	}
	void changed () { dirty = true; revision++; }
	bool canUndo () const { return m_nUndo > 0; }
	bool canRedo () const { return m_nRedo > 0; }
	bool undo ()
	{
		if (!m_nUndo) return false;
		char *cur = snapshot ();
		char *s = m_undo[--m_nUndo]; m_undo[m_nUndo] = 0;
		restore (s); free (s);
		if (cur) { if (m_nRedo == MAX_UNDO) { free (m_redo[0]); for (int i = 1; i < MAX_UNDO; i++) m_redo[i - 1] = m_redo[i]; m_nRedo--; } m_redo[m_nRedo++] = cur; }
		changed ();
		return true;
	}
	bool redo ()
	{
		if (!m_nRedo) return false;
		char *cur = snapshot ();
		char *s = m_redo[--m_nRedo]; m_redo[m_nRedo] = 0;
		restore (s); free (s);
		if (cur) { if (m_nUndo == MAX_UNDO) { free (m_undo[0]); for (int i = 1; i < MAX_UNDO; i++) m_undo[i - 1] = m_undo[i]; m_nUndo--; } m_undo[m_nUndo++] = cur; }
		changed ();
		return true;
	}

	// ---- files ----
	bool load (const char *file, char *err, int errcap)
	{
		void *f = kapi_open (file);
		if (!f) { snprintf (err, errcap, "cannot open %s", file); return false; }
		unsigned n = kapi_fsize (f);
		char *buf = (char *) malloc (n + 1);
		if (!buf) { kapi_close (f); snprintf (err, errcap, "out of memory"); return false; }
		int got = kapi_read (f, buf, n); kapi_close (f);
		if (got < 0) got = 0;
		buf[got] = 0;
		Project np;
		bool ok = loadProject (buf, (unsigned long) got, np, err, errcap);
		free (buf);
		if (!ok) return false;
		p = np;
		snprintf (path, sizeof path, "%s", file);
		dirty = false; sel = Selection (); selTrack = 0; clearHistory (); revision++;
		return true;
	}
	bool save (const char *file)
	{
		json::Writer w (true);
		saveProject (p, w);
		if (!w.ok ()) return false;
		if (kapi_save_file (file, w.data (), (unsigned) w.size ()) != 0) return false;
		if (file != path) snprintf (path, sizeof path, "%s", file);
		dirty = false;
		return true;
	}
	void newSong ()
	{
		Project np;
		np.key.tonicLetter = 0; np.key.mode = 0;
		Track lead; lead.name = "Melody"; lead.instrument = 0; np.tracks.push (lead);
		Track acc; acc.name = "Accompaniment"; acc.instrument = 0; np.tracks.push (acc);
		Track bass; bass.name = "Bass"; bass.instrument = 32; np.tracks.push (bass);
		Track dr; dr.name = "Drums"; dr.type = TRACK_DRUM; np.tracks.push (dr);
		Track ch; ch.name = "Chords"; ch.type = TRACK_CHORD; np.tracks.push (ch);
		np.minBeats = 32;
		p = np; path[0] = 0; dirty = false; sel = Selection (); selTrack = 0; clearHistory (); revision++;
	}

	// ---- geometry ----
	double itemStart (int t, int i) const { return p.itemStart (p.tracks[t], i); }
	double itemLen (int t, int i) const { return p.itemLength (p.tracks[t].items[i]); }
	Module *module (int t, int i) { return (t >= 0 && t < p.tracks.size () && i >= 0 && i < p.tracks[t].items.size ()) ? p.tracks[t].items[i].module : 0; }
	Module *selected () { return sel.valid () ? module (sel.track, sel.item) : 0; }
	// the item at a beat on a track, -1 none
	int itemAt (int t, double beat) const
	{
		if (t < 0 || t >= p.tracks.size ()) return -1;
		double c = 0;
		const Track &tr = p.tracks[t];
		for (int i = 0; i < tr.items.size (); i++)
		{
			c += tr.items[i].silenceBefore;
			double len = p.itemLength (tr.items[i]);
			if (beat >= c && beat < c + len) return i;
			c += len;
		}
		return -1;
	}

	// ---- edits (the caller checkpoints first, then calls changed ()) ----
	// Put m on track t at `beat` (or right after the item there): the items after it keep their
	// places when there is room (the next silence shrinks); else they are pushed. -> its index.
	int place (int t, Module *m, double beat)
	{
		Track &tr = p.tracks[t];
		double len = moduleBeats (m, p);
		if (beat < 0) beat = 0;
		// find where it goes: after the last item that starts before `beat`
		double c = 0; int at = 0;
		for (int i = 0; i < tr.items.size (); i++)
		{
			double s = c + tr.items[i].silenceBefore, e = s + p.itemLength (tr.items[i]);
			if (s <= beat + 1e-9) { at = i + 1; c = e; if (beat < e) beat = e; }
			else break;
		}
		double silence = beat - c;
		if (at < tr.items.size ())
		{
			Item &next = tr.items[at];
			double room = next.silenceBefore - silence;		// what is left before the next item
			next.silenceBefore = room >= len ? room - len : 0;
		}
		tr.items.insert (at, Item (silence, m));
		return at;
	}
	// append at the end of the track
	int append (int t, Module *m)
	{
		Track &tr = p.tracks[t];
		tr.items.push (Item (0, m));
		return tr.items.size () - 1;
	}
	// the item moved to start at `beat`, between its neighbours (clamped)
	void moveItem (int t, int i, double beat)
	{
		Track &tr = p.tracks[t];
		double s0 = itemStart (t, i), len = itemLen (t, i);
		double prevEnd = s0 - tr.items[i].silenceBefore;
		double nextStart = i + 1 < tr.items.size () ? s0 + len + tr.items[i + 1].silenceBefore : 1e18;
		if (beat < prevEnd) beat = prevEnd;
		if (beat + len > nextStart) beat = nextStart - len;
		double d = beat - s0;
		tr.items[i].silenceBefore += d;
		if (i + 1 < tr.items.size ()) tr.items[i + 1].silenceBefore -= d;
	}
	// the item's length changed from oldLen: the next item keeps its place
	void lengthChanged (int t, int i, double oldLen)
	{
		Track &tr = p.tracks[t];
		double d = itemLen (t, i) - oldLen;
		if (i + 1 < tr.items.size ())
		{
			double s = tr.items[i + 1].silenceBefore - d;
			tr.items[i + 1].silenceBefore = s < 0 ? 0 : s;
		}
	}
	void removeItem (int t, int i)
	{
		Track &tr = p.tracks[t];
		double gap = tr.items[i].silenceBefore + itemLen (t, i);
		if (i + 1 < tr.items.size ()) tr.items[i + 1].silenceBefore += gap;
		tr.items.removeAt (i);
		if (sel.track == t && sel.item == i) sel = Selection ();
		else if (sel.track == t && sel.item > i) sel.item--;
	}
	// a copy right after the item (the next items pushed only if there is no room)
	int duplicateItem (int t, int i)
	{
		Module *m = p.tracks[t].items[i].module;
		if (!m) return -1;
		Module *c = m->clone ();
		c->id = newId ();
		if (c->kind == M_PLAYRIFF)				// a riff is copied too (its own notes)
		{
			PlayRiffModule *pr = (PlayRiffModule *) c;
			const Riff *r = p.riffById (pr->riffId);
			if (r) { Riff nr = *r; nr.id = newId (); char nm[80]; snprintf (nm, sizeof nm, "%s copy", r->name.c ()); nr.name = nm; pr->riffId = nr.id; p.riffs.push (nr); }
		}
		return place (t, c, itemStart (t, i) + itemLen (t, i));
	}
	int addTrack (const char *name, int type, int instrument)
	{
		Track t; t.name = name; t.type = type; t.instrument = instrument;
		int at = p.chordTrackIndex ();				// before the chord track, pinned last
		if (at < 0) at = p.tracks.size ();
		p.tracks.insert (at, t);
		return at;
	}
	void removeTrack (int t)
	{
		if (t < 0 || t >= p.tracks.size () || p.tracks[t].type == TRACK_CHORD) return;
		p.tracks.removeAt (t);
		sel = Selection ();
		if (selTrack >= p.tracks.size ()) selTrack = p.tracks.size () - 1;
	}
	void moveTrack (int t, int d)
	{
		int u = t + d;
		if (t < 0 || u < 0 || t >= p.tracks.size () || u >= p.tracks.size ()) return;
		if (p.tracks[t].type == TRACK_CHORD || p.tracks[u].type == TRACK_CHORD) return;
		Track tmp = move (p.tracks[t]); p.tracks[t] = move (p.tracks[u]); p.tracks[u] = move (tmp);
		if (selTrack == t) selTrack = u;
		sel = Selection ();
	}
	// a new riff (its notes empty), `beats` long: its PlayRiff module
	PlayRiffModule *newRiff (const char *name, double beats)
	{
		Riff r; r.id = newId (); r.name = name; r.spq = 24; r.lengthSlices = iround (beats * 24);
		PlayRiffModule *m = new PlayRiffModule; m->riffId = r.id;
		p.riffs.push (r);
		return m;
	}
	// the riffs no module plays any more: dropped (at save)
	void pruneRiffs ()
	{
		for (int r = p.riffs.size () - 1; r >= 0; r--)
		{
			bool used = false;
			for (int t = 0; t < p.tracks.size () && !used; t++)
				for (int i = 0; i < p.tracks[t].items.size () && !used; i++)
				{
					const Module *m = p.tracks[t].items[i].module;
					if (m && m->kind == M_PLAYRIFF && ((const PlayRiffModule *) m)->riffId == p.riffs[r].id) used = true;
				}
			if (!used) p.riffs.removeAt (r);
		}
	}

private:
	char *m_undo[MAX_UNDO], *m_redo[MAX_UNDO];
	int m_nUndo, m_nRedo;
	char *snapshot ()
	{
		json::Writer w (false);
		saveProject (p, w);
		if (!w.ok ()) return 0;
		return w.release ();
	}
	void restore (const char *s)
	{
		Project np; char err[64];
		if (loadProject (s, strlen (s), np, err, sizeof err)) p = np;
		sel = Selection ();
		if (selTrack >= p.tracks.size ()) selTrack = 0;
	}
};

extern Doc &g_doc;		// (made in main: a static one would allocate before the kernel's table is there)

} // namespace kui

#endif
