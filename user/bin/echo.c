//
// echo -- write the arguments, separated by a blank, followed by a newline to stdout.
//   usage: echo [-n] [text ...]          -n: no newline at the end
// The words are those the shell split (quotes removed): echo "a  b" c prints  a  b c .
//
#include "appkit/appkit.h"
#include "applib.h"

int main (void)
{
	static char blk[4096 + 512];
	int n = kapi_get_argv (blk, sizeof blk - 2);
	if (n <= 0)						// (a kernel before v75: the line as it is)
	{
		kapi_get_args (blk, sizeof blk);
		ax_putln (blk);
		return 0;
	}
	if (n > (int) sizeof blk - 2) n = (int) sizeof blk - 2;
	blk[n] = blk[n + 1] = '\0';
	const char *p = blk + ax_strlen (blk) + 1;		// (after argv[0], the program's path)
	int nl = 1, first = 1;
	if (ax_streq (p, "-n")) { nl = 0; p += 3; }
	for (; *p; p += ax_strlen (p) + 1)
	{
		if (!first) kapi_stdout_write (" ", 1);
		ax_puts (p);
		first = 0;
	}
	if (nl) kapi_stdout_write ("\n", 1);
	return 0;
}
