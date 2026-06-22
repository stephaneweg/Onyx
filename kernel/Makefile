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
	sys/syscall.o \
	arch/aarch64/vectors.o \
	arch/aarch64/exception.o \
	arch/aarch64/user_stub.o

# Our include dirs must come BEFORE Circle's (Rules.mk appends -I$(CIRCLEHOME)/include),
# so <circle/sched/scheduler.h> resolves to our shadow in compat/.
INCLUDE = -I compat -I include

LIBS = \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
