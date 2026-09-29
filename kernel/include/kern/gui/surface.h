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
// (passed by the shell over IPC). The frames are owned here. A process that maps a surface
// it does not own becomes one of its users (kapi_surface_map); the frames are freed once the
// owner has let it go (kapi_surface_destroy, or its end) AND every user has ended too -- so
// no process keeps a mapping of freed frames, whichever ends first (a Control Panel applet
// drawing into its host's surface, v65).
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

	// Its users (the processes that mapped it, besides the owner) and the owner's release.
	void AddUser (unsigned nPid);
	boolean DropUser (unsigned nPid);		// TRUE if it was one
	void OwnerGone (void)		{ m_bOwnerGone = TRUE; }
	boolean Unused (void) const;			// the owner gone and no user left

	u64 Phys (void) const		{ return m_ulPhys; }	// 64 KB-aligned start (== kernel VA)
	unsigned Pages (void) const	{ return m_nPages; }	// 64 KB pages spanned
	u32 *Buffer (void) const	{ return (u32 *) m_ulPhys; }

private:
	int		m_nId;
	int		m_nW, m_nH;
	unsigned	m_nOwnerPid;
	enum { MAX_USERS = 4 };
	unsigned	m_Users[MAX_USERS];	// pids that mapped it (0 = a free slot)
	boolean		m_bOwnerGone;
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

	// The owner lets surface nId go: freed now if nobody else maps it, else when its last
	// user ends. No-op if unknown.
	void Destroy (int nId);

	// A process is being torn down: the surfaces it owns are let go, it stops being a user of
	// the others; the ones nobody uses any more are freed.
	void DestroyByOwner (unsigned nPid);

private:
	enum { MAX_SURFACES = 64 };
	CSurface  *m_pSurfaces[MAX_SURFACES];
	int	   m_nNextId;
	CSpinLock  m_Lock;
	static CSurfaceManager *s_pThis;
};

#endif // _kern_gui_surface_h
