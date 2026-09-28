//
// v3d/gxtev.cpp -- see gxtev.h. A small code generator: the values are constants (folded while
// generating), register-file registers (the TEV's registers, the rasterized colours, the texel),
// accumulators r0..r3 (the temporaries) or uniforms (read through r5 by an ldunif just before the
// instruction that uses them). One ALU operation an instruction; a NOP where an instruction would
// read a register-file register the previous one wrote (the hardware reads the old value).
//
// The program: the constants and the registers' initial values (ldunifrf), the rasterized colours
// (ldvary -> x W + C -> x 255 -> rounded integers), then the stages in order. The texture lookups
// go in groups of 4 (the TMU's output FIFO: 8 words a thread with 2 threads): a group's coordinates
// are read (ldvary) and its lookups started when its first stage comes, then a thread switch (the
// last group's is the last-segment pair; without lookups the pair comes after the stages); each
// stage reads its two words (ldtmu) -- R G, B A as f16 -- and turns them into 0..255. The end:
// PREV & 255, the alpha test (setmsf), the EFB's format, the TLB writes.
//
#include "v3d/gxtev.h"

namespace gxtev
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
	bool byte = kind == U_KONST || kind == U_ALPHAREF;
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
		if (y.lo == -1 && y.hi == 0) { lo = x.lo < 0 ? x.lo : 0; hi = x.hi > 0 ? x.hi : 0; }	// (a mask: x or 0)
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

struct Gen
{
	const Config &c;
	Shader &s;
	unsigned long long prevW = 0;		// the register-file registers the previous instruction wrote
	bool prevLdvary = false;
	int accBusy = 0;			// r0..r3
	unsigned long long rfUsed = 1ull;	// rf0: W (the varyings'; rf2's Z is free while the fog / z texture are not generated)
	int K255 = -1;
	int reg[4][4];				// the TEV registers' channels (-1: not used)
	long long rlo[4][4], rhi[4][4];		// what their values can be
	int F255 = -1, INV255 = -1;		// (float 255, 1 / 255: when there are registers enough)
	I lastI; int lastIdx = -1, lastAcc = -1, lastAlu = 0;	// (the last instruction: an accumulator it wrote)
	int ras[2][4];
	int tex[4];
	int T = -1, TC = -1;			// the alpha / colour compare's key mask
	bool failed = false;

	Gen (const Config &cc, Shader &ss) : c (cc), s (ss)
	{
		for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) { reg[i][k] = -1; rlo[i][k] = -1024; rhi[i][k] = 1023; }
		for (int i = 0; i < 2; i++) for (int k = 0; k < 4; k++) ras[i][k] = -1;
		for (int k = 0; k < 4; k++) tex[k] = -1;
	}
	void fail (const char *e) { if (!failed) s.err = e; failed = true; }
	void claim (int reg, long long lo, long long hi)
	{
		if (s.nClaim < Shader::MAX_CLAIMS && (lo > -WIDE || hi < WIDE - 1))
		{ Shader::Claim &k = s.claim[s.nClaim++]; k.ip = s.prog.count () - 1; k.reg = reg; k.lo = lo; k.hi = hi; }
	}

	int allocRf ()
	{
		for (int r = 1; r < 32; r++) if (!(rfUsed >> r & 1)) { rfUsed |= 1ull << r; s.nRegs++; return r; }
		fail ("more than 32 registers");
		return 31;
	}
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

	// a value as an operand: small immediate, register, accumulator; the constants and uniforms
	// through r5 (the caller emits the ldunif: needR5)
	bool smallImm (int v, int o) const
	{
		if (isShift (o)) return true;
		return v >= -16 && v <= 15;
	}
	R operand (const Val &x, int o, unsigned long long &reads)
	{
		switch (x.k)
		{
		case VK_CONST:
			if (isShift (o)) { int n = x.v & 31; return imm ((u32) (n > 15 ? n - 32 : n)); }
			if (x.v >= -16 && x.v <= 15) return imm ((u32) x.v);
			return r5;
		case VK_RF: reads |= 1ull << x.v; return rf (x.v);
		case VK_ACC: return x.v == 0 ? r0 : x.v == 1 ? r1 : x.v == 2 ? r2 : r3;
		default: return r5;
		}
	}
	bool needR5 (const Val &x, int o) const { return (x.k == VK_CONST && !smallImm (x.v, o)) || x.k == VK_UNIF; }
	void loadR5 (const Val &x)
	{
		if (x.k == VK_CONST) ldunif (U_CONST, 0, 0, (unsigned) x.v);
		else ldunif (x.uk, x.ua, x.ub, 0);
	}
	// x into an accumulator (a copy)
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
	static R accR (int a) { return a == 0 ? r0 : a == 1 ? r1 : a == 2 ? r2 : r3; }

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
		// identities
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
	Val dup (const Val &x) { return x.k == VK_ACC ? toAcc2 (x) : x; }
	Val toAcc2 (const Val &x)		// (a copy, x kept)
	{
		Val d; d.k = VK_ACC; d.v = allocAcc (); d.uk = d.ua = d.ub = 0; d.lo = x.lo; d.hi = x.hi;
		emit (I ().m (V3D_QPU_M_MOV, accR (d.v), accR (x.v)), 0, 0);
		return d;
	}
	// x into rf dst: the instruction that just made x writes dst instead when it can
	void store (Val x, int dst)
	{
		if (x.k == VK_ACC && x.v == lastAcc && lastIdx == s.prog.count () - 1)
		{
			if (lastAlu) { lastI.in.alu.mul.waddr = (uint8_t) dst; lastI.in.alu.mul.magic_write = false; }
			else { lastI.in.alu.add.waddr = (uint8_t) dst; lastI.in.alu.add.magic_write = false; }
			s.prog.set (lastIdx, lastI);
			if (s.nClaim && s.claim[s.nClaim - 1].ip == lastIdx && s.claim[s.nClaim - 1].reg == x.v) s.claim[s.nClaim - 1].reg = 6 + dst;
			prevW |= 1ull << dst; lastAcc = -1;
			freeV (x);
			return;
		}
		op (O_MOV, x, cnst (0), dst);
	}
	// r clamped (0..255, else -1024..1023) into rf dst: its range after
	void clampTo (Val r, bool clamp, int dst, long long &lo, long long &hi)
	{
		long long mn = clamp ? 0 : -1024, mx = clamp ? 255 : 1023;
		bool needLo = r.lo < mn, needHi = r.hi > mx;
		lo = r.lo < mn ? mn : r.lo > mx ? mx : r.lo; hi = r.hi > mx ? mx : r.hi < mn ? mn : r.hi;
		if (needLo && needHi) { r = op (O_MAX, r, cnst ((int) mn)); op (O_MIN, r, clamp ? k255 () : cnst (1023), dst); }
		else if (needLo) op (O_MAX, r, cnst ((int) mn), dst);
		else if (needHi) op (O_MIN, r, clamp ? k255 () : cnst (1023), dst);
		else store (r, dst);
	}
	Val k255 () { return inRf (K255, 255, 255); }

	// ---- the TEV's operands
	Val regCh (int r, int ch, bool masked)
	{
		int n = reg[r][ch];
		if (n < 0) { fail ("a register not allocated"); return cnst (0); }
		Val v = inRf (n, rlo[r][ch], rhi[r][ch]);
		if (!masked || (v.lo >= 0 && v.hi <= 255)) return v;
		return op (O_AND, v, k255 ());
	}
	static int kfrac (int sel) { static const int f[8] = { 255, 223, 191, 159, 128, 96, 64, 32 }; return f[sel & 7]; }
	Val konstC (unsigned ksel, int ch)
	{
		int sel = (int) (ksel & 31);
		if (sel < 8) return cnst (kfrac (sel));
		if (sel >= 12 && sel < 16) return unif (U_KONST, sel - 12, ch);
		if (sel >= 16) return unif (U_KONST, (sel - 16) & 3, (sel - 16) >> 2);
		return cnst (0);
	}
	Val konstA (unsigned ksel)
	{
		int sel = (int) ((ksel >> 5) & 31);
		if (sel < 8) return cnst (kfrac (sel));
		if (sel >= 16) return unif (U_KONST, (sel - 16) & 3, (sel - 16) >> 2);
		return cnst (0);
	}
	// the stage's texel / rasterized channel ch (after the swap tables)
	bool texOn; int texTab, rasTab, chn;
	Val texCh (int ch)
	{
		if (!texOn) return cnst (255);
		return inRf (tex[c.swap[texTab * 4 + ch]], 0, 255);
	}
	Val rasCh (int ch)
	{
		if (chn != 0 && chn != 1) return cnst (0);
		return inRf (ras[chn][c.swap[rasTab * 4 + ch]], 0, 255);
	}
	// a colour input (sel 0..15) for channel ch (0..2); masked: & 255 (a, b, c)
	Val cin (int sel, int ch, unsigned ksel, bool masked)
	{
		switch (sel)
		{
		case 0: case 2: case 4: case 6: return regCh (sel >> 1, ch, masked);
		case 1: case 3: case 5: case 7: return regCh (sel >> 1, 3, masked);
		case 8: return texCh (ch);
		case 9: return texCh (3);
		case 10: return rasCh (ch);
		case 11: return rasCh (3);
		case 12: return cnst (255);
		case 13: return cnst (128);
		case 14: return konstC (ksel, ch);
		default: return cnst (0);
		}
	}
	Val ain (int sel, unsigned ksel, bool masked)
	{
		switch (sel)
		{
		case 0: case 1: case 2: case 3: return regCh (sel, 3, masked);
		case 4: return texCh (3);
		case 5: return rasCh (3);
		case 6: return konstA (ksel);
		default: return cnst (0);
		}
	}

	// (d + bias +- lerp (a, b, c)) x scale -> clamped -> rf dst
	void combine (Val a, Val b, Val cc, Val d, int bias, int sub, int scale, bool clamp, int dst)
	{
		int sh = scale == 1 ? 1 : scale == 2 ? 2 : 0;
		int rnd = sub == 0 ? (scale == 3 ? 128 : 0) : (scale == 3 ? 0 : 127);
		Val l;
		// c' = c + (c >> 7)
		Val cp;
		if (cc.k == VK_CONST) cp = cnst (cc.v + (cc.v >> 7));
		else { Val t = op (O_SHR, dup (cc), cnst (7)); cp = op (O_ADD, cc, t); }
		if (cp.k == VK_CONST && cp.v == 0) { freeV (b); l = op (O_SHL, a, cnst (sh)); }
		else if (cp.k == VK_CONST && cp.v == 256) { freeV (a); l = op (O_SHL, b, cnst (sh)); }
		else
		{
			Val diff = op (O_SUB, b, dup (a));
			Val m = op (O_MUL, diff, cp);
			Val u = op (O_ADD, op (O_SHL, a, cnst (8)), m);
			u = op (O_SHL, u, cnst (sh));
			u = op (O_ADD, u, cnst (rnd));
			l = op (O_ASR, u, cnst (8));
		}
		Val r = op (O_ADD, d, cnst (bias == 1 ? 128 : bias == 2 ? -128 : 0));
		r = op (O_SHL, r, cnst (sh));
		r = op (sub ? O_SUB : O_ADD, r, l);
		if (scale == 3) r = op (O_ASR, r, cnst (1));
		clampTo (r, clamp, dst, outLo, outHi);
	}
	long long outLo, outHi;			// (the range combine / compare left in dst)
	// masks: -1 where x > y, else 0; -1 where x == y
	Val gtMask (Val x, Val y) { return op (O_ASR, op (O_SUB, y, x), cnst (31)); }
	Val eqMask (Val x, Val y)
	{
		Val d = op (O_SUB, x, y);
		Val n = op (O_NEG, dup (d), cnst (0));
		return op (O_NOT, op (O_ASR, op (O_OR, d, n), cnst (31)), cnst (0));
	}
	// the compare modes' key of a colour input (m 0: r, 1: g r, 2: b g r)
	Val key (int sel, unsigned ksel, int m)
	{
		Val k = cin (sel, 0, ksel, true);
		if (m >= 1) k = op (O_OR, k, op (O_SHL, cin (sel, 1, ksel, true), cnst (8)));
		if (m >= 2) k = op (O_OR, k, op (O_SHL, cin (sel, 2, ksel, true), cnst (16)));
		return k;
	}

	// ---- the texture lookups
	int nextLook = 0, lookAt = 0;		// the next lookup to start, the next stage's
	void issueGroup ()
	{
		int first = nextLook, n = s.nLook - first < 4 ? s.nLook - first : 4;
		for (int L = first; L < first + n; L++)
		{
			const Lookup &lk = s.look[L];
			int comps = lk.proj ? 3 : 2;
			for (int k = 0; k < comps; k++)
			{
				if (s.nVary >= 64) { fail ("too many varyings"); return; }
				Vary &v = s.vary[s.nVary++]; v.kind = V_COORD; v.a = (unsigned char) L; v.b = (unsigned char) k;
				R dst = k == 0 ? r1 : k == 1 ? r2 : r3;
				emit (I ().ldvary (r0), 0, 0, true);
				emit (I ().m (V3D_QPU_M_FMUL, dst, r0, rf (0)), 1, 0);
				emit (I ().a (V3D_QPU_A_FADD, dst, dst, r5), 0, 0);
			}
			if (lk.proj)
			{
				emit (I ().m (V3D_QPU_M_MOV, recip, r3), 0, 0);
				nop_ (); nop_ ();
				emit (I ().m (V3D_QPU_M_FMUL, r1, r1, r4), 0, 0);
				emit (I ().m (V3D_QPU_M_FMUL, r2, r2, r4), 0, 0);
			}
			emit (I ().m (V3D_QPU_M_MOV, tmut, r2), 0, 0);
			addUni (U_TEXP0, L, 0, 0); emit (I ().wrtmuc (), 0, 0, false, true);
			addUni (U_TEXP1, L, 0, 0); emit (I ().wrtmuc (), 0, 0, false, true);
			emit (I ().m (V3D_QPU_M_MOV, tmus, r1), 0, 0);
		}
		nextLook = first + n;
		emit (I ().thrsw (), 0, 0);
		if (nextLook == s.nLook) emit (I ().thrsw (), 0, 0);		// (the last segment)
		nop_ (); nop_ ();
	}
	// r2 = x * 255 (the float constant, else x 128 x 2 - x: the same rounding)
	void times255 (R x, R xAdd)
	{
		if (F255 >= 0) { emit (I ().m (V3D_QPU_M_FMUL, r2, x, rf (F255)), 1ull << F255, 0); return; }
		emit (I ().m (V3D_QPU_M_FMUL, r2, x, imm (0x43000000)), 0, 0);	// x 128
		emit (I ().a (V3D_QPU_A_FADD, r2, r2, r2), 0, 0);		// x 256
		emit (I ().a (V3D_QPU_A_FSUB, r2, r2, xAdd), 0, 0);		// x 255
	}
	// the stage's lookup: its two words -> the texel's channels the stage reads (0..255)
	void readTexel (unsigned need)
	{
		emit (I ().ldtmu (r0), 0, 0);
		emit (I ().ldtmu (r1), 0, 0);
		for (int ch = 0; ch < 4; ch++)
		{
			if (!(need >> ch & 1)) continue;
			R x = ch == 0 ? l (r0) : ch == 1 ? h (r0) : ch == 2 ? l (r1) : h (r1);
			times255 (x, x);
			emit (I ().a (V3D_QPU_A_FTOIN, rf (tex[ch]), r2), 0, 1ull << tex[ch]);
		}
	}

	void stage (int st)
	{
		unsigned C = c.cenv[st], A = c.aenv[st], TR = c.tref[st], KS = c.ksel[st];
		texOn = (TR & 64) != 0; texTab = (int) ((A >> 2) & 3); rasTab = (int) (A & 3); chn = (int) ((TR >> 7) & 7);
		if (chn == 5 || chn == 6) { s.unsupported = true; chn = 7; }
		int ca = (int) ((C >> 12) & 15), cb = (int) ((C >> 8) & 15), ccs = (int) ((C >> 4) & 15), cd = (int) (C & 15);
		int aa = (int) ((A >> 13) & 7), ab = (int) ((A >> 10) & 7), ac = (int) ((A >> 7) & 7), ad = (int) ((A >> 4) & 7);
		int cBias = (int) ((C >> 16) & 3), cSub = (int) ((C >> 18) & 1), cScale = (int) ((C >> 20) & 3);
		int aBias = (int) ((A >> 16) & 3), aSub = (int) ((A >> 18) & 1), aScale = (int) ((A >> 20) & 3);
		bool last = st == c.nStages - 1;
		int cDst = last ? 0 : (int) ((C >> 22) & 3), aDst = last ? 0 : (int) ((A >> 22) & 3);
		bool cClamp = (C & 0x80000) != 0, aClamp = (A & 0x80000) != 0;
		// the texel
		if (texOn)
		{
			if (lookAt == nextLook) issueGroup ();
			lookAt++;
			unsigned need = 0;
			int cs[4] = { ca, cb, ccs, cd };
			for (int k = 0; k < 4; k++) { if (cs[k] == 8) need |= 7; if (cs[k] == 9) need |= 8; }
			int as[4] = { aa, ab, ac, ad };
			for (int k = 0; k < 4; k++) if (as[k] == 4) need |= 8;
			unsigned src = 0;					// (the sampled channels the swap table reads)
			for (int ch = 0; ch < 4; ch++) if (need >> ch & 1) src |= 1u << c.swap[texTab * 4 + ch];
			readTexel (src);
			if (failed) return;
		}
		// the compare modes' key masks, before any register is written
		Val cMask = cnst (0);
		if (cBias == 3 && (cScale << 1 | cSub) >> 1 != 3)
		{
			int m = cScale;
			bool eq = cSub != 0;
			cMask = eq ? eqMask (key (ca, KS, m), key (cb, KS, m)) : gtMask (key (ca, KS, m), key (cb, KS, m));
			if (cMask.k == VK_ACC) { store (cMask, TC); cMask = inRf (TC, -1, 0); }
		}
		Val aMask = cnst (0);
		if (aBias == 3 && aScale != 3)
		{
			bool eq = aSub != 0;
			aMask = eq ? eqMask (key (ca, KS, aScale), key (cb, KS, aScale)) : gtMask (key (ca, KS, aScale), key (cb, KS, aScale));
			if (aMask.k == VK_ACC) { store (aMask, T); aMask = inRf (T, -1, 0); }
		}
		// the colour
		for (int ch = 0; ch < 3; ch++)
		{
			int dst = reg[cDst][ch];
			if (cBias != 3) combine (cin (ca, ch, KS, true), cin (cb, ch, KS, true), cin (ccs, ch, KS, true), cin (cd, ch, KS, false),
						 cBias, cSub, cScale, cClamp, dst);
			else
			{
				Val m = cMask;
				if (cScale == 3)
				{
					Val x = cin (ca, ch, KS, true), y = cin (cb, ch, KS, true);
					m = cSub ? eqMask (x, y) : gtMask (x, y);
				}
				Val r = op (O_ADD, cin (cd, ch, KS, false), op (O_AND, cin (ccs, ch, KS, true), m));
				clampTo (r, cClamp, dst, outLo, outHi);
			}
			rlo[cDst][ch] = outLo; rhi[cDst][ch] = outHi;
		}
		// the alpha
		{
			int dst = reg[aDst][3];
			if (aBias != 3) combine (ain (aa, KS, true), ain (ab, KS, true), ain (ac, KS, true), ain (ad, KS, false),
						 aBias, aSub, aScale, aClamp, dst);
			else
			{
				Val m = aMask;
				if (aScale == 3)
				{
					Val x = ain (aa, KS, true), y = ain (ab, KS, true);
					m = aSub ? eqMask (x, y) : gtMask (x, y);
				}
				Val r = op (O_ADD, ain (ad, KS, false), op (O_AND, ain (ac, KS, true), m));
				clampTo (r, aClamp, dst, outLo, outHi);
			}
			rlo[aDst][3] = outLo; rhi[aDst][3] = outHi;
		}
		if (accBusy) fail ("an accumulator left busy");
	}

	// the alpha test's comparison f of a (0..255) with reference i: a mask (-1 pass), or a constant
	Val atest (int f, Val a, int i)
	{
		switch (f)
		{
		case 0: freeV (a); return cnst (0);
		case 1: return gtMask (unif (U_ALPHAREF, i, 0), a);				// a < r
		case 2: return eqMask (a, unif (U_ALPHAREF, i, 0));
		case 3: return op (O_NOT, gtMask (a, unif (U_ALPHAREF, i, 0)), cnst (0));	// a <= r
		case 4: return gtMask (a, unif (U_ALPHAREF, i, 0));
		case 5: return op (O_NOT, eqMask (a, unif (U_ALPHAREF, i, 0)), cnst (0));
		case 6: return op (O_NOT, gtMask (unif (U_ALPHAREF, i, 0), a), cnst (0));	// a >= r
		default: freeV (a); return cnst (-1);
		}
	}

	void run ()
	{
		// what is used
		unsigned regUsed[4] = { 15, 0, 0, 0 };		// (channels: bit 0 r ... 3 a; PREV: the output)
		unsigned rasUsed[2] = { 0, 0 }, texUsed = 0;		// (the raw channels the swap tables read)
		unsigned written[4] = { 0, 0, 0, 0 }, load[4] = { 0, 0, 0, 0 };	// (a channel read before any stage wrote it: loaded)
		bool needT = false, needTC = false;
		for (int st = 0; st < c.nStages; st++)
		{
			unsigned C = c.cenv[st], A = c.aenv[st], TR = c.tref[st];
			int cs[4] = { (int) ((C >> 12) & 15), (int) ((C >> 8) & 15), (int) ((C >> 4) & 15), (int) (C & 15) };
			int as[4] = { (int) ((A >> 13) & 7), (int) ((A >> 10) & 7), (int) ((A >> 7) & 7), (int) ((A >> 4) & 7) };
			int ch = (int) ((TR >> 7) & 7);
			bool last = st == c.nStages - 1;
			for (int k = 0; k < 4; k++)
			{
				if (cs[k] < 8) { unsigned m = cs[k] & 1 ? 8 : 7; regUsed[cs[k] >> 1] |= m; load[cs[k] >> 1] |= m & ~written[cs[k] >> 1]; }
				if (as[k] < 4) { regUsed[as[k]] |= 8; load[as[k]] |= 8 & ~written[as[k]]; }
				int rt = (int) (A & 3), tt = (int) ((A >> 2) & 3);
				if (ch == 0 || ch == 1)
				{
					if (cs[k] == 10) for (int j = 0; j < 3; j++) rasUsed[ch] |= 1u << c.swap[rt * 4 + j];
					if (cs[k] == 11 || as[k] == 5) rasUsed[ch] |= 1u << c.swap[rt * 4 + 3];
				}
				if (TR & 64)
				{
					if (cs[k] == 8) for (int j = 0; j < 3; j++) texUsed |= 1u << c.swap[tt * 4 + j];
					if (cs[k] == 9 || as[k] == 4) texUsed |= 1u << c.swap[tt * 4 + 3];
				}
			}
			if (!last) { regUsed[(C >> 22) & 3] |= 7; regUsed[(A >> 22) & 3] |= 8; written[(C >> 22) & 3] |= 7; written[(A >> 22) & 3] |= 8; }
			else written[0] |= 15;
			if (((C >> 16) & 3) == 3 && ((C >> 20) & 3) != 3) needTC = true;
			if (((A >> 16) & 3) == 3 && ((A >> 20) & 3) != 3) needT = true;
			if (TR & 64)
			{
				if (s.nLook >= MAX_LOOKUPS) { fail ("more than 8 texture lookups"); return; }
				Lookup &lk = s.look[s.nLook++];
				lk.stage = (unsigned char) st; lk.map = (unsigned char) (TR & 7); lk.coord = (unsigned char) ((TR >> 3) & 7);
				lk.proj = (c.projMask >> lk.coord & 1) != 0;
			}
		}
		// the registers
		K255 = allocRf ();
		ldunifrf (K255, U_CONST, 0, 0, 255);
		for (int r = 0; r < 4; r++)
			for (int ch = 0; ch < 4; ch++)
				if (regUsed[r] >> ch & 1)
				{
					reg[r][ch] = allocRf ();
					if (load[r] >> ch & 1) ldunifrf (reg[r][ch], U_REG, r, ch, 0);
				}
		for (int i = 0; i < 2; i++) for (int ch = 0; ch < 4; ch++) if (rasUsed[i] >> ch & 1) ras[i][ch] = allocRf ();
		for (int ch = 0; ch < 4; ch++) if (texUsed >> ch & 1) tex[ch] = allocRf ();
		if (needT) T = allocRf ();
		if (needTC) TC = allocRf ();
		// the float constants, when registers are left
		int left = 0;
		for (int r = 1; r < 32; r++) if (!(rfUsed >> r & 1)) left++;
		if (left >= 1 && (rasUsed[0] | rasUsed[1] | texUsed)) { F255 = allocRf (); ldunifrf (F255, U_CONST, 0, 0, 0x437F0000); left--; }
		if (left >= 1) { INV255 = allocRf (); union { float f; unsigned u; } k; k.f = 1.0f / 255.0f; ldunifrf (INV255, U_CONST, 0, 0, k.u); }
		if (failed) return;
		// the rasterized colours: x W + C, x 255, rounded
		for (int i = 0; i < 2; i++)
		{
			for (int ch = 0; ch < 4; ch++)
			{
				if (!(rasUsed[i] >> ch & 1)) continue;
				if (s.nVary >= 64) { fail ("too many varyings"); return; }
				Vary &v = s.vary[s.nVary++]; v.kind = (unsigned char) (i ? V_C1 : V_C0); v.a = (unsigned char) ch; v.b = 0;
				emit (I ().ldvary (r0), 0, 0, true);
				emit (I ().m (V3D_QPU_M_FMUL, r1, r0, rf (0)), 1, 0);
				emit (I ().a (V3D_QPU_A_FADD, r1, r1, r5), 0, 0);
				times255 (r1, r1);
				emit (I ().a (V3D_QPU_A_FTOIN, rf (ras[i][ch]), r2), 0, 1ull << ras[i][ch]);
			}
		}
		// the stages
		for (int st = 0; st < c.nStages && !failed; st++) stage (st);
		if (failed) return;
		// no lookups: the last-segment pair here (only the registers live across it). The program never
		// starts in its final section: KAPI_GPU_P_FS_FINAL stopped the GPU now and then on wide targets
		// (>= 512 pixels) -- Mesa does not use it for fragment shaders either.
		if (s.nLook == 0)
		{
			if (accBusy) { fail ("an accumulator live at the last thread switch"); return; }
			emit (I ().thrsw (), 0, 0);
			emit (I ().thrsw (), 0, 0);
			nop_ (); nop_ ();
		}
		// the output: PREV & 255, the alpha test, the EFB's format
		Val out[4];
		for (int ch = 0; ch < 4; ch++)
		{
			Val v = inRf (reg[0][ch], rlo[0][ch], rhi[0][ch]);
			out[ch] = v.lo >= 0 && v.hi <= 255 ? v : op (O_AND, v, k255 (), reg[0][ch]);
		}
		int f0 = c.alphaFunc[0] & 7, f1 = c.alphaFunc[1] & 7;
		Val p0 = atest (f0, out[3], 0), p1 = atest (f1, out[3], 1);
		Val pass;
		switch (c.alphaLogic & 3)
		{
		case 0: pass = op (O_AND, p0, p1); break;
		case 1: pass = op (O_OR, p0, p1); break;
		case 2: pass = op (O_XOR, p0, p1); break;
		default: pass = op (O_NOT, op (O_XOR, p0, p1), cnst (0)); break;
		}
		if (pass.k != VK_CONST || pass.v != -1)
		{
			if (pass.k == VK_CONST) { freeV (pass); pass = toAcc (pass); }
			unsigned long long rd = 0;
			R pm = operand (pass, O_OR, rd);
			emit (I ().a (V3D_QPU_A_OR, nop, pm, pm).apf (V3D_QPU_PF_PUSHZ), rd, 0);	// flag A: failed
			freeV (pass);
			emit (I ().a (V3D_QPU_A_SETMSF, nop, imm (0)).ac (V3D_QPU_COND_IFA), 0, 0);
		}
		// the channels as floats 0..1 in r0..r3
		int fmt = c.efbFmt <= 2 ? c.efbFmt : 0;
		for (int ch = 0; ch < 4; ch++)
		{
			R d = accR (ch);
			if (ch == 3 && (c.dstAlpha || fmt == 2))
			{
				if (fmt == 2 && !c.dstAlpha) emit (I ().m (V3D_QPU_M_MOV, d, imm (0x3f800000)), 0, 0);
				else { ldunif (U_DSTALPHA, 0, 0, 0); emit (I ().m (V3D_QPU_M_MOV, d, r5), 0, 0); }
				continue;
			}
			int shr = fmt == 1 ? 2 : fmt == 2 ? (ch == 1 ? 2 : 3) : 0;
			float div = fmt == 1 ? 63.0f : fmt == 2 ? (ch == 1 ? 63.0f : 31.0f) : 255.0f;
			unsigned long long rd = 0;
			R x = operand (out[ch], O_MOV, rd);
			if (shr) { emit (I ().a (V3D_QPU_A_SHR, d, x, imm ((u32) shr)), rd, 0); emit (I ().a (V3D_QPU_A_ITOF, d, d), 0, 0); }
			else emit (I ().a (V3D_QPU_A_ITOF, d, x), rd, 0);
			if (div == 255.0f && INV255 >= 0) { emit (I ().m (V3D_QPU_M_FMUL, d, d, rf (INV255)), 1ull << INV255, 0); continue; }
			union { float f; unsigned u; } k; k.f = 1.0f / div;
			ldunif (U_CONST, 0, 0, k.u);
			emit (I ().m (V3D_QPU_M_FMUL, d, d, r5), 0, 0);
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
	if (c.nStages < 1 || c.nStages > 16) { s.err = "1..16 stages"; return false; }
	Gen g (c, s);
	g.run ();
	if (!g.failed && !s.prog.ok ()) { s.err = "an instruction could not be encoded"; g.failed = true; }
	return !g.failed;
}

unsigned long long key (const Config &c)
{
	unsigned long long h = 1469598103934665603ull;
	auto mix = [&h] (unsigned v) { for (int i = 0; i < 4; i++) { h ^= (v >> (i * 8)) & 255; h *= 1099511628211ull; } };
	mix ((unsigned) c.nStages);
	for (int st = 0; st < c.nStages; st++) { mix (c.cenv[st]); mix (c.aenv[st]); mix (c.tref[st]); mix (c.ksel[st]); }
	for (int k = 0; k < 16; k++) mix (c.swap[k]);
	mix ((unsigned) c.alphaFunc[0] | (unsigned) c.alphaFunc[1] << 8 | (unsigned) c.alphaLogic << 16 | (unsigned) c.efbFmt << 24);
	mix ((unsigned) c.dstAlpha | (unsigned) c.projMask << 8);
	return h;
}

} // namespace gxtev
