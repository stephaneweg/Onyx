# AutoDev round 4 — Product Analyst: Pinball

Date: 2026-10-06. Input: `01-product-manager.md` (the pick: **Pinball**, the queue's item 3; its MUST / SHOULD /
deferred scope is kept here — what this document adds to the MUST list is only what the existing code makes
necessary, flagged *(analyst)*).

Looked at: `user/Apps/games/game.h` (`GameView` — `paint`, `press/release/move`, `key`, `tick (dt)`; `GameRoot` —
`onTick` → `view->step ()`; `sfx` / `sfx_later` / `sfx_win` / `sfx_lose` / `sfx_set_mute` on AudioKit's FM voices
12–15; `rng_seed` / `rng_n`; `gtext*`, `gitoa`, `gcat*`), `user/Apps/invaders/main.cpp` (448 × 480 window, a HUD,
states title / play / paused / over, `kapi_key_held (KEY_LEFT / ' ')` polled in `tick`, a *Game* menu: *New Game ^N*,
*Pause P*, *Sound On / Off*; its `hiscore` lives in memory only), `user/Include/gamepad.h` (`pad_buttons (-1)` →
`PAD_UP…PAD_HOME`, `PAD_L/R/L2/R2`, mapped by `SD:/etc/gamepad.ini`), `user/Kits/filekit/kvtext.h` (`fk_kv_load /
parse / get / set / block / line / save`; repeated `[section]` blocks, the line of every value, unknown keys kept),
`user/Kits/systemkit/notify.h`, `kernel/sys/kapi.cpp` `kapi_key_held` and `kernel/gui/kwin.cpp` `UsageToKey` /
`KeyHeldAny` (**which keys can be held**, §4.3 below), `kapi_get_modifiers` (`MOD_SHIFT`: no left/right),
`tools/tests/desktop_sim/fakekapi.cpp` (`key_held` returns 0, line ~1311), `tools/tests/desktop_sim/shots.sh`
(`SIM_ARGS`, `SIM_OVERLAY`, `SIM_WRITES`), `sdcard/apps/invaders.app/app.txt` (`category = Games`),
`sdcard/etc/fileassoc.ini` (no `table` entry yet), `tools/pkg/packages.ini` (`[app.invaders] needs = audiokit`,
`[app.circuits] needs = … filekit >= 1.96`), `sdcard/cmdline.txt` (the screen: 1920 × 1080), `docs/04-USER-GUIDE.md`
§12 (the games' catalog rows).

---

## 1. The name

**Pinball** — `user/Apps/pinball/`, `SD:/apps/pinball.app/`, `app.txt`: `name = Pinball`, `category = Games`,
`opens = table`. Started from the dock's Games drawer, or `run pinball`; `pinball <file.table>` plays that table at
once. In French the app keeps its name (*Pinball* is used in French; the window title stays *Pinball*; the
picker's heading may say *Flipper* — a UX choice).

## 2. The one-line pitch

**Pinball: three real-physics pinball tables — flippers, bumpers, drop targets, a ramp and multiball — and a plain
text format to make your own.**

## 3. The users and their tasks

| User | Task | How Pinball serves it |
|---|---|---|
| A player (child or adult) | play a quick game, beat their best score | pick a table, 3 balls, a top-5 per table kept on the card, the name typed once and remembered |
| A gamepad player (the Pi on a TV) | play without touching the keyboard | every action on the pad: flippers on the shoulders, plunger on A, nudge on Y, pause on Start; the name entry accepts the remembered name with A |
| A French-speaking player | play in their language | every word on screen, the tables' names, goals and messages in French when `language = fr` |
| A maker / a learner of coordinates | build a table in a text editor and play it | the `.table` text format (§7.1): blocks of `key = value`, an error names the line; `SD:/docs/pinball/*.table` listed on the picker; a double click on a `.table` plays it |
| The user (the reviewer) | see that the physics works without a Pi | a host test that drops thousands of balls on every table, deterministic; simulator screenshots |

## 4. Features

### MUST (this round)

**4.1 The physics** (a module without UIKit, `user/Apps/pinball/physics.{h,cpp}` or as the Technical Analyst
names it, so the host test links it alone).

1. **Units**: table units, origin top-left, **x right, y down** (towards the player), the table's `size` in units
   (the shipped tables: 520 × 1040). Time in seconds, speeds in units/s, angles in degrees.
2. **The ball**: a circle of radius `ball` (default 13), position and velocity in floats. **Gravity** along the
   table (+y), the table's `gravity` (default 1400 units/s²).
3. **A fixed step**: the world advances **1/60 s per frame in 8 sub-steps**; the ball's speed is **capped at 4000
   units/s** — so one sub-step moves it at most 8.4 units, less than its radius: it cannot cross a wall. The game
   runs one step per `GameRoot` tick; a late frame catches up at most 3 steps (then the time is dropped — the
   game slows rather than jumps). **Deterministic**: the same table, seed and inputs per frame give the same run,
   bit for bit (the tests depend on it; the only randomness is `rng_*` seeded per game).
4. **Collisions** of the ball with: **segments** (walls are polylines of segments), **circles** (posts, bumpers),
   **arcs** (a circle's part, the round top) — each with its `bounce` (restitution, 0…1.5) and `friction`
   (0…1, the tangential speed lost). Two balls (multiball) collide with each other (equal masses).
5. **Flippers**: a capsule (two radii, base and tip) turning about its pivot between `rest` and `up` at `speed`
   (default 1800 °/s); while it turns, the contact point's speed (ω × r) is added to the ball's bounce, so a
   moving flipper **strikes**; the collision is checked along its sweep in each sub-step so a fast flipper never
   passes through the ball. Held = up; released = falls back to rest. Two flippers at least (a left, a right),
   **a third** allowed (it moves with the key of its `side`).
6. **The plunger**: holding the plunger key pulls it (0 → full in 1 s, drawn as a spring); releasing launches the
   ball resting on it at `max × pull` upwards; a **tap** (released before 0.1 s) launches at the table's `auto`
   speed (the simulator's and the gamepad's quick launch). A ball only leaves the shooter lane upwards (a one-way
   gate at its top, in the table).

**4.2 The table's elements** (all from the file, §7.1; drawn with their lamps, §4.4).

7. **Pop bumpers**: a circle that kicks the ball away at least at its `kick` speed, scores, flashes 0.15 s, plays a
   sound.
8. **Slingshots**: a segment that kicks the ball away perpendicularly at its `kick` speed when hit faster than a
   threshold (200 units/s), scores.
9. **Drop targets** in **banks**: a segment that, hit, scores and **falls** (no longer collides, drawn down);
   every target of a bank down → the **bank cleared** event (§4.5 rules), then the bank rises again after 1 s.
   **Standup targets**: a segment that scores and lights its lamp each hit (stays up).
10. **Rollover lanes**: a rectangle sensor; the ball crossing it scores and lights the lane. The lanes of the
    table's `rotate` group (the top lanes) **shift their lit lamps one place on each flipper press** (left flipper
    → left, right → right) — the classic "lane change"; all of a group lit → the **lanes complete** event, the
    group goes dark. Inlanes / outlanes are lanes of other groups (they score, a rule may use them).
11. **One-way gates**: a segment the ball crosses in its `pass` direction only, a wall the other way.
12. **A ramp / orbit**: an **entry** segment crossed in its direction → the ball leaves the playfield, is drawn
    travelling along the ramp's `path` for `time` seconds, then comes back at the path's last point with the
    velocity `out` (towards a flipper); scores a **ramp shot**. Crossed the other way: nothing (the ball rolls on).
    No 3D.
13. **A saucer / kicker**: a circle sensor that catches a ball slower than 1500 units/s (centred, stopped), holds
    it `hold` seconds, then ejects it with `out`; scores; each catch is a **saucer** event (a "lock").
14. **The drain**: a ball whose centre goes below the table's `drain` line is lost.
15. **Decoration**: filled polygons (`[shape]`) and painted words (`[label]`, with `text.fr`) under the elements,
    without collision — the table's artwork.

**4.3 The controls** — keyboard and gamepad, §10. *(analyst)* **The held keys the system reports are letters,
digits, Space, Enter, Esc and the arrows only** (`kernel/gui/kwin.cpp` `UsageToKey`); Shift is a modifier
(`kapi_get_modifiers` `MOD_SHIFT`), the same for both sides; `/` cannot be held. So the PM's *Left / Right Shift*
and *Z / /* are replaced by **← / →** and **Z / M** (a kernel change is out of this round). Flippers and plunger
read `kapi_key_held` and `pad_buttons (-1)` every tick (as Invaders); nudge, pause, mute are key presses.

**4.4 The screen** — one fixed-size window (portrait; the UX designer sizes it: it must fit the 1920 × 1080
screen with the window's frame and menu bar, ideally ≤ 760 px tall so a 1280 × 800 screen fits too).

16. **The table picker** (at start, after a game, on Esc): the tables as a list or cards — the shipped three in a
    fixed order (*Space Station*, *Haunted Manor*, *Volcano*), then the player's (`SD:/docs/pinball/*.table`, by
    name); for the chosen one: its **name**, its **goal** (one or two lines), a **thumbnail** of the playfield
    (the same drawing, scaled down) and its **top 5**. ↑ / ↓ (d-pad) choose, Enter / Space / A play. A player's
    table that fails to load is listed **greyed with its error** (*"line 12: unknown block [bumber]"*) and cannot
    be played.
17. **Playing**: the whole playfield scaled to fit, the elements drawn with UIKit's `Canvas` (filled polygons,
    circles, gradients), lit lamps bright / unlit dim, a moving flipper at its true angle, the ball(s) with a
    highlight, the plunger's spring and pull. A **score panel**: score, ball *n / 3*, multiplier *×n*, the table's
    best score, the bonus building up, and a **message line** for the events (*"MULTIBALL!"*, *"Extra ball!"*,
    *"Ball saved"*, *"Tilt warning"*, *"TILT"*, the rules' messages) shown 2 s.
18. **Pause** (P, Start, *Game ▸ Pause*): the world frozen, a *Paused* notice; Esc in a game: pause with
    *Resume / Back to tables* (the game is lost).
19. **End of ball**: the bonus counted up × the multiplier (≈ 1.5 s, skippable with any key), then the next ball
    on the plunger. **Game over**: the final score; if it enters the table's top 5, a **name entry** (a text field
    holding the last name used; Enter / A keeps it) — then the top 5 with the new line highlighted.

**4.5 The rules**

20. **3 balls** a game (the table's `balls`). **Ball save**: a ball lost within `ballsave` seconds of its launch
    (and not in multiball) comes back on the plunger, the ball not counted, *"Ball saved"*. **Extra ball**: one
    more ball, played after the current one (*"Shoot again"* lit).
21. **Bonus**: every scoring element also adds 1/10 of its score to the ball's bonus (rounded down to 10); at the
    end of a ball the bonus × the multiplier is added; multiplier (1…5) and bonus go back to 1 / 0 for the next
    ball. A tilted ball has no bonus.
22. **Multiball**: a rule's `multiball n` (n = 2 or 3) puts balls in play up to n — the new ones auto-launched from
    the plunger one after the other (0.5 s apart). While 2+ balls play, a lost ball is just removed; the last one
    lost ends the ball as usual. Not started again while it runs.
23. **Tilt**: a nudge (N / ↑ / Y) gives each ball a small push (150 units/s, up and towards a random side —
    `rng`), and the table shakes on screen 0.1 s; the nudges within the last 5 s are counted: the 2nd →
    *"Tilt warning"*, the **3rd → TILT**: flippers dead and scoring off until the ball drains, no bonus. The next
    ball is normal.
24. **The table's rules** (`[rule]` blocks, §7.1): an event (*bank cleared*, *lanes complete*, *ramp shot*,
    *saucer catch*, *a lane / a target hit*) counted; at `count` the rule fires its actions (*score*, *bonus*,
    *multiplier*, *multiball*, *extra ball*, *ball save*) and shows its message, then counts again from 0
    (`once = 1`: at most once a game). Counters last the whole game.

**4.6 The tables, the files**

25. **The table format** (§7.1), read with FileKit's `fk_kv` (`FK_KV_PIPES` not needed). A bad file is **refused
    with its line and reason**; never a crash, never a half-loaded table.
26. **Three tables** (§6), `sdcard/apps/pinball.app/tables/{1-space-station,2-haunted-manor,3-volcano}.table`, each
    with its look, its goal and its rules; plus the players' `SD:/docs/pinball/*.table` on the picker.
27. **High scores**: a **top 5 per table** in `SD:/apps/pinball.app/scores.ini` (§7.2), written with `fk_kv` at
    each new entry; shown on the picker and at game over.
28. **Sounds** (`sfx_*`): bumper, slingshot, flipper, target / drop, lane, ramp, saucer catch / eject, drain;
    jingles for multiball, extra ball, game over (`sfx_win` / `sfx_lose`); **S** or *Game ▸ Sound On / Off* mutes
    *(analyst: M is now a flipper)*; the choice remembered in `scores.ini` `[settings]`.
29. **English and French** (§11).
30. **The host test and the simulator** (§12): `tools/tests/pinball/pinballtest.cpp` +
    `tools/tests/run_pinball_test.sh`; the simulator's `hold <key>` / `release <key>` script commands (the test-only
    change to `fakekapi.cpp`, its `key_held` answering them); `shots.sh pinball`.
31. **Packaging and docs**: `user/Makefile`, `sdcard/apps/pinball.app/` (`app.txt`, `icon.bmp`, `tables/`,
    `lang/fr.txt`), `sdcard/etc/fileassoc.ini` `table = pinball`, `tools/pkg/packages.ini` `[app.pinball]`
    (declared, not published — PIPELINE §0.5), docs/04 §12 (catalog row) and a *Pinball* section (controls, rules,
    the three tables, files, **the `.table` format**), `docs/HANDOFF.md`, `IDEAS.md`'s row, `python
    docs/build_docs.py`.

### SHOULD (in this order, if time allows)

1. **Table viewer / checker**: *Table ▸ Check a Table…* (a file dialog) or `pinball --check <file>`: the table drawn
   still, every element outlined with its `id`, the walls' points numbered, the error list (all errors, not just
   the first) — so a maker can see what they wrote. A sample `SD:/docs/pinball/my-first-table.table` (the §7.1
   example, commented) to start from.
2. **A shared high-score helper** — only beside the app this round (`scores.{h,cpp}`), written to move into a kit
   when a second game uses it (kits-first rule: one user = beside the program).
3. **Attract mode** on the picker (the thumbnail's lamps cycling) and the **skill shot** (a top lane lit at
   random at launch; the plunged ball rolling through it: 5 000 × the ball number).
4. Dropping a `.table` file on the window plays it (if UIKit's drops come cheaply to a `GameView`).

### LATER (deferred, as the PM sized them)

A graphical table editor; true 3D ramps / habitrails, spinners, magnets, moving toys; tall scrolling tables and a
dot-matrix display with animations; several players taking turns; rules with conditions (a lock lit only after a
bank: `needs =`), modes and jackpots; the feel tuned on the real Pi (frame rate, pad latency); Left / Right Shift
as flippers (a kernel change: the held-key table, §4.3).

## 5. What it does NOT do (this round)

- No editor inside the app (the text format + the checker SHOULD); no table downloaded from the net (a `.table`
  file can come by any means — the package manager, the network share, a USB key).
- No 3D, no camera tilt, no scrolling: the whole table is always visible.
- One player at a time; no online scores, no notification (the game is in front and shows its own messages;
  Invaders, Tetris… do not notify either).
- No clipboard use; no file written but `scores.ini` (and nothing in `SD:/docs/pinball`).
- No kernel, kapi or AppKit change; no new kit.
- No Left / Right Shift (§4.3); no analogue plunger on a stick.
- Not a physics sandbox: an element type outside §7.1 is refused, not ignored.

## 6. The three tables

Shipped in `SD:/apps/pinball.app/tables/`, 520 × 1040 units each, two flippers at the bottom (pivots about
(150, 905) and (326, 905), length 70 — the gap between their tips lets a ball drain when no flipper is up), a
shooter lane on the right (x 476…520) with a one-way gate at its top, a round top (an arc), inlanes and outlanes
beside the flippers. The Technical Analyst / UX designer draw the exact layouts; what each must have:

| Table (EN / FR) | File | Feel | Elements (at least) | Goal (shown on the picker) and rules |
|---|---|---|---|---|
| **Space Station** / *Station spatiale* — the starter | `1-space-station.table` | gravity 1200, ball save 10 s | 3 bumpers, 2 slingshots, 3 top lanes (rotate), a bank of 3 drop targets, a left **orbit** (ramp) back to the right flipper, a saucer *Dock*, 2 standups | *"Dock the shuttle twice for multiball."* — lanes complete → multiplier +1; bank cleared → 5 000 bonus; orbit ×3 → extra ball (once); saucer ×2 → multiball 2 |
| **Haunted Manor** / *Manoir hanté* | `2-haunted-manor.table` | gravity 1400, ball save 8 s | 3 bumpers, 2 slingshots, 3 top lanes, **two banks of 4 drop targets** (*Ghosts*, *Bats*), a saucer *Crypt*, a right orbit, 3 standups | *"Clear the ghosts and the bats, then lock 2 balls in the crypt: 3-ball multiball."* — each bank cleared → 10 000; both banks ×2 → extra ball (once); crypt ×2 → multiball 3 |
| **Volcano** / *Volcan* — faster | `3-volcano.table` | gravity 1700, ball save 6 s | 4 bumpers, 2 slingshots, **4 top lanes**, a centre **Lava ramp**, a **third flipper** (upper right, `side = right`), a saucer *Crater*, a bank of 3 drop targets | *"Hit the lava ramp 5 times: eruption multiball!"* — ramp ×5 → multiball 2; lanes complete → multiplier +1; ramp ×10 → extra ball (once); bank cleared → ball save 10 s |

Every table has its colours (a dark background, its own palette), `[shape]` artwork and `[label]` words (in EN and
FR) — the UX designer's mock-ups decide them.

## 7. Files read and written

| File | Read / written | Format |
|---|---|---|
| `SD:/apps/pinball.app/tables/*.table` | read | §7.1 — the shipped tables (in the package) |
| `SD:/docs/pinball/*.table` | read | §7.1 — the player's tables, listed on the picker (the folder may not exist) |
| any `.table` given as argument (`pinball <path>`, a double click) | read | §7.1 — played at once, also added to the picker for this run |
| `SD:/apps/pinball.app/scores.ini` | read and written | §7.2 — not shipped (made on the first high score), never overwritten by an update |
| `SD:/apps/pinball.app/lang/fr.txt` | read | UIKit's catalogue (`English<TAB>French`) |
| `SD:/etc/system.ini` `language=` (via UIKit), `SD:/etc/gamepad.ini` (via `gamepad.h`) | read | the system's |

### 7.1 The table format (`*.table`) — version 1

A UTF-8 text file read by FileKit's `fk_kv_parse` (flags 0): `#` / `;` start a comment line, blank lines ignored,
`[block]` headers, `key = value` lines; a block name may come many times (each header is one element). The game
then checks every block — **a file breaks one of the rules below → it is refused, the message
`line <n>: <reason>`** (the line of the offending key, or of the block's header for a missing key; reasons in §7.1.4).

#### 7.1.1 The values

```
number   := ["-"] digits ["." digits]                      (no exponent)
point    := number SEP number                              x y, in table units
points   := point { SEP point }                            SEP = spaces and/or one comma
colour   := "#" 6 hex digits                               #RRGGBB
bool     := "0" | "1"
text     := any characters to the end of the line          (trimmed)
id       := [a-z0-9_-]{1,24}                               unique in the file
```

Every coordinate must lie inside `0…width`, `0…height` (a circle's centre inside; its edge may touch). Unknown
**keys** inside a known block are ignored (a later version's keys load); an unknown **block** is an error (a typo
like `[bumber]` must not vanish silently).

#### 7.1.2 The blocks and their keys (R = required; the default otherwise)

| Block | Key | Meaning |
|---|---|---|
| `[table]` (exactly one, first) | `format` R | `1` |
| | `name` R, `name.fr` | the table's name (≤ 40 characters) |
| | `goal`, `goal.fr` | one or two sentences shown on the picker (≤ 160 characters) |
| | `size` R | `width height` (200…2000 each) |
| | `gravity` | units/s² along +y — 1400 (200…5000) |
| | `ball` | ball radius — 13 (6…30) |
| | `balls` | balls a game — 3 (1…9) |
| | `ballsave` | seconds — 8 (0…30) |
| | `drain` R | the y below which a ball is lost |
| | `rotate` | the lane group whose lamps shift with the flippers — none |
| | `background` | colour — `#101828` |
| `[wall]` | `points` R | a polyline, ≥ 2 points |
| | `id` | `outline` = the table's outline (exactly one, `closed = 1`, ≥ 3 points): every ball stays inside it |
| | `closed` | bool — 0 (1: the last point joined to the first) |
| | `bounce`, `friction` | 0.5, 0.1 |
| | `colour`, `width` | `#8090A0`, drawn width 4 |
| `[arc]` | `centre` R, `radius` R | a circle's arc (collides on both faces) |
| | `from` R, `to` R | degrees, 0 = +x, 90 = +y (down), drawn clockwise from `from` to `to` |
| | `bounce`, `friction`, `colour`, `width` | as `[wall]` |
| `[post]` | `at` R, `radius` R | a fixed round post — `bounce` 0.6, `colour` |
| `[bumper]` | `id` R, `at` R, `radius` R | a pop bumper |
| | `kick` | units/s — 900; `score` — 100; `colour` |
| `[sling]` | `id` R, `a` R, `b` R | a slingshot's face (segment a → b) — `kick` 700, `score` 10, `colour` |
| `[target]` | `id` R, `a` R, `b` R | a target's face |
| | `kind` R | `drop` or `standup` |
| | `bank` | required for `drop`: the bank's id (2…8 targets) |
| | `score` | 500 (drop), 250 (standup); `colour` |
| `[lane]` | `id` R, `rect` R | a rollover sensor: `x y w h` (top-left, size) |
| | `group` | a name (`top`, `in`, `out`…) — none; `score` — 50; `colour` |
| `[gate]` | `a` R, `b` R, `pass` R | one-way: `up`, `down`, `left` or `right` = the direction the ball may cross |
| `[flipper]` | `side` R | `left` or `right` (which key moves it; which way it points) |
| | `pivot` R, `length` R | the pivot, the length to the tip's centre (30…200) |
| | `rest`, `up` | degrees below the horizontal, pointing towards the table's centre — 30, −25 |
| | `radius` | `base tip` — `12 6`; `speed` °/s — 1800; `bounce` 0.4; `colour` |
| `[plunger]` (exactly one) | `at` R | the ball's place on the plunger |
| | `max`, `auto` | launch speeds, units/s — 2600, 2300 |
| `[ramp]` | `id` R, `a` R, `b` R | the entry segment |
| | `pass` R | the direction that enters (as `[gate]`) |
| | `path` R | ≥ 2 points the ball is drawn along; the last = where it comes back |
| | `time` | seconds on the ramp — 0.8; `out` R: the velocity `vx vy` it comes back with |
| | `score` — 1000; `colour`, `width` (drawn band) — 24 | |
| `[saucer]` | `id` R, `at` R, `radius` R | the kicker |
| | `hold` — 1.5 s; `out` R: `vx vy`; `score` — 750; `colour` | |
| `[shape]` | `points` R, `colour` R | a filled polygon (artwork, no collision), drawn in file order, under the rest |
| `[label]` | `at` R, `text` R, `text.fr` | painted words (centred on `at`) — `size` 1…4 (1), `colour`, `angle` 0 or 90 |
| `[rule]` | `when` R | the event: `bank <id>` · `lanes <group>` · `ramp <id>` · `saucer <id>` · `hit <id>` (any lane, target, bumper or sling) |
| | `count` | how many events fire it — 1 (1…99) |
| | `do` R | actions separated by `;`: `score <n>` · `bonus <n>` · `multiplier` (+1, max 5) · `multiball <2\|3>` · `extraball` · `ballsave <seconds>` |
| | `once` | bool — 0 (1: at most once a game) |
| | `message`, `message.fr` | shown in the message line (≤ 32 characters) |

**Limits**: at most 2 000 wall segments, 64 circles (posts + bumpers + saucers), 64 targets, 32 lanes, 16 gates,
4 ramps, **2 or 3 flippers with at least one `left` and one `right`**, 32 rules, 128 shapes, 64 labels; a file
≤ 64 KB.

#### 7.1.3 An example — `my-first-table.table`

```ini
# My first table -- two bumpers, three drop targets, three top lanes.
[table]
format     = 1
name       = My First Table
name.fr    = Ma première table
goal       = Knock down the three targets twice for multiball.
goal.fr    = Abattez deux fois les trois cibles pour le multiball.
size       = 520 1040
gravity    = 1400
drain      = 1010
ballsave   = 10
rotate     = top
background = #10203A

[wall]
id     = outline
closed = 1
points = 0 0, 520 0, 520 1040, 0 1040

[arc]
centre = 260 260
radius = 258
from   = 180
to     = 360

# the shooter lane: its inner wall, its one-way gate at the top
[wall]
points = 476 1040, 476 300
[gate]
a    = 476 300
b    = 518 300
pass = up

# the guides down to the flippers
[wall]
points = 20 760, 20 840, 150 900
[wall]
points = 456 760, 456 840, 326 900

[flipper]
side  = left
pivot = 150 905
length = 70
[flipper]
side  = right
pivot = 326 905
length = 70

[plunger]
at = 498 980

[bumper]
id = b1
at = 190 330
radius = 28
colour = #E04060
[bumper]
id = b2
at = 290 330
radius = 28
colour = #E04060

[sling]
id = sl
a = 80 760
b = 130 850
[sling]
id = sr
a = 396 760
b = 346 850

[lane]
id = l1
group = top
rect = 150 110 30 60
[lane]
id = l2
group = top
rect = 225 110 30 60
[lane]
id = l3
group = top
rect = 300 110 30 60

[target]
id = t1
kind = drop
bank = trio
a = 60 520
b = 60 556
[target]
id = t2
kind = drop
bank = trio
a = 60 562
b = 60 598
[target]
id = t3
kind = drop
bank = trio
a = 60 604
b = 60 640

[rule]
when    = lanes top
do      = multiplier; score 1000
message = Bonus x up!
message.fr = Bonus x augmenté !

[rule]
when    = bank trio
count   = 2
do      = multiball 2
message = MULTIBALL!
message.fr = MULTIBALL !

[label]
at      = 240 700
text    = MY TABLE
text.fr = MA TABLE
size    = 2
colour  = #304870
```

#### 7.1.4 The errors (the reason after `line <n>: `; translated with `TR`, the tests compare the English)

| Case | Reason |
|---|---|
| a block not in §7.1.2 | `unknown block [<name>]` |
| no `[table]`, or not the first block, or two | `[table] must be the first block, once` |
| a required key missing | `[<block>] needs <key>` (the block's header line) |
| a value that does not read (wrong count of numbers, not a number, not a colour, bad word) | `bad value for <key>` |
| a value out of its range, a point outside `size` | `<key> out of range` |
| two elements with one id | `id <id> used twice` |
| no `outline` wall, or not closed, or two | `the table needs one closed wall with id = outline` (line of `[table]` if missing) |
| flippers: fewer than 2, more than 3, no left or no right | `the table needs 2 or 3 flippers, a left one and a right one` |
| no `[plunger]` / two | `the table needs one [plunger]` |
| a rule naming an id or group that does not exist, an unknown event or action | `unknown <id>` / `bad value for when` / `bad value for do` |
| a bank of 1 or more than 8 targets | `bank <id> needs 2 to 8 targets` |
| over a limit | `too many <things> (max <n>)` |
| the file > 64 KB, unreadable | `the file is too big` / `cannot read the file` (line 0) |

### 7.2 `scores.ini` — written by the game (`fk_kv`, `FK_KV_ESCAPES`)

```ini
# Pinball -- high scores (written by the game)
[settings]
name  = Steph
sound = 1

[1-space-station]
1 = 1250340 Steph
2 = 830120 Léa
3 = 412000 Steph

[user.my-first-table]
1 = 95210 Léa
```

- A section per table: the shipped tables by their file's base name; any other table (`SD:/docs/pinball/`, an
  argument) as `user.<base name>`.
- Lines `1`…`5`, best first: `<score> <name>` — the score an integer ≥ 0, the name the rest (1…16 characters,
  spaces allowed, a `=` or a new line replaced by a space). Fewer than 5 lines: the rest empty on screen.
- Equal scores: the older entry stays above. A line that does not read is skipped (the file is not refused).
- `[settings] name` = the last name typed (the name entry's default; "Player" / "Joueur" before any), `sound` = 0
  muted. Unknown sections and keys are kept when the file is written back.

## 8. File associations

- **New**: `sdcard/etc/fileassoc.ini` `table = pinball` (under a `# Pinball` comment), `app.txt` `opens = table`,
  `[app.pinball] opens = table` — a double click on a `.table` in the File Viewer plays it. *(`.table` is free: no
  other app claims it.)* Its text opens in Tinypad through *Open With*, the way a maker edits it.

## 9. How it fits with the other apps

- **The games** (Invaders, Pong, Snake, Tetris, Arkanoid…): the same frame (`games/game.h`: `GameView` /
  `GameRoot`, `sfx_*`, the *Game* menu with *New Game ^N*, *Pause P*, *Sound On / Off*), the dock's **Games**
  drawer. Pinball is the first game with **high scores kept on the card**; the scores code is written so it can
  move into a kit later (SHOULD 2), the others unchanged now.
- **Tinypad** edits the tables; the **File Viewer** opens them (§8); `SD:/docs/pinball/` is the player's folder,
  as `SD:/docs/` holds the other apps' documents.
- **Gamepad** (Pad Config's `SD:/etc/gamepad.ini`) — the pad's buttons named by place, as every game.
- **Language & Region**: the app follows `language=`; it joins the list of translated apps in docs/04.
- **Package Manager**: `[app.pinball] needs = uikit >= …, audiokit >= 1.232, filekit >= 1.96` (the versions the
  Technical Analyst confirms), `opens = table`; `apps/pinball.app/` packaged whole by the `[*apps]` rule; no user
  file shipped (`scores.ini` is made on the Pi). Declared, not published.
- No notification, no clipboard, no change to another app's files.

## 10. Controls

| Action | Keyboard | Gamepad (`gamepad.h`) | Menu |
|---|---|---|---|
| Left flipper (held) | **←** or **Z** | **L** or **L2** or **d-pad ←** | |
| Right flipper (held) | **→** or **M** | **R** or **R2** or **B** | |
| Plunger: hold to pull, release to launch; tap = quick launch | **Space** or **↓** or **Enter** | **A** | |
| Nudge | **↑** or **N** | **Y** | |
| Pause / resume | **P** | **Start** | *Game ▸ Pause* |
| Back to the tables (asks while playing) | **Esc** | **Select** | *Game ▸ Choose a Table…* |
| New game (same table) | **Ctrl+N** | | *Game ▸ New Game* |
| Sound on / off | **S** | | *Game ▸ Sound On / Off* |
| Picker: choose / play | **↑ ↓**, **Enter** / **Space** | **d-pad ↑ ↓**, **A** | |
| Bonus count / messages: skip | any key | any button | |
| Name entry: confirm | **Enter** | **A** (keeps the name shown) | |

The mouse is not needed (a click on a picker entry chooses it, a double click plays it).

## 11. English and French

- Every word on screen in `TR ("...")` (UIKit's `uikit/lang.h`), `uk_lang_init ()` in `main` after the text face;
  the French in `sdcard/apps/pinball.app/lang/fr.txt` (`English<TAB>French`); `python tools/lang/check.py pinball`
  → **0 missing**.
- The tables' texts: `name.fr`, `goal.fr`, `message.fr`, `text.fr` used when the language is `fr` (the English
  one when the `.fr` key is absent). All three shipped tables have every `.fr` key.
- Translated: the picker (*Tables*, *Best scores*, *Play*), the panel (*Score*, *Ball 2 / 3*, *Bonus*, *×3*),
  the messages (*MULTIBALL!* → *MULTIBALL !*, *Extra ball!* → *Bille supplémentaire !*, *Ball saved* → *Bille
  sauvée*, *Shoot again* → *Rejouez*, *Tilt warning* → *Attention, tilt*, *TILT* stays, *Game over* → *Partie
  terminée*, *New high score!* → *Nouveau record !*), the name entry, the menu, the load errors (§7.1.4).
- Never translated: what is stored or compared (the ids, the `[rule]` words, `scores.ini` keys and section names).
- The French screenshot checked: the words fit the panel and the picker (French is ~20 % longer).

## 12. Acceptance criteria (testable)

Host test (`tools/tests/pinball/pinballtest.cpp`, `tools/tests/run_pinball_test.sh`, built on the PC against the
physics / table / rules code without UIKit; every random choice from a fixed seed) — **E**; PC desktop simulator
(`tools/tests/desktop_sim/`, `shots.sh pinball`, `SIM_ARGS` / `SIM_OVERLAY` / `SIM_WRITES`) — **S**; build, tools,
docs — **B**. "A frame" = one 1/60 s step; "simulated" seconds = frames / 60.

**Tables**

1. **E** The three shipped tables load without error; each has 2 or 3 flippers, one plunger, ≥ 3 bumpers, 2
   slingshots, ≥ 3 lanes of its `rotate` group, ≥ 1 drop-target bank, ≥ 1 ramp, ≥ 1 saucer, a rule with
   `multiball`, and `name.fr` + `goal.fr` + every `message.fr`; *Volcano* has 3 flippers.
2. **E** The §7.1.3 example loads (the test embeds it or reads `SD:/docs/pinball/my-first-table.table` if shipped).
3. **E** Each case of §7.1.4 is refused with the stated reason and the right line number (one small broken file
   per case, built from the example by changing one line); none crashes; a refused table yields no table.
4. **E** A file with an unknown key inside a known block (`sparkle = 1` in `[bumper]`) loads.

**Physics**

5. **E** Determinism: on each table, the same seed and the same per-frame inputs (a scripted 2-minute game:
   launches, flipper presses, nudges) run twice give the identical score and identical ball positions at every
   frame.
6. **E** Containment: on each table, **500** launches (seeded random plunger pull 0.2…1.0, flippers pressed at
   seeded random moments), each run until it drains or 120 simulated s: **every ball's centre stays inside the
   `outline` polygon at every sub-step**, and its speed never exceeds 4000 units/s.
7. **E** No dead spot: on each table, 200 seeded launches **with the flippers never pressed**: every ball drains
   within 60 simulated s (a saucer's hold counts; multiball balls included).
8. **E** Wall tunnelling: on the example table, a ball shot at 4000 units/s at each of 64 evenly spaced angles from
   the playfield's centre never ends up on the other side of any wall segment it meets.
9. **E** Drain between the flippers: a ball placed at rest at the midpoint between the two lower flippers' tips,
   120 units above them, flippers down, drains within 2 simulated s with its x between the two tips.
10. **E** Flipper shot: a ball at rest on the lowered left flipper (placed on it, 2/3 of the way to its tip, left to
    settle 1 s), then the left flipper held: within 0.25 s the ball moves up (vy < −1000) and within 1.5 s its
    centre is above the table's half height; the same mirrored for the right flipper.
11. **E** No flipper tunnelling: a ball dropped onto each flipper while it swings up (pressed at every frame offset
    0…10 before contact) never ends below the flipper's swept area with its centre inside the flipper capsule.
12. **E** Plunger: a pull held 1 s launches at `max` (± 1 %), held 0.5 s at half (± 2 %); a tap (released at
    frame 3) launches at `auto`; the launched ball passes the shooter gate and never re-enters the shooter lane
    from above.

**Elements**

13. **E** Bumper: a ball dropped onto a bumper scores its `score` (× 1, untilted), and leaves the contact at a speed
    ≥ its `kick`; the bumper's flash state is on for 0.15 s.
14. **E** Slingshot: a ball hitting a sling at ≥ 200 units/s is kicked at ≥ its `kick` and scores; at 100 units/s it
    only bounces (no score).
15. **E** Drop targets: each target of a bank hit once falls and scores once; a ball crossing a fallen target's
    segment passes through; the last one down fires the `bank` event once, and 1 s later all are up again.
16. **E** Lanes: a ball crossing a lane lights it and scores; with lanes 1 and 2 of the `rotate` group lit, a left
    flipper press gives lanes {2, 3}… shifted left with wrap-around (exact sets checked for both directions); all
    lit → the `lanes` event once and all go dark.
17. **E** Gate: a ball crossing a gate in its `pass` direction goes through; the other way it bounces back.
18. **E** Ramp: crossing the entry in its `pass` direction → the ball leaves play, scores the ramp, reappears at
    `time` ± 1 frame later at the path's last point with velocity `out`; crossing the other way → no score, the
    ball keeps moving.
19. **E** Saucer: a slow ball entering it is held `hold` ± 1 frame, scores once, and is ejected with `out`; a ball
    faster than 1500 units/s rolls over it.

**Rules**

20. **E** A game has `balls` balls: three drains (no ball save, no extra ball) → game over; the score is unchanged
    by drains except the bonus.
21. **E** Ball save: a drain 3 s after launch (ballsave 8) → the ball comes back, the ball number unchanged; a drain
    9 s after → the next ball.
22. **E** Bonus: points scored during a ball add 1/10 (rounded down to 10) to the bonus; at the drain the score
    grows by bonus × multiplier; multiplier and bonus are 1 and 0 on the next ball; the multiplier never exceeds 5.
23. **E** Rules: on the example table, the `bank trio` event twice → `multiball 2` fires once; `lanes top` → the
    multiplier +1 and 1 000 points; a rule with `once = 1` fires once in a game though its event comes 3 × `count`
    times; counters survive a drain.
24. **E** Multiball: when it fires, a 2nd ball is auto-launched from the plunger; with 2 balls, losing one keeps
    the ball number; losing both ends the ball (one ball lost per ball, bonus once); `multiball 3` gives 3 balls;
    firing it during multiball adds none.
25. **E** Extra ball: after `extraball`, the next drain keeps the ball number (*Shoot again*), once per award.
26. **E** Tilt: 2 nudges within 5 s → a warning, no tilt; a 3rd within the same 5 s → tilt: flipper keys move no
    flipper, bumpers score 0, no bonus at the drain; the next ball's flippers work. 3 nudges spread over 15 s → no
    tilt. A nudge changes the ball's velocity by 150 units/s (± 1).

**High scores**

27. **E** Top 5: on an empty `scores.ini`, 6 games scoring 10, 50, 30, 50, 20, 5 → the table's section holds, in
    order, 50 (the first), 50, 30, 20, 10; a 6th lower score is not added; the file written and read back gives
    the same lines; a name with `=` is stored with a space; a broken line is skipped and the others kept; unknown
    sections survive a write.

**Simulator (S)**

28. **S** `fakekapi.cpp` gains the script commands `hold <key>` and `release <key>` (a key code or a character),
    and its `key_held` answers them; the existing `shots.sh` scenarios give the same screenshots as before.
29. **S** `shots.sh pinball` produces at least: `screenshots/pinball.png` (the picker: 3 tables, the chosen one's
    thumbnail, goal and top 5 from a `SIM_OVERLAY` fixture `scores.ini`), `screenshots/pinball-play.png` (*Volcano*
    or *Space Station* in play: the ball, a raised flipper — a `hold` — lit lamps, the score panel),
    `screenshots/pinball-multiball.png` (2+ balls and the *MULTIBALL!* message — reached with a script or a test
    start state the Technical Analyst defines), `screenshots/pinball-fr.png` (the picker in French; no word cut or
    overflowing).
30. **S** `pinball <path>` (via `SIM_ARGS`) of the example table starts it at once; of a broken table shows the
    picker with that table greyed and its `line <n>: <reason>`.
31. **S** A scripted game reaching a top-5 score writes `scores.ini` (seen in `SIM_WRITES`) with the section and the
    line of §7.2.

**Build, tools, docs (B)**

32. **B** `sh tools/tests/run_pinball_test.sh` passes (criteria 1–27), the same result on every run.
33. **B** `python tools/lang/check.py pinball` reports 0 missing.
34. **B** `make` from `kernel/` builds `pinball` (in `user/Makefile`), `make stage` puts `SD:/apps/pinball.app/main`;
    `app.txt` (`name = Pinball`, `category = Games`, `opens = table`), `icon.bmp`, `tables/` (3 files),
    `lang/fr.txt` are in `sdcard/apps/pinball.app/`; `sdcard/etc/fileassoc.ini` has `table = pinball`;
    `tools/pkg/packages.ini` has `[app.pinball]` with `opens = table` and its `needs` (not published).
35. **B** Every new source file carries the MIT notice; no kernel / kapi / AppKit change (`kapi_abi.h`,
    `appkit.abi` untouched); the app reaches the system only through the kits (AppKit, UIKit, FileKit, AudioKit
    via `game.h`).
36. **B** Docs: docs/04 §12 catalog row (Games) and a *Pinball* section (controls §10, rules, the three tables, the
    files, the `.table` format with the example and the errors, the screenshots), the translated-apps list,
    `docs/HANDOFF.md`, `IDEAS.md`'s *Pinball* row marked done, `python docs/build_docs.py` run.
37. **B** The other games unchanged: `shots.sh invaders` (and the other game scenarios) give the same screenshots;
    `games/game.h` changes, if any, are additions only.
