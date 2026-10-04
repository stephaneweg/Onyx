#!/usr/bin/env python3
# gen_cursors.py -- the pointer's shapes (kapi v81 set_cursor), drawn here and written as
# kernel/gui/cursors.inc: black shapes with a white edge, as the arrow. Each shape is a set of black
# pixels; the white edge is every pixel that touches one. `python3 tools/gui/gen_cursors.py` writes the
# file again (and prints the shapes with --show).
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
import os, sys

def outline(black):
    """-> rows of 'B', 'W', '.', and the offset the shape was moved by (so a hot spot follows)"""
    white = set()
    for (x, y) in black:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                p = (x + dx, y + dy)
                if p not in black: white.add(p)
    pts = black | white
    x0 = min(x for x, y in pts); y0 = min(y for x, y in pts)
    x1 = max(x for x, y in pts); y1 = max(y for x, y in pts)
    rows = []
    for y in range(y0, y1 + 1):
        rows.append(''.join('B' if (x, y) in black else 'W' if (x, y) in white else '.' for x in range(x0, x1 + 1)))
    return rows, x0, y0

def from_art(art):
    black = set()
    for y, row in enumerate(art):
        for x, c in enumerate(row):
            if c == 'B': black.add((x, y))
    return black

def line(black, x0, y0, x1, y1):
    n = max(abs(x1 - x0), abs(y1 - y0)) or 1
    for i in range(n + 1):
        black.add((round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n)))

def head(black, tx, ty, dx, dy, n=4):
    """an arrow head whose tip is (tx, ty), pointing along (dx, dy) (an axis), n rows deep"""
    for k in range(n):
        for j in range(-k, k + 1):
            black.add((tx - dx * k + (j if dx == 0 else 0), ty - dy * k + (j if dy == 0 else 0)))

def hand():
    return from_art([
        "....BB...........",
        "....BB...........",
        "....BB...........",
        "....BB...........",
        "....BB.BB........",
        "....BB.BB.BB.....",
        "....BB.BB.BB.BB..",
        "....BB.BB.BB.BB..",
        "BB..BBBBBBBBBBB..",
        "BBB.BBBBBBBBBBB..",
        ".BB.BBBBBBBBBBB..",
        "..BBBBBBBBBBBBB..",
        "..BBBBBBBBBBBBB..",
        "...BBBBBBBBBBBB..",
        "...BBBBBBBBBBB...",
        "....BBBBBBBBBB...",
        "....BBBBBBBBBB...",
        ".....BBBBBBBB....",
        ".....BBBBBBBB....",
    ]), (4, 0)

def text():
    b = set()
    line(b, 3, 1, 3, 15)
    for x in (1, 2, 4, 5): b.add((x, 0)); b.add((x, 16))
    return b, (3, 8)

def move():
    b = set(); c = 9
    line(b, c, 1, c, 17); line(b, 1, c, 17, c)
    head(b, c, 0, 0, -1); head(b, c, 18, 0, 1); head(b, 0, c, -1, 0); head(b, 18, c, 1, 0)
    return b, (c, c)

def size_h():
    b = set(); c = 4
    line(b, 1, c, 17, c); head(b, 0, c, -1, 0); head(b, 18, c, 1, 0)
    return b, (9, c)

def size_v():
    b = set(); c = 4
    line(b, c, 1, c, 17); head(b, c, 0, 0, -1); head(b, c, 18, 0, 1)
    return b, (c, 9)

def size_diag(mirror):
    b = set(); n = 14
    for i in range(2, n - 1):
        b.add((i, i)); b.add((i + 1, i))
    for k in range(6):                      # the two heads: corners
        for j in range(6 - k):
            b.add((k, j)); b.add((n - k, n - j))
    if mirror: b = set((n - x, y) for x, y in b)
    return b, (n // 2, n // 2)

def cell():                                 # a thick plus: the spreadsheet's cell pointer
    b = set(); c = 7
    for i in range(0, 15):
        for d in (-1, 0, 1):
            b.add((i, c + d)); b.add((c + d, i))
    return b, (c, c)

def crosshair():
    b = set(); c = 9
    for i in range(0, 19):
        if abs(i - c) > 2: b.add((i, c)); b.add((c, i))
    b.add((c, c))
    return b, (c, c)

def wait():                                 # an hourglass
    b = set()
    for x in range(0, 11):
        for y in (0, 1, 15, 16): b.add((x, y))
    for t in range(0, 5):
        b.add((1 + t, 2 + t)); b.add((9 - t, 2 + t)); b.add((1 + t, 14 - t)); b.add((9 - t, 14 - t))
    b.add((5, 7)); b.add((5, 8)); b.add((5, 9))
    for t in range(2, 5):                   # the sand left in the top, the heap below
        for x in range(1 + t, 10 - t): b.add((x, 2 + t))
    for x in range(3, 8): b.add((x, 13))
    for x in range(4, 7): b.add((x, 12))
    return b, (5, 8)

def no():                                   # a barred circle
    b = set(); c = 9
    for x in range(0, 19):
        for y in range(0, 19):
            d2 = (x - c) ** 2 + (y - c) ** 2
            if 49 <= d2 <= 81: b.add((x, y))
            elif d2 < 49 and abs(x - y) <= 1: b.add((x, y))
    return b, (c, c)

SHAPES = [                                  # the order is kapi_abi.h's KAPI_CURSOR_*
    ('ARROW', None), ('HAND', hand), ('TEXT', text), ('MOVE', move), ('SIZE_H', size_h), ('SIZE_V', size_v),
    ('SIZE_NWSE', lambda: size_diag(False)), ('SIZE_NESW', lambda: size_diag(True)), ('CELL', cell),
    ('CROSSHAIR', crosshair), ('WAIT', wait), ('NO', no),
]

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, '..', '..', 'kernel', 'gui', 'cursors.inc')
    if len(sys.argv) > 2 and sys.argv[1] == '--out': out = sys.argv[2]
    text_ = ['// cursors.inc -- the pointer\'s shapes (kapi v81 set_cursor): W white, B black, . transparent.',
             '// GENERATED by tools/gui/gen_cursors.py: change the shapes there. Entry 0 (the arrow) is',
             '// kernel.cpp\'s own s_Cursor.',
             'struct TCursorArt { int nW, nH, nHotX, nHotY; const char *pRows; };',
             'static const TCursorArt s_CursorArt[] = {', '\t{ 0, 0, 0, 0, "" },\t\t// 0 ARROW']
    for i, (name, fn) in enumerate(SHAPES):
        if fn is None: continue
        black, (hx, hy) = fn()
        rows, x0, y0 = outline(black)
        w, h = len(rows[0]), len(rows)
        assert all(len(r) == w for r in rows) and w <= 24 and h <= 24, name
        if '--show' in sys.argv:
            print(name, w, h, 'hot', hx - x0, hy - y0); print('\n'.join(rows)); print()
        text_.append('\t{ %d, %d, %d, %d,\t\t// %d %s' % (w, h, hx - x0, hy - y0, i, name))
        for r in rows: text_.append('\t  "%s"' % r)
        text_.append('\t},')
    text_.append('};')
    open(out, 'w', newline='\n').write('\n'.join(text_) + '\n')
    print('written:', os.path.normpath(out), len(SHAPES), 'shapes')

main()
