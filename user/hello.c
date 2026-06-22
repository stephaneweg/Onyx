//
// hello.c -- minimal EL0 ELF program: validates the loader + EL0 round-trip.
//
#include "usys.h"

int main (void)
{
	const char msg[] = "hello from an ELF process running in EL0!\n";
	write (1, msg, sizeof (msg) - 1);
	return 0;
}
