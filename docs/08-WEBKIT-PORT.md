# Onyx: the WebKit port

## Status / how to resume (2026-10-02, branch `webkit-port-step1`)

**Step 1 (WTF + JavaScriptCore → the `jsc` shell) is done on the PC bench and waits for its test
on the Pi.** `/bin/jsc` (the LLInt, no JIT; 31.7 MB with ICU's data) is staged in that branch's
`sdcard/`, with `SD:/docs/jsc/smoke.js` and `bench.js`.

**Pinned WebKit revision:** `b8a7a626127c0010a557c9d6466fefd38d9477c1` (WebKit `main`, 2026-10-02;
its `Source/ThirdParty/skia` is Skia m154 `588b550a`, the copy already ported into the sysroot:
`third_party/skia-m154/README.onyx`). Set in `tools/webkit/revision.sh`.

### What is there

- **The scripts** (`tools/webkit/`): `fetch.sh` (a sparse, shallow checkout of the pinned revision
  outside the repo — default `/home/user/webkit`, else `$HOME/webkit`, `WEBKIT_DIR=` to choose —
  about 280 MB: the top-level CMake files, `Source/cmake`, `Source/WTF`, `Source/JavaScriptCore`,
  `Source/bmalloc`, `Tools/Scripts/webkitperl`, `JSTests/stress`, `JSTests/es6`; then `git am` of the
  patch series on a branch `onyx`), `export-patches.sh` (the branch `onyx` → `tools/webkit/patches/`),
  `build-jsc.sh` (configure + build `jsc`; `INTERP=cloop` (default) or `INTERP=llint`, each in its own
  build tree `<WEBKIT_DIR>-build/jsc-<interp>`; `install` strips it into `user/bin/jsc.elf`),
  `test-jsc.sh` (the tests on the posixsim bench, below), `smoke.js`, `bench.js`.
  `make -C user/bin jsc` = `fetch.sh` + `INTERP=llint build-jsc.sh install`.
- **The port** (the patch series, `tools/webkit/patches/`):
  1. `0001` — `PORT=Onyx`: `Source/cmake/OptionsOnyx.cmake` (static libraries; `ENABLE_JIT` OFF,
     `ENABLE_C_LOOP` ON by default, WebAssembly / sampling profiler / remote inspector OFF,
     `USE_SYSTEM_MALLOC` ON, no mimalloc / IsoMalloc, `USE_64KB_PAGE_BLOCK`, the generic event loop),
     Onyx in `WebKitCommon.cmake` (`CMAKE_SYSTEM_NAME Onyx` → `WTF_OS_ONYX` + `WTF_OS_UNIX`),
     `OS(ONYX)` in `wtf/PlatformOS.h` (from `__onyx__`, a Unix), `CeilingOnPageSize` 64 KB,
     `wtf/PlatformOnyx.cmake` (POSIX + Unix sources, `RunLoopGeneric`, `WorkQueueGeneric`,
     `MainThreadGeneric`, `MemoryPressureHandlerUnix`), `wtf/onyx/` (the process's memory status
     from libonyxposix's `getrusage`), `bmalloc/PlatformOnyx.cmake`, `JavaScriptCore/PlatformOnyx.cmake`.
  2. `0002` — WTF builds: `-D_GNU_SOURCE` (newlib hides its POSIX / GNU declarations from a strict
     `-std=c++NN` compile), **no machine context and no thread suspension** (see below), the signal
     handlers never installed, the memory-pressure hold-off timer and memory status for `OS(ONYX)`,
     the thread name through `pthread_setname_np(pthread_self(), name)`.
  3. `0003` — JavaScriptCore builds and `jsc` links: the bytecode cache version from the build's
     timestamp (no `<link.h>` in a static program), ICU's static archives in the order i18n, uc, data.
  4. `0004` — the **LLInt**: offlineasm's `globaladdr` by direct `adrp` / `add :lo12:` (static,
     non-PIC: no GOT), `OS(ONYX)` among `InlineASM.h`'s ELF platforms, `cacheFlush` through
     `__builtin___clear_cache` (not reached without a JIT).
  5. `0005` — the collector: the mutator **finishes the collections before it releases heap access**
     (see below).
  New WebKit-side files carry WebKit's BSD-2 header with "Onyx contributors"; WebKit's LGPL / BSD
  notices are untouched.
- **libonyxposix** (commits on the branch): `<uchar.h>`, `onyx-cc.specs` (`__unix__`, `-pthread`
  accepted), `MAP_FILE` in `<sys/mman.h>`.

### Two things Onyx does differently (and why they matter for the next steps)

- **No asynchronous signals, so no thread suspension.** On Onyx a signal handler only runs when the
  process raises the signal itself; a faulting process is ended by the kernel; newlib's
  `struct sigaction` has no `sa_sigaction`. WTF suspends a thread with a signal
  (`Thread::suspend`, `ThreadingPOSIX.cpp`) and JavaScriptCore uses that for three things: the
  sampling profiler and the JIT tiers' signal-based VM traps (both off here), and **the collector
  scanning the stack of a thread that has no heap access**. `Thread::suspend()` is a fatal error
  for `OS(ONYX)`; `PlatformRegisters` is the stack pointer only (`HAVE(MACHINE_CONTEXT)` off).
- **The collector never scans another thread's stack.** A thread with heap access scans its own
  stack (stopped at a safepoint, or conducting the collection itself). Without it — the VM's lock
  released, the thread back in its run loop — an unfinished collection goes on in the collector
  thread, which must suspend the thread to read its stack: on Onyx that ended the process as soon
  as a program allocated, then waited (`setTimeout` in `jsc`; any page in a browser). Patch `0005`:
  `Heap::releaseAccess()` first waits for the outstanding collections
  (`finishCollectionsBeforeReleasingAccess`). No collection starts while the mutator has no access
  (requests come from the thread holding the VM's lock). Cost: the end of a collection is a pause
  on the mutator when it goes idle; marking stays concurrent and parallel while it runs.
  **Limits: one thread per VM** (a VM shared between threads would need the suspension; workers
  have their own VM). A kernel call "suspend this thread and give me its registers" would lift
  both (a kapi addition: not needed so far).

### Tests (the PC bench: `sh tools/webkit/test-jsc.sh`, qemu-user on `tools/tests/posixsim`)

`jsc` is relinked against the bench's libonyxposix + fake kapi and run under qemu.
`INTERP=llint|cloop`, the suites `smoke es6 stress bench`, `STRESS_STEP=1` for every stress test.

| Suite | LLInt | C_LOOP |
|---|---|---|
| `smoke` (`smoke.js`: 29 checks — the language, ICU, the collector, microtasks; `jsc -e`; exit 3 on an exception) | pass | pass |
| `es6` (WebKit's `JSTests/es6`, 605 tests: 595 expected to pass, 10 to fail) | 595 + 10 as expected | 595 + 10 as expected |
| `stress` (WebKit's `JSTests/stress`: the 4690 tests the harness runs with its default options) | 4686 pass, 4 fail as expected | 4686 pass, 4 fail as expected |

The 4 expected stress failures: `intl-relativetimeformat.js` and `string-localeCompare.js` (Swahili
and Ewe are not in the filtered ICU data, `tools/ports/icu/data-filter.json`), `structured-clone.js`
and `wasm-gc-structureid-cast-optimization.js` (WebAssembly is not built). The harness's options
and time zone are reproduced (`--maxPerThreadStackUsage=1572864`, `TZ=US/Pacific`...). The stress
tests with other `//@` directives (JIT tiers, special options: about 1150) are not run.

`bench.js` under qemu on the PC (not the Pi's timings; qemu flatters the C++ loop):

| | LLInt | C_LOOP |
|---|---|---|
| `fib(30)` | 737 ms | 1062 ms |
| 20M additions in a loop | 2659 ms | 3722 ms |
| 1M object allocations | 683 ms | 817 ms |
| sort 300k numbers | 1599 ms | 2417 ms |
| 5M typed-array doubles | 6142 ms | 7550 ms |
| regexp, 100k matches | 359 ms | 373 ms |

The LLInt is the one staged: it needs no executable memory at run time (it is assembled at build
time) and is the faster. `el0scan` on the unstripped `jsc`: clean (libgcc's guarded SME helpers
only, as every program of the toolchain).

### The Pi test (to do: the user)

On a card made from the branch's `sdcard/` (kernel v76 or later), in the Terminal:

1. `jsc -e "print(6*7)"` → `42`.
2. `jsc SD:/docs/jsc/smoke.js` → the last line is `smoke: ok (29 checks)`.
3. `jsc SD:/docs/jsc/bench.js` → nine lines of timings (send them back: the first real numbers).
4. `jsc` alone → a `>>>` prompt: `1+1`, `new Intl.DateTimeFormat("fr-FR", {dateStyle: "full"}).format(new Date())`,
   `quit()`.
5. `jsc -e "setTimeout(() => print('late'), 2000); let a = []; for (let i = 0; i < 300000; i++) a.push({i})"`
   → `late` after two seconds (the collector while the shell waits).
6. Memory: the Task Manager's figure for `jsc` while step 3 runs.
What can differ from the bench: the loader on a 31.7 MB image, the kernel's `vm_*` under the
collector's reservations, the main thread's stack size, timing.

### Resume

```sh
sudo apt-get install -y gperf unifdef cmake ninja-build ruby ccache   # host tools (docs/LOCAL-AGENT-WEBKIT.md)
sh tools/toolchain/fetch.sh                                 # aarch64-onyx-elf into /opt/toolchains
make -C user/libc/posix install PREFIX=aarch64-onyx-elf-   # the sysroot (out/sysroot-onyx)
sh tools/ports/build-all.sh icu                             # ICU into the sysroot (webkit: all of them)
sh tools/webkit/fetch.sh                                    # the checkout at the pin + the patches
INTERP=llint sh tools/webkit/build-jsc.sh install           # -> user/bin/jsc.elf (6 min on 16 cores)
INTERP=llint STRESS_STEP=1 sh tools/webkit/test-jsc.sh smoke es6 stress bench
# a change: commit in the checkout (branch onyx), then: sh tools/webkit/export-patches.sh
```
(`ninja -C <build> -k 0 jsc` lists many errors at once. WebKit's CMake uses ccache when it is
installed.)

### Next steps

1. The Pi test above; then merge `webkit-port-step1` into `main` and publish the package `jsc`
   (`tools/pkg/packages.ini` declares it: `bin/jsc`, `docs/jsc/`).
2. Step 2, WebCore, on a branch `webkit-port` made from this one: widen the checkout
   (`SPARSE_EXTRA="Source/WebCore Source/ThirdParty/..."`), `PLATFORM(ONYX)` on the PlayStation
   port's model, Skia from the sysroot, curl + mbedTLS, libxml2, SQLite, woff2 (to port).
3. For later, in the kernel: PROT_EXEC for a JIT, a call to suspend a thread (above), a malloc that
   returns memory, a streaming ELF loader and shared code pages (a static WebKit is 60–100 MB).

## The plan of the whole port

WebKit replaces Jet (NetSurf) as Onyx's browser engine (docs/POSIX-PLAN.md §8, §9, §15: WebKit2 on
the PlayStation port's model, static binaries, distributed under LGPL-2.1+):

1. **WTF + JavaScriptCore** → the `jsc` shell (C_LOOP, and the LLInt without JIT). *Done on the
   bench; the Pi test is pending.*
2. **WebCore** (Skia CPU raster from WebKit's own copy, `SkFontMgr_onyx` instead of fontconfig,
   curl + mbedTLS networking, ICU, HarfBuzz, libxml2, SQLite, woff2).
3. **WebKit2** (UI, web and network processes over WP-IPC: AF_UNIX socketpairs, SCM_RIGHTS, shm).
4. The Onyx view, compositor (later the V3D) and media (`MediaPlayerPrivate` on `user/av`).
5. The browser (Jet's UI reused).
6. Parity with Jet, then the switch.

## The patch series

`tools/webkit/patches/*.patch` (git format-patch; each patch's message says what and why), applied
by `tools/webkit/fetch.sh` with `git am` onto the pinned revision. WebKit is not vendored in the Onyx
repository. `/bin/jsc` is distributed under LGPL-2.1+ (docs/LICENSING.md): the pinned revision, this
series and `build-jsc.sh` are its complete corresponding source.
