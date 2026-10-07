//
// autostart.h -- SD:/etc/autostart, the programs started at boot (one shell command a line: `run agenda`,
// `keyb FR`, `preload /boot` the last one), as an app that offers "start it at every boot" changes it: a line
// looked for, a line added where it belongs -- never moved, never removed, every other line kept byte for byte.
// Setup's held-back lines count: on a card whose first-run wizard has not ended, a line is written
// "#setup: <command>" and given back by Setup at its end (apps/setup) -- such a line is "there", and a line
// added after one of them is held back the same way. Used by Notes (View > Show Stickies on the Desktop).
// C and C++.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef AUTOSTART_H
#define AUTOSTART_H
#include "appkit/appkit.h"
#include "sk_api.h"

#define AUTOSTART_PATH	"SD:/etc/autostart"
#define AUTOSTART_MAX	16384			// a bigger file is left alone (never cut)

// Is `cmd` ("run stickies", say) started at boot? A line whose words begin with cmd's words -- blanks before
// it, more words after it allowed ("run stickies --x") -- either active or held back by Setup
// ("#setup: run stickies"). A plain comment ("# run stickies") is not. -> 1 there, 0 not (or no file)
SK_API int autostart_has (const char *cmd);
// Make sure `cmd` is started at boot. Nothing written when autostart_has (cmd). Else the line `cmd`, after the
// comment line `comment` ("# ...", or 0: none), is INSERTED: right after the first line whose command starts
// with the words `after` (e.g. "run agenda"; 0: no such rule) -- with that line's "#setup: " when it has one
// (held back as it is); else just before the first `preload` line (it stays the last); else at the end (no
// file: a new one). -> 1 already there, 2 added, 0 not written (the file too big, the write failed)
SK_API int autostart_ensure (const char *cmd, const char *after, const char *comment);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "autostart.inc"
#endif

#endif
