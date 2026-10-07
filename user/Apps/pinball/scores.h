//
// scores.h -- Pinball's high scores: a top 5 per table and the player's settings, in an fk_kv document (FileKit) that
// the window reads from and writes to SD:/apps/pinball.app/scores.ini (fk_kv_load / fk_kv_save, FK_KV_ESCAPES):
//
//   [settings]                 name = the last name typed; sound = 0 muted; table = the last table played
//   [1-space-station]          a shipped table, by its file's base name; a player's table: [user.<base name>]
//   1 = 1250340 Steph          lines 1..5, best first: "<score> <name>"
//
// Equal scores: the older entry stays above. A line that does not read is skipped (and replaced when the table's
// lines are written again); unknown sections and keys are kept. No pinball type in this API: it can move into a kit
// the day a second game keeps high scores.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _pinball_scores_h
#define _pinball_scores_h

#include "filekit/filekit.h"

namespace pinball {

enum { TOPN = 5, NAMEC = 16 /* characters */, NAMEL = 4 * NAMEC + 1 /* bytes */ };
struct ScoreLine { long score; char name[NAMEL]; };

// The table's lines (best first) -> how many (0..TOPN); the lines that do not read are skipped
int scores_read (const fk_kv *kv, const char *section, ScoreLine out[TOPN]);
// Would this score enter the table's top 5? (a score of 0 never does)
bool scores_qualifies (const fk_kv *kv, const char *section, long score);
// The score added -> its rank (0 = the best) and the section's lines written again; -1: not in the top 5 (nothing
// written). The name: '=' and new lines become spaces, trimmed, cut to NAMEC characters ("Player" if empty).
int scores_add (fk_kv *kv, const char *section, long score, const char *name);
// The section of a table's file: "1-space-station" for a shipped one, "user.my-table" for any other
void scores_section (const char *path, bool shipped, char *out, int cap);
// [settings]: name, sound, table
const char *scores_setting (const fk_kv *kv, const char *key, const char *def);
void scores_set_setting (fk_kv *kv, const char *key, const char *value);
// A name as it is stored (the rules of scores_add) into out (NAMEL bytes)
void scores_clean_name (const char *name, char *out);

}

#endif
