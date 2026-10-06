# AutoDev round 1 — Validation 1 (Technical Analyst): Notes + Stickies

Date: 2026-10-06. Reviewed together: `02-product-analysis.md` (33 acceptance criteria, "AC n"),
`03-technical-analysis.md` (§0–§7 and the appended §8 "GUI plan"), `04-ux-design.md` and the mock-ups
(`notes-window.png`, `desktop-stickies.png`, `notes-menu-view.png` looked at). Each claim was checked against
the code, not taken on trust.

## Verdict: **NOT GREEN**

The plan is sound and almost complete. Every AC has a step, every kapi function and kit call it names exists
with the right signature, and no kapi or kernel change is needed. Three gaps remain, all small. One of them is
a design decision that is risky as it stands (the autostart write). Fix them and the next validation can be
GREEN without a new full review.

---

## 1. What was verified (and holds)

| Check | Result |
|---|---|
| AC → step coverage (03 §7 + §8.3) | **All 33 ACs mapped.** AC 29 and AC 33 are explicitly left to the user (no cross compiler). |
| AppKit calls (`user/Kits/appkit/appkit.h`) | All present with the planned signatures: `kapi_path_stat` (507; `struct kapi_stat.mtime` = UTC s, `kapi_abi.h:838`), `kapi_dir_read`, `kapi_clock_info` (`tz_minutes` at offset 24), `kapi_get_datetime`, `kapi_save_file`, `kapi_opendir/readdir/mkdir/remove`, `kapi_ipc_register/lookup`, `kapi_mailbox_send/recv` (812–813), `kapi_raise_app`, `kapi_cursor_pos`, `kapi_move_window`, `kapi_get_args`, `kapi_exec`, `kapi_get_ticks`, `kapi_wallpaper_buffer`, `lx_launch` (928), `app_ini_load_path/get/get_int`; `INI_MAX 64` / `INI_BUFSZ 2048` (876/878), so a separate `notes.ini` parser beside the app is justified. `KAPI_CURSOR_MOVE` (kapi_abi.h:1239), `WIN_FLAG_BORDERLESS/BACKMOST/SYSTEM/ALPHA`, `MB_OK`. **No kapi change.** |
| SystemKit / FileKit / FontKit | `trash_move`, `notify`, `clip_set_text_n`, `clip_get_text`, `fa_open`. `fs_join/basename/exists/is_dir/ci_cmp/text_fix`. `ft_uikit_install`, `FtTextFace::open (family, px)`, `UkFaceScope`, `ft_messagebox`, `ft_file_save`. On the PC, SystemKit and FileKit are inline (`sk_api.h`, `FS_API static inline`), so the simulator links them for free. |
| UIKit widgets named in 04 §2.1 | All exist with the named members: `ToolBar` (`line`, `add`, `addRight`, `sep`), `ToolButton` (`setGlyph`, `setIcon`, `ToolIconFn`, `iconSize`, `setText`, `setToggle`, `fitWidth`, `filled`, `setOn`, `setDisabled`), `WKT_PLUS`, `HSplitter (l,t,w,h,split,bg)` + `minA/minB`, `Panel.bg`, `Label.fg/bg`, `Textarea` (`setColors`, `setContent`, `insertText`, `cut/copy/paste/selectAll`, `len`, `content`, capacity at construction), `Menu` (`item`, `separator`, `publish`, `UK_CTRL`), `Root` (`onTick`, `onDrop`, `setResizable`, `setMinSize`, `onResized`), `uk_rbox`, `uk_rline`, `uk_bead`, `uk_blend_px`, `uk_paint_alpha`, `uk_text`, `uk_tw_n`, `uk_text_fit`, `uk_u8_next`, `uk_mix`, `uk_tone`, `uk_draw_vscroll`, `UkBarDrag`, `uk_cursor`, `WKG_CHECK`, `WKG_CHEV_RIGHT`, the `C_*` colours. |
| UIKit additions | `uk_text_wrap`, `uk_text_over`, `WKT_TRASH`, `WKT_PIN`: none exists yet and none clashes with an app's name (grep). The `.abi` lines are appended (`uikit.abi`, 784 lines, Itanium-mangled names). The enum values go before `WKT_COUNT`, and no program uses `WKT_COUNT`. `toolbar.h` is not in `uikit.h`. `NoteList` and `NoteEdit` stay beside the app (correct: `ListBox` is text only). |
| Menus have no check marks | Confirmed (`Menu::item` spec `I<id>\t<label>\t<shortcut>`, menubar draws none). Label toggles (D1) are the right call. |
| Click-through under the cards (D10) | Confirmed: Elegant's hit test is `(px >> 24) != 0xFF` (`Servers/elegant/wm/window.cpp:431`). The agenda's `CATCH 0xFE000000` works the same way. |
| Makefile | `FT_APPS` / `FT_EXTRA_<app>` / the generic `Apps/%/main.cpp` rule exist (`user/Makefile:45,765–777`): the plan's `FT_EXTRA_notes/stickies = Apps/notes/notesmodel.cpp` fits. |
| Simulator | `fakekapi.cpp` has `SIM_WRITES`, `SIM_ARGS`, `SIM_MBOX`, `SIM_IPC`, `SIM_CLIPFILE`, `drop`, `quit`, a fixed `SIM_CURSOR="x,y"` (client-relative: "follow" is a new mode), and `launch`/`exec` logged. `notify ()` without a service waits 40 × `kapi_msleep (50)` (`notify.inc:24`), which justifies `SIM_SERVICES`. Host `g++` is present and no `aarch64` compiler is: correctly deferred. |
| CLAUDE.md rules | Kits first (wrap, text-over and icons go into UIKit, model beside the app). No program reaches the kernel except through AppKit. MIT on every new file. docs/04 / 06 / 11 (generated) / HANDOFF and `build_docs.py` are in step 8. The package is **declared, not published**. |

---

## 2. Remaining gaps (blocking)

### Gap 1 — the autostart write (D2 / G7) is inconsistent and too broad. Owners: **4** (the decision in 04 D2), **3** (G7 + the helper)

What exists today:
- Exactly one app edits `SD:/etc/autostart`: **Keyboard & Mouse** (`user/Apps/keyconf/main.cpp:56–96`). It **replaces in place** the one `keyb` line, only when the user explicitly picks a layout in a settings UI, and it **says so** in its status line ("…set at every boot (SD:/etc/autostart)").
- **The agenda has no on/off switch at all.** It is only an autostart line (`#setup: run agenda`). Theme kills and relaunches it (`theme/main.cpp:539`). The dock is the same (`dockconf.h:52`: "the user may have taken it out of etc/autostart").
- `preload /boot` is documented as **the last line** of autostart (`systemkit/preloadini.h:3`, `preloadini.inc:49`, and the Preload applet's on-screen text `preloadconf/main.cpp:220`).

Problems with D2 as written:
1. "Append" puts `run stickies` **after `preload /boot`**. This breaks the documented invariant above, and the Control Panel would then show a false statement.
2. It also fires on **the first pin** (G7: "the first pin while `stickies = 1` and the service absent → launch + the same"). A user who removed `run stickies` by hand would see it come back silently on an unrelated action. Keyconf never edits autostart as a side effect.
3. It is silent. Keyconf tells the user.
4. It is a second private copy of keyconf's `set_line` logic. The kits-first rule asks for reusable code in a kit.
5. It does not handle the held-back case. On a card where Setup has not finished, `#setup: run stickies` is pending, and adding an active line would start Stickies during Setup.

**Fix (the consistent approach):**
- Notes edits autostart **only on the explicit *View ▸ Show Stickies on the Desktop*** action, never on a pin. A pin while Stickies is off only launches nothing and leaves the label "Show Stickies…" in place. A pin while it is on and not running just `lx_launch`es it, without touching autostart. Hide never edits autostart, as now.
- Ensure the line: if an active line `run stickies` exists, or a `#setup: run stickies` line exists, change nothing. Otherwise **insert `run stickies` right after the agenda's line** (`run agenda` or `#setup: run agenda`). If there is no agenda line, insert it before `preload /boot`; else append. Keep D2's one comment line.
- Say it: the status line shows *"Stickies shown, and started at every boot (SD:/etc/autostart)"*, or *"… (autostart not written)"* on failure, as keyconf does.
- The code goes into **SystemKit** as a small reusable helper, e.g. `systemkit/autostart.h`:
  - `bool autostart_has (const char *cmd)` returns true for an active line or a `#setup:` line;
  - `bool autostart_ensure (const char *line, const char *after)` inserts the line after the first line whose command starts with `after`, else before `preload`, else at the end.

  The code lives in a `.inc` (inline on the PC), with `systemkit.abi` appended. `SK_API` is `extern "C"`, so the symbol names are plain and easy to append by hand. Add `python tools/docgen/kitdocs.py` and a docs/06 line. Add a PC unit test in `run_notes_test.sh`: the insert position, no duplicate, the `#setup:` case, and `preload` staying last. Switching keyconf to it is a later clean-up (IDEAS), not this round.
- Update 04 D2 and §10, 03 §8.1 (the R3 row) and G7, and the G7 test cases accordingly. Keep docs/04 saying that Stickies' boot start is the autostart line, like the agenda's.

### Gap 2 — the round is too big; the *should* items must be cut explicitly. Owner: **3**

Must is already large: two apps, three UIKit additions plus the SystemKit helper (gap 1), four `fakekapi` additions, a unit-test suite, a scripted simulator test of ~30 cases, four screenshot scenarios plus a desktop scene, icons, and docs. Step 9 lists six *should* items "each independent". Several of them carry hidden cost:
- **Search** needs `HintBox` moved into UIKit, a new class under the layout lock (`uikit/abi.h`, `tools/libgen/layout.py`).
- **Checklist click-to-tick** makes Stickies a second writer of the notes.

**Fix:** in 03 §5 step 9 and §8.2 G9, mark as **out of this round** (moved to "Later", with an IDEAS.md line):
- the search and the `HintBox` move;
- the checklist boxes and the tick;
- *Sort by*;
- the drag out;
- *Export…*.

Keep only **Open in Text Editor (Ctrl+E)** as the one optional *should*: it is one `lx_launch ("tinypad", path)` after a save. The tool bar then has no search field, so the `notes` screenshot will differ from `notes-window.png` (no "Search notes" box) and Stickies draws `[ ]` / `[x]` as plain text. Say so in 04 §1 / §10 (a one-line note). The mock-ups need not be redone.

### Gap 3 — the step-0 test tooling has three holes. Owner: **3**

1. **`SIM_MBOX` answers every `kapi_ipc_lookup` with pid 7** (`fakekapi.cpp:1237`). In any scenario using it (AC 25's quit/reload):
   - Notes' single-instance check ("service `notes` exists → forward and exit") would make Notes quit at once;
   - Stickies' click would take the IPC path instead of `lx_launch`, so AC 23's expected `sim: exec … notes` log would not appear.

   Also, `ipc_register` returns **0** in the simulator for every name but `control`/`dock` (line 1231).

   Fix in the plan:
   - With `SIM_SERVICES` set, `ipc_lookup` answers **only** for the names listed, even with `SIM_MBOX`.
   - `ipc_register` returns 1 for any name.
   - Notes treats a failed register as "go on alone", not "exit".
2. **G4's read-only test uses `chmod a-w`**, but the container (and many CI runners) run as root (`id -u` = 0), so writes still succeed. Fix: a `fakekapi` switch, e.g. `SIM_ROFS=SD:/Notes`, that makes `kapi_save_file` / `kapi_mkdir` / `kapi_rename` under that prefix fail.
3. **AC 26** (Stickies picks up a change at its next poll) relies on "a background `cp` with `SIM_SLEEP=1`", which is timing-dependent and flaky. Fix: a script step in `fakekapi`, `copy SRC DST` (a host file into `SIM_WRITES` at that step). The test then runs `…;copy …/notes2.ini SD:/Notes/notes.ini;waits 200;dump …` deterministically.

Add these to 03 §3.6 / step 0 and to the AC 23 / 25 / 26 and G4 test lines.

---

## 3. Notes for the developer (non-blocking, settle while implementing)

1. **No "window activated" event.** `Root` has no such virtual and AppKit no such event (`root.h`, `GUI_EVENT_*`). 03 §3.3's "rescan when the window becomes active again" is not available. Use the ~5 s idle rescan (optionally also on `GUI_EVENT_PTR_ENTER`). Gate it by a cheap signature (the listing's names + sizes + `notes.ini` size), as Stickies does, rather than re-reading 512 × 2 KB every 5 s.
2. **Dirty marking is not only keys.** `NoteEdit::onKey` covers typing, but the menu's Cut / Paste, a `DND_TEXT` drop (`insertText`) and the 64 KB paste cut (R8) also change the text. Mark dirty and re-title after each, and after `Textarea` mouse edits if any.
3. **Tab** from the editor to the list: intercept `KEY_TAB` in `NoteEdit::onKey` before the base class. Enter / Tab in the list go to the editor.
4. **FT app dialogs:** use FontKit's `ft_messagebox` / `ft_file_save` (`fontkit/uikitface.h:174–180`), not bare `uk_messagebox` (04 §6) / `uk_file_save`, so the dialogs have the app's face.
5. **AC 3's assertion:** check that `SD:/Notes` is **absent** from `SIM_WRITES`, not that the writes are empty. Notes legitimately writes `SD:/apps/notes.app/config.ini` (`last`, size) at quit.
6. **Back-most (AC 27):** the composed scene only illustrates it, because `compose.py` does the layering itself. Also assert the window's flags: log them in `fakekapi`'s `create_window_ex`, or check by review that Stickies passes `WIN_FLAG_BORDERLESS | WIN_FLAG_BACKMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA`.
7. **Stickies' header ink after a wallpaper change:** re-read the wallpaper under the widget in the poll, as the agenda does (`agenda/main.cpp:316`). Theme restarts only `menubar` and `agenda` (`theme/main.cpp:539`), not Stickies.
8. **`uikit.abi` by hand:** slots strictly sequential after 784's last; names from the host `nm` of `text.o`. Flag it in `06-development.md` for the user's `make` (libgen verifies). The `[notes]` package's `needs = uikit >= <new>, systemkit >= <new>, fontkit`: put the next versions and say they are to be confirmed at publish time. `versions.ini` is not bumped in AutoDev.
9. **Packages:** `[notes]` goes before `[onyx]` (03 R2). Check the attribution of `apps/stickies.app/` with `tools/pkg/mkrepo.py` in a dry/local mode only, never `publish.sh`.
   - AC 31's "the autostart line" cannot be a package key (none exists). It is delivered by `sdcard/etc/autostart` (`#setup: run stickies`) plus gap 1's Show action.
   - `SD:/Notes/` is runtime data, not a package file.
10. **`config.ini` without a `[notes]` section** (03 §1.3, deviating from 02 §5.3) is accepted: the Control Panel's App Settings edits plain `key = value` lines.
11. **Sample notes** for the simulator dated ≤ 2026-09-28 (the fixed clock), seeded into a fresh `SIM_WRITES` per scenario (03 §2). `tools/tests/desktop_sim/sd/notes.txt` (tinypad's sample) and `sd/Notes/` coexist (different names, also on a case-insensitive FS).
12. `shots.sh`: add `notes` and `stickies` to `APPS` **and** to the FT case list (`shots.sh:142`), with `extra=user/Apps/notes/notesmodel.cpp` for both. Commit only the intended PNGs (R7).
13. If `HintBox` ever moves into UIKit (not this round, gap 2), it needs **its own header not included by `uikit.h`**. Mail, Calendar and Photos each define a global `HintBox` with `using namespace uikit`, and the name would become ambiguous.
