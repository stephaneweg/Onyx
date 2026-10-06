# AutoDev round 1 — Technical Analyst: Notes + Stickies

Date: 2026-10-06. Inputs: `01-product-manager.md`, `02-product-analysis.md` (the 33 acceptance criteria, "AC n"
below). Read: CLAUDE.md, `autodev/PIPELINE.md` §3, `user/Apps/agenda/main.cpp`, `user/Apps/tinypad/main.cpp`,
`user/Apps/calendar/main.cpp` (font face, agenda link), `user/Apps/config/main.cpp` (App Settings),
`user/Apps/setup/system.h` (`#setup:` lines), the kits' headers (`appkit.h`, `systemkit/{notify,clipboard,trash}.h`,
`filekit/fsutil.h`, `uikit/{root,textarea,listbox,menu,toolbar,splitter,text,paint}.h`, `uikit.abi`),
`user/Makefile`, `tools/pkg/packages.ini`, `sdcard/etc/autostart`, `tools/tests/desktop_sim/{fakekapi.cpp,run.sh,shots.sh,sd/}`,
`tools/tests/{mock_kapi.h,run_trash_test.sh,run_ledger_test.sh}`.

**Summary.** Everything needed exists except: an owner-drawn note list (beside the app), a `notes.ini`
reader/writer (beside the app — AppKit's `.ini` reader is too small, see 1.3), a word-wrap helper (UIKit:
the only kit change, append-only), and three small additions to the PC simulator's stand-in kernel (test tools
only). **No kapi change, no kernel change.** Menus have no check marks and no sub-menus: the toggles change their
label instead (a decision for the UX Designer, §3.4).

---

## 0. Toolchain in this container (checked)

| Tool | Status |
|---|---|
| `aarch64-none-elf-` cross toolchain (`user/Makefile`'s `PREFIX`) | **absent** (no `aarch64-*` compiler; `apt` has none listed; `tools/toolchain/build-onyx-toolchain.sh` builds one, long). → `make` / `make stage` / libgen's `.abi` update **cannot run here**: AC 29 and AC 33 are for the user's PC / the Pi. |
| Host `g++` 13.3 / `gcc`, `python3` + Pillow + numpy | **present** |
| Desktop simulator | **works here**: UIKit (all `user/Kits/uikit/*.cpp`) + `fakekapi.cpp` + `user/Apps/agenda/main.cpp` built with the host `g++` in ~5 s, run through a script, dumped, made a PNG by `shot.py` (verified). |
| `fakekapi.cpp` as a unit-test host | **works**: a test program linked with `fakekapi.o` (no window) gets `kapi_open/save_file/opendir/readdir/mkdir/rename`, `trash_move` (SystemKit inline on the PC) into `SIM_WRITES`, the fixed clock 2026-09-28 12:34:00 (verified). `kapi_path_stat` / `kapi_dir_read` / `kapi_clock_info` return `-KAPI_ENOSYS` there (not in the stand-in table). |
| `python tools/docgen/kitdocs.py` | **works** (regenerates `docs/10..19`; no diff today) |
| `pandoc`, `libreoffice` (`docs/build_docs.py`) | **present** |
| `sh tools/tests/desktop_sim/shots.sh <name>` | builds **every** app of its `APPS` list whatever the names given (incl. the Media Player, which builds FFmpeg for the host the first time) — see §4 R7 for the measured time. Note: it writes `screenshots/<name>.png` into the repo. |

---

## 1. What exists and is reused

### 1.1 AppKit (`user/Kits/appkit/appkit.h`)

| Need | Call |
|---|---|
| Read a note | `kapi_open` / `kapi_fsize` / `kapi_read` / `kapi_close` |
| Write a note, `notes.ini`, the `config.ini`s | `kapi_save_file` (whole file; returns bytes written, `-1` failure) |
| List `SD:/Notes/` | `kapi_opendir` / `kapi_readdir` (`struct kapi_dirent`: name ≤127, size, is_dir) / `kapi_closedir` |
| Create `SD:/Notes/` | `kapi_mkdir` (fails harmlessly when it exists) |
| Now (names, `modified`) | `kapi_get_datetime (&y,&mo,&d,&h,&mi,&s)` → 1 real date, **0 clock not set yet** (§4 R3) |
| **A file's modification time** (open point of the analyst) | **`kapi_path_stat (path, &st)` → `st.mtime`, UTC seconds since 1970** (v75, `struct kapi_stat`); also `kapi_dir_read` (`kapi_dirent2.mtime`, 255-char names). The local offset: **`kapi_clock_info (&ci)` → `ci.tz_minutes`**. So FAT's mtime *is* reachable through AppKit — no kapi change. Used only as the fallback for a `.txt` without a `notes.ini` section (the analyst's rule: `notes.ini`'s `modified` is the reference). Unavailable (`-KAPI_ENOSYS`) in the simulator unless step 0 adds it. |
| Arguments (`notes <path>`) | `kapi_get_args` (tinypad's pattern; `SIM_ARGS` in the simulator) |
| Start Stickies / Notes | `lx_launch ("stickies", 0)` / `lx_launch ("notes", path)` (or `kapi_exec ("SD:apps/notes.app/main", path)`, agenda's way); `kapi_raise_app ("notes")` to bring Notes forward |
| IPC | `kapi_ipc_register` / `kapi_ipc_lookup` / `kapi_mailbox_send` / `kapi_mailbox_recv (…, 0)` (non-blocking, drained in `onTick`, ≤ 512 B) |
| Widget moves itself | `kapi_cursor_pos`, `kapi_move_window` (agenda's drag code, copied) |
| Exit at once | `kapi_exit (0)` / returning from `main` before the `Root` |
| Small `.ini` (the `config.ini`s) | `app_ini_load_path` / `app_ini_get` / `app_ini_get_int` (read only; written back whole with `kapi_save_file`, as agenda does) |

### 1.2 SystemKit, FileKit, FontKit, UIKit

- **SystemKit** (`systemkit/systemkit.h`): `trash_move (path)` (SD:/.Trash, freedesktop layout — AC 8);
  `notify ("Notes", "Note moved to the Trash")`; `clip_set_text_n` / `clip_get_text` (AC 12; the Textarea's own
  Ctrl+C/X/V go through them already).
- **FileKit** (`filekit/fsutil.h`): `fs_join`, `fs_basename`, `fs_exists`, `fs_is_dir`, `fs_ci_cmp`,
  **`fs_text_fix`** (drops a UTF-8 BOM, turns UTF-16 into 8-bit — for dropped / hand-put files).
- **FontKit**: `ft_uikit_install ("DejaVu Sans", 13)` (`fontkit/uikitface.h`, the Calendar's line 465) — both new
  programs are **FT apps** ("every new app joins them", `user/Makefile`): proportional text, bold titles.
- **UIKit**: `Root` (window; `onTick` ~60 Hz, `onDrop`, `setResizable`, `onResized`); the borderless back-most
  see-through window of agenda (`Root (x, y, w, h, "stickies", WIN_FLAG_BORDERLESS | WIN_FLAG_BACKMOST |
  WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA)`); `Textarea` (capacity fixed at construction → the 64 KB cap is natural;
  `setContent`, `insertText`, `copy/cut/paste`, `selectAll`, `len`, `content ()`); `Splitter` (list | editor,
  `split`); `ToolBar` / `ToolButton` (`uikit/toolbar.h`: `WKT_NEW`, `WKT_COPY`… and an app icon drawer for
  pin / delete / colour; `setToggle` for the pin); `Menu` (`menu`, `item (label, "^N", UK_CTRL('N'), cb)`,
  `separator`, `publish`; rebuilt and re-published to change labels — the Game Library's pattern,
  `user/Apps/gamelib/main.cpp:1012-1034`); `Label`/status line; paint helpers `uk_rbox`, `uk_rline`, `uk_bead`
  (the colour dot), `uk_blend_px`, `uk_paint_alpha`, `uk_text`, `uk_tw`, `uk_tw_n`, **`uk_text_fit`** (cut + "...");
  theme colours `C_*`.
- **`user/Include/docguard.h`**: `doc_hash` (FNV-1a: "has the text changed since saved"), `doc_first_path` (the
  first path of a `DND_FILES` drop — tinypad's).
- **Agenda** (`user/Apps/agenda/main.cpp`): copied as Stickies' skeleton — `config.ini` place (`x`, `y`), the
  ~3 s poll in `onTick` with a content signature, the header drag, the click → `kapi_exec`, `read_back` (the
  wallpaper's brightness under the widget, if the cards' shadow wants it).

### 1.3 What does **not** fit as it is

| Gap | Why | Where it goes |
|---|---|---|
| `notes.ini` reading / writing | AppKit's `.ini` reader holds **`INI_MAX` 64 entries, `INI_BUFSZ` 2048 bytes** and only reads: 3 keys a note → 21 notes / ~15 with comments. Changing those limits changes `appkit.so`'s static store (risky for every program). | A small parser/writer in the **notes model** beside the app (`user/Apps/notes/notesmodel.cpp`), unit-tested. |
| A list whose rows show a colour dot, a title, a date and a pin | `uikit::ListBox` is text only (64-char items, no owner draw). | `NoteList` widget beside the app (`user/Apps/notes/notelist.h`, a `Widget` subclass: `onDraw`, `onMouse`, `onKey` Up/Down/Delete/Enter). |
| Know when the editor's text changed | `Textarea` has no change callback. | `NoteEdit : Textarea` beside the app, `onKey` → base, then compare `len` / `doc_hash` → dirty + time stamp. |
| Word-wrap a text into N lines (Stickies' cards) | Not in UIKit (RichTextBox has a private one). Reusable (any widget/card showing a paragraph). | **UIKit**: `uk_text_wrap` in `text.h` / `text.cpp` (kits-first rule; the analyst's §7 asked for it). The only kit change: one free function, `uikit.abi` appended. |
| Check marks in menus, sub-menus | `uikit::Menu` has neither (spec lines `M`/`I`/`-`), the menu bar draws none. Adding them = UIKit + menubar + `Menu`'s layout (locked by `abi.h`): out of scope. | Label toggles (§3.4). |
| `config.ini` with a `[notes]` section (analyst 5.3) | The Control Panel's *App Settings* (`user/Apps/config`) edits plain `key = value` lines; agenda's has no section. | **No section**: `stickies = 1`, `last = …`, `width`, `height`, `split` as plain lines (deviation from 02 §5.3, harmless). |

---

## 2. Simulator: the analyst's open point — folder listings

**Does `SIM_OVERLAY` support listing a folder? No.** In `fakekapi.cpp`, `sdpath ()` looks in the overlay
directories only for **regular files** (`S_ISREG`); `f_opendir` resolves the folder through `sdpath`, so
`kapi_opendir ("SD:/Notes")` with `tools/tests/desktop_sim/sd/Notes/` gives **0** (it falls back to
`sdcard/Notes`, which does not exist). Also, an overlay file can be **neither removed nor renamed**
(`f_remove` / `f_rename` act only on `SIM_WRITES` and `RAM:`), so `trash_move` of a sample note fails.

**What works today, unchanged:** a folder present in **`SIM_WRITES`** and absent from the card *is* listed
(`sdpath` returns the writes' path when the card has no such folder), files there are read first, and can be
renamed / trashed. So each Notes / Stickies scenario **seeds a fresh writes folder** with the sample notes:

```sh
seed () { rm -rf "$OUT/$1"; mkdir -p "$OUT/$1"; cp -r $D/sd/Notes "$OUT/$1/Notes"; }   # in shots.sh
seed nw; sim notes notes "$W" SIM_WRITES="$OUT/nw"
```

This is the recommended answer (no change to the overlay's semantics, on which every other scenario relies).
A merged listing (writes ∪ overlays ∪ card) in `f_opendir` would be possible later but is not needed.

---

## 3. Design

### 3.1 Files

```
user/Apps/notes/main.cpp          Notes: window, menus, toolbar, autosave, IPC "notes"        (new, MIT)
user/Apps/notes/notesmodel.h      the model's declarations (+ NOTES_DIR, colours, limits)       (new, MIT)
user/Apps/notes/notesmodel.cpp    the model: scan, read, write, notes.ini, names, config        (new, MIT)
user/Apps/notes/notelist.h        NoteList widget (owner-drawn rows)                            (new, MIT)
user/Apps/notes/stickies_proto.h  the two services' names and message types                     (new, MIT)
user/Apps/stickies/main.cpp       Stickies widget (includes "Apps/notes/notesmodel.h")          (new, MIT)
user/Kits/uikit/text.h/.cpp       + uk_text_wrap                                                (change)
user/Kits/uikit/uikit.abi         + one line (append)                                           (change)
sdcard/apps/notes.app/{app.txt,icon.bmp}, sdcard/apps/stickies.app/{app.txt,icon.bmp}           (new)
tools/icons/notes_icon.py         both icons (a note pad; a pinned sticky note), slides_icon.py's way (new)
user/Makefile                     FT_APPS += notes stickies; FT_EXTRA_notes/stickies = Apps/notes/notesmodel.cpp; deps
tools/tests/desktop_sim/fakekapi.cpp   SIM_SERVICES, sends logged, SIM_CURSOR=follow, path_stat (§3.6)
tools/tests/desktop_sim/shots.sh  build + scenarios notes / stickies (+ desktop scene)
tools/tests/desktop_sim/sd/Notes/ sample notes + notes.ini
tools/tests/run_notes_test.sh, tools/tests/notes/model_test.cpp     PC unit tests (model)
tools/tests/run_notes_sim_test.sh  scripted simulator checks (asserts on SIM_WRITES and the log)
sdcard/etc/autostart, tools/pkg/packages.ini, docs/04, docs/06 (§UIKit, wrap), docs/11 (generated), docs/HANDOFF.md
```

Stickies gets **its own folder** (`user/Apps/stickies/`) rather than a second ELF in `notes/`: the generic
rules (`$(FT_APP_ELFS): %.elf: Apps/%/main.cpp`, `shots.sh`'s `build ()`) then work unchanged; the shared code
is `Apps/notes/notesmodel.*`, linked into both through `FT_EXTRA_<app>` (the gpiolab pattern). `user/` is on
every include path, so `#include "Apps/notes/notesmodel.h"` works from both.

### 3.2 The model (`notesmodel.h`) — no UI, unit-tested

```cpp
#define NOTES_DIR      "SD:/Notes"
#define NOTES_INI      "SD:/Notes/notes.ini"
#define NOTES_CFG      "SD:/apps/notes.app/config.ini"
#define STICKIES_CFG   "SD:/apps/stickies.app/config.ini"
#define NOTE_MAX_BYTES 65536            // a note's text, at most
#define NOTES_MAX      512              // notes listed
enum { NC_YELLOW, NC_GREEN, NC_BLUE, NC_PINK, NC_PURPLE, NC_GREY, NC_COUNT };
struct NoteInfo { char file[72]; char title[96]; int colour; bool pinned; long long modified; /* YYYYMMDDHHMMSS */ };
struct Notes    { NoteInfo n[NOTES_MAX]; int count; };

int  notes_scan (Notes &s);                     // *.txt of NOTES_DIR (dirs, notes.ini, non-.txt skipped) + notes.ini;
                                                // titles read (first 2 KB); sorted newest first -> count
int  notes_read (const char *file, char *buf, int cap);   // -> length; \r\n -> \n, BOM/UTF-16 fixed (fs_text_fix);
                                                // -1 missing, -2 larger than NOTE_MAX_BYTES (never truncated)
int  notes_write (Notes &s, const char *file, const char *text, int len, long long now);  // the .txt + its
                                                // modified; NOTES_DIR made; -2 too large; 0 ok
bool notes_save_ini (const Notes &s);           // every listed note's section; files gone -> dropped
bool notes_new_name (char *out, int cap, long long now);  // note-YYYYMMDD-HHMMSS[-k].txt, free in NOTES_DIR
                                                // AND not already in s (an unsaved new note holds its name)
void notes_title (const char *text, int len, char *out, int cap);   // first non-blank line, trimmed, UTF-8 safe
long long notes_now (void);                     // kapi_get_datetime -> YYYYMMDDHHMMSS
long long notes_file_time (const char *path);  // kapi_path_stat mtime + kapi_clock_info tz -> local; 0 unknown
int  notes_find (const Notes &s, const char *file);       // index or -1 (a path or a bare name)
int  notes_pinned (const Notes &s, int *idx, int max);    // the max most recently changed pinned -> count
bool notes_trash (Notes &s, const char *file);  // trash_move + removed from s + notes_save_ini
const char *notes_colour_name (int c); int notes_colour_parse (const char *v);   // unknown -> NC_YELLOW
unsigned notes_colour_paper (int c); unsigned notes_colour_dot (int c);          // 0x00RRGGBB
struct NotesCfg { int stickies; char last[72]; int width, height, split; };
void notes_cfg_load (NotesCfg &c); bool notes_cfg_save (const NotesCfg &c);   // no [section]
```

`notes.ini` is written whole by Notes after every change of a note (save, colour, pin, delete), sections in
the list's order, the analyst's comment header kept; unknown keys of a section are dropped (acceptable: Notes
alone writes it). A `.txt` with no section: yellow, not pinned, `modified = notes_file_time ()` (0 when
unknown → sorted last, the date column empty). An unsaved new note is an in-memory entry with a reserved name
(not on the card until it has text — AC 3, AC 6).

### 3.3 Notes (`main.cpp`)

- **Window**: `Root (760, 480, "Notes")`, resizable; toolbar (New, Delete, Pin toggle, Colour palette as a split
  button or 6 dots, Copy Note), a `Splitter` (`NoteList` | `NoteEdit`), a one-line status label (the 64 KB
  refusal, "Saved"). Sizes / split from `NotesCfg`.
- **Start**: `ft_uikit_install`; if the service `notes` already exists, forward the argument
  (`NOTES_MSG_OPEN` + path), `kapi_raise_app ("notes")`, exit (one Notes at a time); else
  `kapi_ipc_register ("notes")`; `notes_scan`; select: the argument's note (AC 14), else `cfg.last`, else the
  newest (AC 2); none → a new empty note, caret in the editor, nothing written (AC 3).
- **Editing / autosave** (AC 4, 5, 7, 15): `NoteEdit::onKey` marks `dirty` + `lastEdit = kapi_get_ticks ()`;
  the list row's title follows (`notes_title` each key, cheap); `onTick`: dirty and `now - lastEdit ≥ 100`
  ticks (1 s) → `notes_write` + `notes_save_ini`, then `STK_MSG_RELOAD` to Stickies if it runs. Also saved on a
  selection change and after `run ()` returns (the window closed / Ctrl+Q — the simulator's `quit` step runs
  that code). The file name never changes. Empty text → the note is not written; leaving it (another
  selection, quit) removes it; if it had a file (a note emptied by the user), that file is removed with
  `kapi_remove` (the analyst's "not kept"; the UX Designer may prefer the Trash).
  Full: `len ≥ NOTE_MAX_BYTES` after a key/paste → status "This note is full (64 KB)".
- **Menus** (rebuilt by one `build_menu ()` whenever a toggle changes; §3.4): *File*: New Note ^N, Delete Note ^D,
  (should: Open in Text Editor, Export…); *Edit*: Cut ^X, Copy ^C, Paste ^V, Select All ^A, Copy Note; *Note*:
  Pin to Desktop / Unpin from Desktop ^P, Yellow … Grey (six items); *View*: Show Stickies on the Desktop /
  Hide Stickies from the Desktop, (should: Find ^F, Sort by Title / Date). Ctrl+Q is the bar's.
- **Delete** (AC 8): `notes_trash`, select the next row, `notify ("Notes", "Note moved to the Trash")`.
- **Colour / pin** (AC 9, 10): set, `notes_save_ini`, list redraw, `STK_MSG_RELOAD`.
- **Show/Hide Stickies** (AC 11): `cfg.stickies` saved; off → `kapi_mailbox_send (pid, STK_MSG_QUIT)` if
  `kapi_ipc_lookup ("stickies")`; on → `lx_launch ("stickies", 0)` if not running.
- **Drop** (AC 13): `Root::onDrop`: `DND_FILES` over the list (x < split) → each `.txt`/`.md` read
  (`notes_read`, max 64 KB) into a **new** note (new name, saved at once, selected); `DND_TEXT` (or a drop over
  the editor) → `insertText` at the caret.
- **IPC drained in `onTick`**: `NOTES_MSG_OPEN` (path) → save the current one, rescan, select it.
- **Rescan** when the window becomes active again or every ~5 s when idle (hand-put files, Stickies' should
  checklist tick) — never while the current note is dirty.

### 3.4 Menus without check marks (decision for the UX Designer)

`uikit::Menu` and the menu bar have no check mark and no sub-menu. The plan therefore uses **label toggles**
("Pin to Desktop" ↔ "Unpin from Desktop"; "Show Stickies on the Desktop" ↔ "Hide Stickies from the Desktop") and
six flat colour items in a *Note* menu; the **toolbar's pin toggle** shows the state (AC 10's "check mark" is
met by the label + the lit toggle). Adding real check marks is a UIKit + menubar feature for a later round.

### 3.5 Stickies (`user/Apps/stickies/main.cpp`)

- `main`: `notes_cfg_load`; `stickies == 0` → return at once, no window (AC 22). `kapi_ipc_register ("stickies")`
  (AC 25). Place: `STICKIES_CFG` `x`/`y`, default top right: `x = screen_w - W - 12`, `y = 40`.
- Window: agenda's kind (borderless, back-most, system, alpha), width ~240 px, height = header + up to 6 cards
  (fixed card height, ~6 text lines) — or one window sized to the cards shown (`kapi_resize_window` on change).
- **Poll every ~3 s** (`onTick`, agenda's way) a signature = the `SD:/Notes` listing (names + sizes) + `notes.ini`'s
  bytes + the pinned notes' first 2 KB → on change `notes_scan` + `notes_pinned (…, 6)` + redraw (AC 14, 20, 26).
  Also on `STK_MSG_RELOAD` at once; `STK_MSG_QUIT` → `kapi_exit (0)` (AC 25).
- **Draw**: header "Notes" (drag handle, AC 24: agenda's code, `config.ini` written on release); each card a
  `uk_rbox` in `notes_colour_paper`, a soft shadow, the title bold (`uk_text_fit`), then the body
  **`uk_text_wrap`** to the card's width, N lines, the last one `uk_text_fit`+"…" when cut (AC 19). Nothing
  pinned: the hint *"No notes pinned — open Notes"* (AC 21).
- **Click** a card → `lx_launch ("notes", "SD:/Notes/<file>")`, or, when the `notes` service exists, send it
  `NOTES_MSG_OPEN` + `kapi_raise_app ("notes")` (AC 15, 23). The hint → `lx_launch ("notes", 0)`.

### 3.6 Simulator additions (test tools only, `fakekapi.cpp`)

1. **`SIM_SERVICES="notify,stickies"`**: those names exist for `kapi_ipc_lookup` (pid 700+k) and
   `kapi_mailbox_send` to them is **logged** (`sim: send <name> type <t> "<payload>"`). Needed because
   `notify ()` without a `notify` service calls `kapi_launch ("notifyd")` then **waits 40 × `kapi_msleep (50)`**,
   and in the simulator every `msleep` consumes one script step — the scenario would lose 40 steps. Without
   `SIM_SERVICES` nothing changes for the existing scenarios.
2. **`SIM_CURSOR=follow`**: `kapi_cursor_pos` returns the last scripted pointer position in screen coordinates
   (window place + client point), so a header drag really moves the widget (AC 24). Default unchanged.
3. **`path_stat`** (and `clock_info` with `tz_minutes = 0`) from the host's `stat ()` of `sdpath ()`: the
   mtime fallback testable (AC 17 for hand-put files). Optional.

Waiting ~1 s in a script: a turn of `Root::run` is `msleep (16)` → +2 ticks; 1 s = 100 ticks ≈ 50 `wait`s. A
helper in `shots.sh` / the sim test: `waits () { i=0; o=""; while [ $i -lt $1 ]; do o="$o;wait"; i=$((i+1)); done; printf '%s' "${o#;}"; }`.
The simulator's clock is fixed (2026-09-28 12:34:00): every note it creates is `note-20260928-123400.txt`, the
second `…-123400-2.txt` (AC 18 shows itself). **The sample notes must be dated ≤ 2026-09-28** (not
2026-10-05 as in AC 14's example): e.g. `note-20260928-091500.txt` (today: shows "09:15"),
`note-20260927-183012.txt`, `note-20260915-101500.txt`, plus pinned ones for Stickies.

---

## 4. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | **No cross toolchain here**: the Pi build, `make stage`, the real `uikit.abi` append by libgen, and AC 29 / 33 cannot be checked in this container. | Everything else is verified on the PC (unit tests, simulator). The `uikit.abi` line is appended by hand with the Itanium-mangled name taken from the host `nm` (`_ZN5uikit12uk_text_wrapE…` — same mangling on AArch64 LP64), to be confirmed by the user's `make` (libgen checks the table). If in doubt, `uk_text_wrap` can stay `static` in Stickies for this round and move to UIKit later — the plan keeps it in UIKit per the kits-first rule. |
| R2 | **Package grouping**: `[onyx]` has `apps = Shell Settings`, so `stickies.app` (`category = Shell`) would be swallowed by the base system package ("a file is taken by the first section that names it"). | Declare **`[notes]` before `[onyx]`** in `packages.ini` with `files = apps/notes.app/ apps/stickies.app/` (Stickies keeps `category = Shell`, hidden from the dock's drawers like Agenda). Check which package takes `apps/stickies.app/` with `tools/pkg/mkrepo.py` (without publishing — AutoDev rule). |
| R3 | **Autostart on existing cards**: `etc/*` is `config` of `[onyx]` (the user's, never overwritten), so an updated card never gets the new `run stickies` line; no package key adds autostart lines. | Ship `#setup: run stickies` in `sdcard/etc/autostart` (new cards, Setup gives it back). And **Notes' "Show Stickies" launches Stickies itself**; optionally, Notes appends `run stickies` to `SD:/etc/autostart` once when the user switches it on and the line is absent (a small, guarded write — the UX Designer / the user decide; default plan: only launch, plus a note in docs/04). |
| R4 | Clock not set (`kapi_get_datetime` → 0, before NTP): names/`modified` from the time since boot (year 1970/0). | Names stay unique (the `-k` suffix); `modified` then sorts oddly until the next edit with a real clock. Accept; documented. |
| R5 | `SD:/Notes` vs the sample `SD:/notes.txt` (tinypad's): FAT is case-insensitive but the names differ (`Notes` / `notes.txt`): no clash. | — |
| R6 | Stickies' poll cost: listing + `notes.ini` + ≤ 6 × 2 KB every 3 s. | Negligible (agenda reads 16 KB every 3 s). |
| R7 | `shots.sh` builds all its apps (FFmpeg for the host the first time) for any scenario; it overwrites `screenshots/*.png` it is asked for. | Run the targeted names only; measured here: see the end of this section. Commit only the intended PNGs. |
| R8 | Textarea at 64 KB capacity: a paste past it is cut by the widget (not refused). | Notes checks `len` after a paste and says so (status line); the model itself refuses > 64 KB (AC 16). |
| R9 | Two programs write `SD:/apps/notes.app/config.ini`? No: Notes writes it, Stickies only reads it (`stickies`); Stickies writes its own `config.ini`. | — |
| R10 | Menu check marks missing (§3.4). | Label toggles; UX Designer to confirm. |

Measured: `sh tools/tests/desktop_sim/shots.sh agenda` in this container (first run, FFmpeg built for the host) — **226 s, exit 0**, `screenshots/agenda.png` regenerated **byte-identical** (no git diff). Pre-existing, unrelated: a few apps that print (Photos, and others using `PrintWriter` / `print_*`) fail to link on the host because PrinterKit is not linked by `shots.sh`; the build runs in the background so the scenario still passes — not this round's problem, but `notes` / `stickies` must not call PrinterKit.

---

## 5. Implementation plan (each step testable on its own)

**Step 0 — test tooling.** `fakekapi.cpp`: `SIM_SERVICES` + logged sends, `SIM_CURSOR=follow`, `path_stat` /
`clock_info`. Sample data `tools/tests/desktop_sim/sd/Notes/` (5 notes: 3 pinned yellow/green/pink, 2 not;
`notes.ini`; one `.txt` with `\r\n` and no section). *Test*: `shots.sh agenda desktop` unchanged
(pictures identical); a tiny probe that `SIM_SERVICES=notify` + `notify ()` logs one send and consumes no step.

**Step 1 — UIKit `uk_text_wrap`.** `int uk_text_wrap (const char *s, int n, int w, int maxLines, int *start,
int *len, bool *more = 0, int style = 0)` in `text.h`/`text.cpp` (words broken at spaces, `\n` honoured, a word
wider than `w` cut at a character — UTF-8 through `uk_u8_next`, widths by `uk_tw_n`; `*more` = text left).
`uikit.abi` + one line; `python tools/docgen/kitdocs.py`; docs/06 UIKit paragraph one line. *Test*: a host test
(`tools/tests/notes/wrap_test.cpp` linked with the host UIKit objects + `fakekapi.o`): bitmap font widths →
exact breaks; a 300-char word; `\n\n`; `more` flag.

**Step 2 — the model.** `notesmodel.h/.cpp`, `stickies_proto.h`. *Test*: `sh tools/tests/run_notes_test.sh`
(`model_test.cpp` + `notesmodel.cpp` + `fakekapi.o`, `SIM_WRITES` = a temp dir seeded per case, ASan/UBSan as
Ledger's): AC 16, 17, 18 + titles (blank first lines, UTF-8 cut), colour names round trip, newest-first order,
`notes_pinned` top 6 of 8 (AC 20's logic), `notes.ini` round trip, 64 KB refusal, trash.

**Step 3 — Notes, read-only window.** `main.cpp` (window, toolbar, `NoteList`, `NoteEdit`, splitter, menus,
selection: argument / `last` / newest / new empty), `user/Makefile` (`FT_APPS += notes`,
`FT_EXTRA_notes = Apps/notes/notesmodel.cpp`, `notes.elf: $(wildcard Apps/notes/*.h) Apps/notes/notesmodel.cpp`),
`sdcard/apps/notes.app/app.txt` (`name = Notes`, `category = Productivity`), `tools/icons/notes_icon.py` →
`icon.bmp`; `shots.sh`: `notes` in `APPS` and the FT case list, `extra=user/Apps/notes/notesmodel.cpp` for notes
and stickies, scenario `notes` (seeded writes). *Test*: `shots.sh notes` → `screenshots/notes.png`; sim test
cases AC 1, 2, 3 (no `SD:/Notes` → nothing in writes), 5, 14.

**Step 4 — editing and autosave.** Dirty tracking, 1 s save, save on select / quit, empty-note rule, Ctrl+N, 64 KB
status. *Test* (`run_notes_sim_test.sh`, asserting on the writes): AC 4 (`typ 'Shopping'; key 10; …; waits 60`
→ exact bytes + `notes.ini` section), AC 6, 7, 15 (`quit` right after typing).

**Step 5 — delete, colour, pin, clipboard, drop.** *Test*: AC 8 (`SIM_SERVICES=notify` → file in
`.Trash/files`, log has the notification), 9, 10, 12 (Copy Note then Ctrl+V in another note), 13
(`drop X Y SD:/docs/x.txt` from the overlay; the overlay file unchanged).

**Step 6 — Stickies.** `user/Apps/stickies/main.cpp`, Makefile (`FT_APPS += stickies`,
`FT_EXTRA_stickies = Apps/notes/notesmodel.cpp`), `sdcard/apps/stickies.app/app.txt` (`name = Stickies`,
`category = Shell`) + icon. *Test*: `shots.sh stickies` (AC 19); sim cases AC 20 (8 pinned), 21 (none pinned:
hint; click → log `sim: exec …notes`), 22 (`stickies = 0` → no dump, exit 0), 23 (click → `notes` with the path
in the log), 24 (`SIM_CURSOR=follow`, drag, `config.ini` in writes; second run opens there), 25 (`SIM_MBOX`
quit / reload), 26 (a script step cannot change files mid-run: run with `SIM_SLEEP=1` and a background `cp`
between polls, or poll twice with a changed seeded `notes.ini` via `SIM_WRITES` edited by the test before a
`waits 200`), 27 (desktop scene).

**Step 7 — Notes ↔ Stickies.** Show/Hide toggle (AC 11: `SIM_SERVICES=stickies` → quit message logged; without
it → `sim: launch/exec stickies` logged; `config.ini` in writes), reload message after saves/pins, the `notes`
service (single instance, `NOTES_MSG_OPEN`). *Test*: sim cases AC 11, 25 (Notes side).

**Step 8 — integration and docs.** `sdcard/etc/autostart`: `#setup: run stickies` after `#setup: run agenda`;
`tools/pkg/packages.ini`: `[notes]` (title Notes, category Productivity, summary, `icon = apps/notes.app/icon.bmp`,
`files = apps/notes.app/ apps/stickies.app/`, `needs = uikit >= <new>, fontkit`) **placed before `[onyx]`** — not
published; docs/04 (§5 *Stickies* beside *The agenda widget*, §12 catalog rows for `notes` and `stickies` + a
*Notes* section: controls, files `SD:/Notes/*.txt`, `notes.ini`, the two `config.ini`s); docs/HANDOFF.md roadmap
(Priority 2 item done); `shots.sh`: a `desktop`-style scene with Stickies top right (AC 27); `python
docs/build_docs.py`. *Test*: `shots.sh notes stickies agenda desktop`; AC 28, 30, 31, 32 by review. AC 29 and
AC 33 on the user's PC / Pi.

**Step 9 (should, each independent).** Search field + Ctrl+F; Open in Text Editor (`fa_open` / `lx_launch
("tinypad", path)`); Export… (`ft_file_save`); checklist boxes + click-to-tick in Stickies (writes the `.txt`
and `notes.ini` through the model — then Stickies writes too: keep it last); sort menu; drag out.

---

## 6. Tests

- **PC unit tests** — `sh tools/tests/run_notes_test.sh`: `tools/tests/notes/model_test.cpp` (+ `wrap_test.cpp`),
  host `g++ -fsanitize=address,undefined`, linked with `fakekapi.o` (its file table into a temp `SIM_WRITES`, its
  fixed clock), each case seeding `SIM_WRITES/Notes`. Prints "notes: all checks passed".
- **Simulator checks** — `sh tools/tests/run_notes_sim_test.sh`: builds `notes` and `stickies` for the host as
  `shots.sh` does (UIKit objects, `fakekapi.o`, FreeType), runs each AC's script with its own seeded
  `SIM_WRITES`, then asserts with `cmp` / `grep` on the written files and the run's stderr (`sim: exec`, `sim:
  send`, `sim: dumped`). One line per AC: `ok AC4`, `FAIL AC4 …`.
- **Screenshots** — `sh tools/tests/desktop_sim/shots.sh notes` (`screenshots/notes.png`: three notes, one
  selected, a pin, the dots) and `shots.sh stickies` (`screenshots/stickies.png`: the cards over the wallpaper,
  `--crop` top right); `shots.sh desktop` re-run to check nothing moved (or a new `desktop` scene with
  Stickies, if the UX Designer wants it in the main picture).
- **Pi** (user): AC 29 (`make`, `make stage`) and AC 33.

---

## 7. Acceptance criteria → steps

| AC | Step(s) | How checked |
|---|---|---|
| 1 list newest first, title, date, dot, pin | 2, 3 | unit (order) + sim + `notes.png` |
| 2 select `last` / newest | 3 | sim |
| 3 no folder → empty note, nothing written | 3, 4 | sim (writes empty) |
| 4 typing + 1 s → file + `notes.ini` | 4 | sim (`cmp`) |
| 5 row title follows | 3, 4 | sim (dump) |
| 6 Ctrl+N; empty note dropped | 4 | sim |
| 7 no rename on title change | 4 | sim |
| 8 delete → Trash + notification | 0, 5 | sim (`.Trash`, `sim: send notify`) |
| 9 colour green | 5 | sim (`notes.ini`) |
| 10 Ctrl+P pin toggle | 5 | sim (`notes.ini`, label/toggle) |
| 11 show/hide Stickies | 0, 7 | sim (`config.ini`, log) |
| 12 Copy Note / paste | 5 | sim |
| 13 drop `.txt` → new note | 5 | sim |
| 14 `notes <path>` | 3 | sim (`SIM_ARGS`) |
| 15 close right after typing saves | 4 | sim (`quit`) |
| 16 `\r\n`, > 64 KB refused | 2 | unit |
| 17 orphan `.txt` / orphan section | 2 | unit |
| 18 same-second names | 2 | unit |
| 19 two cards drawn | 1, 6 | `stickies.png` + sim |
| 20 six most recent pinned | 2, 6 | unit + sim |
| 21 hint + click launches notes | 6 | sim (log) |
| 22 `stickies = 0` → exit | 6 | sim |
| 23 click card → `notes <path>` | 6, 7 | sim (log) |
| 24 drag header → `config.ini` | 0, 6 | sim (`SIM_CURSOR=follow`) |
| 25 service, quit, reload | 6, 7 | sim (`SIM_MBOX`, `SIM_SERVICES`) |
| 26 picks up changes at next poll | 6 | sim |
| 27 borderless, back-most, scene | 6, 8 | composed scene |
| 28 `shots.sh` notes/stickies + agenda/desktop pass | 3, 6, 8 | run |
| 29 `make` / `make stage` | 3, 6, 8 | **user's PC** (no cross toolchain here) |
| 30 docs/04 + exports | 8 | review + `build_docs.py` |
| 31 `packages.ini` `[notes]` (not published) | 8 | review |
| 32 MIT notices; no kapi/kernel change; UIKit `.abi` appended + docs regenerated | 1, all | review |
| 33 on the Pi | — | **user** |

---

## 8. GUI plan (added by the UX Designer, 2026-10-06 — see `04-ux-design.md`)

The UX design (`04-ux-design.md`, its pictures in `mockups/`, rendered by UIKit in the simulator by
`mockups/mockups.sh` + `mockups/notes_mock.cpp`) fixes the window, the menus, the states and the widget. The steps
of §5 stay in their order; this section **adds to** them (G-items, run inside the step named) and lists what it
**changes**. `notes_mock.cpp` is MIT and may be lifted from: its `NoteList::onDraw`, the Stickies `card ()`, the
colour constants and `wall_text` are the drawing the design asks for.

### 8.1 Changed against the original plan

| Was (03 §1–§5) | Now | Why (04) |
|---|---|---|
| UIKit change: `uk_text_wrap` only | **+ `uk_text_over`** (text over a see-through canvas, blended by coverage, with a face or the bitmap fonts; shadow: none / soft / engraved) **+ `WKT_TRASH`, `WKT_PIN`** tool icons (appended before `WKT_COUNT`) | D11 (a face's `uk_text` ignores the alpha mode: the header came out garbled), D13 (reusable icons, kits first) |
| Toolbar "colour palette as a split button or 6 dots" | **six round `ToolButton` toggles** (`ToolIconFn` drawing the dot), the current one lit | D5 |
| `NoteEdit : Textarea` with the theme's colours | `NoteEdit` **in the note's paper** (`setColors`) and a **15-px `FtTextFace`** in a `UkFaceScope` around `onDraw` / `onMouse` / `onKey` | D6 |
| New Note: a plain `ToolButton` | `ToolButton` `filled = true`, `setOn (true)`, not a toggle (the accent pill of Mail / Calendar) | D7 |
| A note emptied → `kapi_remove` | → the **Trash** (`notes_trash`), no notification | D3 |
| R3: only launch Stickies, no autostart write | Notes **appends `run stickies`** to `SD:/etc/autostart` when it starts Stickies and no active line runs it (never edits other lines; never on Hide) | D2 |
| — | A failed save: red status line, retry at each pause; at quit, the text **to the clipboard** + a notification | D8 |
| Menus: File (New ^N, Delete ^D, Open in Text Editor, Export…), Edit, Note, View | the same, **+ Ctrl+E** for Open in Text Editor, Delete Note last in File after a separator, Copy Note without shortcut, *Find… ^F* first in View; labels exactly as 04 §3 | D1, 04 §3 |
| Stickies: fixed-height cards, window ~240 wide, `kapi_resize_window` on change | cards **of their own height** (60–145 px, ≤ 6 body lines), window **240 × (screen_h − 160)** made once, the area under the cards fully see-through (click-through); default **x = screen_w − 248, y = 40**; "+N more in Notes" when they do not fit | D9, D10, 04 §8.1 |
| Stickies hint *"No notes pinned — open Notes"* (one line) | a dashed place with **"No notes pinned"** / *"Click to open Notes"* | 04 §8.1 (AC 21's text) |
| Search: a field above the list | a `HintBox` at the tool bar's right (Mail's place); **`HintBox` moved into UIKit** when the search is built | D12 |
| AC 10 "a check mark" | the **lit Pin toggle** + the label *Unpin from Desktop* | D1 |

### 8.2 The GUI steps

- **G1 (in step 1, UIKit)** — `uk_text_over (Canvas &cv, int x, int y, const char *s, unsigned ink, int style, int
  shade, unsigned back)` in `text.h` / `text.cpp` (shade 0 none, 1 soft shadow, 2 engraved on `back`; with a face:
  the glyphs drawn into a scratch canvas, their coverage blended with `uk_blend_px`; without: the bitmap glyphs as
  agenda's `wall_text`); `WKT_TRASH`, `WKT_PIN` in `toolbar.h` (before `WKT_COUNT`) and their geometry in
  `toolbar.cpp`. `uikit.abi` + the `uk_text_over` line (append). `python tools/docgen/kitdocs.py`; docs/06 UIKit
  one line each. *Test*: the wrap test of step 1 + an alpha-canvas case for `uk_text_over` (a pixel of a glyph is
  opaque ink, a pixel beside it keeps `0xFE`/`0xFF` transparency); `studio.sh`-style look at the two icons
  (Widget Showcase unchanged). The agenda keeps its own `wall_text` this round (moving it: a later clean-up).
- **G2 (in step 2, the model)** — the colours of 04 §7 in `notesmodel.cpp` (`notes_colour_paper`,
  `notes_colour_dot`, `NOTE_INK 0x2B2925`); `notes_date_label (modified, now, out)` → *09:15 / Yesterday / Fri /
  15 Sep / 15/09/2025 / ""* (04 §2.2) — unit-tested (each case, the year change, `modified = 0`).
- **G3 (in step 3, the window)** — the widget tree of 04 §2.1 exactly (`ToolBar` 44, `HSplitter` split 250 min
  180 / 260, `NoteList`, `Panel` + `Label` info line + `NoteEdit`, status `Label`s 24); `NoteList` drawn as 04 §2.2
  (head 44 with the count badge, rows 56, dot, bold title, date, preview, pin, tinted selection, scroll bar); the
  first-start state of 04 §5 (the *New Note* row, the info line's hint, Delete greyed). `build_menu ()` with the
  labels of 04 §3. `shots.sh`: scenarios **`notes`** (the mock-up's `window` scene: six sample notes, *Shopping*
  selected → `screenshots/notes.png`) and **`notes-empty`** (no `Notes` folder → `screenshots/notes-empty.png`),
  the sample notes of the mock-up (`notes_mock.cpp`'s `g_notes`, written as `tools/tests/desktop_sim/sd/Notes/*.txt`
  + `notes.ini`, dated for the simulator's clock).
- **G4 (in step 4, editing)** — the status line's messages (*✓ Saved*, *This note is full (64 KB)* for 4 s, the
  red *Not saved…*), D8 at quit. *Test*: a sim case with the writes' `Notes` folder made read-only (`chmod a-w`):
  the status text in the dump's log / no file; after `quit`, `clip_get_text` (the fake clipboard) holds the text and
  the log has the notification.
- **G5 (in step 5)** — the tool bar's Pin toggle and the six colour toggles kept in sync with the note (and the
  menu's labels re-published); Delete in the list; the emptied note to the Trash (D3); the import refusal
  `uk_messagebox` (04 §6; several files: one box). *Test*: AC 9 / 10 (the toggles' `on` read back through the dump
  — or the `notes.ini` values), a drop of a 70 KB file → the box (the dump shows it) and no new note.
- **G6 (in step 6, Stickies)** — the geometry of 04 §8.1 (header via `uk_text_over`, ink from the wallpaper as the
  agenda's; cards of their own height via `uk_text_wrap`; band, shadow, hover outline + chevron; the dashed
  empty place; "+N more"; click-through below the cards). `shots.sh`: **`stickies`** (three pinned cards, crop of
  the top right → `screenshots/stickies.png`) and **`stickies-empty`**; the **desktop scene** of AC 27 = the
  mock-up's `desktop-stickies` (agenda, Stickies, the Notes window in front, the dock, the bar) — as a new
  `screenshots/notes-desktop.png`, leaving `desktop.png` as it is.
- **G7 (in step 7)** — *View ▸ Show / Hide Stickies…* label toggle; **ensure the autostart line** (D2): read
  `SD:/etc/autostart`, if no line whose first words are `run stickies` (after spaces, not starting with `#`) → append
  `# Stickies: the pinned notes on the desktop (Notes, View menu)\nrun stickies\n`; the first pin while
  `stickies = 1` and the service absent → launch + the same. *Test*: sim — Show twice → the writes' `etc/autostart`
  has exactly one `run stickies`; with an overlay autostart that already runs it → unchanged; Hide → unchanged.
- **G8 (in step 8, docs)** — docs/04's *Notes* section and *Stickies* paragraph take the pictures of G3 / G6 and the
  menus / keys tables of 04 §3–§4; `tools/icons/notes_icon.py` draws the two icons of 04 §9.
- **G9 (step 9, should)** — the search: `HintBox` into UIKit (`uikit/hintbox.h` or in `textbox.h`, a new class:
  append-only, `uikit.abi` lines; Mail / Calendar / Photos switch to it in a later round), the *Found / n of N*
  head and the empty line of 04 §5, Ctrl+F, Esc; *Open in Text Editor* Ctrl+E; *Export…* (`uk_file_save`); the
  checklist boxes of 04 §8.1 in Stickies (drawing first, the click-to-tick last).

### 8.3 Acceptance criteria touched

AC 10 (read as D1), AC 11 (the label toggle), AC 19 (the cards of 04 §8.1), AC 21 (the hint's text), AC 27 (the
desktop scene `notes-desktop.png`), AC 28 (the scenarios `notes`, `notes-empty`, `stickies`, `stickies-empty`).
New checks from the design: the failed-save path (G4), the autostart line (G7), the import refusal (G5), the
date labels (G2).
