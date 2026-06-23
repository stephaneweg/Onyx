//
// grep -- print stdin lines that contain the pattern. Usage: grep <pattern>
// (case-sensitive substring). A /bin filter, composable via pipes.
//
#include "kapi.h"
#include "applib.h"

static int contains (const char *hay, const char *needle)
{
	if (needle[0] == '\0') return 1;
	for (int i = 0; hay[i] != '\0'; i++)
	{
		int j = 0;
		while (needle[j] != '\0' && hay[i + j] == needle[j]) j++;
		if (needle[j] == '\0') return 1;
	}
	return 0;
}

int main (void)
{
	char pat[128];
	kapi_get_args (pat, sizeof (pat));
	// pattern = first token only
	int i = 0; while (pat[i] == ' ') i++;
	int s = i; while (pat[i] && pat[i] != ' ') i++; pat[i] = '\0';
	const char *needle = &pat[s];

	char line[512]; int ll = 0;
	char buf[256]; int n;
	while ((n = kapi_stdin_read (buf, sizeof (buf))) > 0)
	{
		for (int k = 0; k < n; k++)
		{
			char c = buf[k];
			if (c == '\n')
			{
				line[ll] = '\0';
				if (contains (line, needle)) ax_putln (line);
				ll = 0;
			}
			else if (ll < (int) sizeof (line) - 1)
			{
				line[ll++] = c;
			}
		}
	}
	if (ll > 0) { line[ll] = '\0'; if (contains (line, needle)) ax_putln (line); }
	return 0;
}
