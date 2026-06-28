#!/usr/bin/env python3
#
# gen_nssans.py -- generate an Onyx ".fnt" font family from NetSurf's "ns-sans" master
# font (third_party/netsurf/.../res/fonts/glyph_data, MIT, (c) Tim Tyler / Michael
# Drake). That source is plain-text ASCII-art: one block per Unicode codepoint, each
# block holding 16 rows x 4 style columns (Regular / Italic / Bold / Bold+Italic),
# '#' = ink, '.' = paper, fixed 8-px-wide columns at fixed offsets.
#
# Output: ONE file sdcard/fonts/ns-sans.fnt holding all four styles, in the Onyx font
# family format read by wtk::Font:
#     0   "ONYF"                         magic
#     4   version (u8)        = 1
#     5   width   (u8)        = 8        glyph width  (px)
#     6   height  (u8)        = 16       glyph height (px)
#     7   styles  (u8)        = 4        number of style blocks
#     8   glyphs  (u16 LE)    = 256      glyphs per style (Latin-1)
#     10  reserved (u16 LE)   = 0
#     12  offset[4] (u32 LE each)        absolute file offset of each style block
#                                        index = (bold?2:0)|(italic?1:0):
#                                        0=regular 1=italic 2=bold 3=bold+italic
#     28  style blocks: each glyphs * height bytes, 1 byte/row, bit 0x80 = leftmost px.
# Missing codepoints are emitted blank.
#
import os, re, struct

HERE   = os.path.dirname(os.path.abspath(__file__))
ROOT   = os.path.abspath(os.path.join(HERE, '..', '..'))
SRC    = os.path.join(ROOT, 'third_party', 'netsurf', 'frontends', 'framebuffer', 'res', 'fonts', 'glyph_data')
OUTDIR = os.path.join(ROOT, 'sdcard', 'fonts')
OUT    = os.path.join(OUTDIR, 'ns-sans.fnt')

W, H    = 8, 16
NGLYPH  = 256					# Latin-1 range U+0000..U+00FF
NSTYLE  = 4					# regular, italic, bold, bold+italic
HDRSZ   = 12 + 4 * NSTYLE			# 28
# Fixed column offsets of the four style glyphs within a row line (Reg, Ital, Bold, B+I).
COLS    = [(3, 11), (14, 22), (25, 33), (36, 44)]

def row_byte(token):
    token = token.ljust(W)[:W]
    b = 0
    for x, ch in enumerate(token):
        if ch == '#':
            b |= (0x80 >> x)
    return b

def parse(path):
    lines = open(path, encoding='latin-1').read().split('\n')
    glyphs = {}					# cp -> [ [16 bytes] x 4 styles ]
    cp_re = re.compile(r'^U\+([0-9A-Fa-f]{4})')
    i, n = 0, len(lines)
    while i < n:
        m = cp_re.match(lines[i].strip())
        if not m:
            i += 1
            continue
        cp = int(m.group(1), 16)
        j = i + 1				# find the dotted "- - -" separator
        while j < n and not lines[j].lstrip().startswith('- -'):
            j += 1
        rows = lines[j + 1: j + 1 + H]		# the 16 glyph rows
        styles = [[0] * H for _ in range(NSTYLE)]
        for r, line in enumerate(rows):
            line = line.ljust(44)
            for c, (a, b) in enumerate(COLS):
                styles[c][r] = row_byte(line[a:b])
        glyphs[cp] = styles
        i = j + 1 + H
    return glyphs

def main():
    glyphs = parse(SRC)
    os.makedirs(OUTDIR, exist_ok=True)
    blocksz = NGLYPH * H
    offsets = [HDRSZ + s * blocksz for s in range(NSTYLE)]
    hdr = b'ONYF' + struct.pack('<BBBBHH', 1, W, H, NSTYLE, NGLYPH, 0)
    hdr += struct.pack('<%dI' % NSTYLE, *offsets)
    assert len(hdr) == HDRSZ, len(hdr)
    body = bytearray()
    for s in range(NSTYLE):
        for cp in range(NGLYPH):
            g = glyphs.get(cp)
            body += bytes(g[s] if g else [0] * H)
    with open(OUT, 'wb') as f:
        f.write(hdr + body)
    print('wrote %s (%d bytes: %dx%d, %d styles x %d glyphs)'
          % (OUT, HDRSZ + len(body), W, H, NSTYLE, NGLYPH))

if __name__ == '__main__':
    main()
