//
// model.cpp -- the song model (model.h): geometry (item starts, lengths) and the files: Koton's .sq
// read with json.hpp (System.Text.Json's layout: PascalCase members, enums as numbers or names,
// "$type" on the modules), Onyx's .kson written the same way (plus its "Onyx..." members).
//
#include "model.h"
#include "theory.h"
#include "json.hpp"
#include <stdio.h>
#include <string.h>

namespace kt {

// ---- small pieces -------------------------------------------------------------------------------------------
float RiffNote::bendAt (int off) const
{
	if (bend.size () == 0) return 0;
	if (off <= bend[0].off) return bend[0].semis;
	for (int i = 1; i < bend.size (); i++)
		if (off <= bend[i].off)
		{
			int span = bend[i].off - bend[i - 1].off;
			if (span <= 0) return bend[i].semis;
			float f = (off - bend[i - 1].off) / (float) span;
			return bend[i - 1].semis + (bend[i].semis - bend[i - 1].semis) * f;
		}
	return bend.back ().semis;
}

void CustomGrid::setNotes (const Vec<RiffNote> &n, int slicesPerQuarter, int lengthSlices)
{
	if (&n != &notes) notes = n;
	hasNotes = true;
	int len = imax (lengthSlices, notes::lengthOf (notes));
	slices = notes::toSlices (notes, len);
	spq = imax (1, slicesPerQuarter);
}

static const char *const s_typeNames[M_KINDS] = {
	"PlayRiff", "Pattern", "DrumKit", "Cadence", "MelodicLine", "PolyDrum", "MelodicPoly", "PolyChord", "ChordArticulation", "KotonGenerator" };
const char *moduleTypeName (int kind) { return (kind >= 0 && kind < M_KINDS) ? s_typeNames[kind] : "?"; }

Module::Module (int k) : kind (k), id (newId ()), x (0), y (0), widthHint (0), collapsed (false) {}

static unsigned s_idState = 0x2545F491u;
void seedIds (unsigned seed) { s_idState = seed ? seed : 1; }
Str newId ()
{
	// xorshift, formatted as a version-4 Guid (enough for ids inside one project)
	char b[40]; static const char hx[] = "0123456789abcdef";
	unsigned char r[16];
	for (int i = 0; i < 16; i++) { s_idState ^= s_idState << 13; s_idState ^= s_idState >> 17; s_idState ^= s_idState << 5; r[i] = (unsigned char) (s_idState >> 11); }
	r[6] = (unsigned char) ((r[6] & 0x0F) | 0x40); r[8] = (unsigned char) ((r[8] & 0x3F) | 0x80);
	int o = 0;
	for (int i = 0; i < 16; i++)
	{
		if (i == 4 || i == 6 || i == 8 || i == 10) b[o++] = '-';
		b[o++] = hx[r[i] >> 4]; b[o++] = hx[r[i] & 15];
	}
	b[o] = 0;
	return Str (b);
}

// ---- the project ------------------------------------------------------------------------------------------------
Project::Project () : timeSigNum (4), timeSigDen (4), pickupBeats (0), timeSigScale (1), minBeats (0), swingPercent (50),
	humanizePercent (0), agogicPercent (0), arrangement (0), reverbBusMix (0.25)
{
	TempoChange t = { 0, 120 }; tempo.push (t);
}
Project::Project (const Project &o) : arrangement (0) { *this = o; }
Project &Project::operator= (const Project &o)
{
	if (this == &o) return *this;
	tempo = o.tempo; tracks = o.tracks;
	userChordStyles = o.userChordStyles; userMelodicLines = o.userMelodicLines; userDrumStyles = o.userDrumStyles;
	key = o.key; timeSigNum = o.timeSigNum; timeSigDen = o.timeSigDen;
	pickupBeats = o.pickupBeats; timeSigScale = o.timeSigScale; minBeats = o.minBeats; swingPercent = o.swingPercent;
	humanizePercent = o.humanizePercent; agogicPercent = o.agogicPercent;
	markers = o.markers; riffs = o.riffs; reverbBusMix = o.reverbBusMix;
	delete arrangement; arrangement = o.arrangement ? new Arrangement (*o.arrangement) : 0;
	return *this;
}

const Riff *Project::riffById (const char *id) const
{
	for (int i = 0; i < riffs.size (); i++) if (riffs[i].id == id) return &riffs[i];
	return 0;
}
Riff *Project::riffById (const char *id)
{
	for (int i = 0; i < riffs.size (); i++) if (riffs[i].id == id) return &riffs[i];
	return 0;
}
double Project::bpmAt (double beat) const
{
	double bpm = tempo.size () ? tempo[0].bpm : 120;
	for (int i = 0; i < tempo.size (); i++) { if (tempo[i].beat <= beat + 1e-9) bpm = tempo[i].bpm > 0 ? tempo[i].bpm : 120; else break; }
	return bpm;
}
int Project::barBeats () const { return timeSigDen == 8 ? imax (1, timeSigNum / 3) : imax (1, timeSigNum); }
double Project::itemLength (const Item &it) const { return it.module ? moduleBeats (it.module, *this) : 0; }
double Project::trackEnd (const Track &t) const
{
	double c = 0;
	for (int i = 0; i < t.items.size (); i++) c += t.items[i].silenceBefore + itemLength (t.items[i]);
	return c;
}
double Project::itemStart (const Track &t, int index) const
{
	double c = 0;
	for (int i = 0; i < t.items.size (); i++)
	{
		c += t.items[i].silenceBefore;
		if (i == index) return c;
		c += itemLength (t.items[i]);
	}
	return c;
}
double Project::totalBeats () const
{
	double t = minBeats;
	for (int i = 0; i < tracks.size (); i++) t = dmax (t, trackEnd (tracks[i]));
	return t;
}
int Project::chordTrackIndex () const
{
	for (int i = 0; i < tracks.size (); i++) if (tracks[i].type == TRACK_CHORD) return i;
	return -1;
}

// ---- reading ------------------------------------------------------------------------------------------------------
typedef json::Value V;

static int rdEnum (const V &v, int def, const char *const *names = 0, int n = 0)
{
	if (v.isNum ()) return v.asInt (def);
	if (v.isStr () && names)
		for (int i = 0; i < n; i++) if (json::seqi (v.asStr (), names[i])) return i;
	return v.isStr () ? v.asInt (def) : def;
}
static void rdSlices (const V &a, Vec<Slice> &out)
{
	out.clear ();
	if (!a.isArr ()) return;
	for (const V *e = a.first (); e; e = e->next) { Slice s; s.lo = (*e)["NotesLow"].asU64 (); s.hi = (*e)["NotesHigh"].asU64 (); out.push (s); }
}
static void rdNotes (const V &a, Vec<RiffNote> &out)
{
	out.clear ();
	if (!a.isArr ()) return;
	for (const V *e = a.first (); e; e = e->next)
	{
		RiffNote n;
		if (e->isArr ())			// the compact form [note, start, length]
		{
			n.note = (*e)[0].asInt (); n.start = (*e)[1].asInt (); n.length = imax (1, (*e)[2].asInt (1)); n.glideFrom = n.note;
			out.push (n);
			continue;
		}
		n.note = (*e)["Note"].asInt (); n.start = (*e)["Start"].asInt (); n.length = imax (1, (*e)["Length"].asInt (1));
		n.voice = (*e)["Voice"].asInt (0);
		n.glideFrom = (*e)["GlideFromNote"].asInt (n.note); n.glideDur = (*e)["GlideDurationSlices"].asInt (0);
		const V &b = (*e)["Bend"];
		for (const V *bp = b.first (); bp; bp = bp->next) { BendPoint p = { (*bp)["Off"].asInt (), (float) (*bp)["Semis"].asDouble () }; n.bend.push (p); }
		out.push (n);
	}
}
static void rdIntArr (const V &a, Vec<int> &out) { out.clear (); for (const V *e = a.first (); e; e = e->next) out.push (e->asInt ()); }
static void rdGrid (const V &o, const char *slicesKey, const char *spqKey, const char *notesKey, CustomGrid &g)
{
	rdSlices (o[slicesKey], g.slices);
	g.spq = imax (1, o[spqKey].asInt (4));
	g.hasNotes = o[notesKey].isArr ();
	rdNotes (o[notesKey], g.notes);
}

static Module *rdModule (const V &o)
{
	const char *t = o["$type"].asStr ("");
	Module *m = 0;
	if (!strcmp (t, "PlayRiff")) { PlayRiffModule *p = new PlayRiffModule; p->riffId = o["RiffId"].asStr (""); m = p; }
	else if (!strcmp (t, "Pattern"))
	{
		PatternModule *p = new PatternModule;
		p->root = imod (o["Root"].asInt (0), 12); p->octave = o["Octave"].asInt (4); p->quality = iclamp (o["Quality"].asInt (0), 0, QUALITY_COUNT - 1);
		p->inversion = imax (0, o["Inversion"].asInt (0)); p->style = o["Style"].asInt (0); p->beatsPerBar = imax (1, o["BeatsPerBar"].asInt (4));
		p->repeats = imax (1, o["Repeats"].asInt (1)); p->bass = o["Bass"].asBool (); p->bassPerBeat = o["BassPerBeat"].asBool ();
		p->heldMode = iclamp (o["HeldMode"].asInt (0), 0, 3); p->climbMode = iclamp (o["ClimbMode"].asInt (0), 0, 3);
		p->halveDurations = o["HalveDurations"].asBool ();
		int d = o["Degree"].asInt (-1); p->degree = d < 0 ? -1 : imin (6, d);
		p->voiceLeadMode = iclamp (o["VoiceLeadMode"].asInt (0), 0, 4);
		p->diatonicColour = iclamp (o["DiatonicColour"].asInt (0), 0, 4); p->suspension = iclamp (o["Suspension"].asInt (0), 0, 2);
		p->modeOverride = iclamp (o["ModeOverride"].asInt (0), 0, 5); p->openVoicing = o["OpenVoicing"].asBool ();
		p->melodicOctave = o["MelodicOctave"].asInt (5); p->melodicAnchor = iclamp (o["MelodicAnchor"].asInt (0), 0, 1);
		p->melodicOpenVoicing = o["MelodicOpenVoicing"].asBool (); p->melodicVoiceLead = iclamp (o["MelodicVoiceLead"].asInt (0), 0, 3);
		p->melodicPreserve = o["MelodicPreserve"].asBool ();
		rdGrid (o, "CustomSlices", "CustomSlicesPerQuarter", "CustomNotes", p->custom);
		p->userStyleName = o["UserStyleName"].asStr ("");
		rdGrid (o, "MelodicSlices", "MelodicSlicesPerQuarter", "MelodicNotes", p->melodic);
		m = p;
	}
	else if (!strcmp (t, "ChordArticulation"))
	{
		ArticulationModule *p = new ArticulationModule;
		p->beats = dmax (0.25, o["Beats"].asDouble (4)); double lb = o["LengthBeats"].asDouble (0); p->lengthBeats = lb <= 0 ? 0 : dmax (0.25, lb);
		p->style = o["Style"].asInt (0); p->octave = o["Octave"].asInt (4); p->inversion = imax (0, o["Inversion"].asInt (0));
		p->voiceLeadMode = iclamp (o["VoiceLeadMode"].asInt (0), 0, 4); p->voiceLeadDirection = iclamp (o["VoiceLeadDirection"].asInt (0), 0, 2);
		p->openVoicingMode = iclamp (o["OpenVoicingMode"].asInt (0), 0, 2); p->openVoicing = o["OpenVoicing"].asBool ();
		p->bass = o["Bass"].asBool (); p->bassPerBeat = o["BassPerBeat"].asBool ();
		p->heldMode = iclamp (o["HeldMode"].asInt (0), 0, 3); p->climbMode = iclamp (o["ClimbMode"].asInt (0), 0, 3);
		p->halveDurations = o["HalveDurations"].asBool ();
		rdGrid (o, "CustomSlices", "CustomSlicesPerQuarter", "CustomNotes", p->custom);
		p->userStyleName = o["UserStyleName"].asStr ("");
		p->melodicOctave = o["MelodicOctave"].asInt (5); p->melodicAnchor = iclamp (o["MelodicAnchor"].asInt (0), 0, 1);
		p->melodicOpenVoicing = o["MelodicOpenVoicing"].asBool (); p->melodicVoiceLead = imax (0, o["MelodicVoiceLead"].asInt (0));
		rdGrid (o, "MelodicSlices", "MelodicSlicesPerQuarter", "MelodicNotes", p->melodic);
		m = p;
	}
	else if (!strcmp (t, "DrumKit"))
	{
		DrumModule *p = new DrumModule;
		p->style = o["Style"].asInt (0); p->density = o["Density"].asInt (0); p->kit = o["Kit"].asInt (0);
		p->fillLast = o["FillLast"].asBool (); p->beatsPerBar = imax (1, o["BeatsPerBar"].asInt (4)); p->repeats = imax (1, o["Repeats"].asInt (4));
		p->catCategory = o["CatCategory"].asStr (""); p->catMotif = o["CatMotif"].asStr ("");
		rdGrid (o, "CustomSlices", "CustomSlicesPerQuarter", "CustomNotes", p->custom);
		m = p;
	}
	else if (!strcmp (t, "Cadence"))
	{
		CadenceModule *p = new CadenceModule;
		p->octave = o["Octave"].asInt (4); p->style = o["Style"].asInt (0); p->beatsPerBar = imax (1, o["BeatsPerBar"].asInt (1));
		p->bass = o["Bass"].asBool (); p->bassPerBeat = o["BassPerBeat"].asBool ();
		p->heldMode = iclamp (o["HeldMode"].asInt (0), 0, 3); p->climbMode = iclamp (o["ClimbMode"].asInt (0), 0, 3);
		p->halveDurations = o["HalveDurations"].asBool (); p->cadenceStyle = o["CadenceStyle"].asInt (0);
		p->startDegree = iclamp (o["StartDegree"].asInt (0), 0, 6); p->measures = imax (1, o["Measures"].asInt (4));
		p->chordsPerMeasure = imax (1, o["ChordsPerMeasure"].asInt (1)); p->voiceLeadMode = iclamp (o["VoiceLeadMode"].asInt (1), 0, 3);
		p->openVoicing = o["OpenVoicing"].asBool ();
		for (const V *c = o["Chords"].first (); c; c = c->next)
		{
			CadenceChord cc;
			cc.root = imod ((*c)["Root"].asInt (), 12); cc.quality = iclamp ((*c)["Quality"].asInt (), 0, QUALITY_COUNT - 1);
			cc.inversion = (*c)["Inversion"].asInt (); cc.octaveShift = (*c)["OctaveShift"].asInt ();
			cc.heldVoice = (*c)["HeldVoice"].asInt (-1); cc.degree = (*c)["Degree"].asInt (-1);
			p->chords.push (cc);
		}
		rdGrid (o, "CustomSlices", "CustomSlicesPerQuarter", "CustomNotes", p->custom);
		m = p;
	}
	else if (!strcmp (t, "MelodicLine"))
	{
		MelodicLineModule *p = new MelodicLineModule;
		p->beatsPerBar = imax (1, o["BeatsPerBar"].asInt (4)); p->voiceCount = iclamp (o["VoiceCount"].asInt (1), 1, 3);
		p->lineName = o["LineName"].asStr (""); p->preserve = o["Preserve"].asBool ();
		rdGrid (o, "Slices", "SlicesPerQuarter", "Notes", p->rhythm);
		p->registerShift = o["RegisterShift"].asInt (0); p->contour = o["Contour"].asInt (0); p->anchor = o["Anchor"].asInt (0);
		p->continuity = o["Continuity"].asInt (0); p->variation = o["Variation"].asInt (0); p->tensionSlope = o["TensionSlope"].asInt (0);
		p->amplitude = o["Amplitude"].asInt (12); p->ornaments = o["Ornaments"].asInt (0); p->waveLength = o["WaveLength"].asInt (0);
		m = p;
	}
	else if (!strcmp (t, "PolyDrum"))
	{
		PolyDrumModule *p = new PolyDrumModule;
		p->kit = o["Kit"].asInt (0); p->beats = imax (1, o["Beats"].asInt (4)); p->repeats = imax (1, o["Repeats"].asInt (4));
		p->beatsPerBar = imax (1, o["BeatsPerBar"].asInt (4)); p->durationBeats = o["DurationBeats"].asInt (0);
		for (const V *l = o["Layers"].first (); l; l = l->next)
		{
			EuclidLayer e;
			e.lane = (*l)["Lane"].asInt (0); e.accentLane = (*l)["AccentLane"].asInt (-1); e.hits = (*l)["Hits"].asInt (3);
			e.steps = (*l)["Steps"].asInt (8); e.rotation = (*l)["Rotation"].asInt (0); e.muted = (*l)["Muted"].asBool ();
			e.collapsed = (*l)["Collapsed"].asBool (); e.customMode = (*l)["CustomMode"].asBool (); rdIntArr ((*l)["CustomHits"], e.customHits);
			p->layers.push (e);
		}
		m = p;
	}
	else if (!strcmp (t, "MelodicPoly"))
	{
		MelodicPolyModule *p = new MelodicPolyModule;
		p->beats = imax (1, o["Beats"].asInt (4)); p->repeats = imax (1, o["Repeats"].asInt (4));
		p->beatsPerBar = imax (1, o["BeatsPerBar"].asInt (4)); p->durationBeats = o["DurationBeats"].asInt (0);
		for (const V *l = o["Layers"].first (); l; l = l->next)
		{
			EuclidVoice e;
			e.voice = (*l)["Voice"].asInt (0); e.hits = (*l)["Hits"].asInt (3); e.steps = (*l)["Steps"].asInt (8);
			e.rotation = (*l)["Rotation"].asInt (0); e.octave = iclamp ((*l)["Octave"].asInt (0), -3, 3); e.muted = (*l)["Muted"].asBool ();
			e.collapsed = (*l)["Collapsed"].asBool (); e.legato = (*l)["Legato"].asBool (); e.customMode = (*l)["CustomMode"].asBool ();
			rdIntArr ((*l)["CustomHits"], e.customHits);
			p->layers.push (e);
		}
		m = p;
	}
	else if (!strcmp (t, "PolyChord"))
	{
		PolyChordModule *p = new PolyChordModule;
		static const char *const modes[] = { "OneRingPerTone", "OneRingSweep" };
		static const char *const restarts[] = { "Nearest", "Grave", "Aigu", "Tonic", "Tierce", "Quinte" };
		static const char *const monos[] = { "Highest", "Lowest", "Auto", "Random" };
		p->octave = o["Octave"].asInt (4); p->cycleBeats = imax (1, o["CycleBeats"].asInt (4));
		double b = o["Beats"].asDouble (0); p->beats = b <= 0 ? 0 : dmax (0.25, b);
		p->openVoicing = o["OpenVoicing"].asBool (); p->voiceLeadAnchor = iclamp (o["VoiceLeadAnchor"].asInt (0), 0, 2);
		p->mode = rdEnum (o["Mode"], 0, modes, 2); p->restart = rdEnum (o["Restart"], 0, restarts, 6);
		p->monodicPick = o["MonodicPick"].asBool (); p->monodicSeed = o["MonodicSeed"].asInt (42);
		p->monodicAvoidRepeat = o["MonodicAvoidRepeat"].asBool (true); p->monodicStrategy = rdEnum (o["MonodicStrategy"], MONO_AUTO, monos, 4);
		for (const V *c = o["Chords"].first (); c; c = c->next)
		{
			PolyChordItem it;
			it.root = imod ((*c)["Root"].asInt (), 12); it.quality = iclamp ((*c)["Quality"].asInt (), 0, QUALITY_COUNT - 1);
			int d = (*c)["Degree"].asInt (-1); it.degree = d < 0 ? -1 : imin (6, d);
			it.diatonicColour = iclamp ((*c)["DiatonicColour"].asInt (), 0, 4); it.suspension = iclamp ((*c)["Suspension"].asInt (), 0, 2);
			it.modeOverride = iclamp ((*c)["ModeOverride"].asInt (), 0, 5); it.beats = imax (1, (*c)["Beats"].asInt (4));
			it.inversion = imax (0, (*c)["Inversion"].asInt ()); it.octaveShift = (*c)["OctaveShift"].asInt ();
			p->chords.push (it);
		}
		for (const V *l = o["Layers"].first (); l; l = l->next)
		{
			EuclidChordLayer e;
			e.hits = (*l)["Hits"].asInt (3); e.steps = (*l)["Steps"].asInt (8); e.rotation = (*l)["Rotation"].asInt (0);
			e.octave = iclamp ((*l)["Octave"].asInt (0), -3, 3); e.toneIndex = (*l)["ToneIndex"].asInt (0);
			e.contour = imax (0, (*l)["Contour"].asInt (0)); e.randomSeed = (*l)["RandomSeed"].asInt (0);
			e.muted = (*l)["Muted"].asBool (); e.collapsed = (*l)["Collapsed"].asBool (); e.customMode = (*l)["CustomMode"].asBool ();
			e.legato = (*l)["Legato"].asBool (); rdIntArr ((*l)["CustomHits"], e.customHits);
			p->layers.push (e);
		}
		m = p;
	}
	else if (!strcmp (t, "KotonGenerator"))
	{
		GeneratorModule *p = new GeneratorModule;
		p->generatorId = o["GeneratorId"].asStr (""); p->state = o["GeneratorState"].asStr ("");
		p->durationBeats = dmax (0.25, o["DurationBeats"].asDouble (4));
		m = p;
	}
	if (!m) return 0;
	if (o["Id"].isStr ()) m->id = o["Id"].asStr ();
	m->x = o["X"].asDouble (0); m->y = o["Y"].asDouble (0); m->widthHint = o["WidthHint"].asDouble (0); m->collapsed = o["Collapsed"].asBool ();
	return m;
}

static void rdPlugin (const V &o, PluginSlot &s) { s.id = o["Id"].asStr (""); s.enabled = o["Enabled"].asBool (true); s.state = o["State"].asStr (""); }

static void rdUserStyles (const V &a, Vec<UserStyle> &out)
{
	for (const V *e = a.first (); e; e = e->next)
	{
		UserStyle u;
		u.name = (*e)["Name"].asStr (""); u.spb = imax (1, (*e)["Spb"].asInt (4)); u.beats = imax (1, (*e)["Beats"].asInt (4));
		rdSlices ((*e)["Slices"], u.slices);
		u.hasNotes = (*e)["Notes"].isArr ();
		rdNotes ((*e)["Notes"], u.notes);
		out.push (u);
	}
}

static void rdRiff (const V &o, Riff &r)
{
	r.id = o["Id"].asStr (""); r.name = o["Name"].asStr ("Riff");
	rdNotes (o["Notes"], r.notes);
	r.lengthSlices = o["LengthSlices"].asInt (96); r.spq = imax (1, o["SlicesPerQuarter"].asInt (24));
	if (r.notes.size () == 0 && o["Slices"].isArr ())			// an old file: the slice grid
	{
		Vec<Slice> s; rdSlices (o["Slices"], s);
		if (s.size () > 0) { r.notes = notes::fromSlices (s); r.lengthSlices = s.size (); }
	}
	if (r.id.empty ()) r.id = newId ();
}

bool loadProject (const char *text, unsigned long len, Project &out, char *err, int errcap)
{
	json::Doc d;
	if (!d.parse (text, len, json::TOLERANT))
	{
		if (err) snprintf (err, errcap, "%s (line %d, column %d)", d.error (), d.errLine (), d.errCol ());
		return false;
	}
	const V &root = d.root ();
	const V &pj = root.has ("Project") ? root["Project"] : root;
	if (!pj.isObj ()) { if (err) snprintf (err, errcap, "not a Koton project"); return false; }
	Project p;
	p.tempo.clear ();
	for (const V *t = pj["Tempo"].first (); t; t = t->next) { TempoChange tc = { (*t)["Beat"].asDouble (0), (*t)["Bpm"].asDouble (120) }; if (tc.bpm <= 0) tc.bpm = 120; p.tempo.push (tc); }
	if (p.tempo.size () == 0) { TempoChange tc = { 0, 120 }; p.tempo.push (tc); }
	p.tempo.sort ([] (const TempoChange &a, const TempoChange &b) { return a.beat < b.beat; });
	const V &k = pj["Key"];
	p.key.tonicLetter = iclamp (k["TonicLetter"].asInt (0), 0, 6); p.key.accidental = iclamp (k["Accidental"].asInt (0), -1, 1);
	p.key.mode = k["Mode"].asInt (0) == 1 ? 1 : 0; p.key.fullMode = k["FullMode"].asInt (-1);
	p.timeSigNum = imax (1, pj["TimeSigNum"].asInt (4)); p.timeSigDen = imax (1, pj["TimeSigDen"].asInt (4));
	p.pickupBeats = pj["PickupBeats"].asDouble (0); p.timeSigScale = pj["TimeSigScale"].asDouble (1); p.minBeats = pj["MinBeats"].asDouble (0);
	p.swingPercent = pj["SwingPercent"].asDouble (50); p.humanizePercent = pj["HumanizePercent"].asInt (0); p.agogicPercent = pj["AgogicPercent"].asInt (0);
	p.reverbBusMix = pj["OnyxReverbMix"].asDouble (0.25);
	for (const V *mk = pj["Markers"].first (); mk; mk = mk->next) { Marker m; m.beat = (*mk)["Beat"].asDouble (0); m.name = (*mk)["Name"].asStr (""); p.markers.push (m); }
	rdUserStyles (pj["UserChordStyles"], p.userChordStyles);
	rdUserStyles (pj["UserMelodicLines"], p.userMelodicLines);
	rdUserStyles (pj["UserDrumStyles"], p.userDrumStyles);
	const V &ar = pj["Arrangement"];
	if (ar.isObj () && ar["Chords"].size () > 0)
	{
		p.arrangement = new Arrangement;
		p.arrangement->slicesPerQuarter = imax (1, ar["SlicesPerQuarter"].asInt (24));
		p.arrangement->chordSlices = imax (1, ar["ChordSlices"].asInt (96));
		for (const V *c = ar["Chords"].first (); c; c = c->next) { ChordCell cc = { imod ((*c)["Root"].asInt (), 12), iclamp ((*c)["Quality"].asInt (), 0, QUALITY_COUNT - 1) }; p.arrangement->chords.push (cc); }
	}
	static const char *const trackTypes[] = { "Instrument", "Drum", "Chord" };
	static const char *const params[] = { "Volume", "Pan", "Expression", "Modulation", "Sustain", "ReverbSend", "ChorusSend", "PitchBend", "Staccato" };
	for (const V *t = pj["Tracks"].first (); t; t = t->next)
	{
		Track tr;
		tr.name = (*t)["Name"].asStr ("Track"); tr.type = iclamp (rdEnum ((*t)["Type"], 0, trackTypes, 3), 0, 2);
		tr.instrument = iclamp ((*t)["Instrument"].asInt (0), 0, 127); tr.drumKit = (*t)["DrumKit"].asInt (0);
		tr.volume = (*t)["Volume"].asDouble (1); tr.pan = (*t)["Pan"].asDouble (0); tr.mute = (*t)["Mute"].asBool (); tr.solo = (*t)["Solo"].asBool ();
		tr.collapsed = (*t)["Collapsed"].asBool (); tr.reverbOffset = iclamp ((*t)["ReverbOffset"].asInt (0), -DEFAULT_REVERB, 127 - DEFAULT_REVERB);
		tr.reverbBusSend = dmax (0, dmin (1, (*t)["ReverbBusSend"].asDouble (0)));
		for (const V *v = (*t)["VolumeAutomation"].first (); v; v = v->next) { VolumePoint vp = { (*v)["Beat"].asDouble (0), (*v)["Volume"].asDouble (1) }; tr.volumeAutomation.push (vp); }
		tr.volumeAutomation.sort ([] (const VolumePoint &a, const VolumePoint &b) { return a.beat < b.beat; });
		for (const V *l = (*t)["AutomationLanes"].first (); l; l = l->next)
		{
			AutomationLane al; al.param = rdEnum ((*l)["Param"], 0, params, 9); al.enabled = (*l)["Enabled"].asBool (true);
			for (const V *pt = (*l)["Points"].first (); pt; pt = pt->next) { AutomationPoint ap = { (*pt)["Beat"].asDouble (0), (*pt)["Value"].asDouble (0) }; al.points.push (ap); }
			al.points.sort ([] (const AutomationPoint &a, const AutomationPoint &b) { return a.beat < b.beat; });
			tr.lanes.push (al);
		}
		rdPlugin ((*t)["OnyxInstrument"], tr.instrumentPlugin);
		for (const V *s = (*t)["OnyxInserts"].first (); s; s = s->next) { PluginSlot ps; rdPlugin (*s, ps); tr.inserts.push (ps); }
		for (const V *it = (*t)["Items"].first (); it; it = it->next)
		{
			Module *m = (*it)["Module"].isObj () ? rdModule ((*it)["Module"]) : 0;
			if (!m) continue;				// a Repeat or a module Onyx does not know: dropped
			tr.items.push (Item ((*it)["SilenceBefore"].asDouble (0), m));
		}
		p.tracks.push (move (tr));
	}
	for (const V *r = root["Riffs"].first (); r; r = r->next) { Riff rf; rdRiff (*r, rf); p.riffs.push (move (rf)); }
	// a chord track pinned last (Koton's EnsureChordTrack): an old project with none gets one
	int ci = p.chordTrackIndex ();
	if (ci < 0) { Track c; c.name = "Chords"; c.type = TRACK_CHORD; p.tracks.push (move (c)); }
	else if (ci != p.tracks.size () - 1) { Track c = move (p.tracks[ci]); p.tracks.removeAt (ci); p.tracks.push (move (c)); }
	out = p;
	return true;
}

// ---- writing ----------------------------------------------------------------------------------------------------------
static void wrSlices (json::Writer &w, const char *k, const Vec<Slice> &s, bool emptyAsArray = false)
{
	if (s.size () == 0 && !emptyAsArray) { w.key (k); w.null (); return; }
	w.key (k); w.beginArr ();
	for (int i = 0; i < s.size (); i++) { w.beginObj (true); w.key ("NotesLow"); w.u64 (s[i].lo); w.key ("NotesHigh"); w.u64 (s[i].hi); w.endObj (); }
	w.endArr ();
}
static void wrNotes (json::Writer &w, const char *k, const Vec<RiffNote> &n, bool present = true)
{
	w.key (k);
	if (!present) { w.null (); return; }
	w.beginArr ();
	for (int i = 0; i < n.size (); i++)
	{
		const RiffNote &r = n[i];
		w.beginObj (true);
		w.key ("Note"); w.num (r.note); w.key ("Start"); w.num (r.start); w.key ("Length"); w.num (r.length);
		w.key ("Bend");
		if (r.bend.size () == 0) w.null ();
		else { w.beginArr (true); for (int j = 0; j < r.bend.size (); j++) { w.beginObj (true); w.key ("Off"); w.num (r.bend[j].off); w.key ("Semis"); w.num ((double) r.bend[j].semis); w.endObj (); } w.endArr (); }
		w.key ("Voice"); w.num (r.voice); w.key ("GlideFromNote"); w.num (r.glideFrom); w.key ("GlideDurationSlices"); w.num (r.glideDur);
		w.endObj ();
	}
	w.endArr ();
}
static void wrGrid (json::Writer &w, const char *sk, const char *qk, const char *nk, const CustomGrid &g)
{
	wrSlices (w, sk, g.slices); w.key (qk); w.num (g.spq); wrNotes (w, nk, g.notes, g.hasNotes || g.notes.size () > 0);
}
static void wrIntArr (json::Writer &w, const char *k, const Vec<int> &a, bool nullIfEmpty = true)
{
	w.key (k);
	if (a.size () == 0 && nullIfEmpty) { w.null (); return; }
	w.beginArr (true); for (int i = 0; i < a.size (); i++) w.num (a[i]); w.endArr ();
}
static void kvI (json::Writer &w, const char *k, int v) { w.key (k); w.num (v); }
static void kvD (json::Writer &w, const char *k, double v) { w.key (k); w.num (v); }
static void kvB (json::Writer &w, const char *k, bool v) { w.key (k); w.boolean (v); }
static void kvS (json::Writer &w, const char *k, const Str &v, bool nullIfEmpty = false) { w.key (k); if (nullIfEmpty && v.empty ()) w.null (); else w.str (v.c ()); }

static void wrModule (json::Writer &w, const Module &m)
{
	w.beginObj ();
	w.key ("$type"); w.str (m.typeName ());
	kvS (w, "Id", m.id); kvD (w, "X", m.x); kvD (w, "Y", m.y); kvD (w, "WidthHint", m.widthHint); kvB (w, "Collapsed", m.collapsed);
	switch (m.kind)
	{
	case M_PLAYRIFF: kvS (w, "RiffId", ((const PlayRiffModule &) m).riffId); break;
	case M_PATTERN:
	{
		const PatternModule &p = (const PatternModule &) m;
		kvI (w, "Root", p.root); kvI (w, "Degree", p.degree); kvB (w, "Bass", p.bass); kvB (w, "BassPerBeat", p.bassPerBeat);
		kvI (w, "HeldMode", p.heldMode); kvI (w, "ClimbMode", p.climbMode); kvB (w, "HalveDurations", p.halveDurations);
		kvI (w, "Octave", p.octave); kvI (w, "Quality", p.quality); kvI (w, "Inversion", p.inversion); kvI (w, "VoiceLeadMode", p.voiceLeadMode);
		kvI (w, "DiatonicColour", p.diatonicColour); kvI (w, "Suspension", p.suspension); kvI (w, "ModeOverride", p.modeOverride);
		kvB (w, "OpenVoicing", p.openVoicing); kvI (w, "Style", p.style); kvI (w, "BeatsPerBar", p.beatsPerBar); kvI (w, "Repeats", p.repeats);
		wrGrid (w, "CustomSlices", "CustomSlicesPerQuarter", "CustomNotes", p.custom);
		kvS (w, "UserStyleName", p.userStyleName, true);
		wrGrid (w, "MelodicSlices", "MelodicSlicesPerQuarter", "MelodicNotes", p.melodic);
		kvI (w, "MelodicOctave", p.melodicOctave); kvI (w, "MelodicAnchor", p.melodicAnchor); kvB (w, "MelodicOpenVoicing", p.melodicOpenVoicing);
		kvI (w, "MelodicVoiceLead", p.melodicVoiceLead); kvB (w, "MelodicPreserve", p.melodicPreserve);
	} break;
	case M_ARTICULATION:
	{
		const ArticulationModule &p = (const ArticulationModule &) m;
		kvD (w, "Beats", p.beats); kvD (w, "LengthBeats", p.lengthBeats); kvI (w, "Style", p.style); kvI (w, "Octave", p.octave);
		kvI (w, "Inversion", p.inversion); kvI (w, "VoiceLeadMode", p.voiceLeadMode); kvI (w, "VoiceLeadDirection", p.voiceLeadDirection);
		kvI (w, "OpenVoicingMode", p.openVoicingMode); kvB (w, "OpenVoicing", p.openVoicing); kvB (w, "Bass", p.bass); kvB (w, "BassPerBeat", p.bassPerBeat);
		kvI (w, "HeldMode", p.heldMode); kvI (w, "ClimbMode", p.climbMode); kvB (w, "HalveDurations", p.halveDurations);
		wrGrid (w, "CustomSlices", "CustomSlicesPerQuarter", "CustomNotes", p.custom);
		kvS (w, "UserStyleName", p.userStyleName, true);
		kvI (w, "MelodicOctave", p.melodicOctave); kvI (w, "MelodicAnchor", p.melodicAnchor); kvB (w, "MelodicOpenVoicing", p.melodicOpenVoicing);
		kvI (w, "MelodicVoiceLead", p.melodicVoiceLead);
		wrGrid (w, "MelodicSlices", "MelodicSlicesPerQuarter", "MelodicNotes", p.melodic);
	} break;
	case M_DRUMKIT:
	{
		const DrumModule &p = (const DrumModule &) m;
		kvI (w, "Kit", p.kit); kvI (w, "Style", p.style); kvI (w, "Density", p.density); kvB (w, "FillLast", p.fillLast);
		kvI (w, "BeatsPerBar", p.beatsPerBar); kvI (w, "Repeats", p.repeats);
		kvS (w, "CatCategory", p.catCategory, true); kvS (w, "CatMotif", p.catMotif, true);
		wrGrid (w, "CustomSlices", "CustomSlicesPerQuarter", "CustomNotes", p.custom);
	} break;
	case M_CADENCE:
	{
		const CadenceModule &p = (const CadenceModule &) m;
		kvI (w, "Octave", p.octave); kvI (w, "Style", p.style); kvI (w, "BeatsPerBar", p.beatsPerBar); kvB (w, "Bass", p.bass);
		kvB (w, "BassPerBeat", p.bassPerBeat); kvI (w, "HeldMode", p.heldMode); kvI (w, "ClimbMode", p.climbMode);
		kvB (w, "HalveDurations", p.halveDurations); kvI (w, "CadenceStyle", p.cadenceStyle); kvI (w, "StartDegree", p.startDegree);
		kvI (w, "Measures", p.measures); kvI (w, "ChordsPerMeasure", p.chordsPerMeasure); kvI (w, "VoiceLeadMode", p.voiceLeadMode);
		kvB (w, "OpenVoicing", p.openVoicing);
		w.key ("Chords"); w.beginArr ();
		for (int i = 0; i < p.chords.size (); i++)
		{
			const CadenceChord &c = p.chords[i];
			w.beginObj (true); kvI (w, "Root", c.root); kvI (w, "Quality", c.quality); kvI (w, "Inversion", c.inversion);
			kvI (w, "OctaveShift", c.octaveShift); kvI (w, "HeldVoice", c.heldVoice); kvI (w, "Degree", c.degree); w.endObj ();
		}
		w.endArr ();
		wrGrid (w, "CustomSlices", "CustomSlicesPerQuarter", "CustomNotes", p.custom);
	} break;
	case M_MELODICLINE:
	{
		const MelodicLineModule &p = (const MelodicLineModule &) m;
		kvB (w, "Preserve", p.preserve); kvI (w, "BeatsPerBar", p.beatsPerBar); kvI (w, "VoiceCount", p.voiceCount); kvS (w, "LineName", p.lineName, true);
		wrGrid (w, "Slices", "SlicesPerQuarter", "Notes", p.rhythm);
		kvI (w, "RegisterShift", p.registerShift); kvI (w, "Contour", p.contour); kvI (w, "Anchor", p.anchor); kvI (w, "Continuity", p.continuity);
		kvI (w, "Variation", p.variation); kvI (w, "TensionSlope", p.tensionSlope); kvI (w, "Amplitude", p.amplitude);
		kvI (w, "Ornaments", p.ornaments); kvI (w, "WaveLength", p.waveLength);
	} break;
	case M_POLYDRUM:
	{
		const PolyDrumModule &p = (const PolyDrumModule &) m;
		kvI (w, "Kit", p.kit); kvI (w, "Beats", p.beats); kvI (w, "Repeats", p.repeats); kvI (w, "BeatsPerBar", p.beatsPerBar); kvI (w, "DurationBeats", p.durationBeats);
		w.key ("Layers"); w.beginArr ();
		for (int i = 0; i < p.layers.size (); i++)
		{
			const EuclidLayer &e = p.layers[i];
			w.beginObj (true); kvI (w, "Lane", e.lane); kvI (w, "AccentLane", e.accentLane); kvI (w, "Hits", e.hits); kvI (w, "Steps", e.steps);
			kvI (w, "Rotation", e.rotation); kvB (w, "Muted", e.muted); kvB (w, "Collapsed", e.collapsed); kvB (w, "CustomMode", e.customMode);
			wrIntArr (w, "CustomHits", e.customHits); w.endObj ();
		}
		w.endArr ();
	} break;
	case M_MELODICPOLY:
	{
		const MelodicPolyModule &p = (const MelodicPolyModule &) m;
		kvI (w, "Beats", p.beats); kvI (w, "Repeats", p.repeats); kvI (w, "BeatsPerBar", p.beatsPerBar); kvI (w, "DurationBeats", p.durationBeats);
		w.key ("Layers"); w.beginArr ();
		for (int i = 0; i < p.layers.size (); i++)
		{
			const EuclidVoice &e = p.layers[i];
			w.beginObj (true); kvI (w, "Voice", e.voice); kvI (w, "Hits", e.hits); kvI (w, "Steps", e.steps); kvI (w, "Rotation", e.rotation);
			kvI (w, "Octave", e.octave); kvB (w, "Muted", e.muted); kvB (w, "Collapsed", e.collapsed); kvB (w, "Legato", e.legato);
			kvB (w, "CustomMode", e.customMode); wrIntArr (w, "CustomHits", e.customHits); w.endObj ();
		}
		w.endArr ();
	} break;
	case M_POLYCHORD:
	{
		const PolyChordModule &p = (const PolyChordModule &) m;
		kvI (w, "Octave", p.octave); kvI (w, "CycleBeats", p.cycleBeats); kvD (w, "Beats", p.beats); kvB (w, "OpenVoicing", p.openVoicing);
		kvI (w, "VoiceLeadAnchor", p.voiceLeadAnchor); kvI (w, "Mode", p.mode); kvI (w, "Restart", p.restart); kvB (w, "MonodicPick", p.monodicPick);
		kvI (w, "MonodicSeed", p.monodicSeed); kvB (w, "MonodicAvoidRepeat", p.monodicAvoidRepeat); kvI (w, "MonodicStrategy", p.monodicStrategy);
		w.key ("Chords"); w.beginArr ();
		for (int i = 0; i < p.chords.size (); i++)
		{
			const PolyChordItem &c = p.chords[i];
			w.beginObj (true); kvI (w, "Root", c.root); kvI (w, "Quality", c.quality); kvI (w, "Degree", c.degree); kvI (w, "DiatonicColour", c.diatonicColour);
			kvI (w, "Suspension", c.suspension); kvI (w, "ModeOverride", c.modeOverride); kvI (w, "Beats", c.beats);
			kvI (w, "Inversion", c.inversion); kvI (w, "OctaveShift", c.octaveShift); w.endObj ();
		}
		w.endArr ();
		w.key ("Layers"); w.beginArr ();
		for (int i = 0; i < p.layers.size (); i++)
		{
			const EuclidChordLayer &e = p.layers[i];
			w.beginObj (true); kvI (w, "Hits", e.hits); kvI (w, "Steps", e.steps); kvI (w, "Rotation", e.rotation); kvI (w, "Octave", e.octave);
			kvB (w, "Muted", e.muted); kvB (w, "Collapsed", e.collapsed); kvB (w, "Legato", e.legato); kvB (w, "CustomMode", e.customMode);
			wrIntArr (w, "CustomHits", e.customHits); kvI (w, "ToneIndex", e.toneIndex); kvI (w, "Contour", e.contour); kvI (w, "RandomSeed", e.randomSeed);
			w.endObj ();
		}
		w.endArr ();
	} break;
	case M_GENERATOR:
	{
		const GeneratorModule &p = (const GeneratorModule &) m;
		kvS (w, "GeneratorId", p.generatorId); kvS (w, "GeneratorState", p.state, true); kvD (w, "DurationBeats", p.durationBeats);
	} break;
	}
	w.endObj ();
}

static void wrPlugin (json::Writer &w, const PluginSlot &s)
{
	w.beginObj (true); kvS (w, "Id", s.id); kvB (w, "Enabled", s.enabled); kvS (w, "State", s.state, true); w.endObj ();
}

static void wrUserStyles (json::Writer &w, const char *k, const Vec<UserStyle> &a)
{
	w.key (k); w.beginArr ();
	for (int i = 0; i < a.size (); i++)
	{
		w.beginObj (); kvS (w, "Name", a[i].name); kvI (w, "Spb", a[i].spb); kvI (w, "Beats", a[i].beats);
		wrSlices (w, "Slices", a[i].slices, true);			// Koton expects an array here, never null
		wrNotes (w, "Notes", a[i].notes, a[i].hasNotes || a[i].notes.size () > 0);
		w.endObj ();
	}
	w.endArr ();
}

void saveProject (const Project &p, json::Writer &w)
{
	w.beginObj ();
	w.key ("Format"); w.str ("koton-onyx"); w.key ("Version"); w.num (1);
	w.key ("Project"); w.beginObj ();
	w.key ("Tempo"); w.beginArr ();
	for (int i = 0; i < p.tempo.size (); i++) { w.beginObj (true); kvD (w, "Beat", p.tempo[i].beat); kvD (w, "Bpm", p.tempo[i].bpm); w.endObj (); }
	w.endArr ();
	w.key ("Tracks"); w.beginArr ();
	for (int ti = 0; ti < p.tracks.size (); ti++)
	{
		const Track &t = p.tracks[ti];
		w.beginObj ();
		kvS (w, "Name", t.name); kvI (w, "Type", t.type); kvI (w, "Instrument", t.instrument); kvI (w, "DrumKit", t.drumKit);
		kvD (w, "Volume", t.volume); kvD (w, "Pan", t.pan); kvB (w, "Mute", t.mute); kvB (w, "Solo", t.solo);
		kvI (w, "ReverbOffset", t.reverbOffset); kvB (w, "Collapsed", t.collapsed); kvD (w, "ReverbBusSend", t.reverbBusSend);
		w.key ("VolumeAutomation"); w.beginArr ();
		for (int i = 0; i < t.volumeAutomation.size (); i++) { w.beginObj (true); kvD (w, "Beat", t.volumeAutomation[i].beat); kvD (w, "Volume", t.volumeAutomation[i].volume); w.endObj (); }
		w.endArr ();
		w.key ("AutomationLanes"); w.beginArr ();
		for (int i = 0; i < t.lanes.size (); i++)
		{
			w.beginObj (); kvI (w, "Param", t.lanes[i].param); kvB (w, "Enabled", t.lanes[i].enabled);
			w.key ("Points"); w.beginArr ();
			for (int j = 0; j < t.lanes[i].points.size (); j++) { w.beginObj (true); kvD (w, "Beat", t.lanes[i].points[j].beat); kvD (w, "Value", t.lanes[i].points[j].value); w.endObj (); }
			w.endArr (); w.endObj ();
		}
		w.endArr ();
		if (!t.instrumentPlugin.id.empty ()) { w.key ("OnyxInstrument"); wrPlugin (w, t.instrumentPlugin); }
		if (t.inserts.size ()) { w.key ("OnyxInserts"); w.beginArr (); for (int i = 0; i < t.inserts.size (); i++) wrPlugin (w, t.inserts[i]); w.endArr (); }
		w.key ("Items"); w.beginArr ();
		for (int i = 0; i < t.items.size (); i++)
		{
			if (!t.items[i].module) continue;
			w.beginObj (); kvD (w, "SilenceBefore", t.items[i].silenceBefore); w.key ("Module"); wrModule (w, *t.items[i].module); w.endObj ();
		}
		w.endArr ();
		w.endObj ();
	}
	w.endArr ();
	wrUserStyles (w, "UserChordStyles", p.userChordStyles);
	wrUserStyles (w, "UserMelodicLines", p.userMelodicLines);
	wrUserStyles (w, "UserDrumStyles", p.userDrumStyles);
	if (p.arrangement)
	{
		w.key ("Arrangement"); w.beginObj ();
		kvI (w, "SlicesPerQuarter", p.arrangement->slicesPerQuarter); kvI (w, "ChordSlices", p.arrangement->chordSlices);
		w.key ("Chords"); w.beginArr ();
		for (int i = 0; i < p.arrangement->chords.size (); i++) { w.beginObj (true); kvI (w, "Root", p.arrangement->chords[i].root); kvI (w, "Quality", p.arrangement->chords[i].quality); w.endObj (); }
		w.endArr (); w.endObj ();
	}
	w.key ("Key"); w.beginObj (true);
	kvI (w, "TonicLetter", p.key.tonicLetter); kvI (w, "Accidental", p.key.accidental); kvI (w, "Mode", p.key.mode); kvI (w, "FullMode", p.key.fullMode);
	w.endObj ();
	kvI (w, "TimeSigNum", p.timeSigNum); kvI (w, "TimeSigDen", p.timeSigDen); kvD (w, "PickupBeats", p.pickupBeats);
	kvD (w, "TimeSigScale", p.timeSigScale); kvD (w, "MinBeats", p.minBeats); kvD (w, "SwingPercent", p.swingPercent);
	kvI (w, "HumanizePercent", p.humanizePercent); kvI (w, "AgogicPercent", p.agogicPercent);
	w.key ("Markers"); w.beginArr ();
	for (int i = 0; i < p.markers.size (); i++) { w.beginObj (true); kvD (w, "Beat", p.markers[i].beat); kvS (w, "Name", p.markers[i].name); w.endObj (); }
	w.endArr ();
	kvD (w, "OnyxReverbMix", p.reverbBusMix);
	w.endObj ();
	w.key ("Riffs"); w.beginArr ();
	for (int i = 0; i < p.riffs.size (); i++)
	{
		const Riff &r = p.riffs[i];
		w.beginObj (); kvS (w, "Id", r.id); kvS (w, "Name", r.name); wrNotes (w, "Notes", r.notes);
		kvI (w, "LengthSlices", r.lengthSlices); kvI (w, "SlicesPerQuarter", r.spq); w.endObj ();
	}
	w.endArr ();
	w.endObj ();
}

} // namespace kt
