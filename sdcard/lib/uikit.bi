# uikit.bi -- uikit for Onyx BASIC (#import uikit): made by tools/kitbi/kitbi.py from uikit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit uikit 722
lang_init 2 v - uk_lang_init
lang_load 3 b s uk_lang_load
lang_choose 4 b s uk_lang_choose
lang_chosen 5 s - uk_lang_chosen
tr 7 s s uk_tr
trc 8 s ss uk_trc
lang 9 s - uk_lang
add_item 687 v ps uk_add_item
ask_open 688 s s uk_ask_open
ask_save 689 s ss uk_ask_save
button 690 l piiiisc uk_button
checkbox 691 l piiiisic uk_checkbox
clear_items 692 v p uk_clear_items
dropdown 693 l piiiisc uk_dropdown
enable 694 v pi uk_enable
enabled 695 i p uk_enabled
focus 696 v p uk_focus
get_text 697 s p uk_get_text
get_value 698 i p uk_get_value
item_count 699 i p uk_item_count
label 700 l piiiis uk_label
listbox 701 l piiiisc uk_listbox
menu_item 702 v psssc uk_menu_item
message 703 i ssi uk_message
move 704 v piiii uk_move
progress 705 l piiiiii uk_progress
set_range 706 v pii uk_set_range
set_text 707 v ps uk_set_text
set_value 708 v pi uk_set_value
show 709 v pi uk_show
shown 710 i p uk_shown
slider 711 l piiiiiic uk_slider
textbox 712 l piiiisc uk_textbox
window 713 l siii uk_window
window_close 714 v p uk_window_close
window_height 715 i p uk_window_height
window_min_size 716 v pii uk_window_min_size
window_on_resize 717 v pc uk_window_on_resize
window_run 718 v p uk_window_run
window_step 719 i p uk_window_step
window_width 720 i p uk_window_width
window_wait 721 i p uk_window_wait
