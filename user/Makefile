#
# Makefile -- userland ELF programs (EL0), built with the same aarch64-none-elf
# toolchain, linked at USER_VA_BASE (user.ld). Integer-only by default
# (-mgeneral-regs-only). An app may opt into hardware float by overriding the
# flags (drop -mgeneral-regs-only, add -mcpu=cortex-a72): the kernel now saves the
# full FP/SIMD state on every trap (kern/trapframe.h), so FP is safe across context
# switches. See user/bin/Makefile (fptest) for the recipe.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
AR      = $(PREFIX)ar

# -I../kernel/include: apps include <kern/kapi_abi.h> (the shared ABI). Apps call
# the kernel through the ABI table at a fixed VA, so they DON'T link against kernel
# addresses and need not be rebuilt when the kernel changes.
CFLAGS  = -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only \
	  -O2 -Wall -Wextra -fno-stack-protector -I../kernel/include
# C++ apps: same base, freestanding C++ subset (no exceptions/RTTI/STL; function-
# local statics are single-threaded; static dtors are not run -- see onyxpp.hpp).
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
LDFLAGS = -Wl,-T,user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none
# Freestanding apps have no libc: alias the C memory primitives GCC may emit on its own
# onto the kapi ones (kapi.h, ABI v36 -> the kernel's Circle implementations).
KAPI_ALIASES = -Wl,--defsym,memset=kapi_memset -Wl,--defsym,memcpy=kapi_memcpy \
	       -Wl,--defsym,memmove=kapi_memmove

# All graphical apps live in Apps/<name>/main.cpp (one folder per app) and build to
# <name>.elf here at user/ (so `make stage` keeps globbing ../user/*.elf; it stages
# each one as apps/<name>.app/main, without extension). Every graphical
# app is C++ on the wtk toolkit now; the /bin console tools stay C in bin/.
APPS = tinycalc tinypad applist panel cppdemo \
       demoA demoB demoC demoD demoE demoF inidemo \
       tetris snake same terminal \
       2048 life pong sokoban \
       mandelbrot minesweeper \
       eyes taskman voronoy irc memmon spin wtkdemo shell notifyd shutdown plasma shelf ask agenda imageview widgets qbasic fmtracker lisa invaders pipes solitaire freecell graphcalc iconedit rtfview gbemu gbaemu nesemu snesemu teapot gpudemo n64emu gcemu wifimenu dock lock cardfile ledger

# The apps whose text is FreeType's (DejaVu Sans through wtk's face, ft/wtkface.h): newlib apps as
# the Calendar (the rule below). Every new app joins them (docs/03, "Fonts").
FT_APPS = control theme config wpaconf padconf dockconf soundconf displayconf keyconf gamelib setup menubar fileviewer
FT_APP_ELFS = $(FT_APPS:=.elf)

APP_ELFS = $(APPS:=.elf)

all: $(APP_ELFS) $(FT_APP_ELFS) writer.elf sheet.elf koton.elf courier.elf calendar.elf paint.elf archiver.elf clipd.elf clipboard.elf screenshot.elf media.elf pdf.elf mail.elf photos.elf pkgman.elf pkgd.elf plugins bin bin/basic.elf doom

# Doom (doom/: doomgeneric from third_party + the Onyx layer, against newlib) -> doom.elf.
.PHONY: doom
doom: wtk/libwtk.a
	@$(MAKE) --no-print-directory -C doom

# /bin console tools live in bin/ (their own Makefile).
.PHONY: bin
bin: v3d/libv3d.a gpucomp/libgpucomp.a
	@$(MAKE) --no-print-directory -C bin

# Apps/<name>/main.cpp -> <name>.elf. -I. so a nested source finds the user-side headers.
# wtk toolkit: compiled once into a static library, linked into every app.
WTK_HDR = $(wildcard wtk/*.h)
WTK_OBJ = $(patsubst %.cpp,%.o,$(wildcard wtk/*.cpp))
wtk/%.o: wtk/%.cpp $(WTK_HDR)
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@
# canvas.cpp is the hot pixel path (clear/fillRect/putOther): build it with NEON/FP
# (drop -mgeneral-regs-only, target the A72). The kernel saves full FP/SIMD on every
# trap, so SIMD is safe even though the rest of wtk and every app stay integer-only --
# the NEON is confined to this TU and Canvas's public signatures use no FP regs, so the
# calling ABI is unchanged. This explicit rule overrides the wtk/%.o pattern above.
CXXFLAGS_NEON = $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72
wtk/canvas.o: wtk/canvas.cpp $(WTK_HDR)
	$(CXX) $(CXXFLAGS_NEON) -I. -c $< -o $@
# The image codecs (stb_image, simplewebp, PCX) use floating point: FP/SIMD too.
wtk/imgload.o: wtk/imgload.cpp img/imgload.hpp img/stb_image.h img/simplewebp.h
	$(CXX) $(CXXFLAGS_NEON) -Wno-unused-parameter -Wno-sign-compare -I. -c $< -o $@
wtk/libwtk.a: $(WTK_OBJ)
	$(AR) rcs $@ $^

$(APP_ELFS): %.elf: Apps/%/main.cpp crt0.S user.ld kapi.h applib.h launch.h httpc.h bmp.hpp umm.h onyxpp.hpp game.h cards.h rtf.h gamepad.h emucore.h wtk/libwtk.a basic/libbasic.a gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a n64/libn64.a gc/libgc.a v3d/libv3d.a
	$(CXX) $(CXXFLAGS) -I. -I$(V3D_MESA) $(LDFLAGS) $(KAPI_ALIASES) crt0.S $< wtk/libwtk.a basic/libbasic.a gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a n64/libn64.a gc/libgc.a v3d/libv3d.a -o $@

# ---- Writer: a newlib app (FreeType wants a libc: ../libc's crt0libc.S + the kapi syscall layer,
# as Doom's), with the apps' TrueType-only FreeType (ft/: its options and modules -- no bytecode
# interpreter, no compressed / web / bitmap fonts --, the upstream sources of third_party).
NL_CFLAGS  = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -nostartfiles \
	     -ffunction-sections -fdata-sections -I. -I../kernel/include
NL_CXXFLAGS = $(NL_CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
NL_LDFLAGS = -Wl,-T,user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none -Wl,--gc-sections
FT_SRC = ../third_party/freetype-2.14.3
vpath ft%.c $(FT_SRC)/src/base
vpath autofit.c $(FT_SRC)/src/autofit
vpath truetype.c $(FT_SRC)/src/truetype
vpath sfnt.c $(FT_SRC)/src/sfnt
vpath smooth.c $(FT_SRC)/src/smooth
FT_OBJ = $(addprefix ft/,ftsystem.o ftinit.o ftdebug.o ftbase.o ftbitmap.o ftsynth.o autofit.o truetype.o sfnt.o smooth.o)
ft/%.o: %.c ft/onyx_ftoption.h ft/onyx_ftmodule.h
	$(CC) $(NL_CFLAGS) -w -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' \
	      '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' -Ift -I$(FT_SRC)/include -c $< -o $@
ft/libft.a: $(FT_OBJ)
	$(AR) rcs $@ $^
libc/crt0libc.o: libc/crt0libc.S
	$(CC) $(NL_CFLAGS) -c $< -o $@
libc/onyx_syscalls.o: libc/onyx_syscalls.c kapi.h
	$(CC) $(NL_CFLAGS) -c $< -o $@
# Writer: its model, layout and files in Apps/writer/*.h (the mail merge reads Cardfile's forms: its model.h;
# .docx / .odt: the PNG writer's zip / deflate).
writer.elf: Apps/writer/main.cpp $(wildcard Apps/writer/*.h) Apps/cardfile/model.h img/pngsave.hpp ft/fonts.h ft/libft.a \
	    wtk/libwtk.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h docguard.h clipboard.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a ft/libft.a -lm -o $@
# The spreadsheet: a newlib app as Writer (FreeType, stdio, doubles) -- its engine and files in
# Apps/sheet/*.h, Writer's icons, the PNG writer's zip / deflate for the .xlsx.
sheet.elf: Apps/sheet/main.cpp $(wildcard Apps/sheet/*.h) Apps/writer/icons.h img/pngsave.hpp ft/fonts.h ft/libft.a \
	   wtk/libwtk.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h docguard.h clipboard.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a ft/libft.a -lm -o $@

# Courier, the HTTP client (Apps/courier: a Postman for Onyx): a newlib app as Writer (FreeType's text through
# wtk), with mbedTLS for https:// (../tls/onyx_tls.hpp over the kapi sockets; the libraries as user/bin's
# TLS tools link them).
COURIER_MBEDTLS ?= ../third_party/mbedtls-3.6.3
courier.elf: Apps/courier/main.cpp $(wildcard Apps/courier/*.h) http.hpp tls/onyx_tls.hpp json.hpp clipboard.h docguard.h 	     img/imgload.hpp ft/libft.a ft/wtkface.h wtk/libwtk.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include -I$(COURIER_MBEDTLS)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< 	       wtk/libwtk.a ft/libft.a -L$(COURIER_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Mail, the mail client (Apps/mail: docs/mail/README.md): a newlib app as Writer (FreeType's text through wtk), its
# protocols and its HTML renderer in mail/ (MIT), mbedTLS for TLS (as Courier), the image codecs (wtk's imgload).
MAIL_MBEDTLS ?= ../third_party/mbedtls-3.6.3
mail.elf: Apps/mail/main.cpp $(wildcard Apps/mail/*.h) $(wildcard mail/*.h) Apps/cardfile/model.h tls/onyx_tls.hpp clipboard.h notify.h fileassoc.h \
	  img/imgload.hpp ft/libft.a ft/fonts.h ft/wtkface.h wtk/libwtk.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Wno-format-truncation -Ift -I$(FT_SRC)/include -I$(MAIL_MBEDTLS)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a ft/libft.a -L$(MAIL_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Photos, the photo library (Apps/photos: docs/photos/README.md): a newlib app as Writer (FreeType's text through
# wtk), the image codecs (wtk's imgload), the JPEG / PNG writers (img/pngsave.hpp), the PDF writer (pdf/pdfwrite.h).
photos.elf: Apps/photos/main.cpp $(wildcard Apps/photos/*.h) img/imgload.hpp img/pngsave.hpp pdf/pdfwrite.h clipboard.h notify.h trash.h wallpaper.h \
	  ft/libft.a ft/fonts.h ft/wtkface.h wtk/libwtk.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Wno-format-truncation -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a ft/libft.a -lm -o $@

# Archiver, the archive manager (Apps/archiver: docs/archiver/README.md): a newlib app as Writer (FreeType's text
# through wtk), its engine and widgets headers included by main.cpp; zlib (Deflate) built from third_party into
# zlib/libz.a.
ZLIB_SRC = ../third_party/zlib-1.3.1
ZLIB_OBJ = $(addprefix zlib/,adler32.o crc32.o deflate.o inflate.o inffast.o inftrees.o trees.o zutil.o)
zlib/%.o: $(ZLIB_SRC)/%.c
	@mkdir -p zlib
	$(CC) $(NL_CFLAGS) -w -c $< -o $@
zlib/libz.a: $(ZLIB_OBJ)
	$(AR) rcs $@ $^
archiver.elf: Apps/archiver/main.cpp $(wildcard Apps/archiver/*.h) fileassoc.h ft/libft.a ft/wtkface.h wtk/libwtk.a zlib/libz.a \
	      libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -IApps/archiver -Ift -I$(FT_SRC)/include -I$(ZLIB_SRC) $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a ft/libft.a zlib/libz.a -lm -o $@

# The shared clipboard (docs/clipboard/README.md): clipd, its service (no window, newlib), and clipboard, its widget
# (the dock's button; FreeType's text). The apps reach clipd through clipboard.h (clipproto.h).
clipd.elf: Apps/clipd/main.cpp Apps/clipd/ring.h clipproto.h notify.h libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -IApps/clipd $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< -o $@
clipboard.elf: Apps/clipboard/main.cpp clipboard.h clipproto.h ft/libft.a ft/wtkface.h wtk/libwtk.a \
	       libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a ft/libft.a -lm -o $@

# The packages (docs/pkg/README.md): pkgman, the Package Manager (the Control Panel's applet), and pkgd, the
# update daemon (no window) -- on pkg/pkglib.h as bin/pkg: HTTPS (mbedTLS), the Archiver's ZIP engine, zlib.
PKG_MBEDTLS ?= ../third_party/mbedtls-3.6.3
PKG_DEPS = pkg/pkglib.h http.hpp tls/onyx_tls.hpp $(wildcard Apps/archiver/*.h) zlib/libz.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
pkgman.elf: Apps/pkgman/main.cpp $(PKG_DEPS) bmp.hpp ft/libft.a ft/wtkface.h wtk/libwtk.a
	$(CXX) $(NL_CXXFLAGS) -DPKG_NET -DONYX_HTTP_TLS -Wno-format-truncation -Ift -I$(FT_SRC)/include -I$(ZLIB_SRC) -I$(PKG_MBEDTLS)/include \
	       $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< wtk/libwtk.a ft/libft.a zlib/libz.a \
	       -L$(PKG_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@
pkgd.elf: Apps/pkgd/main.cpp $(PKG_DEPS) notify.h
	$(CXX) $(NL_CXXFLAGS) -DPKG_NET -DONYX_HTTP_TLS -Wno-format-truncation -I$(ZLIB_SRC) -I$(PKG_MBEDTLS)/include \
	       $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< zlib/libz.a \
	       -L$(PKG_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Screenshot, the screen capture tool (Apps/screenshot: docs/screenshot/README.md): a newlib app (FreeType's text
# through wtk), the PNG / JPEG / BMP writers of img/pngsave.hpp, the clipboard (clipd) and notifyd.
# Paint: a newlib app (FreeType's text, its Text tool), its canvas composited by the GPU (gpucomp: the
# layers as textures, their blend modes); its pieces in Apps/paint/*.h.
paint.elf: Apps/paint/main.cpp $(wildcard Apps/paint/*.h) clipboard.h docguard.h img/imgload.hpp img/pngsave.hpp ft/libft.a \
	   ft/wtkface.h wtk/libwtk.a gpucomp/libgpucomp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a gpucomp/libgpucomp.a ft/libft.a -lm -o $@
screenshot.elf: Apps/screenshot/main.cpp img/pngsave.hpp clipboard.h clipproto.h notify.h ft/libft.a ft/wtkface.h wtk/libwtk.a \
		libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a ft/libft.a -lm -o $@

# Koton, the studio (Apps/koton: docs/daw/README.md): a newlib app as Writer (FreeType text through wtk's
# text face, floats everywhere). Its engine (the song model, the harmony, the generators, the compiler,
# the audio engine) and MeltySynth are separate objects; the UI is headers included by main.cpp.
KOTON_SRC = $(wildcard Apps/koton/engine/*.cpp) $(wildcard Apps/koton/synth/*.cpp) $(wildcard Apps/koton/plug/*.cpp)
KOTON_OBJ = $(patsubst Apps/koton/%.cpp,Apps/koton/obj/%.o,$(KOTON_SRC))
KOTON_HDR = $(wildcard Apps/koton/engine/*.h) $(wildcard Apps/koton/synth/*.h) $(wildcard Apps/koton/plug/*.h) kplug_proto.h json.hpp kapi.h
Apps/koton/obj/%.o: Apps/koton/%.cpp $(KOTON_HDR)
	@mkdir -p $(dir $@)
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -IApps/koton -c $< -o $@
koton.elf: Apps/koton/main.cpp $(wildcard Apps/koton/ui/*.h) $(KOTON_OBJ) ft/libft.a ft/wtkface.h wtk/libwtk.a \
	   libc/crt0libc.o libc/onyx_syscalls.o user.ld $(KOTON_HDR)
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -IApps/koton -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       $(KOTON_OBJ) wtk/libwtk.a ft/libft.a -lm -o $@

# PDF Viewer (Apps/pdf: docs/pdf/README.md): a newlib app as Writer, on MuPDF -- its fitz + pdf parts, jbig2dec, openjpeg,
# zlib, libjpeg and a FreeType with the PDF fonts' drivers, built by Apps/pdf/mupdf.mk into Apps/pdf/obj/libmupdf.a (that
# FreeType serves the app's own text too: no ft/libft.a). The app is AGPL-3.0, MuPDF's licence (docs/LICENSING.md).
MU_NL_CFLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -ffunction-sections -fdata-sections
Apps/pdf/obj/libmupdf.a: Apps/pdf/mupdf.mk $(wildcard Apps/pdf/mu/*)
	@$(MAKE) --no-print-directory -f Apps/pdf/mupdf.mk MU_ROOT=.. MU_ONYX=1 MU_CC=$(CC) MU_AR=$(AR) MU_OUT=Apps/pdf/obj MU_CFLAGS="$(MU_NL_CFLAGS)"
pdf.elf: Apps/pdf/main.cpp $(wildcard Apps/pdf/*.h) Apps/pdf/obj/libmupdf.a clipboard.h docguard.h ft/wtkface.h \
	 wtk/libwtk.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include -I../third_party/mupdf-1.28.5/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a Apps/pdf/obj/libmupdf.a -lm -o $@

# Media Player, the music and video library (Apps/media: docs/media/README.md): a newlib app (FreeType's text through
# wtk), its music decoders from third_party (minimp3, stb_vorbis, dr_flac, dr_wav: Apps/media/codecs.c, vorbis.c, built
# with the FPU), Koton's MeltySynth for the MIDI files (Koton's objects, shared), and for the videos Onyx's media library
# (user/av, compiled here into Apps/media/obj/av/) with its codecs -- libvpx, dav1d, libopus, the libraries Jet links
# (built by `make -C netsurf codecs`; their sources and flags: av/codecs.mk) -- and FFmpeg 7.1 (every decoder and
# demuxer: H.264, H.265, AAC, AVI, MPEG-TS...; GPL-2.0+: the Media Player is distributed under the GPL-2.0), its
# libraries built by third_party/ffmpeg-7.1.2/onyx/build.sh pi (committed).
MEDIA_OBJ = Apps/media/obj/codecs.o Apps/media/obj/vorbis.o
Apps/media/obj/%.o: Apps/media/%.c Apps/media/codecs.h
	@mkdir -p $(dir $@)
	$(CC) $(NL_CFLAGS) -w -I../third_party -c $< -o $@
TP := ../third_party
AV_ARCH := aarch64
include av/codecs.mk
MEDIA_AV_OBJ = $(patsubst av/%.c,Apps/media/obj/av/%.o,$(wildcard av/*.c))
FFMPEG = ../third_party/ffmpeg-7.1.2/onyx
MEDIA_AV_LIBS = $(VPX)/libvpx.a $(DAV1D)/libdav1d.a $(OPUS)/libopus.a \
	$(FFMPEG)/aarch64/libavformat.a $(FFMPEG)/aarch64/libavcodec.a $(FFMPEG)/aarch64/libswscale.a \
	$(FFMPEG)/aarch64/libswresample.a $(FFMPEG)/aarch64/libavutil.a
Apps/media/obj/av/%.o: av/%.c $(wildcard av/*.h) kapi.h
	@mkdir -p $(dir $@)
	$(CC) $(NL_CFLAGS) -std=gnu11 -Iav -I. $(AV_CODECS_CF) -DAV_WITH_FFMPEG -I$(FFMPEG)/include -c $< -o $@
MEDIA_SYNTH = $(filter Apps/koton/obj/synth/%,$(KOTON_OBJ))
media.elf: Apps/media/main.cpp $(wildcard Apps/media/*.h) $(MEDIA_OBJ) $(MEDIA_AV_OBJ) $(MEDIA_SYNTH) img/imgload.hpp img/pngsave.hpp fileassoc.h notify.h \
	   ft/libft.a ft/wtkface.h wtk/libwtk.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h $(wildcard av/*.h)
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -I../third_party -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       $(MEDIA_OBJ) $(MEDIA_AV_OBJ) $(MEDIA_SYNTH) wtk/libwtk.a ft/libft.a $(MEDIA_AV_LIBS) -lm -o $@

# Koton's plugins (user/kplug.h, the protocol user/kplug_proto.h; the host: Apps/koton/plug): each
# Apps/kp_<name>/main.cpp -> Apps/kp_<name>/kp_<name>.elf, a newlib app with the FPU. Built next to
# its source, NOT as user/*.elf (make stage makes
# an SD:/apps/<name>.app of each of those: the dock would list them): `make stage` copies them to
# SD:/koton/plugins/<name>/main with their plugin.json.
KPLUG_DIRS = $(wildcard Apps/kp_*)
KPLUG_ELFS = $(foreach d,$(KPLUG_DIRS),$(d)/$(notdir $(d)).elf)
KPLUG_HDR = kplug.h kplug_proto.h applet_proto.h json.hpp kapi.h Apps/koton/engine/kbase.h
define KPLUG_RULE
$(1)/$(notdir $(1)).elf: $(1)/main.cpp $$(KPLUG_HDR) wtk/libwtk.a libc/crt0libc.o libc/onyx_syscalls.o user.ld
	$$(CXX) $$(NL_CXXFLAGS) -fno-math-errno -Wall $$(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $$< wtk/libwtk.a -lm -o $$@
endef
$(foreach d,$(KPLUG_DIRS),$(eval $(call KPLUG_RULE,$(d))))
.PHONY: plugins
plugins: $(KPLUG_ELFS)

# The Game Boy / Color emulator core (gb/: integer only, no libc), for gbemu and the library.
gb/gb.o: gb/gb.cpp gb/gb.h
	$(CXX) $(CXXFLAGS) -O2 -I. -c $< -o $@
gb/libgb.a: gb/gb.o
	$(AR) rcs $@ $^

# The Game Boy Advance emulator core (gba/: integer only, no libc), for gbaemu and the library.
GBA_OBJS = gba/gba.o gba/gba_cpu.o gba/gba_bios.o gba/gba_ppu.o gba/gba_apu.o
gba/%.o: gba/%.cpp gba/gba.h
	$(CXX) $(CXXFLAGS) -O2 -I. -c $< -o $@
gba/libgba.a: $(GBA_OBJS)
	$(AR) rcs $@ $^

# The NES / Famicom emulator core (nes/: integer only, no libc), for nesemu and the library.
NES_OBJS = nes/nes.o nes/nes_ppu.o nes/nes_apu.o
nes/%.o: nes/%.cpp nes/nes.h nes/nes_optable.inc nes/nes_mix.inc
	$(CXX) $(CXXFLAGS) -O2 -I. -c $< -o $@
nes/libnes.a: $(NES_OBJS)
	$(AR) rcs $@ $^

# The Super Nintendo core (snes/: integer only, no libc), for snesemu and the library.
SNES_OBJS = snes/snes.o snes/snes_cpu.o snes/snes_ppu.o snes/snes_apu.o
snes/%.o: snes/%.cpp snes/snes.h snes/snes_gauss.inc
	$(CXX) $(CXXFLAGS) -O2 -I. -c $< -o $@
# The Nintendo 64 core (n64/: the FPU is used: FP/SIMD flags), for n64emu.
N64_OBJS = n64/n64_cpu.o n64/n64_bus.o n64/n64_gfx.o n64/n64_audio.o
n64/%.o: n64/%.cpp n64/n64.h
	$(CXX) $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72 -mno-outline-atomics -fno-math-errno -O3 -I. -c $< -o $@
n64/libn64.a: $(N64_OBJS)
	$(AR) rcs $@ $^
# The GameCube core (gc/: the FPU is used, as n64/), for gcemu.
GC_OBJS = gc/gc_cpu.o gc/gc_mem.o gc/gc_hw.o gc/gc_gx.o gc/gc_gxdraw.o gc/gc_dsp.o gc/gc_boot.o gc/gc_jit.o gc/gc_card.o gc/gc_zelda.o gc/gc_gxgpu.o
gc/%.o: gc/%.cpp gc/gc.h
	$(CXX) $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72 -mno-outline-atomics -fno-math-errno -O3 -I. -c $< -o $@
gc/libgc.a: $(GC_OBJS)
	$(AR) rcs $@ $^
# The V3D QPU builder (v3d/: shaders generated at run time, e.g. the GameCube's TEV): the C++
# builder + Mesa's QPU packer (tools/qpu/mesa, MIT) + the few libc functions it calls.
V3D_MESA = ../tools/qpu/mesa
V3D_OBJS = v3d/qpu.o v3d/shaders.o v3d/gxtev.o v3d/qpu_pack.o v3d/qpu_instr.o v3d/shim.o
v3d/qpu.o: v3d/qpu.cpp v3d/qpu.h
	$(CXX) $(CXXFLAGS) -O2 -I. -I$(V3D_MESA) -c $< -o $@
v3d/shaders.o: v3d/shaders.cpp v3d/shaders.h v3d/qpu.h
	$(CXX) $(CXXFLAGS_NEON) -O2 -I. -I$(V3D_MESA) -c $< -o $@
v3d/gxtev.o: v3d/gxtev.cpp v3d/gxtev.h v3d/qpu.h
	$(CXX) $(CXXFLAGS_NEON) -O2 -I. -I$(V3D_MESA) -c $< -o $@
v3d/qpu_pack.o: $(V3D_MESA)/broadcom/qpu/qpu_pack.c
	$(CC) $(CFLAGS) -std=gnu11 -DNDEBUG -w -I$(V3D_MESA) -c $< -o $@
v3d/qpu_instr.o: $(V3D_MESA)/broadcom/qpu/qpu_instr.c
	$(CC) $(CFLAGS) -std=gnu11 -DNDEBUG -w -I$(V3D_MESA) -c $< -o $@
v3d/shim.o: v3d/shim.c
	$(CC) $(CFLAGS) -c $< -o $@
v3d/libv3d.a: $(V3D_OBJS)
	$(AR) rcs $@ $^
# The GPU compositing service (gpucomp/: layers composited by the V3D, kapi v70, or by the CPU):
# pure C without libc, FP/SIMD on and -O3 (the CPU path's row loops become NEON code), no fused
# multiply-adds (-ffp-contract=off: the same pixels as the PC's build, tools/tests/run_gpucomp_test.sh).
# Linked by freestanding apps (gpcdemo) and newlib ones (NetSurf, stage 2) alike.
gpucomp/gpucomp.o: gpucomp/gpucomp.c gpucomp/gpucomp.h kapi.h ../kernel/include/kern/kapi_abi.h
	$(CC) $(filter-out -mgeneral-regs-only -O2,$(CFLAGS)) -mcpu=cortex-a72 -O3 -ffp-contract=off -fno-math-errno -I. -c $< -o $@
gpucomp/libgpucomp.a: gpucomp/gpucomp.o
	$(AR) rcs $@ $^

snes/libsnes.a: $(SNES_OBJS)
	$(AR) rcs $@ $^

# Onyx BASIC (basic/): the core (compiler + VM, doubles -> FP/SIMD, no libc) as a library
# (linked into the qbasic editor too), and the runtime /bin/basic (basic/runtime.cpp).
BAS_HDR = basic/bas.h basic/basint.h basic/basnum.h basic/bas3d.h basic/bas3dscene.h basic/basscreen.h
BAS_OBJ = basic/basnum.o basic/bascomp.o basic/basvm.o basic/basbax.o
CXXFLAGS_FP = $(CXXFLAGS_NEON) -fno-math-errno
# The graphing calculator computes in double: FP/SIMD for the whole app.
graphcalc.elf: CXXFLAGS := $(CXXFLAGS_FP)
# The teapot demo computes its geometry in float: FP/SIMD too.
teapot.elf: CXXFLAGS := $(CXXFLAGS_FP)
teapot.elf: Apps/teapot/teapot.h Apps/teapot/teapot_data.inc
gpudemo.elf: CXXFLAGS := $(CXXFLAGS_FP)
n64emu.elf: CXXFLAGS := $(CXXFLAGS_FP)
gcemu.elf: CXXFLAGS := $(CXXFLAGS_FP)
gcemu.elf: Apps/gcemu/gxv3d.h v3d/gxtev.h v3d/shaders.h
graphcalc.elf: Apps/graphcalc/expr.h
cardfile.elf: $(wildcard Apps/cardfile/*.h) clipboard.h docguard.h
# The Calendar: a newlib app as Writer (FreeType's text through wtk's face); its model (iCalendar), views
# and dialogs in Apps/calendar/*.h.
calendar.elf: Apps/calendar/main.cpp $(wildcard Apps/calendar/*.h) notify.h applib.h ft/libft.a ft/wtkface.h \
	      wtk/libwtk.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       wtk/libwtk.a ft/libft.a -lm -o $@
agenda.elf: notify.h
setup.elf: Apps/setup/system.h wallpaper.h img/imgload.hpp
# The FreeType apps (FT_APPS): newlib + ft/libft.a, as the Calendar. The Game Library also links the
# emulators it takes the covers and the save states' pictures from.
FT_EXTRA_gamelib = gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a
$(FT_APP_ELFS): %.elf: Apps/%/main.cpp applib.h bmp.hpp umm.h onyxpp.hpp ft/libft.a ft/wtkface.h wtk/libwtk.a 		       gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< 	       $(FT_EXTRA_$*) wtk/libwtk.a ft/libft.a -lm -o $@
# Ledger: its books, pages and printing in Apps/ledger/*.h (Cardfile's model and widgets: the merge's data;
# the reports' .xlsx: the PNG writer's zip / deflate).
ledger.elf: $(wildcard Apps/ledger/*.h) $(wildcard Apps/cardfile/*.h) clipboard.h docguard.h img/pngsave.hpp
basic/%.o: basic/%.cpp $(BAS_HDR)
	$(CXX) $(CXXFLAGS_FP) -I. -c $< -o $@
basic/libbasic.a: $(BAS_OBJ)
	$(AR) rcs $@ $^
bin/basic.elf: basic/runtime.cpp basic/libbasic.a wtk/libwtk.a crt0.S user.ld kapi.h $(BAS_HDR)
	$(CXX) $(CXXFLAGS_FP) -I. $(LDFLAGS) $(KAPI_ALIASES) crt0.S basic/runtime.cpp basic/libbasic.a wtk/libwtk.a -o $@

clean:
	rm -f *.elf *.o ft/*.o ft/*.a zlib/*.o zlib/*.a libc/*.o basic/*.o basic/*.a $(KPLUG_ELFS) gb/*.o gb/*.a gba/*.o gba/*.a nes/*.o nes/*.a snes/*.o snes/*.a n64/*.o n64/*.a gc/*.o gc/*.a v3d/*.o v3d/*.a gpucomp/*.o gpucomp/*.a bin/basic.elf
	rm -rf Apps/pdf/obj
	@$(MAKE) --no-print-directory -C bin clean
	@$(MAKE) --no-print-directory -C doom clean
