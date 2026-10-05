//
// n64/n64_audio.cpp -- the N64's sound: the audio tasks of the RSP, done at a high level (what the
// microcode computes from its command list, not its instructions), and the AI's output (the
// samples each AI DMA plays, resampled to the host's rate for the app).
//
//   * The audio microcode is recognised by its data (the task's ucode_data, as the HLE plug-ins
//     of the PC emulators do): the "nead" ABI of Zelda Ocarina of Time / Majora's Mask (its
//     commands are those of the OoT decompilation's abi.h). Another audio microcode: the task is
//     acknowledged without sound (the game runs, silent).
//   * The commands work on a 4 KB DMEM image (big-endian 16-bit samples): load / save from
//     RDRAM, VADPCM decoding (the 2 x 8 prediction of the codebook), resampling (a 4-tap
//     interpolation, 64 phases), the envelope mixer (dry / wet, left / right, volumes ramped
//     every 8 samples), mixing with a gain, interleaving the two channels for the AI.
//   * Written for Onyx; what each command computes follows the behaviour described by the
//     open-source HLE audio of mupen64plus-rsp-hle (its alist / "nead" code, GPL-2) and the OoT
//     decompilation's abi.h (the command encodings) -- not copied; the resampler's filter is
//     our own (Catmull-Rom), not the microcode's table.
//   * The AI plays 16-bit stereo big-endian frames at VI_CLOCK / (DACRATE + 1) Hz; each DMA's
//     frames are resampled (linearly) into a ring that audioRead () empties.
//
#include "n64/n64.h"

namespace n64 {

enum { VI_CLOCK_NTSC = 48681812, VI_CLOCK_PAL = 49656530 };

static inline s16 clamp16 (s32 v) { return (s16) (v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

// the 4-tap interpolation of the resampler: 64 phases, Catmull-Rom, Q15 (the output lies
// between the 2nd and the 3rd sample)
static s16 g_rsLut[64 * 4];
static bool g_rsLutDone = false;
static void rsLutInit ()
{
	for (int p = 0; p < 64; p++)
	{
		float t = p / 64.0f, t2 = t * t, t3 = t2 * t;
		float w[4] = { (-t3 + 2 * t2 - t) / 2, (3 * t3 - 5 * t2 + 2) / 2, (-3 * t3 + 4 * t2 + t) / 2, (t3 - t2) / 2 };
		for (int k = 0; k < 4; k++)
		{
			int v = (int) (w[k] * 32768.0f + (w[k] < 0 ? -0.5f : 0.5f));
			g_rsLut[p * 4 + k] = (s16) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
	}
	g_rsLutDone = true;
}

// ---- the DMEM image ----
s16 Machine::aS (u32 a) { a &= 0xFFE; return (s16) (aDmem[a] << 8 | aDmem[a + 1]); }
void Machine::aW (u32 a, s16 v) { a &= 0xFFE; aDmem[a] = (u8) ((u16) v >> 8); aDmem[a + 1] = (u8) v; }

void Machine::aLoad (u32 dmem, u32 addr, u32 count)
{
	dmem &= ~3u; addr &= ~7u; count = (count + 7) & ~7u;
	for (u32 i = 0; i < count; i++) aDmem[(dmem + i) & 0xFFF] = rdB (addr + i);
}

void Machine::aSave (u32 dmem, u32 addr, u32 count)
{
	dmem &= ~3u; addr &= ~7u; count = (count + 7) & ~7u;
	for (u32 i = 0; i < count; i++)
	{
		u32 a = (addr + i) & 0x7FFFFF;
		((u8 *) rdram)[a ^ 3] = aDmem[(dmem + i) & 0xFFF];
	}
}

static inline s16 rdS16 (const u32 *rdram, u32 a) { return (s16) *(const u16 *) ((const u8 *) rdram + ((a & 0x7FFFFE) ^ 2)); }
static inline void wrS16 (u32 *rdram, u32 a, s16 v) { *(u16 *) ((u8 *) rdram + ((a & 0x7FFFFE) ^ 2)) = (u16) v; }

// VADPCM: 9-byte frames (4 bits a sample) or 5-byte ones (2 bits), 16 samples each
void Machine::aAdpcm (u32 flags, u32 state)
{
	bool init = flags & 1, loop = flags & 2, twoBit = flags & 4;
	u32 in = aIn, out = aOut, count = (aCount + 31) & ~31u;
	s16 last[16];
	if (init) for (int i = 0; i < 16; i++) last[i] = 0;
	else { u32 src = loop ? aLoop : state; for (int i = 0; i < 16; i++) last[i] = rdS16 (rdram, src + i * 2); }
	for (int i = 0; i < 16; i++, out += 2) aW (out, last[i]);
	while (count)
	{
		u8 code = aDmem[in++ & 0xFFF];
		int scale = code >> 4;
		const s16 *book1 = aBook + ((code & 15) << 4), *book2 = book1 + 8;
		s16 frame[16];
		if (twoBit)
		{
			int rs = scale < 14 ? 14 - scale : 0;
			for (int i = 0; i < 4; i++)
			{
				u8 b = aDmem[in++ & 0xFFF];
				for (int k = 0; k < 4; k++) frame[i * 4 + k] = (s16) ((s16) (u16) ((u16) ((b << (2 * k)) & 0xC0) << 8) >> rs);
			}
		}
		else
		{
			int rs = scale < 12 ? 12 - scale : 0;
			for (int i = 0; i < 8; i++)
			{
				u8 b = aDmem[in++ & 0xFFF];
				frame[i * 2] = (s16) ((s16) (u16) ((u16) (b & 0xF0) << 8) >> rs);
				frame[i * 2 + 1] = (s16) ((s16) (u16) ((u16) ((b << 4) & 0xF0) << 8) >> rs);
			}
		}
		for (int h = 0; h < 2; h++)				// two halves of 8, each predicted from the 2 before it
		{
			s16 l1 = last[h ? 6 : 14], l2 = last[h ? 7 : 15];
			const s16 *src = frame + h * 8;
			for (int i = 0; i < 8; i++)
			{
				s32 acc = (s32) src[i] << 11;
				acc += book1[i] * l1 + book2[i] * l2;
				for (int k = 0; k < i; k++) acc += book2[k] * src[i - 1 - k];
				last[h * 8 + i] = clamp16 (acc >> 11);
			}
		}
		for (int i = 0; i < 16; i++, out += 2) aW (out, last[i]);
		count -= 32;
	}
	for (int i = 0; i < 16; i++) wrS16 (rdram, state + i * 2, last[i]);
}

void Machine::aResample (u32 flags, u32 pitch, u32 state)
{
	if (!g_rsLutDone) rsLutInit ();
	u32 ipos = (aIn >> 1) - 4, opos = aOut >> 1, count = ((aCount + 15) & ~15u) >> 1;
	u32 acc;
	if (flags & 1) { for (int k = 0; k < 4; k++) aW ((ipos + k) * 2, 0); acc = 0; }
	else { for (int k = 0; k < 4; k++) aW ((ipos + k) * 2, rdS16 (rdram, state + k * 2)); acc = (u16) rdS16 (rdram, state + 8); }
	while (count--)
	{
		const s16 *lut = g_rsLut + ((acc >> 10) & 63) * 4;
		s32 v = 0;
		for (int k = 0; k < 4; k++) v += (aS ((ipos + k) * 2) * lut[k]) >> 15;
		aW (opos++ * 2, clamp16 (v));
		acc += pitch;
		ipos += acc >> 16;
		acc &= 0xFFFF;
	}
	for (int k = 0; k < 4; k++) wrS16 (rdram, state + k * 2, aS ((ipos + k) * 2));
	wrS16 (rdram, state + 8, (s16) acc);
}

void Machine::aEnvMix (u32 w0, u32 w1)
{
	u32 in = (w0 >> 12) & 0xFF0, count = ((w0 >> 8) & 0xFF);
	bool swapWet = (w0 >> 4) & 1;
	s16 x2 = (s16) -(s16) ((w0 & 8) >> 1), x3 = (s16) -(s16) ((w0 & 4) >> 1);
	s16 x0 = (s16) -(s16) ((w0 & 2) >> 1), x1 = (s16) -(s16) (w0 & 1);
	u32 dl = (w1 >> 20) & 0xFF0, dr = (w1 >> 12) & 0xFF0, wl = (w1 >> 4) & 0xFF0, wr = (w1 << 4) & 0xFF0;
	if (swapWet) { u32 t = wl; wl = wr; wr = t; }
	count = (count + 7) & ~7u;
	while (count)
	{
		for (int i = 0; i < 8; i++)
		{
			s32 s = aS (in + i * 2);
			s16 l = (s16) (((s32) ((s64) s * aEnvV[0] >> 16)) ^ x0);
			s16 r = (s16) (((s32) ((s64) s * aEnvV[1] >> 16)) ^ x1);
			s16 l2 = (s16) (((s32) ((s64) l * aEnvV[2] >> 16)) ^ x2);
			s16 r2 = (s16) (((s32) ((s64) r * aEnvV[2] >> 16)) ^ x3);
			aW (dl + i * 2, clamp16 (aS (dl + i * 2) + l));
			aW (dr + i * 2, clamp16 (aS (dr + i * 2) + r));
			aW (wl + i * 2, clamp16 (aS (wl + i * 2) + l2));
			aW (wr + i * 2, clamp16 (aS (wr + i * 2) + r2));
		}
		for (int k = 0; k < 3; k++) aEnvV[k] = (u16) (aEnvV[k] + aEnvS[k]);
		in += 16; dl += 16; dr += 16; wl += 16; wr += 16;
		count -= 8;
	}
}

// One audio task: the command list at data_ptr (data_size bytes).
void Machine::audioTask ()
{
	u32 ucodeData = dmem[0xFD8 / 4] & 0x7FFFFF;
	u32 kind = rdW (ucodeData + 0x10);
	if (rdW (ucodeData) != 1 || rdW (ucodeData + 0x30) == 0xF0000F00) { audioUnknown++; return; }
	if (kind != 0x1F681230 && kind != 0x1F801250 && kind != 0x109411F8) { audioUnknown++; return; }	// OoT, MM
	u32 list = dmem[0xFF0 / 4] & 0x7FFFFF, size = dmem[0xFF4 / 4];
	if (size > 0x10000) size = 0x10000;
	for (u32 p = 0; p + 8 <= size; p += 8)
	{
		u32 w0 = rdW (list + p), w1 = rdW (list + p + 4);
		switch (w0 >> 24)
		{
		case 1: aAdpcm ((w0 >> 16) & 0xFF, w1 & 0xFFFFFF); break;			// ADPCM
		case 2:										// CLEARBUFF
		{
			u32 d = w0 & 0xFFFF, n = w1 & 0xFFF;
			n = (n + 15) & ~15u;
			for (u32 i = 0; i < n; i++) aDmem[(d + i) & 0xFFF] = 0;
			break;
		}
		case 4:										// ADDMIXER
		{
			u32 n = (w0 >> 12) & 0xFF0, i0 = w1 >> 16, o = w1 & 0xFFFF;
			for (u32 i = 0; i < n; i += 2) aW (o + i, clamp16 (aS (o + i) + aS (i0 + i)));
			break;
		}
		case 5: aResample ((w0 >> 16) & 0xFF, (w0 & 0xFFFF) << 1, w1 & 0xFFFFFF); break;	// RESAMPLE
		case 6:										// RESAMPLE_ZOH
		{
			u32 pitch = (w0 & 0xFFFF) << 1, acc = w1 & 0xFFFF;
			u32 ip = aIn >> 1, op = aOut >> 1, n = aCount >> 1;
			while (n--) { aW (op++ * 2, aS (ip * 2)); acc += pitch; ip += acc >> 16; acc &= 0xFFFF; }
			break;
		}
		case 7: break;									// FILTER (not done: unfiltered)
		case 8: aIn = w0 & 0xFFFF; aOut = w1 >> 16; aCount = w1 & 0xFFFF; break;	// SETBUFF
		case 9:										// DUPLICATE: 128 bytes, n times
		{
			u32 n = (w0 >> 16) & 0xFF, i0 = w0 & 0xFFFF, o = w1 >> 16;
			u8 t[128];
			for (u32 i = 0; i < 128; i++) t[i] = aDmem[(i0 + i) & 0xFFF];
			while (n--) { for (u32 i = 0; i < 128; i++) aDmem[(o + i) & 0xFFF] = t[i]; o += 128; }
			break;
		}
		case 10:									// DMEMMOVE
		{
			u32 i0 = w0 & 0xFFFF, o = w1 >> 16, n = w1 & 0xFFFF;
			n = (n + 3) & ~3u;
			if (o > i0 && o < i0 + n) for (u32 i = n; i-- > 0; ) aDmem[(o + i) & 0xFFF] = aDmem[(i0 + i) & 0xFFF];
			else for (u32 i = 0; i < n; i++) aDmem[(o + i) & 0xFFF] = aDmem[(i0 + i) & 0xFFF];
			break;
		}
		case 11:									// LOADADPCM: the codebook
		{
			u32 n = (w0 & 0xFFFF) >> 1, a = w1 & 0xFFFFFF;
			if (n > 256) n = 256;
			for (u32 i = 0; i < n; i++) aBook[i] = rdS16 (rdram, a + i * 2);
			break;
		}
		case 12:									// MIXER: out += in × gain
		{
			u32 n = (w0 >> 12) & 0xFF0, i0 = w1 >> 16, o = w1 & 0xFFFF;
			s32 gain = (s16) (w0 & 0xFFFF);
			for (u32 i = 0; i < n; i += 2) aW (o + i, clamp16 (aS (o + i) + ((aS (i0 + i) * gain) >> 15)));
			break;
		}
		case 13:									// INTERLEAVE
		{
			u32 n = ((w0 >> 12) & 0xFF0) >> 1, o = w0 & 0xFFFF, l = w1 >> 16, r = w1 & 0xFFFF;
			s16 t[0x800];
			if (n > 0x400) n = 0x400;
			for (u32 i = 0; i < n; i++) { t[i * 2] = aS (l + i * 2); t[i * 2 + 1] = aS (r + i * 2); }
			for (u32 i = 0; i < n * 2; i++) aW (o + i * 2, t[i]);
			break;
		}
		case 14:									// HILOGAIN: × gain (Q4.4)
		{
			s32 gain = (s8) ((w0 >> 16) & 0xFF);
			u32 n = w0 & 0xFFFF, d = w1 >> 16;
			for (u32 i = 0; i < n; i += 2) aW (d + i, clamp16 ((aS (d + i) * gain) >> 4));
			break;
		}
		case 15: aLoop = w1 & 0xFFFFFF; break;						// SETLOOP
		case 17:									// INTERL: every other sample
		{
			u32 n = w0 & 0xFFFF, i0 = w1 >> 16, o = w1 & 0xFFFF;
			while (n--) { aW (o, aS (i0)); o += 2; i0 += 4; }
			break;
		}
		case 18: aEnvV[2] = (u16) ((w0 >> 8) & 0xFF00); aEnvS[2] = (u16) w0; aEnvS[0] = (u16) (w1 >> 16); aEnvS[1] = (u16) w1; break;	// ENVSETUP1
		case 19: aEnvMix (w0, w1); break;						// ENVMIXER
		case 20: aLoad (w0 & 0xFFFF, w1 & 0xFFFFFF, (w0 >> 12) & 0xFF0); break;		// LOADBUFF
		case 21: aSave (w0 & 0xFFFF, w1 & 0xFFFFFF, (w0 >> 12) & 0xFF0); break;		// SAVEBUFF
		case 22: aEnvV[0] = (u16) (w1 >> 16); aEnvV[1] = (u16) w1; break;			// ENVSETUP2
		default: break;
		}
	}
	audioTasks++;
}

// ---- the AI: a DMA starts playing ----
void Machine::setAudioRate (int hz) { outRate = hz > 0 ? (u32) hz : 48000; }

void Machine::aiOutput (u32 addr, u32 len, u32 rate)
{
	if (!outRate) return;
	u32 n = len / 4;
	for (u32 i = 0; i < n; i++)
	{
		u32 w = rdW (addr + i * 4);
		s32 l = (s16) (w >> 16), r = (s16) w;
		resAcc += outRate;
		while (resAcc >= rate)
		{
			resAcc -= rate;
			s32 f = (s32) (resAcc * 256 / outRate);			// (0..255: the part of the previous sample)
			s32 ol = l + ((lastL - l) * f >> 8), orr = r + ((lastR - r) * f >> 8);
			u32 nh = (aHead + 1) & (ABUF - 1);
			if (nh == aTail) aTail = (aTail + 1) & (ABUF - 1);	// full: drop the oldest
			abuf[aHead * 2] = (s16) ol; abuf[aHead * 2 + 1] = (s16) orr;
			aHead = nh;
		}
		lastL = l; lastR = r;
	}
}

int Machine::audioRead (short *lr, int maxFrames)
{
	int n = 0;
	while (n < maxFrames && aTail != aHead)
	{
		lr[n * 2] = abuf[aTail * 2]; lr[n * 2 + 1] = abuf[aTail * 2 + 1];
		aTail = (aTail + 1) & (ABUF - 1);
		n++;
	}
	return n;
}

u32 Machine::aiRate () const
{
	u32 rate = (pal ? VI_CLOCK_PAL : VI_CLOCK_NTSC) / ((ai[4] & 0x3FFF) + 1);
	return rate < 4000 ? 4000 : rate;
}

} // namespace n64
