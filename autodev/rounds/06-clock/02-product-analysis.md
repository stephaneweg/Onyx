# AutoDev round 6 — Product Analyst: Clock

Date: 2026-10-07. Input: `01-product-manager.md` (the pick: **Clock** — world clocks, alarms that ring with
the app closed, a countdown timer, a stopwatch; the user's roadmap item, `docs/HANDOFF.md` Priority 3–4:
"alarms, timer, stopwatch, world clocks (notifications through notifyd; a small service for the alarms when
the app is closed)").

Looked at: the `agenda` widget (`user/Apps/agenda/main.cpp`: `reminders ()` reads
`SD:/apps/calendar.app/reminders.txt` — `YYYYMMDDHHMM|text` — every few seconds and notifies the minutes that
came **since its last look**, never the past at its start); round 1's Notes / Stickies (one app + one
background program, a one-instance service `notes` with `NOTES_MSG_OPEN`, a `RELOAD` message to the widget,
`stickies_proto.h`); SystemKit `locale.h` (23 zones, `locale_zone_offset / city / utc / summer`, the system's
zone in `SD:/etc/system.ini` `zone=`; summer time judged by the day only), `notify.h` (`notify_action (title,
text, "app args")`: a click runs that app), `clipboard.h`; AudioKit (`ak_note_on` on the SoundFont of
`SD:/res/soundfonts`, `ak_play` for a file — the card has no sound effects files, only the SoundFont); the menu
bar (24-hour `HH:MM`, its calendar pop-up with *Open Calendar*); docs/04 §5 (notifications, the agenda widget,
the clipboard) and §12 (the catalog: no clock app); `SD:/etc/autostart`; where apps keep their data (the
Calendar in `SD:/apps/calendar.app/`, Notes' documents in `SD:/Notes/`); the simulator's fixed clock
(`fakekapi.cpp`: 2026-09-28 12:34:00, a **Monday**, in the summer-time season of the EU and the US).

---

## 1. The name

- **Clock** — the application (`user/Apps/clock/`, `SD:/apps/clock.app/`; `app.txt`: `name = Clock`,
  `category = Productivity`). Started from the dock's Productivity drawer, `run clock`, a notification's click,
  or (*should*) the menu bar's clock. In French: **Horloge** (its window title and the dock's label through
  `lang/fr.txt`; the program's name stays `clock`).
- **clockd** — its background part, the **alarm service**: no window, started at boot by `SD:/etc/autostart`
  (`run clockd`), the `clockd` IPC service. Its sources beside the app's (`user/Apps/clock/`, the alarm code
  shared), built as a second program, like Stickies from Notes. A service of its own rather than the `agenda`
  widget: the agenda is a desktop decoration the user may remove from autostart, and an alarm must not depend on
  a widget being shown (the Technical Analyst may still argue otherwise on cost, but the product requires that
  removing `run agenda` never silences an alarm).

## 2. The one-line pitch

**Clock: the time here and around the world, alarms that ring even when the app is closed, a kitchen timer and
a stopwatch — in English and French.**

## 3. The users and their tasks

| User | Task | How Clock serves it |
|---|---|---|
| Anyone at the Pi | **"Remind me in 10 minutes"** — the tea, the oven, a break | Timer tab: a preset (10 min), **Start**; it rings with a sound and a notification |
| The household | A **daily alarm** (wake-up, medicine, the school run) | Alarms tab: 07:00, Mon–Fri, label *School*; it rings at 07:00 whatever app is open, the Clock closed |
| The developer (the user) | Know **what time it is for a friend / a server** abroad | World tab: Tokyo, New York… with their time, *Tomorrow* / *Yesterday*, the difference to here |
| A pupil / a sports club | **Time** a run, a reading, an experiment, with laps | Stopwatch tab: Start / Lap / Stop, hundredths; the laps copied to the clipboard into Ledger or Letters |
| Anyone | Stop or delay a ringing alarm | The ringing dialog: **Snooze** (10 min) or **Stop**; Esc = Stop |

## 4. Features

All four tabs live in **one window** (a tab bar: **World · Alarms · Timer · Stopwatch**; French: **Monde ·
Alarmes · Minuteur · Chronomètre**), in the desktop's style (UIKit). Times are **24-hour** `HH:MM[:SS]`, as the
menu bar shows them, in both languages. The dates and weekdays are in the system's language (Monday / lundi).

### Must (this round)

**General**

1. **One Clock at a time**: the app registers the service `clock`; a second `clock …` started passes its
   arguments to the running one (message `CLOCK_MSG_OPEN`), raises it and quits (as Notes).
2. **Arguments**: `clock` (the last tab), `clock world|alarms|timer|stopwatch` (that tab), `clock --ring <id>`
   (the ringing dialog of alarm `<id>`, started by clockd; §4.4).
3. **Bilingual**: every word in `TR ()`, `sdcard/apps/clock.app/lang/fr.txt`; `tools/lang/check.py clock` at
   0 missing; the window fits in French (*Chronomètre*, *Répéter*, *Rappel dans 10 min*).
4. **Keyboard**: **Ctrl+1…4** the tabs, **Ctrl+Tab** the next tab; **Space** starts / stops the timer or the
   stopwatch of the tab shown; **L** a lap (stopwatch), **R** reset (timer / stopwatch, when stopped);
   **Ctrl+N** a new alarm (Alarms) or a new city (World); **Delete** the selected alarm / city; **Ctrl+Q** /
   the close button quit (§4.5).

**4.1 World**

5. **Here, big**: the local time `HH:MM:SS` in a large face, the full date (*Monday 28 September 2026* /
   *lundi 28 septembre 2026*), the zone's city and its `UTC±h` (`locale_zone_utc`; *Summer time* / *Heure
   d'été* when on), refreshed every second. No zone chosen (`locale_zone () == -1`): *Time zone not set* and a
   button *Language & Region…* that opens the applet (`control langconf`).
6. **The cities**: a list of the chosen cities (from SystemKit's zone table only — the 23 cities it has), each
   row: the **city**, its **time** `HH:MM`, the **day** when not today (*Tomorrow* / *Yesterday*; *Demain* /
   *Hier*), the **difference** to here (*+7 h*, *−6 h*, *+5 h 30*, *Same time*). Refreshed every second
   (cheaply: the minute changes).
7. **Add a city**: **+** (Ctrl+N) opens a list of the zones not yet chosen (city + `UTC±h`), typing filters it;
   OK / double-click adds it at the end. **Remove** (−, Delete). **Move up / Move down** buttons reorder. At
   most **12** cities. Kept in `config.ini` (§5.2) at once.
8. **Empty list**: a hint *"Add the cities you want to follow with +"*; the first start proposes nothing (no
   guess) — the list starts empty.

**4.2 Alarms**

9. **The list**: each alarm one row — its **time** big (`07:00`), its **label** (*Alarm* when none), its
   **repeat** (*Once*, *Every day*, *Weekdays*, *Weekends*, or the days: *Mon, Wed, Fri*; *Une fois*, *Tous les
   jours*, *En semaine*, *Le week-end*, *lun., mer., ven.*), an **on / off switch**. Sorted by time. A once
   alarm already rung shows off.
10. **The next alarm**: above the list, *"Next alarm: tomorrow 07:00 — in 18 h 26 min"* (*Prochaine alarme :
    demain 07:00 — dans 18 h 26 min*); *No alarm set* / *Aucune alarme* when none is on.
11. **New / edit** (**+**, double-click, Enter): a dialog — **time** (two spin boxes, hours 0–23, minutes
    0–59), **label** (a text field, 40 characters at most), **repeat** (seven day check boxes Mon…Sun in the
    language's order — Monday first in both — with *Every day* / *Weekdays* shortcuts; none ticked = once),
    **sound** (a choice of the built-in sounds, §4.4, with a ▶ **Test** button), **OK / Cancel** (and
    **Delete** when editing). At most **20** alarms.
12. **Toggle** a row's switch: on / off, written at once. Turning a once alarm on (or saving it) sets it to the
    **next** time that `HH:MM` comes (today if still to come, else tomorrow).
13. **Delete**: the − button / Delete / the dialog's Delete — no confirmation (the alarm is small to remake).
14. **Every change** is written to `alarms.txt` (§5.1) and clockd is told to reload (`CLOCKD_MSG_RELOAD`);
    clockd also notices a changed file by itself within 30 s (a file edited by hand, a message lost).

**4.3 Alarms with the app closed: clockd**

15. **clockd runs from boot** (`run clockd` in `SD:/etc/autostart`, beside `run notifyd`, not held back by
    Setup), no window, the service `clockd`; it reads `alarms.txt`, and checks the time about **every second**.
16. **An alarm rings once, on its minute**: when the local clock reaches `HH:MM:00` of an enabled alarm on one
    of its days (or its once date), clockd **rings it** within 2 s — exactly once for that minute, even if the
    file is reloaded during that minute.
17. **Ringing** = (a) a notification **"Clock — 07:00 School"** (*Horloge — 07:00 École*) whose click opens the
    Clock on its Alarms tab (`notify_action (…, "clock alarms")`), and (b) clockd starts **`clock --ring <id>`**
    (or tells the running Clock so): the **ringing dialog** comes to the front — the time, the label, **Snooze**
    (*Rappel dans 10 min* / *Snooze 10 min*) and **Stop** (*Arrêter*), the sound looping meanwhile. Esc = Stop;
    Enter = Snooze.
18. **Snooze**: the dialog closes, the sound stops; the alarm rings again after the snooze minutes (10 by
    default, 1–30 in `config.ini`), as many times as snoozed; the next-alarm line says *Snoozed until 07:10*.
19. **Stop**: the sound stops; a once alarm is now off (shown off); a repeating one waits for its next day.
20. **Unanswered**: after **2 minutes** of ringing without an answer the sound stops and the dialog closes by
    itself; the notification *"Missed alarm: 07:00 School"* stays (counted as Stop).
21. **Late or missed**: an alarm whose minute came while the Pi was off (or clockd not running) is **not rung
    late** — clockd, like the agenda, starts by taking "now" as already looked at. A delay of up to **2
    minutes** (the Pi busy, a slow boot of clockd during that minute) is still rung; later is not.
22. **A disabled alarm never rings**, nor a deleted one, nor one whose days do not include today.
23. **Day and summer time**: the alarms are in the **local wall time** the menu bar shows: 07:00 rings at
    07:00 on the clock, also on the days summer time begins or ends (the next occurrence is counted in calendar
    days, not 24 h steps). (The "in 7 h 12 min" line may be an hour off across that night: said, accepted.)

**4.4 The sounds**

24. **Three built-in sounds**, played on AudioKit's General MIDI synthesizer (the card's SoundFont, no sound
    file needed): **Chimes** (a rising arpeggio, *Carillon*), **Beeps** (short high notes, *Bips*), **Marimba**
    (a soft phrase). Stored by a fixed token (`chimes`, `beeps`, `marimba`). Played at the system's volume
    (`SD:/etc/sound.ini`), looped while ringing; ▶ Test plays one round.
25. **No sound possible** (no SoundFont, no audio output): the alarm still rings — the dialog and the
    notification — silently; nothing crashes, the status line says *Sound unavailable*.
26. The ringing **sound is played by the Clock app** (the ringing dialog's process), so Stop / Snooze stop it at
    once; clockd only decides *when*.

**4.5 Timer**

27. **Set**: three spin boxes hours (0–23) / minutes (0–59) / seconds (0–59), and **presets** 1, 3, 5, 10, 15
    min (a click sets the time; a double-click sets and starts). Shown big `MM:SS` (`H:MM:SS` from an hour), with
    a ring of progress (the time left).
28. **Start / Pause / Resume / Reset**: Start counts down; Pause holds; Reset back to the time set (the spin
    boxes are locked while it runs). Accurate to the second over an hour (counted from `kapi_get_ticks`, not by
    adding frames).
29. **At zero**: the *Time's up* dialog (*Temps écoulé*) with the sound looping, **Stop** and **+1 min**
    (*+1 min* starts it again for a minute), a notification *"Timer — 10:00 done"* (*Minuteur — 10:00
    terminé*); Clock comes to the front. Unanswered: stops after 2 minutes as an alarm.
30. **The last time set** is kept (`config.ini`) and proposed next time.

**4.6 Stopwatch**

31. **Start / Stop / Lap / Reset**: the time big `MM:SS.hh` (`H:MM:SS.hh` from an hour), hundredths shown,
    refreshed at the screen's pace; **Lap** while running; **Reset** when stopped clears the time and the laps.
32. **The laps**: a list, newest first — *Lap n*, the **lap time**, the **total**; from 3 laps, the **fastest**
    in green and the **slowest** in red (marked also by a word / sign, not only by colour). Up to 999 laps.
33. **Copy the laps** (*Edit ▸ Copy Laps*, Ctrl+C on the Stopwatch tab): to the clipboard as plain text, one lap
    a line, tab-separated, the header in the system's language:
    ```
    Lap	Lap time	Total
    1	00:12.34	00:12.34
    2	00:11.90	00:24.24
    ```
    then a notification *Laps copied* through SystemKit's clipboard (as the other apps do).

**4.7 Closing**

34. **Closing with a timer or stopwatch running** asks *"The timer is still running. Quit anyway?"*
    (*Quit* / *Cancel*); the alarms never need the app (clockd). Closing keeps the tab and the window's size.

### Should (if the round has time; in this order, each independent)

- **The timer handed to clockd** when the Clock is closed while it runs (instead of the question of 34): its end
  written as a one-time entry (`[timer]` in `alarms.txt`) that clockd rings like an alarm, with the timer's
  label; the stopwatch's start saved so it shows the right time when reopened.
- **The menu bar**: a small **bell** left of the time when an alarm is on (the next one within 24 h), and
  *Alarms and timers…* (*Alarmes et minuteurs…*) in the time's calendar pop-up, beside *Open Calendar* — only if
  a few lines in `user/Apps/menubar/main.cpp`.
- **A missed-alarm notification at start**: clockd starting finds a once alarm whose time passed while the Pi was
  off (within the last 12 h): *"Missed alarm: 07:00 School"*, no sound.
- **An analogue face** for "here" on the World tab (UIKit canvas), *View ▸ Analogue / Digital*, kept in
  `config.ini`.
- **Named timers, several at once** (a list on the Timer tab).

### Later (not this round)

- A full-screen bedside / night clock; a world map with the day / night line.
- Alarm sounds from the user's music (the Media Player's library), a volume that grows.
- More cities than SystemKit's 23 zones (the zone table grows in SystemKit, not in the Clock).
- 12-hour display (AM / PM) as a region setting (the system's, not the app's).
- The task scheduler (IDEAS.md): clockd could become its seed.
- Alarms from the BASIC or a `/bin/alarm` command; alarms shared with the Calendar's reminders.

## 5. The files it reads and writes

All under the app's folder, `SD:/apps/clock.app/` (as the Calendar's `reminders.txt` and `config.ini`): they
are the app's settings, not the user's documents. **Nothing stored is translated**: the days, the sounds, the
tabs, the cities are kept as fixed English tokens; only what is shown goes through `TR ()`.

### 5.1 The alarms — `SD:/apps/clock.app/alarms.txt`

A FileKit `fk_kv` text (`filekit/kvtext.h`): one `[alarm]` block an alarm, in the order of their creation.
**Written by the Clock only** (one writer); clockd only reads it.

```ini
# Clock -- the alarms (written by the Clock app, rung by clockd). One [alarm] block an alarm.
[alarm]
id     = 1                 ; a number unique in the file, never reused while the file lives
time   = 07:00             ; HH:MM, 24-hour, local wall time
label  = School            ; the user's words (UTF-8, at most 40 characters; may be empty)
on     = 1                 ; 1 enabled, 0 off
days   = mon tue wed thu fri   ; the days it repeats, among mon tue wed thu fri sat sun; empty = once
date   =                   ; a once alarm: the day it rings, YYYYMMDD (set when it is saved / turned on)
sound  = chimes            ; chimes | beeps | marimba
snooze =                   ; YYYYMMDDHHMM: snoozed, rings again at that minute (empty: not snoozed)

[alarm]
id     = 2
time   = 14:30
label  = Medicine
on     = 1
days   =
date   = 20260928
sound  = marimba
snooze =
```

Rules:

- A **once** alarm (`days` empty) rings at `date` + `time`; once that minute has passed it **is off for the
  user** (the app shows it off and writes `on = 0` the next time it saves the file — clockd never writes):
  clockd reads "date + time in the past" as nothing to ring.
- A **repeating** alarm rings on each listed weekday at `time`; `date` is ignored.
- `snooze`, when set, rings once more at that minute (in addition to the normal schedule); the Clock clears it on
  Stop and when it has rung.
- An unknown key is kept (fk_kv writes back what it read); an unknown day token or sound is ignored (sound:
  `chimes`); a block with no valid `time` is skipped by clockd and shown as *Invalid* in the app (it can be
  deleted). Missing file = no alarms; the file is created at the first alarm.
- At most 20 alarms (more in a hand-edited file: the first 20 are used).

### 5.2 The settings — `SD:/apps/clock.app/config.ini`

AppKit's `.ini` (editable in the Control Panel's *App Settings*):

```ini
# Onyx Clock settings.
[clock]
tab     = world            ; world | alarms | timer | stopwatch: the tab opened by "clock" alone
cities  = Tokyo,New York,London   ; the World tab's cities, SystemKit's zone names (English), in order
snooze  = 10               ; minutes, 1..30
timer   = 300              ; the timer's last time set, seconds
width   = 560              ; the window's size
height  = 440
```

An unknown city in `cities` is skipped (not shown, dropped at the next write).

### 5.3 Read, never written

- `SD:/etc/system.ini` (through SystemKit's `locale.h`): the language, the zone.
- `SD:/etc/sound.ini` (through AudioKit): the volume.
- `SD:/res/soundfonts/*` (through AudioKit): the alarm sounds.
- `SD:/etc/autostart`: the line `run clockd` is shipped on the card (`sdcard/etc/autostart`); the Clock never
  edits autostart.

## 6. File associations

None. The Clock opens no document; `alarms.txt` and `config.ini` are its own (a `.txt` / `.ini` double-clicked
in the File Viewer still opens in the Text Editor).

## 7. How it fits with the other apps

| With | How |
|---|---|
| **notifyd** (SystemKit `notify_action`) | Every ring (alarm, timer) and *Missed alarm* is a notification; its click runs `clock alarms` / `clock timer`. The notification alone says it when the app cannot start. |
| **The clipboard** (SystemKit `clip_*`) | The stopwatch's laps copied as text (tab-separated: pasted into **Ledger** they fill columns). Nothing pasted into the Clock (the label field takes paste as any UIKit text field). |
| **The menu bar's clock** | Untouched as a must. *Should*: a bell when an alarm is on, *Alarms and timers…* in its calendar pop-up. The Clock shows the same local time (one source: `kapi_get_datetime`). |
| **Language & Region** (`langconf`) | The system's zone and language; the Clock never sets the zone, it links to the applet when none is set. A zone changed there is seen by the Clock within a minute (re-read with the time). |
| **Calendar / Agenda** | Independent: the Calendar's reminders stay the agenda's (`reminders.txt`), the alarms are the Clock's. No shared file. (A later idea: one service for both.) |
| **Notes / Stickies** | The same pattern (an app + a background program + a reload message), nothing shared. |
| **The dock** | In the Productivity drawer; clockd (category `Shell`) is not shown in the drawers. |
| **The Task Manager** | clockd appears as a process; killing it stops the alarms until the next boot or until the Clock is started (the Clock starts clockd if `kapi_ipc_lookup ("clockd")` finds none). |
| **Drag & drop** | None needed (nothing to drop; nothing worth dragging out). |

## 8. What it does NOT do

- No time setting: the clock is set by NTP / `SD:/etc/clock` (the system's); the zone by Language & Region.
- No wake from power-off or from the screen saver's sleep beyond what the Pi does (the Pi has no RTC alarm: an
  alarm rings only while Onyx runs).
- No cities beyond SystemKit's zone table; no live time-zone database, no historical summer-time rules.
- No 12-hour display; no seconds on alarms (minute precision); no alarm sound files of the user's.
- No calendar events, no reminders on notes (that is the Calendar's).
- No sound through Bluetooth (Onyx has none) — the jack / HDMI as the rest of the system.
- No network: nothing leaves the Pi.

## 9. Acceptance criteria

Each is a testable statement. **[PC]** = checked on the PC: the simulator (`tools/tests/desktop_sim`,
`shots.sh clock`, its fixed clock **Monday 2026-09-28 12:34:00**) or a PC unit test (`tools/tests/clock/`, a fake
"now"). **[Pi]** = only the Pi can check it (by hand, said in the round's report).

**Build, language, docs**

1. [PC] `make` builds `clock` and `clockd` without a warning added; both are in `user/Makefile`;
   `sdcard/apps/clock.app/app.txt` says `name = Clock`, `category = Productivity`.
2. [PC] `python tools/lang/check.py clock` reports **0 missing** (clockd's words included).
3. [PC] `shots.sh clock` produces the screenshots of the four tabs (World with 3 cities, Alarms with 3 alarms,
   Timer running, Stopwatch with laps) and of the ringing dialog, in **English and in French**
   (`SHOTS_LANG=fr`); in French no label is cut or overlaps (checked on the pictures).
4. [PC] docs/04's catalog has the Clock (its tabs, keys, the files of §5, clockd); `sdcard/etc/autostart` has
   `run clockd`; the package is declared in `tools/pkg/packages.ini` (not published);
   `python docs/build_docs.py` runs.
5. [PC] No kernel or kapi file changed (`git diff main -- kernel/ user/Kits/appkit/` empty for this round); the
   other apps' screenshots produced by `shots.sh` are unchanged.

**World**

6. [PC] With the system zone Brussels and the simulator's date (EU summer time), the World tab shows
   `12:34:00`, *Monday 28 September 2026* (*lundi 28 septembre 2026* in French) and `UTC+2`, *Summer time*.
7. [PC] With Tokyo, New York and London added, the rows say **Tokyo 19:34 +7 h**, **New York 06:34 −6 h**,
   **London 11:34 −1 h** (Tokyo has no summer time, New York and London have it on that date).
8. [PC] Unit test: at a fake local 23:30 in Brussels, Tokyo shows *Tomorrow* (06:30); at 01:00, Los Angeles shows
   *Yesterday* (16:00).
9. [PC] Adding a city writes `cities =` in `config.ini` in the shown order; Move up / down and Remove change it;
   restarting the Clock shows the same list; the add dialog does not list a city already chosen; a 13th city
   cannot be added.
10. [PC] With no zone in `system.ini` (and no `timezone=`), the World tab shows *Time zone not set* and its
    button runs `control langconf`.

**Alarms — the app**

11. [PC] Creating an alarm 07:00 *School* Mon–Fri *chimes* writes exactly one `[alarm]` block with
    `time = 07:00`, `label = School`, `on = 1`, `days = mon tue wed thu fri`, `sound = chimes` (whatever the
    system's language: French UI writes the same tokens).
12. [PC] At the simulator's Monday 12:34, the next-alarm line for that only alarm reads *Next alarm: tomorrow
    07:00 — in 18 h 26 min*; with a once alarm 14:30 added, *Next alarm: today 14:30 — in 1 h 56 min*; with all
    alarms off, *No alarm set*.
13. [PC] A once alarm created at 12:34 for 10:00 gets `date = 20260929` (tomorrow); for 13:00, `date =
    20260928`.
14. [PC] The repeat column shows *Weekdays* for mon–fri, *Every day* for all seven, *Weekends* for sat sun, *Mon,
    Wed, Fri* otherwise, *Once* for none (and the French words in French).
15. [PC] Toggling, editing and deleting an alarm each rewrite `alarms.txt` and send `CLOCKD_MSG_RELOAD` to the
    `clockd` service (seen in the simulator's mailbox, `SIM_MBOX`); a 21st alarm cannot be added.
16. [PC] A hand-written `alarms.txt` with an unknown key keeps that key after the app saves; a block with
    `time = 25:99` is shown *Invalid* and never rung.

**Alarms — the ringing logic (PC unit test, fake "now", deterministic)**

17. [PC] An enabled 07:00 mon–fri alarm: "now" stepped second by second from Monday 06:58:00 to 07:03:00 → it
    rings **exactly once**, at a time between 07:00:00 and 07:00:02; reloading the file at 07:00:30 does not ring
    it again.
18. [PC] The same alarm is not rung on Saturday or Sunday; its next occurrence from Friday 08:00 is **Monday
    07:00**; from Sunday 23:59:59 it is Monday 07:00 (across midnight and the week's end).
19. [PC] A disabled alarm and a deleted one never ring over a simulated week.
20. [PC] A once alarm (date 20260929, 07:00) rings once on 2026-09-29 07:00, and never again over the following
    week.
21. [PC] Snooze 10 at 07:00:20 sets `snooze = 202609290710`; the alarm rings again at 07:10 (once), and not at
    07:20 unless snoozed again.
22. [PC] Missed: clockd started at 09:00 with a 07:00 alarm for that day does not ring it; a "now" that jumps
    from 06:59:50 to 07:01:30 (a busy Pi) rings it; a jump from 06:59 to 07:03 does not.
23. [PC] Summer time: a daily 07:00 alarm's next occurrences from Saturday 2026-10-24 08:00 are **2026-10-25
    07:00** and **2026-10-26 07:00** (the day summer time ends counted as one calendar day); same around
    2027-03-28.

**Alarms — clockd and the ringing dialog**

24. [PC] In the simulator, with an alarm one minute after the fake "now" and clockd running, clockd sends one
    notification *Clock — HH:MM label* with the action `clock alarms`, and starts `clock --ring <id>` (or sends
    the running Clock `CLOCK_MSG_OPEN` with `--ring <id>`).
25. [PC] `clock --ring 1` (by `SIM_ARGS`) shows the ringing dialog: the time, the label, **Snooze 10 min** and
    **Stop**; Esc acts as Stop, Enter as Snooze; Stop on a once alarm leaves it shown off and written `on = 0`.
26. [PC] A second `clock alarms` started while one runs: one window only, raised, on the Alarms tab.
27. [PC] The Clock started while no `clockd` service is registered starts clockd.
28. [Pi] An alarm set 2 minutes ahead, the Clock then closed: it rings on time (dialog + notification + sound)
    while another app (the Terminal, a game) is in front; Snooze and Stop silence it at once.
29. [Pi] With no answer, the sound stops after about 2 minutes and *Missed alarm* stays in the notifications.
30. [Pi] The three sounds are heard at the system volume (jack and HDMI); with the volume muted, the dialog still
    shows; with the SoundFont removed, the alarm rings silently and nothing crashes.
31. [Pi] After a reboot, the alarms are still there and clockd rings the next one; an alarm whose time passed
    while the Pi was off is not rung at boot.

**Timer**

32. [PC] The presets set the spin boxes (5 min → 0 / 5 / 0); Start shows `05:00` then counts down (the
    simulator's ticks advancing); Pause holds the time; Reset returns to `05:00`; the spin boxes are locked while
    it runs.
33. [PC] A 3-second timer reaching zero shows the *Time's up* dialog and sends the notification *Timer — 00:03
    done*; **+1 min** restarts it at `01:00`; **Stop** returns it to the time set.
34. [PC] Unit test: the time left is computed from the start tick, not from counted frames: started at tick T for
    600 s, at tick T + 599 400 ms it shows `00:01`, at T + 600 000 it rings (no drift with irregular frames).
35. [PC] The last time set is in `config.ini` (`timer = 300`) after closing and proposed at the next start.
36. [Pi] A 10-minute timer on the Pi rings within 1 s of a phone's stopwatch.

**Stopwatch**

37. [PC] Start, three laps, Stop: the list shows 3 rows newest first with lap and total times, the fastest marked
    green and the slowest red (each also marked in words / a sign); Reset clears them; Reset is disabled while
    running.
38. [PC] Copy Laps puts on the clipboard the header line (in the system's language) then one line a lap,
    tab-separated, `MM:SS.hh` (as §4.6), and shows *Laps copied*.
39. [PC] Unit test: the formatting `00:12.34`, `59:59.99`, `1:00:00.00`, and the lap times' sum equal to the
    total.

**Closing and keys**

40. [PC] Closing the window with the timer or the stopwatch running asks *Quit anyway?*; Cancel keeps it running;
    with nothing running it closes at once; the tab and the window's size are kept in `config.ini`.
41. [PC] Ctrl+1…4 switch the tabs; Space starts / stops the timer or the stopwatch on their tab; L adds a lap;
    `clock stopwatch` opens on the Stopwatch tab.

---

**Summary for the next roles**: the must list is items 1–34; two programs (`clock`, `clockd`) from
`user/Apps/clock/`, the alarm logic in a file of its own testable on the PC with a fake "now"; two files in
`SD:/apps/clock.app/` (`alarms.txt` — fk_kv, written by the Clock only; `config.ini`); messages `CLOCK_MSG_OPEN`
(to `clock`) and `CLOCKD_MSG_RELOAD` (to `clockd`); no kernel / kapi change; 41 acceptance criteria, of which
5 (28–31, 36) need the Pi.
