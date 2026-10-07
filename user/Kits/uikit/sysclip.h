//
// sysclip.h -- UIKit's own use of the system clipboard (its text fields' copy and paste): SystemKit, opened
// when a field first copies or pastes. (A program calls SystemKit's clip_* itself: systemkit/clipboard.h.)
//
#ifndef _uikit_sysclip_h
#define _uikit_sysclip_h
namespace uikit {
bool uk_clip_ready ();			// SystemKit is there (false: no clipboard -- the field does nothing)
}
#endif
