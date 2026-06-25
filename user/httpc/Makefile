#
# Makefile -- httpc, the Onyx text web browser. A newlib C++ GUI app: it links the
# reusable HttpClient + the mbedTLS transport (http:// and https://), like /bin/httpsget,
# but draws a windowed UI. Same crt0 split as the other TLS apps (../bin/Makefile): the C
# startup + kapi syscalls are compiled by gcc, then linked with g++ alongside mbedTLS.
#
# Needs the cross-built mbedTLS (>=3.6.3; see ../tls/README.md). Pass MBEDTLS_DIR if it is
# not at ../tls/mbedtls, e.g.  make MBEDTLS_DIR=/path/to/mbedtls
#
PREFIX  ?= aarch64-none-elf-
CC       = $(PREFIX)gcc
CXX      = $(PREFIX)g++
PYTHON  ?= python3
MBEDTLS_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../third_party/mbedtls-3.6.3)

CFLAGS_LIBC = -mcpu=cortex-a72 -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie \
	      -nostartfiles -I.. -I../../kernel/include
CXXFLAGS_LIBC = $(CFLAGS_LIBC) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
LDFLAGS = -Wl,-T,../user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none

DEPS = main.cpp ../http.hpp ../tls/onyx_tls.hpp ../kapi.h \
       ../libc/crt0libc.S ../libc/onyx_syscalls.c ../user.ld

all: main.elf

main.elf: $(DEPS)
	$(CC)  $(CFLAGS_LIBC) -c ../libc/crt0libc.S -o crt0libc.o
	$(CC)  $(CFLAGS_LIBC) -c ../libc/onyx_syscalls.c -o onyx_syscalls.o
	$(CXX) $(CXXFLAGS_LIBC) -I$(MBEDTLS_DIR)/include $(LDFLAGS) \
	  crt0libc.o onyx_syscalls.o main.cpp \
	  -L$(MBEDTLS_DIR)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o main.elf
	@rm -f crt0libc.o onyx_syscalls.o

# Install onto the SD card as a desktop app (apps/httpc.app).
SDCARD ?= ../../sdcard
stage: main.elf
	@mkdir -p $(SDCARD)/apps/httpc.app
	cp main.elf $(SDCARD)/apps/httpc.app/main.elf
	printf '# Onyx application metadata\nname = httpc\ncategory = Internet\n' > $(SDCARD)/apps/httpc.app/app.txt
	-$(PYTHON) gen-icon.py $(SDCARD)/apps/httpc.app/icon.bmp
	@echo "staged httpc -> $(SDCARD)/apps/httpc.app"

clean:
	rm -f main.elf *.o
