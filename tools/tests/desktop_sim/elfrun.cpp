// elfrun.cpp -- run an Onyx app's own Pi binary (its ELF, newlib and all) on Linux AArch64 (under
// qemu-aarch64), its kapi table the desktop simulator's (fakekapi.cpp, linked in: the table mapped at
// KAPI_TABLE_VA by its static constructor). The ELF's PT_LOAD segments are mapped at their addresses,
// a stack of the app.txt's size gets a guard page below it, then the app's _start runs.
//
//   elfrun APP.elf [STACK_BYTES]        (SIM=... and the other simulator variables as usual)
//
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

int main (int argc, char **argv)
{
	if (argc < 2) { fprintf (stderr, "elfrun APP.elf [STACK_BYTES]\n"); return 2; }
	size_t stack = argc > 2 ? strtoul (argv[2], 0, 0) : (size_t) 4 << 20;
	FILE *f = fopen (argv[1], "rb");
	if (!f) { perror (argv[1]); return 2; }
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *buf = (unsigned char *) malloc (n);
	if (fread (buf, 1, n, f) != (size_t) n) { perror ("read"); return 2; }
	fclose (f);
	Elf64_Ehdr *eh = (Elf64_Ehdr *) buf;
	if (memcmp (eh->e_ident, ELFMAG, SELFMAG) || eh->e_machine != EM_AARCH64) { fprintf (stderr, "not an AArch64 ELF\n"); return 2; }
	Elf64_Phdr *ph = (Elf64_Phdr *) (buf + eh->e_phoff);
	for (int i = 0; i < eh->e_phnum; i++)
	{
		if (ph[i].p_type != PT_LOAD) continue;
		uintptr_t a = ph[i].p_vaddr & ~(uintptr_t) 0xFFFF, e = (ph[i].p_vaddr + ph[i].p_memsz + 0xFFFF) & ~(uintptr_t) 0xFFFF;
		void *m = mmap ((void *) a, e - a, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (m != (void *) a) { fprintf (stderr, "elfrun: cannot map %lx..%lx\n", (unsigned long) a, (unsigned long) e); return 2; }
		memcpy ((void *) ph[i].p_vaddr, buf + ph[i].p_offset, ph[i].p_filesz);
		fprintf (stderr, "elfrun: segment %lx..%lx (file %lx)\n", (unsigned long) ph[i].p_vaddr,
			 (unsigned long) (ph[i].p_vaddr + ph[i].p_memsz), (unsigned long) ph[i].p_filesz);
	}
	__builtin___clear_cache ((char *) 0x200000000, (char *) 0x200000000 + (64 << 20));
	// the stack, a 64 KB guard below it (an overflow faults there instead of going on)
	char *st = (char *) mmap (0, stack + 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (st == MAP_FAILED) { perror ("stack"); return 2; }
	mprotect (st, 65536, PROT_NONE);
	uintptr_t top = ((uintptr_t) st + 65536 + stack) & ~(uintptr_t) 15;
	fprintf (stderr, "elfrun: stack %lx..%lx (%zu KB), entry %lx\n", (unsigned long) (st + 65536), (unsigned long) top,
		 stack >> 10, (unsigned long) eh->e_entry);
	uintptr_t entry = eh->e_entry;
	__asm__ volatile ("mov sp, %0\n\tblr %1" :: "r" (top), "r" (entry) : "memory", "x30");
	return 0;
}
