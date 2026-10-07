# AutoDev round 1 — Product Manager

Date: 2026-10-06. Round 1 (no earlier rounds in `autodev/STATE.md`).

## Inputs read

- `IDEAS.md` (applications table, raw ideas, action plan P1–P9).
- `docs/HANDOFF.md`, *End-user apps roadmap* (the user's priority order, decided 2026-09-30):
  Priority 1 all done (Media Player, Mail, PDF Viewer, Screenshot, Photos); **Priority 2**:
  Localisation (*Region & Keyboard*), About / System, Presentations (**done**: Slides), **Quick notes
  with a desktop widget that can be shown or hidden**; **Priority 3–4**: Clock (alarms, timer,
  stopwatch, world clocks), the package manager (done); **Priority 5**: a global key vault.
  *Other open items* and the ideas list (task scheduler, HTTP server, ISO9660...).
- `docs/04-USER-GUIDE.md` §12 (the catalog), `user/Apps/` (≈110 folders), `user/BinUtils/`.
- `tools/tests/desktop_sim/shots.sh` (how apps run on the PC: built against `fakekapi.cpp` +
  UIKit, driven by event scripts; the `agenda` desktop widget already has a scenario there).

Excluded by the brief: the multi-user plan (set aside), Jet / WebKit, the GameCube emulator.

## Existence checks

| Candidate | Already in Onyx? |
|---|---|
| Quick notes + desktop widget | **No.** No `notes` app; `grep -i "sticky\|quick note"` in `user/Apps` finds nothing. Only tinypad (a plain editor) and the `agenda` widget (a precedent for a desktop widget). |
| Clock (alarms, timer, stopwatch, world clocks) | **No.** Only the menu bar's clock + its calendar pop-up, and `lock`'s full-screen time; `stopwatch`/`alarm` appear only in `calendar/model.h` (iCalendar VALARM). |
| About / System | **No** dedicated app; Task Manager has Processes / Memory / Network tabs only. No kapi for the CPU temperature (`kapi_abi.h` has none). |
| Region & Keyboard applet | **Partly**: Setup (first-run wizard) has the country → layout / time zone tables; `keyconf` has only layout + wheel speed. |
| Key vault | **No.** |
| Task scheduler | **No** (idea in HANDOFF *Other open items*). |

## Scores (1 = poor, 5 = best)

Value = everyday / educational use, weighted by the user's own priority lists. Base = what Onyx
already has to build on. Feasibility = one round, a result visible in the PC simulator.
Risk = 5 when no kernel / kapi change and nothing shared is touched.

| # | Candidate | Value | Base | Feasibility | Risk | **Total /20** |
|---|---|:-:|:-:|:-:|:-:|:-:|
| 1 | **Quick notes + desktop widget** (`notes`) | 4 (Priority 2, everyday) | 5 (UIKit text editing, the `agenda` widget model, SystemKit clipboard / file associations, FontKit) | 5 (a window + a widget, plain files; `agenda` already runs in the simulator) | 5 (no kapi, no kernel) | **19** |
| 2 | Clock: alarms, timer, stopwatch, world clocks (`clock`) | 4 (Priority 3–4, everyday) | 4 (notifyd notifications, AudioKit for the ring, Setup's time-zone table) | 3 (alarms while the app is closed need a small service; time-driven behaviour is harder to script in the simulator) | 5 | **16** |
| 3 | Region & Keyboard applet (`keyconf` → region) | 3 (Priority 2, but set once) | 5 (Setup's country / zone tables, `kapi_set_timezone`) | 5 | 4 (touches a Control Panel applet and `system.ini`) | **17** |
| 4 | About / System (`about`) | 3 (Priority 2, informative) | 3 (memory, network, process calls exist) | 3 (the temperature / CPU clock need a new kapi; the simulator shows fake values) | 2 (kapi change, ABI bump) | **11** |
| 5 | Key vault (encrypted secrets, integrated in Wi-Fi, Lisa, Mail, Courier, ftpfs) | 4 (Priority 5, security) | 3 (mbedTLS crypto present) | 1 (crypto design + changes in five apps: several rounds) | 2 (touches many apps' secrets) | **10** |

(Also considered, below these five: a task scheduler — needs a resident daemon and process
starting at times, hard to see in the simulator; a simple HTTP server app — network-only, not
testable in the simulator; more Sokoban levels — too small for a round.)

## The pick: **Quick notes** — folder `autodev/rounds/01-notes/`

A notes app (`user/Apps/notes/`) with a **desktop widget** that can be shown or hidden: notes
written quickly, kept on the card, shown on the wallpaper as sticky notes.

Why:

1. **The user's own priority**: Priority 1 is done and Presentations (Slides) too; *Quick notes
   with a desktop widget that can be shown or hidden* is the highest open item of the user's
   roadmap that does not need a kernel change (About / System does).
2. **Everyday value**: a shopping list, a phone number, a reminder kept in view on the desktop —
   the kind of small app used daily, and a natural companion of Letters, Calendar and the agenda.
3. **Everything to build on is there**: UIKit's text fields and editor widgets, FontKit's text,
   SystemKit (clipboard, file associations, notifications), FileKit for the files, and the
   `agenda` widget as the model of a desktop widget already started by `SD:/etc/autostart`.
4. **Fits one round and is testable on the PC**: a window and a widget drawing plain files;
   `agenda` already has a `shots.sh` scenario, so both the app and the widget can be shown in
   the desktop simulator with sample notes from `tools/tests/desktop_sim/sd/`.
5. **No risk to the system**: no kapi, no kernel, no shared kit ABI change expected — the work
   stays in a new app folder (plus its docs, its autostart line commented / optional, its scenario).

Runner-up for a later round: **Clock** (alarms, timer, stopwatch, world clocks), then the
**Region & Keyboard** applet.
