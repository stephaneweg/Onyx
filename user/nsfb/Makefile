#
# Makefile -- cross-build libnsfb (NetSurf's framebuffer library) for Onyx, with the Onyx
# surface backend (onyx_surface.c) compiled in. Produces libnsfb.a, which a NetSurf-style
# app links. Same split as user/tls/ and user/img/: the upstream library is cross-built
# here, the Onyx glue (onyx_surface.c) lives in this repo and is built into the archive.
#
# We bypass NetSurf's buildsystem (it wants the shared netsurf-buildsystem makefiles) and
# compile the needed translation units directly: the core, the RAM + Onyx surfaces, and
# the software plotters. The SDL/X/VNC/Wayland surfaces are NOT built (they need host libs).
#
# Prereqs: the libnsfb source placed as ./libnsfb (git clone / submodule, version pinned in
# README.md) and the aarch64-none-elf toolchain with newlib. Then:  make -C user/nsfb
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
AR      = $(PREFIX)ar

NSFB       ?= libnsfb
ONYX_USER  ?= ..
ONYX_KINC  ?= ../../kernel/include

# Same world as the newlib apps (../bin/Makefile CFLAGS_LIBC): hardware FP, PIC/SSP off,
# NOT -nostdlib (libnsfb uses newlib malloc/memcpy/...). -std=c99 + the feature macros the
# upstream build sets. No -Werror (upstream's WARNFLAGS would reject our toolchain's nits).
# compat/ supplies a tiny <endian.h> shim (newlib has no glibc-style one; libnsfb's
# plot.h includes it then reads the GCC byte-order builtins). Keeps libnsfb unpatched.
CF = -mcpu=cortex-a72 -O2 -std=c99 -fno-pic -fno-pie -fno-stack-protector \
     -ffunction-sections -fdata-sections \
     -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L \
     -Icompat -I$(NSFB)/include -I$(NSFB)/src

# libnsfb translation units we need (decode/draw path only):
#   core      : context, cursor, palette  (dump.c is skipped: PPM export needs dup())
#   surfaces  : the registry + the RAM surface (template) -- SDL/X/VNC/WLD excluded
#   plotters  : api/generic/util + the 8/16/32bpp tables generic.c references
#               (32bpp-common.c and common.c are #included templates, NOT compiled;
#                24bpp/1bpp are behind ENABLE_* and unreferenced -> skipped)
NSFB_OBJ = \
  $(NSFB)/src/libnsfb.o $(NSFB)/src/cursor.o $(NSFB)/src/palette.o \
  $(NSFB)/src/surface/surface.o $(NSFB)/src/surface/ram.o \
  $(NSFB)/src/plot/api.o $(NSFB)/src/plot/generic.o $(NSFB)/src/plot/util.o \
  $(NSFB)/src/plot/8bpp.o $(NSFB)/src/plot/16bpp.o \
  $(NSFB)/src/plot/32bpp-xrgb8888.o $(NSFB)/src/plot/32bpp-xbgr8888.o

.PHONY: all clean
all: $(NSFB)/libnsfb.a

# upstream TUs
$(NSFB)/%.o: $(NSFB)/%.c
	$(CC) $(CF) -c $< -o $@

# Onyx surface backend (this repo): also needs the kapi headers.
$(NSFB)/src/surface/onyx_surface.o: onyx_surface.c
	$(CC) $(CF) -I$(ONYX_USER) -I$(ONYX_KINC) -c $< -o $@

$(NSFB)/libnsfb.a: $(NSFB_OBJ) $(NSFB)/src/surface/onyx_surface.o
	$(AR) rcs $@ $(NSFB_OBJ) $(NSFB)/src/surface/onyx_surface.o
	@echo "libnsfb -> $@  (Onyx surface = nsfb_type_from_name(\"onyx\"))"

clean:
	rm -f $(NSFB_OBJ) $(NSFB)/src/surface/onyx_surface.o $(NSFB)/libnsfb.a
