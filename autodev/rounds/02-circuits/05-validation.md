# AutoDev round 2 — Validation (Technical Analyst, fresh): Circuits

Date: 2026-10-06. Reviewed together: `02-product-analysis.md`, `03-technical-analysis.md` (with its appended *GUI
plan*), `04-ux-design.md` and `mockups/*.png` (main, fr-check looked at). Every claim below was checked against the
code on branch `AutoDev`, not only against the documents.

## Verdict: **NOT GREEN** — 2 gaps (one for role 3, one for role 4)

Both are small and local; once fixed, the plan is ready for the developer. Everything else checks out (§2).

---

## 1. Remaining gaps

### Gap 1 — owner: **3 Technical Analyst** — the PC builds of Circuits do not link FileKit's core

03 §1.2 has Circuits call `fk_load`, `fk_path_name`, `fk_path_ext`. On the PC those live **only in
`user/Kits/filekit/fkcore.cpp`** (which needs zlib). They are not inline (unlike `fsutil.h` and the planned
`kvtext.h`), and the PC builds the plan names do not compile that file:

- `tools/tests/desktop_sim/shots.sh`: the generic FT branch (line ~149: `$CXX … user/Apps/$1/main.cpp $extra
  libuikit.a libft.a $AK`) links no FileKit. Step 4 / G6 say "`circuits` in `APPS` and in the FT list,
  `extra=user/Apps/circuits/circuit.cpp` (as `notes`)". That fails to link as soon as `main.cpp` calls `fk_load`.
  Only `archiver` and `gpiolab` compile FileKit there, each in a branch of its own (`fkcore.cpp` plus the zlib
  objects, `-Ithird_party/zlib-1.3.1`; see gpiolab's block at lines ~133–140).
- `tools/tests/run_circuits_sim_test.sh` (step 7, "Notes' `run_notes_sim_test.sh` as the model") has the same
  problem. Notes' test links `main.cpp` + its model + UIKit/FreeType/fakekapi only.

**To fix (choose one and write it into 03):**
(a) Circuits calls no `fkcore` function. The window reads a pack with `fk_kv_load (path, FK_KV_PIPES)`, which is
inline on the PC, and `parse_pack` takes the `fk_kv *` (or the window gets the text through AppKit's
`kapi_open`/`kapi_fsize`/`kapi_read`). The file name comes from `fs_basename` (`filekit/fsutil.h`, inline on the
PC) and the `.circuits` test is done by hand. Then correct 03 §1.2 and step 11's `grep "kapi_"` allow-list. Or
(b) add an `if [ "$1" = circuits ]` branch to `shots.sh`, gpiolab's pattern: `circuit.cpp` (+ `board.cpp` if it
exists), `user/Kits/filekit/fkcore.cpp`, the zlib objects, `-Ithird_party/zlib-1.3.1`. Do the same in
`run_circuits_sim_test.sh`, and give the exact command lines in step 4 / step 7.

In both cases, also say that `board.cpp` (GUI plan, "if long") goes into the PC builds' source lists, not only
into `FT_EXTRA_circuits`.

### Gap 2 — owner: **4 UX Designer** — the palette does not fit at the minimum window width

D3 says the palette fits "down to the minimum window". Computed with UIKit's real `ToolBar`
(`toolbar.cpp`: `m_x` starts at 6, `add` gap 1, `sep ()` = 11 px, `addRight` from `w − 6` with gap 4):

- left: 6 + (1 + 48) Select + 11 sep + 6 × (1 + 48) gates = **360 px**;
- right: 3 × (4 + 34) Delete/Undo/Redo + 6 = **120 px** → the right group starts at `mw − 120`;
- at `setMinSize (920, 600)`: `mw = 920 − 10 − 238 − 10 − 234` = **428** → the right group starts at **308**:
  **it covers the last gate buttons by 52 px** on every level that allows the six gates (2.4–2.6, the whole of
  world 3). At the default 1000 px (`mw` 508, start 388) it fits, which is why the mock-ups look right.

**To fix:** pick one of these and update D3, §2.1, §4 and the GUI plan's "§5 window" row (min size), and G1's check:
- a minimum width of at least **~975** (`mw ≥ 480`; say 980 × 600);
- narrower palette buttons: at most 40 px wide, which gives 6 + 41 + 11 + 6 × 41 + 120 = 424 ≤ 428, a tight fit
  (check that *NON-OU* still fits in 10 px);
- or Delete / Undo / Redo somewhere other than the palette's row.

---

## 2. What was checked and holds

**Acceptance criteria → steps.** All 26 ACs of 02 §10 map to plan steps (03 §9 table; G0–G6 extend steps 2, 4, 5,
7 and 9). AC 23's Pi `make` / `make stage` is deferred to the user and says so (no `aarch64-none-elf-g++` here).
That is accepted. `make stage` copies every `user/*.elf` to `apps/<name>.app/main` by itself (`kernel/Makefile`
stage loop), so adding `circuits` to `FT_APPS` is enough.

**FileKit addition.**
- `filekit.abi`: last slot **77** (`fs_unique_name`), 78 entries. The 18 `fk_kv_*` functions of 03 §3.1 give
  **78…95**, a table of 96 entries, so `filekit >= 1.96` is right. `versions.ini` has `filekit = 1.78.3` and is
  left alone, as planned.
- libgen appends new names **sorted** (`libgen.py` l.170). The planned options (`--nm --name --abi --init
  --allow-data --export --table --stubs --bind`) all exist.
- The export regex `'^(fk_|fs_)'` is in `user/Makefile` (l.~261). `FK_OBJ` (l.244) and the `lib/fk/fsutil.o`
  rule (l.251) exist to copy.
- The inline-on-PC / library-on-Pi pattern mirrors `fsutil.h` (`FS_API`, `FS_IMPL` in `fsutil.cpp`).
- `filekit.h` includes `fsutil.h` only for C++ (l.201–204), so the plan to include `kvtext.h` for both languages
  is needed and is sound.
- I checked that `appkit/appkit.h` and `filekit/filekit.h` compile as C99 on the host (`gcc -std=c99
  -fsyntax-only`: clean). Step 1's C check is therefore realistic.
- `kitdocs.py` l.47–48 has FileKit's header list (`["filekit/filekit.h", "filekit/fsutil.h"]`); adding
  `filekit/kvtext.h` there regenerates `docs/14-FILEKIT.md`.
- `docs/06` §7 (FileKit) and §14 (Quick reference) exist. `python docs/build_docs.py` is in step 10.

**UIKit / FontKit / SystemKit / AppKit names.** Every one used exists with the stated signature:
- `ToolButton` (`iconSize`, `setIcon (ToolIconFn, id)`, `setText`, `setToggle`, `filled`, `raised`, `setOn`,
  `setDisabled`, an `onMouse` override, so `PaletteButton : ToolButton` needs no new virtual) and `ToolBar` (`bg`,
  `line`, `add`, `addRight`, `sep`);
- the glyphs `WKT_UNDO/REDO/TRASH/TO_START` and `WKG_CHECK/CLOSE/LEFT`;
- `VPath` (`poly`, `circle`, `hole`, `line`, `polyline`, `arc`, `fill`) and `uk_sin`/`uk_cos`;
- the paint helpers `uk_mix`, `uk_tone`, `uk_raised`, `uk_sunken`, `uk_hilite`, `uk_hilite_ink`, `uk_rbox`, and
  `uk_thumb` / `uk_draw_vscroll`, `uk_text_fit`, `UkFaceScope` (0 = no change, as 04 says);
- `Widget::catchOutside`, `tip`;
- `Root::onTick`, `onDrop`, `onResized`, `setResizable`, `setMinSize`, `fitWorkArea`;
- `Menu::item (label, shortcut, key, fn)`, `UK_CTRL` (^Z = 0x1A) and `TR`/`TRC`/`TRN`/`uk_lang`/`uk_lang_init`;
- `ft_uikit_install`, `ft_messagebox`, `ft_file_open`, and extra faces through `FtTextFace::open` (Turtle's
  `g_big`);
- `clip_set_text` / `clip_get_text`;
- `KEY_F1` 0x110, `KEY_DEL` 0x108, `KEY_BACKSPACE` 8, `kapi_opendir/readdir/closedir/get_args/open/fsize/read/
  save_file`.

The mouse routing in `widget.cpp` (`handleMouse`: the widget under the pointer gets the events, held button
included, and `catchOutside` widgets get them everywhere) supports the press-on-palette / release-on-board drag
(R5, D9). No UIKit change is needed. Confirmed.

**Simulator mechanisms.**
- `fakekapi.cpp` has `SIM_ARGS`, `SIM_WRITES`, `SIM_OVERLAY` (files), `SIM_CLIPFILE` (l.1294), `SIM_POS`,
  `SIM_SCREEN`, `SIM_LOG`, and the script verbs `down/up/move/rdown/rup/key <hex>/menu/drop/wait/dump/quit/exit`.
- Time: 60 `wait`s ≈ 1.2 s of its clock (Notes' `PAUSE`), so the autosave cases are testable.
- `shot.py`, the `lang` helper, `want`, the `desktop_sim/turtle/*.ini` fixture pattern and
  `run_notes_sim_test.sh` all exist.
- `git diff --stat origin/main -- kernel/include/kern/kapi_abi.h kernel/sys user/Kits/appkit user/Apps/turtle` is
  empty today, so the baseline for AC 24 is clean.

**Card, package, docs.**
- `sdcard/etc/fileassoc.ini` l.104 `turtle = turtle`.
- `[app.turtle]` in `packages.ini` (`needs = uikit >= 1.779, systemkit >= 1.74`); `versions.ini` agrees.
- `mkrepo.py` takes `--out --no-sign --versions --bump`.
- Nothing is published: step 8 is declared only, as PIPELINE §0.5 says.
- `tools/lang/check.py` (`--keys`, `<app>`) scans `user/Apps/<app>/**/*.{cpp,h,inc}`, so the split files
  (`gates.h`, `views.h`, `board.h`) are seen.
- `tools/icons/*_icon.py` (Pillow) is the pattern for the icon.
- docs/03 l.~2203 and l.~3931 and docs/04 l.1657 (Language & Region) are where the plan says.
- MIT notices: required for every new source, and checked by step 11's `grep -L`.

**The missions' logic.**
- Every truth table in 02 §6 checks out by hand: 1.1–1.8, 2.1–2.6 (2.6's S A B order gives `0 0 1 1 0 1 0 1`),
  3.1–3.6, and 3.4's example 1011 → 101.
- Every reference construction computes its function: 2.2, 2.5, 2.6 (`A XOR (S AND (A XOR B))`), 3.3, 3.5
  (`Y1 = B XOR AB`, `Y2 = A XOR AB`) and 3.6.
- I **re-ran an independent exhaustive search** (a separate Python search over sets of signals, with a gate's two
  inputs allowed to be the same signal). It confirms Min = 3 (1.8, 2.1, 2.2), 4 (2.3), 2 (2.4, 3.1, 3.2), 4 (2.5),
  3 (2.6), 4 (3.5, 3.6). 3.3's 5 is the known full-adder minimum, and 3.4's 7 is a reference only (R8).
- AC 3's list of gate counts equals the Min column. The full adder's depth 3 (AC 10) is right.
- The fixed-part rows `y = (2k+1)·30/(2n) − 1` give 14 / 6, 21 / 4, 14, 24 / 2, 10, 17, 25, as G0 says.
- At 1000 × 620 the board's cell is 12 px; at 920 × 600 it is 10 px. The 16-row table of 3.4 (390 px) and the
  bench fit at 600 px high, with the mode line hidden as 04 §2.3 says.

**02 ↔ 03 ↔ 04 consistency.** Each change the UX made is written into the GUI plan's "What this changes" table:
- the bench on the right (the objective and Check moved);
- the result card;
- 40 × 30 cells;
- 920 × 600;
- 1–6 / arrows / picking up a wire / clicking a row;
- French gate names;
- `pinAt` / `wireAt` / `canConnect` / `fits`.

The menus and the F-keys agree across the three documents.

---

## 3. Notes for the developer (settle alone; they do not block)

1. **2.2's stars.** 02 §6.2's note says the textbook `(A AND NOT B) OR (NOT A AND B)` (5 gates) is "1 star only".
   With `par = 3 5` it earns **2 stars**, which is also 02 §6's own rule ("the textbook answer earns two stars").
   Keep the table (3 / 5). Do not repeat "1 star" in the hint, the lesson or docs/04.
2. **`nandxor`** is in neither of 04 §9.1's lists (gate / idea). Treat it as an *idea* (no gate of its own), with
   `Lesson::gate = −1`. "The level's own smallest example" for an idea card means the level's goal table.
3. **Gate naming** on placing is not specified. Use the lowest free `g<n>`, so that the history and circuit text
   stay deterministic (G2's test expects `part g1 AND x y`).
4. **`fk_kv_set` with section `""`** must put the entry before the first `[header]`. That is the top keys `pack`,
   `level` and `open` of `progress.ini`. Pin it in `kvtest`.
5. In an FT app, use `ft_file_open` (not bare `uk_file_open`) so the dialog uses the FreeType face (04 §9.4 says
   so; 03 §5 does not).
6. `SD:/docs/circuits` (the pack chooser's start folder) does not exist on the card. Check `ft_file_open`'s
   fallback, or create the folder at the first Ctrl+O.
7. Step 11's `grep "kapi_"` allow-list is too narrow if the autosave reads a clock (`kapi_get_ticks`). Widen it to
   what is used through AppKit.
8. `Label`'s constructor is `(l, t, w, h, s, fg, bg)`; 04 §4 writes it without `l, t`.
9. `VPath` has no dash style. Draw the dashed `WIRE_X` and the rubber wire as short segments.
10. **Testing at 920 × 600.** `fitWorkArea` only shrinks the window, so use `SIM_SCREEN` (or an env/argument size
    for tests) rather than a "resize step", which fakekapi does not have.
11. Add sim cases for the UX additions too: the digits 1–6, an arrow-key move, picking up a fed wire, and a click
    on a truth-table row.
12. AC 17 asks for "palette tooltips" in `circuits-fr.png`. A still image shows a tooltip only after a hover pause
    (`move` + `wait`s). Show one, or say in 06 that it was not shot.
13. **Docs.**
    - docs/03 §5.8 (FileKit) should mention `filekit/kvtext.h` beside `fsutil.h` (l.~1087/1238).
    - docs/04's Turtle Quest section sits in §13 *Programming in BASIC*. Place the *Circuits* section beside it
      (or under §12), and do not present it as BASIC.
14. **`History`** holds 65 × up to 8 KB. Keep it global or on the heap, not on the stack.
15. Run `kitdocs.py` after the header is final. Then `git diff --stat docs/` should show only `docs/14`, plus the
    docs edited on purpose.
