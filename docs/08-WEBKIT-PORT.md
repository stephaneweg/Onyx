# Onyx: the WebKit port

## Status / how to resume (2026-10-02)

**Step 1 (WTF + JavaScriptCore → the `jsc` shell) is done: it passes on the PC bench and on the
Pi** (the five steps of the Pi test below), and is in `main`. Step 2 (WebCore) is next, on a branch
`webkit-port`. `/bin/jsc` (the LLInt and WebAssembly's interpreter, no JIT; 33.1 MB with ICU's data) is
staged in `sdcard/`, with `SD:/docs/jsc/smoke.js` and `bench.js`.

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
   → `late` after two seconds (the collector while the shell waits; on the Pi 3 to 4 seconds in
   all: the 33 MB image loaded from the card, JavaScriptCore's start, the loop, then the timer's
   two seconds). Written without `<`, `>`, `|` or spaces because the shell had no quoting
   then (a first version, with `i < 300000` and arrow functions, answered "cannot open input
   file" without starting `jsc`); since 2026-10-02 `/bin/cmd` honours `"…"`, `'…'` and `\`
   (docs/04 §7 *Quotes and escapes*), so
   `jsc -e "let a = []; for (let i = 0; i < 300000; i++) a.push({i}); setTimeout(() => print('late'), 2000)"`
   works as well.
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

1. Step 2, WebCore (below), on the branch `webkit-port`. The package `jsc` 1.0.0 is published.
2. For later, in the kernel: PROT_EXEC for a JIT, a call to suspend a thread (above), a malloc that
   returns memory, a streaming ELF loader and shared code pages (a static WebKit is 60–100 MB).

## Step 2: WebCore (2026-10-02, branch `webkit-port`)

**Where it stands: WebCore builds for Onyx, links, and renders a page — on the PC bench and on
the Pi (2026-10-02).** On the Pi 4, `wctest SD:/docs/page1.html RAM:/page1.png` (the 80 MB program
copied by hand to `SD:/bin`) printed the same title, element count and text as the bench — the
line written by the page's script included — and wrote the same picture (47 465 bytes, as on the
bench; opened in the Image Viewer): the kernel's loader takes an 80 MB static image, the card's
fonts are found, Skia paints on the CPU, JavaScriptCore runs inside WebCore. **5 to 6 seconds
from the command to the prompt** (the user's timing): most of it is probably the loader reading
and copying the 80 MB image from the card (`jsc`, 33 MB, takes 1 to 2 seconds before it runs
anything); WebCore's own work is one second under qemu on the PC. Not broken down yet: `wctest`
could print its own times. What it means for step 3: WebKit2 starts several processes of large
images — the streaming ELF loader and the code pages shared between processes of one executable
(docs/POSIX-PLAN.md §5, §13) will matter there.
`libPAL.a` and `libWebCore.a` build (`sh tools/webkit/build-webcore.sh`: 30 minutes on 16 cores
for a first build, 18 to 22 GB of memory at 14 jobs); `wctest` (`tools/webkit/wctest.cpp`, built by
`build-wctest.sh`: WebCore alone, the "empty" clients, no window and no network) loads an HTML
file, lays it out, paints it with Skia on the CPU into a PNG; `sh tools/webkit/test-webcore.sh`
runs it under qemu on `tools/webkit/tests/page1.html` and checks what it prints and the picture's
pixels: **21 checks pass** — the title and the text (UTF-8, the card's fonts through
`SkFontMgr_onyx`), CSS boxes, flexbox, grid, a gradient with rounded corners, an inline SVG, form
controls (WebKit's Adwaita theme), a script that changes the DOM and draws into a canvas
(JavaScriptCore inside WebCore). One second under qemu. The program is 80 MB (58 MB of code, 22 MB
of data with ICU's): not staged on the card (too big for the repository) — for a Pi test, strip
`<build>/bin/wctest` and copy it by hand.

Patches `0008`–`0011` (`tools/webkit/patches/`): `USE(SKIA_GPU)` (Skia's GL / Ganesh paths compiled
out: 24 files), the curl back end and PAL's digests on mbedTLS, WebCrypto's containers and stubs
for mbedTLS, and the port itself (`OptionsOnyx.cmake` with `ONYX_WEBCORE`, `PlatformOnyx.cmake`,
the Onyx files). What was found on the way, beyond the study below:
- **The theme is WebKit's own Adwaita** (`USE_THEME_ADWAITA`, as WPE, GTK and Windows): drawn form
  controls and scrollbars with no GLib and nothing from the system. No Onyx theme was written.
- **The libwpe pasteboard and editor files call no libwpe function**: Onyx shares them
  (`PLATFORM(ONYX)` beside `USE(LIBWPE)`); the real clipboard is behind the `PasteboardStrategy`
  (step 3, on `clipd`).
- WebCore needs a `LoaderStrategy` even to make a page (`Page::firstTimeInitialization`); the
  "empty clients" page is fully sandboxed (scripts refused) until its sandbox flags are cleared:
  both are in `wctest.cpp`, and are WebKit2's job in step 3.
- The link asked for 36 symbols in all (pasteboard, editor, WebCrypto, one keyboard function).

**Not done / not tested yet in step 2**: the memory on the Pi was not measured, the time only as a whole;
**the network path is compiled, not exercised** (curl with
the mbedTLS glue of `MbedTLSHelper.cpp`: a page loaded over HTTPS comes with step 3's network
process, or a `wctest` with a real loader); **WebCrypto** is digests only (decision below);
the public suffix list is an interim rule (`PublicSuffixStoreOnyx.cpp`); WOFF2 fonts, video and
audio are off; `PlatformScreenOnyx` answers 1920 × 1080.

### The design (the study it started from)

**Decided (the user, 2026-10-02): the graphics, in this order** — (1) a browser rendered on the
**CPU, without GL** (Skia raster); (2) once that has worked well for a few days, a **compositor on
the V3D** (Onyx's own kernel driver, as Jet with `user/gpucomp`); (3) **WebGL is a goal for later**,
and its route is chosen: **an ANGLE back end of our own that drives Onyx's V3D kernel driver
directly, with only Mesa's shader compiler library (SPIR-V → NIR → QPU) behind it** — not a port of
the whole of Mesa (no Gallium `v3d` driver, no emulation of Linux's DRM interface, no Mesa EGL):
the fewest interfaces, the most direct path. (WebKit's WebGL is ANGLE, which translates GLSL to
another shader language, never to QPU code: Mesa has the only GLSL → QPU compiler. The GL state →
control lists driver is then ours to write. A software WebGL would need executable memory.) To
study before any code: ANGLE's back-end interface, Mesa's compiler built alone. Nothing done now
should close that door: GL is cut out of the build by a flag, not removed.

What a study of the pinned sources found (read, not compiled: verify when building):

- **No port builds WebCore without GL.** Windows, WPE / GTK and PlayStation all composite with GL
  (TextureMapper or Coordinated Graphics); `platform/graphics/skia/SkiaCompositingLayer*` is
  GL-only too. With `USE(SKIA)`, about ten files use Ganesh / GL whatever the configuration:
  `PlatformDisplaySkia.cpp`, `ImageBufferSkiaAcceleratedBackend.cpp`, `SkiaGPUAtlas.cpp`,
  `SkiaReplayAtlas.cpp`, `SkiaReplayCanvas.cpp` (whole files), and hunks of `GraphicsContextSkia.cpp`,
  `NativeImageSkia.cpp`, `ImageUtilitiesSkia.cpp`, `SkiaUtilities.cpp/.h`,
  `SkiaSerializedImageBuffer.cpp`, `SkiaRecordingResult.h`, `platform/graphics/ImageBuffer.cpp`,
  `PlatformDisplay.h/.cpp`. The run-time paths are already CPU-safe (`RenderingMode::Unaccelerated`):
  the problem is compiling and linking. **Chosen: a guard patch** (a flag such as `USE(SKIA_GPU)`,
  set by the other ports, unset by Onyx) rather than building Ganesh with stubbed GL. Onyx's Skia
  (`tools/ports/skia`) has no Ganesh.
- **Compositing compiled out**: none of `USE_TEXTURE_MAPPER`, `USE_COORDINATED_GRAPHICS`,
  `USE_GRAPHICS_LAYER_WC`, `USE_LIBEPOXY`, `USE_ANGLE_EGL`, `USE_LIBWPE`; `ENABLE_WEBGL`,
  `ENABLE_GPU_PROCESS`, `ENABLE_ASYNC_SCROLLING`, `ENABLE_VIDEO`, `ENABLE_WEB_AUDIO` OFF for the
  first build. Onyx supplies `GraphicsLayer::create()` (a `GraphicsLayerOnyx` with no-op
  `setNeedsDisplay*`: the only pure virtuals); pages paint through `LocalFrameView` into a plain
  `GraphicsContext` with `acceleratedCompositingEnabled` false at run time. WebKit2 has the
  matching path (`DrawingAreaCoordinatedGraphics::display` into a `ShareableBitmap`,
  `UIProcess/skia/BackingStoreSkia.cpp`): step 3.
- **CMake**: `Source/CMakeLists.txt` adds `ThirdParty/skia` when `USE_SKIA` (Onyx: an imported
  `Skia::Skia` from the sysroot, with a `<skia/...>` include directory — the sysroot has
  `include/skia/include/...`); `WebCore/CMakeLists.txt` links GLES / EGL for every port without
  epoxy or ANGLE (to skip for Onyx).
- **Fonts**: `FontCacheSkia.cpp` and `SkiaSystemFallbackFontCache.cpp` call fontconfig directly
  except for Android / Windows: Onyx takes that side of the guards with `SkFontMgr_New_Onyx`.
- **TLS**: the curl back end uses OpenSSL in `OpenSSLHelper.cpp` (the certificate chain and its
  summary), `CurlSSLVerifier.cpp`, `CertificateInfoCurl.cpp`, a few places of `CurlContext.cpp`.
  curl 8.16's mbedTLS back end already gives what they need (`CURLOPT_SSL_CTX_FUNCTION` with the
  `mbedtls_ssl_config *`, `CURLINFO_TLS_SSL_PTR`, CA blobs): **mbedTLS glue, about 600 lines**
  (docs/POSIX-PLAN.md §5.7's choice). Read the chain after the handshake rather than replacing
  curl's own verify callback.
- **WebCrypto cannot be switched off** (no option; its sources are unconditional) and its only
  portable back ends are OpenSSL (`crypto/openssl/`, 2700 lines) and gcrypt. **First build: a stub
  back end** (every `platform*` function answers NotSupported; SHA digests through
  `PAL::CryptoDigest` on mbedTLS). **Open decision for the user, later**: a real back end on mbedTLS
  (one crypto stack, about 3000 lines of ours to maintain) or OpenSSL's libcrypto ported for it
  (Apache-2.0, WebKit's code unchanged, a second crypto library on the card).
- **Others**: libpsl (public suffixes) is required by `Curl.cmake`: a `PublicSuffixStoreOnyx.cpp`
  first, the library (MIT) later — cookies and site isolation need a real list. `USE_WOFF2` OFF
  first (FreeType with brotli, or libwoff2, later). No libwpe: `PasteboardOnyx`,
  `PlatformPasteboardOnyx` (on `clipd`), `PlatformKeyboardEventOnyx` instead of the `libwpe/` files.
- **The per-port files** (the PlayStation ones are 40–160 lines each): accessibility (`AXObjectCache`,
  `AccessibilityObject`: no-ops), `SystemFontDatabase`, `CurlSSLHandle` (the CA bundle
  `SD:/res/ca-bundle`), `NetworkStateNotifier`, `MIMETypeRegistry`, `PlatformScreen`,
  `ScrollbarTheme`, `Theme`, `UserAgent`, `RenderTheme`; `PLATFORM(PLAYSTATION)` appears only 13
  times in WebCore (`RenderTheme.h`, `PlatformRenderTheme.h`, `AXCoreObject.h`, `ChromeClient.h`,
  `EmptyClients.h`...): an `ONYX` branch beside each that matters. PAL: `CryptoDigestMbedTLS.cpp`.

The order of work: `OptionsOnyx.cmake` (`ENABLE_WEBCORE`, the packages, `USE_SKIA`, `USE_CURL`,
`USE_HARFBUZZ`, `USE_MBEDTLS`), `WebCore/PlatformOnyx.cmake` and `PAL/pal/PlatformOnyx.cmake`, the
CMake patches, then `ninja -k 0` on WebCore and the errors in families (as step 1), the stubs first,
the real implementations (TLS glue, pasteboard, theme) once it links. The target of step 2 on the
PC: WebCore links into a test program that loads a page from a file and paints it into a PNG.

## The plan of the whole port

WebKit replaces Jet (NetSurf) as Onyx's browser engine (docs/POSIX-PLAN.md §8, §9, §15: WebKit2 on
the PlayStation port's model, static binaries, distributed under LGPL-2.1+):

1. **WTF + JavaScriptCore** → the `jsc` shell (the LLInt without JIT, WebAssembly in its
   interpreter; C_LOOP as an alternative). *Done: validated on the Pi, 2026-10-02.*
2. **WebCore** (Skia CPU raster from WebKit's own copy, no GL, `SkFontMgr_onyx` instead of
   fontconfig, curl + mbedTLS networking, ICU, HarfBuzz, libxml2, SQLite, woff2). *Builds, links,
   renders a page on the bench and on the Pi (2026-10-02); the network path, WebCrypto, WOFF2 and
   media are still to do.*
3. **WebKit2** (UI, web and network processes over WP-IPC: AF_UNIX socketpairs, SCM_RIGHTS, shm).
4. The Onyx view; then, in the order the user asked for (2026-10-02): **the JavaScript JIT before
   video**. The JIT: executable memory for a program's own mappings in the kernel (`PROT_EXEC`
   through `vm_map` / `vm_protect`, W^X, the instruction cache flushed from EL0: a kapi change),
   then `ENABLE_JIT` (the Baseline tier first) with JavaScriptCore's executable allocator on it —
   and a check of what the JIT tiers ask of thread suspension and signals, which Onyx lacks
   (above). Then media (`MediaPlayerPrivate` on `user/av`, MSE: Jet's decoders — VP9, AV1, Opus —
   are there), and the compositor on the V3D once the CPU rendering has proved itself. YouTube is
   the reference site: its pages need the JIT to feel fast, its videos need MSE.
5. The browser (Jet's UI reused). **No tabs** (the user, 2026-10-02): one page per browser
   window — one UI, one web and one network process per browser; what `window.open` and
   `target=_blank` do is to be decided then (the same view, or a new window).
   **Start-up** (the user, 2026-10-02): the large images are slow to load (above). Two ways,
   which add up: a lazy, file-backed ELF loader in the kernel (pages read from the file when
   first touched, code pages shared between the processes: the study is `docs/ELF-LOADER-PLAN.md`
   — the time is the SD read at 16 MB/s, the images are 99.8 % read-only, and sharing one loaded
   image between the processes of one executable is the cheap, safe gain; lazy fill is riskier
   and only helps the first start), and — the
   fallback if that cannot be done, and good for the perceived speed in any case — **a small
   front end, the host window, separate from the web view** (the page and the toolbar): the front
   end is a few hundred KB, its window appears at once, and it starts the web view, which then
   attaches to the window as an applet does (to check: how a process draws into another's
   window on Onyx — an existing applet mechanism, or a canvas in v76 shared memory — and how the
   key and mouse events reach it). If both are feasible, both are wanted, and **the web view is
   then a component other programs embed — the Mail client first** (HTML messages): a
   stand-alone web-view service with a small host interface (load a URL or the HTML given, back /
   forward / reload, the title and URL changes, a hook on navigations), an optional toolbar (the
   browser's; Mail has none), and restrictions the host sets (Mail: no remote content until the
   user allows it, scripts off, a clicked link opened in the browser). The browser's own UI does
   not go into the web view.
6. Parity with Jet, then the switch.

## The patch series

`tools/webkit/patches/*.patch` (git format-patch; each patch's message says what and why), applied
by `tools/webkit/fetch.sh` with `git am` onto the pinned revision. WebKit is not vendored in the Onyx
repository. `/bin/jsc` is distributed under LGPL-2.1+ (docs/LICENSING.md): the pinned revision, this
series and `build-jsc.sh` are its complete corresponding source.
