//
// preload -- load programs ahead and keep them in memory (kapi v77: program images). A preloaded
// program starts without reading the card -- its image is mapped, shared by all its processes --
// and stays in memory when none runs. The load itself runs in the kernel: preload returns at once.
//   usage: preload <program>...     a path, an app's name (apps/<name>.app/main) or a /bin tool's
//          preload                  list the program images in memory
//          preload /boot            the programs SD:/etc/preload.ini lists (preloadini.h; the Control
//                                   Panel's Preload applet writes it)
// In /etc/autostart: the last line, "preload /boot" (after the desktop's lines: the boot is not
// longer for it). `unload <program>` releases one.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "appkit/appkit.h"
#include "applib.h"
#include "imgname.h"
#include "preloadini.h"

#define MAX_IMAGES	64
static struct kapi_image_info g_img[MAX_IMAGES];

// v right-aligned in w columns
static void put_num (unsigned v, int w)
{
	char b[12];
	int n = ax_itoa ((int) v, b);
	for (; n < w; n++) ax_puts (" ");
	ax_puts (b);
}

static int list (void)
{
	int n = kapi_image_list (0, g_img, MAX_IMAGES);
	if (n == -KAPI_ENOSYS) { ax_putln ("preload: this kernel has no program images (kapi v77)"); return 1; }
	if (n < 0) { ax_putln ("preload: cannot list the images"); return 1; }
	ax_putln ("  size KB  uses  state    kept  program");
	unsigned long long total = 0;
	for (int i = 0; i < n && i < MAX_IMAGES; i++)
	{
		const struct kapi_image_info *p = &g_img[i];
		put_num ((unsigned) ((p->size + 1023) >> 10), 9);
		put_num (p->refs, 6);
		ax_puts ((p->flags & KAPI_IMG_LOADING) ? "  loading  " : "  ready    ");
		ax_puts ((p->flags & KAPI_IMG_KEPT) ? "yes   " : (p->flags & KAPI_IMG_UNNAMED) ? "gone  " : "no    ");
		ax_puts (p->path[0] != '\0' ? p->path : "(no path)");
		ax_putln ((p->flags & KAPI_IMG_LIB) ? "  (library)" : "");
		total += p->size;
	}
	char b[12];
	ax_itoa (n, b); ax_puts (b); ax_puts (" images, ");
	ax_itoa ((int) ((total + 1023) >> 10), b); ax_puts (b); ax_putln (" KB");
	return 0;
}

// One program (a path, an app's name, a /bin tool's) -> 0, 1: not preloaded (said)
static int preload_one (const char *word)
{
	char path[300];
	img_program (word, path, sizeof path, 0);
	int r = kapi_image_preload (path);
	if (r == 0) return 0;
	ax_puts ("preload: ");
	ax_puts (path);
	ax_putln (r == -KAPI_ENOENT ? ": no such program"
		: r == -KAPI_ENOSYS ? ": this kernel has no program images (kapi v77)"
		: r == -KAPI_ENOMEM ? ": out of memory"
		: ": cannot preload it");
	return 1;
}

static int streq (const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

int main (void)
{
	static char args[1024];
	kapi_get_args (args, sizeof (args));

	int pos = 0, any = 0, rc = 0;
	char word[256];
	while (img_next_arg (args, &pos, word, sizeof word))
	{
		any = 1;
		if (streq (word, "/boot"))			// the list of SD:/etc/preload.ini (none: nothing to do)
		{
			static struct PreloadList l;
			preload_ini_load (&l);
			for (int i = 0; i < l.n; i++) rc |= preload_one (l.prog[i]);
			continue;
		}
		rc |= preload_one (word);
	}
	if (!any) return list ();
	return rc;
}
