# AutoDev round 1 — Development: Notes + Stickies

## Developer A — steps 0–2

Date: 2026-10-06. Branch `AutoDev`. Inputs: `03-technical-analysis.md` (§3.2, §3.6, §3.7, §5 steps 0–2, §8 G1 / G2,
§9), `04-ux-design.md` (§2.2 dates, §7 colours, §8 Stickies' text, D11, D13), `05-validation.md` (both
validations' developer notes). Everything below builds and runs on the host (g++ 13.3); there is no aarch64
cross compiler in this container (03 §0), so nothing here was built for the Pi.

### Step 0 — the simulator's test tooling

**Done** (`tools/tests/desktop_sim/fakekapi.cpp`, every addition documented in its header comment; nothing
changes while its variable is unset):

| Addition | What it does |
|---|---|
| `SIM_SERVICES="notify,stickies"` (or `-`) | `kapi_ipc_lookup` answers **only** the listed names (pid 700 + index), even with `SIM_MBOX`; `kapi_ipc_register` (both table entries, `ipc_register` and `ipc_register_note`) gives 1 for any name; `kapi_mailbox_send` to a listed pid is logged `sim: send <name> type <t> "<payload>"` (NUL as `\0`, other control bytes `\xNN`). `SIM_IPC` is tested first (validation 2, note 4): the two are not meant to be combined, and the header says so. |
| `SIM_ROFS="SD:/Notes[,SD:/etc]"` | `save_file`, `mkdir`, `remove`, `rename` (either side) and `file_out` under a listed folder fail (prefix compared without the volume, case-insensitively, on a `/` boundary), logged `sim: rofs <path>`. |
| `SIM_CURSOR=follow` | `kapi_cursor_pos` gives the scripted pointer in **screen** coordinates: the client origin the window had at the last `down` (before any: at the first pointer step) + the scripted point. A drag `down 50 10;move 150 12` moves a self-dragging widget (agenda's code) by 100 px. |
| Script step `copy SRC DST` | the host file SRC copied to the card path DST (into `SIM_WRITES`, folders made) at that step, logged `sim: copy DST`. |
| `SIM_STAT=1` | `kapi_path_stat` (host `stat ()` of what the app would read: writes, overlay, card — size, mtime UTC, mode, attr) and `kapi_clock_info` (the fixed clock 2026-09-28 12:34:00 UTC, `tz_minutes` 0; the PC's with `SIM_REALNET`). **Deviation**: opt-in, not always on — with them always on, the Preload applet (`preloadconf`, which calls `kapi_path_stat`) would draw differently. Unset: `-KAPI_ENOSYS` as before. |
| window flags logged | every window made: `sim: window <title> flags 0x<hex>` (AC 27: Stickies must log `flags 0x33`). |

**Sample data**: `tools/tests/desktop_sim/sd/Notes/` — the mock-up's six notes (`notes_mock.cpp`'s texts,
names and `modified` dated ≤ 2026-09-28: *Onyx to-do* blue pinned 11:48, *Shopping* yellow pinned 09:15, *Wi-Fi
at the club* green pinned yesterday, *Gift ideas for Léa* pink Fri, *Pi 4 GPIO pins* grey 15 Sep, *Books to read*
purple 2 Sep) + `notes.ini`. **Deviation**: the `\r\n` orphan note and AC 26's changed `notes.ini` are **beside**
the folder, not in it — `sd/notes-crlf.txt` and `sd/notes2.ini` — so `sd/Notes/` is exactly the six notes of
`notes.png`; a test copies them in when it wants them (`cp`, or the script's `copy`). `notes-crlf.txt` is
marked `-text` in `.gitattributes` (the repo's `* text=auto eol=lf` would have stored it with `\n`).
`sd/README.md` says all this.

**Probe**: `tools/tests/notes/sim_probe.cpp` (one case an invocation), run by `run_notes_test.sh`: `notify`
(SIM_SERVICES=notify: `notify ()` logs one send, launches no notifyd, consumes no script step — the script is
`exit`), `lookup` (SIM_SERVICES=- + SIM_MBOX: lookup of notes/stickies = 0, register = 1, SIM_MBOX still read),
`listed` (pids 700..702, a send logged), `rofs` (6 refusals logged, `NotesX/` not under `Notes`, `SD:/x.txt`
written), `copy` (absent before the step, the exact bytes after one `msleep`), `follow` (screen coordinates
across a `kapi_move_window`; `sim: window probe flags 0x33`), `stat` (mtime of a file touched to 2026-09-20 08:00
UTC, `-KAPI_ENOENT`, the clock), `nostat` (`-KAPI_ENOSYS` without SIM_STAT).

### Step 1 — UIKit and SystemKit

**UIKit** (`user/Kits/uikit/`):

- `text.h` / `text.cpp`: `int uk_text_wrap (const char *s, int n, int w, int maxLines, int *start, int *len, bool
  *more = 0, int style = 0)` — `\n` honoured (blank lines kept, no line after a final `\n`), broken at the last
  space that fits (the spaces at the break dropped, an indent kept), a word wider than `w` cut at a character
  (UTF-8 via `uk_u8_next`, never inside one; at least one character a line), `*more` = more than blanks left.
  `void uk_text_over (Canvas &cv, int x, int y, const char *s, unsigned ink, int style = 0, int shade = 0, unsigned
  back = 0)` — the glyphs drawn white on black into a scratch canvas by `uk_text` (the face **or** the bitmap
  fonts), their coverage then blended with `uk_blend_px` (so the alpha mode is heeded); shade 1 = the agenda's soft
  shadow, 2 = engraved with `uk_tone (back, 205)`. The agenda keeps its own `wall_text` (03 G1).
- `toolbar.h` / `toolbar.cpp`: `WKT_TRASH` (lid, handle, can, two ribs) and `WKT_PIN` (an upright push pin),
  appended before `WKT_COUNT` (no program uses `WKT_COUNT`), drawn with the same `Pen` geometry as the others
  (looked at, 18 and 30 px: consistent with `WKT_SEARCH` … `WKT_GEAR`).
- `uikit.abi`: **`779 _ZN5uikit12uk_text_overERNS_6CanvasEiiPKcjiij`**, **`780 _ZN5uikit12uk_text_wrapEPKciiiPiS2_Pbi`**
  appended. Checked by running `tools/libgen/libgen.py --nm nm` (without `--vtables`: the x86 relocations are
  not read) on the host-built UIKit objects against a copy of the old `.abi`: it appends exactly these two
  lines, in this order (it sorts the new names), and nothing else. **To be confirmed by the user's `make`**
  (libgen on the AArch64 objects; the Itanium mangling is the same on LP64: `j` = `unsigned`).
- No class changed: the layout lock (`abi.h` / `layout_lock.cpp`) is untouched.

**SystemKit** (`user/Kits/systemkit/`):

- `autostart.h` (C-compatible, beside `preloadini.h`) + `autostart.inc`: `int autostart_has (const char *cmd)`,
  `int autostart_ensure (const char *cmd, const char *after, const char *comment)` exactly as 03 §3.7 (returns 1
  there / 2 added / 0 not written; `AUTOSTART_MAX` 16384). Word-wise matching (`run stickies --x`, `run   stickies`
  match; `run stickiesX`, `# run stickies`, `#setup# …` do not); `\r\n` files kept; a last line without its `\n`
  closed. The `.inc` is valid C (no `bool`, no references); its helpers are `static inline as_*_` (validation 2,
  notes 2 and 3).
- `systemkit.h` includes it with the C subjects (header comment updated), `systemkit.cpp` builds it,
  **`systemkit.abi` + `59 autostart_ensure`, `60 autostart_has`** — what libgen appends when run (`--nm nm`) on
  `systemkit.cpp` built for the host with `SK_API = extern "C"` against the old `.abi` (identical file). To be
  confirmed by the user's `make` as well.
- `tools/docgen/kitdocs.py`: `systemkit/autostart.h` added to SystemKit's header list (+ "started at boot" in its
  blurb). `python tools/docgen/kitdocs.py` → `docs/11-UIKIT.md` (781 entries, the two functions, the enum) and
  `docs/12-SYSTEMKIT.md` (61 entries, the new section) regenerated. `docs/06-KITS-GUIDE.md`: a UIKit paragraph
  (text in an owner-drawn widget: fit / wrap / over, the two icons) and a SystemKit one (starting a program at
  every boot) + the subjects table row.

### Step 2 — the notes model

`user/Apps/notes/notesmodel.h` / `notesmodel.cpp` (MIT; uses AppKit, FileKit's `fsutil` — `fs_text_fix`,
`fs_join`, `fs_exists`, `fs_ci_cmp`, `fs_basename`, `fs_copy` — and SystemKit's `trash_move`; no float, no
UIKit), `user/Apps/notes/stickies_proto.h`. Everything of 03 §3.2 and G2, with these **additions / precisions**
(for the next developers):

| Item | Note |
|---|---|
| `NoteInfo` | + `preview[96]` (the list's second line, 04 §2.2), + `saved` (its file is on the card; a new note not written yet is `false` and gets no `notes.ini` section). `file[72]` / `title[96]` as planned (`NOTE_FILE`, `NOTE_TITLE`). |
| `Notes` | + `total`: the `.txt` found; > `count` when only the `NOTES_MAX` (512) newest are kept (04 §5's "Showing the 512 newest notes"). |
| `notes_scan` | ini first, then the listing; a `.txt` without a section: yellow, not pinned, `notes_file_time ()` (0 in the simulator unless `SIM_STAT=1`); folders, `notes.ini`, non-`.txt`, names ≥ 72 chars skipped; titles/previews from the first 2 KB (`NOTE_HEAD`); sorted newest first (then the name descending). It **resets** the list: an unsaved new note held by the app is not kept — re-add it. |
| `notes_sort (Notes &)` | new: re-sort after a write if the app wants the edited note to move up (`notes_write` does not move it). |
| `notes_read` | a bare name (in `NOTES_DIR`) or any path (a drop). `-1` missing, `-2` > `NOTE_MAX_BYTES` after the BOM / UTF-16 fix (never cut; a file over 128 KB refused unread), `-3` buffer too small; a buffer of `NOTE_MAX_BYTES + 1` always fits. Exactly 65 536 bytes are accepted. |
| `notes_write (s, file, text, len, now)` | makes `SD:/Notes`, writes the `.txt` (`0` / `-1` failed / `-2` > 64 KB), sets the entry's `modified`, `saved`, title, preview (adds it at the top when absent). It does **not** write `notes.ini`: call `notes_save_ini (s)` after (03 §3.3). |
| `notes_add_new (s, now)` / `notes_forget (s, i)` | new: the in-memory new note (a reserved name, yellow, unsaved) at index 0; dropping an entry. |
| `notes_new_name (const Notes &s, out, cap, now)` | **takes `s` first** (03 wrote `notes_new_name (out, cap, now)` but asked for "not already in s"). |
| `notes_trash (s, file)` | an unsaved note is only forgotten; else `trash_move` + out of `s` + `notes_save_ini`. No notification (the app's, AC 8 / D3). |
| `notes_pinned (s, idx, max)` | the `max` most recent pinned **and saved** notes, newest first. |
| `notes_preview`, `notes_colour_label` | new: the second non-blank line; "Yellow"… for menus and tips. |
| `notes_date_label (modified, now, out, cap)` | 04 §2.2: same day `09:15`, the day before `Yesterday`, 2–6 days before the weekday (`Fri`), this year `15 Sep`, else `15/09/2025`, `0` → `""`. (A week ago or more → the date: 21 Sep seen on Mon 28 Sep is "21 Sep".) |
| `NotesCfg` | defaults 1, "", 760, 480, 250; read with the model's own small `.ini` parser (sections ignored, inline `; …` comments dropped — the Control Panel's App Settings may add a `[notes]` line), written as plain lines. AppKit's `app_ini_*` is not used anywhere in the model (its single store is the program's). |
| `stickies_proto.h` | `NOTES_SERVICE "notes"`, `STICKIES_SERVICE "stickies"`, `NOTES_MSG_OPEN 1` (payload: a path or a bare name + 0; empty = only come forward), `STK_MSG_RELOAD 2`, `STK_MSG_QUIT 3`. |
| Memory | static: the ini table (1024 sections) and its 160 KB text buffer, a 2 KB head buffer; `Notes` itself is ~140 KB — the app should make it static, not on the stack. |

### Commits (branch `AutoDev`)

| Commit | What |
|---|---|
| `2e95b4c7` | step 0: fakekapi additions, `sd/Notes` + `notes2.ini` + `notes-crlf.txt` (+ `.gitattributes`), `sim_probe.cpp`, `run_notes_test.sh` |
| `6885b62c` | step 2: `notesmodel.h/.cpp`, `stickies_proto.h`, `model_test.cpp` |
| `8935d221` | step 1: UIKit `uk_text_wrap`, `uk_text_over`, `WKT_TRASH`, `WKT_PIN`, `uikit.abi`, `docs/11`, `wrap_test.cpp` |
| `7734a7a5` | step 1: SystemKit `autostart.h/.inc`, `systemkit.h/.cpp/.abi`, `kitdocs.py`, `docs/12`, `autostart_test.cpp` |
| `6ab36965` | step 1: `docs/06`; `run_notes_test.sh` runs the kit tests |
| `aeca3555` | fix: `run_trash_test.sh` silently passed while its test did not compile (below) |
| (this file) | `06-development.md` |

### Tests run (exact commands, from the repository's root) and results

| Command | Result |
|---|---|
| `sh tools/tests/run_notes_test.sh` | **exit 0**: `notes: the simulator's additions: ok` (8 probe cases + log checks), `wrap: 58 checks passed`, `autostart: 59 checks passed` (+ the C compile check of `systemkit.h` with `gcc -std=c99 -Wall -Wextra`: no warning from `autostart.inc`), `model: 143 checks passed`, `notes: all checks passed`. UBSan on (`-fsanitize=undefined -fno-sanitize-recover`); **ASan cannot be used** with `fakekapi.o`: its shadow memory covers `KAPI_TABLE_VA` (14 GB), where fakekapi maps the kernel's table (said in the script). |
| `sh tools/tests/desktop_sim/shots.sh agenda desktop preloadconf` (after step 0; 3 min 47 s, exit 0) | `agenda.png`, `desktop.png` **byte-identical** (no git diff). `preloadconf.png` differs — **not from this work**: the committed picture is stale against the card (it lacks *3DForge* in the list, older buttons); restored with `git checkout`, not committed. |
| `sh tools/tests/desktop_sim/shots.sh agenda desktop widgets` (after step 1, UIKit + SystemKit changed; exit 0) | `agenda.png`, `desktop.png` identical; the Widget Showcase `widgets.png` differs only in one file name of a card listing (`shelf.ini` → `dock.ini`: stale picture, unrelated); restored, not committed. |
| `sh tools/tests/desktop_sim/shots.sh … calendar` | the four `calendar*.png` identical. (`letters` cannot run here: it fails to link on the host — PrinterKit's `print_*` — the pre-existing issue 03 §4 R7 names; not caused by this work.) |
| `sh tools/tests/run_trash_test.sh` | was **silently broken** before this work: `trash_test.cpp` included `systemkit/systemkit.h`, which the mock kernel cannot build, and the script chained the run with `&&`, so it exited 0 without testing. Fixed: the test includes `systemkit/trash.h`, the run is not chained → `trash/fsutil: all checks passed`. |
| `sh tools/tests/run_clipboard_test.sh`, `run_letters_test.sh`, `run_mail_test.sh` (they build fakekapi / SystemKit / UIKit) | all exit 0 (`0 failure(s)`, `14952 checks, 0 failed`, `mail: all good`). |
| `python3 tools/docgen/kitdocs.py` | regenerated `docs/11`, `docs/12` (no other kit doc changed). |
| `g++ -O2 -Wall -Wextra -c user/Apps/notes/notesmodel.cpp` | no warning. |

### What could not be done here

- **The Pi build** (`make`, `make stage`, libgen on the AArch64 objects): no cross compiler. The hand-appended lines
  `uikit.abi` 779–780 and `systemkit.abi` 59–60 are what libgen itself appends from the host objects, but only the
  user's `make` confirms them (libgen dies on a misplaced line). AC 29 / 33 stay with the user.
- **`python docs/build_docs.py`** (the Word / PDF exports of docs/06, 11, 12) was **not run**: step 8 regenerates
  every export once, after the docs/04 and HANDOFF changes (03 §5 step 8), to avoid churning the binary exports
  twice.

### Hints for the next developer (Notes window, Stickies, integration)

- **Run** `sh tools/tests/run_notes_test.sh` (~1 min: it builds UIKit for the host) after any change to the model,
  UIKit's text code, SystemKit's autostart or fakekapi.
- **Seed** each Notes / Stickies scenario: `rm -rf $W; mkdir -p $W; cp -r tools/tests/desktop_sim/sd/Notes $W/Notes`
  then `SIM_WRITES=$W` (an overlay folder cannot be listed, 03 §2). Add `SIM_STAT=1` when a hand-put note's date
  matters (else `modified` 0: sorted last, no date). For AC 26: `…;copy tools/tests/desktop_sim/sd/notes2.ini
  SD:/Notes/notes.ini;…` (Shopping unpinned, *Books to read* pinned + blue, Wi-Fi touched at 12:15).
- **Notifications** in a scenario: always set `SIM_SERVICES` (e.g. `notify` or `notify,stickies`), else
  `notify ()` waits 40 script steps for notifyd. With `SIM_SERVICES=-`, `kapi_ipc_lookup ("notes")` is 0 and
  `kapi_ipc_register ("notes")` 1 (Notes runs alone).
- **Stickies' window** must log `sim: window stickies flags 0x33` (BORDERLESS 1 | BACKMOST 2 | SYSTEM 16 | ALPHA 32).
- **Drag** (AC 24): `SIM_CURSOR=follow` and a script in client coordinates of the place at the press.
- **The read buffer** for `notes_read` is `NOTE_MAX_BYTES + 1`. `Textarea (…, capacity)` holds `capacity - 1`
  characters (its `setContent` / `insertChar` stop at `cap - 1`): create the editor with `NOTE_MAX_BYTES + 1`, so a
  note of exactly 64 KB (accepted by the model) is shown whole, and "full" is `len () >= NOTE_MAX_BYTES`.
- **Status texts** of View ▸ Show Stickies are keyed on `autostart_ensure`'s result (validation 2, binding): 2 →
  "Stickies shown, and started at every boot (SD:/etc/autostart).", 1 → "Stickies shown.", 0 → "Stickies shown
  (autostart not written)." Call it as `autostart_ensure ("run stickies", "run agenda", "# Stickies: the pinned
  notes on the desktop (Notes, View menu)")`; on the shipped card file it inserts `#setup: run stickies` right after
  `#setup: run agenda` (asserted in `autostart_test.cpp`). Step 8 ships `#setup: run stickies` in
  `sdcard/etc/autostart`; `sdcard_lite/etc/autostart` gets it at the next publish (validation 2, note 6).
- **Header text on the wallpaper**: `uk_text_over (canvas, x, y, "Pinned notes", ink, 2 /*bold*/, light ? 2 : 1,
  wallpaperColour)` inside `uk_paint_alpha (true)`; wrapped card bodies: `uk_text_wrap (body, len, 196, 6, st, ln,
  &more)` then `uk_text_fit` the last line with "…" when `more`.
- **Makefile / shots.sh** (step 3): `FT_EXTRA_notes = Apps/notes/notesmodel.cpp` (and for stickies); FileKit and
  SystemKit import libraries are already linked into every app. In `shots.sh`, apps that print (Letters, Photos…)
  do not link on the host: do not call PrinterKit from Notes / Stickies.
- `shots.sh` overwrites `screenshots/*.png` it is asked for, and several committed pictures are already stale
  against the card (`preloadconf.png`, `widgets.png`): commit only the Notes / Stickies pictures.

## Developer B — steps 3–5

Date: 2026-10-06. Branch `AutoDev`. Inputs: 03 §3.3, §5 steps 3–5 (+ G3–G5) and step 9 (Ctrl+E), 04 §2–§7 and the
mock-ups (`notes-window.png`, `notes-empty.png`, the menus, `notes-dialog.png`, `notes-error.png`), 05 (binding notes),
Developer A's hints above. Built and run on the host only (no aarch64 compiler here).

### Step 3 — the window

- `user/Apps/notes/main.cpp` (MIT): the widget tree of 04 §2.1 exactly — `ToolBar` 44 (New Note as the filled accent
  `ToolButton`, Delete `WKT_TRASH`, Pin `WKT_PIN` toggle, six round colour toggles drawn by a `ToolIconFn`),
  `HSplitter` (split from `config.ini`, min 180 / 260), `NoteList` | `Panel` (`InfoLine` 34 + `NoteEdit`), a
  `StatusLine` 24. `NoteEdit : Textarea` in the note's paper (`uk_mix (C_FIELD, paper, 150)`, `NOTE_INK`), a 15-px
  `FtTextFace` in a `UkFaceScope` around draw / mouse / keys, capacity `NOTE_MAX_BYTES + 1`. The info line: "Today,
  09:15 · Yellow · (pin) On the desktop" (a long date helper in main.cpp), the empty state's hint; the status line:
  "6 notes · 3 on the desktop" / "No notes yet" / "Showing the 512 newest notes" at the left, the message (dim, ✓
  Saved, or red ✕) at the right. Window from `config.ini` width / height, resizable, `setMinSize (520, 320)`.
- `user/Apps/notes/notelist.h` (MIT): `NoteList`, owner-drawn as 04 §2.2 (head with the count badge, 56-px rows: dot,
  bold title / *New Note* bold italic dim, date via `notes_date_label`, preview / "Now", pin glyph, tinted rounded
  selection 90 / 60, separators), UIKit's scroll bar (`uk_draw_vscroll`, `UkBarDrag`), the wheel; keys Up / Down /
  PgUp / PgDn / Home / End, Enter / Tab → editor, Delete → delete. Callbacks to the app (`onPick`, `onEnter`,
  `onDelete`). Also `notes_draw_dot` (the list's and the tool bar's dot).
- Menus (`build_menu`, re-published on every state change): File (New Note ^N / Open in Text Editor ^E / Delete
  Note ^D), Edit (Cut, Copy, Paste, Select All, Copy Note), Note (Pin to Desktop ↔ Unpin from Desktop ^P, the six
  colours), View (Show ↔ Hide Stickies…). **Item ids** (the simulator's `menu N`): 0 New, 1 Open in TE, 2 Delete, 3
  Cut, 4 Copy, 5 Paste, 6 Select All, 7 Copy Note, 8 Pin, 9–14 Yellow…Grey, 15 View.
- Start-up: `notes_scan`; the argument's note (`notes <path>` / bare name), else `config.ini`'s `last`, else the
  newest; none → `notes_add_new` (in memory only), caret in the editor; otherwise the list has the focus.
- `user/Makefile`: `notes` in `FT_APPS`, `FT_EXTRA_notes = Apps/notes/notesmodel.cpp`, `notes.elf` deps (its `.h`,
  `docguard.h`, `toolbar.h`). `sdcard/apps/notes.app/app.txt` (`Notes`, `Productivity`) + `icon.bmp` from the new
  `tools/icons/notes_icon.py`, which draws **both** icons (04 §9) — the Stickies one is written to
  `sdcard/apps/stickies.app/icon.bmp` but **not committed** (an `.app` folder without `app.txt` / `main` was left
  out on purpose): Developer C runs `python3 tools/icons/notes_icon.py` when creating `stickies.app`.
- `shots.sh`: `notes` in `APPS` and in the FT list (`stickies` already in the FT list), `extra=notesmodel.cpp` for
  `notes|stickies`, scenario `notes` (the sample notes copied into a writes folder of their own, `last` = Shopping)
  → `screenshots/notes.png`, and `notes-empty` (no `Notes`) → `screenshots/notes-empty.png`. Both compared with the
  mock-ups: identical layout (no search field and no "✓ Saved" in `notes.png`, as validation 1 / the plan say).

### Step 4 — editing and autosave

- Every change of the text (keys, mouse, the menu's Cut / Paste, a text drop) goes through `text_changed ()`: a
  `doc_hash` compare → dirty, `g_lastEdit`, the row's title / preview follow (first `NOTE_HEAD` bytes).
- `onTick`: dirty and 100 ticks (1 s) since the last change → `save_current ()` (`notes_write` + `notes_save_ini`,
  `notes_sort`, the note moves to the top, selection kept by name, "✓ Saved", `stickies_reload ()`). Saved also when
  another note is chosen, on Ctrl+N, on a drop, before Delete / Open in Text Editor, and after `run ()` (quit).
- Empty-note rule (`leave_current`): never-written empty note → forgotten; a saved note emptied by the user → the
  Trash silently (D3). Ctrl+N on an empty new note only focuses the editor.
- Failed save (G4 / D8): red "Not saved: the card is full or read-only — trying again", retried at the next pause in
  typing (not every second); at quit the text goes to the clipboard and `notify ("Notes", "Notes could not save
  “<title>”: its text is on the clipboard")`.
- 64 KB: a typed key refused at the cap → "This note is full (64 KB)." (4 s, dim); a paste past it → "…the paste was
  cut." A note on the card larger than 64 KB (or unreadable) is shown empty, read-only, never written nor trashed,
  the info line pointing to Ctrl+E.
- Idle rescan every 5 s (`folder_sig`: names + sizes of `SD:/Notes`), only while not dirty; keeps the selection and
  an unsaved new note; reloads the current note's text if it changed on the card.

### Step 5 (+ step 9's Ctrl+E)

- Delete (menu, ^D, toolbar, Delete key in the list): saved first, `notes_trash`, `notify ("Notes", "Note moved to
  the Trash")`, the next row selected (none left → a new empty note).
- Colours (menu, six toolbar toggles, kept in sync), pin (^P, menu label toggle, lit toolbar toggle): `notes.ini`
  written at once for a saved note, `stickies_reload ()`; a pin with `stickies = 1` and no `stickies` service →
  `lx_launch ("stickies", 0)` (never autostart).
- Clipboard: the menu's Cut / Copy / Paste / Select All on the editor; Copy Note → `clip_set_text_n`.
- Drops: `DND_FILES` anywhere on the window → each `.txt` / `.md` read with `notes_read` into a new saved note, the
  last one selected; the refused ones (> 64 KB, not text, unreadable) listed in one `ft_messagebox ("Import a note",
  …)` with 04 §6's text for the single-file case. `DND_TEXT` → `insertText` at the caret.
- Ctrl+E: saved, then `lx_launch ("tinypad", "SD:/Notes/<file>")`.

### Hooks left for Developer C (step 7)

- `stickies_reload ()` — done: `STK_MSG_RELOAD` to the `stickies` service after every save / pin / colour / delete /
  import (tested).
- `stickies_set_shown (bool)` — only saves `stickies` in `config.ini` and shows "Stickies shown." / "Stickies
  hidden.". **To complete**: Show → `autostart_ensure (...)` + `lx_launch ("stickies")` if the service is absent + the
  three status texts keyed on its result (validation 2 §2); Hide → `STK_MSG_QUIT`. The menu label toggle and
  `build_menu ()` already work.
- `main ()`: `kapi_ipc_register ("notes")` is done (a failure ignored); the single-instance forward (lookup `notes`
  before, `NOTES_MSG_OPEN` + `kapi_raise_app`, exit) is a marked comment to fill. Receiving `NOTES_MSG_OPEN` is
  done in `onTick` (selects the note, rescanning first if unknown) — not tested yet.

### Commits

| Commit | What |
|---|---|
| `bc07c342` | the Notes window: `main.cpp`, `notelist.h`, Makefile, `notes.app`, `notes_icon.py` |
| `64835118` | `run_notes_sim_test.sh`, `shots.sh` notes / notes-empty, `screenshots/notes.png`, `notes-empty.png` |
| (this file) | `06-development.md` |

### Tests run (from the repository's root)

| Command | Result |
|---|---|
| `sh tools/tests/run_notes_sim_test.sh` (new; ~15 s) | **`notes-sim: all 44 checks passed`**. Builds Notes with `-Wall -Wextra` (no warning allowed) and runs: AC 1, 2, 3, 4 (exact bytes + the three `notes.ini` keys), 5 (nothing written before the pause; the title on the dump), 6, 7, 8 (+ the Delete key), 9 (menu + toolbar), 10 (Ctrl+P once / twice, toolbar), 12, 13, G5 (70 KB drop refused: the box, no note), 14, 15, D3 (emptied → Trash, no notification), G4 / D8 (`SIM_ROFS`), Ctrl+E, the reload message to `stickies`, the idle rescan (a file `copy`'d in mid-run, `SIM_STAT=1`). Dumps of AC 3, 4, 5, 6, 8, 9, 10, 13, G4, G5 as PNGs in `/tmp/onyx_notes_sim/` — looked at. |
| `sh tools/tests/desktop_sim/shots.sh notes` | exit 0 (1 min; the pre-existing PrinterKit link errors of Letters / Photos in the background, as before) → `screenshots/notes.png`, `notes-empty.png`, compared with the mock-ups. |
| `sh tools/tests/run_notes_test.sh` | still `notes: all checks passed` (model, wrap, autostart, probe). |

### Not done here

- The Pi build (`make` for `notes.elf`) — no cross compiler; the Makefile entries follow gpiolab's pattern.
- AC 11 (show / hide) and the single-instance forward: step 7 (hooks above). Docs (docs/04 catalog, docs/03): step 8.
- AC 5's "row follows the title" is checked on the dump by eye (`ac5.png`), not by an assertion.

### Hints for Developer C (Stickies + integration)

- Run `sh tools/tests/run_notes_sim_test.sh` after touching Notes; add the AC 11 / single-instance cases there
  (helpers `seed`, `run`, `check`, `ini`, `cfg_last`, `row`). Remember `key 5` is the **character** '5': write Ctrl
  keys as `key 0x05`.
- In `shots.sh`, `stickies` is already in the FT case list and gets `extra=notesmodel.cpp`; only add it to `APPS`.
  Seed scenarios as the `notes` one does (`$OUT/notes_w`, passed as `SIM_WRITES=`).
- `notes_draw_dot` in `notelist.h` is usable by Stickies if wanted (it includes UIKit's toolbar header).
- `sdcard/apps/stickies.app/icon.bmp`: `python3 tools/icons/notes_icon.py` (draws both icons).
- docs/04 (step 8): Notes' controls are in the header comment of `main.cpp`; the menu ids above.

## Developer C — steps 6–8

Date: 2026-10-06. Branch `AutoDev`. Inputs: 03 §3.5, §3.7, §5 steps 6–8, §8 G6–G8; 04 §3, §5, §8 (and the mock-ups
`stickies*.png`, `desktop-stickies.png`); 05 (validation 2's binding notes); Developers A and B's hints above. Built and
run on the host only (no aarch64 compiler here).

### Step 6 — Stickies

- **`user/Apps/stickies/main.cpp`** (MIT, an FT app): agenda's skeleton. `main`: `notes_cfg_load` → `stickies = 0` →
  `return 0` before any window (AC 22); `kapi_ipc_register ("stickies")`; window **240 × (screen_h − 160)** at
  **x = screen_w − 248, y = 40** or `SD:/apps/stickies.app/config.ini`'s `x` / `y`; flags exactly
  `BORDERLESS | BACKMOST | SYSTEM | ALPHA` (logged `flags 0x33`).
- Drawing per 04 §8.1: the header (the yellow note icon, "Pinned notes" + "(N)" through **`uk_text_over`**, shade 1 on a
  dark wallpaper / 2 engraved on a light one, the agenda's etched line), cards of their own height (band 5 + pad 10 +
  title 18 + 17 × 1..6 lines + pad 10) with **`uk_text_wrap`** at 196 px, "…" on the last line when the text goes on,
  the shadow, the hover outline + chevron; the dashed empty place ("No notes pinned" / "Click to open Notes", the
  `WKT_PIN` glyph); "+N more in Notes" when the cards (≤ 6, `notes_pinned`) do not all fit or more than 6 are pinned.
  Only the header and the cards (and the gaps between them) are `0xFE000000`; below: `0xFF000000` (click-through).
  The pictures match the mock-ups pixel for pixel in layout (compared side by side).
- Poll every 300 ticks: a signature (the folder's names + sizes, `notes.ini`'s bytes, the shown notes' first 2 KB)
  → `notes_scan` + `notes_pinned` + the heads read again only on change; the wallpaper's ink re-read (`read_back`, the
  agenda's); `config.ini`'s `stickies` read again (0 → `kapi_exit (0)`: a Hide that came without the message).
  Mailbox drained each tick: `STK_MSG_RELOAD` → forced reload now, `STK_MSG_QUIT` → `kapi_exit (0)`.
- Click a card → `NOTES_MSG_OPEN` "SD:/Notes/<file>" + `kapi_raise_app ("notes")` when the `notes` service exists, else
  `lx_launch ("notes", path)`; the empty place / "+N more" → the same with no note. Header drag: the agenda's code
  (`kapi_cursor_pos` screen coordinates, `kapi_move_window`, `x` / `y` saved on release), `uk_cursor
  (KAPI_CURSOR_MOVE)` over the header.
- `user/Makefile`: `stickies` in `FT_APPS`, `FT_EXTRA_stickies = Apps/notes/notesmodel.cpp`, `stickies.elf` deps.
  `sdcard/apps/stickies.app/app.txt` (`Stickies`, `Shell`) + `icon.bmp` (`python3 tools/icons/notes_icon.py`; Notes'
  icon came out identical).
- **Test tooling**: `fakekapi.cpp` — a `SIM_MBOX` line `@<ticks>:type:pid:payload` is held back until that many ticks
  after the start (needed for AC 25's reload: all canned messages were otherwise drained at the first tick, before
  the change on the card; the ticks start at 1000, 2 a script step; documented in the header).
  **`tools/tests/notes/cards.py`** reads a Stickies dump back: the cards (opaque runs down x = 224) and their colours
  (the nearest paper), the window's place, a pixel. **`tools/tests/run_stickies_sim_test.sh`** (new).
- `shots.sh`: `stickies` in `APPS`; scenarios **`stickies`** (crop of the top right, the menu bar), **`stickies-empty`**,
  **`notes-desktop`** (agenda, Stickies, the Notes window at 60,166, the dock, the bar with Notes' menus — 04's
  `desktop-stickies` scene; `desktop.png` left as it is) → `screenshots/stickies.png`, `stickies-empty.png`,
  `notes-desktop.png` (committed; compared with the mock-ups: same layout, the only difference the status line's
  "✓ Saved", which the real start does not show).

### Step 7 — Notes ↔ Stickies

- `stickies_set_shown`: **Show** → `stickies = 1` saved, `autostart_ensure ("run stickies", "run agenda", "# Stickies:
  the pinned notes on the desktop (Notes, View menu)")`, `lx_launch ("stickies", 0)` only when the service is absent,
  status by the result (validation 2, binding): 2 "Stickies shown, and started at every boot (SD:/etc/autostart).", 1
  "Stickies shown.", 0 "Stickies shown (autostart not written)." (dim, 4 s). **Hide** → `stickies = 0` saved,
  `STK_MSG_QUIT` to the service if any, "Stickies hidden."; autostart never touched. (The pin's rule was Developer B's
  and is kept: with `stickies = 1` and no service → `lx_launch` only.)
- **Single instance** at the top of `main ()`: `kapi_ipc_lookup ("notes")` > 0 → the argument sent as `NOTES_MSG_OPEN`
  (with its 0; empty = only come forward), `kapi_raise_app ("notes")`, `return 0` before any window; else
  `kapi_ipc_register` (its failure ignored: Notes goes on alone). The reception (Developer B's `onTick`) is now tested.
- Reload after save / pin / colour / delete / import: Developer B's `stickies_reload ()`, unchanged (tested).
- `main.cpp`'s header comment updated.

### Step 8 — integration

- `sdcard/etc/autostart`: `# Stickies: the pinned notes on the desktop (Notes, View menu)` + `#setup: run stickies`
  right after `#setup: run agenda` — byte for byte what `autostart_ensure` inserts (Setup gives it back with the
  others). `autostart_test.cpp`'s card case now takes its "already shipped" branch (56 checks instead of 59).
  `sdcard_lite/etc/autostart` gets the line at the next publish (`mkrepo.py --lite`; validation 2, note 6).
- `tools/pkg/packages.ini`: **`[notes]` before `[onyx]`** — title Notes, Productivity, summary, `icon =
  apps/notes.app/icon.bmp`, `files = apps/notes.app/ apps/stickies.app/`, `config = apps/notes.app/config.ini
  apps/stickies.app/config.ini`, `needs = uikit >= 1.781, systemkit >= 1.61, fontkit` (the tables with the new lines:
  `uikit.abi` 780, `systemkit.abi` 60 → **to be confirmed at publish time**). `SD:/Notes` is runtime data, not packaged.
  **Not published**: `versions.ini`, `sdcard/var/pkg/db`, `sdcard_lite` untouched. Checked with
  `python3 tools/pkg/mkrepo.py --out <scratch>/repo --no-sign --versions <scratch>/versions.ini --bump` (a copy of
  versions.ini, an unsigned scratch repository, nothing pushed): `notes-1.0.0.opk` holds `apps/notes.app/{app.txt,
  icon.bmp}` and `apps/stickies.app/{app.txt,icon.bmp}`; the `onyx` package holds neither.
- **docs/04**: §4 (the autostart lines), §5's intro, a **§5 *Stickies, the pinned notes*** after the agenda widget
  (behaviour, clicks, drag, Show / Hide, files, `stickies.png`), §11's autostart defaults, §12 catalog rows **notes**
  and **stickies**, a **§12 *Notes, quick notes*** section (window, writing, the menus / keys table of 04 §3–§4, drops,
  rescan, single instance, the Stickies switch and its three texts, the files table, `notes.png`, `notes-empty.png`,
  `notes-desktop.png`). **docs/03**: the simulator's tooling for services / read-only folders / drags / `copy` /
  `@ticks` mail, the two test scripts; `stickies` in the Shell category list. **docs/HANDOFF.md**: a section for the
  round (top) + Priority 2's *Quick notes* marked built. **IDEAS.md** (in French, as the file): a row *Notes rapides
  + widget de bureau* with the items left for later — the search (+ `HintBox` into UIKit under its own header), the
  checklist boxes and the tick, Sort by, the drag out, Export…, real menu check marks, keyconf / Setup onto
  `autostart.h`, the agenda's `wall_text` → `uk_text_over`.
- `python docs/build_docs.py`: needed `pip install pypandoc python-docx` first (absent); then **every export
  regenerated, the PDFs by LibreOffice** (1 min, no error); committed.

### Commits

| Commit | What |
|---|---|
| `56f6d360` | step 6: Stickies, Makefile, `stickies.app`, fakekapi `@ticks`, `cards.py`, `run_stickies_sim_test.sh`, shots.sh scenarios + 3 screenshots |
| `c7466279` | step 7: Notes' Show / Hide, single instance, `sdcard/etc/autostart`, `run_notes_sim_test.sh` cases |
| `e6a9b2e2` | step 8: `packages.ini`, docs/04, docs/03, HANDOFF, IDEAS |
| `a91900f5` | step 8: the exports |
| (this file) | `06-development.md` |

### Tests run (from the repository's root) and results

| Command | Result |
|---|---|
| `sh tools/tests/run_stickies_sim_test.sh` (new, ~20 s) | **`stickies-sim: all 29 checks passed`**. Stickies built with `-Wall -Wextra` (no warning in its source). AC 19 (two pinned + one not → `2 yellow green`, at 776,40; the samples → `3 blue yellow green`), AC 20 (8 pinned, short → the 6 newest in order; 8 long → 3–5 cards + "+N more", its click → `launch notes`), AC 21 (no card, the hint catches clicks, click → `sim: launch notes`), AC 22 (no window, no dump), AC 23 (no Notes → `sim: exec SD:apps/notes.app/main SD:/Notes/<Shopping>`; `SIM_SERVICES=notes` → `sim: send notes type 1 "SD:/Notes/<Onyx>\0"` + `raise_app`, nothing started), AC 24 (`SIM_CURSOR=follow` drag (−60, +10) → `x = 716`, `y = 50`, no note opened; next start dumped at 716,50), AC 25 (`@20:3` → `sim: exit 0` before the dump, after its window; `@16:2` reload → the `notes2.ini` change shown at once; without it not yet), AC 26 (`copy notes2.ini` then 200 waits → `3 green blue blue`, not restarted; a note's text changed in place, same size → redrawn), AC 27 (`flags 0x33`, `0xFF000000` below the cards, `0xFE…` under the header), `stickies = 0` written mid-run → it ends at its next poll. Dumps looked at (`/tmp/onyx_notes_sim/*.png`: ac19, ac20, ac20b, ac21, ac26a/b, light on a light wallpaper — engraved ink). |
| `sh tools/tests/run_notes_sim_test.sh` | **`notes-sim: all 67 checks passed`** (Developer B's 44 + 23): AC 11 Hide (`stickies = 0`, `sim: send stickies type 3`, no autostart), AC 11 Show (`stickies = 1`, `sim: launch stickies`), G7 on the pre-round card (inserted right after `#setup: run agenda`, one line, `preload /boot` last, nothing else changed), Show/Hide/Show (one line), the shipped card (not written), a hand-made `run stickies` (unchanged), Show while running (not started again), `SIM_ROFS=SD:/etc` (started, file unchanged), a pin while hidden (nothing started, no autostart) / shown but not running (started, no autostart), the single instance (`sim: send notes type 1 "SD:/Notes/<Gift>\0"`, `raise_app notes`, no window; `SIM_SERVICES=-` → Notes runs), `NOTES_MSG_OPEN` received (`SIM_MBOX`) → that note chosen. The four status texts looked at on the dumps. |
| `sh tools/tests/run_notes_test.sh` | `notes: all checks passed` (wrap 58, autostart 56 — the card branch —, model 143, probe) |
| `sh tools/tests/desktop_sim/shots.sh stickies stickies-empty notes-desktop` | exit 0 (1 min) → the three new pictures, compared with `mockups/stickies.png`, `stickies-empty.png`, `desktop-stickies.png` |
| `sh tools/tests/desktop_sim/shots.sh notes notes-empty agenda desktop` | exit 0; `notes.png`, `notes-empty.png`, `agenda.png`, `desktop.png` **byte-identical** (no git diff). (Letters / Photos' PrinterKit link errors in the background: pre-existing, 03 R7.) |
| `sh tools/tests/run_trash_test.sh`, `run_clipboard_test.sh`, `run_mail_test.sh`, `run_letters_test.sh` (they build fakekapi) | all exit 0 |
| `python3 tools/pkg/mkrepo.py --out <scratch> --no-sign --versions <scratch copy> --bump` | 77 packages into the scratch folder; `[notes]` takes both bundles (above) |
| `python3 docs/build_docs.py` | every `.docx` and `.pdf` regenerated (LibreOffice), "Done." |

### Not done here

- **The Pi build** (`make`, `make stage` for `notes.elf` / `stickies.elf`, libgen confirming `uikit.abi` 779–780 and
  `systemkit.abi` 59–60) — no cross compiler: AC 29 stays with the user, as AC 33 (on the Pi).
- **Publishing** (AutoDev rule): `[notes]` declared only; its `needs` versions to be confirmed when the kits are
  rebuilt and published.

## Summary of the round's development

Steps 0–8 of 03 §5 (+ step 9's Ctrl+E) are done on the branch `AutoDev`: the simulator's tooling, UIKit (`uk_text_wrap`,
`uk_text_over`, `WKT_TRASH`, `WKT_PIN`), SystemKit (`autostart_has`, `autostart_ensure`), the notes model, Notes,
Stickies, their link, the autostart line, the declared package and the docs. No kapi or kernel change. Tests: `sh
tools/tests/run_notes_test.sh`, `sh tools/tests/run_notes_sim_test.sh` (67), `sh tools/tests/run_stickies_sim_test.sh`
(29), the screenshots (`notes`, `notes-empty`, `stickies`, `stickies-empty`, `notes-desktop`; `agenda`, `desktop`
unchanged).

| AC | Status | By |
|---|---|---|
| 1 list newest first, title, date, dot, pin | passed | `model_test` (order), `run_notes_sim_test` AC1, `notes.png` |
| 2 `last` / newest selected | passed | `run_notes_sim_test` AC1, AC2 |
| 3 no folder → empty note, nothing written | passed | `run_notes_sim_test` AC3 |
| 4 typing + 1 s → exact file + `notes.ini` | passed | `run_notes_sim_test` AC4 |
| 5 the row's title follows | passed | `run_notes_sim_test` AC5 (+ the dump looked at) |
| 6 Ctrl+N; an empty note dropped | passed | `run_notes_sim_test` AC6 |
| 7 no rename on title change | passed | `run_notes_sim_test` AC7 |
| 8 Delete → Trash + notification | passed | `run_notes_sim_test` AC8 |
| 9 colour green | passed | `run_notes_sim_test` AC9 |
| 10 Ctrl+P pin / unpin | passed | `run_notes_sim_test` AC10 |
| 11 Show / Hide Stickies | passed | `run_notes_sim_test` AC11, G7 |
| 12 Copy Note / Ctrl+V | passed | `run_notes_sim_test` AC12 |
| 13 a dropped `.txt` → new note | passed | `run_notes_sim_test` AC13, G5 |
| 14 `notes <path>` | passed | `run_notes_sim_test` AC14, AC10 |
| 15 closed right after typing → written | passed | `run_notes_sim_test` AC15 |
| 16 `\r\n`, > 64 KB refused | passed | `model_test` |
| 17 orphan `.txt` / orphan section | passed | `model_test` |
| 18 same-second names | passed | `model_test` |
| 19 two cards drawn | passed | `run_stickies_sim_test` AC19, `stickies.png` |
| 20 the six most recent pinned | passed | `model_test` (`notes_pinned`), `run_stickies_sim_test` AC20 |
| 21 hint + click launches Notes | passed | `run_stickies_sim_test` AC21, `stickies-empty.png` |
| 22 `stickies = 0` → exit, no window | passed | `run_stickies_sim_test` AC22 |
| 23 a card's click → Notes on that note | passed | `run_stickies_sim_test` AC23 (launch / send + raise), `run_notes_sim_test` AC23 (received) |
| 24 header drag → `config.ini`, reopens there | passed | `run_stickies_sim_test` AC24 |
| 25 the service, quit, reload (+ Notes' single instance) | passed | `run_stickies_sim_test` AC25, `run_notes_sim_test` AC25 |
| 26 a change picked up at the next poll | passed | `run_stickies_sim_test` AC26 |
| 27 borderless, back-most, the desktop scene | passed | `run_stickies_sim_test` AC27 (flags 0x33, click-through), `notes-desktop.png` |
| 28 `shots.sh` notes / stickies + agenda / desktop | passed | `shots.sh` runs above (agenda, desktop byte-identical) |
| 29 `make` / `make stage` | **deferred to the user** | no aarch64 cross compiler in the container (03 §0); the autostart line part is done |
| 30 docs/04 + exports | passed | review; `python docs/build_docs.py` |
| 31 `[notes]` declared, not published; the autostart line | passed | review; `mkrepo.py` into a scratch folder; `sdcard/etc/autostart`; `autostart_test` |
| 32 MIT notices; no kapi / kernel change; `.abi` appended, kit docs regenerated | passed (review) | the `.abi` lines are libgen's own output on host objects — **the user's `make` confirms them** |
| 33 on the Pi | **deferred to the user** | needs the hardware: a pin visible on the desktop within seconds, a reboot keeps the pins and Show / Hide |

**31 passed, 2 deferred to the user (AC 29: the Pi build; AC 33: on the Pi).**
