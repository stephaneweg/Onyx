# Onyx: an app runner for the PC, with the V3D bridged — a feasibility study

*Status (2026-10-09): **a study, nothing built.** Asked by the user: since we know what Onyx expects
of the hardware, how hard is a Pi 4 emulator for Windows that loads the binaries unchanged, to speed
the tests up — and then: an **app runner** that also bridges the GPU to simulate the V3D? Facts
marked **[repo]** were read in this repository, **[memory]** are from knowledge and were not
re-checked — verify them before building on them. Answer the user in French; this page stays in
English.*

## 1. Short answer

- **A whole Pi 4 emulator written from scratch: no.** An ARMv8 CPU with a JIT plus every device is
  months to years of work, for less than QEMU already gives. If a full-system emulator is wanted one
  day, it is **a fork of QEMU's `raspi4b` machine** (section 8) — its big missing pieces are the
  PCIe bridge + the VL805 xHCI (the USB: keyboard, mouse, gamepads), GENET and the V3D.
- **An app runner is the sweet spot**: the apps' and the kits' **Pi binaries, unchanged**, run on the
  PC against a host-side stand-in for the kernel (the kapi table), Elegant included. Most of it
  already exists in pieces (section 2).
- **The GPU bridges well, because no app touches the V3D's registers** **[repo]**: everything goes
  through ten kapi calls (`kapi_gpu_*`, section 4). The runner implements those calls; it does not
  emulate the chip. The apps' own QPU shaders (`gpu_program`: gcemu's TEV, n64emu…) run in
  **`tools/qpu/qpusim.cpp`**, the QPU simulator the tests already use.
- **What it does not replace**: the kernel (not run — except the V3D driver in variant G3), the
  weak memory ordering (an x86 host hides missing barriers), the caches (a missing cache clean before
  a DMA passes), the timing. The Pi stays the reference; the runner makes the functional tests fast.

## 2. What already exists [repo]

| Piece | Where | What it does |
|---|---|---|
| `elfrun` | `tools/tests/desktop_sim/elfrun.cpp` (49 lines), `elfrun.sh` | An app's own Pi ELF run under `qemu-aarch64` on Linux: its `PT_LOAD` segments mapped at their addresses, a stack with a guard page, the desktop simulator's kapi table at `KAPI_TABLE_VA`. Used to find the 1060-pixel window freeze (docs/HANDOFF.md). |
| desktop_sim's `fakekapi.cpp` | `tools/tests/desktop_sim/` (1 779 lines) | A host kernel for uikit apps: files from `sdcard/`, a window canvas, a script of input events, window dumps (`.elsm`) composed by `compose.py`. Its `gpu_info` answers "no GPU (the simulator)". |
| posixsim's `fakekapi.c` | `tools/tests/posixsim/` (2 278 lines) | A kapi of raw Linux system calls under `qemu-aarch64-static`: threads (`clone`), files, sockets, `poll`, spawn / wait, v76's IPC (AF_UNIX + `SCM_RIGHTS`), shm (memfd). |
| `server_sim` | `tools/tests/server_sim/` | Elegant's / PocketUI's shared code (`user/Servers/common/`) built for the host with a real app as its client: the `kapi_ws_ctl` side, the shared buffers at `KAPI_WS_VA_*`, the routing of the input. |
| `qpusim` | `tools/qpu/qpusim.{h,cpp}` (486 lines) | A functional V3D 4.2 / 7.1 QPU simulator: 16 lanes, accumulators and register file, the ALU operations the shaders use, flags and conditions, `ldvary`, `ldunif`, the TMU (2D RGBA8, nearest / bilinear, repeat / clamp / mirror), the SFU, the TLB writes, vertex shaders (`ldvpmv_in` / `stvpmv`). **Not modelled**: branches, threads, VPM in general, general TMU access, timing. Used by `run_qpu_test.sh` (gxtev against its reference, the kernel's shaders). |
| The V3D packets | `kern/v3d_cl.h`, `kern/v3d_pack42.h`, `v3d_pack71.h` | Generated from Mesa's XML and checked bit by bit (`run_v3d_cl_test.sh`). |
| gpucomp's CPU path | `user/Libs/gpucomp/` | Already does every compositing call on the CPU when there is no GPU: the browser and its users never break without one. |

So the runner is mostly **a merge**: elfrun's loading, posixsim's processes and threads,
desktop_sim's and server_sim's GUI, plus two new parts: the kits' loading and the GPU.

## 3. The runner

```
 Windows / Linux host
 ┌───────────────────────────────────────────────────────────────────────┐
 │  onyxrun (host program)                                               │
 │   ├─ loader: program ELF at 8 GB, appkit.so + the kits at 16 GB+      │
 │   │          (R_AARCH64_RELATIVE applied, as kernel/proc/image.cpp)   │
 │   ├─ kapi table at KAPI_TABLE_VA: files (SD:/ → sdcard/), threads,    │
 │   │   processes, IPC, shm, sockets, the window server's channel, GPU  │
 │   ├─ GPU bridge: kapi_gpu_* → software V3D (qpusim) / host GPU        │
 │   └─ screen: one host window showing Elegant's composed screen,       │
 │              the host's keyboard / mouse / gamepad sent to Elegant    │
 │  CPU: the aarch64 code run by qemu-user (WSL2) or Unicorn (native)    │
 └───────────────────────────────────────────────────────────────────────┘
```

### 3.1 The CPU

- **First: WSL2 + `qemu-aarch64`** — the path elfrun and posixsim already take, and Linux gives
  the address space for free (the guest's addresses are the host's: a pointer an app passes to a
  kapi call is usable as it is). Every Onyx process is a qemu-user process; the runner's processes
  talk through AF_UNIX as posixsim's do. A host window from WSL2 goes through WSLg **[memory]**.
- **Later, if wanted: native Windows with Unicorn Engine** (QEMU's TCG as a library) **[memory]**:
  one host process, the guest memory mapped by the runner, every pointer a kapi call receives
  translated (guest → host). More work (every kapi structure with pointers inside: `kapi_gpu_frame`,
  the batches…), but a single `.exe`. Unicorn is GPL-2.0: fine for a development tool that is not
  distributed with Onyx (docs/LICENSING.md), to note there if it is used.
- Either way the JIT emulators (gcemu, ndsemu) work but slowly: the AArch64 code they generate is
  translated again by TCG (the qemu tests already do it: `run_gc_test.sh`, `ndstest`).

### 3.2 The loader [repo]

Since AppKit and the kits (docs/SHARED-LIBS-PLAN.md §0, §3), a program is an `ET_EXEC` at 8 GB that
reaches the kernel only through `SD:/lib/appkit.so`, and the kits are `ET_DYN` libraries placed in the
arena 16 GB..32 GB, with only `R_AARCH64_RELATIVE` relocations, their table returned by
`kapi_lib_open`. elfrun maps only the program's segments; the runner adds the library loading,
ideally **by reusing `kernel/proc/elf.cpp`'s plan** (`ElfReadPlan`) built for the host, as
`tools/tests/image/imagetest.cpp` already does, so the runner refuses what the kernel refuses.

### 3.3 The kapi

posixsim's table (threads, files, sockets, IPC, shm) and desktop_sim's (windows, events, chrome,
dumps) are merged into one, and the window server's side taken from server_sim: **Elegant itself
runs as an Onyx binary** in the runner (`SD:/bin/elegant`), the apps are its clients over
`kapi_ws_ctl` and the shared buffers at `KAPI_WS_VA_CANVAS` / `FRAME` / `FRAME_OFF`, and the runner
shows the screen Elegant composes in one host window, sending it the host's input as the kernel
does. Sound (`COnyxSoundDevice`'s kapi) goes to the host's audio output; the network to the host's
sockets (posixsim does).

What is left out at first: GPIO / SPI (`gpiokit`: a "no device" answer), the USB device classes
(MIDI…), the kernel's own console.

## 4. The GPU bridge

### 4.1 The surface to implement [repo]

`user/Kits/appkit/appkit_calls.inc`, `kern/kapi_abi.h`:

| Call | Since | What the runner does |
|---|---|---|
| `gpu_info` | v52 | "V3D 4.2 (runner)" — or 7.1 to test the Pi 5's paths (`KAPI_GPU_P_V71`) |
| `gpu_draw` | v52 | triangles with the kernel's stock shaders, into the caller's pixels |
| `gpu_texture`, `gpu_texture_rect` | v53, v70 | the textures kept per process (handles), RGBA as the kernel lays them |
| `gpu_render` | v53 | batches of `kapi_gpu_vertex3` with the stock shaders, clipped as the kernel does (`ClipFrame`) |
| `gpu_program` | v61 | the app's own QPU code (`vs`, `cs`, `fs`, ≤ 4 096 instructions each) checked and kept |
| `gpu_render2`, `gpu_render3` | v61, v62 | batches drawn with those programs, their uniforms, ≤ 8 textures each, `render3`'s framing |
| `gpu_vbuf` | v63 | memory the GPU reads too: plain guest memory in the runner (with `render3`'s in-place framing and clipping at the buffer's end) |

The users **[repo]**: gcemu, n64emu, 3dforge, teapot, gpudemo, BASIC's 3D, gpucomp (so Jet's
compositing), `/bin/v3dprog`.

### 4.2 The ways, in order

- **G1 — the stock shaders in software.** `gpu_draw` / `gpu_render` / `gpu_texture*` done by a
  small rasteriser on the CPU, reproducing the kernel's pipeline (`kernel/sys/v3d.cpp`: the clipping
  on the CPU, the colour / texture fragment shaders, the alpha `KAPI_GPU_F_ALPHA`). Simple, about a
  week; it lights gpucomp's GPU path, teapot, gpudemo, 3dforge.
- **G2 — the apps' shaders in qpusim.** `gpu_program`'s code run by qpusim: the vertex / coordinate
  shader on the vertices (16 at a time), the rasteriser interpolating the varyings `ldvary` reads
  (with the C term), the fragment shader on quads of 16 pixels, the TMU on the bound textures, the
  TLB writes into the target, the depth (`KAPI_GPU_P_FS_ZWRITE`). **Faithful** — the same simulator
  the tests trust — **but slow**: a few frames a second at 1024 × 768 (an estimate), enough to test,
  not to play. To add to qpusim: what the apps' shaders use and it lacks (branches, if `qpu.h` emits
  them; the thread switches as sections; the 4-way / 2-way register split). Two to four weeks.
- **G3 — the kernel's driver too (optional).** `kernel/sys/v3d.cpp` itself (2 024 lines) built
  for the host against a **fake V3D**: its register writes and its control lists interpreted —
  only the packets Onyx emits (`v3d_pack42.h` / `v3d_pack71.h`), not the whole chip —, the shaders in
  qpusim. Then the driver is tested as well: the bugs of the "stride three bits low" kind that
  `tools/tests/v3d/cl_test.cpp` recounts show up on the PC. More work than G2 (the binner's tile
  lists, the render list, the tile buffer's loads and stores, the interrupts as completions), maybe
  four to six weeks on top; worth it only if the driver changes often.
- **G4 — the host GPU, for speed (later).** The stock shaders and gpucomp's draws translated to
  OpenGL / Direct3D 11: fast, close to the eye, not exact to the pixel (the rasterisation rules, the
  precision, the filtering). The apps' QPU shaders on the host GPU need a **QPU → GLSL / HLSL
  recompiler** (the registers and accumulators into SSA, `ldvary`, the TMU writes and reads, the
  thread sections); feasible because `qpu.h` produces restricted patterns, but several weeks and a
  risk of subtle differences — G2 stays the reference to compare it against.

**Recommended: G1 then G2** — the runner is a test tool, exactness before speed. G4 only if playing
gcemu / n64emu on the PC becomes a goal; G3 only for work on the driver.

## 5. What it will not show

- **Memory ordering**: the guest's AArch64 code runs on an x86 host, which is strongly ordered
  (TSO): a missing barrier between two threads or two cores does not fail there.
- **Caches**: not emulated; a missing cache clean / invalidate around a DMA or a GPU job passes.
- **Timing**: nothing is cycle-exact; timeouts, races and the watchdog behave differently.
- **The kernel**: not run (the scheduler, the faults, the drivers, the FAT) — except the V3D driver
  in G3. Its own tests stay `tools/tests/run_*` and the Pi.
- **The GPU's limits**: a shader the Pi's QPU would reject for a timing restriction is checked by
  `qpu.h`'s rules (`run_qpu_test.sh`), not by the runner's execution.

## 6. The plan

| Phase | What | Estimate |
|---|---|---|
| R0 | `onyxrun` under WSL2: elfrun + the kits' loader (`ElfReadPlan` for the host) + posixsim's kapi; a console program (`/bin/*`) runs unchanged | 3–5 days |
| R1 | One uikit app unchanged, the desktop_sim kapi merged in, its window in a host window (SDL2, zlib licence), the host's mouse and keyboard | 1 week |
| R2 | Elegant run as its own binary, several apps as its clients (server_sim's channel), the session as on the Pi | 1–2 weeks |
| R3 | GPU G1 (stock shaders in software): gpucomp, teapot, gpudemo, 3dforge | 1 week |
| R4 | GPU G2 (`gpu_program` in qpusim, qpusim's gaps filled): gcemu, n64emu, BASIC's 3D | 2–4 weeks |
| R5 | Sound and network to the host; gamepads (host gamepads → `gamepad.h`'s events) | 1 week |
| R6 | A test bench: scripted runs of every app (the `SIM` scripts), screenshots compared | 1 week |
| R7 | (optional) native Windows with Unicorn; G3; G4 | each several weeks |

Estimates, not measurements.

## 7. Questions for the user

1. **WSL2 first, or a native Windows `.exe` from the start** (Unicorn, more work, GPL-2.0 tool)?
2. **The GPU: exactness (G2) first, or speed (G4)** — to test, or also to play the emulators on the PC?
3. **G3** (the kernel's V3D driver against a fake V3D): wanted, or left for later?

## 8. For the record: the full-system emulator

If the kernel itself must one day run on the PC, the way is a fork of QEMU's `raspi4b` machine
(QEMU ≥ 9.0 **[memory]**; it builds on Windows with MSYS2). It has the four Cortex-A72 cores (TCG,
multi-threaded), the GIC-400, the timers, the UART, the mailbox and its framebuffer, the EMMC2
**[memory]**; it lacks, for Onyx: the **PCIe bridge + VL805 xHCI** (the USB: keyboard, mouse,
gamepads, USB sound — weeks; QEMU's `qemu-xhci` exists, the Pi's PCIe bridge and the firmware's
VL805 notification do not), **GENET** (the Ethernet, about 1 500–2 500 lines), the sound, and the
**V3D** (to leave absent: gpucomp already falls back to the CPU, the other GPU apps say "no GPU"). Phase 0 would be to
boot `kernel8-rpi4.img` as it is and see where it stops — not tried. docs/03 notes there is no QEMU
`raspi4b` in the reference development environment.
