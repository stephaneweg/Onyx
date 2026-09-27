/* cputest_main.c -- the reference side: runs cputest.c's run_tests under qemu-ppc and writes
 * the words (big-endian, as the Gekko's memory) to stdout. */
#include <stdio.h>
typedef unsigned int u32;
int run_tests (u32 *out);
static u32 buf[1 << 18];
int main (void)
{
	int n = run_tests (buf);
	fwrite (buf, 4, (size_t) n, stdout);
	return 0;
}
