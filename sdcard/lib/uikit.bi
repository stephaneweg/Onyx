# uikit.bi -- uikit for Onyx BASIC (#import uikit): made by tools/kitbi/kitbi.py from uikit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit uikit 781
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
