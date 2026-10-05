# Protected mode for apps (EL0 + system calls) — design note

> **Status (2026-10-02): done — every app runs at EL0, the EL1 ("legacy") mode is removed
> (kapi v74; merged into `main`, published as the package `onyx` 2026.10.21).** Steps 0–5 were
> tried on the Pi (v73, opt-in): all passed; then the legacy path was removed, the ID register
> reads emulated, the system calls counted per process (`sysstat`), every app audited
> (`tools/el0scan.sh`) and rebuilt; v74 tried on the Pi: every test passes. What was done:
> [§7](#7-implementation-2026-10-02); the reference is docs/02 §6. §1–§6 below are the original
> study (2026-09-30, kapi v68, 189 entries): they describe the system **before** the work.

## 1. Where things stood (2026-09-30, before the work)

- Apps run at **EL1t**, in their own `TTBR0`/ASID, and call the kernel through the `kapi` table
  mapped read-only at `KAPI_TABLE_VA` (14 GB): a plain indirect call, no trap
  ([02 §3–§4, §8](02-KERNEL-INTERNALS.md)).
- The kernel identity region (0–4 GB: RAM, MMIO, PCIe) is mapped **RW at EL1** in every address
  space. So an app can read/write kernel memory, poke device registers, and — through the
  identity map of physical RAM — reach other apps' pages. ASID isolation only holds against
  well-behaved code.
- Task and thread stacks live in the **kernel heap** (identity region).
- A fault anywhere halts the machine (the post-mortem console).
- The EL0 machinery from the early design is still there, **dormant**: `enter_user`
  (`arch/aarch64/vectors.S`), `SyncEL0Entry`, `SyscallEntry` (`sys/syscall.cpp`, self-tested
  only), `copy_from_user`/`copy_to_user` (byte-wise `LDTR`/`STTR`, **no fault recovery**), the
  `KPAGE_ATTR_USER_*` presets.
- Why EL1 was chosen ([`ARCHITECTURE.md` §11](../ARCHITECTURE.md), "Option C"): direct-call
  ergonomics — at the time apps were linked against kernel symbols. That reason has since
  weakened: apps now go through a **table at a fixed address**, which is exactly the seam a
  syscall layer needs.

## 2. What it would bring

1. **Fault isolation** — a crash in NetSurf, an emulator, Koton… kills the process, not the
   OS. The main gain.
2. **No silent corruption** — a wild pointer into 0–4 GB today scribbles on the kernel heap and
   crashes later elsewhere; at EL0 it faults at once, at the right PC.
3. **Real isolation** — from the hardware (no direct MMIO) and between apps (no path to other
   apps' RAM via the identity map).
4. **An explicit kernel boundary** — every argument validated on entry; a prerequisite for
   running ported or third-party code without trusting it.
5. **Possibly simpler preemption** — an IRQ taken from EL0 arrives on the kernel stack; the
   `PreemptTrampoline` (`SP_EL0`/`SP_EL1`) trick may become unnecessary for EL0 apps.

## 3. What it would cost

### Performance — small

A `kapi` call today is a `blr` (a few ns). `svc` + register save + argument validation is on the
order of 0.2–0.5 µs (an estimate, to be measured). At 10 000 calls/s that is < 0.5 % of a core.
Only very fine-grained calls in hot loops matter (a `gpu_*` per primitive, tiny `sound_write`
blocks). Canvases, surfaces, the sound ring and GPU vertex buffers are already mapped into the
app: no pixel copies. **Exception:** `memcpy`/`memset`/`memmove` (§4.5) must not become
syscalls.

### Work — the real cost

See the inventory below; in short, from heaviest to lightest: pointer validation across ~110
entries, opaque handles, EL0 entry/exit with user stacks (small but delicate on hardware),
threads/`core_run`, app-side changes.

## 4. Inventory of the `kapi` ABI (v68)

### 4.1 Classification of the 189 entries

| Category | Count | Work |
|---|---|---|
| No pointer argument | 79 | stub + syscall, nothing else |
| Takes a `const char *` | 46 | fault-safe bounded `strnlen` + copy into a kernel buffer (also closes a TOCTOU: `ResolvePath` reads the path while the app's other threads run) |
| Buffer + length (in or out) | ~50 | range check `[p, p+n)` in the user VA range + fault-safe access |
| Nested pointers | 2 structs | `kapi_gpu_frame.pixels`, `kapi_gpu_program.vs/cs/fs`: validate each inner pointer too |
| Returns a mapping | 23 return a pointer | all go through `CAddressSpace::Map*` → only the page attributes change (§4.3) |

Today only **7** `IS_USER_VA` checks exist in `kernel/sys/`; one comment
([`kapi.cpp`, `kapi_win_list`](../kernel/sys/kapi.cpp)) notes a check is impossible because an
app's stack is in the kernel heap. With EL0 stacks in user VA, uniform checks become possible.

### 4.2 Callbacks — easier than expected

Key, click, pointer and menu handlers (`gui_handler`) and `post` are **not** called
asynchronously: `kapi_pump_events` (`kernel/sys/kapi.cpp`) pops the window's events and calls
`Ev.ulHandler (sender, event, value)` synchronously in the app's own task; `ThreadsRunPosts`
does the same for posts; `wait_for_exit` and `pump_wait` loop over it.

At EL0: move that loop **to the app side**. The table entries for `pump_events`,
`wait_for_exit`, `pump_wait` point to user code that calls a new `pop_event` / `pop_post`
syscall and invokes the handler itself. No signal-style upcall frame; ABI unchanged.
Detail: `get_modifiers` during a key handler reads `m_nKeyEventMods`, which the kernel sets
around the call — the event must carry its modifiers back to the user-side loop.

### 4.3 Mappings — one place

`create_window`/`resize_window*` (canvas), `wallpaper_buffer`, `surface_map`,
`fullscreen_begin`, `fullscreen_direct` (`MapScreen`), `sound_map`, `gpu_vbuf` (`MapSurface`),
`code_alloc`, `sbrk`, `get_chrome` (chrome copies) all map through `CAddressSpace::Map*` with
`KPAGE_ATTR_APP_*`. Switching the presets to `AP=*_ALL`, `UXN=0` for code (and `PXN=1`), is a
central change. The JIT (`code_alloc`, RWX) additionally needs `SCTLR_EL1.UCI` so the app can
do its own `DC CVAU`/`IC IVAU`.

### 4.4 Handles are raw kernel pointers — the main security hole

`open`, `opendir`, `pipe`, `file_in`, `file_out`, `spawn`, `stdin_stream`, `stdout_stream`
return a `FIL *`, `CStream *`, `CProcess *` or dir object from the kernel heap; `read`, `fsize`,
`fsize64`, `seek`, `close`, `readdir`, `closedir`, `stream_*`, `wait`, `proc_done` cast it back
unchecked (`kapi_close` deletes it). An EL0 app could forge one and have the kernel `delete`
arbitrary memory.

→ A **per-process handle table** (small integers, typed entries). ~20 entries affected, plus
the VFS provider paths (`VfsIsFile`). Side benefit: handles closed automatically when a process
dies (today they appear to leak on kill — to be confirmed). Sockets are already `int`s (check
they are owner-checked); sync objects already use a per-process table (`CProcThreads`).

### 4.5 `memcpy`/`memset`/`memmove` go through the kernel

Since v36 the app Makefiles alias the C names onto `kapi_memcpy`… (`-Wl,--defsym`, see
[`user/kapi.h`](../user/kapi.h)), which call `KT->memcpy` — the kernel's implementation; iconedit
also calls `kapi_memcpy` directly. As syscalls this would be ruinous. Fix: the table entries
point to a **user-side** implementation (mapped next to the stubs). No recompilation.

### 4.6 Threads and app cores

- `thread_create`: the kernel calls `fn (arg)` at EL1 on a kernel-heap stack → needs a user
  stack mapped in the app's space, an `eret` to `fn`, and `thread_exit` as the return path
  (a user-side trampoline).
- `core_run`: cores 2–3 call `fn` bare → `eret` to EL0 on those cores; the fault path
  (`KAPI_CORE_FAULT`) already exists.
- The main task: `CUserProcessTask` calls the entry point directly → `enter_user` with a user
  stack; the `CTask` stack becomes the task's **kernel stack**.

### 4.7 EL0 entry/exit — the delicate part

On `svc` or an IRQ from EL0 (EL1h, on `SP_EL1`): save the user frame, save the user `SP_EL0`,
load `SP_EL0` with the task's kernel stack, switch to EL1t (`msr spsel, #0`) and run the kapi
exactly as today — so kapis that `Yield` (`ChunkedRead`/`ChunkedWrite`, waits) keep working
under Circle's scheduler. On return, restore and `eret`. Preemption: an IRQ from EL0 can
simply `Yield` on that kernel stack. To be brought up on the real Pi.

Fault-safe user access: `copy_from_user`/`copy_to_user` exist but a bad pointer still faults
in the kernel → add an exception **fixup table** (faulting PC → recovery PC returning
`-EFAULT`), and word-sized copies instead of byte-wise.

### 4.8 App-side privileged instructions

| Use | Where | Fix |
|---|---|---|
| `mrs mpidr_el1` (current core) | `user/kapi.h:465`, `user/libc/onyx_syscalls.c:110` | read `TPIDRRO_EL0`, set per core by the kernel |
| `cntpct_el0`, `cntvct_el0`, `cntfrq_el0` | many apps | `CNTKCTL_EL1.EL0PCTEN/EL0VCTEN` |
| PMU (`pmcr_el0`, `pmevcntr*`, `pmccntr_el0`) | `user/gc/gc.h` | `PMUSERENR_EL0.EN` (or drop) |
| `msr daifset` | `user/BinUtils/hangtest.c` | intentionally breaks (it is a hang test) |
| `dmb`, `dsb`, `sev`, `wfe`, `fpcr` | various | fine at EL0 |

The two `mpidr` sites are inline → those apps need a rebuild; everything else is transparent.

## 5. What EL0 does *not* close

- **The GPU.** `gpu_program` submits app-supplied V3D shaders. Unless the driver uses the V3D
  MMU (to check), a shader can read/write all physical memory — DMA bypasses EL0.
- **Powerful kapis** — `reboot`, `shutdown`, `kill_pid`, `inject_key`, `inject_pointer`,
  `inject_modifiers`, `screen_grab`, `win_read`, `win_close` stay open to every app. Real
  protection would need a minimal permission model (e.g. a list in `app.txt`). Separate task.

## 6. Migration path — keeps the ABI

Apps only ever call `KT->fn (...)` at a fixed address. So:

1. **Handles** → per-process opaque handles (§4.4). Useful even at EL1.
2. **Validation** helpers (`UserStr`, `UserBuf`, `UserOut`, fixup table) and a pass over the
   ~110 pointer-taking entries (§4.1). Also useful at EL1 (paired with step 0 below).
3. **EL0 entry/exit**, user stacks, threads, `core_run` (§4.6–§4.7).
4. **Per-process table**: for an EL0 app, `KAPI_TABLE_VA` holds pointers to EL0 stubs
   (`mov x8, #n; svc #0; ret`) plus user-side `memcpy`/event loop. Existing binaries keep
   working unmodified (except the `mpidr` sites).
5. **Opt-in per app** (e.g. `mode = protected` in `app.txt`): both modes coexist; migrate the
   small `bin/` tools first, emulators and NetSurf last.

**Step 0 (cheap, independent):** on a synchronous fault at EL1t whose PC is in the user VA
range, kill the process instead of halting the machine — as `core_run` already does for app
cores. It gives much of gain 1, none of gains 2–3.


## 7. Implementation (2026-10-02)

Done in four parts (each built and reviewed here — there is no Pi 4 emulator — then tried on the
Pi by the user: v73 opt-in, then v74). The reference description is [docs/02 §6](02-KERNEL-INTERNALS.md#6-exceptions-and-vectors).

| Step | What | Where |
|---|---|---|
| 0 | A fault at EL1t in an app's own code (or a wild call from it, or in the kernel's app `memcpy`/`memset`/`memmove`) kills the app, not the machine; `appfault=halt` restores the halt | `arch/aarch64/exception.cpp` (`AppFaultRedirect`, `AppFaultExit`); `/bin/faulttest` |
| 1 | Per-process opaque handles for files, dirs, streams, processes; closed at the process's end; streams and `CProcess` reference-counted; sockets owner-checked | `sys/handle.cpp`, `kern/handle.h`, `sys/kapi.cpp`, `sys/net.cpp`, `sys/vfs.cpp` |
| 2 | Every pointer-taking kapi checks its pointers (`CUserStr`, `UserCopyIn/Out`, `UserReadable/Writable`), fault-safe copies with a fixup table; `nullguard=1` (off by default) takes page 0 out of the apps' spaces | `sys/uaccess.cpp`, `arch/aarch64/uaccess.S`, `kern/uaccess.h`, `mm/addrspace.cpp` |
| 3–5 | EL0 entry/exit on the task's kernel stack, `svc` dispatch, IRQ/preemption from EL0, EL0 mappings, the shared EL0 table + stubs + user-side `memcpy` and event pump (v73: `pop_event`, `event_mods`, `pop_post`, `pump_sleep`), threads and app cores at EL0, `TPIDRRO_EL0` for the core number, opt-in per app | `arch/aarch64/el0.S`, `el0blob.S`, `sys/el0.cpp`, `kern/el0.h`, `mm/addrspace.cpp`, `sys/thread.cpp`, `sys/appcore.cpp`; `/bin/el0test` |

Findings on the way:

- Circle maps **page 0** (armstub, spin table) RWX at EL1 everywhere: a legacy app's NULL write
  corrupts it silently — hence `nullguard`. A protected app cannot reach it (EL1-only).
- An app's legitimate pointer may be on its **stack in the kernel heap** (legacy): the checks accept
  the calling process's own `CTask` stacks for a legacy app, never for a protected one.
- Apps built before v73 read `mpidr_el1` in `kapi__core` and are killed at EL0: every app was
  rebuilt (Jet must be rebuilt apart before it runs protected).
- The A72 has no PAN and no UAO: the kernel reaches EL0 pages directly, and the copies use plain
  `LDR`/`STR` after the range check.

**v74 (the same day): no legacy left.** Every process at EL0; removed: the launch choice
(`appmode=`, `protected=`, `app.txt mode`), `PreemptTrampoline`, the EL1 app-fault path and
`appfault=`, the kernel's app `memcpy` copies, `nullguard` (page 0 is EL1-only anyway), the old
dormant syscall code; added: the ID register emulation, `proc_stats` / `sysstat`, `tools/el0scan.sh`
(the card scans clean), the user-side `memcpy`/`memset` aligning their stores (Device framebuffer);
`hangtest` removed (an app can no longer freeze the machine).

**Still open:** the powerful kapis (§5) need a permission model; the GPU can reach physical memory
through shaders; the crash record does not capture EL0 kills (kmsg only); the system-call cost is
not measured. (Corrected 2026-10-02: `TPIDR_EL0` **is** saved per task -- Circle's `TaskSwitch`
saves and restores it, and an EL0 preemption goes through it; what was missing, an initial value for
a new thread and for an app-core job, came with kapi v75: `thread_create_ex`'s `tls`, the caller's
value for `core_run`. docs/POSIX-PLAN.md §0.1, docs/02 §8 "v75: memory".) The v74 test
on the Pi passed (2026-10-02: el0test, faulttest, threads, app cores, emulators, Jet, media, office,
network, BASIC, kills under load); the GameCube emulator, and perhaps Jet, run a little slower
(leads in docs/HANDOFF.md: the user-side `memcpy` for small copies first). **Next:** that speed;
then demand paging (`mmap`/`munmap`/`mprotect`), the first brick of a POSIX layer.
