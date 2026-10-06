// gktest.cpp -- GPIOKit's simulator on the PC (run_gpiokit_test.sh): the pins, PWM, edges, the I2C devices,
// SPI, the header's table, the C++ classes. MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#include "gpiokit/gpiokit.h"
#include <cstdio>
#include <cstring>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main ()
{
	CHECK (gk_available () == 2);				// (a PC: the simulator)
	// pins
	CHECK (gk_mode (17, GK_OUT) == 0);
	CHECK (gk_read (17) == 0);
	CHECK (gk_write (17, 1) == 0 && gk_read (17) == 1);
	CHECK (gk_toggle (17) == 0 && gk_read (17) == 0);
	CHECK (gk_mode (14, GK_OUT) == GK_EPERM);		// the serial console
	CHECK (gk_mode (0, GK_IN) == GK_EPERM);			// the HAT EEPROM
	CHECK (gk_mode (28, GK_IN) == GK_EINVAL);
	CHECK (gk_write (22, 1) == GK_EPERM);			// (not the program's)
	CHECK (gk_mode (22, GK_IN_PULLDOWN) == 0 && gk_read (22) == 0);
	CHECK (gk_write (22, 1) == GK_EINVAL);			// (an input)
	CHECK (gk_mode (27, GK_IN_PULLUP) == 0 && gk_read (27) == 1);
	CHECK (gk_mode (5, GK_IN) == 0 && gk_read (5) == 0);	// (floating: 0 in the simulator)
	CHECK (gk_sim_input (5, 1) == 0 && gk_read (5) == 1);
	unsigned all = gk_read_all ();
	CHECK ((all >> 27 & 1) && (all >> 5 & 1) && !(all >> 17 & 1));
	// PWM
	CHECK (gk_pwm (18, 50, 750) == 0);
	CHECK (gk_pwm (12, 1000, 5000) == GK_EBUSY);		// (12 and 18: one channel)
	CHECK (gk_pwm (13, 1000, 5000) == 0);
	CHECK (gk_pwm (5, 1000, 5000) == GK_EINVAL);
	CHECK (gk_pwm (13, 1000, 10001) == GK_EINVAL);
	CHECK (gk_servo (18, 2000) == 0);
	gk_pin_info info[GK_PINS];
	CHECK (gk_info (info, GK_PINS) == GK_PINS);
	CHECK (info[18].mode == GK_PWM && info[18].pwm_freq == 50 && info[18].pwm_duty == 1000);
	CHECK (info[14].flags & GK_F_RESERVED);
	CHECK (info[12].flags & GK_F_PWM);
	CHECK (!strcmp (gk_mode_name (info[22].mode), "pull-down"));
	// edges
	CHECK (gk_edges (17, GK_BOTH) == GK_EINVAL);		// (an output)
	CHECK (gk_edges (27, GK_FALLING) == 0);
	gk_sim_input (27, 0); gk_sim_input (27, 1); gk_sim_input (27, 0);
	gk_event ev[8];
	int n = gk_events (ev, 8, 0);
	CHECK (n == 2 && ev[0].pin == 27 && ev[0].edge == GK_FALLING && ev[1].us >= ev[0].us);
	CHECK (gk_events (ev, 8, 10) == 0);
	// I2C
	unsigned char map[16];
	CHECK (gk_i2c_scan (map) == GK_EPERM);			// (the bus not opened)
	CHECK (gk_i2c_open (0) == 0);
	CHECK (gk_i2c_scan (map) == 2 && (map[0x3C / 8] >> 4 & 1) && (map[0x76 / 8] >> 6 & 1));
	CHECK (gk_i2c_reg_read (0x76, 0xD0) == 0x60);
	CHECK (gk_i2c_reg_read (0x50, 0) == GK_EIO);
	unsigned char cmd[] = { 0x00, 0x21, 0, 127, 0x22, 0, 7 }, px[] = { 0x40, 0xFF, 0x81 };
	CHECK (gk_i2c_write (0x3C, cmd, sizeof cmd) == (int) sizeof cmd);
	CHECK (gk_i2c_write (0x3C, px, sizeof px) == (int) sizeof px);
	unsigned char fb[1024];
	CHECK (gk_sim_display (fb) == 1 && fb[0] == 0xFF && fb[1] == 0x81);
	CHECK (gk_mode (2, GK_OUT) == GK_EBUSY);		// (a pin of the bus)
	CHECK (!strcmp (gk_i2c_guess (0x3C), "SSD1306 / SH1106 OLED display"));
	// SPI
	CHECK (gk_spi_open (0, 4) == GK_EINVAL);
	CHECK (gk_spi_open (1000000, 0) == 0);
	unsigned char rx[4];
	CHECK (gk_spi_transfer (0, "abc", rx, 3) == 3 && !memcmp (rx, "abc", 3));
	CHECK (gk_spi_close () == 0 && gk_i2c_close () == 0);
	// the header
	CHECK (gk_header_gpio (11) == 17 && gk_header_pin (17) == 11 && gk_header_gpio (1) == -1);
	CHECK (!strcmp (gk_header_label (6), "GND") && !strcmp (gk_header_label (17), "3V3") && !strcmp (gk_header_label (2), "5V"));
	CHECK (!strcmp (gk_gpio_function (3), "SCL1"));
	// C++
	{
		gpiokit::Pin led (23, GK_OUT);
		CHECK (led.ok () && led.write (1) == 0 && led.read () == 1);
		gpiokit::Pin bad (15, GK_IN);
		CHECK (!bad.ok () && bad.status () == GK_EPERM);
		gpiokit::I2CDevice bme (0x76);
		CHECK (bme.ok () && bme.reg (0xD0) == 0x60);
	}
	CHECK (gk_info (info, GK_PINS) == GK_PINS && info[23].mode == GK_FREE);	// (the Pin gave it back)
	CHECK (gk_release () == 0 && gk_info (info, GK_PINS) == GK_PINS && info[17].mode == GK_FREE && info[18].mode == GK_FREE);
	printf (fails ? "gktest: %d failed\n" : "gktest: all passed\n", fails);
	return fails != 0;
}
