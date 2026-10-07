//
// unload -- release a program's image (kapi v77: program images; see `preload`). The image loses
// its pin and its name at once: the next start of the program reads its file again; the processes
// running it go on, and its memory is freed when the last of them ends (now if none runs).
//   usage: unload <program>...      a path, an app's name (apps/<name>.app/main) or a /bin tool's
// `pkg` does it by itself before it replaces a preloaded program (and preloads the new one).
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
#include "imgname.h"

int main (void)
{
	static char args[1024];
	kapi_get_args (args, sizeof (args));

	int pos = 0, any = 0, rc = 0;
	char word[256], path[300];
	while (img_next_arg (args, &pos, word, sizeof word))
	{
		any = 1;
		img_program (word, path, sizeof path, 1);
		int r = kapi_image_unload (path);
		if (r == 0) continue;
		rc = 1;
		ax_puts ("unload: ");
		ax_puts (path);
		ax_putln (r == -KAPI_ENOENT ? ": not in memory"
			: r == -KAPI_ENOSYS ? ": this kernel has no program images (kapi v77)"
			: ": cannot unload it");
	}
	if (!any)
	{
		ax_putln ("usage: unload <program>...   (`preload` lists the images)");
		return 1;
	}
	return rc;
}
