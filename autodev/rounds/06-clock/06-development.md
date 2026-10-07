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
