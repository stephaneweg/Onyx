//
// echo -- write the arguments followed by a newline to stdout.
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[256];
	kapi_get_args (args, sizeof (args));
	ax_putln (args);
	return 0;
}
