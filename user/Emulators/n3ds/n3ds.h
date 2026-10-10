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
	VA_DSP_RAM     = 0x1FF00000,		// the sound processor's memory (0x80000 bytes; shared structures at +0x50000, +0x70000)
	DSP_RAM_SIZE   = 0x00080000,
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
struct Storage;

// Where a program comes from: a file the host reads pieces of (a game is hundreds of MB: only its code is loaded,
// its RomFS is read as the game asks).
struct Source
{
	void *user;
	u64 size;
	bool (*read) (void *user, u64 offset, void *dst, u32 n);
};

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
enum { HANDLE_MAX = 1024, WAIT_MAX = 16, TIMER_MAX = 32, STORAGE_MAX = 512, ARCHIVES_MAX = 16 };
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
	// (n3ds_fs.cpp) a file of the RomFS: the piece of the program's source it reads; a stored file or a folder being
	// listed: its full name (and how many entries were given)
	u64 base, size; int kind; char *path;
	Session (ServiceFn f, const char *n) : Object (OBJ_SESSION), fn (f), name (n), base (0), size (0), kind (0), path (0) {}
	~Session () override;
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
// A frame for the host's GPU (Machine::gpuDraw). The numbers are the Onyx kernel's (kern/kapi_abi.h: kapi_gpu_batch2,
// gpu_program, gpu_texture): a program is a V3D fragment shader of `nVary` varyings (user/Libs/v3d/picatev; the
// vertex and coordinate shaders are the stock ones for 4 + nVary floats a vertex), a texture is pixels r g b a
// (bytes, the first row at the top) that changed when its serial did.
struct GpuProgramInfo { const unsigned long long *words; u32 nWords, nVary, flags; };
struct GpuTextureInfo { const u32 *px; u32 w, h, serial; };
struct GpuBatch
{
	u32 program;				// an index in GpuFrame::programs
	u32 off, count, stride;			// `count` vertices of `stride` floats from float `off`: x y z w (clip space of the whole target), the varyings
	u32 flags, blend, wmask;		// KAPI_GPU_B_ZFUNC / NOZWRITE; KAPI_GPU_BLEND2 or 0; the channels not written
	int scissor[4];				// x, y (from the top), w, h
	u32 uni, nUni;				// its fragment uniforms in GpuFrame::u
	int tex[3]; u32 texFlags[3]; int texUni[3];	// each lookup's texture (an index in GpuFrame::textures), its wrapping, where its two words go (-1: none)
};
struct GpuFrame
{
	int w, h;
	const float *v; u32 nFloats;
	const GpuBatch *b; u32 nb;
	const u32 *u; u32 nu;
	const GpuProgramInfo *programs; u32 nPrograms;
	const GpuTextureInfo *textures; u32 nTextures;
};

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
		u64 usLists, usTransfers, usFills;	// ... and the host's time in each, in microseconds
		u64 usRaster, usRasterAside;		// the rasterizer's time; of it, what ran while the program went on (not in the lists' time)
		bool listPending;			// a list's triangles are still being drawn: its interrupt is owed
		// the right eye left out (monoOnly): the top screen's (left, right) framebuffers seen, the buffer the
		// program renders an eye in and where it was last sent (1 left, 2 right), how many frames in a row
		// showed "left, then right", and: its draws are being left out now (until its transfer to the right)
		u32 eyePairs[4][2]; int eyePairCount;
		u32 eyeSrc; int eyeLastSide, eyeSeen, eyeBroken; bool eyeSkip;
		u64 eyeDraws;				// (draws left out)
		u64 hostTransfers, usHostTransfers;	// the transfers read from the host's pixels, their time
		u64 usRead, usShade, usAssemble;	// (of the lists' time, in the draws of many vertices: their attributes read, shaded, their triangles assembled)
		u64 shaderChecked, shaderWrong;		// (shaderCheck: vertices run by both the compiled shader and the interpreter; the ones that differed)
		u64 gpuFrames, softFrames, usGpu, skippedFrames;	// the targets' frames drawn by the host's GPU, by the software renderer; the host's time
		u64 vertices, trianglesDrawn, pixelsDrawn, shaderSteps, trianglesIn;	// (shaderSteps: the vertex shader's instructions run; trianglesIn: before clipping and culling)
	} gsp;
	struct Pica *pica;			// the GPU's state (n3ds_pica.cpp), made at its first command list
	// APT (n3ds_apt.cpp): the application's life -- its events, the parameter the system sends it (the wake-up)
	struct Apt { Mutex *lock; Event *signal, *param; bool pending; u32 cpuLimit; SharedMem *font; bool fontReady; } apt;
	// HID (n3ds_hid.cpp): the buttons, the circle pad and the touch screen, in a shared page
	struct Hid { SharedMem *shared; Event *events[5]; u32 buttons; s16 cpadX, cpadY; bool touch; u16 touchX, touchY; u32 padIndex, touchIndex; } hid;
	// the sound processor (n3ds_dsp.cpp): its memory, the pipe's answer, the events of each audio frame
	struct Dsp
	{
		u8 *ram; bool on; u64 nextTick, frames; u16 pipe[16]; u32 pipeLen, pipePos; Event *interrupt, *semaphore;
		struct DspVoice *voices;		// the 24 sources being played (n3ds_dsp.cpp)
		// what was mixed, for the host: stereo, 32728 samples a second (audioRead takes it)
		enum { OUT_FRAMES = 16384 };
		s16 *out; u32 outRead, outWrite;
		u64 mixed;				// (frames mixed with a source playing: is there sound at all?)
	} dsp;
	// The sound mixed since the last call: up to `frames` stereo frames (16 bits, AUDIO_RATE a second) -> how many.
	enum { AUDIO_RATE = 32728 };
	int audioRead (s16 *dst, int frames);
	// the console's user (cfg): the name (UTF-16), the language (0 Japanese, 1 English, 2 French, 3 German, 4 Italian,
	// 5 Spanish...), the region (0 Japan, 1 USA, 2 Europe)
	struct User { u16 name[11]; u8 language, region; } user;
	// what the program writes -- save data, extra data, the SD card -- (n3ds_fs.cpp): files in memory, stored by the host
	Storage *storage; bool storageDirty;
	char archives[ARCHIVES_MAX][40];	// the archives the program opened: their roots' names
	// the program's source, and in it its read-only files (RomFS: romfsSize 0 = none)
	Source source;
	u64 romfsBase, romfsSize;
	const u8 *memFile; u32 memSize;		// (load (file, size): the source is that memory)
	char title[16]; char productCode[20];	// (a game's: from its headers)
	// The GPU's rasterizer on other host cores (all three, or none: this thread alone then). fn (arg, worker) takes
	// the work piece by piece until none is left; it makes no system call and allocates nothing (an Onyx app
	// core can do neither); a worker's number is its own, below 8.
	//   parallelBegin  the host's helpers start fn (true; false: there is none), and this comes back at once
	//   parallelDone   have they all come back? (then all the work is done)
	//   parallelEnd    this thread takes its part too (fn, with its own number), then waits for the helpers
	// A command list's triangles are rasterized that way while the program goes on: its "list done" interrupt is
	// raised when they are (gpuSync: at once when the program waits for it, or before the GPU's next command).
	bool (*parallelBegin) (void *user, void (*fn) (void *arg, int worker), void *arg);
	bool (*parallelDone) (void *user);
	void (*parallelEnd) (void *user, void (*fn) (void *arg, int worker), void *arg);
	void *parallelUser;
	int helpers;				// (how many the host has: to say it)
	// A host that shows one picture a screen (no stereo screen) sets this: when the program draws its top screen
	// twice a frame -- the left eye's picture, then the right eye's, each sent to its own framebuffer --, the
	// second one is not drawn (n3ds_gsp.cpp: learnt from the transfers, checked again each frame).
	bool monoOnly;
	// A host with a GPU (Onyx's V3D) sets this: a render target's frame -- its draws from one display transfer to
	// the next -- is given whole to gpuDraw as GPU batches when every draw of it could be put so (GpuFrame below),
	// and the pixels it gives back (w x h, 0xAARRGGBB, the first row at the top) are put into the program's
	// buffer; false, or a frame with a draw the GPU's interface has not: the software renderer draws it.
	// (skipDraw, with gpuDraw: a render target's frame that begins while it is set is not drawn, nor transferred --
	// the screen keeps its picture; the program and its sound go on at their pace. The host sets it when it is late.)
	bool skipDraw;
	bool (*gpuDraw) (void *user, const struct GpuFrame *f, u32 *pixels);
	void *gpuUser;
	void (*gpuSoft) (void *user, const u32 *pixels, int w, int h);	// (tests: a frame gpuDraw refused, as the software renderer drew it)
	void gpuSync ();			// the GPU's work under way is ended (call it before reading a screen)
	bool shaderCheck;			// (tests: every vertex through the compiled shader and the interpreter, compared)
	bool trace;				// the system calls and the requests, on stderr (tests)
	bool traceGpu;				// ... each draw and transfer of the GPU
	u32 gpuSkip;				// (to find what is slow: 1 no procedural texture, 2 no blending, 4 no depth, 8 no texture, 16 no lighting, 32 no pixel at all, 64 the fragments found but not shaded, 128 not combined nor written, 256 no vertex shader, 512 no triangle)
	// what the program says (svcOutputDebugString): the host's
	void (*debugOut) (void *user, const char *text, u32 len);
	void *debugUser;
	// counters (tests, the speed display)
	u64 svcCount; u64 switchCount; u32 unknownSvcs;

	Machine ();
	~Machine ();
	bool init ();
	// (n3ds_loader.cpp) an ELF, a .3dsx, or a game (.3ds / .cci: NCSD, .cxi: NCCH -- decrypted). The source must
	// stay readable while the machine lives (the RomFS is read from it).
	bool loadFrom (const Source &src);
	bool load (const u8 *file, u32 size);	// (a file in memory, kept by the caller)
	bool loadNcch (u64 at);
	bool loadElf (const u8 *file, u32 size);
	bool load3dsx (const u8 *file, u32 size);
	void setUser (const char *name, int language, int region);
	// What the program wrote, as one block (the host keeps it in a file beside the game): storageExport gives the
	// size it needs (and fills dst when it is big enough); storageImport before the program runs.
	u32 storageExport (u8 *dst, u32 cap) const;
	bool storageImport (const u8 *src, u32 size);
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
// the vertex shaders compiled for the host's processor (n3ds_shjit.cpp)
typedef void (*ShaderCode) (float *regs, const float *uniforms, const void *consts, const u8 *ints, u32 bools);
struct ShaderJit;
ShaderCode shaderCompile (ShaderJit **jit, const u32 *code, const u32 *opdesc, u32 entry);
void shaderJitReset (ShaderJit *jit);
const void *shaderJitConsts (const ShaderJit *jit);
void picaBeforeFill (Machine *m, u32 physStart, u32 physEnd);	// memory is about to be filled: what is queued for it is drawn first
bool picaBusy (const Machine *m);		// a list's triangles are being drawn aside
bool picaDone (Machine *m);			// ... and the helpers have finished them
void picaSync (Machine *m);			// ... this thread helps, waits, and the list is over
void picaFree (Machine *m);
void storageFree (Machine *m);
void aptRequest (Machine *m, Session *s, u32 *cmd);			// APT:U / APT:S / APT:A (n3ds_apt.cpp)
void hidRequest (Machine *m, Session *s, u32 *cmd);			// hid:USER / hid:SPVR (n3ds_hid.cpp)
void hidUpdate (Machine *m);						// (each frame: the input into the shared page)
void fsRequest (Machine *m, Session *s, u32 *cmd);			// fs:USER (n3ds_fs.cpp)
void dspRequest (Machine *m, Session *s, u32 *cmd);			// dsp::DSP (n3ds_dsp.cpp)
void dspFrame (Machine *m);						// (an audio frame has passed)
void cfgRequest (Machine *m, Session *s, u32 *cmd);			// cfg:u / cfg:s / cfg:i (n3ds_cfg.cpp)
void ptmRequest (Machine *m, Session *s, u32 *cmd);			// ptm:u / ptm:sysm

}

#endif
