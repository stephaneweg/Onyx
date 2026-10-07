# AutoDev round 6 — Development: Clock + clockd

Date: 2026-10-07. Inputs: `02-product-analysis.md`, `03-technical-analysis.md` (§5 the plan, §8 the tests, §10 the GUI
plan, *Changes after validation 1 / 2*), `04-ux-design.md`, `05-validation.md` (the three validations and their notes).
Branch `AutoDev`.

## Developer A (steps 0–3)

Steps 0, 1a, 1b, 2 and 3 of 03 §5: the simulator's clock, the two SystemKit functions, the three FileKit functions,
and the Clock's UI-free core. No kernel, kapi, AppKit or UIKit change (`git diff 63580c5d -- kernel user/Kits/appkit
user/Kits/uikit` is empty). Every new file carries the MIT notice.

### What was done, by step

| Step | Files | What |
|---|---|---|
| **0** — the simulator's clock | `tools/tests/desktop_sim/fakekapi.cpp` (header comment + code), `tools/tests/clock/sim_probe.cpp`, `tools/tests/run_clock_test.sh` (created) | `SIM_CLOCK=YYYYMMDDHHMMSS`: `kapi_get_datetime` = that wall time + `(ticks − 1000) / 100` s; `kapi_clock_info` answers (valid, without `SIM_STAT`) with UTC = wall − `SIM_TZ` (default 120) and `tz_minutes = SIM_TZ`, its `utc_us` to the hundredth; `kapi_set_timezone (m)` changes that offset for the run (UTC goes on, the wall time follows). **Unset: everything as before**; the only unconditional change is the log line `sim: set_timezone <m>` (also for a refused value). |
| **1a** — `locale_zone_offset_at` | `user/Kits/systemkit/locale.h`, `locale.inc`, `systemkit.abi` (`76 locale_zone_offset_at`), `tools/docgen/kitdocs.py` (`"systemkit/locale.h"` added to SystemKit's list), `docs/12-SYSTEMKIT.md` (regenerated: the whole of `locale.h` now documented), `docs/06-KITS-GUIDE.md` (a *time zones at an instant* paragraph + example), `tools/tests/clock/zone_test.cpp` | The zone's offset at a UTC instant, the hour of the change counted: EU from the last Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC; US from the 2nd Sunday of March 02:00 local standard to the 1st Sunday of November 02:00 local summer. Own `locale_days_` / `locale_year_` (Hinnant) helpers; floored days (an instant before 1970 works). C-compatible (checked with `gcc -std=c99 -Wall -Wextra`). `locale_zone_summer / _offset` untouched. |
| **1b** — `locale_zone_sync` | `locale.h`, `locale.inc`, `systemkit.abi` (`77 locale_zone_sync`), docs/06 (a sentence), docs/12 | Reads `zone=` **itself** (never `locale_zone ()`'s guess from `timezone=`: validation 1 gap 1) → unknown / absent: 0. `kapi_clock_info` not valid: 0. Offset now = `locale_zone_offset_at (z, UTC minutes)`; equal to `tz_minutes`: 0; else `kapi_set_timezone (m)` and `timezone=` written (`locale_ini_set`), → 1. Called by nobody yet (clockd, step 4, calls it at start and once a minute). |
| **2** — FileKit blocks written | `user/Kits/filekit/kvtext.h`, `kvtext.inc`, `filekit.abi` (`96 fk_kv_block_get`, `97 fk_kv_block_new`, `98 fk_kv_block_set`), `tools/tests/filekit/kvtest.cpp` (`test_blocks`), `tools/tests/run_kvtext_test.sh` (21 exported symbols, was 18), docs/06 (a paragraph + example in FileKit's chapter), `docs/14-FILEKIT.md` (regenerated) | `fk_kv_block_new (kv, name)` → the new block's number (1-based) or −1; `fk_kv_block_get (kv, b, key, def)` (b 0 / out of range / no such key: def; a null kv: def); `fk_kv_block_set (kv, b, key, value)` → replaced in block b, else added at **that block's** end → 0, −1 (no memory, empty key, no block b, null kv). |
| **3** — the core | `user/Apps/clock/clock_proto.h`, `clocktime.{h,cpp}`, `alarms.{h,cpp}`, `tools/tests/clock/alarm_test.cpp`, `clocktime_test.cpp`, `run_clock_test.sh` (grown) | See *APIs as built* below. Neither `alarms.*` nor `clocktime.*` calls the kernel or UIKit (the test script greps their code, comments stripped); `alarms.cpp` reaches the card only through FileKit's `fk_kv_load / fk_kv_save`. |
| docs exports | `docs/exports/{06-KITS-GUIDE,12-SYSTEMKIT,14-FILEKIT}.{docx,pdf}` | `python3 docs/build_docs.py` (pypandoc installed for it); only the three exports whose source changed were committed (the others differed by their build stamps only, reverted). |

### The commits

| Commit | Message |
|---|---|
| `611af573` | AutoDev round 6: step 0 — the simulator's advancing clock (SIM_CLOCK, SIM_TZ, set_timezone logged) |
| `e5f1b07e` | AutoDev round 6: step 1a — SystemKit locale_zone_offset_at (the summer time at an instant) |
| `ace67997` | AutoDev round 6: step 1b — SystemKit locale_zone_sync (the clock follows the summer time) |
| `f71bdf64` | AutoDev round 6: step 2 — FileKit fk_kv_block_new / _get / _set (blocks of the same name written) |
| `1c7784c2` | AutoDev round 6: step 3 — the Clock's core (clocktime, alarms + Ringer, clock_proto.h) |
| `9036c9f4` | AutoDev round 6: docs exports of 06, 12 and 14 regenerated (build_docs.py) |

### The tests run, and their results

| Command | Result |
|---|---|
| `sh tools/tests/run_clock_test.sh` | `clock: the simulator's clock: ok` · `clock: SystemKit's locale_zone_offset_at: ok` · `clock: SystemKit's locale_zone_sync: ok` · `clock: the core (-O1 -fsanitize=undefined …): alarm: 158 checks passed; clocktime: 129 checks passed` · the same at `-O2` · `clock: all checks passed` |
| — its step 0 cases (`sim_probe`) | `clock` (from 20260928123400, 100 × `msleep (1000)` = 10 100 ticks → **12:35:41**, `clock_info` UTC **10:35:41**, tz 120), `tz` (`set_timezone (60)` → wall 01:59:30, UTC unchanged, logged; 900 refused, logged, nothing changed), `sim_tz` (`SIM_TZ=-300`), `noclock` (unset: frozen 12:34:00 after 10 sleeps, no `clock_info`, `set_timezone` changes nothing, logged), `stat` (`SIM_STAT=1` alone: 12:34 UTC tz 0 as before) |
| — step 1a (`zone_test at`, 77 checks) | every instant of 03 §5 step 1a (Brussels 2026-03-29 00:59 → 60, 01:00 → 120; 2026-10-25 00:59 → 120, 01:00 → 60; 2027-03-28 01:00 → 120; 2027-10-31 01:00 → 60; New York 2026-03-08 06:59 → −300, 07:00 → −240; 2026-11-01 05:59 → −240, 06:00 → −300; 2027-03-14 07:00 → −240; Tokyo 540; UTC 0; out of range 0) + London, Los Angeles, and every zone checked hour by hour over 2026–2027 (only its two offsets, exactly 4 changes for a zone with summer time, 0 for Tokyo / UTC); an instant before 1970 |
| — step 1b (`zone_test` sync cases; the shell asserts the `sim: set_timezone` lines and `cmp`s `system.ini`) | `autumn` (zone=Brussels, 2026-10-25 02:59:30 wall, SIM_TZ 120: 0 and nothing logged; a minute later 1, exactly one `sim: set_timezone 60`, `timezone=60`, `zone=` kept; then 0 for 90 more minutes: **no oscillation**), `spring` (Paris 2027-03-28: → 120), `newyork` (2026-11-01: → −300), `wrong` (a summer day with timezone=60: put right to 120), and the **no-change cases, nothing logged, `system.ini` byte-identical**: `right`, `nozone` (`language=en` only), **`guess` (`timezone=120`, no `zone=`, 2026-10-25 00:30 UTC: never Helsinki — gap 1)**, `card` (the card's own `sdcard/etc/system.ini`: none written in the writes), `nowhere` (`zone=Nowhere`), `noclock` (no `SIM_CLOCK`: no real date) |
| `sh tools/tests/run_kvtext_test.sh` | `ok kvtext, ASan build, no files (148 checks)` · `ok kvtext (162 checks)` (was 101 / 115); `kvtext.o` exports 21 `fk_*` |
| `sh tools/tests/run_notes_test.sh` | `notes: all checks passed` (unchanged with SIM_CLOCK unset) |
| Other existing tests that link `fakekapi.cpp`, FileKit's `kvtext` or SystemKit: `run_notes_sim_test`, `run_stickies_sim_test`, `run_circuits_test`, `run_circuits_sim_test`, `run_critters_test`, `run_critters_sim_test`, `run_pinball_test`, `run_pinball_sim_test`, `run_clipboard_test`, `run_archiver_test`, `run_pkg_test`, `run_mail_test`, `run_media_test`, `run_letters_test`, `run_slides_test` | all pass: notes-sim 67, stickies-sim 29, circuits 582 (20 levels three stars), circuits-sim 61, critters 930, critters-sim 25, pinball (fingerprint `73999beb7987e858`, as before), pinball-sim 19, clipboard / archiver / pkg 0 failures, mail / media all good, letters 14 952 checks 0 failed, slides 41 0 failed |
| `sh tools/tests/desktop_sim/run.sh` | `desktop_sim: done` (gallery, gallery-menu) |
| AC-5 (no other picture changed): `shots.sh` on the round's base commit `63580c5d` (a worktree) and on the new tree, the same 54 groups, `SHOTS_PNG` scratch | **134 / 134 PNGs byte-identical** (Setup, Language & Region and the menu bar's clock among them). The clipboard shot differed once when both runs went in parallel (clipd's in-process thread, `SIM_IPC`, raced: an empty ring); rendered again alone (`shots.sh desktop clipboard`) it is identical. |
| Pi target (see below) | the touched kits and the core compile for AArch64 with no warning; libgen on those objects appends exactly the committed `.abi` lines |

**Pre-existing, not touched** (noted for the next developers): a full `sh tools/tests/desktop_sim/shots.sh` stops at
**paint** / **letters**: the host build does not link PrinterKit (`print_dialog`, `print_begin`, … undefined —
round 5 saw it too), so the groups `3dforge letters ledger paint pdf photos sheet slides` cannot be rendered; **media**
needs the Python module `mutagen` (`tools/tests/media/make_library.py`); in a git worktree `circle/` must exist
(`tools/screenshot/render.py` reads `circle/lib/font8x16.cpp`). `shots.sh <names>` works (round 5 used it so).

### The Pi build

`aarch64-none-elf-g++` is **absent** here (as rounds 1–5): `make` / `make stage` are the user's. What was checked
instead, with `clang --target=aarch64` and `user/Makefile`'s flags (`LIB_CXXFLAGS` / `AK_CXXFLAGS`: freestanding,
`-fPIC -fvisibility=hidden`, `-mgeneral-regs-only` or `-mcpu=cortex-a72`, `-Wall -Wextra`, no exceptions / RTTI):
`Kits/systemkit/systemkit.cpp` (`SK_IMPL`: the `extern "C"` exports), `Kits/filekit/kvtext.cpp` (`FK_KV_IMPL`),
`fkcore.cpp`, `fsutil.cpp` (newlib's headers stood in by the host's, for declarations only) and the core
`Apps/clock/alarms.cpp`, `clocktime.cpp` — **no error, no warning**. Then `tools/libgen/libgen.py --nm llvm-nm` over
those AArch64 objects against the **old** `.abi` files (from `63580c5d`) appended `76 locale_zone_offset_at`,
`77 locale_zone_sync` and `96 fk_kv_block_get`, `97 fk_kv_block_new`, `98 fk_kv_block_set` — **identical** to the
committed files (`diff` empty). To be confirmed by the user's `make` (03 R-11).

### Deviations from the plan, and why

1. **`AlarmSet` has more than 03 §3.2's fields**: `bool timer` (a `[timer]` block present), `long long timer_utc` (its
   `utc =`, −1 none — validation 1 note 4 asks it be stored and checked) and `int timer_block`. `timer_end_tick` is a
   `long`, as every tick in the core (see *APIs*).
2. **More functions than §3.2 lists**, so that the Clock and clockd share one implementation: `alarms_init / _free`,
   `alarm_new / _add / _find / _remove`, `alarm_set_label` (40 characters, cut on a UTF-8 character, new lines made
   spaces), `alarm_sound`, `alarm_passed`, `alarm_snooze`, `alarm_stop`, and for the `[timer]`
   **`alarms_timer_state`** (`TMR_NONE / _STALE / _PENDING / _DUE`: §3.3's stale and `utc` rules in one place — the
   Clock's take-back needs PENDING, clockd's ring DUE, validation 3 note 1's drop STALE), `alarms_timer_put / _clear`.
3. **`alarm_next` ignores `on`** (the schedule only, `valid` checked); `alarms_next` and the Ringer skip the off ones.
   A snooze rings only while the alarm is on; `alarm_passed` is false while a snooze is still to come (so a once alarm
   snoozed is not written `on = 0` by a save in between).
4. **The Ringer starts itself**: a zeroed `Ringer` (`Ringer r = {};`) is "not started"; the first `ringer_step` calls
   `ringer_start` (timer_rung = −1), then checks the `[timer]` (rule 5 before rule 1), then returns. The 2-hour
   jump-back threshold is §3.3's `> 120` minutes.
5. **Names**: 03 §3.4's `fmt_sw` / `laps_text` are `clk_fmt_sw` / `sw_laps_text`; the timer and stopwatch are
   structs with free functions (`timer_*`, `sw_*`), not 03's loose fields (`paused_left_cs` is `left_cs` when
   `TM_PAUSED`).
6. **`clk_fmt_diff` writes an ASCII minus** (`-6 h`), as 03 §8.4's `clock-world` log expects; the UI may draw U+2212.
   `+5 h 30`, `""` for the same time (the UI says *Same time*).
7. **alarms.txt as written**: `fk_kv_text` puts a blank line after the comment line (before the first `[alarm]`); a
   weekly alarm is written with `date =` empty; `missed =` only when set; an **invalid `time`** (`25:99`) is written
   back **as it was read**; the part before any header and blocks of other names are kept; alarms beyond the 20th of a
   hand-edited file are **dropped** at the next save (the first 20 are used: 02 §5.1). After a save the set is re-based
   on what was written (`s.doc` = the new document, each `block` its new number), so saving twice gives the same text.
8. **`timer_set`** clamps to 0…86 399 s; `timer_start` with nothing set does nothing; `timer_resume` raises the time
   set if more is left than it (never a negative ring).
9. **Extra tests** beyond 03 §5 / §8: zone_test's spring / New York / wrong-offset / card / no-clock cases and the
   whole-2026–2027 sweep; kvtest's middle block of a read document, a new block after read ones, ESCAPES; alarm_test's
   2-minutes-late ring, a once alarm's `alarm_passed`, the ids renumbered, labels, the 21st refused, a missing file,
   the `[timer]` `utc =` check and its write / clear; clocktime_test's round trips, the New York change night.
10. **docs/04** (*Language & Region*: "while clockd runs, the summer time changes the clock by itself") is **not**
    written yet: it describes clockd, which does not exist before step 4 — left to step 13 with the rest of docs/04.
    docs/06, docs/12 and docs/14 are done.

### What the next developer must know — the APIs as built

**Simulator** (`fakekapi.cpp`): `SIM_CLOCK=YYYYMMDDHHMMSS` (+ `SIM_TZ`, default 120) turns the advancing clock on;
`kapi_clock_info` then answers even without `SIM_STAT`; `kapi_set_timezone` is always logged `sim: set_timezone <m>`.
One clockd step (`msleep (500)`) = 51 ticks = 0.51 s of the simulated clock; `SIM_MBOX`'s `@<ticks>` counts the same
ticks (they start at 1000).

**SystemKit** (`systemkit/systemkit.h`):
```c
int locale_zone_offset_at (int z, long long utc_minutes);	// minutes; 0 for a zone out of range
int locale_zone_sync (void);					// 1 changed (clock + timezone=), 0 not
```
clockd: `locale_zone_sync ()` at its start and once a minute. The World tab: `utc_min = ci.utc_us / 60000000` then
`locale_zone_offset_at (city, utc_min)`.

**FileKit** (`filekit/filekit.h`): `int fk_kv_block_new (fk_kv *, const char *name)`, `const char *fk_kv_block_get
(const fk_kv *, int b, const char *key, const char *def)`, `int fk_kv_block_set (fk_kv *, int b, const char *key, const
char *value)` — the core uses them; the app should not need them directly.

**`clock_proto.h`**: `CLOCK_SERVICE "clock"`, `CLOCKD_SERVICE "clockd"`, `CLOCK_MSG_OPEN 1` (payload: the arguments
and a 0; empty = come forward), `CLOCKD_MSG_RELOAD 2`, `CLOCKD_MSG_QUIT 3`.

**`clocktime.h`** (link `Apps/clock/clocktime.cpp`) — ticks and hundredths are `long`; pass `(long) kapi_get_ticks ()`:
- `long clk_days (y, mo, d)`, `void clk_civil (days, &y, &mo, &d)`, `int clk_wday (days)` (0 Monday … 6 Sunday =
  the bit of `AL_*`), `long clk_minute (y, mo, d, h, mi)` (the **wall minute**: `kapi_get_datetime`'s fields),
  `long clk_day_of (minute)`, `int clk_valid_date`.
- Stamps: `clk_parse_day ("20260929")`, `clk_parse_minute ("202609290710")` (−1 when not one), `clk_fmt_day`,
  `clk_fmt_minute`, `clk_parse_hm ("07:00", &h, &m)`.
- Texts: `clk_fmt_hm` `07:00`, `clk_fmt_hms` `12:34:00`, `clk_fmt_diff` `+7 h` / `-6 h` / `+5 h 30` / `""`,
  `clk_fmt_utc` `UTC+2` / `UTC-3:30` / `UTC`, `clk_fmt_timer (seconds)` `05:00` / `1:00:00`, `clk_fmt_sw (cs)`
  `00:12.34` / `1:00:00.00`.
- `ClkCity clk_city (long long utc_s, int city_off, long here_day, int here_off)` → `hh, mm, ss`, `day` (−1 / 0 / +1
  against here's wall day), `diff` (minutes). Here's offset: `kapi_clock_info`'s `tz_minutes` when valid, else
  `timezone=` (03 §3.4, validation 2 note 4).
- `Timer` (`state` `TM_IDLE / TM_RUNNING / TM_PAUSED`, `total_cs`, `start_tick`, `left_cs`): `timer_set (t, s)`,
  `timer_start (t, now)` (also resumes), `timer_pause`, `timer_reset`, `timer_left_cs`, `timer_shown (t, now)`
  (seconds rounded **up** → `clk_fmt_timer`), `timer_due (t, now)`, `timer_end_tick (t)` (the hand-over's `end =`;
  −1 not running), `timer_resume (t, set_s, end_tick, now)` (the take-back).
- `Stopwatch` (`running`, `start_tick`, `base_cs`, `nlaps`, `laps[999]` = the **totals**): `sw_reset`, `sw_start`,
  `sw_stop`, `sw_total_cs`, `sw_lap` (→ its number, 0 refused), `sw_lap_time (w, i)`, `sw_running_lap_cs`,
  `sw_fastest` / `sw_slowest` (0-based, −1 under 3 laps), `sw_laps_text (w, TR ("Lap"), TR ("Lap time"),
  TR ("Total"), out, cap)` (oldest first, a `\n` after each line). A `Stopwatch` is ~8 KB: keep it static / global.
  Saving it in `config.ini` (`sw_*`, R-1) is the app's (step 10): `start_tick`, `base_cs`, `running` and the laps'
  totals are all it needs.

**`alarms.h`** (link `Apps/clock/alarms.cpp` + `clocktime.cpp`; FileKit: `lib/filekit.imp.a`):
- `AlarmSet s; alarms_init (s);` once, then `alarms_load (s, ALARMS_PATH)` (missing file = 0 alarms) /
  `alarms_parse (s, text)`; `alarms_save (s, ALARMS_PATH)` → 0 / −1; `alarms_free (s)` at the end (it holds the
  `fk_kv` document read). An `AlarmSet` is ~4 KB.
- `Alarm` fields as 03 §3.2 (`id, valid, hh, mm, on, days, date, label[164], sound[12], snooze, missed, block`);
  `date` / `snooze` / `missed` are `clk_days` / wall minutes, −1 none.
- New alarm: `Alarm a; alarm_new (s, a);` (id = next, 07:00, on, once, chimes) … `a.date = alarm_once_day (hh, mm,
  now_min)` for a once alarm … `alarm_add (s, a)` (→ index, −1 at 20: the 21st refused). `alarm_find (s, id)`,
  `alarm_remove (s, id)`, `alarm_set_label (a, text)`, `alarm_sound (token)`, `alarms_days_parse` /
  `alarms_days_text`, `alarm_repeat_kind (days)` → `REP_ONCE / _DAILY / _WEEKDAYS / _WEEKENDS / _CUSTOM`.
- Next-alarm line: `long at = alarms_next (s, now_min, &idx)` (−1 none); `at - now_min` minutes; the day word from
  `clk_day_of (at) - clk_day_of (now_min)`. *Snoozed until* when `s.a[idx].snooze == at`.
- Ring answered: `alarm_snooze (a, now_min, snooze_minutes)` / `alarm_stop (a)` (clears snooze and missed; a once alarm
  off), then `alarms_save` + `CLOCKD_MSG_RELOAD`. A once alarm whose minute is past: `alarm_passed (a, now_min)` → show
  it off and set `on = false` before the next save. Unanswered: `a.missed = minute`.
- `[timer]`: `alarms_timer_state (s, now_tick, now_utc_s or -1)` → `TMR_NONE / _STALE / _PENDING / _DUE`; hand-over
  at exit: `alarms_timer_put (s, timer_end_tick (t), set_s, end_utc or -1, label)` + save + RELOAD; take-back:
  `timer_resume (t, s.timer_set, s.timer_end_tick, now)` then `alarms_timer_clear (s)` + save + RELOAD; a STALE one:
  `alarms_timer_clear` before the next save (validation 3 note 1); `--ring timer`: show *Time's up* with
  `s.timer_set` / `s.timer_label`, then clear + save + RELOAD.
- **clockd's loop**: `Ringer r = {};` (zeroed = not started), every 0.5 s: `now_min = clk_minute (y, mo, d, h, mi)` of
  `kapi_get_datetime`; `trusted = (get_datetime returned 1) && kapi_get_ticks () >= 9000` (`--grace 0`: no guard);
  `now_utc` = `clock_info` valid ? `utc_us / 1000000` : −1; `Due d[8]; n = ringer_step (r, s, now_min, trusted,
  (long) kapi_get_ticks (), now_utc, d, 8)`; for each: `d[i].id == ALARM_TIMER` → `"--ring timer"`, else
  `"--ring <id>"` (`d[i].snooze` tells a snooze ring). A reload (`alarms_load` into the same `AlarmSet`) keeps the
  `Ringer`: nothing is rung twice (`last`, `timer_rung`).
- The core includes `"filekit/filekit.h"` (the kit's one header); built with `-I user/Apps` the tests include
  `"clock/alarms.h"`; the app and clockd, beside it, include `"alarms.h"` / `"../clock/alarms.h"` as the Makefile's
  include paths allow (`user/Makefile` builds with `-I.` from `user/`: `"Apps/clock/alarms.h"` works from anywhere).

`sh tools/tests/run_clock_test.sh` is the place to add the next steps' unit tests; the scripted simulator cases of
03 §8.4 (`clockd-*`, `clock-*`) go into a new `tools/tests/run_clock_sim_test.sh` (step 4 on).

## Developer B (steps 4–8)

Steps 4, 5, 6, 7 and 8 of 03 §5 with their GUI work of 03 §10.2: clockd; the Clock's skeleton; the World tab and its
city picker; the Alarms tab and its editor; the ringing. No kernel, kapi, AppKit, UIKit, SystemKit or FileKit change
(`git diff 02a68379 -- kernel user/Kits` is empty); no change to the core of step 3 (`alarms.*`, `clocktime.*`,
`clock_proto.h`). Every new file carries the MIT notice. Every user-visible word is in `TR ()` with its French in
`sdcard/apps/clock.app/lang/fr.txt` (`python3 tools/lang/check.py clock` → **119 words, 0 missing**; the 39 lines
"not used" are the Timer's, the Stopwatch's and the menus' of steps 9–11, from the mock catalogue).

### What was done, by step

| Step | Files | What |
|---|---|---|
| **4** — clockd | `user/Apps/clockd/main.cpp`, `sdcard/apps/clockd.app/{app.txt,icon.bmp}`, `user/Makefile` (`clockd.elf`: newlib as `clipd`, the core's two `.cpp` after `$<`, `lib/filekit.imp.a lib/systemkit.imp.a`; in `all:` and the order-only `appkit_stubs` line), `sdcard/etc/autostart` (`run clockd` after `run notifyd`, a comment line), `tools/icons/clock_icon.py` (both icons), `tools/tests/run_clock_sim_test.sh` (created) | The `clockd` service (a second one quits); `alarms.txt` read at start, on `CLOCKD_MSG_RELOAD`, and when its signature (size + sum) changed (looked at every 30 s); `CLOCKD_MSG_QUIT`; every 0.5 s the core's `ringer_step` (trusted = a real date **and** ticks ≥ the guard, 9000 by default, `--grace N`); each `Due` → the running Clock sent `CLOCK_MSG_OPEN "--ring <id>"` / `"--ring timer"`, else `lx_launch ("clock", "--ring …")`; neither → a word-free `notify_action ("HH:MM", label, "clock alarms" / "clock timer")`. `locale_zone_sync ()` at start and every 6000 ticks. Log: `clockd: reload (start|message|changed): N alarms[, a timer]`, `clockd: ring <id> HH:MM[ (snoozed)] at HH:MM:SS`, `clockd: ring timer HH:MM at …`, `clockd: quit`. |
| **5** — the skeleton | `user/Apps/clock/main.cpp`, `ui.h` (new), `world.h` / `alarmsview.h` / `ring.h` (stubs, filled by 6–8), `timerview.h` / `swview.h` (**the hooks for Developer C**), `sdcard/apps/clock.app/{app.txt,icon.bmp,lang/fr.txt}`, `user/Makefile` (`FT_APPS += clock`, `FT_EXTRA_clock`, `clock.elf` deps), `tools/tests/desktop_sim/shots.sh` (`build ()`: `clock`'s extra, the FT `case` list, `APPS`) | `ClockRoot` (`Root (w, h, TR ("Clock"))`, size from `config.ini`, `setResizable`, `setMinSize (560, 440)`, the 1-px line under the tab bar), the `SegmentedControl` tab bar (440 × 28, re-centred in `onResized`) over four `Page`s (hidden / shown), the menus (View; Alarm and City from 6–7; `timer_menu` / `sw_menu` hooks), the keys in `onKey` (Ctrl+1…4, Ctrl+Tab / Shift: back; then the tab's `*_key`), one Clock at a time (`CLOCK_MSG_OPEN` + `kapi_raise_app`), the arguments (a tab, `--ring <id>`, `--ring timer`; at start `--ring` = ring-only), `config.ini` through FileKit (`cfg_get / cfg_set / cfg_set_int`, unknown keys kept), `autostart_ensure ("run clockd", "run notifyd", …)`, clockd started by `lx_launch` when `kapi_ipc_lookup ("clockd")` finds none, `uk_lang_init` after `ft_uikit_install`, the window's own loop (`attach` + `step`: a ring-only Clock leaves it by itself), at the end `sound_stop`, `timer_at_exit`, `sw_at_exit`, the tab and size kept (not by a ring-only Clock), `cfg_save`. `ui.h`: the faces (lazy `FtTextFace` cache), the meaning colours with their dark twins, the drawings (bell, slash, globe, sun, moon, hourglass, warning, struck speaker, pin, the chevrons for `setIcon`), `tool_button`, `FootText` (text + a green / red message), **`Spin : NumericUpDown`** (G9), `Page`, `Caption`, **`Veil`** (G3: modal, its controls its children placed from the card's corner, the snapshot of what is under it dimmed 96/256 and frozen, the card recentred in `layout ()`, `show (root, focus)` / `hide ()` with the focus given back to the card under it or `onHidden ()`). |
| **6** — World | `world.h` | `HereCard` (D4: 54-px bold time, the date in the system's language, pin + city · `UTC+h` · *Summer time* — here's offset from `kapi_clock_info`'s `tz_minutes` else `timezone=`, its standard offset = the smaller of `locale_zone_offset_at` in January and July; D7: no zone → amber warning + `Button` *Language & Region…* → `lx_launch ("control", "langconf")`; *Clock not set yet* when `kapi_get_datetime` returns 0), `CityList` (D5: 50-px rows, sun / moon, day word, difference with a true minus sign, *Same time*, 26-px time; scroll bar + wheel + `UkBarDrag`; Up / Down / Home / End / PgUp / PgDn), the empty state (D6), the **Add a City** card (D8: search glyph + `Textbox` filter — case and Latin-1 accents ignored, on the shown and the English name —, `DataGrid` City · Time · UTC sorted by the shown name, Enter / double click / *Add*, Esc / *Cancel*), the footer (*n cities · up to 12*, Add City, Remove, ▲ ▼ disabled at the ends), Ctrl+N, Delete, Ctrl+Up / Down, the City menu; `config.ini` `cities =` written at each change; 12 at most (Add disabled, Ctrl+N refused and logged). Log: `clock: here 12:34:00 UTC+2 summer (Brussels)`, `clock: city Tokyo 19:34 +7 h[ tomorrow|yesterday]`, `clock: cities …`, `clock: add a city (N zones)`, `clock: city refused (12 already)`. |
| **7** — Alarms | `alarmsview.h` | `NextAlarmBar` (D9: *Next alarm: today 14:30 — in 1 h 56 min*, *Snoozed until 12:44 — School*, the struck bell and *No alarm set*; a click selects that alarm), `AlarmList` (D10: 60-px rows sorted by time — invalid last —, greyed when off or a once alarm gone by, the repeat words, *Snoozed until* / *Missed at* / *Invalid* + its red line, `uk_switch_mark`; click selects, a click on the switch toggles, two clicks on a row within 0.4 s edit; Space / Enter / Delete / Ctrl+N; scrolling), the empty state (D11), the **editor** card (D12: `LcdDisplay` + two `Spin`s + *Next: tomorrow 07:00*, the label `Textbox` (160 bytes, `alarm_set_label` cuts at 40 characters), seven day toggles + *Every day* / *Weekdays*, the sounds' `SegmentedControl` + *Test*, Delete (editing) / Cancel / OK; focus on Hours; Enter = OK and Esc = Cancel also over the spin boxes), the footer (*n alarms · n on*, New Alarm / Edit / Delete), *Not saved: the card is full or read-only* in red when `alarms_save` fails, the Alarm menu. Each change: `alarms_write ()` (a once alarm gone by written `on = 0`, a stale `[timer]` dropped — validation 3 note 1) + `CLOCKD_MSG_RELOAD`. Log: `clock: next today 14:30 in 1 h 56 min` / `next snoozed 12:44 (School)` / `next none`, `clock: alarm 1 07:00 School [Weekdays] on[ snoozed 12:44| missed 12:34]`, `clock: alarm 9 invalid`, `clock: editor new|edit <id>|cancelled`, `clock: alarm <id> saved|deleted|turned on|off`, `clock: alarm refused (20 already)`, `clock: alarms saved (n)` / `alarms.txt not saved`. |
| **8** — ringing | `ring.h`, `sounds.h` | `ring_alarm (id)`: `alarms.txt` read again, `notify_action (TR ("Clock"), "07:00 School", "clock alarms")` **first**, the snooze / missed marks cleared in memory, the sound (`sound_play (token, loop)`: `ak_out_open (0, 0)` as a probe — 1 closed at once, then the FM voices; 0 *busy*; −1 *none* — logged `clock: sound unavailable busy|none` / `clock: sound chimes (looped)`), the Alarms tab, the **ring card** (D13 / D14: bell with waves, 50-px time, label, repeat · sound, the amber no-sound line and the card 28 px taller, *Snooze N min* (focused: Enter) / *Stop* (Esc)), `kapi_raise_app`. Snooze → `alarm_snooze (…, config's snooze)`; Stop → `alarm_stop`; 12000 ticks unanswered → *Missed alarm: 07:00 School* notified, `missed =` the ring's minute; each → the card hidden, the sound stopped, saved + RELOAD; ring-only → closes (`clock: ring-only, closing`). An unknown id → nothing (ring-only: closes). A ring while a ring shows replaces it; a ring over the editor stacks (R-8). `sounds.h`: Chimes (C6 E6 G6 C7, an inharmonic 2-op FM bell), Beeps (four 2093-Hz square blips), Marimba (a five-note phrase, a woody FM patch), voices 4–7, stepped from the ticks by `ring_tick`; one round for the editor's *Test* (its *Next:* line turns amber *Sound unavailable*). |

### The commits

| Commit | Message |
|---|---|
| `2eac784d` | AutoDev round 6: step 4 — clockd, the Clock's alarm service |
| `967de35b` | AutoDev round 6: step 5 — the Clock's skeleton (window, tabs, menus, one instance, args, config.ini) |
| `7b9b50da` | AutoDev round 6: step 6 — the World tab (here, the cities, Add a City) |
| `8e58c49a` | AutoDev round 6: step 7 — the Alarms tab and the alarm editor |
| `f53ff7dc` | AutoDev round 6: step 8 — an alarm ringing (--ring, the ring card, the sounds, Snooze / Stop / missed) |

### The tests run, and their results

| Command | Result |
|---|---|
| `sh tools/tests/run_clock_sim_test.sh` (new; builds clockd and the Clock for the PC — their own sources at `-Wall -Wextra`, no warning allowed — and runs each case with a fresh `SIM_WRITES`) | **`clock-sim: all 117 checks passed`** (≈ 30 s) |
| — clockd (step 4, 26) | `clockd-ring` (one `sim: exec SD:apps/clock.app/main --ring 1`, `clockd: ring 1 12:35 at 12:35:00` — within 2 s —, no bubble of its own), `clockd-running` (`sim: send clock type 1 "--ring 1\0"`, no exec), `clockd-reload` (`@2000` RELOAD after a `copy` adding 12:36: 12:35 once, 12:36 once), `clockd-changed` (the same with no message: the 30-s signature), `clockd-disabled` (6000 steps, nothing), `clockd-late` (09:00 start, 07:00 alarm: nothing), `clockd-guard` (no `--grace 0`: nothing in the first 90 s), `clockd-sync` (2026-10-25 02:59:30, zone=Brussels: one `sim: set_timezone 60`, `timezone=60` written), `clockd-sync-nozone` (the card's `system.ini`: nothing set, nothing written), `clockd-timer` (`[timer] end = 1300`: one `--ring timer` in 200 steps; with a RELOAD at `@3000`: still one), `clockd-fallback` (no `clock.app/main`: `sim: send notify type 1 "12:35\0Tea\0clock alarms\0"`), `clockd-quit` |
| — the skeleton (step 5, 14) | `clock-args` (`clock stopwatch` → `clock: tab stopwatch`, the tab kept in `config.ini`; `clock` alone → the last tab), `clock-one` (`SIM_SERVICES=clock`: `sim: send clock type 1 "alarms\0"`, `sim: raise_app clock`, no window), `clock-clockd` (`sim: launch clockd`; not when it runs), `clock-keys` (Ctrl+2/3/4/1, Ctrl+Tab, and `CLOCK_MSG_OPEN "timer"` from `SIM_MBOX` → the tab, raised), `clock-lang` (`check.py clock`: 0 missing) |
| — World (step 6, 19) | `clock-world` (AC-6/7: `here 12:34:00 UTC+2 summer (Brussels)`, Tokyo 19:34 +7 h, New York 06:34 −6 h, London 11:34 −1 h), 23:30 → *Tokyo 06:30 tomorrow*, 01:00 → *Los Angeles 16:00 yesterday* (AC-8 on the window, `SIM_CLOCK`), the minute's rows printed again 60 s later (19:35), `clock-cities` (AC-9: the picker lists the 20 not chosen; Enter adds Amsterdam at the end; Ctrl+Up; Delete; `cities = Tokyo,New York,London`; restarted, the same list), the filter (`dub` + Enter → Dublin), the 13th refused, `clock-nozone` (AC-10: *Time zone not set*, its button → `sim: exec SD:apps/control.app/main langconf`, the cities with their UTC offset only) |
| — Alarms (step 7, 31) | `clock-alarm-new` (AC-11/15: Ctrl+N, the label, *Weekdays*, Enter → one block `time = 07:00`, `label = School`, `on = 1`, `days = mon tue wed thu fri`, `sound = chimes`, `sim: send clockd type 2`; the same tokens with `language=fr`, shown *En semaine*), AC-13 (10:00 made at 12:34 → `date = 20260929`; 13:00 → `20260928`), `clock-next` (AC-12: *today 14:30 in 1 h 56 min*; *tomorrow 07:00 in 18 h 26 min*; Space twice → *none*, `on = 0` written twice, RELOAD each time; the rows sorted by time), the repeat words (AC-14: *Mon, Wed, Fri*, *Every day*, *Alarm*; French *lun., mer., ven.*, *Tous les jours*, *Alarme*), Delete, `clock-21`, `clock-invalid` (AC-16: *Invalid*, `colour = red` kept, `25:99` written back as read), `clock-edit-keys` (a) Enter over Hours after typing 8 → `time = 08:00`; (b) Esc → `alarms.txt` byte-identical), the editor's Delete, a read-only card (`SIM_ROFS`) → *not saved* |
| — ringing (step 8, 27) | `clock-ring` (AC-24/25: `sim: send notify type 1 "Clock\007:00 School\0clock alarms\0"`, the ring, raised, Alarms tab, Esc = Stop, a weekly alarm stays on, RELOAD; French: `Horloge`), `clock-ring-only` (G2: `clock: ring-only, closing` before the script's end), Stop on the once Medicine → `on = 0` and shown off, `clock-snooze` (Enter → `snooze = 202609281244`, *next snoozed 12:44 (School)*, the row), the snooze's ring answered → `snooze =` cleared, `clock-nosound` (no `SIM_SOUND` → `clock: sound unavailable none`, the ring still comes; `SIM_SOUND=1` → `sim: sound acquired`, `clock: sound chimes (looped)`), the editor's Test (none → unavailable; with sound: one round, not looped), `clock-missed` (6100 steps → `Missed alarm: 07:00 School` notified, `missed = 202609281234`, exactly two bubbles), a ring by message to a running Clock (stays after Stop), `clock-veil-stack` (R-8: a ring over the editor, Esc stops it, Esc then cancels the editor), an unknown id |
| `sh tools/tests/run_clock_test.sh` | `clock: all checks passed` (alarm 158, clocktime 129, at `-O1` UBSan and `-O2` — unchanged) |
| `SHOTS_TMP=… SHOTS_PNG=<scratch> sh tools/tests/desktop_sim/shots.sh clock` | builds every app with the new `clock` entry (the Clock built), renders the menu bar's `clock.png`: **pixel-identical** to `screenshots/clock.png` (only the PNG encoding differs). The other `ld` errors of that run are the known PrinterKit ones (letters, ledger, paint… — Developer A's note) |
| Looked at (scratch PNGs, EN and FR, compared with `mockups/clk-*.png`) | World, World FR, no zone FR, Add a City EN / FR (filtered *at* → *Athènes*), Alarms, Alarms FR, the editor EN / FR (*Écouter* fits), New Alarm, the ring EN / FR (no sound), the ring over the editor, the editor's *Sound unavailable*, a 760 × 520 window (anchors, scroll bar, centred card): all match the mock-ups; every French word fits. The test leaves its dumps as PNGs in `/tmp/onyx_clock_sim/*.png` (world, cities, nozone, new, alarms, alarms-fr, ring, ring-fr, edit-nosound, stack, args). |
| AArch64 (`clang++ --target=aarch64-none-elf` with `NL_CXXFLAGS` + `-Wall -Wextra`, the host's headers for declarations) | `Apps/clockd/main.cpp`, `Apps/clock/main.cpp`, `alarms.cpp`, `clocktime.cpp`: **no error, no warning of their own** (only FontKit's `fonts.h` unused-function warnings, as every FT app); every undefined symbol is an AppKit / SystemKit / FileKit / AudioKit export, FreeType's or libc's. The Pi `make` is the user's (no `aarch64-none-elf` here). |

### Deviations from the plan, and why

1. **The overlays are one object a kind, kept** (`g_cityVeil`, `g_edit`, `g_ring`): `show ()` adds it (or brings it to the
   front), `hide ()` removes it — never deleted, because a card closes from its own buttons' callbacks (a `delete`
   there would free the widget UIKit is still in, and the root's `prevHandled` keeps a pointer). A second ring while
   one shows **replaces** it (logged `ring B replaces ring A`).
2. **Log lines**: clockd adds the wall second (`… at 12:35:00`) so the "within 2 s" of AC-17/24 is asserted; the next
   line is printed with English tokens (`next today 14:30 in 1 h 56 min`), the rows with the **shown** repeat words
   (`[En semaine]` in French: AC-14 checked in both languages); the no-zone line prints its button's centre (the test
   clicks it).
3. **Add a City** lists every zone not chosen — here's zone included (02 #7; the mock left Brussels out); the filter
   matches the shown and the English name.
4. **The ring card shows the alarm's own time** (`07:00`), also for a snooze's ring; `ring_alarm` clears `snooze` and
   `missed` in memory, written at the answer (Snooze / Stop / missed).
5. **Two French words more than the mock catalogue's plural forms**: `1 city · up to 12`, `1 alarm · %d on`; plus
   `Clock not set yet`, `No city matches`, `Not saved: the card is full or read-only`.
6. **clockd's fallback bubble** uses the action `clock timer` for a timer (`clock alarms` for an alarm).
7. **`clockd-one`** (a second clockd quits) is not testable in the simulator (`kapi_ipc_register` gives 1 for any name):
   Pi only. clockd's `kapi_get_args` reads `--grace N` anywhere in its arguments.
8. **The tab of `clock` alone** is the one of the last *user* session: a ring-only Clock does not overwrite `tab`,
   `width`, `height`.
9. **The sounds' notes and FM patches** (`sounds.h`) are a first take, to be judged by ear on the Pi.
10. The Timer and Stopwatch tabs are **empty pages** until steps 9–10 (the Timer's shows its footer's keys line).

### For Developer C (steps 9–13, S1–S3)

- **The hooks** are in `timerview.h` / `swview.h` (stubs now), each called by `main.cpp`: `timer_build (Page *)` /
  `sw_build` (the tab's widgets; the page is `w × (h − 48)`, the footer at `page->height − FOOT + 8`), `timer_shown` /
  `sw_shown` (the focus), `timer_tick` / `sw_tick` (every turn; `g_now` read just before), `timer_key (k, ctrl)` /
  `sw_key` (the keys no focused widget took — Ctrl+1…4 / Ctrl+Tab are handled before), `timer_menu (Menu &)` /
  `sw_menu` (04 §6; bind only Copy Laps, `UK_CTRL ('C')`), `timer_resume_handed ()` (at start, before the arguments: a
  `[timer]` PENDING taken back → open the Timer tab — `tab_show (TAB_TIMER)`), `timer_at_exit ()` / `sw_at_exit ()`
  (before `cfg_save`: the hand-over, `sw_*` keys with `cfg_set` / `cfg_set_int`; `g_cfg.timer` is written by
  `cfg_save`), and **`ring_timer ()`** (called by `open_args` for `--ring timer`, at start with `g_ringOnly` already
  true: G2 — clear `g_ringOnly` on *+1 min*, validation 3 note 4; set `g_closeNow = true` to close a ring-only Clock).
- **Shared pieces**: `Spin` (the Timer's three spin boxes), `tool_button`, `FootText` (`set` / `say (text, bad)`),
  `Caption`, `Veil` (`place (w, x, y)` from the card's corner, `drawCard`, `onKey`, `onHidden`; `show (g_root, focus)` /
  `hide ()`; keep one object), `face (px)`, `draw_hourglass`, `dim_on`, `GREEN () / RED () / AMBER ()`, `DOT`, `cat`;
  `sound_play (token, loop)` / `sound_stop ()` (one sound at a time: the newest wins), `say ()` (`clock: …` log),
  `g_now` (`tick`, `minute`, `utc`, `real`), `g_al` + `alarms_write ()` (saves and sends RELOAD; it also drops a STALE
  `[timer]`), `g_alPrint = true` + `alarms_changed ()` after changing `g_al` behind the Alarms tab's back.
- **The Time's up card over the editor** (R-8) works as the ring's does (`clock-veil-stack` is the pattern). `ring_tick`
  steps `sound_step ()` every turn — a Time's up loop needs nothing more.
- **Tests**: add the cases of 03 §8.4 / §10.3 for steps 9–11 to `tools/tests/run_clock_sim_test.sh` (helpers `seed`,
  `fixture` — the three alarms of 03 §8.3 —, `alarms`, `config`, `run PROGRAM CASE SCRIPT VAR=…`, `kv CASE FILE BLOCK KEY`,
  `logs`, `nolog`, `count`, `counts`, `waits`, `png`); still open there: `clock-edit-keys` (c) (a Timer spin box,
  Ctrl+2), `clock-keys`' Space / L, `clock-timer*`, `clock-timesup*`, `clock-sw`, `clock-quit-hand`. The Pi-only checks
  of steps 4–8: AC-28–31, `clockd-one`, the sounds, R-7's latency.
- **Screenshots (step 12)**: the fixtures are in `tools/tests/desktop_sim/clock/` (`alarms.txt`, `config.ini`; add
  `config-3s.ini`); `shots.sh` already builds `clock`; the `want clock` block's new lines go after the menu bar's (G5,
  §10.4). The positions in 03 §10.4 for the World / Alarms / editor / ring shots hold (the editor opens with Enter on the
  focused list; Ctrl+N opens the city picker).
- **Docs / package (step 13)**: nothing of docs/04, docs/03, `packages.ini`, `IDEAS.md` was written by this step group.
  `sdcard_lite/etc/autostart` was not touched (the publish tool's). The dock's label stays *Clock* (validation 1 note 7).
