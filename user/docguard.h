//
// docguard.h -- "unsaved changes?" for document apps (tinypad, writer, paint).
//
// An app keeps the hash of its document as last loaded / saved (doc_hash); before
// replacing the document (a file dropped on its window, Open...) it calls doc_confirm:
// if the document changed, a Yes / No / Cancel box asks to save it first.
//   doc_confirm (name, changed, save_fn) -> true = go ahead (saved or discarded),
//                                          false = cancelled
//
#ifndef _docguard_h
#define _docguard_h

#include "kapi.h"
#include "wtk/dialog.h"

static inline unsigned doc_hash (const void *p, unsigned n)	// FNV-1a
{
	const unsigned char *b = (const unsigned char *) p;
	unsigned h = 2166136261u;
	for (unsigned i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
	return h ^ n;
}

static inline bool doc_confirm (const char *name, bool changed, void (*save_fn) (void))
{
	if (!changed) return true;
	static char msg[160];
	const char *a = "Save the changes to ";
	int p = 0;
	for (int i = 0; a[i]; i++) msg[p++] = a[i];
	for (int i = 0; name && name[i] && p < (int) sizeof msg - 3; i++) msg[p++] = name[i];
	msg[p++] = '?'; msg[p] = '\0';
	int r = wtk::wk_messagebox ("Unsaved changes", msg, MB_YESNOCANCEL);
	if (r == 0) return false;			// Cancel / Esc
	if (r == 1 && save_fn) save_fn ();		// Yes
	return true;					// Yes (saved) / No (discard)
}

// The first path of a DND_FILES payload ('\n'-separated) into out.
static inline bool doc_first_path (const char *data, char *out, int cap)
{
	int n = 0;
	while (data[n] && data[n] != '\n' && n < cap - 1) { out[n] = data[n]; n++; }
	out[n] = '\0';
	return n > 0;
}

#endif
