//
// shutdown -- end the session from the command line (like the Onyx menu's Shut Down...):
//   shutdown          unmount the SD card (every pending write flushed), then halt: the ACT
//                     LED goes dark, it is safe to switch the Raspberry Pi off;
//   shutdown -r       the same, then restart (= reboot).
// Also over telnet: the connection just drops.
//
#include "appkit/appkit.h"
#include "applib.h"

int main (void)
{
	char args[32];
	kapi_get_args (args, sizeof args);
	int i = 0; while (args[i] == ' ') i++;
	int restart = args[i] == '-' && (args[i + 1] == 'r' || args[i + 1] == 'R');
	if (args[i] && !restart)
	{
		ax_putln ("usage: shutdown [-r]    (-r: restart)");
		return 1;
	}
	ax_putln (restart ? "Restarting..." : "Shutting down: safe to switch off when the green LED is dark.");
	kapi_msleep (200);				// (the line out to the terminal / telnet)
	kapi_shutdown (restart ? SHUTDOWN_RESTART : SHUTDOWN_HALT);	// does not return
	return 0;
}
