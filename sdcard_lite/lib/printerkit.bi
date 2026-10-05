# printerkit.bi -- printerkit for Onyx BASIC (#import printerkit): made by tools/kitbi/kitbi.py from printerkit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
kit printerkit 33
abort 0 v p print_abort
begin 1 l ps print_begin
dialog 2 i pp print_dialog
end 3 i p print_end
font 4 i psi print_font
font_data 5 i psi print_font_data
font_metrics 6 v pifFFF print_font_metrics
frame 7 v pfffffi print_frame
glyph 8 v pifffiiii print_glyph
image 9 v ppiiffffi print_image
job_cancel 10 i i print_job_cancel
jobs 11 i pi print_jobs
jobs_forget 12 v - print_jobs_forget
line 13 v pfffffi print_line
page 14 i pff print_page
path_close 15 v p print_path_close
path_curve 16 v pffffff print_path_curve
path_fill 17 v pi print_path_fill
path_line 18 v pff print_path_line
path_move 19 v pff print_path_move
path_stroke 20 v pfi print_path_stroke
printer_add 21 i sspi print_printer_add
printer_default 22 i s print_printer_default
printer_media 23 i sipipi print_printer_media
printer_remove 24 i s print_printer_remove
printer_status 25 i spi print_printer_status
printers 26 i pi print_printers
rect 27 v pffffi print_rect
setup_default 28 v p print_setup_default
setup_paper 29 i psi print_setup_paper
text 30 f pifffsi print_text
text_width 31 f pifs print_text_width
printers_find 32 i pi print_printers_find
