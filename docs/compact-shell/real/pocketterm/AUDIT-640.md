# The apps at 640 × 480 in pocket mode — an audit (2026-10-11, the branch `PocketTerm`)

The device: a Waveshare PocketTerm35 — a 3.5" **640 × 480** screen, a D-pad that sends the arrows, A / B / X / Y,
L / R, a thumb keyboard, **no pointer**. The window an app gets under PocketUI is **640 × 450** (the band takes 30).

**How it was made.** Every app of `sdcard/apps/` that has a window was built for the PC against the pocket UIKit and
run under PocketUI's simulator at 640 × 480 in pocket mode, then looked at:
`MEDIA_SHOTS=<shots.sh's folder> AUDIT_PNG=docs/compact-shell/real/pocketterm sh tools/tests/server_sim/pocket640.sh
<out> audit` (one app: `AUDIT_APPS="sheet slides"`). The pictures are beside this file: `audit-<app>-640.png` — the app
**as it was before any fix of this work**; `audit-<app>-640-after.png` where a fix was made here; the apps reworked
for this screen have their own series, `pt-<app>-640*.png`. A Control Panel applet is shown inside the Control Panel,
as on the device. Not looked at: the daemons, the demos (`demo*`, `cppdemo`, `inidemo`, `gpudemo`, `wtkdemo`,
`basicdemo`, the Widget Showcase), the games and the emulators (their picture is their own), Jet, the shell's own
parts (the menu bar, the pocket shell, the lock screen: section 20 of `docs/COMPACT-SHELL-STUDY.md`).

**What the pictures do not show: the keys.** A layout that fits is not yet an app that works without a pointer. What
UIKit gives every app there (docs/04 *The pocket mode*): the arrows move a focus ring over the controls that take the
focus, Enter / Esc, the Menu (CMD or F10) with every menu command, F6 on UIKit's toolbars, L / R on the tabs, the
places of a side panel or — since this work — its drawer, Alt + Up / Down to scroll. What it cannot give: **a view
the app draws and hit-tests itself** (a canvas, a board, a hand-made toolbar, a list with its own rows) answers the
keys only if the app wrote it so. The column *Keys* is **an estimate** — from what kind of app it is, what its
picture shows (UIKit's controls, or its own drawing) and what the user guide says of its keys; it was **not tested
app by app** (only the four reworked apps were driven by keys), and nothing was run on the device: *ok* — its main
path should work by keys; *partly* — the named parts need a pointer; *pointer* — the app's main work needs one. A
pass by the keys alone over every app is the audit's missing half.

## The table

Verdict: **fine** / **minor** (it works, something is cut or wasted) / **broken** (something needed is cut, covered
or unreadable). Fix size: small (under an hour, one place), medium (a day: a panel to move, a toolbar to migrate, a
translation due — CLAUDE.md asks for it when an app is worked on), large (several days, a layout to rethink).

### Reworked for this screen in this work (tested by keys: `pocket640.sh`)

| App | Before | Now | Keys | Left |
|---|---|---|---|---|
| **Mail** | minor: a rail, the list squeezed to 260 px beside a 330 px message; nothing by keys but the list | **three screens** — folders, messages, message — with a "‹" bar, Esc back; writing on the whole screen (`pt-mail-640-1-folders` … `-3-message`, `-compose`); the hint line on each | **driven**: Enter into a folder and a message, Esc back twice, Down ×4 + Enter another folder, R the next folder, Right ×2 the ring on the message's buttons, Down scrolls, Down to the reply's field, typed, Ctrl+N / Esc, Ctrl+R | the accounts' wizard (600 × 470) and *Accounts and settings* (680 × 500) are bigger than the window: **broken**, medium (two dialogs to lay out for 640 × 450); a link in a message's text needs a pointer |
| **Circuits** | broken: the board 190 px wide between the levels and the table; the palette's gates behind a misplaced "»"; everything by the mouse | the levels in a drawer (L), the board 360 px; **a pointer moved by the arrows** on the board, Enter its button; the hint line (PUT, MOVE, WIRE, STEP); dark cards (`pt-circuits-640*`) | **driven**: L, Up, Enter (a level), the arrows (the pointer), 1 (a gate armed), Enter (put), F8 / F7 (step / live), F5 (Check: the result's card) | — |
| **Turtle Quest** | broken: the board 170 px wide, cut; the star thresholds cut | the levels in a drawer (L), the program and the board side by side; one row of buttons with the speed; the hint line; dark cards (`pt-turtle-640*`) | **driven**: L, Up, Enter (a level), a letter typed, F5 (run), F8 (step), F9, F2 (the hint) | **the level editor** (Ctrl+E): its panel is taller than 450 px and its tools and board are a pointer's — *designer* |
| **GPIO Lab** | broken: a 940 × 600 window in a viewport, the right half out of the screen | the header in a drawer (L), the pin's panel and the charts on the width; **R steps Pins / Code**; the status whole; the hint line with SIMULATOR as its pill (`pt-gpiolab-640*`) | **driven**: L, Down, Enter (a pin), Right (its mode), R, R (Code and back), L again | the I2C map shows its first rows only (the chart area is 104 px high): minor, medium |

### The others

| App | Verdict at 640 × 480 | Keys | The fix proposed | Size | A designer first? |
|---|---|---|---|---|---|
| **Letters** | minor: the status bar wrote *(modified)* over *Page 1 of 2* | **driven**: F6, Right, Right (the ring on the toolbar); the hint line says F6, Alt + D-pad, CMD (`pt-letters-640*`) | **done here**: the page / words move right of the name, the name is cut for what is left (`-after`) | small | no |
| **Sheet** | minor → **done here**: both toolbars were cut at the window's edge (a hand-made bar) | ok; the chart and the cells' drag handles are a pointer's | **done**: its bars are UIKit's `ToolBar` — "»" and F6 (`-after`). Left: two rows of tools take 68 px (Letters folds them into one: `foldInto`, priorities to choose) | small (done) + medium | no |
| **Slides** | was **broken** (the slide 180 px wide between the thumbnails and the inspector, light panels) → **fine since Q1 was built** (below): the slide alone, drawers, dark panes | **driven**: L, Down ×2, Enter; R, Right, Down ×2, R; Down ×2 (objects), M, Right ×3, Shift + Down, Enter; S, Right, Down, Esc; Enter, a letter, Esc; F5, Enter ×2, Esc | the thumbnails a navigation SidePanel (a drawer), the inspector an inspector SidePanel (a slide-over), the colours from the theme, the translation due | large | **yes** — Q1 |
| **Paint** | was **broken** (the ribbon cut, the layers 250 px wide) → **fine since Q2 was built** (below): a picture adjusted by a row of tasks | **driven**: A (Crop), Right, Down, Shift + Left ×2, R, A, Ctrl+Z; Right ×2, A (Rotate), Right, Up, B; A, Left, A; Resize / Adjust: A, Esc; Text: A, two letters, A | the ribbon cannot simply overflow: see Q2; the layers an inspector SidePanel | large | **yes** — Q2 |
| **Notes** | fine | ok | — | — | no |
| **Stickies**, **Agenda** | not shown: desktop widgets (they opened no window under PocketUI in the simulator) | — | decide whether pocket has them at all | — | **yes** — Q9 |
| **Text Editor** (tinypad) | fine | ok | — | — | no |
| **RTF Reader** | fine | ok | — | — | no |
| **Cardfile** | minor: *Design* is under the search box; the form's title and the chosen field's label are dark blue on the dark theme (hard-coded colours) | ok (fields by Tab) | its bar to UIKit's `ToolBar`, its colours from the theme, the translation due | medium | no |
| **Calendar** | minor: the drawer's tab covers the hours' first digit (*10:00* reads *0:00*) | partly: the drawer opens by L (new); an event is made and moved with the mouse — *New event* (Ctrl+N) by keys | the generic question of the tab: Q3 | small | **yes** — Q3 |
| **Clock** | fine | ok | — | — | no |
| **Calculator** (tinycalc) | fine (a card in the middle) | ok (the digits typed) | — | — | no |
| **Graphing Calculator** | fine | ok (the functions typed; the arrows pan, + / − zoom) | — | — | no |
| **File Viewer** | fine | ok | — | — | no |
| **Archiver** | minor: the drawer's tab covers the first file's icon | ok | Q3 | small | Q3 |
| **PDF Viewer** | fine (the tab over the page's margin) | ok | — | — | no |
| **Photos** | minor: the empty library's line of help was cut on both sides | partly (the grid's pictures are chosen with the mouse) | **done here**: the line over two lines on a small screen (`-after`). Left: the arrows over the grid | small (done) + medium | no |
| **Media Player** | fine | partly (the transport's buttons are the app's own drawing: Space plays; the lists by the mouse) | the arrows over its lists and its bar | medium | no |
| **Image Viewer** | fine | ok | — | — | no |
| **Icon Editor** | fine (the colours: R opens their panel — new) | pointer (pixels are drawn with the mouse) | the keys' pointer on its grid (`UkKeyPointer`, as Circuits' board) | small | no |
| **FM Tracker** | fine | ok (a tracker is a keyboard's) | — | — | no |
| **Koton** (the music studio) | **broken**: the transport is under *Compose with AI…*, the arrangement is a strip, the editor's text is one word a line | pointer | a studio on 640 × 480 is a new layout, not a fold | large | **yes** — Q4 |
| **IRC** | fine | ok | — | — | no |
| **Telegram** | fine (its look is its own; the contact's column is wide: 150 px) | ok | the contact's column away under 720 px (it already goes under 640 of pane) | small | no |
| **Courier** | **broken**: *Import* is under the environment's list; the collections take 250 px, the welcome page is cut on both sides | partly | P7's unfinished migration: its rail and collections a navigation SidePanel, the translation due | large | no (the pattern exists) |
| **Ledger** | fine (a customer's name cut in *Invoices overdue*) | partly (its tables' rows by the mouse) | the name's column wider there | small | no |
| **Package Manager** | was **broken** (a fixed window in a viewport) → **fine** (below: the medium fixes) | **driven**: R (Installed), Down ×2, L | resizable in pocket, laid out by the width (as the Icon Editor was made in P7) | medium | no |
| **Disks** | fine | ok | — | — | no |
| **Task Manager** | fine | ok | — | — | no |
| **Terminal** | fine | ok | — | — | no |
| **Screenshot** | fine | ok | — | — | no |
| **Setup** (the first start) | **broken**: a fixed 800 × 600 window in a viewport: the right 160 px and the bottom 150 px are out of the screen — and it is the first thing a new card shows | ok inside what shows | its pages for 640 × 450: see Q5 | large | **yes** — Q5 |
| **Control Panel** | fine (one thing at a time: the list, then an applet under "‹ Control Panel") | ok | — | — | no |
| its applets: **Theme, Mode, Panel, Preload, App Settings** | were minor / **broken** (700 × 470 in a 640 × 406 pane) → **fine** (below: the medium fixes): each fits the pane | not driven in the simulator (an applet's keys come from the Control Panel's process); nothing is out of sight any more | each applet's least width down to 640 (five layouts), and the Control Panel's pane scrolled to the focused control | medium | no |
| its applets: **Display, Sound, Keyboard & Mouse, Language & Region, Gamepad, Wi-Fi** | fine (what they show fits in the 640 px) | ok | — | — | no |
| its applet **Printers** | not judged (it drew nothing in the simulator: no printing service there) | — | — | — | — |
| **QBStudio** | was **broken** (the form and the code 185 px wide between three panes) → **fine since Q6 was built** (below) | **driven**: Down (a control), M, Right, Enter; S, Right ×2, Esc; Down; L, Down ×3, Enter (a control added), Enter; R, Down ×2, R; F7, F7 | an IDE on 640 × 480: see Q6 | large | **yes** — Q6 |
| **Onyx BASIC** (qbasic) | not judged (it needs a program; it opened no window alone) | — | — | — | — |
| **3DForge** | **broken**: the ribbon's tools are under *Export* and cut after *Move*; the view is 400 px beside a 225 px panel | pointer (the view is turned and the sketches drawn with the mouse) | see Q7 | large | **yes** — Q7 |
| **Game Library** | minor: with no game, the line naming the file types is cut at the right (with games: fine, checked on this branch) | ok (L / R the systems, the arrows the games) | the line wrapped | small | no |
| **Lisa** | minor: a fixed window bigger than the screen (a viewport: the first lines and the right edge out of sight) | ok | resizable in pocket | small | no |
| **Life**, **Mandelbrot**, **Eyes**, **Shut Down** | fine | ok | — | — | no |

## After the designer's answers (2026-10-11, later)

The nine questions were answered (`docs/compact-shell/mockups/pocketterm/apps/ANSWERS.md`, its pictures beside it) and
the user agreed to all of it. **Built from it so far** (each driven by the keys alone in the simulator:
`pocket640.sh`):

| What | State | Captures |
|---|---|---|
| **Q3, the closed drawer** | **done, in UIKit for every app**: no tab, no gutter while no pointer exists; the band names the drawer beside its button (`[L] Levels` after Menu, `Inspector [R]` before the status icons), lit while open; the tab is back as soon as a pointer event reaches the program. The names travel with the app's menus (no change to PocketUI's protocol nor to the kernel). The Calendar's hours and the Archiver's first icon are no longer covered: both **fine** now | `pt-drawer-calendar-0` (no tab), `-pointer` (the tab back), `pt-drawer-band` (the chips) |
| **Q5, Setup** | **done in its structure**: one page a step, a bar "3 of 5", B back / A next, the welcome page with the device's four controls, dark cards, every part by the keys. **Differs from the mock-up**: the Wi-Fi password stays in the network's row; Region keeps its two columns; the Display AND the Appearance steps are skipped (five steps, not six); the foot keeps its buttons beside the hint. Verdict: **fine** to set a card up; the "You are online" banner's text is hard to read on the dark card (minor) | `pt-setup-640-1-welcome` … `-5-ready`, `-1-welcome-fr` |
| **The review of the four reworked apps** (21 directives) | **done: 1** (no tab, no gutter), **3** (Circuits: no mouse words where the keys drive), **7** (Mail's list bar: title, All / Unread, pen; typing searches), **8** (the account's stripe at the left), **9** (reply icons once), **16** (Turtle Quest: no word chips, 12 whole lines of program), **19** (GPIO Lab's warning a line of text). **Not done**: 2 (the hint line), 4 (the level cards and Circuits' message card still light), 5-6 (type scale, margins), 10-11, 12-15, 17-18, 20, 21 | the `pt-mail-640-*`, `pt-turtle-640*`, `pt-gpiolab-640*` series, taken again |

**The review, finished (2026-10-12)** — every directive but those said here, each driven by the keys in
`pocket640.sh`:

| Directive | State |
|---|---|
| **2, the hint line** | **done, once in UIKit** (`uk_hint_line`, pocket and console only): a 30 px black line at the window's foot, the app's hints with the device's glyphs, a mode's name as a blue pill; an open drawer ends above it. Given to Mail (a set a screen), Circuits (the board, PUT, MOVE, WIRE, STEP, a card, the drawer), Turtle Quest (RUNNING, STEP), GPIO Lab (SIMULATOR / PI HEADER as the pill), Letters (UIKit's standard three: F6, Alt + D-pad, CMD). **Setup keeps its own foot** (B Back, A Continue beside its buttons, as its mock-up): no second line under it |
| **4, light cards** | **done** for the level's card and the message bar of Circuits and Turtle Quest (`bg1`, `text1` / `text2`, the level's number the one accent, the disc keeps the message's colour); the boards keep their colours. **Left light**: the lesson and the result cards over the board (content cards, shown a moment) |
| **5-6, type and margins** | **done in the four apps where it was off**: 14 px round Circuits and Turtle Quest, 8 px between blocks, 44 px rows in Circuits' levels, GPIO Lab's bar 40 px; labels of 9-10 px are 11-12 px bold (Mail's TODAY, Circuits' IN / GOAL). **Not done**: Mail's bar stays 44 px and its list rows 80 px (two lines of text and a preview); Turtle Quest's levels keep 34 px rows |
| **10** | **done**: the message's text 15 px with 22 px lines (the HTML renderer's base size and line factor), its title 20 px |
| **11** | **done**: in the reply's field A (Enter) sends; the hint line says so |
| **12** | **the coordinator's rule instead of the designer's**: L keeps the drawer everywhere; Circuits' gates stay in the palette (1-6, F6) |
| **13** | **done**: the table and the count start at the same left edge, IN / GOAL 12 px bold, the count's number 18 px |
| **14** | Check was already the filled default; the hint line names F5. **No A glyph on it**: A is the board's button there |
| **15, 17** | **done**: the program's box is a whole number of lines; Run, Step, Stop, Reset and the speed are one row |
| **18** | **done by 4**: on the dark card Hint and Lesson are UIKit's plain buttons; they stay under the text at the card's right (a 314 px card has no room beside its title) |
| **20** | **the coordinator's rule, in UIKit**: a left drawer keeps L, R steps the tabs round (a window of tabs without a left drawer is unchanged) — *Pins / Code* are the window's tabs (`uk_segmented_nav`); the three charts are a row the focus reaches |
| **21** | **done**: the status is whole (two lines when needed), the SIMULATOR pill is the hint line's |

Found on the way: a closed drawer's tab was shown in GPIO Lab although no pointer existed (a window resize counted as
a pointer event) — fixed in UIKit.

**Q1, Slides — built (2026-10-12)**, driven by the keys alone (`pocket640.sh slides`: 30 checks; `pt-slides-640-*`):
the slide alone under one row of tools; **L** the slides (a drawer: Up / Down, A opens), **R** the inspector (a
drawer: Up / Down a row, A what a click does, digits a value, Left / Right its tabs); on the slide the D-pad goes from
object to object, **M** / **S** the move / size modes (8 px, Shift 1 px, A keeps, B cancels), A edits the text, Bksp
deletes; a new object arrives in the slide's middle in the move mode; F5 presents (A next, B ends). The stepping and
the modes are **UIKit's `uikit/objnav.h`**, written once (QBStudio's forms use it next). Verdict: **fine** for
viewing, presenting, retouching a deck. **Differs from the mock-up**: the app keeps its toolbar (one row, the rest
behind ») instead of a bar with the file's name, the slide's number, *Insert* and *Present* — Present is F5 and in
the Menu, the slide's number is in the drawer; in the inspector a row of several buttons (alignment, order) gives its
first button only by A (the others: the Menu, the toolbar); "another object's edge" does not light a guide (the
slide's centre lines do); *Master and layouts…* shows no "not in pocket" card (the master view opens and works by
the same keys); the notes are not shown at all (no full-screen notes). Slides stays untranslated but for its pocket
words (the hint line, the drawers' names: `lang/fr.txt`).

**Q6, QBStudio — built (2026-10-12)**, driven by the keys alone (`pocket640.sh qbstudio`: 30 checks;
`pt-qbstudio-640-*`): the form OR the code (F7; *Form / Code* in the bar), **L** the project and the toolbox in one
drawer (Left / Right between its two parts; A opens a file, or adds a control after the chosen one, in the move
mode), **R** the properties and events (Up / Down, A edits); on the form the D-pad steps from control to control
with Q1's code (`uikit/objnav.h`), M / S the modes, A the control's code, Bksp removes. Verdict: **fine** to retouch
a form and its code. **Differs from the mock-up**: a form is a tree of layouts, so "move" is a place on the 8 px
grid only on a Canvas — in a Column, a Row or a Grid the D-pad changes the control's ORDER among its neighbours (the
hint line still says "8 px"); the D-pad steps over the leaves (a container with children is reached by Esc from one
of them, then M / S work on it); the app keeps its toolbar (New, Open, Save… Run, Check) with *Form / Code* at its
right, not the mock-up's bar with the file's name; the problems keep a 40 px strip under the code. QBStudio stays
untranslated but for its pocket words.

**Q2, Paint — built (2026-10-12)**, driven by the keys alone (`pocket640.sh paint`: 27 checks; `pt-paint-640-*`): in
pocket Paint adjusts a picture — the picture over one row of tasks (Crop, Resize, Rotate, Adjust, Text), the line
*Drawing needs a pointer…*, no ribbon, no options, no layers' panel. Crop: a frame moved by the D-pad, sized by
Shift + D-pad, its ratio by L / R, A applies; Rotate: Left / Right turn, Up / Down flip, B undoes; Resize and Adjust
are the existing dialogs, by the keys; Text: a box in the middle, typed, A puts it. Verdict: **fine**. **Differs from
the mock-up**: the tasks are a segmented row (text, no icons) and the ratio's choices take its place while cropping;
Adjust is the *Brightness / Contrast* dialog (saturation is in the Menu's *Hue / Saturation*), not three sliders
under the picture; Text has no colour row of its own (the text colour last used) and one size; a picture with layers
is NOT flattened nor saved under another name — it is shown whole and saved with its layers, which loses nothing;
no "not in pocket" card (nothing leads to a drawing tool there: one line says it). Paint stays untranslated but for
its pocket words.

**The medium fixes — built (2026-10-12)** (`pocket640.sh applets pkgman mail setup`):
- **the five applets made for 700 × 470** (Theme, Mode, Preload, App Settings, Panel): in pocket under 800 px the
  Control Panel gives an applet its pane as it is (640 × 410) and each fits its layout to it (`uk_fit_pane`, UIKit):
  **fine** now — nothing out at the right, no sideways scroll (`pt-applet-<name>-640.png`). Mode keeps its cards'
  height (its last line of text gives its room); in Theme the preview picture is cut by 28 px at its right, the rest
  is whole. *Not driven by keys in the simulator*: an applet's keys come from the Control Panel's process, which the
  simulator does not run with it — the check is the pane's size and the right part drawn in it;
- **the Package Manager**: its window is the screen's (640 × 450), the layout fitted; L / R its tabs, the keys in the
  list — **fine**, driven (R, Down ×2, L);
- **Mail's two account dialogs**: 600 × 440 and 624 × 440, whole in the window; *Contacts* and *Accounts and settings*
  are rows of the folders' list in pocket (they were a foot only a pointer reached), B closes the boxes — **fine**,
  driven (Down to the last row, Enter, Esc; Enter on the welcome, Esc);
- **Setup's leftovers**: the Wi-Fi list by the keys (a bar; A chooses), **the password on its own page** (the
  network alone, its field, the password shown), B back; "You are online" readable on the dark card — driven (Down,
  A, four letters, B, Down, A, eight letters, A: connecting). **Still differs from the mock-up**: the rows are 42 px
  (not 46), *Rescan* and *Other network* stay around the list, *Skip* stays in the foot, Region keeps two columns.

**Not built**: Q2 Paint, Q7 3DForge, Q8 Turtle
Quest's level editor, Q9 (the clipboard's sheet, Stickies, the Agenda's line), Q4 Koton, and the medium fixes (the
applets' least width, the Package Manager, Mail's two account dialogs, Courier). Their verdicts in the table above
stand as they were.

## What was fixed in this work (the audit's "small")

- **Letters**: the status bar (above). **Sheet** and **Slides**: their toolbars are UIKit's (`user/Apps/sheet/
  ui_base.h`: the class both share) — the desktop's rows are the same pixel for pixel. **Photos**: the empty
  library's help. **UIKit, for all**: a drawer or an inspector without places opens by L / R, a closed one no longer
  eats the arrows (the Calendar, the Archiver, FM Tracker, the PDF Viewer, the Icon Editor gained it without a line).
- **Not done among the small ones** (they need the app's translation first, or more than their line): Cardfile, the
  Game Library's line, Lisa, Ledger's column, Telegram's column, the Icon Editor's grid by the keys.

## The questions for the UX designer

Each is what must be decided before code; a mock-up at 640 × 450 (the window under the band) answers it.

- **Q1 — Slides.** With the thumbnails and the inspector folded away the slide gets the screen. What stays visible
  while editing: the slide alone with L = the thumbnails and R = the inspector (drawers, as built for the other
  apps), or a strip of thumbnails kept under the slide? And how is a shape placed, moved and resized without a
  pointer — the keys' pointer of Circuits (arrows + Enter), or a list of the slide's objects to choose from, then
  arrows to move and Shift + arrows to resize?
- **Q2 — Paint.** The ribbon (Clipboard, Image, Tools, Brushes, Shapes, Colours, Text) is 1 000 px of tools. On 640:
  one group at a time behind tabs (L / R step them)? A single row of the current tool's options with everything else
  in the Menu? Where do the two colours live so that they are always in sight? And is painting with a D-pad (the
  keys' pointer, Enter held = the brush down) wanted at all, or is Paint on this device a viewer with crop / resize /
  adjust only?
- **Q3 — a drawer's tab.** A closed drawer shows a 20 × 60 px tab on the content's edge; it covers what is under it
  (the Calendar's hours, a file's icon). Three ways: the content leaves a 20 px gutter (as Turtle Quest and GPIO Lab
  do here); the tab goes away on a keys-only device and the band says "L: levels"; or the tab stays but slimmer (6 px,
  as a handle). Which — and the same for an inspector's tab at the right?
- **Q4 — Koton.** A music studio on 640 × 450: which ONE of its views is the screen (the arrangement, the piano roll,
  the mixer, the generators), and how does one go from one to the other (tabs under L / R)? Is composing wanted there,
  or only playing a project and its mix?
- **Q5 — Setup.** The first start on the device itself. The 7 steps' column at the left (210 px) and the page beside
  it do not fit: the steps as "3 of 7" in a bar with Back / Next (A / B), the page under it? Which pages need a
  shorter form (Wi-Fi's list and password, the keyboard's layout, the appearance's previews)? Must the device's own
  buttons be drawn on the welcome page (no mouse: "A: Next, B: Back")?
- **Q6 — QBStudio.** Design, Split, Code: on 640 only ONE of the form and the code at a time (Split away)? The
  project tree and the toolbox as one drawer (L), the properties as the other (R)? And placing a control on a form
  without a pointer: from the toolbox's list into the form's layout tree (a tree edited by keys) rather than onto a
  canvas?
- **Q7 — 3DForge.** Is modelling wanted on this device, or looking at a part (turn it with the D-pad, the steps'
  list, export)? If modelling: the ribbon's groups as Q2's, the view's navigation on the D-pad (arrows turn, L / R
  zoom?) against the arrows as the sketch's pointer — which mode is the default and how is it shown?
- **Q8 — Turtle Quest's level editor.** Its panel (8 fields, 12 tools, 3 buttons) is 560 px tall. Two steps — the
  level's texts, then the map with a row of tools — or not offered on a 480 px screen at all (levels come from another
  machine)?
- **Q9 — the desktop's small windows** (Stickies, the Agenda, the Clipboard's history): they live ON the desktop. In
  pocket: full-screen apps of their own, sheets over the app in front (as quick settings), or nothing?

## Not judged, and why

- **Printers** (applet), **Onyx BASIC** alone, **Stickies**, **Agenda**: no picture in the simulator (above).
- **The file chooser** (`uk_file_open` / `uk_file_save`), the colour and font choosers: shared dialogs every app
  opens — not captured here; they deserve the same pass (a file is chosen by keys in most apps of this table).
- Nothing was run on the Raspberry Pi or on the device: the pictures are the PC simulator's.
