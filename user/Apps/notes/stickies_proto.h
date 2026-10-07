//
// stickies_proto.h -- how Notes and Stickies speak (AppKit's services and mailboxes: kapi_ipc_register /
// kapi_ipc_lookup, kapi_mailbox_send / kapi_mailbox_recv, 512 bytes a message at most).
//
//   Notes registers "notes"; a second Notes started sends it NOTES_MSG_OPEN with its argument, raises it
//   (kapi_raise_app ("notes")) and quits -- one Notes at a time. Stickies does the same on a card's click.
//   Stickies registers "stickies"; Notes sends it STK_MSG_RELOAD after a note is saved, pinned, coloured or
//   deleted (Stickies also polls every ~3 s), and STK_MSG_QUIT on View > Hide Stickies from the Desktop.
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
#ifndef NOTES_STICKIES_PROTO_H
#define NOTES_STICKIES_PROTO_H

#define NOTES_SERVICE		"notes"
#define STICKIES_SERVICE	"stickies"

// To "notes": open this note. Payload: its path ("SD:/Notes/note-....txt") or bare name, then a 0; empty
// (a lone 0): only come forward (the selection kept).
#define NOTES_MSG_OPEN		1
// To "stickies": read SD:/Notes again now (no payload).
#define STK_MSG_RELOAD		2
// To "stickies": quit (no payload).
#define STK_MSG_QUIT		3

#endif
