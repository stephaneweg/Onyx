//
// gba/gba_bios.cpp -- the BIOS calls (SWI), done in C++: no BIOS image is needed. The
// interrupt entry the BIOS provides at 0x18 is a few ARM words (gba.cpp, BIOS_IRQ).
//
#include "gba/gba.h"

namespace gba {
#ifdef GBA_DEBUG
unsigned g_swiCount[256];
#endif

// sin (2 pi i / 256) in 1.14 fixed point (BgAffineSet / ObjAffineSet)
static const s16 SIN[256] = {
	0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
	6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
	11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
	15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
	16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143, 16069, 15986, 15893, 15791, 15679, 15557, 15426, 15286,
	15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842, 13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866,
	11585, 11297, 11003, 10702, 10394, 10080, 9760, 9434, 9102, 8765, 8423, 8076, 7723, 7366, 7005, 6639,
	6270, 5897, 5520, 5139, 4756, 4370, 3981, 3590, 3196, 2801, 2404, 2006, 1606, 1205, 804, 402,
	0, -402, -804, -1205, -1606, -2006, -2404, -2801, -3196, -3590, -3981, -4370, -4756, -5139, -5520, -5897,
	-6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765, -9102, -9434, -9760, -10080, -10394, -10702, -11003, -11297,
	-11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395, -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
	-15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986, -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
	-16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143, -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
	-15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842, -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
	-11585, -11297, -11003, -10702, -10394, -10080, -9760, -9434, -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639,
	-6270, -5897, -5520, -5139, -4756, -4370, -3981, -3590, -3196, -2801, -2404, -2006, -1606, -1205, -804, -402 };

static s32 arcTan (s32 i)
{
	s32 a = -((i * i) >> 14);
	s32 b = ((0xA9 * a) >> 14) + 0x390;
	b = ((b * a) >> 14) + 0x91C;
	b = ((b * a) >> 14) + 0xFB6;
	b = ((b * a) >> 14) + 0x16AA;
	b = ((b * a) >> 14) + 0x2081;
	b = ((b * a) >> 14) + 0x3651;
	b = ((b * a) >> 14) + 0xA2F9;
	return (i * b) >> 16;
}

static u32 arcTan2 (s32 x, s32 y)
{
	if (!y) return x >= 0 ? 0 : 0x8000;
	if (!x) return y >= 0 ? 0x4000 : 0xC000;
	if (y >= 0)
	{
		if (x >= 0) { if (x >= y) return (u32) arcTan ((y << 14) / x) & 0xFFFF; }
		else if (-x >= y) return (u32) (arcTan ((y << 14) / x) + 0x8000) & 0xFFFF;
		return (u32) (0x4000 - arcTan ((x << 14) / y)) & 0xFFFF;
	}
	if (x <= 0) { if (-x > -y) return (u32) (arcTan ((y << 14) / x) + 0x8000) & 0xFFFF; }
	else if (x >= -y) return (u32) (arcTan ((y << 14) / x) + 0x10000) & 0xFFFF;
	return (u32) (0xC000 - arcTan ((x << 14) / y)) & 0xFFFF;
}

static u32 isqrt (u32 n)
{
	u32 res = 0, bit = 1u << 30;
	while (bit > n) bit >>= 2;
	while (bit) { if (n >= res + bit) { n -= res + bit; res = (res >> 1) + bit; } else res >>= 1; bit >>= 2; }
	return res;
}

// 2^(-f / 3072) in 16.16 for f in 0..3071 (MidiKey2Freq), from 2^(-k/64) with a straight line between
static u32 pow2neg (u32 f)
{
	static const u32 T[65] = {
		65536, 64830, 64132, 63441, 62757, 62081, 61413, 60751, 60097, 59449, 58809, 58176, 57549, 56929, 56316, 55709,
		55109, 54515, 53928, 53347, 52773, 52204, 51642, 51085, 50535, 49991, 49452, 48920, 48393, 47871, 47356, 46846,
		46341, 45842, 45348, 44859, 44376, 43898, 43425, 42958, 42495, 42037, 41584, 41136, 40693, 40255, 39821, 39392,
		38968, 38548, 38133, 37722, 37316, 36914, 36516, 36123, 35734, 35349, 34968, 34591, 34219, 33850, 33486, 33125,
		32768 };
	u32 x = f * 64, k = x / 3072, fr = x % 3072;
	return T[k] - (T[k] - T[k + 1]) * fr / 3072;
}

void Machine::swi (u32 n)
{
	cyc += 3;
	biosLatch = 0xE3A02004;				// (what a read of the BIOS gives after a call)
#ifdef GBA_DEBUG
	extern unsigned g_swiCount[256]; g_swiCount[n & 255]++;
#endif
	switch (n)
	{
	case 0x00:						// SoftReset
	{
		bool toRam = iwram[0x7FFA] != 0;
		for (int i = 0x7E00; i < 0x8000; i++) iwram[i] = 0;
		setCpsr (0x13); r[13] = 0x03007FE0; r[14] = 0; spsr = 0;
		setCpsr (0x12); r[13] = 0x03007FA0; r[14] = 0; spsr = 0;
		setCpsr (0x1F); r[13] = 0x03007F00;
		for (int i = 0; i < 13; i++) r[i] = 0;
		r[14] = 0;
		cpsr &= ~0x20u;
		setPC (toRam ? 0x02000000 : 0x08000000);
		break;
	}
	case 0x01:						// RegisterRamReset
	{
		u32 f = r[0];
		if (f & 1) for (u32 i = 0; i < sizeof ewram; i++) ewram[i] = 0;
		if (f & 2) for (u32 i = 0; i < 0x7E00; i++) iwram[i] = 0;
		if (f & 4) for (u32 i = 0; i < sizeof pal; i++) pal[i] = 0;
		if (f & 8) for (u32 i = 0; i < sizeof vram; i++) vram[i] = 0;
		if (f & 16) for (u32 i = 0; i < sizeof oam; i++) oam[i] = 0;
		if (f & 0x80) { for (u32 i = 0; i < 0x60; i++) io[i] = 0; io[0x20] = io[0x26] = io[0x30] = io[0x36] = 0; io[0x21] = io[0x27] = io[0x31] = io[0x37] = 1; }
		io[0] = 0x80; io[1] = 0;
		break;
	}
	case 0x02: halted = true; break;			// Halt
	case 0x03: halted = true; break;			// Stop
	case 0x04: case 0x05:					// IntrWait (r0: forget the old flags, r1: which), VBlankIntrWait
	{
		u32 discard = n == 5 ? 1 : r[0], mask = n == 5 ? 1 : r[1];
		if (n == 5) { r[0] = 1; r[1] = 1; }
		io[0x208] = 1; checkIrq ();
		u16 bif = (u16) (iwram[0x7FF8] | (iwram[0x7FF9] << 8));
		if (!intrWait) { intrWait = 1; if (discard) bif &= (u16) ~mask; }
		if (bif & mask)
		{
			bif &= (u16) ~mask; intrWait = 0;
		}
		else
		{
			halted = true;
			setPC (fetchPC);				// run the call again once an interrupt came
		}
		iwram[0x7FF8] = (u8) bif; iwram[0x7FF9] = (u8) (bif >> 8);
		break;
	}
	case 0x06: case 0x07:					// Div, DivArm
	{
		s32 num = (s32) (n == 6 ? r[0] : r[1]), den = (s32) (n == 6 ? r[1] : r[0]);
		if (den == 0) { r[0] = num < 0 ? (u32) -1 : 1; r[1] = (u32) num; r[3] = 1; break; }
		if (num == (s32) 0x80000000 && den == -1) { r[0] = 0x80000000; r[1] = 0; r[3] = 0x80000000; break; }
		s32 q = num / den;
		r[0] = (u32) q; r[1] = (u32) (num % den); r[3] = (u32) (q < 0 ? -q : q);
		cyc += 20;
		break;
	}
	case 0x08: r[0] = isqrt (r[0]); cyc += 20; break;	// Sqrt
	case 0x09: r[0] = (u32) arcTan ((s32) (s16) r[0]) & 0xFFFF; break;
	case 0x0A: r[0] = arcTan2 ((s32) (s16) r[0], (s32) (s16) r[1]); break;
	case 0x0B:						// CpuSet
	{
		u32 s = r[0], d = r[1], c = r[2], cnt = c & 0x1FFFFF;
		bool fill = c & (1 << 24), word = c & (1 << 26);
		if ((s >> 24) == 0 && fetchPC >= 0x4000) break;	// (the BIOS refuses its own area)
		if (word)
		{
			s &= ~3u; d &= ~3u;
			u32 v = read32 (s);
			for (u32 i = 0; i < cnt; i++) { write32 (d, fill ? v : read32 (s)); d += 4; if (!fill) s += 4; }
		}
		else
		{
			s &= ~1u; d &= ~1u;
			u16 v = read16 (s);
			for (u32 i = 0; i < cnt; i++) { write16 (d, fill ? v : read16 (s)); d += 2; if (!fill) s += 2; }
		}
		cyc += (int) cnt * 2;
		break;
	}
	case 0x0C:						// CpuFastSet
	{
		u32 s = r[0] & ~3u, d = r[1] & ~3u, c = r[2], cnt = (c & 0x1FFFFF);
		cnt = (cnt + 7) & ~7u;
		bool fill = c & (1 << 24);
		u32 v = read32 (s);
		for (u32 i = 0; i < cnt; i++) { write32 (d, fill ? v : read32 (s)); d += 4; if (!fill) s += 4; }
		cyc += (int) cnt;
		break;
	}
	case 0x0D: r[0] = 0xBAAE187F; break;			// GetBiosChecksum
	case 0x0E:						// BgAffineSet
	{
		u32 s = r[0], d = r[1];
		for (u32 k = 0; k < r[2]; k++)
		{
			s32 ox = (s32) read32 (s), oy = (s32) read32 (s + 4);
			s32 cx = (s16) read16 (s + 8), cy = (s16) read16 (s + 10);
			s32 sx = (s16) read16 (s + 12), sy = (s16) read16 (s + 14);
			int th = read16 (s + 16) >> 8;
			s32 sn = SIN[th], cs = SIN[(th + 64) & 255];
			s32 a = (cs * sx) >> 14, b = -((sn * sx) >> 14), c = (sn * sy) >> 14, dd = (cs * sy) >> 14;
			write16 (d, (u16) a); write16 (d + 2, (u16) b); write16 (d + 4, (u16) c); write16 (d + 6, (u16) dd);
			write32 (d + 8, (u32) (ox - (a * cx + b * cy)));
			write32 (d + 12, (u32) (oy - (c * cx + dd * cy)));
			s += 20; d += 16;
		}
		break;
	}
	case 0x0F:						// ObjAffineSet
	{
		u32 s = r[0], d = r[1], stride = r[3];
		for (u32 k = 0; k < r[2]; k++)
		{
			s32 sx = (s16) read16 (s), sy = (s16) read16 (s + 2);
			int th = read16 (s + 4) >> 8;
			s32 sn = SIN[th], cs = SIN[(th + 64) & 255];
			write16 (d, (u16) ((cs * sx) >> 14)); d += stride;
			write16 (d, (u16) -((sn * sx) >> 14)); d += stride;
			write16 (d, (u16) ((sn * sy) >> 14)); d += stride;
			write16 (d, (u16) ((cs * sy) >> 14)); d += stride;
			s += 8;
		}
		break;
	}
	case 0x10:						// BitUnPack
	{
		u32 s = r[0], d = r[1], info = r[2];
		u32 len = read16 (info), sw = read8 (info + 2), dw = read8 (info + 3), off = read32 (info + 4);
		bool zero = off & 0x80000000; off &= 0x7FFFFFFF;
		if (!sw || !dw || dw > 32) break;
		u32 out = 0; int outBits = 0;
		for (u32 i = 0; i < len; i++)
		{
			u32 byte = read8 (s + i);
			for (u32 b = 0; b < 8; b += sw)
			{
				u32 v = (byte >> b) & ((1u << sw) - 1);
				if (v || zero) v += off;
				out |= v << outBits; outBits += (int) dw;
				if (outBits >= 32) { write32 (d, out); d += 4; out = 0; outBits = 0; }
			}
		}
		break;
	}
	case 0x11: case 0x12:					// LZ77UnCompWram / Vram
	{
		u32 s = r[0], d = r[1], hdr = read32 (s); s += 4;
		u32 size = hdr >> 8, done = 0;
		bool vr = n == 0x12;
		u32 half = 0;
		while (done < size)
		{
			u8 flags = read8 (s++);
			for (int k = 0; k < 8 && done < size; k++, flags <<= 1)
			{
				if (flags & 0x80)
				{
					u16 v = (u16) ((read8 (s) << 8) | read8 (s + 1)); s += 2;
					u32 len = (v >> 12) + 3, disp = (v & 0xFFF) + 1;
					for (u32 j = 0; j < len && done < size; j++)
					{
						u32 from = d + done - disp;
						u8 b = vr ? (u8) (read16 (from & ~1u) >> ((from & 1) * 8)) : read8 (from);
						if ((from & ~1u) == ((d + done) & ~1u) && vr) b = (u8) half;	// (the half being built)
						if (vr) { if ((d + done) & 1) write16 ((d + done) & ~1u, (u16) (half | (b << 8))); else half = b; }
						else write8 (d + done, b);
						done++;
					}
				}
				else
				{
					u8 b = read8 (s++);
					if (vr) { if ((d + done) & 1) write16 ((d + done) & ~1u, (u16) (half | (b << 8))); else half = b; }
					else write8 (d + done, b);
					done++;
				}
			}
		}
		cyc += (int) size;
		break;
	}
	case 0x13:						// HuffUnComp
	{
		u32 s = r[0], d = r[1], hdr = read32 (s);
		u32 bits = hdr & 15, size = hdr >> 8;
		u32 tree = s + 4, treeSize = read8 (tree);
		u32 data = tree + (treeSize + 1) * 2;
		u32 out = 0; int outBits = 0; u32 done = 0;
		u32 node = tree + 1; u8 nv = read8 (node);
		while (done < size)
		{
			u32 word = read32 (data); data += 4;
			for (int b = 31; b >= 0 && done < size; b--)
			{
				int dir = (word >> b) & 1;
				u32 next = (node & ~1u) + (nv & 0x3F) * 2 + 2 + (u32) dir;
				bool leaf = dir ? (nv & 0x40) : (nv & 0x80);
				if (leaf)
				{
					out |= (u32) (read8 (next) & ((1u << bits) - 1)) << outBits;
					outBits += (int) bits;
					if (outBits == 32) { write32 (d, out); d += 4; done += 4; out = 0; outBits = 0; }
					node = tree + 1; nv = read8 (node);
				}
				else { node = next; nv = read8 (node); }
			}
		}
		break;
	}
	case 0x14: case 0x15:					// RLUnCompWram / Vram
	{
		u32 s = r[0], d = r[1], size = read32 (s) >> 8, done = 0; s += 4;
		bool vr = n == 0x15; u32 half = 0;
		while (done < size)
		{
			u8 f = read8 (s++);
			u32 len = (f & 0x80) ? (f & 0x7F) + 3 : (f & 0x7F) + 1;
			u8 rep = (f & 0x80) ? read8 (s++) : 0;
			for (u32 j = 0; j < len && done < size; j++)
			{
				u8 b = (f & 0x80) ? rep : read8 (s++);
				if (vr) { if ((d + done) & 1) write16 ((d + done) & ~1u, (u16) (half | (b << 8))); else half = b; }
				else write8 (d + done, b);
				done++;
			}
		}
		break;
	}
	case 0x16: case 0x17: case 0x18:			// Diff8bitUnFilterWram / Vram, Diff16bitUnFilter
	{
		u32 s = r[0], d = r[1], size = read32 (s) >> 8; s += 4;
		if (n == 0x18)
		{
			u16 acc = 0;
			for (u32 i = 0; i < size; i += 2) { acc = (u16) (acc + read16 (s + i)); write16 (d + i, acc); }
		}
		else
		{
			u8 acc = 0; u32 half = 0;
			for (u32 i = 0; i < size; i++)
			{
				acc = (u8) (acc + read8 (s + i));
				if (n == 0x16) write8 (d + i, acc);
				else if (i & 1) write16 ((d + i) & ~1u, (u16) (half | (acc << 8)));
				else half = acc;
			}
		}
		break;
	}
	case 0x19: break;					// SoundBias
	case 0x1F:						// MidiKey2Freq: freq / 2^((180 - key - fine / 256) / 12)
	{
		u32 base = read32 (r[0] + 4);
		u32 e = (180 - (r[1] & 0xFF)) * 256 - (r[2] & 0xFF);	// in 1/256 semitones
		u32 oct = e / 3072, frac = e % 3072;
		u64 v = (u64) (oct < 32 ? base >> oct : 0) * pow2neg (frac) >> 16;
		r[0] = (u32) v;
		break;
	}
	default: break;
	}
}

} // namespace gba
