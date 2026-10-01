#!/usr/bin/env python3
"""tools/tests/archiver/test.py -- the Archiver's engine on the PC (run by run_archiver_test.sh).
Archives made by Python's zipfile and the zip tool, read, extracted, changed by arctool (the engine
over the simulator's kapi: RAM: is the folder SIM_RAM); every result checked by zipfile and unzip -t."""
import os, sys, zipfile, subprocess, shutil, random, hashlib

TOOL, RAM = sys.argv[1], sys.argv[2]
fails = 0
def check (cond, what):
	global fails
	print (("  ok   " if cond else "  FAIL ") + what)
	if not cond: fails += 1
def run (*a):
	r = subprocess.run ([TOOL] + list (a), capture_output = True, text = True, env = dict (os.environ, SIM_RAM = RAM))
	return r.returncode, r.stdout.strip () + (("\n" + r.stderr.strip ()) if r.returncode and r.stderr.strip () else "")
def R (*p): return os.path.join (RAM, *p)
def sha (p): return hashlib.sha1 (open (p, "rb").read ()).hexdigest ()
def tree (root):
	out = {}
	for d, dirs, files in os.walk (root):
		for f in files:
			p = os.path.join (d, f); out[os.path.relpath (p, root)] = sha (p)
		for x in dirs:
			p = os.path.join (d, x)
			if not os.listdir (p): out[os.path.relpath (p, root) + "/"] = "dir"
	return out
def ztree (z):
	out = {}
	with zipfile.ZipFile (z) as f:
		for i in f.infolist ():
			if i.is_dir (): out[i.filename] = "dir"
			else: out[i.filename] = hashlib.sha1 (f.read (i)).hexdigest ()
	return out

shutil.rmtree (RAM, ignore_errors = True); os.makedirs (RAM)
# ---- the source tree ----
src = R ("src")
random.seed (7)
files = {
	"README.md": b"# Onyx\n" * 300,
	"kernel/sys/kapi.cpp": ("int kapi_%d (void) { return %d; }\n" * 2000 % tuple (x for i in range (2000) for x in (i, i))).encode (),
	"kernel/sys/sched.cpp": b"// scheduler\n" * 5000,
	"kernel/include/kapi_abi.h": b"#define X 1\n" * 100,
	"docs/images/noise.bin": bytes (random.getrandbits (8) for _ in range (300000)),
	"docs/café crème.txt": "café été\n".encode () * 50,
	"empty.txt": b"",
}
for n, b in files.items ():
	os.makedirs (os.path.dirname (os.path.join (src, n)) or src, exist_ok = True)
	open (os.path.join (src, n), "wb").write (b)
os.makedirs (os.path.join (src, "emptydir"))

def mkzip (name, method, prefix = b"", comment = b""):
	p = R (name)
	with open (p, "wb") as fo:
		fo.write (prefix)
		with zipfile.ZipFile (fo, "w", method) as z:
			for d, dirs, fs in sorted (os.walk (src)):
				for x in sorted (dirs):
					q = os.path.join (d, x)
					if not os.listdir (q): z.write (q, os.path.relpath (q, src) + "/")
				for f in sorted (fs):
					q = os.path.join (d, f); z.write (q, os.path.relpath (q, src))
			z.comment = comment
	return p
mkzip ("deflate.zip", zipfile.ZIP_DEFLATED, comment = b"made by test.py")
mkzip ("stored.zip", zipfile.ZIP_STORED)
mkzip ("sfx.zip", zipfile.ZIP_DEFLATED, prefix = b"MZ" + b"\0" * 5000)
srcTree = tree (src)

print ("reading and extracting")
for name in ("deflate.zip", "stored.zip", "sfx.zip"):
	rc, out = run ("list", "RAM:/" + name)
	check (rc == 0 and out.startswith ("FORMAT ZIP"), name + ": listed (" + out.splitlines ()[0] + ")")
	check (any (l.startswith ("F 0 ") and l.endswith (" empty.txt") for l in out.splitlines ()) and "D 0 0 00000000 emptydir" in out, name + ": empty file and folder")
	check ("docs/café crème.txt" in out, name + ": a UTF-8 name")
	rc, out = run ("extract", "RAM:/" + name, "RAM:/out_" + name, "0", "-", "1")
	check (rc == 0, name + ": extracted: " + out)
	check (tree (R ("out_" + name)) == srcTree, name + ": the tree extracted is the source's")
rc, out = run ("list", "RAM:/deflate.zip")
check ("COMMENT made by test.py" in out, "the archive's comment read")

print ("extracting a selection")
rc, out = run ("extract", "RAM:/deflate.zip", "RAM:/sel_full", "0", "kernel", "1", "kernel/sys/kapi.cpp", "docs/images")
check (rc == 0 and sorted (tree (R ("sel_full"))) == ["docs/images/noise.bin", "kernel/sys/kapi.cpp"], "keep the folders: " + str (sorted (tree (R ("sel_full")))))
rc, out = run ("extract", "RAM:/deflate.zip", "RAM:/sel_cur", "1", "kernel", "1", "kernel")
check (sorted (tree (R ("sel_cur"))) == ["include/kapi_abi.h", "sys/kapi.cpp", "sys/sched.cpp"], "from the current folder down: " + str (sorted (tree (R ("sel_cur")))))
rc, out = run ("extract", "RAM:/deflate.zip", "RAM:/sel_flat", "2", "-", "1", "kernel")
check (sorted (tree (R ("sel_flat"))) == ["kapi.cpp", "kapi_abi.h", "sched.cpp"], "flat: " + str (sorted (tree (R ("sel_flat")))))
rc, out = run ("extract", "RAM:/deflate.zip", "RAM:/sel_flat", "2", "-", "2", "kernel")
check ("files=0 skipped=3" in out, "existing files skipped: " + out)
rc, out = run ("extract", "RAM:/deflate.zip", "RAM:/sel_flat", "2", "-", "3", "kernel")
check (sorted (tree (R ("sel_flat"))) == ["kapi (2).cpp", "kapi.cpp", "kapi_abi (2).h", "kapi_abi.h", "sched (2).cpp", "sched.cpp"], "keep both: " + str (sorted (tree (R ("sel_flat")))))

print ("a damaged archive")
b = bytearray (open (R ("deflate.zip"), "rb").read ())
with zipfile.ZipFile (R ("deflate.zip")) as z: off = z.getinfo ("kernel/sys/sched.cpp").header_offset
b[off + 30 + len ("kernel/sys/sched.cpp") + 40] ^= 0x55
open (R ("bad.zip"), "wb").write (b)
rc, out = run ("extract", "RAM:/bad.zip", "RAM:/out_bad", "0", "-", "1", "kernel/sys/sched.cpp")
check (rc != 0 and "sched.cpp" in out, "a damaged entry refused: " + out)
check (not os.path.exists (R ("out_bad", "kernel/sys/sched.cpp")), "no half file left")
open (R ("notzip.zip"), "wb").write (b"hello")
rc, out = run ("list", "RAM:/notzip.zip")
check (rc != 0 and "ERROR" in out, "not an archive: " + out)

print ("ZipCrypto")
subprocess.run (["zip", "-q", "-r", "-P", "secret", R ("crypt.zip"), "."], cwd = src, check = True)
rc, out = run ("list", "RAM:/crypt.zip")
check (" *" in out, "encrypted entries flagged")
rc, out = run ("pw", "RAM:/crypt.zip", "secret", "RAM:/out_crypt")
check (rc == 0 and tree (R ("out_crypt")) == srcTree, "decrypted with the password: " + out)
rc, out = run ("pw", "RAM:/crypt.zip", "wrong", "RAM:/out_crypt2")
check (rc != 0 and "password" in out.lower (), "a wrong password refused: " + out)

print ("changing an archive")
def zcheck (name, what):
	with zipfile.ZipFile (R (name)) as z: bad = z.testzip ()
	t = subprocess.run (["unzip", "-tq", R (name)], capture_output = True, text = True)
	check (bad is None and t.returncode == 0, what + ": zipfile and unzip -t agree (" + t.stdout.strip () + ")")
shutil.copy (R ("deflate.zip"), R ("work.zip"))
before = ztree (R ("work.zip"))
os.makedirs (R ("add", "photos", "2026"), exist_ok = True)
open (R ("add", "photos", "2026", "a.txt"), "wb").write (b"hello " * 1000)
open (R ("add", "photos", "b.png"), "wb").write (bytes (range (256)) * 40)
open (R ("add", "notes.txt"), "wb").write (b"notes\n" * 777)
open (R ("add", "photos", "big.log"), "wb").write (b"".join (b"line %d of a big log\n" % i for i in range (300000)))	# (> 4 MB: streamed)
rc, out = run ("add", "RAM:/work.zip", "docs", "1", "6", "1", "RAM:/add/photos", "RAM:/add/notes.txt")
check (rc == 0, "added a folder and a file into docs/: " + out)
zcheck ("work.zip", "after the add")
t = ztree (R ("work.zip"))
check (all (t.get (k) == v for k, v in before.items ()), "the old entries kept, unchanged")
check (t.get ("docs/photos/2026/a.txt") == sha (R ("add", "photos", "2026", "a.txt")) and t.get ("docs/notes.txt") == sha (R ("add", "notes.txt"))
       and t.get ("docs/photos/big.log") == sha (R ("add", "photos", "big.log"))
       and "docs/photos/" in t and "docs/photos/2026/" in t, "the new ones where they belong")
with zipfile.ZipFile (R ("work.zip")) as z:
	check (z.getinfo ("docs/photos/big.log").flag_bits & 8 != 0, "a big file streamed, with a data descriptor")
	check (z.getinfo ("docs/photos/b.png").compress_type == 0 and z.getinfo ("docs/notes.txt").compress_type == 8, "a .png stored, a text deflated")
	check (z.comment == b"made by test.py", "the archive's comment kept")
open (R ("add", "notes.txt"), "wb").write (b"new notes\n")
rc, out = run ("add", "RAM:/work.zip", "docs", "1", "9", "0", "RAM:/add/notes.txt")
check (rc != 0 and ztree (R ("work.zip"))["docs/notes.txt"] != sha (R ("add", "notes.txt")), "an existing name not replaced when told so: " + out)
rc, out = run ("add", "RAM:/work.zip", "docs", "1", "9", "1", "RAM:/add/notes.txt")
check (rc == 0 and ztree (R ("work.zip"))["docs/notes.txt"] == sha (R ("add", "notes.txt")), "... replaced when told so")
zcheck ("work.zip", "after the replace")
rc, out = run ("add", "RAM:/work.zip", "-", "0", "1", "1", "RAM:/add/photos")
t = ztree (R ("work.zip"))
check (rc == 0 and "a.txt" in t and "b.png" in t and "photos/" not in t, "only the files (flat) at the top")
with zipfile.ZipFile (R ("work.zip")) as z:
	i = z.getinfo ("docs/photos/big.log")
	check (i.compress_type == 8 and not i.flag_bits & 8 and i.compress_size < i.file_size // 4, "the big file copied by a rewrite: its sizes now in its header (%d -> %d)" % (i.file_size, i.compress_size))
	i = z.getinfo ("docs/notes.txt")
	check (i.flag_bits & 8 == 0, "a small file with its sizes in its header")
rc, out = run ("delete", "RAM:/work.zip", "docs/images", "a.txt", "b.png")
t = ztree (R ("work.zip"))
check (rc == 0 and not any (k.startswith ("docs/images") for k in t) and "a.txt" not in t, "deleted a folder and files")
zcheck ("work.zip", "after the delete")
rc, out = run ("rename", "RAM:/work.zip", "kernel/sys", "kernel/system")
t = ztree (R ("work.zip"))
check (rc == 0 and t.get ("kernel/system/kapi.cpp") == srcTree["kernel/sys/kapi.cpp"] and not any (k.startswith ("kernel/sys/") for k in t), "a folder renamed")
rc, out = run ("mkdir", "RAM:/work.zip", "docs/new folder")
check (rc == 0 and "docs/new folder/" in ztree (R ("work.zip")), "a new folder")
zcheck ("work.zip", "after rename and mkdir")
check (not any (f.startswith ("work.zip.") for f in os.listdir (RAM)), "no temporary file left")
shutil.copy (R ("crypt.zip"), R ("crypt2.zip"))
rc, out = run ("add", "RAM:/crypt2.zip", "-", "1", "6", "1", "RAM:/add/notes.txt")
rc, out = run ("pw", "RAM:/crypt2.zip", "secret", "RAM:/out_crypt3")
check (rc == 0 or "notes.txt" in out, "encrypted entries still decrypt after a rewrite: " + out)
t = subprocess.run (["unzip", "-tq", "-P", "secret", R ("crypt2.zip")], capture_output = True, text = True)
check (t.returncode == 0, "unzip -t with the password after the rewrite: " + t.stdout.strip ())

print ("a new archive")
rc, out = run ("add", "RAM:/new.zip", "-", "1", "6", "1", "RAM:/src")
check (rc == 0, "made: " + out)
zcheck ("new.zip", "the new archive")
t = ztree (R ("new.zip"))
check ({k[4:]: v for k, v in t.items () if k.startswith ("src/") and v != "dir"} == {k: v for k, v in srcTree.items () if v != "dir"}, "its files are the source's")

print ("%d failure(s)" % fails)
sys.exit (1 if fails else 0)
