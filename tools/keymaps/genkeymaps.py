#!/usr/bin/env python3
#
# genkeymaps.py -- compile Circle's built-in keyboard tables into Onyx .kmap files.
#
# Onyx loads keymaps from SD:/etc/keymaps/<NAME>.kmap at runtime: the `keyb` tool
# reads the file and hands the bytes to the kernel (kapi_set_keymap_data), which
# copies them into the live keyboard. New layouts can thus be added without
# recompiling the kernel. This tool seeds the stock layouts straight from the
# country tables in circle/lib/input/keymap_*.h, so they match byte-for-byte what
# the kernel used to compile in.
#
# .kmap format (little-endian):
#   "OKM1"                     4 bytes  magic
#   rows  u16 = PHY_MAX_CODE+1  (128)
#   cols  u16 = K_CTRLTAB+1     (5)
#   table u16[rows][cols]       row-major m_KeyMap[phyCode][table]
#
# Usage:  python tools/keymaps/genkeymaps.py [outdir]   (default: sdcard/etc/keymaps)
#
import os, re, ast, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KEYMAP_H = os.path.join(ROOT, "circle", "include", "circle", "input", "keymap.h")
TBL_DIR  = os.path.join(ROOT, "circle", "lib", "input")
ROWS, COLS = 128, 5                                  # PHY_MAX_CODE+1, K_CTRLTAB+1
LOCALES = ["DE", "DV", "ES", "FR", "IT", "UK", "US"]

# --- parse the TSpecialKey enum from keymap.h (drift-proof: no hand-copied values) --
def parse_enum():
    txt = open(KEYMAP_H, encoding="latin-1").read()
    body = re.search(r"enum\s+TSpecialKey\s*\{(.*?)\}", txt, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    vals, cur = {}, 0
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        m = re.match(r"([A-Za-z_]\w*)\s*(?:=\s*(0x[0-9A-Fa-f]+|\d+))?$", item)
        if not m:
            continue
        name, expr = m.group(1), m.group(2)
        cur = int(expr, 0) if expr is not None else cur
        vals[name] = cur
        cur += 1
    return vals

ENUM = parse_enum()

# A cell is one of: KeyXxx (enum), a bare char literal 'a'/'@', or C('<char>') ==
# ((u16)(u8)chr). The char literal may be a hex escape (\xA3), a simple escape
# (\t, \\, \'), or any single char (incl. ')' , ','). C(...) must come first in the
# alternation so its inner literal isn't matched on its own.
CHAR = r"'(?:\\x[0-9A-Fa-f]+|\\.|[^'])'"
TOKEN = re.compile(r"C\(" + CHAR + r"\)|" + CHAR + r"|Key\w+")

def cell_value(tok):
    if tok.startswith("C("):
        tok = tok[2:-1]                              # unwrap C( ... )
    if tok.startswith("'"):
        return ord(ast.literal_eval(tok)) & 0xFF     # char literal -> (u8) code
    if tok not in ENUM:
        raise ValueError("unknown key token: " + tok)
    return ENUM[tok]

def compile_table(locale):
    path = os.path.join(TBL_DIR, "keymap_%s.h" % locale.lower())
    txt = open(path, encoding="latin-1").read()
    txt = re.sub(r"//[^\n]*", "", txt)               # drop end-of-line comments (0x7F etc.)
    toks = TOKEN.findall(txt)
    if len(toks) != ROWS * COLS:
        raise ValueError("%s: got %d cells, expected %d" % (locale, len(toks), ROWS * COLS))
    return [cell_value(t) for t in toks]

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "sdcard", "etc", "keymaps")
    os.makedirs(outdir, exist_ok=True)
    for lc in LOCALES:
        cells = compile_table(lc)
        with open(os.path.join(outdir, lc + ".kmap"), "wb") as f:
            f.write(b"OKM1")
            f.write(struct.pack("<HH", ROWS, COLS))
            f.write(struct.pack("<%dH" % len(cells), *cells))
        print("wrote %s.kmap (%d bytes)" % (lc, 8 + 2 * len(cells)))

if __name__ == "__main__":
    main()
