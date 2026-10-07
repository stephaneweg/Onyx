//
// uikit/globals.cpp -- uikit's global variables (uikit/global.h, uikit/globals.inc) and the named themes.
// Compiled on both sides of the shared library: in a program (the import library lib/uikit.imp.a, or a
// static build of uikit) it DEFINES the variables and the table of their addresses the program hands
// to the library; in the library (ONYX_LIB_BUILD) it binds the references to them. Linked first in
// the library: its initialisers run before any other constructor of it.
//
#include "uikit/theme.h"
#include "uikit/text.h"

#ifdef ONYX_LIB_BUILD
extern "C" void *onyx_lib_data (unsigned i);	// (librt.cpp: the program's i-th variable, 0: it has none)
#endif

namespace uikit {

#ifdef ONYX_LIB_BUILD
enum {
#define UIKIT_GLOBAL(type, name, init)	UIKIT_IDX_##name,
#include "uikit/globals.inc"
#undef UIKIT_GLOBAL
};
static void *uk_global (unsigned i, void *own)	{ void *p = onyx_lib_data (i); return p != 0 ? p : own; }
#define UIKIT_GLOBAL(type, name, init) \
	static type uk_own_##name = init; \
	type &name = *(type *) uk_global (UIKIT_IDX_##name, &uk_own_##name);
#include "uikit/globals.inc"
#undef UIKIT_GLOBAL
#else
#define UIKIT_GLOBAL(type, name, init)	type name = init;
#include "uikit/globals.inc"
#undef UIKIT_GLOBAL
#endif

// Dark Coffee: black coffee's browns under Milk's beads, a caramel accent, a black outline.
static const UkPalette s_coffee = { 0x001F1A17, 0x00C8813F, 0x00322A26, 0x00151210, 0x00352C27, 0x00151210, 2 };

const UkNamedTheme uk_themes[] = {
	{ "Peach", 0x00F0B07A, UK_STYLE_CDE }, { "Steel", 0x007A98C0, UK_STYLE_CDE },
	{ "Sage", 0x0080AA76, UK_STYLE_CDE }, { "Brick", 0x00C45450, UK_STYLE_CDE },
	{ "Slate", 0x003A4458, UK_STYLE_CDE }, { "Milk", 0x00D4D4D6, UK_STYLE_MILK },
	{ "Dark Coffee", 0x004A3E37, UK_STYLE_MILK, &s_coffee }, { 0, 0, 0 }
};

} // namespace uikit

#ifndef ONYX_LIB_BUILD
// The variables' addresses, in globals.inc's order: what lib/uikit_bind hands to the library.
extern "C" {
extern void *const onyx_uikit_data[];
extern const unsigned onyx_uikit_data_count;
void *const onyx_uikit_data[] = {
#define UIKIT_GLOBAL(type, name, init)	&uikit::name,
#include "uikit/globals.inc"
#undef UIKIT_GLOBAL
};
const unsigned onyx_uikit_data_count = sizeof onyx_uikit_data / sizeof onyx_uikit_data[0];
}
#endif
