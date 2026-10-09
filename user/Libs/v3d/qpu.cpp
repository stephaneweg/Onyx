//
// v3d/qpu.cpp -- see qpu.h.
//
#include "v3d/qpu.h"

extern "C" {
#include "broadcom/common/v3d_device_info.h"
}

namespace qpu
{

static v3d_device_info s_dev;			// (zeroed; .ver set by the first instruction: no static constructor)
static int s_ver = 42, s_regs = 32;		// (setVersion)

bool setVersion (int ver, int regs)
{
	if (ver != 42 && ver != 71) return false;
	if (regs != 16 && regs != 32) return false;
	s_ver = ver; s_regs = regs;
	return true;
}
int version () { return s_ver; }
int rfLimit () { return s_ver >= 71 ? s_regs - 7 : 32; }
int versionOf (const char *t)
{
	if (t == 0) return 0;
	for (; t[0]; t++)
		if (t[0] == 'V' && t[1] == '3' && t[2] == 'D' && t[3] == ' ' && t[4] >= '0' && t[4] <= '9' && t[5] == '.' && t[6] >= '0' && t[6] <= '9')
		{
			int v = (t[4] - '0') * 10 + (t[6] - '0');
			return v == 42 || v == 71 ? v : 0;
		}
	return 0;
}

// V3D 7.1: where a 4.2 program's register lives (-1: beyond the budget)
static int phys71 (const R &x)
{
	int B = s_regs;
	if (x.kind == 0) return x.n == 5 ? 0 : B - 7 + x.n;		// r5 -> rf0 (ldunif, ldvary's C); r0..r4
	if (x.kind != 1) return -1;
	if (x.n == 0) return 3;						// W
	if (x.n == 2) return B - 1;
	if (x.n == 3) return B - 2;
	return x.n < B - 7 ? x.n : -1;
}

static void zero (void *p, unsigned n) { unsigned char *d = (unsigned char *) p; while (n--) *d++ = 0; }

int physical (const R &x) { return s_ver >= 71 ? phys71 (x) : x.kind == 1 ? x.n : -1; }

I::I () : nrf (0), ok (true)
{
	s_dev.ver = (uint8_t) s_ver;
	zero (&in, sizeof in);
	in.type = V3D_QPU_INSTR_TYPE_ALU;
	in.alu.add.op = V3D_QPU_A_NOP; in.alu.add.waddr = V3D_QPU_WADDR_NOP; in.alu.add.magic_write = true;
	in.alu.mul.op = V3D_QPU_M_NOP; in.alu.mul.waddr = V3D_QPU_WADDR_NOP; in.alu.mul.magic_write = true;
	rfregs[0] = rfregs[1] = -1;
}

// a source: its mux (an accumulator, register-file port A or B, the small immediate on B)
int I::src (R x, v3d_qpu_mux *mux)
{
	if (x.kind == 0) { *mux = (v3d_qpu_mux) x.n; return 1; }
	if (x.kind == 1)
	{
		for (int i = 0; i < nrf; i++)
			if (rfregs[i] == x.n) { *mux = i == 0 ? V3D_QPU_MUX_A : V3D_QPU_MUX_B; return 1; }
		if (nrf == 0) { in.raddr_a = (unsigned char) x.n; rfregs[nrf++] = x.n; *mux = V3D_QPU_MUX_A; return 1; }
		if (nrf == 1 && !in.sig.small_imm_b) { in.raddr_b = (unsigned char) x.n; rfregs[nrf++] = x.n; *mux = V3D_QPU_MUX_B; return 1; }
		return 0;						// (a third register-file read)
	}
	if (x.kind == 3)
	{
		uint32_t packed;
		if (!v3d_qpu_small_imm_pack (&s_dev, (uint32_t) x.n, &packed)) return 0;
		if (in.sig.small_imm_b && in.raddr_b != packed) return 0;
		if (nrf == 2) return 0;
		in.sig.small_imm_b = true; in.raddr_b = (unsigned char) packed;
		*mux = V3D_QPU_MUX_B;
		return 1;
	}
	return 0;
}

// V3D 7.1: an input -- its register's address, or the instruction's one small immediate (cls: 0 add a,
// 1 add b, 2 mul a, 3 mul b)
int I::src71 (R x, v3d_qpu_input *inp, int cls)
{
	if (x.kind == 0 || x.kind == 1)
	{
		int p = phys71 (x);
		if (p < 0) return 0;
		inp->raddr = (unsigned char) p;
		return 1;
	}
	if (x.kind == 3)
	{
		uint32_t packed;
		if (!v3d_qpu_small_imm_pack (&s_dev, (uint32_t) x.n, &packed)) return 0;
		if (in.sig.small_imm_a + in.sig.small_imm_b + in.sig.small_imm_c + in.sig.small_imm_d) return 0;
		if (cls == 0) in.sig.small_imm_a = true; else if (cls == 1) in.sig.small_imm_b = true;
		else if (cls == 2) in.sig.small_imm_c = true; else in.sig.small_imm_d = true;
		inp->raddr = (unsigned char) packed;
		return 1;
	}
	return 0;
}

// V3D 7.1: "mov recip, x" (4.2's SFU, the result in r4 later) -> "recip r4, x" in the add slot
bool I::sfu71 (R d, R x)
{
	if (d.kind != 2) return false;
	v3d_qpu_add_op op;
	switch (d.n)
	{
	case V3D_QPU_WADDR_RECIP: op = V3D_QPU_A_RECIP; break;
	case V3D_QPU_WADDR_RSQRT: op = V3D_QPU_A_RSQRT; break;
	case V3D_QPU_WADDR_EXP: op = V3D_QPU_A_EXP; break;
	case V3D_QPU_WADDR_LOG: op = V3D_QPU_A_LOG; break;
	case V3D_QPU_WADDR_SIN: op = V3D_QPU_A_SIN; break;
	case V3D_QPU_WADDR_RSQRT2: op = V3D_QPU_A_RSQRT2; break;
	default: return false;
	}
	if (in.alu.add.op != V3D_QPU_A_NOP) { ok = false; return true; }	// (the add slot taken)
	in.alu.add.op = op;
	in.alu.add.waddr = (uint8_t) phys71 (r4); in.alu.add.magic_write = false;
	in.alu.add.output_pack = V3D_QPU_PACK_NONE;
	if (!src71 (x, &in.alu.add.a, 0)) ok = false;
	in.alu.add.a.unpack = (v3d_qpu_input_unpack) x.unpack;
	return true;
}

static bool dst (R d, uint8_t *waddr, bool *magicW, v3d_qpu_output_pack *pack)
{
	*pack = (v3d_qpu_output_pack) d.pack;
	if (s_ver >= 71 && (d.kind == 0 || d.kind == 1))
	{
		int p = phys71 (d);
		if (p < 0) return false;
		*waddr = (uint8_t) p; *magicW = false;
		return true;
	}
	if (d.kind == 0) { *waddr = (uint8_t) d.n; *magicW = true; return true; }	// (r0..r5 are magic addresses 0..5)
	if (d.kind == 1) { *waddr = (uint8_t) d.n; *magicW = false; return true; }
	if (d.kind == 2) { *waddr = (uint8_t) d.n; *magicW = true; return true; }
	if (d.kind == 4) { *waddr = V3D_QPU_WADDR_NOP; *magicW = true; return true; }
	return false;
}

I &I::a (v3d_qpu_add_op op, R d, R x, R y)
{
	if (s_ver >= 71 && op == V3D_QPU_A_VPMWT) return *this;		// (4.2's GFXH-1684: none on 7.1, as Mesa)
	if (s_ver >= 71 && in.alu.add.op != V3D_QPU_A_NOP) { ok = false; return *this; }	// (the add slot: an SFU's already)
	in.alu.add.op = op;
	int ns = v3d_qpu_add_op_num_src (op);
	if (v3d_qpu_add_op_has_dst (op)) { if (!dst (d, &in.alu.add.waddr, &in.alu.add.magic_write, &in.alu.add.output_pack)) ok = false; }
	// (ops without a destination: the operands are the first arguments)
	R s0 = v3d_qpu_add_op_has_dst (op) ? x : d, s1 = v3d_qpu_add_op_has_dst (op) ? y : x;
	if (s_ver >= 71)
	{
		if (ns >= 1) { if (!src71 (s0, &in.alu.add.a, 0)) ok = false; in.alu.add.a.unpack = (v3d_qpu_input_unpack) s0.unpack; }
		if (ns >= 2) { if (!src71 (s1, &in.alu.add.b, 1)) ok = false; in.alu.add.b.unpack = (v3d_qpu_input_unpack) s1.unpack; }
		return *this;
	}
	if (ns >= 1) { if (!src (s0, &in.alu.add.a.mux)) ok = false; in.alu.add.a.unpack = (v3d_qpu_input_unpack) s0.unpack; }
	if (ns >= 2) { if (!src (s1, &in.alu.add.b.mux)) ok = false; in.alu.add.b.unpack = (v3d_qpu_input_unpack) s1.unpack; }
	return *this;
}

I &I::m (v3d_qpu_mul_op op, R d, R x, R y)
{
	if (s_ver >= 71 && op == V3D_QPU_M_MOV && sfu71 (d, x)) return *this;	// (the SFU: an add op on 7.1)
	in.alu.mul.op = op;
	int ns = v3d_qpu_mul_op_num_src (op);
	if (v3d_qpu_mul_op_has_dst (op)) { if (!dst (d, &in.alu.mul.waddr, &in.alu.mul.magic_write, &in.alu.mul.output_pack)) ok = false; }
	R s0 = v3d_qpu_mul_op_has_dst (op) ? x : d, s1 = v3d_qpu_mul_op_has_dst (op) ? y : x;
	if (s_ver >= 71)
	{
		if (ns >= 1) { if (!src71 (s0, &in.alu.mul.a, 2)) ok = false; in.alu.mul.a.unpack = (v3d_qpu_input_unpack) s0.unpack; }
		if (ns >= 2) { if (!src71 (s1, &in.alu.mul.b, 3)) ok = false; in.alu.mul.b.unpack = (v3d_qpu_input_unpack) s1.unpack; }
		return *this;
	}
	if (ns >= 1) { if (!src (s0, &in.alu.mul.a.mux)) ok = false; in.alu.mul.a.unpack = (v3d_qpu_input_unpack) s0.unpack; }
	if (ns >= 2) { if (!src (s1, &in.alu.mul.b.mux)) ok = false; in.alu.mul.b.unpack = (v3d_qpu_input_unpack) s1.unpack; }
	return *this;
}

void I::sigDst (R d)
{
	if (s_ver >= 71 && (d.kind == 0 || d.kind == 1))
	{
		int p = phys71 (d);
		if (p < 0) { ok = false; return; }
		in.sig_addr = (uint8_t) p; in.sig_magic = false;
		return;
	}
	if (d.kind == 1) { in.sig_addr = (uint8_t) d.n; in.sig_magic = false; }
	else if (d.kind == 0 || d.kind == 2) { in.sig_addr = (uint8_t) d.n; in.sig_magic = true; }
	else ok = false;
}

Prog &Prog::operator<< (const I &i)
{
	if (n >= MAX) { if (!bad) badAt = n; bad = true; return *this; }
	uint64_t word = 0;
	if (!i.ok || !v3d_qpu_instr_pack (&s_dev, &i.in, &word)) { if (!bad) badAt = n; bad = true; }
	w[n++] = word;
	return *this;
}

void Prog::set (int k, const I &i)
{
	if (k < 0 || k >= n) return;
	uint64_t word = 0;
	if (!i.ok || !v3d_qpu_instr_pack (&s_dev, &i.in, &word)) { if (!bad) badAt = k; bad = true; }
	w[k] = word;
}

} // namespace qpu
