---
name: wifi-ampdu-tx-off
description: "The Pi's Wi-Fi sends without A-MPDU aggregation on purpose (kernel default since onyx 2026.10.38) — re-enabling it brings back the remote-desktop freezes; the test that shows it"
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-04T07:02:05.986Z
---

Since 2026-10-04 the kernel sets the Wi-Fi firmware's `ampdu_tx` to 0 (`onyx_wl_ampdu_tx = 0` in `NetWlanOptions`, `kernel/sys/net.cpp`), with frame bursting on and TCP delayed acknowledgements (one for 8 segments). With A-MPDU on the frames sent, the Pi 4 (firmware 7.45.x, the user's Orange Livebox, 5 GHz) lost 20–75 % of what it *sent* while it was receiving a download — the firmware counted the frames as acknowledged — and RDP / telnet / an echo froze for 1 to 60 s. Details: `docs/05-CIRCLE-CHANGES.md` §26 item 5.

**Why:** the user's complaint (2026-10-04): "when WebKit downloads, RDP or VNC freeze". Raw speed is lower without it (the Pi sends ~3 MB/s, receives 4–6.5 MB/s; with aggregation 8–9 MB/s both ways) — the user was told and reliability was chosen.

**How to apply:** do not turn `ampdu_tx` back on to gain speed without running the test that exposed it: the PC pings the Pi (`ping -i 0.2`) while the Pi's `curl --limit-rate 1M` downloads from the internet (the loss is highest with a *limited* download); 0 % is the pass mark. Try any such change through the trial file ([[pi-net-trial]]: words `ampdutx=`, `frameburst=`, `ackn=`, `tcpws=`, `netstat=1`).
