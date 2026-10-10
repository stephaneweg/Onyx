//
// input.inc -- input.h's bodies (SystemKit; on the PC inline).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifdef SK_IMPL
#define SK_FN	extern "C"
#else
#define SK_FN	static inline
#endif
static inline char in_low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static inline int in_ieq (const char *a, const char *b) { while (*a && in_low (*a) == in_low (*b)) { a++; b++; } return in_low (*a) == in_low (*b); }
static inline int in_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
// A text file with its line starting with key replaced by line (added at its end when there is none) -> 1 written.
static inline int in_set_line (const char *path, const char *key, const char *line)
{
	static char in[8192], out[8400];
	int n = 0;
	void *f = kapi_open (path);
	if (f) { n = kapi_read (f, in, sizeof in - 1); kapi_close (f); if (n < 0) n = 0; }
	in[n] = 0;
	int o = 0, kl = in_len (key), done = 0;
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && in[i] != '\n') i++;
		int e = i; if (i < n) i++;
		int t = s; while (t < e && (in[t] == ' ' || in[t] == '\t')) t++;
		int match = e - t >= kl;
		for (int k = 0; match && k < kl; k++) match = in[t + k] == key[k];
		if (match && !done)
		{
			for (int k = 0; line[k] && o < (int) sizeof out - 2; k++) out[o++] = line[k];
			out[o++] = '\n'; done = 1;
			continue;
		}
		for (int k = s; k < i && o < (int) sizeof out - 2; k++) out[o++] = in[k];
		if (i == n && e == n && n > 0 && in[n - 1] != '\n') out[o++] = '\n';
	}
	if (!done) { for (int k = 0; line[k] && o < (int) sizeof out - 2; k++) out[o++] = line[k]; out[o++] = '\n'; }
	return kapi_save_file (path, out, (unsigned) o) >= 0;
}

SK_FN int input_keymaps (char (*out)[12], int max)
{
	int nk = 0;
	void *d = kapi_opendir ("SD:/etc/keymaps");
	if (d == 0) return 0;
	struct kapi_dirent e;
	while (nk < max && kapi_readdir (d, &e))
	{
		int n = in_len (e.name);
		if (e.is_dir || n < 6 || !in_ieq (e.name + n - 5, ".kmap")) continue;
		int b = n - 5; if (b > 11) b = 11;
		for (int i = 0; i < b; i++) out[nk][i] = e.name[i];
		out[nk][b] = 0;
		nk++;
	}
	kapi_closedir (d);
	for (int i = 1; i < nk; i++)
		for (int j = i; j > 0; j--)
		{
			const char *a = out[j - 1], *b = out[j]; int k = 0;
			while (a[k] && in_low (a[k]) == in_low (b[k])) k++;
			if (in_low (a[k]) <= in_low (b[k])) break;
			char t[12]; for (int q = 0; q < 12; q++) { t[q] = out[j][q]; out[j][q] = out[j - 1][q]; out[j - 1][q] = t[q]; }
		}
	return nk;
}
SK_FN const char *input_keymap_name (const char *code)
{
	static const char *const K[][2] = { { "BE", "Belgian (azerty)" }, { "DE", "German (qwertz)" }, { "DK", "Danish" }, { "DV", "Dvorak" },
		{ "ES", "Spanish" }, { "FR", "French (azerty)" }, { "IT", "Italian" }, { "JP", "Japanese" }, { "NO", "Norwegian" },
		{ "PT", "Portuguese" }, { "SE", "Swedish / Finnish" }, { "UK", "British" }, { "US", "American (qwerty)" } };
	for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) if (in_ieq (K[i][0], code)) return K[i][1];
	return "";
}
SK_FN int input_keymap_now (char *out, int cap) { out[0] = 0; int n = kapi_get_keymap (out, (unsigned) cap); return n > 0 ? n : 0; }
SK_FN int input_keymap_set (const char *code)
{
	if (!ax_load_keymap (code)) return 0;
	char line[24]; int n = 0;
	const char *a = "keyb "; while (*a) line[n++] = *a++;
	for (int i = 0; code[i] && n < 22; i++) line[n++] = code[i];
	line[n] = 0;
	return in_set_line ("SD:/etc/autostart", "keyb ", line) ? INPUT_KEPT : INPUT_TAKEN;
}
SK_FN int input_keymap_kind (const char *code)
{
	if (in_ieq (code, "BE") || in_ieq (code, "FR")) return 1;
	if (in_ieq (code, "DE")) return 2;
	return 0;
}
SK_FN int input_wheel (void)
{
	static char b[2048];
	void *f = kapi_open ("SD:/etc/theme.txt");
	if (!f) return 3;
	int n = kapi_read (f, b, sizeof b - 1); kapi_close (f);
	if (n <= 0) return 3;
	b[n] = 0;
	for (int i = 0; b[i]; i++)
		if ((i == 0 || b[i - 1] == '\n') && !(b[i] != 'w' || b[i + 1] != 'h'))
		{
			const char *k = "wheelspeed"; int j = 0; while (k[j] && b[i + j] == k[j]) j++;
			if (k[j]) continue;
			int p = i + j; while (b[p] == ' ' || b[p] == '=') p++;
			int v = 0; while (b[p] >= '0' && b[p] <= '9') v = v * 10 + (b[p++] - '0');
			return v >= 1 && v <= 16 ? v : 3;
		}
	return 3;
}
SK_FN int input_wheel_save (int lines)
{
	if (lines < 1) lines = 1;
	if (lines > 16) lines = 16;
	char line[24]; int n = 0;
	const char *a = "wheelspeed="; while (*a) line[n++] = *a++;
	if (lines >= 10) line[n++] = (char) ('0' + lines / 10);
	line[n++] = (char) ('0' + lines % 10); line[n] = 0;
	return in_set_line ("SD:/etc/theme.txt", "wheelspeed", line);
}
#undef SK_FN
