//
// nds/nds_bios.cpp -- what the BIOS does, without its image: the two processors' interrupt entries
// (a few ARM words, as the real ones: they call the game's handler at DTCM + 0x3FFC / 0x0380FFFC),
// the calls (SWI) done in C++ (IntrWait and its flags at DTCM + 0x3FF8 / 0x0380FFF8, Div, Sqrt,
// CpuSet, the decompressions -- the ones that read through the game's callbacks run them --, the
// ARM7's sound tables); the firmware made up (its user settings: a name, the language, the touch
// screen's calibration); and the direct boot: the header's two programs loaded where the BIOS would,
// the processors set up as it leaves them.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

#include "nds/nds_tables.inc"

enum { MAGIC9 = 0xFFFF0F00, MAGIC7 = 0x00003F00 };

void Machine::biosMake ()
{
	mfill (bios9, 0, sizeof bios9);
	mfill (bios7, 0, sizeof bios7);
	// ARM9 (0xFFFF0000): the vectors, the IRQ entry at 0x40
	for (int i = 0; i < 8; i++) wr32 (bios9 + i * 4, 0xEAFFFFFE);	// b .
	wr32 (bios9 + 0x18, 0xEA000008);				// b 0x40
	static const u32 IRQ9[] = { 0xE92D500F, 0xEE190F11, 0xE1A00620, 0xE1A00600, 0xE2800901, 0xE28FE000, 0xE510F004, 0xE8BD500F, 0xE25EF004 };
	for (int i = 0; i < 9; i++) wr32 (bios9 + 0x40 + i * 4, IRQ9[i]);
	wr32 (bios9 + (MAGIC9 & 0xFFF), 0xEAFFFFFE);
	// ARM7 (0x00000000)
	for (int i = 0; i < 8; i++) wr32 (bios7 + i * 4, 0xEAFFFFFE);
	wr32 (bios7 + 0x18, 0xEA000008);
	static const u32 IRQ7[] = { 0xE92D500F, 0xE3A00301, 0xE28FE000, 0xE510F004, 0xE8BD500F, 0xE25EF004 };
	for (int i = 0; i < 6; i++) wr32 (bios7 + 0x40 + i * 4, IRQ7[i]);
	wr32 (bios7 + MAGIC7, 0xEAFFFFFE);
	// the ARM7 BIOS's tables where games might read them (the SDK calls the SWIs)
}

static u16 crc16 (u16 crc, const u8 *p, u32 n)
{
	static const u16 V[8] = { 0xC0C1, 0xC181, 0xC301, 0xC601, 0xCC01, 0xD801, 0xF001, 0xA001 };
	for (u32 i = 0; i < n; i++)
	{
		crc ^= p[i];
		for (int j = 0; j < 8; j++)
		{
			bool c = crc & 1;
			crc >>= 1;
			if (c) crc ^= (u16) (V[j] << (7 - j));
		}
	}
	return crc;
}

void Machine::firmwareMake ()
{
	mfill (firmware, 0xFF, sizeof firmware);
	u8 *f = firmware;
	mfill (f, 0, 0x200);
	wr16 (f + 0x20, 0x7FC0);					// the user settings at 0x3FE00
	f[0x08] = 'M'; f[0x09] = 'A'; f[0x0A] = 'C'; f[0x0B] = 'P';
	f[0x1D] = 0x20;							// a DS Lite
	wr16 (f + 0x2C, 0x0138);					// the wifi settings
	f[0x2F] = 5;
	static const u8 MAC[6] = { 0x00, 0x09, 0xBF, 0x11, 0x22, 0x33 };
	for (int i = 0; i < 6; i++) f[0x36 + i] = MAC[i];
	wr16 (f + 0x3C, 0x3FFE);
	wr16 (f + 0x2A, crc16 (0, f + 0x2C, 0x138));
	// the access points: none set
	for (int k = 0; k < 3; k++) { u8 *ap = f + 0x3FA00 + k * 0x100; mfill (ap, 0, 0x100); ap[0xE7] = 0xFF; wr16 (ap + 0xFE, crc16 (0, ap, 0xFE)); }
	// the user settings, twice
	u8 us[0x100]; mfill (us, 0, sizeof us);
	wr16 (us + 0x00, 5);
	us[0x02] = (u8) userColor; us[0x03] = (u8) userBMonth; us[0x04] = (u8) userBDay;
	int nl = 0;
	for (; nl < 10 && userName[nl]; nl++) wr16 (us + 0x06 + nl * 2, (u16) (u8) userName[nl]);
	wr16 (us + 0x1A, (u16) nl);
	u16 ax1, ay1, ax2, ay2;
	touchCalibrate (1, 1, ax1, ay1);
	touchCalibrate (255, 191, ax2, ay2);
	wr16 (us + 0x58, ax1); wr16 (us + 0x5A, ay1); us[0x5C] = 1; us[0x5D] = 1;
	wr16 (us + 0x5E, ax2); wr16 (us + 0x60, ay2); us[0x62] = 255; us[0x63] = 191;
	wr16 (us + 0x64, (u16) ((userLang & 7) | 0x0030 | 0xFC00));
	us[0x66] = 26;
	wr16 (us + 0x70, 1);
	wr16 (us + 0x72, crc16 (0xFFFF, us, 0x70));
	mcopy (f + 0x3FE00, us, 0x100);
	wr16 (us + 0x70, 0);
	wr16 (us + 0x72, crc16 (0xFFFF, us, 0x70));
	mcopy (f + 0x3FF00, us, 0x100);
}

// ---- a call into the game (the decompressions' callbacks) ------------------------------------------------------
u32 Machine::callGuest (Arm &c, u32 fn, u32 a0, u32 a1, u32 a2)
{
	u32 save[16]; for (int i = 0; i < 16; i++) save[i] = c.r[i];
	u32 cpsr = c.cpsr;
	bool br = c.branched; int cy = c.cyc;
	u32 magic = c.num ? MAGIC7 : MAGIC9;
	c.r[0] = a0; c.r[1] = a1; c.r[2] = a2;
	c.r[14] = magic;
	c.cpsr = (fn & 1) ? (c.cpsr | 0x20) : (c.cpsr & ~0x20u);
	c.r[15] = fn & ((fn & 1) ? ~1u : ~3u);
	for (int guard = 0; guard < 1000000; guard++)
	{
		if (c.r[15] == magic && !(c.cpsr & 0x20)) break;
		int k = (c.cpsr & 0x20) ? c.stepThumb () : c.stepArm ();
		c.ts += (s64) k << c.shift;
	}
	u32 res = c.r[0];
	for (int i = 0; i < 16; i++) c.r[i] = save[i];
	c.cpsr = cpsr;
	c.branched = br; c.cyc = cy;
	return res;
}

// ---- the calls ---------------------------------------------------------------------------------------------------------
namespace {
// the source of a decompression: memory, or the game's callbacks
struct Src
{
	Machine *m; Arm *c; bool cb; u32 addr; u32 param, fGet8, fGet32;
	u8 get8 () { if (!cb) return (u8) c->read8 (addr++); u32 v = m->callGuest (*c, fGet8, addr, 0, param); addr++; return (u8) v; }
	u32 get32 ()
	{
		if (!cb) { u32 v = c->read32 (addr & ~3u); addr += 4; return v; }
		u32 v = fGet32 ? m->callGuest (*c, fGet32, addr, 0, param) : (u32) get8 () | ((u32) get8 () << 8) | ((u32) get8 () << 16) | ((u32) get8 () << 24);
		if (fGet32) addr += 4;
		return v;
	}
};
// the destination: bytes, or halfwords (VRAM)
struct Dst
{
	Arm *c; u32 base; u32 n; bool w16; u8 half;
	void put (u8 b)
	{
		if (!w16) c->write8 (base + n, b);
		else if ((base + n) & 1) c->write16 ((base + n) & ~1u, (u32) half | ((u32) b << 8));
		else half = b;
		n++;
	}
	u8 back (u32 disp)
	{
		u32 a = base + n - disp;
		if (w16 && (a & ~1u) == ((base + n) & ~1u)) return half;
		return (u8) c->read8 (a);
	}
};
}

void Machine::swi (Arm &c, u32 n)
{
	bool thumb = c.cpsr & 0x20;
	u32 self = c.r[15] - (thumb ? 4 : 8);			// the call's own address
	c.cyc += 3;
	switch (n & 0xFF)
	{
	case 0x00: break;					// SoftReset (not supported)
	case 0x03: c.ts += (s64) c.r[0] * 4 << c.shift; c.r[0] = 0; break;	// WaitByLoop
	case 0x04: case 0x05:					// IntrWait, VBlankIntrWait
	{
		if (n == 5) { c.r[0] = 1; c.r[1] = 1; }
		u32 flagsAddr = c.num ? 0x0380FFF8 : c.dtcmBase + 0x3FF8;
		u32 mask = c.r[1];
		u32 f = c.read32 (flagsAddr);
		if (!c.intrWait && c.r[0]) { f &= ~mask; c.write32 (flagsAddr, f); }
		ime[c.num] = 1;
		if (f & mask) { c.write32 (flagsAddr, f & ~mask); c.intrWait = false; break; }
		c.intrWait = true;
		c.halted = true;
		c.setPC (self);
		break;
	}
	case 0x06: c.halted = true; break;			// Halt
	case 0x07: if (c.num) c.halted = true; break;		// Sleep
	case 0x08: if (c.num) spu.bias = c.r[0] ? 0x200 : 0; break;	// SoundBias
	case 0x09:						// Div
	{
		s32 num = (s32) c.r[0], den = (s32) c.r[1];
		if (den == 0) { c.r[0] = num < 0 ? (u32) -1 : 1; c.r[1] = (u32) num; c.r[3] = 1; break; }
		if (num == (s32) 0x80000000 && den == -1) { c.r[0] = 0x80000000; c.r[1] = 0; c.r[3] = 0x80000000; break; }
		s32 q = num / den;
		c.r[0] = (u32) q; c.r[1] = (u32) (num % den); c.r[3] = (u32) (q < 0 ? -q : q);
		c.cyc += 20;
		break;
	}
	case 0x0B:						// CpuSet
	{
		u32 s = c.r[0], d = c.r[1], k = c.r[2], cnt = k & 0x1FFFFF;
		bool fill = k & (1 << 24), word = k & (1 << 26);
		if (word)
		{
			s &= ~3u; d &= ~3u;
			u32 v = c.read32 (s);
			for (u32 i = 0; i < cnt; i++) { c.write32 (d, fill ? v : c.read32 (s)); d += 4; if (!fill) s += 4; }
		}
		else
		{
			s &= ~1u; d &= ~1u;
			u32 v = c.read16 (s);
			for (u32 i = 0; i < cnt; i++) { c.write16 (d, fill ? v : c.read16 (s)); d += 2; if (!fill) s += 2; }
		}
		c.cyc += (int) cnt;
		break;
	}
	case 0x0C:						// CpuFastSet
	{
		u32 s = c.r[0] & ~3u, d = c.r[1] & ~3u, k = c.r[2], cnt = (k & 0x1FFFFF);
		cnt = (cnt + 7) & ~7u;
		bool fill = k & (1 << 24);
		u32 v = c.read32 (s);
		for (u32 i = 0; i < cnt; i++) { c.write32 (d, fill ? v : c.read32 (s)); d += 4; if (!fill) s += 4; }
		c.cyc += (int) cnt / 2;
		break;
	}
	case 0x0D:						// Sqrt
	{
		u32 v = c.r[0], res = 0, bit = 1u << 30;
		while (bit > v) bit >>= 2;
		while (bit) { if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; } else res >>= 1; bit >>= 2; }
		c.r[0] = res;
		break;
	}
	case 0x0E:						// GetCRC16
	{
		u16 crc = (u16) c.r[0];
		u32 a = c.r[1] & ~1u, len = c.r[2] & ~1u;
		u32 last = 0;
		for (u32 i = 0; i < len; i += 2)
		{
			last = c.read16 (a + i);
			u8 b[2] = { (u8) last, (u8) (last >> 8) };
			crc = crc16 (crc, b, 2);
		}
		c.r[0] = crc; c.r[3] = last;
		break;
	}
	case 0x0F: c.r[0] = 0; break;				// IsDebugger
	case 0x10:						// BitUnPack
	{
		u32 s = c.r[0], d = c.r[1], info = c.r[2];
		u32 len = c.read16 (info), sw = c.read8 (info + 2), dw = c.read8 (info + 3), off = c.read32 (info + 4);
		bool zero = off & 0x80000000; off &= 0x7FFFFFFF;
		if (!sw || !dw || dw > 32 || sw > 8) break;
		u32 out = 0; int outBits = 0;
		for (u32 i = 0; i < len; i++)
		{
			u32 byte = c.read8 (s + i);
			for (u32 b = 0; b < 8; b += sw)
			{
				u32 v = (byte >> b) & ((1u << sw) - 1);
				if (v || zero) v += off;
				out |= (dw == 32 ? v : (v & ((1u << dw) - 1))) << outBits; outBits += (int) dw;
				if (outBits >= 32) { c.write32 (d, out); d += 4; out = 0; outBits = 0; }
			}
		}
		break;
	}
	case 0x11: case 0x12:					// LZ77 (memory, bytes) / (callbacks, halfwords)
	{
		Src s = { this, &c, n == 0x12, c.r[0], c.r[2], 0, 0 };
		u32 hdr;
		if (s.cb)
		{
			u32 cbs = c.r[3];
			u32 fOpen = c.read32 (cbs), fClose = c.read32 (cbs + 4);
			s.fGet8 = c.read32 (cbs + 8); s.fGet32 = c.read32 (cbs + 16);
			hdr = callGuest (c, fOpen, c.r[0], c.r[1], c.r[2]); s.addr += 4;
			(void) fClose;
		}
		else hdr = s.get32 ();
		Dst d = { &c, c.r[1], 0, n == 0x12, 0 };
		u32 size = hdr >> 8;
		while (d.n < size)
		{
			u8 flags = s.get8 ();
			for (int k = 0; k < 8 && d.n < size; k++, flags <<= 1)
			{
				if (flags & 0x80)
				{
					u32 b0 = s.get8 (), b1 = s.get8 ();
					u32 len = (b0 >> 4) + 3, disp = (((b0 & 15) << 8) | b1) + 1;
					for (u32 j = 0; j < len && d.n < size; j++) d.put (d.back (disp));
				}
				else d.put (s.get8 ());
			}
		}
		if (s.cb) { u32 fClose = c.read32 (c.r[3] + 4); if (fClose) callGuest (c, fClose, s.addr, 0, s.param); }
		c.cyc += (int) size;
		break;
	}
	case 0x13:						// Huffman (callbacks)
	{
		Src s = { this, &c, true, c.r[0], c.r[2], 0, 0 };
		u32 cbs = c.r[3];
		u32 fOpen = c.read32 (cbs);
		s.fGet8 = c.read32 (cbs + 8); s.fGet32 = c.read32 (cbs + 16);
		u32 hdr = callGuest (c, fOpen, c.r[0], c.r[1], c.r[2]); s.addr += 4;
		u32 bits = hdr & 15, size = hdr >> 8;
		if (bits != 4 && bits != 8) break;
		static u8 tree[512];
		u32 tsize = s.get8 ();
		tree[0] = (u8) tsize;
		for (u32 i = 1; i < (tsize + 1) * 2 && i < 512; i++) tree[i] = s.get8 ();
		u32 d = c.r[1], out = 0, done = 0; int outBits = 0;
		u32 node = 1;
		while (done < size)
		{
			u32 word = s.get32 ();
			for (int b = 31; b >= 0 && done < size; b--)
			{
				int dir = (word >> b) & 1;
				u8 nv = tree[node & 511];
				u32 next = (node & ~1u) + (nv & 0x3Fu) * 2 + 2 + (u32) dir;
				bool leaf = dir ? (nv & 0x40) : (nv & 0x80);
				if (leaf)
				{
					out |= (u32) (tree[next & 511] & ((1u << bits) - 1)) << outBits;
					outBits += (int) bits;
					if (outBits == 32) { c.write32 (d, out); d += 4; done += 4; out = 0; outBits = 0; }
					node = 1;
				}
				else node = next;
			}
		}
		u32 fClose = c.read32 (cbs + 4); if (fClose) callGuest (c, fClose, s.addr, 0, s.param);
		break;
	}
	case 0x14: case 0x15:					// RL (memory, bytes) / (callbacks, halfwords)
	{
		Src s = { this, &c, n == 0x15, c.r[0], c.r[2], 0, 0 };
		u32 hdr;
		if (s.cb)
		{
			u32 cbs = c.r[3];
			u32 fOpen = c.read32 (cbs);
			s.fGet8 = c.read32 (cbs + 8); s.fGet32 = c.read32 (cbs + 16);
			hdr = callGuest (c, fOpen, c.r[0], c.r[1], c.r[2]); s.addr += 4;
		}
		else hdr = s.get32 ();
		Dst d = { &c, c.r[1], 0, n == 0x15, 0 };
		u32 size = hdr >> 8;
		while (d.n < size)
		{
			u8 f = s.get8 ();
			u32 len = (f & 0x80) ? (f & 0x7Fu) + 3 : (f & 0x7Fu) + 1;
			if (f & 0x80) { u8 rep = s.get8 (); for (u32 j = 0; j < len && d.n < size; j++) d.put (rep); }
			else for (u32 j = 0; j < len && d.n < size; j++) d.put (s.get8 ());
		}
		if (s.cb) { u32 fClose = c.read32 (c.r[3] + 4); if (fClose) callGuest (c, fClose, s.addr, 0, s.param); }
		break;
	}
	case 0x16: case 0x17: case 0x18:			// Diff8bitUnFilter (bytes / halfwords), Diff16bitUnFilter
	{
		u32 s = c.r[0], d = c.r[1], size = c.read32 (s) >> 8; s += 4;
		if (n == 0x18)
		{
			u32 acc = 0;
			for (u32 i = 0; i < size; i += 2) { acc = (acc + c.read16 (s + i)) & 0xFFFF; c.write16 (d + i, acc); }
		}
		else
		{
			Dst o = { &c, d, 0, n == 0x17, 0 };
			u8 acc = 0;
			for (u32 i = 0; i < size; i++) { acc = (u8) (acc + c.read8 (s + i)); o.put (acc); }
		}
		break;
	}
	case 0x1A: c.r[0] = (u32) (s32) SINE[c.r[0] & 63]; break;	// GetSineTable
	case 0x1B: c.r[0] = PITCH[c.r[0] < 768 ? c.r[0] : 767]; break;	// GetPitchTable
	case 0x1C: c.r[0] = VOLUME[c.r[0] < 724 ? c.r[0] : 723]; break;	// GetVolumeTable
	case 0x1D: c.r[0] = c.r[1] = c.r[2] = 0; break;		// GetBootProcs
	case 0x1F:
		if (c.num) { u32 v = c.r[2] & 0xC0; if (v == 0x80 || v == 0xC0) c.halted = true; }	// CustomHalt
		else postflg[0] = (u8) (c.r[0] & 3);		// CustomPost
		break;
	default: break;
	}
}

// ---- the direct boot -----------------------------------------------------------------------------------------------------
bool Machine::directBoot ()
{
	const u8 *h = cart.rom;
	u32 off9 = rd32 (h + 0x20), entry9 = rd32 (h + 0x24), ram9 = rd32 (h + 0x28), size9 = rd32 (h + 0x2C);
	u32 off7 = rd32 (h + 0x30), entry7 = rd32 (h + 0x34), ram7 = rd32 (h + 0x38), size7 = rd32 (h + 0x3C);
	if (size9 > 0x3BFE00) size9 = 0x3BFE00;
	if (size7 > 0x3BFE00) size7 = 0x3BFE00;
	wramcnt = 3;						// (the shared WRAM the ARM7's, before its program goes in)
	pagesUpdate ();
	// the ARM9's program (the secure area made plain)
	for (u32 i = 0; i < size9; i += 4)
	{
		u32 v = off9 + i + 4 <= cart.romSize ? rd32 (h + off9 + i) : 0;
		bus9Write32 (ram9 + i, v);
	}
	if (off9 >= 0x4000 && off9 < 0x8000)
	{
		u8 sec[0x800];
		for (u32 i = 0; i < 0x800; i++) sec[i] = (u8) bus9Read8 (ram9 + i);
		u32 w0 = rd32 (sec), w1 = rd32 (sec + 4);
		bool plain = (w0 == 0x72636E65 && w1 == 0x6A624F79) || (w0 == 0xE7FFDEFF && w1 == 0xE7FFDEFF);
		if (!plain && bios7Key)
		{
			decryptSecureArea (sec);
			w0 = rd32 (sec); w1 = rd32 (sec + 4);
			plain = w0 == 0x72636E65 && w1 == 0x6A624F79;
			if (plain) for (u32 i = 0; i < 0x800; i++) bus9Write8 (ram9 + i, sec[i]);
		}
		if (plain) { bus9Write32 (ram9, 0xE7FFDEFF); bus9Write32 (ram9 + 4, 0xE7FFDEFF); }
		else
		{
			const char *e = "encrypted secure area: a decrypted dump (or the ARM7 BIOS) is needed";
			int k = 0; while (e[k] && k < 95) { lastError[k] = e[k]; k++; } lastError[k] = 0;
		}
	}
	for (u32 i = 0; i < size7; i += 4)
	{
		u32 v = off7 + i + 4 <= cart.romSize ? rd32 (h + off7 + i) : 0;
		a7Write32 (ram7 + i, v);
	}
	// what the BIOS and the firmware leave in main memory
	for (u32 i = 0; i < 0x170; i += 4) bus9Write32 (0x027FFE00 + i, rd32 (h + i));
	bus9Write32 (0x027FF800, cart.chipId); bus9Write32 (0x027FF804, cart.chipId);
	bus9Write16 (0x027FF808, rd16 (h + 0x15E)); bus9Write16 (0x027FF80A, rd16 (h + 0x6C));
	bus9Write16 (0x027FF850, 0x5835);
	bus9Write32 (0x027FFC00, cart.chipId); bus9Write32 (0x027FFC04, cart.chipId);
	bus9Write16 (0x027FFC08, rd16 (h + 0x15E)); bus9Write16 (0x027FFC0A, rd16 (h + 0x6C));
	bus9Write16 (0x027FFC10, 0x5835);
	bus9Write16 (0x027FFC30, 0xFFFF);
	bus9Write16 (0x027FFC40, 0x0001);
	for (u32 i = 0; i < 0x70; i++) bus9Write8 (0x027FFC80 + i, firmware[0x3FE00 + i]);
	// the processors
	arm9.cpDtcm = 0x0300000A; arm9.cpItcm = 0x00000020;
	arm9.cpCtl = 0x00052078;
	arm9.excBase = 0xFFFF0000;
	arm9.updateTcm ();
	arm9.setCpsr (0xD2); arm9.r[13] = 0x03003F80;
	arm9.setCpsr (0xD3); arm9.r[13] = 0x03003FC0;
	arm9.setCpsr (0xDF); arm9.r[13] = 0x03002F7C;
	arm9.r[12] = arm9.r[14] = entry9; arm9.r[15] = entry9 & ~3u;
	if (entry9 & 1) { arm9.cpsr |= 0x20; arm9.r[15] = entry9 & ~1u; }
	arm7.setCpsr (0xD2); arm7.r[13] = 0x0380FF80;
	arm7.setCpsr (0xD3); arm7.r[13] = 0x0380FFC0;
	arm7.setCpsr (0xDF); arm7.r[13] = 0x0380FD80;
	arm7.r[12] = arm7.r[14] = entry7; arm7.r[15] = entry7 & ~3u;
	if (entry7 & 1) { arm7.cpsr |= 0x20; arm7.r[15] = entry7 & ~1u; }
	wramcnt = 3;
	postflg[0] = postflg[1] = 1;
	powcnt1 = 0x820F; powcnt2 = 1;
	exmemcnt = 0x6000;
	spu.bias = 0x200;
	wifiWait[0] = 0x30;
	biosProt = 0x1204;
	keycnt[0] = keycnt[1] = 0;
	rtcStat1 = 0x02;					// (24-hour)
	pagesUpdate ();
	return true;
}

} // namespace nds
