//
// heaptest -- exercises the user allocator (umm.h) over kapi_sbrk: many alloc/write/
// verify/free across size classes, free-list reuse, a large block + realloc. Prints
// PASS/FAIL and how much the system app-memory grew (the heap pages sbrk mapped).
//
#include "kapi.h"
#include "applib.h"
#include "umm.h"

int main (void)
{
	unsigned long app0 = 0; kapi_meminfo (0, 0, &app0, 0);
	int fail = 0;
	void *p[256];

	// 1) small allocations across classes: write a per-block pattern, verify, free.
	for (int i = 0; i < 256; i++)
	{
		unsigned long sz = (unsigned long) (i % 64) + 1;
		p[i] = umm_malloc (sz);
		if (!p[i]) { fail++; continue; }
		unsigned char *b = p[i];
		for (unsigned long k = 0; k < sz; k++) b[k] = (unsigned char) (i + k);
	}
	for (int i = 0; i < 256; i++)
	{
		if (!p[i]) continue;
		unsigned long sz = (unsigned long) (i % 64) + 1;
		unsigned char *b = p[i];
		for (unsigned long k = 0; k < sz; k++)
			if (b[k] != (unsigned char) (i + k)) { fail++; break; }
	}
	for (int i = 0; i < 256; i++) umm_free (p[i]);

	// 2) reuse: allocate the same sizes again (should pop the free lists), then free.
	for (int i = 0; i < 256; i++) { p[i] = umm_malloc ((unsigned long) (i % 64) + 1); if (!p[i]) fail++; }
	for (int i = 0; i < 256; i++) umm_free (p[i]);

	// 3) a large block + realloc growing it, preserving contents.
	char *big = umm_malloc (20000);
	if (!big) fail++;
	else
	{
		for (int k = 0; k < 20000; k++) big[k] = (char) (k & 0x7f);
		big = umm_realloc (big, 40000);
		if (!big) fail++;
		else
		{
			for (int k = 0; k < 20000; k++)
				if (big[k] != (char) (k & 0x7f)) { fail++; break; }
			umm_free (big);
		}
	}

	unsigned long app1 = 0; kapi_meminfo (0, 0, &app1, 0);

	ax_puts ("heaptest: "); ax_putln (fail ? "FAIL" : "PASS");
	char nb[16]; ax_itoa ((int) (app1 - app0), nb);
	ax_puts ("system app-memory grew by "); ax_puts (nb); ax_putln (" KB (heap pages)");
	return fail ? 1 : 0;
}
