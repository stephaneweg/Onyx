#!/usr/bin/env python3
"""tools/tests/archiver/cli_test.py -- /bin/zip and /bin/unzip on the PC (run by run_archiver_test.sh):
built over the simulator's kapi (RAM: = the folder given), their archives checked by zipfile / unzip -t."""
import os, sys, subprocess, shutil, zipfile, random
ZIP, UNZIP, RAM = sys.argv[1], sys.argv[2], sys.argv[3]
fails = 0
def check (c, what):
	global fails
	print (("  ok   " if c else "  FAIL ") + what); fails += 0 if c else 1
def run (tool, *a, stdin = ""):
	r = subprocess.run ([tool], input = stdin, capture_output = True, text = True,
			    env = dict (os.environ, SIM_RAM = RAM, SIM_ARGS = " ".join ('"%s"' % x if " " in x else x for x in a)))
	return r.returncode, "\n".join (l for l in r.stdout.splitlines () if not l.startswith ("sim:"))
def R (*p): return os.path.join (RAM, *p)
def files (d): return sorted (os.path.relpath (os.path.join (a, f), R (d)) for a, _, fs in os.walk (R (d)) for f in fs)
shutil.rmtree (RAM, ignore_errors = True)
os.makedirs (R ("pkg", "my app", "res"))
open (R ("pkg", "my app", "main"), "wb").write (b"\x7fELF" + bytes (1000))
open (R ("pkg", "my app", "app.txt"), "w").write ("name = My App\n" * 50)
random.seed (3); open (R ("pkg", "my app", "res", "data.bin"), "wb").write (bytes (random.getrandbits (8) for _ in range (20000)))
print ("zip / unzip")
rc, out = run (ZIP, "-r", "RAM:/app.zip", "RAM:/pkg/my app")
with zipfile.ZipFile (R ("app.zip")) as z: names = z.namelist (); bad = z.testzip ()
check (rc == 0 and bad is None and "my app/res/data.bin" in names and "my app/" in names, "zip -r: a folder from its last part, with a space in its name")
check (subprocess.run (["unzip", "-tq", R ("app.zip")], capture_output = True).returncode == 0, "unzip -t (Info-ZIP) agrees")
rc, out = run (UNZIP, "-l", "RAM:/app.zip")
check (rc == 0 and "3 files" in out and "my app/app.txt" in out, "unzip -l")
rc, out = run (UNZIP, "-t", "RAM:/app.zip")
check (rc == 0 and "No errors" in out, "unzip -t")
rc, out = run (UNZIP, "-q", "-d", "RAM:/out", "RAM:/app.zip")
check (rc == 0 and files ("out") == ["my app/app.txt", "my app/main", "my app/res/data.bin"], "unzip -d: the folders kept")
check (open (R ("out", "my app", "main"), "rb").read () == open (R ("pkg", "my app", "main"), "rb").read (), "the bytes are the same")
rc, out = run (UNZIP, "-n", "-q", "-d", "RAM:/out", "RAM:/app.zip")
check (rc == 1, "unzip -n: nothing replaced (exit 1)")
rc, out = run (UNZIP, "-d", "RAM:/out", "RAM:/app.zip", "*.txt", stdin = "r\n")
check ("replace" in out and os.path.exists (R ("out", "my app", "app (2).txt")), "the question: [r]ename keeps both")
rc, out = run (UNZIP, "-j", "-o", "-q", "-d", "RAM:/flat", "RAM:/app.zip", "my app/res")
check (files ("flat") == ["data.bin"], "unzip -j with a folder chosen")
rc, out = run (UNZIP, "-o", "-q", "-d", "RAM:/ex", "RAM:/app.zip", "-x", "*.bin")
check (files ("ex") == ["my app/app.txt", "my app/main"], "unzip -x leaves out")
rc, out = run (UNZIP, "-p", "RAM:/app.zip", "my app/app.txt")
check (out.startswith ("name = My App"), "unzip -p to the output")
rc, out = run (ZIP, "-d", "RAM:/app.zip", "my app/res")
with zipfile.ZipFile (R ("app.zip")) as z: names = z.namelist ()
check (rc == 0 and not any ("res" in n for n in names), "zip -d a folder")
rc, out = run (ZIP, "-9", "-p", "docs/notes", "RAM:/app.zip", "RAM:/pkg/my app/app.txt")
with zipfile.ZipFile (R ("app.zip")) as z: i = z.getinfo ("docs/notes/app.txt"); bad = z.testzip ()
check (rc == 0 and bad is None and i.compress_type == 8, "zip -p FOLDER -9")
rc, out = run (ZIP, "-k", "-p", "docs/notes", "RAM:/app.zip", "RAM:/pkg/my app/app.txt")
check (rc == 1, "zip -k: an existing name kept (exit 1)")
rc, out = run (ZIP, "-j", "RAM:/flat.zip", "RAM:/pkg/my app/main", "RAM:/pkg/my app/res/data.bin")
with zipfile.ZipFile (R ("flat.zip")) as z: names = z.namelist ()
check (sorted (names) == ["data.bin", "main"], "zip -j")
rc, out = run (UNZIP, "RAM:/nothere.zip")
check (rc == 2 and "unzip:" in out, "a missing archive: exit 2")
print ("%d failure(s)" % fails)
sys.exit (1 if fails else 0)
