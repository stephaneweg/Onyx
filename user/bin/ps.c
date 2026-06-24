//
// ps -- list running processes. The kernel returns one line per task as
// "<pid> <a|k> <state> <pages> <name>" (pid 0 = kernel task); we print it in columns.
// State: R ready, S sleeping, B blocked, N new. Kind: a app (killable), k kernel.
// PAGES is 64 KB physical frames the app owns; MEM is that in KB.
//
#include "kapi.h"
#include "applib.h"

static int parse_uint (const char *s, int *pi)
{
	int v = 0, i = *pi;
	while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
	*pi = i;
	return v;
}

// Append s right-justified in a width-w field to line[*p].
static void pad (char *line, int *p, const char *s, int w)
{
	int n = 0; while (s[n]) n++;
	for (int k = 0; k < w - n; k++) line[(*p)++] = ' ';
	for (int k = 0; s[k]; k++) line[(*p)++] = s[k];
}

int main (void)
{
	static char buf[4096];
	kapi_list_procs (buf, sizeof (buf));

	ax_putln (" PID  K  S  PAGES   MEM  NAME");

	int i = 0;
	while (buf[i] != '\0')
	{
		int pid = parse_uint (buf, &i);
		while (buf[i] == ' ') i++;
		char kind = buf[i] ? buf[i++] : '?';
		while (buf[i] == ' ') i++;
		char st = buf[i] ? buf[i++] : '?';
		while (buf[i] == ' ') i++;
		int pages = parse_uint (buf, &i);		// owned 64 KB frames
		while (buf[i] == ' ') i++;

		char name[64]; int n = 0;
		while (buf[i] != '\0' && buf[i] != '\n' && n < 63) name[n++] = buf[i++];
		name[n] = '\0';
		if (buf[i] == '\n') i++;

		char num[12]; char line[128]; int p = 0;
		ax_itoa (pid, num);   pad (line, &p, num, 4);
		line[p++] = ' '; line[p++] = ' '; line[p++] = kind;
		line[p++] = ' '; line[p++] = ' '; line[p++] = st;
		ax_itoa (pages, num);          pad (line, &p, num, 7);
		ax_itoa (pages * 64, num);     pad (line, &p, num, 6);   // KB (64 KB/page)
		line[p++] = ' '; line[p++] = ' ';
		for (int s = 0; name[s]; s++) line[p++] = name[s];
		line[p] = '\0';
		ax_putln (line);
	}
	return 0;
}
