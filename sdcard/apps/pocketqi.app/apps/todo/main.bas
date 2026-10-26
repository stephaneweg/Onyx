' main.bas -- app vocale « todo » de Pocket : la liste des choses a faire (Onyx BASIC + galo.bi)
'   todo add texte [duree] [AAAA-MM-JJ]        ajoute une chose a faire (duree et echeance facultatives)
'   todo plan mots AAAA-MM-JJ [HH:MM]          la marque prevue ce jour-la (placee dans l'agenda) ; elle reste
'   todo done mots                             la supprime : faite ou plus necessaire
'   todo list [AAAA-MM-JJ]                     dit d'abord ce qui est a planifier, puis ce qui est prevu
'   todo edit mots texte [duree] [AAAA-MM-JJ]  change le texte, la duree, l'echeance ("" : inchange)
'   todo clear                                 vide la liste
'   todo genpdf [fichier]                      PDF de la liste (SD:/tmp/todo.pdf)
' Deux statuts : a faire (pas de date prevue) et prevue (une date). Une date prevue passee sans done : la chose
' redevient a faire (« prevue samedi, pas encore faite »), rien ne se perd.
' La liste : SD:/apps/todo/todo.txt, une ligne par chose : texte <tab> echeance <tab> duree <tab> prevue (dates
' AAAA-MM-JJ, prevue avec l'heure : AAAA-MM-JJ HH:MM). Lancee par le cron (cron = list) : ne parle que s'il y a
' quelque chose.

fichier$ = "SD:/apps/todo/todo.txt"
action$ = LCASE$(Arg$(1))
DIM SHARED tx$(300), ec$(300), du$(300), pr$(300)
n = 0
IF FILEEXISTS(fichier$) THEN
    f = FREEFILE
    OPEN fichier$ FOR INPUT AS #f
    DO WHILE NOT EOF(f)
        LINE INPUT #f, l$
        IF l$ <> "" AND n < 300 THEN
            n = n + 1
            tx$(n) = Champ$(l$, 1)
            ec$(n) = Champ$(l$, 2)
            du$(n) = Champ$(l$, 3)
            pr$(n) = Champ$(l$, 4)
        END IF
    LOOP
    CLOSE #f
END IF

SELECT CASE action$
CASE "add"
    t$ = Arg$(2)
    d$ = Arg$(3)
    e$ = Arg$(4)
    IF t$ = "" THEN
        Say "Je n'ai pas compris quoi noter."
        END
    END IF
    IF e$ <> "" AND IsDate(e$) = 0 THEN e$ = ""
    k = Trouve(t$, n)
    IF k > 0 THEN
        IF WordScore(tx$(k), t$) >= WordScore(t$, t$) THEN
            IF d$ <> "" THEN du$(k) = d$
            IF e$ <> "" THEN ec$(k) = e$
            Say "C'est déjà sur ta liste : " + Dit$(k) + "."
            Ecrit n
            END
        END IF
    END IF
    n = n + 1
    tx$(n) = t$
    du$(n) = d$
    ec$(n) = e$
    pr$(n) = ""
    Say "C'est noté : " + Dit$(n) + "."
    Ecrit n

CASE "plan"
    t$ = Arg$(2)
    d$ = Arg$(3)
    h$ = Arg$(4)
    k = Trouve(t$, n)
    IF IsDate(d$) = 0 THEN
        Say "Je n'ai pas compris quel jour."
    ELSEIF k = 0 THEN
        ' (pas dans la liste : ajoutee, deja prevue)
        n = n + 1
        tx$(n) = t$
        pr$(n) = d$
        IF h$ <> "" THEN pr$(n) = d$ + " " + h$
        Ecrit n
        PRINT "plan : "; t$; " ajoutee et prevue "; pr$(n)
    ELSEIF k < 0 THEN
        Say "Il y en a plusieurs : " + Plusieurs$(t$, n) + ". Laquelle ?"
    ELSE
        pr$(k) = d$
        IF h$ <> "" THEN pr$(k) = d$ + " " + h$
        Ecrit n
        PRINT "plan : "; tx$(k); " prevue "; pr$(k)
    END IF

CASE "done"
    t$ = Arg$(2)
    k = Trouve(t$, n)
    IF k = 0 THEN
        Say "Je ne trouve pas " + t$ + " dans ta liste."
    ELSEIF k < 0 THEN
        Say "Il y en a plusieurs : " + Plusieurs$(t$, n) + ". Laquelle ?"
    ELSE
        Say "C'est retiré de ta liste : " + tx$(k) + "." + Reste$(n - 1)
        tx$(k) = ""
        Ecrit n
    END IF

CASE "edit"
    t$ = Arg$(2)
    k = Trouve(t$, n)
    IF k = 0 THEN
        Say "Je ne trouve pas " + t$ + " dans ta liste."
    ELSEIF k < 0 THEN
        Say "Il y en a plusieurs : " + Plusieurs$(t$, n) + ". Laquelle ?"
    ELSE
        IF Arg$(3) <> "" THEN tx$(k) = Arg$(3)
        IF Arg$(4) <> "" THEN du$(k) = Arg$(4)
        IF Arg$(5) <> "" AND IsDate(Arg$(5)) THEN ec$(k) = Arg$(5)
        Say "C'est changé : " + Dit$(k) + "."
        Ecrit n
    END IF

CASE "list"
    lim$ = Arg$(2)
    IF lim$ <> "" AND IsDate(lim$) = 0 THEN lim$ = ""
    af$ = ""
    pv$ = ""
    na = 0
    np = 0
    FOR i = 1 TO n
        IF tx$(i) <> "" THEN
            IF lim$ = "" OR ec$(i) = "" OR ec$(i) <= lim$ THEN
                IF Prevue(i) THEN
                    np = np + 1
                    IF pv$ <> "" THEN pv$ = pv$ + " ; "
                    pv$ = pv$ + tx$(i) + ", " + SpokenDay$(LEFT$(pr$(i), 10))
                    IF LEN(pr$(i)) > 10 THEN pv$ = pv$ + " à " + SpokenTime$(MID$(pr$(i), 12))
                ELSE
                    na = na + 1
                    IF af$ <> "" THEN af$ = af$ + " ; "
                    af$ = af$ + Dit$(i)
                END IF
            END IF
        END IF
    NEXT
    IF na + np = 0 THEN
        IF ForCron = 0 THEN Say "Tu n'as rien à faire, profite !"
    ELSE
        IF na = 1 THEN Say "À planifier : une chose, " + af$ + "."
        IF na > 1 THEN Say "À planifier : " + LTRIM$(STR$(na)) + " choses, " + af$ + "."
        IF np > 0 THEN Say "Déjà prévu : " + pv$ + "."
    END IF

CASE "clear"
    DeleteFile fichier$
    Say "Ta liste de choses à faire est vide."

CASE "genpdf"
    out$ = Arg$(2)
    IF out$ = "" THEN out$ = "SD:/tmp/todo.pdf"
    d$ = Today$
    PdfOpen "A faire - " + MID$(d$, 9, 2) + "/" + MID$(d$, 6, 2) + "/" + LEFT$(d$, 4)
    FOR i = 1 TO n
        IF tx$(i) <> "" THEN
            l$ = "[  ]  " + tx$(i)
            IF du$(i) <> "" THEN l$ = l$ + "  (" + du$(i) + ")"
            IF ec$(i) <> "" THEN l$ = l$ + "   avant le " + MID$(ec$(i), 9, 2) + "/" + MID$(ec$(i), 6, 2)
            IF Prevue(i) THEN l$ = l$ + "   prevue le " + MID$(pr$(i), 9, 2) + "/" + MID$(pr$(i), 6, 2)
            PdfLine l$
        END IF
    NEXT
    PdfClose out$
    PRINT "PDF : "; out$
    Say "Ta liste de choses à faire est prête en PDF."

CASE ELSE
    Say "La liste de choses à faire ne sait pas faire " + action$ + "."
END SELECT
END

' le k-ieme champ (separe par des tabulations) d'une ligne
FUNCTION Champ$ (l$, k)
    c$ = l$ + CHR$(9)
    r$ = ""
    m = 1
    s = 1
    FOR i = 1 TO LEN(c$)
        IF MID$(c$, i, 1) = CHR$(9) THEN
            IF m = k THEN r$ = MID$(c$, s, i - s)
            m = m + 1
            s = i + 1
        END IF
    NEXT
    Champ$ = r$
END FUNCTION

' prevue a une date qui n'est pas passee (passee sans done : redevenue a faire)
FUNCTION Prevue (i)
    r = 0
    IF pr$(i) <> "" THEN IF LEFT$(pr$(i), 10) >= Today$ THEN r = 1
    Prevue = r
END FUNCTION

' la chose a faire qui a le plus de mots en commun avec t$ : son numero, 0 si aucune, -1 si plusieurs ex aequo
FUNCTION Trouve (t$, n)
    best = 0
    k = 0
    nb = 0
    FOR i = 1 TO n
        IF tx$(i) <> "" THEN
            s = WordScore(tx$(i), t$)
            IF s > best THEN
                best = s
                k = i
                nb = 1
            ELSEIF s = best AND s > 0 THEN
                nb = nb + 1
            END IF
        END IF
    NEXT
    IF nb > 1 THEN k = -1
    Trouve = k
END FUNCTION

FUNCTION Plusieurs$ (t$, n)
    l$ = ""
    FOR i = 1 TO n
        IF tx$(i) <> "" THEN
            IF WordScore(tx$(i), t$) > 0 THEN
                IF l$ <> "" THEN l$ = l$ + ", "
                l$ = l$ + tx$(i)
            END IF
        END IF
    NEXT
    Plusieurs$ = l$
END FUNCTION

' « tondre la pelouse, deux heures, avant vendredi 30 octobre », « ..., prevue samedi passe, pas encore faite »
FUNCTION Dit$ (k)
    r$ = tx$(k)
    IF du$(k) <> "" THEN r$ = r$ + ", " + du$(k)
    IF ec$(k) <> "" THEN r$ = r$ + ", avant " + SpokenDay$(ec$(k))
    IF pr$(k) <> "" AND Prevue(k) = 0 THEN r$ = r$ + ", prévue " + SpokenDay$(LEFT$(pr$(k), 10)) + " mais pas encore faite"
    Dit$ = r$
END FUNCTION

' « Il t'en reste 3. » apres un done
FUNCTION Reste$ (m)
    IF m <= 0 THEN
        Reste$ = " Ta liste est vide."
    ELSEIF m = 1 THEN
        Reste$ = " Il t'en reste une."
    ELSE
        Reste$ = " Il t'en reste " + LTRIM$(STR$(m)) + "."
    END IF
END FUNCTION

SUB Ecrit (n)
    f = FREEFILE
    OPEN "SD:/apps/todo/todo.txt" FOR OUTPUT AS #f
    FOR i = 1 TO n
        IF tx$(i) <> "" THEN PRINT #f, tx$(i) + CHR$(9) + ec$(i) + CHR$(9) + du$(i) + CHR$(9) + pr$(i)
    NEXT
    CLOSE #f
END SUB
