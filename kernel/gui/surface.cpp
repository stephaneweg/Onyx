//
// surface.cpp -- CSurface / CSurfaceManager (see surface.h).
//
#include <kern/gui/surface.h>
#include <kern/gui/window.h>		// g_nScreenWidth / g_nScreenHeight (caps)
#include <kern/layout.h>		// KPAGE_SIZE / KPAGE_MASK
#include <circle/util.h>		// memset
#include <circle/new.h>

CSurface::CSurface (int nId, int nW, int nH, unsigned nOwnerPid)
:	m_nId (nId), m_nW (nW), m_nH (nH), m_nOwnerPid (nOwnerPid),
	m_pRaw (0), m_ulPhys (0), m_nPages (0)
{
	// Same page-aligned, contiguous allocation as a window canvas: over-allocate from
	// the identity-mapped heap and align the start up to 64 KB so PA == the aligned VA
	// and the region can be mapped into a process address space.
	unsigned nBytes = (unsigned) (nW * nH) * sizeof (u32);
	m_nPages = (nBytes + KPAGE_MASK) / KPAGE_SIZE;
	if (m_nPages == 0)
	{
		m_nPages = 1;
	}
	m_pRaw = new u8[m_nPages * KPAGE_SIZE + KPAGE_SIZE];
	if (m_pRaw == 0)
	{
		return;
	}
	uintptr ulAligned = ((uintptr) m_pRaw + KPAGE_MASK) & ~((uintptr) KPAGE_MASK);
	m_ulPhys = ulAligned;			// identity region: PA == kernel VA
	memset ((void *) ulAligned, 0, m_nPages * KPAGE_SIZE);
}

CSurface::~CSurface (void)
{
	if (m_pRaw != 0)
	{
		delete [] (u8 *) m_pRaw;
		m_pRaw = 0;
	}
}

CSurfaceManager *CSurfaceManager::s_pThis = 0;

CSurfaceManager::CSurfaceManager (void)
:	m_nNextId (1)
{
	for (int i = 0; i < MAX_SURFACES; i++)
	{
		m_pSurfaces[i] = 0;
	}
	s_pThis = this;
}

int CSurfaceManager::Create (int nW, int nH, unsigned nOwnerPid)
{
	if (nW <= 0 || nH <= 0)
	{
		return 0;
	}
	if (g_nScreenWidth  > 0 && nW > g_nScreenWidth)  nW = g_nScreenWidth;
	if (g_nScreenHeight > 0 && nH > g_nScreenHeight) nH = g_nScreenHeight;

	m_Lock.Acquire ();
	int nSlot = -1;
	for (int i = 0; i < MAX_SURFACES; i++)
	{
		if (m_pSurfaces[i] == 0) { nSlot = i; break; }
	}
	if (nSlot < 0)
	{
		m_Lock.Release ();
		return 0;
	}
	int nId = m_nNextId++;
	m_Lock.Release ();

	// Allocate outside the lock (new[] may be slow); then publish under the lock.
	CSurface *pS = new CSurface (nId, nW, nH, nOwnerPid);
	if (pS == 0 || !pS->IsValid ())
	{
		delete pS;
		return 0;
	}
	m_Lock.Acquire ();
	m_pSurfaces[nSlot] = pS;
	m_Lock.Release ();
	return nId;
}

CSurface *CSurfaceManager::Find (int nId)
{
	if (nId <= 0)
	{
		return 0;
	}
	m_Lock.Acquire ();
	CSurface *pFound = 0;
	for (int i = 0; i < MAX_SURFACES; i++)
	{
		if (m_pSurfaces[i] != 0 && m_pSurfaces[i]->Id () == nId)
		{
			pFound = m_pSurfaces[i];
			break;
		}
	}
	m_Lock.Release ();
	return pFound;
}

void CSurfaceManager::Destroy (int nId)
{
	m_Lock.Acquire ();
	for (int i = 0; i < MAX_SURFACES; i++)
	{
		if (m_pSurfaces[i] != 0 && m_pSurfaces[i]->Id () == nId)
		{
			CSurface *pS = m_pSurfaces[i];
			m_pSurfaces[i] = 0;
			m_Lock.Release ();
			delete pS;
			return;
		}
	}
	m_Lock.Release ();
}

void CSurfaceManager::DestroyByOwner (unsigned nPid)
{
	for (;;)
	{
		m_Lock.Acquire ();
		CSurface *pVictim = 0;
		for (int i = 0; i < MAX_SURFACES; i++)
		{
			if (m_pSurfaces[i] != 0 && m_pSurfaces[i]->OwnerPid () == nPid)
			{
				pVictim = m_pSurfaces[i];
				m_pSurfaces[i] = 0;
				break;
			}
		}
		m_Lock.Release ();
		if (pVictim == 0) break;
		delete pVictim;
	}
}
