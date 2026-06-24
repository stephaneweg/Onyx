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
12. [Post-mortem debug console](#12-post-mortem-debug-console)

---

## 1. Scope: ours vs. Circle

| Written by Onyx | Reused from Circle |
|---|---|
| Per-process address spaces (`mm/addrspace.cpp`) | 64 KB page allocator (`palloc`/`pfree`) |
| Scheduler (`sched/scheduler.cpp`, `sched/task.cpp`) | `taskswitch.S`, mutex/semaphore/event |
| `VBAR_EL1` vectors, trap frame (`arch/aarch64/`) | `InterruptHandler` (GIC + EOI), `FIQStub` |
| ELF loader (`proc/elf.cpp`) | EMMC + FatFs, USB HID |
| ABI table + kapi impl. (`sys/`) | `C2DGraphics` (framebuffer), `CTimer` |
| GUI: rendering, compositor, widgets, skins, dialogs (`gui/`) | `CCharGenerator` (bitmap font) |

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
   `f_mount()` (FatFs). Loading the skins from `SD:skins/` (`wings.bmp`, the cursor
   `mousecur.bin`, `theme.txt`).
7. **USB**: `m_USB.Initialize()` (mouse + HID keyboard, hot-plug).

### `CKernel::Run()` — the service loop

1. Logs machine info.
2. Reads `SD:apps/autostart.txt` and launches one task per line (`LaunchApp`).
   By default: `voronoy` (paints the wallpaper then exits) and `panel` (the shell).
3. Launches the **kernel service tasks**:
   - **`CCompositorTask`** — presents the screen at ~60 Hz (composes the windows then
     `UpdateDisplay()`).
   - **`CReaperTask`** — reaps terminated tasks every ~50 ms (frees
     `CTask` + stack + address space outside the scheduler core).
   - **`CInputTask`** (if USB is present) — pumps keyboard/mouse events to the
     window manager.
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

### Cooperative, not preemptive (on hardware)

The kernel was designed for preemption (100 Hz tick, `OnTimerTick` which counts down a
time slice), **but on hardware preemption from the IRQ is disabled**. The
reason: Circle runs the main thread and **all tasks in EL1t** (with
`SP_EL0`), whereas the IRQ handler runs in **EL1h** (with `SP_EL1`, the shared exception
stack). A context switch from the IRQ would swap `SP_EL1` and not the thread's
`SP_EL0` stack → impossible to preempt correctly.

→ `KernelIRQExit()` does **not** reschedule. **Tasks switch voluntarily**
when they call `Yield()`, `MsSleep()`, `present`, `wait`, etc. Real preemption
would require a context switch with a full save/restore of `SP_EL0` (future
work). The `OnTimerTick` hook therefore stays dormant.

### `CScheduler` (shadow) and `CTask`

- Circle's public + friend API is preserved (`Yield`, `Sleep/MsSleep`,
  `GetCurrentTask`, `AddTask`, `BlockTask`, `WakeTasks`…) → Circle drivers that block
  (USB, etc.) work without modification.
- `Yield()` performs the context switch **with the IRQ disabled** for atomicity, then
  **restores the full `DAIF`** of the resumed task (each task keeps its own IRQ
  enable state).
- The **only** change in `task.cpp`: `TaskEntry()` re-enables the IRQ
  (`msr daifclr, #2`) so that a **fresh task** does not start with the IRQ masked
  inherited from the switcher.
- The idle task uses `wfi`.

### Per-task data

`CTask`'s `TASK_USER_DATA_USER` slot holds the task's `CAddressSpace*` pointer
(0 for kernel tasks). The PID, the state (R/S/B/N), the name and the ASID are accessible via
the address space and the enumeration API (see [§8](#8-the-kapi-abi-table)). The
reaping (`reaper`) calls the termination handler (which frees the address
space) then destroys the `CTask`.

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
tick + EOI) → `KernelIRQExit` (no-op) → re-masks the FIQ → `RESTORE_TRAP` + `ERET`.

> Bring-up note: leaving the FIQ masked during `EnterCritical` caused a Circle
> assertion to fail (`synchronize64.cpp`). Hence the `DAIFClr,#1`/`DAIFSet,#1` around the
> call, as Circle's `IRQStub` does.

### Trap frame

`TTrapFrame` (272 bytes): `x[0..30]`, `sp_el0`, `elr_el1`, `spsr_el1`. Saved by the
assembler macro `SAVE_TRAP` onto the stack on exception entry.

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

`CUserProcessTask` (subclass of `CTask`, **256 KB** stack):
1. Creates a fresh `CAddressSpace`.
2. Installs stdin/stdout, the process handle, argv, cwd.
3. `LoadELF` into the address space.
4. `SetUserData(AS, TASK_USER_DATA_USER)` + `Activate()` (switches `TTBR0`/ASID).
5. **Calls the entry point directly**: `((void(*)())entry)()` — in EL1, in the
   app's page table + stack. No trap.
6. On return, `Terminate()`; the reaper reaps the task and frees the address space.

### Launch entry points

- **`LaunchApp(name)`**: builds `SD:apps/<name>.app/main.elf`, reads it into RAM, creates a
  `CUserProcessTask` (without stdio). Exposed via `kapi_launch`.
- **`SpawnProcess(path, args, stdin, stdout, cwd)`**: creates a `CProcess` handle,
  increments the streams' refs, creates the task. Exposed via `kapi_spawn` (the terminal for
  pipes/redirections). `kapi_wait`/`kapi_proc_done` query the handle.
- **`ExecPath(path, args)`**: "fire-and-forget" (without stdio or handle; the task name
  is derived from the path). Exposed via `kapi_exec` (the file manager to
  open a document in an app, or launch an `.elf`).

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

`KAPI_ABI_VERSION = 25`. The `TKApiTable` struct is **strictly append-only**: you
never remove or reorder a field; you add new ones **at the end** and you
increment the version. An old app only touches the prefix it knows → it
stays compatible. The history of additions is annotated in the file (v1 = `app_dir`,
v2 = `set_click_handler`, v3 = `opendir/readdir`, v4 = streams/spawn, … v16 =
`set_window_theme`, v17 = `chdir`/`getcwd`, v18 = `stdin_stream`/`stdout_stream`,
v19 = `klog_read`, v20 = `set_verbose`/`get_verbose`, v21 = the TCP socket calls,
v22 = `set_pointer_handler`, v23 = `meminfo`, v24 = `sbrk`, v25 = `reboot`).

### Categories of exposed functions

| Category | Examples |
|---|---|
| Windowing | `create_window(_ex)`, `resize_window`, `move_window`, `present`, `exit` |
| Launch/management | `launch`, `toggle_app`, `raise_app`, `exec`, `kill`, `kill_pid` |
| Enumeration | `list_apps`, `list_windows`, `list_tasks`, `list_procs`, `get_datetime` |
| Widgets | `add_button/label/checkbox/textbox/progress/slider/textarea/scrollbar/icon`, `widget_get/set_*` |
| Events | `pump_events`, `wait_for_exit`, `should_exit`, `set_key_handler`, `set_click_handler`, `set_pointer_handler` (full pointer stream, v22) |
| App-drawn text | `draw_text`, `font_width`, `font_height` |
| Files | `open/read/fsize/close`, `save_file`, `opendir/readdir/closedir`, `mkdir/remove/rename`, `chdir/getcwd` (current working directory, inherited by children) |
| Streams/processes | `pipe`, `file_in/out`, `stream_read(_nb)/write/close/eof`, `stdin_read`, `stdout_write`, `spawn`, `wait`, `proc_done`, `get_args` |
| Modal dialogs | `message_box`, `file_open`, `file_save` |
| Desktop | `screen_size`, `set_wallpaper`, `wallpaper_generate`, `wallpaper_buffer`, `wallpaper_commit`, `cursor_pos` |
| Appearance/keyboard | `set_window_theme`, `set_keymap`, `get_keymap`, `app_dir` |
| Logging / memory | `klog_read`, `set_verbose`, `get_verbose`, `meminfo` (total/free/app KB + page size, v23), `sbrk` (per-process heap, v24) |
| Networking (v21) | `net_status`, `tcp_connect`, `tcp_send`, `tcp_recv`, `tcp_close` |
| Power (v25) | `reboot` (restart the machine — applies settings read only at boot, e.g. the WLAN config rewritten by *wpaconf*) |

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

- **`CWindow`**: position, logical size, title, flags (`WIN_FLAG_BORDERLESS`), a
  `GImage` **canvas allocated 64 KB-aligned and physically contiguous** (so it can be mapped into
  the app at `USER_WINDOW_CANVAS` = 12 GB — the app draws directly, with no per-pixel call),
  up to 16 widgets, an event queue (spinlock-protected ring), the app's
  keyboard/click handlers, and a possible modal dialog.
- Decoration: title bar (32 px), border (7 px), close box, drawn via the
  skins (see 10.4). *Borderless* windows (panel) have no decoration.
- **`CWindowManager`** (singleton): Z-ordered list (the last = on top = active),
  protected by a `CSpinLock`. `Add`/`Remove`/`Raise`. `Composite(screen)`:
  1. snapshot the list under the lock, then blit outside the lock;
  2. clears the desktop, blits the wallpaper;
  3. draws the windows from back to front (`DrawTo`, `bActive` for the last one);
  4. draws the cursor last (`mousecur.bin` bitmap or fallback arrow).

> **Known TODO:** the compositor **redraws the entire screen on every frame** (no
> dirty-rectangle optimization). See the "compositor dirty-rect" note.

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

### 10.4 Skins, theme, wallpaper

- **`CSkin`**: "9-slices" BMP skin (fixed corners, repeated edges, center filled with a
  sampled color). `wings.bmp` (window chrome) is loaded **twice** and
  **tinted on the fly** (`Colorize`): an *active* version (warm gold) and an *inactive*
  one (slate) — the compositor chooses according to the focus, without per-pixel tint
  computation.
- **Runtime theme**: `kapi_set_window_theme(active, inactive, text)` re-tints the chrome
  live (used by the theme editor). Persisted by writing `SD:skins/theme.txt` (read at boot).
- **Wallpaper**: `set_wallpaper` (BMP), `wallpaper_generate` (toroidal Voronoi generated
  at runtime), or **an app-drawn background**: `wallpaper_buffer` maps the screen-sized
  shared buffer at `USER_WALLPAPER_CANVAS` (13 GB), the app draws, `wallpaper_commit` makes it
  live. The frames are **owned by the kernel** → the background persists after the app exits
  (the `voronoy` case).

### 10.5 Modal dialogs

`CDialog` (types: `DLG_MSGBOX`, `DLG_FOPEN`, `DLG_FSAVE`). In a cooperative system,
the calling app **yields in a loop** in the kernel as long as the dialog is not
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

**Phase 1 = the whole stack on the primary core.** The user's goal of a *dedicated
core* for the network is deferred (it needs `CMultiCoreSupport` + cross-core socket
queues, a separate sub-project against the single-core cooperative model of §5).

- **Bring-up is non-blocking and non-fatal.** `CNetBringupTask` (a kernel `CTask`,
  started from `Run()` once the SD card is mounted) runs `CBcm4343Device::Initialize`
  → `CNetSubSystem::Initialize(FALSE)` → `CWPASupplicant::Initialize`, waits for the
  DHCP bind, logs `net: up, IP …`, then **returns** (the task ends cleanly). A
  missing firmware / `wpa_supplicant.conf` / access point only logs a warning — the
  GUI boots regardless.
- **Self-driving.** `CNetSubSystem::Initialize` spawns Circle's own `CNetTask` /
  `CPHYTask`; they run as ordinary cooperative tasks on our scheduler (§5), so no
  explicit pumping is needed. (This is also why the net task is CPU-busy — a
  motivation for the future dedicated core.)
- **Globals.** `g_pNet` (the `CNetSubSystem`) and `g_bNetUp` are published in
  `net.h`; `NetIsUp()` gates the socket calls.
- **Sockets.** `sys/net.cpp` keeps a small table of Circle `CSocket`s behind integer
  handles. `tcp_connect` resolves a dotted-quad or a DNS name (`CDNSClient`) and
  connects (blocking, cooperative); `tcp_send` blocks with a 5 s timeout; `tcp_recv`
  is **non-blocking** (so a GUI app polls it from its frame loop); `tcp_close` drops
  it. Each socket records its **owner pid**, and `AddressSpaceTaskTerminate` calls
  `NetCloseByPid` so a process that dies without closing does not leak its
  connections or table slots.
- **Clock.** Once the link is up the bring-up task starts a `CNTPDaemon`
  (`system.ini ntp=`), which updates `CTimer`'s wall clock; the boot reads
  `system.ini timezone=` (minutes from UTC) and calls `CTimer::SetTimeZone` so
  `get_datetime`, the agenda and the log timestamps show local time.
- **Caveats.** Plain-text only (no TLS); `MAX_TASKS` was raised to 40 to fit the net
  workers; the firmware load uses FatFs and is not locked against concurrent app
  file I/O (low risk, one-shot at boot).

The apps that use it: the **irc** client (`user/irc.c`) and the **`net`** `/bin`
tool (link status / IP).

---

## 12. Post-mortem debug console

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
- When the debug console takes over (`DebugConsoleTakeover`, called on `exit`), the
  compositor **detects** `DebugConsoleActive()` and **stops presenting** so as not to
  contend for the framebuffer.

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
| `KAPI_ABI_VERSION` | 25 | kapi_abi.h |
| `USER_HEAP_BASE` | 10 GB | layout.h |
| `MAX_TASKS` | 40 | sysconfig.h |
| `ASID` | 8 bits (1..255; 0 = kernel) | layout.h |
| Kernel stack of an app task | 256 KB | kernel.cpp |
| Screen resolution | 1024×768 (configurable) | kernel.h / cmdline.txt |
| `GIMAGE_TRANSPARENT` | `0xFF00FF` | gimage.h |
