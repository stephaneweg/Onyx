//
// n3ds/n3ds_dsp.cpp -- dsp::DSP, the sound processor's service, emulated at a high level: no DSP code is run. A
// game loads the DSP's program (its "component": ignored), then talks to it through a pipe -- it writes
// "initialize" and reads back where, in the DSP's memory, the structures they share are (the sources'
// configurations and states, the mixer's, the samples) --, maps that memory through an address the service
// converts, and from then on fills the structures and waits for the DSP's interrupt at every audio frame
// (160 samples at 32728 Hz: about 4.9 ms).
//
// The structures' places below are OURS (a game reads them from the pipe: it does not know them), with room for
// each at its real size. The memory is there twice (two "regions"): the program writes a frame's orders into one,
// raises its frame counter above the other's, and reads the answers there; the DSP takes the one whose counter
// is ahead.
//
// The sources (phase T4): 24 voices, each with a queue of sample buffers in the program's linear memory -- 8-bit
// or 16-bit PCM, mono or stereo, or the DSP's ADPCM (frames of 8 bytes: a header, 14 samples of 4 bits, predicted
// from the two before with a pair of the voice's coefficients) --, a rate (the buffer's samples a mixed sample),
// gains to the front left and right. A frame: each voice's orders read where their "dirty" bits say (then the
// bits cleared), 160 samples made of each playing voice (stepped at its rate, interpolated between two samples),
// summed, the voices' states written back (playing, which buffer, how far), the mix handed to the host
// (Machine::audioRead). Not done: the filters, the auxiliary mixes (the effects), the master volume, the
// compressor, the surround's back channels.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

// The 15 structures, as DSP addresses (16-bit words from 0x8000: the program's address is VA_DSP_RAM + 0x40000 +
// 2 * address). In the order the pipe gives them: the frame counter, the sources' configurations (24 of 192
// bytes), their states (24 of 12), their ADPCM coefficients (24 of 32), the DSP's configuration, its state, the
// final samples, the intermediate mixes, the compressor's table, debug, and five unknown ones.
static const u16 STRUCTS[15] = {
	0xBFFF, 0x8000, 0x8A00, 0x8B00, 0x8D00, 0x8E00, 0x8F00, 0x9200, 0x9E00, 0xA600, 0xA800, 0xA900, 0xAA00, 0xAB00, 0xAC00,
};
enum { DSP_FRAME_TICKS = 1310720 };		// 160 samples at 32728 Hz, in the processor's ticks

// ---- the voices -----------------------------------------------------------------------------------------------------
enum { VOICES = 24, FRAME = 160, QUEUE = 8 };
// (the offsets in the regions, from the structures' addresses)
enum { OFF_COUNTER = (0xBFFF - 0x8000) * 2, OFF_CONFIG = 0, OFF_STATUS = (0x8A00 - 0x8000) * 2, OFF_COEFS = (0x8B00 - 0x8000) * 2, OFF_FINAL = (0x8F00 - 0x8000) * 2 };
struct DspBuffer { u32 address, length; u16 id; bool looping, adpcmGiven; s16 ps, yn1, yn2; u32 start; };
struct DspVoice
{
	bool enabled; u16 sync;
	int channels, format;					// 1 or 2; 0 PCM8, 1 PCM16, 2 ADPCM
	float rate, gainL, gainR;
	s16 coefs[16];
	DspBuffer queue[QUEUE]; int queued;			// waiting, by their ids
	// the buffer being played: its bytes in the host, where the next sample is, the ADPCM's two last
	DspBuffer cur; const u8 *data; bool playing; u32 pos;
	s32 yn1, yn2;
	u16 bufferId; bool bufferChanged;
	// the two samples the output is between (left, right), and how far (16.16)
	s16 s0[2], s1[2]; u32 frac; bool primed;
};

// a 32-bit value as the DSP keeps it: its two halves, the high one first
static inline u32 dsp32 (const u8 *q) { return (u32) (q[0] | q[1] << 8) << 16 | (u32) (q[2] | q[3] << 8); }
static inline void putDsp32 (u8 *q, u32 v) { q[0] = (u8) (v >> 16); q[1] = (u8) (v >> 24); q[2] = (u8) v; q[3] = (u8) (v >> 8); }
static inline u16 le16 (const u8 *q) { return (u16) (q[0] | q[1] << 8); }
static inline float leFloat (const u8 *q) { float f; memcpy (&f, q, 4); return f; }

static void voicePush (DspVoice &v, const DspBuffer &b)
{
	if (!b.address || !b.length || v.queued >= QUEUE) return;
	int at = v.queued;
	while (at > 0 && v.queue[at - 1].id > b.id) { v.queue[at] = v.queue[at - 1]; at--; }	// (played by their ids)
	v.queue[at] = b; v.queued++;
}
// The next buffer of the queue becomes the one played. -> false: none.
static bool voiceNext (Machine *m, DspVoice &v)
{
	while (v.queued)
	{
		v.cur = v.queue[0];
		for (int i = 1; i < v.queued; i++) v.queue[i - 1] = v.queue[i];
		v.queued--;
		const u32 bytes = v.format == 2 ? (v.cur.length + 13) / 14 * 8 : v.cur.length * (u32) v.channels * (v.format == 1 ? 2 : 1);
		v.data = m->physPtr (v.cur.address, bytes);
		if (!v.data) continue;						// (a buffer that is not in memory: left out)
		v.pos = v.cur.start < v.cur.length ? v.cur.start : 0;
		if (v.cur.adpcmGiven) { v.yn1 = v.cur.yn1; v.yn2 = v.cur.yn2; }
		v.bufferId = v.cur.id; v.bufferChanged = true; v.playing = true;
		if (v.cur.looping) { DspBuffer again = v.cur; again.start = 0; voicePush (v, again); }
		return true;
	}
	v.playing = false;
	return false;
}
// The next sample of the buffer played (left, right). -> false: the voice has nothing more.
static bool voiceSample (Machine *m, DspVoice &v, s16 out[2])
{
	if (!v.playing || v.pos >= v.cur.length) { if (!voiceNext (m, v)) return false; }
	const u8 *d = v.data;
	const u32 i = v.pos++;
	switch (v.format)
	{
	case 0:
		if (v.channels == 2) { out[0] = (s16) ((s8) d[i * 2] << 8); out[1] = (s16) ((s8) d[i * 2 + 1] << 8); }
		else out[0] = out[1] = (s16) ((s8) d[i] << 8);
		break;
	case 1:
		if (v.channels == 2) { out[0] = (s16) le16 (d + i * 4); out[1] = (s16) le16 (d + i * 4 + 2); }
		else out[0] = out[1] = (s16) le16 (d + i * 2);
		break;
	default:
	{
		const u8 *f = d + i / 14 * 8;					// the frame: its header, then 14 samples of 4 bits
		const int n = (int) (i % 14), scale = f[0] & 15, idx = f[0] >> 4 & 7;
		const u8 byte = f[1 + n / 2];
		int x = (n & 1) ? byte & 15 : byte >> 4;
		if (x >= 8) x -= 16;
		s32 val = ((x << scale) << 11) + 0x400 + v.coefs[idx * 2] * v.yn1 + v.coefs[idx * 2 + 1] * v.yn2;
		val >>= 11;
		if (val > 32767) val = 32767; else if (val < -32768) val = -32768;
		v.yn2 = v.yn1; v.yn1 = val;
		out[0] = out[1] = (s16) val;
		break;
	}
	}
	return true;
}

// A voice's orders (its 192 bytes of the region) taken where its dirty bits say; the bits cleared.
static void voiceOrders (DspVoice &v, u8 *c, const u8 *coefs)
{
	const u32 dirty = (u32) c[0] | (u32) c[1] << 8 | (u32) c[2] << 16 | (u32) c[3] << 24;
	if (!dirty) return;
	if (dirty & (1u << 29)) { memset (&v, 0, sizeof v); v.rate = 1.0f; v.channels = 1; }			// reset
	if (dirty & (1u << 4)) { v.queued = 0; v.playing = false; v.primed = false; }				// the buffers forgotten
	if (dirty & (1u << 16)) v.enabled = c[0xA0] != 0;
	if (dirty & (1u << 28)) v.sync = le16 (c + 0xA2);
	if (dirty & (1u << 18)) { const float r = leFloat (c + 0x34); if (r > 0.0f && r < 64.0f) v.rate = r; }
	if (dirty & (1u << 25)) { v.gainL = leFloat (c + 0x04); v.gainR = leFloat (c + 0x08); }
	const u16 flags = le16 (c + 0xB4);
	// (the format comes with a first buffer too: the game's library does not flag it apart)
	if (dirty & (2u | 1u << 30)) v.channels = (flags & 3) == 2 ? 2 : 1;
	if (dirty & (1u | 1u << 30)) v.format = (flags >> 2 & 3) > 2 ? 0 : (int) (flags >> 2 & 3);
	if (dirty & 4u) for (int i = 0; i < 16; i++) v.coefs[i] = (s16) le16 (coefs + i * 2);
	if (dirty & (1u << 30))											// the first buffer, in the orders themselves
	{
		DspBuffer b;
		b.address = dsp32 (c + 0xAC); b.length = dsp32 (c + 0xB0);
		b.ps = (s16) le16 (c + 0xB6); b.yn1 = (s16) le16 (c + 0xB8); b.yn2 = (s16) le16 (c + 0xBA);
		const u16 f2 = le16 (c + 0xBC);
		b.adpcmGiven = (f2 & 1) != 0; b.looping = (f2 & 2) != 0;
		b.id = le16 (c + 0xBE);
		b.start = (dirty & (1u << 21)) ? dsp32 (c + 0xA4) : 0;
		voicePush (v, b);
	}
	if (dirty & (1u << 19))											// the queue's four places
	{
		const u16 which = le16 (c + 0x4A);
		for (int i = 0; i < 4; i++)
		{
			if (!(which >> i & 1)) continue;
			const u8 *q = c + 0x4C + i * 20;
			DspBuffer b;
			b.address = dsp32 (q); b.length = dsp32 (q + 4);
			b.ps = (s16) le16 (q + 8); b.yn1 = (s16) le16 (q + 10); b.yn2 = (s16) le16 (q + 12);
			b.adpcmGiven = q[14] != 0; b.looping = q[15] != 0;
			b.id = le16 (q + 16); b.start = 0;
			voicePush (v, b);
		}
		c[0x4A] = c[0x4B] = 0;
	}
	c[0] = c[1] = c[2] = c[3] = 0;
}

// An audio frame: the orders of the region that is ahead, 160 samples mixed, the states written back.
static void dspMix (Machine *m)
{
	Machine::Dsp &d = m->dsp;
	if (!d.voices)
	{
		d.voices = (DspVoice *) calloc (VOICES, sizeof (DspVoice));
		d.out = (s16 *) calloc (Machine::Dsp::OUT_FRAMES * 2, sizeof (s16));
		if (!d.voices || !d.out) { free (d.voices); free (d.out); d.voices = 0; d.out = 0; return; }
		for (int i = 0; i < VOICES; i++) { d.voices[i].rate = 1.0f; d.voices[i].channels = 1; }
	}
	u8 *r0 = d.ram + 0x50000, *r1 = d.ram + 0x70000;
	const u16 c0 = le16 (r0 + OFF_COUNTER), c1 = le16 (r1 + OFF_COUNTER);
	u8 *r = (c0 == 0xFFFF && c1 != 0xFFFE) ? r1 : (c1 == 0xFFFF && c0 != 0xFFFE) ? r0 : c0 > c1 ? r0 : r1;	// (the one ahead; the counter wraps)
	static s32 mix[FRAME][2];
	memset (mix, 0, sizeof mix);
	bool any = false;
	for (int i = 0; i < VOICES; i++)
	{
		DspVoice &v = d.voices[i];
		voiceOrders (v, r + OFF_CONFIG + i * 0xC0, r + OFF_COEFS + i * 32);
		if (v.enabled)
		{
			const u32 step = (u32) (v.rate * 65536.0f);
			const s32 gl = (s32) (v.gainL * 4096.0f), gr = (s32) (v.gainR * 4096.0f);
			for (int n = 0; n < FRAME; n++)
			{
				if (!v.primed) { if (!voiceSample (m, v, v.s0) || !voiceSample (m, v, v.s1)) { v.enabled = false; v.bufferId = 0; v.bufferChanged = true; break; } v.primed = true; v.frac = 0; }
				const s32 f = (s32) (v.frac >> 4);						// (12 bits)
				mix[n][0] += (((v.s0[0] * (4096 - f) + v.s1[0] * f) >> 12) * gl) >> 12;
				mix[n][1] += (((v.s0[1] * (4096 - f) + v.s1[1] * f) >> 12) * gr) >> 12;
				v.frac += step;
				bool ended = false;
				while (v.frac >= 65536)
				{
					v.frac -= 65536;
					v.s0[0] = v.s1[0]; v.s0[1] = v.s1[1];
					if (!voiceSample (m, v, v.s1)) { ended = true; break; }
				}
				if (ended) { v.enabled = false; v.primed = false; v.bufferId = 0; v.bufferChanged = true; break; }
			}
			any = true;
		}
		u8 *st = r + OFF_STATUS + i * 12;
		st[0] = v.enabled ? 1 : 0; st[1] = v.bufferChanged ? 1 : 0;
		st[2] = (u8) v.sync; st[3] = (u8) (v.sync >> 8);
		putDsp32 (st + 4, v.playing ? v.pos : 0);
		st[8] = (u8) v.bufferId; st[9] = (u8) (v.bufferId >> 8);
		v.bufferChanged = false;
	}
	if (any) d.mixed++;
	u8 *fin = r + OFF_FINAL;
	for (int n = 0; n < FRAME; n++)
	{
		s16 o[2];
		for (int k = 0; k < 2; k++) { const s32 x = mix[n][k]; o[k] = (s16) (x > 32767 ? 32767 : x < -32768 ? -32768 : x); }
		fin[n * 4] = (u8) o[0]; fin[n * 4 + 1] = (u8) (o[0] >> 8); fin[n * 4 + 2] = (u8) o[1]; fin[n * 4 + 3] = (u8) (o[1] >> 8);
		const u32 next = (d.outWrite + 1) % Machine::Dsp::OUT_FRAMES;
		if (next == d.outRead) continue;						// (the host does not take it: lost)
		d.out[d.outWrite * 2] = o[0]; d.out[d.outWrite * 2 + 1] = o[1];
		d.outWrite = next;
	}
}

int Machine::audioRead (s16 *dst, int frames)
{
	int n = 0;
	while (n < frames && dsp.out && dsp.outRead != dsp.outWrite)
	{
		dst[n * 2] = dsp.out[dsp.outRead * 2]; dst[n * 2 + 1] = dsp.out[dsp.outRead * 2 + 1];
		dsp.outRead = (dsp.outRead + 1) % Dsp::OUT_FRAMES;
		n++;
	}
	return n;
}

static void signal (Machine *m, Event *e) { if (e) { e->signaled = true; m->wakeWaiters (e); } }

// An audio frame has passed: the frame counter of both copies of the shared memory, the DSP's interrupt.
void dspFrame (Machine *m)
{
	Machine::Dsp &d = m->dsp;
	d.frames++;
	if (d.ram) dspMix (m);
	signal (m, d.interrupt);
	signal (m, d.semaphore);
}

void dspRequest (Machine *m, Session *s, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	Machine::Dsp &d = m->dsp;
	const u32 *stat = cmd + 0x40;						// (the thread's static buffers: where a pipe's data goes)
	switch (id)
	{
	case 0x01:								// RecvData (register) -> 1: the program runs
	case 0x02:								// RecvDataIsReady (register) -> yes
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 1;
		break;
	case 0x07:								// SetSemaphore (value)
	case 0x12:								// UnloadComponent
	case 0x13:								// FlushDataCache
	case 0x14:								// InvalidateDCache
	case 0x17:								// SetSemaphoreMask
		if (id == 0x12) d.on = false;
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x0C:								// ConvertProcessAddressFromDspDram (address) -> the program's address
		cmd[0] = ipcHeader (id, 2, 0); cmd[2] = VA_DSP_RAM + 0x40000 + (cmd[1] << 1); cmd[1] = RES_OK;
		break;
	case 0x0D:								// WriteProcessPipe (pipe, size, the data): on the audio pipe, a state change
	{
		const u32 pipe = cmd[1], size = cmd[2], src = cmd[4];
		if (pipe == 2 && size >= 2 && m->mem.r16 (src) == 0)		// initialize: the structures' addresses can be read back
		{
			d.pipe[0] = 15;
			memcpy (d.pipe + 1, STRUCTS, sizeof STRUCTS);
			d.pipeLen = 32; d.pipePos = 0;
			memset (d.ram + 0x50000, 0, 0x8000); memset (d.ram + 0x70000, 0, 0x8000);
			d.on = true; d.nextTick = m->now + DSP_FRAME_TICKS;
		}
		else if (pipe != 2) m->note ("dsp::DSP pipe %u", (unsigned) pipe);
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	}
	case 0x0E:								// ReadPipe (pipe, -, size) -> into the static buffer
	case 0x10:								// ReadPipeIfPossible (the same, what there is)
	{
		u32 n = cmd[3] & 0xFFFF;
		if (cmd[1] != 2 || n > d.pipeLen - d.pipePos) n = cmd[1] == 2 ? d.pipeLen - d.pipePos : 0;
		if (n) m->mem.write (stat[1], (const u8 *) d.pipe + d.pipePos, n);
		d.pipePos += n;
		cmd[0] = ipcHeader (id, 2, 2); cmd[1] = RES_OK; cmd[2] = n; cmd[3] = n << 14 | 2; cmd[4] = stat[1];
		break;
	}
	case 0x0F:								// GetPipeReadableSize (pipe) -> bytes
		cmd[0] = ipcHeader (id, 2, 0); cmd[2] = cmd[1] == 2 ? d.pipeLen - d.pipePos : 0; cmd[1] = RES_OK;
		break;
	case 0x11:								// LoadComponent (size, masks, the program) -> loaded
		cmd[0] = ipcHeader (id, 2, 2); cmd[1] = RES_OK; cmd[2] = 1; cmd[3] = cmd[4]; cmd[4] = cmd[5];
		break;
	case 0x15:								// RegisterInterruptEvents (interrupt, pipe, an event)
	{
		Event *e = (Event *) m->handleGet (cmd[4], OBJ_EVENT);
		if (e) e->refs++;
		if (cmd[2] == 2 || !e) { m->release (d.interrupt); d.interrupt = e; } else m->release (e);
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	}
	case 0x16:								// GetSemaphoreEventHandle -> an event
	{
		if (!d.semaphore) d.semaphore = new Event (RESET_ONESHOT);
		d.semaphore->refs++;
		cmd[0] = ipcHeader (id, 1, 2); cmd[1] = RES_OK; cmd[2] = 0; cmd[3] = m->handleNew (d.semaphore);
		break;
	}
	case 0x1F:								// GetHeadphoneStatus -> none
		cmd[0] = ipcHeader (id, 2, 0); cmd[1] = RES_OK; cmd[2] = 0;
		break;
	default:
		ipcStub (m, s->name, cmd);
		break;
	}
}

}
