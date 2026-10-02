#!/usr/bin/env python3
# checkpng.py -- read pixels of a PNG (8-bit RGB / RGBA, not interlaced) and check them:
#   checkpng.py <file.png> <x>,<y>=<r>,<g>,<b> ...      each pixel within TOLERANCE (default 3)
#   checkpng.py <file.png> ink=<x>,<y>,<w>,<h>          the box holds pixels that are not white
# Prints PASS / FAIL lines; the exit status is the number of failures. No library needed
# (tools/webkit/test-webcore.sh: the picture painted by wctest).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
import struct, sys, zlib

def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, width, height, channels = 8, b"", 0, 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and colour in (2, 6) and not interlace, "8-bit RGB(A), not interlaced, expected"
            channels = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, previous, pos = [], bytearray(stride), 0
    for _ in range(height):
        kind = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            upleft = previous[i - channels] if i >= channels else 0
            if kind == 1: line[i] = (line[i] + left) & 255
            elif kind == 2: line[i] = (line[i] + up) & 255
            elif kind == 3: line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else upleft)) & 255
        rows.append(line)
        previous = line
    return width, height, channels, rows

def main():
    tolerance = 3
    width, height, channels, rows = read_png(sys.argv[1])
    print("picture: %d x %d" % (width, height))
    failures = 0
    for check in sys.argv[2:]:
        what, want = check.split("=")
        if what == "ink":
            x, y, w, h = map(int, want.split(","))
            ink = sum(1 for j in range(y, y + h) for i in range(x, x + w)
                      if min(rows[j][i * channels:i * channels + 3]) < 200)
            ok = ink >= 20
            print("%s  ink in %s: %d pixels" % ("PASS" if ok else "FAIL", want, ink))
        else:
            x, y = map(int, what.split(","))
            rgb = tuple(rows[y][x * channels:x * channels + 3])
            wanted = tuple(map(int, want.split(",")))
            ok = all(abs(a - b) <= tolerance for a, b in zip(rgb, wanted))
            print("%s  pixel %s: %s (want %s)" % ("PASS" if ok else "FAIL", what, rgb, wanted))
        failures += not ok
    sys.exit(failures)

main()
