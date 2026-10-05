# systemkit.bi -- systemkit for Onyx BASIC (#import systemkit): made by tools/kitbi/kitbi.py from systemkit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit systemkit 56
struct PreloadList 4100 PreloadList
field n 0 i
clip_clear 0 v - clip_clear
clip_get 1 b pipiLI clip_get
clip_get_file 2 i piI clip_get_file
clip_get_image 3 l II clip_get_image
clip_get_text 4 i pi clip_get_text
clip_put 5 b pppi clip_put
clip_set_files 7 v si clip_set_files
clip_set_image 8 b pii clip_set_image
clip_set_text 9 v s clip_set_text
clip_set_text_n 10 v si clip_set_text_n
fa_app_for 11 b spi fa_app_for
fa_ext 12 s s fa_ext
fa_is_program 13 b s fa_is_program
fa_open 14 b s fa_open
mixer_set 15 i pii mixer_set
notify 16 i ss notify
notify_action 17 i sss notify_action
preload_ini_load 18 i p preload_ini_load
preload_ini_save 19 i p preload_ini_save
trash_count 20 i - trash_count
trash_empty 21 i - trash_empty
trash_ensure 22 v - trash_ensure
trash_move 24 b s trash_move
trash_origin 25 b spi trash_origin
trash_purge 26 b s trash_purge
trash_restore 27 b spi trash_restore
volume_output_word 28 s i volume_output_word
volume_restore 29 v - volume_restore
volume_save 30 v ii volume_save
volume_set_output 31 i i volume_set_output
wp_colour 32 u si wp_colour
wp_eq 34 b ss wp_eq
wp_grey_cover 35 v piipii wp_grey_cover
wp_grey_tile 36 v piipiiii wp_grey_tile
wp_isqrt 37 u i wp_isqrt
wp_key 38 v ssp wp_key
wp_lines 39 b scp wp_lines
wp_lum 41 u i wp_lum
wp_multiply 42 v Iiiis wp_multiply
wp_put 44 i piis wp_put
wp_put_colour 45 i piii wp_put_colour
wp_tint 47 u ii wp_tint
dc_copy 48 v psi dc_copy
dc_put 49 i piis dc_put
dc_split 50 v spipi dc_split
dock_reload 51 v - dock_reload
dock_running 52 i - dock_running
