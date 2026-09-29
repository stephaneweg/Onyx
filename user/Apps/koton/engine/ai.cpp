//
// ai.cpp -- Koton's AI composition (see ai.h), the prompt side: the name tables and the tolerant
// translations (AiTranslate), the prompts (AiArrangementPrompt, AiArrangement.Build*Prompt,
// AiPolyModules' fragments, AiPolyrhythmPrompt) with the piece / theme contexts they carry, the
// request the /bin/llm helper reads and the reading of what it answers.
//
// The French prompt texts are Koton's, byte for byte: they were tuned against the models (a word
// changed changes the replies). Only the newline differs (Koton's AppendLine writes CR LF on Windows).
//
#include "ai.h"
#include "gen.h"
#include "../../../json.hpp"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace kt {

const char *const g_aiKindNames[AI_KINDS] = {
	"Compose a piece", "Develop the theme", "Add an instrument", "Add drums", "Drum groove", "Riff", "Polyrhythmic piece" };

// ---- providers (AiProviders.cs) ------------------------------------------------------------------------------
const char *const g_aiProviders[AI_PROVIDER_COUNT] = { "gemini", "groq", "mistral", "claude", "deepseek", "grok", "openai-compatible" };
static const char *const s_providerLabels[AI_PROVIDER_COUNT] = { "Gemini", "Groq", "Mistral", "Claude", "DeepSeek", "Grok", "OpenAI-compatible" };
static const char *const s_providerModels[AI_PROVIDER_COUNT] = {
	"gemini-2.0-flash", "llama-3.3-70b-versatile", "mistral-small-latest", "claude-opus-4-8", "deepseek-chat", "grok-4", "" };
static int providerIndex (const char *p)
{
	for (int i = 0; i < AI_PROVIDER_COUNT; i++) if (json::seqi (p, g_aiProviders[i])) return i;
	return -1;
}
const char *aiProviderLabel (const char *p) { int i = providerIndex (p); return i >= 0 ? s_providerLabels[i] : "AI"; }
const char *aiProviderDefaultModel (const char *p) { int i = providerIndex (p); return i >= 0 ? s_providerModels[i] : ""; }

namespace aidet {

// ---- Koton's French tables ----------------------------------------------------------------------------------------
const char *const kQualityFr[35] = {
	"Majeur", "Mineur", "Diminué", "Augmenté", "Sus2", "Sus4", "Maj7", "Min7", "7 (dom)", "m7♭5", "dim7",
	"6", "m6", "add9", "m(add9)", "9 (dom)", "Maj9", "m9", "7♭9", "7♯9", "11 (dom)", "13 (dom)", "Maj7♯11",
	"7sus4", "7sus2", "9sus4", "9sus2",
	"6sus4", "6sus2", "Maj7sus4", "Maj7sus2", "Maj9sus4", "Maj9sus2", "add9sus4",
	"7♯5" };
const char *const kStyleFr[29] = {
	"Accords plaqués (tenu)", "Accords plaqués (noires)", "Accords plaqués (croches)", "Arpège montant",
	"Arpège montant-descendant", "Alberti (Do-Sol-Mi-Sol)", "Jazz comping (Charleston)", "Rock (croches)",
	"Pop (basse + accord)", "Blues shuffle (triolets)", "Arpège descendant", "Arpège (croches)",
	"Valse (basse-accord-accord)", "Reggae skank (contretemps)", "Marche (basse-accord)", "Tango (staccato)",
	"Bossa nova / Latin (syncopé)", "Funk (stabs 16e)", "Habanera / Tango (basse)", "Ballade (arpège tenu)",
	"Country (basse alternée)", "Slow rock (triolets 12-8)", "Arpège : 2 croches + noire",
	"Arpège : 3 croches + noire pointée", "Arpège : 4 croches + blanche", "Arpège : triolet + noire",
	"Arpège : 4 croches + noire", "Harpe (arpège roulé)", "Personnalisé…" };
const char *const kContourFr[9] = { "Vague (arcs)", "Montante", "Descendante", "Statique (pivot)", "Zigzag", "Aléatoire", "Thue-Morse", "L-système", "Fractale (1/f)" };
const char *const kAnchorFr[6] = { "Défaut (au plus proche)", "Fondamentale", "Tierce", "Quinte", "Septième", "Neuvième" };

// ---- the text builder -----------------------------------------------------------------------------------------------
void Text::add (const char *t, int len)
{
	if (!t) return;
	if (len < 0) len = (int) strlen (t);
	if (n + len + 1 > cap)
	{
		int nc = cap ? cap * 2 : 1024;
		while (nc < n + len + 1) nc *= 2;
		char *ns = new char[nc];
		if (n) memcpy (ns, s, n);
		delete [] s; s = ns; cap = nc;
	}
	memcpy (s + n, t, len); n += len; s[n] = 0;
}
void Text::addInt (long long v) { char b[24]; snprintf (b, sizeof b, "%lld", v); add (b); }
void Text::addNum (double v) { char b[32]; num2 (v, b, sizeof b); add (b); }
void Text::fmt (const char *f, ...)
{
	char b[512];
	va_list ap; va_start (ap, f);
	int k = vsnprintf (b, sizeof b, f, ap);
	va_end (ap);
	if (k < (int) sizeof b) { add (b, k < 0 ? 0 : k); return; }
	char *big = new char[k + 1];
	va_start (ap, f); vsnprintf (big, k + 1, f, ap); va_end (ap);
	add (big, k);
	delete [] big;
}

void num2 (double v, char *buf, int cap)
{
	bool neg = v < 0;
	double a = neg ? -v : v;
	long long r = (long long) (a * 100 + 0.5);		// .NET's "0.##": 2 decimals, midpoints away from zero
	long long ip = r / 100; int fp = (int) (r % 100);
	if (r == 0) neg = false;
	if (fp == 0) snprintf (buf, cap, "%s%lld", neg ? "-" : "", ip);
	else if (fp % 10 == 0) snprintf (buf, cap, "%s%lld.%d", neg ? "-" : "", ip, fp / 10);
	else snprintf (buf, cap, "%s%lld.%02d", neg ? "-" : "", ip, fp);
}

// KeySig.Derive (k, 0).Name
void frenchKeyName (const Key &k, Str &out)
{
	static const int letterFifths[7] = { 0, 2, 4, -1, 1, 3, 5 };
	static const char *const majNames[15] = { "Do♭", "Sol♭", "Ré♭", "La♭", "Mi♭", "Si♭", "Fa", "Do", "Sol", "Ré", "La", "Mi", "Si", "Fa♯", "Do♯" };
	static const char *const minNames[15] = { "La♭", "Mi♭", "Si♭", "Fa", "Do", "Sol", "Ré", "La", "Mi", "Si", "Fa♯", "Do♯", "Sol♯", "Ré♯", "La♯" };
	int letter = iclamp (k.tonicLetter, 0, 6), acc = iclamp (k.accidental, -1, 1);
	int fifths = letterFifths[letter] + 7 * acc - (k.mode == 1 ? 3 : 0);
	while (fifths > 7) fifths -= 12;
	while (fifths < -7) fifths += 12;
	int idx = iclamp (fifths + 7, 0, 14);
	Text t; t.add (k.mode == 1 ? minNames[idx] : majNames[idx]); t.add (k.mode == 1 ? " mineur" : " majeur");
	t.take (out);
}

// ---- AiTranslate: tolerant name matching ----------------------------------------------------------------------------
// Koton's Norm: lower case, letters and digits only (accented letters are letters, ♭ ♯ … are not). UTF-8
// in, UTF-8 out; *cps = the length in characters.
static void norm (const char *s, char *out, int cap, int *cps = 0)
{
	int o = 0, n = 0;
	const unsigned char *p = (const unsigned char *) (s ? s : "");
	while (*p && o + 5 < cap)
	{
		unsigned cp; int k;
		if (*p < 0x80) { cp = *p; k = 1; }
		else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 31u) << 6) | (p[1] & 63u); k = 2; }
		else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { cp = ((p[0] & 15u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u); k = 3; }
		else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = ((p[0] & 7u) << 18) | ((p[1] & 63u) << 12) | ((p[2] & 63u) << 6) | (p[3] & 63u); k = 4; }
		else { p++; continue; }
		p += k;
		bool keep;
		if (cp < 0x80)
		{
			if (cp >= 'A' && cp <= 'Z') cp += 32;
			keep = (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9');
		}
		else if (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7)
		{
			if (cp <= 0xDE) cp += 0x20;				// À..Þ -> à..þ
			else if (cp >= 0x100 && cp <= 0x17F && !(cp & 1)) cp += 1;	// Latin Extended-A pairs
			keep = true;
		}
		else keep = (cp >= 0x370 && cp < 0x2000) || cp >= 0x3040;	// other scripts; punctuation / symbols (♭ ♯ …) dropped
		if (!keep) continue;
		if (cp < 0x80) out[o++] = (char) cp;
		else if (cp < 0x800) { out[o++] = (char) (0xC0 | (cp >> 6)); out[o++] = (char) (0x80 | (cp & 63)); }
		else if (cp < 0x10000) { out[o++] = (char) (0xE0 | (cp >> 12)); out[o++] = (char) (0x80 | ((cp >> 6) & 63)); out[o++] = (char) (0x80 | (cp & 63)); }
		else { out[o++] = (char) (0xF0 | (cp >> 18)); out[o++] = (char) (0x80 | ((cp >> 12) & 63)); out[o++] = (char) (0x80 | ((cp >> 6) & 63)); out[o++] = (char) (0x80 | (cp & 63)); }
		n++;
	}
	out[o] = 0;
	if (cps) *cps = n;
}

static bool blank (const char *s)
{
	if (!s) return true;
	for (; *s; s++) if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') return false;
	return true;
}

// MatchIndex: exact, then a leading word (3+ characters), then "contains" (4+)
static int matchIndex (const char *name, const char *const *table, int n, int fallback)
{
	if (blank (name) || !table) return fallback;
	char nn[128], t[128]; int len;
	norm (name, nn, sizeof nn, &len);
	for (int i = 0; i < n; i++) { norm (table[i], t, sizeof t); if (!strcmp (t, nn)) return i; }
	for (int i = 0; i < n; i++) { norm (table[i], t, sizeof t); if (len >= 3 && !strncmp (t, nn, strlen (nn))) return i; }
	for (int i = 0; i < n; i++) { norm (table[i], t, sizeof t); if (len >= 4 && strstr (t, nn)) return i; }
	return fallback;
}

// int.TryParse (name.Trim ())
static bool parseIntStrict (const char *s, int *out)
{
	if (!s) return false;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
	bool neg = false;
	if (*s == '-' || *s == '+') { neg = *s == '-'; s++; }
	if (*s < '0' || *s > '9') return false;
	long long v = 0;
	while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); if (v > 2147483647LL) return false; s++; }
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
	if (*s) return false;
	*out = (int) (neg ? -v : v);
	return true;
}

// IndexOrMatch, with Onyx's English names as a second chance (the prompt lists the French ones)
static int indexOrMatch (const char *name, const char *const *fr, const char *const *en, int n)
{
	int idx;
	if (parseIntStrict (name, &idx)) return iclamp (idx, 0, n - 1);
	int i = matchIndex (name, fr, n, -1);
	if (i < 0 && en) i = matchIndex (name, en, n, -1);
	return i < 0 ? 0 : i;
}
int contourIndex (const char *name) { return indexOrMatch (name, kContourFr, g_contourNames, 9); }
int anchorIndex (const char *name) { return indexOrMatch (name, kAnchorFr, g_anchorNames, 6); }
int styleIndex (const char *name) { return indexOrMatch (name, kStyleFr, g_styleNames, 29); }

int qualityIndex (const char *name)
{
	if (blank (name)) return 0;
	// Onyx: an exact name first, ♭ / b and ♯ / # told apart (Koton's Norm drops both signs, so the "7♯9" its
	// own prompt lists was read as 7♭9, "Maj7♯11" could not be told from others...)
	int q0 = kt::qualityIndex (name);
	if (q0 >= 0) return q0;
	int exact = matchIndex (name, kQualityFr, 35, -1);
	if (exact >= 0) return exact;
	char n[128]; norm (name, n, sizeof n);
	static const struct { const char *a; int q; } alias[] = {
		{ "maj", 0 }, { "major", 0 }, { "min", 1 }, { "minor", 1 }, { "m", 1 }, { "dim", 2 }, { "aug", 3 },
		{ "maj7", 6 }, { "major7", 6 }, { "m7", 7 }, { "min7", 7 }, { "minor7", 7 }, { "7", 8 }, { "dom7", 8 },
		{ "dominant7", 8 }, { "m7b5", 9 }, { "halfdim", 9 }, { "dim7", 10 }, { "6", 11 }, { "m6", 12 }, { "add9", 13 },
		{ "9", 15 }, { "dom9", 15 }, { "maj9", 16 }, { "m9", 17 }, { "min9", 17 } };
	for (unsigned i = 0; i < sizeof alias / sizeof alias[0]; i++) if (!strcmp (n, alias[i].a)) return alias[i].q;
	int q = kt::qualityIndex (name);		// Onyx: the English names ("Diminished", "7#9"...) as a last chance
	return q >= 0 ? q : 0;
}

// AiTranslate.ParseTonic, with Koton's French-name slip fixed: "Do" read as D (its first letter) and
// "Ré" not recognised; the French syllables are tried first here
static void parseTonic (const char *t, int *letter, int *acc)
{
	*letter = 0; *acc = 0;
	if (blank (t)) return;
	while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n') t++;
	char low[64]; norm (t, low, sizeof low);
	static const char *const fr[7] = { "do", "re", "mi", "fa", "sol", "la", "si" };
	int found = -1;
	for (int i = 0; i < 7 && found < 0; i++) if (!strncmp (low, fr[i], strlen (fr[i]))) found = i;
	if (found < 0 && !strncmp (low, "ré", strlen ("ré"))) found = 1;
	if (found < 0)
	{
		char c0 = t[0]; if (c0 >= 'a' && c0 <= 'z') c0 -= 32;
		const char *L = strchr ("CDEFGAB", c0);
		if (c0 && L) found = (int) (L - "CDEFGAB");
	}
	if (found >= 0) *letter = found;
	// accidentals: # / dièse / sharp; b (after the first letter) / bémol / flat
	char lw[96]; int k = 0;
	for (const char *p = t; *p && k + 1 < (int) sizeof lw; p++) lw[k++] = (*p >= 'A' && *p <= 'Z') ? (char) (*p + 32) : *p;
	lw[k] = 0;
	if (strchr (t, '#') || strstr (t, "♯") || strstr (lw, "dièse") || strstr (lw, "diese") || strstr (lw, "sharp")) *acc = 1;
	else if ((strlen (t) > 1 && strchr (t + 1, 'b')) || strstr (t, "♭") || strstr (lw, "bémol") || strstr (lw, "bemol") || strstr (lw, "flat")) *acc = -1;
}

Key parseKey (const char *tonic, const char *mode)
{
	Key k;
	if (!tonic && !mode) return k;
	if (mode)
	{
		while (*mode == ' ' || *mode == '\t') mode++;
		k.mode = (json::seqi (mode, "min") || ((mode[0] | 32) == 'm' && (mode[1] | 32) == 'i' && (mode[2] | 32) == 'n')) ? 1 : 0;
	}
	parseTonic (tonic, &k.tonicLetter, &k.accidental);
	return k;
}

int rootPc (const Key &k, int degree1based)
{
	const int *scale = modeScale (effectiveMode (k));
	int d = imod (degree1based - 1, 7);
	return imod (tonicPc (k) + scale[d], 12);
}

// ---- reply cleaning (StripFences + ExtractFirstObject) ------------------------------------------------------------------
void cleanJson (const char *s, Str &out)
{
	if (blank (s)) { out = "{}"; return; }
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
	const char *e = s + strlen (s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
	if (e - s >= 3 && !strncmp (s, "```", 3))
	{
		const char *nl = (const char *) memchr (s, '\n', e - s);
		if (nl) s = nl + 1;
		if (e - s >= 3) for (const char *q = e - 3; q >= s; q--) if (!strncmp (q, "```", 3)) { e = q; break; }
	}
	const char *start = (const char *) memchr (s, '{', e - s);
	if (!start) { out.set (s, (int) (e - s)); return; }
	int depth = 0; bool inStr = false, esc = false;
	for (const char *q = start; q < e; q++)
	{
		char c = *q;
		if (inStr)
		{
			if (esc) esc = false;
			else if (c == '\\') esc = true;
			else if (c == '"') inStr = false;
			continue;
		}
		if (c == '"') inStr = true;
		else if (c == '{') depth++;
		else if (c == '}' && --depth == 0) { out.set (start, (int) (q - start + 1)); return; }
	}
	const char *b = 0;						// unbalanced: best effort (may be truncated)
	for (const char *q = e - 1; q > start; q--) if (*q == '}') { b = q; break; }
	if (b) out.set (start, (int) (b - start + 1)); else out.set (start, (int) (e - start));
}

// ---- the chord track as the prompts see it --------------------------------------------------------------------------------
static int firstChordTrack (const Project &p) { return p.chordTrackIndex (); }

Vec<AiChordRef> chordsUnder (const Project &p, double startBeat, double lenBeats, int barTemps)
{
	Vec<AiChordRef> res;
	int ci = firstChordTrack (p);
	if (ci < 0) return res;
	const Track &ct = p.tracks[ci];
	double c = 0;
	for (int i = 0; i < ct.items.size (); i++)
	{
		const Item &it = ct.items[i];
		double s = c + it.silenceBefore, len = p.itemLength (it);
		c = s + len;
		if (s >= startBeat + lenBeats - 1e-6 || s + len <= startBeat + 1e-6) continue;
		const Module *m = it.module;
		if (!m) continue;
		if (m->kind == M_PATTERN)
		{
			const PatternModule *pg = (const PatternModule *) m;
			int deg = pg->degree >= 0 ? pg->degree : degreeOf (p.key, imod (pg->root, 12));
			AiChordRef r; r.measure = imax (1, iround ((s - startBeat) / imax (1, barTemps)) + 1);
			r.degree = imax (1, deg + 1); r.quality = pg->quality;
			res.push (r);
		}
		else if (m->kind == M_POLYCHORD)
		{
			const PolyChordModule *pc = (const PolyChordModule *) m;
			double chordStart = s;
			for (int j = 0; j < pc->chords.size (); j++)
			{
				const PolyChordItem &pci = pc->chords[j];
				if (chordStart >= startBeat + lenBeats - 1e-6) break;
				double chordLen = imax (1, pci.beats);
				if (chordStart + chordLen > startBeat + 1e-6)
				{
					int deg = pci.degree >= 0 ? pci.degree : degreeOf (p.key, imod (pci.root, 12));
					AiChordRef r; r.measure = imax (1, iround ((chordStart - startBeat) / imax (1, barTemps)) + 1);
					r.degree = imax (1, deg + 1); r.quality = pci.quality;
					res.push (r);
				}
				chordStart += chordLen;
			}
		}
	}
	return res;
}

static const char *qualityFr (int q) { return (q >= 0 && q < 35) ? kQualityFr[q] : ""; }	// TimelineHelper.Get

bool lastRiff (const Project &p, int *track, int *item)
{
	double best = -1; bool found = false;
	for (int t = 0; t < p.tracks.size (); t++)
	{
		const Track &tr = p.tracks[t];
		double c = 0;
		for (int i = 0; i < tr.items.size (); i++)
		{
			c += tr.items[i].silenceBefore;
			const Module *m = tr.items[i].module;
			if (m && m->kind == M_PLAYRIFF && tr.type != TRACK_CHORD)
			{
				const Riff *r = p.riffById (((const PlayRiffModule *) m)->riffId);
				if (r && r->notes.size () > 0 && c >= best) { best = c; *track = t; *item = i; found = true; }
			}
			c += p.itemLength (tr.items[i]);
		}
	}
	return found;
}

static const Riff *riffAt (const Project &p, int track, int item)
{
	if (track < 0 || track >= p.tracks.size () || item < 0 || item >= p.tracks[track].items.size ()) return 0;
	const Module *m = p.tracks[track].items[item].module;
	if (!m || m->kind != M_PLAYRIFF) return 0;
	return p.riffById (((const PlayRiffModule *) m)->riffId);
}

void riffTarget (const Project &p, const AiRequest &r, double *startBeat, double *lenBeats, int *measures, bool *exists)
{
	int barTemps = p.barBeats ();
	if (riffAt (p, r.track, r.item))
	{
		const Track &t = p.tracks[r.track];
		*startBeat = p.itemStart (t, r.item);
		*lenBeats = dmax (barTemps, p.itemLength (t.items[r.item]));
		*measures = imax (1, iround (*lenBeats / imax (1, barTemps)));
		*exists = true;
		return;
	}
	*measures = imax (1, r.measures);
	*lenBeats = *measures * barTemps;
	bool instr = r.track >= 0 && r.track < p.tracks.size () && p.tracks[r.track].type == TRACK_INSTRUMENT;
	*startBeat = instr ? p.trackEnd (p.tracks[r.track]) : 0;
	*exists = false;
}

} // namespace aidet

using namespace aidet;

// ---- the piece as text (AiArrangement.BuildFullPieceContext / BuildThemeContext) ---------------------------------------------
struct FlatNote { int pitch, start, len; };		// MIDI; slices at 24 a beat

static void flattenTrack (const Project &p, const Track &t, Vec<FlatNote> &out)
{
	const int spq = 24;
	int carry[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	double cur = 0;
	for (int i = 0; i < t.items.size (); i++)
	{
		const Item &it = t.items[i];
		cur += it.silenceBefore;
		const Module *m = it.module;
		if (m)
		{
			Riff cell;
			Riff r = renderModule (*m, p, cur, carry, &cell);
			if (t.type == TRACK_DRUM && (m->kind == M_DRUMKIT || m->kind == M_POLYDRUM))
			{
				// the lane grid read as GM keys, one slice a hit (FlattenLeaf's percussion path)
				int gspq = r.spq > 0 ? r.spq : 4;
				Vec<FlatNote> hits;
				for (int k = 0; k < r.notes.size (); k++)
				{
					FlatNote f; f.pitch = r.notes[k].note + 12; f.start = iround ((cur + (double) r.notes[k].start / gspq) * spq); f.len = 1;
					hits.push (f);
				}
				hits.sort ([] (const FlatNote &a, const FlatNote &b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
				out.append (hits);
			}
			else
			{
				int mspq = cell.spq > 0 ? cell.spq : 4;
				for (int k = 0; k < cell.notes.size (); k++)
				{
					FlatNote f; f.pitch = cell.notes[k].note + 12;
					f.start = iround ((cur + (double) cell.notes[k].start / mspq) * spq);
					f.len = imax (1, iround ((double) cell.notes[k].length / mspq * spq));
					out.push (f);
				}
				int rspq = r.spq > 0 ? r.spq : 4;
				for (int k = 0; k < r.notes.size (); k++)
				{
					FlatNote f; f.pitch = r.notes[k].note + 12;
					f.start = iround ((cur + (double) r.notes[k].start / rspq) * spq);
					f.len = imax (1, iround ((double) r.notes[k].length / rspq * spq));
					out.push (f);
				}
			}
		}
		cur += p.itemLength (it);
	}
}

static void addChordList (Text &sb, const Vec<AiChordRef> &chords)
{
	for (int i = 0; i < chords.size (); i++)
	{
		sb.add ("["); sb.addInt (chords[i].measure); sb.add (","); sb.addInt (chords[i].degree); sb.add (",");
		sb.add (qualityFr (chords[i].quality)); sb.add ("] ");
	}
}

static void fullPieceContext (const Project &p, Str &out, int *measuresOut)
{
	int barTemps = p.barBeats ();
	double maxEnd = 0;
	for (int t = 0; t < p.tracks.size (); t++) maxEnd = dmax (maxEnd, p.trackEnd (p.tracks[t]));
	int measures = imax (1, iceil (maxEnd / imax (1, barTemps) - 1e-6));
	*measuresOut = measures;
	Str kn; frenchKeyName (p.key, kn);
	double bpm = p.mainBpm () > 0 ? p.mainBpm () : 120;
	Text sb;
	sb.add ("MORCEAU ACTUEL — tonalité "); sb.add (kn); sb.add (p.key.mode == 1 ? " mineur" : " majeur");
	sb.fmt (", mesure %d/%d, ", p.timeSigNum, p.timeSigDen); sb.addNum (bpm);
	sb.fmt (" BPM, %d mesures (%d temps/mesure).\n", measures, barTemps);
	Vec<AiChordRef> chords = chordsUnder (p, 0, measures * barTemps, barTemps);
	if (chords.size () > 0)
	{
		sb.add ("Accords [mesure,degré,qualité] : ");
		addChordList (sb, chords);
		sb.add ("\n");
	}
	int ct = firstChordTrack (p);
	for (int t = 0; t < p.tracks.size (); t++)
	{
		const Track &tr = p.tracks[t];
		if (tr.items.size () == 0 || t == ct) continue;
		Vec<FlatNote> notes;
		flattenTrack (p, tr, notes);
		if (notes.size () == 0) continue;
		sb.add (tr.type == TRACK_DRUM ? "BATTERIE «" : "PISTE «"); sb.add (tr.name);
		sb.fmt ("» (GM %d, %d notes) [pitchMIDI@débutTemps xduréeTemps] : ", tr.instrument, notes.size ());
		for (int i = 0; i < notes.size (); i++)
		{
			sb.addInt (notes[i].pitch); sb.add ("@"); sb.addNum (notes[i].start / 24.0); sb.add ("x"); sb.addNum (notes[i].len / 24.0); sb.add (" ");
		}
		sb.add ("\n");
	}
	sb.take (out);
}

static void themeContext (const Project &p, int track, int item, const Riff &riff, Str &out)
{
	int barTemps = p.barBeats ();
	int spq = imax (1, riff.spq);
	double themeStart = p.itemStart (p.tracks[track], item);
	double themeLen = riff.lengthSlices / (double) spq;
	Str kn; frenchKeyName (p.key, kn);
	Text sb;
	sb.add ("THÈME à développer — tonalité "); sb.add (kn); sb.add (p.key.mode == 1 ? " mineur" : " majeur");
	sb.fmt (", mesure %d/%d, ", p.timeSigNum, p.timeSigDen); sb.addNum (themeLen / imax (1, barTemps)); sb.add (" mesures.\n");
	int ci = firstChordTrack (p);
	if (ci >= 0)
	{
		const Track &ct = p.tracks[ci];
		double cur = 0; Text chords; int nc = 0;
		for (int i = 0; i < ct.items.size (); i++)
		{
			const Item &it = ct.items[i];
			cur += it.silenceBefore;
			double len = p.itemLength (it);
			if (it.module && it.module->kind == M_PATTERN && cur + len > themeStart + 0.01 && cur < themeStart + themeLen - 0.01)
			{
				const PatternModule *pg = (const PatternModule *) it.module;
				int deg = pg->degree >= 0 ? pg->degree + 1 : degreeOf (p.key, imod (pg->root, 12)) + 1;
				int mRel = iround ((cur - themeStart) / imax (1, barTemps)) + 1;
				if (nc++) chords.add (", ");
				chords.fmt ("m%d=degré %d ", mRel, deg); chords.add (qualityFr (pg->quality));
			}
			cur += len;
		}
		if (nc) { sb.add ("Accords du thème : "); sb.add (chords.c ()); sb.add (".\n"); }
	}
	sb.add ("Notes du thème (pitchMIDI@débutEnTemps x duréeEnTemps) : ");
	for (int i = 0; i < riff.notes.size (); i++)
	{
		if (i) sb.add (" ");
		sb.addInt (riff.notes[i].note + 12); sb.add ("@"); sb.addNum (riff.notes[i].start / (double) spq);
		sb.add ("x"); sb.addNum (riff.notes[i].length / (double) spq);
	}
	sb.add (".\n");
	sb.take (out);
}

// (intention ?? "").Trim ()
static void trimmed (const char *s, Str &out)
{
	if (!s) { out = ""; return; }
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
	int n = (int) strlen (s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
	out.set (s, n);
}

// ---- AiPolyModules' prompt fragments ------------------------------------------------------------------------------------------
enum { MAX_SPAN_MEASURES = 4 };

static const char *const kPolyChordSchema =
	"  \"polyChords\": [ { \"fromMeasure\": int, \"measures\": int(1..4),\n"
	"      \"cycleBeats\": int, \"octave\": int, \"mode\": \"perTone|sweep\",\n"
	"      \"restart\": \"nearest|grave|aigu|tonic|tierce|quinte\", \"openVoicing\": bool,\n"
	"      \"layers\": [ { \"toneIndex\": int, \"contour\": int, \"hits\": int, \"steps\": int, \"rotation\": int, \"octave\": int, \"legato\": bool }, ... ] }, ... ]";
static const char *const kPolyDrumSchema =
	"  \"polyDrums\": [ { \"fromMeasure\": int, \"measures\": int(1..4), \"kit\": 0, \"cycleBeats\": int,\n"
	"      \"layers\": [ { \"lane\": int, \"hits\": int, \"steps\": int, \"rotation\": int, \"accentLane\": int }, ... ] }, ... ]";

static void cycleRules (Text &sb)
{
	sb.add ("- MODÈLE POLYRYTHMIQUE : tous les anneaux partagent le MÊME cycle ('cycleBeats' temps, typiquement 2 à 6). "
		"Chaque anneau découpe CE cycle en 'steps' parts égales et frappe sur les cases actives du motif euclidien E(hits,steps), décalé de 'rotation'. "
		"Il n'y a PAS de subdivision propre à un anneau : la polyrythmie vient de ce que les 'steps' DIFFÈRENT d'un anneau à l'autre (3 contre 5, 4 contre 7, 5 contre 8…).\n"
		"  hits ∈ [1, steps] ; steps ∈ [2, 32] ; rotation ∈ [0, steps−1]. Choisis des 'steps' premiers entre eux et évite deux anneaux au même 'steps' (ils sonneraient à l'unisson).\n"
		"- UN TABLEAU D'ENTRÉES, PAS UNE SEULE ROUE : chaque entrée de 'polyChords'/'polyDrums' est UN module posé sur la timeline, couvrant 'measures' mesures (1 à ");
	sb.addInt (MAX_SPAN_MEASURES);
	sb.add (" MAXIMUM, jamais plus) à partir de 'fromMeasure' (1-based). Enchaîne les entrées sans trou ni recouvrement pour couvrir le morceau : "
		"un morceau de 64 mesures = 16 entrées de 4 mesures.\n"
		"  COHÉRENCE PAR SECTION, VARIATION ENTRE SECTIONS — c'est la règle d'or du découpage :\n"
		"    • À L'INTÉRIEUR d'une même section ('sections'), toutes les entrées gardent la MÊME IDENTITÉ de trame : mêmes 'steps' sur chaque anneau, MÊME nombre d'anneaux, mêmes 'toneIndex'/'lane', même 'cycleBeats'. "
		"Un thème de 16 mesures = 4 entrées de 4 mesures qui se RESSEMBLENT ; on ne réinvente pas le motif au milieu du thème. Ce sont bien 4 entrées (jamais plus de ");
	sb.addInt (MAX_SPAN_MEASURES);
	sb.add (" mesures par module), pas une seule roue étalée.\n"
		"    • À l'intérieur d'une section, la SEULE variation permise est FINE et progressive : décaler d'un cran une 'rotation', ajouter/retirer UN 'hits' sur un anneau, faire disparaître un anneau au dernier bloc pour respirer. Aucun changement de 'steps', ni du nombre d'anneaux, ni du 'cycleBeats'.\n"
		"    • AU CHANGEMENT DE SECTION en revanche, change VRAIMENT : autres 'steps', autre nombre d'anneaux, autres 'toneIndex'/'lane', éventuellement autre 'cycleBeats' — trame qui se densifie vers un refrain, se creuse sur un pont, anneau qui disparaît puis revient déphasé au retour du thème. C'est là que l'auditeur doit entendre la coupure.\n"
		"  Deux sections consécutives à la trame identique = travail non fait ; deux entrées consécutives de la MÊME section à la trame radicalement différente = section incohérente.");
}

static const char *const kPolyChordRules =
	"- ACCORDS EN POLYRYTHME : la piste d'accords n'est PLUS jouée mesure par mesure — elle est jouée par des ROUES d'anneaux euclidiens ('polyChords'). "
	"Laisse donc 'articulation' VIDE ([]) : elle ne serait pas utilisée.\n"
	"  La PROGRESSION reste celle de 'chords' (un degré par mesure, comme d'habitude) : ne la réécris PAS dans 'polyChords', qui ne décrit QUE les anneaux et les réglages des roues. Chaque entrée reprend automatiquement les accords de SA plage de mesures.\n"
	"  COUVRE TOUT LE MORCEAU : la piste d'accords porte l'harmonie que les autres pistes lisent ; une plage sans entrée serait comblée en répétant l'entrée précédente (donc sans variation — à toi de l'écrire).\n"
	"  'cycleBeats' fixe la VITESSE des anneaux, indépendamment de la durée des accords : un changement d'accord ne fait que basculer sur le voicing suivant, la roue continue au même tempo.\n"
	"  'mode' = \"perTone\" (à privilégier) : chaque anneau joue UNE note fixe du voicing, désignée par 'toneIndex' — 0 = la plus grave, 1 = la suivante, 2 = celle d'après… ; ça s'ENROULE À L'OCTAVE dans les deux sens, donc le nombre d'anneaux n'est PAS limité au nombre de notes de l'accord : sur une triade, 3 = la fondamentale une octave au-dessus, 4 = la tierce au-dessus, 6 = la fondamentale deux octaves au-dessus, −1 = la quinte une octave en dessous. Un anneau grave lent + des anneaux aigus rapides = ostinato entrelacé.\n"
	"  'mode' = \"sweep\" : chaque anneau PARCOURT les notes de l'accord selon 'contour' (0 vague, 1 montante, 2 descendante, 3 statique, 4 zigzag, 5 aléatoire) ; 'restart' dit quelle note reprendre au changement d'accord.\n"
	"  'octave' du module = octave de base (4 = Do central) ; 'octave' d'un anneau = décalage en OCTAVES (−3..+3) ; 'legato' = la note tient jusqu'au coup suivant (nappes) ; 'openVoicing' = voicing écarté.\n"
	"  Vise 4 à 8 anneaux par entrée (0 à 7 en 'toneIndex' : les voix de l'accord puis leurs octaves), et fais varier ce nombre d'une entrée à l'autre. N'indique AUCUN renversement ni décalage d'octave par accord : la conduite des voix est calculée par l'application.\n"
	"  TIMBRE — SUGGESTION FORTE : un anneau par note, chaque note attaquée SÉPARÉMENT puis laissée résonner, c'est le geste d'un instrument RÉSONANT PINCÉ OU FRAPPÉ (harpe, handpan, kalimba) — pas celui d'un piano d'accompagnement. "
	"Choisis 'chordInstrument' dans cet esprit plutôt que le piano par défaut : 46 harpe, 108 kalimba, 12 marimba, 8 célesta, 11 vibraphone, 10 boîte à musique, 114 steel drums (esprit handpan), 15 dulcimer, 107 koto, 24 guitare nylon, 45 cordes pizzicato. "
	"Ça reste un choix musical : un autre timbre est permis si l'intention le demande.";

static const char *const kPolyDrumRules =
	"- BATTERIE EN POLYRYTHME : n'écris AUCUN champ 'drums'. La batterie est une suite de modules d'anneaux euclidiens ('polyDrums').\n"
	"  La batterie PEUT SE TAIRE sur une plage : il suffit de n'écrire AUCUNE entrée pour ces mesures (intro à nu, pont sans percussions…) — contrairement aux accords, un trou n'est pas comblé.\n"
	"  Un anneau = UN instrument de percussion, désigné par 'lane' (INDEX de lane, PAS une note MIDI). 'accentLane' (facultatif, −1 = aucun) = instrument joué sur le PREMIER coup de chaque tour de motif (ex. un son de basse de djembé sur le 1, le tone sur le reste).\n"
	"  Vise 3 à 5 anneaux — c'est ce qui rend le polyrythme LISIBLE.\n"
	"  LANES : 0 grosse caisse · 1 caisse claire · 2 charley fermé · 3 charley ouvert · 4 charley pied · 5 rimshot · 6 clap · 7 tom basse · 8 tom médium · 9 tom aigu · 10 crash · 11 ride\n"
	"          22 tambourin · 23 cowbell · 24 vibraslap · 25 bongo aigu · 26 bongo grave · 27 conga aigu étouffé · 28 conga aigu · 29 conga grave\n"
	"          30 timbale aiguë · 31 timbale grave (dundun) · 32 agogo aigu · 33 agogo grave · 34 cabasa · 35 maracas · 40 claves · 41/42 wood block · 45/46 triangle";

// ---- AiArrangementPrompt ---------------------------------------------------------------------------------------------------
static const char *const kRiffsSchema =
	"  \"riffs\": [ { \"section\": string, \"track\": string, \"instrument\": int(GM 0..127),\n"
	"      \"fromMeasure\": int, \"measures\": int,\n"
	"      \"notes\": [ [hauteur, début, durée], ... ] } ]\n"
	"      (chaque note = TABLEAU [hauteur MIDI (60=Do central), début en temps RELATIF au début de ce riff (0 = 1re note), durée en temps])";
static const char *const kMelodicLinesSchema =
	"  \"melodicLines\": [ { \"section\": string, \"track\": string, \"instrument\": int(GM 0..127),\n"
	"      \"fromMeasure\": int, \"measures\": int, \"durations\": [nombres en TEMPS],\n"
	"      \"anchor\": string, \"contour\": string, \"register\": int } ]";

// "First" of a name: up to its first space
static void firstWord (Text &sb, const char *s) { const char *sp = strchr (s, ' '); sb.add (s, sp && sp > s ? (int) (sp - s) : -1); }

static void systemPrompt (Text &sb, bool riffMode, bool wantDrums, bool chordsAsVoice, bool polyChords, bool polyDrums, bool english)
{
	if (polyChords) chordsAsVoice = false;			// the two options contradict each other: the rings win
	bool drumsBlock = wantDrums && !polyDrums;		// the polyrhythmic drums replace the 'drums' phrases
	bool tailBlock = drumsBlock || polyDrums;		// is there a block after 'polyChords'?

	sb.addLine ("Tu es un compositeur assistant. Tu renvoies UNIQUEMENT un objet JSON (aucune prose) décrivant un morceau à poser sur une timeline.");
	sb.addLine ("Schéma EXACT (respecte les noms de clés) :");
	sb.addLine ("{");
	sb.addLine ("  \"meter\": { \"num\": int, \"den\": int },");
	sb.addLine ("  \"key\": { \"tonic\": \"C|C#|D|...|B (ou Do, Ré...)\", \"mode\": \"major|minor\" },");
	sb.addLine ("  \"bpm\": int,");
	sb.addLine ("  \"chordInstrument\": int(GM 0..127, instrument de la piste d'accords),");
	sb.addLine ("  \"sections\": [ { \"name\": string, \"measures\": int } ],");
	sb.addLine ("  \"chords\": [ [mesure, degré, \"qualité\"], ... ],   (chaque accord = TABLEAU [mesure 1-based, degré 1..7 relatif à la tonalité, qualité string])");
	sb.addLine ("  \"articulation\": [ { \"section\": string, \"name\": string,\n"
		"      \"motif\": [ [voix, début, durée], ... ],   (chaque événement = TABLEAU [voix 0..10, début en temps, durée en temps])\n"
		"      \"melodicCell\": [ [degré, début, durée], ... ] } ],   (chaque note = TABLEAU [degré 1..14, début en temps, durée en temps])");
	// the melody: explicit riffs vs melodic lines; "chords as a voice" needs the riffs block too
	if (riffMode) sb.add (kRiffsSchema);
	else if (chordsAsVoice) { sb.add (kMelodicLinesSchema); sb.add (",\n"); sb.add (kRiffsSchema); }
	else sb.add (kMelodicLinesSchema);
	sb.addLine ((polyChords || tailBlock) ? "," : "");
	if (polyChords) { sb.add (kPolyChordSchema); sb.addLine (tailBlock ? "," : ""); }
	if (drumsBlock)
		sb.addLine ("  \"drums\": [ { \"section\": string, \"fromMeasure\": int, \"measures\": int,\n"
			"      \"motifBars\": int, \"repeats\": int,\n"
			"      \"notes\": [ [note GM, début, durée], ... ] } ]   (note batterie = TABLEAU [note GM 35..81, début en temps RELATIF au début du MOTIF, durée en temps])");
	if (polyDrums) sb.addLine (kPolyDrumSchema);
	sb.addLine ("}");
	sb.addLine ();
	sb.addLine ("Règles :");
	sb.add ("- LANGUE : rédige TOUS les libellés lisibles par un humain (noms de sections 'name', noms d'articulations 'name', et tout titre/intitulé) en ");
	sb.add (english ? "ANGLAIS" : "FRANÇAIS");
	sb.addLine (". Les clés JSON, elles, restent inchangées (en anglais).");
	sb.addLine ("- FORMAT COMPACT OBLIGATOIRE : réponds en JSON MINIFIÉ (aucun espace ni retour à la ligne superflu). Chaque note et chaque événement de motif est un TABLEAU ORDONNÉ (ex. [64,0,1] ou [64,0.5,1.5]), JAMAIS un objet {\"pitch\":...}. C'est ~2× plus court : tu peux ainsi écrire tout le morceau, jusqu'à la dernière section, sans être tronqué.");
	sb.addLine ("- Tu choisis la tonalité (key), la métrique (meter) et le tempo (bpm) selon le style et l'intention.");
	sb.addLine ("- Les accords sont donnés par DEGRÉ (chiffre 1..7 relatif à la tonalité), pas par nom absolu. Un accord par mesure (ou par changement).");
	sb.addLine ("- COULEUR des accords : dans la qualité (3e élément du tableau accord), précise la couleur quand le style s'y prête (7e, Maj7, m7, 9, add9, 6, m6, sus2, sus4, 7sus4…), pas seulement des triades. Selon l'ambiance (Maj7/add9 = doux/rêveur, 7 dom = tension, sus = suspension).");
	sb.addLine ("- La somme des 'measures' des sections = la longueur demandée.");
	if (polyChords || polyDrums) { cycleRules (sb); sb.addLine (); }	// the ring model is the same for both: told once
	if (polyChords) sb.addLine (kPolyChordRules);
	else if (chordsAsVoice)
	{
		sb.addLine ("- ACCORDS EN VOIX DÉDIÉE : laisse 'articulation' VIDE ([]). Les 'chords' (degrés) servent UNIQUEMENT de repère harmonique — la piste d'accords reste silencieuse ; NE lui donne AUCUN motif.");
		sb.addLine ("- À la place, écris toi-même le CONTENU des accords, mis en forme À TA GUISE, dans UNE entrée 'riffs' de rôle « Accords » (track=\"Accords\") : les vraies NOTES MIDI (renversements, espacement, arpèges ou rythme d'accompagnement de ton choix), couvrant TOUT le morceau et cohérentes avec les degrés de 'chords'. Donne-lui un 'instrument' adapté (souvent piano 0 ou harpe 46).");
	}
	else
	{
		sb.addLine ("- Chaque section a UNE 'articulation' = un MOTIF d'accompagnement d'UNE mesure (réutilisé sur toute la section), liste d'événements 'motif'.");
		sb.addLine ("  Chaque événement joue une VOIX de l'accord : voice = 0 basse, 1 fondamentale, 2 tierce, 3 quinte, 4 septième, 5 fondamentale(8va), 6 neuvième, 7 tierce(8va), 8 quinte(8va), 9 septième(8va), 10 neuvième(8va). 'start'/'length' en TEMPS.");
		sb.addLine ("  Ex. plaqué 3/4 : [0,0,3],[1,0,3],[2,0,3],[3,0,3]. Valse : [0,0,1],[1,1,1],[2,1,1],[3,1,1],[1,2,1],[2,2,1],[3,2,1]. Arpège : [0,0,1],[1,1,1],[2,2,1].");
		sb.addLine ("  Donne aussi un 'name' court et parlant à chaque articulation (ex. \"valse_couplet\", \"arpege_refrain\") : elle est enregistrée sous ce nom et réutilisable/modifiable ensuite.");
		sb.addLine ("- CELLULE MÉLODIQUE ('melodicCell', optionnelle mais RECOMMANDÉE) : une petite phrase chantante d'UNE mesure attachée au motif, jouée en 2e voix PAR-DESSUS chaque accord de la section.");
		sb.addLine ("  Elle s'écrit en DEGRÉS DIATONIQUES, pas en notes absolues : 1 = note d'ancrage de l'accord, 2..7 = degrés suivants de la gamme, 8..14 = les mêmes une octave au-dessus. Elle est donc TRANSPOSÉE MODALEMENT sur chaque accord (le même dessin sonne juste partout).");
		sb.addLine ("  'start'/'length' en TEMPS, dans la mesure (comme le motif). Ex. arpège montant : [1,0,1],[3,1,1],[5,2,1]. Ex. broderie : [1,0,0.5],[2,0.5,0.5],[1,1,1]. Garde-la COURTE (3 à 6 notes) et complémentaire du motif d'accompagnement.");
	}
	if (riffMode)
	{
		sb.addLine ("- MÉLODIE = des RIFFS : écris explicitement les NOTES (pitch MIDI + rythme). Les notes doivent tenir dans la tonalité et coller aux accords.");
		sb.addLine ("- COUVERTURE OBLIGATOIRE : produis UNE entrée 'riffs' pour CHAQUE section de 'sections' (et pour chaque piste/rôle), du DÉBUT à la FIN du morceau. NE t'arrête PAS après la première section — le tableau 'riffs' doit couvrir toutes les sections.");
		sb.addLine ("- Dans chaque entrée, 'start' est RELATIF au début de CE riff (0 = première note ; PAS un temps absolu depuis le début du morceau). Les notes couvrent toute la phrase : 'start' va de 0 à measures × (temps par mesure). Écris une mélodie évolutive (un motif court serait juste répété). Sois CONCIS sur le rythme pour ne pas dépasser la limite de sortie (privilégie noires/croches).");
		sb.addLine ("- SILENCES : pour DYNAMISER la mélodie, laisse des ESPACES entre les notes — il suffit que la note suivante COMMENCE PLUS TARD que la fin de la précédente (l'écart forme le silence). Respirations, fins de phrase, question-réponse ; évite un flux ininterrompu, surtout pour le lead.");
	}
	else
	{
		sb.addLine ("- MÉLODIE = des LIGNES : 'durations' = le motif rythmique d'une ligne (en TEMPS, ex. [1,1,0.5,0.5,1]), répété sur la partie ; le moteur choisit les hauteurs via 'contour' et 'anchor'.");
		sb.addLine ("- Choisis 'contour' et 'anchor' de CHAQUE ligne selon l'INTENTION (ex. sombre → contour descendant/statique + ancrage tierce/fondamentale ; joyeux → montante/vague). 'register' décale le registre en demi-tons (basse ≈ -24, mélodie 0, aigu +12).");
	}
	sb.addLine ("- VARIATION : fais VARIER le motif rythmique d'une section à l'autre (couplet ≠ refrain ≠ pont) — ne réutilise pas le même rythme partout.");
	sb.addLine ("- CONTRECHANT : ajoute une piste 'contrechant' rythmiquement COMPLÉMENTAIRE du 'lead' — elle joue plutôt dans les silences/tenues du lead (question-réponse), parfois en même temps.");
	sb.addLine ("- Regroupe par 'track' (rôle) : une piste par rôle. Chaque piste a son 'instrument' (GM 0..127). Ajoute AUTANT de pistes que l'orchestration le demande selon le style (ex. lead, contrechant, harmonies/nappe, arpèges, basse, doublures…) — vise une texture riche et vivante, pas seulement 2-3 voix.");
	sb.addLine ("- INSTRUMENTS (GM 0..127) selon le style : piano 0, cordes 48/49, violoncelle 42, contrebasse 43, flûte 73, hautbois 68, clarinette 71, harpe 46, guitare 24/25, cor 60, chœur 52.");
	if (polyDrums) sb.addLine (kPolyDrumRules);
	if (drumsBlock)
	{
		sb.addLine ("- BATTERIE ('drums') : un GROOVE = UN MOTIF COURT qui se RÉPÈTE, PAS des notes remplissant toute la section. Écris les 'notes' d'UN SEUL motif ('motifBars' mesures, souvent 1 ou 2), 'start' RELATIF au début du motif (0 à motifBars mesures). Renseigne 'motifBars' (longueur du motif) et 'repeats' (nombre de répétitions pour couvrir la section : motifBars × repeats = 'measures'). Ex. motif d'1 mesure sur une section de 4 → motifBars=1, repeats=4. Un FILL/variation en fin de section : mets-le dans le motif si motifBars couvre la section, sinon garde le motif régulier.");
		sb.addLine ("  pitch = note batterie GM. KIT : 36 grosse caisse, 38 caisse claire, 42 charley fermé, 46 charley ouvert, 44 charley pied, 49 crash, 51 ride, 39 clap, 37 rimshot, 41/43/45/47/48/50 toms.");
		sb.addLine ("  PERCUSSION SECONDAIRE (texture — superpose une 2e couche dans le MÊME motif selon le style, ça donne du corps/mouvement) : 54 tambourin, 56 cowbell, 69 cabasa, 70 maracas, 75 claves, 60/61 bongos, 62/63/64 congas, 76/77 wood block, 80/81 triangle. Ex. pop/rock → tambourin sur les temps ; latin/folk/bossa → shaker(cabasa/maracas) en croches + congas ; ballade → shaker léger ; valse → triangle. Dose selon l'intensité de la section.");
	}
	sb.addLine ();
	sb.addLine ("Valeurs autorisées :");
	sb.add ("- quality (accords) : ");
	for (int i = 0; i < 35; i++) { if (i) sb.add (", "); sb.add (kQualityFr[i]); }
	sb.addLine ();
	if (!riffMode)
	{
		sb.add ("- contour : ");
		for (int i = 0; i < 9; i++) { if (i) sb.add (", "); sb.addInt (i); sb.add ("="); firstWord (sb, kContourFr[i]); }
		sb.addLine ();
		sb.add ("- anchor : ");
		for (int i = 0; i < 6; i++) { if (i) sb.add (", "); sb.addInt (i); sb.add ("="); firstWord (sb, kAnchorFr[i]); }
		sb.addLine ();
	}
}

static void userPrompt (Text &sb, const char *style, int measures, const char *intention, const char *theme)
{
	Str st, in, th;
	if (blank (style)) st = "libre"; else trimmed (style, st);
	trimmed (intention, in);
	if (!blank (theme))
	{
		trimmed (theme, th);
		sb.fmt ("DÉVELOPPE / VARIE le thème ci-dessous sur environ %d mesures (développement : variation motivique, séquences, réharmonisation, modulations passagères — garde l'esprit et des fragments reconnaissables du thème). ", imax (1, measures));
		sb.add ("CHOISIS les accords du développement. GARDE la même tonalité et la même métrique que le thème (les mesures 'measure'/'fromMeasure' repartent de 1 pour le développement). ");
		sb.add ("Style : « "); sb.add (st); sb.add (" ».");
		if (!blank (intention)) { sb.add (" Intention : « "); sb.add (in); sb.add (" »."); }
		sb.add ("\n\n"); sb.add (th);
		sb.add ("\n\nRenvoie UNIQUEMENT l'objet JSON (mêmes clés que le schéma).");
		return;
	}
	sb.add ("Compose un morceau dans le style : « "); sb.add (st); sb.fmt (" », d'environ %d mesures.", imax (1, measures));
	if (!blank (intention)) { sb.add (" Intention musicale : « "); sb.add (in); sb.add (" »."); }
	sb.add (" Choisis tonalité, métrique et tempo cohérents. Renvoie UNIQUEMENT l'objet JSON.");
}

// ---- the element prompts (AiArrangement.Build*Prompt) ------------------------------------------------------------------------
static void drumGroovePrompt (const char *keyStr, const char *meterStr, int barTemps, const char *intention, Text &sys, Text &usr)
{
	Str in; trimmed (intention, in);
	sys.addLine ("Tu es un batteur/percussionniste assistant. Tu renvoies UNIQUEMENT un objet JSON (aucune prose) décrivant UN GROOVE de batterie/percussions.");
	sys.addLine ("Schéma EXACT : { \"motifBars\": int, \"repeats\": int, \"notes\": [ [note GM, début, durée], ... ] }");
	sys.add ("- Écris UN MOTIF de 'motifBars' mesures (souvent 1 ou 2) qui se RÉPÈTE 'repeats' fois. 'début' et 'durée' sont en TEMPS ; 'début' est relatif au début du motif (0 à motifBars×");
	sys.addInt (barTemps); sys.addLine (").");
	sys.addLine ("- 'note GM' KIT : 36 grosse caisse, 38 caisse claire, 42 charley fermé, 46 charley ouvert, 44 charley pied, 49 crash, 51 ride, 39 clap, 37 rimshot, 41/43/45/47/48/50 toms.");
	sys.addLine ("- PERCUSSION SECONDAIRE (texture, selon l'intention) : 54 tambourin, 56 cowbell, 69 cabasa, 70 maracas, 75 claves, 60/61 bongos, 62/63/64 congas, 76/77 wood block, 80/81 triangle.");
	sys.addLine ("- FORMAT COMPACT : chaque note est un TABLEAU ordonné [note, début, durée] (JSON minifié). motifBars × repeats = longueur totale voulue.");
	usr.add ("Mesure "); usr.add (meterStr); usr.fmt (" (%d temps par mesure), tonalité ", barTemps); usr.add (keyStr);
	usr.add (". Intention du groove : « "); usr.add (in); usr.add (" ». Compose un groove cohérent et musical. Renvoie UNIQUEMENT le JSON.");
}

static void riffPrompt (const char *keyStr, const char *meterStr, int barTemps, int measures, const Vec<AiChordRef> &chords, const char *intention, Text &sys, Text &usr)
{
	bool has = chords.size () > 0;
	Str in; trimmed (intention, in);
	sys.addLine ("Tu es un compositeur assistant. Tu renvoies UNIQUEMENT un objet JSON (aucune prose).");
	if (has)
	{
		sys.addLine ("Schéma EXACT : { \"notes\": [ [hauteur MIDI, début, durée], ... ] }  (la MÉLODIE du riff)");
		sys.fmt ("- Écris la mélodie d'un riff de %d mesure(s) qui SUIT les accords fournis : notes d'accord sur les temps forts, notes de passage/gamme ailleurs.\n", measures);
	}
	else
	{
		sys.addLine ("Schéma EXACT : { \"notes\": [ [hauteur MIDI, début, durée], ... ], \"chords\": [ [mesure, degré, \"qualité\"], ... ], \"articulation\": \"style\" }");
		sys.fmt ("- Aucun accord n'est présent : propose une PROGRESSION (degré 1..7 relatif à la tonalité, un accord par mesure ou par changement) sur %d mesure(s) ET la mélodie du riff dessus. Donne une 'articulation' d'accompagnement (ex. plaqué, arpège, valse, alberti…).\n", measures);
	}
	sys.fmt ("- 'hauteur' = MIDI (60 = Do central). 'début'/'durée' en TEMPS ; 'début' relatif au début du riff (0 à %d×%d). Reste dans la tonalité et cohérent avec la métrique.\n", measures, barTemps);
	sys.addLine ("- FORMAT COMPACT : chaque note/accord est un TABLEAU ordonné, JSON minifié.");

	usr.add ("Mesure "); usr.add (meterStr); usr.fmt (" (%d temps par mesure), tonalité ", barTemps); usr.add (keyStr); usr.fmt (", %d mesure(s). ", measures);
	if (has) { usr.add ("Accords présents [mesure, degré, qualité] : "); addChordList (usr, chords); usr.add (". "); }
	usr.add ("Intention : « "); usr.add (in); usr.add (" ». Renvoie UNIQUEMENT le JSON.");
}

static void addTrackPrompt (const char *fullContext, int barTemps, int measures, bool fullMelody, const char *intention, bool english, Text &sys, Text &usr)
{
	Str in; trimmed (intention, in);
	sys.addLine ("Tu es un compositeur assistant. Tu AJOUTES UNE nouvelle voix instrumentale par-dessus un morceau EXISTANT (fourni ci-dessous). Tu renvoies UNIQUEMENT un objet JSON (aucune prose).");
	sys.addLine ("- La nouvelle voix doit COMPLÉTER le morceau : cohérente avec les accords, la tonalité et le rythme des pistes existantes, sans les recopier (contrechant/complément, respirations, dialogue question-réponse).");
	sys.add ("- Le champ 'track' (nom de la piste) doit être rédigé en "); sys.add (english ? "ANGLAIS" : "FRANÇAIS");
	sys.add (" et nommer l'instrument (ex. "); sys.add (english ? "\"Flute\", \"Cello\"" : "\"Flûte\", \"Violoncelle\""); sys.addLine (").");
	if (fullMelody)
	{
		sys.addLine ("Schéma EXACT : { \"riffs\": [ { \"track\": string, \"instrument\": int(GM 0..127), \"fromMeasure\": int, \"measures\": int, \"notes\": [ [hauteur MIDI, début, durée], ... ] } ] }");
		sys.fmt ("- MÉLODIE COMPLÈTE : écris les NOTES explicites. 'hauteur' = MIDI (60 = Do central). 'début'/'durée' en TEMPS ; 'début' RELATIF au début de CHAQUE entrée (0 à measures×%d). Couvre les %d mesures, en une ou plusieurs entrées ('fromMeasure' 1-based, 'measures').\n", barTemps, measures);
	}
	else
	{
		sys.addLine ("Schéma EXACT : { \"melodicLines\": [ { \"track\": string, \"instrument\": int(GM 0..127), \"fromMeasure\": int, \"measures\": int, \"durations\": [nombres en TEMPS], \"anchor\": string, \"contour\": string, \"register\": int } ] }");
		sys.fmt ("- LIGNE MÉLODIQUE : fixe seulement le RYTHME ('durations' en temps, ex. [1,1,0.5,0.5,1]) ; le moteur choisit les hauteurs via 'contour'/'anchor'. Une durée NÉGATIVE = un silence. Couvre les %d mesures.\n", measures);
		sys.add ("- 'contour' ∈ { ");
		for (int i = 0; i < 9; i++) { if (i) sys.add (", "); sys.add (kContourFr[i]); }
		sys.add (" }. 'anchor' ∈ { ");
		for (int i = 0; i < 6; i++) { if (i) sys.add (", "); sys.add (kAnchorFr[i]); }
		sys.addLine (" }. 'register' = décalage en demi-tons (grave ≈ -12, médium 0, aigu +12).");
	}
	sys.addLine ("- Choisis un 'instrument' (GM 0..127) adapté à l'intention. FORMAT COMPACT : tableaux ordonnés, JSON minifié.");
	usr.addLine (fullContext);
	usr.addLine ();
	usr.add ("Intention pour la nouvelle voix : « "); usr.add (in); usr.add (" ». Renvoie UNIQUEMENT le JSON de la nouvelle piste.");
}

static void addDrumsPrompt (const char *fullContext, int barTemps, int measures, const char *intention, Text &sys, Text &usr)
{
	Str in; trimmed (intention, in);
	sys.addLine ("Tu es un batteur assistant. Tu AJOUTES UNE piste de batterie/percussions par-dessus un morceau EXISTANT (fourni ci-dessous). Tu renvoies UNIQUEMENT un objet JSON (aucune prose).");
	sys.addLine ("Schéma EXACT : { \"drums\": [ { \"fromMeasure\": int, \"measures\": int, \"motifBars\": int, \"repeats\": int, \"notes\": [ [note GM, début, durée], ... ] } ] }");
	sys.fmt ("- Un GROOVE = UN MOTIF COURT ('motifBars' mesures, souvent 1-2) qui se RÉPÈTE 'repeats' fois ; motifBars×repeats couvre 'measures'. 'début' RELATIF au début du motif (0 à motifBars×%d), en TEMPS.\n", barTemps);
	sys.addLine ("- Le groove doit coller au tempo/feel du morceau. KIT : 36 grosse caisse, 38 caisse claire, 42 charley fermé, 46 charley ouvert, 44 charley pied, 49 crash, 51 ride, 39 clap, 41/43/45/47/48/50 toms. PERCUS : 54 tambourin, 56 cowbell, 69 cabasa, 70 maracas, 75 claves, 62/63/64 congas, 80/81 triangle.");
	sys.fmt ("- Couvre les %d mesures (une entrée suffit : fromMeasure=1). FORMAT COMPACT : tableaux ordonnés, JSON minifié.\n", measures);
	usr.addLine (fullContext);
	usr.addLine ();
	usr.add ("Intention pour la batterie : « "); usr.add (in); usr.add (" ». Renvoie UNIQUEMENT le JSON.");
}

// ---- AiPolyrhythmPrompt ------------------------------------------------------------------------------------------------------
static void polyrhythmPrompt (const char *style, int measures, const char *intention, int meterNum, Text &sb, Text &usr)
{
	sb.addLine ("Tu es un compositeur assistant SPÉCIALISÉ en RYTHMES POLYRYTHMIQUES. Tu renvoies UNIQUEMENT un objet JSON (aucune prose autour).");
	sb.addLine ("MODÈLE : tous les calques partagent le MÊME cycle (durée = `beats` temps). Chaque calque découpe ce cycle en son propre nombre de PAS (steps), et une frappe tombe sur les cases actives d'un pattern euclidien E(hits,steps) éventuellement décalé par rotation. La polyrythmie vient de ce que 3 pas contre 5 pas dans le même cycle produisent des ratios musicalement intéressants (3-contre-5, 7-contre-4…).");
	sb.addLine ();
	sb.addLine ("Schéma EXACT :");
	sb.addLine ("{");
	sb.addLine ("  \"bpm\": int,                            // tempo (60..200)");
	sb.addLine ("  \"key\": { \"tonic\": \"C|C#|D|Eb|E|F|F#|G|Ab|A|Bb|B\", \"mode\": \"major|minor\" },");
	sb.addLine ("  \"durationBeats\": int,                  // durée totale en TEMPS (indicatif ; le module la répartit en cycle × répétitions)");
	sb.addLine ("  \"drum\": {");
	sb.addLine ("    \"kit\": 0,                            // toujours 0 (Standard) sauf demande explicite");
	sb.addLine ("    \"layers\": [ { \"lane\": int, \"hits\": int, \"steps\": int, \"rotation\": int }, ... ]");
	sb.addLine ("  },");
	sb.addLine ("  \"melodic\": {");
	sb.addLine ("    \"instrument\": int,                    // programme GM 0..127 (ex. 12 marimba, 108 kalimba, 73 flûte, 46 harpe)");
	sb.addLine ("    \"layers\": [ { \"voice\": 0|1|2, \"hits\": int, \"steps\": int, \"rotation\": int, \"legato\": bool }, ... ]");
	sb.addLine ("  },");
	sb.addLine ("  \"chord\": { \"root\": \"...\", \"quality\": \"major|minor|sus2|sus4|dim|aug\", \"octave\": int }");
	sb.addLine ("}");
	sb.addLine ();
	sb.addLine ("RÈGLES DES CALQUES :");
	sb.addLine (" - hits ∈ [1, steps] ; steps ∈ [2, 32] ; rotation ∈ [0, steps-1].");
	sb.addLine (" - Le \"cycle commun\" = ~4 temps (par défaut). Les calques diffèrent par leur NOMBRE DE PAS (steps).");
	sb.addLine (" - Choisis des paires de steps mutuellement PREMIÈRES pour un beau polyrythme (3&5, 4&7, 5&8, 7&12…). Éviter des steps identiques sur plusieurs calques.");
	sb.addLine (" - Vise 3 à 5 calques de batterie et 2 à 3 voix mélodiques ; c'est ce qui rend le polyrythme LISIBLE.");
	sb.addLine ();
	sb.addLine ("LANES DE PERCUSSION (index → nom) :");
	sb.addLine (" 0 grosse caisse · 1 caisse claire · 2 charley fermé · 3 charley ouvert · 5 rimshot · 6 clap");
	sb.addLine (" 7 tom basse · 8 tom médium · 9 tom aigu · 10 crash · 11 ride");
	sb.addLine (" 22 tambourin · 23 cowbell · 24 vibraslap");
	sb.addLine (" 25 bongo aigu · 26 bongo grave · 27 conga aigu étouffé · 28 conga aigu · 29 conga grave");
	sb.addLine (" 30 timbale aiguë · 31 timbale grave (dundun) · 32 agogo aigu · 33 agogo grave");
	sb.addLine (" 34 cabasa · 35 maracas · 40 claves · 45/46 triangle");
	sb.addLine ();
	sb.addLine ("VOIX MÉLODIQUES : voice=0 = basse, voice=1 = médium, voice=2 = aigu (le moteur choisit ensuite les hauteurs sur les accords).");
	sb.addLine (" - legato = true : la note dure jusqu'à la suivante (utile pour les nappes/soutiens) ; false = coup court.");

	int barTemps = imax (1, meterNum);
	Str st, in; trimmed (style, st); trimmed (intention, in);
	usr.fmt ("Compose une pièce POLYRYTHMIQUE de %d mesure(s) en %d/4 (soit %d temps). ", measures, meterNum, measures * barTemps);
	usr.add ("Style : « "); usr.add (st); usr.add (" ». ");
	if (!blank (intention)) { usr.add ("Intention : « "); usr.add (in); usr.add (" ». "); }
	usr.add ("Renvoie UNIQUEMENT le JSON, minifié.");
}

// ---- the public entry points -------------------------------------------------------------------------------------------------
static void keyMeter (const Project &p, const AiRequest &r, Str &keyStr, Str &meterStr)
{
	if (!r.keyText.empty ()) keyStr = r.keyText;
	else { char b[64]; keyName (p.key, b, sizeof b); keyStr = b; }
	if (!r.meterText.empty ()) meterStr = r.meterText;
	else { char b[16]; snprintf (b, sizeof b, "%d/%d", p.timeSigNum, p.timeSigDen); meterStr = b; }
}

static void setErr (char *err, int cap, const char *msg) { if (err && cap > 0) snprintf (err, cap, "%s", msg); }

bool aiBuildPrompt (const Project &p, const AiRequest &r, Str &system, Str &user, char *err, int errcap)
{
	Text sys, usr;
	int barTemps = p.barBeats ();
	switch (r.kind)
	{
	case AI_COMPOSE:
		systemPrompt (sys, r.fullMelody, r.drums, r.chordsVoice && !r.polyChords, r.polyChords, r.drums && r.polyDrums, r.english);
		userPrompt (usr, r.style, r.measures, r.intention, 0);
		break;
	case AI_DEVELOP:
	{
		int t = r.track, i = r.item;
		const Riff *riff = riffAt (p, t, i);
		if (!riff || riff->notes.size () == 0)
		{
			if (!lastRiff (p, &t, &i)) { setErr (err, errcap, "No riff to develop: select a riff (the theme)."); return false; }
			riff = riffAt (p, t, i);
		}
		Str theme; themeContext (p, t, i, *riff, theme);
		systemPrompt (sys, r.fullMelody, r.drums, r.chordsVoice && !r.polyChords, r.polyChords, r.drums && r.polyDrums, r.english);
		userPrompt (usr, r.style, r.measures, r.intention, theme);
		break;
	}
	case AI_ADD_TRACK:
	case AI_ADD_DRUMS:
	{
		int measures; Str ctx;
		fullPieceContext (p, ctx, &measures);
		if (r.kind == AI_ADD_DRUMS) addDrumsPrompt (ctx, barTemps, measures, r.intention, sys, usr);
		else addTrackPrompt (ctx, barTemps, measures, r.fullMelody, r.intention, r.english, sys, usr);
		break;
	}
	case AI_DRUM_GROOVE:
	{
		Str k, m; keyMeter (p, r, k, m);
		drumGroovePrompt (k, m, barTemps, r.intention, sys, usr);
		break;
	}
	case AI_RIFF:
	{
		Str k, m; keyMeter (p, r, k, m);
		double start, len; int measures; bool exists;
		riffTarget (p, r, &start, &len, &measures, &exists);
		Vec<AiChordRef> chords = chordsUnder (p, start, len, barTemps);
		riffPrompt (k, m, barTemps, measures, chords, r.intention, sys, usr);
		break;
	}
	case AI_POLYRHYTHM:
		polyrhythmPrompt (r.style, imax (1, r.measures), r.intention, 4, sys, usr);	// the placer forces 4/4
		break;
	default:
		setErr (err, errcap, "Unknown AI request.");
		return false;
	}
	sys.take (system); usr.take (user);
	return true;
}

void aiFullPrompt (const Str &system, const Str &user, Str &out)
{
	Text t; t.add (system); t.add ("\n\n"); t.add (user);
	t.take (out);
}

// ---- the helper's request and answer ------------------------------------------------------------------------------------------
void aiBuildRequestJson (const char *provider, const char *model, const char *apiKey, const Str &system, const Str &user,
			 double temperature, int thinking, json::Writer &out, const char *url)
{
	out.beginObj ();
	out.key ("provider"); out.str (provider ? provider : "gemini");
	out.key ("model"); out.str (model ? model : "");
	out.key ("key"); out.str (apiKey ? apiKey : "");
	out.key ("system"); out.str (system.c ());
	out.key ("user"); out.str (user.c ());
	out.key ("json"); out.boolean (true);
	out.key ("temperature"); out.num (temperature);
	out.key ("thinking"); out.num (thinking);
	if (url && url[0]) { out.key ("url"); out.str (url); }
	out.endObj ();
}

// the helper's output: the last whole progress line, the result line (the last one starting with '{')
static const char *scanOutput (const char *out, unsigned long len, unsigned long *resultLen, Str *lastProgress)
{
	const char *result = 0; *resultLen = 0;
	unsigned long i = 0;
	while (i < len)
	{
		unsigned long e = i;
		while (e < len && out[e] != '\n') e++;
		unsigned long b = i;
		while (b < e && (out[b] == ' ' || out[b] == '\t' || out[b] == '\r')) b++;
		if (b < e && out[b] == '{') { result = out + b; *resultLen = len - b; }	// the result: from here to the end
		else if (b < e && lastProgress && e < len)		// a whole progress line (the last one may be half written)
		{
			unsigned long q = e; while (q > b && (out[q - 1] == '\r' || out[q - 1] == ' ')) q--;
			const char *s = out + b; int n = (int) (q - b);
			if (n > 5 && !strncmp (s, "llm: ", 5)) { s += 5; n -= 5; }
			lastProgress->set (s, n);
		}
		i = e + 1;
	}
	return result;
}

bool aiParseLlmOutput (const char *out, unsigned long len, Str &text, Str &error, Str *lastProgress)
{
	text = ""; error = "";
	unsigned long resultLen;
	const char *result = scanOutput (out, len, &resultLen, lastProgress);
	if (!result) { error = "no answer from the AI helper"; return false; }
	json::Doc d;
	if (!d.parse (result, resultLen, json::TOLERANT)) { error = "the AI helper's answer is unreadable"; return false; }
	const json::Value &root = d.root ();
	if (root["ok"].asBool (false))
	{
		text = root["text"].asStr ("");
		if (text.empty ()) { error = "empty answer"; return false; }
		return true;
	}
	error = root["error"].asStr ("the request failed");
	return false;
}

void aiBuildFetchJson (const char *url, const char *outPath, json::Writer &out, int timeoutSec)
{
	out.beginObj ();
	out.key ("fetch"); out.str (url ? url : "");
	out.key ("out"); out.str (outPath ? outPath : "");
	out.key ("timeout"); out.num (timeoutSec);
	out.endObj ();
}

bool aiParseFetchOutput (const char *out, unsigned long len, unsigned long *bytes, Str &error, Str *lastProgress)
{
	error = ""; if (bytes) *bytes = 0;
	unsigned long resultLen;
	const char *result = scanOutput (out, len, &resultLen, lastProgress);
	if (!result) { error = "no answer from the download helper"; return false; }
	json::Doc d;
	if (!d.parse (result, resultLen, json::TOLERANT)) { error = "the download helper's answer is unreadable"; return false; }
	const json::Value &root = d.root ();
	if (root["ok"].asBool (false)) { if (bytes) *bytes = (unsigned long) root["bytes"].asLong (0); return true; }
	error = root["error"].asStr ("the download failed");
	return false;
}

} // namespace kt
