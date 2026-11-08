---
name: wsl-webkit-build-env
description: "Where and how the WebKit port (Jet) is built on this PC (WSL Ubuntu, ~/onyx + ~/webkit since 2026-10-19), how to run commands and push from Windows"
metadata:
  node_type: memory
  type: project
  originSessionId: ffa19f44-35fb-43e7-89a2-d94f901f5b56
  modified: 2026-10-19T14:42:00.101Z
---

**Since 2026-10-19** (the old WSL disk was lost, see [[kernel-build-local-wsl]]): WSL Ubuntu 26.04, user
**`trolly`**, passwordless sudo (the user set it up), 26 GB / 16 CPUs / 16 GB swap (`.wslconfig`).
- Onyx clone: `~/onyx` (main, Circle built); WebKit checkout `~/webkit` (made by build-jet.sh's fetch: the pinned
  revision + tools/webkit/patches); build tree `~/webkit-build/webkit-jit`; toolchain
  `/opt/toolchains/aarch64-onyx-elf-14.2`; sysroot `~/onyx/out/sysroot-onyx`; logs `~/logs`.
- Rebuilding Jet: `cd ~/onyx && git pull --recurse-submodules && sh tools/build-jet.sh` (resumable, 10 jobs).
  From scratch on 2026-10-19 it ran ~2-3 h; it passed after commit ec94332ba (codecs.mk excluded every
  `*/onyx/*` path, i.e. all of libvpx in a clone named `onyx`). Run it detached (`setsid nohup ... &`) and keep a
  `wsl.exe` waiting on the log so WSL stays up; a PC reboot stops it (just run it again).
- Then copy `~/onyx/sdcard/apps/jet.app/{main,start.html}` to `/mnt/c/Projects/Onyx/sdcard/apps/jet.app/` -- **not
  app.txt**: build-web.sh stages user/Apps/jet's, which lacks the card's `pad_*` lines.
- The old folders (~/src/Onyx, ~/src/Onyx-loader, ~/webkit-build/jsc-*, ~/src/onyx-packages) are gone.
- `git am` in `~/webkit` needs `-c user.name=... -c user.email=...` (no identity set in WSL).
- `/tmp` in WSL does not survive between sessions: keep bench data under `$HOME`.
- Never edit a shell script that a background job is running (replace it atomically instead).
- No pandoc in WSL nor on Windows: `docs/build_docs.py` exports cannot be regenerated here. Windows has Python 3.12.
- The package signing key is on this PC: `C:\Users\troll\onyx_pkg-key.pem` (never print it). Publishing from
  here: in WSL, from `/mnt/c/Projects/Onyx`, with `ONYX_PKG_KEY_FILE=/mnt/c/Users/troll/onyx_pkg-key.pem`,
  `ONYX_PACKAGES_DIR=$HOME/src/onyx-packages` (re-clone it first), `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0='*'`:
  `sh tools/pkg/publish.sh --no-push`; then amend the author (`Stephan WEGENER <>`) and push from Windows.
- **Before publishing from `C:\Projects\Onyx`**: `sdcard/apps/jet.app/main` (≈105 MB, git-ignored) must be there,
  else mkrepo bumps `jet` to a package without its program ([[jet-package-needs-local-binary]]).
- `git status` on `/mnt/c/Projects/Onyx` from WSL takes many minutes: run git on that checkout from Windows.
- The GitHub repository moved to `stephaneweg/Onyx` (capital O; the old URL redirects).

**How to apply:**
- Simple commands: `wsl.exe -e bash -lc '...'` from the Bash tool works; for complex quoting write a script in
  the scratchpad and run it from PowerShell (`wsl.exe -d Ubuntu -- bash /mnt/c/.../script.sh`).
- Read/Edit WSL files through `\\wsl.localhost\Ubuntu\home\trolly\...`; never grep thousands of files through that path.
- WSL has no GitHub credentials: push from the Windows checkout.
- Ubuntu 26.04 has no `qemu-user-static`: the package is `qemu-user` (binary `qemu-aarch64`).
