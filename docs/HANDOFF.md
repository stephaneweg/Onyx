# Handoff — where the work stands (for the next session)

Written at the end of a long cloud session so that a new session (e.g. a local one on the
user's Windows PC) can continue. Read `CLAUDE.md` first, then this. The user writes in French;
answer in French. The docs stay in English.

## Elegant, the graphics server in a user process (2026-10-05, branch `UserSpaceElegant`) -- stages 1, 2 done; 3: the whole desktop runs on Elegant in a one-boot trial

**The server is named Elegant** (`SD:/bin/elegant`, sources `user/Servers/elegant/`). Work on the branch
`UserSpaceElegant` only: commit and push there, merge `origin/main` into it, merge it into `main` only when
the user says so -- and **do not publish packages from it** until then (the Pi is fed for the tests through
`ftpd`: `run SD:/bin/ftpd SD:/` over telnet, then an FTP upload to `bin/elegant`).

**Decided by the user (2026-10-05):**

- **Fallback**: the kernel keeps a minimal console (boot, panic, debug); if Elegant dies it takes the
  display back and starts Elegant again. The whole window manager leaves the kernel.
- **Migration**: behind a **one-boot trial file** (as the network trials): one start on Elegant, the next
  one back on the kernel's window manager; the default once the test pass is done; then the kernel's
  window manager is removed.
- **Events**: the **per-process event queue stays in the kernel**, as a mechanism; Elegant pushes into
  it. The pump (`el0blob.S`), `kapi_post` and `kapi_pump_wait` do not change.
- **The protocol is AppKit's** (private to it, shared with Elegant; no program includes it): its `kapi_*`
  window names cannot move, UIKit depends on AppKit, `vncd` / `rdpd` / `plasma` / `gpcdemo` have no UIKit.
  What Elegant brings that is new (several windows a process, damage rectangles, a frame-done pace) is
  shown to the programs by UIKit.
- **`rdpd` stays a process of its own** (and `vncd`), with an access to what it needs: a capture channel
  of Elegant (the windows' content -- their buffers mapped read-only, the damage as it happens) and the
  injection of the network client's clicks and keys. A network parser's fault must not take the display
  down.

**The design** (the study `docs/GUI-USERSPACE-STUDY.md` §2, revised with the kits): the kernel keeps
mechanisms, Elegant has all the policy (the window list, z-order, focus, desks, input routing, drag and
drop, menus' specs, composition, cursor, wallpaper). The client side is the bodies of AppKit's window
calls (`appkit_calls.inc`): no program is rebuilt. Three paths: the rare requests (create, move, resize,
menu) by a local socket; `kapi_present` writes its damage into a shared block and wakes Elegant (no
message); what is read often (pointer, modifiers, held keys, screen size, desk) from a state page Elegant
publishes (no call). Full screen: the kernel keeps its buffer and the direct mode (no round trip a
frame), Elegant only says who owns the display. The clipboard stays where it is.

**What the kernel lacks** (inventory of 2026-10-05; stage 2, kapi v89):

1. window buffers that are physically contiguous (V3D's direct path), mapped by Elegant too, and
   unmappable (surfaces: no unmap, 64 at most, 4 users; shm: not contiguous);
2. a raw input ring for Elegant (Circle's callbacks call the kernel's `CWindowManager` today), with a
   wake at once; `kapi_inject_*` then become requests to Elegant;
3. a display device for Elegant: the back buffer mapped, rectangles sent by the 2D DMA, the wait for its
   end, the resolution change;
4. named local sockets (`bind` / `listen` / `connect` answer `EOPNOTSUPP`; mailboxes are 31 messages of
   512 bytes, dropped when full);
5. a "display server" role (nothing is privileged today: any process injects, grabs, closes);
6. a way for Elegant to push into another process's event queue.

**The stages** (each keeps the Pi bootable):

1. **Done**: the kernel's window manager built for a user process. `user/Servers/elegant/`: `port/`
   (stand-ins for the few Circle headers `kernel/gui/window.cpp` and `gimage.cpp` include -- the sources
   themselves are the kernel's, compiled from `kernel/gui`, one code until the kernel's copy goes),
   `core.h` / `core.cpp` (the window manager behind plain functions; the only file that includes
   `kern/gui/window.h`), `main.cpp`. **`elegant --demo`** takes the full screen as any program may and
   shows three windows of its own, composed by its window manager from the pointer the kernel sends it.
   **Tried on the Pi (onyx 2026.10.87, over VNC): composed, a window dragged by its title, another
   raised and closed, Esc back to the desktop.** 38 KB. Built by `make servers` in `user/` (in `all`),
   staged to `SD:/bin` by `make stage`. Not on the card yet (no package from this branch).
   The PC test of the window manager (`sh tools/tests/desktop_sim/run.sh`) was broken since kapi v67 (a
   stand-in missing: `kstub/circle/sched/synchronizationevent.h`): repaired, it passes.
2. The kernel's mechanisms above (kapi v89), unused by default, with a test tool on the Pi.
   **2a done, tried on the Pi** (kapi v89: `kernel/sys/wsrv.cpp`, `kern/wsrv.h`, one table entry
   `ws_ctl`, AppKit's `kapi_ws_ctl`; docs/02 §8, v89): the role, the display (the kernel's compositor
   pauses; the server's rectangles copied and sent), the raw input ring (USB and injected), one wait,
   the way back (the server gone, or silent 5 s). The test: **`elegant --display`** -- the three
   windows of the demonstration on the real display, from the raw input; Esc, the last window closed
   or 60 s give the display back. **On the Pi (2026-10-05, over telnet and VNC)**: the pointer exact, a
   window dragged, windows closed, the desktop back after Esc, after the 60 s, after `kill`; idle,
   Elegant makes 27 calls a second. Two faults found there and fixed: the wait returned at every I/O
   wake of the system (1 300 calls a second: it now sleeps through what is not for the server), and a
   press was merged into the pointer move that followed it (a fast drag started from the wrong
   place: only moves are merged now). **Not tried: a USB mouse and keyboard** (the tests went through
   `inject_*`); and with a client connected to `rdpd`, its pointer is injected too and disturbs a
   scripted click (seen: the pointer somewhere else, a press released early) -- not a fault.
   A test build goes to the Pi with `python tools/tests/elegant/pi_deploy.py` (the kernel from
   `~/src/Onyx-elegant/kernel` in WSL -- `rsync` this tree's `kernel/` there, `make kernel8-rpi4.img
   sizecheck`, copy the image to `kernel/` here --, `user/lib/appkit.so`, `elegant.elf`; it saves the
   Pi's own kernel and AppKit first, `--restore` puts them back). **The Pi runs this test kernel (v89)
   now, with the packages' programs: `pkg update` of `onyx` would put the published v88 back.**
   **2b done, tried on the Pi** (docs/02 §8, v89, *the programs' windows*): attached programs (the
   kernel keeps their event queue as a kernel window that is only that: `pop_event`, `should_exit`,
   `pump_wait`, `kill` unchanged), shared buffers (contiguous; in the program at the addresses its
   canvas and frame always had, in Elegant at 52 GB + number x 64 MB), requests (the caller sleeps
   until Elegant answers: `sys/vfs.cpp`'s pattern, the pid stamped by the kernel -- no named sockets),
   the ring's `KICK` (a program's pixels changed) and `GONE` (it ended). `kernel/gui/window.cpp` takes
   its pixels from `WinPixelsAlloc` / `_Free` (Elegant: the shared buffers). The protocol:
   `user/Kits/appkit/elegant.h` (private; CREATE, FRAME, HANDLER, MOVE so far). Elegant:
   `server.cpp` (the loop: input, requests, events forwarded, composition), `kws.h` (its kernel
   calls), `elegant --serve` (no demonstration, no end); it writes one line every 5 s to the kernel's
   log while it works (`elegant 5s: input.., requests.., events sent.., frames..`).
   **The test: `python tools/tests/elegant/pi_wstest.py`** (`user/BinUtils/wstest.c` talks the
   protocol by hand): its window shown, two clicks counted by its handler, the window dragged, its
   close button ending the program -- **all passed on the Pi (2026-10-05)**.
   Learnt while testing: a VNC capture waits for the screen to CHANGE and hangs on a still one (the
   test only sends through VNC and reads the Pi's own log); a telnet session left in `kmsg` stays
   for ever (kill it), a few of them and telnetd answers no more; each `vncdotool` command starts
   with its pointer at 0, 0.
   Not done in 2b: the role given by the kernel to the process it starts (stage 3, with the trial);
   a restarted Elegant taking the orphan buffers again.
3. **Elegant serving the programs' windows + AppKit's client side: done but the full screen; tried on
   the Pi (2026-10-05).**
   - **AppKit**: `appkit_ws.inc` + `KAPI_WS` in `appkit_calls.inc` -- each window call's second body
     (docs/02 §8, v89). The way is settled at a program's first window call, for its life.
   - **The protocol**: `user/Kits/appkit/elegant.h`, 30 operations (append-only numbers).
   - **Elegant**: `ops.cpp` (each request = the kernel's call of the same name, on Elegant's window
     manager), `server.cpp` (the loop), `core.cpp` (the window manager behind plain functions; the
     pointer's shapes from `kernel/gui/cursors.inc`), `corepriv.h`.
   - **The trial**: `python tools/tests/elegant/pi_deploy.py --trial` (it puts `etc/elegant.trial`;
     the kernel removes it at the start, starts `elegant --serve` before init). The next restart is
     the kernel's window manager's again.
   - **Seen on the Pi**: 2048 and eyes started by hand under `elegant --serve`
     (`tools/tests/elegant/first-apps-2026-10-05.png`); then a whole start on Elegant: the wallpaper
     (voronoy through its copy of the buffer), the menu bar, the dock, the agenda, Terminal and
     Tinypad opened from the dock, Tinypad's menus in the menu bar, the dock's running marks --
     every program as it is on the card.
   - **The full screen** (docs/02 §8 v89's end): the kernel's calls as before, on the program's
     kernel-side window; Elegant told by the ring (`KAPI_WS_IN_FULLSCREEN`), all the input to that
     program, nothing shown meanwhile. Tried: `plasma` started on the Elegant desktop, Esc, the
     desktop back. Typing (Tinypad), a window dragged, the menu bar's drop-down: tried too.
   - **Onyx Remote works on Elegant** (the user, connected by RDP during the trial of 2026-10-05:
     "en rdp ça marche bien"): `rdpd` as it is on the card -- its `win_list` / `win_read` are
     requests to Elegant (about 130 a second; 110 ms of read + compare a round: each read is a round
     trip and a copy through the transfer buffer -- what the capture channel is for), its clicks
     and keys go through `inject_*`.
   - **The pointer's shape in Onyx Remote** (asked by the user, 2026-10-05): `kapi_cursor_shown` (a new
     AppKit call: Elegant's `EL_OP_CURSOR_SHOWN`; -1 under the kernel's window manager), `rdpd` looks
     20 times a second and sends message 11 when it changed (an older client skips it), Onyx Remote
     shows the matching Windows pointer (`MainForm.ShowCursor`). The Pi's side tried with a test client
     (arrow, then the size arrows over a frame's edge); `pc/dist/OnyxRemote.exe` rebuilt -- **the user saw the pointer change in Onyx Remote** (the
     exe of THIS tree: the main checkout's `pc/dist` has the old one until the branch is merged).
   - **Elegant's arrow** is its own (white, black edge; `core.cpp`).
   - **VNC felt jerky under Elegant** when a window was dragged (the user, twice). Done since: one
     frame every 16 ms at most (the programs' presents composed together); Elegant's thread at the
     high priority (`kapi_thread_priority`); the kernel yields to it right after an injected pointer
     event (`kapi_inject_pointer`: the server moves and shows before the remote desktop grabs again);
     rectangles that nearly make one sent as one. **Measured** with
     `python tools/tests/elegant/vnc_drag_bench.py X Y` (an RFB client that drags a window by its title
     and counts vncd's updates; zlib rectangles as a viewer -- raw ones measure the Wi-Fi): Terminal's
     window dragged, three runs of 8 s each, same Pi, same evening -- **the kernel's window manager
     14.0 / 12.6 / 6.7 updates a second (longest gap 0.6 / 1.2 / 2.2 s); Elegant with those changes
     14.9 / 10.5 / 11.3 (0.3 / 2.3 / 2.4 s)**: the same within the noise; the gaps of seconds are
     there under both (vncd or the Wi-Fi: not looked into). No valid measure of Elegant BEFORE the
     changes; the user has not yet said how it feels now.
   - **`screen_set` under Elegant: written and built, NOT tried** (the user was on the Pi by RDP: no
     restart then). The kernel's compositor does the resize as always (between two of Elegant's
     presents), then tells Elegant (`KAPI_WS_IN_SCREEN`): a new screen buffer, `OnScreenResized`.
   - **Left in stage 3**: Elegant dying -> the kernel takes the display back (done) AND
     starts it again (not done; its programs' buffers are kept for it: `bServer` in `wsrv.cpp`); the
     role given to the process the kernel started (today: the program named `elegant`); `rdpd` /
     Onyx Remote and `vncd` to check on Elegant (`win_list`, `win_read` by the transfer buffer,
     `inject_*`); the screen's size changed while running; a PC test (Elegant's core + AppKit's
     client joined to `tools/tests/desktop_sim/fakekapi.cpp`) -- not written, the Pi was used.
   - **Known differences**: a program's wallpaper buffer is its own copy (what is shown when it asks),
     not the live one; a drag's payload is 4 044 bytes at most (4 096 before); a program started
     while the kernel's window manager has the display stays there (and the reverse).
4. The whole test pass on the Pi (every app, full screen, GPU apps, VNC / RDP, dock, menus, drag and
   drop, desks, a resolution change); Elegant the default.
5. The kernel's window manager removed (`kernel/gui/window.cpp`, the compositor task); the sources move
   to `user/Servers/elegant`; docs/02, 03, 04.

Notes for the next stages: the text of `kapi_draw_text*` needs the kernel's bitmap font (Elegant's
stand-in has no glyphs: it draws no text); `window.cpp` is called from ONE thread in Elegant (its spin
lock stand-in does nothing); a VNC capture of a full-screen program waits until something changes (move
the pointer first).

What there was to build on (written before the work started):

- **The study**: `docs/GUI-USERSPACE-STUDY.md` (2026: where things stand in the kernel, §2 *The GUI in
  user space* — the target, what the kernel must add, the protocol, compatibility, cost and risks; §4
  the recommended path; the questions left to the user at its end). Written BEFORE the kits: read it
  with what follows in mind.
- **AppKit changes the compatibility question** (docs/03 §5.10, docs/02 §8): every program reaches
  the windows through AppKit's functions by name (`kapi_create_window`, `kapi_present`, the event
  calls...). Their bodies are in `user/Kits/appkit/appkit_calls.inc`, the only code that reads the
  kernel's table: **AppKit can send those calls to a server process instead of the kernel, and no
  program is rebuilt.** Checked on 2026-10-05 by disassembly: of the 215 programs and libraries of
  the card, only `appkit.so` reads the kernel's table (and `el0test` / `faulttest`, the two tests of
  the table itself).
- **What the server can use**: shared surfaces (kapi v35), mailboxes and IPC services
  (`kapi_ipc_register` / `kapi_mailbox_*`), shared memory, threads, the V3D compositing library
  (`user/Libs/gpucomp`, already used in user space by Jet and Paint), UIKit's own drawing of the
  frames (`user/Kits/uikit/skin.cpp`: the window chrome is already user-side).
- **The kits-first rule** (CLAUDE.md): what is reusable goes into a kit. A window server's client side
  belongs in AppKit / UIKit, not in a new loose header.
- **Process**: the user decides the design questions (ask, with a recommendation); a change of the
  kernel's table is fine now (adapt AppKit); a Wi-Fi / SDIO / TCP change goes through the one-boot
  trial file; test on the Pi (192.168.0.7) through packages; `sdcard/config.txt` is the user's.

## The kits and the layout of user/ (2026-10-05) -- done, in main, on the Pi

One day's work, all published (onyx 2026.10.87, kapi v88):

- **AppKit** is the one interface between the programs and the kernel, called by name (v86); it also
  carries strings, console, `.ini`, keymap (v87, `applib.h` gone) and program starting `lx_*` (v88).
  `user/kapi.h` no longer exists: programs include `appkit/appkit.h`.
- **New kits**: SystemKit (notifications, clipboard, trash, volume, wallpaper, dock settings, file
  associations, preload list, applets' protocol), NetKit (httpc, ftpfs; `http.hpp` filed there),
  FontKit (was `ft.so`). FileKit got `fsutil.h`; UIKit loads icons through ImageKit
  (`ui::icon_load`); ImageKit's folder holds the codecs' sources.
- **One header a kit** (`<kit>/<kit>.h`); a C program links `lib/<kit>.imp_c.a` (libgen `--bind-c`).
- **Layout**: `user/Apps`, `BinUtils`, `Kits`, `Runtime`, `Libs`, `Emulators`, `Ports`, `Include`
  (only `docguard.h`, `gamepad.h`, `json.hpp`, `lineedit.h` left there). All on every include path.
- **Removed**: the Shelf, `ask`, the panel, the app list, the activity shell (`embed.h`,
  `shell_proto.h`), `quicklaunch.txt`, `imgtest`, `stkpoc` (binary). `jsc` is an optional package off
  the card (`tools/pkg/extra/jsc`, mkrepo `root =`).
- **Docs**: `docs/06-KITS-GUIDE.md` (the kits, an example each) and `docs/10..18` (one reference per
  kit, GENERATED from the headers: `python tools/docgen/kitdocs.py`, then `python docs/build_docs.py`).
- **Left open**: a kapi rise makes the desktop incomplete between the install of `onyx` and that of
  the kits (they are separate packages: ship the kits with `onyx`, or install them with it);
  17 apps still include ImageKit's codec headers directly; `docguard.h` waits for DocumentKit;
  `http.hpp` and FontKit's `fonts.h` / `uikitface.h` are still headers with their code; PC tests
  broken before this work and still so: `ftpd`, `ftpfs`, `rdpd`, `rdpd_pipeline`, `games`,
  `cardfile`, the Letters screenshot; a WebKit tree patched before 2026-10-05 must have its two CMake
  paths follow (`user/Libs/gpucomp`, `user/Libs/av`: docs/08's note); symbolic links on FAT are a
  design only (docs/POSIX-PLAN.md §11).

## Onyx BASIC: the kits by `#import` (2026-10-05; tested on the PC, under qemu and on the Pi)

- **Structures done too (step 2, the same day)**: the `.bi` lists the structures the functions name
  (`struct` / `field` lines: sizes and offsets worked out by `kitbi.py` and **checked by the compiler** --
  `lib/<kit>.bi.check.cpp`, 56 structures today, 38 of them AppKit's); `#import` makes each a TYPE
  (`DIM e AS FileKit.zip_entry`); a variable, an element or a whole array goes where a function takes a
  pointer (packed before, unpacked after); `PEEKT` / `POKET` at an address. Not done: a structure with a
  union (`kapi_pad`), an array field that is not a text (no name in BASIC; its bytes go as zeros **and
  what the function wrote there is lost** when a variable is passed -- pass an `ALLOC`ed address and
  `PEEK` it if such a field matters).

- **Done**: a BASIC program says `#import filekit` and calls the kit's functions by their name
  (`FileKit.copy (a$, b$, 0, 0)`), **with no kit named in BASIC**: each kit has a description,
  `SD:/lib/<kit>.bi` (name, place in the table, types), made by `tools/kitbi/kitbi.py` from its `.abi` and
  its headers at every build of the kits; the VM calls through one generic instruction (`OP_KCALL`).
  A new kit with C functions is importable as soon as it is built and staged. Also: `BYREF variable`
  (numbers a function fills), `ADDRESSOF (Sub)` (callbacks: 48 relays, the SUB run on the VM),
  `ALLOC` / `DEALLOC` / `CSTR$` / `PEEKx` / `POKEx` (memory shared with a kit). The user's choices:
  the kits' C functions and a **flat C exposure for the C++ kits** (handles, not objects); nothing checked
  at run time (as FreeBASIC); **what a kit returns allocated is the program's to free**. Read: docs/03
  (*BASIC and the kits*), docs/04 §13 (*The system's kits*), `SD:/basic/examples/kits.bas`.
- **UIKit from BASIC, and QBStudio on it (step 4, the same day; asked by the user: "que ça utilise uikit
  pour création et utilisation de fenêtre et widgets")**: `user/Kits/uikit/flat.h` / `flat.cpp`, C functions
  on handles (window, label, button, textbox, checkbox, listbox, dropdown, slider, progress, menus,
  dialogs; UIKit's table 722, `Root::step ()` new); QBStudio's controls' library and generated code call
  them (`#import UIKit`, `ADDRESSOF` of the user's SUBs, the loop in BASIC over `UIKit.window_wait`).
  A program has one window: the runtime's screen is not opened once a UIKit window exists. **Tested on
  the Pi through a private copy of the new `uikit.so`** (`SD:/tmp/uikitf.so`, a test runtime bound to it:
  `BAS_KIT_TEST_NAME` / `_PATH` in `baskits.cpp`) -- the generated converter converts, lays out, closes --
  **but not seen**: that day the Pi's screen was another session's (`SD:/bin/elegant`), and the system's
  `uikit.so` was not replaced nor the Pi restarted. **To do first when the Pi is free**: update it by
  packages, restart, run `SD:/basic/examples/uikit.bas` and a QBStudio project, and look (clicks, typing,
  the menus, a resize). Not in the flat layer yet: the other widgets (tabs, grids, trees, pictures, a
  drawing surface), the keyboard and the pointer as events, several windows.
- **Next**: what the list above lacks, as programs ask for it. Also possible: `OP_KCALL` in machine code (today the machine code hands it to the VM: one call),
  FreeType's functions in `fontkit.bi` (their prototypes are in FreeType's headers, not in the kit's
  folder: `kitbi.py` reads `user/Kits/<kit>/*.h` only).

## Onyx BASIC: classes (2026-10-05; tested on the PC, not yet on the Pi) -- then a native back end

- **Done (phase A)**: objects in BASIC, on the VM. The user's choices: `TYPE` stays a value; a new
  **`CLASS`** is a reference (C#'s struct / class); methods are written outside the block
  (`VIRTUAL` / `OVERRIDE` / `ABSTRACT SUB Class.Name`); `ABSTRACT` and a destructor
  (`SUB Class.delete`) are in, `PRIVATE` is not; **a TYPE no longer has methods** (asked the same
  day: a `SUB Type.Name` is a compile error; QBStudio's `Control` / `Window` became classes). `CLASS B EXTENDS A IMPLEMENTS I, J`,
  `INTERFACE ... END INTERFACE`, `BASE.Name`, `NEW`, `NOTHING`, `x IS Class`. The language: docs/04
  §13 (*Classes*); the internals: docs/03 (*Onyx BASIC*, *Classes*). New opcodes at the end of
  `enum Op` (`OP_NIL` ... `OP_CAST`, then `OP_COUNT_`), `.bax` format 2 (format 1 still loads).
  The class words are not reserved. Tests: `tools/tests/basic/progs/t20_classes`, `t21_cls_*`,
  `t22_classwords`; the example `sdcard/basic/examples/classes.bas`. `qbasic`, `qbstudio`,
  `pc/OnyxBasic` know the modifiers and leave an INTERFACE block in the main module.
- **Not done**: `PRIVATE`, interfaces extending interfaces, a cycle collector, `obj.Method` on an
  expression other than a call's result.
- **Measured (2026-10-05)**, before phase B: `basic -p` (in the sources: `Host::prof`, `bas::Profile`;
  the card's `/bin/basic` does not have it yet -- it goes out with the next build of BASIC) splits
  the time into the VM's instructions, the runtime's primitives and the waits, written to
  `SD:/basprof.txt` every 5 s and printed at the end; `tools/tests/basic/pi_prof.py <ip> calc`
  uploads a runtime as `SD:/bin/basicp` and runs the benchmark `tools/tests/basic/bench/calc.bas`.
  On the Pi 4: pure computing = 98 % in the VM, **35 ns per instruction** (40.8 M instructions in
  1.44 s); Arkanoid runs about **50 000 instructions per second of play** (counted on the PC host,
  `PROF=1 PROFVMS=<ms>`), i.e. 2 ms of VM per second: native code will not show there.
  Careful on the Pi: another session uses it too, and `pkg` may be updating it (a staged system
  update restarts it); `pi_apps.py` runs at import (do not import it).
- **Phase B, step (a) done (2026-10-05)**: machine code at load, on by default (`basjit.h`; docs/03
  *Machine code*, docs/04 §13 *Machine code or managed*). The user's decisions: form (a) then (c);
  native by default with the VM as the fallback; `-m` and a "Managed" box in the compile dialogs
  (the user wrote the label in French, "Managé": the dialogs being in English it reads "Managed").
  Pi 4: the benchmark 1.48 s -> 0.11 s. `sh tools/tests/run_basic_native_test.sh` (qemu-aarch64 +
  the bare-metal toolchain) and its fuzzer must stay green after any change to the VM's
  instructions or to `opLen ()`.
- **Calls and numeric functions in machine code (asked by the user, done 2026-10-05)**: frames made
  and given back in line, by-reference arguments, ABS ... LOG. Pi 4, one telnet session, nothing
  else running: `calc.bas` 1.47 s -> 0.07 s, `calls_t.bas` 1.69 s -> 0.19 s (Fib&(27) 0.40 -> 0.05).
  Measure with nothing else running (`ps`): shells left by dropped telnet sessions made the timings
  vary threefold that day (fixed since: a dropped session ends its shell, see *Testing on the Pi*).
- **Next**: (1) strings, GOSUB, virtual calls in machine code; faster SIN / COS; (2) done the same day: **form (c)**, the standalone app --
  "Make App" > Standalone writes `apps/<name>.app/main` = the card's `/bin/basic` with the `.bax`
  attached (`bas::attachBax`; tried on the Pi with Planets 3D's program: it starts and runs); (3) Arkanoid and a demo timed on the Pi in both modes (only the benchmark was).
  **The Pi restarting around a `kill` of a graphical BASIC program: found and fixed (2026-10-05).** Not
  the kill (`kill <pid>` only asks the window to close) and not BASIC: the *start* of a full-screen
  program. The compositor yields while its display DMA runs and holds the frame buffer's DMA; an app
  that takes the full screen and sends its first frame meanwhile (`kapi_present_fb` ->
  `CBcmFrameBuffer::SetArea`) waited for that DMA without yielding: core 0 stopped, the hang watchdog
  restarted the Pi 15 s later. Seen in `SD:/etc/lastcrash.txt` (core 0 in `SetArea` under `basicp`,
  "display: waiting for the display DMA"), reproduced with `tools/tests/fsrace/` (the old kernel
  froze before 40 rounds; with the fix 600 rounds pass). The fix: `DisplayPresentIdle` (kernel.cpp),
  called by `kapi_present_fb` -- docs/02, *Full-screen apps*. Read `SD:/etc/lastcrash.txt` first after
  any unexplained restart; a restart that leaves it unchanged was a clean one (`reboot`, a system
  update, another session putting its kernel on the Pi).
  Another session was adding AudioKit statements to BASIC at the same time (the end of
  `enum Builtin`): merge `origin/main` before touching `basint.h`.

## Printing (2026-10-05)

A generic print system, done and tested on the Pi with the user's HP DeskJet 2700 (192.168.0.14, IPP
Everywhere: no PDF, PWG Raster at 300 dpi) and the PDF printer. Read `docs/03` §5.7 (the design, the API,
an example) and `docs/04` §11 *Printing*.

- **`SD:/lib/printerkit.so`** (`user/Kits/printerkit/`: `print.h` the API, `print.cpp`, `dialog.cpp`; table `printerkit/printerkit.abi`,
  33 entries): the Print dialog and the jobs. The first library that uses others (`fontkit.so`, `uikit.so`: their
  stubs linked in, opened on demand; uikit's variables through `--data onyx_uikit_data`).
- **`printd`** (`user/Apps/printd`): the queue (`SD:/var/spool/print`), the printers (`SD:/etc/printers.ini`),
  a job replayed as a PDF (`printerkit/pdfsink.h`) or rendered (`printerkit/raster.h`, our own MIT rasteriser — the
  user's choice, no MuPDF in the chain) and streamed as PWG Raster over IPP (`printerkit/ipp.h`).
- **Printers** applet (`user/Apps/printconf`), `/bin/ipp`; the Control Panel's list scrolls now. **Find**
  (the user's request): a one-shot mDNS query from printd (`scan ()`); from the PC the HP answers it; on the
  Pi the user ran it: the HP is found, quickly (2026-10-05). A first version tried every address of the /24 with 12 non-blocking connects at a time: **it
  restarted the Pi** (the network stack; not investigated) — removed. The Pi also restarted once earlier in
  the session, right after `ipp ... validate` and stopping `ftpd` with Ctrl-C: cause unknown, to watch.
- **File ▸ Print… (Ctrl+P)** in Letters, Sheet, Slides (through `printerkit/pdfprint.h`: their PDF export code),
  Paint, Photos, the PDF Viewer (`print_image`).
- Tests: `sh tools/tests/run_print_test.sh` (`IPP_PRINTER=192.168.0.14` asks the real printer).
- **Next / not done**: the screenshots (`printconf`, the Print dialog, `control` with its 11 applets:
  `shots.sh` has no scenario for them — the library is not built for the simulator yet); the Word / PDF
  exports (`docs/build_docs.py`: no pandoc on this PC); IPPS (TLS), a password; Apple Raster / PCLm printers that take neither PWG Raster nor PDF; two-sided
  printing; a job kept and retried when the printer is off (it fails with a notification today); print from
  Mail, Cardfile, Jet; a command-line `lp`. On the Pi, only Letters' print (dialog, PDF) and the applet were
  driven by me; the user printed Letters' current page on the HP, in colour ("instant, clean"); the user also printed
  from the Spreadsheet: as on the screen, the same colours and gradients. Slides, Paint, Photos and the PDF
  Viewer were deployed but their Print not exercised.

## Working conventions (keep them)

- **Git (the user's rule, 2026-10-01)**: before each new development and each commit, fetch and
  merge the latest `origin/main` into the working branch, commit, then push into `main`
  (`git push origin HEAD:main`) and the branch. Delete the working branch only when the user
  asks (CLAUDE.md).

- Repo `stephaneweg/Onyx`; the cloud session worked on branch `claude/kind-rubin-vddz2w` and
  always pushed it to `main` as well. Circle is a submodule (`circle/`, fork
  `stephaneweg/circle`, branch `onyx`): commit there with
  `git -C circle -c user.name="stephaneweg" -c user.email="steph.wegener@gmail.com" commit ...`,
  push `git -C circle push origin HEAD:onyx`, then commit the submodule pointer in Onyx. Never
  modify the nested `circle/addon/wlan/hostap` (upstream, not pushable).
- The user never compiles for the Pi: they test the staged `sdcard/` on the Pi and `pc/dist` on
  Windows. `pc/dist` and `sdcard/` binaries are committed.
- Never commit: ROMs / ISOs / saves (`.z64 .n64 .v64 .sfc .smc .nes .gb* .sav .iso .gcm .wav`),
  `sdcard/etc/wpa_supplicant.conf` (the user's Wi-Fi: untracked, ignored, never packaged, the
  pre-commit hook refuses it — never defeat it), `sdcard/etc/ftpfs.ini`, `shelf.ini`, `SD:/apps/lisa.app/config.ini` (Groq key),
  `sdcard/etc/clock`. Check before each commit:
  `git diff --cached --name-only | grep -i -E "\.sfc$|\.smc$|\.nes$|\.gb|\.sav$|\.z64$|\.n64$|\.v64$|\.wav$|\.iso$|\.gcm$|wpa_supplicant|ftpfs.ini|shelf.ini|lisa.app/config"`
  must print nothing. Do not download commercial ROMs; the user's own ISO/ROMs stay local.
- The browser is **Jet, on WebKit** (`docs/08-WEBKIT-PORT.md`; `user/Apps/jet`, `tools/webkit/`):
  NetSurf, its libraries, its PC bench and Jet for Windows left the tree on 2026-10-04 (the commit
  before: `3e2eb270`). WebKit's own tree is not in this repository: its changes are the patch series
  `tools/webkit/patches/` (export them after each WebKit commit: `sh tools/webkit/export-patches.sh`).
- Docs rule (CLAUDE.md): kapi / app changes → docs 02 / 03 / 04 (+ 05 for Circle patches),
  screenshots via `sh tools/tests/desktop_sim/shots.sh [name ...]` (the real apps on the PC);
  the user runs `python docs/build_docs.py`.
- A rebuild of the Pi apps changes every `sdcard/apps/*/main` a little (build noise): commit only
  the apps that really changed (`git checkout -- sdcard/...` the others).

## Builds

- **Pi (Linux + AArch64 toolchain):** `export PATH=<arm-gnu-toolchain aarch64-none-elf>/bin:$PATH`;
  `cd kernel && (make -j8 || make -j1) && make stage` (the -j8 link race on crt0libc.o is known,
  the -j1 retry works). Circle libs: `make -C circle/lib`, `-C circle/lib/net`,
  `-C circle/addon/fatfs`, `-C circle/addon/wlan`, `(cd circle/addon/wlan/hostap/wpa_supplicant &&
  make -f Makefile.circle)`. After any `ffconf.h` change: rebuild libfatfs, libwlan,
  libwpa_supplicant and the kernel clean (the `FIL` layout).
- **Windows tools:** `sh pc/build.sh` → `pc/dist` (Koton: `sh pc/Koton/build.sh` → `pc/dist/Koton`, MinGW-w64 only; nemucore.dll with MinGW-w64 `g++`,
  `-lwinmm -lopengl32 -lgdi32`; the .NET Framework 4.8 WinForms exes with the .NET SDK). On
  Windows: MSYS2 (or WSL) with MinGW-w64 + the .NET SDK. The script rebuilds every exe; restore
  the unchanged ones (`git checkout pc/dist/<file>`) before committing.
- **Host tests:** `tools/tests/run_*.sh` (fs, v3d clip, gamepad, gc, nemu...). N64 headless:
  `g++ -std=c++17 -O2 [-DN64_TRACE] -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I user/Libs/basic tools/tests/n64/n64test.cpp user/Emulators/n64/*.cpp`
  then `n64test <rom> <frames>` with `N64_SAV`, `N64_INPUT="f0-f1:hex;..."`, `N64_SNAP=1`,
  `N64_GFX=prefix N64_GFXEVERY=n`, `N64_FRAMELOG=f`, `N64_CIMG=f` (see the file's header). With
  the user's OoT ROM, the pause menu is reached with the input script: Start at 1000, A at 1200,
  1450, 1550, 1650, then A every 80 frames from 1800 to 16000, Start at 16500.

## Several users: studied, then set aside by the user (2026-10-05) — `docs/MULTI-USER-PLAN.md`

**The user's decision: Onyx stays a simple, single-user system — no multi-user.** The study is kept
as an idea (`IDEAS.md`); do not start it nor bring it up again unasked. What the paragraph below
describes is the study, not a task.

The user asked to "see how" Onyx becomes multi-user (accounts and a login screen, the desktop started
by the session, `/home/<user>`, rights on FAT through an index in `/etc` enforced by the kernel, remote
access per user). The study, the proposed design and seven steps are in `docs/MULTI-USER-PLAN.md`.
**Nothing is built, and no decision is taken**: its section 2 lists fourteen (D1–D14), each with a
recommendation — get the user's answers before writing any code, then record them in that section.
Step 0 (a real random generator behind `kapi_random`, PBKDF2 in the kernel) changes nothing visible and
can go first. The printing work (`printd`, another session) is taken into account in its section 9.

## Shared libraries: built (2026-10-05) — `SD:/lib/uikit.so`, `SD:/lib/fontkit.so`, every app on them

**Where to read**: `docs/SHARED-LIBS-PLAN.md` section 0 (what was built, where it departs from the
plan, the results), docs/02 §7 *Shared libraries* (the kernel: kapi v83 `lib_open`), docs/03 §5.6 (the
generator, writing and using a library), **`user/Kits/uikit/abi.h` (the rules — read it before changing uikit)**.

- **Changing uikit now**: a fix in a `.cpp` → rebuild `lib/uikit.so` only (`make -C user libs`), stage
  `sdcard/lib/uikit.so`, publish the `uikit` package: every app gets it, none is rebuilt. A new function:
  the same (the build appends it to `user/Kits/uikit/uikit.abi`: commit that file). **Never** add a field or a
  virtual to a class of the headers: use the reserve (`Widget::reserved_` / `ext`, `uk_reserved0..7`,
  `Root`'s) — `uikit/layout_lock.cpp` fails the library's build otherwise. A change in a header's
  **inline** code reaches only the apps rebuilt after it. When the apps are rebuilt against a table
  that grew: raise `uikit >= 1.<entries>` in `tools/pkg/packages.ini` (`[onyx]` and `[*apps]`) and the
  `uikit` version in `versions.ini`.
- **A new app**: link `lib/uikit.imp.a` (and `lib/fontkit.imp.a`) — the generic rules of `user/Makefile` do.
  A static constructor of an app may use uikit: the bind constructors run first (priority 101; `user.ld`
  now orders the priorities across files — it did not before).
- **Jet** links `uikit.so` too since 2026-10-05 (`tools/webkit/build-web.sh` compiles the import side for
  the POSIX toolchain; `make -C user lib/uikit.imp.a` first). **Still static**: the PC builds (the simulator,
  Koton for Windows, macOS) — they compile `user/Kits/uikit/*.cpp` as before.
- **Tests**: `sh tools/tests/run_image_test.sh` (PC), on the Pi `libtest`, then from the PC
  `python tools/tests/shlib/pi_apps.py <pi-ip>` (every app started) and `sh tools/tests/shlib/compat.sh`
  + `python tools/tests/shlib/compat_pi.py <pi-ip> out/shlib-compat` (uikit N's program on uikit N+1).
- **Next on this mechanism** (not started): other libraries as wanted — the user asked for an
  **`audiokit.so`** (the study's result is in `IDEAS.md`: MeltySynth, the music decoders, a resampler, a
  MIDI file reader, a sound output helper; MIT; to expose to BASIC too) —, then `libgui` and the
  user-space window server `wsd` (`docs/GUI-USERSPACE-STUDY.md`), mbedTLS, newlib.
- **Open**: the inline code with logic of uikit's headers was not moved into the library; `kmsg` is not
  stopped by Ctrl+C over telnet since the shell's rework (the tests keep a second session in it).

## The sound's output: the jack, USB or HDMI (2026-10-05, kapi v84; published: onyx 2026.10.51)

`COnyxSoundDevice` (`kernel/sys/sound.cpp`; docs/02 §13 and *v84: sound_output*): one producer, three
outputs over Circle's devices, `SD:/etc/sound.ini` `output = auto | jack | usb | hdmi`, the Sound
applet's **Play on**, `volume output`. **Checked on the Pi 4**: the outputs switched while tones
played (jack → HDMI → USB with no device → jack), the log's `sound: output: …` lines, the applet.
**To do with the user's ears and hardware — nothing of it was heard**: HDMI on the screen; a real USB
headset (48 kHz, 16 or 24 bits; its volume through its own control: `ApplyVolume`'s dB mapping;
unplugged = silence, plugged again = back by itself); the Pi 400 (no jack: `auto` gives HDMI). Left
from the idea (IDEAS.md): VCHIQ, I2S, the Pi 5. The rate converter is a linear interpolation
(44.1 → 48 kHz): good enough to start, a better filter if it is heard.

## The network made fast, then reliable; Web's video (2026-10-04; on the Pi, in `main`, published: onyx 2026.10.39, web 1.0.8)

**Where to read**: `docs/05-CIRCLE-CHANGES.md` §25–§27 (every change, its measure), `docs/02` §11
(the net core's IPI, the trial file), `docs/08` *Media* (the video layer, the user agent).

- **Speed** (a Pi 4 on the user's Livebox, 5 GHz): 0.5–0.8 MB/s at the start of the day; now the
  Pi receives 4–6.5 MB/s and sends 3–4. What did it: the net core's inter-core interrupt, the
  Wi-Fi chip polled, both bands scanned and 5 GHz preferred, one SDIO command a frame, the bus at
  50 MHz, the controller's registers without a wait, TCP window scaling.
- **Reliability — the finding to keep**: with A-MPDU aggregation of the frames *sent*, half of
  what the Pi sent during a download was lost (the remote desktop froze for seconds). The kernel
  sets `ampdu_tx` 0, frame bursting, and TCP acknowledges one segment in eight. With aggregation
  the raw rates were 8–9 MB/s each way: **do not turn it back on without the test** — the PC pings
  the Pi (`ping -i 0.2`) while the Pi's `curl --limit-rate 1M` downloads; 0 % lost is the pass mark.
- **Trying a driver / TCP change on the Pi**: never as a test kernel's default (one cut the Pi off
  the network: a card had to be copied by hand). `SD:/etc/net-trial.txt` — `name=value` words,
  read and deleted at boot, a restart at the end (`secs=`), the kernel log's tail kept in
  `SD:/etc/net-trial.log`: docs/02 §11 *A trial*. `tcpbench` + `tools/tests/net/tcpbench.py`
  measure; `uname -v` says which kernel runs.
- **Open**: the Pi's sending rate without aggregation (3–4 MB/s); a rare 1 s pause left (TCP's
  minimum retransmission timeout is 1 s); whether a newer Wi-Fi firmware aggregates soundly;
  receive glomming does nothing with this firmware (tried, removed).
- **Web**: video and Media Source on `user/Libs/av`, the pictures as a compositor layer (YouTube 480p
  at its frame rate), the JavaScript console (F12), YouTube and Google asked for as a phone.
  **Not verified on the Pi by hand yet**: the Console window, AltGr characters (`@`), closing the
  Downloads window after a cancelled download. **Still to do, in the user's order**: full screen
  video, Web Audio, H.264 / AAC (needs an LGPL FFmpeg build: ask the user); then — once the user
  says the video work is done — nothing: **done 2026-10-04**, Jet / NetSurf and what only they
  used are deleted and the WebKit browser is Jet (WebKit's user agent kept).
- **Dock**: its window no longer moves when a launcher's name is shown (a pointer on a drawer's
  strip was taken to be on the launcher under it).

## Every app at EL0, the legacy mode removed (2026-10-02, kapi v74; tried on the Pi: all tests pass; in `main`, published)

- **What**: apps, /bin tools, Koton plugins, Jet run at **EL0** and call the kernel by `svc` through the
  same kapi table (per-process handles, every pointer checked, user-side `memcpy` and event pump); a
  fault kills the app only; the EL1 "legacy" mode and its options are gone; ID register reads emulated;
  `proc_stats` (v74) → `ps` SYSC/s, the Task Manager, `/bin/sysstat`; `tools/el0scan.sh` checks binaries.
  `cpu_stats` / `net_stats` (v80) → the Task Manager's Processor and Network tabs.
  `win_resizable` (v82) → a window's edges and corners drag (an outline, then `GUI_EVENT_WINRESIZE`:
  `Root::frameResize`); every `setResizable (true)` window has it. Not done: a live resize, a size kept
  from one run to the next.
  `set_cursor` (v81) → the pointer's shapes (a hand on links, the I bar on text, arrows on what drags, Sheet's
  cross): `uk_cursor` in a widget's `onMouse`; drawn by `tools/gui/gen_cursors.py`. Not done yet: Mail's HTML
  view (its page is drawn by another process: the shape has to come back through `webview_proto.h`), Slides
  and Studio (another session's), the terminal, a busy app's hourglass.
  The story and the design: `docs/EL0-PROTECTED-MODE.md` §7; the reference: docs/02 §5–§6.
- **On the Pi (the user, 2026-10-02)**: every test of the plan passes (el0test, faulttest, threads,
  app cores, emulators, Jet, media, office, network, BASIC, kills under load). Merged into `main`
  (from `ccr-182e4cf6-fxr778`) and the packages published (`onyx` 2026.10.21, every app rebuilt).
  The docs (01–04, the READMEs, `ARCHITECTURE.md`'s banner, LICENSING) describe the EL0 system.
- **Open: the GameCube emulator is slower than before** (the user: "later"). Leads, to measure first
  (`sysstat gcemu`, the GX/machine frame times): (1) the user-side `memcpy`/`memset` (el0blob.S) copy
  < 16 bytes byte by byte and align the destination first -- slower than Circle's for many small
  copies; a fast path for small Normal-memory copies (unaligned `ldr`/`str` when no Device memory is
  involved -- only `fullscreen_direct` is Device) would restore it; (2) system calls from its main
  thread per frame (the top slots in `sysstat`); (3) IRQs taken at EL0 on cores 2–3 now build a full
  800-byte frame (the stop IPI only: should be rare); (4) `tlbi`/ASID on each `core_run`.
  **Jet (NetSurf) may be a little slower too** (the user: not important, a WebKit port is planned);
  lead (1) and the newlib/umm locks (`sysstat jet`) apply there as well.
- **Performance after EL0, to analyse later (the user, 2026-10-02)**: the GameCube emulator and maybe Jet
  slower (above); **Arkanoid in full screen slow** (a BASIC `.bax` app; only ~4 300 system calls/s, so
  not the calls: look at the user-side `memcpy`/`memset` -- byte loops under 16 bytes and the
  destination aligned first, for the Device framebuffer -- and `present_fb`/`fullscreen_direct`);
  **Doom slower, ~588 000 system calls/s** (`ps` SYSC/s): a kapi in a tight loop (a clock read --
  `get_ticks`/`clock_us`, readable at EL0 through `cntvct` instead --, newlib's locks, the app-core
  RPC of `user/Ports/doom`): `sysstat doom` names the top slots; fix it app-side.
  **Measured (`sysstat doom`, the user):** `key_held` 77 % (184 169), `pad_state` 7 %, then thread_self,
  sound_status, msleep, should_exit, get_modifiers, pop_event (~4 300/s each). Cause: `poll_input`
  (`user/Ports/doom/doom_onyx.c`) asks `kapi_key_held` 43 times (7 specials + a-z + 0-9) per call, and is
  called ~4 300 times a second (not once a frame). The same per-key polling is in **every emulator**
  (gb, gba, nes, snes, n64, **gc**), invaders, `user/Apps/games/game.h` and the BASIC runtime (**Arkanoid**) --
  likely a big part of their slowdown at EL0. **The fix (the user's design, 2026-10-02):** `key_held` must not be
  a system call. The kernel keeps **one input-state page** (a 256-bit held-key map, the modifiers,
  the pads' state -- removing `pad_state`'s 7 % too), mapped **read-only at a fixed VA** next to the
  kapi table; the EL0 table's `key_held` (and `get_modifiers`, `pad_state`) slots point at
  user-side functions reading it (like memcpy): **existing binaries get it without a rebuild**.
  **Anti-keylogger**: only the process with the keyboard focus maps the real page; every other
  process maps a shared zero page at the same VA (no key held); on a focus change the kernel swaps
  the two PTEs (TLBI by VA + ASID) and clears the state. Key events for typing still go through
  the pump. Then Doom polling once per frame, and the small-`memcpy` fast path.
- **Later: memory balance on 1 GB Pis (the user, 2026-10-02)**: a 1 GB Pi 4 has no high zone, so all
  the apps share Circle's low pager (`PAGE_RESERVE`, 256 MB) while the kernel heap keeps ~680 MB it
  does not need; a colleague's 1 GB Pi runs out of memory on Wikipedia in Jet (~3 000 pages of
  64 KB = ~190 MB for one page; the footprint grows page after page -- maybe a NetSurf leak, maybe
  newlib's sbrk-only malloc that never returns memory: WebKit2 with process swap on navigation and
  a vm_map-based malloc (dlmalloc/mimalloc) will help). To do: a bigger app pool on 1 GB boards
  (the reserve chosen at boot from the board's RAM, in the Circle fork + kernel), a small-memory
  profile (smaller `RAM:` -- `ramfs=` --, Jet's caches on the card / capped), and a `vmmap <app>`
  tool (the regions, resident pages, kinds: heap, stacks, image, canvases) on vm_query/vm_stats.
- **To do: the PDF Viewer flickers when scrolling fast from page to page (the user, 2026-10-02).**
  Check first where it comes from (the view cleared / a blank placeholder painted before the
  worker thread's bitmap arrives; drawing straight into the visible canvas without a back buffer;
  pages rendered again on every scroll). The fix the user suggests: render into memory buffers
  once and scroll what is already generated -- keep the rendered pages (at the current zoom) of
  the visible ones and their neighbours in a cache, render ahead above and below, compose the view
  from that cache into a back buffer and present it whole (never clear first); while a page is not
  rendered yet, show its thumbnail or the previous zoom's bitmap scaled, not a blank.
- **Next**: the GameCube speed; then demand paging (`mmap`/`munmap`/`mprotect`, faults filled on
  first touch, the stacks and the heap lazy) -- the first brick of a POSIX layer (the plan discussed:
  files in stream, stat, env/posix_spawn/waitpid, pthreads + TLS (`TPIDR_EL0` is already saved per
  task by Circle's `TaskSwitch`; v75 gives a new thread and an app-core job their initial value), mmap,
  clock_gettime; then BSD sockets + poll, signals, termios) -- and, much later, a WebKit port
  (WebKitLegacy, single process, on the PlayStation/WinCairo model; LGPL: the user's decision).

## Program images: loaded once, shared, preloaded (2026-10-03, kapi v77; validated on the Pi, in `main`, published)

- **What**: stages (a), (c), (e) of `docs/ELF-LOADER-PLAN.md` -- the loader streams a program from
  its file once into an image object (`kernel/proc/image.cpp`, `kern/image.h`); its read-only
  segments are shared by its processes (mapped not owned), the writable ones copied per process.
  **The key is the program's canonical path** (`ImageCanonPath`: lower case, `sd:/apps/x.app/main`):
  a run of a path that has an image does not touch the card. The file kapis drop an image where its
  file changes (`ImageFileChanged`). kapi v77: `image_preload` / `image_unload` / `image_list`
  (slots 253..255); `/bin/preload`, `/bin/unload`; `pkg` re-preloads a kept program it replaces; a
  commented `preload jsc` example in `sdcard/etc/autostart`. docs/02 §7 *Program images* and §8
  *v77*, docs/03 (the kapi chapter), docs/04 §8.
- **Tested**: `sh tools/tests/run_image_test.sh` (465 checks: the canonical path, the ELF header
  checks, load / share / wait / fail / pin / unload / the hook); `run_ipc_test.sh`,
  `run_ofile_test.sh` (`CIRCLE=<circle tree>`), `run_pkg_test.sh` still pass; the changed kernel
  files pass `g++ -fsyntax-only` against Circle's headers on the PC.
- **On the Pi (the user, 2026-10-03)**: built with Arm's `aarch64-none-elf` 14.2 (first compile
  clean, the kernel 313 KB below its size limit); the system and the apps run as before; `wctest`
  (80 MB) preloaded starts and runs its test at least 4 times faster. Not reported one by one:
  `unload`, a `pkg` update of a preloaded program, the file hook, a preload from `/etc/autostart`.
- **The checks to run when something looks wrong** (`kmsg` shows one `image <path>: loaded|shared in N ms, mapped in N ms`
  line per start): boot (every program now goes through the new loader); start a program twice
  (`shared` the second time; `ps`' `PAGES` no longer counts a program's code); `preload jsc`, `preload` (the list),
  `jsc` (shared: no card read), `unload jsc`; replace a preloaded tool with `pkg` or over FTP and
  check the next start says `loaded`; `filetest`, `proctest`, `ipctest`, `memtest` as before.
- **Next**: stage (d) (keep images after exit, evict under memory pressure) and (b) (lazy fill) of
  the plan, if the measurements ask for them.

## POSIX layer, IPC, toolchain, WebKit port -- where it stands (2026-10-02)

- **Done, on `main`, validated on the Pi**: kapi **v76** -- demand paging and `vm_*` (v75), real files /
  processes / BSD sockets + `poll` (v75), IPC for WebKit2 (v76: AF_UNIX socketpair, SCM_RIGHTS-like
  handle passing, memfd/shm shared memory, spawn with handles); **libonyxposix** (`user/Runtime/libc/posix/`);
  the **`aarch64-onyx-elf`** toolchain (GCC 14.2, posix threads, native TLS; prebuilt + sources in the
  repo `stephaneweg/onyx-toolchain`; install: `sh tools/toolchain/fetch.sh`, rebuild:
  `tools/toolchain/build-onyx-toolchain.sh`); ports: SQLite, libxml2, curl+mbedTLS, ICU 78.3, HarfBuzz,
  Skia m154 (WebKit's copy), libjpeg-turbo (`tools/ports/`, `make -C user/BinUtils ports`). Tests on the Pi:
  memtest, filetest, proctest, nettest, ipctest, posixtest (all groups), posixtest-cxx, malloctest,
  icutest, hbtest, skiatest, skiademo -- all pass. The plan and every decision: **`docs/POSIX-PLAN.md`**.
- **Decisions (the user)**: WebKit2 replaces Jet (§8–§9), on the PlayStation port's model (no GLib,
  curl, Skia CPU, our compositor on the V3D later); static binaries (§13); the WebKit browser under
  **LGPL-2.1+** (§15); fork/vfork, symlinks on FAT, self-hosting, Mesa (GPU), 1 GB memory balance,
  the input-state page for key_held/pad_state, the PDF Viewer's flicker: later (notes above / plan).
- **WebKit port step 1** (WTF + JavaScriptCore up to a `jsc` shell; `PLATFORM(ONYX)`): **done,
  validated on the Pi (2026-10-02), in `main`** (built on the user's PC, WSL): WTF and JavaScriptCore build, `jsc`
  runs with the LLInt (no JIT; WebAssembly in its interpreter) and passes WebKit's es6, default
  stress and WebAssembly tests under qemu
  (`sh tools/webkit/test-jsc.sh`); `/bin/jsc` is on the card (its own package, `jsc`). **Step 2 (WebCore) renders a
  page on the bench and on the Pi** (2026-10-02: `tools/webkit/build-webcore.sh`, `wctest`,
  `test-webcore.sh`; Skia on the CPU, no GL; the network path still to do), in `main`. WebCrypto
  on mbedTLS is done on the branch `webkit-port` (`test-webcrypto.sh`: 299 checks on the bench).
  **Step 3 (WebKit2) and Web, the browser, run on the Pi (2026-10-03, in `main`, package `web`)**:
  kotonstudio.com over HTTPS in about 3.2 s, GitHub's pages, new windows for `target=_blank`; what
  was fixed (a heap corruption in Skia's glyph painting, curl's wake-up, the IPC monitor thread,
  WTF's RedBlackTree) and what is open (the Circle TCP panic a telnet client can trigger) are in
  docs/08 "Step 3". **The heap corruption's cause is still open**: newlib's `malloc_usable_size` was
  checked and is right (`/bin/malloctest`, bench and Pi), so Skia's workaround (`skmallocsize.c`)
  hides something else; resume with `HEAPCHECK=2 sh tools/webkit/build-web.sh` (docs/08 "Step 3"). Then (patch `0016`, Web 1.0.2): `<select>` lists, the system clipboard, find
  in the page, the right button's menu (Save Link / Image / Page As), downloads into `SD:/Downloads`
  with the Downloads window (its own process: progress, Cancel) and the status bar; Mail draws its
  HTML messages with Web as an applet (`web --applet`); kapi **v78**: `PROT_EXEC` for a JIT (memtest,
  posixtest pass on the Pi) — **next: the JavaScriptCore JIT** (roadmap step 3), then the V3D
  compositor. The roadmap and the user's decisions (one
  executable for the three roles, no tabs, the order of the steps) are in docs/08; the program
  images and `preload` / `unload` (kapi v77, `docs/ELF-LOADER-PLAN.md`) are in `main`. To resume: `docs/08-WEBKIT-PORT.md` ("status / how
  to resume") and `docs/LOCAL-AGENT-WEBKIT.md` (the PC's setup): the toolchain
  (`sh tools/toolchain/fetch.sh`), WebKit with `tools/webkit/fetch.sh` (pinned revision, sparse
  checkout, Onyx's patch series in `tools/webkit/patches/` -- WebKit is not vendored), the build
  with `tools/webkit/build-jsc.sh` (or `make -C user/BinUtils jsc`). Next steps: WebCore → WebKit2 → the
  Onyx view / compositor / media player → the browser → parity with Jet.

## Mail, the mail client (2026-10-02; tried on the Pi with Gmail: works well)

- **What**: `user/Apps/mail` + `user/Libs/mail` (docs/03 *Mail*; docs/04 §12; the mock-ups, the plan and the user's
  decisions: `docs/mail/README.md`), **MIT**. Gmail (an **app password**), Outlook.com / Hotmail (**Microsoft's
  device code**: the "Onyx Mail" application registered by the user at Microsoft Entra, its id
  `85ccaf6e-81ff-4a62-9194-930fc36429ad` built in -- `oauth_defaults`, user/Libs/mail/oauth.h; `SD:/etc/mail/oauth.ini` overrides it), any IMAP, POP3 + SMTP. Three columns as the mock-ups: all the inboxes,
  starred, each account's folders; conversations (Gmail's thread id, else References; one's replies from Sent
  joined); the reading pane with **our own HTML 4 + CSS 2 renderer** (`user/Libs/mail/html*.h`: the user, "simple,
  not Jet"), remote pictures held back, attachments opened / saved; writing with completion (contacts + the
  addresses written to), drafts, attachments; the wizard and the settings; **Contacts = a Cardfile form**,
  `SD:/Documents/Contacts.card`. `eml = mail` in `fileassoc.ini`. A worker thread does all the network; the
  passwords and tokens are encrypted on the card (AES-256-GCM, a key of the card).
- **Tests**: `sh tools/tests/run_mail_test.sh` (163 checks: protocols against `fakemail.py`, the renderer, the
  model); the screenshots: `shots.sh mail` (two made-up mailboxes: `fakemail.py --demo`).
- **On the Pi** (2026-10-02, the user): **works well with Gmail** (an app password). Yahoo: its app passwords are
  currently unavailable at Yahoo (the option greyed: Yahoo's own doing, no date given). Outlook: the user registered
  "Onyx Mail" at Microsoft Entra (2026-10-02): its id built in, to try on the Pi. Watch: big mailboxes (the first
  look takes a folder's newest 100), the TLS handshakes' time, the memory of large HTML mails (a newsletter
  wider than the pane is drawn once at its width and averaged down).
- **Next**: try Outlook on the Pi (the id is in), IDLE for the Inbox (the code is in `imap.h`, the
  worker polls today), older messages on demand, rich text when writing (bold, lists, links: the HTML part is
  generated from the text today), "always show pictures from this sender", search on the server, printing / PDF.

## Photos, the photo library (2026-10-02; tested on the PC, not yet on the Pi)

- **What**: `user/Apps/photos` (docs/03 *Photos*; docs/04 *Photos*; the mock-ups and the user's decisions:
  `docs/photos/README.md`), **MIT**. Every picture of `SD:/Pictures` (+ `Camera`), each volume's `DCIM` and the folders
  added by hand, **by day** (the EXIF date, else the name's), favourites, recently added, albums (lists of paths),
  folders, the search (name, date, camera, album, description), the years' strip; the viewer (zoom, film strip,
  details, a description); **editing** (crop with ratios, quarter turns, straighten, Enhance, 7 adjustments, 5
  filters; **Save** over the original or **Save as...**, the user's choice; a JPEG keeps its EXIF); the lossless
  rotation (the EXIF orientation); Send by Mail (`mail --attach <list>`, added to Mail), the Clipboard, the
  wallpaper (copied upright to `SD:/res/wallpaper.<ext>` when not on `SD:`), Paint, a PDF contact sheet, the slideshow (full screen, cross-fade). The user decided: no import, no
  videos, no places (GPS). Two threads: the scan, the thumbnails (cached as JPEGs in `SD:/etc/photos/thumbs`).
- **Tests**: `shots.sh photos` over `tools/tests/photos/make_samples.py`'s library.
- **Watch on the Pi**: the first scan of a big card (only headers are read: fast) and the thumbnails (each photo
  decoded whole once: a 12 MP JPEG is ~1 s on the Pi -- the visible ones first, then the backlog of all the
  missing ones in the same thread, a bar in the status line; cached as JPEGs), the memory when
  editing a big photo (the whole picture plus two working copies when saving).
- **Next**: the camera's own preview (EXIF IFD1) as a first thumbnail while the real one is made; a JPEG decoded at
  1/2, 1/4, 1/8 (stb has no DCT scaling: our own, or the preview); drag photos onto an album; a map is out (no GPS).

## PDF Viewer and the PDF export (2026-10-01; the viewer tried on the Pi 2026-10-02: works well)

- **What**: `user/Apps/pdf` (docs/03 *PDF Viewer*; docs/04 §12; the mock-ups and the user's decisions:
  `docs/pdf/README.md`) on **MuPDF 1.28.5** (`third_party/mupdf-1.28.5`, its fitz + pdf parts, built by
  `user/Apps/pdf/mupdf.mk`; **the app is AGPL-3.0**): a tab a document, thumbnails / contents / search in the
  side panel, the zoom, one page / continuous / two pages, rotation, full screen, selection and copy, links
  (the web: Jet), passwords, Properties (facts, fonts, security), the home's recent documents (reopened at
  their page: `SD:/etc/pdf/recent.tsv`), `pdf = pdf` in `fileassoc.ini`. A worker thread draws the display
  lists and searches; the files go through the kapi (`KStream`).
- **The export**: `user/Libs/pdf/pdfwrite.h` (**MIT**, ours): PDF 1.7, TrueType fonts embedded as subsets (Identity-H,
  ToUnicode), images, links, bookmarks. Letters' *File ▸ Export as PDF* (its pages drawn again into it:
  `PageView::paintPage`, `g_pdf`; the headings as bookmarks) and the Spreadsheet's (the used cells cut into
  A4 pages, fitted to the width, the charts as images).
- **Tests**: `sh tools/tests/run_pdf_test.sh`; the screenshots: `shots.sh pdf writer sheet`.
- **Licences**: the user, 2026-10-01: **all our own software under MIT** wherever possible (CLAUDE.md,
  docs/LICENSING.md).
- **On the Pi** (2026-10-02, the user): the viewer works well. Still to watch: very large PDFs (the bitmaps:
  18 M pixels at most; MuPDF's store: 96 MB); the export from Letters / the Spreadsheet on the Pi.
- **Next**: annotations and forms (MuPDF has them), colour management (lcms2), the CJK fonts; Letters'
  hyperlinks (the export would make them links).

## Paint made "pro" (2026-10-02, kernel v72, not yet tried on the Pi)

- **Asked by the user**: Paint more professional, FreeType, blend modes per layer composited by the GPU (a
  hidden layer as if absent), Tab between a dialog's fields (uikit), Ctrl+wheel zoom, pattern brushes, the
  colour selection and the free-form one, the fill's gradient along a line (GIMP-like gradients), Open as
  Layer / Paste as New Layer, a fade between two pictures, a Colours menu (desaturate, colorize, the
  channels remapped... on the selection, the layer or everything). The mock-ups the user approved and
  where it landed: **`docs/paint/README.md`**; the user guide: docs/04 *Paint*; the pieces: docs/03 *Paint*.
- **Kernel v72**: `gpu_render`'s blend presets 5..12 (`KAPI_GPU_BLEND_MULCOL` ... `DSTOUT`, kern/kapi_abi.h,
  sys/v3d.cpp's table: colour and alpha factors apart). **gpucomp**: `gpc_layer.blend` (`GPC_B_*`),
  `gpc_blend_pixel`; tests `sh tools/tests/run_gpucomp_test.sh` (the software V3D has the presets).
- **uikit**: `Widget::tabFocus` / `isField` / `onTabFocus`, `Textbox::changed`, `Button::onKey`; a `Textbox`
  without `cb` and a `Checkbox` / `RadioButton` leave Enter to the dialog. Every uikit app was rebuilt for it.
- **Paint** is a newlib app now (`paint.elf` rule in user/Makefile; its window sized from the screen).
  Simulator: `sh tools/tests/desktop_sim/shots.sh paint` (`paint_scene.py`). Samples: `SD:/docs/pictures/`
  (`tools/gen_paint_samples.py`, package `paint-samples`).
- **To try on the Pi**: the v72 kernel with the blend modes on the GPU (`gpcdemo test` still ALL PASS),
  Paint's speed on a big picture, the brushes, Ctrl+wheel (it was there before: the user had not tried it),
  Tab in other apps' dialogs (Ledger keeps its own Tab).

## Media Player, the music and video library (2026-10-01 music, 2026-10-02 videos; not yet tried on the Pi)

- **What**: `user/Apps/media` (docs/03 *Media Player*; docs/04 §12; the mock-ups and the user's decisions:
  `docs/media/README.md`): MP3, OGG, FLAC, WAV (minimp3, stb_vorbis, dr_flac, dr_wav in `third_party/`) and
  MIDI (Koton's MeltySynth + GeneralUser GS), a library scanned from the folders watched (`SD:/Music`), by
  artists / albums / songs / genres / folders, favourites, recently added, `.m3u` playlists; home, album
  pages, search, a songs' table with its menu, now playing (a MIDI file: its notes as coloured lines),
  the mini player (the window reduced at the screen's bottom right). Tags read only; covers from the
  files and folders; closing stops the music.
- **The videos (2026-10-02)**: on Jet's media library `user/Libs/av` (VP9, VP8, AV1 + Opus...; `av_player_open_file`,
  its reader now through the kapi): `videos.h` (the facts, `videos.tsv`, the kinds: Films / Clips and series,
  episodes), `thumbs.h` (a frame a tenth in, `SD:/etc/media/thumbs/*.jpg`), `watch.h` + `WatchView` (the
  whole window, controls over the picture, full screen, resumed where left, *Next* episode); the sound
  handed between the music's thread and the video's (`Player::release`). `media.elf` links `libvpx.a`,
  `libdav1d.a`, `libopus.a` and **FFmpeg 7.1.2** (the user, 2026-10-02: "FFmpeg complet", the Media Player
  GPL-2.0): every decoder (H.264, H.265, AAC...) and container (AVI, TS, WMV, FLV, OGV...) through `user/Libs/av`'s
  `av_ffmpeg.c` / `av_lavf.c` (`third_party/ffmpeg-7.1.2/onyx/build.sh`; test videos: `Samples/Videos`). PC: `shots.sh media` (the sample library's `Videos/`: `make_library.py`; the
  simulator's build of `user/Libs/av`: `tools/tests/desktop_sim/av_host.mk`); `tools/tests/av/run.sh` checks the
  file mode now.
- **To try on the Pi**: the sound (`kapi_sound_write` from the player's thread: a song heard whole,
  pause / seek at once), the scan of a big `SD:/Music` (its time; the next start from `library.tsv`), a
  MIDI file (the SoundFont's load: 30 MB, a second or two), the covers' loading, the mini player's place;
  **a video**: 480p VP9 / 360p AV1 smooth in the window and full screen (the frame scaled on the UI thread:
  nearest neighbour; `st.decode_us`, `st.dropped` to look at), the sound's hand-over with the music, a big
  film's thumbnail (the seek through Cues / `stbl`), the start's scan with many videos (`probe_video`: a few MB
  each the first time).
- **Next**: media keys; ReplayGain; gapless; the covers cached on the card; a playlist reordered by
  dragging; the videos' next steps in `docs/media/README.md` (subtitles, AAC, H.264, a video's mini player).
## Screenshot, the screen capture tool (2026-10-01, not yet tried on the Pi)

- **What**: `user/Apps/screenshot` (docs/03 *Screenshot, the capture tool*; docs/04 §12; the study and the
  mock-ups validated by the user: `docs/screenshot/README.md`), laid out as Windows' Snipping Tool: New,
  the mode (Rectangle / Window / Full screen) and the delay (0 / 3 / 5 / 10 s) as drop-down buttons, then
  Copy and Save As, the drawing tools at the right (pen and marker with their colours and sizes, an
  eraser that takes a stroke away, a crop, undo / redo). The screen frozen full screen for a rectangle
  (a magnifier) or a window; copied to the clipboard at once (clipd), notifyd's notification; saved only
  on Save As (PNG / JPEG / BMP).
- **Kernel** (no ABI change: v71): **Print Screen** -- the input task sees USB usage 0x46, the main
  task's loop (now every 50 ms) tells the `screenshot` service or starts the app (`--now`;
  Alt: `--window <id>`, the window that has the keyboard); `IpcPost (service, type, data, len)` in
  `sys/ipc.cpp` (IpcNotify is built on it). docs/02 §2.
- **The simulator**: `SIM_GRAB` (what screen_grab gives), full-screen apps dumped, `SIM_WINS` titles;
  `sh tools/tests/desktop_sim/shots.sh screenshot`. Icon: `tools/icons/screenshot_icon.py`.
- **To try on the Pi**: the window hidden before the grab (0.35 s: the compositor must have drawn the
  screen without it -- lengthen if the window shows in the captures), Print Screen from another app
  (started / told), Alt+Print Screen, a capture pasted in Paint.
- **Next ideas**: handles to adjust a rectangle before taking it, text / arrows / shapes / a blur, a
  free-form selection, the capture of a menu (the delay works today).

## RAM:, a volume in memory; Jet Browser's caches there (2026-10-01, kernel v71, not yet tried on the Pi)

- **Kernel `RAM:`** (`kernel/sys/ramfs.cpp`, `kern/ramfs.h`; docs/02 §16): a file system in memory,
  reached by the same file kapis as the card (open/read/fsize/seek/close, save_file, file_in/file_out
  with append, opendir/readdir, mkdir/remove/rename, chdir) -- `kapi.cpp` resolves the path first,
  `RAM:` goes to ramfs, `FTP:` to vfs, the rest to FatFs; handles told apart by address. Folders and
  files are heap records of one size; the bytes are in 64 KB pages of `palloc_high` cut into 256 B ..
  32 KB chunks (a small file takes its size rounded to 256 B), given back when nothing in a page is
  used. One re-entrant sleeping lock; 1 MB slices with a Yield between them; a save is no-kill.
  Size: `system.ini` **`ramfs=`** (MB or `N%`; 0 = none; default 128 MB, at most a quarter of the
  free page memory), 32 MB of pages always left to the apps, 16384 nodes. Lost at a restart.
- **Kapi v71 `vol_info`** (`struct kapi_vol_info`): total / free / used / type of `SD:`, `SD1:`..,
  `RAM:`. New `/bin/df` and `/bin/ramtest`.
- **Jet Browser**: the disk cache in `RAM:/jet/cache`, the code cache in `RAM:/jet/jscache/` by
  default (stored at first sight, not paced, 2 MB objects, half / a quarter of `RAM:` at most);
  Choices' **`cache_on_card:1`** puts them back on the card with §32's rules. History and Cookies
  stay on the card. docs/06 §33. The code cache now does its file I/O through the kapi.
- **Tests**: `sh tools/tests/run_ramfs_test.sh` (ramfs.cpp on the PC under ASan, against a model);
  `httptest.sh` (RAM: one `SIM_RAM` = one boot; a restart; `cache_on_card:1` over three launches),
  jstest / nettest / fxtest / iframetest / gputest pass. The desktop simulator maps `RAM:` to
  `SIM_RAM` (else a temporary folder per run).
- **To try on the Pi**: `ramtest` (ALL PASS; its 16 MB times) and `ramtest full`; `df`; `ls RAM:`,
  `cp`, `rm`, `cd RAM:`; Jet Browser: a site, Jet closed and opened, the site again (from `RAM:`:
  `ls RAM:/jet/cache`), no `stall: jet:cache` in `kmsg`; after a restart `RAM:` empty; the boot log's
  `ramfs: RAM: volume, up to ... MB`. A 1 GB Pi: the size (~60 MB) and the apps still fine.
- **Next ideas**: `RAM:` in the File Viewer's Computer places and uikit's file dialog volume list;
  `mv` across volumes (copy + remove); a `ramfs` line in the Control Panel.

## TCP, SD and save fixes (2026-10-01, not yet tried on the Pi)

- **Circle's TCP** (docs/05 §20–22, docs/02 §11; host test `sh tools/tests/run_circlenet_test.sh`,
  the fork's real TCP code against stub headers): only real duplicate ACKs count (the remote
  desktop client's input messages started spurious fast retransmits: the Pi's sending throttled
  while the mouse moved); RTO 1 s initial (was 3 s), the **1 s minimum kept** (200 ms was tried and
  undone: spurious timeouts over Wi-Fi to Windows, the remote desktop ~50x slower), Karn after
  a fast retransmit; `CSocket::Send` answers the bytes queued when a later chunk times out (it answered the
  error: an app resending "the rest" duplicated bytes). rdpd now sends the rest after a short
  count instead of ending the session.
- **SD writes no longer hold core 0**: many short waits in a row (a long multi-block write)
  yield after 10 ms (`OnyxDriverPoll`, a second weak hook: docs/05 §7b, docs/02 §5).
- **`kapi_save_file` returns the bytes written**: Koton's `Doc::save` (every save "failed" on the
  Pi) and `fsutil.h`'s copy (an empty file) fixed; the desktop simulator's fake answers the length
  (it answered 0), so does `pc/Koton/winkapi.cpp` (pc/dist/Koton not rebuilt: no MinGW here).
- **To try on the Pi**: Onyx Remote while moving the mouse over a busy window (the updates a
  second should not drop; `rdpd` lines in `kmsg`: `partial`, the send times); a big file copied
  or saved (File Viewer copy of a 20 MB file, Jet's cache) with no `stall: ... TimeoutWait` lines
  in `kmsg`; Koton: save a song (no error); `fsbench` (the read speed should be about as before).
- The NetSurf bench `httptest.sh`'s two "launched again" disc-cache checks: fixed by the caches
  in `RAM:` (below; the card's "second sight" rule needed a third launch).

## The screen's resolution, changed while running (2026-09-29, not yet tried on the Pi)

- **Kernel v66 `screen_set`** (`ScreenResizeRequest`, kernel.cpp): the compositor task calls
  Circle's `C2DGraphics::Resize` between two frames (the firmware makes a frame buffer of the new
  size), then `CWindowManager::OnScreenResized` keeps the windows on the screen and sends every
  window **`GUI_EVENT_DISPLAY_RESIZE`** (19). Refused while a full-screen app owns the display.
  The wallpaper buffer is made again at the new size (the old one leaked on purpose: it may still
  be mapped). A window may be as big as the screen (was 1024 x 768).
- **uikit**: `Root::onDisplayResize (w, h)`; ~0.3 s later a maximised window fills the new work
  area, another is moved / shrunk into it (`displayTick`, `fitWorkArea`); borderless ones place
  themselves. The menu bar, the dock and notifyd do; vncd (VNC DesktopSize, else the session
  closed), rdpd (`SCREEN` message) and Onyx Remote (its view of the Pi's screen resized) follow.
- **The Control Panel's Display applet** (`displayconf`, `15-display.lnk`): a list of sizes,
  Apply: at once + `SD:/cmdline.txt`; voronoy paints the wallpaper again.
- **To try on the Pi**: bigger and smaller, with maximised windows, the Spreadsheet, a VNC viewer
  and Onyx Remote connected. Not handled: an app's own dialogs placed by the old size.

## Threads (2026-09-29, kernel v67, not yet tried on the Pi)

- **The scheduler has no task limit**: a circular linked list of `TSchedNode`s (the per-task
  scheduler state lives there: `CTask`'s layout is Circle's); `MAX_TASKS` and its `LogPanic` are
  gone. docs/02 §5.
- **Threads** (`kernel/sys/thread.cpp`, `kern/thread.h`): `kapi_thread_create / exit / join /
  self`, mutex / event (manual, auto) / barrier, `kapi_post` (a call run by the process's pump)
  and `kapi_pump_wait` (a pump that sleeps until an event or a post). A thread is a task sharing
  the app's `CAddressSpace`; the process ends with its main task, a kill takes them all
  (`TerminateGroup`), and the reaper frees a killed task only with its whole group (it may be on
  a wait list of the process). 32 threads per process. docs/02 §7, docs/03 §5.2.
- **User side**: `umm` and newlib (its retargetable locks, in `libc/onyx_syscalls.c`) are
  thread-safe; `kapi_lock` / `kapi_unlock`. `errno` is still shared (per-thread needs TLS: the
  kernel side is there -- `TPIDR_EL0` is saved and restored per task by Circle's `TaskSwitch`, and
  since v75 a thread starts with `thread_create_ex`'s `tls` and an app-core job with its caller's
  value; the C library's TLS block is WP-LIBC's, docs/POSIX-PLAN.md).
- **To try on the Pi**: `threadtest` in a terminal (PASS; the prompt comes back although a thread
  still runs), then kill it from the task manager in the middle; the usual apps (nothing should
  change for them: one task each). Watch `stall:` lines in `kmsg`.
- **Tried on the Pi (2026-09-30)**: `threadtest` PASS, killed mid-run cleanly (after the
  `task.cpp` FIQ fix: a thread first run after a preemption halted on Circle's EnterCritical).
- **Users**: NetSurf's downloads (a thread each: `user/netsurf/onyx_fetch.c`, docs/06 §1 --
  the connects one at a time: several at once all failed, a page's style sheet among them);
  `telnetd` (a session per thread, 8 at once); SuperTuxKart's `stkpoc` (`std::thread` on them:
  `user/Ports/stk`, docs/SUPERTUXKART-PORT.md -- PASS on the Pi).
- **Next**: asynchronous kapi calls (a file read, a connect) with a completion posted
  to the pump; `errno` per thread; threads in the BASIC VM (an idea, written down in
  `docs/BASIC-VM-THREADS.md`).

## The shared clipboard (2026-10-01, not yet tried on the Pi)

- **`user/Apps/clipd`** (the service, IPC "clipboard": a ring of 10 typed copies, a cursor), **`user/Kits/uikit/clipboard.h`**
  (the apps' side, its old functions kept + images and formats), **`user/Kits/uikit/clipproto.h`** (messages by mailbox,
  bytes by `RAM:/clip` files), **`user/Apps/clipboard`** (the widget, the dock's new button; the dock's small
  buttons now: lock / gear at the left, power / clipboard at the right). Every uikit app gets it through
  `textbox.cpp` / `textarea.cpp`: all the apps were rebuilt and staged. `autostart` runs clipd.
- **Tested on the PC**: `sh tools/tests/run_clipboard_test.sh` (21 checks); the simulator has in-process
  mailboxes (`SIM_IPC=1`) and the magenta key of `WIN_FLAG_TRANSPARENT` windows in its dumps.
- **To do**: Jet's copy / paste through clipboard.h (docs/clipboard/README.md, *Still to do*); try on the
  Pi: a copy in tinypad, the notification, the widget above the dock, an image (when an app copies one).

## Archiver, the archive manager -- version 1, ZIP (2026-10-01, not yet tried on the Pi)

- **`user/Apps/archiver`** (docs/04 §9 *Archiver*, docs/03 *The Archiver*, the plan and the user's
  decisions in `docs/archiver/README.md`): ZIP opened (zip64, CP437 / UTF-8 names, self-extractors,
  ZipCrypto), browsed as folders, extracted (the selection or all; the archive's folders kept, from
  the current folder down, or flat; Ask / Replace / Skip / Keep both), changed by a rewrite into a
  new copy swapped in (add, delete, rename, new folder). Files **dropped** from the File Viewer go
  straight into the folder under the pointer; rows dragged out are extracted to `RAM:` and handed
  over; a file opened (extracted to `RAM:`) and saved is put back. Jobs on a **thread**. A newlib
  app (FreeType) with zlib (`user/Libs/zlib/libz.a`). Built here with the Arm GNU toolchain 13.3.
- **Tested on the PC**: `sh tools/tests/run_archiver_test.sh` (the engine, 51 checks against
  `zipfile` and `unzip -t`) and the app in the desktop simulator (which now has `kapi_file_in /
  file_out` streams, remove / rename of the files an app wrote, and the script's `dragover` / `drop`).
- **To try on the Pi**: a big archive (hundreds of MB: the reads go through `kapi_seek`, the
  central directory is read whole), extracting to the card while it writes slowly, a drop from the
  File Viewer, Background / Cancel during a long add, a `.zip` opened from the File Viewer.
- **Next**: 7z (the LZMA SDK), tar / .tar.gz / .tar.zst / .tar.xz, RAR read only through
  libarchive's readers (BSD; unRAR's licence is not GPL-compatible: `docs/LICENSING.md` §4).

## Courier, the HTTP client -- Postman for Onyx (2026-09-30, not yet tried on the Pi)

- **`user/Apps/courier`** (docs/04 *Courier, the HTTP client*, docs/03 after `http.hpp`): requests
  with params / auth / headers / bodies (raw, urlencoded, multipart with files, binary), `{{vars}}`
  (environment > collection > globals, `{{$guid}}`...), collections with folders, environments, the
  history, a cookie jar, tests + captures, code snippets, Postman v2.1 import / export, cURL import.
  A newlib app (FreeType) linking mbedTLS; each request on a **thread** (kapi v67) with `kapi_post`.
  `SD:/courier/` holds its files; the card ships `courier/collections/courier-examples...json`
  (JSONPlaceholder + httpbin over https).
- **Tested on the PC**: the desktop simulator now has **threads** (pthreads, `kapi_post`) and
  **`SIM_REALNET=1`** (real TCP): the engine was checked against a local echo server (gzip,
  redirects, cookies, multipart, auth, tests, Postman round trip, cURL). HTTPS only on the Pi.
- **To try on the Pi**: the examples collection (https, a thread doing TLS: its 512 KB stack), a
  big response, Cancel during a slow request, the window maximised, import of a real Postman export.

## Setup, the first-run wizard; the settings in FreeType (2026-09-30, kernel v69, not yet tried on the Pi)

- **The rule now**: every new or redesigned app draws its text with **FreeType** (DejaVu Sans
  through uikit's face) unless the user says otherwise — `FT_APPS` in `user/Makefile` (and the same
  list in `shots.sh`'s `build`); docs/03 after `ft_uikit_install`. Moved to it: the Control Panel,
  its 8 applets, the Game Library, (2026-10-01) the menu bar (text measured in pixels, `drawFont` gone), (2026-10-02)
  Paint and the File Viewer (names cut to the column's width: `uk_text_fit`; the text preview clipped).
  **The windows' titles** too, in every app (FreeType or not): `SD:/res/fonts/title.aaf` (DejaVu Sans Bold
  13 px pre-rendered by the apps' FreeType: `sh tools/title_font/build.sh`), read by uikit's frame (`skin.cpp`).
- **`user/Apps/setup`** (docs/04 §4 *Setup*): 7 pages in uikit's theme (the user's validated mock-up:
  `screenshots/setup-*.png`, `shots.sh setup`) — country / keyboard / time zone, Wi-Fi, resolution
  with "Keep this resolution?", colour + wallpaper + 32 tints, host name + remote services, a
  summary. `system.h`: what it writes (system.ini, wpa_supplicant.conf, cmdline.txt, theme.txt,
  wallpaper.ini, the autostart). The card's autostart starts it (`run setup`) and holds back the
  menu bar, the dock and the agenda (`#setup: ` lines, given back and started at the end).
- **Kernel v69**: `WIN_FLAG_FIXED` (not movable, no title buttons, re-centred on a resolution
  change), `screen_native` (EDID), `set_timezone`; `system.ini` `hostname=` (Circle patch 17:
  `CNetSubSystem::SetHostname`) and `ntp=off`. `/bin/verbose` no longer erases system.ini.
- **To try on the Pi**: the EDID size (an HDMI monitor, a TV), Try it / Revert / the 15-s timeout
  and the window re-centred, joining a network (and a wrong password: 30 s), the host name seen by
  the router after a restart, the services started / stopped at Start Onyx.
- **Next (asked), in this order**: **PRIORITY -- apps in EL0** (protected mode: a faulting app killed, not the OS; plan in docs/EL0-PROTECTED-MODE.md; trigger: a panic closing Ledger, not reproduced -- the exception report now names EC/ELR/FAR/task); then redesign every app icon; then games and emulators with 1- or 2-stick pads.

## GPU compositing, stage 1 -- the service (2026-09-30, kernel v70, not yet tried on the Pi)

- `user/Libs/gpucomp/gpucomp.{h,c}` (+ `libgpucomp.a`): layers (premultiplied ARGB textures, tiled past
  2048) composited by the V3D into a canvas -- affine matrix, clip, opacity, source-over, bilinear,
  scrolling by the source rectangle -- or by the CPU (NEON loops) with the same API and pixels.
  Kernel v70: `gpu_texture_rect` (damaged rectangles only), `KAPI_GPU_F_ALPHA` (ARGB targets), fair
  shares of the GPU's handles between programs (1024 textures / 512 a program; 32 vbufs / 8 a
  program). docs/02 §15 (*Sharing the GPU*, *The compositing service*), docs/03 *GPU compositing*,
  docs/07 §6 (the stage 2 plan: NetSurf's layers into it).
- Tests: `sh tools/tests/run_gpucomp_test.sh` (the PC: CPU and software-V3D paths against a
  reference, tiles, updates, a refused texture, the GPU lost, qemu-aarch64 NEON = PC bit for bit).
- **On the Pi, first**: `gpcdemo test` (must end `ALL PASS`), `gpcdemo bench` (write the numbers
  into docs/07 §6), `gpcdemo` (the window) -- also while gcemu runs (the sharing), and `v3dprog`
  (must still pass: the kernel's GPU paths were touched for F_ALPHA). kmsg after a failure.
- Stage 2 done (docs/06 §25, docs/07 §6: the page in a band, retained layers, composite-only
  animations, `gpu_compositing` in Choices; `tools/tests/netsurf/gputest.sh`). The V3D's target
  load packet was wrong (stride / 8: frames drawn over the target) -- fixed, `run_v3d_cl_test.sh`.
  Next: the Pi's numbers (`gpcdemo test` / `bench`, NetSurf's `ONYX-SCROLL` with
  `gpu_compositing` 0 and 1), overflow scrollers and fixed boxes as layers, groups holding layers.

## Done recently (all pushed)

- **The random N64 freeze on the Pi — fixed.** Root cause (found with the crash record):
  `CScheduler::WakeTasks` asserted on a task whose `WaitWithTimeout` had just expired (Ready but
  still on the wait list) when the V3D frame-done interrupt set the event → Circle halted all
  cores. `kernel/sched/scheduler.cpp` now only unlinks such a task. The user played 15+ minutes
  without a freeze afterwards. The same bug is in upstream Circle: see below.
- **Crash record** (`kernel/sys/crashlog.cpp`, docs/02 §13): log tail, core-0 IRQ samples,
  breadcrumbs (GPU, display), power/temperature, panic line, in 64 KB of RAM kept out of the heap
  (top of the RAM above 4 GB on the user's 8 GB Pi — it survives the watchdog reset); the hardware
  watchdog (`hangreboot=`, 15 s); core 1 writes a report into `SD:/etc/crashdump.txt` sectors when
  core 0 stops (LED signs); next boot → `SD:/etc/lastcrash.txt`. `hangtest` froze core 0 on
  purpose (removed with v74: an app at EL0 can no longer freeze the machine). `SD:/etc/clock` keeps the time across boots (files written before NTP get a date).
- **Crash record, round 2 (2026-09-29, for the Spreadsheet's freeze: a long hang, then a restart,
  nothing on the card):** a Circle panic (assertion, kernel heap "Out of memory") halted every
  core, core 1 too — no report; now the logger's panic handler has core 1 write it first. The
  free memory (heap, kernel pages, app pages) every second in the record; the return addresses
  on a faulting stack; the **app watchdog**: a frozen app watched (its task's PCs, its stack) and
  `SD:/etc/apphang.txt` rewritten every 2 s, merged into `lastcrash.txt` if the Pi restarts
  (docs/02 *The crash record*). **Not yet tried on the Pi**: next, reproduce the Spreadsheet's
  freeze and read `lastcrash.txt` (addresses → `addr2line -e user/sheet.elf`).
- **The kernel's size limit:** image + BSS must end below `0x280000` (0x80000 + Circle's
  `KERNEL_MAX_SIZE`, 2 MB) — past it the BSS runs over the kernel's stacks and the Pi does not
  boot, without a message (a 32 KB static buffer did it). `make` / `make stage` now check `_end`
  in the map (`sizecheck`) and delete an image too big. 128 KB left: big buffers go on the heap.
- **N64 (task done):** OoT pause background (the copy into the z-buffer drawn by the CPU into
  RDRAM, the host's frame written back as the framebuffer: `Machine::fbSnapshot`), the 8-bit
  coverage copy written as full (menu opens in ~1 s instead of ~4), decal z bias (z-fighting),
  V3D CPU clipping, polled display DMA (`dispdma`).
- **Onyx Remote:** telnet **Console** window; window fixed to the Pi's screen size when
  connected; the desktop no longer tiles.
- **Circle upstream contributions:** 5 clean branches on the fork (`pr/scheduler-waketasks-timeout`,
  `pr/emmc-high-speed-fixes`, `pr/heap-large-block-reuse`, `pr/2d-partial-update`,
  `pr/dhcp-restart`), based on upstream `develop`; texts, patches and an issue draft (RAM above
  3 GB on the Pi 4) in `docs/circle-upstream/`. The user opens the pull requests.

## The desktop redesign -- a modernised CDE: implemented, in `main`

- **Designed with the user on 2026-09-28 and implemented the same day** on the branch
  `Elegant-UI`, merged into `main` once the user had seen it running (through Onyx Remote).
  `docs/gui-redesign/README.md` has the decisions, the mock-ups and §5 *where the work landed*;
  the user guide (`docs/04` §4-§6, §11) describes the result; `screenshots/` are the real apps
  (`sh tools/tests/desktop_sim/shots.sh`).
  In short: uikit's procedural painter (`user/Kits/uikit/paint.h`) and the theme's colours as variables
  (`theme.txt`: theme Peach / Steel / Sage / Brick / Slate or a colour, inactive, face, accent,
  outline, dock), every uikit widget restyled (the user's framed button; since 2026-10-03 the push button is a plain
  raised face, as the drop-down's: `uk_framed` draws `uk_raised`, no frame nor well), the window frames drawn
  by uikit (title 28, border 4, rounded corners r 8, the window menu / minimise / maximise / close
  buttons), kapi **v64** (`win_minimise`, `win_geometry`, `resize_window2`), the **dock**
  (`user/Apps/dock`: categories + drawers, the Shelf's tabs as its switcher, lock / gear / power,
  Terminal, File Viewer, Trash) instead of the Shelf and the panel, the see-through agenda, the
  menu bar restyled (its time opens a calendar), the **lock** screen, the **Theme** app
  rewritten, every app's hard-coded dark colours converted. Then (2026-10-01) a sixth theme,
  **Milk** (Xfce's Milk / Mac OS X: soft greys, the title buttons as coloured beads, the frame
  melting into the window with no line between them, `UK_STYLE`, `uk_bead`), chosen in the
  Theme app (and in Setup) like the others. Then (2026-10-03) the Theme applet's **Theme: Classic /
  Modern** over its schemes (Classic: Peach … Slate; Modern: Milk and the new **Dark Coffee**, a named
  theme with a palette of its own: `UkNamedTheme::pal`, `uk_theme_take`), Koton in the desktop's theme
  (its arrangement's lanes alone dark), **FM Tracker** made again (FreeType; a transport bar, the
  patterns' list, M / S / meters, blocks, undo, a piano, an instrument dialog with the waves, the
  envelopes and the sound's wave drawn), Ledger in FreeType, the Package Manager's tabs a
  `SegmentedControl`, a **System** category. Then the **Task Manager in tabs** (Processes: a sortable grid with the
  memory and the calls per second; Memory: what `memmon` showed, drawn — `memmon` is gone); its Processor and
  Network tabs came on 2026-10-04 with **kapi v80** (`cpu_stats`: per-core time in `CScheduler::Yield`, the
  sound and app cores' busy time; `net_stats`: bytes per process in `NetTcpSend/Recv`, `NetSockSend/Recv` —
  docs/02 *v80*). Not there yet: a process's own processor time (a CPU column in Processes), the bytes on the
  air (the driver's counters) next to the sockets' payload.
- The emulators' fast path is intact: an app's present damages only its client area unless its
  frame changed, `CoversOpaque` less the corners' see-through pixels only
  (`tools/tests/desktop_sim/wmtest.cpp` checks it); the V3D, `gpudirect`, `dispdma`,
  `fullscreen_direct` paths untouched.
- **Next ideas** (none started): anti-aliased text (the `.aaf` fonts and `user/elegant.h` on
  **`archive/elegant-ui-2026-09-28`**); resizing a window by dragging its border (today:
  maximise / restore; a resizable app says so with `Root::setResizable`); a theme change applied
  to the open apps live (today: at their next start; the shell apps are restarted); the dock's
  drawers by drag & drop (an app to a drawer); the Shelf / panel apps removed once the dock is
  proven on the Pi. (The second round below did the workspaces, the Control Panel and the
  dock's drawers.)
- The phone-like "elegant layout" the branch held first is on **`archive/elegant-ui-2026-09-28`**.

## The desktop's second round (2026-09-29, branch `claude/happy-wright-wg38ez`)

Asked by the user after trying the modernised CDE (not merged into `main` yet: the user tests
the staged `sdcard/` and `pc/dist/OnyxRemote.exe` first).

- **Workspaces** (kapi **v65** `desk` / `win_desk`, `kernel/gui/window.cpp`: `SetDeskLocked`,
  `Hidden ()` = minimised or off-desk everywhere the compositor / hit-testing / focus look,
  Ctrl+Alt+Left / Right in `OnKey`); the dock's pager (4 squares by default, the windows drawn
  small), the window menu's Move to / On All Workspaces. `wmtest.cpp` checks them.
- **The dock** rewritten (`user/Apps/dock`): drawers = a group + its main app (the icon starts
  the app, the strip above opens the drawer, as Xfce), launchers, the pager, lock / gear
  (Control Panel) / power, the Trash (a click opens it in the File Viewer: `fileviewer trash`;
  a drop trashes). The Shelf's switcher is gone. `SD:/etc/dock.ini` (`user/Include/dockconf.h`).
- **The Control Panel** (`user/Apps/control`): applets drawn inside its window through a shared
  surface (`user/Include/applet_proto.h`; uikit's `Root` has an applet mode: `uk_applet ()`, `uk_pump`,
  `uk_present`, `uk_quit`), listed by link files (`sdcard/apps/control.app/applets/*.lnk`).
  Applets: Theme (rewritten: a Windows-98-like desktop preview, a colour per part — frames,
  content, buttons, fields, selection, menu bar, dock — the wallpaper's modes, `user/Include/wallpaper.h`
  painted by `voronoy`), Panel (`dockconf`), Sound (`soundconf`), Keyboard & Mouse (`keyconf`),
  Gamepad, Wi-Fi, App Settings (`config`). Kernel surfaces are now counted per user (an applet's
  surface outlives its host or itself safely).
- **The categories**: the System group gone (its apps in Productivity / Graphics / Settings) --
  back on 2026-10-03 for the File Viewer, the Memory Monitor, the Task Manager and the Terminal
  (the Onyx menu's last category; no drawer of the dock by default) --,
  `Settings` and `Emulators` left out of the menus (the emulators through the Game Library).
- **The File Viewer**: a sidebar (Personal: pinned folders under a name, the Trash; Computer:
  the partitions; Network: the FTP servers connected once, under a name — a click reconnects),
  an elementary-style path bar, padded columns. `SD:/etc/places.ini`.
- **Onyx Remote**: the borderless windows above the framed ones (the menu bar's drop-downs,
  the dock, the notifications, the Wi-Fi menu) are layered windows with per-pixel alpha
  (`pc/OnyxRemote/Overlay.cs`) — no more black under a popup; hidden windows not shown; the
  Onyx frames' title buttons pressed on the Pi. rdpd sends the ALPHA windows and the frames in 32
  bits. The kernel bumps a window's ChromeGen again at its first present after a frame redraw
  (rdpd could read a half-drawn frame: a title bar without buttons). `eyes` presents now.
- **The Game Library** redone as the File Viewer (a sidebar: All Games, the systems with their
  icons and counts, the folders; a path bar; the cards filtered on the system chosen).
- **Wallpaper patterns**: eight abstract grey pictures (`sdcard/wallpapers/*.png`, 1024 x 768,
  made by `tools/gen_wallpapers.py`) that the Theme applet colours (`mode = pattern`: the grey
  multiplies the gradient of the two colours; `wallpaper.h` `wp_grey_cover` / `wp_multiply`,
  painted by `voronoy`). uikit's `Dropdown` opens upward when it must.
- **Open question for the user**: *Gamelib without its title buttons* — not reproduced (its
  frame is drawn like every Root app's, in the simulator too); the ChromeGen fix above covers
  the Onyx Remote case (a frame read while being drawn). Ask where it showed (the Pi's screen
  or Onyx Remote, with or without Onyx frames) if it comes back.
- **Next ideas**: drag an app onto a drawer to add it; the applets' own help; a wallpaper
  slideshow; the workspaces' windows moved by drag & drop onto the pager.

## Writer renamed Letters; Slides, the presentation program (2026-10-04)

- **Writer is now called Letters** (the user: a name for the document, as Sheet and Slides, not for the trade),
  everywhere: `app.txt`, the window, the messages of Letters, Cardfile, Ledger (and its French strings), the RTF
  viewer; the folder `SD:/apps/letters.app`, the package **`letters`** (and `letters-samples`), the sources
  `user/Apps/letters/`, `letters.elf`, `fileassoc.ini`, the samples (`tools/gen_letters_sample.py`,
  `SD:/docs/letters-tour.rtf`), the tests (`tools/tests/run_letters_test.sh`), the screenshots `letters*.png`,
  the docs, the Ledger manuals; on the Mac, Ledger.app's helper `Letters.app`. The packages `writer` and
  `writer-samples` left the repository: **`pkg` learnt `replaces =`** (`packages.ini`, the index, `pkglib.h`'s
  `update_for` / `drop_replaced`): a card that has `writer` sees `letters` as its update, installs it, then
  `writer` is removed (its mode kept; a file the new one took over, a sample, is not removed nor written `.new`).
- **Slides** (the presentation program, in the way of PowerPoint): the study and seven mock-ups,
  `docs/slides/README.md` (`tools/screenshot/mockup_slides.py`), then **built** (`user/Apps/slides/`, the package
  `slides` and `slides-samples`): themes, layouts, text boxes with lists and autofit, 28 shapes, pictures, tables,
  charts, sections, notes, the sorter, transitions and effects, the full-screen show and the presenter view; every
  object a layer, **composited by the GPU** (`gpucomp`: the editor's view and the show; the CPU path for the
  thumbnails and the exports); `.odp` read and written (LibreOffice opens ours, its own are read; `onyx:` attributes
  and `onyx.xml` for an exact round trip); PDF and PNG export. Test: `sh tools/tests/run_slides_test.sh`; sample
  `SD:/docs/cafe-2026.odp`; screenshots `slides*.png`.
- **PowerPoint's `.pptx`** (asked by the user, same day): `user/Apps/slides/pptx.h`, read and written. Written the
  way PowerPoint writes it, each Slides feature in its native form (the theme, a master, a layout per Slides layout,
  placeholders by type/idx, tables in PowerPoint's default style, real charts with cached data, footers as slide
  placeholders, `p14:dur` transitions, `p:timing` effects through a preset table both ways, `p14` sections): our
  files round-trip exactly (a stress deck in the test: every shape, effect, transition, chart kind) and pass the
  `pptx` skill's OOXML schema validator; LibreOffice renders them faithfully. Read generically: PowerPoint's
  placeholders inherit position and text formats from layout and master, theme colour modifiers (tint/shade in
  linear light), style references, groups flattened, `mc:AlternateContent`, every master's layouts (LibreOffice
  writes one master per layout). Known: LibreOffice swaps the direction of push / cover transitions when it resaves
  (ours follow PowerPoint: `dir` is the motion), and drops sections; percentage spacing (`spcPct` before/after)
  is not read. `SD:/docs/cafe-2026.pptx` ships in `slides-samples`; `tools/tests/slides/powerpoint.pptx`
  (python-pptx, PowerPoint's template) feeds the reader test and `screenshots/slides-pptx.png`. Also fixed: an
  uninitialised point count in `render.h`'s `PolyB` (an ellipse's outline could hang the renderer). The host
  harness note: the simulator's heap sits at a fixed address, so a one-off test binary may need `setarch -R`.
  **Packages**: `slides` / `slides-samples` 1.0.1 (the `.pptx` sample); the same publish made **`jet` 2.0.4 without
  its program** (this checkout had no `sdcard/apps/jet.app/main`, not in git) -- republished at once as 2.0.5 with
  it (taken from 2.0.3's package); `tools/pkg/publish.sh` now takes Jet's program back from the last package
  itself, or stops.
- **The master view** (`master.h`, View ▸ Master and Layouts): the master and the 8 layouts as slides, edited with
  the normal tools; the samples' formats become the text styles; layouts' placeholders; the slides follow the
  layouts' new places (unless moved by hand); one undo step. Test in `slides_test.cpp`; `screenshots/slides-master.png`.
- **Find and Replace** (`find.h`, Edit menu, Ctrl+F), **effects by paragraph** in the show (`show.h`: the sample's
  slide 5 list), **notes pages and handouts** (2, 3 with lines, 6) in Export as PDF. All tested
  (`slides_test.cpp`: 41 checks).
  **Next**: groups (a model change: render, ODP, PPTX, the editor), a vector PDF (the text as text).

## QBStudio, the IDE for desktop apps in BASIC: mock-ups (2026-10-04)

- Asked by the user: an IDE for Onyx BASIC aimed at desktop apps, without touching `qbasic`; a GUI designer in the
  way of Visual Studio's WPF one, a light layout format (a control a line, the indentation for the parent), the
  window's code generated. **Study and five mock-ups**: `docs/qbstudio/README.md` (`tools/screenshot/mockup_qbstudio.py`):
  the designer (layout containers, the `.form` text kept in step), the code (event SUBs, controls as objects,
  completion), debugging, a new project, the generated `Main.form.bas`. The BASIC additions it needs are listed
  there (objects and properties, `Move`, menus, resize, `$INCLUDE`, a debug channel). **Approved by the user**, who
  named it **QBStudio** (`qbstudio`).
- **Built (2026-10-04)**, the first version: the BASIC additions (`PROPERTY`, `MOVECONTROL` / `SHOWCONTROL` /
  `ENABLECONTROL` / `FOCUSCONTROL`, a resizable `WINDOW` and its -2 event, `WINDOWWIDTH` / `WINDOWHEIGHT`,
  `MENUITEM`; `/bin/basic -s <service>`), the core (`form.h`, `gen.h`; `run_qbstudio_test.sh`: 17 checks; BASIC's
  `t19_forms`), the app (`user/Apps/qbstudio/`: the designer with the toolbox's drag and drop, the properties and
  events, the form's text kept in step, the code editor with colours, completion and problems as you type, the
  object / event lists, Run, Check, Make App, New Project's three templates), the example `SD:/projects/converter`.
  Screenshots `qbstudio.png`, `qbstudio-code.png`; docs 04 §13 *QBStudio*, 03 (*Onyx BASIC*).
  **Next**: the debugger (the mock-up: breakpoints, stepping, variables -- a debug channel in `/bin/basic`), several
  windows in a project, a Timer and a ToolBar's icons, `'$INCLUDE`, the PC runtime (`pc/`: rebuild `obcore.dll`
  for the new statements -- their `Host` virtuals default to nothing).

## Letters, a word processor (2026-09-29, same branch, pushed to `main`)

Asked by the user: Letters "toward AbiWord", no printing, FreeType from the NetSurf work, drawn by
Letters itself (no RichTextBox). Done:

- **The apps' FreeType** (`user/Kits/fontkit/`): TrueType only, auto-hinted, anti-aliased, kerned; built by
  `user/Makefile` into `ft/libft.a` (NetSurf keeps its own). `fontkit/fonts.h`: the card's families
  (`SD:/res/fonts`, `SD:/fonts`), sized fonts, a glyph cache at quarter pixels, the drawing.
- **Letters** (`user/Apps/letters/`, a newlib app now: `letters.elf` rule): pages (A4, margins,
  page numbers), styles, fonts, sizes, B/I/U/S, super/subscript, colours, highlights,
  alignments, indents (the ruler's markers dragged), spacing, lists, page breaks, images (PNG /
  JPEG / BMP / GIF / WebP, resized with a handle), undo / redo, rich copy / paste, Find and
  Replace, Special Character, Date and Time, Word Count, Page Setup, zoom, formatting marks;
  RTF read / written with all of it, text, HTML export; a recovered document after a close with
  unsaved changes. `.rtf` files now open in Letters (`sdcard/etc/fileassoc.ini`). The docs:
  `docs/04` *Letters, the word processor*, `docs/03` (TrueType text, `VPath`, Letters' pieces).
- **uikit**: `uikit/vpaint.h` (`VPath`: anti-aliased vector shapes, integer); `img_load_mem`.
- **Sample**: `sdcard/docs/letters-tour.rtf` (`tools/gen_letters_sample.py`); the screenshot
  `screenshots/letters.png`. The desktop simulator's script has `mods N` (modifier keys).
- **Next ideas**: done since — tables, headers / footers, fields, tab stops, a table of contents,
  `.docx` / `.odt`, the mail merge: see *Letters as Word* below.

## Paint, as Windows 11's, with layers (2026-09-29, same branch, pushed to `main`)

Asked by the user right after Letters. `user/Apps/paint/` rewritten (still a freestanding integer
app): the ribbon (Edit, Image — select, crop, resize / canvas size, rotate / flip —, Tools — pencil,
fill, eraser, colour picker, magnifier, brush —, fifteen Shapes inscribed in their box's ellipse
with outline / fill, Size, Colours — 1 and 2, twenty, ten custom, Edit —, View — the pixel **Grid**
toggle, Fit), transparent **layers** (eye, opacity, add, duplicate, delete, move, merge, flatten),
a floating selection (moved, nudged, turned, flipped; a click outside puts it down), zoom 12 % –
3200 %, undo by tiles. **Save** = OpenRaster (`.ora`, the layers; GIMP / Krita read it); **Open**:
`.ora`, PNG, JPEG, BMP, GIF (WebP, PCX); **Export**: PNG, JPEG, BMP, GIF (flattened) — the writers
in `user/Kits/imagekit/img/pngsave.hpp` (deflate, PNG, JPEG, GIF, BMP, ZIP), `img_inflate` in imgload.hpp. A
closed-unsaved picture is recovered. Screenshots `paint.png`, `paint-grid.png`; docs 04 *Paint*,
03. **Next ideas**: a text tool (it would make Paint a newlib app: `fontkit/fonts.h`), free-form
selection, a selection resized by handles, brushes with soft edges, a gradient fill.

## Cardfile, a small database (2026-09-29, same branch, pushed to `main`)

Asked by the user ("a simple Access without SQL"). Done (the user guide: docs/04 *Cardfile, a
small database*; the pieces: docs/03):

- **Cardfile** (`user/Apps/cardfile/`, integer only): one `.card` file = a **form** (a title, a
  description, fields: display name, column name, type — one-line text, multi-line text, integer,
  decimal with its decimals, date, colour, yes / no, a choice list with its choices) and its
  **records**; the format (text, INI-like head, the records tab-separated) is at the top of
  `main.cpp`. Three views: **Form** (an index card, an editor a field, the navigator, validation
  when a record is left), **List** (a grid: sort by a title, again the other way; columns widened
  by their edge; double-click → the form), **Design** (fields added / removed / moved / named /
  typed; a new type converts the values, asked first when some would be emptied). Search (every
  word, any field), Undo / Redo (the whole document kept before each change), CSV export and import
  (the types guessed), a document kept at a close with unsaved changes (`recovered.card`).
- **uikit**: `uikit/datagrid.h` — `DataGrid`, a virtual table (docs/03).
- Samples `SD:/docs/books.card` (every type) and `contacts.card`; `card = cardfile` in
  `fileassoc.ini`; the icon by `tools/gen_assets.py cardfile`; screenshots `cardfile.png`,
  `cardfile-list.png`, `cardfile-design.png`; host test `sh tools/tests/run_cardfile_test.sh`.
- **Next ideas**: a cell edited in place in the grid (Access's datasheet); the form's layout
  (two columns, a field's width, a memo's height); a default value per field, required fields;
  computed fields; an image field; printing / a report; a lookup into another `.card`.

## The Spreadsheet, as LibreOffice Calc / Gnumeric (2026-09-29, same branch, pushed to `main`)

Asked by the user ("un peu plus poussé comme gcalc": read as LibreOffice Calc / Gnumeric). The old
`sheet` rewritten from scratch (the user guide: docs/04 *The Spreadsheet*; the pieces: docs/03):

- **`user/Apps/sheet/`**, a **newlib** uikit app (`sheet.elf` rule, FreeType), `stack = 4M` in its
  `app.txt`. The engine (plain C++, the same on the PC): workbooks of sheets of 1 048 576 × 16 384
  cells in a hash of the used ones; Excel's formula syntax (references relative / absolute, to other
  sheets, whole rows / columns, arrays, names), **237 functions**, full recalculation at each
  change (on demand, an explicit stack for deep chains, `#CIRC!`), Excel's number formats, typed
  entries read as Calc does (numbers, %, amounts, dates day first, times), interned styles, merged
  cells, frozen panes, rows / columns inserted / deleted with every reference moved, copy / cut /
  paste (Paste Special), fill series, sort (3 keys), Find and Replace, **defined names**,
  **conditional formatting** (value, text, top / bottom, average, duplicates, formula; colour
  scales, data bars), the **AutoFilter**, **charts** (column, bar, line, area, pie, scatter,
  anchored to a cell), undo / redo (100 steps).
- **Files**: `.xlsx` read and written (styles, formats, merges, sizes, panes, charts, conditional
  formats with their dxfs, AutoFilter, names — LibreOffice opens them as written); `.ods` read
  (LibreOffice's styles, number styles, charts, calcext conditional formats, database-range
  filters, named ranges); CSV / TSV read and written. `xlsx`, `ods`, `csv`, `tsv` = `sheet` in
  `fileassoc.ini` (**`.csv` used to open in the text editor**). A workbook closed unsaved is
  kept as `SD:/apps/sheet.app/recovered.xlsx`.
- **Sample** `SD:/docs/cafe-2026.xlsx` (made by `tools/tests/sheet/make_sample.cpp`); screenshots
  `sheet.png`, `sheet-filter.png`, `sheet-loan.png` (the `sheet` scenario of `shots.sh`); host
  tests `sh tools/tests/run_sheet_test.sh` (328 engine checks, 122 file checks; LibreOffice round
  trips when `soffice` is installed).
- **The euro sign**: uikit's font has it in slot 0x80 (Windows-1252's; `gen_nssans.py` `EXTRA`);
  **AltGr+E** now types it on the FR, BE, DE, ES keymaps (AltGr+4 on UK; IT and US had it) —
  `tools/keymaps/maps/*.h` → `genkeymaps.py`. The Spreadsheet takes it; uikit's `Textbox` /
  `Textarea` still ignore 0x80 (they accept 0x20–0x7E, 0xA0–0xFF).
- **Next ideas**: spilled dynamic arrays (and with them SORT, UNIQUE, FILTER, SEQUENCE); copy /
  paste does not carry conditional formats, and a cut / paste moves the cells' formulas but not the
  names' nor the rules' (rows / columns inserted or deleted move all of them); comments; data
  validation (drop-down lists); pivot tables; `.ods` writing; printing / PDF; the € in uikit's
  text boxes.
- **The freeze at its first start on the Pi (fixed, `73a1eb05`).** Its window was 1060 pixels
  wide; the kernel then made none over 1024 × 768 (`CreateWindow`, `sys/kapi.cpp`; since v66, none
  bigger than the screen) and returns a
  null canvas, which uikit drew into: an app then ran at EL1 with the kernel's identity mapping, so the
  first frame overwrote the kernel at address 0 (at EL0 since v74, that is a fault: the app killed) — the Pi froze, nothing in `kmsg`, no
  `lastcrash.txt` (a Pi without RAM above 3 GB keeps no record, and a panic halts core 1 too), the
  watchdog restarted it. Now 1000 pixels; uikit's `Root` stops an app the kernel gives no window;
  the desktop simulator refuses windows over 1024 × 768 (its screen) as the kernel does. How it was found, and
  worth reusing: the Pi binary itself run under **qemu-aarch64** with the simulator's kapi (a
  loader mapping the ELF's segments, the kapi table at `KAPI_TABLE_VA`, a 4 MB stack with a guard
  page), valgrind and ASan on the simulator build (for ASan, a copy of `kern/kapi_abi.h` with
  `KAPI_TABLE_VA` moved out of its shadow). **Kernel follow-ups worth doing**: unmap the first
  pages of the identity map in the apps' address spaces (a null pointer would fault instead of
  writing over the kernel), and stop a faulting app instead of the kernel panic (any app fault
  takes the whole Pi down today).

## Letters as Word: tables, pages, fields, .docx / .odt; Cardfile's mail merge (2026-09-29, `claude/happy-wright-wg38ez`, pushed to `main`)

Asked by the user: Letters pushed further (".odt, .docx, tables, a table of contents, headers and
footers, pagination") and, in Cardfile, a mail merge with a Letters letter (one record → one
document; all the records → a series of documents). Done (the user guide: docs/04 *Letters* —
*Tables*, *Pages*, *The mail merge* — and *Cardfile*; the pieces: docs/03):

- **The model** (`doc.h`): stories (the body, the header, the footer, the first page's own; one
  edited at a time), tables (a run of paragraphs saying their cell; the `Table` — widths, heights,
  spans, shading, lines, heading row — a value replaced whole by an edit, so undo covers it), fields
  (`FIELD_CHAR` + a `Field`: page, pages, date / time with a picture, merge field), tab stops with
  alignments and leaders, keep with next / lines together / widow control, TOC and header / footer
  styles. **The layout** (`layout.h`): boxes (the text's width or a cell's), the headers and
  footers first (the body's top follows the header's height), a `Pager` (headings kept with their
  next line, widows / orphans, a table broken between its rows with its heading row repeated).
  **Editing** (`edit.h`, `view.h`): the table commands (rows, columns, merge, split, even widths,
  properties), Tab from cell to cell, a column border dragged (page or ruler), a double click on a
  header / footer (the body greyed), Insert ▸ Page Numbers / Field / Table of Contents, Tools ▸
  Update Table of Contents, Format ▸ Tabs.
- **Files**: RTF extended (tables, headers, fields, tabs, the TOC's field); **`docx.h`**
  (WordprocessingML) and **`odt.h`** (ODF) read and written, over `xml.h` (a pull reader on a zip
  entry; the zip written with `pngsave.hpp`'s deflate). **Tested** by `sh tools/tests/run_letters_test.sh`
  (14936 checks: a document with all of it through RTF, .docx and .odt, and LibreOffice's
  conversions of ours when `soffice` is installed — `apt install libreoffice-writer` in the cloud
  container; valgrind clean with `VG=1`). `tools/tests/letters/conv.cpp` converts a file.
- **The mail merge**: Letters' `merge.h` (the data read with Cardfile's own `model.h`; Tools ▸ Mail
  Merge: fields inserted, values previewed, merged to a new document or to files named after a
  field) and `writer --merge JOB` for Cardfile's **Record ▸ Mail Merge** (`MergeBox`: this record or
  all those shown; one document or files; the form's `merge` key remembers the letter).
- **Fixed on the way**: a question asked at an app's start (the recovered document of Letters,
  Cardfile, Paint, the Spreadsheet; a merge's end) got no click nor key — `Root::run` hooked the
  pointer and the keys; they are hooked (`root.attach ()`) right after the `Root` now. **Keep it
  so in a new app that asks something before `run ()`**: the kernel drops a window's events while
  it has no handler. `xml.h`: `XBuf::str ()` of an empty value was not ended (valgrind).
- **Samples**: `SD:/docs/letters-tour.rtf` (two pages: a TOC, a header / footer — the title page's
  own —, a table) and `SD:/docs/new-year-letter.rtf` (the Contacts form's letter; `contacts.card`
  names it), both by `tools/gen_letters_sample.py`; `.docx` / `.odt` open in Letters
  (`fileassoc.ini`). Screenshots `letters.png`, `letters-table.png`, `letters-merge.png`,
  `cardfile-merge.png`.
- **Next ideas**: a table's rows split across pages (a row taller than a page runs over its foot
  today); text boxes and shapes (dropped when read); comments, tracked changes (read accepted);
  columns (newspaper); footnotes; sections with their own page setups; a mail merge's conditions
  (IF fields) and a filter on the records; printing / PDF.

## Ledger, Belgian accounting (2026-09-29, `claude/happy-wright-wg38ez`, pushed to `main`)

Asked by the user: "un logiciel de comptabilité soigné, professionnel et utilisable, pour une PME ou
un indépendant, avec le PCMN belge et la déclaration TVA XML (Intervat)" — customers / suppliers,
purchases / sales, misc. operations, general ledger and journal; GnuCash as the reference for the
look, BOB 50 for the features; then documents from templates (quotes, orders, delivery notes) and the
general ledger, income statement and balance sheet in Letters or the Spreadsheet. Done (the user
guide: docs/04 *Ledger, the accounts*; the pieces: docs/03):

- **`user/Apps/ledger/`** (integer only; the engine plain C++, tested on the PC): the PCMN (French /
  Dutch), parties (VAT numbers and IBANs checked, a VAT situation choosing the codes, a language for
  the documents), sales / purchase invoices and credit notes (Odoo's Belgian VAT codes, reverse
  charges, half-deductible cars, the entry shown as typed, a sale's structured communication), bank
  and cash statements (open items ticked, matched), misc. operations, matching, the fiscal years
  (closed: the result appropriated), the VAT grids and Intervat's checks, the return's XML, the
  settlement (451200 / 411200), the customer and intra-Community listings, reports (journals, general
  ledger, trial balance, balance sheet, income statement, balances, ages, a party's account, VAT
  detail) to Letters (RTF), the Spreadsheet (.xlsx) or CSV.
- **Quotes, orders, delivery notes, purchase orders** (`commerce.h`, `commerce_ui.h`): numbered by
  kind and year, each becomes the next, then the invoice (posted, the document marked invoiced).
- **Printing from templates** (`print.h`): the data written as Cardfile forms (the document's, its
  lines'), then `writer --merge`: Letters' merge job has a new key, **`lines`** — the template's table
  row holding `Line...` fields repeated per line (`merge.h`, `merge_lines`). The templates (French,
  `nl/`, `en/`, and `fields.card`) by `tools/ledger/gen_templates.py`; Settings ▸ Printing edits them.
  Letters' RTF reader now tells a table's lines apart (rows only → `TB_ROWS`; test added).
- **CODA import** (`coda.h`): the bank's statements, their parties and invoices found; the statements
  shown one after the other to complete (`main.cpp`'s queue, `StatementPage::loadImport`).
- **SEPA payments** (`sepa.h`, `payui.h`): pain.001.001.09 (hybrid addresses, as Febelfin asks from
  November 2026), validated against ISO's schema; the invoices flagged `P` (Transfer sent).
- **Demo** `SD:/docs/demo-company.ledger` + `SD:/docs/demo-bank-statement.cod` (made by
  `tools/ledger/make_demo.cpp` through the engine: 2025 closed, 2026 to September, quotes and orders);
  `ledger = ledger` in `fileassoc.ini`; the icon (`tools/gen_assets.py ledger`); screenshots
  `ledger*.png` (the `ledger` scenario of `shots.sh`, `ledger-print` through Letters); host test
  `sh tools/tests/run_ledger_test.sh` (200 checks; the XML validated when `xmllint` is installed).
- **The manual** (asked: "un manuel pour le logiciel de comptabilité, avec captures, en .md et en pdf;
  tout gros logiciel fera l'objet d'un manuel"): `sdcard/manuals/ledger/Ledger.md` + `Ledger.pdf` (55
  pages) + `images/` (43 pictures: `sh tools/manuals/ledger_shots.sh`; the PDF:
  `python tools/manuals/build_manuals.py`) — docs/03 *Manuals*. On the way: an opened statement or
  invoice saved again kept its matchings no more (`entry_save` now carries them; test added), a saved
  statement's movements show what they paid, a new statement proposes the bank's next number, the
  reports' and VAT page's columns fit, the demo's "Flémalle" in Latin-1. Then (asked) the manual in
  French and Dutch too: `Ledger.fr.md` / `.pdf` (59 pages), `Ledger.nl.md` / `.pdf` (60). **Next**: a manual reader app
  on Onyx (the same Markdown subset), then a manual for each big app (Letters, the Spreadsheet...).
- **Next ideas**: **e-invoicing** — Belgium requires structured B2B invoices through **Peppol** from
  2026: a sales invoice as UBL (Peppol BIS Billing 3.0) and a purchase UBL read would be the most
  useful next step; CAMT.053 statements (the XML successor of CODA); payment reminders from the
  overdue invoices (a template like the others); an articles catalogue for the quotes' lines (their
  prices, units); recurring invoices; invoices typed as quantity × price like the quotes; a party's
  statement printed; analytic codes; foreign currencies; the annual accounts (NBB) not done.

## The GameCube on the Pi -- the TEV renderer (the black screen: fixed; next: the speed)

- **Done (cloud session), all pushed:** option A of `docs/GC-WINDOWS-REPORT.md` §5 -- the GX on the
  V3D with generated QPU shaders:
  - **Kernel ABI v61** `gpu_program` / `gpu_render2` (`kernel/sys/v3d.cpp`): the app's own
    vertex / coordinate / fragment shaders, batches with uniform ranges, up to 8 textures, blend
    factors, write mask, scissor; generic CPU clipping (`V3DClipTriangleN`).
  - **QPU toolchain**: `user/Libs/v3d/qpu.h` (C++ instruction builder over Mesa's packer),
    `user/Libs/v3d/shaders.h` (pass-through VS / CS, simple FS), `tools/qpu/qpulib` (instruction
    restrictions checker), `tools/qpu/qpusim` (fragment-shader simulator).
  - **TEV generator** `user/Libs/v3d/gxtev.{h,cpp}`: a TEV configuration -> fragment shader in
    integers as the hardware; `user/Libs/v3d/gxtev_ref.h` = gxgl.cpp's GLSL TEV in C++. Checked:
    `tools/tests/run_qpu_test.sh` (thousands of random configs in the simulator, exact) and on
    the Pi: `/bin/v3dprog` -> **ALL PASS 24/24** (incl. 11 TEV configs, Wind Waker's first two).
  - **gcemu backend** `user/Apps/gcemu/gxv3d.h` (`Rec`: gc::GxGpu on the app core -- vertices
    through gxgl.cpp's VS ported to C++, TEV program cached by key, uniforms, state; `Out`: main
    thread -> gpu_program / gpu_texture / gpu_render2). View > TEV Shaders On / Off.
  - **PC harness** `tools/tests/gc/gcv3d.cpp`: runs a `.dol` / `.iso` with the same `Rec` and
    draws the last frame with a software V3D (the shaders in qpusim) -> `.ppm`. Build line in
    `tools/tests/run_gc_test.sh` (on Windows: WinLibs MinGW `g++`, see its line); options in its
    header (`GCV3D_EVERY`, `GCV3D_DUMP=1/2`, `GCV3D_SAVE`, `--replay`).
- **The black screen -- fixed (local session, tested on the user's Pi with the ISO).** Three bugs,
  one behind the other:
  1. **The GPU hung** on the first TEV frame (`gpu_render2` -3, kmsg "rendering timed out ... GPU
     left off", then -1 until a reboot). The TEV shaders without texture lookups were flagged
     `KAPI_GPU_P_FS_FINAL` (start in the final thread section). `v3dprog ww` (a w x h target, one
     quad with WW's first TEV program) showed it: hangs now and then on targets >= 512 pixels wide
     (5 runs of 7, mostly the first frame after boot), never at 64 x 64 (the v3dprog tests). Now
     `gxtev` always ends with the last-segment pair (as Mesa): no hang since, in hours of frames.
     docs/02 §15.
  2. **The window was never redrawn** once the game ran slower than real time: gcemu's loop asked
     for the next field before looking for the new picture (`ec_pending` then never 0 there, the
     F12 line showed `draw 0.0 ms`). The picture is now taken first. **n64emu has the same loop**
     (a separate task was suggested to the user: same fix).
  3. **No texture reached the GPU**: `Machine::gxTexture` allocated the texture pixels with `new`
     on the app core -- `umm` is not safe across cores and `kapi_sbrk` from core 2 grows the heap of
     the task core 0 happens to run (seldom gcemu): `px` null (and the decoding wrote through it).
     Now a pool of `TEX_POOL` texels (64 MB) made with the machine; full, every texture is
     forgotten (`texFlush`, the draws re-resolve). docs/03 (the app-core rules).
  And `Out::render` kept only half of `KAPI_GPU_MAX_VERTS`: the island flyover (147 K vertices)
  lost its last 250 batches (the logo, the sea); now 7/8 of it (the rest: the clipping's room).
- **State:** The Wind Waker runs with the TEV on the Pi -- the title (island flyover, logo, "Press
  Start"), the intro story; frames dumped on the Pi and replayed on the PC give the same picture
  (<= 2 % of the pixels differ, on edges). Diagnostics for the next bugs: F12's third line, F9 /
  `--diag[=FTP:<pc>:<port>]` dumps, `gcv3d --replay` (docs/03).
- **The speed (local session, first pass).** F12's fourth line / `--diag` measure it (docs/03).
  The island flyover (~370 batches, 147 K vertices a frame, ~4800 draws of ~5 vertices a field)
  went from ~7 to ~12 fields/s, lighter scenes 20-33 (60 is real time):
  - the drawing overlaps the next field (`Out::prepare` while the machine waits, `Out::submit`
    after the request), and a frame already in the window is not drawn again (30 fps: half);
  - the recorder: only the colour channels the program reads, the alpha from the colour's
    lighting when the controls match, the lights read once a draw, the last TEV configuration
    matched without its hash -- 366 -> 271 cycles a vertex on the PC, exact (the pictures
    identical, `gcv3d`);
  - `gpuTexture` remembers each map's answer within a field (`texMemo`, `texEpoch`: a field, an
    EFB copy, a TLUT load, a DMA, the pool emptied): 7.7 -> 0.4 ms a field.
  Where a heavy field's ~75 ms go now (app core): the recorder ~29, the vertices decoded + the GX
  state ~7.5, the FIFO ~2.5, the textures 0.4, **the CPU (JIT) and the rest ~36**; the main thread
  (~9 ms copy + ~43 ms `gpu_render2` a frame, the kernel's CPU clipping mostly) now runs beside.
  Next, by gain / effort:
  1. **The CPU**: profile the JIT on the Pi (`jitProfile` / `GC_PROFILE` in gctest: the hot
     blocks; the instructions still interpreted, the FPR loads / stores per instruction, the
     dispatch) -- 36 ms a field alone is twice real time.
  2. **The vertex stage on the GPU**: a generated vertex shader (the XF transform, the lighting,
     the texgens: `Rec::record`'s work) fed with the decoded vertices and the matrices / lights as
     uniforms -- removes most of the recorder's ~29 ms; the per-draw setup (~22 % of it) could
     meanwhile be cut by reusing the last draw's program / uniforms when `GxState::serial` has not
     changed.
  3. A fast path in the kernel's `V3DClipTriangleN` for the triangles fully inside (most): the
     main thread's ~43 ms (not on the critical path any more, but it is the display's latency).
  4. Dual-issue scheduling of the generated TEV code.
  Then stage 4: EFB copies to textures (`rec skipped` counts the draws reading one: the heat haze,
  the bloom...), the fog, the indirect textures.
- **The speed, second pass (local session, 2026-09-28).** `netcore=0` on the user's Pi (both app
  cores free): the GX runs on core 3 while the machine runs on core 2 (docs/03, "The GX on its
  own core"). A command-by-command copy of the FIFO near its end made that slower at first (4 fps):
  the commands are now read in place. The Wind Waker (PAL, 50 fields/s) on the Pi: the title's
  heavy flyover ~20 fields/s (the GX core ~90 % busy, ~45 ms a field: the recorder ~32), the
  lighter scenes 35-50, the user's first steps on Outset ~15 fps (60-65 %). JIT (docs/03): the
  memory fast path (one EOR check, loads into the destination), compare + branch fused on the
  host's flags (`crDead`); `gctest fuzz` passes (GC_FUZZC too). The JIT profile (--jitprof,
  200 s): 7.1 host instructions a guest one; lwz / stw 8.5, bc 7.7, b 9.2, lfs 12.3, cmpli 9.4
  before those two changes; a block run ~10 guest instructions; the CPU part is ~90 % JIT code,
  IPC ~0.85 (L2 refills ~4 a thousand instructions).
  **Fixed: the camera "in Link's head"** (the user's report; on the PC it was right): not the
  camera -- the recorder framed the vertices with the copy registers of the draw's time, and a
  half-size EFB copy to a texture in the middle of the frame made the rest a 2x zoom of the top
  left quarter (the title screen too). Found by comparing `gcrun GC_GL=1` and `gcv3d` on the same
  scripted game (`GC_PAD`, `GC_CARD`, `GC_HASH`: the same MEM1, so the drawing). Now the EFB's
  space + `Frame::rect` (docs/03).
  Then (same day): the JIT's base pointers (a D-form access through a register found in MEM1:
  one load / store; little measured gain -- the CPU part is memory-bound), the code buffer's
  hot / cold chunks (L1I refills 20 -> 17.5 a thousand instructions, -7 % cycles), the
  recorder's and the vertex decoder's per-draw plans (the recordings bit for bit the same:
  `GCV3D_VHASH`). The CPU-bound scene of the intro: 20.4 -> 17.0 ms a field (50 fields/s, full
  speed); the user saw 15-20 fps in the game before the GX plans.
  Next, by gain: the JIT's hot code smaller still (the not-taken paths of the branches the
  compiler hints as unlikely out of line; the FP compares fused), a second, optimising tier for
  the hot code (whole functions, the registers kept across blocks, the hot code packed), then the
  display's `gpu_render2` (~20-45 ms a frame on core 0: the kernel's clipping and copies).
- **The speed, third pass (local session, 2026-09-28 evening).** Measured in the game (Outset's
  bridge, ~96k vertices a frame) with `--statlog=FTP:<pc>:2121` (F12's lines whole, every
  second; docs/03): 30.6 -> **~38 fields/s** (~19 fps of 25; F10 said 14-15 fps, 60 %, before).
  - LR, CR, r3, r0 kept in host registers across the blocks (the JIT's SRA), a call counting as
    setting CR0 (`crDead`), `gctest jitsize` (6.56 -> 5.85 host instructions a guest one on the
    Pi profile's hot blocks); `gctest fuzz` (GC_FUZZC, GC_FUZZ2 too) passes.
  - gcemu's loop: the TEV frame prepared and drawn after the next field is asked for, two fields
    asked for when a frame is drawn and the game is behind (the machine, its field done, idled
    ~13 ms each frame while `gpu_render2` ran), the main thread naps while the field's end is
    far (a yield in a loop: core 0 busy, heat).
  - The kernel's `ClipFrame2`: the triangles inside copied once, straight into the GPU's buffer
    (were four copies): `gpu_render2` 34 -> 25 ms a frame; its split in kmsg (docs/02).
  - Where it stands: the machine (core 2) ~26 ms a field, busy ~99 % — the bottleneck; the GX
    (core 3) ~22-24 ms, ~87 %; the main thread ~32 ms a frame (prep 6.8, clip 9.8, GPU 14).
  - **The Pi was throttling**: kmsg `power: SoC 81-84 C ... soft temp limit NOW`, the cores
    ~1.3 GHz (the PMU's cycles against the time) instead of 1.5: a fan / heatsink is ~+15 %.
  - Measures (`--pmu=08,01,52,53`, docs/03): the machine's IPC 0.84, L1I refills 11.4 and L2
    read refills 3.2 a thousand instructions — its data from the RAM. `--nodraw` (the frames
    recorded, not drawn): the machine 37 -> 30 M cycles a field (IPC 1.02), ~45 fields/s, the GX
    core then 98 % busy: **the display's copies (core 0) cost the machine ~18 %** through the
    shared L2 / RAM, and the GX is right behind.
  - The JIT profile (`--jitprof`, 180 s, the game): 5.36 host instructions a guest one on the
    main paths; the branches ~33 % of them (bc 12.7, bclr 8.8 -- plus the dispatcher's --, b / bl
    7.7, bcctr 3.4), the loads / stores ~42 % (lwz 13.4 at 5.7 each, lfs 9.6 at 10.1), fcmpo 4.5 %
    at 14.8 each. The hottest loop (~17 %): a linked list searched through two function pointers
    a node (`8024A118..8024A150`: bctrl -> `8024A7E0` -> bctrl -> `80043E1C` -> blr -> blr), 22k
    nodes a field: four trips through the dispatcher a node.
  Then (same evening): the recorder leaves out the triangles behind the eye (~10 % in the game:
  85k vertices a frame for 96k, prep 6.8 -> 5.8 ms, the pictures the same), and **the JIT's calls
  / returns on the host's `bl` / `ret`** (a stack of pairs {landing, return address}: docs/03;
  `gctest calltest`, the fuzz pass): branch mispredictions 5.6 -> 2.0 a thousand instructions,
  **~42 fields/s** (21 fps) on Outset. Both app cores are now full: the machine ~23 ms a field,
  the GX ~22 (the recorder ~15.7 of it); core 0 ~29 ms a frame (prep 5.8, gpu_render2 23).
  Then the recorder's plans kept from a draw to the next (the draws: 4.6 vertices, ~90 % of them
  with the last one's state): the recorder 15.7 -> 8.2 ms a field, the GX core ~61 % busy (docs/03).
  **Only the machine is full now** (~23 ms a field: ~43 fields/s). Seen meanwhile: after a few
  hundred FTP transfers (`--statlog=FTP:...` every 5 s over long runs) the Pi's `ftpfs` no longer
  opened its passive data connections ("Passive data channel timed out" on the PC; a failed
  `cat FTP:... > file` then leaves cat's error in the file) until a reboot -- not investigated;
  `--statlog` on `SD:/gcdump`, read with `cat` over telnet, avoids it.
  Then **kapi v62 `gpu_render3`** (the kernel reads the recorder's frame where it is: each batch's
  offset and stride, framed on the way) and the recorder's three frames (the one drawn is never
  built into): `Out::prepare` 5.9 -> 0.1 ms, the machine 33.5 -> 29.9 M cycles a field, **~46-47
  fields/s (~23 fps, 94 %)** on Outset -- measured: without the display at all (`--nodraw`) the
  machine runs at full speed (26.8 M cycles, IPC 1.14); `--noren` (prepared, not drawn) said the
  copy in `Out::prepare` cost it ~2.9 M cycles, the kernel's copy + the GPU ~3.8 M. The machine's
  time is 98 % in the JIT's code (--pmu: `in JIT`), its runs end ~310 times a field on VI lines
  and ~80 on the audio DMA, the JIT is entered ~2000 times (mtmsr / mtspr / rfi / sc leave it).
  Then **kapi v63 `gpu_vbuf`**: the recorder's frames in GPU-visible memory, `gpu_render3` draws
  them in place (framed in place, runs of the inside triangles, the clipped ones into the
  buffer's end): no copy left (docs/02). The user's game then: ~48-49 fields/s (~24 fps, 92-98 %)
  in the scenes played, the GX core ~44 % busy. Not yet measured against v62 on the same spot:
  the kernel's pass still reads every vertex's position and writes its x / y back (the framing),
  ~85 ns a vertex against ~105 for the copy.
  What is left, estimated: the next section.
- **gcemu's speed: what is left, estimated (2026-09-28, end of the day).** Where it stands (The Wind
  Waker PAL, Outset; the Pi throttling at 80-83 °C): **~46-49 fields/s of 50 (92-98 %), ~23-24
  fps of 25** -- "perfectly smooth" on the user's TV. The machine (core 2) is the only full core: ~29-30 M cycles, ~21 ms a field; the
  GX core ~44-69 % busy (9-14 ms a field); core 0 ~17-21 ms a frame (the kernel's pass ~3.5 ms,
  the GPU ~13 ms). Full speed everywhere needs the machine at <= 20 ms a field: its cycles down
  by ~5-10 % here, more in heavier scenes. The gains below are on the machine's time unless said,
  from the measures of this session (`--pmu`, `--nodraw`, `--noren`, `--jitprof`); they do not
  simply add up.

  | # | What | Estimated gain | Effort, risk |
  |---|---|---|---|
  | 1 | A fan / heatsink on the Pi (kmsg `power: SoC 80-83 C ... soft temp limit NOW`: the cores ~1.3-1.4 GHz instead of 1.5) | +7-15 % on every core | none (hardware) |
  | 2 | Then an overclock (`arm_freq` 1750-2000 + `over_voltage` in config.txt, with the cooling) | +15-30 % more | the user's call (boot config) |
  | 3 | The display's last traffic: the XFB framing in the vertex / coordinate shaders (`user/Libs/v3d/shaders.cpp`: 4 more uniforms, 2 fmul + fadd a coordinate) so the kernel only reads the positions -- or the recorder flagging the batches wholly inside (it has the positions; a conservative guard band) so the kernel skips them. The display still costs the machine ~9 % (`--nodraw`: 26.8 against 29.3 M cycles) | -3-5 % (the kernel's pass 3.5 -> < 1 ms a frame) | medium (QPU code, v3dprog) |
  | 4 | JIT, small: the call landing's cycle check (its return checked them: -2 instructions a call), `and` / `orr` immediates on blr / bctrl (-1 each), the CR field from an NZCV table (`mrs nzcv` + `ldrb`: 7 -> 5 instructions a materialized compare, the same results), fcmpo / fcmpu fused with their branch like the integer compares (14.4 host instructions each, ~4 % of the hot code) | -2-4 % | small, fuzz + calltest under qemu |
  | 5 | JIT: mtmsr / mfmsr native (OSDisable / RestoreInterrupts: ~550 of the ~2000 exits to C a field; each one also empties the call / return pairs): exit only when EE comes on with an interrupt pending, or IR / DR change | -1-2 % | small-medium |
  | 6 | Fewer run ends: a VI line (~310 a field) only when a VI interrupt can fire on it; the audio DMA's ~80 | -0.5-1 % | small |
  | 7 | Fastmem: MEM1 mapped so that a load / store needs no address test (`and` + `ldr` + `rev`), the kernel forwarding an app core's data abort (`AppCoreOnFault`) to a handler that patches the site to its slow path (Dolphin's backpatching): -3 instructions an access, ~15 % less host code (the L1I: 11 refills a thousand instructions) | -5-10 % | large (kernel + JIT + tests) |
  | 8 | A second JIT tier for the hottest code (whole functions / traces, the registers kept across their blocks, the code packed): the hot 80 blocks are ~47 % of the time | -10-20 % | large |
  | 9 | The GX (not the bottleneck now): the recorder's vertex loop on NEON, four vertices at once (-30-50 % of its 5-8 ms); indexed geometry / packed attributes (less memory traffic: the machine -1-3 %) | GX only, machine -1-3 % | medium-large |

  Likely path: 3 + 4 + 5 give ~50 fields/s in the scenes measured; with 1 there is a margin; 7 and
  8 are for heavier scenes and games. Not worth it: a degraded display (the sea left out...) --
  the machine emulates the game's CPU whatever is drawn; SVE / SVE2 -- the Cortex-A72 has NEON
  only, and the JIT already keeps the FPRs and paired singles in NEON registers.
- **Testing on the Pi yourself** (on the user's network; ask its IP -- it was 192.168.0.7):
  - a console: `telnet <pi-ip>` (telnetd, port 23; or OnyxRemote's Console button). If telnetd
    stops answering (a process spinning, see below): in the Pi's Terminal `ps`, then
    `kill <pid> --force`; or `reboot`.
  - files: an anonymous FTP server on the PC (e.g. Python's `pyftpdlib`, port 2121) and on the Pi
    `cat FTP:<pc-ip>:2121/<file> > SD:/<path>` (ftpfs; `cp` does not go through it); gcemu's
    `--diag=FTP:<pc-ip>:2121` writes its log and dumps straight there. Or `ftpd SD:/` on the Pi
    (port 21). `apps/gcemu.app/main` and `bin/*` need no reboot, `kernel8-rpi4.img` does.
  - run: `run gcemu SD1:/roms/ZeldaWIndWaker/ZeldaWIndWaker.iso --diag=...` (the second
    partition); `v3dprog` must pass after any GPU change; a GPU hang leaves the GPU off until a
    reboot (`v3dprog` then says `no GPU: V3D: stopped`).
  - pitfalls: `grep` / `wc` read stdin only (`wc < file`; with a file argument they wait).
    **A telnet session that drops ends its shell and what it runs** (telnetd, 2026-10-05,
    `user/BinUtils/shellend.h`, `tools/tests/run_telnetd_test.sh`; before, a program still running kept
    its `cmd`, then both waited for ever on a pipe nobody read -- state R, no system call -- and
    the leftovers slowed everything): a daemon to leave on the Pi is started with
    `run SD:/bin/ftpd SD:/`, not in a session's foreground. **Still open, in the kernel's
    network**: (1) now and then a connection a server accepts is deaf and mute (3 times in a dozen
    connects to a telnetd started on port 2323 a few seconds before -- its first or second
    client --, once the first telnet after a boot; not reproduced at will): the
    PC's connect succeeds, no greeting comes, what it sends is never received -- its close
    neither --, the next connect is reset, the ones after work; on the Pi it stays ESTAB for ever
    (`netstat`), and nothing sent on it makes the TCP give up. telnetd ends such a session after
    5 min (not one byte ever received); the others (vncd, rdpd, ftpd) were not looked at. (2)
    Circle's retransmission timer starts again at every segment sent (`SendNewSegment` ->
    `StartTimer`), so a peer sent to more often than the backed-off timeout never times out
    (a keepalive every 30 s kept a dead connection alive: telnetd's is every 5 min).
    `kmsg` streams until Ctrl+C and consumes the log (each line is read once).
  - the picture: `OnyxRemote.exe` (`pc/dist/`, rdpd port 3390); or VNC (vncd, no password):
    `python -m vncdotool.command -s <pi-ip> capture x.png`, `... key p` (a key).

## Koton, the studio -- a DAW (2026-09-29: implemented, not yet run on the Pi)

Koton Studio (the user's C# DAW, `github.com/stephaneweg/MusicTracker`) made again for Onyx, no
score view: **`docs/daw/README.md`** (the study, the plan, the user's decisions in §8, **where it
stands and what to test on the Pi in §9**). Code: `user/Apps/koton` (engine/, synth/, plug/, ui/,
main.cpp), `user/kplug*.h` + `user/Apps/kp_*` (the plugins), `user/BinUtils/llm.cpp` (the AI's HTTPS
helper), uikit's text face (`user/Kits/fontkit/uikitface.h`) and studio widgets. Docs: docs/04 *Koton, the
studio*, docs/03 *A large app: Koton*, *Koton's plugins*, the `/bin/llm` section. Tests:
`sh tools/tests/koton/{synth,engine,ai,plug,plug_host}_run.sh`; the app on the PC:
`sh tools/tests/desktop_sim/shots.sh koton`. The card carries the GeneralUser GS SoundFont
(`sdcard/res/soundfonts`, licence beside it: its own package, `generaluser-gs`, that Koton and Media Player need).

**Koton for Windows (2026-09-30):** `pc/dist/Koton` (`sh pc/Koton/build.sh`, MinGW-w64): the Onyx
sources unchanged over `pc/Koton/winkapi.cpp`, the kernel's table on Win32 -- docs/03 *Koton for
Windows*. Checked under Wine (no sound card there: the silent drain); to try on a real Windows: the
sound (WASAPI), a USB MIDI keyboard, the window's resize by hand.

**Ledger for macOS (2026-10-01):** `pc/macOS` -- Ledger (and Letters, its printing) for Apple silicon,
the Onyx sources unchanged over `pc/macOS/hostkapi.cpp` (POSIX) + `cocoa.mm` (the window, menus, keys,
clipboard): docs/03 *Ledger for macOS*. Built **on a Mac** by `sh pc/macOS/build.sh` -> `pc/dist/macOS/
Ledger.app` + zip (not built here: no macOS SDK on Linux; nothing committed in pc/dist/macOS). Checked on
Linux by `sh pc/macOS/check.sh` (the POSIX half under a screen-less window: open, print, save, the card's
folders). To try on a real Mac: the first build (Apple clang's warnings), the window, Retina drawing, the
keys (Cmd, dead keys), resizing / full screen, the trackpad's scrolling, drop / Finder open, printing.

**Ledger in French (2026-10-01):** every word of Ledger wrapped `TR ()` (uikit's new `lang.h`, docs/03 *An app
in another language*), the catalogue `sdcard/apps/ledger.app/lang/fr.txt` (~930 words) + uikit's own
`sdcard/res/lang/fr.txt` (in the `onyx` package); EN | FR at the side bar's foot and in the File menu (Ledger restarts by itself).
Pi binary rebuilt (`sdcard/apps/ledger.app/main`, Arm GNU 13.3); the other apps not restaged (their old
uikit has no `TR`, fine). Checked: `sh pc/macOS/check.sh` (the switch, French pictures), the engine test.
To do: Dutch (`nl.txt`: the same keys), the manual's pictures in French, Letters' own words.

**User manual (2026-09-30):** `sdcard/manuals/koton/Koton.md` + `Koton.fr.md` and their PDFs
(`python tools/manuals/build_manuals.py <the .md>`), 25 pictures in `images/` by
`sh tools/manuals/koton_shots.sh` (clicks at fixed places on the demo song: move them if the layout
changes) -- docs/03 *Manuals*. Keep it in step when Koton changes.

**Performance** (why there is no audible latency, why the UI stays fluid, the rules not to
break): **`docs/daw/PERFORMANCE.md`**. Its TODO (none started): the arrangement's playhead drawn
over a cached canvas instead of the whole view redrawn at each tick while playing, redraw only
when the playhead moved a pixel, a *Low latency* setting (128 × 2 in the kernel, a 512-frame ring
≈ 20 ms) for live MIDI.

## End-user apps roadmap (decided with the user, 2026-09-30; Priority 1 done)

**How (the user, 2026-10-01)**: Screenshot first (laid out as Windows' Snipping Tool:
docs/screenshot/README.md), then the **Priority 1** apps **in their order**; for each, **mock-ups first**
for the user to validate, then the app -- polished, **worthy of a commercial product**.

Every new app: FreeType text through uikit's face, polished, its catalog entry in docs/04 and a
`shots.sh` scenario. In the user's priority order:

**Priority 1**
- **Music library** -- **done** (2026-10-01: *Media Player*, its section above; its videos done 2026-10-02) -- (audio player as a polished library app, in the way of iTunes / Rhythmbox):
  MP3, OGG, FLAC, WAV **and MIDI** (`.mid` played through MeltySynth + a SoundFont -- the synth
  is in `user/Apps/koton/synth/`, to share rather than copy); artists / albums / playlists,
  tags and cover art, a now-playing view, file associations.
- **Mail client** -- **done** (2026-10-02: *Mail*, its section above; works on the Pi with Gmail), as user-friendly as possible: IMAP / SMTP over TLS, an account wizard
  (well-known providers pre-filled), threads, attachments, drafts. **Contacts** = a Cardfile
  form: the mail client creates the `.card` structure, reads / writes it (address completion,
  "add sender"), and the file opens in Cardfile too.
- **PDF viewer** + **PDF export** in Letters and the Spreadsheet -- **done** (2026-10-01: *PDF Viewer*, its section above; MuPDF, the app AGPL; the export ours, MIT).
- **Screenshot** tool (screen / window / area; Print Screen key). **Done** (2026-10-01: its section above).
- **Photos** (the user's pick among the ideas of 2026-10-02: weather, EPUB reader, RSS, SSH, a KeePass-compatible
  vault, GPIO lab, home automation, backup, WebDAV, CalDAV / CardDAV, internet radio and podcasts, a code editor,
  chess, Matrix) -- **done** (2026-10-02: *Photos*, its section above).

**Priority 2**
- **Localisation** screen: the Keyboard applet becomes *Region & Keyboard* -- the country /
  time zone next to the layout (the time itself stays set by NTP automatically).
- **About / System**: a fuller successor to memmon (version, kernel, CPU, temperature, RAM,
  uptime, network, processes).
- **Presentations** (as Impress), to complete Letters / Sheet / Cardfile.
- **Quick notes** with a desktop widget that can be shown or hidden.
- (Storage applet: not for now. Updates: part of the future package manager / app store.)

**Priority 3–4**: **Clock** (moved here by the user, 2026-10-02): alarms, timer, stopwatch, world clocks (notifications through notifyd; a small service for the alarms when the app is closed). **Video player** -- **done** (2026-10-02: the Media Player's videos); the **app store / package manager** (see IDEAS.md below; `docs/pkg/README.md`: `pkg`, the Package Manager `pkgman`, the daemon `pkgd` done and tested on the PC, the repository `onyx-packages` published (signed with the user's key, kept off the repositories), `sdcard_lite`; the Game Library finds its emulators from their app.txt; next: try it on the Pi; tryboot: not for now).

**Priority 5**: a global **key vault** (encrypted secrets store) with seamless integration in
the apps that hold secrets (Wi-Fi, Lisa / Groq keys, mail passwords, Courier, ftpfs...).

## The shell made useful: text tools, scripts, the line editor (2026-10-04, branch `term_updates`; its second commit, the script language, is not merged; nothing staged, nothing published)

- **The script language** (`user/BinUtils/cmdscript.h`, at least a DOS `.bat`'s level; docs/04 §7 *Scripts*):
  variables (`name=value`, `$name`; handed to the children as their environment, read back by a
  child `cmd`), `$(command)`, `$((arithmetic))`, file patterns (`*.txt`: `cmd_glob_hook` in
  `cmdparse.h`), `if` / `elif` / `else` / `fi`, `while` / `until` / `for … in` / `done`, `break`,
  `continue`, `! command`; builtins `test` / `[ ]`, `echo`, `read`, `set`, `unset`, `shift`, `true`,
  `false`. A block typed at the prompt is read up to its end (`> `). Ctrl-C stops a loop of builtins
  too (the keyboard is polled between two commands; what was typed ahead is kept for the next
  reader). `ls` (files, several paths, `-l`) and `cat` (`-n`) rewritten on `tool.h` for the patterns.
  Tested: `cmdscript_test.c` (116 checks) and on the Pi as `cmd2` (a script with every construct, a
  child script reading a parent's variable, a block at the prompt, `read`, Ctrl-C in an endless loop).
  Not there: functions, `case`, here-documents, a block piped or redirected as a whole, background
  jobs; `rm` / `cp` / `mv` / `mkdir` / `touch` still read the old argument line (a name with a blank
  from a pattern breaks them).

- **Tools** (`user/BinUtils`, on `tool.h`; docs/04 §8): new `head tail sed ed sort uniq cut tr tee nl find
  date sleep hexdump diff`; `grep` (regular expressions, `-i -v -n -c -q -F`, files), `wc` (`-l -w -c`,
  files) and `echo` (the argv, `-n`) rewritten. `regex.h`: basic expressions, no alternation.
- **`cmd`**: `;` `&&` `||`, `#` comments, exit codes (`$?`), scripts (`cmd file [args]`, `cmd -c`,
  `source`, a command word ending with `.sh`: the current folder then `SD:/bin`), `$1`…`$9 $# $* $0`.
  Ctrl-C stops the stages (the programs after 40 ms, a child `cmd` after 2 s: it stops its own first)
  and the rest of the script; a stage that cannot start no longer leaves the others waiting; the end
  of cmd's own stdin is handed to the first stage. `cmd` exits through `kapi_exit` (main's return
  value is not the exit code).
- **Line editor** (`user/Include/lineedit.h`): the cursor in the line, the history (Up / Down), in the
  terminal (the typed line is drawn after the prompt, wrapped; it enters the scrollback when sent) and
  in `telnetd` (ANSI escape sequences read; `tools/onyx-telnet.py` sends them on Windows).
- **Tested**: `run_cmd_test.sh`, `run_tools_test.sh`, `run_telnetd_test.sh` (PC); on the Pi through telnet, the binaries
  under other names (`cmd2`, `telnetd2` on another port…): the tools, scripts, exit codes, Ctrl-C,
  Ctrl-D, the arrows. The terminal: in the desktop simulator only. **Not tried**: a `.sh` command
  word and Ctrl-C on a nested script with the real `cmd` in place (they need `SD:/bin/cmd` replaced).
- **To do after the merge**: `make` + `make stage`, then publish (`onyx`: `bin/`, the terminal);
  `python docs/build_docs.py` (no pandoc on this PC). Ideas: a pager (`more`), alternation in `regex.h`, a history kept across sessions, the redraw of a
  line longer than the telnet client's window (needs its width: NAWS).

## Other open items

### To do -- left open by the session of 2026-10-04 (Jet on WebKit, the network, the desktop)

Nothing below is started unless it says so. The user's order for the browser is in the first group.

**Jet (the WebKit port, docs/08)** -- in the user's order:
- [ ] The **host + web view** as a reusable component (a small host window, the web view attached),
      then the **web-view daemon** started at boot and the **lazy loading** of the program.
- [ ] **Web Audio**.
- [ ] **H.264 / AAC**: needs FFmpeg under the LGPL in Jet -- a licence decision that is the user's
      (CLAUDE.md, the licences rule): ask before pulling it.
- [ ] **Full-screen video**: put off by the user (2026-10-04), lower priority -- not to be started unasked.
- [ ] **WebGL**: later (our own ANGLE back end, Mesa's compiler only).
- [ ] **Mail's HTML view** (Jet's program run inside Mail's window): not tried on the Pi since Web
      became Jet; it has no pointer shapes yet (the page is drawn by another process: the shape has to
      come back through `webview_proto.h`).
- [ ] The user to check by hand: Jet's **Console** (F12).

**Network (docs/02 *The network*, docs/05 §26-27)**:
- [ ] The Pi **sends** at 3-4 MB/s without aggregation (A-MPDU TX lost frames during downloads: it stays
      off -- the test to pass before turning it on again: PC pings at 5 a second during a download
      limited to 1 MB/s, none lost).
- [ ] A rare **pause of one second** in a transfer: TCP's smallest retransmission timeout.
- [ ] A **newer Wi-Fi firmware**: does it aggregate soundly?
- [ ] One trial with **55 % of the pings lost** ("destination unreachable"), the network core's sleep
      off, on 2026-10-04: seen once, not reproduced, not explained. The signal was weak that afternoon
      (-74 dBm, 97-130 Mbit/s instead of 195-292).
- [ ] rdpd / OnyxRemote over **UDP**: asked once by the user; probably no longer needed (no more
      freezes during a page load or a video, the user's check of 2026-10-04) -- ask before doing it.

**Desktop**:
- [ ] Task Manager: a **CPU column** by process; the network's bytes are the sockets' payload, not what
      goes on the air (the driver's counters).
- [ ] Pointer shapes (kapi v81): the **terminal**, **Slides** and **QBStudio** (another session's apps),
      an **hourglass** while an app is busy.
- [ ] Windows resized by their frame (kapi v82): no **live resize** (an outline only), the size is **not
      kept** from one run to the next; a window can be dragged under the dock. The user to try by hand:
      every edge and corner, the smallest size, the stop under the menu bar, other apps than the Task
      Manager (Jet, Letters, the terminal); and the pointer shapes not checked on screen (the four
      arrows while a window moves, the hand on a link of Mail, the I bar in Letters, the cross in Paint,
      splitters, a list's column edges).
- [ ] The shell's `rm` cannot name a file with **spaces** (quotes are not understood: `rm "SD:/Downloads/a (1).dat"`
      fails), and `rm SD:/cookie.jar.db` failed without a word -- both had to be deleted through ftpd.
- [ ] The in-OS strings that still say **"Zircon"**: to rename to Onyx (CLAUDE.md's note).

**Publishing**: Jet's program (`sdcard/apps/jet.app/main`, 105 MB) is ignored by git and exists only
in the clone where Jet is built: `tools/pkg/publish.sh` run from another clone publishes a `jet` package
without its program (it happened: jet 2.0.2 was 2.6 KB). After a publish, look at
`pkgs/jet-*.opk`'s size (~43 MB).

- **gcemu, The Wind Waker: Link's eyes are missing** (the user, on the TV, 2026-09-28; to look at
  after the GUI work). Leads: the game draws the eyes and eyebrows after the hair with their own
  depth compare so they show through it, and uses the EFB's alpha around them -- a draw left out
  (`GCV3D_SKIPLOG` / F12's third line: the reason), the depth function or its precision (the V3D's
  against the EFB's 24 bits: z-fighting on the face), or the destination alpha (`dstAlpha`, the
  EFB's RGBA6 / RGB8 format) not kept. To start: F9 on a close view of Link's face, then
  `gcv3d --replay` (docs/03) -- the recorder's batches for the eyes, their state and textures.
- VNC (`vncd`): the image froze while the sound went on, OnyxRemote (rdpd) kept working —
  not investigated yet.
- Ideas (IDEAS.md): an ISO9660 driver + `mount` of ISO / disk / partition images as volumes
  `VD0:`, `VD1:`…; an mstsc-compatible RDP server (~3000–4500 lines, TLS without NLA); the AX
  microcode's audio for the GameCube (Dolphin's `AXUCode`); NintendoEMU keyboard remapping;
  a task scheduler (as Windows': at boot, every x minutes / hours / days, on given weekdays;
  runs any executable -- an app, `bin/`, a `.bas`); packages as archives mirroring the card's
  root (`apps/calc.app/…`, `bin/…`, even `kernel8-rpi4.img`: one format for apps and system
  updates) with a manifest each, in a public repository with an index; a package manager (an
  applet of the Settings app: new apps, available updates, manual / automatic per app) and an
  update daemon run by the scheduler (updates the "automatic" ones) or once from the applet (a
  checklist of what to update); a `pkg` command (`add` / `delete` / `update [-a]` / `upgrade` /
  `list [-a]`). Details in IDEAS.md.
