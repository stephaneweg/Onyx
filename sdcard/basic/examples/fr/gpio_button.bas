' gpio_button.bas -- un bouton poussoir lu de deux façons : en lisant BROCHE(), et par un événement (SUR BROCHE ... GOSUB).
'
' Câblage : GPIO 27 (broche 13) -> le bouton -> GND (broche 14). Pas de résistance : la broche est rappelée à 1 dans la
' puce ("RAPPELHAUT") : elle lit 1 au repos et 0 tant que le bouton est enfoncé.
' Une LED sur le GPIO 17 (le câblage de gpio_blink.bas) suit le bouton.
'
MODEBROCHE 27, "RAPPELHAUT"
MODEBROCHE 17, "SORTIE"
appuis = 0
SUR BROCHE (27, 2) GOSUB Appui          ' 2 = les fronts descendants : chaque appui (1 montants, 3 les deux)
AFFICHER "Appuyez sur le bouton du GPIO 27 -- une touche pour finir."
FAIRE
    BROCHE 17 = 1 - BROCHE(27)              ' la LED allumée tant que le bouton est tenu
    PAUSE 10
BOUCLE TANTQUE TOUCHE$ = ""
BROCHE (27) ARRET
LIBERERBROCHE
AFFICHER appuis; "appuis."
FIN

Appui:
    appuis = appuis + 1
    AFFICHER "Appui !"; appuis        ' (un bouton rebondit : un appui peut compter double)
    RETOUR
