#!/usr/bin/env python3
# mknds.py -- packs an ARM9 and an ARM7 program into a .nds image (a plain header, no secure area),
# with a data blob at 0x10000 (the cartridge-read test reads it back): mknds.py out.nds TITLE CODE arm9.bin arm7.bin
import sys, struct

def crc16(data, crc=0xFFFF):
    v = [0xC0C1, 0xC181, 0xC301, 0xC601, 0xCC01, 0xD801, 0xF001, 0xA001]
    for b in data:
        crc ^= b
        for j in range(8):
            c = crc & 1
            crc >>= 1
            if c: crc ^= v[j] << (7 - j)
    return crc

out, title, code, a9p, a7p = sys.argv[1:6]
a9 = open(a9p, 'rb').read(); a7 = open(a7p, 'rb').read()
def pad(b, n): return b + b'\0' * ((-len(b)) % n)
a9 = pad(a9, 4); a7 = pad(a7, 4)
off9 = 0x200
off7 = (off9 + len(a9) + 0x1FF) & ~0x1FF
blob = bytes(((i * 7 + (i >> 8) * 13) & 0xFF) for i in range(0x4000))
end = max(off7 + len(a7), 0x10000 + len(blob))
img = bytearray(end)
h = bytearray(0x200)
h[0:12] = title.encode()[:12].ljust(12, b'\0')
h[12:16] = code.encode()[:4]
h[16:18] = b'01'
struct.pack_into('<IIII', h, 0x20, off9, 0x02000000, 0x02000000, len(a9))
struct.pack_into('<IIII', h, 0x30, off7, 0x037F8000, 0x037F8000, len(a7))
struct.pack_into('<II', h, 0x60, 0x00586000, 0x001808F8)
struct.pack_into('<I', h, 0x80, end)
struct.pack_into('<I', h, 0x84, 0x4000)
struct.pack_into('<H', h, 0x15E, crc16(h[:0x15E]))
img[0:0x200] = h
img[off9:off9 + len(a9)] = a9
img[off7:off7 + len(a7)] = a7
img[0x10000:0x10000 + len(blob)] = blob
open(out, 'wb').write(img)
