# Multi-process kernel on Circle (Raspberry Pi 4)

A preemptive, multi-process kernel with MMU isolation, built **on top of Circle**
([rsta2/circle](https://github.com/rsta2/circle)) as the hardware-abstraction +
driver layer. Apps are loaded from the SD card and run isolated in EL0.

> Working name only. The `circle/` subdirectory is the upstream Circle clone and is
> used unmodified where possible. Our code lives in `kernel/`.

---

## 1. Strategy: own the top, reuse the bottom

Circle is a *bare-metal, single-address-space, cooperative* framework ("your app
IS the system"). We keep its excellent low-level half and replace its OS-personality
half.

| Layer | Decision | Notes |
|---|---|---|
| Boot reset → EL1, MMU enable, GIC, C++ ctors | **Keep Circle** (`startup64.S`, `sysinit.cpp`, `CMemorySystem`, `CInterruptSystem`) | Our entry is `main()`; everything below it is already done. |
| `CMemorySystem` / page allocator (`palloc`/`pfree`, 64 KB pages) | **Keep** — used as our frame allocator | `CMemorySystem::Get()` singleton. |
| `CInterruptSystem` (GIC-400), `CTimer`, mailbox, property tags | **Keep** | We drive our own scheduler tick on top. |
| EMMC + FatFs, USB, BCM54213 Ethernet, framebuffer | **Pull in by need** | The payoff of reusing Circle. |
| `CScheduler` / `CTask` / `CSynchronizationEvent` (cooperative) | **Rewrite implementation, keep API** | The heart of "rewrite the multitasking". See §5. |
| Context switch, exception vectors, EL0 transition, syscalls, per-process page tables | **Write from scratch** | Does not exist in Circle. |

**Drivers run in EL1 (in-kernel)**, called from EL0 apps via syscalls. Userspace
driver isolation (DFv2/Zircon-style) is explicitly out of scope for now.

### Reuse mechanics
- Link against Circle's built static libs (`libcircle.a`, `lib/fs`, `lib/usb`, …)
  rather than copying sources. Missing symbols at link time reveal the dependency
  subgraph a driver pulls in.
- We own the C++ runtime glue we need (`operator new/delete` → our heap, static
  ctors are already invoked by `sysinit`).
- **Re-entrancy:** real preemption breaks Circle's "critical section by not
  yielding" assumption. Put IRQ-safe spinlocks at driver seams.

---

## 2. Target hardware

- **Raspberry Pi 4** (BCM2711, 4× Cortex-A72, ARMv8-A). Primary target.
- RPi 5 (BCM2712) deferred: I/O is behind the proprietary **RP1** chip over PCIe —
  much harder bring-up. Circle's RPi-5 support exists but is more partial.

---

## 3. AArch64 MMU facts (as Circle configures them — verified in source)

These constrain every memory decision. From `lib/memory64.cpp`,
`lib/translationtable64.cpp`, `include/circle/armv8mmu.h`:

- **64 KB granule** (`TG0 = 64KB`), **EL1 stage-1 only**.
- **2 levels**: L2 entry = 512 MB (table descriptor → L3), L3 page = 64 KB.
- **TTBR0 only**; `TCR_EL1.EPD1 = 1` → **TTBR1 disabled** (high-half VAs fault).
- **VA window = 64 GB** on RPi 4 (`T0SZ = TCR_EL1_T0SZ_64GB = 28`, 36-bit).
  (RPi 5: 128 GB.)
- `TCR_EL1.A1 = 0` → **ASID is taken from TTBR0_EL1[63:48]**. `AS` unset → **8-bit
  ASID** (256 contexts).
- **Identity mapping** covers **[0, 4 GB)** (RAM + the PCIe window at
  `0xFA000000`); high RAM is windowed at **[1 GB, 3 GB)**. Everything in
  **[4 GB, 64 GB) is left unmapped** by Circle → free for us.
- `MAIR_EL1`: idx0 = Normal WB (`ATTRINDX_NORMAL`), idx1 = Device-nGnRE
  (`ATTRINDX_DEVICE`), idx2 = Device-nGnRnE (`ATTRINDX_COHERENT`, used for DMA-
  coherent region). Our user RAM uses **idx0 + SH inner-shareable**.

---

## 4. Address-space design (Pi-4)

Single TTBR0 per process. The kernel/identity half must stay at low VAs (because
identity needs VA == PA), so **kernel and user both live in TTBR0**; kernel L2
entries are shared, user L2/L3 are per-process.

```
VA                                                            attrs
0x0000_0000 ┌───────────────────────────────────────────┐
            │ KERNEL IDENTITY (RAM, MMIO, PCIe window)   │  AP=RW_EL1 (EL0 ✗)
            │ code RX / data RW-XN / device / DMA-coherent│  nG=0 (global), shared
0x1_0000_0000 (4 GB) ──────────────────────────────────── │  (identity, VA==PA)
            │ ░░░ unmapped hole ░░░                       │
0x2_0000_0000 (8 GB) = USER_VA_BASE ────────────────────── │
            │ USER text/data/heap  (per process)         │  AP=RW_ALL / RO_ALL+X
            │   ...                                        │  nG=1, ASID-tagged
0x4_0000_0000 (16 GB) = USER_STACK_TOP ─────────────────── │  UXN per type
            │ USER stack (grows down)                     │
            │ ...                                          │
0x10_0000_0000 (64 GB) ───────────────────────────────────┘  T0SZ ceiling
```

Constants live in `kernel/include/kern/layout.h`.

### Per-process page tables
- Each process owns a fresh **L2 table** (one 64 KB page).
- **Kernel entries shared**: copy the L2 descriptors for the identity region from a
  template (they point at the *same* shared kernel L3 tables). One descriptor copy
  per 512 MB slot — cheap.
- **User entries**: allocate new L3 table(s) for the L2 slots covering the user
  region; fill page descriptors as `nG=1`, ASID-tagged, `AP=RW_ALL` (data) or
  `RO_ALL`+`UXN=0` (code), `PXN=1`.

### Page-descriptor attribute matrix (L3, 64 KB pages)
| | AttrIndx | AP[2:1] | SH | nG | PXN | UXN |
|---|---|---|---|---|---|---|
| Kernel code (Circle) | NORMAL | RW_EL1 (0) | inner | 0 | 0 | 1 |
| Kernel data/DMA | NORMAL/COHERENT | RW_EL1 (0) | inner/outer | 0 | 1 | 1 |
| MMIO | DEVICE | RW_EL1 (0) | — | 0 | 1 | 1 |
| User code | NORMAL | RO_ALL (3) | inner | 1 | 1 | **0** |
| User data/stack | NORMAL | RW_ALL (1) | inner | 1 | 1 | 1 |

### Context switch (address-space part)
```
msr ttbr0_el1, x0      ; x0 = phys(L2 table) | (ASID << 48)
isb
```
No TLB flush on switch (ASID-tagged). On ASID **recycle**:
`tlbi aside1is, Xt ; dsb ish ; isb`.

---

## 5. Scheduler & the CScheduler seam (resolves piège 2)

Circle's `CScheduler` is **cooperative**; its context switch (`taskswitch.S`) saves
**only callee-saved** registers because it's a voluntary call. Preemption interrupts
at an arbitrary PC, so we must save the **full trap frame** in the IRQ vector.

Plan:
1. Write a **preemptive** scheduler: run queues, per-thread kernel stack + TCB,
   timer-tick driven, sleep/wake, round-robin (+ priorities later).
2. **Reimplement Circle's `CScheduler` / `CSynchronizationEvent` API on top of it**
   (same class surface: `Yield`, `Sleep/MsSleep/usSleep`, `GetCurrentTask`, `Get`,
   `IsActive`, and the friend hooks `AddTask`, `BlockTask(ppWaitListHead, us)`,
   `WakeTasks(ppWaitListHead)` — the last callable from IRQ). Circle drivers that
   block (USB, etc.) then work unmodified, backed by real preemption.
3. Early bring-up can run EMMC/UART in **polling** mode (no scheduler dependency);
   introduce the seam when bringing up USB (which needs blocking + IRQ).

### Preemptive context (trap frame) saved on exception entry
`x0–x30`, `SP_EL0`, `ELR_EL1`, `SPSR_EL1` (+ FP/SIMD if used). A switch swaps the
kernel stack pointer / trap frame; "return to user" restores the frame and `ERET`s.

Note: Circle's `TaskSwitch` (callee-saved swap) is sufficient for **both**
voluntary and preemptive switches, **because** the full trap frame lives on each
thread's own kernel stack (saved by the IRQ vector) and `TaskSwitch` swaps the
kernel SP. So we reuse it unchanged.

### File-level replacement (build plan)
Done with the minimum surface, `circle/` untouched:
- **Shadow header** `kernel/compat/circle/sched/scheduler.h` — put `kernel/compat`
  *before* `circle/include` on the include path so `<circle/sched/scheduler.h>`
  resolves to ours. Adds `OnTimerTick()` / `m_bResched` / slice; same public +
  friend API.
- **Replace** these `circle/lib/sched/` files — compile our versions instead:
  - `scheduler.cpp` → `kernel/sched/scheduler.cpp` (preemption-ready; `wfi` idle
    task; identical block/wake/timeout protocol).
  - `task.cpp` → `kernel/sched/task.cpp` (one change: `TaskEntry` enables IRQ so a
    freshly-created task does not inherit the switch's IRQ-disabled state).
- **Reuse unchanged** from `circle/lib/sched/`: `taskswitch.S`,
  `synchronizationevent.cpp`, `mutex.cpp`, `semaphore.cpp`, `pipe.cpp`.
- `task.h` is unmodified (`CTask` layout unchanged), so per-process/ASID linkage
  in #5 can hang off the existing `TASK_USER_DATA_USER` slot.

---

## 6. Exceptions & syscalls

- Install our own **`VBAR_EL1`** (16 entries: sync/IRQ/FIQ/SError × 4 source
  modes). Handle sync-from-EL0 (`SVC` → syscall, aborts → page-fault) and IRQ from
  both ELs.
- Decode `ESR_EL1.EC`: `0x15` = SVC from AArch64 EL0.
- **Syscall ABI** (Linux-ish): number in `x8`, args `x0–x5`, return in `x0`.
- **copy_from_user / copy_to_user**: validate user range; use `LDTR`/`STTR`
  (unprivileged load/store) so the access honors EL0 permissions while in EL1.
  (Alternatively leave PAN off and access directly — less safe.)
- **User DMA**: user buffers are not identity-mapped → for v1 use a **bounce
  buffer** (copy user ↔ kernel identity buffer, then DMA); later, walk the user
  table to pin frames and get PAs.

### Implementation (build) — files & integration
- New files: `kernel/arch/aarch64/vectors.S` (our `KVectorTable` + stubs +
  `install_vectors` + `enter_user`), `kernel/arch/aarch64/exception.cpp` (C
  handlers + `PeriodicTick`), `kernel/sys/syscall.cpp` (dispatch + `copy_*_user`),
  `kernel/include/kern/{trapframe,syscall}.h`.
- We **keep Circle's** `exceptionstub64.S` / `exceptionhandler64.cpp` /
  `interruptgic.cpp` compiled and **install our `VBAR_EL1` over** Circle's (after
  `CExceptionHandler` runs). Our IRQ stub calls Circle's `InterruptHandler`
  (GIC dispatch + EOI); our FIQ vector branches to Circle's `FIQStub`; EL1 faults
  reuse Circle's `ExceptionHandler` dump. `CExceptionHandler` must still be
  instantiated so `ExceptionHandler()` can reach `CExceptionHandler::Get()`.
- **Preemption correctness**: `Yield` runs the switch with IRQ disabled and
  restores the *full* prior `DAIF`; the timer tick (`HZ=100`) drives `OnTimerTick`
  via `CTimer::RegisterPeriodicHandler`; `KernelIRQExit` reschedules post-EOI.
- **EL0 round-trip** (`enter_user`) is built but not exercised until #5 maps a
  user page; for now the syscall path is self-tested with an SVC from EL1.

---

## 7. User binaries

Conventional ELF base (`0x10000`) **collides with the identity-mapped RAM** → unusable.
Options:
- **PIE** (preferred): loader picks a high VA in the user region; or
- custom linker script placing `.text` at `USER_VA_BASE` (8 GB).

Loader: read ELF64 from FatFs → map `PT_LOAD` segments → set up user stack → `ERET`
into EL0.

---

## 8. Milestones (bring-up order)

1. ✅ Skeleton + this doc + `layout.h`.
2. ✅ Own `main()` taking over from Circle; init kept subsystems; serial console.
3. 🚧 Scheduler replacement + `CScheduler` shadow (kernel threads, cooperative).
   Preemption hook (`OnTimerTick`) in place; timer-IRQ trigger wired in #4.
4. 🚧 Exception vectors EL0/EL1 + trap frame + syscall path + **preemption**
   (100 Hz tick → `OnTimerTick` → IRQ-exit reschedule). Syscall self-tested via
   SVC from EL1; `enter_user` built. The actual EL0 process round-trip is
   exercised in #5/#6 (needs a mapped user page).
5. 🚧 Per-process page tables (`CAddressSpace`) + TTBR0/ASID switch on every task
   switch; first real EL0 process (loads an embedded stub, `enter_user`, does an
   SVC). Isolation is structural (kernel pages AP=EL1-only). Clean process exit +
   address-space teardown deferred to #6.
6. ELF loader from SD → first real EL0 process.
7. Multi-process preempted concurrently + core syscalls (read/write/exit/yield/
   sleep/brk/spawn), file I/O via FatFs. IPC (pipe/channel) later.

---

## 9. Build (deferred)

Toolchain via **WSL** (aarch64 bare-metal GCC). Build Circle's needed libs, then
link our kernel objects + a linker script producing `kernel8.img`. Boot path on the
Pi: VideoCore firmware → (optionally U-Boot) → our image.

### Status: builds ✅

`#1`–`#5` compile and link into `kernel8-rpi4.img` (~80 KB) with the
`aarch64-none-elf` (GCC 10.3) toolchain under WSL. Confirmed steps:

```sh
# one-time: renormalize the Circle clone to LF (it was checked out CRLF on Windows)
git -C circle config core.autocrlf false && git -C circle rm --cached -rq . && git -C circle reset --hard
# build Circle's libraries (skip tools/, which are host-only and need host gcc)
cd circle && ./configure -r 4 -p aarch64-none-elf- -f
(cd lib && make -j4) && (cd lib/sched && make -j4) && (cd lib/fs && make -j4 && cd fat && make -j4)
# build our kernel
cd ../kernel && make            # -> kernel8-rpi4.img
```

Not yet run: no QEMU in this WSL; runtime test needs `qemu-system-aarch64 -M raspi4b`
or a real Pi 4 (rename to `kernel8.img`, `arm_64bit=1`).

### Build manifest

Compile (target **RASPPI=4**, AArch64, 64 KB granule):
- **Our sources**: `kernel/main.cpp`, `kernel/kernel.cpp`, `kernel/sched/scheduler.cpp`,
  `kernel/sched/task.cpp`, `kernel/sys/syscall.cpp`, `kernel/mm/addrspace.cpp`,
  `kernel/arch/aarch64/{vectors,user_stub}.S`, `kernel/arch/aarch64/exception.cpp`.
- **Include paths (order matters)**: `kernel/compat` **first** (so
  `<circle/sched/scheduler.h>` resolves to our shadow), then `kernel/include`,
  then `circle/include`.
- **Circle**: build its libs, but **exclude** `lib/sched/scheduler.cpp` and
  `lib/sched/task.cpp` (replaced by ours). Keep everything else (incl.
  `lib/sched/{taskswitch.S,synchronizationevent,mutex,semaphore,pipe}`,
  `lib/exceptionstub64.S`, `lib/exceptionhandler64.cpp`, `lib/interruptgic.cpp`).
- Link our objects + Circle libs with a linker script → `kernel8-rpi4.img`.
  Because our `scheduler.o`/`task.o` are in `OBJS` (before the `--start-group`
  archives), the linker resolves `CScheduler`/`CTask` from them and never pulls
  `libsched.a`'s versions — verified (single `CScheduler::Yield` in the image, no
  duplicate-symbol error). No need to strip `libsched.a`.
