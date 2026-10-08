# PocketUI: the desktop, pocket and console modes — a technical study

> **Where it stands (2026-10-08):** phases **P1** (the kernel: kapi v97), **P2** (the window API in UIKit) and **P3**
> (`user/Servers/common/`, PocketUI's skeleton `user/Servers/pocketui/`, its protocol `user/Kits/uikit/port/pocket.h` —
> beside `elegant.h` rather than in the server's folder —, the pocket UIKit `lib/pocket/uikit.so`,
> `tools/libgen/abi_same.py`, the PC test `tools/tests/server_sim`) and **P4** (the sessions: `SD:/etc/session/*`, `/bin/session`,
> SystemKit's `session.h`, the autostart's split by `pkg commit`, the Mode applet `modeconf`; the pocket session starts the
> menu bar — the user's decision —, the console's none; Setup's mode page stays P8's) and **P5** (the pocket shell
> `user/Apps/pocketshell`: the launcher behind the apps, the task switcher with PocketUI's thumbnails, quick settings
> with the notifications — it serves `notify` —, the system keys; the shell's operations `PK_OP_SHELL`..`PK_OP_GRAB`
> and UIKit's `uk_shell_*`, `uk_shell_tasks` / `uk_shell_grab` added; the menu bar kept as the top band, its Onyx menu
> and its time opening the shell's screens through SystemKit's `shell.h`; quick settings came at P5 rather than P8; the
> status bar of §7.2 is the menu bar, not pocketshell's) and **P6** (the adaptive widgets: `uikit/adapt.h` — the size
> class, the metrics, `Root::onSizeClass`, `uk_scroll_gutter`, input types, the focus ring and arrows —, `SidePanel` with
> `place` / `setSubtitle`, `FormDialog`, `ToolBar`'s priorities and », `TabStrip`'s presentations, `DataGrid`'s roles
> and cards, `TreeView`'s drill-down, the menus' / dialogs' / scroll bars' renderings; PocketUI's viewport; 99 entries
> appended to `uikit.abi`, both UIKits the same; pilots: the Control Panel — its links-and-applet layout also the
> desktop's, the user's decision —, the File Viewer made resizable, the Task Manager, the Terminal's tabs; pocketshell's
> Settings open the Control Panel; docs/03 §5.10.6) are built; P1–P4 first tried on the Pi (the user's reports), P5 and P6 not yet. With P5, the
> user's rule after the Pi's first run: every app's main window full screen (cards: an app's other windows and the
> apps of `SD:/etc/pocketui.ini`), the keys following the front app. `docs/HANDOFF.md` says what was done and the Pi's
> checklists; the text below is the study as it was written.

*Status (2026-10-08): **a technical analysis only, nothing built.** No code changed. It works out the
user's decisions of 2026-10-08 on the design study `docs/COMPACT-SHELL-STUDY.md` (whose §11.1–11.2, "one
Elegant with a policy per mode", they supersede): **PocketUI**, a graphics server of its own beside
Elegant, serving **pocket** and **console**; **one UIKit per graphics server**, with the same exports;
the server's UIKit loaded under the **alias `SD:/lib/uikit.so`** through a new kernel call
`kapi_lib_open_as`; **switching mode = closing the graphical session and starting it again**, from the
Control Panel and from Setup; **one binary per app, all modes** — an app's sources may (and should)
adopt the new adaptive widgets, never a mode-specific build (§1). Every statement about today's code was checked in the tree (files and
lines cited); the rest is a proposal. **Revised the same day after the user's answers (§13)**: the
kernel changes K1–K3 accepted; **the window API moves out of AppKit into UIKit** (`uk_win_*`, every
program migrated, AppKit's window calls removed — a deliberate ABI break); Elegant may be reorganised
with its behaviour unchanged; the sessions, the Mode applet and Setup's choice accepted; the other
recommendations taken by default. Netbook and pad are later modes: the architecture keeps them open,
they are not detailed.*

## 0. In short

- **The alias works as decided**, with one refinement of its lifetime (§3.4): the image cache keys
  libraries by their canonical path (`kernel/proc/image.cpp` `FindNamed`, line 221); an alias is a second
  key on the same `TImage`, looked up first, set only by the process that holds the graphics server's
  role, never for `appkit.so`. Libraries are placed once in the arena (`LibPlace`, line 356): the
  server and the apps map the aliased UIKit **at the same address**, one copy of its code.
- **The window API moves into UIKit (the user's decision, §2.2, §4).** Today a program's window calls
  are AppKit's `kapi_*` functions speaking `elegant.h` (`user/Kits/appkit/appkit_ws.inc`), and twelve
  games, Doom, the shell programs, `rdpd`... use them without UIKit's `Root`. The user chose to reverse
  the HANDOFF's decision of 2026-10-05 ("the protocol is AppKit's"): the window API becomes **UIKit's,
  `uk_win_*`** (C linkage, `uikit/win.h`); **every program is migrated** to it (≈ 55 source files, §4.2);
  **AppKit's ≈ 48 window functions are removed** (`appkit.h`, `appkit_calls.inc`, `appkit_ws.inc`,
  `appkit.abi` — the append-only rule waived for this change: every program rebuilt and republished
  together). AppKit keeps only the kernel's transport (`kapi_ws_ctl`: `KAPI_WS_CALL`, `KICK`, `ACTIVE`)
  and the kernel's full-screen primitives, which only UIKit calls. **Each server's UIKit is then the only
  code that speaks its server's protocol**: the desktop UIKit's port holds `elegant.h` and today's
  client code (with the restart replay); the pocket UIKit's port speaks PocketUI's own protocol, free to
  differ (it starts from Elegant's numbering where meanings match, to reuse Elegant's sources, §4.3).
- **The adaptive widgets** (§6.5–6.15): the app declares *what* — the menu bar's data (unchanged), a
  navigation or inspector `SidePanel`, tools with priorities, tabs, dialogs (`Form`), input types, column
  roles — and each UIKit renders it per mode (desktop as today; pocket landscape / portrait; console).
  Most of it is library code that adapts every app at once (dialogs, inputs, lists, scroll bars,
  `TabStrip`); the rest is a migration of the apps' home-made side panels, toolbars, tabs and tables
  (about 32 days for the apps the user listed and those found, §6.15). Unmigrated apps keep the
  automatic fallbacks (fill, card, viewport, scale).
- **Two UIKits from one source** (§5): the same headers (they *are* the ABI: `uikit/abi.h`), the same
  objects compiled once, one *port* object per server; the pocket table generated against the same
  `uikit.abi` with `libgen --frozen`; an automatic test that the two tables are identical slot by slot.
- **PocketUI** (§7): Elegant's server-neutral sources (the kernel plumbing, shared buffers, shots,
  restart) extracted into `user/Servers/common/` — allowed, Elegant's behaviour unchanged — and compiled
  into both servers; PocketUI adds its policy and its protocol; the shell — status bar,
  launcher, switcher, quick settings, the console home — are **client programs**, not the server.
  **Console is the same server** in another mode, with its own home program.
- **Three small kernel changes, all accepted** (§3.8): `lib_open_as`, the graphics server chosen
  from `system.ini` `shell=` with a fallback to Elegant (today `SD:bin/elegant` is hard-coded,
  `kernel/sys/wsrv.cpp` lines 105 and 742), and a "switch the server" operation of `ws_ctl` (no table
  entry). All three in kapi v97.
- **The plan** (§9): phases P0–P10 (P11 later), about **97–104 session-days**, 10 of them moving the
  window API into UIKit and migrating every program (P2), 32 the adaptive widgets' migration (P7,
  spreadable); the first visible milestone (every card app full screen under a bare PocketUI, the switch
  from the Control Panel) at the end of P4, about 24 days in.

## 1. The frame

| # | Decided by the user (2026-10-08) | What this study adds |
|---|---|---|
| 1 | PocketUI, a graphics server of its own beside Elegant (`user/Servers/pocketui/`); Elegant unchanged; **one binary per app for all modes** (clarified: the apps' *sources* may adopt the new system; a migrated app still runs on the desktop's UIKit — the same `.abi`) | console = the same server, `--mode console` (§7.5); Elegant's behaviour unchanged, its sources reorganised into `user/Servers/common/` (§7.1, answer 3); unmigrated apps covered by fallbacks (§6.3) |
| 2 | One UIKit per server, same exports / same `.abi`; UIKit is to its server what AppKit is to the kernel — and (answer, 2026-10-08) **the window protocol moves to UIKit**: `UIKit_Win_*`, every app migrated, no `kapi_win*` left, ABI compatibility not required for this change | spelt `uk_win_*` (§4.2); each UIKit's port is the only code speaking its server's protocol; AppKit keeps the kernel's transport only (§2.2, §4, §5) |
| 3 | The server loads `SD:/lib/pocket/uikit.so` with `kapi_lib_open_as (path, alias, min_version, err)`, keyed under `SD:/lib/uikit.so`; a new call, `appkit.abi` append-only, no app rebuilt | the kernel's side in detail (§3); slot 234, kapi v97 |
| 4 | The server keeps its own reference; no persistent flag; `preload.ini` for persistence | the alias is *anchored* on the server: kept across a crash-and-relaunch of the same server, dropped when another server is started (§3.4: a windowless daemon such as `printd` would otherwise keep the pocket UIKit as `uikit.so` after a switch back to the desktop) |
| 5 | Switching = close the graphical session and start it again | a `session` tool and per-mode session files (§8.2–8.3); the kernel restarts the right server (§3.8, K3) |
| 6 | The widgets adapt per mode — the menu bar, context menus, dialogs, `SidePanel` (left navigation / right inspector), toolbars, tabs, inputs, lists and tables, scroll bars; the apps with home-made ones migrate | the family, its API and `.abi` additions, the renderings, the migration list (§6.5–6.15) |
| 7 | Console: apps full screen, their menu bar revealed on demand (Select / Home, Alt / F10, the top edge), merged with the system's items; Home opens a game's quick menu; **no on-screen keyboard in console** | the shell's top overlay (§6.6, §7.5); the keyboard pocket only (§6.11, §7.4) |

## 2. Where things stand (checked in the code)

### 2.1 How a program reaches Elegant

```
 app ──► UIKit (Root, menus, dialogs; uikit.so)          ──┐
 app ──► AppKit's kapi_* window calls (appkit.so) ◄────────┘   (every program: UIKit or not)
          │ appkit_ws.inc: ws_raw () → KT->ws_ctl (KAPI_WS_CALL, struct kapi_ws_call)
          ▼
 kernel: kernel/sys/wsrv.cpp — the role, the request queue (pid stamped), the raw input ring,
          the shared buffers (KAPI_WS_BUF_*), each program's event queue, the full screen
          ▼
 Elegant (SD:/bin/elegant): server.cpp (the loop) → ops.cpp (el_op: one case per EL_OP_*)
          → core.cpp (core.h: the window manager behind plain functions) → wm/window.cpp
          (CWindowManager: z-order, frames' hit tests, drag, desks, menus' specs, composition)
```

Four paths, none of them UIKit's (`user/Kits/appkit/elegant.h`, its head comment):

| Path | How |
|---|---|
| Rare requests (create, move, resize, menu, lists, tray, drag...) | `KAPI_WS_CALL`: an operation `EL_OP_*` (low 16 bits; since v94 the window number in the high bits), four numbers, up to `KAPI_WS_DATA_MAX` = 4096 bytes each way; the caller sleeps until the server answers |
| Pixels | memory shared with the server: the canvas at `EL_VA_CANVAS`, the frame's two copies at `EL_VA_FRAME` (the frame is **drawn by the app**: `uikit/skin.cpp` `uk_decorate_window` → `kapi_get_chrome`), other windows at `EL_VA_WIN (w, p)`; a read-out copy for `rdpd` (`EL_OP_SHOT`) |
| Present | `KAPI_WS_KICK`: no message, the server is woken |
| Events | the server posts into the program's queue *in the kernel* (`KAPI_WS_POST`); the pump (`kapi_pump_wait`, `kapi_should_exit`) is unchanged |
| Full screen | the kernel's (`kapi_fullscreen_begin` / `_direct` / `_end`): the server is only told (`KAPI_WS_IN_FULLSCREEN`) and routes all input to that program |

AppKit also keeps what each program asked (windows, handlers, menu, tray) and asks it again of a server
that does not know the program (`ws_replay`, `appkit_ws.inc` lines 86–128): this is how the programs
survive an Elegant restart — and it works for **any** server that answers the same operations.

### 2.2 The protocol is AppKit's today — the user moves it to UIKit

The HANDOFF records as a decision of 2026-10-05: "**The protocol is AppKit's** (private to it, shared
with Elegant; no program includes it): its `kapi_*` window names cannot move, UIKit depends on AppKit,
`vncd` / `rdpd` / `plasma` / `gpcdemo` have no UIKit". The code agrees: about 48 AppKit functions speak
to the server (`appkit_calls.inc`'s `KAPI_WS` bodies and `appkit_ws.inc`), and the programs of §4.2
call them directly.

**That decision is reversed (the user, 2026-10-08)**: "for windows we'll have UIKit_Win_*; the
applications will be migrated, and no `kapi_win_` left in the kapi at all". So:

- the window API is **UIKit's** (`uk_win_*`, §4.2), and **each server's UIKit is the only code that
  speaks its server's protocol** — the desktop UIKit speaks `elegant.h`, the pocket UIKit speaks
  PocketUI's; nothing else in the system knows either protocol;
- **AppKit loses its window functions** and keeps what reaches the kernel (§4.4): the transport
  `kapi_ws_ctl` (already an AppKit call, used by Elegant), the full-screen primitives, the event pump,
  the screen's size, the injection — still the only code that reads the kernel's table (the kits rule);
- **every program that has a window maps `uikit.so`** — most already do (every app of `user/Makefile`
  links `uikit.imp.a`); the few that did not (the C programs `rdpd`, `gpcdemo`, Doom's layer, `el0test`,
  `wstest`, and the C++ ones that never used a UIKit symbol) gain one library (§4.5);
- this is a **deliberate ABI break**: `appkit.abi` loses lines (the rule "append-only by name" waived by
  the user for this change), every windowed program is rebuilt and published with the new AppKit and
  UIKit at once (packages needing `kapi >= 97`).
### 2.3 What lives where today

| Feature | Where | Notes |
|---|---|---|
| Window list, z-order, focus, desks, drag of frames, composition, cursor, wallpaper | Elegant (`wm/window.cpp`, `core.cpp`) | |
| Window frames' pixels | **UIKit** in the app (`skin.cpp`), into buffers Elegant allocates; `EL_OP_FRAME` gives the insets (0: borderless) | a server can make every window frameless without UIKit |
| Menus | spec published by UIKit (`menu.cpp` → `kapi_set_menu` → `EL_OP_MENU_SET`); drawn by **the menu bar program** (`apps/menubar`: `kapi_get_menu`, `kapi_menu_command`) | |
| Tray icons | programs → `EL_OP_TRAY_SET` (v95); the menu bar reads `TRAY_LIST/ICON` | |
| Dock, running marks | `apps/dock` (`kapi_win_list`, `kapi_list_windows`, `kapi_desk`), its layout from SystemKit `dockconf.h` | polls |
| Notifications | SystemKit `notify.h` → IPC service `notify` → `notifyd` (topmost borderless windows placed with `kapi_screen_size` / `kapi_move_window`) | |
| Clipboard | kernel's (`kapi_clipboard_*`) + `clipd` (history) | server-agnostic |
| Drag and drop | `EL_OP_DRAG_BEGIN/DATA` (payload ≤ 4044 bytes) | |
| Control Panel applets | app-to-app: a surface shared with the host + mailboxes (`uikit/root.cpp` lines 36–122, `systemkit/applet_proto.h`) | server-agnostic |
| Print Screen, wheel speed | Elegant (`server.cpp` `print_screen`, `wheel_speed`) | |
| The desktop's programs | `SD:/etc/autostart`, run once by `init` (`user/BinUtils/init.c`): `run voronoy`, `run setup`, `#setup: run menubar`, `run notifyd`, ..., `#setup: run dock`, `#setup: run agenda`, `#setup: run stickies`, services, `preload /boot` | system services and desktop programs mixed in one file |

No other kit talks to the server: SystemKit's calls are files, IPC, mailboxes, the clipboard, sound,
`kapi_launch` (checked: no `kapi_win*`, `kapi_wallpaper*`, `kapi_menu*`, `kapi_tray*` in
`user/Kits/systemkit`); FontKit, ImageKit, FileKit, NetKit, AudioKit, GPIOKit neither. PrinterKit
opens `uikit` by name (`printerkit.cpp` line 47): it follows the alias by itself.

### 2.4 Who starts Elegant

The kernel, at every boot, **before init** (`kernel.cpp` line 1941 → `WsBootStart`, `wsrv.cpp` line
744): `ExecPath ("SD:bin/elegant", "--serve")`, then up to 5 s for it to take the display (else the
debug console). The role is given to **the program named `elegant`** (`Register`, line 228: the task's
basename compared with `"elegant"`). When it ends, `WsPoll` (line 93) starts `SD:bin/elegant --serve
--restart` again, 5 times at most. `init` then runs `SD:/etc/autostart`. The kernel already reads
`SD:/etc/system.ini` at boot (`ReadSystemConfig`, `kernel.cpp` line 1626: verbose, timezone, ntp,
hostname, ramfs).

### 2.5 The library cache today

- `kapi_lib_open` (`kernel/sys/kapi.cpp` line 293): a bare name becomes `SD:/lib/<name>.so`, then
  `ImageCanonPath` (lower case, volume first: `sd:/lib/uikit.so`), then `LibraryOpen` (`kernel.cpp`
  line 249) → `ProgramImage` → `ImageOpen` (`image.cpp` line 545) → `FindNamed (Key)` (line 221: the
  named images whose `Path` equals the key). Found: shared, the card untouched; else streamed from the file.
- Version: `ImageLibInfo` → the export table's first `u32` (its number of entries) ≥ `min_version`,
  else `-ENOTSUP` (`LibraryOpen`).
- Placement: **once, system-wide**, at load (`LibPlace`: the first free range of the arena
  `USER_LIB_BASE` 16 GB .. `USER_LIB_END` 28 GB); `ImageMapLib` (line 683) maps it at `o->ulBase` in every
  address space, one reference per space (`AS_LIB_MAX` = 16 libraries a process).
- `ImageFileChanged` (line 713, the file layer's hook): the image of a written, removed or renamed
  path loses its name (`Unname`): no new process maps it; the running ones keep it.
- Preload (`ProgramPreload`, `kernel.cpp` line 239) pins an image; `ImageUnload` (line 703) unnames;
  `ImageList` (line 748) and `Describe` (line 737) give `struct kapi_image_info` (280 bytes, a fixed
  stride: no room for a second path).
- AppKit itself is an image found the same way (`AppKitLoad`, line 302, `APPKIT_PATH` =
  `SD:/lib/appkit.so`): **an alias on `appkit.so` would hijack every program** — it must be refused.

## 3. The alias, on the kernel's side

### 3.1 The data

Three fields in `TImage` (`image.cpp` line 74):

```
char     Alias[IMG_PATH_MAX];   // a second key, canonical ("": none) -- only for a library
unsigned nAliasOwner;           // the pid that set it (the graphics server)
boolean  bAliasOrphan;          // its owner ended; kept for a relaunch of the same server
```

`FindNamed (Key)` looks at the aliases **first**, then at the paths: while an alias is set, a named
image whose real path is `sd:/lib/uikit.so` (the desktop UIKit, loaded earlier or preloaded) is
not returned for that key. Nothing else in the cache changes: the aliased image *is* the pocket
UIKit's image, placed once, its `Path` its real file's.

### 3.2 The call

`const void *kapi_lib_open_as (const char *path, const char *alias, unsigned min_version, int *err)` —
`path` and `alias` in `kapi_lib_open`'s forms (a bare name or a path), canonicalised. The library at
`path` is opened and mapped into the caller exactly as `LibraryOpen` does (same version check, on the
**real** library), then the alias is set on its image. Returns its export table (the caller's
mapping), or 0 with `*err`:

| Error | When |
|---|---|
| `-EPERM` | the caller does not hold the graphics server's role (`wsrv.cpp`'s `s_nServerPid`; a new `WsIsServer ()` in `kern/wsrv.h`); or the alias is `sd:/lib/appkit.so`; or the alias or the real path is not under `sd:/lib/` |
| `-EBUSY` | the alias is set by another **live** process, or by the caller on another image (one alias per key) |
| `-EINVAL` | `path` is not a library, or `path` = `alias` |
| `-ENOENT`, `-ENOTSUP`, `-ENOMEM`, `-EMFILE`, `-ENAMETOOLONG`, `-EFAULT` | as `lib_open` |

Called again by the owner with the same pair: the same table, nothing changed (idempotent). The order
in the server's start matters: **`KAPI_WS_REGISTER` → `kapi_lib_open_as` → `KAPI_WS_DISPLAY`**:
`WsBootStart` returns when the display is taken and only then does `init` start programs, so every
program of the session finds the alias.

### 3.3 Who may alias, without heavy machinery

The role is already the kernel's notion of "the graphics server" (one pid, `Register`). Restricting
`lib_open_as` to it costs one comparison and needs no new privilege system. With K2 (§3.8) the role
goes to the program the kernel started for the mode (`elegant` or `pocketui`), so a stray program
cannot take it while the server lives (`Register` returns 0 when a live process holds it). The
remaining hole — any program may kill the server (nothing is privileged on Onyx today, HANDOFF) — is
not widened by the alias.

### 3.4 Lifetime

The user's rule — the alias lives while the server or apps hold the image, no persistent flag — is
kept, anchored on the server:

| Event | The alias |
|---|---|
| The image's references reach zero | gone (with the image, or with its pin's last mapping) |
| Its owner ends and the kernel starts **the same server** again (a crash: `WsPoll`) | kept, *orphaned*: programs started meanwhile still get the pocket UIKit; the new server's `lib_open_as` with the same real path takes it over (owner = its pid) |
| The kernel starts **another** server (a mode switch, K3; or the fallback to Elegant, K2) | dropped at once: the image keeps its real name, the processes mapping it keep it |
| A different real path asked by a new server while the alias is orphaned | replaced |

Why not "while referenced" alone: after a switch from pocket to desktop, a windowless daemon of the
system part of the autostart that maps UIKit (`printd`, through PrinterKit) would keep the pocket image
referenced, and every desktop app started afterwards would get the pocket UIKit. With the rule above it
cannot happen. A daemon of the old session that opens a window later would hold the other server's
UIKit — and, since each UIKit speaks only its own server's protocol (§2.2), it would not be understood:
the port's first request is a *hello* carrying its protocol's name (Elegant's `EL_OP_CREATE` already
fails cleanly on an unknown operation, `EL_E_BADOP`; PocketUI answers the same), and on a mismatch the
port logs "this program's UIKit is the other server's: restart it" and its window calls fail. `session`
therefore also restarts the windowless services that map UIKit (`printd`) at a switch (§8.3).

### 3.5 Files, versions, preload, the lists

- **A file change** (`ImageFileChanged`): compared with the image's **real path** only. An update of
  `SD:/lib/uikit.so` (the desktop's) does not touch the aliased image. An update of
  `SD:/lib/pocket/uikit.so` unnames the real path but **keeps the alias** on the old image: the session
  stays coherent (every app on one UIKit) and the new file is taken at the next session — as AppKit's
  update takes a restart today. `pkg` says "takes effect when the session restarts".
- **Versions**: checked on the real library (`LibraryOpen`); an app built against UIKit N asking
  `min_version` N gets `-ENOTSUP` only if the pocket table is older — which the build forbids (§5.3:
  one package carries both libraries, built together).
- **Preload**: `preload uikit` while aliased pins the aliased image through its real path (it is that
  image `ImageList` returns for the alias key); persistence across sessions = `preload.ini` naming
  `SD:/lib/pocket/uikit.so`. `unload` of an alias key: refused (`-EBUSY`, "ended with its server").
- **`image_list`**: `Describe` adds a flag **`KAPI_IMG_ALIAS` (16)**; `kapi_image_list ("SD:/lib/uikit.so")`
  returns the info of the image the key resolves to, whose `path` is the real one — `/bin/preload`
  prints `sd:/lib/uikit.so -> sd:/lib/pocket/uikit.so` without any change of `struct kapi_image_info`.
- The log line of `LibraryOpen` names the real path and, for an alias, `(as sd:/lib/uikit.so)`.

### 3.6 Before the server, without a server

Programs start after `WsBootStart`; telnet and `init` come later. If PocketUI fails to take the display
in 5 s, the kernel starts Elegant (K2): the alias was never set, or is dropped (§3.4) — the desktop's
UIKit, the desktop. A program that a user starts by hand during a server's relaunch gets the orphaned
alias: coherent with the session.

### 3.7 Same address in the server and in the apps

Yes: the address is the image's (`LibPlace` at load), not the process's; the server's `ImageMapLib`
and every app's map the same frames at the same `ulBase`. The alias adds no copy. The desktop and the
pocket UIKit may both be in memory (two ranges of the arena: about 2 MB each), never mixed in one
process (an app maps one image under `sd:/lib/uikit.so`; `FindLib` is by image).

### 3.8 The kernel changes and the kapi additions

| # | Change | Where | Table |
|---|---|---|---|
| **K1** | `lib_open_as` + the alias in the cache (§3.1–3.5) | `kern/image.h` (`ImageAlias`, `ImageUnalias (owner, bOrphan)`), `proc/image.cpp`, `kernel.cpp` (`LibraryOpenAs`), `sys/kapi.cpp`, `kern/wsrv.h` (`WsIsServer`) | **slot 234**, `KAPI_CHECK_SLOT (lib_open_as, 234)`, `KAPI_IMG_ALIAS` |
| **K2** | The graphics server chosen at boot from `system.ini` **`shell=`**: `pocket` / `console` → `SD:bin/pocketui --serve --mode <m>`, anything else → `SD:bin/elegant --serve` (Elegant's arguments unchanged); a server that is missing or does not take the display in 5 s → Elegant (a bad setting cannot leave the screen dark); `WsPoll` relaunches the server *that was started*; `Register` accepts the basename of the started server's path (not only `elegant`) | `wsrv.cpp` (`WS_SERVER_PATH` becomes a variable; `WsBootStart`, `WsPoll`, `Register`), `kernel.cpp` `ReadSystemConfig` (one key) | none |
| **K3** | **`KAPI_WS_SWITCH`**, a sub-operation of `ws_ctl`: the current server asked to end (`KAPI_WS_EXIT` on it, killed after 3 s), its aliases dropped, the relaunch counter reset, `shell=` read again, the matching server started (with K2's fallback) → 0 / `-EBUSY` (a full-screen program holds the display) | `wsrv.cpp`, `kapi_abi.h` (`KAPI_WS_SWITCH`) | none (a `ws_ctl` operation) |

All three in **kapi v97** (`KAPI_ABI_VERSION` 96 → 97, its history line in `kapi_abi.h`). AppKit:
`appkit.h` declares `kapi_lib_open_as`; `appkit_calls.inc` gets
`KAPI_CALL (const void *, kapi_lib_open_as, (...), { if (KT->version >= 97 && KT->lib_open_as) return ...; *err = -KAPI_ENOSYS; return 0; })`;
`appkit.abi` gains `kapi_lib_open_as` — and **loses the ≈ 48 window functions** that move to UIKit
(§4.2; the user waived the append-only rule for this change: every program rebuilt; the version 97 is
also what makes every package of the rebuilt programs wait for the new AppKit). `docs/02`: §7 *Shared
libraries* (the alias paragraph), §8's table (a `### v97: lib_open_as, the graphics server per mode`
block: slot 234's row, `ws_ctl`'s row amended for `KAPI_WS_SWITCH` and `REGISTER`'s new rule), §10 (the
server per mode). `docs/03` §5.6 and §5.10. `docs/10-APPKIT.md` regenerated (`tools/docgen/kitdocs.py`).
K2 and K3 were **accepted by the user on 2026-10-08** with K1 (§13). Until K3 is written, a switch is a
restart of the Pi.

### 3.9 Tests of the kernel's side

`tools/tests/image/imagetest.cpp` (the host test of the cache) gains: an alias found before a real
image of the same key; refused on `appkit.so` and outside `sd:/lib/`; `-EBUSY` for a second owner;
idempotent call; a file change of the alias path does not unname, of the real path keeps the alias;
the orphan kept, taken over, replaced; dropped by `ImageUnalias`; `Describe`'s flag; the version check
on the real table. On the Pi: P1's `aliastest` (BinUtils) run as the server under a `--serve`-less test
mode of PocketUI, and the log lines.

## 4. The window API in UIKit — the inventory and the migration

### 4.1 The kits

| Kit | Talks to the server? | After the change |
|---|---|---|
| AppKit | **yes today** (the window calls) | **no**: keeps the kernel's transport only (§4.4) |
| UIKit | through AppKit today | **the only one**, through its port (§5.2): `uk_win_*` |
| SystemKit | no (notify: IPC `notify`; clipboard: kernel + `clipd`; applets: surfaces + mailboxes; wallpaper: files + `kapi_launch`; autostart, locale, dock layout: files) | unchanged; gains `session.h` (§8.2) and generalised `autostart_*` (§8.2); the launcher's catalogue comes from its `dockconf.h` |
| PrinterKit | opens `uikit` by name | follows the alias |
| FontKit, ImageKit, FileKit, NetKit, AudioKit, GPIOKit | no | unchanged |

### 4.2 `uk_win_*`, and every program migrated

**The naming**: the user writes `UIKit_Win_*`; UIKit's C-level functions are spelt `uk_*` (`uk_messagebox`,
`uk_lang_init`, and `uk_win_select` already exists in `root.cpp` line 158), so the API is **`uk_win_*`**,
`extern "C"`, declared in **`uikit/win.h`** (C-compatible, included by `uikit/uikit.h`; a C program
includes it alone). The rule: `kapi_create_window` → `uk_win_create`, every other window call
`kapi_<name>` → `uk_win_<name>` without a doubled `win_`:

| Today (AppKit) | Becomes (UIKit) |
|---|---|
| `kapi_create_window`, `_ex`, `kapi_win_new`, `kapi_win_select`, `kapi_win_destroy` | `uk_win_create`, `uk_win_create_ex`, `uk_win_new`, `uk_win_select`, `uk_win_destroy` |
| `kapi_move_window`, `kapi_resize_window`, `kapi_resize_window2`, `kapi_win_resizable`, `kapi_set_window_alpha`, `kapi_win_geometry`, `kapi_get_chrome` | `uk_win_move`, `uk_win_resize`, `uk_win_resize2`, `uk_win_resizable`, `uk_win_alpha`, `uk_win_geometry`, `uk_win_chrome` |
| `kapi_present`, `kapi_draw_text`, `kapi_set_key_handler`, `_click_`, `_pointer_handler`, `kapi_set_cursor`, `kapi_cursor_pos`, `kapi_cursor_shown` | `uk_win_present`, `uk_win_draw_text`, `uk_win_on_key`, `uk_win_on_click`, `uk_win_on_pointer`, `uk_win_cursor`, `uk_win_cursor_pos`, `uk_win_cursor_shown` |
| `kapi_set_menu`, `kapi_get_menu`, `kapi_menu_command` | `uk_win_menu_set`, `uk_win_menu_get`, `uk_win_menu_command` |
| `kapi_win_list`, `kapi_win_raise`, `kapi_win_close`, `kapi_win_minimise`, `kapi_win_move`, `kapi_win_desk`, `kapi_desk`, `kapi_win_read`, `kapi_list_windows`, `kapi_raise_app`, `kapi_toggle_app` | `uk_win_list`, `uk_win_raise`, `uk_win_close`, `uk_win_minimise`, `uk_win_place`, `uk_win_to_desk`, `uk_win_desk`, `uk_win_read`, `uk_win_apps`, `uk_win_app_raise`, `uk_win_app_toggle` |
| `kapi_wallpaper_buffer`, `_commit`, `_generate`; `kapi_drag_begin`, `kapi_drag_data`; `kapi_tray_set`, `_clear`, `_list`, `_icon`, `_activate`; `kapi_get_wheel_speed`, `kapi_set_wheel_speed` | `uk_win_wallpaper_buffer`, ...; `uk_win_drag_begin`, `uk_win_drag_data`; `uk_win_tray_set`, ...; `uk_win_wheel_get`, `uk_win_wheel_set` |
| `kapi_fullscreen_begin` / `_end` (the server told, then the kernel's call) | `uk_win_fullscreen_begin` / `_end` (the port tells its server, then calls AppKit's kernel primitive); `kapi_present_fb`, `kapi_fullscreen_direct`, `kapi_gpu_frame` stay AppKit's (kernel) |
| — (new) | `uk_win_server` (the server's name, mode, logical screen, work area, scale, size class), the shell's calls `uk_shell_*` (§4.3) |

The structures (`kapi_win_info`, `kapi_win_geom`, `kapi_chrome`, `kapi_tray_info`) and the `WIN_FLAG_*`,
`GUI_EVENT_*`, `KAPI_CURSOR_*` constants keep their definitions (the events still arrive through the
kernel's pump); they move to `uikit/win.h` where they are only the server's business.

**The programs to migrate** (found by grep over `user/`; most of the work is mechanical renaming):

| Group | Programs | Calls today | Under PocketUI |
|---|---|---|---|
| **UIKit itself** | `root.cpp`, `menu.cpp`, `skin.cpp`, `canvas.h` (a comment) | windows, menus, chrome, tray, drag, desks | the port (§5.2) |
| **A. Own window, no `Root`** | 2048, cppdemo, eyes, inidemo, life, mandelbrot, minesweeper, pong, same, snake, sokoban, tetris (C++), `gpcdemo` (C) | `create_window`, `present`, `cursor_pos` | a fixed window: a centred card over a dim (§7.3) |
| **B. Full screen** | the six emulators, Doom (`doom_onyx.c`, C), BASIC's runtime (`Libs/basic/runtime.cpp`), plasma, lock, Slides' show, PDF, Photos' share, Media, Screenshot | `fullscreen_begin/_end`, `resize_window` (+ `present_fb`, `gpu_frame`, kernel) | the kernel's path; the server told by the port; console's overlay limit (§7.5) |
| **C. The desktop's shell** | menubar, dock, agenda, stickies, wifimenu, voronoy | `create_window_ex` (bands), `menu_get`, `menu_command`, `apps`, `win_list`, `desk`, `tray_*`, `wallpaper_*`, `move` | desktop session only (§8.2) |
| **D. Services with windows** | notifyd, the Clock (`raise_app` in 4 files), Notes / stickies | `create_window_ex` topmost, `move`, `alpha`, `app_raise` | `notifyd` desktop only; `pocketshell` serves `notify` (§7.2) |
| **E. UIKit apps managing windows** | Telegram, Control Panel, Jet (`console.cpp`, `downloads.cpp`, `main.cpp`'s `kapi_get_chrome`), File Viewer, Archiver, Media, imageview, taskman | `win_select`, `win_list`, `win_raise`, `drag_begin`, `geometry`, `wallpaper_*`, `chrome` | the pocket port's meanings (§4.3) |
| **F. Remote, tests** | `rdpd` (C: `win_list`, `win_read`, `win_move`, `win_close`, `cursor_shown`), `el0test` (C), `wstest` (C, the protocol by hand: moves into a UIKit test), the PC simulators (`tools/tests/desktop_sim/fakekapi.cpp`) | | `vncd` uses only kernel calls (`screen_grab`, `inject_*`, `screen_set`): **unchanged** |

C programs link UIKit's C binding (`lib/uikit.imp_c.a`, `libgen --bind-c`, new for UIKit: the C entry
points only, with the program's allocator).

### 4.3 PocketUI's protocol, designed freely

Since only the pocket UIKit speaks it, PocketUI's protocol owes nothing to `elegant.h`. Recommended
nevertheless: **start from Elegant's operation numbers and structures wherever the meaning is the
same** (create, frame, handlers, resize, menus, lists, tray, wallpaper, drag, shots...) — then
PocketUI reuses Elegant's request decoding and window store from `user/Servers/common/` (§7.1), and the
pocket port reuses the desktop port's client code (the replay included) — and **add its own operations
in a range of their own** (`user/Servers/pocketui/pocket.h`, private to the pocket UIKit and PocketUI).
What it then leaves out or changes is free: no desks, no window moves, no minimise (the switcher), a
work area and size classes instead of free geometry. The meanings PocketUI gives (they were the
"compatibility layer" of the first draft; they are now simply its policy, the pocket port asking
accordingly):

| Operation(s) | Elegant | PocketUI (pocket, console) |
|---|---|---|
| create | placed at x, y or by Elegant; frame by flags | **normal**: the work area, **frameless**; **fixed** or small and not resizable: a centred card, framed by UIKit's `skin.cpp`; **topmost borderless** popups: kept where asked, clamped; **backmost**: the shell's only |
| frame | the insets | 0 0 for a filled window |
| move, place | moves | ignored for a filled window; honoured for a card and a popup |
| resize, grow, resizable, geometry | as asked | a resizable app given the work area; too large → the **viewport** (§6.3); geometry = the work area |
| menus | stored; `menubar` reads | the same; the status bar / console overlay reads (§6.6) |
| lists, raise, close, destroy | | the same (other windows are switcher cards) |
| minimise | minimises | sends the app behind (the launcher shows) |
| desks | desks | none |
| alpha, cursor, pointer, wheel, drag, wallpaper, read, shot, tray | | the same |

Its private operations (the shell's and the pocket UIKit's; the shell calls them through UIKit's
**`uk_shell_*`** functions — entries of both UIKits, the desktop one answering "not supported" where
Elegant has no equivalent):

| `PK_OP_*` | From | What |
|---|---|---|
| `SHELL` | pocketshell / consolehome | "I am the shell" (accepted from the program PocketUI started); its windows become the status bar band, the launcher (backmost), the overlays |
| `EVENTS` | shell | window opened / closed / title or menu changed / front changed, pushed as `GUI_EVENT_*` to its handler (no polling, E1) |
| `KEYS` | shell | the system keys registered (Super, Alt+Tab, Super+arrows, F10 when no app takes it, a lone Alt, the pad's Home / Select) delivered to the shell first (E2) |
| `THUMB` | shell | a window's picture scaled to w × h into the caller's transfer area — the switcher's cards |
| `SPLIT`, `FRONT` | shell | split view, a window to the front / the back |
| `CLASS` | UIKit | the window's size class and metrics profile; pushed again on rotation (E4, U5) |
| `TEXT_HINT` | UIKit | a text field took / lost the focus, its input type (E5/U10: the on-screen keyboard, pocket only) |
| `SHEET` | UIKit | this window is a dialog shown as a sheet (U9) |
| `DIM`, `OVERLAY` | shell | the dim behind an overlay, the console's quick menu |
| `PANEL` | UIKit | the window has a navigation drawer / an inspector: the status bar shows ☰ or its button (§6.8) |
| `TOOLS` | UIKit | the toolbar's actions with their labels, for console's **Tools** section (§6.9) |
| `FOCUS_RECT` | UIKit | the focused widget's rectangle: the viewport and the on-screen keyboard keep it visible |

### 4.4 What AppKit keeps

| AppKit call | Used by | Why it stays |
|---|---|---|
| `kapi_ws_ctl (KAPI_WS_CALL, ...)` | the UIKit port | a request to the server (the kernel stamps the pid, the caller waits) |
| `kapi_ws_ctl (KAPI_WS_KICK, win)` | the port's `uk_win_present` | "my pixels changed" |
| `kapi_ws_ctl (KAPI_WS_ACTIVE)` | the port | is there a server (the 5 s wait at a restart) |
| `kapi_ws_ctl (...)` server operations | Elegant, PocketUI | unchanged |
| `kapi_fullscreen_begin/_end/_direct`, `kapi_present_fb`, `kapi_gpu_frame` | the port (begin / end), the programs (the rest) | kernel primitives; `kapi_fullscreen_begin` no longer tells the server itself (the port does first) |
| `kapi_pump_*`, `kapi_should_exit`, `kapi_post`, `kapi_screen_size`, `kapi_screen_set`, `kapi_screen_grab`, `kapi_inject_*`, `kapi_key_held`, `kapi_pad_state`, `kapi_font_width/height`, `kapi_draw_text_buf` | everyone | kernel calls, not the server's |
| `kapi_lib_open_as` (new, K1) | the servers | §3 |

No new transport call is needed: `kapi_ws_ctl` is already an AppKit function (Elegant uses it). A
program could still call it directly — it is not a secret, merely not an API: `appkit.h` marks it as
the servers' and UIKit's.

### 4.5 The cost of mapping UIKit

| Item | Figure | Note |
|---|---|---|
| UIKit's code | 300 682 bytes (`sdcard/lib/uikit.so`, its R+X segment) | **shared**, in memory once whatever the number of programs |
| UIKit's private data | 16 KB of file data, 60 KB with its bss (the RW segment) → **one 64 KB page** a process | the only per-process cost; the window state of `appkit_ws.inc` moves with it (a few KB) |
| Libraries a process | + 1 for the programs that had no UIKit (`AS_LIB_MAX` = 16) | the C programs and a few C++ ones; every app already maps it |
| Static constructors | `uikit_lib_init` runs UIKit's (`onyx_lib_init`) once a process | ImageKit and SystemKit are opened lazily by UIKit (`imgload.cpp` line 32, `sysclip.cpp` line 20), FontKit not at all: no chain of libraries |
| The programs' own size | AppKit's stubs for the window calls go, UIKit's (one per used entry) come | a wash |

About 64 KB a windowed program that did not map UIKit before — a handful of programs.

## 5. Two UIKits from one source

### 5.1 What ties a program to UIKit

`user/Kits/uikit/abi.h`: (1) the export table, append-only (`uikit.abi`, 798 entries); (2) the
classes' layout — programs allocate, embed, derive widgets and read their fields; locked by
`layout_lock.cpp` (`tools/libgen/layout.py`); (3) the virtual functions' order (reserved slots
`uk_reserved0..7`, `uk_rootReserved*`); plus what is **inline in the headers**, compiled into the
programs (e.g. `uk_fh ()`, `uk_fw ()`, `UK_SBW = 10` in `widget.h`), and the global variables, which are
the **program's** (`globals.inc`: the palette, the text face, handed to the library by `--data`).

So the two UIKits **must share every header byte for byte**; they may differ only in the `.cpp` files
— and only behind functions that are entries of the table or internal to the library.

### 5.2 Common code and the port

A **port** — a set of internal functions in `namespace uikit::port` (`uikit/port.h`, internal, not
included by programs) — takes what differs between the servers; everything else stays common:

| Common (unchanged sources) | Port (`port_desktop.cpp` / `port_pocket.cpp`) |
|---|---|
| every widget's logic, the canvas, text, layout panels, lists, grids, text editing, the applets' protocol, `lang`, icons, the theme's palette; the `uk_win_*` and `uk_shell_*` entry points (thin: they call the port) | **the window driver**: the server's protocol (`elegant.h` moved here from `user/Kits/appkit/`, private to the desktop port; `pocket.h` for the pocket port), the transport through `kapi_ws_ctl`, the per-program window state and the **replay** after a server's restart (today `appkit_ws.inc` lines 26–128); `window_open` (flags, placement wishes), `frame` (decorate or not: the skin's call), `present`, `menu_publish`, `dialog_kind` (window, in-window modal, **sheet**), `metrics ()` (the profile: row, menu row, button heights, paddings, scroll-bar style, hit slop), `focus_policy` (ring always shown, arrows move the focus), `input_filter` (touch: long press → right click, drag → scroll), `text_hint`, `size_class ()`, `context_menu_style`, the adaptive widgets' renderings (§6.5) |

The desktop port is today's code moved verbatim — `appkit_ws.inc` and the `KAPI_WS` bodies of
`appkit_calls.inc` become its window driver, UIKit's current drawing paths its renderings —
behaviour-neutral: the desktop's screenshots must come out identical, Elegant's restart must still bring
every window back (`kill` of Elegant during `pi_apps.py`). The pocket port is new. Widgets ask `port::metrics ()` where they use constants today
— in the `.cpp` only (an inline constant of a header cannot change: `UK_SBW` keeps its 10 px of layout,
the pocket port draws an overlay bar inside it).

### 5.3 The build

```
lib/uikit/*.o                 the common objects, compiled ONCE (UIKIT_SO_OBJ, user/Makefile line 134)
lib/uikit/port_desktop.o      → lib/uikit.so          (+ uikit_table.S, stubs, bind, uikit.imp.a, uikit.imp_c.a)
lib/uikit/port_pocket.o       → lib/pocket/uikit.so   (+ lib/pocket/uikit_table.S only)
```

- `libgen --name uikit --abi Kits/uikit/uikit.abi --vtables --data onyx_uikit_data --exclude '^_ZN5uikit4port'`
  for **both**, plus **`--bind-c lib/uikit.imp_c.a`** for the C programs (the `uk_win_*` entries); the
  pocket one with **`--frozen`** (a new function is an error: it must enter the
  common `.abi` through the desktop build first) and without `--stubs/--bind` (the programs link the
  desktop's `uikit.imp.a`: the stubs are by slot, the same for both). `libgen` already fails when an
  entry of the `.abi` is not defined (`libgen.py` line 166–169), so a function missing from the
  pocket port is caught.
- `layout_lock.cpp` compiled into both (it is a common object).
- **One package** `uikit` carries `lib/uikit.so` and `lib/pocket/uikit.so` (`tools/pkg/packages.ini`
  `[uikit] files =`): they cannot drift apart on a card.

### 5.4 The automatic identity test

`tools/libgen/abi_same.py lib/uikit.so lib/pocket/uikit.so` (run by `make libs` after both are linked,
and by `tools/tests/shlib/`): read each library's export table from the ELF (its version = entries,
then, per slot, the address → the symbol by the symbol table) and require **the same version and the
same symbol at every slot**; compare the two generated `uikit_table.S` (`.quad` lines, comments
stripped); check that the data imports (`--data`) list is the same. A second test, on the model of
`tools/tests/shlib/compat.sh`: an app built against the desktop's import library, run against the
pocket library under the name `uikitc` (no alias needed), calls a sample of entries and derives a
widget — PASS on the PC's host build and on the Pi.

### 5.5 On the PC

`tools/tests/desktop_sim/run.sh` compiles `user/Kits/uikit/*.cpp` for the host: the port files move to
`user/Kits/uikit/port/` and the script picks `port/port_${UK_PORT:-desktop}.cpp` — `UK_PORT=pocket`
gives the pocket look in the simulator; `shots.sh` gains a pocket compose (the work area, the status
bar) for the docs' screenshots.

## 6. Resolution independence and the adaptive widgets

### 6.1 What exists

- **Colours** only in the theme (`uikit/theme.h`, `SD:/etc/theme.txt`): no metrics.
- **Text**: the installed face (`uk_face_`, program global) — each app chooses its size in pixels
  (`ft_uikit_install ("DejaVu Sans", 13)`, an inline of `fontkit/uikitface.h`); `uk_fh ()` follows it.
- **Geometry**: in pixels, given by the apps (`langconf`: `Label (310, ct + k * 20, 360, 20, ...)` in a
  700 × 470 window); `layout.h`'s stack and grid panels, anchors (`ANCHOR_FILL`), `Root::setResizable`
  — used by **31 of the 115 folders of `user/Apps`** (the rest fixed); one app is `WIN_FLAG_FIXED`
  (Setup, 800 × 600).
- **Icons**: `icon.bmp` 40 × 40.
- **The screen**: `kapi_screen_set` 640 × 480 .. 2560 × 1600, even widths (`SCREEN_MIN_W/H`,
  `kernel.cpp` line 663): no portrait size under 640 px wide.

### 6.2 What is added, and where

| Need (design study §9) | Where | How |
|---|---|---|
| U1 metrics profile | pocket UIKit (port) | `regular`, `compact`, `touch` from `theme.txt` `metrics=`; read by the common widgets through `port::metrics ()` |
| U2 scale | PocketUI (+ opt-in UIKit) | §6.3: integer composition for every app; true scale only for the apps that are known to draw only through UIKit |
| U3 icons @2x | ImageKit / UIKit `icon_load` | `icon@2x.bmp` (80 × 80) chosen when the scale is 2 or the tile is large; else the 40 × 40 scaled |
| U4 frameless | PocketUI | `EL_OP_FRAME` 0 0 (no UIKit change at all) |
| U5 size classes | pocket UIKit + PocketUI + the apps | `PK_OP_CLASS`; the adaptive widgets (`SidePanel`, `ToolBar`, `TabStrip`, `Form`, `DataGrid`'s roles: §6.5–6.15) render per class; an app's own layout through `uk_size_class ()` / `Root::onSizeClass ()` |
| U6 scrolling root | **PocketUI** | the viewport (§6.3): for every app, UIKit or not |
| U7 focus everywhere | common code + pocket port's policy | arrows move the focus between focusable widgets in reading order; ring always drawn |
| U8 touch | kernel (E6) + PocketUI + pocket port | a "touch" flag on pointer events; long press, drag-to-scroll in the port's input filter |
| U9 sheets | pocket port | message boxes, file dialogs, the colour picker sized to the work area |
| U10 text hint | pocket port → `PK_OP_TEXT_HINT` | |
| U11 menus unchanged | — | `set_menu` as today |

### 6.3 The fallbacks, for the apps not yet migrated

In order of preference, decided by PocketUI per window:

1. **Fill**: a resizable app (`RESIZABLE` on, its minimum fits) is resized to the work area — it lays
   itself out (anchors, panels).
2. **Card**: a fixed or small window is shown at its size, centred (games, the Calculator, dialogs).
3. **Viewport**: a window larger than the work area that will not shrink keeps its canvas; PocketUI
   shows the part that fits and scrolls it (wheel, two-finger drag, the d-pad at the edge, a thin
   indicator; the focused widget's rectangle kept visible when UIKit says where it is — a private
   `PK_OP_FOCUS_RECT`). No copy: the compositor reads a sub-rectangle of the shared canvas; pointer
   coordinates are offset. Works for UIKit and non-UIKit programs alike.
4. **Scale**: at a device scale of 2, every window is composed **2 × nearest** (crisp pixels) and the
   pointer divided; at 1.5, the choice of the user: 1 × (small) or 1.5 × bilinear (soft). Per-window, so a
   **native** window (canvas in device pixels) sits beside scaled ones.
5. **Native scale (opt-in)**: an app known to draw only through UIKit's widgets and `Canvas` methods is
   given a canvas in device pixels; the pocket UIKit draws at the scale, the widgets' coordinates stay
   logical (`Canvas::ext` carries the scale, `abi.h` rule 2). It is a property PocketUI reads from
   **`SD:/etc/pocket/apps.ini`** (`[ledger] scale = native`), set after testing an app — no file of the
   app touched — or by the app itself, `uk_logical_units (true)` (§6.14). Apps that write `canvas.px`
   themselves (Paint's canvas, the games) stay scaled.

`kapi_screen_size` keeps the device's size (vncd needs it with `screen_grab`); apps that place windows
with it (notifyd, wifimenu) are clamped by PocketUI's policy.

### 6.4 Orientation

Portrait is not a mode: PocketUI computes the logical width / height / aspect and pushes the size class
(`PK_OP_CLASS`) and a resize to the filled windows; the shell reflows by the design study's §6.1 rules.
The kernel side (E7/E8) — portrait sizes below 640 px wide in `kapi_screen_set`, the firmware's display
rotation, the touch coordinates rotated — is P10.

### 6.5 The adaptive widgets: the app says what, each UIKit says how

The fallbacks of §6.3 make every app *usable*; they do not make it *good*. The better result comes from
apps that **declare the semantics** of their parts — "this is a navigation list", "these are the
inspector's pages", "these are tools in order of importance", "these are tabs" — and let **each UIKit
render them per mode**. Since the user's clarification (one binary per app, sources may change, §1),
this is the recommended path for every app that is worked on; the family:

| Widget | What the app declares | Desktop UIKit | Pocket UIKit, landscape | Pocket UIKit, portrait / narrow | Pocket UIKit, console |
|---|---|---|---|---|---|
| **The menu bar** (exists, §6.6) | the app's menus as data (`set_menu`), unchanged | Elegant's `menubar` program | `pocketshell`'s status bar: drop-downs | `pocketshell`: ☰, a bottom sheet (menus as tabs, a submenu pushes a level with ←) | `consolehome`'s top overlay, revealed on demand, merged with the system's row (§7.5) |
| **Context menus, combos** (exist, §6.6) | `PopupMenu`, `Dropdown`, `ComboBox` | at the pointer | at the touch point, touch-size rows; long press = right click | a bottom action sheet | a centred list; △ opens a context menu |
| **Dialogs** (exist; §6.7) | `uk_messagebox`, `uk_file_*`, `uk_color_dialog`, `print_dialog`, `Modal` subclasses; new `Form` | a modal box centred on the window | a sheet over the dimmed app (full height if tall, buttons pinned at the bottom, content scrolls); the file dialog full screen with the places as a `SidePanel` rail | a full-screen sheet with a title bar (Cancel left, OK right); a bottom sheet for a short confirmation | a centred PS2-styled panel; ✕ confirms, ○ cancels, focus on the default button, the d-pad between fields |
| **SidePanel**, navigation (new, §6.6) | sections, items, headings, badges | today's left sidebar, width resizable in an `HSplitter` | an **icon rail** (48 lp) that expands over the content on hover / tap / focus | hidden; a **drawer** opened by ☰ (in the status bar) or a swipe from the edge | a **d-pad column** of big rows; **L1 / R1** step through the sections from anywhere |
| **SidePanel**, inspector (new, §6.6) | pages of properties, rich rows (layers) | today's right panel | hidden; a **toggle button** slides it **over the content from the right** | a **bottom sheet** (half height, dragged to full) | a panel opened from the revealed menu's row or **R2** where the pad has one |
| **ToolBar** (exists; adaptive, §6.9) | tools with a priority (*always*, *if room*, *overflow*), groups, toggles, drop-down tools | as today (rows, labels, tooltips) | **one row** by priority, the rest in **»**, touch-size targets | a short **bottom bar** of the 4–5 first tools + » | no toolbar: its actions in the revealed menu's **Tools** section; the first ones on shoulder / face buttons, shown as hints |
| **Tabs** (`TabStrip` exists; extended, §6.10) | tabs: title, mark, data, close, new | as today | a compact strip that **scrolls**, an overflow **⋯** list when they do not fit | **the current tab's title ▾** opening the list (close and new inside it); a segmented control when ≤ 3 short tabs | **L1 / R1** switch tabs; a PS2-style **header row** (the current tab big, its neighbours dim); close from the revealed menu |
| **Input widgets** (exist; type hints new, §6.11) | a field's type (text, number, URL, e-mail, password, terminal), ranges | as today | touch metrics (taller fields, bigger boxes and knobs); on focus of a text field the **on-screen keyboard** of its type, the content scrolled so the field stays visible; spin boxes as steppers; text selection handles | the same; combos as a list sheet; date and colour pickers as full sheets | a focus glow on every input; the d-pad between fields; ✕ edits / toggles; ← / → change sliders, spins, combos in place; text expects the physical keyboard (a hint if none); **no on-screen keyboard** |
| **Lists and tables** (exist; roles new, §6.12) | columns with a **priority** and a role (*primary*, *secondary*, *detail*) | as today (headers, resizable columns, Ctrl / Shift multi-select) | taller rows, low-priority columns hidden when narrow (a column chooser), kinetic scroll, long press = context menu or multi-select with check boxes | a table as **two-line cards** (primary = title, 1–2 secondary = subtitle, the rest in the detail view); a tree as a **drill-down** list with ←; icon views reflow | big focusable rows with a glow; d-pad moves, L2 / R2 page; ✕ opens, △ context menu, ○ back; sorting from the revealed menu |
| **Scrollbars** (exist; §6.13) | nothing new (a gutter query for the apps' layouts) | permanent bars, arrows, track click, drag | thin **overlay** bars (no layout room) shown while scrolling, fading after ~1 s; a grabbable fast-scroll thumb on long lists (a letter bubble when sorted); the content dragged with inertia; an edge glow | the same | no bar to grab: a slim position indicator; the content **follows the focus**; L2 / R2 page; the right stick scrolls when there is one |
| **Size class / metrics** (new, §6.14) | `onSizeClass ()`: the app's own layout per class | always *regular* | *compact* | *narrow* | *console* |

The desktop's column is the **desktop UIKit** and is the look of today: a migrated app looks the same on
the desktop (the user's requirement), and **the same binary** runs on both UIKits — the new classes and
calls are entries of the one `uikit.abi`, implemented by both libraries (§5), the difference being in
the port's rendering only.

### 6.6 The menu bar adapts, not the menus

Today the menus are **data**: UIKit's `Menu` publishes the spec (`menu.cpp` → `kapi_set_menu` →
`EL_OP_MENU_SET`); **the `menubar` program** (`user/Apps/menubar/main.cpp`: "the ACTIVE app's name and its
menus (`kapi_get_menu`...), opens a drop-down on click and sends the chosen command back
(`kapi_menu_command`)") draws them, with the "Onyx" system menu first. So **the menu bar is the shell's
component, one per mode**, and nothing changes in the apps or in UIKit's `Menu`:

| Mode | The menu bar | Where it lives |
|---|---|---|
| desktop | today's `menubar` program on Elegant | `apps/menubar` (unchanged) |
| pocket | the **status bar**: the front app's menus as drop-downs (landscape), ☰ and a bottom sheet (portrait); shortcuts shown in landscape, hidden in portrait | `pocketshell` (a client, §7.2) |
| console | the **top overlay** revealed on demand (Select / Home, Alt or F10, the pointer held at the top edge ~300 ms), console-styled, the app's menus above the system's row (Home, Switch app, Quit), submenus as a horizontal step, shortcuts hidden; for games and emulators Home opens the quick menu instead and the edge is off | `consolehome` (a client) |

**The menus' data model does not change**: an app publishes its spec (`uk_win_menu_set`, called by
UIKit's `Menu`), the shell reads the front app's (`uk_win_menu_get`) and sends the command back
(`uk_win_menu_command`) — exactly what `menubar` does today, renamed. Each UIKit carries these three
calls in its own server's protocol: the desktop port as Elegant's `MENU_SET/GET/COMMAND`, the pocket port
as PocketUI's operations of the same numbers (§4.3), served from the shared request code (§7.1). The one addition: PocketUI tells the
shell when the front app's menu changes (`PK_OP_EVENTS`, instead of polling the serial). **Context menus
and combos** stay UIKit's in-window popups (`PopupMenu`, `dialog.h` line 101; `Dropdown`, `ComboBox`),
rendered by the pocket port: touch-size rows at the touch point, a bottom action sheet in portrait, a
centred list in console (△ opens the context menu of the focused widget).

### 6.7 Dialogs

**Today** a dialog is an in-window widget: `Modal` (`uikit/dialog.h` line 20) is added as the top child
of the `Root`, flagged modal, run by a nested loop; `MessageBox`, `FileDialog`, `ColorDialog`,
`PopupMenu` derive from it; PrinterKit's `print_dialog` is built the same way. **37 apps** call
`uk_messagebox` / `uk_file_*`; **99 custom dialog classes** (`public Modal` / `public Dialog`) in 29
files lay out their controls **at absolute coordinates** inside a fixed box — Letters 17 (its own
`Dialog` base, `letters/dialogs.h` line 62), the Spreadsheet 15, Slides 8, Paint 6, Archiver 6, Courier
6, Media 3, Calendar 3, Cardfile 3, QBasic 3, and 1–2 in 3DForge, Ledger, Mail, Photos, PDF, QBStudio,
Screenshot, Telegram, Turtle Quest, fmtracker, FreeCell, Jet, File Viewer.

- **UIKit's own dialogs** are library code: the pocket port gives each its renderings (§6.5's table)
  with no app change — the message box, the file dialog (full screen, its places on a `SidePanel` rail),
  the colour dialog, the popup menus; PrinterKit's `print_dialog` follows (it is built from UIKit's
  widgets: a sheet in pocket, a panel in console).
- **Custom dialogs, unmigrated**: `Modal::run ()` is library code, so the port decides how the box is
  presented without touching the dialog: at its size over the dimmed app when it fits (landscape); as a
  **scrolling sheet** when it does not (the box's subtree offset and scrolled inside the window, the
  focused control kept visible); console: the same, PS2-framed, with **✕ = Enter, ○ = Esc** and the
  d-pad moving the focus in Tab order (U7) — the default button focused first when the dialog marks it
  (`Button::isDefault`, a reserved field).
- **Migrated custom dialogs** use a new **`Form`** (a `Modal` that lays out rows): `addRow (label,
  widget)`, `addSection (title)`, `addButton (label, role)` with roles *default*, *cancel*,
  *destructive*, *other*; the port places the rows (two columns on the desktop as today's dialogs look,
  one column in portrait), pins the buttons (bottom-right on the desktop, title bar in a portrait sheet,
  ✕ / ○ in console). Migrating **Letters' `Dialog` base** and the Spreadsheet's `ui_base.h` first moves
  32 dialogs at once to a common shape.

### 6.8 SidePanel

**Today**: home-made side panels in 19 apps (§6.15), each a `Widget` of its own (`media/main.cpp`
line 373 `Sidebar`, `mail/main.cpp` line 137, `ledger/main.cpp` line 76 `SideBar`, `pdf/main.cpp`
line 549 `SidePanel`, `slides/sidebar.h` line 171, `courier/rail.h`, `paint` `LayersPanel` at the
right...): UIKit cannot fold what it does not know.

**The API** (`uikit/sidepanel.h`, namespace `uikit`; structured items first, free content as the
fallback):

```cpp
enum { UK_SP_LEFT = 0, UK_SP_RIGHT = 1 };                 // the side
enum { UK_SP_NAVIGATION = 0, UK_SP_INSPECTOR = 1 };       // the role
enum { UK_SP_FULL, UK_SP_RAIL, UK_SP_DRAWER, UK_SP_SLIDEOVER, UK_SP_SHEET,
       UK_SP_COLUMN, UK_SP_HIDDEN };                      // how it is shown now (the port decides)
enum { UK_SPI_HEADING = 1, UK_SPI_FOLDABLE = 2, UK_SPI_DISABLED = 4, UK_SPI_SEPARATOR = 8 };

class SidePanel : public Widget
{
public:
    SidePanel (int l, int t, int w, int h, int side = UK_SP_LEFT, int role = UK_SP_NAVIGATION);

    // ---- structured items (what the rail, the drawer, the console column can render) ----
    int  addHeading (const char *label, unsigned flags = 0);          // -> its id (UK_SPI_FOLDABLE: folds)
    int  addItem (int id, const char *label, const Icon *icon = 0, int level = 0, int under = -1);
    void setLabel (int id, const char *label);
    void setBadge (int id, int count);                // 0: none; > 0 a pill ("3"); -1 a dot
    void setTrailing (int id, const char *text);      // a short value at the right ("80 %", "12")
    void setThumb (int id, const unsigned *px, int w, int h);    // a picture (a layer, a slide, a page)
    void setToggle (int id, int on, const Icon *on_icon = 0, const Icon *off_icon = 0);  // -1: none (a layer's eye)
    void setFlags (int id, unsigned flags);
    void moveItem (int id, int before_id);  void remove (int id);  void clear ();
    void select (int id, bool fire = false);  int selected () const;
    void (*onSelect) (SidePanel &, int id);
    void (*onToggle) (SidePanel &, int id, int on);
    void (*onItemMenu) (SidePanel &, int id, int x, int y);   // right click, long press, △
    bool (*onDrop) (SidePanel &, int id, int type, const char *data, int len);   // a file dropped on a place
    void (*onReorder) (SidePanel &, int id, int before_id);   // dragged (layers, playlists); 0: not reorderable

    // ---- free content (the fallback: it only collapses to a drawer / a sheet) ----
    int  addPage (const char *label, const Icon *icon, Widget *content);   // the inspector's tabs
    void setContent (Widget *w);         // a navigation panel's content below its items (a tree, a calendar)
    void setFooter (Widget *w);          // under the list: the selected item's controls, buttons

    // ---- geometry, state ----
    void setWidths (int min_w, int pref_w, int max_w);        // desktop: the HSplitter's range
    int  presentation () const;          // UK_SP_*
    int  reservedWidth () const;         // what the app's layout must leave for it now (rail: 48 lp; overlay: 0)
    void (*onPresentation) (SidePanel &, int presentation);  // the app lays itself out again
    void open (bool on);  bool isOpen () const;               // a drawer, a slide-over, a sheet
    ToolButton *toggleButton ();         // the inspector's button, for the app's ToolBar (a no-op on the desktop)

    void *ext = 0;  unsigned long sp_reserved_[6] = {};       // (abi.h rule 2)
    virtual void sp_reserved0 () {}  virtual void sp_reserved1 () {}   // (rule 3: room for later)
    // the overrides: onDraw, onMouse, onKey, layout (Widget's virtuals: no new slot)
};
```

- **Rich rows**: items carry a small fixed set of accessories — picture, toggle, badge, trailing text,
  indent — enough for Paint's layers (thumbnail, eye, name, "80 %"), a mailbox's unread count, Ledger's
  badges, File Viewer's volumes. Anything richer (an opacity slider, a blend mode) goes in the
  **footer**, which shows the selected item's controls: the rail and the console column stay renderable
  because a row never holds an arbitrary widget. An inspector made of forms (QBStudio's properties,
  3DForge's parameters, Slides' four tabs) uses **pages** — free content: on the desktop they are tabs at
  the panel's top, in pocket a slide-over / a sheet with a segmented header, in console L1 / R1 pages
  inside the opened panel.
- **Free content in a navigation panel** (Calendar's month, Archiver's folder tree) cannot become a
  rail: such a panel goes straight from *full* to a *drawer*.
- **Where the presentation is decided**: the port (`port::sidepanel_presentation (side, role, has_items)`)
  from the mode, the size class and the panel's room; the desktop port always says `UK_SP_FULL`.
  `reservedWidth ()` lets the app's own resize code place its content (rail: 48 lp at the left; a
  drawer, a slide-over: 0 — the content takes the width). The ☰ of a navigation drawer and the button of
  an inspector are published to the shell (`PK_OP_PANEL`: the status bar shows ☰, Esc / ○ closes) so an
  app needs no button of its own; `toggleButton ()` is there for an app that wants one in its toolbar.
- **Keys**: arrows inside, Enter selects; in the rail a focused icon shows its label; F6 moves the focus
  between the panel and the content (the desktop too); console: the column is a focus area of its own,
  L1 / R1 step the sections without moving the focus.
- **In the `.abi`**: a new class — its constructor, methods and overrides are new functions that the
  desktop build appends to `uikit.abi` (`libgen`), and the pocket build (`--frozen`) must define them all
  (`abi_same.py`); its size and field offsets are added to the layout lock (`make uikit-layout-update`,
  once, before its first release: then frozen like every class). The callbacks are fields (as `TabStrip`'s
  `Action`s), the item storage behind `ext` (the library's), so items can change format freely.
- **Name**: `pdf` has a class `SidePanel` of its own at global scope with `using namespace uikit`: its
  migration removes it (an unqualified use would otherwise be ambiguous once `uikit/uikit.h` includes the
  new header). No other app defines the name (checked).

### 6.9 ToolBar

`uikit::ToolBar` exists (`toolbar.h` line 70: `add`, `addRight`, `sep`, `space`; `ToolButton` with
toggles and split arrows) but the apps with big toolbars have their own: Letters (two rows), Mail
(`mail/app.h`), Photos (`photos/app.h`), PDF (`pdf/main.cpp` line 67), Paint's tools and options, Media's
`TopBar`. Added (methods only; the state behind `Widget::ext`, the class's fields being locked):

```cpp
enum { UK_TB_ALWAYS = 0, UK_TB_IF_ROOM = 1, UK_TB_OVERFLOW = 2 };
void setPriority (Widget *w, int prio, int rank = 0);  // ALWAYS / IF_ROOM (dropped by rank) / OVERFLOW (only in »)
void setLabel (Widget *w, const char *label);          // the row's text in » and in console's Tools section
void setGroup (Widget *w, int group);                  // a group (a separator's run) overflows together, as a submenu
void setShortcut (Widget *w, int pad_button);          // console: the tool on a shoulder / face button (a hint shown)
void foldInto (ToolBar *first);                        // a second row: its tools join first's when compact
int  rows () const;                                    // the rows the port lets it have now (desktop: as built)
```

Per mode, as §6.5's table. What must survive the overflow: a **toggle** becomes a checked row; a
**split** tool a row with a submenu; a **drop-down tool** (Letters' font and size, Paint's colour) is
hosted **as itself** in the » panel (a combo stays a combo, the colour well opens its picker) — the » is
a popup panel of rows, not a plain menu; **groups** (separators) become submenus when more than one
group overflows. Console: the toolbar is hidden (design study §7.3: every tool is also in a menu, or
is given a label for the **Tools** section the shell builds from `PK_OP_TOOLS`, published by the pocket
UIKit). A constraint of `abi.h`: `ToolBar` did not override `layout ()`; giving it one changes its
vtable, and a program carries a *copy* of the vtables it uses (`libgen --vtables`) — an old program keeps
the old behaviour until it is rebuilt: harmless, an app gets the adaptive bar when it is migrated.

### 6.10 Tabs

| Widget | Users | Plan |
|---|---|---|
| `TabStrip` (`tabstrip.h`: titles, marks, data, close, "+", `onChange/onClose/onNew`) | Terminal, the Spreadsheet | **already semantic**: its rendering moves into the port — Terminal and the Spreadsheet adapt **without a source change** (their binaries rebuilt against nothing new: `onDraw` / `onMouse` are library code) |
| `TabHost` (a header drop-down, `addTab (title, content)`) | none | kept; its header follows the same port rules |
| `SegmentedControl` | 14 apps (3DForge, Archiver, Calendar, Clock, Courier, fmtracker, GPIO Lab, Ledger, Paint, pkgman, QBStudio, taskman, Theme, widgets) | a control, not tabs: denser in compact, big in console; an app that uses it *as* tabs (to verify app by app) may move to `TabStrip` |
| Home-made tabs | Courier (`TabBar`, `DocTabs`: `courier/widgets.h` lines 977, 1467), PDF (`TabBar`, `pdf/main.cpp` line 780: its documents), QBStudio (`DocTabs`, `main.cpp` line 164), the Spreadsheet (`SheetTabs`, `sheet/bars.h` line 207: the sheets at the bottom) | migrate to `TabStrip` (a bottom placement flag for the sheets' tabs: `setEdge (UK_TAB_BOTTOM)`) |

Additions to `TabStrip` (methods, state behind `ext`): `setEdge (top | bottom)`, `setStyle (auto | strip
| segmented)` (portrait may turn ≤ 3 short tabs into a segmented control), `setNav (bool)` — this strip
takes the console's L1 / R1 (the first visible strip of the window does by default: `TabStrip`'s
constructor, library code, registers it with its `Root`). Renderings as in §6.5's table; the list of
the ⋯ overflow and of portrait's ▾ shows each tab's mark and a close cross, and "+ New" at its end.

**L1 / R1 in console**, one owner at a time: the window's tabs if it has any, else its navigation
`SidePanel`'s sections; the switcher is in Home's menu ("Switch app") and on Home + L1 / R1 — not on
L1 / R1 alone: the shoulders are the app's (§7.5).

### 6.11 Input widgets

UIKit has them all as library classes: `Textbox` (with `password`), `Textarea`, `RichTextBox`,
`CodeEdit`, `NumericUpDown`, `Slider`, `Checkbox`, `RadioButton`, `ToggleSwitch`, `Combobox`, `Dropdown`,
`Calendar` / `DatePicker`, `ColorPicker`. Their drawing and event code is in the `.cpp`: **the pocket and
console renderings of §6.5 come with no app change** (metrics from the profile, the focus glow, ← / →
in place, ✕ = Enter / Space). What the app may *declare* is new and small: **`uk_set_input_type (Widget *,
int type)`** (`UK_IN_TEXT`, `_NUMBER`, `_DECIMAL`, `_URL`, `_EMAIL`, `_PASSWORD`, `_TERMINAL`, `_SEARCH`;
kept behind `ext`, an entry of the table) — read by the pocket port to choose the on-screen keyboard's
layout (`PK_OP_TEXT_HINT`), by console to say "a keyboard is needed" once; a `Textbox` with `password`
set is `_PASSWORD` without a call. `NumericUpDown`'s range is already its own. The on-screen keyboard is
**pocket only** (the user, 2026-10-08): in console the shell never raises it. When it rises, PocketUI
shortens the window's work area and the pocket UIKit scrolls the focused field into view (the Root's
`PK_OP_FOCUS_RECT`, §6.3).

### 6.12 Lists and tables

UIKit: `ListBox` (15 apps), `DataGrid` (Cardfile, Clock, IRC, Ledger, taskman: columns `{ title, width,
align }`, `datagrid.h` line 38), `TreeView` (IRC, QBStudio). Home-made tables and lists (classes read):
Media's Songs table (its `Content`), File Viewer's columns, Mail's `ListPane`, Photos' `Grid`, Ledger's
journals, Letters' and Cardfile's lists, Courier's, Calendar's agenda, QBStudio's, fmtracker's pattern
grid, Circuits' and the Icon Editor's palettes (the last three are editors, not lists: they stay).

- The renderings of §6.5's table are the ports' for the UIKit classes: taller rows, kinetic scroll,
  long press, the console's rows and keys — no app change.
- New semantics (methods, state behind `ext`; `DataGrid::Column`'s fields are locked):
  **`setColumnRole (c, role, priority)`** (`UK_COL_PRIMARY`, `_SECONDARY`, `_DETAIL`; priority 0 = never
  hidden), `setDetailView (Widget *)` (the portrait card's detail), `setMultiSelect (bool)`; `TreeView` /
  `ListBox` gain `setDrillDown (bool)` (portrait: a level a screen, with ←).
- The home-made tables migrate to `DataGrid` with roles where they are plain tables (Media's Songs, File
  Viewer's details, Mail's message list, Ledger's journals, Cardfile's list); a picture grid (Photos)
  reflows by itself in its own code with `uk_size_class ()`.

### 6.13 Scrollbars

UIKit's `Scrollbar` (`scrollbar.h`) and the scrolling inside its lists, text areas, grids and editors
(`lists.cpp`, `textarea.cpp`, `datagrid.cpp`, `richtextbox.cpp`, `codeedit.cpp`) are library code: the
overlay bars, the fading, the fast-scroll thumb, the drag with inertia, console's indicator and
follow-the-focus are the pocket port's, for every app. The same machinery draws the indicator of
PocketUI's **viewport** fallback (§6.3) — the fallback of unmigrated apps depends on it.

**The catch**: `UK_SBW` (10 px, "reserved right-edge gutter width", `widget.h` line 32) is a `static
const` of a header — **compiled into the programs**. 22 apps subtract it in their own layout (Calendar,
Cardfile, Circuits, Clock, Control Panel, Courier, Critters, File Viewer, IRC, Ledger, Letters, Lisa,
Mail, Media, Notes, Paint, Pinball, QBStudio, the Spreadsheet, Slides, Telegram, Turtle Quest) and three
have their own `SB_W` (fmtracker, PDF, Slides). Under the overlay bars these apps keep an empty 10-px
gutter — harmless, the overlay bar is drawn in it. A migrated app asks **`uk_scroll_gutter ()`** (10 on
the desktop, 0 in pocket and console) instead of the constant, so its content takes the width; nothing
else in an app may depend on the bar's width.

### 6.14 Size classes and metrics for the apps

For an app's own layout code (Media's resize code, Letters' ruler, Ledger's cards): `int uk_size_class ()`
(`UK_SC_REGULAR`, `_COMPACT`, `_NARROW`, `_CONSOLE`), `int uk_mode ()` (desktop / pocket / console),
`const UkMetrics &uk_metrics ()` (the profile's row, button, menu heights, paddings, the scale), and a
virtual **`Root::onSizeClass ()`** — the reserved slot `uk_rootReserved2` renamed (the one renaming
`uikit.abi` allows, `abi.h` rule 3), so older programs answer with the empty default. On the desktop
UIKit they return *regular* / desktop / today's sizes: a migrated app is unchanged there. An app that
draws only through UIKit may also declare `uk_logical_units (true)` — the opt-in of §6.3's native scale,
from the source instead of `apps.ini`.

### 6.15 The apps to migrate, and the others

*(P7's progress — `docs/HANDOFF.md` has the details and the pattern: the side panels of **Media**, **Photos**, **Mail** (and
its one-pane layout for a narrow window) and the **Game Library** (pocket and console; the desktop keeps its own), the
**Calendar**'s (its content: a drawer) done on 2026-10-09, all five translated; the File
Viewer's in pocket and console at P6; Ledger waits for a header slot in `SidePanel`.)*

Side panels found by `grep -E "SIDE_W|PANEL_W|DP_W|class \w*(Sidebar|SideBar|SidePanel|Rail)"` over
`user/Apps`, then read; the user's list (Media, File Viewer, Game Library, Photos, Mail, Courier, Ledger,
PDF, Slides, Paint) is confirmed and completed:

| App | Today | Becomes | Est. (days) |
|---|---|---|---|
| Media | `Sidebar` 208 px (library, playlists) | navigation, items + headings | 1 |
| File Viewer | places 188 px, pinned folders, volumes, Trash | navigation, items + badges, `onDrop` | 1 |
| Game Library | sidebar 220 px, foldable groups (Library, systems, folders) | navigation, foldable headings | 1 |
| Photos | `Sidebar` (library, albums) | navigation | 0.5 |
| Mail | `Sidebar` (accounts, folders, unread) | navigation, headings per account, badges | 1 |
| Courier | `rail.h`: a rail of three places + the place's list | navigation: the places as items, the list as content (drawer) | 1 |
| Ledger | `SideBar` (modules, badges) | navigation (its rail is the design study's case) | 0.5 |
| PDF | `SidePanel` (thumbnails, outline) + home-made `TabBar` of documents | navigation with two pages (free content) + `TabStrip` | 1.5 |
| Slides | `sidebar.h`: the panel **at the right**, four tabs | inspector, four pages | 1 |
| Paint | `LayersPanel` **at the right** | inspector, rich rows (thumb, eye, opacity text), footer (opacity, blend, add / remove), `onReorder` | 1.5 |
| QBStudio | project tree (left), properties at the right (`props.h`), `DocTabs` | navigation (content: the tree) + inspector (pages: properties, events) + `TabStrip` | 2 |
| 3DForge | the right panel's parameters | inspector, free content | 1 |
| Calendar | side 272 px (month, calendars) | navigation, content → drawer | 0.5 |
| IRC | `TreeView` in an `HSplitter` (conversations) | navigation, items + badges | 0.5 |
| Archiver | `FolderTree` | navigation, content → drawer | 0.5 |
| Icon Editor | the tools' panel at the right (170 px) | inspector, free content | 0.5 |
| fmtracker | instruments at the left (156 px) | navigation | 0.5 |
| Telegram | the conversations (`LIST_W` 300, master–detail) and the contact's pane at the right (`DP_W` 150) | the right pane: inspector; the list: its existing one-window-a-conversation mode (`list_only`, v94) in pocket — a `MasterDetail` widget is a later idea | 0.5 |
| Setup | its steps' rail | done with Setup's own work (P8) | — |
| Letters | its own two-row `ToolBar` | `uikit::ToolBar` with priorities, `foldInto`, overflow; the ruler hidden when compact; (a draft view in portrait: later) | 1.5 |
| Courier, PDF, QBStudio, Spreadsheet | home-made tabs | `TabStrip` (counted above for PDF, QBStudio; Courier 0.5, Spreadsheet 0.5) | 1 |
| Mail, Photos, PDF, Paint, Media (`TopBar`) | own toolbars | `uikit::ToolBar` with priorities | 2 |
| Letters (17), Spreadsheet (15) | custom dialogs on their own bases | their bases on `Form` (`letters/dialogs.h` line 62, `sheet/ui_base.h`): 32 dialogs | 3 |
| Slides (8), Paint (6), Archiver (6), Courier (6), the rest (≈ 35 in 19 apps) | custom dialogs | `Form` when the app is worked on; until then the scrolling sheet | 0.25 each (≈ 15, spread) |
| Media, File Viewer, Mail, Ledger, Cardfile | home-made tables | `DataGrid` with column roles | 3.5 |
| the 22 + 3 apps that subtract a scroll-bar width | `UK_SBW`, `SB_W` | `uk_scroll_gutter ()` (a line or two each, done with the app's other migration) | 1 |
| every app with a text field | — | `uk_set_input_type` where the type is not plain text (URLs in Jet and Courier, numbers in Ledger and the Spreadsheet's dialogs, e-mail in Mail) | 1 |

About **32 days** for the side panels, toolbars, tabs, the two dialog bases, the tables and the input
types; the remaining custom dialogs (≈ 15 days) as each app is worked on. The others, until worked on, use the fallbacks (fill, card, viewport, scale):
the Terminal and the Spreadsheet (tabs adapt by themselves), Cardfile (900 × 600 forms: viewport; its
`NavBar` is a record navigator at the foot, not a side panel), Koton (a DAW: viewport — a poor fit,
desktop-first), Jet (760 wide: fill), Tinypad, Notes, the Clock, the Control Panel and its applets
(fill or card), the games and demos (cards; scaled at 2×), the emulators (full screen). P0's pictures at
800 × 480 and 640 × 480 decide which of them deserve a size-class layout next.

## 7. The PocketUI server

### 7.1 Code reuse — recommendation

| Option | For | Against |
|---|---|---|
| a. Copy Elegant into `pocketui/` and diverge | fastest start | two copies of the plumbing (requests, buffers, restart, shots) drift; every fix twice |
| b. **Compile Elegant's sources from `../elegant`**, as Elegant compiles `kernel/gui/gimage.cpp` | one code; Elegant's files untouched | Elegant's internal seams are not made for it (`ops.cpp` reaches `CWindowManager` 30 times through `corepriv.h`) |
| c. Extract the server-neutral part into `user/Servers/common/` (compiled into both) | the clean end state | edits Elegant's files (behaviour-neutral) |

**Decided: c** (the user, 2026-10-08: Elegant's *behaviour* unchanged, its sources may be reorganised).
At P3, extract into **`user/Servers/common/`** what is not policy: the kernel plumbing of `server.cpp`
(the display, the raw input, the request loop, the 16 ms pace), the shared buffers and the shots of
`core.cpp`, the restart state (`KAPI_WS_STATE`, `KAPI_WS_CLIENTS`, `KAPI_WS_BUF_ADOPT`), and the request
decoding of `ops.cpp` over an abstract **window store** interface (create, frame, move, resize, menus,
lists, tray, wallpaper, drag, read, shot) — the operations whose numbers PocketUI shares with Elegant
(§4.3). Elegant keeps `wm/window.cpp` (its overlapping-window policy and compositor) behind that
interface; PocketUI has its own store (`user/Servers/pocketui/wm.cpp`: one front window or two, cards,
the viewport, the per-window scale, the overlays' dim, the bands of the shell) and its own operations.
Elegant relinked is checked unchanged by `pi_wstest.py` (rewritten against the desktop UIKit),
`desktop_sim` and `pi_apps.py`. Options a (a copy) and b (compiling Elegant's files from `../elegant`
untouched) are no longer needed.

### 7.2 What runs where

| In the server (PocketUI) | Client programs (pocket UIKit) |
|---|---|
| placement and z-order (§4.3), the work area, split view, the viewport, the scale | **`pocketshell`** (one program, several windows — v94): the status bar (Onyx button, app name, the front app's menus via `MENU_GET`, tray via `TRAY_LIST`, bell, Wi-Fi, battery, time), the launcher (tabs from SystemKit's `dockconf.h` categories, search), the switcher (cards from `PK_OP_THUMB`), quick settings + notifications (it serves the `notify` IPC service) |
| system keys and the pad's Home routed to the shell (`PK_OP_KEYS`) | **`consolehome`**: console mode's home, library, settings, running, the quick menu of games and the **top overlay of the front app's menus on demand** (§6.6, §7.5) (PS2 style), using Game Library's index and the emulators' save states |
| the dim behind overlays, the cursor, Print Screen, wheel speed | `modeconf` (the Control Panel's applet), `session` (a `/bin` tool) |

Why the shell outside the server: a fault in the launcher (FreeType, icon decoding, the search index)
must not take the display down (the rule Elegant follows with the menu bar and the dock); the server's
single loop must not wait on file I/O; the server stays text-free (Elegant draws no text). The price —
the shell needs the private operations of §4.4 — is small.

### 7.3 Policy (pocket)

One front app (or two in split view) fills the work area; the launcher is the backmost band (the
shell's), the status bar a topmost band; fixed windows are cards over a flat dim; topmost borderless
popups of the apps (menus' drop-downs inside UIKit are in-window; `notifyd`-like toasts) keep their
place; overlays (switcher, quick settings, menus' sheet) are the shell's topmost windows. Workspaces:
none. The switcher's order = the most recently fronted first.

### 7.4 Input

- **Keyboard**: PocketUI filters before `el_core_key`: the registered system keys go to the shell, the
  rest to the front app (as Elegant's `OnKey`, whose Ctrl+Alt+arrows desks, `window.cpp` line 2099,
  PocketUI does not use).
- **Gamepad as a system input**: PocketUI reads the pads itself (`kapi_pad_state` returns the raw state
  to any caller; `focus` follows `KAPI_WS_FOCUS`, set by the server). While an overlay or the console home
  is up, PocketUI sets the focus to the shell: the game's `gamepad.h` sees `focus = 0` and gets no
  buttons (`Include/gamepad.h` line 274). The pad mapped to keys (d-pad → arrows, A → Enter...) only for
  the apps marked `pad = keys` in `SD:/etc/pocket/apps.ini` (default: keys for every category but Games
  and Emulators, which read the pad themselves).
- **Touch** (E6): no touch input in the kernel today (no touch in `kern/wsrv.h`'s input types); Circle
  has drivers for the official display and USB HID touch screens (to check in our fork, `circle/` is a
  nested repository not checked out here). A `KAPI_WS_IN_TOUCH` input type then — P10.
- **On-screen keyboard** (E5), **pocket only**: a shell window that never takes the keys and injects
  them into the front app (in the server: `el_core_key`), shown on `PK_OP_TEXT_HINT` (its layout from the
  field's input type, §6.11) or Super+K. In console there is none (the user): a text field expects the
  physical keyboard, and the shell says so once when none is connected.

### 7.5 Console

The same binary, `--mode console`; the home program is `consolehome`, the UIKit the same pocket UIKit
with the console metrics (large words, the focus glow). A third server would duplicate the compositor
and the protocol for no gain (design study §7.2: "console mode is Pocket with another home and another
style"). What the policy adds (design study §7.3):

- **Apps full screen, no chrome** (no status bar): the work area is the screen.
- **The menu bar on demand**: PocketUI routes to `consolehome` the pad's Select / Home, a lone **Alt**
  (pressed and released with no key between — the modifiers' events of the raw ring) or **F10**, and the
  pointer held at the top edge for ~300 ms; `consolehome` shows its top overlay (§6.6): the front app's
  menus (`MENU_GET`), the toolbar's actions (`PK_OP_TOOLS`), then Home, Switch app, Quit; ✕ chooses, ○
  closes; it hides after a command. A hint "Home or Alt: menu" for ~3 s when an app starts
  (`PK_OP_EVENTS`).
- **Games and emulators** (their `app.txt` category, or `apps.ini`): Home opens the **quick menu**
  (resume, save / load state, screenshot, controls, back to Games) instead, the top edge is off.
- **The shoulders belong to the app**: L1 / R1 to its tabs, else to its navigation sections (§6.10);
  L2 / R2 page in lists; the switcher on Home + L1 / R1 and in the menu ("Switch app").
- **No on-screen keyboard.**

**Limit**: a full-screen game draws through the kernel's direct path; no server can draw over it. v1:
Home over a full-screen game shows the quick menu only when the game leaves full screen (the emulators
are windowed until F11, and in console a windowed app already fills the screen, so the emulators need
not take the kernel's full screen at all); later (**K4**), a `ws_ctl` operation "take the display back
from a full-screen program, give it back after".

### 7.6 Netbook and pad, later

Kept open by: the mode being a string (`shell=`) mapped to a server and a session file; the port layer
(a netbook port or a tablet profile); the private protocol versioned by the server; the size classes.
A netbook mode may as well be Elegant with another session file — nothing here forbids it.

## 8. Modes and the session

### 8.1 The settings

| Key | File | Written by | Read by |
|---|---|---|---|
| `shell = desktop \| pocket \| console` (no line: desktop) | `SD:/etc/system.ini` | `modeconf`, Setup, console's settings | the kernel (K2, at boot and at K3), `session`, SystemKit `session.h` |
| `metrics = regular \| compact \| touch`, `scale = 1 \| 1.5 \| 2` | `SD:/etc/theme.txt` (the look's file) | `modeconf`, Setup | the pocket UIKit, PocketUI |
| per-device keys (soft keys, rotation, battery source) | `SD:/etc/pocket/device.ini` | Setup (proposed from the screen), by hand | PocketUI, pocketshell |
| per-app compatibility (`scale = native`, `pad = keys \| raw`) | `SD:/etc/pocket/apps.ini` | shipped with PocketUI, by hand | PocketUI |

SystemKit gains **`systemkit/session.h`** (on the model of `locale.h`, through its `locale_ini_get/set`):
`session_mode ()`, `session_set_mode (m)`, `session_modes ()` with their names, and
`session_switch (m, flags)` (runs `/bin/session --switch m`).

### 8.2 The boot, with sessions

1. The kernel reads `shell=`, starts the matching server (K2); the server registers, aliases its UIKit
   (pocket), takes the display.
2. `init` runs `SD:/etc/autostart` — **the system part only** (pkg commit, keyb, notify-less services:
   clockd, pkgd, clipd, printd, telnetd, vncd, rdpd, `preload /boot`) and one line **`session`** where the
   desktop's lines were.
3. `/bin/session` reads `shell=` and runs `SD:/etc/session/<mode>` (same syntax as the autostart:
   `run voronoy`, `run setup`, `#setup: run menubar`, `run notifyd`, `#setup: run dock`, agenda,
   stickies for **desktop**; `run pocketshell` for **pocket**; `run consolehome` for **console**), and keeps
   the pids it started (the session's programs).

**Migration** of a card: `pkg commit` of the `onyx` version that brings `session` moves the desktop's
lines from `autostart` into `session/desktop` once (marked done in the file). SystemKit's
**`autostart_has` / `autostart_ensure`** (Notes' "Show Stickies on the Desktop") look in `autostart`
and in the session files, and insert after the anchor in the file that holds it (`run agenda` →
`session/desktop`): Notes unchanged. Setup's `#setup:` lines live in the session file of the mode.

### 8.3 The switch

`session --switch pocket` (called by `modeconf`, Setup, console's settings):

1. **Close the apps**: each program with a window (`kapi_list_windows`) is asked to close
   (`EL_OP_APP_CLOSE` → `RequestExit` → `kapi_should_exit`, `ops.cpp` line 501); an app with an unsaved
   document shows its question (UIKit's dialogs, `Include/docguard.h`); after 5 s the ones still running
   are listed: "Ledger is waiting for an answer" — Wait / Force / Cancel the switch. Cancel = nothing
   changed.
2. **End the session's programs** (the pids `session` started: menubar, dock... or pocketshell). The windowless
   services that map UIKit (`printd`, through PrinterKit) are restarted after the switch, so that they
   hold the new server's UIKit (§3.4).
3. **Write `shell=`** (`session_set_mode`).
4. **`ws_ctl (KAPI_WS_SWITCH)`** (K3): the old server ends, its aliases dropped, the new one started; its
   arrival waited for (`KAPI_WS_ACTIVE`).
5. **Run the new session file.** If the new server failed, K2's fallback gave Elegant: `session` puts
   `shell=desktop` back and runs the desktop's file, with a notification.

Before K3 exists, steps 4–5 are a restart of the Pi (`kapi_reboot`-like path of `/bin/shutdown`).
The services (telnet, vncd, rdpd, clipd, printd) are not touched by a switch.

### 8.4 The Control Panel: a new applet, "Mode"

A new applet rather than a page of Display (`displayconf`: the resolution) or Theme: the mode, the
density and the scale go together and the switch closes the apps — it deserves its own page.
`user/Apps/modeconf/` (MIT), `sdcard/apps/modeconf.app/` (icon, `lang/fr.txt`),
`sdcard/apps/control.app/applets/12-modeconf.lnk` (`name = Mode`, `target = modeconf`, `text = The
interface: desktop, pocket or console; the size of things`). Built as `langconf` is (applet protocol
through UIKit's `Root`, alone a window of its own):

- three cards with a preview (the design study's mock-ups reduced to 160 × 100, BMPs in
  `modeconf.app/res/`): **Desktop**, **Pocket**, **Console** — the current one marked; a line on each;
- for pocket and console: density (regular / compact / touch) and scale (1 / 1.5 / 2), proposed from
  the screen (`kapi_screen_size`, `kapi_screen_native`);
- **Apply**: "The open programs will be closed, then the interface starts again." → `session_switch`.
  The density alone (same mode) is written and applies to the programs started next (as the language).
- Every string in `TR ("...")`, `uk_lang_init ()`, `lang/fr.txt` ("Mode" / "Mode", "Desktop" / "Bureau",
  "Pocket" / "Poche", "Console" / "Console", ...); `python tools/lang/check.py modeconf` at 0 missing;
  `SHOTS_LANG=fr` screenshot.

### 8.5 Setup's welcome page

Page 0 (Welcome: the language, `user/Apps/setup/main.cpp` line 1427) gains the mode: the same three
cards, small, under the language — preselected from the screen (below 1024 × 600 logical: pocket; a
gamepad connected and no keyboard: console). Choosing another mode than the running session's writes
`shell=` and calls `session_switch` — the pattern of the language, for which Setup already starts
itself again (`kapi_exec ("SD:/apps/setup.app/main", g_args)`, line 1267): Setup's `run setup` line is
in the new mode's session file, so it comes back at the same page. Setup's `autostart_finish`
(`system.h` line 183) handles the session file's `#setup:` lines. Its 800 × 600 fixed window shows as a
card or in the viewport under 640 × 480; a reflowing layout for Setup is worth doing when it is touched.
The texts: TR + `setup.app/lang/fr.txt`.

## 9. The plan

| Phase | Deliverable | Files | Test on the PC | Test on the Pi | Est. (session-days) |
|---|---|---|---|---|---|
| **P0** | the decisions (§13); the apps' pictures at 800 × 480 and 640 × 480 under Elegant | `tools/tests/desktop_sim/shots.sh` (a size option) | the pictures, the list of apps that do not fit | — | 0.5 |
| **P1** | K1 + K2 + K3, kapi v97; AppKit `kapi_lib_open_as` | `kern/image.h`, `proc/image.cpp`, `kernel.cpp`, `sys/kapi.cpp`, `sys/kapitable.cpp`, `kern/kapi_abi.h`, `sys/wsrv.cpp`, `kern/wsrv.h`, `appkit.h`, `appkit_calls.inc`, `appkit_ws.inc`, `appkit.abi`, `elegant.h`; docs/02, 03, 10 | `imagetest` (§3.9), the kernel's host stand-ins build | the desktop as before (Elegant, `shell=` absent), `pi_wstest.py`, `el0test`; `shell=pocket` with no PocketUI → Elegant (the fallback) | 3 |
| **P2** | **the window API into UIKit**: `uikit/win.h` (`uk_win_*`, `uk_shell_*`), the **port** split with the desktop port = `appkit_ws.inc` + the `KAPI_WS` bodies moved (and `elegant.h`, the replay), UIKit's C binding (`--bind-c`); **every program migrated** (§4.2: ≈ 55 files — UIKit, groups A–F, Doom, BASIC's runtime, `rdpd`, `el0test`, `wstest`, the PC simulators' stand-ins); **the ≈ 48 window functions removed from AppKit** and `appkit.abi` (the deliberate break); `lib/pocket/uikit.so` built `--frozen` (still the desktop port); `abi_same.py`; every package republished (`kapi >= 97`) | `user/Kits/uikit/` (`win.h`, `port/`), `user/Kits/appkit/` (`appkit.h`, `appkit_calls.inc`, `appkit_ws.inc` gone, `appkit.abi`, `elegant.h` moved), the programs, `user/Makefile`, `user/BinUtils/Makefile`, `Ports/doom`, `tools/libgen/abi_same.py`, `tools/tests/desktop_sim/`, `tools/tests/elegant/`, `tools/pkg/packages.ini`; docs/02, 03, 10, 11 | `desktop_sim` pictures **identical**; `abi_same.py`; the host builds of every app | **`pi_apps.py`: every app starts on Elegant**; `rdpd` with Onyx Remote, `vncd`; Elegant killed → every window back (the replay, now UIKit's); an emulator, Doom full screen | 10 |
| **P3** | **PocketUI skeleton**: `user/Servers/common/` extracted from Elegant (behaviour unchanged, §7.1); PocketUI's store and policy (fill, frameless, cards, no desks), Alt+Tab cycling; its protocol (`pocket.h`: Elegant's numbers where meanings match, §4.3); the pocket port speaking it; the alias | `user/Servers/common/`, `user/Servers/elegant/`, `user/Servers/pocketui/` (`Makefile`, `main.cpp`, `wm.cpp`, `pocket.h`), `user/Kits/uikit/port/port_pocket.cpp`, `user/Makefile` | a host build of both stores over `wmtest`-like checks | Elegant unchanged (`pi_apps.py`, `pi_wstest.py`); under PocketUI **every app starts**; full screen, `rdpd`, `vncd`; PocketUI killed (alias orphaned, taken over, windows back) | 6 |
| **P4** | **sessions**: `/bin/session`, `SD:/etc/session/*`, the autostart's migration, SystemKit `session.h` and `autostart_*`; `modeconf` (switch = restart of the Pi first, then K3) | `user/BinUtils/session.c`, `sdcard/etc/session/`, `user/Kits/systemkit/`, `user/Apps/modeconf/`, `sdcard/apps/modeconf.app`, `.lnk`, `tools/pkg/packages.ini` | `tools/tests/` for the migration (an old autostart → the new files), `check.py modeconf` | desktop → pocket → console → desktop from the Control Panel, an unsaved Tinypad document asked, Cancel | 4 |
| **P5** | **pocketshell** v1: status bar with the front app's menus and the tray, launcher (tabs, grid, search of apps), switcher (`PK_OP_THUMB`), the shell role, system keys, events; serves `notify` | `user/Apps/pocketshell/` (+ `lang/fr.txt`), PocketUI's private operations | `desktop_sim` scenarios (`UK_PORT=pocket`) for the docs' pictures | the navigation map of the design study §6.10 with a keyboard | 7 |
| **P6** | **the adaptive widgets in UIKit** (§6.5–6.14): the pocket and console renderings of what exists (context menus, combos, dialogs and `Modal`'s scrolling sheet, inputs, lists, scroll bars, `TabStrip`), metrics, the focus ring and arrows; the new API, appended to `uikit.abi` once and frozen: `SidePanel`, `ToolBar`'s priorities and overflow, `Form`, `TabStrip`'s additions, `uk_set_input_type`, `DataGrid`'s column roles, drill-down, `uk_size_class` / `Root::onSizeClass`, `uk_scroll_gutter`, `uk_logical_units`; PocketUI's **resize-to-fit** and **viewport** (start of the `Servers/common/` extraction if agreed) | `user/Kits/uikit/` (`sidepanel.h/.cpp`, `form.h/.cpp`, `port/port_pocket.cpp`, the widgets' `.cpp`), `uikit.abi`, `layout_lock.cpp`, PocketUI; `docs/11-UIKIT.md` regenerated | `desktop_sim` pictures of the `widgets` gallery in the three modes (`UK_PORT`); `abi_same.py`; the desktop's pictures unchanged | the desktop unchanged; the gallery on the 7" display, a pad | 14 |
| **P7** | **the apps migrated** (§6.15): side panels (Media, File Viewer, Game Library, Photos, Mail, Courier, Ledger, PDF, Slides, Paint, QBStudio, 3DForge, Calendar, IRC, Archiver, Icon Editor, fmtracker, Telegram's pane), toolbars (Letters, Mail, Photos, PDF, Paint, Media), tabs (Courier, PDF, QBStudio, the Spreadsheet), the dialog bases of Letters and the Spreadsheet, the tables (Media, File Viewer, Mail, Ledger, Cardfile), input types, `uk_scroll_gutter`; each app's docs/04 entry and screenshots; packages | those apps' sources and `lang/fr.txt` | each app's pictures in the three modes (`shots.sh`), `check.py <app>` | each app on the 7" display and with a pad; **the same binary on the desktop** | 32 (+ ≈ 15 for the other custom dialogs, as apps are worked on) |
| **P8** | **Setup**: the mode on the welcome page; quick settings in pocketshell (Wi-Fi, volume, notifications, battery where known); split view | `user/Apps/setup/`, `pocketshell`, PocketUI | `check.py setup`, `SHOTS_LANG=fr` | a first boot that chooses pocket, then console | 4 |
| **P9** | **console**: `consolehome` (home, library from Game Library, save states as slots, settings, running), the top overlay of menus on demand (§7.5), the quick menu, the pad as a system input | `user/Apps/consolehome/`, PocketUI `--mode console`, `SD:/etc/pocket/apps.ini` | pictures (`SHOTS_LANG=fr`) | a handheld with a pad alone: launch a ROM, Home, save state, back | 8 |
| **P10** | **scale and orientation, touch**: per-window 2 × composition, native scale opt-in, `icon@2x`; kernel: portrait sizes, rotation, touch input (E6–E8); the on-screen keyboard (pocket only) | PocketUI, ImageKit/UIKit icons, kernel (`window.h` limits, a touch driver hook, `KAPI_WS_IN_TOUCH`) | the compositor's checks at 2× | the official 7" display (touch), a portrait panel | 8–15 |
| **P11** | (later) **K4** overlay over full screen; netbook / pad modes | | | | — |

Total: about **97–104 session-days** without P11 (the touch and portrait part depends on the hardware the
user picks), of which 32 are the apps' migration (P7) — which can be spread, app by app, and run beside
P8–P10; plus about 15 days for the remaining custom dialogs as their apps are worked on. The first useful milestone is **P4** (every app under PocketUI, the switch from the
Control Panel): about 24 days. Each phase keeps the desktop exactly as it is (`shell=` absent or
`desktop`), so the Pi stays usable throughout; P1, P2 and P6 are the ones that touch what every program uses
(the kernel, UIKit) and are tested hardest. Every migrated app is checked on the desktop's UIKit too
(one binary, all modes).

Risks per phase are in §12.

## 10. Licences

Everything new is ours and under **MIT**: PocketUI (it links no Circle code — like Elegant, it builds
the kernel's `gimage.cpp` with our own stand-ins in `port/`), the pocket UIKit (UIKit's licence),
`pocketshell`, `consolehome`, `modeconf`, `session`, the SystemKit additions, the kernel's changes (the
kernel image stays GPL-3.0 as a whole with Circle). No new third-party library. `docs/LICENSING.md` gains
the two servers' line (Elegant is not listed there today).

## 11. The documentation, when it is built

| Document | What |
|---|---|
| `docs/02-KERNEL-INTERNALS.md` | §7 the alias; §8 the v97 block and rows (slot 234, `ws_ctl` `KAPI_WS_SWITCH`, `REGISTER`'s rule); §10 the server chosen per mode, the fallback; the version history |
| `docs/03-DEVELOPER-GUIDE.md` | §5.6 aliases (who may, lifetime); §5.10 `kapi_lib_open_as` and the window functions gone from AppKit; the window API `uk_win_*` (a
section replacing every `kapi_create_window` example, which are many in docs/03); a UIKit section "one source, two ports" (what may differ, the frozen pocket table, `abi_same.py`); "your app on a small screen" (resizable layouts, size classes, `apps.ini`); the build of `lib/pocket/uikit.so` |
| `docs/04-USER-GUIDE.md` | each migrated app's entry (its panels, toolbar, keys per mode); the three modes, the Mode applet, Setup's choice, pocket's and console's keys and pad buttons, the catalogue entries for `pocketshell`, `consolehome`, `modeconf`, `/bin/session` |
| `docs/06-KITS-GUIDE.md`, `docs/10-APPKIT.md`, `docs/11-UIKIT.md`, `docs/12-SYSTEMKIT.md` | the new calls; in 06 a subject "the adaptive widgets" with an example each (`SidePanel`, `ToolBar` priorities, `Form`, column roles, input types); the reference pages regenerated by `python tools/docgen/kitdocs.py` |
| `docs/HANDOFF.md` | a section: where PocketUI stands, how to test, what is next |
| `docs/COMPACT-SHELL-STUDY.md` | §11's note points here (done with this study) |
| `docs/LICENSING.md` | Elegant and PocketUI |
| screenshots | `shots.sh` scenarios for pocket and console; then `python docs/build_docs.py` |

## 12. Risks

| Risk | Weight | Mitigation |
|---|---|---|
| The two UIKits drift (a function added to one only, an inline changed) | high | one source, objects compiled once, `--frozen`, `abi_same.py` in `make libs`, one package |
| The port split changes the desktop's look or behaviour | high | the desktop port is today's code moved; `desktop_sim` pictures compared pixel for pixel; `pi_apps.py` |
| The apps at 800 × 480 (most designed for ~1000 × 700) | high | fill / card / viewport from P3 on (every app usable, if not pretty); then size classes for the composites |
| `ops.cpp`'s coupling with `CWindowManager` makes the extraction of `Servers/common/` delicate | medium | an abstract window store; Elegant's tests (`pi_wstest.py`, `desktop_sim`, `pi_apps.py`, the restart) before and after |
| The window API's move breaks every windowed program at once (≈ 55 files, the AppKit ABI break) | high | one phase (P2), mechanical renames reviewed, the host builds and `pi_apps.py` before publishing; all packages published together with `kapi >= 97`; the replay tested by killing Elegant |
| A program of the old session keeps the other server's UIKit after a switch | low | the port's hello refuses a mismatch with a clear log line; `session` restarts the services that map UIKit (§3.4, §8.3) |
| A switch leaves a program of the old session behind (a daemon, a hung app) | medium | the alias rule of §3.4; `session` lists, waits, forces; the services are untouched by design |
| The kernel changes K2, K3 | low | small, in `wsrv.cpp`; K2's fallback makes a bad setting harmless; P4 can ship with a restart if K3 is late |
| The console overlay cannot cover a full-screen game | medium | v1 limited (§7.5); K4 later |
| Touch and portrait need kernel and Circle work on hardware not yet chosen | medium | P10 last; try the device early (design study §15 step 2) |
| Updates of `lib/pocket/uikit.so` while a pocket session runs | low | the alias keeps the old image (§3.5); `pkg` says when the session must restart |
| Two servers to maintain | medium | one plumbing (b then c); the policy small; the shell outside |
| The new widgets' API frozen too early or too late (the `.abi` and the layout lock are forever) | high | design `SidePanel`, `Form`, the roles against three real apps each (Media + Paint + QBStudio; Letters' and the Spreadsheet's dialogs; Media's and Mail's tables) before the first release; `ext` and reserved slots in every new class |
| The migration's size (≈ 32 + 15 days) and the desktop's look of migrated apps drifting | medium | one app at a time, each a commit with its desktop pictures compared before / after; the fallbacks cover the rest meanwhile |
| `UK_SBW` compiled into 22 apps (a fixed gutter) | low | an empty 10-px gutter under overlay bars until each app uses `uk_scroll_gutter ()` |
| Console's L1 / R1 wanted by several owners (tabs, sections, the switcher) | low | one rule (§7.5): the app's tabs, else its sections; the switcher on Home + shoulders |

## 13. The user's answers, and what is taken by default

**Answered by the user on 2026-10-08:**

1. **Kernel**: K1 (`lib_open_as`), K2 (the server chosen from `shell=`, with the fallback to Elegant) and
   K3 (`KAPI_WS_SWITCH`) — **accepted** (§3.8).
2. **Protocol**: "for windows we'll have UIKit_Win_*; the applications will be migrated, and no
   `kapi_win_` left in the kapi at all; no relay; ABI compatibility not a concern for now" — **the window
   API moves to UIKit** (`uk_win_*`), every program migrated, AppKit's window functions removed, each
   UIKit the only code speaking its server's protocol (§2.2, §4, §5.2; P2). The HANDOFF's decision of
   2026-10-05 ("the protocol is AppKit's") is reversed.
3. **Elegant**: "behaviour unchanged" — its sources may be reorganised; `user/Servers/common/` is
   extracted at P3 (§7.1).
4. **Sessions**: **yes** — the autostart split into a system part and `SD:/etc/session/<mode>` files
   (migrated once by `pkg commit`), a new **Mode** applet, Setup's welcome page preselecting the mode from
   the screen and the inputs (§8).

**Taken by default — the recommendation stands unless the user objects** (the user asked to proceed to
development after this analysis):

5. The alias's lifetime anchored on the server (§3.4).
6. The shell as client programs (`pocketshell`, `consolehome`), the server text-free (§7.2).
7. Scaling: integer composition for every app; native scale for the apps that declare it
   (`uk_logical_units`) or are listed in `apps.ini` (§6.3).
8. The adaptive widgets' API as proposed (§6.5–6.14), and the console's shoulders: the app's tabs, else
   its navigation sections; the switcher on Home + L1 / R1 (§7.5).
9. One shared desktop look for the migrated apps' side panels, toolbars and dialogs (the desktop UIKit's).
10. The first device: none chosen yet — P0's pictures at 800 × 480 and 640 × 480 and the official 7"
    display as the default target of P6–P10 until the user names another.

## Résumé (FR)

Cette étude technique (analyse seulement, rien n'est construit) met en œuvre les décisions de
l'utilisateur du 2026-10-08 : **PocketUI**, un serveur graphique à part à côté d'Elegant, sert les modes
**pocket** et **console** ; **une UIKit par serveur**, mêmes exports ; le serveur charge la sienne
(`SD:/lib/pocket/uikit.so`) sous l'**alias `SD:/lib/uikit.so`** par un nouvel appel noyau
`kapi_lib_open_as` ; **changer de mode = fermer la session graphique et la relancer**, depuis le Panneau
de configuration et l'assistant Setup. Contrainte reformulée par l'utilisateur : **un seul binaire par
appli pour tous les modes** — les sources des applis peuvent (et doivent) adopter le nouveau système.

**L'alias fonctionne** : le cache d'images du noyau indexe les bibliothèques par leur chemin canonique
(`FindNamed`) ; l'alias est une seconde clé sur la même image, consultée en premier, posée uniquement par
le processus qui détient le rôle de serveur graphique, jamais sur `appkit.so`. Les bibliothèques sont
placées une seule fois dans l'arène (`LibPlace`) : serveur et applis voient l'UIKit aliasée **à la même
adresse**. Affinage proposé de la durée de vie : l'alias est ancré sur le serveur — conservé si le même
serveur redémarre après un plantage, abandonné dès qu'un autre serveur démarre (sinon un démon sans
fenêtre comme `printd` garderait l'UIKit pocket pour les applis du bureau). Les vérifications de version
et de « fichier modifié » portent sur le vrai chemin ; `image_list` signale l'alias par un drapeau.

**Le protocole des fenêtres passe dans UIKit (décision de l'utilisateur, 2026-10-08).** Aujourd'hui les
appels de fenêtre sont les fonctions `kapi_*` d'AppKit qui parlent `elegant.h` (`appkit_ws.inc`), et une
douzaine de jeux, Doom, la barre de menus, le dock, `rdpd`… s'en servent sans UIKit. La décision du
2026-10-05 (« le protocole est celui d'AppKit ») est donc renversée : l'API des fenêtres devient celle
d'UIKit, **`uk_win_*`** (le `UIKit_Win_*` de l'utilisateur, écrit selon les conventions d'UIKit ;
liaison C, `uikit/win.h`) ; **tous les programmes sont migrés** (≈ 55 fichiers : jeux, plein écran,
shell du bureau, services, applis, `rdpd`, `el0test`, `wstest` ; `vncd` n'est pas touché) ; les **≈ 48
fonctions de fenêtre sont retirées d'AppKit** et d'`appkit.abi` — rupture d'ABI voulue, tout est
recompilé et republié ensemble (`kapi >= 97`). AppKit ne garde que le transport du noyau
(`kapi_ws_ctl` : `KAPI_WS_CALL`, `KICK`, `ACTIVE`) et les primitives de plein écran. **Chaque UIKit est
alors le seul code qui parle le protocole de son serveur** : le *port* bureau contient `elegant.h`, le
code client d'aujourd'hui et le rejeu après redémarrage d'Elegant ; le port pocket parle le protocole
propre de PocketUI, libre (recommandé : reprendre la numérotation d'Elegant là où le sens est le même,
pour réutiliser le code commun, plus ses opérations à lui — rôle de shell, raccourcis système,
vignettes, classes de taille, clavier à l'écran). Coût : tout programme fenêtré mappe `uikit.so` — code
partagé (300 Ko, une fois), une page privée de 64 Ko par processus, une bibliothèque de plus sur 16 pour
les quelques programmes qui ne l'avaient pas ; les programmes C passent par la liaison C (`--bind-c`).
**Deux UIKit d'une même source** : mêmes en-têtes (ils sont l'ABI), objets
communs compilés une fois, un objet *port* par serveur, table pocket générée avec `libgen --frozen` sur
le même `uikit.abi`, test automatique d'identité des tables, un seul paquet. **Indépendance de la
résolution** : remplir l'écran pour les applis redimensionnables, carte centrée pour les fixes,
*viewport* défilant côté serveur pour les trop grandes, composition ×2 entière, échelle native sur liste
(`SD:/etc/pocket/apps.ini`) pour les applis pas encore migrées. **Les widgets adaptatifs** : l'appli déclare le *quoi*, chaque UIKit
rend le *comment* selon le mode — la barre de menus (les menus restent des données ; c'est le composant
du shell qui s'adapte : `menubar` sur le bureau, barre d'état de `pocketshell`, bandeau révélé à la
demande de `consolehome`), menus contextuels et listes déroulantes, dialogues (feuilles en pocket,
panneau façon PS2 en console ; nouveau `Form` ; 99 dialogues maison en coordonnées absolues présentés en
feuille défilante tant qu'ils ne sont pas migrés), **SidePanel** (un côté et un rôle : navigation à
gauche — rail d'icônes, tiroir ☰, colonne au d-pad avec L1/R1 — ou inspecteur à droite — panneau qui
glisse, feuille du bas ; éléments structurés avec vignette, bascule, pastille, plus un pied et des pages
en contenu libre, pour les calques de Paint), **ToolBar** à priorités et débordement », onglets
(`TabStrip` adapté sans changer le Terminal ; L1/R1 en console), champs de saisie (type déclaré ;
clavier à l'écran en pocket seulement, jamais en console), listes et tableaux (rôles et priorités des
colonnes : cartes à deux lignes en portrait), barres de défilement superposées (attention à `UK_SBW`,
compilé dans 22 applis). Migration de 19 applis à panneau latéral (Media, File Viewer, Game Library,
Photos, Mail, Courier, Ledger, PDF, Slides, Paint, QBStudio, 3DForge…), des barres d'outils (Letters…),
des onglets, des bases de dialogues de Letters et du Tableur, des tableaux : environ 32 jours. **Le
serveur** : le code d'Elegant qui n'est pas de la politique est extrait dans `user/Servers/common/`
(comportement d'Elegant inchangé, accepté) et compilé dans les deux serveurs ; le shell (barre d'état,
lanceur, sélecteur, réglages rapides, accueil console) est fait de **programmes clients**. **Console** =
le même serveur dans un autre mode. **Trois petits changements noyau, acceptés** : l'alias, le choix du serveur
selon `shell=` avec repli sur Elegant, et une opération « changer de serveur ». Les
sessions (acceptées) : l'autostart scindé en partie système et fichiers `SD:/etc/session/<mode>`, un outil
`session`, une nouvelle applet « Mode » et un choix sur la page d'accueil de Setup (traduits). Plan en
phases P0 à P10, environ 97 à 104 jours de sessions (dont 10 pour passer l'API des fenêtres dans UIKit
et migrer tous les programmes, et 32 de migration vers les widgets adaptatifs, étalables), premier jalon
utile (toutes les applis sous PocketUI, changement depuis le Panneau) vers 24 jours. Le §13 consigne les
réponses de l'utilisateur ; les autres recommandations sont retenues par défaut.
