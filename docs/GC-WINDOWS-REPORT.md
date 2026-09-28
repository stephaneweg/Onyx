# GameCube on Windows — the report (for the Pi port)

What a local session on the user's Windows PC changed to take **The Legend of Zelda: The Wind
Waker** (PAL, GZLP01 — the user's own ISO, local only, never committed) from a black screen to
**playable**: title screen, memory card save created, name entry, the intro story, Outset Island
in game, with its **sound** and its **pictures drawn by the GPU**. Everything was tested in
NintendoEMU and in the headless runner `tools/tests/gc/gcrun.cpp` (Windows, MinGW-w64).

Almost all of it is in the shared core (`user/gc/`), so the Pi's `gcemu` gets it by being
rebuilt; §4 lists what the Pi app itself still needs, and §5 the options for the rendering.

## 1. Where Wind Waker stands

| | Before | Now (NintendoEMU) |
|---|---|---|
| Boot | black screen, `DVD 0 reads` | Nintendo / Dolby logos, title screen with its sparkles |
| Memory card | none ("no memory card" loops) | slot A, formatted on first use, save file created, `<game>.sav` |
| Menus | the stick moved two cells a tap | one cell a tap (the game ran twice too fast, see §2.9) |
| After the intro story | black screen (DVD reads stuck at 2840) | Outset Island, Aryll, the lookout, dialogs, HUD, map |
| Sound | none | music, voices, effects (the "Zelda" DSP microcode rendered) |
| Picture | CPU transform + TEV per vertex (grey silhouettes, white tiles) | the GX pipeline in GL 3.3 shaders (cel shading, fog, EFB-copy effects, 1–4× resolution) |
| Speed (this laptop: Intel UHD + NVIDIA T1200) | ~15 % (interpreter) | real time (paced by the sound); headless (`gcrun`), 2× resolution: Intel UHD ~80–160 fields/s on the title, ~100 on the island; NVIDIA 150–250 |

## 2. The fixes, in the order the game needed them

1. **The IPL's state** (`gc_hw.cpp` `hwReset`): the VI registers the IPL leaves (HTR0/1, VTO/VTE,
   BBOI/BBEI, the two display interrupts DI0 = line 313/263, DI1 = line 1: VIInit keeps them),
   the DSP halted with DSPINIT set (CSR = 0x804), AR_MODE = 1 (ARInit waits for it),
   AR_REFRESH = 156, AICR = 0x40 (the DMA at 32 kHz), EXTINT on EXI channels 0 / 1.
2. **SRAM read by EXI DMA**: the IPL chip's DMA now goes through `exiIpl` in 4-byte chunks (the
   SDK reads the SRAM with a DMA; the cards' serials and the language come from it).
3. **SI**: COMCSR's COMERR is not writable (it was sticking and the pad init looped), RDSTINT /
   RDST / NOREP / ERRSTAT as the hardware, `siUpdate`, a direct-command (0x40) path.
4. **The DSP** (`gc_dsp.cpp`, Dolphin's HLE as the reference): the mail queue (a mail's IRQ is
   raised when the one before it is read; mails hidden while halted; the low half's read pops),
   the ROM boot by mail keys, the microcode identified by its CRC (ector's: `crc ^= b; crc =
   crc << 3 | crc >> 29`) with the Zelda variants' flags (light protocol, sync per frame, tiny VPB,
   four destinations…), the AX protocol (0xBABE, DSP_YIELD, CDD1), the Zelda protocol (DSP_INIT +
   0xF3551111, command batches acked with DSP_SYNC + 0xF355xxxx, sync mails per 16 voices,
   DSP_FRAME_END), the task switch (MAIL_NEW_UCODE + 10 mails, DSP_RESUME of the saved
   microcode), the card microcode (0xFF000000 + address → DSP_DONE). CSR writes: the interrupt
   bits acked, RES resets the DSP and stops the audio DMA, DSPINIT 1→0 runs the init microcode.
   ARAM DMA: above 16 MB is the (absent) expansion (reads 0), the address / count registers
   advance after a transfer.
5. **The memory card** (`gc_card.cpp`, new): see docs/03. A blank card is formatted with the
   console's serial so that the SDK accepts it; the SRAM's flash ID is set back from the card
   (Dolphin's trick: a card from another console is accepted).
6. **HID0**: ICFI / DCFI read back 0 — the game sets ICFI, and the JIT flushed every block.
7. **The AI**: each sample is an event while the AI plays (`__AI_SRC_INIT` polls the counter; the
   JIT's polling loops ran to the next event and skipped past it).
8. **The Zelda microcode's renderer** (`gc_zelda.cpp`, new — Dolphin's ZeldaAudioRenderer): the
   black screen after the intro was the game waiting for its streamed music to advance; with no
   voice rendered nothing advanced. Command 01 loads the resampling / pattern / sine / AFC tables
   and the VPB and reverb bases; command 02 renders frames into the game's two buffers.
9. **`runFrame` runs one field**, not a frame (`gc_hw.cpp`: its end line was wrong after the
   first call). The front ends call it 50 / 60 times a second: the game ran **twice too fast**,
   and a short key tap outlasted the game's menu repeat delay (the "one press moves two cells").
10. **The sound out**: the audio DMA's blocks resampled to the host rate (`setAudioRate`,
    `audioRead`), R L big-endian as the AX microcode writes them; the DMA's rate from AIDFR.
11. **The picture while loading**: an EFB → XFB copy records its address (`xfbCopyAddr`); while
    the VI shows one of those XFBs, the front end keeps showing the GX picture (`xfbIsCopy`).

## 3. The files

Shared core (`user/gc/`, built for the Pi too):

| File | Change |
|---|---|
| `gc.h` | DSP state (queue, microcode, Zelda mixer), audio ring, cards, GX-GPU interface (`GxVertex`, `GxState`, `GxCopy`, `GxGpu`), `texFind` hints, texture mip levels, `f32` |
| `gc_hw.cpp` | IPL state, SI, EXI (cards, IPL DMA), DSP registers, ARAM / audio DMA, `runFrame` per field, AI sample events, `audioBlock` / `audioRead` |
| `gc_dsp.cpp` | rewritten: the HLE above |
| `gc_zelda.cpp` | **new**: the Zelda audio renderer |
| `gc_card.cpp` | **new**: the memory card |
| `gc_gxgpu.cpp` | **new**: the GPU path (state → uniform block, indices, EFB copy slots) |
| `gc_gxdraw.cpp` | vertex decode shared by both paths (`GxVertex`), mipmaps, the texture cache's hint table, EFB copies to the GPU |
| `gc_gx.cpp` | dirty flags for the GPU state (`gxsDirty`, `xfSerial`) |
| `gc_cpu.cpp` | HID0 |
| `gc_mem.cpp` | cards / audio / gpu initialised |
| `gc_jit_x64.cpp` | **new**: the x86-64 JIT (not built for the Pi) |
| `gc_jit.cpp` | `#elif !defined(__x86_64__)` (the AArch64 JIT unchanged) |
| `user/Makefile` | `gc_card.o gc_zelda.o gc_gxgpu.o` added to `GC_OBJS` |

Windows only: `pc/NintendoEMU/core/gxgl.cpp` (**new**, the OpenGL 3.3 backend), `nemucore.cpp`
(sound, memory card, GX backend, `NEMU_GX_DUMP`), `GameForm.cs` (the high-performance GPU
option), `pc/build.sh` (+ `gxgl.cpp`), `tools/tests/run_nemu_test.sh` (+ `gxgl.cpp`),
`tools/tests/gc/gctest.cpp` (x86-64 hosts), `tools/tests/gc/gcrun.cpp` (**new**: the headless
runner).

## 4. What the Pi port needs (`user/Apps/gcemu/main.cpp`)

1. **Rebuild** with the new core: `GC_OBJS` already lists the new objects. Things to watch on
   AArch64: `gc_gxgpu.cpp` uses `offsetof` / `static_assert` (the std140 layout: 1200 bytes) and
   `__builtin_memcpy`; the `Machine` grew (the DSP queue, the Zelda mixer, `efbCopies`, `gxs`,
   the 64 KB audio ring) — it is `new`ed. Re-run `run_gc_test.sh` (the AArch64 JIT under
   qemu-aarch64, `gctest fuzz`): the JIT reads `Machine` fields by `offsetof`, nothing moved by
   hand, but check.
2. **The pacing** stays one `runFrame` per field (it already was: the game now runs at its real
   speed on the Pi too — half what it did, when the Pi could keep up).
3. **Sound**: `setAudioRate (<the kapi sound rate>)` before `loadDiscImage`, then after each field
   drain `audioRead (lr, n)` into the kapi sound queue, as `n64emu` does with its own ring
   (stereo s16, host order L R). `gc_zelda.cpp` runs on the app core with the rest of the machine
   (a few percent of a core).
4. **The memory card**: before `loadDiscImage`, allocate 2 MB (`Machine::CARD_SIZE`), fill it with
   0xFF, read `<game>.sav` over it if it exists (any power-of-two size from 512 KB to 16 MB: a
   Dolphin `.raw`), `cardInsert (0, flash, size)`; write the file back when `card[0].dirty` (every
   few seconds and on exit — like the other emulators' `.sav`), then clear `dirty`. Same file
   name and format as NintendoEMU, so a save moves between the PC and the Pi.
5. **Docs**: docs/04's `gcemu` row still says "no sound, no memory card yet" — update it once
   the app has them.

## 5. The rendering on the Pi ("y compris le rendu GL")

On Windows the GX is now drawn by `gxgl.cpp` through the `GxGpu` interface. The Pi's V3D has no
GLSL: its shaders are hand-written QPU code (`kernel/sys/v3d_shaders.qasm`, `tools/qpu/qpuasm`),
and `gcemu` draws the old `GFrame` path (the TEV evaluated per vertex on the CPU). The interface
is meant for a Pi backend too — `Machine::gpu` set by the app, called on the app core while the
machine runs; the backend can record the draws for the main core (the kapi GPU calls) and replay
them after the field. Options, from the most faithful:

- **A V3D backend with generated QPU fragment shaders**, one per TEV configuration, like
  `gxgl.cpp`'s specialized programs (`GxGl::program`: the key is the stages' registers, the swap
  tables, the alpha / fog / z-texture modes; the uniforms are the rest of `GxState`). The vertex
  stage can stay on the CPU (`gc_gxdraw.cpp` already transforms and lights: pass the two
  channel colours and up to 8 texture coordinates per vertex instead of c1 / c2). The TEV in
  integers is in `gxgl.cpp`'s `FS_HEAD` / `FS_STAGE` / `FS_END` (GLSL — the reference to port).
  EFB copies: render to texture (a V3D render target at the copy's size), as `GxGl::copy`
  (the format conversions in `COPY_FS`). Needs kapi support for custom fragment shaders
  (an ABI addition: docs/02's table and `KAPI_ABI_VERSION`).
- **Cheaper, on the existing kapi `gpu_render`**: keep the per-vertex TEV but fix its two big
  gaps — (a) Wind Waker's cel shading is a texgen from the lit colour (texgen type 2 / 3,
  `vT = (colour.r, colour.g)`) sampling a small ramp texture: evaluate that lookup per vertex on
  the CPU (the bands become gradients, but the colours are right instead of grey); (b) EFB
  copies to textures: without them the textures at those addresses are stale memory (white or
  garbage tiles) — at least skip such draws (`gpuTexture` knows the copies' addresses), or read
  back the V3D's picture into a texture.
- The software renderer (`bas::swTriangles`) cannot afford a per-pixel TEV at 640 × 528 on the
  Pi's cores.

## 6. Testing

- `tools/tests/gc/gcrun.cpp` (Windows / MinGW-w64; see its header): `gcrun <iso> <fields>`
  headless, `GC_JIT=1`, `GC_PAD` input script, `GC_SNAP` pictures (the GPU's with `GC_GL=<scale>`),
  `GC_WAV` the sound, `GC_CARD` a card image, `GC_PROF` a sampling profiler. Wind Waker to the
  island (fields, PAL): Start at 840, stick left at 1240 (create the save), A at 1320, 2300, 2400,
  2500 (message, file 1, Start), A at 2700 / 2800 / 2900 (a name), Start at 3000 (END), A at 3100,
  then A every 120 fields (the story, the dialogs): the island at ~14000.
- `gctest fuzz` (the JIT against the interpreter) passes on x86-64.

## 7. Known limits (Windows)

- AX games (most of the library) are silent: the AX microcode is acknowledged, not rendered
  (Dolphin's `AXUCode` would be the next audio piece).
- The first time a TEV configuration appears, an Intel GPU (no parallel shader compile) pauses
  while compiling it; the driver's cache removes it the next time.
- Not done: the EFB read back by the CPU (EFB peeks, copies read by the CPU), bounding box,
  z-freeze, emboss mapping's bump (texgen type 1 uses its source's coordinates), the point / line
  texture offsets, the copy filter / gamma, PAL's XFB y-scale (the picture is the EFB's rectangle).
