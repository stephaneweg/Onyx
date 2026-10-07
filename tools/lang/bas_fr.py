#!/usr/bin/env python3
#
# tools/lang/bas_fr.py -- a BASIC program's words put in French (Onyx BASIC's French words: user/Libs/basic,
# bas::FRENCH): the keywords outside the strings and the comments, and -- from a table, lines "English<TAB>French" -- its comments, its strings and its own names. Made for the
# examples GPIO Lab shows in French (sdcard/basic/examples/fr/, from sdcard/basic/examples/gpio_*.bas):
#
#   python tools/lang/bas_fr.py                      the GPIO examples, again (their table: tools/lang/gpio_fr/gpio.fr.txt)
#   python tools/lang/bas_fr.py in.bas table out.bas
#
# A table's line whose English is a whole comment (without its apostrophe) or a whole string (without its quotes)
# replaces it; one whose English is a name (letters, digits) renames that name in the code. A comment with no
# line in the table is said (it stays in English). The strings keep to ASCII: PRINT writes code page 437.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import glob, os, re, sys

ROOT = os.path.normpath (os.path.join (os.path.dirname (os.path.abspath (__file__)), '..', '..'))
WORDS = { 'IF': 'SI', 'THEN': 'ALORS', 'ELSE': 'SINON', 'ELSEIF': 'SINON SI', 'END': 'FIN', 'FOR': 'POUR', 'TO': 'JUSQUE',
	'STEP': 'PAS', 'NEXT': 'SUITE', 'DO': 'FAIRE', 'LOOP': 'BOUCLE', 'WHILE': 'TANTQUE', 'WEND': 'FIN TANTQUE', 'UNTIL': 'JUSQUA',
	'PRINT': 'AFFICHER', 'INPUT': 'SAISIR', 'FUNCTION': 'FONCTION', 'RETURN': 'RETOUR', 'CALL': 'APPELER', 'EXIT': 'SORTIR',
	'ON': 'SUR', 'OFF': 'ARRET', 'AS': 'COMME', 'INTEGER': 'ENTIER', 'REAL': 'REEL', 'STRING': 'CHAINE', 'BYTE': 'OCTET',
	'AND': 'ET', 'OR': 'OU', 'NOT': 'NON', 'SELECT': 'SELON', 'CASE': 'CAS', 'INKEY$': 'TOUCHE$',
	'PIN': 'BROCHE', 'PINMODE': 'MODEBROCHE', 'PINFREE': 'LIBERERBROCHE', 'PINCHANGED': 'BROCHECHANGEE',
	'I2COPEN': 'I2COUVRIR', 'I2CWRITE': 'I2CECRIRE', 'I2CSEND': 'I2CENVOYER', 'I2CREAD': 'I2CLIRE', 'I2CREAD$': 'I2CLIRE$',
	'SPIOPEN': 'SPIOUVRIR' }
MODES = { 'OUT': 'SORTIE', 'OUTPUT': 'SORTIE', 'IN': 'ENTREE', 'INPUT': 'ENTREE', 'PULLUP': 'RAPPELHAUT', 'PULLDOWN': 'RAPPELBAS', 'FREE': 'LIBRE' }
TOKEN = re.compile (r'"[^"\n]*"?|\'.*|[A-Za-z_][A-Za-z0-9_.]*[$%&!#]?|.', re.S)

def translate (src, table, name = ''):
	out = []
	for line in src.split ('\n'):
		o = ''; data = False
		for m in TOKEN.finditer (line):
			t = m.group (0)
			if data: o += t
			elif t.startswith ("'"):
				body = t[1:]; lead = len (body) - len (body.lstrip (' ')); key = body.strip ()
				if not key: o += t
				elif key in table: o += "'" + ' ' * lead + table[key]
				else: print ('%s: a comment with no French: %r' % (name, key), file = sys.stderr); o += t
			elif t.startswith ('"'):
				s = t.strip ('"')
				if s in table: s = table[s]
				elif s.upper () in MODES and re.search (r'(?i)\b(PINMODE|MODEBROCHE)\b', line): s = MODES[s.upper ()]
				elif re.search (r'[A-Za-z]{3}', s) and not s.startswith ('#'): print ('%s: a string with no French: %r' % (name, s), file = sys.stderr)
				s.encode ('ascii')				# (PRINT writes code page 437: the strings keep to ASCII)
				o += '"' + s + '"'
			elif re.match (r'[A-Za-z_]', t):
				u = t.upper ()
				if u == 'DATA' or u == 'REM': data = True; o += t
				elif u in WORDS: o += WORDS[u]
				elif t in table: o += table[t]
				else: o += t
			else: o += t
		out.append (o)
	return '\n'.join (out)

def table_of (path):
	table = {}
	for l in open (path, encoding = 'utf-8').read ().split ('\n'):
		if l.startswith ('#') or '\t' not in l: continue
		k, v = l.split ('\t', 1); table[k] = v
	return table

def main ():
	a = sys.argv[1:]
	if len (a) == 3:
		open (a[2], 'w', encoding = 'utf-8', newline = '\n').write (translate (open (a[0], encoding = 'utf-8').read (), table_of (a[1]), a[0]))
		return 0
	os.makedirs (os.path.join (ROOT, 'sdcard', 'basic', 'examples', 'fr'), exist_ok = True)
	table = table_of (os.path.join (ROOT, 'tools', 'lang', 'gpio_fr', 'gpio.fr.txt'))
	for path in sorted (glob.glob (os.path.join (ROOT, 'sdcard', 'basic', 'examples', 'gpio_*.bas'))):
		name = os.path.basename (path)[:-4]
		src = open (path, encoding = 'utf-8').read ()
		open (os.path.join (ROOT, 'sdcard', 'basic', 'examples', 'fr', name + '.bas'), 'w', encoding = 'utf-8', newline = '\n').write (translate (src, table, name))
		print ('  sdcard/basic/examples/fr/%s.bas' % name)
	return 0

if __name__ == '__main__': sys.exit (main ())
