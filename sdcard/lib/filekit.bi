# filekit.bi -- filekit for Onyx BASIC (#import filekit): made by tools/kitbi/kitbi.py from filekit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit filekit 78
struct zip_entry 360 fk_zip_entry
field name 0 a 300
field size 304 l
field packed 312 l
field crc 320 u
field dos_time 324 u
field dir 328 i
field encrypted 332 i
field method 336 i
struct extract 72 fk_extract
field size 0 u
field dest 8 l
field selected 16 l
field from 24 l
field layout 32 i
field exists 36 i
field ask 40 l
field progress 48 l
field user 56 l
field files 64 i
field skipped 68 i
struct format 220 fk_format
field name 0 a 16
field extensions 16 a 96
field note 112 a 80
field can_read 192 i
field can_write 196 i
field can_password 200 i
adler32 0 u ipi fk_adler32 adler,data,n
copy 1 i sscp fk_copy src,dst,cb,user
crc32 2 u ipi fk_crc32 crc,data,n
deflate 3 i piiiLI fk_deflate data,n,wrap,level,out,out_n
dos_time_str 4 v ipi fk_dos_time_str dos_time,out,cap
exists 5 i s fk_exists path
file_size 6 l s fk_file_size path
free 7 v p fk_free p
human_size 8 v ipi fk_human_size bytes,out,cap
inflate 9 i piiiLI fk_inflate data,n,wrap,size_hint,out,out_n
load 10 i sLI fk_load path,out,out_n
mkdirs 11 i s fk_mkdirs path
move 12 i sscp fk_move src,dst,cb,user
path_ext 13 v spi fk_path_ext path,out,cap
path_folder 14 v spi fk_path_folder path,out,cap
path_join 15 v piss fk_path_join out,cap,folder,name
path_name 16 s s fk_path_name path
path_unique 17 v pi fk_path_unique path,cap
remove 18 i s fk_remove path
save 19 i spi fk_save path,data,n
tree_size 20 l sII fk_tree_size path,files,folders
zip_close 21 v p fk_zip_close z
zip_count 22 i p fk_zip_count z
zip_entry 23 i pip fk_zip_entry z,i,out
zip_error 24 s p fk_zip_error z
zip_extract 25 i pis fk_zip_extract z,i,dest_path
zip_extract_all 26 i psscp fk_zip_extract_all z,prefix,dest_dir,cb,user
zip_find 27 i ps fk_zip_find z,name
zip_open 28 l spi fk_zip_open path,err,cap
zip_password 29 v ps fk_zip_password z,password
zip_read 30 i piLI fk_zip_read z,i,out,out_n
zipbuf_add 31 i pspii fk_zipbuf_add b,name,data,n,level
zipbuf_finish 32 i pLI fk_zipbuf_finish b,out,out_n
zipbuf_free 33 v p fk_zipbuf_free b
zipbuf_new 34 l - fk_zipbuf_new
zipmem_count 35 i pi fk_zipmem_count zip,n
zipmem_entry 36 i piipiI fk_zipmem_entry zip,n,i,name,cap,size
zipmem_get 37 i pisLI fk_zipmem_get zip,n,name,out,out_n
zipw_add 38 i pss fk_zipw_add w,disk_path,name
zipw_add_data 39 i pspi fk_zipw_add_data w,name,data,n
zipw_close 40 i pcppi fk_zipw_close w,cb,user,err,cap
zipw_create 41 l s fk_zipw_create path
zipw_level 42 v pi fk_zipw_level w,level
arc_add_bytes 44 l ppisii fk_arc_add_bytes a,paths,n,into,keep_folders,replace
arc_add_conflicts 45 i ppisi fk_arc_add_conflicts a,paths,n,into,keep_folders
arc_comment 46 s p fk_arc_comment a
arc_delete 47 i pscp fk_arc_delete a,selected,cb,user
arc_extract_bytes 48 l ps fk_arc_extract_bytes a,selected
arc_extract_with 49 i pp fk_arc_extract_with a,x
arc_format 50 s p fk_arc_format a
arc_formats 51 i pi fk_arc_formats out,max
arc_is_name 52 i s fk_arc_is_name name
arc_method_name 53 i pipi fk_arc_method_name a,i,out,cap
arc_new 54 l ss fk_arc_new path,format
arc_new_folder 55 i pscp fk_arc_new_folder a,name,cb,user
arc_open 56 l spi fk_arc_open path,err,cap
arc_path 57 s p fk_arc_path a
arc_probe 58 i spipi fk_arc_probe path,format,fcap,why,wcap
arc_rename 59 i psscp fk_arc_rename a,from,to,cb,user
arc_test 60 i picp fk_arc_test a,i,cb,user
arc_writable 61 i p fk_arc_writable a
arc_write_empty 62 i p fk_arc_write_empty a
fs_basename 63 s s fs_basename path
fs_ci_cmp 64 i ss fs_ci_cmp a,b
fs_copy 65 v psi fs_copy d,s,cap
fs_copy_file 66 b ss fs_copy_file src,dst
fs_copy_tree 67 b ssi fs_copy_tree src,dst,depth
fs_dirname 68 v pis fs_dirname out,cap,path
fs_exists 69 b s fs_exists path
fs_is_dir 70 b s fs_is_dir path
fs_join 71 v piss fs_join out,cap,dir,name
fs_len 72 i s fs_len s
fs_lower 73 b i fs_lower c
fs_remove_tree 74 b si fs_remove_tree path,depth
fs_skip_dot 75 b s fs_skip_dot n
fs_text_fix 76 i pi fs_text_fix b,n
fs_unique_name 77 v pisss fs_unique_name out,cap,dir,name,tag
