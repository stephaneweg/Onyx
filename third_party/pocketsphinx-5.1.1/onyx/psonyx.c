/*
 * psonyx.c -- PocketSphinx 5.1.1 on Onyx (a newlib program): in the place of util/mmio.c (no mmap: NULL, the caller
 * reads the file through stdio -- the decoder is made with "-mmap no") and util/profile.c (getrusage, gettimeofday:
 * the timers are not used by PocketQI's keyphrase spotting, they count nothing). As PocketQI's Circle port
 * (system/modules/stt_circle/ps/pspal.c), from which these lines come.
 *
 * MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include <stdio.h>
#include "util/mmio.h"
#include "util/profile.h"

mmio_file_t *mmio_file_read (const char *filename) { (void) filename; return NULL; }
void *mmio_file_ptr (mmio_file_t *mf) { (void) mf; return NULL; }
void mmio_file_unmap (mmio_file_t *mf) { (void) mf; }

void ptmr_start (ptmr_t *tm) { tm->start_cpu = tm->start_elapsed = 0.0; }
void ptmr_stop (ptmr_t *tm) { (void) tm; }
void ptmr_reset (ptmr_t *tm) { tm->t_cpu = 0.0; tm->t_elapsed = 0.0; }
void ptmr_init (ptmr_t *tm)
{
	tm->t_cpu = tm->t_elapsed = 0.0;
	tm->t_tot_cpu = tm->t_tot_elapsed = 0.0;
	tm->start_cpu = tm->start_elapsed = 0.0;
}
void ptmr_reset_all (ptmr_t *tm) { for (; tm->name != NULL; tm++) ptmr_reset (tm); }
void ptmr_print_all (FILE *fp, ptmr_t *tm, float64 norm) { (void) fp; (void) tm; (void) norm; }
int32 host_endian (void) { return 1; }		/* (little-endian) */
