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
# app is C++ on the uikit toolkit now; the /bin console tools stay C in bin/.
APPS = tinycalc tinypad applist panel cppdemo \
       demoA demoB demoC demoD demoE demoF inidemo \
       tetris snake same terminal \
       2048 life pong sokoban \
       mandelbrot minesweeper \
       eyes voronoy irc spin wtkdemo shell notifyd shutdown plasma shelf ask agenda imageview widgets qbasic lisa invaders pipes solitaire freecell graphcalc iconedit rtfview gbemu gbaemu nesemu snesemu teapot gpudemo n64emu gcemu wifimenu dock lock cardfile

# The apps whose text is FreeType's (DejaVu Sans through uikit's face, ft/uikitface.h): newlib apps as
# the Calendar (the rule below). Every new app joins them (docs/03, "Fonts").
FT_APPS = control theme config wpaconf padconf dockconf soundconf displayconf keyconf preloadconf printconf gamelib setup menubar fileviewer ledger fmtracker taskman
FT_APP_ELFS = $(FT_APPS:=.elf)

APP_ELFS = $(APPS:=.elf)

all: libs $(APP_ELFS) $(FT_APP_ELFS) letters.elf sheet.elf slides.elf qbstudio.elf koton.elf courier.elf calendar.elf paint.elf archiver.elf clipd.elf clipboard.elf screenshot.elf media.elf pdf.elf mail.elf photos.elf pkgman.elf pkgd.elf printd.elf plugins bin bin/basic.elf doom

# ---- Shared libraries (docs/SHARED-LIBS-PLAN.md; kapi v83): position-independent code linked with
# lib.ld at 0 -- the kernel places it --, ONE exported symbol (lib.vers), the export table, which is
# the ELF entry. -z text and --no-undefined: no relocation in the code, nothing imported by name; what
# is left for the kernel is R_AARCH64_RELATIVE relocations in the data. Built into lib/<name>.so,
# staged as SD:/lib/<name>.so. librt.o is every library's runtime (the importer's allocator, the
# constructors).
LIB_CXXFLAGS = -ffreestanding -nostdlib -fPIC -fvisibility=hidden -mgeneral-regs-only -O2 -Wall -Wextra \
	       -fno-stack-protector -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
	       -I. -I../kernel/include
LIB_LD      = $(PREFIX)ld
LIB_LDFLAGS = -shared -Bsymbolic -z text -z max-page-size=0x10000 --no-undefined --hash-style=sysv \
	      --build-id=none -T lib.ld --version-script lib.vers -e onyx_lib_table
LIB_DEP     = lib.ld lib.vers lib.h kapi.h ../kernel/include/kern/kapi_abi.h
lib/librt.o: librt.cpp $(LIB_DEP)
	@mkdir -p lib
	$(CXX) $(LIB_CXXFLAGS) -c $< -o $@
# The loader's test library (bin/libtest runs it) and its "other build".
lib/demo.o: demo/demolib.cpp demo/demo.h $(LIB_DEP)
	@mkdir -p lib
	$(CXX) $(LIB_CXXFLAGS) -c $< -o $@
lib/demo2.o: demo/demolib.cpp demo/demo.h $(LIB_DEP)
	@mkdir -p lib
	$(CXX) $(LIB_CXXFLAGS) -DDEMO_BUILD=2 -c $< -o $@
lib/demo.so lib/demo2.so: lib/%.so: lib/%.o lib/librt.o
	$(LIB_LD) $(LIB_LDFLAGS) -o $@ $^
# A library's interface, both halves, from its objects and its append-only list <lib>.abi
# (tools/libgen/libgen.py): lib/<lib>_table.S (the library's export table), lib/<lib>_stubs.S and
# lib/<lib>_bind.cpp (the program's import stubs and the constructor that opens the library) --
# the last two make lib/<lib>.imp.a, which a program links where it linked the static library.
LIBGEN = python3 ../tools/libgen/libgen.py --nm $(PREFIX)nm
IMP_CXXFLAGS = $(CXXFLAGS) -I.
lib/%_table.o: lib/%_table.S
	$(CC) -c $< -o $@
lib/%_stubs.o: lib/%_stubs.S
	$(CC) -c $< -o $@
lib/%_bind.o: lib/%_bind.cpp lib.h kapi.h
	$(CXX) $(IMP_CXXFLAGS) -c $< -o $@
lib/ft.imp.a: lib/ft_stubs.o lib/ft_bind.o
	@rm -f $@
	$(AR) rcs $@ $^
.PRECIOUS: lib/%_table.S lib/%_stubs.S lib/%_bind.cpp lib/%_table.o lib/%_stubs.o lib/%_bind.o

# ---- ft.so: FreeType (the apps' TrueType-only build: ft/onyx_ftoption.h, ft/onyx_ftmodule.h) as a
# shared library. Its table: FreeType's public API (the functions its public headers declare: the list
# lib/ft.exports), in ft/ft.abi's order. ft/ftso.c: its memory through the importer's allocator; the
# string functions, qsort and setjmp from the toolchain's newlib, linked in.
FT_SRC = ../third_party/freetype-2.14.3
FT_SO_CFLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections \
	       -I. -I../kernel/include
FT_SO_OBJ = $(addprefix lib/ft/,ftsystem.o ftinit.o ftdebug.o ftbase.o ftbitmap.o ftsynth.o autofit.o truetype.o sfnt.o smooth.o)
lib/ft/%.o: %.c ft/onyx_ftoption.h ft/onyx_ftmodule.h
	@mkdir -p lib/ft
	$(CC) $(FT_SO_CFLAGS) -w -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' \
	      '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' -Ift -I$(FT_SRC)/include -c $< -o $@
lib/ft/ftso.o: ft/ftso.c lib.h kapi.h
	@mkdir -p lib/ft
	$(CC) $(FT_SO_CFLAGS) -Wall -c $< -o $@
lib/ft.exports: $(wildcard $(FT_SRC)/include/freetype/*.h)
	@mkdir -p lib
	@grep -hoE "^  FT_[A-Za-z0-9_]+\(" $^ | sed 's/^  //; s/($$//' | sort -u > $@
lib/ft_table.S: $(FT_SO_OBJ) lib/ft/ftso.o lib/ft.exports ../tools/libgen/libgen.py
	$(LIBGEN) --name ft --abi ft/ft.abi --init ft_lib_init --export-file lib/ft.exports --allow-data . \
		--table lib/ft_table.S --stubs lib/ft_stubs.S --bind lib/ft_bind.cpp $(FT_SO_OBJ)
lib/ft_stubs.S lib/ft_bind.cpp: lib/ft_table.S ;
LIBC_A   := $(shell $(CC) -mcpu=cortex-a72 -print-file-name=libc.a)
LIBGCC_A := $(shell $(CC) -mcpu=cortex-a72 -print-libgcc-file-name)
lib/ft.so: $(FT_SO_OBJ) lib/ft/ftso.o lib/ft_table.o lib/librt.o
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBC_A) $(LIBGCC_A)

UIKIT_HDR = $(wildcard uikit/*.h) kapi.h onyxpp.hpp applib.h applet_proto.h bmp.hpp clipboard.h gamepad.h notify.h
# ---- uikit.so: the toolkit as a shared library. Its table: every global function of uikit's objects
# (uikit/uikit.abi, append-only), the programs' copies of its vtables (--vtables); its global variables
# are the program's (uikit/globals.cpp, compiled into the import library too: --data). globals.o is
# linked first: its initialisers bind the library's references before any other constructor.
UIKIT_SO_SRC = $(sort $(filter-out uikit/globals.cpp,$(wildcard uikit/*.cpp)) uikit/layout_lock.cpp)
UIKIT_SO_OBJ = lib/uikit/globals.o $(patsubst uikit/%.cpp,lib/uikit/%.o,$(UIKIT_SO_SRC))
LIB_CXXFLAGS_NEON = $(filter-out -mgeneral-regs-only,$(LIB_CXXFLAGS)) -mcpu=cortex-a72
lib/uikit/%.o: uikit/%.cpp $(UIKIT_HDR) uikit/globals.inc $(LIB_DEP)
	@mkdir -p lib/uikit
	$(CXX) $(LIB_CXXFLAGS) -DONYX_LIB_BUILD -c $< -o $@
lib/uikit/canvas.o: uikit/canvas.cpp $(UIKIT_HDR) $(LIB_DEP)
	@mkdir -p lib/uikit
	$(CXX) $(LIB_CXXFLAGS_NEON) -DONYX_LIB_BUILD -c $< -o $@
lib/uikit/imgload.o: uikit/imgload.cpp img/imgload.hpp img/stb_image.h img/simplewebp.h $(LIB_DEP)
	@mkdir -p lib/uikit
	$(CXX) $(LIB_CXXFLAGS_NEON) -DONYX_LIB_BUILD -Wno-unused-parameter -Wno-sign-compare -c $< -o $@
# The layout lock (uikit/abi.h): the classes' sizes and the base classes' offsets, as static_asserts
# compiled with the library. Made once from the headers; `make uikit-layout-update` only for a class added.
UIKIT_LAYOUT = python3 ../tools/libgen/layout.py --cxx $(CXX) --flags "$(LIB_CXXFLAGS) -DONYX_LIB_BUILD" --umbrella uikit/uikit.h 	--ns uikit --headers 'uikit/*.h' --out uikit/layout_lock.cpp --offsets 	Widget:canvas,left,tag,anchor,transparent,tip,parent,prevHandled,ext,reserved_ Canvas:px,w,stride,owns,capH,ext Root:bg
uikit/layout_lock.cpp:
	$(UIKIT_LAYOUT)
.PHONY: uikit-layout-update
uikit-layout-update:
	$(UIKIT_LAYOUT) --update
lib/uikit_table.S: $(UIKIT_SO_OBJ) uikit/uikit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name uikit --abi uikit/uikit.abi --init uikit_lib_init --vtables --data onyx_uikit_data \
		--allow-data '^_ZN5uikit' --table lib/uikit_table.S --stubs lib/uikit_stubs.S --bind lib/uikit_bind.cpp $(UIKIT_SO_OBJ)
uikit/uikit.abi:
	@touch $@
lib/uikit_stubs.S lib/uikit_bind.cpp: lib/uikit_table.S ;
lib/uikit.so: $(UIKIT_SO_OBJ) lib/uikit_table.o lib/librt.o
	$(LIB_LD) $(LIB_LDFLAGS) -o $@ $^ $(LIBGCC_A)
# The program side: the stubs, the bind constructor, and the variables (uikit/globals.cpp).
lib/uikit_globals.o: uikit/globals.cpp uikit/globals.inc $(UIKIT_HDR)
	@mkdir -p lib
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@
lib/uikit.imp.a: lib/uikit_stubs.o lib/uikit_bind.o lib/uikit_globals.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- printerkit.so: printing (printerkit/printerkit.h; docs/03 "Printing") -- a job's pages recorded, the Print dialog.
# Its table: the print_ functions (printerkit/printerkit.abi). The first library that uses others: FreeType (the
# text's glyphs) and uikit (the dialog), through their import stubs linked in -- printerkit/printerkit.cpp opens
# ft.so / uikit.so when first needed and sets the stubs' tables. uikit's variables are the program's, as for
# uikit.so itself: uikit's globals.o (the library build: references bound through the importer's table) and
# --data onyx_uikit_data in the bind object. Floats in its interface: FP/SIMD flags.
PRINT_HDR = $(wildcard printerkit/*.h) pdf/pdfwrite.h img/pngsave.hpp
PRINT_SO_OBJ = lib/printerkit/printerkit.o lib/printerkit/dialog.o
lib/printerkit/%.o: printerkit/%.cpp $(PRINT_HDR) $(UIKIT_HDR) $(LIB_DEP)
	@mkdir -p lib/printerkit
	$(CXX) $(LIB_CXXFLAGS_NEON) -DONYX_LIB_BUILD -Ift -I$(FT_SRC)/include -c $< -o $@
lib/printerkit_table.S: $(PRINT_SO_OBJ) printerkit/printerkit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name printerkit --abi printerkit/printerkit.abi --init print_lib_init --export '^print_' --exclude '^print_lib_init$$' 		--allow-data '^onyx_' --data onyx_uikit_data --table lib/printerkit_table.S --stubs lib/printerkit_stubs.S --bind lib/printerkit_bind.cpp $(PRINT_SO_OBJ)
printerkit/printerkit.abi:
	@touch $@
lib/printerkit_stubs.S lib/printerkit_bind.cpp: lib/printerkit_table.S ;
lib/printerkit.so: $(PRINT_SO_OBJ) lib/uikit/globals.o lib/uikit_stubs.o lib/ft_stubs.o lib/printerkit_table.o lib/librt.o
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBGCC_A)
lib/printerkit.imp.a: lib/printerkit_stubs.o lib/printerkit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- audiokit.so: AudioKit, the shared sound library (audiokit/audiokit.h; docs/03 "AudioKit"). In it:
# MeltySynth, Koton's SoundFont synthesizer (Apps/koton/synth: its C++ interface exported as it is), the
# audio decoders (Apps/media/codecs.c, vorbis.c: minimp3, dr_flac, dr_wav, stb_vorbis -- their C
# interfaces as they are), and AudioKit's own functions (audiokit/akcore.cpp: the files, the background
# player, the output, mixing, notes, WAV). Built with the FPU; newlib's libm and libc linked in. Exported:
# ms::Synthesizer and ms::soundfont_* (not the synthesizer's inner classes), the decoders' functions, ak_*.
AK_FLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections \
	   -fno-math-errno -DONYX_LIB_BUILD -I. -I../kernel/include
AK_CXXFLAGS = $(AK_FLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
AK_SYNTH_SRC = $(wildcard Apps/koton/synth/*.cpp)
AK_OBJ = $(patsubst Apps/koton/synth/%.cpp,lib/ak/synth_%.o,$(AK_SYNTH_SRC)) lib/ak/codecs.o lib/ak/vorbis.o lib/ak/akcore.o \
	 lib/ak/akmix.o lib/ak/aksf.o lib/ak/akwav.o lib/ak/akfm.o
lib/ak/synth_%.o: Apps/koton/synth/%.cpp $(wildcard Apps/koton/synth/*.h)
	@mkdir -p lib/ak
	$(CXX) $(AK_CXXFLAGS) -IApps/koton -c $< -o $@
lib/ak/codecs.o lib/ak/vorbis.o: lib/ak/%.o: Apps/media/%.c Apps/media/codecs.h
	@mkdir -p lib/ak
	$(CC) $(AK_FLAGS) -w -I../third_party -IApps/media -c $< -o $@
lib/ak/akcore.o: audiokit/akcore.cpp audiokit/audiokit.h audiokit/fmsynth.h audiokit/fm_tables.h Apps/fmtracker/fms.h Apps/media/tags.h Apps/media/decode.h Apps/media/midi.h Apps/media/codecs.h \
		 Apps/koton/synth/meltysynth.h kapi.h $(LIB_DEP)
	@mkdir -p lib/ak
	$(CXX) $(AK_CXXFLAGS) -Wall -I../third_party -IApps/media -IApps/koton -c $< -o $@
lib/ak/akmix.o lib/ak/aksf.o lib/ak/akwav.o: lib/ak/%.o: audiokit/%.cpp audiokit/audiokit.h Apps/koton/synth/meltysynth.h kapi.h $(LIB_DEP)
	@mkdir -p lib/ak
	$(CXX) $(AK_CXXFLAGS) -Wall -IApps/koton -c $< -o $@
# (the FM synthesizer: audiokit/fmsynth.h)
lib/ak/akfm.o: audiokit/akfm.cpp audiokit/audiokit.h audiokit/fmsynth.h audiokit/fm_tables.h kapi.h $(LIB_DEP)
	@mkdir -p lib/ak
	$(CXX) $(AK_CXXFLAGS) -w -c $< -o $@
lib/ak/akso.o: audiokit/akso.c lib.h kapi.h
	@mkdir -p lib/ak
	$(CC) $(AK_FLAGS) -Wall -c $< -o $@
audiokit/audiokit.abi:
	@touch $@
lib/audiokit_table.S: $(AK_OBJ) lib/ak/akso.o audiokit/audiokit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name audiokit --abi audiokit/audiokit.abi --init ak_lib_init --allow-data . \
		--export '^(_ZNK?2ms(11Synthesizer|[0-9]+soundfont_)|mp3dec_|drflac_|drwav_|stb_vorbis_|ak_)' \
		--table lib/audiokit_table.S --stubs lib/audiokit_stubs.S --bind lib/audiokit_bind.cpp $(AK_OBJ)
lib/audiokit_stubs.S lib/audiokit_bind.cpp: lib/audiokit_table.S ;
LIBM_A := $(shell $(CC) -mcpu=cortex-a72 -print-file-name=libm.a)
lib/audiokit.so: $(AK_OBJ) lib/ak/akso.o lib/audiokit_table.o lib/librt.o
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBM_A) $(LIBC_A) $(LIBGCC_A)
lib/audiokit.imp.a: lib/audiokit_stubs.o lib/audiokit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- filekit.so: FileKit, what programs do with files (filekit/filekit.h; docs/03 "FileKit"): zlib
# (third_party, compiled for the library), the Archiver's ZIP engine (Apps/archiver/arc.h, zip.h, ops.h:
# compiled here once), ZIP archives in memory, whole files, trees copied / moved / removed, paths.
# Integer only in its interface; newlib's libc linked in. Exported: fk_*.
ZLIB_SRC = ../third_party/zlib-1.3.1
FK_ZOBJ = $(addprefix lib/fk/z_,adler32.o crc32.o deflate.o inflate.o inffast.o inftrees.o trees.o zutil.o)
FK_OBJ = lib/fk/fkcore.o $(FK_ZOBJ)
lib/fk/z_%.o: $(ZLIB_SRC)/%.c
	@mkdir -p lib/fk
	$(CC) $(AK_FLAGS) -w -I$(ZLIB_SRC) -c $< -o $@
lib/fk/fkcore.o: filekit/fkcore.cpp filekit/filekit.h $(wildcard Apps/archiver/arc.h Apps/archiver/zip.h Apps/archiver/ops.h) kapi.h $(LIB_DEP)
	@mkdir -p lib/fk
	$(CXX) $(AK_CXXFLAGS) -Wall -Ifilekit -I$(ZLIB_SRC) -c $< -o $@
lib/fk/fkso.o: filekit/fkso.c lib.h kapi.h
	@mkdir -p lib/fk
	$(CC) $(AK_FLAGS) -Wall -c $< -o $@
filekit/filekit.abi:
	@touch $@
lib/filekit_table.S: $(FK_OBJ) lib/fk/fkso.o filekit/filekit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name filekit --abi filekit/filekit.abi --init fk_lib_init --allow-data . --export '^fk_' \
		--table lib/filekit_table.S --stubs lib/filekit_stubs.S --bind lib/filekit_bind.cpp $(FK_OBJ)
lib/filekit_stubs.S lib/filekit_bind.cpp: lib/filekit_table.S ;
lib/filekit.so: $(FK_OBJ) lib/fk/fkso.o lib/filekit_table.o lib/librt.o
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBC_A) $(LIBGCC_A)
lib/filekit.imp.a: lib/filekit_stubs.o lib/filekit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- imagekit.so: ImageKit, pictures (imagekit/imagekit.h; docs/03 "ImageKit"): the decoders
# (img/imgload.hpp: stb_image, simplewebp, PCX), the encoders (img/pngsave.hpp; PNG's deflate is zlib's,
# through FileKit: its import stubs linked in, the library opened when a PNG is first written), what a
# file says and the adjustments (Photos' exif.h, imgops.h), one resize right in alpha. Built with the
# FPU, an integer interface; newlib's libm and libc linked in. Exported: ik_*.
IK_OBJ = lib/ik/ikcore.o
lib/ik/ikcore.o: imagekit/ikcore.cpp imagekit/imagekit.h filekit/filekit.h img/imgload.hpp img/pngsave.hpp img/stb_image.h img/simplewebp.h \
		 Apps/photos/imgops.h Apps/photos/exif.h kapi.h $(LIB_DEP)
	@mkdir -p lib/ik
	$(CXX) $(AK_CXXFLAGS) -w -Iimagekit -c $< -o $@
lib/ik/ikso.o: imagekit/ikso.c lib.h kapi.h
	@mkdir -p lib/ik
	$(CC) $(AK_FLAGS) -Wall -c $< -o $@
imagekit/imagekit.abi:
	@touch $@
lib/imagekit_table.S: $(IK_OBJ) lib/ik/ikso.o imagekit/imagekit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name imagekit --abi imagekit/imagekit.abi --init ik_lib_init --allow-data . --export '^ik_' --exclude '^ik_lib_init$$' \
		--table lib/imagekit_table.S --stubs lib/imagekit_stubs.S --bind lib/imagekit_bind.cpp $(IK_OBJ)
lib/imagekit_stubs.S lib/imagekit_bind.cpp: lib/imagekit_table.S ;
lib/imagekit.so: $(IK_OBJ) lib/ik/ikso.o lib/filekit_stubs.o lib/imagekit_table.o lib/librt.o
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBM_A) $(LIBC_A) $(LIBGCC_A)
lib/imagekit.imp.a: lib/imagekit_stubs.o lib/imagekit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^

SHLIBS = lib/demo.so lib/demo2.so lib/ft.so lib/uikit.so lib/printerkit.so lib/audiokit.so lib/filekit.so lib/imagekit.so
.PHONY: libs
libs: $(SHLIBS) lib/ft.imp.a lib/uikit.imp.a lib/printerkit.imp.a lib/audiokit.imp.a lib/filekit.imp.a lib/imagekit.imp.a

# Doom (doom/: doomgeneric from third_party + the Onyx layer, against newlib) -> doom.elf.
.PHONY: doom
doom: lib/uikit.imp.a
	@$(MAKE) --no-print-directory -C doom

# /bin console tools live in bin/ (their own Makefile).
.PHONY: bin
bin: v3d/libv3d.a gpucomp/libgpucomp.a libs
	@$(MAKE) --no-print-directory -C bin

# Apps/<name>/main.cpp -> <name>.elf. -I. so a nested source finds the user-side headers.
# uikit toolkit: compiled once into a static library, linked into every app.
# (+ the user-side headers uikit includes: an inline of kapi.h -- kapi_lock, kapi__core -- is compiled
# into the library, which must follow it)
UIKIT_HDR = $(wildcard uikit/*.h) kapi.h onyxpp.hpp applib.h applet_proto.h bmp.hpp clipboard.h gamepad.h notify.h
UIKIT_OBJ = $(patsubst %.cpp,%.o,$(wildcard uikit/*.cpp))
uikit/%.o: uikit/%.cpp $(UIKIT_HDR)
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@
# canvas.cpp is the hot pixel path (clear/fillRect/putOther): build it with NEON/FP
# (drop -mgeneral-regs-only, target the A72). The kernel saves full FP/SIMD on every
# trap, so SIMD is safe even though the rest of uikit and every app stay integer-only --
# the NEON is confined to this TU and Canvas's public signatures use no FP regs, so the
# calling ABI is unchanged. This explicit rule overrides the uikit/%.o pattern above.
CXXFLAGS_NEON = $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72
uikit/canvas.o: uikit/canvas.cpp $(UIKIT_HDR)
	$(CXX) $(CXXFLAGS_NEON) -I. -c $< -o $@
# The image codecs (stb_image, simplewebp, PCX) use floating point: FP/SIMD too.
uikit/imgload.o: uikit/imgload.cpp img/imgload.hpp img/stb_image.h img/simplewebp.h
	$(CXX) $(CXXFLAGS_NEON) -Wno-unused-parameter -Wno-sign-compare -I. -c $< -o $@
uikit/libuikit.a: $(UIKIT_OBJ)
	$(AR) rcs $@ $^
# (uikit/libuikit.a, the static build: no app of this Makefile links it any more -- they link the shared
# library's import library, lib/uikit.imp.a --; Jet's hosted build compiles that import side for its own toolchain, tools/webkit/build-web.sh; the PC builds make their own static toolkit.)

$(APP_ELFS): %.elf: Apps/%/main.cpp crt0.S user.ld kapi.h applib.h launch.h httpc.h bmp.hpp umm.h onyxpp.hpp game.h cards.h rtf.h gamepad.h emucore.h lib/uikit.imp.a lib/audiokit.imp.a audiokit/audiokit.h lib/imagekit.imp.a imagekit/imagekit.h basic/libbasic.a gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a n64/libn64.a gc/libgc.a v3d/libv3d.a
	$(CXX) $(CXXFLAGS) -I. -I$(V3D_MESA) $(LDFLAGS) $(KAPI_ALIASES) crt0.S $< lib/uikit.imp.a lib/audiokit.imp.a lib/imagekit.imp.a basic/libbasic.a gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a n64/libn64.a gc/libgc.a v3d/libv3d.a -o $@

# ---- Letters: a newlib app (FreeType wants a libc: ../libc's crt0libc.S + the kapi syscall layer,
# as Doom's), with the apps' TrueType-only FreeType (ft/: its options and modules -- no bytecode
# interpreter, no compressed / web / bitmap fonts --, the upstream sources of third_party).
# FreeType is the shared library SD:/lib/ft.so (above): the apps link its import library
# lib/ft.imp.a. (ft/libft.a, the static build, is still made by `make ft/libft.a`: nothing links it.)
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
# Letters: its model, layout and files in Apps/letters/*.h (the mail merge reads Cardfile's forms: its model.h;
# .docx / .odt: the PNG writer's zip / deflate).
letters.elf: Apps/letters/main.cpp $(wildcard Apps/letters/*.h) Apps/cardfile/model.h img/pngsave.hpp ft/fonts.h lib/printerkit.imp.a $(PRINT_HDR) lib/ft.imp.a \
	    lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h docguard.h clipboard.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/uikit.imp.a lib/ft.imp.a -lm -o $@
# The spreadsheet: a newlib app as Letters (FreeType, stdio, doubles) -- its engine and files in
# Apps/sheet/*.h, Letters' icons, the PNG writer's zip / deflate for the .xlsx.
sheet.elf: Apps/sheet/main.cpp $(wildcard Apps/sheet/*.h) Apps/letters/icons.h img/pngsave.hpp ft/fonts.h lib/printerkit.imp.a $(PRINT_HDR) lib/ft.imp.a \
	   lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h docguard.h clipboard.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/uikit.imp.a lib/ft.imp.a -lm -o $@

# Slides, the presentation program (Apps/slides: docs/slides/README.md): a newlib app as Letters (FreeType), its
# slides' layers composited by the GPU (gpucomp), Letters' icons and Sheet's controls (Apps/sheet/ui_base.h), the
# .odp's zip (img/pngsave.hpp), the PDF writer.
slides.elf: Apps/slides/main.cpp $(wildcard Apps/slides/*.h) Apps/sheet/ui_base.h Apps/sheet/core.h Apps/sheet/xml.h Apps/letters/icons.h \
	    img/pngsave.hpp pdf/pdfwrite.h ft/fonts.h lib/printerkit.imp.a $(PRINT_HDR) lib/ft.imp.a lib/uikit.imp.a gpucomp/libgpucomp.a libc/crt0libc.o libc/onyx_syscalls.o \
	    user.ld kapi.h docguard.h clipboard.h
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/uikit.imp.a gpucomp/libgpucomp.a lib/ft.imp.a -lm -o $@

# QBStudio, the IDE for desktop apps in BASIC (Apps/qbstudio: docs/qbstudio/README.md): a newlib app as Letters
# (FreeType's text through uikit, DejaVu Sans Mono for the code), with Onyx BASIC's compiler and VM built in (the
# problems as you type, Make App's .bax).
qbstudio.elf: Apps/qbstudio/main.cpp $(wildcard Apps/qbstudio/*.h) basic/bascomp.cpp basic/basvm.cpp basic/basnum.cpp basic/basbax.cpp \
	      basic/bas.h basic/basint.h basic/basjit.h basic/basnum.h lib/ft.imp.a ft/uikitface.h lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h clipboard.h notify.h
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       basic/bascomp.cpp basic/basvm.cpp basic/basnum.cpp basic/basbax.cpp lib/uikit.imp.a lib/ft.imp.a -lm -o $@

# Courier, the HTTP client (Apps/courier: a Postman for Onyx): a newlib app as Letters (FreeType's text through
# uikit), with mbedTLS for https:// (../tls/onyx_tls.hpp over the kapi sockets; the libraries as user/bin's
# TLS tools link them).
COURIER_MBEDTLS ?= ../third_party/mbedtls-3.6.3
courier.elf: Apps/courier/main.cpp $(wildcard Apps/courier/*.h) http.hpp tls/onyx_tls.hpp json.hpp clipboard.h docguard.h 	     img/imgload.hpp lib/ft.imp.a ft/uikitface.h lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include -I$(COURIER_MBEDTLS)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< 	       lib/uikit.imp.a lib/ft.imp.a -L$(COURIER_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Mail, the mail client (Apps/mail: docs/mail/README.md): a newlib app as Letters (FreeType's text through uikit), its
# protocols and its HTML renderer in mail/ (MIT), mbedTLS for TLS (as Courier), the image codecs (uikit's imgload).
MAIL_MBEDTLS ?= ../third_party/mbedtls-3.6.3
mail.elf: Apps/mail/main.cpp $(wildcard Apps/mail/*.h) $(wildcard mail/*.h) Apps/cardfile/model.h tls/onyx_tls.hpp clipboard.h notify.h fileassoc.h applet_proto.h Apps/jet/webview_proto.h \
	  img/imgload.hpp lib/ft.imp.a ft/fonts.h ft/uikitface.h lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Wno-format-truncation -Ift -I$(FT_SRC)/include -I$(MAIL_MBEDTLS)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/uikit.imp.a lib/ft.imp.a -L$(MAIL_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Photos, the photo library (Apps/photos: docs/photos/README.md): a newlib app as Letters (FreeType's text through
# uikit), the image codecs (uikit's imgload), the JPEG / PNG writers (img/pngsave.hpp), the PDF writer (pdf/pdfwrite.h).
photos.elf: Apps/photos/main.cpp $(wildcard Apps/photos/*.h) img/imgload.hpp img/pngsave.hpp pdf/pdfwrite.h clipboard.h notify.h trash.h wallpaper.h \
	  lib/ft.imp.a ft/fonts.h ft/uikitface.h lib/printerkit.imp.a printerkit/printerkit.h lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Wno-format-truncation -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/uikit.imp.a lib/ft.imp.a -lm -o $@

# Archiver, the archive manager (Apps/archiver: docs/archiver/README.md): a newlib app as Letters (FreeType's text
# through uikit), its engine and widgets headers included by main.cpp; zlib (Deflate) built from third_party into
# zlib/libz.a.
ZLIB_SRC = ../third_party/zlib-1.3.1
ZLIB_OBJ = $(addprefix zlib/,adler32.o crc32.o deflate.o inflate.o inffast.o inftrees.o trees.o zutil.o)
zlib/%.o: $(ZLIB_SRC)/%.c
	@mkdir -p zlib
	$(CC) $(NL_CFLAGS) -w -c $< -o $@
zlib/libz.a: $(ZLIB_OBJ)
	$(AR) rcs $@ $^
archiver.elf: Apps/archiver/main.cpp $(wildcard Apps/archiver/*.h) fileassoc.h lib/ft.imp.a ft/uikitface.h lib/uikit.imp.a zlib/libz.a \
	      libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -IApps/archiver -Ift -I$(FT_SRC)/include -I$(ZLIB_SRC) $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/uikit.imp.a lib/ft.imp.a zlib/libz.a -lm -o $@

# The shared clipboard (docs/clipboard/README.md): clipd, its service (no window, newlib), and clipboard, its widget
# (the dock's button; FreeType's text). The apps reach clipd through clipboard.h (clipproto.h).
clipd.elf: Apps/clipd/main.cpp Apps/clipd/ring.h clipproto.h notify.h libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -IApps/clipd $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< -o $@
# The print service (docs/03 "Printing"): printd, no window, newlib -- the queue, the printers, a job's pages
# rendered (printerkit/raster.h: FreeType; the images' codecs: uikit.so's) and sent to a network printer (printerkit/ipp.h).
printd.elf: Apps/printd/main.cpp $(PRINT_HDR) notify.h lib/ft.imp.a lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Wno-format-truncation -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< 	       lib/uikit.imp.a lib/ft.imp.a -lm -o $@
clipboard.elf: Apps/clipboard/main.cpp clipboard.h clipproto.h lib/ft.imp.a ft/uikitface.h lib/uikit.imp.a \
	       libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/uikit.imp.a lib/ft.imp.a -lm -o $@

# The packages (docs/pkg/README.md): pkgman, the Package Manager (the Control Panel's applet), and pkgd, the
# update daemon (no window) -- on pkg/pkglib.h as bin/pkg: HTTPS (mbedTLS), the Archiver's ZIP engine, zlib.
PKG_MBEDTLS ?= ../third_party/mbedtls-3.6.3
PKG_DEPS = pkg/pkglib.h http.hpp tls/onyx_tls.hpp $(wildcard Apps/archiver/*.h) zlib/libz.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
pkgman.elf: Apps/pkgman/main.cpp $(PKG_DEPS) bmp.hpp lib/ft.imp.a ft/uikitface.h lib/uikit.imp.a
	$(CXX) $(NL_CXXFLAGS) -DPKG_NET -DONYX_HTTP_TLS -Wno-format-truncation -Ift -I$(FT_SRC)/include -I$(ZLIB_SRC) -I$(PKG_MBEDTLS)/include \
	       $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< lib/uikit.imp.a lib/ft.imp.a zlib/libz.a \
	       -L$(PKG_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@
pkgd.elf: Apps/pkgd/main.cpp $(PKG_DEPS) notify.h
	$(CXX) $(NL_CXXFLAGS) -DPKG_NET -DONYX_HTTP_TLS -Wno-format-truncation -I$(ZLIB_SRC) -I$(PKG_MBEDTLS)/include \
	       $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< zlib/libz.a \
	       -L$(PKG_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Screenshot, the screen capture tool (Apps/screenshot: docs/screenshot/README.md): a newlib app (FreeType's text
# through uikit), the PNG / JPEG / BMP writers of img/pngsave.hpp, the clipboard (clipd) and notifyd.
# Paint: a newlib app (FreeType's text, its Text tool), its canvas composited by the GPU (gpucomp: the
# layers as textures, their blend modes); its pieces in Apps/paint/*.h.
paint.elf: Apps/paint/main.cpp $(wildcard Apps/paint/*.h) clipboard.h docguard.h img/imgload.hpp img/pngsave.hpp lib/ft.imp.a \
	   ft/uikitface.h lib/printerkit.imp.a printerkit/printerkit.h lib/uikit.imp.a gpucomp/libgpucomp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/uikit.imp.a gpucomp/libgpucomp.a lib/ft.imp.a -lm -o $@
screenshot.elf: Apps/screenshot/main.cpp img/pngsave.hpp clipboard.h clipproto.h notify.h lib/ft.imp.a ft/uikitface.h lib/uikit.imp.a \
		libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/uikit.imp.a lib/ft.imp.a -lm -o $@

# Koton, the studio (Apps/koton: docs/daw/README.md): a newlib app as Letters (FreeType text through uikit's
# text face, floats everywhere). Its engine (the song model, the harmony, the generators, the compiler,
# the audio engine) and MeltySynth are separate objects; the UI is headers included by main.cpp.
# (its synthesizer, Apps/koton/synth, is in the shared library AudioKit: lib/audiokit.imp.a)
KOTON_SRC = $(wildcard Apps/koton/engine/*.cpp) $(wildcard Apps/koton/plug/*.cpp)
KOTON_OBJ = $(patsubst Apps/koton/%.cpp,Apps/koton/obj/%.o,$(KOTON_SRC))
KOTON_HDR = $(wildcard Apps/koton/engine/*.h) $(wildcard Apps/koton/synth/*.h) $(wildcard Apps/koton/plug/*.h) kplug_proto.h json.hpp kapi.h
Apps/koton/obj/%.o: Apps/koton/%.cpp $(KOTON_HDR)
	@mkdir -p $(dir $@)
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -IApps/koton -c $< -o $@
koton.elf: Apps/koton/main.cpp $(wildcard Apps/koton/ui/*.h) $(KOTON_OBJ) lib/ft.imp.a ft/uikitface.h lib/uikit.imp.a lib/audiokit.imp.a \
	   libc/crt0libc.o libc/onyx_syscalls.o user.ld $(KOTON_HDR)
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -IApps/koton -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       $(KOTON_OBJ) lib/audiokit.imp.a lib/uikit.imp.a lib/ft.imp.a -lm -o $@

# PDF Viewer (Apps/pdf: docs/pdf/README.md): a newlib app as Letters, on MuPDF -- its fitz + pdf parts, jbig2dec, openjpeg,
# zlib, libjpeg and a FreeType with the PDF fonts' drivers, built by Apps/pdf/mupdf.mk into Apps/pdf/obj/libmupdf.a (that
# FreeType serves the app's own text too: no lib/ft.imp.a). The app is AGPL-3.0, MuPDF's licence (docs/LICENSING.md).
MU_NL_CFLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -ffunction-sections -fdata-sections
Apps/pdf/obj/libmupdf.a: Apps/pdf/mupdf.mk $(wildcard Apps/pdf/mu/*)
	@$(MAKE) --no-print-directory -f Apps/pdf/mupdf.mk MU_ROOT=.. MU_ONYX=1 MU_CC=$(CC) MU_AR=$(AR) MU_OUT=Apps/pdf/obj MU_CFLAGS="$(MU_NL_CFLAGS)"
pdf.elf: Apps/pdf/main.cpp $(wildcard Apps/pdf/*.h) Apps/pdf/obj/libmupdf.a clipboard.h docguard.h ft/uikitface.h \
	 lib/printerkit.imp.a printerkit/printerkit.h lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include -I../third_party/mupdf-1.28.5/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/uikit.imp.a Apps/pdf/obj/libmupdf.a -lm -o $@

# Media Player, the music and video library (Apps/media: docs/media/README.md): a newlib app (FreeType's text through
# uikit), its music decoders from third_party (minimp3, stb_vorbis, dr_flac, dr_wav: Apps/media/codecs.c, vorbis.c, built
# with the FPU), Koton's MeltySynth for the MIDI files (Koton's objects, shared), and for the videos Onyx's media library
# (user/av, compiled here into Apps/media/obj/av/) with its codecs -- libvpx, dav1d, libopus
# (built by `make -C av`; their sources and flags: av/codecs.mk) -- and FFmpeg 7.1 (every decoder and
# demuxer: H.264, H.265, AAC, AVI, MPEG-TS...; GPL-2.0+: the Media Player is distributed under the GPL-2.0), its
# libraries built by third_party/ffmpeg-7.1.2/onyx/build.sh pi (committed).
# (Its music decoders -- codecs.c, vorbis.c -- and the MIDI synthesizer are in the shared library AudioKit:
# lib/audiokit.imp.a; decode.h and midi.h call them through it.)
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
media.elf: Apps/media/main.cpp $(wildcard Apps/media/*.h) $(MEDIA_AV_OBJ) lib/audiokit.imp.a img/imgload.hpp img/pngsave.hpp fileassoc.h notify.h \
	   lib/ft.imp.a ft/uikitface.h lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h $(wildcard av/*.h)
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -I../third_party -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       $(MEDIA_AV_OBJ) lib/audiokit.imp.a lib/uikit.imp.a lib/ft.imp.a $(MEDIA_AV_LIBS) -lm -o $@

# Koton's plugins (user/kplug.h, the protocol user/kplug_proto.h; the host: Apps/koton/plug): each
# Apps/kp_<name>/main.cpp -> Apps/kp_<name>/kp_<name>.elf, a newlib app with the FPU. Built next to
# its source, NOT as user/*.elf (make stage makes
# an SD:/apps/<name>.app of each of those: the dock would list them): `make stage` copies them to
# SD:/koton/plugins/<name>/main with their plugin.json.
KPLUG_DIRS = $(wildcard Apps/kp_*)
KPLUG_ELFS = $(foreach d,$(KPLUG_DIRS),$(d)/$(notdir $(d)).elf)
KPLUG_HDR = kplug.h kplug_proto.h applet_proto.h json.hpp kapi.h Apps/koton/engine/kbase.h
define KPLUG_RULE
$(1)/$(notdir $(1)).elf: $(1)/main.cpp $$(KPLUG_HDR) lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld
	$$(CXX) $$(NL_CXXFLAGS) -fno-math-errno -Wall $$(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $$< lib/uikit.imp.a -lm -o $$@
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
# Linked by freestanding apps (gpcdemo) and newlib ones alike.
gpucomp/gpucomp.o: gpucomp/gpucomp.c gpucomp/gpucomp.h kapi.h ../kernel/include/kern/kapi_abi.h
	$(CC) $(filter-out -mgeneral-regs-only -O2,$(CFLAGS)) -mcpu=cortex-a72 -O3 -ffp-contract=off -fno-math-errno -I. -c $< -o $@
gpucomp/libgpucomp.a: gpucomp/gpucomp.o
	$(AR) rcs $@ $^

snes/libsnes.a: $(SNES_OBJS)
	$(AR) rcs $@ $^

# Onyx BASIC (basic/): the core (compiler + VM, doubles -> FP/SIMD, no libc) as a library
# (linked into the qbasic editor too), and the runtime /bin/basic (basic/runtime.cpp).
BAS_HDR = basic/bas.h basic/basint.h basic/basjit.h basic/basnum.h basic/bas3d.h basic/bas3dscene.h basic/basscreen.h kapi.h
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
# The Calendar: a newlib app as Letters (FreeType's text through uikit's face); its model (iCalendar), views
# and dialogs in Apps/calendar/*.h.
calendar.elf: Apps/calendar/main.cpp $(wildcard Apps/calendar/*.h) notify.h applib.h lib/ft.imp.a ft/uikitface.h \
	      lib/uikit.imp.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< \
	       lib/uikit.imp.a lib/ft.imp.a -lm -o $@
agenda.elf: notify.h
setup.elf: Apps/setup/system.h wallpaper.h img/imgload.hpp
# The FreeType apps (FT_APPS): newlib + lib/ft.imp.a, as the Calendar. The Game Library also links the
# emulators it takes the covers and the save states' pictures from.
FT_EXTRA_gamelib = gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a
FT_EXTRA_fmtracker = lib/audiokit.imp.a
FT_EXTRA_soundconf = lib/audiokit.imp.a
soundconf.elf: lib/audiokit.imp.a audiokit/audiokit.h
fmtracker.elf: lib/audiokit.imp.a audiokit/audiokit.h
FT_EXTRA_printconf = lib/printerkit.imp.a
printconf.elf: lib/printerkit.imp.a printerkit/printerkit.h
$(FT_APP_ELFS): %.elf: Apps/%/main.cpp applib.h bmp.hpp umm.h onyxpp.hpp lib/ft.imp.a ft/uikitface.h lib/uikit.imp.a 		       gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a libc/crt0libc.o libc/onyx_syscalls.o user.ld kapi.h
	$(CXX) $(NL_CXXFLAGS) -Ift -I$(FT_SRC)/include $(NL_LDFLAGS) libc/crt0libc.o libc/onyx_syscalls.o $< 	       $(FT_EXTRA_$*) lib/uikit.imp.a lib/ft.imp.a -lm -o $@
# Ledger: its books, pages and printing in Apps/ledger/*.h (Cardfile's model and widgets: the merge's data;
# the reports' .xlsx: the PNG writer's zip / deflate).
ledger.elf: $(wildcard Apps/ledger/*.h) $(wildcard Apps/cardfile/*.h) clipboard.h docguard.h img/pngsave.hpp
basic/%.o: basic/%.cpp $(BAS_HDR)
	$(CXX) $(CXXFLAGS_FP) -I. -c $< -o $@
basic/libbasic.a: $(BAS_OBJ)
	$(AR) rcs $@ $^
bin/basic.elf: basic/runtime.cpp basic/libbasic.a lib/uikit.imp.a lib/audiokit.imp.a audiokit/audiokit.h crt0.S user.ld kapi.h $(BAS_HDR)
	$(CXX) $(CXXFLAGS_FP) -I. $(LDFLAGS) $(KAPI_ALIASES) crt0.S basic/runtime.cpp basic/libbasic.a lib/audiokit.imp.a lib/uikit.imp.a -o $@

clean:
	rm -f *.elf *.o ft/*.o ft/*.a zlib/*.o zlib/*.a libc/*.o basic/*.o basic/*.a $(KPLUG_ELFS) gb/*.o gb/*.a gba/*.o gba/*.a nes/*.o nes/*.a snes/*.o snes/*.a n64/*.o n64/*.a gc/*.o gc/*.a v3d/*.o v3d/*.a gpucomp/*.o gpucomp/*.a bin/basic.elf
	rm -f uikit/*.o uikit/*.a
	rm -rf lib
	rm -rf Apps/pdf/obj Apps/koton/obj Apps/media/obj
	@$(MAKE) --no-print-directory -C bin clean
	@$(MAKE) --no-print-directory -C doom clean
