//
// nemutest -- NintendoEMU's native core (pc/NintendoEMU/core/nemucore.cpp) on the PC: its C API
// as the .NET front end uses it.
//   nemutest <rom> <frames> [out.ppm]   open, run the frames (keys: none), the last picture
//   nemutest thumb <rom> [out.ppm]      the library's picture of the game
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" {
int ne_system_of (const char *path);
void *ne_open (const char *path, int audioRate, char *err, int errCap);
int ne_system (void *h);
void ne_title (void *h, char *out, int cap);
int ne_fps1000 (void *h);
void ne_set_keys (void *h, const unsigned char *vk);
int ne_run_frame (void *h, int draw);
int ne_audio (void *h, short *lr, int maxFrames);
unsigned ne_video (void *h, unsigned *px, int cap, int *w, int *hh);
int ne_is_3d (void *h);
void ne_close (void *h);
int ne_thumb (const char *path, unsigned *px, unsigned *banner, char *name, int cap);
}

static void ppm (const char *path, const unsigned *px, int w, int h)
{
	FILE *f = fopen (path, "wb"); if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) { fputc ((px[i] >> 16) & 255, f); fputc ((px[i] >> 8) & 255, f); fputc (px[i] & 255, f); }
	fclose (f);
}

int main (int argc, char **argv)
{
	if (argc >= 3 && !strcmp (argv[1], "thumb"))
	{
		static unsigned px[160 * 144], bn[96 * 32]; char name[128];
		int r = ne_thumb (argv[2], px, bn, name, sizeof name);
		printf ("thumb %d \"%s\"\n", r, name);
		if (r == 1 && argc > 3) ppm (argv[3], px, 160, 144);
		if (r == 3 && argc > 3) ppm (argv[3], bn, 96, 32);
		return r ? 0 : 1;
	}
	if (argc < 3) { fprintf (stderr, "nemutest <rom> <frames> [out.ppm]\n"); return 1; }
	char err[256] = "";
	void *h = ne_open (argv[1], 44100, err, sizeof err);
	if (!h) { printf ("open failed: %s\n", err); return 1; }
	char title[64]; ne_title (h, title, sizeof title);
	printf ("system %d, \"%s\", %.3f fps\n", ne_system (h), title, ne_fps1000 (h) / 1000.0);
	unsigned char keys[256]; memset (keys, 0, sizeof keys); ne_set_keys (h, keys);
	int frames = atoi (argv[2]); long audio = 0;
	static short pcm[8192];
	clock_t t0 = clock ();
	for (int i = 0; i < frames; i++)
	{
		if (i == frames / 2) { keys[0x0D] = 1; ne_set_keys (h, keys); }		// Enter (Start) held a moment
		if (i == frames / 2 + 10) { keys[0x0D] = 0; ne_set_keys (h, keys); }
		if (!ne_run_frame (h, 1)) { printf ("stopped at frame %d\n", i); break; }
		int k; while ((k = ne_audio (h, pcm, 4096)) > 0) audio += k;
	}
	double s = (double) (clock () - t0) / CLOCKS_PER_SEC;
	int w = 0, hh = 0;
	static unsigned px[2560 * 2200];
	unsigned serial = ne_video (h, px, (int) (sizeof px / 4), &w, &hh);
	printf ("%d frames in %.2f s (%.0f fps), picture %dx%d (serial %u, %s), sound %ld frames\n", frames, s, frames / (s > 0 ? s : 1), w, hh, serial, ne_is_3d (h) ? "3D" : "2D", audio);
	if (argc > 3 && serial) ppm (argv[3], px, w, hh);
	ne_close (h);
	return serial ? 0 : 1;
}
