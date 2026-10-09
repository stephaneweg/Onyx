//
// n3ds/n3ds_cpu.cpp -- the ARM11 behind n3ds.h's Cpu: Dynarmic's A32 JIT (third_party/dynarmic-*, built by
// user/Libs/dynarmic). The only file of the core that sees Dynarmic: another JIT (the DS's, extended) could take
// its place. The JIT reads and writes memory through the machine's page table (inline code), and through our
// callbacks where a page is missing; SVC comes to Machine::svc; CP15 gives the thread's TLS address.
// Compiled with Dynarmic's flags (C++20; user/Libs/dynarmic/Makefile's DYNARMIC_INC).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <array>
#include <cstring>
#include <memory>
#include <optional>

#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/A32/coprocessor.h"
#include "dynarmic/interface/exclusive_monitor.h"

#include "n3ds/n3ds.h"

namespace n3ds {

namespace {

using Dynarmic::A32::CoprocReg;

// CP15: the thread registers (c13), and the cache / barrier operations programs do (c7), which do nothing here.
struct Cp15 final : Dynarmic::A32::Coprocessor
{
	u32 tls = 0;				// TPIDRURO: the thread's local storage
	u32 urw = 0;				// TPIDRURW: the thread's own word

	std::optional<Callback> CompileInternalOperation (bool, unsigned, CoprocReg, CoprocReg, CoprocReg, unsigned) override { return std::nullopt; }
	CallbackOrAccessOneWord CompileSendOneWord (bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm, unsigned opc2) override
	{
		if (two || opc1 != 0) return std::monostate {};
		if (CRn == CoprocReg::C13 && CRm == CoprocReg::C0 && opc2 == 2) return &urw;
		if (CRn == CoprocReg::C7)			// flush prefetch, data sync / memory barriers, cache maintenance
			return Callback { [] (void *, u32, u32) -> std::uint64_t { return 0; }, std::nullopt };
		return std::monostate {};
	}
	CallbackOrAccessTwoWords CompileSendTwoWords (bool, unsigned, CoprocReg) override { return std::monostate {}; }
	CallbackOrAccessOneWord CompileGetOneWord (bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm, unsigned opc2) override
	{
		if (two || opc1 != 0) return std::monostate {};
		if (CRn == CoprocReg::C13 && CRm == CoprocReg::C0 && opc2 == 3) return &tls;
		if (CRn == CoprocReg::C13 && CRm == CoprocReg::C0 && opc2 == 2) return &urw;
		return std::monostate {};
	}
	CallbackOrAccessTwoWords CompileGetTwoWords (bool, unsigned, CoprocReg) override { return std::monostate {}; }
	std::optional<Callback> CompileLoadWords (bool, bool, CoprocReg, std::optional<std::uint8_t>) override { return std::nullopt; }
	std::optional<Callback> CompileStoreWords (bool, bool, CoprocReg, std::optional<std::uint8_t>) override { return std::nullopt; }
};

struct DynCpu final : Cpu, Dynarmic::A32::UserCallbacks
{
	Machine *m;
	std::shared_ptr<Cp15> cp15;
	Dynarmic::ExclusiveMonitor monitor { 1 };
	std::unique_ptr<Dynarmic::A32::Jit> jit;

	explicit DynCpu (Machine *machine) : m (machine), cp15 (std::make_shared<Cp15> ())
	{
		Dynarmic::A32::UserConfig cfg;
		cfg.callbacks = this;
		cfg.arch_version = Dynarmic::A32::ArchVersion::v6K;
		cfg.coprocessors[15] = cp15;
		cfg.global_monitor = &monitor;
		// (the machine's table has the layout of Dynarmic's std::array of page pointers)
		cfg.page_table = reinterpret_cast<std::array<std::uint8_t *, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES> *> (m->mem.pages);
		cfg.code_cache_size = 64 * 1024 * 1024;
		jit = std::make_unique<Dynarmic::A32::Jit> (cfg);
	}

	// ---- Cpu
	void run () override { jit->ClearHalt (); jit->Run (); }
	void halt () override { jit->HaltExecution (); }
	u32 *regs () override { return jit->Regs ().data (); }
	void save (CpuState &s) override
	{
		std::memcpy (s.r, jit->Regs ().data (), sizeof s.r);
		std::memcpy (s.vfp, jit->ExtRegs ().data (), sizeof s.vfp);
		s.cpsr = jit->Cpsr (); s.fpscr = jit->Fpscr (); s.tls = cp15->tls;
	}
	void load (const CpuState &s) override
	{
		std::memcpy (jit->Regs ().data (), s.r, sizeof s.r);
		std::memcpy (jit->ExtRegs ().data (), s.vfp, sizeof s.vfp);
		jit->SetCpsr (s.cpsr); jit->SetFpscr (s.fpscr); cp15->tls = s.tls;
		jit->ClearExclusiveState ();
	}
	void invalidate (u32 va, u32 size) override { jit->InvalidateCacheRange (va, size); }

	// ---- Dynarmic's callbacks
	std::optional<u32> MemoryReadCode (u32 va) override
	{
		if (!m->mem.mapped (va, 4)) return std::nullopt;		// (no code there: Dynarmic raises NoExecuteFault)
		u32 v; m->mem.read (va, &v, 4);
		return v;
	}
	u8 MemoryRead8 (u32 va) override { return m->mem.r8 (va); }
	u16 MemoryRead16 (u32 va) override { return m->mem.r16 (va); }
	u32 MemoryRead32 (u32 va) override { return m->mem.r32 (va); }
	u64 MemoryRead64 (u32 va) override { return m->mem.r64 (va); }
	void MemoryWrite8 (u32 va, u8 v) override { m->mem.w8 (va, v); }
	void MemoryWrite16 (u32 va, u16 v) override { m->mem.w16 (va, v); }
	void MemoryWrite32 (u32 va, u32 v) override { m->mem.w32 (va, v); }
	void MemoryWrite64 (u32 va, u64 v) override { m->mem.w64 (va, v); }
	// (one emulated core: an exclusive store succeeds when the value is still the one read)
	bool MemoryWriteExclusive8 (u32 va, u8 v, u8 expected) override { if (m->mem.r8 (va) != expected) return false; m->mem.w8 (va, v); return true; }
	bool MemoryWriteExclusive16 (u32 va, u16 v, u16 expected) override { if (m->mem.r16 (va) != expected) return false; m->mem.w16 (va, v); return true; }
	bool MemoryWriteExclusive32 (u32 va, u32 v, u32 expected) override { if (m->mem.r32 (va) != expected) return false; m->mem.w32 (va, v); return true; }
	bool MemoryWriteExclusive64 (u32 va, u64 v, u64 expected) override { if (m->mem.r64 (va) != expected) return false; m->mem.w64 (va, v); return true; }

	void InterpreterFallback (u32 pc, size_t) override
	{
		m->fail ("the JIT cannot run the instruction at %08x (%08x)", (unsigned) pc, (unsigned) m->mem.r32 (pc));
	}
	void CallSVC (u32 swi) override { m->svc (swi & 0xFF); }
	void ExceptionRaised (u32 pc, Dynarmic::A32::Exception e) override
	{
		using E = Dynarmic::A32::Exception;
		if (e == E::Yield || e == E::WaitForInterrupt || e == E::WaitForEvent || e == E::SendEvent || e == E::SendEventLocal
		    || e == E::PreloadData || e == E::PreloadDataWithIntentToWrite || e == E::PreloadInstruction) return;
		const char *what = e == E::UndefinedInstruction ? "undefined instruction" : e == E::UnpredictableInstruction ? "unpredictable instruction"
				 : e == E::Breakpoint ? "breakpoint" : e == E::NoExecuteFault ? "no code" : "exception";
		m->fail ("%s at %08x (%08x), lr %08x", what, (unsigned) pc, (unsigned) m->mem.r32 (pc), (unsigned) jit->Regs ()[14]);
	}
	void AddTicks (u64 n) override { m->now += n; m->ticksLeft -= (s64) n; }
	u64 GetTicksRemaining () override { return m->ticksLeft > 0 ? (u64) m->ticksLeft : 0; }
};

}

Cpu *Cpu::create (Machine *m) { return new DynCpu (m); }

}
