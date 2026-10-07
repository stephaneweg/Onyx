' gpio_oled.bas -- un écran OLED SSD1306 (128 x 64) sur le bus I2C : une balle qui rebondit dans un cadre.
'
' Câblage : VCC -> 3V3 (broche 1), GND -> GND (broche 9), SDA -> GPIO 2 (broche 3), SCL -> GPIO 3 (broche 5).
' Son adresse : &H3C (certaines cartes : &H3D). L'image est gardée ici (1024 octets : 8 bandes de 8 lignes,
' un octet par colonne, le bit 0 en haut) et envoyée entière : I2CENVOYER adresse, CHR$(&H40) + les octets.
'
a = &H3C
I2COUVRIR 400000                        ' (l'écran accepte le bus rapide)
' son démarrage : éteint, l'horloge, 64 lignes, sans décalage, la pompe de charge, adressage horizontal, tourné de 180,
' le contraste, allumé
init$ = CHR$(0)
POUR i = 1 JUSQUE 26: READ v: init$ = init$ + CHR$(v): SUITE
DATA &HAE, &HD5, &H80, &HA8, &H3F, &HD3, 0, &H40, &H8D, &H14, &H20, 0, &HA1, &HC8
DATA &HDA, &H12, &H81, &HCF, &HD9, &HF1, &HDB, &H40, &HA4, &HA6, &HAF, &HAF
I2CENVOYER a, init$
DIM fb(1023) COMME OCTET
x = 20: y = 20: dx = 3: dy = 2
AFFICHER "Une balle sur l'ecran en &H3C -- une touche pour finir."
FAIRE
    POUR i = 0 JUSQUE 1023: fb(i) = 0: SUITE
    POUR i = 0 JUSQUE 127: fb(i) = 1: fb(896 + i) = 128: SUITE          ' le cadre : haut et bas
    POUR p = 0 JUSQUE 7: fb(p * 128) = 255: fb(p * 128 + 127) = 255: SUITE
    POUR yy = -3 JUSQUE 3: POUR xx = -3 JUSQUE 3                            ' la balle
        SI xx * xx + yy * yy <= 9 ALORS px = x + xx: py = y + yy: k = (py \ 8) * 128 + px: fb(k) = fb(k) OU 2 ^ (py MOD 8)
    SUITE: SUITE
    x = x + dx: y = y + dy
    SI x < 5 OU x > 120 ALORS dx = -dx
    SI y < 5 OU y > 58 ALORS dy = -dy
    s$ = CHR$(0) + CHR$(&H21) + CHR$(0) + CHR$(127) + CHR$(&H22) + CHR$(0) + CHR$(7)
    I2CENVOYER a, s$                                              ' tout l'écran, depuis son coin
    d$ = CHR$(&H40)
    POUR i = 0 JUSQUE 1023: d$ = d$ + CHR$(fb(i)): SUITE
    I2CENVOYER a, d$
BOUCLE TANTQUE TOUCHE$ = ""
I2CENVOYER a, CHR$(0) + CHR$(&HAE)                                ' l'écran éteint
