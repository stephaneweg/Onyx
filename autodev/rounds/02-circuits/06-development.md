# AutoDev round 2 — Development: Circuits

## Developer A (steps 0–3, G0)

Date: 2026-10-06, branch `AutoDev`. Plan: `03-technical-analysis.md` §7 steps 0–3 and the GUI plan's G0, with the
notes of `05-validation.md` §3.

**There is no AArch64 compiler in this container** (`aarch64-none-elf-g++` absent). Everything below was built and
tested with the host `g++` / `gcc` only. The Pi build (`make` from `kernel/`) has not been run. It is the one
place where libgen confirms the `filekit.abi` slots and where `lib/fk/kvtext.o` is compiled for real.

### Commits

| Commit | Step |
|---|---|
| `b194ed9f` | step 1: FileKit `fk_kv_*` (`kvtext.h/.inc/.cpp`), `filekit.h`, `user/Makefile`, `filekit.abi` 78–95, the unit test, `kitdocs.py`, docs/14 regenerated, docs/06 §7 + §14, docs/03 §5.6 + §5.8 |
| `e1c2524d` | step 2 + G0: `user/Apps/circuits/circuit.h` / `circuit.cpp` (engine core + geometry), `tools/tests/circuits/circuitstest.cpp` part A, `tools/tests/run_circuits_test.sh` |
| `f5a26164` | step 3: packs, progress and unlocking in the engine; `lessons.h`; the three packs in `sdcard/apps/circuits.app/levels/`; test part B |
| `f8846c98` | step 1 follow-up: docs/14 regenerated again (its *Using it* section is taken from docs/06 §7, which had just changed) |

### Step 0 — baseline

- `sh tools/tests/run_turtle_test.sh` → `ok   turtle (27 levels: solved, written back; the errors)` (40 s).
- `python3 tools/docgen/kitdocs.py && git status --short docs/` → no diff.
- A copy of `filekit.abi` (78 entries, last slot 77 `fs_unique_name`) was kept in the scratchpad for libgen.

### Step 1 — FileKit `fk_kv_*`

Files:
- `user/Kits/filekit/kvtext.h`: the API of 03 §3.1, all 18 functions, C and C++, `FK_KV_API` macro as planned.
- `kvtext.inc`: valid C99 and C++, `static inline` helpers `fkkv_*_`, `malloc`/`realloc`/`free`.
  - It loads through `kapi_open`/`kapi_fsize`/`kapi_read` and saves through `kapi_save_file`.
  - It has no `<string.h>` dependency: only `<stdlib.h>`.
- `kvtext.cpp` (`FK_KV_IMPL`).
- `filekit.h` includes `kvtext.h` after its `extern "C"` block, for both languages, and has a line in its summary.
- `user/Makefile`: `FK_OBJ += lib/fk/kvtext.o` and its rule (a copy of `fsutil.o`'s).
- `filekit.abi`: slots **78–95** appended (`fk_kv_block` … `fk_kv_value`, sorted, as libgen does). See "Commands" for how they were made.

Semantics as fixed in 03 §3.1, plus:
- A UTF-8 BOM is skipped.
- `;` lines are comments, as `#` lines are.
- `fk_kv_get`/`fk_kv_set` take section `""` or `0` for "before the first header". `set ""` inserts before the first `[header]` (validation note 4).
- `fk_kv_set` with an empty key returns `-1`.
- The value written depends on the flags:
  - with **both** flags, escapes win: one line per value;
  - with **neither** flag, a value's new lines are written as spaces, so the file stays valid.
- `fk_kv_save` of an empty document writes an empty file. `fk_kv_load` of an empty file gives an **empty
  document, not 0**. 0 means only "no such file / unreadable / no memory". That is how I read 03's "empty / missing file".

Test: `tools/tests/filekit/kvtest.cpp` + `tools/tests/run_kvtext_test.sh`. It covers:
- parsing; repeated `[level]` blocks and `fk_kv_block`;
- `fk_kv_line` of keys and of `|` values (value line j at `line + j`, an empty first `|` line);
- `|` lines without a key; blank and comment lines ending a block;
- `\r\n`, the BOM, `=` inside a value;
- escape round-trip (`a\nb\\c`, an unknown escape kept);
- `set`/`remove` order rules; unknown keys kept on write-back;
- `fk_kv_text` → `fk_kv_parse` with identical entries, and the same text twice;
- the comment line; empty and null documents;
- save/load through `fakekapi.cpp` with `SIM_WRITES`, a missing file giving 0, an empty file;
- a real card file, Turtle Quest's `1-first-steps.turtle` (its `map` value and line).

The script also:
- compiles a C99 file that includes `filekit/filekit.h` and calls `fk_kv_*` (`-Wall -Wextra -Werror`);
- builds `kvtext.cpp` as the library object and checks that it exports exactly 18 `fk_|fs_` symbols.

Docs:
- `tools/docgen/kitdocs.py`: `"filekit/kvtext.h"` added to FileKit's list, `FK_KV_API` added to its prefix regex (otherwise the macro would show in the signatures), and FileKit's one-line description extended. Then run: `docs/14-FILEKIT.md` gains the `kvtext.h` section, 18 index rows, "96 entries", and the docs/06 example.
- `docs/06-KITS-GUIDE.md`: §7 has an example (a progress file, and a word on packs); §14 has the row "read and write a settings / progress file with sections → FileKit → `fk_kv_load`, `fk_kv_set`, `fk_kv_save`".
- `docs/03-DEVELOPER-GUIDE.md`: §5.8's table has a *Key / value text documents* row (validation note 13). §5.6's "how a kit of this kind is made" names `kvtext.h` (C) and `-DFK_KV_INLINE`.
- `python docs/build_docs.py` was **not** run. It belongs to step 10, after Developer B's docs/04 edits, so the exports are regenerated once.

### Step 2 + G0 — the engine core and its geometry

`user/Apps/circuits/circuit.h` / `circuit.cpp`, `namespace circuits`:
- No UI and no file I/O.
- No STL and no exceptions. It builds with `-fno-exceptions -fno-rtti -Wall -Wextra` (checked on the host).
- It uses `snprintf`, `malloc` and `strcmp` (newlib on the Pi).

What it has:
- `PartType`, `Err` (+ `err_name` for logs), `Lv`.
- `Level`: the `concept` key is the field `topic`, because `concept` is a C++20 keyword and `-Wextra` warns.
- `Part`, `Circuit`. `Circuit`'s members:
  - `setup`, `find`, `pins`, `fixed`, `gates`, `wires`, `upstream`;
  - `placeErr`/`fits`, `add` (lowest free `g<n>`: validation note 3), `move`, `canConnect`, `connect`, `disconnect`, `remove`, `clear`;
  - `box`, `pinIn`, `pinOut`, `at`, `route`, `junctions`, `pinAt`, `wireAt`.
- `Eval`, `evaluate`, `evaluate_to`.
- `CheckResult`, `check`, `stars_for`.
- `write_text`, `read_text`, `truth_table_text`.
- `History`: up to 65 malloc'd snapshots, each as long as its own text, so no 8 KB × 65 block.

Geometry is exactly the GUI plan's G0 row:
- 40 × 30 cells; switches at x 0 (5 × 2), lamps at x 34 (6 × 2), gates 5 × 4 in columns 6–33.
- Pins as in 04 §5.2; fixed rows by `fixed_row`.
- Forward routes have 4 points. The vertical takes the middle column, then +1, −1, +2, −2… The first free column wins. Free means no vertical of another source overlaps it and no footprint is crossed.
- Backward routes have 6 points, under both parts, or above them if that would leave the grid.
- Routes are computed in the canonical wire order: a wire knows the verticals of the wires before it.
- `junctions` puts a dot where a wire's corner lies inside another wire of the same source, or where two such wires split from a shared corner.
- `pinAt` uses a radius of 0.6 cell; `wireAt` uses less than 1/3 cell (in 1/16 cell) and returns the fed part, with `*pin`.

Test part A (`tools/tests/circuits/circuitstest.cpp`), with levels built inline:
- AC 4–11, 15 and 16 as listed in step 2.
- G0's cases:
  - the fixed rows for n = 1…4; the pins of every type;
  - x = 3 and x = 30 give `E_OUTSIDE`; overlap gives `E_OVERLAP`; touching gates are allowed;
  - a forward route takes the middle column, moves one column for another source, and keeps the column for the same source; a footprint is avoided;
  - a backward route has 6 points;
  - `pinAt`/`wireAt` at known points and just outside;
  - `canConnect` returns `E_LOOP` for a loop, `E_PIN` for a lamp as source, and accepts a fed input.

### Step 3 — packs, progress, lessons, the 20 missions

Engine (still `circuit.h/.cpp`, as 03 §2 says):
- `Pack` holds fixed `Level *lv[64]`.
- `parse_pack_kv (Pack &, const fk_kv *, why, cap)`, plus `parse_pack (text)` for the host test.
- `ids_unique (Pack *const *, n, why, cap)`.
- `Progress` over an `fk_kv` with `FK_KV_ESCAPES`. It has `attach`, `stars`, `record` (returns true for a new record and never lowers), `circuit`/`setCircuit`, `seen`/`setSeen`, `isOpen`/`open`, `lastPack`/`lastLevel`/`setLast`. Section `[Player]`; top keys `pack`, `level`, `open`.
- `level_open`, `open_first` (a fresh start), `unlock_after` (returns the opened id).

Pack errors, each with its line:
- a missing required key, reported at the `[level]` header line;
- an id that is not `[a-z0-9-]{1,23}`, or an id used twice;
- 1–4 inputs/outputs, each `[A-Z][A-Za-z0-9]{0,3}`, and a name used twice;
- unknown parts;
- `par` that is not two numbers with 3★ ≤ 2★;
- a table header that does not equal the inputs/outputs;
- a row with the wrong number of bits, a value other than 0/1, rows out of counting order, too many rows (reported at the first extra row's line), too few (reported at the table's line);
- "no [level] in the pack".

Unknown keys are ignored. Messages read like *"line 14: the table has 3 rows, 4 expected"*.

`user/Apps/circuits/lessons.h`:
- 18 `Lesson { key, gate, title[2], text[2] }`, with `find_lesson`.
- The gate cards are not, and, or, nand, nor, xor. The other 12 are ideas with `gate = -1`, `nandxor` included (validation note 2).
- The FR texts use D13's names (NON, ET, OU, OUX, NON-ET, NON-OU).

The packs `sdcard/apps/circuits.app/levels/1-gates.circuits` (8), `2-combining.circuits` (6) and
`3-arithmetic.circuits` (6) follow 02 §6:
- ids, EN/FR titles, texts and hints, concepts (none on `nandor` and `add2`), inputs/outputs in the given order, parts, `par` = Min / ★★, tables in counting order.
- Each `solution` is the canonical circuit text with positions, laid out by depth in columns 7/15/23-ish. Each output gate's out pin is placed on its lamp's row where possible.
- 2.2's hint does not say "1 star" (validation note 1).
- The packs were generated once by a throwaway script, so the tables are computed. The committed `.circuits` files are the data. The C++ test checks them independently.

Test part B, run on the card's packs:
- AC 1: 3 packs, 8 + 6 + 6, ids unique, the order of §6, the pack titles.
- AC 2: each table equals a per-level C++ lambda on every row; parts and par are as 02.
- AC 3: each solution parses, uses only allowed parts, wins every row, gets 3 stars, has gates == Min (0,1,1,1,2,2,2,3 / 3,3,4,2,4,3 / 2,2,5,7,4,4), and is already canonical (`write_text` gives it back).
- G0: each solution read **without positions** is auto-placed inside the columns, without overlap, and still wins.
- AC 12: 14 malformed variants of a pack, each with its exact message and line. An unknown key loads. An id used twice is caught in one pack and across packs. A refused parse leaves the pack unchanged.
- AC 13: progress round trip through `fk_kv_text`/`fk_kv_parse`: stars, a multi-line escaped circuit, a backslash, `seen.*`, `open`, `pack`/`level`, an unknown key kept. A fresh start opens only `wire`.
- AC 14: wire opens not; the last of pack 1 opens `nandor`; the last of pack 2 opens `parity`; the very last opens nothing. Stars never go down. A pack opened from a file is all open and is not on the path.
- AC 22 (b): every level has `title.fr`, `text.fr`, `hint`, `hint.fr`. All 18 concepts are used exactly once, and each has an EN + FR lesson with the right gate.

### Commands and results (run from `/home/user/onyx`, last run after the final commit)

```
sh tools/tests/run_kvtext_test.sh
  ok   kvtext, ASan build, no files (101 checks)
  ok   kvtext (115 checks)
sh tools/tests/run_circuits_test.sh            # = ... sdcard/apps/circuits.app/levels/*.circuits
  ok   circuits (582 checks: the engine; 3 packs, 20 levels solved with three stars)
sh tools/tests/run_turtle_test.sh              # AC 26 baseline, unchanged
  ok   turtle (27 levels: solved, written back; the errors)
sh tools/tests/run_archiver_test.sh            # regression: fkcore.cpp now includes kvtext.h through filekit.h
  0 failure(s)
python3 tools/docgen/kitdocs.py && git status --short   # clean after f8846c98
git diff --stat origin/main -- kernel/include/kern/kapi_abi.h kernel/sys user/Kits/appkit user/Apps/turtle   # empty
grep -L "MIT License" user/Apps/circuits/* user/Kits/filekit/kvtext.* tools/tests/circuits/* tools/tests/filekit/* \
     tools/tests/run_kvtext_test.sh tools/tests/run_circuits_test.sh                                      # nothing
```

`run_circuits_test.sh` builds with `-Wall -Wextra -Werror -fsanitize=address,undefined`, so a warning in the engine
fails the run. `.abi` lines: `kvtext.cpp`, `fsutil.cpp` and `fkcore.cpp` were compiled for the host
(`-DONYX_LIB_BUILD`; `fkcore` with `-Ithird_party/zlib-1.3.1`). `fsutil.cpp` was compiled from a shim copy whose `fsutil.h` has
`#if 1` in place of the `__aarch64__` test, because its `FS_API` is `static inline` on the host (round 1's method).
Then:

```
python3 tools/libgen/libgen.py --nm nm --name filekit --abi <copy of filekit.abi> --init fk_lib_init \
    --allow-data . --export '^(fk_|fs_)' --table t.S --stubs s.S --bind b.cpp kvtext.o fsutil.o fkcore.o
  libgen: filekit: 18 new entries appended to filekit.abi (version 96 now)
```

These lines were copied into `user/Kits/filekit/filekit.abi`. The user's `make` on the Pi confirms them.

### Deviations from the plan

1. **ASan and `fakekapi.cpp` do not go together.** fakekapi `mmap`s the kapi table `MAP_FIXED` at `KAPI_TABLE_VA`, inside ASan's shadow, and crashes at start. `run_kvtext_test.sh` therefore runs two builds:
   - ASan + UBSan on everything except the files (`-DKV_NO_FILES`);
   - UBSan + fakekapi on everything, files included.

   Developer B: the same holds for any sim test. Notes' does not use ASan either.
2. **48 gates do not fit on a 40 × 30 board.** Gates are 5 × 4 in columns 6–33, so at most 5 × 7 = **35** gates fit side by side. `E_FULL` (the 49th gate) cannot be reached by placing. The test shows the 35-gate limit (`E_OVERLAP`/`E_OUTSIDE` after it) and checks `E_FULL` on a board filled by hand. The UX's *"The board is full: 48 gates at most."* message will in practice never show. AC 9's "board refuses a 49th gate" holds in the engine. Worth a line in docs/04 and the report: the limit is room, 35.
3. **Forward route when the target pin is only 1 column right of the source** (a switch at x 5 into a gate at x 6, which is legal). There is no column strictly between, so the vertical goes on the **target's pin column**: still 4 points, not the 6-point backward route. "Backward" is `tx <= sx`.
4. **A wire into the same part** gives `E_LOOP`, as AC 7 requires. `E_SAME` stays in the enum but is never produced.
5. `Level::concept` is named **`topic`**: the pack key stays `concept`. `concept` is a C++20 keyword, and `-Wextra` with `-Werror` refuses it.
6. **Circuit text has no final new line.** A one-line circuit is stored as `wire.circuit = wire A Out`, as AC 21 expects. Canonical order: gates (in creation order), then the wires into the gates (pin 1 then 2, by gate order), then the wires into the lamps. This matches 02's examples.
7. `read_text` auto-placement:
   - column `7 + 6 (d − 1)`;
   - rows `(2k + 1)·30 / (2m) − 2` for the k-th of m gates of depth d, centred like the fixed parts, `−2` because a gate is 4 high;
   - when that place is taken or outside (depth > 4, more than 7 in a column), the first free place, scanned column by column.
8. The `.circuits` packs carry two `#` comment lines (format pointer + MIT line) above `[pack]`.
9. `Pack` uses a fixed `Level *lv[64]`, not Turtle's `Arr`. `Progress::player` is fixed at `"Player"` (several players is SHOULD).

### Hints for Developer B (the window, steps 4–11)

- **Build lists.** Circuits' extra sources are only `user/Apps/circuits/circuit.cpp`; `lessons.h` is header-only. So:
  - `FT_EXTRA_circuits = Apps/circuits/circuit.cpp`;
  - `shots.sh`: `extra="user/Apps/circuits/circuit.cpp"`.

  `user/Makefile` is **not** yet touched for the app: `FT_APPS += circuits` and the dependency line (step 4) are yours, because `main.cpp` does not exist yet.
- **Includes.** `circuit.h` includes `"filekit/filekit.h"` (for `fk_kv`), so `main.cpp` gets `fk_kv_*` and `fs_*` from it.
  - Do **not** call `fk_load`/`fk_path_*` (fkcore: the PC builds would not link).
  - Include `lessons.h` for the cards.
- **Loading.**
  - Packs: `fk_kv *d = fk_kv_load (path, FK_KV_PIPES); parse_pack_kv (*pk, d, why, sizeof why); fk_kv_free (d);`. A 0 from `fk_kv_load` means "cannot be read".
  - Then `ids_unique` over the loaded packs plus the new one, for 04 §9.4's clash dialog. The message is *a level "x" is already loaded*; compose the dialog text around it.
  - Built-in packs keep `opened = false`; a pack from Ctrl+O, a drop or the argument gets `opened = true`.
- **Progress.**
  - Load: `g_pr.attach (fk_kv_load (PROGRESS, FK_KV_ESCAPES)); if nothing is open: open_first (g_pr, packs, n);`. Use `isOpen` on the first level to tell.
  - Save: `fk_kv_save (g_pr.kv, PROGRESS, "# Circuits -- the player's progress (written by the game)")` → 0 / −1 for 04's *"Progress not saved"*.
  - `level_open (pr, pack, l)` answers the list's padlocks.
  - On a win: `record` (true = *New record!*), then `unlock_after (pr, packs, n, p, l)` (returns the next id, or 0 on the last level or a file pack).
  - `setCircuit (id, text)` + `setLast (fs_basename (pack->path), id)` at each autosave.
- **Board ↔ engine.**
  - Keep one `Circuit` per shown level: `setup (L)`, then `read_text (c, L, pr.circuit (id))` when non-empty. On failure, fall back to `setup` and say nothing.
  - `History` must live on the heap or as a global (validation note 14). Call `start (c)` on each level shown and `push (c)` after every change (an unchanged board pushes nothing).
  - Undo/redo: `h.undo (c, L)` / `h.redo (c, L)`.
  - Errors map from `Err` to the UI words: `E_OVERLAP`/`E_OUTSIDE` → *No room there* / *Gates go between…*, `E_FULL`, `E_LOOP` → 02 §4.6's message, `E_LAMP_OPEN`/`E_GATE_OPEN` with `CheckResult::errPart` (its `p[i].name`, its type via `type_name`).
- **Evaluation.**
  - Live: `evaluate (c, row, e)`, where `row` is the switches as a number, the first input the MSB (`bit ni−1−k` = switch k). So a table-row click is `row = r` directly, and Check's `firstWrong` is the row to set.
  - Step mode: `evaluate_to (c, row, step, e)`, step 0…`e.maxDepth`. `e.v[i] == LX` means grey, `e.depth[i]` is the badge number, and `e.det[i] == false` means downstream of an open input (live).
  - Lamps are `e.v[c.ni + o]`.
- **Hit tests.**
  - Convert pixels to 1/16 cell yourself: `gx16 = (mx − ox) * 16 / cell`.
  - Order: `pinAt` (pins), then `c.at (cx, cy)` (parts: a switch's key toggles it, a gate selects), then `wireAt` (returns the fed part + `*pin`; `disconnect (dst, pin)` removes it).
  - `pinAt` returns the nearest pin. Gates may touch, so an output and an input can sit 1 cell apart but never on the same point.
  - Apply 04's 7-px minimum in the window.
- **Drawing.**
  - `route (dst, pin, xy, 12)` gives a wire's points (it recomputes every route; for drawing all wires once per frame, calling it per wire is fine: ≤ 100 wires × 100 = cheap).
  - `junctions (xy, cap)` gives the dots.
  - The rubber wire: `canConnect (src, dst, pin, &e)` for the ring colour.
  - The ghost: `placeErr (type, x, y)` (`ignore` = the gate being moved) or `fits`.
- **Copy.** Copy Truth Table: `truth_table_text (L, checked ? &lastResult : 0, buf, cap)`. Copy Circuit: `write_text`.
- **The 35-gate limit** (deviation 2): docs/04 should say "as many gates as fit on the board (35)", or the 48 message can stay as a safeguard. A UX/PM call; nothing in the engine changes.
- **Lessons.** `find_lesson (L.topic)` (0 for `nandor` and `add2`, which have no concept).
  - `gate >= 0`: draw that gate and its 2-input table (NOT 1-input).
  - `gate == -1`: the level's own goal table (validation note 2).
  - `title[lang]` goes after *"New gate: "* / *"New idea: "* (`TR`).
- **Fixtures for G6.** `solving.ini` on `full` can reuse the shipped solution text (positions: g1 XOR 8 12, g2 XOR 16 20, g3 AND 8 2, g4 AND 16 11, g5 OR 24 5). `check.ini` on `xor`: `part g1 OR 10 4` + 3 wires, which fails exactly on row `1 1` (asserted by the engine test).
- **Not done here** (later steps): `FT_APPS`, `app.txt`, `icon.bmp`, `lang/fr.txt`, `fileassoc.ini`, `packages.ini`, docs/04, HANDOFF, `build_docs.py`.

## Developer B (steps 4–7, G1–G5)

Date: 2026-10-06, branch `AutoDev`. Plan: `03-technical-analysis.md` steps 4–7 as refined by the GUI plan's G1–G5,
`04-ux-design.md`, the mock-ups, the notes of `05-validation.md` §3 and Developer A's hints above. Host only (no
AArch64 compiler): the window is built and tested in the desktop simulator; `make` for the Pi is the user's.

### Commits

| Commit | Step |
|---|---|
| `09da4a43` | step 4 + G1: `user/Apps/circuits/main.cpp`, `gates.h`, `board.h`, `views.h`; `user/Makefile` (`FT_APPS += circuits`, `FT_EXTRA_circuits = Apps/circuits/circuit.cpp` + its dependency line); `shots.sh` (FT list, `APPS`, `extra`) |
| `b6ac07a2` | step 5 + G2–G4: the interactions, the game logic; the fixtures `tools/tests/desktop_sim/circuits/{solving,check,stars,and}.ini`, `tools/tests/desktop_sim/sd/docs/circuits/{extra,bad}.circuits` |
| `9f178483` | step 6 + G5: `sdcard/apps/circuits.app/lang/fr.txt` |
| `94a1ca51` | step 7: `tools/tests/run_circuits_sim_test.sh` |

### Step 4 / G1 — the window

- One translation unit, as Turtle Quest: `main.cpp` holds the game's state, then includes `gates.h` (colours, faces
  18/10/30/9, `gate_outline` with **integer** quadratic curves and `uk_sin/uk_cos`, `draw_gate_shape`, star, padlock,
  wrapped text, `circuits_icon`), `board.h` (`Board`, `PaletteButton`) and `views.h` (`LevelList`, `Card`, `MsgBar`,
  `TruthTable`, `CountView`, `LessonCard`, `ResultCard`). No `board.cpp`: `FT_EXTRA_circuits` stays `circuit.cpp`.
- Layout of 04 §2.1 in `layout ()` (`PAD 10`, list 214, bench 238, palette 46, message 50); `Root (1000, 620)`,
  `setMinSize (920, 600)`, `fitWorkArea`. The palette is built once (Select, sep, the six gates, the *no gate* label,
  Delete/Undo/Redo at the right); per level the gates not allowed are hidden and the others re-placed — no widget is
  deleted (Root keeps pointers to hovered widgets).
- The card wraps the level's text on up to 4 lines (French at 920 px needs 4; 04 said 3).

### Step 5 / G2–G4 — interactive

All of G2–G4: arm on press (`PaletteButton::onMouse`), the ghost, click-click and drag from the palette (placed on
the release seen by the board), back to Select after one; wiring with the rubber route and the green/red ring
(`canConnect`), the loop message; picking a fed input's wire up (re-plug / drop = remove, one undo step); select,
move by drag (ghost, refused → back + message), arrows, Del/Backspace, trash, right click; undo/redo
(`History` on the heap) with the buttons' disabled states; keys 1–6; Esc's order (card › drag › armed › selection);
`catchOutside` while a board drag is held. Live switches, table-row clicks; step mode (F8 enters at step 0, each F8
one depth more; F9, F7; any edit leaves it; a switch / row in step mode → step 0), badges, grey dashed wires, `?`
lamps, the mode line (hidden when the 16-row table needs the room). Check: refusals with the red outline / ring, the
obtained columns and marks, first wrong row set on the switches, the message (the first wrong output, "must light /
stay off"), won → stars recorded (never lowered), `unlock_after`, saved, result card (*New record!*, last level),
message bar stars + *Next level* (shown only for an OK message with a next level open). Levels: locked row → message
only; lesson card first time (`seen.*`) and F1, Enter/Esc; F2 hint (card + message bar); Ctrl+N/P; Ctrl+O
(`ft_file_open` in `SD:/docs/circuits`, made by `kapi_mkdir` first — validation note 6), drop, argument; malformed /
clashing pack → `ft_messagebox` with the engine's line; About; *Copied* messages. Autosave 1 s after the last change
(`onTick`, `kapi_get_ticks`), on level change and on quit; a failed save says so once.

### Step 6 / G5 — French

`sdcard/apps/circuits.app/lang/fr.txt`, the words of 04 §13/D13 (NON, ET, OU, OUX, NON-ET, NON-OU on the palette, the
board and the lessons; the loop message exactly as 02 §4.6). `python3 tools/lang/check.py circuits` →
**`circuits [fr]: 108 words, 0 missing, 0 not used`** (exit 0). The levels' texts and the lesson cards come from the
packs' `.fr` keys / `lessons.h` (Developer A). Looked at in French (the system's `language=fr` in the writes, the
mechanism `SHOTS_LANG=fr` uses) at 1000 × 620 and 920 × 600: menus, palette (*NON-OU* fits in 40 px with the 9-px
face), bench (*Pas à pas*, *Au départ*, *Vérifier*), the step line, the table heads, the message bar — nothing
truncated but the level titles, fitted with "…" as 04 planned. Left in English: the engine's pack-error detail inside
the French dialog ("line 20: the table has 1 rows, 2 expected") — the engine is language-free (03 §5).

### Step 7 — the scripted simulator test

`tools/tests/run_circuits_sim_test.sh` (Notes' model; `build` as its argument only builds): UIKit/FreeType/fakekapi
cached in `$TMPDIR/onyx_circuits_sim`, every `user/Apps/circuits/*.cpp` built with `-Wall -Wextra` (a warning in
`Apps/circuits/` fails it), no FileKit core, no zlib, no ASan (fakekapi, see A's deviation 1). Each case has its own
`SIM_WRITES`; the board's top is probed from a dump (the card's height varies), the grid points computed as the board
does; assertions on `progress.ini`, `SIM_CLIPFILE` and dump pixels (numpy). 61 checks: AC 16 (both copies), 18
(lesson card, Enter, `open = wire`, padlocked rows, a fixture's 3/2/1/0 stars), 19 (lamp and wire dark → lit after
two switch clicks; a table-row click), 20 (argument, its second level open via Ctrl+N, a malformed pack refused, a
drop), 21 (win: `wire = 3`, `wire.circuit = wire A Out`, `open` holds `not`, the result card, Enter → next), G2
(drag from the palette + 3 wires, Ctrl+Z, Ctrl+Y, keys 2 then 1, arrows, body drag, a wire picked up and re-plugged /
dropped, Esc, right click + undo, wire click + Del, Clear Board, a loop refused with the red message and the board
unchanged, save at quit), G3 (check.ini: row 1 1 tinted, both switches green, red message, nothing recorded;
solving.ini F8 twice: the OR's face `F0F2F4`, a depth-1 AND computed, F7 back), G4 (locked row, open row, F1/Esc,
F2), French and 920 × 600 runs (`SIM_SCREEN=928x746`).

### Commands and results (from `/home/user/onyx`, after the last commit)

```
sh tools/tests/run_circuits_sim_test.sh      ->  circuits-sim: all 61 checks passed     (~25 s once UIKit/FreeType are built)
python3 tools/lang/check.py circuits         ->  circuits [fr]: 108 words, 0 missing, 0 not used
sh tools/tests/run_circuits_test.sh          ->  ok   circuits (582 checks: ...)          (engine untouched)
grep -L "MIT License" user/Apps/circuits/* tools/tests/run_circuits_sim_test.sh tools/tests/desktop_sim/sd/docs/circuits/*   -> nothing
kapi_ calls in the app: opendir/readdir/closedir, get_args, get_ticks, mkdir, exit (AppKit); no fkcore call
the shots.sh build line (FT branch, extra = circuit.cpp) compiled and linked by hand -> ok
```

The window was compared by eye with `mockups/circuits-main`, `-empty`, `-check`, `-step`, `-won`, `-lesson`,
`-min`, `-min-fr`, `-fr`, `-fr-check` (the dumps of the test are PNGs in `$TMPDIR/onyx_circuits_sim/`); they match
in layout, colours and words.

### Deviations

1. Step mode is entered at **step 0** (all gates unknown); "F8 twice" = step 1.
2. The gate-open refusal reads *"This AND gate (outlined in red) has an input not connected: wire it, then Check
   again."* (no English article problem; the outline names it). The pack-clash dialog uses the malformed one's form
   (*"“x” cannot be opened: a level "and" is already loaded."*).
3. The lesson pins' name stays *Out* in French (the packs' output names are data, as 04 §13 says).
4. A pack given as the argument is opened after the window shows its level, so a refusal's dialog is over the window.
5. The card wraps on 4 lines (above).

### For Developer C (steps 8+)

- **Fixtures ready for G6**: `tools/tests/desktop_sim/circuits/solving.ini` (3.3 solved, the mock-up's scene),
  `check.ini` (2.2 with OR, F5 → "1 row is wrong…"), `stars.ini`, `and.ini`; packs in `sd/docs/circuits/`.
  `shots.sh` already builds `circuits` (FT list + `extra`); only the `if want circuits` block is missing — copy the
  fixture into `$OUT/writes/apps/circuits.app/progress.ini`, `SIM_POS=8,34`, `rm -rf` that folder after.
- **Click coordinates at 1000 × 620** (client): the board is at x 234, its top `by` = 144 for a 2-line card (3.3,
  2.2: 144); cell 12 px, `ox` 14, `oy` 24 → cell (gx, gy) = (248 + 12 gx, 168 + 12 gy). For `circuits.png` (A = 1,
  B = 0, Cin = 1, the AND g4 selected): `down 290 228;up 290 228;wait;down 290 468;up 290 468;wait;down 470 324;up 470 324`.
  `circuits-check.png`: `key 0x114`. `circuits-step.png`: the first + `key 0x117` three times (= step 2, the mock-up's).
  `run_circuits_sim_test.sh`'s `geo cell` computes any other point.
- The tooltips for AC 17's French shot: `move` over a palette button (x 234 + 58 + 41 k + 20, y `by` − 29) then
  ~30 `wait`s.
- docs/04: the board holds **35** gates at most in practice (A's deviation 2); menus and keys are 04 §7/§8 exactly
  (menu ids for `menu N`: 0 Next … 7 Clear Board, 8 Copy Truth Table, 9 Copy Circuit, 10 Check, 11 Step, 12 Live,
  13 Reset, 14 Open Level Pack, 15 Lesson, 16 Hint, 17 About).
- Not built here: the Pi `make` of `circuits.elf` (newlib: the window uses only `snprintf`, `strcmp`… and the kits).

## Developer C (steps 8–11/12)

Date: 2026-10-06, branch `AutoDev`. Plan: `03-technical-analysis.md` steps 8–11 and the GUI plan's G6, the notes of
`05-validation.md` (16: the grep guard) and Developers A and B's hints. Host only: **there is no AArch64 compiler in
this container** — `make` / `make stage` for the Pi (which builds `circuits.elf`, `lib/fk/kvtext.o`, lets libgen
confirm `filekit.abi` 78–95 and puts `SD:/apps/circuits.app/main` on the card) are **deferred to the user**.

### Commits

| Commit | Step |
|---|---|
| `e38067df` | step 8: `sdcard/apps/circuits.app/app.txt`, `icon.bmp` + `tools/icons/circuits_icon.py`, `sdcard/etc/fileassoc.ini`, `[app.circuits]` in `tools/pkg/packages.ini` |
| `f9edaf32` | step 9 + G6: the `if want circuits` block of `shots.sh`; `screenshots/circuits.png`, `circuits-step.png`, `circuits-check.png`, `circuits-fr.png` |
| `d70b7043` | step 10: docs/04, docs/03, `docs/HANDOFF.md`; every export regenerated |
| (this one) | step 11's results, this section and the summary |

### Step 8 — the card and the package (declared, not published)

- `sdcard/apps/circuits.app/app.txt`: `name = Circuits`, `category = Programming`, `opens = circuits`, `stack = 4M`
  (Turtle Quest's form).
- `tools/icons/circuits_icon.py` (Pillow, notes_icon.py's method: drawn at 160 px, brought to 40 × 40, magenta key)
  → `sdcard/apps/circuits.app/icon.bmp`: an AND gate on the board's pale green paper, a lit and a dark input wire, a
  lit output into a yellow lamp (the board's colours of `gates.h`). Looked at enlarged beside Turtle Quest's.
- `sdcard/etc/fileassoc.ini`: `# Circuits (logic puzzle level packs)` + `circuits = circuits` after Turtle Quest's.
- `tools/pkg/packages.ini`: `[app.circuits]` after `[app.turtle]`, `needs = uikit >= 1.779, systemkit >= 1.74,
  filekit >= 1.96`, `opens = circuits`, with a comment: filekit 1.96 = the `fk_kv_*` slots 78–95, to be confirmed by
  the Pi build and at publish. **Not published**: no `publish.sh`, `versions.ini`, `sdcard/var/pkg/db` untouched.
- Dry run (nothing pushed, nothing kept): `python3 tools/pkg/mkrepo.py --out <scratch>/repo --no-sign --versions
  <scratch copy of versions.ini> --bump` → 78 packages; `[circuits]` 1.0.0, category Programming, `needs = …,
  filekit >= 1.96, kapi >= 93`, `assoc = circuits=circuits` (its `main` comes with the user's `make stage`).
  Note: `main` now ships uikit 1.781 / systemkit 1.76 (Notes' round); the `needs` above are minimums and stay right.

### Step 9 / G6 — the screenshots

`shots.sh`'s `if want circuits` block (after Turtle Quest's): the fixtures `desktop_sim/circuits/solving.ini` and
`check.ini` copied into `$OUT/writes/apps/circuits.app/progress.ini`, `SIM_POS=8,34`, Developer B's coordinates at
1000 × 620; the folder removed after.

| PNG | Scene |
|---|---|
| `screenshots/circuits.png` | 3.3 *Full adder* solved; A and Cin clicked (A = 1, B = 0, Cin = 1: Cout lit, S dark, the table's row `1 0 1` marked); the AND g4 clicked (selected) |
| `screenshots/circuits-step.png` | the same + F8 three times: *Step 2 of 3*, depth badges 1/2/3, the OR grey with a dashed wire, Cout `?`, Step lit, Reset enabled |
| `screenshots/circuits-check.png` | 2.2 *The hallway light* with an OR, F5: the *Yours* column, row `1 1` red with its ✗, both switches set on it, *"1 row is wrong: with A = 1 and B = 1 the lamp must stay off. …"* in red |
| `screenshots/circuits-fr.png` | the first scene in French (`lang fr`) + the pointer resting on the palette's NON-OU: the tooltip *"Poser une porte : NON-OU (6)"* (AC 17's "palette tooltips"; validation note 12) |

All four looked at with the Read tool: layout, colours, words correct; nothing overlaps. A first try hovered the
palette's first gate: its tooltip hid the other gates' names, so the last gate (NOR) is hovered instead (the tooltip
then lies over the empty part of the tool bar). The French card wraps on 3 lines (the board 18 px lower): the clicks
still hit (the switches are 24 px high, the AND large enough), checked on the picture.

**French check (`SHOTS_LANG=fr`)**: `SHOTS_LANG=fr SHOTS_PNG=<scratch> sh tools/tests/desktop_sim/shots.sh circuits`
→ all four scenes rendered in French and looked at: *Vérifier*, *Pas à pas*, *Au départ*, *Table de vérité*
(*ENTRÉES / BUT / OBTENU*), *1 ligne est fausse : avec A = 1 et B = 1 la lampe doit rester éteinte. Les
interrupteurs sont mis sur cette ligne.*, *Pas 2 sur 3 : les portes de profondeur 2 sont calculées. F8 : la suivante
· F9 : au départ · F7 : en direct.*, the palette NON / ET / OU / OUX / NON-ET / NON-OU — nothing truncated except the
list's long titles, fitted with "…" as designed. (Not committed: only the English run's PNGs are in `screenshots/`,
plus `circuits-fr.png`.)

Found while running it (pre-existing, not Circuits'): `shots.sh`'s build of **paint, letters, sheet, slides,
media, pdf, photos** fails to link (`print_begin`, `print_dialog`, `print_font_data`… — PrinterKit is not linked
by `shots.sh`'s generic build lines). It is the same on `origin/main`; it only matters when one of those apps is
asked for. Not fixed here (outside this round's scope); worth a line to the user.

### Step 10 — the documentation

- `docs/04-USER-GUIDE.md`:
  - §12's catalog: a **Circuits** row right after Turtle Quest's (the table they share), pointing to the section;
  - a section **"Circuits, logic gates as a puzzle game (`circuits`)"** in §12, after GPIO Lab and before *Games*
    (with the other Programming apps; not in §13 *Programming in BASIC*, validation note 13): the window, building
    (placing, wiring, one wire per input, the loop message, picking a wire up, editing, undo 64), **the 35-gate
    limit** ("as many as fit between the columns"), live / step by step / Check, the stars, the three worlds and
    their 20 levels (a table), the lessons and hints, the menus, French, the files (packs, `progress.ini`, the pack
    and circuit-text formats); the four screenshots with captions. 2.2's stars are not called "1 star" (note 1);
  - the menu bar's *Programming* list (§5) and the **Language & Region** row's translated apps gain Circuits.
- `docs/03-DEVELOPER-GUIDE.md`: a **Circuits** paragraph after Turtle Quest's (the engine / window split, `fk_kv`,
  the PC build, the three tests, the screenshots) and Circuits in the translated-apps note (§ text faces / lang).
  (§5.6 / §5.8 / docs/06 / docs/14 were done in step 1 by Developer A.)
- `docs/HANDOFF.md`: a section at the top *"Circuits, a logic-gate puzzle game (AutoDev round 2)"*: what is done,
  tested, not done (the Pi build), the 35-gate limit, and the **follow-ups**: Turtle Quest's `kv_*` / `parse_pack`
  and Notes' `notes.ini` onto `fk_kv`; sequential logic (latches, flip-flops, a clock); chips; Paste Circuit, the
  sandbox, several players; a level editor; a shared star glyph in UIKit.
- `python3 docs/build_docs.py`: `pypandoc` was missing → `pip install pypandoc` (`python-docx` was there); then
  **every `.docx` and `.pdf` regenerated, the PDFs by LibreOffice** ("Done.", ~70 s, no error). Committed.

### Step 11 — regression and rules (AC 24, 26)

| Check | Result |
|---|---|
| `git diff --stat origin/main -- kernel/include/kern/kapi_abi.h kernel/sys user/Kits/appkit user/Apps/turtle` | shows `user/Apps/turtle/world.h` — **from `main`, not from AutoDev**: `origin/main` moved on after the round's resync (`4b2b26bf` "GPIO Lab in French…, the French words in the BASIC library"). Against the merge base (`git diff $(git merge-base HEAD origin/main) HEAD -- …`) the diff is **empty**, and `git log origin/main..HEAD -- user/Apps/turtle` is empty: AutoDev touched none of these. The next resync with `main` brings it in. |
| `grep -L "MIT License" user/Apps/circuits/* user/Kits/filekit/kvtext.* tools/tests/circuits/* tools/tests/filekit/* tools/icons/circuits_icon.py tools/tests/run_{kvtext,circuits,circuits_sim}_test.sh` | nothing (every file has it) |
| `grep -n "kapi_" user/Apps/circuits/*` | only AppKit's `kapi_opendir`, `kapi_readdir`, `kapi_closedir`, `kapi_dirent` (its type), `kapi_get_args`, `kapi_get_ticks` (the autosave's clock), `kapi_mkdir` (`SD:/docs/circuits` before Ctrl+O), `kapi_exit` — the allow-list widened per validation note 7 |
| `grep -nE "\bfk_[a-z]" user/Apps/circuits/* \| grep -v "fk_kv"` (note 16) | nothing: no FileKit core call |
| `python3 tools/lang/check.py circuits` | `circuits [fr]: 108 words, 0 missing, 0 not used` (exit 0) |
| `sh tools/tests/run_kvtext_test.sh` | `ok kvtext, ASan build, no files (101 checks)`, `ok kvtext (115 checks)` |
| `sh tools/tests/run_circuits_test.sh` | `ok circuits (582 checks: the engine; 3 packs, 20 levels solved with three stars)` |
| `sh tools/tests/run_circuits_sim_test.sh` | `circuits-sim: all 61 checks passed` |
| `sh tools/tests/run_turtle_test.sh` | `ok turtle (27 levels: solved, written back; the errors)` — unchanged |
| `sh tools/tests/desktop_sim/shots.sh turtle` (into a scratch folder) | `turtle.png`, `turtle-fr.png`, `turtle-editor.png` **byte-identical** to `screenshots/` |
| `sh tools/tests/run_archiver_test.sh` (FileKit's core includes `filekit.h` → `kvtext.h`) | all `ok`, `0 failure(s)` |
| `sh tools/tests/run_notes_test.sh`, `run_notes_sim_test.sh`, `run_stickies_sim_test.sh` (FileKit's `fsutil` users, round 1) | `notes: all checks passed`; `notes-sim: all 67 checks passed`; `stickies-sim: all 29 checks passed` |
| `sh tools/tests/run_pkg_test.sh` (`packages.ini`, `fileassoc.ini` changed) | `0 failure(s)` |

### Step 12 — not done

The SHOULD items (Paste Circuit, chips, sandbox, several players) were **not** started: each needs its own engine,
window, test, French and docs work, and the round's MUST is complete and green; starting one half-way is less safe
than leaving it for a later round (they are in HANDOFF's follow-ups).

## Summary

### Acceptance criteria (02 §10)

| AC | Status | How / why |
|---|---|---|
| 1 three packs, 20 levels, ids unique, in order | **done** | `run_circuits_test.sh` part B |
| 2 tables = objectives | **done** | idem (a C++ lambda per level, every row) |
| 3 solutions win, 3★, = Min | **done** | idem |
| 4 stars by gate count | **done** | part A |
| 5 wrong rows (`xor` with OR: row 11; `and` with OR: 01, 10) | **done** | part A; in the app: sim G3 + `circuits-check.png` |
| 6 incomplete board refused | **done** | part A; the app's red outline (sim) |
| 7 loops refused | **done** | part A; sim G2 (message, board unchanged) |
| 8 one input one wire, fan-out | **done** | part A |
| 9 parts allowed, pins, 49th gate refused | **done**, with a limit | the engine refuses a 49th gate (`E_FULL`, tested on a board filled by hand), but **only 35 gates fit** on the 40 × 30 board, so placing stops at 35 (`E_OVERLAP`/`E_OUTSIDE`); documented in docs/04 and HANDOFF |
| 10 step by step depths | **done** | part A; sim G3; `circuits-step.png` |
| 11 circuit text round-trip, errors with lines | **done** | part A |
| 12 pack errors with lines | **done** | part B (14 malformed variants); sim AC 20 (malformed pack refused) |
| 13 progress round-trip | **done** | part B; `run_kvtext_test.sh` |
| 14 unlocking, stars never lower | **done** | part B; sim AC 21 |
| 15 undo / redo, 64 steps | **done** | part A; sim G2 (Ctrl+Z / Ctrl+Y) |
| 16 Copy Truth Table text | **done** | part A; sim AC 16 (`SIM_CLIPFILE`) |
| 17 three screenshots | **done** | `circuits.png`, `circuits-check.png`, `circuits-fr.png` (+ `circuits-step.png`); the French one with a palette tooltip; checked with `SHOTS_LANG=fr` too |
| 18 fresh start / fixture stars | **done** | sim AC 18 |
| 19 live switch → lamp | **done** | sim AC 19 (pixels), `circuits.png` |
| 20 `circuits <pack>` | **done** | sim AC 20 (+ drop) |
| 21 progress after a win | **done** | sim AC 21 |
| 22 French complete, `.fr` keys | **done** | `check.py circuits` 0 missing; part B |
| 23 Makefile / card / fileassoc / package | **partly — the Pi build deferred to the user** | `user/Makefile` (`FT_APPS`, `FT_EXTRA_circuits`, `FK_OBJ += lib/fk/kvtext.o`), `app.txt`, `icon.bmp`, `levels/`, `lang/fr.txt`, `fileassoc.ini`, `[app.circuits]` all done; `make` / `make stage` need the AArch64 compiler, absent here |
| 24 MIT, no kapi change, kits only | **done** | step 11 (no AutoDev change to kapi / AppKit / Turtle; the `filekit.abi` lines 78–95 to be confirmed by libgen on the user's `make`) |
| 25 docs, build_docs | **done** | docs/04, 03, 06, 14, HANDOFF; exports (docx + PDF via LibreOffice) regenerated |
| 26 Turtle unchanged | **done** | `run_turtle_test.sh` passes; `shots.sh turtle` byte-identical |

### Tests

| Test | Result |
|---|---|
| `sh tools/tests/run_kvtext_test.sh` | ok (101 + 115 checks) |
| `sh tools/tests/run_circuits_test.sh` | ok (582 checks) |
| `sh tools/tests/run_circuits_sim_test.sh` | all 61 checks passed |
| `python3 tools/lang/check.py circuits` | 108 words, 0 missing, 0 not used |
| `sh tools/tests/desktop_sim/shots.sh circuits` (EN, and `SHOTS_LANG=fr`) | 4 PNGs each, looked at |
| `sh tools/tests/run_turtle_test.sh` | ok (27 levels) |
| `sh tools/tests/desktop_sim/shots.sh turtle` | 3 PNGs byte-identical |
| `sh tools/tests/run_archiver_test.sh` | 0 failures |
| `sh tools/tests/run_notes_test.sh` / `run_notes_sim_test.sh` / `run_stickies_sim_test.sh` | all passed (67 / 29 sim checks) |
| `sh tools/tests/run_pkg_test.sh` | 0 failures |
| `mkrepo.py --no-sign` dry run into a scratch folder | 78 packages, `[circuits]` well formed |
| `python3 docs/build_docs.py` | Done (docx + PDF) |

### What the user must check by hand on the Pi

1. From `kernel/`: `make` — `lib/fk/kvtext.o` compiles for AArch64, **libgen accepts the hand-appended
   `filekit.abi` lines 78–95** (if it reorders them, keep libgen's and adjust `needs = filekit >= 1.96` to the version
   it gives), `circuits.elf` links (newlib); then `make stage` → `SD:/apps/circuits.app/main`.
2. Start Circuits from *Onyx ▸ Programming*: the first-start lesson card, only 1.1 open; the window at its size.
3. The mouse on real hardware: drag a gate from the palette, click-then-click, wire output → input (the green / red
   ring), pick a wire up, move a gate, right click; the switches and table rows live.
4. F5 / F8 / F9 / F7, the result card and the next level opened; Ctrl+Z / Ctrl+Y.
5. The 1 s autosave on the card: quit, restart — the board and stars are back (`SD:/apps/circuits.app/progress.ini`).
6. A `.circuits` file double-clicked in the File Viewer (the association) and Ctrl+O (`SD:/docs/circuits` created).
7. In French (Language & Region ▸ Français, then restart Circuits): the words fit as on `circuits-fr.png`.
8. Then publishing (outside AutoDev): `tools/pkg/publish.sh` after the user's validation, merging `AutoDev` into
   `main` first.
