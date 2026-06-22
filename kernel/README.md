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
  kernel.h/.cpp     CKernel: takes over after sysinit, brings up serial console
  include/kern/
    layout.h        address-space map + AArch64 page-table attribute constants
```

Source subtrees to come: `arch/aarch64/` (vectors, context switch), `sched/`
(preemptive scheduler + CScheduler seam), `mm/` (per-process page tables),
`proc/` (process model, ELF loader), `sys/` (syscalls).

## Status

- **#1 done** — skeleton, architecture doc, layout header.
- **#2 in progress** — `main()` takes over from Circle; serial console + timer
  heartbeat. Build is deferred (toolchain via WSL), so this is not yet compiled.

## Build

Deferred. Target: aarch64 bare-metal GCC under WSL, linking Circle's static libs,
producing `kernel8.img` for Raspberry Pi 4. See ARCHITECTURE.md §9.
