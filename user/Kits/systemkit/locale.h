//
// locale.h -- the system's language and region, kept in SD:/etc/system.ini:
//   language = fr        the language of the programs' words ("en" when no line): uikit/lang.h's TR () reads
//                        it (uk_lang_init), a program with words of its own asks locale_language ()
//   zone     = Brussels  the time zone's city (locale_zone_*), beside "timezone=" -- its offset in minutes,
//                        the summer time counted, which the kernel reads at boot
// Chosen in the Control Panel's Language & Region applet and in Setup (the first-run wizard). A language is
// taken by a program when it starts.
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
#ifndef _locale_onyx_h
#define _locale_onyx_h
#include "appkit/appkit.h"
#include "sk_api.h"

#define LOCALE_INI	"SD:/etc/system.ini"

// system.ini's "key=value": the value in out ("" none) -> 1 found; the key's line replaced (else added) -> 1 written.
SK_API int locale_ini_get (const char *key, char *out, int cap);
SK_API int locale_ini_set (const char *key, const char *value);

// ---- the languages Onyx speaks ----
SK_API int locale_language_count (void);
SK_API const char *locale_language_code (int i);	// "en", "fr" ("" out of range)
SK_API const char *locale_language_name (int i);	// in the language itself, UTF-8: "English", "Français"
// The system's language: one of the codes ("en" when none, or an unknown one, is said). Read from the file at
// each call: a program keeps it.
SK_API const char *locale_language (void);
SK_API int locale_language_index (void);		// its place in the list
SK_API int locale_set_language (const char *code);	// kept in system.ini -> 1 written (the programs started next take it)

// ---- the time zones ----
SK_API int locale_zone_count (void);
SK_API const char *locale_zone_city (int z);		// "Brussels" ("" out of range)
SK_API int locale_zone_summer (int z);			// 1: the zone is on summer time today (by the clock's date)
SK_API int locale_zone_offset (int z);			// minutes from UTC today (the summer time counted)
SK_API void locale_zone_utc (int z, char *out, int cap);	// "UTC+2", "UTC-3:30", "UTC"
SK_API int locale_zone (void);				// the one chosen: system.ini's zone=, else the first of its timezone= (-1 none)
SK_API int locale_set_zone (int z);			// the clock's offset at once, zone= and timezone= kept -> 1 written

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "locale.inc"
#endif

#endif
