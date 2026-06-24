#
# Makefile -- /bin console tools. Same EL1 app model as the GUI apps but they read
# stdin / write stdout / exit (no window). Built with the shared crt0.S + user.ld +
# kapi.h from the parent dir; staged to SD:/bin/<name>.elf.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc

CFLAGS  = -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only \
	  -O2 -Wall -Wextra -fno-stack-protector -I.. -I../../kernel/include
LDFLAGS = -Wl,-T,../user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none

PROGS   = ls.elf cat.elf echo.elf grep.elf wc.elf page.elf \
	  rm.elf mkdir.elf touch.elf cp.elf mv.elf ps.elf kill.elf \
	  run.elf keyb.elf cmd.elf kmsg.elf verbose.elf net.elf wget.elf heaptest.elf \
	  init.elf

all: $(PROGS)

%.elf: %.c ../crt0.S ../user.ld ../kapi.h ../applib.h ../httpc.h ../umm.h
	$(CC) $(CFLAGS) $(LDFLAGS) ../crt0.S $< -o $@

clean:
	rm -f *.elf
