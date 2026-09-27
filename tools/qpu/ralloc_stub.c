/* The three ralloc functions Mesa's QPU disassembler uses, on plain malloc (qpuasm only). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
void *rzalloc_size (const void *ctx, size_t n) { (void) ctx; return calloc (1, n); }
void ralloc_free (void *p) { free (p); }
bool ralloc_vasprintf_rewrite_tail (char **str, size_t *start, const char *fmt, va_list args)
{
	va_list c; va_copy (c, args);
	int n = vsnprintf (0, 0, fmt, c); va_end (c);
	*str = realloc (*str, *start + n + 1);
	vsnprintf (*str + *start, n + 1, fmt, args);
	*start += n;
	return true;
}
