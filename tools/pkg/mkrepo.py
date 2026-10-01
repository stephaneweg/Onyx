#!/usr/bin/env python3
"""mkrepo.py -- make Onyx's package repository from sdcard/ (docs/pkg/README.md).

    python3 tools/pkg/mkrepo.py --out ../onyx-packages [--key ~/.onyx/pkg-key.pem] [--bump] [--db]

tools/pkg/packages.ini says which files make each package, tools/pkg/versions.ini their versions.
For each package: a .opk (a ZIP of its files, the card's tree, plus PKG/manifest.ini) in <out>/pkgs/,
its icon in <out>/icons/; then <out>/index.txt (every package: version, size, SHA-256...) signed into
<out>/index.sig (ECDSA P-256 / SHA-256, DER, in hex) with the private key.

A package whose files changed while its version did not is an error -- unless --bump, which raises
the version's last number (versions.ini is rewritten). An unchanged package keeps its .opk.
--db also writes sdcard/var/pkg/db/<name>.ini: the card made "installed" (what pkg reads).
--lite DIR also makes a card of the required packages only (the system and the firmware) with their
database: the packages manager's test card (sdcard_lite), every other package installed from there.
--sd DIR: another card (tests); --versions FILE: another versions.ini; --no-sign: no index.sig.
"""
import argparse, configparser, fnmatch, hashlib, io, os, re, sys, zipfile, datetime

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))

def sha256_file (p):
	h = hashlib.sha256 ()
	with open (p, "rb") as f:
		for b in iter (lambda: f.read (1 << 20), b""): h.update (b)
	return h.hexdigest ()

def read_app_txt (sd, app):
	d = {}
	p = os.path.join (sd, "apps", app + ".app", "app.txt")
	if os.path.exists (p):
		for line in open (p, encoding = "utf-8", errors = "replace"):
			line = line.split ("#")[0].strip ()
			if "=" in line:
				k, v = line.split ("=", 1); d[k.strip ()] = v.strip ()
	return d

def kapi_version ():
	t = open (os.path.join (ROOT, "kernel", "include", "kern", "kapi_abi.h")).read ()
	return int (re.search (r"#define\s+KAPI_ABI_VERSION\s+(\d+)", t).group (1))

def card_files (sd):
	"""Every file of the card, '/'-separated, relative (var/ left out: the card's own state)."""
	out = []
	for dp, dns, fns in os.walk (sd):
		dns.sort ()
		rel = os.path.relpath (dp, sd).replace (os.sep, "/")
		if rel == ".": rel = ""
		if rel == "var" or rel.startswith ("var/"): dns[:] = []; continue
		for f in sorted (fns):
			out.append ((rel + "/" if rel else "") + f)
	return out

def matches (path, pat):
	if pat.endswith ("/"): return path.startswith (pat)
	if any (ch in pat for ch in "*?["): return fnmatch.fnmatchcase (path, pat)
	return path == pat

def split (v): return v.split () if v else []

def bump (v):
	n = v.split (".")
	n[-1] = str (int (n[-1]) + 1) if n[-1].isdigit () else n[-1] + ".1"
	return ".".join (n)

def vkey (v): return [int (x) if x.isdigit () else 0 for x in re.split (r"[.\-]", v)]

def plan (sd, ini):
	"""-> [package dict], in the ini's order; the files left to no package."""
	files = card_files (sd)
	taken = {}
	pkgs = []
	cfg = configparser.ConfigParser (interpolation = None, comment_prefixes = ("#", ";"), inline_comment_prefixes = (";",))
	cfg.optionxform = str
	cfg.read (ini, encoding = "utf-8")
	apps = sorted (d[:-4] for d in os.listdir (os.path.join (sd, "apps")) if d.endswith (".app"))
	app_cat = { a: read_app_txt (sd, a).get ("category", "Other") for a in apps }
	def take (name, pats):
		got = []
		for f in files:
			if f in taken: continue
			if any (matches (f, p) for p in pats): taken[f] = name; got.append (f)
		return got
	def make (name, sec, pats, extra = {}):
		p = { "name": name, "title": sec.get ("title", name), "category": sec.get ("category", "Other"),
		      "summary": sec.get ("summary", ""), "author": sec.get ("author", "Onyx"),
		      "needs": [n.strip () for n in sec.get ("needs", "").split (",") if n.strip ()],
		      "required": sec.get ("required", "0"), "restart": sec.get ("restart", "0"),
		      "config_pats": split (sec.get ("config", "")), "icon": sec.get ("icon", "") }
		p.update (extra)
		p["files"] = take (name, pats)
		pkgs.append (p)
	for s in cfg.sections ():
		if s.startswith ("app."): continue
		sec = cfg[s]
		if s == "*apps":
			for a in apps:
				if any (f.startswith ("apps/%s.app/" % a) and f in taken for f in files): continue
				at = read_app_txt (sd, a)
				x = cfg["app." + a] if cfg.has_section ("app." + a) else {}
				pats = ["apps/%s.app/" % a] + split (x.get ("files", "") if x else "")
				d = dict (x) if x else {}
				d.setdefault ("title", at.get ("name", a)); d.setdefault ("category", at.get ("category", "Other"))
				d.setdefault ("icon", "apps/%s.app/icon.bmp" % a)
				make (a, d, pats)
			continue
		pats = split (sec.get ("files", ""))
		for c in split (sec.get ("apps", "")):
			pats += ["apps/%s.app/" % a for a in apps if app_cat[a] == c]
		icon = sec.get ("icon", "")
		if not icon:
			own = [a for a in apps if any (pt == "apps/%s.app/" % a for pt in pats)]
			need = [n.split ()[0] for n in sec.get ("needs", "").split (",") if n.strip () and n.split ()[0] in apps]
			icon = "apps/%s.app/icon.bmp" % (own[0] if own else need[0] if need else "control")	# (a sample: its app's)
		d = dict (sec); d["icon"] = icon
		make (s, d, pats)
	left = [f for f in files if f not in taken]
	return pkgs, left

def manifest_text (p, version, kapi):
	needs = p["needs"] + ["kapi >= %d" % kapi]
	cfgfiles = [f for f in p["files"] if any (matches (f, c) for c in p["config_pats"])]
	lines = ["# Onyx package manifest (tools/pkg/mkrepo.py)",
		 "name = " + p["name"], "title = " + p["title"], "version = " + version,
		 "category = " + p["category"], "author = " + p["author"], "summary = " + p["summary"],
		 "needs = " + ", ".join (needs), "required = " + p["required"], "restart = " + p["restart"],
		 "installed = %d" % p["bytes"], "config = " + " ".join (cfgfiles)]
	return "\n".join (lines) + "\n", needs, cfgfiles

def write_opk (path, sd, p, manifest):
	"""A deterministic ZIP: the same files give the same bytes."""
	tmp = path + ".part"
	with zipfile.ZipFile (tmp, "w", zipfile.ZIP_DEFLATED, compresslevel = 9) as z:
		def add (name, data):
			zi = zipfile.ZipInfo (name, (1980, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED
			zi.external_attr = 0o644 << 16; z.writestr (zi, data)
		add ("PKG/manifest.ini", manifest.encode ())
		for f in p["files"]:
			with open (os.path.join (sd, f), "rb") as fh: add (f, fh.read ())
	os.replace (tmp, path)

def read_index (path):
	cfg = configparser.ConfigParser (interpolation = None); cfg.optionxform = str
	if os.path.exists (path): cfg.read (path, encoding = "utf-8")
	return cfg

def sign (data, keyfile):
	try:
		from cryptography.hazmat.primitives import hashes, serialization
		from cryptography.hazmat.primitives.asymmetric import ec
		key = serialization.load_pem_private_key (open (keyfile, "rb").read (), password = None)
		return key.sign (data, ec.ECDSA (hashes.SHA256 ())).hex ()
	except ImportError:
		import subprocess, tempfile
		with tempfile.NamedTemporaryFile (delete = False) as t: t.write (data)
		der = subprocess.check_output (["openssl", "dgst", "-sha256", "-sign", keyfile, t.name]); os.unlink (t.name)
		return der.hex ()

def write_db (db, pkgs):
	"""SD:/var/pkg/db/<name>.ini for each package: the card made "installed" (what pkg reads)."""
	os.makedirs (db, exist_ok = True)
	for f in os.listdir (db):
		if f.endswith (".ini"): os.remove (os.path.join (db, f))
	for p in pkgs:
		L = ["# installed by tools/pkg/mkrepo.py", "[package]", "name = " + p["name"], "title = " + p["title"],
		     "version = " + p["version"], "category = " + p["category"], "author = " + p["author"], "summary = " + p["summary"],
		     "mode = manual", "required = " + p["required"], "restart = " + p["restart"], "needs = " + ", ".join (p["needs_all"]),
		     "config = " + " ".join (p["cfgfiles"]), "", "[files]"] + ["%s = %s" % h for h in p["hashes"]]
		open (os.path.join (db, p["name"] + ".ini"), "w", encoding = "utf-8").write ("\n".join (L) + "\n")

def main ():
	ap = argparse.ArgumentParser ()
	ap.add_argument ("--out", required = True); ap.add_argument ("--key"); ap.add_argument ("--bump", action = "store_true")
	ap.add_argument ("--db", action = "store_true"); ap.add_argument ("--sd", default = os.path.join (ROOT, "sdcard"))
	ap.add_argument ("--ini", default = os.path.join (HERE, "packages.ini"))
	ap.add_argument ("--versions", default = os.path.join (HERE, "versions.ini"))
	ap.add_argument ("--lite", help = "also make this card: the required packages only");
	ap.add_argument ("--no-sign", action = "store_true"); ap.add_argument ("--repo-name", default = "onyx-packages")
	a = ap.parse_args ()
	if not a.no_sign and not a.key: sys.exit ("mkrepo: --key KEY.pem (the private key: tools/pkg/keygen.py) or --no-sign")
	kapi = kapi_version ()
	pkgs, left = plan (a.sd, a.ini)
	if left:
		print ("mkrepo: %d file(s) in no package:" % len (left), file = sys.stderr)
		for f in left[:20]: print ("   " + f, file = sys.stderr)
	vcfg = configparser.ConfigParser (interpolation = None); vcfg.optionxform = str
	if os.path.exists (a.versions): vcfg.read (a.versions, encoding = "utf-8")
	if not vcfg.has_section ("versions"): vcfg.add_section ("versions")
	V = vcfg["versions"]
	old = read_index (os.path.join (a.out, "index.txt"))
	os.makedirs (os.path.join (a.out, "pkgs"), exist_ok = True); os.makedirs (os.path.join (a.out, "icons"), exist_ok = True)
	errors, changed = [], []
	for p in pkgs:
		hashes = [(f, sha256_file (os.path.join (a.sd, f))) for f in p["files"]]
		p["hashes"] = hashes
		p["bytes"] = sum (os.path.getsize (os.path.join (a.sd, f)) for f in p["files"])
		# the content: the files and what the manifest says of them (a new need is a new version)
		desc = "needs %s\nconfig %s\nrequired %s\nrestart %s\n" % (p["needs"], p["config_pats"], p["required"], p["restart"])
		p["content"] = hashlib.sha256 ((desc + "".join ("%s %s\n" % h for h in hashes)).encode ()).hexdigest ()
		ver = V.get (p["name"], "2026.10.0" if p["name"] == "onyx" else "1.0.0")
		if old.has_section (p["name"]) and old[p["name"]].get ("content") != p["content"] and vkey (ver) <= vkey (old[p["name"]].get ("version", "0")):
			if a.bump: ver = bump (old[p["name"]]["version"]); changed.append ("%s -> %s" % (p["name"], ver))
			else: errors.append ("%s %s: its files changed (--bump, or raise it in versions.ini)" % (p["name"], ver))
		V[p["name"]] = ver; p["version"] = ver
	if errors: sys.exit ("mkrepo:\n  " + "\n  ".join (errors))
	with open (a.versions, "w", encoding = "utf-8") as f:
		f.write ("# tools/pkg/versions.ini -- each package's version (mkrepo.py --bump raises them)\n")
		vcfg.write (f)
	idx = ["# Onyx package index -- tools/pkg/mkrepo.py (docs/pkg/README.md)", "[repo]", "name = " + a.repo_name,
	       "date = " + datetime.datetime.utcnow ().strftime ("%Y-%m-%d %H:%M"), "kapi = %d" % kapi, "packages = %d" % len (pkgs), ""]
	for p in pkgs:
		man, needs, cfgfiles = manifest_text (p, p["version"], kapi)
		p["needs_all"], p["cfgfiles"] = needs, cfgfiles
		fn = "pkgs/%s-%s.opk" % (p["name"], p["version"])
		path = os.path.join (a.out, fn)
		o = old[p["name"]] if old.has_section (p["name"]) else None
		if not (o and o.get ("content") == p["content"] and o.get ("version") == p["version"] and os.path.exists (path)):
			write_opk (path, a.sd, p, man)
		icon = os.path.join (a.sd, p["icon"]) if p["icon"] else ""
		if icon and os.path.exists (icon):
			with open (icon, "rb") as s, open (os.path.join (a.out, "icons", p["name"] + ".bmp"), "wb") as d: d.write (s.read ())
		idx += ["[%s]" % p["name"], "title = " + p["title"], "version = " + p["version"], "category = " + p["category"],
			"author = " + p["author"], "summary = " + p["summary"], "size = %d" % os.path.getsize (path),
			"installed = %d" % p["bytes"], "sha256 = " + sha256_file (path), "file = " + fn,
			"icon = icons/%s.bmp" % p["name"], "needs = " + ", ".join (needs), "required = " + p["required"],
			"restart = " + p["restart"], "content = " + p["content"], ""]
	keep = set ("%s-%s.opk" % (p["name"], p["version"]) for p in pkgs)	# the old versions' archives dropped
	for f in os.listdir (os.path.join (a.out, "pkgs")):
		if f.endswith (".opk") and f not in keep: os.remove (os.path.join (a.out, "pkgs", f))
	data = ("\n".join (idx)).encode ()
	# nothing changed (the same packages, versions, archives): the index and its signature left as they are
	ip = os.path.join (a.out, "index.txt")
	oldtext = open (ip, "rb").read () if os.path.exists (ip) else b""
	strip = lambda t: re.sub (rb"\ndate = [^\n]*", b"", t)
	if strip (oldtext) == strip (data) and os.path.exists (os.path.join (a.out, "index.sig")):
		print ("mkrepo: no package changed: the index kept")
	else:
		open (ip, "wb").write (data)
		if not a.no_sign:
			open (os.path.join (a.out, "index.sig"), "w").write (sign (data, a.key) + "\n")
	if a.db: write_db (os.path.join (a.sd, "var", "pkg", "db"), pkgs)
	if a.lite:
		# the smallest card: the required packages only (the system, the firmware) and their
		# database -- every other package installed by pkg / the package manager
		import shutil
		shutil.rmtree (a.lite, ignore_errors = True)
		req = [p for p in pkgs if p["required"] == "1"]
		for p in req:
			for f in p["files"]:
				d = os.path.join (a.lite, f); os.makedirs (os.path.dirname (d), exist_ok = True)
				shutil.copy2 (os.path.join (a.sd, f), d)
		write_db (os.path.join (a.lite, "var", "pkg", "db"), req)
		print ("mkrepo: %s: %s (%.1f MB)" % (a.lite, ", ".join (p["name"] for p in req), sum (p["bytes"] for p in req) / 1e6))
	print ("mkrepo: %d packages -> %s%s" % (len (pkgs), a.out, ("; bumped: " + ", ".join (changed)) if changed else ""))

if __name__ == "__main__":
	main ()
