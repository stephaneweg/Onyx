# rdpd_test.py -- a client of rdpd (the host build with mock_rdpd.h): the hello, the windows,
# their pixels (LZ4 decoded, compared with the mock's), only the changed tile the next round,
# nothing sent while nothing changes,
# the pointer / keys / focus / close sent back and what rdpd injected on the "Pi".
import socket, struct, sys, time
port, log = int(sys.argv[1]), sys.argv[2]
fails = 0
def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok: fails += 1

def lz4(src, n):
    out = bytearray(); i = 0
    while i < len(src):
        t = src[i]; i += 1; l = t >> 4
        if l == 15:
            while True:
                b = src[i]; i += 1; l += b
                if b != 255: break
        out += src[i:i+l]; i += l
        if i >= len(src): break
        off = src[i] | src[i+1] << 8; i += 2; m = t & 15
        if m == 15:
            while True:
                b = src[i]; i += 1; m += b
                if b != 255: break
        m += 4; r = len(out) - off
        for k in range(m): out.append(out[r + k])
    assert len(out) == n, (len(out), n)
    return bytes(out)

def mock_px(id, part, x, y, changed):
    if changed and id == 7 and part == 0 and x == 70 and y == 10: return 0xFF00FF
    if part: return 0x202020 + part * 0x100000 + (x & 7)
    return ((x * 2) << 16 | (y * 3) << 8 | ((x ^ y) & 0xFF)) if id == 7 else 0x123456

s = socket.create_connection(("127.0.0.1", port)); s.settimeout(5)
def recv(n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError
        b += c
    return b
h = recv(14)
check(h[:8] == b"ONYXRDP1" and struct.unpack("<HHH", h[8:]) == (1024, 768, 56), "hello: ONYXRDP1, 1024 x 768, kapi 56")
s.sendall(b"ONYXRDP1\x00" + b"\x01")		# 32-bit pixels, READY

def round_():
    msgs = []
    while True:
        t, n = struct.unpack("<BI", recv(5)); p = recv(n) if n else b""
        msgs.append((t, p))
        if t == 5: return msgs

wins, pixels = {}, []
for t, p in round_():
    if t == 1:
        id, x, y, w, h, ow, oh, il, it, fl, al, st, tn = struct.unpack("<IhhHHHHHHIBBB", p[:27])
        wins[id] = dict(x=x, y=y, w=w, h=h, ow=ow, oh=oh, il=il, it=it, flags=fl, title=p[27:27+tn].decode())
    if t == 4:
        id, part, bpp, lz, x, y, w, h = struct.unpack("<IBBBHHHH", p[:15])
        d = lz4(p[15:], w * h * 4) if lz else p[15:]
        pixels.append((id, part, x, y, w, h, d, lz))
check(set(wins) == {7, 9} and wins[7]["title"] == "Test A" and wins[7]["ow"] == 114 and wins[9]["flags"] == 5, "the two windows (WIN: place, frame, flags, title)")
bad = 0; covered = {}
for id, part, x, y, w, h, d, lz in pixels:
    for j in range(h):
        for i in range(w):
            v = struct.unpack_from("<I", d, (j * w + i) * 4)[0] & 0xFFFFFF
            if v != mock_px(id, part, x + i, y + j, False): bad += 1
            covered[(id, part, x + i, y + j)] = 1
check(bad == 0, "the pixels decoded == the windows' (%d rectangles, %d LZ4)" % (len(pixels), sum(1 for q in pixels if q[7])))
check(sum(1 for k in covered if k[0] == 7 and k[1] == 0) == 100 * 70 and sum(1 for k in covered if k[0] == 7 and k[1] == 1) == 114 * 109
      and sum(1 for k in covered if k[0] == 9) == 50 * 20, "every pixel of the contents and the frames sent")
s.sendall(b"\x01")
r3 = round_()		# (the 2nd look changes nothing: no empty round is sent; the 3rd has the change)
px = [struct.unpack("<IBBBHHHH", p[:15]) for t, p in r3 if t == 4]
check(len(px) == 1 and px[0][0] == 7 and px[0][1] == 0 and px[0][4:6] == (64, 0) and px[0][6] == 36, "a pixel changed: only its tile row run is sent %s" % (px,))
s.sendall(b"\x01")
s.settimeout(0.6)
try:
    recv(5); quiet = False
except socket.timeout:
    quiet = True
s.settimeout(5)
check(quiet, "no change: no round sent (no empty rounds)")
# input back: the pointer in window coordinates, a key, a character, focus, close
s.sendall(struct.pack("<BIhhBb", 2, 7, 10, 20, 1, 0) + struct.pack("<BIhhBb", 2, 7, 11, 20, 0, 0)
          + struct.pack("<BBI", 3, 1, 0xFF51) + struct.pack("<BBI", 3, 3, ord('x')) + struct.pack("<BI", 6, ord('A'))
          + struct.pack("<BI", 4, 7) + struct.pack("<BI", 5, 7))
time.sleep(0.5)
L = open(log).read().split("\n")
check("PTR 117 152 1 0" in L and "PTR 118 152 0 0" in L, "the pointer put back on the screen (107 + 10, 132 + 20)")
check("KEY 27" in L and "HELD 258 1" in L, "Left: the key (ESC[D) + held")
check("HELD 120 1" in L and "KEY 65" in L, "a letter: held only; a character typed")
check("RAISE 7" in L and "CLOSE 7" in L, "focus -> raise, close box -> close")
s.close()
print("%d failed" % fails)
sys.exit(1 if fails else 0)
