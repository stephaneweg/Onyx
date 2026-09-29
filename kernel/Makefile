#
# Makefile -- our multi-process kernel, built on top of Circle.
#
# Uses Circle's Rules.mk (toolchain flags, link rule, circle.ld) and its prebuilt
# libraries, but compiles OUR sources and replaces Circle's scheduler.cpp/task.cpp:
# because our scheduler.o/task.o are listed in OBJS (linked before the archives),
# the linker resolves CScheduler/CTask from them and never pulls libsched.a's
# versions. See ARCHITECTURE.md §9.
#
# Build (from WSL):
#   (cd ../circle && ./configure -r 4 -p aarch64-none-elf- -f && ./makeall --nosample)
#   make
#

CIRCLEHOME = ../circle

OBJS = \
	main.o \
	kernel.o \
	sched/scheduler.o \
	sched/task.o \
	mm/addrspace.o \
	gui/gimage.o \
	gui/window.o \
	gui/surface.o \
	proc/elf.o \
	sys/syscall.o \
	sys/kapi.o \
	sys/kapitable.o \
	sys/ipc.o \
	sys/vfs.o \
	sys/net.o \
	sys/sound.o \
	sys/stream.o \
	sys/debugcon.o \
	sys/fslock.o \
	sys/appcore.o \
	sys/v3d.o \
	sys/crashlog.o \
	sys/thread.o \
	arch/aarch64/vectors.o \
	arch/aarch64/exception.o

# Our include dirs must come BEFORE Circle's (Rules.mk appends -I$(CIRCLEHOME)/include),
# so <circle/sched/scheduler.h> resolves to our shadow in compat/.
INCLUDE = -I compat -I include

# Net stack + WLAN (BCM4343 SDIO) + wpa_supplicant. Order matters for static
# linking: higher-level archives first so the linker can resolve their references
# in the lower-level ones (wpa -> wlan -> net -> circle). Our scheduler.o/task.o in
# OBJS still win over libsched.a (OBJS precede the archives), as before.
LIBS = \
	$(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
	$(CIRCLEHOME)/addon/wlan/libwlan.a \
	$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	$(CIRCLEHOME)/lib/net/libnet.a \
	$(CIRCLEHOME)/lib/sound/libsound.a \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

# Joining another Wi-Fi network without a reboot (kapi v60 wlan_reconnect, sys/net.cpp): the
# reconfiguration handler wpa_supplicant registers for SIGHUP (Circle's eloop has no signals) is
# caught here, then run from the supplicant's own eloop -- no change to hostap (upstream's).
LDFLAGS += --wrap=eloop_register_signal_reconfig

# The V3D packets (kern/v3d_cl.h) use default member initialisers on bit-fields: C++20.
sys/v3d.o: CPPFLAGS += -std=c++20

# Apps call the kernel through the kapi ABI table at a fixed VA (see
# kern/kapi_abi.h), NOT by linking against kernel addresses -- so they no longer
# depend on the kernel build and need not be rebuilt when the kernel changes.
.PHONY: apps
apps:
	@$(MAKE) --no-print-directory -C ../user

# The image + its BSS must end below 0x80000 + KERNEL_MAX_SIZE (Circle: 2 MB): past it the
# BSS runs over the kernel's stacks and the Pi does not boot at all, without a word. Checked
# after every link: an image too big is deleted.
KERNEL_END_MAX = 0x280000
.PHONY: sizecheck
sizecheck: $(TARGET).img
	@end=$$(grep -E '^ +0x[0-9a-f]+ +_end = \.' $(TARGET).map | awk '{print $$1}'); \
	if [ -z "$$end" ] || [ $$(( end )) -gt $$(( $(KERNEL_END_MAX) )) ]; then \
		echo "*** kernel too big: _end = $$end, the limit $(KERNEL_END_MAX) (KERNEL_MAX_SIZE): $(TARGET).img deleted"; \
		rm -f $(TARGET).img; exit 1; \
	fi; \
	echo "kernel: _end = $$end, $$(( ($(KERNEL_END_MAX) - end) / 1024 )) KB below the 2 MB limit"

# `make` builds the kernel, then (for convenience) the apps.
all: $(TARGET).img sizecheck apps
.DEFAULT_GOAL := all

# Copy kernel + apps to the SD staging dir. The app ELFs are ABI-stable now, so a
# kernel-only change just needs the new .img; the programs can stay. `make stage`
# still refreshes both for convenience.
#
# Each app lives in its own folder:  apps/<name>.app/main  (the Maynard-style
# layout). The app name is the ELF basename; autostart.txt names them per line.
# Executables on the card carry NO extension: the build's <name>.elf outputs are
# staged as apps/<name>.app/main and bin/<name>.
SDCARD = ../sdcard
# Apps written in BASIC: compiled on this machine (tools/basc, the same compiler core) to
# apps/<name>.app/main.bax -- "name=source.bas" pairs.
BASIC_APPS = arkanoid=$(SDCARD)/basic/examples/arkanoid.bas planets3d=$(SDCARD)/basic/examples/planets3d.bas
BASC = ../tools/basc/basc
BASC_SRC = ../tools/basc/basc.cpp ../user/basic/bascomp.cpp ../user/basic/basnum.cpp ../user/basic/basbax.cpp ../user/basic/basvm.cpp
$(BASC): $(BASC_SRC) ../user/basic/bas.h ../user/basic/basint.h ../user/basic/bas3d.h ../user/basic/bas3dscene.h
	@g++ -std=c++17 -O1 -I../user -o $@ $(BASC_SRC)
.PHONY: stage
stage: sizecheck apps $(BASC)
	@cp $(TARGET).img $(SDCARD)/
	@for elf in ../user/*.elf; do \
		name=$$(basename $$elf .elf); \
		mkdir -p $(SDCARD)/apps/$$name.app; \
		cp $$elf $(SDCARD)/apps/$$name.app/main; \
	done
	@mkdir -p $(SDCARD)/bin
	@for elf in ../user/bin/*.elf; do \
		[ -e "$$elf" ] || continue; \
		cp $$elf $(SDCARD)/bin/$$(basename $$elf .elf); \
	done
	@for pair in $(BASIC_APPS); do \
		name=$${pair%%=*}; src=$${pair#*=}; \
		mkdir -p $(SDCARD)/apps/$$name.app; rm -f $(SDCARD)/apps/$$name.app/main; \
		$(BASC) $$src $(SDCARD)/apps/$$name.app/main.bax || exit 1; \
	done
	@echo "  STAGE $(TARGET).img + apps/*.app/main + bin/* -> $(SDCARD)/"

-include $(DEPS)
