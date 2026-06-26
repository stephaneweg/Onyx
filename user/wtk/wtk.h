//
// wtk/wtk.h -- umbrella header for the wtk widget toolkit. Apps:
//   #include "wtk/wtk.h"          (declarations)
//   ... build a tree of wtk::Widget subclasses, root.run() ...
//   and link wtk/libwtk.a         (the compiled toolkit).
//
// The toolkit is a recursive, retained-mode widget tree (port of VMKernel's
// GUI_ELEMENT/GIMAGE): every Widget owns a Canvas; damage propagates up (valid /
// shouldRedraw); draw() recomposes only dirty subtrees; handleMouse() routes down
// with coordinate conversion; the Root adopts the kapi window canvas and presents.
//
#ifndef _wtk_wtk_h
#define _wtk_wtk_h

#include "onyxpp.hpp"		// operator new/delete (umm) -- ensure it's emitted in the app TU

#include "wtk/canvas.h"
#include "wtk/widget.h"
#include "wtk/label.h"
#include "wtk/panel.h"
#include "wtk/button.h"
#include "wtk/checkbox.h"
#include "wtk/textbox.h"
#include "wtk/slider.h"
#include "wtk/progress.h"
#include "wtk/scrollbar.h"
#include "wtk/textarea.h"
#include "wtk/richtextbox.h"
#include "wtk/icon.h"
#include "wtk/root.h"

#endif
