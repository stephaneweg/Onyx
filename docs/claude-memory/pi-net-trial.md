---
name: pi-net-trial
description: "Trying a Wi-Fi driver / TCP change on the Pi without losing it — the kernel's one-boot trial file (a bad test kernel cut the network on 2026-10-04)"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-03T23:13:22.440Z
---

A kernel change that touches the Wi-Fi driver (Circle `addon/wlan`), the SDIO path or TCP must never be the default of a kernel sent to the Pi by FTP: on 2026-10-04 my SDIO "fast path" kernel booted without Wi-Fi, the Pi was unreachable, and the user had to take the card out to copy a kernel by hand.

**Why:** the Pi (192.168.0.7) is only reachable by its Wi-Fi; a kernel whose network does not come up cannot be replaced remotely.

**How to apply:** put the new behaviour behind a switch that is off by default, and try it with the kernel's trial file: `SD:/etc/net-trial.txt` holding `name=value` words (`wlfast=`, `p9noyield=`, `tcpws=`, `secs=`), read and deleted at boot by `NetTrialLoad` (`kernel/sys/net.cpp`); the Pi restarts by itself after `secs` (default 180) unless `SD:/etc/net-trial.keep` exists — so a failed trial costs one restart. Only flip a default once its trial passed. A safe kernel for a copy by hand goes to `C:\Projects\Onyx\out\card-test\` (see [[pi-remote-access]]).
