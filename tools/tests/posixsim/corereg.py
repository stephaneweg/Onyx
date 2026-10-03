#!/usr/bin/env python3
# corereg.py -- make a posixsim program read the core it runs on where the bench can set it.
#
#   python3 tools/tests/posixsim/corereg.py <program>      (run.sh does it, in place)
#
# kapi__core () (user/kapi.h) reads TPIDRRO_EL0, where Onyx's kernel publishes the core: 0 for the
# threads, 2 or 3 on an app core. Under qemu-user that register is 0 for good, so the code of a
# fake app core (fakekapi.c) took itself for a thread and made kernel calls the Pi faults on.
# Every "mrs xN, tpidrro_el0" of the program's .text becomes "mrs xN, tpidr2_el0": a register the
# program itself may write under qemu-user, which fakekapi.c's core_tramp sets to the core.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
import re
import struct
import sys

MRS_TPIDRRO = 0xD53BD060	# mrs x0, tpidrro_el0 (the register in bits 0-4)
MRS_TPIDR2 = 0xD53BD0A0		# mrs x0, tpidr2_el0


def main():
	path = sys.argv[1]
	with open(path, "rb") as f:
		data = bytearray(f.read())
	if data[:4] != b"\x7fELF" or data[4] != 2:
		sys.exit("corereg.py: %s is not a 64-bit ELF file" % path)
	shoff, = struct.unpack_from("<Q", data, 0x28)
	shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
	sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]
	names = sections[shstrndx]
	count = 0
	for name, kind, flags, addr, offset, size, link, info, align, entsize in sections:
		end = data.index(b"\0", names[4] + name)
		if kind != 1 or not (flags & 4) or not data[names[4] + name:end].startswith(b".text"):
			continue				# (PROGBITS, executable, .text*)
		for m in re.finditer(rb"[\x60-\x7f]\xd0\x3b\xd5", bytes(data[offset:offset + size])):
			at = offset + m.start()
			if (at - offset) % 4 != 0:
				continue
			word, = struct.unpack_from("<I", data, at)
			struct.pack_into("<I", data, at, MRS_TPIDR2 | (word & 31))
			count += 1
	with open(path, "wb") as f:
		f.write(data)
	print("corereg.py: %s: %d reads of the core" % (path, count))


main()
