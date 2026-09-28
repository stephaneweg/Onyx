// (host stub) A spin lock that catches a second Acquire by the same holder: on the Pi that is
// a dead lock (Circle's CSpinLock is not recursive).
#ifndef _circle_spinlock_h
#define _circle_spinlock_h
#include <circle/types.h>
#include <stdio.h>
#include <stdlib.h>
class CSpinLock
{
public:
	CSpinLock (unsigned = 0) : m_bHeld (false) {}
	void Acquire (void) { if (m_bHeld) { fprintf (stderr, "wmtest: DEAD LOCK -- CSpinLock acquired twice\n"); abort (); } m_bHeld = true; }
	void Release (void) { if (!m_bHeld) { fprintf (stderr, "wmtest: CSpinLock released unheld\n"); abort (); } m_bHeld = false; }
	bool m_bHeld;
};
#endif
