// imagetest.cpp -- the kernel's program images (kapi v77: kernel/proc/image.cpp, kernel/proc/elf.cpp;
// kern/image.h) on the PC: the canonical path that is an image's key, the ELF header checks against
// crafted files, the streaming load, two "processes" mapping the same frames, a start waiting for
// another task's load, a failed load, the references and the pin, unload while in use, the file
// layer's hook. The kernel around them is stubbed here: an address space is a map of pages, the
// frames come from the C heap, a file is a buffer whose reads yield, and the kernel's cooperative
// tasks are threads that run one at a time (a yield hands the processor to the next).
// Run by tools/tests/run_image_test.sh. docs/02 section 7 "Program images".
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
#include <kern/image.h>
#include <kern/elf.h>
#include <kern/addrspace.h>
#include <kern/layout.h>
#include <kern/vm.h>
#include <kern/kapi_abi.h>
#include <circle/alloc.h>
#include <circle/synchronize.h>
#include <circle/logger.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <functional>
#include <vector>
#include <string>
#include <set>

// ---- the checks -------------------------------------------------------------------------------------

static int s_nChecks, s_nFailed;
#define CHECK(c)	do { s_nChecks++; if (!(c)) { s_nFailed++; printf ("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- the stubbed kernel -------------------------------------------------------------------------------

const char *g_pLastLog;
static long s_nFramesOut;			// palloc_high frames not freed yet
static long s_nFrameBudget = -1;		// frames palloc_high may still give (-1: no limit)
static u8  *s_pArena;				// frames handed out one after the other in memory (0: apart)
static unsigned s_nArenaNext, s_nArenaPages;
static unsigned s_nSyncs, s_nZaps;
static boolean s_bCommitOK = TRUE;

void *palloc_high (void)
{
	if (s_nFrameBudget == 0) return 0;
	if (s_nFrameBudget > 0) s_nFrameBudget--;
	s_nFramesOut++;
	if (s_pArena != 0 && s_nArenaNext < s_nArenaPages) return s_pArena + (u64) s_nArenaNext++ * KPAGE_SIZE;
	void *p = aligned_alloc (KPAGE_SIZE, KPAGE_SIZE);
	memset (p, 0xAA, KPAGE_SIZE);		// (a frame comes dirty: the loader must zero it)
	return p;
}

void pfree (void *p)
{
	s_nFramesOut--;
	if (s_pArena != 0 && (u8 *) p >= s_pArena && (u8 *) p < s_pArena + (u64) s_nArenaPages * KPAGE_SIZE) return;
	free (p);
}

void SyncDataAndInstructionCache (void)		{ s_nSyncs++; }
boolean VmCommitOK (u64)			{ return s_bCommitOK; }
void WordWaitsZap (u64, u64)			{ s_nZaps++; }

void VmNoteRegion (CAddressSpace *pAS, u64 s, u64 e, unsigned nProt, unsigned nKind)
{
	pAS->m_Regions.push_back ({ s, e, nProt, nKind });
}

boolean CAddressSpace::MapPage (uintptr ulVA, uintptr ulPA, const TKPageAttr &Attr, boolean bOwned)
{
	if (m_nBudget == 0) return FALSE;
	if (m_nBudget > 0) m_nBudget--;
	if ((ulVA & KPAGE_MASK) != 0 || (ulPA & KPAGE_MASK) != 0 || !IS_USER_VA (ulVA)) abort ();	// (the real one asserts)
	if (m_Pages.count (ulVA)) abort ();	// (a page mapped twice: the loader's bug)
	m_Pages[ulVA] = { (u64) ulPA, Attr.AP, Attr.UXN, bOwned != FALSE };
	return TRUE;
}

void *CAddressSpace::MapNewPage (uintptr ulVA, const TKPageAttr &Attr)
{
	void *p = palloc_high ();
	if (p == 0) return 0;
	memset (p, 0, KPAGE_SIZE);
	if (!MapPage (ulVA, (uintptr) p, Attr, TRUE)) { pfree (p); return 0; }
	return p;
}

CAddressSpace::~CAddressSpace (void)		// (as mm/addrspace.cpp: the owned frames, then the image)
{
	for (auto &it : m_Pages) if (it.second.bOwned) pfree ((void *) (uintptr) it.second.ulFrame);
	ImageRelease (m_pImage);
	while (m_nLibs != 0) ImageRelease (m_pLib[--m_nLibs]);
}

// ---- cooperative tasks: one runs at a time, a yield passes the turn ----------------------------------

static pthread_mutex_t s_Lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_Cond = PTHREAD_COND_INITIALIZER;
static int s_nTasks, s_nTurn;
static std::vector<bool> s_Done;
static thread_local int t_nMe = -1;
static unsigned s_nYields;

static void NextTurn (void)			// (s_Lock held)
{
	for (int k = 1; k <= s_nTasks; k++)
	{
		int n = (s_nTurn + k) % s_nTasks;
		if (!s_Done[n]) { s_nTurn = n; break; }
	}
	pthread_cond_broadcast (&s_Cond);
}

void TestYield (void)
{
	s_nYields++;
	if (t_nMe < 0) return;			// (one flow: nobody else runs)
	pthread_mutex_lock (&s_Lock);
	NextTurn ();
	while (s_nTurn != t_nMe) pthread_cond_wait (&s_Cond, &s_Lock);
	pthread_mutex_unlock (&s_Lock);
}

struct TTaskArg { int n; std::function<void ()> *pFn; };

static void *TaskMain (void *p)
{
	TTaskArg *a = (TTaskArg *) p;
	t_nMe = a->n;
	pthread_mutex_lock (&s_Lock);
	while (s_nTurn != t_nMe) pthread_cond_wait (&s_Cond, &s_Lock);
	pthread_mutex_unlock (&s_Lock);
	(*a->pFn) ();
	pthread_mutex_lock (&s_Lock);
	s_Done[t_nMe] = true;
	NextTurn ();
	pthread_mutex_unlock (&s_Lock);
	return 0;
}

// The tasks run until all have ended; task 0 starts.
static void RunTasks (std::vector<std::function<void ()>> Fns)
{
	s_nTasks = (int) Fns.size ();
	s_nTurn = 0;
	s_Done.assign (Fns.size (), false);
	std::vector<pthread_t> Th (Fns.size ());
	std::vector<TTaskArg> Arg (Fns.size ());
	for (size_t i = 0; i < Fns.size (); i++)
	{
		Arg[i] = { (int) i, &Fns[i] };
		pthread_create (&Th[i], 0, TaskMain, &Arg[i]);
	}
	for (size_t i = 0; i < Fns.size (); i++) pthread_join (Th[i], 0);
	s_nTasks = 0;
}

// ---- a program file ------------------------------------------------------------------------------------

struct TFile
{
	std::vector<u8> Data;
	unsigned nReads = 0;
	u64	 nBytes = 0;
	u64	 nMaxRead = 0;
	long	 nFailAfter = -1;		// reads that still succeed (-1: all)
};

static int FileRead (void *pCtx, u64 ulOffset, void *pBuffer, unsigned nBytes)
{
	TFile *f = (TFile *) pCtx;
	TestYield ();				// (the SD driver yields while the card keeps it waiting)
	if (f->nFailAfter == 0) return -1;
	if (f->nFailAfter > 0) f->nFailAfter--;
	f->nReads++;
	if (ulOffset >= f->Data.size ()) return 0;
	if (nBytes > f->Data.size () - ulOffset) nBytes = (unsigned) (f->Data.size () - ulOffset);
	memcpy (pBuffer, f->Data.data () + ulOffset, nBytes);
	f->nBytes += nBytes;
	if (nBytes > f->nMaxRead) f->nMaxRead = nBytes;
	return (int) nBytes;
}

static TImgSource SourceOf (TFile &f)	{ return { FileRead, &f, (u64) f.Data.size () }; }

struct TSegSpec
{
	u64	 ulVAddr;
	u64	 nFileSz;
	u64	 nMemSz;
	u32	 nFlags;
	u32	 nType = PT_LOAD;
};

#define BASE	0x200000000ULL			// USER_VA_BASE: where our programs are linked

// An ELF with these segments: each one's file bytes are a pattern of its index and the offset
// (so a wrong copy shows), laid out page-aligned as our linker script does.
static TFile MakeElf (const std::vector<TSegSpec> &Segs, u64 ulEntry = BASE)
{
	TFile f;
	u64 ulOff = KPAGE_SIZE;
	std::vector<Elf64_Phdr> Ph (Segs.size ());
	for (size_t i = 0; i < Segs.size (); i++)
	{
		memset (&Ph[i], 0, sizeof (Elf64_Phdr));
		Ph[i].p_type = Segs[i].nType;
		Ph[i].p_flags = Segs[i].nFlags;
		Ph[i].p_vaddr = Segs[i].ulVAddr;
		Ph[i].p_filesz = Segs[i].nFileSz;
		Ph[i].p_memsz = Segs[i].nMemSz;
		Ph[i].p_align = KPAGE_SIZE;
		Ph[i].p_offset = ulOff + (Segs[i].ulVAddr & KPAGE_MASK);	// (congruent to the address)
		ulOff = KPAGE_ALIGN_UP (Ph[i].p_offset + Segs[i].nFileSz) + KPAGE_SIZE;	// (a gap: sections not loaded)
	}
	f.Data.assign (ulOff, 0xEE);		// (what is not a segment: never to be seen in memory)
	Elf64_Ehdr Eh;
	memset (&Eh, 0, sizeof Eh);
	Eh.e_ident[0] = 0x7F; Eh.e_ident[1] = 'E'; Eh.e_ident[2] = 'L'; Eh.e_ident[3] = 'F';
	Eh.e_ident[4] = 2;
	Eh.e_type = ET_EXEC;
	Eh.e_machine = EM_AARCH64;
	Eh.e_entry = ulEntry;
	Eh.e_phoff = sizeof Eh;
	Eh.e_phentsize = sizeof (Elf64_Phdr);
	Eh.e_phnum = (Elf64_Half) Segs.size ();
	memcpy (f.Data.data (), &Eh, sizeof Eh);
	for (size_t i = 0; i < Segs.size (); i++)
	{
		memcpy (f.Data.data () + sizeof Eh + i * sizeof (Elf64_Phdr), &Ph[i], sizeof (Elf64_Phdr));
		for (u64 k = 0; k < Segs[i].nFileSz; k++) f.Data[Ph[i].p_offset + k] = (u8) (i * 37 + k * 7 + (k >> 9) + 1);
	}
	return f;
}

static u8 SegByte (size_t i, u64 k)		{ return (u8) (i * 37 + k * 7 + (k >> 9) + 1); }

static Elf64_Ehdr *EhdrOf (TFile &f)		{ return (Elf64_Ehdr *) f.Data.data (); }
static Elf64_Phdr *PhdrOf (TFile &f, int i)	{ return (Elf64_Phdr *) (f.Data.data () + sizeof (Elf64_Ehdr)) + i; }

// The usual program: R+X text of 3.5 pages, RW data of 0x1234 file bytes and a bss to 2.5 pages.
static const std::vector<TSegSpec> s_Usual = {
	{ BASE, 0x38000, 0x38000, PF_R | PF_X },
	{ BASE + 0x40000, 0x1234, 0x28000, PF_R | PF_W },
};

// The byte an address space reads at ulVA (through its page), -1: not mapped.
static int Peek (CAddressSpace &AS, u64 ulVA)
{
	auto it = AS.m_Pages.find (KPAGE_ALIGN_DOWN (ulVA));
	if (it == AS.m_Pages.end ()) return -1;
	return ((const u8 *) (uintptr) it->second.ulFrame)[ulVA & KPAGE_MASK];
}

static int Open (const char *pPath, TFile *pFile, unsigned nFlags, TImage **ppImage, unsigned *pHow = 0,
		 const char **ppWhy = 0)
{
	const char *pWhy = "";
	unsigned nHow = 0;
	TImgSource Src;
	if (pFile != 0) Src = SourceOf (*pFile);
	int r = ImageOpen (pPath, 0, pFile != 0 ? &Src : 0, nFlags, ppImage, &nHow, &pWhy);
	if (pHow != 0) *pHow = nHow;
	if (ppWhy != 0) *ppWhy = pWhy;
	return r;
}

static unsigned Images (void)			{ return ImageList (0, 0, 0, 0); }

static boolean InfoOf (const char *pPath, struct kapi_image_info *pInfo)
{
	return ImageList (pPath, 0, pInfo, 1) == 1;
}

// Nothing left: no image, no frame.
static void CheckClean (void)
{
	CHECK (Images () == 0);
	CHECK (s_nFramesOut == 0);
	CHECK (ImagePagesTotal () == 0);
}

// ---- the canonical path ---------------------------------------------------------------------------------

static std::string Canon (const char *pIn, const char *pCwd = 0)
{
	char Out[IMG_PATH_MAX];
	memset (Out, 'x', sizeof Out);
	boolean b = ImageCanonPath (pIn, pCwd, Out);
	if (!b) { CHECK (Out[0] == '\0'); return "<none>"; }
	return Out;
}

static void TestCanon (void)
{
	printf ("the canonical path\n");
	// lower case, the volume included (the user's example)
	CHECK (Canon ("SD:/Apps/MonApp.App/main") == "sd:/apps/monapp.app/main");
	CHECK (Canon ("sd:/apps/monapp.app/main") == "sd:/apps/monapp.app/main");
	// the forms the kernel's own launchers use
	CHECK (Canon ("SD:apps/tinypad.app/main") == "sd:/apps/tinypad.app/main");
	CHECK (Canon ("SD:bin/ls") == "sd:/bin/ls");
	CHECK (Canon ("SD:/bin/ls") == "sd:/bin/ls");
	// SD: and SD1: are two partitions: two keys
	CHECK (Canon ("SD1:/Apps/x.app/main") == "sd1:/apps/x.app/main");
	CHECK (Canon ("SD1:/apps/x.app/main") != Canon ("SD:/apps/x.app/main"));
	CHECK (Canon ("USB:/a") == "usb1:/a" && Canon ("usb1:/a") == "usb1:/a" && Canon ("USB1P2:/a") == "usb1p2:/a" && Canon ("usb2:/a") == "usb2:/a");
	// the spellings of ONE volume collapse: FatFs's numbers, the kernel's SD0:
	CHECK (Canon ("0:/bin/ls") == "sd:/bin/ls");
	CHECK (Canon ("1:/bin/ls") == "sd1:/bin/ls");
	CHECK (Canon ("4:/x") == "usb:/x" && Canon ("8:/x") == "nvme:/x");
	CHECK (Canon ("9:/x") == "9:/x");			// (no such volume: left alone)
	CHECK (Canon ("SD0:/bin/ls") == "sd:/bin/ls" && Canon ("sd0:bin/ls") == "sd:/bin/ls");
	// no volume: the caller's (default SD:), '/' = its root, else under the working directory
	CHECK (Canon ("/bin/ls") == "sd:/bin/ls");
	CHECK (Canon ("bin/ls") == "sd:/bin/ls");
	CHECK (Canon ("/bin/ls", "SD1:/roms") == "sd1:/bin/ls");
	CHECK (Canon ("ls", "SD:/bin") == "sd:/bin/ls");
	CHECK (Canon ("../Bin/./LS", "SD:/apps/x.app/") == "sd:/apps/bin/ls");
	CHECK (Canon ("SD1:/x", "SD:/bin") == "sd1:/x");	// (its own volume: the directory ignored)
	CHECK (Canon ("x", "nonsense") == "sd:/x");		// (a directory without a volume: the default)
	// one separator, none doubled, none at the end, no "." or ".."
	CHECK (Canon ("SD://apps///x.app//main/") == "sd:/apps/x.app/main");
	CHECK (Canon ("SD:\\apps\\x.app\\main") == "sd:/apps/x.app/main");
	CHECK (Canon ("SD:/apps/./x.app/../y.app/main") == "sd:/apps/y.app/main");
	CHECK (Canon ("SD:/../../bin/ls") == "sd:/bin/ls");	// (above the root: the root)
	CHECK (Canon ("SD:") == "sd:/" && Canon ("SD:/") == "sd:/" && Canon ("SD:/x/..") == "sd:/");
	// a name's trailing dots and spaces (FatFs drops them)
	CHECK (Canon ("SD:/bin/ls.") == "sd:/bin/ls" && Canon ("SD:/bin/ls . ") == "sd:/bin/ls");
	CHECK (Canon ("SD:/bin/.../ls") == "sd:/bin/ls");
	CHECK (Canon ("SD:/bin/.profile") == "sd:/bin/.profile");
	// bytes that are not ASCII letters: kept as they are (only A-Z folded)
	CHECK (Canon ("SD:/Apps/\x90t\x82.app/Main") == "sd:/apps/\x90t\x82.app/main");
	CHECK (Canon ("SD:/apps/\x90.app/main") != Canon ("SD:/apps/\x82.app/main"));
	CHECK (Canon ("SD:/a b/C_d-1~2") == "sd:/a b/c_d-1~2");
	// idempotent
	CHECK (Canon (Canon ("SD0:\\Apps//MonApp.App/./main.").c_str ()) == "sd:/apps/monapp.app/main");
	// nothing, too long, too deep
	CHECK (Canon ("") == "<none>" && Canon (0) == "<none>");
	std::string Long = "SD:/" + std::string (IMG_PATH_MAX - 5, 'a');		// 255 characters: fits
	CHECK (Canon (Long.c_str ()).size () == IMG_PATH_MAX - 1);
	Long += "a";
	CHECK (Canon (Long.c_str ()) == "<none>");
	std::string Deep = "SD:";
	for (int i = 0; i < 65; i++) Deep += "/a";
	CHECK (Canon (Deep.c_str ()) == "<none>");
	Deep = "SD:";
	for (int i = 0; i < 64; i++) Deep += "/a";
	CHECK (Canon (Deep.c_str ()) != "<none>");
}

// ---- the ELF headers --------------------------------------------------------------------------------

static int Plan (TFile &f, TElfPlan *pPlan, std::string *pWhy = 0)
{
	TImgSource Src = SourceOf (f);
	const char *p = "";
	int r = ElfReadPlan (&Src, pPlan, &p);
	if (pWhy != 0) *pWhy = p;
	return r;
}

static void TestElf (void)
{
	printf ("the ELF headers\n");
	TElfPlan P;
	std::string Why;

	TFile Good = MakeElf (s_Usual, BASE + 0x40);
	CHECK (Plan (Good, &P) == 0 && P.nSegs == 2 && P.ulEntry == BASE + 0x40);
	CHECK (P.Seg[0].ulVAddr == BASE && P.Seg[0].ulFileSz == 0x38000 && P.Seg[0].nFlags == (PF_R | PF_X));
	CHECK (P.Seg[1].ulFileSz == 0x1234 && P.Seg[1].ulMemSz == 0x28000);
	CHECK (Good.nBytes == sizeof (Elf64_Ehdr) + 2 * sizeof (Elf64_Phdr));	// (the headers only)

	// truncated: an empty file, half a header, the header alone
	TFile T = Good; T.Data.resize (0);
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "too short for an ELF header");
	T = Good; T.Data.resize (sizeof (Elf64_Ehdr) - 1);
	CHECK (Plan (T, &P) == -KAPI_EINVAL);
	T = Good; T.Data.resize (sizeof (Elf64_Ehdr));
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "program headers past end of image");
	T = Good; T.Data.resize (sizeof (Elf64_Ehdr) + 2 * sizeof (Elf64_Phdr) - 1);
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "program headers past end of image");
	// a read that fails (the card)
	T = Good; T.nFailAfter = 0;
	CHECK (Plan (T, &P) == -KAPI_EIO);
	T = Good; T.nFailAfter = 2;
	CHECK (Plan (T, &P, &Why) == -KAPI_EIO && Why == "read failed (the program headers)");

	// not ours
	T = Good; T.Data[1] = 'X';
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "bad ELF magic");
	T = Good; EhdrOf (T)->e_ident[4] = 1;
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "not AArch64 ELF64");
	T = Good; EhdrOf (T)->e_machine = 62;
	CHECK (Plan (T, &P) == -KAPI_EINVAL);
	T = Good; EhdrOf (T)->e_type = 1;
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "not an executable ELF");
	T = Good; EhdrOf (T)->e_type = ET_DYN;
	CHECK (Plan (T, &P) == 0);

	// the program headers: beyond the file, an entry size too small, an offset that wraps
	T = Good; EhdrOf (T)->e_phoff = T.Data.size () - 10;
	CHECK (Plan (T, &P) == -KAPI_EINVAL);
	T = Good; EhdrOf (T)->e_phoff = ~0ULL - 8;
	CHECK (Plan (T, &P) == -KAPI_EINVAL);
	T = Good; EhdrOf (T)->e_phentsize = 8;
	CHECK (Plan (T, &P) == -KAPI_EINVAL);
	T = Good; EhdrOf (T)->e_phnum = 0;
	CHECK (Plan (T, &P) == 0 && P.nSegs == 0);

	// a segment beyond the file (by one byte; an offset that wraps)
	T = Good; PhdrOf (T, 1)->p_filesz = T.Data.size () - PhdrOf (T, 1)->p_offset + 1; PhdrOf (T, 1)->p_memsz = 0x800000;
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "segment past end of image");
	T = Good; PhdrOf (T, 1)->p_filesz = T.Data.size () - PhdrOf (T, 1)->p_offset; PhdrOf (T, 1)->p_memsz = 0x800000;
	CHECK (Plan (T, &P) == 0);
	T = Good; PhdrOf (T, 0)->p_offset = ~0ULL - 16;
	CHECK (Plan (T, &P) == -KAPI_EINVAL);
	T = Good; T.Data.resize (PhdrOf (T, 0)->p_offset + 0x1000);		// (the file cut inside the text)
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "segment past end of image");

	// out of the user range: below, above, across its end, wrapping
	T = Good; PhdrOf (T, 0)->p_vaddr = 0x80000;
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "segment out of user range");
	T = Good; PhdrOf (T, 0)->p_vaddr = USER_VA_END;
	CHECK (Plan (T, &P) == -KAPI_EINVAL);
	T = Good; PhdrOf (T, 1)->p_vaddr = USER_VA_END - 0x10000; PhdrOf (T, 1)->p_memsz = 0x10001;
	CHECK (Plan (T, &P) == -KAPI_EINVAL);
	T = Good; PhdrOf (T, 1)->p_memsz = ~0ULL - BASE;
	CHECK (Plan (T, &P) == -KAPI_EINVAL);

	// two segments in one page: overlapping, and merely sharing a 64 KB page
	T = MakeElf ({ { BASE, 0x20000, 0x20000, PF_R | PF_X }, { BASE + 0x10000, 0x100, 0x100, PF_R | PF_W } });
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "two segments share a page");
	T = MakeElf ({ { BASE, 0x8000, 0x8000, PF_R | PF_X }, { BASE + 0x9000, 0x100, 0x100, PF_R | PF_W } });
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "two segments share a page");
	T = MakeElf ({ { BASE + 0x20000, 0x100, 0x100, PF_R | PF_W }, { BASE, 0x20001, 0x20001, PF_R | PF_X } });
	CHECK (Plan (T, &P) == -KAPI_EINVAL);
	T = MakeElf ({ { BASE + 0x20000, 0x100, 0x100, PF_R | PF_W }, { BASE, 0x20000, 0x20000, PF_R | PF_X } });
	CHECK (Plan (T, &P) == 0);				// (next to each other, in any order)

	// a segment without memory is left out (as /bin/ls's empty data segment at address 0), so are
	// the other types
	T = MakeElf ({ { BASE, 0x2a8, 0x2a8, PF_R | PF_X }, { 0, 0, 0, PF_R | PF_W }, { 0x10, 0x40, 0x40, PF_R, 4 /* PT_NOTE */ } });
	CHECK (Plan (T, &P) == 0 && P.nSegs == 1 && P.Seg[0].ulFileSz == 0x2a8);
	// more file bytes than memory: the excess is not loaded
	T = Good; PhdrOf (T, 1)->p_memsz = 0x1000;
	CHECK (Plan (T, &P) == 0 && P.Seg[1].ulFileSz == 0x1000 && P.Seg[1].ulMemSz == 0x1000);
	// too many segments
	std::vector<TSegSpec> Many;
	for (unsigned i = 0; i < ELF_MAX_SEGS; i++) Many.push_back ({ BASE + i * 0x10000ULL, 0x10, 0x10, PF_R });
	T = MakeElf (Many);
	CHECK (Plan (T, &P) == 0 && P.nSegs == ELF_MAX_SEGS);
	Many.push_back ({ BASE + ELF_MAX_SEGS * 0x10000ULL, 0x10, 0x10, PF_R });
	T = MakeElf (Many);
	CHECK (Plan (T, &P, &Why) == -KAPI_EINVAL && Why == "too many segments");
}

// ---- load, map, share --------------------------------------------------------------------------------

// A process of the usual program: every byte it sees is the file's, the bss zero.
static void CheckUsual (CAddressSpace &AS)
{
	boolean bText = TRUE, bData = TRUE, bBss = TRUE;
	for (u64 k = 0; k < 0x38000; k++) if (Peek (AS, BASE + k) != SegByte (0, k)) bText = FALSE;
	for (u64 k = 0; k < 0x1234; k++) if (Peek (AS, BASE + 0x40000 + k) != SegByte (1, k)) bData = FALSE;
	for (u64 k = 0x1234; k < 0x30000; k++) if (Peek (AS, BASE + 0x40000 + k) != 0) bBss = FALSE;
	for (u64 k = 0x38000; k < 0x40000; k++) if (Peek (AS, BASE + k) != 0) bText = FALSE;	// (its last page's rest)
	CHECK (bText); CHECK (bData); CHECK (bBss);
	CHECK (AS.m_Pages.size () == 4 + 3);
	CHECK (Peek (AS, BASE + 0x70000) == -1 && Peek (AS, BASE - 1) == -1);
	for (u64 va = BASE; va < BASE + 0x40000; va += KPAGE_SIZE)		// the text: shared, read + execute
	{
		const TMockPage &Pg = AS.m_Pages[va];
		CHECK (!Pg.bOwned && Pg.nAP == ATTRIB_AP_RO_ALL && Pg.nUXN == 0);
	}
	for (u64 va = BASE + 0x40000; va < BASE + 0x70000; va += KPAGE_SIZE)	// the data: its own, read + write
	{
		const TMockPage &Pg = AS.m_Pages[va];
		CHECK (Pg.bOwned && Pg.nAP == ATTRIB_AP_RW_ALL && Pg.nUXN == 1);
	}
	CHECK (AS.m_Regions.size () == 2);
	CHECK (AS.m_Regions[0].ulStart == BASE && AS.m_Regions[0].ulEnd == BASE + 0x38000
	       && AS.m_Regions[0].nProt == (KAPI_PROT_READ | KAPI_PROT_EXEC) && AS.m_Regions[0].nKind == KAPI_VMK_IMAGE);
	CHECK (AS.m_Regions[1].ulStart == BASE + 0x40000 && AS.m_Regions[1].ulEnd == BASE + 0x68000
	       && AS.m_Regions[1].nProt == (KAPI_PROT_READ | KAPI_PROT_WRITE));
}

static void TestShare (void)
{
	printf ("load, map, a second process\n");
	TFile F = MakeElf (s_Usual, BASE + 0x123);
	TImage *I1 = 0, *I2 = 0;
	unsigned nHow = 0;
	u64 ulEntry = 0;

	// not in memory, no file given: nothing done
	CHECK (Open ("SD:/bin/prog", 0, 0, &I1) == -KAPI_ENOENT && I1 == 0 && Images () == 0);

	// the first process: the file streamed -- the headers and the two segments, nothing else
	CHECK (Open ("SD:/bin/prog", &F, 0, &I1, &nHow) == 0 && nHow == IMG_HOW_LOADED);
	CHECK (F.nBytes == sizeof (Elf64_Ehdr) + 2 * sizeof (Elf64_Phdr) + 0x38000 + 0x1234);
	CHECK (F.nMaxRead <= IMG_CHUNK);
	CHECK (ImagePagesTotal () == 4 && s_nFramesOut == 4);		// (the text's frames; the data: a copy)
	u64 nShared = 0, nPrivate = 0;
	ImageSizes (I1, &nShared, &nPrivate);
	CHECK (nShared == 0x40000 && nPrivate == 0x30000);
	CAddressSpace *A = new CAddressSpace;
	CHECK (ImageMap (I1, A, &ulEntry) && ulEntry == BASE + 0x123);
	CHECK (!ImageMap (I1, A, &ulEntry));				// (one image per space)
	ImageRelease (I1);						// (A holds it)
	CheckUsual (*A);
	CHECK (s_nFramesOut == 4 + 3);

	// the second: by its path in another spelling; the card is not touched
	unsigned nReads = F.nReads;
	CHECK (Open ("sd:/BIN/Prog", 0, 0, &I2, &nHow) == 0 && nHow == IMG_HOW_SHARED && I2 == I1);
	CHECK (F.nReads == nReads);
	CAddressSpace *B = new CAddressSpace;
	CHECK (ImageMap (I2, B, &ulEntry) && ulEntry == BASE + 0x123);
	ImageRelease (I2);
	CheckUsual (*B);
	CHECK (s_nFramesOut == 4 + 3 + 3 && ImagePagesTotal () == 4);
	for (u64 va = BASE; va < BASE + 0x40000; va += KPAGE_SIZE)
	{
		CHECK (A->m_Pages[va].ulFrame == B->m_Pages[va].ulFrame);	// the same frames
	}
	for (u64 va = BASE + 0x40000; va < BASE + 0x70000; va += KPAGE_SIZE)
	{
		CHECK (A->m_Pages[va].ulFrame != B->m_Pages[va].ulFrame);	// its own data
	}
	// A writes its data: B's is untouched, and so is the image's copy (a third process)
	memset ((void *) (uintptr) A->m_Pages[BASE + 0x40000].ulFrame, 0x55, KPAGE_SIZE);
	CHECK (Peek (*B, BASE + 0x40000) == SegByte (1, 0));
	struct kapi_image_info Info;
	CHECK (InfoOf ("SD:/bin/prog", &Info) && Info.refs == 2 && Info.flags == 0
	       && Info.size == 0x40000 + 0x1234 && Info.file_size == F.Data.size ()
	       && strcmp (Info.path, "sd:/bin/prog") == 0);
	CHECK (Images () == 1);

	// A ends: its data freed, the text stays for B
	delete A;
	CHECK (s_nFramesOut == 4 + 3 && Images () == 1);
	CHECK (InfoOf ("SD:/bin/prog", &Info) && Info.refs == 1);
	CAddressSpace *C = new CAddressSpace;
	CHECK (Open ("SD:/bin/prog", 0, 0, &I1) == 0 && ImageMap (I1, C, &ulEntry));
	ImageRelease (I1);
	CheckUsual (*C);
	delete C;
	CheckUsual (*B);
	// the last one ends: everything freed (the image is not kept)
	unsigned nZaps = s_nZaps;
	delete B;
	CHECK (s_nZaps == nZaps + 4);
	CheckClean ();
	// then a start reads the file again
	CHECK (Open ("SD:/bin/prog", 0, 0, &I1) == -KAPI_ENOENT);
	CHECK (Open ("SD:/bin/prog", &F, 0, &I1, &nHow) == 0 && nHow == IMG_HOW_LOADED);
	ImageRelease (I1);
	CheckClean ();

	// two programs, two volumes: two images
	TFile G = MakeElf ({ { BASE, 0x100, 0x100, PF_R | PF_X } });
	CHECK (Open ("SD:/apps/x.app/main", &F, 0, &I1) == 0);
	CHECK (Open ("SD1:/apps/x.app/main", &G, 0, &I2, &nHow) == 0 && nHow == IMG_HOW_LOADED && I2 != I1);
	CHECK (Images () == 2);
	ImageRelease (I1); ImageRelease (I2);
	CheckClean ();

	// a path too long for a key: loaded, never shared
	std::string Long = "SD:/" + std::string (300, 'a');
	CHECK (Open (Long.c_str (), &G, 0, &I1, &nHow) == 0 && nHow == IMG_HOW_LOADED);
	CHECK (Open (Long.c_str (), &G, 0, &I2, &nHow) == 0 && nHow == IMG_HOW_LOADED && I2 != I1);
	ImageRelease (I1); ImageRelease (I2);
	CheckClean ();
}

// The segments' shapes: an unaligned start, a segment of one byte, read-only data without
// execute, a writable + executable one, a bss alone.
static void TestShapes (void)
{
	printf ("the segments' shapes\n");
	TFile F = MakeElf ({
		{ BASE + 0x1230, 0x20000, 0x20000, PF_R | PF_X },	// starts inside its first page: 3 pages
		{ BASE + 0x40000, 1, 1, PF_R },				// read-only data: shared, no execute
		{ BASE + 0x50000, 0x10, 0x20010, PF_R | PF_W | PF_X },	// writable: private (PF_X: mapped as code)
		{ BASE + 0x80000, 0, 0x100, PF_R | PF_W },		// a bss alone
	});
	TImage *I = 0;
	u64 ulEntry;
	CHECK (Open ("SD:/bin/shapes", &F, 0, &I) == 0);
	CHECK (ImagePagesTotal () == 3 + 1);
	CAddressSpace AS;
	CHECK (ImageMap (I, &AS, &ulEntry));
	ImageRelease (I);
	boolean bOK = TRUE;
	for (u64 k = 0; k < 0x20000; k++) if (Peek (AS, BASE + 0x1230 + k) != SegByte (0, k)) bOK = FALSE;
	for (u64 k = 0; k < 0x1230; k++) if (Peek (AS, BASE + k) != 0) bOK = FALSE;		// (before it: zero)
	for (u64 k = 0x21230; k < 0x30000; k++) if (Peek (AS, BASE + k) != 0) bOK = FALSE;	// (after it: zero)
	CHECK (bOK);
	CHECK (Peek (AS, BASE + 0x40000) == SegByte (1, 0) && Peek (AS, BASE + 0x40001) == 0);
	CHECK (!AS.m_Pages[BASE + 0x40000].bOwned && AS.m_Pages[BASE + 0x40000].nAP == ATTRIB_AP_RO_ALL
	       && AS.m_Pages[BASE + 0x40000].nUXN == 1);
	CHECK (AS.m_Regions[1].nProt == KAPI_PROT_READ);
	CHECK (AS.m_Pages[BASE + 0x50000].bOwned && AS.m_Pages[BASE + 0x50000].nAP == ATTRIB_AP_RO_ALL
	       && AS.m_Pages[BASE + 0x50000].nUXN == 0);
	CHECK (Peek (AS, BASE + 0x5000F) == SegByte (2, 0xF) && Peek (AS, BASE + 0x50010) == 0 && Peek (AS, BASE + 0x7000F) == 0);
	CHECK (AS.m_Pages.count (BASE + 0x70000) == 1 && AS.m_Pages.count (BASE + 0x30000) == 0);
	CHECK (AS.m_Pages[BASE + 0x80000].bOwned && Peek (AS, BASE + 0x800FF) == 0);
	CHECK (AS.m_Pages.size () == 3 + 1 + 3 + 1);
}

// The reads: one per IMG_CHUNK when the frames follow each other in memory, one per page when not;
// a yield every IMG_CHUNK bytes in both cases.
static void TestReads (void)
{
	printf ("the reads of a load\n");
	const u64 nText = 10 * KPAGE_SIZE + 0x8000;
	TFile F = MakeElf ({ { BASE, nText, nText, PF_R | PF_X } });
	TImage *I = 0;
	u64 ulEntry;

	s_nArenaPages = 16;
	s_pArena = (u8 *) aligned_alloc (KPAGE_SIZE, s_nArenaPages * KPAGE_SIZE);
	memset (s_pArena, 0xAA, s_nArenaPages * KPAGE_SIZE);
	s_nArenaNext = 0;
	s_nYields = 0;
	CHECK (Open ("SD:/bin/big", &F, 0, &I) == 0);
	CHECK (F.nReads == 2 + 6 && F.nMaxRead == IMG_CHUNK);		// (2 headers; 5 x 128 KB + 96 KB)
	unsigned nLoadYields = s_nYields - F.nReads;			// (the file's own yields apart)
	CHECK (nLoadYields == 5);
	{
		CAddressSpace AS;
		CHECK (ImageMap (I, &AS, &ulEntry));
		boolean bOK = TRUE;
		for (u64 k = 0; k < nText; k++) if (Peek (AS, BASE + k) != SegByte (0, k)) bOK = FALSE;
		for (u64 k = nText; k < 11 * KPAGE_SIZE; k++) if (Peek (AS, BASE + k) != 0) bOK = FALSE;
		CHECK (bOK);
	}
	ImageRelease (I);
	CheckClean ();
	free (s_pArena);
	s_pArena = 0;

	TFile G = MakeElf ({ { BASE, nText, nText, PF_R | PF_X } });	// frames apart: a read per page
	s_nYields = 0;
	CHECK (Open ("SD:/bin/big", &G, 0, &I) == 0);
	CHECK (G.nReads <= 2 + 11 && G.nReads >= 2 + 6 && G.nMaxRead <= IMG_CHUNK);
	CHECK (s_nYields - G.nReads == 5);
	{
		CAddressSpace AS;
		CHECK (ImageMap (I, &AS, &ulEntry));
		boolean bOK = TRUE;
		for (u64 k = 0; k < nText; k++) if (Peek (AS, BASE + k) != SegByte (0, k)) bOK = FALSE;
		for (u64 k = nText; k < 11 * KPAGE_SIZE; k++) if (Peek (AS, BASE + k) != 0) bOK = FALSE;
		CHECK (bOK);
	}
	ImageRelease (I);
	CheckClean ();

	// a large writable segment: its copy read in pieces too
	const u64 nData = 3 * IMG_CHUNK + 5;
	TFile H = MakeElf ({ { BASE, nData, nData + 0x1000, PF_R | PF_W } });
	CHECK (Open ("SD:/bin/data", &H, 0, &I) == 0 && H.nReads == 2 + 4 && H.nMaxRead == IMG_CHUNK);
	{
		CAddressSpace AS;
		CHECK (ImageMap (I, &AS, &ulEntry));
		boolean bOK = TRUE;
		for (u64 k = 0; k < nData; k++) if (Peek (AS, BASE + k) != SegByte (0, k)) bOK = FALSE;
		for (u64 k = nData; k < KPAGE_ALIGN_UP (nData + 0x1000); k++) if (Peek (AS, BASE + k) != 0) bOK = FALSE;
		CHECK (bOK);
	}
	ImageRelease (I);
	CheckClean ();
}

// ---- a start during another task's load -----------------------------------------------------------------

static void TestWait (void)
{
	printf ("a start during another task's load\n");
	const u64 nText = 20 * KPAGE_SIZE;
	TFile F = MakeElf ({ { BASE, nText, nText, PF_R | PF_X }, { BASE + nText, 0x100, 0x100, PF_R | PF_W } });
	TImage *I[3] = { 0, 0, 0 };
	unsigned nHow[3] = { 0, 0, 0 };
	int nErr[3] = { 1, 1, 1 };
	unsigned nSeenLoading = 0;
	CAddressSpace *AS[3] = { new CAddressSpace, new CAddressSpace, new CAddressSpace };
	auto Start = [&] (int n, TFile *pFile)
	{
		u64 ulEntry;
		nErr[n] = Open ("SD:/bin/wait", pFile, 0, &I[n], &nHow[n]);
		if (nErr[n] == 0)
		{
			CHECK (ImageMap (I[n], AS[n], &ulEntry));
			ImageRelease (I[n]);
		}
	};
	RunTasks ({
		[&] { Start (0, &F); },				// the first: loads (its reads yield)
		[&] { Start (1, 0); },				// the second, while the load runs: waits, no file
		[&] {						// the third looks at the list, then starts too
			struct kapi_image_info Info;
			if (InfoOf ("SD:/bin/wait", &Info) && (Info.flags & KAPI_IMG_LOADING)) nSeenLoading = Info.refs;
			TFile Own = F;				// (it had opened the file itself meanwhile)
			Start (2, &Own);
			CHECK (Own.nReads == 0);		// ... and did not read it
		},
	});
	CHECK (nErr[0] == 0 && nErr[1] == 0 && nErr[2] == 0);
	CHECK (nHow[0] == IMG_HOW_LOADED && nHow[1] == IMG_HOW_WAITED && nHow[2] == IMG_HOW_WAITED);
	CHECK (I[0] == I[1] && I[1] == I[2]);
	CHECK (nSeenLoading == 2);				// (the loader and the second one)
	CHECK (F.nBytes == sizeof (Elf64_Ehdr) + 2 * sizeof (Elf64_Phdr) + nText + 0x100);	// read once
	CHECK (ImagePagesTotal () == 20);
	CHECK (AS[0]->m_Pages[BASE].ulFrame == AS[1]->m_Pages[BASE].ulFrame
	       && AS[1]->m_Pages[BASE].ulFrame == AS[2]->m_Pages[BASE].ulFrame);
	boolean bOK = TRUE;
	for (int n = 0; n < 3; n++)
		for (u64 k = 0; k < nText; k += 0x777) if (Peek (*AS[n], BASE + k) != SegByte (0, k)) bOK = FALSE;
	CHECK (bOK);
	for (int n = 0; n < 3; n++) delete AS[n];
	CheckClean ();
}

static void TestFail (void)
{
	printf ("a load that fails\n");
	const u64 nText = 20 * KPAGE_SIZE;
	TFile Good = MakeElf ({ { BASE, nText, nText, PF_R | PF_X }, { BASE + nText, 0x100, 0x100, PF_R | PF_W } });
	TImage *I = 0;
	const char *pWhy = "";

	// alone: the card fails in the middle of the text, then in the data's copy
	TFile F = Good; F.nFailAfter = 6;
	CHECK (Open ("SD:/bin/fail", &F, 0, &I, 0, &pWhy) == -KAPI_EIO && I == 0 && strcmp (pWhy, "read failed") == 0);
	CheckClean ();
	// a file that ends early (cut after its headers were read: a short read)
	F = Good; F.Data.resize (F.Data.size ());
	{
		TImgSource Src = SourceOf (F);
		F.Data.resize (PhdrOf (F, 0)->p_offset + 5 * KPAGE_SIZE + 100);
		unsigned nHow;
		CHECK (ImageOpen ("SD:/bin/fail", 0, &Src, 0, &I, &nHow, &pWhy) == -KAPI_EIO);	// (the size it was opened with)
	}
	CheckClean ();
	// malformed: the reason comes back
	F = Good; F.Data[0] = 0;
	CHECK (Open ("SD:/bin/fail", &F, 0, &I, 0, &pWhy) == -KAPI_EINVAL && strcmp (pWhy, "bad ELF magic") == 0);
	CheckClean ();
	// out of frames in the middle
	F = Good; s_nFrameBudget = 7;
	CHECK (Open ("SD:/bin/fail", &F, 0, &I, 0, &pWhy) == -KAPI_ENOMEM && strcmp (pWhy, "out of memory") == 0);
	s_nFrameBudget = -1;
	CheckClean ();

	// with two starts waiting: both get the error, nothing is left, and the next start loads
	int nErr[3] = { 1, 1, 1 };
	TImage *pI[3] = { 0, 0, 0 };
	F = Good; F.nFailAfter = 9;
	RunTasks ({
		[&] { nErr[0] = Open ("SD:/bin/fail", &F, 0, &pI[0]); },
		[&] { nErr[1] = Open ("SD:/bin/fail", 0, IMG_OPEN_PIN, &pI[1]); },	// (a preload waiting)
		[&] { nErr[2] = Open ("SD:/bin/fail", 0, 0, &pI[2]); },
	});
	CHECK (nErr[0] == -KAPI_EIO && nErr[1] == -KAPI_EIO && nErr[2] == -KAPI_EIO);
	CHECK (pI[0] == 0 && pI[1] == 0 && pI[2] == 0);
	CheckClean ();
	F = Good;
	CHECK (Open ("SD:/bin/fail", &F, 0, &I) == 0);
	ImageRelease (I);
	CheckClean ();

	// a process that cannot map it (out of memory for its own pages, then for its tables): the
	// image is intact, the space's teardown gives everything back
	CHECK (Open ("SD:/bin/fail", &Good, 0, &I) == 0);
	u64 ulEntry;
	{
		CAddressSpace AS;
		s_nFrameBudget = 0;
		CHECK (!ImageMap (I, &AS, &ulEntry));
		s_nFrameBudget = -1;
	}
	{
		CAddressSpace AS;
		AS.m_nBudget = 3;
		CHECK (!ImageMap (I, &AS, &ulEntry));
	}
	struct kapi_image_info Info;
	CHECK (InfoOf ("SD:/bin/fail", &Info) && Info.refs == 1);
	{
		CAddressSpace AS;
		CHECK (ImageMap (I, &AS, &ulEntry) && Peek (AS, BASE + 0x777) == SegByte (0, 0x777));
	}
	ImageRelease (I);
	CheckClean ();
}

// ---- preload, unload, the file layer's hook ---------------------------------------------------------------

static void TestPin (void)
{
	printf ("preload, unload\n");
	TFile F = MakeElf (s_Usual);
	TImage *I = 0, *I2 = 0;
	unsigned nHow;
	u64 ulEntry;
	struct kapi_image_info Info;

	// a preload: loaded, pinned, kept with no reference
	CHECK (Open ("SD:/Apps/MonApp.App/main", &F, IMG_OPEN_PIN, &I, &nHow) == 0 && nHow == IMG_HOW_LOADED);
	ImageRelease (I);
	CHECK (Images () == 1 && ImagePagesTotal () == 4);
	CHECK (InfoOf ("sd:/apps/monapp.app/main", &Info) && Info.refs == 0 && Info.flags == KAPI_IMG_KEPT);
	// a second preload: nothing more
	CHECK (Open ("SD:/apps/monapp.app/main", 0, IMG_OPEN_PIN, &I, &nHow) == 0 && nHow == IMG_HOW_SHARED);
	ImageRelease (I);
	CHECK (Images () == 1);
	// runs of that path: the kept image, the card never touched; it stays when they end
	unsigned nReads = F.nReads;
	for (int k = 0; k < 3; k++)
	{
		CAddressSpace AS;
		CHECK (Open ("SD:/Apps/MonApp.App/main", 0, 0, &I, &nHow) == 0 && nHow == IMG_HOW_SHARED);
		CHECK (ImageMap (I, &AS, &ulEntry));
		ImageRelease (I);
		CheckUsual (AS);
	}
	CHECK (F.nReads == nReads && Images () == 1 && s_nFramesOut == 4);
	// unload with nobody running it: freed at once
	CHECK (ImageUnload ("sd:/apps/MONAPP.app/main", 0) == 0);
	CheckClean ();
	CHECK (ImageUnload ("SD:/apps/monapp.app/main", 0) == -KAPI_ENOENT);
	CHECK (ImageUnload ("", 0) == -KAPI_ENOENT);

	// a running program preloaded afterwards: pinned where it is
	CAddressSpace *A = new CAddressSpace;
	CHECK (Open ("SD:/bin/tool", &F, 0, &I) == 0 && ImageMap (I, A, &ulEntry));
	ImageRelease (I);
	CHECK (InfoOf ("SD:/bin/tool", &Info) && Info.flags == 0);
	nReads = F.nReads;
	CHECK (Open ("SD:/bin/tool", &F, IMG_OPEN_PIN, &I2, &nHow) == 0 && nHow == IMG_HOW_SHARED && I2 == I);
	ImageRelease (I2);
	CHECK (F.nReads == nReads && InfoOf ("SD:/bin/tool", &Info) && Info.flags == KAPI_IMG_KEPT && Info.refs == 1);

	// unload while in use: no name, no pin at once; the process goes on; a new start loads again
	CHECK (ImageUnload ("tool", "SD:/bin") == 0);			// (relative to a working directory)
	CHECK (!InfoOf ("SD:/bin/tool", &Info));
	CHECK (Images () == 1);
	struct kapi_image_info All[4];
	CHECK (ImageList (0, 0, All, 4) == 1 && All[0].flags == KAPI_IMG_UNNAMED && All[0].refs == 1);
	CHECK (Open ("SD:/bin/tool", 0, 0, &I2) == -KAPI_ENOENT);
	CheckUsual (*A);
	TFile F2 = MakeElf ({ { BASE, 0x500, 0x500, PF_R | PF_X } }, BASE + 8);	// (the new version of the file)
	CAddressSpace *B = new CAddressSpace;
	CHECK (Open ("SD:/bin/tool", &F2, 0, &I2, &nHow) == 0 && nHow == IMG_HOW_LOADED && I2 != I);
	CHECK (ImageMap (I2, B, &ulEntry) && ulEntry == BASE + 8);
	ImageRelease (I2);
	CHECK (Images () == 2 && ImagePagesTotal () == 4 + 1);
	CHECK (A->m_Pages[BASE].ulFrame != B->m_Pages[BASE].ulFrame);
	CheckUsual (*A);						// (the old process: its old program, whole)
	CHECK (ImageList (0, 0, All, 1) == 2);				// (more than asked: the count)
	delete A;							// the old image goes with its last process
	CHECK (Images () == 1 && ImagePagesTotal () == 1);
	delete B;
	CheckClean ();

	// a preload that would take the app pool's reserve is refused; a process's own start is not
	s_bCommitOK = FALSE;
	const char *pWhy = "";
	CHECK (Open ("SD:/bin/tool", &F, IMG_OPEN_PIN, &I, 0, &pWhy) == -KAPI_ENOMEM
	       && strcmp (pWhy, "not enough free memory to keep it") == 0);
	CheckClean ();
	CHECK (Open ("SD:/bin/tool", &F, 0, &I) == 0);
	ImageRelease (I);
	s_bCommitOK = TRUE;
	CheckClean ();

	// unload during its load (a preload): the loader finishes, nothing is kept
	int nErr = 1;
	TFile Big = MakeElf ({ { BASE, 12 * KPAGE_SIZE, 12 * KPAGE_SIZE, PF_R | PF_X } });
	RunTasks ({
		[&] { TImage *p = 0; nErr = Open ("SD:/bin/big", &Big, IMG_OPEN_PIN, &p); if (nErr == 0) ImageRelease (p); },
		[&] { CHECK (InfoOf ("SD:/bin/big", &Info) && Info.flags == (KAPI_IMG_KEPT | KAPI_IMG_LOADING));
		      CHECK (ImageUnload ("SD:/bin/big", 0) == 0); },
	});
	CHECK (nErr == 0);
	CheckClean ();
}

static void TestFileChanged (void)
{
	printf ("the file layer's hook\n");
	TFile F = MakeElf (s_Usual);
	struct kapi_image_info Info;
	TImage *I = 0;
	u64 ulEntry;
	auto Preload = [&] (const char *pPath)
	{
		TImage *p = 0;
		CHECK (Open (pPath, &F, IMG_OPEN_PIN, &p) == 0);
		ImageRelease (p);
	};

	ImageFileChanged ("SD:/bin/nothing");				// (no image at all: nothing)
	Preload ("SD:/apps/x.app/main");
	Preload ("SD:/apps/xy.app/main");
	Preload ("SD:/bin/tool");
	Preload ("SD1:/apps/x.app/main");
	CHECK (Images () == 4);
	// other files: nothing
	ImageFileChanged ("SD:/bin/tool2");
	ImageFileChanged ("SD:/bin/too");
	ImageFileChanged ("SD:/apps/x.app/main.bak");
	ImageFileChanged ("SD:/apps/x.app/icon.bmp");
	ImageFileChanged ("SD:/apps/x.ap");
	ImageFileChanged ("SD2:/apps/x.app/main");
	ImageFileChanged ("RAM:/bin/tool");
	ImageFileChanged ("SD:/apps/x.app/main/deeper");
	ImageFileChanged ("");
	CHECK (Images () == 4);
	// the file itself (removed, renamed, opened for writing), in any spelling: unpinned, freed
	ImageFileChanged ("SD:/BIN/Tool");
	CHECK (Images () == 3 && !InfoOf ("SD:/bin/tool", &Info));
	// a folder above it (renamed, removed): the programs under it, not its neighbours
	ImageFileChanged ("SD:/apps/x.app");
	CHECK (Images () == 2 && !InfoOf ("SD:/apps/x.app/main", &Info));
	CHECK (InfoOf ("SD:/apps/xy.app/main", &Info) && InfoOf ("SD1:/apps/x.app/main", &Info));
	// its app.txt (the stack size is read once per image): the image stays, the size is forgotten
	CHECK (Open ("SD:/apps/xy.app/main", 0, 0, &I) == 0);
	ImageSetStack (I, 0x800000);
	ImageFileChanged ("SD:/apps/xy.app/notapp.txt");
	ImageFileChanged ("SD:/apps/x.app/app.txt");			// (another app's)
	ImageFileChanged ("SD1:/apps/xy.app/app.txt");			// (another volume's)
	CHECK (ImageStack (I) == 0x800000);
	ImageFileChanged ("SD:/Apps/XY.app/App.txt");
	CHECK (ImageStack (I) == 0);
	ImageRelease (I);
	CHECK (Images () == 2 && InfoOf ("SD:/apps/xy.app/main", &Info) && Info.flags == KAPI_IMG_KEPT);
	// a volume's root
	ImageFileChanged ("SD1:/");
	CHECK (Images () == 1);
	ImageFileChanged ("SD:/apps");
	CheckClean ();

	// in use: the process keeps its image, new starts load the file again, freed with the process
	CAddressSpace *A = new CAddressSpace;
	Preload ("SD:/bin/tool");
	CHECK (Open ("SD:/bin/tool", 0, 0, &I) == 0 && ImageMap (I, A, &ulEntry));
	ImageRelease (I);
	ImageFileChanged ("SD:/bin/tool");
	CHECK (Images () == 1 && !InfoOf ("SD:/bin/tool", &Info) && ImagePagesTotal () == 4);
	CheckUsual (*A);
	ImageFileChanged ("SD:/bin/tool");				// (again: nothing more)
	CHECK (Open ("SD:/bin/tool", 0, 0, &I) == -KAPI_ENOENT);
	delete A;
	CheckClean ();

	// the stack size kept with the image
	CHECK (Open ("SD:/apps/x.app/main", &F, 0, &I) == 0);
	CHECK (ImageStack (I) == 0);
	ImageSetStack (I, 0x800000);
	TImage *I2 = 0;
	CHECK (Open ("SD:/apps/x.app/main", 0, 0, &I2) == 0 && ImageStack (I2) == 0x800000);
	ImageRelease (I); ImageRelease (I2);
	CheckClean ();
}

// ---- the old entry point ------------------------------------------------------------------------------

static void TestLoadELF (void)
{
	printf ("LoadELF (a whole file in memory)\n");
	TFile F = MakeElf (s_Usual, BASE + 0x10);
	u64 ulEntry = 0;
	unsigned nSyncs = s_nSyncs;
	{
		CAddressSpace A, B;
		CHECK (LoadELF (F.Data.data (), F.Data.size (), &A, &ulEntry) && ulEntry == BASE + 0x10);
		CHECK (s_nSyncs == nSyncs + 1);
		CheckUsual (A);
		CHECK (LoadELF (F.Data.data (), F.Data.size (), &B, &ulEntry));	// (never shared: no path)
		CHECK (A.m_Pages[BASE].ulFrame != B.m_Pages[BASE].ulFrame);
		CHECK (Images () == 2);
		struct kapi_image_info All[2];
		CHECK (ImageList (0, 0, All, 2) == 2 && All[0].flags == KAPI_IMG_UNNAMED && All[0].path[0] == '\0');
	}
	CheckClean ();
	{
		CAddressSpace A;
		F.Data[2] = 'x';
		CHECK (!LoadELF (F.Data.data (), F.Data.size (), &A, &ulEntry));
		CHECK (g_pLastLog != 0 && strcmp (g_pLastLog, "bad ELF magic") == 0);
		CHECK (!LoadELF (0, 100, &A, &ulEntry) && !LoadELF (F.Data.data (), 10, &A, &ulEntry));
		CHECK (A.m_Pages.empty ());
	}
	CheckClean ();
}

// ---- shared libraries (v83): a real library, built by the cross toolchain (user/Libs/demo) -----------------

static u64 Peek64 (CAddressSpace &AS, u64 ulVA)
{
	u64 v = 0;
	for (int i = 7; i >= 0; i--) v = (v << 8) | (u8) Peek (AS, ulVA + i);
	return v;
}

static void TestLib (void)
{
	const char *pSo = getenv ("ONYX_DEMO_SO");
	if (pSo == 0 || pSo[0] == '\0')
	{
		printf ("shared libraries: SKIPPED (no cross toolchain: ONYX_DEMO_SO is not set)\n");
		return;
	}
	printf ("shared libraries\n");
	TFile F;
	{
		FILE *fp = fopen (pSo, "rb");
		CHECK (fp != 0);
		if (fp == 0) return;
		u8 Buf[4096]; size_t n;
		while ((n = fread (Buf, 1, sizeof Buf, fp)) > 0) F.Data.insert (F.Data.end (), Buf, Buf + n);
		fclose (fp);
	}
	const char *pWhy = "";
	TElfPlan P, Q;
	TImgSource S = SourceOf (F);
	// a library is a library, not a program -- and the other way round
	CHECK (ElfReadPlan (&S, &P, &pWhy, ELF_KIND_LIB) == 0 && P.bLib && P.nSegs == 2 && P.ulDynSize != 0);
	CHECK (ElfReadPlan (&S, &Q, &pWhy, ELF_KIND_PROGRAM) == -KAPI_EINVAL);
	CHECK (ElfReadPlan (&S, &Q, &pWhy, ELF_KIND_ANY) == 0 && Q.bLib);
	TFile Prog = MakeElf (s_Usual);
	TImgSource S2 = SourceOf (Prog);
	CHECK (ElfReadPlan (&S2, &Q, &pWhy, ELF_KIND_LIB) == -KAPI_EINVAL);
	CHECK (ElfReadPlan (&S2, &Q, &pWhy, ELF_KIND_ANY) == 0 && !Q.bLib);
	const u64 ulEntry = P.ulEntry;
	const u64 nSpan = KPAGE_ALIGN_UP (P.Seg[1].ulVAddr + P.Seg[1].ulMemSz);
	const u64 nDataOff = P.Seg[1].ulOffset - P.Seg[1].ulVAddr;	// a data address -> its place in the file

	// loaded: placed at the arena's base, relocated once
	TImage *pI = 0;
	CHECK (Open ("SD:/lib/demo.so", &F, IMG_OPEN_LIB, &pI) == 0);
	u64 ulBase = 0; unsigned nRelocs = 0, nVersion = 0;
	CHECK (ImageLibInfo (pI, &ulBase, &nRelocs, &nVersion) && ulBase == USER_LIB_BASE && nRelocs > 4 && nVersion == 2);
	{
		CAddressSpace A, B, C;
		u64 tA = 0, tB = 0, e = 0;
		CHECK (ImageMapLib (pI, &A, &tA) == 1 && ImageMapLib (pI, &B, &tB) == 1 && tA == tB);
		CHECK (ImageMapLib (pI, &A, &tA) == 0 && tA == ulBase + ulEntry);	// again: the same table
		CHECK (!ImageMap (pI, &C, &e));						// not a program
		// the code: the same frames in both, not owned, never writable; the data: each its own
		CHECK (A.m_Pages[ulBase].ulFrame == B.m_Pages[ulBase].ulFrame && !A.m_Pages[ulBase].bOwned);
		CHECK (A.m_Pages[ulBase].nAP == ATTRIB_AP_RO_ALL && A.m_Pages[ulBase].nUXN == 0);
		u64 ulData = KPAGE_ALIGN_DOWN (tA);
		CHECK (A.m_Pages[ulData].ulFrame != B.m_Pages[ulData].ulFrame && A.m_Pages[ulData].bOwned);
		CHECK (A.m_Pages[ulData].nAP == ATTRIB_AP_RW_ALL && A.m_Pages[ulData].nUXN == 1);
		// the table as a process reads it: its version, and init = the base + what the file holds
		CHECK ((u32) Peek64 (A, tA) == 2);
		u64 ulRaw = 0;
		memcpy (&ulRaw, F.Data.data () + nDataOff + ulEntry + 8, 8);
		CHECK (ulRaw != 0 && ulRaw < P.Seg[0].ulMemSz);
		CHECK (Peek64 (A, tA + 8) == ulBase + ulRaw && Peek64 (B, tA + 8) == ulBase + ulRaw);
		struct kapi_image_info Info;
		CHECK (InfoOf ("SD:/lib/demo.so", &Info) && (Info.flags & KAPI_IMG_LIB) && Info.refs == 3);
		// found by its path: only as what it is
		TImage *pX = 0;
		CHECK (Open ("SD:/lib/demo.so", 0, 0, &pX) == -KAPI_EINVAL);
		CHECK (Open ("SD:/lib/demo.so", 0, IMG_OPEN_ANY, &pX) == 0 && pX == pI);
		ImageRelease (pX);

		// a second library: the next place (a page between), both in one process
		TImage *p2 = 0;
		CHECK (Open ("SD:/lib/demo2.so", &F, IMG_OPEN_LIB, &p2) == 0);
		u64 ulBase2 = 0, t2 = 0;
		CHECK (ImageLibInfo (p2, &ulBase2, 0, 0) && ulBase2 == ulBase + nSpan + KPAGE_SIZE);
		CHECK (ImageMapLib (p2, &A, &t2) == 1 && t2 == ulBase2 + ulEntry);
		CHECK (Peek64 (A, t2 + 8) == ulBase2 + ulRaw);
		ImageRelease (p2);

		// its file replaced: the processes keep the old one where it is, a new load goes elsewhere
		ImageFileChanged ("SD:/lib/demo.so");
		TImage *p3 = 0;
		CHECK (Open ("SD:/lib/demo.so", &F, IMG_OPEN_LIB, &p3) == 0 && p3 != pI);
		u64 ulBase3 = 0;
		CHECK (ImageLibInfo (p3, &ulBase3, 0, 0) && ulBase3 == ulBase2 + nSpan + KPAGE_SIZE);
		CHECK (Peek64 (A, tA + 8) == ulBase + ulRaw);
		ImageRelease (p3);
		ImageRelease (pI);
	}
	CheckClean ();
	// the arena's range came back
	CHECK (Open ("SD:/lib/demo.so", &F, IMG_OPEN_LIB, &pI) == 0);
	CHECK (ImageLibInfo (pI, &ulBase, 0, 0) && ulBase == USER_LIB_BASE);
	{
		// out of memory while mapping: not usable in that process, never handed out, all freed
		CAddressSpace A;
		u64 t = 0;
		A.m_nBudget = 1;
		CHECK (ImageMapLib (pI, &A, &t) == -KAPI_ENOMEM);
		A.m_nBudget = -1;
		CHECK (ImageMapLib (pI, &A, &t) == -KAPI_ENOMEM);
		// 16 libraries in one process at most
		CAddressSpace B;
		TImage *Many[AS_LIB_MAX + 1];
		for (unsigned i = 0; i <= AS_LIB_MAX; i++)
		{
			char Name[32];
			snprintf (Name, sizeof Name, "SD:/lib/many%u.so", i);
			CHECK (Open (Name, &F, IMG_OPEN_LIB, &Many[i]) == 0);
			CHECK (ImageMapLib (Many[i], &B, &t) == (i < AS_LIB_MAX ? 1 : -KAPI_EMFILE));
			ImageRelease (Many[i]);
		}
	}
	ImageRelease (pI);
	CheckClean ();

	// refused: a relocation that is not RELATIVE, one outside the data, text relocations
	u64 ulRela = 0, ulRelaEntTag = 0;
	for (u64 o = nDataOff + P.ulDynVAddr; o < nDataOff + P.ulDynVAddr + P.ulDynSize; o += sizeof (Elf64_Dyn))
	{
		Elf64_Dyn D;
		memcpy (&D, F.Data.data () + o, sizeof D);
		if (D.d_tag == DT_RELA) ulRela = D.d_val;	// (in the code segment: its file offset too)
		if (D.d_tag == DT_RELAENT) ulRelaEntTag = o;
	}
	CHECK (ulRela != 0 && ulRelaEntTag != 0);
	{
		TFile G = F;
		((Elf64_Rela *) (G.Data.data () + ulRela))[1].r_info = 1026;	// R_AARCH64_JUMP_SLOT
		CHECK (Open ("SD:/lib/bad.so", &G, IMG_OPEN_LIB, &pI, 0, &pWhy) == -KAPI_EINVAL);
		CHECK (strstr (pWhy, "relocation type 1026") != 0);
		G = F;
		((Elf64_Rela *) (G.Data.data () + ulRela))[0].r_offset = 0x100;	// in the code
		CHECK (Open ("SD:/lib/bad.so", &G, IMG_OPEN_LIB, &pI, 0, &pWhy) == -KAPI_EINVAL);
		CHECK (strstr (pWhy, "outside the library's data") != 0);
		G = F;
		u64 nTag = DT_TEXTREL;
		memcpy (G.Data.data () + ulRelaEntTag, &nTag, 8);
		CHECK (Open ("SD:/lib/bad.so", &G, IMG_OPEN_LIB, &pI, 0, &pWhy) == -KAPI_EINVAL);
		CHECK (strstr (pWhy, "text relocations") != 0);
		G = F;
		EhdrOf (G)->e_entry = 0x40;					// the table must be in the data
		CHECK (Open ("SD:/lib/bad.so", &G, IMG_OPEN_LIB, &pI, 0, &pWhy) == -KAPI_EINVAL);
		G = F;
		G.nFailAfter = 3;						// a read error half-way
		CHECK (Open ("SD:/lib/bad.so", &G, IMG_OPEN_LIB, &pI, 0, &pWhy) < 0);
	}
	CheckClean ();
	// after the failures the arena is whole
	CHECK (Open ("SD:/lib/demo.so", &F, IMG_OPEN_LIB, &pI) == 0);
	CHECK (ImageLibInfo (pI, &ulBase, 0, 0) && ulBase == USER_LIB_BASE);
	ImageRelease (pI);
	CheckClean ();
}

int main (void)
{
	TestCanon ();
	TestElf ();
	TestShare ();
	TestShapes ();
	TestReads ();
	TestWait ();
	TestFail ();
	TestPin ();
	TestFileChanged ();
	TestLoadELF ();
	TestLib ();
	printf ("%d checks, %d failed\n", s_nChecks, s_nFailed);
	return s_nFailed == 0 ? 0 : 1;
}
