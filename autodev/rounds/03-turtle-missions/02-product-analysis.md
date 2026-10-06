# AutoDev round 3 — Product Analyst: Turtle Quest, *Gems, portals and fractals*

Date: 2026-10-06. Input: `01-product-manager.md` (the pick: **Turtle Quest: more missions**, the queue's item 2;
its MUST / SHOULD / deferred scope is kept as it is here — nothing added to the MUST list but what the existing code
makes necessary, flagged *(analyst)*).

Looked at: `user/Apps/turtle/world.h` (the words `WORDS` / `FR_NAME` / `ALIASES_FR` / `SHOWN_FR` / `word_help`,
`CONCEPTS`, `Level` — `bool draw`, `topic[16]`, `text[2][480]` —, `parse_pack` / `write_pack`, `World::reset` /
`blocks` / `won`, the `EV_*` record and `apply`, `Recorder::move` — `grid = !draw`: a drawing level ignores the map —
and `ext`, `sample` / `covered` / `same_drawing` (0.15 square tolerance, every 0.1 square), `run_program`'s
"not won" messages, `MAX_EVENTS = 40000`, `MAX_POLLS = 3000`), `user/Apps/turtle/main.cpp` (`PEN[16]`, `Board::tile`
per map character, the target drawn in `0xD9D6CC`, the HUD "Coins 2 / 5  Keys 1", `TOOL_CH[] = { '#', '.', '*', 'k',
'c', 'D', 'p', '>', ' ' }` and `ToolPal` 3 × 3, `edit_cell`, the editor's `Checkbox` *Drawing* and `Dropdown` of
`CONCEPT_KEYS`, the `WordBar` and its `ORDER` of concepts that decides the purple block words, the private `kv_*`
progress store keyed **by level id per player**: `<id>` = stars, `<id>.code`, `seen.<concept>`),
`sdcard/apps/turtle.app/levels/*.turtle` (27 levels; the 8 drawing levels are 11 × 9 pages of `.`),
`sdcard/apps/turtle.app/app.txt` (`opens = turtle`), `sdcard/etc/fileassoc.ini` (`turtle = turtle`),
`tools/pkg/packages.ini` (`[app.turtle]`; the `[*apps]` rule packages the whole `apps/turtle.app/` folder, so new
files under `levels/` ship without a new line), `tools/tests/turtle/turtletest.cpp` + `tools/tests/run_turtle_test.sh`
(every pack given on the command line: parsed, written back and re-read equal, every solution wins with 3 stars;
then negative cases on named levels), `tools/tests/desktop_sim/shots.sh` (the `turtle` scenario: 3 shots, fixtures
`desktop_sim/turtle/{maze,star-fr}.ini`), `docs/04-USER-GUIDE.md` §12 catalog row and §13 *Turtle Quest*, Onyx
BASIC's VM (`user/Libs/basic/basvm.cpp`: `MAXFRAMES = 400`, *"Out of stack space (too deep recursion)"*).

The fractal sizes below were **checked with a throw-away turtle simulation** (Python, the engine's geometry: a move
cut in steps of at most 1 square, 0° north, clockwise; the page's bounds −0.5 … w − 0.5): bounding box, shortest
segment and event count — §6.3.

---

## 1. The name

The app stays **Turtle Quest** (`user/Apps/turtle/`, `SD:/apps/turtle.app/`, French: *Turtle Quest*). The feature
is **"Gems, portals and fractals"** (FR *« Gemmes, portails et fractales »*): two new packs, three new mechanics
and six new lesson cards. No new app, no new window, no new file type.

## 2. The one-line pitch

**Turtle Quest grows two worlds: pick numbered gems in order and jump through portals — then draw rainbow spirals,
trees and snowflakes with words that take values, give values back and call themselves.**

## 3. The users and their tasks

| User | Task | How the feature serves it |
|---|---|---|
| A pupil (9–14) who finished the 3 packs | More levels, new things to think about | Pack 4: **order** (gems 1, 2, 3…) and **space** (portals that move the turtle) — puzzles where the shortest program needs a loop *and* a plan |
| The same pupil, later | Understand counting with a variable | `GEM ()` + a counter `n`: *"pick the gem only if it is the next one"* — a variable that holds **state**, not only a size |
| A student (12–17, NSI / technology) | Learn parameters, return values, **recursion** | Pack 5: `SUB Polygon (sides, size)`, `FUNCTION Half (x)`, then a tree, the Koch curve and snowflake, the Sierpinski triangle — recursion *seen* as it draws |
| A teacher / parent | Set an exercise on these ideas | The editor places gems and portals and sets *Drawing: shape and colours*; the pack is a text file (`.turtle`) handed out as before |
| The tinkerer | The shortest program | The stars' `par` on every new level: the recursive solution is far shorter than the drawn-by-hand one |
| A French-speaking user | Everything in French | Every new text has its `.fr`; `GEMME ()`; the new lesson cards and messages in French |

## 4. Features

### MUST (this round)

**A. Gems in order** (map characters `1`…`9`)

1. A **gem** is a map character `1` to `9`, its number. A level's gems are numbered **1 … N without a gap and
   without a repeat** (N ≤ 9); `parse_pack` refuses another set with a clear reason: *"level 3 (gems-line): gem 2
   is missing"*, *"… two gems 3"*. *(analyst: a gap would make a level unwinnable)*
2. `PICK` on a gem takes it **only if it is the next one** (the turtle's count of gems picked + 1). Otherwise a
   run-time error at that line: **EN** *"Gem 2 first! This is gem 3."* — **FR** *« D'abord la gemme 2 ! Celle-ci est
   la 3. »* (the numbers filled in). `PICK` on a coin or a key is unchanged.
3. The sensors: `ITEM ()` is true on a gem (picked: false), `FRONT ()` says **2** for a gem ahead (as for a coin or
   a key). **New sensor `GEM ()`** (FR **`GEMME ()`**): the number of the gem under the turtle, 0 if none (or
   already picked). Its word help — EN *"GEM () -- the number of the gem under the turtle (0: none)"*, FR *« GEMME ()
   -- le numéro de la gemme sous la tortue (0 : aucune) »*. `GEM` is a normal level word: a level lists it in
   `words` to allow it (the palette shows it), otherwise *"This level does not know GEM yet."*.
4. **Winning**: every gem picked, in addition to the existing rules (flag, coins, tiles, figure). Not won because
   of gems: EN *"The program ended, but %d gem(s) are still on the floor."* / FR *« Le programme est fini, mais il
   reste %d gemme(s) par terre. »* (checked before the flag's message, as coins are).
5. **Drawn** as a faceted gem (a coloured diamond: 1 red, 2 orange, 3 yellow, 4 green, 5 cyan, 6 blue, 7 violet,
   8 pink, 9 white — colours chosen by the UX designer) with its **number** written on it, legible at the
   smallest cell size (`cs` = 8 → the number may be dropped below 16 px, the colour stays). A picked gem shrinks
   away like a coin (the existing pick animation). The HUD gains *"Gems 2 / 5"* / *« Gemmes 2 / 5 »* beside coins
   and keys; the **next gem to pick** is outlined (a pulse or a ring) so the order is visible without counting.

**B. Teleporters** (map characters `T` and `U`)

6. A **pad** is `T` (pair 1) or `U` (pair 2); each letter appears **exactly twice** in a level, or not at all;
   `parse_pack` refuses another count: *"level 6 (portal): the teleporter T has no twin"* / *"… three teleporters
   U"*. (`T`, `U` clash with no existing character: `# . * k c D p ^ > v <` and space; `v` is lower case.)
7. When a turtle's **step ends on a pad** (`FORWARD` or `BACK`, in a grid level), it is moved **at once to the
   twin**, **keeping its heading**. A `FORWARD n` that has steps left goes on **from the twin** (e.g. `FORWARD 3`
   with a pad 1 square ahead: 1 step to the pad, jump, 2 steps from the twin). Arriving on the twin does **not**
   jump again (no ping-pong); stepping off and back onto a pad jumps again. Turning on a pad, starting on a pad,
   `PICK` / sensors on a pad: no jump.
8. The record gains an event (**`EV_TELEPORT`**: from the pad to the twin) so the **playback shows it**: the turtle
   fades out at the first pad and in at the twin (or a flash at both pads), with a duration like a step's at the
   chosen speed; in step-by-step (F8) the jump belongs to the statement that moved. The pen **draws no line across
   the board**: a segment ends on the first pad, the next starts at the twin; with the pen down the twin's cell is
   painted (`p` tiles) as any cell walked on.
9. Sensors: a pad is floor — `WALL ()` false, `ITEM ()` false; **`FRONT ()` says 5** for a pad ahead (a new value,
   documented in `word_help` and docs/04: *"0 free, 1 a wall, 2 something to pick, 3 the goal, 4 a door, 5 a
   teleporter"*). No existing level has a pad, so no old program changes meaning.
10. **Drawn** as a round pad, pair 1 in cyan, pair 2 in magenta (UX's choice), the two pads of a pair alike — the
    player sees which ones go together. In a **drawing level** (`draw` set) the map is a page: gems and pads there
    are ignored, as walls are today (the editor does not stop a teacher from placing them; nothing happens).

**C. Drawings in colour**

11. A level's `draw` key takes a **mode**: `draw = 1` (or `shape`) — the figure, any colour (today's behaviour,
    unchanged); **`draw = color`** (also read: `colour`, `2`) — the figure **with the solution's colours**.
    `write_pack` writes `draw = 1` and `draw = color` (so the 8 existing levels write back as they are).
12. In colour mode the comparison checks colour with shape: every sampled point of the player's lines must be within
    0.15 square of a target line **of the same pen colour**, and every target point within 0.15 of a player's line
    **of the same colour** (where two colours overlap, either fits). If the figure matches by shape (today's test)
    but not by colour: EN *"The right figure, but not the right colours."* / FR *« La bonne figure, mais pas les
    bonnes couleurs. »*; otherwise the existing *"Not quite the same figure…"*.
13. The target of a colour level is drawn in **light tints of its own colours** (each target segment in its colour
    mixed with the page, not the grey `0xD9D6CC`); shape levels keep the grey. Colour levels use pen colours
    **1 … 14** only (0 black is fine on the page, but 15 white would be invisible; 7 / 8 greys are avoided so the
    tint is not mistaken for a shape target) — a rule for the packs, checked by the test (AC9).

**D. The two packs** (§6): **`4-gems-and-portals.turtle`** (10 levels) and **`5-spirals-and-fractals.turtle`**
(11 levels): **21 new levels, 48 in all**. Every level has `id` (unique **across all packs**: progress is keyed by id
— *analyst*), `title` / `title.fr`, `text` / `text.fr`, `hint` / `hint.fr`, `concept`, `words`, `par`, `map`,
`solution` (and `draw` on pack 5). The 27 existing levels are not touched.

**E. Lesson cards** (EN + FR, in `CONCEPTS`, shown the first time a level of that concept is opened, F1 after):

14. `gems` — *Gems in order*: take them 1, 2, 3…; `GEM ()` says the number under you; a counter `n` remembers which
    one is next (example `IF GEM () = n THEN PICK : n = n + 1`).
15. `teleport` — *Portals*: step on a pad, come out of its twin, same direction; the same colour = the same pair.
16. `color` — *Colours are numbers*: `COLOR 4` is red; a number from a loop's counter makes a rainbow
    (`COLOR i MOD 6 + 1`); the colours listed 0–15 with their names.
17. `params` — *Words that take values*: `SUB Polygon (sides, size)` … `Polygon 5, 3`; the turn is `360 / sides`.
18. `function` — *Words that give a value back*: `FUNCTION Half (x)` … `Half = x / 2` … `FORWARD Half (8)`.
19. `recursion` — *A word that calls itself*: a tree is a trunk with two smaller trees on it; `SUB Tree (size)`
    calls `Tree size * 0.6` — and **a test that stops it** (`IF size < 1 THEN EXIT SUB`), else *"too deep"*.
    The VM's *"Out of stack space (too deep recursion)"* is made friendly: EN *"Line %d: the word calls itself
    without end -- does it have a test that stops it?"* / FR *« Ligne %d : le mot s'appelle lui-même sans fin --
    a-t-il un test qui l'arrête ? »* *(analyst: the raw message is what a pupil gets on a first recursion)*.

    The six keys go into `CONCEPT_KEYS` (the editor's *Idea* drop-down) and after `draw` in the `WordBar`'s `ORDER`
    (so these levels show all five purple block words, `SUB` included). `Level.topic` is 16 bytes: the keys fit.

**F. The level editor**

20. The tool palette gains **Gem** (FR *Gemme*), **Portal 1** (*Portail 1*), **Portal 2** (*Portail 2*): 12 tools,
    4 rows of 3. **Gem**: a click on a free cell places the **lowest number not on the map**; a click on an existing
    gem **cycles** its number 1 → 9 → 1; dragging places, never cycles. **Portal 1/2**: places `T` / `U`; a third
    pad of a pair **moves** the older one (the first placed is removed), so a pair never exceeds two.
21. **Save** and **Test** check the level as `parse_pack` would: a gap in the gems or a pad without twin is
    reported (*"Gem 2 is missing."*, *"Portal 1 has no twin."*) and Save is refused until fixed.
22. The *Drawing* checkbox becomes a three-way choice — **No / Shape / Shape and colours** (FR *Non / Forme / Forme
    et couleurs*) — a `Dropdown` (or two checkboxes: UX's call), mapped to `draw` = 0 / 1 / color.
23. The new concept keys are in the *Idea* drop-down (their card titles shown, as today).
24. **Round trip**: `write_pack` writes the new characters and `draw = color` back; `parse_pack` of the written text
    gives the same levels (map, draw mode, texts, par, solution).

**G. English + French**

25. All new player-facing strings in both languages: the messages (§A–C), the lesson cards, `word_help (GEM)`, the
    editor's tool names, the drawing choice, the HUD's *Gems*, every level's title / text / hint. The French word
    `GEMME` (one name, no short form) in `FR_NAME`, `ALIASES_FR` (`GEMME` → `GEM`) and `SHOWN_FR`. The app
    keeps its own `L2 (en, fr)` mechanism and `T (lang, en, fr)` in `world.h` (its words are not `TR ()` ones —
    `tools/lang/check.py` does not cover Turtle Quest; the completeness is checked by the host test instead, AC9).

**H. Tests, simulator, docs** — see §10 (AC1–AC17).

### SHOULD (if time allows, in this order — the PM's order)

1. **Your best program**: the smallest instruction count of a won run kept per level and player
   (`<id>.best = 6` in `progress.ini`), shown with the count — EN *"Your best: 6 — three stars at 5"*, FR *« Ton
   record : 6 — trois étoiles à 5 »*. Absent key = never won (old files read as before).
2. **Stars per pack**: the pack drop-down's entries and the list header show the stars won out of the pack's total
   (*★ 18 / 30*).
3. **Progress on FileKit's `fk_kv`** (round 2's follow-up, kits first): `main.cpp`'s `kv_*` replaced by
   `fk_kv_*` — **only** if `desktop_sim/turtle/maze.ini` and `star-fr.ini` (and a file with a multi-line `.code`)
   read back and write back **byte-identical** (a host test). `parse_pack` stays as it is.
4. A **FUNCTION** chip in the `WordBar` for the `function` and `recursion` levels (writes a `FUNCTION … END
   FUNCTION` block, as `SUB` does).
5. A free-drawing **sandbox** at the end of pack 5 (`id = sandbox`, `draw` absent, no flag, no `par`, a 24 × 18
   page): always "won", one star — a place to try.

### LATER (deferred, as the PM sized them)

Several turtles (its own round), a daily challenge (needs a generator + solver), GPIO bonus levels (hardware),
coloured keys / doors, switches, moving obstacles (a "mechanics 2" pack), locked levels until the previous is won
(a behaviour change the user decides), the UI words moved to `TR ()` + `lang/fr.txt`, `SAY`, sounds, a shared star
glyph in UIKit.

## 5. What it does NOT do (this round)

- No new app, window, menu or file type; no kapi, kernel or kit change (FileKit only if SHOULD 3 is done — using
  what round 2 added, nothing new in it).
- No change to the 27 existing levels, their solutions, their par, their screenshots or how they play.
- No locking: all 48 levels stay open (docs/04 §13's design).
- Gems are **one** sequence per level (no two colours of gems, no "any order" gems — coins are that). At most 9.
- Two portal pairs at most (`T`, `U`); no one-way portals, no portal that turns the turtle.
- Teleporters and gems do nothing in a drawing level; a drawing level has no colour tolerance beyond exact pen
  numbers (no "close enough" shades).
- No turtle speed-up for big fractals beyond the existing speed slider (the instant end of the slider exists).
- No new BASIC feature: `SUB` / `FUNCTION` with parameters and recursion already work (`MAXFRAMES = 400`).
- No package publishing (PIPELINE §0.5): the package is declared, not published.

## 6. The missions — 2 new packs, 21 levels

Ids are new and unique across the 5 packs (none of `hello … square-spiral`). `words` lists the turtle's words; BASIC's
own (`IF`, `WHILE`, `SUB`, `FUNCTION`…) are always known. `par` and exact maps are set by the Developer from the
reference solution (its count = par3; par2 ≈ par3 + 2…4), the counts below are indicative.

### 6.1 Pack 4 — *4. Gems and portals* / *4. Gemmes et portails* (`4-gems-and-portals.turtle`, 10 levels, grid)

| # | id | Title EN / FR | concept | Idea, map sketch | Reference solution (gist) |
|---|---|---|---|---|---|
| 1 | `gems-line` | *Gems in a row* / *Des gemmes en rang* | gems | corridor `>.1.2.3.*`: pick 1, 2, 3 then flag | `REPEAT 3: FORWARD 2: PICK` + `FORWARD 2` (≈ 4) |
| 2 | `gems-back` | *The wrong way round* / *À l'envers* | gems | corridor `>.3.2.1#`: gem 1 at the far end — go to the end, pick on the way back with `BACK` | `FORWARD 6: PICK`, `REPEAT 2: BACK 2: PICK` (≈ 5) |
| 3 | `gems-corners` | *Round the square* / *Le tour du carré* | gems | a square ring, a gem at each corner 1-4 clockwise, the flag at the start's side | `REPEAT 4: FORWARD 3: PICK: RIGHT` … (≈ 5) |
| 4 | `gems-count` | *Back and forth* / *Aller et retour* | gems (GEM) | corridor with gems `3 1 4 2` mixed: one pass east picks 1, 2; back west picks 3; east again picks 4 | `n = 1`, `WHILE n <= 4`: `IF WALL () THEN RIGHT: RIGHT` / `FORWARD` / `IF GEM () = n THEN PICK: n = n + 1` (≈ 8) |
| 5 | `gem-door` | *The locked gem* / *La gemme enfermée* | gems | gem 2 in a room behind a door, the key on the way, gem 1 before it | sequence with `PICK` × 3 and a door (≈ 9) |
| 6 | `portal` | *A first jump* / *Un premier saut* | teleport | two rooms with no way between; `T` in each | `FORWARD 3`, (jump), `FORWARD 2` (≈ 2) |
| 7 | `portal-chain` | *Portal hopping* / *De portail en portail* | teleport | three rooms: `T` takes to room 2, `U` from room 2 to room 3 | moves and turns, 2 jumps (≈ 6) |
| 8 | `portal-shortcut` | *The short way* / *Le raccourci* | teleport | a long winding corridor to the flag, and a `T` pair that skips it: the par only allows the portal way | (≈ 4) |
| 9 | `portal-gems` | *Gems beyond the portal* / *Des gemmes derrière le portail* | gems | a loop corridor: gems 1-4 spaced alike, a portal at the end returns to the start line; `WHILE` | `WHILE NOT ONGOAL (): FORWARD: IF ITEM () THEN PICK` (≈ 4) — the gems are met in order |
| 10 | `grand-finale` | *Grand finale* / *Le grand final* | teleport | a key, a door, gems 1-5, both portal pairs, the flag | a SUB for the repeated part (≈ 14) |

`words`: levels 1-3, 5 `FORWARD BACK LEFT RIGHT PICK ITEM`; level 4 adds `GEM WALL`; 6-8 `FORWARD BACK LEFT
RIGHT FRONT WALL`; 9-10 all of them with `GEM ONGOAL KEYS`.

### 6.2 Pack 5 — *5. Spirals and fractals* / *5. Spirales et fractales* (`5-spirals-and-fractals.turtle`, 11 levels, drawing)

All are drawing levels on a **24 × 18 page of `.`** (the largest board, so the figures are big), the turtle placed
where the reference solution starts. `words` = `FORWARD BACK LEFT RIGHT PENUP PENDOWN COLOR`.

| # | id | Title EN / FR | concept | draw | Reference solution (gist) |
|---|---|---|---|---|---|
| 1 | `polygon-sub` | *Any polygon* / *N'importe quel polygone* | params | 1 | `SUB Polygon (sides, size)` (`REPEAT sides: FORWARD size: RIGHT 360 / sides`), then a pentagon and an octagon side by side (`Polygon 5, 3` … `PENUP` move `PENDOWN` … `Polygon 8, 2`) |
| 2 | `polygon-row` | *From triangle to octagon* / *Du triangle à l'octogone* | params | 1 | `FOR s = 3 TO 8: Polygon s, 2: NEXT` — all on one corner: nested polygons |
| 3 | `rainbow-spiral` | *Rainbow snail* / *L'escargot arc-en-ciel* | color | **color** | square spiral, 16 legs from 1 to 16: `FOR i = 1 TO 16: COLOR i MOD 6 + 1: FORWARD i: RIGHT: NEXT` (fits: x 3.5…19.5, y 1.5…16.5) |
| 4 | `hex-spiral` | *A spiral that is not square* / *Une spirale pas carrée* | variable | 1 | `n = 0.5`, 18 legs, `RIGHT 60`, `n = n + 0.5` (the 22-leg version leaves the page: 18 max) |
| 5 | `color-flower` | *The flower in two colours* / *La fleur bicolore* | color | **color** | 6 hexagons of 2.5 turned by 60°, odd ones red, even ones blue: `IF i MOD 2 = 0 THEN COLOR 4 ELSE COLOR 1` |
| 6 | `halves` | *Half and half again* / *La moitié de la moitié* | function | 1 | `FUNCTION Half (x)`; `n = 8`, `REPEAT 5: FORWARD n: RIGHT: n = Half (n)` (8, 4, 2, 1, 0.5) |
| 7 | `snail-rec` | *The snail that calls itself* / *L'escargot qui s'appelle* | recursion | 1 | `SUB Snail (size)`: `IF size < 1 THEN EXIT SUB`, `FORWARD size: RIGHT: Snail size - 1`; `Snail 14` (x 3…17, y 2…15) |
| 8 | `tree` | *A tree* / *Un arbre* | recursion | 1 | `SUB Tree (size, depth)`: trunk, `LEFT 30`, `Tree size * 0.6, depth - 1`, `RIGHT 60`, the same, `LEFT 30`, `BACK size`; `Tree 6, 4` from (11.5, 17) north — x 6.5…16.5, y 4.6…17, smallest branch 1.3 |
| 9 | `koch` | *The Koch curve* / *La courbe de Koch* | recursion | 1 | `SUB Koch (size, depth)` (`depth = 0`: `FORWARD size`; else 4 × `Koch size / 3, depth - 1` with `LEFT 60 / RIGHT 120 / LEFT 60`); `Koch 18, 3` from (2, 12) east — x 2…20, y 6.8…12, smallest 0.67 |
| 10 | `snowflake` | *The snowflake* / *Le flocon de neige* | recursion | 1 | `REPEAT 3: Koch 12, 2: RIGHT 120` from (5, 5) east — x 5…17, y 1.5…15.4, smallest 1.33 (depth 3 at 13.5 also fits: smallest 0.5 — the Developer may choose it) |
| 11 | `sierpinski` | *The Sierpinski triangle* / *Le triangle de Sierpinski* | recursion | **color** | `SUB Tri (size, depth)`: `depth = 0`: a triangle; else three half-size `Tri`; the smallest triangles in `COLOR depth`-based colours or each corner its colour; `Tri 16, 3` from (3, 16) east — x 3…19, y 2.1…16, smallest side 2 |

(An H-tree depth 3, size 8, from (11.5, 8.5): x 4.5…18.5, y 1.5…15.5 — an alternative to level 2 or 4 if one
reads better.) Pack 5 has 3 colour levels (3, 5, 11) so the colour mode is exercised at three difficulties.

### 6.3 How the sizes were checked

A Python copy of the engine's turtle geometry ran each reference solution: every figure stays inside the 24 × 18
page (−0.5 … 23.5 / 17.5), every **geometric** segment is ≥ 0.5 square (the comparison's 0.15 tolerance keeps
distinct lines apart), and the record is small — the biggest (Sierpinski depth 3) ≈ 450 move/turn events, plus
the line events; the H-tree ≈ 460 — far under `MAX_EVENTS = 40000`. Recursion depth ≤ 5 frames (+ the caller's),
far under `MAXFRAMES = 400`. The real check is AC1 (the host test runs the solutions in the engine itself).

## 7. Files read and written

| File | Read / written | Change |
|---|---|---|
| `SD:/apps/turtle.app/levels/4-gems-and-portals.turtle` | read | **new** (pack 4) |
| `SD:/apps/turtle.app/levels/5-spirals-and-fractals.turtle` | read | **new** (pack 5) |
| `SD:/apps/turtle.app/levels/1…3-*.turtle` | read | unchanged |
| `SD:/docs/turtle/my-levels.turtle` | read + written (editor) | may now hold `1`…`9`, `T`, `U`, `draw = color` |
| any `*.turtle` opened / dropped / double-clicked | read | as above |
| `SD:/apps/turtle.app/progress.ini` | read + written | unchanged format; new keys only from new level ids (`<id>`, `<id>.code`) and new concepts (`seen.gems` …); SHOULD 1 adds `<id>.best` |

### 7.1 The pack format — additions (the header comment of `world.h` and docs/04 §13 updated with them)

```
map characters, new:
  1 … 9   a gem, its number: pick them in order (1 first); a level's gems are 1 … N, no gap, no repeat
  T  U    a teleporter pad, pair 1 / pair 2: each letter exactly twice (or not at all)
level key, extended:
  draw = 1 | shape          a drawing level: the solution's figure, any colour      (as today)
  draw = color | colour | 2 a drawing level: the figure AND its colours            (new)
  (absent or 0: not a drawing level)
concept, new values:  gems  teleport  color  params  function  recursion
words, new turtle word: GEM  (FR GEMME)
FRONT () new value: 5 = a teleporter ahead
```

`parse_pack`'s new refusals (the pack is not loaded; the message box *"Not a pack of levels: <why>"* as today):
gem gap / repeat, pad count ≠ 0 or 2. `write_pack` writes `draw = 1` or `draw = color`; the map rows as they are.

A level of pack 4, for the record (illustrative):

```
[level]
id = gems-line
title = Gems in a row
title.fr = Des gemmes en rang
concept = gems
words = FORWARD BACK LEFT RIGHT PICK ITEM
text = Pick the gems in order -- 1, then 2, then 3 -- and reach the flag.
text.fr = Ramasse les gemmes dans l'ordre -- 1, puis 2, puis 3 -- et rejoins le drapeau.
hint = REPEAT 3: FORWARD 2, PICK.
hint.fr = REPETER 3 : AVANCER 2, RAMASSER.
par = 4 7
map =
| ##########
| #>.1.2.3*#
| ##########
solution =
| REPEAT 3
|   FORWARD 2
|   PICK
| END REPEAT
| FORWARD
```

### 7.2 French texts

Inside the packs (`title.fr`, `text.fr`, `hint.fr`, `[pack] title.fr`) and in `world.h` / `main.cpp` (`T ()`,
`L2 ()`, `CONCEPTS[].title/text[1]`, `word_help` FR, `FR_NAME`). Limits: `text`/`hint` 480 bytes, `title` 80 bytes —
UTF-8 French counted in bytes (AC9 checks nothing is cut).

## 8. File associations

**Unchanged**: `sdcard/etc/fileassoc.ini` `turtle = turtle`, `app.txt` `opens = turtle`, `[app.turtle] opens =
turtle`. The new packs are `.turtle` files like the others; a pack written by this version with gems / portals /
`draw = color`, opened by an **older** Turtle Quest, loads (unknown characters are drawn as floor, `draw = color` as
a drawing level since `atoi ("color")` is 0 → **not** a drawing level): acceptable, the packages update the app and
the packs together.

## 9. How it fits with the other apps

- **Circuits** (round 2), its sister game: the same window plan, stars and lesson cards — unchanged by this round.
  SHOULD 3 brings Turtle Quest's progress onto the `fk_kv` store Circuits uses.
- **QBStudio / QBasic / `/bin/basic`**: the same Onyx BASIC; pack 5 teaches `SUB` with parameters, `FUNCTION`,
  recursion — the same code runs there (without the turtle words). Nothing changes in `user/Libs/basic`.
- **File Viewer / Files**: a double click on a `.turtle` opens it in Turtle Quest (as today).
- **Package Manager**: the `turtle` package (`[*apps]` takes `apps/turtle.app/`, so `levels/4-…` and `5-…` ship
  with it) — declared, not published this round.
- No clipboard, drag & drop or notification change (the editor's text fields keep UIKit's clipboard).

## 10. Acceptance criteria (testable)

Automatic (host test bench — `sh tools/tests/run_turtle_test.sh`, which runs `turtletest` on all 5 packs):

- **AC1 — Every level solvable, 3 stars.** The test, given the five packs, reports **48 levels**; each level's
  `solution` wins (`R_WON`) with **3 stars**, pack 4 and pack 5 included, and prints `ok turtle (48 levels …)`.
- **AC2 — Old packs unchanged.** Packs 1-3 are byte-identical to before the round (`git diff` empty on them); their
  27 solutions still win with 3 stars; every existing negative case of `turtletest.cpp` still passes.
- **AC3 — Round trip.** Every pack (1-5) written back by `write_pack` and re-read by `parse_pack` gives the same
  levels: id, size, map (gems and pads included), par, texts in both languages, solution, and the **draw mode**
  (0 / 1 / color — the existing check compares a `bool`; it compares the mode).
- **AC4 — Gems.** On a pack 4 level: `PICK` on gem 3 while 2 is not picked → `R_ERROR` at that line, message
  contains *"Gem 2 first"* (EN) / *"D'abord la gemme 2"* (FR); picking all gems in order and reaching the flag wins;
  ending with a gem left → `R_LOST`, message contains *"gem(s)"*; `GEM ()` returns the gem's number on it, 0 on
  floor and after the pick; `ITEM ()` true on a gem; `FRONT ()` = 2 facing a gem; `GEMME ()` works in a French
  program; a level without `GEM` in `words` refuses it (*"does not know GEM"*).
- **AC5 — Gem numbering refused.** `parse_pack` on a pack whose level has gems `1 3` (no 2) or two `2`s returns
  false with a reason naming the level and the gem.
- **AC6 — Teleport.** On a portal level: a step onto `T` ends with the turtle on the twin `T`, **heading
  unchanged**; the record holds one `EV_TELEPORT` from pad to twin; `FORWARD 3` with the pad 1 ahead ends 2 squares
  past the twin; landing on the twin does not jump back; the world's pen segments contain **no segment longer than
  1 square joining the two pads** (no line across); `FRONT ()` = 5 facing a pad, `WALL ()` false.
- **AC7 — Unpaired pad refused.** `parse_pack` on a level with one `T` (or three `U`) returns false, the reason
  names the level and the pad.
- **AC8 — Colour drawings.** On `rainbow-spiral` (or another `draw = color` level): the solution wins; the same
  program with one `COLOR` changed → `R_LOST`, message contains *"not the right colours"* (FR *"pas les bonnes
  couleurs"*); a wrong shape → *"Not quite the same figure"*. On an existing `draw = 1` level (`draw-square`) a
  solution with `COLOR 4` added still wins (any colour).
- **AC9 — Texts complete (EN/FR).** For every level of packs 4 and 5: `title`, `title.fr`, `text`, `text.fr`, `hint`,
  `hint.fr` non-empty and shorter than their buffers (no truncation: the parsed length equals the file's); both
  packs' `[pack] title.fr` set; every `concept` found in `CONCEPTS` with both titles and texts non-empty (the six new
  ones present: `gems teleport color params function recursion`); `word_help (W_GEM, EN/FR)` non-empty and
  `word_name (W_GEM, LANG_FR)` = `GEMME`; every colour level's solution uses pen colours 1-14 only.
- **AC10 — Unique ids.** No two levels of the five packs share an `id`.
- **AC11 — Recursion.** The `tree`, `koch`, `snowflake`, `sierpinski` and `snail-rec` solutions win (no *"too
  deep"*); a recursive SUB without a stop on a pack 5 level gives `R_ERROR` with the friendly message *"calls itself
  without end"* (FR *"s'appelle lui-même sans fin"*), not the raw VM text.
- **AC12 — The rest of the bench.** `sh tools/tests/run_basic_test.sh` passes unchanged; the app builds for the
  card (`make` from `kernel/`, the `turtle` target) and in the simulator (`shots.sh`'s turtle build line) with no new
  warning.

In the simulator (`sh tools/tests/desktop_sim/shots.sh turtle`, new fixtures in `tools/tests/desktop_sim/turtle/`):

- **AC13 — Existing shots unchanged.** `turtle.png`, `turtle-fr.png`, `turtle-editor.png` are produced as before
  (same scenario; pixel changes only where the pack drop-down now lists 5 packs, if visible).
- **AC14 — Gems and portals shot** (`turtle-portals.png`): a pack 4 level (e.g. `portal-gems` or `grand-finale`)
  stepped with F8 to just after a jump — numbered gems visible (one picked), both pairs of pads visible in their
  colours, the turtle on the twin pad, the HUD *"Gems n / N"*, the lit line in the program.
- **AC15 — Fractal shot** (`turtle-fractal.png`): the snowflake (or the tree) run to the end over its target, the
  message bar showing the win with 3 stars; plus the colour level `rainbow-spiral` drawn in colours over its tinted
  target (in the same shot or `turtle-rainbow.png`).
- **AC16 — French shot** (`turtle-fr-gems.png`, `SHOTS_LANG=fr` or the scenario's `lang fr`): a pack 4 level in
  French with its lesson card *Gemmes dans l'ordre* (or the level's text) and `GEMME` in the word palette — every
  word fitting its chip / card (no clipped text).
- **AC17 — Editor round trip, by hand in the simulator and by test.** In the editor (Ctrl+E → New Level): the Gem
  tool places 1, then 2 on the next cell; a click on a gem cycles its number; Portal 1 placed three times leaves two
  pads; *Shape and colours* chosen; **Save** writes `SD:/docs/turtle/my-levels.turtle` (the simulator's writes
  folder) containing the digits, `T`/`U` and `draw = color`; re-opening the pack shows the level identical. A
  `turtle-editor-gems.png` shot shows the 12-tool palette and a level with gems and pads. Save with a lone pad or a
  gem gap shows the refusal message and writes nothing.

Documentation (checked by reading):

- **AC18 — Docs.** `docs/04-USER-GUIDE.md` §13: the gems, `GEM ()` / `GEMME ()` in the words table, `FRONT ()`'s
  value 5, the teleporters, `draw = color` and the colour rule, the 5 packs / 48 levels, the editor's new tools and
  drawing choice, the new map characters in the pack-format paragraph, the new screenshots; §12's catalog row (48
  levels in five packs, gems, portals, fractals); `world.h`'s header comment (the format); `docs/HANDOFF.md` and
  `IDEAS.md`'s Turtle Quest row (what is done, what stays: several turtles, daily challenge…); `python
  docs/build_docs.py` run. `tools/pkg/packages.ini`: nothing to add (the `[*apps]` rule ships `levels/`) — said in
  06-development.md; **not published**.
