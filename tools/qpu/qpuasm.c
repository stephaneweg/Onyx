/*
 * qpuasm -- a small assembler for the V3D 4.2 QPU (the Raspberry Pi 4's GPU shader cores), for
 * the kernel's hand-written shaders (kernel/sys/v3d_shaders.inc). The encoding is Mesa's
 * (mesa/broadcom/qpu, MIT): the text is parsed into a struct v3d_qpu_instr, packed by
 * v3d_qpu_instr_pack, then disassembled again and compared to the source line -- a line is
 * accepted only when the disassembly gives it back word for word, so what is assembled is
 * what was written.
 *
 * The syntax is the one of Mesa's disassembler, one instruction a line:
 *
 *     add-op[.cond][.pf][.uf] [dst[.pack], src[.unpack], src]  ;  mul-op ...  [; signal] ...
 *
 *   registers r0..r5, rf0..rf63, the magic write addresses by name (tlb, tmus, tmut, recip,
 *   vpm, -, ...), small immediates (-16..15, or 0x........ for the float ones), signals
 *   thrsw, ldunif, ldunifrf.rfN, ldvary.rN / .rfN, ldtmu.rN / .rfN, wrtmuc, ldvpm, ...
 *   '#' starts a comment; ".name NAME vertex|coord|frag" starts a new shader (the C array's name).
 *
 * Each shader is then checked against the hardware's instruction restrictions (Mesa's
 * qpu_validate.c and its scheduler's timing rules): r4 read >= 3 instructions after an SFU
 * write, r5 >= 2 after an ldvary, one of TMU / SFU / VPM / TLB a line, no regfile read right after
 * its write, the thread switches (last THRSW = two in a row, program end = THRSW + 2 delay
 * slots, no regfile write there), no TLB access before the scoreboard wait.
 *
 *   qpuasm in.qasm out.inc      assemble (a C header of u64 arrays, each word commented)
 *   qpuasm -d HEX ...           disassemble words
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "broadcom/common/v3d_device_info.h"
#include "broadcom/qpu/qpu_disasm.h"
#include "broadcom/qpu/qpu_instr.h"

static struct v3d_device_info devinfo = { .ver = 42 };
static const char *g_file; static int g_line;

static void die (const char *msg, const char *what)
{
	fprintf (stderr, "%s:%d: %s%s%s\n", g_file, g_line, msg, what ? ": " : "", what ? what : "");
	exit (1);
}

/* ---- lookups over Mesa's own name tables ------------------------------------------------------- */
static int find_add_op (const char *s)
{
	for (int op = 0; op <= V3D_QPU_A_SETNNMODE_SS; op++)
	{
		const char *n = v3d_qpu_add_op_name ((enum v3d_qpu_add_op) op);
		if (n && strcmp (n, s) == 0) return op;
	}
	return -1;
}
static int find_mul_op (const char *s)
{
	for (int op = 0; op <= V3D_QPU_M_V8DOT; op++)
	{
		const char *n = v3d_qpu_mul_op_name ((enum v3d_qpu_mul_op) op);
		if (n && strcmp (n, s) == 0) return op;
	}
	return -1;
}
static int find_magic (const char *s)
{
	for (int w = 0; w < 64; w++)
	{
		if (w <= 5) { char r[4] = { 'r', (char) ('0' + w), 0 }; if (strcmp (r, s) == 0) return w; continue; }
		const char *n = v3d_qpu_magic_waddr_name (&devinfo, (enum v3d_qpu_waddr) w);
		if (n && strcmp (n, s) == 0) return w;
	}
	return -1;
}
static int find_unpack (const char *s)	/* s includes the dot */
{
	static const int list[] = { V3D_QPU_UNPACK_ABS, V3D_QPU_UNPACK_L, V3D_QPU_UNPACK_H,
		V3D_QPU_UNPACK_REPLICATE_32F_16, V3D_QPU_UNPACK_REPLICATE_L_16, V3D_QPU_UNPACK_REPLICATE_H_16,
		V3D_QPU_UNPACK_SWAP_16 };
	for (unsigned i = 0; i < sizeof list / sizeof list[0]; i++)
		if (strcmp (v3d_qpu_unpack_name ((enum v3d_qpu_input_unpack) list[i]), s) == 0) return list[i];
	return -1;
}

/* ---- one instruction --------------------------------------------------------------------------- */
struct rfuse { int n; int regs[2]; };		/* the two register-file read ports, A and B */

static char *trim (char *s)
{
	while (isspace ((unsigned char) *s)) s++;
	char *e = s + strlen (s);
	while (e > s && isspace ((unsigned char) e[-1])) *--e = 0;
	return s;
}

/* splits "a, b, c" into at most 3 trimmed tokens */
static int split_args (char *s, char **out)
{
	int n = 0;
	s = trim (s);
	if (!*s) return 0;
	for (;;)
	{
		char *c = strchr (s, ',');
		if (c) *c = 0;
		if (n == 3) die ("too many operands", 0);
		out[n++] = trim (s);
		if (!c) break;
		s = c + 1;
	}
	return n;
}

/* an operand: register / small immediate, with an optional unpack suffix */
static void parse_src (struct v3d_qpu_instr *in, struct rfuse *rf, char *tok, enum v3d_qpu_mux *mux,
		       enum v3d_qpu_input_unpack *unpack)
{
	*unpack = V3D_QPU_UNPACK_NONE;
	char *dot = (tok[0] == '-' || isdigit ((unsigned char) tok[0])) ? 0 : strchr (tok, '.');
	if (dot)
	{
		int u = find_unpack (dot);
		if (u < 0) die ("unknown unpack", dot);
		*unpack = (enum v3d_qpu_input_unpack) u;
		*dot = 0;
	}
	if (tok[0] == 'r' && tok[1] == 'f')
	{
		int r = atoi (tok + 2);
		if (r < 0 || r > 63) die ("bad register", tok);
		for (int i = 0; i < rf->n; i++)
			if (rf->regs[i] == r) { *mux = i == 0 ? V3D_QPU_MUX_A : V3D_QPU_MUX_B; return; }
		if (rf->n == 2 || (rf->n == 1 && in->sig.small_imm_b)) die ("more than two register-file reads", tok);
		if (rf->n == 0 && !in->sig.small_imm_b) { in->raddr_a = (uint8_t) r; rf->regs[rf->n++] = r; *mux = V3D_QPU_MUX_A; }
		else if (rf->n == 0) { in->raddr_a = (uint8_t) r; rf->regs[rf->n++] = r; *mux = V3D_QPU_MUX_A; }
		else { in->raddr_b = (uint8_t) r; rf->regs[rf->n++] = r; *mux = V3D_QPU_MUX_B; }
		return;
	}
	if (tok[0] == 'r' && tok[1] >= '0' && tok[1] <= '5' && tok[2] == 0) { *mux = (enum v3d_qpu_mux) (tok[1] - '0'); return; }
	/* a small immediate */
	char *end;
	unsigned long v = strtoul (tok, &end, 0);
	if (tok[0] == '-') v = (unsigned long) strtol (tok, &end, 0);
	if (*end) die ("bad operand", tok);
	uint32_t packed;
	if (!v3d_qpu_small_imm_pack (&devinfo, (uint32_t) v, &packed)) die ("not a small immediate", tok);
	if (in->sig.small_imm_b && in->raddr_b != packed) die ("two different small immediates", tok);
	if (rf->n == 2) die ("small immediate: register-file port B already used", tok);
	in->sig.small_imm_b = true;
	in->raddr_b = (uint8_t) packed;
	*mux = V3D_QPU_MUX_B;
}

static void parse_dst (char *tok, uint8_t *waddr, bool *magic, enum v3d_qpu_output_pack *pack)
{
	*pack = V3D_QPU_PACK_NONE;
	char *dot = strchr (tok, '.');
	if (dot)
	{
		if (strcmp (dot, ".l") == 0) *pack = V3D_QPU_PACK_L;
		else if (strcmp (dot, ".h") == 0) *pack = V3D_QPU_PACK_H;
		else die ("unknown pack", dot);
		*dot = 0;
	}
	if (tok[0] == 'r' && tok[1] == 'f') { *waddr = (uint8_t) atoi (tok + 2); *magic = false; return; }
	int m = find_magic (tok);
	if (m < 0) die ("unknown destination", tok);
	*waddr = (uint8_t) m; *magic = true;
}

/* "op.cond.pf.uf" -> op name + flags */
static void parse_opname (char *s, enum v3d_qpu_cond *c, enum v3d_qpu_pf *pf, enum v3d_qpu_uf *uf)
{
	char *dot = strchr (s, '.');
	if (!dot) return;
	char suf[64]; snprintf (suf, sizeof suf, "%s", dot);
	*dot = 0;
	char *p = suf;
	while (*p == '.')
	{
		char *q = strchr (p + 1, '.');
		char one[32]; int len = q ? (int) (q - p) : (int) strlen (p);
		snprintf (one, sizeof one, "%.*s", len, p);
		int ok = 0;
		for (int i = 1; i <= V3D_QPU_COND_IFNB && !ok; i++)
			if (strcmp (v3d_qpu_cond_name ((enum v3d_qpu_cond) i), one) == 0) { *c = (enum v3d_qpu_cond) i; ok = 1; }
		for (int i = 1; i <= V3D_QPU_PF_PUSHC && !ok; i++)
			if (strcmp (v3d_qpu_pf_name ((enum v3d_qpu_pf) i), one) == 0) { *pf = (enum v3d_qpu_pf) i; ok = 1; }
		for (int i = 1; i <= V3D_QPU_UF_NORC && !ok; i++)
			if (strcmp (v3d_qpu_uf_name ((enum v3d_qpu_uf) i), one) == 0) { *uf = (enum v3d_qpu_uf) i; ok = 1; }
		if (!ok) die ("unknown condition / flag", one);
		p += len;
	}
}

static void parse_sig (struct v3d_qpu_instr *in, char *s)
{
	char *dot = strchr (s, '.');
	if (dot)
	{
		*dot = 0;
		char *a = dot + 1;
		if (a[0] == 'r' && a[1] == 'f') { in->sig_addr = (uint8_t) atoi (a + 2); in->sig_magic = false; }
		else { int m = find_magic (a); if (m < 0) die ("bad signal address", a); in->sig_addr = (uint8_t) m; in->sig_magic = true; }
	}
	struct v3d_qpu_sig *g = &in->sig;
	if (!strcmp (s, "thrsw")) g->thrsw = true;
	else if (!strcmp (s, "ldunif")) g->ldunif = true;
	else if (!strcmp (s, "ldunifa")) g->ldunifa = true;
	else if (!strcmp (s, "ldunifrf")) g->ldunifrf = true;
	else if (!strcmp (s, "ldunifarf")) g->ldunifarf = true;
	else if (!strcmp (s, "ldtmu")) g->ldtmu = true;
	else if (!strcmp (s, "ldvary")) g->ldvary = true;
	else if (!strcmp (s, "ldvpm")) g->ldvpm = true;
	else if (!strcmp (s, "ldtlb")) g->ldtlb = true;
	else if (!strcmp (s, "ldtlbu")) g->ldtlbu = true;
	else if (!strcmp (s, "wrtmuc")) g->wrtmuc = true;
	else die ("unknown signal", s);
	if (dot && !(g->ldunifrf || g->ldunifarf || g->ldtmu || g->ldvary || g->ldtlb || g->ldtlbu))
		die ("this signal has no destination", s);
}

static void parse_alu (struct v3d_qpu_instr *in, struct rfuse *rf, char *part, int mul)
{
	part = trim (part);
	char name[64]; int k = 0;
	while (*part && !isspace ((unsigned char) *part) && k < 63) name[k++] = *part++;
	name[k] = 0;
	enum v3d_qpu_cond c = V3D_QPU_COND_NONE; enum v3d_qpu_pf pf = V3D_QPU_PF_NONE; enum v3d_qpu_uf uf = V3D_QPU_UF_NONE;
	parse_opname (name, &c, &pf, &uf);
	char *args[3]; int n = split_args (part, args);
	int op = mul ? find_mul_op (name) : find_add_op (name);
	if (op < 0) die (mul ? "unknown mul op" : "unknown add op", name);
	bool has_dst = mul ? v3d_qpu_mul_op_has_dst ((enum v3d_qpu_mul_op) op) : v3d_qpu_add_op_has_dst ((enum v3d_qpu_add_op) op);
	int nsrc = mul ? v3d_qpu_mul_op_num_src ((enum v3d_qpu_mul_op) op) : v3d_qpu_add_op_num_src ((enum v3d_qpu_add_op) op);
	if (n != (has_dst ? 1 : 0) + nsrc) die ("wrong number of operands for", name);
	uint8_t waddr = V3D_QPU_WADDR_NOP; bool magic = true; enum v3d_qpu_output_pack pack = V3D_QPU_PACK_NONE;
	int a = 0;
	if (has_dst) parse_dst (args[a++], &waddr, &magic, &pack);
	enum v3d_qpu_mux m[2] = { V3D_QPU_MUX_R0, V3D_QPU_MUX_R0 };
	enum v3d_qpu_input_unpack u[2] = { V3D_QPU_UNPACK_NONE, V3D_QPU_UNPACK_NONE };
	for (int i = 0; i < nsrc; i++) parse_src (in, rf, args[a++], &m[i], &u[i]);
	if (mul)
	{
		in->alu.mul.op = (enum v3d_qpu_mul_op) op; in->alu.mul.waddr = waddr; in->alu.mul.magic_write = magic;
		in->alu.mul.output_pack = pack;
		in->alu.mul.a.mux = m[0]; in->alu.mul.a.unpack = u[0]; in->alu.mul.b.mux = m[1]; in->alu.mul.b.unpack = u[1];
		in->flags.mc = c; in->flags.mpf = pf; in->flags.muf = uf;
	}
	else
	{
		in->alu.add.op = (enum v3d_qpu_add_op) op; in->alu.add.waddr = waddr; in->alu.add.magic_write = magic;
		in->alu.add.output_pack = pack;
		in->alu.add.a.mux = m[0]; in->alu.add.a.unpack = u[0]; in->alu.add.b.mux = m[1]; in->alu.add.b.unpack = u[1];
		in->flags.ac = c; in->flags.apf = pf; in->flags.auf = uf;
	}
}

static uint64_t assemble (char *line, struct v3d_qpu_instr *out)
{
	struct v3d_qpu_instr in;
	memset (&in, 0, sizeof in);
	in.type = V3D_QPU_INSTR_TYPE_ALU;
	char *parts[8]; int np = 0;
	for (char *s = line;;)
	{
		char *c = strchr (s, ';');
		if (c) *c = 0;
		if (np == 8) die ("too many parts", 0);
		parts[np++] = trim (s);
		if (!c) break;
		s = c + 1;
	}
	if (np < 2) die ("an instruction is 'add part ; mul part [; signals]'", 0);
	struct rfuse rf = { 0, { 0, 0 } };
	for (int i = 2; i < np; i++) parse_sig (&in, parts[i]);	/* first: small_imm_b is known before */
	parse_alu (&in, &rf, parts[0], 0);
	parse_alu (&in, &rf, parts[1], 1);
	uint64_t w;
	if (!v3d_qpu_instr_pack (&devinfo, &in, &w)) die ("cannot be encoded", 0);
	if (!v3d_qpu_instr_unpack (&devinfo, w, out)) die ("cannot be decoded back", 0);
	return w;
}


/* ---- the instruction restrictions (Mesa's qpu_validate.c + the scheduler's timing rules) ----------- */
struct vstate
{
	int kind;			/* 0 vertex / coordinate (start in the final thread section), 1 fragment */
	int ip, last_sfu, last_thrsw, last_ldvary, end_thrsw, thrsw_count, lock_ip;
	bool last_thrsw_found, thrend_found;
	bool prev_valid; struct v3d_qpu_instr prev;
};

static void vfail (const char *msg) { die ("instruction restriction", msg); }

static int magic_writes (const struct v3d_qpu_instr *in, bool (*pred) (enum v3d_qpu_waddr))
{
	int n = 0;
	if (in->alu.add.op != V3D_QPU_A_NOP && in->alu.add.magic_write && pred ((enum v3d_qpu_waddr) in->alu.add.waddr)) n++;
	if (in->alu.mul.op != V3D_QPU_M_NOP && in->alu.mul.magic_write && pred ((enum v3d_qpu_waddr) in->alu.mul.waddr)) n++;
	return n;
}
static bool is_tmu (enum v3d_qpu_waddr w) { return v3d_qpu_magic_waddr_is_tmu (&devinfo, w); }
static bool is_reserved (enum v3d_qpu_waddr w)
{
	return w == 10 || (w >= 14 && w <= 15) || (w >= 25 && w <= 31) || (w >= 47 && w <= 54) || (w >= 56 && w <= 63);
}
static bool rf_written (const struct v3d_qpu_instr *in, int r)
{
	if (in->alu.add.op != V3D_QPU_A_NOP && v3d_qpu_add_op_has_dst (in->alu.add.op) && !in->alu.add.magic_write && in->alu.add.waddr == r) return true;
	if (in->alu.mul.op != V3D_QPU_M_NOP && v3d_qpu_mul_op_has_dst (in->alu.mul.op) && !in->alu.mul.magic_write && in->alu.mul.waddr == r) return true;
	if (v3d_qpu_sig_writes_address (&devinfo, &in->sig) && !in->sig_magic && in->sig_addr == r) return true;
	return false;
}
static bool writes_any_rf (const struct v3d_qpu_instr *in)
{
	if (in->alu.add.op != V3D_QPU_A_NOP && !in->alu.add.magic_write) return true;
	if (in->alu.mul.op != V3D_QPU_M_NOP && !in->alu.mul.magic_write) return true;
	return v3d_qpu_sig_writes_address (&devinfo, &in->sig) && !in->sig_magic;
}

static void validate (struct vstate *v, const struct v3d_qpu_instr *in)
{
	int ip = v->ip;
	bool in_thrsw_slots = ip - v->last_thrsw < 3;
	int sfu = magic_writes (in, v3d_qpu_magic_waddr_is_sfu);
	int tmu = magic_writes (in, is_tmu), vpm = magic_writes (in, v3d_qpu_magic_waddr_is_vpm);
	int tlb = magic_writes (in, v3d_qpu_magic_waddr_is_tlb), tsy = magic_writes (in, v3d_qpu_magic_waddr_is_tsy);

	if (v->end_thrsw >= 0 && ip > v->end_thrsw + 2) vfail ("an instruction after the program end");
	if (v->prev_valid && v->prev.sig.ldvary && (in->sig.ldunif || in->sig.ldunifa)) vfail ("LDUNIF right after a LDVARY");
	if (in_thrsw_slots && sfu) vfail ("SFU write in THRSW delay slots");
	if (in_thrsw_slots && in->sig.ldvary) vfail ("LDVARY in THRSW delay slots");
	if (magic_writes (in, is_reserved)) vfail ("write to a reserved magic waddr");
	if (ip - v->last_sfu <= 2 && v3d_qpu_uses_mux (in, V3D_QPU_MUX_R4)) vfail ("r4 read too soon after SFU (3 instructions)");
	if (ip - v->last_sfu < 2 && (v3d_qpu_writes_r4 (&devinfo, in) || sfu)) vfail ("r4 / SFU write too soon after SFU");
	if (ip - v->last_ldvary <= 1 && v3d_qpu_uses_mux (in, V3D_QPU_MUX_R5)) vfail ("r5 read too soon after LDVARY (2 instructions)");
	if (tmu + sfu + vpm + tlb + tsy + in->sig.ldtmu + in->sig.ldtlb + in->sig.ldvpm + in->sig.ldtlbu > 1)
		vfail ("only one of TMU, SFU, TSY, TLB, VPM an instruction");
	if (v->prev_valid)	/* regfile read of what the previous instruction wrote (kept conservative) */
	{
		if (v3d_qpu_uses_mux (in, V3D_QPU_MUX_A) && rf_written (&v->prev, in->raddr_a)) vfail ("regfile A read right after its write");
		if (v3d_qpu_uses_mux (in, V3D_QPU_MUX_B) && !in->sig.small_imm_b && rf_written (&v->prev, in->raddr_b)) vfail ("regfile B read right after its write");
	}
	if (v->kind == 1 && (tlb || in->sig.ldtlb || in->sig.ldtlbu) && !(v->last_thrsw_found && ip >= v->lock_ip))
		vfail ("TLB access before the scoreboard wait (last THRSW + 3)");
	if (sfu) v->last_sfu = ip;
	if (in->sig.ldvary) v->last_ldvary = ip;
	if (in->sig.thrsw)
	{
		if (v->last_thrsw_found) { v->thrend_found = true; v->end_thrsw = ip; }
		if (v->last_thrsw == ip - 1)
		{
			if (v->last_thrsw_found) vfail ("two last-THRSW signals");
			v->last_thrsw_found = true;
			v->lock_ip = v->last_thrsw + 3;	/* the last segment's scoreboard wait */
		}
		else
		{
			if (in_thrsw_slots) vfail ("THRSW too close to another THRSW");
			v->thrsw_count++;
			v->last_thrsw = ip;
		}
	}
	if (v->thrend_found && ip - v->last_thrsw <= 2)
	{
		if (writes_any_rf (in)) vfail ("regfile write after THREND");
		if (ip - v->last_thrsw == 2 && in->alu.add.op == V3D_QPU_A_TMUWT) vfail ("TMUWT in the last instruction");
	}
	v->prev = *in; v->prev_valid = true;
	v->ip++;
}

static void validate_end (struct vstate *v)
{
	if (v->ip == 0) return;
	if (v->thrsw_count > 1 && !v->last_thrsw_found) die ("instruction restriction", "thread switches without a last-THRSW");
	if (!v->thrend_found) die ("instruction restriction", "no program-end THRSW");
	if (v->ip != v->end_thrsw + 3) die ("instruction restriction", "the program must end 2 instructions after its end THRSW");
}

/* the text without any whitespace: what must survive the round trip */
static void squeeze (const char *s, char *out, size_t cap)
{
	size_t n = 0;
	for (; *s && n + 1 < cap; s++) if (!isspace ((unsigned char) *s)) out[n++] = *s;
	out[n] = 0;
}

int main (int argc, char **argv)
{
	if (argc >= 2 && strcmp (argv[1], "-d") == 0)
	{
		for (int i = 2; i < argc; i++)
		{
			uint64_t v = strtoull (argv[i], 0, 16);
			printf ("%016llX  %s\n", (unsigned long long) v, v3d_qpu_disasm (&devinfo, v));
		}
		return 0;
	}
	if (argc != 3) { fprintf (stderr, "usage: qpuasm in.qasm out.inc | qpuasm -d HEX...\n"); return 2; }
	g_file = argv[1];
	FILE *f = fopen (argv[1], "r");
	if (!f) { perror (argv[1]); return 1; }
	static char out[1 << 20]; size_t no = 0;
	no += (size_t) snprintf (out + no, sizeof out - no, "// Generated by tools/qpu/qpuasm from %s -- do not edit.\n", argv[1]);
	char buf[512]; int open = 0, count = 0;
	struct vstate vs; memset (&vs, 0, sizeof vs);
	while (fgets (buf, sizeof buf, f))
	{
		g_line++;
		char *h = strchr (buf, '#'); if (h) *h = 0;
		char *s = trim (buf);
		if (!*s) continue;
		if (strncmp (s, ".name", 5) == 0)
		{
			if (open) { validate_end (&vs); no += (size_t) snprintf (out + no, sizeof out - no, "};\n"); }
			char nm[128], kind[32] = "";
			if (sscanf (s + 5, "%127s %31s", nm, kind) < 1) die (".name NAME vertex|coord|frag", 0);
			memset (&vs, 0, sizeof vs);
			vs.last_sfu = vs.last_thrsw = vs.last_ldvary = -10; vs.end_thrsw = -1;
			if (!strcmp (kind, "frag")) vs.kind = 1;
			else if (!strcmp (kind, "vertex") || !strcmp (kind, "coord")) { vs.kind = 0; vs.last_thrsw_found = true; }
			else die (".name NAME vertex|coord|frag", kind);
			no += (size_t) snprintf (out + no, sizeof out - no, "static const u64 %s[] = {\n", nm);
			open = 1; continue;
		}
		if (!open) die ("an instruction before the first .name", 0);
		char copy[512]; snprintf (copy, sizeof copy, "%s", s);
		struct v3d_qpu_instr in;
		uint64_t w = assemble (s, &in);
		validate (&vs, &in);
		const char *d = v3d_qpu_disasm (&devinfo, w);
		char a[512], b[512];
		squeeze (copy, a, sizeof a); squeeze (d, b, sizeof b);
		if (strcmp (a, b) != 0)
		{
			fprintf (stderr, "%s:%d: the encoding does not give the line back\n  source: %s\n  gives:  %s\n", g_file, g_line, copy, d);
			return 1;
		}
		no += (size_t) snprintf (out + no, sizeof out - no, "\t0x%016llXull,\t// %s\n", (unsigned long long) w, d);
		count++;
	}
	fclose (f);
	if (open) validate_end (&vs);
	if (open) no += (size_t) snprintf (out + no, sizeof out - no, "};\n");
	FILE *o = fopen (argv[2], "w");
	if (!o) { perror (argv[2]); return 1; }
	fwrite (out, 1, no, o);
	fclose (o);
	printf ("qpuasm: %d instructions -> %s\n", count, argv[2]);
	return 0;
}
