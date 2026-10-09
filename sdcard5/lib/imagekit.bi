# imagekit.bi -- imagekit for Onyx BASIC (#import imagekit): made by tools/kitbi/kitbi.py from imagekit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit imagekit 46
struct adjust 64 ik_adjust
field exposure 0 i
field contrast 4 i
field highlights 8 i
field shadows 12 i
field saturation 16 i
field warmth 20 i
field sharpness 24 i
field filter 28 i
struct info 192 ik_info
field w 0 i
field h 4 i
field orientation 8 i
field taken 16 l
field camera 24 a 64
field exposure 88 a 64
field has_preview 152 i
struct format 84 ik_format
field name 0 a 12
field extensions 12 a 40
field can_read 52 i
field can_write 56 i
field alpha 60 i
field animated 64 i
adjust_apply 0 v pp ik_adjust_apply im,a
adjust_auto 1 v pp ik_adjust_auto im,out
cover 2 l pii ik_cover im,w,h
crop 3 i piiii ik_crop im,x,y,w,h
encode 4 i psiLI ik_encode im,format,quality,out,out_n
fill 5 v pi ik_fill im,argb
filter_name 6 s i ik_filter_name filter
fit 7 l piii ik_fit im,max_w,max_h,grow
flatten 8 v pi ik_flatten im,rgb
flip 9 i pi ik_flip im,vertical
format 10 i p ik_format im
frames_count 11 i p ik_frames_count f
frames_delay 12 i pi ik_frames_delay f,i
frames_free 13 v p ik_frames_free f
frames_height 14 i p ik_frames_height f
frames_load 15 l s ik_frames_load path
frames_load_mem 16 l pi ik_frames_load_mem data,n
frames_pixels 17 l pi ik_frames_pixels f,i
frames_width 18 i p ik_frames_width f
free 19 v p ik_free p
has_alpha 20 i p ik_has_alpha im
height 21 i p ik_height im
image_copy 22 l p ik_image_copy im
image_free 23 v p ik_image_free im
image_from 24 l piii ik_image_from px,w,h,stride
image_new 25 l ii ik_image_new w,h
is_image_name 26 i s ik_is_image_name name
load 27 l si ik_load path,flags
load_format 28 s - ik_load_format
load_mem 29 l pii ik_load_mem data,n,flags
load_preview 30 l s ik_load_preview path
opaque 31 v p ik_opaque im
orient 32 i pi ik_orient im,exif_orientation
pixels 33 l p ik_pixels im
probe 34 i sp ik_probe path,out
resize 35 l pii ik_resize im,w,h
rotate 36 i pi ik_rotate im,quarter_turns
save 37 i psi ik_save im,path,quality
straighten 39 i pi ik_straighten im,millidegrees
width 40 i p ik_width im
formats 41 i pi ik_formats out,max
frames_take 42 i pLIi ik_frames_take f,px,delay,max
inflate 43 l piiI ik_inflate data,n,zlib,out_n
