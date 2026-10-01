# Onyx — project instructions

**Onyx** is a homemade **multi-process operating system** for the **Raspberry Pi 4**
(AArch64), built on **Circle** as its HAL. *Onyx* is the name of the OS, its kernel, and its
GUI. The repository folder is historically named `Zircon` (a legacy name — keep filesystem
paths like `Zircon/circle` as-is; no relation to Google/Fuchsia's Zircon).

**Continuing the work? Read `docs/HANDOFF.md` first** (where things stand, the conventions, the
next tasks: after the desktop redesign — a modernised CDE, `docs/gui-redesign/README.md`, done
and in `main` — its next ideas, the GameCube emulator, and Jet Browser (NetSurf) "as in Chrome": its section
says where the code is, how to test on the PC, how to build, what is next).

The reference documentation is in **`docs/`** and is written in **English**:

- `docs/01-PROJECT-OVERVIEW.md`
- `docs/02-KERNEL-INTERNALS.md` (kernel internals, excluding Circle)
- `docs/03-DEVELOPER-GUIDE.md`
- `docs/04-USER-GUIDE.md`
- `docs/05-CIRCLE-CHANGES.md` (patches in our Circle fork vs upstream `Step51`)
- `docs/06-JET-BROWSER.md` (Jet Browser, the Onyx web browser based on NetSurf: the Onyx changes to NetSurf, libcss, FreeType; the fonts)
- `docs/07-BROWSER-GAPS.md` (what the browser lacks next to Ladybird / Chromium / WebKit, the plan)
- `docs/LICENSING.md` (the licences of everything Onyx contains; under which licence it can be distributed)

Word/PDF exports (with screenshots) live in `docs/exports/`, generated from the `.md` by
**`docs/build_docs.py`** (`python docs/build_docs.py` → `.docx` via pandoc using the themed
**`docs/assets/reference.docx`** = the Onyx visual signature; `.pdf` via Word, else
LibreOffice headless). Screenshots
(the real apps, run on the PC) are in `screenshots/`, produced by
**`sh tools/tests/desktop_sim/shots.sh`**; the `.md` reference them as `../screenshots/<x>.png`.

> Note: in-OS strings and the rendered screenshots may still say "Zircon" (legacy); the docs
> use "Onyx". Renaming the code/app strings to Onyx is a separate, pending task.

## RULE — keep the documentation up to date automatically

When you **add or change a `kapi` function or an application**, you **update the
documentation in the same session**, without being asked again:

- **`kapi` function** (`kernel/include/kern/kapi_abi.h`, `kernel/sys/kapi.cpp`,
  `kernel/sys/kapitable.cpp`, `user/kapi.h`) → update the ABI table in
  `docs/02-KERNEL-INTERNALS.md` (+ `docs/03` if dev-facing) and the version history
  (`KAPI_ABI_VERSION`). **The ABI is append-only**: never reorder/remove a field.
- **Application** (`user/*.c`, `user/bin/*.c`, `sdcard/apps/<name>.app`) → update the catalog
  in `docs/04-USER-GUIDE.md` (controls, files read/written) and `docs/03` if relevant. Add
  the `.elf` to `user/Makefile` (or `user/bin/Makefile`).
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
kernel GPL-3.0 with Circle, Jet GPL-2.0 with NetSurf, the PDF Viewer AGPL-3.0 with MuPDF, Doom GPL). Never pull a
library that would force another licence on an app without asking the user. Details: `docs/LICENSING.md`.

## Build (reminder)

From `kernel/`: `make` (→ `kernel8-rpi4.img` then the apps), `make stage` (copies image +
`apps/<name>.app/main` + `bin/*` to `sdcard/` — executables carry **no extension** on the card). Prerequisites and details in
`docs/03-DEVELOPER-GUIDE.md`. Commit in the Onyx repo explicitly (the cwd drifts).
