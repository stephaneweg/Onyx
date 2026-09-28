//
// gc/gc_zelda.cpp -- the audio of Nintendo EAD's "Zelda" DSP microcode (The Wind Waker, Mario
// Sunshine, Pikmin, Mario Kart DD, Twilight Princess...), at a high level: each frame of 0x50
// samples (32 kHz), the voices the game says are ready are mixed. A voice is a VPB in main memory
// (0xC0 words: its state, then read only its parameters): its samples come from ARAM (AFC --
// Nintendo's ADPCM, 16 samples in 9 or 5 bytes --, PCM8, PCM16), from main memory (PCM16), or are
// made (square / saw waves, the patterns the game gave); they are resampled (4 taps, the game's
// coefficients), filtered (low pass, biquad), then added to the mixing buffers with their volume
// ramps (6 destinations, or a Dolby position); the reverbs (4, circular buffers in main memory) are
// fed back. The front left / right buffers go to the game's output buffers; the game plays them
// through the audio DMA. The voices' state (positions, loops, "done") is written back as the
// microcode does: the games wait for it (a stream's end, a sound's).
// (After Dolphin's DSP HLE, which worked the microcode's formats out.)
//
#include "gc/gc.h"

namespace gc {

// a VPB's words
enum
{
	V_ENABLED = 0x00, V_DONE = 0x01, V_RATIO = 0x02, V_RESET = 0x04, V_END_REACHED = 0x05, V_CONST = 0x06,
	V_CHANNELS = 0x08,						// (6 of { id, target volume, current volume, - })
	V_DOLBY_POS = 0x28, V_DOLBY_REVERB = 0x29, V_DOLBY_CUR = 0x2A, V_DOLBY_TARGET = 0x2B, V_USE_DOLBY = 0x2C,
	V_POS_FRAC = 0x30, V_AFC_LEFT = 0x32, V_CONST_SAMPLE = 0x33, V_CUR_POS = 0x34, V_BEFORE_LOOP = 0x36,
	V_ARAM_ADDR = 0x38, V_REMAINING = 0x3A, V_RESAMPLE_BUF = 0x3C, V_BIQUAD_X1 = 0x54, V_BIQUAD_X2 = 0x55,
	V_BIQUAD_Y1 = 0x56, V_BIQUAD_Y2 = 0x57, V_AFC_SAMPLES = 0x58, V_LP_Y1 = 0x68, V_LP_X1 = 0x69,
	V_SRC = 0x80, V_LOOPING = 0x81, V_LOOP_YN1 = 0x82, V_LOOP_YN2 = 0x83, V_FILTERS = 0x84, V_END_REQ = 0x85,
	V_LOOP_ADDR = 0x88, V_LOOP_START = 0x8A, V_BASE = 0x8C, V_BIQUAD_B1 = 0xA4, V_BIQUAD_B2 = 0xA5,
	V_BIQUAD_A1 = 0xA6, V_BIQUAD_A2 = 0xA7, V_LP_COEFF = 0xA8
};
// the sample sources
enum { SRC_SQUARE = 0, SRC_SAW = 1, SRC_SQUARE25 = 3, SRC_PATTERN1 = 4, SRC_AFC_LQ = 5, SRC_PATTERN0 = 7, SRC_PCM8 = 8,
       SRC_AFC_HQ = 9, SRC_PATTERN0_VAR = 10, SRC_PATTERN2 = 11, SRC_PATTERN3 = 12, SRC_PCM16 = 16, SRC_PCM16_MRAM = 33 };

static inline s16 clamp16 (s64 v) { return (s16) (v < -0x8000 ? -0x8000 : v > 0x7FFF ? 0x7FFF : v); }
static inline u32 g32 (const u16 *v, int i) { return (u32) v[i] << 16 | v[i + 1]; }
static inline void s32w (u16 *v, int i, u32 x) { v[i] = (u16) (x >> 16); v[i + 1] = (u16) x; }
static inline u16 be16 (const u8 *p) { return (u16) (p[0] << 8 | p[1]); }

// the mixing helpers: a volume in 1.15 / 4.12 applied in place; a buffer added with a volume
// ramping from vol by step each sample (16.16) -> its end; a buffer added with a 1.15 volume
static void volumeInPlace (s16 *b, int n, u16 vol, int intBits)
{
	for (int i = 0; i < n; i++)
	{
		s32 t = (s32) ((u32) (s32) b[i] * (u32) vol);
		b[i] = clamp16 (t >> (16 - intBits));
	}
}
static s32 addRamp (s16 *dst, const s16 *src, s32 vol, s32 step)
{
	if (!vol && !step) return vol;
	for (int i = 0; i < 0x50; i++) { dst[i] = (s16) (dst[i] + (((vol >> 16) * src[i]) >> 16)); vol += step; }
	return vol;
}
static void addVolume (s16 *dst, const s16 *src, int n, u16 vol)
{
	for (int i = 0; i < n; i++) dst[i] = (s16) (dst[i] + clamp16 (((s32) src[i] * (s32) vol) >> 15));
}

// the buffer a VPB / a reverb names (the address of a buffer in the DSP's memory)
static s16 *zBuffer (ZeldaMix &z, u16 id)
{
	switch (id)
	{
	case 0x0D00: return z.fl;
	case 0x0D60: return z.fr;
	case 0x0F40: return z.bl;
	case 0x0CA0: return z.br;
	case 0x0E80: return z.flR;
	case 0x0EE0: return z.frR;
	case 0x0C00: return z.blR;
	case 0x0C50: return z.brR;
	case 0x0DC0: return z.u0R;
	case 0x0E20: return z.u1R;
	case 0x09A0: return z.u0;
	case 0x0FA0: return z.u1;
	case 0x0B00: return z.u2;
	}
	return 0;
}

// the 4 reverbs (RPBs of 16 words in main memory): before the voices, each one's circular buffer
// (the frame of two frames ago...) filtered, added to its destinations; after, the frame stored
static void zReverb (Machine &m, ZeldaMix &z, bool post)
{
	if (!z.reverbBase) return;
	s16 *bufs[4] = { z.u0R, z.u1R, z.flR, z.frR };
	for (int r = 0; r < 4; r++)
	{
		u32 a = (z.reverbBase & 0x01FFFFFF) + (u32) r * 32;
		if (a + 32 > MEM1_SIZE) return;
		u16 w[16];
		for (int i = 0; i < 16; i++) w[i] = be16 (m.mem1 + a + i * 2);
		if (!w[0]) continue;					// (enabled)
		u32 circ = ((u32) w[2] << 16 | w[3]) & 0x01FFFFFF;
		u32 at = circ + (u32) z.reverbFrame[r] * 0xA0;
		if (at + 0xA0 > MEM1_SIZE) continue;
		if (!post)
		{
			s16 b[0x58];
			for (int i = 0; i < 8; i++) b[i] = z.last8[r][i];
			for (int i = 0; i < 0x50; i++) b[8 + i] = (s16) be16 (m.mem1 + at + i * 2);
			for (int i = 0; i < 8; i++) z.last8[r][i] = b[0x50 + i];
			const s16 *co = (const s16 *) (void *) &w[8];
			auto filter = [&] ()
			{
				for (int i = 0; i < 0x50; i++)
				{
					s32 s = 0;
					for (int j = 0; j < 8; j++) s += (s32) b[i + j] * co[j];
					b[i] = clamp16 (s >> 15);
				}
			};
			if (w[0] & 1) filter ();				// (pre-filtered)
			for (int d = 0; d < 2; d++)
			{
				u16 id = w[4 + d * 2], vol = w[5 + d * 2];
				s16 *dst = id ? zBuffer (z, id) : 0;
				if (dst) addVolume (dst, b, 0x50, vol);
			}
			if (w[0] & 2) filter ();				// (post-filtered)
			for (int i = 0; i < 0x50; i++) bufs[r][i] = b[i];
		}
		else
		{
			for (int i = 0; i < 0x50; i++) { m.mem1[at + i * 2] = (u8) ((u16) bufs[r][i] >> 8); m.mem1[at + i * 2 + 1] = (u8) bufs[r][i]; }
			z.reverbFrame[r] = (u16) (w[1] ? (z.reverbFrame[r] + 1) % w[1] : 0);
		}
	}
}

void Machine::zPrepare ()
{
	ZeldaMix &z = dspUc.mix;
	if (z.prepared) return;
	for (int i = 0; i < 0x50; i++) z.fl[i] = z.fr[i] = 0;
	volumeInPlace (z.bl, 0x50, 0x6784, 1);
	volumeInPlace (z.br, 0x50, 0x6784, 1);
	zReverb (*this, z, false);
	addVolume (z.flR, z.blR, 0x50, 0x7FFF);
	addVolume (z.frR, z.blR, 0x50, 0xB820);
	addVolume (z.flR, z.brR + 0x28, 0x28, 0xB820);
	addVolume (z.frR, z.brR + 0x28, 0x28, 0x7FFF);
	for (int i = 0; i < 0x50; i++) z.blR[i] = z.brR[i] = 0;
	// the patterns 2 and 3 move from frame to frame
	s16 *p2 = z.patterns + 2 * 0x40;
	s32 yn2 = p2[0x3E], yn1 = p2[0x3F], v;
	for (int i = 0; i < 0x40; i += 2)
	{
		v = yn2 * yn1 - p2[i] * 65536; yn2 = yn1; yn1 = p2[i]; p2[i] = (s16) (v >> 16);
		v = 2 * (yn2 * yn1 + p2[i + 1] * 65536); yn2 = yn1; yn1 = p2[i + 1]; p2[i + 1] = (s16) (v >> 16);
	}
	s16 *p3 = z.patterns + 3 * 0x40;
	yn2 = p3[0x3E]; yn1 = p3[0x3F];
	s16 acc = (s16) yn1;
	s16 step = (s16) (p3[0] + ((yn1 * yn2 + (yn2 * 65536 + yn1)) >> 16));
	step = (s16) ((step & 0x1FF) | 0x2000);
	for (s32 i = 0; i < 0x40; i++) p3[i] = (s16) (acc + (i + 1) * step);
	z.prepared = true;
}

void Machine::zFinalize ()
{
	ZeldaMix &z = dspUc.mix;
	volumeInPlace (z.fl, 0x50, z.outVolume, 4);
	volumeInPlace (z.fr, 0x50, z.outVolume, 4);
	for (int c = 0; c < 2; c++)
	{
		u32 a = (c ? dspUc.zOutR : dspUc.zOutL) & 0x01FFFFFF;
		const s16 *b = c ? z.fr : z.fl;
		if (a + 0xA0 <= MEM1_SIZE) for (int i = 0; i < 0x50; i++) { mem1[a + i * 2] = (u8) ((u16) b[i] >> 8); mem1[a + i * 2 + 1] = (u8) b[i]; }
	}
	dspUc.zOutL += 0xA0; dspUc.zOutR += 0xA0;
	zReverb (*this, z, true);
	z.prepared = false;
}

// ---- a voice's samples ---------------------------------------------------------------------------------------
struct ZVoice
{
	Machine &m; ZeldaMix &z; u16 *v;
	u8 aramAt (u32 a) { return a < Machine::ARAM_SIZE ? m.aram[a] : 0; }
	u32 needed () { return ((u32) v[V_POS_FRAC] + 0x50u * v[V_RATIO]) >> 12; }

	// PCM8 / PCM16 from ARAM, looping at the loop start (the loop address: where it goes back)
	void pcm (s16 *dst, u32 count, int size)
	{
		if (v[V_DONE]) { for (u32 i = 0; i < count; i++) dst[i] = 0; return; }
		if (v[V_RESET])
		{
			s32w (v, V_REMAINING, g32 (v, V_LOOP_START) - g32 (v, V_CUR_POS));
			s32w (v, V_ARAM_ADDR, g32 (v, V_BASE) + g32 (v, V_CUR_POS) * (u32) size);
		}
		v[V_END_REACHED] = 0;
		for (int guard = 0; count && guard < 64; guard++)
		{
			if (v[V_END_REACHED])
			{
				v[V_END_REACHED] = 0;
				if (!v[V_LOOPING]) { for (u32 i = 0; i < count; i++) dst[i] = 0; v[V_DONE] = 1; return; }
				s32w (v, V_CUR_POS, g32 (v, V_LOOP_ADDR));
				s32w (v, V_REMAINING, g32 (v, V_LOOP_START) - g32 (v, V_CUR_POS));
				s32w (v, V_ARAM_ADDR, g32 (v, V_BASE) + g32 (v, V_CUR_POS) * (u32) size);
			}
			u32 rem = g32 (v, V_REMAINING), n = rem < count ? rem : count, a = g32 (v, V_ARAM_ADDR);
			for (u32 i = 0; i < n; i++, a += (u32) size)
				*dst++ = size == 1 ? (s16) ((s8) aramAt (a) << 8) : (s16) (aramAt (a) << 8 | aramAt (a + 1));
			s32w (v, V_REMAINING, rem - n); s32w (v, V_ARAM_ADDR, a);
			count -= n;
			if (rem == n) v[V_END_REACHED] = 1;
		}
		for (u32 i = 0; i < count; i++) dst[i] = 0;		// (a loop of nothing)
	}

	// AFC: blocks of 16 samples, a byte of scale / coefficients index, then 8 bytes of 4-bit
	// samples (HQ) or 4 of 2-bit ones (LQ); two previous samples predict the next
	void afcDecode (s16 *dst, u32 blocks)
	{
		u32 a = g32 (v, V_ARAM_ADDR), bsz = v[V_SRC];			// (the source's number: its bytes a block)
		s32w (v, V_ARAM_ADDR, a + blocks * bsz);
		s32 yn1 = (s16) v[V_AFC_SAMPLES + 15], yn2 = (s16) v[V_AFC_SAMPLES + 14];
		for (u32 b = 0; b < blocks; b++, a += bsz)
		{
			u8 h = aramAt (a);
			s32 delta = 1 << ((h >> 4) & 15);
			int idx = h & 15;
			s16 nib[16];
			if (bsz == SRC_AFC_HQ)
				for (int i = 0; i < 16; i += 2) { u8 x = aramAt (a + 1 + (u32) i / 2); nib[i] = (s16) ((s16) (x >> 4 << 12) >> 1); nib[i + 1] = (s16) ((s16) ((x & 15) << 12) >> 1); }
			else
				for (int i = 0; i < 16; i += 4)
				{
					u8 x = aramAt (a + 1 + (u32) i / 4);
					for (int k = 0; k < 4; k++) nib[i + k] = (s16) ((s16) (((x >> (6 - 2 * k)) & 3) << 14) >> 1);
				}
			for (int i = 0; i < 16; i++)
			{
				s32 s = (delta * nib[i] + yn1 * z.afc[idx * 2] + yn2 * z.afc[idx * 2 + 1]) >> 11;
				s = s < -0x8000 ? -0x8000 : s > 0x7FFF ? 0x7FFF : s;
				*dst++ = (s16) s;
				yn2 = yn1; yn1 = s;
			}
		}
		v[V_AFC_SAMPLES + 14] = (u16) yn2; v[V_AFC_SAMPLES + 15] = (u16) yn1;
	}
	void afc (s16 *dst, u32 count)
	{
		if (v[V_RESET])
		{
			v[V_AFC_SAMPLES + 14] = v[V_AFC_SAMPLES + 15] = 0;
			v[V_AFC_LEFT] = 0;
			s32w (v, V_REMAINING, g32 (v, V_LOOP_START));
			s32w (v, V_ARAM_ADDR, g32 (v, V_BASE));
		}
		if (v[V_DONE]) { for (u32 i = 0; i < count; i++) dst[i] = 0; return; }
		if (v[V_AFC_LEFT] > 16) v[V_AFC_LEFT] = 16;
		for (int guard = 0; guard < 64; guard++)
		{
			// the samples decoded and kept from the last block first
			u32 left = v[V_AFC_LEFT], out = left < count ? left : count;
			const u16 *base = &v[V_AFC_SAMPLES + 16 - left];
			for (u32 i = 0; i < out; i++) *dst++ = (s16) base[i];
			v[V_AFC_LEFT] = (u16) (left - out);
			count -= out;
			if (!count) return;
			u32 rem = g32 (v, V_REMAINING);
			if (count <= rem)
			{
				u32 blocks = (count + 15) >> 4, decoded = blocks << 4;
				if (decoded < rem) { v[V_AFC_LEFT] = (u16) (decoded - count); s32w (v, V_REMAINING, rem - decoded); }
				else { v[V_AFC_LEFT] = (u16) (rem - count); s32w (v, V_REMAINING, 0); }
				afcDecode (dst, blocks);
				if (v[V_AFC_LEFT])
				{
					for (u32 i = 0; i < 16; i++) v[V_AFC_SAMPLES + i] = (u16) dst[decoded - 16 + i];
					if (!g32 (v, V_REMAINING) && g32 (v, V_LOOP_START))
					{
						// (the samples kept: from the loop's next pass)
						int b = (int) ((g32 (v, V_LOOP_START) + 15) & 15);
						s16 tmp[16]; for (int i = 0; i < 16; i++) tmp[i] = (s16) v[V_AFC_SAMPLES + i];
						for (u32 i = 0; i < v[V_AFC_LEFT]; i++) v[V_AFC_SAMPLES + 16 - i - 1] = (u16) tmp[b - (int) i >= 0 ? b - (int) i : 0];
					}
				}
				return;
			}
			if (rem)							// (what is left, then the end or the loop)
			{
				count -= rem;
				afcDecode (dst, (rem + 15) >> 4);
				dst += rem;
			}
			if (!v[V_LOOPING])
			{
				v[V_DONE] = 1;
				for (u32 i = 0; i < count; i++) *dst++ = 0;
				return;
			}
			u32 loopBytes = (g32 (v, V_LOOP_ADDR) >> 4) * v[V_SRC];
			s32w (v, V_ARAM_ADDR, g32 (v, V_BASE) + loopBytes);
			v[V_AFC_SAMPLES + 14] = v[V_LOOP_YN2]; v[V_AFC_SAMPLES + 15] = v[V_LOOP_YN1];
			s16 blk[16];
			afcDecode (blk, 1);
			for (int i = 0; i < 16; i++) v[V_AFC_SAMPLES + i] = (u16) blk[i];
			v[V_AFC_LEFT] = (u16) (16 - (g32 (v, V_LOOP_ADDR) & 15));
			s32w (v, V_REMAINING, g32 (v, V_LOOP_START) - v[V_AFC_LEFT] - g32 (v, V_LOOP_ADDR));
		}
		for (u32 i = 0; i < count; i++) *dst++ = 0;
	}

	// PCM16 from main memory (a buffer the game fills), its loop in main memory too
	void mram (s16 *dst, u32 count)
	{
		u32 addr = (g32 (v, V_BASE) & 0x01FFFFFF) + v[V_CUR_POS] * 2u, rem = g32 (v, V_REMAINING);
		auto rd = [&] (u32 a) -> s16 { a &= 0x01FFFFFF; return a + 1 < MEM1_SIZE ? (s16) be16 (m.mem1 + a) : 0; };
		if (count > rem)
		{
			s16 last = 0;
			for (u32 i = 0; i < rem; i++) last = dst[i] = rd (addr + i * 2);
			for (u32 i = rem; i < count; i++) dst[i] = last;
			v[V_CUR_POS] = (u16) (v[V_CUR_POS] + rem);
			s32w (v, V_REMAINING, 0);
			v[V_DONE] = 1;
			return;
		}
		s32w (v, V_REMAINING, rem - count);
		v[V_BEFORE_LOOP] = (u16) (v[V_LOOP_START] - v[V_CUR_POS]);
		if (count <= v[V_BEFORE_LOOP])
		{
			for (u32 i = 0; i < count; i++) dst[i] = rd (addr + i * 2);
			v[V_CUR_POS] = (u16) (v[V_CUR_POS] + count);
		}
		else
		{
			u32 before = v[V_BEFORE_LOOP];
			for (u32 i = 0; i < before; i++) dst[i] = rd (addr + i * 2);
			u32 loop = g32 (v, V_LOOP_ADDR);
			s32w (v, V_BASE, loop);
			v[V_CUR_POS] = (u16) (count - before);
			for (u32 i = 0; i < v[V_CUR_POS]; i++) dst[before + i] = rd (loop + i * 2);
		}
	}

	// the raw samples (4 kept from the last frame first) resampled to 0x50: 4 taps (the game's
	// coefficients, by the position's fraction), or the nearest one above 4:1
	void resample (const s16 *src, s16 *out)
	{
		u32 ratio = v[V_RATIO], pos = v[V_POS_FRAC];
		if ((ratio >> 12) >= 4)
			for (int i = 0; i < 0x50; i++) { pos += ratio; out[i] = src[pos >> 12]; }
		else
			for (int i = 0; i < 0x50; i++)
			{
				const s16 *c = &z.resample[((pos & 0xFFF) >> 6) * 4], *in = &src[pos >> 12];
				s64 acc = 0;
				for (int k = 0; k < 4; k++) acc += (s64) 2 * c[k] * in[k];
				out[i] = clamp16 (acc >> 16);
				pos += ratio;
			}
		for (int k = 0; k < 4; k++) v[V_RESAMPLE_BUF + k] = (u16) src[(pos >> 12) + (u32) k];
		v[V_CONST_SAMPLE] = (u16) out[0x4F];
		v[V_POS_FRAC] = (u16) (pos & 0xFFF);
	}

	void load (s16 *out)
	{
		static s16 raw[4 + 0x500 + 0x20];
		for (int k = 0; k < 4; k++) raw[k] = (s16) v[V_RESAMPLE_BUF + k];
		if (v[V_CONST]) { for (int i = 0; i < 0x50; i++) out[i] = (s16) v[V_CONST_SAMPLE]; return; }
		u32 n = needed ();
		if (n > 0x510) n = 0x510;
		switch (v[V_SRC])
		{
		case SRC_SQUARE: case SRC_SQUARE25:
		{
			u32 shift = v[V_SRC] == SRC_SQUARE ? 1 : 2, mk = (1u << shift) - 1, ratio = (u32) v[V_RATIO] << (shift - 1);
			u32 pos = (u32) v[V_POS_FRAC] << shift;
			for (int i = 0; i < 0x50; i++) { out[i] = ((pos >> 16) & mk) ? (s16) 0xC000 : 0x4000; pos += ratio; }
			v[V_POS_FRAC] = (u16) ((pos >> shift) & 0xFFFF);
			return;
		}
		case SRC_SAW:
		{
			u32 pos = v[V_POS_FRAC];
			for (int i = 0; i < 0x50; i++) { out[i] = (s16) (pos & 0xFFFF); pos += (u32) v[V_RATIO] >> 1; }
			v[V_POS_FRAC] = (u16) (pos & 0xFFFF);
			return;
		}
		case SRC_PATTERN0: case SRC_PATTERN0_VAR: case SRC_PATTERN1: case SRC_PATTERN2: case SRC_PATTERN3:
		{
			int t = v[V_SRC], idx = t == SRC_PATTERN1 ? 1 : t == SRC_PATTERN2 ? 2 : t == SRC_PATTERN3 ? 3 : 0;
			const s16 *pat = z.patterns + idx * 0x40;
			u32 pos = (u32) v[V_POS_FRAC] << 6, step = (u32) v[V_RATIO] << 5;
			for (int i = 0; i < 0x50; i++)
			{
				out[i] = pat[(pos >> 16) & 0x3F];
				pos = (pos + step) % (0x40u << 16);
				if (t == SRC_PATTERN0_VAR) pos = (u32) (((s64) pos * 1024 + (s64) z.br[i] * v[V_RATIO]) >> 10);
			}
			v[V_POS_FRAC] = (u16) (pos >> 6);
			return;
		}
		case SRC_PCM8: pcm (raw + 4, n, 1); break;
		case SRC_PCM16: pcm (raw + 4, n, 2); break;
		case SRC_AFC_HQ: case SRC_AFC_LQ: afc (raw + 4, n); break;
		case SRC_PCM16_MRAM: mram (raw + 4, n); break;
		default: for (int i = 0; i < 0x50; i++) out[i] = 0; return;
		}
		resample (raw, out);
	}
};

// ---- a voice: its samples, its filters, mixed with its volumes ---------------------------------------------------
void Machine::zAddVoice (u32 id)
{
	ZeldaMix &z = dspUc.mix;
	u32 flags = dspUc.flags;
	bool tiny = flags & Z_TINY_VPB;
	u32 size = tiny ? 0x100 : 0x180, a = (z.vpbBase & 0x01FFFFFF) + id * size;
	if (a + size > MEM1_SIZE) return;
	u16 v[0xC0];
	for (u32 i = 0; i < 0xC0; i++) v[i] = i * 2 < size ? be16 (mem1 + a + i * 2) : 0;
	if (tiny)							// (0x80-word VPBs: their parts moved)
	{
		for (int i = 0; i < 0x40; i++) { v[0x80 + i] = v[0x40 + i]; v[0x40 + i] = 0; }
		for (int i = 0; i < 0x10; i++) { v[0x58 + i] = v[0x30 + i]; v[0x30 + i] = 0; }
		for (int i = 0; i < 0x18; i++) { v[0x30 + i] = v[0x18 + i]; v[0x18 + i] = 0; }
	}
	if (!v[V_ENABLED] || v[V_DONE]) return;
	ZVoice zv = { *this, z, v };
	s16 in[0x50];
	zv.load (in);
	if (v[V_LP_COEFF])						// a low pass
	{
		s32 yn1 = v[V_RESET] ? 0 : (s16) v[V_LP_Y1], xn1 = v[V_RESET] ? 0 : (s16) v[V_LP_X1], co = v[V_LP_COEFF];
		for (int i = 0; i < 0x50; i++)
		{
			s32 xn0 = in[i];
			s16 yn0 = clamp16 ((((s64) (xn0 - xn1) * co) >> 7) + yn1);
			in[i] = yn0; yn1 = yn0; xn1 = xn0;
		}
		v[V_LP_Y1] = (u16) yn1; v[V_LP_X1] = (u16) xn1;
	}
	s16 b1 = (s16) v[V_BIQUAD_B1], b2 = (s16) v[V_BIQUAD_B2], a1 = (s16) v[V_BIQUAD_A1], a2 = (s16) v[V_BIQUAD_A2];
	if ((v[V_FILTERS] & 0x20) && (a2 || a1 || b2 || b1 != 0x7FFF))	// a biquad
	{
		s32 x1 = (s16) v[V_BIQUAD_X1], x2 = (s16) v[V_BIQUAD_X2], y1 = (s16) v[V_BIQUAD_Y1], y2 = (s16) v[V_BIQUAD_Y2];
		for (int i = 0; i < 0x50; i++)
		{
			s32 x0 = in[i];
			s16 y0 = clamp16 (((s64) b1 * x1 + (s64) b2 * x2 + (s64) a1 * y1 + (s64) a2 * y2) >> 15);
			in[i] = y0; x2 = x1; x1 = x0; y2 = y1; y1 = y0;
		}
		v[V_BIQUAD_X1] = (u16) x1; v[V_BIQUAD_X2] = (u16) x2; v[V_BIQUAD_Y1] = (u16) y1; v[V_BIQUAD_Y2] = (u16) y2;
	}
	if (v[V_USE_DOLBY])						// a position (X left / right, Y front / back)
	{
		if (v[V_END_REQ]) { v[V_DOLBY_TARGET] = (u16) ((s16) v[V_DOLBY_CUR] / 2); if (!v[V_DOLBY_TARGET]) v[V_DONE] = 1; }
		int x = (v[V_DOLBY_POS] >> 8) & 0x7F, y = v[V_DOLBY_POS] & 0x7F;
		s32 right = z.sine[x], back = z.sine[y], left = z.sine[x ^ 0x7F], front = z.sine[y ^ 0x7F];
		int sh = (flags & Z_LOUDER) ? 15 : 16;
		s16 q[4] = { (s16) ((left * front) >> sh), (s16) ((left * back) >> sh), (s16) ((right * front) >> sh), (s16) ((right * back) >> sh) };
		s16 delta = (s16) ((s16) v[V_DOLBY_TARGET] - (s16) v[V_DOLBY_CUR]);
		s16 qd[4], rv[4], rd[4];
		for (int i = 0; i < 4; i++) qd[i] = (s16) (((u16) q[i] * delta) >> sh);
		for (int i = 0; i < 4; i++) q[i] = (s16) ((q[i] * (s16) v[V_DOLBY_CUR]) >> sh);
		for (int i = 0; i < 4; i++) { rv[i] = (s16) ((q[i] * (s16) v[V_DOLBY_REVERB]) >> sh); rd[i] = (s16) ((qd[i] * (s16) v[V_DOLBY_REVERB]) >> sh); }
		s16 *dst[8] = { z.fl, z.bl, z.fr, z.br, z.flR, z.blR, z.frR, z.brR };
		for (int i = 0; i < 8; i++)
		{
			s16 vol = i < 4 ? q[i] : rv[i - 4], dv = i < 4 ? qd[i] : rd[i - 4];
			addRamp (dst[i], in, vol * 65536, (dv * 65536) / 0x50);
		}
		v[V_DOLBY_CUR] = v[V_DOLBY_TARGET];
	}
	else								// up to 6 destinations, each its volume ramp
	{
		int n = (flags & Z_FOUR_DESTS) ? 4 : 6;
		if (v[V_END_REQ])
		{
			bool mute = true;
			for (int i = 0; i < n; i++)
			{
				u16 *ch = &v[V_CHANNELS + i * 4];
				ch[1] = (u16) ((s16) ch[2] / 2);
				mute = mute && ch[1] == 0;
			}
			if (mute) v[V_DONE] = 1;
		}
		for (int i = 0; i < n; i++)
		{
			u16 *ch = &v[V_CHANNELS + i * 4];
			if (!ch[0]) continue;
			s16 delta = (flags & Z_VOLUME_STEP) ? (s16) ch[1] : (s16) ((s16) ch[1] - (s16) ch[2]);
			s32 step = (delta * 65536) / 0x50;
			if (!ch[2] && !step) continue;
			s16 *dst = zBuffer (z, ch[0]);
			if (!dst) continue;
			s32 nv = addRamp (dst, in, (s16) ch[2] * 65536, step);
			ch[2] = (u16) (nv >> 16);
		}
	}
	if (!v[V_CONST]) v[V_RESET] = 0;
	// its state back (the first 0x80 words; the rest is its parameters)
	if (tiny)
	{
		for (int i = 0; i < 0x18; i++) { v[0x18 + i] = v[0x30 + i]; v[0x30 + i] = 0; }
		for (int i = 0; i < 0x10; i++) { v[0x30 + i] = v[0x58 + i]; v[0x58 + i] = 0; }
	}
	u32 back = tiny ? 0x40 : 0x80;
	for (u32 i = 0; i < back; i++) { mem1[a + i * 2] = (u8) (v[i] >> 8); mem1[a + i * 2 + 1] = (u8) v[i]; }
}

} // namespace gc
