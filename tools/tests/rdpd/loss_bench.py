#!/usr/bin/env python3
# loss_bench.py -- rdpd over a lossy link, on the PC: the rounds a second and the longest stall
# for a lock-step client (an older Onyx Remote) and a pipelined one (PINGs answered), against
# rdpd built for the PC with RDPD_ANIM=1 (build_host.sh). The loss: iptables dropping a share of
# the loopback's packets in a network namespace of its own (no root needed beyond unshare):
#   unshare -rn python3 tools/tests/rdpd/loss_bench.py RDPD_HOST [OLD_RDPD_HOST] [loss 0.2] [secs 10]
# Linux's TCP on both ends (200 ms minimum RTO, tail-loss probes) is kinder than Circle's (1 s)
# and Windows', so the stalls here are shorter than on the Pi; the comparison is what counts.
import fcntl, os, socket, struct, subprocess, sys, time

def lo_up():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ifr = struct.pack("16sH", b"lo", 0)
    flags = struct.unpack("16sH", fcntl.ioctl(s, 0x8913, ifr))[1]		# SIOCGIFFLAGS
    fcntl.ioctl(s, 0x8914, struct.pack("16sH", b"lo", flags | 1))		# SIOCSIFFLAGS: IFF_UP

def run(exe, port, options, secs):
    p = subprocess.Popen([exe, str(port)], env=dict(os.environ, RDPD_ANIM="1"), stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    time.sleep(0.5)
    s = socket.create_connection(("127.0.0.1", port), timeout=30)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    buf = b""
    while len(buf) < 14:
        buf += s.recv(64)
    buf = buf[14:]
    s.sendall(b"ONYXRDP1" + bytes([options]) + b"\x01")
    s.settimeout(0.05)
    rounds, last, worst, t0 = 0, time.time(), 0.0, time.time()
    while time.time() - t0 < secs:
        try:
            d = s.recv(65536)
            if not d:
                break
            buf += d
        except socket.timeout:
            pass
        while len(buf) >= 5:
            t, n = struct.unpack("<BI", buf[:5])
            if len(buf) < 5 + n:
                break
            pay, buf = buf[5:5 + n], buf[5 + n:]
            if t == 5:
                now = time.time()
                worst = max(worst, now - last); last = now; rounds += 1
                s.sendall(b"\x01")
            elif t == 10 and options & 4:
                s.sendall(b"\x08" + pay)
    s.close(); p.terminate(); p.wait()
    return rounds / secs, worst

exe = sys.argv[1]
old = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].replace(".", "").isdigit() else None
rest = [a for a in sys.argv[2:] if a.replace(".", "").isdigit()]
loss = float(rest[0]) if rest else 0.2
secs = float(rest[1]) if len(rest) > 1 else 10
lo_up()
subprocess.run(["iptables", "-A", "INPUT", "-i", "lo", "-m", "statistic", "--mode", "random", "--probability", str(loss), "-j", "DROP"], check=True)
print("loopback, %d %% of the packets dropped, %g s each, a window redrawn 25 times a second:" % (loss * 100, secs))
cases = [("new rdpd, pipelined client ", exe, 4), ("new rdpd, lock-step client ", exe, 0)]
if old:
    cases.append(("old rdpd, lock-step client ", old, 0))
for i, (name, e, opt) in enumerate(cases):
    r, w = run(e, 35700 + i, opt, secs)
    print("  %s %5.1f rounds / s, longest stall %.2f s" % (name, r, w))
