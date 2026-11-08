---
name: nds-emulator-local-tests
description: "Testing ndsemu (the DS emulator) from this PC — where the user's ROMs are, the WSL build of ndstest, what is missing, traps met on 2026-10-09"
metadata:
  type: reference
---

The brief is `docs/LOCAL-AGENT-NDS.md`; results go in `docs/HANDOFF.md`'s DS section (the games' table).

- **The user's ROMs**: `C:\Temp\Roms\<Game>\<Game>.nds` on the PC (`/mnt/c/temp/Roms/...` in WSL), the same tree on
  the Pi under `SD1:/Roms/`. Never copied into the repository. The Pi's ftpd only serves `SD:` (not `SD1:`).
- **ndstest** (interpreter) builds in WSL with plain `g++` (the brief's line) into `~/ndsw`; `NDS_SHOTS=<prefix>`
  is a file prefix, not a folder. The ARM toolchain is in WSL's
  `~/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin` (installed 2026-10-09 without sudo, the user
  agreed): put it on the PATH for `tools/tests/nds/src/build.sh` and the 3DS tests. Still missing: `aarch64-linux-gnu-g++`
  (the DS JIT under qemu; the bare-metal toolchain + `linux_shim.c` does instead, as `run_n3ds_test.sh`).
- **Device build**: `make -C user ndsemu.elf` as in [[pocketui-p7-local-setup]]; the Pi 4 and Pi 5 app binaries are
  the same file (copy to `sdcard/` and `sdcard5/`), publish both boards (`publish.sh --no-push`, then `--board pi5`).
- **Traps**: `git merge` fails half-way when the user's Onyx Remote runs (`pc/dist/OnyxRemote.exe` locked): the tree
  is main's but HEAD is not — `git reset origin/main` (mixed), leave the exe alone, never commit it.
  `tools/tests/shlib/pi_apps.py` has no `__main__` guard: copy its `Pi` class, do not import it.
  `pkg update ndsemu` refuses while the user's game runs — do not kill their game; ask them to close it.
  Look at `ps` before `run ndsemu ...` on the Pi (the user may be playing); see [[pi-remote-access]].
