//
// rec -- record the microphone from the command line, through AudioKit (ak_in_*: the input the Sound applet chose,
// a USB microphone or an I2S one on the header).
//   rec <file.wav> [seconds] [rate]   records that long (default 5 s, 16000 Hz) to a 16-bit mono WAV file; the
//                                     level is shown as it records (a key on stdin: stop -- the rest is silence)
//   rec --level [seconds]             the level only, no file (default 10 s): is the microphone heard?
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
#include "appkit/appkit.h"
#include "onyxpp.hpp"
#include "audiokit/audiokit.h"

static void putn (long v) { char b[24]; ax_itoa ((int) v, b); ax_puts (b); }

static int number (const char *s, int def)
{
	if (s == 0 || *s < '0' || *s > '9') return def;
	int n = 0;
	while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
	return n;
}

// A level line: the loudest sample of the last 0.2 s as a bar of 40, and in dBFS.
static void show_level (int peak)
{
	char bar[48]; int n = peak * 40 / 32767, i = 0;
	bar[i++] = '[';
	for (int k = 0; k < 40; k++) bar[i++] = k < n ? '#' : ' ';
	bar[i++] = ']'; bar[i] = 0;
	ax_puts (bar);
	int db = 0;					// 20 log10 (peak / 32767), by halves (-6 dB each)
	for (int p = peak; p > 0 && p < 16384 && db > -90; p *= 2) db -= 6;
	ax_puts (" "); if (peak == 0) ax_puts ("-inf"); else putn (db); ax_putln (" dBFS");
}

int main (void)
{
	char args[512];
	kapi_get_args (args, sizeof args);
	char *tok[4]; int nt = 0;
	for (char *p = args; *p && nt < 4; )
	{
		while (*p == ' ' || *p == '\t') p++;
		if (!*p || *p == '\r' || *p == '\n') break;
		tok[nt++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
		if (*p) *p++ = 0;
	}
	if (nt == 0)
	{
		ax_putln ("usage: rec <file.wav> [seconds] [rate]   (16-bit mono WAV; default 5 s, 16000 Hz)");
		ax_putln ("       rec --level [seconds]             (the microphone's level only)");
		return 1;
	}
	bool levelOnly = ax_streq (tok[0], "--level") != 0;
	int seconds = number (nt > 1 ? tok[1] : 0, levelOnly ? 10 : 5);
	int rate = number (nt > 2 ? tok[2] : 0, 16000);
	if (seconds < 1) seconds = 1;
	if (rate < 8000 || rate > 48000) { ax_putln ("rec: the rate is 8000..48000 Hz"); return 1; }

	int r = ak_in_open (rate);
	if (r == 0) { ax_putln ("rec: another program is recording"); return 1; }
	if (r < 0) { ax_putln ("rec: no microphone (Control Panel > Sound: Record from)"); return 1; }
	long sel = kapi_sound_input (KAPI_SND_IN_SELECT, -1, -1);
	ax_puts ("rec: "); ax_puts (KAPI_SND_IN_NOW (sel) == KAPI_SND_IN_I2S ? "the I2S microphone" : "the USB microphone");
	ax_puts (", "); putn (rate); ax_puts (" Hz, gain "); putn (KAPI_SND_IN_GAIN (sel)); ax_puts (" dB, ");
	putn (seconds); ax_putln (" s (a key: stop)");

	long long total = (long long) rate * seconds, done = 0;
	ak_wav *w = 0;
	if (!levelOnly)
	{
		w = ak_wav_begin (tok[0], rate, 1, total);
		if (w == 0) { ak_in_close (); ax_puts ("rec: cannot write "); ax_putln (tok[0]); return 1; }
	}
	static short buf[4096];
	int peak = 0; long long lastShow = 0;
	unsigned t0 = kapi_get_ticks ();
	bool stopped = false, gone = false;
	while (done < total)
	{
		int n = ak_in_read (buf, (int) (total - done < 4096 ? total - done : 4096));
		if (n < 0) { gone = true; break; }
		if (n > 0)
		{
			if (w) ak_wav_write (w, buf, n);
			int p = ak_in_peak (buf, n);
			if (p > peak) peak = p;
			done += n;
			if (done - lastShow >= rate / 5) { show_level (peak); peak = 0; lastShow = done; }
		}
		else
		{
			if (kapi_get_ticks () - t0 > (unsigned) (seconds * 100 + 300)) { gone = true; break; }	// (nothing comes)
			kapi_msleep (10);
		}
		char c;					// (a key stops it -- not the end of the command's own line)
		if (kapi_stdin () != 0 && kapi_stream_read_nb (kapi_stdin (), &c, 1) > 0 && c != '\r' && c != '\n') { stopped = true; break; }
	}
	ak_in_close ();
	if (w) ak_wav_end (w);
	if (gone) ax_putln (done == 0 ? "rec: nothing came from the microphone" : "rec: the microphone is gone");
	ax_puts ("rec: "); putn ((long) (done / (rate / 10))); ax_puts (" tenths of a second");
	ax_putln (stopped ? " (stopped)" : "");
	return gone && done == 0 ? 1 : 0;
}
