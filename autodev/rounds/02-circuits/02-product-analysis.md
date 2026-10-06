# AutoDev round 2 — Product Analyst: Circuits

Date: 2026-10-06. Input: `01-product-manager.md` (the pick: **Circuits**, a combinational logic-circuit puzzle
game, the pair of Turtle Quest; its MUST / SHOULD / deferred scope is kept as it is here).

Looked at: Turtle Quest (`user/Apps/turtle/world.h` — the pack parser, `concept` lesson cards compiled in, `par`;
`user/Apps/turtle/main.cpp` — the `progress.ini` key/value store: a section per player, `<id>` = stars,
`<id>.code`, `seen.<concept>`; the F5/F8/F7/F9/F1/F2 keys, the message bar with its stars, the levels list with
three stars a row), `sdcard/apps/turtle.app/levels/*.turtle`, `sdcard/apps/turtle.app/app.txt`,
`sdcard/etc/fileassoc.ini` (`turtle = turtle`), `tools/pkg/packages.ini` (`[app.turtle]`, `opens = turtle`),
`docs/04-USER-GUIDE.md` §12 (the catalog row) and §13 (*Turtle Quest*), UIKit's `lang.h` (`TR()`) and
`tools/lang/check.py`, `sdcard/apps/ledger.app/lang/fr.txt` (the catalogue's format).

Every mission's minimum gate count below was **checked by an exhaustive search** (a throw-away brute-force
program over all circuits of 0…5 gates built from the level's allowed parts, a gate's two inputs allowed to be
the same signal), except the 2-bit adder, whose 7 is the reference construction (half adder + full adder) — see
§6.4.

---

## 1. The name

**Circuits** — `user/Apps/circuits/`, `SD:/apps/circuits.app/`, `app.txt`: `name = Circuits`,
`category = Programming` (beside Turtle Quest, QBStudio and GPIO Lab), `opens = circuits`. Started from the dock's
Programming drawer, or `run circuits`. In French the app keeps its name (*Circuits* is French too).

## 2. The one-line pitch

**Circuits: wire logic gates on a board until the lamps light exactly as the truth table says — from one wire to
a two-bit adder, in 20 missions.**

## 3. The users and their tasks

| User | Task | How Circuits serves it |
|---|---|---|
| A pupil (10–16), after Turtle Quest | Learn what AND / OR / NOT *are*, by doing | Levels one idea at a time, a lesson card when a gate first appears, the lamps react live as switches are toggled |
| A student (technology, NSI, electronics) | See why NAND is "universal", how an adder is made | World 1 ends with NAND-only circuits, world 3 builds a half adder → full adder → 2-bit adder |
| A parent / teacher | Follow a child's progress; set an exercise | The levels list with each level's stars; a pack is a plain text file a teacher can write and hand out (`.circuits`, opened by a double click) |
| The tinkerer | Find the smallest circuit | The stars count the gates: 3 stars only for the best known count — a real puzzle even for those who know the answer |
| Anyone | Understand *how* a signal goes through | Step by step (F8): one gate depth at a time, the wires lighting as the signal travels |
| A French-speaking user | Play in French | The UI through `TR()` + `lang/fr.txt`, the levels' texts through their `.fr` keys, the language from `SD:/etc/system.ini` |

## 4. Features

### MUST (this round)

**The window** (one window, as Turtle Quest's: a menu bar, a left column, the board, a message bar).

1. **The levels list** (left column, top): the packs as headers (*1. Gates*, *2. Combining*, *3. Arithmetic*),
   each level a row with its number, title and **three stars** (gold for those won, grey otherwise); a
   **padlock** on a level not yet open. A click on an open level shows it.
2. **The objective panel** (left column, below the list): the level's title, its text (what to do), and its
   **truth table** — one column per input, one per output; after a Check, an *obtained* column per output
   beside the expected one and **each wrong row marked** (red background + a cross); right rows ticked.
3. **The board**: a grid canvas (a custom UIKit `Widget`). The level's **input switches** fixed on the left edge
   (named, e.g. `A`, `B`, `Cin`), its **output lamps** fixed on the right edge (named, e.g. `S`, `Cout`); they
   cannot be moved or deleted.
4. **The palette** (a `ToolBar` above the board): one button per part **allowed by the level** (from NOT, AND, OR,
   XOR, NAND, NOR — 2-input gates, NOT 1-input) + a **Select** tool + **Wire** is implicit (drag from a pin). A
   part not allowed is not shown. A level that allows none (the first) shows only Select.
5. **Placing parts**: click a palette button then a free cell, or drag the button onto the board; parts snap to
   the grid; a part cannot overlap another (refused, the drop is cancelled). Max **48 gates** on a board.
6. **Wiring**: press on an **output pin** (of a switch or a gate), drag, release on an **input pin** (of a gate or
   a lamp): a wire is made, drawn as orthogonal segments routed automatically between the two pins. An output
   may feed **any number** of inputs; an input takes **one** wire (wiring an input already fed **replaces** its
   wire). Released elsewhere: nothing is made. Wiring an output to an input of the **same part, or of any part
   that already depends on it** (a loop) is **refused** with the message *"That wire would make a loop: in this
   game signals only go forward."* (FR: *« Ce fil ferait une boucle : dans ce jeu, les signaux vont seulement
   vers l'avant. »*).
7. **Editing**: select a part by clicking it (a frame), move it by dragging (its wires follow), **Delete**/
   Backspace removes the selected part and its wires; a click on a wire selects it, Delete removes it; a
   right click on a wire or part removes it too. **Undo / Redo** (Ctrl+Z / Ctrl+Y, *Edit* menu): every place,
   move, wire, delete, clear — at least 64 steps. *Edit ▸ Clear Board* removes every gate and wire (undoable).
8. **The gates drawn** as the classic (ANSI "distinctive shape") symbols with `VPath`: NOT a triangle + bubble,
   AND a D, OR the curved shield, XOR the shield with the extra curve, NAND / NOR with the output bubble; the
   gate's name written small inside or under it (for those who do not know the shapes yet). Switches drawn as a
   toggle showing 0/1; lamps as a bulb, lit yellow on 1, dark on 0.
9. **Live simulation** (the default mode): a click on a switch toggles it; every wire and lamp shows its level at
   once — **wire at 1 lit (bright colour), at 0 dark**; a wire from an unconnected / undetermined source **grey**
   (an unconnected gate input counts as 0 in live mode).
10. **Step by step**: **F8** (*Simulate ▸ Step*) enters step mode: all gate outputs become *unknown* (grey); each
    F8 evaluates **one depth more** (depth 1 = gates fed only by switches; depth n = gates whose inputs are all
    known), the wires of the newly computed gates lit; the message bar says *"Step 2 of 3"*. After the last
    depth, the lamps are lit and the message says so. **F9** (*Reset*) goes back to step 0; **F7** (*Live*) or a
    change on the board leaves step mode. Toggling a switch in step mode resets to step 0.
11. **Check** (**F5**, a *Check* button in the message bar): every combination of the inputs (2^n rows, up to 16)
    is evaluated, the truth table panel filled (§4.2). Before evaluating, a board with **a lamp not wired** or **a
    gate input not wired** is refused with a message naming it (*"Lamp S is not connected."*, *"An AND gate has an
    input not connected."*) and the gate is outlined in red. All rows right → **won**: the message bar shows *"Well
    done! 3 gates."* and **1–3 stars**; otherwise *"2 rows are wrong."* and the first wrong row is selected in the
    table (its inputs set on the switches, so the player sees the wrong lamp live).
12. **Stars by gate count**: every NOT/AND/OR/XOR/NAND/NOR on the board counts (switches, lamps and wires do not;
    an unused gate still counts — remove it). Won → 1 star; gates ≤ the level's 2-star count → 2 stars; ≤ its
    3-star count → 3 stars (the level's `par = <3-star> <2-star>`, as Turtle Quest). The best stars are kept.
13. **Missions**: 3 packs, **20 levels** (§6), shipped as `SD:/apps/circuits.app/levels/*.circuits`.
14. **Unlocking**: the packs in their file-name order form one path. The first level is open; **winning a level
    opens the next** (the last level of a pack opens the first of the next pack). A level once open stays open. A
    pack opened from a file (§4.20) has all its levels open.
15. **Lessons**: a short **lesson card** (a panel over the board, *OK* to close) the first time a level with a new
    `concept` is shown (wire, not, and, or, chain, nand, nor, universal, xor, nandxor, equal, majority, mux,
    parity, adder, fulladder, decoder, compare); **F1** (*Help ▸ Lesson*) shows it again. A lesson shows the gate's
    symbol and its truth table. The cards' texts are compiled in (as Turtle Quest's), in EN and FR.
16. **Hint**: **F2** (*Help ▸ Hint*) shows the level's hint in the message bar.
17. **Navigation**: *Game ▸ Next Level* (**Ctrl+N**, only if open), *Previous Level* (**Ctrl+P**), *Restart Level*
    (clears the board, undoable), *Quit* (**Ctrl+Q**). Starting the app shows the level last played.
18. **Saved progress**: `SD:/apps/circuits.app/progress.ini` (§7.2): the stars of each level, the player's
    circuit of each level (saved at each change of the board, after a short pause, and when leaving the level /
    quitting — coming back to a level shows the circuit as it was left), the lessons seen, the levels open, the
    level last played. One player.
19. **English and French**: every UI word through UIKit's `TR()` / `TRC()`, the catalogue
    `SD:/apps/circuits.app/lang/fr.txt` complete (`python tools/lang/check.py circuits` clean); the levels'
    `title.fr`, `text.fr`, `hint.fr`; the lesson cards in both. The language: the system's (`uk_lang_init`).
20. **Open a pack**: *Levels ▸ Open Level Pack…* (**Ctrl+O**, the shared `FileDialog`, filter `*.circuits`), a
    `.circuits` file **dropped** on the window, or `circuits <path>` (the File Viewer's double click through the
    association, §8): the pack is added to the levels list (after the built-in ones, all levels open) and its first
    level shown. A malformed pack: a `MessageBox` naming the file and the line (*"line 42: the table has 3 rows,
    4 expected"*), nothing added.
21. **Copy to the clipboard**: *Edit ▸ Copy Truth Table* — the level's table as text (tab-separated: inputs,
    expected outputs, and after a Check the obtained outputs; a header line with the names), for a report or a
    worksheet; *Edit ▸ Copy Circuit* — the board as circuit text (§7.3), so a solution can be pasted into a
    mail, a note, or a pack's `solution =`.
22. **About** (*Help ▸ About*): name, version, MIT.

### SHOULD (in this order, if time allows)

1. **Your solved circuits as parts** (*chips*): after winning *Half adder* (3.2), a **HALF ADD** part (2 in →
   S, C) appears in the palette of the levels whose `chips =` names it (3.3 full adder, 3.4 two-bit adder); after
   *Full adder*, a **FULL ADD** part (3.4). The chip is the player's own winning circuit, evaluated by the engine
   as a sub-circuit; it counts as its gates (`chipcount = gates`, the default — stars stay honest) or as one
   (`chipcount = one`), a pack setting. Drawn as a rectangle with its name and pin names.
2. **Paste Circuit** (*Edit ▸ Paste Circuit*): circuit text from the clipboard replaces the board (undoable);
   parts the level does not allow are refused with a message.
3. **Sandbox** (*Levels ▸ Sandbox*): a board with all parts, choosable switches/lamps (1–4 each, named A–D /
   X–W), no objective; the truth table panel shows the circuit's computed table; *File ▸ Save / Open* a
   `.circuit` file (§7.3) in `SD:/docs/circuits/`.
4. **Several players** (Turtle Quest's *Player* menu, up to 8, a section each in `progress.ini`).
5. 3-input AND / OR parts; a short sound on a win (`game.h`'s sfx, AudioKit).

### LATER (deferred, as the Product Manager sized them)

- **Sequential logic** ("Circuits 2"): clock, SR / D flip-flops, counters, **timing-chart objectives** (GPIO Lab's
  chart as the model), feedback loops allowed.
- **Chip designer**: named pins, a chip library, chips inside chips, an ALU world.
- **Level editor** (draw the truth table, choose the parts and the par, test with a solution) — Turtle Quest's
  editor as the model; writes `SD:/docs/circuits/my-levels.circuits`.
- Multi-bit parts and buses, multiplexer / decoder as parts, 7-segment display.
- Don't-care (`x`) rows in objectives; a "fewest depth" (speed) score beside the gate count.
- **Export to GPIO Lab** (wire it for real on the Pi's pins).

## 5. What it does NOT do (this round)

- No **state**: no loops, no flip-flops, no clock, no timing charts — a loop is refused when wired.
- No **timing / propagation delay** modelling: step mode shows depth, not nanoseconds; no glitches.
- No analogue electronics (resistors, LEDs, voltages) — GPIO Lab and the real Pi are for that.
- No level editor, no chip designer, no custom part shapes; no multi-bit wires/buses.
- No more than 4 inputs and 4 outputs per level (16 rows), 48 gates per board.
- No schematic export (SVG/PNG) or printing; no import of other simulators' files (Logisim, etc.).
- No network, no notifications (a game in its own window: its messages go to its message bar).
- Does not change Turtle Quest (its `L2()` strings, its `progress.ini`, its packs) — any sharing of the pack
  parser / key-value store is the Technical Analyst's decision, without changing Turtle's behaviour.

## 6. The missions — 3 packs, 20 levels

Conventions, for every level: inputs listed **most significant first**; the truth table's rows in **binary
counting order** of the inputs (first input = MSB), all 2^n rows present. "Parts" is the palette (the gates
allowed; switches, lamps and wires are always there). "★★★ / ★★" is `par`: the largest gate count still worth 3
stars / 2 stars; any correct circuit wins ★. **Min** is the proven minimum with those parts (exhaustive search),
so ★★★ = Min everywhere: three stars mean "optimal". ★★ is the count of the obvious textbook circuit (so the
textbook answer earns two stars, and the puzzle is to find the better one). The reference solution of each level
(in the pack's `solution =`, used by the host test) reaches ★★★.

Parts abbreviations: N = NOT, A = AND, O = OR, X = XOR, ND = NAND, NR = NOR.

### 6.1 Pack 1 — *1. Gates* / *1. Les portes* (`1-gates.circuits`, 8 levels)

| # | id | Title EN / FR | In → Out | Objective | Parts | Min | ★★★ / ★★ | concept |
|---|---|---|---|---|---|:-:|:-:|---|
| 1.1 | `wire` | First light / Première lumière | A → Out | Out = A | — | 0 | 0 / 0 | wire |
| 1.2 | `not` | Upside down / À l'envers | A → Out | Out = NOT A | N | 1 | 1 / 1 | not |
| 1.3 | `and` | Both at once / Les deux à la fois | A B → Out | Out = A AND B | N A | 1 | 1 / 1 | and |
| 1.4 | `or` | One or the other / L'un ou l'autre | A B → Out | Out = A OR B | N A O | 1 | 1 / 1 | or |
| 1.5 | `and3` | Three keys / Trois clés | A B C → Out | Out = A AND B AND C | N A O | 2 | 2 / 2 | chain |
| 1.6 | `nand` | Not both / Pas les deux | A B → Out | Out = NOT (A AND B) | N A O | 2 | 2 / 3 | nand |
| 1.7 | `nor` | Neither one / Ni l'un ni l'autre | A B → Out | Out = NOT (A OR B) | N A O ND | 2 | 2 / 3 | nor |
| 1.8 | `nandonly` | NAND does it all / NAND sait tout faire | A B → X, Y | X = NOT A; Y = A AND B | ND | 3 | 3 / 4 | universal |

Notes. 1.1: only a wire from A to the lamp — teaches wiring, switches and Check. 1.5: two ANDs chained. 1.6: the
lesson card *names* this function NAND; **NAND is a part from 1.7 on** (the "parts unlocked level after level").
1.6's ★★ = 3 is `NOT A OR NOT B` (De Morgan). 1.7: OR + NOT; ★★ = 3 is `NOT A AND NOT B`; the card names it NOR,
**a part from 2.2 on**. 1.8: X = NAND(A, A) (both inputs on one signal — the lesson *universal* says so); Y =
NAND(n, n) with n = NAND(A, B).

Truth tables (Out / X Y), rows A B = 00, 01, 10, 11 (1.1, 1.2: A = 0, 1; 1.5: ABC = 000…111):

| level | rows |
|---|---|
| 1.1 | 0 1 |
| 1.2 | 1 0 |
| 1.3 | 0 0 0 1 |
| 1.4 | 0 1 1 1 |
| 1.5 | 0 0 0 0 0 0 0 1 |
| 1.6 | 1 1 1 0 |
| 1.7 | 1 0 0 0 |
| 1.8 | X: 1 1 0 0 · Y: 0 0 0 1 |

### 6.2 Pack 2 — *2. Combining* / *2. Combiner* (`2-combining.circuits`, 6 levels)

| # | id | Title EN / FR | In → Out | Objective | Parts | Min | ★★★ / ★★ | concept |
|---|---|---|---|---|---|:-:|:-:|---|
| 2.1 | `nandor` | An OR made of NAND / Un OU en NAND | A B → Out | Out = A OR B | ND | 3 | 3 / 4 | — (universal seen) |
| 2.2 | `xor` | The hallway light / La lumière du couloir | A B → Out | Out = A XOR B (lamp on when exactly one switch is on) | N A O ND NR | 3 | 3 / 5 | xor |
| 2.3 | `nandxor` | XOR made of NAND / Un OU exclusif en NAND | A B → Out | Out = A XOR B | ND | 4 | 4 / 5 | nandxor |
| 2.4 | `same` | Same or not / Pareil ou pas | A B → Out | Out = 1 when A = B (XNOR) | N A O X ND NR | 2 | 2 / 3 | equal |
| 2.5 | `majority` | Majority vote / Vote à la majorité | A B C → Out | Out = 1 when at least two inputs are 1 | N A O X ND NR | 4 | 4 / 5 | majority |
| 2.6 | `mux` | Railway points / L'aiguillage | S A B → Out | Out = A when S = 0, B when S = 1 | N A O X ND NR | 3 | 3 / 4 | mux |

Notes. 2.1: OR = NAND(NAND(A,A), NAND(B,B)). 2.2: min 3 = AND(OR(A,B), NAND(A,B)) (or NOR(AND, NOR)); the textbook
`(A AND NOT B) OR (NOT A AND B)` is 5 → 1 star only; `(A OR B) AND NOT (A AND B)` is 4 → ★★. The card names the
function XOR, **a part from 2.4 on**. 2.3: the classic 4 NANDs: n = NAND(A,B), Out = NAND(NAND(A,n), NAND(B,n)).
2.4: NOT(XOR) = 2; without XOR it would be 3 (`OR(AND, NOR)`) → ★★. 2.5: min 4 = `(A AND B) OR (C AND (A OR B))`;
the textbook `AB + AC + BC` is 5 → ★★. 2.6: min 3 = `A XOR (S AND (A XOR B))`; the textbook
`(A AND NOT S) OR (B AND S)` is 4 → ★★ (the hint gives the textbook idea; the third star is the puzzle).

Truth tables, rows in counting order:

| level | inputs | rows |
|---|---|---|
| 2.1 | A B | 0 1 1 1 |
| 2.2, 2.3 | A B | 0 1 1 0 |
| 2.4 | A B | 1 0 0 1 |
| 2.5 | A B C | 0 0 0 1 0 1 1 1 |
| 2.6 | S A B | 0 0 1 1 0 1 0 1 |

### 6.3 Pack 3 — *3. Arithmetic* / *3. Calculer* (`3-arithmetic.circuits`, 6 levels)

All six gates allowed in every level (N A O X ND NR).

| # | id | Title EN / FR | In → Out | Objective | Min | ★★★ / ★★ | concept |
|---|---|---|---|---|:-:|:-:|---|
| 3.1 | `parity` | Odd one out / Nombre impair | A B C → Out | Out = 1 when an odd number of inputs are 1 (A XOR B XOR C) | 2 | 2 / 3 | parity |
| 3.2 | `half` | Half adder / Demi-additionneur | A B → C S | A + B in binary: S = A XOR B, C = A AND B | 2 | 2 / 3 | adder |
| 3.3 | `full` | Full adder / Additionneur complet | A B Cin → Cout S | A + B + Cin: S = A XOR B XOR Cin, Cout = majority(A, B, Cin) | 5 | 5 / 6 | fulladder |
| 3.4 | `add2` | Two-bit adder / Additionneur 2 bits | A1 A0 B1 B0 → C S1 S0 | (A1A0) + (B1B0) = (C S1 S0) | 7 (ref.) | 7 / 9 | — (adder seen) |
| 3.5 | `decoder` | Decoder / Décodeur | A B → Y0 Y1 Y2 Y3 | Yk = 1 only when AB (A = MSB) is k | 4 | 4 / 6 | decoder |
| 3.6 | `compare` | Comparator / Comparateur | A B → G E L | G = A > B, E = A = B, L = A < B | 4 | 4 / 5 | compare |

Notes. 3.2's outputs are listed C then S so the lamps read as the binary sum (C S = 10 for 1 + 1). 3.3: 2 XOR +
2 AND + 1 OR, reusing A XOR B: Cout = (A AND B) OR (Cin AND (A XOR B)); 5 is the known minimum for a full adder
over 2-input gates (and confirmed by the search); (two half adders + an OR is also 5); ★★ 6 allows one wasted gate, e.g. Cout computed as
`AB + C(A OR B)` without reusing A XOR B. 3.4: S0/C0 a half adder (2), then a full adder on A1, B1, C0 (5) = 7;
a smaller circuit was not searched exhaustively (too large) — if a player finds one, it is 3 stars anyway; the
textbook "two full adders" is 10 → 1 star, ★★ 9 leaves room for one waste. With the SHOULD chips, 3.3 and 3.4 name
`chips = HALF` / `chips = HALF FULL`. 3.5: min 4 = Y0 = NOR(A,B), Y3 = AND(A,B), Y1 = B XOR Y3, Y2 = A XOR Y3; the
textbook 2 NOT + 4 AND = 6 → ★★. 3.6: min 4 (e.g. t = AND(A,B), G = A XOR t, L = B XOR t, E = NOR(G, L)); the
textbook `A AND NOT B`, `NOT A AND B`, `XNOR` ≈ 5–6.

Truth tables, rows in counting order (outputs in the order of the In → Out column):

| level | inputs | rows (per output) |
|---|---|---|
| 3.1 | A B C | 0 1 1 0 1 0 0 1 |
| 3.2 | A B | C: 0 0 0 1 · S: 0 1 1 0 |
| 3.3 | A B Cin | Cout: 0 0 0 1 0 1 1 1 · S: 0 1 1 0 1 0 0 1 |
| 3.4 | A1 A0 B1 B0 | (C S1 S0) = A + B, e.g. row 1011 (2 + 3) → 101 |
| 3.5 | A B | Y0: 1 0 0 0 · Y1: 0 1 0 0 · Y2: 0 0 1 0 · Y3: 0 0 0 1 |
| 3.6 | A B | G: 0 0 1 0 · E: 1 0 0 1 · L: 0 1 0 0 |

### 6.4 How the counts were checked

A throw-away C program enumerated every circuit of k = 0, 1, 2… gates over the level's allowed parts (each gate's
inputs any earlier signal, the same signal allowed twice), stopping at the first k where every target output
appears among the signals. Results: 1.1 → 0; 1.2–1.4 → 1; 1.5–1.7 → 2; 1.8 → 3; 2.1 → 3; 2.2 → 3 (4 with only
N A O); 2.3 → 4; 2.4 → 2 (3 without XOR); 2.5 → 4; 2.6 → 3 (4 without XOR, 4 with NAND only); 3.1 → 2; 3.2 → 2
(5 with NAND only); 3.3 → 5; 3.5 → 4; 3.6 → 4. 3.4 (4 inputs, 3 outputs, 7 gates) is beyond the search; 7 is the
reference. The host test checks that each reference solution wins with 3 stars — not minimality.

## 7. Files read and written

| File | R/W | What |
|---|---|---|
| `SD:/apps/circuits.app/main` | R | the program |
| `SD:/apps/circuits.app/app.txt` | R (launcher) | `name = Circuits`, `category = Programming`, `opens = circuits`, `stack = 4M` |
| `SD:/apps/circuits.app/icon.bmp` | R (launcher) | the icon (a gate with a lit wire) |
| `SD:/apps/circuits.app/levels/*.circuits` | R | the three packs, in file-name order |
| `SD:/apps/circuits.app/lang/fr.txt` | R | the UI's French catalogue (UIKit `lang.h` format) |
| `SD:/apps/circuits.app/progress.ini` | R/W | the progress (§7.2) |
| any `*.circuits` | R | a pack opened (Ctrl+O, drop, argument) |
| `SD:/etc/system.ini` | R (via `uk_lang_init`) | the language |
| `SD:/docs/circuits/*.circuit` | R/W | SHOULD (sandbox) only |

### 7.1 The pack format (`*.circuits`) — Turtle Quest's, with circuit keys

UTF-8 text; `#` comment lines; `[pack]` then one `[level]` section per level; `key = value`; a value on several
lines is given by the following lines each starting with `|` (Turtle Quest's rule); `key.fr` the French of `title`,
`text`, `hint`.

`[pack]`: `title`, `title.fr`; optional `chipcount = gates | one` (SHOULD).

`[level]` keys:

| Key | Required | Meaning |
|---|---|---|
| `id` | yes | unique in all the packs loaded (`[a-z0-9-]`, ≤ 23 chars); the key of the progress |
| `title`, `title.fr` | yes / no | the level's name |
| `text`, `text.fr` | yes / no | what to do |
| `hint`, `hint.fr` | no | the F2 hint |
| `concept` | no | the lesson card shown the first time (§4.15) |
| `inputs` | yes | 1–4 names, space-separated, MSB first (≤ 4 chars each: `A`, `Cin`, `A1`) |
| `outputs` | yes | 1–4 names, likewise |
| `parts` | yes (may be empty) | the allowed gates: any of `NOT AND OR XOR NAND NOR` |
| `par` | yes | `<3-star count> <2-star count>`, 3-star ≤ 2-star |
| `table` | yes | the truth table, multi-line: a header `\| A B \| Out` then 2^n rows `\| 0 1 \| 1`, inputs in counting order; the header names must match `inputs` / `outputs` |
| `solution` | yes in the shipped packs | a reference circuit, circuit text (§7.3) |
| `chips` | no (SHOULD) | the solved levels usable as parts here: `HALF`, `FULL` (→ the levels `half`, `full`) |

Example:

```
[pack]
title = 1. Gates
title.fr = 1. Les portes

[level]
id = and
title = Both at once
title.fr = Les deux à la fois
concept = and
inputs = A B
outputs = Out
parts = NOT AND
par = 1 1
text = The lamp must light only when both switches are on.
text.fr = La lampe doit s'allumer seulement quand les deux interrupteurs sont en marche.
hint = One AND gate: wire A and B to its inputs, its output to the lamp.
hint.fr = Une porte ET : relie A et B à ses entrées, sa sortie à la lampe.
table =
| A B | Out
| 0 0 | 0
| 0 1 | 0
| 1 0 | 0
| 1 1 | 1
solution =
| part g1 AND 8 4
| wire A g1.1
| wire B g1.2
| wire g1 Out
```

Errors refuse the pack with the line number: unknown key in a level is ignored (forward compatible), but a missing
required key, a wrong row count, a row not in counting order, an unknown part name, more than 4 inputs/outputs,
`par` not two numbers, a duplicate `id` are errors.

### 7.2 `progress.ini` — Turtle Quest's layout

```
# Circuits -- the player's progress (written by the game)
pack = 1-gates.circuits
level = nand
open = wire not and or and3 nand

[Player]
wire = 3
not = 3
and = 3
or = 2
and.circuit = part g1 AND 8 4\nwire A g1.1\nwire B g1.2\nwire g1 Out
seen.wire = 1
seen.and = 1
```

Keys without a section: the pack and level last played, the open levels' ids. Section `[Player]` (the SHOULD
several players: one section per player name, `players = Ann|Bob`, `player = Ann`): `<id>` = best stars (0–3),
`<id>.circuit` = the player's board as circuit text (`\n` and `\\` escaped, as Turtle's `kv_save`), `seen.<concept>`
= 1 once the lesson was shown. A missing or unreadable file = a fresh start (only level 1.1 open); unknown keys are
kept when it is written back.

### 7.3 Circuit text — one format for `solution`, `<id>.circuit`, the clipboard, and `.circuit` (SHOULD)

One statement per line; `#` comments; names case-sensitive:

- `part <name> <TYPE> [<x> <y>]` — a gate; `<name>` `[a-z][a-z0-9]*` (≤ 8 chars, not an input/output name),
  `<TYPE>` one of `NOT AND OR XOR NAND NOR` (SHOULD: `HALF`, `FULL`); `x y` its grid cell (left-top), optional in a
  `solution` (then placed automatically, in columns by depth).
- `wire <from> <to>` — `<from>` an input name (`A`) or a gate name (`g1`; a chip's output `h1.S`); `<to>` a gate's
  input pin `g1.1` / `g1.2` (NOT: `.1` only) or an output name (`Out`).
- The level's switches and lamps are implicit (from `inputs` / `outputs`), at fixed places.
- A sandbox `.circuit` file (SHOULD) adds a first line `inputs = A B` and `outputs = X`.

Reading circuit text checks it: unknown part type, a part not allowed by the level, a pin that does not exist, two
wires into one input, or a loop → refused with the line. The text written is canonical (parts in creation order,
then wires), so writing what was read gives the same text.

## 8. File associations

- `.circuits` → `circuits`: a line `circuits = circuits` in `SD:/etc/fileassoc.ini` (beside `turtle = turtle`) and
  `opens = circuits` in `app.txt` and in the package `[app.circuits]` of `tools/pkg/packages.ini` (declared, not
  published, per PIPELINE §0.5). A double click in the File Viewer starts `circuits <path>`: the pack opens (§4.20).
- `.circuit` (SHOULD, sandbox) → `circuits` too, when the sandbox is built.

## 9. How it fits with the other apps

- **Turtle Quest**: the same family — same category, same window layout (levels list with stars, message bar with
  stars, lesson cards, F-keys: F5 / F8 / F7 / F9 / F1 / F2, Ctrl+N / Ctrl+P / Ctrl+O / Ctrl+Q), same pack syntax
  (`[pack]`/`[level]`, `.fr`, `|` lines, `par = 3★ 2★`), same `progress.ini` layout. Only difference: F5 is *Check*
  (there is nothing to "run" — the simulation is live). A user who knows one knows the other.
- **The clipboard** (SystemKit): *Copy Truth Table* (tab-separated text: pastes as columns in Sheet, as text in
  Letters / Notes / Mail), *Copy Circuit* (circuit text; SHOULD *Paste Circuit*).
- **Drag & drop**: a `.circuits` file dropped from the File Viewer opens the pack.
- **File Viewer**: double click on a `.circuits` (association, §8). A pack is plain text: the Text Editor edits it.
- **Notifications**: none (the game speaks in its message bar).
- **Language & Region** applet: the language chosen there is the game's at its next start; docs/04's list of
  translated apps gains Circuits.
- **GPIO Lab**: no link this round (the export is LATER).
- **docs**: a catalog row in docs/04 §12 (Programming, beside Turtle Quest) and a section in §13 like Turtle
  Quest's (the board, the parts, wiring, live / step / check, the stars, the levels, the menus, the files);
  `docs/HANDOFF.md`.

## 10. Acceptance criteria (testable)

Host test (`tools/tests/circuits/circuitstest.cpp`, `tools/tests/run_circuits_test.sh`, built on the PC against the
engine `circuit.h/.cpp` without UI) — **E**; PC desktop simulator (`tools/tests/desktop_sim/`, `shots.sh circuits`,
`SIM_ARGS` / `SIM_OVERLAY` / `SIM_WRITES` fixtures) — **S**; build / tools — **B**.

1. **E** The three shipped packs load without error: 3 packs, **20 levels**, 8 + 6 + 6, ids unique, in the order of §6.
2. **E** For every level, the `table` equals the objective of §6 (the test recomputes it from a C++ lambda per level
   and compares every row) — the packs say what this document says.
3. **E** For every level, the reference `solution` parses, uses only the level's `parts`, passes Check on every row,
   and earns **3 stars** (its gate count ≤ the 3-star par); its gate count equals the **Min** column of §6.
4. **E** Stars: for a level with `par = 3 5`, a correct circuit of 3 gates → 3 stars, 4 or 5 → 2, 6 → 1; an
   incorrect circuit → 0 stars and not won.
5. **E** A wrong circuit fails on the right rows: level `xor` with the board `OR(A,B) → Out` fails exactly on row
   `1 1` (1 wrong row, its index reported); level `and` with `OR` fails on rows `01` and `10`.
6. **E** Check refuses an incomplete board: a lamp not wired → the error "lamp not connected" naming it; a gate input
   not wired → the error naming the gate; neither counts as a played attempt.
7. **E** Loops are refused: on any board, adding a wire from a gate's output to its own input, or to an input of a
   gate upstream of it (g1 → g2 → g1.1), is refused with the loop error and leaves the circuit unchanged.
8. **E** One input, one wire: wiring a second source into an input already fed replaces the first wire (wire count
   unchanged); an output feeding 3 inputs is accepted.
9. **E** Parts: placing a gate type not in the level's `parts` is refused; NOT has one input pin, the others two;
   the board refuses a 49th gate.
10. **E** Step by step: for the reference full adder (depth 3), the evaluator reports 3 depths; after step 1 only
    the depth-1 gates' outputs are known, after step 3 all outputs equal the live evaluation, for every input row.
11. **E** Circuit text round-trip: writing a circuit, reading it back and writing again gives the identical text
    (parts, positions, wires); a text with an unknown part, a missing pin, two wires into one input, or a loop is
    refused with its line number.
12. **E** Pack errors: a pack with a wrong row count, rows out of order, a missing `inputs`, or 5 inputs is refused
    with the right line number; a pack with an unknown extra key loads.
13. **E** Progress round-trip: stars, `<id>.circuit` (multi-line, escaped), `seen.*`, `open`, `pack` / `level`
    written to a `progress.ini` and read back give the same values; a missing file gives "only `wire` open".
14. **E** Unlocking: winning level k opens level k+1; winning the last level of pack 1 opens `nandor`; a better
    result raises the stars, a worse one never lowers them.
15. **E** Undo / redo: after place, wire, move, delete, clear, undo restores each previous circuit exactly (by its
    circuit text) and redo re-applies it; 64 steps are kept.
16. **E** Copy Truth Table text for `half` after a Check with a correct board is exactly the header line
    `A\tB\tC\tS` (and the obtained columns when checked) + 4 rows, tab-separated, `\n`-terminated.
17. **S** `shots.sh circuits` builds and runs Circuits in the desktop simulator and produces at least:
    `screenshots/circuits.png` (a world-2 or -3 level being solved: palette, gates in their shapes, lit and dark
    wires, the truth table), `screenshots/circuits-check.png` (a failed Check: the wrong row marked, the message
    *"1 row is wrong."*), `screenshots/circuits-fr.png` (the same app in French: menus, palette tooltips, message
    bar, level text in French, stars shown).
18. **S** Started with no `progress.ini`, the window shows level 1.1 *First light* with its lesson card, only 1.1
    open in the list (others padlocked); with a fixture `progress.ini`, the stars shown in the list match it.
19. **S** In the simulator, toggling a switch (a scripted click) changes the lamp and the wires' colours on the
    next frame (live mode visible in a screenshot pair).
20. **S** `circuits <pack path>` (via `SIM_ARGS`) adds that pack to the list with all its levels open and shows
    its first level.
21. **S** After winning a level in the simulator, `progress.ini` (via `SIM_WRITES`) holds the level's stars and
    its circuit, and the next level is open.
22. **B** `python tools/lang/check.py circuits` reports no missing French string; every level of the shipped packs
    has `title.fr` and `text.fr` (checked by the host test).
23. **B** `make` from `kernel/` builds `circuits` (listed in `user/Makefile`), `make stage` puts
    `SD:/apps/circuits.app/main`; `app.txt`, `icon.bmp`, `levels/`, `lang/fr.txt` are in `sdcard/apps/circuits.app/`;
    `sdcard/etc/fileassoc.ini` has `circuits = circuits`; `tools/pkg/packages.ini` has `[app.circuits]` with
    `opens = circuits` (not published).
24. **B** Every new source file carries the MIT notice; no kapi change (`kapi_abi.h`, `appkit.abi` untouched); the
    app reaches the system only through the kits (AppKit, UIKit, SystemKit, FileKit).
25. **B** Docs: docs/04 §12 catalog row (Programming) and a §13 *Circuits* section (controls, missions, files,
    screenshots), the translated-apps list in the Language & Region row updated, `docs/HANDOFF.md` updated,
    `python docs/build_docs.py` run.
26. **B** Turtle Quest unchanged in behaviour: `tools/tests/run_turtle_test.sh` still passes and `shots.sh turtle`
    still produces its screenshots.
