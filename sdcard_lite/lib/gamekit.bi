# gamekit.bi -- gamekit for Onyx BASIC (#import gamekit): made by tools/kitbi/kitbi.py from gamekit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit gamekit 9
struct game_system 196 game_system
field name 0 a 40
field ext 40 a 64
field ext_text 104 a 64
field emu 168 a 24
field order 192 i
struct game 332 game
field path 0 a 200
field name 200 a 64
field key 264 a 64
field sys 328 i
folders 0 i ci games_folders out,max
folders_save 1 i ci games_folders_save folders,n
nice_name 2 v spi games_nice_name file,out,cap
scan 3 i cipipi games_scan folders,nfolders,sys,nsys,out,max
system_of 4 i spi games_system_of file,sys,nsys
systems 5 i pi games_systems out,max
thumb_load 6 i pI games_thumb_load g,px
thumb_path 7 v ppi games_thumb_path g,out,cap
thumb_save 8 i pp games_thumb_save g,px
