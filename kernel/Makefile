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
	sys/net.o \
	sys/stream.o \
	sys/debugcon.o \
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
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

# Apps call the kernel through the kapi ABI table at a fixed VA (see
# kern/kapi_abi.h), NOT by linking against kernel addresses -- so they no longer
# depend on the kernel build and need not be rebuilt when the kernel changes.
.PHONY: apps
apps:
	@$(MAKE) --no-print-directory -C ../user

# `make` builds the kernel, then (for convenience) the apps.
all: $(TARGET).img apps
.DEFAULT_GOAL := all

# Copy kernel + apps to the SD staging dir. The app ELFs are ABI-stable now, so a
# kernel-only change just needs the new .img; the .elf's can stay. `make stage`
# still refreshes both for convenience.
#
# Each app lives in its own folder:  apps/<name>.app/main.elf  (the Maynard-style
# layout). The app name is the ELF basename; autostart.txt names them per line.
SDCARD = ../sdcard
.PHONY: stage
stage: apps
	@cp $(TARGET).img $(SDCARD)/
	@for elf in ../user/*.elf; do \
		name=$$(basename $$elf .elf); \
		mkdir -p $(SDCARD)/apps/$$name.app; \
		cp $$elf $(SDCARD)/apps/$$name.app/main.elf; \
	done
	@mkdir -p $(SDCARD)/bin
	@for elf in ../user/bin/*.elf; do \
		[ -e "$$elf" ] || continue; \
		cp $$elf $(SDCARD)/bin/$$(basename $$elf); \
	done
	@echo "  STAGE $(TARGET).img + apps/*.app/main.elf + bin/*.elf -> $(SDCARD)/"

-include $(DEPS)
