//
// tone -- play a note on the audio output (ABI v46 sound), to test the sound system.
//   usage: tone [frequency Hz [milliseconds [wave]]]      (default 440 Hz, 500 ms, sine)
//   wave: square, sine, triangle, saw, noise.   "tone scale": a C major scale.
//
#include "kapi.h"
#include "applib.h"

static int num (const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

int main (void)
{
	char a[128]; kapi_get_args (a, sizeof a);
	char *w[3] = { 0, 0, 0 }; int n = 0;
	for (char *p = a; *p && n < 3; )
	{
		while (*p == ' ') p++;
		if (!*p) break;
		w[n++] = p; while (*p && *p != ' ') p++;
		if (*p) *p++ = 0;
	}
	int r = kapi_sound_acquire ();
	if (r <= 0) { ax_putln (r < 0 ? "tone: no audio output" : "tone: the audio output is used by another program"); return 1; }
	if (n && ax_streq (w[0], "scale"))
	{
		static const unsigned f[] = { 261626, 293665, 329628, 349228, 391995, 440000, 493883, 523251 };
		for (int i = 0; i < 8; i++) { kapi_sound_start (0, f[i], SOUND_TRIANGLE, 200); kapi_msleep (250); kapi_sound_stop (0); kapi_msleep (30); }
	}
	else
	{
		int hz = n > 0 ? num (w[0]) : 440, ms = n > 1 ? num (w[1]) : 500, wave = SOUND_SINE;
		if (n > 2)
		{
			static const char *names[] = { "square", "sine", "triangle", "saw", "noise" };
			for (int i = 0; i < 5; i++) if (ax_streq (w[2], names[i])) wave = i;
		}
		kapi_sound_start (0, (unsigned) hz * 1000, wave, 200);
		kapi_msleep ((unsigned) ms);
		kapi_sound_stop (0);
	}
	kapi_msleep (20);				// let the release ramp end
	kapi_sound_release ();
	return 0;
}
