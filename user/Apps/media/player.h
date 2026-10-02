//
// Apps/media/player.h -- Media Player's playback: a thread that opens the song asked for (decode.h),
// decodes it ahead and feeds the sound output (kapi_sound_write, s16 stereo at SOUND_RATE: ~0.1 s
// queued, kapi_sound_config), at the volume chosen. The window's thread asks (play, pause, seek,
// stop) and reads its state (playing, the position heard, the length, the end reached) at each tick:
// they share a few words under a lock (kapi_lock).
//
// No sound output (the PC's simulator, or none on the Pi): the song is decoded and timed all the
// same, silent -- the position moves as if heard. The output used by another app: an error.
//
#ifndef _media_player_h
#define _media_player_h

#include "decode.h"

namespace media {

enum { PS_STOPPED, PS_LOADING, PS_PLAYING, PS_PAUSED };

class Player
{
public:
	// read by the window (under lk)
	volatile int state;			// PS_*
	volatile i64 posMs, lenMs;		// what is heard now, the song's length
	volatile int endedGen;			// bumped when a song ends by itself (the window plays the next)
	volatile int errGen; char err[160];	// bumped with an error (err: why)
	char fmt[8]; int rate, bits, kbps, channels;	// the song playing (its description)
	int sound;				// 1 the output is ours, -1 none (silent), 0 not asked yet
	volatile int volume;			// 0..100
	volatile int lk;

	Player () : state (PS_STOPPED), posMs (0), lenMs (0), endedGen (0), errGen (0), rate (0), bits (0), kbps (0), channels (0),
		sound (0), volume (80), lk (0), released (0), m_cmd (CMD_NONE), m_seekMs (0), m_stream (0), m_written (0), m_base (0), m_cap (0), m_quit (false), m_tid (-1), m_t0 (0)
	{ err[0] = fmt[0] = 0; m_path[0] = 0; }

	void start () { m_tid = kapi_thread_create (thread_main, this, 256 * 1024, "player"); }
	void quit () { m_quit = true; if (m_tid > 0) kapi_thread_join (m_tid, 3000, 0); }

	void play (const char *path) { kapi_lock (&lk); snprintf (m_path, sizeof m_path, "%s", path); m_cmd = CMD_OPEN; state = PS_LOADING; posMs = 0; kapi_unlock (&lk); }
	void pause () { kapi_lock (&lk); if (state == PS_PLAYING) m_cmd = CMD_PAUSE; kapi_unlock (&lk); }
	void resume () { kapi_lock (&lk); if (state == PS_PAUSED) m_cmd = CMD_RESUME; kapi_unlock (&lk); }
	void stop () { kapi_lock (&lk); m_cmd = CMD_STOP; kapi_unlock (&lk); }
	// give the sound output back (a video takes it): the song stopped; released bumped once done
	void release () { kapi_lock (&lk); m_cmd = CMD_RELEASE; kapi_unlock (&lk); }
	volatile int released;
	void seek (i64 ms) { kapi_lock (&lk); m_seekMs = ms < 0 ? 0 : ms; if (m_cmd == CMD_NONE || m_cmd == CMD_SEEK) m_cmd = CMD_SEEK; posMs = m_seekMs; kapi_unlock (&lk); }

private:
	enum { CMD_NONE, CMD_OPEN, CMD_PAUSE, CMD_RESUME, CMD_STOP, CMD_SEEK, CMD_RELEASE };
	enum { CHUNK = 1024 };
	int m_cmd; i64 m_seekMs; char m_path[300];
	Stream *m_stream; i64 m_written;		// frames sent since the song's (or the seek's) position 0
	i64 m_base;					// the position (frames) m_written counts from
	unsigned m_cap;				// the output's queue, frames (its free frames when empty)
	bool m_quit; int m_tid;
	unsigned m_t0;				// (no output: the time the song plays by)

	static int thread_main (void *p) { ((Player *) p)->run (); return 0; }

	unsigned queued ()
	{
		if (sound != 1) return 0;
		unsigned rate_ = 0, fr = 0, own = 0; kapi_sound_status (&rate_, &fr, &own);
		return fr < m_cap ? m_cap - fr : 0;
	}
	void set_error (const char *e) { kapi_lock (&lk); snprintf (err, sizeof err, "%s", e); errGen++; state = PS_STOPPED; kapi_unlock (&lk); }
	void ensure_sound ()
	{
		if (sound) return;
		int r = kapi_sound_acquire ();
		if (r == 1)
		{
			sound = 1;
			kapi_sound_config (CHUNK, 3);
			unsigned rt, fr, own; kapi_sound_status (&rt, &fr, &own); m_cap = fr ? fr : 22050;
		}
		else if (r == 0) { sound = 0; set_error ("The sound output is used by another app: close it to hear the music."); }
		else sound = -1;
	}
	void close_song () { delete m_stream; m_stream = 0; }
	void open_song (const char *path)
	{
		close_song ();
		char e[160];
		Decoder *d = decoder_open (path, e, sizeof e);
		if (!d) { set_error (e); return; }
		m_stream = new Stream (d);
		kapi_lock (&lk);
		snprintf (fmt, sizeof fmt, "%s", d->format); rate = d->rate; bits = d->bits; kbps = d->kbps; channels = d->channels;
		lenMs = m_stream->lengthMs (); posMs = 0; state = PS_PLAYING;
		kapi_unlock (&lk);
		m_base = 0; m_written = 0; m_t0 = kapi_get_ticks ();
	}
	void apply_volume (short *b, int n)
	{
		int v = volume; if (v >= 100) return;
		int g = v * v * 65536 / 10000;			// (a square law: the ear's)
		for (int i = 0; i < 2 * n; i++) b[i] = (short) ((b[i] * g) >> 16);
	}
	void run ()
	{
		short *buf = new short[2 * CHUNK];
		while (!m_quit)
		{
			kapi_lock (&lk);
			int cmd = m_cmd; m_cmd = CMD_NONE; i64 seekMs = m_seekMs; char path[300]; memcpy (path, m_path, sizeof path);
			kapi_unlock (&lk);
			switch (cmd)
			{
			case CMD_OPEN: ensure_sound (); open_song (path); break;
			case CMD_PAUSE: kapi_lock (&lk); if (state == PS_PLAYING) state = PS_PAUSED; kapi_unlock (&lk); break;
			case CMD_RESUME:
				kapi_lock (&lk);
				if (state == PS_PAUSED) { state = PS_PLAYING; m_t0 = kapi_get_ticks (); m_base = posMs * SOUND_RATE / 1000; m_written = 0; }
				kapi_unlock (&lk);
				break;
			case CMD_STOP: close_song (); kapi_lock (&lk); state = PS_STOPPED; posMs = 0; kapi_unlock (&lk); break;
			case CMD_RELEASE:
				close_song (); kapi_lock (&lk); state = PS_STOPPED; posMs = 0; kapi_unlock (&lk);
				if (sound == 1) kapi_sound_release ();
				sound = 0; released++;
				break;
			case CMD_SEEK:
				if (m_stream) { m_stream->seekMs (seekMs); m_base = seekMs * SOUND_RATE / 1000; m_written = 0; m_t0 = kapi_get_ticks (); }
				break;
			}
			if (!m_stream || state != PS_PLAYING) { kapi_msleep (10); continue; }
			// room in the output?
			if (sound == 1)
			{
				unsigned q = queued ();
				if (q + CHUNK > m_cap) { kapi_lock (&lk); posMs = (m_base + m_written - q) * 1000 / SOUND_RATE; kapi_unlock (&lk); kapi_msleep (8); continue; }
			}
			else
			{	// silent: as much as the time gone allows
				i64 due = (i64) (kapi_get_ticks () - m_t0) * SOUND_RATE / 100;
				kapi_lock (&lk); posMs = (m_base + (m_written < due ? m_written : due)) * 1000 / SOUND_RATE; kapi_unlock (&lk);
				if (m_written > due + CHUNK) { kapi_msleep (10); continue; }
			}
			int n = m_stream->read (buf, CHUNK);
			if (n <= 0)
			{	// the end: let the output play out what it has
				while (sound == 1 && queued () > 0 && !m_quit && m_cmd == CMD_NONE) { kapi_lock (&lk); posMs = (m_base + m_written - queued ()) * 1000 / SOUND_RATE; kapi_unlock (&lk); kapi_msleep (10); }
				if (m_cmd != CMD_NONE) continue;
				close_song ();
				kapi_lock (&lk); state = PS_STOPPED; posMs = lenMs; endedGen++; kapi_unlock (&lk);
				continue;
			}
			apply_volume (buf, n);
			if (sound == 1)
			{
				int off = 0;
				while (off < n && !m_quit)
				{
					int w = kapi_sound_write (buf + 2 * off, (unsigned) (n - off));
					if (w <= 0) { kapi_msleep (5); continue; }
					off += w;
				}
			}
			m_written += n;
		}
		close_song ();
		delete[] buf;
	}
};

} // namespace media

#endif
