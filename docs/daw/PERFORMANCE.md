# Koton — why it is fast (the sound, the UI) and what is left to gain

Written 2026-09-30 from the code, after the user found "no audio latency at all" on a plain Pi 4.
The design is in `README.md` (§3); this note is what the code actually does, with the numbers,
so that the next change does not undo it. **The rules at the end are the ones to keep.**

## 1. The sound

### 1.1 A core of its own

The Pi 4's four cores: 0 = the scheduled system (the kernel's tasks, every app's UI), 1 = the
kernel's sound producer (the PWM jack), 3 = the network (`netcore=1`). **Core 2 is taken whole by
Koton's engine** (`kapi_core_acquire` / `kapi_core_run`, `user/Apps/koton/ui/audio.h`, `coreLoop`):

- the loop makes **no kapi call** (so it never waits on the kernel) and **allocates nothing**
  (the voice pool, the block buffers, the compiled song: all made before Play);
- it renders 256-frame blocks while the ring holds less than `AUDIO_TARGET` (1024 frames), else
  spins on `yield` — the core is ours, nothing else runs there;
- it measures its own load with the generic timer (`cntpct_el0`), no kernel call.

Nothing on core 0 — a UI redraw, the SD card, a plugin, the network — can delay a block.

Fallbacks, same engine: a **real-time thread** (`kapi_thread_priority`) that sleeps on the ring's
read index (`kapi_wait_word`) when no app core is free; else (the PC simulator, an old kernel)
the UI tick pushing through `kapi_sound_write`.

### 1.2 The kernel's low-latency path (kapi v68)

`kernel/sys/sound.cpp`:

- **`kapi_sound_config (frames, ahead)`**: the DMA chunk and how many chunks core 1 keeps rendered.
  Koton asks `256 × 2` (the default is `1024 × 4` ≈ 116 ms). The device is made once with the
  biggest chunk; a shorter chunk is just a shorter `GetChunk` return (nothing re-created, the PWM
  clock untouched). What is heard lags the rendering by ≈ (ahead + 1) chunks: 256 × 2 ≈ 17 ms.
  The defaults come back when the owner releases the output or dies.
- **`kapi_sound_map`**: a 64 KB page (`struct kapi_sound_ring`) mapped into the app. The engine
  writes the frames then moves `wr`; core 1 mixes them and moves `rd`; `dry` counts the chunks the
  app did not fill in time (Koton's status bar shows them as *underruns*). No copy through a
  syscall, no lock, no side waits on the other. The kernel indexes the ring masked by its own
  capacity whatever the app writes in the header.

Total output latency: ≈ 17 ms (kernel) + ≤ 23 ms (Koton's ring, 1024 frames) ≈ **40 ms**, shown in
the status bar. For playback this is invisible (Play starts ~40 ms later, then everything is in
time); for live MIDI playing it is acceptable, and could go to ~10 ms (see §3).

### 1.3 The music is resolved before it plays

Koton thinks in harmony (degree chords, articulation grids, melodic-line engine, euclidean
rings…). **None of that runs in the audio path.** On each edit the project is *compiled* into an
immutable list of timed note events per track (`engine/`, a few ms for a song of minutes), and
handed to the engine, which swaps it **at a block boundary** and hands the old one back
(`engine.retired ()`) to be freed on the UI side. The engine only reads events, plays SoundFont
voices (MeltySynth in C++, `synth/`), applies the built-in effects and mixes.

### 1.4 Plugins cannot stall the sound

Each plugin is a process that renders **ahead** into shared memory (`plug/plugshm.h`). A block
not ready in time is silence for that plugin and a counter (`pluginUnderruns`) — the engine
**never waits**. A crashed plugin leaves its track dry, the rest plays on. Cost: an instrument
plugin adds ~0.1 s at Play (its render-ahead window); effect latencies are compensated (PDC).

### 1.5 Floating point

Apps build with `-mgeneral-regs-only`; Koton opts out (as n64 / gc do) and mixes in `float`.
64 SF2 voices at 44.1 kHz ≈ 5–10 % of a Cortex-A72 core.

## 2. The UI

The UI runs on core 0 and is **decoupled from the sound**: a slow frame can drop an image, never
a sample.

1. **Two-level damage** (`user/uikit/widget.cpp`, `invalidate`): each widget owns its canvas;
   `invalidate (true)` redraws that widget, its ancestors only **re-blit** children's canvases
   already drawn. Hovering a toolbar button redraws that button, not the lanes.
2. **The big areas are hand-drawn**: the lanes, the chord grid, the piano roll, the rings are each
   **one canvas the size of the view**, drawn by a plain loop over what is visible
   (`ui/arrange.h`, `onDraw`) — no widget per note, per block, per cell.
3. **Cached work**:
   - the blocks' thumbnails (the notes drawn inside each module) are rebuilt only when the
     document's `revision` changed (`rebuildThumbs`), not at each redraw, scroll or playhead move;
   - the anti-aliased text: FreeType glyphs cached per quarter-pixel position (`user/ft/fonts.h`),
     and the labels' widths remembered (`user/ft/uikitface.h`).
4. **Recompiling the song is debounced** (`main.cpp`, `KotonRoot::onTick`): after an edit, once the
   edits pause for ~60 ms (6 ticks), or at most every 250 ms during a long drag — not on every
   mouse move. The mixer (volume, pan, mute, solo) is synced without any compile.
5. **What changes often is throttled**: the meters repaint only when the level moved
   (`ui/chrome.h`, `tick`); the status bar twice a second; plugin editors draw in their own
   process, into a shared surface (the applet protocol).

## 3. TODO — what is left to gain (none started)

UI:
- [ ] **The playhead without redrawing the lanes.** While playing, `KotonRoot::onTick` calls
  `g_arrange->invalidate (true)` every tick, so the whole arrangement is redrawn to move a line.
  Keep the lanes in their own cached canvas (redrawn only on edit / scroll / zoom / resize) and
  compose the playhead over it — only the playhead's old and new strips change.
- [ ] **Only when it moved**: skip the redraw when the playhead's x (`m_lastPlayX`) is the same
  pixel as last time (at low zoom it moves one pixel every several ticks).
- [ ] Same idea for the piano roll / rhythm editors' playhead when *Listen* plays.
- [ ] Measure first: a frame-time counter in the status bar (debug), on a big song on the Pi.

Sound:
- [ ] **Lower live latency** for MIDI playing: `kapi_sound_config (128, 2)` (≈ 9 ms) and
  `AUDIO_TARGET` 512 (≈ 12 ms) → ≈ 20 ms total; a setting (*Low latency*) rather than the default,
  and check the `dry` counter stays at 0 on the Pi with a heavy song.
- [ ] HDMI audio / an I2S DAC (README §4, A2): the PWM jack is ≈ 11-bit with hiss.

## 4. Rules to keep

- Nothing on core 2 calls kapi or allocates. A new effect / voice type: its buffers are made
  before Play.
- Anything musical (harmony, generators, AI) happens in the compile step, never in `render`.
- The engine never waits on a plugin, a lock or the UI; late = silence + a counter.
- In the UI: redraw what changed (`invalidate (true)` on the widget, not the root); cache what
  is derived from the document and key it on `g_doc.revision`.
