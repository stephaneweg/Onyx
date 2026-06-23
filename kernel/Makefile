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
	proc/elf.o \
	sys/syscall.o \
	sys/kapi.o \
	sys/debugcon.o \
	arch/aarch64/vectors.o \
	arch/aarch64/exception.o

# Our include dirs must come BEFORE Circle's (Rules.mk appends -I$(CIRCLEHOME)/include),
# so <circle/sched/scheduler.h> resolves to our shadow in compat/.
INCLUDE = -I compat -I include

LIBS = \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

# Option C: export the kernel's kapi_* symbol addresses so the app ELFs can be
# linked against them (user.ld INCLUDEs this). Then build the apps.
KERNEL_SYMS = ../user/kernel_syms.ld

$(KERNEL_SYMS): $(TARGET).img
	@echo "  SYMS  $@"
	@$(PREFIX64)nm $(TARGET).elf | grep -E ' [TtWw] kapi_' | awk '{print $$3" = 0x"$$1";"}' > $@

.PHONY: apps
apps: $(KERNEL_SYMS)
	@$(MAKE) --no-print-directory -C ../user

# `make` builds the kernel, exports kapi_* symbols, then builds the apps.
all: apps
.DEFAULT_GOAL := apps

# Copy kernel + apps to the SD staging dir TOGETHER. The app ELFs are linked
# against the kernel's kapi_* addresses (kernel_syms.ld), which move on every
# kernel rebuild -- so the .img and the .elf on the card MUST come from the same
# build. Always `make stage` (never copy the kernel alone).
SDCARD = ../sdcard
.PHONY: stage
stage: apps
	@cp $(TARGET).img $(SDCARD)/
	@cp ../user/*.elf $(SDCARD)/
	@echo "  STAGE $(TARGET).img + apps -> $(SDCARD)/"

-include $(DEPS)
