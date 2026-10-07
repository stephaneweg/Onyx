#!/usr/bin/env python3
"""pi_deploy.py -- put a test build of the kernel, AppKit and Elegant on the Pi, without a package.

    python tools/tests/elegant/pi_deploy.py [--ip 192.168.0.7] [--no-reboot] [--restore] [--trial] [--apps a,b,c]

Run from the repository's root, after the builds (docs/HANDOFF.md, Elegant's section):
    kernel/kernel8-rpi4.img            -> SD:/kernel8-rpi4.img
    user/lib/appkit.so                 -> SD:/lib/appkit.so
    user/Servers/elegant/elegant.elf   -> SD:/bin/elegant
    user/BinUtils/wstest.elf           -> SD:/bin/wstest   (if it is built)

It starts ftpd on the Pi over telnet (ftpd's default account), saves the Pi's present kernel and
AppKit into tools/tests/elegant/backup/ (once: an existing save is kept), uploads, checks the sizes,
then restarts the Pi. --restore puts the saved kernel and AppKit back. --apps: those apps too
(user/<name>.elf -> SD:/apps/<name>.app/main). --trial: the next start (that one only) is on Elegant -- SD:/etc/elegant.trial, which the kernel removes as it reads it.

MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
granted, free of charge, to any person obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so, subject to the following
conditions: The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED.
"""
import ftplib
import os
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BACKUP = os.path.join(HERE, "backup")
FILES = [("kernel/kernel8-rpi4.img", "kernel8-rpi4.img"),
         ("user/lib/appkit.so", "lib/appkit.so"),
         ("user/Servers/elegant/elegant.elf", "bin/elegant")]
OPTIONAL = [("user/BinUtils/wstest.elf", "bin/wstest"), ("user/BinUtils/rdpd.elf", "bin/rdpd"),
            ("user/lib/uikit.so", "lib/uikit.so"),
            ("user/BinUtils/el0test.elf", "bin/el0test"), ("user/BinUtils/faulttest.elf", "bin/faulttest"),
            ("user/BinUtils/sysstat.elf", "bin/sysstat")]         # the tests, when they are built
SAVED = ["kernel8-rpi4.img", "lib/appkit.so"]


def telnet(ip, cmds, wait=6.0):
    """Send shell lines to telnetd (a deaf connection: tried again)."""
    for _ in range(4):
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
            for c in cmds:
                s.sendall(c.encode() + b"\r")
                t = time.time()
                while time.time() - t < wait:
                    try:
                        if not s.recv(65536):
                            break
                    except socket.timeout:
                        break
            s.close()
            return True
        except OSError:
            time.sleep(1)
    return False


def ftp(ip):
    try:
        f = ftplib.FTP()
        f.connect(ip, 21, timeout=5)
    except OSError:
        if not telnet(ip, ["run SD:/bin/ftpd SD:/"], wait=2):
            sys.exit("pi_deploy: no telnet session with " + ip)
        time.sleep(2)
        f = ftplib.FTP()
        f.connect(ip, 21, timeout=8)
    f.login("onyx", "onyx")
    return f


def put(f, local, remote):
    size = os.path.getsize(local)
    for attempt in range(3):
        try:
            with open(local, "rb") as h:
                f.storbinary("STOR " + remote, h)
            if f.size(remote) == size:
                print("  put  %-22s %8d bytes" % (remote, size))
                return
        except ftplib.all_errors as e:
            print("  (retry %s: %s)" % (remote, e))
            time.sleep(1)
    sys.exit("pi_deploy: %s was not uploaded whole -- do NOT restart the Pi" % remote)


def main():
    args = sys.argv[1:]
    ip = "192.168.0.7"
    if "--ip" in args:
        ip = args[args.index("--ip") + 1]
    restore = "--restore" in args
    f = ftp(ip)
    f.voidcmd("TYPE I")
    if restore:
        for remote in SAVED:
            local = os.path.join(BACKUP, os.path.basename(remote))
            if not os.path.exists(local):
                sys.exit("pi_deploy: no save of " + remote)
            put(f, local, remote)
    else:
        for local, _ in FILES:
            if not os.path.exists(local):
                sys.exit("pi_deploy: %s is not built (run from the repository's root)" % local)
        os.makedirs(BACKUP, exist_ok=True)
        for remote in SAVED:
            local = os.path.join(BACKUP, os.path.basename(remote))
            if os.path.exists(local):
                continue
            with open(local, "wb") as h:
                f.retrbinary("RETR " + remote, h.write)
            print("  save %-22s %8d bytes" % (remote, os.path.getsize(local)))
        apps = args[args.index("--apps") + 1].split(",") if "--apps" in args else []
        extra = [("user/%s.elf" % a, "apps/%s.app/main" % a) for a in apps]
        for local, _ in extra:
            if not os.path.exists(local):
                sys.exit("pi_deploy: %s is not built" % local)
        for local, remote in FILES + [o for o in OPTIONAL if os.path.exists(o[0])] + extra:
            put(f, local, remote)
    if "--trial" in args:                       # the next start (only) is Elegant's
        import io
        f.storbinary("STOR etc/elegant.trial", io.BytesIO(b"one start on Elegant, the graphics server\n"))
        print("  put  etc/elegant.trial")
    f.quit()
    if "--no-reboot" in args:
        print("pi_deploy: done; the Pi was not restarted")
        return
    telnet(ip, ["reboot"], wait=1)
    print("pi_deploy: done; the Pi restarts")


if __name__ == "__main__":
    main()
