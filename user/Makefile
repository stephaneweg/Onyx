#
# Makefile -- userland ELF programs (EL0), built with the same aarch64-none-elf
# toolchain, linked at USER_VA_BASE (Runtime/user.ld). Integer-only by default
# (-mgeneral-regs-only). An app may opt into hardware float by overriding the
# flags (drop -mgeneral-regs-only, add -mcpu=cortex-a72): the kernel now saves the
# full FP/SIMD state on every trap (kern/trapframe.h), so FP is safe across context
# switches. See user/BinUtils/Makefile (fptest) for the recipe.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
AR      = $(PREFIX)ar

# -I../kernel/include -IKits: apps include <kern/kapi_abi.h> (the shared ABI). Apps call
# the kernel through the ABI table at a fixed VA, so they DON'T link against kernel
# addresses and need not be rebuilt when the kernel changes.
CFLAGS  = -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only \
	  -O2 -Wall -Wextra -fno-stack-protector -I../kernel/include -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts
# C++ apps: same base, freestanding C++ subset (no exceptions/RTTI/STL; function-
# local statics are single-threaded; static dtors are not run -- see Runtime/onyxpp.hpp).
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
# Every program reaches the kernel through AppKit (SD:/lib/appkit.so, Kits/appkit/appkit.c: the kapi_* functions,
# by name): its import stubs (lib/appkit_stubs.o, generated: each one jumps through AppKit's table, which the
# kernel puts at a fixed address for every program -- no bind, nothing to open) are linked into each of them.
APPKIT_STUBS = lib/appkit_stubs.o
LDFLAGS = -Wl,-T,Runtime/user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none $(APPKIT_STUBS)
# Freestanding apps have no libc: alias the C memory primitives GCC may emit on its own
# onto the kapi ones (Kits/appkit/appkit.h, ABI v36 -> the kernel's Circle implementations).
KAPI_ALIASES = -Wl,--defsym,memset=kapi_memset -Wl,--defsym,memcpy=kapi_memcpy \
	       -Wl,--defsym,memmove=kapi_memmove

# All graphical apps live in Apps/<name>/main.cpp (one folder per app) and build to
# <name>.elf here at user/ (so `make stage` keeps globbing ../user/*.elf; it stages
# each one as apps/<name>.app/main, without extension). Every graphical
# app is C++ on the uikit toolkit now; the /bin console tools stay C in BinUtils/.
APPS = tinycalc tinypad cppdemo \
       demoA demoB demoC demoD demoE demoF inidemo \
       tetris snake same terminal \
       2048 life pong sokoban \
       mandelbrot minesweeper \
       eyes voronoy irc spin wtkdemo notifyd shutdown plasma agenda imageview widgets qbasic lisa invaders pipes solitaire freecell graphcalc iconedit rtfview gbemu gbaemu nesemu snesemu teapot gpudemo n64emu gcemu wifimenu dock lock cardfile

# The apps whose text is FreeType's (DejaVu Sans through uikit's face, Kits/fontkit/uikitface.h): newlib apps as
# the Calendar (the rule below). Every new app joins them (docs/03, "Fonts").
FT_APPS = disks control theme config wpaconf padconf dockconf soundconf displayconf keyconf langconf modeconf pocketshell consolehome preloadconf printconf gamelib setup menubar fileviewer ledger fmtracker taskman gpiolab notes stickies circuits pinball critters clock
FT_APP_ELFS = $(FT_APPS:=.elf)

APP_ELFS = $(APPS:=.elf)

all: libs $(APP_ELFS) $(FT_APP_ELFS) letters.elf sheet.elf slides.elf qbstudio.elf turtle.elf 3dforge.elf koton.elf courier.elf calendar.elf paint.elf archiver.elf clipd.elf clockd.elf clipboard.elf screenshot.elf media.elf pdf.elf mail.elf telegram.elf photos.elf pkgman.elf pkgd.elf printd.elf plugins bin servers BinUtils/basic.elf doom
# (AppKit's stubs are linked into every program: made first)
# (a real prerequisite, not an order-only one: every program relinks when AppKit's stubs change -- libgen
# rewrites them only when a slot moved. On 2026-10-08 an order-only one let the programs keep stale stubs:
# none could start. The link lines use $< and LDFLAGS, never $^: it is not linked twice)
$(APP_ELFS) $(FT_APP_ELFS) letters.elf sheet.elf slides.elf qbstudio.elf turtle.elf 3dforge.elf koton.elf courier.elf calendar.elf paint.elf archiver.elf clipd.elf clockd.elf clipboard.elf screenshot.elf media.elf pdf.elf mail.elf telegram.elf photos.elf pkgman.elf pkgd.elf printd.elf BinUtils/basic.elf $(KPLUG_ELFS): lib/appkit_stubs.o

# ---- Shared libraries (docs/SHARED-LIBS-PLAN.md; kapi v83): position-independent code linked with
# Runtime/lib.ld at 0 -- the kernel places it --, ONE exported symbol (Runtime/lib.vers), the export table, which is
# the ELF entry. -z text and --no-undefined: no relocation in the code, nothing imported by name; what
# is left for the kernel is R_AARCH64_RELATIVE relocations in the data. Built into lib/<name>.so,
# staged as SD:/lib/<name>.so. librt.o is every library's runtime (the importer's allocator, the
# constructors).
LIB_CXXFLAGS = -ffreestanding -nostdlib -fPIC -fvisibility=hidden -mgeneral-regs-only -O2 -Wall -Wextra \
	       -fno-stack-protector -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
	       -I. -I../kernel/include -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts
LIB_LD      = $(PREFIX)ld
LIB_LDFLAGS = -shared -Bsymbolic -z text -z max-page-size=0x10000 --no-undefined --hash-style=sysv \
	      --build-id=none -T Runtime/lib.ld --version-script Runtime/lib.vers -e onyx_lib_table
# (a library calls the kernel as a program does: AppKit's stubs in each -- what it does not use is dropped)
LIB_KAPI    = lib/appkit_stubs.o
LIB_DEP     = Runtime/lib.ld Runtime/lib.vers Runtime/lib.h Kits/appkit/appkit.h ../kernel/include/kern/kapi_abi.h
lib/librt.o: Runtime/librt.cpp $(LIB_DEP)
	@mkdir -p lib
	$(CXX) $(LIB_CXXFLAGS) -c $< -o $@
# The loader's test library (BinUtils/libtest runs it) and its "other build".
lib/demo.o: Libs/demo/demolib.cpp Libs/demo/demo.h $(LIB_DEP)
	@mkdir -p lib
	$(CXX) $(LIB_CXXFLAGS) -c $< -o $@
lib/demo2.o: Libs/demo/demolib.cpp Libs/demo/demo.h $(LIB_DEP)
	@mkdir -p lib
	$(CXX) $(LIB_CXXFLAGS) -DDEMO_BUILD=2 -c $< -o $@
lib/demo.so lib/demo2.so: lib/%.so: lib/%.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) -o $@ $^
# A library's interface, both halves, from its objects and its append-only list <lib>.abi
# (tools/libgen/libgen.py): lib/<lib>_table.S (the library's export table), lib/<lib>_stubs.S and
# lib/<lib>_bind.cpp (the program's import stubs and the constructor that opens the library) --
# the last two make lib/<lib>.imp.a, which a program links where it linked the static library.
LIBGEN = python3 ../tools/libgen/libgen.py --nm $(PREFIX)nm
IMP_CXXFLAGS = $(CXXFLAGS) -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts
lib/%_table.o: lib/%_table.S
	$(CC) -c $< -o $@
lib/%_stubs.o: lib/%_stubs.S
	$(CC) -c $< -o $@
# (a C program's bind -- libgen --bind-c: lib/<name>.imp_c.a; the library's allocator is umm.h's, in this object)
lib/%_bind_c.o: lib/%_bind_c.c Runtime/lib.h Runtime/umm.h Kits/appkit/appkit.h
	$(CC) $(CFLAGS) -I. -c $< -o $@
lib/%_bind.o: lib/%_bind.cpp Runtime/lib.h Kits/appkit/appkit.h
	$(CXX) $(IMP_CXXFLAGS) -c $< -o $@
lib/fontkit.imp.a: lib/fontkit_stubs.o lib/fontkit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^
.PRECIOUS: lib/%_table.S lib/%_stubs.S lib/%_bind.cpp lib/%_table.o lib/%_stubs.o lib/%_bind.o

# ---- fontkit.so: FreeType (the apps' TrueType-only build: Kits/fontkit/onyx_ftoption.h, Kits/fontkit/onyx_ftmodule.h) as a
# shared library. Its table: FreeType's public API (the functions its public headers declare: the list
# lib/ft.exports), in Kits/fontkit/fontkit.abi's order. Kits/fontkit/ftso.c: its memory through the importer's allocator; the
# string functions, qsort and setjmp from the toolchain's newlib, linked in.
FT_SRC = ../third_party/freetype-2.14.3
FT_SO_CFLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections \
	       -I. -I../kernel/include -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts
FT_SO_OBJ = $(addprefix lib/ft/,ftsystem.o ftinit.o ftdebug.o ftbase.o ftbitmap.o ftsynth.o autofit.o truetype.o sfnt.o smooth.o)
lib/ft/%.o: %.c Kits/fontkit/onyx_ftoption.h Kits/fontkit/onyx_ftmodule.h
	@mkdir -p lib/ft
	$(CC) $(FT_SO_CFLAGS) -w -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' \
	      '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' -IKits/fontkit -I$(FT_SRC)/include -c $< -o $@
lib/ft/ftso.o: Kits/fontkit/ftso.c Runtime/lib.h Kits/appkit/appkit.h
	@mkdir -p lib/ft
	$(CC) $(FT_SO_CFLAGS) -Wall -c $< -o $@
lib/ft.exports: $(wildcard $(FT_SRC)/include/freetype/*.h)
	@mkdir -p lib
	@grep -hoE "^  FT_[A-Za-z0-9_]+\(" $^ | sed 's/^  //; s/($$//' | sort -u > $@
lib/fontkit_table.S: $(FT_SO_OBJ) lib/ft/ftso.o lib/ft.exports ../tools/libgen/libgen.py
	$(LIBGEN) --name fontkit --abi Kits/fontkit/fontkit.abi --init ft_lib_init --export-file lib/ft.exports --allow-data . \
		--table lib/fontkit_table.S --stubs lib/fontkit_stubs.S --bind lib/fontkit_bind.cpp $(FT_SO_OBJ)
lib/fontkit_stubs.S lib/fontkit_bind.cpp: lib/fontkit_table.S ;
LIBC_A   := $(shell $(CC) -mcpu=cortex-a72 -print-file-name=libc.a)
LIBGCC_A := $(shell $(CC) -mcpu=cortex-a72 -print-libgcc-file-name)
lib/fontkit.so: $(FT_SO_OBJ) lib/ft/ftso.o lib/fontkit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBC_A) $(LIBGCC_A)

UIKIT_HDR = $(wildcard Kits/uikit/*.h) Kits/appkit/appkit.h Runtime/onyxpp.hpp Kits/systemkit/applet_proto.h Kits/uikit/bmp.h Kits/systemkit/clipboard.h Include/gamepad.h 
# ---- uikit.so: the toolkit as a shared library. Its table: every global function of uikit's objects
# (Kits/uikit/uikit.abi, append-only), the programs' copies of its vtables (--vtables); its global variables
# are the program's (Kits/uikit/globals.cpp, compiled into the import library too: --data). globals.o is
# linked first: its initialisers bind the library's references before any other constructor.
UIKIT_SO_SRC = $(sort $(filter-out Kits/uikit/globals.cpp,$(wildcard Kits/uikit/*.cpp)) Kits/uikit/layout_lock.cpp)
UIKIT_SO_OBJ = lib/uikit/globals.o $(patsubst Kits/uikit/%.cpp,lib/uikit/%.o,$(UIKIT_SO_SRC))
LIB_CXXFLAGS_NEON = $(filter-out -mgeneral-regs-only,$(LIB_CXXFLAGS)) -mcpu=cortex-a72
lib/uikit/%.o: Kits/uikit/%.cpp $(UIKIT_HDR) Kits/uikit/globals.inc $(LIB_DEP)
	@mkdir -p lib/uikit
	$(CXX) $(LIB_CXXFLAGS) -DONYX_LIB_BUILD -c $< -o $@
lib/uikit/canvas.o: Kits/uikit/canvas.cpp $(UIKIT_HDR) $(LIB_DEP)
	@mkdir -p lib/uikit
	$(CXX) $(LIB_CXXFLAGS_NEON) -DONYX_LIB_BUILD -c $< -o $@
# The port (Kits/uikit/port/: what differs between the graphics servers -- the window driver, the server's
# protocol; docs/POCKETUI-TECH-STUDY.md section 5): Kits/uikit/port.cpp compiles the desktop's (Elegant's).
lib/uikit/port.o: $(wildcard Kits/uikit/port/*.h Kits/uikit/port/*.cpp Kits/uikit/port/*.inc)
lib/uikit/imgload.o: Kits/uikit/imgload.cpp Kits/imagekit/img/imgload.hpp Kits/imagekit/imagekit.h $(LIB_DEP)
	@mkdir -p lib/uikit
	$(CXX) $(LIB_CXXFLAGS_NEON) -DONYX_LIB_BUILD -Wno-unused-parameter -Wno-sign-compare -c $< -o $@
# The layout lock (Kits/uikit/abi.h): the classes' sizes and the base classes' offsets, as static_asserts
# compiled with the library. Made once from the headers; `make uikit-layout-update` only for a class added.
UIKIT_LAYOUT = python3 ../tools/libgen/layout.py --cxx $(CXX) --flags "$(LIB_CXXFLAGS) -DONYX_LIB_BUILD" --umbrella Kits/uikit/uikit.h 	--ns uikit --headers 'Kits/uikit/*.h' --out Kits/uikit/layout_lock.cpp --offsets 	Widget:canvas,left,tag,anchor,transparent,tip,parent,prevHandled,ext,reserved_ Canvas:px,w,stride,owns,capH,ext Root:bg
Kits/uikit/layout_lock.cpp:
	$(UIKIT_LAYOUT)
.PHONY: uikit-layout-update
uikit-layout-update:
	$(UIKIT_LAYOUT) --update
lib/uikit_table.S: $(UIKIT_SO_OBJ) Kits/uikit/uikit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name uikit --abi Kits/uikit/uikit.abi --init uikit_lib_init --vtables --data onyx_uikit_data --exclude '^_ZN5uikit(4port|8internal)' \
		--allow-data '^(_ZN5uikit|onyx_imagekit_table$$|onyx_systemkit_table$$)' --table lib/uikit_table.S --stubs lib/uikit_stubs.S --bind lib/uikit_bind.cpp \
		--bind-c lib/uikit_bind_c.c $(UIKIT_SO_OBJ)
Kits/uikit/uikit.abi:
	@touch $@
lib/uikit_stubs.S lib/uikit_bind.cpp lib/uikit_bind_c.c: lib/uikit_table.S ;
# (its img_load... are relays to ImageKit: that library's import stubs linked in, opened at the first picture)
lib/uikit.so: $(UIKIT_SO_OBJ) lib/imagekit_stubs.o lib/systemkit_stubs.o lib/uikit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) -o $@ $^ $(LIBGCC_A)
# ---- lib/pocket/uikit.so: PocketUI's UIKit (the pocket and console modes' server loads it under the name
# SD:/lib/uikit.so: kapi_lib_open_as). The SAME objects as lib/uikit.so but the port, compiled with UK_PORT_POCKET
# (Kits/uikit/port/port_pocket.cpp: PocketUI's protocol); its table generated against the same Kits/uikit/uikit.abi
# with --frozen (a function new to one library only is an error: entries come through the desktop's build first),
# no stubs nor bind of its own (the programs link the desktop's lib/uikit.imp.a: the stubs are by slot, the same for
# both). tools/libgen/abi_same.py then checks the two tables identical slot by slot (docs/03 "One UIKit per server").
UIKIT_POCKET_OBJ = $(patsubst lib/uikit/port.o,lib/pocket/port.o,$(UIKIT_SO_OBJ))
lib/pocket/port.o: Kits/uikit/port.cpp $(wildcard Kits/uikit/port/*.h Kits/uikit/port/*.cpp Kits/uikit/port/*.inc) $(UIKIT_HDR) $(LIB_DEP)
	@mkdir -p lib/pocket
	$(CXX) $(LIB_CXXFLAGS) -DONYX_LIB_BUILD -DUK_PORT_POCKET -c $< -o $@
lib/pocket/uikit_table.S: $(UIKIT_POCKET_OBJ) Kits/uikit/uikit.abi ../tools/libgen/libgen.py | lib/uikit_table.S
	$(LIBGEN) --name uikit --abi Kits/uikit/uikit.abi --init uikit_lib_init --vtables --data onyx_uikit_data --exclude '^_ZN5uikit(4port|8internal)' \
		--allow-data '^(_ZN5uikit|onyx_imagekit_table$$|onyx_systemkit_table$$)' --frozen --table lib/pocket/uikit_table.S $(UIKIT_POCKET_OBJ)
lib/pocket/uikit.so: $(UIKIT_POCKET_OBJ) lib/imagekit_stubs.o lib/systemkit_stubs.o lib/pocket/uikit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) -o $@ $^ $(LIBGCC_A)
lib/pocket/abi_same.stamp: lib/uikit.so lib/pocket/uikit.so lib/uikit_table.S lib/pocket/uikit_table.S ../tools/libgen/abi_same.py
	python3 ../tools/libgen/abi_same.py --nm $(PREFIX)nm lib/uikit.so lib/pocket/uikit.so --tables lib/uikit_table.S lib/pocket/uikit_table.S
	@touch $@

# The program side: the stubs, the bind constructor, and the variables (Kits/uikit/globals.cpp).
lib/uikit_globals.o: Kits/uikit/globals.cpp Kits/uikit/globals.inc $(UIKIT_HDR)
	@mkdir -p lib
	$(CXX) $(CXXFLAGS) -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
lib/uikit.imp.a: lib/uikit_stubs.o lib/uikit_bind.o lib/uikit_globals.o
	@rm -f $@
	$(AR) rcs $@ $^
# A C program's side (the window API, uikit/win.h: rdpd, gpcdemo, el0test, wstest): the stubs and the C bind
# (no C++ runtime, no variables handed over: the library keeps its own copy of UIKit's globals). A newlib
# program compiles lib/uikit_bind_c.c itself with -DONYX_BIND_LIBC (its malloc) instead of this archive's bind.
lib/uikit.imp_c.a: lib/uikit_stubs.o lib/uikit_bind_c.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- printerkit.so: printing (Kits/printerkit/printerkit.h; docs/03 "Printing") -- a job's pages recorded, the Print dialog.
# Its table: the print_ functions (Kits/printerkit/printerkit.abi). The first library that uses others: FreeType (the
# text's glyphs) and uikit (the dialog), through their import stubs linked in -- Kits/printerkit/printerkit.cpp opens
# fontkit.so / uikit.so when first needed and sets the stubs' tables. uikit's variables are the program's, as for
# uikit.so itself: uikit's globals.o (the library build: references bound through the importer's table) and
# --data onyx_uikit_data in the bind object. Floats in its interface: FP/SIMD flags.
PRINT_HDR = $(wildcard Kits/printerkit/*.h) Libs/pdf/pdfwrite.h Kits/imagekit/img/pngsave.hpp
PRINT_SO_OBJ = lib/printerkit/printerkit.o lib/printerkit/dialog.o
lib/printerkit/%.o: Kits/printerkit/%.cpp $(PRINT_HDR) $(UIKIT_HDR) $(LIB_DEP)
	@mkdir -p lib/printerkit
	$(CXX) $(LIB_CXXFLAGS_NEON) -DONYX_LIB_BUILD -IKits/fontkit -I$(FT_SRC)/include -c $< -o $@
lib/printerkit_table.S: $(PRINT_SO_OBJ) Kits/printerkit/printerkit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name printerkit --abi Kits/printerkit/printerkit.abi --init print_lib_init --export '^print_' --exclude '^print_lib_init$$' 		--allow-data '^onyx_' --data onyx_uikit_data --table lib/printerkit_table.S --stubs lib/printerkit_stubs.S --bind lib/printerkit_bind.cpp $(PRINT_SO_OBJ)
Kits/printerkit/printerkit.abi:
	@touch $@
lib/printerkit_stubs.S lib/printerkit_bind.cpp: lib/printerkit_table.S ;
lib/printerkit.so: $(PRINT_SO_OBJ) lib/uikit/globals.o lib/uikit_stubs.o lib/fontkit_stubs.o lib/printerkit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBGCC_A)
lib/printerkit.imp.a: lib/printerkit_stubs.o lib/printerkit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- audiokit.so: AudioKit, the shared sound library (Kits/audiokit/audiokit.h; docs/03 "AudioKit"). In it:
# MeltySynth, Koton's SoundFont synthesizer (Apps/koton/synth: its C++ interface exported as it is), the
# audio decoders (Apps/media/codecs.c, vorbis.c: minimp3, dr_flac, dr_wav, stb_vorbis -- their C
# interfaces as they are), and AudioKit's own functions (Kits/audiokit/akcore.cpp: the files, the background
# player, the output, mixing, notes, WAV). Built with the FPU; newlib's libm and libc linked in. Exported:
# ms::Synthesizer and ms::soundfont_* (not the synthesizer's inner classes), the decoders' functions, ak_*.
AK_FLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections \
	   -fno-math-errno -DONYX_LIB_BUILD -I. -I../kernel/include -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts
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
lib/ak/akcore.o: Kits/audiokit/akcore.cpp Kits/audiokit/audiokit.h Kits/audiokit/fmsynth.h Kits/audiokit/fm_tables.h Apps/fmtracker/fms.h Apps/media/tags.h Apps/media/decode.h Apps/media/midi.h Apps/media/codecs.h \
		 Apps/koton/synth/meltysynth.h Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/ak
	$(CXX) $(AK_CXXFLAGS) -Wall -I../third_party -IApps/media -IApps/koton -c $< -o $@
lib/ak/akmix.o lib/ak/aksf.o lib/ak/akwav.o: lib/ak/%.o: Kits/audiokit/%.cpp Kits/audiokit/audiokit.h Apps/koton/synth/meltysynth.h Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/ak
	$(CXX) $(AK_CXXFLAGS) -Wall -IApps/koton -c $< -o $@
# (the FM synthesizer: Kits/audiokit/fmsynth.h)
lib/ak/akfm.o: Kits/audiokit/akfm.cpp Kits/audiokit/audiokit.h Kits/audiokit/fmsynth.h Kits/audiokit/fm_tables.h Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/ak
	$(CXX) $(AK_CXXFLAGS) -w -c $< -o $@
lib/ak/akso.o: Kits/audiokit/akso.c Runtime/lib.h Kits/appkit/appkit.h
	@mkdir -p lib/ak
	$(CC) $(AK_FLAGS) -Wall -c $< -o $@
Kits/audiokit/audiokit.abi:
	@touch $@
lib/audiokit_table.S: $(AK_OBJ) lib/ak/akso.o Kits/audiokit/audiokit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name audiokit --abi Kits/audiokit/audiokit.abi --init ak_lib_init --allow-data . \
		--export '^(_ZNK?2ms(11Synthesizer|[0-9]+soundfont_)|mp3dec_|drflac_|drwav_|stb_vorbis_|ak_)' \
		--table lib/audiokit_table.S --stubs lib/audiokit_stubs.S --bind lib/audiokit_bind.cpp $(AK_OBJ)
lib/audiokit_stubs.S lib/audiokit_bind.cpp: lib/audiokit_table.S ;
LIBM_A := $(shell $(CC) -mcpu=cortex-a72 -print-file-name=libm.a)
lib/audiokit.so: $(AK_OBJ) lib/ak/akso.o lib/audiokit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBM_A) $(LIBC_A) $(LIBGCC_A)
lib/audiokit.imp.a: lib/audiokit_stubs.o lib/audiokit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- filekit.so: FileKit, what programs do with files (Kits/filekit/filekit.h; docs/03 "FileKit"): zlib
# (third_party, compiled for the library), the archives' engine (Kits/filekit/arc.h, zip.h, tar.h, ops.h: ZIP read
# and written, tar / tar.gz / gzip read -- the Archiver calls it through fk_arc_*), ZIP archives in memory, whole files, trees copied / moved / removed, paths, key / value text documents (Kits/filekit/kvtext.h).
# Integer only in its interface; newlib's libc linked in. Exported: fk_*.
ZLIB_SRC = ../third_party/zlib-1.3.1
FK_ZOBJ = $(addprefix lib/fk/z_,adler32.o crc32.o deflate.o inflate.o inffast.o inftrees.o trees.o zutil.o)
FK_OBJ = lib/fk/fkcore.o lib/fk/fsutil.o lib/fk/kvtext.o $(FK_ZOBJ)
lib/fk/z_%.o: $(ZLIB_SRC)/%.c
	@mkdir -p lib/fk
	$(CC) $(AK_FLAGS) -w -I$(ZLIB_SRC) -c $< -o $@
lib/fk/fkcore.o: Kits/filekit/fkcore.cpp Kits/filekit/filekit.h $(wildcard Kits/filekit/*.h) Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/fk
	$(CXX) $(AK_CXXFLAGS) -Wall -IKits/filekit -I$(ZLIB_SRC) -c $< -o $@
lib/fk/fsutil.o: Kits/filekit/fsutil.cpp Kits/filekit/fsutil.h Kits/filekit/fsutil.inc Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/fk
	$(CXX) $(AK_CXXFLAGS) -Wall -c $< -o $@
lib/fk/kvtext.o: Kits/filekit/kvtext.cpp Kits/filekit/kvtext.h Kits/filekit/kvtext.inc Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/fk
	$(CXX) $(AK_CXXFLAGS) -Wall -c $< -o $@
lib/fk/fkso.o: Kits/filekit/fkso.c Runtime/lib.h Kits/appkit/appkit.h
	@mkdir -p lib/fk
	$(CC) $(AK_FLAGS) -Wall -c $< -o $@
Kits/filekit/filekit.abi:
	@touch $@
lib/filekit_table.S: $(FK_OBJ) lib/fk/fkso.o Kits/filekit/filekit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name filekit --abi Kits/filekit/filekit.abi --init fk_lib_init --allow-data . --export '^(fk_|fs_)' \
		--table lib/filekit_table.S --stubs lib/filekit_stubs.S --bind lib/filekit_bind.cpp $(FK_OBJ)
lib/filekit_stubs.S lib/filekit_bind.cpp: lib/filekit_table.S ;
lib/filekit.so: $(FK_OBJ) lib/fk/fkso.o lib/filekit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBC_A) $(LIBGCC_A)
lib/filekit.imp.a: lib/filekit_stubs.o lib/filekit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- imagekit.so: ImageKit, pictures (Kits/imagekit/imagekit.h; docs/03 "ImageKit"): the decoders
# (Kits/imagekit/img/imgload.hpp: stb_image, simplewebp, PCX), the encoders (Kits/imagekit/img/pngsave.hpp; PNG's deflate is zlib's,
# through FileKit: its import stubs linked in, the library opened when a PNG is first written), what a
# file says and the adjustments (Photos' exif.h, imgops.h), one resize right in alpha. Built with the
# FPU, an integer interface; newlib's libm and libc linked in. Exported: ik_*.
IK_OBJ = lib/ik/ikcore.o
lib/ik/ikcore.o: Kits/imagekit/ikcore.cpp Kits/imagekit/imagekit.h Kits/filekit/filekit.h Kits/imagekit/img/imgload.hpp Kits/imagekit/img/pngsave.hpp Kits/imagekit/img/stb_image.h Kits/imagekit/img/simplewebp.h \
		 Apps/photos/imgops.h Apps/photos/exif.h Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/ik
	$(CXX) $(AK_CXXFLAGS) -w -IKits/imagekit -c $< -o $@
lib/ik/ikso.o: Kits/imagekit/ikso.c Runtime/lib.h Kits/appkit/appkit.h
	@mkdir -p lib/ik
	$(CC) $(AK_FLAGS) -Wall -c $< -o $@
Kits/imagekit/imagekit.abi:
	@touch $@
lib/imagekit_table.S: $(IK_OBJ) lib/ik/ikso.o Kits/imagekit/imagekit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name imagekit --abi Kits/imagekit/imagekit.abi --init ik_lib_init --allow-data . --export '^ik_' --exclude '^ik_lib_init$$' \
		--table lib/imagekit_table.S --stubs lib/imagekit_stubs.S --bind lib/imagekit_bind.cpp $(IK_OBJ)
lib/imagekit_stubs.S lib/imagekit_bind.cpp: lib/imagekit_table.S ;
lib/imagekit.so: $(IK_OBJ) lib/ik/ikso.o lib/filekit_stubs.o lib/imagekit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBM_A) $(LIBC_A) $(LIBGCC_A)
lib/imagekit.imp.a: lib/imagekit_stubs.o lib/imagekit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- appkit.so: AppKit, the interface between the programs and the kernel (Kits/appkit/appkit.c + appkit_calls.inc:
# the bodies of the calls appkit.h declares; docs/03
# "AppKit"). Its table: every kapi_* function, by name (Kits/appkit/appkit.abi, append-only). Its stubs read the
# table where the kernel copies it for every program (APPKIT_TABLE_VA, kern/kapi_abi.h): --fixed-table. No
# state, no library runtime (so no strlen / memset of GCC's own making in it: -fno-tree-loop-distribute-patterns); built with the FPU on (some calls take floats: they pass through).
APPKIT_TABLE_VA = 0x380008000
lib/appkit/appkit.o: Kits/appkit/appkit.c Kits/appkit/appkit_calls.inc Kits/appkit/appkit_lib.inc Kits/appkit/appkit.h Runtime/lib.h ../kernel/include/kern/kapi_abi.h
	@mkdir -p lib/appkit
	$(CC) $(AK_FLAGS) -Wall -Wno-unused-function -fno-tree-loop-distribute-patterns -fno-builtin -c $< -o $@
Kits/appkit/appkit.abi:
	@touch $@
lib/appkit_table.S: lib/appkit/appkit.o Kits/appkit/appkit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name appkit --abi Kits/appkit/appkit.abi --init appkit_init --allow-data . --export '^(kapi_|ax_|app_ini_|lx_)' \
		--fixed-table $(APPKIT_TABLE_VA) --table lib/appkit_table.S --stubs lib/appkit_stubs.S lib/appkit/appkit.o
lib/appkit_stubs.S: lib/appkit_table.S ;
lib/appkit.so: lib/appkit/appkit.o lib/appkit_table.o
	$(LIB_LD) $(LIB_LDFLAGS) --gc-sections -o $@ $^ $(LIBGCC_A)

# ---- systemkit.so: SystemKit -- what talks to the system and to the other programs (Kits/systemkit: the
# wallpaper, the volume, the trash, the preload list, the file associations, the notifications, the
# clipboard; docs/03 "SystemKit"). Freestanding, integer only. Exported by name: its headers' functions.
SK_HDR = $(wildcard Kits/systemkit/*.h) $(wildcard Kits/systemkit/*.inc) Kits/filekit/fsutil.h Kits/filekit/fsutil.inc
lib/sk/systemkit.o: Kits/systemkit/systemkit.cpp $(SK_HDR) Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/sk
	$(CXX) $(LIB_CXXFLAGS) -Wno-unused-function -DONYX_LIB_BUILD -IKits/systemkit -c $< -o $@
Kits/systemkit/systemkit.abi:
	@touch $@
lib/systemkit_table.S: lib/sk/systemkit.o Kits/systemkit/systemkit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name systemkit --abi Kits/systemkit/systemkit.abi --init sk_lib_init --allow-data . --exclude '^(sk_lib_init|onyx_)' \
		--table lib/systemkit_table.S --stubs lib/systemkit_stubs.S --bind lib/systemkit_bind.cpp --bind-c lib/systemkit_bind_c.c lib/sk/systemkit.o
lib/systemkit_stubs.S lib/systemkit_bind.cpp lib/systemkit_bind_c.c: lib/systemkit_table.S ;
lib/systemkit.so: lib/sk/systemkit.o lib/systemkit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) -o $@ $^ $(LIBGCC_A)
lib/systemkit.imp.a: lib/systemkit_stubs.o lib/systemkit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^
lib/systemkit.imp_c.a: lib/systemkit_stubs.o lib/systemkit_bind_c.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- netkit.so: NetKit -- the network for the programs (Kits/netkit: the FTP volumes' client, the small
# HTTP/1.0 client; http.hpp, the HTTP/1.1 class, is still a header there). Freestanding.
NK_HDR = $(wildcard Kits/netkit/*.h) $(wildcard Kits/netkit/*.inc)
lib/nk/netkit.o: Kits/netkit/netkit.cpp $(NK_HDR) Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/nk
	$(CXX) $(LIB_CXXFLAGS) -Wno-unused-function -DONYX_LIB_BUILD -IKits/netkit -c $< -o $@
Kits/netkit/netkit.abi:
	@touch $@
lib/netkit_table.S: lib/nk/netkit.o Kits/netkit/netkit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name netkit --abi Kits/netkit/netkit.abi --init nk_lib_init --allow-data . --exclude '^(nk_lib_init|onyx_)' \
		--table lib/netkit_table.S --stubs lib/netkit_stubs.S --bind lib/netkit_bind.cpp --bind-c lib/netkit_bind_c.c lib/nk/netkit.o
lib/netkit_stubs.S lib/netkit_bind.cpp lib/netkit_bind_c.c: lib/netkit_table.S ;
lib/netkit.so: lib/nk/netkit.o lib/netkit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) -o $@ $^ $(LIBGCC_A)
lib/netkit.imp.a: lib/netkit_stubs.o lib/netkit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^
lib/netkit.imp_c.a: lib/netkit_stubs.o lib/netkit_bind_c.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- gpiokit.so: GPIOKit -- the 40-pin header (Kits/gpiokit/gpiokit.h; docs/06 "GPIOKit", docs/19): pins, PWM, edges,
# I2C, SPI over the kernel's gpio_ctl (kapi v91: GPIOKit reads the kernel's table itself, the user's exception --
# docs/03 §5.10), and a simulated board. Freestanding, integer only; shipped with the kernel (the package onyx).
lib/gk/gkcore.o: Kits/gpiokit/gkcore.cpp Kits/gpiokit/gpiokit.h Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/gk
	$(CXX) $(LIB_CXXFLAGS) -fno-tree-loop-distribute-patterns -fno-builtin -c $< -o $@
Kits/gpiokit/gpiokit.abi:
	@touch $@
lib/gpiokit_table.S: lib/gk/gkcore.o Kits/gpiokit/gpiokit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name gpiokit --abi Kits/gpiokit/gpiokit.abi --init gk_lib_init --allow-data . --export '^gk_' --exclude '^gk_lib_init$$' \
		--table lib/gpiokit_table.S --stubs lib/gpiokit_stubs.S --bind lib/gpiokit_bind.cpp --bind-c lib/gpiokit_bind_c.c lib/gk/gkcore.o
lib/gpiokit_stubs.S lib/gpiokit_bind.cpp lib/gpiokit_bind_c.c: lib/gpiokit_table.S ;
lib/gpiokit.so: lib/gk/gkcore.o lib/gpiokit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) -o $@ $^ $(LIBGCC_A)
lib/gpiokit.imp.a: lib/gpiokit_stubs.o lib/gpiokit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^
lib/gpiokit.imp_c.a: lib/gpiokit_stubs.o lib/gpiokit_bind_c.o
	@rm -f $@
	$(AR) rcs $@ $^

# ---- gamekit.so: GameKit -- the games of the card's ROMs (Kits/gamekit/gamekit.h; docs/06 "GameKit", docs/20): the
# consoles of the installed emulators, the watched folders, the ROMs, their pictures. Through AppKit only; in the
# package onyx (lib/). The Game Library and the console's shell read it.
lib/gm/gamekit.o: Kits/gamekit/gamekit.cpp Kits/gamekit/gamekit.h Kits/appkit/appkit.h $(LIB_DEP)
	@mkdir -p lib/gm
	$(CXX) $(LIB_CXXFLAGS) -fno-tree-loop-distribute-patterns -fno-builtin -DONYX_LIB_BUILD -c $< -o $@
Kits/gamekit/gamekit.abi:
	@touch $@
lib/gamekit_table.S: lib/gm/gamekit.o Kits/gamekit/gamekit.abi ../tools/libgen/libgen.py
	$(LIBGEN) --name gamekit --abi Kits/gamekit/gamekit.abi --init gm_lib_init --allow-data . --export '^games_' --exclude '^gm_lib_init$$' \
		--table lib/gamekit_table.S --stubs lib/gamekit_stubs.S --bind lib/gamekit_bind.cpp --bind-c lib/gamekit_bind_c.c lib/gm/gamekit.o
lib/gamekit_stubs.S lib/gamekit_bind.cpp lib/gamekit_bind_c.c: lib/gamekit_table.S ;
lib/gamekit.so: lib/gm/gamekit.o lib/gamekit_table.o lib/librt.o $(LIB_KAPI)
	$(LIB_LD) $(LIB_LDFLAGS) -o $@ $^ $(LIBGCC_A)
lib/gamekit.imp.a: lib/gamekit_stubs.o lib/gamekit_bind.o
	@rm -f $@
	$(AR) rcs $@ $^
lib/gamekit.imp_c.a: lib/gamekit_stubs.o lib/gamekit_bind_c.o
	@rm -f $@
	$(AR) rcs $@ $^

SHLIBS = lib/appkit.so lib/demo.so lib/demo2.so lib/fontkit.so lib/uikit.so lib/printerkit.so lib/audiokit.so lib/filekit.so lib/imagekit.so lib/systemkit.so lib/netkit.so lib/gpiokit.so lib/gamekit.so
.PHONY: libs
libs: $(SHLIBS) lib/pocket/uikit.so lib/pocket/abi_same.stamp lib/kits.bi.stamp lib/appkit_stubs.o lib/fontkit.imp.a lib/uikit.imp.a lib/printerkit.imp.a lib/audiokit.imp.a lib/filekit.imp.a lib/imagekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/systemkit.imp_c.a lib/netkit.imp_c.a lib/gpiokit.imp.a lib/gpiokit.imp_c.a lib/gamekit.imp.a lib/gamekit.imp_c.a lib/uikit.imp_c.a

# The kits' descriptions for Onyx BASIC (lib/<kit>.bi -> SD:/lib/<kit>.bi; `#import filekit`): each kit's functions,
# their places (its .abi) and their types (its headers' prototypes), by tools/kitbi/kitbi.py -- every kit of Kits/,
# so a new kit is importable without a line here (docs/03 "BASIC and the kits").
lib/kits.bi.stamp: $(wildcard Kits/*/*.abi) $(wildcard Kits/*/*.h) ../tools/kitbi/kitbi.py | $(SHLIBS)
	python3 ../tools/kitbi/kitbi.py --out-dir lib --check
	@# (the structures' sizes and fields' places it worked out, checked by the compiler: static_asserts)
	@for c in lib/*.bi.check.cpp; do \
		[ -e "$$c" ] || continue; \
		$(CXX) $(CXXFLAGS_FP) -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -fsyntax-only $$c || exit 1; \
	done
	@touch $@

# Doom (Ports/doom/: doomgeneric from third_party + the Onyx layer, against newlib) -> doom.elf.
.PHONY: doom
doom: lib/uikit.imp.a lib/appkit_stubs.o
	@$(MAKE) --no-print-directory -C Ports/doom

rtfview.elf: Apps/rtfview/rtf.h

# /bin console tools live in BinUtils/ (their own Makefile).
.PHONY: bin
bin: Libs/v3d/libv3d.a Libs/gpucomp/libgpucomp.a libs
	@$(MAKE) --no-print-directory -C BinUtils

# The system's servers live in Servers/<name>/ (their own Makefile), staged to SD:/bin/<name>:
# Elegant, the desktop's graphics server; PocketUI, the pocket and console modes' (both built with what they
# share: Servers/common/, Servers/common/common.mk).
.PHONY: servers
servers: lib/appkit_stubs.o
	@$(MAKE) --no-print-directory -C Servers/elegant
	@$(MAKE) --no-print-directory -C Servers/pocketui

# Apps/<name>/main.cpp -> <name>.elf. -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts so a nested source finds the user-side headers.
# uikit toolkit: compiled once into a static library, linked into every app.
# (+ the user-side headers uikit includes: an inline of Kits/appkit/appkit.h -- kapi_lock, kapi__core -- is compiled
# into the library, which must follow it)
UIKIT_HDR = $(wildcard Kits/uikit/*.h) Kits/appkit/appkit.h Runtime/onyxpp.hpp Kits/systemkit/applet_proto.h Kits/uikit/bmp.h Kits/systemkit/clipboard.h Include/gamepad.h 
UIKIT_OBJ = $(patsubst %.cpp,%.o,$(wildcard Kits/uikit/*.cpp))
Kits/uikit/%.o: Kits/uikit/%.cpp $(UIKIT_HDR)
	$(CXX) $(CXXFLAGS) -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
# canvas.cpp is the hot pixel path (clear/fillRect/putOther): build it with NEON/FP
# (drop -mgeneral-regs-only, target the A72). The kernel saves full FP/SIMD on every
# trap, so SIMD is safe even though the rest of uikit and every app stay integer-only --
# the NEON is confined to this TU and Canvas's public signatures use no FP regs, so the
# calling ABI is unchanged. This explicit rule overrides the Kits/uikit/%.o pattern above.
CXXFLAGS_NEON = $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72
Kits/uikit/canvas.o: Kits/uikit/canvas.cpp $(UIKIT_HDR)
	$(CXX) $(CXXFLAGS_NEON) -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
# The image codecs (stb_image, simplewebp, PCX) use floating point: FP/SIMD too.
Kits/uikit/imgload.o: Kits/uikit/imgload.cpp Kits/imagekit/img/imgload.hpp Kits/imagekit/img/stb_image.h Kits/imagekit/img/simplewebp.h
	$(CXX) $(CXXFLAGS_NEON) -Wno-unused-parameter -Wno-sign-compare -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
Kits/uikit/libuikit.a: $(UIKIT_OBJ)
	$(AR) rcs $@ $^
# (Kits/uikit/libuikit.a, the static build: no app of this Makefile links it any more -- they link the shared
# library's import library, lib/uikit.imp.a --; Jet's hosted build compiles that import side for its own toolchain, tools/webkit/build-web.sh; the PC builds make their own static toolkit.)

$(APP_ELFS): %.elf: Apps/%/main.cpp Runtime/crt0.S Runtime/user.ld Kits/appkit/appkit.h Kits/uikit/bmp.h Runtime/umm.h Runtime/onyxpp.hpp Apps/games/game.h Apps/games/cards.h Include/gamepad.h Emulators/emucore.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/audiokit.imp.a Kits/audiokit/audiokit.h lib/imagekit.imp.a Kits/imagekit/imagekit.h Libs/basic/libbasic.a Emulators/gb/libgb.a Emulators/gba/libgba.a Emulators/nes/libnes.a Emulators/snes/libsnes.a Emulators/n64/libn64.a Emulators/gc/libgc.a Libs/v3d/libv3d.a
	$(CXX) $(CXXFLAGS) -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -I$(V3D_MESA) $(LDFLAGS) $(KAPI_ALIASES) Runtime/crt0.S $< lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/audiokit.imp.a lib/imagekit.imp.a Libs/basic/libbasic.a Emulators/gb/libgb.a Emulators/gba/libgba.a Emulators/nes/libnes.a Emulators/snes/libsnes.a Emulators/n64/libn64.a Emulators/gc/libgc.a Libs/v3d/libv3d.a -o $@

# ---- Letters: a newlib app (FreeType wants a libc: ../libc's crt0libc.S + the kapi syscall layer,
# as Doom's), with the apps' TrueType-only FreeType (Kits/fontkit/: its options and modules -- no bytecode
# interpreter, no compressed / web / bitmap fonts --, the upstream sources of third_party).
# FreeType is the shared library SD:/lib/fontkit.so (above): the apps link its import library
# lib/fontkit.imp.a. (Kits/fontkit/libft.a, the static build, is still made by `make Kits/fontkit/libft.a`: nothing links it.)
NL_CFLAGS  = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -nostartfiles \
	     -ffunction-sections -fdata-sections -I. -I../kernel/include -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts
NL_CXXFLAGS = $(NL_CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
NL_LDFLAGS = -Wl,-T,Runtime/user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none -Wl,--gc-sections $(APPKIT_STUBS)
FT_SRC = ../third_party/freetype-2.14.3
vpath ft%.c $(FT_SRC)/src/base
vpath autofit.c $(FT_SRC)/src/autofit
vpath truetype.c $(FT_SRC)/src/truetype
vpath sfnt.c $(FT_SRC)/src/sfnt
vpath smooth.c $(FT_SRC)/src/smooth
FT_OBJ = $(addprefix Kits/fontkit/,ftsystem.o ftinit.o ftdebug.o ftbase.o ftbitmap.o ftsynth.o autofit.o truetype.o sfnt.o smooth.o)
Kits/fontkit/%.o: %.c Kits/fontkit/onyx_ftoption.h Kits/fontkit/onyx_ftmodule.h
	$(CC) $(NL_CFLAGS) -w -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' \
	      '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' -IKits/fontkit -I$(FT_SRC)/include -c $< -o $@
Kits/fontkit/libft.a: $(FT_OBJ)
	$(AR) rcs $@ $^
Runtime/libc/crt0libc.o: Runtime/libc/crt0libc.S
	$(CC) $(NL_CFLAGS) -c $< -o $@
Runtime/libc/onyx_syscalls.o: Runtime/libc/onyx_syscalls.c Kits/appkit/appkit.h
	$(CC) $(NL_CFLAGS) -c $< -o $@
# Letters: its model, layout and files in Apps/letters/*.h (the mail merge reads Cardfile's forms: its model.h;
# .docx / .odt: the PNG writer's zip / deflate).
letters.elf: Apps/letters/main.cpp $(wildcard Apps/letters/*.h) Apps/cardfile/model.h Kits/imagekit/img/pngsave.hpp Kits/fontkit/fonts.h lib/printerkit.imp.a $(PRINT_HDR) lib/fontkit.imp.a \
	    lib/imagekit.imp.a Kits/imagekit/imagekit.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h Include/docguard.h Kits/systemkit/clipboard.h
	$(CXX) $(NL_CXXFLAGS) -DPNGSAVE_USE_IMAGEKIT -DUSE_IMAGEKIT -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/imagekit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@
# The spreadsheet: a newlib app as Letters (FreeType, stdio, doubles) -- its engine and files in
# Apps/sheet/*.h, Letters' icons, the PNG writer's zip / deflate for the .xlsx.
sheet.elf: Apps/sheet/main.cpp $(wildcard Apps/sheet/*.h) Apps/letters/icons.h Kits/imagekit/img/pngsave.hpp Kits/fontkit/fonts.h lib/printerkit.imp.a $(PRINT_HDR) lib/fontkit.imp.a \
	   lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h Include/docguard.h Kits/systemkit/clipboard.h
	$(CXX) $(NL_CXXFLAGS) -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@

# Slides, the presentation program (Apps/slides: docs/slides/README.md): a newlib app as Letters (FreeType), its
# slides' layers composited by the GPU (gpucomp), Letters' icons and Sheet's controls (Apps/sheet/ui_base.h), the
# .odp's zip (Kits/imagekit/img/pngsave.hpp), the PDF writer.
slides.elf: Apps/slides/main.cpp $(wildcard Apps/slides/*.h) Apps/sheet/ui_base.h Apps/sheet/core.h Apps/sheet/xml.h Apps/letters/icons.h \
	    Kits/imagekit/img/pngsave.hpp Libs/pdf/pdfwrite.h Kits/fontkit/fonts.h lib/printerkit.imp.a $(PRINT_HDR) lib/fontkit.imp.a lib/imagekit.imp.a Kits/imagekit/imagekit.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Libs/gpucomp/libgpucomp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o \
	    Runtime/user.ld Kits/appkit/appkit.h Include/docguard.h Kits/systemkit/clipboard.h
	$(CXX) $(NL_CXXFLAGS) -DPNGSAVE_USE_IMAGEKIT -DUSE_IMAGEKIT -fno-math-errno -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/imagekit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Libs/gpucomp/libgpucomp.a lib/fontkit.imp.a -lm -o $@

# QBStudio, the IDE for desktop apps in BASIC (Apps/qbstudio: docs/qbstudio/README.md): a newlib app as Letters
# (FreeType's text through uikit, DejaVu Sans Mono for the code), with Onyx BASIC's compiler and VM built in (the
# problems as you type, Make App's .bax).
qbstudio.elf: Apps/qbstudio/main.cpp $(wildcard Apps/qbstudio/*.h) Libs/basic/bascomp.cpp Libs/basic/basvm.cpp Libs/basic/basnum.cpp Libs/basic/basbax.cpp Libs/basic/baskits.cpp Libs/basic/baskits.h \
	      Libs/basic/bas.h Libs/basic/basint.h Libs/basic/basjit.h Libs/basic/basnum.h lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h Kits/systemkit/clipboard.h 
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       Libs/basic/bascomp.cpp Libs/basic/basvm.cpp Libs/basic/basnum.cpp Libs/basic/basbax.cpp Libs/basic/baskits.cpp lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@

# Turtle Quest, the game that teaches programming with a turtle (Apps/turtle): a newlib app as QBStudio (FreeType's text,
# DejaVu Sans Mono for the code, UIKit's CodeEdit), with Onyx BASIC's compiler and VM built in -- the turtle's words a
# dialect of it, the VM's statement hook for the playback (world.h).
turtle.elf: Apps/turtle/main.cpp Apps/turtle/world.h Libs/basic/bascomp.cpp Libs/basic/basvm.cpp Libs/basic/basnum.cpp Libs/basic/basbax.cpp \
	      Libs/basic/bas.h Libs/basic/basint.h Libs/basic/basjit.h Libs/basic/basnum.h lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       Libs/basic/bascomp.cpp Libs/basic/basvm.cpp Libs/basic/basnum.cpp Libs/basic/basbax.cpp lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@

# Courier, the HTTP client (Apps/courier: a Postman for Onyx): a newlib app as Letters (FreeType's text through
# uikit), with mbedTLS for https:// (../tls/onyx_tls.hpp over the kapi sockets; the libraries as user/BinUtils's
# TLS tools link them).
COURIER_MBEDTLS ?= ../third_party/mbedtls-3.6.3
courier.elf: Apps/courier/main.cpp $(wildcard Apps/courier/*.h) Kits/netkit/http.hpp Libs/tls/onyx_tls.hpp Include/json.hpp Kits/systemkit/clipboard.h Include/docguard.h 	     Kits/imagekit/img/imgload.hpp lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -IKits/fontkit -I$(FT_SRC)/include -I$(COURIER_MBEDTLS)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< 	       lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -L$(COURIER_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Mail, the mail client (Apps/mail: docs/mail/README.md): a newlib app as Letters (FreeType's text through uikit), its
# protocols and its HTML renderer in Libs/mail/ (MIT), mbedTLS for TLS (as Courier), the image codecs (uikit's imgload).
MAIL_MBEDTLS ?= ../third_party/mbedtls-3.6.3
mail.elf: Apps/mail/main.cpp $(wildcard Apps/mail/*.h) $(wildcard Libs/mail/*.h) Apps/cardfile/model.h Libs/tls/onyx_tls.hpp Kits/systemkit/clipboard.h Kits/systemkit/fileassoc.h Kits/systemkit/applet_proto.h Apps/jet/webview_proto.h \
	  Kits/imagekit/img/imgload.hpp lib/fontkit.imp.a Kits/fontkit/fonts.h Kits/fontkit/uikitface.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -Wno-format-truncation -IKits/fontkit -I$(FT_SRC)/include -I$(MAIL_MBEDTLS)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -L$(MAIL_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Telegram, the instant messenger in the way of Windows Live Messenger (Apps/telegram: docs/04 "Telegram"): a newlib app as
# Mail (FreeType's text through uikit), its own MTProto 2.0 (tl.h, mtproto.h, client.h) on mbedTLS's cryptography, the API's
# schema compiled in (schema.inc: tools/telegram/mkschema.py), FileKit's inflate (gzip_packed), uikit's image codecs, ImageKit's
# JPEG writer (the pictures sent).
TG_MBEDTLS ?= ../third_party/mbedtls-3.6.3
telegram.elf: Apps/telegram/main.cpp $(wildcard Apps/telegram/*.h) Apps/telegram/schema.inc Kits/imagekit/img/imgload.hpp Kits/imagekit/img/pngsave.hpp lib/fontkit.imp.a Kits/fontkit/uikitface.h \
	      lib/imagekit.imp.a Kits/imagekit/imagekit.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -DPNGSAVE_USE_IMAGEKIT -Wno-format-truncation -IKits/fontkit -I$(FT_SRC)/include -I$(TG_MBEDTLS)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/imagekit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -L$(TG_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Photos, the photo library (Apps/photos: docs/photos/README.md): a newlib app as Letters (FreeType's text through
# uikit), the image codecs (uikit's imgload), the JPEG / PNG writers (Kits/imagekit/img/pngsave.hpp), the PDF writer (Libs/pdf/pdfwrite.h).
photos.elf: Apps/photos/main.cpp $(wildcard Apps/photos/*.h) Kits/imagekit/img/imgload.hpp Kits/imagekit/img/pngsave.hpp Libs/pdf/pdfwrite.h Kits/systemkit/clipboard.h Kits/systemkit/trash.h Kits/systemkit/wallpaper.h \
	  lib/fontkit.imp.a Kits/fontkit/fonts.h Kits/fontkit/uikitface.h lib/printerkit.imp.a Kits/printerkit/printerkit.h lib/imagekit.imp.a Kits/imagekit/imagekit.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -DPNGSAVE_USE_IMAGEKIT -DUSE_IMAGEKIT -Wno-format-truncation -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/imagekit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@

# Archiver, the archive manager (Apps/archiver: docs/archiver/README.md): a newlib app as Letters (FreeType's text
# through uikit), its engine and widgets headers included by main.cpp; zlib (Deflate) built from third_party into
# Libs/zlib/libz.a.
ZLIB_SRC = ../third_party/zlib-1.3.1
ZLIB_OBJ = $(addprefix Libs/zlib/,adler32.o crc32.o deflate.o inflate.o inffast.o inftrees.o trees.o zutil.o)
Libs/zlib/%.o: $(ZLIB_SRC)/%.c
	@mkdir -p Libs/zlib
	$(CC) $(NL_CFLAGS) -w -c $< -o $@
Libs/zlib/libz.a: $(ZLIB_OBJ)
	$(AR) rcs $@ $^
archiver.elf: Apps/archiver/main.cpp $(wildcard Apps/archiver/*.h) Kits/filekit/filekit.h Kits/filekit/arcbase.h Kits/filekit/arcpath.h Kits/systemkit/fileassoc.h \
	      lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -IApps/archiver -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/uikit.imp.a lib/fontkit.imp.a -lm -o $@

# The shared clipboard (docs/clipboard/README.md): clipd, its service (no window, newlib), and clipboard, its widget
# (the dock's button; FreeType's text). The apps reach clipd through Kits/systemkit/clipboard.h (Kits/systemkit/clipproto.h).
clipd.elf: Apps/clipd/main.cpp Apps/clipd/ring.h Kits/systemkit/clipproto.h Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h lib/systemkit.imp.a
	$(CXX) $(NL_CXXFLAGS) -IApps/clipd $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< lib/systemkit.imp.a -o $@
# The print service (docs/03 "Printing"): printd, no window, newlib -- the queue, the printers, a job's pages
# rendered (Kits/printerkit/raster.h: FreeType; the images' codecs: uikit.so's) and sent to a network printer (Kits/printerkit/ipp.h).
printd.elf: Apps/printd/main.cpp $(PRINT_HDR) lib/fontkit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -Wno-format-truncation -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< 	       lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@
clipboard.elf: Apps/clipboard/main.cpp Kits/systemkit/clipboard.h Kits/systemkit/clipproto.h lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a \
	       Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@

# clockd (AutoDev round 6): the Clock's alarm service -- no window, newlib, as clipd; the Clock's core (the alarms
# model, the Ringer, the wall time: Apps/clock/alarms.cpp, clocktime.cpp) compiled in; FileKit (alarms.txt) and
# SystemKit (locale_zone_sync, notify_action) by their import libraries.
clockd.elf: Apps/clockd/main.cpp Apps/clock/alarms.cpp Apps/clock/clocktime.cpp Apps/clock/alarms.h Apps/clock/clocktime.h Apps/clock/clock_proto.h \
	    Kits/filekit/kvtext.h Kits/systemkit/locale.h lib/filekit.imp.a lib/systemkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< Apps/clock/alarms.cpp Apps/clock/clocktime.cpp lib/filekit.imp.a lib/systemkit.imp.a -o $@
# The packages (docs/pkg/README.md): pkgman, the Package Manager (the Control Panel's applet), and pkgd, the
# update daemon (no window) -- on Libs/pkg/pkglib.h as BinUtils/pkg: HTTPS (mbedTLS), the Archiver's ZIP engine, zlib.
PKG_MBEDTLS ?= ../third_party/mbedtls-3.6.3
PKG_DEPS = Libs/pkg/pkglib.h Libs/pkg/pkgjob.h Kits/netkit/http.hpp Libs/tls/onyx_tls.hpp $(wildcard Kits/filekit/*.h) Libs/zlib/libz.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
pkgman.elf: Apps/pkgman/main.cpp $(PKG_DEPS) Kits/uikit/bmp.h lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a
	$(CXX) $(NL_CXXFLAGS) -DPKG_NET -DONYX_HTTP_TLS -Wno-format-truncation -IKits/fontkit -I$(FT_SRC)/include -I$(ZLIB_SRC) -I$(PKG_MBEDTLS)/include \
	       $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a Libs/zlib/libz.a \
	       -L$(PKG_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@
pkgd.elf: Apps/pkgd/main.cpp $(PKG_DEPS) 
	$(CXX) $(NL_CXXFLAGS) -DPKG_NET -DONYX_HTTP_TLS -Wno-format-truncation -I$(ZLIB_SRC) -I$(PKG_MBEDTLS)/include \
	       $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< lib/systemkit.imp.a Libs/zlib/libz.a \
	       -L$(PKG_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

# Screenshot, the screen capture tool (Apps/screenshot: docs/screenshot/README.md): a newlib app (FreeType's text
# through uikit), the PNG / JPEG / BMP writers of Kits/imagekit/img/pngsave.hpp, the clipboard (clipd) and notifyd.
# Paint: a newlib app (FreeType's text, its Text tool), its canvas composited by the GPU (gpucomp: the
# layers as textures, their blend modes); its pieces in Apps/paint/*.h.
paint.elf: Apps/paint/main.cpp $(wildcard Apps/paint/*.h) Kits/systemkit/clipboard.h Include/docguard.h Kits/imagekit/img/imgload.hpp Kits/imagekit/img/pngsave.hpp lib/fontkit.imp.a \
	   Kits/fontkit/uikitface.h lib/printerkit.imp.a Kits/printerkit/printerkit.h lib/imagekit.imp.a Kits/imagekit/imagekit.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Libs/gpucomp/libgpucomp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -DPNGSAVE_USE_IMAGEKIT -DUSE_IMAGEKIT -fno-math-errno -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/imagekit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Libs/gpucomp/libgpucomp.a lib/fontkit.imp.a -lm -o $@
screenshot.elf: Apps/screenshot/main.cpp Kits/imagekit/img/pngsave.hpp Kits/systemkit/clipboard.h Kits/systemkit/clipproto.h lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/imagekit.imp.a Kits/imagekit/imagekit.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a \
		Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -DPNGSAVE_USE_IMAGEKIT -DUSE_IMAGEKIT -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/imagekit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@

# Koton, the studio (Apps/koton: docs/daw/README.md): a newlib app as Letters (FreeType text through uikit's
# text face, floats everywhere). Its engine (the song model, the harmony, the generators, the compiler,
# the audio engine) and MeltySynth are separate objects; the UI is headers included by main.cpp.
# (its synthesizer, Apps/koton/synth, is in the shared library AudioKit: lib/audiokit.imp.a)
KOTON_SRC = $(wildcard Apps/koton/engine/*.cpp) $(wildcard Apps/koton/plug/*.cpp)
KOTON_OBJ = $(patsubst Apps/koton/%.cpp,Apps/koton/obj/%.o,$(KOTON_SRC))
KOTON_HDR = $(wildcard Apps/koton/engine/*.h) $(wildcard Apps/koton/synth/*.h) $(wildcard Apps/koton/plug/*.h) Apps/koton/plug/kplug_proto.h Include/json.hpp Kits/appkit/appkit.h
Apps/koton/obj/%.o: Apps/koton/%.cpp $(KOTON_HDR)
	@mkdir -p $(dir $@)
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -IApps/koton -c $< -o $@
koton.elf: Apps/koton/main.cpp $(wildcard Apps/koton/ui/*.h) $(KOTON_OBJ) lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/audiokit.imp.a \
	   Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld $(KOTON_HDR)
	$(CXX) $(NL_CXXFLAGS) -fno-math-errno -IApps/koton -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       $(KOTON_OBJ) lib/audiokit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@

# PDF Viewer (Apps/pdf: docs/pdf/README.md): a newlib app as Letters, on MuPDF -- its fitz + pdf parts, jbig2dec, openjpeg,
# zlib, libjpeg and a FreeType with the PDF fonts' drivers, built by Apps/pdf/mupdf.mk into Apps/pdf/obj/libmupdf.a (that
# FreeType serves the app's own text too: no lib/fontkit.imp.a). The app is AGPL-3.0, MuPDF's licence (docs/LICENSING.md).
MU_NL_CFLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -ffunction-sections -fdata-sections
Apps/pdf/obj/libmupdf.a: Apps/pdf/mupdf.mk $(wildcard Apps/pdf/mu/*)
	@$(MAKE) --no-print-directory -f Apps/pdf/mupdf.mk MU_ROOT=.. MU_ONYX=1 MU_CC=$(CC) MU_AR=$(AR) MU_OUT=Apps/pdf/obj MU_CFLAGS="$(MU_NL_CFLAGS)"
pdf.elf: Apps/pdf/main.cpp $(wildcard Apps/pdf/*.h) Apps/pdf/obj/libmupdf.a Kits/systemkit/clipboard.h Include/docguard.h Kits/fontkit/uikitface.h \
	 lib/printerkit.imp.a Kits/printerkit/printerkit.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -IKits/fontkit -I$(FT_SRC)/include -I../third_party/mupdf-1.28.5/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/printerkit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Apps/pdf/obj/libmupdf.a -lm -o $@

# Media Player, the music and video library (Apps/media: docs/media/README.md): a newlib app (FreeType's text through
# uikit), its music decoders from third_party (minimp3, stb_vorbis, dr_flac, dr_wav: Apps/media/codecs.c, vorbis.c, built
# with the FPU), Koton's MeltySynth for the MIDI files (Koton's objects, shared), and for the videos Onyx's media library
# (user/Libs/av, compiled here into Apps/media/obj/av/) with its codecs -- libvpx, dav1d, libopus
# (built by `make -C Libs/av`; their sources and flags: Libs/av/codecs.mk) -- and FFmpeg 7.1 (every decoder and
# demuxer: H.264, H.265, AAC, AVI, MPEG-TS...; GPL-2.0+: the Media Player is distributed under the GPL-2.0), its
# libraries built by third_party/ffmpeg-7.1.2/onyx/build.sh pi (committed).
# (Its music decoders -- codecs.c, vorbis.c -- and the MIDI synthesizer are in the shared library AudioKit:
# lib/audiokit.imp.a; decode.h and midi.h call them through it.)
TP := ../third_party
AV_ARCH := aarch64
include Libs/av/codecs.mk
MEDIA_AV_OBJ = $(patsubst Libs/av/%.c,Apps/media/obj/av/%.o,$(wildcard Libs/av/*.c))
FFMPEG = ../third_party/ffmpeg-7.1.2/onyx
MEDIA_AV_LIBS = $(VPX)/libvpx.a $(DAV1D)/libdav1d.a $(OPUS)/libopus.a \
	$(FFMPEG)/aarch64/libavformat.a $(FFMPEG)/aarch64/libavcodec.a $(FFMPEG)/aarch64/libswscale.a \
	$(FFMPEG)/aarch64/libswresample.a $(FFMPEG)/aarch64/libavutil.a
Apps/media/obj/av/%.o: Libs/av/%.c $(wildcard Libs/av/*.h) Kits/appkit/appkit.h
	@mkdir -p $(dir $@)
	$(CC) $(NL_CFLAGS) -std=gnu11 -ILibs/av -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts $(AV_CODECS_CF) -DAV_WITH_FFMPEG -I$(FFMPEG)/include -c $< -o $@
media.elf: Apps/media/main.cpp $(wildcard Apps/media/*.h) $(MEDIA_AV_OBJ) lib/audiokit.imp.a Kits/imagekit/img/imgload.hpp Kits/imagekit/img/pngsave.hpp Kits/systemkit/fileassoc.h \
	   lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/imagekit.imp.a Kits/imagekit/imagekit.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h $(wildcard Libs/av/*.h)
	$(CXX) $(NL_CXXFLAGS) -DPNGSAVE_USE_IMAGEKIT -DUSE_IMAGEKIT -fno-math-errno -I../third_party -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       $(MEDIA_AV_OBJ) lib/audiokit.imp.a lib/imagekit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a $(MEDIA_AV_LIBS) -lm -o $@

# Koton's plugins (user/Apps/koton/plug/kplug.h, the protocol user/Apps/koton/plug/kplug_proto.h; the host: Apps/koton/plug): each
# Apps/kp_<name>/main.cpp -> Apps/kp_<name>/kp_<name>.elf, a newlib app with the FPU. Built next to
# its source, NOT as user/*.elf (make stage makes
# an SD:/apps/<name>.app of each of those: the dock would list them): `make stage` copies them to
# SD:/koton/plugins/<name>/main with their plugin.json.
KPLUG_DIRS = $(wildcard Apps/kp_*)
KPLUG_ELFS = $(foreach d,$(KPLUG_DIRS),$(d)/$(notdir $(d)).elf)
KPLUG_HDR = Apps/koton/plug/kplug.h Apps/koton/plug/kplug_proto.h Kits/systemkit/applet_proto.h Include/json.hpp Kits/appkit/appkit.h Apps/koton/engine/kbase.h
define KPLUG_RULE
$(1)/$(notdir $(1)).elf: $(1)/main.cpp $$(KPLUG_HDR) lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld
	$$(CXX) $$(NL_CXXFLAGS) -fno-math-errno -Wall $$(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $$< lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a -lm -o $$@
endef
$(foreach d,$(KPLUG_DIRS),$(eval $(call KPLUG_RULE,$(d))))
.PHONY: plugins
plugins: $(KPLUG_ELFS)

# The Game Boy / Color emulator core (Emulators/gb/: integer only, no libc), for gbemu and the library.
Emulators/gb/gb.o: Emulators/gb/gb.cpp Emulators/gb/gb.h
	$(CXX) $(CXXFLAGS) -O2 -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
Emulators/gb/libgb.a: Emulators/gb/gb.o
	$(AR) rcs $@ $^

# The Game Boy Advance emulator core (Emulators/gba/: integer only, no libc), for gbaemu and the library.
GBA_OBJS = Emulators/gba/gba.o Emulators/gba/gba_cpu.o Emulators/gba/gba_bios.o Emulators/gba/gba_ppu.o Emulators/gba/gba_apu.o
Emulators/gba/%.o: Emulators/gba/%.cpp Emulators/gba/gba.h
	$(CXX) $(CXXFLAGS) -O2 -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
Emulators/gba/libgba.a: $(GBA_OBJS)
	$(AR) rcs $@ $^

# The NES / Famicom emulator core (Emulators/nes/: integer only, no libc), for nesemu and the library.
NES_OBJS = Emulators/nes/nes.o Emulators/nes/nes_ppu.o Emulators/nes/nes_apu.o
Emulators/nes/%.o: Emulators/nes/%.cpp Emulators/nes/nes.h Emulators/nes/nes_optable.inc Emulators/nes/nes_mix.inc
	$(CXX) $(CXXFLAGS) -O2 -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
Emulators/nes/libnes.a: $(NES_OBJS)
	$(AR) rcs $@ $^

# The Super Nintendo core (Emulators/snes/: integer only, no libc), for snesemu and the library.
SNES_OBJS = Emulators/snes/snes.o Emulators/snes/snes_cpu.o Emulators/snes/snes_ppu.o Emulators/snes/snes_apu.o
Emulators/snes/%.o: Emulators/snes/%.cpp Emulators/snes/snes.h Emulators/snes/snes_gauss.inc
	$(CXX) $(CXXFLAGS) -O2 -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
# The Nintendo 64 core (Emulators/n64/: the FPU is used: FP/SIMD flags), for n64emu.
N64_OBJS = Emulators/n64/n64_cpu.o Emulators/n64/n64_bus.o Emulators/n64/n64_gfx.o Emulators/n64/n64_audio.o
Emulators/n64/%.o: Emulators/n64/%.cpp Emulators/n64/n64.h
	$(CXX) $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72 -mno-outline-atomics -fno-math-errno -O3 -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
Emulators/n64/libn64.a: $(N64_OBJS)
	$(AR) rcs $@ $^
# The GameCube core (Emulators/gc/: the FPU is used, as Emulators/n64/), for gcemu.
GC_OBJS = Emulators/gc/gc_cpu.o Emulators/gc/gc_mem.o Emulators/gc/gc_hw.o Emulators/gc/gc_gx.o Emulators/gc/gc_gxdraw.o Emulators/gc/gc_dsp.o Emulators/gc/gc_boot.o Emulators/gc/gc_jit.o Emulators/gc/gc_card.o Emulators/gc/gc_zelda.o Emulators/gc/gc_gxgpu.o
Emulators/gc/%.o: Emulators/gc/%.cpp Emulators/gc/gc.h
	$(CXX) $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72 -mno-outline-atomics -fno-math-errno -O3 -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
Emulators/gc/libgc.a: $(GC_OBJS)
	$(AR) rcs $@ $^
# The V3D QPU builder (Libs/v3d/: shaders generated at run time, e.g. the GameCube's TEV): the C++
# builder + Mesa's QPU packer (tools/qpu/mesa, MIT) + the few libc functions it calls.
V3D_MESA = ../tools/qpu/mesa
V3D_OBJS = Libs/v3d/qpu.o Libs/v3d/shaders.o Libs/v3d/gxtev.o Libs/v3d/qpu_pack.o Libs/v3d/qpu_instr.o Libs/v3d/shim.o
Libs/v3d/qpu.o: Libs/v3d/qpu.cpp Libs/v3d/qpu.h
	$(CXX) $(CXXFLAGS) -O2 -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -I$(V3D_MESA) -c $< -o $@
Libs/v3d/shaders.o: Libs/v3d/shaders.cpp Libs/v3d/shaders.h Libs/v3d/qpu.h
	$(CXX) $(CXXFLAGS_NEON) -O2 -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -I$(V3D_MESA) -c $< -o $@
Libs/v3d/gxtev.o: Libs/v3d/gxtev.cpp Libs/v3d/gxtev.h Libs/v3d/qpu.h
	$(CXX) $(CXXFLAGS_NEON) -O2 -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -I$(V3D_MESA) -c $< -o $@
Libs/v3d/qpu_pack.o: $(V3D_MESA)/broadcom/qpu/qpu_pack.c
	$(CC) $(CFLAGS) -std=gnu11 -DNDEBUG -w -I$(V3D_MESA) -c $< -o $@
Libs/v3d/qpu_instr.o: $(V3D_MESA)/broadcom/qpu/qpu_instr.c
	$(CC) $(CFLAGS) -std=gnu11 -DNDEBUG -w -I$(V3D_MESA) -c $< -o $@
Libs/v3d/shim.o: Libs/v3d/shim.c
	$(CC) $(CFLAGS) -c $< -o $@
Libs/v3d/libv3d.a: $(V3D_OBJS)
	$(AR) rcs $@ $^
# The GPU compositing service (Libs/gpucomp/: layers composited by the V3D, kapi v70, or by the CPU):
# pure C without libc, FP/SIMD on and -O3 (the CPU path's row loops become NEON code), no fused
# multiply-adds (-ffp-contract=off: the same pixels as the PC's build, tools/tests/run_gpucomp_test.sh).
# Linked by freestanding apps (gpcdemo) and newlib ones alike.
Libs/gpucomp/gpucomp.o: Libs/gpucomp/gpucomp.c Libs/gpucomp/gpucomp.h Kits/appkit/appkit.h ../kernel/include/kern/kapi_abi.h
	$(CC) $(filter-out -mgeneral-regs-only -O2,$(CFLAGS)) -mcpu=cortex-a72 -O3 -ffp-contract=off -fno-math-errno -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
Libs/gpucomp/libgpucomp.a: Libs/gpucomp/gpucomp.o
	$(AR) rcs $@ $^
# Manifold (Libs/manifold/libmanifold.a: boolean operations on closed meshes, extrusion, 2D outlines -- 3DForge's
# kernel, docs/3dforge/README.md): third_party/manifold-3.5.4 with Clipper2 (third_party/clipper2-46f6391), a newlib
# build with the FPU, sequential (no TBB), no iostream. Libs/manifold/onyx_manifold.h goes before every file (the
# empty std::mutex this libstdc++ lacks); a program compiles with $(MF_INC) too. Test: tools/tests/run_manifold_test.sh.
MF_SRC = ../third_party/manifold-3.5.4
CLIPPER_SRC = ../third_party/clipper2-46f6391/CPP/Clipper2Lib
MF_INC = -std=c++17 -include Libs/manifold/onyx_manifold.h -DMANIFOLD_PAR=-1 -DMANIFOLD_CROSS_SECTION -DMANIFOLD_NO_IOSTREAM \
	 -DMANIFOLD_NO_FILESYSTEM -DCLIPPER2_NO_IOSTREAM -I$(MF_SRC)/include -I$(CLIPPER_SRC)/include
MF_OBJ = $(addprefix Libs/manifold/obj/,boolean3.o boolean_result.o constructors.o csg_tree.o edge_op.o execution_impl.o \
	 face_op.o impl.o manifold.o minkowski.o polygon.o properties.o quickhull.o sdf.o smoothing.o sort.o subdivision.o \
	 tree2d.o cross_section.o clipper.engine.o clipper.offset.o clipper.rectclip.o clipper.triangulation.o)
MF_CXX = @mkdir -p Libs/manifold/obj; $(CXX) $(NL_CXXFLAGS) $(MF_INC) -w -c $< -o $@
Libs/manifold/obj/%.o: $(MF_SRC)/src/%.cpp Libs/manifold/onyx_manifold.h
	$(MF_CXX)
Libs/manifold/obj/%.o: $(MF_SRC)/src/cross_section/%.cpp Libs/manifold/onyx_manifold.h
	$(MF_CXX)
Libs/manifold/obj/%.o: $(CLIPPER_SRC)/src/%.cpp Libs/manifold/onyx_manifold.h
	$(MF_CXX)
Libs/manifold/libmanifold.a: $(MF_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^
# 3DForge, the small parametric CAD (Apps/3dforge: docs/3dforge/README.md): a newlib app as Paint (FreeType's text
# through uikit), its geometry Manifold's, its view drawn by the GPU (kapi_gpu_render); its pieces in Apps/3dforge/*.h.
# libstdc++'s operator new (ONYX_HOSTED_NEW, in main.cpp): the program and Manifold are full of std::vector.
# Its symbols are stripped (Manifold's templates make 2 MB of names) but libgcc's SME helpers', which tools/el0scan.sh
# knows by their names.
3dforge.elf: Apps/3dforge/main.cpp $(wildcard Apps/3dforge/*.h) Libs/manifold/libmanifold.a Libs/manifold/onyx_manifold.h Include/docguard.h Libs/pdf/pdfwrite.h Kits/imagekit/img/pngsave.hpp lib/fontkit.imp.a 	     Kits/fontkit/uikitface.h lib/imagekit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) $(MF_INC) -DPNGSAVE_USE_IMAGEKIT -DUSE_IMAGEKIT -fno-math-errno -IApps/3dforge -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< 	       Libs/manifold/libmanifold.a lib/imagekit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lstdc++ -lm -o $@
	$(PREFIX)strip -K __arm_za_disable -K __arm_tpidr2_save -K __arm_tpidr2_restore -K __arm_sme_state -K __arm_get_current_vg $@

Emulators/snes/libsnes.a: $(SNES_OBJS)
	$(AR) rcs $@ $^

# Onyx BASIC (Libs/basic/): the core (compiler + VM, doubles -> FP/SIMD, no libc) as a library
# (linked into the qbasic editor too), and the runtime /bin/basic (Libs/basic/runtime.cpp).
BAS_HDR = Libs/basic/bas.h Libs/basic/basint.h Libs/basic/basjit.h Libs/basic/basnum.h Libs/basic/bas3d.h Libs/basic/bas3dscene.h Libs/basic/basscreen.h Libs/basic/baskits.h Kits/appkit/appkit.h
# (baskits: the kits a BASIC program imports -- #import --, read and opened on Onyx)
BAS_OBJ = Libs/basic/basnum.o Libs/basic/bascomp.o Libs/basic/basvm.o Libs/basic/basbax.o Libs/basic/baskits.o
CXXFLAGS_FP = $(CXXFLAGS_NEON) -fno-math-errno
# The graphing calculator computes in double: FP/SIMD for the whole app.
graphcalc.elf: CXXFLAGS := $(CXXFLAGS_FP)
# The teapot demo computes its geometry in float: FP/SIMD too.
teapot.elf: CXXFLAGS := $(CXXFLAGS_FP)
teapot.elf: Apps/teapot/teapot.h Apps/teapot/teapot_data.inc
gpudemo.elf: CXXFLAGS := $(CXXFLAGS_FP)
n64emu.elf: CXXFLAGS := $(CXXFLAGS_FP)
gcemu.elf: CXXFLAGS := $(CXXFLAGS_FP)
gcemu.elf: Apps/gcemu/gxv3d.h Libs/v3d/gxtev.h Libs/v3d/shaders.h
graphcalc.elf: Apps/graphcalc/expr.h
cardfile.elf: $(wildcard Apps/cardfile/*.h) Kits/systemkit/clipboard.h Include/docguard.h
# The Calendar: a newlib app as Letters (FreeType's text through uikit's face); its model (iCalendar), views
# and dialogs in Apps/calendar/*.h.
calendar.elf: Apps/calendar/main.cpp $(wildcard Apps/calendar/*.h) lib/fontkit.imp.a Kits/fontkit/uikitface.h \
	      lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< \
	       lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@
agenda.elf: 
setup.elf: Apps/setup/system.h Kits/systemkit/wallpaper.h Kits/imagekit/img/imgload.hpp
# The FreeType apps (FT_APPS): newlib + lib/fontkit.imp.a, as the Calendar. The Game Library also links the
# emulators it takes the covers and the save states' pictures from.
# (consolehome's settings pages, Apps/consolehome/xset.h: the Packages page is the package manager's library -- HTTPS,
# zlib --, the Sound page's chime AudioKit's)
FT_EXTRA_consolehome = lib/gamekit.imp.a lib/audiokit.imp.a Libs/zlib/libz.a -L$(PKG_MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto
consolehome.elf: NL_CXXFLAGS += -DPKG_NET -DONYX_HTTP_TLS -Wno-format-truncation -I$(ZLIB_SRC) -I$(PKG_MBEDTLS)/include
consolehome.elf: lib/gamekit.imp.a lib/audiokit.imp.a Kits/gamekit/gamekit.h Apps/consolehome/xmb.h Apps/consolehome/xset.h Apps/pocketshell/catalog.h Apps/pocketshell/look.h $(PKG_DEPS) Include/gamepad.h
FT_EXTRA_gamelib = lib/gamekit.imp.a Emulators/gb/libgb.a Emulators/gba/libgba.a Emulators/nes/libnes.a Emulators/snes/libsnes.a
FT_EXTRA_fmtracker = lib/audiokit.imp.a
FT_EXTRA_soundconf = lib/audiokit.imp.a
# (GPIO Lab's Code view: Onyx BASIC's compiler and VM built in, as Turtle Quest's)
FT_EXTRA_gpiolab = lib/gpiokit.imp.a Libs/basic/bascomp.cpp Libs/basic/basvm.cpp Libs/basic/basnum.cpp Libs/basic/basbax.cpp
gpiolab.elf: lib/gpiokit.imp.a Kits/gpiokit/gpiokit.h Libs/basic/bascomp.cpp Libs/basic/basvm.cpp Libs/basic/basnum.cpp Libs/basic/basbax.cpp \
	     Libs/basic/bas.h Libs/basic/basint.h Libs/basic/basjit.h Libs/basic/basnum.h
soundconf.elf: lib/audiokit.imp.a Kits/audiokit/audiokit.h
fmtracker.elf: lib/audiokit.imp.a Kits/audiokit/audiokit.h
# Notes (AutoDev round 1): its model -- SD:/Notes, notes.ini, config.ini -- shared with Stickies, its list beside it
FT_EXTRA_notes = Apps/notes/notesmodel.cpp
notes.elf: Apps/notes/notesmodel.cpp $(wildcard Apps/notes/*.h) Include/docguard.h Kits/uikit/toolbar.h
# Stickies (AutoDev round 1): the pinned notes on the desktop -- it reads them through Notes' model
FT_EXTRA_stickies = Apps/notes/notesmodel.cpp
stickies.elf: Apps/notes/notesmodel.cpp Apps/notes/notesmodel.h Apps/notes/stickies_proto.h Kits/uikit/toolbar.h
# Circuits (AutoDev round 2): its engine (the board, the evaluation, the packs, the progress) beside the window;
# gates.h, board.h, views.h, lessons.h are the window's own headers (one translation unit with main.cpp)
FT_EXTRA_circuits = Apps/circuits/circuit.cpp
circuits.elf: Apps/circuits/circuit.cpp $(wildcard Apps/circuits/*.h) Kits/filekit/kvtext.h Kits/filekit/kvtext.inc Kits/uikit/toolbar.h
# Pinball (AutoDev round 4): its core (the table loader, the physics, the rules, the scores) beside the window; draw.h,
# panel.h, picker.h are the window's own headers. The same floating-point results as the host test
# (tools/tests/run_pinball_test.sh): no fused multiply-add. Its sounds: game.h's sfx, AudioKit's voices.
FT_EXTRA_pinball = Apps/pinball/table.cpp Apps/pinball/physics.cpp Apps/pinball/rules.cpp Apps/pinball/scores.cpp lib/audiokit.imp.a
pinball.elf: Apps/pinball/table.cpp Apps/pinball/physics.cpp Apps/pinball/rules.cpp Apps/pinball/scores.cpp $(wildcard Apps/pinball/*.h) \
	     Apps/games/game.h Include/gamepad.h lib/audiokit.imp.a Kits/audiokit/audiokit.h Kits/filekit/kvtext.h Kits/filekit/kvtext.inc Kits/uikit/toolbar.h
pinball.elf: NL_CXXFLAGS += -ffp-contract=off
# Critters (AutoDev round 5): its core (the terrain, the levels, the world, the solutions, the progress -- integers
# only, tools/tests/run_critters_test.sh) beside the window; draw.h, bar.h, picker.h are the window's own headers.
# Its sounds: game.h's sfx, AudioKit's voices.
FT_EXTRA_critters = Apps/critters/terrain.cpp Apps/critters/level.cpp Apps/critters/world.cpp Apps/critters/solution.cpp Apps/critters/progress.cpp lib/audiokit.imp.a
critters.elf: Apps/critters/terrain.cpp Apps/critters/level.cpp Apps/critters/world.cpp Apps/critters/solution.cpp Apps/critters/progress.cpp \
	      $(wildcard Apps/critters/*.h) Apps/games/game.h lib/audiokit.imp.a Kits/audiokit/audiokit.h Kits/filekit/kvtext.h Kits/filekit/kvtext.inc Kits/uikit/toolbar.h Kits/uikit/lcd.h
# Clock (AutoDev round 6): its core beside the window, shared with clockd (the alarms model and the Ringer, the wall
# time, the world rows, the timer and the stopwatch); ui.h, world.h, alarmsview.h, timerview.h, swview.h, ring.h,
# sounds.h are the window's own headers (one translation unit with main.cpp). Its sounds: AudioKit's FM voices.
FT_EXTRA_clock = Apps/clock/alarms.cpp Apps/clock/clocktime.cpp lib/audiokit.imp.a
clock.elf: Apps/clock/alarms.cpp Apps/clock/clocktime.cpp $(wildcard Apps/clock/*.h) lib/audiokit.imp.a Kits/audiokit/audiokit.h \
	   Kits/filekit/kvtext.h Kits/filekit/kvtext.inc Kits/systemkit/locale.h Kits/uikit/toolbar.h
# The menu bar's bell (AutoDev round 6, S1): an alarm of the Clock within 24 hours -- the Clock's model read.
FT_EXTRA_menubar = Apps/clock/alarms.cpp Apps/clock/clocktime.cpp
menubar.elf: Apps/clock/alarms.cpp Apps/clock/clocktime.cpp Apps/clock/alarms.h Apps/clock/clocktime.h Kits/filekit/kvtext.h
FT_EXTRA_printconf = lib/printerkit.imp.a
printconf.elf: lib/printerkit.imp.a Kits/printerkit/printerkit.h
$(FT_APP_ELFS): %.elf: Apps/%/main.cpp Kits/uikit/bmp.h Runtime/umm.h Runtime/onyxpp.hpp lib/fontkit.imp.a Kits/fontkit/uikitface.h lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a 		       Emulators/gb/libgb.a Emulators/gba/libgba.a Emulators/nes/libnes.a Emulators/snes/libsnes.a Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o Runtime/user.ld Kits/appkit/appkit.h
	$(CXX) $(NL_CXXFLAGS) -IKits/fontkit -I$(FT_SRC)/include $(NL_LDFLAGS) Runtime/libc/crt0libc.o Runtime/libc/onyx_syscalls.o $< 	       $(FT_EXTRA_$*) lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/fontkit.imp.a -lm -o $@
# Ledger: its books, pages and printing in Apps/ledger/*.h (Cardfile's model and widgets: the merge's data;
# the reports' .xlsx: the PNG writer's zip / deflate).
ledger.elf: $(wildcard Apps/ledger/*.h) $(wildcard Apps/cardfile/*.h) Kits/systemkit/clipboard.h Include/docguard.h Kits/imagekit/img/pngsave.hpp
Libs/basic/%.o: Libs/basic/%.cpp $(BAS_HDR)
	$(CXX) $(CXXFLAGS_FP) -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts -c $< -o $@
Libs/basic/libbasic.a: $(BAS_OBJ)
	$(AR) rcs $@ $^
BinUtils/basic.elf: Libs/basic/runtime.cpp Libs/basic/libbasic.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a lib/audiokit.imp.a Kits/audiokit/audiokit.h Runtime/crt0.S Runtime/user.ld Kits/appkit/appkit.h $(BAS_HDR)
	$(CXX) $(CXXFLAGS_FP) -I. -IKits -IRuntime -IInclude -ILibs -IEmulators -IPorts $(LDFLAGS) $(KAPI_ALIASES) Runtime/crt0.S Libs/basic/runtime.cpp Libs/basic/libbasic.a lib/audiokit.imp.a lib/uikit.imp.a lib/filekit.imp.a lib/systemkit.imp.a lib/netkit.imp.a -o $@

clean:
	rm -f *.elf *.o Kits/fontkit/*.o Kits/fontkit/*.a Libs/zlib/*.o Libs/zlib/*.a Runtime/libc/*.o Libs/basic/*.o Libs/basic/*.a $(KPLUG_ELFS) Emulators/gb/*.o Emulators/gb/*.a Emulators/gba/*.o Emulators/gba/*.a Emulators/nes/*.o Emulators/nes/*.a Emulators/snes/*.o Emulators/snes/*.a Emulators/n64/*.o Emulators/n64/*.a Emulators/gc/*.o Emulators/gc/*.a Libs/v3d/*.o Libs/v3d/*.a Libs/gpucomp/*.o Libs/gpucomp/*.a BinUtils/basic.elf
	rm -f Kits/uikit/*.o Kits/uikit/*.a
	rm -rf lib
	rm -rf Apps/pdf/obj Apps/koton/obj Apps/media/obj
	@$(MAKE) --no-print-directory -C BinUtils clean
	@$(MAKE) --no-print-directory -C Servers/elegant clean
	@$(MAKE) --no-print-directory -C Servers/pocketui clean
	@$(MAKE) --no-print-directory -C Ports/doom clean
