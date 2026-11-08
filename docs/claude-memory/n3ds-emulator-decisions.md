---
name: n3ds-emulator-decisions
description: "The 3DS emulator: the user's choices (option C, Old 3DS first; 2026-10-10: V3D for the fragments, the app modelled on ndsemu, then the sound), how I test it on the Pi"
metadata:
  type: project
---

The study is `docs/3DS-EMULATOR-STUDY.md` (plan T0-T8). The user answered on 2026-10-09: **option C** (our own core
under MIT, Dynarmic 0BSD for the ARM11), **Old 3DS only to begin with**, and "tu peux y aller, je te fournirai la rom
de test au moment venu" (he had asked how *Zelda: A Link Between Worlds* would run).

**T0 done the same day** (study §9b, HANDOFF's 3DS section): Dynarmic vendored unpatched in `third_party/`, built by
`user/Libs/dynarmic/Makefile`, `sh tools/tests/run_n3ds_t0.sh` passes under qemu and on the Pi. Next: T1.

**How to apply:** do not re-ask the option or Old/New 3DS; ask for the test ROM only when a phase needs a retail game
(T5). The app `n3dsemu` still starts with a mock-up ([[mockup-first-for-apps]]). WSL work folder `~/n3ds` (Dynarmic's
clone, Boost, CMake builds) is only for re-running `tools/n3ds/vendor_dynarmic.py`. T1's first slice (memory, CPU, kernel,
ELF loader; `sh tools/tests/run_n3ds_test.sh`, 137 checks) was done the same day; the ARM toolchain for its test
programs is in `~/toolchains` (see [[nds-emulator-local-tests]]). Other sessions push to main meanwhile: a
`--ff-only` merge can abort -- check its output before committing. See [[nds-emulator-local-tests]].

**2026-10-09, later:** the user said "continue ... et poursuis jusqu'à la fin" (after the GPU test): work through the
list in HANDOFF's 3DS section without stopping for approval, committing and pushing each step. Their ROM for T5:
`C:\Temp\Roms\ZeldaLinkBetweenWorlds\ZeldaLinkBetweenWorlds.3ds` (decrypted, EU). Homebrew to try: `sdcard/roms/3ds/`.

**2026-10-10 (the user's choices that day):** after the software renderer's numbers he chose **the V3D** for the
fragments ("V3D"), allowed the trials of generated shaders on his Pi ("tu peux lancer les essais sur le pi"), then asked
for **the app "inspire-toi de notre emulateur DS"** (that settled its screens: no separate mock-up round) and then
**the sound**. He relays suggestions from other assistants (Gemini): answer each on its merits, briefly.

**How I test on the Pi (my own practice, scratchpad scripts, not in the repo):** FTP the stripped binary (start
`run SD:/bin/ftpd SD:/` first), run `n3dstest` from telnet with a second session in `kmsg`; a program started with
`run` dies when its telnet session closes -- keep the session open while capturing the screen through VNC (a 40-line
RFB client, raw encoding). An app core that faults at "impossible" places after an upload = stale instruction cache
(see the study's section 9l). The Pi was unreachable at the end of 2026-10-10: the app's sound was not heard there.
Packages: `publish.sh --no-push` from WSL into a fresh clone, then push from Windows ([[wsl-webkit-build-env]]).
