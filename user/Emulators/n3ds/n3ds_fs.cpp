//
// n3ds/n3ds_fs.cpp -- fs:USER, the file service. This slice: what a program needs to start and to read its own
// read-only files -- the RomFS, a piece of the file the program was loaded from, opened as one file (archive 3,
// "SelfNCCH") that the program's library then reads as a file system by itself. A file is a session of its own
// (Read, GetSize, Close). No SD card yet: it is said absent, and the other archives are refused.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

enum : u32 { ARCHIVE_ROMFS = 3, FS_NOT_FOUND = 0xC8804478, FS_NO_ARCHIVE = 0xC8804465 };

// A file open for reading: the session's data and size.
static void fileRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	switch (id)
	{
	case 0x0802:								// Read (offset, size, the buffer) -> bytes read
	{
		const u64 off = (u64) cmd[2] << 32 | cmd[1];
		u32 n = cmd[3]; const u32 dst = cmd[5];
		if (off >= s->size) n = 0; else if (n > s->size - off) n = (u32) (s->size - off);
		if (n && !m->mem.write (dst, s->data + off, n)) n = 0;
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = n;
		break;
	}
	case 0x0804:								// GetSize
		cmd[0] = ipcHeader (id, 3, 0); cmd[1] = RES_OK; cmd[2] = (u32) s->size; cmd[3] = (u32) (s->size >> 32);
		break;
	case 0x0808:								// Close
	case 0x0809:								// Flush
	case 0x080A:								// SetPriority
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x080B:								// GetPriority
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x080C:								// OpenLinkFile -> another session on the same file
	{
		Session *f = new Session (fileRequest, "file"); f->data = s->data; f->size = s->size;
		u32 h = m->handleNew (f);
		cmd[0] = ipcHeader (id, 1, 2); cmd[1] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; cmd[2] = 0x10; cmd[3] = h;
		break;
	}
	default:
		m->note ("file command %08x", (unsigned) cmd[0]);
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_NOT_IMPLEMENTED;
		break;
	}
}

static void openRomfs (Machine *m, u32 id, u32 *cmd)
{
	if (!m->romfs) { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = FS_NOT_FOUND; return; }
	Session *f = new Session (fileRequest, "file"); f->data = m->romfs; f->size = m->romfsSize;
	u32 h = m->handleNew (f);
	cmd[0] = ipcHeader (id, 1, 2); cmd[1] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; cmd[2] = 0x10; cmd[3] = h;
}

void fsRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	switch (id)
	{
	case 0x0801:								// Initialize
	case 0x0861:								// InitializeWithSdkVersion
	case 0x0862:								// SetPriority
	case 0x080E:								// CloseArchive
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x0863:								// GetPriority
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x0817:								// IsSdmcDetected -> no
	case 0x0818:								// IsSdmcWritable -> no
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x0803:								// OpenFileDirectly (-, archive, its path, the file's path, flags...)
		if (cmd[2] == ARCHIVE_ROMFS) openRomfs (m, id, cmd);
		else { m->note ("fs:USER archive %08x", (unsigned) cmd[2]); cmd[0] = ipcHeader (id, 1, 0); cmd[1] = FS_NO_ARCHIVE; }
		break;
	case 0x080C:								// OpenArchive (archive, its path) -> a 64-bit handle
		if (cmd[1] == ARCHIVE_ROMFS && m->romfs) { cmd[0] = ipcHeader (id, 3, 0); cmd[1] = RES_OK; cmd[2] = ARCHIVE_ROMFS; cmd[3] = 0; }
		else { m->note ("fs:USER archive %08x", (unsigned) cmd[1]); cmd[0] = ipcHeader (id, 1, 0); cmd[1] = FS_NO_ARCHIVE; }
		break;
	case 0x0802:								// OpenFile (-, archive handle, the path, flags...)
		if (cmd[2] == ARCHIVE_ROMFS && cmd[3] == 0) openRomfs (m, id, cmd);
		else { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = FS_NOT_FOUND; }
		break;
	default:
		m->note ("%s command %08x", s->name, (unsigned) cmd[0]);
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_NOT_IMPLEMENTED;
		break;
	}
}

}
