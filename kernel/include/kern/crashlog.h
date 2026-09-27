//
// kern/crashlog.h -- what the Pi was doing when it froze or panicked, kept across a reboot.
//
// 64 KB of RAM kept out of the heap (circle/lib/memory64.cpp, g_ulOnyxCrashArea: the top of
// the >= 3 GB low RAM) hold, for the current session: the tail of the kernel log, the last
// places the primary core was interrupted at (its PC, LR, task, from every IRQ), a few
// breadcrumbs (what the GPU and the compositor were doing) and a panic's registers. The
// hardware watchdog (cmdline.txt hangreboot=, seconds, default 15, 0 = off), fed by the
// reaper, restarts a frozen Pi; at the next boot a session that did not end with
// shutdown / reboot is written to SD:/etc/lastcrash.txt (and noted in kmsg).
// Best effort: nothing when the board has no RAM above 3 GB, or the RAM did not survive.
//
#ifndef _kern_crashlog_h
#define _kern_crashlog_h

#include <circle/types.h>

struct TTrapFrame;

// breadcrumbs
#define CRUMB_V3D		0	// 0 idle, 1 clipping, 2 binning, 3 rendering, 4 texture upload
#define CRUMB_PRESENT		1	// 0 idle, 1 composing, 2 display DMA, 3 display DMA wait
#define CRUMB_THROTTLED		2	// the firmware's GET_THROTTLED bits
#define CRUMB_TEMP		3	// the SoC's temperature, degrees C
#define CRUMB_TEMP_MAX		4
#define CRUMB_COUNT		8

void CrashLogInit (void);				// at boot, before the first log line is kept
void CrashLogReport (void);				// once SD: is mounted: SD:/etc/lastcrash.txt
void CrashLogText (const void *pText, size_t nCount);	// (the logger's output, CLogSwitch)
void CrashLogSample (const TTrapFrame *pFrame);		// (core 0, each IRQ)
void CrashLogAlive (void);				// (the reaper, 20 times a second: uptime + watchdog)
void CrashLogCrumb (unsigned nIndex, u32 nValue);
void CrashLogPower (void);
void CrashLogClockRestore (void);			// (boot, SD: mounted: SD:/etc/clock until NTP)
void CrashLogClockSave (void);				// (every 10 minutes, shutdown / reboot)				// (once a second: throttling + temperature -> kmsg, record)
void CrashLogPanic (const char *pLine);			// before the panic screen
void CrashLogCleanEnd (void);				// shutdown / reboot: stops the watchdog

void CrashLogStartWatchdog (unsigned nSeconds);		// (from the reaper's start; 0 = off)

// core 1 (the sound core) watches core 0: CrashLogCoreInit once, CrashLogCoreCheck in its loop.
// When the reaper has not run for 10 s, it writes the report into SD:/etc/crashdump.txt's
// sectors (raw, through the SD device) and restarts the Pi; the next boot keeps it as
// SD:/etc/lastcrash.txt.
// The system is stuck although the scheduler runs (the compositor without a frame for 12 s):
// the same report, written by core 1 while core 0 waits with its IRQs masked, then a restart.
void CrashLogRequest (const char *pReason);
void CrashLogCoreInit (void);
void CrashLogCoreCheck (void);
extern volatile boolean g_bCrashDumping;		// (the SD driver never yields then)

#endif
