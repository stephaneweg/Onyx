/*
 * sys.h -- the 3DS core's test programs: the system calls (sys.S) and a few helpers to print and to check.
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#ifndef N3DS_TEST_SYS_H
#define N3DS_TEST_SYS_H

typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
typedef signed int s32; typedef signed long long s64;
typedef u32 Handle; typedef u32 Result;

#define RES_TIMEOUT		0x09401BFEu
#define RES_INVALID_HANDLE	0xD8E007F7u
#define RES_NOT_OWNER		0xD8E0041Fu
#define MEMOP_FREE		1
#define MEMOP_ALLOC		3
#define MEMOP_PROT		6
#define MEMOP_LINEAR		0x10000
#define MEMPERM_RW		3
#define CUR_THREAD		0xFFFF8000u
#define NS_MS			1000000ll

Result svcControlMemory (u32 *out, u32 addr0, u32 addr1, u32 size, u32 op, u32 perm);
void svcExitProcess (void);
Result svcCreateThread (Handle *out, void (*entry) (void *), u32 arg, u32 *stackTop, s32 priority, s32 processor);
void svcExitThread (void);
void svcSleepThread (s64 ns);
Result svcGetThreadPriority (s32 *out, Handle h);
Result svcSetThreadPriority (Handle h, s32 priority);
Result svcCreateMutex (Handle *out, int locked);
Result svcReleaseMutex (Handle h);
Result svcCreateSemaphore (Handle *out, s32 initial, s32 max);
Result svcReleaseSemaphore (s32 *before, Handle h, s32 count);
Result svcCreateEvent (Handle *out, u32 resetType);
Result svcSignalEvent (Handle h);
Result svcClearEvent (Handle h);
Result svcCreateAddressArbiter (Handle *out);
Result svcArbitrateAddress (Handle arbiter, u32 addr, u32 type, s32 value, s64 ns);
Result svcCloseHandle (Handle h);
Result svcWaitSynchronization (Handle h, s64 ns);
Result svcWaitSynchronizationN (s32 *out, Handle *handles, s32 count, int all, s64 ns);
Result svcDuplicateHandle (Handle *out, Handle h);
u64 svcGetSystemTick (void);
Result svcGetThreadId (u32 *out, Handle h);
void svcBreak (u32 reason);
void svcOutputDebugString (const char *text, int length);
Result svcUnknown (void);
u32 *getTls (void);
s32 atomicAdd (s32 *p, s32 n);
u32 thumbSum (u32 n);

/* printing and checking (util.c) */
void print (const char *s);
void printHex (u32 v);
void printDec (s32 v);
void section (const char *name);
void check (const char *what, int ok);
void checkEq (const char *what, u32 got, u32 want);
int summary (void);

#endif
