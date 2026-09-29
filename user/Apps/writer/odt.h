#ifndef _writer_odt_h
#define _writer_odt_h
#include "fileio.h"
namespace wr {
static bool odt_is (const char *, int) { return false; }
static bool odt_load (Doc &, const char *, int) { return false; }
static bool odt_save (Doc &, unsigned char **, unsigned *) { return false; }
}
#endif
