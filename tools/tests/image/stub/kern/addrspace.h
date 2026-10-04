// host stub of kern/addrspace.h (tools/tests/run_image_test.sh): a process's address space as a
// map of its pages -- which frame, which rights, owned (freed by the teardown) or not -- and the
// regions noted. Its destructor does what the real one does: the owned frames freed, then the
// program's image released.
#ifndef _kern_addrspace_h
#define _kern_addrspace_h
#include <kern/layout.h>
#include <map>
#include <vector>

struct TImage;
#define AS_LIB_MAX		16

struct TMockPage
{
	u64	 ulFrame;
	unsigned nAP;
	unsigned nUXN;
	bool	 bOwned;
};

struct TMockRegion
{
	u64	 ulStart, ulEnd;
	unsigned nProt, nKind;
};

class CAddressSpace
{
public:
	CAddressSpace (void) : m_nBudget (-1), m_pImage (0), m_nLibs (0), m_nLibReady (0) {}
	~CAddressSpace (void);

	boolean MapPage (uintptr ulVA, uintptr ulPA, const TKPageAttr &Attr, boolean bOwned = FALSE);
	void *MapNewPage (uintptr ulVA, const TKPageAttr &Attr);

	TImage *GetImage (void)			{ return m_pImage; }
	void SetImage (TImage *p)		{ m_pImage = p; }

	// (v83) The shared libraries mapped here (kern/image.h ImageMapLib): one reference each,
	// dropped by the destructor as the program's. Ready: wholly mapped (a mapping that ran out of
	// memory stays recorded -- its pages are there -- and is never handed out).
	int FindLib (const TImage *p) const
	{
		for (unsigned i = 0; i < m_nLibs; i++) if (m_pLib[i] == p) return (int) i;
		return -1;
	}
	boolean LibReady (int n) const		{ return (m_nLibReady >> n) & 1; }
	boolean AddLib (TImage *p)
	{
		if (m_nLibs == AS_LIB_MAX) return FALSE;
		m_pLib[m_nLibs++] = p;
		return TRUE;
	}
	void SetLibReady (const TImage *p)
	{
		int n = FindLib (p);
		if (n >= 0) m_nLibReady |= 1u << n;
	}

	// (the test's side)
	std::map<u64, TMockPage>  m_Pages;
	std::vector<TMockRegion>  m_Regions;
	int			  m_nBudget;	// pages it may still map (-1: no limit)

public:						// (the stub's destructor is the test's)
	TImage *m_pImage;
	TImage *m_pLib[AS_LIB_MAX];
	unsigned m_nLibs, m_nLibReady;
};
#endif
