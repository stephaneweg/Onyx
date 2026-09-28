/*
 * qpulib.h -- the V3D 4.2 QPU assembler as a library (tools/qpu/qpulib.c).
 */
#ifndef QPULIB_H
#define QPULIB_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { QPU_VERTEX = 0, QPU_COORD = 1, QPU_FRAG = 2 };

/* the program (lines "add ; mul [; signals]", '#' comments) -> words in out[max]: how many, or -1
   with the reason (and the line) in err */
int qpu_assemble (const char *text, int kind, uint64_t *out, int max, char *err, unsigned errcap);

/* the instruction restrictions checked on words already encoded (a generator's output): 0, or
   -1 with the reason in err */
int qpu_check (const uint64_t *w, int n, int kind, char *err, unsigned errcap);

/* one word -> its text (a static buffer, or Mesa's allocation: for tools and tests) */
const char *qpu_disassemble (uint64_t w);

#ifdef __cplusplus
}
#endif
#endif
