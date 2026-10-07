# AutoDev round 2 — Product Manager

Date: 2026-10-06. Round 2 (round 1 built Notes + Stickies, awaiting the user's validation).

## Inputs read

- `autodev/QUEUE.md` — the user's queue: **1. Circuits** (the user's priority), 2. Turtle Quest: more
  missions, 3. Pinball, 4. Lemmings-like. None is done yet (`autodev/STATE.md`'s history: Notes only).
- `IDEAS.md`, Applications table, the *Circuits* row (a logic-circuit simulator with missions: truth table /
  timing chart objectives, allowed parts, target gate count → 1-3 stars, automatic checking of every input,
  step-by-step simulation, parts unlocked level after level, your own circuits reused as chips, a level
  editor, saved progress, FR / EN; *later*: export to GPIO Lab).
- `docs/HANDOFF.md`, *Turtle Quest* (done 2026-10-06: engine `world.h` without UI + window `main.cpp`,
  packs as text files with `.fr` keys, stars by count, lessons per new idea, progress in `progress.ini`,
  a host test checking every level's solution, `shots.sh turtle`).
- `user/Apps/turtle/` (`world.h` 765 lines, `main.cpp` 1508 lines), `sdcard/apps/turtle.app/levels/*.turtle`,
  `docs/04-USER-GUIDE.md` §12 (catalog) and §13 (*Turtle Quest*), `docs/06-KITS-GUIDE.md` (language),
  `docs/11-UIKIT.md`, `user/Kits/uikit/lang.h` (`TR()`, `uk_lang_init`), `user/Apps/games/game.h`,
  `user/Apps/gpiolab/main.cpp` (its timing chart).

Existence check: no logic-circuit code anywhere in `user/` (`grep -i "truth table\|logic gate"`: nothing).

## Candidates and scores (1 = poor, 5 = best)

Value (educational / everyday, the user's priorities first) · Base (what exists to build on) ·
Feasibility (one round, result visible in the PC simulator) · Risk (5 = no kernel / kapi change).

| # | Candidate | Value | Base | Feasibility | Risk | **/20** |
|---|---|:-:|:-:|:-:|:-:|:-:|
| 1 | **Circuits** (queue #1) | 5 (the user's stated priority; educational, the pair of Turtle Quest) | 4 (Turtle Quest's whole structure, UIKit, `VPath` for gate shapes, `TR()`) | 4 (combinational version: yes; sequential + chips + editor: no) | 5 (an app only) | **18** |
| 2 | Turtle Quest: more missions (queue #2) | 4 | 5 (the engine exists) | 4 (mechanics like teleporters / several turtles touch `world.h` and its test) | 5 | **18** |
| 3 | Pinball (queue #3) | 3 | 3 (`game.h`'s GameView, tick, sfx) | 3 (physics tuning is long, judged by feel, hard to assert in tests) | 5 | **14** |
| 4 | Lemmings-like (queue #4) | 3 | 3 (`game.h`) | 2 (pixel terrain + 6 roles + levels + editor: two rounds) | 5 | **13** |
| 5 | Clock: alarms, timer, stopwatch, world clocks (free pick, round 1's runner-up) | 4 | 4 | 3 (alarms while closed need a resident piece) | 5 | **16** |

## The pick: **Circuits** — folder `autodev/rounds/02-circuits/`

The queue imposes it (first item not done) and it also scores best: the user's priority, purely an app
(no kapi, no kernel), fully testable on the PC (the engine by a host test, the window by `shots.sh`), and
Turtle Quest gives a proven template for missions, packs, stars, progress and the two languages. Turtle
Quest "more missions" ties on points but comes second in the user's order — and can later reuse what
Circuits adds.

## Scope for this round — a shippable first version: **combinational logic**

The full IDEAS row is three rounds of work. This round delivers a complete, polished game on
combinational circuits; the engine is designed so the deferred pieces fit later without a rewrite.

### MUST (the round is not done without these)

1. **App `circuits`** (`user/Apps/circuits/`, `sdcard/apps/circuits.app/`, category *Programming* beside
   Turtle Quest, an icon), split as Turtle Quest: **`circuit.h`/`.cpp` = the engine without UI** (parts,
   wires, evaluation, checking, packs) and **`main.cpp` = the window**. MIT notices.
2. **The editor on a grid canvas** (a custom UIKit `Widget`): parts placed from a palette by click or
   drag, snapped to the grid; wires drawn from an output pin to an input pin (straight or orthogonal
   segments), an output may feed several inputs, an input takes one wire; select, move, delete (Del),
   a wire deleted by clicking it; **undo/redo** (Ctrl+Z / Ctrl+Y). Gates drawn as the classic shapes
   (`VPath`), wires coloured by their level (0 dark, 1 lit).
3. **Parts**: input switch (click to toggle), output lamp, **NOT, AND, OR, XOR, NAND, NOR** (2 inputs).
   The level's inputs/outputs are fixed and named (A, B, Cin → S, Cout). A combinational loop is refused
   with a clear message (no state this round).
4. **Simulation**: *live* (toggle the switches, the lamps and wires follow) and **step by step** (F8: one
   gate depth per step — the signal visibly travels; F9 reset), as Turtle Quest's F5/F8/F7/F9 keys.
5. **Missions**: **3 worlds, ~20 levels** in text packs `sdcard/apps/circuits.app/levels/*.circuits`
   (Turtle's `[pack]` / `[level]` key = value format, `.fr` keys), e.g. *1. Gates* (wire, NOT, AND, OR,
   NAND, NOR, the gates from NAND only), *2. Combining* (XOR from AND/OR/NOT, XOR from NAND, equality,
   majority, 2:1 multiplexer, enable), *3. Arithmetic* (half adder, full adder, 2-bit adder, 2→4 decoder,
   a comparator). Each level: title, text, hint, the **objective as a truth table**, the **allowed parts**
   (the palette — parts unlocked level after level), `par` (gate counts for 3 and 2 stars), a reference
   solution (for the test). A short lesson card the first time a new gate appears.
6. **Automatic checking** ("Check", F5): every input combination is evaluated; the truth table panel
   shows expected vs. obtained, the **wrong rows marked**; won → **1-3 stars by gate count**.
7. **Saved progress**: stars and the player's circuit per level in `SD:/apps/circuits.app/progress.ini`
   (as Turtle Quest; one player is enough — several players is SHOULD), the levels list with their stars,
   next level unlocked when one is won.
8. **English + French**: the UI words through UIKit's **`TR()`** + `sdcard/apps/circuits.app/lang/fr.txt`
   (checked with `tools/lang/check.py circuits`) — the newer mechanism, rather than Turtle's `L2()`; the
   levels' texts through their `.fr` keys.
9. **Tests and docs**: host test `tools/tests/circuits/circuitstest.cpp` + `tools/tests/run_circuits_test.sh`
   (every level's reference solution wins with 3 stars; a wrong circuit fails on the expected row; a loop
   is refused; a pack and a saved circuit read back); `shots.sh circuits` scenario (a level being solved,
   a failed check with its wrong row, the French UI) → screenshots; `user/Makefile`; docs/04 §12 catalog
   row + a section like §13's *Turtle Quest*; `docs/HANDOFF.md`; `python docs/build_docs.py`.

### SHOULD (if time allows, in this order)

- **Solved circuits as parts** — the cheap form of "your own circuits as chips": once *Half adder* is won,
  a *HALF ADD* part appears in the palette of later levels (a black box evaluated by the engine from the
  player's own circuit, counted as its gates or as one — a pack setting). It gives the half adder →
  full adder → 2-bit adder progression without a chip designer.
- A **sandbox** (free play, all parts, no objective, save/open a `.circuit` file).
- Several players (Turtle Quest's player list), 3-input AND/OR, sounds on win.

### Deferred to later rounds (honestly sized)

| Item | Fits a later single round? | Why not now |
|---|---|---|
| **Sequential logic**: clock, SR / D flip-flops, counters, **timing-chart objectives** | Yes — one round of its own ("Circuits 2") | needs state, feedback loops allowed, time steps, a checker over sequences and a chart widget (GPIO Lab's chart is a model) |
| **Custom chips designer** (name the pins, a chip library, chips inside chips, ALU) | Yes, with sequential or just after | the engine's sub-circuits come in MUST's design; the designer UI and file format are a round's worth |
| **Level editor** (draw the objective's truth table, choose the parts and par, test with a solution) | Yes, small — pairs well with the sandbox | not needed for a first playable version; Turtle Quest's editor is the model |
| Bigger parts: multiplexer / decoder as parts, 7-segment display, buses | With sequential | needs multi-bit pins; the 7-segment is most fun with a counter |
| **Export to GPIO Lab** (wire it for real) | Doubtful as a round: needs the Pi to see anything | not testable in the simulator; last |

## Existing code to build on

- **Turtle Quest** — `user/Apps/turtle/world.h` (the pack parser: `[pack]`/`[level]`, `key.fr`, `|` multi-line
  values; `Arr<T>`; stars by `par`), `user/Apps/turtle/main.cpp` (the levels list with stars, the message bar
  drawing stars, the lesson card, `progress.ini` key/value store, F5/F8/F7/F9 keys, the step playback pace),
  `sdcard/apps/turtle.app/levels/*.turtle`, `sdcard/apps/turtle.app/app.txt`.
- **Tests / simulator** — `tools/tests/turtle/turtletest.cpp` + `tools/tests/run_turtle_test.sh` (host test
  model), `tools/tests/desktop_sim/shots.sh` (the `turtle` build line and progress fixture, ~lines 130, 344).
- **UIKit** (`docs/11-UIKIT.md`) — `uikit/widget.h` (custom canvas widget), `uikit/vpaint.h` `VPath`
  (anti-aliased gate shapes), `uikit/canvas.h`, `uikit/toolbar.h` (`ToolBar`, `ToolButton`: the palette),
  `uikit/splitter.h`, `uikit/listbox.h` / `uikit/treeview.h` (levels), `uikit/datagrid.h` (the truth table,
  or drawn by hand), `uikit/dialog.h` (`MessageBox`, `FileDialog`), `uikit/text.h` (`TextFace`),
  **`uikit/lang.h`** (`TR`, `TRC`, `uk_lang_init`) + `tools/lang/check.py`, catalogues as
  `sdcard/apps/ledger.app/lang/fr.txt`.
- **AppKit** — `app_ini_load_path` / `app_ini_get` (reading `.ini`), the app's folder; **SystemKit**
  `locale.h` (`locale_language`); **FileKit** `filekit/fsutil.h` (writing files).
- Later: `user/Apps/gpiolab/main.cpp` (timing chart, GPIOKit) for sequential logic and the GPIO export;
  `user/Apps/games/game.h` (sfx) for sounds.

Kits-first note: the progress key/value store and the pack parser are now needed by two apps (Turtle,
Circuits). The Technical Analyst should decide whether a small shared piece belongs in a kit (e.g. FileKit /
AppKit `.ini` writing) rather than a third copy — without touching Turtle Quest's behaviour this round.

No kapi change, no kernel change, no new kit expected.
