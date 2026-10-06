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

---

# Validation 2 (Technical Analyst, fresh, 2026-10-06)

Reviewed: `03-technical-analysis.md` (§3.3, §3.5–§3.7, R3, §5 steps 0–9, §6, §7, §8, §9 "Revision after
validation 1") and `04-ux-design.md` (D2, D12, §1 scope note, §3–§5, §10, "Revision after validation 1"), checked
again against the code.

## Verdict: **GREEN**

The three gaps of validation 1 are closed. 03 and 04 agree on the autostart behaviour, the scope and the function
names. They differ only on one status text, which is settled below as a binding note: the user-facing text is 04's,
and the signature is 03's. No AC is lost. Nothing new contradicts CLAUDE.md or the AutoDev rules.

## 1. The gaps of validation 1

| Gap | Closed? | Checked against |
|---|---|---|
| **1. Autostart** | **Yes.** | 03 §3.7 / R3 / §3.3 / G7 and 04 D2 / §3 / §5 / §10 all say the same: the line is written only by *View ▸ Show Stickies on the Desktop*, never by a pin, never by Hide. It is a no-op when `run stickies` or `#setup: run stickies` exists. Otherwise it goes after the agenda's line (keeping its `#setup: ` prefix), else before `preload /boot`, else at the end, and the status line says so.<br>The helper is SystemKit's `autostart_has` / `autostart_ensure`, and the plan matches the real layout:<br>- `systemkit.h` includes its C subjects unconditionally, so `autostart.h` goes beside `preloadini.h`;<br>- `sk_api.h` gives `extern "C"` on AArch64 and `static inline` + `SK_BODIES_INLINE` on the PC, and the `.inc` is pulled into the header as `preloadini.h` does;<br>- `systemkit.cpp` `#include`s each `.h` then each `.inc`;<br>- `systemkit.abi`'s last line is `58 dock_layout_save`, so 59 / 60 is right and libgen confirms it at `make`;<br>- `kitdocs.py` lists SystemKit's headers explicitly (line 37–38), so adding `"systemkit/autostart.h"` is needed and is planned.<br>The insert rule fits the real `sdcard/etc/autostart` (`#setup: run agenda` then `keyb`…`preload /boot` last) and Setup's `autostart_finish` (`setup/system.h:229`: every `#setup: ` line is given back, plain `#` comments are kept, so the inserted comment line is harmless). The unit cases cover the position, no duplicate, `#setup:`, `preload` last, a plain-comment non-match, the size cap and `SIM_ROFS`. Keyconf and Setup moving onto the helper is a *Later* item only. |
| **2. Scope** | **Yes.** | 03 step 9 / G9 and 04 D12 / §1 / §3 / §4 / §10 keep only *Open in Text Editor (Ctrl+E)*. The following are *Later*, with an IDEAS.md line each in step 8: search + `HintBox` (with the own-header caveat), checklist boxes / tick, *Sort by*, drag out, *Export…*. No must AC names any of them; this was re-checked against 02's AC 1–33. The menus in 03 §3.3 and in 04 §3 are identical. `notes.png` without a search field is stated in both. |
| **3. Test tooling** | **Yes.** | 03 §3.6 is now written from the code.<br>- `ipc_lookup` (`fakekapi.cpp:1232`, `SIM_MBOX` → 7 at 1237).<br>- `ipc_register` (1231) and `ipc_register_note` (1319, installed over the first at 1387): `SIM_SERVICES` restricts the lookup even with `SIM_MBOX`, and registers any name as 1 in both functions.<br>- Notes treats a failed register as "go on alone".<br>- `SIM_ROFS=<prefix>` covers `save_file` (199), `f_mkdir` (692), `f_remove` (696), `f_rename` and `file_out` (1071), after `relpath ()`.<br>- The script step `copy SRC DST` runs in `step ()` (510) and makes AC 26 deterministic.<br>- Window flags are logged (AC 27).<br>Step 0 adds `sim_probe.cpp` to test each addition. The AC 23 / 25 / 26 / 27 and G4 test lines use them. |

## 2. Consistency 03 ↔ 04

- **Same:**
  - Show / Hide / pin behaviour (pin with `stickies = 0`: nothing launched; with `stickies = 1` and no service: `lx_launch` only).
  - The scope.
  - `autostart_has` / `autostart_ensure` and the comment line's text.
  - The menus and the keys.
  - The Trash for an emptied note: 03 §8.1 supersedes §3.3's `kapi_remove` sentence.
  - `ft_messagebox`.
  - The 5 s rescan.
  - The status texts for "written", "failed" and "hidden".
- **Different, settled here (binding):** the status text when the line **was already there**:
  - 04 D2 / §5: *"Stickies shown."*
  - 03 §3.7 / G7: the long text for both return values 1 and 2.

  **Follow 04.** The texts are the UX Designer's, and `autostart_ensure` already tells the cases apart:
  - 2 → *"Stickies shown, and started at every boot (SD:/etc/autostart)."*
  - 1 → *"Stickies shown."*
  - 0 → *"Stickies shown (autostart not written)."*

  All three end with a period, as keyconf's do.
- **Wording only:**
  - 04 D2 writes `autostart_ensure ("run stickies", "run agenda")` with the comment line described beside it, and calls `autostart_has` first. The signature is **03 §3.7's, three arguments** (`cmd, after, comment`).
  - The separate `autostart_has` call before it is redundant, because `ensure` returns 1 itself. Calling `ensure` alone is fine.

## 3. AC coverage and CLAUDE.md (re-check)

- 03 §7 still maps AC 1–33. AC 11 / 23 / 25 / 26 / 27 / 31 / 32 now name the new tooling and the helper. AC 29 / 33 stay with the user (no cross compiler).
- Kits first: the autostart helper is in SystemKit, `uk_text_wrap` / `uk_text_over` / `WKT_TRASH` / `WKT_PIN` are in UIKit, and the model stays beside the app.
- No kapi or kernel change.
- MIT on new files.
- `.abi` files are append-only.
- `kitdocs.py`, then `build_docs.py`. The docs/06 line for SystemKit's new subject is in the plan, as CLAUDE.md asks for a kit gaining a subject.
- docs/04 catalog.
- The package is declared and not published (AutoDev §0.5 over CLAUDE.md's publish rule). Nothing is pushed to `main`.

## 4. Notes for the developer (non-blocking)

1. **The status texts:** follow §2 above (04's three texts, keyed on `autostart_ensure`'s return value).
2. **`autostart.inc` must be valid C as well as C++.** `autostart.h` sits in `systemkit.h`'s C part, and on the PC the `.inc` is pulled into every includer, C programs included (`SK_BODIES_INLINE`). So: no `bool` without `<stdbool.h>`, no C++ casts or references, no `nullptr`.
3. **Name the `.inc`'s `static` helpers with an `as_` prefix.** `systemkit.cpp` compiles every `.inc` in one translation unit, so a generic name (`line_eq`, `read_all`) can clash with another subject's helper.
4. **`SIM_SERVICES` vs `SIM_IPC`:** `ipc_register` / `ipc_lookup` test `sim_ipc ()` first (lines 1231, 1234). Either check `SIM_SERVICES` before it, or document that the two are not combined. None of the planned cases sets both.
5. **03 §3.3 holds two superseded sentences**, both overridden by §8.1 / 04: the toolbar "split button or 6 dots, Copy Note", and the emptied note going to `kapi_remove`. Implement §8.1 / 04 §2.1 / D3 and ignore those sentences.
6. **`sdcard_lite/etc/autostart` also has `#setup: run agenda` (line 39).** It is regenerated by `mkrepo.py --lite` at publish time, which AutoDev does not run. Leave it, and mention in `06-development.md` that the lite card gets `#setup: run stickies` at the next publish. If it is copied from `sdcard/etc`, check that at publish time.
7. **Setup's `autostart_finish` reads at most 8 KB** (`setup/system.h:231`). The helper's 16 KB cap only means that the helper refuses later than Setup would cut. The real file is ~3 KB, so there is nothing to do.
