---
name: basic-native-test-setup
description: "How BASIC's machine code is tested on this PC (qemu + the bare-metal toolchain, no Linux cross compiler), publishing from a worktree, and a shell-escaping trap"
metadata:
  node_type: memory
  type: reference
  originSessionId: 74344419-3955-4412-8f40-4bfd205d753d
  modified: 2026-10-05T01:34:10.900Z
---

Set up on 2026-10-05 (BASIC classes + native back end, worktree session):

- **AArch64 tests without a Linux cross compiler**: WSL has `qemu-aarch64` but no
  `aarch64-linux-gnu-g++` (installing it needs sudo). `tools/tests/run_basic_native_test.sh` builds
  the test host with the bare-metal toolchain (`~/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin`
  on the PATH) + newlib, its system calls done by Linux ones (`tools/tests/basic/a64/linux_shim.c`),
  and runs it under qemu. The same trick works for any freestanding-ish code to try in AArch64.
- **Publishing packages from a git worktree**: the shared WSL clone `~/src/onyx-packages` may hold
  another session's uncommitted index files (publish.sh then stops at its merge). I used my own
  shallow clone, `~/src/onyx-packages-oo` (`git clone --depth 1`), with
  `ONYX_PACKAGES_DIR=$HOME/src/onyx-packages-oo ... sh tools/pkg/publish.sh --no-push`, checked
  the commit's file list, amended the author (`Stephan WEGENER <>`), pushed from Windows. In a
  worktree WSL's git cannot read the onyx tree (the "fatal: not a git repository" lines of
  publish.sh are harmless: only the commit message loses the onyx hash).
- **Trap**: in a Bash-tool heredoc (`python - <<'EOF'`), a `\\n` inside the script arrives as a real
  newline (C / Python string literals get broken). Write such patch scripts with the Write tool
  (raw strings) and run the file; fix a broken line with Edit.
- **The Pi**: `tools/tests/shlib/pi_apps.py` runs at import (never import it). Telnet drops now
  and then (retry the connect). The Pi restarting around a `kill` of a graphical app that day was a kernel hang at a
  full-screen app's *start* (fixed 2026-10-05, `DisplayPresentIdle`; docs/HANDOFF.md, the BASIC
  section): `kill` is safe. After an unexplained restart read `SD:/etc/lastcrash.txt` first; unchanged
  = a clean restart (often another session putting its own kernel on the Pi -- check `ps` and the
  kernel's size before replacing it or rebooting).

See [[pi-remote-access]], [[wsl-webkit-build-env]], [[basic-oo-native-decisions]].
