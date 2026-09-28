//
// qpusim.cpp -- see qpusim.h.
//
#include "qpusim.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
extern "C" {
#include "broadcom/common/v3d_device_info.h"
#include "broadcom/qpu/qpu_instr.h"
#include "broadcom/qpu/qpu_disasm.h"
}

namespace qpusim
{

static v3d_device_info dev () { v3d_device_info d; memset (&d, 0, sizeof d); d.ver = 42; return d; }

static inline float asF (uint32_t u) { float f; memcpy (&f, &u, 4); return f; }
static inline uint32_t asU (float f) { uint32_t u; memcpy (&u, &f, 4); return u; }

uint16_t toHalf (float f)
{
	uint32_t x = asU (f), sign = (x >> 16) & 0x8000;
	int e = (int) ((x >> 23) & 0xFF) - 127 + 15;
	uint32_t m = x & 0x7FFFFF;
	if (((x >> 23) & 0xFF) == 0xFF) return (uint16_t) (sign | 0x7C00 | (m ? 0x200 : 0));
	if (e >= 31) return (uint16_t) (sign | 0x7C00);
	if (e <= 0)
	{
		if (e < -10) return (uint16_t) sign;
		m |= 0x800000;
		uint32_t shift = (uint32_t) (14 - e);
		uint32_t hm = m >> shift, rem = m & ((1u << shift) - 1), half = 1u << (shift - 1);
		if (rem > half || (rem == half && (hm & 1))) hm++;
		return (uint16_t) (sign | hm);
	}
	uint32_t hm = m >> 13, rem = m & 0x1FFF;
	uint32_t r = sign | ((uint32_t) e << 10) | hm;
	if (rem > 0x1000 || (rem == 0x1000 && (hm & 1))) r++;
	return (uint16_t) r;
}

float fromHalf (uint16_t h)
{
	uint32_t sign = (uint32_t) (h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 0x3FF;
	if (e == 0) { float f = ldexpf ((float) m, -24); return sign ? -f : f; }
	if (e == 31) return asF (sign | 0x7F800000 | (m << 13));
	return asF (sign | ((e - 15 + 127) << 23) | (m << 13));
}

enum { L = 16 };
typedef uint32_t Vec[L];

struct Pending { int reg; int at; Vec v; bool mask[L]; };	// a write landing later (r4, r5)

struct State
{
	Vec acc[6], rf[64];
	bool fa[L], fb[L];
	uint32_t msf[L];
	std::vector<Pending> pend;
	// the TMU
	uint32_t cfg[4]; int ncfg;
	Vec tmuT; bool haveT;
	std::vector<uint32_t> fifo[L];
	size_t uni;
};

static uint32_t unpackIn (uint32_t v, v3d_qpu_input_unpack u, bool isFloat)
{
	switch (u)
	{
	case V3D_QPU_UNPACK_L: return asU (fromHalf ((uint16_t) v));
	case V3D_QPU_UNPACK_H: return asU (fromHalf ((uint16_t) (v >> 16)));
	case V3D_QPU_UNPACK_ABS: return isFloat ? v & 0x7FFFFFFF : v;
	default: return v;
	}
}

static uint32_t fixF (float f) { return asU (f); }

// a texel of t at the integer coordinates (wrapped)
static uint32_t texel (const Texture &t, int x, int y)
{
	auto wrap = [] (int c, int n, int mode) {
		if (mode == 1) return c < 0 ? 0 : c >= n ? n - 1 : c;
		if (mode == 2) { int p = 2 * n; c %= p; if (c < 0) c += p; return c < n ? c : p - 1 - c; }
		c %= n; if (c < 0) c += n; return c;
	};
	x = wrap (x, t.w, t.wrapS); y = wrap (y, t.h, t.wrapT);
	return t.px[(size_t) y * (size_t) t.w + (size_t) x];
}

static void sample (const Texture &t, float s, float tt, float out[4])
{
	if (!t.linear)
	{
		int x = (int) floorf (s * (float) t.w), y = (int) floorf (tt * (float) t.h);
		uint32_t c = texel (t, x, y);
		for (int k = 0; k < 4; k++) out[k] = (float) ((c >> (k * 8)) & 255) / 255.0f;
		return;
	}
	float fx = s * (float) t.w - 0.5f, fy = tt * (float) t.h - 0.5f;
	int x0 = (int) floorf (fx), y0 = (int) floorf (fy);
	float ax = fx - (float) x0, ay = fy - (float) y0;
	uint32_t c00 = texel (t, x0, y0), c10 = texel (t, x0 + 1, y0), c01 = texel (t, x0, y0 + 1), c11 = texel (t, x0 + 1, y0 + 1);
	for (int k = 0; k < 4; k++)
	{
		float a = (float) ((c00 >> (k * 8)) & 255), b = (float) ((c10 >> (k * 8)) & 255);
		float c = (float) ((c01 >> (k * 8)) & 255), d = (float) ((c11 >> (k * 8)) & 255);
		out[k] = ((a * (1 - ax) + b * ax) * (1 - ay) + (c * (1 - ax) + d * ax) * ay) / 255.0f;
	}
}

static bool addOp (v3d_qpu_add_op op, uint32_t a, uint32_t b, uint32_t *r, State &, int lane, std::string &err, bool *isF)
{
	*isF = false;
	(void) lane;
	switch (op)
	{
	case V3D_QPU_A_FADD: case V3D_QPU_A_FADDNF: *r = fixF (asF (a) + asF (b)); *isF = true; return true;
	case V3D_QPU_A_FSUB: case V3D_QPU_A_FCMP: *r = fixF (asF (a) - asF (b)); *isF = true; return true;
	case V3D_QPU_A_FMIN: *r = fixF (fminf (asF (a), asF (b))); *isF = true; return true;
	case V3D_QPU_A_FMAX: *r = fixF (fmaxf (asF (a), asF (b))); *isF = true; return true;
	case V3D_QPU_A_VFPACK: *r = (uint32_t) toHalf (asF (a)) | (uint32_t) toHalf (asF (b)) << 16; return true;
	case V3D_QPU_A_ADD: *r = a + b; return true;
	case V3D_QPU_A_SUB: *r = a - b; return true;
	case V3D_QPU_A_MIN: *r = (uint32_t) ((int32_t) a < (int32_t) b ? (int32_t) a : (int32_t) b); return true;
	case V3D_QPU_A_MAX: *r = (uint32_t) ((int32_t) a > (int32_t) b ? (int32_t) a : (int32_t) b); return true;
	case V3D_QPU_A_UMIN: *r = a < b ? a : b; return true;
	case V3D_QPU_A_UMAX: *r = a > b ? a : b; return true;
	case V3D_QPU_A_SHL: *r = a << (b & 31); return true;
	case V3D_QPU_A_SHR: *r = a >> (b & 31); return true;
	case V3D_QPU_A_ASR: *r = (uint32_t) ((int32_t) a >> (b & 31)); return true;
	case V3D_QPU_A_ROR: { uint32_t s = b & 31; *r = s ? (a >> s) | (a << (32 - s)) : a; return true; }
	case V3D_QPU_A_AND: *r = a & b; return true;
	case V3D_QPU_A_OR: *r = a | b; return true;
	case V3D_QPU_A_XOR: *r = a ^ b; return true;
	case V3D_QPU_A_NOT: *r = ~a; return true;
	case V3D_QPU_A_NEG: *r = (uint32_t) -(int32_t) a; return true;
	case V3D_QPU_A_MOV: *r = a; return true;
	case V3D_QPU_A_FMOV: *r = a; *isF = true; return true;
	case V3D_QPU_A_FTOIN: { float f = asF (a); *r = (uint32_t) (int32_t) nearbyintf (f); return true; }
	case V3D_QPU_A_FTOIZ: { float f = asF (a); *r = (uint32_t) (int32_t) truncf (f); return true; }
	case V3D_QPU_A_FTOUZ: { float f = asF (a); *r = f <= 0 ? 0 : (uint32_t) truncf (f); return true; }
	case V3D_QPU_A_ITOF: *r = fixF ((float) (int32_t) a); *isF = true; return true;
	case V3D_QPU_A_UTOF: *r = fixF ((float) a); *isF = true; return true;
	case V3D_QPU_A_FFLOOR: *r = fixF (floorf (asF (a))); *isF = true; return true;
	case V3D_QPU_A_FCEIL: *r = fixF (ceilf (asF (a))); *isF = true; return true;
	case V3D_QPU_A_FTRUNC: *r = fixF (truncf (asF (a))); *isF = true; return true;
	case V3D_QPU_A_FROUND: *r = fixF (nearbyintf (asF (a))); *isF = true; return true;
	default: err = std::string ("add op not modelled: ") + v3d_qpu_add_op_name (op); return false;
	}
}

static bool mulOp (v3d_qpu_mul_op op, uint32_t a, uint32_t b, uint32_t *r, std::string &err, bool *isF)
{
	*isF = false;
	switch (op)
	{
	case V3D_QPU_M_FMUL: *r = fixF (asF (a) * asF (b)); *isF = true; return true;
	case V3D_QPU_M_ADD: *r = a + b; return true;
	case V3D_QPU_M_SUB: *r = a - b; return true;
	case V3D_QPU_M_UMUL24: case V3D_QPU_M_UMUL24_RTOP0: *r = (a & 0xFFFFFF) * (b & 0xFFFFFF); return true;
	case V3D_QPU_M_SMUL24:
	{
		int32_t x = (int32_t) (a << 8) >> 8, y = (int32_t) (b << 8) >> 8;
		*r = (uint32_t) (x * y); return true;
	}
	case V3D_QPU_M_MOV: *r = a; return true;
	case V3D_QPU_M_FMOV: *r = a; *isF = true; return true;
	default: err = std::string ("mul op not modelled: ") + v3d_qpu_mul_op_name (op); return false;
	}
}

static bool isFloatAdd (v3d_qpu_add_op op)
{
	return op == V3D_QPU_A_FADD || op == V3D_QPU_A_FADDNF || op == V3D_QPU_A_FSUB || op == V3D_QPU_A_FCMP || op == V3D_QPU_A_FMIN
	    || op == V3D_QPU_A_FMAX || op == V3D_QPU_A_VFPACK || op == V3D_QPU_A_FMOV || op == V3D_QPU_A_FTOIN || op == V3D_QPU_A_FTOIZ
	    || op == V3D_QPU_A_FTOUZ || op == V3D_QPU_A_FFLOOR || op == V3D_QPU_A_FCEIL || op == V3D_QPU_A_FTRUNC || op == V3D_QPU_A_FROUND;
}

bool runFragment (const uint64_t *words, int n, std::vector<Pixel> &pixels, Run &run)
{
	v3d_device_info d = dev ();
	std::vector<v3d_qpu_instr> ins ((size_t) n);
	for (int i = 0; i < n; i++)
		if (!v3d_qpu_instr_unpack (&d, words[i], &ins[(size_t) i])) { run.error = "cannot decode instruction " + std::to_string (i); return false; }
	run.instructions = 0;
	for (size_t base = 0; base < pixels.size (); base += L)
	{
		State *S = new State ();
		State &s = *S;
		memset (s.acc, 0, sizeof s.acc); memset (s.rf, 0, sizeof s.rf);
		size_t np = pixels.size () - base < L ? pixels.size () - base : L;
		for (int l = 0; l < L; l++) { s.fa[l] = s.fb[l] = false; s.msf[l] = (size_t) l < np ? 1 : 0; s.rf[0][l] = asU (1.0f); s.rf[1][l] = asU (1.0f); s.rf[2][l] = 0; }
		s.ncfg = 0; s.haveT = false; s.uni = 0;
		std::vector<size_t> varyAt (L, 0);
		for (size_t k = 0; k < np; k++) { pixels[base + k].written = false; pixels[base + k].nTlb = 0; }
		for (int ip = 0; ip < n; ip++)
		{
			const v3d_qpu_instr &in = ins[(size_t) ip];
			run.instructions++;
			if (in.type != V3D_QPU_INSTR_TYPE_ALU) { run.error = "a branch: not modelled"; delete S; return false; }
			if (in.flags.auf != V3D_QPU_UF_NONE || in.flags.muf != V3D_QPU_UF_NONE) { run.error = "flag updates: not modelled"; delete S; return false; }
			// the operands (before any write of this instruction)
			uint32_t smallImm = 0;
			if (in.sig.small_imm_b) v3d_qpu_small_imm_unpack (&d, in.raddr_b, &smallImm);
			auto read = [&] (v3d_qpu_mux m, int l) -> uint32_t {
				if (m <= V3D_QPU_MUX_R5) return s.acc[m][l];
				if (m == V3D_QPU_MUX_A) return s.rf[in.raddr_a][l];
				return in.sig.small_imm_b ? smallImm : s.rf[in.raddr_b][l];
			};
			Vec addR, mulR; bool addF[L], mulF[L];
			bool haveAdd = in.alu.add.op != V3D_QPU_A_NOP, haveMul = in.alu.mul.op != V3D_QPU_M_NOP;
			int ansrc = haveAdd ? v3d_qpu_add_op_num_src (in.alu.add.op) : 0, mnsrc = haveMul ? v3d_qpu_mul_op_num_src (in.alu.mul.op) : 0;
			for (int l = 0; l < L; l++)
			{
				if (haveAdd && in.alu.add.op != V3D_QPU_A_SETMSF && in.alu.add.op != V3D_QPU_A_TMUWT && in.alu.add.op != V3D_QPU_A_MSF)
				{
					bool f = isFloatAdd (in.alu.add.op);
					uint32_t a = ansrc >= 1 ? unpackIn (read (in.alu.add.a.mux, l), in.alu.add.a.unpack, f) : 0;
					uint32_t b = ansrc >= 2 ? unpackIn (read (in.alu.add.b.mux, l), in.alu.add.b.unpack, f) : 0;
					if (!addOp (in.alu.add.op, a, b, &addR[l], s, l, run.error, &addF[l])) { delete S; return false; }
				}
				if (haveAdd && in.alu.add.op == V3D_QPU_A_MSF) addR[l] = s.msf[l];
				if (haveMul)
				{
					bool f = in.alu.mul.op == V3D_QPU_M_FMUL || in.alu.mul.op == V3D_QPU_M_FMOV;
					uint32_t a = mnsrc >= 1 ? unpackIn (read (in.alu.mul.a.mux, l), in.alu.mul.a.unpack, f) : 0;
					uint32_t b = mnsrc >= 2 ? unpackIn (read (in.alu.mul.b.mux, l), in.alu.mul.b.unpack, f) : 0;
					if (!mulOp (in.alu.mul.op, a, b, &mulR[l], run.error, &mulF[l])) { delete S; return false; }
				}
			}
			// the landing of the delayed writes planned for the end of the previous instruction ... (applied below)
			// the results: conditions, flags, destinations
			auto condOk = [&] (v3d_qpu_cond c, int l) {
				switch (c) { case V3D_QPU_COND_IFA: return s.fa[l]; case V3D_QPU_COND_IFB: return s.fb[l];
				case V3D_QPU_COND_IFNA: return !s.fa[l]; case V3D_QPU_COND_IFNB: return !s.fb[l]; default: return true; }
			};
			bool faNew[L], fbNew[L];
			memcpy (faNew, s.fa, sizeof faNew); memcpy (fbNew, s.fb, sizeof fbNew);
			auto push = [&] (v3d_qpu_pf pf, const Vec &r, const bool *isF, uint32_t (*)(void), int which) {
				(void) which;
				if (pf == V3D_QPU_PF_NONE) return;
				for (int l = 0; l < L; l++)
				{
					bool v;
					if (pf == V3D_QPU_PF_PUSHZ) v = isF[l] ? (r[l] & 0x7FFFFFFF) == 0 : r[l] == 0;
					else if (pf == V3D_QPU_PF_PUSHN) v = isF[l] ? (r[l] >> 31) != 0 && (r[l] & 0x7FFFFFFF) != 0 : (int32_t) r[l] < 0;
					else v = (r[l] >> 31) != 0;	/* (C: approximated by the sign) */
					fbNew[l] = s.fa[l]; faNew[l] = v;
				}
			};
			Vec wr[70]; bool wrMask[70][L]; bool wrAny[70];	// 0..5 accumulators, 6..69 rf
			memset (wrAny, 0, sizeof wrAny); memset (wrMask, 0, sizeof wrMask);
			auto dest = [&] (uint8_t waddr, bool magicW, v3d_qpu_output_pack pack, const Vec &r, v3d_qpu_cond c, const bool *isF, const char *who) -> bool {
				(void) isF;
				for (int l = 0; l < L; l++)
				{
					if (!condOk (c, l)) continue;
					uint32_t v = r[l];
					int slot = -1;
					if (!magicW) slot = 6 + waddr;
					else if (waddr <= 5) slot = waddr;
					if (slot >= 0)
					{
						uint32_t old = slot < 6 ? s.acc[slot][l] : s.rf[slot - 6][l];
						if (pack == V3D_QPU_PACK_L) v = (old & 0xFFFF0000u) | toHalf (asF (v));
						else if (pack == V3D_QPU_PACK_H) v = (old & 0xFFFFu) | (uint32_t) toHalf (asF (v)) << 16;
						wr[slot][l] = v; wrMask[slot][l] = true; wrAny[slot] = true;
						continue;
					}
					switch (waddr)
					{
					case V3D_QPU_WADDR_NOP: break;
					case V3D_QPU_WADDR_TLB: case V3D_QPU_WADDR_TLBU:
						if ((size_t) l < np) { Pixel &p = pixels[base + (size_t) l]; if (p.nTlb < 4) p.tlbWords[p.nTlb++] = v; }
						break;
					case V3D_QPU_WADDR_TMUT: s.tmuT[l] = v; s.haveT = true; break;
					case V3D_QPU_WADDR_TMUS:
					{
						if (s.ncfg < 1) { run.error = "a TMU lookup without its configuration (wrtmuc)"; return false; }
						auto it = run.textures.find (s.cfg[0] & ~15u);
						if (it == run.textures.end ()) { run.error = "a TMU lookup of an unknown texture"; return false; }
						float o[4];
						sample (it->second, asF (v), s.haveT ? asF (s.tmuT[l]) : 0.0f, o);
						s.fifo[l].push_back ((uint32_t) toHalf (o[0]) | (uint32_t) toHalf (o[1]) << 16);
						s.fifo[l].push_back ((uint32_t) toHalf (o[2]) | (uint32_t) toHalf (o[3]) << 16);
						break;
					}
					case V3D_QPU_WADDR_RECIP: case V3D_QPU_WADDR_RSQRT: case V3D_QPU_WADDR_EXP: case V3D_QPU_WADDR_LOG: case V3D_QPU_WADDR_SIN:
					{
						float x = asF (v), y;
						if (waddr == V3D_QPU_WADDR_RECIP) y = 1.0f / x;
						else if (waddr == V3D_QPU_WADDR_RSQRT) y = 1.0f / sqrtf (x);
						else if (waddr == V3D_QPU_WADDR_EXP) y = exp2f (x);
						else if (waddr == V3D_QPU_WADDR_LOG) y = log2f (x);
						else y = sinf (x * 3.14159265f);
						bool placed = false;
						for (auto &p : s.pend) if (p.reg == 4 && p.at == ip + 2) { p.v[l] = asU (y); p.mask[l] = true; placed = true; }
						if (!placed) { Pending p; p.reg = 4; p.at = ip + 2; memset (p.mask, 0, sizeof p.mask); p.v[l] = asU (y); p.mask[l] = true; s.pend.push_back (p); }
						break;
					}
					default: run.error = std::string ("magic write not modelled (") + who + "): " + v3d_qpu_magic_waddr_name (&d, (v3d_qpu_waddr) waddr); return false;
					}
				}
				return true;
			};
			if (haveAdd)
			{
				if (in.alu.add.op == V3D_QPU_A_SETMSF)
				{
					uint32_t a[L];
					for (int l = 0; l < L; l++) a[l] = unpackIn (read (in.alu.add.a.mux, l), in.alu.add.a.unpack, false);
					for (int l = 0; l < L; l++) if (condOk (in.flags.ac, l)) s.msf[l] = a[l] & 15;
				}
				else if (in.alu.add.op != V3D_QPU_A_TMUWT)
				{
					push (in.flags.apf, addR, addF, 0, 0);
					if (v3d_qpu_add_op_has_dst (in.alu.add.op) && !dest (in.alu.add.waddr, in.alu.add.magic_write, in.alu.add.output_pack, addR, in.flags.ac, addF, "add")) { delete S; return false; }
				}
			}
			if (haveMul)
			{
				push (in.flags.mpf, mulR, mulF, 0, 1);
				if (!dest (in.alu.mul.waddr, in.alu.mul.magic_write, in.alu.mul.output_pack, mulR, in.flags.mc, mulF, "mul")) { delete S; return false; }
			}
			memcpy (s.fa, faNew, sizeof faNew); memcpy (s.fb, fbNew, sizeof fbNew);
			// the signals
			auto sigWrite = [&] (const Vec &v) {
				int slot = !in.sig_magic ? 6 + in.sig_addr : in.sig_addr <= 5 ? in.sig_addr : -1;
				if (slot < 0) return;
				for (int l = 0; l < L; l++) { wr[slot][l] = v[l]; wrMask[slot][l] = true; }
				wrAny[slot] = true;
			};
			if (in.sig.wrtmuc)
			{
				if (s.uni >= run.uniforms.size ()) { run.error = "wrtmuc: no uniform left"; delete S; return false; }
				if (s.ncfg < 4) s.cfg[s.ncfg++] = run.uniforms[s.uni];
				s.uni++;
			}
			if (in.sig.ldunif || in.sig.ldunifrf)
			{
				if (s.uni >= run.uniforms.size ()) { run.error = "ldunif: no uniform left"; delete S; return false; }
				Vec v; for (int l = 0; l < L; l++) v[l] = run.uniforms[s.uni];
				s.uni++;
				if (in.sig.ldunif) { for (int l = 0; l < L; l++) { wr[5][l] = v[l]; wrMask[5][l] = true; } wrAny[5] = true; }
				else sigWrite (v);
			}
			if (in.sig.ldvary)
			{
				Vec v, c;
				for (int l = 0; l < L; l++)
				{
					float p = 0;
					if ((size_t) l < np) { Pixel &px = pixels[base + (size_t) l]; if (varyAt[l] < px.vary.size ()) p = px.vary[varyAt[l]]; }
					varyAt[l]++;
					v[l] = asU (p); c[l] = 0;
				}
				sigWrite (v);
				Pending pd; pd.reg = 5; pd.at = ip + 1; for (int l = 0; l < L; l++) { pd.v[l] = c[l]; pd.mask[l] = true; }
				s.pend.push_back (pd);
			}
			if (in.sig.ldtmu)
			{
				Vec v;
				for (int l = 0; l < L; l++)
				{
					if (s.fifo[l].empty ()) { if ((size_t) l < np) { run.error = "ldtmu: the TMU FIFO is empty"; delete S; return false; } v[l] = 0; continue; }
					v[l] = s.fifo[l].front (); s.fifo[l].erase (s.fifo[l].begin ());
				}
				sigWrite (v);
			}
			if (in.sig.thrsw && !(ip > 0 && ins[(size_t) ip - 1].sig.thrsw))	// (a switch after its 2 delay slots -- a pair
			{									// is one switch --: the accumulators lost)
				for (int r = 0; r < 6; r++)
				{
					Pending pd; pd.reg = r; pd.at = ip + 2;
					for (int l = 0; l < L; l++) { pd.v[l] = 0xDEAD0000u + (uint32_t) (r * 16 + l); pd.mask[l] = true; }
					s.pend.push_back (pd);
				}
			}
			if (in.sig.ldvpm || in.sig.ldtlb || in.sig.ldtlbu || in.sig.ldunifa || in.sig.ldunifarf) { run.error = "a signal not modelled"; delete S; return false; }
			// the writes of this instruction, then the delayed ones due now
			for (int k = 0; k < 70; k++)
				if (wrAny[k])
					for (int l = 0; l < L; l++)
						if (wrMask[k][l]) { if (k < 6) s.acc[k][l] = wr[k][l]; else s.rf[k - 6][l] = wr[k][l]; }
			for (size_t k = 0; k < s.pend.size (); )
			{
				if (s.pend[k].at == ip)
				{
					for (int l = 0; l < L; l++) if (s.pend[k].mask[l]) s.acc[s.pend[k].reg][l] = s.pend[k].v[l];
					s.pend.erase (s.pend.begin () + (long) k);
				}
				else k++;
			}
			for (const Run::Watch &w : run.watch)
				if (w.ip == ip)
					for (size_t l = 0; l < (size_t) L && l < np; l++)
					{
						int32_t v = (int32_t) (w.reg < 6 ? s.acc[w.reg][l] : s.rf[w.reg - 6][l]);
						if (v < w.lo || v > w.hi)
						{
							char b[160]; snprintf (b, sizeof b, "the value range: after instruction %d, %s%d = %d, not in %lld..%lld",
									      ip, w.reg < 6 ? "r" : "rf", w.reg < 6 ? w.reg : w.reg - 6, v, w.lo, w.hi);
							run.error = b; delete S; return false;
						}
					}
			if (in.alu.mul.magic_write && in.alu.mul.waddr == V3D_QPU_WADDR_TMUS) { s.ncfg = 0; s.haveT = false; }
			if (in.alu.add.magic_write && in.alu.add.waddr == V3D_QPU_WADDR_TMUS && haveAdd) { s.ncfg = 0; s.haveT = false; }
		}
		// the colours: two f16 pairs (R G, B A) -> RGBA8
		for (size_t k = 0; k < np; k++)
		{
			Pixel &p = pixels[base + k];
			if (!s.msf[k] || p.nTlb < 2) continue;
			float c[4] = { fromHalf ((uint16_t) p.tlbWords[0]), fromHalf ((uint16_t) (p.tlbWords[0] >> 16)),
				       fromHalf ((uint16_t) p.tlbWords[1]), fromHalf ((uint16_t) (p.tlbWords[1] >> 16)) };
			uint32_t o = 0;
			for (int j = 0; j < 4; j++)
			{
				float v = c[j] != c[j] ? 0 : c[j] < 0 ? 0 : c[j] > 1 ? 1 : c[j];
				o |= (uint32_t) lrintf (v * 255.0f) << (j * 8);
			}
			p.rgba = o; p.written = true;
		}
		delete S;
	}
	return true;
}

} // namespace qpusim
