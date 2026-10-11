# The nine questions of AUDIT-640.md — the designer's answers

For the PocketTerm35 (640 × 480, a D-pad, no pointer), pocket mode only: **the desktop mode does not change**.
Mock-ups, nothing built. The pictures are made by `tools/screenshot/mockup_pocketterm_apps.py` (the tokens, fonts and
helpers of `mockup_pocketterm.py`); each is the whole screen: the band (30 px) and the app's window (640 × 450).
One contact sheet per question (`q1-slides.png` … `q9-small-windows.png`) and the single screens at their exact size.

## What every answer shares

- **Only the controls that work today**: the D-pad / arrows, A / Enter, B / Esc, L / R (tabs, a side panel's places,
  the drawers), CMD or F10 (the Menu), F6 (the toolbar), Alt + D-pad (scroll), and typed keys. **X, Y, Start and Select
  are not used.** Every command stays in the Menu as well.
- **One selection pattern for a canvas of objects** (Slides, QBStudio's forms): the D-pad goes from object to object
  (by geometry, as the focus ring does over controls); **M** starts the *move* mode and **S** the *size* mode, in which
  the D-pad nudges (8 px; Shift: 1 px), A keeps, B cancels. No free pointer. `UkKeyPointer` stays for what really is a
  surface of points (Circuits' board, the Icon Editor's grid).
- **A grid gets a cell cursor**, not a pointer (Turtle Quest's map).
- **The hint line** (the black 30 px line the Menu already shows at the bottom) appears while a view or a mode has keys
  of its own, and says them with the device's glyphs. It is the only place a mode is announced (a label at its left:
  MOVE, CROP).
- **"Not in pocket" is said once, calmly** (`koton-not-in-pocket.png`): a card with why, what can be done here, a
  button back, and one line saying where the rest is (desktop mode, with a mouse). Never a disabled screen, never a
  cut layout. The command that leads to it stays in the Menu, so that the user learns it exists.
- Cost classes: **small** (hours, one place), **medium** (a day or two), **large** (several days, a layout of its own).

---

## Q1 — Slides

**Decision.** The slide alone. **L = the slides** (thumbnails, a drawer), **R = the inspector** (a drawer). No strip
of thumbnails under the slide. Shapes are chosen by stepping, moved and sized in a mode — not with a pointer.

**Why.** Under the app's bar and the hint line the slide gets 600 × 338 px; a strip of thumbnails (70 px at least)
would shrink it by a quarter to show three slides nobody can read. A pointer driven by the D-pad would need a long
travel and a "held" button for every drag; stepping from object to object is exact and takes one press an object.

**Keys.**

| Where | Control | Does |
|---|---|---|
| The slide | D-pad | the next object in that direction |
| | A / Enter | edit the object's text (B ends) |
| | M / S | the move / the size mode |
| | Bksp | delete the object |
| | F6 | the toolbar (Insert, », Present) |
| | L / R | the slides / the inspector |
| | F5 | present |
| Move, size | D-pad; Shift + D-pad | 8 px; 1 px (the size mode moves the bottom right corner) |
| | A / B | keep / cancel; S or M switches the mode |
| The slides (L) | Up / Down, A | choose, open; Ctrl + M a new slide; L or B closes |
| The inspector (R) | Up / Down, digits | a field, its value typed; Left / Right its tabs; R or B closes |
| Presenting | Right or A / Left | next / previous; B ends |

*Insert* puts the new object in the slide's middle at its usual size, already in the move mode. Guides (the slide's
centre, another object's edge) light up while moving.

**Dropped in pocket.** Dragging, free rotation, the notes' pane under the slide (View > Notes opens them full screen),
animations' timeline, *Master and layouts…* → the "not in pocket" card (desktop mode).

**Cost: large.** The two panels as SidePanels (the pattern exists), the theme's colours and the translation due; the
object stepping and the two modes are new code in the app — to write once and share with QBStudio (Q6).

Pictures: `slides-edit`, `slides-thumbnails`, `slides-inspector`, `slides-move`, `slides-present`.

## Q2 — Paint

**Decision.** In pocket **Paint adjusts a picture; it does not paint.** The picture and ONE row of tasks — Crop,
Resize, Rotate, Adjust, Text — which is UIKit's toolbar at the bottom (Left / Right, A). No ribbon, no brushes, no
shapes, no layers' panel.

**Why.** Freehand drawing with a D-pad is a party trick: the result is worse than nothing and nobody does it twice. What
people do with a picture on a handheld is crop it, turn it, resize it for a mail, fix its light, write a word on it —
all of which are a frame, a number or a slider: made for keys. The two colours need no home because nothing draws
(Text asks its one colour in its own row). Pixel work by keys already has its app: the Icon Editor, with the key
pointer on its grid.

The alternative (the ribbon's groups as tabs under L / R, the two colours first in the row, a key pointer whose A held
is the brush down) is drawn in `paint-brush-alternative.png` to be fair. **Not recommended.**

**Keys.**

| Where | Control | Does |
|---|---|---|
| The picture | Left / Right, A | a task, start it |
| | + / −, Alt + D-pad | zoom, scroll |
| | Ctrl + Z, Ctrl + S | undo, save |
| Crop | D-pad; Shift + D-pad | move the frame; size it |
| | L / R | the ratio (Free, 1:1, 4:3, 16:10, 16:9) |
| | A or Enter / B | apply / cancel |
| Resize | digits | width, height (proportions kept), or a percentage |
| Rotate | Left / Right | 90° steps; Up / Down flip |
| Adjust | Up / Down, Left / Right | a slider (brightness, contrast, saturation), its value |

**Dropped in pocket.** Brushes, shapes, fill, the gradient, selections, layers (a file with layers opens flattened
*for viewing*; saving it asks for another name so the layers are not lost) → the card: "Drawing needs a pointer".

**Cost: medium–large.** A pocket front of its own (one screen and five small modes) on the Image group's existing
operations; the ribbon is not migrated at all.

Pictures: `paint-view`, `paint-crop`, `paint-brush-alternative`.

## Q3 — a closed drawer's tab

**Decision.** **No tab, no gutter, no handle.** A closed drawer takes no pixel of the content. **The band names it
beside its button**: `[L] Calendars` after *Menu* for the left drawer, `Inspector [R]` before the status icons for the
right one. The chip is lit (Aqua) while the drawer is open. The same for every app, left and right. When a pointer
exists (a mouse is plugged in and has moved), the tab comes back — it is a pointer's target.

**Why.** A tab is something to press; here nothing can press it, so it only hides content (an hour's digit, an icon).
A 20 px gutter costs every app 3 % of its width for ever to show a mark that does nothing; a 6 px handle says nothing
to someone who does not already know. What the user needs is the *name* and the *button*, and the band is where the
shell already says what the buttons reach (Menu).

**Keys.** L / R open and close; B closes; in the drawer Up / Down and A.

**What it asks.** The app (through UIKit's SidePanel, no app code) tells the shell its drawers' names, as it publishes
its menus; the band draws the chips. Two short names, 12 characters at most each (the band has room at 640: see
`drawer-closed.png`; with both chips and a long app name the right one loses its name first, keeping `[R]`).

**Cost: medium**, once (UIKit's SidePanel + the band), then nothing per app. Turtle Quest and GPIO Lab take their
20 px gutter back.

Pictures: `drawer-closed`, `drawer-open` (and Q1, Q6, Q7 use it).

## Q4 — Koton

**Decision.** **Koton in pocket plays a project and mixes it. Composing is not in pocket.** Two views behind L / R:
**Song** (the tracks as rows, their blocks, the playhead) and **Mixer** (a strip a track). The transport is in the
app's bar, always.

**Why.** The piano roll, the sound chains and the generators are surfaces to draw and drag on; folded onto 640 × 450
they become what the capture shows. A handheld is where one listens to yesterday's project, balances it, mutes a
track, changes the tempo, exports it. For writing music with keys Onyx has a keyboard's instrument already: FM Tracker.

**Keys.**

| Where | Control | Does |
|---|---|---|
| Both | L / R | Song / Mixer |
| | Space | play / pause; Home: to the start |
| | A, S | mute, solo the chosen track |
| Song | Up / Down; Left / Right | the track; the playhead, bar by bar |
| Mixer | Left / Right; Up / Down | the track; its level (Shift: fine) |
| Menu | | Open, Tempo…, Export to WAV…, *Compose with AI…* (a text and a button: it works by keys) |

**Dropped in pocket.** The piano roll, the editor's sound chain and effects, the generators' list, moving blocks →
the card of `koton-not-in-pocket.png`, with *Open FM Tracker* beside *Back to the song*.

**Cost: large** (two new views), but bounded: no editing code is touched.

Pictures: `koton-song`, `koton-mixer`, `koton-not-in-pocket`.

## Q5 — Setup

**Decision.** One page a step. The steps' column becomes **the step's title and "3 of 7" with seven segments** in the
bar; a foot with **Back on B** and **Next on A**. **Yes, the welcome page draws the device's buttons** — four of them,
once: D-pad, A, B, CMD.

**Why.** It is the first screen of a machine without a mouse: if the user does not learn there that A goes on and B
goes back, nothing else on the device will be found. After that page the foot's two buttons carry their glyph on every
step, which is reminder enough.

**The pages that need a short form.**

- **Region & keyboard**: three list rows (country, time zone, layout), each opening a full-width list where typing
  filters. On this device the layout is preselected (the built-in keyboard) and the test field is kept.
- **Wi-Fi**: the networks as full-width rows of 46 px; *skip* is a row of the list, not a small link; **the password
  has its own page** (one field, shown by default — a hidden password on a thumb keyboard is a locked door).
- **Display**: nothing to choose on the built-in screen → the step is skipped on this device ("6 steps").
- **Appearance**: the themes as full-width rows with a small swatch each, one preview above; no grid of previews.
- **Name & privacy**, **Ready**: as they are, in one column.

**Keys.** Up / Down a choice; Left / Right a value (the language); A / Enter next; B / Esc back; typing in a list
filters it.

**Dropped.** The left column, the welcome page's list of what is coming (the segments say it), the Display step here.

**Cost: large** (seven pages laid out for 640 × 450) — but each is a UIKit Form of rows; the window must stop being a
fixed 800 × 600.

Pictures: `setup-welcome`, `setup-wifi`, `setup-wifi-password`.

## Q6 — QBStudio

**Decision.** **The form OR the code, never both** (F7 goes from one to the other — VB's key; a two-part switch in
the bar says where one is). **L = the project and the toolbox** in one drawer, **R = the properties** (and events).
A control is placed **on the form**, not in a tree: the toolbox adds it after the chosen control and puts it in the
move mode; the D-pad then steps from control to control exactly as on a slide (Q1).

**Why.** Code needs the width (68 columns at 15 px); a 540 × 300 form fits the window at its real size, so the form
can be shown true and a tree would only hide what it looks like. The outline is still there: it is the tab order, and
the D-pad follows it.

**Keys.**

| Where | Control | Does |
|---|---|---|
| Both | F7 | form / code |
| | F5 | run; Check in the bar (F6) |
| | L / R | the project and toolbox / the properties |
| Code | typing; Alt + D-pad | — ; scroll |
| Form | D-pad | the next control |
| | A / Enter | its code (the default event) |
| | M / S | move / size on the 8 px grid (as Q1) |
| | Bksp | remove |
| Toolbox (L) | Up / Down, A | a control; add it after the chosen one |
| Properties (R) | Up / Down, A | a property; edit it (typed, or a list) |

**Dropped in pocket.** Split, the tabs of open files in a strip (the project's list in L replaces them), dragging from
the toolbox, rubber-band selection.

**Cost: large**; the stepping and the modes are Q1's code.

Pictures: `qbstudio-code`, `qbstudio-form`, `qbstudio-toolbox`.

## Q7 — 3DForge

**Decision.** **View a part and change its values; no sketching, no new step in pocket.** The D-pad **always turns the
view** — there is no second mode, so nothing to show. The steps are the L drawer; a step's numbers (a height, a
diameter, a count) can be retyped and the part is rebuilt. R holds Display (edges, grid, see-through).

**Why.** A sketch is points placed and constrained by eye on a plane: the worst case for a D-pad on 3.5 inches. But a
parametric part is *numbers*, and numbers are a keyboard's: the useful thing on the bench, part in hand, is "make this
12 a 15, export, print".

**Keys.**

| Where | Control | Does |
|---|---|---|
| The view | D-pad | turn; Alt + D-pad slides; + / − zoom |
| | 1, 2, 3, 0 | front, top, right, home |
| | F6 | the bar (Export STL) |
| Steps (L) | Up / Down | a step (it lights up on the part) |
| | A, digits, Enter | open it, type a value, rebuild; B undoes |
| Display (R) | Up / Down, A | a switch |

**Dropped in pocket.** The ribbon (Shapes, Sketch, Extrude, Fillet, Chamfer, Move), Manufacture → the card: "A new
step needs a pointer"; said in the steps' drawer too. A pill in the bar, *VIEW AND VALUES*, says the mode at a glance.

**Cost: medium.** The view's keys, the steps' panel as a SidePanel with its values editable; nothing of the ribbon.

Pictures: `3dforge-view`, `3dforge-steps`.

## Q8 — Turtle Quest's level editor

**Decision.** **Offered, in two steps**: *1 of 2, the texts* (a form), *2 of 2, the map* with one row of tools.

**Why.** A grid of cells is a D-pad's natural ground: a cell cursor, one press a cell — this is how console level
editors have always worked, and it is faster here than a mouse. The panel was too tall only because it stacked a form
on a toolbox; apart, each fits.

**Keys.**

| Where | Control | Does |
|---|---|---|
| The texts | Up / Down, A | a field, edit it; F2 the map; B back to the levels |
| The map | D-pad | the cell |
| | A | put the tool's cell; **A held + D-pad** paints a run |
| | L / R | the tool (Wall, Floor, Turtle, Flag, Coin, Door, Erase) |
| | F5 | try the level; B back to the texts |

The Turtle and the Flag exist once: putting one moves it.

**Dropped.** Nothing. The 12 tools become 7 in the row; the rarer ones (the turtle's heading, a cell's count) are asked
when the cell is put, or are in the Menu.

**Cost: medium.**

Pictures: `turtle-editor-texts`, `turtle-editor-map`.

## Q9 — the desktop's small windows

**Decision.** Three answers, one each.

- **The clipboard's history: a sheet** over the app in front, like quick settings — Ctrl + Shift + V, and *Edit >
  Clipboard history* in every Menu. Up / Down, A pastes into the app under it, Bksp forgets an entry, B closes. It is a
  system function that is *more* useful without a pointer (no selection to drag).
- **Stickies: an ordinary full-screen app**: its notes as cards, two columns; D-pad, A writes in a note, Ctrl + N a new
  one. The notes keep their colours — they are the content. No floating note.
- **The Agenda: no window.** Its content is one line on Home ("Today — 14:00 Team call") and the lock screen's third
  line (both already drawn in the shell's mock-ups); A on it opens the Calendar.

**Why.** "On the desktop" does not exist in pocket: there is one app at a time. What was glanceable goes where the
user glances (Home), what was a tool on call becomes a sheet, what was a place to write becomes an app.

**Cost: medium** (the sheet: the shell and `clipd`), **small** (Stickies full screen), **small** (Home's line).

Pictures: `clipboard-sheet`, `stickies`.

---

## Review — the four reworked apps

The structure is right (one screen at a time, "‹" bars, drawers): keep it. What I would correct, as small directives.

**All four**

1. **Remove the drawer's tab and the 20 px gutter** (Q3); name the drawer in the band.
2. **Show the hint line** whenever the view has keys of its own (Circuits' pointer, Turtle's Run / Step, GPIO Lab's
   pin): today nothing on screen says that F5, F8 or Enter do anything. Use the device's glyphs.
3. **No word of the mouse**: "Live: *click* a switch or a row", "*click*: put it on…" → "press A on…". Search the
   strings for click / drag / hover.
4. **Light cards in a dark app.** The beige level card (Circuits, Turtle Quest) with its gradient, the pale blue hint
   card and the pastel keyword chips are the desktop's light look: make them `bg1` cards with `text1` / `text2` and keep
   colour for one accent (the level's number, a keyword's dot). The *boards* (the circuit, the maze) keep their own
   colours — they are content.
5. **One type scale**: 13 / 14 / 15 / 18 px. Section labels of 10 px in capitals (TODAY, MODE, CALENDARS) are under
   1 mm tall on this screen: 12 px bold, `text2`.
6. **Outer margin 14 px, 8 px between blocks, 44 px rows** for lists; the bar under the band 40 px. Today the margins
   go from 6 to 24 px from one app to the next.

**Mail**

7. The list's bar is cramped: the search field is cut to "Search the …". Keep the title, *All / Unread* and the pen;
   **search becomes typing** (typing in the list filters, as on Home) and *refresh* goes to the Menu.
8. The category's coloured bar sits against the scroll bar: put it at the row's **left** edge (where the unread dot is)
   and keep the right edge for the time and the star.
9. The message shows reply / reply-all / forward twice (in the bar and under the date): keep the bar's.
10. The message's text is 14 px Liberation while the chrome is the UI face: fine (it is the content), but give it
    15 px and 22 px of line height; the title 20 px is enough (24 today pushes the text down).
11. *Send* beside the reply field needs the A glyph or "Ctrl + Enter" (Enter alone makes a new line).

**Circuits**

12. The gates' row under the level card: Select and the six gates are a toolbar — make *it* the thing L / R step
    (today L opens the levels; the levels can be the "‹ Levels" of the bar instead, as in Turtle's editor), so that
    choosing a gate does not need F6.
13. The truth table's 9 px headers (IN, GOAL) and the 5 "gates" block: align both on the table's left edge; the green
    "5" at 30 px is the screen's biggest thing and the least important — 18 px.
14. Step / Reset / Check: Check is the default (Aqua, with the A glyph when the board has no focus); Reset greyed is
    fine.

**Turtle Quest**

15. The editor shows 10 lines and half of the 11th: size the box to whole lines.
16. The keyword chips take 110 px for something typed on a keyboard: one line of them at most (the others on
    "more…"), or none in pocket — the lesson names the words.
17. Run / Step / Stop / Reset and the speed: one row is enough (the speed's slider after Reset); gain 30 px for the
    code.
18. Hint / Lesson are black buttons on beige: after 4, two plain `bg2` buttons at the card's right.

**GPIO Lab**

19. The yellow 3.3 V banner is the loudest thing on the screen at every visit: one line of `warning` text with its
    icon, no filled box (keep the filled box for the first run).
20. *Pins / Code* at the top right and *Timing Chart / I2C Bus / Edges* lower down are two sets of tabs; L / R can only
    step one. Make Pins / Code the tabs (L / R) and the three charts a row the focus reaches.
21. The bottom line is cut ("…a button on …"): two lines, or drop it in pocket; the SIMULATOR pill can go in the band's
    right side or the title.

## Assumed

- X, Y, Start and Select do nothing in apps (as said); M and S are free in Slides and QBStudio's form view (they are
  typed only while a text is being edited); F2, F5, F7 are reachable (Fn + digits).
- CMD alone opens the Menu, so no chord uses CMD: the clipboard's history is Ctrl + Shift + V.
- The band can show two short names given by the app in front (Q3): this needs a small addition to what the app tells
  the shell; if it cannot be done, the fallback is the same two chips at the left and right ends of the app's own bar.
- Slides', Paint's, Koton's, QBStudio's and 3DForge's contents in the pictures are illustrative (the file names, the
  menus' items, the steps of a part); the apps' real commands were not read in their code.
- Nothing was tried on the device; sizes are for 640 × 480 at 229 ppi (one pixel is 0.11 mm).
