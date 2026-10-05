//
// snes/snes_cpu.cpp -- the 65C816 (the 5A22's core): native and emulation modes, the 8 / 16-bit
// accumulator and index registers, every opcode and addressing mode (with the direct-page and
// stack wraps of emulation mode), decimal ADC / SBC, the block moves, WAI / STP, NMI / IRQ /
// BRK / COP. Each memory access costs the speed of its address (snes.cpp: read / write add
// to the master clock), each internal cycle 6 clocks.
//
#include "snes/snes.h"

namespace snes {

enum { FC = 0x01, FZ = 0x02, FI = 0x04, FD = 0x08, FX = 0x10, FM = 0x20, FV = 0x40, FN = 0x80 };

void Machine::setP (u8 v)
{
	P = v;
	if (E) P |= 0x30;
	if (P & FX) { X &= 0xFF; Y &= 0xFF; }
}

void Machine::push8 (u8 v)
{
	write (S, v);
	S = E ? (u16) (0x100 | ((S - 1) & 0xFF)) : (u16) (S - 1);
}
u8 Machine::pull8 ()
{
	S = E ? (u16) (0x100 | ((S + 1) & 0xFF)) : (u16) (S + 1);
	return read (S);
}

void Machine::interrupt (u16 vecNative, u16 vecEmu, bool brk)
{
	io (); io ();
	if (!E) push8 (PB);
	push8 ((u8) (PC >> 8)); push8 ((u8) PC);
	push8 (E ? (u8) (brk ? (P | 0x10) : (P & ~0x10)) : P);
	P = (u8) ((P | FI) & ~FD);
	PB = 0;
	u16 vec = E ? vecEmu : vecNative;
	PC = (u16) (read (vec) | read ((u16) (vec + 1)) << 8);
	waiting = false;
}

// An operand's place: its 24-bit address; b0: its second byte wraps in bank 0 (direct page,
// stack relative), not across banks.
struct Loc { u32 a; bool b0; };

void Machine::cpuStep ()
{
	if (nmiPending) { nmiPending = false; interrupt (0xFFEA, 0xFFFA, false); return; }
	if (irqLine && !(P & FI)) { interrupt (0xFFEE, 0xFFFE, false); return; }

	const bool m8 = P & FM, x8 = P & FX;
	auto setZN8 = [&] (u8 v) { P = (u8) ((P & ~(FZ | FN)) | (v ? 0 : FZ) | (v & FN)); };
	auto setZN16 = [&] (u16 v) { P = (u8) ((P & ~(FZ | FN)) | (v ? 0 : FZ) | ((v >> 8) & FN)); };
	auto rd16 = [&] (Loc l) -> u16 { u8 lo = read (l.a); u8 hi = read (l.b0 ? (l.a + 1) & 0xFFFF : (l.a + 1) & 0xFFFFFF); return (u16) (lo | hi << 8); };
	auto wr16 = [&] (Loc l, u16 v) { write (l.a, (u8) v); write (l.b0 ? (l.a + 1) & 0xFFFF : (l.a + 1) & 0xFFFFFF, (u8) (v >> 8)); };
	// the direct page: in emulation mode with DL = 0, indexing wraps in the page
	auto dpAddr = [&] (u32 off) -> u32 { if (E && !(D & 0xFF)) return (u32) ((D & 0xFF00) | (off & 0xFF)); return (u32) ((D + off) & 0xFFFF); };
	auto dpPtr = [&] (u32 off) -> u16 { u8 lo = read (dpAddr (off)); u8 hi = read (dpAddr (off + 1)); return (u16) (lo | hi << 8); };
	auto dpPtrN = [&] (u32 off) -> u16 { u8 lo = read ((D + off) & 0xFFFF); u8 hi = read ((D + off + 1) & 0xFFFF); return (u16) (lo | hi << 8); };
	auto dpIo = [&] () { if (D & 0xFF) io (); };
	auto idxPenalty = [&] (u32 base, u32 a, bool wr) { if (wr || !x8 || ((base ^ a) & 0xFF00)) io (); };

	// the addressing modes of the ALU group (op & 0x1F)
	auto mode = [&] (int m, bool wr) -> Loc {
		switch (m)
		{
		case 0x01: {					// (dp,X)
			u8 off = fetch (); dpIo (); io ();
			u16 lo, ptr;
			if (E) { u32 a0 = (D & 0xFF) ? (u32) ((D + off + X) & 0xFFFF) : (u32) ((D & 0xFF00) | ((off + X) & 0xFF));
				lo = read (a0); u8 hi = read ((a0 & 0xFF00) | ((a0 + 1) & 0xFF)); ptr = (u16) (lo | hi << 8); }
			else { u32 a0 = (D + off + X) & 0xFFFF; lo = read (a0); u8 hi = read ((a0 + 1) & 0xFFFF); ptr = (u16) (lo | hi << 8); }
			return Loc { (u32) DB << 16 | ptr, false }; }
		case 0x03: { u8 off = fetch (); io (); return Loc { (u32) ((S + off) & 0xFFFF), true }; }
		case 0x05: { u8 off = fetch (); dpIo (); return Loc { (u32) ((D + off) & 0xFFFF), true }; }
		case 0x07: { u8 off = fetch (); dpIo (); u16 p = dpPtrN (off); u8 b = read ((D + off + 2) & 0xFFFF); return Loc { (u32) b << 16 | p, false }; }
		case 0x0D: { u16 a = fetch16 (); return Loc { (u32) DB << 16 | a, false }; }
		case 0x0F: { u16 a = fetch16 (); u8 b = fetch (); return Loc { (u32) b << 16 | a, false }; }
		case 0x11: { u8 off = fetch (); dpIo (); u32 base = (u32) DB << 16 | dpPtr (off); u32 a = (base + Y) & 0xFFFFFF; idxPenalty (base, a, wr); return Loc { a, false }; }
		case 0x12: { u8 off = fetch (); dpIo (); return Loc { (u32) DB << 16 | dpPtr (off), false }; }
		case 0x13: { u8 off = fetch (); io (); u32 pa = (S + off) & 0xFFFF; u16 p = (u16) (read (pa) | read ((pa + 1) & 0xFFFF) << 8); io ();
			return Loc { (((u32) DB << 16 | p) + Y) & 0xFFFFFF, false }; }
		case 0x15: { u8 off = fetch (); dpIo (); io (); return Loc { dpAddr ((u32) off + X), !E || (D & 0xFF) }; }
		case 0x17: { u8 off = fetch (); dpIo (); u16 p = dpPtrN (off); u8 b = read ((D + off + 2) & 0xFFFF); return Loc { (((u32) b << 16 | p) + Y) & 0xFFFFFF, false }; }
		case 0x19: { u32 base = (u32) DB << 16 | fetch16 (); u32 a = (base + Y) & 0xFFFFFF; idxPenalty (base, a, wr); return Loc { a, false }; }
		case 0x1D: { u32 base = (u32) DB << 16 | fetch16 (); u32 a = (base + X) & 0xFFFFFF; idxPenalty (base, a, wr); return Loc { a, false }; }
		case 0x1F: { u16 a = fetch16 (); u8 b = fetch (); return Loc { (((u32) b << 16 | a) + X) & 0xFFFFFF, false }; }
		}
		return Loc { 0, false };
	};
	auto dpX = [&] () -> Loc { u8 off = fetch (); dpIo (); io (); return Loc { dpAddr ((u32) off + X), true }; };
	auto dpY = [&] () -> Loc { u8 off = fetch (); dpIo (); io (); return Loc { dpAddr ((u32) off + Y), true }; };
	auto dp = [&] () -> Loc { return mode (0x05, false); };
	auto abs = [&] () -> Loc { return mode (0x0D, false); };
	auto absX = [&] (bool wr) -> Loc { return mode (0x1D, wr); };
	auto absY = [&] (bool wr) -> Loc { return mode (0x19, wr); };

	auto loadM = [&] (Loc l) -> u16 { return m8 ? read (l.a) : rd16 (l); };
	auto loadX = [&] (Loc l) -> u16 { return x8 ? read (l.a) : rd16 (l); };
	auto storeM = [&] (Loc l, u16 v) { if (m8) write (l.a, (u8) v); else wr16 (l, v); };
	auto storeX = [&] (Loc l, u16 v) { if (x8) write (l.a, (u8) v); else wr16 (l, v); };

	auto adc = [&] (u16 v) {
		int c = P & FC, r;
		if (m8)
		{
			int a = A & 0xFF; v &= 0xFF;
			if (!(P & FD)) r = a + v + c;
			else { r = (a & 0x0F) + (v & 0x0F) + c; if (r > 0x09) r += 0x06; c = r > 0x0F; r = (a & 0xF0) + (v & 0xF0) + (c << 4) + (r & 0x0F); }
			P = (u8) ((P & ~FV) | ((~(a ^ v) & (a ^ r) & 0x80) ? FV : 0));
			if ((P & FD) && r > 0x9F) r += 0x60;
			P = (u8) ((P & ~FC) | (r > 0xFF ? FC : 0));
			A = (u16) ((A & 0xFF00) | (r & 0xFF)); setZN8 ((u8) r);
		}
		else
		{
			int a = A;
			if (!(P & FD)) r = a + v + c;
			else
			{
				r = (a & 0x000F) + (v & 0x000F) + c; if (r > 0x0009) r += 0x0006; c = r > 0x000F;
				r = (a & 0x00F0) + (v & 0x00F0) + (c << 4) + (r & 0x000F); if (r > 0x009F) r += 0x0060; c = r > 0x00FF;
				r = (a & 0x0F00) + (v & 0x0F00) + (c << 8) + (r & 0x00FF); if (r > 0x09FF) r += 0x0600; c = r > 0x0FFF;
				r = (a & 0xF000) + (v & 0xF000) + (c << 12) + (r & 0x0FFF);
			}
			P = (u8) ((P & ~FV) | ((~(a ^ v) & (a ^ r) & 0x8000) ? FV : 0));
			if ((P & FD) && r > 0x9FFF) r += 0x6000;
			P = (u8) ((P & ~FC) | (r > 0xFFFF ? FC : 0));
			A = (u16) r; setZN16 (A);
		}
	};
	auto sbc = [&] (u16 v) {
		int c = P & FC, r;
		if (m8)
		{
			int a = A & 0xFF; v = (u16) (~v & 0xFF);
			if (!(P & FD)) r = a + v + c;
			else { r = (a & 0x0F) + (v & 0x0F) + c; if (r <= 0x0F) r -= 0x06; c = r > 0x0F; r = (a & 0xF0) + (v & 0xF0) + (c << 4) + (r & 0x0F); }
			P = (u8) ((P & ~FV) | ((~(a ^ v) & (a ^ r) & 0x80) ? FV : 0));
			if ((P & FD) && r <= 0xFF) r -= 0x60;
			P = (u8) ((P & ~FC) | (r > 0xFF ? FC : 0));
			A = (u16) ((A & 0xFF00) | (r & 0xFF)); setZN8 ((u8) r);
		}
		else
		{
			int a = A; v = (u16) ~v;
			if (!(P & FD)) r = a + v + c;
			else
			{
				r = (a & 0x000F) + (v & 0x000F) + c; if (r <= 0x000F) r -= 0x0006; c = r > 0x000F;
				r = (a & 0x00F0) + (v & 0x00F0) + (c << 4) + (r & 0x000F); if (r <= 0x00FF) r -= 0x0060; c = r > 0x00FF;
				r = (a & 0x0F00) + (v & 0x0F00) + (c << 8) + (r & 0x00FF); if (r <= 0x0FFF) r -= 0x0600; c = r > 0x0FFF;
				r = (a & 0xF000) + (v & 0xF000) + (c << 12) + (r & 0x0FFF);
			}
			P = (u8) ((P & ~FV) | ((~(a ^ v) & (a ^ r) & 0x8000) ? FV : 0));
			if ((P & FD) && r <= 0xFFFF) r -= 0x6000;
			P = (u8) ((P & ~FC) | (r > 0xFFFF ? FC : 0));
			A = (u16) r; setZN16 (A);
		}
	};
	auto cmp = [&] (u16 r, u16 v, bool b8) {
		if (b8) { r &= 0xFF; v &= 0xFF; int d = r - v; P = (u8) ((P & ~FC) | (d >= 0 ? FC : 0)); setZN8 ((u8) d); }
		else { int d = r - v; P = (u8) ((P & ~FC) | (d >= 0 ? FC : 0)); setZN16 ((u16) d); }
	};
	auto bit = [&] (u16 v, bool imm) {
		if (m8) { if (!imm) P = (u8) ((P & ~(FN | FV)) | (v & 0xC0)); P = (u8) ((P & ~FZ) | ((A & v & 0xFF) ? 0 : FZ)); }
		else { if (!imm) P = (u8) ((P & ~(FN | FV)) | ((v >> 8) & 0xC0)); P = (u8) ((P & ~FZ) | ((A & v) ? 0 : FZ)); }
	};
	// read-modify-write on memory / on A: kind 0 ASL, 1 ROL, 2 LSR, 3 ROR, 4 INC, 5 DEC, 6 TSB, 7 TRB
	auto alter = [&] (int kind, u16 v, bool b8) -> u16 {
		u16 top = b8 ? 0x80 : 0x8000, mask = b8 ? 0xFF : 0xFFFF;
		u16 r = 0;
		switch (kind)
		{
		case 0: P = (u8) ((P & ~FC) | ((v & top) ? FC : 0)); r = (u16) ((v << 1) & mask); break;
		case 1: { u16 c = P & FC; P = (u8) ((P & ~FC) | ((v & top) ? FC : 0)); r = (u16) (((v << 1) | c) & mask); break; }
		case 2: P = (u8) ((P & ~FC) | (v & 1)); r = (u16) (v >> 1); break;
		case 3: { u16 c = (P & FC) ? top : 0; P = (u8) ((P & ~FC) | (v & 1)); r = (u16) ((v >> 1) | c); break; }
		case 4: r = (u16) ((v + 1) & mask); break;
		case 5: r = (u16) ((v - 1) & mask); break;
		case 6: P = (u8) ((P & ~FZ) | ((A & v & mask) ? 0 : FZ)); return (u16) ((v | A) & mask);
		case 7: P = (u8) ((P & ~FZ) | ((A & v & mask) ? 0 : FZ)); return (u16) (v & ~A & mask);
		}
		if (b8) setZN8 ((u8) r); else setZN16 (r);
		return r;
	};
	auto rmw = [&] (Loc l, int kind) {
		if (m8) { u8 v = read (l.a); io (); write (l.a, (u8) alter (kind, v, true)); }
		else { u16 v = rd16 (l); io (); u16 r = alter (kind, v, false);
			write (l.b0 ? (l.a + 1) & 0xFFFF : (l.a + 1) & 0xFFFFFF, (u8) (r >> 8)); write (l.a, (u8) r); }
	};
	auto rmwA = [&] (int kind) {
		io ();
		if (m8) A = (u16) ((A & 0xFF00) | alter (kind, A & 0xFF, true));
		else A = alter (kind, A, false);
	};
	auto branch = [&] (bool take) {
		s8 off = (s8) fetch ();
		if (!take) return;
		u16 t = (u16) (PC + off);
		io ();
		if (E && ((t ^ PC) & 0xFF00)) io ();
		PC = t;
	};
	auto setX = [&] (u16 &r, u16 v) { if (x8) { r = v & 0xFF; setZN8 ((u8) r); } else { r = v; setZN16 (v); } };
	auto setA = [&] (u16 v) { if (m8) { A = (u16) ((A & 0xFF00) | (v & 0xFF)); setZN8 ((u8) v); } else { A = v; setZN16 (v); } };
	auto pushM = [&] (u16 v, bool b8) { if (!b8) push8 ((u8) (v >> 8)); push8 ((u8) v); };
	auto pullM = [&] (bool b8) -> u16 { u16 l = pull8 (); if (b8) return l; return (u16) (l | pull8 () << 8); };

	u8 op = fetch ();
	// the ALU group: ORA AND EOR ADC STA LDA CMP SBC x 15 addressing modes
	int lo = op & 0x1F;
	if (((op & 1) && (op & 0x0F) != 0x0B) || lo == 0x12)
	{
		int kind = op >> 5;
		u16 v;
		if (lo == 0x09)
		{
			if (kind == 4) { v = m8 ? fetch () : fetch16 (); bit (v, true); return; }	// $89 BIT #
			v = m8 ? fetch () : fetch16 ();
		}
		else
		{
			Loc l = mode (lo, kind == 4);
			if (kind == 4) { storeM (l, A); return; }
			v = loadM (l);
		}
		switch (kind)
		{
		case 0: setA ((u16) (A | v)); break;
		case 1: setA ((u16) (A & v)); break;
		case 2: setA ((u16) (A ^ v)); break;
		case 3: adc (v); break;
		case 5: setA (v); break;
		case 6: cmp (A, v, m8); break;
		case 7: sbc (v); break;
		}
		return;
	}

	switch (op)
	{
	// ---- read-modify-write ----
	case 0x06: rmw (dp (), 0); break;  case 0x0E: rmw (abs (), 0); break;  case 0x16: rmw (dpX (), 0); break;  case 0x1E: rmw (absX (true), 0); break;
	case 0x26: rmw (dp (), 1); break;  case 0x2E: rmw (abs (), 1); break;  case 0x36: rmw (dpX (), 1); break;  case 0x3E: rmw (absX (true), 1); break;
	case 0x46: rmw (dp (), 2); break;  case 0x4E: rmw (abs (), 2); break;  case 0x56: rmw (dpX (), 2); break;  case 0x5E: rmw (absX (true), 2); break;
	case 0x66: rmw (dp (), 3); break;  case 0x6E: rmw (abs (), 3); break;  case 0x76: rmw (dpX (), 3); break;  case 0x7E: rmw (absX (true), 3); break;
	case 0xE6: rmw (dp (), 4); break;  case 0xEE: rmw (abs (), 4); break;  case 0xF6: rmw (dpX (), 4); break;  case 0xFE: rmw (absX (true), 4); break;
	case 0xC6: rmw (dp (), 5); break;  case 0xCE: rmw (abs (), 5); break;  case 0xD6: rmw (dpX (), 5); break;  case 0xDE: rmw (absX (true), 5); break;
	case 0x04: rmw (dp (), 6); break;  case 0x0C: rmw (abs (), 6); break;
	case 0x14: rmw (dp (), 7); break;  case 0x1C: rmw (abs (), 7); break;
	case 0x0A: rmwA (0); break; case 0x2A: rmwA (1); break; case 0x4A: rmwA (2); break; case 0x6A: rmwA (3); break;
	case 0x1A: rmwA (4); break; case 0x3A: rmwA (5); break;
	case 0xE8: io (); setX (X, (u16) (X + 1)); break;
	case 0xC8: io (); setX (Y, (u16) (Y + 1)); break;
	case 0xCA: io (); setX (X, (u16) (X - 1)); break;
	case 0x88: io (); setX (Y, (u16) (Y - 1)); break;

	// ---- the index registers, STZ, BIT ----
	case 0xA2: setX (X, x8 ? fetch () : fetch16 ()); break;
	case 0xA6: setX (X, loadX (dp ())); break;
	case 0xB6: setX (X, loadX (dpY ())); break;
	case 0xAE: setX (X, loadX (abs ())); break;
	case 0xBE: setX (X, loadX (absY (false))); break;
	case 0xA0: setX (Y, x8 ? fetch () : fetch16 ()); break;
	case 0xA4: setX (Y, loadX (dp ())); break;
	case 0xB4: setX (Y, loadX (dpX ())); break;
	case 0xAC: setX (Y, loadX (abs ())); break;
	case 0xBC: setX (Y, loadX (absX (false))); break;
	case 0x86: storeX (dp (), X); break;
	case 0x96: storeX (dpY (), X); break;
	case 0x8E: storeX (abs (), X); break;
	case 0x84: storeX (dp (), Y); break;
	case 0x94: storeX (dpX (), Y); break;
	case 0x8C: storeX (abs (), Y); break;
	case 0xE0: cmp (X, x8 ? fetch () : fetch16 (), x8); break;
	case 0xE4: cmp (X, loadX (dp ()), x8); break;
	case 0xEC: cmp (X, loadX (abs ()), x8); break;
	case 0xC0: cmp (Y, x8 ? fetch () : fetch16 (), x8); break;
	case 0xC4: cmp (Y, loadX (dp ()), x8); break;
	case 0xCC: cmp (Y, loadX (abs ()), x8); break;
	case 0x64: storeM (dp (), 0); break;
	case 0x74: storeM (dpX (), 0); break;
	case 0x9C: storeM (abs (), 0); break;
	case 0x9E: storeM (absX (true), 0); break;
	case 0x24: bit (loadM (dp ()), false); break;
	case 0x2C: bit (loadM (abs ()), false); break;
	case 0x34: bit (loadM (dpX ()), false); break;
	case 0x3C: bit (loadM (absX (false)), false); break;

	// ---- branches, jumps, calls ----
	case 0x10: branch (!(P & FN)); break;
	case 0x30: branch (P & FN); break;
	case 0x50: branch (!(P & FV)); break;
	case 0x70: branch (P & FV); break;
	case 0x90: branch (!(P & FC)); break;
	case 0xB0: branch (P & FC); break;
	case 0xD0: branch (!(P & FZ)); break;
	case 0xF0: branch (P & FZ); break;
	case 0x80: branch (true); break;
	case 0x82: { u16 off = fetch16 (); io (); PC = (u16) (PC + off); break; }
	case 0x4C: PC = fetch16 (); break;
	case 0x5C: { u16 a = fetch16 (); PB = fetch (); PC = a; break; }
	case 0x6C: { u16 a = fetch16 (); PC = (u16) (read (a) | read ((u16) (a + 1)) << 8); break; }
	case 0x7C: { u16 a = fetch16 (); io (); u16 p = (u16) (a + X); PC = (u16) (read ((u32) PB << 16 | p) | read ((u32) PB << 16 | (u16) (p + 1)) << 8); break; }
	case 0xDC: { u16 a = fetch16 (); u16 l = (u16) (read (a) | read ((u16) (a + 1)) << 8); PB = read ((u16) (a + 2)); PC = l; break; }
	case 0x20: { u16 a = fetch16 (); io (); u16 r = (u16) (PC - 1); push8 ((u8) (r >> 8)); push8 ((u8) r); PC = a; break; }
	case 0xFC:
	{
		u8 l = fetch ();
		u16 r = PC;
		pushN ((u8) (r >> 8)); pushN ((u8) r);
		u8 h = read ((u32) PB << 16 | PC); PC++;
		io ();
		u16 p = (u16) ((l | h << 8) + X);
		PC = (u16) (read ((u32) PB << 16 | p) | read ((u32) PB << 16 | (u16) (p + 1)) << 8);
		fixE ();
		break;
	}
	case 0x22:
	{
		u16 a = fetch16 ();
		pushN (PB); io ();
		u8 b = fetch ();
		u16 r = (u16) (PC - 1);
		pushN ((u8) (r >> 8)); pushN ((u8) r);
		PB = b; PC = a;
		fixE ();
		break;
	}
	case 0x60: { io (); io (); u16 l = pull8 (); u16 h = pull8 (); PC = (u16) ((l | h << 8) + 1); io (); break; }
	case 0x6B: { io (); io (); u16 l = pullN (); u16 h = pullN (); PB = pullN (); PC = (u16) ((l | h << 8) + 1); fixE (); break; }
	case 0x40:
	{
		io (); io ();
		setP (pull8 ());
		u16 l = pull8 (); u16 h = pull8 (); PC = (u16) (l | h << 8);
		if (!E) PB = pull8 ();
		break;
	}
	case 0x00: fetch (); interrupt (0xFFE6, 0xFFFE, true); break;
	case 0x02: fetch (); interrupt (0xFFE4, 0xFFF4, true); break;

	// ---- the stack ----
	case 0x08: io (); push8 (P); break;
	case 0x28: io (); io (); setP (pull8 ()); break;
	case 0x48: io (); pushM (A, m8); break;
	case 0x68: io (); io (); setA (pullM (m8)); break;
	case 0xDA: io (); pushM (X, x8); break;
	case 0xFA: io (); io (); setX (X, pullM (x8)); break;
	case 0x5A: io (); pushM (Y, x8); break;
	case 0x7A: io (); io (); setX (Y, pullM (x8)); break;
	case 0x8B: io (); push8 (DB); break;
	case 0xAB: io (); io (); DB = pullN (); setZN8 (DB); fixE (); break;
	case 0x4B: io (); push8 (PB); break;
	case 0x0B: io (); pushN ((u8) (D >> 8)); pushN ((u8) D); fixE (); break;
	case 0x2B: { io (); io (); u16 l = pullN (); u16 h = pullN (); D = (u16) (l | h << 8); setZN16 (D); fixE (); break; }
	case 0xF4: { u16 v = fetch16 (); pushN ((u8) (v >> 8)); pushN ((u8) v); fixE (); break; }
	case 0xD4: { u8 off = fetch (); dpIo (); u16 v = dpPtrN (off); pushN ((u8) (v >> 8)); pushN ((u8) v); fixE (); break; }
	case 0x62: { u16 off = fetch16 (); io (); u16 v = (u16) (PC + off); pushN ((u8) (v >> 8)); pushN ((u8) v); fixE (); break; }

	// ---- transfers ----
	case 0xAA: io (); setX (X, A); break;
	case 0xA8: io (); setX (Y, A); break;
	case 0x8A: io (); setA (X); break;
	case 0x98: io (); setA (Y); break;
	case 0x9B: io (); setX (Y, X); break;
	case 0xBB: io (); setX (X, Y); break;
	case 0xBA: io (); setX (X, S); break;
	case 0x9A: io (); S = E ? (u16) (0x100 | (X & 0xFF)) : X; break;
	case 0x1B: io (); S = E ? (u16) (0x100 | (A & 0xFF)) : A; break;
	case 0x3B: io (); A = S; setZN16 (A); break;
	case 0x5B: io (); D = A; setZN16 (D); break;
	case 0x7B: io (); A = D; setZN16 (A); break;
	case 0xEB: io (); io (); A = (u16) (A >> 8 | A << 8); setZN8 ((u8) A); break;

	// ---- flags, modes ----
	case 0x18: io (); P &= ~FC; break;
	case 0x38: io (); P |= FC; break;
	case 0x58: io (); P &= ~FI; break;
	case 0x78: io (); P |= FI; break;
	case 0xD8: io (); P &= ~FD; break;
	case 0xF8: io (); P |= FD; break;
	case 0xB8: io (); P &= ~FV; break;
	case 0xC2: { u8 v = fetch (); io (); setP ((u8) (P & ~v)); break; }
	case 0xE2: { u8 v = fetch (); io (); setP ((u8) (P | v)); break; }
	case 0xFB:
	{
		io ();
		bool c = P & FC;
		P = (u8) ((P & ~FC) | (E ? FC : 0));
		E = c;
		if (E) { P |= 0x30; X &= 0xFF; Y &= 0xFF; S = (u16) (0x100 | (S & 0xFF)); }
		break;
	}

	// ---- block moves ----
	case 0x54: case 0x44:
	{
		u8 dst = fetch (), src = fetch ();
		DB = dst;
		u8 v = read ((u32) src << 16 | X);
		write ((u32) dst << 16 | Y, v);
		io (); io ();
		if (op == 0x54) { X++; Y++; } else { X--; Y--; }
		if (x8) { X &= 0xFF; Y &= 0xFF; }
		A--;
		if (A != 0xFFFF) PC = (u16) (PC - 3);
		break;
	}

	// ---- the rest ----
	case 0xEA: io (); break;
	case 0x42: fetch (); break;
	case 0xCB: io (); io (); waiting = true; break;
	case 0xDB: io (); io (); stopped = true; break;
	default: break;
	}
}

} // namespace snes
