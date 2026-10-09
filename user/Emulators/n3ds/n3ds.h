//
// n3ds/n3ds.h -- a Nintendo 3DS emulator core for Onyx (docs/3DS-EMULATOR-STUDY.md, option C): the Old 3DS's
// application processor (ARM11 MPCore, ARMv6K + VFPv2) run by Dynarmic behind our own interface (Cpu), and the
// console's operating system emulated at a high level -- the process's memory, the kernel's objects and system
// calls, the services (to come). No Nintendo key, firmware or system file: decrypted dumps and homebrew only.
//
// Phase T1: the memory (n3ds_mem.cpp), the processor (n3ds_cpu.cpp: the only file that sees Dynarmic), the
// kernel -- threads, events, mutexes, semaphores, timers, address arbiters, shared memory, waiting, time -- and
// its system calls (n3ds_kernel.cpp), the services a program reaches through `srv:` (n3ds_ipc.cpp), the graphics
// service gsp::Gpu and the two screens (n3ds_gsp.cpp), an ELF loader (n3ds_loader.cpp). The core uses the C
// library (Dynarmic needs it anyway) but nothing of Onyx: it runs on the PC too (tools/tests/n3ds).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef ONYX_N3DS_H
#define ONYX_N3DS_H

#include <stdint.h>
#include <stddef.h>

namespace n3ds {

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;

enum { PAGE_BITS = 12, PAGE_SIZE = 1 << PAGE_BITS, PAGE_COUNT = 1 << (32 - PAGE_BITS) };
enum { PERM_R = 1, PERM_W = 2, PERM_X = 4, PERM_RW = 3, PERM_RX = 5 };

// The process's address space (the Old 3DS's layout).
enum : u32 {
	VA_CODE        = 0x00100000,		// the program
	VA_HEAP        = 0x08000000,		// ControlMemory's ordinary heap
	VA_HEAP_END    = 0x10000000,		// (the main thread's stack ends here too)
	VA_LINEAR      = 0x14000000,		// the linear heap: FCRAM in order, what the GPU is given
	VA_LINEAR_END  = 0x1C000000,
	VA_VRAM        = 0x1F000000,
	VA_CONFIG      = 0x1FF80000,		// the kernel's configuration page
	VA_SHARED      = 0x1FF81000,		// the shared page (time, 3D slider...)
	VA_TLS         = 0x1FF82000,		// the threads' local storage, 0x200 bytes each
	VA_FONT        = 0x18000000,		// the shared system font (inside the linear range: FCRAM past the application's 64 MB)
	FONT_FCRAM     = 0x04000000,		// ... its place in FCRAM
	FONT_SIZE      = 0x00332000,
	APP_LINEAR_MAX = 0x04000000,		// the application's share of FCRAM (the linear heap never passes it)
	FCRAM_SIZE     = 0x08000000,		// 128 MB
	VRAM_SIZE      = 0x00600000,
	PA_FCRAM       = 0x20000000,
	PA_VRAM        = 0x18000000,
	TLS_SIZE       = 0x200,
	TLS_MAX        = 0x40,			// threads at most (0x8000 bytes of TLS)
};

// The processor's clock: SVC times are in nanoseconds, ours in its ticks.
static const u64 TICKS_PER_SECOND = 268111856ull;
static const u64 TICKS_PER_FRAME  = 4481136ull;			// (59.83 frames a second)

// Result codes (the ones the kernel gives).
enum : u32 {
	RES_OK              = 0,
	RES_TIMEOUT         = 0x09401BFE,
	RES_INVALID_HANDLE  = 0xD8E007F7,
	RES_INVALID_ADDRESS = 0xE0E01BF5,
	RES_INVALID_ARG     = 0xE0E01BEE,		// (invalid combination / size)
	RES_OUT_OF_MEMORY   = 0xD86007F3,
	RES_OUT_OF_HANDLES  = 0xD8600413,
	RES_OUT_OF_RANGE    = 0xE0E01BFD,
	RES_NOT_OWNER       = 0xD8E0041F,		// (ReleaseMutex by another thread)
	RES_PORT_NOT_FOUND  = 0xD88007FA,
	RES_NOT_IMPLEMENTED = 0xF8C007F4,
	RES_NO_SERVICE      = 0xD8E06406,		// (srv: no such service)
};

// The screens: the top one 400 x 240, the bottom one 320 x 240 (the touch screen).
enum { SCREEN_TOP = 0, SCREEN_BOTTOM = 1, TOP_W = 400, BOTTOM_W = 320, SCREEN_H = 240 };
// The graphics interrupts a program is told (gsp::Gpu's queue).
enum { GSP_PSC0 = 0, GSP_PSC1, GSP_PDC0, GSP_PDC1, GSP_PPF, GSP_P3D, GSP_DMA };
// The buttons (hid's bits).
enum : u32 {
	BTN_A = 1, BTN_B = 2, BTN_SELECT = 4, BTN_START = 8, BTN_RIGHT = 0x10, BTN_LEFT = 0x20, BTN_UP = 0x40, BTN_DOWN = 0x80,
	BTN_R = 0x100, BTN_L = 0x200, BTN_X = 0x400, BTN_Y = 0x800, BTN_TOUCH = 0x100000,
	BTN_CPAD_RIGHT = 0x10000000, BTN_CPAD_LEFT = 0x20000000, BTN_CPAD_UP = 0x40000000, BTN_CPAD_DOWN = 0x80000000,
};

struct Machine;
struct Pica;

// ---- memory ----------------------------------------------------------------------------------------------------------
struct Memory
{
	u8 **pages;				// [PAGE_COUNT] the host's page of each guest page, or 0 (Dynarmic reads it too)
	u8 *perms;				// [PAGE_COUNT] PERM_*
	u8 *fcram, *vram;
	u32 linearUsed;				// FCRAM taken from its start (the linear heap: va - VA_LINEAR = its offset)
	u32 topUsed;				// ... and from its end (the program, its heap, stacks, TLS)

	bool init ();
	void quit ();
	u8 *allocTop (u32 size);		// zeroed pages from FCRAM's end, 0 when it is full
	bool map (u32 va, u32 size, u8 *host, int perm);
	void unmap (u32 va, u32 size);
	bool mapped (u32 va, u32 size) const;
	inline u8 *ptr (u32 va) const { u8 *p = pages[va >> PAGE_BITS]; return p ? p + (va & (PAGE_SIZE - 1)) : 0; }
	u8 r8 (u32 va) const; u16 r16 (u32 va) const; u32 r32 (u32 va) const; u64 r64 (u32 va) const;
	void w8 (u32 va, u8 v); void w16 (u32 va, u16 v); void w32 (u32 va, u32 v); void w64 (u32 va, u64 v);
	bool read (u32 va, void *dst, u32 n) const;
	bool write (u32 va, const void *src, u32 n);
	bool fill (u32 va, u8 v, u32 n);
	u32 faults;				// accesses outside the mapped pages (read as 0, writes dropped)
	u32 faultAddr;
};

// ---- the processor ---------------------------------------------------------------------------------------------------
struct CpuState
{
	u32 r[16];
	u32 cpsr;
	u32 vfp[64];				// d0-d31 as words (the 3DS has d0-d15)
	u32 fpscr;
	u32 tls;				// the thread's TLS address (CP15 c13, c0, 3: read by every program)
};

struct Cpu
{
	static Cpu *create (Machine *m);	// (n3ds_cpu.cpp: Dynarmic)
	virtual ~Cpu () {}
	virtual void run () = 0;		// until the machine's ticks run out, or halt ()
	virtual void halt () = 0;		// (from a system call: leave run () now)
	virtual u32 *regs () = 0;		// the running thread's r0-r15, valid inside a system call
	virtual void save (CpuState &s) = 0;
	virtual void load (const CpuState &s) = 0;
	virtual void invalidate (u32 va, u32 size) = 0;		// the code there changed
};

// ---- the kernel's objects --------------------------------------------------------------------------------------------
enum { OBJ_THREAD = 1, OBJ_EVENT, OBJ_MUTEX, OBJ_SEMAPHORE, OBJ_ARBITER, OBJ_PROCESS, OBJ_TIMER, OBJ_SHMEM, OBJ_SESSION };
enum { RESET_ONESHOT = 0, RESET_STICKY = 1, RESET_PULSE = 2 };
enum { THREAD_READY, THREAD_WAIT_SLEEP, THREAD_WAIT_SYNC, THREAD_WAIT_ARBITER, THREAD_DEAD };
enum { HANDLE_MAX = 1024, WAIT_MAX = 16, TIMER_MAX = 32 };
enum : u32 { HANDLE_CUR_THREAD = 0xFFFF8000, HANDLE_CUR_PROCESS = 0xFFFF8001 };

struct Thread;

struct Object
{
	int type;
	int refs;
	Object (int t) : type (t), refs (1) {}
	virtual ~Object () {}
	virtual bool waitable () const { return false; }
	virtual bool available (const Thread *) const { return false; }	// would a wait on it end now, for this thread?
	virtual void acquire (Thread *) {}				// ... and what ending it takes
};

struct Event : Object
{
	int reset; bool signaled;
	Event (int r) : Object (OBJ_EVENT), reset (r), signaled (false) {}
	bool waitable () const override { return true; }
	bool available (const Thread *) const override { return signaled; }
	void acquire (Thread *) override { if (reset != RESET_STICKY) signaled = false; }
};

struct Mutex : Object
{
	Thread *owner; int count;
	Mutex () : Object (OBJ_MUTEX), owner (0), count (0) {}
	bool waitable () const override { return true; }
	bool available (const Thread *t) const override { return !owner || owner == t; }
	void acquire (Thread *t) override { owner = t; count++; }
};

struct Semaphore : Object
{
	s32 count, max;
	Semaphore (s32 c, s32 m) : Object (OBJ_SEMAPHORE), count (c), max (m) {}
	bool waitable () const override { return true; }
	bool available (const Thread *) const override { return count > 0; }
	void acquire (Thread *) override { count--; }
};

// A timer: signalled when its time comes (and again every interval), waited for as an event is.
struct Timer : Object
{
	Machine *m;
	int reset; bool signaled;
	u64 fireTick, interval;			// (fireTick ~0: not set)
	Timer (Machine *machine, int r);
	~Timer () override;
	bool waitable () const override { return true; }
	bool available (const Thread *) const override { return signaled; }
	void acquire (Thread *) override { if (reset != RESET_STICKY) signaled = false; }
};

// A block of memory several address ranges (or the system and the program) show.
struct SharedMem : Object
{
	u8 *host; u32 size;
	u32 va;					// where it goes when the program maps it "at 0" (the system's own blocks), or 0
	SharedMem (u8 *h, u32 n, u32 at = 0) : Object (OBJ_SHMEM), host (h), size (n), va (at) {}
};

// A program's end of a connection to a service: a request (the command buffer in the thread's TLS) is answered
// at once by the service's function.
struct Session;
typedef void (*ServiceFn) (Machine *m, Session *s, u32 *cmd);
struct Session : Object
{
	ServiceFn fn; const char *name;
	const u8 *data; u64 size;		// (a file's session: what it reads -- n3ds_fs.cpp)
	Session (ServiceFn f, const char *n) : Object (OBJ_SESSION), fn (f), name (n), data (0), size (0) {}
};

struct Arbiter : Object { Arbiter () : Object (OBJ_ARBITER) {} };
struct Process : Object { Process () : Object (OBJ_PROCESS) {} };

struct Thread : Object
{
	u32 id;
	int status;
	s32 priority;				// 0 the highest, 0x3F the lowest
	u64 readySeq;				// the order threads of one priority run in
	u64 wakeTick;				// a sleep's or a timed wait's end (~0: none)
	Object *waitOn[WAIT_MAX]; int waitCount; bool waitAll;
	u32 waitOut;				// (WaitSynchronizationN: where the index goes -- r1)
	u32 arbiterAddr;
	int tlsSlot;
	CpuState ctx;
	Thread () : Object (OBJ_THREAD), id (0), status (THREAD_READY), priority (0x30), readySeq (0), wakeTick (~0ull),
		    waitCount (0), waitAll (false), waitOut (0), arbiterAddr (0), tlsSlot (-1) {}
	bool waitable () const override { return true; }
	bool available (const Thread *) const override { return status == THREAD_DEAD; }
};

// ---- the machine -----------------------------------------------------------------------------------------------------
struct Machine
{
	Memory mem;
	Cpu *cpu;
	// time
	u64 now;				// ticks since the start
	s64 ticksLeft;				// of the slice the processor runs (the Cpu counts it down)
	// the kernel
	Object *handles[HANDLE_MAX];
	Thread *threads[TLS_MAX]; int threadCount;
	Thread *current;
	u32 nextThreadId; u64 nextSeq;
	bool resched;				// a system call changed who should run
	bool exited; u32 exitCode;
	u32 heapSize;				// the ordinary heap's committed bytes (from VA_HEAP)
	u32 entry;
	char lastError[160];
	char notes[256];			// what is not emulated that the program asked for (not an error by itself)
	Timer *timers[TIMER_MAX]; int timerCount;		// (the timers that exist: no reference held)
	// gsp::Gpu and the screens (n3ds_gsp.cpp)
	struct Framebuffer { u32 active, left, right, stride, format, select, unknown; bool set; };
	struct Gsp
	{
		Event *irq;				// the program's event, signalled at each interrupt
		SharedMem *shared;			// the interrupt queue, the framebuffers' updates, the command queue
		Framebuffer fb[2];
		u64 frames;				// VBlanks since the start
		u32 fills, transfers, cmdLists;		// (counters: what the program asked the GPU)
	} gsp;
	struct Pica *pica;			// the GPU's state (n3ds_pica.cpp), made at its first command list
	// APT (n3ds_apt.cpp): the application's life -- its events, the parameter the system sends it (the wake-up)
	struct Apt { Mutex *lock; Event *signal, *param; bool pending; u32 cpuLimit; SharedMem *font; bool fontReady; } apt;
	// HID (n3ds_hid.cpp): the buttons, the circle pad and the touch screen, in a shared page
	struct Hid { SharedMem *shared; Event *events[5]; u32 buttons; s16 cpadX, cpadY; bool touch; u16 touchX, touchY; u32 padIndex, touchIndex; } hid;
	// the console's user (cfg): the name (UTF-16), the language (0 Japanese, 1 English, 2 French, 3 German, 4 Italian,
	// 5 Spanish...), the region (0 Japan, 1 USA, 2 Europe)
	struct User { u16 name[11]; u8 language, region; } user;
	// the program's read-only files (RomFS): a piece of the file it was loaded from (kept by the host)
	const u8 *romfs; u32 romfsSize;
	bool trace;				// the system calls and the requests, on stderr (tests)
	// what the program says (svcOutputDebugString): the host's
	void (*debugOut) (void *user, const char *text, u32 len);
	void *debugUser;
	// counters (tests, the speed display)
	u64 svcCount; u64 switchCount; u32 unknownSvcs;

	Machine ();
	~Machine ();
	bool init ();
	// (n3ds_loader.cpp) an ELF or a .3dsx; the file must stay in memory while the machine lives (its RomFS)
	bool load (const u8 *file, u32 size);
	bool loadElf (const u8 *file, u32 size);
	bool load3dsx (const u8 *file, u32 size);
	void setUser (const char *name, int language, int region);
	// The shared system font, a BCFNT file (ours: tools/n3ds/mkfont.py -> data/sysfont.bcfnt; never Nintendo's
	// unless the user gives the dump of their own console's). Before the program runs. false: not a font.
	bool setSharedFont (const u8 *bcfnt, u32 size);
	// what the player does: BTN_* held, the circle pad (-156..156), the touch screen (pixels of the bottom screen)
	void setInput (u32 buttons, int cpadX, int cpadY, bool touch, int touchX, int touchY);
	bool start (u32 entryPoint, u32 stackSize);			// the main thread
	void run (u64 ticks);						// the scheduler: runs the threads for that long
	void runFrame () { run (TICKS_PER_FRAME); vblank (); }
	// the picture of a screen: TOP_W or BOTTOM_W by SCREEN_H pixels, 0x00RRGGBB
	void screenImage (int screen, u32 *dst) const;

	// the kernel (n3ds_kernel.cpp)
	void svc (u32 n);						// called by the Cpu
	u32 handleNew (Object *o);					// (takes the caller's reference; 0: the table is full)
	Object *handleGet (u32 h, int type = 0);
	bool handleClose (u32 h);
	void release (Object *o);
	Thread *threadNew (u32 entryPoint, u32 arg, u32 stackTop, s32 priority);
	void wakeWaiters (Object *o);
	void fail (const char *fmt, ...);
	void note (const char *fmt, ...);
	// services (n3ds_ipc.cpp, n3ds_gsp.cpp)
	Session *serviceOpen (const char *name);			// 0: no such service
	void vblank ();
	void gspInterrupt (int id);
	u8 *physPtr (u32 pa, u32 size) const;				// FCRAM or VRAM, 0 outside
	static u32 virtToPhys (u32 va);

private:
	Thread *pick ();
	void switchTo (Thread *t);
	void threadExit (Thread *t);
	bool tryWait (Thread *t, bool first);
	u64 nsToTicks (s64 ns) const;
	u32 svcControlMemory (u32 op, u32 addr0, u32 addr1, u32 size, u32 perm, u32 *out);
};

// ---- services --------------------------------------------------------------------------------------------------------
// A request's first word: the command, how many plain words follow, how many words of handles / buffers after them.
// The answer goes in the same buffer: its header, the result in cmd[1].
inline u32 ipcHeader (u32 command, u32 normal, u32 translate) { return command << 16 | normal << 6 | translate; }
// A command that is not emulated: noted, and answered "done" (what lets a program go on most often).
void ipcStub (Machine *m, const char *service, u32 *cmd);
void srvRequest (Machine *m, Session *s, u32 *cmd);			// srv: (n3ds_ipc.cpp)
void gspRequest (Machine *m, Session *s, u32 *cmd);			// gsp::Gpu (n3ds_gsp.cpp)
// the GPU (n3ds_pica.cpp): a command list at a program's address; a display transfer (the GX command's 8 words)
void picaCommandList (Machine *m, u32 va, u32 size);
void picaDisplayTransfer (Machine *m, const u32 *c);
void picaFree (Machine *m);
void aptRequest (Machine *m, Session *s, u32 *cmd);			// APT:U / APT:S / APT:A (n3ds_apt.cpp)
void hidRequest (Machine *m, Session *s, u32 *cmd);			// hid:USER / hid:SPVR (n3ds_hid.cpp)
void hidUpdate (Machine *m);						// (each frame: the input into the shared page)
void fsRequest (Machine *m, Session *s, u32 *cmd);			// fs:USER (n3ds_fs.cpp)
void cfgRequest (Machine *m, Session *s, u32 *cmd);			// cfg:u / cfg:s / cfg:i (n3ds_cfg.cpp)
void ptmRequest (Machine *m, Session *s, u32 *cmd);			// ptm:u / ptm:sysm

}

#endif
