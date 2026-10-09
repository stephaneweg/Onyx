// host stand-in of Circle's <circle/armv8mmu.h> for the app runner: the geometry kern/layout.h names (64 KB pages).
#ifndef _circle_armv8mmu_h
#define _circle_armv8mmu_h
#define ARMV8MMU_LEVEL3_PAGE_SIZE	0x10000ULL
#define ARMV8MMU_LEVEL2_BLOCK_SIZE	0x20000000ULL
#define ARMV8MMU_TABLE_ENTRIES		8192
#define ATTRIB_AP_RW_EL1		0
#define ATTRIB_AP_RW_ALL		1
#define ATTRIB_AP_RO_EL1		2
#define ATTRIB_AP_RO_ALL		3
#define ATTRIB_SH_NON_SHAREABLE		0
#define ATTRIB_SH_OUTER_SHAREABLE	2
#define ATTRIB_SH_INNER_SHAREABLE	3
#endif
