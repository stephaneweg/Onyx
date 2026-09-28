# qpuasm: the V3D QPU assembler (host tool). "make" builds it and assembles the kernel's shaders
# (kernel/sys/v3d_shaders.qasm -> kernel/sys/v3d_shaders.inc); the .inc is committed, so the
# kernel build does not need this tool.
CC      ?= gcc
CFLAGS  = -O1 -std=gnu11 -Imesa -w
SRC     = qpuasm.c qpulib.c ralloc_stub.c mesa/broadcom/qpu/qpu_instr.c mesa/broadcom/qpu/qpu_pack.c mesa/broadcom/qpu/qpu_disasm.c
SHADERS = ../../kernel/sys/v3d_shaders

all: qpuasm $(SHADERS).inc

qpuasm: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

$(SHADERS).inc: $(SHADERS).qasm qpuasm
	./qpuasm $< $@

clean:
	rm -f qpuasm
