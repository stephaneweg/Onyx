//
// compile.cpp -- a project flattened for playback (compile.h), TimelinePlayer.cs's constructor and
// PlaceRiffNotes: the same metric velocities, swing warp, seeded humanisation and agogic lengthening.
//
#include "compile.h"

namespace kt {

Vec<int> g_kitPrograms;			// the SoundFont's drum kits (bank 128) by kit index: set by the app

int CompiledSong::sliceAtSample (long long s) const
{
	int lo = 0, hi = sliceSample.size () - 1;
	if (hi < 0) return 0;
	if (s <= 0) return 0;
	if (s >= sliceSample[hi]) return hi;
	while (lo + 1 < hi) { int mid = (lo + hi) / 2; if (sliceSample[mid] <= s) lo = mid; else hi = mid; }
	return lo;
}
double CompiledSong::beatAtSample (long long s) const
{
	int sl = sliceAtSample (s);
	if (sl + 1 >= sliceSample.size ()) return sl / (double) CSPB;
	long long a = sliceSample[sl], b = sliceSample[sl + 1];
	double f = b > a ? (double) (s - a) / (double) (b - a) : 0;
	return (sl + f) / CSPB;
}
long long CompiledSong::sampleAtBeat (double beat) const
{
	if (sliceSample.size () == 0) return 0;
	double sl = beat * CSPB;
	if (sl <= 0) return 0;
	int i = (int) sl;
	if (i + 1 >= sliceSample.size ()) return sliceSample.back ();
	return sliceSample[i] + (long long) ((sl - i) * (double) (sliceSample[i + 1] - sliceSample[i]));
}

double trackGain (const Vec<VolumePoint> &a, double baseVol, double beat)
{
	if (a.size () == 0) return baseVol;
	if (beat <= a[0].beat) { if (a[0].beat <= 1e-9) return a[0].volume; double f = dmax (0, beat) / a[0].beat; return baseVol + (a[0].volume - baseVol) * f; }
	for (int i = 0; i + 1 < a.size (); i++)
		if (beat >= a[i].beat && beat <= a[i + 1].beat)
		{
			double span = a[i + 1].beat - a[i].beat, f = span > 1e-9 ? (beat - a[i].beat) / span : 0;
			return a[i].volume + (a[i + 1].volume - a[i].volume) * f;
		}
	return a.back ().volume;
}
double sampleCurve (const Vec<AutomationPoint> &pts, double def, double beat)
{
	if (pts.size () == 0) return def;
	if (beat <= pts[0].beat) { if (pts[0].beat <= 1e-9) return pts[0].value; double f = dmax (0, beat) / pts[0].beat; return def + (pts[0].value - def) * f; }
	for (int i = 0; i + 1 < pts.size (); i++)
		if (beat >= pts[i].beat && beat <= pts[i + 1].beat)
		{
			double span = pts[i + 1].beat - pts[i].beat, f = span > 1e-9 ? (beat - pts[i].beat) / span : 0;
			return pts[i].value + (pts[i + 1].value - pts[i].value) * f;
		}
	return pts.back ().value;
}

// ---- the placing ------------------------------------------------------------------------------------------
struct Placer
{
	const Project &p;
	int totalSlices, swingPoint, hv, ha;
	CTrack *tr;
	int seq;
	Placer (const Project &pp, int total) : p (pp), totalSlices (total), tr (0), seq (0)
	{
		double sw = p.swingPercent;
		swingPoint = (sw > 50.5 && sw < 90) ? iround (CSPB * sw / 100.0) : 0;
		hv = iclamp (p.humanizePercent, 0, 100);
		ha = iclamp (p.agogicPercent, 0, 100);
	}
	int metricVelocity (int s) const
	{
		int num = imax (1, p.timeSigNum), den = imax (1, p.timeSigDen);
		int spBeat = imax (1, iround (CSPB * 4.0 / den));
		int barSlices = imax (1, spBeat * num);
		int pickup = iround (p.pickupBeats * CSPB);
		int pos = imod (s - pickup, barSlices);
		int inBeat = pos % spBeat, beatInBar = pos / spBeat;
		if (inBeat == 0)
		{
			if (beatInBar == 0) return 118;
			if (num % 2 == 0 && beatInBar == num / 2) return 108;
			return 100;
		}
		if (inBeat * 2 == spBeat) return 90;
		if (spBeat % 3 == 0 && inBeat % (spBeat / 3) == 0) return 84;
		if (spBeat % 4 == 0 && inBeat % (spBeat / 4) == 0) return 84;
		return 78;
	}
	int swingSlice (int s) const
	{
		if (swingPoint <= 0 || s <= 0) return s;
		int beat = s / CSPB, within = s - beat * CSPB, half = CSPB / 2;
		double w = within <= half ? within * (swingPoint / (double) half) : swingPoint + (within - half) * ((CSPB - swingPoint) / (double) half);
		return beat * CSPB + iround (w);
	}
	void place (const Riff &riff, double startBeat)
	{
		int spq = riff.spq > 0 ? riff.spq : 4;
		double scale = (double) CSPB / spq;
		int off = iround (startBeat * CSPB);
		for (int i = 0; i < riff.notes.size (); i++)
		{
			const RiffNote &n = riff.notes[i];
			if (n.note < 0 || n.note >= 96) continue;
			int straight = off + iround (n.start * scale);
			int vel = metricVelocity (straight);
			bool isDown = vel == 118, isStrong = vel >= 108;
			int timingShift = 0, endExt = 0;
			if (hv > 0)
			{
				NetRandom rng ((int) (unsigned) ((unsigned) n.note * 7919u + (unsigned) n.start * 131u + (unsigned) off + 17u));
				int magV = hv * 8 / 100, magT = hv * 2 / 100;
				if (magV > 0) vel = iclamp (vel + rng.next (-magV, magV + 1), 1, 127);
				if (magT > 0 && !isDown) timingShift = rng.next (-magT, magT + 1);
			}
			if (ha > 0 && isStrong) endExt = iround (CSPB * (isDown ? 0.10 : 0.05) * ha / 100.0);
			int s = swingSlice (straight + timingShift);
			int e = swingSlice (off + iround (n.end () * scale)) + endExt;
			if (e <= s) e = s + 1;
			if (s < 0) s = 0;
			if (s >= totalSlices) continue;
			if (e > totalSlices) e = totalSlices;
			CEvent on; on.slice = s; on.kind = EV_ON; on.note = (unsigned char) (n.note + 12); on.vel = (unsigned char) vel; on.pad = 0;
			on.glideFrom = 0; on.glideSec = 0;
			if (n.glideDur > 0 && n.glideFrom != n.note)
			{
				on.glideFrom = (float) (n.glideFrom + 12);
				on.glideSec = (float) (n.glideDur * (1.0 / spq) * 60.0 / p.bpmAt (straight / (double) CSPB));
			}
			CEvent off_; off_.slice = e; off_.kind = EV_OFF; off_.note = on.note; off_.vel = 0; off_.pad = 0; off_.glideFrom = 0; off_.glideSec = 0;
			tr->events.push (on); tr->events.push (off_);
		}
	}
};

static void finishTrack (CTrack &t)
{
	// by slice, the note-offs first (a re-attacked note sounds again), insertion order otherwise
	t.events.sort ([] (const CEvent &a, const CEvent &b) { return a.slice != b.slice ? a.slice < b.slice : a.kind < b.kind; });
}

static void setupSound (CTrack &ct, const Track &t, int index)
{
	ct.srcIndex = index;
	ct.drum = t.type == TRACK_DRUM || t.instrument >= 128;
	ct.channel = ct.drum ? 9 : 0;
	ct.bank = ct.drum ? 128 : 0;
	if (ct.drum) ct.program = (t.drumKit >= 0 && t.drumKit < g_kitPrograms.size ()) ? g_kitPrograms[t.drumKit] : 0;
	else ct.program = iclamp (t.instrument, 0, 127);
	ct.silent = t.type == TRACK_CHORD;
	ct.pluginId = t.instrumentPlugin.id;
	ct.volume = t.volumeAutomation;
	for (int i = 0; i < t.lanes.size (); i++)
		if (t.lanes[i].enabled && t.lanes[i].points.size () > 0 && t.lanes[i].param >= 0 && t.lanes[i].param < 9)
			ct.lane[t.lanes[i].param] = t.lanes[i].points;
}

static void buildTempo (CompiledSong &cs, const Project &p, int sampleRate)
{
	cs.sampleRate = sampleRate;
	cs.sliceSample.resize (cs.totalSlices + 1);
	long long acc = 0;
	for (int s = 0; s < cs.totalSlices; s++)
	{
		cs.sliceSample[s] = acc;
		double v = (60.0 / p.bpmAt (s / (double) CSPB)) / CSPB * sampleRate;
		acc += (long long) iround (v);
	}
	cs.sliceSample[cs.totalSlices] = acc;
}

CompiledSong *compileSong (const Project &p, int sampleRate, int onlyTrack)
{
	CompiledSong *cs = new CompiledSong;
	double totalBeats = 0;
	for (int i = 0; i < p.tracks.size (); i++) totalBeats = dmax (totalBeats, p.trackEnd (p.tracks[i]));
	cs->totalSlices = imax (1, iceil (totalBeats * CSPB) + 1);
	buildTempo (*cs, p, sampleRate);
	g_ternary = p.timeSigDen == 8;
	Placer pl (p, cs->totalSlices);
	for (int ti = 0; ti < p.tracks.size (); ti++)
	{
		if (onlyTrack >= 0 && ti != onlyTrack) continue;
		const Track &t = p.tracks[ti];
		CTrack &ct = cs->tracks.add ();
		setupSound (ct, t, ti);
		if (ct.silent) continue;			// the chord track only tells the harmony
		pl.tr = &ct;
		int carry[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
		double cursor = 0;
		for (int i = 0; i < t.items.size (); i++)
		{
			const Item &it = t.items[i];
			cursor += it.silenceBefore;
			if (it.module)
			{
				Riff cell;
				Riff r = renderModule (*it.module, p, cursor, carry, &cell);
				pl.place (r, cursor);
				if (cell.notes.size ()) pl.place (cell, cursor);
			}
			cursor += p.itemLength (it);
		}
		finishTrack (ct);
	}
	return cs;
}

CompiledSong *compileModulePreview (const Project &p, int track, int item, int sampleRate)
{
	if (track < 0 || track >= p.tracks.size () || item < 0 || item >= p.tracks[track].items.size ()) return 0;
	const Track &t = p.tracks[track];
	const Item &it = t.items[item];
	if (!it.module) return 0;
	double start = p.itemStart (t, item), len = p.itemLength (it);
	CompiledSong *cs = new CompiledSong;
	cs->totalSlices = imax (1, iceil (len * CSPB));
	cs->sampleRate = sampleRate;
	cs->sliceSample.resize (cs->totalSlices + 1);
	long long acc = 0;
	for (int s = 0; s <= cs->totalSlices; s++) { cs->sliceSample[s] = acc; acc += (long long) iround ((60.0 / p.bpmAt (start + s / (double) CSPB)) / CSPB * sampleRate); }
	g_ternary = p.timeSigDen == 8;
	CTrack &ct = cs->tracks.add ();
	setupSound (ct, t, track);
	ct.silent = false;
	if (t.type == TRACK_CHORD) { ct.program = 0; ct.drum = false; ct.channel = 0; ct.bank = 0; }	// a chord heard on a piano
	ct.volume.clear ();
	for (int k = 0; k < 9; k++) ct.lane[k].clear ();
	Placer pl (p, cs->totalSlices);
	pl.tr = &ct;
	int carry[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	Riff cell;
	Riff r = renderModule (*it.module, p, start, carry, &cell);
	// placed at 0 (a preview starts at its beginning), the metric accents of its real position kept
	int shift = iround (start * CSPB);
	int n0 = ct.events.size ();
	pl.totalSlices = cs->totalSlices + shift;
	pl.place (r, start);
	if (cell.notes.size ()) pl.place (cell, start);
	for (int i = n0; i < ct.events.size (); i++) ct.events[i].slice = imin (cs->totalSlices, imax (0, ct.events[i].slice - shift));
	finishTrack (ct);
	return cs;
}

CompiledSong *compileRiffPreview (const Project &p, const Riff &r, int program, bool drum, int sampleRate)
{
	CompiledSong *cs = new CompiledSong;
	double len = r.spq > 0 ? (double) r.lengthSlices / r.spq : 4;
	cs->totalSlices = imax (1, iceil (len * CSPB));
	cs->sampleRate = sampleRate;
	cs->sliceSample.resize (cs->totalSlices + 1);
	long long acc = 0; double bpm = p.mainBpm ();
	for (int s = 0; s <= cs->totalSlices; s++) { cs->sliceSample[s] = acc; acc += (long long) iround ((60.0 / bpm) / CSPB * sampleRate); }
	CTrack &ct = cs->tracks.add ();
	ct.srcIndex = -1; ct.drum = drum; ct.channel = drum ? 9 : 0; ct.bank = drum ? 128 : 0; ct.program = program; ct.silent = false;
	Placer pl (p, cs->totalSlices);
	pl.tr = &ct;
	pl.place (r, 0);
	finishTrack (ct);
	return cs;
}

} // namespace kt
