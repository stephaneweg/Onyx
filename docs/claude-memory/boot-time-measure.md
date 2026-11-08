---
name: boot-time-measure
description: "How to time Onyx's boot on the Pi: since kapi v100 kmsg prints each line's time since boot and keeps 1024 events; the 2026-10-27 figures (kernel 3.5 s to the display)"
metadata:
  type: reference
---

Since 2026-10-27 (kapi v100, Circle patch 37) `kmsg` prints `[   12.345]` = seconds since the chip's start
(the system timer: the firmware's time is included, the first kernel line is at ~4.5 s) and Circle's event
queue keeps 1024 events, so `kmsg` over telnet right after a reboot shows the whole boot. Milestones:
`logger: Circle ... started` (kernel start), `wsrv: the graphics server (...) has the display`,
`pocketui: home` / `kernel: launch: menubar`, `kernel: net: up`, `app: telnetd: listening`.
Script: reboot over telnet, wait for port 23, run `kmsg` (scratchpad bootkmsg.py, 2026-10-27).

Measured 2026-10-27 (Pi 4 8 GB, pocket 640x480): firmware 0-4.5 s; SD 5.3 s; USB enumeration 5.4-7.2 s;
the app-dir listing ~0.75 s (one log line per app, ~6 ms each); graphics server has the display 8.0 s
(**3.5 s of kernel**), shell home 8.5 s, network 23.2 s (Wi-Fi association > 10 s, one DHCP retry). About 7 s
pass between the `reboot` command and the timer's zero. Desktop mode measured earlier by net trial:
Elegant has the display 16-17 s after `reboot`.

Before v100 (an older kernel): kmsg had no times and only the last 50 events; the fallback was a no-change
net trial (`secs=N` alone in SD:/etc/net-trial.txt dumps Circle's 16 KB text ring to net-trial.log).
See [[pi-remote-access]], [[pi-net-trial]].
