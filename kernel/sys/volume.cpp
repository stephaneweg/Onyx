//
// volume.cpp -- the FatFs volumes as the system sees them (kapi v93, kern/volume.h): the SD
// card's partitions mounted at boot, the USB mass-storage devices' volumes mounted and unmounted
// as they come and go, the list, the eject, the format.
//
// The USB devices and their names (the user's choice, 2026-10-06). Circle names the USB
// mass-storage devices umsd1.. in the order they come (a number freed at an unplug is given to
// the next one); device n (1..3) is umsdn, and its volumes are:
//   - USBn: the device whole, when it has ONE partition or none (a "superfloppy": a file system
//     from sector 0) -- FatFs' auto search (VolToPart {n, 0}: the first FAT volume);
//   - USBnP1 .. USBnP4: its MBR partitions, when it has SEVERAL (VolToPart {n, m}); each FAT /
//     exFAT one mounted, the others listed as unreadable (they can be formatted one by one).
// USB: is an alias of USB1: (as SD0: is of SD:). The device's sector 0 decides (UsbScan). No
// logical partitions, no GPT (FF_LBA64 off). VolPoll, every 100 ms in the input task right after
// Circle's plug-and-play, sees a device appear (it scans and mounts it) or go (it unmounts it).
// All the volumes of a device share one FatFs lock slot (fslock.cpp: one per physical drive).
//
// A device pulled out without an eject. Nothing waits for it: the kernel is not preempted and the
// USB driver waits for its transfers in a busy loop, so Circle deletes the device (inside
// UpdatePlugAndPlay, in the input task) only between two transfers; diskio.cpp's removed handler
// then makes every later transfer fail (RES_NOTRDY), and the FatFs calls in flight end with an
// error. VolPoll takes the device's lock (it waits for the call holding it), closes the open-file
// layer's files of its volumes (their nodes are lost: every call on them -EIO) and unregisters the
// volumes: the other FatFs objects of them (a read handle, a folder, a stream) are invalid from
// then on (FatFs' validate: fs_type 0, or another mount's id -- our fork tests it again once the
// lock is held, ff.c). The programs get errors and go on; the lock is never left held (its holder
// is in a no-kill section and only waits for transfers that fail).
//
// The eject acts on the DEVICE (any of its volumes names it): the open-file layer's written files
// synced (f_sync: the data, the FAT, the entry), the device's own cache flushed (CTRL_SYNC: SCSI
// SYNCHRONIZE CACHE, our fork's usbmassdevice), its volumes unmounted. With files still open it
// refuses (-EBUSY: they are synced, the volumes stay mounted) unless forced.
//
// The format. f_mkfs (FF_USE_MKFS, our fork): USBn: the whole device (an MBR with one partition,
// as Windows does -- whatever partitions it had); USBnPm: that partition only; SD1..SD3 their
// partition of the card. Never SD: -- refused here, whatever the caller.
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
#include <kern/volume.h>
#include <kern/ofile.h>
#include <kern/image.h>
#include <kern/ramfs.h>
#include <kern/uaccess.h>
#include <kern/kapi_abi.h>
#include <circle/devicenameservice.h>
#include <circle/sched/scheduler.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <circle/string.h>
#include <circle/new.h>
#include <fatfs/ff.h>
#include <fatfs/diskio.h>


static const char From[] = "volume";

// the FatFs volume lock (sys/fslock.cpp: re-entrant, its waiters yield; it enters a no-kill section)
void OnyxFsLockTake (int vol);
void OnyxFsLockGive (int vol);

static const char *const s_Names[] = { FF_VOLUME_STRS };	// "SD", "SD1".., "USB1", "USB1P1".., "FD", "NVME"
#define NVOL		(sizeof s_Names / sizeof s_Names[0])
#define VOL_SD		0
#define VOL_USB		4				// USB1 = 4, USB1P1..P4 = 5..8, USB2 = 9...
#define USB_DEVS	3
#define USB_STRIDE	5				// a device's volumes: the whole one + 4 partitions
#define MOUNT_TRIES	3				// a device that does not answer at once: tried again 1 s later
#define LABEL_MAX	36
#define MKFS_WORK	(128 * 1024)			// f_mkfs' buffer (bigger: fewer transfers)

#define ST_NONE		0				// (KAPI_VST_* otherwise)

struct TVolume
{
	FATFS	*pFs;				// registered with FatFs, mounted (0: not)
	unsigned nState;			// ST_NONE (not listed), KAPI_VST_*
	unsigned nFlags;			// KAPI_VF_UNSAFE, KAPI_VF_IOERR
	unsigned nGen;
	char	 Label[LABEL_MAX];
	DWORD	 nSerial;
};

struct TDevice
{
	CDevice	*pDev;				// seen (0: none)
	volatile boolean bGone;			// its removed handler ran
	boolean	 bOp;				// an eject / a mount / a format runs (VolPoll leaves it)
	boolean	 bPending;			// seen, not scanned yet
	unsigned nTries, nNextTry;
};

static TVolume	s_Vol[NVOL];
static TDevice	s_Dev[USB_DEVS];
static FATFS	s_Fs[NVOL];			// the mounts made here (SD: is the kernel's own)
static unsigned	s_nGen;

static boolean IsCardPart (unsigned v)	{ return v >= 1 && v <= 3; }
static int DevOf (unsigned v)		{ return v >= VOL_USB && v < VOL_USB + USB_DEVS * USB_STRIDE ? (int) ((v - VOL_USB) / USB_STRIDE) : -1; }
static unsigned DevVol (int d, unsigned m) { return VOL_USB + (unsigned) d * USB_STRIDE + m; }	// m 0: whole, 1..4: partition m
static boolean IsWhole (unsigned v)	{ return DevOf (v) >= 0 && (v - VOL_USB) % USB_STRIDE == 0; }
static boolean DevThere (int d)		{ return d >= 0 && s_Dev[d].pDev != 0 && !s_Dev[d].bGone; }

static void Root (unsigned v, char *pOut)	// "USB2P1:"
{
	unsigned n = 0;
	for (const char *p = s_Names[v]; *p != '\0' && n < 8; p++) pOut[n++] = *p;
	pOut[n++] = ':'; pOut[n] = '\0';
}

static void DevName (unsigned v, char *pOut, unsigned nCap)	// "emmc1", "umsd2"
{
	int d = DevOf (v);
	if (v <= 3) { strncpy (pOut, "emmc1", nCap); return; }
	if (d >= 0) { CString s; s.Format ("umsd%u", (unsigned) d + 1); strncpy (pOut, s, nCap); pOut[nCap - 1] = '\0'; return; }
	pOut[0] = '\0';
}

static void Bump (unsigned v) { s_Vol[v].nGen = ++s_nGen; }

static unsigned Ticks (void) { return CTimer::Get ()->GetTicks (); }

static void NoKillEnter (void) { if (CScheduler::IsActive ()) CScheduler::Get ()->EnterNoKill (); }
static void NoKillLeave (void) { if (CScheduler::IsActive ()) CScheduler::Get ()->LeaveNoKill (); }

// "USB1", "usb1:", "USB1P2:/a/b", "SD0:" (= SD), "USB" (= USB1) -> the volume, -1 none
static int VolIndex (const char *p)
{
	if (p == 0) return -1;
	char Name[12];
	unsigned n = 0;
	while (p[n] != '\0' && p[n] != ':' && p[n] != '/' && n < sizeof Name - 1)
	{
		char c = p[n];
		Name[n++] = c >= 'a' && c <= 'z' ? (char) (c - 32) : c;
	}
	if (p[n] != '\0' && p[n] != ':' && p[n] != '/') return -1;
	Name[n] = '\0';
	if (strcmp (Name, "SD0") == 0) return VOL_SD;
	if (strcmp (Name, "USB") == 0) return VOL_USB;
	for (unsigned v = 0; v < NVOL; v++) if (strcmp (Name, s_Names[v]) == 0) return (int) v;
	return -1;
}

static int FatErr (FRESULT r)
{
	switch (r)
	{
	case FR_OK:			return 0;
	case FR_NO_FILESYSTEM:		return -KAPI_EINVAL;
	case FR_MKFS_ABORTED:		return -KAPI_ENOSPC;
	case FR_WRITE_PROTECTED:	return -KAPI_EROFS;
	case FR_INVALID_PARAMETER:
	case FR_INVALID_NAME:		return -KAPI_EINVAL;
	case FR_NOT_ENOUGH_CORE:	return -KAPI_ENOMEM;
	case FR_NOT_ENABLED:
	case FR_INVALID_DRIVE:		return -KAPI_ENOENT;
	default:			return -KAPI_EIO;
	}
}

// ---- the FatFs objects kept open outside the open-file layer -----------------------------------------

struct TTracked
{
	FFOBJID	 *pObj;
	boolean	  bWrite;
	TTracked *pNext;
};
static TTracked *s_pTracked;

void VolTrack (FFOBJID *pObj, FIL *pWrite)
{
	if (pObj == 0) return;
	TTracked *t = new TTracked;
	if (t == 0) return;				// (only the eject's count misses it)
	t->pObj = pObj; t->bWrite = pWrite != 0; t->pNext = s_pTracked;
	s_pTracked = t;
}

void VolUntrack (FFOBJID *pObj)
{
	for (TTracked **pp = &s_pTracked; *pp != 0; pp = &(*pp)->pNext)
	{
		if ((*pp)->pObj == pObj)
		{
			TTracked *t = *pp;
			*pp = t->pNext;
			delete t;
			return;
		}
	}
}

// The tracked objects valid on volume v (the mount they were opened on) -> how many
static unsigned TrackedOn (unsigned v)
{
	unsigned n = 0;
	for (TTracked *t = s_pTracked; t != 0; t = t->pNext)
	{
		const FATFS *pFs = t->pObj->fs;
		if (pFs != 0 && pFs->fs_type != 0 && pFs->ldrv == v && t->pObj->id == pFs->id) n++;
	}
	return n;
}

static unsigned OpenOn (unsigned v)
{
	if (s_Vol[v].pFs == 0) return 0;
	return (unsigned) OFileVolume ((int) v, OFV_COUNT) + TrackedOn (v);
}

static unsigned OpenOnDev (int d)
{
	unsigned n = 0;
	for (unsigned m = 0; m < USB_STRIDE; m++) n += OpenOn (DevVol (d, m));
	return n;
}

// ---- mount, unmount ----------------------------------------------------------------------------------

static void ReadLabel (unsigned v)
{
	char R[12]; Root (v, R);
	s_Vol[v].Label[0] = '\0'; s_Vol[v].nSerial = 0;
	if (f_getlabel (R, s_Vol[v].Label, &s_Vol[v].nSerial) != FR_OK) s_Vol[v].Label[0] = '\0';
	s_Vol[v].Label[LABEL_MAX - 1] = '\0';
}

static const char *TypeOf (const FATFS *pFs)
{
	if (pFs == 0) return "";
	switch (pFs->fs_type)
	{
	case FS_FAT12:	return "FAT12";
	case FS_FAT16:	return "FAT16";
	case FS_FAT32:	return "FAT32";
	case FS_EXFAT:	return "exFAT";
	default:	return "";
	}
}

static FRESULT MountVol (unsigned v)
{
	char R[12]; Root (v, R);
	FATFS *pFs = &s_Fs[v];
	FRESULT r = f_mount (pFs, R, 1);
	if (r != FR_OK)
	{
		f_mount (0, R, 0);			// (unregistered: no later call mounts it behind our back)
		s_Vol[v].pFs = 0;
		return r;
	}
	s_Vol[v].pFs = pFs;
	ReadLabel (v);
	s_Vol[v].nState = KAPI_VST_MOUNTED;
	s_Vol[v].nFlags = 0;
	Bump (v);
	return FR_OK;
}

// The device's own cache written (USB: SYNCHRONIZE CACHE)
static void DeviceSync (unsigned v)
{
	disk_ioctl (VolToPart[v].pd, CTRL_SYNC, 0);
}

// The volume unregistered: its open-file layer's files closed (flushed when the device is still
// there) and lost, its programs' images forgotten (another stick may have a file of that name).
static void UnmountVol (unsigned v)
{
	char R[12]; Root (v, R);
	OFileVolume ((int) v, OFV_DROP);		// (node locks first: never while holding the volume's)
	char Path[16]; strcpy (Path, R); strcat (Path, "/");
	ImageFileChanged (Path);
	OnyxFsLockTake ((int) v);			// (waits for the FatFs call in flight)
	if (s_Vol[v].pFs != 0) DeviceSync (v);
	f_mount (0, R, 0);
	s_Vol[v].pFs = 0;
	OnyxFsLockGive ((int) v);
}

void VolMountCard (FATFS *pSdFs)
{
	s_Vol[VOL_SD].pFs = pSdFs;
	s_Vol[VOL_SD].nState = KAPI_VST_MOUNTED;
	ReadLabel (VOL_SD);
	Bump (VOL_SD);
	// the card's other partitions: each FAT / exFAT one mounted as SD1: .. SD3:
	for (unsigned v = 1; v <= 3; v++)
	{
		if (MountVol (v) == FR_OK)
		{
			CLogger::Get ()->Write (From, LogNotice, "SD card partition %u mounted (%s:)", v + 1, s_Names[v]);
		}
	}
}

// ---- the USB devices come and go ----------------------------------------------------------------------

static void DeviceRemoved (CDevice *pDevice, void *pContext)	// (Circle: inside UpdatePlugAndPlay)
{
	((TDevice *) pContext)->bGone = TRUE;
}

// Sector 0 is a FAT / exFAT boot sector (a "superfloppy": no partition table)?
static boolean IsBootSector (const u8 *p)
{
	if (p[510] != 0x55 || p[511] != 0xAA) return FALSE;
	if (p[0] != 0xEB && p[0] != 0xE9 && p[0] != 0xE8) return FALSE;
	return memcmp (p + 3, "EXFAT   ", 8) == 0 || memcmp (p + 82, "FAT32", 5) == 0 || memcmp (p + 54, "FAT", 3) == 0;
}

// The device's layout from its sector 0 -> 0: one volume (USBn: a superfloppy, one partition, none
// -- FatFs' auto search finds it or says there is none), else the bits 1..4 of its MBR partitions
// (USBnPm); -1: sector 0 could not be read.
static int UsbLayout (int d)
{
	unsigned v = DevVol (d, 0);
	BYTE pd = VolToPart[v].pd;
	u8 Sector[FF_MAX_SS] __attribute__ ((aligned (8)));		// (on the stack: two devices may be scanned at once)
	OnyxFsLockTake ((int) v);			// (the device's lock: nobody else reads it meanwhile)
	int nRet = -1;
	if (!(disk_initialize (pd) & STA_NOINIT) && disk_read (pd, Sector, 0, 1) == RES_OK)
	{
		nRet = 0;
		if (!IsBootSector (Sector) && Sector[510] == 0x55 && Sector[511] == 0xAA)
		{
			unsigned nMask = 0, nParts = 0;
			for (unsigned m = 1; m <= 4; m++)
			{
				const u8 *e = Sector + 0x1BE + (m - 1) * 16;
				u32 nStart = e[8] | e[9] << 8 | e[10] << 16 | (u32) e[11] << 24;
				if (e[4] != 0 && nStart != 0) { nMask |= 1u << m; nParts++; }
			}
			if (nParts >= 2) nRet = (int) nMask;
		}
	}
	OnyxFsLockGive ((int) v);
	return nRet;
}

// The device's volumes forgotten (a new device in its place, a format of the whole device)
static void UsbClear (int d)
{
	for (unsigned m = 0; m < USB_STRIDE; m++)
	{
		TVolume &V = s_Vol[DevVol (d, m)];
		V.nState = ST_NONE; V.nFlags = 0; V.Label[0] = '\0';
	}
}

static void UsbGone (int d)
{
	boolean bAny = FALSE;
	for (unsigned m = 0; m < USB_STRIDE; m++)
	{
		unsigned v = DevVol (d, m);
		TVolume &V = s_Vol[v];
		if (V.nState == ST_NONE) continue;
		boolean bMounted = V.pFs != 0;
		if (bMounted)
		{
			UnmountVol (v);
			CLogger::Get ()->Write (From, LogWarning, "%s: removed without an eject (its open files now fail)", s_Names[v]);
			bAny = TRUE;
		}
		V.nState = KAPI_VST_REMOVED;
		V.nFlags = bMounted ? KAPI_VF_UNSAFE : 0;
		V.Label[0] = '\0';
		Bump (v);
	}
	if (!bAny) CLogger::Get ()->Write (From, LogNotice, "USB%d: device removed", d + 1);
	s_Dev[d].pDev = 0;
	s_Dev[d].bGone = FALSE;
	s_Dev[d].bPending = FALSE;
}

static void UsbMountOne (unsigned v, FRESULT r)	// (r: MountVol's) the log, the state if it failed
{
	TVolume &V = s_Vol[v];
	if (r == FR_OK)
	{
		u64 nBytes = (u64) (V.pFs->n_fatent - 2) * V.pFs->csize * FF_MAX_SS;
		CLogger::Get ()->Write (From, LogNotice, "%s: mounted (%s, %u MB%s%s)", s_Names[v], TypeOf (V.pFs),
					(unsigned) (nBytes >> 20), V.Label[0] ? ", " : "", V.Label);
		return;
	}
	V.nState = KAPI_VST_UNREADABLE;
	V.nFlags = r == FR_NO_FILESYSTEM ? 0 : KAPI_VF_IOERR;
	Bump (v);
	CLogger::Get ()->Write (From, LogNotice, "%s: %s", s_Names[v],
				r == FR_NO_FILESYSTEM ? "no FAT / exFAT file system (it can be formatted)" : "could not be read");
}

// The device scanned (its layout) and its volumes mounted -> FALSE: tried again later (no answer)
static boolean UsbScan (int d, boolean bLast)
{
	int nLayout = UsbLayout (d);
	UsbClear (d);
	if (nLayout == 0)
	{
		unsigned v = DevVol (d, 0);
		FRESULT r = MountVol (v);
		if (r != FR_OK && r != FR_NO_FILESYSTEM && !bLast) return FALSE;
		UsbMountOne (v, r);
		return TRUE;
	}
	if (nLayout < 0)
	{
		if (!bLast) return FALSE;
		UsbMountOne (DevVol (d, 0), FR_DISK_ERR);
		return TRUE;
	}
	CLogger::Get ()->Write (From, LogNotice, "USB%d: several partitions: USB%dP1..P4", d + 1, d + 1);
	for (unsigned m = 1; m <= 4; m++)
	{
		if (!(nLayout & (1 << m))) continue;
		unsigned v = DevVol (d, m);
		UsbMountOne (v, MountVol (v));
	}
	return TRUE;
}

void VolPoll (void)
{
	CDeviceNameService *pDNS = CDeviceNameService::Get ();
	for (int d = 0; d < USB_DEVS; d++)
	{
		TDevice &D = s_Dev[d];
		if (D.bOp) continue;				// (an eject / a format: it sees to it)
		if (D.pDev != 0 && D.bGone)
		{
			UsbGone (d);
		}
		if (D.pDev == 0)
		{
			CString Dev; Dev.Format ("umsd%u", (unsigned) d + 1);
			CDevice *pDev = pDNS->GetDevice (Dev, TRUE);
			if (pDev == 0) continue;
			D.pDev = pDev;
			D.bGone = FALSE;
			pDev->RegisterRemovedHandler (DeviceRemoved, &D);
			D.bPending = TRUE;
			D.nTries = 0;
			D.nNextTry = Ticks ();
			UsbClear (d);
			CLogger::Get ()->Write (From, LogNotice, "USB%d: device %s plugged in (%u MB)", d + 1, (const char *) Dev,
						(unsigned) (pDev->GetSize () >> 20));
		}
		if (D.bPending && (int) (Ticks () - D.nNextTry) >= 0)
		{
			D.bOp = TRUE;
			if (UsbScan (d, ++D.nTries >= MOUNT_TRIES)) D.bPending = FALSE;
			else D.nNextTry = Ticks () + HZ;		// (some sticks answer the first reads late)
			D.bOp = FALSE;
		}
	}
}

void VolSyncAll (void)
{
	for (unsigned v = 0; v < NVOL; v++)
	{
		if (s_Vol[v].pFs == 0) continue;
		OFileVolume ((int) v, OFV_SYNC);
		if (DevOf (v) >= 0) DeviceSync (v);
	}
}

// ---- the kapis -------------------------------------------------------------------------------------

static void Fill (unsigned v, struct kapi_volume *e, unsigned nFlags)
{
	TVolume &V = s_Vol[v];
	int d = DevOf (v);
	memset (e, 0, sizeof *e);
	strncpy (e->name, s_Names[v], sizeof e->name - 1);
	e->state = V.nState;
	e->flags = V.nFlags | (v == VOL_SD ? KAPI_VF_SYSTEM : 0) | (d >= 0 ? KAPI_VF_REMOVABLE : 0)
		 | ((IsCardPart (v) || DevThere (d)) ? KAPI_VF_FORMATTABLE : 0);
	e->gen = V.nGen;
	e->open = OpenOn (v);
	e->free = ~0ull;
	DevName (v, e->device, sizeof e->device);
	if (v <= 3 || DevThere (d))
	{
		LBA_t nSectors = 0;
		if (disk_ioctl (VolToPart[v].pd, GET_SECTOR_COUNT, &nSectors) == RES_OK) e->device_size = (u64) nSectors * FF_MAX_SS;
	}
	if (V.pFs != 0)
	{
		e->total = (u64) (V.pFs->n_fatent - 2) * V.pFs->csize * FF_MAX_SS;
		strncpy (e->type, TypeOf (V.pFs), sizeof e->type - 1);
		strncpy (e->label, V.Label, sizeof e->label - 1);
		e->serial = V.nSerial;
		if (nFlags & KAPI_VOLS_ROOM)
		{
			char R[12]; Root (v, R);
			DWORD nFree = 0; FATFS *pFs = 0;
			if (f_getfree (R, &nFree, &pFs) == FR_OK && pFs != 0) e->free = (u64) nFree * pFs->csize * FF_MAX_SS;
		}
	}
}

extern "C" {

int kapi_vol_list (struct kapi_volume *pUserOut, int nMax, unsigned nFlags)
{
	if (nMax < 0 || (nMax > 0 && (pUserOut == 0 || !UserRange (pUserOut, (u64) nMax * sizeof *pUserOut)))) return -KAPI_EFAULT;
	int n = 0;
	for (unsigned v = 0; v < NVOL; v++)
	{
		TVolume &V = s_Vol[v];
		if (V.pFs == 0 && V.nState == ST_NONE) continue;
		if (n < nMax)
		{
			struct kapi_volume e;
			Fill (v, &e, nFlags);
			if (!UserPut (&pUserOut[n], e)) return -KAPI_EFAULT;
		}
		n++;
	}
	if (RamFsMounted ())
	{
		if (n < nMax)
		{
			struct kapi_volume e;
			memset (&e, 0, sizeof e);
			strcpy (e.name, "RAM");
			e.state = KAPI_VST_MOUNTED;
			e.flags = KAPI_VF_RAM;
			strcpy (e.type, "RAM");
			u64 nTotal, nUsed, nFree; unsigned nFiles, nDirs;
			RamFsInfo (&nTotal, &nUsed, &nFree, &nFiles, &nDirs);
			e.total = e.device_size = nTotal;
			e.free = nFree;
			if (!UserPut (&pUserOut[n], e)) return -KAPI_EFAULT;
		}
		n++;
	}
	return n;
}

// Any volume of a USB device names it: all its volumes are synced, unmounted, EJECTED.
int kapi_vol_eject (const char *pUserVol, unsigned nFlags)
{
	CUserStr Vol (pUserVol, 64);
	if (!Vol.OK ()) return -KAPI_EFAULT;
	int v = VolIndex (Vol.Get ());
	if (v < 0) return -KAPI_ENOENT;
	int d = DevOf (v);
	if (d < 0) return -KAPI_EINVAL;			// (the card's volumes: the card is not removed while the Pi runs)
	TDevice &D = s_Dev[d];
	if (D.bOp) return -KAPI_EBUSY;
	boolean bMounted = FALSE, bListed = FALSE, bEjected = FALSE;
	for (unsigned m = 0; m < USB_STRIDE; m++)
	{
		const TVolume &V = s_Vol[DevVol (d, m)];
		bMounted |= V.pFs != 0;
		bListed |= V.nState == KAPI_VST_MOUNTED || V.nState == KAPI_VST_UNREADABLE;
		bEjected |= V.nState == KAPI_VST_EJECTED;
	}
	if (!bListed) return bEjected ? 0 : -KAPI_ENOENT;
	NoKillEnter ();
	D.bOp = TRUE;
	int nRet = 0;
	unsigned nOpen = OpenOnDev (d);
	int nFailed = 0;
	for (unsigned m = 0; m < USB_STRIDE; m++)		// (written files synced, whatever comes next)
	{
		unsigned w = DevVol (d, m);
		if (s_Vol[w].pFs != 0) nFailed += OFileVolume ((int) w, OFV_SYNC);
	}
	if (D.bGone)
	{
		nRet = -KAPI_EIO;			// (pulled out meanwhile: VolPoll sees to it)
	}
	else if (nOpen > 0 && !(nFlags & KAPI_EJECT_FORCE))
	{
		DeviceSync (DevVol (d, 0));
		nRet = -KAPI_EBUSY;
	}
	else
	{
		for (unsigned m = 0; m < USB_STRIDE; m++)
		{
			unsigned w = DevVol (d, m);
			TVolume &V = s_Vol[w];
			if (V.nState != KAPI_VST_MOUNTED && V.nState != KAPI_VST_UNREADABLE) continue;
			if (V.pFs != 0) UnmountVol (w);
			V.nState = KAPI_VST_EJECTED;
			V.nFlags = 0;
			Bump (w);
		}
		if (!bMounted) DeviceSync (DevVol (d, 0));
		CLogger::Get ()->Write (From, LogNotice, "USB%d: ejected%s, it can be removed", d + 1,
					nOpen ? " (files were still open)" : "");
		if (nFailed > 0) nRet = -KAPI_EIO;	// (ejected, but a file could not be written)
	}
	D.bOp = FALSE;
	NoKillLeave ();
	return nRet;
}

// A USB device's volume: the device scanned and mounted again (all its volumes); SD1..SD3: mounted.
int kapi_vol_mount (const char *pUserVol)
{
	CUserStr Vol (pUserVol, 64);
	if (!Vol.OK ()) return -KAPI_EFAULT;
	int v = VolIndex (Vol.Get ());
	if (v < 0) return -KAPI_ENOENT;
	if (s_Vol[v].pFs != 0) return 0;		// (mounted)
	int d = DevOf (v);
	if (IsCardPart (v))
	{
		NoKillEnter ();
		FRESULT r = MountVol (v);
		NoKillLeave ();
		if (r == FR_OK) CLogger::Get ()->Write (From, LogNotice, "%s: mounted again", s_Names[v]);
		return FatErr (r);
	}
	if (!DevThere (d)) return -KAPI_ENODEV;
	TDevice &D = s_Dev[d];
	if (D.bOp) return -KAPI_EBUSY;
	NoKillEnter ();
	D.bOp = TRUE;
	for (unsigned m = 0; m < USB_STRIDE; m++)		// (an eject may have left some mounted: a fresh scan)
	{
		unsigned w = DevVol (d, m);
		if (s_Vol[w].pFs != 0) UnmountVol (w);
	}
	UsbScan (d, TRUE);
	D.bPending = FALSE;
	int nRet = -KAPI_EIO, nAny = 0;
	for (unsigned m = 0; m < USB_STRIDE; m++)
	{
		const TVolume &V = s_Vol[DevVol (d, m)];
		if (V.pFs != 0) nAny++;
		else if (V.nState == KAPI_VST_UNREADABLE && !(V.nFlags & KAPI_VF_IOERR)) nRet = -KAPI_EINVAL;
	}
	if (s_Vol[v].pFs != 0 || nAny > 0) nRet = 0;		// (the name asked may be of the other layout)
	D.bOp = FALSE;
	NoKillLeave ();
	return nRet;
}

// A label FAT takes: at most 11 characters, none of "*+,./:;<=>?[\]| nor a control character
static boolean LabelOK (const char *p)
{
	unsigned n = 0;
	for (; p[n] != '\0'; n++)
	{
		unsigned char c = (unsigned char) p[n];
		if (c < 0x20 || c == 0x7F || strchr ("\"*+,./:;<=>?[\\]|", c) != 0) return FALSE;
	}
	return n <= 11;
}

int kapi_vol_format (const char *pUserVol, const struct kapi_format *pUserFmt)
{
	CUserStr Vol (pUserVol, 64);
	if (!Vol.OK ()) return -KAPI_EFAULT;
	struct kapi_format F;
	if (pUserFmt == 0 || !UserGet (&F, pUserFmt)) return -KAPI_EFAULT;
	F.label[sizeof F.label - 1] = '\0';
	int v = VolIndex (Vol.Get ());
	if (v < 0) return -KAPI_ENOENT;
	int d = DevOf (v);
	// SD: is the system's volume: never, whoever asks. The card's other partitions only when the
	// caller says the user confirmed it (KAPI_FMT_CARD).
	if (v == VOL_SD) return -KAPI_EPERM;
	if (IsCardPart (v) && !(F.flags & KAPI_FMT_CARD)) return -KAPI_EPERM;
	if (!IsCardPart (v) && !DevThere (d)) return -KAPI_ENODEV;
	// a partition of a USB device: one it has now (USBnPm listed); the whole device (USBn): always
	if (d >= 0 && !IsWhole (v) && s_Vol[v].nState == ST_NONE) return -KAPI_ENODEV;
	if (F.fs > KAPI_FMT_EXFAT || !LabelOK (F.label)) return -KAPI_EINVAL;
	if (F.cluster != 0 && ((F.cluster & (F.cluster - 1)) != 0 || F.cluster < 512 || F.cluster > 0x1000000)) return -KAPI_EINVAL;
	if (d >= 0 && s_Dev[d].bOp) return -KAPI_EBUSY;
	unsigned nOpen = d >= 0 && IsWhole (v) ? OpenOnDev (d) : OpenOn (v);
	if (nOpen > 0 && !(F.flags & KAPI_FMT_FORCE)) return -KAPI_EBUSY;

	u8 *pWork = new u8[MKFS_WORK];
	if (pWork == 0) return -KAPI_ENOMEM;
	NoKillEnter ();
	if (d >= 0) s_Dev[d].bOp = TRUE;
	char R[12]; Root (v, R);
	CLogger::Get ()->Write (From, LogNotice, "%s: formatting (%s%s)", s_Names[v],
				F.fs == KAPI_FMT_FAT ? "FAT" : F.fs == KAPI_FMT_FAT32 ? "FAT32" : F.fs == KAPI_FMT_EXFAT ? "exFAT" : "auto",
				d >= 0 && IsWhole (v) ? ", the whole device" : "");
	if (d >= 0 && IsWhole (v))			// (the whole device: every volume it had goes)
	{
		for (unsigned m = 0; m < USB_STRIDE; m++)
		{
			unsigned w = DevVol (d, m);
			if (s_Vol[w].pFs != 0) UnmountVol (w);
		}
		UsbClear (d);
		for (unsigned m = 1; m < USB_STRIDE; m++) Bump (DevVol (d, m));
	}
	else if (s_Vol[v].pFs != 0) UnmountVol (v);
	OnyxFsLockTake (v);				// (the drive's lock: the whole card / device waits meanwhile)
	MKFS_PARM Opt;
	memset (&Opt, 0, sizeof Opt);
	Opt.fmt = F.fs == KAPI_FMT_FAT ? FM_FAT : F.fs == KAPI_FMT_FAT32 ? FM_FAT32 : F.fs == KAPI_FMT_EXFAT ? FM_EXFAT : FM_ANY;
	Opt.n_fat = F.fs == KAPI_FMT_EXFAT ? 1 : 2;	// (two FATs: as Windows writes them)
	Opt.au_size = F.cluster;
	LBA_t nSectors = 0;
	if (disk_ioctl (VolToPart[v].pd, GET_SECTOR_COUNT, &nSectors) == RES_OK && nSectors >= (256u << 11))
	{
		Opt.align = 2048;			// (1 MB: a flash erase block's multiple)
	}
	FRESULT r = f_mkfs (R, &Opt, pWork, MKFS_WORK);
	delete [] pWork;
	boolean bGone = d >= 0 && s_Dev[d].bGone;
	if (r == FR_OK && !bGone) r = MountVol (v);
	if (r == FR_OK && F.label[0] != '\0')
	{
		char Set[64]; strcpy (Set, R); strcat (Set, F.label);
		if (f_setlabel (Set) == FR_OK) ReadLabel (v);
	}
	if (r == FR_OK) DeviceSync (v);
	OnyxFsLockGive (v);
	if (r != FR_OK && !bGone)
	{
		s_Vol[v].nState = d >= 0 ? KAPI_VST_UNREADABLE : ST_NONE;
		s_Vol[v].nFlags = 0;
		Bump (v);
	}
	CLogger::Get ()->Write (From, r == FR_OK ? LogNotice : LogWarning, "%s: format %s (%d)", s_Names[v],
				r == FR_OK ? "done" : "failed", (int) r);
	if (d >= 0) s_Dev[d].bOp = FALSE;
	NoKillLeave ();
	return FatErr (r);
}

}  // extern "C"
