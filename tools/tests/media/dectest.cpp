//
// tools/tests/media/dectest.cpp -- Media Player's decoders on the PC (the simulator's kapi): each file
// given decoded whole through decode.h's Stream (at SOUND_RATE), its length, level and a seek checked.
//   sh tools/tests/run_media_test.sh
//
#include <stdio.h>
#include <math.h>
#include "Apps/media/decode.h"
using namespace media;

int main (int argc, char **argv)
{
	int bad = 0;
	for (int i = 1; i < argc; i++)
	{
		char err[160];
		Decoder *d = decoder_open (argv[i], err, sizeof err);
		if (!d) { printf ("%-24s FAIL: %s\n", argv[i], err); bad++; continue; }
		Stream s (d);
		static short buf[2 * 4096];
		long long n = 0; double sq = 0; int peak = 0;
		for (;;)
		{
			int k = s.read (buf, 4096);
			if (k <= 0) break;
			for (int j = 0; j < 2 * k; j++) { int v = buf[j]; sq += (double) v * v; if (abs (v) > peak) peak = abs (v); }
			n += k;
		}
		double ms = n * 1000.0 / SOUND_RATE, rms = n ? sqrt (sq / (2.0 * n)) : 0;
		// a seek to the middle, then a piece
		s.seekMs ((long long) (ms / 2));
		int k = s.read (buf, 4096);
		printf ("%-24s %-4s %6d Hz %d ch  %8.0f ms (said %lld)  rms %6.0f peak %5d  seek+read %d\n", argv[i], d->format, d->rate, d->channels,
			ms, s.lengthMs (), rms, peak, k);
		if (n == 0 || rms < 100 || k <= 0) bad++;
	}
	printf (bad ? "media: %d FAILED\n" : "media: all good\n", bad);
	return bad != 0;
}
