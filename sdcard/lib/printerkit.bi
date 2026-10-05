# printerkit.bi -- printerkit for Onyx BASIC (#import printerkit): made by tools/kitbi/kitbi.py from printerkit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit printerkit 33
struct PrintSetup 500 PrintSetup
field size 0 u
field printer 4 a 64
field media 68 a 64
field paper_w 132 f
field paper_h 136 f
field margin_l 140 f
field margin_t 144 f
field margin_r 148 f
field margin_b 152 f
field orientation 156 i
field copies 160 i
field mono 164 i
field quality 168 i
field from 172 i
field to 176 i
field output 180 a 256
struct PrintDialogInfo 40 PrintDialogInfo
field size 0 u
field title 8 l
field pages 16 i
field current 20 i
field flags 24 i
field paper_w 28 f
field paper_h 32 f
struct PrintJobInfo 256 PrintJobInfo
field id 0 u
field state 4 i
field pages 8 i
field page 12 i
field printer 16 a 64
field title 80 a 80
field message 160 a 96
struct PrintPrinter 380 PrintPrinter
field name 0 a 64
field kind 64 a 8
field uri 72 a 160
field model 232 a 64
field color 296 i
field copies 300 i
field dpi 304 i
field is_default 308 i
field quality 312 u
field media_default 316 a 64
struct PrintFound 164 PrintFound
field address 0 a 32
field name 32 a 64
field model 96 a 64
field usable 160 i
abort 0 v p print_abort j
begin 1 l ps print_begin s,title
dialog 2 i pp print_dialog s,info
end 3 i p print_end j
font 4 i psi print_font j,family,style
font_data 5 i psi print_font_data j,ttf,len
font_metrics 6 v pifFFF print_font_metrics j,font,size,ascent,descent,line
frame 7 v pfffffi print_frame j,x,y,w,h,width,rgb
glyph 8 v pifffiiii print_glyph j,font,size,x,baseline,glyph,unicode,rgb,style
image 9 v ppiiffffi print_image j,px,pw,ph,x,y,w,h,flags
job_cancel 10 i i print_job_cancel id
jobs 11 i pi print_jobs out,max
jobs_forget 12 v - print_jobs_forget
line 13 v pfffffi print_line j,x0,y0,x1,y1,width,rgb
page 14 i pff print_page j,w,h
path_close 15 v p print_path_close j
path_curve 16 v pffffff print_path_curve j,x1,y1,x2,y2,x3,y3
path_fill 17 v pi print_path_fill j,rgb
path_line 18 v pff print_path_line j,x,y
path_move 19 v pff print_path_move j,x,y
path_stroke 20 v pfi print_path_stroke j,width,rgb
printer_add 21 i sspi print_printer_add name,address,err,cap
printer_default 22 i s print_printer_default name
printer_media 23 i sipipi print_printer_media printer,i,name,ncap,label,lcap
printer_remove 24 i s print_printer_remove name
printer_status 25 i spi print_printer_status name,text,cap
printers 26 i pi print_printers out,max
rect 27 v pffffi print_rect j,x,y,w,h,rgb
setup_default 28 v p print_setup_default s
setup_paper 29 i psi print_setup_paper s,media,orientation
text 30 f pifffsi print_text j,font,size,x,baseline,utf8,rgb
text_width 31 f pifs print_text_width j,font,size,utf8
printers_find 32 i pi print_printers_find out,max
