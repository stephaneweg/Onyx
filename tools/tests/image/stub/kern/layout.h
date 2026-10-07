// host stub of kern/layout.h (tools/tests/run_image_test.sh): the page size, the user range and the
// page attribute presets the loader uses (the values of the real header)
#ifndef _kern_layout_h
#define _kern_layout_h
#include <circle/types.h>
#define KPAGE_SIZE		0x10000ULL
#define KPAGE_MASK		(KPAGE_SIZE - 1)
#define KPAGE_ALIGN_DOWN(a)	((u64) (a) & ~KPAGE_MASK)
#define KPAGE_ALIGN_UP(a)	(((u64) (a) + KPAGE_MASK) & ~KPAGE_MASK)
#define GIGABYTE		0x40000000ULL
#define USER_VA_BASE		(8ULL  * GIGABYTE)
#define USER_VA_END		(60ULL * GIGABYTE)
#define USER_LIB_BASE		(16ULL * GIGABYTE)
#define USER_LIB_END		(32ULL * GIGABYTE)
#define IS_USER_VA(va)		((u64) (va) >= USER_VA_BASE && (u64) (va) < USER_VA_END)

#define ATTRIB_AP_RW_ALL	1
#define ATTRIB_AP_RO_ALL	3
struct TKPageAttr
{
	unsigned AttrIndx, AP, SH, nG, PXN, UXN;
};
#define KPAGE_ATTR_APP_CODE	{ 0, ATTRIB_AP_RO_ALL, 3, 1, 1, 0 }
#define KPAGE_ATTR_APP_DATA	{ 0, ATTRIB_AP_RW_ALL, 3, 1, 1, 1 }
#define KPAGE_ATTR_APP_RODATA	{ 0, ATTRIB_AP_RO_ALL, 3, 1, 1, 1 }
#endif
