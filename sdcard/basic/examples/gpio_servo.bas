' gpio_servo.bas -- a hobby servo (an SG90, an MG996R) swept from side to side by PWM.
'
' Wiring: the servo's signal (orange / yellow) -> GPIO 18 (pin 12), its ground (brown / black) -> GND
' (pin 14), its power (red) -> 5 V (pin 2) -- the 5 V pin feeds the motor only: the signal stays 3.3 V.
' A big servo wants its own supply (its ground joined to the Pi's).
'
' SERVO pin, angle: 50 pulses a second, 0.5 ms (0 degrees) .. 2.5 ms (180 degrees).
' PWM pin, duty% [, Hz] makes any square wave on GPIO 12, 13, 18 or 19 (an LED dimmed, a buzzer).
'
PRINT "Sweeping the servo on GPIO 18 -- a key stops."
DO
    FOR a = 0 TO 180 STEP 5
        SERVO 18, a: PAUSE 30
    NEXT
    FOR a = 180 TO 0 STEP -5
        SERVO 18, a: PAUSE 30
    NEXT
LOOP WHILE INKEY$ = ""
SERVO 18, 90
PAUSE 500
PINFREE 18
