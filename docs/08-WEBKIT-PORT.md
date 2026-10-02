# Onyx: the WebKit port

## Status / how to resume (2026-10-02, branch `webkit-port-step1`, work in progress)

**Pinned WebKit revision:** `b8a7a626127c0010a557c9d6466fefd38d9477c1` (WebKit `main`, 2026-10-02;
its `Source/ThirdParty/skia` is Skia m154 `588b550a`, the copy already ported into the sysroot:
`third_party/skia-m154/README.onyx`). Set in `tools/webkit/revision.sh`.

**Done:**
- Prerequisites (commit "libonyxposix: <uchar.h>, onyx-cc.specs"):
  - `<uchar.h>` for newlib in libonyxposix's overlay (`user/libc/posix/include/uchar.h`, `uchar.c`:
    `mbrtoc16`/`c16rtomb`/`mbrtoc32`/`c32rtomb`/`mbrtoc8`/`c8rtomb`, UTF-8 in every locale), checked on
    the posixsim bench with both toolchains.
  - `__unix__` / `__unix` / `__onyx__` defined and `-pthread` accepted **without a GCC rebuild**: the
    sysroot's new `lib/onyx-cc.specs` (given to every compile by `tools/onyx-toolchain.cmake` and
    `tools/onyx-env.sh`) and `lib/onyx.specs` (the link). GCC's driver accepts an unknown option only
    when a `-specs=` file names it (a driver `specs` file in the toolchain's lib dir does define
    `__unix__` but cannot validate `-pthread`; a "Driver" option in a target `.opt` would need a GCC
    rebuild — not done, not needed).
  - gperf: `apt-get install gperf` (3.1) on the build machine; unifdef likewise (`USE_SYSTEM_UNIFDEF`).
- `tools/webkit/fetch.sh` (sparse, shallow checkout of the pinned revision outside the repo —
  default `/home/user/webkit`, else `$HOME/webkit`, `WEBKIT_DIR=` to choose — about 280 MB: the
  top-level CMake files, `Source/cmake`, `Source/WTF`, `Source/JavaScriptCore`, `Source/bmalloc`,
  `Tools/Scripts/webkitperl`, `JSTests/stress`, `JSTests/es6`; then `git am` of the patch series on a
  branch `onyx`), `tools/webkit/export-patches.sh` (the branch `onyx` → `tools/webkit/patches/`),
  `tools/webkit/build-jsc.sh` (configure + build `jsc`; `INTERP=llint` for the LLInt; `install` strips
  it into `user/bin/jsc.elf`).
- The port (patch `0001`, WIP): `Source/cmake/OptionsOnyx.cmake` (PORT=Onyx; static libraries;
  `ENABLE_JIT` OFF, `ENABLE_C_LOOP` ON, WebAssembly / sampling profiler / remote inspector OFF,
  `USE_SYSTEM_MALLOC` ON, no mimalloc / IsoMalloc, `USE_64KB_PAGE_BLOCK`, the generic event loop),
  Onyx in `WebKitCommon.cmake` (ports list; `CMAKE_SYSTEM_NAME Onyx` → `WTF_OS_ONYX` + `WTF_OS_UNIX`),
  `OS(ONYX)` in `wtf/PlatformOS.h` (from `__onyx__`, a Unix), `CeilingOnPageSize` 64 KB for `OS(ONYX)`
  (`wtf/PageBlock.h`), `WTF_PLATFORM_ONYX=1`, `Source/WTF/wtf/PlatformOnyx.cmake` (POSIX + Unix
  sources, `RunLoopGeneric`, `WorkQueueGeneric`, `MainThreadGeneric`, `MemoryPressureHandlerUnix`),
  `wtf/onyx/CurrentProcessMemoryStatus{.h,Onyx.cpp}` and `MemoryFootprintOnyx.cpp` (libonyxposix's
  `getrusage`: `ru_maxrss` is the current resident size from `vm_stats`),
  `Source/bmalloc/PlatformOnyx.cmake` (libpas sources dropped: never used with the system malloc, and
  `pas_lock.h` has no Onyx lock), `Source/JavaScriptCore/PlatformOnyx.cmake`. New WebKit-side files
  carry WebKit's usual BSD-2 header with "Onyx contributors"; WebKit's LGPL / BSD notices untouched.

**Current state of the build:** CMake configures (`PORT=Onyx`, ICU 78.3 found in the sysroot);
bmalloc compiles; **WTF stops at its first files**:
```
Source/WTF/wtf/PlatformRegisters.h:43:10: fatal error: sys/ucontext.h: No such file or directory
```
(newlib has no `<sys/ucontext.h>`). Nothing past that has been tried yet.

**Resume:**
```sh
sudo apt-get install -y gperf unifdef            # host tools (+ cmake ninja perl python3 ruby)
make -C user/libc/posix install PREFIX=aarch64-onyx-elf-   # the sysroot with uchar.h / onyx-cc.specs
sh tools/ports/build-all.sh webkit               # ICU etc. into out/sysroot-onyx (if not there)
sh tools/webkit/fetch.sh                         # /home/user/webkit at the pin + the patches
sh tools/webkit/build-jsc.sh                     # -> /home/user/webkit-build/jsc-cloop/bin/jsc
# fix, commit in /home/user/webkit (branch onyx), then: sh tools/webkit/export-patches.sh
```
(`ninja -C /home/user/webkit-build/jsc-cloop -k 0 jsc` lists many errors at once.)

**Next steps:**
1. `wtf/PlatformRegisters.h`: for `OS(ONYX)`, no `sys/ucontext.h` — either a minimal
   `<sys/ucontext.h>` in libonyxposix's overlay (`mcontext_t` / `ucontext_t` for AArch64, the Linux
   layout: `fault_address`, `regs[31]`, `sp`, `pc`, `pstate`, reserved) or an `OS(ONYX)` branch with a
   stub `PlatformRegisters` (JSC only uses it for thread suspension / sampling, off here). The overlay
   header is the more useful (JSC's `MachineContext.h` has per-OS branches: add `OS(ONYX)` like Linux).
2. Continue the WTF compile: the `OS(HAIKU)` sites listed by
   `grep -rn 'OS(HAIKU)' Source/WTF Source/JavaScriptCore` are the checklist (StackBounds via
   `pthread_getattr_np`, `NumberOfCores`, `RAMSize`, `CurrentTime`, `ThreadingPOSIX` thread
   priorities (stubs), `FileSystemPOSIX`, `OSAllocatorPOSIX`, `MemoryPressureHandler.h`,
   `ProcessMemoryStatus.h` (add `OS(ONYX)`), `InlineASM.h` (ELF symbol directives), `RandomDevice`
   (`getrandom`), `Language` (`LANG`).
3. JavaScriptCore with C_LOOP, then link `jsc`; run it on the posixsim bench (relink with
   `tools/tests/posixsim/run.sh` `PROG=none OBJS=...`); then the tests and the benchmark of the task
   (scripts, a JSTests/stress subset, timings), then `INTERP=llint` (`USE_64KB_PAGE_BLOCK` makes
   LLInt-without-JIT the default on ARM64 upstream: the LLInt is assembled at build time, no
   executable memory needed — to be confirmed).
4. `user/bin` target to build/stage `/bin/jsc`, docs/04 entry, docs/LICENSING.md row (WebKit
   LGPL-2.1+/BSD-2, the patches published), CLAUDE.md docs list.

## The plan of the whole port

WebKit replaces Jet (NetSurf) as Onyx's browser engine (docs/POSIX-PLAN.md §8, §9, §15: WebKit2 on
the PlayStation port's model, static binaries, distributed under LGPL-2.1+):

1. **WTF + JavaScriptCore** → the `jsc` shell (C_LOOP, then LLInt without JIT). *In progress.*
2. **WebCore** (Skia CPU raster from WebKit's own copy, `SkFontMgr_onyx` instead of fontconfig,
   curl + mbedTLS networking, ICU, HarfBuzz, libxml2, SQLite, woff2).
3. **WebKit2** (UI, web and network processes over WP-IPC: AF_UNIX socketpairs, SCM_RIGHTS, shm).
4. The Onyx view, compositor (later the V3D) and media (`MediaPlayerPrivate` on `user/av`).
5. The browser (Jet's UI reused).
6. Parity with Jet, then the switch.

## The patch series

`tools/webkit/patches/*.patch` (git format-patch; each patch's message says what and why), applied
by `tools/webkit/fetch.sh` with `git am` onto the pinned revision. WebKit is not vendored in the Onyx
repository.
