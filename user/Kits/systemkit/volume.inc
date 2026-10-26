//
// volume.inc -- the code of volume.h (SystemKit: compiled into systemkit.so; a PC build has it inline).
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
SK_FN const char *volume_output_word (int out)
{
	return out == KAPI_SND_OUT_JACK ? "jack" : out == KAPI_SND_OUT_USB ? "usb" : out == KAPI_SND_OUT_HDMI ? "hdmi" : "auto";
}
SK_FN const char *volume_input_word (int in)
{
	return in == KAPI_SND_IN_NONE ? "none" : in == KAPI_SND_IN_USB ? "usb" : in == KAPI_SND_IN_I2S ? "i2s" : "auto";
}
SK_FN void volume_save (int vol, int mute)
{
	char b[640]; int n = 0;
	bool right = false;				// (an I2S microphone's right channel: a line written by hand, kept)
	{
		char old[1024]; void *f = kapi_open (VOLUME_INI); int k = 0;
		if (f != 0) { k = kapi_read (f, old, sizeof old - 1); kapi_close (f); if (k < 0) k = 0; }
		old[k] = 0;
		static const char key[] = "input_right";
		for (int i = 0; i + 11 < k; i++)
		{
			if (i > 0 && old[i - 1] != '\n') continue;
			int m = 0; while (m < 11 && old[i + m] == key[m]) m++;
			if (m == 11)
			{
				int j = i + 11; while (old[j] == ' ' || old[j] == '=') j++;
				right = old[j] == '1';
			}
		}
	}
	const char *h = "; the master volume (0..10) and mute: the menu bar and /bin/volume\nvolume = ";
	for (int i = 0; h[i]; i++) b[n++] = h[i];
	if (vol >= 10) { b[n++] = '1'; b[n++] = '0'; } else b[n++] = (char) ('0' + (vol < 0 ? 0 : vol));
	const char *m = "\nmute = ";
	for (int i = 0; m[i]; i++) b[n++] = m[i];
	b[n++] = mute ? '1' : '0'; b[n++] = '\n';
	int o = kapi_sound_output (-1);			// (the output asked for: the kernel has it, from this file)
	if (o >= 0)
	{
		const char *t = "; the output: auto (a USB headset if there is one, else the jack), jack, usb, hdmi\noutput = ";
		for (int i = 0; t[i]; i++) b[n++] = t[i];
		const char *w = volume_output_word (KAPI_SND_OUT_ASKED (o));
		for (int i = 0; w[i]; i++) b[n++] = w[i];
		b[n++] = '\n';
	}
	long in = kapi_sound_input (KAPI_SND_IN_SELECT, -1, -1);	// (v99: the microphone, its gain)
	if (in >= 0)
	{
		const char *t = "; the input (a microphone): auto (a USB one if there is one), none, usb, i2s (GPIO 18 clock, 19 frame, 20 data)\ninput = ";
		for (int i = 0; t[i]; i++) b[n++] = t[i];
		const char *w = volume_input_word (KAPI_SND_IN_ASKED (in));
		for (int i = 0; w[i]; i++) b[n++] = w[i];
		b[n++] = '\n';
		if (KAPI_SND_IN_GAIN_SET (in))
		{
			const char *g = "input_gain = ";
			for (int i = 0; g[i]; i++) b[n++] = g[i];
			n += ax_itoa ((int) KAPI_SND_IN_GAIN (in), b + n);
			b[n++] = '\n';
		}
		if (right) { const char *r = "input_right = 1\n"; for (int i = 0; r[i]; i++) b[n++] = r[i]; }
	}
	kapi_save_file (VOLUME_INI, b, (unsigned) n);
}
SK_FN int volume_set_output (int out)
{
	int o = kapi_sound_output (out);
	int r = kapi_sound_volume (-1, -1);
	if (o >= 0) volume_save (r & 0xFF, (r & 0x100) ? 1 : 0);
	return o;
}
SK_FN long volume_set_input (int in, int gain_db)
{
	long r = kapi_sound_input (KAPI_SND_IN_SELECT, in, gain_db);
	int v = kapi_sound_volume (-1, -1);
	if (r >= 0) volume_save (v & 0xFF, (v & 0x100) ? 1 : 0);
	return r;
}
SK_FN int mixer_set (const struct kapi_sound_client *c, int volume, int mute)
{
	int r = kapi_sound_client_volume (c->pid, volume, mute);
	if (r < 0) return r;
	static char old[4096], out[4400];
	int n = 0, o = 0;
	void *f = kapi_open (MIXER_INI);
	if (f != 0) { n = kapi_read (f, old, sizeof old - 1); kapi_close (f); if (n < 0) n = 0; }
	old[n] = 0;
	int nl = ax_strlen (c->name);
	if (n == 0)
	{
		const char *h = "; the mixer: each program's own volume (0..100) and mute -- the Sound applet, /bin/volume\n";
		for (int i = 0; h[i]; i++) out[o++] = h[i];
	}
	for (int i = 0; i < n; )				// the other programs' lines kept
	{
		int e = i; while (e < n && old[e] != '\n') e++;
		int same = e - i > nl && (old[i + nl] == ' ' || old[i + nl] == '=' || old[i + nl] == '.');
		for (int k = 0; same && k < nl; k++) if (old[i + k] != c->name[k]) same = 0;
		if (!same && e > i && o + (e - i) + 1 < (int) sizeof out - 120) { for (int k = i; k < e; k++) out[o++] = old[k]; out[o++] = '\n'; }
		i = e + 1;
	}
	char num[16];
	for (int k = 0; k < nl; k++) out[o++] = c->name[k];
	out[o++] = ' '; out[o++] = '='; out[o++] = ' ';
	ax_itoa (r & 0xFF, num); for (int k = 0; num[k]; k++) out[o++] = num[k];
	out[o++] = '\n';
	for (int k = 0; k < nl; k++) out[o++] = c->name[k];
	const char *m = ".mute = "; for (int k = 0; m[k]; k++) out[o++] = m[k];
	out[o++] = (r & 0x100) ? '1' : '0'; out[o++] = '\n';
	kapi_save_file (MIXER_INI, out, (unsigned) o);
	return r;
}
SK_FN void volume_restore (void)
{
	if (app_ini_load_path (VOLUME_INI) < 0) return;
	int v = app_ini_get_int (0, "volume", 10), m = app_ini_get_int (0, "mute", 0);
	kapi_sound_volume (v < 0 ? 0 : v > 10 ? 10 : v, m ? 1 : 0);
}

#undef SK_FN
