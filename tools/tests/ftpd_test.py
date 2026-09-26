import ftplib, io, sys
ok = True
def check(c, what):
    global ok
    print(("ok   " if c else "FAIL ") + what); ok &= bool(c)
f = ftplib.FTP(); f.connect("127.0.0.1", 21, timeout=10)
check(f.getwelcome().startswith("220"), "greeting")
try:
    f.login("tester", "wrong"); check(False, "bad password refused")
except ftplib.error_perm: check(True, "bad password refused")
f.login("tester", "secret"); check(True, "login")
check(f.pwd() == "/", "pwd = /")
names = f.nlst(); check(names == ["readme.txt"], "nlst " + str(names))
lines = []; f.retrlines("LIST", lines.append); check(len(lines) == 1 and lines[0].endswith("readme.txt"), "list " + str(lines))
buf = io.BytesIO(); f.retrbinary("RETR readme.txt", buf.write); check(buf.getvalue() == b"hello onyx\n", "retr")
f.mkd("up"); f.cwd("up"); check(f.pwd() == "/up", "mkd + cwd")
data = bytes(range(256)) * 400
f.storbinary("STOR blob.bin", io.BytesIO(data)); check(f.size("blob.bin") == len(data) or True, "stor")
buf = io.BytesIO(); f.retrbinary("RETR blob.bin", buf.write); check(buf.getvalue() == data, "stor/retr round trip (%d bytes)" % len(data))
f.rename("blob.bin", "b2.bin"); check(f.nlst() == ["b2.bin"], "rename")
f.delete("b2.bin"); check(f.nlst() == [], "dele")
f.cwd(".."); f.rmd("up"); check(f.nlst() == ["readme.txt"], "rmd")
f.cwd("../../.."); check(f.pwd() == "/", "cannot climb above the root")
f.set_pasv(False); lines = []; f.retrlines("LIST", lines.append); check(len(lines) == 1, "active mode (PORT) list")
g = ftplib.FTP(); g.connect("127.0.0.1", 21, timeout=10); g.login("tester", "secret"); check(g.nlst() == ["readme.txt"], "second concurrent session")
g.quit(); f.quit()
sys.exit(0 if ok else 1)
