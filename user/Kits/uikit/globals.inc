// uikit/globals.inc -- uikit's global variables: UIKIT_GLOBAL (type, name, initial value), in the order of
// the table a program hands to the shared library (uikit/global.h, uikit/globals.cpp). APPEND-ONLY: a
// program built before a variable was added does not have it (the library then uses its own).
// The palette (uikit/theme.h): the modernised CDE's defaults -- CDE's beige face, a teal accent, the
// Peach frame in front.
UIKIT_GLOBAL (unsigned, C_BG, 0)
UIKIT_GLOBAL (unsigned, C_FACE, 0)
UIKIT_GLOBAL (unsigned, C_FACE_HI, 0)
UIKIT_GLOBAL (unsigned, C_FACE_DN, 0)
UIKIT_GLOBAL (unsigned, C_BORDER, 0)
UIKIT_GLOBAL (unsigned, C_TEXT, 0)
UIKIT_GLOBAL (unsigned, C_ACCENT, 0x004992A7)
UIKIT_GLOBAL (unsigned, C_DIS, 0)
UIKIT_GLOBAL (unsigned, C_FIELD, 0)
UIKIT_GLOBAL (unsigned, C_FIELD_TEXT, 0)
UIKIT_GLOBAL (unsigned, C_SEL_TEXT, 0)
UIKIT_GLOBAL (unsigned, C_FRAME_ACTIVE, 0x00F0B07A)
UIKIT_GLOBAL (unsigned, C_FRAME_INACTIVE, UK_GREY)
UIKIT_GLOBAL (int, UK_OUTLINE, 1)
UIKIT_GLOBAL (int, UK_STYLE, UK_STYLE_CDE)
UIKIT_GLOBAL (unsigned, C_DOCK, 0x00A4BACE)
UIKIT_GLOBAL (unsigned, C_BUTTON, 0)
UIKIT_GLOBAL (unsigned, C_BUTTON_TEXT, 0)
UIKIT_GLOBAL (unsigned, C_MENUBAR, 0)
// The installed text face (uikit/text.h)
UIKIT_GLOBAL (TextFace *, uk_face_, 0)
UIKIT_GLOBAL (int, uk_face_fw_, 8)
