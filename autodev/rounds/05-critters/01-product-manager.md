# AutoDev round 5 — Product Manager

Date: 2026-10-07. Round 5, the last of `rounds_max: 5` (rounds 1-4 built Notes + Stickies, Circuits, Turtle Quest:
more missions, Pinball; all awaiting the user's validation).

## Inputs read

- `autodev/QUEUE.md` — 1. Circuits (**done**, round 2), 2. Turtle Quest: more missions (**done**, round 3),
  3. Pinball (**done**, round 4), **4. Lemmings-like**. Item 4 is the first not done: **it is the pick**; this
  document frames its scope.
- `autodev/STATE.md` — 4 of 5 rounds done; nothing on a creatures/terrain game yet.
- `IDEAS.md`, row *Lemmings-like*: guide creatures to the exit by giving them roles (dig, block, build, climb,
  float, explode); pixel-destructible terrain; levels with objectives (n saved out of m); a level editor; **our own
  name and graphics** (Lemmings is a trademark: no names, sprites, levels or sounds from it).
- `docs/04-USER-GUIDE.md` §12 (games): Pong, Invaders, Tetris, Snake, Sokoban, Minesweeper, 2048, Solitaire,
  FreeCell, Pipes, Pinball (round 4)… — no puzzle-action game with many independent agents, no destructible terrain.
- `user/Apps/` (no such app), `user/Apps/games/game.h`, `user/Apps/pinball/*` (round 4's split: a headless core —
  `table.cpp`, `physics.cpp`, `rules.cpp`, `scores.cpp` — and the UI headers `draw.h`, `panel.h`, `picker.h`),
  `sdcard/apps/pinball.app/tables/*.table` (levels as `fk_kv` text with `[shape]` polygons), `user/Apps/circuits/
  circuit.h` (packs + `progress.ini` over `fk_kv`), `user/Kits/uikit/canvas.h`, `tools/tests/desktop_sim/
  {shots.sh,fakekapi.cpp}` (`click`/`drag`/`key`/`hold`/`release` script commands), `tools/tests/run_pinball_test.sh`.

## Candidates and scores (1 = poor, 5 = best)

Value (fun / educational, the user's priorities first) · Base (what exists to build on) · Feasibility (one round,
result visible in the PC simulator) · Risk (5 = no kernel / kapi change, little risk to existing code).

| # | Candidate | Value | Base | Feasibility | Risk | **/20** |
|---|---|:-:|:-:|:-:|:-:|:-:|
| 1 | **Lemmings-like** (queue #4): creatures on a pixel terrain, 6 roles, levels as data, n-of-m objective, EN/FR | 4 (queue #4, the user's order; a puzzle game that suits all ages) | 4 (`game.h` loop and sfx, pinball's level-as-data and core/UI split, Circuits' progress, `Canvas`, `fk_kv`) | 3 (terrain + 6 roles + 12 levels fit if the editor is deferred; the core is testable headless) | 5 (an app only) | **16** |
| 2 | Pinball: the table checker (`--check`), attract mode, skill shot (round 4's "Reste") | 2 | 5 | 5 | 5 | **17** but a follow-up, not a round's app |
| 3 | Circuits 2: sequential logic (flip-flops, clocks, timing charts) | 4 | 4 | 3 | 5 | **16** |
| 4 | Sokoban: more levels + a level editor (IDEAS row) | 2 | 4 | 5 | 5 | **16** |
| 5 | A shared high-score / progress helper in a kit (`fk_scores_*`), used by Pinball, Circuits and the new game | 2 | 4 | 5 | 4 (touches 2 shipped apps) | **15** |

Candidate 1 is the pick by the user's order (the scoring only frames it). #2 and #4 are small follow-ups for a
later session; #5 stays beside the apps until the kits-first rule calls for it (a third user would be this game:
the Technical Analyst may judge whether a small FileKit helper is worth it — SHOULD, below).

## The pick: a Lemmings-like — name to be finalised by the Product Analyst

Name ideas (plain, short, ours; none is an existing Onyx app):

1. **Critters** — plain, says "small creatures", easy in French too (*Critters* kept, or *Bestioles*). **Proposed.**
2. **Burrow** — says digging; a bit narrow (the game is also building, blocking, floating).
3. **Wanderers** — says the walking; longer.

The creatures need a name of their own in the game's words (e.g. "critters" / « bestioles »); no green hair, no
blue robes, no "Let's go!" — our own look (the UX designer draws them: e.g. small round coloured creatures).

Folder: **`autodev/rounds/05-critters/`** (the slug follows the proposed name; if the Analyst picks another name,
the folder stays — it is a working folder).

Why it fits one round: it is a self-contained app (no kernel, no kapi, no kit change required), and its core —
the terrain, the creatures' state machine, the roles, the level format, the win/lose rule — is **deterministic and
checkable headless on the PC**, the same way round 4 tested Pinball's physics; the look is shown in the simulator.
The level editor, the riskiest part in time, is deferred (a text format + a checker come first, as Pinball did).

## Scope for this round

### MUST

1. **The core, headless** (beside the app, e.g. `user/Apps/critters/{terrain,world,level}.{h,cpp}`, no UIKit in it,
   so the host test links it alone): a fixed-step simulation (e.g. 17 or 20 steps a second for the creatures, the
   drawing at 60 Hz), integer coordinates, a seeded RNG only where needed — **the same inputs (level + timed role
   assignments) give the same run**, bit for bit (the tests and a replay depend on it).
2. **The terrain**: a pixel bitmap (one byte a pixel: empty / earth / **steel** (indestructible) / water or lava (a
   hazard)), up to e.g. 1600 × 240 logical pixels; made at load from the level's shapes (below); digging, bashing
   and exploding remove earth (never steel), building adds earth. The drawing keeps a colour layer beside it (the
   earth's texture/colours from the level) updated with each change.
3. **The creatures**: up to e.g. 80 per level; enter from a **hatch** at the level's release rate; **walk** (turn at
   a wall, step up/down small slopes of a few pixels), **fall** (a fall higher than N pixels kills unless
   floating), **drown/burn** in a hazard, leave the map (die), reach an **exit** (saved). Each has its state,
   direction, animation frame, role.
4. **Six roles** given by the player (the queue's list), each with a count per level:
   - **Climber** (permanent: climbs walls instead of turning), **Floater** (permanent: falls slowly, survives any
     fall) — the two can be combined;
   - **Blocker** (stands still, turns the others back; only an explosion frees it);
   - **Builder** (lays a stair of e.g. 12 steps, then shrugs; stops at a wall);
   - **Digger** (digs straight down through earth; stops at steel or when it falls through);
   - **Exploder** (a 5-second countdown above its head, then explodes, removing a disc of earth and itself).
5. **The objective**: each level gives *n* to save out of *m*, a time limit, the counts of each role; the level is
   won when every creature is saved or dead and saved ≥ *n*; lost otherwise (or when the time runs out). The end
   screen says saved / needed / time, and **Retry** / **Next**.
6. **Controls** (mouse first, keyboard for everything): a **skill bar** at the bottom (the six roles with their
   counts, release rate − / +, pause, fast-forward, **"all explode"** (the nuke, with a confirmation)); click a
   role, then click a creature (the cursor shows which creature is under it — the nearest one in a small square;
   a creature that cannot take the role refuses it); **keys 1-6** pick the roles, **F** fast-forward, **P/Space**
   pause, **− / +** release rate, **←/→** (or the mouse at the window's edge, or a drag on the minimap) scroll,
   **Esc** the menu. A gamepad is a SHOULD.
7. **Scrolling and the minimap**: the level wider than the window scrolls horizontally; a **minimap** in the skill
   bar shows the whole terrain, the creatures as dots and the visible frame (click to jump).
8. **Levels as data**: a text format read by FileKit's `fk_kv` (as Pinball's `.table`): `[level]` (`format`,
   `name` + `name.fr`, `hint` + `hint.fr`, `size`, `count`, `save`, `time`, `rate`, the role counts), `[shape]`
   (polygons / rectangles / circles with a material and a colour — Pinball's `[shape]` idea), `[hatch]`,
   `[exit]`, `[hazard]`; a bad file is refused with its line and reason. **12 levels in 2 packs**: *Training*
   (6 levels, one role taught each, with a hint shown at start) and *Expedition* (6 levels mixing roles, harder);
   in `sdcard/apps/critters.app/levels/`. Players' levels in `SD:/docs/critters/*.level` listed too.
9. **Progress**: the levels solved and the best result per level (most saved, best time) in
   `SD:/apps/critters.app/progress.ini` written with `fk_kv` (Circuits' way); the next level opens when one is
   solved; a level picker (the packs, a tick on the solved ones) at start.
10. **Drawing**: a `GameView` (`games/game.h`), the terrain blitted from its colour layer with `Canvas`
    (`putOther` / direct pixels), scaled ×2 (or as the UX designer decides) in a fixed-size window (e.g. 640 × 480);
    the creatures as our own small sprites (a few frames per state, drawn in code or loaded as one BMP sheet via
    `uikit` / ImageKit), the hatch, the exits, an explosion's particles; a HUD (out / in / needed / time).
11. **Sounds**: `sfx_*` (game.h → AudioKit): the hatch opening, a role given, a splat, the explosion's tick and
    boom, a saved creature, win / lose jingles; **M** mutes.
12. **English + French**: the UI words in `TR ()`, `sdcard/apps/critters.app/lang/fr.txt`, the levels' names and
    hints with `.fr` keys; `python tools/lang/check.py critters` at 0 missing; the French shots checked (the skill
    bar's words must fit).
13. **Tests**: a host test `tools/tests/critters/critterstest.cpp` + `tools/tests/run_critters_test.sh`: the 12 levels
    parse; a walker turns at a wall and climbs a 3-pixel step; a high fall kills, a floater survives it; a blocker
    turns the others; a digger digs to steel and stops; a builder's stair lets the others cross a gap; an
    exploder removes earth but not steel; the win/lose rule; **each shipped level is solvable**: a recorded
    solution (a list of `step role x y` assignments, kept beside the test) replayed headless reaches the target —
    the guard against unwinnable levels; a bad level file is refused with its line. Deterministic.
14. **Simulator & docs**: `shots.sh critters` (the level picker, a level in play with the skill bar and minimap,
    a builder's stair and a digger at work, the end screen, the French picker and skill bar) — the simulator
    already has `click`, `key`, `hold`/`release` (round 4); the app in `user/Makefile`, its
    `sdcard/apps/critters.app` (icon, `app.txt` with `category = Games`, `opens = level`, the levels, lang);
    `docs/04` §12 (the rules, the roles, the controls, the files, the level format), `docs/HANDOFF.md`, `IDEAS.md`'s
    row, `python docs/build_docs.py`; the package declared in `tools/pkg/packages.ini` — **declared, not
    published** (PIPELINE §0.5).

### SHOULD (in this order)

- **Two more digging roles**: a **Basher** (digs horizontally) and a **Miner** (digs diagonally down) — the
  format and the skill bar made for 8 slots from the start, so adding them is small.
- **A level checker / viewer** (`critters --check file.level` or a menu "Check a level…"): parses, draws the
  terrain, names the elements, replays a solution file if one is beside it — the IDEAS row's "editor" in its
  minimal form, enough to make levels in a text editor.
- **Gamepad** (`gamepad.h`): the d-pad moves a cursor, **A** gives the role, **L/R** pick the role, **Start** pause.
- A **replay** of the player's best solution (the core being deterministic, a replay is the list of assignments).
- A shared progress/score helper in FileKit if the Technical Analyst finds Pinball + Circuits + this game would
  share it cleanly; else beside the app.

### Deferred

| Item | Why not now |
|---|---|
| A graphical level editor (paint terrain, place hatch/exits, set counts, test-play) | a round of its own; the text format + the checker come first (as Pinball) |
| Terrain from a picture (a BMP/PNG as the earth's layer via ImageKit) | the shapes cover the shipped levels; a picture layer after the editor |
| Traps, one-way walls, teleporters, moving parts, several hatch colours / two players | rules beyond one round |
| More level packs (30+ levels), level music | content work once the engine is validated by the user |
| Tuning speed and the feel on the real Pi | needs hardware: the user's validation |

## Acceptance (for the Product Analyst to refine)

- `sh tools/tests/run_critters_test.sh` passes (the cases of MUST 13), deterministic; every shipped level is won by
  its recorded solution and lost when nothing is done (where *n* > 0).
- `shots.sh critters`: the picker lists 2 packs / 12 levels; a level in play shows creatures walking, a role at work,
  the skill bar with counts and the minimap; the end screen shows saved / needed; the French picker and skill bar fit.
- A broken level is refused with its line; `check.py critters` reports 0 missing.
- No kernel / kapi change; the other games and their screenshots unchanged.

## Existing code to build on

- **`user/Apps/games/game.h`** — `GameView` (paint, `press`/`release`/`move` with the button state, `key`,
  `tick (dt)` ~60 Hz), `GameRoot`, `sfx` / `sfx_later` / `sfx_win` / `sfx_lose` / `sfx_set_mute`, `rng*`,
  `gtext*`, `gitoa`.
- **`user/Apps/pinball/`** — the model of this round: the headless core (`table.cpp` — the `fk_kv` level reader with
  errors at the line, `[shape]` polygons; `rules.cpp`; `scores.cpp`) apart from the UI headers (`draw.h`, `panel.h`,
  `picker.h` — a level picker with the player's files from `SD:/docs/<app>` and a refused file with its reason);
  `tools/pinball/mktables.py` (levels generated by a script — useful for terrain shapes); its tests
  `tools/tests/pinball/`, `run_pinball_test.sh`, `run_pinball_sim_test.sh`; its `[app.pinball]` block in
  `tools/pkg/packages.ini`.
- **`user/Apps/circuits/circuit.{h,cpp}`** — packs of levels, the progress file (`progress.ini` over `fk_kv`,
  `level_open`), levels unlocked one after the other, hints/lessons with `.fr` keys.
- **FileKit `filekit/kvtext.h`** — `fk_kv_load / parse / section / block / value / line`.
- **UIKit** `uikit/canvas.h` (`pixel`, `fillRect`, `putOther` with magenta transparency, `alloc`/`adopt` — the
  terrain's colour layer as an owned `Canvas` blitted each frame), `uk_lang_init`, `TR ()` (`uikit/lang.h`);
  `tools/lang/check.py`.
- **`user/Apps/invaders/main.cpp`**, **`user/Apps/minesweeper`** — the title / play / over states, the mouse-driven
  game view; **`user/Include/gamepad.h`** for the SHOULD.
- **Simulator** — `tools/tests/desktop_sim/shots.sh` (the `pinball` lines: its core's sources passed as `extra`,
  ~line 64; its scenario), `fakekapi.cpp` (`click`, `drag`, `key`, `hold`/`release`, waiting for N turns — enough to
  let creatures walk before a shot).

No kapi change, no kernel change, no new kit required.
