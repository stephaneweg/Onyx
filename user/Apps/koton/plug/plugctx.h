//
// plug/plugctx.h -- a generator plugin's request and reply (kplug_proto.h KP_GENERATE; the plugin's
// side: kplug.h KpContext), without the kernel: the PC tests use them as the host does.
//
//   the context   { "from": 0, "to": L, "length": L, "blockStart": beat, "bpm", "pickup",
//                   "key": { "tonic": pc, "mode": 0 major / 1 minor, "scale": [7 pcs from the tonic],
//                            "name" }, "meter": [num, den], "seed": the module's,
//                   "chords": [{ "start", "len" (beats from the block's start), "root", "quality"
//                            (Koton's 0..34), "iv": [its intervals], "basic" (KotonChordQuality),
//                            "bass" (a pc), "name" }...],
//                   "state": the module's state (JSON) }
//   the reply     { "notes": [[start, len, MIDI note, velocity]...] } (beats from the block's start)
//
// A GeneratorModule keeps its state as base64 of the plugin's JSON (as Koton's files do).
//
#ifndef _koton_plugctx_h
#define _koton_plugctx_h

#include "../engine/gen.h"

namespace json { class Writer; }

namespace kt {

// the request for module m at startBeat of p, its state (JSON text, 0 or "": the defaults)
void genContext (const GeneratorModule &m, const Project &p, double startBeat, const char *stateJson, json::Writer &w);
// a reply's notes into a riff at SPQ slices a beat (its lengthSlices kept: the notes are cut there)
bool genReply (const char *text, unsigned long len, Riff &out);
// Koton's plugin contract: a chord quality (0..34) -> its KotonChordQuality (Major, Minor... Aug7)
int basicQuality (int quality);
// a module's state <-> the plugin's JSON
void b64encode (const char *data, int len, Str &out);
bool b64decode (const char *text, Str &out);
void moduleStateJson (const GeneratorModule &m, Str &json);		// "" when it has none
void setModuleStateJson (GeneratorModule &m, const char *json);

} // namespace kt

#endif
