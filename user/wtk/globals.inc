// wtk/globals.inc -- wtk's global variables: WTK_GLOBAL (type, name, initial value), in the order of
// the table a program hands to the shared library (wtk/global.h, wtk/globals.cpp). APPEND-ONLY: a
// program built before a variable was added does not have it (the library then uses its own).
// The palette (wtk/theme.h): the modernised CDE's defaults -- CDE's beige face, a teal accent, the
// Peach frame in front.
WTK_GLOBAL (unsigned, C_BG, 0)
WTK_GLOBAL (unsigned, C_FACE, 0)
WTK_GLOBAL (unsigned, C_FACE_HI, 0)
WTK_GLOBAL (unsigned, C_FACE_DN, 0)
WTK_GLOBAL (unsigned, C_BORDER, 0)
WTK_GLOBAL (unsigned, C_TEXT, 0)
WTK_GLOBAL (unsigned, C_ACCENT, 0x004992A7)
WTK_GLOBAL (unsigned, C_DIS, 0)
WTK_GLOBAL (unsigned, C_FIELD, 0)
WTK_GLOBAL (unsigned, C_FIELD_TEXT, 0)
WTK_GLOBAL (unsigned, C_SEL_TEXT, 0)
WTK_GLOBAL (unsigned, C_FRAME_ACTIVE, 0x00F0B07A)
WTK_GLOBAL (unsigned, C_FRAME_INACTIVE, WK_GREY)
WTK_GLOBAL (int, WK_OUTLINE, 1)
WTK_GLOBAL (int, WK_STYLE, WK_STYLE_CDE)
WTK_GLOBAL (unsigned, C_DOCK, 0x00A4BACE)
WTK_GLOBAL (unsigned, C_BUTTON, 0)
WTK_GLOBAL (unsigned, C_BUTTON_TEXT, 0)
WTK_GLOBAL (unsigned, C_MENUBAR, 0)
// The installed text face (wtk/text.h)
WTK_GLOBAL (TextFace *, wk_face_, 0)
WTK_GLOBAL (int, wk_face_fw_, 8)
