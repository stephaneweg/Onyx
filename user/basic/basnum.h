//
// basic/basnum.h -- the math functions of basnum.cpp (internal to the BASIC core).
//
#ifndef _basnum_h
#define _basnum_h

namespace bas {
double nfloor (double x);
double nsqrt (double x);
double nexp (double x);
double nlog (double x);
double nsin (double x);
double ncos (double x);
double ntan (double x);
double natan (double x);
double npow (double x, double y, bool *ok);
}

#endif
