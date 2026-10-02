# kernel/

The **Onyx kernel**: a multi-process kernel built on top of Circle (the HAL + driver layer,
our fork in `../circle`, a git submodule). Every process runs at **EL0** and calls the kernel
by system calls through the `kapi` table (ABI v74). The full description:
[`docs/02-KERNEL-INTERNALS.md`](../docs/02-KERNEL-INTERNALS.md); the design history:
[`../ARCHITECTURE.md`](../ARCHITECTURE.md) (historical) and
[`docs/EL0-PROTECTED-MODE.md`](../docs/EL0-PROTECTED-MODE.md).

## Layout

```
kernel/
  main.cpp            entry point (main(), called by Circle's sysinit)
  kernel.h/.cpp       CKernel: init (console, vectors, scheduler, kapi table, cores 1-3,
                      SD/FatFs, RAM:, USB), the kernel tasks (compositor, reaper, input,
                      GUI watchdog), the process task (ELF load, user stack, El0Enter),
                      the cmdline.txt options
  arch/aarch64/
    vectors.S         our VBAR_EL1 table, trap frame (SAVE_TRAP/RESTORE_TRAP), IRQ path
    el0.S             EL0 entry/exit: system calls, IRQs and faults from EL0, El0Enter/El0Return
    el0blob.S         the user-side code mapped in every process: memcpy/memset/memmove,
                      the event pump, the return paths (thread, main, app-core job)
    uaccess.S         fault-safe copies to / from app memory (+ fixup table)
    exception.cpp     kernel fault handling, panic, IRQ exit (stall samples, app-core stop)
  mm/addrspace.cpp    CAddressSpace: per-process L2/L3 tables (64 KB pages), ASID, mappings
  sched/              our CScheduler / CTask (replace Circle's): preemption of apps, the
                      CPU-hog priority, real-time tasks, one scheduler per core that runs tasks
  compat/circle/sched/scheduler.h   SHADOW of <circle/sched/scheduler.h>
  proc/elf.cpp        ELF64 loader
  sys/
    kapi.cpp          the kapi implementation (files, windows, processes, IPC, input, ...)
    kapitable.cpp     KApiTableInit: the system-call dispatch table
    el0.cpp           the EL0 table + code page, El0SyncHandler (svc dispatch, faults,
                      ID register emulation), per-process system-call statistics
    uaccess.cpp       kapi pointer checks (kern/uaccess.h)
    handle.cpp        per-process handles (kern/handle.h)
    thread.cpp        threads, mutexes, events, barriers, posts, word waits (futex)
    stream.cpp        streams, pipes, stdio
    ipc.cpp           mailboxes, IPC services
    net.cpp           TCP sockets, the network core (netcore=1)
    sound.cpp         sound on core 1 (PWM, FM synthesizer, mapped ring)
    appcore.cpp       app cores 2-3
    v3d.cpp           the GPU (V3D): 3D, textures, compositing blends
    ramfs.cpp         the RAM: volume
    vfs.cpp           user-space file-system providers (an app serving FTP: ...)
    fslock.cpp        the FatFs volume lock (sleeping, re-entrant)
    crashlog.cpp      the crash record, the hang watchdog (core 1)
    debugcon.cpp      the post-mortem console
  gui/                GImage (software renderer), windows + compositor, shared surfaces
  include/kern/       the headers (layout.h: the address-space map; kapi_abi.h: the ABI)
```

## Build

With the `aarch64-none-elf` toolchain on the `PATH`, and Circle's libraries built once
(docs/03 §2):

```sh
make -j8 || make -j1     # kernel8-rpi4.img, then every app and tool (../user)
make stage               # copy the image, apps/<name>.app/main and bin/<tool> to ../sdcard/
```

`make` checks that the image and its BSS end below `0x280000` (`sizecheck`): past it the Pi
does not boot. A kernel-only change that respects the ABI needs no app rebuild. Details:
[`docs/03-DEVELOPER-GUIDE.md`](../docs/03-DEVELOPER-GUIDE.md) §2–§4, §10 (extending the kapi).
