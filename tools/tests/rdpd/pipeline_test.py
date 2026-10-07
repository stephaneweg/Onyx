#!/usr/bin/env python3
# rdpdtest.py -- protocol checks of rdpd (user/BinUtils/rdpd.c) built for the PC (build_host.sh):
# lock-step for an older client, pipelined rounds (CAPS, the credit window), no empty rounds,
# the PINGs (probes while a round is in flight, liveness while idle), the PONG round trip, the
# held keys released when a session ends, and an OLDER rdpd with a new client's options.
#   python3 rdpdtest.py NEW_RDPD_HOST [OLD_RDPD_HOST]
import os, socket, struct, subprocess, sys, time

PORT = 33900
fails = 0

def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1

class Server:
    def __init__(self, exe, anim, port):
        env = dict(os.environ, RDPD_ANIM="1" if anim else "0")
        self.p = subprocess.Popen([exe, str(port)], env=env, stderr=subprocess.PIPE, stdout=subprocess.DEVNULL, text=True)
        self.port = port
        time.sleep(0.3)
    def stop(self):
        self.p.terminate()
        try:
            out = self.p.communicate(timeout=3)[1]
        except subprocess.TimeoutExpired:
            self.p.kill(); out = self.p.communicate()[1]
        return out

class Client:
    def __init__(self, port, options):
        self.s = socket.create_connection(("127.0.0.1", port))
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        hello = self.read(14)
        assert hello[:8] == b"ONYXRDP1", hello
        self.s.sendall(b"ONYXRDP1" + bytes([options]))
    def read(self, n, timeout=5.0):
        self.s.settimeout(timeout)
        while len(self.buf) < n:
            d = self.s.recv(65536)
            if not d:
                raise EOFError
            self.buf += d
        r, self.buf = self.buf[:n], self.buf[n:]
        return r
    def msg(self, timeout=5.0):
        """-> (type, payload) or None on a timeout"""
        try:
            h = self.read(5, timeout)
        except socket.timeout:
            return None
        t, n = struct.unpack("<BI", h)
        return t, self.read(n, 10.0) if n else b""
    def ready(self): self.s.sendall(b"\x01")
    def collect(self, secs):
        """the messages arriving within secs"""
        out, end = [], time.time() + secs
        while True:
            left = end - time.time()
            if left <= 0:
                return out
            m = self.msg(left)
            if m is None:
                return out
            out.append(m)
    def close(self): self.s.close()

def types(ms): return [t for t, _ in ms]

def test_new_server(exe, old_exe):
    # 1. an older client (options 0): lock-step, no CAPS, no empty rounds
    sv = Server(exe, False, PORT)
    c = Client(PORT, 0)
    c.ready()
    ms = c.collect(0.6)
    t = types(ms)
    check(9 not in t, "older client: no CAPS")
    check(t.count(5) == 1 and 1 in t and 4 in t and 3 in t, "older client: the first round (WIN, PIXELS, ZORDER, END)")
    c.ready()
    ms = c.collect(0.8)
    check(5 not in types(ms), "static screen: no empty round after READY")
    c.close()
    sv.stop()

    # 2. an older client, the screen changing: exactly one round per READY; PINGs (skipped) while it waits
    sv = Server(exe, True, PORT + 1)
    c = Client(PORT + 1, 0)
    c.ready()
    ms = c.collect(1.0)
    t = types(ms)
    check(t.count(5) == 1, "older client, animating: one round without a READY (got %d)" % t.count(5))
    check(set(t) <= {1, 3, 4, 5, 10}, "older client: only known types + PING (%s)" % sorted(set(t)))
    pings = t.count(10)
    check(1 <= pings <= 6, "a round in flight and the client quiet: probes (%d PINGs in 1 s)" % pings)
    n = 0
    for _ in range(10):
        c.ready()
        ms = c.collect(0.15)
        n += types(ms).count(5)
    check(8 <= n <= 10, "older client: one round per READY (%d for 10)" % n)
    c.close()
    sv.stop()

    # 3. a pipelined client (option 4): CAPS, 3 rounds in flight, PING / PONG
    sv = Server(exe, True, PORT + 2)
    c = Client(PORT + 2, 4)
    first = c.msg()
    check(first is not None and first[0] == 9 and first[1] == bytes([2, 3]), "pipelined: CAPS (protocol 2, 3 rounds) first")
    c.s.sendall(struct.pack("<BIhh", 9, 2, 120, -5))	# MOVE window 2 (dragged on the PC)
    c.ready()
    ms = c.collect(1.0)
    t = types(ms)
    check(t.count(5) == 3, "pipelined: 3 rounds in flight without an answer (got %d)" % t.count(5))
    pings = [p for ty, p in ms if ty == 10]
    check(len(pings) >= 2, "pipelined, no answer: probes (%d)" % len(pings))
    for p in pings:
        c.s.sendall(b"\x08" + p)		# PONG
    for _ in range(t.count(5)):
        c.ready()				# (the rounds handled, late)
    t0 = time.time(); got = 0
    while time.time() - t0 < 2.0:		# answer each END: the rounds flow
        m = c.msg(0.5)
        if m is None:
            break
        if m[0] == 5:
            got += 1; c.ready()
        elif m[0] == 10:
            c.s.sendall(b"\x08" + m[1])
    check(got >= 30, "pipelined, answering: %d rounds in 2 s (the animation: 25 / s)" % got)
    # input: a held arrow key + a button down, then the connection drops -> released
    c.s.sendall(struct.pack("<BBI", 3, 1 | 2, 0xFF51))			# KEY held Left down
    c.s.sendall(struct.pack("<BIhhBb", 2, 2, 10, 10, 1, 0))		# PTR button 1 down
    time.sleep(0.2)
    c.close()
    time.sleep(0.3)
    log = sv.stop()
    check("host: held 258 1" in log and "host: held 258 0" in log, "session end: a held key released")
    check(log.count("host: ptr 0 0 0 0") >= 1, "session end: the button released")
    check("host: move 2 120 -5" in log, "MOVE: the window put where the PC has it")
    check("pipelined: 3 rounds in flight" in log, "kmsg: the session's mode")
    check("rdpd   credit" in log, "kmsg: the credit line (at the end of a session, every 5 s)")

    # 4. pipelined, idle: PING every 2 s, answered; silent -> the session ends
    sv = Server(exe, False, PORT + 3)
    c = Client(PORT + 3, 4)
    c.msg()
    c.ready()
    ms = c.collect(0.5)
    check(types(ms).count(5) == 1, "pipelined, static: one round, then nothing")
    ms = c.collect(2.5)
    t = types(ms)
    check(5 not in t and t.count(10) == 1, "pipelined, idle: one liveness PING in 2.5 s (%s)" % t)
    c.s.sendall(b"\x08" + ms[-1][1] if ms else b"")
    t0 = time.time(); closed = False
    while time.time() - t0 < 16:
        try:
            if c.msg(1.0) is None:
                continue
        except (EOFError, ConnectionResetError):
            closed = True
            break
    took = time.time() - t0
    check(closed and 9 < took < 15, "pipelined, silent client: the session ends (after %.1f s)" % took)
    log = sv.stop()
    check("was silent" in log, "kmsg: the silent client logged")

    # 5. an OLDER rdpd and a new client's options (bit 2): it ignores the bit, lock-step
    if old_exe:
        sv = Server(old_exe, True, PORT + 4)
        c = Client(PORT + 4, 4)
        c.ready()
        ms = c.collect(0.8)
        t = types(ms)
        check(9 not in t and t.count(5) == 1, "older rdpd + option 4: no CAPS, lock-step (%d rounds)" % t.count(5))
        n = 0
        for _ in range(10):
            c.ready()
            n += types(c.collect(0.15)).count(5)
        check(n >= 8, "older rdpd: one round per READY (%d)" % n)
        c.close()
        sv.stop()

def test_takeover(exe):
    """a second client while a session runs (the PC reconnecting, the first connection dead
    without the Pi knowing): it is served at once, the first session ends"""
    sv = Server(exe, False, PORT + 7)
    a = Client(sv.port, 4)
    a.ready()
    check(any(t == 5 for t in types(a.collect(1.0))), "takeover: the first client served")
    t0 = time.time()
    b = Client(sv.port, 4)              # (raises if no hello comes within 5 s)
    took = time.time() - t0
    check(took < 2.0, "takeover: the second client greeted at once (%.2f s)" % took)
    b.ready()
    check(any(t == 5 for t in types(b.collect(1.5))), "takeover: the second client served")
    try:
        a.s.settimeout(3.0)
        dead = False
        while True:
            d = a.s.recv(65536)
            if not d:
                dead = True
                break
    except (socket.timeout, ConnectionResetError):
        dead = isinstance(sys.exc_info()[1], ConnectionResetError)
    check(dead, "takeover: the first session closed")
    a.close(); b.close()
    out = sv.stop()
    check("this session ends" in out, "takeover: kmsg says the session ended for the new client")

test_new_server(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
test_takeover(sys.argv[1])
print("FAILED: %d" % fails if fails else "all passed")
sys.exit(1 if fails else 0)
