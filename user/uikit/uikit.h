//
// uikit/uikit.h -- umbrella header for the uikit widget toolkit. Apps:
//   #include "uikit/uikit.h"          (declarations)
//   ... build a tree of uikit::Widget subclasses, root.run() ...
//   and link uikit/libuikit.a         (the compiled toolkit).
//
// The toolkit is a recursive, retained-mode widget tree (port of VMKernel's
// GUI_ELEMENT/GIMAGE): every Widget owns a Canvas; damage propagates up (valid /
// shouldRedraw); draw() recomposes only dirty subtrees; handleMouse() routes down
// with coordinate conversion; the Root adopts the kapi window canvas and presents.
//
#ifndef _uikit_uikit_h
#define _uikit_uikit_h

#include "onyxpp.hpp"		// operator new/delete (umm) -- ensure it's emitted in the app TU

#include "uikit/canvas.h"
#include "uikit/font.h"
#include "uikit/text.h"		// the text face an app installs (FreeType's: ft/uikitface.h)
#include "uikit/widget.h"
#include "uikit/skin.h"
#include "uikit/label.h"
#include "uikit/panel.h"
#include "uikit/button.h"
#include "uikit/checkbox.h"
#include "uikit/textbox.h"
#include "uikit/slider.h"
#include "uikit/progress.h"
#include "uikit/scrollbar.h"
#include "uikit/textarea.h"
#include "uikit/richtextbox.h"
#include "uikit/icon.h"
#include "uikit/dropdown.h"
#include "uikit/combobox.h"
#include "uikit/colorpick.h"
#include "uikit/splitter.h"
#include "uikit/layout.h"
#include "uikit/tabhost.h"
#include "uikit/root.h"
#include "uikit/dialog.h"
#include "uikit/menu.h"
#include "uikit/radio.h"
#include "uikit/groupbox.h"
#include "uikit/toggle.h"
#include "uikit/numeric.h"
#include "uikit/listbox.h"
#include "uikit/datagrid.h"
#include "uikit/treeview.h"
#include "uikit/calendar.h"
#include "uikit/imagebox.h"
#include "uikit/vpaint.h"
#include "uikit/knob.h"
#include "uikit/vumeter.h"
#include "uikit/segmented.h"
#include "uikit/lcd.h"
#include "uikit/lang.h"		// TR (): the words in the language chosen
// (uikit/toolbar.h -- ToolBar, ToolButton, the WKT_* icons -- is included by the app itself: Letters,
//  the Spreadsheet and Cardfile have their own ToolBar / ToolButton next to `using namespace uikit`.)

#endif
