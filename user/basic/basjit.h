//
// basic/basjit.h -- Onyx BASIC in machine code (AArch64): the program's bytecode translated once, when
// it starts, and run instead of the VM's loop. Included by basvm.cpp (it works on the VM's own state).
//
// The machine code and the VM share everything -- the value stack, the frames, the variables -- so the
// translation need not know every instruction: what it does not translate is ONE call to the VM
// (VM::nativeStep: that instruction, then what the loop does between two instructions -- an error's
// handler, an event, a destructor), which says where the program goes on. So PRINT, strings, files,
// graphics, the controls, SUB calls, GOSUB, ON ERROR / RESUME, ON TIMER / ON KEY, CHAIN behave as on the
// VM: they ARE the VM. What is translated is what a program computes with:
//
//   * numbers: constants, numeric variables (globals, locals, by-reference parameters), + - * / \ MOD,
//     the comparisons (joined to the jump that follows), AND OR XOR EQV IMP NOT, the INTEGER / LONG
//     stores (rounding, overflow), the elements of numeric arrays (1 or 2 dimensions), the jumps.
//     Numbers being computed stay in d8..d15: the top of the VM's stack, as far as it is numbers, lives
//     in registers (`regs`), written back to the stack (`flush`) before anything else looks at it;
//   * an instruction that cannot go on in machine code (a division by zero, an index out of range, an
//     overflow, an array not DIMmed yet) puts its operands back and gives the instruction to the VM,
//     which fails or does it its own way: the errors, their lines and RESUME are the VM's.
//
// Entry points: the machine code may be entered where the registers hold nothing -- a jump's target, a
// SUB's start, after an instruction left to the VM, a statement's start or end (RESUME); `table` gives
// the code of each. A jump back counts down; at zero VM::nativeTick pumps the window and the events.
//
// Registers: x19 the VM, x20 the globals, x21 the current SUB's locals (read again at each entry),
// x22 the value stack, x23 the constants, x24 the table, w25 the countdown, x26 &sp, x27 &nloc,
// x28 nativeStep; x9..x17, d0..d3 scratch.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#if defined(__aarch64__) && !defined(BAS_NO_NATIVE)

struct Native
{
	unsigned *base; unsigned words;
	void **table; int npc;
	long (*enter) (VM *vm, long pc);
};

static long natStepC (VM *vm, long pc) { return vm->nativeStep (pc); }
static long natTickC (VM *vm, long pc) { return vm->nativeTick (pc); }
static long natStackC (VM *vm, long pc) { vm->pc = (int) pc; vm->opPc = (int) pc; vm->fail ("Out of stack space"); return vm->nativeAfter (); }

class Jit
{
public:
	typedef unsigned u32;
	typedef unsigned long long u64;
	enum { XVM = 19, XG = 20, XLOC = 21, XSTK = 22, XNUM = 23, XTAB = 24, WTICK = 25, XSP = 26, XNLOC = 27, XSTEP = 28 };
	enum { EQ = 0, NE, HS, LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE };
	enum { TICKS = 2048, VSIZE = 24, V_N = 8, V_P = 16 };

	VM &vm; Program *P; const int *code; int n;
	u32 *base, *p, *lim;
	void **table;
	unsigned char *start, *entry;		// per pc: an instruction starts here / an entry point
	int *procAt;				// per pc: the SUB it is in (-1: the main module)
	u32 **label;				// per pc: its machine code (for the jumps)
	bool bad;
	// the numbers on top of the stack that are in registers: regs[0] the deepest
	int regs[8], nreg; unsigned freeMask;
	// jumps forward (to a pc not translated yet) and the out-of-line paths
	struct Fix { u32 *at; int pc; };
	Vec<Fix> fixes;
	struct Stub { u32 *at; int kind; int pc; int target; int regs[8]; int nreg; };	// kind 0 bail, 1 back edge, 2 stack
	Vec<Stub> stubs;
	u32 *dispatch, *leave;

	Jit (VM &v) : vm (v), P (v.P), code (v.P->code.d), n (v.P->code.n), base (0), p (0), lim (0), table (0),
		start (0), entry (0), procAt (0), label (0), bad (false), nreg (0), freeMask (0xFF), dispatch (0), leave (0) {}
	~Jit () { delete [] start; delete [] entry; delete [] procAt; delete [] label; }

	// ---- the assembler ---------------------------------------------------------------------------------
	void put (u32 i) { if (p < lim) *p++ = i; else bad = true; }
	void movz (int rd, u32 imm, int hw, bool x) { put ((x ? 0xD2800000u : 0x52800000u) | (u32) hw << 21 | (imm & 0xFFFF) << 5 | rd); }
	void movk (int rd, u32 imm, int hw, bool x) { put ((x ? 0xF2800000u : 0x72800000u) | (u32) hw << 21 | (imm & 0xFFFF) << 5 | rd); }
	void movw (int rd, u32 v) { movz (rd, v, 0, false); if (v >> 16) movk (rd, v >> 16, 1, false); }
	void movx (int rd, u64 v)
	{
		movz (rd, (u32) v, 0, true);
		for (int hw = 1; hw < 4; hw++) if ((v >> (hw * 16)) & 0xFFFF) movk (rd, (u32) (v >> (hw * 16)), hw, true);
	}
	void movrx (int rd, int rm) { put (0xAA0003E0u | (u32) rm << 16 | rd); }
	// rd = rn + off (x registers; off >= 0)
	void addi (int rd, int rn, u32 off)
	{
		if (off < 4096) { put (0x91000000u | off << 10 | (u32) rn << 5 | rd); return; }
		if (off < (4096u << 12) && !(off & 0xFFF)) { put (0x91400000u | (off >> 12) << 10 | (u32) rn << 5 | rd); return; }
		movx (16, off); put (0x8B000000u | 16u << 16 | (u32) rn << 5 | rd);
	}
	// loads / stores with an unsigned offset (through x16 when it does not fit)
	void mem (u32 op, int scale, int rt, int rn, u32 off)
	{
		if (!(off & ((1u << scale) - 1)) && (off >> scale) < 4096) { put (op | (off >> scale) << 10 | (u32) rn << 5 | rt); return; }
		addi (16, rn, off); put (op | 16u << 5 | rt);
	}
	void ldrw (int rt, int rn, u32 off) { mem (0xB9400000u, 2, rt, rn, off); }
	void ldrsw (int rt, int rn, u32 off) { mem (0xB9800000u, 2, rt, rn, off); }
	void strw (int rt, int rn, u32 off) { mem (0xB9000000u, 2, rt, rn, off); }
	void ldrx (int rt, int rn, u32 off) { mem (0xF9400000u, 3, rt, rn, off); }
	void strx (int rt, int rn, u32 off) { mem (0xF9000000u, 3, rt, rn, off); }
	void ldrd (int rt, int rn, u32 off) { mem (0xFD400000u, 3, rt, rn, off); }
	void strd (int rt, int rn, u32 off) { mem (0xFD000000u, 3, rt, rn, off); }
	void fop (u32 op, int rd, int rn, int rm) { put (op | (u32) rm << 16 | (u32) rn << 5 | rd); }
	void fmov (int rd, int rn) { if (rd != rn) put (0x1E604000u | (u32) rn << 5 | rd); }
	void blr (int rn) { put (0xD63F0000u | (u32) rn << 5); }
	void bcond (int cond, u32 *to) { put (0x54000000u | ((u32) (to - p) & 0x7FFFF) << 5 | cond); }
	void b (u32 *to) { put (0x14000000u | ((u32) (to - p) & 0x3FFFFFF)); }
	static void patchB (u32 *at, u32 *to) { *at = 0x14000000u | ((u32) (to - at) & 0x3FFFFFF); }
	static void patchCond (u32 *at, u32 *to) { *at = (*at & 0xFF00001Fu) | ((u32) (to - at) & 0x7FFFF) << 5; }
	// x10 = the address of a variable's V (a local that is a reference: what it designates); false: not translated
	void slotAddr (bool global, int s)
	{
		if (global) { addi (10, XG, (u32) s * VSIZE); return; }
		addi (10, XLOC, (u32) s * VSIZE);
		ldrw (9, 10, 0);
		put (0x7100001Fu | (u32) VR << 10 | 9u << 5);		// cmp w9, #VR
		put (0x54000041u);					// b.ne +2
		ldrx (10, 10, V_P);
	}

	// ---- the registers of the stack's top -----------------------------------------------------------------
	int allocReg ()
	{
		if (!freeMask) spillBottom ();
		for (int i = 0; i < 8; i++) if (freeMask & (1u << i)) { freeMask &= ~(1u << i); return 8 + i; }
		return 8;
	}
	void freeReg (int r) { freeMask |= 1u << (r - 8); }
	void pushReg (int r) { regs[nreg++] = r; }
	void writeRegs (const int *r, int k, int pc)
	{
		if (!k) return;
		ldrw (9, XSP, 0);
		put (0x7100001Fu | (u32) (VM::STACK - 8) << 10 | 9u << 5);	// cmp w9, #STACK - 8
		Stub s; s.at = p; s.kind = 2; s.pc = pc; s.target = 0; s.nreg = 0; stubs.push (s);
		put (0x54000002u);						// b.hs (the stack is full)
		put (0x11000000u | (u32) k << 10 | 9u << 5 | 11);		// add w11, w9, #k
		strw (11, XSP, 0);
		put (0x8B090529u);						// add x9, x9, x9, lsl #1
		put (0x8B090ECAu);						// add x10, x22, x9, lsl #3
		for (int i = 0; i < k; i++)
		{
			strw (31, 10, (u32) i * VSIZE);
			strd (r[i], 10, (u32) i * VSIZE + V_N);
			strx (31, 10, (u32) i * VSIZE + V_P);
		}
	}
	void flush (int pc)
	{
		writeRegs (regs, nreg, pc);
		for (int i = 0; i < nreg; i++) freeReg (regs[i]);
		nreg = 0;
	}
	void spillBottom ()
	{
		writeRegs (regs, 1, curPc);
		freeReg (regs[0]);
		for (int i = 1; i < nreg; i++) regs[i - 1] = regs[i];
		nreg--;
	}
	// The stack's top number in a register (taken off the stack: the caller pushes or frees it).
	int popReg ()
	{
		if (nreg) return regs[--nreg];
		int r = allocReg ();
		ldrw (9, XSP, 0);
		put (0x51000529u);						// sub w9, w9, #1
		strw (9, XSP, 0);
		put (0x8B090529u); put (0x8B090ECAu);
		ldrd (r, 10, V_N);
		return r;
	}
	// A bail-out: `cond` -> the operands (the registers given, deepest first) go back on the stack and the VM
	// does the instruction.
	void bail (int cond, const int *ops, int nops)
	{
		Stub s; s.at = p; s.kind = 0; s.pc = curPc; s.target = 0; s.nreg = 0;
		for (int i = 0; i < nreg; i++) s.regs[s.nreg++] = regs[i];
		for (int i = 0; i < nops; i++) s.regs[s.nreg++] = ops[i];
		stubs.push (s);
		put (0x54000000u | cond);
	}
	void bailNZ (int xreg, const int *ops, int nops)		// cbnz x<reg>
	{
		Stub s; s.at = p; s.kind = 0; s.pc = curPc; s.target = 0; s.nreg = 0;
		for (int i = 0; i < nreg; i++) s.regs[s.nreg++] = regs[i];
		for (int i = 0; i < nops; i++) s.regs[s.nreg++] = ops[i];
		stubs.push (s);
		put (0xB5000000u | xreg);
	}
	// x<xr> = the integer the VM's popI gives for d<dr>: floor (d + 0.5)
	void toInt (int xr, int dr)
	{
		movz (16, 0x3FE0, 3, true);				// 0.5
		put (0x9E670200u | 0);					// fmov d0, x16
		fop (0x1E602800u, 1, dr, 0);				// fadd d1, dr, d0
		put (0x1E654021u);					// frintm d1, d1
		put (0x9E780020u | xr);					// fcvtzs xr, d1
	}

	// ---- what a pc is --------------------------------------------------------------------------------------
	int curPc;
	int kindOf (bool global, int s, int pc)
	{
		if (global) return s >= 0 && s < P->gkind.n ? P->gkind[s] : -1;
		int pr = procAt[pc];
		if (pr < 0 || s < 0 || s >= P->procs[pr].nlocals) return -1;
		return P->lkind[P->procs[pr].kindOff + s];
	}
	// Translated without the VM whatever the registers hold?
	bool inlineOp (int pc)
	{
		int op = code[pc];
		switch (op)
		{
		case OP_NUM: case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_IDIV: case OP_MOD: case OP_NEG:
		case OP_EQ: case OP_NE: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
		case OP_AND: case OP_OR: case OP_XOR: case OP_EQV: case OP_IMP: case OP_NOT:
		case OP_JMP: case OP_JZ: case OP_JNZ: case OP_NOP:
			return true;
		case OP_CONV: return code[pc + 1] == NT_INT || code[pc + 1] == NT_LNG;
		case OP_LDG: case OP_STG: return kindOf (true, code[pc + 1], pc) == K_NUM;
		case OP_LDL: case OP_STL: return kindOf (false, code[pc + 1], pc) == K_NUM;
		case OP_ALDG: case OP_ASTG: return kindOf (true, code[pc + 1], pc) == K_NUMARR && (code[pc + 2] == 1 || code[pc + 2] == 2);
		case OP_ALDL: case OP_ASTL: return kindOf (false, code[pc + 1], pc) == K_NUMARR && (code[pc + 2] == 1 || code[pc + 2] == 2);
		}
		return false;
	}
	bool scan ()
	{
		start = new unsigned char[n + 1]; entry = new unsigned char[n + 1]; procAt = new int[n + 1]; label = new u32 *[n + 1];
		for (int i = 0; i <= n; i++) { start[i] = entry[i] = 0; procAt[i] = -1; label[i] = 0; }
		for (int pc = 0; pc < n; )
		{
			int op = code[pc];
			if (op < OP_NUM || op >= OP_COUNT_ || pc + opLen (op) >= n + 1) return false;
			start[pc] = 1;
			pc += 1 + opLen (op);
		}
		for (int i = 0; i < P->procs.n; i++)
		{
			int e = P->procs[i].entry;
			if (e < 2 || e > n || code[e - 2] != OP_JMP || !start[e - 2]) continue;
			int end = code[e - 1];
			if (end < e || end > n) continue;
			for (int pc = e; pc < end; pc++) procAt[pc] = i;
			entry[e] = 1;
		}
		entry[0] = 1;
		for (int pc = 0; pc < n; )
		{
			int op = code[pc], next = pc + 1 + opLen (op), t = -1;
			if (op == OP_JMP || op == OP_JZ || op == OP_JNZ || op == OP_GOSUB || op == OP_ONERR) t = code[pc + 1];
			else if (op == OP_ONEVENT || op == OP_RESUME || op == OP_RUN) t = code[pc + 2];
			if (t >= 0 && t <= n)
			{
				if (!start[t] && t < n) { if (op == OP_JMP || op == OP_JZ || op == OP_JNZ) return false; }
				else entry[t] = 1;
			}
			else if (op == OP_JMP || op == OP_JZ || op == OP_JNZ) return false;
			if (!inlineOp (pc) && next <= n) entry[next] = 1;
			pc = next;
		}
		for (int i = 0; i < P->stmts.n; i++)
		{
			int a = P->stmts[i].start, b = P->stmts[i].end;
			if (a >= 0 && a < n && start[a]) entry[a] = 1;
			if (b >= 0 && b < n && start[b]) entry[b] = 1;
		}
		return true;
	}

	// ---- the translation -----------------------------------------------------------------------------------
	void prologue ()
	{
		put (0xA9B67BFDu);					// stp x29, x30, [sp, #-160]!
		put (0x910003FDu);					// mov x29, sp
		put (0xA90153F3u); put (0xA9025BF5u); put (0xA90363F7u); put (0xA9046BF9u); put (0xA90573FBu);	// x19 .. x28
		put (0x6D0627E8u); put (0x6D072FEAu); put (0x6D0837ECu); put (0x6D093FEEu);			// d8 .. d15
		movrx (XVM, 0);
		movx (XG, (u64) vm.G);
		movx (XSTK, (u64) &vm.stack[0]);
		movx (XNUM, (u64) P->nums.d);
		movx (XTAB, (u64) table);
		movw (WTICK, TICKS);
		movx (XSP, (u64) &vm.sp);
		movx (XNLOC, (u64) &vm.nloc);
		movx (XSTEP, (u64) &natStepC);
		movrx (0, 1);
		// dispatch: x0 = the pc to go on at (< 0: leave)
		dispatch = p;
		put (0xB7F80000u | 0);					// tbnz x0, #63, leave (patched)
		u32 *t1 = p - 1;
		ldrx (XLOC, XNLOC, 0);
		put (0xF8607B10u);					// ldr x16, [x24, x0, lsl #3]
		put (0xB4000010u);					// cbz x16, leave (patched)
		u32 *t2 = p - 1;
		put (0xD61F0200u);					// br x16
		leave = p;
		*t1 |= ((u32) (leave - t1) & 0x3FFF) << 5;
		*t2 |= ((u32) (leave - t2) & 0x7FFFF) << 5;
		put (0xA94153F3u); put (0xA9425BF5u); put (0xA94363F7u); put (0xA9446BF9u); put (0xA94573FBu);
		put (0x6D4627E8u); put (0x6D472FEAu); put (0x6D4837ECu); put (0x6D493FEEu);
		put (0xA8CA7BFDu);					// ldp x29, x30, [sp], #160
		put (0xD65F03C0u);					// ret
	}
	// The instruction at pc done by the VM, then on.
	void fallback (int pc, int next)
	{
		flush (pc);
		movrx (0, XVM); movw (1, (u32) pc); blr (XSTEP);
		int op = code[pc];
		bool straight = !(op == OP_CALL || op == OP_VCALL || op == OP_ICALL || op == OP_RET || op == OP_RETF || op == OP_GOSUB
				  || op == OP_RETSUB || op == OP_RESUME || op == OP_RUN || op == OP_END || op == OP_STOP || op == OP_CHAIN);
		if (straight)
		{
			movw (16, (u32) next);
			put (0xEB10001Fu);				// cmp x0, x16
			put (0x54000040u);				// b.eq +2
		}
		b (dispatch);
	}
	// A jump to the code of pc `t` (cond < 0: always). A jump back counts down to the next tick.
	void jumpTo (int t, int cond)
	{
		if (t > curPc)
		{
			if (cond >= 0) { put (0x54000040u | (u32) (cond ^ 1)); }	// b.!cond +2
			Fix f; f.at = p; f.pc = t; fixes.push (f);
			put (0x14000000u);
			return;
		}
		if (cond >= 0)
		{
			put (0x54000040u | (u32) (cond ^ 1));				// b.!cond +2
			Stub s; s.at = p; s.kind = 1; s.pc = curPc; s.target = t; s.nreg = 0; stubs.push (s);
			put (0x14000000u);
			return;
		}
		put (0x71000739u);					// subs w25, w25, #1
		put (0x54000040u | LE);					// b.le +2
		b (label[t]);
		tick (t);
	}
	void tick (int t)
	{
		movw (WTICK, TICKS);
		movrx (0, XVM); movw (1, (u32) t);
		movx (16, (u64) &natTickC); blr (16);
		b (dispatch);
	}
	void emitStubs ()
	{
		for (int i = 0; i < stubs.n; i++)
		{
			Stub &s = stubs[i];
			if (s.kind == 1) patchB (s.at, p); else patchCond (s.at, p);
			if (s.kind == 0)
			{
				// (the registers go back on the stack as they were; a full stack here: the VM says so)
				if (s.nreg)
				{
					ldrw (9, XSP, 0);
					put (0x7100001Fu | (u32) (VM::STACK - 8) << 10 | 9u << 5);	// cmp w9, #STACK - 8
					u32 *ok = p;
					put (0x54000003u);						// b.lo ok
					movrx (0, XVM); movw (1, (u32) s.pc);
					movx (16, (u64) &natStackC); blr (16);
					b (dispatch);
					patchCond (ok, p);
					writeRegsPlain (s.regs, s.nreg);
				}
				movrx (0, XVM); movw (1, (u32) s.pc); blr (XSTEP);
				b (dispatch);
			}
			else if (s.kind == 1)
			{
				put (0x71000739u);			// subs w25, w25, #1
				put (0x54000040u | LE);			// b.le +2
				b (label[s.target]);
				tick (s.target);
			}
			else
			{
				movrx (0, XVM); movw (1, (u32) s.pc);
				movx (16, (u64) &natStackC); blr (16);
				b (dispatch);
			}
		}
		stubs.n = 0;
	}
	void writeRegsPlain (const int *r, int k)
	{
		if (!k) return;
		ldrw (9, XSP, 0);
		put (0x11000000u | (u32) k << 10 | 9u << 5 | 11);
		strw (11, XSP, 0);
		put (0x8B090529u); put (0x8B090ECAu);
		for (int i = 0; i < k; i++)
		{
			strw (31, 10, (u32) i * VSIZE);
			strd (r[i], 10, (u32) i * VSIZE + V_N);
			strx (31, 10, (u32) i * VSIZE + V_P);
		}
	}
	static int condOf (int op) { return op == OP_EQ ? EQ : op == OP_NE ? NE : op == OP_LT ? MI : op == OP_GT ? GT : op == OP_LE ? LS : GE; }

	// The element of a numeric array: the indices (nd of them, in registers idx[], first dimension first) ->
	// x14 = the address of its V. The operands for a bail-out: ops / nops.
	void element (bool global, int s, int nd, const int *idx, const int *ops, int nops)
	{
		for (int i = 0; i < nd; i++) toInt (11 + i, idx[i]);
		slotAddr (global, s);
		ldrw (9, 10, 0);
		put (0x7100001Fu | (u32) VA << 10 | 9u << 5);		// cmp w9, #VA
		bail (NE, ops, nops);
		ldrx (13, 10, V_P);
		ldrw (9, 13, (u32) __builtin_offsetof (Arr, nd));
		put (0x7100001Fu | (u32) nd << 10 | 9u << 5);
		bail (NE, ops, nops);
		for (int i = 0; i < nd; i++)
		{
			ldrsw (14, 13, (u32) (__builtin_offsetof (Arr, lo) + 4 * i));
			put (0xCB0E0000u | (u32) (11 + i) << 5 | (u32) (11 + i));	// sub x, x, x14
			ldrw (15, 13, (u32) (__builtin_offsetof (Arr, cnt) + 4 * i));
			put (0xEB0F001Fu | (u32) (11 + i) << 5);			// cmp x, x15
			bail (HS, ops, nops);
			if (i > 0) put (0x9B0F0000u | 12u << 10 | 11u << 5 | 11);	// madd x11, x11, x15, x12
		}
		ldrx (14, 13, (u32) __builtin_offsetof (Arr, e));
		put (0x8B0B056Bu);					// add x11, x11, x11, lsl #1
		put (0x8B0B0DCEu);					// add x14, x14, x11, lsl #3
	}

	void translate ()
	{
		prologue ();
		for (int pc = 0; pc < n && !bad; )
		{
			int op = code[pc], next = pc + 1 + opLen (op);
			curPc = pc;
			if (stubs.n && p - stubs[0].at > 60000)		// (the out-of-line paths stay within a branch's reach)
			{
				u32 *over = p; put (0x14000000u);
				emitStubs ();
				patchB (over, p);
			}
			if (entry[pc]) { flush (pc); table[pc] = p; }
			label[pc] = p;
			if (!inlineOp (pc))
			{
				if (op == OP_POP && nreg) { freeReg (regs[--nreg]); pc = next; continue; }
				fallback (pc, next);
				bool ends = op == OP_RET || op == OP_RETF || op == OP_RETSUB || op == OP_END || op == OP_STOP || op == OP_RESUME || op == OP_GOSUB
					    || op == OP_CALL || op == OP_VCALL || op == OP_ICALL || op == OP_RUN || op == OP_CHAIN;
				if (ends && stubs.n) emitStubs ();
				pc = next;
				continue;
			}
			switch (op)
			{
			case OP_NOP: break;
			case OP_NUM: { int r = allocReg (); ldrd (r, XNUM, (u32) code[pc + 1] * 8); pushReg (r); break; }
			case OP_LDG: case OP_LDL:
			{
				int r = allocReg ();
				slotAddr (op == OP_LDG, code[pc + 1]);
				ldrd (r, 10, V_N);
				pushReg (r);
				break;
			}
			case OP_STG: case OP_STL:
			{
				int r = popReg ();
				slotAddr (op == OP_STG, code[pc + 1]);
				strd (r, 10, V_N);
				freeReg (r);
				break;
			}
			case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
			{
				int rb = popReg (), ra = popReg ();
				if (op == OP_DIV)
				{
					int ops[2] = { ra, rb };
					put (0x1E602008u | (u32) rb << 5);		// fcmp db, #0.0
					bail (EQ, ops, 2);
				}
				fop (op == OP_ADD ? 0x1E602800u : op == OP_SUB ? 0x1E603800u : op == OP_MUL ? 0x1E600800u : 0x1E601800u, ra, ra, rb);
				freeReg (rb); pushReg (ra);
				break;
			}
			case OP_NEG: { int r = popReg (); put (0x1E614000u | (u32) r << 5 | r); pushReg (r); break; }
			case OP_IDIV: case OP_MOD: case OP_AND: case OP_OR: case OP_XOR: case OP_EQV: case OP_IMP:
			{
				int rb = popReg (), ra = popReg ();
				int ops[2] = { ra, rb };
				toInt (9, rb); toInt (10, ra);				// x9 = b, x10 = a
				if (op == OP_IDIV || op == OP_MOD)
				{
					Stub s; s.at = p; s.kind = 0; s.pc = pc; s.target = 0; s.nreg = 0;
					for (int i = 0; i < nreg; i++) s.regs[s.nreg++] = regs[i];
					s.regs[s.nreg++] = ops[0]; s.regs[s.nreg++] = ops[1];
					stubs.push (s);
					put (0xB4000009u);				// cbz x9, bail
					put (0x9AC90D4Bu);				// sdiv x11, x10, x9
					if (op == OP_MOD) put (0x9B09A96Bu);		// msub x11, x11, x9, x10
				}
				else if (op == OP_AND) put (0x8A09014Bu);		// and x11, x10, x9
				else if (op == OP_OR) put (0xAA09014Bu);		// orr x11, x10, x9
				else if (op == OP_XOR) put (0xCA09014Bu);		// eor x11, x10, x9
				else if (op == OP_EQV) put (0xCA29014Bu);		// eon x11, x10, x9
				else put (0xAA2A012Bu);					// orn x11, x9, x10  (b | ~a)
				put (0x9E620160u | ra);					// scvtf da, x11
				freeReg (rb); pushReg (ra);
				break;
			}
			case OP_NOT:
			{
				int r = popReg ();
				toInt (9, r);
				put (0xAA2903EBu);					// mvn x11, x9
				put (0x9E620160u | r);
				pushReg (r);
				break;
			}
			case OP_EQ: case OP_NE: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
			{
				int rb = popReg (), ra = popReg ();
				int nop = next < n ? code[next] : 0;
				if ((nop == OP_JZ || nop == OP_JNZ) && !entry[next])				// the comparison and its jump
				{
					flush (pc);
					fop (0x1E602000u, 0, ra, rb);					// fcmp da, db
					freeReg (ra); freeReg (rb);
					label[next] = p;
					int c = condOf (op);
					curPc = next;
					jumpTo (code[next + 1], nop == OP_JNZ ? c : (c ^ 1));
					pc = next + 2;
					continue;
				}
				fop (0x1E602000u, 0, ra, rb);
				put (0x5A9F03E9u | (u32) (condOf (op) ^ 1) << 12);			// csetm w9, cond
				put (0x1E620120u | ra);							// scvtf da, w9
				freeReg (rb); pushReg (ra);
				break;
			}
			case OP_JMP:
				flush (pc);
				jumpTo (code[pc + 1], -1);
				if (stubs.n) emitStubs ();
				break;
			case OP_JZ: case OP_JNZ:
			{
				int r = popReg ();
				flush (pc);
				put (0x1E602008u | (u32) r << 5);			// fcmp dr, #0.0
				freeReg (r);
				jumpTo (code[pc + 1], op == OP_JZ ? EQ : NE);
				break;
			}
			case OP_CONV:
			{
				int r = popReg ();
				int ops[1] = { r };
				put (0x1E644000u | (u32) r << 5 | 0);			// frintn d0, dr
				put (0x9E780009u);					// fcvtzs x9, d0
				if (code[pc + 1] == NT_INT)
				{
					put (0x91400000u | 8u << 10 | 9u << 5 | 10);	// add x10, x9, #32768 (#8, lsl #12)
					put (0xD350FD4Au);				// lsr x10, x10, #16
				}
				else
				{
					movz (11, 0x8000, 1, true);			// 2^31
					put (0x8B0B012Au);				// add x10, x9, x11
					put (0xD360FD4Au);				// lsr x10, x10, #32
				}
				bailNZ (10, ops, 1);
				fmov (r, 0);
				pushReg (r);
				break;
			}
			case OP_ALDG: case OP_ALDL:
			{
				int nd = code[pc + 2], idx[2];
				for (int i = nd - 1; i >= 0; i--) idx[i] = popReg ();
				element (op == OP_ALDG, code[pc + 1], nd, idx, idx, nd);
				ldrd (idx[0], 14, V_N);
				if (nd == 2) freeReg (idx[1]);
				pushReg (idx[0]);
				break;
			}
			case OP_ASTG: case OP_ASTL:
			{
				int nd = code[pc + 2], ops[3];
				int rv = popReg ();
				for (int i = nd - 1; i >= 0; i--) ops[i] = popReg ();
				ops[nd] = rv;
				element (op == OP_ASTG, code[pc + 1], nd, ops, ops, nd + 1);
				strd (rv, 14, V_N);
				for (int i = 0; i <= nd; i++) freeReg (ops[i]);
				break;
			}
			}
			pc = next;
		}
		flush (n > 0 ? n - 1 : 0);
		label[n] = p;
		movx (0, (u64) -1); b (dispatch);			// (past the end: never reached -- the program ends with END)
		emitStubs ();
		for (int i = 0; i < fixes.n; i++)
		{
			if (!label[fixes[i].pc]) { bad = true; break; }
			patchB (fixes[i].at, label[fixes[i].pc]);
		}
	}
};

static void nativeFlush (void *from, void *to)
{
	typedef unsigned long long u64;
	u64 ctr; asm volatile ("mrs %0, ctr_el0" : "=r" (ctr));
	u64 dl = 4u << ((ctr >> 16) & 15), il = 4u << (ctr & 15);
	u64 b = (u64) from, e = (u64) to;
	for (u64 x = b & ~(dl - 1); x < e; x += dl) asm volatile ("dc cvau, %0" :: "r" (x) : "memory");
	asm volatile ("dsb ish" ::: "memory");
	for (u64 x = b & ~(il - 1); x < e; x += il) asm volatile ("ic ivau, %0" :: "r" (x) : "memory");
	asm volatile ("dsb ish\n\tisb" ::: "memory");
}

static Native *nativeTranslate (VM &vm)
{
	Program *P = vm.P;
	if (P->code.n <= 0) return 0;
	Jit j (vm);
	if (!j.scan ()) return 0;
	unsigned words = (unsigned) P->code.n * 40u + 16384u;
	unsigned *buf = (unsigned *) vm.H.codeAlloc (words * 4u);
	if (!buf) return 0;
	Native *nt = new Native;
	nt->base = buf; nt->words = words; nt->npc = P->code.n;
	nt->table = new void *[P->code.n + 1];
	for (int i = 0; i <= P->code.n; i++) nt->table[i] = 0;
	j.base = j.p = buf; j.lim = buf + words - 64; j.table = nt->table;
	j.translate ();
	if (j.bad) { delete [] nt->table; delete nt; return 0; }	// (too big for the buffer: the VM runs it)
	nativeFlush (buf, j.p);
	nt->enter = (long (*) (VM *, long)) (void *) buf;
	return nt;
}
static bool nativeEntry (Native *n, int pc) { return pc >= 0 && pc < n->npc && n->table[pc] != 0; }
static void nativeRun (Native *n, VM &vm) { n->enter (&vm, vm.pc); }
static void nativeFree (Native *n) { if (n) { delete [] n->table; delete n; } }

#else

struct Native { int unused; };
static Native *nativeTranslate (VM &) { return 0; }
static bool nativeEntry (Native *, int) { return false; }
static void nativeRun (Native *, VM &) {}
static void nativeFree (Native *) {}

#endif
