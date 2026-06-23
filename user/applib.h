//
// applib.h -- tiny freestanding string helpers for the apps (no libc in EL1 apps).
// Header-only; included by the shell apps that build paths and parse config files.
//
#ifndef _applib_h
#define _applib_h

// Append src to dst[*pos], advancing *pos, never overflowing cap. NUL-terminates.
static inline void ax_strcat (char *dst, int cap, int *pos, const char *src)
{
	while (*src != '\0' && *pos < cap - 1)
	{
		dst[(*pos)++] = *src++;
	}
	dst[*pos] = '\0';
}

// Build "SD:apps/<name><suffix>" (e.g. suffix ".app/icon.bmp") into dst.
static inline void ax_app_path (char *dst, int cap, const char *name, const char *suffix)
{
	int p = 0;
	ax_strcat (dst, cap, &p, "SD:apps/");
	ax_strcat (dst, cap, &p, name);
	ax_strcat (dst, cap, &p, suffix);
}

static inline int ax_streq (const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) { a++; b++; }
	return *a == *b;
}

// Two-digit zero-padded decimal into d[0],d[1].
static inline void ax_fmt2 (char *d, int v)
{
	if (v < 0) v = 0;
	d[0] = (char) ('0' + (v / 10) % 10);
	d[1] = (char) ('0' + v % 10);
}

#endif
