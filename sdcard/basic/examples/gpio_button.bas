' gpio_button.bas -- a push button read two ways: by polling PIN(), and as an event (ON PIN ... GOSUB).
'
' Wiring: GPIO 27 (pin 13) -> the button -> GND (pin 14). No resistor: the pin is pulled up inside the
' chip ("PULLUP"), so it reads 1 at rest and 0 while the button is pressed.
' An LED on GPIO 17 (gpio_blink.bas's wiring) follows the button.
'
PINMODE 27, "PULLUP"
PINMODE 17, "OUT"
presses = 0
ON PIN (27, 2) GOSUB Pressed          ' 2 = the falling edges: each press (1 rising, 3 both)
PRINT "Press the button on GPIO 27 -- a key stops."
DO
    PIN 17 = 1 - PIN(27)              ' the LED lit while the button is held
    PAUSE 10
LOOP WHILE INKEY$ = ""
PIN (27) OFF
PINFREE
PRINT presses; "presses."
END

Pressed:
    presses = presses + 1
    PRINT "Pressed!"; presses        ' (a button bounces: one press may count twice)
    RETURN
