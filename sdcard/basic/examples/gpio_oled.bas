' gpio_oled.bas -- an SSD1306 OLED display (128 x 64) on the I2C bus: a bouncing ball and a frame.
'
' Wiring: VCC -> 3V3 (pin 1), GND -> GND (pin 9), SDA -> GPIO 2 (pin 3), SCL -> GPIO 3 (pin 5).
' Its address: &H3C (some boards: &H3D). The picture is kept here (1024 bytes: 8 bands of 8 rows,
' a byte a column, bit 0 at the top) and sent whole: I2CSEND address, CHR$(&H40) + the bytes.
'
a = &H3C
I2COPEN 400000                        ' (the display takes the fast bus)
' its start: off, the clock, 64 rows, no offset, the charge pump, horizontal addressing, turned 180,
' contrast, on
init$ = CHR$(0)
FOR i = 1 TO 26: READ v: init$ = init$ + CHR$(v): NEXT
DATA &HAE, &HD5, &H80, &HA8, &H3F, &HD3, 0, &H40, &H8D, &H14, &H20, 0, &HA1, &HC8
DATA &HDA, &H12, &H81, &HCF, &HD9, &HF1, &HDB, &H40, &HA4, &HA6, &HAF, &HAF
I2CSEND a, init$
DIM fb(1023) AS INTEGER
x = 20: y = 20: dx = 3: dy = 2
PRINT "A ball on the display at &H3C -- a key stops."
DO
    FOR i = 0 TO 1023: fb(i) = 0: NEXT
    FOR i = 0 TO 127: fb(i) = 1: fb(896 + i) = 128: NEXT          ' the frame: top and bottom
    FOR p = 0 TO 7: fb(p * 128) = 255: fb(p * 128 + 127) = 255: NEXT
    FOR yy = -3 TO 3: FOR xx = -3 TO 3                            ' the ball
        IF xx * xx + yy * yy <= 9 THEN px = x + xx: py = y + yy: k = (py \ 8) * 128 + px: fb(k) = fb(k) OR 2 ^ (py MOD 8)
    NEXT: NEXT
    x = x + dx: y = y + dy
    IF x < 5 OR x > 122 THEN dx = -dx
    IF y < 5 OR y > 58 THEN dy = -dy
    s$ = CHR$(0) + CHR$(&H21) + CHR$(0) + CHR$(127) + CHR$(&H22) + CHR$(0) + CHR$(7)
    I2CSEND a, s$                                              ' the whole screen, from its corner
    d$ = CHR$(&H40)
    FOR i = 0 TO 1023: d$ = d$ + CHR$(fb(i)): NEXT
    I2CSEND a, d$
LOOP WHILE INKEY$ = ""
I2CSEND a, CHR$(0) + CHR$(&HAE)                                ' the display off
