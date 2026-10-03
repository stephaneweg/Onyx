/*
 * skmallocsize.c -- Skia's sk_malloc_size() made "what was asked" in Web (linked with
 * --wrap=_Z14sk_malloc_sizePvm by build-web.sh). Skia grows its containers to the size the C library
 * says a block really has (malloc_usable_size) and fills them; Web's web process was killed on the Pi
 * by a heap corruption in Skia's glyph painting, gone when Skia keeps to the size it asked for
 * (docs/08-WEBKIT-PORT.md "Step 3"). newlib's answer itself is right (/bin/malloctest: every byte it
 * promises is the block's, on the bench and on the Pi): what goes wrong with the slack in Web is not
 * found yet, so this stays (SKMALLOCSIZE=0 sh build-web.sh links Web without it; HEAPCHECK=2 puts
 * the slack under heapcheck.c's canaries). The slack is only an optimisation.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
 */
#include <stddef.h>

size_t __wrap__Z14sk_malloc_sizePvm (void *addr, size_t size)
{
	(void) addr;
	return size;
}
