# systemkit.bi -- systemkit for Onyx BASIC (#import systemkit): made by tools/kitbi/kitbi.py from systemkit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit systemkit 76
struct PreloadList 4100 PreloadList
field n 0 i
clip_clear 0 v - clip_clear
clip_get 1 b pipiLI clip_get fmt,nf,got,cap,data,len
clip_get_file 2 i piI clip_get_file buf,cap,cut
clip_get_image 3 l II clip_get_image w,h
clip_get_text 4 i pi clip_get_text buf,cap
clip_put 5 b pppi clip_put fmt,data,len,n
clip_set_files 7 v si clip_set_files paths,cut
clip_set_image 8 b pii clip_set_image px,w,h
clip_set_text 9 v s clip_set_text s
clip_set_text_n 10 v si clip_set_text_n s,n
fa_app_for 11 b spi fa_app_for path,app,cap
fa_ext 12 s s fa_ext path
fa_is_program 13 b s fa_is_program path
fa_open 14 b s fa_open path
mixer_set 15 i pii mixer_set c,volume,mute
notify 16 i ss notify title,text
notify_action 17 i sss notify_action title,text,action
preload_ini_load 18 i p preload_ini_load l
preload_ini_save 19 i p preload_ini_save l
trash_count 20 i - trash_count
trash_empty 21 i - trash_empty
trash_ensure 22 v - trash_ensure
trash_move 24 b s trash_move path
trash_origin 25 b spi trash_origin name,out,cap
trash_purge 26 b s trash_purge name
trash_restore 27 b spi trash_restore name,where,wcap
volume_output_word 28 s i volume_output_word out
volume_restore 29 v - volume_restore
volume_save 30 v ii volume_save vol,mute
volume_set_output 31 i i volume_set_output out
wp_colour 32 u si wp_colour v,def
wp_eq 34 b ss wp_eq a,b
wp_grey_cover 35 v piipii wp_grey_cover img,iw,ih,out,w,h
wp_grey_tile 36 v piipiiii wp_grey_tile img,iw,ih,out,w,h,sw,sh
wp_isqrt 37 u i wp_isqrt n
wp_key 38 v ssp wp_key k,v,ctx
wp_lines 39 b scp wp_lines path,fn,ctx
wp_lum 41 u i wp_lum c
wp_multiply 42 v Iiiis wp_multiply dst,w,h,stride,grey
wp_put 44 i piis wp_put o,p,cap,s
wp_put_colour 45 i piii wp_put_colour o,p,cap,c
wp_tint 47 u ii wp_tint base,dist
dc_copy 48 v psi dc_copy d,s,cap
dc_put 49 i piis dc_put o,p,cap,s
dc_split 50 v spipi dc_split v,a,acap,b,bcap
dock_reload 51 v - dock_reload
dock_running 52 i - dock_running
dock_category_docked 56 b s dock_category_docked cat
locale_ini_get 59 i spi locale_ini_get key,out,cap
locale_ini_set 60 i ss locale_ini_set key,value
locale_language 61 s - locale_language
locale_language_code 62 s i locale_language_code i
locale_language_count 63 i - locale_language_count
locale_language_index 64 i - locale_language_index
locale_language_name 65 s i locale_language_name i
locale_set_language 66 i s locale_set_language code
locale_set_zone 67 i i locale_set_zone z
locale_zone 68 i - locale_zone
locale_zone_city 69 s i locale_zone_city z
locale_zone_count 70 i - locale_zone_count
locale_zone_offset 71 i i locale_zone_offset z
locale_zone_summer 72 i i locale_zone_summer z
locale_zone_utc 73 v ipi locale_zone_utc z,out,cap
autostart_ensure 74 i sss autostart_ensure cmd,after,comment
autostart_has 75 i s autostart_has cmd
