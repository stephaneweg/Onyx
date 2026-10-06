Sample files for the desktop simulator's screenshots (`SIM_OVERLAY`, see `../fakekapi.cpp` and
`../shots.sh`): read instead of the SD card's own, so the pictures show some content (a note in
tinypad, appointments in the calendar and the agenda) without touching `sdcard/`.

`Notes/` -- the sample notes of Notes and Stickies (AutoDev round 1; the mock-ups' six notes, dated for the
simulator's fixed clock, Monday 28 September 2026 12:34): `note-*.txt` + `notes.ini` (three pinned). An
overlay folder cannot be listed (`sdpath ()` finds only files there): a scenario copies `Notes/` into a fresh
`SIM_WRITES` first. `notes2.ini`: the same notes changed by "another program" (Shopping unpinned, Books to
read pinned and blue, Wi-Fi at the club touched) -- the script step `copy` puts it in mid-run (Stickies'
next poll). `notes-crlf.txt`: a note put there by hand on a PC (`\r\n` lines, no `notes.ini` section).
