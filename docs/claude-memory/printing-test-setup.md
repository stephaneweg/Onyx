---
name: printing-test-setup
description: "The user's printer (HP DeskJet 2700 at 192.168.0.14, IPP, PWG Raster only) and how the print system was tested on the Pi; a mass TCP scan restarts the Pi"
metadata:
  node_type: memory
  type: reference
  originSessionId: 4b2a739b-4509-4974-928d-c881a2494bc4
  modified: 2026-10-04T23:26:24.869Z
---

The user's printer: **HP DeskJet 2700 series ("HP1469F0") at 192.168.0.14**, Wi-Fi, IPP Everywhere /
AirPrint, no PDF: PWG Raster 300 dpi, colour, one-sided (asked 2026-10-05). `sh tools/tests/run_print_test.sh`
with `IPP_PRINTER=192.168.0.14` asks it and sends a Validate-Job (nothing printed).

- A real print uses the user's paper and ink: the user allowed one on 2026-10-05 and then printed himself;
  ask before printing again (my interpretation: the permission was for that test).
- Fast deployment to the Pi without publishing: start `ftpd` over telnet (user / password `onyx`), upload
  with Python's ftplib (STOR works; RETR answered 550 that day). See [[pi-remote-access]].
- **Never open many TCP connections at once from the Pi** (12 non-blocking connects a batch over the /24
  restarted it on 2026-10-05). Printer discovery is a one-shot mDNS query instead. See [[pi-net-trial]].
- Driving the Pi's screen over VNC (vncdotool) clicks wherever the pointer lands: capture and check before
  each click, and stop when the user is using the Pi.
