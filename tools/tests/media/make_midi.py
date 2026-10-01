#!/usr/bin/env python3
"""make_midi.py OUT.mid -- a small Standard MIDI File for the tests: a named track, a tempo, two
channels (a piano and a bass) playing a scale."""
import struct, sys
def vlq (n):
	b = [n & 0x7F]; n >>= 7
	while n: b.append ((n & 0x7F) | 0x80); n >>= 7
	return bytes (reversed (b))
ev = vlq (0) + bytes ([0xFF, 0x03, 4]) + b"Test"
ev += vlq (0) + bytes ([0xFF, 0x51, 3, 0x07, 0xA1, 0x20])
ev += vlq (0) + bytes ([0xC0, 0]) + vlq (0) + bytes ([0xC1, 33])
for k in (60, 62, 64, 65, 67, 69, 71, 72):
	ev += vlq (0) + bytes ([0x90, k, 100]) + vlq (0) + bytes ([0x91, k - 24, 90]) + vlq (240) + bytes ([0x80, k, 0]) + vlq (0) + bytes ([0x81, k - 24, 0])
ev += vlq (0) + bytes ([0xFF, 0x2F, 0])
open (sys.argv[1], "wb").write (b"MThd" + struct.pack (">IHHH", 6, 0, 1, 480) + b"MTrk" + struct.pack (">I", len (ev)) + ev)
