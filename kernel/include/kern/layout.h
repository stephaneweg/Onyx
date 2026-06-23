//
// layout.h
//
// Address-space layout and AArch64 page-table attribute constants for the
// multi-process kernel running on top of Circle (Raspberry Pi 4).
//
// All values here are CONSTRAINED by how Circle configures the MMU at boot
// (lib/memory64.cpp, lib/translationtable64.cpp). See ARCHITECTURE.md §3-§4.
// Do not change these without re-checking that file.
//
#ifndef _kern_layout_h
#define _kern_layout_h

#include <circle/armv8mmu.h>		// TARMV8MMU_LEVEL{2,3}_*, ATTRIB_AP_*, ATTRIB_SH_*
#include <circle/translationtable64.h>	// ATTRINDX_NORMAL / _DEVICE / _COHERENT
#include <circle/types.h>

#ifndef GIGABYTE
#define GIGABYTE		0x40000000ULL
#endif

//
// ---- Granule / translation geometry (Circle: 64 KB granule, EL1 stage 1) ----
//
// L2 entry spans 512 MB and points to an L3 table; L3 page is 64 KB.
//
#define KPAGE_SIZE		ARMV8MMU_LEVEL3_PAGE_SIZE	// 0x10000 (64 KB)
#define KPAGE_MASK		(KPAGE_SIZE - 1)
#define L2_BLOCK_SIZE		ARMV8MMU_LEVEL2_BLOCK_SIZE	// 512 MB
#define L3_ENTRIES		ARMV8MMU_TABLE_ENTRIES		// 8192 pages per L3 table

#define KPAGE_ALIGN_DOWN(a)	((uintptr)(a) & ~KPAGE_MASK)
#define KPAGE_ALIGN_UP(a)	(((uintptr)(a) + KPAGE_MASK) & ~KPAGE_MASK)

// L2 index for a virtual address (which 512 MB slot it falls in).
#define L2_INDEX(va)		((unsigned)((u64)(va) / L2_BLOCK_SIZE))
// L3 index within a 512 MB slot (which 64 KB page).
#define L3_INDEX(va)		((unsigned)(((u64)(va) % L2_BLOCK_SIZE) / KPAGE_SIZE))

//
// ---- Virtual-address map (RPi 4: 64 GB TTBR0 window) ----
//
// [0, 4 GB)        kernel identity (RAM, MMIO, PCIe window) -- mapped by Circle,
//                  EL1-only, global, shared into every process table.
// [4 GB, 8 GB)     unmapped guard hole.
// [8 GB, 64 GB)    user space (per process, ASID-tagged, EL0-accessible).
//
#define KERNEL_IDENTITY_END	(4ULL * GIGABYTE)	// top of Circle's identity map

#define USER_VA_BASE		(8ULL  * GIGABYTE)	// 0x2_0000_0000, L2 index 16
#define USER_VA_END		(60ULL * GIGABYTE)	// leave headroom below 64 GB ceiling
#define USER_VA_CEILING		(64ULL * GIGABYTE)	// T0SZ_64GB hard limit on RPi 4

// Default placement for a (non-PIE) user image. PIE images may be placed anywhere
// 64 KB-aligned within [USER_VA_BASE, USER_STACK_TOP).
#define USER_LOAD_BASE		USER_VA_BASE		// link .text here, or build PIE

// User stack: top of a fixed slot, grows down. One slot per process for now.
#define USER_STACK_TOP		(16ULL * GIGABYTE)	// 0x4_0000_0000
#define USER_STACK_SIZE		(1ULL * 0x100000)	// 1 MB initial (64 KB-aligned)

// Where a process's window canvas is mapped (shared with the kernel compositor).
#define USER_WINDOW_CANVAS	(12ULL * GIGABYTE)	// 0x3_0000_0000

// Where the shared desktop-wallpaper buffer is mapped for a wallpaper-writer app
// (e.g. voronoy.app). Sits in the gap between the window canvas (12 GB) and the
// kapi table (14 GB). The frames are kernel-owned, so the wallpaper persists after
// the writer app exits.
#define USER_WALLPAPER_CANVAS	(13ULL * GIGABYTE)	// 0x3_4000_0000

#define IS_USER_VA(va) \
	((u64)(va) >= USER_VA_BASE && (u64)(va) < USER_VA_END)

//
// ---- ASID allocation (8-bit: TCR_EL1.AS unset by Circle) ----
//
#define ASID_BITS		8
#define ASID_KERNEL		0			// kernel/global pages (nG=0)
#define ASID_USER_FIRST		1
#define ASID_USER_LAST		255
#define ASID_COUNT		(1u << ASID_BITS)

// TTBR0_EL1 value = phys(L2 table base) | (asid << 48).
#define TTBR0_ASID_SHIFT	48
#define MAKE_TTBR0(phys, asid)	(((u64)(phys)) | ((u64)(asid) << TTBR0_ASID_SHIFT))

//
// ---- Page-descriptor attribute presets (L3 page descriptor field values) ----
//
// These are the per-field values; mm code assembles them into a
// TARMV8MMU_LEVEL3_PAGE_DESCRIPTOR. AP / SH / ATTRINDX come from the Circle
// headers so they always match the MAIR_EL1 Circle programmed.
//
// Kernel identity pages are produced by Circle (AP=RW_EL1, nG=0); we only ever
// share/copy those L2 descriptors, never rebuild them.
//
struct TKPageAttr
{
	unsigned AttrIndx;	// ATTRINDX_NORMAL / _DEVICE / _COHERENT
	unsigned AP;		// ATTRIB_AP_RW_ALL / _RO_ALL / _RW_EL1 / _RO_EL1
	unsigned SH;		// ATTRIB_SH_INNER_SHAREABLE / _OUTER_SHAREABLE / _NON
	unsigned nG;		// 1 = ASID-tagged (user), 0 = global (kernel)
	unsigned PXN;		// 1 = no execute at EL1
	unsigned UXN;		// 1 = no execute at EL0
};

// User read-only code: EL0 can read+execute, nobody writes, never exec at EL1.
#define KPAGE_ATTR_USER_CODE \
	{ ATTRINDX_NORMAL, ATTRIB_AP_RO_ALL,  ATTRIB_SH_INNER_SHAREABLE, 1, 1, 0 }

// User read/write data and stack: EL0 RW, no execute at either level.
#define KPAGE_ATTR_USER_DATA \
	{ ATTRINDX_NORMAL, ATTRIB_AP_RW_ALL,  ATTRIB_SH_INNER_SHAREABLE, 1, 1, 1 }

// Convenience: writable+executable user page (loader scratch before remapping
// code RO+X). Avoid leaving pages in this state once a process runs.
#define KPAGE_ATTR_USER_RWX \
	{ ATTRINDX_NORMAL, ATTRIB_AP_RW_ALL,  ATTRIB_SH_INNER_SHAREABLE, 1, 1, 0 }

//
// ---- Option C: apps run in EL1 (privileged) with per-process page tables ----
//
// Apps are NOT isolated from the kernel (they can call kernel code directly), but
// each has its own ASID-tagged address space, so apps are isolated from EACH OTHER.
// These pages are therefore EL1-accessible (AP=*_EL1), executable at EL1 for code
// (PXN=0), and never executable at EL0 (UXN=1). nG=1 keeps them ASID-tagged.
//
// App code: EL1 read+execute, no write.
#define KPAGE_ATTR_APP_CODE \
	{ ATTRINDX_NORMAL, ATTRIB_AP_RO_EL1,  ATTRIB_SH_INNER_SHAREABLE, 1, 0, 1 }

// App data / stack / window canvas: EL1 read/write, never executable.
#define KPAGE_ATTR_APP_DATA \
	{ ATTRINDX_NORMAL, ATTRIB_AP_RW_EL1,  ATTRIB_SH_INNER_SHAREABLE, 1, 1, 1 }

// App read-only data (the shared kapi ABI table): EL1 read-only, never executable.
#define KPAGE_ATTR_APP_RODATA \
	{ ATTRINDX_NORMAL, ATTRIB_AP_RO_EL1,  ATTRIB_SH_INNER_SHAREABLE, 1, 1, 1 }

#endif // _kern_layout_h
