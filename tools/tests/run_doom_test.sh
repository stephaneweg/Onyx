#!/bin/sh
# run_doom_test.sh -- Doom's Onyx port on the PC, headless (tools/tests/doom/host_doom.c on a
# virtual clock): doomgeneric + user/doom/doom_sound.c with a stand-in kapi. Plays the IWAD's
# demos for a while (a key press starts a new game), then saves the last frame (doom_last.ppm)
# (Enter x 4: a new game, then Ctrl: shots) and the sound effects (doom_sfx.raw, s16 stereo 44100 Hz) and counts the music's FM notes.
#   sh tools/tests/run_doom_test.sh [SD:/doom iwad, default sdcard/doom/freedoom1.wad]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}/onyx_doom
IWAD=${1:-$ROOT/sdcard/doom/freedoom1.wad}
DG=$ROOT/third_party/doomgeneric
mkdir -p "$T"
SRC=""
for f in dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net \
	 f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound \
	 i_system i_timer memio m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc \
	 m_random p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats \
	 p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw \
	 r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff s_sound tables \
	 v_video wi_stuff w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric; do
	SRC="$SRC $DG/$f.c"
done
gcc -O1 -g -w -DONYX -DFEATURE_SOUND -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=640 -DDOOMGENERIC_RESY=400 \
	-I"$HERE/doom" -I"$ROOT/kernel/include" -I"$DG" $SRC "$ROOT/user/doom/doom_sound.c" "$HERE/doom/host_doom.c" -lm -o "$T/host_doom"
cd "$T" && "$T/host_doom" "$IWAD" 40 "8:13,9:13,10:13,11:13,20:163,25:163,30:163"
