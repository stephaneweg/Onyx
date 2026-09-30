#
# netsurf-src.mk -- the NetSurf frontend + Onyx glue source lists shared by the Pi build
# (netsurf-app.mk) and the PC build (tools/tests/netsurf/host.mk), so both compile the same
# files. Paths: NS_FB_FILES relative to frontends/framebuffer/, ONYX_CXX_FILES absolute.
#
NS_FB_FILES := gui.c framebuffer.c schedule.c bitmap.c fetch.c findfile.c \
               corewindow.c local_history.c clipboard.c font_freetype.c onyx_paint.c \
               onyx_layer.c

# frontend toolbar/pointer/throbber bitmaps: res PNG -> image-NAME.c (name:respath pairs)
NS_FB_IMAGES := \
  left_arrow:icons/back.png right_arrow:icons/forward.png reload:icons/reload.png \
  stop_image:icons/stop.png history_image:icons/history.png \
  left_arrow_g:icons/back_g.png right_arrow_g:icons/forward_g.png reload_g:icons/reload_g.png \
  stop_image_g:icons/stop_g.png history_image_g:icons/history_g.png \
  scrolll:icons/scrolll.png scrollr:icons/scrollr.png scrollu:icons/scrollu.png scrolld:icons/scrolld.png \
  osk_image:icons/osk.png pointer_image:pointers/default.png hand_image:pointers/point.png \
  caret_image:pointers/caret.png menu_image:pointers/menu.png progress_image:pointers/progress.png \
  move_image:pointers/move.png \
  throbber0:throbber/throbber0.png throbber1:throbber/throbber1.png throbber2:throbber/throbber2.png \
  throbber3:throbber/throbber3.png throbber4:throbber/throbber4.png throbber5:throbber/throbber5.png \
  throbber6:throbber/throbber6.png throbber7:throbber/throbber7.png throbber8:throbber/throbber8.png

# FreeType's compile flags for the library's clients (the same options as the library:
# user/netsurf/freetype/)
NS_FT_CF = -DFB_USE_FREETYPE '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>'

# the Onyx glue in C++ (wtk): the window and its native toolbar
ONYX_CXX_FILES := $(dir $(lastword $(MAKEFILE_LIST)))onyx_chrome.cpp
