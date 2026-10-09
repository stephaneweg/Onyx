# rdpd_pocket_test.py -- rdpd under PocketUI (the host build with mock_rdpd.h, MOCK_POCKET=1 or 2): the flags it tells
# Onyx Remote -- an app's frameless main window and the shell's home (while no app shows) as plain windows (the PC's
# keys come only from those), the popups, the menu bar and the home behind an app as they are, PocketUI's matte
# hidden (or a picture on the desktop when the desktop is asked) -- and that the matte is never raised from the PC.
#   python3 rdpd_pocket_test.py <port> <log> <1|2>
import socket, struct, sys, time
port, log, mode = int(sys.argv[1]), sys.argv[2], int(sys.argv[3])
fails = 0
def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok: fails += 1

s = socket.create_connection(("127.0.0.1", port)); s.settimeout(5)
def recv(n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError
        b += c
    return b
recv(14)
s.sendall(b"ONYXRDP1\x00" + b"\x01")
geo = {}
def round_():
    wins = {}
    while True:
        t, n = struct.unpack("<BI", recv(5)); p = recv(n) if n else b""
        if t == 1:
            id = struct.unpack("<I", p[:4])[0]
            wins[id] = struct.unpack("<I", p[20:24])[0]
            geo[id] = struct.unpack("<hhHHHHHH", p[4:20]) + (p[25],)
        if t == 5: return wins
f = round_()
B, K, T, SYS, A = 1, 2, 4, 16, 32
if mode in (3, 4):					# a program with the full screen (3: PocketUI, a BASIC game; 4: the desktop, an emulator)
    check(list(f.keys()) == [22], "the full-screen window told alone (%s)" % sorted(f.keys()))
    check(f.get(22) == 0, "as a plain window, without a frame: it takes the keys (%s)" % f.get(22))
    g = geo.get(22, ())
    check(g[:4] == (0, 0, 1024, 768) and g[4:8] == (0, 0, 0, 0), "at 0, 0, the screen's size, no frame (%s)" % (g,))
    check(len(g) == 9 and g[8] & 2 == 0 and g[8] & 1, "its state without FULLSCREEN (the client's native frame), the keys kept (%s)" % (g[8:],))
elif mode == 5:
    check(f.get(20) == SYS, "console, home: the home told as a plain window (%s)" % f.get(20))
    check(f.get(25) == B | SYS | A, "the home's menu at the top edge told without TOPMOST: not a menu bar (%s)" % f.get(25))
    check(26 not in f, "its parked tip not told (%s)" % sorted(f.keys()))
elif mode == 2:
    check(f.get(20) == SYS, "home, no app: the shell's home told as a plain window (%s)" % f.get(20))
    check(f.get(24) == B | T | SYS | A, "the menu bar as it is")
    check(25 not in f and 26 not in f, "the shell's parked overlays (see-through, off the screen) not told (%s)" % sorted(f.keys()))
else:
    check(f.get(22) == 0, "the app's frameless main window told as a plain window (%s)" % f.get(22))
    check(f.get(23) == B, "its popup stays borderless (an overlay: %s)" % f.get(23))
    check(f.get(20) == B | K | SYS, "the home behind an app stays backmost (%s)" % f.get(20))
    check(f.get(21) == B | K | SYS, "the matte hidden without the desktop (backmost: %s)" % f.get(21))
    check(f.get(24) == B | T | SYS | A, "the menu bar as it is")
    s.sendall(b"\x01" + b"\x07\x01")			# READY, the desktop on
    f2 = {}
    for k in range(3):
        f2.update(round_()); s.sendall(b"\x01")
        if 21 in f2: break
    check(f2.get(21) == B | SYS, "with the desktop: the matte told again as a picture on it (%s)" % f2.get(21))
    s.sendall(struct.pack("<BI", 4, 21) + struct.pack("<BIhhBb", 2, 21, 5, 5, 1, 0) + struct.pack("<BIhhBb", 2, 21, 5, 5, 0, 0)
              + struct.pack("<BI", 4, 22) + struct.pack("<BI", 4, 20) + struct.pack("<BBI", 3, 3, ord('a')) + struct.pack("<BI", 6, ord('a')))
    time.sleep(0.5)
    L = open(log).read().split("\n")
    check("RAISE 21" not in L, "the matte never raised (focus, click)")
    check("RAISE 22" in L, "the app's window raised when focused on the PC")
    check("RAISE 20" not in L, "the home not raised (backmost)")
    check("KEY 97" in L and "HELD 97 1" in L, "the keys typed injected")
s.close()
print("%d failed" % fails)
sys.exit(1 if fails else 0)
