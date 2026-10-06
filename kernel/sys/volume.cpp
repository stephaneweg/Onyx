//
// volume.cpp -- the FatFs volumes as the system sees them (kapi v92, kern/volume.h): the SD
// card's partitions mounted at boot, the USB mass-storage volumes mounted and unmounted as
// sticks come and go, the list, the eject, the format.
//
// The USB volumes. Circle names the USB mass-storage devices umsd1.. in the order they come (a
// number freed at an unplug is given to the next one); our FatFs volumes USB:, USB2:, USB3: are
// umsd1..umsd3, each the first FAT / exFAT volume of its device (a "superfloppy", or the first FAT
// partition of its MBR: FatFs' auto search, ffconf.h / diskio.cpp's VolToPart). VolPoll, every
// 100 ms in the input task right after Circle's plug-and-play, sees a device appear (it mounts
// it) or go (it unmounts it).
//
// A stick pulled out without an eject. Nothing waits for it: the kernel is not preempted and the
// USB driver waits for its transfers in a busy loop, so Circle deletes the device (inside
// UpdatePlugAndPlay, in the input task) only between two transfers; diskio.cpp's removed handler
// then makes every later transfer fail (RES_NOTRDY), and the FatFs calls in flight end with an
// error. VolPoll takes the volume's lock (it waits for the call holding it), closes the open-file
// layer's files of the volume (their nodes are lost: every call on them -EIO) and unregisters the
// volume: the other FatFs objects of it (a read handle, a folder, a stream) are invalid from then
// on (FatFs' validate: fs_type 0, or another mount's id -- our fork tests it again once the lock
// is held, ff.c). The programs get errors and go on; the volume lock is never left held (its
// holder is in a no-kill section and only waits for transfers that fail).
//
// The eject. The open-file layer's written files synced (f_sync: the data, the FAT, the entry),
// the device's own cache flushed (CTRL_SYNC: SCSI SYNCHRONIZE CACHE, our fork's usbmassdevice),
// the volume unmounted. With files still open it refuses (-EBUSY: they are synced, the volume
// stays mounted) unless forced.
//
// The format. f_mkfs (FF_USE_MKFS, our fork): on a USB volume the whole device (an MBR with one
// partition, as Windows does), on SD1..SD3 their partition of the card. Never SD: -- refused here,
// whatever the caller.
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

static const char *const s_Names[] = { FF_VOLUME_STRS };	// "SD", "SD1".. "USB", "USB2", "USB3", "FD", "NVME"
#define NVOL		(sizeof s_Names / sizeof s_Names[0])
#define VOL_SD		0
#define VOL_USB		4				// USB:, USB2:, USB3: = 4, 5, 6
#define USB_SLOTS	3
#define MOUNT_TRIES	3				// a device that does not answer at once: tried again 1 s later
#define LABEL_MAX	36
#define MKFS_WORK	(128 * 1024)			// f_mkfs' buffer (bigger: fewer transfers)

#define ST_NONE		0				// (KAPI_VST_* otherwise)
#define ST_NEW		100				// a device seen, not mounted yet

struct TVolume
{
	FATFS	*pFs;				// registered with FatFs, mounted (0: not)
	unsigned nState;			// ST_NONE, ST_NEW, KAPI_VST_*
	unsigned nFlags;			// KAPI_VF_UNSAFE, KAPI_VF_IOERR
	unsigned nGen;
	boolean	 bOp;				// an eject / a mount / a format runs (VolPoll leaves it)
	char	 Label[LABEL_MAX];
	DWORD	 nSerial;
	// a USB volume's device
	CDevice	*pDev;				// seen (0: none)
	volatile boolean bGone;			// its removed handler ran
	unsigned nTries, nNextTry;
};

static TVolume	s_Vol[NVOL];
static FATFS	s_Fs[NVOL];			// the mounts made here (SD: is the kernel's own)
static unsigned	s_nGen;

static boolean IsUsb (unsigned v)	{ return v >= VOL_USB && v < VOL_USB + USB_SLOTS; }
static boolean IsCardPart (unsigned v)	{ return v >= 1 && v <= 3; }

static void Root (unsigned v, char *pOut)	// "USB2:"
{
	unsigned n = 0;
	for (const char *p = s_Names[v]; *p != '\0' && n < 8; p++) pOut[n++] = *p;
	pOut[n++] = ':'; pOut[n] = '\0';
}

static void DevName (unsigned v, char *pOut, unsigned nCap)	// "emmc1", "umsd2"
{
	if (v <= 3) { strncpy (pOut, "emmc1", nCap); return; }
	if (IsUsb (v)) { CString s; s.Format ("umsd%u", v - VOL_USB + 1); strncpy (pOut, s, nCap); pOut[nCap - 1] = '\0'; return; }
	pOut[0] = '\0';
}

static void Bump (unsigned v) { s_Vol[v].nGen = ++s_nGen; }

static unsigned Ticks (void) { return CTimer::Get ()->GetTicks (); }

static void NoKillEnter (void) { if (CScheduler::IsActive ()) CScheduler::Get ()->EnterNoKill (); }
static void NoKillLeave (void) { if (CScheduler::IsActive ()) CScheduler::Get ()->LeaveNoKill (); }

// "USB", "usb:", "USB:/a/b", "SD0:" (= SD), "USB1" (= USB) -> the volume, -1 none
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
	if (strcmp (Name, "USB1") == 0) return VOL_USB;
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
	((TVolume *) pContext)->bGone = TRUE;
}

static void UsbGone (unsigned v)
{
	TVolume &V = s_Vol[v];
	boolean bMounted = V.pFs != 0;
	if (bMounted)
	{
		UnmountVol (v);
		CLogger::Get ()->Write (From, LogWarning, "%s: removed without an eject (its open files now fail)", s_Names[v]);
	}
	else
	{
		CLogger::Get ()->Write (From, LogNotice, "%s: device removed", s_Names[v]);
	}
	V.pDev = 0;
	V.bGone = FALSE;
	V.nState = KAPI_VST_REMOVED;
	V.nFlags = bMounted ? KAPI_VF_UNSAFE : 0;
	V.Label[0] = '\0';
	Bump (v);
}

static void UsbTryMount (unsigned v)
{
	TVolume &V = s_Vol[v];
	FRESULT r = MountVol (v);
	if (r == FR_OK)
	{
		u64 nBytes = (u64) (V.pFs->n_fatent - 2) * V.pFs->csize * FF_MAX_SS;
		CLogger::Get ()->Write (From, LogNotice, "%s: mounted (%s, %u MB%s%s)", s_Names[v], TypeOf (V.pFs),
					(unsigned) (nBytes >> 20), V.Label[0] ? ", " : "", V.Label);
		return;
	}
	if (r != FR_NO_FILESYSTEM && ++V.nTries < MOUNT_TRIES)
	{
		V.nNextTry = Ticks () + HZ;		// (some sticks answer the first reads late)
		return;
	}
	V.nState = KAPI_VST_UNREADABLE;
	V.nFlags = r == FR_NO_FILESYSTEM ? 0 : KAPI_VF_IOERR;
	Bump (v);
	CLogger::Get ()->Write (From, LogNotice, "%s: %s", s_Names[v],
				r == FR_NO_FILESYSTEM ? "no FAT / exFAT file system (it can be formatted)" : "the device could not be read");
}

void VolPoll (void)
{
	CDeviceNameService *pDNS = CDeviceNameService::Get ();
	for (unsigned i = 0; i < USB_SLOTS; i++)
	{
		unsigned v = VOL_USB + i;
		TVolume &V = s_Vol[v];
		if (V.bOp) continue;				// (an eject / a format: it sees to it)
		if (V.pDev != 0 && V.bGone)
		{
			UsbGone (v);
		}
		if (V.pDev == 0)
		{
			char Dev[12]; DevName (v, Dev, sizeof Dev);
			CDevice *pDev = pDNS->GetDevice (Dev, TRUE);
			if (pDev == 0) continue;
			V.pDev = pDev;
			V.bGone = FALSE;
			pDev->RegisterRemovedHandler (DeviceRemoved, &V);
			V.nState = ST_NEW;
			V.nFlags = 0;
			V.nTries = 0;
			V.nNextTry = Ticks ();
			CLogger::Get ()->Write (From, LogNotice, "%s: device %s plugged in (%u MB)", s_Names[v], Dev,
						(unsigned) (pDev->GetSize () >> 20));
		}
		if (V.nState == ST_NEW && (int) (Ticks () - V.nNextTry) >= 0)
		{
			V.bOp = TRUE;
			UsbTryMount (v);
			V.bOp = FALSE;
		}
	}
}

void VolSyncAll (void)
{
	for (unsigned v = 0; v < NVOL; v++)
	{
		if (s_Vol[v].pFs == 0) continue;
		OFileVolume ((int) v, OFV_SYNC);
		if (IsUsb (v)) DeviceSync (v);
	}
}

// ---- the kapis -------------------------------------------------------------------------------------

static void Fill (unsigned v, struct kapi_volume *e, unsigned nFlags)
{
	TVolume &V = s_Vol[v];
	memset (e, 0, sizeof *e);
	strncpy (e->name, s_Names[v], sizeof e->name - 1);
	e->state = V.nState == ST_NEW ? KAPI_VST_UNREADABLE : V.nState;
	e->flags = V.nFlags | (v == VOL_SD ? KAPI_VF_SYSTEM : 0) | (IsUsb (v) ? KAPI_VF_REMOVABLE : 0)
		 | ((IsCardPart (v) || (IsUsb (v) && V.pDev != 0)) ? KAPI_VF_FORMATTABLE : 0);
	e->gen = V.nGen;
	e->open = OpenOn (v);
	e->free = ~0ull;
	DevName (v, e->device, sizeof e->device);
	if (v <= 3 || (IsUsb (v) && V.pDev != 0 && !V.bGone))
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

int kapi_vol_eject (const char *pUserVol, unsigned nFlags)
{
	CUserStr Vol (pUserVol, 64);
	if (!Vol.OK ()) return -KAPI_EFAULT;
	int v = VolIndex (Vol.Get ());
	if (v < 0) return -KAPI_ENOENT;
	TVolume &V = s_Vol[v];
	if (!IsUsb (v)) return -KAPI_EINVAL;		// (the card's volumes: the card is not removed while the Pi runs)
	if (V.bOp) return -KAPI_EBUSY;
	if (V.pFs == 0) return V.nState == KAPI_VST_EJECTED ? 0 : -KAPI_ENOENT;
	NoKillEnter ();
	V.bOp = TRUE;
	int nRet = 0;
	unsigned nOpen = OpenOn (v);
	int nFailed = OFileVolume (v, OFV_SYNC);	// (written files synced, whatever comes next)
	if (V.bGone)
	{
		nRet = -KAPI_EIO;			// (pulled out meanwhile: VolPoll sees to it)
	}
	else if (nOpen > 0 && !(nFlags & KAPI_EJECT_FORCE))
	{
		DeviceSync (v);
		nRet = -KAPI_EBUSY;
	}
	else
	{
		UnmountVol (v);
		V.nState = KAPI_VST_EJECTED;
		V.nFlags = 0;
		Bump (v);
		CLogger::Get ()->Write (From, LogNotice, "%s: ejected%s, it can be removed", s_Names[v],
					nOpen ? " (files were still open)" : "");
		if (nFailed > 0) nRet = -KAPI_EIO;	// (ejected, but a file could not be written)
	}
	V.bOp = FALSE;
	NoKillLeave ();
	return nRet;
}

int kapi_vol_mount (const char *pUserVol)
{
	CUserStr Vol (pUserVol, 64);
	if (!Vol.OK ()) return -KAPI_EFAULT;
	int v = VolIndex (Vol.Get ());
	if (v < 0) return -KAPI_ENOENT;
	TVolume &V = s_Vol[v];
	if (V.pFs != 0) return 0;			// (mounted)
	if (!(IsCardPart (v) || (IsUsb (v) && V.pDev != 0 && !V.bGone))) return -KAPI_ENODEV;
	if (V.bOp) return -KAPI_EBUSY;
	NoKillEnter ();
	V.bOp = TRUE;
	FRESULT r = MountVol (v);
	if (r != FR_OK && IsUsb (v) && V.nState != KAPI_VST_UNREADABLE)
	{
		V.nState = KAPI_VST_UNREADABLE;
		V.nFlags = r == FR_NO_FILESYSTEM ? 0 : KAPI_VF_IOERR;
		Bump (v);
	}
	if (r == FR_OK) CLogger::Get ()->Write (From, LogNotice, "%s: mounted again", s_Names[v]);
	V.bOp = FALSE;
	NoKillLeave ();
	return FatErr (r);
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
	TVolume &V = s_Vol[v];
	// SD: is the system's volume: never, whoever asks. The card's other partitions only when the
	// caller says the user confirmed it (KAPI_FMT_CARD).
	if (v == VOL_SD) return -KAPI_EPERM;
	if (IsCardPart (v) && !(F.flags & KAPI_FMT_CARD)) return -KAPI_EPERM;
	if (!IsCardPart (v) && !(IsUsb (v) && V.pDev != 0 && !V.bGone)) return -KAPI_ENODEV;
	if (F.fs > KAPI_FMT_EXFAT || !LabelOK (F.label)) return -KAPI_EINVAL;
	if (F.cluster != 0 && ((F.cluster & (F.cluster - 1)) != 0 || F.cluster < 512 || F.cluster > 0x1000000)) return -KAPI_EINVAL;
	if (V.bOp) return -KAPI_EBUSY;
	if (OpenOn (v) > 0 && !(F.flags & KAPI_FMT_FORCE)) return -KAPI_EBUSY;

	u8 *pWork = new u8[MKFS_WORK];
	if (pWork == 0) return -KAPI_ENOMEM;
	NoKillEnter ();
	V.bOp = TRUE;
	char R[12]; Root (v, R);
	CLogger::Get ()->Write (From, LogNotice, "%s: formatting (%s)", s_Names[v],
				F.fs == KAPI_FMT_FAT ? "FAT" : F.fs == KAPI_FMT_FAT32 ? "FAT32" : F.fs == KAPI_FMT_EXFAT ? "exFAT" : "auto");
	if (V.pFs != 0) UnmountVol (v);
	OnyxFsLockTake (v);				// (the card's partitions: the whole card waits meanwhile)
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
	if (r == FR_OK && !V.bGone) r = MountVol (v);
	if (r == FR_OK && F.label[0] != '\0')
	{
		char Set[64]; strcpy (Set, R); strcat (Set, F.label);
		if (f_setlabel (Set) == FR_OK) ReadLabel (v);
	}
	if (r == FR_OK) DeviceSync (v);
	OnyxFsLockGive (v);
	if (r != FR_OK && !V.bGone)
	{
		V.nState = IsUsb (v) ? KAPI_VST_UNREADABLE : ST_NONE;
		V.nFlags = 0;
		Bump (v);
	}
	CLogger::Get ()->Write (From, r == FR_OK ? LogNotice : LogWarning, "%s: format %s (%d)", s_Names[v],
				r == FR_OK ? "done" : "failed", (int) r);
	V.bOp = FALSE;
	NoKillLeave ();
	return FatErr (r);
}

}  // extern "C"
