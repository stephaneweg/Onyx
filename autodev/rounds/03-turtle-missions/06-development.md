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

---

## Developer C (steps 12–13) and the summary

First: `git fetch origin main` — `origin/main` had moved (`c5f87a1b`: Circuits and FileKit's `fk_kv_*` built for the
Pi, packages); merged into `AutoDev` without conflict (`Merge origin/main into AutoDev (round 3, Developer C)`), then
`run_turtle_test.sh` and `run_basic_test.sh` re-run: green.

### Step 12 — the docs

| File | Change |
|---|---|
| `docs/04-USER-GUIDE.md` §12 | the catalog row: 48 levels in five packs, gems, portals, fractals, colour; the editor's gems / portals; the best programs |
| `docs/04-USER-GUIDE.md` §13 | the intro (gems, portals, colour figures); the window (the pack's stars at the list's foot, *your best* by the count, the HUD *Coins / Gems / Keys*, the **920 × 600** minimum, the `FUNCTION` chip); the words table (`PICK` and gems, `FRONT ()` 5 a portal, **`GEM ()` / `GEMME ()`**); winning (gems, colour drawings and the colour rule); the new messages (gem order, colours, recursion); the five packs / 48 levels and the six new lesson cards; a *Gems and portals* paragraph (the word **portal**, the pairs, the jump's rules); the editor (Idea first, *3 and 2 stars* \| *Drawing: No / Shape / Colours*, the 12 tools, Gem's numbering and cycling, a pair keeps two pads, the red check and its ringed cell); the pack format (`draw = 1 / shape / color`, map characters `1`…`9`, `T`, `U`, the refusal); `progress.ini`'s best counts; the 5 new screenshots `../screenshots/turtle-{portals,fractal,rainbow,fr-gems,editor-gems}.png` with captions |
| `docs/03-DEVELOPER-GUIDE.md` *Turtle Quest* | `EV_TELEPORT` (where it is emitted), `draw` 2 and `same_colours`, `check_level` / `level_fault_reason` / `level_fault_text`, `edit_gem` / `edit_pad`, `friendly`'s recursion line, `progress.ini`'s keys (`<id>.best`), the test's new cases and lint, the 8 shots. (Its i18n paragraph already names Turtle Quest's `L2`: unchanged.) |
| `user/Apps/turtle/world.h` header | the best program (`new_best`, `<id>.best`) and the editor's rules (`check_level`, the two wordings, `edit_gem`, `edit_pad`) — the map characters, `draw` modes, concepts, `GEM`, `FRONT` 5 were already there (Developer A) |
| `docs/HANDOFF.md` | a new section *Turtle Quest, Gems, portals and fractals (AutoDev round 3)*: done, tested, not done here, follow-ups; the old section's ideas point to it |
| `IDEAS.md` | the row *Turtle Quest : plus de missions* → 🔨, what round 3 did and what remains (in French); P9's mention 48 levels / 5 packs |
| `docs/exports/*` | `python docs/build_docs.py` run (pandoc 3.1.3 + LibreOffice present; `pypandoc` was missing and was installed with pip): every `.docx` / `.pdf` rebuilt and committed |

### Step 13 — the SHOULD items (in the PM's order)

| # | Item | Status | How / why |
|---|---|---|---|
| 1 | **Your best program** | **done** | `world.h` `new_best (was, count)` (pure, host-tested); `finish_run` keeps `<id>.best` per player when a won run (not in the editor) has fewer instructions; `refresh_count` shows it after the stars' counts: *"Instructions: 4   ★★★ ≤ 4   ★★ ≤ 7   Best: 6"* / *« … Record : 6 »* (the gaps narrow from 7 to 3 spaces only when a best is shown — without one the line is drawn exactly as before). Shown when the level is shown or the program edited, not right after the win (the message bar already gives that run's count), which keeps the older shots' count line identical. An old `progress.ini` (no `.best`) reads as before. Fixture `fractals.ini` gains `rainbow-spiral.best = 6` → `turtle-rainbow.png` shows it |
| 2 | **Stars per pack** | **done (the list's foot)** | the pack drop-down (220 px) has no room beside *3. Variables, words and figures*, so the total is drawn at the **foot of the level list** (a rule, a star, *"This pack: 21 / 30"* / *« Ce recueil : 21 / 30 »*; just the numbers if the text were wider than the list) when the levels leave room (no scrolling — always at 920 × 600 with ≤ 11 levels). The first French try (*"… étoiles dans ce recueil"*) overflowed in the French shot and was shortened |
| 3 | Progress on FileKit's `fk_kv` | **skipped** | the card link (`turtle.elf` + `lib/filekit.imp.a`, `[app.turtle] needs filekit >= 1.96`) cannot be built or checked here (no AArch64 compiler), and the byte identity does not hold in general: `kv_save` writes a section again where it comes back in insertion order (`[Sam] … [Alex] … [Sam]`), `fk_kv_set` appends to the section's first block — the rewritten file would differ after a second player joins. Left to a round with the card build (HANDOFF follow-up) |
| 4 | A **FUNCTION** chip | **done** | `CONTROL_WORDS` / `CONTROL_FR` + `FUNCTION` / `FONCTION`, `NEED[5] = 14` (from `function` in `ORDER`, so `function` and `recursion` levels); the block `FUNCTION MyValue (x)` / `MyValue = x` / `END FUNCTION` (FR `FONCTION MaValeur (x)` … `FIN FONCTION`); a host test runs both blocks, as written, on `halves` (won, EN and FR). Seen in `turtle-fractal.png` (EN) and in French (*FONCTION*) |
| 5 | A free-drawing **sandbox** | **skipped** | it needs a goal-free, target-free drawing rule in `run_program` (today a drawing level without a solution has no target and a grid level forbids free angles), and it changes AC1's fixed "48 levels, each solution three stars" — a risk to the green state for the PM's last item |

### Commits (Developer C)

- `f3ec473f` — merge of `origin/main` into `AutoDev` (Circuits' card build, FileKit `fk_kv`; no conflict)
- `751b2c6e` — Turtle Quest: your best program per level, the stars of the pack at the foot of the list, a FUNCTION chip (step 13; with `world.h`'s header comment)
- `c35ec311` — Turtle Quest docs: docs/04 §12–§13, docs/03, HANDOFF, IDEAS, exports rebuilt (step 12)
- this section (06)

### Final regression (all run after the last change)

| Test | Result |
|---|---|
| `sh tools/tests/run_turtle_test.sh` (ASan + UBSan, `-Wall -Wextra`) | **ok turtle (48 levels: solved, written back; the errors)** — incl. the new `test_should ()` (best; FUNCTION / FONCTION blocks) |
| `sh tools/tests/run_basic_test.sh` | ok (t9_types, dialect, BASIC in French) |
| `sh tools/tests/run_kvtext_test.sh` (main's, after the merge) | ok (101 + 115 checks) |
| `g++ -Wall -Wextra -fsyntax-only user/Apps/turtle/main.cpp` (simulator flags) | 0 warnings |
| `sh tools/tests/desktop_sim/shots.sh turtle` | the 8 shots; run twice → byte-identical. Against the previous commit: `turtle-editor.png`, `turtle-editor-gems.png` identical; `turtle.png`, `turtle-fr.png`, `turtle-portals.png`, `turtle-fr-gems.png` differ **only in the box (26, 608)–(222, 631)** = the list's new foot line (SHOULD 2); `turtle-fractal.png` also the FUNCTION chip, `turtle-rainbow.png` also *Best: 6*. The script's link errors are other apps' (`print_*`, PrinterKit), as before |
| `SHOTS_LANG=fr SHOTS_PNG=<scratch> sh tools/tests/desktop_sim/shots.sh turtle` | the 8 French PNGs looked at: *Ce recueil : 25 / 33*, *Record : 6*, the *FONCTION* chip, *Gemmes 2 / 4*, the colour message's two lines — all fit. Seen, older than this round: the selected row's **bold** title touches its stars when long (*Le flocon de neige*: the cut is measured in the regular face) — noted in HANDOFF |
| `sh tools/tests/desktop_sim/run.sh` | ok (`wmtest: all passed`, the gallery dumped) |
| `python3 tools/lang/check.py --all` | exit 0, every app 0 missing; `turtle: no lang/fr.txt (0 words)` — expected: Turtle Quest uses its own `L2 / T` pairs, its completeness is checked by `turtletest`'s lint (AC9) |
| `make` from `kernel/` (the card) | **not run**: no AArch64 compiler in the container (`aarch64-none-elf-g++`, `aarch64-linux-gnu-g++` absent) — the user's |

### The acceptance criteria

| AC | Status | Evidence |
|---|---|---|
| AC1 every level solvable, 3 stars | ✅ | `run_turtle_test.sh`: 48 levels, each solution `R_WON` with 3 stars; `total == 48` checked |
| AC2 old packs unchanged | ✅ | `git diff 509e6ff5 -- sdcard/apps/turtle.app/levels/{1,2,3}-*` empty; their 27 solutions win; old negative cases pass |
| AC3 round trip | ✅ | every pack written back and re-read equal on every field incl. the draw mode (0/1/2) |
| AC4 gems | ✅ | `test_gems ()`: out of order EN/FR, in order wins, one left `gem(s)`, `GEM ()`, `ITEM ()`, `FRONT ()` 2, `GEMME ()`, *does not know GEM* |
| AC5 gem numbering refused | ✅ | *"level 2 (gap): gem 2 is missing"*, *"level 1 (twice): two gems 2"* |
| AC6 teleport | ✅ | `test_portals ()`: one `EV_TELEPORT` after the `EV_MOVE`, heading kept, `FORWARD 3` 2 past the twin, no jump back, no segment > 1, `FRONT ()` 5, `WALL ()` false |
| AC7 unpaired pad refused | ✅ | *"the teleporter T has no twin"*, *"three teleporters U"* |
| AC8 colour drawings | ✅ | rainbow solution wins; a colour changed → *not the right colours* (EN/FR); wrong shape → *Not quite the same figure*; `draw-square` + `COLOR 4` wins |
| AC9 texts EN/FR | ✅ | the lint: texts present, not cut, pack titles, concept cards, `GEMME`, colour levels' pens 1–14 (not 7, 8) |
| AC10 unique ids | ✅ | the lint across the five packs |
| AC11 recursion | ✅ | tree / koch / snowflake / sierpinski / snail-rec win; no stop → *calls itself without end* (EN/FR), never *stack* |
| AC12 the rest of the bench | ✅ (PC) / ⏳ card | `run_basic_test.sh` ok; the simulator build ok, 0 warnings; the card's `make` is the user's (no AArch64 compiler) |
| AC13 existing shots | ✅ with a by-design change | `turtle-editor.png` changed by design (Developer B, note 5); `turtle.png` / `turtle-fr.png` now differ only by the list's foot line (SHOULD 2, (26, 608)–(222, 631)) — an intended addition, not a regression |
| AC14 gems and portals shot | ✅ | `turtle-portals.png` (Developer B; foot line added) |
| AC15 fractal shot | ✅ | `turtle-fractal.png` (snowflake, 3 stars), `turtle-rainbow.png` (tinted target, colour message) |
| AC16 French shot | ✅ | `turtle-fr-gems.png`: *Aller et retour*, `GEMME` chip, *Gemmes 2 / 4*; the French run looked at |
| AC17 editor round trip | ✅ | `test_editor ()` (gem placing / cycling, pads, written back equal); `turtle-editor-gems.png` (12 tools, Save refused, the ringed gem) |
| AC18 docs | ✅ | docs/04 §12–§13, docs/03, `world.h` header, HANDOFF, IDEAS, `build_docs.py` run; `tools/pkg/packages.ini` unchanged (the `[*apps]` rule ships `levels/`); **nothing published** |

### What could not be done

- The card build (`make` / `make stage` from `kernel/`) — no AArch64 compiler here; nothing staged, no package
  published (PIPELINE §0.5).
- SHOULD 3 (`fk_kv`) and SHOULD 5 (sandbox) — skipped, reasons above.

### What the user must check on the Pi

1. `make` from `kernel/` builds `turtle.elf` without a new warning; `make stage`.
2. Pack 4: the gems' ring and the HUD; a portal jump's animation at the real pace (speed 1 and 10), F8 through a jump.
3. Pack 5: the snowflake and Sierpinski at full speed (event counts ≤ 1007); the rainbow's tinted target.
4. The window at its **920 × 600** minimum: the French lesson cards (small faces), the *Idea* list opening down.
5. The editor by mouse: Gem (place, cycle, drag), Portal 1 / 2 (a third pad moves the older), *No / Shape / Colours*,
   Save refused with the ringed cell; then the level saved in `SD:/docs/turtle/my-levels.turtle` and re-opened.
6. Win a level twice with fewer instructions: *Best* follows (`progress.ini` `<id>.best`); the list's foot counts.
