//
// dynarmic_t0.cpp -- the 3DS emulator's phase T0 (docs/3DS-EMULATOR-STUDY.md): Dynarmic's ARM11 JIT built with
// Onyx's toolchain and run in AArch64. Small ARM / Thumb / VFP programs (their machine code written here: no ARM
// assembler is needed) run through the JIT, once with the memory callbacks alone and once with a page table,
// and their registers and memory are compared with what the instructions must give.
//   sh tools/tests/run_n3ds_t0.sh
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"

using u8 = std::uint8_t; using u16 = std::uint16_t; using u32 = std::uint32_t; using u64 = std::uint64_t;

enum { MEM_SIZE = 0x10000, PAGE = 0x1000 };

struct Env final : Dynarmic::A32::UserCallbacks
{
	u8 mem[MEM_SIZE];
	Dynarmic::A32::Jit *jit = nullptr;
	u64 ticks = 0;
	unsigned svcs = 0, faults = 0, callbackReads = 0, callbackWrites = 0;

	template <class T> T rd (u32 a) { callbackReads++; T v = 0; if (a + sizeof (T) <= MEM_SIZE) std::memcpy (&v, mem + a, sizeof (T)); else faults++; return v; }
	template <class T> void wr (u32 a, T v) { callbackWrites++; if (a + sizeof (T) <= MEM_SIZE) std::memcpy (mem + a, &v, sizeof (T)); else faults++; }
	// (the code is read at translation: not counted as a data access)
	std::optional<u32> MemoryReadCode (u32 a) override { u32 v = 0; if (a + 4 <= MEM_SIZE) { std::memcpy (&v, mem + a, 4); return v; } return std::nullopt; }
	u8 MemoryRead8 (u32 a) override { return rd<u8> (a); }
	u16 MemoryRead16 (u32 a) override { return rd<u16> (a); }
	u32 MemoryRead32 (u32 a) override { return rd<u32> (a); }
	u64 MemoryRead64 (u32 a) override { return rd<u64> (a); }
	void MemoryWrite8 (u32 a, u8 v) override { wr (a, v); }
	void MemoryWrite16 (u32 a, u16 v) override { wr (a, v); }
	void MemoryWrite32 (u32 a, u32 v) override { wr (a, v); }
	void MemoryWrite64 (u32 a, u64 v) override { wr (a, v); }
	void InterpreterFallback (u32 pc, size_t n) override { std::printf ("  interpreter fallback at %08x (%u instructions)\n", (unsigned) pc, (unsigned) n); faults++; jit->HaltExecution (); }
	void CallSVC (u32) override { svcs++; jit->HaltExecution (); }
	void ExceptionRaised (u32 pc, Dynarmic::A32::Exception e) override { std::printf ("  exception %d at %08x\n", (int) e, (unsigned) pc); faults++; jit->HaltExecution (); }
	void AddTicks (u64 n) override { ticks += n; }
	u64 GetTicksRemaining () override { return 1000000; }
};

static int g_fail;
static void check (const char *what, u32 got, u32 want)
{
	if (got == want) return;
	std::printf ("  FAIL %s: %08x, expected %08x\n", what, (unsigned) got, (unsigned) want);
	g_fail++;
}

// ARM: the sum 1..100 in a loop, a store and a load, ARMv6's UXTB / REV / SSAT-free media set's simplest, MUL,
// UMULL, a conditional, shifts with flags, LDM / STM through the stack, then VFP (int -> float, add, mul, back).
static const u32 ARM_CODE[] = {
	0xE3A00000,	// 00 mov   r0, #0
	0xE3A01064,	// 04 mov   r1, #100
	0xE0800001,	// 08 add   r0, r0, r1		<- loop
	0xE2511001,	// 0c subs  r1, r1, #1
	0x1AFFFFFC,	// 10 bne   loop			r0 = 5050
	0xE3A02A01,	// 14 mov   r2, #0x1000
	0xE5820000,	// 18 str   r0, [r2]
	0xE5923000,	// 1c ldr   r3, [r2]			r3 = 5050
	0xE6EF4070,	// 20 uxtb  r4, r0			r4 = 0xBA
	0xE6BF5F30,	// 24 rev   r5, r0			r5 = 0xBA130000
	0xE0060090,	// 28 mul   r6, r0, r0			r6 = 25502500
	0xE0898590,	// 2c umull r8, r9, r0, r5		r9:r8 = 5050 * 0xBA130000
	0xE3500A01,	// 30 cmp   r0, #0x1000
	0x83A0A001,	// 34 movhi r10, #1			r10 = 1 (5050 > 4096)
	0x93A0A002,	// 38 movls r10, #2
	0xE1B0B0A0,	// 3c movs  r11, r0, lsr #1		r11 = 2525, C = 0
	0xE2ABB000,	// 40 adc   r11, r11, #0		r11 = 2525
	0xE3A0DA02,	// 44 mov   sp, #0x2000
	0xE92D0039,	// 48 push  {r0, r3, r4, r5}
	0xE8BD1000,	// 4c pop   {r12}			r12 = 5050 (the lowest register first)
	0xE28DD00C,	// 50 add   sp, sp, #12
	0xEE000A10,	// 54 vmov  s0, r0
	0xEEB80AC0,	// 58 vcvt.f32.s32 s0, s0		s0 = 5050.0
	0xEE300A00,	// 5c vadd.f32 s0, s0, s0		s0 = 10100.0
	0xEE200A00,	// 60 vmul.f32 s0, s0, s0		s0 = 102010000.0 (exact in a float)
	0xEEBD0AC0,	// 64 vcvt.s32.f32 s0, s0
	0xEE107A10,	// 68 vmov  r7, s0			r7 = 102010000
	0xEF000000,	// 6c svc   0
};

// Thumb: immediates, a shift, a loop with a conditional branch, a store and a load.
static const u16 THUMB_CODE[] = {
	0x2007,		// 00 movs r0, #7
	0x0100,		// 02 lsls r0, r0, #4			r0 = 0x70
	0x3001,		// 04 adds r0, #1			r0 = 0x71
	0x210A,		// 06 movs r1, #10
	0x2200,		// 08 movs r2, #0
	0x1852,		// 0a adds r2, r2, r1		<- loop
	0x3901,		// 0c subs r1, #1
	0xD1FC,		// 0e bne  loop				r2 = 55
	0x2301,		// 10 movs r3, #1
	0x031B,		// 12 lsls r3, r3, #12			r3 = 0x1000
	0x605A,		// 14 str  r2, [r3, #4]
	0x685C,		// 16 ldr  r4, [r3, #4]			r4 = 55
	0xDF00,		// 18 svc  0
};

static void run (bool pageTable)
{
	std::printf ("%s\n", pageTable ? "with a page table" : "with the memory callbacks");
	auto env = std::make_unique<Env> ();
	std::memset (env->mem, 0, MEM_SIZE);
	std::memcpy (env->mem + 0x100, ARM_CODE, sizeof ARM_CODE);
	std::memcpy (env->mem + 0x400, THUMB_CODE, sizeof THUMB_CODE);

	auto pages = std::make_unique<std::array<u8 *, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>> ();
	pages->fill (nullptr);
	if (pageTable) for (u32 p = 0; p < MEM_SIZE / PAGE; p++) (*pages)[p] = env->mem + p * PAGE;

	Dynarmic::A32::UserConfig cfg;
	cfg.callbacks = env.get ();
	cfg.arch_version = Dynarmic::A32::ArchVersion::v6K;		// the 3DS's ARM11 MPCore
	if (pageTable) cfg.page_table = pages.get ();
	Dynarmic::A32::Jit jit (cfg);
	env->jit = &jit;

	// ARM
	jit.Regs ().fill (0);
	jit.Regs ()[15] = 0x100;
	jit.SetCpsr (0x00000010);					// user mode, ARM
	jit.Run ();
	const auto &r = jit.Regs ();
	check ("arm: svc reached", env->svcs, 1);
	check ("arm: r0 (the loop's sum)", r[0], 5050);
	check ("arm: r3 (str / ldr)", r[3], 5050);
	check ("arm: memory 0x1000", env->mem[0x1000] | env->mem[0x1001] << 8, 5050);
	check ("arm: r4 (uxtb)", r[4], 0xBA);
	check ("arm: r5 (rev)", r[5], 0xBA130000);
	check ("arm: r6 (mul)", r[6], 25502500);
	const u64 wide = (u64) 5050 * 0xBA130000u;
	check ("arm: r8 (umull, low)", r[8], (u32) wide);
	check ("arm: r9 (umull, high)", r[9], (u32) (wide >> 32));
	check ("arm: r10 (movhi)", r[10], 1);
	check ("arm: r11 (movs lsr, adc)", r[11], 2525);
	check ("arm: r12 (push / pop)", r[12], 5050);
	check ("arm: sp", r[13], 0x2000);
	check ("arm: r7 (vfp)", r[7], 102010000);
	check ("arm: pc", r[15], 0x100 + sizeof ARM_CODE);

	// Thumb
	jit.Regs ().fill (0);
	jit.Regs ()[15] = 0x400;
	jit.SetCpsr (0x00000030);					// user mode, Thumb
	jit.Run ();
	check ("thumb: svc reached", env->svcs, 2);
	check ("thumb: r0", r[0], 0x71);
	check ("thumb: r2 (the loop's sum)", r[2], 55);
	check ("thumb: r4 (str / ldr)", r[4], 55);
	check ("thumb: pc", r[15], 0x400 + sizeof THUMB_CODE);
	check ("thumb: T flag kept", jit.Cpsr () & 0x20, 0x20);

	// the code changed under the JIT: its blocks invalidated, the new code runs
	const u32 patched = 0xE3A01032;					// mov r1, #50: the sum 1..50
	std::memcpy (env->mem + 0x104, &patched, 4);
	jit.InvalidateCacheRange (0x104, 4);
	jit.Regs ().fill (0);
	jit.Regs ()[15] = 0x100;
	jit.SetCpsr (0x00000010);
	jit.Run ();
	check ("invalidation: r0", r[0], 1275);

	check ("no fault", env->faults, 0);
	if (pageTable) check ("page table: no data access through the callbacks", env->callbackReads + env->callbackWrites, 0);
	else if (env->callbackReads + env->callbackWrites == 0) { std::printf ("  FAIL the callbacks were never called\n"); g_fail++; }
	std::printf ("  %llu ticks, %u reads and %u writes through the callbacks\n", (unsigned long long) env->ticks, env->callbackReads, env->callbackWrites);
}

int main ()
{
	run (false);
	run (true);
	std::printf (g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
	return g_fail ? 1 : 0;
}
