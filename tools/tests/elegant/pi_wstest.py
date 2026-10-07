#!/usr/bin/env python3
"""pi_wstest.py -- on the Pi: a program's window served by Elegant (kapi v89, stage 2b).

    python tools/tests/elegant/pi_wstest.py [--ip 192.168.0.7] [--keep]

Needs the test build on the Pi (pi_deploy.py: the kernel, AppKit, bin/elegant, bin/wstest) and
`pip install vncdotool`. It starts `elegant --serve` and `wstest` over telnet, then through VNC
(vncd injects the pointer; it is only used to SEND: a capture waits for the screen to change, and
hangs on a still one): two clicks in the window, the window dragged by its title, its close button.
What it checks, from the Pi itself (ps, the kernel's log):
  - wstest got its window (it runs, Elegant answered 3 requests);
  - the clicks reached its handler (its last line: "closed after 2 clicks");
  - the drag moved the window (the close button is where the drag put it: the click there closes);
  - closing the window ends the program.
Elegant is stopped at the end (--keep: left running).

MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
granted, free of charge, to any person obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so, subject to the following
conditions: The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED.
"""
import socket
import subprocess
import sys
import time


def clean(data):
    out, i = bytearray(), 0
    while i < len(data):
        if data[i] == 255:
            i += 3 if i + 1 < len(data) and data[i + 1] in (251, 252, 253, 254) else 2
            continue
        out.append(data[i])
        i += 1
    return out.decode("latin-1")


def shell(ip, cmds, wait=8.0):
    """The output of each shell line (a deaf connection: tried again)."""
    for _ in range(5):
        try:
            s = socket.create_connection((ip, 23), timeout=5)
            s.settimeout(0.5)
            buf, t = b"", time.time()
            while time.time() - t < 5 and b"$ " not in buf:
                try:
                    buf += s.recv(4096)
                except socket.timeout:
                    pass
            if b"$ " not in buf:
                s.close()
                continue
            res = []
            for c in cmds:
                s.sendall(c.encode() + b"\r")
                buf, t = b"", time.time()
                while time.time() - t < wait:
                    try:
                        d = s.recv(65536)
                        if not d:
                            break
                        buf += d
                    except socket.timeout:
                        pass
                    if clean(buf).rstrip().endswith("$"):
                        break
                res.append(clean(buf))
            s.close()
            return res
        except OSError:
            time.sleep(1)
    sys.exit("pi_wstest: no telnet session with " + ip)


def pids(ip, name):
    out = shell(ip, ["ps"])[0]
    return [l.split()[0] for l in out.splitlines() if l.split()[-1:] in ([name], ["SD:/bin/" + name])]


def start(ip, name, args=""):
    for _ in range(3):
        shell(ip, ["run SD:/bin/%s %s" % (name, args)], wait=3)
        time.sleep(2)
        if pids(ip, name):
            return
    sys.exit("pi_wstest: %s does not start" % name)


def vnc(ip, *steps):
    subprocess.run([sys.executable, "-m", "vncdotool.command", "-s", ip, "--timeout", "40"] + list(steps),
                   timeout=60, check=False, capture_output=True)


def main():
    args = sys.argv[1:]
    ip = args[args.index("--ip") + 1] if "--ip" in args else "192.168.0.7"
    for name in ("wstest", "elegant"):
        for p in pids(ip, name):
            shell(ip, ["kill " + p])
    time.sleep(1)
    start(ip, "elegant", "--serve")
    start(ip, "wstest")
    fails = []

    # two clicks in its client area (the window opens at 140, 110: the first of the cascade)
    vnc(ip, "move", "300", "250", "pause", "0.5", "click", "1", "pause", "0.7",
        "move", "300", "260", "pause", "0.5", "click", "1", "pause", "0.7")
    # dragged by its title: 750 right, 500 down
    vnc(ip, "move", "250", "123", "pause", "0.5", "mousedown", "1", "pause", "0.5", "move", "600", "400",
        "pause", "0.5", "move", "1000", "623", "pause", "0.5", "mouseup", "1", "pause", "0.7")
    if not pids(ip, "wstest"):
        fails.append("wstest ended before its window was closed")
    # its close button, where the drag put it (frame 328 wide: 890 + 328 - 6 - 11, 610 + 5 + 9)
    vnc(ip, "move", "1201", "624", "pause", "0.5", "click", "1", "pause", "1.5")
    time.sleep(1)
    if pids(ip, "wstest"):
        fails.append("the close button did not end wstest (the drag did not move the window, or the exit request is lost)")
    log = shell(ip, ["kmsg"], wait=4)[0]
    for p in pids(ip, "kmsg"):                  # (kmsg follows the log for ever: its session would stay)
        shell(ip, ["kill " + p])
    closed = [l for l in log.splitlines() if "wstest: closed after" in l]
    if not closed:
        fails.append("wstest's last line is not in the kernel's log")
    elif "2 clicks" not in closed[-1]:
        fails.append("the clicks: " + closed[-1].strip())
    for l in [l for l in log.splitlines() if "elegant 5s" in l][-4:]:
        print("  " + l.strip())
    if "--keep" not in args:
        for p in pids(ip, "elegant"):
            shell(ip, ["kill " + p])
    if fails:
        for f in fails:
            print("pi_wstest: FAILED -- " + f)
        sys.exit(1)
    print("pi_wstest: all passed (" + closed[-1].strip().split("app: ")[-1] + ")")


if __name__ == "__main__":
    main()
