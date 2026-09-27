//
// hangtest -- freeze core 0 on purpose, to check the crash report: IRQs masked, then an
// endless loop (apps run at EL1, so this stops the whole scheduler). Core 1 notices after
// 10 s, writes SD:/etc/crashdump.txt and restarts the Pi; the next boot keeps the report as
// SD:/etc/lastcrash.txt. `hangtest irq` loops with the IRQs on instead (the samples then
// show the loop). Needs the hang watchdog (cmdline.txt hangreboot, on by default).
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char a[32]; kapi_get_args (a, sizeof a);
	int i = 0; while (a[i] == ' ') i++;
	int bIrq = a[i] == 'i';
	ax_putln (bIrq ? "Freezing core 0 (IRQs on): the Pi restarts in about 10 s..."
		       : "Freezing core 0 (IRQs masked): the Pi restarts in about 10 s...");
	kapi_msleep (300);				// (the line out to the terminal / telnet)
	if (!bIrq) asm volatile ("msr daifset, #3" ::: "memory");
	for (;;) asm volatile ("" ::: "memory");
	return 0;
}
