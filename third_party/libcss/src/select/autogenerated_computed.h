/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2017 The NetSurf Project
 */

#ifndef CSS_COMPUTED_COMPUTED_H_
#define CSS_COMPUTED_COMPUTED_H_

#include "select/calc.h"

typedef union {
	css_fixed value;
	lwc_string *calc;
} css_fixed_or_calc;


struct css_computed_style_i {
/*
 * Property                       Size (bits)     Size (bytes)
 * ---                            ---             ---
 * align_content                    3             
 * align_items                      3             
 * align_self                       3             
 * aspect_ratio                     2               8
 * background_attachment            2             
 * background_clip                  3             
 * background_color                 2               4
 * background_image                 1             sizeof(ptr)
 * background_position              1 + 10          8
 * background_repeat                3             
 * background_size                  3 + 10          8
 * border_bottom_color              2               4
 * border_bottom_left_radius        1 + 5           4
 * border_bottom_right_radius       1 + 5           4
 * border_bottom_style              4             
 * border_bottom_width              3 + 5           4
 * border_collapse                  2             
 * border_left_color                2               4
 * border_left_style                4             
 * border_left_width                3 + 5           4
 * border_right_color               2               4
 * border_right_style               4             
 * border_right_width               3 + 5           4
 * border_spacing                   1 + 10          8
 * border_top_color                 2               4
 * border_top_left_radius           1 + 5           4
 * border_top_right_radius          1 + 5           4
 * border_top_style                 4             
 * border_top_width                 3 + 5           4
 * bottom                           2 + 5           4
 * box_shadow                       3 + 20         20
 * box_sizing                       2             
 * break_after                      4             
 * break_before                     4             
 * break_inside                     4             
 * caption_side                     2             
 * clear                            3             
 * clip                             6 + 20         16
 * color                            1               4
 * column_count                     2               4
 * column_fill                      2             
 * column_gap                       2 + 5           4
 * column_rule_color                2               4
 * column_rule_style                4             
 * column_rule_width                3 + 5           4
 * column_span                      2             
 * column_width                     2 + 5           4
 * direction                        2             
 * display                          5             
 * empty_cells                      2             
 * fill                             4               4 + sizeof(ptr)
 * fill_opacity                     1               4
 * fill_rule                        2             
 * flex_basis                       2 + 5           4
 * flex_direction                   3             
 * flex_grow                        1               4
 * flex_shrink                      1               4
 * flex_wrap                        2             
 * float                            2             
 * font_size                        4 + 5           4
 * font_style                       2             
 * font_variant                     2             
 * font_weight                      4             
 * grid_auto_columns                2             sizeof(ptr)
 * grid_auto_flow                   3             
 * grid_auto_rows                   2             sizeof(ptr)
 * grid_column_end                  2             sizeof(ptr)
 * grid_column_start                2             sizeof(ptr)
 * grid_row_end                     2             sizeof(ptr)
 * grid_row_start                   2             sizeof(ptr)
 * grid_template_areas              2             sizeof(ptr)
 * grid_template_columns            2             sizeof(ptr)
 * grid_template_rows               2             sizeof(ptr)
 * height                           2 + 5           4
 * justify_content                  3             
 * justify_items                    3             
 * justify_self                     3             
 * left                             2 + 5           4
 * letter_spacing                   2 + 5           4
 * line_height                      2 + 5           4
 * list_style_image                 1             sizeof(ptr)
 * list_style_position              2             
 * list_style_type                  6             
 * margin_bottom                    2 + 5           4
 * margin_left                      2 + 5           4
 * margin_right                     2 + 5           4
 * margin_top                       2 + 5           4
 * mask_image                       2             sizeof(ptr)
 * mask_position                    2             sizeof(ptr)
 * mask_repeat                      2             sizeof(ptr)
 * mask_size                        2             sizeof(ptr)
 * max_height                       2 + 5           4
 * max_width                        2 + 5           4
 * min_height                       2 + 5           4
 * min_width                        2 + 5           4
 * object_fit                       3             
 * object_position                  1 + 10          8
 * opacity                          1               4
 * order                            1               4
 * orphans                          1               4
 * outline_color                    2               4
 * outline_style                    4             
 * outline_width                    3 + 5           4
 * overflow_x                       3             
 * overflow_y                       3             
 * padding_bottom                   1 + 5           4
 * padding_left                     1 + 5           4
 * padding_right                    1 + 5           4
 * padding_top                      1 + 5           4
 * page_break_after                 3             
 * page_break_before                3             
 * page_break_inside                2             
 * position                         3             
 * right                            2 + 5           4
 * rotate                           2             sizeof(ptr)
 * row_gap                          2 + 5           4
 * scale                            2             sizeof(ptr)
 * stop_color                       2               4
 * stop_opacity                     1               4
 * stroke                           4               4 + sizeof(ptr)
 * stroke_dasharray                 2             sizeof(ptr)
 * stroke_dashoffset                1 + 5           4
 * stroke_linecap                   2             
 * stroke_linejoin                  3             
 * stroke_miterlimit                1               4
 * stroke_opacity                   1               4
 * stroke_width                     1 + 5           4
 * table_layout                     2             
 * text_align                       4             
 * text_decoration                  5             
 * text_indent                      1 + 5           4
 * text_overflow                    2             
 * text_shadow                      2 + 15         16
 * text_transform                   3             
 * top                              2 + 5           4
 * transform                        2             sizeof(ptr)
 * translate                        2             sizeof(ptr)
 * unicode_bidi                     2             
 * vertical_align                   4 + 5           4
 * visibility                       2             
 * white_space                      3             
 * widows                           1               4
 * width                            2 + 5           4
 * word_spacing                     2 + 5           4
 * writing_mode                     2             
 * z_index                          2               4
 * 
 * Encode content as an array of content items, terminated with a blank entry.
 * 
 * content                          2             sizeof(ptr)
 * 
 * Encode counter_increment as an array of name, value pairs, terminated with a
 * blank entry.
 * 
 * counter_increment                1             sizeof(ptr)
 * 
 * Encode counter_reset as an array of name, value pairs, terminated with a
 * blank entry.
 * 
 * counter_reset                    1             sizeof(ptr)
 * 
 * Encode cursor uri(s) as an array of string objects, terminated with a blank
 * entry
 * 
 * cursor                           5             sizeof(ptr)
 * 
 * Encode font family as an array of string objects, terminated with a blank
 * entry.
 * 
 * font_family                      3             sizeof(ptr)
 * 
 * Encode quotes as an array of string objects, terminated with a blank entry.
 * 
 * quotes                           1             sizeof(ptr)
 * 
 * ---                            ---             ---
 *                                645 bits        344 + 28sizeof(ptr) bytes
 *                                ===================
 *                                425 + 28sizeof(ptr) bytes
 * 
 * Bit allocations:
 * 
 * 0  bbbbbbbbbbbbbbbbbbbbbbbvvvvvvvvv
 * box_shadow; vertical_align
 * 
 * 1  bbbbbbbboooooooorrrrrrrrdddddddd
 * border_top_width; border_right_width; border_left_width; border_bottom_width
 * 
 * 2  cccccccccccccccccccccccccctttttt
 * clip; text_indent
 * 
 * 3  bbbbbbbbbbbooooooooccccccccttttt
 * background_position; outline_width; column_rule_width; text_decoration
 * 
 * 4  wwwwwwwiiiiiiitttttttrrrrrrreeee
 * word_spacing; width; top; row_gap; text_align
 * 
 * 5  rrrrrrrmmmmmmmiiiiiiiaaaaaaassss
 * right; min_width; min_height; max_width; stroke
 * 
 * 6  mmmmmmmaaaaaaarrrrrrrgggggggoooo
 * max_height; margin_top; margin_right; margin_left; outline_style
 * 
 * 7  mmmmmmmllllllleeeeeeefffffffoooo
 * margin_bottom; line_height; letter_spacing; left; font_weight
 * 
 * 8  hhhhhhhfffffffcccccccoooooooiiii
 * height; flex_basis; column_width; column_gap; fill
 * 
 * 9  tttttttttttttttttbbbbbbbbbbbbbzz
 * text_shadow; background_size; z_index
 * 
 * 10 ppppppaaaaaallllllbbbbbbooooooww
 * padding_left; padding_bottom; list_style_type; border_top_right_radius;
 * border_top_left_radius; writing_mode
 * 
 * 11 bbbbbboooooodddddcccccllllrrrrvv
 * border_bottom_right_radius; border_bottom_left_radius; display; cursor;
 * column_rule_style; break_inside; visibility
 * 
 * 12 bbbbrrrrooooddddeeeettttwwwxxxuu
 * break_before; break_after; border_top_style; border_right_style;
 * border_left_style; border_bottom_style; white_space; text_transform;
 * unicode_bidi
 * 
 * 13 ssspppaaagggooovvvbbbjjjuuutttrr
 * stroke_linejoin; position; page_break_before; page_break_after; overflow_y;
 * overflow_x; object_fit; justify_self; justify_items; justify_content;
 * translate
 * 
 * 14 ttssrrooccaappuummkkiiggllddeeGG
 * table_layout; stroke_linecap; stroke_dasharray; stop_color; scale; rotate;
 * page_break_inside; outline_color; mask_size; mask_repeat; mask_position;
 * mask_image; list_style_position; grid_template_rows; grid_template_columns;
 * grid_template_areas
 * 
 * 15 ggrriiddaauuffoolleeFFmmccnnssCC
 * grid_row_start; grid_row_end; grid_column_start; grid_column_end;
 * grid_auto_rows; grid_auto_columns; font_variant; font_style; float;
 * flex_wrap; fill_rule; empty_cells; direction; content; column_span;
 * column_rule_color
 * 
 * 16 ooooooooooobbbbbbbbbbbfffffffffw
 * object_position; border_spacing; font_size; widows
 * 
 * 17 bbbbbbbssssssttttttppppppaaaaaar
 * bottom; stroke_width; stroke_dashoffset; padding_top; padding_right;
 * stroke_opacity
 * 
 * 18 gggffflllcccbbbaaaiiinnnooottees
 * grid_auto_flow; font_family; flex_direction; clear; background_repeat;
 * background_clip; align_self; align_items; align_content; transform;
 * text_overflow; stroke_miterlimit
 * 
 * 19 ccooaabbrrddeellttkkggsspqhOiyfx
 * column_fill; column_count; caption_side; box_sizing; border_top_color;
 * border_right_color; border_left_color; border_collapse; border_bottom_color;
 * background_color; background_attachment; aspect_ratio; stop_opacity; quotes;
 * orphans; order; opacity; list_style_image; flex_shrink; flex_grow
 * 
 * 20 fcolb...........................
 * fill_opacity; counter_reset; counter_increment; color; background_image
 */
	uint32_t bits[21];
	
	css_fixed aspect_ratio_a;
	css_fixed aspect_ratio_b;
	css_color background_color;
	lwc_string *background_image;
	css_fixed background_position_a;
	css_fixed background_position_b;
	css_fixed background_size_a;
	css_fixed background_size_b;
	css_color border_bottom_color;
	css_fixed border_bottom_left_radius;
	css_fixed border_bottom_right_radius;
	css_fixed border_bottom_width;
	css_color border_left_color;
	css_fixed border_left_width;
	css_color border_right_color;
	css_fixed border_right_width;
	css_fixed border_spacing_a;
	css_fixed border_spacing_b;
	css_color border_top_color;
	css_fixed border_top_left_radius;
	css_fixed border_top_right_radius;
	css_fixed border_top_width;
	css_fixed bottom;
	css_fixed box_shadow_a;
	css_fixed box_shadow_b;
	css_fixed box_shadow_c;
	css_fixed box_shadow_d;
	css_color box_shadow_e;
	css_fixed clip_a;
	css_fixed clip_b;
	css_fixed clip_c;
	css_fixed clip_d;
	css_color color;
	int32_t column_count;
	css_fixed column_gap;
	css_color column_rule_color;
	css_fixed column_rule_width;
	css_fixed column_width;
	css_color fill_a;
	lwc_string *fill_b;
	css_fixed fill_opacity;
	css_fixed flex_basis;
	css_fixed flex_grow;
	css_fixed flex_shrink;
	css_fixed font_size;
	lwc_string *grid_auto_columns;
	lwc_string *grid_auto_rows;
	lwc_string *grid_column_end;
	lwc_string *grid_column_start;
	lwc_string *grid_row_end;
	lwc_string *grid_row_start;
	lwc_string *grid_template_areas;
	lwc_string *grid_template_columns;
	lwc_string *grid_template_rows;
	css_fixed height;
	css_fixed left;
	css_fixed letter_spacing;
	css_fixed line_height;
	lwc_string *list_style_image;
	css_fixed margin_bottom;
	css_fixed margin_left;
	css_fixed margin_right;
	css_fixed margin_top;
	lwc_string *mask_image;
	lwc_string *mask_position;
	lwc_string *mask_repeat;
	lwc_string *mask_size;
	css_fixed max_height;
	css_fixed max_width;
	css_fixed min_height;
	css_fixed min_width;
	css_fixed object_position_a;
	css_fixed object_position_b;
	css_fixed opacity;
	int32_t order;
	int32_t orphans;
	css_color outline_color;
	css_fixed outline_width;
	css_fixed padding_bottom;
	css_fixed padding_left;
	css_fixed padding_right;
	css_fixed padding_top;
	css_fixed right;
	lwc_string *rotate;
	css_fixed row_gap;
	lwc_string *scale;
	css_color stop_color;
	css_fixed stop_opacity;
	css_color stroke_a;
	lwc_string *stroke_b;
	lwc_string *stroke_dasharray;
	css_fixed stroke_dashoffset;
	css_fixed stroke_miterlimit;
	css_fixed stroke_opacity;
	css_fixed stroke_width;
	css_fixed text_indent;
	css_fixed text_shadow_a;
	css_fixed text_shadow_b;
	css_fixed text_shadow_c;
	css_color text_shadow_d;
	css_fixed top;
	lwc_string *transform;
	lwc_string *translate;
	css_fixed vertical_align;
	int32_t widows;
	css_fixed_or_calc width;
	css_fixed word_spacing;
	int32_t z_index;
};

struct css_computed_style {
	struct css_computed_style_i i;
	
	css_computed_content_item *content;
	css_computed_counter *counter_increment;
	css_computed_counter *counter_reset;
	lwc_string **cursor;
	lwc_string **font_family;
	lwc_string **quotes;
	
	struct css_computed_style *next;
	uint32_t count;
	uint32_t bin;
	css_calculator *calc;
};

#endif
