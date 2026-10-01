// Host stub (tools/tests/circlenet): one thread, no lock needed.
#ifndef _circle_spinlock_h
#define _circle_spinlock_h
#include <circle/types.h>
#define TASK_LEVEL 0
#define IRQ_LEVEL 1
#define FIQ_LEVEL 2
class CSpinLock
{
public:
	CSpinLock (unsigned nTargetLevel = IRQ_LEVEL) {}
	void Acquire (void) {}
	void Release (void) {}
};
#endif
