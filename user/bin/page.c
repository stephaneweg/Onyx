//
// page -- copy stdin to stdout. Paging itself is provided by the terminal's
// scrollback (PgUp/PgDn); a real interactive pager needs a tty channel separate
// from the piped data, which we don't have yet. Useful as a pipeline endpoint.
//
#include "kapi.h"

int main (void)
{
	char buf[256]; int n;
	while ((n = kapi_stdin_read (buf, sizeof (buf))) > 0)
	{
		kapi_stdout_write (buf, (unsigned) n);
	}
	return 0;
}
