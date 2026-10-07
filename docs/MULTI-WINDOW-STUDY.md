# Onyx: more windows, and several windows per program — a feasibility study

*Status (2026-10-07): **a study only, nothing built.** The user asked two questions:

1. Can Elegant hold more than 16 windows, or have no fixed limit at all?
2. Can one program own several top-level windows? The example is Telegram: one window for the contacts,
   and one window for each open conversation.

**Short answer: both are feasible.** The first is small: about one session. The second is a real but
well-bounded change: about four to six sessions across the kernel, Elegant, AppKit and UIKit, plus the
app itself. The design already allows for it: `docs/GUI-USERSPACE-STUDY.md` §2 lists "several windows
per process" as one of the things Elegant would bring, and `docs/HANDOFF.md` repeats it ("shown to the
programs by UIKit"). It was simply never built.

Answer the user in French; this page stays in English.*

## 1. Where things stand

### 1.1 The 16-window limit is Elegant's alone

The limit is defined twice, and the two definitions must stay equal:
- `WM_MAX_WINDOWS 16` in `user/Servers/elegant/wm/kern/gui/window.h:87`
- `EL_WINDOWS_MAX 16` in `user/Servers/elegant/core.h:23`

These do **not** depend on the limit:
- **The kernel.** It keeps no window list. It keeps one event queue per attached program.
- **The kapi ABI.** No structure is sized by a window count.
- **Window ids.** They are serial numbers, never reused (`window.cpp:35`). Nothing is stored as a bit mask.

What is sized by it, all inside Elegant:

| What | Where |
|---|---|
| The z-order: a packed array in three bands (backmost, normal, topmost); O(n) shifts | `wm/kern/gui/window.h:652` `m_pWindows[]`; `wm/window.cpp:647-730` |
| Copies of the z-order on the stack, 8 B per window | `window.cpp:1036` (Composite), `:1156`, `:1386`; `ops.cpp:345`, `:406`, `:433` |
| The table from Elegant's window number (0..15) to its window | `core.cpp:20` `g_pElWin[]` (linear searches: `core.cpp:221, 277`; `ops.cpp:39, 46, 113, 254`) |
| The copies "as last presented" (`SHOTS_MAX = 2 x`) | `core.cpp:32` |
| The events waiting to be forwarded | `core.cpp:134-135` |
| The saved-state keys | `ops.cpp:79` `s_Key[]` |
| The "exit asked" flags; the event-forwarding loop | `server.cpp:57`, `:88` |

**A trap.** `CWindowManager::Add` (`window.cpp:647`) returns `void` and silently drops the window when
the z-order is full. This is safe only because the two constants are equal. If they ever differ, a
window gets a number but never enters the z-order: it is invisible and it leaks.

**What happens at the limit today.** `el_core_window_add` returns -1, so `OpCreate` returns 0, so
`kapi_create_window` and `uk_window` return 0. Most apps then quit with no message
(`if (root.canvas.px == 0) return 1;`). The program also keeps its kernel client slot until it exits,
because `el_sys_attach` ran first (`ops.cpp:186`). After an Elegant restart, at most 16 windows come back.

### 1.2 The limits behind it

If the 16 goes away, these are the next ceilings, in the order they would be hit:

| Limit | Value | Where |
|---|---|---|
| Windows in one `kapi_win_list` answer | 36, plus the desktop (`KAPI_WS_DATA_MAX` 4096 / 108 B) | `ops.cpp:335`, `appkit_ws.inc:306` |
| Shared pixel buffers (kernel) | 128 in all. A framed window uses 3 (canvas, two frame copies), a borderless one 1, and a growing window briefly 6. In practice about **40 framed windows**. | `kern/layout.h:122` `USER_WS_SLOTS`, `wsrv.cpp:439` |
| Programs attached to Elegant (kernel) | 64 | `wsrv.cpp:335` `WS_CLIENTS` |
| Programs with a wallpaper or transfer buffer (Elegant) | 32 | `ops.cpp:197` `PROGS_MAX` |
| Apps' own fixed arrays for the window list | rdpd 20, dock 24, control 24, wifimenu 32, screenshot 48, jet 64 | `rdpd.c:146`, `dock/main.cpp:260-266`, `control/main.cpp:465`, … |

The list comes back from bottom to top. An app with a small array therefore loses the **topmost**
windows: the most recently raised ones. This matters most for rdpd at 20.

The kernel's server-side view of the buffers is one 64 MB slot of address space per buffer, at
`USER_WS_BASE` (52 GB .. 60 GB, 8 GB in all). That is why `USER_WS_SLOTS` cannot simply become 512: it
would need 32 GB of address space.

### 1.3 What a window costs

**Memory.** Each window's buffers come from the kernel heap (`wsrv.cpp:481`). They are physically
contiguous and 64 KB aligned, plus one 64 KB page. Each is mapped both in the program and in Elegant.
- The canvas is the client width x height x 4 bytes. It is **doubled** when it keeps its "as last
  presented" copy (`core.cpp:52`).
- The frame has two copies, each (w + 8) x (h + 32) x 4 bytes.

| Window | Memory |
|---|---|
| 800 x 600, framed | about **8 MB** |
| 1920 x 1080, maximised | about **34 MB** |

On a 4 GB Pi, 100 ordinary windows take 800 MB. **Memory, not a count, is the real limit.**

**Time.** Composition is done on the CPU, once per damage rectangle (at most 16; otherwise the whole
screen):
1. Search from the top for the highest window that covers the area and is opaque (`CoversOpaque`).
2. Draw from that window upwards.

Hit tests and focus searches are also O(n). Windows that are minimised or on another desk cost nothing
to draw. With a few dozen windows this is negligible next to the pixel copies.

**The GPU.** There are no per-window resources (V3D draws into the canvas directly).

### 1.4 One window per program is built into every layer

| Layer | The assumption | Where |
|---|---|---|
| Elegant | "the caller's window" is `WinOf(pid)`, the first window that pid owns; a second create returns 1 and changes nothing | `ops.cpp:37-42`, `:162` `(one window a program)`, `:311` |
| Elegant | a present (`KAPI_WS_IN_KICK`) carries no window: `el_core_window_of(pid)` | `server.cpp:270`, `core.h:63` |
| Elegant | closing the window ends the program: the close box calls `RequestExit`, which becomes `kws_exit(pid)` | `window.cpp:1763`, `server.cpp:103-107` |
| Elegant | the saved state is one `TSaved` per pid (2304 B); a restart rebuilds one window per program | `ops.cpp:60-75, 126-156`, `kapi_abi.h:1137` |
| Kernel | the pixels sit at **fixed addresses** in the program: canvas at 12 GB, frame copies at +512 MB and +768 MB. A new buffer in a slot replaces the old one. | `kern/layout.h:78-80`, `wsrv.cpp:441, 458-496` |
| Kernel | one event queue per address space, 32 events | `wsrv.cpp:347-362`, `WIN_EVENT_QUEUE` |
| ABI | `struct kapi_event` has no window field. Its `sender` is always 0 ("kernel widgets are gone"). Routing uses only the handler's function pointer. | `kapi_abi.h:651-659` |
| AppKit | every window call means "this program's window" (`kapi_present`, `kapi_move_window`, the handlers, the menu, the cursor, …); `kapi_create_window` returns the canvas, not a handle. AppKit's state for replaying the window after an Elegant restart is a set of statics. | `appkit.h:123-140`, `appkit_ws.inc:49-88, 123-172` |
| UIKit | `Root::active()` is "the single active window per app". The static `ptrEvent` / `keyEvent` dispatch to it. One `uk_pump`, one `uk_present`. `uk_quit()` is the program's "should exit". The skin's state is static (`s_winFlags`). | `root.h:109`, `root.cpp:172-217, 108-117`, `skin.cpp:91, 293-301` |
| UIKit | the dialogs, popup menus, dropdowns and tooltips are **widgets inside the window** (`Modal` is the Root's top child, with a nested pump) | `dialog.h`, `dialog.cpp:39-59`, `dropdown.cpp:53-62` |

**Already ready for several windows:**
- **The window list.** `EL_OP_WIN_LIST`, `WIN_RAISE`, `WIN_CLOSE` and `WIN_READ` work per window id, and
  `kapi_win_info` carries both `id` and `pid`.
- **Program exit.** When a program ends, Elegant removes all of its windows in a loop (`server.cpp:284-289`).
- **The dock.** It already groups by program (`EL_OP_APP_LIST` dedupes the pids).

**The Telegram app** (`user/Apps/telegram/`) is one 940 x 640 `TgRoot`:
- a 300 px contact list on the left;
- one conversation on the right: the header, the chat view, the input bar and the profile column.

The conversation shown is the global `g_open` (`buddylist.h:18`), and the panes are file-scope
singletons (`main.cpp:46-52`). The network client `g_c` is process-wide, which is what several windows
want. The app is single-instance: `kapi_ipc_lookup("telegram")`, then `kapi_raise_app`.

## 2. Part A — more windows, no fixed count

**Goal:** no compile-time window count in Elegant. A window is refused only when there is no memory for
it, and the refusal says so.

| Step | What | Size |
|---|---|---|
| A1 | **Elegant.** Make the z-order and the number → window table **growable arrays** (doubled when full, never shrunk). Remove `EL_WINDOWS_MAX` / `WM_MAX_WINDOWS` as limits. Have `Add` return whether it worked. Keep the snapshots in a static growable array instead of on the stack. Make the "shot" copies, `s_Peek`, `s_Key` and `s_nExitAsked` per window (members of the window's record). | ~300 lines, Elegant only |
| A2 | **Kernel.** Give the server's view of the buffers an allocator by **actual size**. The 8 GB region at `USER_WS_BASE` then holds hundreds of ordinary buffers instead of 128 slots of 64 MB. Make `s_Buf[]` growable, or set it to 1024 entries (about 40 KB). Raise `WS_CLIENTS` to 256. | `wsrv.cpp`, `layout.h`; the kernel and AppKit are rebuilt together, no program is |
| A3 | **The list.** Page `EL_OP_WIN_LIST` with a new op `EL_OP_WIN_LIST_FROM` (a[0] = the first index), and have AppKit's `ws_win_list` loop over it. Grow the apps' arrays: rdpd, dock, control, wifimenu, screenshot, and the mocks `tools/tests/rdpd/mock_rdpd.h`, `desktop_sim/fakekapi.cpp`. | AppKit + 5 apps |
| A4 | **Refusal.** When a window cannot be made, the program detaches (no kernel slot kept). UIKit shows a notification, "Not enough memory to open a window" (SystemKit, translated), instead of a silent exit. | small |
| A5 | **Restart.** `el_core_restore_all` restores every saved window (no 16 cap, `Pids[64]` growable). | small |

**Should there be no limit at all?** Yes, as a count. But keep a **ceiling per program** (for example 64,
in `SD:/etc/elegant.ini`), so that a program stuck in a loop of window creations cannot use all the
memory and harm the others.

**Estimate:** one session. Tests: `desktop_sim` with 60 windows (a scenario that opens them), then the
Pi with about 40 real apps open.

## 3. Part B — several windows per program

### 3.1 The design (proposed)

The rule: **an old program sees nothing change.** A window id of **0 means "the program's first
window"**, the one it has today. Everything new is optional.

**Window ids for programs.** A program's windows are numbered 0, 1, 2, … in that program (Elegant maps
them to its own windows). The global serial ids of `kapi_win_list` do not change.

**Pixels (kernel).**
- Window 0 keeps its fixed addresses (`KAPI_WS_VA_CANVAS`, …), so old programs are unchanged.
- Windows 1 and up get their three buffers at addresses the **kernel chooses** in the program's mmap
  arena (34 GB .. 52 GB, through `vm_map`'s placement). `struct kapi_ws_buf` gains `prog_addr` (out),
  and a slot is now (window, part). The struct is private to the kernel and Elegant, which are built
  together.
- `CreateWindow`'s answer carries the canvas address back to AppKit.
- A buffer that grows gets a new address. AppKit already re-reads the canvas after `kapi_resize_window2`.

**Events.**
- `struct kapi_event.sender` (always 0 today) carries **the program's window id** in its low bits, plus a
  flag bit so that "window 0" can be told apart from the old "0". Old programs ignore `sender`.
- The queue stays per program. Raise it from 32 to 64 events, since there are several windows to feed.
- `KAPI_WS_KICK` (present) takes the window id as an argument. It is a kernel ↔ AppKit call, so it changes
  freely.

**Closing.**
- A window made with the **new** call is not the program's life. Its close box posts
  `GUI_EVENT_CLOSE` (to that window) instead of `kws_exit`.
- The program closes it (`kapi_win_destroy(wid)`) or keeps it, for example to ask "save changes?".
- The program ends when it wants to. UIKit's default: when its last window closes.
- Window 0 of an old program still means "quit", as now.

**AppKit (new calls, appended to `appkit.abi`).**

```c
int   kapi_win_create (int x, int y, int w, int h, const char *title, unsigned flags, unsigned **canvas);  // -> wid >= 0
void  kapi_win_destroy (int wid);
void  kapi_win_present (int wid);
int   kapi_win_resize2 (int wid, int w, int h, unsigned **canvas);
void  kapi_win_move2 (int wid, int x, int y);
void  kapi_win_handler (int wid, int kind, void *fn);
int   kapi_win_chrome (int wid, struct kapi_chrome *out);
void  kapi_win_set_menu2 (int wid, const char *spec);
void  kapi_win_title (int wid, const char *title);
void  kapi_win_focus (int wid);            // raise and give it the keyboard
int   kapi_event_window (const struct kapi_event *e);   // the event's wid
```

The old calls stay, and mean wid 0. AppKit's replay statics become a small per-window table.

**Elegant.**
- Ops that take a window: a[3] (or a new op range) holds the wid, with 0 by default.
- `WinOf(pid)` becomes `WinOf(pid, wid)`.
- `forward_events` stamps `sender` with the window's wid.
- **Saved state** (`KAPI_WS_STATE`): one `TSaved` per window. Either grow `KAPI_WS_STATE_BYTES` (a kernel
  constant, rebuilt with Elegant), or key the kernel's store by (pid, wid).
- **Keyboard and menu.** The menu bar shows **the active window's menu**, as today. A program gives each
  window its menu, or gives them all the same one.
- `EL_OP_APP_RAISE` raises **the program's most recently active window**, not the first one found.

**UIKit** (the largest part; `uikit.so` is append-only, and `Root` has 8 spare virtual slots plus
`m_ext`).
- Each `Root` is a window. `Root::init` for the first one is unchanged. A **new** `Root::open(...)` makes
  another one (`kapi_win_create`) and enters it in a list of (wid, Root*).
- `ptrEvent` and `keyEvent` route by the event's wid to that Root. `active()` becomes "the Root that has
  the keyboard": the active window. That is what the dialogs, the menu and the shortcuts want.
- One loop: `uk_pump`, then draw and present (`kapi_win_present(wid)`) **every** Root that has damage.
- `Root::onClose()` (a new virtual, in a reserve slot). Its default closes the Root, and the program ends
  when the last one is gone. `uk_quit()` stays "the program must end".
- `Modal::run` attaches to the Root that asked (or to the active one), not to the first.
- The skin's statics (`s_winFlags`, the chrome) and the cursor state move into the Root's `m_ext`.
- The global `Menu::current()` becomes per Root, with a shared default.

**Out of scope here:**
- a window that belongs to another (a dialog as its own window, a palette that floats over its document);
- a group of windows that minimise together.

Both would be easy once windows have ids: a `WIN_FLAG_TRANSIENT` plus a parent wid. They come later if
wanted. **Dialogs stay widgets inside the window.**

### 3.2 The steps

| Step | What | Size | Test |
|---|---|---|---|
| B1 | Kernel: buffers per (window, part), placed in the mmap arena for wid ≥ 1; the kick with a wid; the event queue of 64 | `wsrv.cpp`, `layout.h`, `kapi_abi.h` (version raised) | `el0test`, a `/bin` test that makes 3 windows with no UIKit |
| B2 | Elegant: wids on the ops, `sender` stamped, the close event, per-window saved state, raise the latest window | `ops.cpp`, `server.cpp`, `core.cpp` | `desktop_sim` (`fakekapi.cpp` also has to learn the wids) |
| B3 | AppKit: the new calls, the per-window replay, `appkit.abi` appended; docs/02 §8 ABI table, docs/03, `python tools/docgen/kitdocs.py` | `appkit.h`, `appkit_calls.inc`, `appkit_ws.inc` | the B1 test written against AppKit; an Elegant restart with 3 windows open |
| B4 | UIKit: the Root list, routing, the loop over the Roots, `onClose`, the per-Root skin and menu | `root.*`, `skin.cpp`, `menu.*`, `dialog.cpp`; `uikit.abi` appended, the layout lock | a demo app with two windows; every existing app re-run (screenshots unchanged) |
| B5 | Telegram: one `ConvWindow : Root` (header, chat view, input bar, profile column, emoji picker) per open conversation, keyed by peer. The main window keeps the contact list. A double click (or a setting: "open conversations in their own window") opens one. Notifications skip conversations with an open window. `g_open` becomes the per-window `peer`. | `user/Apps/telegram/` + `lang/fr.txt` | `shots.sh telegram` (EN and FR), then the Pi |
| B6 | The dock and the menu bar: the dock's menu on an app lists its windows. "Window" in the menu bar lists the program's windows. | `dock`, `menubar` | |

**Estimate:**
- B1 and B2: about two sessions.
- B3: half a session.
- B4: one to two sessions; most of the risk is here, in the ABI and in apps that rely on `active()`.
- B5: one session.
- B6: half a session.

**Total: about five to six sessions,** after Part A.

### 3.3 The risks

- **UIKit's ABI.**
  - Old apps call `Root::active()` and get the Root they have today, but they must keep getting it.
  - The new virtuals go into the reserve slots, and `tools/libgen/layout.py` will say if a layout
    moves.
  - Every app is re-run in `desktop_sim` before publishing.
- **The 32-event queue.** Several busy windows (a chat view that scrolls, the pointer moving) could
  fill it. Raising it to 64 is the first answer. Merging the pointer-move events is the next one.
- **Restarting Elegant.**
  - A program with three windows must get all three back. B3's per-window replay is what does it.
  - It is tested by killing Elegant with three windows open.
- **The desktop simulator.** `tools/tests/desktop_sim/fakekapi.cpp` imitates AppKit with one window. It
  has to learn the wids (B2) before any screenshot of a multi-window app.
- **The menu bar.**
  - The application menu now follows the active **window**, not the active program.
  - A program that sets one menu on window 0 only would show an empty bar when another of its windows is
    active.
  - The fix: a window with no menu of its own shows its program's window-0 menu.

## 4. Recommendation

1. **Do Part A first.** It is small, it removes a limit the user can actually hit (16 windows include
   the dock, the menu bar and the applets), and B depends on its growable lists.
2. **Then do B1–B4 as one series**, with a two-window demo app as the test.
3. **Then do Telegram (B5)** as the first real user of several windows. Mail (one window per message
   being written) and Notes (one window per note) are natural next ones.

Nothing here touches the user's decisions so far: one window manager in Elegant, the kernel's event
queue per process, AppKit as the only path to the kernel, and the kits' append-only ABIs.
