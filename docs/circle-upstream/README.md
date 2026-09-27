# Contributions to upstream Circle

Changes of the Onyx fork of Circle (`docs/05-CIRCLE-CHANGES.md`) that are useful to every
Circle user, prepared as clean branches on top of upstream **`develop`** (Circle 51.1.1),
without any Onyx-specific code. One pull request per branch, each small and self-contained.

- Fork: `https://github.com/stephaneweg/circle` — the branches `pr/...` below.
- Upstream: `https://github.com/rsta2/circle` — open each pull request **against `develop`**
  (the maintainer merges new work there; `master` gets releases).
- The same commits as patch files, in this folder (`git am 000N-*.patch` on upstream `develop`).

Each branch builds (the changed files compiled with the upstream configuration, the optional
code with its option on). They have been running in Onyx for weeks, except as noted.

| # | Branch | Kind | Priority |
|---|---|---|---|
| 1 | `pr/scheduler-waketasks-timeout` | **bug fix** (system halt) | high |
| 2 | `pr/emmc-high-speed-fixes` | **bug fix** (SD High Speed, on by default in `develop`) | high |
| 3 | `pr/heap-large-block-reuse` | enhancement (option, off by default) | medium |
| 4 | `pr/2d-partial-update` | new API (2D graphics) | medium |
| 5 | `pr/dhcp-restart` | new API (network) | low |

To open one: on GitHub, go to the fork, choose the branch, **Contribute → Open pull request**,
set the base repository `rsta2/circle` and the base branch `develop`, and paste the text below.

---

## 1. sched/scheduler: Do not panic when waking a task, whose timeout expired

**Branch:** `pr/scheduler-waketasks-timeout` — `lib/sched/scheduler.cpp` (+8 −12)

### Pull request text

> **sched/scheduler: Do not panic when waking a task, whose timeout expired**
>
> A task waiting with `CSynchronizationEvent::WaitWithTimeout()` (or any
> `CScheduler::BlockTask()` with a timeout) is set ready by `GetNextTask()` when its timeout
> expires, but it stays on the event's wait list until it runs again and removes itself in
> `BlockTask()`.
>
> `CSynchronizationEvent::Set()` is documented to be callable from interrupt context. If the
> event is set by an interrupt handler in this window (after `GetNextTask()` has set the
> timed-out task ready, before that task has run), `WakeTasks()` finds a task which is not
> blocked any more, and
> `assert (pTask->GetState () == TaskStateBlocked || ... == TaskStateBlockedWithTimeout)`
> fails (with `NDEBUG`: the `LogPanic` "Tried to wake non-blocked task"). Either way the
> system halts.
>
> We hit it at random (after seconds to minutes) with a driver, which waits for a GPU
> interrupt in slices of 2 ms (`WaitWithTimeout (2000)` in a loop, the event set from the
> interrupt handler): the log showed
>
> ```
> sched/scheduler.cpp(742): assertion failed: pTask->GetState () == TaskStateBlocked || pTask->GetState () == TaskStateBlockedWithTimeout
> ```
>
> with `CScheduler::WakeTasks()` called from `CInterruptSystem::InterruptHandler()` while
> the interrupted task was in `CScheduler::Yield()`.
>
> Such a task has already been woken by its timeout, so `WakeTasks()` now only removes it
> from the list (its own removal loop in `BlockTask()` then finds nothing, which is fine;
> `GetWakeTicks()` is 0, so `WaitWithTimeout()` reports the timeout, and the caller checks
> the event state as documented). Blocked tasks are woken as before.

---

## 2. addon/SDCard: Set the host's High Speed Enable bit; align the CMD6 buffer

**Branch:** `pr/emmc-high-speed-fixes` — `addon/SDCard/emmc.cpp` (+6 −1)

### Pull request text

> **addon/SDCard: Set the host's High Speed Enable bit; align the CMD6 buffer**
>
> Two small fixes in the SD High Speed path (`SD_HIGH_SPEED`, now enabled by default):
>
> 1. After the card has been switched to High Speed mode with CMD6, the clock is raised to
>    50 MHz, but the **High Speed Enable** bit of the host controller (CONTROL0 bit 2,
>    `HCTL_HS_EN` in the SD Host Controller Simplified Specification) was never set, so the
>    host kept sampling with the Default Speed timing at 50 MHz. It is now set before the
>    clock is raised (EMMC host only, not with `USE_SDHOST`).
> 2. The 64-byte CMD6 status buffer is a `u8` array on the stack, but the PIO data transfer
>    reads `m_buf` word by word and asserts `((uintptr) m_buf & 3) == 0`. It is now declared
>    word aligned.
>
> Used on a Raspberry Pi 4 (SDXC card): High Speed at 50 MHz, about 16 MB/s sequential reads
> (about 10 MB/s at 25 MHz).

---

## 3. heapallocator: Optionally reuse blocks bigger than the largest bucket

**Branch:** `pr/heap-large-block-reuse` — `include/circle/sysconfig.h`,
`include/circle/heapallocator.h`, `lib/heapallocator.cpp` (+81 −1)

### Pull request text

> **heapallocator: Optionally reuse blocks bigger than the largest bucket**
>
> Blocks bigger than the largest bucket size (`HEAP_BLOCK_BUCKET_SIZES`, 512 KB by default)
> cannot be returned to a free list, and their memory is lost when they are freed (as
> documented in `sysconfig.h`). An application which allocates and frees big blocks
> repeatedly — frame buffers, image or file buffers, a window manager's canvases — runs out
> of heap after a while, although it never holds more than a few of them.
>
> This adds the system option **`HEAP_LARGE_BLOCK_REUSE`** (off by default, so nothing
> changes unless it is defined): the size of such a block is rounded up to the next power of
> two (`HEAP_BLOCK_ALIGN << n`) and, when it is freed, it goes onto a free list per power of
> two, from which the next allocation of the same class is served. This costs up to twice the
> requested size for these blocks, which is why it is an option; bucket-sized blocks are not
> affected. 32 classes cover any size.
>
> In Onyx (a multi-process OS on Circle) each application's window canvas (up to ~3 MB) was
> lost at every launch before this change; with it, the canvases are reused.

---

## 4. 2dgraphics: Partial display update, 2D DMA with a source stride

**Branch:** `pr/2d-partial-update` — `2dgraphics`, `bcmframebuffer`, `dmachannel`,
`dma4channel` (.h/.cpp, +239)

### Pull request text

> **2dgraphics: Partial display update, 2D DMA with a source stride**
>
> `C2DGraphics::UpdateDisplay ()` always copies the whole offscreen buffer (8 MB at 1920 x 1080
> x 32 bits). An application which knows what changed — a window manager, a game with a
> status line, a text console — can now copy only that rectangle:
>
> ```cpp
> void C2DGraphics::UpdateDisplay (unsigned nPosX, unsigned nPosY,
>                                  unsigned nWidth, unsigned nHeight);
> void C2DGraphics::UpdateDisplayAsync (unsigned nPosX, unsigned nPosY,
>                                       unsigned nWidth, unsigned nHeight,   // 0: whole screen
>                                       CDisplay::TAreaCompletionRoutine *pRoutine,
>                                       void *pParam);
> ```
>
> The rectangle is read **in place** from the offscreen buffer, with no copy by the CPU: both
> DMA engines get a source stride (legacy DMA: `STRIDE` bits 0–15; DMA4: `SRCI` bits 16–31):
>
> ```cpp
> CDMAChannel::SetupMemCopy2D (..., size_t nSourceStride);    // new overload
> CDMA4Channel::SetupMemCopy2D (..., size_t nSourceStride);   // new overload
> CBcmFrameBuffer::SetAreaPitch (const TArea &Area, const void *pPixels, unsigned nSourcePitch,
>                                TAreaCompletionRoutine *pRoutine = nullptr, void *pParam = nullptr);
> ```
>
> Only the rectangle's rows are cleaned from the data cache. The asynchronous variant starts
> the DMA and returns; the routine is called from the DMA interrupt, so other tasks run
> meanwhile (the offscreen buffer must not change until then). With VSync (page flipping) or a
> display which is not the frame buffer, the whole screen is updated, as before. Existing
> calls are unchanged.
>
> In Onyx's compositor (dirty rectangles) this cut the display cost from 2–4 ms of busy
> waiting per frame (1080p) to the rectangles actually changed, with the CPU free meanwhile.

---

## 5. net/dhcpclient: Add CDHCPClient::Restart ()

**Branch:** `pr/dhcp-restart` — `include/circle/net/dhcpclient.h`, `lib/net/dhcpclient.cpp`
(+22 −1)

### Pull request text

> **net/dhcpclient: Add CDHCPClient::Restart ()**
>
> When the WLAN joins another network while the system is running (e.g. after
> `wpa_supplicant` has reloaded its configuration to switch networks), the DHCP client keeps
> its lease until the renewal time, so the system keeps an IP address of the old network.
>
> `static void CDHCPClient::Restart ()` drops the lease and starts over (discover / request).
> The bound state now sleeps in steps of 0.5 s (instead of 10 s at a time), so that a restart
> is seen at once; the renewal timing is unchanged.
>
> Used in Onyx for a "join another Wi-Fi network" menu, together with a SIGHUP-style
> reconfiguration of `wpa_supplicant`.

---

## Not proposed (Onyx-specific, or would need rework for upstream)

- **Keyboard maps decoupled from the kernel** (layouts loaded from files, no compiled-in
  tables) — an Onyx design choice.
- **Shift + navigation keys** (xterm `ESC[1;2A`… sequences, appended to `TSpecialKey`) — useful
  upstream, but it needs the Shift column filled in every compiled-in `keymap_*.h` table first;
  a possible later pull request.
- **Yielding SD waits** (weak hooks in `ffsystem.cpp` / `emmc.cpp`) — upstream has
  `NO_BUSY_WAIT` for its own model; the hooks exist for Onyx's preemptive scheduler.
- **Sector cache, multi-cluster transfers, exFAT / partitions as volumes, fast seek (FatFs)** —
  changes to the vendored FatFs or its configuration; the multi-cluster transfer would rather
  go to FatFs itself (ChaN).
- **High memory above 3 GB for applications, the crash area, `ARM_ALLOW_MULTI_CORE`,
  `MAX_TASKS`, `PAGE_RESERVE`** — configuration or Onyx's memory model.
- **Free-space accounting, `CSpinLock::TryAcquire`, the polled display DMA** — small helpers
  for Onyx; `TryAcquire` could be offered on its own if someone needs it.
