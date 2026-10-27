/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ====================================================================
 * Copyright (c) 2013 Carnegie Mellon University.  All rights 
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced 
 * Research Projects Agency and the National Science Foundation of the 
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
#ifndef FE_NOISE_H
#define FE_NOISE_H

#include "fe/fe.h"
#include "fe/fixpoint.h"
#include "fe/fe_type.h"

typedef struct noise_stats_s noise_stats_t;

#define SMOOTH_WINDOW 4

struct noise_stats_s {
    /* Smoothed power */
    powspec_t *power;
    /* Noise estimate */
    powspec_t *noise;
    /* Signal floor estimate */
    powspec_t *floor;
    /* Peak for temporal masking */
    powspec_t *peak;
    /* Buffers used in update_noisestats */
    powspec_t *signal, *gain;

    /* Initialize it next time */
    int undefined;
    /* Number of items to process */
    int num_filters;

    /* Sum of slow peaks for VAD */
    powspec_t slow_peak_sum;

    /* Precomputed constants */
    powspec_t lambda_power;
    powspec_t comp_lambda_power;
    powspec_t lambda_a;
    powspec_t comp_lambda_a;
    powspec_t lambda_b;
    powspec_t comp_lambda_b;
    powspec_t lambda_t;
    powspec_t mu_t;
    powspec_t max_gain;
    powspec_t inv_max_gain;

    powspec_t smooth_scaling[2 * SMOOTH_WINDOW + 3];
};

/* Creates noisestats object */
noise_stats_t *fe_init_noisestats(int num_filters);

/* Resets collected noise statistics */
void fe_reset_noisestats(noise_stats_t * noise_stats);

/* Frees allocated data */
void fe_free_noisestats(noise_stats_t * noise_stats);

/**
 * Process frame, update noise statistics, remove noise components if needed.
 */
void fe_remove_noise(fe_t *fe);

#endif                          /* FE_NOISE_H */
