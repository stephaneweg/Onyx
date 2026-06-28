//
// surface.h
//
// A shared pixel surface for the activity-shell compositor model. A CSurface owns a
// page-aligned, physically-contiguous 0x00RRGGBB buffer (PA == kernel VA, identity
// region) that can be mapped into one or more user address spaces: the shell allocates
// it (kapi_surface_create), the owner app draws into it (kapi_surface_map in the app),
// the shell composites it (kapi_surface_map in the shell) -- same frames, two VAs.
//
// Surfaces are addressed by a small global id so the id can travel to another process
// (passed by the shell over IPC). The frames are owned here and freed when the surface
// is destroyed (kapi_surface_destroy) or when its owner process dies. Mappers get plain
// page-table entries that go stale if the surface is freed under them -- the shell, as
// the single owner/lifetime-coordinator, keeps that from happening (phase-1 model; a
// refcount can harden it later).
//
#ifndef _kern_gui_surface_h
#define _kern_gui_surface_h

#include <circle/spinlock.h>
#include <circle/types.h>

class CSurface
{
public:
	CSurface (int nId, int nW, int nH, unsigned nOwnerPid);
	~CSurface (void);

	boolean IsValid (void) const	{ return m_pRaw != 0; }
	int Id (void) const		{ return m_nId; }
	int Width (void) const		{ return m_nW; }
	int Height (void) const		{ return m_nH; }
	unsigned OwnerPid (void) const	{ return m_nOwnerPid; }

	u64 Phys (void) const		{ return m_ulPhys; }	// 64 KB-aligned start (== kernel VA)
	unsigned Pages (void) const	{ return m_nPages; }	// 64 KB pages spanned
	u32 *Buffer (void) const	{ return (u32 *) m_ulPhys; }

private:
	int		m_nId;
	int		m_nW, m_nH;
	unsigned	m_nOwnerPid;
	void	       *m_pRaw;		// over-allocated block (freed on destroy)
	u64		m_ulPhys;	// 64 KB-aligned start (PA == kernel VA, identity region)
	unsigned	m_nPages;	// 64 KB pages spanned
};

class CSurfaceManager
{
public:
	CSurfaceManager (void);

	static CSurfaceManager *Get (void)	{ return s_pThis; }

	// Create a w x h surface owned by nOwnerPid (capped to the screen). Returns its
	// id (> 0) or 0 on failure.
	int Create (int nW, int nH, unsigned nOwnerPid);

	// Resolve an id to its surface, or 0. (Snapshot; the pointer stays valid until the
	// surface is destroyed -- only the owning shell does that, single-threaded.)
	CSurface *Find (int nId);

	// Destroy a surface by id (frees its frames). No-op if unknown.
	void Destroy (int nId);

	// Free every surface owned by a process that is being torn down.
	void DestroyByOwner (unsigned nPid);

private:
	enum { MAX_SURFACES = 64 };
	CSurface  *m_pSurfaces[MAX_SURFACES];
	int	   m_nNextId;
	CSpinLock  m_Lock;
	static CSurfaceManager *s_pThis;
};

#endif // _kern_gui_surface_h
