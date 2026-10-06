//
// sound.cpp -- the Onyx sound system (see kern/sound.h): the PCM stream and the mapped ring, mixed in
// integer arithmetic (no FP in the kernel) and played by the output chosen; GetChunk () is the
// "producer": zeros while nothing plays, otherwise the owner's frames.
// (The synthesizer -- the 16 voices, the FM instruments -- left the kernel on 2026-10-05: it is
// AudioKit's, user/Kits/audiokit/fmsynth.h, in user space; the kernel only puts sound out.)
//
// Single core (the default Circle build): GetChunk renders the chunk itself, in the DMA
// completion interrupt on core 0 (~1024 frames every 23 ms: cheap).
// ARM_ALLOW_MULTI_CORE: core 1 is the producer -- it sleeps (WFE) until the audio is first
// used, then keeps SND_AHEAD chunks rendered in a ring; GetChunk only copies the next one
// (or zeros if core 1 fell behind).
//
// Low latency (ABI v68, SoundConfig): the device is made once with the biggest chunk
// (SND_CHUNK words); Circle's CDMASoundBuffers programs each DMA transfer with the length
// GetChunk returns, so a shorter chunk is just a shorter return -- nothing re-created, the
// PWM clock untouched. The sound owner picks the chunk (SND_CHUNK_MIN .. SND_FRAMES frames)
// and how many chunks core 1 keeps rendered ahead (1 .. SND_AHEAD); the sound heard lags
// the rendering by about (ahead + 1) chunks (the ahead ring + the DMA buffer queued behind
// the one playing): 1024 x 4 (the default) ~116 ms, 256 x 2 ~17 ms, 128 x 2 ~9 ms. The
// defaults come back when the owner releases the output or dies.
//
// The mapped ring (ABI v68, SoundRing): one 64 KB page (struct kapi_sound_ring) that the
// owner maps (kapi_sound_map) and fills from anywhere -- an app core, which makes no kapi
// call -- and that Render mixes like the PCM stream: the app moves `wr` after the frames,
// the producer moves `rd` after it has taken them. Allocated on first use and kept for
// ever (the producer may be reading it when its owner dies); the kernel only ever indexes
// it masked by its own capacity, whatever the app writes in the header.
//
#ifndef SOUND_HOST_TEST				// (tools/tests/sound: the synth on a PC)
#include <kern/crashlog.h>
#include <kern/sound.h>
#include <kern/gpio.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/usbsoundbasedevice.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/soundcontroller.h>
#include <circle/devicenameservice.h>
#include <circle/machineinfo.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <fatfs/ff.h>
#include <circle/interrupt.h>
#include <circle/spinlock.h>
#include <circle/synchronize.h>
#include <circle/logger.h>
#include <circle/new.h>
#include <circle/util.h>
#endif
#include <kern/kapi_abi.h>				// struct kapi_fm_instrument
#include "sound_resample.h"

#define SND_CHUNK	2048				// words per GetChunk = 1024 stereo frames
#define SND_FRAMES	(SND_CHUNK / 2)
#define SND_STREAM	(SND_RATE / 2)			// PCM ring: 0.5 s of stereo frames
#define SND_AHEAD	4				// (multi-core) chunks rendered ahead: the most
#define SND_CHUNK_MIN	64				// the smallest chunk SoundConfig allows (frames)

#ifdef SOUND_HOST_TEST
#define DataMemBarrier()	do { } while (0)
#endif

// The mixer (v85): one CHANNEL per program that plays -- its own PCM ring, its volume, its mute --,
// all of them added by the producer. (Until v84 one program at a time owned the output.)
#define SND_CLIENTS	8
struct TClient
{
	unsigned nPid;					// 0: free
	s16	*pStream;				// its ring: SND_STREAM stereo frames (made once, kept)
	unsigned nRd, nWr;				// frame indices (mod SND_STREAM)
	int	 nVolume;				// 0..100
	boolean	 bMute;
	u32	 nGain;					// 16.16, from the two (the ear's curve)
	unsigned nChunk, nAhead;			// the latency it asked for (0: no wish)
	unsigned nPeak;					// its loudest sample lately, 0..32767 (the mixer's meter)
	char	 Name[KAPI_SOUND_NAME];
};
static TClient s_Client[SND_CLIENTS];
// The volumes remembered by name (SD:/etc/mixer.ini at the start, then what the mixer sets): a
// program finds its own again each time it plays.
#define SND_REMEMBER	24
struct TRemember { char Name[KAPI_SOUND_NAME]; int nVolume; boolean bMute; };
static TRemember s_Remember[SND_REMEMBER];
static unsigned s_nRemember = 0;
static CSpinLock s_Lock (IRQ_LEVEL);
static volatile boolean s_bRunning = FALSE;

// Low latency (v68): the chunk the producer renders now (frames) and how many it keeps ahead.
static volatile unsigned s_nChunkFrames = SND_FRAMES;
static volatile unsigned s_nAheadCfg = SND_AHEAD;

// The mapped ring (v68): 0 while no program uses it (then Render does not look at it). One program
// at a time has it (s_nRingPid: the first that maps it, until it releases the output).
static struct kapi_sound_ring * volatile s_pRingOn = 0;
static unsigned s_nRingPid = 0;
static boolean s_bRingFlowing = FALSE;			// the ring had frames at the last chunk

static const char From[] = "sound";

static TClient *ClientOf (unsigned nPid)
{
	if (nPid == 0) return 0;
	for (int i = 0; i < SND_CLIENTS; i++) if (s_Client[i].nPid == nPid) return &s_Client[i];
	return 0;
}
static u32 GainOf (int nVolume, boolean bMute)		// 0..100 on a squared curve -> 16.16
{
	if (bMute || nVolume <= 0) return 0;
	if (nVolume >= 100) return 65536;
	return (u32) (nVolume * nVolume) * 65536u / 10000u;
}

// Mix nFrames stereo frames into pOut (s16 L, R): every client's stream at its volume, and the
// mapped ring at its program's. Caller holds s_Lock.
static void Render (s16 *pOut, unsigned nFrames)
{
	static s32 Mix[SND_FRAMES * 2];
	if (nFrames > SND_FRAMES) nFrames = SND_FRAMES;
	memset (Mix, 0, nFrames * 2 * sizeof (s32));

	u32 nRingGain = 65536;
	TClient *pRingClient = 0;
	for (int c = 0; c < SND_CLIENTS; c++)
	{
		TClient &x = s_Client[c];
		if (x.nPid == 0) continue;
		if (x.nPid == s_nRingPid) { nRingGain = x.nGain; pRingClient = &x; }
		unsigned nPeak = 0;
		unsigned nHave = (x.nWr + SND_STREAM - x.nRd) % SND_STREAM;
		if (nHave > nFrames) nHave = nFrames;
		unsigned rd = x.nRd;
		const u32 g = x.nGain;
		for (unsigned f = 0; f < nHave; f++)
		{
			s32 l = x.pStream[rd * 2], r = x.pStream[rd * 2 + 1];
			if (g != 65536) { l = (s32) (((s64) l * g) >> 16); r = (s32) (((s64) r * g) >> 16); }
			Mix[f * 2] += l; Mix[f * 2 + 1] += r;
			unsigned a = (unsigned) (l < 0 ? -l : l); if (a > nPeak) nPeak = a;
			if (++rd == SND_STREAM) rd = 0;
		}
		x.nRd = rd;
		unsigned nOld = x.nPeak - (x.nPeak >> 3);		// the meter falls back by itself
		x.nPeak = nPeak > nOld ? nPeak : nOld;
	}

	// The mapped ring: what the app has written (its index read once, before the frames).
	struct kapi_sound_ring *pRing = s_pRingOn;
	if (pRing != 0)
	{
		unsigned nWr = pRing->wr;
		DataMemBarrier ();
		unsigned nRingRd = pRing->rd;
		unsigned nRingAvail = nWr - nRingRd;
		if (nRingAvail > KAPI_SOUND_RING_FRAMES)	// (nonsense from the app: start again)
		{
			nRingRd = nWr;
			nRingAvail = 0;
		}
		if (nRingAvail > nFrames) nRingAvail = nFrames;
		unsigned nPeak = 0;
		for (unsigned f = 0; f < nRingAvail; f++)
		{
			unsigned i = (nRingRd + f) & (KAPI_SOUND_RING_FRAMES - 1);
			s32 l = pRing->data[i * 2], r = pRing->data[i * 2 + 1];
			if (nRingGain != 65536) { l = (s32) (((s64) l * nRingGain) >> 16); r = (s32) (((s64) r * nRingGain) >> 16); }
			Mix[f * 2] += l; Mix[f * 2 + 1] += r;
			unsigned a = (unsigned) (l < 0 ? -l : l); if (a > nPeak) nPeak = a;
		}
		if (pRingClient != 0 && nPeak > pRingClient->nPeak) pRingClient->nPeak = nPeak;
		DataMemBarrier ();				// (the frames read before rd moves)
		pRing->rd = nRingRd + nRingAvail;
		if (nRingAvail < nFrames)
		{
			if (s_bRingFlowing) pRing->dry++;	// the app did not keep up
			s_bRingFlowing = FALSE;
		}
		else
		{
			s_bRingFlowing = TRUE;
		}
	}

	for (unsigned i = 0; i < nFrames * 2; i++)
	{
		s32 v = Mix[i];
		pOut[i] = (s16) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
}

#ifndef SOUND_HOST_TEST
// ---- the device -------------------------------------------------------------------------------
#ifdef ARM_ALLOW_MULTI_CORE
static s16 s_Ahead[SND_AHEAD][SND_FRAMES * 2];
static unsigned s_nAheadLen[SND_AHEAD];			// each chunk's frames (s_nChunkFrames then)
static volatile unsigned s_nAheadRd = 0, s_nAheadWr = 0;	// chunk counters
#endif

// The master volume (kapi v60): 0..10 on a squared curve (the ear hears it evenly), and mute.
static volatile int s_nVolume = 10;
static volatile boolean s_bMute = FALSE;
static const u32 s_Gain[11] = { 0, 655, 2621, 5898, 10486, 16384, 23593, 32113, 41943, 53084, 65536 };

// ---- the output: COnyxSoundDevice (v84) -----------------------------------------------------------
//
// ONE producer -- the chunks rendered at SND_RATE (core 1's ring, or Render here on a single core) --
// and SEVERAL outputs, one running at a time: the jack (Circle's CPWMSoundBaseDevice), a USB audio
// device (CUSBSoundBaseDevice: a headset, a DAC) and HDMI (CHDMISoundBaseDevice: the screen). Which
// one: SD:/etc/sound.ini's "output = auto | jack | usb | hdmi" (read when the sound first starts),
// changed at once by kapi sound_output (the Sound applet). auto: the USB device if there is one,
// else the jack, else HDMI on a board without a jack (the Pi 400).
//
// The output adapts to the device, the producer and the apps do not change: each output class
// overrides Circle's GetChunk and takes the producer's frames -- the jack at SND_RATE, as before; USB
// and HDMI at 48 kHz through the rate converter (sound_resample.h), in the device's sample format
// (16 or 24 bits for USB, IEC958 frames for HDMI). The master volume is applied here: by the
// output's own controller when it has one (a USB headset's volume, in dB), else in software.
//
// A USB device unplugged: the sound is simply off (no other output takes over); the producer goes
// on and its chunks are dropped (the drain below), so the apps see nothing. Plugged again -- the
// same one or another --, the USB output is made again by SoundPoll and the sound is back.
//
#define OUT_RATE_48K	48000
#define HDMI_CHUNK	(384 * 3)			// words: 576 frames, 12 ms (a multiple of 384)
#define USB_AUDIO_NAME	"uaudio1-1"			// Circle's first USB audio streaming interface

static int s_nOutCfg = KAPI_SND_OUT_AUTO;		// what is asked for
static int s_nOutNow = 0;				// what runs (KAPI_SND_OUT_JACK / _USB / _HDMI), 0: none
static CSoundBaseDevice *s_pDevice = 0;
static boolean s_bStarted = FALSE;			// the sound was started (EnsureDevice)
static boolean s_bCfgSet = FALSE;			// sound.ini read, or sound_output called
static boolean s_bSwitching = FALSE;
static boolean s_bAutoUsb = FALSE;			// auto chose USB once: it stays USB (unplugged: silence)
static int s_nFailed = 0;				// the output whose start failed (not tried again as it is)
static volatile boolean s_bHwVolume = FALSE;		// the output's controller applies the volume

// The producer's next chunk -> its frames (<= nMax) at *ppSrc; SourceDone once it is copied.
static s16 s_SrcTmp[SND_FRAMES * 2];
static inline unsigned SourceTake (const s16 **ppSrc, unsigned nMax, boolean *pTaken)
{
	unsigned nFrames = s_nChunkFrames;
	if (nFrames > nMax) nFrames = nMax;
	*ppSrc = s_SrcTmp;
	*pTaken = FALSE;
#ifdef ARM_ALLOW_MULTI_CORE
	if (s_nAheadRd != s_nAheadWr)
	{
		DataMemBarrier ();
		unsigned nSlot = s_nAheadRd % SND_AHEAD;
		*ppSrc = s_Ahead[nSlot];
		nFrames = s_nAheadLen[nSlot];
		if (nFrames > nMax) nFrames = nMax;
		*pTaken = TRUE;
	}
	else for (unsigned i = 0; i < nFrames * 2; i++) s_SrcTmp[i] = 0;	// core 1 is late
#else
	s_Lock.Acquire ();
	Render (s_SrcTmp, nFrames);
	s_Lock.Release ();
#endif
	return nFrames;
}
static inline void SourceDone (boolean bTaken)
{
#ifdef ARM_ALLOW_MULTI_CORE
	if (bTaken)
	{
		// Freed only now that it is copied: core 1 may render into this slot next.
		DataMemBarrier ();
		s_nAheadRd++;
	}
	asm volatile ("sev");
#else
	(void) bTaken;
#endif
}

// The software gain (16.16): 0 when muted, 1 when the output's own controller has the volume.
static inline s32 SoftGain (void)
{
	if (s_bMute || s_nVolume == 0) return 0;
	return s_bHwVolume ? 65536 : (s32) s_Gain[s_nVolume];
}

// The producer through the rate converter (the 48 kHz outputs), the gain applied.
static TSndResampler s_Rs;
static unsigned SourceMore (short *pBuf, unsigned nMax)
{
	const s16 *pSrc; boolean bTaken;
	unsigned n = SourceTake (&pSrc, nMax, &bTaken);
	memcpy (pBuf, pSrc, n * 2 * sizeof (s16));
	SourceDone (bTaken);
	return n;
}
static void Pull48 (s16 *pOut, unsigned nFrames)
{
	SndResample (s_Rs, pOut, nFrames, SourceMore);
	s32 nGain = SoftGain ();
	if (nGain != 65536) for (unsigned i = 0; i < nFrames * 2; i++) pOut[i] = (s16) (((s64) pOut[i] * nGain) >> 16);
}

// The jack: PWM at SND_RATE. The chunk is as long as the producer made it (SoundConfig): the DMA
// transfer is programmed with the length returned here (<= nChunkSize, the buffer's size).
class COutJack : public CPWMSoundBaseDevice
{
public:
	COutJack (void) : CPWMSoundBaseDevice (CInterruptSystem::Get (), SND_RATE, SND_CHUNK) {}
protected:
	unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override
	{
		const s16 *pSrc; boolean bTaken;
		unsigned nFrames = SourceTake (&pSrc, nChunkSize / 2, &bTaken);
		u32 nRange = (u32) GetRangeMax ();
		s32 nGain = SoftGain ();
		for (unsigned i = 0; i < nFrames * 2; i++)
		{
			s32 v = (s32) (((s64) pSrc[i] * nGain) >> 16);		// (the master volume)
			pBuffer[i] = (u32) (((u64) (v + 32768) * nRange) >> 16);
		}
		SourceDone (bTaken);
		return nFrames * 2;
	}
};

// HDMI (the screen's speakers): 48 kHz, each sample a 24-bit one framed for IEC958.
class COutHDMI : public CHDMISoundBaseDevice
{
public:
	COutHDMI (void) : CHDMISoundBaseDevice (CInterruptSystem::Get (), OUT_RATE_48K, HDMI_CHUNK), m_nFrame (0) {}
protected:
	unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override
	{
		static s16 Tmp[HDMI_CHUNK];
		if (nChunkSize > HDMI_CHUNK) nChunkSize = HDMI_CHUNK;
		Pull48 (Tmp, nChunkSize / 2);
		for (unsigned i = 0; i < nChunkSize; i++)
		{
			pBuffer[i] = ConvertIEC958Sample ((u32) ((s32) Tmp[i] << 8), m_nFrame);
			if (i & 1) m_nFrame = (m_nFrame + 1) % IEC958_FRAMES_PER_BLOCK;
		}
		return nChunkSize;
	}
private:
	unsigned m_nFrame;
};

// A USB audio device: 48 kHz, 16-bit samples -- or 24-bit ones, packed in three bytes each.
#define USB_TMP		1024				// words a transfer asks for at most (1 ms: ~96)
class COutUSB : public CUSBSoundBaseDevice
{
public:
	COutUSB (void) : CUSBSoundBaseDevice (OUT_RATE_48K, DeviceModeTXOnly, 0) {}
protected:
	unsigned GetChunk (s16 *pBuffer, unsigned nChunkSize) override
	{
		Pull48 (pBuffer, nChunkSize / 2);
		return nChunkSize;
	}
	unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override
	{
		static s16 Tmp[USB_TMP];
		if (nChunkSize > USB_TMP) nChunkSize = USB_TMP;
		Pull48 (Tmp, nChunkSize / 2);
		u8 *p = (u8 *) pBuffer;
		for (unsigned i = 0; i < nChunkSize; i++)
		{
			*p++ = 0; *p++ = (u8) Tmp[i]; *p++ = (u8) (Tmp[i] >> 8);	// (the 16 bits, shifted to 24)
		}
		return nChunkSize;
	}
};

static boolean UsbPresent (void)
{
	return CDeviceNameService::Get ()->GetDevice (USB_AUDIO_NAME, FALSE) != 0;
}
static boolean HasJack (void)				// the Pi 400 has none
{
	return CMachineInfo::Get ()->GetMachineModel () != MachineModel400;
}
static const char *OutName (int n)
{
	return n == KAPI_SND_OUT_JACK ? "the jack (PWM)" : n == KAPI_SND_OUT_USB ? "USB" : n == KAPI_SND_OUT_HDMI ? "HDMI" : "none";
}

// The master volume given to the output's own controller when it has one (a USB headset): the
// level's gain in dB below the control's maximum. Else (and for the mute) the software gain.
// Task context (a control transfer).
static void ApplyVolume (void)
{
	s_bHwVolume = FALSE;
	CSoundController *pCtl = s_pDevice != 0 ? s_pDevice->GetController () : 0;
	if (pCtl == 0) return;
	CSoundController::TControlInfo Info = pCtl->GetControlInfo (CSoundController::ControlVolume,
								    CSoundController::JackDefaultOut, CSoundController::ChannelAll);
	if (!Info.Supported || Info.RangeMax <= Info.RangeMin) return;
	static const int dB[11] = { -99, -40, -28, -21, -16, -12, -9, -6, -4, -2, 0 };	// (level / 10) squared
	int nLevel = s_nVolume < 1 ? 1 : s_nVolume;
	int nValue = Info.RangeMax + dB[nLevel];
	if (nValue < Info.RangeMin) nValue = Info.RangeMin;
	if (!pCtl->SetControl (CSoundController::ControlVolume, CSoundController::JackDefaultOut,
			       CSoundController::ChannelAll, nValue)) return;
	s_bHwVolume = TRUE;				// (the mute stays the software's: SoftGain)
}

int SoundVolume (int nVolume, int nMute)
{
	boolean bChanged = FALSE;
	if (nVolume >= 0) { int v = nVolume > 10 ? 10 : nVolume; bChanged = v != s_nVolume; s_nVolume = v; }
	if (nMute >= 0) s_bMute = nMute != 0;
	if (bChanged && s_pDevice != 0 && !s_bSwitching) ApplyVolume ();
	return s_nVolume | (s_bMute ? 0x100 : 0);
}

// No output runs (a USB device unplugged, an output that could not start): the producer's chunks
// are dropped at the rate they would have been played, so that the apps' streams go on. A kernel
// timer, every tick (10 ms): IRQ context, as an output's GetChunk.
static unsigned s_nDrainOwed = 0;
static void DrainTimer (TKernelTimerHandle, void *, void *)
{
	if (s_nOutNow == 0 && s_bRunning)
	{
		s_nDrainOwed += SND_RATE / HZ;
		while (s_nDrainOwed >= s_nChunkFrames)
		{
			const s16 *pSrc; boolean bTaken;
			unsigned n = SourceTake (&pSrc, SND_FRAMES, &bTaken);
			SourceDone (bTaken);
			if (n == 0) break;
			s_nDrainOwed -= n < s_nDrainOwed ? n : s_nDrainOwed;
		}
	}
	else s_nDrainOwed = 0;
	CTimer::Get ()->StartKernelTimer (1, DrainTimer);
}

// The running output stopped and deleted. Task context (it waits for the transfers to end).
static void OutputStop (void)
{
	CSoundBaseDevice *p = s_pDevice;
	if (p == 0) return;
	s_nOutNow = 0;					// (the drain takes over)
	s_bHwVolume = FALSE;
	p->Cancel ();
	for (unsigned n = 0; p->IsActive () && n < 100; n++) CScheduler::Get ()->MsSleep (5);
	s_pDevice = 0;
	delete p;
	GpioPwmClockKeep ();				// (the jack stopped the PWM clock: the header's PWM may need it, kern/gpio.h)
}

static boolean OutputStart (int nOut)
{
	CSoundBaseDevice *p = 0;
	switch (nOut)
	{
	case KAPI_SND_OUT_JACK:	p = new COutJack; break;
	case KAPI_SND_OUT_USB:	if (UsbPresent ()) p = new COutUSB; break;
	case KAPI_SND_OUT_HDMI:	p = new COutHDMI; break;
	}
	if (p == 0) return FALSE;
	SndResampleInit (s_Rs, SND_RATE, OUT_RATE_48K);
	if (!p->Start ())
	{
		CLogger::Get ()->Write (From, LogError, "cannot start the output: %s", OutName (nOut));
		delete p;
		return FALSE;
	}
	s_pDevice = p;
	s_nOutNow = nOut;
	ApplyVolume ();
	CLogger::Get ()->Write (From, LogNotice, "output: %s (%u Hz%s, volume by %s)", OutName (nOut),
				nOut == KAPI_SND_OUT_JACK ? SND_RATE : OUT_RATE_48K,
				nOut == KAPI_SND_OUT_JACK ? "" : ", converted from 44100", s_bHwVolume ? "the device" : "software");
	return TRUE;
}

// What should run now.
static int OutputWanted (void)
{
	int n = s_nOutCfg;
	if (n == KAPI_SND_OUT_AUTO)
	{
		if (UsbPresent ()) s_bAutoUsb = TRUE;
		n = s_bAutoUsb ? KAPI_SND_OUT_USB : HasJack () ? KAPI_SND_OUT_JACK : KAPI_SND_OUT_HDMI;
	}
	return n;
}

// The output made what it should be: started, switched, made again when the USB device is back,
// stopped when it is gone. Task context; called by EnsureDevice, sound_output and SoundPoll.
static void OutputUpdate (void)
{
	if (s_bSwitching) return;
	int nWant = OutputWanted ();
	boolean bUsb = UsbPresent ();
	if (nWant == KAPI_SND_OUT_USB && !bUsb && s_nFailed == KAPI_SND_OUT_USB) s_nFailed = 0;	// (gone: tried again when back)
	if (s_nOutNow == nWant && (nWant != KAPI_SND_OUT_USB || (bUsb && s_pDevice != 0 && s_pDevice->IsActive ()))) return;
	if (s_nOutNow == 0 && (nWant == s_nFailed || (nWant == KAPI_SND_OUT_USB && !bUsb))) return;	// (nothing to do yet)
	s_bSwitching = TRUE;
	if (s_nOutNow != 0)
	{
		if (s_nOutNow == KAPI_SND_OUT_USB && !bUsb)
			CLogger::Get ()->Write (From, LogNotice, "the USB audio device is gone: the sound is off until one is plugged in");
		OutputStop ();
	}
	if (nWant != s_nFailed && (nWant != KAPI_SND_OUT_USB || bUsb))
	{
		if (!OutputStart (nWant)) s_nFailed = nWant;
	}
	s_bSwitching = FALSE;
}

// SD:/etc/sound.ini's "output = ..." (the volume and the mute there are the menu bar's: volume.h).
static void ReadConfig (void)
{
	if (s_bCfgSet) return;
	s_bCfgSet = TRUE;
	FIL File;
	if (f_open (&File, "SD:/etc/sound.ini", FA_READ) != FR_OK) return;
	static char Text[1024];
	UINT nRead = 0;
	if (f_read (&File, Text, sizeof Text - 1, &nRead) != FR_OK) nRead = 0;
	f_close (&File);
	Text[nRead] = '\0';
	for (const char *p = Text; *p != '\0'; )
	{
		while (*p == ' ' || *p == '\t') p++;
		if (strncmp (p, "output", 6) == 0)
		{
			p += 6;
			while (*p == ' ' || *p == '\t' || *p == '=') p++;
			s_nOutCfg = strncmp (p, "jack", 4) == 0 || strncmp (p, "pwm", 3) == 0 ? KAPI_SND_OUT_JACK
				  : strncmp (p, "usb", 3) == 0 ? KAPI_SND_OUT_USB
				  : strncmp (p, "hdmi", 4) == 0 ? KAPI_SND_OUT_HDMI : KAPI_SND_OUT_AUTO;
		}
		while (*p != '\0' && *p != '\n') p++;
		if (*p == '\n') p++;
	}
}

// (kapi v84) nOut -1: nothing changed; KAPI_SND_OUT_*: that output from now on (applied at once when
// the sound runs) -> what runs | what is asked << 8 | the outputs present << 16 (bit n: output n).
int SoundOutput (int nOut)
{
	ReadConfig ();					// (asked before the sound ever started: sound.ini's)
	if (nOut >= KAPI_SND_OUT_AUTO && nOut <= KAPI_SND_OUT_HDMI)
	{
		s_bCfgSet = TRUE;
		if (nOut != s_nOutCfg) { s_nOutCfg = nOut; s_bAutoUsb = FALSE; s_nFailed = 0; }
		if (s_bStarted) OutputUpdate ();
	}
	else if (nOut != -1) return -1;
	unsigned nPresent = (1u << KAPI_SND_OUT_AUTO) | (1u << KAPI_SND_OUT_HDMI)
			  | (HasJack () ? 1u << KAPI_SND_OUT_JACK : 0) | (UsbPresent () ? 1u << KAPI_SND_OUT_USB : 0);
	return s_nOutNow | (s_nOutCfg << 8) | (int) (nPresent << 16);
}

// Every 100 ms, from the kernel's input task (kernel.cpp, beside the USB plug-and-play): the USB
// output follows its device.
void SoundPoll (void)
{
	if (s_bStarted) OutputUpdate ();
}

#ifdef ARM_ALLOW_MULTI_CORE
// Core 1: wait until the audio is started, then keep the ring SND_AHEAD chunks ahead.
static volatile u64 s_ulRenderUs;				// (kapi v80 cpu_stats)
u64 SoundCoreBusyUs (void) { return s_ulRenderUs; }

// The clock in microseconds, 64 bits (the counter: readable on every core).
static inline u64 ClockUs64 (void)
{
	u64 c, f;
	asm volatile ("mrs %0, cntpct_el0" : "=r" (c));
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f != 0 ? c / f * 1000000 + c % f * 1000000 / f : 0;
}

void SoundCoreMain (void)
{
	CrashLogCoreInit ();					// (woken every ~1 ms: core 0 watched)
	for (;;)
	{
		CrashLogCoreCheck ();
		if (!s_bRunning || s_nAheadWr - s_nAheadRd >= s_nAheadCfg) { asm volatile ("wfe"); continue; }
		if (!s_Lock.TryAcquire ()) continue;		// (never stuck behind core 0: the crash watch goes on)
		unsigned nSlot = s_nAheadWr % SND_AHEAD;
		unsigned nFrames = s_nChunkFrames;		// (read under the lock: SoundConfig takes it)
		u64 ulT0 = ClockUs64 ();
		Render (s_Ahead[nSlot], nFrames);
		s_ulRenderUs = s_ulRenderUs + (ClockUs64 () - ulT0);
		s_nAheadLen[nSlot] = nFrames;
		s_Lock.Release ();
		DataMemBarrier ();
		s_nAheadWr++;
	}
}
#endif

// Start the sound on first use: the producer, then the output sound.ini asks for. The producer runs
// whether or not an output could be started (its chunks are dropped meanwhile: DrainTimer).
static boolean EnsureDevice (void)
{
	if (s_bStarted) return TRUE;
	ReadConfig ();
	s_bRunning = TRUE;					// (multi-core: core 1 starts rendering)
#ifdef ARM_ALLOW_MULTI_CORE
	asm volatile ("sev");
	for (unsigned n = 0; n < 1000000 && s_nAheadWr - s_nAheadRd < 2; n++) {}	// a little ahead
#endif
	s_bStarted = TRUE;
	CLogger::Get ()->Write (From, LogNotice, "audio started (the producer: %u Hz, %u-frame chunks%s)", SND_RATE, SND_FRAMES,
#ifdef ARM_ALLOW_MULTI_CORE
				" rendered on core 1"
#else
				""
#endif
				);
	CTimer::Get ()->StartKernelTimer (1, DrainTimer);
	OutputUpdate ();
	return TRUE;
}

#else
static boolean EnsureDevice (void) { s_bRunning = TRUE; return TRUE; }
#endif

// ---- the mixer's memory of the volumes ---------------------------------------------------------------
static boolean NameIs (const char *a, const char *b)
{
	for (unsigned i = 0; i < KAPI_SOUND_NAME; i++) { if (a[i] != b[i]) return FALSE; if (a[i] == '\0') return TRUE; }
	return TRUE;
}
static void NameCopy (char *d, const char *s)
{
	unsigned i = 0;
	if (s != 0) for (; s[i] != '\0' && i < KAPI_SOUND_NAME - 1; i++) d[i] = s[i];
	d[i] = '\0';
}
static TRemember *RememberLocked (const char *pName, boolean bMake)
{
	for (unsigned i = 0; i < s_nRemember; i++) if (NameIs (s_Remember[i].Name, pName)) return &s_Remember[i];
	if (!bMake) return 0;
	TRemember *r = &s_Remember[s_nRemember < SND_REMEMBER ? s_nRemember++ : SND_REMEMBER - 1];	// (full: the last one reused)
	NameCopy (r->Name, pName); r->nVolume = 100; r->bMute = FALSE;
	return r;
}

#ifndef SOUND_HOST_TEST
// SD:/etc/mixer.ini, once: "media = 60", "media.mute = 1" -- what the Sound applet's mixer wrote.
static void ReadMixer (void)
{
	static boolean s_bRead = FALSE;
	if (s_bRead) return;
	s_bRead = TRUE;
	FIL File;
	if (f_open (&File, "SD:/etc/mixer.ini", FA_READ) != FR_OK) return;
	static char Text[2048];
	UINT nRead = 0;
	if (f_read (&File, Text, sizeof Text - 1, &nRead) != FR_OK) nRead = 0;
	f_close (&File);
	Text[nRead] = '\0';
	for (const char *p = Text; *p != '\0'; )
	{
		while (*p == ' ' || *p == '\t') p++;
		if (*p != ';' && *p != '#' && *p != '\n' && *p != '\r' && *p != '\0')
		{
			char Name[KAPI_SOUND_NAME]; unsigned n = 0; boolean bMuteKey = FALSE;
			while (*p != '\0' && *p != '\n' && *p != '=' && *p != ' ' && *p != '\t')
			{
				if (*p == '.' && strncmp (p, ".mute", 5) == 0) { bMuteKey = TRUE; p += 5; break; }
				if (n < KAPI_SOUND_NAME - 1) Name[n++] = *p;
				p++;
			}
			Name[n] = '\0';
			while (*p == ' ' || *p == '\t' || *p == '=') p++;
			int v = 0; boolean bNum = FALSE;
			while (*p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); bNum = TRUE; if (v > 1000) v = 1000; }
			if (n != 0 && bNum)
			{
				s_Lock.Acquire ();
				TRemember *r = RememberLocked (Name, TRUE);
				if (bMuteKey) r->bMute = v != 0; else r->nVolume = v > 100 ? 100 : v;
				s_Lock.Release ();
			}
		}
		while (*p != '\0' && *p != '\n') p++;
		if (*p == '\n') p++;
	}
}
#else
static void ReadMixer (void) {}
#endif

// The chunk and the chunks ahead in force: the SHORTEST any client asked for (a program that wants
// little latency gets it; the others only see their frames leave sooner), else the defaults.
static void ConfigLocked (void)
{
	unsigned nChunk = SND_FRAMES, nAhead = SND_AHEAD;
	for (int i = 0; i < SND_CLIENTS; i++)
	{
		const TClient &x = s_Client[i];
		if (x.nPid == 0) continue;
		if (x.nChunk != 0 && x.nChunk < nChunk) nChunk = x.nChunk;
		if (x.nAhead != 0 && x.nAhead < nAhead) nAhead = x.nAhead;
	}
	s_nChunkFrames = nChunk;
	s_nAheadCfg = nAhead;
	struct kapi_sound_ring *pRing = s_pRingOn;
	if (pRing != 0) { pRing->chunk = nChunk; pRing->ahead = nAhead; }
}

// ---- the API -----------------------------------------------------------------------------------------
// A channel of the mixer for the program (it keeps the one it has): 1, or 0 when the SND_CLIENTS
// channels are all taken, -1 no audio. pName: the program's name (its volume remembered, the mixer's list).
int SoundAcquire (unsigned nPid, const char *pName)
{
	if (nPid == 0) return 0;
	if (!EnsureDevice ()) return -1;
	ReadMixer ();
	s_Lock.Acquire ();
	TClient *x = ClientOf (nPid);
	if (x != 0) { s_Lock.Release (); return 1; }
	int nFree = -1;
	for (int i = 0; i < SND_CLIENTS && nFree < 0; i++) if (s_Client[i].nPid == 0) nFree = i;
	s_Lock.Release ();
	if (nFree < 0) return 0;
	s16 *pNew = 0;
	if (s_Client[nFree].pStream == 0)			// (its ring: made outside the lock, kept for ever)
	{
		pNew = new s16[SND_STREAM * 2];
		if (pNew == 0) return -1;
	}
	s_Lock.Acquire ();
	x = &s_Client[nFree];
	if (x->nPid != 0)					// (taken meanwhile: once more)
	{
		s_Lock.Release ();
		delete [] pNew;
		return SoundAcquire (nPid, pName);
	}
	if (x->pStream == 0) { x->pStream = pNew; pNew = 0; }
	x->nRd = x->nWr = 0;
	x->nChunk = x->nAhead = 0;
	x->nPeak = 0;
	NameCopy (x->Name, pName != 0 && pName[0] != '\0' ? pName : "app");
	TRemember *r = RememberLocked (x->Name, FALSE);
	x->nVolume = r != 0 ? r->nVolume : 100;
	x->bMute = r != 0 ? r->bMute : FALSE;
	x->nGain = GainOf (x->nVolume, x->bMute);
	x->nPid = nPid;
	s_Lock.Release ();
	delete [] pNew;
	return 1;
}

void SoundRelease (unsigned nPid)
{
	s_Lock.Acquire ();
	TClient *x = ClientOf (nPid);
	if (x != 0)
	{
		x->nPid = 0; x->nRd = x->nWr = 0; x->nPeak = 0;
		if (s_nRingPid == nPid) { s_nRingPid = 0; s_pRingOn = 0; s_bRingFlowing = FALSE; }
		ConfigLocked ();
	}
	s_Lock.Release ();
}

void SoundOnProcessGone (unsigned nPid) { SoundRelease (nPid); }

int SoundWrite (unsigned nPid, const s16 *pFrames, unsigned nFrames)
{
	if (pFrames == 0) return -1;
	s_Lock.Acquire ();
	TClient *x = ClientOf (nPid);
	if (x == 0) { s_Lock.Release (); return -1; }
	unsigned n = 0;
	while (n < nFrames)
	{
		unsigned next = (x->nWr + 1) % SND_STREAM;
		if (next == x->nRd) break;				// full
		x->pStream[x->nWr * 2] = pFrames[n * 2];
		x->pStream[x->nWr * 2 + 1] = pFrames[n * 2 + 1];
		x->nWr = next; n++;
	}
	s_Lock.Release ();
	return (int) n;
}

// The caller's own channel: its room. *pOwner: the caller's pid when it has a channel, else 0
// (nobody "owns" the output any more: a program that asks gets a channel).
int SoundStatus (unsigned nPid, unsigned *pRate, unsigned *pFree, unsigned *pOwner)
{
	s_Lock.Acquire ();
	TClient *x = ClientOf (nPid);
	unsigned used = x != 0 ? (x->nWr + SND_STREAM - x->nRd) % SND_STREAM : 0;
	if (pRate) *pRate = SND_RATE;
	if (pFree) *pFree = SND_STREAM - 1 - used;
	if (pOwner) *pOwner = x != 0 ? nPid : 0;
	s_Lock.Release ();
	return s_bRunning ? 1 : 0;
}

// A program asks for less latency (v68): chunks of nChunkFrames, nAhead of them rendered
// ahead (0 or less: no wish). The shortest wish of the programs playing is in force (ConfigLocked).
// The chunks already rendered play out first, so the new latency is reached within ~(old ahead)
// chunks. -> the latency now in frames ((ahead + 1) x chunk), -1 no channel.
int SoundConfig (unsigned nPid, int nChunkFrames, int nAhead)
{
	if (nChunkFrames < 0) nChunkFrames = 0;
	if (nChunkFrames != 0 && nChunkFrames < SND_CHUNK_MIN) nChunkFrames = SND_CHUNK_MIN;
	if (nChunkFrames > SND_FRAMES) nChunkFrames = SND_FRAMES;
	if (nAhead < 0) nAhead = 0;
	if (nAhead > SND_AHEAD) nAhead = SND_AHEAD;
	s_Lock.Acquire ();
	TClient *x = ClientOf (nPid);
	if (x == 0) { s_Lock.Release (); return -1; }
	x->nChunk = (unsigned) nChunkFrames;
	x->nAhead = (unsigned) nAhead;
	ConfigLocked ();
	int r = (int) ((s_nAheadCfg + 1) * s_nChunkFrames);
	s_Lock.Release ();
	return r;
}

// (v85) The mixer's channels -> how many (pOut: up to nMax of them).
int SoundClients (struct kapi_sound_client *pOut, int nMax)
{
	int n = 0;
	s_Lock.Acquire ();
	for (int i = 0; i < SND_CLIENTS; i++)
	{
		const TClient &x = s_Client[i];
		if (x.nPid == 0) continue;
		if (pOut != 0 && n < nMax)
		{
			struct kapi_sound_client &o = pOut[n];
			memset (&o, 0, sizeof o);
			o.pid = x.nPid; o.volume = x.nVolume; o.mute = x.bMute ? 1 : 0;
			o.peak = (int) (x.nPeak > 32767 ? 32767 : x.nPeak);
			o.queued = (int) ((x.nWr + SND_STREAM - x.nRd) % SND_STREAM);
			NameCopy (o.name, x.Name);
		}
		n++;
	}
	s_Lock.Release ();
	return n;
}

// (v85) A channel's volume (0..100; -1: kept) and mute (0 / 1; -1: kept), remembered for the
// program's name -> volume | 0x100 if muted, -1: no such channel.
int SoundClientVolume (unsigned nPid, int nVolume, int nMute)
{
	s_Lock.Acquire ();
	TClient *x = ClientOf (nPid);
	if (x == 0) { s_Lock.Release (); return -1; }
	if (nVolume >= 0) x->nVolume = nVolume > 100 ? 100 : nVolume;
	if (nMute >= 0) x->bMute = nMute != 0;
	x->nGain = GainOf (x->nVolume, x->bMute);
	TRemember *r = RememberLocked (x->Name, TRUE);
	r->nVolume = x->nVolume; r->bMute = x->bMute;
	int v = x->nVolume | (x->bMute ? 0x100 : 0);
	s_Lock.Release ();
	return v;
}

#ifndef SOUND_HOST_TEST
// The mapped ring for the owner (v68): made on first use (one 64 KB page, kept for ever),
// emptied, and mixed from now on until the owner releases the output. -> its kernel
// (identity) address -- the caller maps that page into the app -- or 0 (not the owner, no
// memory).
struct kapi_sound_ring *SoundRing (unsigned nPid)
{
	static struct kapi_sound_ring *s_pRing = 0;
	if (s_pRing == 0)
	{
		// A page of its own (it is mapped into an app whole): 64 KB-aligned in a heap block.
		u8 *pRaw = new u8[2 * SND_RING_PAGE];
		if (pRaw == 0) return 0;
		uintptr ulPage = ((uintptr) pRaw + SND_RING_PAGE - 1) & ~(uintptr) (SND_RING_PAGE - 1);
		memset ((void *) ulPage, 0, SND_RING_PAGE);
		s_pRing = (struct kapi_sound_ring *) ulPage;
	}
	s_Lock.Acquire ();
	if (ClientOf (nPid) == 0 || (s_nRingPid != 0 && s_nRingPid != nPid)) { s_Lock.Release (); return 0; }	// (no channel; another program has the ring)
	struct kapi_sound_ring *pRing = s_pRing;
	pRing->magic = KAPI_SOUND_RING_MAGIC;
	pRing->frames = KAPI_SOUND_RING_FRAMES;
	pRing->rate = SND_RATE;
	pRing->chunk = s_nChunkFrames;
	pRing->ahead = s_nAheadCfg;
	if (s_pRingOn == 0)				// (mapped again by its owner: kept as it is)
	{
		pRing->wr = pRing->rd = 0;
		pRing->dry = 0;
		s_bRingFlowing = FALSE;
		DataMemBarrier ();
		s_nRingPid = nPid;
		s_pRingOn = pRing;
	}
	s_Lock.Release ();
	return pRing;
}
#endif
