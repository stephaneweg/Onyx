//
// akwav.cpp -- AudioKit (audiokit.h): WAV files written -- a buffer at once (ak_wav_save), or a long
// one as it is made (ak_wav_begin, ak_wav_write, ak_wav_end: an export). Only the kapi's files:
// compiled into the library and, as it is, into the PC builds (Koton for Windows).
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
#include "appkit/appkit.h"
#include "audiokit.h"

extern "C" int ak_wav_save (const char *path, const short *frames, int n, int rate)
{
	if (path == 0 || frames == 0 || n < 0) return -1;
	unsigned bytes = (unsigned) n * 4;
	unsigned char *b = (unsigned char *) malloc (44 + bytes);
	if (b == 0) return -1;
	ak_wav_header (b, rate, 2, bytes);
	memcpy (b + 44, frames, bytes);
	int r = kapi_save_file (path, b, 44 + bytes);
	free (b);
	return r == (int) (44 + bytes) ? 0 : -1;
}

// A long file: its length is said first (the header is written before the frames: the file is a
// stream, not gone back into).
struct ak_wav { void *out; int channels; long long left; int bad; };

extern "C" ak_wav *ak_wav_begin (const char *path, int rate, int channels, long long frames)
{
	if (path == 0 || frames < 0 || channels < 1 || channels > 2) return 0;
	if (frames * 2 * channels > 0xFFFFFF00ll - 44) return 0;		// (a WAV file's limit: 4 GB)
	void *out = kapi_file_out (path, 0);
	if (out == 0) return 0;
	unsigned char h[44];
	ak_wav_header (h, rate, channels, (unsigned) (frames * 2 * channels));
	kapi_stream_write (out, h, 44);
	ak_wav *w = (ak_wav *) malloc (sizeof (ak_wav));
	if (w == 0) { kapi_stream_close (out); return 0; }
	w->out = out; w->channels = channels; w->left = frames; w->bad = 0;
	return w;
}
extern "C" int ak_wav_write (ak_wav *w, const short *frames, int n)
{
	if (w == 0 || frames == 0 || n <= 0) return 0;
	if (n > w->left) n = (int) w->left;
	if (n > 0 && kapi_stream_write (w->out, frames, (unsigned) (n * 2 * w->channels)) < 0) w->bad = 1;
	w->left -= n;
	return n;
}
// The file closed (what was announced and not written: silence) -> 0 / -1.
extern "C" int ak_wav_end (ak_wav *w)
{
	if (w == 0) return -1;
	static const short zero[2 * 256] = { 0 };
	while (w->left > 0)
	{
		int n = w->left > 256 ? 256 : (int) w->left;
		if (kapi_stream_write (w->out, zero, (unsigned) (n * 2 * w->channels)) < 0) w->bad = 1;
		w->left -= n;
	}
	int bad = w->bad;
	kapi_stream_close (w->out);
	free (w);
	return bad ? -1 : 0;
}
