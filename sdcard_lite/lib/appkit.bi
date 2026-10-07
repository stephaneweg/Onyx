# appkit.bi -- appkit for Onyx BASIC (#import appkit): made by tools/kitbi/kitbi.py from appkit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit appkit 314
struct clock_info 48 kapi_clock_info
field cnt 0 l
field freq 8 l
field utc_us 16 l
field tz_minutes 24 i
field flags 28 u
field boot_cnt 32 l
field reserved 40 l
struct cpu_core 16 kapi_cpu_core
field busy_us 0 l
field role 8 u
field pid 12 u
struct cpu_stats 144 kapi_cpu_stats
field now_us 0 l
field cores 8 u
field reserved 12 u
struct dirent2 288 kapi_dirent2
field name 0 a 256
field size 256 l
field mtime 264 l
field mode 272 u
field attr 276 u
field ino 280 l
struct stat 64 kapi_stat
field size 0 l
field mtime 8 l
field ino 16 l
field mode 24 u
field dev 28 u
field blksize 32 u
field attr 36 u
field blocks 40 l
field ctime 48 l
field reserved 56 l
struct chrome 104 kapi_chrome
field content 0 l
field content_w 8 i
field content_h 12 i
field active 16 l
field inactive 24 l
field chrome_w 32 i
field chrome_h 36 i
field inset_l 40 i
field inset_r 44 i
field inset_t 48 i
field inset_b 52 i
field title 56 a 48
struct handle_xfer 24 kapi_handle_xfer
field h 0 l
field kind 8 i
field tag 12 u
field fd 16 i
field flags 20 u
struct gpu_vertex 16 kapi_gpu_vertex
field x 0 f
field y 4 f
field z 8 f
field r 12 b
field g 13 b
field b 14 b
field a 15 b
struct gpu_program 56 kapi_gpu_program
field vs 0 l
field cs 8 l
field fs 16 l
field nvs 24 u
field ncs 28 u
field nfs 32 u
field inputs 36 u
field csInputs 40 u
field csOutputs 44 u
field varyings 48 u
field flags 52 u
struct gpu_frame 32 kapi_gpu_frame
field pixels 0 l
field w 8 i
field h 12 i
field stride 16 i
field clear 20 u
field flags 24 u
struct gpu_vertex3 32 kapi_gpu_vertex3
field x 0 f
field y 4 f
field z 8 f
field w 12 f
field s 16 f
field t 20 f
field r 24 b
field g 25 b
field b 26 b
field a 27 b
field r2 28 b
field g2 29 b
field b2 30 b
field a2 31 b
struct gpu_batch 80 kapi_gpu_batch
field first 0 u
field count 4 u
field texture 8 i
field flags 12 u
struct gpu_batch2 160 kapi_gpu_batch2
field first 0 u
field count 4 u
field program 8 i
field flags 12 u
field blend 16 u
field wmask 20 u
field vsUni 40 u
field vsNUni 44 u
field csUni 48 u
field csNUni 52 u
field fsUni 56 u
field fsNUni 60 u
struct gpu_batch3 168 kapi_gpu_batch3
field b 0 t gpu_batch2
field off 160 u
field stride 164 u
struct image_info 280 kapi_image_info
field size 0 l
field file_size 8 l
field refs 16 u
field flags 20 u
field path 24 a 256
struct midi_event 12 kapi_midi_event
field time_us 0 u
field cable 4 b
field status 5 b
field data1 6 b
field data2 7 b
field device 8 b
field length 9 b
field reserved 10 a 2
struct net_stats 24 kapi_net_stats
field rx_bytes 0 l
field tx_bytes 8 l
field sockets 16 u
field reserved 20 u
struct pollfd 16 kapi_pollfd
field kind 0 i
field h 4 i
field events 8 h
field revents 10 h
field reserved 12 i
struct event 32 kapi_event
field handler 0 l
field sender 8 l
field value 16 l
field event 24 i
field mods 28 u
struct posted 24 kapi_posted
field fn 0 l
field ctx 8 l
field value 16 l
struct syscall_stats 104 kapi_syscall_stats
field syscalls 0 l
field emulated 8 l
field rate 16 u
field slots 20 u
struct proc_status 16 kapi_proc_status
field code 0 i
field reason 4 i
field pid 8 i
field reserved 12 i
struct dirent 136 kapi_dirent
field name 0 a 128
field size 128 u
field is_dir 132 i
struct sockaddr 16 kapi_sockaddr
field family 0 w
field port 2 w
field addr 4 a 4
field zero 8 a 8
struct msghdr 48 kapi_msghdr
field iov 0 l
field handles 8 l
field iovcnt 16 u
field nhandles 20 u
field flags 24 u
field reserved 28 u
struct sound_client 60 kapi_sound_client
field pid 0 u
field volume 4 i
field mute 8 i
field peak 12 i
field queued 16 i
field name 20 a 24
struct fm_op 9 kapi_fm_op
field mult 0 b
field level 1 b
field ksl 2 b
field attack 3 b
field decay 4 b
field sustain 5 b
field release 6 b
field wave 7 b
field flags 8 b
struct fm_instrument 20 kapi_fm_instrument
field feedback 18 b
field connection 19 b
struct spawn_attr 64 kapi_spawn_attr
field path 0 l
field argv 8 l
field envp 16 l
field cwd 24 l
field in 32 l
field out 40 l
field reserved 48 l
field flags 56 u
field reserved2 60 u
struct thread_attr 64 kapi_thread_attr
field fn 0 l
field arg 8 l
field stack_size 16 l
field tls 24 l
field name 32 l
field flags 40 u
field prio 44 i
struct thread_info 32 kapi_thread_info
field stack_lo 0 l
field stack_hi 8 l
field tid 16 i
field state 20 i
field guard 24 l
struct vfs_req 640 kapi_vfs_req
field id 0 u
field op 4 i
field path 8 a 300
field path2 308 a 300
field a0 608 l
field a1 616 l
field a2 624 l
field in_len 632 u
struct vm_region 32 kapi_vm_region
field start 0 l
field end 8 l
field prot 16 u
field kind 20 u
field resident 24 u
field flags 28 u
struct vm_stats 48 kapi_vm_stats
field resident 0 l
field lazy 8 l
field writable 16 l
field faults 24 l
field pt_bytes 32 l
field limit 40 l
struct vol_info 48 kapi_vol_info
field total 0 l
field free 8 l
field used 16 l
field files 24 u
field dirs 28 u
field flags 32 u
field type 36 a 12
struct win_geom 44 kapi_win_geom
field x 0 i
field y 4 i
field w 8 i
field h 12 i
field cw 16 i
field ch 20 i
field ax 24 i
field ay 28 i
field aw 32 i
field ah 36 i
field state 40 u
struct win_info 108 kapi_win_info
field id 0 u
field pid 4 u
field x 8 i
field y 12 i
field w 16 i
field h 20 i
field flags 24 u
field alpha 28 i
field gen 32 u
field state 36 u
field title 40 a 48
field ow 88 i
field oh 92 i
field il 96 i
field it 100 i
field chromeGen 104 u
struct wlan_ap 52 kapi_wlan_ap
field ssid 0 a 33
field bssid 33 a 6
field security 39 b
field channel 40 b
field connected 41 b
field freq 44 i
field level 48 i
struct volume 112 kapi_volume
field name 0 a 8
field state 8 u
field flags 12 u
field gen 16 u
field open 20 u
field device_size 24 l
field total 32 l
field free 40 l
field serial 48 u
field type 52 a 8
field label 60 a 36
field device 96 a 12
struct format 48 kapi_format
field fs 0 u
field flags 4 u
field cluster 8 u
field label 12 a 36
struct tray_info 64 kapi_tray_info
field pid 0 u
field gen 4 u
field tip 8 a 56
abi_version 0 u - kapi_abi_version
app_dir 1 i pi kapi_app_dir b,s
barrier_create 2 i i kapi_barrier_create count
barrier_wait 3 i i kapi_barrier_wait h
chdir 4 i s kapi_chdir p
clipboard_get 5 i IpiI kapi_clipboard_get type,b,cap,serial
clipboard_set 6 i ipi kapi_clipboard_set type,d,n
clock_info 7 i p kapi_clock_info out
close 8 v p kapi_close h
closedir 9 v p kapi_closedir d
code_alloc 10 l i kapi_code_alloc size
core_acquire 11 i - kapi_core_acquire
core_release 12 v i kapi_core_release core
core_run 13 i icpp kapi_core_run core,fn,arg,stack_top
core_state 14 i i kapi_core_state core
cpu_stats 15 i p kapi_cpu_stats out
create_window 16 l iis kapi_create_window w,h,t
create_window_ex 17 l iiiisi kapi_create_window_ex x,y,w,h,t,f
cursor_pos 18 v II kapi_cursor_pos x,y
desk 19 i ii kapi_desk set,count
dir_read 20 i pp kapi_dir_read dir,out
drag_begin 21 i ipis kapi_drag_begin type,data,len,label
drag_data 22 i Ipi kapi_drag_data type,buf,cap
draw_text 23 v iisi kapi_draw_text x,y,s,c
draw_text_buf 24 v Iiiiisi kapi_draw_text_buf dst,dw,dh,x,y,s,c
event_create 25 i ii kapi_event_create manual_reset,initial
event_mods 26 u i kapi_event_mods mods
event_reset 27 i i kapi_event_reset h
event_set 28 i i kapi_event_set h
event_wait 29 i ii kapi_event_wait h,timeout_ms
exec 30 i ss kapi_exec path,args
exec_as 31 i sss kapi_exec_as path,args,name
exit 32 v i kapi_exit s
file_close 33 i i kapi_file_close h
file_in 34 l s kapi_file_in p
file_open 35 l sii kapi_file_open path,flags,mode
file_out 36 l si kapi_file_out p,append
file_read 37 l ipii kapi_file_read h,buf,len,off
file_seek 38 l iii kapi_file_seek h,off,whence
file_stat 39 i ip kapi_file_stat h,out
file_sync 40 i i kapi_file_sync h
file_truncate 41 i ii kapi_file_truncate h,size
file_write 42 l ipii kapi_file_write h,buf,len,off
font_height 43 i - kapi_font_height
font_width 44 i - kapi_font_width
fsize 45 u p kapi_fsize h
fsize64 46 l p kapi_fsize64 h
fullscreen_begin 47 l II kapi_fullscreen_begin w,h
fullscreen_direct 48 l III kapi_fullscreen_direct w,h,stride
fullscreen_end 49 v - kapi_fullscreen_end
get_args 50 i pi kapi_get_args b,n
get_argv 51 i pi kapi_get_argv buf,cap
get_chrome 52 i p kapi_get_chrome out
get_datetime 53 i IIIIII kapi_get_datetime y,mo,d,h,mi,se
get_env 54 i pi kapi_get_env buf,cap
get_handles 55 i pi kapi_get_handles out,cap
get_keymap 56 i pi kapi_get_keymap b,s
get_menu 57 u pipi kapi_get_menu buf,cap,title,tcap
get_modifiers 58 u - kapi_get_modifiers
get_ticks 59 u - kapi_get_ticks
get_verbose 60 i - kapi_get_verbose
get_wheel_speed 61 i - kapi_get_wheel_speed
getcwd 62 i pi kapi_getcwd b,n
getpid 63 i i kapi_getpid which
gpu_draw 64 i piiIiii kapi_gpu_draw v,n,clear,pixels,w,h,stride
gpu_info 65 i pi kapi_gpu_info buf,cap
gpu_program 66 i ip kapi_gpu_program handle,p
gpu_render 67 i ppipi kapi_gpu_render f,v,nv,b,nb
gpu_render2 68 i ppiipipi kapi_gpu_render2 f,v,nv,stride,b,nb,uni,nuni
gpu_render3 69 i ppipipip kapi_gpu_render3 f,v,nfloats,b,nb,uni,nuni,view
gpu_texture 70 i ipiii kapi_gpu_texture handle,pixels,w,h,stride
gpu_texture_rect 71 i iiiiipi kapi_gpu_texture_rect handle,x,y,w,h,pixels,stride
gpu_vbuf 72 l i kapi_gpu_vbuf bytes
handle_close 73 i i kapi_handle_close h
image_list 74 i spi kapi_image_list path,out,cap
image_preload 75 i s kapi_image_preload path
image_unload 76 i s kapi_image_unload path
inject_key 77 v s kapi_inject_key keys
inject_key_held 78 v ii kapi_inject_key_held key,down
inject_modifiers 79 v i kapi_inject_modifiers mods
inject_pointer 80 v iiii kapi_inject_pointer x,y,buttons,wheel
ipc_lookup 81 i s kapi_ipc_lookup name
ipc_register 82 i s kapi_ipc_register name
is_protected 83 i - kapi_is_protected
kbd_ready 84 i - kapi_kbd_ready
kernel_info 85 i pi kapi_kernel_info buf,cap
key_held 86 i i kapi_key_held key
kill 87 i s kapi_kill name
kill_pid 88 i ii kapi_kill_pid pid,force
klog_read 89 i Ipipi kapi_klog_read sev,src,sc,msg,mc
launch 90 i s kapi_launch n
lib_open 91 l siI kapi_lib_open name,min_version,err
list_apps 92 i pi kapi_list_apps b,s
list_procs 93 i pi kapi_list_procs b,s
list_tasks 94 i pi kapi_list_tasks b,s
list_windows 95 i pi kapi_list_windows b,s
lock 96 v I kapi_lock l
mailbox_recv 97 i IIpii kapi_mailbox_recv from_pid,type,buf,cap,blocking
mailbox_send 98 i iipi kapi_mailbox_send target_pid,type,in,len
memcpy 99 l ppi kapi_memcpy dst,src,n
meminfo 100 i LLLI kapi_meminfo total_kb,free_kb,app_kb,page_kb
memmove 101 l ppi kapi_memmove dst,src,n
memset 102 l pii kapi_memset dst,c,n
menu_command 103 i i kapi_menu_command id
midi_devices 104 i - kapi_midi_devices
midi_read 105 i pi kapi_midi_read ev,max
mkdir 106 i s kapi_mkdir p
move_window 107 v ii kapi_move_window x,y
msleep 108 v i kapi_msleep ms
mutex_create 109 i - kapi_mutex_create
mutex_lock 110 i ii kapi_mutex_lock h,timeout_ms
mutex_unlock 111 i i kapi_mutex_unlock h
net_info 112 i pi kapi_net_info buf,cap
net_ping 113 i siipi kapi_net_ping host,seq,timeout_ms,ip,cap
net_resolve 114 i spi kapi_net_resolve host,ip,cap
net_stats 115 i ip kapi_net_stats pid,out
net_status 116 i pi kapi_net_status ip,cap
open 117 l s kapi_open p
opendir 118 l s kapi_opendir p
pad_state 119 i ip kapi_pad_state index,out
path_mkdir 120 i si kapi_path_mkdir path,mode
path_rename 121 i ss kapi_path_rename from,to
path_stat 122 i sp kapi_path_stat path,out
path_unlink 123 i si kapi_path_unlink path,flags
path_utime 124 i si kapi_path_utime path,mtime
pipe 125 l - kapi_pipe
poll 126 i pii kapi_poll fds,n,timeout_ms
pop_event 127 i p kapi_pop_event ev
pop_post 128 i p kapi_pop_post p
post 129 i cpi kapi_post fn,ctx,value
present 130 v - kapi_present
present_fb 131 v - kapi_present_fb
proc_done 132 i p kapi_proc_done proc
proc_stats 133 i ip kapi_proc_stats pid,out
proc_wait 134 i pip kapi_proc_wait proc,flags,out
pump_events 135 v - kapi_pump_events
pump_sleep 136 i i kapi_pump_sleep timeout_ms
pump_wait 137 i i kapi_pump_wait timeout_ms
raise_app 138 i s kapi_raise_app n
ram_detail 139 i LLLLI kapi_ram_detail detected_kb,apppool_kb,apppool_free_kb,above4g_kb,nsegments
random 140 i pi kapi_random buf,len
read 141 i ppi kapi_read h,b,n
readdir 142 i pp kapi_readdir d,e
reboot 143 v - kapi_reboot
register_shell 144 i - kapi_register_shell
remove 145 i s kapi_remove p
rename 146 i ss kapi_rename from,to
resize_window 147 l ii kapi_resize_window w,h
resize_window2 148 l iiI kapi_resize_window2 w,h,stride
save_file 149 i spi kapi_save_file p,b,n
sbrk 150 l i kapi_sbrk inc
screen_grab 151 i Iii kapi_screen_grab dst,w,h
screen_native 152 i II kapi_screen_native w,h
screen_set 153 i ii kapi_screen_set w,h
screen_size 154 v II kapi_screen_size w,h
seek 155 i pi kapi_seek h,pos
set_click_handler 156 v c kapi_set_click_handler fn
set_cursor 157 i i kapi_set_cursor shape
set_key_handler 158 v c kapi_set_key_handler fn
set_keymap 159 i s kapi_set_keymap name
set_keymap_data 160 i spi kapi_set_keymap_data name,data,len
set_menu 161 i sc kapi_set_menu spec,h
set_pointer_handler 162 v c kapi_set_pointer_handler fn
set_timezone 163 i i kapi_set_timezone minutes
set_verbose 164 i i kapi_set_verbose on
set_wheel_speed 165 v i kapi_set_wheel_speed lines_per_notch
set_window_alpha 166 v i kapi_set_window_alpha a
shell_request 167 i ipi kapi_shell_request type,in,len
shm_create 168 l ii kapi_shm_create size,flags
shm_ctl 169 l iii kapi_shm_ctl h,op,arg
shm_map 170 l iiiiii kapi_shm_map h,addr,len,prot,flags,off
shm_open 171 l sii kapi_shm_open name,oflags,mode
shm_unlink 172 i s kapi_shm_unlink name
should_exit 173 i - kapi_should_exit
shutdown 174 v i kapi_shutdown mode
sleep_us 175 i i kapi_sleep_us us
sock_accept 176 i ipi kapi_sock_accept s,peer,flags
sock_bind 177 i ip kapi_sock_bind s,addr
sock_close 178 i i kapi_sock_close s
sock_connect 179 i ip kapi_sock_connect s,to
sock_getopt 180 i iiI kapi_sock_getopt s,opt,value
sock_listen 181 i ii kapi_sock_listen s,backlog
sock_name 182 i iip kapi_sock_name s,peer,out
sock_open 183 i ii kapi_sock_open type,flags
sock_pair 184 i iiI kapi_sock_pair type,flags,sv
sock_recv 185 l ipiip kapi_sock_recv s,buf,len,flags,from
sock_recvmsg 186 l ipi kapi_sock_recvmsg s,m,flags
sock_send 187 l ipiip kapi_sock_send s,buf,len,flags,to
sock_sendmsg 188 l ipi kapi_sock_sendmsg s,m,flags
sock_setopt 189 i iii kapi_sock_setopt s,opt,value
sock_shutdown 190 i ii kapi_sock_shutdown s,how
sound_acquire 191 i - kapi_sound_acquire
sound_client_volume 192 i iii kapi_sound_client_volume pid,volume,mute
sound_clients 193 i pi kapi_sound_clients out,max
sound_config 194 i ii kapi_sound_config chunk_frames,ahead
sound_instrument 195 i ip kapi_sound_instrument voice,ins
sound_map 196 l - kapi_sound_map
sound_output 197 i i kapi_sound_output out
sound_release 198 v - kapi_sound_release
sound_start 199 i iiii kapi_sound_start voice,millihz,wave,volume
sound_status 200 i III kapi_sound_status rate,free_frames,owner
sound_stop 201 i i kapi_sound_stop voice
sound_volume 202 i ii kapi_sound_volume volume,mute
sound_write 203 i pi kapi_sound_write frames,n
spawn 204 l sspp kapi_spawn path,args,in,out
spawn_ex 205 l p kapi_spawn_ex a
spawn_ex2 206 l ppi kapi_spawn_ex2 a,handles,n
stdin 207 l - kapi_stdin
stdin_read 208 i pi kapi_stdin_read b,n
stdout 209 l - kapi_stdout
stdout_write 210 i pi kapi_stdout_write b,n
stream_close 211 v p kapi_stream_close h
stream_eof 212 v p kapi_stream_eof h
stream_read 213 i ppi kapi_stream_read h,b,n
stream_read_nb 214 i ppi kapi_stream_read_nb h,b,n
stream_write 215 i ppi kapi_stream_write h,b,n
stream_write_nb 216 i ppi kapi_stream_write_nb h,buf,len
surface_create 217 i ii kapi_surface_create w,h
surface_destroy 218 i i kapi_surface_destroy id
surface_map 219 l i kapi_surface_map id
surface_present 220 v i kapi_surface_present id
surface_size 221 i iII kapi_surface_size id,w,h
sync_close 222 i i kapi_sync_close h
table_slot 223 l i kapi_table_slot slot
tcp_accept 224 i ipi kapi_tcp_accept listen_sock,ip,cap
tcp_close 225 v i kapi_tcp_close sock
tcp_connect 226 i si kapi_tcp_connect host,port
tcp_listen 227 i i kapi_tcp_listen port
tcp_recv 228 i ipi kapi_tcp_recv sock,buf,len
tcp_send 229 i ipi kapi_tcp_send sock,buf,len
thread_create 230 i cpis kapi_thread_create fn,arg,stack_size,name
thread_create_ex 231 i p kapi_thread_create_ex attr
thread_exit 232 v i kapi_thread_exit code
thread_info 233 i ip kapi_thread_info tid,out
thread_join 234 i iiI kapi_thread_join tid,timeout_ms,code
thread_priority 235 i ii kapi_thread_priority tid,prio
thread_self 236 i - kapi_thread_self
toggle_app 237 i s kapi_toggle_app n
vfs_next 238 i pi kapi_vfs_next req,blocking
vfs_register 239 i s kapi_vfs_register prefix
vfs_reply 240 i iipi kapi_vfs_reply id,status,data,len
vfs_req_data 241 i ipii kapi_vfs_req_data id,buf,cap,offset
vm_advise 242 i iii kapi_vm_advise addr,len,advice
vm_map 243 l iiii kapi_vm_map addr,len,prot,flags
vm_protect 244 i iii kapi_vm_protect addr,len,prot
vm_query 245 i ip kapi_vm_query addr,out
vm_stats 246 i ip kapi_vm_stats pid,out
vm_unmap 247 i ii kapi_vm_unmap addr,len
vol_info 248 i sp kapi_vol_info path,out
wait 249 i p kapi_wait proc
wait_for_exit 250 v - kapi_wait_for_exit
wait_word 251 i Iii kapi_wait_word addr,expected,timeout_ms
wake_word 252 i I kapi_wake_word addr
wallpaper_buffer 253 l II kapi_wallpaper_buffer w,h
wallpaper_commit 254 v - kapi_wallpaper_commit
wallpaper_generate 255 i iii kapi_wallpaper_generate base,pts,seed
win_close 256 i i kapi_win_close id
win_desk 257 i ii kapi_win_desk id,n
win_geometry 258 i p kapi_win_geometry out
win_list 259 i pi kapi_win_list out,max
win_minimise 260 i i kapi_win_minimise id
win_raise 261 i i kapi_win_raise id
win_read 262 i iiiiiiIi kapi_win_read id,part,x,y,w,h,dst,stride
win_resizable 263 i iii kapi_win_resizable on,min_w,min_h
wlan_reconnect 264 i - kapi_wlan_reconnect
wlan_scan 265 i pi kapi_wlan_scan out,max
write 266 i ipi kapi_write fd,b,n
yield 267 v - kapi_yield
app_ini_count 268 i - app_ini_count
app_ini_get 269 s sss app_ini_get section,key,def
app_ini_get_int 270 i ssi app_ini_get_int section,key,def
app_ini_key 271 s i app_ini_key i
app_ini_load 272 i s app_ini_load filename
app_ini_load_path 273 i s app_ini_load_path path
app_ini_section 274 s i app_ini_section i
app_ini_value 275 s i app_ini_value i
ax_app_path 276 v piss ax_app_path dst,cap,name,suffix
ax_fmt2 277 v pi ax_fmt2 d,v
ax_itoa 278 i ip ax_itoa v,b
ax_load_keymap 279 i s ax_load_keymap name
ax_putln 280 v s ax_putln s
ax_puts 281 v s ax_puts s
ax_strcat 282 v piIs ax_strcat dst,cap,pos,src
ax_streq 283 i ss ax_streq a,b
ax_strlen 284 i s ax_strlen s
lx_app_for 285 i spi lx_app_for ext,out,cap
lx_cat 286 v piIs lx_cat d,cap,n,s
lx_cmdline 287 v piss lx_cmdline out,cap,path,args
lx_entry 288 i ipipi lx_entry i,ext,ecap,prog,pcap
lx_exists 289 i s lx_exists path
lx_launch 290 i ss lx_launch name,args
lx_launch_dir 291 i sss lx_launch_dir dir,name,args
lx_len 292 i s lx_len s
lx_lists_ext 293 i sisi lx_lists_ext v,len,ext,games
lx_low 294 b i lx_low c
lx_open 295 i ss lx_open path,args
lx_open_as 296 i sss lx_open_as path,args,name
lx_runner 297 i spi lx_runner path,out,cap
ws_ctl 298 l iiii kapi_ws_ctl op,a0,a1,a2
cursor_shown 299 i - kapi_cursor_shown
proc_tree 300 i iiIi kapi_proc_tree pid,op,out,cap
vol_list 301 i pii kapi_vol_list out,max,flags
vol_eject 302 i si kapi_vol_eject vol,flags
vol_mount 303 i s kapi_vol_mount vol
vol_format 304 i sp kapi_vol_format vol,fmt
win_destroy 305 v i kapi_win_destroy win
win_new 306 i iiiisiL kapi_win_new x,y,w,h,t,f,canvas
win_select 307 i i kapi_win_select win
tray_activate 308 i ii kapi_tray_activate pid,kind
tray_clear 309 v - kapi_tray_clear
tray_icon 310 i iI kapi_tray_icon pid,px
tray_list 311 i pi kapi_tray_list out,max
tray_set 312 i psc kapi_tray_set px,tip,h
win_move 313 i iii kapi_win_move id,x,y
