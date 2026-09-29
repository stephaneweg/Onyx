//
// ai.h -- Koton's AI composition, ported from Koton Studio (MusicTracker/Engine/AI: AiArrangement.cs,
// AiArrangementPlacer.cs, AiPolyArrangement.cs, AiPolyrhythm.cs, AiProviders.cs, the clients; and the
// parts of Timeline/ChordModelOps.cs and TimelineHelper.cs they call: AddAiChord, ApplyAiDrum,
// ApplyAiRiff, ChordsUnder, AddSectionMarkers).
//
// The flow (the app side; the network is a helper process, SD:/bin/llm, as Lisa does with bin/groq):
//
//   AiRequest r; r.kind = AI_COMPOSE; r.style = "bossa"; r.measures = 32; ...
//   Str sys, usr;
//   aiBuildPrompt (project, r, sys, usr);                   the French prompts, verbatim from Koton
//   json::Writer w (false);
//   aiBuildRequestJson ("gemini", model, key, sys, usr, 0.7, -1, w);
//   ... spawn SD:/bin/llm, write w.data () to its stdin (or a file), read its stdout ...
//   Str text, error;
//   if (aiParseLlmOutput (out, outLen, text, error)) aiApplyReply (project, r, text, err, sizeof err);
//
// Without a key: "Copy the prompt" = aiFullPrompt (sys, usr); "Paste a reply" = aiApplyReply on the
// clipboard's text. The replies are parsed tolerantly (a ``` fence, text around the object, trailing
// commas, notes as [a,b,c] arrays or {..} objects, numbers as strings, one object instead of an array).
//
// The prompts are Koton's, in French, byte for byte (they are tuned): only the human-readable labels
// the model writes (section / track names) follow AiRequest::english. The replies are placed exactly as
// AiArrangementPlacer places them: the chord track pinned last with one degree-locked chord a bar
// (its colour from colourForQuality), the accompaniment as chord articulations on their own track,
// melodic lines or riffs (+ PlayRiff modules) in 4-bar blocks, drums as custom drum modules tiled
// from their motif, polychords / polydrums when asked, the sections as markers, key / meter / bpm.
//
#ifndef _koton_ai_h
#define _koton_ai_h

#include "model.h"

namespace kt {

// ---- the request ------------------------------------------------------------------------------------------
enum AiKind
{
	AI_COMPOSE = 0,		// a new piece ("Compose with AI"): the project is REPLACED (Koton's BuildFresh)
	AI_DEVELOP,		// develop / vary a theme (a riff), the result appended after the end of the piece
	AI_ADD_TRACK,		// one new instrument voice over the whole piece (a full melody or a melodic line)
	AI_ADD_DRUMS,		// a drum track over the whole piece
	AI_DRUM_GROOVE,		// the groove of one drum module
	AI_RIFF,		// the notes of one riff (+ a chord progression when no chord is under it)
	AI_POLYRHYTHM,		// a polyrhythmic piece (Koton's AiPolyDialog): the project is REPLACED
	AI_KINDS
};
extern const char *const g_aiKindNames[AI_KINDS];	// "Compose a piece", "Develop the theme"...

struct AiRequest
{
	int kind;
	Str style, intention;		// the free texts of the dialog
	int measures;			// COMPOSE / DEVELOP: about how many bars; POLYRHYTHM: bars of 4/4;
					// RIFF: the length of a NEW riff (an existing one keeps its own)
	bool fullMelody;		// COMPOSE / DEVELOP: melody as riffs (explicit notes: Koton's "riff mode") vs melodic
					// lines (rhythm only); ADD_TRACK: a full melody vs a rhythm-only melodic line
	bool drums;			// COMPOSE / DEVELOP: a drum part
	bool chordsVoice;		// COMPOSE / DEVELOP: the AI voices the chords itself in an "Accords" riff track
	bool polyChords, polyDrums;	// COMPOSE / DEVELOP: the polyrhythmic options (polyChords wins over chordsVoice;
					// polyDrums only with drums)
	bool english;			// the labels the model writes (sections, tracks) in English (else in French)
	int track, item;		// DEVELOP: the theme, a PlayRiff (-1: the last riff of the piece);
					// DRUM_GROOVE: the drum module (-1: a new one); RIFF: the PlayRiff (-1: a new one,
					// appended to `track` when it is an instrument track, else on a new track)
	Str keyText, meterText;		// DRUM_GROOVE / RIFF: how the key and the meter are named in the prompt (Koton
					// uses the toolbar's summary); empty: keyName () and "num/den"
	AiRequest () : kind (AI_COMPOSE), measures (32), fullMelody (false), drums (true), chordsVoice (false),
		polyChords (false), polyDrums (false), english (true), track (-1), item (-1) {}
};

// The system and user prompts. False (with a message) when the request cannot be made: an unknown
// kind, DEVELOP with no riff at all in the piece (with no theme given, the last riff of the piece is the
// theme). A RIFF / DRUM_GROOVE target that is not a riff / a drum module means a new one.
bool aiBuildPrompt (const Project &p, const AiRequest &r, Str &system, Str &user, char *err = 0, int errcap = 0);

// "Copy the prompt" (Koton: system + a blank line + user)
void aiFullPrompt (const Str &system, const Str &user, Str &out);

// Check a reply without touching the project (the dialog's summary before "Apply"): false with the
// parse error in `summary`, else a line such as "OK: 4 sections, 32 chords, 3 melodic lines, 1 drum part."
bool aiCheckReply (const AiRequest &r, const char *replyText, char *summary, int cap);

// Place a reply (the model's text) on the project as Koton does. False with a message when the reply is
// not usable (not JSON, no chords for a piece, no notes for a riff / groove); the project is then
// unchanged. COMPOSE / POLYRHYTHM replace the whole project (tempo, key, meter, tracks, riffs, markers);
// the others add to it.
bool aiApplyReply (Project &p, const AiRequest &r, const char *replyText, char *err, int errcap);

// ---- the helper process (SD:/bin/llm) -----------------------------------------------------------------------
// The JSON document /bin/llm reads on stdin:
//   { "provider", "model", "key", "system", "user", "json": true, "temperature", "thinking"[, "url"] }
// provider: "gemini", "groq", "mistral", "claude", "openai-compatible" (+ "deepseek", "grok", "openai":
// OpenAI-compatible endpoints); url: another endpoint (a local server...); thinking: Gemini's thinking
// budget in tokens, -1 = the model's default.
void aiBuildRequestJson (const char *provider, const char *model, const char *apiKey, const Str &system,
			 const Str &user, double temperature, int thinking, json::Writer &out, const char *url = 0);

// Read what /bin/llm wrote on its stdout: progress lines ("llm: connecting to ...", "llm: receiving
// 12345 bytes"; Onyx sends a child's stderr to the same stream) then ONE line of JSON:
// { "ok": true, "text": "..." } or { "ok": false, "error": "..." }. True with the model's text; false
// with the error (or "no answer" when the result line is missing: the helper died). *lastProgress:
// the last progress line (for a status bar while it runs: call it on the partial output too).
bool aiParseLlmOutput (const char *out, unsigned long len, Str &text, Str &error, Str *lastProgress = 0);

// /bin/llm's download mode (Koton's SoundFont): { "fetch": url, "out": path[, "timeout": s] } ->
// { "ok": true, "bytes": N } or { "ok": false, "error": "..." }; the progress lines as above
// ("llm: receiving 1048576 bytes of 31000000")
void aiBuildFetchJson (const char *url, const char *outPath, json::Writer &out, int timeoutSec = 120);
bool aiParseFetchOutput (const char *out, unsigned long len, unsigned long *bytes, Str &error, Str *lastProgress = 0);

// the providers (Koton's AiProviders) and their default models (the dialog's model box is editable)
enum { AI_PROVIDER_COUNT = 7 };
extern const char *const g_aiProviders[AI_PROVIDER_COUNT];	// "gemini", "groq", "mistral", "claude", "deepseek", "grok", "openai-compatible"
const char *aiProviderLabel (const char *provider);		// "Gemini"...
const char *aiProviderDefaultModel (const char *provider);	// "" for openai-compatible

// ---- internals shared by ai.cpp and ai_place.cpp --------------------------------------------------------------
namespace aidet {

// Koton's name tables, in French: the prompts list them and the replies use them
extern const char *const kQualityFr[35];		// PatternGenerator.QualityNames
extern const char *const kStyleFr[29];			// PatternGenerator.StyleNames
extern const char *const kContourFr[9];			// MelodicLineEngine.ContourNames
extern const char *const kAnchorFr[6];			// MelodicLineEngine.AnchorNames

// AiTranslate: a name (or an index as text) -> an index, tolerant as Koton's
int qualityIndex (const char *name);			// 0 (Majeur) when unknown
int contourIndex (const char *name);
int anchorIndex (const char *name);
int styleIndex (const char *name);
Key parseKey (const char *tonic, const char *mode);
int rootPc (const Key &k, int degree1based);

// a double as .NET's ToString ("0.##", InvariantCulture): "1.5", "0.04", "2"
void num2 (double v, char *buf, int cap);
// KeySig.Derive (key, 0).Name: "Fa♯ mineur", "Si♭ majeur"
void frenchKeyName (const Key &k, Str &out);

// a growable text (the prompts)
struct Text
{
	char *s; int n, cap;
	Text () : s (0), n (0), cap (0) {}
	~Text () { delete [] s; }
	void add (const char *t, int len = -1);
	void addLine (const char *t = "") { add (t); add ("\n"); }
	void addInt (long long v);
	void addNum (double v);				// num2
	void fmt (const char *f, ...) __attribute__ ((format (printf, 2, 3)));
	const char *c () const { return s ? s : ""; }
	void take (Str &out) const { out = c (); }
private:
	Text (const Text &);
	Text &operator= (const Text &);
};

// Koton's StripFences + ExtractFirstObject: the first balanced {...} of a reply (a fence and any text
// around it dropped; an unbalanced object -> from its '{' to the last '}'). Into out.
void cleanJson (const char *s, Str &out);

// ChordsUnder: the chords of the chord track overlapping [start, start + len), as [measure, degree, quality]
// with the measure relative to `start` (1-based) and the degree 1..7
struct AiChordRef { int measure, degree; int quality; };
Vec<AiChordRef> chordsUnder (const Project &p, double startBeat, double lenBeats, int barTemps);

// "the last riff of the piece" (DEVELOP with no theme): track / item of the PlayRiff starting last, or false
bool lastRiff (const Project &p, int *track, int *item);
// The riff a RIFF request writes: its start and length in beats, whether it exists
void riffTarget (const Project &p, const AiRequest &r, double *startBeat, double *lenBeats, int *measures, bool *exists);

} // namespace aidet

} // namespace kt

#endif
