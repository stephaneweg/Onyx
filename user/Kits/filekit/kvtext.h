//
// kvtext.h -- FileKit: a text document of [section] headers and "key = value" lines, read and written back (a
// game's progress, a level pack, a settings file bigger than AppKit's .ini reader takes). The entries are kept in
// the file's order; a section may come back many times (a pack's [level] blocks: each "[...]" line is a block of its
// own); a value may hold new lines -- written "\n" (FK_KV_ESCAPES: a progress file) or as the lines that follow,
// each starting with "|" (FK_KV_PIPES: a pack's tables and solutions); the line of every value is kept (an error
// message's "line 42"). Unknown keys are kept: a program writes back what it read with only its own keys changed.
// No limit on sizes but memory. C and C++.
//
//   [pack]                       '#' and ';' start a comment line; a blank line or a comment ends a "|" block
//   title = 1. Gates
//   [level]                      fk_kv_block: 2 (the second "[...]" line); fk_kv_section: "level"
//   table =                      fk_kv_value: "A | Out\n0 | 0\n1 | 1"; fk_kv_line: 5 (its first "|" line),
//   | A | Out                    the value's line j at fk_kv_line + j
//   | 0 | 0
//   | 1 | 1
//
// FileKit's (filekit.so) since 2026-10-06 (AutoDev round 2, Circuits): this header declares, kvtext.inc has the code
// (on the PC the code is inline in the program, as fsutil.h's).
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
#ifndef _kvtext_h
#define _kvtext_h
#include "appkit/appkit.h"

// (a program for Onyx: FileKit's functions, by name. A PC build, or FK_KV_INLINE: the code inline in the program --
//  no shared library there. FK_KV_IMPL: the library's own build, kvtext.cpp.)
#if (defined (__aarch64__) && !defined (FK_KV_INLINE)) || defined (FK_KV_IMPL)
#  ifdef __cplusplus
#    define FK_KV_API	extern "C"
#  else
#    define FK_KV_API	extern
#  endif
#else
#  define FK_KV_API	static inline
#endif

typedef struct fk_kv fk_kv;			// a document (its entries, its blocks), made by fk_kv_new / parse / load
#define FK_KV_ESCAPES	1			// values may hold new lines and '\': written "\n", "\\", read back (progress.ini)
#define FK_KV_PIPES	2			// a value may go on in the lines that follow, each starting with "|" (packs)

// ---- making and freeing -----------------------------------------------------------------------
FK_KV_API fk_kv *fk_kv_new (int flags);					// an empty document (FK_KV_ESCAPES, FK_KV_PIPES); 0: no memory
FK_KV_API fk_kv *fk_kv_parse (const char *text, int flags);		// never fails on syntax (lines without '=' skipped); 0: no memory
FK_KV_API fk_kv *fk_kv_load (const char *path, int flags);		// a file read and parsed; 0: no such file / unreadable / no memory
FK_KV_API void fk_kv_free (fk_kv *kv);					// the document and all its strings (0 accepted)

// ---- the entries, in the file's order ---------------------------------------------------------
FK_KV_API int fk_kv_count (const fk_kv *kv);				// how many entries
FK_KV_API const char *fk_kv_key (const fk_kv *kv, int i);		// entry i's key, trimmed ("" out of range)
FK_KV_API const char *fk_kv_value (const fk_kv *kv, int i);		// its value: unescaped, its "|" lines joined by '\n'
FK_KV_API const char *fk_kv_section (const fk_kv *kv, int i);		// the name of its section ("" before any header)
FK_KV_API int fk_kv_block (const fk_kv *kv, int i);			// its block: 0 before any header, then 1, 2...: one per "[...]" line
// The 1-based line of the value's FIRST line: the key's line, or its first "|" line when "key =" is empty; the
// value's line j is at fk_kv_line + j. 0: an entry made by fk_kv_set, not read.
FK_KV_API int fk_kv_line (const fk_kv *kv, int i);

// ---- the blocks (one per "[...]" line) --------------------------------------------------------
FK_KV_API int fk_kv_blocks (const fk_kv *kv);				// the "[...]" headers read / made
FK_KV_API const char *fk_kv_block_name (const fk_kv *kv, int b);	// block b's name, "level" (b 1-based; "" for 0 / out of range)
FK_KV_API int fk_kv_block_line (const fk_kv *kv, int b);		// its header's line (0: made, not read)

// ---- looking up and changing ------------------------------------------------------------------
// The value of the first entry with that section and key, def if none (section "" or 0 = before any header,
// case-sensitive).
FK_KV_API const char *fk_kv_get (const fk_kv *kv, const char *section, const char *key, const char *def);
// The first match's value replaced, or a new entry at the end of that section's first block -> 0, -1 (no memory, no
// key). Section "" or 0 = before the first header; no block of that name = a new block at the end.
FK_KV_API int fk_kv_set (fk_kv *kv, const char *section, const char *key, const char *value);
FK_KV_API int fk_kv_remove (fk_kv *kv, const char *section, const char *key);	// the first match removed -> 1, 0 none

// ---- writing ----------------------------------------------------------------------------------
// The document as text: the comment as a first line ("# " put before it unless it starts with '#'; 0: none), the
// entries before any header, then each block "[name]" with its entries (ESCAPES: values escaped; PIPES: a multi-line
// value as "key =" and its "| " lines; neither: its new lines written as spaces). Written then parsed with the same
// flags, it gives the same entries. The pointer is the document's, valid until its next change / free; *len its
// length (0 accepted).
FK_KV_API const char *fk_kv_text (fk_kv *kv, const char *comment, int *len);
FK_KV_API int fk_kv_save (fk_kv *kv, const char *path, const char *comment);	// fk_kv_text written to path (kapi_save_file) -> 0, -1

#if (!defined (__aarch64__) || defined (FK_KV_INLINE)) && !defined (FK_KV_IMPL)
#include "kvtext.inc"
#endif

#endif
