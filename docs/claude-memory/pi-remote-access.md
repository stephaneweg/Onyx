---
name: pi-remote-access
description: "The user's Pi on the LAN (192.168.0.7): telnet (port 23) and VNC to deploy through packages and test it myself — allowed by the user on 2026-10-03"
metadata:
  node_type: memory
  type: reference
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-05T02:57:19.274Z
---

The user's Raspberry Pi running Onyx was at **192.168.0.7** (2026-10-03) and at **192.168.0.10** on 2026-10-08 (DHCP: when the address is silent, look in `arp -a` for a d8-3a-dd MAC).
It runs `telnetd` (TCP 23, no password: `tools/onyx-telnet.py`) and `vncd` (5900) from
`/etc/autostart`.

The user allowed (2026-10-03, overnight work on the WebKit browser): publish the app as a package,
install it on the Pi from telnet (`pkg` over the internet), and test it there myself.

**How to apply:** use it to run tests on the real Pi (`kmsg`, the browser, `wk2test`), not only the
posixsim bench. The address may change (DHCP): if it does not answer, ask the user. Do not change
the Pi's settings beyond what the test needs. See [[webkit-browser-ui-decisions]].

**How I deployed and tested from this PC (2026-10-05, the shared libraries):**
- Files: start `run SD:/bin/ftpd SD:/` on the Pi over telnet (detached, it stays up; since
  2026-10-05 a program in a session's foreground ends when the session drops), upload with Python `ftplib`
  (account `onyx` / `onyx`: ftpd's default; binary; big files reset the connection sometimes: retry).
  `wget ... > file` on the Pi is NOT binary-safe.
- `cmd` runs with its output only the programs of `SD:/bin` (a path like `SD:/tmp/x` is "command not
  found"; `run <path>` starts it detached, without output).
- `kmsg` over telnet is not stopped by Ctrl+C since the shell's rework (2026-10-04): keep a second
  telnet session in `kmsg`, kill its pid at the end (`tools/tests/shlib/pi_apps.py` does).
- If telnetd dies (it did twice): VNC still works — `python -m vncdotool.command -s <ip> move 1157 1033
  click 1 pause 3 type telnetd key enter` (the Terminal's dock icon on a 1920x1080 screen).
  A full-screen app makes a VNC capture hang: give it a timeout.
- A connect that succeeds but gets no greeting is one of the kernel's "deaf" connections
  (docs/HANDOFF.md, *Testing on the Pi*): close and connect again; wait for the `$ ` prompt before
  sending. `ps` state R with 0 calls/s is just a task waiting on a pipe, not a spin (`sysstat <pid>`
  shows the call it sits in). Other sessions use the Pi too (it restarted twice under me that day).
- Scripts kept in the repo: `tools/tests/shlib/pi_apps.py`, `compat_pi.py`.
- `rdpd` (port 3390) serves one client, and the user's Onyx Remote reconnects by itself: a test client
  of rdpd (`tools/tests/rdpd/flicker_bench.py`) is cut off within seconds while Onyx Remote runs, and
  it disconnects the user (seen 2026-10-06). Ask the user to close Onyx Remote before such a test, and
  before a reboot when they are clearly working on the Pi.
