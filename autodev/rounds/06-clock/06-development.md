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

## Developer C (steps 9–11)

Steps 9, 10 and 11 of 03 §5 with their GUI work of 03 §10.2: the Timer tab and *Time's up*, the timer handed to clockd
and taken back; the Stopwatch tab; the keys of 04 §7 and the hand-over at every exit (no question, G1). No kernel,
kapi, AppKit, UIKit, SystemKit or FileKit change (`git diff b9feaead -- kernel user/Kits` is empty); no change to the
core of step 3 (`alarms.*`, `clocktime.*`, `clock_proto.h`) nor to clockd. Every user-visible word is in `TR ()` with
its French in `sdcard/apps/clock.app/lang/fr.txt` (`python3 tools/lang/check.py clock` → **158 words, 0 missing**; the
one line "not used", *Alarms and timers…*, is S1's, the menu bar's).

### What was done, by step

| Step | Files | What |
|---|---|---|
| **9** — Timer, *Time's up*, the hand-over | `user/Apps/clock/timerview.h` (the hooks filled), `ring.h` (`ring_busy ()`, one line) | **`TimerRing`** (D17, the mock's drawing lifted: the track and the time left as a `VPath::arc` from 12 o'clock with a white dot at its moving end; *of 05:00*, the time Bold 46 px, *Ends at 12:37* with a small bell; paused: the arc mixed toward the face and *Paused*; idle: the full arc, the time set, no *of* line; redrawn only when the second shown, the half-degree or the state changes). The column: *Duration* — three **`Spin`s** (hours / minutes / seconds, locked while it runs or is paused; typed digits taken when Space starts it), *Presets* — five toggle `ToolButton`s (lit when the duration is one; a click sets; two clicks on one within 40 ticks set and start; refused, logged, while it runs), the start button (filled, 42 px: *Start* / *Pause* / *Resume*), *Reset* (36 px, disabled while it runs and when idle at the time set). The ring + column are one 534-px block centred by a small **`Centred`** container (its `layout ()` shifts its children when the window is resized). **`TimesUpVeil`** (D18: hourglass, *Time's up* Bold 28 px, *The 05:00 timer ended at 12:39*, `+1 min` / `Stop` (focused), the hint; Enter / Esc = Stop, `+` or `=` = one more minute): the notification *Clock — Timer — 05:00 done* (action `clock timer`) first, the Chimes looped, the window raised, the Timer tab shown — **unless another card shows** (the editor, the city picker: R-8, the card stacks over it); Stop → back to the time set (config's `timer =`); +1 min → a fresh 60-s timer (`timer =` unchanged); 12 000 ticks unanswered → the sound stops, the card goes. `timer =` is the duration last chosen. **At every exit** (`timer_at_exit`): a running timer → `alarms_timer_put` (its end in ticks, its time set, its end in UTC when `kapi_clock_info` is valid) + `alarms_write` (RELOAD); a **paused** one → `timer_left` / `timer_of` in `config.ini`, back paused at the next start. **At the start** (`timer_resume_handed`): a `[timer]` PENDING → `timer_resume`, the block removed, saved + RELOAD, `clock: timer running 02:00`, the Timer tab; DUE → left to clockd; STALE → dropped at the next save (Developer B's `alarms_write`). **`ring_timer ()`** (`--ring timer`, argument or message): `alarms.txt` read again, no `[timer]` → nothing (ring-only: closes); else the block removed + saved + RELOAD, then *Time's up* with its `set`; a ring-only Clock closes after Stop / the timeout, **stays after +1 min** (`g_ringOnly` cleared: validation 3 note 4). |
| **10** — Stopwatch | `user/Apps/clock/swview.h` (the hooks filled), `fr.txt` (`1 lap`) | **`StopwatchFace`** (D19: `MM:SS` Bold 60 px and `.hh` Bold 36 px a shade lighter on one baseline, `H:MM:SS` from an hour; under it *Lap 4 · 00:04.45*, at zero the keys; redrawn when the hundredths change). *Lap* (L, while it runs) / *Reset* (R, stopped; disabled at zero) and *Start* / *Stop* (Space, filled), 164 × 38, centred (`Centred`). The laps' **`DataGrid`** (`stripes`, *Lap · Lap time · Total* + an untitled fourth column that follows the window's width), newest first, a `cellDraw`: from 3 laps the fastest lap time **green bold** with a green pill *▲ Fastest*, the slowest **red bold** with *▼ Slowest*; empty: *No laps yet: Lap (L) while it runs*. Footer: *No laps* / *1 lap* / *3 laps*, **Copy Laps** (`WKT_COPY`, disabled with no lap; the menu's only bound key, `UK_CTRL ('C')`, acting on the Stopwatch tab only): `sw_laps_text` with `TR ("Lap")`, `TR ("Lap time")`, `TR ("Total")` → SystemKit's `clip_set_text_n`, *Laps copied* (green check) in the footer until the next action. **Kept at every exit** (`sw_at_exit`): `sw_run`, `sw_start` (ticks), `sw_base`, `sw_total`, `sw_tick` (the close's tick), `sw_utc` (the close's UTC, −1 unknown), `sw_laps` (the totals, comma-separated). **At the start** (`sw_restore`): running and the same boot (the ticks went on since the close, and the UTC agrees within 5 s when known both times) → it goes on from `sw_start`; another boot with the UTC known both times → `sw_total` + the UTC difference, running; else → stopped at `sw_total`. The menu *Stopwatch* (Start / Stop, Lap, Reset, —, Copy Laps). |
| **11** — keys, the hand-over at every exit | `main.cpp` (its header: the keys, config.ini's keys), `timerview.h` (`=` as `+`) | Space / R on the Timer, Space / L / R on the Stopwatch (`timer_key` / `sw_key`: the keys no focused widget took; over a focused `Spin` they pass through, Ctrl+1…4 too — `clock-edit-keys` (c)); the menus *Timer* (Start / Pause, Reset, —, 1 minute, 3 / 5 / 10 / 15 minutes) and *Stopwatch*, their items showing their tab first. **No question on closing** (G1): Ctrl+Q (`MENU_QUIT` in `Menu::shortcut`), *Clock ▸ Quit* and the close box all end the window's loop, after which `main` runs `timer_at_exit` and `sw_at_exit` (also for a ring-only Clock closing itself). |

### The commits

| Commit | Message |
|---|---|
| `bbfa697b` | AutoDev round 6: step 9 — the Timer tab, Time's up, the timer handed to clockd and taken back |
| `5d3a94b2` | AutoDev round 6: step 10 — the Stopwatch tab (laps, fastest / slowest, Copy Laps, kept at the close) |
| `f51fcd02` | AutoDev round 6: step 11 — the keys of 04 §7 and the hand-over at every exit |

### The tests run, and their results

| Command | Result |
|---|---|
| `sh tools/tests/run_clock_sim_test.sh` | **`clock-sim: all 188 checks passed`** (was 117; ≈ 16 s). The Clock's sources still build with `-Wall -Wextra` and no warning. |
| — Timer (step 9, 46) | `clock-timer` (AC-32: the 5 min preset clicked at (421, 193) → `timer set 05:00`; Space → `started 05:00 (the duration locked)`; the minutes' up arrow while it runs changes nothing; R refused; Pause after ~1 s → `paused 04:59`; R → `reset 05:00`; `timer = 300` in config.ini), the preset's double click (3 min set **and** started; a preset refused while it runs; `timer = 180`), `2` typed in the minutes + Space → `started 02:00` (`timer = 120`), `clock-timesup` (AC-33: a 3-s timer → `sim: send notify type 1 "Clock\0Timer — 00:03 done\0clock timer\0"`, raised; `+` → `started 01:00 (one more minute)` then Space pauses it at 01:00; Esc = Stop → back to 00:03; French: `Horloge\0Minuteur — 00:03 terminé`), unanswered 6 100 steps → `time's up unanswered`, `clock-timer-close` (AC-35, R-1: `quit` → `timer handed to clockd (05:00 left)`, a 4th block `[timer]` with `end` > 30 000, `set = 300` and — `SIM_CLOCK` — `utc =`, the three alarms kept, `sim: send clockd type 2`, `timer = 300`), a paused timer kept and back paused (`timer paused 04:59 (kept)`, then `resumed 04:59`; no `alarms.txt` written), `clock-timer-resume` (`end = 13000` → `timer running 02:00`, the block gone, RELOAD, `tab timer` with no argument; `end = 500` → the file byte-identical, nothing sent), a stale `[timer]` dropped at the next save, `clock-timesup-hand` (`--ring timer` + a past `[timer]` → `time's up (05:00`, the notification, no `[timer]` left and the three alarms kept, RELOAD, `ring-only, closing` after Stop; with `+` instead: it stays and hands the new minute over at `quit`, `set = 60`; by message to a running Clock: it stays after Stop), `clock-veil-stack` (timer) (the editor opened on *School* while a 3-s timer runs → *Time's up* over it, the tab left on Alarms; Esc stops it, Esc then cancels the editor), `clock-edit-keys` (c) (the minutes' field focused, Ctrl+2 → Alarms; the duration still 05:00) |
| — Stopwatch (step 10, 15) | `clock-sw` (AC-37 / 38: Space, laps after 600 / 590 / 640 steps, R refused while running, Space → the rows **newest first** `3 2 1`, `lap 2 00:11.84 00:23.86 fastest`, `lap 3 00:12.82 00:36.68 slowest`, `lap 1 00:12.02 00:12.02`; Ctrl+C (`key 0x03`) → `laps copied (3)` and the clipboard file (`SIM_CLIPFILE`) **byte-equal** to `Lap\tLap time\tTotal\n1\t00:12.02\t00:12.02\n…`; R → reset), French header `Tour\tTemps du tour\tTotal`, Ctrl+C off the tab copies nothing, kept across a close: the same boot (`stopwatch kept 00:05.xx running (1 laps)`), another boot with the UTC known (`01:10.xx running`: 10 s + 60 s), another boot without (`00:10.00`, stopped), the close writing `sw_run = 1`, `sw_start`, `sw_laps` |
| — keys, hand-over (step 11, 10) | `clock-keys` (Space on the Timer starts then pauses; Ctrl+4; Space / L / Space / R on the Stopwatch; Ctrl+N inert on both tabs; `=` on Time's up = one more minute), `clock-quit-hand` (the timer and the stopwatch running, Ctrl+Q → `sim: menu_command -1`, nothing asked, then `quit` (the simulator's `menu_command` only logs: 03 §10.3) → the program ends before the script's end, `sw_run = 1` and `sw_start` > 1000 in config.ini, `[timer]` `set = 300` + RELOAD; reopened: `timer running 0…` and the block gone, the stopwatch shown) |
| `sh tools/tests/run_clock_test.sh` | `clock: all checks passed` (alarm 158, clocktime 129, at `-O1` UBSan and `-O2` — unchanged: no core change) |
| `python3 tools/lang/check.py clock` | `158 words, 0 missing, 1 not used` (*Alarms and timers…*: S1) |
| `SHOTS_TMP=… SHOTS_PNG=<scratch> sh tools/tests/desktop_sim/shots.sh clock` | builds every app with the new Clock (the known PrinterKit `ld` errors aside), `shots: done`. The menu bar's `clock.png` differs from the committed one by **the *Open Calendar* button's frame only** (the committed picture shows it focused / pointed, today's not): the menu bar is not touched by this round (no file of it changed) — to look at in step 12. |
| Looked at (scratch PNGs, compared with `mockups/clk-timer*.png`, `clk-timesup*.png`, `clk-stopwatch*.png`) | Timer running (04:58 of 05:00, *Ends at 12:38*, 5 min lit, Pause, Reset disabled), paused, Time's up, the timer taken back (02:00 of 05:00), Time's up over the editor, Stopwatch running / stopped with three laps / *Laps copied* / zero (**identical to `clk-stopwatch-zero.png`**), and in **French** the Timer (*Remettre à zéro* fits, the footer line whole), *Temps écoulé* (*Le minuteur de 00:03 s'est terminé à 12:34*, *Entrée ou Échap : arrêter · + : une minute de plus* fit), the Stopwatch (*Temps du tour*, *Plus lent*, *Plus rapide*, *Copier les tours* fit); a 760 × 520 window (the block centred, the grid's last column filling). All match the mock-ups (the block 1 px left of the mock's: (560 − 534) / 2 = 13). The test leaves `timer-paused`, `timesup`, `timesup-fr`, `timer-resume`, `timer-stack`, `stopwatch`, `stopwatch-copied`, `stopwatch-zero`, `stopwatch-fr` PNGs in `/tmp/onyx_clock_sim/`. |
| AArch64 (`clang++ --target=aarch64-none-elf`, `NL_CXXFLAGS` + `-Wall -Wextra`, the host's headers for declarations) | `Apps/clock/main.cpp` with the three views: no error, **no warning of its own** (only FontKit's `fonts.h` unused functions, as every FT app); the new undefined symbol is SystemKit's `clip_set_text_n`. The Pi `make` is the user's. |

### Deviations from the plan, and why

1. **A paused timer is kept** (`timer_left`, `timer_of` in `config.ini`) and comes back paused — R-1 speaks of the
   running one only; a paused one cannot ring, so it stays out of `alarms.txt`, and nothing is lost on closing (G1's
   spirit).
2. **The stopwatch across a reboot**: R-1 says "within the same boot"; when the real time was known at the close and
   is now (`sw_utc`), it goes on by the UTC difference; else it comes back **stopped** at the time it had (never a
   wrong running time). The same-boot test is "the ticks went on since the close" (+ the UTC agreeing within 5 s).
3. **Presets while the timer runs or is paused are refused** (logged), not a reset: one click must not lose a running
   timer. The spins are locked then as D17 says.
4. ***Time's up* does not change the tab when another card shows** (the editor, the city picker): R-8's stack keeps the
   user's place; with no card it shows the Timer tab (D18).
5. **Unanswered 2 minutes**: the sound stops and the card goes ("as D13"), with **no second bubble** — the *done*
   notification was already sent; there is no `missed` mark for a timer.
6. **+1 min does not change `timer =`** (the duration chosen); *Stop* after it goes back to `timer =`. *Time's up*
   rings the **Chimes** looped (the alarm sounds' first; no choice in the UI).
7. **`=`** is taken as **`+`** on *Time's up* (the `+` key without Shift).
8. **The close box in the simulator** is `quit` (`winclose` only acts on a program's second windows); Ctrl+Q's
   `menu_command -1` is logged and followed by `quit` (03 §10.3).
9. **The ticks restart at 1000 in every simulated process**, so a `[timer]` handed over in one run and taken back in
   the next shows a second more (`05:01` for a 5-min timer taken back at once): `timer_resume` raises the time set
   rather than ring early — a simulator artefact (on the Pi the ticks go on).
10. **Small additions beside the app**: `Centred` (a container keeping a block of children centred: the Timer's ring +
    column, the Stopwatch's two buttons), `ring_busy ()` in `ring.h` (ending *Time's up* leaves an alarm's sound
    alone). One French line more: `1 lap	1 tour`.
11. **Log lines** (read by the tests): `clock: timer set|started|resumed|paused|reset MM:SS`, `timer reset refused
    (running)`, `preset refused (the timer runs)`, `time's up (05:00, ended at 12:39)`, `time's up stopped|unanswered`,
    `timer started 01:00 (one more minute)`, `timer handed to clockd (MM:SS left)`, `timer running MM:SS` (taken
    back), `timer paused MM:SS (kept)`, `ring timer: no timer`; `stopwatch started|stopped …`, `lap n LAP TOTAL[
    fastest|slowest]` (at each lap, and the rows newest first at Stop — ten at most), `stopwatch reset[ refused
    (running)]`, `laps copied (n)`, `stopwatch kept … [running] (n laps)`, `stopwatch kept at the close …`.

### For Developer D (steps 12–13, S1–S3)

- **Screenshots (step 12)**: §10.4's positions hold — `down 421 193` is the *5 min* preset's centre; `clock-timesup`
  needs the fixture **`tools/tests/desktop_sim/clock/config-3s.ini`** (`[clock]` + `timer = 3`; not created yet), 170
  waits after Space show the card; the stopwatch script of §8.3 gives laps 12.02 / 11.82 / 12.82 (lap 2 fastest, lap 3
  slowest, as the mock). The PNGs of `run_clock_sim_test.sh` in `/tmp/onyx_clock_sim/` show what to expect. Check the
  menu bar's `clock.png` (the *Open Calendar* button's frame differs from the committed picture; not this round's code).
- **docs/04 (step 13)**: the Timer (presets: a double click starts; Space / R; the duration kept; *Time's up*: Stop,
  +1 min (`+` / `=`), the notification, 2 minutes; closing hands it to clockd, which rings it — *Time's up* then comes
  by itself; reopened before its end, the Clock shows it running; a paused timer stays paused), the Stopwatch (Space /
  L / R, laps newest first, fastest / slowest marked, *Copy Laps* / Ctrl+C: tab-separated text; kept when closed, and
  across a restart when the clock was set), the files: `config.ini`'s `timer_left`, `timer_of`, `sw_*` keys,
  `alarms.txt`'s `[timer]`. Pi-only checks of steps 9–11: AC-36 (a 10-minute timer within 1 s), the Chimes of *Time's
  up*, the hand-over across a real close (clockd → `clock --ring timer`).
- **S1** (the menu bar's bell): `Alarms and timers…` is already in `fr.txt` (the only "not used" line).

## Developer D (steps 12–13, S1–S3)

Step 12 (the screenshots), step 13 (the docs, the package declared, IDEAS) and the three *should* items S1 (the menu
bar's bell and *Alarms and timers…*), S2 (the alarms missed while the Pi was off) and S3 (the analogue face), each
with its tests and pictures, in English and French. No kernel, kapi or AppKit change (`git diff origin/main --
kernel/ user/Kits/appkit/` is **empty**); no kit change (UIKit, SystemKit, FileKit untouched by this step group).
Every new or changed file keeps its MIT notice. Nothing published (no `publish.sh`, no onyx-packages).

### What was done, by step

| Step | Files | What |
|---|---|---|
| **12** — screenshots | `tools/tests/desktop_sim/shots.sh` (the `want clock` block), `tools/tests/desktop_sim/clock/config-3s.ini` (new: `timer = 3`), `screenshots/clock-*.png` | The block of 03 §10.4 after the menu bar's `clock.png`: `kfix` (the fixtures copied into the writes before each run), `KS="SIM_SERVICES=notify,clockd"`, the eight English lines (`clock-world`, `-cities`, `-alarms`, `-edit`, `-ring`, `-timer`, `-timesup`, `-stopwatch`) and the eight explicit `-fr` lines (`lang fr` … `lang "$SHOTS_LANG"`), `rm -rf "$KQ"` at the end (the later groups — `desktop`, `milk` — see no alarms). Every picture looked at, English and French: they match the mock-ups, every French word fits. One French line reworded on the way: the Timer's footer *il sonne même Horloge fermée* → *il sonne même l'Horloge fermée* (it still fits: it ends at x 545 of 560). |
| **S2** — missed while off | `user/Apps/clock/alarms.{h,cpp}` (`alarms_missed_since`), `user/Apps/clockd/main.cpp`, `user/Apps/clock/main.cpp`, `ring.h` (`ring_missed`), `clock_proto.h` (comments), `tools/tests/clock/alarm_test.cpp` (`t20_missed_since`), `tools/tests/run_clock_sim_test.sh` | `alarms_missed_since (s, since, upto, Due *, max)`: the **once** alarms, on and valid, whose ring (their minute, or their snooze when it is the later one) lies in `[since, upto]`, with no `missed =` at or after it and no snooze still to come. clockd looks **once, at the first step whose date is trusted** (a real date and the 90-s guard past): `[now − 12 h, the Ringer's last minute]` (so nothing the Ringer still rings is said missed), and hands **all of them in one** `--missed <id> <YYYYMMDDHHMM> …` to the Clock (the running one by `CLOCK_MSG_OPEN`, else `lx_launch`: one start only — a second start would have found the first one ending); neither → word-free bubbles (`07:00` / the label), as its ring fallback. The Clock (`clock --missed …`, before any window; or the running one, not raised): each one *Missed alarm: 07:00 School* (`Horloge — Alarme manquée : …` in French) notified, `missed =` its ring, the snooze cleared, `alarms_write` (a once alarm gone by written off, clockd told), no sound, no window; told twice, it says it once (`missed 1: nothing to say`); an alarm its ring card shows is left alone. Logged `clockd: missed 1 07:00[ (snoozed)]`, `clock: missed 1 07:00 School (while off)`. The Clock's argument buffer went from 160 to 400 bytes (20 pairs). |
| **S3** — analogue face | `user/Apps/clock/world.h` (`draw_face`, `HereCard::analogueCols / drawAnalogue`, `world_face`, `world_view_menu`), `main.cpp` (`Config::analogue`, `face =` read and written, the View menu), `sdcard/apps/clock.app/lang/fr.txt` (*Horloge analogique*, *Horloge numérique*), `shots.sh` (`clock-analogue`, `clock-analogue-fr`), `run_clock_sim_test.sh` | **View ▸ Analogue Clock / Digital Clock** (the World tab shown; `face = analogue \| digital` in `config.ini`, written at once). The face (VPath, 1/16 px): the field's colour, an accent rim, 60 minute ticks and 12 hour ones (the quarters longer), the hour and minute hands, the second hand in the accent with its tail, the pin; radius 55 in the 122-px card, the time (Bold 34), the date and the zone's line in a column right of it, the whole block centred; no zone: the warning and *Language & Region…* in the column (the block widens to hold them). Redrawn every second as the digital one. |
| **S1** — the menu bar | `user/Apps/menubar/main.cpp`, `sdcard/apps/menubar.app/lang/fr.txt` (new, 41 words), `sdcard/apps/clock.app/lang/fr.txt` (*Alarms and timers…* moved out: it is the menu bar's), `user/Makefile` (`FT_EXTRA_menubar` = the Clock's core; `menubar.elf`'s deps), `shots.sh` (`build ()`: menubar's extra; the menu bar's `clock.png` now made after `kfix`), `screenshots/clock.png`, `run_clock_sim_test.sh` (the menu bar built and driven) | **The bell**: `alarms.txt` read through the Clock's own model (`alarms_load` + `alarms_next`, then `alarms_free`) at the start, at each new minute and when the calendar opens; shown when the next ring (the snoozes counted) is within 24 h and the date is real; drawn (VPath, 14 px) left of the time, the Wi-Fi and the rest moved 20 px left only then — **no alarm, nothing moves** (every other menu-bar picture byte-identical); a click on it → `lx_launch ("clock", "alarms")` (a running Clock is told and raised: one instance). **The calendar's second button** *Alarms and timers…* under *Open Calendar* (both the same width: the longer label + 32, 150 at least), shown when `SD:apps/clock.app/app.txt` exists (the Clock installed), → the same launch. **The menu bar translated** (CLAUDE.md: an older app is translated when it is worked on): `uk_lang_init ()` after the text face; `TR` on the Onyx menu (Terminal, Control Panel, File Viewer, Task Manager, the categories — `TRN` table + `// TR: Other / Multimedia`, the category's name translated where drawn only —, Open Windows, Shut Down…), Quit, *(no open window)*, *(empty)*, the volume box, the calendar's buttons, the USB box and its notifications (the sentences made `snprintf` formats, the English output unchanged). The Calendar widget inside takes UIKit's own French (months, days). |
| **13** — docs, package | `docs/04-USER-GUIDE.md`, `docs/03-DEVELOPER-GUIDE.md`, `docs/HANDOFF.md`, `IDEAS.md`, `tools/pkg/packages.ini`, `tools/pkg/versions.ini`, `docs/exports/0{3,4}-*.{docx,pdf}` | **docs/04**: §5 the menu bar (the bell, *Alarms and timers…*, its words in the system's language, the picture's caption); §11 *Language & Region* (the translated list: + the menu bar, Notes and Stickies, the Clock; "**while clockd runs, the summer time changes the clock by itself**", a `zone=` needed); §12 the catalog rows **Clock** and **clockd** (as printd's); a section ***Clock, the time, alarms, a timer and a stopwatch*** after Notes: the window, each tab (World + the analogue face + Add a City, Alarms + the editor, the ring, what rings and when — once, 2 minutes, missed while off, the summer-time nights —, Timer + Time's up + the hand-over, Stopwatch + Copy Laps), the menus and keys, clockd, the files (`alarms.txt`, `config.ini` with every key, `system.ini`, `autostart`), French; 10 pictures. **docs/03**: the Clock and clockd as **the pattern of an app with a service without a window** (the core both link, clockd's loop, "clockd has no words: the rings handed to the Clock", the missed ones, `locale_zone_sync`, **the close rule R-1**: a close cannot be refused, so every exit hands over), the tests and shots; `SIM_CLOCK` / `SIM_TZ` in the simulator's paragraph; the menu bar and the Clock in the translated list; clockd / clipd in the `Shell` category's list. docs/06 needed nothing more (Developer A's SystemKit and FileKit paragraphs). **HANDOFF**: the Clock's priority line says built (AutoDev round 6, awaiting validation). **IDEAS.md** (French, as the file): a row *Horloge* (done, what is left for the Pi, the *later* list of 02), clockd as the seed of the task scheduler, and four subjects: **a date kit** (the date code written several times: clocktime, SystemKit's `locale_days_`, the Calendar, the agenda, Notes; the menu bar linking the Clock's model), **a notification history** (R-2), **a refusable close** (R-1), **a priority sound output for alarms** (R-4). **Package**: `[clock]` before `[onyx]` (clockd is `Shell`, which `[onyx]`'s `apps = Shell` would take): `files = apps/clock.app/ apps/clockd.app/`, `config = apps/clock.app/config.ini apps/clock.app/alarms.txt`, `needs = uikit >= 1.793, systemkit >= 1.78, filekit >= 1.99, audiokit >= 1.232, fontkit` (a kit's version is 1.<its table's slot + 1>, as `[notes]`' "uikit.abi 780 → 1.781": `locale_zone_sync` is slot 77, `fk_kv_block_set` slot 98); `[onyx]`'s `filekit >= 1.78` → **1.99** (the menu bar now links `alarms.cpp`, which calls `fk_kv_block_*`); `versions.ini` `filekit = 1.99.0`, `systemkit = 1.78.0` (the skill: "to give a bigger version, write it in versions.ini first" — else `--bump` would only raise the last number and the needs could not be met). Checked with `mkrepo.plan ()` (no file left in no package; clock's 5 files, menubar's `lang/fr.txt` in `onyx`) and `run_pkg_test.sh` (0 failures) — **not published**. `sdcard_lite` untouched: `mkrepo.py --lite` makes it at publishing (the plan asks nothing of it); `sdcard/etc/autostart` already has `run clockd` (Developer B). **Exports**: `python3 docs/build_docs.py` (pandoc + LibreOffice present) — only `03-DEVELOPER-GUIDE` and `04-USER-GUIDE` `.docx` / `.pdf` committed (the others differed by their build stamps only, reverted). |

### The commits

| Commit | Message |
|---|---|
| `a1031a62` | AutoDev round 6: step 12 — the Clock's screenshots, English and French (shots.sh clock block, config-3s.ini) |
| `a086932d` | AutoDev round 6: S2 — the alarms missed while the Pi was off (alarms_missed_since, clockd at its start, clock --missed) |
| `abfd6746` | AutoDev round 6: S3 — the analogue face (View > Analogue / Digital Clock, config.ini face =) |
| `da97fada` | AutoDev round 6: S3 — the analogue face's screenshots (clock-analogue, -fr); the French Timer footer reworded |
| `b228a731` | AutoDev round 6: S1 — the menu bar's bell and Alarms and timers… (the menu bar translated, English and French) |
| `a9889e1b` | AutoDev round 6: step 13 — the docs (docs/04 Clock, the menu bar, Language & Region; docs/03 clockd), the clock package declared, IDEAS |

### The tests run, and their results

| Command | Result |
|---|---|
| `sh tools/tests/run_clock_test.sh` | `clock: all checks passed` — alarm **174** checks (was 158: + `t20_missed_since`, 16), clocktime 129, at `-O1` UBSan and `-O2` |
| `sh tools/tests/run_clock_sim_test.sh` | **`clock-sim: all 220 checks passed`** (was 188). New: **S2** (22) — `clockd-late` and `clockd-guard` now also say the missed alarm (`--missed 1 202609280700`; the guard's 12:35 once the 90 s are past), `clockd-missed` (the 07:00 one and the 12:10 snooze in **one** `--missed 1 202609280700 6 202609281210`, exactly one exec; not last night's 23:00 — 13 h —, not a weekly, an off, an already-told or a later one; the running Clock told by message; the fixture: nothing; no Clock: the word-free bubble), `clock-missed-start` (both notified, `missed =` written, the once alarms off, the others not said, RELOAD, no window / sound, ended by itself; told twice → said once; French *Horloge\0Alarme manquée : 07:00 Pills*; a running Clock: notified, the row *missed 07:00*, not raised); **S3** (5) `clock-face` (View ▸ Analogue Clock from the Alarms tab → World, `face = analogue`, read back at the next start, Digital → `face = digital`, no zone, French); **S1** (5) `menubar-bell` (an alarm today → the bell, its click → `sim: exec SD:apps/clock.app/main alarms`; no alarms / the next one in 3 days → no bell, the click opens nothing), `menubar-alarms` (the calendar's second button → the Clock's Alarms; *Open Calendar* still the Calendar). The Clock's and clockd's sources still build with `-Wall -Wextra` and no warning. |
| `sh tools/tests/run_kvtext_test.sh` | `ok kvtext, ASan build, no files (148 checks)` · `ok kvtext (162 checks)` |
| `sh tools/tests/run_notes_test.sh` · `run_notes_sim_test.sh` · `run_stickies_sim_test.sh` · `run_clipboard_test.sh` · `run_pkg_test.sh` | `notes: all checks passed` · `notes-sim: all 67` · `stickies-sim: all 29` · clipboard 0 failures · pkg 0 failures |
| `sh tools/tests/desktop_sim/run.sh` | `desktop_sim: done` |
| `python3 tools/lang/check.py clock menubar` | `clock [fr]: 160 words, 0 missing, 0 not used` · `menubar [fr]: 41 words, 0 missing, 0 not used` (clockd has no words) |
| `shots.sh clock` (+ `SHOTS_LANG=fr` for the menu bar) | 19 pictures (`clock.png` + 9 English + 9 French), each looked at; the menu bar in French looked at too (the calendar, *Ouvrir le Calendrier* / *Alarmes et minuteurs…*, the Onyx menu — *Panneau de configuration*, *Gestionnaire des tâches*, *Multimédia* —, the USB box): every word fits |
| **The menu bar's other pictures** (AC-5), rendered and compared channel by channel: `menubar`, `menubar-tray`, `usbmenu`, `wifimenu`, `volume`, `stickies`, `stickies-empty`, `notes-desktop`, `desktop`, `milk` | **identical** to `screenshots/` except `desktop.png` (the terminal window's area, 392…1012 × 156…576) and `milk.png` (92…931 × 67…756): both **render the same on `origin/main`** (a scratch worktree) — a drift older than this round, not touched. |
| **The *Open Calendar* frame** (Developer C's note) | Rendered on `origin/main` in a scratch worktree: `origin/main` and this branch draw `clock.png` **the same**, and both differ from the committed picture only in that button's frame (210…360 × 216…244) — the committed picture is older than a UIKit button style change in `main`: **not this round's**. `clock.png` is regenerated anyway by S1 (its popup has two buttons now: intended). (Note: comparing RGBA pictures with `ImageChops.difference (a, b).getbbox ()` looks at the alpha channel only — Developer C's "only the PNG encoding differs" came from it; compare each channel.) |
| AArch64 (`clang++ --target=aarch64-none-elf`, `NL_CXXFLAGS` + `-Wall -Wextra`, the host's headers for declarations) | `Apps/clockd/main.cpp`, `Apps/clock/main.cpp` (+ its views), `alarms.cpp`, `clocktime.cpp`, `Apps/menubar/main.cpp`: no error, **no warning of their own** (only FontKit's six `fonts.h` unused functions, the same count before the change for the menu bar). The Pi `make` is the user's. |

### Deviations from the plan, and why

1. **S1 translates the whole menu bar** (not only the new button): CLAUDE.md's rule (an older app is translated when
   it is worked on). *Alarms and timers…* moved from the Clock's catalogue to the menu bar's own
   (`SD:/apps/menubar.app/lang/fr.txt`: the catalogue is the running program's). The bell has a click (the Clock's
   alarms), which 02 did not ask.
2. **S1's bell reads the Clock's model** (`FT_EXTRA_menubar` = `alarms.cpp`, `clocktime.cpp`), as 03 §5 says; a third
   program now links that core — noted in IDEAS as the *date kit* subject. `[onyx]` therefore needs `filekit >= 1.99`.
3. **S2 goes through the Clock** (`--missed`), not a notification of clockd's own: clockd has no words (03 §3.5,
   fact 7) — the miss is said in the system's language and written on the row (*Missed at*); all the missed ones in
   one argument (one start). The check runs when the date is first **trusted**, not at clockd's very first step: at
   boot the restored clock is not the real time for 90 s (R-5). An alarm due in those 90 s, before not rung at all
   (R-5's residual), is now said missed.
4. **S3's menu**: two items *Analogue Clock* / *Digital Clock* (UIKit's `Menu` has no check mark), no key.
5. **`versions.ini`** raised for FileKit and SystemKit so that the declared needs can be met at publishing (see 13).

### For the Pi (to check by hand)

AC-28 (an alarm with the Clock closed, another app in front; Snooze / Stop at once; R-7's delay), AC-29 (2 minutes,
then *Missed alarm*; the bubble does not stay: R-2), AC-30 (the three FM sounds on the jack and HDMI, muted, the
output busy with the Media Player playing → *Sound unavailable: the sound output is busy*), AC-31 (a reboot: the
alarms kept, the next one rung; one passed while off **not rung, said missed** after the 90 s — S2), AC-36 (a 10-min
timer within 1 s), *Time's up* by clockd after a real close, `clockd-one` (a second clockd quits), the summer-time
night (`locale_zone_sync`: the clock moves within a minute), the menu bar's bell appearing within a minute of an
alarm made in the Clock, the menu bar in French after a reboot, and the `make` itself (the `.abi` lines — R-11).

## Summary

**Round 6 built the Clock and clockd** (Developers A–D: their commits in each section above, the last code commit
`a9889e1b`). Two programs around one UI-free core (`user/Apps/clock/alarms.*`, `clocktime.*`), two small kit
additions (SystemKit `locale_zone_offset_at` / `locale_zone_sync`; FileKit `fk_kv_block_new / _get / _set`), one
simulator addition (`SIM_CLOCK`), **no kernel, kapi or AppKit change**. All the *must* items and the three *should*
items S1–S3 are done (02's first *should*, the timer handed to clockd and the stopwatch kept, was promoted to must
by 03 R-1). Not done: 02's last *should* (named timers, several at once) — not in 03's plan; noted in IDEAS.

**Every test, last run** (2026-10-07, this container): `run_clock_test.sh` (alarm 174, clocktime 129, zones, the
simulator's clock — all passed), `run_clock_sim_test.sh` (**220 / 220**), `run_kvtext_test.sh` (148 + 162),
`run_notes_test.sh`, `run_notes_sim_test.sh` (67), `run_stickies_sim_test.sh` (29), `run_clipboard_test.sh`,
`run_pkg_test.sh` (0 failures), `desktop_sim/run.sh` (done), `check.py clock menubar` (0 missing), `shots.sh clock`
(19 pictures, looked at in both languages), the menu-bar pictures compared (unchanged but for two drifts older than
the round), `git diff origin/main -- kernel/ user/Kits/appkit/` empty, AArch64 compiles without a warning of their
own. Developer A's other runs (circuits, critters, pinball, mail, media, letters, slides, archiver…) touched code
the later steps did not change.

**Screenshots** (`screenshots/`): `clock.png` (the menu bar's calendar: the bell, *Alarms and timers…*),
`clock-world`, `clock-analogue`, `clock-cities`, `clock-alarms`, `clock-edit`, `clock-ring`, `clock-timer`,
`clock-timesup`, `clock-stopwatch`, and each `-fr`.

**Acceptance criteria** (02 §9, as amended by 03 §9 / §10.5):

| AC | Status | How |
|---|---|---|
| 1 build, Makefile, app.txt | sim + AArch64 compile (Pi `make` the user's) | the PC builds in both test scripts at `-Wall -Wextra`; `clock.elf`, `clockd.elf` in `user/Makefile`; `app.txt` `Clock` / `Productivity` |
| 2 check.py 0 missing | passed (tool) | `clock` 160 words, `menubar` 41, 0 missing; clockd has no words by design |
| 3 shots EN / FR | passed in sim | 9 + 9 pictures + `clock.png`, looked at; no French word cut |
| 4 docs, autostart, package | passed | docs/04 catalog + section, docs/03, `run clockd` in `autostart`, `[clock]` declared, `build_docs.py` ran |
| 5 no kernel / kapi change, other shots unchanged | passed | the diff empty; the other pictures identical (two older drifts, on `main` too) |
| 6 here 12:34:00, date, UTC+2 summer | passed in sim | `clock-world` (EN / FR), zone_test |
| 7 Tokyo / New York / London | passed in sim + unit test | `clock-world`, clocktime_test |
| 8 tomorrow / yesterday | unit test + sim | clocktime_test; `clock-world` at 23:30 / 01:00 |
| 9 cities in config.ini, 12 at most | passed in sim | `clock-cities` |
| 10 no zone | passed in sim | `clock-nozone`, `clock-face` (analogue) |
| 11 the alarm block written | passed in sim + unit test | `clock-alarm-new` (EN / FR), alarm_test |
| 12 the next-alarm line | passed in sim + unit test | `clock-next`, alarm_test |
| 13 a once alarm's date | passed in sim + unit test | `clock-alarm-new`, alarm_test |
| 14 the repeat words | passed in sim + unit test | EN / FR rows, alarm_test |
| 15 rewrite + RELOAD, 21st refused | passed in sim | `clock-alarm-new`, `clock-21`, `clockd-reload` |
| 16 unknown key kept, 25:99 invalid | passed in sim + unit test | `clock-invalid`, alarm_test |
| 17 exactly once, reload | unit test + sim | alarm_test 1–2, `clockd-reload` |
| 18 not on weekends, next Monday | unit test | alarm_test 3 |
| 19 disabled / deleted never | unit test + sim | alarm_test 4, `clockd-disabled` |
| 20 a once alarm rings once | unit test | alarm_test 5 |
| 21 snooze | unit test + sim | alarm_test 6, `clock-snooze` |
| 22 late / missed window | unit test + sim | alarm_test 7–9, `clockd-late` (and S2's notice) |
| 23 the summer-time days | unit test | alarm_test 10, 13 |
| 24 clockd rings (changed: the Clock notifies) | passed in sim | `clockd-ring`, `clockd-running`, `clock-ring` |
| 25 the ring card, Esc / Enter, on = 0 (+ ring-only closes) | passed in sim | `clock-ring`, `clock-snooze`, `clock-ring-only` |
| 26 one instance | passed in sim | `clock-one` |
| 27 starts clockd | passed in sim | `clock-clockd` |
| 28 rings with the app closed | **Pi only** | (clockd → Clock in sim: `clockd-ring`) |
| 29 2 minutes, *Missed* stays | **reduced** (R-2) — PC part passed in sim, Pi to check | `clock-missed`: the bubble + *Missed at* on the row; no notification history (IDEAS) |
| 30 sounds, muted, no SoundFont | **reduced / changed** (R-3, R-4) — PC part passed in sim, Pi to check | FM voices (no SoundFont to remove); `clock-nosound` (no output); *busy* on the Pi |
| 31 reboot | **Pi only** — PC part passed in sim | `clockd-late`, `clockd-guard`, S2's `clockd-missed` |
| 32 timer presets, run, pause, reset | passed in sim | `clock-timer` |
| 33 Time's up, +1 min | passed in sim + unit test | `clock-timesup`, `clockd-timer`, `clock-timesup-hand`, alarm_test 19 |
| 34 time from the ticks | unit test | clocktime_test |
| 35 timer = 300 kept | passed in sim | `clock-timer-close` |
| 36 10-min accuracy | **Pi only** | — |
| 37 laps, marks, reset | passed in sim | `clock-sw` |
| 38 Copy Laps | passed in sim + unit test | `clock-sw` (the clipboard byte-equal, EN / FR header) |
| 39 formats, sum | unit test | clocktime_test |
| 40 close with timer / stopwatch running | **reduced** (R-1, G1: no question, every exit hands over) — passed in sim | `clock-quit-hand`, `clock-timer-close`, `clock-timer-resume`, `clock-timesup-hand` |
| 41 keys | passed in sim | `clock-keys`, `clock-edit-keys`, `clock-args` |

**Left for the Pi** (the user): the `make` / `make stage` (no AArch64 toolchain here: R-11's `.abi` lines to
confirm), AC-28, 29, 30, 31, 36 and the items of *For the Pi* above; then, after the user's validation, the
publishing (`publish.sh`: the `clock` package, `onyx` with the menu bar, FileKit 1.99 and SystemKit 1.78).
