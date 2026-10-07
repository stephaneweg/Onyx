# AutoDev round 6 — Technical Analyst: Clock + clockd

Date: 2026-10-07. Inputs: `01-product-manager.md`, `02-product-analysis.md` (the scope, the files §5, the 41
acceptance criteria — "AC-n" below = 02 §9 item n, "02 #n" = 02 §4 item n). Branch `AutoDev`. The shape follows
rounds 1 and 5 (`autodev/rounds/01-notes/03-technical-analysis.md`: an app + a background program).

Read for this analysis (each claim below was checked in these files): `user/Kits/appkit/appkit.h`
(`kapi_get_datetime`, `kapi_get_ticks`, `kapi_clock_info`, `kapi_set_timezone`, `kapi_ipc_register/lookup`,
`kapi_mailbox_send/recv`, `kapi_raise_app`, `kapi_exec`, `kapi_get_args`, `kapi_should_exit`, `lx_launch`),
`user/Kits/appkit/appkit_lib.inc` (`lx_launch`), `kernel/sys/kapi.cpp` (`kapi_get_datetime`, `kapi_should_exit`,
`kapi_set_timezone`), `kernel/sys/procx.cpp` (`kapi_clock_info`), `kernel/kernel.cpp` (`ReadSystemConfig`,
`m_Timer.SetTimeZone (g_nTimeZoneMin)` at boot), `kernel/sys/crashlog.cpp` (`CrashLogClockRestore`),
`user/Kits/systemkit/{locale.h,locale.inc,notify.h,clipboard.h,autostart.h,systemkit.abi}`,
`user/Apps/notifyd/main.cpp` (`run_action`, the queue, no history), `user/Kits/audiokit/{audiokit.h,aksf.cpp}`,
`user/Apps/games/game.h` (`sfx*` on `ak_fm_*`), `user/Kits/filekit/{kvtext.h,kvtext.inc,filekit.abi}`,
`user/Kits/uikit/{root.h,root.cpp,dialog.h,dialog.cpp,segmented.h,tabhost.h,numeric.h,toggle.h,vpaint.h,lang.h,
lang.cpp}`, `user/Kits/fontkit/uikitface.h`, `user/Apps/agenda/main.cpp` (`reminders ()`, `minutes_of`),
`user/Apps/notes/{main.cpp,stickies_proto.h}`, `user/Apps/stickies/`, `user/Apps/clipd/main.cpp`,
`user/Apps/pkgd/main.cpp`, `user/Apps/taskman/main.cpp` (`SegmentedControl` as the tab bar),
`user/Apps/langconf/main.cpp`, `user/Apps/setup/main.cpp`, `user/Apps/menubar/main.cpp`, `user/Makefile`
(`FT_APPS`, `FT_EXTRA_*`, the `clipd.elf` / `pkgd.elf` newlib rules), `kernel/Makefile` (`stage`), `sdcard/etc/
{autostart,system.ini}`, `sdcard/apps/{notes,stickies,clipd,agenda}.app/app.txt`, `sdcard/res/soundfonts/`,
`tools/pkg/packages.ini` (`[notes]`), `tools/tests/desktop_sim/{fakekapi.cpp,shots.sh}`,
`tools/tests/{run_notes_test.sh,run_critters_sim_test.sh,run_kvtext_test.sh}`, `tools/tests/filekit/kvtest.cpp`,
`tools/lang/check.py`, `autodev/rounds/01-notes/06-development.md`.

**Summary.** The Clock is **two programs** — `clock` (an FT app, UIKit + FontKit + AudioKit) and `clockd` (a newlib
program without a window, as `clipd` / `pkgd`) — around **one UI-free core beside the app** (`user/Apps/clock/
alarms.{h,cpp}`, `clocktime.{h,cpp}`) that both link and the PC tests link alone. **No kapi change, no kernel change,
no AppKit change** (`kapi_abi.h`, `appkit.h`, `appkit_calls.inc`, `appkit.abi` untouched: AC-5 holds). Two **small kit
additions**, each justified in §2: **SystemKit** `locale_zone_offset_at` + `locale_zone_sync` (the summer time judged
at an instant, and applied to the system's clock — today it is **never** applied after the zone is chosen: §1.1 fact
1), and **FileKit** `fk_kv_block_new / _get / _set` (an `fk_kv` document cannot today set a key in its *second*
`[alarm]` block: §1.1 fact 6). **One simulator addition** (`SIM_CLOCK`: a date and time that advance with the ticks —
today the simulator's clock is frozen at 12:34:00). Three **scope reductions** forced by the code (§6): the
"Quit anyway?" question on the close box (AC-34/40), the *Missed alarm* "staying" in the notifications (AC-29), and
the SoundFont as the alarm's synthesizer (02 #24, AC-30) — each with its replacement.

### Environment (checked in this container)

| Item | State |
|---|---|
| `aarch64-none-elf-g++` | **absent** — `make` / `make stage` for the Pi are the user's (AC-1's Pi build "written, not built here", as rounds 1–5) |
| host `g++`, `python3` | present (Pillow for `shot.py`, as the earlier rounds) |
| `shots.sh` | builds every app of its `APPS` list whatever names are given (≈ 4 min); develop with `SHOTS_PNG=<scratch>` |

---

## 1. What exists and is reused (by file)

| File | What the Clock takes | Notes |
|---|---|---|
| `user/Kits/appkit/appkit.h` | `kapi_get_datetime (&y,&mo,&d,&h,&mi,&s)` → the **local wall time** (returns 0 while the clock is not set: "the time since the boot"); `kapi_get_ticks ()` (100 Hz since boot — the timer / stopwatch base, 10 ms = one hundredth); `kapi_clock_info` (`utc_us`, `tz_minutes`, `KAPI_CLOCK_REALTIME_VALID`); `kapi_set_timezone (min)`; `kapi_ipc_register / lookup`, `kapi_mailbox_send / recv` (≤ 512 B); `kapi_raise_app`; `kapi_get_args`; `kapi_msleep`; `lx_launch (name, args)` | existing calls only. `kapi_get_datetime` = `CTimer::GetLocalTime ()` = UTC + the offset set by `SetTimeZone` (kernel/sys/kapi.cpp). |
| `user/Kits/systemkit/locale.h` / `locale.inc` | `locale_zone_count / city / utc / summer / offset`, `locale_zone ()` (system.ini `zone=`, else the first zone whose offset today equals `timezone=`), `locale_ini_get ("timezone")`, `locale_dow` | the 23 zones (`LocaleZone { city, std, dst }`, `LOCALE_DST_EU / _US / _NONE`); **the summer time is judged by the day only** (`locale_zone_summer`: "the hour of the change is not looked at") — §2.1 |
| `user/Kits/systemkit/notify.h` | `notify_action (title, text, "clock alarms")` → notifyd's bubble; a click runs `SD:/apps/clock.app/main alarms` (`run_action` in `user/Apps/notifyd/main.cpp`) | a bubble with an action stays `2 * HOLD_MS` = 8 s, then is gone: **notifyd keeps no history** (§6 R-2) |
| `user/Kits/systemkit/clipboard.h` | `clip_set_text_n (text, n)` | clipd itself notifies *Text copied* (`user/Apps/clipd/main.cpp` `tell ()`); the Clock's *Laps copied* is its own status line, not a second bubble (AC-38) |
| `user/Kits/systemkit/autostart.h` (round 1) | `autostart_ensure ("run clockd", "run notifyd", comment)`, `autostart_has` | a card **updated** by the package manager keeps its own `SD:/etc/autostart` (the `onyx` package's): the Clock adds the line at its first start (as Notes adds `run stickies`) |
| `user/Kits/filekit/kvtext.h` | `fk_kv_parse / load / new / free`, `fk_kv_blocks`, `fk_kv_block_name`, `fk_kv_block (kv, i)`, `fk_kv_key / value / section / line`, `fk_kv_get / set / save` | reading `alarms.txt` (repeated `[alarm]` blocks) works today; **writing** one needs §2.2. `config.ini` (one `[clock]` section) works today with `fk_kv_load / get / set / save`. |
| `user/Kits/audiokit/audiokit.h` | **`ak_fm_instrument`, `ak_fm_start (voice, milli_hz, wave, volume)`, `ak_fm_stop`, `ak_fm_silence`** (the FM voices, no file) — §6 R-3; `ak_play_state` (`AK_BUSY`) for *Sound unavailable* | as `user/Apps/games/game.h` `sfx / sfx_later / sfx_tick` (a note queue stepped by the loop) — the Clock has its own 3-pattern sequencer (`sounds.h`), not game.h (game.h is the games') |
| `user/Kits/uikit/segmented.h` | `SegmentedControl` (the four tabs, as the Task Manager's `g_tabs`, `user/Apps/taskman/main.cpp:547`) | `TabHost` is a drop-down task picker, not a tab bar: not used |
| `user/Kits/uikit/numeric.h`, `toggle.h`, `checkbox.h`, `listbox.h`, `textbox.h`, `button.h`, `menu.h`, `vpaint.h`, `lang.h` | `NumericUpDown` (hours / minutes / seconds), `ToggleSwitch` (an alarm on / off), `Checkbox` (the seven days), `ListBox` / a drawn list widget (cities, alarms, laps — as Pinball's `picker.h`), `Textbox` (the label, 40 characters), `VPath::arc` (the timer's progress ring), `Menu`, `TR` / `TRC` / `TRN` / `uk_lang_init` | **no UIKit change** |
| `user/Kits/fontkit/uikitface.h` | `ft_uikit_install ("DejaVu Sans", 13)`, an `FtTextFace` at a large size for `12:34:00` / `05:00` / `00:12.34` | as Critters' / Ledger's big numbers |
| `user/Apps/agenda/main.cpp` | **the pattern** of `reminders ()`: "now" taken as already looked at when it starts (`if (last < 0) { last = now; return; }`), the minutes in `(last, now]` rung; `minutes_of` (Hinnant's `days_from_civil`) | copied as a pattern into the core (with the 2-minute window and the backward-jump rule the agenda lacks: §3.3) |
| `user/Apps/notes/main.cpp`, `stickies_proto.h`, `user/Apps/stickies/main.cpp` | one instance (`kapi_ipc_lookup (NOTES_SERVICE)` → send `NOTES_MSG_OPEN` + `kapi_raise_app` + quit; else `kapi_ipc_register`), the companion told `STK_MSG_RELOAD`, started with `lx_launch ("stickies", 0)` when `kapi_ipc_lookup` finds none; the companion in **its own folder** sharing the model's `.cpp` (`FT_EXTRA_stickies = Apps/notes/notesmodel.cpp`) | `clock_proto.h` is the same file shape |
| `user/Apps/clipd/main.cpp` + `user/Makefile` `clipd.elf` | a newlib program with no window, an IPC service, staged as `apps/clipd.app/main`, `category = Shell` | **clockd's build rule and its `app.txt`** |
| `kernel/Makefile` `stage` | every `user/*.elf` → `sdcard/apps/<name>.app/main` | `clockd.elf` → `apps/clockd.app/main`; `run clockd` launches it |
| `tools/tests/desktop_sim/fakekapi.cpp` | `SIM_SERVICES` (`sim: send <name> type <t> "<payload>"`), `SIM_MBOX` with `@<ticks>:` (a message mid-run), `SIM_ARGS`, `SIM_LOG` + `waitlog`, `sim: exec <path> <args>` logged, `copy SRC DST`, `mods`, `key`, `winclose`, `quit` | the 20 ms a script step (`g_ticks += ms / 10 + 1` for `msleep (16)`) drives the timer and the stopwatch already; the **date is frozen** (`get_datetime`: always 2026-09-28 12:34:00) — §4 |
| `tools/tests/run_notes_test.sh`, `run_critters_sim_test.sh`, `run_kvtext_test.sh` | the shapes of the PC unit tests (`-fsanitize=undefined`, a `CHECK` macro, linked with `fakekapi.o` when the kapi is needed) and of a scripted sim test (fresh `SIM_WRITES` per case, asserts on the log and on the written files) | copied for `run_clock_test.sh` and `run_clock_sim_test.sh` |
| `tools/tests/desktop_sim/shots.sh` | `build ()` (`extra`, the FT `case` list, `APPS`), `sim`, `png`, `lang fr`, fixtures copied into `$OUT/writes/apps/<app>.app/` | a `clock` block (§8.3) |
| `tools/lang/check.py` | scans `user/Apps/clock/**` (`TR`, `TRC`, `TRN`, `// TR:` comments) against `sdcard/apps/clock.app/lang/fr.txt` | clockd has **no words** (§3.5), so no catalogue of its own |
| `tools/pkg/packages.ini` `[notes]` | one package for the app + its companion (`files = apps/notes.app/ apps/stickies.app/`) | `[clock]` the same (§7) |

### 1.1 Facts found that shape the design

1. **The system's summer time is never applied by itself.** The kernel reads `timezone=` (minutes) from
   `SD:/etc/system.ini` once at boot (`kernel/kernel.cpp`: `ReadSystemConfig` → `m_Timer.SetTimeZone (g_nTimeZoneMin)`);
   `locale_set_zone` (Language & Region, Setup) writes *today's* offset there. Nothing changes it on 2026-10-25 or
   2027-03-28: the menu bar stays on UTC+2 all winter until the user picks the zone again. And `locale_zone_summer` is
   judged by the **local day** only — applied automatically as it is, it would oscillate on the night of the change
   (at Sunday 00:00 local it says "winter" → the clock goes back to Saturday 23:00 → "summer" → …). §2.1.
2. **The simulator's clock does not move.** `fakekapi.cpp` `get_datetime` returns 12:34:00 whatever the ticks;
   `clock_info` is `-KAPI_ENOSYS` unless `SIM_STAT=1` (then 12:34 **UTC**, `tz_minutes` 0 — inconsistent with a Brussels
   wall time of 12:34). AC-24 (clockd ringing "one minute after the fake now") needs a clock that advances. §4.
3. **The main window's close cannot be refused.** `kapi_should_exit` is a sticky window flag
   (`kernel/sys/kapi.cpp` → `CWindow::ShouldExit`); `uk_quit ()` (root.cpp) returns it, `Root::run` and every
   `Modal::run` loop (`while (!done && !uk_quit ())`, dialog.cpp:50) end on it, and no kapi clears it. Letters keeps an
   unsaved document *after* `root.run ()` for the next start (`user/Apps/letters/main.cpp`) — it does not ask. §6 R-1.
4. **`Modal::run` does not call `Root::onTick`** (dialog.cpp:50–55: `uk_pump`, `paintAll`, `msleep (16)`). A ringing
   dialog made a `Modal` could not step its sound pattern nor its 2-minute timeout, and a timer reaching zero while the
   alarm-edit dialog is open would not ring until it closes. The Clock's dialogs are **overlays** (real widgets as the
   root's children, focus taken, driven by `onTick`) — round 4's R11 pattern (Pinball's overlays).
5. **The SoundFont is heavy for an alarm.** `ak_note_on` loads the default SoundFont on first use
   (`aksf.cpp` `ak_soundfont_default` → `ak_soundfont_load`: the whole file `malloc`ed and read, then parsed by
   MeltySynth, then the buffer freed): `sdcard/res/soundfonts/GeneralUser-GS.sf2` is **32 MB** → ~64 MB at the peak and
   seconds of card reading in the ringing process, at the moment it must ring; and it is a separate package
   (`generaluser-gs`), absent from a lite card. The FM voices (`ak_fm_*`) need no file and start at once. §6 R-3.
6. **`fk_kv` cannot edit the 2nd block of a name.** `fk_kv_set (kv, section, key, value)` changes "the first match" or
   adds "at the end of that section's **first** block" (kvtext.h:75–77); there is no way to make a second `[alarm]` block
   nor set a key in it. Reading them is fine (`fk_kv_block (kv, i)` per entry). §2.2.
7. **clockd cannot translate cheaply.** `uk_tr` lives in UIKit and reads `SD:/apps/<this process>.app/lang/` (lang.cpp
   `lg_app_path` → `kapi_app_dir`) — for clockd `clockd.app`, not `clock.app`; and without a text face it converts the
   words to Latin-1 (`lg_latin1`), while notifyd's text is UTF-8. So **the words of a ring are the Clock's** (§3.5).
8. **No "NTP synced" signal reaches a program.** At boot the kernel restores the last time seen (`CrashLogClockRestore`,
   `SD:/etc/clock`, flagged valid like a real one) and NTP corrects it later — usually a **forward jump** of hours. The
   2-minute window (02 #21) is what keeps clockd from ringing the night's alarms at once; a short guard after boot
   covers the rest (§3.3).
9. **A notification's click** runs `SD:/apps/<app>.app/main <args>` with `kapi_exec` (notifyd `run_action`): `clock
   alarms` reaches the one-instance logic like any start.

---

## 2. What is missing, and where it goes (kits first)

### 2.1 SystemKit — the summer time at an instant, and applied (`systemkit/locale.h`)

```c
// The zone's offset from UTC at that instant (minutes since 1970, UTC), the hour of the change counted: the EU's
// from the last Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC; the US' from the second Sunday of
// March 02:00 local standard time to the first Sunday of November 02:00 local summer time. -> minutes (0 out of range).
SK_API int locale_zone_offset_at (int z, long long utc_minutes);
// The chosen zone's offset now (locale_zone (), kapi_clock_info's UTC) given to the clock (kapi_set_timezone) and
// to system.ini's timezone= when it differs from the clock's (kapi_clock_info's tz_minutes) -> 1 changed, 0 not
// (no zone chosen, no real date yet, already right).
SK_API int locale_zone_sync (void);
```

- **Why in SystemKit**: the zone table and its rules are SystemKit's (`locale.inc` `locale_zone_at`); the Clock must
  not hold a second copy of the summer-time rules (CLAUDE.md, kits first). Language & Region and Setup can show the
  exact offset with it later. The existing `locale_zone_summer / _offset` are **left as they are** (day-only: their
  callers, langconf and Setup, and their screenshots unchanged).
- **Why judged from UTC**: UTC never goes back, so the sync cannot oscillate (§1.1 fact 1); a city's time on the World
  tab is `UTC + locale_zone_offset_at (city, UTC)` — right for New York on 2026-11-01 whatever Brussels' day is.
- **`locale_zone_sync` is called by clockd** (at its start and once a minute): clockd runs from boot, so the system's
  clock follows the summer time at last (25 Oct 2026 at 03:00 CEST it becomes 02:00 CET). It uses only existing calls
  (`kapi_clock_info`, `kapi_set_timezone`, `locale_ini_set`). **A system behaviour change** — small and wanted (the
  applet already says "the summer time counted"), but the validators may defer it (§5 step 1b is separable): without
  it the Clock stays right for the cities (from UTC) and its "here" follows the menu bar.
- `systemkit.abi`: `76 locale_zone_offset_at`, `77 locale_zone_sync`; `python tools/docgen/kitdocs.py` →
  `docs/12-SYSTEMKIT.md`; a paragraph in `docs/06-KITS-GUIDE.md` (SystemKit: "the time zones").

### 2.2 FileKit — blocks of the same name, written (`filekit/kvtext.h`)

```c
FK_KV_API int fk_kv_block_new (fk_kv *kv, const char *name);			// a new "[name]" block at the end -> its number (1-based), -1 no memory
FK_KV_API const char *fk_kv_block_get (const fk_kv *kv, int b, const char *key, const char *def);	// block b's value of key, def if none
FK_KV_API int fk_kv_block_set (fk_kv *kv, int b, const char *key, const char *value);	// in block b: replaced, else added at its end -> 0, -1
```

- **Why in FileKit**: the header promises "a section may come back many times … a program writes back what it read
  with only its own keys changed" (kvtext.h:3–7) — true for reading, not for writing a second block. Any editor of a
  multi-block document needs it (a Pinball / Critters / Circuits level editor later; the IDEAS task scheduler). The
  internals already exist (`fkkv_add_block_`, `fkkv_insert_` in `kvtext.inc`): ~40 lines.
- The Clock's save = `fk_kv_new (0)`, for each alarm `b = fk_kv_block_new (kv, "alarm")`, its keys with
  `fk_kv_block_set`, then **the unknown keys of its block in the file read** copied in (AC-16), then `fk_kv_save`.
- `filekit.abi`: `96 fk_kv_block_get`, `97 fk_kv_block_new`, `98 fk_kv_block_set` (what libgen appends, sorted —
  to be confirmed by the user's `make`, as rounds 1–2); `docs/14-FILEKIT.md` regenerated; cases in
  `tools/tests/filekit/kvtest.cpp` (`run_kvtext_test.sh`).
- *Fallback if the validators refuse a kit change*: the core writes the text itself (one `key = value` per line,
  labels stripped of new lines) — 30 lines in `alarms.cpp`; the reading stays `fk_kv`'s.

### 2.3 Beside the app (one app's two programs: "what one program alone uses stays beside it")

| What | Where | Why not a kit |
|---|---|---|
| The alarms model (parse / save `alarms.txt`, `[timer]`), the next occurrence of a once / weekly alarm, the "ring once, within 2 min" ringer, snooze, missed | `user/Apps/clock/alarms.{h,cpp}` | used by `clock` and `clockd` only — the Notes / Stickies case (`notesmodel.cpp`). The IDEAS task scheduler could take the next-occurrence part later; it would then move to a kit (SystemKit) with a user of its own. |
| Wall-time arithmetic (`clk_days`, `clk_civil`, `clk_wday`, wall minutes since 1970), the world rows (`city time, day ±1, difference`), the formats (`MM:SS.hh`, `H:MM:SS`), the timer and stopwatch states from ticks, the laps' text | `user/Apps/clock/clocktime.{h,cpp}` | Hinnant's `days_from_civil` is already copied in ~15 programs (agenda, calendar, notes, ledger, sheet, courier…): **a real kit candidate**, but moving them is not this round's — noted for `IDEAS.md` ("a date kit subject: SystemKit `locale_days / locale_civil`"). |
| The three alarm sounds (FM patterns) and their sequencer | `user/Apps/clock/sounds.h` | one app's |
| The protocol | `user/Apps/clock/clock_proto.h` | as `stickies_proto.h` |

### 2.4 No kapi change

Everything uses calls that exist: `kapi_get_datetime`, `kapi_get_ticks`, `kapi_clock_info`, `kapi_set_timezone`,
`kapi_ipc_*`, `kapi_mailbox_*`, `kapi_raise_app`, `lx_launch`, `kapi_open/read/save_file`. The one thing a kapi
change would buy — refusing the close box (§1.1 fact 3) — is reduced instead (§6 R-1).

---

## 3. The design

### 3.1 Files

```
user/Apps/clock/
  clock_proto.h     services "clock", "clockd"; CLOCK_MSG_OPEN 1 (payload: the arguments, e.g. "alarms", "--ring 3",
                    "--ring timer"; empty: come forward), CLOCKD_MSG_RELOAD 2 (no payload), CLOCKD_MSG_QUIT 3
  clocktime.h/.cpp  UI-free, no kapi: wall time, world rows, formats, Timer, Stopwatch (§3.4)
  alarms.h/.cpp     UI-free, kapi only through fk_kv: Alarm, AlarmSet, alarms_parse/save, alarm_next, Ringer (§3.2, 3.3)
  sounds.h          the chimes / beeps / marimba patterns on ak_fm_*, stepped by onTick
  main.cpp          the window, the tabs, the overlays (ring, time's up, alarm editor, add city), config.ini
  (+ world.h, alarmsview.h, timerview.h, swview.h if main.cpp grows: one translation unit, as Circuits' views)
user/Apps/clockd/main.cpp   the service (§3.5), links ../clock/alarms.cpp + clocktime.cpp
sdcard/apps/clock.app/{app.txt, icon.bmp, lang/fr.txt}     sdcard/apps/clockd.app/{app.txt, icon.bmp}
```

### 3.2 The alarms model (`alarms.h`)

```cpp
#define ALARMS_PATH "SD:/apps/clock.app/alarms.txt"
#define ALARMS_MAX  20
enum { AL_MON = 1, AL_TUE = 2, AL_WED = 4, AL_THU = 8, AL_FRI = 16, AL_SAT = 32, AL_SUN = 64 };
struct Alarm {
	int  id;            // the file's id (never reused while the file lives: AlarmSet::next_id = max + 1)
	bool valid;         // time = HH:MM read (else shown "Invalid", never rung: AC-16)
	int  hh, mm;
	bool on;
	unsigned days;      // AL_* mask; 0 = once
	long date;          // once: the day it rings, clk_days (); -1 none
	char label[164];    // UTF-8, <= 40 characters (cut on a character, uk_u8 rules)
	char sound[12];     // "chimes" | "beeps" | "marimba" (unknown -> "chimes")
	long snooze;        // wall minute (clk_minute) it rings again, -1 none
	long missed;        // wall minute of the last unanswered ring, -1 none (R-2's replacement; written by the Clock)
	int  block;         // its block in the document read (0: new) -- the unknown keys copied from there
};
struct AlarmSet { Alarm a[ALARMS_MAX]; int n; int next_id; long timer_end_tick; int timer_set; char timer_label[64];
                  fk_kv *doc; /* the file read, for its unknown keys */ };
int  alarms_parse (AlarmSet &s, const char *text);        // the first 20 [alarm] blocks, [timer]; -> how many
int  alarms_load  (AlarmSet &s, const char *path);        // missing file = empty set
int  alarms_save  (AlarmSet &s, const char *path);        // §2.2; -> 0 / -1
unsigned alarms_days_parse (const char *v);  void alarms_days_text (unsigned m, char *out, int cap);  // "mon tue ..."
long alarm_once_day (int hh, int mm, long now_min);       // today if hh:mm is still to come, else tomorrow (AC-13)
long alarm_next (const Alarm &a, long from_min);          // the first wall minute >= from_min it rings (snooze aside), -1 never
long alarms_next (const AlarmSet &s, long now_min, int *idx); // the soonest of the on ones (snooze counted) after now
enum { REP_ONCE, REP_DAILY, REP_WEEKDAYS, REP_WEEKENDS, REP_CUSTOM };
int  alarm_repeat_kind (unsigned days);                   // the UI turns it into TR words (AC-14)
```

- **Wall minutes, calendar days.** `clk_minute (y,mo,d,h,mi) = clk_days (y,mo,d) * 1440 + h*60 + mi` (no time zone in
  it). `alarm_next` walks **calendar days** from `from_min`'s day: on the first day of `days` whose `hh:mm` minute is
  `>= from_min`. A summer-time night changes nothing (AC-23: the day of 2026-10-25 is one step like any other).
- "Next alarm: … in 18 h 26 min" = `at - now_min` wall minutes (02 #23: one hour off across a change night, accepted);
  the day word from `at / 1440 - now_min / 1440` (0 *today*, 1 *tomorrow*, else the weekday).
- A once alarm whose minute has passed is **off for the user**: the app shows it off and writes `on = 0` at its next
  save (02 §5.1); clockd never writes.

### 3.3 The ringer (in `alarms.h`, used by clockd; the PC test drives it with a fake now)

```cpp
struct Due { int id; long minute; bool snooze; };  // id ALARM_TIMER (-1) for the [timer] entry
struct Ringer { long last; bool started; };
void ringer_start (Ringer &r, long now_min);       // what is past stays past (the agenda's rule)
int  ringer_step  (Ringer &r, const AlarmSet &s, long now_min, bool trusted, Due *out, int max);
```

`ringer_step` (≈ 30 lines):

1. not `started` → `ringer_start`, return 0. `!trusted` (no real date yet, or the boot guard below) → `last = now`, 0.
2. `now == last` → 0. `now < last`: a jump **back** — if `last - now > 120` (the clock was set: NTP, the user)
   `last = now`; else keep `last` (summer time ending, a small step back: the same wall minute is **not** rung twice).
   Return 0.
3. `now > last`: for each enabled valid alarm, `m = alarm_next (a, max (last + 1, now - 2))`; `m != -1 && m <= now` →
   due. Its `snooze` the same way. Then `last = now`. A forward jump of more than 2 minutes therefore rings nothing
   older than 2 minutes (AC-22: 06:59:50 → 07:01:30 rings 07:00; 06:59 → 07:03 does not).
4. **Exactly once**: a minute is in `(last, now]` once, and `last` survives a reload of the file (AC-17).

clockd's `trusted` = `kapi_get_datetime` returned 1 **and** the uptime is past a guard (`kapi_get_ticks () >= 9000`:
90 s), so that the minutes right after the restored "last time seen" are not rung before NTP corrects the clock
(§1.1 fact 8). `clockd --grace 0` (a test argument) removes the guard (the simulator's ticks start at 1000).

The `[timer]` block: `end = <ticks>`, `set = <seconds>`, `label = …`; due when `kapi_get_ticks () >= end` and
`end - now <= set * 100 + 100` (else a stale entry of an earlier boot: ignored).

### 3.4 World, timer, stopwatch (`clocktime.h`)

- **UTC now** = `kapi_clock_info.utc_us` when it answers with `KAPI_CLOCK_REALTIME_VALID`; else the wall time minus
  `timezone=` (`locale_ini_get`). The **here** line is the wall time (`kapi_get_datetime`: what the menu bar shows),
  its `UTC±h` = `locale_zone_utc` style from `locale_zone_offset_at (here, utc)`, *Summer time* when that offset is
  above the zone's standard one. A **city** = `utc + locale_zone_offset_at (city, utc)`; its day word from the day
  difference to here's wall day; the difference = city offset − here's offset (`+7 h`, `−6 h`, `+5 h 30`, *Same time*).
  In the simulator (no `SIM_STAT`): `clock_info` is `-KAPI_ENOSYS` → wall − 120 = 10:34 UTC → Tokyo 19:34, New York
  06:34, London 11:34 (AC-7 ✓).
- **Timer**: `start_tick`, `total_cs`, `paused_left_cs`; `left = total − (now_tick − start_tick)`: from the ticks, never
  from counted frames (AC-34). Shown rounded **up** to the second (`00:01` until it reaches 0 — at T + 599 400 ms
  `left` = 60 cs → `00:01`; at T + 600 000 ms it rings).
- **Stopwatch**: `start_tick`, `base_cs` (the time before the last Start), laps as totals in hundredths (`int`, 999 at
  most); lap time = total − previous total; fastest / slowest from 3 laps (the first lap included). `fmt_sw (cs)` →
  `MM:SS.hh`, `H:MM:SS.hh` from an hour (AC-39: `00:12.34`, `59:59.99`, `1:00:00.00`). `laps_text (header words,
  …)` → the tab-separated text of 02 #33 (the header words given by the UI, translated).

### 3.5 clockd (`user/Apps/clockd/main.cpp`)

```
main: kapi_ipc_register ("clockd") (taken: another clockd runs -> exit); args "--grace N"
      alarms_load; ringer; sig = signature of the file (size + sum, as agenda's reload ())
loop (kapi_msleep (500)):
  mailbox: CLOCKD_MSG_RELOAD -> alarms_load (the Ringer kept); CLOCKD_MSG_QUIT -> exit
  every 30 s: the file's signature changed -> alarms_load (02 #14: a file edited by hand, a message lost)
  every minute: locale_zone_sync ()                                        (§2.1, separable)
  now = wall minute (kapi_get_datetime); n = ringer_step (...); for each Due:
      args = "--ring <id>" (or "--ring timer")
      pid = kapi_ipc_lookup ("clock"): pid > 0 -> kapi_mailbox_send (pid, CLOCK_MSG_OPEN, args)
      else lx_launch ("clock", args); log "clockd: ring <id> <HH:MM>"
      both failed -> notify_action ("HH:MM", label, "clock alarms")   (no words: §1.1 fact 7)
```

- **No words, no UIKit**: the notification *Clock — 07:00 School* is sent by the ringing Clock (it has the catalogue
  and UTF-8 text); clockd's own bubble is only the fallback when the Clock cannot be started, and it is word-free
  (`07:00` / the label). AC-2's "clockd's words included" is then empty by design.
- Built as `clipd.elf` is (newlib, `NL_CXXFLAGS`), linked with `lib/systemkit.imp.a lib/filekit.imp.a`; staged
  `apps/clockd.app/main`; `app.txt`: `name = Clock Service`, `category = Shell` (not in the dock's drawers).
- `sdcard/etc/autostart`: `run clockd` right after `run notifyd` (not a `#setup:` line: not held back by Setup). The
  Clock calls `autostart_ensure ("run clockd", "run notifyd", "# The Clock's alarms (rung with the app closed)")` at
  start, and `lx_launch ("clockd", 0)` when `kapi_ipc_lookup ("clockd")` finds none (AC-27).

### 3.6 The Clock (`main.cpp`)

- One instance: `kapi_ipc_lookup ("clock")` > 0 → `CLOCK_MSG_OPEN` with the arguments, `kapi_raise_app ("clock")`,
  exit; else `kapi_ipc_register ("clock")` (Notes' code). Arguments: none (the last tab), `world|alarms|timer|stopwatch`,
  `--ring <id>`, `--ring timer`.
- **The ring** (`--ring <id>` or the message): reload `alarms.txt`; clear that alarm's `snooze`; `notify_action
  (TR ("Clock"), "07:00 School", "clock alarms")`; the **ring overlay** (time, label, *Snooze 10 min*, *Stop*; Enter =
  Snooze, Esc = Stop) over the Alarms tab, `kapi_raise_app`; the sound pattern looped by `onTick`; after 2 min
  unanswered: the sound stops, `notify_action (TR ("Clock"), TR ("Missed alarm: ") + "07:00 School", "clock alarms")`,
  `missed = <minute>` written (R-2). Stop: once alarm → `on = 0`; Snooze → `snooze = now + snooze minutes`. Each →
  `alarms_save` + `CLOCKD_MSG_RELOAD`. Started *only* to ring (no window before) it still opens its full window — one
  code path; the UX designer may choose a ring-only small window instead (same core calls).
- **Overlays, not Modals** (§1.1 fact 4): the ring, *Time's up*, the alarm editor, the add-city list.
- `config.ini` through `fk_kv` (`[clock]` `tab, cities, snooze, timer, width, height` + R-1's `sw_*`, `timer_*`).
- Keys (02 #4): `Root::onKey` / the view's key handler with `kapi_get_modifiers () & MOD_CTRL` (Ctrl+1…4, Ctrl+Tab,
  Ctrl+N, Ctrl+C, Ctrl+Q), Space / L / R / Delete when no text field has the focus.
- Build: `FT_APPS += clock`, `FT_EXTRA_clock = Apps/clock/alarms.cpp Apps/clock/clocktime.cpp lib/audiokit.imp.a`,
  `clock.elf: … $(wildcard Apps/clock/*.h) Kits/filekit/kvtext.h Kits/systemkit/locale.h`;
  `clockd.elf: Apps/clockd/main.cpp Apps/clock/alarms.cpp Apps/clock/clocktime.cpp Apps/clock/*.h …` (the `clipd.elf`
  rule + the two `.cpp`); `clockd.elf` added to `all:` beside `clipd.elf`.

---

## 4. The simulator addition (`tools/tests/desktop_sim/fakekapi.cpp`)

`SIM_CLOCK="YYYYMMDDHHMMSS"` (opt-in; unset = today's frozen 12:34:00, so **no other screenshot changes**, AC-5):

- `get_datetime` = that wall time + `(g_ticks − 1000) / 100` seconds (one script step = 20 ms; `msleep (500)` in clockd
  = 51 ticks ≈ 0.5 s);
- `clock_info` answers (valid) with `utc = wall − SIM_TZ` (`SIM_TZ` minutes, default 120: Brussels in summer) and
  `tz_minutes = SIM_TZ`, even without `SIM_STAT`;
- `set_timezone (m)` changes `SIM_TZ`'s value for the run (the wall time follows) and logs `sim: set_timezone <m>`.

Documented in the header comment with the round-1 additions; a case in a small probe (`tools/tests/clock/
sim_probe.cpp`, as `tools/tests/notes/sim_probe.cpp`): after 100 `msleep (1000)` from `20260928123400` the date reads
12:35:41 (101 ticks a call), `clock_info` UTC = 10:35:41.

Note for the clockd sim test: `lx_launch ("clock", …)` calls `lx_exists ("SD:apps/clock.app/main")` (appkit_lib.inc) —
until the user stages, the card has no `clock.app/main`: the test puts a placeholder file into its `SIM_WRITES/apps/
clock.app/main` (the simulator reads the writes first) so that `sim: exec SD:apps/clock.app/main --ring 1` is logged.

---

## 5. Step-by-step implementation plan (each step builds and is tested before the next)

| # | Step | Files | Test (exact) |
|---|---|---|---|
| **0** | Simulator: `SIM_CLOCK`, `SIM_TZ`, `set_timezone` logged | `tools/tests/desktop_sim/fakekapi.cpp` (+ header comment); `tools/tests/clock/sim_probe.cpp`; `tools/tests/run_clock_test.sh` (created here, grows each step) | `sh tools/tests/run_clock_test.sh` → `clock: the simulator's clock: ok`; `sh tools/tests/run_notes_test.sh` still passes (nothing changes unset) |
| **1a** | SystemKit `locale_zone_offset_at` | `locale.h`, `locale.inc`, `systemkit.abi` (+76), `tools/docgen/kitdocs.py` run → `docs/12-SYSTEMKIT.md`, `docs/06` | `tools/tests/clock/zone_test.cpp`: Brussels 2026-03-29 00:59 UTC → 60, 01:00 → 120; 2026-10-25 00:59 → 120, 01:00 → 60; 2027-03-28 01:00 → 120; 2027-10-31 01:00 → 60; New York 2026-03-08 06:59 UTC → −300, 07:00 → −240; 2026-11-01 05:59 → −240, 06:00 → −300; 2027-03-14 07:00 → −240; Tokyo always 540; UTC 0; out of range 0 |
| **1b** | SystemKit `locale_zone_sync` (separable, §2.1) | `locale.h/.inc`, `systemkit.abi` (+77), docs/12 | same test linked with `fakekapi.o` + `SIM_CLOCK`: at 2026-10-25 00:59:30 UTC (wall 02:59:30, `SIM_TZ=120`, `zone=Brussels` in a writes `system.ini`) → 0 and nothing logged; one minute later → 1, `sim: set_timezone 60`, `timezone=60` written; called again → 0 (**no oscillation**); no `zone=` and no `timezone=` → 0 |
| **2** | FileKit `fk_kv_block_new / _get / _set` | `kvtext.h`, `kvtext.inc`, `filekit.abi` (+96..98), docs/14 | `tools/tests/filekit/kvtest.cpp` new cases: two `[alarm]` blocks made, a key set in the 2nd only, written then parsed back equal; `_get` on block 0 / out of range → def; `sh tools/tests/run_kvtext_test.sh` |
| **3** | The core: `clocktime.{h,cpp}`, `alarms.{h,cpp}`, `clock_proto.h` | `user/Apps/clock/` | `tools/tests/clock/alarm_test.cpp` (§8.1), `clocktime_test.cpp` (§8.2) in `run_clock_test.sh`, `-Wall -Wextra -Werror`, UBSan; built twice (`-O1`, `-O2`) |
| **4** | clockd | `user/Apps/clockd/main.cpp`, `sdcard/apps/clockd.app/app.txt` (+ `icon.bmp`), `user/Makefile` (`clockd.elf`, `all:`), `sdcard/etc/autostart` (`run clockd`) | `run_clock_sim_test.sh` cases `clockd-ring`, `clockd-once`, `clockd-reload`, `clockd-disabled`, `clockd-running` (§8.4) |
| **5** | The Clock's skeleton: window, tabs, menus, one instance, args, `config.ini`, `autostart_ensure`, clockd started, `uk_lang_init`, `lang/fr.txt`, `app.txt`, icon | `user/Apps/clock/main.cpp`, `sdcard/apps/clock.app/`, `tools/icons/clock_icon.py`, `user/Makefile` (`FT_APPS`, `FT_EXTRA_clock`), `shots.sh` (`build` extra + FT list + `APPS`) | sim: `clock stopwatch` opens on Stopwatch (log `clock: tab stopwatch`); a second start with `SIM_SERVICES=clock` logs `sim: send clock type 1 "alarms\0"` + `sim: raise_app clock`; no `clockd` service → `sim: exec SD:apps/clockd.app/main` (or `launch clockd`); `check.py clock` 0 missing |
| **6** | World tab | `main.cpp` (+ `world.h`) | sim: `cities = Tokyo,New York,London` → log rows `Tokyo 19:34 +7 h` …; add / move / remove → `config.ini`; 13th refused; no zone → *Time zone not set*, the button → `sim: exec … control … langconf` |
| **7** | Alarms tab + editor overlay | `main.cpp` (+ `alarmsview.h`) | sim: the editor creates 07:00 *School* Mon–Fri → `alarms.txt` block (AC-11), `sim: send clockd type 2`; the next-alarm line logged; FR UI writes the same tokens; 21st refused; `time = 25:99` *Invalid*; unknown key kept |
| **8** | Ringing: `--ring`, overlay, `sounds.h`, notification, Snooze / Stop / 2-min timeout | `main.cpp`, `sounds.h` | sim: `SIM_ARGS="--ring 1"` → `sim: send notify type 1 "Clock\007:00 School\0clock alarms\0"`; `key 27` → `on = 0` written; `key 13` → `snooze = …`; 6 000 steps (2 min) → *Missed alarm* sent, `missed =` written |
| **9** | Timer tab + *Time's up* + hand-over at close | `main.cpp` (+ `timerview.h`) | sim: preset 5 min → spin boxes 0/5/0; Start + 50 waits → `04:59`; a 3-s timer → `Timer — 00:03 done` sent; +1 min → `01:00`; `quit` while running → `[timer] end = …` in `alarms.txt`, `sim: send clockd type 2`; `timer = 300` in config |
| **10** | Stopwatch tab, laps, Copy Laps, start saved at close | `main.cpp` (+ `swview.h`) | sim: Space, 3 × L, Space → 3 rows logged with marks; Ctrl+C → the clipboard's text (kernel clipboard in the sim: `clip_get_text` from a probe, or the log line) |
| **11** | Keys, Ctrl+Q's question, close behaviour (R-1) | `main.cpp` | sim: Ctrl+1…4, Space, L; Ctrl+Q with the timer running → *Quit anyway?* overlay, Esc keeps it; `winclose`/`quit` → no question, state handed over |
| **12** | Screenshots EN + FR | `shots.sh` `clock` block (§8.3) | `SHOTS_PNG=<scratch> sh …/shots.sh clock`, then `SHOTS_LANG=fr …`; looked at; then into `screenshots/` |
| **13** | Docs, package | `docs/04-USER-GUIDE.md` (§12 catalog row + a *Clock* section: tabs, keys, files, clockd; the translated-apps list), `docs/03` (clockd as the example of a service without a window, the R-1 rule), `docs/06`, `tools/pkg/packages.ini` `[clock]`, `IDEAS.md` (the date kit subject; a notification history), `python docs/build_docs.py` | `python tools/lang/check.py clock` → 0 missing; `git diff main -- kernel/ user/Kits/appkit/` empty |
| *S1* | *Should*: the menu bar's bell + *Alarms and timers…* | `user/Apps/menubar/main.cpp` (the calendar pop-up's *Open Calendar* button, line ~475: a second one; the bell from `alarms_next` read every minute) | menubar sim shot unchanged when no alarm; with an alarm the bell |
| *S2* | *Should*: missed alarm at clockd's start (once alarm passed within 12 h, no sound) | `clockd/main.cpp` (`alarms.cpp` gets `alarms_missed_since`) | alarm_test case |
| *S3* | *Should*: analogue face | `main.cpp` (`VPath`) | shot |

Steps 0–3 need no window and can be done by one developer, 4–8 by a second, 9–13 by a third (the round-1 split).

---

## 6. Risks and the scope reductions

| # | Risk / infeasible ask | Decision |
|---|---|---|
| **R-1** | **AC-34 / AC-40: "Quit anyway?" on closing** cannot be asked for the close box nor the menu bar's Quit: the window's exit flag is sticky and ends every loop (§1.1 fact 3); refusing it needs a kapi (a "close requested" event that the app may decline) — out of this round. | **Reduction**: (a) the Clock's **own Ctrl+Q / Clock ▸ Quit** asks (*The timer is still running. Quit anyway?*, an overlay; Cancel keeps it); (b) the **close box never loses anything**: a running timer is handed to clockd (`[timer]` in `alarms.txt`, rung by clockd: 02's first *should* promoted to must), the stopwatch's `sw_start` (ticks) / `sw_base` / laps saved in `config.ini` and resumed at the next start within the same boot. AC-40 becomes: "Ctrl+Q with the timer or the stopwatch running asks; Cancel keeps it; the close box closes at once and the timer still rings (clockd) and the stopwatch shows the right time when reopened". A kapi for a refusable close is noted for IDEAS. |
| **R-2** | **AC-29: "*Missed alarm* stays in the notifications"** — notifyd has no history: a bubble with an action stays 8 s and is gone (`notifyd/main.cpp`, `HOLD_MS`). | **Reduction**: the *Missed alarm* bubble (with its action) is sent; the alarm's row shows *Missed at 07:00* (`missed =` in `alarms.txt`, cleared at its next ring or when the user toggles / edits it). A notification centre is noted for IDEAS (it is notifyd's, not the Clock's). |
| **R-3** | **02 #24 / AC-30: the sounds on the SoundFont** — 32 MB loaded in the ringing process (≈ 64 MB at the peak, seconds of reading) at the moment it rings, and absent from a lite card (§1.1 fact 5). | **Decision**: the three sounds on AudioKit's **FM voices** (`ak_fm_instrument` — a bell-like 2-operator patch for *Chimes*, square for *Beeps*, a soft decay for *Marimba* — and `ak_fm_start / _stop`), as the games' effects: no file, instant, a few KB. AC-30's "SoundFont removed" case becomes moot (nothing to remove); "no audio output / muted" stays (*Sound unavailable* when `ak_fm_start` fails or `ak_play_state () == AK_BUSY`). Same tokens in the file; the SoundFont can come later behind the same `sounds.h`. |
| R-4 | **Another program holds the sound output** (the Media Player playing): AudioKit's player says `AK_BUSY`, the alarm is **silent** (the dialog and the notification still come). | Said in the status line (*Sound unavailable*), in docs/04, and checked on the Pi (AC-30). A priority output for alarms would be AudioKit's / the kernel's — IDEAS. |
| R-5 | **The clock jumps** at boot (restored last time, then NTP) — a night's alarms rung at once, or an alarm of the minute after the shutdown rung at the next boot. | The 2-minute window, the backward-jump rule, and the 90-s boot guard (§3.3); unit-tested (§8.1 cases 9–11). Residual: an alarm due in the first 90 s after boot is not rung (said in docs/04). |
| R-6 | **Summer time**: the system's clock does not change on the night (§1.1 fact 1); `locale_zone_summer` is day-only. | §2.1: `locale_zone_offset_at` (from UTC, the hour counted) + `locale_zone_sync` in clockd (separable step 1b). Alarms are counted in wall calendar days (AC-23); on the night summer time ends, the repeated wall hour does not ring twice (§3.3 rule 2); on the night it begins, an alarm inside the skipped hour (02:30) is **not rung** that day (the wall minute never comes; the window is 2 min) — said in docs/04. |
| R-7 | **The ring's start time**: clockd → `lx_launch ("clock", "--ring 1")` → an FT app starting (FreeType, its face) ≈ 1–2 s on the Pi before the dialog and the bubble (02 #16 asks 2 s). | clockd decides on the minute within 0.5 s; the Clock sends the notification **before** building its window. Measured on the Pi (AC-28). If too slow: clockd sends the word-free bubble at once and the Clock only the dialog (one flag). |
| R-8 | **Dialogs blocking the clock** (`Modal::run` without `onTick`, §1.1 fact 4). | Overlays only (§3.6); a test: the timer reaches zero while the alarm editor is open → *Time's up* comes (step 9). |
| R-9 | **Two writers**: the Clock is the only writer of `alarms.txt`; two Clocks cannot run (one instance). clockd only reads; a hand edit while the Clock runs is overwritten at its next save (as Notes' notes.ini). | Said in the file's comment line. |
| R-10 | **The simulator**: one program a run — clockd and the Clock are tested apart (clockd's `exec` logged, the Clock started with `--ring` by `SIM_ARGS`); no sound in the simulator (AudioKit's FM voices render into nothing audible; the calls are logged by the app: `clock: sound chimes`). | AC-28–31, 36 stay [Pi] as 02 says. |
| R-11 | Kit ABI lines appended without an AArch64 build here (`systemkit.abi` 76–77, `filekit.abi` 96–98). | libgen run on the host objects against a copy of the old `.abi` (round 1's method); "to be confirmed by the user's `make`" in 06-development.md. |

---

## 7. Package, docs, files on the card

- `tools/pkg/packages.ini` (declared, **not published** — 01 MUST 8):
  ```ini
  [clock]
  title    = Clock
  category = Productivity
  summary  = The time here and around the world, alarms that ring with the app closed (clockd), a timer, a stopwatch
  icon     = apps/clock.app/icon.bmp
  files    = apps/clock.app/ apps/clockd.app/
  config   = apps/clock.app/config.ini apps/clock.app/alarms.txt
  needs    = uikit, systemkit >= <the version with line 77>, filekit >= <the version with line 98>, audiokit, fontkit
  ```
  (the exact `>=` numbers as the onyx-packages skill computes them; `sdcard/etc/autostart` stays the `onyx` package's —
  the Clock's `autostart_ensure` covers updated cards.)
- `sdcard/apps/clock.app/app.txt`: `name = Clock`, `category = Productivity`; `lang/fr.txt` (`Clock	Horloge`, …).
- `sdcard/apps/clockd.app/app.txt`: `name = Clock Service`, `category = Shell`.
- Docs: docs/04 (catalog + section), docs/03, docs/06, docs/12 and docs/14 (generated), `python docs/build_docs.py`.

---

## 8. The tests

### 8.1 The alarm logic — `tools/tests/clock/alarm_test.cpp` (PC, no kapi, fake "now", deterministic)

Links `user/Apps/clock/alarms.cpp`, `clocktime.cpp` and FileKit's inline `kvtext.inc` (no `fakekapi.o` needed: the core
takes `now` as a wall minute; the file is given as text to `alarms_parse`). A helper `W (y,mo,d,h,mi)` =
`clk_minute (...)`; "stepping second by second" = calling `ringer_step` with `W` of each second (the minute changes
every 60 calls — what clockd does every 0.5 s).

| # | Case | Expected | AC |
|---|---|---|---|
| 1 | 07:00 mon–fri, now stepped Mon 2026-09-28 06:58:00 → 07:03:00 | rung once, at the step of 07:00:00 (0 s late ≤ 2 s) | 17 |
| 2 | same, `alarms_parse` again at 07:00:30 (the reload), stepping on | still once | 17 |
| 3 | `alarm_next` from Fri 2026-10-02 08:00 → Mon 2026-10-05 07:00; from Sun 2026-10-04 23:59 → Mon 07:00; nothing on Sat / Sun over a stepped weekend | | 18 |
| 4 | `on = 0` alarm and a deleted one (parse without it), stepped minute by minute over the week 2026-09-28 → 10-05 | never due | 19 |
| 5 | once `date = 20260929`, 07:00, stepped 09-28 → 10-06 | due once at 2026-09-29 07:00 | 20 |
| 6 | snooze 10 at 07:00:20 → `alarm_snooze` gives `snooze = 202609290710` (written text checked); stepped to 07:30 | due at 07:10 (snooze) once, not at 07:20 | 21 |
| 7 | ringer started at 09:00 with a 07:00 alarm that day | nothing | 22 |
| 8 | now 06:59:50 → jump to 07:01:30 | 07:00 due | 22 |
| 9 | now 06:59 → jump to 07:03 | nothing | 22 |
| 10 | a jump back of 60 min at 03:00 → 02:00 (summer time ending) with a 02:30 daily alarm, stepped to 04:00 | 02:30 due **once** (the first time), not again | 23, R-6 |
| 11 | a jump back of 5 h (the clock set), then forward normally | rings resume at once after the jump | R-5 |
| 12 | `trusted = false` for the first 90 s, an alarm in that window | not due; the next minute after the guard rings normally | R-5 |
| 13 | daily 07:00, `alarm_next` from Sat **2026-10-24 08:00** → **2026-10-25 07:00**, then from 10-25 07:01 → **2026-10-26 07:00**; from Sat **2027-03-27 08:00** → **2027-03-28 07:00** → **2027-03-29 07:00** | (calendar days, the change night one day) | 23 |
| 14 | `alarm_once_day` at Mon 12:34 for 10:00 → `20260929`; for 13:00 → `20260928`; for 12:34 → tomorrow | | 13 |
| 15 | `alarms_next` at Mon 12:34:00, only the mon–fri 07:00 → tomorrow, 1 106 min (18 h 26); + once 14:30 today → today, 116 min (1 h 56); all off → none | | 12 |
| 16 | `alarm_repeat_kind`: mon–fri WEEKDAYS, all DAILY, sat sun WEEKENDS, mon wed fri CUSTOM, none ONCE | | 14 |
| 17 | parse a hand-written file: an unknown key `colour = red`, a block `time = 25:99`, 22 blocks | unknown key back after `alarms_save` (text compared); 25:99 `valid = false`, never due; 20 kept | 16, 15 |
| 18 | create 07:00 *School* mon–fri chimes, `alarms_save` | exactly the five lines of AC-11, the `id`, `date =`, `snooze =` empty | 11 |
| 19 | `[timer] end = T, set = 600`: due at tick T, not before; a stale entry (`end` > now + set·100 + 100) never | | 33, R-1 |

### 8.2 World and formats — `tools/tests/clock/clocktime_test.cpp`, `zone_test.cpp`

- `zone_test.cpp`: §5 step 1a's instants; step 1b's sync (with `fakekapi.o`, `SIM_CLOCK`, a writes `system.ini`).
- World rows at UTC 10:34 on 2026-09-28: Tokyo `19:34` `+7 h` today; New York `06:34` `−6 h`; London `11:34` `−1 h`
  (AC-7); here UTC+2 summer (AC-6's offset); at wall 23:30 Brussels (UTC 21:30) Tokyo `06:30` *tomorrow*; at wall
  01:00 (UTC 23:00 the day before) Los Angeles `16:00` *yesterday* (AC-8); a half-hour difference text `+5 h 30`
  (a synthetic offset); *Same time* for Paris.
- Timer: started at tick T for 600 s; at T + 59 940 ticks (599 400 ms) `00:01`; at T + 60 000 rings; frames at
  irregular steps (1, 7, 33 ticks) give the same left time (AC-34).
- `fmt_sw`: `00:12.34`, `59:59.99`, `1:00:00.00`; laps 1234, 2424, 3700 → lap times 12.34 / 11.90 / 12.76, the
  sum = the total, fastest lap 2, slowest lap 3; `laps_text` equal to 02 #33's block (AC-39, 38).

`sh tools/tests/run_clock_test.sh` builds each test twice (`-O1 -fsanitize=undefined`, `-O2`), `-Wall -Wextra -Werror`
on the Clock's sources → `clock: all N checks passed`.

### 8.3 The screenshots — `shots.sh` `clock` block

Fixtures: `tools/tests/desktop_sim/clock/{config.ini, alarms.txt}` — `cities = Tokyo,New York,London`; three alarms:
07:00 *School* mon–fri chimes on, 14:30 *Medicine* once `date = 20260928` marimba on, 09:00 *Gym* sat sun beeps off.
Copied into `$OUT/writes/apps/clock.app/` before each run (`kfix`, as Critters' `cfix`). `SIM_SERVICES=notify,clockd`.

```sh
if want clock; then			# (Clock, AutoDev round 6: the fixed clock Mon 2026-09-28 12:34:00, Brussels summer time)
	KQ="$OUT/writes/apps/clock.app"; KS="SIM_SERVICES=notify,clockd"
	kfix () { rm -rf "$KQ"; mkdir -p "$KQ"; cp $D/clock/config.ini $D/clock/alarms.txt "$KQ/"; }
	kw () { printf 'wait;%.0s' $(seq 1 $1); }
	kfix; sim clock clock "wait;wait;$W" $KS SIM_ARGS=world; png clock                          # World: here + 3 cities
	kfix; sim clock clock-alarms "wait;wait;$W" $KS SIM_ARGS=alarms; png clock-alarms             # 3 alarms, the next-alarm line
	kfix; sim clock clock-timer "wait;wait;<preset 5 min click>;key 32;$(kw 120)" $KS SIM_ARGS=timer; png clock-timer   # 04:58, the ring
	kfix; sim clock clock-stopwatch "wait;key 32;$(kw 600)key l;$(kw 590)key l;$(kw 640)key l;key 32;$W" $KS SIM_ARGS=stopwatch; png clock-stopwatch   # 3 laps
	kfix; sim clock clock-ring "wait;wait;$W" $KS "SIM_ARGS=--ring 1"; png clock-ring            # the ringing overlay
	kfix; sim clock clock-edit "wait;wait;key 13;$W" $KS SIM_ARGS=alarms; png clock-edit          # the alarm editor
	kfix; lang fr; sim clock clock-fr "wait;wait;$W" $KS SIM_ARGS=world; png clock-fr
	kfix; sim clock clock-alarms-fr "wait;wait;$W" $KS SIM_ARGS=alarms; png clock-alarms-fr
	kfix; sim clock clock-ring-fr "wait;wait;$W" $KS "SIM_ARGS=--ring 1"; png clock-ring-fr
	lang "$SHOTS_LANG"; rm -rf "$KQ"
fi
```

(The click positions are the UX designer's layout's; `SHOTS_LANG=fr sh tools/tests/desktop_sim/shots.sh clock` renders
every one of them in French as well — AC-3: the four tabs + the ring, EN and FR, *Chronomètre*, *Rappel dans 10 min*
checked to fit.) The stopwatch shot: 600 + 590 + 640 waits ≈ 12.0 / 11.8 / 12.8 s laps (20 ms a step, the `key` steps included) — deterministic.

### 8.4 Scripted checks — `tools/tests/run_clock_sim_test.sh` (the Critters one's shape)

| Case | Program, variables, script | Asserted | AC |
|---|---|---|---|
| clockd-ring | `clockd --grace 0`, `SIM_CLOCK=20260928123400`, `alarms.txt` 12:35 once today, placeholder `apps/clock.app/main`, `SIM_SERVICES=notify`; `waitlog 400 clockd: ring` then 200 waits | exactly one `sim: exec SD:apps/clock.app/main --ring 1`, between steps for 12:35:00 and 12:35:02 | 24 |
| clockd-running | same, `SIM_SERVICES=notify,clock` | `sim: send clock type 1 "--ring 1\0"`, no exec | 24 |
| clockd-reload | same + `SIM_MBOX="@2000:2:9:"` (RELOAD mid-run) + `copy` of a file adding 12:36 | 12:35 once, 12:36 once | 15, 17 |
| clockd-disabled | `on = 0` | nothing over 6 000 steps | 19, 22 |
| clockd-late | `SIM_CLOCK=20260928090000`, a 07:00 alarm | nothing | 22, 31 |
| clockd-sync | `SIM_CLOCK=20261025025930` (wall, `SIM_TZ=120`), `zone=Brussels` | `sim: set_timezone 60` once | R-6 |
| clock-one | `clock alarms`, `SIM_SERVICES=clock` | `sim: send clock type 1 "alarms\0"`, `sim: raise_app clock`, no window | 26 |
| clock-clockd | `clock`, `SIM_SERVICES=notify` (no clockd) | `clockd` started (`sim: exec …clockd.app/main` / `launch clockd`) | 27 |
| clock-ring | `clock --ring 1`, `key 27` | notify `Clock\007:00 School\0clock alarms\0`; Stop: once alarm `on = 0` written; `sim: send clockd type 2` | 24, 25 |
| clock-snooze | `--ring 1`, `key 13` | `snooze = 202609281244` (12:34 + 10) | 18, 25 |
| clock-missed | `--ring 1`, 6 100 waits | *Missed alarm: 07:00 School* sent, `missed =` written | 20, R-2 |
| clock-world | `world`, the fixtures | log `Tokyo 19:34 +7 h`, `New York 06:34 -6 h`, `London 11:34 -1 h`, here `12:34:00 UTC+2 summer` | 6, 7 |
| clock-cities | Ctrl+N + Enter, move up, remove; restart | `cities =` in order; 13th refused (log) | 9 |
| clock-nozone | writes `system.ini` without `zone`/`timezone` | *Time zone not set*, the button → `control langconf` exec | 10 |
| clock-alarm-new | editor: 07:00, *School*, Weekdays, OK — EN and `language=fr` | the AC-11 block, the same tokens in French | 11, 15 |
| clock-next | the fixtures (and all off) | log `next: tomorrow 07:00 in 18 h 26 min` / `today 14:30 in 1 h 56 min` / `none` | 12 |
| clock-21 | 20 alarms, Ctrl+N | refused | 15 |
| clock-invalid | `time = 25:99`, an unknown key, then a toggle | *Invalid* row; the key kept in the file | 16 |
| clock-timer | preset 5, Start, 50 waits, Pause, Reset | `05:00`, `04:59`, held, `05:00`; spin boxes disabled while running (log) | 32 |
| clock-timesup | 3-s timer, 160 waits | `Timer — 00:03 done` sent; `+1 min` → `01:00` | 33 |
| clock-timer-close | running timer, `quit` | `[timer]` in `alarms.txt`, `sim: send clockd type 2`, `timer = 300` in config | 35, R-1 |
| clock-sw | Space, 3 × L, Space, Ctrl+C, R | 3 rows newest first, marks, the clipboard text (kernel clipboard), cleared; R ignored while running | 37, 38 |
| clock-keys | Ctrl+1…4, Space on Timer, L, `clock stopwatch` | the tab logs | 41 |
| clock-quit | Ctrl+Q with the stopwatch running, Esc; then Ctrl+Q, Enter | asked; kept; then quits, `sw_start` saved | 40 (reduced) |

---

## 9. Acceptance criteria → steps → tests

| AC | Step(s) | Test | Note |
|---|---|---|---|
| 1 build, Makefile, app.txt | 4, 5 | host builds in `run_clock_*`; Pi `make` by the user | AArch64 build not here |
| 2 check.py 0 | 5–11, 13 | `python tools/lang/check.py clock` | clockd has no words (§3.5) |
| 3 shots EN/FR | 12 | §8.3 | |
| 4 docs, autostart, package | 4, 13 | review; `build_docs.py` runs | |
| 5 no kernel/kapi change, other shots unchanged | all (0: opt-in) | `git diff main -- kernel/ user/Kits/appkit/`; shots diff | SystemKit/FileKit change, not AppKit |
| 6 here 12:34:00, date, UTC+2 summer | 1a, 6 | clocktime_test; clock-world; shot | |
| 7 Tokyo/NY/London | 1a, 3, 6 | clocktime_test; clock-world | |
| 8 tomorrow / yesterday | 3 | clocktime_test | |
| 9 cities in config, 12 max | 6 | clock-cities | |
| 10 no zone | 6 | clock-nozone | |
| 11 alarm block written | 2, 3, 7 | alarm_test 18; clock-alarm-new | |
| 12 next-alarm line | 3, 7 | alarm_test 15; clock-next | |
| 13 once date | 3, 7 | alarm_test 14 | |
| 14 repeat words | 3, 7 | alarm_test 16; shot EN/FR | |
| 15 rewrite + RELOAD; 21st refused | 7 | clock-alarm-new, clock-21, clockd-reload | |
| 16 unknown key kept; 25:99 invalid | 2, 3, 7 | alarm_test 17; clock-invalid | |
| 17 exactly once, reload | 3 | alarm_test 1–2; clockd-reload | |
| 18 not weekend, next Monday | 3 | alarm_test 3 | |
| 19 disabled / deleted never | 3, 4 | alarm_test 4; clockd-disabled | |
| 20 once rings once | 3 | alarm_test 5 | |
| 21 snooze | 3, 8 | alarm_test 6; clock-snooze | |
| 22 missed / late window | 3, 4 | alarm_test 7–9; clockd-late | |
| 23 summer time dates | 3 (+1a) | alarm_test 10, 13 | |
| 24 clockd notifies + starts the ring | 4, 8 | clockd-ring, clockd-running, clock-ring | **changed**: the notification is sent by the ringing Clock (§3.5); clockd's own only as a word-free fallback |
| 25 ring dialog, Esc/Enter, on = 0 | 8 | clock-ring, clock-snooze; shot | |
| 26 one instance | 5 | clock-one | |
| 27 starts clockd | 5 | clock-clockd | |
| 28 [Pi] rings with the app closed | 4, 8 | by hand on the Pi | R-7 timing |
| 29 [Pi] 2-min stop, *Missed* stays | 8 | clock-missed (PC part); Pi by hand | **reduced** (R-2): the bubble + the row's *Missed at* |
| 30 [Pi] sounds, muted, no SoundFont | 8 | Pi by hand | **changed** (R-3): FM voices; *no SoundFont* moot; *output busy* added (R-4) |
| 31 [Pi] reboot | 4 | clockd-late (PC part); Pi by hand | |
| 32 timer presets, run, pause, reset | 9 | clock-timer | |
| 33 time's up, +1 min | 9 | clock-timesup; alarm_test 19 | |
| 34 time from ticks | 3 | clocktime_test | |
| 35 timer = 300 kept | 9 | clock-timer-close | |
| 36 [Pi] 10 min accuracy | 9 | Pi by hand | |
| 37 laps, marks, reset | 10 | clock-sw | |
| 38 copy laps | 10 | clock-sw; clocktime_test | *Laps copied* in the status line (clipd already bubbles *Text copied*) |
| 39 formats, sum | 3 | clocktime_test | |
| 40 close with timer / stopwatch running | 9, 10, 11 | clock-quit, clock-timer-close | **reduced** (R-1): Ctrl+Q asks; the close box hands over |
| 41 keys | 11 | clock-keys | |

**02's must items vs this plan**: 02 #1–33 kept, with #17 (who sends the bubble: the Clock), #20 (*Missed* not kept by
notifyd), #24–25 (FM voices) and #34 (the question only on Ctrl+Q; the timer handed to clockd, the stopwatch resumed —
02's first two *should* items promoted) adjusted as above. Not added: anything from 02's *later* list.

---

## 10. GUI plan (appended by the UX Designer, 2026-10-07 — `04-ux-design.md`, mock-ups in `mockups/`)

**No UIKit addition.** Everything in `04-ux-design.md` is UIKit's existing widgets (`SegmentedControl`, `ToolButton`,
`Button`, `NumericUpDown`, `Textbox`, `LcdDisplay`, `DataGrid` + `cellDraw`, `Menu`) and drawing (`VPath::arc`,
`uk_switch_mark`, `uk_sunken`, `uk_rbox`, `uk_title_strip`, `uk_glyph`), plus a few widgets drawn **beside the app**
(`HereCard`, `CityList`, `NextAlarmBar`, `AlarmList`, `TimerRing`, `StopwatchFace`, `FootText`, `Veil`) — proven by the
mock-ups, which build and run against today's UIKit in the simulator (`mockups/mockups.sh`).

### 10.1 What was changed in this plan (and why)

| # | Was | Now | Why (checked in the code) |
|---|---|---|---|
| G1 | R-1 (a): "the Clock's own Ctrl+Q / Clock ▸ Quit asks *Quit anyway?*" (step 11, test `clock-quit`, AC-40 reduced) | **No question at all.** The close box and Ctrl+Q both close at once and **hand over** (R-1 (b) unchanged: `[timer]` to clockd, the stopwatch's `sw_*` saved). AC-40 becomes: "closing (close box or Ctrl+Q) with the timer or the stopwatch running asks nothing; the timer still rings (clockd → `clock --ring timer` → *Time's up*), the stopwatch shows the right time when reopened within the boot". | `Menu::shortcut` (user/Kits/uikit/menu.cpp:61) turns `UK_CTRL ('Q')` into `kapi_menu_command (MENU_QUIT)` **before** the key reaches the app (root.cpp:452: menu shortcuts first) — the same sticky exit as the close box; asking would need a UIKit change, and with the hand-over nothing is lost. |
| G2 | 03 §3.6: "the UX designer may choose a ring-only small window" | **The full window** on the Alarms tab with the ring card (one code path); **started only to ring** (`--ring` with no Clock running), it **closes by itself** after Stop / Snooze / the 2-minute timeout. | 04 D15. A flag set in `main` (`g_ringOnly`); the card is modal, so the user cannot have started anything else. |
| G3 | "Overlays (real widgets as the root's children, focus taken, driven by onTick)" | Concretely: **`Veil : Widget`**, the client area's size, added last, **`modal = true`**, the card's controls **its children**; its `onDraw` copies the root's canvas under it (already composed: `Widget::draw` blits the earlier siblings first) mixed 96/256 toward black, then the card. Veils stack; closing = `removeChild` + focus back. | `modal` gives the routing (widget.cpp `handleMouse` / `handleKey`: only the topmost modal child; Tab walks its controls) and **switches the menu shortcuts off** (`menu.cpp` `modal_open ()`): Ctrl+C in the label `Textbox` is the field's, Ctrl+Q is inert while a card shows. Not `Modal::run`: the root's loop and `onTick` go on (fact 4). |
| G4 | Keys "Ctrl+1…4, Ctrl+N… through `Root::onKey`" (§3.6) | Menu items for them are `item (label, "^1", 0, cb)` — **key 0** — and the keys are handled in the root's `onKey` (`kapi_get_modifiers () & MOD_CTRL` and `k == '1'…'4'`); only *Copy Laps* is bound (`UK_CTRL ('C')`). | `UK_CTRL ('1')` = `'1' & 0x1F` = 0x11 = **`UK_CTRL ('Q')`** (and '2'…'4' = ^R ^S ^T); the Spreadsheet (`sheet/main.cpp:1440, 1750`) and the PDF Viewer do the same. Ctrl+N means two things by tab; Space / L / R / Delete in a menu would be taken from the fields. |
| G5 | §8.3: the screenshots `clock.png`, `clock-fr.png`, … | **`clock-world.png`**, `clock-alarms`, `clock-edit`, `clock-cities`, `clock-ring`, `clock-timer`, `clock-timesup`, `clock-stopwatch`, and each `-fr`. **Never `clock.png`.** | `screenshots/clock.png` **already exists**: the menu bar's calendar pop-up, made by the existing `if want clock` block (`tools/tests/desktop_sim/shots.sh:596`). The new block goes after it in the same `want clock`; AC-5 ("other screenshots unchanged") then holds. |
| G6 | 02 #11: the time as "two spin boxes" | Two `NumericUpDown`s **and an `LcdDisplay`** showing `07:00` beside them. | `NumericUpDown::onDraw` (controls.cpp:150) prints the value unpadded (`7`, `0`); no UIKit change for a padded spinner. |
| G7 | — | The Timer's start and Reset buttons **stacked** full width; the Stopwatch's two buttons 164 px. | *Remettre à zéro* did not fit two 121-px buttons side by side (the French mock-ups, 04 §2.1). |
| G8 | — | Widgets resized after creation use **`resizeTo`**, not `width =`. | A `ToolButton` / `Button` whose `width` is changed keeps its canvas (the mock-ups' first run drew cut buttons). |

### 10.2 Which step gets which GUI work (the steps of §5; files beside the app)

| Step | GUI work (04's decisions) | Files |
|---|---|---|
| 5 | `ClockRoot` 560 × 440, `setResizable (true)`, `setMinSize (560, 440)`, size from `config.ini`; the `SegmentedControl` tab bar + its line (D2); **one container `Widget` per tab** (shown / hidden, anchored fill); the footer pattern `FootText` + right-aligned `ToolButton`s (D3); the `Menu` of 04 §6 (G4) and the key dispatcher of 04 §7; the lazy face cache (04 §4); **`ui.h`**: `Veil`, `FootText`, the drawings (`bell`, `slash`, `globe`, `sun`, `moon`, `hourglass`, `warn`, `speaker_off`, `pin`), the meaning colours with their dark-theme twins; the app icon (`tools/icons/clock_icon.py`, 04 §1.1); `lang/fr.txt` started from `mockups/sd/apps/clock.app/lang/fr.txt` | `main.cpp`, `ui.h` (new), `tools/icons/clock_icon.py` |
| 6 | `HereCard` (D4), `CityList` (D5, Ctrl+Up / Down), the empty state (D6), the no-zone warning + `Button` (D7), the **Add a City** veil: `Textbox` filter + `DataGrid` City · Time · UTC (D8); the cities' names through `TR` with `// TR:` marks (as `langconf`) | `world.h` |
| 7 | `NextAlarmBar` (D9), `AlarmList` with the switch's hit area, the states *Snoozed* / *Missed* / *Invalid* (D10), the empty state (D11), the **editor** veil: `LcdDisplay` + 2 `NumericUpDown` + *Next:* line, `Textbox`, 7 toggle `ToolButton`s + *Every day* / *Weekdays*, `SegmentedControl` sounds + *Test* (D12); *Not saved* in the footer | `alarmsview.h` |
| 8 | The **ring** veil (D13) and its no-sound line (D14); `g_ringOnly` closing (G2) | `main.cpp` |
| 9 | `TimerRing` (`VPath::arc`, D17), the right column (3 `NumericUpDown` disabled while running, 5 preset toggles, the stacked start / Reset), the **Time's up** veil (D18); a veil over a veil (R-8's test) | `timerview.h` |
| 10 | `StopwatchFace` (D19), the Lap / Reset and Start / Stop `ToolButton`s, the laps `DataGrid` + `cellDraw` (fastest / slowest: colour + arrow + word), *Copy Laps* and *Laps copied* in `FootText` | `swview.h` |
| 11 | The keys of 04 §7; **no question** (G1); the hand-over at every exit | `main.cpp` |
| 12 | The `shots.sh` block (G5), below | `tools/tests/desktop_sim/shots.sh`, `tools/tests/desktop_sim/clock/` |

### 10.3 Tests changed or added (§8.4)

- `clock-quit` → **`clock-quit-hand`**: the stopwatch running, `key 17` (Ctrl+Q's code); assert: no veil logged, the
  program ends (if the simulator's `kapi_menu_command (MENU_QUIT)` does not end it, `quit` as the close box), `sw_start`
  in `config.ini`; the same with the timer running → `[timer]` in `alarms.txt` and `sim: send clockd type 2`.
- **`clock-ring-only`**: `clock --ring 1` (no `clock` service), `key 27` → the program ends by itself (log
  `clock: ring-only, closing`); `clock-ring` with `SIM_SERVICES=notify,clock`… (the ring delivered to a running Clock by
  `CLOCK_MSG_OPEN`, `SIM_MBOX`) → after Esc it keeps running.
- **`clock-veil-stack`** (R-8): the editor open (`key 13` on Alarms), a 3-s timer running (fixture `timer = 3`, started
  from the Timer tab first) → *Time's up* logged over the editor; Esc closes it, the editor is still there.
- `clock-keys`: Ctrl+1…4 sent as `mods 1;key 49…52;mods 0` (not `UK_CTRL`): the tab logs.

### 10.4 The screenshots (§8.3 amended)

After the existing menu-bar `want clock` lines, in the same block (`SIM_SERVICES=notify,clockd`, the fixtures of §8.3;
a second config fixture `config-3s.ini` with `timer = 3` for *Time's up*):

| Shot | `SIM_ARGS` | Script (positions at the default 560 × 440) |
|---|---|---|
| `clock-world` | `world` | `wait;wait;$W` |
| `clock-cities` | `world` | `wait;mods 1;key 14;mods 0;wait;$W` (Ctrl+N) |
| `clock-alarms` | `alarms` | `wait;wait;$W` |
| `clock-edit` | `alarms` | `wait;key 13;wait;$W` (Enter on the focused list: *School*) |
| `clock-ring` | `--ring 1` | `wait;wait;$W` |
| `clock-timer` | `timer` | `wait;down 421 193;up 421 193;key 32;` + 120 waits (the *5 min* preset at its centre, Space) |
| `clock-timesup` | `timer` (config-3s) | `wait;key 32;` + 170 waits |
| `clock-stopwatch` | `stopwatch` | as §8.3 (Space, 3 × L, Space) |

`SHOTS_LANG=fr` renders the same names with `-fr` (AC-3: the four tabs + the ring, plus the editor, the city
picker and *Time's up*). The pictures must look like `mockups/clk-*.png` (the real data: the timer at 04:58 rather than
the mock's 03:12).

### 10.5 Acceptance criteria touched

- **AC-3**: the screenshot names of G5 (not `clock.png`); the editor, the city picker and *Time's up* added to the
  four tabs and the ring.
- **AC-25**: + "started only to ring, the Clock closes by itself after Stop / Snooze" (G2).
- **AC-40**: as G1 (no question; the hand-over on every exit).
- **AC-41**: Ctrl+1…4 are handled in `onKey` (G4); unchanged otherwise.
