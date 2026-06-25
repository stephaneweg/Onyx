#
# Makefile -- cross-build the image codecs for Onyx (aarch64-none-elf + newlib):
# zlib, libpng and libjpeg. Produces  zlib/libz.a  libpng/libpng.a  libjpeg/libjpeg.a,
# which image-decoding apps link against. The Onyx-side decode wrapper is header-only
# in image.hpp; this builds the upstream libraries only (same split as ../tls/).
#
# Prereqs: the three upstream sources placed (git submodule / unpacked tarball) as
#     ./zlib     ./libpng     ./libjpeg          (versions pinned in README.md)
# and the aarch64-none-elf toolchain with newlib. Then:   make -C user/img
#
# A newlib C++ app then links them, e.g. (see ../bin/Makefile for the crt0 split):
#     g++ ... -I../img -I user/img/libpng -I user/img/zlib -I user/img/libjpeg \
#         app.cpp ... -Luser/img/libpng -Luser/img/zlib -Luser/img/libjpeg \
#         -lpng -ljpeg -lz -lm
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
AR      = $(PREFIX)ar

ZLIB ?= zlib
PNG  ?= libpng
JPEG ?= libjpeg

# Same world as the newlib apps (../bin/Makefile CFLAGS_LIBC): hardware FP
# (-mcpu=cortex-a72 = the Pi 4 core), PIC/SSP off. NOT -nostdlib -- these use newlib
# (malloc / memcpy / ...). Pure C: libpng's ARM-NEON path is off for a safe first build.
CF = -mcpu=cortex-a72 -O2 -fno-pic -fno-pie -fno-stack-protector \
     -ffunction-sections -fdata-sections

# ---- zlib : inflate/deflate stream API + the gz* file helpers. libpng only needs the
#             stream API, but NetSurf (utils/messages.c) reads gzipped files via gzopen,
#             so the gz* TUs are built too -- they bottom out in newlib open/read/close,
#             resolved at link time in a newlib app.
ZSRC = adler32 compress crc32 deflate gzclose gzlib gzread gzwrite \
       infback inffast inflate inftrees trees uncompr zutil
ZOBJ = $(addprefix $(ZLIB)/,$(addsuffix .o,$(ZSRC)))

# ---- libpng : decode + encode core (no the platform-specific arm/ intrinsics TU).
PNGSRC = png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil \
         pngset pngtrans pngwio pngwrite pngwtran pngwutil
PNGOBJ = $(addprefix $(PNG)/,$(addsuffix .o,$(PNGSRC)))

# ---- libjpeg (IJG v9f) : the library objects + jmemnobs (no-temp-file memory mgr).
#      Excludes the jpegtran app and the alt memory managers (jmemansi/dos/mac/name).
JSRC = jaricom jcapimin jcapistd jcarith jccoefct jccolor jcdctmgr jchuff jcinit \
       jcmainct jcmarker jcmaster jcomapi jcparam jcprepct jcsample jctrans jdapimin \
       jdapistd jdarith jdatadst jdatasrc jdcoefct jdcolor jddctmgr jdhuff jdinput \
       jdmainct jdmarker jdmaster jdmerge jdpostct jdsample jdtrans jerror jfdctflt \
       jfdctfst jfdctint jidctflt jidctfst jidctint jquant1 jquant2 jutils \
       jmemmgr jmemnobs
JOBJ = $(addprefix $(JPEG)/,$(addsuffix .o,$(JSRC)))

.PHONY: all zlib png jpeg clean
all: zlib png jpeg

# ---- zlib --------------------------------------------------------------
zlib: $(ZLIB)/libz.a
$(ZLIB)/zconf.h:
	test -f $@ || cp $(ZLIB)/zconf.h.in $@
$(ZOBJ): $(ZLIB)/zconf.h
$(ZLIB)/%.o: $(ZLIB)/%.c
	$(CC) $(CF) -I$(ZLIB) -c $< -o $@
$(ZLIB)/libz.a: $(ZOBJ)
	$(AR) rcs $@ $(ZOBJ)
	@echo "zlib   -> $@"

# ---- libpng (needs zlib's headers present) -----------------------------
png: $(PNG)/libpng.a
$(PNG)/pnglibconf.h:
	cp $(PNG)/scripts/pnglibconf.h.prebuilt $@
$(PNGOBJ): $(PNG)/pnglibconf.h $(ZLIB)/zconf.h
$(PNG)/%.o: $(PNG)/%.c
	$(CC) $(CF) -DPNG_ARM_NEON_OPT=0 -I$(PNG) -I$(ZLIB) -c $< -o $@
$(PNG)/libpng.a: $(PNGOBJ)
	$(AR) rcs $@ $(PNGOBJ)
	@echo "libpng -> $@"

# ---- libjpeg -----------------------------------------------------------
jpeg: $(JPEG)/libjpeg.a
$(JPEG)/jconfig.h: jconfig.h
	cp jconfig.h $@
$(JOBJ): $(JPEG)/jconfig.h
$(JPEG)/%.o: $(JPEG)/%.c
	$(CC) $(CF) -I$(JPEG) -c $< -o $@
$(JPEG)/libjpeg.a: $(JOBJ)
	$(AR) rcs $@ $(JOBJ)
	@echo "libjpeg -> $@"

clean:
	rm -f $(ZOBJ) $(ZLIB)/libz.a $(PNGOBJ) $(PNG)/libpng.a $(JOBJ) $(JPEG)/libjpeg.a
