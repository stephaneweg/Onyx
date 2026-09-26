// A stand-in for user/kapi.h to run Doom's Onyx sound code on the PC (tools/tests/doom).
#pragma once
#include <kern/kapi_abi.h>
int  kapi_sound_acquire (void);
int  kapi_sound_status (unsigned *rate, unsigned *freeFrames, unsigned *owner);
int  kapi_sound_write (const short *frames, unsigned n);
int  kapi_sound_instrument (int voice, const struct kapi_fm_instrument *ins);
int  kapi_sound_start (int voice, unsigned millihz, int wave, int volume);
int  kapi_sound_stop (int voice);
