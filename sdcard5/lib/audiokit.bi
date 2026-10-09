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
close 26 v p ak_close s
control 27 v iii ak_control channel,controller,value
f32_to_s16 28 v pppif ak_f32_to_s16 left,right,out,frames,gain
gain_s16 29 v pii ak_gain_s16 buf,frames,gain
info_of 30 v pp ak_info_of s,out
mix_s16 31 v ppii ak_mix_s16 dst,src,frames,gain
mono_to_stereo 32 v pi ak_mono_to_stereo buf,frames
note_mhz 33 i i ak_note_mhz key
note_name 34 v ip ak_note_name key,out8
note_off 35 v ii ak_note_off channel,key
note_on 36 i iii ak_note_on channel,key,velocity
note_parse 37 i s ak_note_parse name
notes_off 38 v - ak_notes_off
open 39 l spi ak_open path,err,cap
out_close 40 v - ak_out_close
out_free 41 i - ak_out_free
out_open 42 i ii ak_out_open chunk_frames,ahead
out_queued 43 i - ak_out_queued
out_write 44 i pi ak_out_write frames,n
pitch_bend 45 v ii ak_pitch_bend channel,value
play 46 i si ak_play path,loop
play_error 47 s - ak_play_error
play_keep_output 48 v i ak_play_keep_output on
play_len_ms 49 l - ak_play_len_ms
play_pause 50 v i ak_play_pause on
play_pos_ms 51 l - ak_play_pos_ms
play_seek_ms 52 i i ak_play_seek_ms ms
play_state 53 i - ak_play_state
play_stop 54 v - ak_play_stop
play_volume 55 i i ak_play_volume volume
play_wait 56 i i ak_play_wait ms
program 57 v ii ak_program channel,program
read 58 i ppi ak_read s,out,frames
resample 59 i ppipiI ak_resample r,in,in_frames,out,out_cap,used
resampler_free 60 v p ak_resampler_free r
resampler_new 61 l ii ak_resampler_new in_rate,out_rate
seek_ms 62 i pi ak_seek_ms s,ms
soundfont_default 63 l pi ak_soundfont_default err,cap
soundfont_name 64 s - ak_soundfont_name
synth_free 65 v p ak_synth_free s
synth_midi 66 v piiii ak_synth_midi s,channel,command,data1,data2
synth_new 67 l - ak_synth_new
synth_render 68 v ppi ak_synth_render s,out,frames
volume_gain 69 i i ak_volume_gain volume
wav_header 70 i piii ak_wav_header out44,rate,channels,data_bytes
wav_save 71 i spii ak_wav_save path,frames,n,rate
chorus_free 206 v p ak_chorus_free c
chorus_mute 207 v p ak_chorus_mute c
chorus_new 208 l ifff ak_chorus_new rate,delay_s,depth_s,hz
chorus_process 209 v pppFFi ak_chorus_process c,inL,inR,outL,outR,frames
fm_instrument 210 i ip ak_fm_instrument voice,ins
fm_render 211 v pi ak_fm_render out,frames
fm_start 212 i iiii ak_fm_start voice,milli_hz,wave,volume
fm_stop 213 v i ak_fm_stop voice
note_key 214 i ii ak_note_key note,octave
note_octave_mhz 215 i ii ak_note_octave_mhz note,octave
reverb_free 216 v p ak_reverb_free r
reverb_mute 217 v p ak_reverb_mute r
reverb_new 218 l i ak_reverb_new rate
reverb_process 219 v ppFFi ak_reverb_process r,in,left,right,frames
reverb_set 220 v pffff ak_reverb_set r,room,damp,wet,width
soft_clip 221 f f ak_soft_clip x
soundfont_find 222 i spi ak_soundfont_find preferred,out,cap
soundfont_free 223 v p ak_soundfont_free sf
soundfont_load 224 l spi ak_soundfont_load path,err,cap
soundfont_prefer 225 v s ak_soundfont_prefer path
wav_begin 226 l siii ak_wav_begin path,rate,channels,frames
wav_end 227 i p ak_wav_end w
wav_write 228 i ppi ak_wav_write w,frames,n
fm_live 229 v i ak_fm_live on
fm_silence 230 v - ak_fm_silence
tags_read 231 i sp ak_tags_read path,out
