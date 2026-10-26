' main.bas -- app vocale « courses » de Pocket (Onyx BASIC + galo.bi)
'   courses add article [quantite]       ajoute (ou augmente la quantite si l'article y est deja)
'   courses remove article               retire
'   courses change article quantite      change la quantite ; +1 / -2 : en ajoute / en retire (0 ou moins : retire)
'   courses list                         dit la liste
'   courses clear                        vide la liste
'   courses genpdf [fichier]             fait un PDF de la liste (SD:/tmp/courses.pdf par defaut)
'   courses genmail [fichier]            fait le texte d'un mail de la liste, pour sendmail (SD:/tmp/courses.txt)
' La liste : SD:/apps/courses/liste.txt, une ligne par article : article <tab> quantite (quantite vide : pas dite).

fichier$ = "SD:/apps/courses/liste.txt"
action$ = LCASE$(Arg$(1))
DIM SHARED art$(300), qte$(300)
n = 0
IF FILEEXISTS(fichier$) THEN
    f = FREEFILE
    OPEN fichier$ FOR INPUT AS #f
    DO WHILE NOT EOF(f)
        LINE INPUT #f, l$
        p = INSTR(l$, CHR$(9))
        IF l$ <> "" AND n < 300 THEN
            n = n + 1
            IF p > 0 THEN
                art$(n) = LEFT$(l$, p - 1)
                qte$(n) = MID$(l$, p + 1)
            ELSE
                art$(n) = l$
                qte$(n) = ""
            END IF
        END IF
    LOOP
    CLOSE #f
END IF

SELECT CASE action$
CASE "add"
    a$ = Arg$(2)
    q$ = Arg$(3)
    IF a$ = "" THEN
        Say "Je n'ai pas compris quoi ajouter."
        END
    END IF
    k = Trouve(a$, n)
    IF k > 0 THEN
        ' (deja sur la liste : les quantites s'additionnent si ce sont des nombres, sinon la nouvelle remplace)
        IF q$ <> "" THEN
            IF VAL(q$) > 0 AND VAL(qte$(k)) > 0 THEN
                qte$(k) = Somme$(qte$(k), VAL(q$))
            ELSE
                qte$(k) = q$
            END IF
        END IF
        Say "C'est déjà sur la liste : " + Dit$(art$(k), qte$(k)) + "."
    ELSE
        n = n + 1
        art$(n) = a$
        qte$(n) = q$
        Say "J'ajoute " + Dit$(a$, q$) + "."
    END IF
    Ecrit n

CASE "remove"
    a$ = Arg$(2)
    k = Trouve(a$, n)
    IF k = 0 THEN
        Say a$ + " n'est pas sur la liste."
    ELSEIF k < 0 THEN
        Say "Il y en a plusieurs : " + Plusieurs$(a$, n) + ". Lequel ?"
    ELSE
        Say "Je retire " + art$(k) + "."
        art$(k) = ""
        Ecrit n
    END IF

CASE "change"
    a$ = Arg$(2)
    q$ = Arg$(3)
    k = Trouve(a$, n)
    rel = 0
    IF LEFT$(q$, 1) = "+" OR LEFT$(q$, 1) = "-" THEN rel = 1
    IF k < 0 THEN
        Say "Il y en a plusieurs : " + Plusieurs$(a$, n) + ". Lequel ?"
        END
    END IF
    IF k = 0 THEN
        IF rel = 1 AND VAL(q$) <= 0 THEN
            Say a$ + " n'est pas sur la liste."
            END
        END IF
        n = n + 1
        k = n
        art$(k) = a$
        IF rel = 1 THEN qte$(k) = LTRIM$(STR$(VAL(q$))) ELSE qte$(k) = q$
    ELSEIF rel = 1 THEN
        v = VAL(qte$(k))
        IF v = 0 THEN v = 1
        IF v + VAL(q$) <= 0 THEN
            Say "Je retire " + art$(k) + "."
            art$(k) = ""
            Ecrit n
            END
        END IF
        IF VAL(qte$(k)) > 0 THEN qte$(k) = Somme$(qte$(k), VAL(q$)) ELSE qte$(k) = LTRIM$(STR$(v + VAL(q$)))
    ELSE
        qte$(k) = q$
    END IF
    Say "C'est noté : " + Dit$(art$(k), qte$(k)) + "."
    Ecrit n

CASE "list"
    l$ = ""
    m = 0
    FOR i = 1 TO n
        IF art$(i) <> "" THEN
            m = m + 1
            IF l$ <> "" THEN l$ = l$ + " ; "
            l$ = l$ + Dit$(art$(i), qte$(i))
        END IF
    NEXT
    IF m = 0 THEN
        Say "La liste de courses est vide."
    ELSE
        Say "Sur la liste : " + l$ + "."
    END IF

CASE "clear"
    DeleteFile fichier$
    Say "La liste de courses est vide."

CASE "genpdf"
    out$ = Arg$(2)
    IF out$ = "" THEN out$ = "SD:/tmp/courses.pdf"
    d$ = Today$
    jour$ = MID$(d$, 9, 2) + "/" + MID$(d$, 6, 2) + "/" + LEFT$(d$, 4)
    c = 0
    FOR i = 1 TO n
        IF art$(i) <> "" THEN c = c + 1
    NEXT
    ' un bandeau (titre, date, nombre d'articles), puis une ligne par article : une case à cocher, l'article, la
    ' quantité à droite ; une ligne sur deux sur fond gris clair
    PdfBegin
    PdfBox 40, 40, 515, 68, 10, "4A90A4"
    PdfText 60, 80, 24, "B", "FFFFFF", "Liste de courses"
    PdfText 60, 97, 10.5, "", "DCEEF3", "du " + jour$
    IF c = 1 THEN cn$ = "1 article" ELSE cn$ = LTRIM$(STR$(c)) + " articles"
    PdfText 535 - PdfTextWidth(cn$, 12, "B"), 80, 12, "B", "FFFFFF", cn$
    y = 128
    r = 0
    FOR i = 1 TO n
        IF art$(i) <> "" THEN
            IF y > 780 THEN
                CoursesFooter jour$
                PdfNewPage
                PdfText 40, 60, 14, "B", "4A90A4", "Liste de courses (suite)"
                PdfRule 40, 70, 555, 70, 1, "4A90A4"
                y = 86
            END IF
            r = r + 1
            IF r MOD 2 = 0 THEN PdfBox 40, y, 515, 30, 5, "F2F5F6"
            PdfFrame 54, y + 9, 12, 12, 3, 1.3, "4A90A4"
            PdfText 78, y + 19.5, 12.5, "", "2B3A42", UCASE$(LEFT$(art$(i), 1)) + MID$(art$(i), 2)
            IF qte$(i) <> "" THEN PdfText 541 - PdfTextWidth(qte$(i), 11, ""), y + 19.5, 11, "", "6B7A80", qte$(i)
            y = y + 30
        END IF
    NEXT
    IF c = 0 THEN PdfText 60, y + 20, 12, "I", "8A979C", "La liste est vide."
    CoursesFooter jour$
    PdfEnd out$
    PRINT "PDF : "; out$
    Say "La liste de courses est prête en PDF."

CASE "genmail"
    ' le texte d'un mail (galo.bi : MailDraft), pour sendmail ; rien n'est dit
    out$ = Arg$(2)
    IF out$ = "" THEN out$ = "SD:/tmp/courses.txt"
    d$ = Today$
    t$ = ""
    FOR i = 1 TO n
        IF art$(i) <> "" THEN
            t$ = t$ + "- " + art$(i)
            IF qte$(i) <> "" THEN t$ = t$ + " (" + qte$(i) + ")"
            t$ = t$ + CHR$(10)
        END IF
    NEXT
    IF t$ = "" THEN
        Say "La liste de courses est vide, il n'y a rien à envoyer."
        END
    END IF
    MailDraft out$, "Liste de courses du " + MID$(d$, 9, 2) + "/" + MID$(d$, 6, 2) + "/" + LEFT$(d$, 4), "Bonjour," + CHR$(10) + CHR$(10) + "Voici la liste de courses :" + CHR$(10) + CHR$(10) + t$
    PRINT "Mail : "; out$

CASE ELSE
    Say "La liste de courses ne sait pas faire " + action$ + "."
END SELECT
END

' l'article de la liste qui a le plus de mots en commun avec a$ : son numero, 0 si aucun, -1 si plusieurs ex aequo
FUNCTION Trouve (a$, n)
    best = 0
    k = 0
    nb = 0
    FOR i = 1 TO n
        IF art$(i) <> "" THEN
            s = WordScore(art$(i), a$)
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

' ceux qui correspondent, pour demander lequel
FUNCTION Plusieurs$ (a$, n)
    l$ = ""
    FOR i = 1 TO n
        IF art$(i) <> "" THEN
            IF WordScore(art$(i), a$) > 0 THEN
                IF l$ <> "" THEN l$ = l$ + ", "
                l$ = l$ + art$(i)
            END IF
        END IF
    NEXT
    Plusieurs$ = l$
END FUNCTION

' « lait, 2 litres » ou « lait »
FUNCTION Dit$ (a$, q$)
    IF q$ = "" THEN Dit$ = a$ ELSE Dit$ = a$ + ", " + q$
END FUNCTION

' « 2 litres » + 1 -> « 3 litres » (le nombre du debut change, l'unite reste)
FUNCTION Somme$ (q$, d)
    v = VAL(q$)
    i = 1
    DO WHILE i <= LEN(q$)
        IF INSTR("0123456789.,", MID$(q$, i, 1)) = 0 THEN EXIT DO
        i = i + 1
    LOOP
    Somme$ = LTRIM$(STR$(v + d)) + MID$(q$, i)
END FUNCTION

' la liste reecrite (les retires ont un article vide)
SUB Ecrit (n)
    f = FREEFILE
    OPEN "SD:/apps/courses/liste.txt" FOR OUTPUT AS #f
    FOR i = 1 TO n
        IF art$(i) <> "" THEN PRINT #f, art$(i) + CHR$(9) + qte$(i)
    NEXT
    CLOSE #f
END SUB

' le pied de page du PDF : imprimé par Pocket, le jour
SUB CoursesFooter (jour$)
    PdfRule 40, 812, 555, 812, 0.5, "D5DCDF"
    PdfText 40, 826, 8, "", "8A979C", "Imprimé par Pocket le " + jour$
END SUB
