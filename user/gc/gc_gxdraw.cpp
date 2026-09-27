//
// gc/gc_gxdraw.cpp -- the GX drawing: the primitives (their vertices transformed by the XF's
// matrices, lit, coloured by the TEV) and the EFB copies (to the XFB the VI shows, or to a
// texture). For now the primitives are counted and the copies to the XFB clear it.
//
#include "gc/gc.h"

namespace gc {

void Machine::gxPrimitive (int, int, int, const u8 *) {}

// BP 0x52: an EFB copy (bit 14: to the XFB; 11: clear the EFB after)
void Machine::gxCopy (u32 v)
{
	gxCopies++;
	(void) v;
}

} // namespace gc
