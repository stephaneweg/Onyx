//
// gpio.h -- the 40-pin header for the programs (kapi v92 gpio_ctl; sys/gpio.cpp): GPIO 0..27 given one
// process at a time, PWM on 12 / 13 / 18 / 19, edges queued to their owner, the I2C bus 1 and SPI 0.
// The pins the system uses are never given (the serial console, the HAT EEPROM); a process's pins,
// buses and queue go back when it ends (GpioOnProcessGone, from IpcOnProcessGone).
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
#ifndef _kern_gpio_h
#define _kern_gpio_h

void GpioOnProcessGone (unsigned nPid);		// its pins back to inputs, its buses closed, its queue freed
void GpioPwmClockKeep (void);			// the PWM clock running again if a header PWM needs it (the
						// jack's output stops it: sys/sound.cpp calls this after)

extern "C" long kapi_gpio_ctl (int nOp, long a0, long a1, long a2);

#endif
