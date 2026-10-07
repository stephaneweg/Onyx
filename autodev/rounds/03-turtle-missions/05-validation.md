# AutoDev round 3 — Validation (Technical Analyst, fresh): Turtle Quest, *Gems, portals and fractals*

Date: 2026-10-06. Reviewed together: `01-product-manager.md`, `02-product-analysis.md` (AC1–AC18),
`03-technical-analysis.md` (steps 1–13, R1–R12, and its appended **GUI plan**), `04-ux-design.md` (D1–D17, §1–§9)
and `mockups/` (pictures, `turtle_mock.patch`, `mockups.sh`, `mkpacks.py`).

Checked against the code: `user/Apps/turtle/world.h` (`W_*` enum ending `W_HEADING, W_COUNT_`; `Level::bool draw`
l. 200; `World::reset / copyFrom / blocks / won`; `EV_*` l. 454; `apply`; `friendly`'s `MAP[]`; `Recorder::fail /
move / ext` — `W_PICK`, `W_ITEM`, `W_FRONT`; `run_program`'s `OWN[]` and not-won messages; `sample / covered /
same_drawing`), `user/Apps/turtle/main.cpp` (`g_big` l. 41/1400, `g_edDraw` l. 193/1050/1078/1290/1292/1463,
`CONCEPT_KEYS` / `NCONCEPTS = 14` l. 198, `g_edLabels[8]` l. 200 + guard l. 1391, `ev_ms` l. 224, `wrap_lines`,
`TOOL_CH` / `NTOOLS = 9` l. 333, `Board::tile` / `pickCell` l. 356–450, the target `0xD9D6CC` l. 456, the drag test
l. 516, `WordBar` `chips[32]` / `ORDER[]` l. 529–545, `Card` (4-line wrap, `need ()`), `Lesson` and its layout
l. 1345, `ToolPal` names l. 744, `setMinSize (920, 560)` l. 1484), `sdcard/apps/turtle.app/` (3 packs, 27 ids — no
clash with the 21 new ids), `tools/tests/turtle/turtletest.cpp` (round trip compares `draw`, 3 stars per
solution), `tools/tests/run_turtle_test.sh` (globs `levels/*.turtle`: the new packs are picked up),
`tools/tests/desktop_sim/shots.sh` (build line 131 compiles `main.cpp` + the BASIC core; scenario 345–357 with the
`down 123 431` / `down 45 505` clicks; `lang fr`), UIKit headers (`Dropdown` + `setOptions` + `sel` + `Widget::tip`,
`uk_mix`, `uk_rbox`, `uk_rline`, `VPath::circle`, `uk_text_c`, `uk_text_l`, `uk_glyph` + `WKG_CHECK`, `UkFaceScope`,
`FtTextFace`), `tools/lang/check.py turtle` (no `lang/fr.txt`: the app's `L2` / `T` mechanism, as 02 §G says).

## Verdict: **GREEN**

Every acceptance criterion maps to a plan step (03 §4.1, completed by the GUI plan); every widget and drawing
function named exists in UIKit — **no UIKit, FileKit, AppKit, kapi or kernel change** (so no ABI question); the
files, ids, map characters (`1`–`9`, `T`, `U` clash with nothing in `# . * k c D p ^ > v <` and space) and test
commands are right; the 03 ↔ 04 decisions agree (R1, R2, R3, R9 settled by 04; the messages, card texts, titles and
`FRONT`'s wording in 04 override 02's drafts, as the GUI plan states). Bilingual EN/FR is covered by the app's own
`L2` / `T` and checked by the host test (AC9) and the French shots; CLAUDE.md is respected (everything stays beside
the app — one program uses it; no new source file, so no new MIT notice beyond the app's existing ones; docs/04,
docs/03, HANDOFF, IDEAS, `build_docs.py` planned; package declared through `[*apps]`, **not published**; branch
`AutoDev` only).

No gap sends the round back. The notes below are **binding for the Developer** (step numbers are 03 §4's).

## Binding notes for the Developer

1. **The Lesson box must not shrink when the card grows to 6 lines (steps 9, 11).** 04 D8 measured the six cards
   in a 360-px box with today's **4-line** card (the mock patch did not apply D9's 6-line wrap — `turtle_mock.patch`
   l. 258 still passes 4). `layout ()` sizes the Lesson from the board (`lh = min (board.height − 30, 360)`, l. 1345),
   and the board loses ~30 px when the card takes 6 lines: the FR *color* card (ends at 301 of 304) would lose its
   last line on `rainbow-spiral`, whose text is longer than 4 lines. Fix: centre the Lesson on the **whole right
   column** (card + board: from `PAD` to the message bar) so its 360 px no longer depend on the card; if a card
   still does not fit at **920 × 600**, draw that card's text in `g_small` (11 px). Verify all six cards, EN and FR,
   at 1000 × 640 **and** 920 × 600 (a `dump` or extra shots in a scratch folder; the repository's shots stay as
   planned).
2. **`check_level` gives one verdict, two wordings (steps 2, 3, 10).** It returns the fault (kind, number or pad,
   cell `badC / badR`); `parse_pack` formats the English reason prefixed *"level %d (%s): "* (*"gem 2 is
   missing"*, *"two gems 3"*, *"the teleporter T has no twin"*, *"three teleporters U"* — AC5 / AC7 assert on these),
   the editor formats 04 §7's sentences in `g_lang`. The two can never disagree on *whether* a level is refused.
3. **`gem_at` reads the world's cell (`w.at`), not `L->map`**, so a picked gem reads 0 (`GEM ()`, `ITEM ()`,
   `FRONT ()`, the ring); the editor's map reads `L->map`. `World::reset` counts `gemsTotal` only when `!lv.draw`;
   `copyFrom` copies `gems`, `gemsTotal` and the pads (R5 — host-tested).
4. **The teleport sits inside `move`'s grid path after the step's `EV_MOVE`**, never after an `EV_BUMP` (the step
   failed) and never in a drawing level; `blocks ()` stays unchanged (`T`, `U`, digits are floor).
5. **AC13's reading:** `turtle-editor.png` changes **by design** (04 §9: the panel's new order, the 4th tool row;
   Test click → (45, 539)). `turtle.png` and `turtle-fr.png` must stay as they are; if D9 (6-line card, title
   fallback) changes them because an existing text was cut before, regenerate them and say so in
   `06-development.md` (an intended fix, not a regression).
6. **AC17's wording:** the Drawing option is **"Colours" / "Couleurs"** (04 D12), not "Shape and colours".
7. **Replacing `g_edDraw` touches five places:** its declaration (l. 193), `edit_collect` (l. 1050:
   `L.draw = g_edDrawSel->sel`), `open_editor` (l. 1078), `retitle` (l. 1290 and the `bts[]` tooltip array l. 1292),
   the panel build (l. 1463). `g_edLabels[8]` → `[10]` **and** its guard `g_nedLabels < 8` (l. 1391) → `< 10`.
   `NCONCEPTS = 20`; the six keys after `"draw"` in `WordBar::ORDER` (R9).
8. **Wording:** the player-facing word is **portal / portail** (04 §7, `FRONT ()`'s help *"5 a portal"*); the pack
   format and the parse reasons may say *teleporter*. docs/04 §13 uses *portal* and gives the map letters.
9. **Pack 5 colour levels:** pens 1–14 except 7 and 8, and `PENUP` over lines already drawn (03 §2.4's lint) — this
   is stricter than AC9 and satisfies it.
10. **Level texts:** keep `text` / `text.fr` ≤ ~150 characters (04 §5) and prefer ≤ 120 in French: at the 920-px
    minimum the card is narrower (`tq-min-fr.png` shows a 4-line cut) — a cut there is the existing behaviour, not
    a failure, but shorter texts avoid it.
11. **Not done here, by rule:** the card build (`make` from `kernel/`) is left to the user (no AArch64 compiler —
    R10); no package publishing (`tools/pkg/packages.ini` unchanged, stated in 06); `mockups/turtle_mock.patch` is
    never applied to the repository's sources (lift pieces by hand). Docs per step 12 + the GUI plan's step 12
    (the editor's panel order, the 920 × 600 minimum, the new screenshots), then `python docs/build_docs.py`.
