#!/usr/bin/env python3
#
# tools/lang/check.py -- an app's words against its catalogues (uikit/lang.h): every TR ("...") and
# TRC ("ctx", "...") of user/Apps/<app>/*.cpp, *.h must have its line in sdcard/apps/<app>.app/lang/<code>.txt
# (or in uikit's own sdcard/res/lang/<code>.txt).
#
#   python tools/lang/check.py <app> [<app>...]     the words missing, the lines no source uses any more
#   python tools/lang/check.py --all                every app that has a lang/ folder
#   python tools/lang/check.py --keys <app>         the app's words, one a line (to start a catalogue)
#
# -> exit 1 when a word is missing. A word given to TR () through a variable (a table of English words,
# TR (T[i])) is not seen: mark the table's words with TRN ("...") -- a macro that is the word itself -- or
# list them in a comment "// TR: word" ... the script takes both.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import glob, os, re, sys

ROOT = os.path.normpath (os.path.join (os.path.dirname (os.path.abspath (__file__)), '..', '..'))
LANGS = ['fr']
STR = r'"(?:[^"\\\n]|\\.)*"'
CAT = r'(?:' + STR + r'\s*)+'
RE_TR = re.compile (r'\b(?:TR|TRN)\s*\(\s*(' + CAT + r')\)')
RE_TRC = re.compile (r'\bTRC\s*\(\s*(' + CAT + r'),\s*(' + CAT + r')\)')

def unescape (lit):				# a C literal's (or several adjacent ones') text
	out = bytearray ()
	for m in re.finditer (STR, lit):
		s = m.group (0)[1:-1]; i = 0
		while i < len (s):
			c = s[i]
			if c != '\\': out += c.encode ('utf-8'); i += 1; continue
			i += 1; c = s[i]
			if c == 'n': out += b'\n'; i += 1
			elif c == 't': out += b'\t'; i += 1
			elif c == 'x':
				j = i + 1
				while j < len (s) and s[j] in '0123456789abcdefABCDEF': j += 1
				out.append (int (s[i + 1:j], 16) & 255); i = j
			elif c in '01234567':
				j = i
				while j < len (s) and j < i + 3 and s[j] in '01234567': j += 1
				out.append (int (s[i:j], 8) & 255); i = j
			else: out += c.encode ('utf-8'); i += 1
	return out.decode ('utf-8', 'replace')

def cat_unescape (s):				# a catalogue's field: \t \n \\
	return re.sub (r'\\(.)', lambda m: {'t': '\t', 'n': '\n'}.get (m.group (1), m.group (1)), s)

def source_keys (app):
	keys = {}
	for path in sorted (glob.glob (os.path.join (ROOT, 'user', 'Apps', app, '**', '*'), recursive = True)):
		if not path.endswith (('.cpp', '.h', '.hpp', '.inc')): continue
		text = open (path, encoding = 'utf-8', errors = 'replace').read ()
		rel = os.path.relpath (path, ROOT).replace ('\\', '/')
		for m in RE_TRC.finditer (text):
			keys.setdefault (unescape (m.group (1)) + '|' + unescape (m.group (2)), (rel, unescape (m.group (2))))
		for m in RE_TR.finditer (text):
			k = unescape (m.group (1))
			if k: keys.setdefault (k, (rel, None))
		for m in re.finditer (r'//\s*TR:\s*(.+)', text):
			keys.setdefault (m.group (1).strip (), (rel, None))
	return keys

def catalogue (path):
	words = {}
	if not os.path.exists (path): return None
	for line in open (path, encoding = 'utf-8-sig').read ().split ('\n'):
		line = line.rstrip ('\r')
		if not line or line.startswith ('#') or '\t' not in line: continue
		k, v = line.split ('\t', 1)
		if v: words[cat_unescape (k)] = cat_unescape (v)
	return words

def check (app):
	keys = source_keys (app); bad = 0
	for code in LANGS:
		own = catalogue (os.path.join (ROOT, 'sdcard', 'apps', app + '.app', 'lang', code + '.txt'))
		base = catalogue (os.path.join (ROOT, 'sdcard', 'res', 'lang', code + '.txt')) or {}
		if own is None: print ('%s: no lang/%s.txt (%d words)' % (app, code, len (keys))); bad += 1; continue
		missing = [k for k, (src, plain) in keys.items () if k not in own and k not in base and not (plain is not None and (plain in own or plain in base))]
		unused = [k for k in own if k not in keys]
		for k in missing: print ('%s [%s] missing: %r  (%s)' % (app, code, k, keys[k][0]))
		for k in unused: print ('%s [%s] not used: %r' % (app, code, k))
		print ('%s [%s]: %d words, %d missing, %d not used' % (app, code, len (keys), len (missing), len (unused)))
		bad += len (missing)
	return bad

def main ():
	a = sys.argv[1:]
	if not a: print (__doc__ or 'usage: check.py <app>... | --all | --keys <app>'); return 2
	if a[0] == '--keys':
		for k in source_keys (a[1]): print (k.replace ('\\', '\\\\').replace ('\t', '\\t').replace ('\n', '\\n') + '\t')
		return 0
	if a[0] == '--all':
		a = sorted (os.path.basename (os.path.dirname (os.path.dirname (p)))[:-4] for p in glob.glob (os.path.join (ROOT, 'sdcard', 'apps', '*.app', 'lang', '')))
	bad = sum (check (app) for app in a)
	return 1 if bad else 0

if __name__ == '__main__': sys.exit (main ())
