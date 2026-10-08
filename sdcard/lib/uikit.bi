# uikit.bi -- uikit for Onyx BASIC (#import uikit): made by tools/kitbi/kitbi.py from uikit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit uikit 949
struct win_server_info 88 uk_win_server_info
field size 0 u
field name 4 a 16
field mode 20 i
field screen_w 24 i
field screen_h 28 i
field work_x 32 i
field work_y 36 i
field work_w 40 i
field work_h 44 i
field scale 48 i
field size_class 52 i
struct shell_task 92 uk_shell_task
field id 0 u
field pid 4 u
field flags 8 u
field w 12 i
field h 16 i
field title 20 a 48
field name 68 a 24
lang_init 2 v - uk_lang_init
lang_load 3 b s uk_lang_load code
lang_choose 4 b s uk_lang_choose code
lang_chosen 5 s - uk_lang_chosen
tr 7 s s uk_tr en
trc 8 s ss uk_trc ctx,en
lang 9 s - uk_lang
add_item 687 v ps uk_add_item widget,text
ask_open 688 s s uk_ask_open folder
ask_save 689 s ss uk_ask_save folder,name
button 690 l piiiisc uk_button window,x,y,w,h,text,on_click
checkbox 691 l piiiisic uk_checkbox window,x,y,w,h,text,checked,on_click
clear_items 692 v p uk_clear_items widget
dropdown 693 l piiiisc uk_dropdown window,x,y,w,h,items,on_change
enable 694 v pi uk_enable widget,on
enabled 695 i p uk_enabled widget
focus 696 v p uk_focus widget
get_text 697 s p uk_get_text widget
get_value 698 i p uk_get_value widget
item_count 699 i p uk_item_count widget
label 700 l piiiis uk_label window,x,y,w,h,text
listbox 701 l piiiisc uk_listbox window,x,y,w,h,items,on_change
menu_item 702 v psssc uk_menu_item window,title,item,key,on_choose
message 703 i ssi uk_message title,text,buttons
move 704 v piiii uk_move widget,x,y,w,h
progress 705 l piiiiii uk_progress window,x,y,w,h,max,value
set_range 706 v pii uk_set_range widget,min,max
set_text 707 v ps uk_set_text widget,text
set_value 708 v pi uk_set_value widget,value
show 709 v pi uk_show widget,on
shown 710 i p uk_shown widget
slider 711 l piiiiiic uk_slider window,x,y,w,h,max,value,on_change
textbox 712 l piiiisc uk_textbox window,x,y,w,h,text,on_change
window 713 l siii uk_window title,w,h,flags
window_close 714 v p uk_window_close window
window_height 715 i p uk_window_height window
window_min_size 716 v pii uk_window_min_size window,w,h
window_on_resize 717 v pc uk_window_on_resize window,fn
window_run 718 v p uk_window_run window
window_step 719 i p uk_window_step window
window_width 720 i p uk_window_width window
window_wait 721 i p uk_window_wait window
height 722 i p uk_height widget
panel 723 l piiii uk_panel window,x,y,w,h
set_parent 724 v pp uk_set_parent widget,window
width 725 i p uk_width widget
win_select 781 i i uk_win_select win
shell_dim 793 i i uk_shell_dim alpha
shell_front 795 i ii uk_shell_front id,front
shell_keys 796 i pi uk_shell_keys keys,count
shell_register 797 i - uk_shell_register
shell_split 798 i ii uk_shell_split left,right
shell_thumb 799 i iIii uk_shell_thumb id,dst,w,h
win_alpha 800 v i uk_win_alpha alpha
win_app_raise 801 i s uk_win_app_raise name
win_app_toggle 802 i s uk_win_app_toggle name
win_apps 803 i pi uk_win_apps b,s
win_chrome 804 i p uk_win_chrome out
win_close 805 i i uk_win_close id
win_create 806 l iis uk_win_create w,h,title
win_create_ex 807 l iiiisi uk_win_create_ex x,y,w,h,title,flags
win_cursor 808 i i uk_win_cursor shape
win_cursor_pos 809 v II uk_win_cursor_pos x,y
win_cursor_shown 810 i - uk_win_cursor_shown
win_desk 811 i ii uk_win_desk set,count
win_destroy 812 v i uk_win_destroy win
win_drag_begin 813 i ipis uk_win_drag_begin type,data,len,label
win_drag_data 814 i Ipi uk_win_drag_data type,buf,cap
win_draw_text 815 v iisi uk_win_draw_text x,y,s,c
win_fullscreen_begin 816 l II uk_win_fullscreen_begin w,h
win_fullscreen_end 817 v - uk_win_fullscreen_end
win_geometry 818 i p uk_win_geometry out
win_list 819 i pi uk_win_list out,max
win_menu_command 820 i i uk_win_menu_command id
win_menu_get 821 u pipi uk_win_menu_get buf,cap,title,tcap
win_minimise 823 i i uk_win_minimise id
win_move 824 v ii uk_win_move x,y
win_new 825 i iiiisiL uk_win_new x,y,w,h,title,flags,canvas
win_place 829 i iii uk_win_place id,x,y
win_present 830 v - uk_win_present
win_raise 831 i i uk_win_raise id
win_read 832 i iiiiiiIi uk_win_read id,part,x,y,w,h,dst,stride
win_resizable 833 i iii uk_win_resizable on,min_w,min_h
win_resize 834 l ii uk_win_resize w,h
win_resize2 835 l iiI uk_win_resize2 w,h,stride
win_server 836 i p uk_win_server out
win_to_desk 837 i ii uk_win_to_desk id,n
win_tray_activate 838 i ii uk_win_tray_activate pid,kind
win_tray_clear 839 v - uk_win_tray_clear
win_tray_icon 840 i iI uk_win_tray_icon pid,px
win_tray_list 841 i pi uk_win_tray_list out,max
win_wallpaper_buffer 843 l II uk_win_wallpaper_buffer w,h
win_wallpaper_commit 844 v - uk_win_wallpaper_commit
win_wallpaper_generate 845 i iii uk_win_wallpaper_generate base,pts,seed
win_wheel_get 846 i - uk_win_wheel_get
win_wheel_set 847 v i uk_win_wheel_set lines
shell_grab 848 i i uk_shell_grab on
shell_tasks 849 i pi uk_shell_tasks out,max
