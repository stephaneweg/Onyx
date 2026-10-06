' gpio_servo.bas -- un servomoteur de modélisme (SG90, MG996R) balayé d'un côté à l'autre par PWM.
'
' Câblage : le signal du servo (orange / jaune) -> GPIO 18 (broche 12), sa masse (marron / noir) -> GND
' (broche 14), son alimentation (rouge) -> 5 V (broche 2) -- le 5 V n'alimente que le moteur : le signal reste en 3,3 V.
' Un gros servo demande sa propre alimentation (sa masse reliée à celle du Pi).
'
' SERVO broche, angle : 50 impulsions par seconde, de 0,5 ms (0 degré) à 2,5 ms (180 degrés).
' PWM broche, rapport% [, Hz] fait un signal carré sur les GPIO 12, 13, 18 ou 19 (une LED atténuée, un buzzer).
'
AFFICHER "Le servo du GPIO 18 balaie -- une touche pour finir."
FAIRE
    POUR a = 0 JUSQUE 180 PAS 5
        SERVO 18, a: PAUSE 30
    SUITE
    POUR a = 180 JUSQUE 0 PAS -5
        SERVO 18, a: PAUSE 30
    SUITE
BOUCLE TANTQUE TOUCHE$ = ""
SERVO 18, 90
PAUSE 500
LIBERERBROCHE 18
