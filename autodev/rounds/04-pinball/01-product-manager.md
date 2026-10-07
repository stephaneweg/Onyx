# AutoDev round 4 — Product Manager

Date: 2026-10-06. Round 4 (rounds 1-3 built Notes + Stickies, Circuits, Turtle Quest: more missions; all awaiting
the user's validation).

## Inputs read

- `autodev/QUEUE.md` — 1. Circuits (**done**, round 2), 2. Turtle Quest: more missions (**done**, round 3),
  **3. Pinball**, 4. Lemmings-like. Item 3 is the first not done: **it is the pick**; this document frames its scope.
- `autodev/STATE.md` — 3 of 5 rounds done; nothing on a pinball yet.
- `IDEAS.md`, row *Pinball — flipper*: a physics pinball (bounces, gravity, bumpers, ramps, targets, multiball),
  several tables, scores, gamepad and keyboard; **a table described as data so others can be made**.
- `docs/04-USER-GUIDE.md` §12 (games): Pong, Invaders, Arkanoid (BASIC), Tetris, Snake, 2048… — no physics game
  with flippers; no game keeps a high-score table on the card (Invaders' `hiscore` lives only in memory).
- `user/Apps/` (no `pinball`), the shared game code `user/Apps/games/game.h`, `user/Apps/invaders/main.cpp`,
  `user/Include/gamepad.h`, `user/Kits/filekit/kvtext.h` (`fk_kv_*`), the simulator
  `tools/tests/desktop_sim/{shots.sh,fakekapi.cpp}`.

## Candidates and scores (1 = poor, 5 = best)

Value (fun / educational, the user's priorities first) · Base (what exists to build on) · Feasibility (one round,
result visible in the PC simulator) · Risk (5 = no kernel / kapi change, little risk to existing code).

| # | Candidate | Value | Base | Feasibility | Risk | **/20** |
|---|---|:-:|:-:|:-:|:-:|:-:|
| 1 | **Pinball** (queue #3): 2D physics, flippers, bumpers, slingshots, targets, lanes, a ramp, multiball, 3 tables as data, high scores, EN/FR | 4 (queue #3; a showcase game, the user asked for tables as data) | 4 (`game.h`: `GameView`/`GameRoot`, 60 Hz tick, `sfx_*` on AudioKit, `gtext`; `gamepad.h`; `fk_kv`) | 4 (physics is self-contained and testable headless; feel needs tuning) | 5 (an app only; a tiny simulator addition) | **17** |
| 2 | Lemmings-like (queue #4) | 4 | 3 (`game.h`; no pixel-terrain code) | 2 (terrain + 6 roles + levels + editor: two rounds) | 5 | **14** |
| 3 | Turtle Quest: the team (several turtles) | 3 | 4 | 2 | 4 | **13** |
| 4 | Circuits 2: sequential logic (flip-flops, clocks, timing charts) | 4 | 4 | 3 | 5 | **16** |
| 5 | A shared high-score table in a kit (SystemKit/FileKit) for all games | 2 | 4 | 5 | 5 | **16** |

Pinball is first by the user's order and by score; #5 is folded into it as a SHOULD (below).

## The pick: **Pinball** — folder `autodev/rounds/04-pinball/`

It is the queue's next item, a self-contained app (no kernel, no kapi change), and its core — the ball physics and
the table format — can be **checked headless on the PC** (a host test that drops balls on each table and asserts
what happens), while its look and play are shown in the simulator.

## Scope for this round

### MUST

1. **The physics** (beside the app, e.g. `user/Apps/pinball/physics.{h,cpp}`, no UIKit in it so the host test
   links it alone): a circular ball (float, fixed sub-steps per frame, e.g. 4-8 at 60 Hz, speed capped to avoid
   tunnelling), gravity along the slanted table, collisions with **segments** (walls, polylines), **circles**
   (posts, bumpers) and **arcs** (the round top); restitution and friction per element; a deterministic step
   (the same inputs give the same run — the tests depend on it).
2. **Flippers** (left / right, a third optional per table): a rotating segment with a swept-collision on the way
   up so the ball is struck, not tunnelled through; held = raised. **Plunger**: held to pull, released to launch
   (strength by hold time); auto-launch with a key tap for the simulator/gamepad.
3. **Table elements**: **pop bumpers** (kick + score + flash), **slingshots** (kicking segments above the
   flippers), **drop targets** in banks (fall when hit, bank cleared → bonus, reset), **standup targets**,
   **rollover lanes** (top lanes lit; all lit → multiplier up; flippers rotate the lit lanes), **outlanes /
   inlanes**, **one ramp or orbit** (a one-way gate + a lane that returns the ball to a flipper: a path, scored
   as a "ramp shot" — a real 3D ramp is not required), **a kicker/saucer** that holds the ball and spits it.
4. **Rules**: 3 balls per game, ball save for the first few seconds, end-of-ball bonus × multiplier, extra ball
   from a goal, **multiball** (2-3 balls) started by a table goal (e.g. lock the ball in the saucer twice), a
   **tilt**: nudge key (shakes the ball a bit); too many nudges → tilt (flippers dead for that ball).
5. **Tables as data**: a text format read by FileKit's `fk_kv` (`[table]`, `[wall]`, `[bumper]`, `[target]`,
   `[lane]`, `[flipper]`, `[ramp]`, `[rule]` blocks with coordinates in table units, colours, scores; `name` and
   `name.fr`), in `sdcard/apps/pinball.app/tables/*.table`; a bad file is refused with its line and reason.
   **3 tables**, each with its own layout and goal (e.g. *Space Station* — the starter, *Haunted Manor* — drop
   targets + saucer multiball, *Volcano* — the ramp and lanes, faster). Players' tables in
   `SD:/docs/pinball/*.table` listed too.
6. **Drawing**: a `GameView` (`games/game.h`), the whole playfield scaled to fit the window in portrait (a
   fixed-size window, e.g. 480 × 720 or as the UX designer decides), elements drawn with UIKit's `Canvas`
   (circles, polygons, gradients), lit/unlit lamps, a score panel (score, ball, multiplier, high score, messages
   such as "MULTIBALL!" / "TILT"); a table picker at start; pause. The frame loop is `GameRoot::onTick`.
7. **Controls**: keyboard — **Left/Right Shift** or **Z / /** (flippers), **Space / Down** (plunger),
   **N** or **Up** (nudge), **P** pause, **Esc** menu; held keys through `kapi_key_held` (as Invaders).
   Gamepad (`gamepad.h`): **L / R** (and d-pad left / face B) flippers, **A** plunger, **Y** nudge, **Start** pause.
8. **High scores**: a top-5 per table with the player's name (or initials), in
   `SD:/apps/pinball.app/scores.ini` written with `fk_kv`; shown on the table picker and at game over.
9. **Sounds**: `sfx_*` (game.h → AudioKit): bumper, flipper, target, lane, drain, multiball / extra-ball jingles;
   **M** mutes.
10. **English + French**: UI words in `TR ()`, `sdcard/apps/pinball.app/lang/fr.txt`, the tables' names/goals
    with `.fr` keys; `python tools/lang/check.py pinball` at 0 missing; the French shot checked.
11. **Tests**: a host test `tools/tests/pinball/pinballtest.cpp` + `tools/tests/run_pinball_test.sh`: the 3 tables
    parse; a ball dropped from rest drains between the flippers (no stuck, no tunnelling through walls in N random
    launches — the ball always stays inside the table outline); a raised flipper sends the ball up; a bumper hit
    scores and kicks; a drop-target bank clears; multiball starts and ends; tilt; a bad table file is refused
    with its line. Deterministic seeds.
12. **Simulator & docs**: the simulator gains a way to **hold a key** (`fakekapi.cpp`'s `key_held` returns 0
    today: add `hold <key>` / `release <key>` script commands — a test-only change); `shots.sh pinball` (the
    picker, a table in play with lamps lit, a multiball, the French picker); the app in `user/Makefile`, its
    `sdcard/apps/pinball.app` (icon, `app.txt`, tables, lang); `docs/04` §12 (controls, rules, files, the table
    format), `docs/03` if relevant, `docs/HANDOFF.md`, `IDEAS.md`'s row, `python docs/build_docs.py`; the package
    `[pinball]` declared in `tools/pkg/packages.ini` — **declared, not published** (PIPELINE §0.5).

### SHOULD (in this order)

- **A table viewer/checker mode** (`pinball --check file.table` or a menu "Check a table…") showing the outlines
  and the elements' ids, so a player can make a table in a text editor — the IDEAS row's "create others".
- A shared **high-score helper** in a kit (e.g. SystemKit or FileKit `fk_scores_*`) if it stays small; else beside
  the app (kits-first rule: only move it when a second game uses it).
- An attract mode (lamps cycling) on the picker; the table's skill shot (plunger strength into a lit lane).

### Deferred

| Item | Why not now |
|---|---|
| A graphical table editor | a round of its own; the text format + the checker come first |
| True 3D ramps / habitrails, spinners, magnets, moving toys | physics and drawing well beyond one round |
| Scrolling tall tables, a dot-matrix display with animations | polish after the base plays well on the Pi |
| Several players taking turns | small, but after the single-player rules are solid |
| Tuning the feel on the real Pi (frame rate, gamepad latency) | needs hardware: the user's validation |

## Acceptance (for the Product Analyst to refine)

- `sh tools/tests/run_pinball_test.sh` passes (the cases of MUST 11), deterministic.
- `shots.sh pinball`: the picker lists 3 tables with high scores; a table in play shows the ball, raised flipper,
  lit lamps and the score; a multiball shot shows 2+ balls; the French picker fits.
- Every table parses; a broken table is refused with its line; `check.py pinball` reports 0 missing.
- No kernel / kapi change; the other games and their screenshots unchanged.

## Existing code to build on

- **`user/Apps/games/game.h`** — `GameView` (paint, mouse edges, `key`, `tick (dt)` at ~60 Hz), `GameRoot`
  (ticks the view, routes keys), `sfx` / `sfx_later` / `sfx_win` / `sfx_lose` / `sfx_set_mute` on AudioKit's FM
  voices, `rng*`, `gtext*`, `gitoa`. **`user/Apps/invaders/main.cpp`** (378 lines) is the model of an action game:
  `kapi_key_held` polled in `tick`, a HUD, states (title / play / over), drawn with UIKit's `Canvas`.
- **`user/Include/gamepad.h`** — `pad_buttons (-1)` → `PAD_L/R/A/Y/START…` (mapped via `SD:/etc/gamepad.ini`).
- **FileKit `filekit/kvtext.h`** — `fk_kv_load / parse / section / block / value / line` (round 2's Circuits uses it
  for its levels and progress: `user/Apps/circuits/circuit.cpp`) — tables and scores.
- **UIKit** `uk_lang_init`, `TR ()` (`uikit/lang.h`), `Canvas` drawing; `tools/lang/check.py`.
- **Simulator** — `tools/tests/desktop_sim/shots.sh` (the `invaders` line ~279, the `circuits` scenario ~379),
  `fakekapi.cpp` (script commands ~588-646; `key_held` stub at ~1311 to extend), `pad_state_sim`.
- **Tests pattern** — `tools/tests/turtle/turtletest.cpp`, `run_turtle_test.sh`, Circuits' host test.

No kapi change, no kernel change, no new kit required.
