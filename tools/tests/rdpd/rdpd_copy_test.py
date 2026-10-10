# rdpd_copy_test.py -- COPY (option bit 3) against the host build of rdpd under MOCK_POCKET=6 (mock_rdpd.h: a band slid
# sideways, a window scrolled): a client that applies COPY and PIXELS to its copy of each window must have exactly the
# mock's pixels after every round, and get far fewer pixels than one without bit 3 (which must get no COPY).
import socket, struct, sys
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
    return bytes(out)

M = 0xFFFFFFFF
def h(u, v): return (((u * 2654435761) & M) ^ ((v * 40503 + 12345) & M)) & 0xFFFFFF
def px(id, x, y, k):
    if id == 30:
        if 100 <= y < 300: return h(x + 37 * k, y)
        if 90 <= y < 100 and 200 <= x < 260: return (k * 0x111111) & 0xFFFFFF
        return 0x101010
    return (k * 0x10101) & 0xFFFFFF if x >= 290 else h(x + 7, y + 23 * k)

def session(opts, rounds):
    s = socket.create_connection(("127.0.0.1", port)); s.settimeout(10)
    def recv(n):
        b = b""
        while len(b) < n:
            c = s.recv(n - len(b))
            if not c: raise EOFError
            b += c
        return b
    recv(14)
    s.sendall(b"ONYXRDP1" + bytes([opts]) + b"\x01")
    wins = {}; stats = []
    for r in range(rounds):
        copies = pix = 0; bad = 0
        while True:
            t, n = struct.unpack("<BI", recv(5)); p = recv(n) if n else b""
            if t == 1:
                id, x, y, w, hh = struct.unpack("<IhhHH", p[:12]); tn = p[26]
                m = wins.setdefault(id, {})
                if m.get("w") != w or m.get("h") != hh: m.update(w=w, h=hh, px=[0] * (w * hh))
                m["k"] = int(p[27:27 + tn].decode()[1:])
            elif t == 4:
                id, part, bpp, lz, x, y, w, hh = struct.unpack("<IBBBHHHH", p[:15])
                d = lz4(p[15:], w * hh * 4) if lz else p[15:]
                m = wins[id]; W = m["w"]
                for j in range(hh):
                    row = struct.unpack_from("<%dI" % w, d, j * w * 4)
                    m["px"][(y + j) * W + x:(y + j) * W + x + w] = [v & 0xFFFFFF for v in row]
                pix += w * hh
            elif t == 12:
                id, x, y, w, hh, dx, dy = struct.unpack("<IHHHHhh", p)
                m = wins[id]; W, H = m["w"], m["h"]
                ok = 0 <= x and x + w <= W and 0 <= y and y + hh <= H and 0 <= x + dx and x + dx + w <= W and 0 <= y + dy and y + dy + hh <= H
                if not ok: bad += 1; continue
                old = list(m["px"])
                for j in range(hh):
                    m["px"][(y + dy + j) * W + x + dx:(y + dy + j) * W + x + dx + w] = old[(y + j) * W + x:(y + j) * W + x + w]
                copies += 1
            elif t == 5: break
        for id, m in wins.items():
            W, H, k = m["w"], m["h"], m["k"]
            for y in range(H):
                for x in range(W):
                    if m["px"][y * W + x] != px(id, x, y, k): bad += 1
        stats.append((copies, pix, bad))
        s.sendall(b"\x01")
    s.close()
    return stats

a = session(8, 6)
check(all(st[2] == 0 for st in a), "with COPY: every round's pixels == the windows' %s" % (a,))
check(all(st[0] >= 2 for st in a[1:]), "with COPY: the band and the scrolled window moved by COPY each round")
b = session(0, 6)
check(all(st[2] == 0 for st in b) and all(st[0] == 0 for st in b), "without bit 3: no COPY, the pixels right %s" % (b,))
pa, pb = sum(st[1] for st in a[1:]), sum(st[1] for st in b[1:])
check(pa * 3 < pb, "COPY sends far fewer pixels after the first round (%d vs %d)" % (pa, pb))
print("log:", open(log).read().strip().splitlines()[:3])
sys.exit(1 if fails else 0)
