/*
 * skmallocsize.c -- Skia's sk_malloc_size() made "what was asked" in Web (linked with
 * --wrap=_Z14sk_malloc_sizePvm by build-web.sh). Skia grows its containers to the size the C library
 * says a block really has (malloc_usable_size) and fills them; newlib's answer is not to be trusted
 * that far on Onyx (a heap corruption in Skia's glyph painting on the Pi, gone when Skia keeps to
 * the size it asked for -- docs/08-WEBKIT-PORT.md). The slack is only an optimisation.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
 */
#include <stddef.h>

size_t __wrap__Z14sk_malloc_sizePvm (void *addr, size_t size)
{
	(void) addr;
	return size;
}
