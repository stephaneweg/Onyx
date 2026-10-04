# Onyx: the GUI in user space, and wtk no longer linked into every app (a feasibility study)

*Status (2026-10-04): a study read from the code (kapi v82); nothing is built. The decisions are the
user's: listed at the end. Asked: (1) can the window manager / compositor leave the kernel for a
user-space process; (2) can the apps stop carrying a static copy of wtk — by putting wtk in the
user-space window manager (the apps then talk IPC), by putting wtk in a process of its own, or by
loading shared libraries (ideally one physical copy, mapped into every process that uses it).
**Revised the same day**: the user ruled out libraries at a link-time fixed address; §3.3 is now the
user's design — PIC libraries that publish their entry points in a table filled at load time, read by
the apps as they read `kapi`'s. **Decided**: the plan to implement is `docs/SHARED-LIBS-PLAN.md`.*

## Summary

| Question | Feasible? | Cost | Recommendation |
|---|---|---|---|
| **GUI (WM + compositor) in user space** | **Yes** — the apps already draw everything themselves; the kernel keeps the window list, the input routing and the composition | large: ~4–5 k lines moved or written, a new protocol, every app to re-test | **yes, in a second step**, on top of the shared libraries |
| **wtk inside the window server** (server-side widgets, X11 / Win32 style) | technically, but not for Onyx's apps | a rewrite of most of the 88 wtk apps | **no** |
| **wtk in a process of its own** | same objection, worse (three parties) | same | **no** (except as a *common-dialogs* service, below) |
| **Shared PIC libraries behind an export table** (the user's design: one physical copy, the table filled at load, apps not bound to a build) | **Yes**, the loading on the v77 program images; no `ld.so` | medium: ~400 kernel lines, a table generator, wtk's layouts and virtual order frozen (C++) | **yes, first** |
| *Libraries at a link-time fixed address* | yes | small | **ruled out by the user** (apps bound to one build) |
| **Full dynamic linking** (`ld.so`, symbol lookup, `dlopen`) | yes | large: an `ld.so`, PIC builds of newlib / libgcc / libstdc++, kernel `PT_INTERP` | not needed; only for unmodified ports expecting ELF shared objects |

The order matters: the shared-library step comes first, and it is also the vehicle that makes the user-space
GUI compatible with every existing binary (the GUI kapi slots can point into a shared client library
instead of `svc`, see §2.4 and §3.3.2).

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
- `libgui` is a shared library (§3.3) loaded into **every** process at start; since the kernel places
  a library once for the system and knows its export table, it writes `libgui`'s entry addresses into
  the GUI slots of the `kapi` table page; `libgui`'s per-process state (socket, handlers, mods) is its
  private RW segment. **`libgui` is the first system library.**
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

### 3.3 Shared libraries — recommended: PIC libraries reached through an export table

**The user's decision (2026-10-04): no library at a link-time fixed address.** A library is PIC code
that **publishes its entry points in a table of pointers**, filled when it is loaded; an app does not
link against any binary of the library: it asks for the library's table and calls through it — the
model of `kapi` (`TKApiTable` at `KAPI_TABLE_VA`, the `KT->x (...)` wrappers of `user/kapi.h`), applied
to user-space libraries. A new build of the library, at whatever addresses, serves every app built
before it, as long as the table only grows.

#### 3.3.1 The library

- **Built PIC**: `-fPIC -fvisibility=hidden`, linked as a position-independent image with no
  imports (`-shared -Bsymbolic`, or `-static-pie --no-dynamic-linker`): its code reaches its own
  functions and data PC-relatively (`adrp` + `add`), so **its text needs no patching and is shared as
  is**. The only relocations left are **`R_AARCH64_RELATIVE`** (base + addend) in its data: the
  export table itself, the C++ vtables, function-pointer arrays, pointers to strings. The loader
  refuses anything else (no symbol lookup at all: nothing like `ld.so`).
- **Its export table**, the library's only exported symbol, versioned like `kapi`:

  ```c
  struct TWtkTable {                 // wtk/wtk_abi.h -- append-only, never reorder / remove
      unsigned version, size;        // what this build provides
      int  (*init) (const TLibImports *imp);  // the app's allocator, once per process
      void (*widget_ctor) (Widget *, int, int, int, int);
      void (*widget_invalidate) (Widget *, bool);
      ...                            // ~350 entries for today's wtk (§3.3.3), generated
  };
  ```

- **Its imports go through the same kind of table, the other way**: the kernel through `KT` (already a
  fixed table, nothing to resolve); the app's allocator (`operator new` / `delete`, `malloc` / `free`:
  a widget made by the app and freed by wtk must use one allocator) passed to `init` as a
  `TLibImports` table; another library (FreeType for wtk) by asking for *its* table.
- **Its constructors** (`init_array`) run inside `init`, once per process.

#### 3.3.2 The loading

- **Kernel** (on the v77 program images, ~400 lines + host tests): a library is a `TImage` like a
  program — its read-only segment read once, **one physical copy** mapped *not owned* into every process
  that uses it, its RW segment a private copy per process; reference counted, preloadable
  (`/etc/preload.ini`), dropped when its file changes (the existing hook). What changes: an address space
  holds **a list of images** instead of one (`image.cpp:449`), a library arena (the unused
  [16 GB, 32 GB)), `ET_DYN` accepted with its `PT_DYNAMIC` read for the `RELATIVE` relocations.
- **Where it goes**: the kernel picks the library's address **when it loads the image**, once for the
  system (a free slot in the arena), and applies the `RELATIVE` relocations **once, to the image's copy
  of the RW bytes** — so a process start copies already-relocated data, as for a program today, with
  no relocation work per process. This is not a link-time address: the next build of the library, or
  the same file on another boot, may get another place. (The relocated read-only-after-relocation part
  — vtables, the table, `.data.rel.ro` — is then identical in every process and can be *shared* too,
  read-only.) A per-process place stays possible (PIC allows it: then each process relocates its copy),
  but nothing needs it; one place per system is also what lets the kernel point `kapi` slots into a
  library (`libgui`, §2.4).
- **How an app finds it**: `kapi_lib_open ("wtk", min_version)` (a new kapi call) maps the library into
  the caller (loading it if no process has), returns its table — or 0 / an error when the library is
  missing or older than `min_version` (the app says so and exits, as with an old kernel). The app-side
  header keeps the table in a global, as `KT`: `#define WTK (wtk_table)`. Optionally the app names its
  libraries in an ELF note and the kernel maps them before `_start` (no start-up call; same table).
- **A running process keeps the image it opened** (reference count): a package update replaces the file,
  the new processes get the new build, the old ones finish on the old one.

#### 3.3.3 wtk behind a table: what C++ adds

The table solves *where the functions are*. It does not solve what C++ exposes **besides** functions,
and wtk is C++ that the apps subclass (418 derived classes, 304 `onDraw` overrides) and whose fields
they read and write (`left`, `width`, `canvas.px`, `valid`, `hidden`...):

1. **Every non-inline method becomes a table entry**, called by an inline thunk in the header that
   stays in the app: `void Widget::invalidate (bool r) { WTK->widget_invalidate (this, r); }`. About
   **330 method declarations + 22 `extern` globals** in today's headers: the table, the thunks and the
   library's side are **generated** from one list (a script, as `kapi_names.h` is), never written by
   hand. Globals (the theme colours `C_BG`..., the text face) become one struct reached through the
   table (`WTK->theme->bg`, the old names kept as macros or inline references).
2. **The vtables**: an app's class derived from `Button` has *its* vtable in the app; the slots it does
   not override point to the app-side thunks of the base methods (no address of the library needed in
   the app). The library calls `w->onDraw ()` through the object's own vtable — the app's for an
   app-made widget, the library's for one it made (a dialog's buttons). Both work **as long as the slot
   order is the same**: **the order of the virtuals of `Widget`, `Root`, `Modal`... is ABI.**
3. **The layouts**: objects made by the app are sized by the app's header, the library writes their
   fields: **the size and the field offsets of every exposed class are ABI.**

So the ABI of a wtk library is: **the table (append-only) + the layouts + the virtual order**. The rule
that keeps old apps working with new builds, as for `kapi`:

- new functions, new classes: **appended** to the table — always compatible;
- **reserved space now**, before the first release: a few spare virtual slots at the end of `Widget`,
  `Root`, `Modal` (25 virtuals today) and spare bytes plus an `ext` pointer in each exposed class; new
  state goes into the reserve or behind `ext` (allocated by the library), new virtuals into a spare
  slot;
- a change that breaks a layout anyway is a **new library name** (`wtk2`), living beside the old one
  (memory paid only while an app uses it) — the escape hatch, not the rule;
- a build-time check (a host test with `offsetof` / `sizeof` and the virtual order written in the ABI
  header, as `kapi_abi.h`'s `static_assert`s would) refuses an accidental change.

What remains compiled into the apps: the header inlines (`widget.h`'s thumb arithmetic, `WkFaceScope`,
`ft/fonts.h`, `ft/wtkface.h`) — small, and they call the table where they need the library. Moving the
header-only FreeType code into the library (`libft` behind its own table, or inside wtk's) is part of
the work if FreeType is to be shared.

*The cleaner alternative* — a C API with opaque handles (Win32 / GTK style) and a header-only C++
wrapper — frees the layouts entirely, but every app that touches a field (most of them) changes: a
rewrite of the apps' UI code, not a recompilation. Not proposed.

#### 3.3.4 The rest of the build

- The apps stay **non-PIC at 8 GB** (`user.ld` unchanged): only the libraries are PIC. An app's calls to
  the library are indirect (`ldr` + `blr` through the table: one load more than a `bl`, as for every
  `kapi` call today).
- The freestanding and the newlib apps share one wtk library (wtk uses no libc; its allocator comes
  from the importer). Jet's hosted build of wtk can stay static, or use the library — to decide when the
  rest works.
- **To verify with the toolchain** (not installed in this session): that `aarch64-none-elf` `ld`
  produces the PIC image (binutils' `aarch64elf` emulation has the shared and PIE scripts), and that the
  few `libgcc` helpers wtk pulls in link PIC (AArch64's non-PIC code is mostly PC-relative already).
- `pkg`: a library is a package (`needs = wtk`), installed under `SD:/lib/`; preloaded at boot like the
  WebKit programs.
- The same mechanism serves FreeType, mbedTLS, newlib, FFmpeg later — each behind its own generated
  table (a C library's table is easy: no layouts beyond the structs it already publishes). **Full
  dynamic linking** (`ld.so`, symbol lookup, `dlopen`) is not needed for any of it; it would only matter
  for unmodified third-party code expecting ELF shared objects (the POSIX ports): it could be added later
  on the same kernel images.

#### 3.3.5 What it saves

Measured on the binaries: **40–150 KB of wtk + ~92 KB of FreeType per app**. With 64 KB pages, that is
**one to four pages a process** of RAM — with fifteen GUI processes running, **1–4 MB**: modest on a
1 GB Pi, real on the card (~10 MB with FreeType over the ~90 apps) and in package downloads (a wtk fix
no longer republishes 90 apps: they read the new build's table). The larger wins are elsewhere and come with the
same mechanism: **newlib, FreeType, mbedTLS (Mail, Courier, Jet, pkg, curl...), FFmpeg (Media, Jet),
libstdc++ / ICU for the POSIX ports** — mbedTLS and FFmpeg weigh hundreds of KB to several MB each in
several binaries. Licences: a shared FFmpeg / MuPDF does not change what is linked with what (LGPL
even prefers dynamic linking: docs/LICENSING.md to update).

---

## 4. Recommended path

1. **Shared PIC libraries behind an export table (§3.3)**, in this order:
   a. the kernel: several images per address space, the library arena, `ET_DYN` + `RELATIVE`
      relocations applied once to the image, `kapi_lib_open` (kapi v83); host tests as for v77; a
      trivial C library (`libdemo`: a table of three functions) and an app that opens it, on the Pi;
   b. the generator (one list → the table header, the app-side thunks, the library's table) and a
      **C library first: FreeType** (`libft`: no layouts to freeze);
   c. **wtk**: its ABI frozen (reserved fields and virtual slots, `ext`, the globals in one struct, the
      `offsetof` / virtual-order check), the generated thunks, every app rebuilt once on it; then a wtk
      change that does not touch the layouts is shipped **without rebuilding any app**;
   d. then mbedTLS, newlib, FFmpeg as wanted.
2. **The GUI's kernel pieces**, usable on their own: named local sockets, contiguous shm, privileges on
   `screen_grab` / `inject_*`, the display and input devices for one process, a hardware cursor trial.
3. **`wsd` + `libgui`**: the window manager moved out, the GUI kapi slots redirected into `libgui`
   (the first library mapped into *every* process), the kernel compositor kept as the fallback; the
   whole test pass on the Pi; then the kernel WM removed.
4. Optional: **common dialogs** as a service; full dynamic linking only if unmodified ports need it.

## For the user to decide

- Whether to do step 1 now (cheap, safe) and the GUI move (step 3) later — or not at all: the GUI in
  the kernel works, and the gain of step 3 is robustness and freedom more than speed.
- ~~Fixed addresses or dynamic linking~~ — **decided (2026-10-04): PIC libraries behind an export table
  filled at load (§3.3)**.
- One place per library for the whole system (recommended: no relocation per process, the relocated
  vtables shared, `kapi` slots can point into `libgui`) or a place per process.
- The libraries found by a call (`kapi_lib_open`) or named in the app's ELF note and mapped before
  `_start` (or both).
- How much reserve to freeze into `Widget`, `Root`, `Modal` (spare virtual slots, spare bytes, `ext`),
  and the name rule for a breaking change (`wtk2` beside `wtk`).
- The allocator for libraries: the importer's (`TLibImports`, proposed) or one shared `libonyx`
  allocator.
- Which libraries after wtk / FreeType: mbedTLS, newlib, FFmpeg.
- For the GUI: one process (`wsd` absorbing the menubar / dock policy) or `wsd` + the existing desktop
  apps; a hardware cursor; `wsd` on a core of its own.

## Not verified

No timing on the Pi: the wake latency of a user-space server under load, the cost of `PRESENT`
messages at 60 Hz × several windows, the hardware cursor through the firmware. The savings are from
`nm` on the committed binaries (unstripped), not from resident-memory counters. The line counts of the
estimates are estimates.
