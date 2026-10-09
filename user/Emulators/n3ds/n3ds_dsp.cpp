//
// n3ds/n3ds_dsp.cpp -- dsp::DSP, the sound processor's service, emulated at a high level: no DSP code is run. A
// game loads the DSP's program (its "component": ignored), then talks to it through a pipe -- it writes
// "initialize" and reads back where, in the DSP's memory, the structures they share are (the sources'
// configurations and states, the mixer's, the samples) --, maps that memory through an address the service
// converts, and from then on fills the structures and waits for the DSP's interrupt at every audio frame
// (160 samples at 32728 Hz: about 4.9 ms).
//
// This slice: that start and the frames' rhythm, so that a game runs -- the sources are not played yet (no
// sound: phase T4). The structures' places below are OURS (a game reads them from the pipe: it does not know
// them), with room for each at its real size.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
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

static void signal (Machine *m, Event *e) { if (e) { e->signaled = true; m->wakeWaiters (e); } }

// An audio frame has passed: the frame counter of both copies of the shared memory, the DSP's interrupt.
void dspFrame (Machine *m)
{
	Machine::Dsp &d = m->dsp;
	d.frames++;
	if (d.ram)
		for (int region = 0; region < 2; region++)
		{
			u8 *counter = d.ram + 0x50000 + region * 0x20000 + (0xBFFF - 0x8000) * 2;
			counter[0] = (u8) d.frames; counter[1] = (u8) (d.frames >> 8);
		}
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
