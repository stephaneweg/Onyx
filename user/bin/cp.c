//
// cp -- copy a file. Usage: cp <src> <dst>. Streams through file streams so any
// size works.
//
#include "kapi.h"
#include "applib.h"

static int next_tok (const char *s, int i, int len, char *dst, int cap)
{
	while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
	int p = 0;
	while (i < len && s[i] != ' ' && s[i] != '\t' && p < cap - 1) dst[p++] = s[i++];
	dst[p] = '\0';
	return i;
}

int main (void)
{
	char args[256];
	int len = kapi_get_args (args, sizeof (args));
	char src[128], dst[128];
	int i = next_tok (args, 0, len, src, sizeof src);
	next_tok (args, i, len, dst, sizeof dst);
	if (src[0] == '\0' || dst[0] == '\0') { ax_putln ("usage: cp <src> <dst>"); return 1; }

	void *in = kapi_file_in (src);
	if (in == 0) { ax_puts ("cp: cannot open "); ax_putln (src); return 1; }
	void *out = kapi_file_out (dst, 0);
	if (out == 0) { kapi_stream_close (in); ax_puts ("cp: cannot create "); ax_putln (dst); return 1; }

	char buf[512]; int n;
	while ((n = kapi_stream_read (in, buf, sizeof buf)) > 0) kapi_stream_write (out, buf, (unsigned) n);
	kapi_stream_close (in);
	kapi_stream_close (out);
	return 0;
}
