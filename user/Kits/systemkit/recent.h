//
// recent.h -- the documents opened last (SD:/etc/recent-docs: "YYYYMMDDHHMM|path" lines, the latest first, at most
// RECENT_DOCS_MAX, a path once). SystemKit's fa_open notes every file it opens with its app (the File Viewer's
// double click, the pocket shell's search and its Documents), and an app may note what it opens itself; the pocket
// shell's launcher shows them (its Recent tab: "Documents", each with the icon of the app that opens it). C and C++.
//
//   recent_doc_add ("SD:/docs/letters-tour.rtf");         // first of the list (again: moved up, its time renewed)
//   struct recent_doc d[12]; int n = recent_docs (d, 12);   // the latest first
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
#ifndef _recent_onyx_h
#define _recent_onyx_h
#include "appkit/appkit.h"
#include "sk_api.h"

#define RECENT_DOCS_FILE	"SD:/etc/recent-docs"
#define RECENT_DOCS_MAX		24
struct recent_doc
{
	char path[160];			// the file ("SD:/docs/cafe-2026.xlsx")
	int  date;			// when it was opened: YYYYMMDD (0: unknown)
	int  time;			// ... HHMM
};
// A document opened: first of the list (a path already there moved up, its time renewed; a folder or "" ignored).
SK_API void recent_doc_add (const char *path);
// The documents opened last, the latest first, into out[0..max) -> how many (0: none, no list yet).
SK_API int recent_docs (struct recent_doc *out, int max);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "recent.inc"
#endif

#endif
