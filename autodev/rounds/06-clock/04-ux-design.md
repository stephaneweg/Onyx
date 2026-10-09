# AutoDev round 6 — UX Designer: Clock

Date: 2026-10-07. Inputs: `01-product-manager.md`, `02-product-analysis.md` (features "02 #n", files "02 §5",
acceptance "AC-n"), `03-technical-analysis.md` (plan "step n", facts "fact n", risks "R-n"). Read for consistency:
`docs/11-UIKIT.md` (`SegmentedControl`, `ToolButton` + `WKT_*`, `Button`, `NumericUpDown`, `Textbox`, `LcdDisplay`,
`DataGrid` + `cellDraw`, `Menu`, `VPath` / `arc`, `uk_switch_mark`, `uk_sunken`, `uk_rbox`, `uk_title_strip`,
`uk_glyph`, `UkFaceScope`), `docs/gui-redesign/README.md` (the modernised CDE: raised faces, rounded corners, no
shadows, every shade from the theme), the Task Manager (`user/Apps/taskman/main.cpp`: the `SegmentedControl` tab bar,
the footer of buttons), Notes (`user/Apps/notes/`: the owner-drawn list `notelist.h`, the status line, the tinted
selected row), the menu bar's calendar pop-up, and round 5's `04-ux-design.md` (the mock-up pipeline). Also read, for
the facts below: `user/Kits/uikit/{menu.cpp,root.cpp,widget.cpp,controls.cpp,toolbar.cpp,datagrid.h}`,
`user/Kits/systemkit/locale.inc` (the 23 cities), `sdcard/apps/langconf.app/lang/fr.txt` (their French names),
`tools/tests/desktop_sim/shots.sh` (its existing `clock` block).

**Every picture below is rendered by UIKit itself on the PC** (the desktop simulator: host `g++`, `fakekapi.cpp`,
FreeType's DejaVu Sans, the card's theme — as `shots.sh`). The app does not exist yet, so the pictures come from a
**throwaway program**, `mockups/clkmock.cpp`: UIKit's real widgets for everything UIKit has, and the few drawn widgets
this design adds (`HereCard`, `CityList`, `NextAlarmBar`, `AlarmList`, `TimerRing`, `StopwatchFace`, `Veil`). Nothing
runs in it: each scene (`MOCK_SCENE`) is set by hand at the simulator's fixed time, **Monday 2026-09-28 12:34:00,
Brussels (UTC+2, summer time)**, with the fixtures of 03 §8.3 (Tokyo, New York, London; 07:00 *School* weekdays,
09:00 *Gym* weekends off, 14:30 *Medicine* once). To redo the pictures (≈ 40 s the first time, ≈ 10 s after):

```sh
sh autodev/rounds/06-clock/mockups/mockups.sh     # -> autodev/rounds/06-clock/mockups/clk-*.png (EN, then *-fr.png)
```

**Where the throwaway lives**: only in `autodev/rounds/06-clock/mockups/` (`clkmock.cpp`, `mockups.sh`, `sd/` = the
overlay with the **mock French catalogue** `sd/apps/clock.app/lang/fr.txt`, a draft of the real one). Nothing of it
is in `user/`, `tools/` or `sdcard/`; no source of the repository is changed by this role. What the Developer may
lift (a starting point, not finished code): the drawings `bell`, `slash`, `globe`, `sun`, `moon`, `hourglass`,
`warn`, `speaker_off`, `pin`; the widgets `HereCard`, `CityList`, `NextAlarmBar`, `AlarmList`, `TimerRing`,
`StopwatchFace`, `FootText`, `Veil`; the `lap_draw` cell drawer; the layout numbers of `world ()`, `alarms ()`,
`timer ()`, `stopwatch ()` and the overlays in `main ()`.

---

## 0. The decisions

| # | Question | Decision |
|---|---|---|
| D1 | The window | **One window, `Root (560, 440, TR ("Clock"))`** (client area; *Clock* / ***Horloge***), **resizable** (`setResizable (true)`), **`setMinSize (560, 440)`** — the French pictures are checked at that size, so it only grows; `width` / `height` kept in `config.ini` (02 §5.2). Placed by the system. The four tabs are **four sets of widgets in the one window, shown / hidden** (`hidden`, as the Task Manager's views). |
| D2 | The tab bar | A **`SegmentedControl`** (440 × 28, four equal segments, centred at the top, y = 10): ***World · Alarms · Timer · Stopwatch*** / ***Monde · Alarmes · Minuteur · Chronomètre***; the chosen one filled with the accent. Under it a 1-px line `uk_tone (bg, 100)` across the window at y = 47 (the Task Manager's). Re-centred in `onResized`. Left / Right move it when it has the focus; **Ctrl+1…4**, **Ctrl+Tab** anywhere (§7). |
| D3 | The footer | Every tab ends with a **46-px footer** (Task Manager's): at the left a **`FootText`** (the tab's count, dim `uk_mix (C_BG, C_TEXT, 170)`; then, after an action, a green check and its result: *Laps copied*), at the right the tab's **`ToolButton`s** (`raised`, 30 px high, glyph + text, `fitWidth ()` + 8 px), anchored right / bottom. No tool bar at the top: the tab bar is the top. |
| D4 | World: "here" | **`HereCard : Widget`** (full width, 122 px): **`12:34:00`** in DejaVu Sans **Bold 54 px**, centred; the date **`Monday 28 September 2026`** / ***lundi 28 septembre 2026*** (16 px); a line of a **map pin** (accent) + ***Brussels · UTC+2 · Summer time*** (dim 13 px; *Summer time* only when on). Seconds tick every second (only this widget redrawn). `clk-world.png`. |
| D5 | World: the cities | **`CityList : Widget`** (owner-drawn, Notes' `NoteList` pattern — `ListBox` draws text only): a sunken `C_FIELD` field (`uk_sunken`, radius 6, the accent outline when focused), **rows of 50 px**: a **day / night mark** (a sun `#F0A020` from 07:00 to 18:59 there, else a moon `#5A6A9A`), the **city** (Bold 15 px, translated: *Londres*), under it **the difference and the zone**, dim: ***+7 h · UTC+9***, ***−6 h · UTC−4***, ***Same time*** (a true minus sign), prefixed by the **day word in accent bold** when not today: ***Tomorrow*** / ***Yesterday*** (`clk-world-late.png`); at the right the city's **`19:34`** (Bold 26 px). The selected row a tinted rounded box (`uk_mix (C_FIELD, C_ACCENT, 90)`, 60 without focus — Notes'); faint separators. Up / Down / Home / End select; the wheel and `uk_scroll_bar` when more than fit. Footer: ***3 cities · up to 12*** and **`+ Add City`** (`WKT_PLUS`), **`− Remove`** (`WKT_MINUS`), then two 30 × 30 icon `ToolButton`s **▲ / ▼** (`setIcon`: `WKG_CHEV_UP / DOWN`) — Move up / down, disabled at the ends; Remove and the arrows disabled with no row. |
| D6 | World: empty (02 #8) | In the field: a **globe** (drawn, `uk_mix (C_FIELD, C_FIELD_TEXT, 70)`), ***No cities yet*** (Bold 15 px), *Add the cities you want to follow with + Add City (Ctrl+N).*; the footer says *No cities*. `clk-world-empty.png`. |
| D7 | World: no zone set (02 #5, AC-10) | The pin line is replaced by an amber **warning triangle** + ***Time zone not set*** (Bold, `#B06A00`) and a **`Button` *Language & Region…*** beside it (runs `control langconf`). The clock is then on UTC (`10:34:00` in the simulator); the cities show **their `UTC±h` only** (no difference: "here" has no zone). `clk-world-nozone.png`, `-fr`. |
| D8 | World: add a city (02 #7) | An **overlay card** (D16) ***Add a City*** (380 × 364): a search glyph (`WKT_SEARCH`) + a **`Textbox`** (the filter: typing filters on the city's shown name, case and accents ignored), a **`DataGrid`** (3 columns ***City · Time · UTC***, `stripes`, the first row selected, sorted by the shown name — *Athènes* among the A's in French) of the zones **not yet chosen**, the hint *Type to filter · Enter: add · Esc: cancel*, **`Button`s *Cancel* · *Add*** (Add the default). Enter / a double-click adds at the end of the list and closes; at 12 cities `+ Add City` is disabled and Ctrl+N blips (the footer says *12 cities · up to 12*). `clk-cities.png`, `-fr`. |
| D9 | Alarms: the next one (02 #10) | **`NextAlarmBar : Widget`** (full width, 40 px): a rounded box tinted `uk_mix (C_BG, C_ACCENT, 46)` (outline 120), a **bell** in the accent, ***Next alarm: today 14:30*** (Bold 14 px) ***— in 1 h 56 min***. Snoozed: ***Snoozed until 12:40 — Medicine*** (`clk-alarms-states.png`). None on: a neutral box, a **struck bell**, ***No alarm set*** dim (`clk-alarms-empty.png`). A click on it selects that alarm in the list. |
| D10 | Alarms: the list (02 #9) | **`AlarmList : Widget`** (owner-drawn, the same field as D5), **rows of 60 px**: the **time** (Bold 30 px; greyed `uk_mix (C_FIELD, C_FIELD_TEXT, 105)` when off), the **label** (Bold 14 px; *Alarm* when empty), under it the **repeat** dim (*Weekdays*, *Weekends*, *Every day*, *Once*, *Mon, Wed, Fri*) and, after a dot, the **state** in bold: ***Snoozed until 12:40*** (accent), ***Missed at 07:00*** (red `#C0302A`, R-2's replacement); at the right the **on / off switch** drawn by **`uk_switch_mark`** (46 × 24 — `ToggleSwitch`'s look; a widget per row would not scroll with the list). An **invalid** block (AC-16): `--:--` greyed, ***Invalid***, *Cannot be read: correct alarms.txt or delete it* in red, no switch (it can only be deleted). Sorted by time. Footer: ***3 alarms · 2 on*** and **`+ New Alarm`**, **`Edit`**, **`Delete`** (`WKT_TRASH`; Edit / Delete disabled with no row). Click: select; a click on the switch: toggle (and select); double-click / Enter: the editor; Space: toggle the selected one; Delete: delete it (no question, 02 #13). |
| D11 | Alarms: empty | In the field: a big **bell** (faint), ***No alarms***, *Create one with + New Alarm (Ctrl+N).* and *Alarms ring even when the Clock is closed.* (the one thing to know). `clk-alarms-empty.png`, `-fr`. |
| D12 | The alarm editor (02 #11) | An **overlay card** ***New Alarm*** / ***Edit Alarm*** (460 × 352), a form with **bold captions** at the left (88 px column): **Time** — an **`LcdDisplay`** (128 × 54, face 34 px, centred) showing **`07:00`** (zero-padded: `NumericUpDown` shows `7` / `0`), beside it two **`NumericUpDown`s** *Hours* (0–23) and *Minutes* (0–59, step 1; Page Up / Down ±10), 72 × 30, captions above; under them, dim, ***Next: tomorrow 07:00*** (what the choices mean, refreshed as they change). **Label** — a **`Textbox`** (the card's width, `maxLen` 160 bytes ≈ 40 characters). **Repeat** — **seven `ToolButton` toggles** (44 × 30, `raised`, *Mon … Sun* / *lun. … dim.*, Monday first in both languages, lit = the accent's tint) and under them two small text `ToolButton`s ***Every day*** · ***Weekdays*** (set the seven); none lit = once. **Sound** — a **`SegmentedControl`** ***Chimes · Beeps · Marimba*** (240 × 30) and a **`ToolButton` ▶ *Test*** (`WKT_PLAY`; plays one round; *Sound unavailable* in amber in the *Next:* line's place if it cannot). A 1-px line, then **`Button`s**: ***Delete*** at the left (editing only), ***Cancel*** · ***OK*** at the right. The focus starts on *Hours*; Tab walks the fields; **Enter = OK, Esc = Cancel** (the `Textbox` keeps Enter for itself: OK still by Enter elsewhere, and by its button). The two spin boxes are **`Spin`s**, not bare `NumericUpDown`s: UIKit's `NumericUpDown::onKey` takes Enter (commit) and Esc (drop the typed digits) and returns true, and the focused child gets the key before the veil's `onKey` (`Widget::handleKey`), so a bare one would eat both. `Spin : NumericUpDown` (in `ui.h`) passes Enter on **after** committing the typed digits (`Hours` `7` typed, Enter → 07:00 saved), Esc on after dropping them, and every key with Ctrl held (§7). 21st alarm: `+ New Alarm` disabled, Ctrl+N blips. `clk-edit.png`, `-fr`. |
| D13 | The ringing overlay (02 #17–20) | Over the **Alarms tab** (the window raised: `kapi_raise_app`), a card ***Alarm*** / ***Alarme*** (380 × 280): a **bell with sound waves** (accent, 38 px), the time **`07:00`** (Bold 50 px), the **label** (Bold 18 px), the repeat and the sound dim (*Weekdays · Chimes*), **`Button`s *Snooze 10 min*** (focused: Enter) and ***Stop***, the hint *Enter: snooze · Esc: stop*. No other way out (it is modal: the menus' shortcuts are off). Unanswered 2 minutes: the card goes, *Missed at* on the row (R-2). `clk-ring.png`, `-fr`. |
| D14 | The ring with no sound (02 #25, R-3, R-4) | The same card 28 px taller with, under the repeat line, an amber **struck speaker** and ***Sound unavailable: the sound output is busy*** (*Son indisponible : la sortie audio est occupée*) when another program holds the output (`ak_out_open (0, 0)` = 0), or the plain ***Sound unavailable*** (*Son indisponible*) when there is no sound at all (−1, 03 R-3; what the simulator's `clock-nosound` shows). The ring is otherwise the same. `clk-ring-nosound.png`, `-fr`. |
| D15 | Ring when the Clock was not open | `clock --ring <id>` opens **the full window on the Alarms tab** with the ring card (03 §3.6's one code path: no separate small window). **Started only to ring, it closes by itself after Stop / Snooze / the 2-minute timeout** (a flag set at start; the user could do nothing else meanwhile: the card is modal). Started by the user, it stays. |
| D16 | Overlays, not `Modal::run` (fact 4) | Every dialog is a **`Veil : Widget`**: the client area's size, the **last child of the root**, **`modal = true`** (UIKit then routes every key and click to it, Tab walks its controls, and `Menu::shortcut` runs none — so Ctrl+C in its `Textbox` is the field's, and Ctrl+Q does nothing while a card shows), **its controls are its children**. Its `onDraw`: **what is under it, dimmed** (the root's canvas — its earlier siblings are already composed there when it draws — mixed 96/256 toward black), then the card: `uk_rbox` radius 9 `C_BG`, `uk_title_strip`, the frame's outline (round 5's cards). The background is **frozen** while the card shows (taken when the veil is drawn; again after a resize); `onTick` goes on (the timer still rings, the ring's sound still steps). Two veils may stack (*Time's up* over the editor: R-8); closing one removes it (`removeChild`) and gives the focus back. |
| D17 | Timer (02 #27–30) | Left: **`TimerRing : Widget`** (262 × 262): a **track** (`VPath::arc` 0–360°, 14 px, `uk_tone (C_BG, 108)`) and the **time left** as an accent arc from 12 o'clock **shrinking counter-clockwise** (`arc (…, 90 − 360·left/total, 90, …)`, round ends) with a small white dot at its moving end; inside: *of 05:00* dim, **`03:12`** (Bold 46 px; `H:MM:SS` from an hour), and ***Ends at 12:37*** with a small bell (the wall time it will ring) — paused: the arc mixed toward the face and ***Paused*** in the accent instead. Right column (252 px): **Duration** — three **`NumericUpDown`s** (hours 0–23, minutes 0–59, seconds 0–59; *hours · minutes · seconds* under them), **disabled while it runs or is paused**; **Presets** — five **`ToolButton` toggles** *1 min · 3 min · 5 min · 10 min · 15 min* (lit when the duration equals one; a click sets it, a double-click sets and starts); then the **start button**, full width, 42 px, `filled` with the accent: ***▶ Start*** / ***❚❚ Pause*** / ***▶ Resume***; under it ***⏮ Reset*** (36 px, `WKT_TO_START`, disabled while running — 02 #4 "R when stopped"). Footer: the keys *Space: start / pause · R: reset · it rings even with the Clock closed*. The two big buttons are stacked (not side by side) so that ***Remettre à zéro*** fits. `clk-timer.png`, `clk-timer-paused.png`, `-fr`. |
| D18 | Time's up (02 #29) | A card ***Timer*** / ***Minuteur*** (380 × 236) over the Timer tab (the ring empty): an **hourglass** (accent), ***Time's up*** (Bold 28 px), *The 05:00 timer ended at 12:39*, **`Button`s *+1 min*** and ***Stop*** (focused), hint *Enter or Esc: stop · +: one more minute*. The window is raised; the sound loops; 2 minutes unanswered: as D13. `clk-timesup.png`, `-fr`. |
| D19 | Stopwatch (02 #31–33) | **`StopwatchFace : Widget`** (104 px): **`00:41`** Bold 60 px and **`.07`** Bold 36 px (a shade lighter), on one baseline, centred — the hundredths smaller so the digits that change fastest do not dominate; under it ***Lap 4 · 00:04.45*** (the lap running) dim. Two **`ToolButton`s** 164 × 38, centred: the left ***Lap*** while running / ***⏮ Reset*** when stopped (disabled at zero), the right `filled` ***▶ Start*** / ***■ Stop*** (`WKT_STOP`). Then a **`DataGrid`** (`stripes`, header ***Lap · Lap time · Total*** + a fourth, untitled column), **newest first**, with a **`cellDraw`**: from 3 laps the fastest lap time in **green bold** and a green pill ***▲ Fastest***, the slowest in **red bold** and ***▼ Slowest*** (colour **and** a word and an arrow: 02 #32). Empty: *No laps yet: Lap (L) while it runs* (`emptyText`); at zero the face's second line says the keys. Footer: ***3 laps*** and **`Copy Laps`** (`WKT_COPY`, disabled with no lap); after a copy the footer adds a green check ***Laps copied*** (until the next action). `clk-stopwatch.png`, `clk-stopwatch-zero.png`, `clk-stopwatch-copied.png`, `-fr`. |
| D20 | Closing (R-1, amended) | **No "Quit anyway?" at all.** UIKit's `Menu::shortcut` turns **Ctrl+Q into `MENU_QUIT` before any key reaches the app** (menu.cpp: `if (key == UK_CTRL ('Q')) kapi_menu_command (MENU_QUIT)` — the window's sticky exit, the same as the close box), so the Clock cannot ask on Ctrl+Q either without a UIKit change — and it does not need to: **every way out hands over** (R-1 (b)): a running timer becomes `[timer]` in `alarms.txt` (clockd rings it; the *Time's up* card then comes with `clock --ring timer`), a running or stopped stopwatch is saved (`sw_*` in `config.ini`) and shown right at the next start within the boot. Nothing is lost, so nothing is asked. |
| D21 | Language | Every word in `TR` (§7); the cities through `TR` with `// TR:` marks (their French: Language & Region's); **the French pictures checked: every word fits** at 560 × 440 (§2.1). |

---

## 1. The pictures (all in `mockups/`, 560 × 440 client + the window's frame)

| Picture | What it shows |
|---|---|
| `clk-world.png`, `clk-world-fr.png` | **World**: here big (12:34:00, the date, Brussels · UTC+2 · Summer time), Tokyo (selected) / New York / London with their differences, day / night marks; FR: *lundi 28 septembre 2026*, *Bruxelles*, *Heure d'été*, *Londres* |
| `clk-world-late.png` | The same at **23:30**: Tokyo ***Tomorrow*** 06:30, a fourth city (Los Angeles) |
| `clk-world-empty.png` | **No cities**: the globe and the hint; Remove and the arrows disabled |
| `clk-world-nozone.png`, `-fr` | **No zone set**: the clock on UTC (10:34:00), *Time zone not set* + *Language & Region…*, the cities with their UTC offset only |
| `clk-cities.png`, `-fr` | **Add a City**: the filter, the zones not chosen with their time and UTC offset, Cancel / Add |
| `clk-alarms.png`, `-fr` | **Alarms**: *Next alarm: today 14:30 — in 1 h 56 min* (AC-12), 07:00 *School* weekdays on, 09:00 *Gym* weekends off (greyed), 14:30 *Medicine* once on |
| `clk-alarms-empty.png`, `-fr` | **No alarms**: the struck bell, *No alarm set*, the empty field's hint |
| `clk-alarms-states.png` | The **states**: *Missed at 07:00* (red), *Snoozed until 12:40* (accent, and in the bar), an **invalid** block, a *Mon, Wed, Fri* alarm with no label (*Alarm*) |
| `clk-edit.png`, `-fr` | **The alarm editor** (editing *School*): the LCD time, Hours / Minutes, *Next: tomorrow 07:00*, the label, Mon–Fri lit, Every day / Weekdays, Chimes · Beeps · Marimba + Test, Delete / Cancel / OK |
| `clk-ring.png`, `-fr` | **The ring** over the Alarms tab: the bell with waves, 07:00, *School*, Snooze 10 min (focused) / Stop |
| `clk-ring-nosound.png`, `-fr` | **The ring without sound**: *Sound unavailable: the sound output is busy* |
| `clk-timer.png`, `-fr` | **Timer running**: 03:12 of 05:00, the ring at 64 %, *Ends at 12:37*, the duration locked, *5 min* lit, Pause, Reset disabled |
| `clk-timer-paused.png` | **Timer paused**: the dimmed arc, *Paused*, Resume, Reset enabled |
| `clk-timesup.png`, `-fr` | **Time's up** over the Timer tab: +1 min / Stop (focused) |
| `clk-stopwatch.png`, `-fr` | **Stopwatch running** at 00:41.07, lap 4 running, three laps with *Slowest* / *Fastest*, Lap / Stop |
| `clk-stopwatch-zero.png` | **At zero**: 00:00.00, the keys, Reset disabled, Start, the empty grid's text, Copy Laps disabled |
| `clk-stopwatch-copied.png` | **Stopped, laps copied**: Reset / Start, *✓ Laps copied* in the footer |

### 1.1 Not drawn (described)

- The **menus** are the system menu bar's (the `menubar` app draws them): §6.
- The **notifications** are notifyd's bubbles: §5.
- The **app icon** (`tools/icons/clock_icon.py`, step 5): a round clock face, a light dial with an accent rim and
  twelve ticks, the hands at 10:10, a small bell at its top right — drawn by code as the other icons.

---

## 2. The window, tab by tab (client coordinates, at 560 × 440)

```
y 0  ┌──────────────────────────────────────────────────────────────────┐
     │            [ World | Alarms | Timer | Stopwatch ]  SegmentedControl 440x28 @ (60,10)
y 47 ├──────────────────────────────────────────────────────────────────┤  1-px line uk_tone (bg, 100)
     │                     the tab's content  (y 48 .. 394)             │
y 394├──────────────────────────────────────────────────────────────────┤
     │ FootText (14, 402, ..)                    [ToolButton]…[ToolButton] (right-aligned, 30 px, top 402)
y 440└──────────────────────────────────────────────────────────────────┘
```

| Tab | Widgets (x, y, w, h at the default size) | Anchors on resize |
|---|---|---|
| World | `HereCard` (12, 54, 536, 122); `CityList` (12, 182, 536, 212); footer: `FootText`, `ToolButton`s *Add City*, *Remove*, ▲, ▼; no zone: a warning widget + `Button` *Language & Region…* centred at y 146 | HereCard left+right; CityList fill; footer left/bottom and right/bottom |
| Alarms | `NextAlarmBar` (12, 56, 536, 40); `AlarmList` (12, 104, 536, 290); footer: `FootText`, *New Alarm*, *Edit*, *Delete* | bar left+right; list fill; footer |
| Timer | `TimerRing` (14, 56, 262, 262); captions + 3 `NumericUpDown` (296 + i·(77+10), 90, 77, 30); 5 preset `ToolButton`s (296 + i·51, 178, 46, 30); start `ToolButton` (296, 234, 252, 42); Reset `ToolButton` (296, 286, 252, 36); footer `FootText` (keys) | the whole block (ring + column, 534 wide) centred in the content area by `onResized` |
| Stopwatch | `StopwatchFace` (0, 52, 560, 104); `ToolButton`s Lap/Reset (109, 160, 164, 38) and Start/Stop (287, 160, 164, 38); `DataGrid` (12, 210, 536, 184): columns 70 / 130 right / 130 right / the rest; footer: `FootText`, *Copy Laps* | face left+right (centred text); buttons re-centred; grid fill (its last column takes the width) |
| Overlays | `Veil` (0, 0, W, H), its card centred; its controls its children (placed from the card's corner) | veil = the client area; card re-centred; snapshot taken again |

### 2.1 The French pictures (AC-3: "no label cut")

Checked on `clk-*-fr.png`: the tab bar (*Chronomètre* fits its 110-px segment), *Ajouter une ville* / *Retirer*,
*Nouvelle alarme* / *Modifier* / *Supprimer*, the editor's *Heures / Minutes*, *lun. … dim.*, *Tous les jours* /
*En semaine*, *Prochaine : demain 07:00* (moved under the spin boxes after a first try overlapped *En semaine*),
*Carillon · Bips · Marimba* + *Écouter*, *Rappel dans 10 min* / *Arrêter*, *Temps écoulé* and its sentence,
*Préréglages* and the five presets (tight at 46 px, but whole), ***Remettre à zéro*** (did **not** fit two buttons side
by side: the buttons are stacked, D17; and 164 px wide on the Stopwatch), *Temps du tour*, *Plus rapide* / *Plus lent*,
*Copier les tours*, the Timer footer (the longest line, whole). **Everything fits.**

---

## 3. States

| State | What shows |
|---|---|
| World, empty | D6 |
| World, no zone | D7 |
| World, 12 cities | *12 cities · up to 12*; *Add City* disabled; Ctrl+N blips |
| Alarms, empty / all off | D11 / the bar's *No alarm set* |
| Alarm snoozed / missed / invalid | D10 (`clk-alarms-states.png`) |
| 20 alarms | *New Alarm* disabled; Ctrl+N blips; footer *20 alarms · n on* |
| Ringing / no sound | D13 / D14 |
| Timer idle | the ring full (the track and a full accent arc), the duration set, *of* line hidden, *Start*, Reset disabled when at the set time |
| Timer running / paused / at zero | D17 / D17 / D18 |
| Stopwatch zero / running / stopped | D19 (`-zero`, `clk-stopwatch.png`, `-copied`) |
| `alarms.txt` cannot be written (card full / read-only) | the change is kept in the window, and the footer shows in red `WKG_CLOSE` + *Not saved: the card is full or read-only* (Notes' wording); tried again at the next change |
| Sound unavailable at *Test* | the editor's *Next:* line becomes the amber *Sound unavailable* (D12) |
| clockd not running | nothing shown: the Clock starts it (AC-27), quietly |

---

## 4. Colours (all from the theme, except the meanings)

| Use | Colour |
|---|---|
| faces, footers, cards | `C_BG`; captions `C_TEXT`; dim text `uk_mix (C_BG, C_TEXT, 170)` / `uk_mix (bg, uk_ink_for (bg), 150)` |
| lists, grid, text fields | `C_FIELD`, `C_FIELD_TEXT`, dim `uk_mix (C_FIELD, C_FIELD_TEXT, 140)`, separators 30 |
| selection, chosen tab, start buttons, the arc, bells, day word, *Snoozed* | `C_ACCENT` (rows tinted `uk_mix (C_FIELD, C_ACCENT, 90)`; the next-alarm bar `uk_mix (C_BG, C_ACCENT, 46)`) |
| ring track | `uk_tone (C_BG, 108)`; paused arc `uk_mix (C_BG, C_ACCENT, 140)` |
| veil | what is under, mixed 96/256 toward black; card title `uk_title_strip` (the active frame's colour) |
| meanings | fastest / copied **green `#2E9A44`**; slowest / missed / invalid / not saved **red `#C0302A`**; no zone / no sound **amber `#B06A00`**, warning triangle `#E0A020`; sun `#F0A020`, moon `#5A6A9A` |
| dark themes (Dark Coffee: `uk_bright (C_FIELD) < 128`) | the meanings' lighter twins: green `#5BD27A`, red `#FF6B5E`, amber `#F0B040` |

Faces (FontKit `FtTextFace`, "DejaVu Sans", opened **lazily** — only the shown tab's, then kept): 13 (the UI,
`ft_uikit_install`), 14, 15, 16, 18, 26, 28, 30, 34, 36, 46, 50, 54, 60 (bold through `style 2`). DejaVu's digits all
have the same advance: the ticking times do not wobble.

---

## 5. Notifications (notifyd; sent by the Clock, 03 §3.5)

| When | Title | Text | Action |
|---|---|---|---|
| an alarm rings | *Clock* / *Horloge* | `07:00 School` (the label as typed) | `clock alarms` |
| unanswered 2 min | *Clock* | *Missed alarm: 07:00 School* / *Alarme manquée : 07:00 École* | `clock alarms` |
| the timer ends | *Clock* | *Timer — 05:00 done* / *Minuteur — 05:00 terminé* | `clock timer` |
| clockd's fallback (the Clock cannot start) | `07:00` | the label | `clock alarms` |

---

## 6. Menus (the system menu bar; `Menu`, published at start)

| Menu | Items (shortcut text) |
|---|---|
| *Clock* (the bar's own) | Quit (^Q) — D20 |
| **View** / *Affichage* | World (^1) · Alarms (^2) · Timer (^3) · Stopwatch (^4) · — · Next Tab (Ctrl+Tab) |
| **Alarm** / *Alarme* | New Alarm… (^N) · Edit Alarm… (Enter) · Turn On / Off (Space) · Delete Alarm (Del) — chosen from another tab, they show the Alarms tab first |
| **City** / *Ville* | Add City… (^N) · Remove City (Del) · Move up (Ctrl+↑) · Move down (Ctrl+↓) |
| **Timer** / *Minuteur* | Start / Pause (Space) · Reset (R) · — · 1 minute · 3 minutes · 5 minutes · 10 minutes · 15 minutes |
| **Stopwatch** / *Chronomètre* | Start / Stop (Space) · Lap (L) · Reset (R) · — · Copy Laps (^C) |

Only **Copy Laps** is bound in the menu (`UK_CTRL ('C')`; it does nothing off the Stopwatch tab). Every other item
is `item (label, "<text>", 0, cb)` — **key 0** — and its key is handled in the root's `onKey`, because:
`UK_CTRL ('1')` is `0x11` = **`UK_CTRL ('Q')`** (`'1' & 0x1F`: Ctrl+1…4 would be Quit, ^R, ^S, ^T — the Spreadsheet
and the PDF Viewer do `menu.item ("…", "^1", 0, …)` + `if (ctrl && k == '1')` for the same reason); Ctrl+N means two
things by tab; and Space / L / R / Delete bound in a menu would be taken from the fields.

## 7. Keyboard

| Key | Where | Does |
|---|---|---|
| Ctrl+1…4, Ctrl+Tab (Shift: back) | anywhere but a card | the tab |
| Ctrl+N | World / Alarms | Add City… / New Alarm… |
| Up / Down, Home / End, Page Up / Down | World, Alarms (the list has the focus) | select |
| Ctrl+Up / Ctrl+Down | World | move the city |
| Delete | World / Alarms | remove the city / delete the alarm |
| Enter, double-click | Alarms | edit |
| Space | Alarms / Timer / Stopwatch | toggle the alarm / start–pause / start–stop |
| R | Timer / Stopwatch, not running | reset |
| L | Stopwatch, running | lap |
| Ctrl+C | Stopwatch | copy the laps |
| Enter / Esc | a card | its default (OK, Add, Snooze, Stop) / Cancel, Stop |
| + | Time's up | one more minute |
| Enter / Esc over the editor's *Hours* / *Minutes* | the editor | the typed digits committed, then OK / dropped, then Cancel (`Spin`, below) |
| Tab | a card | its next control |

The letter keys act only when no `Textbox` / spin box has the focus (they take digits; a letter they refuse
comes back to the root). Ctrl+Q: D20.

**The spin boxes (`Spin`).** Every spin box of the app (the editor's *Hours* / *Minutes*, the Timer's three) is a
`Spin : NumericUpDown` beside the app (`ui.h`), because UIKit's `NumericUpDown::onKey` (controls.cpp:195–220) returns
true for Enter, Esc, Up / Down / Page Up / Down, Backspace and **every digit, Ctrl held or not**, and the focused child
is asked first (`Widget::handleKey`, widget.cpp:143–158; in a card the veil is asked after its focused control).
`Spin::onKey (k)`:

| Key over a focused `Spin` | `NumericUpDown` alone | `Spin` |
|---|---|---|
| any key with **Ctrl** held (`kapi_get_modifiers () & MOD_CTRL`) | Ctrl+1…4 typed as a digit | **passed on** (false): Ctrl+1…4, Ctrl+N, Ctrl+Tab reach the root |
| **Enter** | commits the typed digits, eaten | `NumericUpDown::onKey (KEY_ENTER)` (commit, the `cb` fires), then **passed on**: the card's OK |
| **Esc** | drops the typed digits, eaten | drops them (the base's Esc), then **passed on**: the card's Cancel |
| digits, Backspace, Up / Down, Page Up / Down | the spin box's | unchanged |
| Space, letters, Tab, + | passed on | unchanged (passed on): Space starts the timer, R resets it |

`NumericUpDown` is not a text field (`isField ()` false), so in the **window** (the Timer tab, no card) Tab does not
leave a spin box — a click elsewhere, or Ctrl+1…4, does; in a **card** Tab walks every control (the veil is modal).
Nothing here needs a UIKit change.

---

## 8. The French words (the draft catalogue, `mockups/sd/apps/clock.app/lang/fr.txt`, about 150 lines)

The mock's catalogue is complete for every word drawn and for §5–§6; the Developer copies it to
`sdcard/apps/clock.app/lang/fr.txt` and `check.py clock` tells what the real sources add. The main ones:

| English | French | English | French |
|---|---|---|---|
| Clock | Horloge | World · Alarms · Timer · Stopwatch | Monde · Alarmes · Minuteur · Chronomètre |
| Summer time | Heure d'été | Time zone not set | Fuseau horaire non défini |
| Language & Region… | Langue et région… | Same time | Même heure |
| Tomorrow / Yesterday | Demain / Hier | %d cities · up to 12 | %d villes · 12 au plus |
| Add City / Remove | Ajouter une ville / Retirer | Move up / Move down | Monter / Descendre |
| No cities yet | Aucune ville pour l'instant | Add a City | Ajouter une ville |
| City · Time | Ville · Heure | Type to filter · Enter: add · Esc: cancel | Tapez pour filtrer · Entrée : ajouter · Échap : annuler |
| Next alarm: / today / tomorrow | Prochaine alarme : / aujourd'hui / demain | in %d h %d min | dans %d h %d min |
| No alarm set / No alarms | Aucune alarme active / Aucune alarme | %d alarms · %d on | %d alarmes · %d actives |
| New Alarm / Edit / Delete | Nouvelle alarme / Modifier / Supprimer | Edit Alarm | Modifier l'alarme |
| Once · Every day · Weekdays · Weekends | Une fois · Tous les jours · En semaine · Le week-end | Mon … Sun | lun. … dim. |
| Snoozed until %s / Missed at %s | Reportée à %s / Manquée à %s | Invalid | Non valable |
| Time · Hours · Minutes · Label · Repeat · Sound | Heure · Heures · Minutes · Nom · Répéter · Son | Next: %s %s | Prochaine : %s %s |
| Chimes · Beeps · Marimba · Test | Carillon · Bips · Marimba · Écouter | Alarm | Alarme |
| Snooze %d min / Stop | Rappel dans %d min / Arrêter | Enter: snooze · Esc: stop | Entrée : rappel · Échap : arrêter |
| Sound unavailable / Sound unavailable: the sound output is busy | Son indisponible / Son indisponible : la sortie audio est occupée | Time's up | Temps écoulé |
| The %s timer ended at %s | Le minuteur de %s s'est terminé à %s | Enter or Esc: stop · +: one more minute | Entrée ou Échap : arrêter · + : une minute de plus |
| Duration · hours · minutes · seconds | Durée · heures · minutes · secondes | Presets | Préréglages |
| Start · Pause · Resume · Reset | Démarrer · Pause · Reprendre · Remettre à zéro | of %s / Ends at %s / Paused | sur %s / Sonne à %s / En pause |
| Lap · Lap time · Total · Lap %d | Tour · Temps du tour · Total · Tour %d | Fastest / Slowest | Plus rapide / Plus lent |
| Copy Laps / Laps copied | Copier les tours / Tours copiés | No laps yet: Lap (L) while it runs | Aucun tour : Tour (L) pendant qu'il tourne |
| Missed alarm: %s | Alarme manquée : %s | Timer — %s done | Minuteur — %s terminé |
| View · Next Tab | Affichage · Onglet suivant | Turn On / Off | Activer / désactiver |
| Brussels · London · Vienna · Warsaw · Lisbon · Athens | Bruxelles · Londres · Vienne · Varsovie · Lisbonne · Athènes | the months, the days | janvier…, lundi… (small letters) |

Never translated (stored or compared, 02 §5): the day tokens `mon…sun`, the sound tokens, the tab names in
`config.ini`, the cities in `cities =`, the laps' numbers. The **laps' header** copied to the clipboard *is*
translated (02 #33: *Tour · Temps du tour · Total*).

## Changes after validation 1 (05-validation.md, 2026-10-07)

- **Gap 5 — keys over a spin box.** D12 and §7: every spin box is a `Spin : NumericUpDown` (`ui.h`) that passes on
  Ctrl keys, Esc (after dropping the typed digits) and Enter (after committing them); the table in §7 says which key
  goes where. 03 §10.2 (steps 5, 7, 9) and §10.3 (`clock-edit-keys`) updated.
- **Gap 4 — the French screenshots.** 03 §10.4 lists the block's French lines explicitly (`lang fr; sim … -fr; png
  … -fr; … lang "$SHOTS_LANG"`, the Critters / Pinball pattern); `SHOTS_LANG=fr SHOTS_PNG=<scratch>` stays only as
  CLAUDE.md's look-at-it check (it renders the same names, it does not add `-fr`).
- **The developer's notes taken in.** D5's ▲ / ▼ footer buttons: `ToolButton::setIcon (ToolIconFn, id)` with a 3-line
  drawer in `ui.h` calling `uk_glyph (WKG_CHEV_UP / WKG_CHEV_DOWN)`. The presets' double-click (D17) is the app's own:
  a second click on the same preset within 400 ms (`kapi_get_ticks`). **The dock's label stays *Clock* in French**
  (the dock shows `app.txt`'s `name` untranslated); the window's title is *Horloge*. No real date yet
  (`kapi_get_datetime` returns 0): the HereCard adds a dim ***Clock not set yet*** / *Horloge pas encore réglée* line
  (one `TR` word, in `fr.txt`) instead of a date.
