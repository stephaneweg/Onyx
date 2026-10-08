//
// sim_mem.cpp -- the kernel's memory calls for a graphics server built for the PC (server_sim.cpp): the C library's.
// Strong definitions, over the weak ones every source that includes appkit/appkit.h has (those read the kernel's
// table, whose entries the desktop simulator leaves unset). In a file of its own: no appkit.h here.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <string.h>

extern "C" void *kapi_memset (void *d, int c, unsigned long n)			{ return memset (d, c, n); }
extern "C" void *kapi_memcpy (void *d, const void *s, unsigned long n)		{ return memcpy (d, s, n); }
extern "C" void *kapi_memmove (void *d, const void *s, unsigned long n)	{ return memmove (d, s, n); }
