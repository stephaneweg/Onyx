' GPIO through GPIOKit's simulator (the host: tools/tests/basic/host_main.cpp)
PINMODE 17, "OUT"
PIN 17 = 1
PRINT "GPIO17 after PIN 17 = 1:"; PIN(17)
PIN 17, 0
PRINT "GPIO17 after PIN 17, 0:"; PIN(17)
PIN (17) = 1
PRINT "GPIO17 again:"; PIN(17)
PINMODE 27, "pullup"
PRINT "GPIO27 pulled up:"; PIN(27)
PINMODE 22, "PULLDOWN"
PRINT "GPIO22 pulled down:"; PIN(22)
PWM 18, 25
PWM 13, 12.5, 50
SERVO 18, 90
PRINT
' the reserved and the wrong
ON ERROR GOTO Oops
PINMODE 14, "OUT"
PINMODE 17, "SIDEWAYS"
PIN 22 = 1
PWM 5, 50
SERVO 12, 45
ON ERROR GOTO 0
' I2C: the simulator's SSD1306 (0x3C) and BME280 (0x76)
PRINT "I2C:"; I2CSCAN$
PRINT "BME280 id:"; HEX$(I2CREAD(&H76, &HD0))
c$ = I2CREAD$(&H76, &H88, 6)
PRINT "dig_T1:"; ASC(MID$(c$, 1, 1)) + 256 * ASC(MID$(c$, 2, 1))
I2CWRITE &H76, &HF4, &H25
I2CSEND &H3C, CHR$(0) + CHR$(&HAF)
PRINT "SPI:"; SPI$("hello")
' edges
n = 0
ON PIN (27, 2) GOSUB Pressed
FOR i = 1 TO 200: x = x + 1: NEXT
PIN (27) OFF
PRINT "pressed:"; (n > 0)
PRINT "changed:"; (PINCHANGED(27) >= 0)
PINFREE 17
PINFREE
PRINT "done"
END
Pressed:
n = n + 1
RETURN
Oops:
PRINT "error"; ERR
RESUME NEXT
