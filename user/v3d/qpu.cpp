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

static void zero (void *p, unsigned n) { unsigned char *d = (unsigned char *) p; while (n--) *d++ = 0; }

I::I () : nrf (0), ok (true)
{
	s_dev.ver = 42;
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

static bool dst (R d, uint8_t *waddr, bool *magicW, v3d_qpu_output_pack *pack)
{
	*pack = (v3d_qpu_output_pack) d.pack;
	if (d.kind == 0) { *waddr = (uint8_t) d.n; *magicW = true; return true; }	// (r0..r5 are magic addresses 0..5)
	if (d.kind == 1) { *waddr = (uint8_t) d.n; *magicW = false; return true; }
	if (d.kind == 2) { *waddr = (uint8_t) d.n; *magicW = true; return true; }
	if (d.kind == 4) { *waddr = V3D_QPU_WADDR_NOP; *magicW = true; return true; }
	return false;
}

I &I::a (v3d_qpu_add_op op, R d, R x, R y)
{
	in.alu.add.op = op;
	int ns = v3d_qpu_add_op_num_src (op);
	if (v3d_qpu_add_op_has_dst (op)) { if (!dst (d, &in.alu.add.waddr, &in.alu.add.magic_write, &in.alu.add.output_pack)) ok = false; }
	// (ops without a destination: the operands are the first arguments)
	R s0 = v3d_qpu_add_op_has_dst (op) ? x : d, s1 = v3d_qpu_add_op_has_dst (op) ? y : x;
	if (ns >= 1) { if (!src (s0, &in.alu.add.a.mux)) ok = false; in.alu.add.a.unpack = (v3d_qpu_input_unpack) s0.unpack; }
	if (ns >= 2) { if (!src (s1, &in.alu.add.b.mux)) ok = false; in.alu.add.b.unpack = (v3d_qpu_input_unpack) s1.unpack; }
	return *this;
}

I &I::m (v3d_qpu_mul_op op, R d, R x, R y)
{
	in.alu.mul.op = op;
	int ns = v3d_qpu_mul_op_num_src (op);
	if (v3d_qpu_mul_op_has_dst (op)) { if (!dst (d, &in.alu.mul.waddr, &in.alu.mul.magic_write, &in.alu.mul.output_pack)) ok = false; }
	R s0 = v3d_qpu_mul_op_has_dst (op) ? x : d, s1 = v3d_qpu_mul_op_has_dst (op) ? y : x;
	if (ns >= 1) { if (!src (s0, &in.alu.mul.a.mux)) ok = false; in.alu.mul.a.unpack = (v3d_qpu_input_unpack) s0.unpack; }
	if (ns >= 2) { if (!src (s1, &in.alu.mul.b.mux)) ok = false; in.alu.mul.b.unpack = (v3d_qpu_input_unpack) s1.unpack; }
	return *this;
}

void I::sigDst (R d)
{
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

} // namespace qpu
