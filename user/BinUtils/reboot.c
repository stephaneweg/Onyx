//
// reboot -- restart Onyx from the command line: unmount the SD card (every pending write
// flushed), then restart the Raspberry Pi (= shutdown -r).
//
#include "appkit/appkit.h"
#include "applib.h"

int main (void)
{
	ax_putln ("Restarting...");
	kapi_msleep (200);				// (the line out to the terminal / telnet)
	kapi_shutdown (SHUTDOWN_RESTART);		// does not return
	return 0;
}
