//
// usys.h -- userland syscall wrappers (EL0). ABI: x8 = number, x0..x5 = args,
// x0 = result. Must stay in sync with kernel/include/kern/syscall.h.
//
#ifndef _usys_h
#define _usys_h

#define SYS_write		1
#define SYS_yield		2
#define SYS_exit		3
#define SYS_getpid		4
#define SYS_create_window	5
#define SYS_present		6
#define SYS_get_ticks		7
#define SYS_msleep		8

static inline long __syscall (long nNum, long a0, long a1, long a2)
{
	register long x8 asm ("x8") = nNum;
	register long x0 asm ("x0") = a0;
	register long x1 asm ("x1") = a1;
	register long x2 asm ("x2") = a2;
	asm volatile ("svc #0" : "+r" (x0) : "r" (x8), "r" (x1), "r" (x2) : "memory");
	return x0;
}

static inline long write (int fd, const void *buf, unsigned long len)
{
	return __syscall (SYS_write, fd, (long) buf, (long) len);
}

static inline void yield (void)			{ __syscall (SYS_yield, 0, 0, 0); }

static inline void exit (int status)
{
	__syscall (SYS_exit, status, 0, 0);
	for (;;) { }
}

// Create a window with a w*h client area; returns the canvas pixel buffer
// (u32 0x00RRGGBB), mapped into this process at a fixed address.
static inline unsigned *create_window (int w, int h, const char *title)
{
	return (unsigned *) __syscall (SYS_create_window, w, h, (long) title);
}

static inline void present (void)		{ __syscall (SYS_present, 0, 0, 0); }
static inline unsigned get_ticks (void)		{ return (unsigned) __syscall (SYS_get_ticks, 0, 0, 0); }
static inline void msleep (unsigned ms)		{ __syscall (SYS_msleep, (long) ms, 0, 0); }

#endif
