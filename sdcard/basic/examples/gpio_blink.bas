' gpio_blink.bas -- an LED blinking on the Raspberry Pi's header (GPIOKit; docs/04 section 13, "GPIO").
'
' Wiring: GPIO 17 (the header's pin 11) -> a 330 ohm resistor -> the LED's long leg (+);
'         the LED's short leg (-) -> a GND pin (pin 9 or 14).
' 3.3 V only: never put 5 V on a GPIO pin. No hardware? GPIOSIM 1 runs the same program on the
' simulator (GPIO Lab shows it), and so does a PC.
'
PRINT "Blinking the LED on GPIO 17 -- a key stops."
PINMODE 17, "OUT"
DO
    PIN 17 = 1: PAUSE 500
    PIN 17 = 0: PAUSE 500
LOOP WHILE INKEY$ = ""
PINFREE 17
PRINT "Done."
