# Onyx: the WebKit port

## Status / how to resume (2026-10-02)

**Step 1 (WTF + JavaScriptCore → the `jsc` shell) is done: it passes on the PC bench and on the
Pi** (the five steps of the Pi test below), and is in `main`. Step 2 (WebCore) is next, on a branch
`webkit-port`. `/bin/jsc` (the LLInt and WebAssembly's interpreter, no JIT; 33.1 MB with ICU's data) is
staged in
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
  `build-jsc.sh` (configure + build `jsc`; `INTERP=llint` (default: the LLInt, with WebAssembly) or
  `INTERP=cloop` (the C++ interpreter, without), each in its own build tree
  `<WEBKIT_DIR>-build/jsc-<interp>`; `install` strips it into `user/bin/jsc.elf`),
  `test-jsc.sh` (the tests on the posixsim bench, below), `smoke.js`, `bench.js`.
  `make -C user/bin jsc` = `fetch.sh` + `build-jsc.sh install`.
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
  6. `0006` — **WebAssembly** in its in-place interpreter (IPInt), and the LLInt as the port's default:
     `ENABLE_C_LOOP` OFF, `ENABLE_WEBASSEMBLY` ON, the BBQ / OMG JIT tiers OFF; the fault handler
     and "fast" memories off for `OS(ONYX)` (explicit bounds checks), `collectContinuously` forced off.
  7. `0007` — WebAssembly exceptions caught by WebAssembly code in a build without the JIT
     (`genericUnwind` had no catch routine for an IPInt handler when `ENABLE(JIT)` is off: every
     `catch` / `try_table` ended the process). Not specific to Onyx: worth sending upstream.
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
  have their own VM), and no `--collectContinuously` (a debugging option whose own thread asks for
  collections while the mutator may have no access: forced off). A kernel call "suspend this
  thread and give me its registers" would lift both (a kapi addition: not needed so far).
- **WebAssembly without faults.** JavaScriptCore normally lets an out-of-bounds access fault in a
  guard region and recovers in a signal handler. Not possible on Onyx: memories are bounds-checked
  explicitly (an option JavaScriptCore has). What that costs: **no shared memories** (the parser
  ties them to the fault handler: "shared memory is not enabled") — so no WebAssembly threads —
  and **no SIMD** (the parser only has it with the B3 JIT). Everything else of WebAssembly runs
  in IPInt: MVP, exceptions, tail calls, GC, references, multi-memory, memory64.

### Tests (the PC bench: `sh tools/webkit/test-jsc.sh`, qemu-user on `tools/tests/posixsim`)

`jsc` is relinked against the bench's libonyxposix + fake kapi and run under qemu.
`INTERP=llint|cloop`, the suites `smoke es6 stress bench`, `STRESS_STEP=1` for every stress test.

| Suite | LLInt (+ WebAssembly) | C_LOOP |
|---|---|---|
| `smoke` (`smoke.js`: 34 checks with WebAssembly, 29 without — the language, ICU, the collector, WebAssembly, microtasks and timers; `jsc -e`; exit 3 on an exception) | pass | pass |
| `es6` (WebKit's `JSTests/es6`, 605 tests: 595 expected to pass, 10 to fail) | 595 + 10 as expected | 595 + 10 as expected |
| `stress` (WebKit's `JSTests/stress`: the 4690 tests the harness runs with its default options) | 4688 pass, 2 fail as expected | 4686 pass, 4 fail as expected |
| `wasm` (WebKit's `JSTests/wasm`: the directories of its "WebAssembly suite", 611 tests after the ones with a `//@ skip` or a run command of their own) | 594 pass, 17 fail as expected | — (no WebAssembly) |

The expected failures, each with its reason in `test-jsc.sh`: in `stress`,
`intl-relativetimeformat.js` and `string-localeCompare.js` (Swahili and Ewe are not in the filtered
ICU data, `tools/ports/icu/data-filter.json`), and with C_LOOP the two tests that use WebAssembly;
in `wasm`, 5 tests that need SIMD and 12 that need shared or "signaling" memories (above). The
harness's options and time zone are reproduced (`--maxPerThreadStackUsage=1572864`,
`TZ=US/Pacific`...). The stress tests with other `//@` directives (JIT tiers, special options:
about 1150) are not run.

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
time), is the faster, and is the one WebAssembly works with. `el0scan` on the unstripped `jsc`: clean (libgcc's guarded SME helpers
only, as every program of the toolchain).

### The Pi test (2026-10-02: the five steps pass)

On a card made from the branch's `sdcard/` (kernel v76 or later), in the Terminal:

1. `jsc -e "print(6*7)"` → `42`.
2. `jsc SD:/docs/jsc/smoke.js` → the last line is `smoke: ok (34 checks)` (WebAssembly included).
3. `jsc SD:/docs/jsc/bench.js` → nine lines of timings (send them back: the first real numbers).
4. `jsc` alone → a `>>>` prompt: `1+1`, `new Intl.DateTimeFormat("fr-FR", {dateStyle: "full"}).format(new Date())`,
   `quit()`.
5. `jsc -e "a=[];for(i=0;i!=300000;i++)a.push({i});setTimeout(function(){print('late')},2000)"`
   → `late` after two seconds (the collector while the shell waits). No `<`, `>`, `|` and no space
   in the script: Onyx's shell (`user/bin/cmd.c`) has no quoting — they are redirections and
   separators even between quotes (a first version of this step, with `i < 300000` and arrow
   functions, answered "cannot open input file" without starting `jsc`).
6. Memory: the Task Manager's figure for `jsc` while step 3 runs.
`bench.js` on the Pi 4 (the LLInt, 2026-10-02) — 2 to 6 times the PC's qemu figures above:

| | Pi 4 |
|---|---|
| `fib(30)` | 1852 ms |
| 20M additions in a loop | 5595 ms |
| 1M object allocations | 2085 ms |
| 300k string concat / split | 2307 ms |
| sort 300k numbers | 4896 ms |
| regexp, 100k matches | 1330 ms |
| Map, 500k set / get | 4001 ms |
| 5M typed-array doubles | 17357 ms |
| JSON, 20 × 200 KB | 1609 ms |

What can differ from the bench: the loader on a 33.1 MB image, the kernel's `vm_*` under the
collector's reservations, the main thread's stack size, timing.

### Resume

```sh
sudo apt-get install -y gperf unifdef cmake ninja-build ruby ccache   # host tools (docs/LOCAL-AGENT-WEBKIT.md)
sh tools/toolchain/fetch.sh                                 # aarch64-onyx-elf into /opt/toolchains
make -C user/libc/posix install PREFIX=aarch64-onyx-elf-   # the sysroot (out/sysroot-onyx)
sh tools/ports/build-all.sh icu                             # ICU into the sysroot (webkit: all of them)
sh tools/webkit/fetch.sh                                    # the checkout at the pin + the patches
sh tools/webkit/build-jsc.sh install                        # -> user/bin/jsc.elf (7 min on 16 cores)
STRESS_STEP=1 sh tools/webkit/test-jsc.sh smoke es6 stress wasm bench   # 20 min
# a change: commit in the checkout (branch onyx), then: sh tools/webkit/export-patches.sh
```
(`ninja -C <build> -k 0 jsc` lists many errors at once. WebKit's CMake uses ccache when it is
installed.)

### Next steps

1. Publish the package `jsc` if it is not yet (`tools/pkg/packages.ini` declares it: `bin/jsc`,
   `docs/jsc/`; `sh tools/pkg/publish.sh`, the user's signing key).
2. Step 2, WebCore, on a branch `webkit-port` made from `main`: widen the checkout
   (`SPARSE_EXTRA="Source/WebCore Source/ThirdParty/..."`), `PLATFORM(ONYX)` on the PlayStation
   port's model, Skia from the sysroot, curl + mbedTLS, libxml2, SQLite, woff2 (to port).
3. For later, in the kernel: PROT_EXEC for a JIT, a call to suspend a thread (above), a malloc that
   returns memory, a streaming ELF loader and shared code pages (a static WebKit is 60–100 MB).

## The plan of the whole port

WebKit replaces Jet (NetSurf) as Onyx's browser engine (docs/POSIX-PLAN.md §8, §9, §15: WebKit2 on
the PlayStation port's model, static binaries, distributed under LGPL-2.1+):

1. **WTF + JavaScriptCore** → the `jsc` shell (the LLInt without JIT, WebAssembly in its
   interpreter; C_LOOP as an alternative). *Done: validated on the Pi, 2026-10-02.*
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
