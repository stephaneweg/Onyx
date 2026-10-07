# AutoDev round 6 — Product Manager

Date: 2026-10-07. Role: PIPELINE.md §1.

## Inputs read

- `autodev/QUEUE.md` — series 2 (rounds 6-10): the queue is **empty**, free pick.
- `autodev/STATE.md` — rounds 1-5 built Notes + Stickies, Circuits, Turtle Quest missions, Pinball, Critters:
  none of them (nor what they cover) is a candidate. The multi-user plan is set aside by the user.
- `IDEAS.md` (Applications and Features tables) — still open (💡): the simple HTTP server, more Sokoban levels,
  the task scheduler, the File Viewer's colours / tags / bookmarks, DocumentKit, the ISO driver + `mount`,
  other languages on the BASIC VM.
- `docs/HANDOFF.md`, *End-user apps roadmap (decided with the user, 2026-09-30)* — Priority 1 done; Priority 2:
  Localisation (done since: the *Language & Region* applet `langconf`), About / System, Presentations (done:
  Slides), Quick notes (done: round 1); **Priority 3–4: Clock** — "alarms, timer, stopwatch, world clocks
  (notifications through notifyd; a small service for the alarms when the app is closed)", *moved there by the
  user, 2026-10-02*; Priority 5: a global key vault.
- `docs/04-USER-GUIDE.md` §12 (the catalog) and `ls user/Apps user/BinUtils` — **there is no clock app**
  (the menu bar shows the time, `lock` shows it full screen, `agenda` sends the Calendar's reminders, `date`
  prints it); no system "About" app (the Task Manager `taskman` has Processes / Memory / Processor / Network);
  no vault, no HTTP server (`ftpd`, `telnetd`, `rdpd`, `vncd` only), no scheduler.

## Candidates and scores (1 = poor, 5 = best)

| # | Candidate | Value to the user | Builds on what exists | Feasible in one round, testable in the simulator | Low risk to the system | Total /20 |
|---|---|:-:|:-:|:-:|:-:|:-:|
| 1 | **Clock** — alarms, timer, stopwatch, world clocks (HANDOFF roadmap, Priority 3–4, the user's own list) | 5 | 5 | 4 | 5 | **19** |
| 2 | **File Viewer: colours, tags, bookmarks** (IDEAS, the user's idea 2026-10-06) | 4 | 4 | 3 | 3 | 14 |
| 3 | **About / System** — version, kernel, CPU, temperature, RAM, uptime, network (HANDOFF Priority 2) | 3 | 4 | 4 | 4 | 15 |
| 4 | **Task scheduler** — triggers at boot / date / interval / weekdays, a service + a UI (IDEAS) | 4 | 3 | 2 | 3 | 12 |
| 5 | **Key vault** — an encrypted secrets store wired into Wi-Fi, Lisa, Mail, Courier, ftpfs (HANDOFF Priority 5) | 4 | 3 | 2 | 2 | 11 |
| 6 | Simple HTTP server (IDEAS) | 3 | 3 | 2 | 4 | 12 |
| 7 | Sokoban: ≥ 10 more levels (IDEAS) | 2 | 5 | 5 | 5 | 17* |

\* Sokoban scores high on ease but is a data-only task of an hour, not a round's application; it can ride along
a later game round.

Notes on the scores:

- **Clock**: an everyday app every desktop OS has and Onyx lacks; the user put it on the roadmap himself with a
  precise feature list. Everything it needs exists: the time and date (`kapi_get_datetime`, `kapi_get_ticks`),
  the time zones with summer time (SystemKit `systemkit/locale.h`: `locale_zone_count / city / offset / utc /
  summer`), notifications with an action (`systemkit/notify.h`), sound (AudioKit `ak_play`, the SoundFont's
  `ak_note_on`), the model of a background sender (`user/Apps/agenda/main.cpp` already notifies the Calendar's
  reminders from a file), the model of an app + its service (round 1's Notes / Stickies). The simulator has a
  fixed clock and a tick counter that advances with `msleep`: the stopwatch and the timer can be driven by a
  scenario; the alarm's firing can be unit-tested on the PC with a fake "now". No kernel / kapi change.
  The only part not fully visible in the simulator is the sound (checked by hand on the Pi).
- **File Viewer tags**: valued, but it changes a large, central app (`fileviewer`) and needs an index kept
  right through moves / renames / deletes in every program — wider than one round, and a risk to the most used
  app.
- **About / System**: much of it is already the Task Manager's; temperature / firmware data need real hardware
  to be seen.
- **Task scheduler**: a service started at boot with catch-up of missed runs; its value shows only over hours
  on the Pi, hard to test in the simulator.
- **Key vault**: real value, but its worth is in touching five apps' secret handling (risk), and the crypto
  choices deserve the user's say first.

## The pick: **Clock** (slug `clock`)

Why: the highest score, the user's own roadmap item with its features already listed, a daily-use app, entirely
built from existing kits (no kernel change), and almost all of it testable in the desktop simulator. It is not a
game (rounds 2-5 were games or learning games), which balances the series.

## Scope for this round

### MUST

1. **The app `clock`** (`user/Apps/clock/`, `sdcard/apps/clock.app/`), one window with four tabs (UIKit), in the
   desktop's style; **English + French from the first version** (`TR ()`, `lang/fr.txt`, `check.py clock` at 0).
2. **World clocks**: the local time big (hours, minutes, seconds, the date, the zone's city and `UTC±h`), a list
   of chosen cities (from SystemKit's zone table) with their time, the day offset ("tomorrow" / "yesterday") and
   the difference to here; add / remove / reorder; kept in the app's settings.
3. **Alarms**: a list of alarms (time, label, on / off, once or on chosen weekdays, a sound), add / edit / delete
   / toggle; the next alarm's "in 7 h 12 min" shown. When one rings: a notification (with the action that opens
   the Clock), a sound, and in the app a ringing dialog with **Snooze** (n minutes) and **Stop**.
4. **Alarms while the app is closed**: a small **service** without a window (or the existing `agenda` widget, if
   the Technical Analyst finds it is the cheaper one — it already sends the Calendar's reminders the same way)
   reads the alarms file and rings them. The file is the one source of truth; the app tells the service to
   reload on a change (IPC, as Notes tells Stickies).
5. **Timer** (countdown): hours / minutes / seconds set, Start / Pause / Reset, a ring at zero (notification +
   sound), a few presets (1, 3, 5, 10, 15 min).
6. **Stopwatch**: Start / Stop / Lap / Reset, the laps listed (lap time and total, the best and worst marked),
   hundredths shown; copy the laps to the clipboard.
7. Files: `SD:/apps/clock.app/config.ini` (the world cities, the last tab) and an alarms file (plain text, one
   alarm per line or an `fk_kv` file — the Product Analyst fixes its format); never in a format a translation
   could change.
8. **The docs** (docs/04 catalog entry, docs/03 if a kit gains something), the `shots.sh clock` scenario and the
   screenshots in English and French, the app in `user/Makefile`, its package declared (not published) in
   `tools/pkg/packages.ini`, the dock's category (Productivity / Accessories).
9. **Tests**: a PC unit test of the alarm logic (next occurrence of a once / weekday alarm across midnight, a
   week, a summer-time change; snooze; a missed alarm while the Pi was off is not rung hours late), the simulator
   scenario for the four tabs.

### SHOULD (in this order)

- The **menu bar's clock**: a right-click (or an entry in its calendar pop-up) "Alarms and timers…" opening the
  Clock, and a small bell icon beside the time when an alarm is set — only if it is a few lines in
  `user/Apps/menubar/main.cpp`.
- An analogue face for the local time (UIKit canvas), switchable with the digital one.
- Several timers at once, named.
- The stopwatch and the timer keep running when the window is closed and reopened (their start time saved).

### Deferred

- A full-screen "night clock" / bedside mode; a world map with the day / night line.
- Alarm sounds chosen from the user's music (Media Player's library); a gradual volume.
- The task scheduler (IDEAS) — the alarms' service could become its seed later.

## Acceptance (for the Product Analyst to refine)

- `shots.sh clock` renders the four tabs (World, Alarms, Timer, Stopwatch) in English and French, the words fit.
- The alarm unit test passes deterministically on the PC.
- With a fake "now" one minute before an alarm, the service / app rings it exactly once; Snooze rings it again
  after the chosen minutes; a disabled alarm never rings.
- World clocks show the right offset for a zone on summer time and one that is not (by the simulator's fixed
  date, 2026-09-28).
- `python tools/lang/check.py clock` says 0 missing. No kernel / kapi change; the other apps' screenshots unchanged.

## Existing code to build on

- **Time**: `user/Kits/appkit/appkit.h` — `kapi_get_datetime`, `kapi_get_ticks`, `kapi_clock_info`,
  `kapi_pump_sleep`.
- **Time zones**: `user/Kits/systemkit/locale.h` / `locale.inc` — `locale_zone_*`, the system's zone
  (`SD:/etc/system.ini`), summer time; the `langconf` applet (`user/Apps/langconf/`) as the user of that table.
- **Notifications**: `user/Kits/systemkit/notify.h` — `notify_action (title, text, "clock ...")`;
  `user/Apps/notifyd/main.cpp`.
- **A background reminder sender**: `user/Apps/agenda/main.cpp` (`reminders ()`: reads
  `SD:/apps/calendar.app/reminders.txt`, notifies the minutes that have come); the Calendar's reminder choices in
  `user/Apps/calendar/dialogs.h`.
- **App + service + IPC reload, tested in the simulator**: round 1, `user/Apps/notes/` (`notesmodel.*`,
  `stickies_proto.h`) and `user/Apps/stickies/main.cpp`; `SIM_SERVICES`, `SIM_MBOX` in
  `tools/tests/desktop_sim/fakekapi.cpp`.
- **Sound**: AudioKit `user/Kits/audiokit/audiokit.h` — `ak_play (path, loop)`, `ak_play_stop`, `ak_note_on`
  (the SoundFont in `sdcard/res/soundfonts`); `user/BinUtils/tone.cpp` as a small example.
- **Settings files**: FileKit `filekit/kvtext.h` (`fk_kv_*`, used by Circuits / Pinball / Critters), AppKit's
  `.ini` helpers.
- **UI**: UIKit (`docs/11-UIKIT.md`) — tabs, lists, spin boxes, check boxes for weekdays, dialogs; the Task
  Manager's tabbed window (`user/Apps/taskman/main.cpp`) and Calendar's dialogs as models; the time shown big
  as in `user/Apps/lock/main.cpp`.
- **The clipboard** (laps copied): `user/Kits/systemkit/clipboard.h`.
- **Translation**: `uikit/lang.h`, `tools/lang/check.py`; **screenshots**: `tools/tests/desktop_sim/shots.sh`.
