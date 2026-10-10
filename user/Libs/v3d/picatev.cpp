//
// v3d/picatev.cpp -- the PICA200's fragment combiner as a V3D fragment shader (picatev.h).
//
// The generator's machinery is v3d/gxtev.cpp's (values with their ranges, one ALU operation an instruction, the
// constants and the uniforms through r5, the register-file hazards, the texture lookups before the thread
// switch, the alpha test by SETMSF, the TLB's writes), with registers given back when their value is read for
// the last time: a configuration is first followed stage by stage to know where each operand's channels come
// from (the "previous" and the buffer are only names for an earlier value), then backwards to know which values
// the output needs and until when.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener (see docs/LICENSING.md)
//
#include "v3d/picatev.h"
#include <stdint.h>

namespace picatev
{
using namespace qpu;

namespace
{

enum { VK_CONST, VK_RF, VK_ACC, VK_UNIF };
enum Op { O_ADD, O_SUB, O_SHL, O_SHR, O_ASR, O_AND, O_OR, O_XOR, O_MIN, O_MAX, O_MUL, O_NOT, O_NEG, O_MOV };
struct Val { int k, v; unsigned char uk, ua, ub; long long lo, hi; };	// lo..hi: what it can be
const long long WIDE = 1ll << 31;
inline Val cnst (int v) { Val x; x.k = VK_CONST; x.v = v; x.uk = x.ua = x.ub = 0; x.lo = x.hi = v; return x; }
inline Val inRf (int r, long long lo = -WIDE, long long hi = WIDE - 1) { Val x; x.k = VK_RF; x.v = r; x.uk = x.ua = x.ub = 0; x.lo = lo; x.hi = hi; return x; }
inline Val unif (int kind, int a, int b)
{
	Val x; x.k = VK_UNIF; x.v = 0; x.uk = (unsigned char) kind; x.ua = (unsigned char) a; x.ub = (unsigned char) b;
	const bool byte = kind == U_KONST || kind == U_BUFFER || kind == U_ALPHAREF;
	x.lo = byte ? 0 : -WIDE; x.hi = byte ? 255 : WIDE - 1;
	return x;
}
// the range of o (x, y)
void range (int o, const Val &x, const Val &y, long long &lo, long long &hi)
{
	lo = -WIDE; hi = WIDE - 1;
	long long n = y.lo == y.hi ? (y.lo & 31) : -1;
	switch (o)
	{
	case O_ADD: lo = x.lo + y.lo; hi = x.hi + y.hi; break;
	case O_SUB: lo = x.lo - y.hi; hi = x.hi - y.lo; break;
	case O_SHL: if (n >= 0 && x.lo >= -(1ll << 20) && x.hi < (1ll << 20) && n <= 8) { lo = x.lo * (1ll << n); hi = x.hi * (1ll << n); } break;
	case O_ASR: if (n >= 0) { lo = x.lo >> n; hi = x.hi >> n; } else { lo = x.lo < 0 ? x.lo : 0; hi = x.hi > 0 ? x.hi : 0; } break;
	case O_SHR: if (n >= 0 && x.lo >= 0) { lo = x.lo >> n; hi = x.hi >> n; } break;
	case O_AND:
		if (y.lo == y.hi && y.lo >= 0) { lo = 0; hi = x.lo >= 0 && x.hi < y.lo ? x.hi : y.lo; }
		else if (x.lo >= 0 || y.lo >= 0) { lo = 0; hi = x.lo >= 0 ? x.hi : y.hi; }
		break;
	case O_MIN: lo = x.lo < y.lo ? x.lo : y.lo; hi = x.hi < y.hi ? x.hi : y.hi; break;
	case O_MAX: lo = x.lo > y.lo ? x.lo : y.lo; hi = x.hi > y.hi ? x.hi : y.hi; break;
	case O_MUL:
		if (x.lo >= -(1 << 23) && x.hi < (1 << 23) && y.lo >= -(1 << 23) && y.hi < (1 << 23))
		{
			long long p[4] = { x.lo * y.lo, x.lo * y.hi, x.hi * y.lo, x.hi * y.hi };
			lo = hi = p[0];
			for (int k = 1; k < 4; k++) { if (p[k] < lo) lo = p[k]; if (p[k] > hi) hi = p[k]; }
		}
		break;
	case O_NOT: lo = ~x.hi; hi = ~x.lo; break;
	case O_NEG: lo = -x.hi; hi = -x.lo; break;
	case O_MOV: lo = x.lo; hi = x.hi; break;
	default: break;
	}
	if (lo < -WIDE || hi > WIDE - 1) { lo = -WIDE; hi = WIDE - 1; }
}

int fold (int o, int x, int y)
{
	unsigned ux = (unsigned) x, uy = (unsigned) y;
	switch (o)
	{
	case O_ADD: return (int) (ux + uy);
	case O_SUB: return (int) (ux - uy);
	case O_SHL: return (int) (ux << (uy & 31));
	case O_SHR: return (int) (ux >> (uy & 31));
	case O_ASR: return x >> (uy & 31);
	case O_AND: return x & y;
	case O_OR: return x | y;
	case O_XOR: return x ^ y;
	case O_MIN: return x < y ? x : y;
	case O_MAX: return x > y ? x : y;
	case O_MUL: { int a = (int) (ux << 8) >> 8, b = (int) (uy << 8) >> 8; return (int) ((unsigned) a * (unsigned) b); }
	case O_NOT: return ~x;
	case O_NEG: return (int) (0u - ux);
	default: return x;
	}
}
bool isShift (int o) { return o == O_SHL || o == O_SHR || o == O_ASR; }

// where a channel's value comes from: an input (0 the vertex colour, 1 2 the lighting's, 3..5 the textures), 6 the
// neutral 0 0 0 255, 7 zero, 8 + s the stage s's result, 14 the stage st's constant, 15 the buffer's colour
enum { ID_NEUTRAL = 6, ID_ZERO = 7, ID_STAGE = 8, ID_KONST = 14, ID_BUFFER = 15 };
struct Ref { unsigned char id, st; };

// the source channel an operand reads for the output channel j (colour), or for the alpha
inline int colChan (unsigned op, int j) { return op == 2 || op == 3 ? 3 : op == 4 || op == 5 ? 0 : op == 8 || op == 9 ? 1 : op == 12 || op == 13 ? 2 : j; }
inline bool colInv (unsigned op) { return op == 1 || op == 3 || op == 5 || op == 9 || op == 13; }
inline int alphaChan (unsigned op) { return op < 2 ? 3 : op < 4 ? 0 : op < 6 ? 1 : 2; }
inline int operands (unsigned mode) { return mode == 4 || mode == 8 || mode == 9 ? 3 : mode == 1 || mode == 2 || mode == 3 || mode == 5 || mode == 6 || mode == 7 ? 2 : 1; }

struct Gen
{
	const Config &c;
	Shader &s;
	unsigned long long prevW = 0;		// the register-file registers the previous instruction wrote
	bool prevLdvary = false;
	int accBusy = 0;			// r0..r3
	unsigned long long rfUsed = 1ull;	// rf0: W (the varyings')
	int rfRefs[64];				// how many values a register holds (a value passed on unchanged shares it)
	int inUse = 0;
	int K255 = -1, F255 = -1, INV255 = -1;
	I lastI; int lastIdx = -1, lastAcc = -1, lastAlu = 0;	// (the last instruction: an accumulator it wrote)
	bool failed = false;
	// the configuration followed: each operand's channels, the output's; what is needed and until which stage
	Ref cref[6][3][4], aref[6][3][4], outRef[4];
	bool need[16][4]; int lastUse[16][4];
	int regOf[16][4];

	Gen (const Config &cc, Shader &ss) : c (cc), s (ss)
	{
		for (int i = 0; i < 64; i++) rfRefs[i] = 0;
		for (int i = 0; i < 16; i++) for (int k = 0; k < 4; k++) { need[i][k] = false; lastUse[i][k] = -1; regOf[i][k] = -1; }
	}
	void fail (const char *e) { if (!failed) s.err = e; failed = true; }
	void claim (int reg, long long lo, long long hi)
	{
		if (s.nClaim < Shader::MAX_CLAIMS && (lo > -WIDE || hi < WIDE - 1))
		{ Shader::Claim &k = s.claim[s.nClaim++]; k.ip = s.prog.count () - 1; k.reg = reg; k.lo = lo; k.hi = hi; }
	}

	int allocRf ()
	{
		for (int r = 1; r < rfLimit (); r++)
			if (!(rfUsed >> r & 1))
			{
				rfUsed |= 1ull << r; rfRefs[r] = 1;
				if (++inUse > s.nRegs) s.nRegs = inUse;
				return r;
			}
		fail (rfLimit () < 32 ? "more registers than V3D 7.1 leaves (25)" : "more than 32 registers");
		return 31;
	}
	void releaseRf (int r) { if (r > 0 && rfRefs[r] > 0 && --rfRefs[r] == 0) { rfUsed &= ~(1ull << r); inUse--; } }
	int allocAcc ()
	{
		for (int a = 0; a < 4; a++) if (!(accBusy >> a & 1)) { accBusy |= 1 << a; return a; }
		fail ("out of accumulators");
		return 0;
	}
	void freeV (const Val &x) { if (x.k == VK_ACC) accBusy &= ~(1 << x.v); }
	int addUni (int kind, int a, int b, unsigned v)
	{
		if (s.nUni >= MAX_UNI) { fail ("too many uniforms"); return 0; }
		Uni &u = s.uni[s.nUni];
		u.kind = (unsigned char) kind; u.a = (unsigned char) a; u.b = (unsigned char) b; u.value = v;
		return s.nUni++;
	}

	// ---- the instructions
	void emit (const I &i, unsigned long long reads, unsigned long long writes, bool ldvary = false, bool unifSig = false)
	{
		if ((reads & prevW) || (unifSig && prevLdvary)) s.prog << I ();
		s.prog << i;
		prevW = writes; prevLdvary = ldvary;
		lastAcc = -1;
	}
	void nop_ () { emit (I (), 0, 0); }
	void ldunif (int kind, int a, int b, unsigned v) { addUni (kind, a, b, v); emit (I ().ldunif (), 0, 0, false, true); }
	void ldunifrf (int r, int kind, int a, int b, unsigned v) { addUni (kind, a, b, v); emit (I ().ldunifrf (rf (r)), 0, 1ull << r, false, true); }

	bool smallImm (int v, int o) const { return isShift (o) || (v >= -16 && v <= 15); }
	static R accR (int a) { return a == 0 ? r0 : a == 1 ? r1 : a == 2 ? r2 : r3; }
	R operand (const Val &x, int o, unsigned long long &reads)
	{
		switch (x.k)
		{
		case VK_CONST:
			if (isShift (o)) { int n = x.v & 31; return imm ((u32) (n > 15 ? n - 32 : n)); }
			if (x.v >= -16 && x.v <= 15) return imm ((u32) x.v);
			return r5;
		case VK_RF: reads |= 1ull << x.v; return rf (x.v);
		case VK_ACC: return accR (x.v);
		default: return r5;
		}
	}
	bool needR5 (const Val &x, int o) const { return (x.k == VK_CONST && !smallImm (x.v, o)) || x.k == VK_UNIF; }
	void loadR5 (const Val &x)
	{
		if (x.k == VK_CONST) ldunif (U_CONST, 0, 0, (unsigned) x.v);
		else ldunif (x.uk, x.ua, x.ub, 0);
	}
	Val toAcc (const Val &x)
	{
		unsigned long long rd = 0;
		if (needR5 (x, O_MOV)) loadR5 (x);
		R src = operand (x, O_MOV, rd);
		freeV (x);
		Val d; d.k = VK_ACC; d.v = allocAcc (); d.uk = d.ua = d.ub = 0; d.lo = x.lo; d.hi = x.hi;
		emit (I ().m (V3D_QPU_M_MOV, accR (d.v), src), rd, 0);
		return d;
	}
	// o (x, y) -> a new value (into rf dst when dst >= 0); x and y are consumed (their accumulators freed)
	Val op (int o, Val x, Val y, int dst = -1)
	{
		bool unary = o == O_NOT || o == O_NEG || o == O_MOV;
		if (x.k == VK_CONST && (unary || y.k == VK_CONST))
		{
			int v = fold (o, x.v, unary ? 0 : y.v);
			if (dst < 0) return cnst (v);
			x = cnst (v); o = O_MOV; unary = true;
		}
		if (!unary && y.k == VK_CONST)
		{
			if ((o == O_ADD || o == O_SUB || o == O_OR || o == O_XOR || isShift (o)) && (isShift (o) ? (y.v & 31) == 0 : y.v == 0))
			{ o = O_MOV; unary = true; }
			else if (o == O_MUL && y.v == 0) { freeV (x); return dst < 0 ? cnst (0) : op (O_MOV, cnst (0), cnst (0), dst); }
			else if (o == O_MUL && y.v == 1) { o = O_MOV; unary = true; }
			else if (o == O_MUL && y.v > 0 && (y.v & (y.v - 1)) == 0) { int n = 0; while ((1 << n) != y.v) n++; y = cnst (n); o = O_SHL; }
		}
		if (unary && o == O_MOV && dst < 0) return x;
		if (!unary && x.k == VK_CONST && (o == O_ADD || o == O_MUL || o == O_AND || o == O_OR || o == O_XOR || o == O_MIN || o == O_MAX))
		{ Val t = x; x = y; y = t; return op (o, x, y, dst); }	// (the constant second)
		if (!unary && needR5 (x, o) && needR5 (y, o)) x = toAcc (x);
		if (unary) { if (needR5 (x, O_MOV)) loadR5 (x); }
		else if (needR5 (x, o)) loadR5 (x);
		else if (needR5 (y, o)) loadR5 (y);
		unsigned long long rd = 0;
		R a = operand (x, unary ? O_MOV : o, rd), b = unary ? none : operand (y, o, rd);
		freeV (x); if (!unary) freeV (y);
		Val d; d.uk = d.ua = d.ub = 0;
		range (o, x, unary ? cnst (0) : y, d.lo, d.hi);
		R dr;
		if (dst >= 0) { d.k = VK_RF; d.v = dst; dr = rf (dst); }
		else { d.k = VK_ACC; d.v = allocAcc (); dr = accR (d.v); }
		unsigned long long wr = dst >= 0 ? 1ull << dst : 0;
		I i;
		switch (o)
		{
		case O_ADD: i.a (V3D_QPU_A_ADD, dr, a, b); break;
		case O_SUB: i.a (V3D_QPU_A_SUB, dr, a, b); break;
		case O_SHL: i.a (V3D_QPU_A_SHL, dr, a, b); break;
		case O_SHR: i.a (V3D_QPU_A_SHR, dr, a, b); break;
		case O_ASR: i.a (V3D_QPU_A_ASR, dr, a, b); break;
		case O_AND: i.a (V3D_QPU_A_AND, dr, a, b); break;
		case O_OR: i.a (V3D_QPU_A_OR, dr, a, b); break;
		case O_XOR: i.a (V3D_QPU_A_XOR, dr, a, b); break;
		case O_MIN: i.a (V3D_QPU_A_MIN, dr, a, b); break;
		case O_MAX: i.a (V3D_QPU_A_MAX, dr, a, b); break;
		case O_MUL: i.m (V3D_QPU_M_SMUL24, dr, a, b); break;
		case O_NOT: i.a (V3D_QPU_A_NOT, dr, a); break;
		case O_NEG: i.a (V3D_QPU_A_NEG, dr, a); break;
		default: i.m (V3D_QPU_M_MOV, dr, a); break;
		}
		emit (i, rd, wr);
		if (dst < 0) { lastI = i; lastIdx = s.prog.count () - 1; lastAcc = d.v; lastAlu = o == O_MUL || o == O_MOV; }
		claim (dst < 0 ? d.v : 6 + dst, d.lo, d.hi);
		return d;
	}
	Val dup (const Val &x)			// (a copy, x kept)
	{
		if (x.k != VK_ACC) return x;
		Val d; d.k = VK_ACC; d.v = allocAcc (); d.uk = d.ua = d.ub = 0; d.lo = x.lo; d.hi = x.hi;
		emit (I ().m (V3D_QPU_M_MOV, accR (d.v), accR (x.v)), 0, 0);
		return d;
	}
	// x into rf dst: the instruction that just made x writes dst instead when it can
	void store (Val x, int dst)
	{
		if (x.k == VK_ACC && x.v == lastAcc && lastIdx == s.prog.count () - 1)
		{
			uint8_t w = (uint8_t) physical (rf (dst));		// (the register itself: 7.1 puts it elsewhere, qpu.h)
			if (lastAlu) { lastI.in.alu.mul.waddr = w; lastI.in.alu.mul.magic_write = false; }
			else { lastI.in.alu.add.waddr = w; lastI.in.alu.add.magic_write = false; }
			s.prog.set (lastIdx, lastI);
			if (s.nClaim && s.claim[s.nClaim - 1].ip == lastIdx && s.claim[s.nClaim - 1].reg == x.v) s.claim[s.nClaim - 1].reg = 6 + dst;
			prevW |= 1ull << dst; lastAcc = -1;
			freeV (x);
			return;
		}
		op (O_MOV, x, cnst (0), dst);
	}
	Val k255 () { return inRf (K255, 255, 255); }
	// r brought into 0..255, into rf dst
	void clampTo (Val r, int dst)
	{
		const bool needLo = r.lo < 0, needHi = r.hi > 255;
		if (needLo && needHi) { r = op (O_MAX, r, cnst (0)); op (O_MIN, r, k255 (), dst); }
		else if (needLo) op (O_MAX, r, cnst (0), dst);
		else if (needHi) op (O_MIN, r, k255 (), dst);
		else store (r, dst);
	}
	// x / 255 for 0 <= x < 65535, exactly: (x + 1 + (x >> 8)) >> 8
	Val div255 (Val x)
	{
		if (x.k != VK_ACC) x = toAcc (x);
		Val h = op (O_SHR, dup (x), cnst (8));
		Val t = op (O_ADD, x, cnst (1));
		t = op (O_ADD, t, h);
		Val r = op (O_SHR, t, cnst (8));
		if (r.hi > 255) r.hi = 255;
		if (r.lo < 0) r.lo = 0;
		return r;
	}
	Val gtMask (Val x, Val y) { return op (O_ASR, op (O_SUB, y, x), cnst (31)); }	// -1 where x > y, else 0
	Val eqMask (Val x, Val y)
	{
		Val d = op (O_SUB, x, y);
		if (d.k != VK_ACC) d = toAcc (d);
		Val n = op (O_NEG, dup (d), cnst (0));
		return op (O_NOT, op (O_ASR, op (O_OR, d, n), cnst (31)), cnst (0));
	}

	// ---- the configuration followed
	Ref resolve (unsigned src, int ch, int st, const Ref *prev, const Ref *buffer)
	{
		Ref r; r.st = (unsigned char) st;
		switch (src & 15)
		{
		case 0: r.id = 0; break;
		case 1: r.id = c.lit ? 1 : 0; break;
		case 2: r.id = c.lit ? 2 : ID_NEUTRAL; break;
		case 3: case 4: case 5: r.id = (c.texOn >> ((src & 15) - 3) & 1) ? (unsigned char) (src & 15) : (unsigned char) ID_NEUTRAL; break;
		case 6: r.id = ID_NEUTRAL; s.unsupported = true; break;
		case 13: return buffer[ch];
		case 14: r.id = ID_KONST; break;
		default: return prev[ch];
		}
		return r;
	}
	void mark (const Ref &r, int ch, int at)
	{
		if (r.id >= ID_KONST || r.id == ID_NEUTRAL || r.id == ID_ZERO) return;
		need[r.id][ch] = true;
		if (at > lastUse[r.id][ch]) lastUse[r.id][ch] = at;
	}
	void follow ()
	{
		Ref prev[4], buffer[4], next[4];
		for (int ch = 0; ch < 4; ch++) { prev[ch].id = 0; prev[ch].st = 0; buffer[ch].id = ID_ZERO; buffer[ch].st = 0; next[ch].id = ID_BUFFER; next[ch].st = 0; }
		for (int st = 0; st < 6; st++)
		{
			const Config::Stage &sg = c.stage[st];
			if (!sg.pass)
			{
				for (int k = 0; k < 3; k++)
					for (int ch = 0; ch < 4; ch++)
					{
						cref[st][k][ch] = resolve (sg.src[k], ch, st, prev, buffer);
						aref[st][k][ch] = resolve (sg.srcA[k], ch, st, prev, buffer);
					}
				for (int ch = 0; ch < 4; ch++) { prev[ch].id = (unsigned char) (ID_STAGE + st); prev[ch].st = (unsigned char) st; }
			}
			for (int ch = 0; ch < 4; ch++) buffer[ch] = next[ch];
			if (st < 4)
			{
				if (c.updateRgb >> st & 1) { next[0] = prev[0]; next[1] = prev[1]; next[2] = prev[2]; }
				if (c.updateA >> st & 1) next[3] = prev[3];
			}
		}
		for (int ch = 0; ch < 4; ch++) { outRef[ch] = prev[ch]; mark (prev[ch], ch, 6); }
		for (int st = 5; st >= 0; st--)
		{
			const Config::Stage &sg = c.stage[st];
			if (sg.pass) continue;
			const bool *nd = need[ID_STAGE + st];
			const bool dot = sg.mode == 6 || sg.mode == 7;
			const bool colour = nd[0] || nd[1] || nd[2];
			if (dot && (colour || (sg.mode == 7 && nd[3])))
				for (int k = 0; k < 2; k++) for (int j = 0; j < 3; j++) { const int ch = colChan (sg.op[k], j); mark (cref[st][k][ch], ch, st); }
			else if (colour)
				for (int k = 0; k < operands (sg.mode); k++)
					for (int j = 0; j < 3; j++) if (nd[j]) { const int ch = colChan (sg.op[k], j); mark (cref[st][k][ch], ch, st); }
			if (nd[3] && sg.mode != 7)
				for (int k = 0; k < operands (sg.modeA); k++) { const int ch = alphaChan (sg.opA[k]); mark (aref[st][k][ch], ch, st); }
		}
	}
	Val val (const Ref &r, int ch)
	{
		switch (r.id)
		{
		case ID_NEUTRAL: return cnst (ch == 3 ? 255 : 0);
		case ID_ZERO: return cnst (0);
		case ID_KONST: return unif (U_KONST, r.st, ch);
		case ID_BUFFER: return unif (U_BUFFER, 0, ch);
		default: return inRf (regOf[r.id][ch], 0, 255);
		}
	}
	Val colIn (int st, int k, int j)
	{
		const unsigned o = c.stage[st].op[k];
		const int ch = colChan (o, j);
		Val x = val (cref[st][k][ch], ch);
		return colInv (o) ? op (O_SUB, k255 (), x) : x;
	}
	Val alphaIn (int st, int k)
	{
		const unsigned o = c.stage[st].opA[k];
		const int ch = alphaChan (o);
		Val x = val (aref[st][k][ch], ch);
		return (o & 1) ? op (O_SUB, k255 (), x) : x;
	}
	// a mode's result from its operands (in (k): operand k, asked as often as it is read), before the scale
	template <class F> Val combine (unsigned mode, F in)
	{
		switch (mode)
		{
		case 1: return div255 (op (O_MUL, in (0), in (1)));
		case 2: return op (O_ADD, in (0), in (1));
		case 3: return op (O_SUB, op (O_ADD, in (0), in (1)), cnst (128));
		case 4:
		{
			Val x = op (O_MUL, in (0), in (2));
			Val y = op (O_MUL, in (1), op (O_SUB, k255 (), in (2)));
			return div255 (op (O_ADD, x, y));
		}
		case 5: return op (O_SUB, in (0), in (1));
		case 8: return op (O_ADD, div255 (op (O_MUL, in (0), in (1))), in (2));
		case 9: return div255 (op (O_MIN, op (O_MUL, op (O_ADD, in (0), in (1)), in (2)), cnst (65279)));	// (more is 255 anyway)
		default: return in (0);
		}
	}
	// a stage's result r, scaled and brought into 0..255, as the value (id, ch): in a register of its own, or the
	// register it already is in when nothing changes it
	void result (Val r, int scale, int id, int ch)
	{
		if (scale > 1) r = op (O_MUL, r, cnst (scale));
		if (r.k == VK_RF && r.lo >= 0 && r.hi <= 255) { regOf[id][ch] = r.v; rfRefs[r.v]++; return; }
		const int d = allocRf ();
		regOf[id][ch] = d;
		clampTo (r, d);
	}
	void stage (int st)
	{
		const Config::Stage &sg = c.stage[st];
		const int id = ID_STAGE + st;
		const bool *nd = need[id];
		const bool dot = sg.mode == 6 || sg.mode == 7;
		if (dot && (nd[0] || nd[1] || nd[2] || (sg.mode == 7 && nd[3])))
		{
			// the dot product of two colours as vectors (2 x - 255 a channel), over 255, not below 0
			Val sum = cnst (0);
			for (int j = 0; j < 3; j++)
			{
				Val a = op (O_SUB, op (O_SHL, colIn (st, 0, j), cnst (1)), cnst (255));
				Val b = op (O_SUB, op (O_SHL, colIn (st, 1, j), cnst (1)), cnst (255));
				sum = op (O_ADD, sum, op (O_MUL, a, b));
			}
			Val d = div255 (op (O_MIN, op (O_MAX, sum, cnst (0)), cnst (65279)));
			const int keep = allocRf ();
			store (d, keep);
			const Val kept = inRf (keep, 0, 255);
			int first = -1;
			for (int j = 0; j < 3; j++)
			{
				if (!nd[j]) continue;
				if (first < 0) { result (kept, sg.scale, id, j); first = j; }
				else { regOf[id][j] = regOf[id][first]; rfRefs[regOf[id][j]]++; }
			}
			if (sg.mode == 7 && nd[3])
			{
				if (first >= 0 && sg.scaleA == sg.scale) { regOf[id][3] = regOf[id][first]; rfRefs[regOf[id][3]]++; }
				else result (kept, sg.scaleA, id, 3);
			}
			releaseRf (keep);
		}
		else for (int j = 0; j < 3; j++)
			if (nd[j]) result (combine (sg.mode, [&] (int k) { return colIn (st, k, j); }), sg.scale, id, j);
		if (nd[3] && sg.mode != 7) result (combine (sg.modeA, [&] (int k) { return alphaIn (st, k); }), sg.scaleA, id, 3);
		if (accBusy) fail ("an accumulator left busy");
		// what was read here for the last time
		for (int i = 0; i < 16; i++)
			for (int ch = 0; ch < 4; ch++)
				if (regOf[i][ch] >= 0 && lastUse[i][ch] <= st && !(i == id)) { releaseRf (regOf[i][ch]); regOf[i][ch] = -1; }
		for (int ch = 0; ch < 4; ch++) if (regOf[id][ch] >= 0 && lastUse[id][ch] < 0) { releaseRf (regOf[id][ch]); regOf[id][ch] = -1; }
	}

	// ---- the inputs
	// r2 = x * 255
	void times255 (R x) { emit (I ().m (V3D_QPU_M_FMUL, r2, x, rf (F255)), 1ull << F255, 0); }
	void colours (int id, int kind)
	{
		for (int ch = 0; ch < 4; ch++)
		{
			if (!need[id][ch]) continue;
			if (s.nVary >= 32) { fail ("too many varyings"); return; }
			Vary &v = s.vary[s.nVary++]; v.kind = (unsigned char) kind; v.a = (unsigned char) ch; v.b = 0;
			const int reg = allocRf ();
			regOf[id][ch] = reg;
			emit (I ().ldvary (r0), 0, 0, true);
			emit (I ().m (V3D_QPU_M_FMUL, r1, r0, rf (0)), 1, 0);
			emit (I ().a (V3D_QPU_A_FADD, r1, r1, r5), 0, 0);
			times255 (r1);
			// (rounded; a colour outside 0..1 is brought back, as the software renderer does)
			Val x; x.k = VK_ACC; x.v = allocAcc (); x.uk = x.ua = x.ub = 0; x.lo = -WIDE; x.hi = WIDE - 1;
			emit (I ().a (V3D_QPU_A_FTOIN, accR (x.v), r2), 0, 0);
			x = op (O_MAX, x, cnst (0));
			op (O_MIN, x, k255 (), reg);
		}
	}
	void lookups ()
	{
		for (int L = 0; L < s.nLook; L++)
		{
			for (int k = 0; k < 2; k++)
			{
				if (s.nVary >= 32) { fail ("too many varyings"); return; }
				Vary &v = s.vary[s.nVary++]; v.kind = V_COORD; v.a = (unsigned char) L; v.b = (unsigned char) k;
				R dst = k == 0 ? r1 : r2;
				emit (I ().ldvary (r0), 0, 0, true);
				emit (I ().m (V3D_QPU_M_FMUL, dst, r0, rf (0)), 1, 0);
				emit (I ().a (V3D_QPU_A_FADD, dst, dst, r5), 0, 0);
			}
			emit (I ().m (V3D_QPU_M_MOV, tmut, r2), 0, 0);
			addUni (U_TEXP0, L, 0, 0); emit (I ().wrtmuc (), 0, 0, false, true);
			addUni (U_TEXP1, L, 0, 0); emit (I ().wrtmuc (), 0, 0, false, true);
			emit (I ().m (V3D_QPU_M_MOV, tmus, r1), 0, 0);
		}
		// the thread switch (the last one: doubled); only the registers live across it
		emit (I ().thrsw (), 0, 0);
		emit (I ().thrsw (), 0, 0);
		nop_ (); nop_ ();
		for (int L = 0; L < s.nLook; L++)
		{
			const int id = 3 + s.look[L].unit;
			emit (I ().ldtmu (r0), 0, 0);
			emit (I ().ldtmu (r1), 0, 0);
			for (int ch = 0; ch < 4; ch++)
			{
				if (!need[id][ch]) continue;
				const int reg = allocRf ();
				regOf[id][ch] = reg;
				R x = ch == 0 ? l (r0) : ch == 1 ? h (r0) : ch == 2 ? l (r1) : h (r1);
				times255 (x);
				emit (I ().a (V3D_QPU_A_FTOIN, rf (reg), r2), 0, 1ull << reg);
			}
		}
	}

	void run ()
	{
		follow ();
		K255 = allocRf (); ldunifrf (K255, U_CONST, 0, 0, 255);
		F255 = allocRf (); ldunifrf (F255, U_CONST, 0, 0, 0x437F0000);
		INV255 = allocRf (); { union { float f; unsigned u; } k; k.f = 1.0f / 255.0f; ldunifrf (INV255, U_CONST, 0, 0, k.u); }
		colours (0, V_PRIMARY);
		colours (1, V_LITP);
		colours (2, V_LITS);
		for (int u = 0; u < 3; u++)
			if (need[3 + u][0] || need[3 + u][1] || need[3 + u][2] || need[3 + u][3]) s.look[s.nLook++].unit = (unsigned char) u;
		if (failed) return;
		if (accBusy) { fail ("an accumulator live at the thread switch"); return; }
		lookups ();
		for (int st = 0; st < 6 && !failed; st++) if (!c.stage[st].pass) stage (st);
		if (failed) return;
		// the output; the alpha test
		Val out[4];
		for (int ch = 0; ch < 4; ch++) out[ch] = val (outRef[ch], ch);
		if (c.alphaTest && (c.alphaFunc & 7) != 1)
		{
			Val pass;
			const Val ref = unif (U_ALPHAREF, 0, 0);
			switch (c.alphaFunc & 7)
			{
			case 0: pass = cnst (0); break;
			case 2: pass = eqMask (out[3], ref); break;
			case 3: pass = op (O_NOT, eqMask (out[3], ref), cnst (0)); break;
			case 4: pass = gtMask (ref, out[3]); break;					// a < r
			case 5: pass = op (O_NOT, gtMask (out[3], ref), cnst (0)); break;		// a <= r
			case 6: pass = gtMask (out[3], ref); break;
			default: pass = op (O_NOT, gtMask (ref, out[3]), cnst (0)); break;		// a >= r
			}
			if (pass.k == VK_CONST && pass.v == -1) ;					// (always)
			else
			{
				if (pass.k != VK_ACC) pass = toAcc (pass);
				unsigned long long rd = 0;
				R pm = operand (pass, O_OR, rd);
				emit (I ().a (V3D_QPU_A_OR, nop, pm, pm).apf (V3D_QPU_PF_PUSHZ), rd, 0);	// flag A: refused
				freeV (pass);
				emit (I ().a (V3D_QPU_A_SETMSF, nop, imm (0)).ac (V3D_QPU_COND_IFA), 0, 0);
			}
		}
		// the channels as floats 0..1 in r0..r3
		for (int ch = 0; ch < 4; ch++)
		{
			R d = accR (ch);
			unsigned long long rd = 0;
			if (needR5 (out[ch], O_MOV)) loadR5 (out[ch]);
			R x = operand (out[ch], O_MOV, rd);
			emit (I ().a (V3D_QPU_A_ITOF, d, x), rd, 0);
			emit (I ().m (V3D_QPU_M_FMUL, d, d, rf (INV255)), 1ull << INV255, 0);
		}
		emit (I ().a (V3D_QPU_A_VFPACK, tlb, r0, r1).thrsw (), 0, 0);		// (end)
		emit (I ().a (V3D_QPU_A_VFPACK, tlb, r2, r3), 0, 0);
		nop_ ();
	}
};

} // namespace

bool build (const Config &c, Shader &s)
{
	s.prog.n = 0; s.prog.bad = false; s.prog.badAt = -1;
	s.nUni = 0; s.nVary = 0; s.nLook = 0; s.flags = 0; s.nRegs = 0; s.err = 0; s.unsupported = false; s.nClaim = 0;
	Gen g (c, s);
	g.run ();
	if (!g.failed && !s.prog.ok ()) { s.err = "an instruction could not be encoded"; g.failed = true; }
	return !g.failed;
}

unsigned long long key (const Config &c)
{
	unsigned long long h = 1469598103934665603ull;
	auto mix = [&h] (unsigned v) { h ^= v & 255; h *= 1099511628211ull; };
	for (int st = 0; st < 6; st++)
	{
		const Config::Stage &g = c.stage[st];
		mix (g.pass);
		if (g.pass) continue;
		for (int k = 0; k < 3; k++) { mix (g.src[k]); mix (g.srcA[k]); mix (g.op[k]); mix (g.opA[k]); }
		mix (g.mode); mix (g.modeA); mix (g.scale); mix (g.scaleA);
	}
	mix (c.updateRgb); mix (c.updateA); mix (c.alphaTest); mix (c.alphaTest ? c.alphaFunc : 0); mix (c.texOn); mix (c.lit);
	return h;
}

} // namespace picatev
