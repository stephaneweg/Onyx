//
// basic/baskits.cpp -- Onyx BASIC's kits on Onyx (baskits.h): a kit's description read from SD:/lib, the
// kit opened as any shared library is (lib.h) -- so a kit made after this BASIC is imported like the
// others: nothing here names one, but AppKit, which every program already has at a fixed place.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "appkit/appkit.h"
#include "lib.h"
#include "basic/baskits.h"

namespace bas {

static int kcat (char *b, int n, int cap, const char *s)
{
	while (*s && n < cap - 1) b[n++] = *s++;
	if (cap > 0) b[n] = 0;
	return n;
}
static bool ksame (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

char *onyxKitSource (const char *name, int *len)
{
	*len = 0;
	for (const char *c = name; *c; c++) if (*c == '/' || *c == ':' || *c == '.') return 0;
	char path[80];
	int n = kcat (path, 0, sizeof path, "SD:/lib/");
	n = kcat (path, n, sizeof path, name);
	kcat (path, n, sizeof path, ".bi");
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned size = kapi_fsize (f);
	char *b = new char[size + 1];
	int r = kapi_read (f, b, size);
	kapi_close (f);
	if (r < 0) { delete [] b; return 0; }
	b[r] = 0; *len = r;
	return b;
}

void *const *onyxKitOpen (const char *name, int minVersion, char *why, int cap)
{
	// AppKit: its entries are where the kernel puts them for every program (APPKIT_TABLE_VA; an entry it has not: 0)
	if (ksame (name, "appkit")) return (void *const *) APPKIT_TABLE_VA;
	for (const char *c = name; *c; c++) if (*c == '/' || *c == ':' || *c == '.') { kcat (why, 0, cap, "not a kit's name"); return 0; }
	int err = 0;
	const TLibHeader *t = (const TLibHeader *) kapi_lib_open (name, (unsigned) (minVersion > 0 ? minVersion : 0), &err);
	if (!t)
	{
		kcat (why, 0, cap, err == -KAPI_ENOENT ? "it is not in SD:/lib: install its package"
				 : err == -KAPI_ENOTSUP ? "the one installed is older: update its package"
				 : err == -KAPI_ENOSYS ? "this kernel has no shared libraries: update the system"
				 : err == -KAPI_ENOMEM ? "out of memory" : "it cannot be loaded");
		return 0;
	}
	if (t->init (lib_cxx_imports ()) < 0) { kcat (why, 0, cap, "its start failed"); return 0; }
	return (void *const *) (t + 1);
}

} // namespace bas
