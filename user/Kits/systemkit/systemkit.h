//
// systemkit.h -- SystemKit (SD:/lib/systemkit.so): what a program says to the system and to the other
// programs. THE header a program includes:
//
//     #include "systemkit/systemkit.h"
//
// It brings every subject (each has its own header beside this one, where its functions are said):
//   notify.h        notifications                    notify, notify_action
//   volume.h        the volume, the output           volume_save, volume_restore, mixer_set
//   preloadini.h    the programs loaded ahead        preload_ini_load, preload_ini_save
//   autostart.h     the programs started at boot     autostart_has, autostart_ensure
//   applet_proto.h  an applet in the Control Panel   (the protocol)
//   session.h       the mode and its session         session_mode, session_set_mode, session_switch...
//   locale.h        the language, the time zone      locale_language, locale_set_language, locale_zone...
//   clipboard.h     the clipboard                    clip_set_text, clip_get_text, clip_get_image...   (C++)
//   trash.h         the trash                        trash_move, trash_restore, trash_count...        (C++)
//   fileassoc.h     which app opens which file       fa_open, fa_app_for                              (C++)
//   wallpaper.h     the wallpaper                    wp_load, wp_save, wp_paint...                    (C++)
//   dockconf.h      the dock's settings              dock_load, dock_save, dock_reload...             (C++)
// A C program gets the subjects written in C (the first seven); a C++ one gets them all.
// Link lib/systemkit.imp.a (C++) or lib/systemkit.imp_c.a (C).
//
#ifndef _systemkit_h
#define _systemkit_h
#include "notify.h"
#include "volume.h"
#include "preloadini.h"
#include "autostart.h"
#include "applet_proto.h"
#include "locale.h"
#include "session.h"
#ifdef __cplusplus
#include "clipboard.h"
#include "trash.h"
#include "fileassoc.h"
#include "wallpaper.h"
#include "dockconf.h"
#endif
#endif
