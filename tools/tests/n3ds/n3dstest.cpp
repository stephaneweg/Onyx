//
// n3dstest.cpp -- the 3DS core (user/Emulators/n3ds) run without a screen: loads a program, runs it frame after
// frame until it ends, prints what it says (svcOutputDebugString) and how it ended. Built for AArch64 with
// Onyx's toolchain and run under qemu-aarch64 (tools/tests/run_n3ds_test.sh): the JIT is the real one.
//   n3dstest <program.elf> [frames]		(600 frames at most by default)
// The exit status is 0 when the program ended by itself without an error of the emulator.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

static void debugOut (void *, const char *text, n3ds::u32 len) { fwrite (text, 1, len, stdout); }

int main (int argc, char **argv)
{
	if (argc < 2) { fprintf (stderr, "usage: n3dstest <program.elf> [frames]\n"); return 2; }
	int frames = argc > 2 ? atoi (argv[2]) : 600;
	FILE *f = fopen (argv[1], "rb");
	if (!f) { fprintf (stderr, "cannot open %s\n", argv[1]); return 2; }
	fseek (f, 0, SEEK_END); long size = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *file = (unsigned char *) malloc ((size_t) size);
	if (!file || fread (file, 1, (size_t) size, f) != (size_t) size) { fprintf (stderr, "cannot read %s\n", argv[1]); return 2; }
	fclose (f);

	n3ds::Machine *m = new n3ds::Machine;
	if (!m->init ()) { fprintf (stderr, "not enough memory for the machine\n"); return 2; }
	m->debugOut = debugOut;
	if (!m->loadElf (file, (n3ds::u32) size)) { fprintf (stderr, "%s\n", m->lastError); return 2; }
	int n = 0;
	while (n < frames && !m->exited) { m->runFrame (); n++; }
	fflush (stdout);
	fprintf (stderr, "%s after %d frames (%llu ticks): %llu system calls, %llu thread switches, %u memory faults\n",
		 m->exited ? "ended" : "still running", n, (unsigned long long) m->now, (unsigned long long) m->svcCount,
		 (unsigned long long) m->switchCount, (unsigned) m->mem.faults);
	if (m->mem.faults) fprintf (stderr, "the last memory fault: %08x\n", (unsigned) m->mem.faultAddr);
	if (m->lastError[0]) fprintf (stderr, "error: %s\n", m->lastError);
	return m->exited && !m->lastError[0] && m->exitCode == 0 ? 0 : 1;
}
