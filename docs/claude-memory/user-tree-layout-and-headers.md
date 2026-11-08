---
name: user-tree-layout-and-headers
description: "user/ layout (Apps, BinUtils, Kits), apps include appkit/appkit.h (no kapi.h), headers hold declarations only — the user's choices of 2026-10-05"
metadata:
  node_type: memory
  type: feedback
  originSessionId: df787827-5be4-4157-830a-70b9c26f7815
  modified: 2026-10-05T09:52:48.257Z
---

The user's decisions on 2026-10-05, after AppKit ([[uikit-rename]] for the "kits" naming):

- **Headers hold declarations only, as far as possible** ("les fichiers h ne contiennent autant que possible que les déclarations"). Bodies go in a source file (or an `.inc` when they must be includable, as `user/Kits/appkit/appkit_calls.inc`).
- **Programs include `appkit/appkit.h`, not `kapi.h`** — "on voit clairement qu'on parle à AppKit et pas au kernel". `user/kapi.h` no longer exists; what reads the kernel's table is in `user/Kits/appkit/` only. The functions keep their `kapi_` names (AppKit's table is append-only by name).
- **Layout of `user/`**: `Apps/` graphical apps, `BinUtils/` console programs (was `user/bin`), `Kits/<kit>/` the kits. `user/Kits` is on every include path (`-I user -I user/Kits -I kernel/include` for a PC build).
- **`user/applib.h` is gone** (the user's decision): its strings, console, `.ini` reader and `ax_load_keymap` are AppKit's (`appkit_lib.inc`, declared in `appkit.h`); a helper used by one app goes into that app, by several into AppKit; a home-made widget goes to UIKit or away.
- **The rest of `user/`, decided by the user the same day**: `Runtime/` (crt0, linker scripts, lib.h, librt, umm.h, onyxpp.hpp, libc/), `Libs/` (av img zlib tls v3d gpucomp pdf mail pkg basic demo), `Emulators/` (gb gba nes snes n64 gc emucore.h), `Ports/` (doom stk), `Include/` (shared small headers). All these roots are on every include path. A header one program uses goes beside that program. `ft.so` became **FontKit** (`user/Kits/fontkit`, `SD:/lib/fontkit.so`, package `fontkit` replaces `ft`).
- Removed as dead (the user): the Shelf, `ask`, the panel, the app list, `quicklaunch.txt`, `applib.h`.
- The WebKit patches name `user/Libs/gpucomp` and `user/Libs/av` in their CMake lines: a WebKit tree built before the move must have those two paths follow.

- **The kits' roles (the user's words)**: AppKit = make an app run (mainly the link to the kapi); SystemKit = talk to the system / the other apps (notify, clipboard, wallpaper, volume, trash, preload, fileassoc, applet_proto); NetKit = the network (ftpfs, httpc, http.hpp); icons go through ImageKit (`ui::icon_load`) — "maybe PNG later". C console tools take the kits' code inline (`-DSK_INLINE`).
- **Into the kits (the user, same day)**: `notify`/`lx_*` (launch) → AppKit; clipboard → SystemKit (first put in UIKit, then the user created SystemKit); `fsutil.h` → FileKit (`filekit/fsutil.h`); `Libs/img` → `Kits/imagekit/img`; `kplug*` → `Apps/koton/plug`; `game.h`/`cards.h` → `Apps/games`; the activity shell, `embed.h`, `shell_proto.h` removed. `docguard.h` waits for the future DocumentKit.
- A package that renames a library (`replaces`) removes the old `.so` at once: programs of a package not yet installed (the staged `onyx`) fail until the restart — update `onyx` first.

**Why:** clarity for a developer reading the tree and the headers.
**How to apply:** new code follows this layout and include style; do not reintroduce inline kernel calls in a header or a `kapi.h`. A tree-wide move breaks paths held in variables (`$U/uikit`, `-I user` with a space): grep those forms, rebuild everything, rerun the PC tests and the Jet build before committing; `git add` with a stale pathspec aborts silently in a `;` chain — check `git show --stat` before pushing.
