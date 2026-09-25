//
// notifytest -- send a notification from the terminal / telnet (tests notifyd).
//   usage: notifytest [-t <title>] <message...>
//   e.g.   notifytest Hello from the shell
//          notifytest -t Build "Kernel staged"
// The message goes through notify() (notify.h): an IPC message to the "notify"
// service, which is started on demand if needed.
//
#include "kapi.h"
#include "applib.h"
#include "notify.h"

int main (void)
{
	char args[512];
	kapi_get_args (args, sizeof args);

	char title[64] = "Test";
	const char *msg = args;
	while (*msg == ' ') msg++;
	if (msg[0] == '-' && msg[1] == 't' && msg[2] == ' ')		// -t <title>
	{
		msg += 3; while (*msg == ' ') msg++;
		int n = 0;
		if (*msg == '"') { msg++; while (*msg && *msg != '"' && n < 63) title[n++] = *msg++; if (*msg == '"') msg++; }
		else while (*msg && *msg != ' ' && n < 63) title[n++] = *msg++;
		title[n] = '\0';
		while (*msg == ' ') msg++;
	}
	char text[480]; int n = 0;				// strip surrounding quotes
	if (*msg == '"') { msg++; while (*msg && *msg != '"' && n < 479) text[n++] = *msg++; }
	else while (*msg && n < 479) text[n++] = *msg++;
	text[n] = '\0';

	if (text[0] == '\0') { ax_putln ("usage: notifytest [-t <title>] <message>"); return 1; }
	if (!notify (title, text)) { ax_putln ("notifytest: the notification service is not available"); return 1; }
	ax_putln ("notification sent");
	return 0;
}
