# Onyx — briefing for a local Claude Code agent: the WebKit port on the user's PC

*Written 2026-10-02 by the cloud session that built the POSIX layer, the IPC, the toolchain and the
WebKit dependencies. Give this whole file to the local agent. It assumes a Windows PC with an Intel
i7 and **WSL 2 (Ubuntu)**; everything below runs inside WSL.*

---

## 0. Who you are working for, and the rules

- The user is the author of **Onyx**, a homemade multi-process OS for the **Raspberry Pi 4** (AArch64)
  built on **Circle**. **The user writes in French: answer in French.** The repository's docs are in
  **English** (keep them so).
- **Read first, in this order**: `CLAUDE.md` (project rules — they are binding), `docs/HANDOFF.md`
  (where everything stands; its top section "POSIX layer, IPC, toolchain, WebKit port"),
  `docs/POSIX-PLAN.md` (the plan and every decision, esp. §8 WebKit replaces Jet, §9 WebKit2 on the
  PlayStation port's model, §13 static binaries, §15 licence), `docs/08-WEBKIT-PORT.md` (the WebKit
  port: its "status / how to resume" section at the top) — the latter lives on the branch
  `webkit-port-step1` until it is merged into `main`.
- **Binding rules from `CLAUDE.md`**, in short:
  - **git**: before every development and every commit, `git fetch origin main` + `git merge
    origin/main`; commit, then push into `main` (`git push origin HEAD:main`) and the working branch.
    Work that is not yet validated on the Pi stays on its branch (the user decides when it goes to
    `main`). Commit messages end with a `Co-Authored-By:` line.
  - **docs**: any new/changed `kapi` call or app → update `docs/02`/`03`/`04` in the same session;
    `python docs/build_docs.py` regenerates the Word/PDF exports (needs pandoc).
  - **packages**: whatever changes what is on the card (`sdcard/`) is packaged and published with
    `.claude/skills/onyx-packages/SKILL.md` (`sh tools/pkg/publish.sh`); the signing key is the
    user's (`~/.onyx/pkg-key.pem` on the PC, or `ONYX_PKG_KEY`) — **never generate another key, never
    write it into a file of a repository or the chat**. Without the key: say so, do not publish.
  - **licences**: our own code is **MIT**; the WebKit browser is **LGPL-2.1+** (the user's decision,
    2026-10-02); never pull a library that would force another licence on an app without asking.
  - **never commit**: ROMs / ISOs / saves, `sdcard/etc/wpa_supplicant.conf` (the pre-commit hook
    refuses it — never defeat it), `sdcard/etc/ftpfs.ini`, `shelf.ini`,
    `SD:/apps/lisa.app/config.ini`, `sdcard/etc/clock` (the check is in `docs/HANDOFF.md`).
- **The user tests on the real Pi** (copies `sdcard/` to the card) and reports results; you cannot
  run anything on the Pi yourself. Validate everything you can on the PC first (the benches below),
  then give the user a precise test plan.

---

## 0bis. Branches — which one to use

| Branch | What it is | You |
|---|---|---|
| **`main`** | everything validated on the Pi (kapi v76, POSIX, IPC, toolchain, ports) and published | merge into it **only** what the user validated on the Pi (then publish the packages) |
| **`webkit-port-step1`** | WebKit step 1 (WTF + JavaScriptCore → `jsc`), pushed regularly by the cloud session | **start here**: `git checkout webkit-port-step1`, finish step 1 if it is not done (see `docs/08-WEBKIT-PORT.md`'s status), keep pushing to it |
| **`webkit-port`** | the WebKit port from step 2 on (WebCore, WebKit2, the Onyx view, the browser) | **create it from `webkit-port-step1`** once `jsc` works (`git checkout -b webkit-port webkit-port-step1 && git push -u origin webkit-port`) and do steps 2–5 there; your sub-agents work in worktrees branched from it and you merge them back into it |
| `ccr-182e4cf6-fxr778` | the cloud session's working branch | **do not use** |

Keep `webkit-port` up to date with `main` (`git fetch origin main && git merge origin/main`) before
each development, as `CLAUDE.md` requires; push it after each milestone. When the user has tested a
stage on the Pi and agrees, merge `webkit-port` into `main` (`git push origin HEAD:main`) and publish.

---

## 1. Set up WSL for big C++ builds (once)

1. **Give WSL enough resources** — `%UserProfile%\.wslconfig` (Windows side), then `wsl --shutdown`:
   ```ini
   [wsl2]
   memory=26GB        # the user's PC: i7-11800H (8 cores / 16 threads), 32 GB RAM -- leave ~6 GB to Windows
   processors=16      # all 16 threads
   swap=16GB
   ```
   **Parallel jobs**: WebCore's unified sources take ~1.5–2.5 GB per GCC job, so memory, not the
   cores, is the limit: build with **`ninja -j12`** (or `-j10` if the build gets killed — an "out of
   memory" kill in `dmesg`), keep `-j16` for the small builds (Onyx itself, the ports). Expect a full
   WebCore build in roughly 1–2 h the first time, minutes afterwards with ccache.
2. **Work on the Linux filesystem, never under `/mnt/c/...`** (Windows files from WSL are 5–10× slower
   to build): e.g. `~/src/Onyx`. Keep ~60 GB free there (WebKit sources + build trees).
3. **Packages** (Ubuntu 22.04/24.04; on 25.04 and later, 26.04 included, `qemu-user-static` is gone:
   install **`qemu-user`** instead — it holds the static binaries, as `qemu-aarch64`, which the
   benches accept):
   ```sh
   sudo apt update
   sudo apt install -y build-essential git curl xz-utils bzip2 cmake ninja-build ccache \
        python3 python3-pip perl ruby gperf unifdef bison flex texinfo pkg-config \
        qemu-user-static unzip zip file
   ```
   (`ruby` runs WebKit's offlineasm; `gperf`, `perl`, `python3` its generators; `qemu-user-static`
   / `qemu-user` the PC benches.) Optional for the docs exports: `pip install pypandoc` + `sudo apt install pandoc`.
4. **ccache** for fast rebuilds: `ccache -M 30G`; the WebKit build script and CMake toolchain file use
   it when `CCACHE=1` / `-DCMAKE_C_COMPILER_LAUNCHER=ccache` (add it if the script does not).

---

## 2. Get the code (once)

```sh
mkdir -p ~/src && cd ~/src
git clone --recurse-submodules https://github.com/stephaneweg/Onyx.git
git clone https://github.com/stephaneweg/onyx-packages.git      # beside Onyx: publish.sh uses ../onyx-packages
cd Onyx
git submodule update --init --recursive                          # circle/ = the fork stephaneweg/circle, branch onyx
```
(If the user's PC already has a clone under Windows, still make a fresh one inside WSL for building.)

---

## 3. The two toolchains

| Toolchain | Used for | Install |
|---|---|---|
| `aarch64-none-elf` (Arm GNU Toolchain 14.2.rel1) | the kernel and the existing apps / `/bin` tools | download Arm's `arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf.tar.xz` from developer.arm.com, extract into `/opt/toolchains/` |
| **`aarch64-onyx-elf`** (GCC 14.2 + binutils 2.43 + newlib 4.4; posix threads, native TLS; Onyx's own) | the POSIX layer's programs, the ports (SQLite, curl, ICU, HarfBuzz, Skia…) and **WebKit** | `sudo mkdir -p /opt/toolchains && sudo chown $USER /opt/toolchains && sh tools/toolchain/fetch.sh` (clones `stephaneweg/onyx-toolchain`, checks the sha256, ~1 min). To rebuild it from source instead: `sh tools/toolchain/build-onyx-toolchain.sh` (~10–15 min on an i7) |

```sh
cd /opt/toolchains
curl -LO https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf.tar.xz
tar xf arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf.tar.xz && rm arm-gnu-toolchain-*.tar.xz
cd ~/src/Onyx && sh tools/toolchain/fetch.sh
export PATH=/opt/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin:$PATH
aarch64-onyx-elf-gcc -v 2>&1 | grep "Thread model"     # -> posix (it is found under /opt/toolchains by the scripts)
```

---

## 4. Build Onyx and check the benches (once, to know the setup is right)

```sh
cd ~/src/Onyx/circle && ./configure -r 4 -p aarch64-none-elf- -d DEPTH=32 -f
for d in lib lib/sched lib/fs lib/fs/fat lib/usb lib/input lib/net lib/sound addon/SDCard addon/fatfs addon/wlan; do make -C $d -j$(nproc) || break; done
(cd addon/wlan/hostap/wpa_supplicant && make -f Makefile.circle -j$(nproc))
cd ../kernel && (make -j$(nproc) || make -j1) && make stage     # the kernel + every app -> sdcard/
cd .. && make -C user/BinUtils ports                                  # SQLite, libxml2, curl, ICU, HarfBuzz, Skia tools
sh tools/tests/posixsim/run.sh                                   # posixtest under qemu: all pass expected
sh tools/tests/posixsim/tc.sh                                    # + posixtest-cxx (the onyx-elf toolchain)
sh tools/tests/posixsim/ports.sh webkit                          # icutest, hbtest, skiatest
for t in run_ipc_test.sh run_ofile_test.sh run_ramfs_test.sh run_circlenet_test.sh; do sh tools/tests/$t; done
./tools/el0scan.sh sdcard/apps sdcard/bin sdcard/koton           # only gcemu (PMU, gated) and stkpoc (old binary) are expected
```
(`-j1` retry: a known link race. Build `make stage` only when the user needs a card.)

---

## 5. The WebKit port — the plan and where you take over

**Goal**: WebKit2 (multi-process: UI, web, network processes) replaces Jet (NetSurf) as Onyx's browser;
a port `PLATFORM(ONYX)` on the model of WebKit's **PlayStation** port (no GLib, curl networking with
mbedTLS, Skia CPU raster, our own platform layer), static binaries, JavaScriptCore without JIT first
(C_LOOP, then LLInt asm), the browser UI taken from Jet. Licence LGPL-2.1+.

**Steps** (details in `docs/08-WEBKIT-PORT.md`):
1. **WTF + JavaScriptCore → a `jsc` shell** — *being done by the cloud session on branch
   `webkit-port-step1`*. When you start: `git fetch origin && git checkout webkit-port-step1` (or
   `main` if it was merged), read `docs/08-WEBKIT-PORT.md`'s status, and continue from there.
2. **WebCore** — DOM, CSS, layout, rendering with Skia, fonts through `SkFontMgr_onyx`
   (`tools/ports/skia/`), ICU, networking through curl (+ mbedTLS: adapt WebKit's
   `CurlSSLVerifier`/`CurlSSLHandle`), storage on SQLite, woff2 (to port). **This is where the i7
   pays off: WebCore is the big build.**
3. **WebKit2** — the process launcher and IPC on Onyx's v76 IPC (`socketpair(AF_UNIX,
   SOCK_SEQPACKET)`, `sendmsg`/`recvmsg` with `SCM_RIGHTS`, `memfd_create` + `mmap(MAP_SHARED)`,
   `posix_spawn` with inherited fds: all validated on the Pi), the UI/web/network processes.
4. **The Onyx parts** — the web view in an Onyx window (canvas, `present`, key/mouse events, clipboard
   `clipd`, menus, cursors), a compositor on the V3D (`user/gpucomp`, as Jet does), a media player on
   `user/av` (FFmpeg, dav1d, libvpx; MSE).
5. **The browser** — Jet's UI (toolbar, site versions, zoom, downloads, history, padlock: `user/netsurf/
   onyx_*.c*` is the reference), then parity with Jet (`docs/07-BROWSER-GAPS.md` as the checklist), then
   the switch.
Kernel work that will come up: a streaming ELF loader (a static WebKit is 60–100 MB), shared code
pages between processes of the same ELF (§13), PROT_EXEC for LLInt asm / later the JIT, a malloc
that returns memory (dlmalloc/mimalloc on `vm_map`).

**How WebKit is handled in the repo**: **WebKit is not vendored**. `tools/webkit/fetch.sh` does a
pinned, sparse, shallow checkout outside the Onyx tree and applies **Onyx's patch series**
(`tools/webkit/patches/*.patch`); `tools/webkit/build-*.sh` build it with `tools/onyx-toolchain.cmake`
and the `aarch64-onyx-elf` toolchain. Any change to WebKit = a patch in that series (with a header:
what and why), never a fork pushed elsewhere. Onyx-new files inside WebKit's tree follow WebKit's
conventions (its BSD-2 header, "Onyx contributors"); Onyx-side scripts carry the MIT notice.

---

## 6. How to work (what made the cloud sessions fast)

- **Split the work into packages that do not touch the same files** and run **sub-agents in parallel,
  each in its own git worktree** (e.g. WebCore networking / WebCore rendering+fonts / WebKit2 IPC +
  launcher / the media player), then merge them in a fixed order. Give each agent a precise brief:
  the docs to read, its files, what not to touch, how to build and test, and "commit in your worktree
  branch, do not push". Clean the worktrees once merged (after checking they are merged).
- **Write a spec first** when several agents must agree on an interface (as `docs/POSIX-PLAN.md` did for
  the kapi v75/v76 blocks) — the ABI is **append-only** (`kernel/include/kern/kapi_abi.h`), slots
  numbered in blocks, `user/BinUtils/kapi_names.h` regenerated with `python3 tools/gen_kapi_names.py`.
- **Test on the PC before the Pi**: host unit tests (`tools/tests/run_*.sh`), the qemu bench
  (`tools/tests/posixsim/`: a fake kapi table + qemu-aarch64-static), `el0scan.sh`. Then stage the card
  and give the user a short test plan (they paste back only the FAIL lines).
- **Push your working branch regularly** (each milestone) so nothing is lost.
- After a round validated on the Pi: merge into `main`, publish the packages, update `docs/HANDOFF.md`.

---

## 7. Known open items (not WebKit, for context)

In `docs/HANDOFF.md`: performance after EL0 (the GameCube emulator and Doom slower — `key_held`
polled as a system call: to move to a read-only input-state page; small `memcpy` fast path), the PDF
Viewer's flicker when scrolling, memory balance on 1 GB Pis, symlinks on FAT, fork/vfork, the
self-hosted toolchain, Mesa (OpenGL ES / Vulkan on the V3D). Don't start them unless the user asks.
