//
// recent.inc -- recent.h's bodies (SystemKit; on the PC inline).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifdef SK_IMPL
#define SK_FN	extern "C"
#else
#define SK_FN	static inline
#endif
static inline char rd_low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static inline int rd_same (const char *a, const char *b)	// the same path, ignoring the case (FAT)
{
	while (*a && rd_low (*a) == rd_low (*b)) { a++; b++; }
	return !*a && !*b;
}
static inline void rd_num (char *b, int *k, int v, int w)	// v in w digits at b + *k
{
	for (int c = w - 1; c >= 0; c--) { b[*k + c] = (char) ('0' + v % 10); v /= 10; }
	*k += w;
}
// The list read: each line "YYYYMMDDHHMM|path" -> how many put in out.
SK_FN int recent_docs (struct recent_doc *out, int max)
{
	static char b[RECENT_DOCS_MAX * 180 + 64];
	int n = -1;
	void *h = kapi_open (RECENT_DOCS_FILE);
	if (h != 0) { n = kapi_read (h, b, sizeof b - 1); kapi_close (h); }
	if (n <= 0) return 0;
	b[n] = 0;
	int k = 0;
	for (int i = 0; i < n && k < max; )
	{
		int s = i;
		while (i < n && b[i] != '\n' && b[i] != '\r') i++;
		int e = i;
		while (i < n && (b[i] == '\n' || b[i] == '\r')) i++;
		if (e - s < 14 || b[s] == '#' || b[s + 12] != '|') continue;
		int d = 0, t = 0, ok = 1;
		for (int j = 0; j < 12; j++)
		{
			char c = b[s + j];
			if (c < '0' || c > '9') { ok = 0; break; }
			if (j < 8) d = d * 10 + (c - '0'); else t = t * 10 + (c - '0');
		}
		if (!ok) continue;
		struct recent_doc *r = &out[k];
		int p = 0;
		for (int j = s + 13; j < e && p < (int) sizeof r->path - 1; j++) r->path[p++] = b[j];
		r->path[p] = 0;
		if (p == 0) continue;
		r->date = d; r->time = t;
		k++;
	}
	return k;
}
SK_FN void recent_doc_add (const char *path)
{
	if (path == 0 || !path[0]) return;
	int L = 0;
	while (path[L]) L++;
	if (L >= 160 || path[L - 1] == '/' || path[L - 1] == ':') return;
	static struct recent_doc d[RECENT_DOCS_MAX];
	int n = recent_docs (d, RECENT_DOCS_MAX);
	static char b[RECENT_DOCS_MAX * 180 + 64];
	int k = 0;
	int y = 2026, mo = 1, dd = 1, hh = 0, mi = 0;
	kapi_get_datetime (&y, &mo, &dd, &hh, &mi, 0);
	rd_num (b, &k, y, 4); rd_num (b, &k, mo, 2); rd_num (b, &k, dd, 2); rd_num (b, &k, hh, 2); rd_num (b, &k, mi, 2);
	b[k++] = '|';
	for (int j = 0; j < L; j++) b[k++] = path[j];
	b[k++] = '\n';
	int kept = 1;
	for (int i = 0; i < n && kept < RECENT_DOCS_MAX; i++)
	{
		if (rd_same (d[i].path, path)) continue;
		rd_num (b, &k, d[i].date, 8); rd_num (b, &k, d[i].time, 4);
		b[k++] = '|';
		for (int j = 0; d[i].path[j]; j++) b[k++] = d[i].path[j];
		b[k++] = '\n';
		kept++;
	}
	kapi_save_file (RECENT_DOCS_FILE, b, (unsigned) k);
}
#undef SK_FN
