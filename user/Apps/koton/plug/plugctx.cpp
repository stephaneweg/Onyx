//
// plug/plugctx.cpp -- a generator plugin's request and reply (plugctx.h).
//
#include "plugctx.h"
#include "../engine/theory.h"
#include "../../../json.hpp"

namespace kt {

int basicQuality (int q)
{
	// Koton's KotonChordResolver.MapQuality: the tensions fall back on their base
	static const unsigned char MAP[QUALITY_COUNT] = {
		0, 1, 2, 3, 4, 5, 8, 9, 7, 12, 11,		// Major Minor dim aug sus2 sus4 Maj7 Min7 7 m7b5 dim7
		0, 1, 0, 1, 7, 8, 9, 7, 7, 7, 7, 8,		// 6 m6 add9 m(add9) 9 Maj9 m9 7b9 7#9 11 13 Maj7#11
		5, 4, 5, 4, 5, 4, 5, 4, 5, 4, 5,		// 7sus4 7sus2 9sus4 9sus2 6sus4 6sus2 Maj7sus4 Maj7sus2 Maj9sus4 Maj9sus2 add9sus4
		3 };						// 7#5
	return q >= 0 && q < QUALITY_COUNT ? MAP[q] : 0;
}

static unsigned hashStr (const char *s)
{
	unsigned h = 2166136261u;
	for (; s && *s; s++) { h ^= (unsigned char) *s; h *= 16777619u; }
	return h ? h : 1;
}

void genContext (const GeneratorModule &m, const Project &p, double startBeat, const char *stateJson, json::Writer &w)
{
	double len = m.durationBeats > 0.25 ? m.durationBeats : 0.25;
	w.beginObj ();
	w.key ("from"); w.num (0); w.key ("to"); w.num (len); w.key ("length"); w.num (len);
	w.key ("blockStart"); w.num (startBeat);
	w.key ("bpm"); w.num (p.bpmAt (startBeat));
	w.key ("pickup"); w.num (p.pickupBeats);
	w.key ("key"); w.beginObj ();
	w.key ("tonic"); w.num (tonicPc (p.key));
	w.key ("mode"); w.num (p.key.mode ? 1 : 0);
	const int *sc = modeScale (effectiveMode (p.key));
	w.key ("scale"); w.beginArr (true); for (int i = 0; i < 7; i++) w.num (sc[i]); w.endArr ();
	char name[64]; keyName (p.key, name, sizeof name);
	w.key ("name"); w.str (name);
	w.endObj ();
	w.key ("meter"); w.beginArr (true); w.num (p.timeSigNum > 0 ? p.timeSigNum : 4); w.num (p.timeSigDen > 0 ? p.timeSigDen : 4); w.endArr ();
	w.key ("seed"); w.u64 (hashStr (m.id.c ()));
	w.key ("chords"); w.beginArr ();
	Vec<ChordSeg> segs = segments (p, startBeat, len);
	for (int i = 0; i < segs.size (); i++)
	{
		const ChordSeg &g = segs[i];
		int notes[16], root = imod (g.root, 12);
		int n = chordNotes (root, 4, g.quality, 0, false, notes), rootMidi = root + 12 * 5;
		int inv[16]; int ni = chordNotes (root, 4, g.quality, g.inversion, false, inv);
		char label[32]; chordLabel (root, g.quality, p.key, label, sizeof label);
		w.beginObj (true);
		w.key ("start"); w.num (g.start - startBeat, 5); w.key ("len"); w.num (g.len, 5);
		w.key ("root"); w.num (root); w.key ("quality"); w.num (g.quality);
		w.key ("iv"); w.beginArr (true); for (int k = 0; k < n; k++) w.num (notes[k] - rootMidi); w.endArr ();
		w.key ("basic"); w.num (basicQuality (g.quality));
		w.key ("bass"); w.num (ni > 0 ? imod (inv[0], 12) : root);
		w.key ("name"); w.str (label);
		w.endObj ();
	}
	w.endArr ();
	w.key ("state");
	json::Doc st;
	if (stateJson && stateJson[0] && st.parse (stateJson, strlen (stateJson), json::TOLERANT) && st.root ().isObj ()) w.value (st.root ());
	else { w.beginObj (); w.endObj (); }
	w.endObj ();
}

bool genReply (const char *text, unsigned long len, Riff &out)
{
	json::Doc d;
	if (!d.parse (text, len, json::TOLERANT) || !d.root ()["notes"].isArr ()) return false;
	out.spq = SPQ;
	int end = out.lengthSlices > 0 ? out.lengthSlices : 1 << 30;
	for (const json::Value *e = d.root ()["notes"].first (); e; e = e->next)
	{
		double s, l; int midi;
		if (e->isArr ()) { s = (*e)[0].asDouble (-1); l = (*e)[1].asDouble (0); midi = (*e)[2].asInt (-1); }
		else { s = (*e)["start"].asDouble (-1); l = (*e)["len"].asDouble (0); midi = (*e)["note"].asInt (-1); }
		int note = midi - 12;				// (Koton's notes: 0 = C0 = MIDI 12)
		if (note < 0 || note >= 96 || s < 0 || l <= 0) continue;
		int st = iround (s * SPQ), ln = iround (l * SPQ);
		if (ln < 1) ln = 1;
		if (st >= end) continue;
		if (st + ln > end) ln = end - st;
		out.notes.push (RiffNote (note, st, ln));
	}
	out.notes.sort ([] (const RiffNote &a, const RiffNote &b) { return a.start != b.start ? a.start < b.start : a.note < b.note; });
	return true;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void b64encode (const char *data, int len, Str &out)
{
	int n = (len + 2) / 3 * 4;
	char *t = new char[n + 1];
	int o = 0;
	for (int i = 0; i < len; i += 3)
	{
		unsigned v = (unsigned char) data[i] << 16;
		if (i + 1 < len) v |= (unsigned char) data[i + 1] << 8;
		if (i + 2 < len) v |= (unsigned char) data[i + 2];
		t[o++] = B64[(v >> 18) & 63]; t[o++] = B64[(v >> 12) & 63];
		t[o++] = i + 1 < len ? B64[(v >> 6) & 63] : '=';
		t[o++] = i + 2 < len ? B64[v & 63] : '=';
	}
	t[o] = 0;
	out = t;
	delete [] t;
}

bool b64decode (const char *text, Str &out)
{
	int len = text ? (int) strlen (text) : 0;
	char *t = new char[len / 4 * 3 + 4];
	int o = 0, bits = 0; unsigned acc = 0;
	bool ok = true;
	for (int i = 0; i < len; i++)
	{
		char c = text[i];
		int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
			: c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
		if (c == '=' ) break;
		if (v < 0) { if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue; ok = false; break; }
		acc = (acc << 6) | (unsigned) v; bits += 6;
		if (bits >= 8) { bits -= 8; t[o++] = (char) ((acc >> bits) & 0xFF); }
	}
	t[o] = 0;
	if (ok) out.set (t, o);
	delete [] t;
	return ok;
}

void moduleStateJson (const GeneratorModule &m, Str &json)
{
	json = "";
	const char *s = m.state.c ();
	while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
	if (*s == '{') { json = s; return; }			// (already JSON: an Onyx-made module)
	Str raw;
	if (*s && b64decode (s, raw)) json = raw;
}

void setModuleStateJson (GeneratorModule &m, const char *json)
{
	b64encode (json ? json : "", json ? (int) strlen (json) : 0, m.state);
}

} // namespace kt
