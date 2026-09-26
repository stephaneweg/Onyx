#
# Makefile -- Doom for Onyx: doomgeneric (third_party/doomgeneric, GPL-2.0) + the Onyx
# platform layer (doom_onyx.c, doom_sound.c, doom_wtk.cpp), built against newlib like the
# /bin libc programs (../libc: crt0libc.S + the kapi syscall layer). Output: ../doom.elf,
# staged as SD:/apps/doom.app/main.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
DG      = ../../third_party/doomgeneric
OUT     = build

CFLAGS  = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -nostartfiles \
	  -DONYX -DFEATURE_SOUND -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=640 -DDOOMGENERIC_RESY=400 \
	  -I.. -I../../kernel/include -I$(DG) -w
CXXFLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -nostartfiles \
	  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -I.. -I../../kernel/include
LDFLAGS = -Wl,-T,../user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none -Wl,--gc-sections

DG_SRC = dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net \
	 f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound \
	 i_system i_timer memio m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc \
	 m_random p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats \
	 p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw \
	 r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff s_sound tables \
	 v_video wi_stuff w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric
OBJS = $(addprefix $(OUT)/, $(addsuffix .o, $(DG_SRC))) $(OUT)/doom_onyx.o $(OUT)/doom_sound.o \
       $(OUT)/doom_wtk.o $(OUT)/crt0libc.o $(OUT)/onyx_syscalls.o

all: ../doom.elf

../doom.elf: $(OBJS) ../wtk/libwtk.a ../user.ld
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) ../wtk/libwtk.a -lm -o $@

$(OUT)/%.o: $(DG)/%.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/doom_onyx.o: doom_onyx.c ../kapi.h ../gamepad.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/doom_sound.o: doom_sound.c ../kapi.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/doom_wtk.o: doom_wtk.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/crt0libc.o: ../libc/crt0libc.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/onyx_syscalls.o: ../libc/onyx_syscalls.c ../kapi.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT) ../doom.elf
