# AutoDev round 3 — Product Manager

Date: 2026-10-06. Round 3 (round 1 built Notes + Stickies, round 2 Circuits; both awaiting the user's validation).

## Inputs read

- `autodev/QUEUE.md` — the user's queue: 1. Circuits (**done**, round 2), **2. Turtle Quest: more missions**,
  3. Pinball, 4. Lemmings-like. Item 2 is the first not done: **it is the pick**; this document frames its scope.
- `autodev/STATE.md` — rounds 1 (Notes) and 2 (Circuits) done; nothing on Turtle Quest yet.
- `IDEAS.md`, Applications table, row *Turtle Quest : plus de missions*: new worlds (mazes, pick-ups in an order,
  doors with keys, teleporters, several turtles, Logo drawings to reproduce, "shortest program" challenges),
  variables / functions / recursion (spirals, fractals), a daily challenge, "real world" bonus levels (GPIO).
- `docs/HANDOFF.md` — *Turtle Quest* (done and published 2026-10-06; ideas: more packs, a free-drawing sandbox,
  `SAY`, sounds) and *Circuits* (its follow-ups: move Turtle Quest's private `kv_*` store and `parse_pack` onto
  FileKit's `fk_kv`; a shared star glyph).
- `docs/04-USER-GUIDE.md` §13 *Turtle Quest* (the words, winning and stars, the 3 packs / 27 levels, the editor,
  the files and the pack format).
- The code: `user/Apps/turtle/world.h` (736 lines: words, dialect, concepts' lesson cards, `Level`/`Pack`,
  `parse_pack` / `write_pack`, `count_instructions`, `World`, the event record, `Recorder : bas::Host`,
  `same_drawing`, `run_program`, `target_of`), `user/Apps/turtle/main.cpp` (1508 lines: the window, `Board::tile`
  drawing each map character, the level editor and its `ToolPal`, `CONCEPT_KEYS`, the private `kv_*` progress
  store, `L2 (en, fr)` for its words), `sdcard/apps/turtle.app/levels/{1-first-steps,2-loops-and-choices,
  3-variables-words-figures}.turtle`, `tools/tests/turtle/turtletest.cpp` + `tools/tests/run_turtle_test.sh`,
  `tools/tests/desktop_sim/shots.sh` (the `turtle` build line ~131 and scenario ~345, fixtures
  `desktop_sim/turtle/{maze,star-fr}.ini`).
- Circuits for progression: `user/Apps/circuits/circuit.{h,cpp}` (`Progress` on `fk_kv`, `isOpen`, levels locked
  until the previous is won), `user/Kits/filekit/kvtext.h` (`fk_kv_*`).
- Onyx BASIC (`user/Libs/basic`): SUB / FUNCTION with parameters, recursion supported (`basvm.cpp`
  `MAXFRAMES = 400`, a "too deep recursion" error), the dialect hook (`Host::ext`).

### What Turtle Quest already has (so it is NOT new work)

Keys and doors (one kind of key), coins, tiles to paint, the pen and `COLOR`, **drawing levels** (the solution's
figure to reproduce — compared by geometry, any order, **any colour**), spirals with a variable, `SUB`, the
1-3 stars by instruction count (already the "shortest program" challenge), lesson cards, the level editor, several
players, all levels open. So "keys and doors" and "Logo drawings to reproduce" exist; the round must bring
**new** mechanics and **new** ideas to learn, not reskins.

## Candidates and scores (1 = poor, 5 = best)

Value (educational / everyday, the user's priorities first) · Base (what exists to build on) · Feasibility (one
round, result visible in the PC simulator) · Risk (5 = no kernel / kapi change, little risk to existing code).
Rows 1-3 are framings of the queue's item 2; rows 4-5 the next queue items, for reference.

| # | Candidate | Value | Base | Feasibility | Risk | **/20** |
|---|---|:-:|:-:|:-:|:-:|:-:|
| 1 | **Turtle Quest: gems in order, teleporters, colour drawings, recursion & fractals** (2 packs, ~20 levels) | 5 (queue #2; brings functions with parameters and recursion — the IDEAS row's "variables / fonctions / récursion") | 5 (engine, pack format, editor, drawing comparison, test, shots all exist) | 4 (engine additions are local to `world.h`; ~20 levels with solutions is the bulk) | 5 (an app only; the 27 existing levels keep passing) | **19** |
| 2 | Turtle Quest: several turtles (a world where 2-3 turtles run the same / their own program) | 4 | 3 (the `World` holds one turtle; the events, the board, the sensors all assume one) | 2 (the language must say which turtle — `TURTLE n` or objects —, the record and the playback per turtle; a round of its own) | 4 (touches every part of `world.h` and `main.cpp`) | **13** |
| 3 | Turtle Quest: a daily challenge (a level generated from the date) | 3 | 3 (the pack format; no generator) | 2 (a maze generator **plus** a solver to set the par fairly; hard to judge in tests) | 5 | **13** |
| 4 | Pinball (queue #3) | 3 | 3 (`games/game.h`) | 3 (physics tuning, judged by feel) | 5 | **14** |
| 5 | Lemmings-like (queue #4) | 3 | 3 (`games/game.h`) | 2 (pixel terrain + 6 roles + editor: two rounds) | 5 | **13** |

## The pick: **Turtle Quest: more missions** — folder `autodev/rounds/03-turtle-missions/`

The queue imposes item 2, and framing 1 is the right cut for one round: it adds **three mechanics that each teach
something new** (order and counting; thinking ahead in space; colours as data) and one **world of ideas the game
cannot teach today** (SUB / FUNCTION with parameters, then recursion: fractals), all inside the existing engine,
with every level checked automatically by the host test and shown in the simulator. Several turtles and the daily
challenge are each worth a round of their own (below).

## Scope for this round

### MUST (the round is not done without these)

1. **Mechanic A — gems to pick in order.** New map characters `1`…`9`: numbered gems. `PICK` on a gem takes it
   only if it is the **next** number (1, then 2…); otherwise a friendly error at the line (*"Gem 2 first! This is
   gem 3."* / *"D'abord la gemme 2 ! Celle-ci est la 3."*). A level is won only with every gem picked. `ITEM ()`
   is true on a gem, `FRONT ()` says 2 for it. One new sensor **`GEM ()`** (`GEMME`): the number of the gem under
   the turtle (0: none) — so a program can decide (`IF GEM () = n THEN PICK : n = n + 1`). Drawn as a coloured
   gem with its number; picked ones vanish (the existing pick animation).
2. **Mechanic B — teleporters.** Pads in **pairs** (proposed characters `T` = pair 1, `U` = pair 2 — the Technical
   Analyst confirms they clash with nothing): a turtle whose step ends on a pad is moved at once to its twin,
   **keeping its heading**; it can then walk off. A new record event (e.g. `EV_TELEPORT`) so the playback shows it
   (a fade / flash at both pads), and the pen does **not** draw a line across the board. A pack that gives a pad
   without its twin is refused by `parse_pack` with a clear reason. Sensors see a pad as free floor (`FRONT ()` 0;
   optionally 5 "a teleporter" — TA's call, documented either way).
3. **Mechanic C — drawings in colour.** A drawing level may say `draw = color` (or `draw = 2`): the figure must be
   drawn **with the solution's colours** — the comparison checks each sampled point's pen colour too (`Seg.color`
   already exists). The target figure is shown in light tints of its colours; the "not quite" message says when
   the shape is right but a colour is wrong (*"The right figure, but not the right colours."*). `draw = 1` levels
   are unchanged (any colour).
4. **Two new packs, ~20 levels**, bilingual (`title.fr`, `text.fr`, `hint.fr`), each with `concept`, `words`,
   `par`, `solution` (and the existing 27 levels untouched):
   - **`4-gems-and-portals.turtle`** — *4. Gems and portals* / *4. Gemmes et portails* (~10): gems in order on a
     line, then around corners, then with a counter variable and `GEM ()`; a gem behind a door; teleporters: a
     first jump, a chain of two pairs, a maze where the portal is the short way, gems + portals + `WHILE`, one
     "grand finale" combining keys, gems and portals.
   - **`5-spirals-and-fractals.turtle`** — *5. Spirals and fractals* / *5. Spirales et fractales* (~10, drawing
     levels): a polygon of any side count with `SUB Polygon (sides, size)`; a polygon spiral (the angle not 90°);
     a rainbow spiral with `COLOR` from the loop's counter (`draw = color`); a flower of polygons; a function that
     returns a value (`FUNCTION Half (x)`); then **recursion**: a branch-tree (depth 3-4), the **Koch curve**, the
     **Koch snowflake**, the **Sierpinski triangle**, an H-tree or a recursive square spiral. Sizes chosen so the
     smallest segment stays ≥ 0.5 square on the 24 × 18 board (the comparison's 0.15 tolerance) and the run stays
     well under `MAX_EVENTS`.
5. **Lesson cards** (EN + FR, in `world.h`'s `CONCEPTS`) for the new ideas, shown the first time: `gems` (order,
   counting), `teleport`, `color` (colours as numbers, `COLOR i`), `params` (a SUB that takes values), `function`
   (a FUNCTION that gives a value back), `recursion` (a SUB that calls itself, with a smaller size, and a depth
   that stops it). Word help (`word_help`) for `GEM`.
6. **The level editor follows**: its palette (`ToolPal`, `TOOL_CH`) gains the **gem** tool (a click on a gem
   cycles 1…9) and the **teleporter** tools (pair 1, pair 2); the **Drawing** setting becomes three-way (no /
   shape / shape and colours); `CONCEPT_KEYS` gains the new concepts. `write_pack` writes the new keys and
   characters back and `parse_pack` reads them (round trip).
7. **English + French**: every new message, lesson, word and level text in both languages, the French word names
   (`GEMME`) beside the English ones; the app keeps its own `L2 (en, fr)` mechanism (its words are not `TR ()`
   ones — consistent with the rest of `main.cpp`). French screenshot checked (the words fit).
8. **Tests**: `tools/tests/turtle/turtletest.cpp` extended — all 5 packs' solutions win with 3 stars (it already
   loops over every pack given); new cases: a gem picked out of order fails at its line with the message; a
   teleport moves the turtle to the twin with its heading kept and draws no line across; a pad without a twin is
   refused; a colour drawing with the right shape and a wrong colour loses with its message, a `draw = 1` level
   still accepts any colour; a recursive solution runs (depth reached, no "too deep recursion"); the new packs
   written back and read again the same. `sh tools/tests/run_turtle_test.sh` passes; `run_basic_test.sh`
   unchanged and passing.
9. **Simulator & docs**: `shots.sh turtle` gains shots — a gems-and-portals level run step by step (a teleport
   visible) and a fractal (the Koch snowflake or the tree) drawn and won, plus one in French (new fixtures in
   `tools/tests/desktop_sim/turtle/*.ini`); the existing three shots unchanged. `docs/04-USER-GUIDE.md` §13 (the
   map characters, `GEM ()`, the teleporters, `draw = color`, the 5 packs / ~47 levels, the editor's new tools),
   `docs/03` *Turtle Quest* if its code notes change, `docs/HANDOFF.md`, `IDEAS.md`'s row (what is done),
   `python docs/build_docs.py`. The package `[turtle]` in `tools/pkg/packages.ini` covers `levels/*`
   (check it ships the new files) — **declared, not published** (PIPELINE §0.5).

### SHOULD (if time allows, in this order)

- **Your best program**: the smallest instruction count reached kept per level and player in `progress.ini`
  (e.g. `<id>.best = 6`) and shown under the count ("Your best: 6 — 3 stars at 5"): makes the shortest-program
  challenge visible on levels already won.
- **Stars per pack**: the pack drop-down / list header shows the stars won out of the pack's total (★ 18 / 30).
- **Progress and packs on FileKit's `fk_kv`** (HANDOFF's follow-up from round 2, kits first): replace `main.cpp`'s
  private `kv_*` store by `fk_kv` — only if the existing `progress.ini` files read back unchanged (a test with
  the `desktop_sim/turtle/*.ini` fixtures). Moving `parse_pack` onto `fk_kv` too only if it stays risk-free.
- A free-drawing **sandbox** level (no goal, no stars: draw anything) at the end of pack 5.

### Deferred (honestly sized)

| Item | Later as | Why not now |
|---|---|---|
| **Several turtles** (a world with 2-3 turtles; `TURTLE n` or one program each) | its own round ("Turtle Quest: the team") | the engine's one-turtle world, the record, the sensors and the board all change; the language must address a turtle |
| **Daily challenge** (a level from the date) | a small round, after a maze generator + solver | the par must be fair: needs a solver; the generated level must be checkable in tests |
| **Real-world GPIO bonus levels** | with GPIO Lab | not testable in the simulator (PIPELINE: no hardware-only work) |
| Coloured keys / doors, switches and gates, moving obstacles | a "mechanics 2" pack | the round already brings three mechanics; more would thin the levels' quality |
| Locking levels until the previous is won (Circuits' `isOpen`) | if the user wants it | Turtle Quest's design keeps all levels open (docs/04 §13); a change of behaviour the user should decide |
| Moving the UI words to `TR ()` + `lang/fr.txt` (so `tools/lang/check.py turtle` covers it) | a clean-up task | the app is already bilingual by `L2`; a 60-call migration is not this round's value |
| `SAY`, sounds, a shared star glyph in UIKit | later | nice-to-have, off the round's theme |

## Acceptance (for the Product Analyst to refine)

- `sh tools/tests/run_turtle_test.sh` passes with the 5 packs (~47 levels each won with 3 stars by its solution)
  and the new negative cases (gem order, unpaired pad, wrong colour).
- In the simulator (`shots.sh turtle`): a pack-4 level shows numbered gems and two teleporters; stepping (F8)
  shows the turtle jumping between pads; a pack-5 fractal is drawn over its grey target and wins with stars; the
  French shot shows the new words (`GEMME`), the lesson cards and the level texts in French, fitting.
- The level editor places gems (cycling numbers) and teleporters, sets "shape and colours", saves to
  `SD:/docs/turtle/my-levels.turtle`, and the saved pack reads back identically.
- The 27 existing levels, their screenshots and the existing `progress.ini` files behave as before.

## Existing code to build on

- **Engine** — `user/Apps/turtle/world.h`: `WORDS` / `FR_NAME` / `ALIASES_FR` / `SHOWN_FR` / `word_help` (add
  `GEM`), `CONCEPTS` (new cards), `Level` (`draw` becomes a mode), `parse_pack` / `write_pack`, `World::reset`
  (count gems, pair pads), `World::won`, `apply` + the `EV_*` record (teleport), `Recorder::move` (land on a pad)
  and `ext` (`W_PICK` order, `W_GEM`), `same_drawing` / `covered` (colour mode), `run_program` (messages).
- **Window** — `user/Apps/turtle/main.cpp`: `Board::tile` (draw gems and pads), the playback (`ev_ms`,
  `playback_tick`: the teleport's timing), `Board` target drawing (tinted colours), `ToolPal` / `TOOL_CH` /
  `edit_cell`, the editor's Drawing checkbox → a `Dropdown` or two checkboxes, `CONCEPT_KEYS`, `WordBar` (the
  `GEM` word in the palette when the level knows it).
- **Onyx BASIC** — `user/Libs/basic` (SUB / FUNCTION parameters, recursion; no change expected).
- **Tests / simulator** — `tools/tests/turtle/turtletest.cpp`, `tools/tests/run_turtle_test.sh`,
  `tools/tests/desktop_sim/shots.sh` (`turtle` scenario), `tools/tests/desktop_sim/turtle/*.ini`.
- **FileKit** `filekit/kvtext.h` (`fk_kv_*`, from round 2) — for the SHOULD migration of the progress store.

No kapi change, no kernel change, no new kit expected.
