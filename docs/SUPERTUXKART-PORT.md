# SuperTuxKart on Onyx — feasibility study and port plan

Status: **paused** (2026-09-30, the user's call: to resume later). Study done, milestone M0 built
(2026-09-29), **M1 done: `stkpoc` PASS on the Pi** (2026-09-30; `SD:/bin/stkpoc`). Next when it
resumes: M2 (see §5). The code of the port lives in [`user/stk/`](../user/stk/); SuperTuxKart's
own source is a shallow clone **outside** the repository (see *Building M0*).

Studied: `supertuxkart/stk-code` at `7644e908` (2026-09-24, the 1.5 development line) — the code
repository only; the art (`stk-assets`, ~1 GB) was not downloaded.

## 1. Verdict

**Feasible, without OpenGL — but a large port** (the biggest Onyx has taken on after NetSurf and
the N64 emulator). The honest summary:

- **The modern renderers are out of reach.** SuperTuxKart's "SP" renderer needs OpenGL 3.1 /
  OpenGL ES 3.0 with GLSL 1.40 / GLSL ES 3.00 shaders (deferred shading, shadows, SSAO, GPU
  skinning, instancing, UBOs, texture arrays); the newer "GE" renderer is Vulkan 1.1. Onyx has no GL
  and no GLSL compiler: its V3D kapi takes QPU code that apps generate themselves
  (`user/v3d/qpu.h`). Writing a GLES 3 implementation (or porting Mesa's `v3d` + its GLSL/NIR
  compiler) would be a project many times the size of the game port. **Not recommended.**
- **But STK still has a fixed-function path, and it is not tied to GL.** When the driver is not
  OpenGL 3.1+ (`CentralVideoSettings::isGLSL()` false) STK draws everything through Irrlicht's
  classic material system (`EMT_SOLID`, `EMT_TRANSPARENT_ALPHA_CHANNEL(_REF)`, `EMT_ONETEXTURE_BLEND`,
  lightmaps, vertex colours, fog) and `IVideoDriver::draw2DImage` for the GUI. STK already runs this
  path on a **non-GL** driver: **DirectX 9** (`render_driver = directx9`: Irrlicht's `CD3D9Driver`,
  3,650 lines, + `GEDX9Texture`, 230 lines). The branches exist in STK's code
  (`irr_driver.cpp`, `central_settings.cpp`, `ge_texture.cpp`: a handful of `switch`es).
- **So the smallest credible path is a native Irrlicht video driver for the V3D** — "the DirectX 9
  driver, but on `gpu_render3`", with its fixed-function combinations turned into QPU programs by a
  small generator, exactly as gcemu turns the GameCube's TEV configurations into shaders
  (`user/v3d/gxtev.*`). No GL API, no shader compiler, no Mesa.
- **Everything else is portable C/C++** and M0 already shows the toughest runtime part works at build
  time: full C++ (exceptions, RTTI, the STL, `std::thread` / `std::mutex` /
  `std::condition_variable` on the kapi v67 threads) and three of STK's libraries (Bullet,
  AngelScript, Irrlicht) compile and link for Onyx. A syntax-only pass over STK's **443 game
  sources** in the server-only configuration: **383 compile as they are, 437 with stub BSD-socket
  headers**; the 6 left are curl, `resolv.h` / `err.h` (DNS) and one ENet IPv6 type mismatch.
- **What to expect in the end:** single-player (and split-screen) races against the AI with the
  legacy look (STK ~0.8-era: no dynamic shadows, no post-processing, no glow), at a modest
  resolution (640×480 … 1024×600) and ~15–30 fps on the Pi 4, 512-texel textures, sound and music,
  gamepads. **No online play** at first (Onyx has no UDP), no add-on downloads.
- **Cost:** roughly **12–20 k lines** of new Onyx-side code (driver, device, SDL2 backend, POSIX
  layer, stubs) plus ports of ~6 C libraries, spread over **~10–15 working sessions** of the size
  that built gcemu or NetSurf, and two small kernel changes. See §5.

## 2. What SuperTuxKart needs vs. what Onyx provides

| Need (STK) | Detail | Onyx today | Gap |
|---|---|---|---|
| Language | C++ (`-std=gnu++0x`, compiles as C++17), ~110 `throw`, ~180 `try`, ~270 `dynamic_cast`, STL everywhere | Apps: freestanding C++ without exceptions / RTTI / STL; newlib apps (Writer, Sheet) also `-fno-exceptions` | **Solved in M0**: full libstdc++ from the toolchain, `-fexceptions -frtti`, a link script keeping the unwind tables (`stk.ld`) and their registration (`onyx_eh.c`) |
| Threads | ~30 `std::thread`, ~80 `std::mutex`, `std::condition_variable`, `std::atomic`; Irrlicht: `std::recursive_mutex` | kapi v67 threads (32 a process), newlib locks; the toolchain's libstdc++ is `--disable-threads` (no `std::mutex` at all) | **Solved in M0** (to be tried on the Pi): libstdc++'s gthreads on the kapi (`compat/bits/gthr-default.h`, `onyx_gthreads*.c*`) |
| TLS | 4 `thread_local` (log prefix, profiler id, RNG, `g_process_type`) | `TPIDR_EL0` is saved per task (Circle's `TaskSwitch`); since kapi v75 a thread starts with `thread_create_ex`'s `tls`; the toolchain's `thread_local` is still emutls (one copy), `errno` shared | A TLS block per thread from the C library (docs/POSIX-PLAN.md, WP-LIBC) — or patch the 4 uses (single process type is enough offline) |
| Files | Irrlicht's file system (POSIX `opendir` / `stat` / `getcwd`), `fopen`, zip archives | newlib `fopen` (whole file slurped), kapi FatFs listing | **Partly in M0**: `compat/dirent.h`, `onyx_posix.c` (`opendir`, `stat`, `mkdir`, `chdir`, `getcwd`, `access`). Later `kapi_lseek` would avoid slurping big files |
| Renderer | SP (GL 3.1 / GLES 3) or GE (Vulkan) or the legacy fixed pipeline (GL, GLES 2, D3D9) | V3D kapi: `gpu_program`, `gpu_render2/3`, `gpu_vbuf`, `gpu_texture` (RGBA8, ≤ 256 textures, no mipmaps), app-written QPU shaders | **The main work**: an Irrlicht `IVideoDriver` on the V3D (§4, M4) |
| Window / input | SDL2 (`CIrrDeviceSDL`; STK's own gamepad code calls ~340 `SDL_` functions in 27 files) | wtk windows, full-screen apps, key / mouse events, `kapi_pad_state` | An SDL2 port with Onyx backends (video, events, joystick, audio, timer, threads) — M3 |
| Sound | OpenAL (or MojoAL = OpenAL over SDL2 audio, bundled in `lib/mojoal`) + libogg/libvorbis; MojoAL wants libsamplerate | `sound_write`: s16 stereo 44.1 kHz PCM ring | MojoAL over the SDL2 Onyx audio backend; port libogg/libvorbis (or stb_vorbis behind the 7 `ov_*` calls); libsamplerate or a linear resampler |
| Text | FreeType, HarfBuzz, SheenBidi (bundled), tinygettext (bundled) | FreeType 2.14.3 in `third_party` (Writer) | Port HarfBuzz (amalgamated `harfbuzz.cc`, C++ without exceptions — a known-portable build) |
| Images | libpng, libjpeg, zlib (Irrlicht loaders) | All three in `third_party` (NetSurf) | Build them with the port's flags |
| Scripting | AngelScript 2.35.1 (bundled) | — | **Built in M0** (`AS_MAX_PORTABILITY`, as STK does on AArch64) |
| Physics | Bullet 2.79 (STK's fork, bundled) | — | **Built in M0** |
| Scene graph | Irrlicht 1.8 fork (bundled) | — | **Built in M0** (server-only config: null driver); needs the new driver + device |
| Networking | ENet (UDP), BSD sockets, `getaddrinfo`, curl + mbedTLS/OpenSSL (online), optional SQLite | TCP only (`kapi_tcp_*`), DNS (`net_resolve`), mbedTLS 3.6 in `third_party` | Stub sockets (fail gracefully → offline game); replace `http_request_curl.cpp`; UDP kapi only if online play is wanted (K3) |
| Memory | 300–600 MB desktop, less at 512-texel textures | 2 GB heap VA per app (10–12 GB), 8 GB Pi; GPU buffers from `HEAP_LOW` (< 1 GB, shared with the kernel) | GPU texture budget: textures must stay small (≤ 512, better 256 on big tracks) |
| CPU | Main loop + physics + scene graph on one thread; audio / loading threads | All of a process's threads on **core 0**; cores 2–3 as app cores (no kapi calls there) | OK for one A72 (STK is mostly single-threaded anyway); the driver's vertex work could move to an app core later |
| Assets | `data/` of stk-code (59 MB) + stk-assets (karts, tracks, music, textures: ~1 GB) | FAT32 SD card | A trimmed pack: 1–3 tracks, 2–4 karts, GUI, fonts, SFX, music (~100–200 MB); PC-side tool to shrink textures |

Bundled libraries' sizes (lines of C/C++): AngelScript 68 k, Irrlicht 78 k, Bullet 44 k,
graphics_engine ("GE") 28 k, mcpp 16 k, SheenBidi 9 k, ENet 5.6 k, MojoAL 4.9 k, graphics_utils
4.7 k, libsquish 2.2 k, tinygettext 2 k. The game itself: 443 `.cpp`, ~275 k lines with headers.

## 3. The renderer — why the legacy path, and how

**Options weighed:**

1. *GLES 3.0 + GLSL ES 3.00 over the V3D* (to run SP): a GLSL front end, an IR, a QPU back end with
   register allocation and scheduling, plus the GL state machine, FBOs, UBOs, texture arrays,
   instancing. Mesa's `v3d` driver is exactly that and it is > 100 k lines with NIR. **No.**
2. *GLES 2.0 + GLSL ES 1.00* (to run Irrlicht's `COGLES2Driver`, which emulates the fixed pipeline
   with 8 small shaders in `data/shaders/irrlicht/`): still needs a GLSL compiler. Only 8 known
   shaders, so they could be hand-translated to QPU — but then the GL API layer is pure overhead.
   **Not worth it.**
3. *A native Irrlicht driver on the V3D kapi* — **recommended.** Derive from `CNullDriver` (which
   already has the mesh-buffer, material and 2D bookkeeping), modelled on `CD3D9Driver`:
   - `beginScene` / `endScene`: collect the frame's batches, one `gpu_render3` per frame into the
     window canvas (full-screen or windowed, "straight into the target" as teapot / gcemu do).
   - `setTransform` / `setMaterial` / `drawVertexPrimitiveList` / `drawMeshBuffer`: transform and
     light on the GPU (a vertex shader with the matrix + directional / ambient light + fog factor) or
     on the CPU for skinned meshes; vertices written once into a `gpu_vbuf` (in-place clipping, v63).
   - A **material → QPU program generator**, one program per distinct combination (cached):
     texture × vertex colour (+ second texture for `EMT_LIGHTMAP*`), alpha test (`_REF`), the blend
     factors (`BLEND_CFG`), z-write / z-test, culling, fog. Tens of combinations, not thousands —
     `gxtev.cpp` (728 lines) is the precedent.
   - `draw2DImage` / `draw2DRectangle` / `draw2DVertexPrimitiveList`: orthographic batches in the same
     frame (the GUI, the HUD, the fonts).
   - Textures: an `ITexture` (and STK's `GE::GETexture` counterpart, like `GEDX9Texture`) uploading
     RGBA8 through `gpu_texture` (the kernel tiles it). Render targets (the minimap is drawn once
     at track load in the legacy path): render into a raster buffer, re-upload as a texture.
   - STK side: take the `EDT_DIRECT3D9` branches with a new `EDT_ONYX` (or reuse the enum value),
     `render_driver = onyx` in the config.

Performance reference: gcemu draws The Wind Waker's ~80–100 k vertices a frame in ~25 ms
(`gpu_render3`, clipping on the CPU); STK's legacy tracks at low details are in that range, so
~20–30 fps at 640×480–800×600 is a fair target, lower on the big tracks.

Missing on the kernel side (§6): **mipmaps** (distant textures shimmer and cost bandwidth), the
**256-texture** per-program limit (a track + karts + GUI can exceed it), the `HEAP_LOW` budget for
texture memory.

## 4. Dependencies — how each is provided

| Dependency | Plan | Notes |
|---|---|---|
| libstdc++ (exceptions, RTTI, STL) | **toolchain's** (M0) | `stk.ld` + `onyx_eh.c`; function-local statics' guards are not thread-safe (libsupc++ single-threaded) |
| C++ threads | **gthreads on kapi v67** (M0) | mutex = kapi_lock word (no handle: 256 max per process), condvar = sequence + 1 ms sleeps, no TLS for `call_once` |
| Bullet 2.79 | **port** — built (M0) | unchanged sources |
| AngelScript 2.35.1 | **port** — built (M0) | `AS_MAX_PORTABILITY`, `AS_NO_THREADS` |
| Irrlicht (STK fork) | **port** — built, null driver (M0) | + `CIrrDeviceOnyx` or the SDL device, + the V3D driver |
| graphics_engine ("GE") | **port** | its GL / Vulkan parts compile (glad, Vulkan headers bundled) but stay unused; add the Onyx texture class |
| POSIX file calls | **Onyx layer** (M0 started) | `dirent`, `stat`, `mkdir`, `getcwd`… on the kapi |
| BSD sockets, `getaddrinfo`, `uname` | **stub** | headers + functions failing with `ENETDOWN` → offline only |
| ENet | **port** (bundled) on the stub sockets | UDP kapi (K3) if online play ever |
| curl | **replace** `src/online/http_request_curl.cpp` | a stub failing requests; later Onyx's `httpc` + mbedTLS |
| mbedTLS / OpenSSL (crypto) | **port** mbedTLS 3.6 (in `third_party`) or stub | only the online protocols use it |
| SQLite | **off** (`USE_SQLITE3=OFF`) | server-side feature |
| SDL2 | **port** with Onyx backends (video: a framebuffer window; events; joystick on `kapi_pad_state`; audio on `sound_write`; timer; threads on kapi) | alternative: a small `SDL_*` shim for STK's gamepad code + a native Irrlicht device |
| OpenAL | **MojoAL** (bundled) over SDL2 audio | needs libsamplerate (port, ~C only) or a linear-resampler patch |
| libogg / libvorbis | **port** (C) | or stb_vorbis behind the 7 `ov_*` calls |
| FreeType | **reuse** `third_party/freetype-2.14.3` | STK wants the SFNT/TrueType modules, as Writer |
| HarfBuzz | **port** (amalgamated build) | could be stubbed to one-glyph-per-codepoint for Latin only |
| SheenBidi, tinygettext, mcpp, libsquish | **port** (bundled) | libsquish only if compressed textures are kept |
| libpng, libjpeg, zlib | **reuse** `third_party` | |
| shaderc, astcenc, libbfd, wiiuse, openglrecorder | **off** | |

## 5. Milestones (in order)

Sizes are rough: *new lines* of Onyx-side code, and *sessions* of the kind that built gcemu or
NetSurf's port.

| # | Milestone | What proves it | Size |
|---|---|---|---|
| **M0** | **Toolchain + first libraries** — *done in this session* | `user/stk`: `libbullet.a`, `libangelscript.a`, `libirrlicht.a` (server-only) and `stkpoc.elf` link | ~700 lines, done |
| M1 | **Run `stkpoc` on the Pi**; decide TLS (K1 or patch the 4 `thread_local`) | `stkpoc` prints PASS: exceptions unwind, threads / condvars work, Bullet and a script run | **done 2026-09-30: PASS on the Pi 4** (every line ok, the ball at y = 0.500, `main () = 6765`); TLS still to decide |
| M2 | **Headless game**: STK built `SERVER_ONLY` (no graphics, no sound), the stub sockets, curl replaced, the POSIX layer completed, the `data/` + one track + the karts on the card; `supertuxkart --no-graphics --profile-laps=1 --track=<t> --numkarts=4` | an AI race runs to the end on Onyx and prints its timings: game logic, XML, track and kart loading, Bullet, AngelScript all proven, CPU cost measured | 2–3 sessions, ~2–3 k lines (mostly stubs and build glue) |
| M3 | **Platform layer**: SDL2 with Onyx backends (or the native device + SDL shim), MojoAL + libogg/vorbis, HarfBuzz, libpng/jpeg; the client build (not `SERVER_ONLY`) links with the **null** video driver | the menus' logic, input and sound work; music plays over a black screen | 2–3 sessions, ~3–4 k lines |
| M4 | **The V3D Irrlicht driver**, 2D first: textures, `draw2DImage`, fonts → the menus on screen; then 3D: meshes, the material → QPU generator, fog, lightmaps, alpha test / blend, skinned karts on the CPU | the main menu, then a race rendered (legacy look) | 3–5 sessions, ~6–9 k lines |
| M5 | **Playable**: gamepad / keyboard mapping, full-screen, a settings preset (resolution, 256/512 textures, no particles-heavy effects), the trimmed asset pack + a PC tool to shrink textures, the `.app` packaging, docs 03 / 04 | a full Grand Prix playable at ≥ 20 fps on a small track | 2 sessions |
| M6 | Optional: mipmaps and more textures in the kapi (K2), vertex work on an app core, split-screen, UDP (K3) and online play | — | open |

The order matters: M2 flushes out everything that is *not* graphics (the bulk of the 275 k lines)
before any renderer work, and gives a benchmark of the CPU side on the Pi.

## 6. Risks

- **The renderer's size and fidelity.** A fixed-function driver means the legacy look; some modern
  tracks rely on SP-only materials (e.g. normal maps, "alpha test + second UV" combinations) and
  will look flatter. STK's legacy path is less tested upstream than SP (D3D9 is its main user).
- **GPU memory and the kapi's limits.** Textures come from `HEAP_LOW` (< 1 GB, shared with the
  kernel); 256 textures per program; no mipmaps. A big track at 512-texel RGBA8 can need
  ~100–200 MB of textures. Mitigation: 256-texel textures, a texture cache that evicts, K2.
- **One core.** All STK threads share core 0 with the rest of Onyx (the compositor, the network
  unless `netcore=1`). Physics at STK's 120 Hz step for 4–8 karts plus the scene graph plus the
  driver's vertex preparation on one A72 may cap the frame rate below the GPU's.
- **The thread layer is new and untried** (M0 builds it; M1 must run it). Condition variables are
  1 ms polling, not true waits; `thread_local` needs K1 or source patches; `errno` is shared; the
  unwinder and function-static guards are not thread-safe.
- **Files are slurped whole** by newlib's `_open`: fine for STK's many small files, heavy for the
  music (`.ogg` streamed from disk — a few MB each) and for zip archives if used. A `kapi_lseek`
  would fix it.
- **Assets.** The art is ~1 GB with thousands of files on FAT32; loading time from the SD card; the
  licensing of the packaged subset (STK's art is CC-BY-SA / GPL, keep the credits). Nothing of it
  goes in the repository.
- **Upstream drift.** Pin an STK release (e.g. 1.4 or a 1.5 tag) rather than git master; the
  legacy / D3D9 path could be removed upstream one day.
- **Networking.** STK's code paths assume sockets exist; the stubs must fail cleanly everywhere the
  game probes the network (LAN discovery, news, add-ons).

## 7. Kernel / kapi changes wished (K1's kernel half: in v75)

- **K1 — TLS** (corrected 2026-10-02: the kernel half was never missing): `TPIDR_EL0` is already
  saved and restored per task (Circle's `TaskSwitch`, which an EL0 preemption goes through too), and
  kapi v75 gives a new thread its initial value (`thread_create_ex`'s `tls`) and an app-core job its
  caller's. What remains is user side: a TLS block per thread (the main thread's `PT_TLS` set up by
  the C library's start-up, the others by its `pthread_create`) -- docs/POSIX-PLAN.md WP-LIBC. Also
  gives per-thread `errno`.
- **K2 — GPU textures**: mipmaps in `gpu_texture` (the V3D samples them; the layouts are Mesa's
  `v3d_setup_slices`), more than 256 textures a program, a larger `HEAP_LOW` budget for textures.
- **K3 — UDP** (optional): datagram sockets for ENet (online / LAN play).
- **K4 — `kapi_lseek`** (optional): no whole-file buffering in `_open`.

## 8. Milestone M0 — what was built

Folder [`user/stk/`](../user/stk/) (new; nothing else in Onyx changed):

| File | What |
|---|---|
| `Makefile` | builds the libraries from STK's tree (`STK=` path, default the clone next to the repository) and `stkpoc.elf`, into `user/stk/build/` (ignored by git) |
| `stk.ld` | `user.ld` + `.eh_frame` / `.gcc_except_table` kept, `.eh_frame` terminated, `__onyx_eh_frame_start` |
| `onyx_eh.c` | registers the unwind tables with libgcc (`__register_frame_info`) in a priority-101 constructor (we link `-nostartfiles`: no `crtbegin.o`) |
| `compat/bits/gthr-default.h` | libstdc++'s gthreads on Onyx, shadowing the toolchain's `gthr-single.h`; with `-D_GLIBCXX_HAS_GTHREADS=1` libstdc++'s `<mutex>`, `<condition_variable>`, `<thread>` come alive |
| `onyx_gthreads.c` | its out-of-line half: `kapi_thread_create` (1 MB stacks), join, timed locks, condvar waits, keys, `sleep` / `usleep` |
| `onyx_gthreads_cxx.cpp` | what `libstdc++.a` lacks when built without threads: `std::thread::_M_start_thread` / `join` / `detach`, `std::condition_variable`, `std::call_once`'s no-TLS helpers (`__once_proxy`…) |
| `compat/dirent.h`, `compat/byteswap.h`, `onyx_posix.c` | the POSIX calls Irrlicht needs (`opendir` family, `stat`, `mkdir`, `chdir`, `getcwd`, `access`; `bswap_*`) |
| `stkpoc.cpp` | the proof program: exceptions, RTTI, STL, `std::atomic`; 4 `std::thread`s on a `std::mutex`, a condition-variable producer / consumer, `std::recursive_mutex`, `std::call_once`; a Bullet sphere dropped on a plane (must rest at y = radius); an AngelScript script calling back a native function. Prints PASS / FAIL |

**Results (build only):**

- `libbullet.a` — all 117 sources of STK's Bullet list: **compile, no source change**.
- `libangelscript.a` — all 39 `as_*.cpp`: **compile, no source change**.
- `libirrlicht.a` — all 138 sources, server-only configuration: **compile, no source change**
  (before the thread layer 19 failed on `std::recursive_mutex`; before the POSIX headers, 2 on
  `<dirent.h>` / `<byteswap.h>`). It references STK's `utils/` and `io/` functions, resolved when
  the game links.
- `stkpoc.elf` — **links**: 1.25 MB text, 2 LOAD segments at 8 GB as every app, `.eh_frame`
  (120 KB) and `.gcc_except_table` kept.
- STK's own `src/` (443 files), syntax-only, `SERVER_ONLY`: 383 compile as they are (the rest:
  `<sys/socket.h>` and friends); with throw-away stub socket headers 437; left: curl, `resolv.h`,
  `err.h`, one ENet `ENetIP` type mismatch (`ENABLE_IPV6` vs. the bundled ENet).
- **Run on the Pi 4 (2026-09-30): PASS** -- exceptions unwind, `std::thread` / mutexes / condition variables / `call_once` on the kapi v67 threads, the Bullet sphere at rest (y = 0.500), the AngelScript script and its native call-back (`main () = 6765, report (88)`).

**Building M0** (from Git Bash on the Windows PC; the toolchain is in WSL):

```sh
git clone --depth 1 https://github.com/supertuxkart/stk-code C:/Users/troll/stk-port/stk-code
MSYS_NO_PATHCONV=1 wsl -e sh -c 'export PATH=$HOME/tc/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin:$HOME/local/usr/bin:$PATH; make -C /mnt/c/Users/troll/source/repos/Zircon/user/stk -j8'
```

(`make STK=<path>` for another checkout; ~3 minutes, Irrlicht being most of it.)

**To try it (M1):** copy `user/stk/build/stkpoc.elf` to the card as e.g. `SD:/bin/stkpoc` (no
extension) and run `stkpoc` in a terminal. Expected: every line `ok`, `the ball at y = 0.500`,
`main () = 6765, report (88)`, then `PASS`.

## 9. Next step

1. **M1**: run `stkpoc` on the Pi; fix what the thread layer / unwinder get wrong.
2. **M2**: vendor a pinned STK release into `third_party/stk-code` (code only), write the stub
   socket headers + `onyx_net.c`, a curl-less `http_request`, complete the POSIX layer, and link
   `SERVER_ONLY` STK (Irrlicht, Bullet, AngelScript, ENet, mcpp, zlib, mbedTLS); run the headless
   AI race with the smallest track and two karts.
3. Then M3 → M5 as above. Keep this document as the port's log (what compiled, what ran, what next),
   the way `docs/06-JET-BROWSER.md` records NetSurf's changes.
