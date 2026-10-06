# AutoDev round 3 — Technical Analyst: Turtle Quest, *Gems, portals and fractals*

Date: 2026-10-06. Inputs: `01-product-manager.md`, `02-product-analysis.md` (scope, the 21 levels, AC1–AC18 — kept
as they are; where this analysis makes a choice the PA left open, it says so: **[TA]**). Branch `AutoDev`.

Read for this analysis: `user/Apps/turtle/world.h` (all 736 lines), `user/Apps/turtle/main.cpp` (the progress
store, the packs, `Board`, `WordBar`, `Card`, `MsgBar`, `Lesson`, `ToolPal`, the run / playback, the editor,
`retitle`, `layout`, `main`), `sdcard/apps/turtle.app/levels/*.turtle`, `user/Libs/basic/bas.h` (`ExtWord`,
`Dialect`, `Host`), `bascomp.cpp` (`FRENCH`: `FONCTION`, `SORTIR` = `EXIT`, `PROCEDURE` = `SUB`), `basvm.cpp`
(`MAXFRAMES = 400`, `STACK = 2048`, *"Out of stack space (too deep recursion)"*),
`tools/tests/turtle/turtletest.cpp`, `tools/tests/run_turtle_test.sh`, `tools/tests/run_basic_test.sh`,
`tools/tests/desktop_sim/shots.sh` (build line 131, scenario 345–359) + `desktop_sim/turtle/{maze,star-fr}.ini`,
`user/Makefile` (`turtle.elf`, l. 495–501), `tools/pkg/packages.ini` (`[*apps]`, `[app.turtle]`),
`user/Kits/uikit/dropdown.{h,cpp}`, `segmented.h`, `combobox.h`, `user/Kits/filekit/kvtext.h`, docs/03 *Turtle
Quest* (l. 3944), docs/04 §12 row (l. 4548) and §13 (l. 5266…), round 2's `03-technical-analysis.md` (format).

**Summary.** An application change only: **no kapi change, no kernel change, no kit change, no new file type**
(FileKit's existing `fk_kv` only if SHOULD 3 is done). Everything goes into the app's two files — the engine
`world.h` (testable on the host) and the window `main.cpp` — plus two new pack files, test additions, shots
fixtures and docs. Onyx BASIC needs **no change**: parameters, `FUNCTION`, `EXIT SUB` and recursion already work
(prototype below).

### Environment (checked in this container)

| Item | State |
|---|---|
| `aarch64-none-elf-g++` | **absent** — `make` from `kernel/` (AC12's card build) is deferred to the user, as in rounds 1–2 |
| `sh tools/tests/run_turtle_test.sh` (baseline) | **passes**: `ok turtle (27 levels: solved, written back; the errors)`, 49 s (ASan + UBSan) |
| host `g++`, `python3` | present (the simulator and `shots.sh` build on the host) |

### Prototype run (the engine as it is, on a scratch pack of pack 5's reference solutions, all `draw = 1`)

The PA's §6.2 sizes were checked **in the real engine** (`world.h` + the BASIC core, a 24 × 18 page of `.`):

| id | start | result | instr. | events | segs | x range | y range | `same_drawing` (self) |
|---|---|---|---:|---:|---:|---|---|---|
| polygon-sub | (3,8) `^` | won | 11 | 93 | 31 | 3.0–16.8 | 4.1–9.4 | 0.1 ms |
| polygon-row | (8,14) `^` | won | 6 | 179 | 66 | 8.0–12.8 | 10.6–15.4 | 0.5 ms |
| rainbow-spiral | (11,9) `^` | won | 4 | 217 | 136 | 3–19 | 2–17 | 2.0 ms |
| hex-spiral | (11,9) `^` | **off the page** | 5 | — | — | — | — | — → start at **(11, 8)** (python: y −7…+9 around the start) |
| color-flower | (11,9) `^` | won | 6 | 253 | 108 | 6.7–15.3 | 4–14 | 0.7 ms |
| halves | (3,2) `>` | won | 7 | 44 | 16 | 3–11 | 2–6 | 0.0 ms |
| snail-rec (`Snail 13`) | (3,15) `^` | won | 6 | 160 | 91 | 3–15 | 2–15 | 0.7 ms |
| tree (`Tree 6, 4`) | (11,17) `^` | won | 10 | 283 | 84 | 6.0–16.0 | 4.6–17 | 0.3 ms |
| koch (`Koch 18, 3`) | (2,12) `>` | won | 12 | 489 | 64 | 2–20 | 6.8–12 | 0.2 ms |
| snowflake (`Koch 12, 2` ×3) | (5,5) `>` | won | 14 | 416 | 96 | 5–17 | 1.5–15.4 | 0.5 ms |
| sierpinski (`Tri 16, 3`) | (3,16) `>` | won | 18 | 851 | 276 | 3–19 | 2.1–16 | 4.4 ms |
| *(a recursion with no stop)* | | **R_ERROR** line 3 | | 1202 | | | | *"Line 3: Out of stack space (too deep recursion)"* — the raw text a pupil gets today |

So: recursion with two parameters, `EXIT SUB` inside a block `IF`, `FUNCTION` returning a value, `COLOR i MOD 6 + 1`
all run; the largest record is 851 events (2 % of `MAX_EVENTS = 40000`); the runaway recursion hits
`MAXFRAMES = 400` (at ~1200 events) long before `MAX_EVENTS`, so it is an **R_ERROR** (not R_ENDLESS) — the
friendly message (PA §E.19) maps the VM text. The comparison costs at most ~5 ms on the PC (≈ 50 ms on the Pi,
once per Run). The prototype files are throw-away (not committed).

---

## 1. What exists and is reused (by file / function)

### 1.1 The engine — `user/Apps/turtle/world.h`

| Piece | What it gives | Reused / changed for |
|---|---|---|
| `WORDS[]`, `W_*` enum, `FR_NAME[W_COUNT_]`, `ALIASES_FR`, `SHOWN_FR`, `word_id`, `word_name`, `word_help` (`EN/FR[W_COUNT_]`) | the turtle's words as a BASIC dialect (`bas::ExtWord { name, id, kind 's'/'n', args }`) | **`W_GEM`** appended after `W_HEADING` (so `W_COUNT_` grows and the per-id tables keep their order): `{ "GEM", W_GEM, 'n', "" }`, `FR_NAME` `"GEMME"`, `ALIASES_FR` `"GEMME","GEM"`, `SHOWN_FR` `"GEM","GEMME"`, both `word_help` lines; `FRONT`'s help gains *"5 a teleporter"* (EN + FR) |
| `CONCEPTS[]` (`key`, `title[2]`, `text[2]`), `find_concept` | the lesson cards | 6 new entries (`gems teleport color params function recursion`) |
| `Level` (`bool draw`, `topic[16]`, `map[MAXH][MAXW+1]`, `set` by `memcpy`) | a level | `bool draw` → **`int draw`** (0 none, 1 shape, 2 shape + colours); `memcpy`/`memset` stay valid |
| `parse_pack` (`flush` lambda, `key.fr`, `\|` lines, the final per-level checks) | the pack reader | `draw` value parsed as a mode; a call to the new **`check_level`** in the final loop (gems, pads) |
| `write_pack` (`kv`, `block`) | the editor's writer | `draw = 1` / `draw = color` |
| `World` (`cell`, `painted`, `x y h`, `pen color keys coins coinsTotal`, `hasGoal gx gy`, `segs`; `reset`, `copyFrom`, `look`, `blocks`, `won`) | the world | `gems`, `gemsTotal`, the pads' places (`pad[2][2]` cells) in `reset`; **`copyFrom` must copy them** (the playback's `g_view` is copied); `won` counts gems |
| `Ev` / `EV_*` / `apply` | the record and its replay (recorder **and** window share `apply`) | **`EV_TELEPORT`** appended (a, b → c, d; no segment; `paintHere`); `EV_PICK` counts a gem |
| `friendly` (`MAP[]` by prefix) | compiler / VM messages made friendly | one row: `"Out of stack space"` → the recursion sentence (EN/FR) |
| `Recorder : bas::Host` — `move` (steps of ≤ 1 square; `grid = !draw`), `turn`, `ext` (the words) | the turtle | teleport at the end of a step in `move`; `W_PICK` order; `W_GEM`; `W_ITEM`, `W_FRONT` |
| `sample` / `seg_dist` / `covered` / `same_drawing` (0.1 square sampling, 0.15 tolerance) | the drawing comparison | a colour-aware variant (§2.4) |
| `run_program` (the `OWN[]` prefixes, the not-won messages), `target_of` | a run, its result, stars, messages; a drawing's figure | the gem message; `OWN[]` gains `"Gem"`, `"D'abord"`; the colour message |
| `count_instructions` | stars | unchanged |

### 1.2 The window — `user/Apps/turtle/main.cpp`

| Piece | Reused / changed for |
|---|---|
| `PEN[16]` | the gem / pad / tint colours can be picked from it or set by UX |
| `ev_ms (e)` | `EV_TELEPORT` duration (e.g. `300 * k`) |
| `Board::tile (cv, c, r, k, w, pickT, pickCell)` | draws `'1'…'9'` (gem, its number when `cs ≥ 16`, the pick shrink via `pickCell` as coins), the **next gem ring** (`k - '0' == w.gems + 1`), `'T'`/`'U'` (pads); returns early on a drawing level already (so gems / pads there show as page dots — PA §10 "ignored") |
| `Board::onDraw` | the target: grey (`0xD9D6CC`) for `draw == 1`, **tinted per segment** (`uk_mix (page, PEN[s.color], ~90)`) for `draw == 2`; the `EV_TELEPORT` animation (turtle at pad A for t < 0.5, at B after; a ring flash at both); the HUD condition `w.keys || w.coinsTotal || w.gemsTotal` and *"Gems n / N"* / *« Gemmes n / N »* |
| `Board::onMouse` (the drag test `map[r][c] != TOOL_CH[g_tool]`) | the gem tool must not cycle while dragging → `edit_cell (c, r, drag)` |
| `WordBar::build` — `ORDER[]` (the concept's rank decides the purple control chips), `chips[32]` | **all six** new keys appended after `"draw"` **[TA]** (the PA put only the last ones there; but a concept missing from `ORDER` gives `lv = -1` → *no* control chip at all, and pack 4 uses REPEAT / IF / WHILE / SUB); 17 words + 5 chips = 22 < 32 |
| `CONCEPT_KEYS[]`, `NCONCEPTS = 14` | 20 keys |
| `ToolPal` (`NTOOLS = 9`, 3 × 3, `EN/FR[NTOOLS]`, swatches by index), `TOOL_CH[]`, `edit_cell`, `g_tool == 7` (the turtle tool, never dragged) | 3 tools appended (indices 9, 10, 11 — the existing indices, including 7, stay), 4 rows |
| `g_edDraw` (`Checkbox`), `edit_collect`, `open_editor`, `cb_ed_test`, `cb_ed_save`, `retitle` (the editor labels) | the three-way drawing choice; the level check before Save / Test |
| `run_program (*g_L, …, g_L->draw ? &g_target : 0)`, `show_level` (`if (g_L->draw) target_of`) | unchanged: `int draw` is truthy for 1 and 2 |
| `L2 (en, fr)`, `T (lang, en, fr)` | every new string (the app's own mechanism, PA §G.25; `tools/lang/check.py` does not cover Turtle Quest) |
| the progress `kv_*` (`<id>`, `<id>.code`, `seen.<concept>`) | unchanged format; new ids / concepts are new keys |

### 1.3 Onyx BASIC (`user/Libs/basic`) — no change

`SUB name (a, b)` called as `Name x, y`, `FUNCTION f (x)` … `f = …`, `EXIT SUB` (French `SORTIR SUB`), `FIN SUB`,
`FONCTION`, `MOD`, recursion up to `MAXFRAMES = 400` frames (`STACK = 2048` values) — all exercised by the prototype
and by `turtletest.cpp`'s existing French `FONCTION`/`SUB` case. `run_basic_test.sh` is untouched by this round.

### 1.4 Tests, simulator, build, package

- `tools/tests/turtle/turtletest.cpp`: already loops over **every pack on the command line** (parse, write back,
  re-read, every solution won with 3 stars), then negative cases on named levels. `run_turtle_test.sh` passes
  `sdcard/apps/turtle.app/levels/*.turtle` → the two new packs are picked up **with no script change**.
- `shots.sh`: the `turtle` build line (131) compiles `main.cpp` (it includes `world.h`) + the BASIC core — no change;
  the scenario (345–359) gains shots.
- `user/Makefile` `turtle.elf` (l. 498): depends on `Apps/turtle/world.h` already; links `lib/filekit.imp.a` already
  (SHOULD 3 needs no Makefile change).
- `tools/pkg/packages.ini`: `[*apps]` makes `apps/turtle.app/` (all of it, `levels/` included) the `turtle` package;
  **no line to add** for the new packs. Only if SHOULD 3 is done: `[app.turtle] needs` gains `filekit >= 1.96`
  (the `fk_kv_*` entries, as Circuits'). Declared, **not published** (PIPELINE §0.5).

---

## 2. What is missing, and where it goes

Everything is used by Turtle Quest alone → **beside the app** (CLAUDE.md "kits first": what one program alone uses
stays beside it). Nothing here is reusable by another app except the progress store, which FileKit already has
(SHOULD 3).

| Missing | Where |
|---|---|
| `int draw` mode, its parse / write | `world.h` `Level`, `parse_pack`, `write_pack` |
| **`check_level (const Level &, int lang, char *why, int cap)`** — gems 1…N no gap / no repeat, each pad letter 0 or 2 times — **[TA]** one function used by `parse_pack` (EN reason, prefixed *"level %d (%s): "*) and by the editor's Save / Test (in the player's language) so the two can never disagree | `world.h` |
| Gems: `World::gems / gemsTotal`, `gem_at (c, r)` (0 on a drawing level or not a digit), `W_GEM`, `PICK` order, `ITEM`, `FRONT`, `won`, the messages | `world.h` |
| Teleporters: `World::pad`, `twin (c, r, &tc, &tr)`, `EV_TELEPORT`, `move`, `FRONT` = 5 | `world.h` |
| Colour comparison: `same_drawing (mine, target, bool colour)` + `same_colours` | `world.h` |
| Recursion message; 6 lesson cards; `word_help (W_GEM)` | `world.h` (`friendly`, `CONCEPTS`) |
| **Editor operations as pure functions [TA]** — `edit_gem (Level &, c, r, bool drag)` (lowest free number / cycle 1→9→1), `edit_pad (Level &, char ch, c, r, int &lastC, int &lastR)` (a third pad removes the older one) — so AC17's rules are **host-tested**; `main.cpp`'s `edit_cell` calls them | `world.h` |
| Drawing gems / pads / next-gem ring / HUD / teleport animation / tinted target / tools / draw chooser | `main.cpp` |
| The two packs | `sdcard/apps/turtle.app/levels/4-gems-and-portals.turtle`, `5-spirals-and-fractals.turtle` |
| Tests | `tools/tests/turtle/turtletest.cpp` (extended); no new script |
| Shots fixtures | `tools/tests/desktop_sim/turtle/{portals,fractal,gems-fr,editor-gems}.ini` (names indicative) |

### 2.1 kapi / kernel / kits

**None.** `kapi_abi.h`, `kapi*.cpp`, `appkit.h`, `appkit_calls.inc`, `appkit.abi` untouched; no UIKit change
(the widgets used exist: `Dropdown`, `SegmentedControl`, `Checkbox`, owner-drawn `Widget`s). FileKit is only
*used* (SHOULD 3), not changed.

### 2.2 Gems — the engine's rules (PA §A)

- Map `'1'…'9'`. `World::reset`: `gemsTotal = (draw ? 0 : count of digits)`, `gems = 0`.
- `blocks ()`: unchanged (a digit is not a wall). `look` unchanged.
- `W_PICK`: `k = cell`; if `gem_at` → `d = k - '0'`; `d != gems + 1` → error
  `snprintf (why, cap, T (lang, "Gem %d first! This is gem %d.", "D'abord la gemme %d ! Celle-ci est la %d."), gems + 1, d)`
  (`fail ()` takes fixed strings: a formatted variant is needed); else `EV_PICK` as a coin.
  `apply (EV_PICK)`: a digit → `w.gems++`, the cell becomes `'.'`.
- `W_ITEM`: `k == 'k' || k == 'c' || gem_at`. `W_FRONT`: `D`→4, blocks→1, `k`/`c`/gem→2, goal→3, **pad→5**, else 0.
- `W_GEM`: `gem_at (cx, cy)` (0 once picked: the cell is `'.'`).
- `won ()`: `gems < gemsTotal` → false. `run_program`'s not-won branch: the gem message after the coins' and before
  the flag's: *"The program ended, but %d gem(s) are still on the floor."* / *« … il reste %d gemme(s) par terre. »*
- `OWN[]` gains `"Gem"`, `"D'abord"` (else the French message goes through `bas::frenchMessage`).

### 2.3 Teleporters — the engine's rules (PA §B)

- `World::reset` records each pad letter's two cells (`check_level` guarantees 0 or 2).
- In `Recorder::move`, after the step's `EV_MOVE` is pushed, on a **grid** level: if the turtle now stands **on a
  pad's centre** (`|x − c| < 1e-6 && |y − r| < 1e-6`, the cell a `T`/`U`) **and the step started elsewhere** →
  push `EV_TELEPORT { a, b = pad, c, d = twin }`. **[TA]** the "centre" test (not "the cell changed") keeps a
  fractional move (`FORWARD 0.5` twice) from jumping half-way, and arriving by the jump is not a step, so there is
  no ping-pong. The remaining steps of `FORWARD n` continue from `w.x, w.y` = the twin (the loop already reads
  `w.x`), heading untouched. Turning, starting, `PICK`, sensors on a pad: nothing (they never call `move`).
- `apply (EV_TELEPORT)`: `w.x = c; w.y = d; w.paintHere ();` — **no `Seg`**, so no line across (AC6).
- A drawing level: `grid` is false → no teleport (PA §10).
- `EV_TELEPORT` is **appended** to the enum (values of the others unchanged; the record is in memory only anyway).

### 2.4 Colour drawings (PA §C)

- `parse_pack`: `draw` = `color` / `colour` / `2` → 2; `shape` / any other non-zero number → 1; absent / `0` → 0.
  `write_pack`: `draw = 1` or `draw = color` (the 8 existing levels write back unchanged — AC2/AC3).
- The comparison **[TA]**: sample each segment with its colour (`sample` gains a parallel `Arr<int>` of colours, or a
  `sample_c` that pushes x, y, colour); `covered_c (pts, cols, segs)` = every point within 0.15 of a segment **of the
  same colour**. `same_drawing (mine, target, colour)`: shape as today; if `colour`, also `covered_c` both ways.
  `run_program`: `draw == 2` and the shape matches but not the colours → *"The right figure, but not the right
  colours."* / *« La bonne figure, mais pas les bonnes couleurs. »*; else the existing *"Not quite the same figure"*.
- **Overlap rule [TA]** (found while checking the solutions): the target is the solution's lines *with their
  colours*; a solution that **retraces** a line in another colour (e.g. Sierpinski's `FORWARD size / 2` along an
  edge drawn in another colour, the pen down) makes a hidden requirement — the player would have to retrace it in
  that colour too, although only the top colour shows. So **colour levels' reference solutions move with `PENUP`
  over lines already drawn**, and the host test **lints** it: on a `draw = color` level, no sampled point of a
  colour-X segment lies within 0.05 of a colour-Y segment (Y ≠ X), the points within 0.2 of a segment end excepted
  (corners touch). The flower's alternate colours are compatible (hexagons k and k+2 share an edge and a colour); the
  lint confirms it.
- The colour rule for packs (PA §13): a colour level's target segments use pens **1–14 except 7 and 8** — linted.

### 2.5 Recursion message and lessons (PA §E)

- `friendly`'s `MAP[]`: `{ "Out of stack space", "Line %d: the word calls itself without end -- does it have a test
  that stops it?", "Ligne %d : le mot s'appelle lui-même sans fin -- a-t-il un test qui l'arrête ?" }` (prefix
  match: also covers the value stack's plain *"Out of stack space"*, which a deep recursion with many locals can hit
  before `MAXFRAMES`).
- The six cards in `CONCEPTS` (EN + FR, texts as PA §E). **Size limit [TA]**: the `Lesson` box is at most
  560 × 360; text starts at y = 58 and stops at `height − 56` → about **14 lines** of 13-px text (code lines
  included) at the largest, fewer on a small board; lines beyond are silently dropped. Each card ≤ ~12 lines in both
  languages; checked in the French shot (AC16).

---

## 3. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | **The editor's *Idea* drop-down cannot show 20 concepts**: UIKit's `Dropdown` has no scrolling list; it opens down or up inside its parents' room (`setOpen`), 20 × 26 + pads ≈ 530 px; at its place (panel y ≈ 218) the room below is 376 px in a 1000 × 640 window → the last six (the new ones) are clipped | **[TA]** move the *Idea* drop-down to the **top of the editor panel** (room below ≈ 580 px at 640 high) and/or build it with `h = 22` (rowH = h): 20 × 22 ≈ 450 px; verified in `turtle-editor-gems.png` with the list open, or by a sim `dump`. Not a UIKit change (a scrolling `Dropdown` would change the class's layout → `uikit.so` ABI for every app) |
| R2 | **The editor panel grows taller**: today its content ends at y = 510 (+10); a 4th tool row (+34) and a separate drawing chooser row (+50) → ≈ 604 px, but at the minimum size (`setMinSize (920, 560)`) the panel is 540 px | UX chooses: put the drawing chooser on the par row (a `Dropdown` 110 px with short options *No / Shape / Colours* — FR *Non / Forme / Couleurs*, ≤ 85 px text) **or** raise `setMinSize` to (920, 640); the shot at the default size proves it |
| R3 | **The existing `turtle-editor.png` scenario clicks at fixed places** (`down 123 431` = the Coin tool, `down 45 505` = Test): a 4th tool row / a moved drop-down shifts them | update the scenario's coordinates to the new layout (same actions, same picture apart from the panel) — AC13 |
| R4 | Colour levels with hidden constraints (retraced lines in another colour) | the overlap lint (§2.4) + PENUP in reference solutions |
| R5 | `World::copyFrom` forgets the new fields (`gems`, `gemsTotal`, pads) → the playback's HUD / ring wrong | the host test compares a copied world's fields; code review |
| R6 | The playback of big fractals is long at the default speed (Sierpinski: 162 squares of line + ~300 statements ≈ 70 s at speed 5) | level texts mention the speed slider; the shots use speed 10 (instant); the record itself is small (851 events) |
| R7 | `parse_pack` refusals could reject the user's own `my-levels.turtle` written by an older build — impossible (older builds had no digits / `T` / `U` meaning) except a map with a lone `T` typed by hand | the message names the level and the pad; acceptable (PA §8) |
| R8 | A new turtle word `GEM`/`GEMME` makes those names unusable as variables in every level (dialect words are global) | no existing level or test uses them; documented |
| R9 | `ORDER[]` — a concept absent from it hides all control chips (§1.2) | all six appended; a host-side check is not possible (main.cpp), the French shot shows the chips |
| R10 | No AArch64 compiler here: the card build (AC12's `make`) cannot run | the host builds (test bench + simulator compile `main.cpp` and `world.h` with `-Wall -Wextra`); the user's `make` confirms, as rounds 1–2 |
| R11 | Pack 4's gists that pick with `IF ITEM () THEN PICK` on a loop where a later gem comes first would error ("Gem 2 first!") | the reference solution decides the map: `portal-gems` uses `IF GEM () = n` or a map where the gems are met in order; the test proves it |
| R12 | `text`/`hint` 480 bytes, `title` 80, a `[pack] title` 80: French UTF-8 cut silently by `tcpy` | AC9 lint: `strlen < sizeof − 1` for every field of packs 4–5 |

---

## 4. Implementation plan (step by step; each step builds and is tested before the next)

Test command after every engine step: `sh tools/tests/run_turtle_test.sh` (must end `ok turtle (…)`); after every
window step: the simulator build — `sh tools/tests/desktop_sim/shots.sh turtle` (or its build line alone).

**Step 1 — the draw mode (no behaviour change).** `world.h`: `int draw`; `parse_pack` reads `1 / shape / 2 / color /
colour`; `write_pack` writes `1` / `color`; the header comment. `turtletest.cpp`: the round-trip check compares
`draw` as an int **and** also `title[0..1]`, `text[0]`, `hint[0..1]`, `topic`, `words` (AC3's full list). *Test*: 27
levels pass; a string pack with `draw = color` reads 2 and writes back `color`. → AC2, AC3 (part).

**Step 2 — gems in the engine.** `W_GEM` + its FR tables and help; `World::gems/gemsTotal`, `gem_at`; `reset`,
`copyFrom`, `apply (EV_PICK)`, `won`; `ext`: `PICK` order (formatted error), `ITEM`, `FRONT` (2), `GEM`;
`run_program`: gem message, `OWN[]`; **`check_level`** (gems part) called by `parse_pack`. *Tests* (inline string
packs in `turtletest.cpp`, independent of pack 4): out-of-order `PICK` → R_ERROR at that line, *"Gem 2 first"* (EN)
and *"D'abord la gemme 2"* (FR); in order + flag → won; a gem left → R_LOST *"gem(s)"*; `GEM ()` = number / 0 after
pick / 0 on floor; `GEMME ()` in a French program; `ITEM ()` true on a gem; `FRONT ()` = 2; a level whose `words`
lack `GEM` → *"does not know GEM"*; a pack with gems `1 3` and one with two `2` → `parse_pack` false, the reason names
the level and the gem; a copied `World` keeps `gems`. → AC4, AC5.

**Step 3 — teleporters in the engine.** pads in `reset`; `twin`; `EV_TELEPORT` + `apply`; `move`'s jump; `FRONT` =
5; `check_level` (pads part); `word_help (W_FRONT)` value 5 EN/FR. *Tests*: a step onto `T` → turtle on the twin,
heading unchanged, exactly one `EV_TELEPORT` with the right from / to; `FORWARD 3` with the pad 1 ahead ends 2 squares
past the twin; the twin does not jump back; with the pen down no `Seg` joins the pads (none longer than 1 square);
`FRONT ()` = 5 and `WALL ()` false facing a pad; `BACK` onto a pad jumps too; one `T` / three `U` → `parse_pack`
false naming the level and the pad; a drawing level with a `T` pair does not jump. → AC6, AC7.

**Step 4 — colour drawings.** colour sampling, `covered_c`, `same_drawing (…, colour)`, the colour message.
*Tests* (inline packs first): a colour level's own solution wins; one `COLOR` changed → R_LOST *"not the right
colours"* (FR *"pas les bonnes couleurs"*); a wrong shape → *"Not quite the same figure"*; `draw-square` (pack 3)
with `COLOR 4` added still wins. → AC8 (engine part).

**Step 5 — recursion message, lessons, word help.** `friendly` row; the six `CONCEPTS` cards EN + FR; `word_help`
GEM. *Tests*: a runaway recursive SUB on an inline drawing level → R_ERROR, *"calls itself without end"* (EN) /
*"s'appelle lui-même sans fin"* (FR), not *"Out of stack space"*; the six keys found with non-empty titles / texts
in both languages; `word_name (W_GEM, LANG_FR)` = `GEMME`. → AC9 (part), AC11 (negative).

**Step 6 — pack 4, `4-gems-and-portals.turtle`** (10 levels, PA §6.1, the format of §7.1 there). Each map built
around its **reference solution** (`solution =` in the pack — the only place it is stored); `par` = the solution's
count, par2 = par3 + 2…4. *Test*: run_turtle_test.sh lists them, each won with 3 stars. → AC1 (part), AC10.

**Step 7 — pack 5, `5-spirals-and-fractals.turtle`** (11 drawing levels on 24 × 18 pages, PA §6.2; starts from the
prototype table above — `hex-spiral` at (11, 8); `rainbow-spiral`, `color-flower`, `sierpinski` are `draw = color`;
Sierpinski's colours passed as a parameter, its moves over drawn lines with `PENUP`). *Test*: all won with 3 stars;
the recursive ones without *"too deep"*. → AC1, AC11 (positive).

**Step 8 — the pack lint in `turtletest.cpp`** (runs on every pack given, so it guards future packs): ids unique
across all packs; for every level of a pack whose levels have `.fr` texts: `title/title.fr/text/text.fr/hint/hint.fr`
non-empty and `strlen < sizeof − 1`; `[pack] title.fr` set; `concept` known; on `draw = color` levels: target colours
in 1–14 except 7, 8, and the overlap lint (§2.4); `total == 48`. **[TA]** the "every field non-empty" check can apply
to **all** packs: packs 1–3 already have every `title.fr` / `text.fr` / `hint` / `hint.fr` (checked: 27 of each), so
it guards them too without touching them (AC2). → AC9, AC10, AC1's count.

**Step 9 — the window: board and playback** (`main.cpp`). `Board::tile` gems (colour per number, its digit when
`cs ≥ 16`, the pick shrink), the next-gem ring, pads (pair colours); `onDraw`: the HUD with gems, the
`EV_TELEPORT` animation, the tinted target of colour levels; `ev_ms (EV_TELEPORT)`; `WordBar` `ORDER` + the six
keys; `CONCEPT_KEYS` / `NCONCEPTS = 20`. *Test*: the simulator build with no new warning; a quick sim run on pack 4
(step 11's fixture). → AC14/AC15 (prepared), AC12 (sim build).

**Step 10 — the window: the level editor.** `edit_gem` / `edit_pad` in `world.h` (host-tested: lowest free number;
cycle 1→9→1 on a click; a drag places and never cycles; a third `T` removes the older one, so never more than two);
`ToolPal`: tools 9 Gem / Gemme, 10 Portal 1 / Portail 1, 11 Portal 2 / Portail 2 (swatches), 4 rows; `edit_cell
(c, r, drag)`; `Board::onMouse` passes `drag`; the drawing chooser (No / Shape / Colours → `draw` 0 / 1 / 2) in
place of `g_edDraw`, and `retitle`'s labels; **Test** and **Save** run `check_level (g_edLevel, g_lang, …)` first:
a refusal is shown in the message bar (*"Gem 2 is missing."* / *"Portal 1 has no twin."*, FR) and nothing is
written; the *Idea* drop-down placed per R1; the panel per R2. *Tests*: host tests of `edit_gem` / `edit_pad` /
`check_level`'s FR texts; a host round trip of a level holding digits, `T`, `U`, `draw = 2` through `write_pack` /
`parse_pack`; in the sim: Ctrl+E on a pack 4 level shows them. → AC17 (by test + shot), AC3.

**Step 11 — shots.** New fixtures in `tools/tests/desktop_sim/turtle/`: `portals.ini` (a player on pack 4's
`portal-gems` or `grand-finale` with the reference program as `<id>.code`, its `seen.*` set so no lesson covers the
board), `fractal.ini` (pack 5 `snowflake` code, speed 10), `gems-fr.ini` (pack 4 level 1, `seen.gems` absent: the
lesson card shows), `editor-gems.ini`. Scenario additions in `shots.sh`'s `if want turtle`:
`turtle-portals` (F8 × n until just after a jump), `turtle-fractal` (F5 then waits; the win with 3 stars),
`turtle-rainbow` (optional, `rainbow-spiral` run over its tinted target), `turtle-fr-gems` (`lang fr`),
`turtle-editor-gems` (Ctrl+E on a pack 4 level; optionally the gem tool clicked twice on one cell). The three
existing shots kept, `turtle-editor`'s coordinates updated (R3). Check every PNG by eye, and the whole set again with
`SHOTS_LANG=fr SHOTS_PNG=<folder> sh tools/tests/desktop_sim/shots.sh turtle` (the words fit: chips, HUD *Gemmes*,
tools *Portail 1*, the chooser, the lesson cards). → AC13–AC17.

**Step 12 — docs.** `world.h` header comment (map characters `1…9`, `T U`; `draw` modes; the concepts; `GEM`;
`FRONT` 5); `docs/04-USER-GUIDE.md` §12 row (48 levels, five packs, gems, portals, fractals) and §13 (words table +
`GEM ()`/`GEMME ()`, `FRONT ()` 5, gems, teleporters, `draw = color` and the colour rule, the 5 packs / 48 levels,
the editor's tools and chooser, the pack-format paragraph, the new screenshots); `docs/03` *Turtle Quest* (the
events now include teleports, the colour comparison, `check_level`, the editor helpers in `world.h`);
`docs/HANDOFF.md` (Turtle Quest: what is done, what stays); `IDEAS.md` row; `python docs/build_docs.py`.
`06-development.md` states: `packages.ini` unchanged (the `[*apps]` rule ships `levels/`), nothing published.
→ AC18.

**Step 13 — SHOULD, in the PM's order, each only if steps 1–12 are green.**
(1) *Your best*: `<id>.best` kept in `finish_run` when won and smaller; shown in `refresh_count`'s label (EN/FR).
(2) Stars per pack in the pack drop-down's titles / list header (`stars_of` summed; `retitle`). (3) Progress on
`fk_kv` — `fk_kv_load (PROGRESS, FK_KV_ESCAPES)` / `fk_kv_get / set / save` replacing `kv_*`, **only** with a host
test (in `turtletest.cpp` or its own) that loads `maze.ini`, `star-fr.ini` and a multi-line `.code` file and writes
them back **byte-identical** to `kv_save`'s output (header comment, top keys first, `\n` escapes) — else dropped;
`[app.turtle] needs` + `filekit >= 1.96`. (4) A `FUNCTION` chip for `function` / `recursion` levels
(`TPL_EN/FR` + `NEED[]`). (5) The `sandbox` level (`id = sandbox`, no flag, no `par`, always won, one star — needs a
`run_program` rule: a drawing-free, goal-free, gem-free level with `draw = 0` is "won" when the program ends: today
already true (`won ()` with nothing to do) — just a 24 × 18 level and its text).

### 4.1 AC → step

| AC | Step(s) | AC | Step(s) |
|---|---|---|---|
| AC1 (48 levels, 3 stars) | 6, 7, 8 | AC10 (unique ids) | 8 |
| AC2 (old packs unchanged) | 1 (and every step: `git diff` on packs 1-3 empty) | AC11 (recursion) | 5, 7 |
| AC3 (round trip incl. draw mode) | 1, 10 | AC12 (bench + builds) | every step; 9, 10 (sim build); card `make` by the user (R10) |
| AC4 (gems) | 2 | AC13 (old shots) | 11 (R3) |
| AC5 (gem numbering refused) | 2 | AC14 (portals shot) | 9, 11 |
| AC6 (teleport) | 3 | AC15 (fractal / rainbow shot) | 4, 9, 11 |
| AC7 (unpaired pad refused) | 3 | AC16 (French shot) | 5, 9, 11 |
| AC8 (colour drawings) | 4, 7 (rainbow), 8 (colours lint) | AC17 (editor) | 10, 11 |
| AC9 (texts EN/FR, colours 1–14) | 5, 8 | AC18 (docs) | 12 |

---

## 5. Tests

**Host unit tests** — `sh tools/tests/run_turtle_test.sh` (g++ `-Wall -Wextra`, ASan + UBSan; ~50 s today):

1. Every pack (1–5): parsed; written back and re-read equal (id, size, map, par, all texts both languages, topic,
   words, solution, **draw mode**); every solution won with **3 stars**; `total == 48`.
2. The pack lint (step 8): unique ids; every pack's texts non-empty and not truncated; concepts known with EN/FR
   cards; colour levels' pens 1–14 \ {7, 8}; no colour overlap.
3. Gems (step 2), teleporters (step 3), colour (step 4), recursion (step 5) cases — on **inline string packs** (so a
   later change to pack 4/5's maps does not break the unit cases), plus `draw-square` any colour.
4. `check_level`, `edit_gem`, `edit_pad` (step 10); a `World::copyFrom` field check (R5).
5. All existing negative cases unchanged.

`sh tools/tests/run_basic_test.sh` run once at the end: unchanged and passing (no BASIC file touched).

**Reference-solution strategy for the 21 new levels.** Stored where all 27 are today: the level's `solution =`
block in its pack (`sdcard/apps/turtle.app/levels/4-…`, `5-…`), which is also what a drawing level's target is made
from (`target_of`) and what the editor's Test sets `par` from. The bench runs them by the existing glob
(`run_turtle_test.sh` → `turtletest <all packs>`): each must win, 3 stars, and its count printed beside its events
(the table the prototype above starts). Method for the Developer: write the solution first, run the bench, then set
`par = <count> <count + 2…4>` and adjust the map/start (pack 5: bounding box inside −0.5 … 23.5 / 17.5, the
smallest segment ≥ 0.5 square, as measured above). Colour levels: PENUP over drawn lines (lint).

**Simulator** — `sh tools/tests/desktop_sim/shots.sh turtle`: the 3 existing shots + `turtle-portals`,
`turtle-fractal` (+ `turtle-rainbow`), `turtle-fr-gems`, `turtle-editor-gems`; then
`SHOTS_LANG=fr SHOTS_PNG=<folder> sh tools/tests/desktop_sim/shots.sh turtle` to look at all of them in French.
Each PNG looked at (gems numbered, the ring on the next gem, both pad pairs, the turtle on the twin, the HUD, the
target tinted, 3 stars, the chips with `GEMME`, the editor's 12 tools, the drop-down not clipped).

**Card build** — `make` from `kernel/` (the `turtle.elf` rule) by the user (no AArch64 compiler here; R10).
