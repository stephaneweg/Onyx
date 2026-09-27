#!/usr/bin/env python3
# elf2dol.py -- an ELF32 big-endian PowerPC executable -> a GameCube .dol (the program format
# the IPL / the emulator load): its loadable segments as text (executable) or data sections,
# the bss, the entry. Usage: elf2dol.py in.elf out.dol
import struct, sys
e = open(sys.argv[1], 'rb').read()
entry = struct.unpack('>I', e[24:28])[0]
phoff, = struct.unpack('>I', e[28:32]); phsz, phn = struct.unpack('>HH', e[42:46])
texts, datas, bss_lo, bss_hi = [], [], None, None
for i in range(phn):
    t, off, va, pa, fsz, msz, flags, al = struct.unpack('>8I', e[phoff + i * phsz: phoff + i * phsz + 32])
    if t != 1: continue
    if fsz: (texts if flags & 1 else datas).append((va, e[off:off + fsz]))
    if msz > fsz:
        lo, hi = va + fsz, va + msz
        bss_lo = lo if bss_lo is None else min(bss_lo, lo); bss_hi = hi if bss_hi is None else max(bss_hi, hi)
assert len(texts) <= 7 and len(datas) <= 11
hdr = bytearray(0x100); body = bytearray(); pos = 0x100
def put(slot, va, data):
    global pos
    struct.pack_into('>I', hdr, slot * 4, pos); struct.pack_into('>I', hdr, 0x48 + slot * 4, va)
    struct.pack_into('>I', hdr, 0x90 + slot * 4, len(data))
    pad = (-len(data)) % 32
    body.extend(data + b'\0' * pad); pos += len(data) + pad
for i, (va, d) in enumerate(texts): put(i, va, d)
for i, (va, d) in enumerate(datas): put(7 + i, va, d)
if bss_lo is not None: struct.pack_into('>II', hdr, 0xD8, bss_lo, bss_hi - bss_lo)
struct.pack_into('>I', hdr, 0xE0, entry)
open(sys.argv[2], 'wb').write(bytes(hdr) + bytes(body))
