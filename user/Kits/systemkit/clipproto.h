//
// clipproto.h -- the shared clipboard's protocol (docs/clipboard/README.md): what the apps
// (clipboard.h), the service (apps/clipd) and its widget (apps/clipboard) say to each other.
//
// The service `clipd` (IPC name "clipboard") keeps a ring of CLIP_RING items in its own memory, a
// cursor on the one Ctrl+V pastes. An item holds one or several representations, each a format (a
// short name: "text", "rtf", "image", "files", "files-cut", "url", "x-<app>") and its bytes.
// Messages go through the mailboxes (<= 512 bytes); the bytes through files of RAM: (a copy in the
// kernel's memory, no mapping kept by anyone): the app writes a container there and says where;
// for a paste, clipd writes the answer where the app asked and the app waits for that file -- so an
// app never reads its own mailbox (it may use it for something else).
//
// A container (a file, or the tail of a CLIP_PUT_INLINE): "CLP1", u32 count, then each
// representation: u16 the format's length, the format, u32 the data's length, the data.
// An image's data: u32 width, u32 height, then width * height pixels 0x00RRGGBB.
//
#ifndef _clipproto_h
#define _clipproto_h

#define CLIP_SERVICE	"clipboard"
#define CLIP_RING	10
#define CLIP_DIR	"RAM:/clip"			// the transfers' files

enum
{
	// an app -> clipd
	CLIP_PUT = 1,			// "path\0source\0": the container in that file (clipd removes it)
	CLIP_PUT_INLINE = 2,		// "source\0" + a container: a small copy, whole in the message
	CLIP_GET = 3,			// "reply path\0fmt\0fmt\0...\0": the cursor's item in the first
					// format given it has -- else the newest item that has one --,
					// written to the reply path (a container of 0 or 1 representation)
	CLIP_DROP_CUT = 4,		// the cursor's item, if it is a cut (files-cut): deleted (pasted)
	// the widget -> clipd
	CLIP_LIST = 5,			// -> CLIP_ITEM ... CLIP_END, to the sender
	CLIP_CURSOR = 6,		// u32 id: the cursor there
	CLIP_DELETE = 7,		// u32 id
	CLIP_CLEAR = 8,			// every item
	CLIP_SUBSCRIBE = 9,		// the sender told CLIP_CHANGED at every change
	// clipd -> the widget
	CLIP_ITEM = 100,		// struct ClipItemMsg
	CLIP_END = 101,			// u32 count
	CLIP_CHANGED = 102
};

// what the widget is told of an item (one message)
struct ClipItemMsg
{
	unsigned id, size;		// size: the main representation's bytes
	unsigned w, h;			// an image's
	unsigned char cursor, nfiles, hour, minute;
	char kind[16];			// the main format ("text", "image", "files", "files-cut", "rtf", "url"...)
	char source[32];		// the app it came from
	char preview[400];		// a line of text (a text's start, the files' names, the URL)
};

// ---- the container ----------------------------------------------------------------------------------
static inline void clipc_put32 (unsigned char *p, unsigned v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24); }
static inline unsigned clipc_get32 (const unsigned char *p) { return (unsigned) p[0] | (unsigned) p[1] << 8 | (unsigned) p[2] << 16 | (unsigned) p[3] << 24; }
static inline int clipc_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline int clipc_eq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

// the size of a container of n representations
static inline unsigned clipc_size (const char *const *fmt, const unsigned *len, int n)
{
	unsigned t = 8;
	for (int i = 0; i < n; i++) t += 2 + (unsigned) clipc_len (fmt[i]) + 4 + len[i];
	return t;
}
// writes it into out (clipc_size bytes); -> the bytes written
static inline unsigned clipc_write (unsigned char *out, const char *const *fmt, const void *const *data, const unsigned *len, int n)
{
	unsigned char *p = out;
	p[0] = 'C'; p[1] = 'L'; p[2] = 'P'; p[3] = '1'; clipc_put32 (p + 4, (unsigned) n); p += 8;
	for (int i = 0; i < n; i++)
	{
		int fl = clipc_len (fmt[i]);
		p[0] = (unsigned char) fl; p[1] = (unsigned char) (fl >> 8); p += 2;
		for (int k = 0; k < fl; k++) *p++ = (unsigned char) fmt[i][k];
		clipc_put32 (p, len[i]); p += 4;
		const unsigned char *d = (const unsigned char *) data[i];
		for (unsigned k = 0; k < len[i]; k++) *p++ = d[k];
	}
	return (unsigned) (p - out);
}
// representation i of a container (0..count-1): its format into fmt (cap), *data, *len -> 1 / 0
static inline int clipc_count (const unsigned char *c, unsigned n)
{
	if (n < 8 || c[0] != 'C' || c[1] != 'L' || c[2] != 'P' || c[3] != '1') return 0;
	return (int) clipc_get32 (c + 4);
}
static inline int clipc_rep (const unsigned char *c, unsigned n, int i, char *fmt, int cap, const unsigned char **data, unsigned *len)
{
	int cnt = clipc_count (c, n);
	unsigned p = 8;
	for (int k = 0; k < cnt; k++)
	{
		if (p + 2 > n) return 0;
		unsigned fl = (unsigned) c[p] | (unsigned) c[p + 1] << 8; p += 2;
		if (p + fl + 4 > n) return 0;
		unsigned dl = clipc_get32 (c + p + fl);
		if (p + fl + 4 + dl > n) return 0;
		if (k == i)
		{
			unsigned m = fl < (unsigned) cap - 1 ? fl : (unsigned) cap - 1;
			for (unsigned j = 0; j < m; j++) fmt[j] = (char) c[p + j];
			fmt[m] = 0;
			*data = c + p + fl + 4; *len = dl;
			return 1;
		}
		p += fl + 4 + dl;
	}
	return 0;
}

#endif
