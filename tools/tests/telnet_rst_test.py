#!/usr/bin/env python3
"""telnet_rst_test -- a telnet client that goes away while the remote shell's output flows.

    python tools/tests/telnet_rst_test.py <pi-ip> [rounds] [port]     (default 30 rounds, port 23)

Reproduces the kernel panic "tcp: Unexpected state 0 at line 1922" (docs/05 patch 25): a peer's
RST closed a TCP connection whose retransmission timer was running, the connection was kept
until its socket was released, and the timer fired on it a second later.

Each round opens a session on /bin/telnetd, starts a command that writes (kmsg, then others
in turn), reads a little of its output and leaves in one of three ways:
  rst    SO_LINGER 0 then close: a RST at once, data in flight unacknowledged
  close  close with output unread: the system answers the next data segments with RST
  fin    shutdown (write side) then close a moment later: FIN, then RST for what still comes
then waits past the retransmission timeout (1 s) and checks that the Pi still answers (a new
session, `echo`). The Pi restarting shows as a failed check; SD:/etc/lastcrash.txt has the panic.
"""
import socket
import struct
import sys
import time

COMMANDS = ["kmsg", "ls /bin", "cat /etc/lastcrash.txt", "kmsg", "ps"]
WAYS = ["rst", "close", "fin"]


def session(host, port, timeout=5.0):
    s = socket.create_connection((host, port), timeout=timeout)
    s.settimeout(timeout)
    return s


def read_some(s, seconds, limit=1 << 20):
    """What arrives within `seconds` (at most `limit` bytes)."""
    end = time.time() + seconds
    got = b""
    while time.time() < end and len(got) < limit:
        s.settimeout(max(0.05, end - time.time()))
        try:
            d = s.recv(4096)
        except socket.timeout:
            break
        except OSError:
            break
        if not d:
            break
        got += d
    return got


def alive(host, port, tag):
    """A new session answers `echo <tag>`."""
    try:
        s = session(host, port)
        read_some(s, 0.5)                       # the banner, the prompt
        s.sendall(("echo %s\r" % tag).encode())
        got = read_some(s, 3.0)
        # the echo of the typed line has the tag once, the command's output a second time
        ok = got.count(tag.encode()) >= 2
        try:
            s.sendall(b"exit\r")
            read_some(s, 0.3)
        except OSError:
            pass
        s.close()
        return ok
    except OSError:
        return False


def round_(host, port, n):
    cmd = COMMANDS[n % len(COMMANDS)]
    way = WAYS[n % len(WAYS)]
    s = session(host, port)
    read_some(s, 0.4)
    s.sendall((cmd + "\r").encode())
    got = read_some(s, 0.15, limit=2048)        # the output has begun, more is on its way
    if way == "rst":
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("HH" if sys.platform == "win32" else "ii", 1, 0))
        s.close()
    elif way == "close":
        s.close()
    else:
        try:
            s.shutdown(socket.SHUT_WR)
        except OSError:
            pass
        time.sleep(0.1)
        s.close()
    return cmd, way, len(got)


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip().splitlines()[2].strip())
        sys.exit(2)
    host = sys.argv[1]
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 23

    if not alive(host, port, "onyx-before"):
        print("FAIL the Pi does not answer on %s:%d before the test" % (host, port))
        sys.exit(1)
    failed = 0
    for n in range(rounds):
        try:
            cmd, way, got = round_(host, port, n)
        except OSError as e:
            print("FAIL round %d: no session (%s)" % (n + 1, e))
            failed += 1
            break
        time.sleep(1.5)                         # past the retransmission timeout
        ok = alive(host, port, "onyx-alive-%d" % n)
        print("%s round %2d: %-22s left by %-5s after %4d bytes" % ("ok  " if ok else "FAIL", n + 1, cmd, way, got))
        if not ok:
            failed += 1
            break
    time.sleep(3.0)                             # (backed-off timers of the last rounds)
    if not failed and not alive(host, port, "onyx-after"):
        print("FAIL the Pi does not answer after the test")
        failed += 1
    print("FAIL" if failed else "PASS: %d rounds, the Pi still answers" % rounds)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
