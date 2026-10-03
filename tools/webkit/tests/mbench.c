/*
 * mbench.c -- what an allocation costs a POSIX program on Onyx, as its heap grows: newlib's malloc
 * (small blocks kept, then freed), sbrk alone, the first touch of fresh pages, a large block. A
 * line a step, with the heap's size: a cost that grows with the heap is the kernel's (sbrk, the
 * page-in), not malloc's.
 *
 *   aarch64-onyx-elf-gcc -specs=<sysroot>/lib/onyx.specs -O2 mbench.c -o mbench   (SD:/bin/mbench)
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

#define N 400000
static void *blocks[N];

int main(void)
{
	double t0;
	int i, round;
	char *base = (char *) sbrk(0);

	/* small blocks, kept: 8 steps of 50 000 */
	for (round = 0; round < 8; round++) {
		t0 = now_ms();
		for (i = round * 50000; i < (round + 1) * 50000; i++) {
			blocks[i] = malloc(24 + (i & 31));
			if (blocks[i] == NULL) { printf("malloc failed at %d\n", i); return 1; }
			*(char *) blocks[i] = 1;
		}
		printf("malloc 50000 small blocks: %.1f ms (%.2f us each), heap %ld KB\n", now_ms() - t0, (now_ms() - t0) * 1000 / 50000,
			(long) ((char *) sbrk(0) - base) / 1024);
	}
	t0 = now_ms();
	for (i = 0; i < N; i += 2) free(blocks[i]);
	printf("free every other one (200000): %.1f ms\n", now_ms() - t0);
	t0 = now_ms();
	for (i = 0; i < N; i += 2) blocks[i] = malloc(24 + (i & 31));
	printf("malloc them again (from the free lists): %.1f ms\n", now_ms() - t0);
	t0 = now_ms();
	for (i = 0; i < N; i++) free(blocks[i]);
	printf("free all (400000): %.1f ms, heap %ld KB\n", now_ms() - t0, (long) ((char *) sbrk(0) - base) / 1024);

	/* sbrk alone: 4 KB at a time, not touched */
	for (round = 0; round < 4; round++) {
		t0 = now_ms();
		for (i = 0; i < 1000; i++) sbrk(4096);
		printf("1000 sbrk (4096), untouched: %.1f ms (%.1f us each), heap %ld KB\n", now_ms() - t0, (now_ms() - t0), (long) ((char *) sbrk(0) - base) / 1024);
	}
	/* the first touch of fresh pages */
	{
		char *p = (char *) sbrk(16 << 20);
		for (round = 0; round < 4; round++) {
			t0 = now_ms();
			for (i = 0; i < 1024; i++) p[(round * 1024 + i) * 4096] = 1;
			printf("first touch of 1024 pages: %.1f ms (%.1f us a page)\n", now_ms() - t0, (now_ms() - t0) * 1000 / 1024);
		}
		t0 = now_ms();
		memset(p, 2, 16 << 20);
		printf("memset 16 MB already there: %.1f ms\n", now_ms() - t0);
	}
	/* a large block, as a table grown: malloc, touched, freed */
	for (round = 0; round < 3; round++) {
		char *p;
		t0 = now_ms();
		p = (char *) malloc(8 << 20);
		memset(p, 1, 8 << 20);
		free(p);
		printf("malloc 8 MB + memset + free: %.1f ms\n", now_ms() - t0);
	}
	return 0;
}
