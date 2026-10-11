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
| **Mail** | minor: a rail, the list squeezed to 260 px beside a 330 px message; nothing by keys but the list | **three screens** — folders, messages, message — with a "‹" bar, Esc back; writing on the whole screen (`pt-mail-640-1-folders` … `-3-message`, `-compose`) | ok | the accounts' wizard (600 × 470) and *Accounts and settings* (680 × 500) are bigger than the window: **broken**, medium (two dialogs to lay out for 640 × 450); a link in a message's text needs a pointer |
| **Circuits** | broken: the board 190 px wide between the levels and the table; the palette's gates behind a misplaced "»"; everything by the mouse | the levels in a drawer (L), the board 372 px; **a pointer moved by the arrows** on the board, Enter its button (`pt-circuits-640*`) | ok | — |
| **Turtle Quest** | broken: the board 170 px wide, cut; the star thresholds cut | the levels in a drawer (L), the program and the board side by side (`pt-turtle-640*`) | ok (the program is typed; Run / Step by F-keys and the Menu) | **the level editor** (Ctrl+E): its panel is taller than 450 px and its tools and board are a pointer's — *designer* |
| **GPIO Lab** | broken: a 940 × 600 window in a viewport, the right half out of the screen | the header in a drawer (L), the pin's panel and the charts on the width (`pt-gpiolab-640*`) | ok (the focus ring on the panel's controls) | the I2C map shows its first rows only (the chart area is 92 px high): minor, medium |

### The others

| App | Verdict at 640 × 480 | Keys | The fix proposed | Size | A designer first? |
|---|---|---|---|---|---|
| **Letters** | minor: the status bar wrote *(modified)* over *Page 1 of 2* | ok (F6 for the toolbar) | **done here**: the page / words move right of the name, the name is cut for what is left (`-after`) | small | no |
| **Sheet** | minor → **done here**: both toolbars were cut at the window's edge (a hand-made bar) | ok; the chart and the cells' drag handles are a pointer's | **done**: its bars are UIKit's `ToolBar` — "»" and F6 (`-after`). Left: two rows of tools take 68 px (Letters folds them into one: `foldInto`, priorities to choose) | small (done) + medium | no |
| **Slides** | **broken**: the slide is 180 px wide between the thumbnails and the inspector; the panels keep the desktop's light colours; toolbars cut (→ **done here**: "»", `-after`) | pointer (shapes are placed and moved by the mouse) | the thumbnails a navigation SidePanel (a drawer), the inspector an inspector SidePanel (a slide-over), the colours from the theme, the translation due | large | **yes** — Q1 |
| **Paint** | **broken**: the ribbon is cut after *Shapes* (the colours, the text tools are out of the screen); the status bar's texts overlap; the layers take 250 px | pointer | the ribbon cannot simply overflow: see Q2; the layers an inspector SidePanel | large | **yes** — Q2 |
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
| **Package Manager** | **broken**: a fixed 900 × 540 window in a viewport — the search, *Install Updates* and the bottom line are out of the screen | partly | resizable in pocket, laid out by the width (as the Icon Editor was made in P7) | medium | no |
| **Disks** | fine | ok | — | — | no |
| **Task Manager** | fine | ok | — | — | no |
| **Terminal** | fine | ok | — | — | no |
| **Screenshot** | fine | ok | — | — | no |
| **Setup** (the first start) | **broken**: a fixed 800 × 600 window in a viewport: the right 160 px and the bottom 150 px are out of the screen — and it is the first thing a new card shows | ok inside what shows | its pages for 640 × 450: see Q5 | large | **yes** — Q5 |
| **Control Panel** | fine (one thing at a time: the list, then an applet under "‹ Control Panel") | ok | — | — | no |
| its applets: **Theme, Mode, Panel, Preload, App Settings** | minor / **broken** for *Panel*: an applet is 700 × 470 at least, the pane is 640 × 406 — the right 60 px (Mode's third card, Panel's workspaces, App Settings' *Delete*) and the bottom 64 px are reached by the pane's scroll bars only | partly: the pane does not follow the focus to what is out of sight | each applet's least width down to 640 (five layouts), and the Control Panel's pane scrolled to the focused control | medium | no |
| its applets: **Display, Sound, Keyboard & Mouse, Language & Region, Gamepad, Wi-Fi** | fine (what they show fits in the 640 px) | ok | — | — | no |
| its applet **Printers** | not judged (it drew nothing in the simulator: no printing service there) | — | — | — | — |
| **QBStudio** | **broken**: the form's surface and the code are 185 px wide between the project / toolbox and the properties; *Check* is under the view's switch | pointer for the form's design; the code is typed | an IDE on 640 × 480: see Q6 | large | **yes** — Q6 |
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

**Not built**: Q1 Slides and the shared object stepping (M / S modes), Q6 QBStudio, Q2 Paint, Q7 3DForge, Q8 Turtle
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
