# A Nintendo 3DS emulator for Onyx — feasibility study

*2026-10-09. The study; since then **phase T0 is done** (section 9b): Dynarmic runs on Onyx, option C stands. The user's request: "toutes les options sont possibles
(portage, portage partiel, from scratch)"; no crypto, no Nintendo key — the user brings decrypted
dumps of their own games.*

## Contents

1. Verdict and recommendation
2. The 3DS in one page
3. Without keys: what is legal, what the emulator never needs
4. What an emulator must provide (high-level emulation)
5. Option A — from scratch, MIT
6. Option B — a full port (Azahar, Panda3DS)
7. Option C — partial port: our core, permissive pieces (Dynarmic, Teakra)
8. Performance budget on the Pi 4 (and the Pi 5)
9. The plan T0–T8 (option C, falling back to A)
10. Tests
11. Risks and open questions
12. Sources

---

## 1. Verdict and recommendation

**Feasible, and within reach of what Onyx already did** (the GameCube: a PowerPC JIT, the TEV
compiled to V3D shaders, the GX on a second app core; the DS: an ARM JIT, a PICA-like fixed 3D
pipeline in software). The 3DS is a **lighter CPU** than the GameCube (one ARM11 at 268 MHz
emulated, against a 486 MHz PowerPC) but a **wider system**: an operating system (Horizon) whose
services the games call, a GPU with programmable vertex shaders, a DSP.

| Option | Licence of `n3dsemu` | Effort | Fit with Onyx | Verdict |
|---|---|---|---|---|
| A. From scratch | MIT | ~25 k lines, the largest emulator of Onyx (DS ~7 k, GameCube ~11 k) | best: our kits, our GPU path, our JIT | possible, longest |
| B. Full port of Azahar / Panda3DS | GPL-2.0 / GPL-3.0 | the renderer rewritten for `kapi_gpu` (they need OpenGL 3.3+/GLES 3.2/Vulkan, Onyx has none), Qt/SDL/Boost replaced, C++20 + exceptions runtime | poor: desktop architecture, heavy | not recommended |
| **C. Partial port** | **MIT** (Dynarmic 0BSD, Teakra MIT) | ~18–20 k lines of ours + Dynarmic | good | **recommended** |

**Recommendation: option C**, our own core (MIT) with **Dynarmic** (0BSD, the ARM JIT that
Citra, Azahar and Panda3DS all use, with an AArch64 backend) for the ARM11 — the risky,
compatibility-critical piece already proven on thousands of games — and everything else ours:
the loader, the HLE kernel and services, the PICA200 on V3D (reusing gcemu's shader generator),
the DSP in HLE. If Dynarmic cannot be brought up on Onyx's runtime in phase T0 (C++20, its
dependencies, exceptions), fall back to **A**: extend the DS's own JIT (`nds_jit.cpp`) to ARMv6K
and VFPv2.

**Expected result on the Pi 4** (estimates, to be measured from T3 on): 2D and light 3D games at
full speed; the big 3D games (Pokémon X/Y–Sun/Moon, Monster Hunter) likely 20–30 fps at first.
On the **Pi 5** (~2.5× the CPU), most games at full speed. New-3DS-only games (a handful:
Xenoblade, …) out of scope.

## 2. The 3DS in one page

| Part | Old 3DS (the target) | New 3DS |
|---|---|---|
| Application CPU | ARM11 MPCore (ARMv6K, VFPv2), **268 MHz**; core 0 runs the game ("appcore") | 4 cores at 804 MHz, L2 cache |
| System CPU | ARM11 core 1 (the "syscore": services, GSP, some audio) | |
| Security CPU | ARM9 (Process9: file system, crypto) — **never emulated in HLE** | |
| Memory | FCRAM 128 MB, VRAM 6 MB, AXI WRAM 512 KB | FCRAM 256 MB |
| GPU | **PICA200** (DMP) 268 MHz: programmable vertex shaders (and geometry shaders), fixed-function fragment stage: 6 texture-combiner stages (TEV-like), per-fragment lighting with look-up tables, fog, procedural textures, shadows | same |
| Screens | top 400 × 240 (800 × 240 in stereoscopic 3D), bottom 320 × 240 touch | |
| Sound | XpertTeak DSP 134 MHz, its firmware loaded by the game; 24 voices, 32728 Hz | |
| Inputs | buttons, Circle Pad, touch, gyroscope / accelerometer, microphone, cameras | + C-Stick, ZL / ZR |
| OS | Horizon: microkernel, ~40 services (processes) reached by IPC | |

A game is an ARM11 process: it talks to the kernel (SVCs: memory, threads, synchronisation, IPC)
and, through IPC, to services (`fs:USER`, `gsp::Gpu`, `hid:USER`, `apt:U`, `dsp::DSP`, `cfg:u`,
`ptm:u`, `ldr:ro`, `y2r:u`, `cecd`, `frd`, `ac:u`, `ndm:u`, `nwm`…). The GPU is driven by
command lists that the game submits through `gsp::Gpu`.

## 3. Without keys: what is legal, what the emulator never needs

- A retail cartridge or eShop title is **encrypted** (AES, keys inside the console's ARM9
  bootrom). Tools on the user's own console (GodMode9) dump it **decrypted** — the NCCH header
  then says *NoCrypto* — and an emulator reads it like any file: **no key, no AES, no bootrom**.
  Onyx will refuse an encrypted dump with a clear message (as `ndsemu` does for an encrypted
  secure area) and will never read an `aes_keys.txt`.
- **No system firmware either**: high-level emulation replaces the OS. Two data files of the
  console are used by many games: the **shared system font** (a BCFNT) and the Mii data. We
  **generate our own** replacement font (a BCFNT built from a free font, e.g. DejaVu / Noto, by a
  tool in `tools/`), and Mii data stubs; a user may also put the dump of their own console's
  font in `SD:/apps/n3dsemu.app/sysdata/`.
- Formats read: `.3ds` / `.cci` (cartridge image, NCSD), `.cxi` (one NCCH), `.3dsx` (homebrew),
  `.elf`. `.cia` (installable, may carry a ticket and title key) only when decrypted; later.
- The project rule stays: no ROM, BIOS, font or firmware of Nintendo committed or shipped.

## 4. What an emulator must provide (high-level emulation)

| Block | Content | Size (ours) |
|---|---|---|
| Loader | NCSD / NCCH / ExeFS / RomFS, the code's LZ ("BLZ") decompression, the exheader (memory, stack, services allowed), `.3dsx` relocations | ~1.5 k |
| CPU | ARMv6K + Thumb + VFPv2 on the application core; the syscore not emulated | Dynarmic, or ~2.5 k on top of the DS JIT |
| Memory | the process's virtual space (code 0x00100000, heap 0x08000000, linear heap 0x14000000 / 0x30000000, VRAM 0x1F000000, shared pages, TLS), page tables as in `ndsemu` | ~1 k |
| Kernel HLE | ~60 SVCs used by games: memory (ControlMemory, MapMemoryBlock), threads (priority scheduler, cooperative on one host core), mutex / semaphore / event / timer / address arbiter, IPC (SendSyncRequest, ports, sessions, handles), time | ~3.5 k |
| Services | `srv:`, `apt:U` (applets: the software keyboard, error display as stubs or small UIs), `gsp::Gpu` (command lists, framebuffers, interrupts), `hid:USER` (buttons, circle pad, touch, gyro), `fs:USER` (archives: RomFS, SaveData, ExtSaveData, SDMC, the shared font), `cfg:u` (the language — Onyx's —, region, user name), `dsp::DSP`, `ptm:u`, `ldr:ro` (**CRO** dynamic modules: Pokémon, Smash, Mario Kart 7), `y2r:u` (YUV videos), `cecd`, `frd`, `ac:u`, `ndm:u`, `news`, `boss`, `act`, `am`, `cam`, `mic`, `ir:USER` (stubs) | ~7 k |
| GPU | the PICA200: command list decoding, vertex attributes / index buffers, the vertex shader (and geometry shader), primitive assembly, clipping, the fragment stage (combiners, lighting LUTs, fog, procedural textures, shadow / stencil / depth, blending, logic op), textures (14 formats, Morton-tiled, ETC1/ETC1A4), the framebuffers and their transfers (display transfer, texture copy, memory fill) | ~6 k |
| DSP (HLE) | the "DspFirmware" interface: 24 sources (PCM8/16, ADPCM; AAC in a few games), mixers, effects (delay, reverb kept simple), the output through AudioKit | ~2 k |
| App | `n3dsemu`: two screens (as `ndsemu`), the touch screen, the Circle Pad from the pad's stick, saves, the Game Library | ~1.5 k |

## 5. Option A — from scratch, MIT

All of section 4 written for Onyx, the CPU by extending the DS's JIT:
- **ARMv6K over ARMv5TE** (the DS's ARM9): the media instructions (`SADD16`, `UQADD8`, `SEL`,
  `SXTB` / `UXTAH` …, `REV` / `REV16`, `SSAT` / `USAT`, `PKHBT`, `SMUAD` / `SMLAD`…), `LDREX` /
  `STREX` / `CLREX`, `CPS`, `SRS` / `RFE` (kernel only), the CP15 thread-ID registers; **VFPv2**
  (single and double: map onto NEON / FP registers, the FPSCR's rounding, the rarely-used vector
  mode in the interpreter). The DS JIT's design carries over (host NZCV = guest flags, register
  cache, inline page-table accesses, chaining, idle-loop skip); the media ops map well to NEON /
  SIMD-within-a-register.
- **Pros**: one licence (MIT), the code sized for Onyx (no C++20 runtime, no exceptions), we know
  every line. **Cons**: compatibility of the JIT on real games is the long tail (Dynarmic needed
  years); ~25 k lines.

## 6. Option B — a full port

| | **Azahar** (Citra + Lime3DS + PabloMK7's fork) | **Panda3DS** |
|---|---|---|
| Licence | GPL-2.0 (Citra: GPL-2.0-or-later) | GPL-3.0 |
| Maturity | the reference: most games playable on PC and Android | "many games boot, many don't" |
| CPU | Dynarmic | Dynarmic |
| GPU | OpenGL 4.3 / GLES 3.2 / Vulkan 1.1; a software renderer (slow) | OpenGL 4.1 / Vulkan, shaders recompiled for the GPU |
| Android floor | Snapdragon 835 (Kryo 280, faster than the Pi 4's A72), GLES 3.2 | arm64 APK |
| Size | very large (Qt, SDL, Boost, many services fully emulated, network, cameras…) | medium |

Porting either means: **a new renderer** for Onyx's GPU path (`kapi_gpu_program` / batches /
textures — there is no OpenGL on Onyx and V3D 4.2's own GLES lacks geometry shaders anyway),
their threading model on our threads and app cores, a C++20 runtime with exceptions (Jet has the
pieces), the frontends replaced by an Onyx app. The core would stay theirs: our kits (GameKit,
AudioKit, UIKit) at the edges only. The app becomes **GPL** (allowed by `docs/LICENSING.md` like
Doom or the Media Player, but against "ours under MIT"). The renderer is the largest single
piece of option A anyway, so the port saves the services and the kernel, not the hardest part.
**Not recommended**; Azahar stays the **behaviour reference** (read, not copied — the clean-room
rule of `docs/LICENSING.md`, extended to Citra / Azahar / Panda3DS).

## 7. Option C — partial port: our core, permissive pieces

- **Dynarmic** (0BSD — public-domain-like: no notice required; its dependencies mcl, oaknut MIT,
  fmt MIT-like, robin-map MIT; xbyak / zydis only for the x86 backend, not built): the ARM11 JIT.
  ARMv6K, Thumb, VFPv2; an AArch64 backend; "fastmem" (a 4 GB host window) optional — Onyx can
  start with its page-table callbacks and add fastmem later (the kernel's fault forwarding that
  gcemu's study already listed). **Bring-up risks** (phase T0): C++20 with the Onyx toolchain
  (GCC 14: fine), whether it needs exceptions / RTTI (it builds with `-fno-exceptions`? to
  check; else its few throw sites patched), its code memory through `kapi_code_alloc`
  (write then execute: the same as our JITs), its use of `std::` containers (newlib + libstdc++:
  available).
- **Teakra** (MIT): a low-level emulator of the XpertTeak DSP — exact but too slow for real time
  on a Pi (the PC emulators run it only as an option). Use it **on the PC only**, to check our
  DSP HLE against the real firmware's output; the app ships the HLE.
- **Our own**: everything else (sections 4 and 9). The app stays **MIT**.

## 8. Performance budget on the Pi 4 (and the Pi 5)

Onyx's cores: core 0 the kernel / the display, core 1 the sound, **cores 2–3 the app cores**
(gcemu: the machine on one, the GX on the other).

| Work | Where | Estimate |
|---|---|---|
| ARM11 at 268 MHz, games often idle-waiting on VBlank / events | app core 2 (JIT) | ARM→AArch64 ≈ 1.5–3 host instructions a guest one at best (same ISA family); ~40–70 % of a 1.5 GHz A72 for a busy game — **fits** (gcemu: a 486 MHz PowerPC on one core at ~95 %) |
| HLE kernel + services | core 2 (between JIT runs) | small (C++ calls) |
| PICA vertex shaders, primitive assembly, clipping | **app core 3**: the shader JIT'd to NEON (4 vertices at once) | 10–60 k vertices a frame in 3D games; ~100–200 host instructions a vertex → 2–12 M instr. a frame: **fits** at 30–60 fps |
| Fragments: combiners, lighting LUTs, fog, depth / stencil, blending | **V3D** (QPU fragment shaders generated per configuration, like `v3d/gxtev`) | the screens are tiny (400 × 240 + 320 × 240 = 173 k pixels): the GPU has a large margin; lighting LUTs as 1-D textures |
| Texture decoding (Morton tiles, ETC1) | core 3, cached by address + hash | as gcemu's |
| Render-to-texture, read-backs, display transfers | **shared memory** (V3D and the CPUs see the same RAM: no PCIe copies as on a PC) | an advantage of the Pi |
| DSP HLE + AudioKit | core 2 or the app's main thread | small |

The PC emulators' high requirements come mostly from **desktop GPU-driver overhead and
accuracy features** (resolution scaling, shader compilation stutter, texture filtering): Onyx
renders at native resolution with its own command path. **Risks**: geometry-shader games (few),
heavy CRO games, the per-pixel lighting shaders' length on V3D 4.2 (a QPU program limit: split
or fall back), thermal throttling (the Pi needs its fan, as for gcemu).

**Pi 5**: Cortex-A76 at 2.4 GHz (~2–3× a Pi 4 core), V3D 7.1 — the same code (Onyx's v42/v71
shader paths) with a comfortable margin.

## 9. The plan T0–T8 (option C, falling back to A)

Each phase ends with something visible and a test, as for the DS (D0–D5).

| Phase | Content | Done when |
|---|---|---|
| **T0** | Dynarmic built for Onyx (`user/Libs/dynarmic/`, its licence files), a bring-up test under qemu-aarch64 and on the PC; **decision C or A** | an ARM test program runs JIT'd, same results as an interpreter |
| **T1** | Loader (NCSD / NCCH / `.3dsx`), memory map, kernel HLE (threads, sync, IPC, handles), `srv:`, `apt:U`, `gsp::Gpu` (framebuffers only), `hid:USER`, `fs:USER` (RomFS, SDMC) | our own homebrew prints and draws on both screens (2D framebuffers) |
| **T2** | PICA200 in **software** (a reference rasterizer like `nds_render3d.cpp`: vertex shader interpreter, combiners, lighting, fog, textures), command lists, display / texture transfers, memory fill | our 3D tests and public homebrew 3D demos render right on the PC |
| **T3** | PICA200 on the Pi: vertex shader JIT (NEON) on app core 3, fragment stage as V3D shaders (generator in `user/Libs/v3d/`, the `gxtev` approach), texture cache; the software path kept as reference and fallback | the same pictures on the Pi, the speed measured |
| **T4** | DSP HLE (sources, ADPCM, mixing, AudioKit), `y2r:u`, `mic`/`cam` stubs; checked against Teakra on the PC | homebrew with sound; first retail games' music |
| **T5** | Retail compatibility: `ldr:ro` (CRO), save archives (SaveData, ExtSaveData: `<rom>.sav/` folder), the shared font (our BCFNT), `cfg:u` from Onyx's settings, the software keyboard applet (Onyx's own), Mii stubs, more SVCs / services as games ask | a first list of the user's games boots to play |
| **T6** | Speed: block linking / fastmem (Dynarmic options), idle-loop and VBlank-wait skips, vertex batching, frame pacing; `sysstat`-style timings like gcemu's | per-game measures on the Pi 4 |
| **T7** | App `n3dsemu` (EN / FR, FreeType): two screens with the DS app's layouts, touch with the mouse, Circle Pad on the pad's left stick, C-Stick / ZL / ZR mapped, F12 speed; GameKit / Game Library (`3ds cci cxi 3dsx`), console home "3DS"; docs 01 / 03 / 04, HANDOFF, LICENSING; packages Pi 4 and Pi 5 | published |
| **T8** | Later: `.cia` (decrypted), stereoscopic 3D (left eye only first), the gyroscope from a pad that has one, New 3DS mode, AAC | |

## 9b. Phase T0 -- done (2026-10-09, the local session)

**Dynarmic's ARM11 JIT runs on Onyx: option C stands, the fallback to our own JIT is not needed.**

- **What is in the repository**: `third_party/dynarmic-a466015` (Azahar's Dynarmic of 2026-09-26, with fmt, mcl,
  oaknut, robin-map: 285 files, 3.2 MB) and `third_party/boost-1.86.0` (the 742 headers Dynarmic includes --
  `boost/icl`, `boost/variant` and what they pull: 5.4 MB; **Boost was not in the study**: BSL-1.0, permissive,
  nothing owed for a binary). Only the AArch64 back end and the **A32 front end** (121 sources instead of 190):
  copied by `tools/n3ds/vendor_dynarmic.py` from a CMake build's dependency log, never edited by hand.
- **The build**: `user/Libs/dynarmic/Makefile` (no cmake; the apps' toolchain, GCC 14.2; ~80 s on 12 threads) ->
  `libdynarmic.a` (Dynarmic + fmt + mcl) and `codemem.o`. Dynarmic's sources are **not patched**; Onyx's pieces are
  three small files in `user/Libs/dynarmic/onyx/`:
  - `sys/mman.h` + `codemem.c`: the code memory (`mmap` = AppKit's `kapi_code_alloc`; newlib has no `<sys/mman.h>`);
  - `onyx_std_mutex.h`: `std::mutex` as a spin lock (the bare-metal libstdc++ has none), put before every source.
- **Exceptions**: Dynarmic does not compile with `-fno-exceptions` (its asserts `throw` in constant expressions),
  so the library is built with exceptions and RTTI; it throws nothing in normal use, and a program built as the
  apps are (`-fno-exceptions -fno-rtti`) links it and runs. The instruction cache is flushed from EL0
  (`__builtin___clear_cache`), as Onyx's own JITs do.
- **The test** `sh tools/tests/run_n3ds_t0.sh` (`tools/tests/n3ds/dynarmic_t0.cpp`): ARM (a loop, loads and stores,
  ARMv6's UXTB / REV, MUL / UMULL, conditions, shifts with flags, PUSH / POP), VFP (conversions, add, multiply)
  and Thumb programs, `ArchVersion::v6K`, once through the memory callbacks and once through a **page table**
  (no callback then: the inline path), and a code invalidation. **All pass under qemu-aarch64 and on the Pi 4**
  (the same test linked as an Onyx program, run from telnet). The program: 2.4 MB of code.
- **Not done in T0**: a differential test against an interpreter on random instructions (section 10's fuzzer:
  with T1, when there is a second CPU to compare with -- Dynarmic's own tests need Unicorn); fastmem (needs the
  kernel to forward faults: T6); a speed measure (T3 / T6); the hook in `user/Makefile` (with the app, T1).

## 9c. Phase T1, first slice -- done (2026-10-09)

The core's skeleton, `user/Emulators/n3ds/` (`n3ds.h` declares everything), and its first test program:

- `n3ds_mem.cpp`: one host pointer a 4 KB page (the table Dynarmic's code reads), the 128 MB of FCRAM taken from
  both ends (the linear heap from its start -- a linear address is its FCRAM offset --, the rest from its end), VRAM.
- `n3ds_cpu.cpp`: **the only file that sees Dynarmic**, behind `Cpu` (run / halt / registers / save / load /
  invalidate): ARMv6K, the page table, CP15's thread registers (the TLS address every program reads), an exclusive
  monitor for LDREX / STREX, SVC to `Machine::svc`.
- `n3ds_kernel.cpp`: the handle table, threads (0x200 bytes of TLS each) and the scheduler (the highest priority
  runs until it waits; one priority's threads take turns when they yield), events (one-shot, sticky, pulse),
  mutexes (recursive, freed at their owner's end), semaphores, address arbiters, waits on one or several objects
  with a time-out, sleeps, the heaps (ControlMemory), and 26 system calls.
- `n3ds_loader.cpp`: an ELF (our test programs); `.3dsx` and NCCH next.
- **Tests**: `sh tools/tests/run_n3ds_test.sh` builds `n3dstest` for AArch64 and runs it under qemu-aarch64 (the
  real JIT; there is no x86-64 build: the vendored Dynarmic has the AArch64 back end only).
  `tools/tests/n3ds/src/` is our own start-up, SVC wrappers and `kernel.c` (built with `arm-none-eabi-gcc`,
  ARMv6K + VFP hard float): **137 checks, 0 failed** (the processor in a real program -- Thumb, VFP, 64-bit
  helpers, LDREX / STREX, the TLS --, the heap, threads and priorities, mutexes, events, time-outs measured in
  ticks, semaphores, WaitSynchronizationN, arbiters, handles).
- **Left in T1**: IPC (ports, sessions, the command buffer in the TLS) and `srv:`; `apt:U`, `gsp::Gpu` (the
  framebuffers, the VBlank interrupts), `hid:USER`, `fs:USER`; the `.3dsx` and NCCH loaders; timers; shared memory
  blocks; the configuration and shared pages' content; then a homebrew of ours that draws on both screens.

## 9d. Phase T1, second slice -- done (2026-10-09): IPC, `srv:`, `gsp::Gpu`, the screens

- **IPC** (`n3ds_kernel.cpp`, `n3ds_ipc.cpp`): `ConnectToPort` ("srv:"), `SendSyncRequest` -- the command buffer in
  the thread's TLS (+0x80), answered in place by the service's function (`Session`, `ServiceFn`); a command that is
  not emulated is answered "done" and **noted** (`Machine::notes`, printed by `n3dstest` as *not emulated: ...* --
  the list to read first when a game stops). `srv:`: RegisterClient, EnableNotification, GetServiceHandle (an
  unknown service is refused and noted), Subscribe / ReceiveNotification.
- **The kernel**: timers (one-shot, periodic; counted in the scheduler's time), shared memory blocks
  (Create / Map / UnmapMemoryBlock): 35 system calls now.
- **`gsp::Gpu`** (`n3ds_gsp.cpp`): RegisterInterruptRelayQueue (the program's event, the shared page), the
  interrupt queue (PSC0 / PSC1 / PDC0 / PDC1 / PPF / P3D / DMA), the framebuffers a program asks for in the shared
  page (taken at the VBlank) or by SetBufferSwap, TriggerCmdReqQueue with the GX commands: **memory fill** and DMA
  done; a command list and a display transfer are counted and their interrupt given (**nothing drawn: T2**).
  `Machine::runFrame` = one frame of the processor, then `vblank ()`.
- **The screens**: `Machine::screenImage` reads a screen's picture from the program's framebuffer (turned a
  quarter, columns from the bottom; RGBA8, BGR8, RGB565, RGB5A1, RGBA4).
- **Test**: `tools/tests/n3ds/src/gfx.c` -- srv:, gsp::Gpu, a picture drawn on both screens, VBlanks timed, a fill
  by the GPU, timers, shared blocks: **45 checks, 0 failed**, and the picture's checksum
  (`tools/tests/n3ds/expect/gfx.crc`; `n3dstest <elf> <frames> <picture.ppm>` writes it).
- **Left in T1**: `apt:U`, `hid:USER`, `fs:USER`, `cfg:u` (what a real homebrew's start-up asks, libctru's), the
  system calls it makes at its start (GetSystemInfo, GetProcessInfo, GetResourceLimit...), the `.3dsx` and NCCH
  loaders, the configuration and shared pages' content; then a public homebrew (not ours) on both screens.

## 9e. Phase T1, third slice -- done (2026-10-09): a public homebrew runs

- **The `.3dsx` loader** (`n3ds_loader.cpp`: three segments, the absolute and relative words adjusted, the RomFS
  at the file's end kept as `Machine::romfs`); `Machine::load` takes an ELF or a `.3dsx` (a game's NCSD / NCCH is
  recognised and refused: T5).
- **What a real homebrew's start-up asks** (libctru's): the configuration and shared pages (kernel 2.57, a retail
  Old 3DS, the application's 64 MB, the date), GetSystemInfo / GetProcessInfo / GetResourceLimit and its values,
  the thread affinity calls: 45 system calls now.
- **`APT:U` / `APT:S` / `APT:A`** (`n3ds_apt.cpp`): the lock, the two events, Enable -> the wake-up parameter an
  application waits for; no HOME menu, no sleep, no applet. **`hid:USER` / `hid:SPVR`** (`n3ds_hid.cpp`): the
  shared page's pad and touch rings, written each frame from `Machine::setInput`. **`fs:USER`** (`n3ds_fs.cpp`):
  the RomFS opened as one file (file sessions: Read, GetSize, Close); no SD card (said absent).
- `n3dstest`: `N3DS_TRACE=1` (the system calls and every request with its answer), `N3DS_KEYS`, `N3DS_TOUCH`,
  `N3DS_SHOTS` as `ndstest` has.
- **Result**: *2048* (a public homebrew, MIT, the text console) **runs on both screens and plays** (the d-pad
  moves and merges the tiles): T1's goal. Not emulated and asked by it: `ptm:sysm`, the SD card's archive.
- ***Snake*** (the user's first target) starts, goes through libctru's whole start-up, then stops itself
  (`svcBreak`) at `APT GetSharedFont`: it draws its text with the **system font** through **citro2d (the GPU)**.
  It needs phase T2 (the PICA200) and the shared font (our own BCFNT, planned in T5: to bring forward).
  *Cube Adventures* and *Mars* use the GPU too.

## 9f. Phase T2, first slice -- done (2026-10-09): the PICA200 in software, the system font; *Snake* plays

- **The shared system font, ours**: `tools/n3ds/mkfont.py` draws a BCFNT from **DejaVu Sans** (already in
  `third_party/`): 368 glyphs (ASCII, Latin-1, Latin Extended-A, punctuation, arrows, the buttons as a letter in a
  disc), the console's metrics (cells of 24 x 30, sheets of 128 x 32 in A4) -> `user/Emulators/n3ds/data/sysfont.bcfnt`
  (154 KB, committed; to ship with the app at T7). `Machine::setSharedFont` puts it in FCRAM where the console
  keeps it (past the application's 64 MB: the programs give the GPU the glyph sheets by their linear address) and
  turns its offsets into addresses; `APT GetSharedFont` hands the block, mapped "at 0" = at its own address
  (0x18000000). A user's own console font (a BCFNT) can be given instead; none of Nintendo's is shipped.
- **The PICA200** (`n3ds_pica.cpp`, ~700 lines, written from 3dbrew's register and shader pages): the command
  lists (register, byte mask, runs); the **vertex shader** interpreter (the whole arithmetic set, MAD, CMP, the
  flow: CALL / IF / LOOP / JMP on comparisons and uniforms; float, integer and boolean uniforms, 24-bit and 32-bit
  uploads); attribute buffers (12 loaders, the four types, padding, fixed attributes) and vertices given one by
  one; the output map; triangle lists, strips and fans; clipping (7 planes), culling, the viewport, the scissor;
  a rasterizer with perspective-correct interpolation; three texture units (RGBA8, RGB8, RGB5A1, RGB565, RGBA4,
  LA8, HILO8, L8, A8, LA4, L4, A4; the wrap modes; nearest texel); the **six combiner stages** with their buffer;
  alpha test, depth test, blending (all factors and equations) or the logic operations, the write masks; tiled
  colour (5 formats) and depth (16 / 24 bits) buffers. **The display transfer** (tiled -> linear, format
  conversion, flip, halving). `gsp::Gpu` now runs the command lists and the transfers.
- **Not done** (noted in `Machine::notes` when asked): lighting, fog, procedural textures, shadows, stencil, ETC1,
  the geometry shader, texture filtering and mipmaps, the texture copy, jumps in command lists.
- **Results** (under `n3dstest`, the pictures looked at): ***Snake* plays** -- the snake, the food, the score and
  "Game Over" in our font, the d-pad obeyed (it draws one game frame every 8 VBlanks: taken to be the game's own
  pace, not verified against a console). *Cube Adventures* shows its menu and runs. *2048* still right. *Mars*
  stops for want of `cfg:u`.
- **The GPU's own test** (`tools/tests/n3ds/src/gpu.c`, no library): a command list of its own with a vertex
  shader written as the GPU's machine words (7 entry points), read back from the tiled colour buffer -- flat and
  interpolated colours to the pixel (a rectangle's first and last pixels, none further), MAD with a 24-bit
  uniform, CMP / IFC both ways, LOOP over indexed uniforms, MOVA, MIN / MAX / RCP / SGE, RSQ / FLR / MUL / DP3 /
  SLT, CALL; an RGBA8 texture's texels (replace, modulate), an A4 texture with a constant colour, blending, a logic
  operation, the colour mask, the depth test, culling, the scissor's edges, vertices given one by one, indexed
  triangles, a fan; the display transfer. **51 checks, 0 failed**, the picture's checksum in
  `tools/tests/n3ds/expect/gpu.crc`. One unit is allowed where a colour of exactly one half is expected (127 or
  128: what the console does there is not known to us). The expected values were worked out from the GPU's rules
  before the first run: 46 of 51 passed at once, the 5 others were that half.
- **Not covered by it**: the other texture formats and wrap modes, strips' winding, clipping, the other blend
  factors and depth formats, the display transfer's flip / scaling / other formats, uniforms' boolean flow (IFU,
  JMP), DP4's use aside from the position.

## 9g. Phase T2, second slice (2026-10-09): *Mars* runs -- cfg, ptm, the procedural texture, a loader fix

- **A fault of the `.3dsx` loader, found by *Mars*** (a C++ program: position-independent references from its code
  to its data): each relocation table of a segment walks the segment **from its start**; ours went on from where
  the table before had stopped, so the relative words were left as they were. *Snake* and *2048* have none in
  their code.
- **`cfg:u` / `cfg:s` / `cfg:i`** (`n3ds_cfg.cpp`): the region (Europe), the model (an Old 3DS), the settings blocks
  (the language and the user's name from `Machine::setUser` -- the host's --, the birthday, the country, the sound's
  output, the EULA...); an unknown block is refused and noted. **`ptm:u` / `ptm:sysm`**: the lid open, the battery
  full, no steps.
- **The procedural texture** (the GPU's unit 3; `n3ds_pica.cpp`): the coordinates' clamps, the ten combinations,
  the colour and alpha maps (128 entries with their slopes), the colour table, a separate alpha. citro2d tints
  every picture through it: without it *Mars*'s sprites were black on black. Not done: its noise, its filtering.
- `N3DS_TRACE=1` also says the system calls that fail and every access to memory that is not there (with the
  program counter): what found the loader's fault.
- **Result**: *Mars* shows its studio logo, its title, its instructions (its own font from its RomFS, its
  sprites), and goes on at each press of A. A thin diagonal line on its bottom screen: its own or a fault of
  ours -- not known.

## 9h. Phase T5 begun (2026-10-09): a game loads -- *A Link Between Worlds* reaches its first pictures

- **The game loader** (`n3ds_loader.cpp`): NCSD (a cartridge's image: its first partition) and NCCH -- the header
  (an encrypted dump is refused with a clear message: no key is in Onyx), the extended header (the name, the three
  segments, the stack), the ExeFS's `.code` unpacked (the backwards LZ, "BLZ"), the RomFS given from its third
  level. A program now comes from a **`Source`** the host reads pieces of (`Machine::loadFrom`): a game of 1 GB is
  not loaded in memory -- its RomFS is read as it asks.
- **The services a game opens**: `dsp::DSP` (`n3ds_dsp.cpp`: LoadComponent, the audio pipe's "initialize" and its
  answer -- where the 15 shared structures are, **at places of ours** --, the address conversion, the interrupt and
  semaphore events at every audio frame of 4.9 ms; **no sound yet**: the sources are not played); and 49 others
  that exist on a console and are not emulated (`ndm:u`, `cecd:u`, `ac:u`, `frd:u`, `boss:U`, `y2r:u`, `ldr:ro`,
  `csnd:SND`, `mic:u`, `cam:u`, `nwm::UDS`...) are **opened and answered "done, nothing"**, each command noted (a game
  stops at once when one cannot be opened; a homebrew's unknown service is still refused).
- **What a program writes** (`n3ds_fs.cpp`, rewritten): save data (archive 4: "not formatted" until the game
  formats it), extra data (6), the system's shared extra data (7: always there), the SD card (9) -- a small file
  system in memory (files and folders by their full names; Create / Delete / Rename, folders listed, files read,
  written, resized). The host stores it whole beside the game: `Machine::storageExport` / `storageImport`,
  `storageDirty` (not wired in `n3dstest` yet: nothing is kept between two runs).
- **The GPU**: a command list goes on in another buffer (the jump registers): games build their frame from such
  pieces -- without it the screen stayed black.
- **Result** (the user's dump, EU, CTR-P-BZLP, under `n3dstest`, 600 frames in 7 minutes of qemu): the game runs its
  start-up, its sound's handshake, reads its settings and its RomFS, opens its saves, and **runs its main loop**:
  5993 command lists, 1724 display transfers. From about the 450th frame **both screens show the game's dark red
  ornate panel**. Wrong or missing, seen: the first 400 frames are black (its logos?), the top screen's picture is
  shifted right by 80 pixels, lighting is asked (not done), ETC1 textures (not done: most of a game's textures).
  Asked and not emulated: `ndm:u` 0006 / 0007 / 0014, `ptm:u` 000B, `cecd:u` 0012.

## 9i. The game's first frames looked into (2026-10-09): three faults of ours found by tracing

`N3DS_GPUTRACE=<frame>` (new: each draw of that frame -- its state, its first vertices in and out of the shader, the
triangles that reach the screen, the pixels refused by the depth and alpha tests, what is written at the screen's
centre --, the shaders' code and uniforms, the GX commands) and `tools/n3ds/picadis.py` (lists a traced shader as
text) found why *A Link Between Worlds* drew next to nothing:
- **Culling was the wrong way round** (`n3ds_pica.cpp`): mode 2 removes the clockwise triangles (the "back": what
  every game sets), mode 1 the counter-clockwise ones; we did the opposite, and our own test had the same mistake
  (corrected: it now uses mode 2). The homebrew, with no culling, did not show it.
- **The stereo camera's calibration** (`cfg` block 0x00050005) was nearly all zeros: the game builds its projection
  and its view from it and sent the GPU matrices of "not a number". Eight plausible values now (the eyes' distance,
  the screen's size and distance...).
- **0 times infinity is 0 on this GPU** (the shader's MUL, DP3, DP4, DPH, MAD), not "not a number".
- Also: ETC1 and ETC1A4 textures; `n3dstest`'s `N3DS_SAVE=<file>` keeps what a program writes between two runs.
- **Where the game is now: its title screen.** The black rectangle over the scene at the 520th frame was the game's
  own fade. Run 1000 frames (`N3DS_SHOTS`, a picture every 40): the Triforce's three pieces fly in and join, the
  town appears behind, then **the logo "The Legend of Zelda -- A Link Between Worlds", "(c) 2013 Nintendo" and
  "Press A" on the bottom screen**. Wrong, seen: the scenery is pale grey-violet and speckled, the Triforce plain
  white before it turns gold -- **lighting** is asked and not done (the scenery's colours come from it), fog
  neither. Not tried: pressing A, the file selection, a save, the game itself.

## 9j. The title screen's colours (2026-10-10): lighting, and the combiner buffer's delay

The user saw the scenery's colours were wrong (pale grey-violet, then too dark once lighting was in) and gave a
picture of the real game as the palette to reach.
- **Fragment lighting** (`n3ds_pica.cpp`): the normal from the shader's quaternion, the view vector, up to 8 lights
  (ambient, diffuse, two speculars, the distance's attenuation), the tables D0 / D1 / reflection / Fresnel with their
  inputs, absolute values and scales. Not in it: spot lights, bump mapping, shadows, the geometric factors.
- **The combiner's buffer is two stages late**: a stage that updates it is seen by the stage after the next (the
  first stage sees nothing, the second the buffer's colour register). Ours was one stage early, so the material's
  "light x texture" stage read the wrong thing: found with the trace (a grass texel read right, the result black).
- **Result**: the title screen has the game's colours -- green grass, blue water, brown paths, grey stone, the
  Triforce gold with its dark twin. And after it (a run with A pressed): **the file selection**, "Start with this
  file?", the game formats and writes its save (17.6 KB kept by `N3DS_SAVE`); choosing *Rename* calls the system's
  software keyboard (`APT` 0x18: an applet we do not have -- to write: Onyx's own).
- **Not verified**: the camera's path over the map (the user expects Link's house in it; a run with a picture
  every 30 frames is to be looked at with him), the timing against a console. Still not done: fog, texture filtering.

## 9k. The core on the Pi 4: first speeds (2026-10-10)

`run_n3ds_test.sh` also links `n3dstest` as an Onyx program (`$N3DS_BUILD/n3dstest.elf`; its arguments from
`kapi_get_args`, the font as a 4th argument, `-` for no picture; it prints the frames a second). Sent to the user's
Pi 4 by FTP and run from telnet, **no display, one core** (the machine and the GPU's software renderer together):

| Program | Frames a second (the console: 59.8) | What it draws |
|---|---|---|
| 2048 | 190 | the text console: no GPU |
| Snake | 116 | a few small rectangles and text |
| Cube Adventures | 63 | a menu: some rectangles |
| Mars | **9.6** | full-screen textured layers on both screens |

The same pictures' checksums as under qemu. **The processor is not the limit (Dynarmic on the Pi: fine); the
software renderer is**: about 140 ns a pixel through the general path (six combiner stages, the procedural
texture, per-pixel floats). *A Link Between Worlds* draws ~900 000 lit pixels a frame: a few frames a second at
best this way. **Phase T3 is the next need** -- the fragments on the V3D (the study's plan), or first a much
faster software path (stages compiled per draw, integer spans, the renderer on the second app core). The game
itself was not run on the Pi (its ROM is on the PC).

## 9l. Phase T3, first slice (2026-10-10): the software renderer three times faster, on two cores

Still the software renderer, the pictures' checksums unchanged at every step (the three test programs, Mars's 120
frames `acc3db37`, the game's first 600 frames `9c6e43e7`):

- **A draw's state worked out once** (`State`: the target, the fragment's rules, the culling), kept until a
  fragment register is written; what the combiner's stages really read (`useTex`, `useProc`, `useLight`,
  `needPrimary`, `stageEnd`, `usesBuffer`) -- the rest is not computed; a fragment that simply replaces what is
  there (`replace`) is written without reading the buffer.
- **The triangles queued and rasterized at the list's end by several host cores**, band by band of 8 rows (a
  band is a row of the buffer's 8 x 8 tiles): a worker takes the next band nobody has (`bandNext`, an atomic
  counter) and draws there the queued triangles that reach it, in their order -- nothing is shared, the order
  within a pixel is kept, and a slower core simply takes fewer bands. What is culled or outside is not queued
  (`triBox`, the one function the queue and the rasterizer ask). The work given to the other cores calls nothing
  and allocates nothing (an Onyx app core can do neither): each has its `Scratch`, made beforehand.
  `Machine::parallelBegin / parallelDone / parallelEnd` are the host's: `n3dstest` on Onyx takes the free
  application cores (`kapi_core_acquire`); on the user's Pi one of the two was free (the other one belongs to a
  server), so: **two cores**, the main thread and one application core.
- **A list's triangles drawn while the program goes on**: at a command list's end the helpers start and the
  processor is given back to the program; the list's interrupt (P3D) is raised when the helpers are done
  (looked for every quarter of a millisecond of the program's time), at once when every thread waits (this
  thread then takes bands too), or before the GPU's next command (`Machine::gpuSync`). *A Link Between Worlds*
  waits for each list, so it gains nothing there -- a program that prepares its next frame meanwhile does.
  Under qemu `N3DS_ASIDE=<n>` plays it (the triangles drawn at the n-th question): the same pictures.
- **The right eye left out** (`Machine::monoOnly`, for a host that shows one picture a screen -- Onyx): the game
  draws its top screen **twice a frame**, the 3D slider down or not (the same lists again into the same buffer,
  sent to the right eye's framebuffer; saying "a 2DS" changes nothing). The transfers tell it: a buffer sent to
  a left framebuffer of the top screen, drawn into again, sent to the right one -- two frames in a row -- and
  from then on the draws into that buffer between the two transfers are not done (nor the second transfer). A
  frame that does otherwise ends it (one picture may be wrong); three such frames and it is not tried again.
  The left picture is the same to the bit; **the title scene: 5.1 -> 9.4 frames a second**.
- **Textures decoded once** into plain pixels (96 kept, found by where the program's bytes are and a checksum of
  samples, made again when they change): a texel is one read, ETC1 included.
- **Fragments shaded by groups of 32** (`CH`): the fragments that passed the depth test are gathered row after row
  (a row is walked only where it can be inside the three edges, the exact test staying the judge),
  then each input (the vertex colour, the texels, the lighting, the procedural texture) and each combiner stage
  is worked out for the whole group in one loop over plain rows of integers -- loops GCC turns into NEON --, a
  stage's choices (its sources, operands, mode) being made once a group. A source is a pointer to rows, a stage
  writes rows of its own: the buffer's two-stage delay is a matter of pointers.
- **Blending by rows too**: what is in the buffer is read for the group, the factors are rows (or pointers to
  rows), the equation one loop a channel.
- **A vertex shaded once a draw**: an indexed draw names a mesh's vertices several times (the game: 3.0 M
  vertices for 1.0 M triangles); the shaded ones are kept by their index for the draw's time (`Shaded`, 1024
  slots): 1.15 M shader runs instead of 3.0 M.
- **The vertex shader's instructions decoded once** (`ShaderIns`: the operands' shuffles, signs and the written
  components as vectors) and worked out four components at once (GCC's vectors: NEON). The gain is small -- about
  30 ns an instruction either way, the dispatch's mispredicted branches: **only compiling the shaders will make
  them fast** (the game runs ~95 instructions a vertex).
- **Fills and display transfers**: direct paths for the usual formats.
- The core says where its time goes (`Machine::gsp.usLists / usRaster / usTransfers / usFills`, the vertices, the
  triangles, the pixels), and `gpuSkip` leaves parts of the renderer out to time them.

On the Pi 4 (`n3dstest`, no display, two cores):

| Program | Before | Now | The rasterizer's part |
|---|---|---|---|
| Mars, 120 frames | 9.6 fps | **28.8 fps** | 2.0 s for 21.8 M pixels |
| *A Link Between Worlds*, its first 600 frames (to the title screen) | not measured (11.1 fps midway through this slice) | **17 fps** (35.2 to 35.6 s) | 17.1 to 17.5 s for 255 M pixels |

Where the game's 35.6 s go: **the rasterizer 17.5 s** (measured by leaving parts out: finding the fragments and
their depth test 3.8 s; their inputs 7.6 s -- the vertices' values, the lighting ~3.9 s, the texels 0.6 s --; the
combiner, the blending and the writing 6.2 s), the vertices 5.0 s (their shader: ~3.5 s), the display transfers
3.3 s, **everything else 9.5 s** (the processor, the system, the card: with no pixel drawn at all the 600 frames
still take 21.6 s, 27.7 frames a second). **So the next steps, in the order of what they give**: the GPU's command
lists on the application cores while the processor goes on (today the processor waits for each list: the two
times add up); both application cores when they are free; the lighting cheaper (per pixel today); the vertex
shaders compiled; and the V3D for the fragments (the study's plan) -- the software path alone will not reach the
console's speed in this game.

**The title scene itself** (frames 900 to 1200: the camera over the castle, the logo; `n3dstest`'s 7th argument
is the frames to run before the time and the counters are taken) is much heavier than the 600 frames before it:
with one eye, **31.5 s for 300 frames (9.5 a second)** -- the rasterizer 18.2 s on two cores (605 000 pixels a
frame, six times the screen: three full-screen layers, the lit ground with six combiner stages; the lighting
alone 5.1 s, finding the fragments 3.2 s), **the vertices 9.9 s** (8 700 a frame after sharing, 88 shader
instructions each: ~40 ns an instruction, on one core), the transfers 1.2 s, the processor 2.3 s. Next there: the
vertex shader compiled or at least run on both cores, the lighting worked out by rows, the full-screen layers
of one colour drawn without the general path.

**A kernel fault found on the way** (and why some runs on the Pi stopped with only their first line): after each
upload of a rebuilt `n3dstest`, the application core faulted at places the new code cannot fault at -- it was
running the OLD binary's instructions. The loader empties the instruction cache of core 0 only (Circle's
`SyncDataAndInstructionCache`: `ic iallu` is local to a core), and a program loaded again gets the frames the
last one had. Two remedies: the kernel empties an application core's instruction cache when a job starts
(`kernel/sys/appcore.cpp`, `LocalICacheFlush` -- **in the source, not yet in a published kernel**), and until
then a program that uses the cores declares its code new to all of them before starting one (`n3dstest`'s
`codeFresh`: `__builtin___clear_cache` over its code region, whose `ic ivau` reaches every core). The runner
also gives core 0 back while it waits for a core (`kapi_yield`: the pager must be able to run) and says when a
core's job stopped (`kapi_core_state`), instead of waiting for ever.

**Calibration**: the DS took D0–D5 in one long session (~7 k lines); this is ~3× bigger with a
harder GPU and an OS — **several sessions**, then the user's tests on the Pi as for the DS.

## 10. Tests

- **Own test programs** built with `arm-none-eabi-gcc` and a minimal `.3dsx` start-up of ours
  (no Nintendo SDK): CPU (ARMv6K media ops, VFP: differential against a PC reference, as the DS's
  `cputest`), kernel (threads, events, IPC), PICA (triangles, combiners, lighting, textures:
  pictures compared with the software renderer), DSP (tones).
- **libctru** (zlib licence) homebrew and public-domain 3DS homebrew (demos, games) — not
  committed, fetched by the tests as for the DS.
- A **JIT differential fuzzer** as `tools/tests/nds/jitfuzz` (Dynarmic or ours against an
  interpreter) under qemu-aarch64.
- The desktop simulator for the app's screenshots; the user's own decrypted dumps on the Pi for
  compatibility.

## 11. Risks and open questions

1. **Dynarmic on Onyx** (T0): **settled** (section 9b: it builds unpatched and runs on the Pi; built with
   exceptions, linked into a `-fno-exceptions` program). Left: its speed through the page table, without fastmem.
2. **V3D 4.2 shader limits** for the fragment lighting (8 lights, LUTs): long programs; split
   passes or a simplified path.
3. **Compatibility long tail**: services and SVC corner cases (the reason Citra took years);
   prioritise the user's own game list.
4. **Geometry shaders** (a few games): in the CPU vertex path, it is just more JIT'd code.
5. **Memory**: 128 MB guest + caches: fine on a 4 GB / 8 GB Pi 4; a 2 GB Pi 4 to check.
6. **Legal**: decrypted dumps only, no key handling code at all, own replacement font — to state
   in the app's messages and `docs/04`.
7. **The user's answers (2026-10-09)**: (a) **option C** (Dynarmic, the app stays MIT) -- agreed; (c) **Old 3DS
   only** to begin with -- agreed; (b) which games first: not said yet (the user asked how *The Legend of Zelda:
   A Link Between Worlds* would run -- an Old 3DS game, light top-down scenes but aiming at 60 fps: a likely first
   target and a good speed yardstick, to confirm).

## 12. Sources

- 3dbrew wiki (hardware, services, file formats, PICA200 registers): https://www.3dbrew.org/
- GBATEK's 3DS sections (Martin Korth): https://problemkaputt.de/gbatek.htm
- Azahar (GPL-2.0, Citra's successor; Android floor Snapdragon 835, GLES 3.2): https://github.com/azahar-emu/azahar
- Panda3DS (GPL-3.0, HLE, Dynarmic, OpenGL / Vulkan, decrypted or encrypted dumps): https://github.com/wheremyfoodat/Panda3DS
- Dynarmic (0BSD; ARMv3–v8 guests, x86-64 and AArch64 hosts; C++20): https://github.com/azahar-emu/dynarmic
- Teakra (MIT; the XpertTeak DSP): https://github.com/wwylele/teakra
- libretro's Panda3DS core options (shader JIT on x86-64 / ARM64): https://docs.libretro.com/library/panda3ds/
- Onyx: `docs/HANDOFF.md` (gcemu's speed analysis, the DS emulator), `user/Libs/v3d/gxtev.h`, `user/Emulators/nds/`.
