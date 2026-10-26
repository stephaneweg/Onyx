' galo.bi -- la bibliotheque des apps vocales de Pocket pour Onyx BASIC (pocketqi_win, pocket_vm, PocketQI).
' Ajoutee APRES le programme a la compilation, comme robot.bi : les numeros de ligne du programme ne changent pas.
' Aucun mot nouveau dans le langage : tout passe par COMMAND$ et par des peripheriques.
'
'   SAY:      une phrase a dire (OPEN "SAY:" FOR OUTPUT, PRINT #, CLOSE) : PicoTTS sur le robot et sur le PC.
'   ASKINFO:  FILEEXISTS("ASKINFO:") vrai quand l'app est lancee pour le LLM (outcome askinfo) : voir ForLLM.
'   https://  le web (REST) : OPEN "[METHODE ]http(s)://..." ; FOR INPUT lit la reponse (GET par defaut),
'             FOR OUTPUT ecrit le corps, envoye au CLOSE (POST par defaut) ; HEADERS: les en-tetes (une ligne
'             « Nom: valeur » chacun), STATUS: le code HTTP de la derniere requete, RESPONSE: son corps.
'             Plus simple : Web$, WebSend$, WebHeader, WebStatus, et JsonRaw$ / JsonText$ pour lire du JSON.
'   COMMAND$  les arguments de l'app, separes par des tabulations : l'outcome du LLM
'             {"app": "agenda", "args": ["write", "2026-10-24", "14:00", "dentiste"]} devient
'             COMMAND$ = "write" + TAB + "2026-10-24" + TAB + "14:00" + TAB + "dentiste".
'   MOOD:     l'humeur (le visage) : "60 40" (valence, eveil) ou "joyeux 2" a ecrire, "60 40 joyeux 2" a lire :
'             voir SetMood.
'   LLM:      une phrase pour le LLM, traitee a la fin de l'app comme si on l'avait dite : voir SendLLM.
'   SCREEN:   l'ecran de Pocket : une scene a ecrire, sa taille a lire ; TOUCH: le dernier toucher : voir ScreenBegin.
'   Dates : AAAA-MM-JJ ; heures : HH:MM (le shell les a verifiees avant de lancer l'app).
' Licence MIT.

' ---- parler ----------------------------------------------------------------------------------------------------
SUB Say (t$)
    f = FREEFILE
    OPEN "SAY:" FOR OUTPUT AS #f
    PRINT #f, t$
    CLOSE #f
END SUB

' ---- les arguments ---------------------------------------------------------------------------------------------
FUNCTION ArgCount
    c$ = COMMAND$
    n = 0
    IF c$ <> "" THEN
        n = 1
        FOR i = 1 TO LEN(c$)
            IF MID$(c$, i, 1) = CHR$(9) THEN n = n + 1
        NEXT
    END IF
    ArgCount = n
END FUNCTION

' Arg$(1) : le premier argument (l'action), Arg$(2)... ; "" s'il n'y en a pas autant
FUNCTION Arg$ (k)
    c$ = COMMAND$ + CHR$(9)
    r$ = ""
    n = 1
    s = 1
    FOR i = 1 TO LEN(c$)
        IF MID$(c$, i, 1) = CHR$(9) THEN
            IF n = k THEN r$ = MID$(c$, s, i - s)
            n = n + 1
            s = i + 1
        END IF
    NEXT
    Arg$ = r$
END FUNCTION

' ---- les dates -------------------------------------------------------------------------------------------------
' AAAA-MM-JJ -> jours depuis le 01/01/1970 (H. Hinnant, days_from_civil)
FUNCTION DayNum (d$)
    y = VAL(LEFT$(d$, 4))
    m = VAL(MID$(d$, 6, 2))
    j = VAL(MID$(d$, 9, 2))
    IF m <= 2 THEN y = y - 1
    era = INT(y / 400)
    yoe = y - era * 400
    IF m > 2 THEN mp = m - 3 ELSE mp = m + 9
    doy = INT((153 * mp + 2) / 5) + j - 1
    doe = yoe * 365 + INT(yoe / 4) - INT(yoe / 100) + doy
    DayNum = era * 146097 + doe - 719468
END FUNCTION

' la date du jour, AAAA-MM-JJ (DATE$ est MM-JJ-AAAA)
FUNCTION Today$
    d$ = DATE$
    Today$ = RIGHT$(d$, 4) + "-" + LEFT$(d$, 2) + "-" + MID$(d$, 4, 2)
END FUNCTION

' une date valide AAAA-MM-JJ ?
FUNCTION IsDate (d$)
    ok = 0
    IF LEN(d$) = 10 AND MID$(d$, 5, 1) = "-" AND MID$(d$, 8, 1) = "-" THEN
        m = VAL(MID$(d$, 6, 2))
        j = VAL(MID$(d$, 9, 2))
        IF m >= 1 AND m <= 12 AND j >= 1 AND j <= 31 THEN ok = 1
    END IF
    IsDate = ok
END FUNCTION

' « aujourd'hui », « demain », « après-demain », sinon « samedi 24 octobre »
FUNCTION SpokenDay$ (d$)
    n = DayNum(d$)
    e = n - DayNum(Today$)
    IF e = 0 THEN
        r$ = "aujourd'hui"
    ELSEIF e = 1 THEN
        r$ = "demain"
    ELSEIF e = 2 THEN
        r$ = "après-demain"
    ELSE
        SELECT CASE (n + 4) MOD 7
        CASE 0: r$ = "dimanche"
        CASE 1: r$ = "lundi"
        CASE 2: r$ = "mardi"
        CASE 3: r$ = "mercredi"
        CASE 4: r$ = "jeudi"
        CASE 5: r$ = "vendredi"
        CASE 6: r$ = "samedi"
        END SELECT
        j = VAL(MID$(d$, 9, 2))
        IF j = 1 THEN r$ = r$ + " premier" ELSE r$ = r$ + " " + LTRIM$(STR$(j))
        SELECT CASE VAL(MID$(d$, 6, 2))
        CASE 1: r$ = r$ + " janvier"
        CASE 2: r$ = r$ + " février"
        CASE 3: r$ = r$ + " mars"
        CASE 4: r$ = r$ + " avril"
        CASE 5: r$ = r$ + " mai"
        CASE 6: r$ = r$ + " juin"
        CASE 7: r$ = r$ + " juillet"
        CASE 8: r$ = r$ + " août"
        CASE 9: r$ = r$ + " septembre"
        CASE 10: r$ = r$ + " octobre"
        CASE 11: r$ = r$ + " novembre"
        CASE 12: r$ = r$ + " décembre"
        END SELECT
    END IF
    SpokenDay$ = r$
END FUNCTION

' « 14:00 » -> « 14 heures », « 14:30 » -> « 14 heures 30 », « 00:15 » -> « minuit 15 », « 12:00 » -> « midi »
FUNCTION SpokenTime$ (h$)
    hh = VAL(LEFT$(h$, 2))
    mm = VAL(MID$(h$, 4, 2))
    IF hh = 0 THEN
        r$ = "minuit"
    ELSEIF hh = 12 THEN
        r$ = "midi"
    ELSEIF hh = 1 THEN
        r$ = "une heure"
    ELSE
        r$ = LTRIM$(STR$(hh)) + " heures"
    END IF
    IF mm > 0 THEN r$ = r$ + " " + LTRIM$(STR$(mm))
    SpokenTime$ = r$
END FUNCTION

' une heure valide HH:MM ?
FUNCTION IsTime (h$)
    ok = 0
    IF LEN(h$) = 5 AND MID$(h$, 3, 1) = ":" THEN
        IF VAL(LEFT$(h$, 2)) <= 23 AND VAL(MID$(h$, 4, 2)) <= 59 THEN ok = 1
    END IF
    IsTime = ok
END FUNCTION

' ---- askinfo : l'app lancee pour le LLM ----------------------------------------------------------------------------
' Outcome {"app": "askinfo", "args": ["meteo", "day", "2026-10-24"]} : l'app tourne sans parler, ses Say partent au
' LLM comme donnee (second tour : « comment m'habiller demain ? »). ForLLM vaut 1 dans ce cas : l'app peut
' alors donner plus de details, chiffres compris.
FUNCTION ForLLM
    IF FILEEXISTS("ASKINFO:") THEN ForLLM = 1 ELSE ForLLM = 0
END FUNCTION

' ---- le web : REST -------------------------------------------------------------------------------------------
' Web$("https://...") : GET, le corps ("" si echec ou si le code n'est pas 2xx)
FUNCTION Web$ (url$)
    Web$ = WebSend$("GET", url$, "")
    IF WebStatus < 200 OR WebStatus > 299 THEN Web$ = ""
END FUNCTION

' WebSend$("POST", "http://192.168.1.50/api/brew", "{""boisson"": ""espresso""}") : n'importe quelle methode,
' avec un corps ou non ; le corps de la reponse, quel que soit le code (WebStatus le donne)
FUNCTION WebSend$ (verb$, url$, body$)
    f = FREEFILE
    IF body$ = "" AND (UCASE$(verb$) = "GET" OR UCASE$(verb$) = "DELETE") THEN
        OPEN UCASE$(verb$) + " " + url$ FOR INPUT AS #f
        CLOSE #f
    ELSE
        OPEN UCASE$(verb$) + " " + url$ FOR OUTPUT AS #f
        PRINT #f, body$
        CLOSE #f
    END IF
    WebSend$ = ReadAll$("RESPONSE:")
END FUNCTION

' un en-tete pour les requetes suivantes : WebHeader "Authorization: Bearer " + field$ ; WebHeader "" les efface tous
SUB WebHeader (header$)
    t$ = ""
    IF header$ <> "" THEN t$ = ReadAll$("HEADERS:")
    f = FREEFILE
    OPEN "HEADERS:" FOR OUTPUT AS #f
    IF t$ <> "" THEN PRINT #f, t$
    IF header$ <> "" THEN PRINT #f, header$
    CLOSE #f
END SUB

' le code HTTP de la derniere requete (200, 404...) ; 0 : pas de reponse (reseau, nom, certificat)
FUNCTION WebStatus
    WebStatus = VAL(ReadAll$("STATUS:"))
END FUNCTION

' (tout un fichier ou peripherique en une chaine, lignes separees par CHR$(10))
FUNCTION ReadAll$ (path$)
    r$ = ""
    f = FREEFILE
    OPEN path$ FOR INPUT AS #f
    DO WHILE NOT EOF(f)
        LINE INPUT #f, l$
        IF r$ <> "" THEN r$ = r$ + CHR$(10)
        r$ = r$ + l$
    LOOP
    CLOSE #f
    ReadAll$ = r$
END FUNCTION

' ---- JSON : lire une valeur ----------------------------------------------------------------------------------
' JsonRaw$(j$, "cle", k) : la valeur brute de "cle" (la premiere trouvee dans le texte, a n'importe quel niveau),
' et si c'est un tableau son k-ieme element (k = 1 sinon) : "texte" avec ses guillemets, 12.5, true, {...}, [...].
' JsonText$ : pareil, une chaine sans ses guillemets et decodee (\" \ \/ \n \t ; les \uXXXX restent tels quels).
' Pour une cle qui revient (plusieurs objets), chercher d'abord le bon morceau : MID$(j$, INSTR(j$, "...")).
FUNCTION JsonRaw$ (j$, field$, k)
    q$ = CHR$(34)
    r$ = ""
    p = INSTR(j$, q$ + field$ + q$)
    IF p > 0 THEN
        p = JsonSkip(j$, p + LEN(field$) + 2)
        IF MID$(j$, p, 1) = ":" THEN
            p = JsonSkip(j$, p + 1)
            IF MID$(j$, p, 1) = "[" THEN
                p = JsonSkip(j$, p + 1)
                FOR i = 2 TO k
                    p = JsonSkip(j$, JsonEnd(j$, p))
                    IF MID$(j$, p, 1) <> "," THEN p = 0: EXIT FOR
                    p = JsonSkip(j$, p + 1)
                NEXT
                IF p > 0 THEN IF MID$(j$, p, 1) = "]" THEN p = 0
            END IF
            IF p > 0 THEN r$ = MID$(j$, p, JsonEnd(j$, p) - p)
        END IF
    END IF
    JsonRaw$ = r$
END FUNCTION

FUNCTION JsonText$ (j$, field$, k)
    r$ = JsonRaw$(j$, field$, k)
    IF LEFT$(r$, 1) = CHR$(34) THEN
        t$ = ""
        i = 2
        DO WHILE i < LEN(r$)
            c$ = MID$(r$, i, 1)
            IF c$ = "\" THEN
                i = i + 1
                c$ = MID$(r$, i, 1)
                IF c$ = "n" THEN c$ = CHR$(10)
                IF c$ = "t" THEN c$ = CHR$(9)
                IF c$ = "u" THEN c$ = "\u"
            END IF
            t$ = t$ + c$
            i = i + 1
        LOOP
        r$ = t$
    END IF
    JsonText$ = r$
END FUNCTION

' (la position du premier caractere non blanc a partir de p)
FUNCTION JsonSkip (j$, p)
    DO WHILE p <= LEN(j$)
        c$ = MID$(j$, p, 1)
        IF c$ <> " " AND c$ <> CHR$(9) AND c$ <> CHR$(10) AND c$ <> CHR$(13) THEN EXIT DO
        p = p + 1
    LOOP
    JsonSkip = p
END FUNCTION

' (la position juste apres la valeur qui commence en p : chaine, objet, tableau ou nombre / mot)
FUNCTION JsonEnd (j$, p)
    q$ = CHR$(34)
    c$ = MID$(j$, p, 1)
    e = p + 1
    IF c$ = q$ THEN
        DO WHILE e <= LEN(j$)
            c$ = MID$(j$, e, 1)
            IF c$ = "\" THEN e = e + 1 ELSE IF c$ = q$ THEN EXIT DO
            e = e + 1
        LOOP
        e = e + 1
    ELSEIF c$ = "{" OR c$ = "[" THEN
        n = 1
        DO WHILE e <= LEN(j$) AND n > 0
            c$ = MID$(j$, e, 1)
            IF c$ = q$ THEN
                e = JsonEnd(j$, e) - 1
            ELSEIF c$ = "{" OR c$ = "[" THEN
                n = n + 1
            ELSEIF c$ = "}" OR c$ = "]" THEN
                n = n - 1
            END IF
            e = e + 1
        LOOP
    ELSE
        DO WHILE e <= LEN(j$)
            c$ = MID$(j$, e, 1)
            IF c$ = "," OR c$ = "]" OR c$ = "}" OR c$ = " " OR c$ = CHR$(10) OR c$ = CHR$(13) THEN EXIT DO
            e = e + 1
        LOOP
    END IF
    JsonEnd = e
END FUNCTION

' ---- retrouver un element par ses mots -----------------------------------------------------------------------
' WordScore(texte$, mots$) : combien des mots de mots$ (3 lettres et plus) sont dans texte$, sans tenir compte de
' la casse ; un mot de 6 lettres et plus est compare sans ses deux dernieres lettres (« Alexandre » mal entendu
' trouve « Alexandra », « tomates » trouve « tomate »).
FUNCTION WordScore (t$, words$)
    tl$ = LCASE$(t$)
    w$ = LCASE$(words$) + " "
    n = 0
    s = 1
    FOR i = 1 TO LEN(w$)
        IF MID$(w$, i, 1) = " " OR MID$(w$, i, 1) = "'" THEN
            m$ = MID$(w$, s, i - s)
            IF LEN(m$) >= 6 THEN m$ = LEFT$(m$, LEN(m$) - 2)
            IF LEN(m$) >= 3 THEN IF INSTR(tl$, m$) > 0 THEN n = n + 1
            s = i + 1
        END IF
    NEXT
    WordScore = n
END FUNCTION

' ---- fichiers ---------------------------------------------------------------------------------------------------
' un dossier, cree s'il n'existe pas (SD:/tmp pour les fichiers de passage : PDF a imprimer...)
SUB MakeDir (d$)
    IF NOT FILEEXISTS(d$) THEN MKDIR d$
END SUB

' supprimer un fichier s'il existe (KILL sans erreur)
SUB DeleteFile (f$)
    IF FILEEXISTS(f$) THEN KILL f$
END SUB

' ---- PDF : un document texte simple (A4, Helvetica), sans aide du LLM -------------------------------------------
'   PdfOpen "Liste de courses"        le titre (gras)
'   PdfLine "lait, 2 litres"          une ligne (autant que voulu ; nouvelle page toute seule)
'   PdfClose "SD:/tmp/courses.pdf"    ecrit le fichier
' Les lignes attendent dans SD:/tmp/pdf.lines (pas de variable partagee entre SUB).
SUB PdfOpen (title$)
    MakeDir "SD:/tmp"
    f = FREEFILE
    OPEN "SD:/tmp/pdf.lines" FOR OUTPUT AS #f
    PRINT #f, title$
    CLOSE #f
END SUB

SUB PdfLine (t$)
    f = FREEFILE
    OPEN "SD:/tmp/pdf.lines" FOR APPEND AS #f
    PRINT #f, t$
    CLOSE #f
END SUB

SUB PdfClose (file$)
    nl$ = CHR$(10)
    DIM pl$(1000)
    n = 0
    f = FREEFILE
    OPEN "SD:/tmp/pdf.lines" FOR INPUT AS #f
    DO WHILE NOT EOF(f) AND n < 1000
        LINE INPUT #f, l$
        n = n + 1
        pl$(n) = l$
    LOOP
    CLOSE #f
    DeleteFile "SD:/tmp/pdf.lines"
    ' les pages : A4 (595 x 842 points), marges de 56, titre en 16 gras, lignes en 12 tous les 18 points
    DIM pg$(60)
    np = 1
    y = 786
    pg$(1) = "BT /F2 16 Tf 56 " + LTRIM$(STR$(y)) + " Td (" + PdfEsc$(pl$(1)) + ") Tj ET" + nl$
    y = y - 34
    FOR i = 2 TO n
        IF y < 56 AND np < 60 THEN
            np = np + 1
            pg$(np) = ""
            y = 786
        END IF
        pg$(np) = pg$(np) + "BT /F1 12 Tf 56 " + LTRIM$(STR$(y)) + " Td (" + PdfEsc$(pl$(i)) + ") Tj ET" + nl$
        y = y - 18
    NEXT
    ' les objets : 1 catalogue, 2 pages, 3 et 4 polices, puis pour chaque page k : 3 + 2k la page, 4 + 2k son contenu
    nobj = 4 + 2 * np
    DIM po(130)
    b$ = "%PDF-1.4" + nl$
    kids$ = ""
    FOR k = 1 TO np
        kids$ = kids$ + LTRIM$(STR$(3 + 2 * k)) + " 0 R "
    NEXT
    po(1) = LEN(b$)
    b$ = b$ + "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj" + nl$
    po(2) = LEN(b$)
    b$ = b$ + "2 0 obj << /Type /Pages /Kids [" + kids$ + "] /Count " + LTRIM$(STR$(np)) + " >> endobj" + nl$
    po(3) = LEN(b$)
    b$ = b$ + "3 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >> endobj" + nl$
    po(4) = LEN(b$)
    b$ = b$ + "4 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >> endobj" + nl$
    FOR k = 1 TO np
        p = 3 + 2 * k
        po(p) = LEN(b$)
        b$ = b$ + LTRIM$(STR$(p)) + " 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents " + LTRIM$(STR$(p + 1)) + " 0 R >> endobj" + nl$
        po(p + 1) = LEN(b$)
        b$ = b$ + LTRIM$(STR$(p + 1)) + " 0 obj << /Length " + LTRIM$(STR$(LEN(pg$(k)))) + " >> stream" + nl$ + pg$(k) + "endstream endobj" + nl$
    NEXT
    x = LEN(b$)
    b$ = b$ + "xref" + nl$ + "0 " + LTRIM$(STR$(nobj + 1)) + nl$ + "0000000000 65535 f " + nl$
    FOR k = 1 TO nobj
        b$ = b$ + RIGHT$("0000000000" + LTRIM$(STR$(po(k))), 10) + " 00000 n " + nl$
    NEXT
    b$ = b$ + "trailer << /Size " + LTRIM$(STR$(nobj + 1)) + " /Root 1 0 R >>" + nl$ + "startxref" + nl$ + LTRIM$(STR$(x)) + nl$ + "%%EOF" + nl$
    f = FREEFILE
    OPEN file$ FOR OUTPUT AS #f
    PRINT #f, b$;
    CLOSE #f
END SUB

' (une chaine PDF : \ ( ) echappes, les accents en octal \351 : le fichier reste en ASCII, police WinAnsi)
FUNCTION PdfEsc$ (t$)
    ' (les chaines de la VM sont en CP437 : chaque caractere 128..255 -> son code Latin-1 / WinAnsi en octal ; table de
    ' la VM, basfont.h bas437ToLatin1)
    t437$ = "307374351342344340345347352353350357356354304305311346306364366362373371377326334242243245077077"
    t437$ = t437$ + "341355363372361321252272277077254275274241253273043043043174053053053053053053174053053053053053"
    t437$ = t437$ + "053053053053055053053053053053053053053055053053053053053053053053053053053053053043043043043043"
    t437$ = t437$ + "077337077077077077265077077077077077077077077077077261077077077077367077260077267077077262043240"
    r$ = ""
    FOR i = 1 TO LEN(t$)
        c$ = MID$(t$, i, 1)
        a = ASC(c$)
        IF c$ = "\" OR c$ = "(" OR c$ = ")" THEN
            r$ = r$ + "\" + c$
        ELSEIF a > 127 THEN
            r$ = r$ + "\" + MID$(t437$, (a - 128) * 3 + 1, 3)
        ELSEIF a >= 32 THEN
            r$ = r$ + c$
        END IF
    NEXT
    PdfEsc$ = r$
END FUNCTION

' ---- PDF dessine : une mise en page libre (A4 : 595 x 842 points, depuis le coin en HAUT a gauche) ----------------
'   PdfBegin                                   un nouveau document (page 1)
'   PdfText x, y, taille, style$, couleur$, t$ du texte ; y : sa ligne de base ; style$ : "" normal, "B" gras,
'                                              "I" italique, "BI" ; Helvetica (les memes chasses que Liberation Sans)
'   PdfBox x, y, l, h, r, couleur$             un rectangle plein (r : rayon des coins, 0 : coins carres)
'   PdfFrame x, y, l, h, r, epaisseur, couleur$  son contour
'   PdfRule x1, y1, x2, y2, epaisseur, couleur$  un trait
'   PdfNewPage                                 la page suivante
'   PdfEnd "SD:/tmp/agenda.pdf"                ecrit le fichier
'   PdfTextWidth(t$, taille, style$)           la largeur d'un texte, pour l'aligner a droite ou le centrer
' Couleurs : "RRGGBB" en hexadecimal ("1E88E5" bleu), "" : noir. Le dessin attend dans SD:/tmp/pdf.draw.
SUB PdfBegin
    MakeDir "SD:/tmp"
    f = FREEFILE
    OPEN "SD:/tmp/pdf.draw" FOR OUTPUT AS #f
    CLOSE #f
END SUB

SUB PdfNewPage
    PdfOp "%PAGE"
END SUB

SUB PdfText (x, y, size, style$, color$, t$)
    s$ = UCASE$(style$)
    fo$ = "/F1"
    IF INSTR(s$, "B") > 0 AND INSTR(s$, "I") > 0 THEN
        fo$ = "/F4"
    ELSEIF INSTR(s$, "B") > 0 THEN
        fo$ = "/F2"
    ELSEIF INSTR(s$, "I") > 0 THEN
        fo$ = "/F3"
    END IF
    PdfOp "BT " + PdfRgb$(color$, "rg") + " " + fo$ + " " + PdfN$(size) + " Tf " + PdfN$(x) + " " + PdfN$(842 - y) + " Td (" + PdfEsc$(t$) + ") Tj ET"
END SUB

SUB PdfBox (x, y, w, h, r, color$)
    PdfOp PdfRgb$(color$, "rg") + " " + PdfShape$(x, y, w, h, r) + " f"
END SUB

SUB PdfFrame (x, y, w, h, r, lw, color$)
    PdfOp PdfRgb$(color$, "RG") + " " + PdfN$(lw) + " w " + PdfShape$(x, y, w, h, r) + " S"
END SUB

SUB PdfRule (x1, y1, x2, y2, lw, color$)
    PdfOp PdfRgb$(color$, "RG") + " " + PdfN$(lw) + " w " + PdfN$(x1) + " " + PdfN$(842 - y1) + " m " + PdfN$(x2) + " " + PdfN$(842 - y2) + " l S"
END SUB

' la largeur de t$ en points (chasses d'Helvetica et d'Helvetica-Bold ; les lettres accentuees comptent comme les autres)
FUNCTION PdfTextWidth (t$, size, style$)
    IF INSTR(UCASE$(style$), "B") > 0 THEN
        w$ = "278333474556556889722238333333389584278333278278556556556556556556556556556556333333584584584611975722722722722667611778722278556722611833722778667778722667611722667944667667611333278333584556333556611556611556333611611278278556278889611611611611389556333611556778556556500389280389584"
    ELSE
        w$ = "278278355556556889667191333333389584278333278278556556556556556556556556556556278278584584584556015667667722722667611778722278500667556833722778667778722667611722667944667667611278278278469556333556556500556556278556556222222500222833556556556556333500278556500722500500500334260334584"
    END IF
    tw = 0
    FOR i = 1 TO LEN(t$)
        c = ASC(MID$(t$, i, 1))
        IF c >= 32 AND c <= 126 THEN
            v = VAL(MID$(w$, (c - 32) * 3 + 1, 3))
            IF v < 100 THEN v = v + 1000
        ELSE
            v = 556
        END IF
        tw = tw + v
    NEXT
    PdfTextWidth = tw * size / 1000
END FUNCTION

SUB PdfEnd (file$)
    nl$ = CHR$(10)
    DIM pg$(60)
    np = 1
    pg$(1) = ""
    f = FREEFILE
    OPEN "SD:/tmp/pdf.draw" FOR INPUT AS #f
    DO WHILE NOT EOF(f)
        LINE INPUT #f, l$
        IF l$ = "%PAGE" THEN
            IF np < 60 THEN
                np = np + 1
                pg$(np) = ""
            END IF
        ELSEIF l$ <> "" THEN
            pg$(np) = pg$(np) + l$ + nl$
        END IF
    LOOP
    CLOSE #f
    DeleteFile "SD:/tmp/pdf.draw"
    ' les objets : 1 catalogue, 2 pages, 3 a 6 polices, puis pour chaque page k : 5 + 2k la page, 6 + 2k son contenu
    nobj = 6 + 2 * np
    DIM po(130)
    b$ = "%PDF-1.4" + nl$
    kids$ = ""
    FOR k = 1 TO np
        kids$ = kids$ + LTRIM$(STR$(5 + 2 * k)) + " 0 R "
    NEXT
    po(1) = LEN(b$)
    b$ = b$ + "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj" + nl$
    po(2) = LEN(b$)
    b$ = b$ + "2 0 obj << /Type /Pages /Kids [" + kids$ + "] /Count " + LTRIM$(STR$(np)) + " >> endobj" + nl$
    FOR k = 3 TO 6
        po(k) = LEN(b$)
        IF k = 3 THEN fn$ = "Helvetica"
        IF k = 4 THEN fn$ = "Helvetica-Bold"
        IF k = 5 THEN fn$ = "Helvetica-Oblique"
        IF k = 6 THEN fn$ = "Helvetica-BoldOblique"
        b$ = b$ + LTRIM$(STR$(k)) + " 0 obj << /Type /Font /Subtype /Type1 /BaseFont /" + fn$ + " /Encoding /WinAnsiEncoding >> endobj" + nl$
    NEXT
    FOR k = 1 TO np
        p = 5 + 2 * k
        po(p) = LEN(b$)
        b$ = b$ + LTRIM$(STR$(p)) + " 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> /Contents " + LTRIM$(STR$(p + 1)) + " 0 R >> endobj" + nl$
        po(p + 1) = LEN(b$)
        b$ = b$ + LTRIM$(STR$(p + 1)) + " 0 obj << /Length " + LTRIM$(STR$(LEN(pg$(k)))) + " >> stream" + nl$ + pg$(k) + "endstream endobj" + nl$
    NEXT
    x = LEN(b$)
    b$ = b$ + "xref" + nl$ + "0 " + LTRIM$(STR$(nobj + 1)) + nl$ + "0000000000 65535 f " + nl$
    FOR k = 1 TO nobj
        b$ = b$ + RIGHT$("0000000000" + LTRIM$(STR$(po(k))), 10) + " 00000 n " + nl$
    NEXT
    b$ = b$ + "trailer << /Size " + LTRIM$(STR$(nobj + 1)) + " /Root 1 0 R >>" + nl$ + "startxref" + nl$ + LTRIM$(STR$(x)) + nl$ + "%%EOF" + nl$
    f = FREEFILE
    OPEN file$ FOR OUTPUT AS #f
    PRINT #f, b$;
    CLOSE #f
END SUB

' (une ligne du dessin en attente)
SUB PdfOp (op$)
    f = FREEFILE
    OPEN "SD:/tmp/pdf.draw" FOR APPEND AS #f
    PRINT #f, op$
    CLOSE #f
END SUB

' (un nombre pour le PDF : deux decimales au plus)
FUNCTION PdfN$ (v)
    PdfN$ = LTRIM$(STR$(INT(v * 100 + 0.5) / 100))
END FUNCTION

' (« 1E88E5 » -> « 0.12 0.53 0.9 rg » ; op$ : rg pour remplir, RG pour un trait ; "" : noir)
FUNCTION PdfRgb$ (c$, op$)
    r$ = ""
    h$ = UCASE$(c$) + "000000"
    FOR i = 0 TO 2
        v = (INSTR("0123456789ABCDEF", MID$(h$, i * 2 + 1, 1)) - 1) * 16 + INSTR("0123456789ABCDEF", MID$(h$, i * 2 + 2, 1)) - 1
        IF v < 0 THEN v = 0
        r$ = r$ + PdfN$(v / 255) + " "
    NEXT
    PdfRgb$ = r$ + op$
END FUNCTION

' (le chemin d'un rectangle aux coins arrondis de rayon r, en coordonnees PDF : y vers le haut)
FUNCTION PdfShape$ (x, y, w, h, r)
    b = 842 - y - h
    rr = r
    IF rr > w / 2 THEN rr = w / 2
    IF rr > h / 2 THEN rr = h / 2
    IF rr <= 0 THEN
        s$ = PdfN$(x) + " " + PdfN$(b) + " " + PdfN$(w) + " " + PdfN$(h) + " re"
    ELSE
        k = rr * 0.4477
        s$ = PdfN$(x + rr) + " " + PdfN$(b) + " m "
        s$ = s$ + PdfN$(x + w - rr) + " " + PdfN$(b) + " l "
        s$ = s$ + PdfN$(x + w - k) + " " + PdfN$(b) + " " + PdfN$(x + w) + " " + PdfN$(b + k) + " " + PdfN$(x + w) + " " + PdfN$(b + rr) + " c "
        s$ = s$ + PdfN$(x + w) + " " + PdfN$(b + h - rr) + " l "
        s$ = s$ + PdfN$(x + w) + " " + PdfN$(b + h - k) + " " + PdfN$(x + w - k) + " " + PdfN$(b + h) + " " + PdfN$(x + w - rr) + " " + PdfN$(b + h) + " c "
        s$ = s$ + PdfN$(x + rr) + " " + PdfN$(b + h) + " l "
        s$ = s$ + PdfN$(x + k) + " " + PdfN$(b + h) + " " + PdfN$(x) + " " + PdfN$(b + h - k) + " " + PdfN$(x) + " " + PdfN$(b + h - rr) + " c "
        s$ = s$ + PdfN$(x) + " " + PdfN$(b + rr) + " l "
        s$ = s$ + PdfN$(x) + " " + PdfN$(b + k) + " " + PdfN$(x + k) + " " + PdfN$(b) + " " + PdfN$(x + rr) + " " + PdfN$(b) + " c h"
    END IF
    PdfShape$ = s$
END FUNCTION

' ---- le mail : peripherique MAIL: (compte configure dans l'admin web, page Mail) --------------------------------
' Le noyau gere le compte, les protocoles et les secrets ; l'app ne voit que les messages. Chaque requete donne
' aussi son code dans STATUS: (MailStatus) et sa reponse ou l'erreur en francais dans RESPONSE: (MailError$) :
' 200 bon, 400 requete mal formee, 401 identification refusee, 403 refuse pour le LLM (askinfo : ni contenu ni
' envoi), 404 message introuvable, 412 pas de compte, 502 refus du serveur, 0 pas de reponse (reseau).
'   OPEN "MAIL:INBOX" FOR INPUT          les 10 derniers messages, une ligne chacun (le plus recent d'abord) :
'                                        uid TAB date TAB nom TAB adresse TAB sujet TAB lu (1) / non lu (0)
'   OPEN "MAIL:INBOX/UNSEEN" FOR INPUT   pareil, les non lus seulement
'   OPEN "MAIL:INBOX/<uid>" FOR INPUT    le message en texte : From:, To:, Date:, Subject:, ligne vide, texte
'   OPEN "MAIL:UNSEEN" FOR INPUT         le nombre de non lus
'   OPEN "MAIL:SEND" FOR OUTPUT          To: ..., Subject: ..., ligne vide, corps ; CLOSE envoie

' le nombre de mails non lus ; -1 si le compte ou le reseau ne repond pas (MailError$ dit pourquoi)
FUNCTION MailUnseen
    t$ = ReadAll$("MAIL:UNSEEN")
    IF MailStatus = 200 THEN MailUnseen = VAL(t$) ELSE MailUnseen = -1
END FUNCTION

' les 10 derniers mails, une ligne par mail separees par CHR$(10) ("" si aucun ou en cas d'erreur) ; MailField$
FUNCTION MailList$
    MailList$ = ReadAll$("MAIL:INBOX")
END FUNCTION

' pareil, seulement les non lus
FUNCTION MailUnseenList$
    MailUnseenList$ = ReadAll$("MAIL:INBOX/UNSEEN")
END FUNCTION

' le texte d'un mail (uid$ : le premier champ d'une ligne de MailList$) ; le mail est marque lu
FUNCTION MailRead$ (uid$)
    MailRead$ = ReadAll$("MAIL:INBOX/" + uid$)
END FUNCTION

' envoie un mail : 1 si c'est parti, 0 sinon (MailError$ dit pourquoi). body$ peut avoir des CHR$(10).
' ATTENTION : demander d'abord confirmation a voix haute ; ne jamais envoyer sur la seule demande du LLM.
FUNCTION MailSend (to$, subject$, body$)
    f = FREEFILE
    OPEN "MAIL:SEND" FOR OUTPUT AS #f
    PRINT #f, "To: " + to$
    PRINT #f, "Subject: " + subject$
    PRINT #f, ""
    PRINT #f, body$
    CLOSE #f
    IF MailStatus = 200 THEN MailSend = 1 ELSE MailSend = 0
END FUNCTION

' le k-ieme champ (1 = uid, 2 = date, 3 = nom, 4 = adresse, 5 = sujet, 6 = lu) d'une ligne de MailList$
FUNCTION MailField$ (l$, k)
    t$ = l$ + CHR$(9)
    r$ = ""
    n = 1
    s = 1
    FOR i = 1 TO LEN(t$)
        IF MID$(t$, i, 1) = CHR$(9) THEN
            IF n = k THEN r$ = MID$(t$, s, i - s)
            n = n + 1
            s = i + 1
        END IF
    NEXT
    MailField$ = r$
END FUNCTION

' le code de la derniere requete MAIL: (200 : bon) et son message d'erreur en francais
FUNCTION MailStatus
    MailStatus = VAL(ReadAll$("STATUS:"))
END FUNCTION

FUNCTION MailError$
    MailError$ = ReadAll$("RESPONSE:")
END FUNCTION

' ---- ecouter : poser une question et entendre la reponse ----------------------------------------------------
' Ask$("Je l'imprime sur la Canon ?") : Pocket pose la question, puis la reponse dite (ou tapee) revient en texte ;
' "" si personne ne repond (environ 10 s) ou si l'app est lancee pour le LLM (askinfo) : l'app doit alors annuler.
FUNCTION Ask$ (q$)
    IF q$ <> "" THEN Say q$
    LINE INPUT r$
    Ask$ = r$
END FUNCTION

' 1 si la reponse est un oui (oui, ouais, vas-y, d'accord, ok, bien sur, yes, ja...), 0 sinon (non, rien, autre chose)
FUNCTION IsYes (a$)
    t$ = " " + LCASE$(a$) + " "
    FOR i = 1 TO LEN(t$)
        c$ = MID$(t$, i, 1)
        IF INSTR(".,;:!?-'", c$) > 0 THEN MID$(t$, i, 1) = " "
    NEXT
    r = 0
    IF INSTR(t$, " oui ") OR INSTR(t$, " ouais ") OR INSTR(t$, " vas y ") OR INSTR(t$, " d accord ") OR INSTR(t$, " ok ") THEN r = 1
    IF INSTR(t$, " okay ") OR INSTR(t$, " bien s") OR INSTR(t$, " yes ") OR INSTR(t$, " ja ") OR INSTR(t$, " volontiers ") THEN r = 1
    IF INSTR(t$, " exactement ") OR INSTR(t$, " parfait ") OR INSTR(t$, " allez ") OR INSTR(t$, " go ") THEN r = 1
    IF INSTR(t$, " non ") OR INSTR(t$, " pas ") OR INSTR(t$, " annule") OR INSTR(t$, " stop ") THEN r = 0
    IsYes = r
END FUNCTION

' le numero d'une reponse a un choix : « 2 », « deux », « la deuxieme », « le premier » -> 2, 1... ; 0 si aucun
FUNCTION AnswerNumber (a$)
    t$ = LCASE$(a$) + " "
    n = 0
    s = 1
    FOR i = 1 TO LEN(t$)
        c$ = MID$(t$, i, 1)
        IF INSTR(" .,;:!?-'", c$) > 0 THEN
            w$ = MID$(t$, s, i - s)
            s = i + 1
            IF n = 0 AND w$ <> "" THEN
                IF VAL(w$) > 0 THEN n = VAL(w$)
                IF w$ = "un" OR w$ = "une" OR LEFT$(w$, 6) = "premie" THEN n = 1
                IF LEFT$(w$, 4) = "deux" OR LEFT$(w$, 5) = "secon" THEN n = 2
                IF LEFT$(w$, 5) = "trois" THEN n = 3
                IF LEFT$(w$, 6) = "quatri" OR w$ = "quatre" THEN n = 4
                IF LEFT$(w$, 4) = "cinq" THEN n = 5
                IF w$ = "six" OR LEFT$(w$, 5) = "sixi" THEN n = 6
                IF LEFT$(w$, 4) = "sept" THEN n = 7
                IF LEFT$(w$, 4) = "huit" THEN n = 8
                IF LEFT$(w$, 4) = "neuf" OR LEFT$(w$, 5) = "neuvi" THEN n = 9
                IF w$ = "dix" OR LEFT$(w$, 4) = "dixi" THEN n = 10
            END IF
        END IF
    NEXT
    ' (au-dela de dix : le nombre dit en entier, « vingt-trois », « septante-deux » ; pas seulement son dernier mot)
    k = SpokenNumber(a$)
    IF k > 10 THEN n = k
    AnswerNumber = n
END FUNCTION

' le nombre dit dans une reponse : Voxtral ecrit les nombres en lettres (pour que la voix les lise bien) et VAL ne lit
' que des chiffres. « quatre » -> 4, « vingt et un » -> 21, « quatre-vingt-dix-sept » -> 97, « septante-trois » -> 73
' (Belgique, Suisse : septante, octante, huitante, nonante), « deux cent cinquante » -> 250, « mille deux cents »
' -> 1200, « deux millions » -> 2000000, « moins trois » -> -3 ; des chiffres (« 23 ») aussi. Le premier nombre de
' la phrase (« le nombre quarante-deux » -> 42) ; -1 s'il n'y en a pas.
FUNCTION SpokenNumber (a$)
    t$ = LCASE$(a$) + " "
    total = 0
    cur = 0
    last = -1
    found = 0
    neg = 0
    fin = 0
    s = 1
    FOR i = 1 TO LEN(t$)
        c$ = MID$(t$, i, 1)
        IF INSTR(" .,;:!?-'", c$) > 0 THEN
            w$ = MID$(t$, s, i - s)
            s = i + 1
            IF w$ <> "" AND fin = 0 THEN
                v = NumberWord(w$)
                IF INSTR("0123456789", LEFT$(w$, 1)) > 0 THEN
                    ' (des chiffres : le nombre tel quel)
                    IF found = 0 THEN
                        cur = VAL(w$)
                        found = 1
                    END IF
                    fin = 1
                ELSEIF v = -2 THEN
                    last = -1
                ELSEIF v = -1 THEN
                    IF found = 1 THEN fin = 1
                    IF found = 0 AND w$ = "moins" THEN neg = 1
                ELSE
                    found = 1
                    IF v = 100 THEN
                        r = cur MOD 100
                        IF r = 0 THEN cur = cur + 100 ELSE cur = cur - r + r * 100
                    ELSEIF v = 1000 THEN
                        IF cur = 0 THEN cur = 1
                        total = total + cur * 1000
                        cur = 0
                    ELSEIF v >= 1000000 THEN
                        IF cur = 0 THEN cur = 1
                        total = total + cur * v
                        cur = 0
                    ELSEIF v = 20 AND last = 4 THEN
                        ' (quatre-vingt(s) : 80)
                        cur = cur - 4 + 80
                    ELSE
                        cur = cur + v
                    END IF
                    last = v
                END IF
            END IF
        END IF
    NEXT
    IF found = 0 THEN
        SpokenNumber = -1
    ELSE
        IF neg = 1 THEN SpokenNumber = -(total + cur) ELSE SpokenNumber = total + cur
    END IF
END FUNCTION

' (la valeur d'un mot de nombre ; -1 : pas un nombre ; -2 : « et », qui relie : vingt et un)
FUNCTION NumberWord (w$)
    v = -1
    IF w$ = "et" THEN v = -2
    IF LEFT$(w$, 1) = "z" AND RIGHT$(w$, 2) = "ro" AND LEN(w$) <= 5 THEN v = 0
    IF w$ = "un" OR w$ = "une" THEN v = 1
    IF w$ = "deux" THEN v = 2
    IF w$ = "trois" THEN v = 3
    IF w$ = "quatre" THEN v = 4
    IF w$ = "cinq" THEN v = 5
    IF w$ = "six" THEN v = 6
    IF w$ = "sept" THEN v = 7
    IF w$ = "huit" THEN v = 8
    IF w$ = "neuf" THEN v = 9
    IF w$ = "dix" THEN v = 10
    IF w$ = "onze" THEN v = 11
    IF w$ = "douze" THEN v = 12
    IF w$ = "treize" THEN v = 13
    IF w$ = "quatorze" THEN v = 14
    IF w$ = "quinze" THEN v = 15
    IF w$ = "seize" THEN v = 16
    IF w$ = "vingt" OR w$ = "vingts" THEN v = 20
    IF w$ = "trente" THEN v = 30
    IF w$ = "quarante" THEN v = 40
    IF w$ = "cinquante" THEN v = 50
    IF w$ = "soixante" THEN v = 60
    IF w$ = "septante" THEN v = 70
    IF w$ = "octante" OR w$ = "huitante" THEN v = 80
    IF w$ = "nonante" THEN v = 90
    IF w$ = "cent" OR w$ = "cents" THEN v = 100
    IF w$ = "mille" OR w$ = "mil" THEN v = 1000
    IF w$ = "million" OR w$ = "millions" THEN v = 1000000
    IF w$ = "milliard" OR w$ = "milliards" THEN v = 1000000000
    NumberWord = v
END FUNCTION

' ---- imprimer : peripherique PRINTER: (imprimantes du reseau, IPP Everywhere / AirPrint) -----------------------
' Le noyau trouve les imprimantes (mDNS, plus SD:/etc/printers.txt : « adresse [nom] » par ligne), parle IPP, met
' en page les .txt (Helvetica 11, A4) et dessine les PDF pour une imprimante qui ne prend pas le PDF. Chaque requete
' donne son code dans STATUS: (PrinterStatus) et une phrase en francais dans RESPONSE: (PrinterMessage$) :
' 200 imprime, 202 envoye (l'imprimante n'a pas confirme la fin), 300 plusieurs imprimantes et aucune nommee,
' 400 ni PDF ni texte, 404 imprimante ou fichier introuvable, 409 imprimante arretee (plus de papier, capot ouvert,
' plus d'encre...), 415 format refuse, 500 polices absentes (SD:/lib/fonts), 502 refus, 0 pas de reponse.
'   OPEN "PRINTER:LIST" FOR INPUT     une ligne par imprimante : nom TAB modele TAB formats TAB adresse
'                                     (la recherche prend quelques secondes ; gardee une minute)
'   OPEN "PRINTER:<nom>" FOR OUTPUT   PRINT # le chemin du fichier ; CLOSE imprime et attend la fin. <nom> : son nom,
'                                     une partie du nom, son numero dans la liste, une adresse, ou rien (la seule)
' ATTENTION : n'imprimer que ce que la personne a demande ; un fichier de passage va dans SD:/tmp/ (DeleteFile apres).

' les imprimantes du reseau, une ligne par imprimante separees par CHR$(10) ("" : aucune)
FUNCTION PrinterList$
    PrinterList$ = ReadAll$("PRINTER:LIST")
END FUNCTION

' le nombre d'imprimantes d'une liste de PrinterList$
FUNCTION PrinterCount (list$)
    n = 0
    IF list$ <> "" THEN
        n = 1
        FOR i = 1 TO LEN(list$)
            IF MID$(list$, i, 1) = CHR$(10) AND i < LEN(list$) THEN n = n + 1
        NEXT
    END IF
    PrinterCount = n
END FUNCTION

' le k-ieme champ (1 nom, 2 modele, 3 formats, 4 adresse) de la i-eme imprimante (1, 2...) d'une liste de PrinterList$
FUNCTION PrinterField$ (list$, i, k)
    t$ = list$ + CHR$(10)
    l$ = ""
    n = 1
    s = 1
    FOR p = 1 TO LEN(t$)
        IF MID$(t$, p, 1) = CHR$(10) THEN
            IF n = i THEN l$ = MID$(t$, s, p - s)
            n = n + 1
            s = p + 1
        END IF
    NEXT
    PrinterField$ = MailField$(l$, k)
END FUNCTION

' le nom de la i-eme imprimante (« HP DeskJet 2700 series »)
FUNCTION PrinterName$ (list$, i)
    PrinterName$ = PrinterField$(list$, i, 1)
END FUNCTION

' imprime un fichier (PDF ou texte) sur l'imprimante nommee ("" : la seule du reseau) et attend la fin : 1 si c'est
' imprime (ou envoye sans confirmation : 202), 0 sinon -- PrinterMessage$ dit pourquoi, PrinterStatus le code
FUNCTION PrintFile (printer$, file$)
    f = FREEFILE
    OPEN "PRINTER:" + printer$ FOR OUTPUT AS #f
    PRINT #f, file$
    CLOSE #f
    s = PrinterStatus
    IF s = 200 OR s = 202 THEN PrintFile = 1 ELSE PrintFile = 0
END FUNCTION

' le code de la derniere requete PRINTER: (200 : imprime) et sa phrase en francais (« C'est imprime sur ... »)
FUNCTION PrinterStatus
    PrinterStatus = VAL(ReadAll$("STATUS:"))
END FUNCTION

FUNCTION PrinterMessage$
    PrinterMessage$ = ReadAll$("RESPONSE:")
END FUNCTION

' ---- un mail tout pret : fichier brouillon (genmail) ------------------------------------------------------------
' Une app qui fait un contenu (courses, agenda...) a une action genmail : elle ecrit le mail dans SD:/tmp/<x>.txt,
' comme genpdf ecrit un PDF. Le fichier : « Subject: le sujet », une ligne vide, puis le texte (un mail redige par le
' LLM, outcome maketext de l'hote : « Subject: », puis « Lire: oui », une ligne vide, le texte). L'app sendmail
' l'envoie ensuite au destinataire du carnet (cardbook), apres un oui dit a voix haute, et le supprime.
'   MailDraft "SD:/tmp/courses.txt", "Liste de courses", texte$      (texte$ : lignes separees par CHR$(10))

SUB MailDraft (file$, subject$, body$)
    MakeDir "SD:/tmp"
    f = FREEFILE
    OPEN file$ FOR OUTPUT AS #f
    PRINT #f, "Subject: " + subject$
    PRINT #f, ""
    PRINT #f, body$
    CLOSE #f
END SUB

' le sujet d'un brouillon (la ligne « Subject: ») ; "" si le fichier n'en a pas
FUNCTION MailDraftSubject$ (file$)
    t$ = ReadAll$(file$)
    r$ = ""
    IF LEFT$(t$, 9) = "Subject: " THEN
        p = INSTR(t$, CHR$(10))
        IF p = 0 THEN p = LEN(t$) + 1
        r$ = MID$(t$, 10, p - 10)
    END IF
    MailDraftSubject$ = r$
END FUNCTION

' le texte d'un brouillon (sans les lignes d'en-tête « Subject: », « Lire: »... ni la ligne vide qui les suit)
FUNCTION MailDraftBody$ (file$)
    t$ = ReadAll$(file$)
    IF LEFT$(t$, 9) = "Subject: " THEN
        fini = 0
        DO WHILE fini = 0
            p = INSTR(t$, CHR$(10))
            IF p = 0 THEN
                t$ = ""
                fini = 1
            ELSEIF p = 1 THEN
                t$ = MID$(t$, 2)
                fini = 1
            ELSE
                t$ = MID$(t$, p + 1)
            END IF
        LOOP
    END IF
    MailDraftBody$ = t$
END FUNCTION

' une ligne d'en-tête d'un brouillon (« Lire: oui » : le texte est relu avant l'envoi, mail rédigé par le LLM avec
' l'outcome maketext) ; "" si absente
FUNCTION MailDraftHeader$ (file$, name$)
    t$ = ReadAll$(file$) + CHR$(10)
    r$ = ""
    k$ = LCASE$(name$) + ":"
    p = INSTR(t$, CHR$(10))
    DO WHILE p > 1
        l$ = LEFT$(t$, p - 1)
        IF LCASE$(LEFT$(l$, LEN(k$))) = k$ THEN r$ = LTRIM$(MID$(l$, LEN(k$) + 1))
        t$ = MID$(t$, p + 1)
        p = INSTR(t$, CHR$(10))
    LOOP
    MailDraftHeader$ = r$
END FUNCTION

' ---- le carnet d'adresses : SD:/apps/cardbook/cardbook.txt (app cardbook) -------------------------------------
' Une fiche par personne : « [Prenom] », puis une ligne « champ = valeur » par champ : mail, phone, birthday
' (AAAA-MM-JJ, ou --MM-JJ sans l'annee), tastes, address, notes... Les adresses et les numeros ne partent jamais
' au LLM : le LLM ne donne que des prenoms, l'app qui ecrit ou appelle cherche elle-meme ici.
'   CardFind$("alexandre")          « Alexandra » : le prenom de la fiche (meme mot, debut de mot, ou un mot proche)
'   CardGet$("Alexandra", "mail")   la valeur d'un champ ("" si pas de fiche ou pas de champ)
'   CardNames$("mail")              les prenoms qui ont ce champ ("" : toutes les fiches), separes par CHR$(10)

FUNCTION CardFile$
    CardFile$ = "SD:/apps/cardbook/cardbook.txt"
END FUNCTION

' le prenom d'une fiche pour un nom dit : le meme (sans majuscules), sinon un debut de mot, sinon la seule fiche dont
' un mot ressemble (WordScore) ; "" si aucune
FUNCTION CardFind$ (name$)
    q$ = LCASE$(RTRIM$(LTRIM$(name$)))
    r$ = ""
    r2$ = ""
    r3$ = ""
    best = 0
    hits = 0
    IF q$ <> "" AND FILEEXISTS(CardFile$) THEN
        f = FREEFILE
        OPEN CardFile$ FOR INPUT AS #f
        DO WHILE NOT EOF(f)
            LINE INPUT #f, x$
            x$ = RTRIM$(LTRIM$(x$))
            IF LEFT$(x$, 1) = "[" AND RIGHT$(x$, 1) = "]" AND LEN(x$) > 2 THEN
                n$ = MID$(x$, 2, LEN(x$) - 2)
                l$ = LCASE$(n$)
                IF l$ = q$ THEN r$ = n$
                IF r2$ = "" AND LEN(q$) >= 3 AND LEN(l$) >= 3 THEN
                    IF INSTR(l$, q$) = 1 OR INSTR(q$, l$) = 1 THEN r2$ = n$
                END IF
                s = WordScore(n$, q$)
                IF s > best THEN
                    best = s
                    r3$ = n$
                    hits = 1
                ELSEIF s = best AND s > 0 THEN
                    hits = hits + 1
                END IF
            END IF
        LOOP
        CLOSE #f
    END IF
    IF r$ = "" THEN r$ = r2$
    IF r$ = "" AND hits = 1 THEN r$ = r3$
    CardFind$ = r$
END FUNCTION

' la valeur d'un champ d'une fiche ("" si rien)
FUNCTION CardGet$ (name$, field$)
    n$ = LCASE$(CardFind$(name$))
    k$ = LCASE$(field$)
    r$ = ""
    dans = 0
    IF n$ <> "" THEN
        f = FREEFILE
        OPEN CardFile$ FOR INPUT AS #f
        DO WHILE NOT EOF(f)
            LINE INPUT #f, x$
            x$ = RTRIM$(LTRIM$(x$))
            IF LEFT$(x$, 1) = "[" AND RIGHT$(x$, 1) = "]" THEN
                dans = 0
                IF LCASE$(MID$(x$, 2, LEN(x$) - 2)) = n$ THEN dans = 1
            ELSEIF dans = 1 AND LEFT$(x$, 1) <> "#" THEN
                p = INSTR(x$, "=")
                IF p > 1 THEN
                    IF LCASE$(RTRIM$(LEFT$(x$, p - 1))) = k$ THEN r$ = LTRIM$(MID$(x$, p + 1))
                END IF
            END IF
        LOOP
        CLOSE #f
    END IF
    CardGet$ = r$
END FUNCTION

' les prenoms des fiches qui ont ce champ ("" : toutes), separes par CHR$(10), dans l'ordre du fichier
FUNCTION CardNames$ (field$)
    k$ = LCASE$(field$)
    r$ = ""
    n$ = ""
    pris = 0
    IF FILEEXISTS(CardFile$) THEN
        f = FREEFILE
        OPEN CardFile$ FOR INPUT AS #f
        DO WHILE NOT EOF(f)
            LINE INPUT #f, x$
            x$ = RTRIM$(LTRIM$(x$))
            IF LEFT$(x$, 1) = "[" AND RIGHT$(x$, 1) = "]" AND LEN(x$) > 2 THEN
                n$ = MID$(x$, 2, LEN(x$) - 2)
                pris = 0
                IF k$ = "" THEN
                    IF r$ <> "" THEN r$ = r$ + CHR$(10)
                    r$ = r$ + n$
                    pris = 1
                END IF
            ELSEIF n$ <> "" AND pris = 0 AND LEFT$(x$, 1) <> "#" THEN
                p = INSTR(x$, "=")
                IF p > 1 THEN
                    IF LCASE$(RTRIM$(LEFT$(x$, p - 1))) = k$ AND LTRIM$(MID$(x$, p + 1)) <> "" THEN
                        IF r$ <> "" THEN r$ = r$ + CHR$(10)
                        r$ = r$ + n$
                        pris = 1
                    END IF
                END IF
            END IF
        LOOP
        CLOSE #f
    END IF
    CardNames$ = r$
END FUNCTION

' une adresse mail a dire : « alexandra point dupont arobase exemple point fr »
FUNCTION SpokenMail$ (a$)
    r$ = ""
    FOR i = 1 TO LEN(a$)
        c$ = MID$(a$, i, 1)
        IF c$ = "@" THEN
            r$ = r$ + " arobase "
        ELSEIF c$ = "." THEN
            r$ = r$ + " point "
        ELSEIF c$ = "-" THEN
            r$ = r$ + " tiret "
        ELSEIF c$ = "_" THEN
            r$ = r$ + " tiret bas "
        ELSE
            r$ = r$ + c$
        END IF
    NEXT
    SpokenMail$ = RTRIM$(LTRIM$(r$))
END FUNCTION

' ---- « moi » : la personne elle-meme -------------------------------------------------------------------------------
' « envoie-moi ça par mail » : le LLM donne moi (ou le prenom de la personne, {nom} de la memoire) comme destinataire.
'   IsMe("moi")            1 si le nom dit designe la personne elle-meme : moi, me, moi-meme..., ou la valeur de
'                          « nom » dans la memoire de Pocket (SD:/etc/memoire.txt) ; 0 sinon
'   MailOwnAddress$        l'adresse de la boite de Pocket (page Mail de l'admin web, SD:/etc/mail/account.ini) ;
'                          "" si aucun compte. L'adresse seulement : les secrets sont dans d'autres fichiers.

FUNCTION IsMe (who$)
    w$ = LCASE$(RTRIM$(LTRIM$(who$)))
    r = 0
    IF w$ = "moi" OR w$ = "me" OR w$ = "moi-même" OR w$ = "moi même" OR w$ = "moi-meme" OR w$ = "moi meme" THEN r = 1
    IF w$ = "à moi" OR w$ = "a moi" OR w$ = "myself" OR w$ = "mich" OR w$ = "mir" OR w$ = "ich" THEN r = 1
    IF r = 0 AND w$ <> "" AND FILEEXISTS("SD:/etc/memoire.txt") THEN
        f = FREEFILE
        OPEN "SD:/etc/memoire.txt" FOR INPUT AS #f
        DO WHILE NOT EOF(f)
            LINE INPUT #f, x$
            p = INSTR(x$, "=")
            IF p > 1 THEN
                IF LCASE$(RTRIM$(LTRIM$(LEFT$(x$, p - 1)))) = "nom" AND LCASE$(RTRIM$(LTRIM$(MID$(x$, p + 1)))) = w$ THEN r = 1
            END IF
        LOOP
        CLOSE #f
    END IF
    IsMe = r
END FUNCTION

FUNCTION MailOwnAddress$
    r$ = ""
    IF FILEEXISTS("SD:/etc/mail/account.ini") THEN
        f = FREEFILE
        OPEN "SD:/etc/mail/account.ini" FOR INPUT AS #f
        DO WHILE NOT EOF(f)
            LINE INPUT #f, x$
            p = INSTR(x$, "=")
            IF p > 1 AND LEFT$(LTRIM$(x$), 1) <> "#" THEN
                IF LCASE$(RTRIM$(LTRIM$(LEFT$(x$, p - 1)))) = "email" THEN r$ = RTRIM$(LTRIM$(MID$(x$, p + 1)))
            END IF
        LOOP
        CLOSE #f
    END IF
    IF INSTR(r$, "@") < 2 THEN r$ = ""
    MailOwnAddress$ = r$
END FUNCTION

' ---- cron : l'app relancee regulierement (« previens-moi quand la machine a fini ») -----------------------------
' L'outcome cron [every, 15, lavage, status] relance « lavage status » toutes les 15 minutes ; seules les actions
' declarees dans app.txt (« cron = status ») peuvent l'etre. Lancee par le cron, l'app n'ecoute pas et ne dit rien
' tant qu'il n'y a rien a dire ; CronStop arrete sa tache (sinon elle s'arrete seule apres 24 h).
FUNCTION ForCron
    IF FILEEXISTS("CRON:") THEN ForCron = 1 ELSE ForCron = 0
END FUNCTION

SUB CronStop
    f = FREEFILE
    OPEN "CRON:STOP" FOR OUTPUT AS #f
    CLOSE #f
END SUB

' ---- l'humeur : le visage de Pocket, peripherique MOOD: -------------------------------------------------------
' Deux axes, de -100 a 100 : la valence (negatif <-> positif) et l'eveil (calme <-> excite). Le neutre est au
' centre ; 16 humeurs sont sur un cercle (joyeux, rieur, malicieux, excite, curieux, surpris, perplexe, peur,
' colere, degout, mefiant, triste, ennui, fatigue, timide, fier), la distance au centre fait l'intensite. Le
' visage passe d'une humeur a l'autre en douceur et revient au neutre 10 s apres la derniere : une app qui veut
' tenir une humeur la repose. Lancee pour le LLM (askinfo), l'app ne change pas l'humeur.
'   SetMood 60, 40              (PocketQI Studio : bouton « Humeur... », le choix sur le plan avec l'apercu)
'   SetMoodName "joyeux", 2     une humeur nommee, intensite 1..3 (aussi amoureux, clin, ou en anglais : happy...)
SUB SetMood (valence, arousal)
    f = FREEFILE
    OPEN "MOOD:" FOR OUTPUT AS #f
    PRINT #f, STR$(valence) + " " + STR$(arousal)
    CLOSE #f
END SUB

' une humeur nommee (francais ou anglais : joyeux, happy, en colere, angry, amoureux, clin...), intensite 1..3
SUB SetMoodName (mood$, intensity)
    f = FREEFILE
    OPEN "MOOD:" FOR OUTPUT AS #f
    PRINT #f, mood$ + " " + STR$(intensity)
    CLOSE #f
END SUB

' l'humeur actuelle : ses deux axes (arrondis) et le nom de l'humeur la plus proche (« neutre » pres du centre)
FUNCTION MoodValence
    MoodValence = VAL(MoodField$(1))
END FUNCTION

FUNCTION MoodArousal
    MoodArousal = VAL(MoodField$(2))
END FUNCTION

FUNCTION MoodName$
    MoodName$ = MoodField$(3)
END FUNCTION

' (le k-ieme mot de "60 40 joyeux 2", MOOD: lu)
FUNCTION MoodField$ (k)
    t$ = ReadAll$("MOOD:")
    r$ = ""
    n = 0
    w$ = ""
    FOR i = 1 TO LEN(t$) + 1
        c$ = MID$(t$, i, 1)
        IF c$ = " " OR c$ = "" OR c$ = CHR$(10) OR c$ = CHR$(13) THEN
            IF w$ <> "" THEN
                n = n + 1
                IF n = k THEN r$ = w$
                w$ = ""
            END IF
        ELSE
            w$ = w$ + c$
        END IF
    NEXT
    MoodField$ = r$
END FUNCTION

' ---- envoyer au LLM : peripherique LLM: -----------------------------------------------------------------------
' SendLLM "mets du lait sur la liste de courses" : la phrase part au LLM a la FIN de l'app, comme si on l'avait dite
' a Pocket : il repond a voix haute et ses outcomes s'executent (lancer une app, l'humeur, retenir...). Plusieurs
' SendLLM : dans l'ordre. Lancee pour le LLM (askinfo), l'app ne peut pas lui renvoyer de phrase ; et au plus 3
' phrases s'enchainent ainsi (une app lancee par une phrase de SendLLM qui en renvoie une autre...), pas de boucle.
SUB SendLLM (prompt$)
    f = FREEFILE
    OPEN "LLM:" FOR OUTPUT AS #f
    PRINT #f, prompt$
    CLOSE #f
END SUB

' ---- l'ecran de Pocket : peripheriques SCREEN: et TOUCH: (Pocket dessine, system/modules/dessin) -------------------
' Une app dessine une scene, les memes commandes que l'outcome draw du LLM (une par ligne, mots en anglais, nombres
' en pixels de l'ecran de l'appareil, couleurs #rrggbb) : fill, point, line, rect, circle, ellipse, poly, text, icon,
' button, picture, clear. ScreenWidth / ScreenHeight : la taille de l'ecran (0 sans ecran) ; HasTouch : 1 s'il est
' tactile. Les surfaces prennent filled (1 plein, 0 le contour seul). La scene s'affiche d'un coup a ScreenShow et
' reste 15 s apres la fin de l'app (ScreenClose : le visage tout de suite). On peut aussi ecrire une commande soi-meme :
' PRINT #s, "rect 10 10 50 50 #ff0000 outline width=3".
'   s = ScreenBegin
'   DrawBackground s, "#0e1e3a"
'   DrawString s, ScreenWidth / 2, 20, "large", "#ffffff", "c", "Pile ou face ?"
'   DrawButton s, "pile", 20, ScreenHeight - 104, ScreenWidth - 40, 44, "#3d7bfd", "Pile"
'   ScreenShow s
'   b$ = WaitTouch$(30)      l'ID du bouton touche ("" hors bouton ou au bout de 30 s) ; TouchX, TouchY : ou
' Un clic dans la fenetre Visage de PocketQI Studio vaut un toucher. Ignore pour une app lancee par askinfo ou le cron.
FUNCTION GdNum$ (v)
    GdNum$ = LTRIM$(STR$(INT(v + .5)))
END FUNCTION

FUNCTION GdField$ (t$, k)
    r$ = ""
    n = 0
    w$ = ""
    FOR i = 1 TO LEN(t$) + 1
        c$ = MID$(t$, i, 1)
        IF c$ = " " OR c$ = "" OR c$ = CHR$(10) OR c$ = CHR$(13) THEN
            IF w$ <> "" THEN
                n = n + 1
                IF n = k THEN r$ = w$
                w$ = ""
            END IF
        ELSE
            w$ = w$ + c$
        END IF
    NEXT
    GdField$ = r$
END FUNCTION

FUNCTION GdFill$ (filled)
    IF filled THEN GdFill$ = " filled" ELSE GdFill$ = " outline"
END FUNCTION

FUNCTION ScreenWidth
    ScreenWidth = VAL(GdField$(ReadAll$("SCREEN:"), 1))
END FUNCTION

FUNCTION ScreenHeight
    ScreenHeight = VAL(GdField$(ReadAll$("SCREEN:"), 2))
END FUNCTION

FUNCTION HasTouch
    HasTouch = GdField$(ReadAll$("SCREEN:"), 4) = "touch"
END FUNCTION

FUNCTION ScreenBegin
    f = FREEFILE
    OPEN "SCREEN:" FOR OUTPUT AS #f
    ScreenBegin = f
END FUNCTION

SUB ScreenShow (s)
    CLOSE #s
END SUB

SUB ScreenClose
    f = FREEFILE
    OPEN "SCREEN:" FOR OUTPUT AS #f
    PRINT #f, "clear"
    CLOSE #f
END SUB

SUB DrawBackground (s, c$)
    PRINT #s, "fill " + c$
END SUB

SUB DrawPoint (s, x, y, c$, size)
    PRINT #s, "point " + GdNum$(x) + " " + GdNum$(y) + " " + c$ + " size=" + GdNum$(size)
END SUB

SUB DrawLine (s, x1, y1, x2, y2, c$, w)
    PRINT #s, "line " + GdNum$(x1) + " " + GdNum$(y1) + " " + GdNum$(x2) + " " + GdNum$(y2) + " " + c$ + " width=" + GdNum$(w)
END SUB

SUB DrawRect (s, x, y, w, h, c$, filled)
    PRINT #s, "rect " + GdNum$(x) + " " + GdNum$(y) + " " + GdNum$(w) + " " + GdNum$(h) + " " + c$ + GdFill$(filled)
END SUB

SUB DrawCircle (s, x, y, r, c$, filled)
    PRINT #s, "circle " + GdNum$(x) + " " + GdNum$(y) + " " + GdNum$(r) + " " + c$ + GdFill$(filled)
END SUB

SUB DrawEllipse (s, x, y, rx, ry, c$, filled)
    PRINT #s, "ellipse " + GdNum$(x) + " " + GdNum$(y) + " " + GdNum$(rx) + " " + GdNum$(ry) + " " + c$ + GdFill$(filled)
END SUB

' points$ : "x1 y1 x2 y2 x3 y3..." (3 a 8 points)
SUB DrawPoly (s, points$, c$, filled)
    PRINT #s, "poly " + c$ + GdFill$(filled) + " " + points$
END SUB

' size$ : small, medium, large, title ; align$ : l, c, r (autour de x) ; y : le haut du texte
SUB DrawString (s, x, y, size$, c$, align$, t$)
    PRINT #s, "text " + GdNum$(x) + " " + GdNum$(y) + " " + size$ + " " + c$ + " " + align$ + " " + t$
END SUB

' name$ : sun partly_cloudy cloud rain storm snow fog wind moon star heart house clock calendar music mail phone gift
' check cross question arrow_left arrow_right arrow_up arrow_down thermometer
SUB DrawIcon (s, name$, x, y, size)
    PRINT #s, "icon " + name$ + " " + GdNum$(x) + " " + GdNum$(y) + " " + GdNum$(size)
END SUB

SUB DrawButton (s, id$, x, y, w, h, c$, t$)
    PRINT #s, "button " + id$ + " " + GdNum$(x) + " " + GdNum$(y) + " " + GdNum$(w) + " " + GdNum$(h) + " " + c$ + " " + t$
END SUB

' un choix a toucher, place par Pocket : les DrawChoice d'une scene, dans l'ordre, empiles en bas de l'ecran ;
' c$ : "" pour la couleur suivante de la palette, ou "#rrggbb"
SUB DrawChoice (s, id$, c$, t$)
    IF c$ = "" THEN PRINT #s, "choice " + id$ + " " + t$ ELSE PRINT #s, "choice " + id$ + " " + c$ + " " + t$
END SUB

' le k-ieme element de "a|b|c" (sans les espaces autour) ; "" s'il n'y en a pas
FUNCTION GdItem$ (liste$, k)
    r$ = ""
    n = 1
    d = 1
    FOR i = 1 TO LEN(liste$) + 1
        IF i > LEN(liste$) OR MID$(liste$, i, 1) = "|" THEN
            IF n = k THEN r$ = LTRIM$(RTRIM$(MID$(liste$, d, i - d)))
            n = n + 1
            d = i + 1
        END IF
    NEXT
    GdItem$ = r$
END FUNCTION

' Choice$ : une question et des choix a toucher, separes par | ; chacun peut commencer par sa couleur (#rrggbb) :
'   c$ = Choice$("Quel animal a des vibrisses ?", "Chien|Chat|#ff9f40 Lapin", 30)
' rend le texte du choix touche ("Chat"), ou "" sans reponse au bout de seconds secondes (ou sans ecran). La scene
' reste affichee : l'app la remplace ou la ferme (ScreenClose). 6 choix au plus.
FUNCTION Choice$ (question$, choices$, seconds)
    r$ = ""
    IF ScreenWidth > 0 THEN
        s = ScreenBegin
        DrawBackground s, "#0e1e3a"
        DrawString s, ScreenWidth / 2, 20, "medium", "#ffffff", "c", question$
        FOR k = 1 TO 6
            it$ = GdItem$(choices$, k)
            IF it$ <> "" THEN
                c$ = ""
                IF LEFT$(it$, 1) = "#" AND INSTR(it$, " ") > 0 THEN
                    c$ = LEFT$(it$, INSTR(it$, " ") - 1)
                    it$ = LTRIM$(MID$(it$, INSTR(it$, " ") + 1))
                END IF
                DrawChoice s, "c" + GdNum$(k), c$, it$
            END IF
        NEXT
        ScreenShow s
        ' (un toucher hors des choix ne compte pas : on attend la suite du temps)
        t0 = TIMER
        b$ = ""
        DO
            reste = seconds - (TIMER - t0)
            IF reste < 1 THEN reste = 1
            b$ = WaitTouch$(reste)
        LOOP UNTIL b$ <> "" OR TIMER - t0 >= seconds
        IF LEFT$(b$, 1) = "c" THEN
            it$ = GdItem$(choices$, VAL(MID$(b$, 2)))
            IF LEFT$(it$, 1) = "#" AND INSTR(it$, " ") > 0 THEN it$ = LTRIM$(MID$(it$, INSTR(it$, " ") + 1))
            r$ = it$
        END IF
    END IF
    Choice$ = r$
END FUNCTION

' format$ : "png" ou "raw" (w * h * 3 octets RGB) ; data$ : le base64
SUB DrawPicture (s, x, y, w, h, format$, data$)
    PRINT #s, "picture " + GdNum$(x) + " " + GdNum$(y) + " " + GdNum$(w) + " " + GdNum$(h) + " format=" + format$ + " " + data$
END SUB

' l'ID du bouton touche ("" hors bouton, ou rien en seconds secondes ; 60 au plus ; 0 : sans attendre)
FUNCTION WaitTouch$ (seconds)
    WaitTouch$ = GdField$(ReadAll$("TOUCH:" + GdNum$(seconds)), 3)
END FUNCTION

' la position du dernier toucher rendu par WaitTouch$ (-1 : aucun)
FUNCTION TouchX
    t$ = ReadAll$("TOUCH:LAST")
    IF GdField$(t$, 1) = "" THEN TouchX = -1 ELSE TouchX = VAL(GdField$(t$, 1))
END FUNCTION

FUNCTION TouchY
    t$ = ReadAll$("TOUCH:LAST")
    IF GdField$(t$, 2) = "" THEN TouchY = -1 ELSE TouchY = VAL(GdField$(t$, 2))
END FUNCTION
