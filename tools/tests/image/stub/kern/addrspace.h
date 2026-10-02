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
	CAddressSpace (void) : m_nBudget (-1), m_pImage (0) {}
	~CAddressSpace (void);

	boolean MapPage (uintptr ulVA, uintptr ulPA, const TKPageAttr &Attr, boolean bOwned = FALSE);
	void *MapNewPage (uintptr ulVA, const TKPageAttr &Attr);

	TImage *GetImage (void)			{ return m_pImage; }
	void SetImage (TImage *p)		{ m_pImage = p; }

	// (the test's side)
	std::map<u64, TMockPage>  m_Pages;
	std::vector<TMockRegion>  m_Regions;
	int			  m_nBudget;	// pages it may still map (-1: no limit)

private:
	TImage *m_pImage;
};
#endif
