// resample_test.cpp -- the sound output's rate converter (kernel/sys/sound_resample.h: the producer's
// 44.1 kHz frames pulled at 48 kHz by the USB and HDMI outputs, kapi v84) on the PC.
// Run by tools/tests/run_sound_resample_test.sh. MIT (as Onyx).
#include "../../../kernel/sys/sound_resample.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int s_nChecks, s_nFailed;
#define CHECK(c) do { s_nChecks++; if (!(c)) { s_nFailed++; printf ("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static long s_nSrc;				// source frames handed out
static int s_nMode;				// what the source is
static unsigned s_nChunk = 1024;		// the source's chunk (the producer's: 64 .. 1024)

static unsigned More (short *pBuf, unsigned nMax)
{
	unsigned n = s_nChunk < nMax ? s_nChunk : nMax;
	for (unsigned i = 0; i < n; i++, s_nSrc++)
	{
		int l = 0, r = 0;
		switch (s_nMode)
		{
		case 0: l = 1234; r = -4321; break;					// constants
		case 1: l = (int) (s_nSrc % 30000); r = -l; break;			// a ramp
		case 2: l = (s_nSrc & 1) ? 32767 : -32768; r = -l - 1; break;		// the extremes, alternating
		case 3: l = r = (int) lrint (20000.0 * sin (2 * M_PI * 1000.0 * s_nSrc / 44100.0)); break;	// 1 kHz
		case 4: return 0;							// nothing: silence
		}
		pBuf[i * 2] = (short) l; pBuf[i * 2 + 1] = (short) r;
	}
	return n;
}

int main (void)
{
	static short Out[48000 * 2];
	TSndResampler R;

	// the rates: one second of output takes one second of source
	for (unsigned nChunk = 64; nChunk <= 1024; nChunk *= 4)
	{
		s_nChunk = nChunk; s_nMode = 0; s_nSrc = 0;
		SndResampleInit (R, 44100, 48000);
		for (int k = 0; k < 10; k++) SndResample (R, Out, 48000, More);		// ten seconds, no drift
		long nUsed = s_nSrc - (R.nLen - R.nPos);
		CHECK (labs (nUsed - 441000) <= 2);
		// a constant stays what it is (after the first frames: it starts from silence)
		bool bConst = true;
		for (int i = 100; i < 48000; i++) if (Out[i * 2] != 1234 || Out[i * 2 + 1] != -4321) bConst = false;
		CHECK (bConst);
	}

	// a ramp: every output frame between its two neighbours, left and right opposite
	s_nChunk = 441; s_nMode = 1; s_nSrc = 0;
	SndResampleInit (R, 44100, 48000);
	SndResample (R, Out, 20000, More);
	bool bMono = true, bOpp = true;
	for (int i = 10; i < 20000; i++)
	{
		if (Out[i * 2] < Out[(i - 1) * 2]) bMono = false;
		if (abs (Out[i * 2] + Out[i * 2 + 1]) > 1) bOpp = false;
	}
	CHECK (bMono); CHECK (bOpp);

	// the extremes: no overflow (every value inside the two it lies between)
	s_nMode = 2; s_nSrc = 0;
	SndResampleInit (R, 44100, 48000);
	SndResample (R, Out, 48000, More);
	bool bIn = true;			// left and right stay opposite: an overflow would break it
	for (int i = 10; i < 48000; i++) if (abs (Out[i * 2] + Out[i * 2 + 1] + 1) > 2) bIn = false;
	CHECK (bIn);
	long nBig = 0;
	for (int i = 0; i < 48000; i++) if (abs (Out[i * 2]) > 30000) nBig++;
	CHECK (nBig > 1000);				// (the swings are there: not flattened)

	// a 1 kHz sine keeps its level and its frequency (zero crossings: 2000 a second)
	s_nMode = 3; s_nSrc = 0;
	SndResampleInit (R, 44100, 48000);
	SndResample (R, Out, 48000, More);
	int nPeak = 0, nCross = 0;
	for (int i = 100; i < 48000; i++)
	{
		if (abs (Out[i * 2]) > nPeak) nPeak = abs (Out[i * 2]);
		if ((Out[i * 2] >= 0) != (Out[(i - 1) * 2] >= 0)) nCross++;
	}
	CHECK (nPeak > 19500 && nPeak <= 20000);
	CHECK (abs (nCross - 1996) <= 4);

	// the same rate: the source as it is, two frames late (the two frames it interpolates between)
	s_nMode = 1; s_nSrc = 0;
	SndResampleInit (R, 44100, 44100);
	SndResample (R, Out, 1000, More);
	bool bSame = true;
	for (int i = 2; i < 1000; i++) if (Out[i * 2] != i - 2) bSame = false;
	CHECK (bSame);

	// a source with nothing: silence, no loop
	s_nMode = 4;
	SndResampleInit (R, 44100, 48000);
	SndResample (R, Out, 1000, More);
	bool bZero = true;
	for (int i = 0; i < 2000; i++) if (Out[i] != 0) bZero = false;
	CHECK (bZero);

	printf ("resample: %d checks, %d failed\n", s_nChecks, s_nFailed);
	return s_nFailed == 0 ? 0 : 1;
}
