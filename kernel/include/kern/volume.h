//
// volume.h -- the FatFs volumes as the system sees them (kapi v91): the SD card's partitions
// (SD:, SD1:..SD3:, mounted at boot) and the USB mass-storage volumes (USB:, USB2:, USB3: --
// Circle's umsd1..umsd3), mounted when a stick is plugged in and unmounted when it is ejected
// or pulled out; the list, the eject, the format (docs/02 "Volumes").
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#ifndef _kern_volume_h
#define _kern_volume_h

#include <circle/types.h>
#include <fatfs/ff.h>

// Boot (CKernel::Initialize): SD: was mounted by the kernel (pFs); the card's partitions 2..4 are
// mounted here as SD1:..SD3: when they hold a FAT / exFAT file system.
void VolMountCard (FATFS *pSdFs);

// Every 100 ms, from the kernel's input task right after Circle's USB plug-and-play: a stick
// plugged in is mounted, one pulled out unmounted (its open files then fail with -EIO).
void VolPoll (void);

// The FatFs objects the kernel keeps open outside the open-file layer (kern/ofile.h has its own):
// kapi_open's read handles, kapi_opendir's folders, the file streams, kapi_save. Tracked so that an
// eject finds them (busy, synced). pWrite: the FIL when it is written (synced at the eject), else
// 0. No yield, no I/O; VolUntrack may run in the reaper's teardown (interrupts masked).
void VolTrack (FFOBJID *pObj, FIL *pWrite);
void VolUntrack (FFOBJID *pObj);

// The session's end (kapi_shutdown): every file open for writing synced, the USB caches flushed.
void VolSyncAll (void);

// ---- shared with sys/ofile.cpp (defined there) ----------------------------------------------------

#define OFV_COUNT	0		// -> the open files on the volume
#define OFV_SYNC	1		// each written one synced -> the failures
#define OFV_DROP	2		// each closed (flushed if it can), its node lost: every call -EIO
int OFileVolume (int nVol, int nOp);

#endif
