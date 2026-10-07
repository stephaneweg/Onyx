# AutoDev round 5 — Product Analyst: Critters

Date: 2026-10-07. Input: `01-product-manager.md` (the pick: the queue's item 4, a Lemmings-like; its MUST / SHOULD /
deferred scope is kept — what this document adds to it is only what the existing code or testability makes
necessary, flagged *(analyst)*).

Looked at: `CLAUDE.md` (the kits-first and EN/FR rules), `autodev/PIPELINE.md` §2, `autodev/rounds/04-pinball/
02-product-analysis.md` (the model of this document), `user/Apps/pinball/` (the headless core `table / physics /
rules / scores` apart from the UI headers `draw.h / panel.h / picker.h`), `sdcard/apps/pinball.app/` (`app.txt`:
`name`, `category = Games`, `opens = table`, `stack`; `tables/*.table` as `fk_kv` text with `[shape]` polygons),
`user/Apps/games/game.h` (`GameView` — `paint`, `press/release/move`, `key`, `tick`; `GameRoot` → `step ()`;
`sfx*`, `rng_seed / rng_n`, `gtext*`, `gitoa`), `user/Apps/circuits/` (`progress.ini` over `fk_kv`, levels opened one
after the other), `user/Kits/filekit/kvtext.h` (`fk_kv_parse / load / get / set / block / block_line / line / save`,
`FK_KV_ESCAPES`), `kernel/gui/kwin.cpp` `UsageToKey` (the keys that can be **held**: letters, digits, Space, Enter, Esc,
the arrows), `sdcard/etc/fileassoc.ini` (**`.level` is free**), `tools/pkg/packages.ini` (`[app.pinball]`), the
`sdcard/apps/*/app.txt` files (**no per-language `name`**: the launcher shows one name), `tools/tests/desktop_sim/
shots.sh` (`SIM_ARGS`, `SIM_OVERLAY`, `SIM_WRITES`; `hold` / `release` since round 4), `docs/04-USER-GUIDE.md` §12 (the
games' rows, the *Pinball* section).

---

## 1. The name

**Critters** — kept from the PM: plain, short, says "small creatures", not an existing Onyx app, not a trademark.

| Where | English | French |
|---|---|---|
| The app (`app.txt` `name`, the dock's Games drawer, `run critters`, the folder `user/Apps/critters/`, `SD:/apps/critters.app/`) | **Critters** | **Critters** — `app.txt` has no per-language name: the launcher shows one name for all (as *Pinball*) |
| The window title, the picker's heading, the About box (`TR`) | *Critters* | ***Bestioles*** |
| The creatures in the game's words | *critter(s)* | *bestiole(s)* |

So a French player sees *Critters* in the dock and *Bestioles* in the game; a per-language app name in `app.txt`
(`name.fr`) is a system matter, **later** (§4, LATER), not this round.

## 2. The one-line pitch

**Critters: lead a stream of little creatures across a diggable world to the exit, giving each one a job — climb,
float, block, build, dig, explode — before the clock runs out.**

## 3. The users and their tasks

| User | Task | How Critters serves it |
|---|---|---|
| A player (child or adult) | solve a puzzle level, then the next | a level picker with two packs, levels opened one after the other, a tick and the best result on the solved ones |
| A beginner | learn the roles one at a time | the *Training* pack: one role a level, a hint card shown at the start (EN / FR) |
| A seasoned player | a harder challenge, a better result | the *Expedition* pack (mixed roles, tight counts); the best saved count and best time kept per level |
| A French-speaking player | play in their language | every word, the levels' names and hints in French when `language = fr` |
| A maker | write a level in a text editor and play it | the `.level` text format (§7.1), terrain drawn from shapes; an error names the line; `SD:/docs/critters/*.level` listed on the picker; a double click on a `.level` plays it |
| The user (the reviewer) | see that the game works without a Pi | a deterministic headless host test (every rule, every shipped level won by its recorded solution), simulator screenshots in EN and FR |

## 4. Features

### MUST (this round)

**4.1 The world — the core, headless** (beside the app, e.g. `user/Apps/critters/{terrain,world,level,solution,
progress}.{h,cpp}`; no UIKit, no AppKit call in it, so the host test links it alone).

1. **Units**: **logical pixels** (*px*), origin top-left, x right, y down; the level's `size` in px. Time in
   **steps**: **1 step = 1/20 s (50 ms)**; the file gives times in seconds (× 20 = steps). Integer arithmetic only
   (no float in the simulation).
2. **Fixed step, deterministic**: the world advances one step at a time; the game runs one step every 50 ms of
   wall time (3 steps per 50 ms when fast-forwarded), at most 4 steps caught up per frame (a late frame slows the
   game, it never skips). **The same level and the same list of assignments (§7.3) give the same run, bit for
   bit** — terrain, creatures, counts, result — on the PC and on the Pi. No random number is used by the rules
   (the drawing's texture noise is seeded from the level, §7.1.3; it does not affect the simulation).
3. **The terrain**: one byte a pixel — `empty`, `earth`, `steel` (never removed), `water`, `lava` (hazards, never
   removed); width 320…1600, height 100…160 (§7.1.2). Built at load from the level's `[shape]` blocks in file order
   (§7.1.3). **Outside the map**: the columns left of 0 and right of the width count as steel (side walls); the
   row above 0 counts as steel (a ceiling); below the height is the void (a creature whose feet go below dies).
   Digging and exploding change `earth` to `empty`; building changes `empty` to `earth`. A **colour layer** beside
   it (one colour a pixel, from the shapes' colours and textures, the bricks' colour) is updated with every change,
   and only the changed rectangle is redrawn.

**4.2 The creatures** (the constants below are the contract the tests check; the Technical Analyst may tune them
only by changing this table and the tests together).

4. **A creature**: its feet point `(x, y)` = the empty pixel it stands in (it stands when the pixel below, `y + 1`, is
   solid — earth or steel); its body is the box `x−4 … x+4`, `y−11 … y` (for the cursor, §4.4, not for collisions);
   its direction (left / right), state, animation frame, permanent abilities (climber, floater), exploder countdown.
   At most **80** a level.
5. **Hatches**: a level has 1…4 `[hatch]`es. The first creature drops **40 steps (2 s)** after the start (the hatch
   opening); then one every *interval* steps, the hatches taking turns in file order, until `count` have come out.
   *interval* = `4 + (99 − rate) × 40 / 98` (integer division): rate 99 → 4 steps, rate 50 → 24, rate 1 → 44. A
   new creature appears at the hatch's point, falling, facing the hatch's `dir`.
6. **Walking** (1 px a step): at the next column, if the pixel at the feet' height is solid, the creature **steps up**
   to the first empty pixel at most **6 px** higher; a higher wall → it **turns** (a climber climbs it, below); if the
   ground falls away, it **steps down** at most **3 px**; deeper → it **falls**.
7. **Falling** (3 px a step): a fall of more than **60 px** (from where it started to where it lands) **kills**
   (a splat); 60 or less: it walks on.
8. **Hazards**: a creature whose feet pixel, or the pixel below it, is `water` or `lava` dies (drowns / burns).
   Going below the map's bottom: dies. A dead creature plays a short animation (≤ 16 steps) and is removed.
9. **Exits**: a level has 1…4 `[exit]`s. A creature (walking, falling, climbing; not a blocker, not mid-explosion)
   whose feet come within **2 px horizontally and 4 px vertically** of an exit's point enters it (8 steps) and is
   **saved**.

**4.3 The roles** — each level gives a count of each (0…99); a role is given to one creature at a time (§4.4).

10. **Climber** (permanent): instead of turning at a wall higher than 6 px, climbs it at 1 px a step; at its top it
    steps over and walks on; with something solid above its head while climbing, it lets go, turns round and falls.
11. **Floater** (permanent): falls at 3 px a step for the first 12 px, then opens its leaf and falls at 1 px a step;
    **never dies of a fall**. A creature can be climber and floater both.
12. **Blocker**: stops and stands; a walking or climbing creature whose feet come within **6 px horizontally and
    10 px vertically** of a blocker, moving towards it, turns round. A blocker whose ground is removed falls (and
    is a walker again on landing). It is freed only by an explosion (its own or the nuke).
13. **Builder**: lays a **brick every 8 steps** — 6 px wide × 2 px high, at its feet' level, ahead of it — then
    moves 3 px forward and 2 px up: a stair rising 2 px for every 3 px; **12 bricks** (the last 3 with a warning
    sound), then shrugs (10 steps) and walks on. Before each brick, a solid pixel ahead at head height (the column
    3 px ahead, from `y−4` to `y−9`) stops it: it turns round and walks. A builder can be given *Builder* again to
    carry on (another 12).
14. **Digger**: every 2 steps clears the row below its feet over 9 px (`x−4 … x+4`) of earth and moves down 1 px;
    a steel pixel in that row stops it (it walks on); no earth to dig under it (it has dug through) → it falls.
15. **Exploder**: a **100-step (5 s) countdown** shown above its head (5, 4, 3, 2, 1) while it goes on with what it
    was doing (falling, walking, building…); at 0 it bursts: the earth inside a **disc of radius 12 px** centred
    on `(x, y−4)` is removed (never steel, never a hazard), and the creature dies (it counts as lost).
16. **Give a role** — who can take what: *Climber* / *Floater*: any living creature that does not have it yet and is
    not leaving; *Blocker* / *Builder* / *Digger*: a creature on the ground (walking, or at work as a builder,
    digger or — for *Builder* — shrugging), not a blocker; *Exploder*: any living creature not already counting
    down and not leaving. A creature that cannot take the role **refuses** it (a short "no" sound), the count is
    unchanged; a role at 0 cannot be chosen. A role given takes one from its count.
17. **All explode** (the nuke): no more creatures come out; every creature still in play gets an exploder
    countdown, one more per step, in the order they came out (it takes no exploder count). Asked to confirm (§10).

**4.4 Playing — the screen and the controls** (one fixed-size window; the UX designer sizes it — it must fit a
1280 × 800 screen with its frame and menu bar; the terrain drawn at an integer scale, ×2 suggested, so a level's
full height of up to 160 px is always visible: **no vertical scroll**).

18. **The play area**: the terrain, the hatches (closed, opening, open), the exits, the creatures as **our own
    sprites** (§13), an explosion's particles, the exploders' countdowns; the creature under the pointer
    highlighted. Wider than the area → **horizontal scroll**: ← / → held (4 px a step… the UX sizes it), the
    pointer within 8 screen px of the area's left or right edge, a click or drag on the minimap. The view starts
    at the level's `start` (default: centred on the first hatch).
19. **The skill bar** (bottom): **8 role slots** (Climber, Floater, Blocker, Builder, Digger, Exploder, then
    Basher and Miner — the last two greyed with a dash unless built, SHOULD 1), each with its icon, its count and
    its key (1…8); release rate **−** / **+** with the current rate (between the level's `rate` and 99: never below
    the level's); **pause**; **fast-forward**; **all explode**; the **minimap** (the whole terrain scaled, the
    creatures as dots, the visible frame as a rectangle). The chosen role is outlined.
20. **The HUD** (one line above or inside the bar): the creature under the pointer (its role or state, and how many
    are under the pointer), **Out** (in play), **Saved** *n* / **Needed** *m*, **Time** *m:ss* (counting down).
21. **Giving a role with the mouse**: choose a slot (a click, or a key 1…8), then click a creature. Under the
    pointer, the candidates are the creatures whose body box (§4.2) holds it; among them, the first that can take
    the role (§16) and does not already have it, else the one nearest the pointer (it then refuses). **With the
    keyboard** *(analyst — the PM's "keyboard for everything")*: **Tab** / **Shift+Tab** highlight the next /
    previous creature in view (from left to right; the view follows), **Enter** gives it the chosen role.
22. **The hint card**: a level with a `hint` starts paused with a card showing the level's name, *Save n of m*, the
    time, the hint; any key or click starts it. Without a hint: the same card without the hint line.
23. **Pause** (P, Space, the bar's button, *Game ▸ Pause*): the world frozen, the view can still scroll, roles can
    still be **chosen** but not given. **Fast-forward** (F, the bar's button): 3 steps per 50 ms, until pressed again.
24. **The end**: the level ends when every creature has come out and each one is saved or dead, or when **the time
    runs out** (the creatures still in play are then lost), or after the nuke's last burst. **Won** when saved ≥
    `save`. The end screen: *Level complete!* / *Not enough critters saved*, **Saved** *n* (and *n %*), **Needed** *m*,
    **Time** taken, *New best!* when it beats the progress file; buttons **Retry**, **Next** (won, and a next level
    exists) and **Levels**.

**4.5 The levels, the progress**

25. **The level format** (§7.1), read with FileKit's `fk_kv`. A bad file is **refused with its line and reason**:
    never a crash, never a half-built level.
26. **12 levels in 2 packs** (§6), `sdcard/apps/critters.app/levels/`: **Training** (6, one role taught each, with a
    hint) and **Expedition** (6, roles mixed, harder). Plus the player's `SD:/docs/critters/*.level` as a third
    group, *My levels*, all open.
27. **The level picker** (at start, after *Levels*, on Esc → *Levels*): the packs and their levels (number, name in
    the system's language, a tick on the solved ones, a lock on the closed ones, the best result), the chosen one's
    **preview** (the terrain scaled, the hatches and exits), *Save n of m*, the roles it gives (icons + counts), its
    best result, **Play**. ↑ / ↓ choose, Enter / double click plays. **Opening rule**: within a pack the first
    level is open, each next one opens when the one before is solved; *Expedition* 1 opens when *Training* 6 is
    solved; the player's levels are always open. A player's level that fails to load is listed **greyed with its
    error** (*"line 14: unknown block [shap]"*) and cannot be played. The level last played is chosen at start.
28. **Progress**: `SD:/apps/critters.app/progress.ini` (§7.2), written with `fk_kv` at the end of each won level
    (and when the sound setting changes): solved, the most saved, the best time.
29. **Command line** *(analyst — the simulator's shots and the reviewer need it; cheap given determinism)*:
    `critters` → the picker; `critters <file.level>` → that level at once (also on the picker for this run);
    `critters <file.level> --replay <file.sol> [--until <step>]` → plays the solution's assignments on the clock
    (the player may watch, pause, scroll) and, with `--until`, pauses at that step. A bad `.sol` → a message with
    its line, then the level normally.

**4.6 Sound, language, packaging**

30. **Sounds** (`sfx_*`, `game.h` → AudioKit): the hatch opening, a creature coming out (soft), a role given, a role
    refused, the builder's last 3 bricks, a splat, a drowning, the countdown's ticks and the burst, a creature
    saved, win / lose jingles (`sfx_win` / `sfx_lose`); **M** or *Game ▸ Sound On / Off* mutes, remembered in
    `progress.ini` `[settings]`.
31. **English and French** (§11).
32. **The host test and the simulator** (§12).
33. **Packaging and docs**: `user/Makefile`, `sdcard/apps/critters.app/` (`app.txt`, `icon.bmp`, `levels/` (12),
    `lang/fr.txt`), `sdcard/etc/fileassoc.ini` `level = critters`, `tools/pkg/packages.ini` `[app.critters]`
    (declared, **not published** — PIPELINE §0.5), docs/04 §12 (catalog row) and a *Critters* section (the rules,
    the roles, the controls, the files, **the `.level` format** with the example and the errors, the screenshots),
    the translated-apps list (docs/04 *Language & Region*), `docs/HANDOFF.md`, `IDEAS.md`'s row,
    `python docs/build_docs.py`.

### SHOULD (in this order, if time allows)

1. **Basher** (digs horizontally: every 2 steps clears a 2 px × 10 px slice ahead and moves 2 px; stops at steel or
   when there is no earth ahead within 8 px) and **Miner** (digs diagonally down: every 3 steps clears a 6 × 6 px
   square ahead-below and moves 2 px forward, 1 px down; stops at steel). The format reads `basher` / `miner` counts
   from the start (§7.1.2); the bar has their slots (19).
2. **The level checker**: `critters --check <file.level>` and *Game ▸ Check a Level…*: the terrain drawn still,
   the hatches / exits / shapes numbered, **every** error (not just the first) listed; if `<file>.sol` lies beside
   it, it is replayed headless and the result shown (*"won: 18 of 20 saved, 2:41"*). A commented sample
   `SD:/docs/critters/my-first-level.level` (§7.1.4) to start from.
3. **Gamepad** (`gamepad.h`): the d-pad moves a cursor (the view follows), **A** gives the role, **L / R** choose the
   role, **B** fast-forward, **Start** pause, **Select** levels.
4. **Replay of the player's best**: the winning run's assignments kept in `progress.ini` (`replay`, §7.2) and
   *Watch the best* on the picker (the same player as 29).
5. A shared progress / scores helper in FileKit, **only** if the Technical Analyst finds Pinball, Circuits and
   Critters would share it cleanly; else beside the app (kits-first: reusable code in a kit, one-user code beside
   the program).

### LATER (deferred, as the PM sized them)

A graphical level editor (paint the terrain, place hatches / exits, test-play); terrain from a picture (a PNG as the
colour and earth layers via ImageKit); traps, one-way walls, teleporters, moving parts, several hatch colours, two
players; more packs (30+ levels), music; a per-language app name in `app.txt` (`name.fr`, read by the dock and the
launcher — a system change); the speed and feel tuned on the real Pi; vertical scrolling for taller levels.

## 5. What it does NOT do (this round)

- No level editor inside the app (text format + the SHOULD checker); no level downloaded from the net (a `.level`
  comes by any means — the package manager, a share, a USB key).
- No vertical scroll, no zoom: the level's full height is always visible.
- One player; no online scores; no notification; no clipboard use.
- Writes only `progress.ini` (never a level, nothing in `SD:/docs/critters`).
- No kernel, kapi or AppKit change; no new kit (unless SHOULD 5 is judged worth it).
- No traps, no one-way walls, no steel that can be dug, no terrain from a picture.
- **Nothing from Lemmings**: no name, sprite, level, sound, music or text of it (Lemmings is a trademark); the
  roles have ordinary words; our creatures look like nothing of theirs (§13).

## 6. The twelve levels

Shipped in `SD:/apps/critters.app/levels/`, named `<pack>-<nn>-<slug>.level` — the pack id is the part before the
first `-` (`training`, `expedition`), the order is `<nn>`. All 160 px high; the counts below are the target the
Technical Analyst / UX designer refine while drawing them (likely generated by a script, as Pinball's
`tools/pinball/mktables.py`), each checked by a recorded solution (AC-30).

| # | File | Name (EN / FR) | Idea | Width | Count / save | Time | Roles given |
|---|---|---|---|---|---|---|---|
| T1 | `training-01-straight-down` | Straight Down / *Tout droit vers le bas* | the exit lies under a thick floor | 400 | 10 / 8 | 3:00 | digger 3 |
| T2 | `training-02-mind-the-gap` | Mind the Gap / *Attention à la marche* | a ravine between two cliffs | 480 | 10 / 8 | 3:00 | builder 4 |
| T3 | `training-03-hold-the-line` | Hold the Line / *Tenir la ligne* | a lake of water past the exit: stop them going on | 480 | 10 / 6 | 3:00 | blocker 2, digger 1 |
| T4 | `training-04-up-the-wall` | Up the Wall / *Grimper au mur* | a tall wall before the exit | 480 | 10 / 10 | 3:00 | climber 10 |
| T5 | `training-05-soft-landing` | Soft Landing / *Atterrissage en douceur* | a deep drop to the exit | 400 | 10 / 10 | 3:00 | floater 10 |
| T6 | `training-06-blast-through` | Blast Through / *Ouvrir la voie* | an earth wall too high to climb, too far to bridge | 480 | 10 / 9 | 3:00 | exploder 2 |
| E1 | `expedition-01-two-ways` | Two Ways / *Deux chemins* | two hatches, both must be routed | 800 | 20 / 16 | 4:00 | builder 4, digger 2, blocker 2 |
| E2 | `expedition-02-steel-floor` | Steel Floor / *Plancher d'acier* | dig where there is no steel only | 800 | 20 / 15 | 4:00 | digger 3, builder 3, exploder 2 |
| E3 | `expedition-03-the-climb` | The Climb / *L'ascension* | climb, then float down the far side | 960 | 20 / 14 | 5:00 | climber 6, floater 6, builder 2 |
| E4 | `expedition-04-lava-lake` | Lava Lake / *Lac de lave* | bridge a lava lake, block the stragglers | 1120 | 30 / 24 | 5:00 | builder 8, blocker 2, digger 1 |
| E5 | `expedition-05-the-maze` | The Maze / *Le labyrinthe* | caves and tunnels, every role once | 1280 | 40 / 30 | 6:00 | 2 of each of the six |
| E6 | `expedition-06-grand-tour` | Grand Tour / *Le grand voyage* | the whole width, two exits, a fast release | 1600 | 60 / 50 | 7:00 | climber 4, floater 4, blocker 2, builder 10, digger 4, exploder 3 |

Each Training level **cannot be won without its role** (the only way past is the role taught) — checked by "lost
when nothing is done" (AC-31). Every shipped level has `name.fr` and `hint.fr`; the Training levels' hints teach the
role (*"Click Digger, then click a critter standing on the floor."*).

## 7. Files read and written

| File | Read / written | Format |
|---|---|---|
| `SD:/apps/critters.app/levels/*.level` | read | §7.1 — the shipped levels (in the package) |
| `SD:/docs/critters/*.level` | read | §7.1 — the player's, listed as *My levels* (the folder may not exist) |
| a `.level` given as argument (`critters <path>`, a double click) | read | §7.1 — played at once |
| a `.sol` given with `--replay` (and, SHOULD, `<level>.sol` beside a checked level) | read | §7.3 |
| `SD:/apps/critters.app/progress.ini` | read and written | §7.2 — not shipped (made on the first win), never overwritten by an update |
| `SD:/apps/critters.app/lang/fr.txt` | read | UIKit's catalogue (`English<TAB>French`) |
| `SD:/etc/system.ini` `language=` (via UIKit), `SD:/etc/gamepad.ini` (SHOULD, via `gamepad.h`) | read | the system's |
| `tools/tests/critters/solutions/<level base name>.sol` | read by the host test | §7.3 — one per shipped level (not on the card) |

### 7.1 The level format (`*.level`) — version 1

A UTF-8 text file read by FileKit's `fk_kv_parse` (flags 0): `#` / `;` comment lines, blank lines ignored,
`[block]` headers, `key = value` lines; a block name may come many times (each header is one element). The game
then checks every block — **a file that breaks a rule below is refused with `line <n>: <reason>`** (the line of the
offending key, or the block's header for a missing key; §7.1.5).

#### 7.1.1 The values

```
int      := ["-"] digits                                  (no fraction: the terrain is pixels)
point    := int SEP int                                   x y, in px
points   := point { SEP point }                           SEP = spaces and/or one comma
colour   := "#" 6 hex digits                              #RRGGBB
text     := any characters to the end of the line         (trimmed)
word     := one of the listed words, lower case
```

Unknown **keys** in a known block are ignored (a later version's keys load); an unknown **block** is an error.
Points: `0 ≤ x < width`, `0 ≤ y < height` for hatches and exits; a shape may reach outside the map (it is clipped).

#### 7.1.2 The blocks and their keys (R = required; the default otherwise)

| Block | Key | Meaning |
|---|---|---|
| `[level]` (exactly one, the first) | `format` R | `1` |
| | `name` R, `name.fr` | the level's name (≤ 32 characters) |
| | `hint`, `hint.fr` | shown on the start card (≤ 160 characters) |
| | `size` R | `width height` — width 320…1600, height 100…160 |
| | `count` R | creatures that come out, 1…80 |
| | `save` R | creatures to save to win, 1…`count` |
| | `time` R | the time limit in seconds, 30…1200 |
| | `rate` | the release rate, 1…99 — 50 (the player may raise it, not lower it) |
| | `climber`, `floater`, `blocker`, `builder`, `digger`, `exploder` | the role counts, 0…99 — 0 |
| | `basher`, `miner` | as above — 0; a count > 0 is refused (`role <name> is not available`) by a build without them (SHOULD 1) |
| | `start` | the view's left edge at the start, px — centred on the first hatch |
| | `background` | the sky's colour — `#101830` |
| | `brick` | the builders' bricks' colour — `#C8A060` |
| `[shape]` (0…256, drawn in file order: a later one paints over an earlier one) | one of `rect` / `points` / `circle` R | `rect = x y w h` (top-left, size, w and h ≥ 1); `points = …` a filled polygon, 3…64 points (even-odd rule); `circle = cx cy r` (r 1…400) |
| | `material` | `earth` (default), `steel`, `water`, `lava`, or `erase` (back to empty: holes, caves) |
| | `colour` | the fill — by material: earth `#8A5A34`, steel `#8890A0`, water `#3070D0`, lava `#E05020` (ignored for `erase`) |
| | `texture` | `plain` (default), `speckle`, `stripes`, `bricks` — drawn with `colour2` |
| | `colour2` | the texture's second colour — `colour` darkened by a quarter |
| `[hatch]` (1…4) | `at` R | the point where creatures appear (the hatch drawn above it) |
| | `dir` | `left` or `right` — `right` |
| `[exit]` (1…4) | `at` R | the exit's point: the threshold, at ground level (the door drawn above it) |
| `[label]` (0…32) | `at` R, `text` R, `text.fr` | painted words on the terrain (decoration, centred on `at`) — `colour` (`#FFFFFF`) |

**Limits**: a file ≤ 64 KB; 256 shapes; 64 points a polygon; 4 hatches, 4 exits, 32 labels.

#### 7.1.3 How the terrain is made

1. All pixels `empty`, colour = `background`.
2. Each `[shape]` in order: every pixel inside it (a rectangle's pixels `x…x+w−1`, `y…y+h−1`; a polygon's by the
   even-odd rule at pixel centres; a circle's pixels with `(px−cx)² + (py−cy)² ≤ r²`) takes the shape's material
   (`erase` → `empty`) and its colour — `plain`: `colour`; `speckle`: `colour2` on about 1 pixel in 6 (a hash of
   the pixel's position and the shape's number — fixed, not `rng`); `stripes`: `colour2` on every 4th row;
   `bricks`: `colour2` mortar lines of a 8 × 4 px brick pattern.
3. A pixel dug or burst → `empty`, the background colour; a brick → `earth`, the `brick` colour (light top row).
4. The hatches' and exits' drawings are not terrain (creatures pass through them; only the terrain collides).

#### 7.1.4 An example — `my-first-level.level`

```ini
# My first level -- dig through the floor, bridge the gap.
[level]
format     = 1
name       = My First Level
name.fr    = Mon premier niveau
hint       = Dig down through the floor, then build across the gap.
hint.fr    = Creusez le sol, puis construisez un pont au-dessus du trou.
size       = 480 160
count      = 10
save       = 7
time       = 180
rate       = 50
digger     = 2
builder    = 3
background = #142040

# the ground, the upper floor, a ravine
[shape]
rect    = 0 120 200 40
texture = speckle
[shape]
rect    = 240 120 240 40
texture = speckle
[shape]
rect    = 0 70 200 12
colour  = #6E8A3C
[shape]
points  = 200 160, 200 120, 210 140, 230 140, 240 120, 240 160
material = lava

# a steel post the diggers must avoid, and a cave under it
[shape]
rect     = 120 70 16 12
material = steel
[shape]
circle   = 60 140 10
material = erase

[hatch]
at  = 40 60
dir = right

[exit]
at  = 440 119

[label]
at      = 100 150
text    = DIG
text.fr = CREUSEZ
```

#### 7.1.5 The errors (the reason after `line <n>: `; translated with `TR`, the tests compare the English)

| Case | Reason |
|---|---|
| a block not in §7.1.2 | `unknown block [<name>]` |
| no `[level]`, not the first block, or two | `[level] must be the first block, once` |
| a required key missing | `[<block>] needs <key>` (the block's header line) |
| a `[shape]` with none or more than one of `rect` / `points` / `circle` | `[shape] needs one of rect, points, circle` |
| a value that does not read (count of numbers, not an int, not a colour, a word not listed) | `bad value for <key>` |
| out of its range; `save` > `count`; a hatch or exit outside the map | `<key> out of range` |
| no hatch / no exit / more than 4 | `the level needs 1 to 4 [hatch]` / `the level needs 1 to 4 [exit]` (line of `[level]`) |
| a hatch or an exit inside solid terrain (its point not `empty` once the terrain is made) | `[hatch] is inside the terrain` / `[exit] is inside the terrain` |
| `basher` / `miner` > 0 in a build without them | `role <name> is not available` |
| over a limit | `too many <shapes / points / labels> (max <n>)` |
| the file > 64 KB, unreadable | `the file is too big` / `cannot read the file` (line 0) |

### 7.2 `progress.ini` — written by the game (`fk_kv`, `FK_KV_ESCAPES`)

```ini
# Critters -- progress (written by the game)
[settings]
sound = 1
last  = training-03-hold-the-line

[training-01-straight-down]
solved = 1
saved  = 10
time   = 74

[user.my-first-level]
solved = 1
saved  = 8
time   = 131
```

- A section per level: the shipped ones by their file's base name; any other (`SD:/docs/critters/`, an argument)
  as `user.<base name>`.
- `solved` = 1 once won; `saved` = the most creatures saved in a won run; `time` = the shortest winning time in
  seconds (independent of `saved`); a lost run writes nothing. SHOULD 4: `replay` = the best run's assignments
  (§7.3 lines, `\n`-escaped), the run with the most saved, then the shortest.
- `[settings] sound` = 0 muted; `last` = the level last played (chosen on the picker at start).
- A line that does not read is ignored (the file is never refused); unknown sections and keys are kept on write.

### 7.3 The solution / replay format (`*.sol`)

Plain text, one assignment a line, `#` comments and blank lines ignored:

```
# training-02-mind-the-gap: 10 out, 8 needed
60   builder  0          # at step 60, creature 0 (the first out) becomes a builder
156  builder  0
300  rate     80         # the release rate set to 80
410  digger   3
900  nuke                # all explode
```

- `<step> <action> [<arg>]`: `step` = the world's step at which it is applied (before that step runs), ascending;
  `action` = a role word (`climber floater blocker builder digger exploder basher miner`, the creature's number as
  `arg` — 0 = the first out), `rate <1…99>`, `nuke`, `pause` / `fast` (ignored by the simulation, kept for a
  watcher). The player's own clicks are recorded in the same form (by creature number), so a replay is exact.
- A line that does not read → `line <n>: bad step` / `bad action` / `bad creature`; an assignment the creature
  refuses at replay time is counted as **refused** (the test reports it — a solution must have none).

## 8. File associations

- **New**: `sdcard/etc/fileassoc.ini` `level = critters` (under a `# Critters` comment), `app.txt` `opens = level`,
  `[app.critters] opens = level` — a double click on a `.level` in the File Viewer plays it. *(`.level` is free: no
  other app claims it.)* *Open With ▸ Tinypad* edits it.
- `.sol` is **not** associated (a maker's and the tests' file).

## 9. How it fits with the other apps

- **The games**: the same frame (`games/game.h`: `GameView` / `GameRoot`, `sfx_*`), the *Game* menu's habits (*Pause
  P*, *Sound On / Off*, *New Game* → here *Restart Level* **Ctrl+R**), the dock's **Games** drawer; the level-as-text
  and picker idea of **Pinball** (its picker's player's-folder and refused-file behaviour reused in spirit), the
  packs and progress of **Circuits** (`progress.ini`, levels opened one after the other).
- **Tinypad** edits the levels; the **File Viewer** opens them (§8); `SD:/docs/critters/` is the player's folder.
- **Language & Region**: follows `language=`; joins the translated-apps list in docs/04.
- **Package Manager**: `[app.critters] needs = uikit >= …, audiokit >= …, filekit >= …, fontkit >= …` (the versions
  the Technical Analyst confirms, as `[app.pinball]`), `opens = level`; `apps/critters.app/` packaged whole by the
  `[*apps]` rule; no user file shipped (`progress.ini` is made on the Pi). Declared, not published.
- No notification, no clipboard, no drag and drop (LATER: dropping a `.level` on the window), no change to another
  app's files; no change to `games/game.h` except additions.

## 10. Controls

| Action | Keyboard | Mouse | Menu |
|---|---|---|---|
| Choose a role | **1**…**8** | click its slot | |
| Give the role | **Tab** / **Shift+Tab** highlight a creature, **Enter** | click the creature | |
| Release rate − / + | **−** / **+** (also `=`) | the bar's − / + | |
| Pause / resume | **P** or **Space** | the bar's ⏸ | *Game ▸ Pause* |
| Fast-forward on / off | **F** | the bar's ⏩ | *Game ▸ Fast Forward* |
| All explode | **N**, then **N** again (or **Enter**) within 2 s to confirm; any other key cancels | the bar's button, then *Yes* | *Game ▸ All Explode* |
| Scroll | **←** / **→** held | the pointer at the area's edge; click / drag the minimap | |
| Restart the level | **Ctrl+R** | the end screen's *Retry* | *Game ▸ Restart Level* |
| Back to the levels | **Esc** (in play: pause, *Resume / Restart / Levels*) | the end screen's *Levels* | *Game ▸ Levels…* |
| Sound on / off | **M** | | *Game ▸ Sound On / Off* |
| Picker: choose / play | **↑ ↓**, **Enter** | click / double click, *Play* | |
| Start card, end screen | any key / **Enter** = Next or Retry | click | |
| How to play, About | **F1** | | *Help ▸ How to Play*, *About Critters* |

(Held keys used: ← / → only — both are reported held by the system, `kwin.cpp` `UsageToKey`.)

## 11. English and French

- Every word on screen in `TR ("...")` (`uikit/lang.h`), `uk_lang_init ()` in `main` after the text face; the French
  in `sdcard/apps/critters.app/lang/fr.txt`; **`python tools/lang/check.py critters` → 0 missing**.
- The levels' texts: `name.fr`, `hint.fr`, `text.fr` used when the language is `fr` (the English one when absent).
  All 12 shipped levels have `name.fr` and `hint.fr`.
- The words: *Climber* → *Grimpeur*, *Floater* → *Planeur*, *Blocker* → *Bloqueur*, *Builder* → *Bâtisseur*,
  *Digger* → *Creuseur*, *Exploder* → *Artificier*, *Basher* → *Perceur*, *Miner* → *Mineur*, *All explode* → *Tout
  faire sauter*, *Release rate* → *Débit*, *Fast forward* → *Accéléré*, *Out* → *Dehors*, *Saved* → *Sauvées*,
  *Needed* → *Requises*, *Time* → *Temps*, *Training* → *Entraînement*, *Expedition* → *Expédition*, *My levels* →
  *Mes niveaux*, *Level complete!* → *Niveau réussi !*, *Not enough critters saved* → *Pas assez de bestioles
  sauvées*, *New best!* → *Nouveau record !*, *Retry* → *Rejouer*, *Next* → *Suivant*, *Levels* → *Niveaux*, the
  window title *Critters* → *Bestioles*; the load errors (§7.1.5) and the menus.
- Never translated: what is stored or compared (block and key names, material and role words in files, `.sol`
  actions, `progress.ini` sections and keys).
- The French screenshots checked: the skill bar, the HUD, the picker and the end screen fit (French is ~20 % longer;
  the bar shows icons and counts, the role's name in the HUD / a tooltip, so its width does not depend on the words).

## 12. Acceptance criteria (testable)

Host test (`tools/tests/critters/critterstest.cpp`, `tools/tests/run_critters_test.sh`, built on the PC against the
core alone; small test levels built in the test from §7.1 text) — **E**; PC desktop simulator (`shots.sh critters`,
`SIM_ARGS` / `SIM_OVERLAY` / `SIM_WRITES`) — **S**; build, tools, docs — **B**. "A step" = 1/20 s of game time.

**Levels and terrain**

- **AC-1 E** The 12 shipped levels load without error; each has `name.fr` and `hint.fr`, 1…4 hatches, 1…4 exits,
  `save ≤ count ≤ 80`, a width 320…1600 and a height ≤ 160; 6 have the file prefix `training-`, 6 `expedition-`;
  the picker's order is `<nn>`.
- **AC-2 E** The §7.1.4 example loads (embedded in the test, or the shipped sample if SHOULD 2 is done).
- **AC-3 E** Each case of §7.1.5 is refused with the stated reason and the right line number (one small broken file
  per case, the example with one line changed); none crashes; a refused file yields no level.
- **AC-4 E** An unknown key in a known block (`sparkle = 1` in `[shape]`) loads.
- **AC-5 E** Terrain building: on a 320 × 100 level, `rect = 10 20 30 5` sets exactly 150 earth pixels at
  x 10…39, y 20…24; a circle `r = 10` sets the pixels with `dx² + dy² ≤ 100` (317 pixels); a triangle polygon's
  pixel count equals the even-odd count at pixel centres; a later `erase` shape empties pixels of an earlier one,
  a later `steel` shape over earth makes steel; a shape reaching past the map is clipped without error.

**Creatures**

- **AC-6 E** Release: with `count = 5`, `rate = 50`, one hatch, the creatures appear at steps 40, 64, 88, 112, 136
  (interval 24); `rate = 99` → interval 4, `rate = 1` → 44; two hatches alternate; the player's rate cannot go
  below the level's `rate` nor above 99.
- **AC-7 E** Walking: on a flat floor a creature moves 1 px a step; it steps up a 3 px step and a 6 px step without
  turning; at a 7 px wall it turns; it steps down a 3 px drop walking; at a 4 px drop it falls.
- **AC-8 E** Falling: a fall of 60 px leaves it alive and walking; 61 px or more kills it; falling speed 3 px a step.
- **AC-9 E** Floater: a floater dropped 150 px lands alive, having fallen at 3 px a step for 12 px then 1 px a step.
- **AC-10 E** Hazards and edges: a creature walking into water dies, into lava dies; one falling below the map's
  bottom dies; at the map's left / right edge it turns (as at a wall); dead creatures count as lost.
- **AC-11 E** Exit: a creature walking onto an exit's point is saved within 8 steps; one passing 3 px beside it
  horizontally (on a shelf 5 px above) is not.

**Roles**

- **AC-12 E** Climber: at a 40 px wall a climber climbs it (1 px a step) and walks on along the top; under an
  overhang it falls back, facing the other way; a non-climber turns at the same wall.
- **AC-13 E** Blocker: walkers coming from both sides turn round within 6 px of it; none passes it in 600 steps; a
  blocker whose floor is dug away falls; an exploder given to it frees the way after its burst.
- **AC-14 E** Builder: on a flat floor it lays 12 bricks of 6 × 2 px, one every 8 steps, each 3 px further and 2 px
  higher (the earth added checked pixel by pixel), then walks on; facing a wall within its path it stops early and
  turns; walkers following it cross a **30 px-wide gap** over a 100 px drop on its stair and reach the far side alive.
- **AC-15 E** Digger: on 20 px of earth over steel it digs down 1 px every 2 steps, a 9 px-wide shaft, and stops
  (walks) on the steel with the steel intact; on 20 px of earth over a cave it digs through and falls.
- **AC-16 E** Exploder: the countdown lasts 100 steps; the burst removes every earth pixel within radius 12 of
  `(x, y−4)` and no steel or hazard pixel; the creature dies; a creature given *Exploder* while falling bursts in
  the air.
- **AC-17 E** Giving roles: a count decreases by one per role given and a role at 0 is refused; *Digger* on a falling
  creature, *Builder* on a blocker, *Climber* twice on the same creature, *Exploder* twice are refused with the count
  unchanged; *Climber* + *Floater* on one creature both hold; the cursor rule of §21 picks, among overlapping
  creatures, the first that can take the role.
- **AC-18 E** All explode: after the nuke no more creatures come out; every creature in play bursts, one more
  starting its countdown each step; the level ends after the last burst; no exploder count is taken.

**The rules and determinism**

- **AC-19 E** Win / lose: a level ends when all creatures are out and each saved or dead; won iff saved ≥ `save`
  (tested at `save − 1`, `save`, `save + 1` saved); the time running out ends it at once with the creatures still in
  play lost, and the result judged the same way.
- **AC-20 E** Determinism: each shipped level replayed twice with its solution gives the same terrain bytes,
  creature states and counts **at every step** (a checksum per step compared); the core uses no `rng`, no clock.
- **AC-21 E** Fast-forward and pause change nothing: the solution replayed with fast-forward on and with pauses
  inserted gives the same result and final checksum.
- **AC-22 E** The `.sol` reader: the §7.3 example reads; a line with a bad step, action or creature number is refused
  with its line; steps out of order are refused (`line <n>: bad step`).

**Progress**

- **AC-23 E** Progress: on an empty `progress.ini`, a won run (8 saved, 120 s) then a won run (9 saved, 140 s) then a
  lost run → the section holds `solved = 1`, `saved = 9`, `time = 120`; written and read back the same; unknown
  sections and keys survive a write; a broken line is ignored.
- **AC-24 E** The opening rule: with nothing solved only *Training* 1 is open; solving *Training* k opens k + 1;
  *Expedition* 1 opens once *Training* 6 is solved; player's levels are open.

**Simulator (S)**

- **AC-25 S** `shots.sh critters` produces at least: `screenshots/critters.png` (the picker: the 2 packs with 12
  levels, some solved from a `SIM_OVERLAY` fixture `progress.ini`, the chosen level's preview and roles);
  `screenshots/critters-play.png` (a level in play: creatures walking, the skill bar with counts, the chosen role
  outlined, the minimap with dots and the frame, the HUD); `screenshots/critters-build.png` (a builder's stair and a
  digger's shaft at work — reached with `--replay … --until`); `screenshots/critters-end.png` (the end screen with
  saved / needed / time); `screenshots/critters-fr.png` (the picker in French) and `screenshots/critters-play-fr.png`
  (the skill bar, HUD and a hint card in French) — **no word cut or overflowing** in either.
- **AC-26 S** `critters <path>` (via `SIM_ARGS`) of the example level starts it at once (its hint card); of a broken
  level shows the picker with that level greyed and its `line <n>: <reason>`.
- **AC-27 S** A level replayed to its end in the simulator (`--replay`) writes `progress.ini` (seen in `SIM_WRITES`)
  with the section and keys of §7.2.
- **AC-28 S** A scripted mouse game in the simulator: click a role slot, click a creature → the count drops by one
  on screen (`click` script commands, a shot before / after or the app's log line).

**Build, tools, docs (B)**

- **AC-29 B** `sh tools/tests/run_critters_test.sh` passes (AC-1 … AC-24 and AC-30 … AC-31), the same result on
  every run.
- **AC-30 E** **Every shipped level is won by its recorded solution**: `tools/tests/critters/solutions/<base>.sol`
  replayed headless reaches saved ≥ `save` before the time limit, with **no refused assignment**; the test prints
  each level's saved / needed / time.
- **AC-31 E** **Every shipped level is lost when nothing is done** (no assignment: saved < `save`); each Training
  level is lost when its taught role's count is set to 0 and the solution's assignments of that role are removed.
- **AC-32 B** `python tools/lang/check.py critters` reports **0 missing**; `fr.txt` holds every `TR` word.
- **AC-33 B** `make` from `kernel/` builds `critters` (in `user/Makefile`), `make stage` puts
  `SD:/apps/critters.app/main`; `sdcard/apps/critters.app/` has `app.txt` (`name = Critters`, `category = Games`,
  `opens = level`), `icon.bmp`, `levels/` (12 files), `lang/fr.txt`; `sdcard/etc/fileassoc.ini` has
  `level = critters`; `tools/pkg/packages.ini` has `[app.critters]` with `opens = level` and its `needs` (declared,
  not published).
- **AC-34 B** Every new source file carries the MIT notice; no kernel / kapi / AppKit change (`kapi_abi.h`,
  `appkit.abi` untouched); the app reaches the system only through the kits (AppKit, UIKit, FileKit, AudioKit via
  `game.h`); the core (`terrain / world / level / solution / progress`) includes no UIKit header.
- **AC-35 B** Docs: docs/04 §12 catalog row (Games) and a *Critters* section (the rules, the roles, the controls §10,
  the levels, the files, the `.level` format with the example and the errors, the `.sol` format, the screenshots),
  the translated-apps list, `docs/HANDOFF.md`, `IDEAS.md`'s *Lemmings-like* row marked done (named Critters),
  `python docs/build_docs.py` run.
- **AC-36 B** The other games unchanged: `shots.sh pinball invaders` (and the other game scenarios) give the same
  screenshots; `games/game.h` changes, if any, are additions only.
- **AC-37 B** Originality: no file, name, level, sprite or sound from Lemmings; the sprites are drawn in code or a
  BMP sheet made for Onyx (§13); the words *Lemming(s)* appear nowhere in the app, its levels or its strings.

## 13. The look (for the UX designer)

Our own: small **round creatures** (a body about 8 px wide, 10 px tall at ×1 — e.g. a pebble-shaped body in a warm
colour with two eyes and tiny feet; no hair, no robe, no human figure), a few frames per state (walk 4–8, fall 2,
float 2 with a leaf for the floater, climb 4, build 4 with a brick, dig 4 with dust, block 1–2 with arms out, shrug,
explode with a countdown digit, splat, drown, enter the exit); the hatch as a **burrow / seed pod** that opens, the
exit as a **glowing doorway** (or a nest) that pulses; earth textures per level (soil, sand, moss, ice-blue rock),
steel as riveted plates. The skill bar in Onyx's CDE-modern style (`docs/gui-redesign/README.md`). All of it
original: the UX designer's mock-ups decide sizes, palette and frames.
