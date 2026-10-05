//
// imgname.h -- preload's and unload's argument -> the program's path (kapi v77: program images,
// docs/02 section 7). An argument with a '/' or a ':' is a program file's path (relative: to the
// working directory -- the kernel resolves it); a bare name is the app of that name
// (SD:/apps/<name>.app/main) if there is one, else the /bin tool (SD:/bin/<name>).
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
#ifndef IMGNAME_H
#define IMGNAME_H

#include "appkit/appkit.h"
#include "applib.h"

// by_image 0: the app's file must exist (preload); 1: the app's image must (unload: the file may
// be gone already).
static inline void img_program (const char *arg, char *out, int cap, int by_image)
{
	int n = 0, sep = 0;
	out[0] = '\0';
	for (int i = 0; arg[i] != '\0'; i++) if (arg[i] == '/' || arg[i] == ':' || arg[i] == '\\') sep = 1;
	if (sep) { ax_strcat (out, cap, &n, arg); return; }

	ax_strcat (out, cap, &n, "SD:/apps/");
	ax_strcat (out, cap, &n, arg);
	ax_strcat (out, cap, &n, ".app/main");
	if (by_image)
	{
		if (kapi_image_list (out, 0, 0) == 1) return;
	}
	else
	{
		void *f = kapi_open (out);
		if (f != 0) { kapi_close (f); return; }
	}
	n = 0;
	ax_strcat (out, cap, &n, "SD:/bin/");
	ax_strcat (out, cap, &n, arg);
}

// The next word of args from *pos (spaces between) -> 1 + word, 0: no more.
static inline int img_next_arg (const char *args, int *pos, char *word, int cap)
{
	int i = *pos, n = 0;
	while (args[i] == ' ' || args[i] == '\t') i++;
	while (args[i] != '\0' && args[i] != ' ' && args[i] != '\t')
	{
		if (n < cap - 1) word[n++] = args[i];
		i++;
	}
	word[n] = '\0';
	*pos = i;
	return n > 0;
}

#endif
