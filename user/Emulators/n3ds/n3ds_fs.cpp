//
// n3ds/n3ds_fs.cpp -- fs:USER, the file service, and what the program's files are kept in.
//   The RomFS -- the program's read-only files -- is a piece of the file the program was loaded from, opened as
//   one file (archive 3, "SelfNCCH") that the program's library reads as a file system by itself.
//   What a program WRITES -- its save data (archive 4), its extra data (6, 7), the SD card (9) -- lives in a
//   small file system in memory: a list of files and folders by their full name ("save:/a/b", "ext0000154E:/...",
//   "sd:/..."). The host stores it whole beside the game (Machine::storageExport / storageImport, when
//   storageDirty) and gives it back at the next start.
// Sessions: a file (Read, Write, GetSize, SetSize, Close, Flush), a folder being listed (Read, Close).
// A game's save data must be "formatted" before it opens (FormatSaveData): a game that finds none does it.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

enum : u32
{
	ARCHIVE_ROMFS = 3, ARCHIVE_SAVE = 4, ARCHIVE_EXT = 6, ARCHIVE_SHARED_EXT = 7, ARCHIVE_SDMC = 9, ARCHIVE_SDMC_WRITE = 10,
	FS_NOT_FOUND      = 0xC8804478,		// no such file or folder
	FS_ALREADY_THERE  = 0xC82044BE,
	FS_NOT_FORMATTED  = 0xC8A04554,		// the save data, or shared extra data, was never made
	FS_NO_EXT_DATA    = 0xC8A04478,		// extra data that does not exist
	FS_NO_ARCHIVE     = 0xC8804465,
	FS_FULL           = 0xD86044D2,
};
enum { KIND_ROMFS = 1, KIND_FILE = 2, KIND_DIR = 3 };

// ---- the files in memory ---------------------------------------------------------------------------------------------
struct Storage
{
	struct Node { char *name; bool dir; u8 *data; u32 size, cap; };
	Node nodes[STORAGE_MAX]; int count;
};

static Storage *storage (Machine *m)
{
	if (!m->storage) m->storage = (Storage *) calloc (1, sizeof (Storage));
	return m->storage;
}
void storageFree (Machine *m)
{
	if (!m->storage) return;
	for (int i = 0; i < m->storage->count; i++) { free (m->storage->nodes[i].name); free (m->storage->nodes[i].data); }
	free (m->storage); m->storage = 0;
}
static int nodeFind (Storage *st, const char *name)
{
	for (int i = 0; i < st->count; i++) if (strcmp (st->nodes[i].name, name) == 0) return i;
	return -1;
}
static int nodeAdd (Storage *st, const char *name, bool dir)
{
	if (st->count >= (int) STORAGE_MAX) return -1;
	Storage::Node &n = st->nodes[st->count];
	memset (&n, 0, sizeof n);
	n.name = (char *) malloc (strlen (name) + 1);
	if (!n.name) return -1;
	strcpy (n.name, name); n.dir = dir;
	return st->count++;
}
static void nodeRemove (Storage *st, int i)
{
	free (st->nodes[i].name); free (st->nodes[i].data);
	for (int k = i; k + 1 < st->count; k++) st->nodes[k] = st->nodes[k + 1];
	st->count--;
}
static bool nodeResize (Storage::Node &n, u32 size)
{
	if (size > 0x04000000) return false;
	if (size > n.cap)
	{
		const u32 cap = size < 0x1000 ? 0x1000 : size + size / 2;
		u8 *d = (u8 *) realloc (n.data, cap);
		if (!d) return false;
		memset (d + n.cap, 0, cap - n.cap);
		n.data = d; n.cap = cap;
	}
	else if (size < n.size) memset (n.data + size, 0, n.size - size);
	n.size = size;
	return true;
}
// Is `parent` there? ("x:" itself is, once its archive exists: its root is a node "x:")
static bool parentThere (Storage *st, const char *name)
{
	char p[300];
	const char *slash = strrchr (name, '/');
	if (!slash) return true;
	size_t n = (size_t) (slash - name);
	if (n >= sizeof p) return false;
	memcpy (p, name, n); p[n] = 0;
	const int i = nodeFind (st, p);
	return i >= 0 && st->nodes[i].dir;
}

// The whole store as one block: "N3FS", the count, then for each: the name's length, the name, folder or not,
// the size, the data.
u32 Machine::storageExport (u8 *dst, u32 cap) const
{
	if (!storage) return 0;
	u32 need = 8;
	for (int i = 0; i < storage->count; i++) need += 2 + (u32) strlen (storage->nodes[i].name) + 1 + 4 + storage->nodes[i].size;
	if (!dst || cap < need) return need;
	u8 *p = dst;
	memcpy (p, "N3FS", 4); p += 4;
	const u32 n = (u32) storage->count; memcpy (p, &n, 4); p += 4;
	for (int i = 0; i < storage->count; i++)
	{
		const Storage::Node &nd = storage->nodes[i];
		const u16 len = (u16) strlen (nd.name);
		memcpy (p, &len, 2); p += 2; memcpy (p, nd.name, len); p += len;
		*p++ = nd.dir ? 1 : 0;
		memcpy (p, &nd.size, 4); p += 4;
		if (nd.size) memcpy (p, nd.data, nd.size);
		p += nd.size;
	}
	return need;
}
bool Machine::storageImport (const u8 *src, u32 size)
{
	if (size < 8 || memcmp (src, "N3FS", 4) != 0) return false;
	Storage *st = n3ds::storage (this);
	if (!st) return false;
	while (st->count) nodeRemove (st, st->count - 1);
	u32 n; memcpy (&n, src + 4, 4);
	const u8 *p = src + 8, *end = src + size;
	for (u32 i = 0; i < n; i++)
	{
		if (end - p < 2) return false;
		u16 len; memcpy (&len, p, 2); p += 2;
		if (len >= 300 || end - p < (long) len + 5) return false;
		char name[300]; memcpy (name, p, len); name[len] = 0; p += len;
		const bool dir = *p++ != 0;
		u32 sz; memcpy (&sz, p, 4); p += 4;
		if ((u32) (end - p) < sz) return false;
		const int k = nodeAdd (st, name, dir);
		if (k < 0) return false;
		if (sz) { if (!nodeResize (st->nodes[k], sz)) return false; memcpy (st->nodes[k].data, p, sz); }
		p += sz;
	}
	storageDirty = false;
	return true;
}

// ---- names -----------------------------------------------------------------------------------------------------------
// An archive's handle (given to the program) is its number in this list + 1; its root's name.
static const char *archiveRoot (Machine *m, u32 handle)
{
	return handle >= 1 && handle <= ARCHIVES_MAX && m->archives[handle - 1][0] ? m->archives[handle - 1] : 0;
}
static u32 archiveOpen (Machine *m, const char *root)
{
	for (u32 i = 0; i < ARCHIVES_MAX; i++) if (strcmp (m->archives[i], root) == 0) return i + 1;
	for (u32 i = 0; i < ARCHIVES_MAX; i++) if (!m->archives[i][0] && strlen (root) < sizeof m->archives[i]) { strcpy (m->archives[i], root); return i + 1; }
	return 0;
}

// A path the program gives: its type (1 nothing, 2 binary, 3 text, 4 UTF-16), its size, where it is.
static bool pathText (Machine *m, u32 type, u32 size, u32 va, char *out, u32 cap)
{
	out[0] = 0;
	if (type == 1 || !size) return true;
	if (type != 3 && type != 4) return false;
	u32 n = 0;
	for (u32 i = 0; i < size && n + 1 < cap; i += type == 4 ? 2 : 1)
	{
		const u32 c = type == 4 ? m->mem.r16 (va + i) : m->mem.r8 (va + i);
		if (!c) break;
		out[n++] = c < 128 ? (char) c : '_';
	}
	out[n] = 0;
	while (n > 1 && out[n - 1] == '/') out[--n] = 0;			// ("/dir/" is "/dir")
	return true;
}
// root + path -> the node's full name ("save:" + "/a" = "save:/a"; the root itself for "/" or nothing)
static bool fullName (Machine *m, u32 archive, u32 type, u32 size, u32 va, char *out, u32 cap)
{
	const char *root = archiveRoot (m, archive);
	char path[280];
	if (!root || !pathText (m, type, size, va, path, sizeof path)) return false;
	if (!strcmp (path, "/")) path[0] = 0;
	snprintf (out, cap, "%s%s%s", root, path[0] && path[0] != '/' ? "/" : "", path);
	return true;
}

// The extra data's name from its binary path: the media type, the id's two words.
static void extRoot (Machine *m, u32 va, u32 size, bool shared, char *out, u32 cap)
{
	const u32 lo = size >= 8 ? m->mem.r32 (va + 4) : 0, hi = size >= 12 ? m->mem.r32 (va + 8) : 0;
	snprintf (out, cap, "%s%08X%08X:", shared ? "shared" : "ext", (unsigned) hi, (unsigned) lo);
}

// ---- sessions --------------------------------------------------------------------------------------------------------
static void fileRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	Storage *st = m->storage;
	Storage::Node *node = 0;
	if (s->kind == KIND_FILE)						// (found again by its name: the list moves when a file is deleted)
	{
		const int i = st && s->path ? nodeFind (st, s->path) : -1;
		if (i < 0) { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = FS_NOT_FOUND; return; }
		node = &st->nodes[i];
	}
	switch (id)
	{
	case 0x0802:								// Read (offset, size, the buffer) -> bytes read
	{
		const u64 off = (u64) cmd[2] << 32 | cmd[1];
		const u64 size = node ? node->size : s->size;
		u32 n = cmd[3]; const u32 dst = cmd[5];
		if (off >= size) n = 0; else if (n > size - off) n = (u32) (size - off);
		u32 done = 0;
		if (node) { if (n && m->mem.write (dst, node->data + off, n)) done = n; }
		else
		{
			static u8 buf[0x10000];
			while (done < n)						// (through a buffer: the program's pages are not one piece)
			{
				const u32 k = n - done < sizeof buf ? n - done : (u32) sizeof buf;
				if (!m->source.read || !m->source.read (m->source.user, s->base + off + done, buf, k) || !m->mem.write (dst + done, buf, k)) break;
				done += k;
			}
		}
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = done;
		break;
	}
	case 0x0803:								// Write (offset, size, flags, the buffer) -> bytes written
	{
		const u64 off = (u64) cmd[2] << 32 | cmd[1];
		const u32 n = cmd[3], src = cmd[6];
		if (!node || off + n > 0x04000000) { cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_NOT_IMPLEMENTED; cmd[2] = 0; break; }
		if (off + n > node->size && !nodeResize (*node, (u32) (off + n))) { cmd[0] = ipcHeader (id, 2, 0); cmd[1] = FS_FULL; cmd[2] = 0; break; }
		const bool ok = !n || m->mem.read (src, node->data + off, n);
		m->storageDirty = true;
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = ok ? n : 0;
		break;
	}
	case 0x0804:								// GetSize
	{
		const u64 size = node ? node->size : s->size;
		cmd[0] = ipcHeader (id, 3, 0); cmd[1] = RES_OK; cmd[2] = (u32) size; cmd[3] = (u32) (size >> 32);
		break;
	}
	case 0x0805:								// SetSize (size)
		if (!node) { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_NOT_IMPLEMENTED; break; }
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = cmd[2] == 0 && nodeResize (*node, cmd[1]) ? (u32) RES_OK : (u32) FS_FULL;
		m->storageDirty = true;
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
		Session *f = new Session (fileRequest, "file"); f->base = s->base; f->size = s->size; f->kind = s->kind;
		if (s->path) { f->path = (char *) malloc (strlen (s->path) + 1); if (f->path) strcpy (f->path, s->path); }
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

// A folder being listed: the entries are the nodes right under it; s->base is how many were given.
static void dirRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	if (id == 0x0801)							// Read (count, the buffer) -> entries (0x228 bytes each)
	{
		const u32 want = cmd[1], dst = cmd[3];
		Storage *st = m->storage;
		const size_t plen = s->path ? strlen (s->path) : 0;
		u32 given = 0, seen = 0;
		for (int i = 0; st && i < st->count && given < want; i++)
		{
			const Storage::Node &n = st->nodes[i];
			if (strncmp (n.name, s->path, plen) != 0 || n.name[plen] != '/' || strchr (n.name + plen + 1, '/')) continue;
			if (seen++ < s->base) continue;
			u8 e[0x228]; memset (e, 0, sizeof e);
			const char *leaf = n.name + plen + 1;
			for (u32 k = 0; leaf[k] && k < 261; k++) e[k * 2] = (u8) leaf[k];
			for (u32 k = 0; k < 8; k++) e[0x20C + k] = leaf[k] && leaf[k] != '.' && k < strlen (leaf) ? (u8) leaf[k] : ' ';
			e[0x21A] = 1; e[0x21C] = n.dir ? 1 : 0;
			memcpy (e + 0x220, &n.size, 4);
			m->mem.write (dst + given * 0x228, e, sizeof e);
			given++;
		}
		s->base += given;
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = given;
		return;
	}
	cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;				// Close, SetPriority
}

static void openRomfs (Machine *m, u32 id, u32 *cmd)
{
	if (!m->romfsSize) { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = FS_NOT_FOUND; return; }
	Session *f = new Session (fileRequest, "file"); f->base = m->romfsBase; f->size = m->romfsSize; f->kind = KIND_ROMFS;
	u32 h = m->handleNew (f);
	cmd[0] = ipcHeader (id, 1, 2); cmd[1] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; cmd[2] = 0x10; cmd[3] = h;
}

// Opens (or, asked, makes) the file `name` of the store.
static void openStored (Machine *m, u32 id, u32 *cmd, const char *name, u32 flags)
{
	Storage *st = storage (m);
	int i = st ? nodeFind (st, name) : -1;
	if (i < 0 && (flags & 4) && st && parentThere (st, name)) { i = nodeAdd (st, name, false); m->storageDirty = true; }	// "create"
	if (i < 0 || st->nodes[i].dir) { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = FS_NOT_FOUND; return; }
	Session *f = new Session (fileRequest, "file"); f->kind = KIND_FILE;
	f->path = (char *) malloc (strlen (name) + 1);
	if (f->path) strcpy (f->path, name);
	u32 h = m->handleNew (f);
	cmd[0] = ipcHeader (id, 1, 2); cmd[1] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; cmd[2] = 0x10; cmd[3] = h;
}

// The root an archive number and its path name; *made: must it exist already (its root node), and the error if not.
static u32 archiveName (Machine *m, u32 archive, u32 pathVa, u32 pathSize, char *root, u32 cap)
{
	switch (archive)
	{
	case ARCHIVE_SAVE: snprintf (root, cap, "save:"); return FS_NOT_FORMATTED;
	case ARCHIVE_EXT: extRoot (m, pathVa, pathSize, false, root, cap); return FS_NO_EXT_DATA;
	case ARCHIVE_SHARED_EXT: extRoot (m, pathVa, pathSize, true, root, cap); return 0;	// (the system's own: always there)
	case ARCHIVE_SDMC: case ARCHIVE_SDMC_WRITE: snprintf (root, cap, "sd:"); return 0;
	}
	root[0] = 0;
	return FS_NO_ARCHIVE;
}

void fsRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	Storage *st = storage (m);
	char name[320], other[320];
#define REPLY(result) do { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = (result); } while (0)
	if (!st) { REPLY (RES_OUT_OF_MEMORY); return; }
	switch (id)
	{
	case 0x0801:								// Initialize
	case 0x0861:								// InitializeWithSdkVersion
	case 0x0862:								// SetPriority
	case 0x080E:								// CloseArchive
	case 0x080D:								// ControlArchive (commit the save data: it is in memory, the host stores it)
		REPLY (RES_OK);
		break;
	case 0x0863:								// GetPriority
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x0817:								// IsSdmcDetected
	case 0x0818:								// IsSdmcWritable
	case 0x0821:								// CardSlotIsInserted
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 1;
		break;
	case 0x0813:								// GetCardType -> a 3DS card
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	case 0x0812:								// GetFreeBytes (archive) -> 64 MB
		cmd[0] = ipcHeader (id, 3, 0); cmd[1] = RES_OK; cmd[2] = 0x04000000; cmd[3] = 0;
		break;
	case 0x080C:								// OpenArchive (archive, its path) -> a 64-bit handle
	{
		if (cmd[1] == ARCHIVE_ROMFS) { cmd[0] = ipcHeader (id, 3, 0); cmd[1] = m->romfsSize ? (u32) RES_OK : (u32) FS_NOT_FOUND; cmd[2] = 0x80000003; cmd[3] = 0; break; }
		const u32 missing = archiveName (m, cmd[1], cmd[5], cmd[3], name, sizeof name);
		if (!name[0]) { m->note ("fs:USER archive %08x", (unsigned) cmd[1]); REPLY (missing); break; }
		if (nodeFind (st, name) < 0)
		{
			if (missing) { REPLY (missing); break; }
			nodeAdd (st, name, true);				// (the SD card is always there)
		}
		const u32 h = archiveOpen (m, name);
		cmd[0] = ipcHeader (id, 3, 0); cmd[1] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; cmd[2] = h; cmd[3] = 0;
		break;
	}
	case 0x084C:								// FormatSaveData (archive, its path, sizes...): the save data is made, empty
	case 0x080F:								// FormatThisUserSaveData
	{
		for (int i = st->count - 1; i >= 0; i--) if (strncmp (st->nodes[i].name, "save:", 5) == 0) nodeRemove (st, i);
		nodeAdd (st, "save:", true);
		m->storageDirty = true;
		REPLY (RES_OK);
		break;
	}
	case 0x0851:								// CreateExtSaveData (media, the id, ...)
	case 0x0830:								// CreateExtSaveData, the old command
	{
		snprintf (name, sizeof name, "ext%08X%08X:", (unsigned) cmd[3], (unsigned) cmd[2]);
		if (nodeFind (st, name) < 0) nodeAdd (st, name, true);
		m->storageDirty = true;
		REPLY (RES_OK);
		break;
	}
	case 0x0845:								// GetFormatInfo (archive, its path) -> size, folders, files, duplicated
	{
		const u32 missing = archiveName (m, cmd[1], cmd[5], cmd[3], name, sizeof name);
		if (!name[0] || nodeFind (st, name) < 0) { REPLY (missing ? missing : (u32) FS_NOT_FORMATTED); break; }
		cmd[0] = ipcHeader (id, 5, 0); cmd[1] = RES_OK; cmd[2] = 0x00100000; cmd[3] = 64; cmd[4] = 64; cmd[5] = 0;
		break;
	}
	case 0x0803:								// OpenFileDirectly (-, archive, the archive's path, the file's path, flags...)
	{
		if (cmd[2] == ARCHIVE_ROMFS) { openRomfs (m, id, cmd); break; }
		const u32 missing = archiveName (m, cmd[2], cmd[10], cmd[4], name, sizeof name);
		if (!name[0]) { m->note ("fs:USER archive %08x", (unsigned) cmd[2]); REPLY (missing); break; }
		if (nodeFind (st, name) < 0) { if (missing) { REPLY (missing); break; } nodeAdd (st, name, true); }
		char path[280];
		if (!pathText (m, cmd[5], cmd[6], cmd[12], path, sizeof path)) { REPLY (FS_NOT_FOUND); break; }
		snprintf (other, sizeof other, "%s%s%s", name, path[0] && path[0] != '/' ? "/" : "", path);
		openStored (m, id, cmd, other, cmd[7]);
		break;
	}
	case 0x0802:								// OpenFile (-, archive handle, the path, flags, attributes)
		if (cmd[2] == 0x80000003) { openRomfs (m, id, cmd); break; }
		if (!fullName (m, cmd[2], cmd[4], cmd[5], cmd[9], name, sizeof name)) { REPLY (FS_NOT_FOUND); break; }
		openStored (m, id, cmd, name, cmd[6]);
		break;
	case 0x0808:								// CreateFile (-, archive, the path, attributes, the size)
	{
		if (!fullName (m, cmd[2], cmd[4], cmd[5], cmd[10], name, sizeof name) || !parentThere (st, name)) { REPLY (FS_NOT_FOUND); break; }
		if (nodeFind (st, name) >= 0) { REPLY (FS_ALREADY_THERE); break; }
		const int i = nodeAdd (st, name, false);
		if (i < 0 || cmd[8] || !nodeResize (st->nodes[i], cmd[7])) { if (i >= 0) nodeRemove (st, i); REPLY (FS_FULL); break; }
		m->storageDirty = true;
		REPLY (RES_OK);
		break;
	}
	case 0x0809:								// CreateDirectory (-, archive, the path, attributes)
		if (!fullName (m, cmd[2], cmd[4], cmd[5], cmd[8], name, sizeof name) || !parentThere (st, name)) { REPLY (FS_NOT_FOUND); break; }
		if (nodeFind (st, name) >= 0) { REPLY (FS_ALREADY_THERE); break; }
		REPLY (nodeAdd (st, name, true) >= 0 ? (u32) RES_OK : (u32) FS_FULL);
		m->storageDirty = true;
		break;
	case 0x0804:								// DeleteFile (-, archive, the path)
	case 0x0806:								// DeleteDirectory
	case 0x0807:								// DeleteDirectoryRecursively
	{
		if (!fullName (m, cmd[2], cmd[4], cmd[5], cmd[7], name, sizeof name)) { REPLY (FS_NOT_FOUND); break; }
		const int i = nodeFind (st, name);
		if (i < 0 || st->nodes[i].dir != (id != 0x0804)) { REPLY (FS_NOT_FOUND); break; }
		const size_t len = strlen (name);
		if (id == 0x0807) for (int k = st->count - 1; k >= 0; k--) if (strncmp (st->nodes[k].name, name, len) == 0 && st->nodes[k].name[len] == '/') nodeRemove (st, k);
		nodeRemove (st, nodeFind (st, name));
		m->storageDirty = true;
		REPLY (RES_OK);
		break;
	}
	case 0x0805:								// RenameFile (-, archive, the path, archive, the new path)
	case 0x080A:								// RenameDirectory
	{
		if (!fullName (m, cmd[2], cmd[4], cmd[5], cmd[11], name, sizeof name) || !fullName (m, cmd[6], cmd[8], cmd[9], cmd[13], other, sizeof other)) { REPLY (FS_NOT_FOUND); break; }
		const int i = nodeFind (st, name);
		if (i < 0) { REPLY (FS_NOT_FOUND); break; }
		if (nodeFind (st, other) >= 0) { REPLY (FS_ALREADY_THERE); break; }
		const size_t len = strlen (name);
		for (int k = 0; k < st->count; k++)				// it, and what is under it
		{
			Storage::Node &n = st->nodes[k];
			if (strncmp (n.name, name, len) != 0 || (n.name[len] && n.name[len] != '/')) continue;
			char *nn = (char *) malloc (strlen (other) + strlen (n.name + len) + 1);
			if (!nn) continue;
			strcpy (nn, other); strcat (nn, n.name + len);
			free (n.name); n.name = nn;
		}
		m->storageDirty = true;
		REPLY (RES_OK);
		break;
	}
	case 0x080B:								// OpenDirectory (archive, the path) -> a session
	{
		if (!fullName (m, cmd[1], cmd[3], cmd[4], cmd[6], name, sizeof name)) { REPLY (FS_NOT_FOUND); break; }
		const int i = nodeFind (st, name);
		if (i < 0 || !st->nodes[i].dir) { REPLY (FS_NOT_FOUND); break; }
		Session *d = new Session (dirRequest, "folder"); d->kind = KIND_DIR;
		d->path = (char *) malloc (strlen (name) + 1);
		if (d->path) strcpy (d->path, name);
		u32 h = m->handleNew (d);
		cmd[0] = ipcHeader (id, 1, 2); cmd[1] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; cmd[2] = 0x10; cmd[3] = h;
		break;
	}
	default:
		m->note ("%s command %08x", s->name, (unsigned) cmd[0]);
		REPLY (RES_NOT_IMPLEMENTED);
		break;
	}
#undef REPLY
}

}
