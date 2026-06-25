#
# netsurf-app.mk -- build the NetSurf browser for Onyx: the core + the framebuffer
# frontend, on the Onyx "onyx" libnsfb surface, linked against the cross-built libraries
# (user/netsurf, user/nsfb, user/img) + newlib. Bypasses NetSurf's buildsystem and compiles
# the TUs directly (as for the libraries). NetSurf brick 9.
#
# Prereqs: the netsurf source tree (NS=) + the libraries built (LIBROOT=), perl + a hosted
# x86 compiler (BUILD_CC) for the codegen tools. Usage:
#   make -f netsurf-app.mk NS=/path/netsurf LIBROOT=/path/with/lib<*> OUT=build
#
PREFIX   ?= aarch64-none-elf-
CC        = $(PREFIX)gcc
AR        = $(PREFIX)ar
BUILD_CC ?= x86_64-linux-gnu-gcc

HERE     := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
NS       ?= /mnt/c/Temp/netsurf
LIBROOT  ?= /mnt/c/Temp
ZUSER    ?= $(abspath $(HERE)/..)
ZKINC    ?= $(abspath $(HERE)/../../kernel/include)
OUT      ?= /tmp/nsbuild

# our cross-built libraries (brick 7 + nsfb + img)
WAP  := $(LIBROOT)/libwapcaplet
PU   := $(LIBROOT)/libparserutils
CSS  := $(LIBROOT)/libcss
DOM  := $(LIBROOT)/libdom
HB   := $(LIBROOT)/libhubbub
NSU  := $(LIBROOT)/libnsutils
GIF  := $(LIBROOT)/libnsgif
BMP  := $(LIBROOT)/libnsbmp
NSFB := $(LIBROOT)/libnsfb
PNG  := $(LIBROOT)/libpng-1.6.44
JPEG := $(LIBROOT)/jpeg-9f
ZLIB := $(LIBROOT)/zlib-1.3.1

CF = -mcpu=cortex-a72 -O2 -std=c99 -fno-pic -fno-pie -fno-stack-protector -fcommon \
     -Dnsframebuffer -DNDEBUG -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L \
     -include $(HERE)compat/onyx_nsconfig.h
INC = -I$(NS) -I$(NS)/include -I$(NS)/content/handlers -I$(NS)/frontends \
      -I$(HERE)compat -I$(HERE)gen -I$(OUT) -I$(ZUSER) -I$(ZKINC) \
      -I$(WAP)/include -I$(PU)/include -I$(CSS)/include -I$(DOM)/include -I$(HB)/include \
      -I$(NSU)/include -I$(GIF)/include -I$(BMP)/include -I$(NSFB)/include \
      -I$(DOM)/bindings -I$(DOM)/src -I$(PNG) -I$(JPEG) -I$(ZLIB)

# ---- source lists (excludes documented in README.md) -------------------
CORE_SRC := \
  $(wildcard $(NS)/utils/*.c) $(wildcard $(NS)/utils/http/*.c) \
  $(wildcard $(NS)/utils/nsurl/*.c) \
  $(wildcard $(NS)/content/*.c) \
  $(filter-out %/curl.c,$(wildcard $(NS)/content/fetchers/*.c)) \
  $(wildcard $(NS)/content/fetchers/about/*.c) \
  $(wildcard $(NS)/content/fetchers/file/*.c) \
  $(wildcard $(NS)/desktop/*.c) \
  $(wildcard $(NS)/content/handlers/css/*.c) \
  $(wildcard $(NS)/content/handlers/html/*.c) \
  $(wildcard $(NS)/content/handlers/text/*.c) \
  $(addprefix $(NS)/content/handlers/image/,bmp.c gif.c ico.c image.c image_cache.c jpeg.c png.c) \
  $(wildcard $(NS)/content/handlers/javascript/*.c) \
  $(wildcard $(NS)/content/handlers/javascript/none/*.c)

FB := $(NS)/frontends/framebuffer
FE_SRC := \
  $(addprefix $(FB)/,gui.c framebuffer.c schedule.c bitmap.c fetch.c findfile.c \
                     corewindow.c local_history.c clipboard.c font_internal.c) \
  $(wildcard $(FB)/fbtk/*.c)

# Onyx glue
ONYX_SRC := $(HERE)onyx_fetch.c $(HERE)compat/onyx_compat.c $(HERE)onyx_main.c

GENFONT := $(OUT)/font-ns-sans.c

# frontend toolbar/pointer/throbber bitmaps: res PNG -> image-NAME.c via a HOST convert_image
# (needs host libpng). name:respath pairs:
FB_IMG := \
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
IMG_C := $(foreach p,$(FB_IMG),$(OUT)/image-$(word 1,$(subst :, ,$(p))).c)

ALL_SRC := $(CORE_SRC) $(FE_SRC) $(ONYX_SRC) $(GENFONT) $(IMG_C)
ALL_OBJ := $(patsubst %.c,$(OUT)/o/%.o,$(subst /,_,$(ALL_SRC)))

.PHONY: all objs link
all: link

# ---- codegen: lib generators + the internal font ----------------------
$(OUT)/tools/convert_font: $(NS)/tools/convert_font.c
	@mkdir -p $(dir $@)
	$(BUILD_CC) -O2 -o $@ $<
$(GENFONT) $(OUT)/font-ns-sans.h: $(OUT)/tools/convert_font $(FB)/res/fonts/glyph_data
	@mkdir -p $(OUT)
	$(OUT)/tools/convert_font -H $(OUT)/font-ns-sans.h $(FB)/res/fonts/glyph_data $(GENFONT)

$(OUT)/tools/convert_image: $(NS)/tools/convert_image.c
	@mkdir -p $(dir $@)
	$(BUILD_CC) -O2 -I$(FB) -o $@ $< -lpng
# one generation rule per FB_IMG entry (name:respath)
define IMG_RULE
$(OUT)/image-$(word 1,$(subst :, ,$(1))).c: $(FB)/res/$(word 2,$(subst :, ,$(1))) $(OUT)/tools/convert_image
	@mkdir -p $(OUT)
	$(OUT)/tools/convert_image $(FB)/res/$(word 2,$(subst :, ,$(1))) $$@ $(word 1,$(subst :, ,$(1)))
endef
$(foreach p,$(FB_IMG),$(eval $(call IMG_RULE,$(p))))


# ---- compile: one rule, mangled object names (sources live in many trees) ----
define CC_RULE
$(OUT)/o/$(subst /,_,$(patsubst %.c,%.o,$(1))): $(1) $(OUT)/font-ns-sans.h
	@mkdir -p $(OUT)/o
	$$(CC) $$(CF) $$(INC) -c $$< -o $$@
endef
$(foreach s,$(ALL_SRC),$(eval $(call CC_RULE,$(s))))

# the frontend's main becomes netsurf_main; onyx_main.c provides the real main() (Onyx args).
$(OUT)/o/$(subst /,_,$(patsubst %.c,%.o,$(FB)/gui.c)): CF += -Dmain=netsurf_main

objs: $(ALL_OBJ)

# ---- link --------------------------------------------------------------
LDLIBS := -L$(CSS) -L$(DOM) -L$(HB) -L$(PU) -L$(WAP) -L$(NSU) -L$(GIF) -L$(BMP) \
          -L$(NSFB) -L$(PNG) -L$(JPEG) -L$(ZLIB) \
          -lcss -ldom -lhubbub -lparserutils -lwapcaplet -lnsutils -lnsgif -lnsbmp \
          -lnsfb -lpng -ljpeg -lz -lm
LDFLAGS := -Wl,-T,$(ZUSER)/user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none

link: objs
	$(CC) -mcpu=cortex-a72 -O2 -nostartfiles -fno-pic -fno-pie -I$(ZUSER) -I$(ZKINC) \
	  $(ZUSER)/libc/crt0libc.S $(ZUSER)/libc/onyx_syscalls.c \
	  $(ALL_OBJ) -Wl,--whole-archive -L$(NSFB) -lnsfb -Wl,--no-whole-archive \
	  $(LDLIBS) $(LDFLAGS) -o $(OUT)/netsurf.elf
	@echo "netsurf.elf: $$(stat -c %s $(OUT)/netsurf.elf 2>/dev/null) bytes"

# ---- stage onto the Onyx SD card ---------------------------------------
# Installs the browser as a desktop app (apps/netsurf.app) + its runtime resources under
# res/ (NETSURF_FB_RESPATH=/res -> SD:/res). The NetSurf resources are GPL upstream content
# regenerated here, so they are .gitignore'd, not committed. Messages is filtered out of
# FatMessages directly (the buildsystem's split-messages.pl needs perl HTML::Entities).
SDCARD ?= $(abspath $(HERE)../../sdcard)
PYTHON ?= python3
.PHONY: stage
stage: link
	@mkdir -p $(SDCARD)/apps/netsurf.app $(SDCARD)/res/en
	cp $(OUT)/netsurf.elf $(SDCARD)/apps/netsurf.app/main.elf
	printf '# Onyx application metadata\nname = NetSurf\ncategory = Internet\n' > $(SDCARD)/apps/netsurf.app/app.txt
	$(PYTHON) $(HERE)gen-icon.py $(SDCARD)/apps/netsurf.app/icon.bmp
	grep -E '^en\.(all|framebuffer)\.' $(NS)/resources/FatMessages | sed -E 's/^en\.(all|framebuffer)\.//' > $(SDCARD)/res/Messages
	for f in default.css quirks.css adblock.css internal.css; do cp $(NS)/resources/$$f $(SDCARD)/res/; done
	# user.css: NetSurf loads it as a UA stylesheet but upstream ships none -> stage an EMPTY
	# one. A missing resource: makes the fetch fail -> about:query/fetcherror, whose error
	# page itself reloads the UA stylesheets -> the missing one fails again -> INFINITE LOOP.
	cp $(NS)/resources/user.css $(SDCARD)/res/ 2>/dev/null || : > $(SDCARD)/res/user.css
	for f in welcome.html credits.html licence.html; do cp $(NS)/resources/en/$$f $(SDCARD)/res/; cp $(NS)/resources/en/$$f $(SDCARD)/res/en/; done
	# resource icons NetSurf maps (directory listings / toolbar) -- avoids more fetcherror loops
	@mkdir -p $(SDCARD)/res/icons
	for f in arrow-l content directory directory2 hotlist-add hotlist-rmv search; do cp $(NS)/resources/icons/$$f.png $(SDCARD)/res/icons/ 2>/dev/null || true; done
	-cp $(NS)/resources/favicon.png $(NS)/resources/netsurf.png $(NS)/resources/ca-bundle $(SDCARD)/res/
	@echo "staged NetSurf -> $(SDCARD)  (apps/netsurf.app + res/; resource path = /res)"

# ---- nstest: console smoke test of the library bricks (-> sdcard/bin/nstest.elf) -------
# Trimmed include set: NO -I$(DOM)/bindings here -- nstest.c includes the binding header
# <dom/bindings/hubbub/parser.h> (resolved via $(DOM)/include + the bindings symlink), and
# -I.../bindings would shadow libhubbub's <hubbub/*.h>.
NSTEST_INC = -I$(WAP)/include -I$(PU)/include -I$(CSS)/include -I$(DOM)/include \
             -I$(HB)/include -I$(NSU)/include -I$(ZLIB) \
             -I$(HERE)compat -I$(ZUSER) -I$(ZKINC)
.PHONY: nstest
nstest:
	@mkdir -p $(OUT)/o
	-ln -sfn ../../bindings $(DOM)/include/dom/bindings
	$(CC) $(CF) $(NSTEST_INC) -c $(HERE)nstest.c -o $(OUT)/o/nstest.o
	$(CC) $(CF) $(NSTEST_INC) -c $(HERE)compat/onyx_compat.c -o $(OUT)/o/oc.o
	$(CC) $(CF) $(NSTEST_INC) -c $(ZUSER)/libc/onyx_syscalls.c -o $(OUT)/o/sys.o
	$(CC) -mcpu=cortex-a72 -O2 -nostartfiles -fno-pic -fno-pie \
	  $(ZUSER)/libc/crt0libc.S $(OUT)/o/nstest.o $(OUT)/o/oc.o $(OUT)/o/sys.o \
	  $(LDLIBS) $(LDFLAGS) -o $(OUT)/nstest.elf
	@mkdir -p $(SDCARD)/bin
	cp $(OUT)/nstest.elf $(SDCARD)/bin/nstest.elf
	@echo "nstest -> $(SDCARD)/bin/nstest.elf ($$(stat -c %s $(OUT)/nstest.elf) bytes)"

clean:
	rm -rf $(OUT)
