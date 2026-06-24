//
// trapframe.h
//
// Full exception/trap frame saved by our VBAR_EL1 vectors (kernel/arch/aarch64/
// vectors.S), shared between the assembly stubs and the C handlers. Offsets are
// usable from both assembly and C.
//
// Layout (800 bytes, 16-byte aligned), low address first:
//   x0..x30   (31 * 8 = 248 bytes)
//   sp_el0    (user stack pointer; banked SP_EL0 when from EL1h)
//   elr_el1   (return PC)
//   spsr_el1  (saved PSTATE)
//   v0..v31   (32 * 16 = 512 bytes; the full 128-bit SIMD/FP registers)
//   fpsr      (FP status register, in a 64-bit slot)
//   fpcr      (FP control register, in a 64-bit slot)
//
// The FP/SIMD block lets floating-point user code survive ANY context switch,
// including a future preemptive switch taken from the IRQ path: that path saves
// this whole frame on the way in and restores it on the way out, whereas the
// cooperative TaskSwitch only preserves the callee-saved d8-d15 mandated by the
// AArch64 PCS. FP/SIMD is enabled at EL0/EL1 at boot (CPACR_EL1, see
// circle/lib/startup64.S), so the stp/ldp q-register forms in vectors.S never
// trap. See kernel/arch/aarch64/vectors.S (SAVE_TRAP / RESTORE_TRAP_ERET).
//
#ifndef _kern_trapframe_h
#define _kern_trapframe_h

// ---- Offsets (assembler + C) -----------------------------------------------
#define TF_X(n)		((n) * 8)	// x0 at 0 ... x30 at 240
#define TF_SP_EL0	248		// 0xF8
#define TF_ELR		256		// 0x100
#define TF_SPSR		264		// 0x108
#define TF_Q(n)		(272 + (n) * 16) // q0 at 272 ... q31 at 768 (128-bit each)
#define TF_FPSR		784		// 0x310
#define TF_FPCR		792		// 0x318
#define TF_SIZE		800		// 0x320 (16-byte aligned)

// SPSR_EL1 mode field (M[3:0]) values used when entering EL0.
#define SPSR_MODE_EL0t	0x0		// EL0, all of DAIF clear (interrupts enabled)

#ifndef __ASSEMBLER__

#include <circle/macros.h>
#include <circle/types.h>

struct TTrapFrame
{
	u64	x[31];		// x0 .. x30
	u64	sp_el0;
	u64	elr_el1;
	u64	spsr_el1;
	u64	v[64];		// q0 .. q31 (128-bit each: 2 u64 words, low word first)
	u64	fpsr;
	u64	fpcr;
}
PACKED;

#ifdef __cplusplus
extern "C" {
#endif

// C handlers called from vectors.S
void SyncHandlerEL0 (TTrapFrame *pFrame);	// SVC -> syscall, abort -> page fault
void SyncHandlerEL1 (TTrapFrame *pFrame);	// SVC (test) -> syscall, else kernel panic
void KernelIRQExit (void);			// reschedule on IRQ exit if a slice expired
void BadModeHandler (TTrapFrame *pFrame);	// unexpected vector / SError -> dump + halt
void SyscallEntry (TTrapFrame *pFrame);		// syscall ABI dispatch (kernel/sys)

// Timer periodic handler (100 Hz) -> CScheduler::OnTimerTick(). Registered with
// CTimer::RegisterPeriodicHandler() in CKernel; runs inside the timer IRQ.
void PeriodicTick (void);

// Assembly helpers (vectors.S)
void install_vectors (void);			// set VBAR_EL1 to our table + isb
void enter_user (u64 ulEntry, u64 ulUserSP, void *pKernelParam);  // ERET to EL0 (#6)

#ifdef __cplusplus
}
#endif

#endif // __ASSEMBLER__

#endif // _kern_trapframe_h
