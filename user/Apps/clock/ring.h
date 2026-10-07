//
// ring.h -- an alarm ringing (04 D13-D15; step 8). One translation unit with main.cpp.
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
static void ring_alarm (int id) { say ("ring %d", id); tab_show (TAB_ALARMS); }
static void ring_tick () {}
static void sound_stop () {}
static int sound_test (const char *) { return -1; }
