/*
 * uaccess.S -- the kernel's fault-safe accesses to an app's memory (kern/uaccess.h), and the
 * memcpy / memset / memmove the kapi table gives legacy apps.
 *
 * Fault-safe copies: a routine here may fault on an app's bad pointer (nothing mapped there, a
 * read-only page, a misaligned access to Device memory). Each one is listed in the fixup table
 * at the end -- [start, end) and its recovery label -- and SyncHandlerEL1 (exception.cpp,
 * UAccessFixup), finding the faulting PC in a listed range, resumes at the recovery label
 * instead of halting: the routine returns -1. They are leaf functions (no stack frame, no lock,
 * x30 untouched), so the recovery is just "mov x0, #-1; ret". Plain LDR / STR, not LDTR / STTR:
 * see kern/uaccess.h for why.
 *
 * Alignment: Circle maps the display (and an app's kapi_fullscreen_direct mapping) as Device
 * memory, where a misaligned access faults; the word loops run only when both pointers are
 * 8-aligned, as Circle's own memcpy does.
 *
 * The app memcpy / memset / memmove (AppMemcpy...): the kapi table's entries for legacy apps
 * (their C library aliases memcpy & co. onto them, user/kapi.h). Leaf functions between
 * AppMemStart and AppMemEnd: a fault there with the return address (x30) in the app's code is
 * the app's own bad pointer, and kills the app (exception.cpp, AppFaultRedirect) instead of
 * halting the machine. (Circle's memcpy / memset carry no symbol size: no exact range to test.)
 *
 * ---------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ---------------------------------------------------------------------------------------------
 */

	.text

/* ---- fault-safe copies ------------------------------------------------------------------- */

/* long UAccessCopy (void *pDst, const void *pSrc, size_t nLen): 0, or -1 on a fault (part of
   the bytes may have been copied). */
	.balign	16
	.globl	UAccessCopy
	.type	UAccessCopy, %function
UAccessCopy:
	orr	x3, x0, x1
	tst	x3, #7
	b.ne	4f			/* not both 8-aligned: bytes */
	cmp	x2, #32
	b.lo	2f
1:	ldp	x4, x5, [x1], #16	/* 32 bytes a turn */
	ldp	x6, x7, [x1], #16
	stp	x4, x5, [x0], #16
	stp	x6, x7, [x0], #16
	sub	x2, x2, #32
	cmp	x2, #32
	b.hs	1b
2:	cmp	x2, #8
	b.lo	4f
3:	ldr	x4, [x1], #8		/* 8 bytes a turn */
	str	x4, [x0], #8
	sub	x2, x2, #8
	cmp	x2, #8
	b.hs	3b
4:	cbz	x2, 6f
5:	ldrb	w4, [x1], #1		/* the rest, a byte a turn */
	strb	w4, [x0], #1
	subs	x2, x2, #1
	b.ne	5b
6:	mov	x0, #0
	ret
UAccessCopyEnd:
	.size	UAccessCopy, . - UAccessCopy

/* long UAccessStrCopy (char *pDst, const char *pSrc, size_t nCap): copies the string up to and
   including its NUL, nCap bytes at most (nCap >= 1). -> its length (< nCap: pDst terminated), nCap
   if there is no NUL in the first nCap bytes (pDst not terminated), -1 on a fault. */
	.balign	16
	.globl	UAccessStrCopy
	.type	UAccessStrCopy, %function
UAccessStrCopy:
	mov	x3, #0
1:	cmp	x3, x2
	b.hs	2f
	ldrb	w4, [x1, x3]
	strb	w4, [x0, x3]
	cbz	w4, 2f
	add	x3, x3, #1
	b	1b
2:	mov	x0, x3
	ret
UAccessStrCopyEnd:
	.size	UAccessStrCopy, . - UAccessStrCopy

/* long UAccessStrLen (const char *pSrc, size_t nMax): the string's length (< nMax), nMax if there
   is no NUL in the first nMax bytes, -1 on a fault. */
	.balign	16
	.globl	UAccessStrLen
	.type	UAccessStrLen, %function
UAccessStrLen:
	mov	x3, #0
1:	cmp	x3, x1
	b.hs	2f
	ldrb	w4, [x0, x3]
	cbz	w4, 2f
	add	x3, x3, #1
	b	1b
2:	mov	x0, x3
	ret
UAccessStrLenEnd:
	.size	UAccessStrLen, . - UAccessStrLen

/* The recovery: the faulting routine returns -1 (x30 still holds its caller's return address). */
	.balign	16
UAccessFault:
	mov	x0, #-1
	ret

/* ---- the legacy apps' memcpy / memset / memmove (the kapi table's entries) ---------------- */

	.balign	16
	.globl	AppMemStart
AppMemStart:

/* void *AppMemcpy (void *pDst, const void *pSrc, size_t nLen) */
	.globl	AppMemcpy
	.type	AppMemcpy, %function
AppMemcpy:
	mov	x8, x0
	orr	x3, x0, x1
	tst	x3, #7
	b.ne	4f			/* not both 8-aligned: bytes (Device memory) */
	cmp	x2, #32
	b.lo	2f
1:	ldp	x4, x5, [x1], #16
	ldp	x6, x7, [x1], #16
	sub	x2, x2, #32
	stp	x4, x5, [x0], #16
	stp	x6, x7, [x0], #16
	prfm	pldl1strm, [x1, #64]
	cmp	x2, #32
	b.hs	1b
2:	cmp	x2, #8
	b.lo	4f
3:	ldr	x4, [x1], #8
	str	x4, [x0], #8
	sub	x2, x2, #8
	cmp	x2, #8
	b.hs	3b
4:	cbz	x2, 6f
5:	ldrb	w3, [x1], #1
	strb	w3, [x0], #1
	subs	x2, x2, #1
	b.ne	5b
6:	mov	x0, x8
	ret
	.size	AppMemcpy, . - AppMemcpy

/* void *AppMemset (void *pDst, int nValue, size_t nLen) */
	.balign	16
	.globl	AppMemset
	.type	AppMemset, %function
AppMemset:
	mov	x8, x0
	and	w1, w1, #0xFF
	cbz	x2, 9f
1:	tst	x0, #7			/* bytes up to an 8-aligned address */
	b.eq	2f
	strb	w1, [x0], #1
	subs	x2, x2, #1
	b.ne	1b
	b	9f
2:	cmp	x2, #8
	b.lo	6f
	mov	x3, #0x0101010101010101
	mul	x3, x3, x1		/* the byte in all 8 */
	cmp	x2, #32
	b.lo	4f
3:	stp	x3, x3, [x0], #16
	stp	x3, x3, [x0], #16
	sub	x2, x2, #32
	cmp	x2, #32
	b.hs	3b
4:	cmp	x2, #8
	b.lo	6f
5:	str	x3, [x0], #8
	sub	x2, x2, #8
	cmp	x2, #8
	b.hs	5b
6:	cbz	x2, 9f
7:	strb	w1, [x0], #1
	subs	x2, x2, #1
	b.ne	7b
9:	mov	x0, x8
	ret
	.size	AppMemset, . - AppMemset

/* void *AppMemmove (void *pDst, const void *pSrc, size_t nLen) */
	.balign	16
	.globl	AppMemmove
	.type	AppMemmove, %function
AppMemmove:
	sub	x3, x0, x1
	cmp	x3, x2			/* dst - src >= len (unsigned): dst below src, or past the */
	b.hs	AppMemcpy		/* end -- a forward copy is right */
	cbz	x3, 9f			/* dst == src */
	add	x4, x0, x2		/* backwards, from the ends */
	add	x5, x1, x2
	orr	x6, x0, x1
	orr	x6, x6, x2
	tst	x6, #7
	b.ne	3f
1:	ldr	x7, [x5, #-8]!		/* all 8-aligned: words */
	str	x7, [x4, #-8]!
	subs	x2, x2, #8
	b.ne	1b
	ret
3:	ldrb	w7, [x5, #-1]!
	strb	w7, [x4, #-1]!
	subs	x2, x2, #1
	b.ne	3b
9:	ret
	.size	AppMemmove, . - AppMemmove

	.globl	AppMemEnd
AppMemEnd:

/* ---- the fixup table: { start, end, recovery } per routine, a 0 start ends it ------------- */

	.section .rodata
	.balign	8
	.globl	g_UAccessFixups
g_UAccessFixups:
	.quad	UAccessCopy,	UAccessCopyEnd,		UAccessFault
	.quad	UAccessStrCopy,	UAccessStrCopyEnd,	UAccessFault
	.quad	UAccessStrLen,	UAccessStrLenEnd,	UAccessFault
	.quad	0,		0,			0
