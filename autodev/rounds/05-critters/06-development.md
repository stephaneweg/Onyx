# AutoDev round 5 — Development: Critters

Date: 2026-10-07. Inputs: `02-product-analysis.md` (AC-1…AC-37, the formats §7), `03-technical-analysis.md` (§3 the
core, §4 the tools, §8 the plan, §9.1 the host test), `04-ux-design.md` (what concerns the core: D13's
`World::onlyBlockersLeft ()`), `05-validation.md` (the fixes and the binding notes). Branch `AutoDev`.

## Developer A — steps 1–9 (the core, the tools, the 12 levels; no UI)

Everything in the core is plain C++ (no STL, no exceptions, no RTTI, fixed arrays, `new` for the big structures),
**integers only** — no `float` / `double`, no random number, no clock, no `kapi_*`, no UIKit (the test script greps
for them, comments stripped) — compiled by the host test with `-Wall -Wextra -Werror` and run under ASan + UBSan. Every
new file carries the MIT notice. No kernel, kapi, AppKit, UIKit, kit or simulator change
(`git diff --stat origin/main -- kernel user/Kits` is empty).

### What was done, by step

| Step | Files | What |
|---|---|---|
| 1 — scaffold | `user/Apps/critters/{terrain,level,world,solution,progress}.{h,cpp}`, `tools/tests/critters/critterstest.cpp`, `tools/tests/run_critters_test.sh` | `namespace critters`; the script: the no-float / no-rng / no-clock / no-kapi / no-UIKit grep over the core (comments stripped), the test built twice (`-O1 -fsanitize=address,undefined`, then `-O2`) and **crsim built too** (so it never rots); the two builds' **determinism fingerprint** (a hash of every shipped solution's per-step checksum stream) must be equal |
| 2 — terrain | `terrain.{h,cpp}` | `Terrain`: materials + `0x00RRGGBB` colour layer, the edges (steel left / right / above, the void below), `set` (the one writer: incremental hash `XOR mix64 (i*8 + m)`, dirty rectangle), `dig_rect`, `dig_disc`, `brick` (columns `x−dir … x+4·dir`, rows `y−1 … y`, only empty pixels, light top row), `any_steel`, `take_dirty`, `full_hash`; `mix64` (splitmix64's finaliser), `mix32` (the speckle's position hash) |
| 3 — level reader | `level.{h,cpp}` | `load_level (text, Level &, LoadError &, withDiggers2)` over FileKit's `fk_kv_parse`: every block / key / default / range of 02 §7.1.2, the value readers (int without fraction, points with spaces and/or one comma, `#RRGGBB`, words, texts with `.fr` and a character limit in UTF-8), the limits ("too many shapes / points / labels (max n)"), the cross-checks (1–4 hatches / exits on `[level]`'s line, a hatch / exit in a non-empty pixel via `material_at` — no terrain built), `basher` / `miner` > 0 refused unless `withDiggers2`; a refused level is cleared; `LoadError` = `line`, `reason` (English, compared by the tests), `fmt` + `arg[2]` (for `TR (e.fmt)`); **every reason and every "too many" word in `// TR:` comments** (`level.h`, `solution.h`: 19 words — `check.py critters` already sees them) |
| 4 — terrain building | `level.cpp` | `build_terrain`: shapes in file order, clipped; rectangles; **polygons by scanlines in doubled integer coordinates** (vertices even, pixel centres odd → never on the line; the first column right of each crossing by exact floor division in `int64`) = the even-odd rule at pixel centres; circles `dx²+dy² ≤ r²`; `erase`; textures (speckle `mix32 % 6`, stripes `y % 4 == 3`, bricks 8×4 mortar), `colour2` default = colour − ¼, **the steel plates' rivets** (steel + bricks: a lighter pixel at each plate's corner — colours only, materials untouched); `material_at` uses the same crossing code, so the two always agree |
| 5 — world, walking / falling | `world.{h,cpp}` | `World::reset / tick` in 03 §3.3's order: release (first at step 40, `interval_for`, hatches in turn, a rate change from the next release), the nuke's next fuse, each creature in release order (fuse → state → void / hazards / exits), the end (all out and none in play, or the clock: those in play lost). `S_WALK` (blockers ahead, step up ≤ 6 to the first empty pixel, else a wall; down ≤ 3, deeper a fall), `S_FALL` (pixel by pixel, 3 px a step, a floater 1 px once 12 px fallen, stops above a hazard; > 60 px kills), exits (`|dx| ≤ 2`, `|dy| ≤ 4`, 8 steps), dying states 16 steps; `checksum ()` over explicit fields (no struct bytes); the **constants in one `enum`** at the top of `world.h`; `Clock::due` (thirds of a ms, 150 / 50 a step, cap 4 / 12, the rest dropped; paused → 0) |
| 6 — the roles | `world.cpp` | climber (climbs 1 px a step, lets go and turns under something solid at `y−12` or at a blocker, steps over the top), floater, blocker, builder (a brick every 8 steps, the head check at `x+3·dir`, rows `y−9 … y−4`, 3 px forward / 2 up, the last three warn, shrug 10), digger (a 9-px row every 2 steps; steel → walk, no earth → fall), exploder (100 steps, `E_TICK` 5…1, the disc r 12 at `(x, y−4)`, earth only), `can_take` / `assign` (02 #16's refusals: `NO_COUNT`, `NOT_ALIVE`, `LEAVING`, `ALREADY`, `NOT_ON_GROUND`, `IS_BLOCKER`, `COUNTING_DOWN`), `pick` (the box `x±4, y−11…y`: the first in release order that can take the role, else the nearest), `nuke` (one more fuse a step, no exploder count taken), `set_rate` (clamped to [level's rate, 99]); **`onlyBlockersLeft ()`** for 04 D13 (all out, not nuking, every creature in play a blocker without a fuse), `alive (i)`, `timeLeft ()` |
| 7 — solutions / replay | `solution.{h,cpp}` | `load_solution` (`#` ends a line, blank lines, equal steps allowed; "bad step" / "bad action" / "bad creature" / "too many actions (max 1024)" with the line), `Replay::apply_due` (refusals counted; `pause` / `fast` ignored), `Recorder` (`add`, `text` → the same `.sol` form), `run_solution (lv, sol, perStep, u, World *w = 0)` → `RunResult` (result, saved, needed, end step, refused, last checksum, `hashOk` = incremental hash equal to the full recount) |
| 8 — progress | `progress.{h,cpp}` | over an `fk_kv` document (the window loads / saves with `FK_KV_ESCAPES`): `progress_section` (`training-01-…` / `user.<base>`, `.level` dropped case-blind), `progress_get` (a line that does not read = absent), `progress_won` (solved = 1, saved = max, time = min, each on its own → true only when it beats an earlier win: "New best!"), `progress_open` (the chain), `progress_setting` / `progress_set_setting` (`[settings]`). No Critters type in the API (it can move to FileKit, 03 §2.2) |
| 9 — tools and levels | `tools/critters/crsim.cpp`, `tools/critters/mklevels.py`, `sdcard/apps/critters.app/levels/*.level` (12), `tools/tests/critters/solutions/*.sol` (12), `tools/tests/critters/example.level` (02 §7.1.4 verbatim) | **crsim**: run to the end (`won|lost saved/needed m:ss step N refused N`), `--trace` (events + every change of state / direction), `--at STEP --ppm F` (the terrain ×2 with hatches, exits, numbered creatures coloured by role), `--where STEP`, `--search LINE A B` (results by ranges of steps), `--no ROLE` (AC-31 by hand), `--check LEVEL…`. **mklevels.py**: the palettes and names of the UX draft, the geometry drawn again (helpers `box`, `ground` with flat spans for exits / gaps, `steel`, `water`, `lava`, `hole`); every level has `name.fr`, `hint.fr`, labels with `text.fr`; the output is deterministic (a re-run leaves `git status` clean) |

### The 12 levels (as built; see the deviations for what changed from 02 §6's targets)

| Level | Size | Count / save | Time | Roles | The puzzle | Solution (`.sol`) | Result |
|---|---|---|---|---|---|---|---|
| T1 Straight Down | 400 | 10 / 8 | 3:00 | digger 3 | a floor closed by two steel walls over a cave with the exit | 1 digger | 10/8 0:34 |
| T2 Mind the Gap | 480 | 10 / 8 (rate 30) | 3:00 | builder 4 | a 16-px ravine with lava | 1 builder at the edge | 9/8 0:36 |
| T3 Hold the Line | 480 | 10 / 6 | 3:00 | blocker 2, digger 1 | the hatch faces a lake; a blocker turns them back over a thin crust; dig to the cave with the exit | blocker, digger, nuke | 9/6 0:40 |
| T4 Up the Wall | 480 | 10 / 10 | 3:00 | climber 10 | an 80-px steel wall, a platform, a drop | 10 climbers | 10/10 0:36 |
| T5 Soft Landing | 400 | 10 / 10 | 3:00 | floater 10 | a ledge 100 px above the exit | 10 floaters | 10/10 0:32 |
| T6 Blast Through | 480 | 10 / 9 | 3:00 | exploder 2 | a closed pen with a 6-px floor over a cave that runs under the wall up to the exit (one burst anywhere in the pen opens it) | 1 exploder | 9/9 0:49 |
| E1 Two Ways | 800 | 20 / 16 (rate 40) | 4:00 | builder 4, digger 2, blocker 2 | left hatch faces lava (block it), then a gap (build); right stream on a shelf behind a steel post (dig) | blocker, digger, builder, nuke | 19/16 1:05 |
| E2 Steel Floor | 800 | 20 / 15 | 4:00 | digger 3, builder 3, exploder 2 | a floor over steel with one gap in the steel; a lava trench before the exit | digger, builder | 18/15 1:21 |
| E3 The Climb | 960 | 16 / 12 | 5:00 | climber 14, floater 14, builder 2 | a steel-faced ridge, a 100-px cliff behind, a pond | 14 climber + floater, builder, nuke | 13/12 1:25 |
| E4 Lava Lake | 1120 | 30 / 24 (rate 1) | 5:00 | builder 8, blocker 2, digger 1 | a lava lake with 3 islands (4 gaps), the exit just past the lake, a lava pool farther on | 4 builders, blocker, nuke | 26/24 2:10 |
| E5 The Maze | 1280 | 40 / 30 | 6:00 | 2 of each of the six | dig from the hatch's chamber into a tunnel, bridge its pool, block + explode the earth plug (the stretch after the pool is a pen: a 10-px step back), build up to the exit's ledge | digger, builder, blocker + exploder, builder | 37/30 2:03 |
| E6 Grand Tour | 1600 | 60 / 50 (rate 70) | 7:00 | climber 4, floater 4, blocker 2, builder 10, digger 4, exploder 3 | a pool (build), a pen closed by a steel wall, a crust over a cave with exit 1 (dig); climbers that float go over the wall to exit 2 | builder, 4 climber + floater, digger | 58/50 1:37 |

Each level was checked with crsim at every stage: lost when nothing is done; each Training level lost without its role
(the host test does both, AC-31); each solution line's **winning window** measured (the other lines fixed) and the line
put inside it — e.g. T1's digger 55…355, T2's builder 176…197, E2's builder 557…582, E4's builders ≈ ±7 steps (the
tightest: the leading creature at each gap must start building before the next one catches up), E5's blocker at the plug
681…694. Every solution that leaves a blocker or stragglers ends with `nuke` (03 fact 7): the test demands the end
**before** the time limit.

### Commits (on `AutoDev`)

- `59e3a4e4` Critters: the core (steps 1-8) — terrain, level reader, world and the six roles, solutions and replay, progress; crsim and the host test
- `80e1481c` Critters: the 12 shipped levels (tools/critters/mklevels.py) and their recorded solutions — each won, each lost when nothing is done (step 9)
- (this document's commit)

### Tests run

| Test | Result |
|---|---|
| `sh tools/tests/run_critters_test.sh` (AC-1…24, 29–31; 03 §9.1's table) | **passes**: `ok   critters (930 checks: 12 levels, 12 solutions, 66145 steps)` in both builds (ASan/UBSan `-O1`: no error, no leak; `-O2`); `determinism fingerprint 1492ed8e6873c835` equal in both; ≈ 11 s with the three compilations (the runs ≈ 1 s each). The 12 levels' lines printed (saved / needed / of / time / limit) as in the table above. The 1600 × 160 build with 60 shapes: ≈ 3.4 ms at `-O2` |
| mutation checks (by hand, reverted) | `WALK_UP` 6→5, `FALL_KILL` 60→59, `BRICK_EVERY` 8→7, `BURST_R` 12→11, `BLOCK_DX` 6→5: each makes the test fail (1 to 12 failures) — the pixel rules are pinned, not vacuous |
| grep guards | no float / rng / clock / kapi / UIKit in the core (the script); `grep -ril lemming` over the app, its levels, the tools and the tests: nothing; every new source has "MIT License" |
| `python3 tools/lang/check.py critters` | sees the 19 `// TR:` words; reports "no lang/fr.txt" — `fr.txt` is step 12 (Developer B) |

AC → test (in `critterstest.cpp`): **A** `test_shipped` (AC-1: the 12 load, `name.fr` / `hint.fr` / labels' `text.fr`,
counts, sizes, 6 + 6 prefixes in `<nn>` order), `test_example` (AC-2 the example's facts and defaults; AC-3: 48 broken
files made from the example by one line changed — each reason, line, `fmt` and the level cleared — plus > 64 KB,
unreadable, empty; AC-4 `sparkle = 1`; `withDiggers2`; `material_at` = built map on every pixel), `test_terrain_build`
(AC-5: rect 150 px, circle r 10 = 317 px, five polygons — triangle, square, star, bow-tie, a thin sliver — against an
independent even-odd count, erase, steel over earth, clipping of rect / circle / polygon, the textures and rivets, the
build time), `test_terrain` (step 2: edges, digging keeps steel / water, dirty rectangle, disc, bricks both ways, 2 000
random edits → `hash == full_hash`); **B** `test_release` (AC-6 incl. a rate change mid-level), `test_walk` (AC-7),
`test_fall` (AC-8, AC-9), `test_hazards` (AC-10, the edges for a walker and a climber), `test_exit` (AC-11); **C**
`test_climber` (AC-12), `test_blocker` (AC-13), `test_builder` (AC-14 pixel by pixel, the early stop, the 30-px gap over
a 100-px drop crossed by the followers — all four saved on the far side), `test_digger` (AC-15), `test_exploder`
(AC-16, in the air too), `test_assign` (AC-17), `test_nuke` (AC-18); **D** `test_end` (AC-19, and
`onlyBlockersLeft`), `test_clock` + `clock_run` (AC-21 on a rule level and on all 12 solutions: random dt 10–30 ms,
random pauses and fast toggles → the same result, refusals and final checksum), `test_solution` (AC-22, the recorder's
round trip); **E** `test_progress` (AC-23, AC-24); **F** `test_solutions` (AC-30 won with 0 refused before the limit;
AC-20 each run twice, equal per-step checksum streams, `hashOk`, the fingerprint; AC-31 lost with nothing, Training lost
without its role and its count at 0).

### Deviations from the plan (and why)

1. **The level counts and rates of 02 §6 were refined** (02 calls them targets): T2 `rate = 30` and a 16-px ravine,
   E1 `rate = 40`, E4 `rate = 1` — a builder's followers walk 1 px a step while the stair grows 3 px every 8 steps, so
   with a fast release the next creature walks off the unfinished stair; a slower release (which the player may raise)
   gives the builder time. E3 became **16 / 12 with climber 14, floater 14** (every creature saved must both climb and
   float: the climber count bounds the saved count, so 02's "climber 6, save 14" could not be won). In E5 the climbers
   and floaters are spare; in E2 the exploders, in E4 the digger, in E6 the blockers and exploders are spare (tools for
   other routes or mistakes). The hints were rewritten to match each level as drawn (EN + FR).
2. **The exits moved where the puzzles need them** (E4's exit just past the lake so that the blocker matters; E5's
   exit on a 18-px ledge reached by a stair; E6's exit 1 in the cave, exit 2 behind the steel wall).
3. **Climbers do not climb the map's side edges** (they turn there, as walkers do): the edge is "steel" (02 #3), but a
   climber going up the edge would only reach the ceiling and fall to its death; AC-10 ("at the map's edge it turns")
   is kept for both. Tested.
4. **A creature leaving through an exit drops its fuse** (an exploder that reaches the exit is saved).
5. **`build_terrain` returns `bool`** (false: no memory) instead of `void`; `Level::start = −1` means "centred on the
   first hatch" — the window computes it (it knows the view's 400-px width).
6. **Added to the API** (no rule change): `E_VOID` (a creature fell out of the map, for a sound), `Event::role` carries
   the countdown digit for `E_TICK` and the bricks left for `E_BRICK_WARN`; `World::alive`, `timeLeft`,
   `onlyBlockersLeft`; `Replay::start`; `run_solution`'s optional `World *` (the window's `--until` can run into its own
   world) and `RunResult::hashOk`; `mix32` / `mix64` exported from `terrain.h`.
7. **Builder given again: `bricks += 12`** (capped at 99) — "another 12" (02 #13), whether it is still building or
   shrugging.
8. **`progress_won` says "a new best" only for a level won before** (the first win is not a "New best!").
9. **The plan's grep line was widened** to `clock (`, `time (`, `rand (` as well, and the test reads the levels from
   the card's folder when no argument is given (the plan's `ls`, made `|| true` so a step-1 run passes).

### Left for Developer B (steps 10–14)

- The window (`main.cpp`, `draw.h`, `bar.h`, `picker.h`): everything of the GUI plan G1–G10. What the core gives it:
  `World` (`c[]`, `nout`, `ev[]`/`nev` after each `tick ()` — `assign`'s refusal is its return value, its event is
  cleared by the next tick), `pick`, `can_take` (the status line's "cannot dig now"), `onlyBlockersLeft`, `timeLeft`,
  `Clock`, `Replay` / `Recorder` / `run_solution` (`--replay`, `--until` — run headless with sounds dropped, binding
  note 3), `t.col` / `t.take_dirty ()` (the ×2 blit, the minimap), `material_at` (the picker validates without
  building), `Text::get (lang)`, `LoadError::fmt` + `arg[]` (translate `fmt` and the "too many" word, then fill).
- `sdcard/apps/critters.app/lang/fr.txt` must hold the 19 loader words (`unknown block [%s]` … `labels`, `bad step` …
  `too many actions (max 1024)`) plus the UI's; `check.py critters` → 0 missing.
- The simulator fixtures (`tools/tests/desktop_sim/critters/progress.ini`, `sd/docs/critters/{broken,quick,
  my-first-level}.level` + `quick.sol`), `run_critters_sim_test.sh`, the `shots.sh` block (the solutions to copy are in
  `tools/tests/critters/solutions/`; for `critters-build` E2's builder works from step 570 and its digger from 372),
  `user/Makefile`, `app.txt`, `icon.bmp`, `fileassoc.ini`, `packages.ini` (declared only), docs, HANDOFF, IDEAS.
- AC-28's click on T1: with `training-01-straight-down.sol` minus its line (or `--until 60`), creature 0 walks on the
  floor at `y = 79` from step 49 (x ≈ 80 + (step − 49)); `crsim … --where N` gives any other position.
