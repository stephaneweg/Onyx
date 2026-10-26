' main.bas -- app vocale « meteo » de Pocket (Onyx BASIC + galo.bi), donnees Open-Meteo (gratuit, sans cle)
'   meteo now [ville]               le temps qu'il fait maintenant
'   meteo day AAAA-MM-JJ [ville]    la prevision d'un jour (aujourd'hui .. six jours plus tard)
'   meteo week [ville]              les sept jours, un par ligne (pour que le LLM les montre : outcome draw)
' Sans ville : la variable « ville » de la memoire de Pocket (SD:/etc/memoire.txt, « ville = Lyon »).
' Lancee par askinfo (ForLLM, galo.bi) : le ressenti et le vent toujours, pour que le LLM conseille.
' Le web et le JSON : Web$, JsonRaw$, JsonText$ de galo.bi.

action$ = LCASE$(Arg$(1))
semaine = action$ = "week"
IF action$ = "day" THEN
    jour$ = Arg$(2)
    ville$ = Arg$(3)
ELSE
    jour$ = ""
    ville$ = Arg$(2)
END IF
IF LEFT$(ville$, 1) = "{" THEN ville$ = ""
IF ville$ = "" THEN ville$ = Memory$("ville")
IF ville$ = "" THEN
    Say "Je ne sais pas où tu habites. Dis-moi dans quelle ville, et je m'en souviendrai."
    END
END IF

' ---- la ville -> ses coordonnees ----
g$ = Web$("https://geocoding-api.open-meteo.com/v1/search?count=1&language=fr&format=json&name=" + ville$)
IF INSTR(g$, CHR$(34) + "results" + CHR$(34)) = 0 THEN
    IF g$ = "" THEN
        Say "Je n'arrive pas à joindre le service météo."
    ELSE
        Say "Je ne trouve pas la ville " + ville$ + "."
    END IF
    END
END IF
nom$ = JsonText$(g$, "name", 1)
lat$ = JsonRaw$(g$, "latitude", 1)
lon$ = JsonRaw$(g$, "longitude", 1)

' ---- la prevision ----
u$ = "https://api.open-meteo.com/v1/forecast?latitude=" + lat$ + "&longitude=" + lon$
' (une seule partie par requete : le chemin d'OPEN est limite a 240 caracteres ; 7 jours par defaut)
IF jour$ = "" AND NOT semaine THEN
    u$ = u$ + "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
ELSE
    u$ = u$ + "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,wind_speed_10m_max"
END IF
u$ = u$ + "&timezone=auto"
m$ = Web$(u$)
IF m$ = "" THEN Say "Je n'arrive pas à joindre le service météo.": END

IF semaine THEN
    ' les sept jours : « lundi 26 octobre : ciel dégagé, 8 à 17 degrés, pluie 10 %, vent 20 km/h »
    p = INSTR(m$, CHR$(34) + "daily" + CHR$(34) + ":")
    IF p = 0 THEN Say "Le service météo ne m'a pas répondu correctement.": END
    d$ = MID$(m$, p)
    s$ = "Météo à " + nom$ + " pour la semaine :"
    FOR i = 1 TO 7
        j$ = JsonText$(d$, "time", i)
        IF j$ <> "" THEN
            s$ = s$ + " " + SpokenDay$(j$) + " (" + j$ + ") : " + Weather$(VAL(JsonRaw$(d$, "weather_code", i)))
            s$ = s$ + ", " + LTRIM$(STR$(CINT(VAL(JsonRaw$(d$, "temperature_2m_min", i))))) + " à "
            s$ = s$ + LTRIM$(STR$(CINT(VAL(JsonRaw$(d$, "temperature_2m_max", i))))) + " degrés"
            s$ = s$ + ", pluie " + LTRIM$(STR$(VAL(JsonRaw$(d$, "precipitation_probability_max", i)))) + " %"
            s$ = s$ + ", vent " + LTRIM$(STR$(CINT(VAL(JsonRaw$(d$, "wind_speed_10m_max", i))))) + " km/h ;"
        END IF
    NEXT
    Say s$
ELSEIF jour$ = "" THEN
    p = INSTR(m$, CHR$(34) + "current" + CHR$(34) + ":")
    IF p = 0 THEN Say "Le service météo ne m'a pas répondu correctement.": END
    c$ = MID$(m$, p)
    t = VAL(JsonRaw$(c$, "temperature_2m", 1))
    r = VAL(JsonRaw$(c$, "apparent_temperature", 1))
    code = VAL(JsonRaw$(c$, "weather_code", 1))
    v = VAL(JsonRaw$(c$, "wind_speed_10m", 1))
    s$ = "À " + nom$ + ", " + Weather$(code) + ", " + Degrees$(t)
    IF ABS(r - t) >= 3 OR ForLLM THEN s$ = s$ + ", ressenti " + Degrees$(r)
    IF v >= 30 OR ForLLM THEN s$ = s$ + ", avec du vent à " + LTRIM$(STR$(CINT(v))) + " kilomètres heure"
    Say s$ + "."
ELSE
    IF IsDate(jour$) = 0 THEN Say "Je n'ai pas compris de quel jour tu parles.": END
    p = INSTR(m$, CHR$(34) + "daily" + CHR$(34) + ":")
    IF p = 0 THEN Say "Le service météo ne m'a pas répondu correctement.": END
    d$ = MID$(m$, p)
    k = 0
    FOR i = 1 TO 7
        IF JsonText$(d$, "time", i) = jour$ THEN k = i
    NEXT
    IF k = 0 THEN Say "Je n'ai les prévisions que pour les sept prochains jours.": END
    code = VAL(JsonRaw$(d$, "weather_code", k))
    tmax = VAL(JsonRaw$(d$, "temperature_2m_max", k))
    tmin = VAL(JsonRaw$(d$, "temperature_2m_min", k))
    pluie = VAL(JsonRaw$(d$, "precipitation_probability_max", k))
    s$ = SpokenDay$(jour$)
    s$ = UCASE$(LEFT$(s$, 1)) + MID$(s$, 2) + " à " + nom$ + " : " + Weather$(code)
    s$ = s$ + ", de " + Degrees$(tmin) + " à " + Degrees$(tmax)
    IF pluie >= 20 THEN
        s$ = s$ + ", " + LTRIM$(STR$(pluie)) + " pour cent de risque de pluie"
    ELSE
        s$ = s$ + ", sans pluie"
    END IF
    vmax = VAL(JsonRaw$(d$, "wind_speed_10m_max", k))
    IF vmax >= 30 OR ForLLM THEN s$ = s$ + ", vent jusqu'à " + LTRIM$(STR$(CINT(vmax))) + " kilomètres heure"
    Say s$ + "."
END IF
END

' ---- la memoire de Pocket : la valeur de « nom = valeur » dans SD:/etc/memoire.txt ----
FUNCTION Memory$ (var$)
    r$ = ""
    IF FILEEXISTS("SD:/etc/memoire.txt") THEN
        f = FREEFILE
        OPEN "SD:/etc/memoire.txt" FOR INPUT AS #f
        DO WHILE NOT EOF(f)
            LINE INPUT #f, l$
            p = INSTR(l$, "=")
            IF p > 0 AND LEFT$(l$, 1) <> "#" THEN
                IF LCASE$(RTRIM$(LTRIM$(LEFT$(l$, p - 1)))) = var$ THEN r$ = RTRIM$(LTRIM$(MID$(l$, p + 1)))
            END IF
        LOOP
        CLOSE #f
    END IF
    Memory$ = r$
END FUNCTION

' ---- le parle ----
FUNCTION Degrees$ (x)
    n = CINT(x)
    IF n < 0 THEN r$ = "moins " + LTRIM$(STR$(-n)) ELSE r$ = LTRIM$(STR$(n))
    IF ABS(n) > 1 THEN r$ = r$ + " degrés" ELSE r$ = r$ + " degré"
    Degrees$ = r$
END FUNCTION

' le code WMO -> le temps, en mots
FUNCTION Weather$ (c)
    SELECT CASE c
    CASE 0: r$ = "ciel dégagé"
    CASE 1: r$ = "plutôt dégagé"
    CASE 2: r$ = "partiellement nuageux"
    CASE 3: r$ = "couvert"
    CASE 45, 48: r$ = "du brouillard"
    CASE 51, 53, 55: r$ = "de la bruine"
    CASE 56, 57: r$ = "de la bruine verglaçante"
    CASE 61: r$ = "une pluie faible"
    CASE 63: r$ = "de la pluie"
    CASE 65: r$ = "une forte pluie"
    CASE 66, 67: r$ = "de la pluie verglaçante"
    CASE 71: r$ = "une neige faible"
    CASE 73: r$ = "de la neige"
    CASE 75: r$ = "une forte neige"
    CASE 77: r$ = "de la neige en grains"
    CASE 80: r$ = "quelques averses"
    CASE 81: r$ = "des averses"
    CASE 82: r$ = "de violentes averses"
    CASE 85, 86: r$ = "des averses de neige"
    CASE 95: r$ = "des orages"
    CASE 96, 99: r$ = "des orages avec de la grêle"
    CASE ELSE: r$ = "un temps incertain"
    END SELECT
    Weather$ = r$
END FUNCTION
