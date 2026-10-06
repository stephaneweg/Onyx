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
