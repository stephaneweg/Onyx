//
// preloadini.h -- SD:/etc/preload.ini: the programs loaded ahead at boot and kept in memory (`preload
// /boot`, the last line of /etc/autostart; the Control Panel's Preload applet writes the file).
// One program a line -- a path, an app's name (apps/<name>.app/main) or a /bin tool's, as preload's
// arguments (bin/imgname.h); blank lines and lines starting with '#' or ';' are skipped.
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
#ifndef PRELOADINI_H
#define PRELOADINI_H
#include "appkit/appkit.h"
#include "sk_api.h"


#define PRELOAD_INI	"SD:/etc/preload.ini"
#define PRELOAD_MAX	32
#define PRELOAD_NAME	128

struct PreloadList
{
	int  n;
	char prog[PRELOAD_MAX][PRELOAD_NAME];
};
// -> how many programs the file lists (0: none, or no file)
SK_API int preload_ini_load (struct PreloadList *l);
// -> 1 written, 0 not
SK_API int preload_ini_save (const struct PreloadList *l);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "preloadini.inc"
#endif

#endif
