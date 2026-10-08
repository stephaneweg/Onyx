// (Elegant's stand-in for Circle's spin lock) Nothing to lock: the server's window manager is
// called from one thread only -- the kernel's had an interrupt-time caller and a compositor task.
#ifndef _circle_spinlock_h
#define _circle_spinlock_h
class CSpinLock
{
public:
	constexpr CSpinLock (unsigned = 0) {}
	void Acquire (void) {}
	void Release (void) {}
};
#endif
