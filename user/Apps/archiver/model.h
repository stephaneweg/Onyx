//
// archiver/model.h -- the archive as a tree of folders and files, for the view: node 0 the archive
// itself, a folder for every path part (whether the archive lists it or not), each node's size,
// packed size and count of files under it; the children of a folder sorted (folders first, then by
// the column the list sorts on); a search over every name.
//
#ifndef _archiver_model_h
#define _archiver_model_h

#include "arc.h"

namespace ui {

using arc::u64; using arc::u32;

struct Node
{
	char *name;				// its part of the path (node 0: the archive's file name)
	int   parent, entry;			// the entry it is (-1: a folder the archive does not list)
	bool  dir, open;			// a folder; shown open in the tree
	u64   size, packed;			// itself, or what is under it
	u32   time;
	int   files;			// files under it (1 for a file)
	int  *kids; int nkids, kcap;
};

enum { SORT_NAME, SORT_SIZE, SORT_PACKED, SORT_RATIO, SORT_TIME, SORT_METHOD };

class Model
{
public:
	arc::Archive *a;
	Node *n; int count, cap;
	int  *hash; int hcap;			// (parent, name) -> node: open addressing

	Model () : a (0), n (0), count (0), cap (0), hash (0), hcap (0) {}
	~Model () { clear (); }
	void clear ()
	{
		for (int i = 0; i < count; i++) { free (n[i].name); free (n[i].kids); }
		free (n); n = 0; count = cap = 0; free (hash); hash = 0; hcap = 0;
	}

	static unsigned hstr (int parent, const char *s, int len)
	{
		unsigned h = 2166136261u ^ (unsigned) parent * 16777619u;
		for (int i = 0; i < len; i++) h = (h ^ (unsigned char) arc::lower (s[i])) * 16777619u;
		return h;
	}
	int lookup (int parent, const char *s, int len)
	{
		unsigned h = hstr (parent, s, len) & (unsigned) (hcap - 1);
		for (;;)
		{
			int k = hash[h];
			if (k < 0) return -1;
			if (n[k].parent == parent && !strncasecmp (n[k].name, s, (size_t) len) && n[k].name[len] == 0) return k;
			h = (h + 1) & (unsigned) (hcap - 1);
		}
	}
	int make (int parent, const char *s, int len, bool dir)
	{
		if (count == cap)
		{
			cap = cap ? cap * 2 : 256;
			n = (Node *) realloc (n, sizeof (Node) * cap);
		}
		Node &x = n[count]; memset (&x, 0, sizeof x);
		x.name = arc::sdup (s, len); x.parent = parent; x.entry = -1; x.dir = dir;
		if (parent >= 0)
		{
			Node &p = n[parent];
			if (p.nkids == p.kcap) { p.kcap = p.kcap ? p.kcap * 2 : 8; p.kids = (int *) realloc (p.kids, sizeof (int) * p.kcap); }
			p.kids[p.nkids++] = count;
			unsigned h = hstr (parent, s, len) & (unsigned) (hcap - 1);
			while (hash[h] >= 0) h = (h + 1) & (unsigned) (hcap - 1);
			hash[h] = count;
		}
		return count++;
	}

	void build (arc::Archive *arch)
	{
		clear (); a = arch;
		hcap = 1024; while (hcap < (a ? a->n : 0) * 3) hcap <<= 1;
		hash = (int *) malloc (sizeof (int) * hcap);
		for (int i = 0; i < hcap; i++) hash[i] = -1;
		const char *an = a ? arc::base_of (a->path) : "";
		make (-1, an, (int) strlen (an), true);
		n[0].open = true;
		if (!a) return;
		for (int i = 0; i < a->n; i++)
		{
			const arc::Entry &e = a->e[i];
			int parent = 0;
			const char *s = e.name;
			while (*s)
			{
				const char *t = s; while (*t && *t != '/') t++;
				bool last = !*t, dir = !last || e.dir;
				int k = lookup (parent, s, (int) (t - s));
				if (k < 0) k = make (parent, s, (int) (t - s), dir);
				if (last) { n[k].entry = i; n[k].dir = e.dir; n[k].time = e.dostime; }
				parent = k;
				s = *t ? t + 1 : t;
			}
		}
		total (0);
	}
	void total (int k)
	{
		Node &x = n[k];
		if (!x.dir) { const arc::Entry &e = a->e[x.entry]; x.size = e.size; x.packed = e.packed; x.files = 1; return; }
		x.size = x.packed = 0; x.files = 0;
		for (int i = 0; i < x.nkids; i++)
		{
			int c = x.kids[i]; total (c);
			x.size += n[c].size; x.packed += n[c].packed; x.files += n[c].files;
			if (n[c].time > x.time && x.entry < 0) x.time = n[c].time;
		}
	}
	// the archive path of node k ("" for the archive itself)
	void pathOf (int k, char *out, int cap)
	{
		int chain[64], d = 0;
		for (int i = k; i > 0 && d < 64; i = n[i].parent) chain[d++] = i;
		out[0] = 0;
		while (d--) { if (out[0]) arc::scat (out, "/", cap); arc::scat (out, n[chain[d]].name, cap); }
	}
	int find (const char *path)
	{
		int k = 0;
		const char *s = path;
		while (*s && k >= 0)
		{
			const char *t = s; while (*t && *t != '/') t++;
			k = lookup (k, s, (int) (t - s));
			s = *t ? t + 1 : t;
		}
		return k;
	}
	int depth (int k) { int d = 0; for (; k > 0; k = n[k].parent) d++; return d; }
	bool isAncestor (int anc, int k) { for (; k >= 0; k = n[k].parent) if (k == anc) return true; return false; }

	// ---- the list: a folder's children, or a search's matches, sorted --------------------------------
	int sortCol; bool sortDesc;
	int cmp (int x, int y)
	{
		const Node &p = n[x], &q = n[y];
		if (p.dir != q.dir) return p.dir ? -1 : 1;
		long long d = 0;
		switch (sortCol)
		{
		case SORT_SIZE: d = p.size < q.size ? -1 : p.size > q.size; break;
		case SORT_PACKED: d = p.packed < q.packed ? -1 : p.packed > q.packed; break;
		case SORT_RATIO: d = (long long) ratio (x) - ratio (y); break;
		case SORT_TIME: d = p.time < q.time ? -1 : p.time > q.time; break;
		case SORT_METHOD:
			if (p.entry >= 0 && q.entry >= 0) d = a->e[p.entry].method - a->e[q.entry].method; break;
		}
		if (!d) d = arc::ci_cmp (p.name, q.name);
		return (int) (sortDesc ? -d : d) < 0 ? -1 : d ? 1 : 0;
	}
	int ratio (int k)			// saved, in tenths of a percent
	{
		const Node &x = n[k];
		if (!x.size) return 0;
		if (x.packed >= x.size) return 0;
		return (int) ((x.size - x.packed) * 1000 / x.size);
	}
	void sortList (int *v, int m)
	{
		for (int i = 1; i < m; i++)		// (an insertion sort for the short, a shell sort for the long)
		{
			int t = v[i], j = i;
			if (m > 64) break;
			while (j > 0 && cmp (v[j - 1], t) > 0) { v[j] = v[j - 1]; j--; }
			v[j] = t;
		}
		if (m > 64)
			for (int gap = m / 2; gap > 0; gap /= 2)
				for (int i = gap; i < m; i++)
				{
					int t = v[i], j = i;
					while (j >= gap && cmp (v[j - gap], t) > 0) { v[j] = v[j - gap]; j -= gap; }
					v[j] = t;
				}
	}
	// the folder's children into *out (malloc'ed) -> how many
	int list (int folder, int **out)
	{
		const Node &f = n[folder];
		int *v = (int *) malloc (sizeof (int) * (f.nkids ? f.nkids : 1));
		for (int i = 0; i < f.nkids; i++) v[i] = f.kids[i];
		sortList (v, f.nkids);
		*out = v; return f.nkids;
	}
	// every node whose name holds `q` (case aside)
	int search (const char *q, int **out)
	{
		int *v = (int *) malloc (sizeof (int) * (count ? count : 1)), m = 0;
		for (int i = 1; i < count; i++)
		{
			const char *s = n[i].name;
			for (; *s; s++)
			{
				int j = 0; while (q[j] && s[j] && arc::lower (s[j]) == arc::lower (q[j])) j++;
				if (!q[j]) { v[m++] = i; break; }
			}
		}
		sortList (v, m);
		*out = v; return m;
	}
};

} // namespace ui

#endif
