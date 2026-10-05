# audiokit.bi -- audiokit for Onyx BASIC (#import audiokit): made by tools/kitbi/kitbi.py from audiokit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit audiokit 232
struct info 32 ak_info
field rate 0 i
field channels 4 i
field bits 8 i
field kbps 12 i
field length_ms 16 l
field format 24 a 8
struct kapi_fm_op 9 kapi_fm_op
field mult 0 b
field level 1 b
field ksl 2 b
field attack 3 b
field decay 4 b
field sustain 5 b
field release 6 b
field wave 7 b
field flags 8 b
struct kapi_fm_instrument 20 kapi_fm_instrument
field feedback 18 b
field connection 19 b
struct tags 568 ak_tags
field title 0 a 128
field artist 128 a 96
field album_artist 224 a 96
field album 320 a 128
field genre 448 a 48
field year 496 i
field track 500 i
field disc 504 i
field duration_ms 508 i
field format 512 a 8
field cover_offset 520 l
field cover_length 528 u
close 26 v p ak_close
control 27 v iii ak_control
f32_to_s16 28 v pppif ak_f32_to_s16
gain_s16 29 v pii ak_gain_s16
info_of 30 v pp ak_info_of
mix_s16 31 v ppii ak_mix_s16
mono_to_stereo 32 v pi ak_mono_to_stereo
note_mhz 33 i i ak_note_mhz
note_name 34 v ip ak_note_name
note_off 35 v ii ak_note_off
note_on 36 i iii ak_note_on
note_parse 37 i s ak_note_parse
notes_off 38 v - ak_notes_off
open 39 l spi ak_open
out_close 40 v - ak_out_close
out_free 41 i - ak_out_free
out_open 42 i ii ak_out_open
out_queued 43 i - ak_out_queued
out_write 44 i pi ak_out_write
pitch_bend 45 v ii ak_pitch_bend
play 46 i si ak_play
play_error 47 s - ak_play_error
play_keep_output 48 v i ak_play_keep_output
play_len_ms 49 l - ak_play_len_ms
play_pause 50 v i ak_play_pause
play_pos_ms 51 l - ak_play_pos_ms
play_seek_ms 52 i i ak_play_seek_ms
play_state 53 i - ak_play_state
play_stop 54 v - ak_play_stop
play_volume 55 i i ak_play_volume
play_wait 56 i i ak_play_wait
program 57 v ii ak_program
read 58 i ppi ak_read
resample 59 i ppipiI ak_resample
resampler_free 60 v p ak_resampler_free
resampler_new 61 l ii ak_resampler_new
seek_ms 62 i pi ak_seek_ms
soundfont_default 63 l pi ak_soundfont_default
soundfont_name 64 s - ak_soundfont_name
synth_free 65 v p ak_synth_free
synth_midi 66 v piiii ak_synth_midi
synth_new 67 l - ak_synth_new
synth_render 68 v ppi ak_synth_render
volume_gain 69 i i ak_volume_gain
wav_header 70 i piii ak_wav_header
wav_save 71 i spii ak_wav_save
chorus_free 206 v p ak_chorus_free
chorus_mute 207 v p ak_chorus_mute
chorus_new 208 l ifff ak_chorus_new
chorus_process 209 v pppFFi ak_chorus_process
fm_instrument 210 i ip ak_fm_instrument
fm_render 211 v pi ak_fm_render
fm_start 212 i iiii ak_fm_start
fm_stop 213 v i ak_fm_stop
note_key 214 i ii ak_note_key
note_octave_mhz 215 i ii ak_note_octave_mhz
reverb_free 216 v p ak_reverb_free
reverb_mute 217 v p ak_reverb_mute
reverb_new 218 l i ak_reverb_new
reverb_process 219 v ppFFi ak_reverb_process
reverb_set 220 v pffff ak_reverb_set
soft_clip 221 f f ak_soft_clip
soundfont_find 222 i spi ak_soundfont_find
soundfont_free 223 v p ak_soundfont_free
soundfont_load 224 l spi ak_soundfont_load
soundfont_prefer 225 v s ak_soundfont_prefer
wav_begin 226 l siii ak_wav_begin
wav_end 227 i p ak_wav_end
wav_write 228 i ppi ak_wav_write
fm_live 229 v i ak_fm_live
fm_silence 230 v - ak_fm_silence
tags_read 231 i sp ak_tags_read
