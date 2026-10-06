' gpio_blink.bas -- une LED qui clignote sur le connecteur du Raspberry Pi (GPIOKit).
'
' Câblage : GPIO 17 (broche 11 du connecteur) -> une résistance de 330 ohms -> la patte longue de la LED (+) ;
'         la patte courte de la LED (-) -> une broche GND (broche 9 ou 14).
' 3,3 V seulement : jamais de 5 V sur une broche GPIO. Pas de matériel ? GPIOSIM 1 lance le même programme sur le
' simulateur (GPIO Lab le montre), comme sur un PC.
'
AFFICHER "La LED du GPIO 17 clignote -- une touche pour finir."
MODEBROCHE 17, "SORTIE"
FAIRE
    BROCHE 17 = 1: PAUSE 500
    BROCHE 17 = 0: PAUSE 500
BOUCLE TANTQUE TOUCHE$ = ""
LIBERERBROCHE 17
AFFICHER "Fini."
