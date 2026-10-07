//
// progress.h -- Critters' progress (02 §7.2): per level solved, the most saved, the best time, over an fk_kv document
// (SD:/apps/critters.app/progress.ini, FK_KV_ESCAPES -- the window loads and saves it; the core does no I/O); the
// opening chain of the shipped levels; the [settings] (sound, last). Self-contained: no Critters type in its API
// (sections, integers, an ordered list of section names), so it can move into FileKit when a fourth game wants it.
// A line that does not read counts as absent; unknown sections and keys are kept (fk_kv writes back what it read).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _critters_progress_h
#define _critters_progress_h

#include "filekit/filekit.h"			// (fk_kv_*)

namespace critters {

struct Best { bool solved; int saved, timeSec; };	// saved / timeSec -1: none

// The section of a level file: a shipped level by its base name ("training-01-straight-down"), any other as
// "user.<base name>" (the ".level" ending dropped)
void progress_section (const char *path, bool shipped, char *out, int cap);
Best progress_get (const fk_kv *kv, const char *section);
// A won run: solved = 1, saved = the most, time = the shortest (each on its own) -> true: a new best (either) of a level solved before
bool progress_won (fk_kv *kv, const char *section, int saved, int timeSec);
// Level k of the chain (the shipped levels in the picker's order) is open: k = 0, or chain[k-1] solved
bool progress_open (const fk_kv *kv, const char *const *chain, int n, int k);
const char *progress_setting (const fk_kv *kv, const char *key, const char *def);	// [settings] sound, last
void progress_set_setting (fk_kv *kv, const char *key, const char *value);

}
#endif
