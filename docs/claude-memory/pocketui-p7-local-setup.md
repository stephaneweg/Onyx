---
name: pocketui-p7-local-setup
description: "How PocketUI's P7 (apps' migration) is built and tested on this PC — WSL folders, what is missing (ffmpeg program, signing key), traps met"
metadata:
  node_type: memory
  type: reference
  originSessionId: c19d344f-4587-46e9-bbf2-772bfea3cf8d
  modified: 2026-10-08T23:40:35.463Z
---

Set up on 2026-10-09 (P7 continued locally after the cloud session was deleted; branch `claude/adoring-planck-f0hnqc`,
pushed into `main` at each slice). The pattern for an app is in `docs/HANDOFF.md` (the P7 section).

- **Host builds** live in WSL under `~/p7`: `SHOTS_TMP=$HOME/p7/shots0 FFMPEG_HOST=$HOME/p7/ffhost sh tools/tests/desktop_sim/shots.sh <app>`
  (the first run builds MuPDF and FFmpeg for the PC: ~15 min; it rebuilds every app each time, ~8 min) and
  `sh tools/tests/server_sim/adaptive.sh $HOME/p7/sim <app>` (the app under PocketUI at the four sizes; Media needs
  `MEDIA_SHOTS=$HOME/p7/shots0 FFMPEG_HOST=$HOME/p7/ffhost`). Copy the PNGs to the session scratchpad to look at them.
- **Device builds** work from the Windows checkout: `make -C /mnt/c/Projects/Onyx/user -j12 <app>.elf` with
  `~/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin` on the PATH (the kits are built there too),
  then `cp <app>.elf ../sdcard/apps/<app>.app/main`; `python3 tools/libgen/check_stubs.py user/Kits/appkit/appkit.abi sdcard/apps sdcard/bin`.
- **Missing on this PC**: the `ffmpeg` program in WSL (`tools/tests/media/make_library.py` → no `screenshots/media-*.png`;
  needs `sudo apt install ffmpeg`, see [[sudo-commands-to-user]]), pandoc (`docs/build_docs.py`), the `circle`
  submodule (shots.sh's `menubar` scene reads `circle/lib/font8x16.cpp`).
- **Publishing from this PC** (the user gave the key's place on 2026-10-09: `C:/Users/troll/onyx_pkg-key.pem`; never
  read or print it): a shallow clone `C:/Projects/onyx-packages-p7`, then in WSL
  `ONYX_PACKAGES_DIR=/mnt/c/Projects/onyx-packages-p7 ONYX_PKG_KEY_FILE=/mnt/c/Users/troll/onyx_pkg-key.pem sh tools/pkg/publish.sh --no-push`,
  look at the commit (`git show --stat`; jet's .opk ~43 MB), `git push origin HEAD:main` from Windows git, then commit
  `versions.ini`, `sdcard/var/pkg/db`, `sdcard_lite` in onyx. A copy `onyx_pkg-key.pem.txt` sat untracked in the repo
  root that day: never `git add -A` there.
- **The Pi** was at 192.168.0.10 that day (telnet 23: connect, wait for `$ `, send a command, read; `kmsg` only shows
  the recent lines). See [[pi-remote-access]].
- **Traps**: a file written with the Write tool loses its lines' trailing spaces (a `lang/fr.txt` whose words end
  with a space -- pieces of a sentence: give them back, `check.py` does not see it); a Bash heredoc eats `\\` (write patch scripts with the Write tool — [[basic-native-test-setup]]); a
  background `wsl ... &` dies with its shell (use the Bash tool's `run_in_background`); a simulator run needs
  `SIM_SLEEP=1` for an app's scan thread to finish; `tools/lang/check.py` needs `PYTHONIOENCODING=utf-8` on Windows;
  a header that a kit also includes (Photos' `imgops.h` ← ImageKit) breaks the kit's build when it gains `TRN`.
