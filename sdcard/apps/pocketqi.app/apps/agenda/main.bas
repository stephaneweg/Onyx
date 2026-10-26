' main.bas -- app vocale « agenda » de Pocket (Onyx BASIC + galo.bi)
'   agenda write AAAA-MM-JJ texte [HH:MM]           ajoute un rendez-vous ou un evenement (heure facultative), le confirme
'   agenda read AAAA-MM-JJ [AAAA-MM-JJ]             dit les rendez-vous d'un jour, ou d'une période (bornes comprises)
'   agenda delete mots [AAAA-MM-JJ]                 supprime le rendez-vous a venir (ou de ce jour) qui a le plus de ces mots
'   agenda edit mots AAAA-MM-JJ [HH:MM]             le déplace à un autre jour (et une autre heure)
'   agenda genpdf AAAA-MM-JJ [AAAA-MM-JJ] [fichier] un PDF du jour ou de la periode (SD:/tmp/agenda.pdf)
'   agenda genmail AAAA-MM-JJ [AAAA-MM-JJ] [fichier] le texte d'un mail du jour ou de la periode, pour sendmail
'                                                   (SD:/tmp/agenda.txt)
' Les rendez-vous sont dans le dossier de l'app : SD:/apps/agenda/agenda.txt, une ligne par rendez-vous :
'   AAAA-MM-JJ <tab> HH:MM <tab> texte   (HH:MM vide : un evenement sans heure, « l'anniversaire de Paul »)
' delete / edit ne touchent à rien si aucun ou plusieurs rendez-vous correspondent : Pocket le dit (et les cite).

fichier$ = "SD:/apps/agenda/agenda.txt"
action$ = LCASE$(Arg$(1))

' tous les rendez-vous (date, heure, texte)
DIM ad$(200), ah$(200), at$(200)
n = 0
IF FILEEXISTS(fichier$) THEN
    f = FREEFILE
    OPEN fichier$ FOR INPUT AS #f
    DO WHILE NOT EOF(f)
        LINE INPUT #f, l$
        p = INSTR(12, l$, CHR$(9))
        IF LEN(l$) > 11 AND p > 0 AND n < 200 THEN
            n = n + 1
            ad$(n) = LEFT$(l$, 10)
            ah$(n) = MID$(l$, 12, p - 12)
            at$(n) = MID$(l$, p + 1)
        END IF
    LOOP
    CLOSE #f
END IF

IF action$ = "write" THEN
    d$ = Arg$(2)
    t$ = Arg$(3)
    h$ = Arg$(4)
    hok = 1
    IF h$ <> "" THEN hok = IsTime(h$)
    IF IsDate(d$) = 0 OR hok = 0 OR t$ = "" THEN
        Say "Je n'ai pas bien compris le jour, l'heure ou le rendez-vous."
    ELSE
        f = FREEFILE
        OPEN fichier$ FOR APPEND AS #f
        PRINT #f, d$ + CHR$(9) + h$ + CHR$(9) + t$
        CLOSE #f
        ' (sans heure, on le dit : la personne entend le trou et peut la donner)
        IF h$ = "" THEN
            Say "C'est noté : " + t$ + ", " + SpokenDay$(d$) + ", sans heure."
        ELSE
            Say "C'est noté : " + t$ + ", " + SpokenDay$(d$) + AtTime$(h$) + "."
        END IF
    END IF

ELSEIF action$ = "read" OR action$ = "genpdf" THEN
    d1$ = Arg$(2)
    d2$ = Arg$(3)
    out$ = Arg$(4)
    IF out$ = "" THEN out$ = "SD:/tmp/agenda.pdf"
    IF d2$ = "" THEN d2$ = d1$
    IF IsDate(d1$) = 0 OR IsDate(d2$) = 0 THEN
        Say "Je n'ai pas compris de quel jour tu parles."
        END
    END IF
    IF d2$ < d1$ THEN SWAP d1$, d2$
    periode = d1$ <> d2$
    ' ceux de la période, dans l'ordre (date puis heure : tri par insertion)
    DIM ds$(200), hs$(200), ts$(200)
    m = 0
    FOR i = 1 TO n
        IF ad$(i) >= d1$ AND ad$(i) <= d2$ THEN
            m = m + 1
            ds$(m) = ad$(i)
            hs$(m) = ah$(i)
            ts$(m) = at$(i)
        END IF
    NEXT
    FOR i = 2 TO m
        k = i
        DO WHILE k > 1
            IF ds$(k - 1) + hs$(k - 1) <= ds$(k) + hs$(k) THEN EXIT DO
            SWAP ds$(k - 1), ds$(k)
            SWAP hs$(k - 1), hs$(k)
            SWAP ts$(k - 1), ts$(k)
            k = k - 1
        LOOP
    NEXT
    IF action$ = "genpdf" THEN
        ' (le document imprimé : des dates en clair, pas « demain » ; rien n'est dit)
        ' Un jour : sa grille des heures (comme la vue « jour » d'un calendrier) ; une période : un cadre par jour.
        IF m = 0 THEN
            ' (rien dans la période : pas de PDF, donc rien à imprimer ; print execute le voit et se tait)
            DeleteFile out$
            IF periode THEN quand$ = "entre " + SpokenDay$(d1$) + " et " + SpokenDay$(d2$) ELSE quand$ = SpokenDay$(d1$)
            Say "Tu n'as rien de prévu " + quand$ + " : il n'y a rien à imprimer."
            END
        END IF
        PdfBegin
        IF periode THEN st$ = "Du " + LongDate$(d1$) + " au " + LongDate$(d2$) ELSE st$ = LongDate$(d1$)
        AgendaHeader st$
        y = 104
        IF periode = 0 THEN
            ' -- un jour --
            nj = DayNum(d1$)
            PdfBox 40, y, 40, 40, 20, "4A90A4"
            dn$ = LTRIM$(STR$(VAL(MID$(d1$, 9, 2))))
            PdfText 60 - PdfTextWidth(dn$, 18, "B") / 2, y + 26.5, 18, "B", "FFFFFF", dn$
            PdfText 92, y + 17, 15, "B", "4A90A4", WeekdayName$(nj)
            PdfText 92, y + 33, 10, "", "6B7A80", "Semaine " + LTRIM$(STR$(WeekNum(nj)))
            y = y + 58
            ' les événements sans heure, en haut (« toute la journée »)
            na = 0
            FOR i = 1 TO m
                IF hs$(i) = "" THEN
                    IF na = 0 THEN PdfText 40, y + 14, 8, "", "6B7A80", "toute la journée"
                    k = Kind(ts$(i))
                    PdfBox 104, y, 451, 21, 4, FillCol$(k)
                    PdfBox 104, y, 3.5, 21, 0, AccentCol$(k)
                    PdfText 114, y + 14.5, 10.5, "B", TextCol$(k), Cap$(ts$(i))
                    y = y + 25
                    na = na + 1
                END IF
            NEXT
            IF na > 0 THEN y = y + 8
            ' la grille : de 8 h à 20 h, élargie aux heures des rendez-vous
            h1 = 8
            h2 = 20
            FOR i = 1 TO m
                IF hs$(i) <> "" THEN
                    hh = VAL(LEFT$(hs$(i), 2))
                    IF hh < h1 THEN h1 = hh
                    IF hh + 1 > h2 THEN h2 = hh + 1
                END IF
            NEXT
            rowh = (790 - y) / (h2 - h1)
            IF rowh > 48 THEN rowh = 48
            gy = y
            PdfBox 100, gy, 455, rowh * (h2 - h1), 0, "F4F6F7"
            FOR hh = h1 TO h2
                ly = gy + (hh - h1) * rowh
                PdfRule 100, ly, 555, ly, 0.5, "D5DCDF"
                hl$ = LTRIM$(STR$(hh)) + ":00"
                IF hh < h2 THEN PdfText 92 - PdfTextWidth(hl$, 9, ""), ly + 4, 9, "", "6B7A80", hl$
            NEXT
            PdfRule 100, gy, 100, gy + rowh * (h2 - h1), 0.5, "D5DCDF"
            FOR i = 1 TO m
                IF hs$(i) <> "" THEN
                    k = Kind(ts$(i))
                    t = VAL(LEFT$(hs$(i), 2)) + VAL(MID$(hs$(i), 4, 2)) / 60
                    ey = gy + (t - h1) * rowh + 1
                    eh = rowh - 2
                    IF eh < 28 THEN eh = 28
                    PdfBox 104, ey, 448, eh, 4, FillCol$(k)
                    PdfBox 104, ey, 3.5, eh, 0, AccentCol$(k)
                    PdfText 114, ey + 14, 10.5, "B", TextCol$(k), Cap$(ts$(i))
                    PdfText 114, ey + 26, 9, "", TextCol$(k), hs$(i)
                END IF
            NEXT
            IF m = 0 THEN PdfText 114, gy + 20, 11, "I", "8A979C", "Rien de prévu."
        ELSE
            ' -- une période : chaque jour, ses rendez-vous dans l'ordre --
            FOR nj = DayNum(d1$) TO DayNum(d2$)
                dj$ = Civil$(nj)
                c = 0
                FOR i = 1 TO m
                    IF ds$(i) = dj$ THEN c = c + 1
                NEXT
                need = 32
                IF c > 0 THEN need = 8 + 34 * c
                IF y + need > 800 THEN
                    AgendaFooter
                    PdfNewPage
                    AgendaHeader st$
                    y = 104
                END IF
                wd = (nj + 4) MOD 7
                IF wd = 0 OR wd = 6 THEN dc$ = "C0504D" ELSE dc$ = "2B3A42"
                IF c > 0 THEN bc$ = "4A90A4" ELSE bc$ = "E3E8EA"
                IF c > 0 THEN nc$ = "FFFFFF" ELSE nc$ = "6B7A80"
                PdfBox 40, y + 2, 26, 26, 13, bc$
                dn$ = LTRIM$(STR$(VAL(MID$(dj$, 9, 2))))
                PdfText 53 - PdfTextWidth(dn$, 12, "B") / 2, y + 19.5, 12, "B", nc$, dn$
                PdfText 76, y + 13, 11, "B", dc$, WeekdayName$(nj)
                PdfText 76, y + 25, 8.5, "", "6B7A80", MonthDay$(dj$)
                IF c = 0 THEN
                    PdfText 180, y + 19, 9.5, "I", "8A979C", "Rien de prévu"
                    y = y + 34
                ELSE
                    ey = y
                    FOR i = 1 TO m
                        IF ds$(i) = dj$ THEN
                            k = Kind(ts$(i))
                            PdfBox 180, ey, 375, 30, 4, FillCol$(k)
                            PdfBox 180, ey, 3.5, 30, 0, AccentCol$(k)
                            PdfText 190, ey + 13, 10.5, "B", TextCol$(k), Cap$(ts$(i))
                            IF hs$(i) = "" THEN q$ = "toute la journée" ELSE q$ = hs$(i)
                            PdfText 190, ey + 25, 8.5, "", TextCol$(k), q$
                            ey = ey + 34
                        END IF
                    NEXT
                    y = ey + 4
                END IF
                PdfRule 40, y - 2, 555, y - 2, 0.4, "E3E8EA"
                y = y + 2
            NEXT
        END IF
        AgendaFooter
        PdfEnd out$
        PRINT "PDF : "; out$
        Say "L'agenda est prêt en PDF."
        END
    END IF
    IF periode THEN quand$ = "entre " + SpokenDay$(d1$) + " et " + SpokenDay$(d2$) ELSE quand$ = SpokenDay$(d1$)
    IF m = 0 THEN
        Say "Tu n'as rien de prévu " + quand$ + "."
    ELSE
        IF m = 1 THEN combien$ = "un rendez-vous" ELSE combien$ = LTRIM$(STR$(m)) + " rendez-vous"
        Say UCASE$(LEFT$(quand$, 1)) + MID$(quand$, 2) + ", tu as " + combien$ + "."
        FOR i = 1 TO m
            IF periode THEN
                jour$ = SpokenDay$(ds$(i))
                Say UCASE$(LEFT$(jour$, 1)) + MID$(jour$, 2) + ", " + ts$(i) + AtTime$(hs$(i)) + "."
            ELSE
                Say ts$(i) + AtTime$(hs$(i)) + "."
            END IF
        NEXT
    END IF

ELSEIF action$ = "genmail" THEN
    ' le texte d'un mail des rendez-vous d'un jour ou d'une période (galo.bi : MailDraft), pour sendmail ; rien
    ' n'est dit. Des dates en clair, pas « demain » : le mail est lu plus tard.
    d1$ = Arg$(2)
    d2$ = Arg$(3)
    out$ = Arg$(4)
    IF UCASE$(LEFT$(d2$, 3)) = "SD:" THEN
        ' (un jour seul, puis le fichier)
        out$ = d2$
        d2$ = ""
    END IF
    IF out$ = "" THEN out$ = "SD:/tmp/agenda.txt"
    IF d2$ = "" THEN d2$ = d1$
    IF IsDate(d1$) = 0 OR IsDate(d2$) = 0 THEN
        Say "Je n'ai pas compris de quel jour tu parles."
        END
    END IF
    IF d2$ < d1$ THEN SWAP d1$, d2$
    DIM md$(200), mh$(200), mt$(200)
    m = 0
    FOR i = 1 TO n
        IF ad$(i) >= d1$ AND ad$(i) <= d2$ THEN
            m = m + 1
            md$(m) = ad$(i)
            mh$(m) = ah$(i)
            mt$(m) = at$(i)
        END IF
    NEXT
    FOR i = 2 TO m
        k = i
        DO WHILE k > 1
            IF md$(k - 1) + mh$(k - 1) <= md$(k) + mh$(k) THEN EXIT DO
            SWAP md$(k - 1), md$(k)
            SWAP mh$(k - 1), mh$(k)
            SWAP mt$(k - 1), mt$(k)
            k = k - 1
        LOOP
    NEXT
    IF d1$ <> d2$ THEN s$ = "Agenda du " + DayDate$(d1$) + " au " + DayDate$(d2$) ELSE s$ = "Agenda du " + DayDate$(d1$)
    t$ = "Bonjour," + CHR$(10) + CHR$(10) + "Voici l'agenda" + MID$(s$, 7) + " :" + CHR$(10)
    IF m = 0 THEN t$ = t$ + CHR$(10) + "Rien de prévu." + CHR$(10)
    FOR i = 1 TO m
        IF i = 1 THEN
            t$ = t$ + CHR$(10) + DayDate$(md$(i)) + CHR$(10)
        ELSEIF md$(i) <> md$(i - 1) THEN
            t$ = t$ + CHR$(10) + DayDate$(md$(i)) + CHR$(10)
        END IF
        IF mh$(i) = "" THEN t$ = t$ + "  - " + mt$(i) + CHR$(10) ELSE t$ = t$ + "  - " + mh$(i) + "  " + mt$(i) + CHR$(10)
    NEXT
    MailDraft out$, s$, t$
    PRINT "Mail : "; out$

ELSEIF action$ = "delete" OR action$ = "edit" THEN
    ' delete mots [date] ; edit mots nouvelle-date [nouvelle-heure] : le LLM ne connait pas l'agenda, il donne des
    ' mots ; on cherche parmi les rendez-vous a venir (ou ce jour-la, si delete le donne)
    mot$ = LCASE$(Arg$(2)) + " "
    d$ = ""
    IF action$ = "delete" THEN d$ = Arg$(3)
    IF d$ <> "" AND IsDate(d$) = 0 THEN d$ = ""
    IF LTRIM$(mot$) = "" THEN
        Say "Je n'ai pas compris quel rendez-vous tu veux changer."
        END
    END IF
    ' le score d'un rendez-vous : le nombre de mots (3 lettres et plus) de mot$ dans son texte ; le meilleur seul
    auj$ = Today$
    best = 0
    trouve = 0
    k = 0
    DIM sc(200)
    FOR i = 1 TO n
        sc(i) = 0
        ok = 0
        IF d$ <> "" THEN
            IF ad$(i) = d$ THEN ok = 1
        ELSE
            IF ad$(i) >= auj$ THEN ok = 1
        END IF
        IF ok THEN
            t$ = LCASE$(at$(i))
            s = 1
            FOR j = 1 TO LEN(mot$)
                IF MID$(mot$, j, 1) = " " THEN
                    w$ = MID$(mot$, s, j - s)
                    ' (un mot long sans ses deux dernieres lettres : « Alexandre » mal entendu trouve « Alexandra »)
                    IF LEN(w$) >= 6 THEN w$ = LEFT$(w$, LEN(w$) - 2)
                    IF LEN(w$) >= 3 THEN
                        IF INSTR(t$, w$) > 0 THEN sc(i) = sc(i) + 1
                    END IF
                    s = j + 1
                END IF
            NEXT
            IF sc(i) > best THEN best = sc(i): trouve = 0
            IF sc(i) = best AND best > 0 THEN trouve = trouve + 1: k = i
        END IF
    NEXT
    IF trouve = 0 THEN
        Say "Je n'ai pas trouvé de rendez-vous " + Arg$(2) + "."
        END
    END IF
    IF trouve > 1 THEN
        l$ = ""
        FOR i = 1 TO n
            IF sc(i) = best THEN l$ = l$ + " ; " + at$(i) + ", " + SpokenDay$(ad$(i)) + AtTime$(ah$(i))
        NEXT
        Say "J'en ai trouvé " + LTRIM$(STR$(trouve)) + " : " + MID$(l$, 4) + ". Lequel ?"
        END
    END IF
    avant$ = at$(k) + ", " + SpokenDay$(ad$(k)) + AtTime$(ah$(k))
    IF action$ = "delete" THEN
        ad$(k) = ""
        Say "J'ai supprimé : " + avant$ + "."
    ELSE
        nd$ = Arg$(3)
        nh$ = Arg$(4)
        IF nh$ = "" THEN nh$ = ah$(k)
        hok = 1
        IF nh$ <> "" THEN hok = IsTime(nh$)
        IF IsDate(nd$) = 0 OR hok = 0 THEN
            Say "Je n'ai pas compris le nouveau jour ou la nouvelle heure."
            END
        END IF
        ad$(k) = nd$
        ah$(k) = nh$
        Say "C'est déplacé : " + at$(k) + ", " + SpokenDay$(nd$) + AtTime$(nh$) + "."
    END IF
    ' on réécrit tout l'agenda (les supprimés ont une date vide)
    f = FREEFILE
    OPEN fichier$ FOR OUTPUT AS #f
    FOR i = 1 TO n
        IF ad$(i) <> "" THEN PRINT #f, ad$(i) + CHR$(9) + ah$(i) + CHR$(9) + at$(i)
    NEXT
    CLOSE #f

ELSE
    Say "L'agenda ne sait pas faire " + action$ + "."
END IF
END

' « à 14 heures » (avec la virgule devant), rien pour un evenement sans heure
FUNCTION AtTime$ (h$)
    IF h$ = "" THEN AtTime$ = "" ELSE AtTime$ = ", à " + SpokenTime$(h$)
END FUNCTION

' « vendredi 23/10/2026 » (pour un document imprime)
FUNCTION DayDate$ (d$)
    SELECT CASE (DayNum(d$) + 4) MOD 7
    CASE 0: j$ = "dimanche"
    CASE 1: j$ = "lundi"
    CASE 2: j$ = "mardi"
    CASE 3: j$ = "mercredi"
    CASE 4: j$ = "jeudi"
    CASE 5: j$ = "vendredi"
    CASE 6: j$ = "samedi"
    END SELECT
    DayDate$ = j$ + " " + MID$(d$, 9, 2) + "/" + MID$(d$, 6, 2) + "/" + LEFT$(d$, 4)
END FUNCTION

' ---- la mise en page du PDF -----------------------------------------------------------------------------------
' l'en-tête d'une page : « Agenda », la période, un trait
SUB AgendaHeader (st$)
    PdfText 40, 62, 26, "B", "2B3A42", "Agenda"
    PdfText 555 - PdfTextWidth(st$, 11, ""), 62, 11, "", "4A90A4", st$
    PdfRule 40, 78, 555, 78, 1.5, "4A90A4"
END SUB

' le pied de page : imprimé par Pocket, le jour
SUB AgendaFooter
    d$ = Today$
    PdfRule 40, 812, 555, 812, 0.5, "D5DCDF"
    PdfText 40, 826, 8, "", "8A979C", "Imprimé par Pocket le " + MID$(d$, 9, 2) + "/" + MID$(d$, 6, 2) + "/" + LEFT$(d$, 4)
END SUB

' le genre d'un rendez-vous, pour sa couleur : 1 bleu (le reste), 2 vert (santé), 3 rose (anniversaire),
' 4 orange (école, fête), 5 violet (sport, loisirs)
FUNCTION Kind (t$)
    l$ = LCASE$(t$)
    k = 1
    IF INSTR(l$, "dentist") OR INSTR(l$, "médecin") OR INSTR(l$, "medecin") OR INSTR(l$, "docteur") OR INSTR(l$, "kiné") OR INSTR(l$, "pharmac") OR INSTR(l$, "hôpital") THEN k = 2
    IF INSTR(l$, "anniv") THEN k = 3
    IF INSTR(l$, "école") OR INSTR(l$, "ecole") OR INSTR(l$, "fête") OR INSTR(l$, "fete") OR INSTR(l$, "réunion") OR INSTR(l$, "sortie") THEN k = 4
    IF INSTR(l$, "sport") OR INSTR(l$, "piano") OR INSTR(l$, "cours") OR INSTR(l$, "foot") OR INSTR(l$, "danse") OR INSTR(l$, "judo") OR INSTR(l$, "piscine") THEN k = 5
    Kind = k
END FUNCTION

FUNCTION FillCol$ (k)
    SELECT CASE k
    CASE 2: FillCol$ = "C9EBDD"
    CASE 3: FillCol$ = "FBE1EE"
    CASE 4: FillCol$ = "FDEBC8"
    CASE 5: FillCol$ = "E8E2FB"
    CASE ELSE: FillCol$ = "DEE7F6"
    END SELECT
END FUNCTION

FUNCTION AccentCol$ (k)
    SELECT CASE k
    CASE 2: AccentCol$ = "17A673"
    CASE 3: AccentCol$ = "D6457F"
    CASE 4: AccentCol$ = "E39A16"
    CASE 5: AccentCol$ = "7C5CD6"
    CASE ELSE: AccentCol$ = "3B78E7"
    END SELECT
END FUNCTION

FUNCTION TextCol$ (k)
    SELECT CASE k
    CASE 2: TextCol$ = "0B5E41"
    CASE 3: TextCol$ = "8C1F4F"
    CASE 4: TextCol$ = "86560A"
    CASE 5: TextCol$ = "4A3192"
    CASE ELSE: TextCol$ = "41608F"
    END SELECT
END FUNCTION

' jours depuis le 01/01/1970 -> AAAA-MM-JJ (H. Hinnant, civil_from_days)
FUNCTION Civil$ (n)
    z = n + 719468
    era = INT(z / 146097)
    doe = z - era * 146097
    yoe = INT((doe - INT(doe / 1460) + INT(doe / 36524) - INT(doe / 146096)) / 365)
    doy = doe - (365 * yoe + INT(yoe / 4) - INT(yoe / 100))
    mp = INT((5 * doy + 2) / 153)
    dd = doy - INT((153 * mp + 2) / 5) + 1
    IF mp < 10 THEN mm = mp + 3 ELSE mm = mp - 9
    yy = yoe + era * 400
    IF mm <= 2 THEN yy = yy + 1
    Civil$ = LTRIM$(STR$(yy)) + "-" + RIGHT$("0" + LTRIM$(STR$(mm)), 2) + "-" + RIGHT$("0" + LTRIM$(STR$(dd)), 2)
END FUNCTION

' « Lundi »
FUNCTION WeekdayName$ (n)
    SELECT CASE (n + 4) MOD 7
    CASE 0: WeekdayName$ = "Dimanche"
    CASE 1: WeekdayName$ = "Lundi"
    CASE 2: WeekdayName$ = "Mardi"
    CASE 3: WeekdayName$ = "Mercredi"
    CASE 4: WeekdayName$ = "Jeudi"
    CASE 5: WeekdayName$ = "Vendredi"
    CASE 6: WeekdayName$ = "Samedi"
    END SELECT
END FUNCTION

' « 26 octobre »
FUNCTION MonthDay$ (d$)
    SELECT CASE VAL(MID$(d$, 6, 2))
    CASE 1: m$ = "janvier"
    CASE 2: m$ = "février"
    CASE 3: m$ = "mars"
    CASE 4: m$ = "avril"
    CASE 5: m$ = "mai"
    CASE 6: m$ = "juin"
    CASE 7: m$ = "juillet"
    CASE 8: m$ = "août"
    CASE 9: m$ = "septembre"
    CASE 10: m$ = "octobre"
    CASE 11: m$ = "novembre"
    CASE ELSE: m$ = "décembre"
    END SELECT
    j = VAL(MID$(d$, 9, 2))
    IF j = 1 THEN j$ = "1er" ELSE j$ = LTRIM$(STR$(j))
    MonthDay$ = j$ + " " + m$
END FUNCTION

' « lundi 26 octobre 2026 »
FUNCTION LongDate$ (d$)
    LongDate$ = LCASE$(WeekdayName$(DayNum(d$))) + " " + MonthDay$(d$) + " " + LEFT$(d$, 4)
END FUNCTION

' le numéro de la semaine (ISO 8601 : celle du jeudi)
FUNCTION WeekNum (n)
    wd = (n + 3) MOD 7
    th = n - wd + 3
    y$ = LEFT$(Civil$(th), 4)
    WeekNum = INT((th - DayNum(y$ + "-01-01")) / 7) + 1
END FUNCTION

' la première lettre en majuscule
FUNCTION Cap$ (t$)
    Cap$ = UCASE$(LEFT$(t$, 1)) + MID$(t$, 2)
END FUNCTION
