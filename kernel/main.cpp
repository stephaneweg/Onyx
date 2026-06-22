//
// main.cpp
//
// Entry point. Circle's startup64.S -> sysinit() has already switched us to EL1,
// enabled the MMU (CMemorySystem), initialized the GIC (CInterruptSystem) and run
// the static C++ constructors. sysinit() then calls main() (MAINPROC). Everything
// from here on is ours.
//
#include "kernel.h"
#include <circle/startup.h>

int main (void)
{
	CKernel Kernel;
	if (!Kernel.Initialize ())
	{
		halt ();
		return EXIT_HALT;
	}

	TShutdownMode ShutdownMode = Kernel.Run ();

	switch (ShutdownMode)
	{
	case ShutdownReboot:
		reboot ();
		return EXIT_REBOOT;

	case ShutdownHalt:
	default:
		halt ();
		return EXIT_HALT;
	}
}
