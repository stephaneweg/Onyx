//
// mv -- rename/move a file or directory (same volume). Usage: mv <src> <dst>
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[256];
	int len = kapi_get_args (args, sizeof (args));
	char src[128], dst[128];
	int i = 0, p;
	while (i < len && args[i] == ' ') i++;
	p = 0; while (i < len && args[i] != ' ' && p < (int) sizeof src - 1) src[p++] = args[i++]; src[p] = '\0';
	while (i < len && args[i] == ' ') i++;
	p = 0; while (i < len && args[i] != ' ' && p < (int) sizeof dst - 1) dst[p++] = args[i++]; dst[p] = '\0';
	if (src[0] == '\0' || dst[0] == '\0') { ax_putln ("usage: mv <src> <dst>"); return 1; }
	if (kapi_rename (src, dst) != 0) { ax_putln ("mv: failed"); return 1; }
	return 0;
}
