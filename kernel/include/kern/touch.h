//
// touch.h -- a touch screen as the pointer (sys/touch.cpp; cmdline.txt touch=1): the Goodix GT911 on the I2C bus 1
// (the Waveshare PocketTerm35's), polled by a kernel task, its touches given to the window server as a mouse's.
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
#ifndef _kern_touch_h
#define _kern_touch_h

// The task started (kernel.cpp, touch=1): it finds the controller, then polls it; none found: a line in the log.
void TouchStart (void);

#endif
