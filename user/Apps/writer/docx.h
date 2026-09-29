#ifndef _writer_docx_h
#define _writer_docx_h
#include "fileio.h"
namespace wr {
static bool docx_is (const char *, int) { return false; }
static bool docx_load (Doc &, const char *, int) { return false; }
static bool docx_save (Doc &, unsigned char **, unsigned *) { return false; }
}
#endif
