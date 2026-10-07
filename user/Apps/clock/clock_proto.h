//
// clock_proto.h -- how the Clock and clockd speak (AppKit's services and mailboxes: kapi_ipc_register /
// kapi_ipc_lookup, kapi_mailbox_send / kapi_mailbox_recv, 512 bytes a message at most).
//
//   The Clock registers "clock"; a second Clock started sends it CLOCK_MSG_OPEN with its arguments, raises it
//   (kapi_raise_app ("clock")) and quits -- one Clock at a time. clockd, ringing an alarm, sends the running Clock
//   CLOCK_MSG_OPEN "--ring <id>" (or "--ring timer"), else starts "clock --ring <id>"; at its start, the once alarms
//   missed while the Pi was off: "--missed <id> <YYYYMMDDHHMM> [...]" the same way (the Clock notifies them, no window).
//   clockd registers "clockd" (a second one quits); the Clock sends it CLOCKD_MSG_RELOAD after it has written
//   SD:/apps/clock.app/alarms.txt (clockd also looks at the file every 30 s), CLOCKD_MSG_QUIT to stop it.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef CLOCK_PROTO_H
#define CLOCK_PROTO_H

#define CLOCK_SERVICE		"clock"
#define CLOCKD_SERVICE		"clockd"

// To "clock": the Clock's arguments as one string and a 0 ("alarms", "stopwatch", "--ring 3", "--ring timer",
// "--missed 2 202609280700");
// empty (a lone 0): only come forward.
#define CLOCK_MSG_OPEN		1
// To "clockd": read alarms.txt again now (no payload). What was already rung stays rung.
#define CLOCKD_MSG_RELOAD	2
// To "clockd": quit (no payload).
#define CLOCKD_MSG_QUIT		3

#endif
