# Onyx: the GUI in user space, and wtk no longer linked into every app (a feasibility study)

*Status (2026-10-04): a study read from the code (kapi v82); nothing is built. The decisions are the
user's: listed at the end. Asked: (1) can the window manager / compositor leave the kernel for a
user-space process; (2) can the apps stop carrying a static copy of wtk — by putting wtk in the
user-space window manager (the apps then talk IPC), by putting wtk in a process of its own, or by
loading shared libraries (ideally one physical copy, mapped into every process that uses it).*

## Summary

| Question | Feasible? | Cost | Recommendation |
|---|---|---|---|
| **GUI (WM + compositor) in user space** | **Yes** — the apps already draw everything themselves; the kernel keeps the window list, the input routing and the composition | large: ~4–5 k lines moved or written, a new protocol, every app to re-test | **yes, in a second step**, on top of the shared libraries |
| **wtk inside the window server** (server-side widgets, X11 / Win32 style) | technically, but not for Onyx's apps | a rewrite of most of the 88 wtk apps | **no** |
| **wtk in a process of its own** | same objection, worse (three parties) | same | **no** (except as a *common-dialogs* service, below) |
| **Shared libraries, fixed address, one physical copy** | **Yes**, most of the machinery exists (v77 program images) | small: ~300 kernel lines, link changes, no ld.so, no toolchain rebuild | **yes, first** |
| **Full dynamic linking** (PIC, `ld.so`, `dlopen`) | yes | large: an `ld.so`, PIC builds of newlib / libgcc / libstdc++, kernel `PT_INTERP` | later, only if `dlopen` plugins are wanted |

The order matters: the shared-library step is cheap, and it is also the vehicle that makes the user-space
GUI compatible with every existing binary (the GUI kapi slots can point into a shared client library
instead of `svc`, see §4.3).

---

## 1. Where things stand (read from the code)

### 1.1 What the kernel does for the GUI

About 3 800 lines in `kernel/gui/` (`window.cpp` 2 107, `gimage.cpp` 375, `surface.cpp` 177, the
cursors 232) + headers (`kern/gui/window.h` 720), ~470 lines of tasks in `kernel/kernel.cpp` (the
compositor task 403–562, the input task 977–1290) and the GUI part of `kernel/sys/kapi.cpp`.
About **60 of the 260 kapi entries** are GUI (windows 21, drawing / surfaces 13, events 16,
screen / full screen 8, wallpaper 3, cursor 2, menus / shell 5).

- **Window list and policy**: `CWindowManager`, `m_pWindows[16]` (`WM_MAX_WINDOWS`), three bands
  (backmost / normal / topmost), **one window per process**, minimise, desks, the work area. Focus
  *is* the z-order (`KeyTargetLocked`: the topmost visible normal window).
- **Composition**: CPU only (`Composite`, `window.cpp:1025`): wallpaper → windows (opaque ones hide
  what is under them) → drag badge → resize outline → software cursor; ≤ 16 merged dirty rectangles;
  out through Circle's 2D DMA (`UpdateDisplayStart` / `Poll`, our patches). No V3D, no vsync, no
  hardware cursor.
- **Input**: Circle's USB callbacks → `OnMouse` / `OnKey`: hit test, raise on press, capture during a
  drag, the title-bar drag, double click, close / minimise, the resize outline, Ctrl+Alt+arrows for
  the desks; per-window event ring of 32 (dropped when full).
- **Pixels drawn by the kernel**: only the cursor, the drag badge, the resize outline, the generated
  wallpaper and `draw_text*` (kernel bitmap font).
- **Full screen** (`fullscreen_begin` / `present_fb` / `fullscreen_direct`), **wallpaper buffer**,
  **screen size change**, **`screen_grab` / `inject_*`** (VNC, rdpd), **menus** (the spec stored,
  served to the menubar app), **drag and drop**, **a clipboard**.

### 1.2 What the apps already do

- **They draw everything**, frames included: wtk paints the title bar and borders into the two chrome
  buffers the kernel maps (`wk_decorate_window`, `user/wtk/skin.cpp:293`). Menus are drawn by the
  menubar *app*, dialogs and tooltips are widgets inside the app's window.
- The canvas is kernel heap, **physically contiguous** (V3D renders into it directly), mapped at
  `USER_WINDOW_CANVAS` (12 GB). The app draws, then `kapi_present()` (whole window, no rectangle).
- Events: the kernel queues `{handler, event, value, mods}`; the user-side pump (`el0blob.S`) pops them
  and calls the app's handlers at EL0. wtk's `Root::run` polls every 16 ms (`should_exit`,
  `pump_events`, draw + `present`, `msleep 16`): ~5 system calls a tick even when idle.
- **The desktop is already user space**: dock, menubar, shelf, notifyd, clipd, applist, agenda,
  taskman, control are wtk processes, talking over kernel mailboxes (512 bytes, named services).
- **Two user-space compositors already exist**: the Control Panel's applets (`applet_proto.h`: the
  host maps a surface the applet draws into, forwards `AP_PTR` / `AP_KEY`, the applet sends
  `AP_PRESENT`; wtk's `Root` has an `--applet` mode) and the *activity shell* (`shell_proto.h`,
  `Apps/shell`, phase 0). Mail's HTML view and Koton's plugins use the same pattern.

### 1.3 Memory, loader, IPC

- 64 KB pages, per-process page tables with ASIDs, user space [8 GB, 60 GB), **[16 GB, 32 GB) unused**
  (`kern/layout.h:109`).
- A frame can be mapped *not owned* in several address spaces (`PAGE_SW_OWNED`); the owner object keeps
  the reference count: **program images (v77)**, shm objects (v76), surfaces, the EL0 pages.
- **Program images** (`kernel/proc/image.cpp`): one `TImage` per program path; the read-only segments'
  frames are read once and mapped into every process of that program; the writable segment's file bytes
  are kept and copied into fresh private pages per process; reference counted, pinnable (`preload`).
  **Limit: one image per address space** (`ImageMap`: `pAS->GetImage () != 0` → refused,
  `image.cpp:449`).
- **The loader is static only**: `ET_EXEC` (and `ET_DYN` taken as is), no relocations, no
  `PT_DYNAMIC`, no `PT_INTERP`; apps are linked `-fno-pic -fno-pie` at 8 GB (`user/user.ld`).
- **The kapi table is already a vDSO**: one read-only table page at 14 GB and one shared code page
  (`svc` stubs + `el0blob.S`: `memcpy`, `memset`, `pump_events`, `pump_wait`...) mapped in every
  process; **some slots already point to user-side code**, not to `svc`.
- **IPC**: mailboxes (32 × 512 bytes per process, no handles, no back-pressure, a global wake
  generation); local sockets v76 (`sock_pair` only: **no bind / connect by name**), carrying up to 256
  handles a message (files, sockets, **shm objects**); shm objects anonymous or named, lazy 64 KB
  frames, **not physically contiguous**, mapped at a per-process address.
- System calls run on core 0, the kernel is not preemptive, EL0 is preempted at 100 Hz.

### 1.4 wtk

- `user/wtk`: 70 files, ~10 000 lines, a static `libwtk.a` compiled once and linked into the
  freestanding apps, the newlib / FreeType apps and the Koton plugins; Jet compiles it a third time
  (hosted toolchain, `-DONYX_HOSTED_NEW`).
- **In each app: 40–150 KB of code + rodata** (tetris 41 KB, tinypad 97 KB, widgets 136 KB, sheet
  147 KB), + ~92 KB of FreeType in the FreeType apps, + 20–70 KB of bss.
- C++ without exceptions / RTTI / STL; a `Widget` base with public fields and virtuals; callbacks are
  plain function pointers; global theme colours (`extern unsigned C_BG...`), a global text face,
  function-local statics; `operator new` / `delete` **defined by the app** (`onyxpp.hpp`) and resolved
  by `libwtk.a` at link time.
- **Customisation is heavy**: 88 apps on wtk, **418 classes derived in app code, 304 `onDraw`
  overrides in 56 apps**; ~19 apps use wtk only as a canvas / font / frame helper around their own
  drawing (games, menubar, lock); emulators blit their frames themselves.
- Churn: every one of the recent wtk commits changed headers (frame resize, cursors, DataGrid) and the
  packages were republished with *every app rebuilt on the new wtk*.

---

## 2. The GUI in user space

### 2.1 Target

A **display server** process (call it `wsd`, started first by `init`) owns the composition and the
window policy. The kernel keeps the drivers and the emergencies:

| Stays in the kernel | Goes to `wsd` |
|---|---|
| the frame buffer, the 2D DMA present, the resolution change (firmware), EDID | the window list, z-order, bands, desks, focus, minimise |
| the USB input drivers, keymaps, gamepads, MIDI | the routing of pointer and keys, captures, the title-bar drag, resize, double click |
| the panic screen and the debug console (they take the display back) | the composition (dirty rectangles, alpha, rounded corners, cursor) |
| the memory behind the canvases (shm / surfaces) | wallpaper, full screen arbitration, drag and drop, menus' spec, `screen_grab` for VNC / rdpd |

### 2.2 What the kernel must add

1. **A display device for one privileged process** (~200 lines): `display_open` (exclusive, `wsd`
   only), map the frame buffer (`MapScreen` exists for `fullscreen_direct`) or a back buffer, a
   `display_present (rects)` that starts the 2D DMA and returns, `display_wait` (DMA end), the
   resolution change as today's `ScreenResizeRequest`. The compositor task stays only as a fallback
   (no `wsd`, or `wsd` dead).
2. **An input channel** (~150 lines): the input task writes raw events (pointer absolute / relative,
   wheel, key codes + modifiers, held keys) into a ring read by `wsd` (a local socket or a stream);
   `inject_*` become writes into the same ring (so VNC / rdpd keep working) — restricted to `wsd`
   and the remote-desktop daemons.
3. **Named rendezvous for local sockets** (~200 lines): `sock_listen ("gui")` / `sock_connect
   ("gui")` (or a mailbox service that hands back a socket end). Today an unrelated client can only
   get a socket from `spawn_ex2` or another socket. Mailboxes are not enough: no handles, 32 slots,
   messages dropped when full.
4. **Contiguous shm** (~100 lines): a `SHM_CONTIGUOUS` flag (or surfaces made passable as handles) so
   a window canvas created by `wsd` can still be a V3D render target (contiguous, below 1 GB) and is
   mapped in both the client and the server. The canvas grows by a new object + remap (today's
   `Grow` logic, three frames late free, moves to `wsd`).
5. **Privileges** (~100 lines): who may open the display, read input, grab the screen, inject — today
   *any* process may `screen_grab`, `inject_*`, `win_close` (a hole the move closes).
6. **Scheduling**: `wsd` must wake at once on input and on `present`. With a non-preemptive kernel and
   a 100 Hz tick, a woken `wsd` behind a busy app could wait up to 10 ms: it needs a priority (or a
   core of its own, as the app cores already allow) and a direct wake (not the global I/O generation).

### 2.3 The protocol

A local `SEQPACKET` socket per client, the canvas and chrome as shm handles in the messages:

- client → server: `CREATE (w, h, flags)`, `PRESENT (rects)` (a damage list at last: today it is
  the whole window), `MOVE`, `RESIZE`, `ALPHA`, `CURSOR`, `MENU (spec)`, `DRAG_BEGIN`, `FULLSCREEN`,
  `CLOSE`...;
- server → client: `CANVAS (shm handles, stride)`, `EVENT (pointer / key / winctl / resize /
  display resize...)`, `FRAME_DONE` (to pace the client instead of a 16 ms poll).

This is `shell_proto.h` / `applet_proto.h` grown up: those already prove the pattern (a surface the
client draws, input forwarded, present messages) on Onyx.

### 2.4 Compatibility: no app rebuilt

The ~60 GUI kapi slots must keep their behaviour (the ABI is append-only). The kapi table page is
shared and some slots already point into user-side code (`pump_events`, `memcpy`). So:

- the GUI slots point into a **client library `libgui`** (user code, mapped in every process) that
  translates each call into the protocol: `create_window` → connect + `CREATE` + map the canvas at
  `USER_WINDOW_CANVAS` as today; `present` → `PRESENT`; `pump_events` → read the socket, call the
  handlers as today; `win_list`, `desk`, `raise_app`... → requests to `wsd`;
- the per-process state of `libgui` (socket, handlers, mods) needs a small private data page at a
  fixed address — exactly what a fixed-address shared library gives (§4.3): **`libgui` is the first
  shared library**.
- the 64 KB code page of the EL0 blob is too small for it; the shared-library mechanism removes that
  limit.

The 88 apps, BASIC programs, emulators and Jet then run unchanged; new apps can use the protocol's new
features (several windows per process, damage rectangles, `FRAME_DONE`).

### 2.5 Cost, gains, risks

- **Size**: `wsd` ≈ 3 000–3 500 lines (most of `window.cpp` / `gimage.cpp` moves almost as is: it is
  already C++ over plain buffers; at EL0 it may use NEON, which the kernel compositor does not);
  `libgui` ≈ 1 000–1 500; kernel additions ≈ 750, removals ≈ 3 000 once the fallback is reduced to a
  minimal one. Several sessions, plus a full test pass on the Pi (every app, full screen, GPU apps,
  VNC / rdpd, the dock, the menubar, drag and drop, the desks, a resolution change).
- **Gains**: a WM bug kills `wsd`, not the kernel (and `wsd` can be restarted: the canvases are shm
  objects held by the clients, the server takes them again); the WM evolves without a kernel rebuild;
  the 16-window and one-window-per-process limits go; real privileges on capture and injection; the
  kernel shrinks; the menubar / dock / shelf can be merged into the server or keep their roles with a
  real protocol.
- **Costs at run time**: per frame, one message and one wake of `wsd` (instead of a flag the kernel
  polls); per input event, one more context switch. Small if `wsd` wakes at once (§2.2-6); the pixels
  copied are the same.
- **Risks**: the input latency (the cursor: today drawn by the kernel; a hardware cursor plane via the
  firmware would make it independent of `wsd` — worth trying first); the boot (the boot console until
  `wsd` runs); the panic / debug console taking the display back from a process; full-screen direct
  apps and the GPU paths (`fullscreen_direct`, V3D into the canvas); the clipboard and drag-and-drop
  data, kernel statics today.

---

## 3. wtk out of the apps: the three ways asked

### 3.1 wtk inside the window server (server-side widgets) — not recommended

The X11 / Win32 model: the app asks the server for a button, a list, a text box; the server draws them
and sends back high-level events.

- **It would cover only the stock controls.** 304 `onDraw` overrides and 418 derived classes in the apps
  (Calendar's grids, Sheet, the emulators, the games, Paint, Slides, Koton...) draw their own pixels:
  they would still need a client canvas, and a hybrid of server widgets around client canvases is
  harder than either.
- **Every interaction becomes a round trip**: a text box's caret, a list's scroll, a data grid's
  selection, layouts, measured text — today function calls, then IPC through core 0's system calls.
- **The API changes**: apps touch widget fields directly (`left`, `width`, `canvas.px`, `valid`,
  `hidden`) and subclass with virtuals; a server-side model means proxy objects and a rewrite of most
  of the 88 apps.
- **The server becomes fragile**: a toolkit bug in one app's dialog kills the desktop.

### 3.2 wtk in a process of its own — not recommended

The same objections, with three parties (app ↔ wtk process ↔ window server) instead of two: twice the
round trips, the same rewrite. What *does* make sense as separate processes are **common dialogs**
(file open / save, colour picker, message boxes, the font chooser): one process draws them, the app
gets back a result (the pattern of `ask` and of the applets). That saves some code in the apps and
gives one look, but it does not remove wtk from them.

### 3.3 Shared libraries — recommended, at a fixed address first

#### Variant A: fixed-address shared libraries (no `ld.so`, no PIC)

The model of a.out shared libraries / Windows DLLs at their preferred base:

- **`libwtk` is linked once, as a static ELF, at a fixed address** in the unused [16 GB, 32 GB)
  (e.g. one 256 MB slot per library and major version: 16 GB `libgui`, 16.25 GB `libwtk.1`,
  16.5 GB `libft.1`...), with its own `user.ld` (RX segment + RW segment, as apps).
- **The apps link against its symbols without its code**: `-Wl,--just-symbols=libwtk.1.elf` (or `-R`):
  calls are plain `bl` / `adrp` to absolute addresses — no PLT, no GOT, **no run-time cost**.
- **The kernel maps it**: the app carries a note (a `PT_NOTE` or a `.onyx.needs` section: the library
  paths) read by `ElfReadPlan`; `ImageMap` accepts a **list** of images per address space instead of
  one (`m_pImage` → a small array): each library is a `TImage` like a program — **its read-only pages
  one physical copy for every process**, its RW segment a private copy per process, preloadable,
  reference counted, dropped when its file changes. This is the existing v77 code, generalised:
  ~300 lines + host tests, **no new fault path, no toolchain rebuild**.
- **Initialisation**: crt0 walks the libraries' `init_array` before the app's (the library exports
  `__lib_init_array_start/end`; or the kernel passes their addresses).
- **What wtk needs from the app**: today `operator new` / `delete` come from the app (freestanding:
  `umm`, newlib: `malloc`). A fixed-address library cannot reference app symbols, and a widget made by
  the app and deleted by wtk must use one allocator. Two answers: (1) an **import table** in the
  library's RW data that crt0 fills (`new`, `delete`, `malloc`, `free`) — 4 function pointers; or (2)
  one allocator for everybody, in a `libonyx` (the kapi wrappers + `umm`) that newlib's `malloc` also
  uses. (1) is a day's work; (2) is cleaner later.
- `memcpy` / `memset` emitted by GCC already resolve to the kapi table's slots (fixed VA): unchanged.
- The header-only parts (`ft/fonts.h`, `ft/wtkface.h`, inlines of `widget.h`) stay compiled into the
  apps unless moved into `.cpp` files.

**The real limit is the ABI, not the loader.** A fixed address freezes *where* every function and
global is, and C++ freezes the class layouts: a field or a virtual added to `Widget` changes 418 app
classes. So:

- **versions side by side**: `libwtk.1` and `libwtk.2` at two addresses, each app naming the one it
  was linked with; a version costs memory only while an app uses it. The packages carry the
  libraries; `pkg` resolves the needs (`packages.ini` already has `needs`).
- **compatible updates** (bug fixes, new non-virtual functions, new classes) keep the same major only
  if every existing symbol keeps its address: link the library with a **jump table / fixed symbol
  order** (an ordered `.text.export` section, or a table of entry points like `TKApiTable`), and new
  globals only at the end of the RW segment. That is discipline the kapi ABI already follows
  (append-only).
- **class layouts**: reserve fields and virtual slots in `Widget` / `Root` / `Modal` at the next
  major, and move new state behind a pointer (`Widget::ext`), so additions do not change the layout.

Given today's churn (every wtk change touched headers, every app rebuilt), the first months would be
"a new major at each wtk change, every app rebuilt" — as now — with the gain in memory and on the
card; the update-without-rebuild benefit comes once the API settles.

#### Variant B: real dynamic linking (PIC, `ld.so`, `dlopen`)

- An `ld.so` in user space (`R_AARCH64_RELATIVE`, `GLOB_DAT`, `JUMP_SLOT`, `ABS64`, the TLS
  relocations of the POSIX toolchain), symbol lookup, versioning; the kernel loads `PT_INTERP`; libc's
  stubbed `dlopen` becomes real.
- **The toolchain's libraries rebuilt PIC**: newlib, libgcc, libstdc++ (both toolchains are built
  `--disable-shared`), every library `-fPIC`.
- Cost at run time: GOT indirections (a few % in calls / globals), relocation at start (small for wtk,
  large for WebKit-size libraries), one more private page or two per library (GOT, data).
- Gains over A: no address slots to manage, symbol versioning, **`dlopen`** (plugins in-process:
  Koton's plugins, codecs) — Onyx's plugins are processes today, by design (a plugin's crash is not
  the host's).
- Weeks, not days. **Worth it only if in-process plugins are wanted**; A's kernel part (images per
  address space) is the base it would build on anyway.

#### What it saves

Measured on the binaries: **40–150 KB of wtk + ~92 KB of FreeType per app**. With 64 KB pages, that is
**one to four pages a process** of RAM — with fifteen GUI processes running, **1–4 MB**: modest on a
1 GB Pi, real on the card (~10 MB with FreeType over the ~90 apps) and in package downloads (a wtk fix
no longer republishes 90 apps once the ABI is stable). The larger wins are elsewhere and come with the
same mechanism: **newlib, FreeType, mbedTLS (Mail, Courier, Jet, pkg, curl...), FFmpeg (Media, Jet),
libstdc++ / ICU for the POSIX ports** — mbedTLS and FFmpeg weigh hundreds of KB to several MB each in
several binaries. Licences: a shared FFmpeg / MuPDF does not change what is linked with what (LGPL
even prefers dynamic linking: docs/LICENSING.md to update).

---

## 4. Recommended path

1. **Images per address space + fixed-address libraries (variant A)** — kernel (`image.cpp`, `elf.cpp`,
   the address space's image list, the needs note), a `lib.ld`, crt0 calling the libraries'
   constructors, the import table, `make` rules (`libwtk.1.elf`, apps with `--just-symbols`), `pkg`
   needs. **First library: `libwtk` + FreeType** (the measurable case), then mbedTLS. Host tests as for
   v77. A kapi call only if the libraries' list is queried (`image_list` shows them).
2. **The GUI's kernel pieces**, usable on their own: named local sockets, contiguous shm, privileges on
   `screen_grab` / `inject_*`, the display and input devices for one process, a hardware cursor trial.
3. **`wsd` + `libgui`**: the window manager moved out, the GUI kapi slots redirected into `libgui`
   (the first library mapped into *every* process), the kernel compositor kept as the fallback; the
   whole test pass on the Pi; then the kernel WM removed.
4. Optional: **common dialogs** as a service; **variant B** if in-process plugins are ever wanted.

## For the user to decide

- Whether to do step 1 now (cheap, safe) and the GUI move (step 3) later — or not at all: the GUI in
  the kernel works, and the gain of step 3 is robustness and freedom more than speed.
- Fixed addresses (A) or full dynamic linking (B); with A, the address slots and the versioning rule
  (a new major at each layout change; every app rebuilt then, as today).
- The allocator answer for libraries: an import table now, or one shared `libonyx` allocator.
- Which libraries after wtk / FreeType: mbedTLS, newlib, FFmpeg.
- For the GUI: one process (`wsd` absorbing the menubar / dock policy) or `wsd` + the existing desktop
  apps; a hardware cursor; `wsd` on a core of its own.

## Not verified

No timing on the Pi: the wake latency of a user-space server under load, the cost of `PRESENT`
messages at 60 Hz × several windows, the hardware cursor through the firmware. The savings are from
`nm` on the committed binaries (unstripped), not from resident-memory counters. The line counts of the
estimates are estimates.
