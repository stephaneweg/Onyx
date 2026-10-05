//
// ringtest -- the low-latency sound of kapi v68: becomes the sound owner, asks for small
// chunks (kapi_sound_config, "ringtest <chunk> <ahead>", default 256 2), maps the PCM ring
// (kapi_sound_map) and plays 3 s of a 440 Hz triangle written by an APP CORE straight into
// the ring (no kapi call there), keeping it only ~2 chunks full. Meanwhile the main thread
// sleeps on the ring's read index with kapi_wait_word (the kernel's tick notices core 1
// moving it). Prints the latency the kernel reports, the underruns (`dry`) and PASS / FAIL.
// Without a free app core the main thread fills the ring itself.
//
#include "kapi.h"
#include "applib.h"

static void put_num (long v)
{
	char b[24]; int n = 0;
	if (v < 0) { kapi_stdout_write ("-", 1); v = -v; }
	do { b[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	while (n > 0) kapi_stdout_write (&b[--n], 1);
}

static void say (const char *a, long v, const char *b) { ax_puts (a); put_num (v); ax_putln (b); }

static struct kapi_sound_ring *s_ring;
static volatile unsigned s_target;			// frames to keep queued
static volatile unsigned s_total;			// frames to play
static volatile unsigned s_done;
static unsigned s_phase;

// Fill the ring up to s_target frames queued with a 440 Hz triangle. -> frames written.
static unsigned fill (void)
{
	short buf[128 * 2];
	unsigned wrote = 0;
	for (;;)
	{
		unsigned queued = s_ring->wr - s_ring->rd;
		if (queued >= s_target || s_done >= s_total) return wrote;
		unsigned n = s_target - queued;
		if (n > 128) n = 128;
		for (unsigned i = 0; i < n; i++)
		{
			s_phase += 440u * 65536u / 44100u * 65536u;	// (a 32-bit phase)
			int t = (int) (s_phase >> 16);			// 0..65535
			int v = (t < 32768 ? t : 65535 - t) - 16384;	// -16384..16383
			buf[i * 2] = buf[i * 2 + 1] = (short) (v / 2);
		}
		unsigned k = kapi_sound_ring_write (s_ring, buf, n);
		s_done += k; wrote += k;
		if (k < n) return wrote;
	}
}

static unsigned char s_Stack[16 * 1024] __attribute__ ((aligned (16)));
static void core_job (void *arg)
{
	(void) arg;
	while (s_done < s_total) fill ();		// (an app core: plain memory only)
}

static int parse (const char *s, int *pos)
{
	int v = 0;
	while (s[*pos] == ' ') (*pos)++;
	while (s[*pos] >= '0' && s[*pos] <= '9') v = v * 10 + (s[(*pos)++] - '0');
	return v;
}

int main (void)
{
	if (kapi_abi_version () < 68) { ax_putln ("ringtest: the kernel is older than v68"); return 1; }
	char args[32]; kapi_get_args (args, sizeof args);
	int p = 0, chunk = parse (args, &p), ahead = parse (args, &p);
	if (chunk == 0) chunk = 256;
	if (ahead == 0) ahead = 2;

	if (kapi_sound_acquire () != 1) { ax_putln ("ringtest: the sound output is busy"); return 1; }
	int lat = kapi_sound_config (chunk, ahead);
	say ("latency (frames): ", lat, "");
	say ("latency (ms x 10): ", lat * 10000L / SOUND_RATE, "");
	s_ring = kapi_sound_map ();
	if (s_ring == 0 || s_ring->magic != KAPI_SOUND_RING_MAGIC) { ax_putln ("ringtest: no ring"); return 1; }
	say ("ring frames: ", s_ring->frames, "");
	say ("chunk / ahead now: ", s_ring->chunk * 100L + s_ring->ahead, " (chunk x 100 + ahead)");

	s_target = (unsigned) chunk * 2;
	s_total = SOUND_RATE * 3;
	unsigned dry0 = s_ring->dry;
	unsigned t0 = kapi_clock_us ();
	int core = kapi_core_acquire ();
	unsigned wakes = 0;
	if (core >= 0)
	{
		ax_putln ("filling from app core");
		kapi_core_run (core, core_job, 0, s_Stack + sizeof s_Stack);
		while (s_done < s_total)
		{
			unsigned rd = s_ring->rd;
			if (kapi_wait_word (&s_ring->rd, rd, 100) == 0) wakes++;
		}
		while (kapi_core_state (core) == KAPI_CORE_RUNNING) kapi_msleep (5);
		kapi_core_release (core);
	}
	else
	{
		ax_putln ("no free app core: filling from the main thread");
		while (s_done < s_total) { fill (); kapi_msleep (1); }
	}
	while (s_ring->wr != s_ring->rd) kapi_msleep (5);	// played out
	unsigned dt = kapi_clock_us () - t0;
	unsigned dry = s_ring->dry - dry0;
	say ("played (ms): ", dt / 1000, "");
	say ("wakes on rd: ", wakes, "");
	say ("underruns: ", dry, "");
	kapi_sound_release ();
	int ok = dt > 2900000 && dt < 3500000;
	ax_putln (ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}
