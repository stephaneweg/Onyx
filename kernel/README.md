# kernel/

Our multi-process kernel, built on top of Circle as the HAL + driver layer.
See [../ARCHITECTURE.md](../ARCHITECTURE.md) for the full design.

## Dependency

Circle is **not** committed here (see root `.gitignore`). Clone it next to this
tree before building:

```sh
# from the repo root (C:\Temp\Zircon)
git clone --recurse-submodules https://github.com/rsta2/circle.git
```

## Layout (current)

```
kernel/
  main.cpp          entry point (main(), called by Circle's sysinit)
  kernel.h/.cpp     CKernel: takes over after sysinit; serial console + sched demo
  include/kern/
    layout.h        address-space map + AArch64 page-table attribute constants
  compat/circle/sched/
    scheduler.h     SHADOW of <circle/sched/scheduler.h> (preemption-ready)
  sched/
    scheduler.cpp   our CScheduler (replaces circle/lib/sched/scheduler.cpp)
    task.cpp        CTask (replaces circle's; TaskEntry enables IRQ for new tasks)
  arch/aarch64/
    vectors.S       our VBAR_EL1 table + trap-frame stubs + enter_user
    exception.cpp   C exception handlers + 100 Hz preemption tick
  arch/aarch64/
    user_stub.S     tiny position-independent EL0 test program (#5)
  mm/
    addrspace.cpp   CAddressSpace: per-process L2/L3 tables, TTBR0/ASID switch
  sys/
    syscall.cpp     syscall dispatch + copy_*_user
  include/kern/
    trapframe.h     trap frame layout (asm + C)
    syscall.h       syscall numbers + ABI
    addrspace.h     per-process address space
```

Source subtrees to come: `proc/` (process model, ELF loader), more of `sys/`
(file I/O, spawn).

Build wiring for the scheduler (which Circle sched files to replace vs reuse, and
the `kernel/compat` include-path ordering) is documented in
[../ARCHITECTURE.md](../ARCHITECTURE.md) §5.

## Status

- **#1 done** — skeleton, architecture doc, layout header.
- **#2 done** — `main()` takes over from Circle; serial console + timer heartbeat.
- **#3 done** — `CScheduler` replacement (shadow header + our scheduler.cpp/task.cpp);
  idle task; two demo kernel threads.
- **#4 done** — our `VBAR_EL1` vectors (EL0/EL1), trap frame, syscall dispatch,
  and **preemptive multitasking** (100 Hz tick → reschedule on IRQ exit).
- **#5 in progress** — `CAddressSpace` (per-process L2/L3 tables sharing kernel
  identity, ASID-tagged user pages), TTBR0/ASID switch on every task switch, and a
  first **EL0 process** (loads `user_stub`, `enter_user`, does an SVC). Process
  exit/teardown comes in #6. Build deferred (WSL), so nothing is compiled yet.

See [../ARCHITECTURE.md](../ARCHITECTURE.md) §9 for the build manifest (files to
compile, include-path order, which Circle files to exclude).

## Build

Deferred. Target: aarch64 bare-metal GCC under WSL, linking Circle's static libs,
producing `kernel8.img` for Raspberry Pi 4. See ARCHITECTURE.md §9.
