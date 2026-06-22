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
```

Source subtrees to come: `arch/aarch64/` (vectors, context switch), `mm/`
(per-process page tables), `proc/` (process model, ELF loader), `sys/` (syscalls).

Build wiring for the scheduler (which Circle sched files to replace vs reuse, and
the `kernel/compat` include-path ordering) is documented in
[../ARCHITECTURE.md](../ARCHITECTURE.md) §5.

## Status

- **#1 done** — skeleton, architecture doc, layout header.
- **#2 done** — `main()` takes over from Circle; serial console + timer heartbeat.
- **#3 in progress** — `CScheduler` replacement (shadow header + our scheduler.cpp);
  two demo kernel threads. Cooperative for now; the preemption hook is in place and
  the timer-IRQ trigger lands in #4. Build deferred (WSL), so nothing is compiled.

## Build

Deferred. Target: aarch64 bare-metal GCC under WSL, linking Circle's static libs,
producing `kernel8.img` for Raspberry Pi 4. See ARCHITECTURE.md §9.
