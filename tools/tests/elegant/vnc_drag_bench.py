#!/usr/bin/env python3
"""vnc_drag_bench.py -- how smooth a window dragged through VNC is: the updates a second vncd gives.

    python tools/tests/elegant/vnc_drag_bench.py X Y [--ip 192.168.0.7] [--seconds 8]

X Y: a point of a window's title bar on the Pi's screen (`rdpd list` over telnet gives the windows'
places: the title is ~14 pixels above the client area). The program is a small RFB client: it presses
there, moves the pointer on a circle (a move every 16 ms) for the time asked, and meanwhile asks for
incremental updates as fast as vncd answers (zlib rectangles, as a viewer; read, not decoded). It prints the updates and the megabytes
a second, and the longest gap between two updates -- run it under the kernel's window manager and
under Elegant to compare.

MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
granted, free of charge, to any person obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so, subject to the following
conditions: The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED.
"""
import math
import socket
import struct
import sys
import threading
import time


def rd(s, n):
    b = bytearray()
    while len(b) < n:
        d = s.recv(min(1 << 20, n - len(b)))
        if not d:
            raise SystemExit("vnc_drag_bench: the connection was closed")
        b += d
    return bytes(b)


def main():
    a = sys.argv[1:]
    ip = a[a.index("--ip") + 1] if "--ip" in a else "192.168.0.7"
    secs = float(a[a.index("--seconds") + 1]) if "--seconds" in a else 8.0
    x0, y0 = int(a[0]), int(a[1])
    s = socket.create_connection((ip, 5900), timeout=10)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    rd(s, 12)
    s.sendall(b"RFB 003.008\n")
    n = rd(s, 1)[0]
    types = rd(s, n)
    if 1 not in types:
        raise SystemExit("vnc_drag_bench: vncd asks for a password")
    s.sendall(b"\x01")
    if struct.unpack(">I", rd(s, 4))[0] != 0:
        raise SystemExit("vnc_drag_bench: refused")
    s.sendall(b"\x01")                                  # shared
    w, h = struct.unpack(">HH", rd(s, 4))
    rd(s, 16)
    rd(s, struct.unpack(">I", rd(s, 4))[0])
    s.sendall(struct.pack(">BBHii", 2, 0, 2, 6, 0))     # encodings: zlib (as a viewer asks), raw
    lock = threading.Lock()

    def ptr(buttons, x, y):
        with lock:
            s.sendall(struct.pack(">BBHH", 5, buttons, max(0, min(w - 1, x)), max(0, min(h - 1, y))))

    def ask(inc):
        with lock:
            s.sendall(struct.pack(">BBHHHH", 3, inc, 0, 0, w, h))

    stop = False

    def drag():
        ptr(0, x0, y0)
        time.sleep(0.4)
        ptr(1, x0, y0)
        time.sleep(0.2)
        i = 0
        while not stop:
            t = i * 2 * math.pi / 180
            ptr(1, int(x0 + 250 * math.cos(t) - 250), int(y0 + 150 * math.sin(t)))
            i += 1
            time.sleep(0.016)
        ptr(1, x0, y0)
        time.sleep(0.2)
        ptr(0, x0, y0)

    def rect():                                         # one rectangle read (not decoded) -> its bytes
        rx, ry, rw, rh, enc = struct.unpack(">HHHHi", rd(s, 12))
        n = struct.unpack(">I", rd(s, 4))[0] if enc == 6 else rw * rh * 4
        rd(s, n)
        return n

    ask(0)                                              # the whole screen once, not counted
    hdr = rd(s, 4)
    for _ in range(struct.unpack(">H", hdr[2:4])[0]):
        rect()
    th = threading.Thread(target=drag)
    th.start()
    time.sleep(1.0)
    t0 = last = time.time()
    updates, nbytes, gap = 0, 0, 0.0
    while time.time() - t0 < secs:
        ask(1)
        hdr = rd(s, 4)
        if hdr[0] != 0:
            raise SystemExit("vnc_drag_bench: an unexpected message %d" % hdr[0])
        for _ in range(struct.unpack(">H", hdr[2:4])[0]):
            nbytes += rect()
        now = time.time()
        gap = max(gap, now - last)
        last = now
        updates += 1
    dt = time.time() - t0
    stop = True
    th.join()
    time.sleep(0.3)
    s.close()
    print("vnc_drag_bench: %.1f updates a second, %.1f MB a second, the longest gap %.0f ms (%d x %d, %.0f s)"
          % (updates / dt, nbytes / dt / 1e6, gap * 1000, w, h, dt))


if __name__ == "__main__":
    main()
