---
name: kernel-build-local-wsl
description: "How to build and try an Onyx kernel from the user's PC: the WSL clone ~/onyx (with Circle), the toolchains, the paths kept for the Windows checkout"
metadata:
  node_type: memory
  type: reference
  originSessionId: ffa19f44-35fb-43e7-89a2-d94f901f5b56
  modified: 2026-10-19T10:47:01.233Z
---

**2026-10-19: the WSL Ubuntu disk was lost in a cleanup** (everything WSL-side gone: ~/p7, ~/ndsw, the WebKit
builds, ~/src/circle5-onyx). Rebuilt that day on a fresh Ubuntu 26.04 (user `trolly`, passwordless sudo set by
the user, 16 cores):
- **`~/onyx`**: a clone of github stephaneweg/onyx with `--recurse-submodules` (Circle b47451c built DEPTH=32 in
  `~/onyx/circle`); `sh tools/build-sdcard.sh` there builds everything (kernel, kits, apps, stage into
  `~/onyx/sdcard`) -- passed on 2026-10-19. Pull it before reusing (`git pull --recurse-submodules`).
- Toolchains: `~/.cache/onyx/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf` (aarch64-none-elf) and
  `/opt/toolchains/aarch64-onyx-elf-14.2` (tools/toolchain/fetch.sh); both on the PATH in `~/.bashrc`.
- Compatibility symlinks for the old workflow: `~/src/circle-onyx -> ~/onyx/circle`,
  `~/toolchains/arm-gnu-...-aarch64-none-elf -> ~/.cache/onyx/...`.

The Windows checkout has no `circle/` sources (submodule not fetched), so `make -C kernel` there needs
`CIRCLEHOME=$HOME/src/circle-onyx` (from WSL: `make -C /mnt/c/Projects/Onyx/kernel -j16 CIRCLEHOME=$HOME/src/circle-onyx kernel8-rpi4.img`).
`make clean` does not remove everything: delete `kernel/**/*.o` and `*.d` after changing Circle's configuration.
Check the submodule commit still matches (`git submodule status`) before reusing the clone. The Pi 5's Circle
(`~/src/circle5-onyx`, a copy configured `-r 5`, what `tools/pi5/circle5.sh` does) **must be recreated** before a
pi5 build: `make -C kernel BOARD=pi5 CIRCLEHOME=$HOME/src/circle5-onyx kernel_2712.img` (switching boards cleans
the objects: build pi5 first, copy the image aside, then pi4 again).

Publishing a kernel from here: never `make stage` in the Windows checkout (it copies every locally built program, other sessions' included) -- copy the image(s) and my own app into `sdcard/` / `sdcard5/` by hand, then `publish.sh` for both boards. A `KAPI_ABI_VERSION` bump rewrites every package's `kapi >= N` in the index and the cards' db (expected).

Trying it on the Pi (with the user's standing permission to test there, see [[pi-remote-access]]): nobody
playing (`ps`), FTP the card's `kernel8-rpi4.img` down and store it back as `kernel8-rpi4.img.prev`, STOR the new one,
read it back, `reboot` over telnet, wait for telnetd; afterwards put the original back unless the kernel is to be
published. See also [[n3ds-emulator-decisions]], [[wsl-webkit-build-env]], [[pocketui-p7-local-setup]], [[nds-emulator-local-tests]] (their WSL folders are gone since 2026-10-19).
