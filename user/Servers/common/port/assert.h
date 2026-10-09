// (Elegant's assert: a failed one stops the server -- a fault the kernel reports -- instead of going on)
#ifndef _elegant_assert_h
#define _elegant_assert_h
#define assert(e)	((e) ? (void) 0 : __builtin_trap ())
#endif
