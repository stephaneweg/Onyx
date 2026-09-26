// host stub of <circle/types.h> for the FatFs + diskio.cpp test (tools/tests/run_fs_test.sh)
#ifndef _circle_types_h
#define _circle_types_h
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef uintptr_t uintptr; typedef int boolean;
#define TRUE 1
#define FALSE 0
#endif
