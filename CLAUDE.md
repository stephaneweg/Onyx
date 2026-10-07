# Onyx — project instructions

**Onyx** is a homemade **multi-process operating system** for the **Raspberry Pi 4**
(AArch64), built on **Circle** as its HAL. *Onyx* is the name of the OS, its kernel, and its
GUI. The repository folder is historically named `Zircon` (a legacy name — keep filesystem
paths like `Zircon/circle` as-is; no relation to Google/Fuchsia's Zircon).

**Continuing the work? Read `docs/HANDOFF.md` first** (where things stand, the conventions, the
next tasks: after the desktop redesign — a modernised CDE, `docs/gui-redesign/README.md`, done
and in `main` — its next ideas, the GameCube emulator, and Jet Browser, the WebKit port: its section
says where the code is, how to build, what is next).

The reference documentation is in **`docs/`** and is written in **English**:

- `docs/01-PROJECT-OVERVIEW.md`
- `docs/02-KERNEL-INTERNALS.md` (kernel internals, excluding Circle)
- `docs/03-DEVELOPER-GUIDE.md`
- `docs/04-USER-GUIDE.md`
- `docs/05-CIRCLE-CHANGES.md` (patches in our Circle fork vs upstream `Step51`)
- `docs/06-KITS-GUIDE.md` (**the kits**: one per domain, how a program uses them, an example for each — keep it up to date when a kit gains a subject)
- `docs/10-APPKIT.md` … `docs/18-PRINTERKIT.md` (**one reference per kit**: every operation it exposes — GENERATED from the kits' headers by `python tools/docgen/kitdocs.py`: never edit them by hand; after a kit's header changes, run it, then `python docs/build_docs.py`)
- `docs/08-WEBKIT-PORT.md` (Jet Browser, the Onyx web browser: the WebKit port — its status and how to resume, the plan, the patch series in `tools/webkit/patches/`, `jsc`; the NetSurf Jet and its documents 06 and 07 were removed on 2026-10-04)
- `docs/LICENSING.md` (the licences of everything Onyx contains; under which licence it can be distributed)
- `docs/BLUETOOTH-AUDIO-STUDY.md` (Bluetooth audio output: the feasibility study -- the chip, the stack options and their licences, the plan B0-B6; nothing built)
- `docs/SHARED-LIBS-PLAN.md` (shared libraries behind an export table — the user's decided design, the plan and the Pi tests; the study before it: `docs/GUI-USERSPACE-STUDY.md`)
- `docs/MULTI-WINDOW-STUDY.md` (more windows in Elegant — no fixed count — and several windows per program, e.g. Telegram's conversations: a feasibility study, 2026-10-07, nothing built; Part A then B1-B6)
- `docs/MULTI-USER-PLAN.md` (several users: accounts, login and sessions, `/home`, rights on FAT enforced by the kernel, remote access per user — a study only: **set aside by the user on 2026-10-05, Onyx stays single-user** — do not start it unasked)
- `docs/ARTICLE-PROGRAMMEZ.md` (in French: the working memory for the article in the magazine *Programmez!* on developing Onyx with Claude — the user's account, the plan, the draft, the pros/cons; waiting for the magazine's specs; not exported)
- `docs/LOCAL-AGENT-WEBKIT.md` (briefing for a local agent continuing the WebKit port on the user's PC: setup, branches, plan)

Word/PDF exports (with screenshots) live in `docs/exports/`, generated from the `.md` by
**`docs/build_docs.py`** (`python docs/build_docs.py` → `.docx` via pandoc using the themed
**`docs/assets/reference.docx`** = the Onyx visual signature; `.pdf` via Word, else
LibreOffice headless). Screenshots
(the real apps, run on the PC) are in `screenshots/`, produced by
**`sh tools/tests/desktop_sim/shots.sh`**; the `.md` reference them as `../screenshots/<x>.png`.

> Note: in-OS strings and the rendered screenshots may still say "Zircon" (legacy); the docs
> use "Onyx". Renaming the code/app strings to Onyx is a separate, pending task.

> Names: the shared libraries are "kits" -- **UIKit** (the widget toolkit, `user/Kits/uikit/`, `namespace uikit`,
> `uk_*`, `SD:/lib/uikit.so`; named **wtk** until 2026-10-05, fully renamed), **SystemKit**, **NetKit**, **AudioKit** (`user/Kits/audiokit/`), **PrinterKit** (`user/Kits/printerkit/`), **FileKit** (`user/Kits/filekit/`: ZIP, zlib, files and trees),
> **ImageKit** (`user/Kits/imagekit/`: pictures read, written, resized, adjusted; it uses FileKit), **AppKit** (`user/Kits/appkit/`: the programs' interface to the kernel, loaded and bound by the kernel), **FontKit** (`user/Kits/fontkit/`, `SD:/lib/fontkit.so`: FreeType and the apps' font manager; it was `user/ft`, `ft.so`, the package `ft` until 2026-10-05). docs/03 sections 5.6 to 5.10. DocumentKit: an analysis only (IDEAS.md).

> Layout of `user/` (the user, 2026-10-05) -- everything sorted by its use:
> - **`Apps/<name>/`** the graphical apps; **`BinUtils/`** the console programs (`SD:/bin`; it was `user/bin`);
> - **`Servers/<name>/`** the system's servers, of several sources (`SD:/bin/<name>`): **Elegant**, the graphics server
>   (`Servers/elegant`: the window manager, the compositor, the input's routing -- no longer the kernel's;
>   docs/02 §10, docs/HANDOFF.md);
> - **`Kits/<kit>/`** the shared libraries (appkit, uikit, systemkit, netkit, audiokit, filekit, imagekit, printerkit, fontkit);
> - **`Runtime/`** what every program is linked with (`crt0.S`, `user.ld`, `lib.ld`, `lib.h`, `librt.cpp`, `umm.h`,
>   `onyxpp.hpp`, `libc/` = newlib's glue and the POSIX library);
> - **`Libs/`** the libraries linked into the programs (av, img, zlib, tls, v3d, gpucomp, pdf, mail, pkg, basic, demo);
> - **`Emulators/`** the emulators' cores (gb, gba, nes, snes, n64, gc, `emucore.h`); **`Ports/`** doom, stk;
> - **`Include/`** the few headers several programs share that no kit has yet (`gamepad.h`, `json.hpp`, `lineedit.h`, `docguard.h`);
>   **`Apps/games/`** what the games share (`game.h`, `cards.h`); Koton's plugin headers are in `Apps/koton/plug`.
> - In the kits since 2026-10-05 (docs/03 §5.9.0): **AppKit** = what makes a program run (the kernel's calls, strings,
>   console, `.ini`, program starting `lx_*`); **SystemKit** (`user/Kits/systemkit`, `systemkit.so`) = what a program
>   says to the system and the other programs (notifications, clipboard, wallpaper, volume, trash, preload list,
>   file associations, the applets' protocol); **NetKit** (`user/Kits/netkit`, `netkit.so`) = the network (ftpfs,
>   httpc, `http.hpp`); **FileKit** has the file helpers (`filekit/fsutil.h`); **UIKit** the icons' pictures
>   (`ui::icon_load`, through ImageKit); **ImageKit** the codecs' sources (`Kits/imagekit/img`).
>
> `user`, `user/Kits`, `user/Runtime`, `user/Include`, `user/Libs`, `user/Emulators`, `user/Ports` are on every
> include path: a source writes `#include "appkit/appkit.h"`, `"uikit/uikit.h"`, `"umm.h"`, `"gamepad.h"`,
> `"tls/onyx_tls.hpp"`, `"gb/gb.h"`... whatever its own folder. A header used by one program is beside that
> program; a header holds declarations as far as possible (the user). The build's outputs: `user/lib/`.

## RULE — kits first (the user, 2026-10-05)

One **kit per domain** instead of many loose headers and libraries, and no tight coupling to the kernel:
**AppKit** (what makes a program run: the kernel's calls, strings, console, `.ini`, starting programs), **UIKit**
(the interface, the icons), **SystemKit** (talking to the system and the other programs), **NetKit** (the network),
**FileKit** (files, archives), **ImageKit** (pictures), **AudioKit** (sound), **FontKit** (fonts), **PrinterKit**
(printing); DocumentKit to come.

- **Reusable code goes into the adequate kit** — not a new shared header, not a copy in an app. No kit fits:
  create one (a header that declares, the code in the library, an append-only `.abi`, a package).
- **Programs draw on the kits as much as they can**, and never reach the kernel themselves (only AppKit does).
- What one program alone uses stays beside that program.
- A C program links a kit through `lib/<kit>.imp_c.a` (libgen `--bind-c`), a C++ one through `lib/<kit>.imp.a`.
- A program includes **the kit's one header**: `"appkit/appkit.h"`, `"uikit/uikit.h"`, `"systemkit/systemkit.h"`,
  `"netkit/netkit.h"`, `"filekit/filekit.h"`, `"imagekit/imagekit.h"`, `"audiokit/audiokit.h"`,
  `"printerkit/printerkit.h"` (FontKit: `"fontkit/uikitface.h"` / `"fontkit/fonts.h"`; `"netkit/http.hpp"` apart).

## RULE — every app in English and French (the user, 2026-10-06)

The **language is the system's** (`SD:/etc/system.ini` `language=`: the Control Panel's **Language & Region**
applet `langconf`, Setup's welcome page; SystemKit's `systemkit/locale.h`) — an app never has a language switch of
its own. **Every new app is translated from its first version**; an older one is translated when it is worked on
(done: Setup, the Control Panel and all its applets, the Terminal, Ledger, Turtle Quest and its BASIC).

- The sources keep their English words wrapped in `TR ("...")` (`uikit/lang.h`; `TRC` with a context, `TRN` for a
  word kept in a table), `uk_lang_init ()` in `main` after the text face is installed; the French in
  `sdcard/apps/<app>.app/lang/fr.txt` (`English<TAB>French`). Never translate what is stored or compared.
- **`python tools/lang/check.py <app>`** (or `--all`) must say 0 missing; look at the app in French with
  `SHOTS_LANG=fr SHOTS_PNG=<folder> sh tools/tests/desktop_sim/shots.sh <app>` (the words must fit).
- docs/03 "An app in another language" says the rest.

## RULE — keep the documentation up to date automatically

When you **add or change a `kapi` function or an application**, you **update the
documentation in the same session**, without being asked again:

- **`kapi` function** (`kernel/include/kern/kapi_abi.h`, `kernel/sys/kapi.cpp`,
  `kernel/sys/kapitable.cpp`, `user/Kits/appkit/appkit.h` + `appkit_calls.inc`) → update the ABI table in
  `docs/02-KERNEL-INTERNALS.md` (+ `docs/03` if dev-facing) and the version history
  (`KAPI_ABI_VERSION`). Since **AppKit** (2026-10-05) the programs reach the kernel through
  `SD:/lib/appkit.so` (`user/Kits/appkit/`): the programs include **`appkit/appkit.h`** (declarations only; there is no `kapi.h` any
  more) and call its `kapi_*` functions by name. Add the call's declaration in `appkit/appkit.h` and its
  `KAPI_CALL` body in `appkit/appkit_calls.inc` (the only code that reads the kernel's table), and commit `user/Kits/appkit/appkit.abi` (append-only **by name**: never remove or
  rename a line). The kernel's own table (`kapi_abi.h`) may be restructured — AppKit is adapted and rebuilt
  with the kernel, no program is; a program never reads `KT` (docs/03 §5.10, docs/02 §8).
- **Application** (`user/Apps/<name>/`, `user/BinUtils/*.c`, `sdcard/apps/<name>.app`) → update the catalog
  in `docs/04-USER-GUIDE.md` (controls, files read/written) and `docs/03` if relevant. Add
  the `.elf` to `user/Makefile` (or `user/BinUtils/Makefile`).
- **If the change is visible on screen** → regenerate the affected screenshot(s) with
  `sh tools/tests/desktop_sim/shots.sh <name>` (add the app's scenario there); this may be handled
  by a dedicated chat.
- After editing the `.md` (or the screenshots), **regenerate the exports**:
  `python docs/build_docs.py`. Keep the English wording and the "Onyx" name.

## RULE — git workflow: always on top of `main`, commit into `main`

Before **every new development** and before **every commit**: `git fetch origin main` and
merge the latest `origin/main` into the working branch (`git merge origin/main`, resolve any
conflict). Then commit and **push into `main`** (`git push origin HEAD:main`; also push the
working branch). Delete the working branch (local and `origin`) **only when the user says so**.

## RULE — publish the packages automatically

When the work **changes what is on the card** (a new app, an app or a `/bin` tool rebuilt and staged,
the kernel, `sdcard/etc`, fonts, resources, samples), you **package and publish it in the same
session**, without being asked, following the skill **`.claude/skills/onyx-packages/SKILL.md`**: a new
app's package declared in `tools/pkg/packages.ini` (its extra files, its needs, its user files, its
samples; an emulator's `games =`), then `sh tools/pkg/publish.sh` → the new versions, the signed
index pushed to `stephaneweg/onyx-packages`, and `tools/pkg/versions.ini`, `sdcard/var/pkg/db`,
`sdcard_lite` committed in onyx. The signing key comes from the environment (`ONYX_PKG_KEY`); without
it, say so to the user and do not publish. Never generate another key.

## RULE — licences: our own software under MIT

Everything of ours that can be is under the **MIT licence** (the user, 2026-10-01): new code carries the MIT
notice; a programme that links a copyleft library keeps its files MIT but is distributed under that licence (the
kernel GPL-3.0 with Circle, Jet LGPL-2.1 with WebKit, the Media Player GPL-2.0 with FFmpeg, the PDF Viewer AGPL-3.0
with MuPDF, Doom GPL). Never pull a
library that would force another licence on an app without asking the user. Details: `docs/LICENSING.md`.

## Build (reminder)

From `kernel/`: `make` (→ `kernel8-rpi4.img` then the apps), `make stage` (copies image +
`apps/<name>.app/main` + `bin/*` to `sdcard/` — executables carry **no extension** on the card). Prerequisites and details in
`docs/03-DEVELOPER-GUIDE.md`. Commit in the Onyx repo explicitly (the cwd drifts).
