/*
 * testament.h -- build/version stamp for the NetSurf port on Onyx.
 *
 * NetSurf's buildsystem generates this from git (tools/make-testament.pl). We bypass the
 * buildsystem, so this is a static stand-in providing the WT_* macros the core references
 * (desktop/version, user-agent). Part of brick 9; supplied via -I.
 */
#ifndef NETSURF_TESTAMENT_H
#define NETSURF_TESTAMENT_H

#define WT_ROOT          "onyx"
#define WT_HOSTNAME      "onyx"
#define WT_COMPILEDATE   "2026-06-25"
#define WT_BRANCHPATH    "master"
#define WT_BRANCHISMASTER 1
#define WT_BRANCHISTRUNK  0
#define WT_BRANCHISTAG    0
#define WT_TAGIS         "master"
#define WT_REVID         "onyx-port"
#define WT_MODIFIED       0
#define WT_NO             0
/* about:testament expands this into  modification_t modifications[] = WT_MODIFICATIONS;
 * the loop is bounded by WT_MODIFIED (0), so the single placeholder is never read. */
#define WT_MODIFICATIONS  { { "", "" } }

#endif
