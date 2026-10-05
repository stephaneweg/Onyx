//
// aksf.cpp -- AudioKit (audiokit.h): the SoundFont -- where it is on the card, its load into MeltySynth,
// the default one kept for the process. Only the kapi's files and MeltySynth: compiled into the
// library and, as it is, into the PC builds (Koton for Windows: the kapi is there too).
// One search for everybody (it was written twice: the Media Player's, Koton's).
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "kapi.h"
#include "synth/meltysynth.h"		// (Apps/koton)
#include "audiokit.h"

// The folders, in this order: the shared one (the package generaluser-gs), Koton's old ones, the music.
static const char *const s_dirs[] = { "SD:/res/soundfonts", "SD:/koton/soundfonts", "SD:/music/soundfonts", "SD:/music", "SD:/apps/koton.app" };

static bool is_sf2 (const char *name)
{
	size_t n = strlen (name);
	return n > 4 && name[n - 4] == '.' && (name[n - 3] == 's' || name[n - 3] == 'S') && (name[n - 2] == 'f' || name[n - 2] == 'F') && name[n - 1] == '2';
}

extern "C" int ak_soundfont_find (const char *preferred, char *out, int cap)
{
	if (out == 0 || cap <= 0) return 0;
	out[0] = 0;
	if (preferred != 0 && preferred[0])
	{
		void *f = kapi_open (preferred);
		if (f != 0) { kapi_close (f); snprintf (out, cap, "%s", preferred); return 1; }
	}
	for (unsigned k = 0; k < sizeof s_dirs / sizeof s_dirs[0]; k++)
	{
		void *d = kapi_opendir (s_dirs[k]);
		if (d == 0) continue;
		struct kapi_dirent e;
		bool found = false;
		while (!found && kapi_readdir (d, &e) > 0)
			if (!e.is_dir && is_sf2 (e.name)) { snprintf (out, cap, "%s/%s", s_dirs[k], e.name); found = true; }
		kapi_closedir (d);
		if (found) return 1;
	}
	return 0;
}

extern "C" void *ak_soundfont_load (const char *path, char *err, int cap)
{
	char e0[160];
	if (err == 0 || cap <= 0) { err = e0; cap = (int) sizeof e0; }
	err[0] = 0;
	void *f = kapi_open (path);
	if (f == 0) { snprintf (err, cap, "The SoundFont %s cannot be opened.", path); return 0; }
	unsigned long long n = kapi_fsize64 (f);
	if (n == 0 || n > (512ull << 20)) { kapi_close (f); snprintf (err, cap, "The SoundFont %s cannot be read.", path); return 0; }
	unsigned char *b = (unsigned char *) malloc ((size_t) n);
	if (b == 0) { kapi_close (f); snprintf (err, cap, "Not enough memory for the SoundFont (%llu MB).", n >> 20); return 0; }
	unsigned long long got = 0;
	while (got < n)
	{
		unsigned k = n - got > (1u << 20) ? (1u << 20) : (unsigned) (n - got);
		int r = kapi_read (f, b + got, k);
		if (r <= 0) break;
		got += (unsigned) r;
	}
	kapi_close (f);
	ms::SoundFont *sf = 0;
	char e[128] = "";
	if (got == n) sf = ms::soundfont_load (b, (size_t) n, e, sizeof e);
	free (b);
	if (sf == 0) snprintf (err, cap, "The SoundFont %s cannot be used (%s).", path, got == n ? e : "read error");
	return sf;
}
extern "C" void ak_soundfont_free (void *sf)			{ if (sf != 0) ms::soundfont_free ((ms::SoundFont *) sf); }

// The default one: found and loaded once for the process, kept.
static ms::SoundFont *s_sf;
static bool s_tried;
static char s_name[96];
static char s_prefer[256];			// (ak_soundfont_prefer: a program's setting)

extern "C" void ak_soundfont_prefer (const char *path)		{ snprintf (s_prefer, sizeof s_prefer, "%s", path != 0 ? path : ""); }

extern "C" void *ak_soundfont_default (char *err, int cap)
{
	char e0[160];
	if (err == 0 || cap <= 0) { err = e0; cap = (int) sizeof e0; }
	err[0] = 0;
	static const char none[] = "No SoundFont to play MIDI (SD:/res/soundfonts: the package GeneralUser GS).";
	if (s_sf != 0 || s_tried) { if (s_sf == 0) snprintf (err, cap, "%s", none); return s_sf; }
	s_tried = true;
	char path[256];
	if (!ak_soundfont_find (s_prefer, path, sizeof path)) { snprintf (err, cap, "%s", none); return 0; }
	s_sf = (ms::SoundFont *) ak_soundfont_load (path, err, cap);
	if (s_sf == 0) return 0;
	const char *nm = ms::soundfont_name (s_sf);
	const char *base = strrchr (path, '/');
	snprintf (s_name, sizeof s_name, "%s", nm != 0 && nm[0] ? nm : base != 0 ? base + 1 : path);
	return s_sf;
}
extern "C" const char *ak_soundfont_name (void)			{ return s_sf != 0 ? s_name : ""; }
