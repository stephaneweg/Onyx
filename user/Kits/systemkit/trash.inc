//
// trash.inc -- the code of trash.h (SystemKit: compiled into systemkit.so; a PC build has it inline).
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
#ifdef SK_IMPL
#define SK_FN	extern "C"
#else
#define SK_FN	static inline
#endif
SK_FN void trash_ensure (void)
{
	kapi_mkdir (TRASH_DIR);
	kapi_mkdir (TRASH_FILES);
	kapi_mkdir (TRASH_INFO);
}
SK_FN void trash_info_path_ (char *out, int cap, const char *name)
{
	char n[FS_NAMEL + 12]; int p = 0;
	for (int i = 0; name[i] && p < FS_NAMEL; i++) n[p++] = name[i];
	const char *e = ".trashinfo"; for (int i = 0; e[i]; i++) n[p++] = e[i];
	n[p] = '\0';
	fs_join (out, cap, TRASH_INFO, n);
}
SK_FN bool trash_move (const char *path)
{
	trash_ensure ();
	char dst[FS_PATHL];
	fs_unique_name (dst, sizeof dst, TRASH_FILES, fs_basename (path), " (trashed)");
	if (kapi_rename (path, dst) != 0) return false;		// (kapi: 0 = ok)
	char info[FS_PATHL], txt[FS_PATHL + 16]; int p = 0;
	trash_info_path_ (info, sizeof info, fs_basename (dst));
	const char *k = "Path="; for (int i = 0; k[i]; i++) txt[p++] = k[i];
	for (int i = 0; path[i] && p < (int) sizeof txt - 2; i++) txt[p++] = path[i];
	txt[p++] = '\n';
	kapi_save_file (info, txt, (unsigned) p);
	return true;
}
SK_FN bool trash_origin (const char *name, char *out, int cap)
{
	char info[FS_PATHL];
	trash_info_path_ (info, sizeof info, name);
	void *f = kapi_open (info);
	if (!f) return false;
	char b[FS_PATHL + 16];
	int n = kapi_read (f, b, sizeof b - 1);
	kapi_close (f);
	if (n <= 5) return false;
	b[n] = '\0';
	if (b[0] != 'P' || b[4] != '=') return false;
	int k = 0;
	for (int i = 5; b[i] && b[i] != '\n' && b[i] != '\r' && k < cap - 1; i++) out[k++] = b[i];
	out[k] = '\0';
	return k > 0;
}
SK_FN bool trash_restore (const char *name, char *where, int wcap)
{
	char src[FS_PATHL], orig[FS_PATHL], dir[FS_PATHL], dst[FS_PATHL];
	fs_join (src, sizeof src, TRASH_FILES, name);
	if (!trash_origin (name, orig, sizeof orig)) { fs_join (orig, sizeof orig, "SD:/", name); }
	fs_dirname (dir, sizeof dir, orig);
	kapi_mkdir (dir);				// (fails harmlessly if it exists)
	fs_unique_name (dst, sizeof dst, dir, fs_basename (orig), " (restored)");
	if (kapi_rename (src, dst) != 0) return false;
	char info[FS_PATHL]; trash_info_path_ (info, sizeof info, name);
	kapi_remove (info);
	if (where) fs_copy (where, dst, wcap);
	return true;
}
SK_FN bool trash_purge (const char *name)
{
	char p[FS_PATHL], info[FS_PATHL];
	fs_join (p, sizeof p, TRASH_FILES, name);
	trash_info_path_ (info, sizeof info, name);
	kapi_remove (info);
	return fs_remove_tree (p);
}
SK_FN int trash_count (void)
{
	void *d = kapi_opendir (TRASH_FILES);
	if (!d) return 0;
	struct kapi_dirent ent; int n = 0;
	while (kapi_readdir (d, &ent)) if (!fs_skip_dot (ent.name)) n++;
	kapi_closedir (d);
	return n;
}
SK_FN int trash_empty (void)
{
	int n = 0;
	for (;;)
	{
		void *d = kapi_opendir (TRASH_FILES);
		if (!d) break;
		struct kapi_dirent ent; char name[FS_NAMEL]; bool any = false;
		while (kapi_readdir (d, &ent)) if (!fs_skip_dot (ent.name)) { fs_copy (name, ent.name, sizeof name); any = true; break; }
		kapi_closedir (d);
		if (!any || !trash_purge (name)) break;
		n++;
	}
	return n;
}

#undef SK_FN
