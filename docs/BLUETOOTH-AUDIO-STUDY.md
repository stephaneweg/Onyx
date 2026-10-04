# Onyx: Bluetooth audio output — a feasibility study

*Status (2026-10-05): **a study, nothing built.** Asked by the user: can the Pi's Bluetooth be
used, the kernel carrying the stack, at least for an audio output device that plugs into
`COnyxSoundDevice` (docs/02 §13, kapi v84) beside the jack, USB and HDMI? Facts marked **[repo]**
were read in this repository, **[web]** were checked against the source given, **[memory]** are from
knowledge and were not re-checked — verify them before building on them. Answer the user in French;
this page stays in English.*

## 1. Short answer

**Feasible, and the audio side is the easy part.** The hard part is the Bluetooth host stack, and
there the choice is forced by licences:

- The only ready-made embedded stack that would be a few days of glue, **BTstack, cannot be used**:
  its licence forbids commercial use, which is incompatible both with the GPL-3.0 kernel image and
  with Onyx's "our code under MIT" rule.
- The realistic route is **a minimal stack of our own** (MIT), limited to what an audio output
  needs — HCI, L2CAP, a small SDP, Secure Simple Pairing "Just Works", AVDTP + A2DP source, AVRCP
  volume — with an **imported SBC encoder under Apache-2.0** (integer only: fits a kernel without
  floating point). Estimated **6 000 to 10 000 lines, one to two months** including the tests with
  real headsets (an estimate, not a measurement).
- **No cryptography is needed in the kernel**: for classic Bluetooth the controller (the chip) does
  the pairing's elliptic-curve work; the host only answers a few events and stores a 16-byte key.
- The hardware is there (the Pi 4 / 400's CYW43455 combo chip), the recipe to bring it up is public
  (the Linux driver), the chip's firmware patch may be redistributed as a separate file.

**One thing to decide before anything else (section 7):** Circle's author **removed** Circle's own
Bluetooth code in 2020 "for legal reasons", without saying which. Our stack would be our own code,
but the reason should be understood — or the risk accepted knowingly — before shipping one.

## 2. What exists today

| Piece | State | Source |
|---|---|---|
| The chip | CYW43455 (BCM4345C0): Wi-Fi over SDIO — used by Onyx —, Bluetooth 5.0 (classic + LE) over a UART, HCI "H4" | [web] the Pi 4's device tree |
| Circle (our fork, Step51) | **No Bluetooth stack.** Only `CUSBBluetoothDevice` (`include/circle/usb/usbbluetooth.h`): the transport for a USB dongle, and only HCI commands and events — **no ACL data**, so no audio through it as it is | [repo] |
| Circle's history | A rudimentary library (`lib/bt`: inquiry only, a UART transport for the Pi 3) existed from 2015; the author called it "rudimentary and cannot be extended because of legal reasons" (2018) and **removed it in February 2020 (Step 41)** "for legal reasons" | [web] commits `be4d98a`, `9222f43` of rsta2/circle |
| The UART | The chip is on the PL011 **UART0**, pins GPIO 30–33 (CTS, RTS, TX, RX, function ALT3). **The kernel's serial console is on that same UART0** (`m_Serial`, 115200 baud, GPIO 14/15) | [repo] `kernel/kernel.h`, `kernel.cpp:1969`; [web] |
| Circle's serial driver | Can route UART0 to GPIO 32/33 (`SERIAL_GPIO_SELECT 32`, meant for Compute Modules) but **"Handshake lines CTS and RTS are not supported"** — and the chip stays silent until RTS is driven | [repo] `circle/include/circle/serial.h` |
| The firmware patch | Not on the card (`sdcard/firmware` holds the Wi-Fi files only). Needed at each power-up: `BCM4345C0.hcd` (64 KB) | [repo]; [web] RPi-Distro/bluez-firmware |
| Wi-Fi / Bluetooth coexistence | Done inside the chip (one antenna, time shared on 2.4 GHz); tuned by `btc_mode` / `btc_params*` in the **Wi-Fi** NVRAM file — and **Onyx's `sdcard/firmware/brcmfmac43455-sdio.txt` already has the four lines** recommended for A2DP | [repo]; [web] |
| The sound side | `COnyxSoundDevice`: one producer (44.1 kHz stereo), outputs that pull its frames; a fourth output is a class and a line in `OutputStart` | [repo] `kernel/sys/sound.cpp` |

## 3. The standard, and the minimum for an audio output

Bluetooth audio output is standard: the **A2DP** profile (a "source" plays to a "sink": headset,
speaker), over **AVDTP**, over **L2CAP**, over **HCI**. Every A2DP sink must decode the **SBC**
codec at 44.1 and 48 kHz [web]; the other codecs (AAC, aptX, LDAC) are optional and not needed.
Volume and the headset's buttons are **AVRCP**. **SDP** tells that a device is an audio sink. A
headset with a microphone does not need the hands-free profile (HFP) to play music [web].

What the kernel must do to play:

1. **Bring the chip up**: its power line (through the firmware's GPIO expander), UART0 on GPIO 30–33
   with RTS/CTS, `HCI_Reset`, the patch download (`.hcd`: a list of vendor HCI commands), the baud
   rate raised (vendor command `0xFC18`; Linux uses 3 Mbaud), a device address.
2. **Find and pair**: inquiry (the list of devices around), connection, Secure Simple Pairing with
   "no input, no output" — the *Just Works* association, enough for headsets and speakers. The
   controller computes the keys; the host answers `IO_Capability_Request` and
   `User_Confirmation_Request`, **stores the link key** it is given (a file on the card) and gives
   it back at the next connection [memory for the split controller / host; BTstack's notes agree].
3. **Open the stream**: SDP query (is it an audio sink?), an L2CAP channel for AVDTP signalling
   (discover, get capabilities, set configuration, open), a second one for the media, `START`.
4. **Send the sound**: the producer's 44.1 kHz frames — **no rate conversion**: SBC takes 44.1 kHz —
   encoded in SBC (joint stereo, 16 blocks, 8 subbands, bitpool up to 53: ~328 kbit/s), packed with
   a 13-byte header, sent at the pace of the audio clock.
5. **Volume**: AVRCP "absolute volume" when the device has it, else the software gain — the same
   rule as the USB output's.

Latency: typically 100–250 ms, most of it the headset's own buffer [web]. Fine for music and
video (Jet and the Media Player would have to delay the picture), poor for games and for Koton.

## 4. The stack: the options

| Option | Licence | Verdict |
|---|---|---|
| **BTstack** (BlueKitchen) | BSD-like **plus "not for any commercial purpose"** | The best technical fit (single run loop, no RTOS, Broadcom patch download and A2DP source included, ~40 000 lines for this subset) — **ruled out by its licence**: a further restriction the GPL-3.0 does not allow in the kernel image, and against Onyx's MIT rule even outside the kernel |
| **Zephyr's host** | Apache-2.0 | Classic Bluetooth, A2DP and AVRCP are all marked *experimental*; tied to Zephyr's kernel API (work queues, net_buf, Kconfig): a shim layer of weeks, on unproven code |
| **Bluedroid** (ESP-IDF's fork of Android's) | Apache-2.0 | Works on ESP32 only; multi-threaded (FreeRTOS), large; a port is months |
| **BlueZ** | GPL / LGPL | Needs Linux's kernel Bluetooth subsystem: not portable here |
| **NimBLE**, **lwBT** | Apache / BSD | Low Energy only / no AVDTP: no audio |
| **A stack of our own, minimal** | **MIT** (ours) | **Recommended** — see the estimate below |

A compact existence proof: FreeBSD's `virtual_oss` plays to A2DP sinks with **~2 500 lines** (AVDTP
+ SBC + glue) on top of the system's L2CAP sockets [web]. And the Linux driver (`btbcm.c`,
`hci_bcm.c`) is a complete recipe for the chip's bring-up.

**Our own stack, layer by layer (estimates):**

| Layer | Lines |
|---|---|
| UART "H4" transport, HCI commands / events, the Broadcom patch download, baud rate, address | 1 200 – 1 800 |
| ACL data, L2CAP basic mode (connect, configure, fragments) | 1 200 – 2 000 |
| SDP: our "audio source" record, a minimal client | 600 – 1 000 |
| Inquiry, connection, pairing (Just Works), the link keys' file | 500 – 800 |
| AVDTP for one source endpoint | 800 – 1 200 |
| A2DP: packets, pacing | ~400 |
| SBC encoder — imported, integer only | 0 (imported) |
| AVRCP: absolute volume, play / pause from the headset | 600 – 1 000 |

**The SBC encoder.** Two permissive integer implementations: the Broadcom encoder of Android
(Apache-2.0, 8 C files; the one BTstack and ESP-IDF embed) and `google/libsbc` (Apache-2.0,
archived). Apache-2.0 is compatible with the GPL-3.0 kernel; it would be a third-party notice in
`docs/LICENSING.md`. BlueZ's is LGPL; FreeBSD's uses floating point. No MIT one was found.

## 5. What changes in Onyx

1. **The serial console leaves UART0.** The Pi 4 has four more PL011s and the mini-UART; the log
   can move to the mini-UART on GPIO 14/15 (what Raspberry Pi OS does) or be dropped (the screen and
   `kmsg` remain). To check: what Circle's `CSerialDevice` offers for the other UARTs on the Pi 4.
2. **A patch to our Circle fork** (docs/05): RTS/CTS and GPIO 30–33 in the serial driver, receive by
   interrupt at 3 Mbaud (or a small driver of our own for this one UART: simpler to own).
3. **The firmware file** `SD:/firmware/BCM4345C0.hcd` in the `pi-firmware` package, under Cypress's
   licence (object code, for Cypress chips: redistributable as a separate file) [web].
4. **The stack** in the kernel (`kernel/bt/`), on the **network core** rather than core 0 — it is
   I/O and timers, like the Wi-Fi —, behind a switch that is **off by default** and tried through
   the one-boot trial file (as every change near the Wi-Fi chip: the Pi's only remote access).
5. **A fourth output**: `COutBT` in `sys/sound.cpp` — `GetChunk`'s role is played by the stack's
   media timer, which takes the producer's chunks (`SourceTake`), encodes and sends them.
   `KAPI_SND_OUT_BT`, `output = bt` in `sound.ini`, **Play on → the paired device's name** in the
   Sound applet. Disconnected: silence and the drain; back in range: the output made again — the
   USB output's rule.
6. **A kapi for the devices** (append-only, a new version): scan (the devices around: name, class),
   pair, forget, the list of known devices, connect / disconnect, the state. And a **Bluetooth
   applet** in the Control Panel (add a device, the known ones, connect at boot).
7. **A USB dongle instead of the on-board chip** is possible later with the same stack:
   `CUSBBluetoothDevice` needs its ACL endpoints exposed (a patch too).

## 6. A plan, if it is decided

| Step | What | Result to see |
|---|---|---|
| B0 | The console off UART0; the UART with RTS/CTS; the chip powered, `HCI_Reset` answered | the chip's version in `kmsg` |
| B1 | The patch download, 3 Mbaud, an address; inquiry | a `/bin/bttest scan` listing the headset's name |
| B2 | Connection, pairing (Just Works), the link key kept | paired once, reconnected after a restart |
| B3 | L2CAP, SDP | the headset's services listed |
| B4 | AVDTP, the SBC encoder, the media channel | a tone heard in the headset |
| B5 | `COutBT` in `COnyxSoundDevice`, the kapi, the Sound applet | the system's sound on the headset, switched from the applet |
| B6 | AVRCP volume, reconnection, the Bluetooth applet, the docs, the package | usable every day |

Each step is testable on the Pi alone (`kmsg`), B4 onward with the user's ears. Host tests are
possible for the packet codecs (HCI, L2CAP, AVDTP, SBC against a reference decoder).

## 7. Risks and open questions

- **Circle's removal "for legal reasons"** [web]: the reason is not published. It may be about
  Bluetooth's qualification and trademark rules (a product that uses the name and the logo must be
  qualified with the Bluetooth SIG, which costs money) rather than about copyright. A hobby system
  that implements the protocols is in the same position as the other unqualified open-source stacks;
  a *sold* product would have to look at qualification. **To decide by the user.**
- **Interoperability**: headsets differ (capabilities, bitpool limits, reconnection habits); the
  time goes there. The user's own devices first.
- **The Wi-Fi shares the antenna**: on a 2.4 GHz network the audio may stutter under traffic; on
  5 GHz it should not. The Pi prefers 5 GHz since 2026-10-04.
- **3 Mbaud on a cooperative kernel**: the UART must be served by interrupt with a ring; the stack
  on the network core keeps it away from core 0's long sections.
- **Latency** makes it a music / video output, not one for games or for Koton's playing.
- **Not verified** (from memory, to check first): the mailbox tag and line numbers of the chip's
  power line on the Pi 4, the address the chip reports after its patch, the exact mandatory SBC
  table for a source, that the pairing's elliptic-curve work is wholly the controller's.
- **The Pi 5** has another chip arrangement (docs/PI5-PORT.md): not looked at here.

## 8. Recommendation

Do it as **our own minimal A2DP-source stack in the kernel (MIT) with an imported Apache-2.0 SBC
encoder**, in the order of section 6, after the user has decided on the two questions that are his:
the "legal reasons" risk (section 7), and whether losing the serial console on UART0 (moved to the
mini-UART) is acceptable. Steps B0–B1 alone (a few days) already prove the hardware path and cost
little if the rest is postponed.
