//
// wc -- count lines, words and bytes of stdin. Output: "<lines> <words> <bytes>".
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	int lines = 0, words = 0, bytes = 0, inword = 0;
	char buf[256]; int n;
	while ((n = kapi_stdin_read (buf, sizeof (buf))) > 0)
	{
		for (int i = 0; i < n; i++)
		{
			char c = buf[i];
			bytes++;
			if (c == '\n') lines++;
			if (c == ' ' || c == '\t' || c == '\n') inword = 0;
			else if (!inword) { inword = 1; words++; }
		}
	}

	char out[48]; int p = 0;
	p += ax_itoa (lines, out + p); out[p++] = ' ';
	p += ax_itoa (words, out + p); out[p++] = ' ';
	p += ax_itoa (bytes, out + p); out[p++] = '\n';
	kapi_stdout_write (out, (unsigned) p);
	return 0;
}
