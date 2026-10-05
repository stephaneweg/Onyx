//
// volume.h -- the master volume (kapi v60 sound_volume: 0..10 and mute), kept in
// SD:/etc/sound.ini ("volume = 7", "mute = 0") so it comes back after a reboot: the menu bar
// applies it at start. Used by the menu bar's volume box and /bin/volume.
// (v84) The file also says which output plays ("output = auto | jack | usb | hdmi": the kernel reads
// it when the sound first starts, kapi sound_output changes it at once): volume_save keeps that line.
//
#ifndef _volume_h
#define _volume_h
#include "appkit/appkit.h"
#include "sk_api.h"

#define VOLUME_INI	"SD:/etc/sound.ini"
// An output's word in sound.ini (KAPI_SND_OUT_*).
SK_API const char *volume_output_word (int out);
SK_API void volume_save (int vol, int mute);
// The output chosen (KAPI_SND_OUT_*): applied now, kept in the file -> sound_output's result.
SK_API int volume_set_output (int out);

// ---- the mixer (kapi v85): a program's own volume ------------------------------------------------
// Every program that plays has a channel (kapi_sound_clients); its volume (0..100) and its mute are
// remembered by its name in SD:/etc/mixer.ini ("media = 60", "media.mute = 1"), which the kernel
// reads when the sound first starts.
#define MIXER_INI	"SD:/etc/mixer.ini"
// The channel's volume and mute set now (-1: kept) and written to the file -> volume | 0x100 muted, -1.
SK_API int mixer_set (const struct kapi_sound_client *c, int volume, int mute);
// the saved volume -> the kernel (no file: full, not muted)
SK_API void volume_restore (void);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "volume.inc"
#endif

#endif
