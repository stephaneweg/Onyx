# Onyx — Kernel internals

This document describes the inner workings of the **Onyx kernel** (the code under
`kernel/`). It does **not** describe Circle, which is used as the HAL/driver stack;
we only point out the seams with Circle where necessary.

All file paths are relative to the repository root. The values (addresses,
constants) come from the code; the layout constants live in
[`kernel/include/kern/layout.h`](../kernel/include/kern/layout.h).

## Contents

1. [Scope: ours vs. Circle](#1-scope-ours-vs-circle)
2. [Boot sequence](#2-boot-sequence)
3. [Address-space map](#3-address-space-map)
4. [Memory management: `CAddressSpace`](#4-memory-management-caddressspace)
5. [Scheduling](#5-scheduling)
6. [Exceptions and vectors](#6-exceptions-and-vectors)
7. [ELF loader and process model](#7-elf-loader-and-process-model)
8. [The kapi ABI table](#8-the-kapi-abi-table)
9. [Stream / stdio subsystem](#9-stream--stdio-subsystem)
10. [Graphics subsystem (GUI)](#10-graphics-subsystem-gui)
11. [Network subsystem (WLAN, TCP/IP, NTP)](#11-network-subsystem-wlan-tcpip-ntp)
12. [Sound and the second core](#12-sound-and-the-second-core)
13. [Post-mortem debug console](#13-post-mortem-debug-console)
14. [App cores (cores 2 and 3)](#14-app-cores-cores-2-and-3)
15. [The GPU (V3D)](#15-the-gpu-v3d)
16. [The RAM: volume](#16-the-ram-volume)

---

## 1. Scope: ours vs. Circle

| Written by Onyx | Reused from Circle |
|---|---|
| Per-process address spaces (`mm/addrspace.cpp`) | 64 KB page allocator (`palloc`/`pfree`) |
| Scheduler (`sched/scheduler.cpp`, `sched/task.cpp`) | `taskswitch.S`, mutex/semaphore/event |
| `VBAR_EL1` vectors, trap frame (`arch/aarch64/`) | `InterruptHandler` (GIC + EOI), `FIQStub` |
| ELF loader (`proc/elf.cpp`) | EMMC + FatFs, USB HID |
| ABI table + kapi impl. (`sys/`) | `C2DGraphics` (framebuffer), `CTimer` |
| GUI: rendering, compositor, widgets, dialogs (`gui/`) | `CCharGenerator` (bitmap font) |

The kernel is linked against Circle's static libraries. **Two Circle files
are replaced** (`lib/sched/scheduler.cpp` and `task.cpp`): because our `.o` files appear in
`OBJS` *before* the `--start-group` archives, the linker resolves `CScheduler`/`CTask`
from our versions and never pulls in those from `libsched.a`.

A **shadow header** (`kernel/compat/circle/sched/scheduler.h`) is placed **before**
`circle/include` on the include path, so that `<circle/sched/scheduler.h>`
resolves to ours (which adds `OnTimerTick()`, the notion of a time slice, etc.,
while keeping Circle's public + friend API).

---

## 2. Boot sequence

The entry point is `main()` ([`kernel/main.cpp`](../kernel/main.cpp)), called by
Circle's `sysinit` **after** it has already: switched the CPU to EL1, enabled the MMU
(64 KB granule), initialized the GIC, run the static C++ constructors.

All the logic lives in the **`CKernel`** class ([`kernel/kernel.cpp`](../kernel/kernel.cpp)):

### `CKernel::Initialize()` — in order

1. **Text console**: `m_Screen` (HDMI) then `m_Serial` (115200). The boot log goes
   to the HDMI screen so it is visible **without a serial cable**. A `CLogSwitch` routes the
   logger's output (see [§11](#11-post-mortem-debug-console)).
2. **Interrupts + timer**: `m_Interrupt.Initialize()`, `m_Timer.Initialize()`.
3. **Exception vectors**: `install_vectors()` installs our `VBAR_EL1`
   (`KVectorTable`) over Circle's.
4. **Scheduler**:
   - `m_Timer.RegisterPeriodicHandler(PeriodicTick)` — wires the tick to `OnTimerTick()`.
   - `AddrSpaceInit()` — captures the kernel's TTBR0 (base of the shared L2 table).
   - `RegisterTaskSwitchHandler(AddressSpaceTaskSwitch)` — on each task switch,
     activates the new task's address space.
   - `RegisterTaskTerminationHandler(AddressSpaceTaskTerminate)` — frees the address
     space when a task ends.
5. **ABI table**: `KApiTableInit()` fills the `kapi` pointer table (see [§8](#8-the-kapi-abi-table)).
6. **Graphics + SD**: `m_2DGraphics.Initialize()` (32 bpp framebuffer); `m_EMMC` +
   `f_mount()` (FatFs) of `SD:` (partition 1, the boot FAT32 one), then of **`SD1:` … `SD3:`**
   (partitions 2–4) when they hold a FAT or **exFAT** file system (see *Volumes* in §8). The theme from `SD:/etc/theme.txt`; the mouse cursor is built in
   (`BuiltinCursor`, the 12×19 arrow that was `SD:skins/mousecur.bin` — the card has no `skins/`
   folder any more: the window chrome is drawn user-side).
7. **RAM:**: `RamFsInit()` — the RAM volume, its size from `system.ini`'s `ramfs=` (§16).
8. **USB**: `m_USB.Initialize()` (mouse + HID keyboard, hot-plug).

### `CKernel::Run()` — the service loop

1. Logs machine info.
2. Launches the **init program** (`StartAutostart`): the ELF named by the `cmdline.txt`
   option **`init=`** (e.g. `init=SD:/bin/init`), defaulting to `SD:bin/init`
   when the option is absent. The kernel does not parse any launch list itself —
   init (PID-1 style) reads `SD:/etc/autostart` and starts everything from there
   (by default `voronoy`, which paints the wallpaper then exits, and `panel`, the
   shell). Pointing `init=` at a different ELF swaps the whole userland launcher
   (e.g. a recovery shell) without rebuilding the kernel.
3. Launches the **kernel service tasks**:
   - **`CCompositorTask`** — presents the screen at up to ~60 Hz, **only what changed**
     (dirty rectangles): every visual change says where — `ScreenDirtyRect` for a part
     (`kapi_present`/`SYS_present` and chrome: the caller's window; window add / remove /
     raise / move / resize / alpha: its old and new places; the cursor: its old and new
     16×16 spots, with the drag badge) or `ScreenDirty` for all of it (wallpaper, full
     screen end, drag & drop start / end). Rectangles that touch are merged (at most 16;
     more, or over 2/3 of the screen, = the whole screen) and `g_nScreenGen` is bumped.
     The task takes the damage (`ScreenTakeDamage`) and for each rectangle composites with
     the screen image **clipped** to it (`GImage::SetClip`: every blit and fill stays
     inside) and sends just that rectangle to the display (`C2DGraphics::UpdateDisplay
     (x, y, w, h)`, our Circle patches 6 / 12: the frame buffer's 2D DMA reads the rectangle in
     place, source and destination strides; `CCompositorTask::Present` starts it with
     `UpdateDisplayAsync` and **yields** to the other tasks until its interrupt says it is
     done, instead of spinning); the
     whole screen only when told, plus a safety refresh every 2 s. A window animating
     (a game, an emulator) costs its own area instead of the whole screen, a mouse move
     two cursor-sized spots. An idle desktop
     thus costs almost no CPU (the heartbeat's fps shows the real composites). It is created **before** `init` (no boot-log pause any more)
     and the main task hands it the CPU at once with `CScheduler::YieldTo()` (scan starts at
     that task instead of the round-robin next), so it presents before the first app's
     window. Starting it late — after a 6 s pause, with a single GUI app already running —
     used to hang the boot.
   - **`CReaperTask`** — reaps terminated tasks every ~50 ms (frees
     `CTask` + stack + address space outside the scheduler core). It also blinks the
     green ACT LED as a headless sign of life: 1 s period while the network is down,
     0.2 s once `NetIsUp()`; a frozen LED means the scheduler stopped.
   - **`CInputTask`** (if USB is present) — pumps keyboard/mouse events to the
     window manager; every 100 ms it also looks for new gamepads (`upad1..4`) and USB MIDI
     devices (`umidi1..4`, v68: §8 *USB MIDI*), also after a hot plug. It takes **every**
     USB keyboard (`ukbd1..4`, each with the layout snapshot), not only `ukbd1`: a wireless
     mouse's receiver (Logitech's nano receiver, e.g. the M187's) has a boot-keyboard
     interface of its own, which may enumerate first and take `ukbd1` — the real keyboard,
     `ukbd2`, was then ignored. Each keyboard's raw report is kept and the window manager gets
     them merged (modifiers ORed, held keys joined), so one keyboard's empty report does not
     release the keys held on another. The mouse is still `mouse1` only.
   - **`CGuiWatchdogTask`** (skipped with `watchdog=0` in `cmdline.txt`) — once a second, checks the GUI and writes to the kernel
     log (`kmsg`): `compositor STALLED` when `CWindowManager::FrameCount()` has not moved
     for 2 s (with every task's `name:state`), `app '<title>' NOT PUMPING events` when a
     window has queued events and its owner has not called `PopEvent` for 2 s (plus the
     matching "recovered" lines), and a **heartbeat** every `heartbeat=` seconds from
     `cmdline.txt` (default 5, 0 = off): uptime, fps, mouse/key deltas, tasks by state,
     windows with queued/dropped events. Counters live in `CWindowManager` (frames,
     `OnMouse`/`OnKey` calls, `Snapshot()`) and `CWindow` (`QueuedEvents`,
     `DroppedEvents`, `LastPumpTicks`).
4. The main task runs an idle loop (`MsSleep`).

---

## 3. Address-space map

One `TTBR0` per process. The kernel (identity) must stay at the **low VAs** (because
identity requires VA == PA), so **kernel and user coexist in `TTBR0`**:
the kernel L2 entries are **shared**, the user L2/L3 are **per process**.

```
VA                        Content                              Attributes
0x0_0000_0000  ┌───────────────────────────────────┐
               │ KERNEL IDENTITY (RAM, MMIO, PCIe)  │  EL1 RW, global (nG=0),
               │ mapped by Circle                   │  shared in every table
0x1_0000_0000  ├───────────────────────────────────┤  (4 GB = KERNEL_IDENTITY_END)
   (4 GB)      │ ░░ unmapped hole (guard) ░░        │
0x2_0000_0000  ├───────────────────────────────────┤  USER_VA_BASE (8 GB)
   (8 GB)      │ .text / .data / .bss / heap (app)  │  EL1, ASID-tagged (nG=1)
0x3_0000_0000  │   window canvas                    │  USER_WINDOW_CANVAS (12 GB)
0x3_4000_0000  │   wallpaper buffer                 │  USER_WALLPAPER_CANVAS (13 GB)
0x3_8000_0000  │   kapi ABI table (read-only)       │  KAPI_TABLE_VA (14 GB)
0x3_9000_0000  │   generated code (a JIT, RWX EL1)  │  USER_CODE_BASE (14.25 GB, v58)
0x4_0000_0000  ├───────────────────────────────────┤  USER_STACK_TOP (16 GB)
   (16 GB)     │ user stack (grows down)            │  1 MB initial
       ...     │                                    │
0x10_0000_0000 └───────────────────────────────────┘  T0SZ ceiling (64 GB) on RPi 4
```

- **64 KB granule, EL1 stage-1 only**, `TTBR1` disabled.
- 2 levels: one **L2 entry = 512 MB** (points to an L3 table), one **L3 page = 64 KB**.
- **8-bit ASID** (256 contexts), taken from `TTBR0_EL1[63:48]`.
- `USER_LOAD_BASE = USER_VA_BASE`: apps are linked at 8 GB (see
  [user.ld](../user/user.ld)).

---

## 4. Memory management: `CAddressSpace`

Source: [`kernel/mm/addrspace.cpp`](../kernel/mm/addrspace.cpp),
[`kernel/include/kern/addrspace.h`](../kernel/include/kern/addrspace.h).

A `CAddressSpace` object = a process. It owns: the L2 table, the ASID, a PID, the
window pointer, the stdin/stdout streams, the process handle, the exit code, and
the argv/cwd string.

**Where the app pages come from, by board RAM.** An app's own frames (ELF segments, heap,
stack, `code_alloc`) are taken by `MapNewPage` with `palloc_high()`: the **high zone**
(1–3 GB, plus the RAM reclaimed above 3 GB) on a 2 / 4 / 8 GB Pi 4. A **1 GB** Pi 4 has no
high zone: `palloc_high()` falls back to the low pager (`PAGE_RESERVE`, 256 MB), which the
apps then share with every process's page tables. The kernel heap (window canvases, the
wallpaper, the full-screen buffer, the GPU's memory, ~680 MB) is the same on every board. So
Onyx runs on 1 GB — the desktop and the small apps take a few MB each — but the apps all
together have ~250 MB instead of ~2 GB or more: the big ones (Jet Browser, the N64 / GameCube
emulators) can run short (`sbrk` / `new` return 0). `memmon` then shows an app pool of 0
(`ram_detail`: the high zone only).

### Construction

1. **Allocates a fresh L2 table** (`palloc`, one 64 KB page).
2. **Copies the kernel L2 descriptors** from the template captured at boot (`memcpy` of
   the whole L2 page). The kernel entries point to the **same shared L3 tables**;
   the L2 slots covering the user area stay invalid (zero).
3. **Allocates an ASID** (1..255; 0 is reserved for the kernel/global).
4. **Maps the `kapi` ABI table** read-only at `KAPI_TABLE_VA` (14 GB) with
   `KPAGE_ATTR_APP_RODATA`. The physical page is a kernel global (not "owned" by
   this process → never freed at destruction).

### `MapPage(VA, PA, attrs, bOwned)`

Maps a 64 KB page (VA and PA aligned to 64 KB):
- `GetOrCreateL3(L2_INDEX(VA))`: if the L2 entry is invalid, allocates a fresh L3 table and
  writes the L2 table descriptor.
- Fills the L3 page descriptor: `AttrIndx`, `AP`, `SH`, `AF=1`, `nG`, output = PA,
  `PXN`, `UXN`. The software bit `PAGE_SW_OWNED` (the *Ignored* field) marks the frames to
  free at destruction.

### Page attribute matrix (`layout.h` presets)

| Use | AttrIndx | AP | nG | PXN | UXN |
|---|---|---|---|---|---|
| `KPAGE_ATTR_APP_CODE` (app code) | NORMAL | RO_EL1 | 1 | **0** (exec. EL1) | 1 |
| `KPAGE_ATTR_APP_DATA` (data/stack/canvas) | NORMAL | RW_EL1 | 1 | 1 | 1 |
| `KPAGE_ATTR_APP_RODATA` (kapi table) | NORMAL | RO_EL1 | 1 | 1 | 1 |
| `KPAGE_ATTR_APP_RWX` (generated code, v58 `code_alloc`) | NORMAL | RW_EL1 | 1 | **0** (exec. EL1) | 1 |
| `KPAGE_ATTR_USER_*` (EL0 legacy, dormant) | NORMAL | *_ALL | 1 | … | … |

App pages are **accessible at EL1** (`AP=*_EL1`), executable at EL1 for code
(`PXN=0`), never executable at EL0 (`UXN=1`), and `nG=1` (ASID-tagged) → isolation
between applications.

### Activation and context switch

```c
// CAddressSpace::Activate()
u64 ttbr0 = MAKE_TTBR0(phys(L2), asid);   // = phys | (asid << 48)
asm volatile ("msr ttbr0_el1, %0; isb" :: "r"(ttbr0) : "memory");
```

No TLB flush on switch (TLB tagged by ASID). The
`AddressSpaceTaskSwitch` hook calls `Activate()` for app tasks, or
`ActivateKernelAddressSpace()` for kernel tasks (which have no address space).

### Destruction (and an important pitfall)

When destroying the address space:
1. Signals EOF on stdout, releases the streams, marks the process handle as terminated.
2. **Invalidates the TLB for this ASID** *before* freeing anything:
   `tlbi aside1is, Xt ; dsb ish ; isb`. (Reusing a page-table frame while the
   walk is still cached would break translations.)
3. Removes and destroys the window.
4. Walks the **user** L2/L3 slots and frees the frames marked
   `PAGE_SW_OWNED`.

> ⚠️ **Pitfall: never free an L3 table shared with the kernel.** Because each
> address space **copies** the kernel L2 descriptors, some L2 entries point
> to the **same** L3 tables as the kernel. Before freeing an L3 from the user
> area, the destructor **compares** the table address against the reference kernel L2's
> and **skips** any shared table. Freeing a shared kernel table would
> corrupt the translations of the *entire system*. (See also the design note
> "per-AS L2 copies the kernel L2".)

---

## 5. Scheduling

Source: [`kernel/sched/scheduler.cpp`](../kernel/sched/scheduler.cpp),
[`kernel/sched/task.cpp`](../kernel/sched/task.cpp),
[`kernel/compat/circle/sched/scheduler.h`](../kernel/compat/circle/sched/scheduler.h).

### Preemptive scheduling (track A)

The kernel **preempts application code**. A 100 Hz timer tick drives `OnTimerTick`,
which counts down the running task's time slice (`SCHED_SLICE_TICKS` = 20 ms) and
sets a reschedule flag when it expires. `KernelIRQExit()` — run at the end of every
IRQ — then forces a switch, **but only when it is safe**: the interrupted context
must be an **application running its own code**, i.e. `SPSR_EL1.M == EL1t` **and**
the return PC (`ELR_EL1`) lies in the **user VA** range (`IS_USER_VA`). An app
inside a `kapi_*` call has a *kernel*-VA return PC and is **not** preempted — that PC
test *is* the kernel lock (the call may hold a kernel resource; such kapis yield
cooperatively anyway). Kernel threads (EL1h, or EL1t with a kernel-VA PC) are never
preempted.

**The SP_EL0/SP_EL1 problem and the trampoline.** Circle runs all tasks in EL1t (on
`SP_EL0`), while the IRQ handler runs in EL1h (on `SP_EL1`). A `TaskSwitch` straight
from the IRQ would swap `SP_EL1`, not the thread's `SP_EL0` → it cannot preempt an
EL1t thread. So instead of switching inside the IRQ, `KernelIRQExit` **redirects the
return**: it stashes the app's `ELR_EL1`/`SPSR_EL1` (in `g_PreemptELR`/`g_PreemptSPSR`),
points `ELR_EL1` at `PreemptTrampoline` and masks IRQ+FIQ. The IRQ's
`RESTORE_TRAP_ERET` then `eret`s into the trampoline **at EL1t, on the app's own
stack**, where it saves the app's full register/FP state and calls the ordinary
cooperative `Yield()` — which now swaps the correct `SP_EL0` and address space. When
the task is rescheduled, the trampoline restores the app state and `eret`s back to
the exact interrupted instruction (original `NZCV` + interrupts restored). A
preempted task is thus parked **exactly like a voluntary yielder** — one resume
path. (The app runs on its `CTask` kernel stack, which is in the global identity
region, so it stays mapped across the address-space switch in `Yield`.)

Because only user-VA application code is preempted, the **kernel itself is
non-preemptive** (the classic Unix model): a long, non-yielding *kernel* loop would
still block other tasks — drop a `if (IsReschedPending()) Yield();`
cooperative-preemption point into any such loop if one appears. Tasks also still
switch **voluntarily** (`Yield`, `MsSleep`, `present`, `wait`, …), unchanged.

**Preemption points in file I/O.** The long kernel operations an app can ask for are cut
into pieces with a `Yield` between them: `kapi_read` / `kapi_save_file` move at most 64 KB
per `f_read` / `f_write` (`ChunkedRead` / `ChunkedWrite` in `sys/kapi.cpp`), the ELF loader
128 KB. Without them one read of a 28 MB file (a Doom WAD, a GBA ROM, a newlib app's
`fopen`, which reads the whole file) stopped every other task — the compositor, the cursor,
the sound feeders — for seconds. Between two pieces the FatFs volume lock is free, so other
tasks' file calls get through; the caller's buffer stays valid (its address space is active
again when it resumes).

**File-system speed.** The FatFs disk layer keeps a 1 MB **sector cache** of the
single-sector reads (the FAT, the folders), write-through (`docs/05` §8; `sdcache=0` turns
it off): opening files and walking folders stopped asking the card for every sector. And
`f_read` / `f_write` move a file's contiguous clusters in **one** SD command (`docs/05`
§10), instead of one per cluster (with 512-byte clusters, one per 512 bytes: 2 MB/s). A
read of 1 MB or more logs its time split (`fs: read ... SD: N commands, waiting, data
port, other tasks`).
The card runs in High Speed (50 MHz, `docs/05` §9; `sdhs=0` = 25 MHz). The boot log says what is in
use (`SD card mounted (SD:): ...`); `/bin/fsbench` measures.

**Yielding SD waits.** The SD driver also yields while the card keeps it waiting: after a
2 ms spin (a normal command ends within that: yielding for it cost a whole time slice per command, 2 MB/s), `CEMMCDevice::TimeoutWait` calls `OnyxDriverWait ()` (a weak hook of our Circle
fork, `docs/05` §7), which `Yield`s if IRQs are on. The FatFs volume lock is held during
that wait, so it is a **sleeping** lock (`OnyxFsLockTake` / `OnyxFsLockGive`,
`sys/fslock.cpp`, re-entrant, its waiters `Yield`): with Circle's spin lock a second task
entering FatFs would spin forever against a holder it never lets run. The holder is in a
**no-kill section** (`CScheduler::EnterNoKill` / `LeaveNoKill`): `TerminateTask` on it
only marks it, and it ends in `LeaveNoKill`, once the lock is free — a dead owner would
lock the card forever. (Before: 100–200 ms freezes each time the menu bar read the
`app.txt` files, or an app opened a file in a big directory.)
Many **short** waits in a row also yield (2026-10-01): a long multi-block write (one SD command
for a file's contiguous clusters, the card taking a few hundred µs per block, each wait well
under 2 ms) kept core 0 for 200 ms (`stall: jet:cache ran 213 ms without yielding`, PC in
`TimeoutWait`). Each turn of a shorter wait calls `OnyxDriverPoll ()` (a second weak hook,
`docs/05` §7), which `Yield`s once the task has run **10 ms** (`DRIVER_SLICE_US`) since its last
`Yield` (`CScheduler::GetLastYield`), under the same conditions (a scheduler on this core, IRQs
on, not core 1's crash dump). Between two blocks of a transfer is a safe point: the volume lock
is held, and the SD host holds a PIO transfer until it is served. Reads (40 MB/s) yield about
every 400 KB, writes every 50–100 KB. Nothing outside FatFs drives the SD card while tasks run:
the boot's `CEMMCDevice::Initialize` comes before the mount, and core 1's crash dump writes raw
sectors only with core 0 stopped (it never yields).

**Stall watchdog.** To find the kernel code that still keeps the CPU too long, the
scheduler notes the time of every `Yield()`. When the running task (not idle) has not
passed through `Yield()` for more than `SCHED_STALL_US` (100 ms), the IRQ exit path
(`KernelIRQExit` → `CScheduler::StallSample`) samples the interrupted PC and LR every
`SCHED_STALL_SAMPLE_US` (50 ms, up to 8 samples). The task's next `Yield()` closes the report
(task name, duration) into a small ring, and the reaper task writes it to the kernel log:

```
stall: doom ran 850 ms without yielding; pc/lr: 1a2b40/1a2a10 1a2b48/1a2a10 ...
```

Kernel addresses resolve with `kernel/kernel8-rpi4.map` (or `aarch64-none-elf-addr2line -f
-e kernel/kernel8-rpi4.elf <pc>`). No sample at all ("IRQs masked") means the task ran
with interrupts off the whole time. (Its first catch: `12dbe4/138044` = `LeaveCritical` from
`CTerminalDevice::Write` — the boot-console log writes, see §11 `MuteNormal`.)

**CPU hogs vs yielders (dynamic priority).** Circle's network and Wi-Fi code waits in
`Yield()` loops (`qlock`/`sleep`/`tsleep` in `addon/wlan/p9proc.cpp`, the `CNetTask`
`Process` loop, socket send/recv — also inside an app's `kapi_*` call, e.g. `vncd`). In
plain round-robin, each of those yields hands the CPU to a CPU-bound app for a full slice,
so the loops crawl. Driver timeouts then expire and the Wi-Fi link drops (the `spin` test
app took the network down, and VNC/telnet with it). So the scheduler lowers the priority
of the tasks the timer has to preempt:

- `PreemptDoYield` calls `CScheduler::OnPreempt()`, which bumps the task's
  **preemption streak** (`m_nPreemptStreak[slot]`). Any voluntary `Yield` (a kapi wait,
  `msleep`, `present`, a kernel task's loop) resets it.
- A task with a streak ≥ `SCHED_HOG_STREAK` (2) is a **CPU hog**. An app that computes more
  than a slice now and then but yields in between (`vncd` encoding a frame, then sending
  it) is not one.
- A hog's preemption opens a **burst** of `SCHED_BURST_US` (60 ms). During the burst
  `GetNextTask` **skips the hogs** (and idle): kernel tasks and apps that yield are served
  first. The burst ends early once none of them is ready.
- A hog also runs with a **one-tick slice** (≤ 10 ms instead of `SCHED_SLICE_TICKS`), so the
  kernel's yield loops never wait more than ~10 ms behind it.

A hog thus keeps roughly 15 % of the CPU while others need it, and all of it
otherwise. Both are tunable at boot for A/B testing: `cmdline.txt` `slice=` (ticks)
and `hogsched=0` (plain round-robin) → `CScheduler::Configure`.

### `CScheduler` (shadow) and `CTask`

- Circle's public + friend API is preserved (`Yield`, `Sleep/MsSleep`,
  `GetCurrentTask`, `AddTask`, `BlockTask`, `WakeTasks`…) → Circle drivers that block
  (USB, etc.) work without modification.
- **The task list has no limit.** Circle's table of `MAX_TASKS` (40) pointers — and its
  `LogPanic` ("System limit of tasks exceeded") when full — is gone: the tasks are a
  **circular linked list** of `TSchedNode`s (`scheduler.h`), each with the scheduler's own state
  about its task (the no-kill depth, a pending kill, the preemption streak, killed from outside).
  `CTask` itself cannot grow (its layout is Circle's, shared with the prebuilt libraries), hence
  the separate node; `AddTask` allocates it, the reaper frees it. The round robin scans the
  circle from the node after `m_pScan` (the last one chosen; `YieldTo` points it just before its
  task); `m_pCurNode` is the running task's node, so the current task's state is reached without
  a search. `EnumerateTasks`'s callback must not yield.
- **A process's tasks are one group** — the tasks whose `TASK_USER_DATA_USER` is the same address
  space (the app's threads, §7). `TerminateTask` on an app's task terminates the whole group
  (`TerminateGroup`: all but the caller); `CountGroup` counts its live ones. A task **killed**
  from outside may still be on a wait list of its process (a mutex, an event, a socket): the
  reaper frees it only once no task of its group is alive, and runs the termination handlers of a
  batch **before** deleting any of its tasks — the last handler frees the process (its sockets
  closed, its objects' wait lists emptied) while the tasks still exist.
- `WakeTasks` **tolerates a task that is no longer blocked**: a `BlockTask` with a timeout that
  expired is made Ready by `GetNextTask` but stays on the event's list until it runs again and
  unlinks itself; an event set in that window (the V3D's frame-done interrupt right after one of
  the drawing task's 2 ms waits ended) found it Ready, and Circle's assertion halted all four
  cores — the random freeze of `n64emu` with the GPU (found with the crash record,
  `sched/scheduler.cpp(742)`). Such a task is now only unlinked.
- **"Real time" tasks (v68, `SetPriority`, `kapi_thread_priority`).** A node's `nPrio`
  (`SCHED_PRIO_HIGH`) makes `GetNextTask` try `ScanPrio` first — only while such tasks exist
  (`m_nPrioTasks`): the first ready real-time task after `m_pScan` wins, before the burst and the
  round robin. The current task is left out of that scan, so a real-time thread that yields while
  still ready (a spin on `kapi_lock` held by an ordinary thread) lets the others run. A tick
  (`OnTimerTick`, after `WordWaitTick`) that finds one ready while an ordinary task runs sets the
  resched flag (`m_bPrioPreempt`): the IRQ exit preempts the app at once (in its own code only, as
  always), and `OnPreempt` does not count that towards the app's hog streak. The priority holds
  only while the task yields by itself: preempted at the end of its slice (a busy loop), it is
  marked `bPrioSpent` — an ordinary task — until its next voluntary `Yield` (sleep, wait). So a
  real-time thread cannot starve the system; an audio pump that renders and sleeps keeps it.
- `Yield()` performs the context switch **with the IRQ disabled** for atomicity, then
  **restores the full `DAIF`** of the resumed task (each task keeps its own IRQ
  enable state).
- The **only** change in `task.cpp`: `TaskEntry()` re-enables the IRQ
  (`msr daifclr, #2`) so that a **fresh task** does not start with the IRQ masked
  inherited from the switcher.
- The idle task uses `wfi` (on core 0; on the network core it only pauses: no timer tick
  there).
- **One scheduler per core that runs tasks.** `CScheduler::Get ()` returns the scheduler of
  the calling core (`s_pThis[core]`, the core read from `MPIDR_EL1`): core 0's, and with
  `netcore=1` core 3's (§11). A `CTask` joins the scheduler of the core that creates it, so
  Circle's code — tasks, `CSynchronizationEvent`, `MsSleep`, `Yield` — works unchanged on
  either core. The wait lists of the events share one spin lock (an event may be set from
  another core). Cores 1-2 have none (`IsActive ()` is FALSE there; `Get ()` falls back to
  core 0's, as before). Circle's own libraries, built with Circle's `scheduler.h`, read
  `s_pThis` as a single pointer in their inline `IsActive ()`: element 0, core 0's.

### Per-task data

`CTask`'s `TASK_USER_DATA_USER` slot holds the task's `CAddressSpace*` pointer
(0 for kernel tasks) — the same one for all the tasks of a process (its threads). The PID, the
state (R/S/B/N), the name and the ASID are accessible via the address space and the
enumeration API (see [§8](#8-the-kapi-abi-table)). The address space counts its tasks
(`AddTask` / `DropTask`; the first is its **main task**, `GetMainTask`); the reaping (`reaper`)
calls the termination handler — which drops the task, and frees the address space with the last
one — then destroys the `CTask`.

---

## 6. Exceptions and vectors

Source: [`kernel/arch/aarch64/vectors.S`](../kernel/arch/aarch64/vectors.S),
[`kernel/arch/aarch64/exception.cpp`](../kernel/arch/aarch64/exception.cpp),
[`kernel/include/kern/trapframe.h`](../kernel/include/kern/trapframe.h).

### `KVectorTable` (our `VBAR_EL1`)

16 entries (4 groups × 4 types). A crucial point from hardware bring-up: the **EL1t
group (Current EL, SP0)** is the one actually taken by the main thread and the
tasks (which run in EL1t). It must route to the real handlers
(`SyncEL1Entry` / `IrqEntry` / `FIQStub`) and **not** to `BadModeEntry` — otherwise the first
tick crashes.

| Group | Sync | IRQ | FIQ | SError |
|---|---|---|---|---|
| Current EL SP0 (EL1t) | SyncEL1 | Irq | FIQStub | BadMode |
| Current EL SPx (EL1h) | SyncEL1 | Irq | FIQStub | BadMode |
| Lower EL AArch64 (EL0) | SyncEL0 | Irq | FIQStub | BadMode |
| Lower EL AArch32 | BadMode ×4 | | | |

### IRQ path

`IrqEntry`: `SAVE_TRAP` → **unmasks the FIQ** (`DAIFClr,#1`, required by
Circle's `EnterCritical`) → calls Circle's `InterruptHandler` (GIC dispatch + periodic
tick + EOI) → `KernelIRQExit` (**preemptive reschedule** via `PreemptTrampoline` — see
[§5](#5-scheduling)) → re-masks the FIQ → `RESTORE_TRAP` + `ERET`.

On cores 2 and 3 (app cores, §14) the same `KVectorTable` is installed: their only IRQ is
the stop IPI, and `KernelIRQExit` hands it to `AppCoreOnIRQExit`; a synchronous fault there
goes to `AppCoreOnFault` instead of the kernel panic.

> Bring-up note: leaving the FIQ masked during `EnterCritical` caused a Circle
> assertion to fail (`synchronize64.cpp`). Hence the `DAIFClr,#1`/`DAIFSet,#1` around the
> call, as Circle's `IRQStub` does.

### Trap frame

`TTrapFrame` (800 bytes): `x[0..30]`, `sp_el0`, `elr_el1`, `spsr_el1`, then the full
FP/SIMD state — `v[0..31]` (the 128-bit `q` registers), `fpsr`, `fpcr`. Saved by the
assembler macro `SAVE_TRAP` onto the stack on exception entry, restored by
`RESTORE_TRAP_ERET`.

The FP/SIMD block is saved on **every** trap so floating-point user code keeps its
registers across a context switch. The cooperative `TaskSwitch` (Circle) only
preserves the callee-saved `d8–d15` mandated by the AArch64 PCS — enough for a
voluntary `Yield()`, but **not** enough when the timer preempts a thread
mid-computation. Saving the whole register file here (and in `PreemptTrampoline`, see
§ "Preemptive scheduling") keeps hardware float correct under both voluntary switches
and preemption from the IRQ-exit path. FP/SIMD is enabled at EL0/EL1 at boot (`CPACR_EL1`,
`circle/lib/startup64.S`), so the `stp`/`ldp` `q`-register forms never trap.

### System calls (dormant)

The `ESR_EL1.EC` decode recognizes `SVC64 = 0x15`. `SyscallEntry` dispatches on `x8`
(number) with args `x0–x5` and return in `x0` (Linux-style ABI). **But** because the apps
run in EL1 and call `kapi_*` directly, **this path is not taken in
normal operation**; it is only self-tested. The `copy_from_user`/
`copy_to_user` helpers use the unprivileged `LDTR`/`STTR` accesses. Synchronous non-SVC EL1
faults trigger a panic dump to the screen (`PanicToScreen` + Circle's register
dump).

---

## 7. ELF loader and process model

Source: [`kernel/proc/elf.cpp`](../kernel/proc/elf.cpp),
[`kernel/include/kern/elf.h`](../kernel/include/kern/elf.h),
task model in `kernel.cpp`.

### `LoadELF(image, size, AS, &entry)`

- Validates the ELF64 header (magic, `ELFCLASS64`, `EM_AARCH64=183`, type `ET_EXEC`/`ET_DYN`).
- For each **`PT_LOAD`** segment: validates the bounds (file + VA within the user
  area), chooses the attributes according to `PF_X` (code = `APP_CODE` RO+X, data =
  `APP_DATA` RW), then `LoadSegment`:
  - maps all the 64 KB pages covering `[vaddr, vaddr+memsz)` (via `MapNewPage`),
  - copies `filesz` bytes from the file (the BSS beyond stays zero).
- `SyncDataAndInstructionCache()` after writing the code, then returns `e_entry`.

### `CUserProcessTask` — one application = one task

`CUserProcessTask` (subclass of `CTask`, **256 KB** stack — or what the app's folder's
`app.txt` asks for, `stack = 8M`: `AppStackSize` reads it in the launcher's context before
the task exists, since `CTask` allocates its stack in its constructor; rounded up to 64 KB,
at most 64 MB; Jet Browser asks for 8 MB):
1. Creates a fresh `CAddressSpace`.
2. Installs stdin/stdout, the process handle, argv, cwd.
3. `LoadELF` into the address space.
4. `SetUserData(AS, TASK_USER_DATA_USER)` + `AS->AddTask(this)` (its main task) +
   `Activate()` (switches `TTBR0`/ASID).
5. **Calls the entry point directly**: `((void(*)())entry)()` — in EL1, in the
   app's page table + stack. No trap.
6. On return, `ThreadsEndProcess()` (its other threads end with it) and `Terminate()`; the
   reaper reaps the tasks and frees the address space with the last one.

### Threads (v67)

Source: [`kernel/sys/thread.cpp`](../kernel/sys/thread.cpp),
[`kernel/include/kern/thread.h`](../kernel/include/kern/thread.h).

A thread is a `CUserThreadTask`: one more `CTask` whose `TASK_USER_DATA_USER` is the app's
`CAddressSpace` (set in its constructor, before it is first scheduled: no `Yield` in between), so
the task switch activates the app's page table and every kapi sees the same process — window,
heap, files, sockets, cwd. It calls `fn (arg)` at EL1 on its own stack (kernel heap: the identity
region, mapped in every space — like the main task's; 256 KB by default, 16 KB .. 16 MB), and
the timer preempts it in its own code like any app (§5). `fn`'s return, or `kapi_thread_exit`,
records its exit code and ends the task; the reaper frees it while the process goes on. A process
runs at most `THREADS_MAX` (32) threads besides its main one.

- **The process ends with its main task** (return from `main`, `kapi_exit`), or when any thread
  calls `kapi_exit`: `ThreadsEndProcess` → `TerminateGroup` (all but the caller). A kill (task
  manager, `kill_pid`, an orphan) ends the group the same way (§5). A fault anywhere still halts
  the machine (the post-mortem console, §13), as before.
- **The per-process state** — `CProcThreads`, made on first use, freed by `~CAddressSpace`
  (`ThreadsFree`, first): the thread records (tid → task, done, exit code; 64: the ended ones are
  kept until joined, the oldest reused first), the synchronisation objects (256 handles), the
  posted-call ring (256) and two events: `DoneEv` (a thread ended: its joiners) and `WakeEv` (a
  post, a window event, the close box: `pump_wait`).
- **The objects** — mutex (recursive, an owner; released when its owner thread ends), event
  (manual or auto reset), barrier (a count, a round number). A kapi runs on core 0 and is not
  preempted, so an object's check-then-wait is atomic: the objects need no lock. A waiter blocks
  on the object's `CSynchronizationEvent`, only ever set-then-cleared (Circle's `Pulse` is
  private) — a condition variable's broadcast; it counts itself in `nWaiters` across the wait and
  checks its condition again when woken. An object closed while waited on (`sync_close`) is freed
  by its last waiter (they return −2); those left at the process's end are pulsed — unlinking the
  killed tasks still on their wait list, which `~CSynchronizationEvent` asserts is empty — and
  deleted.
- **Posts**: `post (fn, ctx, value)` appends to the ring and pulses `WakeEv`; `pump_events` runs
  the pending posts (at most one ring's worth) before the window's events, on the calling thread.
  `pump_wait` points the window's wake-up (`CWindow::SetWake`: `PushEvent` and `RequestExit`
  pulse it) at `WakeEv`, sleeps if nothing is pending, then pumps; `wait_for_exit` sleeps on it
  too (16 ms at most, as before).
- **Word waits (v68, a futex)** — `wait_word (addr, expected, ms)` / `wake_word (addr)`. The
  word's **physical** address is the key (`WordPhys`: `AT S1E1R` on the caller's `TTBR0`, then
  `PAR_EL1`; below 4 GB, the kernel's identity map): the same word of a shared surface, mapped at
  another address in each process, wakes across processes. A waiter (`TWordWaiter`: the address,
  the value, its process, a `CSynchronizationEvent`) lives on its task's stack, linked in one
  kernel list under an IRQ spin lock; it is linked, then the word read again, then it waits —
  with the IRQ masked until it is on the event's list (Circle's `Wait` tests then blocks: a
  tick's `Set` in between would be lost; `Yield` keeps each task's own `DAIF`). `wake_word`
  sets the events of the waiters on that address. **Code on an app core changes words without
  any kapi**: `WordWaitTick`, called by `PeriodicTick` in the 100 Hz timer interrupt, reads every
  sleeping word through the identity map and wakes those whose value moved (nothing to do, and
  nothing read, while nobody sleeps). Spurious wakes are allowed (callers loop). A dying process
  unlinks its waiters first (`ThreadsFree` → `WordWaitsFree`: a killed task's record is on its
  stack, freed after the batch's handlers). Test: `/bin/futextest`.
- **Priority (v68)**: `thread_priority (tid, 1)` makes a thread "real time" (§5): picked first
  whenever it is ready, as long as it sleeps before its slice ends — for an audio pump.
- **The lists show a process once**: `list_windows`, `list_tasks` and `list_procs` skip the tasks
  that are not its main task; a thread's name is `<app>:<name>` (or `<app>:<tid>`), which the
  crash reports' task lists show. `kill` / `kill_pid` refuse the caller's own process.
- **User side**: `umm_malloc` / `umm_free` take a lock (`kapi_lock`: an exclusive swap, a
  `yield` while it is held — a spin on an app core, which makes no kapi call), and
  `user/libc/onyx_syscalls.c` defines newlib's retargetable locks (the 8 static ones and the 10
  `__retarget_lock_*` functions, which keeps newlib's `lock.o` out); a recursive lock's owner is
  the tid on core 0, the core on an app core. `errno` stays shared.

### Launch entry points

- **`LaunchApp(name)`**: builds `SD:apps/<name>.app/main`, reads it into RAM, creates a
  `CUserProcessTask` (without stdio). Exposed via `kapi_launch`.
- **`SpawnProcess(path, args, stdin, stdout, cwd)`**: creates a `CProcess` handle,
  increments the streams' refs, creates the task. Exposed via `kapi_spawn` (the terminal for
  pipes/redirections). `kapi_wait`/`kapi_proc_done` query the handle.
- **`ExecPath(path, args)`**: "fire-and-forget" (without stdio or handle; the task name
  is derived from the path). Exposed via `kapi_exec` (the file manager to
  open a document in an app, or launch a program).
- **Only ELF programs**: the kernel no longer knows BASIC (the former `BasicRedirect`). The
  formats a program runs (`.bas` / `.bax` → `SD:/bin/basic`) are chosen in user space:
  `SD:/etc/runners.ini` + `user/launch.h`, which starts the runner with
  **`kapi_exec_as (path, args, name)`** (v49, `ExecPath` with a name), so the process keeps
  the app's name (windows, `raise_app`, `list_windows`).

---

## 8. The kapi ABI table

Source: [`kernel/include/kern/kapi_abi.h`](../kernel/include/kern/kapi_abi.h),
[`kernel/sys/kapitable.cpp`](../kernel/sys/kapitable.cpp),
[`kernel/sys/kapi.cpp`](../kernel/sys/kapi.cpp).
App side: [`user/kapi.h`](../user/kapi.h).

### The mechanism

Rather than linking the apps against the **kernel symbol addresses** (which move on
each rebuild), the kernel publishes a **function-pointer table** (`struct
TKApiTable`) at a **fixed virtual address**:

- `KAPI_TABLE_VA = 14 GB` — stable "forever" (between the canvas at 12 GB and the stack at
  16 GB).
- The table is a static variable aligned to 64 KB (`s_Table` in `kapitable.cpp`),
  hence in the identity region (PA == kernel VA). `KApiTableInit()` fills all the
  pointers + the `version` field.
- Each `CAddressSpace` maps this page **read-only** at `KAPI_TABLE_VA` (cf.
  [§4](#4-memory-management-caddressspace)).
- App side, `kapi.h` defines `#define KT ((const struct TKApiTable *) KAPI_TABLE_VA)` and
  one inline function per entry (`kapi_create_window`, `kapi_open`, …) that does nothing but
  dereference the table.

**Consequence:** an application binary **embeds no kernel address** and
keeps working against any kernel that exposes the same ABI → no rebuild
of the apps when the kernel changes.

### The *append-only* contract

`KAPI_ABI_VERSION = 71`. The `TKApiTable` struct is **strictly append-only**: you
never remove or reorder a field; you add new ones **at the end** and you
increment the version. An old app only touches the prefix it knows → it
stays compatible. The history of additions is annotated in the file (v1 = `app_dir`,
v2 = `set_click_handler`, v3 = `opendir/readdir`, v4 = streams/spawn, … v16 =
`set_window_theme`, v17 = `chdir`/`getcwd`, v18 = `stdin_stream`/`stdout_stream`,
v19 = `klog_read`, v20 = `set_verbose`/`get_verbose`, v21 = the TCP socket calls,
v22 = `set_pointer_handler`, v23 = `meminfo`, v24 = `sbrk`, v25 = `reboot`,
v26 = `kbd_ready`, v27 = `set_keymap_data`, v28 = `get_chrome`/`draw_text_buf`
(user-side window chrome), v29 = compat break (kernel-drawn widget API removed, table
consolidated), v30 = `random` (hardware RNG), v33 = `ram_detail`, v34 =
`set_wheel_speed`/`get_wheel_speed`, v35 = shared surfaces (`surface_*`) + shell IPC
(`register_shell`, `shell_request`, `mailbox_send`/`mailbox_recv`), v36 =
`memset`/`memcpy`/`memmove`, v37 = `tcp_listen`/`tcp_accept`, v38 = `screen_grab`/`inject_pointer`/`inject_key`, v39 = `set_menu`/`get_menu`/`menu_command`, v40 = `ipc_register`/`ipc_lookup`, `clipboard_set`/`clipboard_get`, `set_window_alpha`, `shutdown`, v41 = `fullscreen_begin`/`present_fb`/`fullscreen_end`, v42 = `drag_begin`/`drag_data`, `get_modifiers`/`inject_modifiers`, v43 = `net_ping`/`net_resolve`/`net_info`, v44 = `vfs_register`/`vfs_next`/`vfs_req_data`/`vfs_reply`, v45 = `wlan_scan`, v46 = `sound_acquire`/`sound_release`/`sound_start`/`sound_stop`/`sound_write`/`sound_status`, v47 = `sound_instrument`, v48 = `key_held`/`inject_key_held`, v49 = `exec_as`, v50 = `pad_state`, v51 = `core_acquire`/`core_run`/`core_state`/`core_release`, v52 = `gpu_info`/`gpu_draw`, v53 = `gpu_texture`/`gpu_render`, v54 = `kapi_gpu_vertex3` second (added) colour `r2 g2 b2 a2` in place of `reserved`, `KAPI_GPU_B_ALPHATEST(t)`, v55 = `fullscreen_direct`, v56 = `win_list`/`win_read`/`win_raise`/`win_close`, v57 = `seek`, v58 = `code_alloc`, v59 = `fsize64`, v60 = `sound_volume`/`wlan_reconnect`, v61 = `gpu_program`/`gpu_render2`, v62 = `gpu_render3`, v63 = `gpu_vbuf`, v64 = `win_minimise`/`win_geometry`/`resize_window2` — the modernised CDE desktop's windows: the frame's metrics and title buttons shared with the apps (`KAPI_FRAME_*`), `GUI_EVENT_WINCTL`, `KAPI_WIN_MINIMISED`, the frames' rounded corners, `WIN_FLAG_ALPHA`), v65 = `desk`/`win_desk` — the
workspaces (virtual desktops: `KAPI_WIN_OFFDESK`, `KAPI_WIN_DESK`, `KAPI_DESK_MAX`), v66 =
`screen_set` — the resolution changed while running, `GUI_EVENT_DISPLAY_RESIZE`; a window as big as
the screen (was 1024 × 768 at most), v67 = `thread_create`/`thread_exit`/`thread_join`/`thread_self`,
`mutex_*`/`event_*`/`barrier_*`/`sync_close`, `post`/`pump_wait` — threads (§7), their
synchronisation objects, calls posted to the event pump; the scheduler's task list has no limit,
v68 = `sound_config`/`sound_map` — low-latency sound and the mapped PCM ring (§12),
`wait_word`/`wake_word` — a futex (§7), `thread_priority` — "real time" threads (§5),
`midi_read`/`midi_devices` — USB MIDI input (`struct kapi_midi_event`),
v69 = `screen_native` — the monitor's own resolution (its EDID), `set_timezone` — the time zone
while running, and the window flag `WIN_FLAG_FIXED` (§10.2) — for Setup, the first-run wizard,
v70 = `gpu_texture_rect` — a rectangle of a GPU texture replaced (no whole re-upload), the frame
flag `KAPI_GPU_F_ALPHA` (a target that keeps its alpha), the GPU's handles shared fairly by the
programs using it at once (`KAPI_GPU_MAX_TEXTURES` 1024 in all, `KAPI_GPU_MAX_TEXTURES_AS` 512 a
program; `gpu_vbuf` 32 in all, 8 a program) — for the GPU compositing service (`user/gpucomp`, §15),
v71 = `vol_info` — a volume's size, used and free bytes, type (`struct kapi_vol_info`, `KAPI_VOL_RAM`);
and the **RAM: volume** (§16): the file calls (`open` … `rename`, `save_file`, `file_in` / `file_out`,
`chdir`, `seek`, `fsize64`) reach a file system in memory on `RAM:` paths — no new call for that.

The callbacks' value (`gui_handler`: sender, event, value) is the type `gui_value`: `long` on Onyx
(64 bits: a pointer event packs its wheel, buttons and position there), `long long` where `long` has 32
bits -- the Windows build of the apps (`pc/Koton`, docs/03 *Koton for Windows*), whose table is filled
by a Win32 layer. On Onyx it is the same type as before: no ABI change, no new version.

### Categories of exposed functions

| Category | Examples |
|---|---|
| Windowing | `create_window(_ex)` (the canvas; **0** when the client area is bigger than the screen — `g_nScreenWidth/Height`; before v66, 1024 × 768 — or memory is short — an app must check it: at EL1, a null canvas drawn into is the kernel's memory at address 0), `resize_window` (the client size shown, ≤ the canvas made at creation; the frame — `OuterW/H`, the chrome copies' size — follows it, and the app redraws its chrome: `wk_decorate_window`), `move_window`, `present`, `exit`. Window flags: `WIN_FLAG_BORDERLESS`, `WIN_FLAG_BACKMOST` (desktop, bottom band), `WIN_FLAG_TOPMOST` (the menu bar: top band, never the active app nor the key target; at y=0 it reserves its smallest logical height — `CWindowManager::TopInset()` — so auto-placement and title-bar drags stay below it), `WIN_FLAG_TRANSPARENT` (client blitted with the magenta key), `WIN_FLAG_SYSTEM` (a shell component — menu bar, notifications, panel, app list: skipped by `list_windows`, so never in the taskbar; a plain flag bit, no ABI change). The z-order is three bands: backmost < normal < topmost (`Add`/`RaiseLocked` keep them). The **key target** is the frontmost non-topmost window; the **active app** (menus, chrome highlight uses the key target) is the frontmost window that is neither topmost, backmost nor borderless. |
| Menu bar (v39) | `set_menu(spec, handler)` stores the app's menu spec (≤ 2 KB; lines `M<title>`, `I<id>\t<label>\t<shortcut>`, `-`) + a `GUI_EVENT_MENU` (14) handler on its `CWindow`; `get_menu(buf, cap, title, tcap)` returns the **active app**'s spec + title and a serial that changes with the active window or its menu (0 = none); `menu_command(id)` queues `GUI_EVENT_MENU(id)` to the active window (`MENU_QUIT` = -1 → `RequestExit`, like the close box). Used by `menubar` + `wtk::Menu`. |
| Launch/management | `launch`, `toggle_app`, `raise_app`, `exec`, `kill`, `kill_pid` |
| Threads (v67) | `thread_create(fn, arg, stack_size, name)` → tid ≥ 2 (main: 1), −1 no memory, −2 too many (32); `thread_exit(code)` (the main thread: the process); `thread_join(tid, timeout_ms, &code)` → 0, −1 timeout, −2 none / joined already, −3 itself; `thread_self`. `mutex_create`/`mutex_lock(h, timeout)`/`mutex_unlock` (recursive), `event_create(manual, initial)`/`event_set`/`event_reset`/`event_wait(h, timeout)`, `barrier_create(count)`/`barrier_wait` (1 for the last one in), `sync_close` — handles, 256 per process; timeouts in ms, 0 = only try, `KAPI_WAIT_FOREVER`. `post(fn, ctx, value)` → queued for the pump (−1 full: 256); `pump_wait(timeout)` sleeps until an event / a post / the close box, pumps → what was pending. See §7. |
| Word waits, priority (v68) | `wait_word(addr, expected, timeout_ms)` sleeps while the 4-byte word `*addr == expected` → 0 (woken, or the value differs), 1 timeout (0 ms: only check), −1 bad address (unaligned, unmapped); `wake_word(addr)` → the sleepers woken. Keyed by the **physical** address (a word of a shared surface wakes across processes); the 100 Hz tick also reads every sleeping word and wakes those that changed — an app core's write needs no `wake_word` (≤ 10 ms). `thread_priority(tid 0 self / 1 main / ≥ 2, prio 0 / 1 / −1 ask)` → the previous one, −1 bad prio, −2 no such thread: a "real time" task is picked first when ready and a tick preempts an app for it, while it yields by itself (§5). See §7. |
| Enumeration | `list_apps`, `list_windows`, `list_tasks`, `list_procs`, `get_datetime` |
| Widgets | `add_button/label/checkbox/textbox/progress/slider/textarea/scrollbar/icon`, `widget_get/set_*` |
| Events | `pump_events`, `wait_for_exit`, `should_exit`, `set_key_handler`, `set_click_handler`, `set_pointer_handler` (full pointer stream, v22 — incl. `GUI_EVENT_PTR_WHEEL`, a signed scroll-notch delta in the `lValue` wheel field via `GUI_PTR_WHEEL`) |
| App-drawn text | `draw_text`, `font_width`, `font_height` |
| Files | `open/read/fsize/close`, `save_file`, `opendir/readdir/closedir`, `mkdir/remove/rename`, `chdir/getcwd` (current working directory, inherited by children). `fsize` (and `readdir`'s size) is clamped to 4 GB − 1; **`fsize64(h)` (v59)** gives an exFAT file's real 64-bit size. `rename` across two volumes fails (−1): the caller copies then deletes (FatFs' `f_rename` would otherwise rename inside the source volume). All of these (and the streams, `seek`, `fsize64`, `chdir`) work on **`RAM:`** paths too (v71, §16): the path is resolved first (`ResolvePath`: relative to a current folder on `RAM:` as well), then a `RAM:` path goes to `sys/ramfs.cpp`, a provider's (`FTP:`) to `sys/vfs.cpp`, the rest to FatFs; a handle is told apart by its address (the RAM volume's handle tables, then the provider's, else a FatFs `FIL` / `DIR`). |
| Volumes (v59) | FatFs volume strings (`FF_STR_VOLUME_ID`, docs/05 §13): **`SD:`** = the SD card's first FAT volume (partition 1, the boot FAT32 one, found as before; **`SD0:`** is an alias), **`SD1:` `SD2:` `SD3:`** = MBR partitions 2–4 (`FF_MULTI_PARTITION`) (FAT12/16/32 or **exFAT**, mounted at boot when present), `USB:`…, `FD:`, `NVME:` (declared, not mounted yet). `ResolvePath`: a volume prefix is upper-cased (`sd1:` → `SD1:`, `SD0:` → `SD:`), a path starting with `/` is relative to the **current directory's volume** root, anything else to the current directory. **`RAM:`** (v71) is the RAM volume (§16), not a FatFs one. The four SD volumes share one FatFs lock slot (`LockSlot`, `sys/fslock.cpp`): they are one card, one command at a time. |
| Streams/processes | `pipe`, `file_in/out`, `stream_read(_nb)/write/close/eof`, `stdin_read`, `stdout_write`, `spawn`, `wait`, `proc_done`, `get_args` |
| Modal dialogs | `message_box`, `file_open`, `file_save` |
| Desktop | `screen_size`, `set_wallpaper`, `wallpaper_generate`, `wallpaper_buffer`, `wallpaper_commit`, `cursor_pos` |
| Appearance/keyboard | `set_window_theme`, `set_keymap` (load a country map *by name* — deprecated: the kernel compiles in **no** maps, so it always returns 0; use `set_keymap_data`), `get_keymap`, `kbd_ready` (USB keyboard attached? v26 — informational; `keyb` no longer needs to poll it), `set_keymap_data` (load a layout from a `.kmap` blob, v27 — records it in a persistent snapshot and installs it on the keyboard whenever it attaches, so it needs no keyboard to be present; returns 1 once the blob is accepted, 0 only on a malformed blob. This removed the old boot race where `keyb` could time out waiting for USB enumeration and leave the keyboard map-less), `app_dir` |
| Logging / memory | `klog_read`, `set_verbose`, `get_verbose`, `meminfo` (total/free/app KB + page size, v23), `sbrk` (per-process heap, v24) |
| Networking (v21, v37) | `net_status` (live: 0 as soon as the Wi-Fi association drops — `CWPASupplicant::IsConnected ()` — not only before the first DHCP bind), `tcp_connect`, `tcp_send`, `tcp_recv`, `tcp_close`; server side (v37): `tcp_listen(port)` → listening handle (Circle `CSocket::Bind`+`Listen`; `-6` = port in use), `tcp_accept(h, ip, cap)` → **blocks** until a peer connects, returns a connected handle + the peer IP (Circle `Accept`). All handles share the 16-slot table in `sys/net.cpp` and are reclaimed when the owner dies. Used by `/bin/telnetd`. |
| Power (v25) | `reboot` (restart the machine — applies settings read only at boot, e.g. the WLAN config rewritten by *wpaconf*) |
| Remote screen (v38) | `screen_grab(dst, w, h)` — runs `CWindowManager::Composite` (windows, wallpaper, cursor; `bCountFrame = FALSE` so the watchdog's fps stays the real compositor's) straight into the caller's `w*h` 0x00RRGGBB buffer (`w`/`h` must be the screen size), or returns **2** without touching it when `g_nScreenGen` has not changed since the previous grab into the same buffer (vncd then skips the diff / encode); `inject_pointer(x, y, buttons, wheel)` → `OnMouse` (+ `OnMouseWheel`), `inject_key(keys)` → `OnKey` — the same paths as the USB mouse/keyboard. Used by `/bin/vncd`. |
| Windows as objects (v56) | For the window-level remote desktop (`/bin/rdpd`): `win_list(out, max)` fills `struct kapi_win_info` bottom to top (`CWindowManager::Snapshot`): `id` (`CWindow::Id`, a serial never reused), owner pid, the client area on the screen (x y w h; the full-screen window: the whole screen, `KAPI_WIN_FULLSCREEN`), `WIN_FLAG_*`, alpha, `gen` (`CWindow::Gen`, bumped by every `Damage` — drawn, moved, resized, raised — and by `present_fb`), `KAPI_WIN_KEYS` (the key target), the title, the frame (`ow oh` = `OuterW/H`, `il it` = the insets, 0 without a frame) and `chromeGen` (bumped by `get_chrome`, i.e. when the app redraws its frame, by a resize, and once more by the first present after that: the frame read again whole). The state also says `KAPI_WIN_MINIMISED` / `KAPI_WIN_OFFDESK` (not shown: rdpd sends a hidden window's pixels only when it is shown again) and the window's desk (v65). `win_read(id, part, x, y, w, h, dst, stride)`: a rectangle of the canvas (part 0; the full-screen window: its back buffer, or the screen itself when direct) or of a chrome copy (1 active, 2 inactive), clipped. `win_raise(id)` = `Raise`, `win_close(id)` = `RequestExit` (as the close box). The **desktop** is listed first as a pseudo-window, `KAPI_WIN_DESKTOP` (0xFFFFFFFF, backmost, screen-sized, `gen` = the wallpaper's counter + the backmost windows'): `win_read` of it (whole only) runs `CWindowManager::CompositeDesktop` (the wallpaper + the backmost windows) straight into the caller's buffer. A `CWindow` is never freed (see `Composite`), so an id found in the snapshot is safe to read. |
| Generated code (v58) | `code_alloc(size)` — zeroed memory the app may **write and execute** (a JIT's code: `gcemu`'s PowerPC → AArch64 translation), rounded up to 64 KB pages: `CAddressSpace::CodeAlloc` maps fresh owned frames (`MapNewPage`, freed at teardown) with `KPAGE_ATTR_APP_RWX` (EL1 RW, PXN = 0) in a per-process bump arena `USER_CODE_BASE` (14.25 GB) … `USER_CODE_END` (15 GB, 768 MB); 0 when full. Circle clears `SCTLR_EL1.WXN`, so a writable page may be executable. After writing code the app cleans the D-cache and invalidates the I-cache over it (`DC CVAU`, `DSB ISH`, `IC IVAU`, `DSB ISH`, `ISB`). |
| File seek (v57) | `seek(h, pos)` — the read position of a file opened with `open` (`f_lseek`); 0, or −1 (a VFS provider's file: not seekable). A file bigger than 4 MB gets its **cluster map** at its first seek (FatFs fast seek, `CREATE_LINKMAP`, docs/05 §11), freed by `close`: any position without walking the FAT. For `gcemu`'s disc images (1.4 GB, read on demand). |
| Shared surfaces (v35) | `surface_create(w, h)` → an id > 0: a page-aligned, physically contiguous 0x00RRGGBB buffer (`CSurface`, `kern/gui/surface.h`) owned by the caller; `surface_map(id)` maps the **same frames** into the caller (the id travels to another process over IPC) → their address; `surface_size`, `surface_present` (yields toward the process composing it), `surface_destroy`. A process mapping a surface it does not own becomes one of its **users** (v65, ≤ 4): the frames are freed once the owner let it go (`surface_destroy`, or its end: `DestroyByOwner`) **and** every user has ended too — no process keeps a mapping of freed frames, whichever ends first. The Control Panel's applets draw into their host's surface this way (`user/applet_proto.h`, the developer guide). |
| IPC services (v40) | `ipc_register(name)` makes the caller the service `name` (≤ 16 services; a name held by a live process is refused; freed when the owner dies — `IpcOnProcessGone`); `ipc_lookup(name)` → pid or 0. Messages use the per-process mailboxes (`mailbox_send`/`mailbox_recv`), now up to **512 bytes** (`MAILBOX_MSG_MAX`). The kernel itself can post: `IpcNotify(title, text)` → the `notify` service (e.g. "Network … connected"). |
| Clipboard (v40) | `clipboard_set(type, data, len)` / `clipboard_get(&type, buf, cap, &serial)`: one typed blob (≤ 64 KB) held by the kernel so it outlives the app that copied (`CLIP_TEXT` 1, `CLIP_FILES` 2, `CLIP_FILES_CUT` 3 — paths `\n`-separated); the serial bumps on every set. Since the shared clipboard (2026-10-01: the service `clipd`, `user/clipboard.h`, `docs/clipboard/README.md`) it is only a **fallback**: `clipboard.h` writes the kernel's copy too, and reads it when clipd cannot be reached; the history (10 items, any format) is clipd's, in its own memory. |
| Window opacity / session (v40) | `set_window_alpha(0..255)` on the caller's window: `CWindow::DrawTo` blends chrome + client over what is below (`BlendRect`, magenta-keyed if `TRANSPARENT`; 0 = not drawn) — used for fades. `shutdown(mode)`: `f_mount(0)` unmounts/flushes the SD card, then `reboot()` (mode 1) or ACT LED off + `halt()` (mode 0). |
| Full-screen apps (v41) | `fullscreen_begin(&w, &h)` maps a kernel-owned, screen-sized back buffer (`CWindowManager::EnsureFullscreenBuffer`, 64 KB-aligned) at `USER_FULLSCREEN_CANVAS` (15 GB) and makes the caller's window (created if missing, moved to 0,0) the **full-screen window**: the compositor task skips its frames, `OnMouse`/`OnMouseWheel` send the whole pointer stream to it in screen coordinates and it is the key target. `present_fb()` copies the buffer into the displayed `C2DGraphics` buffer + `UpdateDisplay()`, then yields. `fullscreen_end()` — or the window's removal when the app exits — gives the desktop back. The buffer is made at the first `fullscreen_begin` and made again, bigger, when the screen has grown since (`screen_set`, v66; the old one is left allocated, as the wallpaper's: it may still be mapped) — before, it kept its first size and a full-screen app after a change to a larger resolution wrote past its end, over the kernel heap. `screen_grab` (VNC) returns the full-screen buffer meanwhile. **v55** `fullscreen_direct(&w, &h, &stride)` (after `fullscreen_begin`): maps the framebuffer the display scans out (`CBcmFrameBuffer`, the `C2DGraphics` display, 32 bpp) at `USER_FULLSCREEN_SCREEN` (15.5 GB), normal **uncached** (`KPAGE_ATTR_APP_SCREEN`); `present_fb` then copies nothing (it only yields); `screen_grab` (VNC) reads the screen through the same uncached mapping in the grabber's address space (the kernel's own map of the framebuffer is Device memory, slow to read). What is drawn there shows at once (no double buffering: tearing is possible); the GPU renders there directly (below 1 GB, contiguous), so a full-screen GPU frame costs no copy at all. Returns 0 when not possible (keep the back buffer). |
| Drag & drop (v42) | `drag_begin(type, data, len, label)` — only while the left button is held — copies the payload (≤ 4 KB: 1 text, 2 `\n`-separated paths) into a kapi-side buffer and calls `CWindowManager::DragBegin(src, label)`. While the session lasts, `OnMouse` sends `GUI_EVENT_DRAG_OVER` (16) to the window under the cursor (`DND_F_LEAVE` when it leaves), and `Composite` draws a label badge next to the cursor (a **+** when Ctrl is held). At the left-button release, `DndFinishLocked` sends `GUI_EVENT_DROP` (15) to the window under the cursor — `(flags << 32) \| (x << 16) \| y`, client coords, `DND_F_COPY` if Ctrl — and `GUI_EVENT_DRAG_DONE` (17) to the source: `(flags << 32) \| target pid` (`CWindow::OwnerPid`, set by `CreateWindow`), flags `DND_F_COPY` / `DND_F_CANCEL` (Esc, via `OnKey`) / `DND_F_DESKTOP` (no window or a backmost one). All three go to the pointer handler; the normal pointer stream still reaches the source (its capture), so its widgets see the button go up. The target reads the payload with `drag_data(&type, buf, cap)`. A window removed mid-drag ends it. **Modifiers**: `get_modifiers()` = `MOD_CTRL` 1 / `MOD_SHIFT` 2 / `MOD_ALT` 4 — from the USB keyboard's raw report (`RegisterKeyStatusHandlerRaw` in **mixed mode**, so the cooked key path is unchanged) or `inject_modifiers()` (vncd, from the RFB Control/Shift/Alt keysyms). **Per key event**: `OnKey` stores the modifiers in each `GUIEvent` (`nMods` = the global state OR the xterm parameter of `ESC[1;<m>X` / `ESC[n;<m>~`, m − 1 = Shift 1 + Alt 2 + Ctrl 4, which Circle's keymap and vncd send for navigation keys); `kapi_pump_events` sets `CWindow::m_nKeyEventMods` around the app's key handler, and `get_modifiers()` returns it while the handler runs — so Shift+arrow selects even when the live state is late or clobbered (VNC, a second keyboard). |
| Network tools (v43) | `net_ping(host, seq, timeout_ms, ip, cap)` resolves the host (`CDNSClient` unless a dotted quad), builds an ICMP echo request (id `0x4F4E`, 32 data bytes, `CChecksumCalculator`) and sends it with `CNetworkLayer::Send(…, IPPROTO_ICMP)`; the reply is read from Circle's **secondary ICMP queue** (`EnableReceiveICMP`, enabled only while a ping is in flight, `ReceiveICMP`), matching type 0 + id + seq + sender, yielding while it waits → RTT in µs or `-1` down / `-3` unresolved / `-4` timeout / `-5` send failed. `net_resolve` = DNS only. `net_info` = a text dump for `netstat`: `up`, `hostname`, `ip`, `mask`, `gateway`, `dns`, `dhcp` lines (`CNetConfig`), then one `tcp <handle> listen\|conn <local port> <remote ip> <pid>` per socket slot (`TSocketSlot.bListen`). Tools: `/bin/ping`, `nslookup`, `netstat`, `whois` (the latter is plain TCP port 43). |
| User-space file systems (v44) | `sys/vfs.cpp`, `kern/vfs.h` — FUSE-like. A **provider** app calls `vfs_register(prefix)` (`/bin/ftpfs`: `FTP:`, `FTPS:`). `kapi_open`/`read`/`fsize`/`close`, `save_file`, `opendir`/`readdir`/`closedir`, `mkdir`/`remove`/`rename` on a path with a registered prefix (`VfsHandles`) become **requests** (`VfsCall`): the calling task fills a slot (op, path, path2, a0–a2, a **kernel copy** of the payload), sets the provider's `CSynchronizationEvent` and waits on the slot's own event (200 ms re-checks: provider alive via `IpcPidAlive`, 120 s timeout). The provider takes it with `vfs_next` (blocking = up to 0.5 s, so it can also poll its mailbox), reads the payload with `vfs_req_data`, answers with `vfs_reply(id, status, data, len)` (copied into a kernel buffer, then into the caller's). Ops: `OPEN` (→ fid + size), `READ` (fid, offset, ≤ 64 KB), `CLOSE`, `LIST` (packed `u32 size, u8 is_dir, name\0` entries), `SAVE`, `MKDIR`, `REMOVE`, `RENAME`. Provider-backed handles live in static tables (`s_File`, `s_Dir`), so the file kapis tell them from FatFs `FIL`/`DIR` by address. `FTP:`/`FTPS:` are **auto-started**: the first use execs `SD:/bin/ftpfs` and waits up to 5 s for it to register. A dying provider is dropped and its pending requests fail (`VfsOnProcessGone`, from `IpcOnProcessGone`). Streams (`kapi_file_in`: `cat`, redirections) go to FatFs (and to the RAM volume on `RAM:`, §16), not to a provider. |
| Volumes' room, RAM: (v71) | `vol_info(path, out)` → 0 and `struct kapi_vol_info { total, free, used; files, dirs; flags; type[12] }` for the volume of `path` (`"SD:"`, `"SD1:/roms"`, `"RAM:"`, a relative path: the current folder's), −1 no such volume. A FatFs volume: `f_getfree` (its free clusters — FSINFO on FAT32, else counted once by FatFs: the first call on a big card can take a moment), `type` `FAT12`/`FAT16`/`FAT32`/`exFAT`. **`RAM:`** (§16): its size, the pages its files take, what can still be written (also bounded by the free page memory less the reserve), its files and folders, `flags` `KAPI_VOL_RAM` (lost at a restart), `type` `RAM`. `/bin/df` prints it; `/bin/ramtest` uses it to check that the memory comes back. |
| Wi-Fi scan (v45) | `wlan_scan(out, max)` → `struct kapi_wlan_ap` (ssid, bssid, security `WLAN_SEC_OPEN`/`WEP`/`WPA`/`WPA2`, channel, freq, level dBm, connected), strongest first, one per BSSID. `NetWlanScan` in `sys/net.cpp` — **no Circle patch**: it drives the BCM4343 firmware's *escan* through `CBcm4343Device::Control ("escan 5")`, collects `ReceiveScanResult` messages for ~3.5 s (the firmware's `brcmf_escan_result_le` layout, as in hostap's `driver_circle.cpp`), then `escan 0`. Security from the capability privacy bit + the RSN (48) / WPA vendor (221) IEs; `connected` = the BSSID `GetBSSID()` reports while `CWPASupplicant::IsConnected()`. wpa_supplicant reads the same result queue for its own scans: while it is still looking for its network, a scan here may take its results (it scans again). Used by `/bin/wifiscan` and `wpaconf`. |
| Master volume (v60) | `sound_volume(volume 0..10, mute 0/1)` (−1 keeps a value) → the volume `| 0x100` if muted. Applied in `COnyxSoundDevice::GetChunk` to everything played (voices + stream), on a squared curve (`s_Gain`, the ear hears the steps evenly). Not kept by the kernel: the menu bar applies `SD:/etc/sound.ini` at start (`user/volume.h`). |
| Wi-Fi join (v60) | `wlan_reconnect()` — `NetWlanReconnect` (a core-3 request with `netcore=1`): wpa_supplicant's SIGHUP handler, caught at link time (`--wrap=eloop_register_signal_reconfig`, `kernel/Makefile`; docs/05 §14) and run from its own event loop (a 0 s eloop timeout) — deauthenticate, read `SD:/etc/wpa_supplicant.conf` again, rescan, join the highest priority network in range — then `CDHCPClient::Restart ()` (a new lease: another network). 0 asked, −1 no Wi-Fi running. Used by the Wi-Fi menu (`wifimenu`). |
| Low-latency sound (v68) | For the sound owner: `sound_config(chunk_frames 64..1024 (0: 1024), ahead 1..4 (0: 4))` → the latency now in frames, (ahead + 1) × chunk (−1 not the owner); `sound_map()` → the **mapped PCM ring** (`struct kapi_sound_ring`: one 64 KB page, 8192 frames, `wr` moved by the app after its frames, `rd` by the kernel, `dry` underruns, the chunk / ahead now), mixed with the voices and the stream until the owner releases the output — plain memory, so an **app core** fills it (`kapi_sound_ring_write`). Both are back to the defaults / off at `sound_release` or the owner's death. See §12. Test: `/bin/ringtest`. |
| Sound (v46) | `sound_acquire` (1 ok / 0 busy / −1 no audio: the caller becomes the owner; the output starts on first use), `sound_release`, `sound_start(voice 0..15, milliHz, wave SOUND_SQUARE/SINE/TRIANGLE/SAW/NOISE, volume 0..255)` (plays until stopped), `sound_stop(voice or -1)`, `sound_write(s16 stereo frames, n)` → frames taken (PCM ring, non-blocking), `sound_status(&rate, &free, &owner)`. Non-owners get −1; the owner's exit silences it. See §12. |
| Run as (v49) | `exec_as(path, args, name)` → `ExecPath` with the process named `name` instead of after the path (1 = started). User space runs a format's program this way (`launch.h`: `SD:/bin/basic` for an app's `main.bax` is named after the app). |
| Monitor, time zone (v69) | `screen_native(&w, &h)` → 1 and the monitor's own resolution, 0 unknown: the EDID's first block (the firmware's `PROPTAG_GET_EDID_BLOCK`, Circle's `CBcmPropertyTags`), its header checked, the **first detailed timing descriptor** (bytes 54..71, the preferred timing) read — the active pixels, 8 low bits and 4 high ones of a shared byte, horizontally (bytes 2, 4) then vertically (5, 7); 0 when it is a display descriptor or too small (no monitor, an analog adapter). `set_timezone(minutes)` → 1: the local time's offset from UTC now (−720 .. 840, `CTimer::SetTimeZone`: `get_datetime`, the menu bar's clock), 0 out of range — `system.ini`'s `timezone=` sets it at boot. Setup (the first-run wizard) marks the monitor's size "Best" and changes the zone as it is picked. |
| USB MIDI (v68) | `midi_read(ev, max)` → up to `max` `struct kapi_midi_event` (`time_us` — the kernel's µs clock, `CTimer::GetClockTicks`, which `kapi_clock_us` reads user side, on an app core too —, `cable`, `status`, `data1`, `data2`, `device` (its `umidiN`), `length` 1..3), oldest first, never waits; −1 bad arguments. One queue for the system (256 events, the newest dropped when full). `midi_devices()` → attached. Circle's USB device factory makes a `CUSBMIDIHostDevice` for every class-compliant MIDI interface (`int1-3-0`) and its `CUSBMIDIDevice` names itself `umidiN`; the input task finds `umidi1..4` every 100 ms (a hot plug too), registers its packet handler (`MidiPacket`: at USB-completion time, stamps the packet and queues it under an IRQ spin lock) and a removed handler that frees the slot. No Circle change. Test: `/bin/miditest`. |
| Gamepads (v50) | `pad_state(index, out)` → 1 and `struct kapi_pad` filled for USB gamepad 0..3 (`KAPI_PAD_MAX`), else 0: `vid`/`pid`, `props` (Circle's `TGamePadProperty`, bit 0 = a known mapping), `focus` (the caller's window has the keyboard), `seq` (reports received), `nbuttons`/`buttons`, `naxes`/`axes[16]` (value, min, max), `nhats`/`hats[6]` (0..7 = N..NW). Raw state: for pads Circle knows (Xbox 360 / One, PS3 / PS4, Switch Pro) `buttons` are its `TGamePadButton` bits, for other HID pads the report's own. The input task finds `upad1..4` (Circle's names) every 100 ms, registers a status handler that copies each report into a slot under a sequence count (odd while writing: the handler runs at USB-completion time), and a removed handler that frees the slot. The mapping to one button set is user space (`user/gamepad.h`, `SD:/etc/gamepad.ini`). |
| App cores (v51) | `core_acquire()` → 2 or 3 (a free app core, now the caller's) or −1; `core_run(core, fn, arg, stack_top)` → 0, or −1 (not yours / still running / `fn` or the stack not a user address): the core calls `fn (arg)` in the caller's address space on the given stack (16-byte aligned, the caller's memory); `core_state(core)` → `KAPI_CORE_IDLE` (0: `fn` returned), `KAPI_CORE_RUNNING` (1), `KAPI_CORE_FAULT` (−2: `fn` faulted and was stopped, logged to kmsg) or `KAPI_CORE_NOTYOURS` (−1); `core_release(core)` stops `fn` if it runs and frees the core (done at the app's exit anyway). `fn` makes **no kapi call and no allocation**. See §14. |
| GPU (v52) | `gpu_info(buf, cap)` → 1 (the V3D is up; `buf` = "V3D 4.2 (1 core)") or 0 (`buf` says why); the first call brings the GPU up. `gpu_draw(v, n, clear, pixels, w, h, stride)`: `n` vertices `struct kapi_gpu_vertex { float x, y, z; u8 r, g, b, a; }` (a triangle list; normalized device coordinates, y up, z −1 near … 1 far; depth test *less*, both faces; colours interpolated) rendered by the GPU into `pixels` (0x00RRGGBB, `w` × `h` ≤ 2048, `stride` pixels a row) after clearing it to `clear` (0xRRGGBB) → 0, −1 no GPU, −2 bad arguments / too many vertices (`KAPI_GPU_MAX_VERTS` = 196608), −3 the GPU did not finish (it is then left off). See §15. |
| GPU (v53) | `gpu_texture(handle, pixels, w, h, stride)`: a texture of `w` × `h` (≤ 2048) pixels 0xAARRGGBB; `handle` < 0 makes one (≤ 256 in all; v70: ≤ 1024 in all, ≤ 512 a program), ≥ 0 replaces its pixels, `pixels` = 0 frees it → the handle, −1 no GPU, −2 bad arguments, −4 no memory / no free handle; a program's textures are freed when it ends. `gpu_render(f, v, nv, b, nb)`: one frame into `struct kapi_gpu_frame { pixels, w, h, stride, clear, flags }` (`KAPI_GPU_F_KEEP`: drawn over the pixels instead of clearing them) of `nv` vertices `struct kapi_gpu_vertex3 { float x, y, z, w, s, t; u8 r, g, b, a; u8 r2, g2, b2, a2; }` (v54: the second colour is added after the texel × colour product and clamped to 1 — 0 for the v53 behaviour) in `nb` (≤ 4096) batches `struct kapi_gpu_batch { first, count, texture, flags, float matrix[16]; }`: each batch draws its triangles with its own matrix (row by row, clip = M·(x y z w), transformed by the GPU, which divides by w and clips; `KAPI_GPU_B_NOMATRIX` = identity), texture (−1: colour only, else texel × colour; `B_LINEAR`, `B_WRAP_S/T(REPEAT, CLAMP, MIRROR)`), depth test (`B_ZFUNC`: less by default, … always) and writes (`B_NOZWRITE`), culling (`B_CULL_BACK/FRONT`, front = counter-clockwise, y up), alpha test (v54: `B_ALPHATEST(t)`, fragments whose alpha < t / 255 are discarded) and blending (`B_BLEND(ALPHA, ADD, MUL, PREMUL)`) → 0, −1, −2, −3 as `gpu_draw`. The kernel gives the V3D only well-formed triangles: each one is transformed by its batch's matrix on the CPU and clipped against the near plane (z ≥ −w, w > 0) and a guard band 4 × the screen (`kern/v3d_clip.h`, the batches then NOMATRIX; test `tools/tests/run_v3d_clip_test.sh`) — Ocarina of Time's triangles crossing the eye plane, projected ~50 000 screens away, wedged the GPU and froze the whole Pi. See §15. |
| GPU (v61) | `gpu_program(handle, p)`: the app's own shaders — `struct kapi_gpu_program { vs, cs, fs; nvs, ncs, nfs; inputs, csInputs, csOutputs, varyings, flags }`, three V3D 4.2 QPU programs (≤ 4096 instructions each; built at run time by `user/v3d/qpu.h`) copied into GPU memory; a vertex is `inputs` floats (4..64, the clip-space x y z w first; the coordinate shader reads the first `csInputs`, writes `csOutputs` ≥ 6: Xc Yc Zc Wc Xs Ys), the vertex shader writes Xs Ys Zs 1/Wc then `varyings` (≤ 64); flags `KAPI_GPU_P_FS_4WAY` (4 threads, else 2), `P_FS_FINAL` (no thread switch — unreliable on wide targets, see §15), `P_FS_ZWRITE`; `handle` < 0 makes one (≤ 256), ≥ 0 replaces it, `p` = 0 frees it → the handle, −1, −2, −4 as `gpu_texture`; freed when the program ends. `gpu_render2(f, v, nv, stride, b, nb, uni, nuni)`: one frame (as `gpu_render`) of `nv` vertices of `stride` floats in `nb` batches `struct kapi_gpu_batch2 { first, count, program, flags, blend, wmask, scissor[4], vsUni, vsNUni, csUni, csNUni, fsUni, fsNUni, tex[8], texFlags[8], texUni[8] }`: each with its program, its uniforms (three ranges of `uni[nuni]`, ≤ 2^20 words), up to 8 textures — the kernel writes each one's TMU words p0 (texture state, 16-bit float RG / BA) and p1 (a sampler from `texFlags`: `B_LINEAR`, `B_WRAP_S/T`) at `fsUni + texUni[i]` —, depth / cull flags (`B_ZFUNC`, `B_NOZWRITE`, `B_CULL_*`), blending `KAPI_GPU_BLEND2(cSrc, cDst, aSrc, aDst, cEq, aEq)` (the V3D factors and equations), a colour write mask (`wmask`: the channels not written) and a scissor (x y w h in the target, top-left origin; w ≤ 0: none) → 0, −1, −2, −3. The triangles are clipped on the CPU as for `gpu_render` (`V3DClipTriangleN`: every float of the vertex interpolated). See §15. |
| GPU (v62) | `gpu_render3(f, v, nfloats, b, nb, uni, nuni, view)`: `gpu_render2` with each batch's vertices where the app keeps them — `struct kapi_gpu_batch3 { struct kapi_gpu_batch2 b; unsigned off, stride; }`: `b.count` vertices of `stride` floats (≥ the program's inputs, ≤ 64) from float `off` of `v[nfloats]` (`b.first` unused) — and their x / y framed by the kernel on the way, `x' = view[0] x + view[1] w`, `y' = view[2] y + view[3] w` (`view` 0: as they are): no common array for the app to make first → as `gpu_render2`. gcemu's TEV renderer gives it its recorder's frame as it is. See §15. |
| GPU (v63) | `gpu_vbuf(bytes)`: memory the GPU reads too — low, physically contiguous, mapped into the program (the surface arena); up to 8 a program (v70: 32 in all; 8 in all before), 64 MB each, freed when it ends → its address, 0 none. `gpu_render3` with `v[nfloats]` inside one draws the vertices **where they are**: their x / y framed **in place** (the program draws the same vertices again with `view` 0), the triangles inside every plane drawn as runs of them, the others clipped into the buffer's end past `nfloats` (keep room there) — nothing copied. |
| GPU (v70) | `gpu_texture_rect(handle, x, y, w, h, pixels, stride)`: the rectangle `x, y, w × h` of texture `handle` (the caller's) replaced by `pixels` (0xAARRGGBB, `stride` pixels a row), the rest kept → 0, −1 no GPU, −2 bad arguments (not the caller's handle, the rectangle not inside the texture). The texels are stored where the texture unit reads them for the texture's size (`StoreRect`, `kern/v3d_tiling.h`: a utile row — 4 texels, 16 contiguous bytes in every layout — per offset computed) and only the span of bytes written is cleaned from the CPU caches; it waits for a frame in flight, as `gpu_texture`. `KAPI_GPU_F_ALPHA` (a frame's flag, `gpu_render` / `2` / `3`): the target is 0xAARRGGBB premultiplied — its alpha loaded with `KAPI_GPU_F_KEEP` (not forced to 1), blended, written (the colour write mask no longer spares it in the direct mode), stored, and cleared to `clear`'s top byte; without it nothing changes (0x00RRGGBB: the direct mode leaves the top byte, the copy mode writes 0). Handles: `gpu_texture` gives 1024 in all (256 before), 512 at most a program (−4 past that), so an emulator's texture cache and the browser's layers fit side by side; `gpu_vbuf` 32 blocks in all, 8 a program (8 in all before). See §15 *Sharing the GPU* and *The compositing service*. |
| Windows (v64) | The modernised CDE desktop's windows. `win_minimise(id)`: window `id` (0: the caller's) minimised — not drawn, not hit, never active nor the keys' target — until `win_raise` / `raise_app` brings it back (`KAPI_WIN_MINIMISED` in `win_list`'s state) → 0 / −1. `win_geometry(out)`: `struct kapi_win_geom { x, y, w, h; cw, ch; ax, ay, aw, ah; state }` — the caller's whole window (frame included), its client size, the **work area** (the screen less the menu bar at the top and the topmost windows standing on the bottom edge: the dock) → 0 / −1. `resize_window2(w, h, &stride)`: as `resize_window`, but the canvas and the frame's copies **grow** past their first size when needed (new memory at the same addresses; their pixels are lost: redraw, `get_chrome` again) → the canvas and its stride, 0 (no memory: the size kept). With it: the frame's metrics `KAPI_FRAME_TITLE_H` 28, `KAPI_FRAME_BORDER` 4, `KAPI_FRAME_RADIUS` 8 (the chrome copies' top byte: a transparency, heeded in the corner squares), the title buttons' places `KAPI_FRAME_BTN_W/H/Y/EDGE/STEP` (the window menu at the left; close, maximise, minimise from the right: `KAPI_FRAME_MENU/CLOSE/MAXIMISE/MINIMISE`); `GUI_EVENT_WINCTL` (18: the window menu, maximise — for the app); `WIN_FLAG_ALPHA` (32: a borderless window's pixels carry their transparency). See §10.2. |
| Screen size (v66) | `screen_set(w, h)`: the screen's resolution **now** (640 × 480 .. 2560 × 1600, `w` even; `ScreenResizeRequest`, kernel.cpp). The compositor does it between two frames (the display DMA is idle — each present waits for its end): `C2DGraphics::Resize` (Circle's: the frame buffer asked of the firmware again at that size, the drawing buffer made again), the old size back if the firmware refuses; then `CWindowManager::OnScreenResized`: the cursor and every window kept on the screen (moved in when past the right / bottom edge; a window parked at a negative place is left there), the app-written wallpaper dropped (the next `wallpaper_buffer` is a new buffer of the new size — the old one is left allocated: an app may still have it mapped), and **`GUI_EVENT_DISPLAY_RESIZE`** (19, `lValue = w << 16 \| h`, `GUI_DISPLAY_W/H`) to every window's pointer handler. The caller waits for it → 0; −1 out of bounds; −2 not now (a full-screen app owns the display — its mapping of the frame buffer would be stale —, the debug console, another change under way); −3 the firmware refused it (the old size kept). `screen_grab` refuses the old size (vncd / rdpd see it and take the new one). Not kept across a reboot: `cmdline.txt`'s `width=` / `height=` are (the Display applet writes both). See §10.2. |
| Workspaces (v65) | The virtual desktops. `desk(set, count)`: `set` ≥ 0 shows desk `set`, `count` > 0 sets how many there are (1 .. `KAPI_DESK_MAX` = 8; the dock keeps 1–6; the windows of the desks dropped go onto the last one); −1 / 0 keep them → the current desk `| count << 8 | gen << 16` (`gen`: bumped at every change — a window moved, a desk shown; `KAPI_DESK_CUR/COUNT/GEN` in `user/kapi.h`, where an older kernel answers `1 << 8`: one desk). `win_desk(id, n)`: window `id` (0: the caller's) to desk `n` (−1: every desk; −2: only asked) → its desk (−1: every desk), −3 no such window; a topmost or backmost window stays on every desk. A window opens on the current desk (the topmost, backmost and system ones on all: desk −1); the others are hidden (`OffDesk`: not drawn, not hit, never active nor the keys' target, as minimised) and flagged `KAPI_WIN_OFFDESK` in `win_list`'s state, with the desk + 1 in its bits 8–15 (`KAPI_WIN_DESK(state)`: −1 all). `list_windows` and `raise_app` see the current desk's windows only (an app on another desk: `raise_app` fails, its launcher starts a new one here); `win_raise` of a window on another desk shows that desk. **Ctrl+Alt+Left / Right** show the previous / next desk (with **Shift** the active window goes along); not while an app has the full screen. See §10.2. |
| Held keys (v48) | `key_held(key)` → 1 while the key is held **and** the caller's window has the keyboard (`KeyTargetLocked`), else 0 — for games, since key events only report presses. Keys: `KEY_UP/DOWN/LEFT/RIGHT`, `KEY_ENTER`, 27, `' '`, `'a'..'z'` (the **US position** of the key), `'0'..'9'`. The WM keeps two bitsets over the logical codes: the USB one, rebuilt from every raw report (`KeyRawStub` → `SetUsbHeld`, HID usage → key), and the injected one (`inject_key_held(key, down)`, from vncd's RFB key down / up); `KeyHeld` ORs them. |
| FM (v47) | `sound_instrument(voice, const struct kapi_fm_instrument *)` — a 2-operator FM instrument (OPL2 style, `struct kapi_fm_op op[2]` = modulator / carrier: `mult`, `level`, `ksl`, `attack`, `decay`, `sustain`, `release`, `wave`, `flags` FM_SUSTAINED / FM_TREMOLO / FM_VIBRATO / FM_KSR; `feedback`, `connection`) for that voice; then `sound_start(voice, milliHz, SOUND_FM, volume)`. Owner only. See §12. |
| Memory primitives (v36) | `memset`, `memcpy`, `memmove` — Circle's kernel implementations (general registers only, so callable from any app). `user/kapi.h` wraps them as weak **`kapi_memset`/`kapi_memcpy`/`kapi_memmove`** symbols, and the freestanding app Makefiles alias the C names onto them (`-Wl,--defsym,memset=kapi_memset`, …): GCC may emit these calls on its own (array/struct initialization, copies) even with `-ffreestanding`, and freestanding apps have no libc. Newlib programs keep newlib's own. |
| Crypto (v30) | `random` (fill a buffer from the Pi's **hardware RNG**, Circle `CBcmRandomNumberGenerator`; for cryptographic seeding — the TLS entropy source in `user/tls/onyx_tls.hpp` feeds mbedTLS's CTR_DRBG from it) |

All the functions **run in the context of the calling app** (its page
table + its stack are active; the arguments are plain pointers in the current
space — no `copy_from_user`). Some are **modal and synchronous**:
`message_box`, `file_open`, `file_save` **block (yield in a loop)** the calling
app until the response, while the compositor and the other apps keep running.

> **Historical note.** `ARCHITECTURE.md` §11 describes an earlier approach where the build
> emitted a `user/kernel_syms.ld` (`kapi_x = 0xADDR;`) and the apps were linked against
> those addresses. This approach is **no longer used**: the fixed-address table replaced
> it. The apps no longer depend on the kernel build.

---

## 9. Stream / stdio subsystem

Source: [`kernel/sys/stream.cpp`](../kernel/sys/stream.cpp),
[`kernel/include/kern/stream.h`](../kernel/include/kern/stream.h).

This is what makes the terminal's **pipes and redirections** and inter-process
communication possible.

### Object model

Abstract base class `CStream` with reference counting (`AddRef`/`Release`;
destroyed when the last reference drops). Three implementations:

| Class | Role |
|---|---|
| `CPipeStream` | in-memory FIFO (ring buffer ~8 KB) between two tasks |
| `CFileStream` | FatFs file presented as a stream (read, write/truncate, append) |

Virtual methods: `Read` (cooperative blocking: yields until ≥1 byte or EOF;
0 = EOF), `ReadNonBlocking` (`>0` / `0`=EOF / `-1`=would block), `Write` (may block if
full), `CloseWrite` (signals "no more writing" → readers see EOF).

- **`CPipeStream`**: ring buffer `head`/`tail`. `Read` yields while empty and
  the write end is not closed; `Write` yields while full. Cooperative → no lock.
- **`CFileStream`**: wraps a FatFs `FIL`; modes 0=read, 1=write+truncate,
  2=append.

### Processes and stdio

- `CAddressSpace` owns `m_pStdin`/`m_pStdout` (set by `SpawnProcess`).
- `kapi_stdin_read`/`kapi_stdout_write` read/write these streams (or log if
  absent).
- `kapi_spawn(path, args, in, out)` → `SpawnProcess`: the child task takes a
  reference on `in`/`out`. `kapi_wait` blocks (cooperatively) on `CProcess::bDone` then
  returns the exit code. When the child's address space is destroyed, stdout gets
  `CloseWrite` → the reader (the terminal) sees EOF.

The terminal thus chains the `stdout` of one stage to the `stdin` of the next via
`CPipeStream`s, and reads the final output non-blocking. App-side details in the
[developer guide](03-DEVELOPER-GUIDE.md) and the [user guide](04-USER-GUIDE.md).

---

## 10. Graphics subsystem (GUI)

Source: `kernel/gui/{gimage,window,skin,dialog}.cpp` + headers. Rendering core ported from
the author's FreeBASIC `SimpleOS`.

### 10.1 `GImage` — software rendering engine

- **Pixel format: 32-bit `0x00RRGGBB`** (alpha byte unused).
- **Transparency color: magenta `0xFF00FF`** (transparent-blit key).
- A `GImage` owns its buffer (`SetSize`) or **wraps** one (`Wrap`, e.g. the framebuffer's
  back buffer or a window's canvas).
- Primitives: `Clear`, `SetPixel`/`GetPixel`, `DrawLine` (Bresenham), `DrawRectangle`,
  `FillRectangle`, blits `PutOtherRaw`/`PutOther` (with key) / `PutOtherPart` (sub-rect,
  used by the 9-slices skin engine).
- **Text**: Circle's bitmap font (`CCharGenerator`). `FontWidth/Height`, `DrawChar`
  (transparent background), `DrawText`.
- **BMP**: `LoadBMP` (24 bpp uncompressed, BGR→RGB, handles top-down/bottom-up).

### 10.2 Windows and compositor

- **`CWindow`**: position, logical size, title, flags (`WIN_FLAG_BORDERLESS`; `BACKMOST`: the
  desktop's band; `TOPMOST`: the menu bar, the dock — above every window, never active, never the
  keys; `TRANSPARENT`: the magenta key; `SYSTEM`: not listed as an open app; `ALPHA` (v64): the
  canvas's top byte is a transparency; `FIXED` (v69): the user cannot move it — no drag, no
  double-click maximise, `HitTitleButton` finds no button (wtk draws none: `WK_WIN_FIXED`) — and
  `OnScreenResized` centres it again at a new resolution instead of only moving it in: Setup's), a `GImage` **canvas allocated 64 KB-aligned and
  physically contiguous** (mapped into the app at `USER_WINDOW_CANVAS` = 12 GB — the app draws
  directly, with no per-pixel call), the frame's two copies (active, inactive: mapped at
  `USER_WINDOW_CHROME` / `_INACTIVE`, drawn by the app — kapi v28 `get_chrome`; wtk:
  `wk_decorate_window`), an event queue (spinlock-protected ring), the app's handlers, its menu.
- **The frame** (v64, the modernised CDE): a 28 px title bar and 4 px borders (`WIN_TITLEBAR_H` /
  `WIN_BORDER` = `KAPI_FRAME_TITLE_H` / `KAPI_FRAME_BORDER`, shared with the apps in `kapi_abi.h`),
  rounded corners (radius `KAPI_FRAME_RADIUS` = 8): in the frame's copies a pixel's top byte is
  its transparency, heeded in the four corner squares only. `DrawTo` blits the frame's **bands**
  only (the title bar, the bottom border, the two sides beside the client area — never under it)
  opaque, the corner squares blended by their pixels' transparency (`BlendAlphaRect`), then the
  client canvas. `CoversOpaque (rect)` — may the compositor skip what lies below? — leaves out only
  the corners' see-through pixels (`CornersIn`: on a corner's row *k* from the edge, its first
  `CornerSpan (k)` pixels — those not wholly inside the arc): a rectangle inside the window still
  covers. *Borderless* windows have no frame.
- **The present's damage** (`PresentDamage`): an app's present dirties **its client area only**
  — its whole window when its frame changed since (`get_chrome`, a resize: `ChromeGen`) or it is
  faded — so a window refreshing alone (an emulator) keeps the compositor's fast path: its client
  rectangle is covered, nothing below it is drawn. That first present after a frame's redraw
  also bumps `ChromeGen` once more: the remote desktop (`rdpd`), which may have read the frame
  while the app was still drawing it (`get_chrome` bumps it **before** the drawing), reads it
  again, whole — no frame left without its buttons on the PC. A window's two frame copies are
  allocated both or none (`AllocChrome`: no half-mapped frame).
- **The title buttons** (v64): the window menu at the left; close, maximise, minimise from the
  right, at `KAPI_FRAME_BTN_*` (the app draws them, the kernel hit-tests them:
  `HitTitleButton`). Close and minimise act at their release over them (`RequestExit`,
  `Minimise`); the window menu (at the press) and maximise (also a double click on the title
  bar) go to the app as `GUI_EVENT_WINCTL` (18; value `KAPI_FRAME_MENU` / `KAPI_FRAME_MAXIMISE`):
  wtk's `Root` shows its window menu, maximises and restores (the developer guide).
- **Minimised windows** (v64, `SetMinimised`): not drawn, not hit, never the active window nor the
  keys' target (`ActiveLocked`, `KeyTargetLocked`), their area damaged as they go and come back;
  `Raise` (`win_raise`, `raise_app`: the dock, the menu bar's Open Windows) brings one back.
- **Workspaces** (v65): a window's desk (`CWindow::Desk`, −1 = every desk: the topmost, backmost
  and system windows) is the current one when it is added (`CWindowManager::m_nDesk`, of
  `m_nDesks`, 4 at boot — the dock sets its number from `SD:/etc/dock.ini`). `SetDeskLocked`
  flags every window of another desk `OffDesk`; `Hidden ()` (minimised or off-desk) is what the
  compositor, `DrawTo`, `CoversOpaque`, `HitTest`, `ActiveLocked`, `KeyTargetLocked` and the
  bottom inset heed, and `SetOffDesk` damages the window's area as it goes and comes back.
  `ForgetHiddenLocked` drops the pointer's state (hover, capture, a drag, a pressed title
  button) naming a window that went hidden. `RaiseLocked` of an off-desk window shows its desk
  first. `OnKey` takes **Ctrl+Alt+Left / Right** (the xterm modifier form `ESC[1;7C` / `D` too)
  before the apps: the previous / next desk, with Shift the active window moved along
  (`MoveToDesk`), not while an app is full screen. `DeskInfo`, `SetDesk`, `MoveToDesk` for kapi
  `desk` / `win_desk` (each change bumps `m_nDeskGen`: the dock redraws its pager).
- **See-through windows** (`WIN_FLAG_ALPHA`, borderless): each pixel blended by its top byte (0
  opaque .. 255 see-through, times the window's fade); never `CoversOpaque`; a click on a wholly
  see-through pixel goes to the window below (`HitTest` → `OpaqueAt`). The dock (its rounded
  corners, the gap under it — almost clear, top byte 254, while a drawer is open, to catch a click
  elsewhere), the menu bar (its drop-downs), the agenda widget (its text straight on the wallpaper).
- **The work area** (`WorkArea`, v64 `win_geometry`): the screen less the top inset (a topmost
  window at y = 0: the menu bar, its smallest height) and the bottom inset (the topmost windows
  standing on the bottom edge: the dock, its smallest height). New windows are placed in it; a
  maximised window fills it.
- **A window that grows** (v64 `resize_window2`): `CWindow::Grow` allocates a bigger canvas and
  frame copies; the process's mappings are moved to them at the same addresses (`MapContig`, then
  `CAddressSpace::FlushTLB`: `tlbi aside1is`); the old memory is retired and freed by the
  compositor three frames later (`FreeRetired`: a frame begun before may still be reading it).
- **The screen's size, while running** (v66 `screen_set`, the Control Panel's Display applet): the
  compositor task makes the frame buffer again (`C2DGraphics::Resize`) between two frames, then
  `OnScreenResized` keeps the windows on the screen and sends them `GUI_EVENT_DISPLAY_RESIZE`. The
  apps placed by the screen's size place themselves again on it: the menu bar (its canvas grown to
  the screen: `resize_window2`), the dock (`onDisplayResize`: laid out for the new width, along the
  bottom), the notifications (the top right corner); wtk re-maximises a maximised window and
  shrinks / moves one past the work area ~0.3 s later (`Root::displayTick`: the dock has moved,
  the work area is the new one). vncd sends the VNC `DesktopSize` pseudo-rectangle (−223) to a
  client that takes it (else it closes the session: the client connects again at the new size);
  rdpd sends `SCREEN` (8: u16 w h) and Onyx Remote resizes its view of the Pi's screen.
- **`CWindowManager`** (singleton): a Z-ordered list (bands: backmost, normal, topmost; the last =
  on top), protected by a `CSpinLock`. `Add`/`Remove`/`Raise`/`Minimise`. `Composite(screen)`:
  1. snapshot the list under the lock (the compositor also frees the grown windows' old memory),
     then blit outside the lock;
  2. find the topmost window that covers the clip rectangle opaquely: nothing below it is drawn;
  3. otherwise clear the desktop and blit the wallpaper;
  4. draw the windows back to front (`DrawTo`, `bActive` for the keys' window);
  5. the drag badge, then the cursor (`mousecur.bin` bitmap or a fallback arrow).

- **Dirty rectangles**: `Composite` honours the screen image's clip rectangle, so the
  compositor task redraws only the damaged rectangles (see the service tasks above); host
  tests `sh tools/tests/run_gui_test.sh` (clipped drawing = full drawing inside the clip,
  nothing touched outside) and `sh tools/tests/desktop_sim/run.sh` (the v64 / v65 windows:
  `wmtest.cpp` — the corners, `CoversOpaque`, the present's damage, the title buttons, minimise,
  the see-through windows' clicks, the work area, a canvas growing, the workspaces).

### 10.3 Widgets (kernel side)

Types: button, label, checkbox, text box (1 line), progress bar,
slider, multi-line text area, V/H scrollbar, icon (image +
label, with an open-app "badge"). Each widget stores the app's **callback
address**. The window manager does the hit-test, the rendering and **pushes the event**;
the app's `pump_events` dispatches it **in the app's context**. Events:

| Constant | Meaning |
|---|---|
| `GUI_EVENT_CLICK` | button/icon released over it |
| `GUI_EVENT_CHECK_CHANGED` | checkbox toggled |
| `GUI_EVENT_TEXT_CHANGED` | textbox/textarea modified |
| `GUI_EVENT_VALUE_CHANGED` | slider/scrollbar moved (0..100) |
| `GUI_EVENT_KEY` | key pressed (`value` = char or `KEY_*`) |
| `GUI_EVENT_CANVAS_CLICK` | client click with no widget: `value=(buttons<<32)|(x<<16)|y` |
| `GUI_EVENT_CANVAS_MOTION` | drag with button held (same coords) |

Mouse handling: hover tracking, press edge (raises the window, hit-test of the close
box / title bar / widget / canvas), drag (window move or continuous
slider/scrollbar), release edge (click/toggle). Focus follows the click
(textbox/textarea). The keyboard (Circle's "cooked" VT100 strings) is translated into logical
keys (`KEY_UP`, `KEY_ENTER`, …) and delivered to the modal dialog, otherwise to the focused
widget, otherwise to the app's keyboard handler.

### 10.4 Theme, wallpaper

- **The theme** is the apps' business: the windows' frames, like every control, are drawn by the
  apps (wtk: `user/wtk/skin.cpp`, `paint.cpp` — by code, no bitmap) from `SD:/etc/theme.txt`
  (`theme` = Peach / Steel / Sage / Brick / Slate, or `active`; `inactive`, `face`, `accent`,
  `outline`, `dock`: read by `wtk/theme.cpp`); the kernel only blits the frames. Of that file the
  kernel reads only `wheelspeed=N` at boot. (The old 9-slice window skin — `wings.bmp` tinted by
  `CSkin` — and `kapi_set_window_theme` are no longer used by the desktop.)
- **Wallpaper**: `set_wallpaper` (BMP), `wallpaper_generate` (toroidal Voronoi generated
  at runtime), or **an app-drawn background**: `wallpaper_buffer` maps the screen-sized
  shared buffer at `USER_WALLPAPER_CANVAS` (13 GB), the app draws, `wallpaper_commit` makes it
  live. The frames are **owned by the kernel** → the background persists after the app exits
  (the `voronoy` case). The agenda widget reads it to choose its ink.

### 10.5 Modal dialogs

`CDialog` (types: `DLG_MSGBOX`, `DLG_FOPEN`, `DLG_FSAVE`). The call runs in the kernel
(not preempted), so the calling app **yields in a loop** in the kernel as long as the dialog is not
resolved; meanwhile **the compositor runs** and draws the dialog **on top of** the
owner window (blocked), and the other apps stay usable. The file dialog lists a
FatFs directory (folders first, `..` to go up), with keyboard selection and, for `FSAVE`,
an editable name field.

---

## 11. Network subsystem (WLAN, TCP/IP, NTP)

Sources: [`kernel/kernel.cpp`](../kernel/kernel.cpp) (`CNetBringupTask`),
[`kernel/sys/net.cpp`](../kernel/sys/net.cpp) (socket backend),
[`kernel/include/kern/net.h`](../kernel/include/kern/net.h). Built on Circle's
`lib/net` (TCP/IP) + the `addon/wlan` BCM4343 driver + `wpa_supplicant`, all linked
into the kernel (see [`kernel/Makefile`](../kernel/Makefile) `LIBS`).

**Two placements**, chosen at boot by `cmdline.txt netcore=`:

- `netcore=0` (the default): the whole stack on the primary core, as ordinary cooperative
  tasks among the others; the socket calls run on the app's task.
- `netcore=1`: **the network core.** Core 3 runs the stack with its own scheduler (§5): at
  boot it waits in `WFE` (`NetCoreMain`, from `COnyxCores::Run`); once the SD card is
  mounted, core 0 calls `NetCoreStart`, and core 3 creates its `CScheduler`, the bring-up task
  (below) and 6 **worker** tasks. Everything the bring-up starts — Circle's `CNetTask`, DHCP,
  NTP, the WLAN driver's kprocs, `wpa_supplicant` — is created on core 3 and runs there, so
  the stack is still used from one core only, as it was written for. The WLAN firmware and
  `wpa_supplicant.conf` are read from core 3 through FatFs: the volume lock
  (`sys/fslock.cpp`) takes its owner with an atomic compare-and-swap. The SDIO interrupt
  stays on core 0 (its handler only masks the controller's interrupt enable; the driver
  polls), and so do the TCP / ARP kernel timers (they set flags under spin locks, which the
  stack reads in `Process`).
  - **Requests.** A socket kapi (`tcp_*`, `net_resolve`, `net_ping`, `net_info`,
    `wlan_scan`) copies its arguments — the app's memory is not mapped on core 3 — into one
    of 32 request slots (8 KB of data each: a bigger send goes in pieces), marks it
    *posted*, and waits: it spins 300 µs (most answers take microseconds), then yields,
    then sleeps 1 ms, then 10 ms steps (an `accept` may wait for hours). A worker claims the
    slot (compare-and-swap), runs the same `Do*` function as `netcore=0`, and marks it
    *done*; a `recv` gathers several segments (up to 8 KB) in one round trip. When every
    worker waits (accepts, connects), the core adds one (up to 24). `net_status` reads the
    state directly.
  - **A process that dies** (in a request, or with sockets open): `NetCloseByPid` (its
    teardown, IRQs masked: nothing waits) drops its posted requests, orphans the running
    ones (the worker then closes what they opened), and queues its pid in a ring that the
    net core's main loop reads to close its sockets.
  - **Notices.** `IpcNotify` is core 0's: the bring-up leaves its "Connected" notice to
    `NetCoreNotify`, and the kernel's main task delivers it (`NetCorePoll`, every 250 ms).
  - Core 3 is then no longer an app core (§14).

- **Bring-up is non-blocking and non-fatal.** `CNetBringupTask` (a kernel `CTask`,
  started from `Run()` once the SD card is mounted) runs `CBcm4343Device::Initialize`
  → `CNetSubSystem::Initialize(FALSE)` → `CWPASupplicant::Initialize`, waits for the
  DHCP bind, logs `net: up, IP …`, then **returns** (the task ends cleanly). A
  missing firmware / `wpa_supplicant.conf` / access point only logs a warning — the
  GUI boots regardless.
- **Self-driving.** `CNetSubSystem::Initialize` spawns Circle's own `CNetTask` /
  `CPHYTask`; they run as ordinary cooperative tasks on our scheduler (§5), so no
  explicit pumping is needed. (This is also why the net task is CPU-busy — the
  reason for `netcore=1`.)
- **Globals.** `g_pNet` (the `CNetSubSystem`) and `g_bNetUp` are published in
  `net.h`; `NetIsUp()` gates the socket calls.
- **Sockets.** `sys/net.cpp` keeps a small table of Circle `CSocket`s behind integer
  handles. `tcp_connect` resolves a dotted-quad or a DNS name and connects (blocking,
  cooperative); `tcp_send` blocks with a 5 s timeout; `tcp_recv` is **non-blocking** (so a
  GUI app polls it from its frame loop; with `netcore=1` one call gathers the segments that
  fit in its 32 KB request buffer, `NET_REQBUF`); `tcp_close` drops it. A connect **takes its
  slot before it blocks** (`SLOT_CONNECTING`, invisible to the other calls): the net core's
  workers run other connects during a DNS lookup or a handshake, and two of them once got the
  same slot (both fetches then read one connection: NetSurf's style sheets failed). **DNS
  cache**: 32 names kept 5 minutes (`ResolveName`, used by `tcp_connect`, `net_resolve`,
  `net_ping`) in front of Circle's `CDNSClient` -- which answers in milliseconds now (it slept
  1 s per query: docs/05 §18). Each socket records its **owner pid**; `kapi_exit` calls
  `NetCloseByPid` as soon as the app's other threads have ended (2026-09-30: it waited for
  the janitor's reaping, `AddressSpaceTaskTerminate`, which calls it again and finds none
  left), so a process that exits or dies without closing does not hold its connections or
  table slots -- a browser relaunched at once found them still held and its pages waited
  (10 s). The table has **64 slots** (`MAX_SOCKETS`; 16 before: a browser keeps a dozen open).
- **TCP fixes in our Circle fork** (2026-10-01, `docs/05` §20–22; host test
  `tools/tests/run_circlenet_test.sh`): only a **real duplicate ACK** (no data, no SYN / FIN,
  the window unchanged, data in flight, ACK = SND.UNA: RFC 5681 §2) counts towards a fast
  retransmit -- a remote desktop client's input messages (data segments that do not advance the
  ACK) started a spurious fast retransmit + recovery while the mouse moved, the Pi's sending
  throttled to ~2 segments a round trip. The **RTO** starts at 1 s (RFC 6298; was 3 s) and its
  floor stays **1 s** (200 ms was tried and undone: spurious timeouts over Wi-Fi to Windows
  collapsed the remote desktop's throughput), Karn's algorithm also after a fast retransmit; a
  connection is given up after 6 timeouts (63 s). **`tcp_send`'s count** is exact: Circle's `CSocket::Send` queues MSS-sized
  chunks, and when one times out (5 s, its queue full) the chunks before it are counted (it
  answered the error, those bytes already on their way: an app resending "the rest" put bytes
  twice in the stream); `NetTcpSend` adds up its 32 KB requests the same way. A short count
  means the bytes queued, then a timeout: resending the rest is right (rdpd does).
- **Clock.** Once the link is up the bring-up task starts a `CNTPDaemon`
  (`system.ini ntp=`; `off` or `none`: no daemon, the clock is not set), which updates
  `CTimer`'s wall clock; the boot reads `system.ini timezone=` (minutes from UTC) and calls
  `CTimer::SetTimeZone` so `get_datetime`, the agenda and the log timestamps show local time
  (`kapi_set_timezone`, v69, changes it while running).
- **Host name.** `system.ini hostname=` (letters, digits, `-`) is handed to `CNetSubSystem` before
  the bring-up task starts (`SetHostname`, an Onyx addition to Circle: docs/05) — the name DHCP
  announces; default Circle's `raspberrypi`. Setup writes it; it takes effect at the next start.
- **Caveats.** Plain-text only (no TLS); `MAX_TASKS` was raised to 40 to fit the net
  workers; the firmware load uses FatFs and is not locked against concurrent app
  file I/O (low risk, one-shot at boot) — with `netcore=1` it is (the atomic volume lock).

The apps that use it: the **irc** client (`user/irc.c`) and the **`net`** `/bin`
tool (link status / IP).

---

## 12. Sound and the second core

Source: [`kernel/sys/sound.cpp`](../kernel/sys/sound.cpp), [`kern/sound.h`](../kernel/include/kern/sound.h).

- **Multi-core.** Circle is built with `ARM_ALLOW_MULTI_CORE` (fork patch #4,
  [Circle Changes](05-CIRCLE-CHANGES.md)). `CKernel::Initialize` starts cores 1–3 through a
  `CMultiCoreSupport` subclass (`COnyxCores`, kernel.cpp) right after the kapi table is
  published. **Everything else stays on core 0**: the scheduler, every process, the
  interrupts (the GIC routes peripherals to core 0; core 1 runs Circle's own `VectorTable`,
  not our `KVectorTable`). Core 1 runs `SoundCoreMain`; cores 2 and 3 are **app cores**
  (§14). A failed start is only a warning (no sound producer, no app cores).
- **The device.** `COnyxSoundDevice` derives from Circle's `CPWMSoundBaseDevice` (PWM + DMA,
  the 3.5 mm jack; 44.1 kHz, 1024-frame chunks) and overrides `GetChunk` — the "producer" —
  which is called from the DMA completion interrupt. It is created and started on the first
  `sound_acquire` (not at boot).
- **Core 1 = the producer.** It sleeps in `WFE` until the device starts, then keeps
  `SND_AHEAD` (4) chunks rendered ahead in a ring; `GetChunk` only copies the next chunk
  (zeros if core 1 fell behind) and `SEV`s core 1. Without `ARM_ALLOW_MULTI_CORE` the same
  code renders in `GetChunk` itself (interrupt, core 0).
- **Rendering** (integer only, no FP in the kernel): 16 voices, each a 32-bit phase
  accumulator (`inc = milliHz · 2³² / (1000 · 44100)`), waveforms square / sine (256-entry
  table, linear interpolation) / triangle / saw / noise (LFSR per period), a linear
  ~5 ms attack/release envelope (no clicks), mixed ÷4 and clipped; plus the **PCM ring**
  (0.5 s of s16 stereo frames, `sound_write`). Converted to the PWM range per sample.
- **FM voices (v47).** `sound_instrument (voice, struct kapi_fm_instrument)` gives a voice a
  2-operator FM instrument in the style of the AdLib's OPL2 (op 0 = modulator, op 1 =
  carrier): multiplier, output level (0.75 dB steps), attack / decay / sustain level /
  release rates (OPL2 timings: attack 2.8 s at rate 1, decay / release 39 s over 96 dB at rate
  1, halving per step), sustained (EG type), tremolo (1 dB, 3.7 Hz) / vibrato (7 cents,
  6.1 Hz), waves sine / half / absolute / quarter pulses, feedback 0–7 and connection
  (FM or additive). Then `sound_start (voice, f, SOUND_FM, volume)` keys it on (both envelopes
  restart) and `sound_stop` keys it off (the release rate fades it). The envelopes run on an
  attenuation in 1/256 octave units (4096 = 96 dB), turned into amplitude by a 256-entry
  2^(−x/256) table; the modulator bends the carrier's phase by up to ±4 periods (as OPL2).
  Tables: `sys/sound_tables.h` (generated). The same file builds on a PC with
  `SOUND_HOST_TEST` (the Circle parts left out): `tools/tests/run_fms_test.sh` renders
  instruments with it, and the Windows FM Song player (`tools/fmsplayer`) plays with it.
- **Low latency (v68, `sound_config`).** `CPWMSoundBaseDevice`'s chunk size is fixed when it is
  made — but it is only the size of its two DMA buffers: Circle's `CDMASoundBuffers` programs each
  transfer with the length `GetChunk` **returns**. So the device is made once, with the biggest
  chunk (1024 frames), and a smaller chunk is just a shorter return: nothing re-created, the PWM
  clock untouched, no Circle change. The owner picks the chunk (`s_nChunkFrames`, 64 .. 1024
  frames) and how many chunks core 1 keeps ahead (`s_nAheadCfg`, 1 .. `SND_AHEAD` 4); each slot
  of the ahead ring records its own length (a change applies from the next chunk rendered — the
  ones already rendered play out first). What is heard lags the rendering by about
  **(ahead + 1) chunks** (the ahead ring + the DMA buffer queued behind the one playing):
  1024 × 4 (the default, what the emulators use) ≈ 116 ms; 256 × 2 ≈ 17 ms; 128 × 2 ≈ 9 ms;
  64 × 1 ≈ 3 ms (a DMA interrupt every 1.5 ms; core 1 must never be late: with 1 chunk ahead any
  hiccup is a click). Add what the app keeps queued in the stream or the ring. `GetChunk` now
  frees a slot only after copying it (it used to free it first: with 4 slots core 1 could render
  into the one being copied). `sound_release` / the owner's death restore 1024 × 4.
- **The mapped ring (v68, `sound_map`).** One 64 KB page (`SoundRing`: a 64 KB-aligned heap
  block, made on first use and **kept for ever** — core 1 may be reading it when its owner dies)
  holding `struct kapi_sound_ring`: a header (`wr` the app's, `rd` the kernel's — free-running
  frame counters —, `dry`, the rate, the chunk / ahead now) and 8192 s16 stereo frames (~186 ms).
  `kapi_sound_map` maps it into the owner like a shared surface (`CAddressSpace::MapSurface`, the
  surface arena; the teardown drops the mapping only), once per process. `Render` reads `wr`
  once per chunk, mixes up to a chunk of frames with the voices and the stream, then moves `rd`
  (`DMB` in between both sides); `dry` counts the times the ring ran out while playing. The
  kernel only ever indexes it masked by its own capacity and treats `wr − rd > 8192` as empty,
  so nothing an app writes there can make it read elsewhere. Since it is plain memory, **an app
  core fills it** (`kapi_sound_ring_write`), and a core-0 thread can sleep on `rd` with
  `wait_word` (the tick sees core 1 move it) — the DAW's engine writes straight into the kernel's
  ring, no pump thread in the audio path. Apps run at EL1 without isolation from the kernel, so a
  former owner that kept its mapping could still write into the page; the kernel stops mixing it
  at the release. `ringtest` plays a tone from an app core this way.
- **Ownership.** One pid owns the output (`sound_acquire`); every other call from another
  pid returns −1. `sound_release`, or the owner's exit (`SoundOnProcessGone`, called from
  `IpcOnProcessGone`), silences the voices, empties the ring and frees the output. The
  state is shared between core 0 (kapi calls) and core 1 (rendering) under a `CSpinLock`.
- **kapi v46**: `sound_acquire`, `sound_release`, `sound_start (voice, milliHz, wave,
  volume)`, `sound_stop (voice | -1)`, `sound_write (frames, n)`, `sound_status`.
  Users: `/bin/tone`, BASIC `PLAY` / `SOUND` / `BEEP` / `NOTEON` / `NOTEOFF`.
- **kapi v68**: `sound_config (chunk_frames, ahead)`, `sound_map ()` (above).

## 13. Post-mortem debug console

Source: [`kernel/sys/debugcon.cpp`](../kernel/sys/debugcon.cpp),
[`kernel/include/kern/debugcon.h`](../kernel/include/kern/debugcon.h).

Purpose: when an app terminates (or in case of trouble), make the logger's last messages
visible **directly on the framebuffer**.

- **`CFbConsole`**: text console that renders the characters (green on black) on the back
  buffer of `C2DGraphics`, with line wrapping, scrolling, and `UpdateDisplay()` after
  each write.
- **`CLogSwitch`**: routes the logger's output either to the boot console
  (`m_Screen`), or to the framebuffer console. At boot: `SetNormal(&m_Screen)` +
  `DebugConsoleRegister`.
- Once the compositor is started, `MuteNormal()` stops the writes to the boot console:
  it is no longer shown, and each line drawn there (a text-mode scroll of the whole
  screen inside `CTerminalDevice::Write`, IRQs masked) froze everything for ~170 ms — an
  app that `printf`s without a terminal (Doom at start-up) froze the GUI and the network
  for seconds. `kmsg` still sees every line (CLogger's event ring does not depend on the
  target).
- When the debug console takes over (`DebugConsoleTakeover`, called on `exit`), the
  compositor **detects** `DebugConsoleActive()` and **stops presenting** so as not to
  contend for the framebuffer.

### The crash record (`lastcrash.txt`)

Source: [`kernel/sys/crashlog.cpp`](../kernel/sys/crashlog.cpp),
[`kern/crashlog.h`](../kernel/include/kern/crashlog.h).

A frozen Pi (the scheduler stopped: the green LED stays lit or dark, no SOS) left nothing to
read. Now 64 KB of RAM kept out of the heap (Circle fork, docs/05 §15) hold, for the running
session, cleaned to RAM as they are written:

- the **tail of the kernel log** (`CLogSwitch::Write` copies every line, ~62 KB ring);
- the **last 16 places core 0 was interrupted at** (`KernelIRQExit`, each IRQ): PC, LR, SP_EL0,
  whether IRQs were masked, the task's name — a kernel loop shows itself there;
- **breadcrumbs**: the GPU (`idle / clipping / binning / rendering / texture upload`) and the
  compositor's display copy (`composing / display DMA / waiting for the display DMA`);
- the reaper's **last pass** (uptime), and a **panic**'s registers (`DumpAndHalt`, before the
  panic screen) with the **return addresses found on the faulting context's stack**
  (`CrashLogStack`);
- the **free memory**, once a second (`CrashLogMemory`, breadcrumbs 5–7): the kernel heap, the
  kernel pages (low pager), the app pages (the high zone) — an "Out of memory" shows its approach;
- the **app the GUI watchdog finds frozen** (below): its pid, the last 16 places its task was
  interrupted at, and its stack's return addresses.

**Return addresses.** The apps are built `-O2` (no frame pointers to follow): `ScanStack` reads
16 KB up from the context's SP and keeps the words that point just after a `BL` / `BLR`, in an
app's code (`[8 GB, 10 GB)`) or the kernel's (`[0x80000, _etext)`) — 24 at most, newest first.
Each page is probed first with `AT S1E1R` (a wild SP, an unmapped page: no fault). Read them with
`aarch64-none-elf-addr2line -f -C -e user/<app>.elf <addr>` (or `kernel/kernel8-rpi4.elf`); a
few are stale words, the first ones are the calls in progress.

The **BCM watchdog** is armed by the reaper (every second, `cmdline.txt hangreboot=` seconds,
default 15, at most ~15; 0 = off): when the scheduler stops, the Pi restarts by itself (a panic
too, after its screen and SOS). `kapi_shutdown` / `kapi_reboot` stop it and mark the session
clean. At the next boot, a session that did not end cleanly (its record still in RAM: a power cut
loses it) is written to **`SD:/etc/lastcrash.txt`** and noted in `kmsg` (`crashlog`). Best
effort: nothing if the RAM did not keep its contents through the reset.

**Core 1 writes the report itself** (the RAM does not survive the Pi 4's watchdog reset:
the first real freeze left no record). At boot (`CrashLogReport`, SD: mounted) the kernel keeps
the previous dump if `SD:/etc/crashdump.txt` holds one (`ONYX CRASH REPORT` … `--- end of the
report ---` → `SD:/etc/lastcrash.txt`), then fills that 64 KB file with "armed" and notes each
of its 128 sectors' LBA (through FatFs: `f_lseek` + a one-byte `f_read` → `FIL.sect`). Core 1,
the sound core (`SoundCoreMain`), is woken at least every ~1.2 ms by the generic timer's event
stream (`CNTKCTL_EL1.EVNTEN`, `EVNTI` 15) and calls `CrashLogCoreCheck`: when the reaper's
`CNTPCT` stamp has not moved for **10 s**, it formats the record (no heap, no logger), writes it
**raw** through the `emmc1` device into those sectors (runs of consecutive sectors in one
write; `g_bCrashDumping` keeps `OnyxDriverWait` from yielding) and restarts the Pi
(`CBcmWatchdog::Restart`). No FatFs, heap, logger or scheduler call: core 0 may hold their
locks; core 1 takes the sound lock only with `TryAcquire`, so a core 0 frozen while holding it
does not stop the watch. Its progress is kept in the RAM record (`nDumpStep`: started, text
ready, written, the write failed): when the SD write cannot finish (a wedged bus), the hardware
watchdog restarts the Pi and the RAM report (the record of an 8 GB Pi sits at the top of the RAM
above 4 GB) says how far core 1 got. **A stuck GUI with the scheduler alive** (the compositor
without a frame for 12 s, the GUI watchdog task) asks for the same report
(`CrashLogRequest`: core 0 masks its IRQs and waits, core 1 writes it, then the restart).
**A Circle panic** (an assertion, the kernel heap's "Out of memory", ...) used to leave nothing:
Circle's logger halts **every** core after the panic line (`CMultiCoreSupport::HaltAll`), core 1
too, so the Pi stayed frozen ~15 s and the hardware watchdog restarted it without a report. The
logger's panic handler is now `CrashLogPanicHandler`: the log's last line (the panic's) goes into
the record and core 1 writes the report at once (`CrashLogDumpNow`), before the halt. An
exception (`DumpAndHalt`) asks for it at once too, after its screen and one SOS (it used to wait
the 10 s of a stopped reaper).

**The app watchdog** (`SD:/etc/apphang.txt`). When the GUI watchdog finds an app not taking its
window's events (2 s, some queued), it watches that app (`CrashLogWatchPid`: core 0's IRQs note
where its task is and, every 8th time, scan its stack) and, every 2 s while it lasts, has the
`apphang` task (its own: never the GUI watchdog stuck on the card) rewrite `SD:/etc/apphang.txt`:
the app, its tasks' states (running / ready / blocked / sleeping) and pages, then the whole record
(the free memory, core 0's samples, the app's samples and stack) and the log's last 12 KB. The
compositor without a frame does the same (every 2 s, before the 12 s report). Recovered (the app
takes its events again, or is closed / killed): renamed `SD:/etc/lasthang.txt`; a clean shutdown /
reboot renames it too. Still there at the next boot (the session never ended cleanly — the
freeze grew into a restart): appended to `SD:/etc/lastcrash.txt` (`MergeAppHang`), or written as
it when the kernel kept no record of its own.
**Power and heat.** Once a second the GUI watchdog task calls `CrashLogPower`: the firmware's
`GET_THROTTLED` bits (under-voltage, ARM frequency capped, throttled, soft temperature limit —
now, or since boot) and the SoC temperature. A change (or each 5 °C above 60 °C) is logged
(`power: SoC 71 C, throttling: under-voltage (earlier)`), and the last values plus the session's
highest temperature go into the crash record (breadcrumbs 2–4), printed in every report — a
freeze that only a cold boot brings back smells of the supply or the heat.

**The clock across boots.** The Pi has no battery-backed clock: until NTP answers (~15 s),
the time was 0 and the files written meanwhile (`crashdump.txt`, `lastcrash.txt`) had no date.
`SD:/etc/clock` keeps the last time seen (UTC seconds, text; written every 10 minutes by the GUI
watchdog task and by `kapi_shutdown` / `kapi_reboot`) and is read back at boot, right after SD: is
mounted (`CrashLogClockRestore`) — behind by how long the Pi was off, until NTP corrects it.

Headless signs on the green ACT LED (a GPIO set / clear, no lock): **3 s of fast blinks** when
core 1 sees the hang, the SD write's own flicker, then **3 s lit** (written) or **3 slow blinks**
(the write failed) before the restart; no blinking at all = core 1 stuck too (a wedged bus).
`hangreboot=0` turns it off with the watchdog. `kmsg` tells at boot where the record is
and whether the dump is armed (`crashlog: ...`). Test: `hangtest` (IRQs masked) / `hangtest irq`.

The panic screen is now copied into the displayed frame buffer **by the CPU** (not the DMA:
the compositor's display DMA may be in flight, and waiting for it hung the panic before its
screen and its SOS).

---

## 14. App cores (cores 2 and 3)

Source: [`kernel/sys/appcore.cpp`](../kernel/sys/appcore.cpp),
[`kern/appcore.h`](../kernel/include/kern/appcore.h); user side
[`user/emucore.h`](../user/emucore.h), test [`user/bin/coretest.c`](../user/bin/coretest.c).

Cores 2 and 3 (core 2 only with `netcore=1`: core 3 then runs the network, §11) are a
**resource an app acquires**, like the sound output: it gets a whole
core and runs one function of its own code there, undisturbed (no scheduler, no timer, no
other task on that core). The rest of the system stays on core 0 as before, so none of
the kernel, Circle's drivers, FatFs or the network has to be multi-core safe.

- **The core's loop** (`AppCoreMain` → `AppCoreLoop`, from `COnyxCores::Run`): it installs
  **our** `KVectorTable` on that core (`VBAR_EL1` is per core), notes its kernel stack,
  enables IRQs (for the stop IPI only: the GIC routes no peripheral there) and waits in
  `WFE`. A job: load the owner's `TTBR0` (its L2 table + ASID, `CAddressSpace::GetTTBR0`),
  `tlbi vmalle1` on that core only (an ASID may have been reused since its last job), then
  `AppCoreCall (fn, arg, stack)` — at EL1t, `SP` = the app's stack, like the app itself on
  core 0. When `fn` returns: back to the kernel address space, state `IDLE`, `SEV`.
- **The kapi side (core 0).** `core_acquire` hands a free, started core to the caller's
  address space; `core_run` checks the owner, that nothing runs, that `fn` and the stack
  are user addresses, fills the job, then `bGo` + `DSB` + `SEV`. Every word shared between
  the cores is plain cacheable memory (inner-shareable, coherent) with `DSB ISH` barriers;
  only core 0 writes the ownership.
- **Stopping a job** (`core_release`, the app's exit or kill): core 0 raises `bAbort` and
  sends the core an IPI (`SendIPI`, `IPI_USER`). The IRQ enters our `IrqEntry` on that core;
  `KernelIRQExit` sees it is not core 0 (no scheduling there) and calls `AppCoreOnIRQExit`,
  which **rewrites the trap frame** to return into `AppCoreRestart` on the core's own kernel
  stack (EL1t, IRQs masked): the job is simply dropped. `AppCoreRestart` goes back to the
  kernel address space and clears `bAbort` — core 0's signal that the core is out of the
  app's memory. Core 0 waits for it at most 200 ms.
- **Faults.** A synchronous exception on core 2–3 (a bad access in the job) reaches
  `SyncHandlerEL1`, which hands it to `AppCoreOnFault` instead of the kernel panic: the ESR
  class, PC and fault address are kept, the state becomes `FAULT` and the frame is rewritten
  to `AppCoreRestart` as above. Core 0 logs it once (`appcore: core N: fault EC=... at pc
  ... (address ...)`) when the owner asks the state or releases the core.
- **Teardown.** `~CAddressSpace` calls `AppCoreReleaseAS (this)` **before** freeing the
  window and the frames: a job still running uses them. A core that does not answer the
  stop (its code masked the interrupts — never do that) is **retired** (`bLost`, never used
  again) and the dying space's memory is kept (leaked) rather than freed under it.
- **The rules for `fn`**: no kapi call (the kernel is not called from two cores at once),
  no allocation (newlib's and umm's `malloc` are not multi-core safe), no masking of the
  interrupts. It computes, reads the clock directly (`cntpct_el0`) and exchanges data with
  the app's main thread through memory, with barriers. It may use `WFE`, woken by the main
  thread's `SEV`.
- **`user/emucore.h`** packages that for the emulators: the machine runs on the app core
  (`ec_thread`), the main thread asks for frames (`ec_request`), gets the pictures from a
  **triple buffer** (`ec_publish` / `ec_take`: the machine never waits for the display and
  the display always gets the latest complete picture) and the sound from a single-producer
  ring (`ec_audio_push` / `ec_audio_pop`), and stops the machine between two frames to touch
  it (`ec_hold` / `ec_resume`: reset, palettes, battery saves). Without a free core,
  `ec_pump` runs the same frames on the main thread. `gbemu`, `gbaemu`, `nesemu` and `snesemu` use it.
- **Test**: `/bin/coretest` (the same computation on an app core and on core 0, a job
  stopped by its flag, an endless job stopped by `core_release`, a faulting job, both app
  cores at once); `coretest exit` leaves a job spinning and exits (the teardown must stop it).
- **Bigger programs on an app core**: newlib's syscalls can run on the main thread when
  called from an app core (`libc/onyx_syscalls.c`, `onyx_rpc_*`: the caller posts the call and
  waits in `WFE`, the main thread runs it in `onyx_rpc_serve` and `SEV`s). Doom's engine runs
  that way (`user/doom`), with its malloc, files and saves.

## 15. The GPU (V3D)

Source: [`kernel/sys/v3d.cpp`](../kernel/sys/v3d.cpp), the packets
[`kern/v3d_cl.h`](../kernel/include/kern/v3d_cl.h), the shaders `sys/v3d_shaders.qasm`, the
texture layouts `kern/v3d_tiling.h`; demos `user/Apps/teapot` (v52), `user/Apps/gpudemo` (v53),
`/bin/gpcdemo` (the compositing service over v53 + v70).

The Raspberry Pi 4's VideoCore VI 3D core (**V3D 4.2**) driven from the kernel, bare metal —
after Random06457's *rpi4-gpu-bare-metal-examples* (MIT: the packet structs, the three shaders,
the control-list recipe) and macoy's `rpi-system` notes (cache cleaning, the binner's memory).

- **Bring-up** (the first `gpu_info` / `gpu_draw`, never at boot): the firmware turns the V3D
  power domain (10) and clock (5) on and the clock to its maximum (mailbox); the reset is
  released (`PM_GRAFX` `V3DRSTN`, with the PM password) and the V3D's async AXI bridges opened
  (`ASB_V3D_M_CTRL` / `_S_CTRL` at `0xFEC11008/C`, waiting for the ACK bit with a time limit);
  then `HUB_IDENT1` must say version 4 (kmsg: `HUB_IDENT0..2`, `CTL_IDENT0`). Any failure leaves
  the GPU off; `gpu_info` says why. The interrupt is connected there too (see below).
- **Memory**: no V3D MMU — the GPU gets physical addresses, all its buffers come from Circle's
  low heap (`HEAP_LOW`, < 1 GB, page aligned): the control lists, the shaders and default
  attributes, the vertices, the render target (raster RGBA8), the tile allocation (the tiles ×
  64 B + 2 MB), the tile state (256 B a tile) and a 4 MB overflow pool. CPU caches: cleaned
  after the CPU writes (the lists, the vertices), the target cleaned and invalidated before
  the GPU writes it and invalidated before the CPU reads it; the GPU's L2T and slice caches
  are flushed before each job.
- **A frame** (`gpu_draw`): the vertices are copied into the GPU buffer; the **binning list**
  (`TILE_BINNING_MODE_CFG`, the clip window, `CFG_BITS` with the depth test *less* and depth
  writes, the viewport scaling and offset, `GL_SHADER_STATE` → a shader record + two
  attribute records over the same 16-byte vertices — position 3 floats, colour 4 normalized
  bytes —, `VERTEX_ARRAY_PRIMS` triangles) sorts the triangles into 64 × 64 tile lists; the
  **rendering list** configures the tile buffer (one RGBA8 target, Z cleared to 1.0), clears,
  then for each tile runs its list, stores the colours (`STORE_TILE_BUFFER_GENERAL`, raster)
  and clears again. Jobs: `CT0QMA/QMS/QTS/QBA/QEA` (bin), `CT1QBA/QEA` (render); the end is
  polled on the frame counters `BFC` / `RFC` (the caller's task yields meanwhile; one frame
  at a time), 0.5 s at most — past that the state of the control-list executors is logged
  and the GPU left off. The binner's out-of-memory requests (`INT_STS` `OUTOMEM`) are answered
  from the overflow pool (`PTB_BPOA` / `BPOS`). The RGBA8 picture is copied into the caller's
  pixels as 0x00RRGGBB.
- **Shaders**: fixed — a vertex shader (position → screen, colour → varyings), its coordinate
  shader for the binner, a fragment shader writing the interpolated colour: Mesa-compiled QPU
  code taken from Random06457's example. The geometry, the lighting and the projection are the
  app's (the CPU); the GPU does the rasterization, the interpolation and the depth test.
- **The full pipeline** (kapi v53, `gpu_render`): the same frame recipe with **batches**. Each
  batch has its own shader record (the attributes: position x y z w — 4 floats —, s t, colour;
  s t, colour, added colour; 32-byte vertices), its uniforms (the batch's 4 × 4 matrix, then the viewport scales), its
  `CFG_BITS` (depth function and writes, culling: `clockwise_primitives` set, front =
  counter-clockwise in y-up coordinates, as GL), its blending (`BLEND_ENABLES` +
  `BLEND_CFG`: alpha `SRC_ALPHA / INV_SRC_ALPHA`, add `SRC_ALPHA / ONE`, multiply
  `DST_COLOR / ZERO`, premultiplied `ONE / INV_SRC_ALPHA`) and one `VERTEX_ARRAY_PRIMS` over its
  range of the vertex buffer. `KAPI_GPU_F_KEEP` loads each tile from the target first
  (`LOAD_TILE_BUFFER_GENERAL`, raster RGBA8, alpha forced to 1) instead of clearing it.
  The target's load and store packets are built in one place (`V3dLoadTarget` /
  `V3dStoreTarget`, `kern/v3d_cl.h`). **Fixed (2026-09-30):** the load packet's packed
  bit-fields lacked 3 bits of padding after `r_b_swap`, so its stride landed at bit 25 instead
  of 28 and the V3D read it divided by 8 — every frame drawn over the target's pixels (the
  compositing service without a clear; `gpcdemo test`'s "over the target's pixels" failed on
  the Pi) loaded the wrong rows. `tools/tests/run_v3d_cl_test.sh` now checks each field of the
  load / store packets against Mesa's `v3d_packet.xml` positions, and the PC's software V3D
  (`tools/tests/gpucomp/hostkapi.cpp`) decodes the kernel's own packets for the target's stride
  (it re-implemented the frame and could not see the packets; `gpcdemo test` also drew over a
  single colour, which hid wrong rows — it draws over a pattern now).
- **Shaders of v53** (hand-written: [`sys/v3d_shaders.qasm`](../kernel/sys/v3d_shaders.qasm) →
  `v3d_shaders.inc`, assembled by `tools/qpu/qpuasm`, see below): `VS_CLIP` — the 10 inputs,
  the matrix product (16 `ldunif`), 1/w on the SFU (`recip`, r4), Xs Ys in 24.8 fixed point,
  Zs, 1/Wc, then the 10 varyings s t r g b a r2 g2 b2 a2 (v54); `CS_CLIP` — its binner copy (clip X Y Z W, then Xs
  Ys); `FS_COLOR` — the interpolated colour (varying = `ldvary` × W + C, C in r5 two
  instructions later); `FS_TEX` — the texture lookup (T to `tmut`, the TMU configuration
  p0 / p1 by `wrtmuc`, S to `tmus` starts it, as Mesa orders them; a thread switch while it
  runs; `ldtmu` returns R G and B A as half floats) multiplied by the colour; both then add the
  second colour and clamp to 1 (v54). `FS_COLOR_AT` / `FS_TEX_AT` (v54, `B_ALPHATEST`) read
  the threshold as one more uniform and discard the fragment (`fsub.pushn`, then
  `setmsf.ifa -, 0`) when alpha is below it. The fragment
  shaders end with the TLB writes (`vfpack tlb`) after the last-segment thread switch.
- **App shaders** (kapi v61, `gpu_program` / `gpu_render2`): the app brings its QPU code
  (built at run time with the C++ builder `user/v3d/qpu.h` over Mesa's instruction packer,
  `tools/qpu/mesa`; tested on the PC by the simulator `tools/qpu/qpusim` — `tools/tests/run_qpu_test.sh`).
  A program (`TProgram`, ≤ 256, owned by its process) holds the three shaders in low GPU
  memory, each at a 256-byte boundary and followed by NOPs (the QPU fetches ahead). Each
  batch of `gpu_render2` gets its shader record from the program: the VPM segment sizes
  (⌈inputs / 8⌉, ⌈(4 + varyings) / 8⌉, the coordinate shader's ⌈csInputs / 8⌉ and
  ⌈csOutputs / 8⌉), the fragment shader's threading (`P_FS_4WAY`, `P_FS_FINAL`) and Z writes;
  one float vec4 attribute per 4 inputs (≤ 16 arrays: the `GL_SHADER_STATE` packet is written
  by hand, its count has 5 bits), its uniforms copied from the call's array — the samplers
  first (32-byte aligned), then the vertex, coordinate and fragment ranges, the texture words
  patched in the latter —, then `CFG_BITS`, `BLEND_CFG` (the batch's own factors and
  equations), `COLOR_WRITE_MASKS` and `CLIP_WINDOW` (the scissor) when they change. The
  kernel does not check what the shaders do: a program that never ends is stopped by the
  frame's time limit (the GPU is then left off), and the TMU's general writes could reach
  any low memory — apps are trusted, as with `code_alloc`. **`P_FS_FINAL` is best avoided**:
  a fragment shader started in its final thread section (no last-segment pair) that reads
  uniforms stopped the GPU now and then on targets 512 pixels wide or more (5 runs out of 7,
  mostly the first frame after boot; 64 × 64 frames always passed; `v3dprog ww`, docs/03). Mesa
  never uses it for fragment shaders; neither do `user/v3d/gxtev` nor gcemu since: they end
  with the last-segment pair (two `thrsw` in a row, the live values in registers of the register
  file — the accumulators do not survive a thread switch) before the TLB writes.
- **`gpu_vbuf` / `gpu_render3` in place** (v63, `ClipInPlace`): a vbuf is a `HEAP_LOW` block
  (64 KB aligned, below 1 GB: its address is the GPU's) mapped into the program by `MapSurface`
  (`s_VBuf`, freed by `V3DReleaseAS`). When `gpu_render3`'s array lies in one of the caller's, each
  batch's vertices are framed where they are and tested; the triangles inside every plane make
  runs `{first, count}` of the batch's own vertices (`s_pRun`, `s_pRunAt`: `Render2` draws each
  run with the batch's shader record, its attributes at the batch's address and stride), the
  others are clipped into the buffer's free end, each fan at a whole number of the batch's
  strides from it (a run too); then the touched range's cache is cleaned. The copying paths
  (`ClipFrame2` / `3`) make one run a batch.
- **`gpu_render3`** (`ClipFrame3`): each batch's triangles are read at its offset and stride in
  the caller's array, framed (x, y) into the GPU's vertex buffer at that stride — kept as they
  are when inside every plane, else clipped in their place — so the output is each batch's own
  packed run (`s_pOutOff`, `s_pOutStride`); `Render2` gives each batch its attribute records at
  that address and stride, and draws from its vertex 0 (`gpu_render2`'s batches too, at the
  common stride). The Wind Waker's display no longer copies its ~7 MB of vertices a frame
  before the call (gcemu's `Out::prepare` 5.9 -> 0.1 ms): the machine's core, sharing the L2 and
  the RAM, 33.5 -> 29.9 M cycles a field.
- **`gpu_render2`'s clipping** (`ClipFrame2`): a triangle whose three vertices are inside every
  plane (`V3DInsideN`: the near plane, w > 0, the guard band — nearly all of them) is copied
  once, its program's inputs only, straight into the GPU's vertex buffer (`s_Verts2`, sized for
  the clipped fans); the others go through `V3DClipTriangleN` (every float interpolated) into the
  same buffer. Before, each vertex was copied four times (staged, clipped, into a buffer, into
  the GPU's) — ~30 MB a frame for The Wind Waker's ~100k vertices through the L2 the app cores
  share: its `gpu_render2` went from ~34 to ~25 ms a frame. Every 256 frames kmsg gets
  `render2, a frame: clip … us, lists … us, GPU … us, target … us; … vertices` (the clipping and
  its copy; the lists, the uniforms and the target's cache cleaning; the binning and rendering;
  the target after) — Wind Waker's game: ~9.8 ms, ~0.9, ~14, ~0.2 for ~80k vertices drawn.
- **Textures** (`gpu_texture`, ≤ 1024 — 512 a program — since v70, owned by the program that made them, freed with its
  address space): a 32-byte `TEXTURE_SHADER_STATE` (base address, size, RGBA8, swizzle, and
  for UIF: *level 0 strictly UIF*, its `UB_PAD`, XOR) then the texels, in the layout the texture
  unit reads for that size (Mesa's `v3d_setup_slices`, [`kern/v3d_tiling.h`](../kernel/include/kern/v3d_tiling.h)):
  **LT** (a line of 4 × 4-texel utiles, a side ≤ 4), **UBLINEAR** 1 / 2 columns (8 × 8 blocks
  of 2 × 2 utiles, width ≤ 8 / 16), else **UIF** (columns of 4 blocks, padded rows against the
  page-cache banks, XOR on odd columns when aligned on them). A batch's `SAMPLER_STATE` (filter,
  wrap S / T) is written per frame; the fragment uniforms are p0 = the texture state | 3 (two
  16-bit words returned) and p1 = the sampler state. Host test (every texel of every size up
  to 300 × 300 against Mesa's functions): `sh tools/tests/run_v3d_tiling_test.sh`.
- **The QPU assembler** `tools/qpu/qpuasm` (host, `cd tools/qpu && make`): Mesa's QPU encoder
  / disassembler (`qpu_instr.c`, `qpu_pack.c`, `qpu_disasm.c`, MIT, vendored in
  `tools/qpu/mesa/`) behind a parser of the disassembler's own syntax; a line is accepted only
  if its encoding disassembles back to it, then each shader is checked against the instruction
  restrictions (Mesa's `qpu_validate.c` and its scheduler's timing rules: r4 ≥ 3 instructions
  after an SFU write, r5 ≥ 2 after `ldvary`, one TMU / SFU / VPM / TLB access a line, no
  regfile read right after its write, the thread-switch rules, no TLB access before the
  scoreboard wait). `qpuasm -d HEX…` disassembles. It re-assembles the three v52 shaders bit
  for bit.
- **Straight into the target** (v52 and v53): the caller's pixel pointer is translated page
  by page (`AT S1E1R` → `PAR_EL1`, IRQs off); when the whole span (`(h − 1) × stride + w`
  pixels) is physically contiguous below 1 GB — a window canvas is — the tile stores (and the
  `KEEP` loads) go straight there: raster RGBA8 with **R/B swap** (= 0x00RRGGBB in memory),
  the row pitch = the caller's stride, alpha not written (`COLOR_WRITE_MASKS` 0x8) and
  cleared to 0; the span is cleaned + invalidated from the CPU caches before and after. Other
  buffers go through `s_Target` and a copy, as before. kmsg says which mode is used (when it
  changes).
- **Completion by interrupt**: the V3D core interrupt (GIC SPI 74, shared by core and hub in
  the Pi 4's device tree; the hub's are masked) is connected at bring-up with `FLDONE` (bin
  done), `FRDONE` (render done) and `OUTOMEM` unmasked. The handler answers the binner's
  memory requests from the overflow pool (and masks `OUTOMEM` once the pool is used up) and
  sets the bin / render events; the drawing task sleeps on them (`WaitWithTimeout`, 2 ms
  slices, the frame counters `BFC` / `RFC` re-checked each time, 0.5 s at most). Without an
  interrupt it degrades to that 2 ms polling (logged once) and serves the binner itself.
- **Sharing the GPU between programs** (the GameCube emulator and the browser at once, say). The
  kernel is not preempted (§5) and every GPU call runs on core 0, so the `while (s_bBusy) Yield ();
  s_bBusy = TRUE;` that starts each `gpu_texture` / `gpu_texture_rect` / `gpu_program` /
  `gpu_render*` is atomic: **one GPU job at a time, whole** — a frame's lists, its binning and
  rendering, its target's copy — and another program's call waits (it yields) until that frame is
  done. Nothing of a frame survives it: the control lists, the vertex buffers, `s_Target`, the tile
  memory are rebuilt by each frame (their memory only grows). What a program owns is marked with
  its address space (textures, programs, vbufs), checked by every call, freed when it ends
  (`V3DReleaseAS`, after the frame in flight). So two programs can use the GPU together; what they
  compete for is the pools of handles: until v70 one program could hold all 256 textures (gcemu and
  n64emu keep up to 256) or all 8 vbufs, and the other got −4 / 0 — **v70**: 1024 textures, 512 a
  program; 32 vbufs, 8 a program. Still shared: the GPU's time (frames are served in the order the
  scheduler runs the callers: a 14 ms Wind Waker frame delays a composite by up to that much), and
  its state — a frame that hangs turns the GPU off for everybody until a reboot (`s_nState = −1`;
  the compositing service then goes on with the CPU, `GPC_LOST`). The GPU's low memory (below 1 GB,
  `HEAP_LOW`) is shared with the kernel's other users.
- **The compositing service** (`user/gpucomp/gpucomp.{h,c}`, a user library over v53 + v70; docs/03
  *GPU compositing*): an app's layers — ARGB premultiplied textures of any size (past 2048 a side
  kept as tiles of 2048 with a one-texel border of their neighbours, so that bilinear filtering is
  seamless) — composited into a target (the window's canvas: the direct mode, no copy) with, per
  layer, a 2D affine matrix, a clip rectangle, an opacity, premultiplied source-over and bilinear
  (or nearest) filtering; scrolling = the layer's source rectangle moved over its texture. A
  composite is one `gpu_render` frame (a target past 2048 a side: equal parts, one frame each): per
  layer and tile, the tile's inner rectangle (within the source rectangle) mapped into the target,
  clipped there (Sutherland-Hodgman against the clip rectangle and the target: the texture
  coordinates are affine, interpolated exactly) and drawn as a fan — the kernel's `FS_TEX`
  (texel × colour, the colour = the opacity), blend `PREMUL` (`ONE, ONE_MINUS_SRC_ALPHA`, or none
  for an opaque layer at full opacity), `Z_ALWAYS` + `NOZWRITE`, clamped, `NOMATRIX` clip-space
  vertices on the kernel's viewport (`X = x ⌊w / 2⌋ + ⌊w / 2⌋`: the halves are integers in the
  binning list's scaling). Zero-copy textures are not possible: V3D 4.2's `TEXTURE_SHADER_STATE`
  has no raster layout for 2D images — the texture unit derives LT / UBLINEAR / UIF from the size
  (§ *Textures* above) —, so an upload always stores texels into the tiled layout (hence
  `gpu_texture_rect` for the damaged parts only); what is zero-copy is the target (the canvas, or a
  `gpu_vbuf` block). The same API is implemented on the CPU (the fallback: no GPU, an older
  kernel, the GPU stopped; a texture the GPU refused is drawn by the CPU between two GPU frames
  that keep the target), checked against the GPU path on the PC (`tools/tests/run_gpucomp_test.sh`:
  a software V3D runs this `FS_TEX` in the QPU simulator) and on the Pi (`/bin/gpcdemo test`).
- **Not yet**: several frames in flight (the caller waits for its frame), mipmaps, shaders
  chosen by the app (v61 has them), antialiased polygon edges (the compositor's rotated layers
  have hard edges: MSAA 4x in the tile buffer would give them).

---

## 16. The RAM: volume

Source: [`kernel/sys/ramfs.cpp`](../kernel/sys/ramfs.cpp),
[`kern/ramfs.h`](../kernel/include/kern/ramfs.h); the stream: `CRamStream`
([`sys/stream.cpp`](../kernel/sys/stream.cpp)); the kapi side: [`sys/kapi.cpp`](../kernel/sys/kapi.cpp);
host test [`tools/tests/run_ramfs_test.sh`](../tools/tests/run_ramfs_test.sh); Pi test
[`user/bin/ramtest.c`](../user/bin/ramtest.c).

**`RAM:`** is a volume in memory, reached by the **same file calls** as the card: `open` / `read` /
`fsize` / `fsize64` / `seek` / `close`, `save_file`, `file_in` / `file_out` (streams, appending
too: `cp`, `cat`, the shell's redirections), `opendir` / `readdir` / `closedir`, `mkdir`, `remove`,
`rename` (within `RAM:`; across volumes −1, the caller copies then removes, as between `SD:` and
`SD1:`), `chdir` (`cd RAM:/x`, relative paths there), and `vol_info` (v71). newlib's `fopen` /
`fread` / `fwrite` / `remove` and NetSurf's `opendir` sit on these, so a program needs nothing new
to use it. Its files live **until the Pi restarts**: never written anywhere, not tied to any app (an
app that ends leaves its files; its open handles are closed, `RamFsOnProcessGone` from
`IpcOnProcessGone`). Programs cannot be started from it (`exec`, `spawn`: the card only). Jet
Browser keeps its disk cache and its JS code cache there (`RAM:/jet/cache`, `RAM:/jet/jscache`,
docs/06 §33): no browsing data on the card, and no SD write freezing core 0.

**The tree.** Folders and files are `TNode` records on the kernel heap — one size, so a freed one is
reused as is (Circle's heap keeps freed blocks by size, it never merges them): a name of up to
127 characters (**case-insensitive, case kept**, as on the card's FAT), the parent, the children (a
list in creation order: `readdir`'s order), the open count (`nRefs`). A file removed while open
leaves its folder at once and is freed at its last close (it can still be read to its end); a
folder must be empty to be removed; a folder cannot be moved into itself; `rename` onto an existing
name fails (as FatFs). Lookups walk the path's names (a folder's children one by one: fine for the
few thousand files of a cache).

**The bytes** are in **64 KB pages of the page allocator** (`palloc_high`: the apps' pool, the high
zone; without one — a 1 GB Pi — the low pager), never in the kernel's BSS (the 2 MB image limit) nor
in the heap. A file is a list of **extents**: a whole page, or a **chunk** of 256 B … 32 KB (8 sizes,
powers of two) of a page cut into chunks of one size (`TSlab`: the page, a bitmap of its chunks;
each size keeps the pages that have a free chunk). Every extent but the last is full. A file
written whole (`save_file`) is cut exactly: whole pages, then the rest in falling powers of two
(40 KB + 100 B → 32 KB, 8 KB, 256 B: under 256 B lost a file). A stream writes whole pages; at its
close its **tail** (the chunks after its last whole page, and that page) is cut again exactly
(`Trim`) — a log appended a line at a time stays compact. The extent list itself is in a chunk (a
page holds ~2700 extents: 170 MB; a file is limited to `RAMFS_FILE_MAX`, 128 MB, and to the
volume). A chunk freed gives its page back as soon as nothing else in it is used: removing a file
returns its memory (`vol_info` shows it, `ramtest` checks it).

**Limits** (`kern/ramfs.h`):

| | |
|---|---|
| The volume's size | `SD:/etc/system.ini` **`ramfs=`**: megabytes (`ramfs=64`) or a share of the page memory free at boot (`ramfs=10%`, at most 90 %); `ramfs=0` (or under 1 MB): no `RAM:` volume (`vol_info` −1, its paths fail). **Default** (no line): **128 MB, at most a quarter** of the free page memory (a 1 GB Pi: ~60 MB). Never more than the free memory less the reserve. Read at boot (a restart applies a change). |
| The reserve | `RAMFS_RESERVE` = 32 MB of page memory always left to the apps: a write that would go below it fails (the volume is then "full" before its size). |
| Files and folders | `RAMFS_MAX_NODES` = 16384 (their records: ~250 bytes of heap each); a new record also needs 8 MB of heap never handed out (Circle panics when its heap runs out). |
| A file | `RAMFS_FILE_MAX` = 128 MB (and the volume's room). |
| Open handles | 256 files, 64 folder listings (on the heap); streams are not counted. |

A full volume: `save_file` fails (−1) and **leaves no half file** (a file that cannot fit is
removed — the old content was already replaced); a stream's `write` answers the bytes it could
store (−1 for none). The boot log says the size: `ramfs: RAM: volume, up to 128 MB (2900 MB of
page memory free)`.

**Locking.** One re-entrant **sleeping lock** for the volume (an atomic owner, its waiters `Yield`
— as the FatFs volume lock, §5); nothing yields while holding it. A big read or write is cut into
**1 MB slices** (`RAMFS_SLICE`) with a `Yield` between them, the lock free meanwhile — a 16 MB copy
does not hold the other tasks (the kernel is not preempted, §5). A `save_file` in progress is in a
**no-kill** section (`EnterNoKill`): killed between two slices, the task ends once the file is
complete or removed. The file calls run on core 0's tasks (and core 3's with `netcore=1`); an app
core never calls the kernel (§14). Opening a file / listing a folder walks the tree under the lock.

**Speed** (no card): a `save_file` or a `read` is a `memcpy` into / from the pages (GBs a second),
plus the yields between the slices; `ramtest` prints the time of a 16 MB file on the Pi.

**Tests.** On the PC: `sh tools/tests/run_ramfs_test.sh` — `sys/ramfs.cpp` built with
`RAMFS_HOST_TEST` (the pages from the PC's allocator, no scheduler: its few platform calls are at
the top of the file) under AddressSanitizer: thousands of random operations (saves, streams written /
appended / truncated, reads whole / in pieces / after a seek, listings, mkdir, remove, rename across
folders) checked against a model, for 5 seeds; then the edges: the names (case, 127 / 128
characters), a folder not empty, a folder into itself, a file removed while open, a dead process's
handles, small files taking their size rounded to 256 B, a streamed file's tail fitted, a log
appended 300 times, the volume filled (a save that does not fit leaves no half file, the room back
after the removes), and every page given back at the end. On the Pi: `/bin/ramtest` (`ramtest
full` also fills the volume).

---

## Annex — useful constants

| Constant | Value | File |
|---|---|---|
| `KPAGE_SIZE` | 64 KB | layout.h |
| `L2_BLOCK_SIZE` | 512 MB | layout.h |
| `USER_VA_BASE` | 8 GB | layout.h |
| `USER_WINDOW_CANVAS` | 12 GB | layout.h |
| `USER_WALLPAPER_CANVAS` | 13 GB | layout.h |
| `KAPI_TABLE_VA` | 14 GB | kapi_abi.h |
| `USER_STACK_TOP` | 16 GB | layout.h |
| `USER_STACK_SIZE` | 1 MB | layout.h |
| `KAPI_ABI_VERSION` | 71 | kapi_abi.h |
| `RAM:` volume | 128 MB by default (≤ ¼ of the free page memory; `system.ini` `ramfs=`), 32 MB reserve, 16384 files + folders, 128 MB a file | ramfs.h |
| `USER_HEAP_BASE` | 10 GB | layout.h |
| `MAX_TASKS` | 40 | sysconfig.h |
| `ASID` | 8 bits (1..255; 0 = kernel) | layout.h |
| Kernel stack of an app task | 256 KB | kernel.cpp |
| Screen resolution | 1024×768 by default (`cmdline.txt` `width=` / `height=`; changed while running: `screen_set`, v66) | window.h / cmdline.txt |
| `GIMAGE_TRANSPARENT` | `0xFF00FF` | gimage.h |
