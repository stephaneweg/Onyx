' gpio_bme280.bas -- a BME280 (temperature, pressure, humidity) on the I2C bus, read every second.
'
' Wiring (a BME280 / BMP280 board): VIN -> 3V3 (pin 1), GND -> GND (pin 6), SDA -> GPIO 2 (pin 3),
' SCL -> GPIO 3 (pin 5). Its address: &H76 (&H77 when its SDO is tied to 3V3).
' The formulas are the datasheet's (Bosch BST-BME280-DS002, 4.2.3).
'
a = &H76
PRINT "On the I2C bus: "; I2CSCAN$
IF I2CREAD(a, &HD0) <> &H60 THEN PRINT "No BME280 at &H76 (its id: &H60; a BMP280 says &H58)": END
c$ = I2CREAD$(a, &H88, 26): h$ = I2CREAD$(a, &HE1, 7)
T1 = U16(c$, 1): T2 = S16(c$, 3): T3 = S16(c$, 5)
P1 = U16(c$, 7): P2 = S16(c$, 9): P3 = S16(c$, 11): P4 = S16(c$, 13): P5 = S16(c$, 15)
P6 = S16(c$, 17): P7 = S16(c$, 19): P8 = S16(c$, 21): P9 = S16(c$, 23)
H1 = ASC(MID$(c$, 26, 1)): H2 = S16(h$, 1): H3 = ASC(MID$(h$, 3, 1))
H4 = S8(h$, 4) * 16 + (ASC(MID$(h$, 5, 1)) AND 15): H5 = S8(h$, 6) * 16 + ASC(MID$(h$, 5, 1)) \ 16: H6 = S8(h$, 7)
I2CWRITE a, &HF2, 1                   ' humidity sampled once
PRINT "A key stops."
DO
    I2CWRITE a, &HF4, &H25            ' temperature and pressure once, one measure ("forced")
    PAUSE 50
    d$ = I2CREAD$(a, &HF7, 8)
    adcP = B(d$, 1) * 4096 + B(d$, 2) * 16 + B(d$, 3) \ 16
    adcT = B(d$, 4) * 4096 + B(d$, 5) * 16 + B(d$, 6) \ 16
    adcH = B(d$, 7) * 256 + B(d$, 8)
    ' temperature
    v1 = (adcT / 16384 - T1 / 1024) * T2
    v2 = (adcT / 131072 - T1 / 8192) ^ 2 * T3
    tf = v1 + v2: t = tf / 5120
    ' pressure
    v1 = tf / 2 - 64000: v2 = v1 * v1 * P6 / 32768 + v1 * P5 * 2: v2 = v2 / 4 + P4 * 65536
    v1 = (P3 * v1 * v1 / 524288 + P2 * v1) / 524288: v1 = (1 + v1 / 32768) * P1
    p = 0
    IF v1 <> 0 THEN p = 1048576 - adcP: p = (p - v2 / 4096) * 6250 / v1: p = p + (P9 * p * p / 2147483648# + p * P8 / 32768 + P7) / 16
    ' humidity
    hh = tf - 76800
    hh = (adcH - (H4 * 64 + H5 / 16384 * hh)) * (H2 / 65536 * (1 + H6 / 67108864 * hh * (1 + H3 / 67108864 * hh)))
    hh = hh * (1 - H1 * hh / 524288)
    IF hh > 100 THEN hh = 100
    IF hh < 0 THEN hh = 0
    PRINT USING "##.## C   ####.## hPa   ###.# %"; t; p / 100; hh
    PAUSE 1000
LOOP WHILE INKEY$ = ""
END

FUNCTION B (s$, i)
    B = ASC(MID$(s$, i, 1))
END FUNCTION

FUNCTION U16 (s$, i)                  ' two bytes, the low one first
    U16 = ASC(MID$(s$, i, 1)) + 256 * ASC(MID$(s$, i + 1, 1))
END FUNCTION

FUNCTION S16 (s$, i)                  ' the same, signed
    v = U16(s$, i)
    IF v >= 32768 THEN v = v - 65536
    S16 = v
END FUNCTION

FUNCTION S8 (s$, i)                   ' a signed byte
    v = ASC(MID$(s$, i, 1))
    IF v >= 128 THEN v = v - 256
    S8 = v
END FUNCTION
