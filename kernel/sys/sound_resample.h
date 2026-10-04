//
// sound_resample.h -- the output's rate converter (sys/sound.cpp, COnyxSoundDevice): the producer
// renders at SND_RATE (44.1 kHz); an output that runs at another rate (USB, HDMI: 48 kHz) pulls its
// frames through this. Linear interpolation between two source frames, integer only (no FP in the
// kernel), exact in the long run (the position is a fraction nFrac / nOutRate: no drift). The source
// is asked for more whenever its buffer is used up (pMore: the producer's next chunk).
// Included by sound.cpp; compiled on the PC by tools/tests/sound/resample_test.cpp.
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
#ifndef _sys_sound_resample_h
#define _sys_sound_resample_h

#define SND_RS_BUF	1024			// source frames held (a producer's biggest chunk)

// The source's next frames into pBuf (stereo, up to nMaxFrames) -> how many (0: none: silence).
typedef unsigned (*TSndMore) (short *pBuf, unsigned nMaxFrames);

struct TSndResampler
{
	unsigned nInRate, nOutRate;
	unsigned nFrac;				// the output's place between the two frames: nFrac / nOutRate
	int	 pl, pr, cl, cr;		// the source frames around it: previous, current
	short	 Buf[SND_RS_BUF * 2];
	unsigned nLen, nPos;
};

static inline void SndResampleInit (TSndResampler &r, unsigned nInRate, unsigned nOutRate)
{
	r.nInRate = nInRate; r.nOutRate = nOutRate;
	r.nFrac = 0; r.pl = r.pr = r.cl = r.cr = 0;
	r.nLen = r.nPos = 0;
}

static inline void SndResampleStep (TSndResampler &r, TSndMore pMore)
{
	r.pl = r.cl; r.pr = r.cr;
	if (r.nPos == r.nLen)
	{
		r.nLen = pMore (r.Buf, SND_RS_BUF);
		r.nPos = 0;
		if (r.nLen == 0) { r.cl = r.cr = 0; return; }
	}
	r.cl = r.Buf[r.nPos * 2]; r.cr = r.Buf[r.nPos * 2 + 1];
	r.nPos++;
}

// nFrames output frames (stereo) at nOutRate.
static inline void SndResample (TSndResampler &r, short *pOut, unsigned nFrames, TSndMore pMore)
{
	for (unsigned i = 0; i < nFrames; i++)
	{
		pOut[i * 2]     = (short) (r.pl + (int) ((long long) (r.cl - r.pl) * (int) r.nFrac / (int) r.nOutRate));
		pOut[i * 2 + 1] = (short) (r.pr + (int) ((long long) (r.cr - r.pr) * (int) r.nFrac / (int) r.nOutRate));
		r.nFrac += r.nInRate;
		while (r.nFrac >= r.nOutRate) { r.nFrac -= r.nOutRate; SndResampleStep (r, pMore); }
	}
}

#endif
