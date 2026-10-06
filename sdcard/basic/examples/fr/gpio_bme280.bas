' gpio_bme280.bas -- un BME280 (température, pression, humidité) sur le bus I2C, lu chaque seconde.
'
' Câblage (une carte BME280 / BMP280) : VIN -> 3V3 (broche 1), GND -> GND (broche 6), SDA -> GPIO 2 (broche 3),
' SCL -> GPIO 3 (broche 5). Son adresse : &H76 (&H77 quand son SDO est relié au 3V3).
' Les formules sont celles de la notice (Bosch BST-BME280-DS002, 4.2.3).
'
a = &H76
AFFICHER "Sur le bus I2C :"; I2CSCAN$
SI I2CLIRE(a, &HD0) <> &H60 ALORS AFFICHER "Pas de BME280 en &H76 (son identifiant : &H60 ; un BMP280 dit &H58)": FIN
c$ = I2CLIRE$(a, &H88, 26): h$ = I2CLIRE$(a, &HE1, 7)
T1 = U16(c$, 1): T2 = S16(c$, 3): T3 = S16(c$, 5)
P1 = U16(c$, 7): P2 = S16(c$, 9): P3 = S16(c$, 11): P4 = S16(c$, 13): P5 = S16(c$, 15)
P6 = S16(c$, 17): P7 = S16(c$, 19): P8 = S16(c$, 21): P9 = S16(c$, 23)
H1 = ASC(MID$(c$, 26, 1)): H2 = S16(h$, 1): H3 = ASC(MID$(h$, 3, 1))
H4 = S8(h$, 4) * 16 + (ASC(MID$(h$, 5, 1)) ET 15): H5 = S8(h$, 6) * 16 + ASC(MID$(h$, 5, 1)) \ 16: H6 = S8(h$, 7)
I2CECRIRE a, &HF2, 1                   ' l'humidité mesurée une fois
AFFICHER "Une touche pour finir."
FAIRE
    I2CECRIRE a, &HF4, &H25            ' température et pression une fois, une seule mesure ("forced")
    PAUSE 50
    d$ = I2CLIRE$(a, &HF7, 8)
    adcP = B(d$, 1) * 4096 + B(d$, 2) * 16 + B(d$, 3) \ 16
    adcT = B(d$, 4) * 4096 + B(d$, 5) * 16 + B(d$, 6) \ 16
    adcH = B(d$, 7) * 256 + B(d$, 8)
    ' température
    v1 = (adcT / 16384 - T1 / 1024) * T2
    v2 = (adcT / 131072 - T1 / 8192) ^ 2 * T3
    tf = v1 + v2: t = tf / 5120
    ' pression
    v1 = tf / 2 - 64000: v2 = v1 * v1 * P6 / 32768 + v1 * P5 * 2: v2 = v2 / 4 + P4 * 65536
    v1 = (P3 * v1 * v1 / 524288 + P2 * v1) / 524288: v1 = (1 + v1 / 32768) * P1
    p = 0
    SI v1 <> 0 ALORS p = 1048576 - adcP: p = (p - v2 / 4096) * 6250 / v1: p = p + (P9 * p * p / 2147483648# + p * P8 / 32768 + P7) / 16
    ' humidité
    hh = tf - 76800
    hh = (adcH - (H4 * 64 + H5 / 16384 * hh)) * (H2 / 65536 * (1 + H6 / 67108864 * hh * (1 + H3 / 67108864 * hh)))
    hh = hh * (1 - H1 * hh / 524288)
    SI hh > 100 ALORS hh = 100
    SI hh < 0 ALORS hh = 0
    AFFICHER USING "##.## C   ####.## hPa   ###.# %"; t; p / 100; hh
    PAUSE 1000
BOUCLE TANTQUE TOUCHE$ = ""
FIN

FONCTION B (s$, i)
    B = ASC(MID$(s$, i, 1))
FIN FONCTION

FONCTION U16 (s$, i)                  ' deux octets, le poids faible d'abord
    U16 = ASC(MID$(s$, i, 1)) + 256 * ASC(MID$(s$, i + 1, 1))
FIN FONCTION

FONCTION S16 (s$, i)                  ' les mêmes, signés
    v = U16(s$, i)
    SI v >= 32768 ALORS v = v - 65536
    S16 = v
FIN FONCTION

FONCTION S8 (s$, i)                   ' un octet signé
    v = ASC(MID$(s$, i, 1))
    SI v >= 128 ALORS v = v - 256
    S8 = v
FIN FONCTION
