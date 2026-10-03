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

#include "kapi.h"

#define PRELOAD_INI	"SD:/etc/preload.ini"
#define PRELOAD_MAX	32
#define PRELOAD_NAME	128

struct PreloadList
{
	int  n;
	char prog[PRELOAD_MAX][PRELOAD_NAME];
};

// -> how many programs the file lists (0: none, or no file)
static inline int preload_ini_load (struct PreloadList *l)
{
	static char buf[8192];
	l->n = 0;
	void *f = kapi_open (PRELOAD_INI);
	if (f == 0) return 0;
	int len = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (len < 0) len = 0;
	buf[len] = '\0';
	for (int i = 0; i < len && l->n < PRELOAD_MAX; )
	{
		while (i < len && (buf[i] == ' ' || buf[i] == '\t')) i++;
		int s = i;
		while (i < len && buf[i] != '\n' && buf[i] != '\r') i++;
		int e = i;
		while (e > s && (buf[e - 1] == ' ' || buf[e - 1] == '\t')) e--;
		while (i < len && (buf[i] == '\n' || buf[i] == '\r')) i++;
		if (e == s || buf[s] == '#' || buf[s] == ';' || buf[s] == '[') continue;
		int n = e - s < PRELOAD_NAME - 1 ? e - s : PRELOAD_NAME - 1;
		for (int k = 0; k < n; k++) l->prog[l->n][k] = buf[s + k];
		l->prog[l->n][n] = '\0';
		l->n++;
	}
	return l->n;
}

// -> 1 written, 0 not
static inline int preload_ini_save (const struct PreloadList *l)
{
	static char buf[8192];
	static const char head[] =
		"# preload.ini -- the programs loaded ahead at boot and kept in memory (`preload /boot`, the\n"
		"# last line of /etc/autostart): they start without reading the card. One a line: an app's\n"
		"# name, a /bin tool's or a path. The Control Panel's Preload applet writes this file.\n";
	int n = 0;
	for (int i = 0; head[i]; i++) buf[n++] = head[i];
	for (int p = 0; p < l->n; p++)
	{
		for (int i = 0; l->prog[p][i] && n < (int) sizeof buf - 2; i++) buf[n++] = l->prog[p][i];
		buf[n++] = '\n';
	}
	return kapi_save_file (PRELOAD_INI, buf, (unsigned) n) >= 0;
}

#endif
