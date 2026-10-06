# AutoDev round 3 — Development: Turtle Quest, *Gems, portals and fractals*

Branch `AutoDev`. Plan: `03-technical-analysis.md` §4 (steps 1–13) and its GUI plan; binding notes of
`05-validation.md`. The work is split: **Developer A** steps 1–8 (the engine, the packs, the tests), **Developer B**
steps 9+ (the window, the editor, the shots, the docs).

---

## Developer A (steps 1–8)

### What was done, by step

| Step | Done | Where |
|---|---|---|
| 1 — draw mode | `Level::draw` is an `int` (0 / 1 shape / 2 colour); `draw_mode ()` reads `1`, `shape`, `2`, `color`, `colour` (any case), anything else non-zero → 1; `write_pack` writes `draw = 1` / `draw = color`. The 8 existing drawing levels write back as before. | `user/Apps/turtle/world.h` |
| 2 — gems | `W_GEM` (`GEM` / `GEMME`: `WORDS`, `FR_NAME`, `ALIASES_FR`, `SHOWN_FR`, both `word_help` lines; `PICK`'s help names the gem); `World::gems / gemsTotal` (counted only when `!draw`), `gem_at ()` on the **world's** cell (note 3: a picked gem reads 0); `reset`, `copyFrom` (note 3 / R5), `apply (EV_PICK)`, `won ()`; `PICK` out of order → *"Gem %d first! This is gem %d."* / *« D'abord la gemme %d ! Celle-ci est la %d. »* (with the run's *"Line n: "* prefix — `OWN[]` gains `"Gem "`, `"D'abord la gemme"`); `ITEM ()` true on a gem, `FRONT ()` = 2, `GEM ()`; the not-won message *"…%d gem(s) are still on the floor."* (after the coins', before the flag's). | `world.h` |
| 2, 3 — `check_level` (note 2) | **One verdict**: `check_level (const Level &, LevelFault &)` → kind (`LF_GEM_MISSING`, `LF_GEM_TWICE`, `LF_PAD_ALONE`, `LF_PAD_MANY`), number / pair, **the cell at fault** (`c`, `r`: the gem after the gap, the second of two, the lone pad, the third pad). Two wordings: `parse_pack` → `level_fault_reason ()` English, prefixed *"level %d (%s): "* (*gem 2 is missing*, *two gems 3*, *the teleporter T has no twin*, *three teleporters U*); the editor → **`level_fault_text (f, lang, …)`**, 04 §7's sentences EN/FR (plus one for a third pad, which `edit_pad` should make impossible: *"Portal %d has more than two pads: keep only two."* / *« Il y a plus de deux portails %d : n'en garde que deux. »*). A drawing level is not checked (its map is a page). | `world.h` |
| 3 — portals | `World::pad[2][2][2]`, `npad[2]`, `pad_at ()`, `twin ()`; `EV_TELEPORT` **appended** to the `EV_*` enum (from the pad `a, b` to the twin `c, d`; `apply`: moves, `paintHere`, **no `Seg`**); in `Recorder::move`, after the step's `EV_MOVE` (never after an `EV_BUMP`, never on a drawing level — note 4), when the step ends on a pad's **centre**: the jump; the steps left go on from the twin, the heading kept, no jump back on arrival; `FRONT ()` = 5; `blocks ()` unchanged; `FRONT ()`'s help *"…, 4 a door, 5 a portal"* / *« …, 4 une porte, 5 un portail »*. | `world.h` |
| 4 — colours | `covered_c ()` (every point of a's lines within 0.15 of a line of b **of the same pen**), `same_colours ()`; `run_program`: on `draw == 2` the shape first, then the colours; the two-line message *"The right figure, but not the right colours.\nEach line takes the colour of the light line under it."* (FR as 04 §7); the wrong shape on a colour level says *"…compare with the light one."* / *« …compare avec la figure claire. »*. | `world.h` |
| 5 — recursion, cards, help | `friendly`'s row `"Out of stack space"` → *"Line %d: the word calls itself without end -- does it have a test that stops it?"* (FR as 04 §7); the six `CONCEPTS` cards **verbatim from 04 §4** (checked by script, EN and FR; the colour card holds its `@palette` line); the header comment (map characters `1…9`, `T U`, the `draw` modes, the concepts, `GEM`, `FRONT` 5). | `world.h` |
| 6 — pack 4 | `4-gems-and-portals.turtle`, 10 levels, the titles of 04 §5, maps ≤ 13 columns: `gems-line`, `gems-back`, `gems-corners` (gem 1 is not the nearest: the other way round), `gems-count` (`GEM ()` + a counter, turning at the walls), `gem-door`, `portal`, `portal-chain` (both pairs), `portal-shortcut` (13 instructions the long way, 2 by the portal), `portal-gems` (an endless corridor: its last square is a portal to the first; `GEM () = n`), `grand-finale` (the UX mock-up's map: 5 gems, both pairs, key, door; the loop `WHILE NOT ONGOAL ()` / `FORWARD` / `IF ITEM () THEN PICK` = 3 instructions). | `sdcard/apps/turtle.app/levels/` |
| 7 — pack 5 | `5-spirals-and-fractals.turtle`, 11 drawing levels on 24 × 18 pages: `polygon-sub`, `polygon-row`, `rainbow-spiral` (**color**), `hex-spiral` (start (11, 8) as the TA found), `color-flower` (**color**), `halves`, `snail-rec`, `tree`, `koch`, `snowflake`, `sierpinski` (**color**). Sierpinski: `SUB Tri (size, depth, c)`, each small triangle in `COLOR c MOD 6 + 1` (the children get c, c + 1, c + 2), the moves **pen up** (note 9; the mock-up's pack moved to the wrong midpoint — fixed: `LEFT 120` … `RIGHT 120`). | idem |
| 6, 7 — pars | Each `par` = the reference solution's count, the 2-star count + 2 (≤ 3), + 3 (≤ 8), + 4 beyond: 4 7, 6 9, 6 9, 6 9, 6 9, 1 3, 5 8, 2 4, 5 8, 3 5 / 11 15, 6 9, 4 7, 5 8, 6 9, 7 10, 6 9, 10 14, 12 16, 14 18, 24 28. Texts: EN and FR ≤ 120 characters (six FR texts shortened to stay under it — note 10). | idem |
| 8 — the lint | In `turtletest.cpp`, on **every** pack given: ids unique across the packs; `title`/`text`/`hint` in both languages non-empty and **not cut** (`strlen < sizeof − 1`); the pack's `title` / `title.fr`; the concept's card in both languages; on a `draw = color` level the figure's pens in **1–14 except 7, 8** and **no line along a line of another colour** (3 sampled points in a row within 0.05, the ends ± 0.2 excepted — a crossing passes); **exactly 48 levels**. Packs 1–3 pass it untouched (AC2: `git diff` on them empty). | `tools/tests/turtle/turtletest.cpp` |

`main.cpp` — **safety only** (Developer B owns the window): `ev_ms` gives `EV_TELEPORT` 420 × k ms (04 D5's
duration; the turtle waits on pad A, then shows at the twin — B adds the animation); the editor's *Drawing*
checkbox keeps a colour level a colour level (`L.draw = checked ? (L.draw ? L.draw : 1) : 0`) until B's chooser
replaces it. Nothing else: gems and pads draw as plain floor today (no crash — `Board::tile` ignores unknown
characters), the six new concepts are not yet in `CONCEPT_KEYS` / `WordBar::ORDER`.

### Commits

- `47cf2dfb` — Turtle Quest: draw modes, gems, portals, colour drawings, recursion message, six lesson cards (steps 1–5)
- `60a2c30e` — Turtle Quest: pack 4 (10 levels) and pack 5 (11 levels) (steps 6–8)

### Tests run

| Test | Result |
|---|---|
| `sh tools/tests/run_turtle_test.sh` (g++ `-Wall -Wextra`, ASan + UBSan) | **`ok turtle (48 levels: solved, written back; the errors)`** — every level of the 5 packs won by its solution with 3 stars, written back and re-read equal on every field (id, size, map, par, both titles, texts, hints, topic, words, start, solution, **draw mode**); 0 warnings |
| — new cases on inline packs (independent of packs 4/5) | gem out of order → `R_ERROR` line 4 *"Gem 2 first! This is gem 3."* (EN) / *"D'abord la gemme 2 ! Celle-ci est la 3."* (FR); in order → won; one left → `R_LOST` *"1 gem(s)"* / *"1 gemme(s) par terre"*; `GEM ()` 0 / n / 0 after the pick, `ITEM ()`, `FRONT ()` = 2, `GEMME ()` in French; *"does not know GEM"* / *"ne connaît pas encore GEMME"*; `parse_pack` refuses *"level 2 (gap): gem 2 is missing"*, *"level 1 (twice): two gems 2"*, *"level 1 (portal): the teleporter T has no twin"*, *"level 2 (many): three teleporters U"*; `check_level`'s cell at fault and `level_fault_text` EN/FR; a copied `World` keeps gems and pads; one jump (3,1) → (7,1) right after the step's move, heading kept, the twin painted, no segment longer than 1; `FORWARD 5` = 2 steps, the jump, 3 steps; `FORWARD 3` with the pad 1 ahead ends 2 past the twin; the pair `U`; stepping off and `BACK` onto a pad jumps again; `FORWARD 1.5` does not jump, `+ 0.5` does; `FRONT ()` = 5 and `WALL ()` false; no jump on a drawing level; `draw = color` / `shape` read 2 / 1 and write back `color` / `1`; a colour level: own solution wins, a colour changed → *"not the right colours"* (EN/FR), a wrong shape → *"Not quite the same figure"*; `draw-square` with `COLOR 4` still wins; a recursion without a stop → *"calls itself without end"* / *"s'appelle lui-même sans fin"*, never *"stack"*; the six cards; `GEMME`, the help lines |
| — on the card's packs 4/5 | `rainbow-spiral` in other colours → *"not the right colours"*; one leg short (FR) → *"figure claire"*; `tree` without its stop → the friendly recursion message; `gems-line` gem 2 first → line 2 *"Gem 1 first! This is gem 2."* |
| `sh tools/tests/run_basic_test.sh` | passes (no BASIC file touched) |
| `SHOTS_PNG=<scratch> sh tools/tests/desktop_sim/shots.sh turtle` | the app builds for the simulator and runs its scenario; `turtle.png`, `turtle-fr.png`, `turtle-editor.png` **byte-identical** to `screenshots/` (AC13 holds so far). The script's other link errors (`print_*`, `PrintWriter::add_font` — apps using PrinterKit) are not Turtle Quest's and were there before |
| `g++ -Wall -Wextra -fsyntax-only user/Apps/turtle/main.cpp` (the simulator's flags without `-w`) | 0 warnings |

### What is left for Developer B (steps 9+)

- **Step 9, the board**: `Board::tile` for `'1'…'9'` (`gem ()`, its digit, the ring on `w.gem_at`… = `w.gems + 1`) and
  `'T'`/`'U'` (`pad ()`, `w.pad_at ()` gives the pair); the HUD with `w.gems / w.gemsTotal`; the `EV_TELEPORT`
  animation (the event: `a, b` → `c, d`; its duration is already in `ev_ms`); the tinted target when
  `g_L->draw == 2`; `CONCEPT_KEYS` / `NCONCEPTS = 20`; the six keys after `"draw"` in `WordBar::ORDER` (R9); the
  `Lesson`'s `@palette` line (today it shows as text), the 6-line card and the title fallback (D8, D9), binding
  note 1 (the Lesson centred on the whole right column; `g_small` at 920 × 600).
- **Step 10, the editor**: `edit_gem` / `edit_pad` (planned in `world.h`, with host tests in `turtletest.cpp`),
  the 12 tools, the *No / Shape / Colours* `Dropdown` replacing `g_edDraw` (note 7's five places; my safety line in
  `edit_collect` goes with it), Test / Save calling **`check_level (g_edLevel, f)`** then
  **`level_fault_text (f, g_lang, …)`** and ringing `f.c, f.r` red, `setMinSize (920, 600)`, the *Idea* drop-down
  first. Until then: opening a pack 4/5 level in the editor and saving it resets its concept to *move* (the
  drop-down knows 14 keys) — a step-10 fix.
- **Steps 11, 12**: the shots (`grand-finale` is the UX mock-up's map: the `tq-portals` scene can be replayed as
  is; its 3-instruction loop goes through both jumps), docs/04 §12–§13, docs/03, HANDOFF, IDEAS,
  `build_docs.py`. `tools/pkg/packages.ini` needs no line (the `[*apps]` rule ships `levels/`); nothing published.
- The card build (`make` from `kernel/`) stays the user's (no AArch64 compiler here — R10).

---

## Developer B (steps 9–11)

### What was done, by step

| Step | Done | Where |
|---|---|---|
| 9 — the board | `GEM_C` / `GEM_INK` / `PAD_RING` / `PAD_FACE` (04 §2.1); `gem ()` (cut stone, rim, table, bold digit when the cell ≥ 16 px), `ring ()`, `pad ()` (one dot T / two dots U, `glow`); gems are drawn **after the pen's trail** (`Board::gemTile`: a trail over a stone hid its digit — seen in the French shot), the white ring on `w.gems + 1` (not in the editor, not on the stone being picked), the pick shrink; `turtle (…, k)` scaled; `EV_TELEPORT` animation (04 §2.5: shrink into A with A's glow, the dotted arc, grow out of B; the pen draws nothing); the colour target tinted `uk_mix (0xFBFAF5, PEN[c], 85)` when `draw == 2`; the HUD *Coins · ◆ Gems · Keys* laid out part by part (the stone of the next gem, a green ✓ when all are picked) — without gems the pill is drawn exactly as before, so `turtle.png` stays byte-identical | `user/Apps/turtle/main.cpp` |
| 9 — concepts, card, lesson | `CONCEPT_KEYS` + the six keys, `NCONCEPTS = 20`; `WordBar::ORDER` + the six keys after `"draw"` (R9); `Card`: a title too wide beside Hint / Lesson falls back to the UI face bold, then is cut with "." (`cut_to ()`, UTF-8 safe), the text wraps to `CARD_LINES = 6` (draw and `need ()`); `Lesson`: *"New idea:"* dropped when it does not fit, a title still too wide in the UI face bold; **note 1**: the box is centred on the **whole right column** (from `PAD` to the message bar), its text laid out by `flow ()` and redrawn in `g_small` (DejaVu Sans 11) + `g_smallMono` (Mono 12) when it would pass the OK button; `@palette` = 16 numbered swatches in one row (≥ 20 px each, digits in the small face under 24 px) or **two rows of 8** in a narrow box | idem |
| 10 — editor | `edit_gem (L, c, r, drag)` / `edit_pad (L, c, r, pair, lastC, lastR)` **pure, in `world.h`** (lowest free number; a click on a gem cycles 1→9→1; a drag places only; never on the turtle; a pair keeps two pads — the older (not the last placed; unknown: the first in reading order) goes); `TOOL_CH` 12 tools / `ToolPal` 4 rows with the gem / pad icons and the 11-px face for names wider than `cell − 22` (*Portal 1/2*, *Gemme*, *Portail 1/2*, *Drapeau*, *Peinture*); `edit_cell (c, r, drag)`, `Board::onMouse` (a gem drag skips gems; the pads and the turtle are click-only); the panel of 04 §6.1 (Idea first at y 18; *3 and 2 stars* \| *Drawing* row; `Dropdown g_edDrawSel` *No / Shape / Colours* — FR *Non / Forme / Couleurs*, its tooltip — replacing `g_edDraw` in the five places of note 7, Developer A's safety line removed; `g_edLabels[10]` and its guard; `retitle ()`'s arrays in the new order); **Test and Save** run `edit_check ()` = `check_level (g_edLevel, f)` → `level_fault_text (f, g_lang, …)` in red (`M_ERR`) and a red ring on `f.c, f.r` (`g_badC/R`, cleared by the next edit, reset when the editor opens); `setMinSize (920, 600)`. A pack 4/5 level opened in the editor now keeps its concept (20 keys). | `world.h`, `main.cpp` |
| 10 — tests | `test_editor ()` in `turtletest.cpp`: gem 1 first, a drag places 2 and never cycles, a click 2→3, the gap filled by the lowest free number, the turtle's cell kept, 9→1, all nine placed → nothing; the T pair: lone T refused (fault cell, EN text), its twin → fine, a third T removes the older and keeps the last placed, never more than two, the U pair beside, never on the turtle, no "last" known → the first in reading order goes; the edited level (digits, T, U) and a `draw = 2` level written back and read again equal | `tools/tests/turtle/turtletest.cpp` |
| 11 — shots | `shots.sh`'s turtle block: `turtle-editor`'s Test click (45, 505) → **(45, 539)** (R3); new: **`turtle-portals`** (grand finale, F8 × 13: gems 1–2 picked, *Gems 2 / 5* with the yellow stone, the ring on gem 3, the turtle out of the twin T pad, the trail ending at pad A), **`turtle-editor-gems`** (Ctrl+E, Gem tool (45, 499), gem 5 clicked → 6, Save (114, 539) refused: *"Gem 5 is missing…"*, gem 6 ringed red), **`turtle-fractal`** (the snowflake run, 3 stars), **`turtle-rainbow`** (rainbow snail in the wrong colours over its tinted target: the two-line colour message), **`turtle-fr-gems`** (French, *Aller et retour* programmed with `GEMME ()`, F8 × 33: *Gemmes 2 / 4*, ring on gem 3, the `GEMME` chip). Fixtures: `tools/tests/desktop_sim/turtle/portals.ini`, `fractals.ini`, `gems-fr.ini` | `tools/tests/desktop_sim/` |

### Commits

- `944e5268` — Turtle Quest: the window and the editor for gems, portals and colour drawings; the new screenshots (steps 9–11)

### Tests and results

| Test | Result |
|---|---|
| `sh tools/tests/run_turtle_test.sh` (ASan + UBSan, `-Wall -Wextra`) | **ok turtle (48 levels: solved, written back; the errors)**, incl. the new `test_editor ()` cases; 0 warnings |
| `sh tools/tests/run_basic_test.sh` | ok (dialect, BASIC in French) |
| `sh tools/tests/desktop_sim/shots.sh turtle` | the 8 turtle shots made; **`turtle.png`, `turtle-fr.png` byte-identical** to before (AC13); `turtle-editor.png` changed by design (panel order, 4th tool row); two runs give identical PNGs (the simulator's clock is deterministic). The script's unrelated link errors (`print_*`: PrinterKit apps) are as before |
| `g++ -Wall -Wextra -fsyntax-only main.cpp` | 0 warnings |
| French (`SHOTS_LANG=fr`, scratch folder) | every turtle shot looked at: tools *Gemme / Portail 1 / Portail 2 / Drapeau / Peinture* in the 11-px face fit, *Non / Forme / Couleurs*, *3 et 2 étoiles* \| *Dessin*, *Gemmes 2 / 5*, the red *« Il manque la gemme 5 … »*, the colour message's two lines, the GEMME chip |
| Note 1 (scratch, not committed) | the six lesson cards, EN and FR, at **1000 × 640 and 920 × 600** (`SIM_SCREEN=928x746`): all end above OK; at 920 × 600 *gems*, *params*, *recursion* (EN) and *gems*, *teleport*, *color*, *params*, *recursion* (FR) switch to the small faces; the palette in two rows there. Also at 920 × 600: the *Idea* list's 20 rows open downward unclipped (EN, FR); the Drawing list open; the grand finale stepped (stones 25 px, digits legible). The jump's animation checked mid-way (speed 1: the arc, the shrinking / growing turtle, the glow) |

Known, accepted (05 note 10): at 920 × 600 the card's title and its 6th line can be cut (*"1. Gems in a ro."*, the grand finale's FR text) — the existing narrow-window behaviour.

### Left for steps 12–13

- **Step 12, docs**: docs/04 §12 row and §13 (gems, portals with their map letters `1…9`, `T`, `U` and the word *portal / portail*, `GEM ()` / `GEMME ()`, `FRONT ()` 5, `draw = color`, the 5 packs / 48 levels, the editor's panel order, the 12 tools, the drawing chooser, the check with its red ring, the **920 × 600 minimum**, the new screenshots `turtle-portals`, `turtle-editor-gems`, `turtle-fractal`, `turtle-rainbow`, `turtle-fr-gems`); docs/03 *Turtle Quest* (`EV_TELEPORT`, colour comparison, `check_level`, `edit_gem` / `edit_pad` in `world.h`); HANDOFF, IDEAS; `python docs/build_docs.py`. `world.h`'s header comment does not yet mention `edit_gem` / `edit_pad` (main.cpp's does).
- **Step 13** (SHOULD), untouched. `tools/pkg/packages.ini` unchanged; nothing published; the card build (`make`) stays the user's.
