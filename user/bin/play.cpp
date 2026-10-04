//
// play -- play a sound file from the command line, through AudioKit (SD:/lib/audiokit.so,
// audiokit/audiokit.h): MP3, FLAC, WAV, Ogg Vorbis, MIDI (through the SoundFont).
//   play <file> [volume 0..100]     plays it to its end (Ctrl+C / a key on stdin: stop)
//   play --info <file>              what the file is
//   play --notes                    a scale on the General MIDI synthesizer (the library's self-test)
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
#include "kapi.h"
#include "onyxpp.hpp"
#include "applib.h"
#include "audiokit/audiokit.h"

static void putn (long v) { char b[24]; ax_itoa ((int) v, b); ax_puts (b); }

static int info (const char *path)
{
	char err[128];
	ak_stream *s = ak_open (path, err, sizeof err);
	if (s == 0) { ax_puts ("play: "); ax_putln (err); return 1; }
	struct ak_info i;
	ak_info_of (s, &i);
	ax_puts (i.format); ax_puts (", "); putn (i.rate); ax_puts (" Hz, "); putn (i.channels); ax_puts (" channel(s), ");
	putn (i.length_ms / 1000); ax_puts (" s");
	if (i.kbps) { ax_puts (", "); putn (i.kbps); ax_puts (" kbit/s"); }
	ax_putln ("");
	// the first second read: the decoder works
	static short buf[2 * 4096];
	long got = 0, peak = 0;
	for (int k = 0; k < 11; k++)
	{
		int n = ak_read (s, buf, 4096);
		if (n <= 0) break;
		got += n;
		for (int j = 0; j < n * 2; j++) { int a = buf[j] < 0 ? -buf[j] : buf[j]; if (a > peak) peak = a; }
	}
	ax_puts ("read "); putn (got); ax_puts (" frames, peak "); putn (peak); ax_putln ("");
	ak_close (s);
	return got > 0 ? 0 : 1;
}

static int notes (void)
{
	static const int scale[8] = { 60, 62, 64, 65, 67, 69, 71, 72 };
	char name[8];
	ak_program (0, 0);					// a piano
	for (int i = 0; i < 8; i++)
	{
		if (ak_note_on (0, scale[i], 100) != 0) { ax_puts ("play: "); ax_putln (ak_play_error ()); return 1; }
		ak_note_name (scale[i], name);
		ax_puts (name); ax_puts (" "); putn (ak_note_mhz (scale[i]) / 1000); ax_putln (" Hz");
		kapi_msleep (250);
		ak_note_off (0, scale[i]);
	}
	ak_note_on (0, 60, 100); ak_note_on (0, 64, 100); ak_note_on (0, 67, 100);	// the chord
	kapi_msleep (900);
	ak_notes_off ();
	kapi_msleep (1200);
	ax_puts ("SoundFont: "); ax_putln (ak_soundfont_name ());
	return 0;
}

int main (void)
{
	static char a[512];
	kapi_get_args (a, sizeof a);
	char *p = a; while (*p == ' ') p++;
	if (!*p) { ax_putln ("usage: play <file> [volume 0..100] | play --info <file> | play --notes"); return 1; }
	if (p[0] == '-' && p[1] == '-' && p[2] == 'n') return notes ();
	if (p[0] == '-' && p[1] == '-' && p[2] == 'i') { p += 6; while (*p == ' ') p++; return info (p); }
	// the file's name, then a volume (a name with spaces: in quotes by the shell)
	int vol = -1;
	char *e = p + ax_strlen (p);
	while (e > p && e[-1] == ' ') *--e = 0;
	char *q = e;
	while (q > p && q[-1] >= '0' && q[-1] <= '9') q--;
	if (q > p && q < e && q[-1] == ' ') { vol = 0; for (char *d = q; *d; d++) vol = vol * 10 + (*d - '0'); q[-1] = 0; }
	if (vol >= 0) ak_play_volume (vol);
	if (ak_play (p, 0) != 0) { ax_puts ("play: "); ax_putln (ak_play_error ()); return 1; }
	ax_puts ("playing "); ax_puts (p); ax_puts (" ("); putn (ak_play_len_ms () / 1000); ax_putln (" s)");
	for (;;)
	{
		int st = ak_play_wait (200);
		if (st == AK_STOPPED) break;
		char c;
		if (kapi_stdin () != 0 && kapi_stream_read_nb (kapi_stdin (), &c, 1) > 0) { ak_play_stop (); break; }	// a key: stop
	}
	kapi_msleep (150);					// (the last frames out)
	return 0;
}
