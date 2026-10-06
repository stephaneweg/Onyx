# AutoDev round 1 — Product Analyst: Notes

Date: 2026-10-06. Input: `01-product-manager.md` (the pick: *Quick notes with a desktop widget that can
be shown or hidden*, the user's Priority 2 in `docs/HANDOFF.md`).

Looked at: the `agenda` widget (`user/Apps/agenda/main.cpp`: a borderless, back-most, see-through
window, `WIN_FLAG_BORDERLESS | WIN_FLAG_BACKMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA`, its place in
`SD:/apps/agenda.app/config.ini`, re-reads its file every few seconds, the `agenda` IPC service the
Calendar looks up), the Calendar (writes `agenda.txt` for the widget), tinypad (`.txt` / `.md` in
`SD:/etc/fileassoc.ini`), docs/04 §5 (*The agenda widget*, *The clipboard*, *Drag & drop and file
associations*), docs/06 §5 (SystemKit: `notify`, `clip_*`, `trash_move`, `fa_open`), UIKit
(`Textarea`, `ListBox`, `Root::onDrop`), `SD:/etc/autostart`, and the PC simulator
(`tools/tests/desktop_sim/`: `SIM_OVERLAY` sample files, `SIM_WRITES` for what an app saves,
`SIM_ARGS`, the `agenda` scenario in `shots.sh`).

---

## 1. The name

- **Notes** — the application (`user/Apps/notes/`, `SD:/apps/notes.app/`, `app.txt`: `name = Notes`,
  `category = Productivity`). Started from the dock's Productivity drawer, or `run notes`.
- **Stickies** — its desktop widget (`SD:/apps/stickies.app/`, `name = Stickies`, `category = Shell`,
  like Agenda), started by `SD:/etc/autostart` (`run stickies`). Its sources live in
  `user/Apps/notes/` beside the app's (they share the note reading code); the Technical Analyst may
  build it as a second ELF from the same folder.

Plain, short, says what it is; "Stickies" says at once what the widget looks like.

## 2. The one-line pitch

**Notes: jot something down in a second, keep it on the card, and pin it to the desktop as a sticky
note.**

## 3. The users and their tasks

| User | Task | How Notes serves it |
|---|---|---|
| Anyone at the Pi | Write down a phone number, a code, an idea **right now** | `run notes` / the dock: the window opens on a new empty note with the caret in it; typing is saved by itself |
| The household | A **shopping list** kept in view | A note pinned to the desktop: Stickies shows it on the wallpaper, under every window |
| The developer (the user) | A **to-do list** while working on Onyx | Several notes, newest first; the important one pinned; lines ticked off (`[x]`, *should*) |
| A pupil | Notes for a lesson, copied from / to Letters or the Text Editor | Plain text files: copy & paste through the clipboard, *Open in Text Editor*, drag a `.txt` in |
| Anyone | A **clean desktop** for a presentation or a screenshot | *Show Stickies on the Desktop* switched off: the widget goes away, and stays away after a reboot |

## 4. Features

### Must (this round)

**The Notes window**

1. **A list of the notes** on the left (a `ListBox`): each row the note's **title** (its first line,
   or *New Note* when empty), its **date** (today: the time; otherwise the day) and a **dot of its
   colour**; the most recently changed first. A **pin mark** on the rows shown on the desktop.
2. **The editor** on the right (a multi-line `Textarea`): the selected note's whole text. The first
   line is the title (the list follows as you type).
3. **New note**: toolbar button, *File ▸ New Note* (**Ctrl+N**): an empty note, at the top of the
   list, selected, the caret in the editor. Starting Notes when there are **no notes** opens on a new
   empty one (the *empty state*: no blank page with nothing to click).
4. **Automatic saving**: no Save command. A note is written to the card **after a short pause in
   typing (about 1 s)**, when another note is selected, and when the window closes. An **empty note**
   (no text at all) is not kept: it is removed when you leave it.
5. **Delete note**: toolbar button, *File ▸ Delete Note* (**Ctrl+D** when the list has the focus, or
   **Delete** in the list): the note goes to the **Trash** (SystemKit `trash_move`), the next note is
   selected; a notification says *"Note moved to the Trash"*. No confirmation dialog (the Trash is
   the undo).
6. **Colour**: *Note ▸ Colour* — six colours: **Yellow** (default), **Green**, **Blue**, **Pink**,
   **Purple**, **Grey**. Shown as the list's dot and the sticky note's paper on the desktop.
7. **Pin to the desktop**: *Note ▸ Pin to Desktop* (**Ctrl+P**, a check mark when on; also a
   toolbar toggle): the note appears in Stickies. Any number of notes can be pinned; Stickies shows
   up to **6** (the most recently changed pinned ones).
8. **Show / hide the widget**: *View ▸ Show Stickies on the Desktop* (a check mark): on → Stickies is
   started (if it is not running) and the setting kept; off → Stickies is told to quit and the
   setting kept. The setting survives a reboot (Stickies reads it when `autostart` starts it and
   quits at once when it is off).
9. **Edit**: Cut / Copy / Paste / Select All (**Ctrl+X / C / V / A**) through the shared clipboard
   (UIKit's text editing already goes through SystemKit's clipboard); *Edit ▸ Copy Note* copies the
   whole note's text.
10. **Open a note by its path**: `notes <path>` (`SIM_ARGS` in the simulator; Stickies' clicks)
    selects that note.
11. **Drag & drop in**: a **`.txt` / `.md` file dropped** on the list becomes a **new note** (its
    text copied; the file itself is not moved); **text dropped** on the editor goes in at the caret
    (UIKit's `Textarea` behaviour, like tinypad).
12. **Keyboard**: **Ctrl+N** new, **Ctrl+P** pin, **Ctrl+F** to the search field (*should* below;
    without it Ctrl+F does nothing), **Up / Down** in the list, **Tab** between list and editor,
    **Ctrl+Q** / the close button quit (after the save).

**The Stickies widget**

13. **On the wallpaper, at the top right** (the agenda is at the top left; the dock at the bottom):
    the pinned notes as **small paper cards in their colour**, one under the other, each its title
    in bold then its first lines (as many as fit in a fixed height, about 6 lines, word-wrapped,
    `…` when cut). Borderless, back-most (every window covers it), on every workspace — exactly the
    agenda's window kind.
14. **Live**: it re-reads the notes every few seconds (as agenda re-reads `agenda.txt`), so a note
    typed in Notes appears / changes / disappears on the desktop by itself, Notes open or not.
15. **Click a card**: Notes opens (or comes to the front) on that note (`notes <path>`).
16. **Drag the widget's top** (its header line, *Notes*): move it; its place is kept in
    `SD:/apps/stickies.app/config.ini` (as agenda's).
17. **Nothing pinned**: the widget shows a discreet one-line hint (*"No notes pinned — open Notes"*;
    a click opens Notes) rather than nothing, so the user knows it is there; when hidden by the
    setting (8) it shows nothing at all (it is not running).
18. It is the **`stickies` IPC service** (`kapi_ipc_register`), so Notes can find it (to tell it to
    quit, or to re-read at once after a change instead of waiting for its next poll).

### Should (if the round has time; each independent)

- **Search**: a field above the list (**Ctrl+F**): the list shows only the notes containing the
  text (case-insensitive), the matches counted.
- **Checklists**: a line starting with `[ ] ` or `[x] ` is drawn with a box in Stickies; a **click on
  the box in Stickies ticks it** (the note's file changed). In the editor it stays plain text.
- **Sort** by title or by date (*View ▸ Sort by*).
- **Open in Text Editor**: *File ▸ Open in Text Editor* opens the note's file in tinypad.
- **Export…**: *File ▸ Export…* writes the note as a `.txt` elsewhere (the file dialog,
  `SD:/docs/<title>.txt` proposed).
- **A drag out**: a note dragged from the list to the File Viewer / the desktop gives its file (as a
  file drag, copy).
- **Notes' own scenario in the Widget gallery** is not needed; the `shots.sh` scenarios are a must
  (see §10).

### Later (not this round)

- **Reminders**: a date / time on a note, a notification when it comes (the Calendar already does
  reminders: perhaps "Send to Calendar" instead).
- Rich text (bold, lists), pictures in a note, a drawing note.
- Folders / tags; a trash of its own; a note's history.
- Stickies' cards each placed freely on the desktop (one window per note), resizable cards.
- Notes from BASIC (`UIKit`/`SystemKit` bindings), a `/bin/note` command (`note "buy milk"`).
- Sync with a PC (FTP / a Nextcloud-like server), encryption (the key vault, Priority 5).
- Notes in the menu bar (a quick-note pop-up from a global shortcut).

## 5. The files it reads and writes

All on the SD card; nothing kept only in memory.

### 5.1 The notes — `SD:/Notes/*.txt` (one file a note)

- **Folder**: `SD:/Notes/` (created by Notes on first save if missing; a top-level user folder like
  `SD:/Pictures`, `SD:/docs`, so the File Viewer, the Text Editor, FTP and a PC reading the card see
  the notes as ordinary text files).
- **Name**: `note-YYYYMMDD-HHMMSS.txt` — the creation time (local); a clash in the same second gets
  `-2`, `-3`… (`note-20261006-101500-2.txt`). The name never changes when the title changes (so a
  pin, a path given to Stickies, stay valid).
- **Content**: the note's text exactly, **UTF-8**, lines ended by `\n` (a `\r\n` read is accepted and
  written back as `\n`), no header, no trailing metadata. **The first line is the title.** At most
  **64 KB** a note (the editor refuses more and says so in the status line).
- **Date shown / sort order**: the file's modification time if the file system gives it, else the
  `modified` key of `notes.ini` (5.2) — the Technical Analyst picks what FAT through AppKit offers;
  `notes.ini` always holds it, so the order never depends on the file system.
- A `.txt` file **put there by hand** (copied from a PC, saved by tinypad) is a note too: it is
  listed with the default colour, not pinned.

### 5.2 The notes' properties — `SD:/Notes/notes.ini`

One section a note, by its file name (without the folder); `.ini` read and written with AppKit's
`.ini` helpers:

```ini
# Notes -- each note's colour, pin and last change (the notes are the .txt files beside this one)
[note-20261006-101500.txt]
colour = yellow          ; yellow | green | blue | pink | purple | grey
pinned = 1               ; 1 = shown on the desktop by Stickies
modified = 20261006101742 ; YYYYMMDDHHMMSS, local time: the list's order and date

[note-20261005-183012.txt]
colour = green
pinned = 0
modified = 20261005183530
```

- A note without a section: `colour = yellow`, `pinned = 0`, `modified` = its file's time (or 0).
- A section whose file is gone is dropped at the next write.
- Written by **Notes only**; Stickies only reads it (the *should* checklist tick writes the note's
  `.txt` and its `modified`, through the same code).

### 5.3 The settings

- **`SD:/apps/notes.app/config.ini`** (Notes; editable in the Control Panel's *App Settings*):
  ```ini
  [notes]
  stickies = 1        ; 1 = Stickies shown on the desktop (View ▸ Show Stickies on the Desktop)
  last = note-20261006-101500.txt   ; the note selected when Notes closed (reopened on it)
  width = 760         ; the window's size
  height = 480
  split = 240         ; the list's width
  ```
- **`SD:/apps/stickies.app/config.ini`** (Stickies; like agenda's): `x = …`, `y = …` (its place). It
  **reads** `stickies` from Notes' `config.ini` at start (quits at once if `0`).
- **`SD:/etc/autostart`**: one line **`run stickies`**, written `#setup: run stickies` with the
  agenda's, so the first-run wizard gives it back like the others (the card ships it on; the user
  turns it off from Notes, not by editing autostart).

### 5.4 Read, never written

- `SD:/etc/fileassoc.ini` (through SystemKit) — only for *Open in Text Editor* (*should*).
- Any dropped `.txt` / `.md` file (read, copied into a new note; never changed or moved).

## 6. File associations

- **Notes claims no extension.** `.txt` and `.md` stay with **tinypad** (`fileassoc.ini`): a text
  file double-clicked in the File Viewer, even in `SD:/Notes/`, opens in the Text Editor — the notes
  *are* plain text files, and stealing `.txt` would surprise everyone.
- The package's `opens` stays empty; nothing added to `SD:/etc/runners.ini`.
- A **`.txt` file dropped on Notes' dock launcher** (if the user puts one in the dock) or on its
  window is imported as a new note (feature 11) — that is the way in from other apps' files.

## 7. How it fits with the existing apps

| With | How |
|---|---|
| **The desktop / agenda** | Stickies is the agenda's sibling: same window kind (borderless, back-most, on every workspace), same `config.ini` place, same polling of a file, same *click opens the app on that item*. Agenda top left, Stickies top right — they never overlap at 1024×768 and up. Unlike the agenda (text etched on the wallpaper), Stickies' notes are paper **cards in their colour** (the sticky-note metaphor): the UX Designer keeps them flat, soft-shadowed, within the theme's look (docs/gui-redesign). |
| **The clipboard** (`clipd`) | Copy / Cut / Paste in the editor through UIKit's text editing (SystemKit's clipboard: the history of 10, the *Text copied* notification as in every app). *Copy Note* puts the whole text there. |
| **Drag & drop** | In: `.txt` / `.md` files from the File Viewer (a new note), text from another app (at the caret). Out (*should*): a note's file to the File Viewer. |
| **The Trash** | Delete = `trash_move`: a deleted note can be put back from the dock's Trash (it comes back as a `.txt` in `SD:/Notes/`, and is listed again; its colour / pin are lost if its section was dropped — acceptable). |
| **Notifications** (`notifyd`) | One only: *"Note moved to the Trash"*. Notes does not otherwise notify (no reminders this round). |
| **Text Editor (tinypad), Letters** | The notes are plain `.txt` files: tinypad opens them as they are; a note copied into Letters (clipboard) or a `.txt` from Letters' *Save as text* dropped into Notes. |
| **Calendar** | No link this round (reminders: *later*, perhaps as *Send to Calendar*). |
| **The File Viewer, FTP, a PC** | `SD:/Notes/` is an ordinary folder: browsed, backed up, edited elsewhere; Notes and Stickies pick up the changes (Stickies by its polling, Notes when its window gets the focus / its list is refreshed). |
| **The Control Panel** | *App Settings* shows `notes.app/config.ini` as any app's. No applet of its own. |
| **The dock** | Notes in the **Productivity** drawer (`category = Productivity`); Stickies hidden from the drawers (`category = Shell`, like Agenda). |
| **The packages** | One package **`notes`** holding both apps (`notes.app`, `stickies.app`), its user files `SD:/Notes/` and `config.ini`s kept on update, the `autostart` line. *Declared* in `tools/pkg/packages.ini`, **not published** (AutoDev rule). |
| **The kits** | AppKit (files, `.ini`, IPC, launching), UIKit (window, list, text area, menus, toolbar), SystemKit (clipboard, trash, notifications). No new kit; no kapi change; nothing added to a kit unless the Technical Analyst finds reusable code (e.g. a "wrap text into N lines" helper belongs in UIKit if it does not exist). |

## 8. What it does NOT do

- No rich text, no pictures, no attachments, no drawing — plain text only.
- No folders, tags, categories or notebooks; one flat folder `SD:/Notes/`.
- No reminders, alarms or dates on notes (Calendar does that).
- No sync, no network, no sharing, no encryption, no password.
- No undo history across sessions; no versions of a note (only the Trash for a deleted one).
- It does not become the `.txt` editor of the system (tinypad stays).
- Stickies does not let you **edit** a note on the desktop (only open it in Notes; ticking a box is a
  *should*); it does not place each note separately nor resize them.
- No new kapi, no kernel change, no change to another app's behaviour (only `autostart` gets a line,
  docs/04 gets the catalog entries).
- Not single-user aware beyond today's Onyx: one `SD:/Notes/` for the machine (the multi-user plan
  is set aside).

## 9. Acceptance criteria

Each one testable; **[SIM]** = checkable in the PC desktop simulator (`tools/tests/desktop_sim/`,
sample notes from an overlay such as `tools/tests/desktop_sim/sd/Notes/`, what is saved read back
from `SIM_WRITES`), **[UNIT]** = a PC unit test of the notes model (no UI), **[PI]** = on the Pi.

**The Notes window**

1. **[SIM]** With three sample notes (`SD:/Notes/*.txt` + `notes.ini`) of different `modified`
   values, Notes lists them **newest first**, each row showing the note's first line, its date and
   its colour's dot; a pinned note's row shows the pin mark.
2. **[SIM]** At start, Notes selects the note named by `last` in its `config.ini` (else the newest)
   and shows its whole text in the editor.
3. **[SIM]** With **no** `SD:/Notes/` folder, Notes starts on one empty new note, the caret in the
   editor, and writes **nothing** to the card while the note stays empty.
4. **[SIM]** Typing `Shopping\nmilk\neggs` in a new note then waiting ~1 s (script `wait`s) writes
   `SD:/Notes/note-YYYYMMDD-HHMMSS.txt` containing exactly those bytes (UTF-8, `\n`), and a
   `notes.ini` section for it with `colour = yellow`, `pinned = 0`, `modified = …`.
5. **[SIM]** As the first line is typed, the list's row shows it as the title (*New Note* while
   empty).
6. **[SIM]** **Ctrl+N** adds an empty note at the top of the list, selected; selecting another note
   while it is still empty removes it (no file written, no row left).
7. **[SIM]** Changing the title of a note does **not** rename its file.
8. **[SIM]** *File ▸ Delete Note* moves the note's file to the Trash (the fake kapi's trash folder /
   `SIM_WRITES`), removes its row, selects the next note, and sends the notification *"Note moved to
   the Trash"*.
9. **[SIM]** *Note ▸ Colour ▸ Green* sets `colour = green` in `notes.ini` and the row's dot turns
   green.
10. **[SIM]** **Ctrl+P** on a note sets `pinned = 1` (the row shows the pin; the menu item a check
    mark); Ctrl+P again sets `pinned = 0`.
11. **[SIM]** *View ▸ Show Stickies on the Desktop* off writes `stickies = 0` in
    `SD:/apps/notes.app/config.ini` (and sends Stickies its quit message when the `stickies` service
    exists); on writes `stickies = 1` and launches `stickies` when the service does not exist.
12. **[SIM]** *Edit ▸ Copy Note* puts the note's whole text on the clipboard (`clip_get_text` returns
    it); **Ctrl+V** in the editor inserts the clipboard's text at the caret.
13. **[SIM]** Dropping a `.txt` file (a `GUI_EVENT_DROP` of a file path) on the list creates a new
    note whose text is the file's, selected; the dropped file is unchanged.
14. **[SIM]** `notes SD:/Notes/note-20261005-183012.txt` (`SIM_ARGS`) opens with that note
    selected.
15. **[SIM]** Closing the window right after typing (no pause) still writes the note (the text in
    `SIM_WRITES` equals what was typed).
16. **[UNIT]** The notes model reads a `\r\n` file and writes it back with `\n`; a file of more than
    64 KB is refused with an error, never truncated silently.
17. **[UNIT]** A `.txt` in `SD:/Notes/` without a `notes.ini` section is listed (yellow, not pinned);
    a `notes.ini` section without a file is dropped at the next write.
18. **[UNIT]** Two notes created in the same second get distinct files (`…-HHMMSS.txt`,
    `…-HHMMSS-2.txt`).

**The Stickies widget**

19. **[SIM]** With two pinned notes (yellow, green) and one not pinned, Stickies draws **two cards**
    in their colours at the top right of the screen — title in bold, then the first lines,
    word-wrapped, `…` when cut — and nothing for the third note. (`shots.sh stickies`.)
20. **[SIM]** With more than 6 pinned notes, Stickies shows the 6 most recently changed.
21. **[SIM]** With no pinned note, Stickies shows the one-line hint; a click on it launches `notes`.
22. **[SIM]** When `stickies = 0` in `SD:/apps/notes.app/config.ini`, Stickies exits at once without
    a window.
23. **[SIM]** A click on a card launches `notes` with that note's path as its argument.
24. **[SIM]** Dragging the header moves the widget and writes its `x` / `y` to
    `SD:/apps/stickies.app/config.ini`; at the next start it opens there.
25. **[SIM]** Stickies registers the `stickies` IPC service; its quit message makes it exit; its
    re-read message makes it redraw from the files at once.
26. **[SIM]** A note's text changed on the card (a new file in the overlay / a changed `notes.ini`
    between two polls) is reflected by Stickies at its next poll without being restarted.
27. **[SIM]** The widget's window is borderless and back-most (an app window placed over it covers
    it) — checked in a `desktop`-style composed scene: wallpaper, agenda top left, Stickies top
    right, an app window in front.

**Integration and docs**

28. **[SIM]** `sh tools/tests/desktop_sim/shots.sh notes` and `shots.sh stickies` run without error
    and produce `screenshots/notes.png` and `screenshots/stickies.png`; the existing `agenda` and
    `desktop` scenarios still pass.
29. `make` from `kernel/` builds `notes` and `stickies` without warnings added; `make stage` puts
    `sdcard/apps/notes.app/{main,app.txt,icon.bmp}` and `sdcard/apps/stickies.app/{main,app.txt,icon.bmp}`
    on the card; `sdcard/etc/autostart` has the `#setup: run stickies` line next to agenda's.
30. `docs/04-USER-GUIDE.md` has a *Notes* section (§12 catalog row for `notes` and `stickies`, the
    controls, the files `SD:/Notes/*.txt`, `notes.ini`, the two `config.ini`) and §5 a *Stickies*
    paragraph beside *The agenda widget*; `python docs/build_docs.py` regenerates the exports.
31. `tools/pkg/packages.ini` declares the `notes` package (both apps, `SD:/Notes/` and the
    `config.ini`s as user files, the autostart line) — **not published**.
32. Every new source file carries the MIT notice; no kapi / kernel / kit ABI change (or, if the
    Technical Analyst adds a helper to a kit, its `.abi` appended and the kit's docs regenerated).
33. **[PI]** (for the user, by hand) On the Pi: a note pinned in Notes appears on the desktop within
    a few seconds; after a reboot the pinned notes and the show / hide setting are as they were.
